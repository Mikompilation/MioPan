// FILE: /home/zero_rom/zero2np/src/ingame/menu/menu_memo.c
//
// The in-game menu's notes page (menu_memo.o).  Twenty-nine functions: six
// exports, twenty-three statics, two 3-entry dispatch tables in .data and
// three lookup tables in .rodata.
//
// The page is the standard menu shape -- a *_CTRL work block with a
// MENU_REF_CTRL inside it, a menu_wrk.step ladder, an anim_step/anim_timer
// fade, and a Get/LoadReq/LoadWait/Liberate/LoadCancel texture quintet.  What
// is its own is the sheet of paper it draws on and the paged reader that
// opens on top of it.
//
// Facts worth knowing before touching it:
//
//  * disp_memo_data[] is a *compacted* copy of the twenty memo slots, not a
//    view of them.  MenuMemoSetDispData() walks all twenty labels once and
//    copies the ones the player holds into consecutive entries, so a row is
//    disp_memo_data[disp_start_pos + i] and the list walks without
//    re-testing anything.  It is built once, on entry -- unlike the
//    inventory's, nothing the page does can change it.
//
//  * Only `memo_label` is ever read back.  DISP_MEMO_DATA::state and
//    ::msg_step are stored by MenuMemoSetDispData() and there is not one
//    load of either anywhere in the object -- every consumer calls
//    GetMemoState() / GetMemoMsgStep() live instead.  Both fields are dead
//    in this build; they are kept because the ROM writes them.
//
//  * `mode` is the page's inner state and indexes BOTH dispatch tables in
//    step: 0 the list, 1 the reader, 2 the "you have no notes" message.
//    `sub_step` is the *mode change* -- MENU_MEMO_SUB_MOVE parks the pad
//    entirely while menu_memo_disp.sub_anim_step fades the old mode out, and
//    the switch in MenuMemo() is what swaps mode over at the bottom of that
//    fade.  Both take menu_wrk.step's own numbering (2 main, 3 move).
//
//  * The page has TWO independent open/close fades.  anim_step/anim_timer is
//    the page's, and drives the paper, the title, the scattered memo titles
//    and the rule; sub_anim_step/sub_anim_timer is the mode's, and drives
//    whatever menu_memo_disp_func[mode] draws.  MenuMemoDisp() runs
//    MenuInOutAnimCtrl() once for each, in that order, and the second call
//    overwrites `alpha` -- which is why the mode's drawing rides the inner
//    fade and the paper under it does not.
//
//  * A memo owns six message ids: three per revision.  msg_id is
//    memo_label * 6 + GetMemoMsgStep(memo_label) * 3 + N, with N 0 the title,
//    1 the body and 2 the one-line caption under the list.  The ROM writes
//    that expression out at all six use sites rather than factoring it, and
//    calls GetMemoMsgStep() twice in MenuMemoContentWindowDisp() because the
//    title and the body are computed as separate statements.
//
//  * MenuMemoPlyrMemoDisp() is the page's signature: every memo the player
//    holds has its title brushed onto the paper at a fixed scattered
//    position, as one, two or three vertical tiles out of menu_memo_tex[].
//    memo_first_tbl[]/memo_second_tbl[] are those tile runs -- one table per
//    revision, -1 terminated -- and the row under the cursor is the only one
//    drawn in ink rather than black.  color_type_tbl[] picks which of the two
//    reds it gets.
//
//  * MenuMemoLineDisp() sizes the vertical rule with a real loop that adds
//    30.0f per visible row, exactly as MenuTopTitleDisp() does.  The four
//    nops in its body are the EE's FP-add latency, not a deleted statement.
//
// Reconstructed from the Feb 6 2004 prototype (SLES_523.84), menu_memo.o.
// All 6 ZERO2.MAP exports plus the 23 statics, verified 6/6 against the map
// and 34/34 against functions.txt (its 35th entry is the DISP_MEMO_DATA
// type_info node, a compiler artifact).  .text is accounted for byte-for-byte
// -- 0x1ff908..0x2011f8 = 0x18f0 = 6384 bytes: thirty-five bodies totalling
// 6304 (the twenty-nine real ones, the four fixed_array boilerplate ones and
// the empty static-init pair) plus twenty 4-byte alignment fills, with no gap
// of 8 bytes or more anywhere.  memo_first_tbl, memo_second_tbl and
// color_type_tbl are read straight out of the ROM's .rodata, and the sprite
// table is in tim_dat/menu_memo_dat.c.
//
// Trailing /* NNN */ comments are the original source line numbers, measured
// from the $LM stabs.  Two things routinely leave a statement with no line of
// its own and those annotations are interpolated into the measured gap:
//
//  * any statement whose first act is a disp_memo_data[] subscript -- the
//    inlined operator[] reports fixed_array.h 124/125 and the caller's own
//    note is dropped.  There are eleven of those;
//
//  * a store the scheduler put in a jr/jal delay slot (the last field write
//    of MenuMemoModeMoveReq(), MenuMemoOutReq(), MenuMemoDispInit() and
//    MenuMemoCtrlInit()).
//
// Two statements measure across a gap rather than on one line, because g++
// tags a sub-expression with its own closing line: MenuMemoItemDisp()'s
// PrintMsg_Arrange() spans 1035..1040 (ten arguments), and
// MenuMemoContentCaptionDisp()'s page test spans 900..904.  The intervening
// lines are not recoverable, so both carry a range rather than a number.

#include "menu_memo.h"

#include "menu.h"                               /* menu_wrk / MENU_BG_TEX_ADRS */
#include "menu_cmn.h"                           /* MenuRefMove* / anim        */
#include "menu_cmn_disp.h"                      /* MenuCmnSelFrameDisp        */
#include "tim_dat/menu_cmn_dat.h"               /* menu_cmn_dat[]             */
#include "tim_dat/menu_memo_dat.h"              /* menu_memo_tex[]            */
#include "zero2_anim2d.h"                       /* Zero2Anim2D_CsrAnimCtrl    */

