// FILE: /home/zero_rom/zero2np/src/ingame/menu/menu_photo.c
//
// The in-game menu's photo album page (menu_photo.o).  Thirty-two
// functions: six exports, twenty-six statics, six lookup tables in .rodata
// and five floats in .lit4.
//
// The page is the standard menu shape -- a *_CTRL work block, a
// menu_wrk.step ladder, an anim_step/anim_timer fade and a
// Get/LoadReq/LoadWait/Liberate/LoadCancel texture quintet.  What is its own
// is the album: a 2x8 grid of sixteen thumbnail cells down the left, the
// selected shot enlarged on the right, the information window under it, and
// two pop-up menus over the lot.
//
// Facts worth knowing before touching it:
//
//  * csr_num[8][2] is the whole grid geometry.  csr_yoko is the column and
//    csr_tate the row, and the table turns the pair into an album slot --
//    { {0,8}, {1,9}, ... {7,15} }, so the left column is slots 0..7 and the
//    right 8..15.  Every function that needs "the photo under the cursor"
//    starts with that subscript, which is why the same two loads open six
//    of them.
//
//  * LEFT and RIGHT are two separate arms of the pad chain, not one.  Both
//    do `csr_yoko = (csr_yoko + 1) % 2` -- with only two columns either
//    direction is the same move -- and GCC cross-jumped the identical
//    bodies, which is what leaves lines 403..408 with no code at all.  The
//    tell that they are two `if`s rather than one four-term condition is
//    that a `||` chain emits ONE line note (the up arm's rpt-plus-analogue
//    test is all under 388) and this pair emits two, 402 and 409.
//
//  * MENU_PHOTO_CTRL::photo_flg is the enlarged picture's cache flag, and
//    the reason every cursor move ends by clearing it.  It is not a
//    "decompress now" request: MenuPhotoLargePhotoDisp() files one with
//    UncompressPhoto() and raises the flag, and photo_make.o's three-byte
//    handshake then feeds a slice per frame.
//
//  * `step` is the page's INNER state and is deliberately separate from
//    menu_wrk.step.  It picks which of the four pads runs (grid, sub-menu,
//    sort menu, delete confirm) and which pop-up MenuPhotoDisp() draws --
//    and the pop-ups are drawn at a flat alpha of 128 rather than the
//    page's fade, so they do not dim with it.
//
//  * The sub-menu's open/close fade is its own, on MENU_PHOTO_CTRL rather
//    than MENU_PHOTO_DISP, and MenuPhotoSubMenuDisp() drives it inline
//    instead of calling MenuInOutAnimCtrl().  That is why closing it is
//    `sub_anim_step = 3; sub_anim_timer = 10;` from the pad and the *draw*
//    is what finally puts `step` back to the grid.
//
//  * The sort menu is a direction toggle, not a one-shot.  MenuPhotoSortPad()
//    clears sort_flg on every cursor move and MenuPhotoSortMenuMain() flips
//    it after each sort, so picking the same row twice sorts the other way
//    (new/old, protected/unprotected, high/low score).
//
//  * MenuPhotoSubMenuMain()'s protect row does not open a window -- it
//    toggles the protect bit and plays a cue.  Only Delete opens one, and
//    only if the photo is NOT protected; a protected one gets the
//    "cannot delete" window (step 4) instead.
//
//  * Two of the alpha expressions are divisions and the rest are shifts, and
//    the difference is real.  Everywhere else in the file the fade is
//    `ds.alpha * alpha >> 7` (mult, then a bare sra); in
//    MenuPhotoSubMenuDisp() it is `ds.alpha * now_alpha / 128`, which keeps
//    expand_divmod's bias-and-movn sequence because nonzero_bits cannot see
//    through the multiplier.  See [[shift-vs-divide-by-power-of-two]].
//
//  * Twenty-five sprites are never drawn -- menu_photo[19..42] (an inner
//    frame of ticks and hairlines round the big picture) and [187].  The
//    draw list is complete without them.
//
// Reconstructed from the Feb 6 2004 prototype (SLES_523.84), menu_photo.o.
// All 6 ZERO2.MAP exports plus the 26 statics, verified 6/6 against the map
// and 32/33 against functions.txt (its 33rd entry is the PICTURE_SUBJECT
// type_info node, a compiler artifact).  .text is accounted for byte-for-byte
// -- 0x2011f8..0x203fbc = 0x2dc4 = 11716 bytes: thirty-six bodies totalling
// 11640 (the thirty-two real ones plus the four fixed_array boilerplate ones)
// and nineteen 4-byte alignment fills, with no gap of 8 bytes or more
// anywhere.  All six .rodata tables, both local
// SQAR_DAT initialisers and all five .lit4 floats are read straight out of
// the ROM; the sprite table is in tim_dat/menu_photo_dat.c.
//
// Trailing /* NNN */ comments are the original source line numbers, measured
// from the $LM stabs.  Three things routinely leave a statement without a
// line of its own, and those annotations are interpolated into the measured
// gap:
//
//  * a store the scheduler put in a jr/jal delay slot (MenuPhotoDispInit()'s
//    first field, MenuPhotoLargePhotoWinDisp()'s `dsq.alpha = 0`, and the
//    first `alphar` in MenuPhotoSortMenuDisp());
//
//  * a statement whose only memory access goes through a fixed_array
//    subscript -- MenuPhotoInfoDisp()'s subject walk reports fixed_array.h
//    124/125 and drops its own note;
//
//  * a body GCC cross-jumped into its twin, which loses the first copy's
//    notes entirely (the LEFT arm above, and MenuPhotoPad()'s two
//    SystemBankPlay(4) calls).
//
// Two functions carry an `if` whose condition spans several source lines,
// which in C++ emits a note at BOTH the `if` line and the closing paren:
// MenuPhotoInfoDisp() 874/878 and MenuPhotoLargeProtectDisp() 1382/1385.
// Neither has a local to hold the subscript -- functions.txt lists none --
// so the two notes are one statement, not two.

#include "menu_photo.h"

#include "menu.h"                               /* menu_wrk / MENU_BG_TEX_ADRS */
#include "menu_cmn.h"                           /* MenuCmnYesNoPad / anim     */
#include "menu_cmn_disp.h"                      /* MenuCmnYesNoWinDisp        */
#include "tim_dat/menu_photo_dat.h"             /* menu_photo[]               */
#include "zero2_anim2d.h"                       /* Zero2Anim2D_CsrAnimCtrl    */
#include "anim_2d.h"                            /* SCL_ANIM_TBL / Anim2D_*    */
#include "play_data.h"                          /* SetDateInfoType            */

#include "../photo/photo.h"                     /* GetFilePhotoState / sorts  */
#include "../photo/photo_make.h"                /* UncompressPhoto / draws    */
#include "../../common/mem_util.h"              /* mem_utilGetMem             */
#include "../../common/utility2.h"              /* PRINT_ASSERT / PRINT_WARNING */
#include "../../common/variable.h"              /* DATE_INFO                  */
#include "../../graphics/graph2d/draw_cmn.h"    /* DrawCmnWindow / CapGroup_W */
#include "../../graphics/graph2d/g2d_draw.h"    /* DISP_SPRT / DISP_SQAR      */
#include "../../graphics/graph2d/message.h"     /* PrintMsg / PrintNumber_N   */
#include "../../graphics/graph2d/tim2.h"        /* PK2SendVram                */
#include "../../graphics/graph3d/ctl/fixed_array.h"  /* fixed_array           */
#include "../../system/eeiop/cddat.h"           /* GetFileSize / MENU_PHOTO_PK2 */
#include "../../system/eeiop/fileload.h"        /* FileLoadReqEE              */
#include "../../system/eeiop/snd3d.h"           /* SND_3D_SET                 */
#include "../../system/os/system.h"             /* SystemBankPlay / GetLanguage */
#include "../../system/pad/pad.h"               /* pad / paddat               */

#include <stdio.h>                              /* printf                     */

/* --------------------------------------------------------------------------
 *  Constants
 * ------------------------------------------------------------------------ */

/* menu_wrk.step values, the same ladder every page uses. */
#define MENU_PHOTO_STEP_INIT    0
#define MENU_PHOTO_STEP_LOAD    1
#define MENU_PHOTO_STEP_MAIN    2
#define MENU_PHOTO_STEP_OUT     3

/* MENU_PHOTO_DISP::anim_step, and MENU_PHOTO_CTRL::sub_anim_step with it.
 * Only START, OUT and END are named; the rest of the ladder is
 * MenuInOutAnimCtrl()'s business, and the sub-menu's own copy of it is
 * open-coded in MenuPhotoSubMenuDisp(). */
#define MENU_PHOTO_ANIM_START   0
#define MENU_PHOTO_ANIM_IN      1
#define MENU_PHOTO_ANIM_MAIN    2
#define MENU_PHOTO_ANIM_OUT     3
#define MENU_PHOTO_ANIM_END     4

/* MENU_PHOTO_CTRL::step -- the page's inner state. */
#define MENU_PHOTO_MODE_SEL     0   /* the thumbnail grid                    */
#define MENU_PHOTO_MODE_SUB     1   /* the per-photo pop-up                  */
#define MENU_PHOTO_MODE_SORT    2   /* the sort menu                         */
#define MENU_PHOTO_MODE_DEL     3   /* "delete this photo?"                  */
#define MENU_PHOTO_MODE_NOT_DEL 4   /* "it is protected"                     */

/* MENU_PHOTO_CTRL::sub_csr -- the per-photo pop-up's rows. */
#define MENU_PHOTO_SUB_PROTECT  0
#define MENU_PHOTO_SUB_DELETE   1
#define MENU_PHOTO_SUB_SORT     2
#define MENU_PHOTO_SUB_NUM      3

/* MENU_PHOTO_CTRL::sort_csr -- the sort menu's rows.  sort_flg picks the
 * direction, so each row is two orderings. */
#define MENU_PHOTO_SORT_PROTECT 0
#define MENU_PHOTO_SORT_TIME    1
#define MENU_PHOTO_SORT_SCORE   2
#define MENU_PHOTO_SORT_NUM     3

/* The grid. */
#define MENU_PHOTO_CSR_TATE_NUM 8   /* rows                                  */
#define MENU_PHOTO_CSR_YOKO_NUM 2   /* columns                               */

/* PICTURE_WRK::status, as photo.h documents it: bit 0 in use, bit 1
 * protected.  The ROM writes the masks out at every site. */
