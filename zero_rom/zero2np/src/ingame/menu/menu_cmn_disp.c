// FILE: /home/zero_rom/zero2np/src/ingame/menu/menu_cmn_disp.c
//
// The widget layer every in-game menu page draws from (menu_cmn_disp.o).
// Thirteen functions, no state at all: the object has no work block, no
// file-scope variables and nothing in .data.  Its whole .rodata is the four
// function-local lookup tables below plus the compiler's own assert literals,
// and its .sdata is the fixed_array assert's `str` pointer, the three type
// names and BIT_FLAGS::IsUp()'s __FUNCTION__ string.  (The first 0x28 bytes
// of that .sdata are zero and no instruction in the object references them --
// the same unexplained gap menu_cmn.o has.)
//
// Two jobs live here.  The larger is MenuPlyrDataDisp(), the plate down the
// left of the menu hub: the status frame, the health bar, the fitted camera
// sub-functions with their upgrade levels, and the loaded film with its
// remaining count.  The smaller is the set of shared chrome the pages place
// themselves -- row frames, rules, the two modal windows and the digit run.
//
// Facts worth knowing before touching it:
//
//  * Bit 1 of CCameraPowerUp::mCamPartsSetFlg forks the plate twice.  With
//    the part fitted the frame comes from menu_cmn_dat[2]/[3] instead of
//    [0]/[1] (a taller plate out of a second pak) and all three sub-function
//    slots are drawn in a row; without it there is one slot and the shorter
//    frame.  Both forks then share the [4]/[5] tail.
//
//  * Every function re-uploads the pak it samples through PK2SendVram(), and
//    they do not all sample the same one.  The plate and its parts come from
//    MENU_STATUS_TEX_ADRS, the lens icons and the selected row frame and both
//    rules from MENU_BG_TEX_ADRS, and the *unselected* row frame from
//    MENU_PLAYDATA_TEX_ADRS.  menu_cmn_dat[]'s own TEX0 words agree: 38/39
//    carry a different one from 36/37.
//
//  * The health bar is four separately-shaded segments drawn right to left.
//    hp_tex_tbl is { 9, 8, 7, 6 } and the sprites' x positions run 258, 213,
//    168, 123, so the run starts at the right-hand end; whichever segment the
//    scaled width runs out inside is clipped from its left edge (u and x both
//    advanced by w - hp_w) and the loop stops there.
//
//  * A film readout has three independent special cases.  Type-00 (film type
//    4) draws the plate at menu_cmn_dat[26] where the others print their
//    number, Type-07 (type 0) shows dashes instead of a count because it is
//    the unlimited film, and no camera at all (item 10) drops the whole
//    readout for four dashes.
//
//  * MenuCmnLineTateDisp() and MenuCmnLineYokoDisp() are the same function on
//    different axes, and so are MenuCmnSelFrameDisp() and
//    MenuCmnNonSelFrameDisp().  Both pairs are written out in full in the ROM
//    rather than sharing a helper.
//
// Reconstructed from the Feb 6 2004 prototype (SLES_523.84), menu_cmn_disp.o.
// All 8 ZERO2.MAP exports plus the 5 statics; .text is accounted for
// byte-for-byte (0x1f2a30..0x1f3d54 = 0x1324 = 4900 bytes: seventeen bodies
// -- thirteen real plus the four fixed_array boilerplate ones -- totalling
// 4852, and twelve 4-byte alignment fills), so there is no unlisted body.
//
// Trailing /* NNN */ comments are the original source line numbers, measured
// from the $LM stabs.  Four statements carry no line of their own and are
// left unannotated rather than guessed:
//
//  * the two `if (...mCamPartsSetFlg.IsUp(1))` tests, whose line was eaten by
//    the inlined accessor (they report variable.h 852/858 instead), and
//
//  * the two MenuNumberDisp() calls in MenuCmnPlyrEquipReinforcedLensDisp(),
//    which report variable.h 124.  That line is not CVariable::Get() (167);
//    it appears in exactly four objects in the build, always on the caller's
//    own arithmetic next to a CVariable read through a fixed_array subscript,
//    and the instructions it lands on are plainly the caller's.  Treated as a
//    stray note.

#include "menu_cmn_disp.h"

#include "menu.h"                               /* MENU_*_TEX_ADRS        */
#include "menu_cmn.h"                           /* menu_yes_no_ctrl       */
#include "tim_dat/menu_cmn_dat.h"               /* menu_cmn_dat           */

