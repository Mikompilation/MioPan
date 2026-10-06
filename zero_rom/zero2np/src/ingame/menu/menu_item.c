// FILE: /home/zero_rom/zero2np/src/ingame/menu/menu_item.c
//
// The in-game menu's inventory page (menu_item.o).  Twenty-eight functions:
// six exports, twenty-two statics, one 3-entry dispatch table in .data and
// three lookup tables in .rodata.
//
// The page is the standard menu shape -- a *_CTRL work block with a
// MENU_REF_CTRL inside it, a menu_wrk.step ladder, an anim_step/anim_timer
// fade, and a Get/LoadReq/LoadWait/Liberate/LoadCancel texture quintet.  What
// is its own is the list itself and the cross-faded picture beside it.
//
// Facts worth knowing before touching it:
//
//  * disp_item[] is a *compacted* copy of the inventory, not a view of it.
//    SetDispItemData() walks all 58 item ids and copies the ones the player
//    holds into consecutive slots, so a row is disp_item[disp_start_pos + i]
//    and the whole list walks without re-testing anything.  Note the clearing
//    loop above it only covers the first SEVEN entries -- the number of
//    visible rows -- not all 58.
//
//  * The item picture is not in a pak.  MenuCrossFadeInStart() loads file
//    (item_id + ITEM_PICTURE_PK2) into one of menu_cmn.o's two cross-fade
//    slots; MenuItemPictureDisp() uploads whichever slot is live to the VRAM
//    scratch page at 0x2bc0 with MenuTim2SendVram() and then *patches*
//    menu_item[100]'s TEX0 to match.  That entry's TEX0 is zero in the table
//    for exactly this reason.  Both slots are drawn each frame, the outgoing
//    one first, which is the cross-fade.
//
//  * `sub_step` is the page's inner state and indexes menu_item_pad_func[]:
//    0 the list, 1 the "cannot use" message, 2 the "use this?" yes/no.
//    MenuItemUseItemMain() is what picks between 1 and 2, and it does so per
//    item type -- a film already loaded, full health or full spirit power all
//    land on 1.
//
//  * conf_csr is 0 for yes and 1 for no, and MenuItemCtrlInit() seeds it to
//    1.  MenuItemUseItemMain() re-seeds it every time it opens the window.
//
//  * A selected row and an unselected one are drawn from different sprite
//    runs and are not the same shape: the selected one repeats its middle
//    piece 20 times horizontally, the unselected one repeats a piece 23 times
//    *rotated 90 degrees* about its own leading edge.  item_sel_on is [7][4]
//    and item_sel_off [7][3] for that reason.
//
//  * The scrollbar thumb is sized from the list length: `scroll_size` is the
//    full 206-pixel rail for a list that fits, and shrinks by (206/data_num)
//    per row past the seventh.  If the thumb ends up shorter than its two end
//    caps together (`center_size < 0`) the middle strip is skipped and both
//    caps are squashed to half the thumb instead.
//
// Reconstructed from the Feb 6 2004 prototype (SLES_523.84), menu_item.o.
// All 6 ZERO2.MAP exports plus the 22 statics, verified 6/6 against the map
// and 30/30 against functions.txt (its 31st entry is the DISP_ITEM_DATA
// type_info node, a compiler artifact).  .text is accounted for byte-for-byte
// -- 0x1f8f80..0x1fb5f8 = 0x2678 = 9848 bytes: thirty-six bodies totalling
// 9748 (the thirty real ones, the four fixed_array boilerplate ones and the
// empty static-init pair) plus twenty-five 4-byte alignment fills, with no
// gap of 8 bytes or more anywhere.  item_sel_on, item_sel_off and
// item_doc_tex are diffed verbatim out of the compiled .obj, and the four
// SQAR_DAT local initialisers reproduce the ROM's .rodata bytes exactly (they
// report a DIFF because MinGW keeps only their first 16 bytes in .rodata and
// materialises the rest as immediates).
//
// Trailing /* NNN */ comments are the original source line numbers, measured
// from the $LM stabs.  Two things routinely leave a statement with no line of
// its own and those annotations are interpolated into the measured gap:
//
//  * any statement whose first act is a fixed_array subscript -- the inlined
//    operator[] reports fixed_array.h 124/125 and the caller's own note is
//    dropped.  Every disp_item[] access in the file is one of these;
//
//  * the three cases of MenuItemUseItemMain()'s switch and the three of
//    MenuItemUse()'s.  Each writes out its own SystemBankPlay() + store, and
//    GCC cross-jumped the identical tails, so only the last contributor of
//    each group carries line numbers.

#include "menu_item.h"

#include "menu.h"                               /* menu_wrk / MENU_*_TEX_ADRS */
#include "menu_cmn.h"                           /* MenuRefMove* / cross-fade  */
#include "menu_cmn_disp.h"                      /* MenuPlyrDataDisp           */
#include "tim_dat/menu_item_dat.h"              /* menu_item[]                */
#include "zero2_anim2d.h"                       /* Zero2Anim2D_CsrAnimCtrl    */

#include "../item/prg/item.h"                   /* GetPlyrItemHaveNum         */
#include "../../common/mem_util.h"              /* mem_utilGetMem             */
#include "../../common/utility2.h"              /* PRINT_ASSERT / PRINT_WARNING */
#include "../../common/variable.h"              /* plyr_wrk                   */
#include "../../graphics/graph2d/draw_cmn.h"    /* DrawCmnWindow              */
#include "../../graphics/graph2d/g2d_draw.h"    /* DISP_SPRT / DISP_SQAR      */
#include "../../graphics/graph2d/message.h"     /* PrintMsg / PrintNumber_N   */
#include "../../graphics/graph2d/tim2.h"        /* PK2SendVram                */
#include "../../ingame/plyr/player.h"           /* GetPlyrEquipmentFilmType   */
#include "../../system/eeiop/cddat.h"           /* GetFileSize / MENU_ITEM_PK2 */
#include "../../system/eeiop/fileload.h"        /* FileLoadReqEE              */
#include "../../system/eeiop/snd3d.h"           /* SND_3D_SET                 */
#include "../../system/os/system.h"             /* SystemBankPlay / GetLanguage */
#include "../../system/pad/pad.h"               /* pad / paddat               */

/* --------------------------------------------------------------------------
 *  Constants
 * ------------------------------------------------------------------------ */

/* menu_wrk.step values, the same ladder every page uses. */
#define MENU_ITEM_STEP_INIT     0
#define MENU_ITEM_STEP_LOAD     1
#define MENU_ITEM_STEP_MAIN     2
#define MENU_ITEM_STEP_MOVE     3

/* MENU_ITEM_DISP::anim_step.  Only END is tested by name here; the rest of
 * the ladder is MenuInOutAnimCtrl()'s business. */
#define MENU_ITEM_ANIM_START    0
#define MENU_ITEM_ANIM_OUT      3
#define MENU_ITEM_ANIM_END      4

/* MENU_ITEM_CTRL::sub_step -- the index into menu_item_pad_func[]. */
#define MENU_ITEM_SUB_LIST      0
#define MENU_ITEM_SUB_NO_USE    1   /* "you cannot use that"                 */
#define MENU_ITEM_SUB_USE_SEL   2   /* "use this?" yes/no                    */

/* How many rows the list shows, and how many entries it can hold. */
#define MENU_ITEM_DISP_NUM      7
#define DISP_ITEM_NUM           58

/* menu_ctrl[] row the page hands back to when it closes. */
#define MENU_STEP_TOP           8

/* An item's picture is a file of its own: this plus the item id. */
#define ITEM_PICTURE_PK2        0xe5f

/* The VRAM page the picture is uploaded to, and its TEX0 once it is there.
 * menu_item[100] carries a zero TEX0 in the table and takes this instead. */
#define ITEM_PICTURE_TBP        0x2bc0
#define ITEM_PICTURE_CBP        12000
#define ITEM_PICTURE_TEX0       0x2005dc066932abc0ULL