#define PHOTO_STATE_USE         0x1
#define PHOTO_STATE_PROTECT     0x2

/* MenuCmnYesNoPad() answers. */
#define MENU_CMN_YESNO_NONE     0
#define MENU_CMN_YESNO_YES      1
#define MENU_CMN_YESNO_NO       2

/* The thumbnail cell, and where the two columns start. */
#define MENU_PHOTO_S_W          45
#define MENU_PHOTO_S_H          30
#define MENU_PHOTO_S_X          32
#define MENU_PHOTO_S_X2         87
#define MENU_PHOTO_S_Y          74
#define MENU_PHOTO_S_STEP       34

/* The enlarged picture: 384x256 out of the work area. */
#define MENU_PHOTO_L_X          181
#define MENU_PHOTO_L_Y          78
#define MENU_PHOTO_L_W          384
#define MENU_PHOTO_L_H          256
#define MENU_PHOTO_L_PRI        160

/* The three window frame pieces are stretched to fit rather than tiled. */
#define MENU_PHOTO_FRAME_H      239.0f
#define MENU_PHOTO_FRAME_W      375.0f

/* SystemBankPlay() cue numbers, as everywhere else in the menus.  4 is the
 * protect toggle, which only this page uses. */
#define SE_CURSOR   0
#define SE_CANCEL   1
#define SE_ERROR    2
#define SE_DECIDE   3
#define SE_PROTECT  4

/* pad[0] bits in the remapped layout, plus the analogue equivalents.  The
 * two extra actions are the sub-menu (SQUARE) and the protect toggle
 * (CIRCLE); pad[0].one bit 2 is L1, and only the debug dump reads it. */
#define PAD_RPT_UP              0x1000
#define PAD_RPT_DOWN            0x4000
#define PAD_RPT_LEFT            0x8000
#define PAD_RPT_RIGHT           0x2000
#define PAD_ONE_L1              0x4
#define PAD_ANALOG_UP           0
#define PAD_ANALOG_DOWN         1
#define PAD_ANALOG_LEFT         2
#define PAD_ANALOG_RIGHT        3
#define PAD_ACT_DECIDE          0       /* CROSS                             */
#define PAD_ACT_CANCEL          1       /* TRIANGLE                          */
#define PAD_ACT_SUB             18      /* SQUARE                            */
#define PAD_ACT_PROTECT         20      /* CIRCLE                            */

/* --------------------------------------------------------------------------
 *  Work
 * ------------------------------------------------------------------------ */

static void *menu_photo_tex_addr;                           /* sdata 3f2ec0 */

static MENU_PHOTO_CTRL menu_photo_ctrl;                     /* bss   4b5cb0 */
static MENU_PHOTO_DISP menu_photo_disp;                     /* sbss  3f4e38 */

/* The grid's geometry: [row][column] -> album slot.  The left column is
 * 0..7 and the right 8..15, so a photo's number on screen is its slot. */
static int csr_num[MENU_PHOTO_CSR_TATE_NUM][MENU_PHOTO_CSR_YOKO_NUM] =
{                                                           /* rdata 3be8f8 */
    { 0,  8 },
    { 1,  9 },
    { 2, 10 },
    { 3, 11 },
    { 4, 12 },
    { 5, 13 },
    { 6, 14 },
    { 7, 15 },
};

static void MenuPhotoInit(void);
static void MenuPhotoCtrlInit(void);
static int  MenuPhotoTexLoadWait(void);
static void MenuPhotoPad(void);
static void MenuNonPhotoPad(void);
static void MenuPhotoOutReq(void);
static void MenuPhotoSubPad(void);
static void MenuPhotoDelPad(void);
static void MenuPhotoSortPad(void);
static void MenuPhotoSubMenuMain(void);
static void MenuPhotoSortMenuMain(void);
static void MenuPhotoDispInit(void);

static void MenuPhotoTitleDisp(int off_x, int off_y, u_char alpha);
static void MenuPhotoInfoDisp(int off_x, int off_y, u_char alpha);
static void MenuPhotoDataWinDisp(int off_x, int off_y, u_char alpha);
static void MenuPhotoThumbnailDisp(int off_x, int off_y, u_char alpha);
static void MenuPhotoProtectCsr(int off_x, int off_y, u_char alpha);
static void MenuPhotoLargePhotoWinDisp(int off_x, int off_y, u_char alpha);
static void MenuPhotoLargePhotoDisp(int off_x, int off_y, u_char alpha);
static void MenuPhotoLargeProtectDisp(int off_x, int off_y, u_char alpha);
static void MenuPhotoCaptionDisp(int off_x, int off_y, u_char alpha);
static void MenuPhotoSubMenuDisp(int off_x, int off_y, u_char alpha);
static void MenuPhotoDelConfirmDisp(int off_x, int off_y, u_char alpha);
static void MenuPhotoNotDelWinDisp(int off_x, int off_y, u_char alpha);
static void MenuPhotoSortMenuDisp(int off_x, int off_y, u_char alpha);
static void MenuPhotoNotHaveDisp(int off_x, int off_y, u_char alpha);

/* --------------------------------------------------------------------------
 *  Entry / texture
 * ------------------------------------------------------------------------ */

/* The pak should already be resident -- menu_top.c requests it as the hub
 * fades out -- so a null here means the load never happened and the page
 * starts one of its own rather than drawing over garbage. */
static void MenuPhotoInit(void)                                         /* 201 */
{
    MenuPhotoCtrlInit();                                                /* 205 */

    if (menu_photo_tex_addr == nullptr) {                               /* 208 */
        PRINT_WARNING("Menu Photo Tex Back reading failure\n");         /* 209 */
        MenuPhotoTexLoadReq();                                          /* 210 */
    }
}

/* Nine lines between the first field and the rest hold no code at all --
 * the ROM has a comment block there. */
static void MenuPhotoCtrlInit(void)                                     /* 219 */
{
    menu_photo_ctrl.step           = MENU_PHOTO_MODE_SEL;               /* 222 */

    menu_photo_ctrl.csr_yoko       = 0;                                 /* 232 */
    menu_photo_ctrl.csr_tate       = 0;                                 /* 233 */
    menu_photo_ctrl.sub_csr        = 0;                                 /* 234 */
    menu_photo_ctrl.sort_csr       = 0;                                 /* 235 */
    menu_photo_ctrl.sort_flg       = 0;                                 /* 236 */
    menu_photo_ctrl.photo_flg      = 0;                                 /* 237 */
    menu_photo_ctrl.sub_anim_step  = MENU_PHOTO_ANIM_START;             /* 238 */
    menu_photo_ctrl.sub_anim_timer = 0;                                 /* 239 */
    menu_photo_ctrl.csr_timer      = 0;                                 /* 240 */
    menu_photo_ctrl.rgb            = 0x40;                              /* 241 */
}

void GetMenuPhotoTexMem(void)                                           /* 248 */
{
    if (menu_photo_tex_addr != nullptr) {                               /* 251 */
        LiberateMenuPhotoTexMem();                                      /* 252 */
    }

    if (menu_photo_tex_addr == nullptr) {                               /* 256 */
        menu_photo_tex_addr =
            mem_utilGetMem((int)GetFileSize(MENU_PHOTO_PK2 + GetLanguage())); /* 257 */
    }
}

void MenuPhotoTexLoadReq(void)                                          /* 266 */
{
    if (menu_photo_tex_addr == nullptr) {                               /* 269 */
        GetMenuPhotoTexMem();                                           /* 271 */
    }

    FileLoadReqEE(MENU_PHOTO_PK2 + GetLanguage(), menu_photo_tex_addr,
                  2, nullptr, nullptr);                                 /* 276 */
}

static int MenuPhotoTexLoadWait(void)                                   /* 286 */
{
    if (FileLoadIsEnd2(MENU_PHOTO_PK2 + GetLanguage(),
                       menu_photo_tex_addr) != 0) {                     /* 294 */
        return 1;
    }

    return 0;                                                           /* 299 */
}

/* --------------------------------------------------------------------------
 *  The page
 * ------------------------------------------------------------------------ */

/* One frame.  An empty album takes the whole page down to MenuNonPhotoPad()
 * -- nothing but "back out" -- and `step` picks between the four pads
 * otherwise.  The "cannot delete" window is the only one whose pad is not
 * its own function; MenuCmnConfirmPad() serves it. */
void MenuPhoto(void)                                                    /* 309 */
{
    if (menu_wrk.step == MENU_PHOTO_STEP_INIT) {                        /* 312 */
        MenuPhotoInit();                                                /* 313 */
        MenuPhotoDispInit();                                            /* 314 */
        menu_wrk.step = MENU_PHOTO_STEP_LOAD;                           /* 315 */
    }

    if (menu_wrk.step == MENU_PHOTO_STEP_LOAD) {                        /* 318 */
        if (MenuPhotoTexLoadWait() != 0) {                              /* 319 */
            menu_wrk.step = MENU_PHOTO_STEP_MAIN;                       /* 320 */
        }
    }

    if (menu_wrk.step == MENU_PHOTO_STEP_MAIN) {                        /* 324 */
        if (GetFilePhotoNum() != 0) {                                   /* 325 */

            if (menu_photo_ctrl.step == MENU_PHOTO_MODE_SEL) {          /* 327 */
                MenuPhotoPad();                                         /* 328 */
            }
            else if (menu_photo_ctrl.step == MENU_PHOTO_MODE_SUB) {     /* 331 */
                /* The pad is parked for the whole close fade; the draw
                 * side is what puts `step` back to the grid. */
                if (menu_photo_ctrl.sub_anim_step != MENU_PHOTO_ANIM_OUT) { /* 332 */
                    MenuPhotoSubPad();                                  /* 333 */
                }
            }
            else if (menu_photo_ctrl.step == MENU_PHOTO_MODE_SORT) {    /* 337 */
                MenuPhotoSortPad();                                     /* 338 */
            }
            else if (menu_photo_ctrl.step == MENU_PHOTO_MODE_DEL) {     /* 341 */
                MenuPhotoDelPad();                                      /* 342 */
            }
            else if (menu_photo_ctrl.step == MENU_PHOTO_MODE_NOT_DEL) { /* 345 */
                if (MenuCmnConfirmPad() != 0) {                         /* 346 */
                    menu_photo_ctrl.step = MENU_PHOTO_MODE_SUB;         /* 347 */
                }
            }
        }
        else {
            MenuNonPhotoPad();                                          /* 352 */
        }
    }
    else if (menu_wrk.step == MENU_PHOTO_STEP_OUT) {                    /* 355 */
        if (menu_photo_disp.anim_step == MENU_PHOTO_ANIM_END) {         /* 356 */
            SetNextMenuStep(MENU_STEP_TOP);                             /* 358 */

            LiberateMenuPhotoTexMem();                                  /* 361 */
        }
    }
}

