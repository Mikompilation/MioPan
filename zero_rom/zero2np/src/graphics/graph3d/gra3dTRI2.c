/* ==========================================================================
 *  gra3dTRI2.c
 *
 *  TRI2 embedded-texture-file services.  A TRI2 file is a run of
 *  SGDTRI2FILEHEADER records; each is a VIF1 DIRECT packet whose payload is a
 *  GS image-transfer descriptor (a GIFTAG followed by BITBLTBUF / TRXPOS /
 *  TRXREG / TRXDIR A+D register items -- the sceGsLoadImage layout).  The
 *  texture pixels follow the header inline.
 *
 *  This module:
 *    - builds a TRI2 header for a VRAM page region (_MakeTRI2FileHeader,
 *      patched onto the s_TRI2FileHeaderDefault template),
 *    - reads back a VRAM page into a fresh TRI2 file (_MakeTRI2FileByVRAMImage
 *      / gra3dGenerateTRI2FileFromVRAM),
 *    - sizes a TRI2 file's VRAM footprint by walking its load-image
 *      descriptors (gra3dGetTRI2SizeData), and
 *    - uploads a TRI2 file to VRAM, either down the DMA chain or directly
 *      (gra3dLoadTRI2FileToVRAM).
 *
 *  The GS register-field accesses are done in their raw packed form (the
 *  descriptor is the on-disc sceGsLoadImage image with the DBP / DPSM / qwc
 *  bitfields where libgraph puts them); the bit masks below mirror the GS
 *  BITBLTBUF / TRXREG layout.
 *
 *  Reconstructed from the Feb 6 2004 prototype (SLES_523.84).
 * ======================================================================== */

#include "gra3dTRI2.h"
#include "gra3dSGD.h"           /* _SetPREVIOUSTRI2PRIM */
#include "miopan/rendering/miopan_graph3d.h"   /* host texture binding */
#include "g3dDma.h"             /* g3dDmaAddPacket */
#include "g3dGsWrapper.h"       /* g3dGsExecStoreImage / g3dGsSyncPath */
#include "sce_gs.h"             /* sceGsLoadImage / SCE_GS_BITBLTBUF */
#include "g3ddbg.h"
#include <eekernel.h>           /* FlushCache */
#include <libgraph.h>           /* sceGsSetDefStoreImage / sceGsExecLoadImage */
#include <algorithm>
#include "ctl/fixed_array.h"

/* Compiler-generated copies of _fixed_array_assert / _fixed_array_verifyrange<T>
 * (001c0050..001c0127) are the inlined ctl/fixed_array.h template; not emitted. */

/* DMA channels accepted by gra3dLoadTRI2FileToVRAM. */
#define SCE_DMA_VIF1            1
#define SCE_DMA_GIF             2

/* TRI2 texture VRAM window (in GS 256-word block units).  Textures live in
 * [TEX_VADR, EWRK_S_VADR); EWRK_S_VADR is the start of the edge-work area. */
#define TEX_VADR                0x2bc0
#define EWRK_S_VADR             0x3aa0

/* --------------------------------------------------------------------------
 *  The blank-header template every patched header is copied from, recovered
 *  byte-for-byte out of the ROM's .rodata at 0x3b86b0 (112 bytes).
 *
 *  Everything the GS needs that does not depend on the call is baked in here:
 *  the three leading NOP VIFcodes, the PACKED A+D GIFtag that carries the four
 *  register items, the three register addresses, BITBLTBUF.DBW = 1,
 *  TRXREG.RRW = 0x40 (a GS page is 64 texels wide), TRXDIR.XDR = 0
 *  (host -> local) and the trailing IMAGE GIFtag's EOP / FLG.  That is why
 *  _MakeTRI2FileHeader() below writes only four fields.
 *
 *  The fields it does write are poisoned here with 0xdeadbeef truncated to the
 *  field width, so an unpatched header stands out in a VRAM dump: the DIRECT
 *  VIFcode keeps all 32 bits, BITBLTBUF.DBP 14 of them (0x3eef) and the IMAGE
 *  GIFtag's NLOOP 15 (0x3eef, with bit 15 of 0xbeef landing on EOP, which
 *  wants to be 1 anyway).
 *
 *  TRXREG is the odd one out: the poison ended up in the 20-bit pad between
 *  RRW and RRH (0xdeadbeef -> 0xdbeef) and RRH itself came out 0, so the ROM's
 *  initialiser was one slot short of the field it meant to poison.  Harmless
 *  -- the GS ignores the pad and RRH is written at line 134 -- and reproduced
 *  because it is part of the ROM's byte image.
 * ------------------------------------------------------------------------ */