#include "../item/prg/item.h"                   /* GetPlyrItemHaveNum     */
#include "../photo/m_plyr_camera.h"             /* m_plyr_camera          */
#include "../plyr/player.h"                     /* GetPlyrEquipmentFilmType */
#include "../../common/variable.h"              /* plyr_wrk               */
#include "../../graphics/graph2d/draw_cmn.h"    /* DrawCmnNumberTex       */
#include "../../graphics/graph2d/g2d_draw.h"    /* DISP_SPRT / DispSprD   */
#include "../../graphics/graph2d/tim2.h"        /* PK2SendVram            */

static void MenuPlyrHPDisp(int x, int y, u_char alpha);
static void MenuCmnPlyrEquipReinforcedLensDisp(int x, int y, u_char alpha);
static void MenuCmnReinforcedLensDisp(int x, int y, u_char alpha,
                                      int lens_label);
static void MenuFilmDisp(int x, int y, u_char alpha);
static void MenuCmnHyphenDisp(int x, int y, u_char alpha, u_int pri);

/* --------------------------------------------------------------------------
 *  The player-data plate
 * ------------------------------------------------------------------------ */

/* The whole left-hand plate.  x/y is its top-left; every part below is a
 * fixed offset from it, so the caller only has to place this once.
 *
 * `y + 41` is written out at each of its four uses rather than held in a
 * local -- functions.txt lists no int local here, so the one register GCC
 * keeps it in is a CSE of the source's own repeats. */
void MenuPlyrDataDisp(int x, int y, u_char alpha)                        /* 68 */
{
    DISP_SPRT plyr_ds;

    PK2SendVram(MENU_STATUS_TEX_ADRS, -1, -1, 0);                        /* 72 */

    MenuPlyrHPDisp(x, y, alpha);                                         /* 76 */

    /* Bit 1 of the fitted-parts set is the sub-function tray.  With it the
     * plate is the taller pair out of the second pak, because three lens
     * slots have to fit under it instead of one. */
    if (m_plyr_camera.camera_power_up.mCamPartsSetFlg.IsUp(1) != 0) {
        CopySprDToSpr(&plyr_ds, &menu_cmn_dat[2]);                       /* 81 */
        plyr_ds.x += (float)x;   plyr_ds.y += (float)y;                  /* 82 */
        plyr_ds.alpha = alpha;                                           /* 83 */
        DispSprD(&plyr_ds);                                              /* 84 */

        CopySprDToSpr(&plyr_ds, &menu_cmn_dat[3]);                       /* 85 */
        plyr_ds.x += (float)x;   plyr_ds.y += (float)y;                  /* 86 */
        plyr_ds.alpha = alpha;                                           /* 87 */
        DispSprD(&plyr_ds);                                              /* 88 */
    }
    else {
        CopySprDToSpr(&plyr_ds, &menu_cmn_dat[0]);                       /* 92 */
        plyr_ds.x += (float)x;   plyr_ds.y += (float)y;                  /* 93 */
        plyr_ds.alpha = alpha;                                           /* 94 */
        DispSprD(&plyr_ds);                                              /* 95 */

        CopySprDToSpr(&plyr_ds, &menu_cmn_dat[1]);                       /* 96 */
        plyr_ds.x += (float)x;   plyr_ds.y += (float)y;                  /* 97 */
        plyr_ds.alpha = alpha;                                           /* 98 */
        DispSprD(&plyr_ds);                                              /* 99 */
    }

    CopySprDToSpr(&plyr_ds, &menu_cmn_dat[4]);                          /* 103 */
    plyr_ds.x += (float)x;   plyr_ds.y += (float)y;                     /* 104 */
    plyr_ds.alpha = alpha;                                              /* 105 */
    DispSprD(&plyr_ds);                                                 /* 106 */

    CopySprDToSpr(&plyr_ds, &menu_cmn_dat[5]);                          /* 107 */
    plyr_ds.x += (float)x;   plyr_ds.y += (float)y;                     /* 108 */
    plyr_ds.alpha = alpha;                                              /* 109 */
    DispSprD(&plyr_ds);                                                 /* 110 */

    MenuCmnPlyrEquipReinforcedLensDisp(x, y, alpha);                    /* 113 */

    /* Item 10 is the camera itself.  Without it there is no film readout at
     * all, just the four dashes where the two numbers would sit. */
    if (GetPlyrItemHaveNum(10) > 0) {                                   /* 116 */
        MenuFilmDisp(x, y, alpha);                                      /* 118 */
    }
    else {
        MenuCmnHyphenDisp(x + 216, y + 41, alpha, 0);                   /* 122 */
        MenuCmnHyphenDisp(x + 216 + menu_cmn_dat[25].w,
                          y + 41, alpha, 0);                            /* 123 */

        MenuCmnHyphenDisp(x + 253, y + 41, alpha, 0);                   /* 126 */
        MenuCmnHyphenDisp(x + 253 + menu_cmn_dat[25].w,
                          y + 41, alpha, 0);                            /* 127 */
    }
}