/* --------------------------------------------------------------------------
 *  Pads
 * ------------------------------------------------------------------------ */

/* The grid.  Up/down walk the eight rows and wrap; LEFT and RIGHT are
 * separate arms that both toggle the column, and GCC cross-jumped them.
 * SQUARE opens the sub-menu, CIRCLE toggles protect in place, TRIANGLE
 * leaves, and L1 dumps the album's slot->page mapping.
 *
 * uncompress_flg is raised by every arm that moved the cursor; the tail
 * clears photo_flg so MenuPhotoLargePhotoDisp() files a fresh decompress. */
static void MenuPhotoPad(void)                                          /* 372 */
{
    int    sel_num;
    u_char uncompress_flg;

    sel_num = csr_num[menu_photo_ctrl.csr_tate][menu_photo_ctrl.csr_yoko]; /* 382 */

    uncompress_flg = 0;

    if ((pad[0].rpt & PAD_RPT_UP) || GetPadAnalogRpt(PAD_ANALOG_UP)) {  /* 388 */
        SystemBankPlay(SE_CURSOR, 1, 0, 0, (SND_3D_SET *)nullptr,
                       0x3200, 0x1000);                                 /* 389 */
        menu_photo_ctrl.csr_tate =
            (menu_photo_ctrl.csr_tate + MENU_PHOTO_CSR_TATE_NUM - 1)
                % MENU_PHOTO_CSR_TATE_NUM;                              /* 390 */

        uncompress_flg = 1;                                             /* 392 */
    }

    else if ((pad[0].rpt & PAD_RPT_DOWN) || GetPadAnalogRpt(PAD_ANALOG_DOWN)) { /* 395 */
        SystemBankPlay(SE_CURSOR, 1, 0, 0, (SND_3D_SET *)nullptr,
                       0x3200, 0x1000);                                 /* 396 */
        menu_photo_ctrl.csr_tate =
            (menu_photo_ctrl.csr_tate + 1) % MENU_PHOTO_CSR_TATE_NUM;   /* 397 */

        uncompress_flg = 1;                                             /* 399 */
    }

    /* Two columns, so left and right are the same move -- written out twice
     * and cross-jumped into one body, which is why 403..408 hold no code. */
    else if ((pad[0].rpt & PAD_RPT_LEFT) || GetPadAnalogRpt(PAD_ANALOG_LEFT)) { /* 402 */
        SystemBankPlay(SE_CURSOR, 1, 0, 0, (SND_3D_SET *)nullptr,
                       0x3200, 0x1000);                                 /* 403 */
        menu_photo_ctrl.csr_yoko =
            (menu_photo_ctrl.csr_yoko + 1) % MENU_PHOTO_CSR_YOKO_NUM;   /* 404 */

        uncompress_flg = 1;                                             /* 406 */
    }

    else if ((pad[0].rpt & PAD_RPT_RIGHT) || GetPadAnalogRpt(PAD_ANALOG_RIGHT)) { /* 409 */
        SystemBankPlay(SE_CURSOR, 1, 0, 0, (SND_3D_SET *)nullptr,
                       0x3200, 0x1000);                                 /* 410 */
        menu_photo_ctrl.csr_yoko =
            (menu_photo_ctrl.csr_yoko + 1) % MENU_PHOTO_CSR_YOKO_NUM;   /* 411 */

        uncompress_flg = 1;                                             /* 413 */
    }

    else if (*paddat[PAD_ACT_SUB] == 1) {                               /* 416 */
        if ((GetFilePhotoState((u_char)sel_num) & PHOTO_STATE_USE) != 0) { /* 418 */
            SystemBankPlay(SE_DECIDE, 1, 0, 0, (SND_3D_SET *)nullptr,
                           0x3200, 0x1000);                             /* 419 */

            menu_photo_ctrl.step           = MENU_PHOTO_MODE_SUB;       /* 420 */
            menu_photo_ctrl.sub_csr        = MENU_PHOTO_SUB_PROTECT;    /* 421 */

            menu_photo_ctrl.sub_anim_step  = MENU_PHOTO_ANIM_START;     /* 423 */
            menu_photo_ctrl.sub_anim_timer = 0;                         /* 424 */
        }
        else {
            SystemBankPlay(SE_ERROR, 1, 0, 0, (SND_3D_SET *)nullptr,
                           0x3200, 0x1000);                             /* 426 */
        }
    }

    else if (*paddat[PAD_ACT_CANCEL] == 1) {                            /* 430 */
        SystemBankPlay(SE_CANCEL, 1, 0, 0, (SND_3D_SET *)nullptr,
                       0x3200, 0x1000);                                 /* 431 */
        MenuPhotoOutReq();                                              /* 432 */
    }

    else if (*paddat[PAD_ACT_PROTECT] == 1) {                           /* 435 */
        if ((GetFilePhotoState((u_char)sel_num) & PHOTO_STATE_USE) != 0) { /* 437 */

            if ((GetFilePhotoState((u_char)sel_num) & PHOTO_STATE_PROTECT) != 0) { /* 439 */
                DelFilePhotoProtect((u_char)sel_num);                   /* 441 */

                SystemBankPlay(SE_PROTECT, 1, 0, 0, (SND_3D_SET *)nullptr,
                               0x3200, 0x1000);                         /* 443 */
            }
            else {
                SetFilePhotoProtect((u_char)sel_num);                   /* 445 */

                SystemBankPlay(SE_PROTECT, 1, 0, 0, (SND_3D_SET *)nullptr,
                               0x3200, 0x1000);                         /* 448 */
            }
        }
        else {
            SystemBankPlay(SE_ERROR, 1, 0, 0, (SND_3D_SET *)nullptr,
                           0x3200, 0x1000);                             /* 451 */
        }
    }

    /* Debug: dump every slot's photo-data page. */
    else if (pad[0].one & PAD_ONE_L1) {                                 /* 455 */
        for (int i = 0; i < PHOTO_FILE_MAX; i++) {                      /* 456 */
            PICTURE_WRK *pic_info = GetPhotoData((u_char)i);            /* 457 */

            printf("pfile photo [%d] adr no[%d]\n", i, pic_info->adr_no); /* 458 */
        }
    }

    /* The selection moved, so whatever is decompressed is the wrong shot. */
    if (uncompress_flg != 0) {                                          /* 464 */
        menu_photo_ctrl.photo_flg = 0;                                  /* 466 */
    }
}

/* An empty album: nothing to select, so the only thing the page does is
 * close. */
static void MenuNonPhotoPad(void)                                       /* 474 */
{
    if (*paddat[PAD_ACT_CANCEL] == 1) {                                 /* 478 */
        SystemBankPlay(SE_CANCEL, 1, 0, 0, (SND_3D_SET *)nullptr,
                       0x3200, 0x1000);                                 /* 479 */
        MenuPhotoOutReq();                                              /* 480 */
    }
}

static void MenuPhotoOutReq(void)                                       /* 490 */
{
    menu_wrk.step = MENU_PHOTO_STEP_OUT;                                /* 493 */

    menu_photo_disp.anim_step  = MENU_PHOTO_ANIM_OUT;                   /* 494 */
    menu_photo_disp.anim_timer = 0;                                     /* 495 */
}

/* The per-photo pop-up.  Backing out does not close it here: it starts the
 * close fade at its far end (timer 10) and MenuPhotoSubMenuDisp() runs it
 * down and puts `step` back. */
static void MenuPhotoSubPad(void)                                       /* 502 */
{
    if ((pad[0].rpt & PAD_RPT_UP) || GetPadAnalogRpt(PAD_ANALOG_UP)) {  /* 506 */
        SystemBankPlay(SE_CURSOR, 1, 0, 0, (SND_3D_SET *)nullptr,
                       0x3200, 0x1000);                                 /* 507 */
        menu_photo_ctrl.sub_csr =
            (menu_photo_ctrl.sub_csr + MENU_PHOTO_SUB_NUM - 1)
                % MENU_PHOTO_SUB_NUM;                                   /* 508 */
    }

    else if ((pad[0].rpt & PAD_RPT_DOWN) || GetPadAnalogRpt(PAD_ANALOG_DOWN)) { /* 511 */
        SystemBankPlay(SE_CURSOR, 1, 0, 0, (SND_3D_SET *)nullptr,
                       0x3200, 0x1000);                                 /* 512 */
        menu_photo_ctrl.sub_csr =
            (menu_photo_ctrl.sub_csr + 1) % MENU_PHOTO_SUB_NUM;         /* 513 */
    }

    else if (*paddat[PAD_ACT_DECIDE] == 1) {                            /* 516 */
        MenuPhotoSubMenuMain();                                         /* 517 */
    }

    else if (*paddat[PAD_ACT_CANCEL] == 1) {                            /* 520 */
        SystemBankPlay(SE_CANCEL, 1, 0, 0, (SND_3D_SET *)nullptr,
                       0x3200, 0x1000);                                 /* 521 */
        menu_photo_ctrl.sub_anim_step  = MENU_PHOTO_ANIM_OUT;           /* 522 */
        menu_photo_ctrl.sub_anim_timer = 10;                            /* 523 */
    }
}

/* "Delete this photo?".  Answering no goes back to the pop-up, not to the
 * grid; only a completed delete returns to it -- and clears photo_flg,
 * because the slot the cursor is on now holds a different shot. */
static void MenuPhotoDelPad(void)                                       /* 533 */
{
    int sel_num;

    sel_num = csr_num[menu_photo_ctrl.csr_tate][menu_photo_ctrl.csr_yoko]; /* 536 */

    switch (MenuCmnYesNoPad()) {                                        /* 538 */
    case MENU_CMN_YESNO_NONE:
        break;

    case MENU_CMN_YESNO_YES:

        DeletePhotoData((u_char)sel_num);                               /* 544 */

        menu_photo_ctrl.step      = MENU_PHOTO_MODE_SEL;                /* 546 */
        menu_photo_ctrl.photo_flg = 0;                                  /* 547 */
        break;

    case MENU_CMN_YESNO_NO:
        menu_photo_ctrl.step = MENU_PHOTO_MODE_SUB;                     /* 550 */
        break;

    default:
        PRINT_WARNING("Label Error!! MenuPhotoDelPad()");               /* 552 */
        break;
    }
}

