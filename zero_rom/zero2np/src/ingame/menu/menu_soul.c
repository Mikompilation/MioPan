// FILE: /home/zero_rom/zero2np/src/ingame/menu/menu_soul.c
//
// The in-game menu's ghost list ("soul list") page (menu_soul.o).  Thirty-nine
// functions: eight exports and thirty-one statics.
//
// The page is the standard menu shape -- a *_CTRL work block with a
// MENU_REF_CTRL inside it, a menu_wrk.step ladder, an anim_step/anim_timer
// fade, and a Get/LoadReq/LoadWait/Liberate/LoadCancel texture quintet.  What
// is its own is the nine-row list, the cross-faded ghost photo beside it and
// the completion-rate readout across the top.
//
// Facts worth knowing before touching it:
//
//  * disp_soul_list_data[] is a *compacted* copy of the 176 ghost list
//    labels, not a view of them.  MenuSoulSetDispData() walks the labels once
//    and copies the ones the player holds into consecutive entries, so a row
//    is disp_soul_list_data[disp_start_pos + i] and the list walks without
//    re-testing anything.  It is built once, on entry -- nothing the page
//    does can change it.
//
//  * How far it walks depends on the player's progress.
//    CheckEnhancingSoulListCondition() is what decides whether the 24
//    "enhancing" labels (152..175) are listed at all, and both
//    MenuSoulSetDispData() and MenuSoulCtrlInit() ask it separately -- one
//    for the bound of the copy, the other for the row count.
//
//  * Only `ghost_list_label` is ever read back.  DISP_SOUL_LIST_DATA::state
//    is stored by MenuSoulSetDispData() and there is not one load of it
//    anywhere in the object -- every consumer calls GetPlyrSoulListState()
//    live instead.  It is dead in this build; it is kept because the ROM
//    writes it.  Same shape as menu_memo.o's two latched fields.
//
//  * `mode` is the page's inner state: 0 the list, 1 the "you completed the
//    list" message, 2 "you have no entries".  Unlike menu_item.o and
//    menu_memo.o there are no dispatch tables -- MenuSoul() and
//    MenuSoulDisp() each switch on it directly, and this object has no .data
//    section at all as a result.
//
//  * The ghost photo is not in a pak.  MenuCrossFadeInStart() loads file
//    (ghost_list_label + REI_PHT_000_TM2) into one of menu_cmn.o's two
//    cross-fade slots; MenuSoulPhotoDisp() uploads whichever slot is live to
//    the VRAM scratch page at 0x2bc0 with MenuTim2SendVram() and then
//    *patches* menu_glist_tex[23]'s TEX0 to match.  Both slots are drawn each
//    frame, the outgoing one first, which is the cross-fade -- and each slot
//    carries its own description message id, so the outgoing text fades with
//    its own picture.  menu_item.o's picture works the same way.
//
//  * Only a READ entry gets a picture.  MenuSoul() and MenuSoulPad() start a
//    cross-fade for a row whose state is SOUL_LIST_STATE_READ; CROSS on a
//    row that is merely HAVE calls ReadSoulList() first and starts the fade
//    on the way past.  So the first press on a new entry both reveals it and
//    retires its unread bracket.
//
//  * The top three scores get a badge, and the unread bracket is suppressed
//    on exactly those rows.  max_score_order[] is GetSoulListOrderScore()'s
//    three highest DISTINCT scores, latched once on entry;
//    MenuSoulOrderSignDisp() draws the badge for a row whose score matches
//    one of them and MenuSoulNoReadFrameDisp() runs the same three-way search
//    to decide *not* to draw the bracket.  The two loops are written out
//    separately rather than shared.
//
//  * A ghost owns three messages in bank 0x3b: name, place and description,
//    at ghost_list_label * 3 + 0/1/2.  The ROM writes that expression out at
//    all four use sites rather than factoring it.
//
//  * The name is drawn THREE times per row, at (+2,+2), (+1,+1) and (+0,+0),
//    the first two in colour 9 and the last in 19 (or 20 for an enhancing
//    label).  That is the drop shadow, and it is a single PrintMsg() inside a
//    j = 2..0 loop with the colour picked by a nested conditional.
//
//  * The scrollbar is float arithmetic here, unlike menu_item.o's integer
//    version of the same widget, and its short-thumb test is strictly
//    `center_size > 0.0f` where menu_item.o's is `0 <= center_size`.
//
// Reconstructed from the Feb 6 2004 prototype (SLES_523.84), menu_soul.o.
// All 8 ZERO2.MAP exports plus the 31 statics, verified 8/8 against the map
// and 39/40 against functions.txt (its 40th entry is the DISP_SOUL_LIST_DATA
// type_info function, a compiler artifact).  .text is accounted for byte-for-byte
// -- 0x206588..0x208bc0 = 0x2638 = 9784 bytes: forty-five bodies totalling
// 9688 (the thirty-nine real ones, the four fixed_array boilerplate ones and
// the empty static-init pair) plus twenty-four 4-byte alignment fills, with
// no gap of 8 bytes or more anywhere.
//
// The object has NO static data of its own.  Its .rodata (0x1a6) holds only
// the fixed_array assert literal, the two __FUNCTION__ strings, the report
// banners, the DISP_SOUL_LIST_DATA type name and the two SQAR_DAT *local*
// initialisers below; its .sdata (0x45) is the "void*" / "char*" type names
// plus three zeroed scalars, and its .lit4 is one word, the 550.0f the
// congratulation window is sized with.  Same signature as subtitle.o and
// title.o -- read the sections out of the ELF before assuming a table exists.
// Both SQAR_DAT initialisers reproduce the ROM's .rodata images exactly (they
// report a DIFF against the compiled .obj only because MinGW keeps a prefix
// in .rodata and materialises the rest as immediates), and MENU_SOUL_CTRL,
// MENU_SOUL_DISP and DISP_SOUL_LIST_DATA are confirmed member-by-member by an
// offsetof harness -- 0x20 + 0xc + 176 * 8 is the .bss's 0x5b0 exactly.
//
// The page's sprites live in tim_dat/menu_glist_dat.c, new with this pass:
// menu_glist_tex[36] is byte-identical to the ROM's .data, diffed out of the
// compiled .obj.
//
// Trailing /* NNN */ comments are the original source line numbers, measured
// from the $LM stabs.  Three things routinely leave a statement with no line
// of its own and those annotations are interpolated into the measured gap:
//
//  * any statement whose first act is a disp_soul_list_data[] or
//    max_score_order[] subscript -- the inlined operator[] reports
//    fixed_array.h 124/125 and the caller's own note is dropped.  Every one
//    of the fourteen subscripts in the file is one of these;
//
//  * a store the scheduler put in a jr/jal delay slot: MenuSoulCtrlInit()'s
//    cross_fade_flg, MenuSoul()'s `mode = LIST`, MenuSoulPhotoDisp()'s
//    fade_alpha[0], and the `load_flg = 1` of both page-key arms;
//
//  * the four cursor-move arms of MenuSoulPad().  All four end in the same
//    SystemBankPlay() + `load_flg = 1` pair and GCC cross-jumped three of
//    them onto the L1 arm's copy, so only that copy carries line numbers
//    (507/508/509) and the other three are placed by the arms' exact 9-line
//    stride.
//
// One statement measures across a gap rather than on one line, because g++
// tags a sub-expression with its own closing line: MenuSoulGhostNameDisp()'s
// PrintMsg() spans 1047..1068.  The intervening lines are not recoverable, so
// it carries a range rather than a number.

#include "menu_soul.h"

#include "menu.h"                               /* menu_wrk / MENU_BG_TEX_ADRS */
#include "menu_cmn.h"                           /* MenuRefMove* / cross-fade  */
#include "tim_dat/menu_glist_dat.h"             /* menu_glist_tex[]           */
#include "zero2_anim2d.h"                       /* Zero2Anim2D_CsrAnimCtrl    */

#include "../item/prg/soul_list.h"              /* GetPlyrSoulListState       */
#include "../../common/mem_util.h"              /* mem_utilGetMem             */
#include "../../common/utility2.h"              /* PRINT_ASSERT / PRINT_WARNING */
#include "../../graphics/graph2d/draw_cmn.h"    /* DrawCmnCapGroup_W          */
#include "../../graphics/graph2d/g2d_draw.h"    /* DISP_SPRT / DISP_SQAR      */
#include "../../graphics/graph2d/message.h"     /* PrintMsg / PrintNumber_N   */
#include "../../graphics/graph2d/tim2.h"        /* PK2SendVram                */
#include "../../system/eeiop/cddat.h"           /* GetFileSize / MENU_GLIST_PK2 */
#include "../../system/eeiop/fileload.h"        /* FileLoadReqEE              */
#include "../../system/eeiop/snd3d.h"           /* SND_3D_SET                 */
#include "../../system/os/system.h"             /* SystemBankPlay / GetLanguage */
#include "../../system/pad/pad.h"               /* pad / paddat               */

