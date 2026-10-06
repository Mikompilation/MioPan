// FILE: /home/zero_rom/zero2np/src/ingame/savepoint/savepoint_disp.c
//
// Everything the save-point screen draws.
//
// The background is five layers stacked over a half-black screen fill, and
// three of the five breathe on their own timers:
//
//   SavePoint_BlackBgDisp(0x80)  the ground -- half-opaque black over the
//                                frozen 3D scene behind the menu
//   BgPattern1 / BgPattern2      two 640-wide pattern sheets, each drawn
//                                twice side by side and scrolled left across
//                                900 / 600 frames so the seam never shows
//   BgFlea                       four corner motes, additive, tinted amber
//   Flea                         four drifting motes at 3x, additive
//   Shadow                       four corner vignettes, plain blend
//
// The two pattern layers and the corner motes take their alpha from
// bg_anim_timer through three hand-authored ALPHA_ANIM_TBL curves, so the
// whole backdrop pulses on one 900-frame cycle while the two sheets slide at
// different rates.  Flea and Shadow are drawn at fixed alpha (0x26 and 0x80).
//
// Every group of four sprites is one quadrant image drawn with flip 0/1/2/3;
// see tim_dat/savepoint_dat.c.
//
// Reconstructed from the Feb 6 2004 prototype (SLES_523.84), savepoint_disp.o.
//
// Trailing /* NNN */ comments are the original source line numbers, measured
// from the $LM stabs.  Where a line carries four assignments (219/220 and
// their equivalents in the sibling draw loops) that is what the stabs say:
// the x/y update and the scale/centre update are one source line each.

#include "savepoint_disp.h"

#include "tim_dat/savepoint_dat.h"                  /* savepoint_tex          */

#include "../menu/anim_2d.h"                        /* Anim2D_CalcNowAlpha    */
#include "../../common/utility2.h"                  /* PRINT_ASSERT           */
#include "../../graphics/graph2d/draw_cmn.h"        /* DrawCmnWindow          */
#include "../../graphics/graph2d/g2d_draw.h"        /* DISP_SPRT / DISP_SQAR  */
#include "../../graphics/graph2d/tim2.h"            /* PK2SendVram            */

/* --------------------------------------------------------------------------
 *  Layer indices into savepoint_tex[]
 * ------------------------------------------------------------------------ */

#define SAVEPOINT_TEX_MOYOU1        0   /* pattern sheet 1                    */
#define SAVEPOINT_TEX_MOYOU2        1   /* pattern sheet 2                    */
#define SAVEPOINT_TEX_BG_FLEA       2   /* [2..5]   corner motes              */
#define SAVEPOINT_TEX_FLEA          6   /* [6..9]   drifting motes            */
#define SAVEPOINT_TEX_SHADOW        10  /* [10..13] corner vignettes          */

static void SavePoint_BgPattern1Disp(float off_x, float off_y, u_char alpha);
static void SavePoint_BgPattern2Disp(float off_x, float off_y, u_char alpha);
static void SavePoint_BgFleaDisp(int off_x, int off_y, u_char alpha);
static void SavePoint_FleaDisp(int off_x, int off_y, u_char alpha);
static void SavePoint_ShadowDisp(int off_x, int off_y, u_char alpha);

/* ==========================================================================
 *  Windows
 * ======================================================================== */

/* The menu frame: a 296x160 common window at (171, 93) with a rule across it
 * at y = 137, which is what separates the caption from the three options. */
void SavePoint_MenuWinDisp(int off_x, int off_y, u_char alpha)           /* 50 */
{
    (void)off_x;                /* both offsets are dead in the ROM */
    (void)off_y;

    DrawCmnWindow(0, 171.0f, 93.0f, 296.0f, 160.0f, alpha, 0x59);       /* 55 */

    DrawCmnLine(171.0f, 137.0f, 296.0f, 1, alpha, 0xa0);                /* 59 */
}

/* The "really?" window: a wide two-line frame with the yes/no pair under it. */
void SavePoint_MenuConfWinDisp(int cursor, int off_x, int off_y, u_char alpha) /* 71 */
{
    (void)off_x;                /* dead in the ROM; only off_y is read */

    if ((u_int)cursor > 1) {                                            /* 74 */
        PRINT_ASSERT("Error! %s", __FUNCTION__);                        /* 75 */
    }

    DrawCmnTwoLineWindow(0, 40.0f, 295.0f, 560.0f, 118.0f, alpha, 0x59); /* 81 */

    DrawCmnYesNoSel(cursor, (float)(off_y + 363), alpha, 0);            /* 84 */
}