/* The sort menu.  Every cursor move clears sort_flg, so a row always sorts
 * one way the first time it is picked and the other way the second. */
static void MenuPhotoSortPad(void)                                      /* 562 */
{
    if ((pad[0].rpt & PAD_RPT_UP) || GetPadAnalogRpt(PAD_ANALOG_UP)) {  /* 566 */
        SystemBankPlay(SE_CURSOR, 1, 0, 0, (SND_3D_SET *)nullptr,
                       0x3200, 0x1000);                                 /* 567 */
        menu_photo_ctrl.sort_csr =
            (menu_photo_ctrl.sort_csr + MENU_PHOTO_SORT_NUM - 1)
                % MENU_PHOTO_SORT_NUM;                                  /* 568 */
        menu_photo_ctrl.sort_flg = 0;                                   /* 569 */
    }

    else if ((pad[0].rpt & PAD_RPT_DOWN) || GetPadAnalogRpt(PAD_ANALOG_DOWN)) { /* 572 */
        SystemBankPlay(SE_CURSOR, 1, 0, 0, (SND_3D_SET *)nullptr,
                       0x3200, 0x1000);                                 /* 573 */
        menu_photo_ctrl.sort_csr =
            (menu_photo_ctrl.sort_csr + 1) % MENU_PHOTO_SORT_NUM;       /* 574 */
        menu_photo_ctrl.sort_flg = 0;                                   /* 575 */
    }

    else if (*paddat[PAD_ACT_DECIDE] == 1) {                            /* 578 */
        SystemBankPlay(SE_DECIDE, 1, 0, 0, (SND_3D_SET *)nullptr,
                       0x3200, 0x1000);                                 /* 579 */
        MenuPhotoSortMenuMain();                                        /* 580 */
    }

    else if (*paddat[PAD_ACT_CANCEL] == 1) {                            /* 583 */
        SystemBankPlay(SE_CANCEL, 1, 0, 0, (SND_3D_SET *)nullptr,
                       0x3200, 0x1000);                                 /* 584 */
        menu_photo_ctrl.step = MENU_PHOTO_MODE_SUB;                     /* 585 */
    }
}

/* The pop-up's decision.  Protect toggles in place; Delete opens the
 * confirm window, or the "it is protected" one if it cannot; Sort hands the
 * page over to the sort menu.
 *
 * The `step = MENU_PHOTO_MODE_SORT` store reuses the switch value's own
 * register -- CSE knows sub_csr == 2 there -- so the constant does not get
 * an `li` of its own. */
static void MenuPhotoSubMenuMain(void)                                  /* 595 */
{
    int sel_num;

    sel_num = csr_num[menu_photo_ctrl.csr_tate][menu_photo_ctrl.csr_yoko]; /* 599 */

    switch (menu_photo_ctrl.sub_csr) {                                  /* 601 */
    case MENU_PHOTO_SUB_PROTECT:
        SystemBankPlay(SE_PROTECT, 1, 0, 0, (SND_3D_SET *)nullptr,
                       0x3200, 0x1000);                                 /* 603 */

        if ((GetFilePhotoState((u_char)sel_num) & PHOTO_STATE_PROTECT) != 0) { /* 605 */
            DelFilePhotoProtect((u_char)sel_num);                       /* 607 */
        }
        else {
            SetFilePhotoProtect((u_char)sel_num);                       /* 611 */
        }

        break;                                                          /* 614 */

    case MENU_PHOTO_SUB_DELETE:
        SystemBankPlay(SE_DECIDE, 1, 0, 0, (SND_3D_SET *)nullptr,
                       0x3200, 0x1000);                                 /* 616 */

        if ((GetFilePhotoState((u_char)sel_num) & PHOTO_STATE_PROTECT) == 0) { /* 618 */
            menu_photo_ctrl.step = MENU_PHOTO_MODE_DEL;                 /* 619 */
            MenuYesNoCtrlInit(MENU_CMN_YESNO_YES);                      /* 620 */
        }
        else {
            menu_photo_ctrl.step = MENU_PHOTO_MODE_NOT_DEL;             /* 625 */
        }
        break;

    case MENU_PHOTO_SUB_SORT:
        SystemBankPlay(SE_DECIDE, 1, 0, 0, (SND_3D_SET *)nullptr,
                       0x3200, 0x1000);                                 /* 627 */

        menu_photo_ctrl.step     = MENU_PHOTO_MODE_SORT;                /* 628 */

        menu_photo_ctrl.sort_flg = 0;                                   /* 630 */
        menu_photo_ctrl.sort_csr = MENU_PHOTO_SORT_PROTECT;             /* 631 */
        break;

    default:
        printf("ERROR!! MenuPhotoSubMenuMain()\n");                     /* 633 */
        break;
    }
}

/* Run the chosen ordering, then flip the direction so the same row picked
 * again reverses it.  photo_flg goes with it: the cursor has not moved but
 * every slot now holds a different shot. */
static void MenuPhotoSortMenuMain(void)                                 /* 642 */
{
    switch (menu_photo_ctrl.sort_csr) {                                 /* 645 */
    case MENU_PHOTO_SORT_PROTECT:
        if (menu_photo_ctrl.sort_flg == 0) {                            /* 647 */
            SortPhotoData_Protect(GetCamPhotoFile());                   /* 648 */
        }
        else if (menu_photo_ctrl.sort_flg == 1) {                       /* 650 */
            SortPhotoData_NonProtect(GetCamPhotoFile());                /* 651 */
        }
        break;                                                          /* 653 */
    case MENU_PHOTO_SORT_TIME:
        if (menu_photo_ctrl.sort_flg == 0) {                            /* 655 */
            SortPhotoData_NewTime(GetCamPhotoFile());                   /* 656 */
        }
        else if (menu_photo_ctrl.sort_flg == 1) {                       /* 658 */
            SortPhotoData_OldTime(GetCamPhotoFile());                   /* 659 */
        }
        break;                                                          /* 661 */
    case MENU_PHOTO_SORT_SCORE:
        if (menu_photo_ctrl.sort_flg == 0) {                            /* 663 */
            SortPhotoData_BigScore(GetCamPhotoFile());                  /* 664 */
        }
        else if (menu_photo_ctrl.sort_flg == 1) {                       /* 666 */
            SortPhotoData_SmallScore(GetCamPhotoFile());                /* 667 */
        }
        break;                                                          /* 669 */
    default:
        printf("ERROR!! MenuPhotoSortMenuMain()\n");                    /* 671 */
        break;
    }

    if (menu_photo_ctrl.sort_flg == 0) {                                /* 674 */
        menu_photo_ctrl.sort_flg = 1;                                   /* 675 */
    }
    else {
        menu_photo_ctrl.sort_flg = 0;
    }

    menu_photo_ctrl.photo_flg = 0;                                      /* 682 */
}

/* --------------------------------------------------------------------------
 *  Drawing
 * ------------------------------------------------------------------------ */

void LiberateMenuPhotoTexMem(void)                                      /* 692 */
{
    if (menu_photo_tex_addr != nullptr) {                               /* 695 */
        mem_utilFreeMem(menu_photo_tex_addr);                           /* 696 */
        menu_photo_tex_addr = nullptr;                                  /* 697 */
    }
}

void MenuPhotoTexLoadCancel(void)                                       /* 706 */
{
    if (MenuPhotoTexLoadWait() == 0) {                                  /* 710 */
        FileLoadCancel2(MENU_PHOTO_PK2 + GetLanguage(), menu_photo_tex_addr,
                        nullptr, nullptr);                              /* 711 */
    }
}

static void MenuPhotoDispInit(void)                                     /* 724 */
{
    menu_photo_disp.anim_step  = MENU_PHOTO_ANIM_START;                 /* 727 */
    menu_photo_disp.anim_timer = 0;                                     /* 728 */
}

/* The whole page, in draw order.  Note the pop-ups take a flat 128 rather
 * than `alpha`: they are not part of the page's fade, and the sub-menu runs
 * a fade of its own inside MenuPhotoSubMenuDisp(). */
void MenuPhotoDisp(void)                                                /* 735 */
{
    u_char alpha;

    alpha = 0;

    if (menu_wrk.step == MENU_PHOTO_STEP_MAIN
        || menu_wrk.step == MENU_PHOTO_STEP_OUT) {                      /* 741 */
        MenuInOutAnimCtrl(&menu_photo_disp.anim_step,
                          &menu_photo_disp.anim_timer, &alpha);         /* 744 */

        Zero2Anim2D_CsrAnimCtrl(&menu_photo_ctrl.csr_timer,
                                &menu_photo_ctrl.rgb);                  /* 747 */

        if (menu_photo_disp.anim_step != MENU_PHOTO_ANIM_END) {         /* 749 */
            MenuPhotoTitleDisp(0, 0, alpha);                            /* 751 */

            if (GetFilePhotoNum() > 0) {                                /* 754 */
                MenuPhotoCaptionDisp(0, 0, alpha);                      /* 757 */

                MenuPhotoThumbnailDisp(0, 0, alpha);                    /* 760 */

                MenuPhotoDataWinDisp(0, 0, alpha);                      /* 763 */

                MenuPhotoInfoDisp(0, 0, alpha);                         /* 766 */

                MenuPhotoProtectCsr(0, 0, alpha);                       /* 769 */

                MenuPhotoLargePhotoWinDisp(0, 0, alpha);                /* 772 */

                MenuPhotoLargePhotoDisp(0, 0, alpha);                   /* 775 */

                MenuPhotoLargeProtectDisp(0, 0, alpha);                 /* 778 */

                if (menu_photo_ctrl.step == MENU_PHOTO_MODE_SUB) {      /* 780 */
                    MenuPhotoSubMenuDisp(0, 0, 128);                    /* 782 */
                }
                else if (menu_photo_ctrl.step == MENU_PHOTO_MODE_DEL) { /* 784 */
                    MenuPhotoSubMenuDisp(0, 0, 128);                    /* 786 */
                    MenuPhotoDelConfirmDisp(0, 0, 128);                 /* 787 */
                }
                else if (menu_photo_ctrl.step == MENU_PHOTO_MODE_NOT_DEL) { /* 789 */
                    MenuPhotoSubMenuDisp(0, 0, 128);                    /* 791 */
                    MenuPhotoNotDelWinDisp(0, 0, 128);                  /* 792 */
                }
                else if (menu_photo_ctrl.step == MENU_PHOTO_MODE_SORT) { /* 794 */
                    MenuPhotoSortMenuDisp(0, 0, 128);                   /* 796 */
                }
            }
            else {
                MenuPhotoNotHaveDisp(0, 0, alpha);                      /* 801 */
            }
        }
    }
}