/* --------------------------------------------------------------------------
 *  Constants
 * ------------------------------------------------------------------------ */

/* menu_wrk.step values, the same ladder every page uses. */
#define MENU_SOUL_STEP_INIT     0
#define MENU_SOUL_STEP_LOAD     1
#define MENU_SOUL_STEP_MAIN     2
#define MENU_SOUL_STEP_OUT      3

/* MENU_SOUL_DISP::anim_step.  Only OUT and END are written by name here; the
 * rest of the ladder is MenuInOutAnimCtrl()'s business. */
#define MENU_SOUL_ANIM_START    0
#define MENU_SOUL_ANIM_OUT      3
#define MENU_SOUL_ANIM_END      4

/* MENU_SOUL_CTRL::mode -- what MenuSoul() and MenuSoulDisp() both switch on. */
#define MENU_SOUL_MODE_LIST     0   /* the list                              */
#define MENU_SOUL_MODE_COMP_MSG 1   /* "you completed the ghost list"        */
#define MENU_SOUL_MODE_NO_LIST  2   /* "you have no entries"                 */

/* How many rows the list shows, and how many entries it can hold. */
#define MENU_SOUL_DISP_NUM      9
#define DISP_SOUL_LIST_NUM      SOUL_LIST_MAX

/* How many best scores get a badge -- and the length of max_score_order[]. */
#define MENU_SOUL_ORDER_NUM     3

/* The completion rate that opens the congratulation message. */
#define MENU_SOUL_COMP_RATE     100

/* Message ids.  A ghost owns three, consecutive, in one bank. */
#define SOUL_MSG_BANK           0x3b
#define SOUL_MSG_PER_GHOST      3
#define SOUL_MSG_NAME           0
#define SOUL_MSG_PLACE          1
#define SOUL_MSG_EXP            2

/* Colour labels.  9 is the drop shadow the name is stencilled with; 19 and 20
 * are the two list inks, picked on whether the label is an enhancing one. */
#define SOUL_COL_SHADOW         9
#define SOUL_COL_BASE           19
#define SOUL_COL_ENHANCING      20
#define SOUL_COL_INFO           0x13

/* The shared banks: 8 is the common one the score's unit suffix comes from,
 * 0x28 the congratulation text. */
#define MSG_BANK_CMN            8
#define MSG_BANK_CONGRATS       0x28

/* The ghost photo.  Not in a pak -- one file per ghost, streamed into a VRAM
 * scratch page, with menu_glist_tex[MG_PHOTO]'s TEX0 patched to match. */
#define SOUL_PHOTO_TBP          0x2bc0
#define SOUL_PHOTO_CBP          12000
#define SOUL_PHOTO_TEX0         0x2005dc066932abc0ULL

/* menu_glist_tex[] indices that are referenced on their own rather than as a
 * range.  See tim_dat/menu_glist_dat.c for the whole layout. */
#define MG_BG                   0    /* [0..3] the background tiles          */
#define MG_BG_SIDE              4    /* the fifth, drawn rot 270             */
#define MG_TITLE_PLATE          5    /* [5],[6] out of the MENU_BG pak       */
#define MG_TITLE                7    /* the words, out of the ghost pak      */
#define MG_NUMBER               8    /* [8..17] the rate readout's digits    */
#define MG_CURSOR               18   /* [18],[19],[20] the row frame         */
#define MG_CURSOR_TRI_UP        21
#define MG_CURSOR_TRI_DOWN      22
#define MG_PHOTO                23
#define MG_SCROLL_ARROW_UP      24
#define MG_SCROLL_ARROW_DOWN    25
#define MG_THUMB_TOP            26
#define MG_THUMB_MIDDLE         27
#define MG_THUMB_BOTTOM         28
#define MG_NON_READ             29
#define MG_COMPLETE             30
#define MG_ORDER                31   /* [31],[32],[33] 1st / 2nd / 3rd       */
#define MG_CONGRATS             34   /* [34],[35]                            */

/* The list's geometry.  Every row-relative widget steps by the same pitch and
 * differs only in where it starts. */
#define MENU_SOUL_ROW_STEP      34
#define MENU_SOUL_CURSOR_BASE_X 40
#define MENU_SOUL_CURSOR_BASE_Y 116
#define MENU_SOUL_CURSOR_Y      112
#define MENU_SOUL_TRI_UP_Y      103
#define MENU_SOUL_TRI_DOWN_Y    148
#define MENU_SOUL_NON_READ_Y    117
#define MENU_SOUL_ORDER_Y       108
#define MENU_SOUL_NAME_X        76
#define MENU_SOUL_NAME_Y        119

/* The scrollbar rail, in pixels -- floats here, unlike menu_item.o's. */
#define SOUL_SCROLL_RAIL_SIZE   251.0f
#define SOUL_SCROLL_RAIL_TOP    142.0f

/* The congratulation window. */
#define SOUL_CONG_WIN_X         45.0f
#define SOUL_CONG_WIN_Y         104.0f
#define SOUL_CONG_WIN_W         550.0f
#define SOUL_CONG_WIN_H         232.0f

/* SystemBankPlay() cue numbers, as everywhere else in the menus. */
#define SE_CURSOR   0
#define SE_CANCEL   1
#define SE_DECIDE   3

/* pad[0] bits in the remapped layout, plus the analogue equivalents. */
#define PAD_RPT_UP              0x1000
#define PAD_RPT_DOWN            0x4000
#define PAD_ONE_L1              0x4
#define PAD_ONE_R1              0x8
#define PAD_ANALOG_UP           0
#define PAD_ANALOG_DOWN         1

/* --------------------------------------------------------------------------
 *  Work
 * ------------------------------------------------------------------------ */

static void *menu_soul_tex_addr;                            /* sdata 3f2f70 */

/* The "the list has been completed" badge.  One byte, saved with the game --
 * so the congratulation message is shown once per playthrough and not once
 * per visit. */
static char list_comp_disp_flg;                             /* sdata 3f2f74 */

static MENU_SOUL_CTRL menu_soul_ctrl;                       /* bss   4b5e28 */
static MENU_SOUL_DISP menu_soul_disp;                       /* bss   4b5e48 */

static fixed_array<DISP_SOUL_LIST_DATA, DISP_SOUL_LIST_NUM>
       disp_soul_list_data;                                 /* bss   4b5e58 */

static void MenuSoulInit(void);
static void MenuSoulCtrlInit(void);
static int  MenuSoulTexLoadWait(void);
static void MenuSoulSetDispData(void);
static void MenuSoulPad(void);
static void MenuSoulOutReq(void);
static void MenuSoulCompMsgPad(void);
static void MenuSoulNoListPad(void);
static void MenuSoulDispInit(void);

static void MenuSoulListSelDisp(int off_x, int off_y, u_char alpha);
static void MenuSoulCompMsgDisp(int off_x, int off_y, u_char alpha);
static void MenuSoulGhostInfoDisp(int off_x, int off_y, u_char alpha);
static void MenuSoulBgDisp(int off_x, int off_y, u_char alpha);
static void MenuSoulTitleDisp(int off_x, int off_y, u_char alpha);
static void MenuSoulPhotoDisp(int off_x, int off_y, u_char alpha);
static void MenuSoulGhostExplainDisp(int msg_id, int off_x, int off_y,
                                     u_char alpha);
static void MenuSoulNoReadFrameDisp(int off_x, int off_y, u_char alpha);
static void MenuSoulGhostNameDisp(int off_x, int off_y, u_char alpha);
static void MenuSoulGhostMaxScoreDisp(int off_x, int off_y, u_char alpha);
static void MenuSoulGhostPlaceDisp(int off_x, int off_y, u_char alpha);
static void MenuSoulAccomplishmentRateDisp(int off_x, int off_y, u_char alpha);
static void MenuSoulCompleteDisp(int off_x, int off_y, u_char alpha);
static void MenuSoulOrderSignDisp(int off_x, int off_y, u_char alpha);
static void MenuSoulCursorBaseDisp(int off_x, int off_y, u_char alpha);
static void MenuSoulCursorDisp(int off_x, int off_y, u_char alpha);
static void MenuSoulCursorTriangleDisp(int off_x, int off_y, u_char alpha);
static void MenuSoulScrollArrowDisp(int off_x, int off_y, u_char alpha);
static void MenuSoulScrollDisp(int off_x, int off_y, u_char alpha);
static void MenuSoulCaptionDisp(int off_x, int off_y, u_char alpha);
static void MenuSoulNumberDisp(int data, int num, int x, int y, u_char alpha,
                               int pri, u_char zero_flg);
static void MenuSoulNotHaveListDisp(int off_x, int off_y, u_char alpha);

/* --------------------------------------------------------------------------
 *  Entry / texture
 * ------------------------------------------------------------------------ */

/* Drop the "the list is complete" badge.  soul_list.o's PlyrSoulListInit() is
 * the only caller, so the congratulation message comes back on a new game.
 *
 * The ROM body is not empty even though it decompiles as {}: the store lives
 * in the delay slot of the `jr ra`. */
