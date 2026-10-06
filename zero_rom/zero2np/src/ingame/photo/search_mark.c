// FILE: /home/zero_rom/zero2np/src/ingame/photo/search_mark.c
//
// The off-screen "a ghost is over there" arrow.
//
// enemy.c aims it at whichever highlighted ghost is nearest and off screen, in
// degrees clockwise from the top of the finder, and fades it in and out as
// that ghost comes and goes.  One alpha ramp and one angle is the whole of it:
// the arrow is a single additive sprite spun about the centre of the finder,
// so the rotation carries it round the rim rather than turning it in place.
//
// Reconstructed from the Feb 6 2004 prototype (SLES_523.84), search_mark.o.
// All seven ZERO2.MAP .text symbols.

#include "m_plyr_camera.h"
#include "n_finder_dat.h"                       /* n_finder_dat            */
#include "../../graphics/graph2d/g2d_draw.h"    /* DISP_SPRT / DispSprD    */

/* The arrow itself. */
#define FD_SEARCH_MARK      0x83

/* The finder's centre of rotation, in finder-local coordinates -- the same
 * point spirit_gage.o spins its ring about, one pixel up and left of the
 * screen centre. */
#define SM_CENTER_X         319
#define SM_CENTER_Y         225

/* The ramps.  Twenty a frame up, twenty down; at a 128 ceiling that is a
 * seven-frame fade either way. */
#define SM_FADE_IN_SPD      20
#define SM_FADE_OUT_SPD   (-20)

/* Additive, and depth-masked like the rest of the finder HUD. */
#define SM_ZBUF_NO_WRITE    0x000000010a000118ULL
#define SM_ALPHA_ADD        0x48

void CSearchMark::Init(void)
{
    mAlpha.Init();
}

void CSearchMark::Release(void)                                         /* 8 */
{
}

void CSearchMark::Work(void)                                            /* 10 */
{
    mAlpha.Work();                                                      /* 11 */
}

void CSearchMark::FadeIn(void)
{
    mAlpha.SetAddVal(SM_FADE_IN_SPD);
}

void CSearchMark::FadeOut(void)
{
    mAlpha.SetAddVal(SM_FADE_OUT_SPD);
}

void CSearchMark::SetRot(float fRot)                                    /* 22 */
{
    mRot = fRot;
}

void CSearchMark::Draw(int fndr_mx, int fndr_my, int iAlpha, float fScale)
{                                                                       /* 25 */
    DISP_SPRT ds;

    CopySprDToSpr(&ds, &n_finder_dat[FD_SEARCH_MARK]);                  /* 30 */
    ds.zbuf   = SM_ZBUF_NO_WRITE;                                       /* 31 */
    ds.alphar = SM_ALPHA_ADD;                                           /* 32 */
    ds.alpha  = (u_char)(iAlpha * mAlpha.Get() / mAlpha.GetMax());      /* 33 */

    ds.x  += (float)fndr_mx;   ds.y   += (float)fndr_my;                /* 34 */
    ds.crx = (float)(fndr_mx + SM_CENTER_X);
    ds.cry = (float)(fndr_my + SM_CENTER_Y);
    ds.rot = mRot;                                                      /* 35 */
    ds.csx = ds.crx;           ds.csy  = ds.cry;
    ds.scw = fScale;           ds.sch  = fScale;                        /* 36 */

    DispSprD(&ds);                                                      /* 37 */
}