#include "../item/prg/memo.h"                   /* GetMemoState / MEMO_MAX    */
#include "../../common/mem_util.h"              /* mem_utilGetMem             */
#include "../../common/utility2.h"              /* PRINT_WARNING              */
#include "../../graphics/graph2d/draw_cmn.h"    /* DrawCmnWindow              */
#include "../../graphics/graph2d/g2d_draw.h"    /* DISP_SPRT / DISP_STR       */
#include "../../graphics/graph2d/message.h"     /* PrintMsg / GetMsgPageNum   */
#include "../../graphics/graph2d/tim2.h"        /* PK2SendVram                */
#include "../../graphics/graph3d/ctl/fixed_array.h"  /* fixed_array           */
#include "../../system/eeiop/cddat.h"           /* GetFileSize / MENU_MEMO_PK2 */
#include "../../system/eeiop/fileload.h"        /* FileLoadReqEE              */
#include "../../system/eeiop/snd3d.h"           /* SND_3D_SET                 */
#include "../../system/os/system.h"             /* SystemBankPlay / GetLanguage */
#include "../../system/pad/pad.h"               /* pad / paddat               */

#include <string.h>                             /* memset                     */

/* --------------------------------------------------------------------------
 *  Constants
 * ------------------------------------------------------------------------ */

/* menu_wrk.step values, the same ladder every page uses. */
#define MENU_MEMO_STEP_INIT     0
#define MENU_MEMO_STEP_LOAD     1
#define MENU_MEMO_STEP_MAIN     2
#define MENU_MEMO_STEP_MOVE     3

/* MENU_MEMO_DISP::anim_step / sub_anim_step.  Only START, OUT and END are
 * named here; the rest of the ladder is MenuInOutAnimCtrl()'s business. */
#define MENU_MEMO_ANIM_START    0
#define MENU_MEMO_ANIM_OUT      3
#define MENU_MEMO_ANIM_END      4

/* MENU_MEMO_CTRL::mode -- the index into both dispatch tables. */
#define MENU_MEMO_MODE_SEL      0   /* the list                              */
#define MENU_MEMO_MODE_CONTENT  1   /* the paged reader                      */
#define MENU_MEMO_MODE_NOT_HAVE 2   /* "you have no notes"                   */

/* MENU_MEMO_CTRL::sub_step.  Deliberately the same two values menu_wrk.step
 * uses for the page as a whole: MAIN runs the mode, MOVE is the fade between
 * two modes and runs nothing at all. */
#define MENU_MEMO_SUB_MAIN      2
#define MENU_MEMO_SUB_MOVE      3

/* How many rows the list shows, and how many entries it can hold. */
#define MENU_MEMO_DISP_NUM      8
#define DISP_MEMO_NUM           MEMO_MAX

/* menu_ctrl[] row the page hands back to when it closes. */
#define MENU_STEP_TOP           8

/* Message ids.  A memo owns six: three per revision, and GetMemoMsgStep()
 * says which revision the player has reached. */
#define MEMO_MSG_BANK           0x30
#define MEMO_MSG_PER_MEMO       6
#define MEMO_MSG_PER_STEP       3
#define MEMO_MSG_TITLE          0   /* the name, on the row and in the window */
#define MEMO_MSG_DATA           1   /* the body, paged                        */
#define MEMO_MSG_EXP            2   /* the one-liner under the list           */

/* The shared banks: 8 is the common one the reader's page markers come from,
 * 0x36 the "you are not carrying any" message menu_item.o also uses. */
#define MSG_BANK_CMN            8
#define MSG_BANK_NO_MEMO        0x36

/* menu_memo_tex[] indices that are referenced on their own rather than as a
 * range.  See tim_dat/menu_memo_dat.c for the whole layout. */
#define MM_BG                   0    /* [0..3] the paper                     */
#define MM_BG_SIDE              4    /* [4],[5] the rails, drawn rot 270     */
#define MM_TITLE_PLATE          6    /* [6],[7] out of the MENU_BG pak       */
#define MM_TITLE                8    /* the word, out of the memo pak        */
#define MM_NON_READ             62   /* [62],[63] the unread bracket pair    */

/* The list's geometry.  The frame and the text step together but start four
 * pixels apart, and the unread bracket two more below that. */
#define MENU_MEMO_ROW_X         29.0f
#define MENU_MEMO_ROW_TOP       58.0f
#define MENU_MEMO_ROW_STEP      35.0f
#define MENU_MEMO_ROW_W         246.0f

/* One row's worth of vertical rule, added per visible row. */
#define MENU_MEMO_LINE_STEP     30.0f

/* SystemBankPlay() cue numbers, as everywhere else in the menus. */
#define SE_CURSOR   0
#define SE_CANCEL   1
#define SE_DECIDE   3
#define SE_PAGE     6

/* pad[0].rpt bits in the remapped layout, plus the analogue equivalents.
 * The reader turns pages on a single press, so it reads pad[0].one for
 * left/right where the list reads pad[0].rpt for up/down. */
#define PAD_RPT_UP              0x1000
#define PAD_RPT_DOWN            0x4000
#define PAD_ONE_RIGHT           0x2000
#define PAD_ONE_LEFT            0x8000
#define PAD_ONE_L1              0x4
#define PAD_ONE_R1              0x8
#define PAD_ANALOG_UP           0
#define PAD_ANALOG_DOWN         1
#define PAD_ANALOG_LEFT         2
#define PAD_ANALOG_RIGHT        3

/* --------------------------------------------------------------------------
 *  Work
 * ------------------------------------------------------------------------ */

static void *menu_memo_tex_addr;                            /* sdata 3f2e78 */

static void MenuMemoPad(void);
static void MenuMemoContentDispPad(void);
static void MenuMemoNotHavePad(void);

static void MenuMemoSelDisp(int off_x, int off_y, u_char alpha);
static void MenuMemoContentDisp(int off_x, int off_y, u_char alpha);
static void MenuMemoNotHaveDisp(int off_x, int off_y, u_char alpha);

/* Both indexed by MENU_MEMO_CTRL::mode, and in step with each other. */
static void (*menu_memo_pad_func[3])(void) =                 /* data 3285d8 */
{
    MenuMemoPad,
    MenuMemoContentDispPad,
    MenuMemoNotHavePad,
};

