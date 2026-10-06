// FILE: /home/zero_rom/zero2np/src/outgame/title_disp.c
//
// Title-screen drawing: the ZERO and TECMO logos, the menu arrow pair, the
// caption strip, and the scrolling multi-layer background.
//
// STATUS -- PARTIALLY RECONSTRUCTED.
//   * The five ZERO2.MAP exports (DispTitleCursorL / DispTitleCursorR /
//     DispTitleZeroLogo / DispTitleTecmoLogo / TitleCaptionDisp) are
//     reconstructed and their line numbers measured.
//   * DispTitleBack() and the black-quad helper below it are the earlier
//     pass's work, relocated here out of title.c and NOT verified against the
//     ROM.  title_disp.o's real background half is DispTitleBack plus six
//     statics -- DispTitleBgPattern1/2, DispTitleBgCloud1/2 and
//     DispTitleBgShadeingOffFlea1/2 -- driven by nine .rodata animation
//     tables at 0x3e6920..0x3e6aa0 (four ALPHA_ANIM_TBL, five POS_ANIM_TBL).
//     None of that is present yet; what is here approximates the motion.
//
// Reconstructed from the Feb 6 2004 prototype (SLES_523.84).
// Trailing /* NNN */ are ROM source line numbers, measured from
// disassemble_function.

#include "title_disp.h"

#include "title.h"                          // GetTitleLogoTexAddr
#include "tim_dat/title_dat.h"              // title_top[]
#include "../graphics/graph2d/draw_cmn.h"   // DrawCmnCapGroup_W
#include "../graphics/graph2d/g2d_draw.h"   // DISP_SPRT / DISP_SQAR / Copy* / Disp*
#include "../graphics/graph2d/tim2.h"       // PK2SendVram

#include <stdint.h>                         // uintptr_t

static void DispTitleBlackBg(int off_x, int off_y, u_char alpha);
static void DispTitleLayer(int spr_id, float x, float y, u_char alpha,
                           int additive, float scale_w, float scale_h);

/* The two menu arrows.  Both are additive (alphar 0x48) and tinted by the
 * shared cursor pulse, and both are placed absolutely -- the caller has
 * already looked the x up in title_left_x_tbl / title_right_x_tbl. */
void DispTitleCursorL(float x, float y, u_char alpha, u_char rgb)
{
    DISP_SPRT ds;

    PK2SendVram((uintptr_t)GetTitleLogoTexAddr(), -1, -1, 0);           /* 66 */

    CopySprDToSpr(&ds, title_top + 0x15);                               /* 79 */
    ds.x = x;                                                           /* 80 */
    ds.y = y;                                                           /* 80 */
    ds.alpha = (u_char)(((int)ds.alpha * (int)alpha) >> 7);             /* 81 */
    ds.r = ds.g = ds.b = rgb;                                           /* 82 */
    ds.alphar = 0x48;                                                   /* 83 */
    DispSprD(&ds);                                                      /* 84 */
}

void DispTitleCursorR(float x, float y, u_char alpha, u_char rgb)
{
    DISP_SPRT ds;

    PK2SendVram((uintptr_t)GetTitleLogoTexAddr(), -1, -1, 0);           /* 100 */

    CopySprDToSpr(&ds, title_top + 0x16);                               /* 112 */
    ds.x = x;                                                           /* 113 */
    ds.y = y;                                                           /* 113 */
    ds.alpha = (u_char)(((int)ds.alpha * (int)alpha) >> 7);             /* 114 */
    ds.r = ds.g = ds.b = rgb;                                           /* 115 */
    ds.alphar = 0x48;                                                   /* 116 */
    DispSprD(&ds);                                                      /* 117 */
}

/* The ZERO logo: four upright plates (6..9) plus two more (10, 11) rotated a
 * quarter turn about a centre placed one sprite-width down from their own
 * position.  ds.w is u_int, hence the unsigned int-to-float conversion in the
 * ROM's code. */
void DispTitleZeroLogo(int off_x, int off_y, u_char alpha)
{
    DISP_SPRT ds;
    int i;

    PK2SendVram((uintptr_t)GetTitleLogoTexAddr(), -1, -1, 0);           /* 158 */

    for (i = 0; i < 4; i++)                                             /* 161 */
    {
        CopySprDToSpr(&ds, title_top + 6 + i);                          /* 162 */
        ds.x += (float)off_x;                                           /* 163 */
        ds.y += (float)off_y;                                           /* 163 */
        ds.alpha = (u_char)(((int)ds.alpha * (int)alpha) >> 7);         /* 164 */
        DispSprD(&ds);                                                  /* 165 */
    }                                                                   /* 166 */

    /* GCC scheduled the two chained stores apart, so the 169/170 split
     * between the position and its rotation centre is not recoverable --
     * the values are. */
    CopySprDToSpr(&ds, title_top + 10);                                 /* 168 */
    ds.crx = ds.x = ds.x + (float)off_x;                                /* 169 */
    ds.rot = 270.0f;                                                    /* 170 */
    ds.cry = ds.y = ds.y + (float)ds.w + (float)off_y;                  /* 170 */
    ds.alpha = (u_char)(((int)ds.alpha * (int)alpha) >> 7);             /* 171 */
    DispSprD(&ds);                                                      /* 172 */

    CopySprDToSpr(&ds, title_top + 11);                                 /* 174 */
    ds.crx = ds.x = ds.x + (float)off_x;                                /* 175 */
    ds.rot = 270.0f;                                                    /* 176 */
    ds.cry = ds.y = ds.y + (float)ds.w + (float)off_y;                  /* 176 */
    ds.alpha = (u_char)(((int)ds.alpha * (int)alpha) >> 7);             /* 177 */
    DispSprD(&ds);                                                      /* 178 */
}

