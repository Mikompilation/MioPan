// FILE: /home/zero_rom/zero2np/src/ingame/photo/hp_bar.c
//
// The player's health bar, along the right-hand edge of the viewfinder.
//
// Five sprites: two plate pieces, a full-height frame, and two overlays whose
// vertical scale carries the fill.  All three of the scaled ones are drawn
// with a *negative* sch so they grow upward from a common baseline at y 389.
//
// The drawn value chases the real one rather than tracking it -- disp_hp_per
// moves at most HB_CHASE_STEP per frame -- which is what makes a hit bleed the
// bar down instead of cutting it.  SyncHpBar() is the way out of that: it
// snaps the drawn value onto the real one, and Init() and the item code call
// it so the bar is never seen animating up from wherever it was left.
//
// Init(), FadeIn() and FadeOut() are inline in hp_bar.h and are reconstructed
// with the class in m_plyr_camera.h; only Work(), Draw() and SyncHpBar() are
// out of line.
//
// Reconstructed from the Feb 6 2004 prototype (SLES_523.84), hp_bar.o.
// All three ZERO2.MAP .text symbols.

#include "m_plyr_camera.h"
#include "n_finder_dat.h"                       /* n_finder_dat            */
#include "../../graphics/graph2d/g2d_draw.h"    /* DISP_SPRT / DispSprD    */
#include "../plyr/player.h"                     /* PlayerGetNowHPPercentage */

#include <math.h>                               /* fabsf                   */

/* n_finder_dat[] indices.  a6/a7 are the plate, a3 the frame, a4 and a5 the
 * two fill overlays. */
#define FD_HP_PLATE_A       0xa6
#define FD_HP_PLATE_B       0xa7
#define FD_HP_FRAME         0xa3
#define FD_HP_FILL_A        0xa4
#define FD_HP_FILL_B        0xa5

/* How fast the drawn value chases the real one -- one percent of full HP a
 * frame, so a full bar takes 100 frames to drain. */
#define HB_CHASE_STEP       0.0099999998f   /* lit4 3ee428 */

/* The bar's baseline and its two column positions, in finder-local pixels.
 * A full bar is HB_BAR_LEN tall; the sprites are one pixel wide and stretched
 * by sch, which is negated so they run up from the baseline. */
#define HB_BAR_LEN          18.0f
#define HB_FRAME_X          579.0f          /* lit4 3ee42c */
#define HB_BAR_Y            389.0f          /* lit4 3ee430 */
#define HB_FILL_X           581.0f          /* lit4 3ee434 */

/* Alpha ceiling.  Unlike the other finder widgets this one divides by a plain
 * 128 rather than by mAlpha.GetMax() -- GCC strength-reduced it to a shift,
 * which it does not do for the accessor (compare CSearchMark::Draw, where the
 * same <short,0,128> keeps a real divide). */
#define HB_ALPHA_MAX        128

/* How fast the bar fades once the hold expires. */
#define HB_FADE_OUT_SPD   (-5)

/* Depth-masked; the plate is source-over and the fill additive. */
#define HB_ZBUF_NO_WRITE    0x000000010a000118ULL
#define HB_ALPHA_BLEND      0x44
#define HB_ALPHA_ADD        0x48

/* The drawn health, 0..1.  File-static because SyncHpBar() has to reach it
 * without a CHpBar to hand. */
static float disp_hp_per;                                   /* sdata 3f1508 */

/* Snap the drawn value onto the real one.  ingame.c and item.c call this
 * whenever the player's HP changes for a reason the bar should not animate. */
void SyncHpBar(void)                                                    /* 14 */
{
    disp_hp_per = PlayerGetNowHPPercentage();                           /* 15 */
}