/* SystemBankPlay() cue numbers, as everywhere else in the menus.  7 and 9 are
 * the two consumable-use cues; the film swap re-uses the decide cue. */
#define SE_CURSOR   0
#define SE_CANCEL   1
#define SE_ERROR    2
#define SE_DECIDE   3
#define SE_ITEM_HP  7
#define SE_ITEM_SP  9

/* pad[0].rpt bits in the remapped layout, plus the analogue equivalents, and
 * the two pad[0].one bits that page the list a window at a time. */
#define PAD_RPT_UP              0x1000
#define PAD_RPT_RIGHT           0x2000
#define PAD_RPT_DOWN            0x4000
#define PAD_RPT_LEFT            0x8000
#define PAD_ANALOG_UP           0
#define PAD_ANALOG_DOWN         1
#define PAD_ANALOG_LEFT         2
#define PAD_ANALOG_RIGHT        3
#define PAD_ONE_L1              0x4
#define PAD_ONE_R1              0x8

/* The scrollbar rail, in pixels, and where its top sits. */
#define SCROLL_RAIL_SIZE        206
#define SCROLL_RAIL_TOP         175

/* menu_item[] indices that are referenced on their own rather than as a
 * range.  See tim_dat/menu_item_dat.c for the whole layout. */
#define MI_PICTURE_PLATE        0    /* the frame the picture sits in        */
#define MI_LENS                 21   /* the magnifier, drawn at 2x, additive */
#define MI_SCROLL_TOP           22   /* [22],[23] rail head; [24] x15; [25]  */
#define MI_SCROLL_TICK          24
#define MI_SCROLL_TAIL          25
#define MI_THUMB_TOP            26
#define MI_THUMB_BOTTOM         27
#define MI_THUMB_MIDDLE         28
#define MI_SCROLL_ARROW         29   /* [29],[30], both pulsing              */
#define MI_PICTURE              100
#define MI_TITLE_ITEM           101  /* [101],[102] out of the item pak      */
#define MI_TITLE_BG             103  /* [103],[104] out of the background one */

/* --------------------------------------------------------------------------
 *  Work
 * ------------------------------------------------------------------------ */

static void *menu_item_tex_addr;                            /* sdata 3f2dd8 */

static void MenuItemPad(void);
static void MenuItemNoUsePad(void);
static void MenuItemUseSelPad(void);

/* Indexed by MENU_ITEM_CTRL::sub_step.  MenuNonItemPad() is deliberately not
 * here -- an empty inventory takes a different path in MenuItem(). */
static void (*menu_item_pad_func[3])(void) =                 /* data 325048 */
{
    MenuItemPad,
    MenuItemNoUsePad,
    MenuItemUseSelPad,
};

static fixed_array<DISP_ITEM_DATA, DISP_ITEM_NUM> disp_item;  /* bss 4b59f0 */
static MENU_ITEM_CTRL menu_item_ctrl;                         /* bss 4b5bc0 */
static MENU_ITEM_DISP menu_item_disp;                         /* sbss 3f4e28 */

static void MenuItemInit(void);
static void MenuItemCtrlInit(void);
static int  MenuItemTexLoadWait(void);
static void SetDispItemData(void);
static void MenuNonItemPad(void);
static void MenuItemUseItemMain(int item_id);
static void MenuItemUse(void);
static void MenuItemOutReq(void);
static void MenuItemDispInit(void);
static void MenuItemTitleDisp(int off_x, int off_y, u_char alpha);
static void MenuItemPlyrDataDisp(int off_x, int off_y, u_char alpha);
static void MenuItemWindowDisp(int off_x, int off_y, u_char alpha);
static void MenuItemListScrollDisp(int off_x, int off_y, u_char alpha);
static void MenuPlyrItemDisp(int off_x, int off_y, u_char alpha);
static void MenuItemExplanation(int off_x, int off_y, u_char alpha);
static void MenuItemPictureDisp(int off_x, int off_y, u_char alpha);
static void MenuItemLensDisp(int off_x, int off_y, u_char alpha);
static void MenuItemCaptionDisp(int off_x, int off_y, u_char alpha);
static void MenuItemConfirmDisp(int off_x, int off_y, u_char alpha);
static void MenuItemUseSelDisp(int off_x, int off_y, u_char alpha);
static void MenuItemNoHaveItemDisp(int off_x, int off_y, u_char alpha);

/* --------------------------------------------------------------------------
 *  Entry / texture
 * ------------------------------------------------------------------------ */

/* The pak should already be resident -- menu_top.c requests it as the hub
 * fades out -- so a null here means the load never happened and the page
 * starts one of its own rather than drawing over garbage. */
static void MenuItemInit(void)                                          /* 164 */
{
    menu_wrk.cursor = 0;                                                /* 166 */

    MenuItemCtrlInit();                                                 /* 170 */

    MenuCrossFadeInit();                                                /* 173 */

    if (menu_item_tex_addr == nullptr) {                                /* 176 */
        PRINT_WARNING("Menu Item Tex Back reading failure\n");          /* 177 */
        MenuItemTexLoadReq();                                           /* 178 */
    }
}

static void MenuItemCtrlInit(void)                                      /* 187 */
{
    menu_item_ctrl.sub_step       = MENU_ITEM_SUB_LIST;                 /* 190 */
    menu_item_ctrl.cross_fade_flg = 0;                                  /* 191 */
    menu_item_ctrl.conf_csr       = 1;                                  /* 192 */

    MenuRefCtrlInit(&menu_item_ctrl.ref_ctrl, GetHaveItemTypeNum());    /* 194 */
}

void GetMenuItemTexMem(void)                                            /* 201 */
{
    if (menu_item_tex_addr != nullptr) {                                /* 204 */
        LiberateMenuItemTexMem();                                       /* 205 */
    }

    if (menu_item_tex_addr == nullptr) {                                /* 209 */
        menu_item_tex_addr =
            mem_utilGetMem((int)GetFileSize(MENU_ITEM_PK2 + GetLanguage())); /* 210 */
    }
}

void MenuItemTexLoadReq(void)                                           /* 219 */
{
    if (menu_item_tex_addr == nullptr) {                                /* 222 */
        GetMenuItemTexMem();                                            /* 224 */
    }

    FileLoadReqEE(MENU_ITEM_PK2 + GetLanguage(), menu_item_tex_addr,
                  2, nullptr, nullptr);                                 /* 229 */
}

static int MenuItemTexLoadWait(void)                                    /* 239 */
{
    if (FileLoadIsEnd2(MENU_ITEM_PK2 + GetLanguage(),
                       menu_item_tex_addr) != 0) {                      /* 247 */
        return 1;
    }

    return 0;                                                           /* 252 */
}

/* Rebuild the compacted list.  Called on entry and after every use, because
 * spending the last of an item removes its row. */
static void SetDispItemData(void)                                       /* 260 */
{
    int   i;
    int   have_num;
    short disp_count;

    disp_count = 0;                                                     /* 267 */

    /* Only the seven visible rows are cleared, not all 58 -- the rest are
     * never read past ref_ctrl.data_num. */
    for (i = 0; i < MENU_ITEM_DISP_NUM; i++) {                          /* 269 */
        disp_item[i].item_id  = 0xff;                                   /* 270 */
        disp_item[i].have_num = 0;                                      /* 271 */
    }                                                                   /* 272 */

    for (i = 0; i < DISP_ITEM_NUM; i++) {                               /* 276 */
        have_num = GetPlyrItemHaveNum(i);                               /* 278 */

        if (have_num > 0) {                                             /* 280 */
            disp_item[disp_count].item_id  = i;                         /* 281 */
            disp_item[disp_count].have_num = have_num;                  /* 282 */

            disp_count++;                                               /* 283 */
        }
    }                                                                   /* 285 */

    menu_item_ctrl.ref_ctrl.data_num = GetHaveItemTypeNum();            /* 289 */
}

/* --------------------------------------------------------------------------
 *  Per-frame
 * ------------------------------------------------------------------------ */