static const SGDTRI2FILEHEADER s_TRI2FileHeaderDefault =
{
    /* 0x00 */ 0,                                       /* NOP VIFcode      */
    /* 0x04 */ 0,                                       /* NOP VIFcode      */
    /* 0x08 */ 0,                                       /* NOP VIFcode      */
    /* 0x0c */ 0xdeadbeef,                              /* DIRECT -- 132    */
    /* 0x10 */ {
        /* one PACKED loop of four A+D register items */
        { .NLOOP = 1, .EOP = 0, .FLG = SCE_GIF_PACKED, .NREG = 4,
          .REGS0 = SCE_GIF_PACKED_AD, .REGS1 = SCE_GIF_PACKED_AD,
          .REGS2 = SCE_GIF_PACKED_AD, .REGS3 = SCE_GIF_PACKED_AD },
        { .DBP = 0x3eef, .DBW = 1 },                    /* DBP -- 133       */
        SCE_GS_BITBLTBUF,
        { },                                            /* TRXPOS 0,0       */
        SCE_GS_TRXPOS,
        { .RRW = 0x40, .pad12 = 0xdbeef },              /* RRH -- 134       */
        SCE_GS_TRXREG,
        { .XDR = 0 },                                   /* host -> local    */
        SCE_GS_TRXDIR,
        /* the IMAGE payload tag; NLOOP is the texel quadword count */
        { .NLOOP = 0x3eef, .EOP = 1, .FLG = SCE_GIF_IMAGE },
    },
};

/* --------------------------------------------------------------------------
 *  _MakeTRI2FileHeader
 *
 *  Copy the default TRI2 header template over pTRI2Head and patch the four
 *  fields that depend on the call, for a uiPageSize-page block at VRAM block
 *  address sVRAMAddress: the VIF1 DIRECT qwc (pages*0x200 texel quadwords plus
 *  the 6 register quadwords), the BITBLTBUF destination block address, the
 *  TRXREG transfer height (pages*0x20 lines of the template's 0x40 texels) and
 *  the trailing IMAGE GIFtag's NLOOP (the same pages*0x200 quadwords).
 *
 *  Everything else -- the NOP VIFcodes, the A+D GIFtag, the register addresses,
 *  DBW, RRW, XDR and the IMAGE tag's EOP / FLG -- comes from the template above
 *  and is deliberately not rewritten here; the ROM emits exactly four stores.
 *
 *  The masks GCC derives for the three bitfield writes (0x3fff, 0xfe0, 0x7e00)
 *  are the field widths narrowed by the known-zero low bits of each shift, not
 *  something the source spells out.
 * ------------------------------------------------------------------------ */
/* 126 */
static void _MakeTRI2FileHeader(SGDTRI2FILEHEADER *pTRI2Head, short int sVRAMAddress, unsigned int uiPageSize)
{
    /* 128 */ *pTRI2Head = s_TRI2FileHeaderDefault;

    /* 132 */ pTRI2Head->uiVif1Code_DIRECT  = SCE_VIF1_SET_DIRECT((uiPageSize << 9) + 6, 0);
    /* 133 */ pTRI2Head->gsli.bitbltbuf.DBP = sVRAMAddress;
    /* 134 */ pTRI2Head->gsli.trxreg.RRH    = uiPageSize << 5;
    /* 135 */ pTRI2Head->gsli.giftag1.NLOOP = uiPageSize << 9;
}

/* --------------------------------------------------------------------------
 *  _MakeTRI2FileByVRAMImage
 *
 *  Read uiPageSize VRAM pages at sVRAMAddress back into the texture body that
 *  follows pTRI2Head, then build the matching header.  The store-image is set
 *  up on the stack, the cache flushed, and the transfer run through the GS
 *  wrapper.
 * ------------------------------------------------------------------------ */
/* 150 */
static void _MakeTRI2FileByVRAMImage(SGDTRI2FILEHEADER *pTRI2Head, short int sVRAMAddress, unsigned int uiPageSize)
{
    sceGsStoreImage spi;

    /* 154 */ sceGsSetDefStoreImage(&spi, sVRAMAddress, 1, 0, 0, 0, 0x40, (int)(uiPageSize << 0x15) >> 0x10);
    /* 155 */ FlushCache(0);
    /* 156 */ g3dGsExecStoreImage(&spi, (u_long128 *)(pTRI2Head + 1));
    /* 159 */ g3dGsSyncPath(0, 0);

    /* 162 */ _MakeTRI2FileHeader(pTRI2Head, sVRAMAddress, uiPageSize);
}