void CHpBar::Draw(int fndr_mx, int fndr_my, int iAlpha)                 /* 20 */
{
    DISP_SPRT ds;
    float     rate;
    float     hp_per;
    float     per;

    /* The master alpha every piece is drawn at.  Written out at each use
     * rather than kept in a local -- the stabs name only rate/hp_per/per. */
    hp_per = PlayerGetNowHPPercentage();                                /* 31 */

    if (hp_per != disp_hp_per)                                          /* 33 */
    {
        per = fabsf(hp_per - disp_hp_per) < HB_CHASE_STEP
                  ? fabsf(hp_per - disp_hp_per) : HB_CHASE_STEP;        /* 34 */
        per = disp_hp_per < hp_per ? disp_hp_per + per : disp_hp_per - per; /* 35 */

        disp_hp_per = per > 1.0f ? 1.0f : (per < 0.0f ? 0.0f : per);    /* 36 */
    }

    CopySprDToSpr(&ds, &n_finder_dat[FD_HP_PLATE_A]);                   /* 40 */
    ds.zbuf   = HB_ZBUF_NO_WRITE;                                       /* 41 */
    ds.alphar = HB_ALPHA_BLEND;                                         /* 42 */
    ds.alpha  = (u_char)(iAlpha * mAlpha.Get() / HB_ALPHA_MAX);         /* 43 */
    ds.x     += (float)fndr_mx;
    ds.y     += (float)fndr_my;                                         /* 44 */
    DispSprD(&ds);                                                      /* 45 */

    CopySprDToSpr(&ds, &n_finder_dat[FD_HP_PLATE_B]);                   /* 47 */
    ds.zbuf   = HB_ZBUF_NO_WRITE;                                       /* 48 */
    ds.alphar = HB_ALPHA_BLEND;                                         /* 49 */
    ds.alpha  = (u_char)(iAlpha * mAlpha.Get() / HB_ALPHA_MAX);         /* 50 */
    ds.x     += (float)fndr_mx;
    ds.y     += (float)fndr_my;                                         /* 51 */
    DispSprD(&ds);                                                      /* 52 */

    rate = HB_BAR_LEN;                                                  /* 56 */

    /* The empty frame: full length, drawn upward from the baseline. */
    CopySprDToSpr(&ds, &n_finder_dat[FD_HP_FRAME]);                     /* 59 */
    ds.zbuf   = HB_ZBUF_NO_WRITE;                                       /* 60 */
    ds.alphar = HB_ALPHA_BLEND;                                         /* 61 */
    ds.alpha  = (u_char)(iAlpha * mAlpha.Get() / HB_ALPHA_MAX);         /* 62 */
    ds.csx    = (float)fndr_mx + HB_FRAME_X;
    ds.csy    = (float)fndr_my + HB_BAR_Y;                              /* 63 */
    ds.x      = ds.csx;
    ds.y      = ds.csy;                                                 /* 64 */
    ds.scw    = 1.0f;
    ds.sch    = -rate;                                                  /* 65 */
    DispSprD(&ds);                                                      /* 66 */

    rate = disp_hp_per * rate;                                          /* 69 */

    /* The fill, in two columns.  Each block has to re-establish the baseline:
     * CopySprDToSpr() resets csy to 0, and the ROM only looks like it carries
     * the frame's value over because GCC CSE'd (float)fndr_my + HB_BAR_Y into
     * $f20 and reuses it here (0x1c84f8 / 0x1c8510).  Dropping the store leaves
     * the fill at y 0 with a negative sch -- growing up off the top of the
     * screen, so the trough draws and the bar inside it never appears. */
    CopySprDToSpr(&ds, &n_finder_dat[FD_HP_FILL_A]);                    /* 75 */
    ds.zbuf   = HB_ZBUF_NO_WRITE;                                       /* 76 */
    ds.alphar = HB_ALPHA_ADD;                                           /* 77 */
    ds.alpha  = (u_char)(iAlpha * mAlpha.Get() / HB_ALPHA_MAX);         /* 78 */
    ds.csx    = (float)fndr_mx + HB_FILL_X;
    ds.csy    = (float)fndr_my + HB_BAR_Y;                              /* 79 */
    ds.x      = ds.csx;
    ds.y      = ds.csy;                                                 /* 80 */
    ds.scw    = 1.0f;
    ds.sch    = -rate;                                                  /* 81 */
    DispSprD(&ds);                                                      /* 82 */

    CopySprDToSpr(&ds, &n_finder_dat[FD_HP_FILL_B]);                    /* 87 */
    ds.zbuf   = HB_ZBUF_NO_WRITE;                                       /* 88 */
    ds.alphar = HB_ALPHA_ADD;                                           /* 89 */
    ds.alpha  = (u_char)(iAlpha * mAlpha.Get() / HB_ALPHA_MAX);         /* 90 */
    ds.csx    = (float)fndr_mx + HB_FRAME_X;
    ds.csy    = (float)fndr_my + HB_BAR_Y;                              /* 91 */
    ds.x      = ds.csx;
    ds.y      = ds.csy;                                                 /* 92 */
    ds.scw    = 1.0f;
    ds.sch    = -rate;                                                  /* 93 */
    DispSprD(&ds);                                                      /* 94 */
}

/* The fade the finder arms with FadeOut(): the hold runs first, and only when
 * it expires does the alpha start moving. */
void CHpBar::Work(void)                                                 /* 98 */
{
    if (mFadeWaitCnt.Work())                                            /* 99 */
    {
        mAlpha.SetAddVal(HB_FADE_OUT_SPD);
    }

    mAlpha.Work();                                                      /* 102 */
}