void MenuItem(void)                                                     /* 299 */
{
    if (menu_wrk.step == MENU_ITEM_STEP_INIT) {                         /* 302 */
        MenuItemInit();                                                 /* 304 */

        SetDispItemData();                                              /* 307 */

        menu_wrk.step = MENU_ITEM_STEP_LOAD;                            /* 309 */
    }

    if (menu_wrk.step == MENU_ITEM_STEP_LOAD) {                         /* 312 */
        if (MenuItemTexLoadWait() != 0) {                               /* 314 */
            MenuItemDispInit();                                         /* 316 */
            menu_wrk.step = MENU_ITEM_STEP_MAIN;                        /* 317 */

            if (menu_item_ctrl.ref_ctrl.data_num != 0) {                /* 320 */
                MenuCrossFadeInStart(menu_item_ctrl.cross_fade_flg,
                                     disp_item[menu_item_ctrl.ref_ctrl.data_pos].item_id
                                         + ITEM_PICTURE_PK2);           /* 321 */
            }

            menu_item_ctrl.sub_step = MENU_ITEM_SUB_LIST;               /* 324 */
        }
    }

    if (menu_wrk.step == MENU_ITEM_STEP_MAIN) {                         /* 328 */
        if (menu_item_ctrl.ref_ctrl.data_num != 0) {                    /* 330 */
            if (menu_item_pad_func[menu_item_ctrl.sub_step] != nullptr) { /* 332 */
                (*menu_item_pad_func[menu_item_ctrl.sub_step])();        /* 333 */
            }

            MenuCmnCrossFade();                                         /* 337 */
        }
        else {
            MenuNonItemPad();                                           /* 341 */
        }
    }

    if (menu_wrk.step == MENU_ITEM_STEP_MOVE) {                         /* 345 */
        if (menu_item_disp.anim_step == MENU_ITEM_ANIM_END) {           /* 346 */
            SetNextMenuStep(MENU_STEP_TOP);                             /* 348 */

            LiberateMenuItemTexMem();                                   /* 351 */

            MenuCrossFadeTexLoadCancel(0);                              /* 353 */
            MenuCrossFadeTexLoadCancel(1);                              /* 354 */

            LiberateAllMenuCrossFadeTexMem();                           /* 356 */
        }
    }
}

/* The list's own pad.  `load_flg` is "the selection moved", which is what
 * makes the picture cross-fade to the new item.
 *
 * The four MenuRefMove* results are each normalised to 0/1 before landing in
 * load_flg; GCC folded three of the four tails together, so which source
 * spelling was used (`load_flg = (x != 0);` or `if (x != 0) load_flg = 1;`)
 * is not recoverable -- the value is. */
static void MenuItemPad(void)                                           /* 367 */
{
    short  disp_num;
    u_char load_flg;

    disp_num = menu_item_ctrl.ref_ctrl.data_num;                        /* 372 */

    if (MENU_ITEM_DISP_NUM < disp_num) {                                /* 374 */
        disp_num = MENU_ITEM_DISP_NUM;
    }

    load_flg = 0;                                                       /* 378 */

    if ((pad[0].rpt & PAD_RPT_UP) || GetPadAnalogRpt(PAD_ANALOG_UP)) {  /* 382 */
        load_flg = (MenuRefMovePadLup(&menu_item_ctrl.ref_ctrl, &menu_wrk.cursor,
                                      disp_num, MENU_ITEM_DISP_NUM) != 0); /* 383 */
    }                                                                   /* 384 */
    else if ((pad[0].rpt & PAD_RPT_DOWN) || GetPadAnalogRpt(PAD_ANALOG_DOWN)) { /* 388 */
        load_flg = (MenuRefMovePadLdown(&menu_item_ctrl.ref_ctrl, &menu_wrk.cursor,
                                        disp_num, MENU_ITEM_DISP_NUM) != 0); /* 389 */
    }                                                                   /* 390 */
    else if (*paddat[0] == 1) {                                         /* 394 */
        if (GetPlyrItemHaveNum(
                disp_item[menu_item_ctrl.ref_ctrl.data_pos].item_id) != 0) { /* 396 */
            MenuItemUseItemMain(
                disp_item[menu_item_ctrl.ref_ctrl.data_pos].item_id);   /* 399 */
        }
    }
    else if (*paddat[1] == 1) {                                         /* 403 */
        SystemBankPlay(SE_CANCEL, 1, 0, 0, (SND_3D_SET *)nullptr,
                       0x3200, 0x1000);                                 /* 404 */

        MenuItemOutReq();                                               /* 407 */
    }
    else if (pad[0].one & PAD_ONE_L1) {                                 /* 410 */
        load_flg = (MenuRefMovePageUp(&menu_item_ctrl.ref_ctrl, &menu_wrk.cursor,
                                      disp_num, MENU_ITEM_DISP_NUM) != 0); /* 411 */
    }                                                                   /* 412 */
    else if (pad[0].one & PAD_ONE_R1) {                                 /* 416 */
        load_flg = (MenuRefMovePageDown(&menu_item_ctrl.ref_ctrl, &menu_wrk.cursor,
                                        disp_num, MENU_ITEM_DISP_NUM) != 0); /* 417 */
    }

    if (load_flg == 1) {                                                /* 423 */
        SystemBankPlay(SE_CURSOR, 1, 0, 0, (SND_3D_SET *)nullptr,
                       0x3200, 0x1000);                                 /* 424 */

        MenuCrossFadeOutStart(menu_item_ctrl.cross_fade_flg);           /* 426 */
        menu_item_ctrl.cross_fade_flg ^= 1;                             /* 427 */
        MenuCrossFadeInStart(menu_item_ctrl.cross_fade_flg,
                             disp_item[menu_item_ctrl.ref_ctrl.data_pos].item_id
                                 + ITEM_PICTURE_PK2);                   /* 428 */
    }
}

/* The "cannot use that" message: either button dismisses it. */
static void MenuItemNoUsePad(void)                                      /* 436 */
{
    if (MenuCmnConfirmPad() != 0) {                                     /* 439 */
        menu_item_ctrl.sub_step = MENU_ITEM_SUB_LIST;                   /* 440 */
    }
}

/* An empty inventory: there is nothing to select, so the only thing the page
 * still answers is TRIANGLE. */
static void MenuNonItemPad(void)                                        /* 449 */
{
    if (*paddat[1] == 1) {                                              /* 453 */
        SystemBankPlay(SE_CANCEL, 1, 0, 0, (SND_3D_SET *)nullptr,
                       0x3200, 0x1000);                                 /* 454 */

        SetNextMenuStep(MENU_STEP_TOP);                                 /* 456 */
    }
}

/* CROSS on a row.  Decides whether the item can actually be used and opens
 * the matching window -- the yes/no if it can, the message if it cannot.
 *
 * The three type arms are written out in full in the ROM and GCC cross-jumped
 * their identical bodies, so only the last copy of each carries lines. */