/* The health bar.  Four segments, each its own shade, drawn right to left
 * from menu_cmn_dat[9] back to [6]; their x positions are baked into the
 * sprite records, so the loop only has to decide how much of each to show.
 *
 * The one that the remaining width runs out inside is clipped from its left
 * edge -- u and x are both advanced by (w - hp_w) so the visible piece stays
 * flush with the segment's right-hand end -- and the run stops there. */
static void MenuPlyrHPDisp(int x, int y, u_char alpha)                  /* 140 */
{
    DISP_SPRT  plyr_ds;
    int        i;
    static int hp_tex_tbl[4] =                              /* rdata 3bd6f0 */
    {
        9, 8, 7, 6
    };
    int        hp_all_w;
    int        hp_w;

    hp_all_w = 0;                                                       /* 152 */

    for (i = 0; i < 4; i++) {                                           /* 154 */
        hp_all_w += menu_cmn_dat[hp_tex_tbl[i]].w;                      /* 155 */
    }                                                                   /* 156 */

    hp_w = (int)((float)hp_all_w * ((float)plyr_wrk.cmn_wrk.st.hp /
                                    (float)plyr_wrk.cmn_wrk.st.hpmax)); /* 157 */

    PK2SendVram(MENU_STATUS_TEX_ADRS, -1, -1, 0);                       /* 159 */

    for (i = 0; i < 4; i++) {                                           /* 162 */
        if (hp_w - menu_cmn_dat[hp_tex_tbl[i]].w >= 0) {                /* 163 */
            CopySprDToSpr(&plyr_ds, &menu_cmn_dat[hp_tex_tbl[i]]);      /* 164 */
            plyr_ds.x += (float)x;   plyr_ds.y += (float)y;             /* 165 */
            plyr_ds.alpha = alpha;                                      /* 166 */
            DispSprD(&plyr_ds);                                         /* 167 */

            hp_w -= menu_cmn_dat[hp_tex_tbl[i]].w;                      /* 168 */
        }
        else {
            CopySprDToSpr(&plyr_ds, &menu_cmn_dat[hp_tex_tbl[i]]);      /* 171 */
            plyr_ds.x = plyr_ds.x + (float)x + (float)(plyr_ds.w - hp_w);
            plyr_ds.y += (float)y;                                      /* 172 */
            plyr_ds.alpha = alpha;                                      /* 173 */
            plyr_ds.u = plyr_ds.u + plyr_ds.w - hp_w;                   /* 174 */
            plyr_ds.w = hp_w;                                           /* 175 */
            DispSprD(&plyr_ds);                                         /* 176 */

            break;                                                      /* 177 */
        }
    }                                                                   /* 179 */
}

/* The fitted camera sub-functions.  With the tray part fitted there are three
 * slots side by side, 37 pixels apart for the icon and 39 for the level; with
 * it missing there is a single slot further right.  An empty slot
 * (CAMERA_SUB_FUNC_NONE) shows a dash where its level would be.
 *
 * The two MenuNumberDisp() calls report variable.h 124 rather than a line of
 * their own -- see the file header. */