/* The page title: the two plate halves out of the MENU_BG pak, then the word
 * itself out of the album's own. */
static void MenuPhotoTitleDisp(int off_x, int off_y, u_char alpha)       /* 816 */
{
    DISP_SPRT title_ds;

    PK2SendVram(MENU_BG_TEX_ADRS, -1, -1, 0);                           /* 820 */

    CopySprDToSpr(&title_ds, &menu_photo[185]);                         /* 823 */
    title_ds.x += (float)off_x;   title_ds.y += (float)off_y;           /* 824 */
    title_ds.alpha = (u_char)(title_ds.alpha * alpha >> 7);             /* 825 */
    DispSprD(&title_ds);                                                /* 826 */

    CopySprDToSpr(&title_ds, &menu_photo[186]);                         /* 827 */
    title_ds.x += (float)off_x;   title_ds.y += (float)off_y;           /* 828 */
    title_ds.alpha = (u_char)(title_ds.alpha * alpha >> 7);             /* 829 */
    DispSprD(&title_ds);                                                /* 830 */

    PK2SendVram((uintptr_t)menu_photo_tex_addr, -1, -1, 0);             /* 833 */

    CopySprDToSpr(&title_ds, &menu_photo[184]);                         /* 835 */
    title_ds.x += (float)off_x;   title_ds.y += (float)off_y;           /* 836 */
    title_ds.alpha = (u_char)(title_ds.alpha * alpha >> 7);             /* 837 */
    DispSprD(&title_ds);                                                /* 838 */
}

/* Everything written under the enlarged picture: the room name, the date and
 * time the shot was taken, its score, and the list of what it caught.
 *
 * The subject list is a fixed_array<PICTURE_SUBJECT,3> terminated by a
 * negative `type`, and it is walked twice: once to count, so the block can be
 * centred, and once to print.  Every one of those subscripts reports
 * fixed_array.h 124/125 and drops the statement's own note, which is why the
 * loops carry interpolated line numbers.
 *
 * The whole `if` spans 874..878 in the ROM -- a condition written across
 * several lines emits a note at both ends in C++ -- and there is no local
 * for the subscript. */
static void MenuPhotoInfoDisp(int off_x, int off_y, u_char alpha)       /* 849 */
{
    int          i;
    int          subject_num;
    int          msg_y;
    PICTURE_WRK *pic_info;
    DATE_INFO    date;

    subject_num = 0;                                                    /* 875 */

    if ((GetFilePhotoState(
             (u_char)csr_num[menu_photo_ctrl.csr_tate][menu_photo_ctrl.csr_yoko])
         & PHOTO_STATE_USE) != 0) {                                     /* 874-878 */
        pic_info = GetPhotoData(
            (u_char)csr_num[menu_photo_ctrl.csr_tate][menu_photo_ctrl.csr_yoko]); /* 880 */

        SetDateInfoType(&date, &pic_info->time);                        /* 882 */

        /* 0xf0 and up are the rooms with no name of their own.  The test is
         * unsigned and the PrintMsg() argument is not: the ROM loads `room`
         * with lhu and compares with sltiu, then sign-extends the same value
         * for the call.  PICTURE_WRK::room is a signed short, so the compare
         * carries a cast.  See [[load-width-settles-signedness]]. */
        if ((u_short)pic_info->room < 0xf0) {                           /* 885 */
            PrintMsg(0x4a, pic_info->room, off_x + 0x4e, off_y + 0x161,
                     2, alpha, 0);                                      /* 887 */
        }

        PrintNumber_N(date.day.year, 2, off_x + 0xa2, off_y + 0x17a,
                      2, alpha, 0, 1, 1);                               /* 892 */
        PrintMsg(8, 1, off_x + 0x6a, off_y + 0x17a, 2, alpha, 0);       /* 894 */
        PrintNumber_N(date.day.month, 2, off_x + 0x78, off_y + 0x17a,
                      2, alpha, 0, 1, 1);                               /* 896 */
        PrintMsg(8, 1, off_x + 0x94, off_y + 0x17a, 2, alpha, 0);       /* 898 */
        PrintNumber_N(date.day.day, 2, off_x + 0x4e, off_y + 0x17a,
                      2, alpha, 0, 1, 1);                               /* 900 */

        PrintNumber_N(date.time.hour, 2, off_x + 0xcc, off_y + 0x17a,
                      2, alpha, 0, 1, 1);                               /* 903 */
        PrintMsg(8, 0, off_x + 0xe9, off_y + 0x17a, 2, alpha, 0);       /* 905 */
        PrintNumber_N(date.time.min, 2, off_x + 0xf4, off_y + 0x17a,
                      2, alpha, 0, 1, 1);                               /* 907 */
        PrintMsg(8, 0, off_x + 0x111, off_y + 0x17a, 2, alpha, 0);      /* 909 */
        PrintNumber_N(date.time.sec, 2, off_x + 0x11c, off_y + 0x17a,
                      2, alpha, 0, 1, 1);                               /* 911 */

        PrintNumber_N(pic_info->score, 5, off_x + 0xb6, off_y + 0x192,
                      2, alpha, 0, 0, 0);                               /* 915 */
        PrintMsg(8, 2, off_x + 0x112, off_y + 0x192, 2, alpha, 0);      /* 918 */

        for (i = 0; i < 3; i++) {                                       /* 922 */
            if (pic_info->maSubject[i].type < 0) {                      /* 923 */
                break;                                                  /* 924 */
            }

            subject_num++;                                              /* 927 */
        }                                                               /* 929 */

        /* One, two or three names, each 0x18 apart -- the block is dropped
         * so it stays centred on the window. */
        switch (subject_num) {                                          /* 931 */
        case 1:
            msg_y = 0x17a;                                              /* 934 */
            break;
        case 2:
            msg_y = 0x16e;                                              /* 937 */
            break;
        case 3:
            msg_y = 0x162;                                              /* 939 */
            break;
        default:
            msg_y = 0x162;                                              /* 940 */
            break;
        }

        msg_y += off_y;                                                 /* 945 */

        for (i = 0; i < 3; i++) {                                       /* 946 */
            if (pic_info->maSubject[i].type < 0) {
                return;                                                 /* 947 */
            }

            if (pic_info->maSubject[i].obj_no < 0
                || GetMsgIDNumMax(pic_info->maSubject[i].type)
                       <= pic_info->maSubject[i].obj_no) {
                PRINT_ASSERT("Error! MenuPhotoInfoDisp msg_id %d",
                             pic_info->maSubject[i].obj_no);            /* 953 */
            }

            PrintMsg(pic_info->maSubject[i].type,
                     pic_info->maSubject[i].obj_no,
                     off_x + 0x16f, msg_y, 2, alpha, 0);                /* 959 */
            msg_y += 0x18;                                             /* 960 */
        }
    }
}

/* The information window down the left of the page, plus the caption bar
 * across the bottom.  The four vertical rules are the same sprite as the
 * horizontal ones drawn rot 270 about (x, y + w) -- the record's own width
 * is what places the rotated piece. */
static void MenuPhotoDataWinDisp(int off_x, int off_y, u_char alpha)    /* 973 */
{
    DISP_SPRT win_ds;
    int       i;

    PK2SendVram((uintptr_t)menu_photo_tex_addr, -1, -1, 0);             /* 979 */

    for (i = 0; i < 5; i++) {                                           /* 982 */
        CopySprDToSpr(&win_ds, &menu_photo[i]);                         /* 983 */
        win_ds.x += (float)off_x;   win_ds.y += (float)off_y;           /* 984 */
        win_ds.alpha = (u_char)(win_ds.alpha * alpha >> 7);             /* 985 */
        DispSprD(&win_ds);                                              /* 986 */
    }                                                                   /* 987 */

    CopySprDToSpr(&win_ds, &menu_photo[5]);                             /* 990 */
    win_ds.x += (float)off_x;   win_ds.y += (float)off_y;               /* 991 */
    win_ds.alpha = (u_char)(win_ds.alpha * alpha >> 7);                 /* 992 */
    DispSprD(&win_ds);                                                  /* 993 */

    CopySprDToSpr(&win_ds, &menu_photo[6]);                             /* 995 */
    win_ds.x += (float)off_x;   win_ds.y += (float)off_y;               /* 996 */
    win_ds.alpha = (u_char)(win_ds.alpha * alpha >> 7);                 /* 997 */
    DispSprD(&win_ds);                                                  /* 998 */

    for (i = 0; i < 15; i++) {                                          /* 1001 */
        CopySprDToSpr(&win_ds, &menu_photo[154 + i]);                   /* 1002 */
        win_ds.x += (float)off_x;   win_ds.y += (float)off_y;           /* 1003 */
        win_ds.alpha = (u_char)(win_ds.alpha * alpha >> 7);             /* 1004 */
        DispSprD(&win_ds);                                              /* 1005 */
    }                                                                   /* 1006 */

    for (i = 0; i < 4; i++) {                                           /* 1009 */
        CopySprDToSpr(&win_ds, &menu_photo[169 + i]);                   /* 1010 */
        win_ds.x += (float)off_x;
        win_ds.y += (float)off_y + (float)menu_photo[169 + i].w;        /* 1011 */
        win_ds.alpha = (u_char)(win_ds.alpha * alpha >> 7);             /* 1012 */
        win_ds.crx = win_ds.x;   win_ds.cry = win_ds.y;   win_ds.rot = 270.0f; /* 1013 */
        DispSprD(&win_ds);                                              /* 1014 */
    }                                                                   /* 1015 */

    for (i = 0; i < 11; i++) {                                          /* 1017 */
        CopySprDToSpr(&win_ds, &menu_photo[173 + i]);                   /* 1018 */
        win_ds.x += (float)off_x;   win_ds.y += (float)off_y;           /* 1019 */
        win_ds.alpha = (u_char)(win_ds.alpha * alpha >> 7);             /* 1020 */
        DispSprD(&win_ds);                                              /* 1021 */
    }                                                                   /* 1022 */
}