/* ==========================================================================
 *  Background
 * ======================================================================== */

/* One frame of the whole backdrop, and the tick of its three timers.
 *
 * The three alpha curves all read bg_anim_timer, so the layers breathe
 * together; only the two horizontal scrolls have counters of their own, and
 * moyou2's is a third shorter, which is what keeps the two sheets from
 * beating in step. */
void SavePoint_BgDisp(int *bg_anim_timer, int *moyou1_anim_timer,
                      int *moyou2_anim_timer, void *pk2_addr)            /* 96 */
{
    u_char moyou1_alpha;
    u_char moyou2_alpha;
    u_char bg_flea_alpha;
    float  moyou1_off_x;
    float  moyou2_off_x;

    static const ALPHA_ANIM_TBL moyou1_alpha_tbl[7] =       /* rdata 3c4c78 */
    {
        { 19, 51,   0, 150 },
        { 51, 12, 150, 250 },
        { 12, 57, 250, 440 },
        { 57, 12, 440, 720 },
        { 12, 25, 720, 850 },
        { 25, 19, 850, 900 },
        { -1, -1,  -1,  -1 },
    };

    static const ALPHA_ANIM_TBL moyou2_alpha_tbl[8] =       /* rdata 3c4cb0 */
    {
        { 38, 64,   0, 120 },
        { 64, 12, 120, 190 },
        { 12, 38, 190, 360 },
        { 38, 83, 360, 480 },
        { 83,  6, 480, 700 },
        {  6, 51, 700, 875 },
        { 51, 38, 875, 900 },
        { -1, -1,  -1,  -1 },
    };

    static const ALPHA_ANIM_TBL bg_flea_alpha_tbl[6] =      /* rdata 3c4cf0 */
    {
        { 19,  0,   0, 230 },
        {  0, 12, 230, 320 },
        { 12, 76, 320, 420 },
        { 76,  6, 420, 615 },
        {  6, 19, 615, 900 },
        { -1, -1,  -1,  -1 },
    };

    static const POS_ANIM_TBL moyou1_x_tbl[2] =             /* rdata 3c4d20 */
    {
        {  0.0f, 640.0f,  0, 900,  0 },
        { -1.0f,  -1.0f, -1,  -1, -1 },
    };

    static const POS_ANIM_TBL moyou2_x_tbl[2] =             /* rdata 3c4d40 */
    {
        {  0.0f, 640.0f,  0, 600,  0 },
        { -1.0f,  -1.0f, -1,  -1, -1 },
    };

    moyou1_alpha  = Anim2D_CalcNowAlpha((ALPHA_ANIM_TBL *)moyou1_alpha_tbl,
                                        *bg_anim_timer);                /* 141 */
    moyou2_alpha  = Anim2D_CalcNowAlpha((ALPHA_ANIM_TBL *)moyou2_alpha_tbl,
                                        *bg_anim_timer);                /* 142 */
    bg_flea_alpha = Anim2D_CalcNowAlpha((ALPHA_ANIM_TBL *)bg_flea_alpha_tbl,
                                        *bg_anim_timer);                /* 143 */

    moyou1_off_x = Anim2D_CalcNowPos((POS_ANIM_TBL *)moyou1_x_tbl,
                                     *moyou1_anim_timer);               /* 145 */
    moyou2_off_x = Anim2D_CalcNowPos((POS_ANIM_TBL *)moyou2_x_tbl,
                                     *moyou2_anim_timer);               /* 146 */

    PK2SendVram((uintptr_t)pk2_addr, -1, -1, 0);                        /* 149 */

    SavePoint_BlackBgDisp(0x80);                                        /* 153 */

    SavePoint_BgPattern1Disp(moyou1_off_x, 0.0f, moyou1_alpha);         /* 156 */
    SavePoint_BgPattern2Disp(moyou2_off_x, 0.0f, moyou2_alpha);         /* 159 */

    SavePoint_BgFleaDisp(0, 0, bg_flea_alpha);                          /* 162 */
    SavePoint_FleaDisp(0, 0, 0x26);                                     /* 165 */
    SavePoint_ShadowDisp(0, 0, 0x80);                                   /* 168 */

    (*bg_anim_timer)++;                                                 /* 171 */
    if (*bg_anim_timer >= 900) {                                        /* 172 */
        *bg_anim_timer = 0;                                             /* 173 */
    }

    (*moyou1_anim_timer)++;                                             /* 176 */
    if (*moyou1_anim_timer >= 900) {                                    /* 177 */
        *moyou1_anim_timer = 0;                                         /* 178 */
    }

    (*moyou2_anim_timer)++;                                             /* 181 */
    if (*moyou2_anim_timer >= 600) {                                    /* 182 */
        *moyou2_anim_timer = 0;                                         /* 183 */
    }
}