static void (*menu_memo_disp_func[3])(int, int, u_char) =    /* data 3285e8 */
{
    MenuMemoSelDisp,
    MenuMemoContentDisp,
    MenuMemoNotHaveDisp,
};

static fixed_array<DISP_MEMO_DATA, DISP_MEMO_NUM> disp_memo_data; /* bss 4b5c00 */
static MENU_MEMO_CTRL menu_memo_ctrl;                        /* bss  4b5ca0 */
static MENU_MEMO_DISP menu_memo_disp;                        /* sbss 3f4e30 */

static void MenuMemoInit(void);
static void MenuMemoCtrlInit(void);
static int  MenuMemoTexLoadWait(void);
static void MenuMemoSetDispData(void);
static void MenuMemoModeMoveReq(u_char next_mode);
static void MenuMemoOutReq(void);
static void MenuMemoDispInit(void);
static void MenuMemoBgDisp(int off_x, int off_y, u_char alpha);
static void MenuMemoTitleDisp(int off_x, int off_y, u_char alpha);
static void MenuMemoPlyrMemoDisp(int off_x, int off_y, u_char alpha);
static void MenuMemoLineDisp(int off_x, int off_y, u_char alpha);
static void MenuMemoCaptionDisp(int off_x, int off_y, u_char alpha);
static void MenuMemoContentCaptionDisp(int off_x, int off_y, u_char alpha);
static void MenuMemoMsgWindowDisp(int off_x, int off_y, u_char alpha);
static void MenuMemoItemDisp(int off_x, int off_y, u_char alpha);
static void MenuMemoNonReadFrameDisp(float y, u_char alpha);
static void MenuMemoContentWindowDisp(int memo_label, u_char alpha);

/* --------------------------------------------------------------------------
 *  Entry / texture
 * ------------------------------------------------------------------------ */

/* The pak should already be resident -- menu_top.c requests it as the hub
 * fades out -- so a null here means the load never happened and the page
 * starts one of its own rather than drawing over garbage. */
static void MenuMemoInit(void)                                          /* 178 */
{
    menu_wrk.cursor = 0;                                                /* 180 */

    MenuMemoCtrlInit();                                                 /* 184 */

    if (menu_memo_tex_addr == nullptr) {                                /* 187 */
        PRINT_WARNING("Menu Memo Tex Back reading failure\n");          /* 188 */
        MenuMemoTexLoadReq();                                           /* 189 */
    }
}

static void MenuMemoCtrlInit(void)                                      /* 199 */
{
    menu_memo_ctrl.mode      = MENU_MEMO_MODE_SEL;                      /* 202 */
    menu_memo_ctrl.next_mode = MENU_MEMO_MODE_SEL;                      /* 203 */
    menu_memo_ctrl.sub_step  = MENU_MEMO_SUB_MAIN;                      /* 204 */

    MenuRefCtrlInit(&menu_memo_ctrl.ref_ctrl, GetMemoHaveNum());        /* 206 */
}

void GetMenuMemoTexMem(void)                                            /* 214 */
{
    if (menu_memo_tex_addr != nullptr) {                                /* 217 */
        LiberateMenuMemoTexMem();                                       /* 218 */
    }

    if (menu_memo_tex_addr == nullptr) {                                /* 222 */
        menu_memo_tex_addr =
            mem_utilGetMem((int)GetFileSize(MENU_MEMO_PK2 + GetLanguage())); /* 223 */
    }
}

void MenuMemoTexLoadReq(void)                                           /* 232 */
{
    if (menu_memo_tex_addr == nullptr) {                                /* 235 */
        GetMenuMemoTexMem();                                            /* 237 */
    }

    FileLoadReqEE(MENU_MEMO_PK2 + GetLanguage(), menu_memo_tex_addr,
                  2, nullptr, nullptr);                                 /* 242 */
}

static int MenuMemoTexLoadWait(void)                                    /* 252 */
{
    if (FileLoadIsEnd2(MENU_MEMO_PK2 + GetLanguage(),
                       menu_memo_tex_addr) != 0) {                      /* 260 */
        return 1;
    }

    return 0;                                                           /* 265 */
}

/* Build the compacted list.  Called once, from MenuMemo()'s step 0 -- the
 * page cannot pick anything up, so nothing invalidates it while it is open. */
static void MenuMemoSetDispData(void)                                   /* 271 */
{
    int i;
    int count;
    int state;

    memset(&disp_memo_data, 0, sizeof(disp_memo_data));                 /* 277 */

    count = 0;                                                          /* 279 */

    for (i = 0; i < DISP_MEMO_NUM; i++) {                               /* 283 */
        state = GetMemoState(i);                                        /* 285 */

        if (state != 0) {                                               /* 288 */
            disp_memo_data[count].memo_label = i;                       /* 289 */
            disp_memo_data[count].state      = state;                   /* 290 */
            disp_memo_data[count].msg_step   = GetMemoMsgStep(i);       /* 291 */

            count++;                                                    /* 293 */
        }
    }                                                                   /* 295 */
}

/* --------------------------------------------------------------------------
 *  Per-frame
 * ------------------------------------------------------------------------ */