void MenuSoulListCompFlgInit(void)                                      /* 189 */
{
    list_comp_disp_flg = 0;                                             /* 192 */
}

/* The pak should already be resident -- menu_top.c requests it as the hub
 * fades out -- so a null here means the load never happened and the page
 * starts one of its own rather than drawing over garbage. */
static void MenuSoulInit(void)                                          /* 200 */
{
    menu_wrk.cursor = 0;                                                /* 202 */

    MenuSoulCtrlInit();                                                 /* 206 */

    MenuCrossFadeInit();                                                /* 209 */

    if (menu_soul_tex_addr == nullptr) {                                /* 212 */
        PRINT_WARNING("Menu Ghost Tex Back reading failure\n");         /* 213 */
        MenuSoulTexLoadReq();                                           /* 214 */
    }
}

/* The row count is asked for twice over, from two different accessors: the
 * enhancing labels are only listed once CheckEnhancingSoulListCondition()
 * says the player has earned them. */
static void MenuSoulCtrlInit(void)                                      /* 224 */
{
    menu_soul_ctrl.mode                 = MENU_SOUL_MODE_LIST;          /* 227 */
    menu_soul_ctrl.before_list_data_pos = 0;                            /* 228 */
    menu_soul_ctrl.cross_fade_flg       = 0;                            /* 229 */

    GetSoulListOrderScore(&menu_soul_ctrl.max_score_order[0],
                          MENU_SOUL_ORDER_NUM);                         /* 232 */

    if (CheckEnhancingSoulListCondition() != 0) {                       /* 235 */
        MenuRefCtrlInit(&menu_soul_ctrl.ref_ctrl,
                        GetPlyrHaveSoulListNum());                      /* 236 */
    }
    else {
        MenuRefCtrlInit(&menu_soul_ctrl.ref_ctrl,
                        GetPlyrHaveBaseSoulListNum());                  /* 239 */
    }
}

void GetMenuSoulTexMem(void)                                            /* 248 */
{
    if (menu_soul_tex_addr != nullptr) {                                /* 251 */
        LiberateMenuSoulTexMem();                                       /* 252 */
    }

    if (menu_soul_tex_addr == nullptr) {                                /* 256 */
        menu_soul_tex_addr =
            mem_utilGetMem((int)GetFileSize(MENU_GLIST_PK2 + GetLanguage())); /* 257 */
    }
}

void MenuSoulTexLoadReq(void)                                           /* 266 */
{
    if (menu_soul_tex_addr == nullptr) {                                /* 269 */
        GetMenuSoulTexMem();                                            /* 271 */
    }

    FileLoadReqEE(MENU_GLIST_PK2 + GetLanguage(), menu_soul_tex_addr,
                  2, nullptr, nullptr);                                 /* 276 */
}

static int MenuSoulTexLoadWait(void)                                    /* 286 */
{
    if (FileLoadIsEnd2(MENU_GLIST_PK2 + GetLanguage(),
                       menu_soul_tex_addr) != 0) {                      /* 294 */
        return 1;
    }

    return 0;                                                           /* 299 */
}

/* Build the compacted list.  Called once, on entry -- nothing the page does
 * can add or remove a row. */
static void MenuSoulSetDispData(void)                                   /* 305 */
{
    int i;
    int count;
    int state;
    int max_disp_num;

    count = 0;                                                          /* 312 */

    max_disp_num = (CheckEnhancingSoulListCondition() != 0)
                       ? SOUL_LIST_MAX : SOUL_LIST_BASE_MAX;            /* 316 */

    for (i = 0; i < max_disp_num; i++) {                                /* 324 */
        state = GetPlyrSoulListState(i);                                /* 326 */

        if (state != SOUL_LIST_STATE_NONE) {                            /* 329 */
            disp_soul_list_data[count].ghost_list_label = i;            /* 331 */
            disp_soul_list_data[count].state           = state;         /* 332 */

            count++;                                                    /* 333 */
        }
    }                                                                   /* 335 */
}

/* --------------------------------------------------------------------------
 *  One frame
 * ------------------------------------------------------------------------ */

void MenuSoul(void)                                                     /* 347 */
{
    int ghost_list_label;

    if (menu_wrk.step == MENU_SOUL_STEP_INIT) {                         /* 354 */
        MenuSoulInit();                                                 /* 356 */

        MenuSoulSetDispData();                                          /* 358 */

        menu_wrk.step = MENU_SOUL_STEP_LOAD;                            /* 360 */
    }

    if (menu_wrk.step == MENU_SOUL_STEP_LOAD                            /* 363 */
        && MenuSoulTexLoadWait() != 0) {                                /* 365 */
        MenuSoulDispInit();                                             /* 367 */

        if (menu_soul_ctrl.ref_ctrl.data_num != 0) {                    /* 370 */
            ghost_list_label =
                disp_soul_list_data[menu_soul_ctrl.ref_ctrl.data_pos].ghost_list_label; /* 372 */

            if (GetPlyrSoulListState(ghost_list_label)
                == SOUL_LIST_STATE_READ) {                              /* 374 */
                MenuCrossFadeInStart(menu_soul_ctrl.cross_fade_flg,
                                     ghost_list_label + REI_PHT_000_TM2); /* 376 */

                /* Both slots, so whichever one PhotoDisp happens to draw
                 * first has something to say. */
                menu_soul_disp.disp_msg_id[menu_soul_ctrl.cross_fade_flg] =
                    ghost_list_label * SOUL_MSG_PER_GHOST + SOUL_MSG_EXP;   /* 378 */
                menu_soul_disp.disp_msg_id[menu_soul_ctrl.cross_fade_flg ^ 1] =
                    ghost_list_label * SOUL_MSG_PER_GHOST + SOUL_MSG_EXP;   /* 379 */
            }

            if (list_comp_disp_flg == 0) {                              /* 383 */
                if (GetSoulListAccomplishmentRate() == MENU_SOUL_COMP_RATE) { /* 385 */
                    menu_soul_ctrl.mode = MENU_SOUL_MODE_COMP_MSG;      /* 386 */

                    list_comp_disp_flg = 1;                             /* 388 */
                }
            }
            else {
                menu_soul_ctrl.mode = MENU_SOUL_MODE_LIST;              /* 392 */
            }
        }
        else {
            menu_soul_ctrl.mode = MENU_SOUL_MODE_NO_LIST;               /* 396 */
        }

        menu_wrk.step = MENU_SOUL_STEP_MAIN;                            /* 399 */
    }

    if (menu_wrk.step == MENU_SOUL_STEP_MAIN) {                         /* 403 */
        switch (menu_soul_ctrl.mode) {                                  /* 404 */
        case MENU_SOUL_MODE_LIST:
            MenuSoulPad();                                              /* 406 */

            MenuCmnCrossFade();                                         /* 409 */
            break;                                                      /* 410 */

        case MENU_SOUL_MODE_COMP_MSG:
            MenuSoulCompMsgPad();                                       /* 412 */

            MenuCmnCrossFade();                                         /* 415 */
            break;                                                      /* 416 */

        case MENU_SOUL_MODE_NO_LIST:
            MenuSoulNoListPad();                                        /* 418 */
            break;                                                      /* 419 */

        default:
            PRINT_WARNING("Error!! MenuSoul");                          /* 421 */
            break;
        }
    }

    if (menu_wrk.step == MENU_SOUL_STEP_OUT                             /* 425 */
        && menu_soul_disp.anim_step == MENU_SOUL_ANIM_END) {            /* 426 */
        LiberateMenuSoulTexMem();                                       /* 428 */

        MenuCrossFadeTexLoadCancel(0);                                  /* 431 */
        MenuCrossFadeTexLoadCancel(1);                                  /* 432 */

        LiberateAllMenuCrossFadeTexMem();                               /* 435 */

        SetNextMenuStep(MENU_STEP_TOP);                                 /* 438 */
    }
}

/* The list's pad.  All four cursor movements share one tail -- play the
 * cursor cue and raise load_flg -- and GCC cross-jumped three of them onto
 * the L1 arm's copy of it. */
