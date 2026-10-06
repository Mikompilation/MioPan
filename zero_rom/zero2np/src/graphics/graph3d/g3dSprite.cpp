/* ==========================================================================
 *  g3dSprite.cpp
 *
 *  CSprite: a 2D screen-space quad drawn through the GS sprite (TRI2)
 *  primitive.  Create() converts the caller's G3DSPRITEDATA into a prebuilt
 *  GIF packet -- a VIF1 DIRECT code + GIFTAG header followed by the per-vertex
 *  PACKED items -- selecting the textured / untextured packet shape from
 *  whether a texture is bound, and converts the two screen corners to GS XYZ
 *  primitive coordinates once up front.  Draw() then streams that packet down
 *  the DMA chain each frame.
 *
 *  Reconstructed from the Feb 6 2004 prototype (SLES_523.84).
 * ======================================================================== */

#include "g3dSprite.h"
#include "g3dCore.h"           /* g3dSetTexture / g3dCalcGsPrimitiveCoord */
#include "g3dDma.h"            /* g3dDmaOpenPacket / g3dDmaClosePacket */
#include "g3ddbg.h"
#include <string.h>           /* memset */

/* VIF1 FLUSH opcode (cmd byte 0x11) emitted ahead of the DIRECT GIFTAG. */
#define VIF1_FLUSH              0x11000000

/* --------------------------------------------------------------------------
 *  CSprite::CSprite
 *
 *  Clear the prebuilt packet header.
 * ------------------------------------------------------------------------ */
CSprite::CSprite()
{
    memset(&m_Header, 0, sizeof(m_Header));
}

/* --------------------------------------------------------------------------
 *  CSprite::Create
 *
 *  Bake the sprite's GIF packet from pSpriteData.  When pTexture is NULL the
 *  packet is RGBAQ + two XYZF corners (5 qwords incl. header); when textured
 *  it is RGBAQ + ST/XYZF per corner (7 qwords).  The header is a VIF1 DIRECT
 *  code plus a PACKED GIFTAG (NLOOP=1, NREG/REGS describing the layout); the
 *  two screen corners are pre-converted to GS XYZ primitive coordinates.
 * ------------------------------------------------------------------------ */
int CSprite::Create(const G3DSPRITEDATA *pSpriteData, CTexture *pTexture)
{
    int   iPrim;
    int   iNreg;
    int   iRegs1;
    int   iRegs3;
    int   iRegs4;
    float vScreenCoordLT[4];
    float vScreenCoordRB[4];

    /* "pSpriteData" */
    G3DASSERT(pSpriteData, "");

    m_SpriteData = *pSpriteData;
    m_pTexture   = pTexture;

    iRegs3 = 0;
    iRegs4 = 0;

    if (pTexture == (CTexture *)0)
    {
        iNreg            = 6;
        m_iQWSizePacket  = 5;
        iRegs1           = 3;
        iPrim            = 4;
    }
    else
    {
        iNreg            = 0x16;
        m_iQWSizePacket  = 7;
        iRegs1           = 5;
        iPrim            = 2;
        iRegs3           = 2;
        iRegs4           = 4;
    }

    /* VIF1: NOP, NOP, FLUSH, then DIRECT (qwc = packet size - 1). */
    m_Header.qwVifCode[0] = 0;
    m_Header.qwVifCode[1] = 0;
    m_Header.qwVifCode[2] = VIF1_FLUSH;
    m_Header.qwVifCode[3] = (m_iQWSizePacket - 1) | 0x50000000;

    /* unpack the packed colour into the RGBAQ item */
    m_Rgbaq.R = (u_int)(unsigned char)(m_SpriteData.Color);
    m_Rgbaq.G = (u_int)(unsigned char)(m_SpriteData.Color >> 8);
    m_Rgbaq.B = (u_int)(unsigned char)(m_SpriteData.Color >> 0x10);
    m_Rgbaq.A = (u_int)(unsigned char)(m_SpriteData.Color >> 0x18);

    /* GIFTAG: NLOOP=1, PRIM enable, PRIM word, NREG and the per-register
     * descriptors selected above. */
    m_Header.Gt.lTag = (m_Header.Gt.lTag & 0xfffffffffff00000)
                     | 1
                     | (long)iPrim << 4
                     | 0x400
                     | (long)iRegs3 << 0xc
                     | (long)iRegs4 << 0x10;
    m_Header.Gt.lRegs = (m_Header.Gt.lRegs & 0x7fffffff8000)
                      | 0x400000008001
                      | (long)iNreg << 0x2f
                      | (long)iRegs1 << 0x3c;

    /* pre-convert the two screen corners to GS XYZ primitive coordinates */
    vScreenCoordLT[0] = m_SpriteData.Rect.fLeft;
    vScreenCoordLT[1] = m_SpriteData.Rect.fTop;
    vScreenCoordLT[2] = m_SpriteData.fZ;
    g3dCalcGsPrimitiveCoord(&m_GsXyzLT, vScreenCoordLT);

    vScreenCoordRB[0] = m_SpriteData.Rect.fRight;
    vScreenCoordRB[1] = m_SpriteData.Rect.fBottom;
    vScreenCoordRB[2] = m_SpriteData.fZ;
    g3dCalcGsPrimitiveCoord(&m_GsXyzRB, vScreenCoordRB);

    return 1;
}