static void MenuItemUseItemMain(int item_id)                            /* 466 */
{
    if (DISP_ITEM_NUM <= item_id) {                                     /* 469 */
        PRINT_ASSERT("Error! MenuItemUseItemMain item_id %d", item_id); /* 470 */
    }

    switch (GetItemType(item_id)) {                                     /* 475 */
    case ITEM_TYPE_FILM:
        /* Loading the film that is already loaded does nothing. */
        if (GetPlyrEquipmentFilmType() != item_id) {                    /* 482 */
            SystemBankPlay(SE_DECIDE, 1, 0, 0, (SND_3D_SET *)nullptr,
                           0x3200, 0x1000);
            menu_item_ctrl.sub_step = MENU_ITEM_SUB_USE_SEL;
        }
        else {                                                          /* 484 */
            SystemBankPlay(SE_ERROR, 1, 0, 0, (SND_3D_SET *)nullptr,
                           0x3200, 0x1000);
            menu_item_ctrl.sub_step = MENU_ITEM_SUB_NO_USE;
        }
        break;

    case ITEM_TYPE_HP:
        if (plyr_wrk.cmn_wrk.st.hp != plyr_wrk.cmn_wrk.st.hpmax) {      /* 493 */
            SystemBankPlay(SE_DECIDE, 1, 0, 0, (SND_3D_SET *)nullptr,
                           0x3200, 0x1000);
            menu_item_ctrl.sub_step = MENU_ITEM_SUB_USE_SEL;
        }
        else {                                                          /* 495 */
            SystemBankPlay(SE_ERROR, 1, 0, 0, (SND_3D_SET *)nullptr,
                           0x3200, 0x1000);
            menu_item_ctrl.sub_step = MENU_ITEM_SUB_NO_USE;
        }
        break;

    case ITEM_TYPE_SP:
        if (plyr_wrk.cmn_wrk.st.sp != plyr_wrk.cmn_wrk.st.spmax) {      /* 504 */
            SystemBankPlay(SE_DECIDE, 1, 0, 0, (SND_3D_SET *)nullptr,
                           0x3200, 0x1000);                             /* 509 */
            menu_item_ctrl.sub_step = MENU_ITEM_SUB_USE_SEL;            /* 510 */
        }
        else {
            SystemBankPlay(SE_ERROR, 1, 0, 0, (SND_3D_SET *)nullptr,
                           0x3200, 0x1000);
            menu_item_ctrl.sub_step = MENU_ITEM_SUB_NO_USE;
        }
        break;                                                          /* 512 */

    case ITEM_TYPE_NONE:
    case ITEM_TYPE_CONSUME:
    case ITEM_TYPE_EVENT:
        SystemBankPlay(SE_ERROR, 1, 0, 0, (SND_3D_SET *)nullptr,
                       0x3200, 0x1000);                                 /* 515 */
        menu_item_ctrl.sub_step = MENU_ITEM_SUB_NO_USE;                 /* 516 */
        break;                                                          /* 517 */

    default:
        PRINT_WARNING("ITEM ID Error!!");                               /* 519 */
        break;
    }

    if (menu_item_ctrl.sub_step == MENU_ITEM_SUB_USE_SEL) {             /* 523 */
        menu_item_ctrl.conf_csr = 1;                                    /* 524 */
    }
}

/* The "use this?" window's pad.  Left and right both just toggle. */
static void MenuItemUseSelPad(void)                                     /* 531 */
{
    if ((pad[0].rpt & PAD_RPT_LEFT) || GetPadAnalogRpt(PAD_ANALOG_LEFT) || /* 535 */
        (pad[0].rpt & PAD_RPT_RIGHT) || GetPadAnalogRpt(PAD_ANALOG_RIGHT)) { /* 541 */
        SystemBankPlay(SE_CURSOR, 1, 0, 0, (SND_3D_SET *)nullptr,
                       0x3200, 0x1000);                                 /* 542 */

        menu_item_ctrl.conf_csr ^= 1;                                   /* 544 */
    }
    else if (*paddat[0] == 1) {                                         /* 546 */
        if (menu_item_ctrl.conf_csr == 0) {                             /* 548 */
            MenuItemUse();                                              /* 549 */
            menu_item_ctrl.sub_step = MENU_ITEM_SUB_LIST;               /* 550 */
        }
        else {
            SystemBankPlay(SE_DECIDE, 1, 0, 0, (SND_3D_SET *)nullptr,
                           0x3200, 0x1000);
            menu_item_ctrl.sub_step = MENU_ITEM_SUB_LIST;               /* 555 */
        }
    }
    else if (*paddat[1] == 1) {                                         /* 558 */
        SystemBankPlay(SE_CANCEL, 1, 0, 0, (SND_3D_SET *)nullptr,
                       0x3200, 0x1000);                                 /* 559 */

        menu_item_ctrl.sub_step = MENU_ITEM_SUB_LIST;                   /* 560 */
    }
}

/* Actually spend it.  Rebuilding the list is the interesting part: if that
 * was the last one the row disappears, so the selection has to be pulled back
 * a row and the picture cross-faded to whatever is now under the cursor.
 *
 * The three cue arms are cross-jumped the same way MenuItemUseItemMain()'s
 * are; only the last carries line numbers. */
static void MenuItemUse(void)                                           /* 569 */
{
    int item_id;

    item_id = disp_item[menu_item_ctrl.ref_ctrl.data_pos].item_id;      /* 571 */

    switch (GetItemType(item_id)) {                                     /* 577 */
    case ITEM_TYPE_FILM:
        SystemBankPlay(SE_DECIDE, 1, 0, 0, (SND_3D_SET *)nullptr,
                       0x3200, 0x1000);                                 /* 580 */
        break;

    case ITEM_TYPE_HP:
        SystemBankPlay(SE_ITEM_HP, 1, 0, 0, (SND_3D_SET *)nullptr,
                       0x3200, 0x1000);                                 /* 583 */
        break;

    case ITEM_TYPE_SP:
        SystemBankPlay(SE_ITEM_SP, 1, 0, 0, (SND_3D_SET *)nullptr,
                       0x3200, 0x1000);                                 /* 585 */
        break;                                                          /* 586 */

    case ITEM_TYPE_NONE:
    case ITEM_TYPE_CONSUME:
    case ITEM_TYPE_EVENT:
    default:
        PRINT_ASSERT("Error!! %s item_id[%d]", __FUNCTION__, item_id);  /* 591 */
        break;
    }

    if (ItemUse(item_id, 1) != 0) {                                     /* 595 */
        if (GetPlyrItemHaveNum(item_id) == 0) {                         /* 597 */
            SetDispItemData();                                          /* 599 */

            if (menu_item_ctrl.ref_ctrl.data_num > 0) {                 /* 602 */
                if (disp_item[menu_item_ctrl.ref_ctrl.data_pos].item_id == 0xff) { /* 603 */
                    menu_item_ctrl.ref_ctrl.data_pos--;                 /* 604 */

                    if (menu_wrk.cursor >= menu_item_ctrl.ref_ctrl.data_num) { /* 606 */
                        menu_wrk.cursor = menu_item_ctrl.ref_ctrl.data_num - 1; /* 607 */
                    }
                }

                MenuCrossFadeOutStart(menu_item_ctrl.cross_fade_flg);   /* 612 */
                menu_item_ctrl.cross_fade_flg ^= 1;                     /* 613 */
                MenuCrossFadeInStart(menu_item_ctrl.cross_fade_flg,
                                     disp_item[menu_item_ctrl.ref_ctrl.data_pos].item_id
                                         + ITEM_PICTURE_PK2);           /* 615 */
            }
            else {
                MenuCrossFadeOutStart(menu_item_ctrl.cross_fade_flg);   /* 619 */
            }
        }
        else {
            SetDispItemData();                                          /* 624 */
        }
    }
}

static void MenuItemOutReq(void)                                        /* 634 */
{
    menu_wrk.step = MENU_ITEM_STEP_MOVE;                                /* 637 */

    menu_item_disp.anim_step  = MENU_ITEM_ANIM_OUT;                     /* 638 */
    menu_item_disp.anim_timer = 0;                                      /* 639 */
}

void LiberateMenuItemTexMem(void)                                       /* 651 */
{
    if (menu_item_tex_addr != nullptr) {                                /* 654 */
        mem_utilFreeMem(menu_item_tex_addr);                            /* 655 */
        menu_item_tex_addr = nullptr;                                   /* 656 */
    }
}

void MenuItemTexLoadCancel(void)                                        /* 665 */
{
    if (MenuItemTexLoadWait() == 0) {                                   /* 669 */
        FileLoadCancel2(MENU_ITEM_PK2 + GetLanguage(), menu_item_tex_addr,
                        nullptr, nullptr);                              /* 670 */
    }
}

/* --------------------------------------------------------------------------
 *  Drawing
 * ------------------------------------------------------------------------ */