static void MenuSoulPad(void)                                           /* 449 */
{
    int  ghost_list_label;
    int  disp_num;
    char load_flg;

    disp_num = menu_soul_ctrl.ref_ctrl.data_num;                        /* 456 */

    if (disp_num > MENU_SOUL_DISP_NUM) {                                /* 459 */
        disp_num = MENU_SOUL_DISP_NUM;
    }

    load_flg = 0;                                                       /* 463 */

    if ((pad[0].rpt & PAD_RPT_UP) || GetPadAnalogRpt(PAD_ANALOG_UP)) {  /* 466 */
        menu_soul_ctrl.before_list_data_pos = menu_soul_ctrl.ref_ctrl.data_pos; /* 467 */

        if (MenuRefMovePadLup(&menu_soul_ctrl.ref_ctrl, &menu_wrk.cursor,
                              disp_num, MENU_SOUL_DISP_NUM) != 0) {     /* 469 */
            SystemBankPlay(SE_CURSOR, 1, 0, 0, nullptr, 0x3200, 0x1000); /* 470 */
            load_flg = 1;                                               /* 471 */
        }
    }
    else if ((pad[0].rpt & PAD_RPT_DOWN) || GetPadAnalogRpt(PAD_ANALOG_DOWN)) { /* 475 */
        menu_soul_ctrl.before_list_data_pos = menu_soul_ctrl.ref_ctrl.data_pos; /* 476 */

        if (MenuRefMovePadLdown(&menu_soul_ctrl.ref_ctrl, &menu_wrk.cursor,
                                disp_num, MENU_SOUL_DISP_NUM) != 0) {   /* 478 */
            SystemBankPlay(SE_CURSOR, 1, 0, 0, nullptr, 0x3200, 0x1000); /* 479 */
            load_flg = 1;                                               /* 480 */
        }
    }
    else if (*paddat[0] == 1) {                                         /* 484 */
        SystemBankPlay(SE_DECIDE, 1, 0, 0, nullptr, 0x3200, 0x1000);    /* 485 */

        ghost_list_label =
            disp_soul_list_data[menu_soul_ctrl.ref_ctrl.data_pos].ghost_list_label; /* 487 */

        /* Only an entry the player has not looked at yet: a READ one already
         * has its picture up, and CROSS on it does nothing. */
        if (GetPlyrSoulListState(ghost_list_label) == SOUL_LIST_STATE_HAVE) { /* 489 */
            ReadSoulList(ghost_list_label);                             /* 490 */

            MenuCrossFadeInStart(menu_soul_ctrl.cross_fade_flg,
                                 ghost_list_label + REI_PHT_000_TM2);   /* 493 */

            menu_soul_disp.disp_msg_id[menu_soul_ctrl.cross_fade_flg] =
                ghost_list_label * SOUL_MSG_PER_GHOST + SOUL_MSG_EXP;   /* 495 */
        }
    }
    else if (*paddat[1] == 1) {                                         /* 499 */
        SystemBankPlay(SE_CANCEL, 1, 0, 0, nullptr, 0x3200, 0x1000);    /* 500 */
        MenuSoulOutReq();                                               /* 501 */
    }
    else if (pad[0].one & PAD_ONE_L1) {                                 /* 504 */
        menu_soul_ctrl.before_list_data_pos = menu_soul_ctrl.ref_ctrl.data_pos; /* 505 */

        if (MenuRefMovePageUp(&menu_soul_ctrl.ref_ctrl, &menu_wrk.cursor,
                              disp_num, MENU_SOUL_DISP_NUM) != 0) {     /* 507 */
            SystemBankPlay(SE_CURSOR, 1, 0, 0, nullptr, 0x3200, 0x1000); /* 508 */
            load_flg = 1;                                               /* 509 */
        }
    }
    else if (pad[0].one & PAD_ONE_R1) {                                 /* 513 */
        menu_soul_ctrl.before_list_data_pos = menu_soul_ctrl.ref_ctrl.data_pos; /* 514 */

        if (MenuRefMovePageDown(&menu_soul_ctrl.ref_ctrl, &menu_wrk.cursor,
                                disp_num, MENU_SOUL_DISP_NUM) != 0) {   /* 516 */
            SystemBankPlay(SE_CURSOR, 1, 0, 0, nullptr, 0x3200, 0x1000); /* 517 */
            load_flg = 1;                                               /* 518 */
        }
    }

    /* The row actually changed: take the outgoing picture down and, if the
     * new row has one, bring it up in the other slot. */
    if (load_flg == 1                                                   /* 523 */
        && menu_soul_ctrl.before_list_data_pos
           != menu_soul_ctrl.ref_ctrl.data_pos) {                       /* 525 */
        if (GetPlyrSoulListState(
                disp_soul_list_data[menu_soul_ctrl.before_list_data_pos].ghost_list_label)
            == SOUL_LIST_STATE_READ) {                                  /* 528 */
            MenuCrossFadeOutStart(menu_soul_ctrl.cross_fade_flg);       /* 529 */
            menu_soul_ctrl.cross_fade_flg ^= 1;                         /* 530 */
        }

        ghost_list_label =
            disp_soul_list_data[menu_soul_ctrl.ref_ctrl.data_pos].ghost_list_label; /* 533 */

        if (GetPlyrSoulListState(ghost_list_label) == SOUL_LIST_STATE_READ) { /* 535 */
            /* The ROM re-derives the row from disp_start_pos + cursor here
             * rather than reusing ghost_list_label -- the same value, spelled
             * both ways in the same statement pair. */
            MenuCrossFadeInStart(
                menu_soul_ctrl.cross_fade_flg,
                disp_soul_list_data[menu_soul_ctrl.ref_ctrl.disp_start_pos
                                    + menu_wrk.cursor].ghost_list_label
                    + REI_PHT_000_TM2);                                 /* 537 */

            menu_soul_disp.disp_msg_id[menu_soul_ctrl.cross_fade_flg] =
                ghost_list_label * SOUL_MSG_PER_GHOST + SOUL_MSG_EXP;   /* 539 */
        }
    }
}

static void MenuSoulOutReq(void)                                        /* 548 */
{
    menu_wrk.step = MENU_SOUL_STEP_OUT;                                 /* 551 */

    menu_soul_disp.anim_step  = MENU_SOUL_ANIM_OUT;                     /* 552 */
    menu_soul_disp.anim_timer = 0;                                      /* 553 */
}

/* The congratulation message dismisses on either button and drops back to
 * the list; it never leaves the page. */
static void MenuSoulCompMsgPad(void)                                    /* 561 */
{
    if (*paddat[0] == 1) {                                              /* 565 */
        SystemBankPlay(SE_DECIDE, 1, 0, 0, nullptr, 0x3200, 0x1000);    /* 568 */
        menu_soul_ctrl.mode = MENU_SOUL_MODE_LIST;                      /* 570 */
    }
    else if (*paddat[1] == 1) {                                         /* 571 */
        SystemBankPlay(SE_CANCEL, 1, 0, 0, nullptr, 0x3200, 0x1000);    /* 572 */
        menu_soul_ctrl.mode = MENU_SOUL_MODE_LIST;                      /* 574 */
    }
}

/* An empty list has nowhere to go: only cancel does anything. */
static void MenuSoulNoListPad(void)                                     /* 583 */
{
    if (*paddat[1] == 1) {                                              /* 587 */
        SystemBankPlay(SE_CANCEL, 1, 0, 0, nullptr, 0x3200, 0x1000);    /* 588 */
        MenuSoulOutReq();                                               /* 589 */
    }
}

void LiberateMenuSoulTexMem(void)                                       /* 602 */
{
    if (menu_soul_tex_addr != nullptr) {                                /* 605 */
        mem_utilFreeMem(menu_soul_tex_addr);                            /* 606 */
        menu_soul_tex_addr = nullptr;                                   /* 607 */
    }
}

void MenuSoulTexLoadCancel(void)                                        /* 616 */
{
    if (MenuSoulTexLoadWait() == 0) {                                   /* 620 */
        FileLoadCancel2(MENU_GLIST_PK2 + GetLanguage(), menu_soul_tex_addr,
                        nullptr, nullptr);                              /* 621 */
    }
}

/* --------------------------------------------------------------------------
 *  Drawing
 * ------------------------------------------------------------------------ */

static void MenuSoulDispInit(void)                                      /* 634 */
{
    int i;

    menu_soul_disp.anim_step    = MENU_SOUL_ANIM_START;                 /* 639 */
    menu_soul_disp.anim_timer   = 0;                                    /* 640 */
    menu_soul_disp.rgb          = 0x40;                                 /* 641 */
    menu_soul_disp.scroll_timer = 0;                                    /* 642 */

    for (i = 0; i < MENU_CROSS_FADE_NUM; i++) {                         /* 644 */
        menu_soul_disp.disp_msg_id[i] = -1;                             /* 645 */
    }                                                                   /* 646 */
}

void MenuSoulDisp(void)                                                 /* 654 */
{
    u_char alpha;

    alpha = 0;

    if (menu_wrk.step == MENU_SOUL_STEP_MAIN
        || menu_wrk.step == MENU_SOUL_STEP_OUT) {                       /* 661 */
        Zero2Anim2D_CsrAnimCtrl(&menu_soul_disp.scroll_timer,
                                &menu_soul_disp.rgb);                   /* 663 */

        if (menu_soul_disp.anim_step != MENU_SOUL_ANIM_END) {           /* 665 */
            MenuInOutAnimCtrl(&menu_soul_disp.anim_step,
                              &menu_soul_disp.anim_timer, &alpha);      /* 667 */

            MenuSoulTitleDisp(0, 0, alpha);                             /* 670 */

            switch (menu_soul_ctrl.mode) {                              /* 672 */
            case MENU_SOUL_MODE_LIST:
                MenuSoulListSelDisp(0, 0, alpha);                       /* 674 */
                break;                                                  /* 675 */

            case MENU_SOUL_MODE_COMP_MSG:
                MenuSoulCompMsgDisp(0, 0, alpha);                       /* 677 */
                break;                                                  /* 678 */

            case MENU_SOUL_MODE_NO_LIST:
                MenuSoulNotHaveListDisp(0, 0, alpha);                   /* 680 */
                break;                                                  /* 681 */

            default:
                PRINT_ASSERT("Error!! %s", __FUNCTION__);               /* 683 */
                break;
            }
        }
    }
}

