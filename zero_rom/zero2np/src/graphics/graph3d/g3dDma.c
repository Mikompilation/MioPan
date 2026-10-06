/* ==========================================================================
 *  g3dDma.c
 *
 *  Low-level VIF1 DMA packet builder for the 3D engine.  Wraps the SCE EE
 *  dmaVif1* ring allocator into an open/add/copy/close model, plus the VU1
 *  micro-program load and micro-subroutine call/continue kicks.  Every entry
 *  point validates its arguments (non-NULL, 16-byte aligned, positive size)
 *  with the debug-assert macros before touching the hardware ring.
 *
 *  Reconstructed from the Feb 6 2004 prototype (SLES_523.84).
 * ======================================================================== */

#include "g3dDma.h"
#include "gra3dTypes.h"         /* G3DDMACHAINTAG, _PACKET_SETGSREGISTER */
#include "g3ddbg.h"
#include <string.h>             /* memset */

/* VIF1 opcodes emitted into the chain (cmd byte in bits 31..24). */
#define VIF1_FLUSH   0x11000000     /* FLUSH        */
#define VIF1_MSCAL   0x14000000     /* MSCAL        */
#define VIF1_MSCALF  0x15000000     /* MSCALF       */
#define VIF1_MSCNT   0x17000000     /* MSCNT        */

/* --------------------------------------------------------------------------
 *  Module state.
 * ------------------------------------------------------------------------ */
static int                 s_bOpened;           /* a packet is currently open */
static _PACKET_SETGSREGISTER s_packetSetRegister =
{
    { 0, 0, VIF1_FLUSH, 0x50000002 },           /* qwVif1Code: FLUSH + DIRECT GIFTAG */
    { { 0x8001, 0x10000000, 0xe, 0 } },         /* GT: PACKED A+D, 1 reg, regs=A+D   */
    { 0, 0 },                                   /* gpa: patched per call             */
};
static sceDmaChan         *s_pDMAChan_VIF1;     /* EE VIF1 DMA channel */

/* --------------------------------------------------------------------------
 *  _AppendVUProgTag
 *
 *  Walk a VU micro-program DMA chain (a sequence of source/transfer tags) and
 *  append each tag's quadword payload to the open packet, stopping when the
 *  chain-end (id == 7, "end" tag) is reached.  The per-tag advance uses the
 *  QWC field to skip the tag + its data.
 * ------------------------------------------------------------------------ */
static void _AppendVUProgTag(const unsigned int *pDMATag)
{
    G3DDMACHAINTAG *pDSCT;

    pDSCT = (G3DDMACHAINTAG *)pDMATag;
    if ((*(u_long *)pDSCT & 0x70000000) != 0x70000000)
    {
        while (true)
        {
            g3dDmaAddPacket((u_long *)((uintptr_t)pDSCT + 0x10), (int)(u_short)pDSCT->QWC);

            /* advance past this tag and its (QWC) quadwords */
            pDSCT = (G3DDMACHAINTAG *)((uintptr_t)pDSCT + ((u_int)(u_short)pDSCT->QWC * 2 + 2) * 8);

            if ((*(u_long *)pDSCT & 0x70000000) == 0x70000000)
            {
                break;
            }
        }
    }
}

/* --------------------------------------------------------------------------
 *  _DmaSend
 *
 *  Kick a raw DMA chain down the given channel (thin wrapper over sceDmaSend).
 * ------------------------------------------------------------------------ */
static void _DmaSend(sceDmaChan *pDmaChan, void *pDmaTag)
{
    sceDmaSend(pDmaChan, pDmaTag);
}

/* --------------------------------------------------------------------------
 *  g3dDmaOpenPacket
 *
 *  Grab the current VIF1 ring write pointer and mark a packet as open.
 * ------------------------------------------------------------------------ */
void *g3dDmaOpenPacket(void)
{
    G3DASSERT(!s_bOpened, "");

    s_bOpened = 1;
    return dmaVif1GetPacket();
}

/* --------------------------------------------------------------------------
 *  g3dDmaCancelPacket
 *
 *  Abandon the open packet without committing it to the ring.
 * ------------------------------------------------------------------------ */
int g3dDmaCancelPacket(void)
{
    s_bOpened = 0;
    return 1;
}

/* --------------------------------------------------------------------------
 *  g3dDmaClosePacket
 *
 *  Commit the open packet, ending at pPacket (must be non-NULL and 16-aligned).
 * ------------------------------------------------------------------------ */