/* --------------------------------------------------------------------------
 *  gra3dGetTRI2SizeData
 *
 *  Walk the load-image descriptors of a TRI2 file and accumulate, starting
 *  from pSDPrev, the texture region it occupies in VRAM: the min/max block
 *  address, the max TBP, and the byte size of the VRAM image.  The qwc field
 *  of the leading VIF1 DIRECT code gives the descriptor count; each descriptor
 *  is a BITBLTBUF/TRXREG block followed by qwc texel quadwords.
 * ------------------------------------------------------------------------ */
void gra3dGetTRI2SizeData(TRI2SIZEDATA *pSD, TRI2SIZEDATA *pSDPrev, SGDTRI2FILEHEADER *pTRI2Head)
{
    sceGsLoadImage          *pLI;
    unsigned int            uiNumDescriptor;
    unsigned int            uiDBP;
    unsigned int            uiQwc;
    unsigned int            uiPSM;

    pLI    = &pTRI2Head->gsli;
    TRI2SIZEDATA SDWork = *pSDPrev;

    /* qwc of the leading VIF1 DIRECT code, minus the leading 8 quadword
     * (VIF/GIF) header words -> number of load-image descriptors. */
    /* 188 */ uiNumDescriptor = pTRI2Head->GetTRI2Size() - 8;
    if (uiNumDescriptor < 2)
    {
        *pSD = SDWork;
        pSD->uiPageSize = SDWork.uiPageSize;
        return;
    }

    while (1 == 1)
    {
        sceGsBitbltbuf rgsBbb = pLI->bitbltbuf;
        unsigned int    uiTexSize;

        G3DASSERT(pLI->bitbltbufaddr == SCE_GS_BITBLTBUF, "");

        /* qwc of this descriptor (TRXREG-side, [0:14] of the qword at +0x50). */
        uiQwc = (unsigned int)(*(unsigned long *)((char *)pLI + 0x50)) & 0x7fff;

        /* BITBLTBUF.DBP (destination block address, bits [32:45]). */
        uiDBP = rgsBbb.DBP;

        if (uiDBP < SDWork.uiMinAddress)
        {
            SDWork.uiMinAddress = uiDBP;
        }

        if (SDWork.uiMaxTbp < uiDBP)
        {
            SDWork.uiMaxTbp = uiDBP;

            /* BITBLTBUF.DPSM (destination pixel format, bits [56:61]). */
            uiPSM = rgsBbb.DPSM;

            switch (uiPSM)
            {
                case 0x13:                          /* PSMT8  */
                case 0x14:                          /* PSMT4  */
                {
                    if ((*(unsigned long *)&rgsBbb & 0x1000000000000) != 0)
                    {
                        SDWork.uiVRAMTexSize = (uiQwc & 0x7fff) << 1;
                        break;
                    }
                    SDWork.uiVRAMTexSize = uiQwc & 0x7fff;
                    break;
                }
                default:
                {
                    SDWork.uiVRAMTexSize = uiQwc & 0x7fff;
                    break;
                }
            }

            /* max address = DBW base + (size in blocks). */
            SDWork.uiMaxAddress =
                (*(unsigned int *)((char *)pLI + 0x14) & 0x3fff) + (SDWork.uiVRAMTexSize >> 4);
        }

        /* advance past this descriptor's qwc texel quadwords + the 6-quadword
         * register block. */
        pLI = (sceGsLoadImage *)((char *)pLI + (uiQwc & 0x7fff) * 0x10 + 0x60);

        if (uiNumDescriptor <= (unsigned int)((char *)pLI - (char *)pTRI2Head) >> 4)
        {
            break;
        }
    }

    *pSD = SDWork;
    pSD->uiPageSize = SDWork.uiPageSize;
}

/* --------------------------------------------------------------------------
 *  _HostForEachLoadImage / _HostExecLoadImageChain             (host only)
 *
 *  One TRI2 header is a single VIF1 DIRECT packet, and that packet may carry
 *  several chained GS load-image descriptors: the furniture, door and effect
 *  models pack an indexed image and its CLUT as two descriptors in one header
 *  (128x128 PSMT8 at DBP 0x2bc0 followed by 16x16 PSMCT32 at 0x2c00, for
 *  instance).  gra3dGetTRI2SizeData() walks exactly this chain.
 *
 *  On the EE the whole packet went down the DMA chain and every descriptor
 *  executed; sceGsExecLoadImage() mirrors only the one descriptor it is handed,
 *  so walk the packet and mirror them all.  Uploading just the leading one left
 *  every paletted model sampling a CLUT that was never written -- RGBA 0,0,0,0,
 *  i.e. transparent, or black wherever blending was off.
 *
 *  The termination test is the one gra3dGetTRI2SizeData() uses: the DIRECT qwc
 *  less the 8 quadwords of leading VIF/GIF header bounds the descriptor list.
 * ------------------------------------------------------------------------ */