/* The list page, back to front.  Every one of the fourteen parts is called
 * with a literal 0, 0 -- this function's own offsets are dead. */
static void MenuSoulListSelDisp(int off_x, int off_y, u_char alpha)      /* 698 */
{
    MenuSoulScrollArrowDisp(0, 0, alpha);                               /* 702 */
    MenuSoulScrollDisp(0, 0, alpha);                                    /* 705 */
    MenuSoulBgDisp(0, 0, alpha);                                        /* 708 */
    MenuSoulAccomplishmentRateDisp(0, 0, alpha);                        /* 711 */
    MenuSoulCursorBaseDisp(0, 0, alpha);                                /* 714 */
    MenuSoulNoReadFrameDisp(0, 0, alpha);                               /* 717 */
    MenuSoulGhostNameDisp(0, 0, alpha);                                 /* 720 */
    MenuSoulCursorDisp(0, 0, alpha);                                    /* 723 */
    MenuSoulCursorTriangleDisp(0, 0, alpha);                            /* 726 */
    MenuSoulOrderSignDisp(0, 0, alpha);                                 /* 729 */
    MenuSoulGhostMaxScoreDisp(0, 0, alpha);                             /* 732 */
    MenuSoulGhostPlaceDisp(0, 0, alpha);                                /* 735 */
    MenuSoulGhostInfoDisp(0, 0, alpha);                                 /* 738 */
    MenuSoulCaptionDisp(0, 0, alpha);                                   /* 741 */
}

/* The congratulation message, drawn on top of the whole list: a half-black
 * wash, the shared two-line window, the two plates and three lines of text. */
static void MenuSoulCompMsgDisp(int off_x, int off_y, u_char alpha)      /* 752 */
{
    int       i;
    DISP_SPRT cong_ds;
    DISP_SQAR dsq;

    SQAR_DAT black_bg = { 640, 448, 0, 0, 0, 0, 0, 0, 0x59 };           /* 757 */

    MenuSoulListSelDisp(off_x, off_y, alpha);                           /* 762 */

    CopySqrDToSqr(&dsq, &black_bg);                                     /* 766 */
    dsq.alpha = (u_char)(dsq.alpha * alpha >> 7);                       /* 767 */
    DispSqrD(&dsq);                                                     /* 768 */

    DrawCmnTwoLineWindow(0, SOUL_CONG_WIN_X, SOUL_CONG_WIN_Y,
                         SOUL_CONG_WIN_W, SOUL_CONG_WIN_H, alpha, 0x80); /* 772 */

    for (i = 0; i < 2; i++) {                                           /* 775 */
        CopySprDToSpr(&cong_ds, &menu_glist_tex[MG_CONGRATS + i]);      /* 776 */
        cong_ds.x += (float)off_x;   cong_ds.y += (float)off_y;         /* 777 */
        cong_ds.alpha = (u_char)(cong_ds.alpha * alpha >> 7);           /* 778 */
        DispSprD(&cong_ds);                                             /* 779 */
    }                                                                   /* 780 */

    PrintMsg_Arrange(MSG_BANK_CONGRATS, 5, 320, 195, 1, alpha, 0, 0, 0, 2); /* 784 */
    PrintMsg_Arrange(MSG_BANK_CONGRATS, 0, 320, 249, 1, alpha, 0, 0, 0, 2); /* 788 */
    PrintMsg_Arrange(MSG_BANK_CONGRATS, 1, 320, 273, 1, alpha, 0, 0, 0, 2); /* 791 */
}

static void MenuSoulGhostInfoDisp(int off_x, int off_y, u_char alpha)    /* 802 */
{
    MenuSoulPhotoDisp(0, 0, alpha);                                     /* 806 */
}

/* The page background: four tiles upright, then a fifth on its side.  With
 * rot 270 about (x, y + w) the sprite's own width is what places it. */
static void MenuSoulBgDisp(int off_x, int off_y, u_char alpha)           /* 817 */
{
    int       i;
    DISP_SPRT bg_ds;

    PK2SendVram((uintptr_t)menu_soul_tex_addr, -1, -1, 0);              /* 822 */

    for (i = 0; i < 4; i++) {                                           /* 825 */
        CopySprDToSpr(&bg_ds, &menu_glist_tex[MG_BG + i]);              /* 826 */
        bg_ds.x += (float)off_x;   bg_ds.y += (float)off_y;             /* 827 */
        bg_ds.alpha = (u_char)(bg_ds.alpha * alpha >> 7);               /* 828 */
        DispSprD(&bg_ds);                                               /* 829 */
    }                                                                   /* 830 */

    CopySprDToSpr(&bg_ds, &menu_glist_tex[MG_BG_SIDE]);                 /* 831 */

    bg_ds.x += (float)off_x;
    bg_ds.y  = bg_ds.y + (float)bg_ds.w + (float)off_y;                 /* 832 */

    bg_ds.crx = bg_ds.x;                                                /* 833 */
    bg_ds.cry = bg_ds.y;
    bg_ds.rot = 270.0f;

    bg_ds.alpha = (u_char)(bg_ds.alpha * alpha >> 7);                   /* 834 */

    DispSprD(&bg_ds);                                                   /* 835 */
}

/* The page title: the shared plate out of the MENU_BG pak, one half mirrored,
 * and then the page's own word out of the ghost-list pak.  Two paks, so two
 * PK2SendVram() calls. */
static void MenuSoulTitleDisp(int off_x, int off_y, u_char alpha)        /* 846 */
{
    int       i;
    DISP_SPRT title_ds;

    PK2SendVram(MENU_BG_TEX_ADRS, -1, -1, 0);                           /* 851 */

    for (i = 0; i < 2; i++) {                                           /* 854 */
        CopySprDToSpr(&title_ds, &menu_glist_tex[MG_TITLE_PLATE + i]);  /* 855 */
        title_ds.x += (float)off_x;   title_ds.y += (float)off_y;       /* 856 */
        title_ds.alpha = (u_char)(title_ds.alpha * alpha >> 7);         /* 857 */
        DispSprD(&title_ds);                                            /* 858 */
    }                                                                   /* 859 */

    PK2SendVram((uintptr_t)menu_soul_tex_addr, -1, -1, 0);              /* 861 */

    CopySprDToSpr(&title_ds, &menu_glist_tex[MG_TITLE]);                /* 864 */
    title_ds.x += (float)off_x;   title_ds.y += (float)off_y;           /* 865 */
    title_ds.alpha = (u_char)(title_ds.alpha * alpha >> 7);             /* 866 */
    DispSprD(&title_ds);                                                /* 867 */
}

/* The ghost photo and its description, one per cross-fade slot: the outgoing
 * one first, then the live one.  Both are drawn through the same VRAM scratch
 * page, so the upload has to happen twice a frame. */