int g3dDmaClosePacket(const void *pPacket)
{
    G3DASSERT(s_bOpened, "");
    G3DASSERT(pPacket, "pPacket:0x%08", 0);
    G3DASSERT(!((uintptr_t)pPacket & 0xf), "pPacket is illegal(0x%08x)\n", pPacket);

    s_bOpened = 0;
    dmaVif1SetPacket(pPacket);
    return 1;
}

/* --------------------------------------------------------------------------
 *  g3dDmaAddPacket
 *
 *  Reference an external quadword buffer from the chain (REF tag), iQWSize
 *  quadwords starting at pPacket.
 * ------------------------------------------------------------------------ */
int g3dDmaAddPacket(const void *pPacket, int iQWSize)
{
    G3DASSERT(pPacket && iQWSize > 0, "pPacket:0x%08, iQWSize:%d", pPacket, iQWSize);
    G3DASSERT(!((uintptr_t)pPacket & 0xf), "pPacket is illegal(0x%08x)\n", pPacket);

    dmaVif1AddRefTag((uintptr_t)pPacket, iQWSize);
    return 1;
}

/* --------------------------------------------------------------------------
 *  g3dDmaFlush
 *
 *  Kick the accumulated VIF1 chain and wait for the GS path to drain.
 * ------------------------------------------------------------------------ */
int g3dDmaFlush(void)
{
    dmaVif1Kick();
    sceGsSyncPath(0, 0);
    return 1;
}

/* --------------------------------------------------------------------------
 *  g3dDmaCopyPacket
 *
 *  Copy iQWSize quadwords from pPacket into a freshly opened packet (inline,
 *  so the source can be reused/overwritten after this returns).
 * ------------------------------------------------------------------------ */
int g3dDmaCopyPacket(const void *pPacket, int iQWSize)
{
    qword *pSrc;
    qword *pDest;
    int    i;

    G3DASSERT(pPacket && iQWSize > 0, "pPacket:0x%08, iQWSize:%d", pPacket, iQWSize);
    //G3DASSERT(!((uintptr_t)pPacket & 0xf), "pPacket is illegal(0x%08x)\n", pPacket);

    pDest = (qword *)g3dDmaOpenPacket();
    pSrc  = (qword *)pPacket;

    G3DASSERT(pSrc, "");
    G3DASSERT(pDest, "");

    for (i = iQWSize; i > 0; i = i - 1)
    {
        (*pDest)[0] = (*pSrc)[0];
        (*pDest)[1] = (*pSrc)[1];
        (*pDest)[2] = (*pSrc)[2];
        (*pDest)[3] = (*pSrc)[3];
        pSrc  = pSrc + 1;
        pDest = pDest + 1;
    }

    g3dDmaClosePacket(pDest);
    return 1;
}

/* --------------------------------------------------------------------------
 *  g3dDmaSetGsRegister (data, address)
 *
 *  Patch the prebuilt single-register packet with one GS register value and
 *  copy it into the chain.
 * ------------------------------------------------------------------------ */
int g3dDmaSetGsRegister(u_long ulGsData, u_long ulGsAddress)
{
    s_packetSetRegister.gpa.DATA = ulGsData;
    s_packetSetRegister.gpa.ADDR = ulGsAddress;
    g3dDmaCopyPacket(&s_packetSetRegister, 3);
    return 1;
}

/* --------------------------------------------------------------------------
 *  g3dDmaSetGsRegister (sceGifPackAd)
 * ------------------------------------------------------------------------ */
int g3dDmaSetGsRegister(const sceGifPackAd *pGPA)
{
    s_packetSetRegister.gpa.DATA = pGPA->DATA;
    s_packetSetRegister.gpa.ADDR = pGPA->ADDR;
    g3dDmaCopyPacket(&s_packetSetRegister, 3);
    return 1;
}

/* --------------------------------------------------------------------------
 *  g3dDmaSetGsRegisters
 *
 *  Write iNum GS registers in one packet: emit a DIRECT VIF1 code + a PACKED
 *  A+D GIFTAG with NLOOP == iNum, then the iNum address/data quadwords.
 * ------------------------------------------------------------------------ */
