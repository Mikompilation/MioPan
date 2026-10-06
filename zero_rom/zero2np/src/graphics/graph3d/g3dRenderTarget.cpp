/* ==========================================================================
 *  g3dRenderTarget.cpp
 *
 *  CRenderTarget: a GS render-buffer wrapper.  The destination buffer is fully
 *  described by m_gsTex0 -- its TBP0 / TBW give the GS FRAME base+width, its
 *  PSM the pixel format, and the TW / TH log2 fields the pixel width/height.
 *
 *  Begin() reprograms the GS FRAME_1 / XYOFFSET_1 / SCISSOR_1 registers for
 *  this buffer, after capturing the previous values onto the m_AutoGsRegisters
 *  save stack; End() pops that stack to restore them.  Clear() rasterises a
 *  single flat-shaded sprite covering the whole target.
 *
 *  The CAutoGsRegisters / CAutoTransform Push/Pop bodies are the inlined
 *  g3dAutoState.h templates; only the render-target methods are written here.
 *
 *  Reconstructed from the Feb 6 2004 prototype (SLES_523.84).
 * ======================================================================== */

#include "g3dRenderTarget.h"
#include "g3dSprite.h"          /* CSprite (used by Clear) */
#include "g3dMath.h"            /* g3dLogi2 */
#include "g3ddbg.h"
#include <string.h>            /* memset */
#include <libgraph.h>

/* --------------------------------------------------------------------------
 *  IG3DCompatible static bridge pointers (wired to the g3d core at startup).
 * ------------------------------------------------------------------------ */
LPFUNC_SETGSREGISTER   IG3DCompatible::s_pFuncSetGsRegister;
LPFUNC_SETGSREGISTERS  IG3DCompatible::s_pFuncSetGsRegisters;
LPFUNC_GETGSREGISTERREF IG3DCompatible::s_pFuncGetGsRegisterRef;
LPFUNC_SETTRANSFORM    IG3DCompatible::s_pFuncSetTransform;
LPFUNC_GETTRANSFORMREF IG3DCompatible::s_pFuncGetTransformRef;

/* --------------------------------------------------------------------------
 *  CRenderTarget::CRenderTarget
 *
 *  Construct the two auto-state sub-objects (zeroing their stacks and saved
 *  data) and tag the render-target vtable.
 * ------------------------------------------------------------------------ */
CRenderTarget::CRenderTarget()
{
}

/* --------------------------------------------------------------------------
 *  CRenderTarget::~CRenderTarget
 *
 *  Tear-down pops both auto-state sub-objects, restoring any GS registers /
 *  transform left saved on their stacks.
 * ------------------------------------------------------------------------ */
CRenderTarget::~CRenderTarget()
{
    m_AutoTransformView.Pop();
    m_AutoGsRegisters.Pop();
}

/* --------------------------------------------------------------------------
 *  CRenderTarget::Create
 *
 *  Initialise the target from the creation data: the destination TEX0, the
 *  clear colour and the max depth value.
 * ------------------------------------------------------------------------ */
int CRenderTarget::Create(const RENDERTARGETCREATIONDATA *pCD)
{
    /* "pCD" */
    G3DASSERT(pCD, "");

    m_gsTex0     = pCD->gsTex0;
    m_ClearColor = pCD->ClearColor;
    m_fZMax      = pCD->fZMax;

    return 1;
}

/* --------------------------------------------------------------------------
 *  CRenderTarget::Begin
 *
 *  Make this target the GS draw destination: flush the texture cache, then
 *  build the FRAME_1 (base / buffer-width / pixel-format), XYOFFSET_1 (centre
 *  the 2048-pixel guard band on the buffer) and SCISSOR_1 (clip to the buffer
 *  extents) register values, push the previous values onto the auto-save stack
 *  and program the new ones.
 * ------------------------------------------------------------------------ */