static void MenuSoulPhotoDisp(int off_x, int off_y, u_char alpha)        /* 878 */
{
    DISP_SPRT ghost_ds;
    int       disp_num;
    u_char    fade_alpha[2];

    disp_num = menu_soul_ctrl.ref_ctrl.data_num;                        /* 885 */

    if (disp_num > MENU_SOUL_DISP_NUM) {                                /* 887 */
        disp_num = MENU_SOUL_DISP_NUM;
    }

    fade_alpha[0] = 0;                                                  /* 891 */
    fade_alpha[1] = 0;                                                  /* 892 */

    if (disp_num != 0) {                                                /* 896 */
        GetMenuCrossFadeAlpha(fade_alpha);                              /* 898 */

        if (menu_wrk.step != MENU_SOUL_STEP_MAIN) {                     /* 900 */
            fade_alpha[menu_soul_ctrl.cross_fade_flg ^ 1] = 0;          /* 901 */
        }

        if (CheckCrossFadeDisp(menu_soul_ctrl.cross_fade_flg ^ 1) != 0  /* 905 */
            && fade_alpha[menu_soul_ctrl.cross_fade_flg ^ 1] != 0) {    /* 906 */
            MenuTim2SendVram(
                (u_int *)GetCrossFadeDataAddr(menu_soul_ctrl.cross_fade_flg ^ 1),
                SOUL_PHOTO_TBP, SOUL_PHOTO_CBP);                        /* 907 */

            CopySprDToSpr(&ghost_ds, &menu_glist_tex[MG_PHOTO]);        /* 909 */
            ghost_ds.tex0 = SOUL_PHOTO_TEX0;                            /* 910 */
            ghost_ds.alpha = (u_char)(
                ghost_ds.alpha
                * fade_alpha[menu_soul_ctrl.cross_fade_flg ^ 1] >> 7);  /* 912 */
            DispSprD(&ghost_ds);                                        /* 913 */

            if (menu_soul_disp.disp_msg_id[menu_soul_ctrl.cross_fade_flg ^ 1]
                != -1) {                                                /* 915 */
                MenuSoulGhostExplainDisp(
                    menu_soul_disp.disp_msg_id[menu_soul_ctrl.cross_fade_flg ^ 1],
                    off_x, off_y,
                    fade_alpha[menu_soul_ctrl.cross_fade_flg ^ 1]);     /* 917 */
            }
        }

        if (menu_wrk.step != MENU_SOUL_STEP_MAIN) {                     /* 922 */
            fade_alpha[menu_soul_ctrl.cross_fade_flg] = alpha;          /* 923 */
        }

        if (CheckCrossFadeDisp(menu_soul_ctrl.cross_fade_flg) != 0      /* 927 */
            && fade_alpha[menu_soul_ctrl.cross_fade_flg] != 0) {        /* 928 */
            MenuTim2SendVram(
                (u_int *)GetCrossFadeDataAddr(menu_soul_ctrl.cross_fade_flg),
                SOUL_PHOTO_TBP, SOUL_PHOTO_CBP);                        /* 929 */

            CopySprDToSpr(&ghost_ds, &menu_glist_tex[MG_PHOTO]);        /* 931 */
            ghost_ds.tex0 = SOUL_PHOTO_TEX0;                            /* 932 */
            ghost_ds.alpha = (u_char)(
                ghost_ds.alpha
                * fade_alpha[menu_soul_ctrl.cross_fade_flg] >> 7);      /* 934 */
            DispSprD(&ghost_ds);                                        /* 935 */

            if (menu_soul_disp.disp_msg_id[menu_soul_ctrl.cross_fade_flg]
                != -1) {                                                /* 937 */
                MenuSoulGhostExplainDisp(
                    menu_soul_disp.disp_msg_id[menu_soul_ctrl.cross_fade_flg],
                    off_x, off_y,
                    fade_alpha[menu_soul_ctrl.cross_fade_flg]);         /* 939 */
            }
        }
    }
}

static void MenuSoulGhostExplainDisp(int msg_id, int off_x, int off_y,
                                     u_char alpha)                      /* 955 */
{
    PrintMsg(SOUL_MSG_BANK, msg_id, off_x + 334, off_y + 277,
             SOUL_COL_SHADOW, alpha, 0);                                /* 961 */
}

/* The unread bracket, on every visible row the player holds but has not read
 * -- except a row carrying one of the three best scores, whose badge takes
 * that place instead.  The three-way search is the same one
 * MenuSoulOrderSignDisp() runs, written out again rather than shared. */
static void MenuSoulNoReadFrameDisp(int off_x, int off_y, u_char alpha)  /* 972 */
{
    int       i;
    int       j;
    int       disp_num;
    int       ghost_list_label;
    DISP_SPRT frame_ds;
    char      disp_flg;

    disp_num = menu_soul_ctrl.ref_ctrl.data_num;                        /* 980 */

    if (disp_num > MENU_SOUL_DISP_NUM) {                                /* 982 */
        disp_num = MENU_SOUL_DISP_NUM;
    }

    PK2SendVram((uintptr_t)menu_soul_tex_addr, -1, -1, 0);              /* 989 */

    for (i = 0; i < disp_num; i++) {                                    /* 992 */
        disp_flg = 1;                                                   /* 994 */

        ghost_list_label =
            disp_soul_list_data[menu_soul_ctrl.ref_ctrl.disp_start_pos + i].ghost_list_label; /* 995 */

        for (j = 0; j < MENU_SOUL_ORDER_NUM; j++) {                     /* 996 */
            if (GetPlyrSoulListMaxScore(ghost_list_label)
                == menu_soul_ctrl.max_score_order[j]) {                 /* 998 */
                disp_flg = 0;                                           /* 999 */
                break;                                                  /* 1000 */
            }
        }                                                               /* 1002 */

        if (disp_flg != 0                                               /* 1005 */
            && GetPlyrSoulListState(ghost_list_label) == SOUL_LIST_STATE_HAVE) { /* 1007 */
            CopySprDToSpr(&frame_ds, &menu_glist_tex[MG_NON_READ]);     /* 1008 */
            frame_ds.x += (float)off_x;
            frame_ds.y  = (float)(off_y + MENU_SOUL_NON_READ_Y
                                  + i * MENU_SOUL_ROW_STEP);            /* 1009 */
            frame_ds.alpha = (u_char)(frame_ds.alpha * alpha >> 7);     /* 1010 */
            DispSprD(&frame_ds);                                        /* 1011 */
        }
    }                                                                   /* 1014 */
}

/* Every visible row's name, stencilled: two shadow passes a pixel apart in
 * colour 9, then the ink.  An enhancing label gets the other ink.
 *
 * The whole call spans 1047..1068 -- the colour conditional carries the first
 * of those two notes and the call's own arguments the last, with the twenty
 * lines between not recoverable. */
static void MenuSoulGhostNameDisp(int off_x, int off_y, u_char alpha)    /* 1025 */
{
    int i;
    int j;
    int disp_num;
    int ghost_list_label;

    disp_num = menu_soul_ctrl.ref_ctrl.data_num;                        /* 1032 */

    if (disp_num > MENU_SOUL_DISP_NUM) {                                /* 1034 */
        disp_num = MENU_SOUL_DISP_NUM;
    }

    for (i = 0; i < disp_num; i++) {                                    /* 1042 */
        ghost_list_label =
            disp_soul_list_data[menu_soul_ctrl.ref_ctrl.disp_start_pos + i].ghost_list_label; /* 1044 */

        for (j = 2; j >= 0; j--) {                                      /* 1045 */
            PrintMsg(SOUL_MSG_BANK,
                     ghost_list_label * SOUL_MSG_PER_GHOST + SOUL_MSG_NAME,
                     off_x + MENU_SOUL_NAME_X + j,
                     off_y + i * MENU_SOUL_ROW_STEP + MENU_SOUL_NAME_Y + j,
                     (j != 0)
                         ? SOUL_COL_SHADOW
                         : ((ghost_list_label < SOUL_LIST_BASE_MAX)
                                ? SOUL_COL_BASE : SOUL_COL_ENHANCING),
                     alpha, 0);                                         /* 1047-1068 */
        }                                                               /* 1069 */
    }                                                                   /* 1070 */
}

/* The selected ghost's best score, and the unit plate after it.  The ROM
 * computes off_y + 73 once and both statements read it back. */
static void MenuSoulGhostMaxScoreDisp(int off_x, int off_y, u_char alpha) /* 1081 */
{
    PrintNumber_N(GetPlyrSoulListMaxScore(
                      disp_soul_list_data[menu_soul_ctrl.ref_ctrl.data_pos].ghost_list_label),
                  5, off_x + 467, off_y + 73, SOUL_COL_INFO, alpha, 0, 0, 0); /* 1091 */

    PrintMsg(MSG_BANK_CMN, 2, off_x + 559, off_y + 73,
             SOUL_COL_INFO, alpha, 0);                                  /* 1095 */
}

static void MenuSoulGhostPlaceDisp(int off_x, int off_y, u_char alpha)   /* 1106 */
{
    int ghost_list_label;

    ghost_list_label =
        disp_soul_list_data[menu_soul_ctrl.ref_ctrl.data_pos].ghost_list_label; /* 1112 */

    PrintMsg(SOUL_MSG_BANK,
             ghost_list_label * SOUL_MSG_PER_GHOST + SOUL_MSG_PLACE,
             off_x + 363, off_y + 97, SOUL_COL_INFO, alpha, 0);         /* 1116 */
}

/* The percentage across the top, and the COMPLETE plate once it is full.
 * Note the readout ignores this function's offsets -- its position is a pair
 * of literals -- while the plate under it honours them. */
static void MenuSoulAccomplishmentRateDisp(int off_x, int off_y, u_char alpha) /* 1126 */
{
    int rate;

    rate = GetSoulListAccomplishmentRate();                             /* 1131 */

    MenuSoulNumberDisp(rate, 3, 119, 72, alpha, 0, 0);                  /* 1135 */

    if (rate >= MENU_SOUL_COMP_RATE) {                                  /* 1138 */
        MenuSoulCompleteDisp(off_x, off_y, alpha);                      /* 1140 */
    }
}

static void MenuSoulCompleteDisp(int off_x, int off_y, u_char alpha)     /* 1152 */
{
    DISP_SPRT complete_ds;

    PK2SendVram((uintptr_t)menu_soul_tex_addr, -1, -1, 0);              /* 1156 */

    CopySprDToSpr(&complete_ds, &menu_glist_tex[MG_COMPLETE]);          /* 1159 */
    complete_ds.x += (float)off_x;   complete_ds.y += (float)off_y;     /* 1160 */
    complete_ds.alpha = (u_char)(complete_ds.alpha * alpha >> 7);       /* 1161 */
    DispSprD(&complete_ds);                                             /* 1162 */
}