/* --------------------------------------------------------------------------
 *  CSprite::Draw
 *
 *  Bind the sprite's texture, open a DMA packet, copy the prebuilt header into
 *  it, fill in the per-vertex PACKED items (untextured: RGBAQ + two XYZF;
 *  textured: RGBAQ + ST/XYZF per corner, with Q forced to 1.0) and close the
 *  packet at the computed quadword length.
 * ------------------------------------------------------------------------ */
void CSprite::Draw(void)
{
    PACKETHEADER *pHeader;

    g3dSetTexture(0, m_pTexture);

    pHeader = (PACKETHEADER *)g3dDmaOpenPacket();

    /* copy the prebuilt VIF1 code + GIFTAG header (two quadwords) */
    *pHeader = m_Header;

    if (m_pTexture == (CTexture *)0)
    {
        GIFPACKET_WITHOUTTEXTURE *pGifPacket = (GIFPACKET_WITHOUTTEXTURE *)(pHeader + 1);

        memset(pGifPacket, 0, sizeof(GIFPACKET_WITHOUTTEXTURE));

        pGifPacket->Rgbaq        = m_Rgbaq;
        pGifPacket->XyzfLT.X     = (int)(u_short)m_GsXyzLT.X;
        pGifPacket->XyzfLT.Y     = (int)(u_short)m_GsXyzLT.Y;
        pGifPacket->XyzfLT.Z     = (u_int)m_GsXyzLT.Z << 4;
        pGifPacket->XyzfRB.X     = (int)(u_short)m_GsXyzRB.X;
        pGifPacket->XyzfRB.Y     = (int)(u_short)m_GsXyzRB.Y;
        pGifPacket->XyzfRB.Z     = (u_int)m_GsXyzRB.Z << 4;
    }
    else
    {
        GIFPACKET_WITHTEXTURE *pGifPacket = (GIFPACKET_WITHTEXTURE *)(pHeader + 1);

        memset(pGifPacket, 0, sizeof(GIFPACKET_WITHTEXTURE));

        pGifPacket->Rgbaq        = m_Rgbaq;
        pGifPacket->StLT.S       = m_SpriteData.StLT.S;
        pGifPacket->StLT.T       = m_SpriteData.StLT.T;
        pGifPacket->StLT.Q       = 1.0f;
        pGifPacket->XyzfLT.X     = (int)(u_short)m_GsXyzLT.X;
        pGifPacket->XyzfLT.Y     = (int)(u_short)m_GsXyzLT.Y;
        pGifPacket->XyzfLT.Z     = (u_int)m_GsXyzLT.Z << 4;
        pGifPacket->StRB.S       = m_SpriteData.StRB.S;
        pGifPacket->StRB.T       = m_SpriteData.StRB.T;
        pGifPacket->StRB.Q       = 1.0f;
        pGifPacket->XyzfRB.X     = (int)(u_short)m_GsXyzRB.X;
        pGifPacket->XyzfRB.Y     = (int)(u_short)m_GsXyzRB.Y;
        pGifPacket->XyzfRB.Z     = (u_int)m_GsXyzRB.Z << 4;
    }

    g3dDmaClosePacket((qword *)pHeader + m_iQWSizePacket);
}