static void MenuCmnPlyrEquipReinforcedLensDisp(int x, int y, u_char alpha)
                                                                        /* 191 */
{
    int  i;
    char equip_special[3];

    m_plyr_camera.eq_tray.GetSubFuncArray(equip_special);               /* 199 */

    if (m_plyr_camera.camera_power_up.mCamPartsSetFlg.IsUp(1) != 0) {
        for (i = 0; i < 3; i++) {                                       /* 203 */
            if (equip_special[i] != 0) {                                /* 205 */
                MenuCmnReinforcedLensDisp(x + 9 + i * 37, y + 15, alpha,
                                          equip_special[i]);            /* 207 */

                PK2SendVram(MENU_STATUS_TEX_ADRS, -1, -1, 0);           /* 209 */

                MenuNumberDisp(
                    m_plyr_camera.eq_tray.mSave.mSubFuncLv[equip_special[i]].Get(),
                    1, x + 31 + i * 39, y + 45, alpha, 0, 0);
            }
            else {
                MenuCmnHyphenDisp(x + 31 + i * 39, y + 45, alpha, 0);   /* 217 */
            }
        }                                                               /* 219 */
    }
    else {
        if (equip_special[0] != 0) {                                    /* 223 */
            MenuCmnReinforcedLensDisp(x + 75, y + 16, alpha,
                                      equip_special[0]);                /* 225 */

            PK2SendVram(MENU_STATUS_TEX_ADRS, -1, -1, 0);               /* 227 */

            MenuNumberDisp(
                m_plyr_camera.eq_tray.mSave.mSubFuncLv[equip_special[0]].Get(),
                1, x + 125, y + 43, alpha, 0, 0);
        }
        else {
            MenuCmnHyphenDisp(x + 125, y + 43, alpha, 0);               /* 235 */
        }
    }
}

/* One sub-function's icon, tinted with that function's own colour out of
 * CNEquipTrayWrk::equip_func_tbl[].  lens_tbl is indexed by
 * CAMERA_SUB_FUNC_ENUM and its -1 for NONE is what makes an empty slot draw
 * nothing; note that x/y here *replace* the sprite's own position rather than
 * offsetting it, unlike every other draw in the file. */
static void MenuCmnReinforcedLensDisp(int x, int y, u_char alpha,
                                      int lens_label)                   /* 249 */
{
    DISP_SPRT  lens_ds;
    static int lens_tbl[10] =                               /* rdata 3bd700 */
    {
        -1, 28, 27, 30, 35, 31, 33, 29, 32, 34
    };

    PK2SendVram(MENU_BG_TEX_ADRS, -1, -1, 0);                           /* 267 */

    if (lens_tbl[lens_label] != -1) {                                   /* 270 */
        CopySprDToSpr(&lens_ds, &menu_cmn_dat[lens_tbl[lens_label]]);   /* 271 */
        lens_ds.x = (float)x;   lens_ds.y = (float)y;                   /* 272 */
        lens_ds.alpha = (u_char)(lens_ds.alpha * alpha >> 7);           /* 273 */
        lens_ds.r = CNEquipTrayWrk::equip_func_tbl[lens_label].r;
        lens_ds.g = CNEquipTrayWrk::equip_func_tbl[lens_label].g;
        lens_ds.b = CNEquipTrayWrk::equip_func_tbl[lens_label].b;       /* 274 */
        DispSprD(&lens_ds);                                             /* 275 */
    }
}

/* The film readout: which film is loaded, and how many are left.
 *
 * GetPlyrEquipmentFilmType() is called five separate times -- functions.txt
 * lists no int local, so the ROM really does re-read it rather than latch it.
 * film_num_tex is the same table n_plyr_camera.c carries as aFilmTypeNo. */
static void MenuFilmDisp(int x, int y, u_char alpha)                    /* 287 */
{
    DISP_SPRT  film_ds;
    static int film_tex[5] =                                /* rdata 3bd728 */
    {
        10, 11, 12, 13, 14
    };
    static int film_num_tex[5] =                            /* rdata 3bd740 */
    {
        7, 14, 61, 90, 0
    };

    PK2SendVram(MENU_STATUS_TEX_ADRS, -1, -1, 0);                       /* 305 */

    CopySprDToSpr(&film_ds,
                  &menu_cmn_dat[film_tex[GetPlyrEquipmentFilmType()]]); /* 309 */
    film_ds.x += (float)x;   film_ds.y += (float)y;                     /* 310 */
    film_ds.alpha = alpha;                                              /* 311 */
    DispSprD(&film_ds);                                                 /* 312 */

    /* Type-00 has no number of its own; it gets a plate instead. */
    if (GetPlyrEquipmentFilmType() == 4) {                              /* 315 */
        CopySprDToSpr(&film_ds, &menu_cmn_dat[26]);                     /* 316 */
        film_ds.x += (float)x;   film_ds.y += (float)y;                 /* 317 */
        film_ds.alpha = alpha;                                          /* 318 */
        DispSprD(&film_ds);                                             /* 319 */
    }
    else {
        MenuNumberDisp(film_num_tex[GetPlyrEquipmentFilmType()], 2,
                       x + 216, y + 41, alpha, 0xa0, 1);                /* 323 */
    }

    /* Type-07 is the unlimited film, so its count is two dashes. */
    if (GetPlyrEquipmentFilmType() == 0) {                              /* 327 */
        MenuCmnHyphenDisp(x + 253, y + 41, alpha, 0);                   /* 328 */
        MenuCmnHyphenDisp(x + 253 + menu_cmn_dat[25].w,
                          y + 41, alpha, 0);                            /* 329 */
    }
    else {
        MenuNumberDisp(GetPlyrItemHaveNum(GetPlyrEquipmentFilmType()), 2,
                       x + 253, y + 41, alpha, 0, 0);                   /* 334 */
    }
}