/* The 1st / 2nd / 3rd badges.  A row whose best score matches one of the
 * three latched values gets that badge, additively blended; the search stops
 * at the first match, so a tie takes the higher place. */
static void MenuSoulOrderSignDisp(int off_x, int off_y, u_char alpha)    /* 1173 */
{
    int       i;
    int       j;
    int       disp_num;
    int       ghost_list_label;
    DISP_SPRT sign_ds;

    disp_num = menu_soul_ctrl.ref_ctrl.data_num;                        /* 1180 */

    if (disp_num > MENU_SOUL_DISP_NUM) {                                /* 1182 */
        disp_num = MENU_SOUL_DISP_NUM;
    }

    PK2SendVram((uintptr_t)menu_soul_tex_addr, -1, -1, 0);              /* 1188 */

    for (i = 0; i < disp_num; i++) {                                    /* 1190 */
        ghost_list_label =
            disp_soul_list_data[menu_soul_ctrl.ref_ctrl.disp_start_pos + i].ghost_list_label; /* 1192 */

        for (j = 0; j < MENU_SOUL_ORDER_NUM; j++) {                     /* 1193 */
            if (GetPlyrSoulListMaxScore(ghost_list_label)
                == menu_soul_ctrl.max_score_order[j]) {                 /* 1195 */
                switch (j) {                                            /* 1197 */
                case 0:
                    CopySprDToSpr(&sign_ds, &menu_glist_tex[MG_ORDER]); /* 1199 */
                    break;                                              /* 1200 */

                case 1:
                    CopySprDToSpr(&sign_ds, &menu_glist_tex[MG_ORDER + 1]); /* 1202 */
                    break;                                              /* 1203 */

                case 2:
                    CopySprDToSpr(&sign_ds, &menu_glist_tex[MG_ORDER + 2]); /* 1205 */
                    break;                                              /* 1206 */

                /* No break -- the ROM's default arm falls into the draw
                 * below with sign_ds still uninitialised.  Unreachable: the
                 * loop bound is the same 3. */
                default:
                    PRINT_ASSERT("Error! MenuSoulOrderSignDisp");       /* 1208 */
                }                                                       /* 1209 */

                sign_ds.x += (float)off_x;
                sign_ds.y  = (float)(off_y + MENU_SOUL_ORDER_Y
                                     + i * MENU_SOUL_ROW_STEP);         /* 1210 */
                sign_ds.alpha  = (u_char)(sign_ds.alpha * alpha >> 7);  /* 1211 */
                sign_ds.alphar = 0x48;                                  /* 1212 */
                DispSprD(&sign_ds);                                     /* 1213 */

                break;                                                  /* 1215 */
            }
        }                                                               /* 1217 */
    }                                                                   /* 1218 */
}

/* The selected row's bed: one 272x30 quad slid to (40, cursor row).  The ROM
 * writes all eight corner updates on ONE source line, the same shape
 * menu_cam_edit.o's row beds use. */
static void MenuSoulCursorBaseDisp(int off_x, int off_y, u_char alpha)   /* 1229 */
{
    DISP_SQAR dsq;

    SQAR_DAT cursor_base = { 272, 30, MENU_SOUL_CURSOR_BASE_X,
                             MENU_SOUL_CURSOR_BASE_Y, 0,
                             0xff, 0xd8, 0x8c, 0x19 };                  /* 1232 */

    CopySqrDToSqr(&dsq, &cursor_base);                                  /* 1239 */

    dsq.x[1] = MENU_SOUL_CURSOR_BASE_X + (dsq.x[1] - dsq.x[0]);
    dsq.x[3] = MENU_SOUL_CURSOR_BASE_X + (dsq.x[3] - dsq.x[2]);
    dsq.y[2] = menu_wrk.cursor * MENU_SOUL_ROW_STEP + MENU_SOUL_CURSOR_BASE_Y
               + (dsq.y[2] - dsq.y[0]);
    dsq.y[3] = menu_wrk.cursor * MENU_SOUL_ROW_STEP + MENU_SOUL_CURSOR_BASE_Y
               + (dsq.y[3] - dsq.y[1]);
    dsq.x[0] = MENU_SOUL_CURSOR_BASE_X;
    dsq.x[2] = MENU_SOUL_CURSOR_BASE_X;
    dsq.y[0] = menu_wrk.cursor * MENU_SOUL_ROW_STEP + MENU_SOUL_CURSOR_BASE_Y;
    dsq.y[1] = menu_wrk.cursor * MENU_SOUL_ROW_STEP + MENU_SOUL_CURSOR_BASE_Y; /* 1240 */

    dsq.alpha = (u_char)(dsq.alpha * alpha >> 7);                       /* 1241 */

    DispSqrD(&dsq);                                                     /* 1242 */
}

/* The selected row's frame: left cap, body and the mirrored cap.  Only the
 * body is scaled -- 253 pixels wide, whatever its authored w says. */
static void MenuSoulCursorDisp(int off_x, int off_y, u_char alpha)       /* 1253 */
{
    int       y;
    DISP_SPRT cursor_ds;

    y = menu_wrk.cursor * MENU_SOUL_ROW_STEP + MENU_SOUL_CURSOR_Y;      /* 1258 */

    PK2SendVram((uintptr_t)menu_soul_tex_addr, -1, -1, 0);              /* 1260 */

    CopySprDToSpr(&cursor_ds, &menu_glist_tex[MG_CURSOR]);              /* 1263 */
    cursor_ds.x += (float)off_x;
    cursor_ds.y  = (float)(y + off_y);                                  /* 1264 */
    cursor_ds.alpha  = (u_char)(cursor_ds.alpha * alpha >> 7);          /* 1265 */
    cursor_ds.alphar = 0x48;                                            /* 1266 */
    DispSprD(&cursor_ds);                                               /* 1267 */

    CopySprDToSpr(&cursor_ds, &menu_glist_tex[MG_CURSOR + 1]);          /* 1269 */
    cursor_ds.x += (float)off_x;
    cursor_ds.y  = (float)(y + off_y);                                  /* 1270 */
    cursor_ds.csx = cursor_ds.x;   cursor_ds.csy = cursor_ds.y;
    cursor_ds.scw = 253.0f / (float)cursor_ds.w;
    cursor_ds.sch = 1.0f;                                               /* 1271 */
    cursor_ds.alpha  = (u_char)(cursor_ds.alpha * alpha >> 7);          /* 1272 */
    cursor_ds.alphar = 0x48;                                            /* 1273 */
    DispSprD(&cursor_ds);                                               /* 1274 */

    CopySprDToSpr(&cursor_ds, &menu_glist_tex[MG_CURSOR + 2]);          /* 1276 */
    cursor_ds.x += (float)off_x;
    cursor_ds.y  = (float)(y + off_y);                                  /* 1277 */
    cursor_ds.alpha  = (u_char)(cursor_ds.alpha * alpha >> 7);          /* 1278 */
    cursor_ds.alphar = 0x48;                                            /* 1279 */
    DispSprD(&cursor_ds);                                               /* 1280 */
}

/* The two triangles above and below the selected row, pulsing with the
 * shared cursor colour. */
static void MenuSoulCursorTriangleDisp(int off_x, int off_y, u_char alpha) /* 1291 */
{
    int       up_y;
    int       down_y;
    DISP_SPRT cursor_ds;

    up_y   = menu_wrk.cursor * MENU_SOUL_ROW_STEP + MENU_SOUL_TRI_UP_Y; /* 1297 */
    down_y = menu_wrk.cursor * MENU_SOUL_ROW_STEP + MENU_SOUL_TRI_DOWN_Y; /* 1298 */

    PK2SendVram((uintptr_t)menu_soul_tex_addr, -1, -1, 0);              /* 1300 */

    CopySprDToSpr(&cursor_ds, &menu_glist_tex[MG_CURSOR_TRI_UP]);       /* 1303 */
    cursor_ds.x += (float)off_x;
    cursor_ds.y  = (float)(up_y + off_y);                               /* 1304 */
    cursor_ds.alpha  = (u_char)(cursor_ds.alpha * alpha >> 7);          /* 1305 */
    cursor_ds.alphar = 0x48;                                            /* 1306 */
    cursor_ds.r = menu_soul_disp.rgb;
    cursor_ds.g = menu_soul_disp.rgb;
    cursor_ds.b = menu_soul_disp.rgb;                                   /* 1307 */
    DispSprD(&cursor_ds);                                               /* 1308 */

    CopySprDToSpr(&cursor_ds, &menu_glist_tex[MG_CURSOR_TRI_DOWN]);     /* 1310 */
    cursor_ds.x += (float)off_x;
    cursor_ds.y  = (float)(down_y + off_y);                             /* 1311 */
    cursor_ds.alpha  = (u_char)(cursor_ds.alpha * alpha >> 7);          /* 1312 */
    cursor_ds.alphar = 0x48;                                            /* 1313 */
    cursor_ds.r = menu_soul_disp.rgb;
    cursor_ds.g = menu_soul_disp.rgb;
    cursor_ds.b = menu_soul_disp.rgb;                                   /* 1314 */
    DispSprD(&cursor_ds);                                               /* 1315 */
}