static void _HostForEachLoadImage(SGDTRI2FILEHEADER *pTRI2Head,
                                  void (*pfnImage)(sceGsLoadImage *, void *),
                                  void *pUser)
{
    unsigned int    uiNumDescriptor = pTRI2Head->GetTRI2Size();
    sceGsLoadImage *pLI             = &pTRI2Head->gsli;

    if (uiNumDescriptor < 8)
    {
        return;
    }
    uiNumDescriptor -= 8;

    for (;;)
    {
        /* giftag1.NLOOP is this descriptor's texel quadword count; the payload
         * sits immediately behind the 6-quadword register block. */
        unsigned int uiQwc = (unsigned int)pLI->giftag1.NLOOP & 0x7fff;

        pfnImage(pLI, pUser);

        pLI = (sceGsLoadImage *)((char *)pLI + uiQwc * 0x10 + 0x60);

        if (uiNumDescriptor <= (unsigned int)((char *)pLI - (char *)pTRI2Head) >> 4)
        {
            break;
        }
    }
}

static void _HostExecLoadImage(sceGsLoadImage *pLI, void *pUser)
{
    (void)pUser;
    sceGsExecLoadImage(pLI, (u_long128 *)(pLI + 1));
}

static void _HostExecLoadImageChain(SGDTRI2FILEHEADER *pTRI2Head)
{
    _HostForEachLoadImage(pTRI2Head, _HostExecLoadImage, NULL);
}

/* --------------------------------------------------------------------------
 *  gra3dHostForEachTRI2Image / gra3dHostLoadTRI2               (host only)
 *
 *  The same walk over a whole TRI2 unit -- iNumTexture headers, stepped as
 *  gra3dLoadTRI2FileToVRAM() steps them -- and the host half of that upload on
 *  its own.  The host texture binding uses the first to learn exactly what a
 *  model's TRI2s write, and the second to send one it skipped after all, when
 *  a later draw turns out to sample what that send would have left behind.
 * ------------------------------------------------------------------------ */
void gra3dHostForEachTRI2Image(int iNumTexture, SGDTRI2FILEHEADER *pTRI2HeadTop,
                               void (*pfnImage)(sceGsLoadImage *, void *),
                               void *pUser)
{
    SGDTRI2FILEHEADER *pTRI2HeadWork = pTRI2HeadTop;
    for (int i = 0; i < iNumTexture; i++)
    {
        unsigned int uiTRI2Size = pTRI2HeadWork->GetTRI2Size();

        _HostForEachLoadImage(pTRI2HeadWork, pfnImage, pUser);

        pTRI2HeadWork = (SGDTRI2FILEHEADER *)((char *)&pTRI2HeadWork->gsli + uiTRI2Size * 0x10);
    }
}

void gra3dHostLoadTRI2(int iNumTexture, SGDTRI2FILEHEADER *pTRI2HeadTop)
{
    gra3dHostForEachTRI2Image(iNumTexture, pTRI2HeadTop, _HostExecLoadImage, NULL);
}

/* --------------------------------------------------------------------------
 *  gra3dLoadTRI2FileToVRAM
 *
 *  Upload iNumTexture TRI2 headers (each one image-transfer packet) to VRAM.
 *  On the VIF1 channel the packet is added to the DMA chain; on the GIF
 *  channel it is run synchronously through sceGsExecLoadImage.  The
 *  destination address and size are bounds-checked against the texture window.
 * ------------------------------------------------------------------------ */