static void MenuItemDispInit(void)                                      /* 683 */
{
    menu_item_disp.anim_step    = MENU_ITEM_ANIM_START;                 /* 686 */
    menu_item_disp.anim_timer   = 0;                                    /* 687 */
    menu_item_disp.rgb          = 0x40;                                 /* 688 */
    menu_item_disp.scroll_timer = 0;                                    /* 689 */
}

void MenuItemDisp(void)                                                 /* 697 */
{
    u_char alpha;

    alpha = 0;

    if (menu_wrk.step == MENU_ITEM_STEP_MAIN
        || menu_wrk.step == MENU_ITEM_STEP_MOVE) {                      /* 704 */
        Zero2Anim2D_CsrAnimCtrl(&menu_item_disp.scroll_timer,
                                &menu_item_disp.rgb);                   /* 706 */

        if (menu_item_disp.anim_step != MENU_ITEM_ANIM_END) {           /* 708 */
            MenuInOutAnimCtrl(&menu_item_disp.anim_step,
                              &menu_item_disp.anim_timer, &alpha);      /* 710 */

            MenuItemTitleDisp(0, 0, alpha);                             /* 713 */

            if (menu_item_ctrl.ref_ctrl.data_num > 0) {                 /* 716 */
                MenuItemWindowDisp(0, 0, alpha);                        /* 718 */

                MenuItemLensDisp(0, 0, alpha);                          /* 720 */

                MenuItemPictureDisp(0, 0, alpha);                       /* 722 */

                MenuPlyrItemDisp(0, 0, alpha);                          /* 724 */

                MenuItemPlyrDataDisp(0, 0, alpha);                      /* 727 */

                MenuItemCaptionDisp(0, 0, alpha);                       /* 730 */

                /* Both windows draw at full alpha -- they open over a page
                 * that has already faded in. */
                if (menu_item_ctrl.sub_step == MENU_ITEM_SUB_NO_USE) {  /* 733 */
                    MenuItemConfirmDisp(0, 0, 0x80);                    /* 734 */
                }
                else if (menu_item_ctrl.sub_step == MENU_ITEM_SUB_USE_SEL) { /* 737 */
                    MenuItemUseSelDisp(0, 0, 0x80);                     /* 738 */
                }
            }
            else {
                MenuItemNoHaveItemDisp(0, 0, alpha);                    /* 744 */
            }
        }
    }
}

/* The page title.  Two halves out of the menu background pak and two out of
 * the item pak, which is why it takes two PK2SendVram() calls. */
static void MenuItemTitleDisp(int off_x, int off_y, u_char alpha)       /* 759 */
{
    DISP_SPRT title_ds;

    PK2SendVram(MENU_BG_TEX_ADRS, -1, -1, 0);                           /* 763 */

    CopySprDToSpr(&title_ds, &menu_item[MI_TITLE_BG]);                  /* 766 */
    title_ds.x += (float)off_x;   title_ds.y += (float)off_y;           /* 767 */
    title_ds.alpha = (u_char)(title_ds.alpha * alpha >> 7);             /* 768 */
    DispSprD(&title_ds);                                                /* 769 */

    CopySprDToSpr(&title_ds, &menu_item[MI_TITLE_BG + 1]);              /* 770 */
    title_ds.x += (float)off_x;   title_ds.y += (float)off_y;           /* 771 */
    title_ds.alpha = (u_char)(title_ds.alpha * alpha >> 7);             /* 772 */
    DispSprD(&title_ds);                                                /* 773 */

    PK2SendVram((uintptr_t)menu_item_tex_addr, -1, -1, 0);              /* 776 */

    CopySprDToSpr(&title_ds, &menu_item[MI_TITLE_ITEM]);                /* 778 */
    title_ds.x += (float)off_x;   title_ds.y += (float)off_y;           /* 779 */
    title_ds.alpha = (u_char)(title_ds.alpha * alpha >> 7);             /* 780 */
    DispSprD(&title_ds);                                                /* 781 */

    CopySprDToSpr(&title_ds, &menu_item[MI_TITLE_ITEM + 1]);            /* 782 */
    title_ds.x += (float)off_x;   title_ds.y += (float)off_y;           /* 783 */
    title_ds.alpha = (u_char)(title_ds.alpha * alpha >> 7);             /* 784 */
    DispSprD(&title_ds);                                                /* 785 */
}

/* The shared player plate, at this page's own position.  off_x / off_y are
 * taken and read by neither -- another of the folder's display helpers that
 * carries them for consistency. */
static void MenuItemPlyrDataDisp(int off_x, int off_y, u_char alpha)    /* 796 */
{
    MenuPlyrDataDisp(34, 57, alpha);                                    /* 797 */
}

/* The two black panels the page sits on, the scrollbar, and the three runs of
 * frame art over them. */
static void MenuItemWindowDisp(int off_x, int off_y, u_char alpha)      /* 806 */
{
    DISP_SPRT title_ds;
    int       i;
    DISP_SQAR dsq;

    SQAR_DAT item_list_bg = { 333, 296,  57, 130, 160, 0, 0, 0, 0x59 }; /* 812 */
    SQAR_DAT item_msg_bg  = { 207, 146, 395, 276, 160, 0, 0, 0, 0x26 }; /* 815 */

    PK2SendVram((uintptr_t)menu_item_tex_addr, -1, -1, 0);              /* 821 */

    CopySqrDToSqr(&dsq, &item_list_bg);                                 /* 824 */
    dsq.alpha = (u_char)(dsq.alpha * alpha >> 7);                       /* 825 */
    DispSqrD(&dsq);                                                     /* 826 */

    CopySqrDToSqr(&dsq, &item_msg_bg);                                  /* 829 */
    dsq.alpha = (u_char)(dsq.alpha * alpha >> 7);                       /* 830 */
    DispSqrD(&dsq);                                                     /* 831 */

    MenuItemListScrollDisp(off_x, off_y, alpha);                        /* 834 */

    for (i = 1; i < 8; i++) {                                           /* 836 */
        CopySprDToSpr(&title_ds, &menu_item[i]);                        /* 837 */
        title_ds.x += (float)off_x;   title_ds.y += (float)off_y;       /* 838 */
        title_ds.alpha = (u_char)(title_ds.alpha * alpha >> 7);         /* 839 */
        DispSprD(&title_ds);                                            /* 840 */
    }                                                                   /* 841 */

    for (i = 8; i < 16; i++) {                                          /* 843 */
        CopySprDToSpr(&title_ds, &menu_item[i]);                        /* 844 */
        title_ds.x += (float)off_x;   title_ds.y += (float)off_y;       /* 845 */
        title_ds.alpha = (u_char)(title_ds.alpha * alpha >> 7);         /* 846 */
        DispSprD(&title_ds);                                            /* 847 */
    }                                                                   /* 848 */

    for (i = 16; i < 21; i++) {                                         /* 850 */
        CopySprDToSpr(&title_ds, &menu_item[i]);                        /* 851 */
        title_ds.x += (float)off_x;   title_ds.y += (float)off_y;       /* 852 */
        title_ds.alpha = (u_char)(title_ds.alpha * alpha >> 7);         /* 853 */
        DispSprD(&title_ds);                                            /* 854 */
    }                                                                   /* 855 */
}

/* The scrollbar: a fifteen-tick rail with a thumb over it.
 *
 * The thumb's length and position come from the list, not from the window --
 * a list that fits gets the whole rail.  Note the two black backing squares
 * are drawn WITHOUT the page alpha, unlike every other quad in the file, so
 * they are solid from the first frame of the fade. */