/* The two scroll arrows at the ends of the rail.  Unlike the cursor parts
 * these are blended normally -- no alphar. */
static void MenuSoulScrollArrowDisp(int off_x, int off_y, u_char alpha)  /* 1326 */
{
    DISP_SPRT arrow_ds;

    PK2SendVram((uintptr_t)menu_soul_tex_addr, -1, -1, 0);              /* 1330 */

    CopySprDToSpr(&arrow_ds, &menu_glist_tex[MG_SCROLL_ARROW_UP]);      /* 1333 */
    arrow_ds.x += (float)off_x;   arrow_ds.y += (float)off_y;           /* 1334 */
    arrow_ds.alpha = (u_char)(arrow_ds.alpha * alpha >> 7);             /* 1335 */
    arrow_ds.r = menu_soul_disp.rgb;
    arrow_ds.g = menu_soul_disp.rgb;
    arrow_ds.b = menu_soul_disp.rgb;                                    /* 1336 */
    DispSprD(&arrow_ds);                                                /* 1337 */

    CopySprDToSpr(&arrow_ds, &menu_glist_tex[MG_SCROLL_ARROW_DOWN]);    /* 1339 */
    arrow_ds.x += (float)off_x;   arrow_ds.y += (float)off_y;           /* 1340 */
    arrow_ds.alpha = (u_char)(arrow_ds.alpha * alpha >> 7);             /* 1341 */
    arrow_ds.r = menu_soul_disp.rgb;
    arrow_ds.g = menu_soul_disp.rgb;
    arrow_ds.b = menu_soul_disp.rgb;                                    /* 1342 */
    DispSprD(&arrow_ds);                                                /* 1343 */
}

/* The scrollbar thumb, sized from the list length: the full 251-pixel rail
 * for a list that fits, shrinking by (251/data_num) per row past the ninth.
 * If the thumb ends up shorter than its two end caps together the middle
 * strip is skipped and both caps are squashed to half the thumb instead --
 * menu_item.o's is the same widget in integer arithmetic. */
static void MenuSoulScrollDisp(int off_x, int off_y, u_char alpha)       /* 1354 */
{
    DISP_SPRT scroll_ds;
    float     scroll_size;
    float     scroll_y;
    float     center_size;
    float     scroll_scl;

    if (menu_soul_ctrl.ref_ctrl.data_num < MENU_SOUL_DISP_NUM) {        /* 1365 */
        scroll_size = SOUL_SCROLL_RAIL_SIZE;                            /* 1366 */
        scroll_y    = SOUL_SCROLL_RAIL_TOP;                             /* 1367 */
    }
    else {
        scroll_size = SOUL_SCROLL_RAIL_SIZE
                    - (SOUL_SCROLL_RAIL_SIZE
                       / (float)menu_soul_ctrl.ref_ctrl.data_num)
                      * (float)(menu_soul_ctrl.ref_ctrl.data_num
                                - MENU_SOUL_DISP_NUM);                  /* 1371 */
        scroll_y    = (SOUL_SCROLL_RAIL_SIZE
                       / (float)menu_soul_ctrl.ref_ctrl.data_num)
                      * (float)menu_soul_ctrl.ref_ctrl.disp_start_pos
                    + SOUL_SCROLL_RAIL_TOP;                             /* 1373 */
    }

    center_size = scroll_size - (float)(menu_glist_tex[MG_THUMB_TOP].h
                                        + menu_glist_tex[MG_THUMB_BOTTOM].h); /* 1376 */

    PK2SendVram((uintptr_t)menu_soul_tex_addr, -1, -1, 0);              /* 1378 */

    if (center_size > 0.0f) {                                           /* 1380 */
        scroll_scl = center_size / (float)menu_glist_tex[MG_THUMB_MIDDLE].h; /* 1381 */

        CopySprDToSpr(&scroll_ds, &menu_glist_tex[MG_THUMB_TOP]);       /* 1384 */
        scroll_ds.x += (float)off_x;
        scroll_ds.y  = scroll_y + (float)off_y;                         /* 1385 */
        scroll_ds.alpha  = (u_char)(scroll_ds.alpha * alpha >> 7);      /* 1386 */
        scroll_ds.alphar = 0x48;                                        /* 1387 */
        DispSprD(&scroll_ds);                                           /* 1388 */

        scroll_y += (float)menu_glist_tex[MG_THUMB_TOP].h;              /* 1390 */

        CopySprDToSpr(&scroll_ds, &menu_glist_tex[MG_THUMB_MIDDLE]);    /* 1393 */
        scroll_ds.x += (float)off_x;
        scroll_ds.y  = scroll_y + (float)off_y;                         /* 1394 */
        scroll_ds.csx = scroll_ds.x;   scroll_ds.csy = scroll_ds.y;
        scroll_ds.scw = 1.0f;
        scroll_ds.sch = scroll_scl;                                     /* 1395 */
        scroll_ds.alpha  = (u_char)(scroll_ds.alpha * alpha >> 7);      /* 1396 */
        scroll_ds.alphar = 0x48;                                        /* 1397 */
        DispSprD(&scroll_ds);                                           /* 1398 */

        scroll_y += center_size;                                        /* 1400 */

        CopySprDToSpr(&scroll_ds, &menu_glist_tex[MG_THUMB_BOTTOM]);    /* 1403 */
        scroll_ds.x += (float)off_x;
        scroll_ds.y  = scroll_y + (float)off_y;                         /* 1404 */
        scroll_ds.alpha  = (u_char)(scroll_ds.alpha * alpha >> 7);      /* 1405 */
        scroll_ds.alphar = 0x48;                                        /* 1406 */
        DispSprD(&scroll_ds);                                           /* 1407 */
    }
    else {
        /* Shorter than its own two caps: no middle at all, and each cap is
         * squashed to half the thumb. */
        CopySprDToSpr(&scroll_ds, &menu_glist_tex[MG_THUMB_TOP]);       /* 1410 */
        scroll_ds.x += (float)off_x;
        scroll_ds.y  = scroll_y + (float)off_y;                         /* 1411 */
        scroll_ds.alpha  = (u_char)(scroll_ds.alpha * alpha >> 7);      /* 1412 */
        scroll_ds.csx = scroll_ds.x;   scroll_ds.csy = scroll_ds.y;
        scroll_ds.scw = 1.0f;
        scroll_ds.sch = scroll_size * 0.5f / (float)scroll_ds.h;        /* 1413 */
        scroll_ds.alphar = 0x48;                                        /* 1414 */
        DispSprD(&scroll_ds);                                           /* 1415 */

        CopySprDToSpr(&scroll_ds, &menu_glist_tex[MG_THUMB_BOTTOM]);    /* 1417 */
        scroll_ds.x += (float)off_x;
        scroll_ds.y  = scroll_y + scroll_size * 0.5f + (float)off_y;    /* 1418 */
        scroll_ds.csx = scroll_ds.x;   scroll_ds.csy = scroll_ds.y;
        scroll_ds.scw = 1.0f;
        scroll_ds.sch = scroll_size * 0.5f / (float)scroll_ds.h;        /* 1419 */
        scroll_ds.alpha  = (u_char)(scroll_ds.alpha * alpha >> 7);      /* 1420 */
        scroll_ds.alphar = 0x48;                                        /* 1421 */
        DispSprD(&scroll_ds);                                           /* 1422 */
    }
}

static void MenuSoulCaptionDisp(int off_x, int off_y, u_char alpha)      /* 1434 */
{
    DrawCmnCapGroup_W(3, 3, alpha, 0);                                  /* 1437 */
}

static void MenuSoulNumberDisp(int data, int num, int x, int y, u_char alpha,
                               int pri, u_char zero_flg)                /* 1472 */
{
    DrawCmnNumberTex(data, num, &menu_glist_tex[MG_NUMBER], x, y,
                     alpha, pri, zero_flg);                             /* 1475 */
}

/* Lines 1487..1506 hold no code -- .text is complete without them, so that
 * twenty-line span is comment or a disabled older draw. */
static void MenuSoulNotHaveListDisp(int off_x, int off_y, u_char alpha)  /* 1486 */
{
    DrawCmnCapGroup_W(12, 12, alpha, 0);                                /* 1507 */
}

/* --------------------------------------------------------------------------
 *  Save
 * ------------------------------------------------------------------------ */

void SetSave_ListCompDispFlg(MC_SAVE_DATA *data)                        /* 1527 */
{
    data->addr = (u_char *)&list_comp_disp_flg;                         /* 1530 */
    data->size = sizeof(list_comp_disp_flg);                            /* 1531 */
}