/* --------------------------------------------------------------------------
 *  Shared chrome
 * ------------------------------------------------------------------------ */

/* `num` digits of `data`, in the menu face.  menu_cmn_dat[15] is the zero
 * glyph and 16..24 are 1..9, which is the run DrawCmnNumberTex() walks. */
void MenuNumberDisp(int data, int num, int x, int y, u_char alpha, int pri,
                    u_char zero_flg)                                    /* 351 */
{
    DrawCmnNumberTex(data, num, &menu_cmn_dat[15], x, y, alpha, pri,
                     zero_flg);                                         /* 354 */
}

/* One dash, the stand-in for a number that has none. */
static void MenuCmnHyphenDisp(int x, int y, u_char alpha, u_int pri)    /* 367 */
{
    DISP_SPRT hyphen_ds;

    PK2SendVram(MENU_STATUS_TEX_ADRS, -1, -1, 0);                       /* 371 */

    CopySprDToSpr(&hyphen_ds, &menu_cmn_dat[25]);                       /* 374 */
    hyphen_ds.x = (float)x;   hyphen_ds.y = (float)y;                   /* 375 */
    hyphen_ds.alpha = alpha;                                            /* 376 */
    hyphen_ds.pri = pri;   hyphen_ds.z = 0xfffff - (pri & 0xfffff);     /* 377 */
    DispSprD(&hyphen_ds);                                               /* 378 */
}

/* The plain modal frame. */
void MenuCmnConfirmWinDisp(int off_x, int off_y, u_char alpha, u_int pri)
                                                                        /* 387 */
{
    DrawCmnWindow(pri, (float)(off_x + 24), (float)(off_y + 178),
                  592.0f, 112.0f, alpha, 0x80);                         /* 391 */
}

/* The same frame with the two answers under it.  The cursor is placed from
 * menu_yes_no_ctrl.csr, which MenuCmnYesNoPad() walks. */
void MenuCmnYesNoWinDisp(int off_x, int off_y, u_char alpha, u_int pri)  /* 402 */
{
    DrawCmnWindow(pri, (float)(off_x + 24), (float)(off_y + 178),
                  592.0f, 112.0f, alpha, 0x80);                         /* 407 */

    DrawCmnSelCsr(pri, (float)(off_x + 155 + menu_yes_no_ctrl.csr * 207),
                  (float)(off_y + 230), alpha, 0.0f, 0);                /* 411 */

    DrawCmnSelYes(pri, (float)(off_x + 153), (float)(off_y + 232), alpha);
                                                                        /* 414 */
    DrawCmnSelNo(pri, (float)(off_x + 361), (float)(off_y + 232), alpha);
                                                                        /* 415 */
}

/* A selected list row's frame: one 82-pixel half scaled to w/2, then its
 * mirror butted against it.  menu_cmn_dat[37] is [36] with flip 2, so the
 * pair meets at x + 36's width * the left scale. */