static void MenuItemListScrollDisp(int off_x, int off_y, u_char alpha)  /* 867 */
{
    DISP_SPRT scroll_ds;
    DISP_SQAR dsq;
    int       i;

    SQAR_DAT scroll_bg_up   = { 20, 20, 37, 135, 160, 0, 0, 0, 0x59 };  /* 871 */
    SQAR_DAT scroll_bg_down = { 20, 20, 37, 399, 160, 0, 0, 0, 0x59 };  /* 874 */

    int scroll_size;
    int scroll_y;
    int center_size;

    if (menu_item_ctrl.ref_ctrl.data_num < MENU_ITEM_DISP_NUM) {        /* 883 */
        scroll_size = SCROLL_RAIL_SIZE;                                 /* 884 */
        scroll_y    = SCROLL_RAIL_TOP;                                  /* 885 */
    }
    else {
        scroll_size = SCROLL_RAIL_SIZE
                    - (SCROLL_RAIL_SIZE / menu_item_ctrl.ref_ctrl.data_num)
                      * (menu_item_ctrl.ref_ctrl.data_num - MENU_ITEM_DISP_NUM); /* 888 */
        scroll_y    = (SCROLL_RAIL_SIZE / menu_item_ctrl.ref_ctrl.data_num)
                      * menu_item_ctrl.ref_ctrl.disp_start_pos
                    + SCROLL_RAIL_TOP;                                  /* 889 */
    }

    center_size = scroll_size - (menu_item[MI_THUMB_TOP].h
                                 + menu_item[MI_THUMB_BOTTOM].h);       /* 891 */

    PK2SendVram((uintptr_t)menu_item_tex_addr, -1, -1, 0);              /* 894 */

    CopySqrDToSqr(&dsq, &scroll_bg_up);                                 /* 897 */
    DispSqrD(&dsq);                                                     /* 898 */

    CopySqrDToSqr(&dsq, &scroll_bg_down);                               /* 899 */
    DispSqrD(&dsq);                                                     /* 900 */

    CopySprDToSpr(&scroll_ds, &menu_item[MI_SCROLL_TOP]);               /* 903 */
    scroll_ds.x += (float)off_x;   scroll_ds.y += (float)off_y;         /* 904 */
    scroll_ds.alpha = (u_char)(scroll_ds.alpha * alpha >> 7);           /* 905 */
    DispSprD(&scroll_ds);                                               /* 906 */

    CopySprDToSpr(&scroll_ds, &menu_item[MI_SCROLL_TOP + 1]);           /* 907 */
    scroll_ds.x += (float)off_x;   scroll_ds.y += (float)off_y;         /* 908 */
    scroll_ds.alpha = (u_char)(scroll_ds.alpha * alpha >> 7);           /* 909 */
    DispSprD(&scroll_ds);                                               /* 910 */

    for (i = 0; i < 15; i++) {                                          /* 911 */
        CopySprDToSpr(&scroll_ds, &menu_item[MI_SCROLL_TICK]);          /* 912 */
        scroll_ds.x += (float)off_x;
        scroll_ds.y  = scroll_ds.y + (float)off_y + (float)(i * 10);    /* 913 */
        scroll_ds.alpha = (u_char)(scroll_ds.alpha * alpha >> 7);       /* 914 */
        DispSprD(&scroll_ds);                                           /* 915 */
    }                                                                   /* 916 */

    CopySprDToSpr(&scroll_ds, &menu_item[MI_SCROLL_TAIL]);              /* 917 */
    scroll_ds.x += (float)off_x;   scroll_ds.y += (float)off_y;         /* 918 */
    scroll_ds.alpha = (u_char)(scroll_ds.alpha * alpha >> 7);           /* 919 */
    DispSprD(&scroll_ds);                                               /* 920 */

    if (0 <= center_size) {                                             /* 924 */
        CopySprDToSpr(&scroll_ds, &menu_item[MI_THUMB_TOP]);            /* 925 */
        scroll_ds.x += (float)off_x;
        scroll_ds.y  = (float)(scroll_y + off_y);                       /* 926 */
        scroll_ds.alpha = (u_char)(scroll_ds.alpha * alpha >> 7);       /* 927 */
        DispSprD(&scroll_ds);                                           /* 928 */

        scroll_y += menu_item[MI_THUMB_TOP].h;                          /* 930 */

        while (center_size - (int)menu_item[MI_THUMB_MIDDLE].h >= 0) {  /* 933 */
            center_size -= menu_item[MI_THUMB_MIDDLE].h;                /* 937 */

            CopySprDToSpr(&scroll_ds, &menu_item[MI_THUMB_MIDDLE]);     /* 940 */
            scroll_ds.x += (float)off_x;
            scroll_ds.y  = (float)(scroll_y + off_y);                   /* 941 */
            scroll_ds.alpha = (u_char)(scroll_ds.alpha * alpha >> 7);   /* 942 */
            DispSprD(&scroll_ds);                                       /* 943 */

            scroll_y += menu_item[MI_THUMB_MIDDLE].h;                   /* 945 */
        }

        /* Whatever is left over is one more middle strip, squashed. */
        CopySprDToSpr(&scroll_ds, &menu_item[MI_THUMB_MIDDLE]);         /* 948 */
        scroll_ds.x += (float)off_x;
        scroll_ds.y  = (float)(scroll_y + off_y);                       /* 949 */
        scroll_ds.alpha = (u_char)(scroll_ds.alpha * alpha >> 7);       /* 950 */
        scroll_ds.scw = 1.0f;
        scroll_ds.sch = (float)center_size / (float)scroll_ds.h;
        scroll_ds.csx = scroll_ds.x;   scroll_ds.csy = scroll_ds.y;     /* 951 */
        DispSprD(&scroll_ds);                                           /* 952 */

        scroll_y += center_size;                                        /* 954 */

        CopySprDToSpr(&scroll_ds, &menu_item[MI_THUMB_BOTTOM]);         /* 956 */
        scroll_ds.x += (float)off_x;
        scroll_ds.y  = (float)(scroll_y + off_y);                       /* 957 */
        scroll_ds.alpha = (u_char)(scroll_ds.alpha * alpha >> 7);       /* 958 */
        DispSprD(&scroll_ds);                                           /* 959 */
    }
    else {
        /* Shorter than its own two caps: no middle at all, and each cap is
         * squashed to half the thumb.  Note this is an integer divide, unlike
         * the float one at 951. */
        CopySprDToSpr(&scroll_ds, &menu_item[MI_THUMB_TOP]);            /* 962 */
        scroll_ds.x += (float)off_x;
        scroll_ds.y  = (float)(scroll_y + off_y);                       /* 963 */
        scroll_ds.alpha = (u_char)(scroll_ds.alpha * alpha >> 7);       /* 964 */
        scroll_ds.scw = 1.0f;
        scroll_ds.sch = (float)(scroll_size / 2 / scroll_ds.h);
        scroll_ds.csx = scroll_ds.x;   scroll_ds.csy = scroll_ds.y;     /* 965 */
        DispSprD(&scroll_ds);                                           /* 966 */

        CopySprDToSpr(&scroll_ds, &menu_item[MI_THUMB_BOTTOM]);         /* 967 */
        scroll_ds.x += (float)off_x;
        scroll_ds.y  = (float)(scroll_y + scroll_size / 2 + off_y);     /* 968 */
        scroll_ds.scw = 1.0f;
        scroll_ds.sch = (float)(scroll_size / 2 / scroll_ds.h);
        scroll_ds.csx = scroll_ds.x;   scroll_ds.csy = scroll_ds.y;     /* 969 */
        scroll_ds.alpha = (u_char)(scroll_ds.alpha * alpha >> 7);       /* 970 */
        DispSprD(&scroll_ds);                                           /* 971 */
    }

    CopySprDToSpr(&scroll_ds, &menu_item[MI_SCROLL_ARROW]);             /* 976 */
    scroll_ds.x += (float)off_x;   scroll_ds.y += (float)off_y;         /* 977 */
    scroll_ds.alpha = (u_char)(scroll_ds.alpha * alpha >> 7);           /* 978 */
    scroll_ds.r = menu_item_disp.rgb;
    scroll_ds.g = menu_item_disp.rgb;
    scroll_ds.b = menu_item_disp.rgb;                                   /* 979 */
    DispSprD(&scroll_ds);                                               /* 980 */

    CopySprDToSpr(&scroll_ds, &menu_item[MI_SCROLL_ARROW + 1]);         /* 983 */
    scroll_ds.x += (float)off_x;   scroll_ds.y += (float)off_y;         /* 984 */
    scroll_ds.alpha = (u_char)(scroll_ds.alpha * alpha >> 7);           /* 985 */
    scroll_ds.r = menu_item_disp.rgb;
    scroll_ds.g = menu_item_disp.rgb;
    scroll_ds.b = menu_item_disp.rgb;                                   /* 986 */
    DispSprD(&scroll_ds);                                               /* 987 */
}