void MenuMemo(void)                                                     /* 307 */
{
    if (menu_wrk.step == MENU_MEMO_STEP_INIT) {                         /* 310 */
        MenuMemoInit();                                                 /* 312 */

        MenuMemoSetDispData();                                          /* 314 */

        menu_wrk.step = MENU_MEMO_STEP_LOAD;                            /* 316 */
    }

    if (menu_wrk.step == MENU_MEMO_STEP_LOAD) {                         /* 319 */
        if (MenuMemoTexLoadWait() != 0) {                               /* 321 */
            MenuMemoDispInit();                                         /* 323 */

            menu_wrk.step         = MENU_MEMO_STEP_MAIN;                /* 325 */
            menu_memo_ctrl.mode   = MENU_MEMO_MODE_SEL;                 /* 326 */

            /* Nothing collected yet: open straight on the message, with the
             * fade already settled there rather than moving to it. */
            if (menu_memo_ctrl.ref_ctrl.data_num <= 0) {                /* 329 */
                menu_memo_ctrl.mode      = MENU_MEMO_MODE_NOT_HAVE;     /* 330 */
                menu_memo_ctrl.next_mode = MENU_MEMO_MODE_NOT_HAVE;     /* 331 */
            }
        }
    }

    if (menu_wrk.step == MENU_MEMO_STEP_MAIN) {                         /* 336 */
        switch (menu_memo_ctrl.sub_step) {                              /* 337 */
        case MENU_MEMO_SUB_MAIN:                                        /* 339 */
            if (menu_memo_pad_func[menu_memo_ctrl.mode] != nullptr) {   /* 340 */
                (*menu_memo_pad_func[menu_memo_ctrl.mode])();           /* 341 */
            }
            break;                                                      /* 343 */
        case MENU_MEMO_SUB_MOVE:                                        /* 344 */
            if (menu_memo_disp.sub_anim_step == MENU_MEMO_ANIM_END) {   /* 345 */
                menu_memo_disp.sub_anim_step  = MENU_MEMO_ANIM_START;   /* 346 */
                menu_memo_disp.sub_anim_timer = 0;                      /* 347 */

                menu_memo_ctrl.mode     = menu_memo_ctrl.next_mode;     /* 349 */
                menu_memo_ctrl.sub_step = MENU_MEMO_SUB_MAIN;           /* 350 */
            }
            break;
        }
    }

    if (menu_wrk.step == MENU_MEMO_STEP_MOVE) {                         /* 356 */
        if (menu_memo_disp.anim_step == MENU_MEMO_ANIM_END) {           /* 357 */
            LiberateMenuMemoTexMem();                                   /* 359 */

            SetNextMenuStep(MENU_STEP_TOP);                             /* 362 */
        }
    }
}

/* Start the cross-fade to another mode.  The pad is parked for its duration:
 * MenuMemo()'s switch runs nothing at all while sub_step is MOVE. */
static void MenuMemoModeMoveReq(u_char next_mode)                       /* 374 */
{
    menu_memo_ctrl.next_mode = next_mode;                               /* 377 */
    menu_memo_ctrl.sub_step  = MENU_MEMO_SUB_MOVE;                      /* 378 */

    menu_memo_disp.sub_anim_step  = MENU_MEMO_ANIM_OUT;                 /* 380 */
    menu_memo_disp.sub_anim_timer = 0;                                  /* 381 */
}

static void MenuMemoPad(void)                                           /* 389 */
{
    int disp_num;
    int memo_label;

    disp_num = menu_memo_ctrl.ref_ctrl.data_num;                        /* 394 */

    if (MENU_MEMO_DISP_NUM < disp_num) {                                /* 396 */
        disp_num = MENU_MEMO_DISP_NUM;
    }

    memo_label =
        disp_memo_data[menu_memo_ctrl.ref_ctrl.data_pos].memo_label;    /* 401 */

    if ((pad[0].rpt & PAD_RPT_UP) || GetPadAnalogRpt(PAD_ANALOG_UP)) {  /* 404 */
        if (MenuRefMovePadLup(&menu_memo_ctrl.ref_ctrl, &menu_wrk.cursor,
                              disp_num, MENU_MEMO_DISP_NUM) != 0) {     /* 405 */
            SystemBankPlay(SE_CURSOR, 1, 0, 0, (SND_3D_SET *)nullptr,
                           0x3200, 0x1000);                             /* 406 */
        }
    }
    else if ((pad[0].rpt & PAD_RPT_DOWN)
             || GetPadAnalogRpt(PAD_ANALOG_DOWN)) {                     /* 410 */
        if (MenuRefMovePadLdown(&menu_memo_ctrl.ref_ctrl, &menu_wrk.cursor,
                                disp_num, MENU_MEMO_DISP_NUM) != 0) {   /* 411 */
            SystemBankPlay(SE_CURSOR, 1, 0, 0, (SND_3D_SET *)nullptr,
                           0x3200, 0x1000);                             /* 412 */
        }
    }
    else if (*paddat[0] == 1) {                                         /* 416 */
        SystemBankPlay(SE_DECIDE, 1, 0, 0, (SND_3D_SET *)nullptr,
                       0x3200, 0x1000);                                 /* 417 */

        /* Rewind the reader before it opens -- the page counter is shared
         * across every message in the build. */
        SetMsgFirstPage();                                              /* 420 */

        ReadMemo(memo_label);                                           /* 423 */

        MenuMemoModeMoveReq(MENU_MEMO_MODE_CONTENT);                    /* 425 */
    }
    else if (*paddat[1] == 1) {                                         /* 428 */
        SystemBankPlay(SE_CANCEL, 1, 0, 0, (SND_3D_SET *)nullptr,
                       0x3200, 0x1000);                                 /* 429 */

        SetMsgFirstPage();                                              /* 432 */

        MenuMemoOutReq();                                               /* 434 */
    }
    else if (pad[0].one & PAD_ONE_L1) {                                 /* 437 */
        if (MenuRefMovePageUp(&menu_memo_ctrl.ref_ctrl, &menu_wrk.cursor,
                              disp_num, MENU_MEMO_DISP_NUM) != 0) {     /* 438 */
            SystemBankPlay(SE_CURSOR, 1, 0, 0, (SND_3D_SET *)nullptr,
                           0x3200, 0x1000);                             /* 439 */
        }
    }
    else if (pad[0].one & PAD_ONE_R1) {                                 /* 443 */
        if (MenuRefMovePageDown(&menu_memo_ctrl.ref_ctrl, &menu_wrk.cursor,
                                disp_num, MENU_MEMO_DISP_NUM) != 0) {   /* 444 */
            SystemBankPlay(SE_CURSOR, 1, 0, 0, (SND_3D_SET *)nullptr,
                           0x3200, 0x1000);                             /* 445 */
        }
    }
}

/* Close the page.  Both fades are started together, so the mode's drawing
 * and the paper under it go out at the same time. */