/* Full-screen black.  save_point_bg's own alpha is 0x80 and is immediately
 * overwritten by the argument -- the initialiser is a .rodata blob copied
 * into the stack slot, so the field is there whether it is used or not. */
void SavePoint_BlackBgDisp(u_char alpha)                                /* 191 */
{
    DISP_SQAR dsq;
    SQAR_DAT  save_point_bg = { 640, 448, 0, 0, 0, 0, 0, 0, 0x80 };     /* 193 */

    CopySqrDToSqr(&dsq, &save_point_bg);                                /* 198 */

    dsq.alpha = alpha;                                                  /* 199 */

    DispSqrD(&dsq);                                                     /* 200 */
}

/* --------------------------------------------------------------------------
 *  Background layers
 * ------------------------------------------------------------------------ */

/* Pattern sheet 1, drawn twice: copy i sits 640 to the left of copy 0, so as
 * off_x walks 0 -> 640 the pair scrolls with no seam.  Both copies are
 * stretched to a full 640x448 regardless of the source size, and blended
 * additively (alphar 0x48). */
static void SavePoint_BgPattern1Disp(float off_x, float off_y, u_char alpha) /* 211 */
{
    DISP_SPRT pattern_ds;
    int       i;

    for (i = 0; i < 2; i++) {                                           /* 217 */
        CopySprDToSpr(&pattern_ds, &savepoint_tex[SAVEPOINT_TEX_MOYOU1]); /* 218 */

        pattern_ds.x = (pattern_ds.x + off_x) - (float)i * 640.0f;      /* 219 */
        pattern_ds.y = pattern_ds.y + off_y;                            /* 219 */

        pattern_ds.scw = 640.0f / (float)pattern_ds.w;                  /* 220 */
        pattern_ds.sch = 448.0f / (float)pattern_ds.h;                  /* 220 */
        pattern_ds.csx = pattern_ds.x;                                  /* 220 */
        pattern_ds.csy = pattern_ds.y;                                  /* 220 */

        pattern_ds.alpha  = (u_char)(((int)pattern_ds.alpha * (int)alpha) >> 7); /* 221 */
        pattern_ds.alphar = 0x48;                                       /* 222 */

        DispSprD(&pattern_ds);                                          /* 223 */
    }
}

/* Pattern sheet 2.  Byte-for-byte the same loop on savepoint_tex[1]; the ROM
 * writes it out twice rather than parameterising the layer. */
static void SavePoint_BgPattern2Disp(float off_x, float off_y, u_char alpha) /* 235 */
{
    DISP_SPRT pattern_ds;
    int       i;

    for (i = 0; i < 2; i++) {                                           /* 241 */
        CopySprDToSpr(&pattern_ds, &savepoint_tex[SAVEPOINT_TEX_MOYOU2]); /* 242 */

        pattern_ds.x = (pattern_ds.x + off_x) - (float)i * 640.0f;      /* 243 */
        pattern_ds.y = pattern_ds.y + off_y;                            /* 243 */

        pattern_ds.scw = 640.0f / (float)pattern_ds.w;                  /* 244 */
        pattern_ds.sch = 448.0f / (float)pattern_ds.h;                  /* 244 */
        pattern_ds.csx = pattern_ds.x;                                  /* 244 */
        pattern_ds.csy = pattern_ds.y;                                  /* 244 */

        pattern_ds.alpha  = (u_char)(((int)pattern_ds.alpha * (int)alpha) >> 7); /* 245 */
        pattern_ds.alphar = 0x48;                                       /* 246 */

        DispSprD(&pattern_ds);                                          /* 247 */
    }
}

/* The four corner motes, 2.4x and tinted amber (255, 200, 96).  The scale is
 * a .lit4 constant one ulp below 2.4 -- EE GCC truncating the literal, so the
 * source said 2.4f. */