void CRenderTarget::Begin(void)
{
    int          iWidth;
    int          iHeight;
    sceGifPackAd aGPA0[3];

    SetGsRegister(0, SCE_GS_TEXFLUSH, 1);

    iWidth  = GetWidth();
    iHeight = GetHeight();

    /* FRAME_1: FBP = TBP0 >> 5, FBW = width / 64, PSM from TEX0. */
    aGPA0[0].DATA = (u_long)(m_gsTex0.TBP0 >> 5)
                  | (long)(iWidth >> 6) << 0x10
                  | (u_long)m_gsTex0.PSM << 0x18;
    aGPA0[0].ADDR = SCE_GS_FRAME_1;

    /* XYOFFSET_1: centre the 0x800-pixel guard band on the buffer (12.4 fp). */
    aGPA0[1].DATA = (long)((0x800 - iWidth / 2) * 0x10)
                  | (long)((0x800 - iHeight / 2) * 0x10) << 0x20;
    aGPA0[1].ADDR = SCE_GS_XYOFFSET_1;

    /* SCISSOR_1: clip to [0, width-1] x [0, height-1]. */
    aGPA0[2].DATA = (long)(iWidth + -1) << 0x10
                  | (long)(iHeight + -1) << 0x30;
    aGPA0[2].ADDR = SCE_GS_SCISSOR_1;

    /* save the current FRAME/XYOFFSET/SCISSOR and program the new ones */
    m_AutoGsRegisters.Push(aGPA0);
}

/* --------------------------------------------------------------------------
 *  CRenderTarget::End
 *
 *  Restore the FRAME/XYOFFSET/SCISSOR registers saved by Begin().
 * ------------------------------------------------------------------------ */
void CRenderTarget::End(void)
{
    m_AutoGsRegisters.Pop();
}

/* --------------------------------------------------------------------------
 *  CRenderTarget::Clear (colour)
 *
 *  Clear the whole target by drawing a single flat-shaded, untextured sprite
 *  spanning the full buffer.  The buffer extents are taken from the TEX0
 *  TW / TH log2 fields; the sprite is centred so it exactly covers the buffer.
 * ------------------------------------------------------------------------ */
void CRenderTarget::Clear(G3DCOLOR Color)
{
    int           iHeight;
    int           iWidth;
    float         fScreenWidthHalf;
    float         fScreenHeightHalf;
    CSprite       sprite;
    G3DSPRITEDATA spritedata;

    iWidth  = 1 << (u_int)m_gsTex0.TW;
    iHeight = 1 << (u_int)m_gsTex0.TH;

    fScreenWidthHalf  = (float)iWidth  * 0.5f;
    fScreenHeightHalf = (float)iHeight * 0.5f;

    memset(&spritedata, 0, sizeof(G3DSPRITEDATA));

    spritedata.Color        = Color;
    spritedata.fZ           = m_fZMax;
    spritedata.Rect.fLeft   = fScreenWidthHalf  - (float)(iHeight / 2);
    spritedata.Rect.fTop    = fScreenHeightHalf - (float)(iHeight / 2);
    spritedata.Rect.fRight  = fScreenWidthHalf  + (float)(iWidth / 2);
    spritedata.Rect.fBottom = fScreenHeightHalf + (float)(iHeight / 2);

    sprite.Create(&spritedata, (CTexture *)0);
    sprite.Draw();
}

/* --------------------------------------------------------------------------
 *  CRenderTarget::Clear
 *
 *  Clear with the target's stored clear colour.
 * ------------------------------------------------------------------------ */
void CRenderTarget::Clear(void)
{
    Clear(m_ClearColor);
}

/* --------------------------------------------------------------------------
 *  CRenderTarget::SetWidth
 *
 *  Encode the pixel width (as log2) into the TEX0 TW field.
 * ------------------------------------------------------------------------ */
void CRenderTarget::SetWidth(int iWidth)
{
    int iLog2;

    iLog2 = g3dLogi2(2, iWidth);
    m_gsTex0.TW = iLog2 & 0xf;
}

/* --------------------------------------------------------------------------
 *  CRenderTarget::SetHeight
 *
 *  Encode the pixel height (as log2) into the TEX0 TH field.
 * ------------------------------------------------------------------------ */
void CRenderTarget::SetHeight(int iHeight)
{
    int iLog2;

    iLog2 = g3dLogi2(2, iHeight);
    m_gsTex0.TH = iLog2 & 0xf;
}

/* --------------------------------------------------------------------------
 *  CRenderTarget::GetWidth
 * ------------------------------------------------------------------------ */
int CRenderTarget::GetWidth(void)
{
    return 1 << (u_int)m_gsTex0.TW;
}

/* --------------------------------------------------------------------------
 *  CRenderTarget::GetHeight
 * ------------------------------------------------------------------------ */
int CRenderTarget::GetHeight(void)
{
    return 1 << (u_int)m_gsTex0.TH;
}