static void MenuMemoOutReq(void)                                        /* 456 */
{
    menu_wrk.step = MENU_MEMO_STEP_MOVE;                                /* 459 */

    menu_memo_disp.anim_step      = MENU_MEMO_ANIM_OUT;                 /* 460 */
    menu_memo_disp.anim_timer     = 0;                                  /* 461 */
    menu_memo_disp.sub_anim_step  = MENU_MEMO_ANIM_OUT;                 /* 462 */
    menu_memo_disp.sub_anim_timer = 0;                                  /* 463 */
}

/* The reader's pad.  Right / R1 / CROSS turn forward and left / L1 back, both
 * wrapping round the ends; the cue only fires if the page actually moved,
 * which is what latching page_num up front is for. */
static void MenuMemoContentDispPad(void)                                /* 470 */
{
    int memo_label;
    int msg_id_data;
    int page_num;

    memo_label =
        disp_memo_data[menu_memo_ctrl.ref_ctrl.data_pos].memo_label;    /* 474 */

    msg_id_data = memo_label * MEMO_MSG_PER_MEMO
                      + GetMemoMsgStep(memo_label) * MEMO_MSG_PER_STEP
                      + MEMO_MSG_DATA;                                  /* 477 */

    page_num = GetNowMsgPageNum();                                      /* 480 */

    if ((*paddat[0] == 1) || (pad[0].one & PAD_ONE_RIGHT)
        || (pad[0].one & PAD_ONE_R1)
        || GetPadAnalogRpt(PAD_ANALOG_RIGHT)) {                         /* 484 */
        if (GetNowMsgPageNum()
            == GetMsgPageNum(MEMO_MSG_BANK, msg_id_data) - 1) {         /* 486 */
            SetMsgPage(0);                                              /* 487 */
        }
        else {
            MesSetNextPage();                                           /* 490 */
        }

        if (page_num != GetNowMsgPageNum()) {                           /* 493 */
            SystemBankPlay(SE_PAGE, 1, 0, 0, (SND_3D_SET *)nullptr,
                           0x3200, 0x1000);                             /* 494 */
        }
    }
    else if ((pad[0].one & PAD_ONE_LEFT) || (pad[0].one & PAD_ONE_L1)
             || GetPadAnalogRpt(PAD_ANALOG_LEFT)) {                     /* 498 */
        if (GetNowMsgPageNum() == 0) {                                  /* 500 */
            SetMsgPage((char)(GetMsgPageNum(MEMO_MSG_BANK, msg_id_data) - 1)); /* 501 */
        }
        else {
            MesSetBeforePage();                                         /* 504 */
        }

        if (page_num != GetNowMsgPageNum()) {                           /* 507 */
            SystemBankPlay(SE_PAGE, 1, 0, 0, (SND_3D_SET *)nullptr,
                           0x3200, 0x1000);                             /* 508 */
        }
    }
    else if (*paddat[1] == 1) {                                         /* 512 */
        SystemBankPlay(SE_CANCEL, 1, 0, 0, (SND_3D_SET *)nullptr,
                       0x3200, 0x1000);                                 /* 513 */

        MenuMemoModeMoveReq(MENU_MEMO_MODE_SEL);                        /* 515 */
    }
}

/* With nothing collected there is nowhere to go but back. */
static void MenuMemoNotHavePad(void)                                    /* 524 */
{
    if (*paddat[1] == 1) {                                              /* 528 */
        SystemBankPlay(SE_CANCEL, 1, 0, 0, (SND_3D_SET *)nullptr,
                       0x3200, 0x1000);                                 /* 529 */

        MenuMemoOutReq();                                               /* 531 */
    }
}

void LiberateMenuMemoTexMem(void)                                       /* 544 */
{
    if (menu_memo_tex_addr != nullptr) {                                /* 547 */
        mem_utilFreeMem(menu_memo_tex_addr);                            /* 548 */
        menu_memo_tex_addr = nullptr;                                   /* 549 */
    }
}

void MenuMemoTexLoadCancel(void)                                        /* 558 */
{
    if (MenuMemoTexLoadWait() == 0) {                                   /* 562 */
        FileLoadCancel2(MENU_MEMO_PK2 + GetLanguage(), menu_memo_tex_addr,
                        nullptr, nullptr);                              /* 563 */
    }
}

/* --------------------------------------------------------------------------
 *  Drawing
 * ------------------------------------------------------------------------ */

static void MenuMemoDispInit(void)                                      /* 576 */
{
    menu_memo_disp.anim_step      = MENU_MEMO_ANIM_START;               /* 579 */
    menu_memo_disp.anim_timer     = 0;                                  /* 580 */
    menu_memo_disp.rgb            = 0x40;                               /* 581 */
    menu_memo_disp.scroll_timer   = 0;                                  /* 582 */
    menu_memo_disp.sub_anim_step  = MENU_MEMO_ANIM_START;               /* 583 */
    menu_memo_disp.sub_anim_timer = 0;                                  /* 584 */
}

/* The page's two fades, in order.  The second MenuInOutAnimCtrl() call
 * overwrites `alpha`, so everything above it rides the page's fade and the
 * mode's own drawing rides the inner one. */
void MenuMemoDisp(void)                                                 /* 592 */
{
    u_char alpha;

    alpha = 0;

    if (menu_wrk.step == MENU_MEMO_STEP_MAIN
        || menu_wrk.step == MENU_MEMO_STEP_MOVE) {                      /* 599 */
        Zero2Anim2D_CsrAnimCtrl(&menu_memo_disp.scroll_timer,
                                &menu_memo_disp.rgb);                   /* 601 */

        if (menu_memo_disp.anim_step != MENU_MEMO_ANIM_END) {           /* 603 */
            MenuInOutAnimCtrl(&menu_memo_disp.anim_step,
                              &menu_memo_disp.anim_timer, &alpha);      /* 605 */

            MenuMemoBgDisp(0, 0, alpha);                                /* 608 */

            MenuMemoTitleDisp(0, 0, alpha);                             /* 611 */

            MenuMemoPlyrMemoDisp(0, 0, alpha);                          /* 614 */

            MenuMemoLineDisp(0, 0, alpha);                              /* 617 */

            MenuInOutAnimCtrl(&menu_memo_disp.sub_anim_step,
                              &menu_memo_disp.sub_anim_timer, &alpha);  /* 620 */

            if (menu_memo_disp_func[menu_memo_ctrl.mode] != nullptr     /* 622 */
                && menu_memo_disp.sub_anim_step != MENU_MEMO_ANIM_END) { /* 623 */
                (*menu_memo_disp_func[menu_memo_ctrl.mode])(0, 0, alpha); /* 624 */
            }
        }
    }
}