/* The sixteen thumbnail cells.  A used slot draws the picture itself out of
 * the small-photo area; an empty one draws its plate and its number.  The
 * selected cell gets a highlight on top either way, tinted with the shared
 * cursor pulse while the grid has the pad. */
static void MenuPhotoThumbnailDisp(int off_x, int off_y, u_char alpha)  /* 1033 */
{
    int       i;
    int       sel_num;
    DISP_SPRT photo_ds;

    /* Which menu_photo[] entry each cell's number, highlight and empty plate
     * is.  A one-digit number leaves the second slot -1. */
    static int number_tbl[16][2] =                          /* rdata 3be650 */
    {
        { 131,  -1 }, { 132,  -1 }, { 133,  -1 }, { 134,  -1 },
        { 135,  -1 }, { 136,  -1 }, { 137,  -1 }, { 138,  -1 },
        { 139,  -1 }, { 140, 141 }, { 142, 143 }, { 144, 145 },
        { 146, 147 }, { 148, 149 }, { 150, 151 }, { 152, 153 },
    };
    static int sel_tbl[16] =                                /* rdata 3be6d0 */
    {
        115, 116, 117, 118, 119, 120, 121, 122,
        123, 124, 125, 126, 127, 128, 129, 130,
    };
    static int bg_tbl[16] =                                 /* rdata 3be710 */
    {
        83, 84, 85, 86, 87, 88, 89, 90,
        91, 92, 93, 94, 95, 96, 97, 98,
    };

    PK2SendVram((uintptr_t)menu_photo_tex_addr, -1, -1, 0);             /* 1099 */

    sel_num = csr_num[menu_photo_ctrl.csr_tate][menu_photo_ctrl.csr_yoko]; /* 1100 */

    for (i = 0; i < PHOTO_FILE_MAX; i++) {                              /* 1102 */
        if ((GetFilePhotoState((u_char)i) & PHOTO_STATE_USE) != 0) {    /* 1104 */
            if (i < 8) {                                                /* 1105 */
                DrawSPhotoFromSmallPhotoArea2(
                    GetFilePhotoAdrNo((u_char)i), 0, 0,
                    off_x + MENU_PHOTO_S_X,
                    off_y + MENU_PHOTO_S_Y + i * MENU_PHOTO_S_STEP,
                    MENU_PHOTO_S_W, MENU_PHOTO_S_H, alpha);             /* 1109 */
            }
            else {
                DrawSPhotoFromSmallPhotoArea2(
                    GetFilePhotoAdrNo((u_char)i), 0, 0,
                    off_x + MENU_PHOTO_S_X2,
                    off_y + MENU_PHOTO_S_Y + (i - 8) * MENU_PHOTO_S_STEP,
                    MENU_PHOTO_S_W, MENU_PHOTO_S_H, alpha);             /* 1115 */
            }
        }
        else {
            CopySprDToSpr(&photo_ds, &menu_photo[bg_tbl[i]]);           /* 1122 */
            photo_ds.x += (float)off_x;   photo_ds.y += (float)off_y;   /* 1123 */
            photo_ds.alpha = (u_char)(photo_ds.alpha * alpha >> 7);     /* 1124 */
            DispSprD(&photo_ds);                                        /* 1125 */

            CopySprDToSpr(&photo_ds, &menu_photo[number_tbl[i][0]]);    /* 1128 */
            photo_ds.x += (float)off_x;   photo_ds.y += (float)off_y;   /* 1129 */
            photo_ds.alpha = (u_char)(photo_ds.alpha * alpha >> 7);     /* 1130 */
            DispSprD(&photo_ds);                                        /* 1131 */

            if (number_tbl[i][1] != -1) {                               /* 1132 */
                CopySprDToSpr(&photo_ds, &menu_photo[number_tbl[i][1]]); /* 1133 */
                photo_ds.x += (float)off_x;   photo_ds.y += (float)off_y; /* 1134 */
                photo_ds.alpha = (u_char)(photo_ds.alpha * alpha >> 7); /* 1135 */
                DispSprD(&photo_ds);                                    /* 1136 */
            }
        }

        if (i == sel_num) {                                             /* 1141 */
            CopySprDToSpr(&photo_ds, &menu_photo[sel_tbl[i]]);          /* 1142 */
            photo_ds.x += (float)off_x;   photo_ds.y += (float)off_y;   /* 1143 */
            photo_ds.alphar = 0x48;                                     /* 1144 */
            photo_ds.alpha = (u_char)(photo_ds.alpha * alpha >> 7);     /* 1145 */

            /* Only pulses while the grid actually has the pad. */
            if (menu_photo_ctrl.step == MENU_PHOTO_MODE_SEL) {          /* 1146 */
                photo_ds.r = menu_photo_ctrl.rgb;
                photo_ds.g = menu_photo_ctrl.rgb;
                photo_ds.b = menu_photo_ctrl.rgb;                   /* 1147 */
            }

            DispSprD(&photo_ds);                                        /* 1149 */
        }
    }                                                                   /* 1151 */
}

/* The little padlock over every protected thumbnail. */
static void MenuPhotoProtectCsr(int off_x, int off_y, u_char alpha)     /* 1162 */
{
    int       i;
    DISP_SPRT photo_ds;

    static int protect_tbl[16] =                            /* rdata 3be750 */
    {
        99, 100, 101, 102, 103, 104, 105, 106,
        107, 108, 109, 110, 111, 112, 113, 114,
    };

    PK2SendVram((uintptr_t)menu_photo_tex_addr, -1, -1, 0);             /* 1187 */

    for (i = 0; i < PHOTO_FILE_MAX; i++) {                              /* 1189 */
        if ((GetFilePhotoState((u_char)i) & PHOTO_STATE_PROTECT) != 0) { /* 1191 */
            CopySprDToSpr(&photo_ds, &menu_photo[protect_tbl[i]]);      /* 1192 */
            photo_ds.x += (float)off_x;   photo_ds.y += (float)off_y;   /* 1193 */
            photo_ds.alpha = (u_char)(photo_ds.alpha * alpha >> 7);     /* 1194 */
            DispSprD(&photo_ds);                                        /* 1195 */
        }
    }                                                                   /* 1197 */
}

/* The frame around the enlarged picture, and the picture's number over it.
 *
 * Two quads go down first: a flat black one the size of the whole window,
 * and a dark red one at 51/128 of `alpha` over the picture area itself --
 * which is what the picture is composited onto.  The four long frame pieces
 * are then *stretched* rather than tiled, each about its own top-left, and
 * the two 375.0f constants are one per expansion of the same source
 * statement rather than two different values. */
static void MenuPhotoLargePhotoWinDisp(int off_x, int off_y, u_char alpha) /* 1209 */
{
    DISP_SPRT win_ds;
    int       i;
    DISP_SQAR dsq;
    int       sel_num;

    SQAR_DAT win_bg   = { 476, 283, 137, 64, 160, 0, 0, 0, 0 };         /* 1215 */
    SQAR_DAT photo_bg = { 392, 256, 177, 78, 160, 0x13, 0x0c, 0x08, 0x33 }; /* 1218 */

    /* The picture's number over the frame, same shape as the thumbnails'. */
    static int number_tbl[16][2] =                          /* rdata 3be7c0 */
    {
        { 46,  -1 }, { 47,  -1 }, { 48,  -1 }, { 49,  -1 },
        { 50,  -1 }, { 51,  -1 }, { 52,  -1 }, { 53,  -1 },
        { 54,  -1 }, { 55, 56 }, { 57, 58 }, { 59, 60 },
        { 61, 62 }, { 63, 64 }, { 65, 66 }, { 67, 68 },
    };

    PK2SendVram((uintptr_t)menu_photo_tex_addr, -1, -1, 0);             /* 1247 */

    sel_num = csr_num[menu_photo_ctrl.csr_tate][menu_photo_ctrl.csr_yoko]; /* 1249 */

    CopySqrDToSqr(&dsq, &win_bg);                                       /* 1252 */
    dsq.alpha = 0;                                                      /* 1253 */
    DispSqrD(&dsq);                                                     /* 1254 */

    for (i = 0; i < 12; i++) {                                          /* 1257 */
        CopySprDToSpr(&win_ds, &menu_photo[7 + i]);                     /* 1258 */
        win_ds.x += (float)off_x;   win_ds.y += (float)off_y;           /* 1259 */
        win_ds.alpha = (u_char)(win_ds.alpha * alpha >> 7);             /* 1260 */
        DispSprD(&win_ds);                                              /* 1261 */
    }                                                                   /* 1262 */

    CopySqrDToSqr(&dsq, &photo_bg);                                     /* 1265 */
    dsq.alpha = (u_char)(alpha * 51 >> 7);                              /* 1267 */
    DispSqrD(&dsq);                                                     /* 1268 */

    for (i = 0; i < 3; i++) {                                           /* 1271 */
        CopySprDToSpr(&win_ds, &menu_photo[69 + i]);                    /* 1272 */
        win_ds.x += (float)off_x;   win_ds.y += (float)off_y;           /* 1273 */
        win_ds.alpha = (u_char)(win_ds.alpha * alpha >> 7);             /* 1274 */
        DispSprD(&win_ds);                                              /* 1275 */
    }                                                                   /* 1276 */

    CopySprDToSpr(&win_ds, &menu_photo[72]);                            /* 1277 */
    win_ds.x += (float)off_x;   win_ds.y += (float)off_y;               /* 1278 */
    win_ds.alpha = (u_char)(win_ds.alpha * alpha >> 7);                 /* 1279 */
    win_ds.scw = 1.0f;   win_ds.sch = MENU_PHOTO_FRAME_H / (float)win_ds.h;
    win_ds.csx = win_ds.x;   win_ds.csy = win_ds.y;                     /* 1280 */
    DispSprD(&win_ds);                                                  /* 1281 */

    CopySprDToSpr(&win_ds, &menu_photo[73]);                            /* 1282 */
    win_ds.x += (float)off_x;   win_ds.y += (float)off_y;               /* 1283 */
    win_ds.alpha = (u_char)(win_ds.alpha * alpha >> 7);                 /* 1284 */
    win_ds.scw = MENU_PHOTO_FRAME_W / (float)win_ds.w;   win_ds.sch = 1.0f;
    win_ds.csx = win_ds.x;   win_ds.csy = win_ds.y;                     /* 1285 */
    DispSprD(&win_ds);                                                  /* 1286 */

    for (i = 0; i < 3; i++) {                                           /* 1288 */
        CopySprDToSpr(&win_ds, &menu_photo[74 + i]);                    /* 1289 */
        win_ds.x += (float)off_x;   win_ds.y += (float)off_y;           /* 1290 */
        win_ds.alpha = (u_char)(win_ds.alpha * alpha >> 7);             /* 1291 */
        DispSprD(&win_ds);                                              /* 1292 */
    }                                                                   /* 1293 */

    CopySprDToSpr(&win_ds, &menu_photo[78]);                            /* 1294 */
    win_ds.x += (float)off_x;   win_ds.y += (float)off_y;               /* 1295 */
    win_ds.alpha = (u_char)(win_ds.alpha * alpha >> 7);                 /* 1296 */
    win_ds.scw = 1.0f;   win_ds.sch = MENU_PHOTO_FRAME_H / (float)win_ds.h;
    win_ds.csx = win_ds.x;   win_ds.csy = win_ds.y;                     /* 1297 */
    DispSprD(&win_ds);                                                  /* 1298 */

    CopySprDToSpr(&win_ds, &menu_photo[77]);                            /* 1299 */
    win_ds.x += (float)off_x;   win_ds.y += (float)off_y;               /* 1300 */
    win_ds.alpha = (u_char)(win_ds.alpha * alpha >> 7);                 /* 1301 */
    win_ds.scw = MENU_PHOTO_FRAME_W / (float)win_ds.w;   win_ds.sch = 1.0f;
    win_ds.csx = win_ds.x;   win_ds.csy = win_ds.y;                     /* 1302 */
    DispSprD(&win_ds);                                                  /* 1303 */

    for (i = 0; i < 2; i++) {                                           /* 1307 */
        CopySprDToSpr(&win_ds, &menu_photo[43 + i]);                    /* 1308 */
        win_ds.x += (float)off_x;   win_ds.y += (float)off_y;           /* 1309 */
        win_ds.alpha = (u_char)(win_ds.alpha * alpha >> 7);             /* 1310 */
        DispSprD(&win_ds);                                              /* 1311 */
    }                                                                   /* 1312 */

    CopySprDToSpr(&win_ds, &menu_photo[45]);                            /* 1316 */
    win_ds.x += (float)off_x;   win_ds.y += (float)off_y;               /* 1317 */
    win_ds.alpha = (u_char)(win_ds.alpha * alpha >> 7);                 /* 1318 */
    DispSprD(&win_ds);                                                  /* 1319 */

    CopySprDToSpr(&win_ds, &menu_photo[number_tbl[sel_num][0]]);        /* 1320 */
    win_ds.x += (float)off_x;   win_ds.y += (float)off_y;               /* 1321 */
    win_ds.alpha = (u_char)(win_ds.alpha * alpha >> 7);                 /* 1322 */
    DispSprD(&win_ds);                                                  /* 1323 */

    if (number_tbl[sel_num][1] != -1) {                                 /* 1324 */
        CopySprDToSpr(&win_ds, &menu_photo[number_tbl[sel_num][1]]);    /* 1325 */
        win_ds.x += (float)off_x;   win_ds.y += (float)off_y;           /* 1326 */
        win_ds.alpha = (u_char)(win_ds.alpha * alpha >> 7);             /* 1327 */
        DispSprD(&win_ds);                                              /* 1328 */
    }
}