/* The seven visible rows.  A selected row gets the four-piece art, its
 * explanation window and colour 4; an unselected one the three-piece art
 * (whose middle strip is drawn rotated) and colour 3. */
static void MenuPlyrItemDisp(int off_x, int off_y, u_char alpha)        /* 999 */
{
    int       i;
    int       j;
    short     disp_num;
    u_char    col_label;
    DISP_SPRT item_ds;

    static int item_sel_on[7][4] =                          /* rdata 3bdd18 */
    {
        { 51, 53, 54, 52 },
        { 55, 57, 58, 56 },
        { 59, 61, 62, 60 },
        { 63, 65, 66, 64 },
        { 67, 69, 70, 68 },
        { 71, 73, 74, 72 },
        { 75, 77, 78, 76 },
    };

    static int item_sel_off[7][3] =                         /* rdata 3bdd88 */
    {
        { 79, 81, 80 },
        { 82, 84, 83 },
        { 85, 87, 86 },
        { 88, 90, 89 },
        { 91, 93, 92 },
        { 94, 96, 95 },
        { 97, 99, 98 },
    };

    disp_num = menu_item_ctrl.ref_ctrl.data_num;                        /* 1029 */

    if (MENU_ITEM_DISP_NUM < disp_num) {                                /* 1031 */
        disp_num = MENU_ITEM_DISP_NUM;
    }

    for (i = 0; i < disp_num; i++) {                                    /* 1038 */
        if (i == menu_wrk.cursor) {                                     /* 1042 */
            CopySprDToSpr(&item_ds, &menu_item[item_sel_on[i][0]]);     /* 1043 */
            item_ds.x += (float)off_x;   item_ds.y += (float)off_y;     /* 1044 */
            item_ds.alpha = (u_char)(item_ds.alpha * alpha >> 7);       /* 1045 */
            DispSprD(&item_ds);                                         /* 1046 */

            for (j = 0; j < 20; j++) {                                  /* 1047 */
                CopySprDToSpr(&item_ds, &menu_item[item_sel_on[i][1]]); /* 1048 */
                item_ds.x = item_ds.x + (float)off_x
                          + (float)(item_ds.w * j);
                item_ds.y += (float)off_y;                              /* 1049 */
                item_ds.alpha = (u_char)(item_ds.alpha * alpha >> 7);   /* 1050 */
                DispSprD(&item_ds);                                     /* 1051 */
            }                                                           /* 1052 */

            CopySprDToSpr(&item_ds, &menu_item[item_sel_on[i][2]]);     /* 1053 */
            item_ds.x += (float)off_x;   item_ds.y += (float)off_y;     /* 1054 */
            item_ds.alpha = (u_char)(item_ds.alpha * alpha >> 7);       /* 1055 */
            DispSprD(&item_ds);                                         /* 1056 */

            CopySprDToSpr(&item_ds, &menu_item[item_sel_on[i][3]]);     /* 1057 */
            item_ds.x += (float)off_x;   item_ds.y += (float)off_y;     /* 1058 */
            item_ds.alpha = (u_char)(item_ds.alpha * alpha >> 7);       /* 1059 */
            DispSprD(&item_ds);                                         /* 1060 */

            col_label = 4;                                              /* 1062 */

            MenuItemExplanation(off_x, off_y, alpha);                   /* 1065 */
        }
        else {
            CopySprDToSpr(&item_ds, &menu_item[item_sel_off[i][0]]);    /* 1069 */
            item_ds.x += (float)off_x;   item_ds.y += (float)off_y;     /* 1070 */
            item_ds.alpha = (u_char)(item_ds.alpha * alpha >> 7);       /* 1071 */
            DispSprD(&item_ds);                                         /* 1072 */

            /* Rotated 90 degrees about its own leading edge, so the strip
             * runs across the row rather than down it. */
            for (j = 0; j < 23; j++) {                                  /* 1073 */
                CopySprDToSpr(&item_ds, &menu_item[item_sel_off[i][1]]); /* 1074 */
                item_ds.x = item_ds.x + (float)off_x
                          + (float)(item_ds.h * j) + (float)item_ds.h;
                item_ds.y += (float)off_y;                              /* 1075 */
                item_ds.alpha = (u_char)(item_ds.alpha * alpha >> 7);   /* 1076 */
                item_ds.rot = 90.0f;
                item_ds.csx = item_ds.x;   item_ds.csy = item_ds.y;     /* 1077 */
                DispSprD(&item_ds);                                     /* 1078 */
            }                                                           /* 1079 */

            CopySprDToSpr(&item_ds, &menu_item[item_sel_off[i][2]]);    /* 1080 */
            item_ds.x += (float)off_x;   item_ds.y += (float)off_y;     /* 1081 */
            item_ds.alpha = (u_char)(item_ds.alpha * alpha >> 7);       /* 1082 */
            DispSprD(&item_ds);                                         /* 1083 */

            col_label = 3;                                              /* 1085 */
        }

        PrintMsg(0x2c,
                 disp_item[menu_item_ctrl.ref_ctrl.disp_start_pos + i].item_id,
                 off_x + 0x50, off_y + i * 35 + 0x9f,
                 col_label, alpha, 0);                                  /* 1090 */

        /* Item 0 is the unlimited film, so its count is two dashes. */
        if (disp_item[menu_item_ctrl.ref_ctrl.disp_start_pos + i].item_id == 0) { /* 1095 */
            PrintMsg(8, 3, off_x + 0x14c, off_y + i * 35 + 0xa1,
                     col_label, alpha, 0);                              /* 1097 */
            PrintMsg(8, 3, off_x + 0x158, off_y + i * 35 + 0xa1,
                     col_label, alpha, 0);                              /* 1099 */
        }
        else {
            PrintNumber_N(
                disp_item[menu_item_ctrl.ref_ctrl.disp_start_pos + i].have_num,
                2, off_x + 0x14a, off_y + i * 35 + 0xa1,
                col_label, alpha, 0, 1, 0);                             /* 1103 */
        }
    }                                                                   /* 1107 */
}

/* The explanation window under the picture: five rows of four pieces, the
 * second of which is repeated eleven times across. */
static void MenuItemExplanation(int off_x, int off_y, u_char alpha)     /* 1120 */
{
    int       i;
    int       j;
    int       k;
    DISP_SPRT item_ds;

    static int item_doc_tex[5][4] =                         /* rdata 3bdde0 */
    {
        { 31, 32, 33, 34 },
        { 35, 36, 37, 38 },
        { 39, 40, 41, 42 },
        { 43, 44, 45, 46 },
        { 47, 48, 49, 50 },
    };

    for (i = 0; i < 5; i++) {                                           /* 1135 */
        for (j = 0; j < 4; j++) {                                       /* 1136 */
            if (j == 1) {                                               /* 1137 */
                for (k = 0; k < 11; k++) {                              /* 1138 */
                    CopySprDToSpr(&item_ds, &menu_item[item_doc_tex[i][j]]); /* 1139 */
                    item_ds.x = item_ds.x + (float)(item_ds.w * k)
                              + (float)off_x;
                    item_ds.y += (float)off_y;                          /* 1140 */
                    item_ds.alpha = (u_char)(item_ds.alpha * alpha >> 7); /* 1141 */
                    DispSprD(&item_ds);                                 /* 1142 */
                }                                                       /* 1143 */
            }
            else {
                CopySprDToSpr(&item_ds, &menu_item[item_doc_tex[i][j]]); /* 1146 */
                item_ds.x += (float)off_x;   item_ds.y += (float)off_y; /* 1147 */
                item_ds.alpha = (u_char)(item_ds.alpha * alpha >> 7);   /* 1148 */
                DispSprD(&item_ds);                                     /* 1149 */
            }
        }                                                               /* 1151 */
    }                                                                   /* 1152 */

    PrintMsg(0x2b,
             disp_item[menu_item_ctrl.ref_ctrl.disp_start_pos + menu_wrk.cursor].item_id,
             off_x + 0x191, off_y + 0x120, 5, alpha, 0xa0, 0, 2);       /* 1156 */
}