/* The paper: four tiles, then the two edge rails on their sides.  With
 * rot 270 about (x, y + w) the rail's own width is what steps y, the same
 * trick menu.c's shoji uses. */
static void MenuMemoBgDisp(int off_x, int off_y, u_char alpha)          /* 641 */
{
    DISP_SPRT bg_ds;
    int       i;

    PK2SendVram((uintptr_t)menu_memo_tex_addr, -1, -1, 0);                  /* 646 */

    for (i = 0; i < 4; i++) {                                           /* 649 */
        CopySprDToSpr(&bg_ds, &menu_memo_tex[MM_BG + i]);               /* 650 */

        bg_ds.x = bg_ds.x + (float)off_x;                               /* 651 */
        bg_ds.y = bg_ds.y + (float)off_y;

        bg_ds.alpha = (u_char)(bg_ds.alpha * alpha >> 7);               /* 652 */

        DispSprD(&bg_ds);                                               /* 653 */
    }                                                                   /* 654 */

    for (i = 0; i < 2; i++) {                                           /* 655 */
        CopySprDToSpr(&bg_ds, &menu_memo_tex[MM_BG_SIDE + i]);          /* 656 */

        bg_ds.x = bg_ds.x + (float)off_x;                               /* 657 */
        bg_ds.y = bg_ds.y + (float)bg_ds.w + (float)off_y;

        bg_ds.rot = 270.0f;                                             /* 658 */
        bg_ds.crx = bg_ds.x;
        bg_ds.cry = bg_ds.y;

        bg_ds.alpha = (u_char)(bg_ds.alpha * alpha >> 7);               /* 659 */

        DispSprD(&bg_ds);                                               /* 660 */
    }                                                                   /* 661 */
}

/* The header.  The plate is two mirrored halves out of the shared menu
 * background pak; the word itself is in the memo pak, so the VRAM page has to
 * be swapped between them. */
static void MenuMemoTitleDisp(int off_x, int off_y, u_char alpha)       /* 672 */
{
    DISP_SPRT title_ds;
    int       i;

    PK2SendVram(MENU_BG_TEX_ADRS, -1, -1, 0);                           /* 677 */

    for (i = 0; i < 2; i++) {                                           /* 680 */
        CopySprDToSpr(&title_ds, &menu_memo_tex[MM_TITLE_PLATE + i]);   /* 681 */

        title_ds.x = title_ds.x + (float)off_x;                         /* 682 */
        title_ds.y = title_ds.y + (float)off_y;

        title_ds.alpha = (u_char)(title_ds.alpha * alpha >> 7);         /* 683 */

        DispSprD(&title_ds);                                            /* 684 */
    }                                                                   /* 685 */

    PK2SendVram((uintptr_t)menu_memo_tex_addr, -1, -1, 0);                  /* 687 */

    CopySprDToSpr(&title_ds, &menu_memo_tex[MM_TITLE]);                 /* 690 */

    title_ds.x = title_ds.x + (float)off_x;                             /* 691 */
    title_ds.y = title_ds.y + (float)off_y;

    title_ds.alpha = (u_char)(title_ds.alpha * alpha >> 7);             /* 692 */

    DispSprD(&title_ds);                                                /* 693 */
}

/* Every memo the player holds, brushed onto the paper where it belongs.
 *
 * A title is one, two or three vertical tiles stacked down the page, and the
 * two tables are the two revisions of it -- a memo whose art does not change
 * repeats the first table's indices in the second.  -1 ends a run early,
 * which is why the inner loop breaks rather than skipping.
 *
 * Only the row under the cursor is drawn in ink; everything else is black. */
static void MenuMemoPlyrMemoDisp(int off_x, int off_y, u_char alpha)    /* 704 */
{
    static int memo_first_tbl[DISP_MEMO_NUM][3] =           /* rodata 3be288 */
    {
        {  9, -1, -1 },
        { 10, 11, -1 },
        { 12, 13, 14 },
        { 15, -1, -1 },
        { 16, 17, -1 },
        { 18, -1, -1 },
        { 19, 20, -1 },
        { 21, -1, -1 },
        { 22, -1, -1 },
        { 23, 24, 25 },
        { 26, -1, -1 },
        { 27, 28, 29 },
        { 30, -1, -1 },
        { 31, -1, -1 },
        { 32, -1, -1 },
        { 33, -1, -1 },
        { 34, 35, 36 },
        { 37, 38, -1 },
        { 39, 40, -1 },
        { 41, 42, -1 },
    };

    static int memo_second_tbl[DISP_MEMO_NUM][3] =          /* rodata 3be378 */
    {
        { 43, -1, -1 },
        { 44, 45, -1 },
        { 46, 47, 48 },
        { 15, -1, -1 },
        { 16, 17, -1 },
        { 49, -1, -1 },
        { 19, 20, -1 },
        { 21, -1, -1 },
        { 22, -1, -1 },
        { 50, 51, 52 },
        { 26, -1, -1 },
        { 53, 54, 55 },
        { 30, -1, -1 },
        { 31, -1, -1 },
        { 56, -1, -1 },
        { 33, -1, -1 },
        { 57, 58, 59 },
        { 37, 38, -1 },
        { 60, 61, -1 },
        { 41, 42, -1 },
    };

    /* Which of the two reds the selected title is brushed in. */
    static char color_type_tbl[DISP_MEMO_NUM] =             /* rodata 3be468 */
    {
        1, 0, 0, 1, 1, 0, 1, 0, 0, 1,
        1, 0, 0, 0, 1, 0, 1, 0, 0, 0,
    };

    DISP_SPRT memo_ds;
    int       i;
    int       j;
    int       tex_label;

    for (i = 0; i < menu_memo_ctrl.ref_ctrl.data_num; i++) {            /* 784 */
        for (j = 0; j < 3; j++) {                                       /* 785 */
            if (GetMemoMsgStep(disp_memo_data[i].memo_label) == 0) {    /* 787 */
                tex_label =
                    memo_first_tbl[disp_memo_data[i].memo_label][j];    /* 788 */
            }
            else {
                tex_label =
                    memo_second_tbl[disp_memo_data[i].memo_label][j];   /* 791 */
            }

            if (tex_label == -1) {                                      /* 793 */
                break;
            }

            CopySprDToSpr(&memo_ds, &menu_memo_tex[tex_label]);         /* 797 */

            memo_ds.x = memo_ds.x + (float)off_x;                       /* 798 */
            memo_ds.y = memo_ds.y + (float)off_y;

            memo_ds.alpha = (u_char)(memo_ds.alpha * alpha >> 7);       /* 799 */

            if (i == menu_memo_ctrl.ref_ctrl.data_pos) {                /* 801 */
                if (color_type_tbl[disp_memo_data[i].memo_label] != 0) { /* 802 */
                    memo_ds.r = 0x44;   memo_ds.g = 4;   memo_ds.b = 4; /* 803 */
                }
                else {
                    memo_ds.r = 0x2b;   memo_ds.g = 6;   memo_ds.b = 6; /* 806 */
                }
            }
            else {
                memo_ds.r = 0;   memo_ds.g = 0;   memo_ds.b = 0;        /* 810 */
            }

            DispSprD(&memo_ds);                                         /* 812 */
        }                                                               /* 814 */
    }                                                                   /* 815 */
}