/* The enlarged picture itself.  photo_flg is the cache: on the frame the
 * selection changed it is 0, so a decompress is filed and the flag raised;
 * from then on the work area is just drawn.  UncompressPhoto() only files
 * the request -- photo_make.o's handshake feeds a slice per frame -- so the
 * picture arrives over several frames and the draw runs regardless. */
static void MenuPhotoLargePhotoDisp(int off_x, int off_y, u_char alpha) /* 1340 */
{
    int sel_num;

    PK2SendVram((uintptr_t)menu_photo_tex_addr, -1, -1, 0);             /* 1345 */

    sel_num = csr_num[menu_photo_ctrl.csr_tate][menu_photo_ctrl.csr_yoko]; /* 1347 */

    if (menu_photo_ctrl.photo_flg == 0) {                               /* 1350 */
        if ((GetFilePhotoState((u_char)sel_num) & PHOTO_STATE_USE) != 0) { /* 1352 */
            UncompressPhoto(GetFilePhotoAdrNo((u_char)sel_num));        /* 1354 */
            menu_photo_ctrl.photo_flg = 1;                              /* 1355 */
        }
    }

    if (menu_photo_ctrl.photo_flg == 1) {                               /* 1359 */
        DrawPhotoFromWorkArea(MENU_PHOTO_L_PRI, 1,
                              MENU_PHOTO_L_X, MENU_PHOTO_L_Y,
                              MENU_PHOTO_L_W, MENU_PHOTO_L_H, alpha);   /* 1362 */
    }
}

/* The four corner marks that say the enlarged picture is protected.  The
 * whole `if` spans 1382..1385 in the ROM and there is no local for the
 * subscript -- see the note on MenuPhotoInfoDisp(). */
static void MenuPhotoLargeProtectDisp(int off_x, int off_y, u_char alpha) /* 1373 */
{
    DISP_SPRT protect_ds;
    int       i;

    PK2SendVram((uintptr_t)menu_photo_tex_addr, -1, -1, 0);             /* 1380 */

    if ((GetFilePhotoState(
             (u_char)csr_num[menu_photo_ctrl.csr_tate][menu_photo_ctrl.csr_yoko])
         & PHOTO_STATE_PROTECT) != 0) {                                 /* 1382-1385 */
        for (i = 0; i < 4; i++) {                                       /* 1386 */
            CopySprDToSpr(&protect_ds, &menu_photo[79 + i]);            /* 1387 */
            protect_ds.x += (float)off_x;   protect_ds.y += (float)off_y; /* 1388 */
            protect_ds.alpha = (u_char)(protect_ds.alpha * alpha >> 7); /* 1389 */
            DispSprD(&protect_ds);                                      /* 1390 */
        }                                                               /* 1391 */
    }
}

static void MenuPhotoCaptionDisp(int off_x, int off_y, u_char alpha)    /* 1404 */
{
    DrawCmnCapGroup_W(13, 13, alpha, 0);                                /* 1407 */
}

/* The per-photo pop-up.  Its open/close fade is its own -- three canned
 * tables, one for the selected row's scale, one for the others', one for the
 * alpha -- and the close arm is what finally hands the page back to the
 * grid, at the bottom of the ramp.
 *
 * Each row is one plate drawn twice: the left half about its own right edge
 * and the right half about its left, so the pair grows outward from the
 * middle as the scale comes up.
 *
 * The alpha here is a real division rather than the shift the rest of the
 * file uses -- `* now_alpha / 128` keeps the bias-and-movn sequence. */