void MenuCmnSelFrameDisp(float x, float y, float w, u_char alpha, u_int pri)
                                                                        /* 428 */
{
    DISP_SPRT frame_ds;
    float     one_size;
    float     frame_scl_l;
    float     frame_scl_r;

    one_size    = w * 0.5f;                                             /* 436 */

    frame_scl_l = one_size / (float)menu_cmn_dat[36].w;                 /* 439 */
    frame_scl_r = one_size / (float)menu_cmn_dat[37].w;                 /* 440 */

    PK2SendVram(MENU_BG_TEX_ADRS, -1, -1, 0);                           /* 443 */

    CopySprDToSpr(&frame_ds, &menu_cmn_dat[36]);                        /* 446 */
    frame_ds.x = x;   frame_ds.y = y;                                   /* 447 */
    frame_ds.scw = frame_scl_l;   frame_ds.sch = 1.0f;
    frame_ds.csx = frame_ds.x;    frame_ds.csy = frame_ds.y;            /* 448 */
    frame_ds.alpha = (u_char)(frame_ds.alpha * alpha >> 7);             /* 449 */
    frame_ds.pri = pri;   frame_ds.z = 0xfffff - (pri & 0xfffff);       /* 450 */
    DispSprD(&frame_ds);                                                /* 451 */

    CopySprDToSpr(&frame_ds, &menu_cmn_dat[37]);                        /* 454 */
    frame_ds.x = x + (float)menu_cmn_dat[36].w * frame_scl_l;
    frame_ds.y = y;                                                     /* 455 */
    frame_ds.scw = frame_scl_r;   frame_ds.sch = 1.0f;
    frame_ds.csx = frame_ds.x;    frame_ds.csy = frame_ds.y;            /* 456 */
    frame_ds.alpha = (u_char)(frame_ds.alpha * alpha >> 7);             /* 457 */
    frame_ds.pri = pri;   frame_ds.z = 0xfffff - (pri & 0xfffff);       /* 458 */
    DispSprD(&frame_ds);                                                /* 459 */
}

/* The unselected twin.  Identical bar the sprite pair and the pak it comes
 * from -- 38/39 carry a different TEX0 from 36/37, which is why this one
 * uploads the play-data pak rather than the background one. */
void MenuCmnNonSelFrameDisp(float x, float y, float w, u_char alpha, u_int pri)
                                                                        /* 472 */
{
    DISP_SPRT frame_ds;
    float     one_size;
    float     frame_scl_l;
    float     frame_scl_r;

    one_size    = w * 0.5f;                                             /* 480 */

    frame_scl_l = one_size / (float)menu_cmn_dat[38].w;                 /* 483 */
    frame_scl_r = one_size / (float)menu_cmn_dat[39].w;                 /* 484 */

    PK2SendVram(MENU_PLAYDATA_TEX_ADRS, -1, -1, 0);                     /* 487 */

    CopySprDToSpr(&frame_ds, &menu_cmn_dat[38]);                        /* 490 */
    frame_ds.x = x;   frame_ds.y = y;                                   /* 491 */
    frame_ds.scw = frame_scl_l;   frame_ds.sch = 1.0f;
    frame_ds.csx = frame_ds.x;    frame_ds.csy = frame_ds.y;            /* 492 */
    frame_ds.alpha = (u_char)(frame_ds.alpha * alpha >> 7);             /* 493 */
    frame_ds.pri = pri;   frame_ds.z = 0xfffff - (pri & 0xfffff);       /* 494 */
    DispSprD(&frame_ds);                                                /* 495 */

    CopySprDToSpr(&frame_ds, &menu_cmn_dat[39]);                        /* 497 */
    frame_ds.x = x + (float)menu_cmn_dat[38].w * frame_scl_l;
    frame_ds.y = y;                                                     /* 498 */
    frame_ds.scw = frame_scl_r;   frame_ds.sch = 1.0f;
    frame_ds.csx = frame_ds.x;    frame_ds.csy = frame_ds.y;            /* 499 */
    frame_ds.alpha = (u_char)(frame_ds.alpha * alpha >> 7);             /* 500 */
    frame_ds.pri = pri;   frame_ds.z = 0xfffff - (pri & 0xfffff);       /* 501 */
    DispSprD(&frame_ds);                                                /* 502 */
}

/* The vertical rule: cap [40], stretched middle [41], mirrored cap [42].
 *
 * A height shorter than the two caps together clamps line_h to zero -- the
 * caps then overlap and the middle is skipped entirely, which is what the
 * `0.0f < line_h` guard around the third block is for.  line_scr stays 1.0
 * on that path so nothing divides by a zero middle. */