static void SavePoint_BgFleaDisp(int off_x, int off_y, u_char alpha)    /* 259 */
{
    DISP_SPRT flea_ds;
    int       i;

    for (i = 0; i < 4; i++) {                                           /* 265 */
        CopySprDToSpr(&flea_ds, &savepoint_tex[SAVEPOINT_TEX_BG_FLEA + i]); /* 266 */

        flea_ds.x = flea_ds.x + (float)off_x;                           /* 267 */
        flea_ds.y = flea_ds.y + (float)off_y;                           /* 267 */

        flea_ds.scw = 2.3999998f;               /* lit4 3ee824, i.e. 2.4f */ /* 268 */
        flea_ds.sch = 2.3999998f;                                       /* 268 */
        flea_ds.csx = flea_ds.x;                                        /* 268 */
        flea_ds.csy = flea_ds.y;                                        /* 268 */

        flea_ds.alpha  = (u_char)(((int)flea_ds.alpha * (int)alpha) >> 7); /* 269 */
        flea_ds.alphar = 0x48;                                          /* 270 */

        flea_ds.r = 0xff;   flea_ds.g = 200;    flea_ds.b = 0x60;       /* 271 */

        DispSprD(&flea_ds);                                             /* 272 */
    }
}

/* The four drifting motes, 3x, additive, untinted.  Their table positions sit
 * well outside the screen because the 3x scale grows about the top-left
 * corner. */
static void SavePoint_FleaDisp(int off_x, int off_y, u_char alpha)      /* 284 */
{
    DISP_SPRT flea_ds;
    int       i;

    for (i = 0; i < 4; i++) {                                           /* 290 */
        CopySprDToSpr(&flea_ds, &savepoint_tex[SAVEPOINT_TEX_FLEA + i]); /* 291 */

        flea_ds.x = flea_ds.x + (float)off_x;                           /* 292 */
        flea_ds.y = flea_ds.y + (float)off_y;                           /* 292 */

        flea_ds.scw = 3.0f;                                             /* 293 */
        flea_ds.sch = 3.0f;                                             /* 293 */
        flea_ds.csx = flea_ds.x;                                        /* 293 */
        flea_ds.csy = flea_ds.y;                                        /* 293 */

        flea_ds.alpha  = (u_char)(((int)flea_ds.alpha * (int)alpha) >> 7); /* 294 */
        flea_ds.alphar = 0x48;                                          /* 295 */

        DispSprD(&flea_ds);                                             /* 296 */
    }
}

/* The four corner vignettes: one quadrant scaled to a full 320x224, so the
 * four together darken the screen edges.  This is the only background layer
 * that leaves alphar alone -- it is a plain blend, not additive, because it
 * has to subtract light rather than add it. */
static void SavePoint_ShadowDisp(int off_x, int off_y, u_char alpha)    /* 308 */
{
    DISP_SPRT shadow_ds;
    int       i;

    for (i = 0; i < 4; i++) {                                           /* 314 */
        CopySprDToSpr(&shadow_ds, &savepoint_tex[SAVEPOINT_TEX_SHADOW + i]); /* 315 */

        shadow_ds.x = shadow_ds.x + (float)off_x;                       /* 316 */
        shadow_ds.y = shadow_ds.y + (float)off_y;                       /* 316 */

        shadow_ds.scw = 320.0f / (float)shadow_ds.w;                    /* 317 */
        shadow_ds.sch = 224.0f / (float)shadow_ds.h;                    /* 317 */
        shadow_ds.csx = shadow_ds.x;                                    /* 317 */
        shadow_ds.csy = shadow_ds.y;                                    /* 317 */

        shadow_ds.alpha = (u_char)(((int)shadow_ds.alpha * (int)alpha) >> 7); /* 318 */

        DispSprD(&shadow_ds);                                           /* 319 */
    }
}

/* ==========================================================================
 *  Caption
 * ======================================================================== */

/* Lives in savepoint_disp.o but is only ever called from savepoint_top.c,
 * which is why it carries the Top prefix.  Both offsets are dead. */
void SavePointTopCaptionDisp(int off_x, int off_y, u_char alpha)        /* 331 */
{
    (void)off_x;
    (void)off_y;

    DrawCmnCapGroup_W(0, 0, alpha, 0);                                  /* 334 */
}