static void MenuPhotoSubMenuDisp(int off_x, int off_y, u_char alpha)    /* 1447 */
{
    DISP_SPRT sub_ds;
    int       i;
    float     sel_scale;
    float     non_sel_scale;
    u_char    now_alpha;

    static int sel_tbl[3][2] =                              /* rdata 3be840 */
    {
        { 195, 196 },
        { 197, 198 },
        { 199, 200 },
    };
    static int not_sel_tbl[3][2] =                          /* rdata 3be858 */
    {
        { 201, 202 },
        { 203, 204 },
        { 205, 206 },
    };
    static SCL_ANIM_TBL sel_scl_tbl[2] =                    /* rdata 3be870 */
    {
        { 0.0f, 1.0f, 0, 10 },
        { -1.0f, -1.0f, -1, -1 },
    };
    static SCL_ANIM_TBL non_sel_scl_tbl[2] =                /* rdata 3be888 */
    {
        { 0.0f, 0.909999967f, 0, 10 },
        { -1.0f, -1.0f, -1, -1 },
    };
    static ALPHA_ANIM_TBL alpha_tbl[2] =                    /* rdata 3be8a0 */
    {
        { 0, 128, 0, 10 },
        { -1, -1, -1, -1 },
    };

    sel_scale     = 1.0f;                                               /* 1479 */
    non_sel_scale = 0.909999967f;                                       /* 1480 */
    now_alpha     = 128;                                                /* 1481 */

    PK2SendVram(MENU_BG_TEX_ADRS, -1, -1, 0);                           /* 1485 */

    if (menu_photo_ctrl.sub_anim_step == MENU_PHOTO_ANIM_START) {       /* 1487 */
        menu_photo_ctrl.sub_anim_timer = 0;                             /* 1488 */
        menu_photo_ctrl.sub_anim_step  = MENU_PHOTO_ANIM_IN;            /* 1489 */
    }

    if (menu_photo_ctrl.sub_anim_step == MENU_PHOTO_ANIM_IN) {          /* 1491 */
        sel_scale     = Anim2D_CalcNowScale(sel_scl_tbl,
                                            menu_photo_ctrl.sub_anim_timer); /* 1492 */
        non_sel_scale = Anim2D_CalcNowScale(non_sel_scl_tbl,
                                            menu_photo_ctrl.sub_anim_timer); /* 1493 */
        now_alpha     = Anim2D_CalcNowAlpha(alpha_tbl,
                                            menu_photo_ctrl.sub_anim_timer); /* 1494 */
        menu_photo_ctrl.sub_anim_timer++;                               /* 1495 */

        if (menu_photo_ctrl.sub_anim_timer > 9) {                       /* 1497 */
            menu_photo_ctrl.sub_anim_step = MENU_PHOTO_ANIM_MAIN;       /* 1498 */
        }
    }

    /* The close ramp runs the same alpha table backwards, and is the only
     * thing that puts the page back on the grid. */
    if (menu_photo_ctrl.sub_anim_step == MENU_PHOTO_ANIM_OUT) {         /* 1501 */
        menu_photo_ctrl.sub_anim_timer--;                               /* 1503 */

        now_alpha = Anim2D_CalcNowAlpha(alpha_tbl,
                                        menu_photo_ctrl.sub_anim_timer); /* 1505 */

        if (menu_photo_ctrl.sub_anim_timer < 1) {                       /* 1506 */
            menu_photo_ctrl.sub_anim_step = MENU_PHOTO_ANIM_END;        /* 1507 */
            menu_photo_ctrl.step          = MENU_PHOTO_MODE_SEL;        /* 1508 */
        }
    }

    for (i = 0; i < MENU_PHOTO_SUB_NUM; i++) {                          /* 1514 */

        if (menu_photo_ctrl.sub_csr == i) {                             /* 1516 */

            CopySprDToSpr(&sub_ds, &menu_photo[sel_tbl[i][0]]);         /* 1518 */
            sub_ds.x += (float)off_x;   sub_ds.y += (float)off_y;       /* 1519 */
            sub_ds.alpha = (u_char)(sub_ds.alpha * now_alpha / 128);    /* 1520 */

            if (menu_photo_ctrl.step == MENU_PHOTO_MODE_SUB) {          /* 1521 */
                sub_ds.r = menu_photo_ctrl.rgb;
                sub_ds.g = menu_photo_ctrl.rgb;
                sub_ds.b = menu_photo_ctrl.rgb;                     /* 1522 */
            }

            sub_ds.scw = sel_scale;   sub_ds.sch = 1.0f;
            sub_ds.csx = sub_ds.x + (float)sub_ds.w;   sub_ds.csy = sub_ds.y; /* 1524 */
            DispSprD(&sub_ds);                                          /* 1525 */

            CopySprDToSpr(&sub_ds, &menu_photo[sel_tbl[i][1]]);         /* 1527 */
            sub_ds.x += (float)off_x;   sub_ds.y += (float)off_y;       /* 1528 */
            sub_ds.alpha = (u_char)(sub_ds.alpha * now_alpha / 128);    /* 1529 */

            if (menu_photo_ctrl.step == MENU_PHOTO_MODE_SUB) {          /* 1530 */
                sub_ds.r = menu_photo_ctrl.rgb;
                sub_ds.g = menu_photo_ctrl.rgb;
                sub_ds.b = menu_photo_ctrl.rgb;                     /* 1531 */
            }

            sub_ds.scw = sel_scale;   sub_ds.sch = 1.0f;
            sub_ds.csx = sub_ds.x;   sub_ds.csy = sub_ds.y;             /* 1533 */
            DispSprD(&sub_ds);                                          /* 1534 */
        }
        else {

            CopySprDToSpr(&sub_ds, &menu_photo[not_sel_tbl[i][0]]);     /* 1538 */
            sub_ds.x += (float)off_x;   sub_ds.y += (float)off_y;       /* 1539 */
            sub_ds.alpha = (u_char)(sub_ds.alpha * now_alpha / 128);    /* 1540 */
            sub_ds.scw = non_sel_scale;   sub_ds.sch = 1.0f;
            sub_ds.csx = sub_ds.x + (float)sub_ds.w;   sub_ds.csy = sub_ds.y; /* 1541 */
            DispSprD(&sub_ds);                                          /* 1542 */

            CopySprDToSpr(&sub_ds, &menu_photo[not_sel_tbl[i][1]]);     /* 1544 */
            sub_ds.x += (float)off_x;   sub_ds.y += (float)off_y;       /* 1545 */
            sub_ds.alpha = (u_char)(sub_ds.alpha * now_alpha / 128);    /* 1546 */
            sub_ds.scw = non_sel_scale;   sub_ds.sch = 1.0f;
            sub_ds.csx = sub_ds.x;   sub_ds.csy = sub_ds.y;             /* 1547 */
            DispSprD(&sub_ds);                                          /* 1548 */
        }
    }                                                                   /* 1550 */

    PK2SendVram((uintptr_t)menu_photo_tex_addr, -1, -1, 0);             /* 1553 */

    for (i = 0; i < MENU_PHOTO_SUB_NUM; i++) {                          /* 1556 */
        CopySprDToSpr(&sub_ds, &menu_photo[188 + i]);                   /* 1557 */
        sub_ds.x += (float)off_x;   sub_ds.y += (float)off_y;           /* 1558 */
        sub_ds.alpha = (u_char)(sub_ds.alpha * now_alpha / 128);        /* 1559 */
        DispSprD(&sub_ds);                                              /* 1560 */
    }                                                                   /* 1561 */
}

static void MenuPhotoDelConfirmDisp(int off_x, int off_y, u_char alpha) /* 1572 */
{
    MenuCmnYesNoWinDisp(0, 0, alpha, 128);                              /* 1576 */

    PrintMsg(0x38, 0, 0x44, 0xc6, 1, alpha, 128);                       /* 1580 */
}

static void MenuPhotoNotDelWinDisp(int off_x, int off_y, u_char alpha)  /* 1592 */
{
    MenuCmnConfirmWinDisp(0, 0, alpha, 128);                            /* 1596 */

    PrintMsg(0x38, 1, 0x44, 0xc6, 1, alpha, 128);                       /* 1599 */
}

/* The sort menu.  No fade of its own -- it appears with the pop-up already
 * open behind it -- so the selected row is simply held bigger (0.8 against
 * 0.7), tinted with the cursor pulse and blended additively. */
static void MenuPhotoSortMenuDisp(int off_x, int off_y, u_char alpha)   /* 1611 */
{
    DISP_SPRT sub_ds;
    int       i;

    static int sel_tbl[3][2] =                              /* rdata 3be8b0 */
    {
        { 209, 210 },
        { 211, 212 },
        { 213, 214 },
    };
    static int not_sel_tbl[3][2] =                          /* rdata 3be8c8 */
    {
        { 215, 216 },
        { 217, 218 },
        { 219, 220 },
    };

    PK2SendVram(MENU_BG_TEX_ADRS, -1, -1, 0);                           /* 1633 */

    for (i = 0; i < 2; i++) {                                           /* 1636 */
        CopySprDToSpr(&sub_ds, &menu_photo[207 + i]);                   /* 1637 */
        sub_ds.x += (float)off_x;   sub_ds.y += (float)off_y;           /* 1638 */
        sub_ds.alpha = (u_char)(sub_ds.alpha * alpha >> 7);             /* 1639 */
        DispSprD(&sub_ds);                                              /* 1640 */
    }                                                                   /* 1641 */

    for (i = 0; i < MENU_PHOTO_SORT_NUM; i++) {                         /* 1643 */

        if (menu_photo_ctrl.sort_csr == i) {                            /* 1645 */

            CopySprDToSpr(&sub_ds, &menu_photo[sel_tbl[i][0]]);         /* 1647 */
            sub_ds.x += (float)off_x;   sub_ds.y += (float)off_y;       /* 1648 */
            sub_ds.alpha = (u_char)(sub_ds.alpha * alpha >> 7);         /* 1649 */
            sub_ds.scw = 0.799999952f;   sub_ds.sch = 1.0f;
            sub_ds.csx = sub_ds.x;   sub_ds.csy = sub_ds.y;             /* 1650 */
            sub_ds.alphar = 0x48;                                       /* 1651 */
            sub_ds.r = menu_photo_ctrl.rgb;
            sub_ds.g = menu_photo_ctrl.rgb;
            sub_ds.b = menu_photo_ctrl.rgb;                         /* 1652 */
            DispSprD(&sub_ds);                                          /* 1653 */

            CopySprDToSpr(&sub_ds, &menu_photo[sel_tbl[i][1]]);         /* 1655 */
            sub_ds.x += (float)off_x;   sub_ds.y += (float)off_y;       /* 1656 */
            sub_ds.alpha = (u_char)(sub_ds.alpha * alpha >> 7);         /* 1657 */
            sub_ds.scw = 0.799999952f;   sub_ds.sch = 1.0f;
            sub_ds.csx = sub_ds.x;   sub_ds.csy = sub_ds.y;             /* 1658 */
            sub_ds.alphar = 0x48;                                       /* 1659 */
            sub_ds.r = menu_photo_ctrl.rgb;
            sub_ds.g = menu_photo_ctrl.rgb;
            sub_ds.b = menu_photo_ctrl.rgb;                         /* 1660 */
            DispSprD(&sub_ds);                                          /* 1661 */
        }
        else {

            CopySprDToSpr(&sub_ds, &menu_photo[not_sel_tbl[i][0]]);     /* 1665 */
            sub_ds.x += (float)off_x;   sub_ds.y += (float)off_y;       /* 1666 */
            sub_ds.alpha = (u_char)(sub_ds.alpha * alpha >> 7);         /* 1667 */
            sub_ds.scw = 0.7f;   sub_ds.sch = 1.0f;
            sub_ds.csx = sub_ds.x;   sub_ds.csy = sub_ds.y;             /* 1668 */
            DispSprD(&sub_ds);                                          /* 1669 */

            CopySprDToSpr(&sub_ds, &menu_photo[not_sel_tbl[i][1]]);     /* 1671 */
            sub_ds.x += (float)off_x;   sub_ds.y += (float)off_y;       /* 1672 */
            sub_ds.alpha = (u_char)(sub_ds.alpha * alpha >> 7);         /* 1673 */
            sub_ds.scw = 0.7f;   sub_ds.sch = 1.0f;
            sub_ds.csx = sub_ds.x;   sub_ds.csy = sub_ds.y;             /* 1674 */
            DispSprD(&sub_ds);                                          /* 1675 */
        }
    }                                                                   /* 1677 */

    PK2SendVram((uintptr_t)menu_photo_tex_addr, -1, -1, 0);             /* 1680 */

    for (i = 0; i < 4; i++) {                                           /* 1683 */
        CopySprDToSpr(&sub_ds, &menu_photo[191 + i]);                   /* 1684 */
        sub_ds.x += (float)off_x;   sub_ds.y += (float)off_y;           /* 1685 */
        sub_ds.alpha = (u_char)(sub_ds.alpha * alpha >> 7);             /* 1686 */
        sub_ds.alphar = 0x48;                                           /* 1687 */
        DispSprD(&sub_ds);                                              /* 1688 */
    }                                                                   /* 1689 */
}

/* "You have not taken any photographs yet." */
static void MenuPhotoNotHaveDisp(int off_x, int off_y, u_char alpha)    /* 1700 */
{
    DISP_STR    msg_data;
    MSG_WIN_DAT msg_win;

    SetMsgDefData(&msg_data, 0x36);                                     /* 1706 */
    SetMsgWinDefData(&msg_win, 0x36);                                   /* 1707 */

    DrawCmnWindow(0, msg_win.x, msg_win.y, msg_win.w, msg_win.h,
                  alpha, 102);                                          /* 1710 */

    PrintMsg(0x36, 1, msg_data.pos_x, msg_data.pos_y, 1, alpha, 0);     /* 1714 */

    DrawCmnCapGroup_W(12, 12, alpha, 0);                                /* 1718 */
}