void gra3dLoadTRI2FileToVRAM(int iNumTexture, SGDTRI2FILEHEADER *pTRI2HeadTop, int iDmaChan)
{
    G3DASSERT(iDmaChan == SCE_DMA_VIF1 || iDmaChan == SCE_DMA_GIF, "iDmaChan:%d", iDmaChan);

    /* PORT: a model whose textures the host has already resolved no longer
     * needs its texels in emulated GS memory -- its draws are handed the
     * textures directly (see "Resident preset textures" in miopan_graph3d.cpp)
     * -- so only the host mirror below is skipped.  The EE side is untouched:
     * the packet is still built and the header still patched by the caller. */
    const int bHostBound = MioPan_Graph3dTextureUploadBound(pTRI2HeadTop);

    SGDTRI2FILEHEADER *pTRI2HeadWork = pTRI2HeadTop;
    for (int i = 0; i < iNumTexture; i++)
    {
        sceGsBitbltbuf &rBBB = ((sceGsLoadImage*)&pTRI2HeadWork->gsli)->bitbltbuf;

        /* qwc of this header's VIF1 DIRECT code. */
        /* 260 */ unsigned int uiTRI2Size = pTRI2HeadWork->GetTRI2Size();

        /* BITBLTBUF.DBP must lie inside the texture window. */
        G3DASSERT(TEX_VADR <= rBBB.DBP && rBBB.DBP < EWRK_S_VADR, "illegal VRAM address, DBP:%d", rBBB.DBP);

        /* The IMAGE tag's NLOOP is this header's texel quadword count; *16 is
         * its byte size and >>8 turns that into 256-byte GS blocks, which is
         * what DBP counts in.  (The template's RRW is a constant 0x40 and says
         * nothing about how much of VRAM the transfer covers.) */
        /* 265 */ int iSizeBlock = pTRI2HeadWork->gsli.giftag1.NLOOP << 4;
        /* 266 */ iSizeBlock >>= 8;

        G3DASSERT(rBBB.DBP + iSizeBlock <= EWRK_S_VADR, "illegal texture size, DBP:%d, iSizeBlock:%d", rBBB.DBP, iSizeBlock);

        if (iDmaChan == SCE_DMA_VIF1)
        {
            g3dDmaAddPacket(pTRI2HeadWork, uiTRI2Size + 1);
            /* Host mirror of the packet the EE handed to the DMA chain: every
             * descriptor in it, not just the first (see the helper above). */
            if (!bHostBound)
            {
                _HostExecLoadImageChain(pTRI2HeadWork);
            }
        }
        else
        {
            /* The GIF branch is the ROM's own single-descriptor transfer, and
             * no caller passes SCE_DMA_GIF -- left as the original wrote it. */
            FlushCache(0);
            sceGsExecLoadImage(&pTRI2HeadWork->gsli, (u_long128 *)(pTRI2HeadWork + 1));
            g3dGsSyncPath(0, 0);
        }

        pTRI2HeadWork = (SGDTRI2FILEHEADER *)((char *)&pTRI2HeadWork->gsli + uiTRI2Size * 0x10);
    }

    /* PORT: tell the host these texels are now in GS memory, which is when a
     * model's first draw may resolve them (see the note above). */
    if (!bHostBound && iDmaChan == SCE_DMA_VIF1)
    {
        MioPan_Graph3dNoteTextureUpload(pTRI2HeadTop);
    }

    /* PORT: LoadTRI2Files() has already latched this unit as the last one
     * sent, and will not send it again while it stays latched -- sound on the
     * PS2, where the texels are then still in VRAM, but not after a host skip,
     * which never put them there.  Unlatch it, exactly as the character draws
     * do after overwriting the window, so a model that stops being bound (a
     * monotone switch, the setting turned off) re-sends instead of sampling
     * whatever the window holds.  All it costs is another bound check. */
    if (bHostBound)
    {
        _SetPREVIOUSTRI2PRIM(NULL);
    }
}

/* --------------------------------------------------------------------------
 *  gra3dGenerateTRI2FileFromVRAM
 *
 *  Rebuild a TRI2 file from the current VRAM image: starting at the min block
 *  address, read back the texture window page-group by page-group (at most
 *  0x3f pages per descriptor), appending a header+body per group.  Returns the
 *  number of TRI2 headers written.
 * ------------------------------------------------------------------------ */
int gra3dGenerateTRI2FileFromVRAM(SGDTRI2FILEHEADER *pTRI2HeadTop, TRI2SIZEDATA *pSD)
{
    SGDTRI2FILEHEADER *pTRI2HeadWork = pTRI2HeadTop;
    int iNewNumTexture = 0;
    int minaddr = pSD->uiMinAddress;
    
    for (int tsize = pSD->uiPageSize; tsize > 0; iNewNumTexture++)
    {
        int tempMax = std::min(0x3f, tsize); // v1
        
        _MakeTRI2FileByVRAMImage(pTRI2HeadWork, minaddr, tempMax);
        pTRI2HeadWork = (SGDTRI2FILEHEADER *)((char*)pTRI2HeadWork + tempMax * 0x2000 + 0x70);
        tsize -= tempMax;
        minaddr += tempMax << 5;
    }
    
    return iNewNumTexture;
}