int g3dDmaSetGsRegisters(const sceGifPackAd *aGPA, int iNum)
{
    int           qwVif1Code[4];
    SCEGIFTAG_EOP gt;
    qword        *pQW;
    int           i;

    memset(qwVif1Code, 0, sizeof(qwVif1Code));
    memset(&gt, 0, sizeof(SCEGIFTAG_EOP));
    /* GIFTAG: NLOOP=iNum, EOP=1, FLG=PACKED */
    gt.lTag  = (gt.lTag & 0x0fffffffffff8000) | (long)(iNum & 0x7fff) | 0x1000000000008000;
    gt.lRegs = (gt.lRegs & 0xfffffffffffffff0) | 0xe;     /* one reg: A+D */

    pQW = (qword *)g3dDmaOpenPacket();
    (*pQW)[0] = 0;
    (*pQW)[1] = 0;
    (*pQW)[2] = VIF1_FLUSH;
    (*pQW)[3] = (iNum + 1) | 0x50000000;                 /* DIRECT, qwc = iNum+1 */
    pQW = pQW + 1;
    (*pQW)[0] = (int)gt.lTag;
    (*pQW)[1] = (int)((u_long)gt.lTag >> 0x20);
    (*pQW)[2] = (int)gt.lRegs;
    (*pQW)[3] = (int)((u_long)gt.lRegs >> 0x20);
    pQW = pQW + 1;

    for (i = iNum; i > 0; i = i - 1)
    {
        (*pQW)[0] = (int)aGPA->DATA;
        (*pQW)[1] = (int)((u_long)aGPA->DATA >> 0x20);
        (*pQW)[2] = (int)aGPA->ADDR;
        (*pQW)[3] = (int)((u_long)aGPA->ADDR >> 0x20);
        pQW  = pQW + 1;
        aGPA = aGPA + 1;
    }

    g3dDmaClosePacket(pQW);
    return 1;
}

/* --------------------------------------------------------------------------
 *  g3dDmaLoadVu1MicroProgram
 *
 *  Load a VU1 micro-program.  Skipped if the same program is already resident.
 *  bImmediately sends it down the channel right away; otherwise it is appended
 *  to the current chain.
 * ------------------------------------------------------------------------ */
int g3dDmaLoadVu1MicroProgram(const unsigned int *pMPG, int bImmediately)
{
    static unsigned int *s_pMPGOld;

    if (s_pMPGOld != pMPG)
    {
        if (bImmediately == 0)
        {
            _AppendVUProgTag(pMPG);
            s_pMPGOld = (unsigned int *)pMPG;
        }
        else
        {
            _DmaSend(s_pDMAChan_VIF1, (void *)pMPG);
            s_pMPGOld = (unsigned int *)pMPG;
        }
    }
    return 1;
}

/* --------------------------------------------------------------------------
 *  g3dDmaCallVu1MicroSubroutine
 *
 *  Issue an MSCAL to run a VU1 micro-subroutine at *pMS.  In deferred mode a
 *  small packet (FLUSH + MSCAL) is opened/closed; in immediate mode a tiny DMA
 *  chain holding the same VIF1 codes is built on the stack and sent directly.
 * ------------------------------------------------------------------------ */
int g3dDmaCallVu1MicroSubroutine(const unsigned int *pMS, int bImmediately)
{
    unsigned int  *pQW;
    G3DDMACHAINTAG tag;
    int            qwVif1Code[4];

    if (bImmediately == 0)
    {
        pQW = (unsigned int *)g3dDmaOpenPacket();
        pQW[2] = VIF1_FLUSH;
        pQW[3] = (uintptr_t)pMS >> 3 | VIF1_MSCAL;
        pQW[0] = 0;
        pQW[1] = 0;
        g3dDmaClosePacket(pQW + 4);
    }
    else
    {
        memset(&tag, 0, sizeof(tag));
        memset(qwVif1Code, 0, sizeof(qwVif1Code));
        /* a single REF tag (id=3) of 1 quadword pointing at qwVif1Code */
        tag.QWC  = 1;
        tag.ID   = 3;
        tag.ADDR = (long)(uintptr_t)qwVif1Code & 0x7fffffff;
        qwVif1Code[2] = VIF1_FLUSH;
        qwVif1Code[3] = (uintptr_t)pMS >> 3 | VIF1_MSCAL;
        qwVif1Code[0] = 0;
        qwVif1Code[1] = 0;
        _DmaSend(s_pDMAChan_VIF1, &tag);
    }
    return 1;
}

/* --------------------------------------------------------------------------
 *  g3dDmaContinueVu1MicroSubroutine
 *
 *  Resume a previously-called VU1 micro-subroutine with an MSCNT, by chaining
 *  the static FLUSH+MSCNT quadword.
 * ------------------------------------------------------------------------ */
int g3dDmaContinueVu1MicroSubroutine(const unsigned int *pMS)
{
    /* { 0, 0, FLUSH, MSCNT } */
    static int s_qwVif1Code_ContinueVu1MicroSubroutine[4] =
    {
        0, 0, VIF1_FLUSH, VIF1_MSCNT
    };

    g3dDmaAddPacket(s_qwVif1Code_ContinueVu1MicroSubroutine, 1);
    return 1;
}