/* Two plates, written out rather than looped. */
void DispTitleTecmoLogo(int off_x, int off_y, u_char alpha)
{
    DISP_SPRT ds;

    PK2SendVram((uintptr_t)GetTitleLogoTexAddr(), -1, -1, 0);           /* 193 */

    CopySprDToSpr(&ds, title_top + 0xc);                                /* 195 */
    ds.x += (float)off_x;                                               /* 196 */
    ds.y += (float)off_y;                                               /* 196 */
    ds.alpha = (u_char)(((int)ds.alpha * (int)alpha) >> 7);             /* 197 */
    DispSprD(&ds);                                                      /* 198 */

    CopySprDToSpr(&ds, title_top + 0xd);                                /* 207 */
    ds.x += (float)off_x;                                               /* 208 */
    ds.y += (float)off_y;                                               /* 208 */
    ds.alpha = (u_char)(((int)ds.alpha * (int)alpha) >> 7);             /* 209 */
    DispSprD(&ds);                                                      /* 210 */
}

/* ──────────────────────────────────────────────────────────────────────
 * NOT RECONSTRUCTED -- the earlier pass's approximation, relocated here out
 * of title.c so title.o's own file stops carrying it.  See the STATUS note at
 * the top for what title_disp.o really contains.
 * ────────────────────────────────────────────────────────────────────── */

void DispTitleBack(int *timer, void *tex_addr)
{
    float phase;

    if (tex_addr == (void *)0)
    {
        return;
    }

    if (*timer > 0x1c1f)
    {
        *timer = 0;
    }

    phase = (float)(*timer % 640);

    PK2SendVram((uintptr_t)tex_addr, -1, -1, 0);
    DispTitleBlackBg(0, 0, 0x80);

    DispTitleLayer(4, phase, 0.0f, 0x50, 1, 640.0f / 512.0f, 1.0f);
    DispTitleLayer(4, phase - 640.0f, 0.0f, 0x50, 1, 640.0f / 512.0f, 1.0f);
    DispTitleLayer(5, -phase * 0.5f, 0.0f, 0x38, 1, 640.0f / 256.0f, 448.0f / 256.0f);
    DispTitleLayer(5, 640.0f - phase * 0.5f, 0.0f, 0x38, 1, 640.0f / 256.0f, 448.0f / 256.0f);
    DispTitleLayer(2, -320.0f + phase * 0.25f, 2.0f, 0x30, 1, 640.0f / 512.0f, 448.0f / 512.0f);
    DispTitleLayer(3, 320.0f - phase * 0.25f, 0.0f, 0x30, 1, 640.0f / 512.0f, 448.0f / 512.0f);
    DispTitleLayer(0, phase * 0.33f, 0.0f, 0x28, 0, 640.0f / 256.0f, 448.0f / 256.0f);
    DispTitleLayer(1, -phase * 0.33f, 0.0f, 0x28, 0, 640.0f / 256.0f, 448.0f / 256.0f);

    *timer = *timer + 1;
}

static void DispTitleBlackBg(int off_x, int off_y, u_char alpha)
{
    DISP_SQAR dsq;
    SQAR_DAT title_bg = { 640, 448, off_x, off_y, 160, 0, 0, 0, alpha };

    CopySqrDToSqr(&dsq, &title_bg);
    DispSqrD(&dsq);
}

/* No such function in the ROM -- a helper the earlier pass invented to stand
 * in for title_disp.o's six per-layer statics. */
static void DispTitleLayer(int spr_id, float x, float y, u_char alpha,
                           int additive, float scale_w, float scale_h)
{
    DISP_SPRT ds;

    CopySprDToSpr(&ds, title_top + spr_id);
    ds.x = x;
    ds.y += y;
    ds.csx = x;
    ds.csy = ds.y;
    ds.scw = scale_w;
    ds.sch = scale_h;
    ds.alpha = alpha;
    if (additive != 0)
    {
        ds.alphar = 0x48;
    }
    DispSprD(&ds);
}

/* The caption strip along the bottom.  All three parameters but `alpha` are
 * dead -- the group is placed by draw_cmn.c. */
void TitleCaptionDisp(int off_x, int off_y, u_char alpha)
{
    (void)off_x;
    (void)off_y;

    DrawCmnCapGroup_W(0xe, 0xe, alpha, 0);                              /* 532 */
}