/* The rule down the left of the list, sized from the two menu_cmn_dat cap
 * heights plus 30 pixels per visible row -- the same shape MenuTopTitleDisp()
 * uses, and written as a real loop there too. */
static void MenuMemoLineDisp(int off_x, int off_y, u_char alpha)        /* 826 */
{
    int   i;
    int   disp_num;
    float title_line_tate;

    title_line_tate = (float)(menu_cmn_dat[40].h + menu_cmn_dat[42].h); /* 832 */

    disp_num = menu_memo_ctrl.ref_ctrl.data_num;                        /* 834 */

    if (MENU_MEMO_DISP_NUM < disp_num) {                                /* 835 */
        disp_num = MENU_MEMO_DISP_NUM;
    }

    for (i = 0; i < disp_num; i++) {                                    /* 840 */
        title_line_tate += MENU_MEMO_LINE_STEP;                         /* 842 */
    }

    PK2SendVram(MENU_BG_TEX_ADRS, -1, -1, 0);                           /* 844 */

    MenuCmnLineTateDisp(22.0f, 0.0f, title_line_tate, alpha, 0xa0);     /* 848 */
}

static void MenuMemoCaptionDisp(int off_x, int off_y, u_char alpha)     /* 860 */
{
    DrawCmnCapGroup_W(3, 3, alpha, 0);                                  /* 863 */
}

/* A memo long enough to page gets the page-turn caption; a one-page one gets
 * the plain close-only group. */
static void MenuMemoContentCaptionDisp(int off_x, int off_y, u_char alpha) /* 894 */
{
    int memo_label;

    memo_label =
        disp_memo_data[menu_memo_ctrl.ref_ctrl.data_pos].memo_label;    /* 897 */

    if (GetMsgPageNum(MEMO_MSG_BANK,
                      memo_label * MEMO_MSG_PER_MEMO
                          + GetMemoMsgStep(memo_label) * MEMO_MSG_PER_STEP
                          + MEMO_MSG_DATA) > 1) {                       /* 900-904 */
        DrawCmnCapGroup_W(9, 9, alpha, 0);                              /* 907 */
    }
    else {
        DrawCmnCapGroup_W(12, 12, alpha, 0);                            /* 929 */
    }
}

/* The strip under the list that the one-line caption is printed into. */
static void MenuMemoMsgWindowDisp(int off_x, int off_y, u_char alpha)   /* 947 */
{
    DrawCmnWindow(0, (float)(off_x + 24), (float)(off_y + 346),
                  592.0f, 100.0f, alpha, 0x66);                         /* 951 */
}

static void MenuMemoSelDisp(int off_x, int off_y, u_char alpha)         /* 966 */
{
    int memo_label;
    int msg_id_exp;

    memo_label =
        disp_memo_data[menu_memo_ctrl.ref_ctrl.data_pos].memo_label;    /* 969 */

    msg_id_exp = memo_label * MEMO_MSG_PER_MEMO
                     + GetMemoMsgStep(memo_label) * MEMO_MSG_PER_STEP
                     + MEMO_MSG_EXP;                                    /* 972 */

    MenuMemoItemDisp(off_x, off_y, alpha);                              /* 976 */

    MenuMemoMsgWindowDisp(off_x, off_y, alpha);                         /* 979 */

    PrintMsg(MEMO_MSG_BANK, msg_id_exp, 48, 370, 1, alpha, 0);          /* 983 */

    MenuMemoCaptionDisp(off_x, off_y, alpha);                           /* 986 */
}

/* The list itself: a frame per row, the memo's title printed into it, and an
 * unread bracket on any row still in MEMO_STATE_HAVE. */
