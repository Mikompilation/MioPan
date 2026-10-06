/* ==========================================================================
 *  g3dSprite.h
 *
 *  CSprite: a single 2D screen-space quad drawn through the GS sprite (TRI2)
 *  primitive.  Create() bakes the caller's G3DSPRITEDATA (screen rect, colour,
 *  z, ST coords) into a ready-to-DMA GIF packet -- the VIF1 code + GIFTAG
 *  header, the packed RGBAQ colour and the two GS XYZ corners -- choosing the
 *  textured or untextured packet shape from whether a CTexture is bound.
 *  Draw() binds the texture, copies the prebuilt header into a DMA packet,
 *  appends the per-vertex GIF items and ships it.
 *
 *  Implemented in g3dSprite.cpp.
 *
 *  Reconstructed from the Feb 6 2004 prototype (SLES_523.84).
 * ======================================================================== */

#ifndef _G3DSPRITE_H
#define _G3DSPRITE_H

#include "eetypes.h"
#include "sce_gs.h"             /* sceGsXyz, sceGifPackRgbaq */
#include "gra3dTypes.h"         /* G3DSPRITEDATA, PACKETHEADER */
#include "g3dTexture.h"         /* CTexture */

/* --------------------------------------------------------------------------
 *  PACKETHEADER
 *
 *  The leading VIF1 code quadword + GIFTAG shared by both sprite packet shapes
 *  (textured / untextured).  Defined here as it is private to the sprite path.
 * ------------------------------------------------------------------------ */
struct PACKETHEADER          /* size 0x20 */
{
    qword         qwVifCode;   /* 0x00 */
    SCEGIFTAG_EOP Gt;          /* 0x10 */
};

/* GIF "PACKED" body for an untextured sprite (RGBAQ + two XYZF corners). */
struct GIFPACKET_WITHOUTTEXTURE          /* size 0x30 */
{
    sceGifPackRgbaq Rgbaq;     /* 0x00 */
    sceGifPackXyzf  XyzfLT;    /* 0x10 */
    sceGifPackXyzf  XyzfRB;    /* 0x20 */
};

/* GIF "PACKED" body for a textured sprite (RGBAQ + ST/XYZF per corner). */
struct GIFPACKET_WITHTEXTURE          /* size 0x50 */
{
    sceGifPackRgbaq Rgbaq;     /* 0x00 */
    sceGifPackSt    StLT;      /* 0x10 */
    sceGifPackXyzf  XyzfLT;    /* 0x20 */
    sceGifPackSt    StRB;      /* 0x30 */
    sceGifPackXyzf  XyzfRB;    /* 0x40 */
};

/* --------------------------------------------------------------------------
 *  CSprite
 * ------------------------------------------------------------------------ */
class CSprite
{
private:
    G3DSPRITEDATA   m_SpriteData;      /* 0x00 */
    PACKETHEADER    m_Header;          /* 0x30 */
    CTexture       *m_pTexture;        /* 0x50 */
    sceGifPackRgbaq m_Rgbaq;           /* 0x54 */
    sceGsXyz        m_GsXyzLT;         /* 0x68 */
    sceGsXyz        m_GsXyzRB;         /* 0x70 */
    int             m_iQWSizePacket;   /* 0x78 */

public:
    CSprite();

    int  Create(const G3DSPRITEDATA *pSpriteData, CTexture *pTexture);
    void Draw(void);
};

#endif /* _G3DSPRITE_H */