/* The item picture.  Both cross-fade slots are drawn, the outgoing one first;
 * outside MAIN the fade is forced to its ends so the page's own fade takes
 * over.  menu_item[100]'s TEX0 is patched here because the picture lives in a
 * VRAM scratch page rather than in a pak. */
static void MenuItemPictureDisp(int off_x, int off_y, u_char alpha)     /* 1168 */
{
    DISP_SPRT item_ds;
    int       disp_num;
    u_char    fade_alpha[2];

    disp_num = menu_item_ctrl.ref_ctrl.data_num;                        /* 1175 */

    if (MENU_ITEM_DISP_NUM < disp_num) {                                /* 1177 */
        disp_num = MENU_ITEM_DISP_NUM;
    }

    fade_alpha[0] = 0;                                                  /* 1181 */
    fade_alpha[1] = 0;                                                  /* 1182 */

    if (disp_num != 0) {                                                /* 1186 */
        GetMenuCrossFadeAlpha(fade_alpha);                              /* 1188 */

        if (menu_wrk.step != MENU_ITEM_STEP_MAIN) {                     /* 1190 */
            fade_alpha[menu_item_ctrl.cross_fade_flg ^ 1] = 0;          /* 1191 */
        }

        if (CheckCrossFadeDisp(menu_item_ctrl.cross_fade_flg ^ 1) != 0  /* 1195 */
            && fade_alpha[menu_item_ctrl.cross_fade_flg ^ 1] != 0) {    /* 1196 */
            MenuTim2SendVram(
                (u_int *)GetCrossFadeDataAddr(menu_item_ctrl.cross_fade_flg ^ 1),
                ITEM_PICTURE_TBP, ITEM_PICTURE_CBP);                    /* 1197 */

            CopySprDToSpr(&item_ds, &menu_item[MI_PICTURE]);            /* 1199 */
            item_ds.tex0 = ITEM_PICTURE_TEX0;                           /* 1200 */
            item_ds.alpha = (u_char)(
                item_ds.alpha * fade_alpha[menu_item_ctrl.cross_fade_flg ^ 1] >> 7); /* 1202 */
            DispSprD(&item_ds);                                         /* 1203 */
        }

        if (menu_wrk.step != MENU_ITEM_STEP_MAIN) {                     /* 1207 */
            fade_alpha[menu_item_ctrl.cross_fade_flg] = alpha;          /* 1208 */
        }

        if (CheckCrossFadeDisp(menu_item_ctrl.cross_fade_flg) != 0      /* 1212 */
            && fade_alpha[menu_item_ctrl.cross_fade_flg] != 0) {        /* 1213 */
            MenuTim2SendVram(
                (u_int *)GetCrossFadeDataAddr(menu_item_ctrl.cross_fade_flg),
                ITEM_PICTURE_TBP, ITEM_PICTURE_CBP);                    /* 1214 */

            CopySprDToSpr(&item_ds, &menu_item[MI_PICTURE]);            /* 1216 */
            item_ds.tex0 = ITEM_PICTURE_TEX0;                           /* 1217 */
            item_ds.alpha = (u_char)(
                item_ds.alpha * fade_alpha[menu_item_ctrl.cross_fade_flg] >> 7); /* 1219 */
            DispSprD(&item_ds);                                         /* 1220 */
        }
    }

    PK2SendVram((uintptr_t)menu_item_tex_addr, -1, -1, 0);              /* 1226 */

    CopySprDToSpr(&item_ds, &menu_item[MI_PICTURE_PLATE]);              /* 1228 */
    item_ds.x += (float)off_x;   item_ds.y += (float)off_y;             /* 1229 */
    item_ds.alpha = (u_char)(item_ds.alpha * alpha >> 7);               /* 1230 */
    DispSprD(&item_ds);                                                 /* 1231 */
}

/* The magnifier over the picture: one sprite at 2x, blended additively. */
static void MenuItemLensDisp(int off_x, int off_y, u_char alpha)        /* 1243 */
{
    DISP_SPRT item_ds;

    PK2SendVram((uintptr_t)menu_item_tex_addr, -1, -1, 0);              /* 1248 */

    CopySprDToSpr(&item_ds, &menu_item[MI_LENS]);                       /* 1251 */
    item_ds.x += (float)off_x;   item_ds.y += (float)off_y;             /* 1252 */
    item_ds.scw = 2.0f;   item_ds.sch = 2.0f;
    item_ds.csx = item_ds.x;   item_ds.csy = item_ds.y;                 /* 1253 */
    item_ds.alpha = (u_char)(item_ds.alpha * alpha >> 7);               /* 1254 */
    item_ds.alphar = 0x48;      /* additive */                                      /* 1255 */
    DispSprD(&item_ds);                                                 /* 1256 */
}

static void MenuItemCaptionDisp(int off_x, int off_y, u_char alpha)     /* 1268 */
{
    DrawCmnCapGroup_W(3, 3, alpha, 0);                                  /* 1271 */
}

/* "You cannot use that." */
static void MenuItemConfirmDisp(int off_x, int off_y, u_char alpha)     /* 1316 */
{
    MenuCmnConfirmWinDisp(0, 0, alpha, 0x80);                           /* 1320 */

    PrintMsg(0x52, disp_item[menu_item_ctrl.ref_ctrl.data_pos].item_id,
             0x44, 0xc6, 1, alpha, 0x80);                               /* 1323 */
}

/* "Use this?"  The same window MenuCmnYesNoWinDisp() draws, written out here
 * because the cursor comes from menu_item_ctrl.conf_csr rather than from the
 * shared menu_yes_no_ctrl. */
static void MenuItemUseSelDisp(int off_x, int off_y, u_char alpha)      /* 1335 */
{
    DrawCmnWindow(0, (float)(off_x + 24), (float)(off_y + 178),
                  592.0f, 112.0f, alpha, 0x80);                         /* 1340 */

    DrawCmnSelCsr(0, (float)(off_x + 155 + menu_item_ctrl.conf_csr * 207),
                  (float)(off_y + 230), alpha, 0.0f, 0);                /* 1344 */

    DrawCmnSelYes(0, (float)(off_x + 153), (float)(off_y + 232), alpha); /* 1347 */
    DrawCmnSelNo(0, (float)(off_x + 361), (float)(off_y + 232), alpha);  /* 1348 */

    PrintMsg(0x33, disp_item[menu_item_ctrl.ref_ctrl.data_pos].item_id,
             0x44, 0xc6, 1, alpha, 0);                                  /* 1351 */
}

/* An empty inventory: the default window and its message, nothing else. */
static void MenuItemNoHaveItemDisp(int off_x, int off_y, u_char alpha)  /* 1363 */
{
    DISP_STR    msg_data;
    MSG_WIN_DAT msg_win;

    SetMsgDefData(&msg_data, 0x36);                                     /* 1369 */
    SetMsgWinDefData(&msg_win, 0x36);                                   /* 1370 */

    DrawCmnWindow(0, msg_win.x, msg_win.y, msg_win.w, msg_win.h,
                  alpha, 0x66);                                         /* 1373 */

    PrintMsg(0x36, 0, msg_data.pos_x, msg_data.pos_y, 1, alpha, 0);     /* 1377 */

    DrawCmnCapGroup_W(12, 12, alpha, 0);                                /* 1381 */
}