void MenuCmnLineTateDisp(float x, float y, float h, u_char alpha, u_int pri)
                                                                        /* 515 */
{
    DISP_SPRT ds;
    float     line_h;
    float     line_scr;

    line_h   = h - (float)(menu_cmn_dat[40].h + menu_cmn_dat[42].h);    /* 522 */
    line_scr = 1.0f;                                                    /* 524 */

    PK2SendVram(MENU_BG_TEX_ADRS, -1, -1, 0);                           /* 526 */

    if (line_h < 0.0f) {                                                /* 529 */
        line_h = 0.0f;                                                  /* 530 */
    }
    else {
        line_scr = line_h / (float)menu_cmn_dat[41].h;                  /* 533 */
    }

    CopySprDToSpr(&ds, &menu_cmn_dat[40]);                              /* 537 */
    ds.pri = pri;   ds.z = 0xfffff - (pri & 0xfffff);                   /* 538 */
    ds.alpha = (u_char)(ds.alpha * alpha >> 7);                         /* 539 */
    ds.x = x;   ds.y = y;                                               /* 540 */
    DispSprD(&ds);                                                      /* 541 */

    CopySprDToSpr(&ds, &menu_cmn_dat[42]);                              /* 543 */
    ds.pri = pri;   ds.z = 0xfffff - (pri & 0xfffff);                   /* 544 */
    ds.alpha = (u_char)(ds.alpha * alpha >> 7);                         /* 545 */
    ds.x = x;   ds.y = y + line_h + (float)menu_cmn_dat[40].h;          /* 546 */
    DispSprD(&ds);                                                      /* 547 */

    if (0.0f < line_h) {                                                /* 549 */
        CopySprDToSpr(&ds, &menu_cmn_dat[41]);                          /* 550 */
        ds.pri = pri;   ds.z = 0xfffff - (pri & 0xfffff);               /* 551 */
        ds.alpha = (u_char)(ds.alpha * alpha >> 7);                     /* 552 */
        ds.x = x;   ds.y = y + (float)menu_cmn_dat[40].h;               /* 553 */
        ds.scw = 1.0f;   ds.sch = line_scr;
        ds.csx = ds.x;   ds.csy = ds.y;                                 /* 554 */
        DispSprD(&ds);                                                  /* 555 */
    }
}

/* The horizontal rule.  The same function on the other axis: cap [43],
 * stretched middle [44], mirrored cap [45]. */
void MenuCmnLineYokoDisp(float x, float y, float w, u_char alpha, u_int pri)
                                                                        /* 570 */
{
    DISP_SPRT ds;
    float     line_w;
    float     line_scr;

    line_w   = w - (float)(menu_cmn_dat[43].w + menu_cmn_dat[45].w);    /* 577 */
    line_scr = 1.0f;                                                    /* 578 */

    PK2SendVram(MENU_BG_TEX_ADRS, -1, -1, 0);                           /* 580 */

    if (line_w < 0.0f) {                                                /* 583 */
        line_w = 0.0f;                                                  /* 584 */
    }
    else {
        line_scr = line_w / (float)menu_cmn_dat[44].w;                  /* 588 */
    }

    CopySprDToSpr(&ds, &menu_cmn_dat[43]);                              /* 592 */
    ds.pri = pri;   ds.z = 0xfffff - (pri & 0xfffff);                   /* 593 */
    ds.alpha = (u_char)(ds.alpha * alpha >> 7);                         /* 594 */
    ds.x = x;   ds.y = y;                                               /* 595 */
    DispSprD(&ds);                                                      /* 596 */

    CopySprDToSpr(&ds, &menu_cmn_dat[45]);                              /* 598 */
    ds.pri = pri;   ds.z = 0xfffff - (pri & 0xfffff);                   /* 599 */
    ds.alpha = (u_char)(ds.alpha * alpha >> 7);                         /* 600 */
    ds.x = x + line_w + (float)menu_cmn_dat[43].w;   ds.y = y;          /* 601 */
    DispSprD(&ds);                                                      /* 602 */

    if (0.0f < line_w) {                                                /* 604 */
        CopySprDToSpr(&ds, &menu_cmn_dat[44]);                          /* 605 */
        ds.pri = pri;   ds.z = 0xfffff - (pri & 0xfffff);               /* 606 */
        ds.alpha = (u_char)(ds.alpha * alpha >> 7);                     /* 607 */
        ds.x = x + (float)menu_cmn_dat[43].w;   ds.y = y;               /* 608 */
        ds.scw = line_scr;   ds.sch = 1.0f;
        ds.csx = ds.x;       ds.csy = ds.y;                             /* 609 */
        DispSprD(&ds);                                                  /* 610 */
    }
}