static void MenuMemoItemDisp(int off_x, int off_y, u_char alpha)        /* 997 */
{
    int i;
    int memo_label;
    int col_label;
    int disp_num;

    disp_num = menu_memo_ctrl.ref_ctrl.data_num;                        /* 1009 */

    if (MENU_MEMO_DISP_NUM < disp_num) {                                /* 1011 */
        disp_num = MENU_MEMO_DISP_NUM;
    }

    for (i = 0; i < disp_num; i++) {                                    /* 1016 */
        memo_label =
            disp_memo_data[menu_memo_ctrl.ref_ctrl.disp_start_pos + i]
                .memo_label;                                            /* 1017 */

        if (i == menu_wrk.cursor) {                                     /* 1019 */
            MenuCmnSelFrameDisp((float)off_x + MENU_MEMO_ROW_X,
                                (float)i * MENU_MEMO_ROW_STEP
                                    + MENU_MEMO_ROW_TOP + (float)off_y,
                                MENU_MEMO_ROW_W, alpha, 0);             /* 1023 */

            col_label = 0;                                              /* 1024 */
        }
        else {
            MenuCmnNonSelFrameDisp((float)off_x + MENU_MEMO_ROW_X,
                                   (float)i * MENU_MEMO_ROW_STEP
                                       + MENU_MEMO_ROW_TOP + (float)off_y,
                                   MENU_MEMO_ROW_W, alpha, 0);          /* 1030 */

            col_label = 3;                                              /* 1032 */
        }

        PrintMsg_Arrange(MEMO_MSG_BANK,
                         memo_label * MEMO_MSG_PER_MEMO
                             + GetMemoMsgStep(memo_label) * MEMO_MSG_PER_STEP
                             + MEMO_MSG_TITLE,                          /* 1035 */
                         off_x + 146, i * 35 + 62 + off_y,
                         col_label, alpha, 0, 0, 0, 2);                 /* 1040 */

        if (GetMemoState(memo_label) == MEMO_STATE_HAVE) {              /* 1043 */
            MenuMemoNonReadFrameDisp((float)i * MENU_MEMO_ROW_STEP + 60.0f,
                                     alpha);                            /* 1044 */
        }
    }                                                                   /* 1046 */
}

/* The unread marker: one bracket and its mirror, both taking their x from
 * the table and only their y from the row. */
static void MenuMemoNonReadFrameDisp(float y, u_char alpha)             /* 1056 */
{
    DISP_SPRT frame_ds;

    CopySprDToSpr(&frame_ds, &menu_memo_tex[MM_NON_READ]);              /* 1062 */
    frame_ds.y = y;                                                     /* 1063 */
    frame_ds.alpha = (u_char)(frame_ds.alpha * alpha >> 7);             /* 1064 */
    DispSprD(&frame_ds);                                                /* 1065 */

    CopySprDToSpr(&frame_ds, &menu_memo_tex[MM_NON_READ + 1]);          /* 1068 */
    frame_ds.y = y;                                                     /* 1069 */
    frame_ds.alpha = (u_char)(frame_ds.alpha * alpha >> 7);             /* 1070 */
    DispSprD(&frame_ds);                                                /* 1071 */
}

static void MenuMemoContentDisp(int off_x, int off_y, u_char alpha)     /* 1086 */
{
    MenuMemoContentWindowDisp(
        disp_memo_data[menu_memo_ctrl.ref_ctrl.data_pos].memo_label,
        alpha);                                                         /* 1094 */

    MenuMemoContentCaptionDisp(off_x, off_y, alpha);                    /* 1097 */
}

/* The reader.  The title is centred by measuring its first line and putting a
 * bracket glyph either side of it; the body is paged and the page counter
 * printed under it.
 *
 * GetMemoMsgStep() is called twice -- the title and the body are separate
 * statements and it is an ordinary call, so nothing merges them. */
static void MenuMemoContentWindowDisp(int memo_label, u_char alpha)     /* 1106 */
{
    int msg_length;
    int msg_id_title;
    int msg_id_data;

    msg_id_title = memo_label * MEMO_MSG_PER_MEMO
                       + GetMemoMsgStep(memo_label) * MEMO_MSG_PER_STEP
                       + MEMO_MSG_TITLE;                                /* 1112 */
    msg_id_data  = memo_label * MEMO_MSG_PER_MEMO
                       + GetMemoMsgStep(memo_label) * MEMO_MSG_PER_STEP
                       + MEMO_MSG_DATA;                                 /* 1113 */

    msg_length = GetMsgLineLength(GetMsgDataAddr(MEMO_MSG_BANK, msg_id_title),
                                  nullptr);                             /* 1116 */

    DrawCmnWindow(0, 36.0f, 57.0f, 572.0f, 373.0f, alpha, 0x66);        /* 1121 */

    PrintMsg(MSG_BANK_CMN, 3, 306 - msg_length / 2, 85, 1, alpha, 0);   /* 1125 */
    PrintMsg(MSG_BANK_CMN, 3, msg_length / 2 + 320, 85, 1, alpha, 0);   /* 1128 */

    PrintMsg_Arrange(MEMO_MSG_BANK, msg_id_title, 320, 85, 1, alpha,
                     0, 0, 0, 2);                                       /* 1132 */

    PrintMsg_P(MEMO_MSG_BANK, msg_id_data, 80, 123, 1, alpha, 0, 0, 0); /* 1136 */

    /* The page readout: "<n> / <total>", the slash out of the common bank. */
    PrintMsg(MSG_BANK_CMN, 3, 280, 372, 1, alpha, 0);                   /* 1140 */
    PrintMsg(MSG_BANK_CMN, 1, 312, 373, 1, alpha, 0);                   /* 1143 */
    PrintMsg(MSG_BANK_CMN, 3, 344, 372, 1, alpha, 0);                   /* 1146 */

    PrintNumber_N(GetNowMsgPageNum() + 1, 1, 294, 373, 1, alpha, 0, 1, 1); /* 1150 */
    PrintNumber_N(GetMsgPageNum(MEMO_MSG_BANK, msg_id_data), 1, 326, 373,
                  1, alpha, 0, 1, 1);                                   /* 1154 */
}

/* Nothing collected.  The window's own geometry and the message's own
 * position both come out of the message record, so this is the same five
 * calls menu_item.o's empty-inventory page makes. */
static void MenuMemoNotHaveDisp(int off_x, int off_y, u_char alpha)     /* 1169 */
{
    DISP_STR    msg_data;
    MSG_WIN_DAT msg_win;

    SetMsgDefData(&msg_data, MSG_BANK_NO_MEMO);                         /* 1175 */
    SetMsgWinDefData(&msg_win, MSG_BANK_NO_MEMO);                       /* 1176 */

    DrawCmnWindow(0, msg_win.x, msg_win.y, msg_win.w, msg_win.h,
                  alpha, 0x66);                                         /* 1179 */

    PrintMsg(MSG_BANK_NO_MEMO, 3, msg_data.pos_x, msg_data.pos_y,
             1, alpha, 0);                                              /* 1183 */

    DrawCmnCapGroup_W(12, 12, alpha, 0);                                /* 1187 */
}
