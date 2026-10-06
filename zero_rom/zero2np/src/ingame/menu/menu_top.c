// FILE: /home/zero_rom/zero2np/src/ingame/menu/menu_top.c
//
// The in-game menu's hub: the list of pages down the left, the play-data
// panel down the right, and the chapter-title plate along the far edge.
//
// The list is eight fixed rows, but not all eight are always there.
// CheckMenuCondition() answers "is this row available", and every part of the
// page walks all eight and skips the ones it says no to -- the cursor
// (MenuTopPad), the row count that sizes the vertical rule (MenuTopTitleDisp),
// and both drawing passes (MenuTopSelectDisp).  That is why the second of
// those passes keeps a `disp_menu_cnt` separate from the loop index: rows are
// drawn 35 pixels apart in *display* order, not table order.
//
// Four of the eight are conditional: the camera and radio pages need the item
// in hand, the file and memo pages are hidden in Mission Mode, and the ghost
// list only appears once something has been cleared.  Map, item and photo are
// always there, and so is anything outside 2..7 -- CheckMenuCondition()'s
// switch has no case for those and its default answers yes.
//
// MenuTopAnimCtrl() is the whole page's alpha, and `move_flg` is what picks
// between the two pairs of curves: leaving *to another page* fades out over 5
// frames and back in over 10, leaving *to the game* over 8 and 8.  The flag is
// set by whichever exit ran (CROSS sets it, TRIANGLE clears it) and survives
// into the next MenuTopInit(), which is what makes the return fade match.
//
// Reconstructed from the Feb 6 2004 prototype (SLES_523.84), menu_top.o.
//
// Trailing /* NNN */ comments are the original source line numbers, measured
// from the $LM stabs.  Six are interpolated rather than measured -- 462, 468,
// 474, 480, 486 and 1377 -- five of them the identical `res = 1` bodies GCC
// cross-jumped into one store, the sixth a delay-slot store.
//
// Two functions carry no line of their own where you would expect one:
// CheckMenuSoulCondition()'s whole body is attributed to variable.h 167 (the
// inlined CVariable::Get()) and DebugMenuTopDebugSwitchPad()'s clear-count
// write to variable.h 39/49 (CVariable::Set()).  See the notes at each.

#include "menu_top.h"

#include "menu.h"                               /* menu_wrk / SetNextMenuStep */
#include "menu_cmn.h"                           /* MENU_BG_TEX_ADRS       */
#include "menu_cmn_disp.h"                      /* MenuCmnSelFrameDisp    */
#include "menu_file.h"                          /* MenuFileTexBackGroundLoad */
#include "menu_item.h"                          /* GetMenuItemTexMem      */
#include "menu_memo.h"                          /* GetMenuMemoTexMem      */
#include "menu_photo.h"                         /* GetMenuPhotoTexMem     */
#include "menu_radio.h"                         /* GetMenuRadioTexMem     */
#include "menu_soul.h"                          /* GetMenuSoulTexMem      */
#include "anim_2d.h"                            /* Anim2D_CalcNowAlpha    */
#include "play_data.h"                          /* GetPlayTime            */
#include "plyr_room_info.h"                     /* DebugAllMapDisp        */
#include "tim_dat/menu_cmn_dat.h"               /* menu_cmn_dat           */
#include "tim_dat/menu_top_dat.h"               /* menu_top               */

#include "../ingame.h"                          /* CheckIngameMission     */
#include "../clear/prg/clear_flg.h"             /* DebugAllClearFlgUp     */
#include "../item/prg/crystal.h"                /* DebugAllCrystalGet     */
#include "../item/prg/file.h"                   /* DebugAllFileGet        */
#include "../item/prg/item.h"                   /* GetPlyrItemHaveNum     */
#include "../item/prg/level_gem.h"              /* DebugSetLevelGemMaxNum */
#include "../item/prg/memo.h"                   /* DebugAllFirstMemoGet   */
#include "../item/prg/soul_list.h"              /* DebugGetAllSoulList    */
#include "../../common/mem_util.h"              /* mem_utilGetMem         */
#include "../../common/utility2.h"              /* PRINT_ASSERT           */
#include "../../common/variable.h"              /* ingame_wrk / DATE_INFO */
#include "../../graphics/graph2d/draw_cmn.h"    /* DrawCmnWindow          */
#include "../../graphics/graph2d/g2d_draw.h"    /* DISP_SPRT              */
#include "../../graphics/graph2d/message.h"     /* PrintMsg / PrintNumber_N */
#include "../../graphics/graph2d/tim2.h"        /* PK2SendVram            */
#include "../../system/eeiop/cddat.h"           /* GetFileSize / pak ids  */
#include "../../system/eeiop/fileload.h"        /* FileLoadReqEE          */
#include "../../system/os/system.h"             /* SystemBankPlay / GetLanguage */
#include "../../system/pad/pad.h"               /* pad / paddat           */

#include <string.h>                             /* memset                 */

/* --------------------------------------------------------------------------
 *  Constants
 * ------------------------------------------------------------------------ */

/* Rows in the list.  The cursor walks 0..7 with a bounded retry, so this is
 * both the row count and the retry limit. */
#define MENU_TOP_CSR_NUM        8

/* Pixels between two *displayed* rows, and where the first one sits. */
#define MENU_TOP_ROW_PITCH      35.0f
#define MENU_TOP_ROW_TOP        68.0f
#define MENU_TOP_ROW_X          32.0f
#define MENU_TOP_ROW_W          164.0f

/* Frames each of the four page fades takes. */
#define MENU_TOP_IN_TIME        8
#define MENU_TOP_MOVE_IN_TIME   10
#define MENU_TOP_OUT_TIME       8
#define MENU_TOP_MOVE_OUT_TIME  5

/* Frames between clock re-reads, per video mode -- half a second either way. */
#define MENU_TOP_CLOCK_PAL      25
#define MENU_TOP_CLOCK_NTSC     30

/* Item ids the camera and radio rows are gated on. */
#define ITEM_CAMERA             10
#define ITEM_RADIO              50

/* SystemBankPlay() cue numbers, as everywhere else in the menus. */
#define SE_CURSOR   0
#define SE_CANCEL   1
#define SE_DECIDE   3

/* pad[0].rpt bits in the remapped layout, plus the analogue equivalents. */
#define PAD_RPT_UP              0x1000
#define PAD_RPT_DOWN            0x4000
#define PAD_ANALOG_UP           0
#define PAD_ANALOG_DOWN         1

/* The debug switches live on controller 2: hold pad[1].now bit 0x100 and
 * press one of the pad[1].one bits below. */
#define DBG_PAD                 1
#define DBG_HOLD                0x100

/* menu_top[] indices this file draws.  35..67 are the eleven chapter-title
 * groups of three; 65..67 doubles as the Mission Mode group, since the
 * mission plate is loaded into the eleventh chapter's slots. */
#define MT_TOP_PLATE            32  /* [32],[33] the two header plates       */
#define MT_TOP_TITLE            31  /* the "MENU" word, from the play pak    */
#define MT_PLAYDATA_PLATE       12  /* [12],[13] the play-data panel         */
#define MT_CHAPTER_MISSION      65  /* [65..67] the Mission Mode plate       */

/* The vertical rule beside the list starts this tall and grows a row's worth
 * per available row. */
#define MENU_TOP_LINE_STEP      30.0f

static void   MenuTopInit(void);
static int    MenuChapterTitleTexLoadWait(void);
static void   MenuTopPad(void);
static void   MenuTopBackGroundLoadStart(void);
static int    CheckMenuCondition(int menu_label);
static int    CheckMenuCameraCondition(void);
static int    CheckMenuFileCondition(void);
static int    CheckMenuMemoCondition(void);
static int    CheckMenuRadioCondition(void);
static int    CheckMenuSoulCondition(void);
static int    GetMenuChapterTitleTexLabel(int chapter_no);
static void   MenuTopDispInit(void);
static u_char MenuTopAnimCtrl(void);
static void   MenuTopTitleDisp(int off_x, int off_y, u_char alpha);
static void   MenuTopSelectDisp(int off_x, int off_y, u_char alpha);
static void   MenuTopPlayDataDisp(int off_x, int off_y, u_char alpha);
static void   MenuTopPlayTimeDisp(int off_x, int off_y, u_char alpha);
static void   MenuTopScoreDisp(int off_x, int off_y, u_char alpha);
static void   MenuTopPhotoNumDisp(int off_x, int off_y, u_char alpha);
static void   MenuTopBusterNumDisp(int off_x, int off_y, u_char alpha);
static void   MenuTopMaxScoreDisp(int off_x, int off_y, u_char alpha);
static void   MenuTopNowDateDisp(int off_x, int off_y, u_char alpha);
static void   MenuTopPlyrDataDisp(int off_x, int off_y, u_char alpha);
static void   MenuTopCaptionDisp(int off_x, int off_y, u_char alpha);
static void   MenuTopChapterTitleDisp(int off_x, int off_y, u_char alpha);
static void   MenuTopWindowDisp(int off_x, int off_y, u_char alpha);
static void   DebugMenuTopDebugSwitchPad(void);

static void         *chapter_title_tex_addr;                /* sdata 3f2fb8 */
static MENU_TOP_DISP menu_top_disp;                         /* bss   4b63d8 */

/* ==========================================================================
 *  Set-up
 * ======================================================================== */

/* The cursor comes back from `top_cursor`, which MenuTopPad() latched on the
 * way out -- so returning from a page puts it back where it was. */
static void MenuTopInit(void)                                           /* 166 */
{
    menu_wrk.cursor = menu_wrk.top_cursor;                              /* 169 */

    menu_top_disp.move_flg = 0;                                         /* 171 */

    MenuTopDispInit();                                                  /* 174 */
}

/* ==========================================================================
 *  The chapter-title plate
 *
 *  One pak per chapter, five languages apiece, plus a fixed one for Mission
 *  Mode.  All three helpers open with the same "which file is it" question,
 *  which is why chapter_tex is spelled out three times rather than shared.
 * ======================================================================== */

void GetMenuChapterTitleTexMem(void)                                    /* 182 */
{
    int chapter_tex = MENU_CHP_MSN_PK2;

    if (CheckIngameMission() == 0) {                                    /* 187 */
        chapter_tex = GetMenuChapterTitleTexLabel(ingame_wrk.mChapterNo.Get()); /* 192 */
    }

    if (chapter_title_tex_addr != nullptr) {                            /* 195 */
        LiberateMenuChapterTitleTexMem();                               /* 196 */
    }

    if (chapter_title_tex_addr == nullptr) {                            /* 200 */
        chapter_title_tex_addr =
            mem_utilGetMem((int)GetFileSize(chapter_tex + GetLanguage())); /* 201 */
    }
}

void MenuChapterTitleTexLoadReq(void)                                   /* 210 */
{
    int chapter_tex = MENU_CHP_MSN_PK2;

    if (CheckIngameMission() == 0) {                                    /* 215 */
        chapter_tex = GetMenuChapterTitleTexLabel(ingame_wrk.mChapterNo.Get()); /* 220 */
    }

    if (chapter_title_tex_addr == nullptr) {                            /* 224 */
        GetMenuChapterTitleTexMem();                                    /* 226 */
    }

    FileLoadReqEE(chapter_tex + GetLanguage(), chapter_title_tex_addr,
                  2, nullptr, nullptr);                                 /* 231 */
}

static int MenuChapterTitleTexLoadWait(void)                            /* 241 */
{
    int chapter_tex = MENU_CHP_MSN_PK2;

    if (CheckIngameMission() == 0) {                                    /* 249 */
        chapter_tex = GetMenuChapterTitleTexLabel(ingame_wrk.mChapterNo.Get()); /* 254 */
    }

    if (FileLoadIsEnd2(chapter_tex + GetLanguage(),
                       chapter_title_tex_addr) == 0) {                  /* 259 */
        return 0;
    }

    return 1;                                                           /* 264 */
}

void LiberateMenuChapterTitleTexMem(void)                               /* 626 */
{
    if (chapter_title_tex_addr != nullptr) {                            /* 629 */
        mem_utilFreeMem(chapter_title_tex_addr);                        /* 630 */
        chapter_title_tex_addr = nullptr;                               /* 631 */
    }
}

/* The eleven chapter paks are spaced five apart, one slot per language. */
static int GetMenuChapterTitleTexLabel(int chapter_no)                  /* 646 */
{
    static const int chapter_data[11] =                     /* rdata 3bedc8 */
    {
        MENU_CHP1_PK2,  MENU_CHP2_PK2,  MENU_CHP3_PK2,  MENU_CHP4_PK2,
        MENU_CHP5_PK2,  MENU_CHP6_PK2,  MENU_CHP7_PK2,  MENU_CHP8_PK2,
        MENU_CHP9_PK2,  MENU_CHP10_PK2, MENU_CHP11_PK2,
    };

    return chapter_data[chapter_no];                                    /* 664 */
}

/* ==========================================================================
 *  The frame loop
 * ======================================================================== */

void MenuTop(void)                                                      /* 274 */
{
    if (menu_wrk.step == MENU_TOP_STEP_INIT) {                          /* 277 */
        MenuTopInit();                                                  /* 278 */
        menu_wrk.step = MENU_TOP_STEP_LOAD;                             /* 279 */
    }

    if (menu_wrk.step == MENU_TOP_STEP_LOAD) {                          /* 282 */
        if (MenuChapterTitleTexLoadWait() != 0) {                       /* 284 */
            menu_wrk.step = MENU_TOP_STEP_MAIN;                         /* 285 */
        }
    }

    if (menu_wrk.step == MENU_TOP_STEP_MAIN) {                          /* 290 */
        MenuTopPad();                                                   /* 292 */

        DebugMenuTopDebugSwitchPad();                                   /* 296 */
    }

    /* Only hand over once the page has finished fading out, so the incoming
     * page's own fade starts from black rather than over this one. */
    if (menu_wrk.step == MENU_TOP_STEP_MOVE) {                          /* 300 */
        if (menu_top_disp.anim_step == MENU_TOP_ANIM_END) {             /* 301 */
            SetNextMenuStep(menu_wrk.cursor);                           /* 303 */
        }
    }
}

/* One frame of the hub's pad.  UP and DOWN walk to the next *available* row,
 * giving up after a full lap; CROSS commits and starts the incoming page's
 * background load; TRIANGLE leaves the menu.
 *
 * The three anim_step guards are the ROM's own, repeated per arm -- TRIANGLE
 * deliberately has none, so the menu can be cancelled mid-fade. */
static void MenuTopPad(void)                                            /* 314 */
{
    int i;

    if ((pad[0].rpt & PAD_RPT_UP) || GetPadAnalogRpt(PAD_ANALOG_UP)) {  /* 320 */
        if (menu_top_disp.anim_step != MENU_TOP_ANIM_SHOW) {            /* 321 */
            return;
        }

        SystemBankPlay(SE_CURSOR, 1, 0, 0, (SND_3D_SET *)nullptr,
                       0x3200, 0x1000);                                 /* 322 */

        for (i = 0; i < MENU_TOP_CSR_NUM; i++) {                        /* 324 */
            menu_wrk.cursor = (menu_wrk.cursor + MENU_TOP_CSR_NUM - 1)
                              % MENU_TOP_CSR_NUM;                       /* 325 */

            if (CheckMenuCondition(menu_wrk.cursor) != 0) {             /* 328 */
                break;                                                  /* 329 */
            }
        }                                                               /* 331 */
    }
    else if ((pad[0].rpt & PAD_RPT_DOWN)
             || GetPadAnalogRpt(PAD_ANALOG_DOWN)) {                     /* 335 */
        if (menu_top_disp.anim_step != MENU_TOP_ANIM_SHOW) {            /* 336 */
            return;
        }

        SystemBankPlay(SE_CURSOR, 1, 0, 0, (SND_3D_SET *)nullptr,
                       0x3200, 0x1000);                                 /* 337 */

        for (i = 0; i < MENU_TOP_CSR_NUM; i++) {                        /* 339 */
            menu_wrk.cursor = (menu_wrk.cursor + 1) % MENU_TOP_CSR_NUM; /* 340 */

            if (CheckMenuCondition(menu_wrk.cursor) != 0) {             /* 343 */
                break;                                                  /* 344 */
            }
        }                                                               /* 346 */
    }
    else if (*paddat[0] == 1) {                                         /* 350 */
        if (menu_top_disp.anim_step != MENU_TOP_ANIM_SHOW) {            /* 351 */
            return;
        }

        SystemBankPlay(SE_DECIDE, 1, 0, 0, (SND_3D_SET *)nullptr,
                       0x3200, 0x1000);                                 /* 352 */

        menu_wrk.top_cursor = (u_char)menu_wrk.cursor;                  /* 354 */

        menu_top_disp.anim_step = MENU_TOP_ANIM_OUT;                    /* 356 */
        menu_top_disp.anim_timer = 0;                                   /* 357 */
        menu_top_disp.move_flg = 1;                                     /* 358 */
        menu_wrk.step = MENU_TOP_STEP_MOVE;                             /* 359 */

        MenuTopBackGroundLoadStart();                                   /* 362 */
    }
    else if (*paddat[1] == 1) {                                         /* 366 */
        SystemBankPlay(SE_CANCEL, 1, 0, 0, (SND_3D_SET *)nullptr,
                       0x3200, 0x1000);                                 /* 367 */

        menu_top_disp.anim_step = MENU_TOP_ANIM_OUT;                    /* 369 */
        menu_top_disp.anim_timer = 0;                                   /* 370 */
        menu_top_disp.move_flg = 0;                                     /* 371 */
        menu_wrk.step = MENU_TOP_STEP_EXIT;                             /* 372 */

        MenuOutReq();                                                   /* 375 */
    }
}

/* Start the incoming page's background load while this one fades out.
 *
 * There is no case for the camera page (cursor 2): its background belongs to
 * menu_cam_main.o and menu.c's MenuTexBackGroundLoad() has already claimed it
 * for the whole visit.  The jump table's slot for it points at the break
 * label, which is exactly where the out-of-range branch goes, so the ROM has
 * no `case 2` and no `default` either.  The map page (0) is outside the table
 * altogether. */
static void MenuTopBackGroundLoadStart(void)                            /* 398 */
{
    switch (menu_wrk.cursor)                                            /* 401 */
    {
    case MENU_STEP_ITEM:
        GetMenuItemTexMem();                                            /* 404 */
        MenuItemTexLoadReq();                                           /* 406 */
        break;

    case MENU_STEP_PHOTO:
        GetMenuPhotoTexMem();                                           /* 410 */
        MenuPhotoTexLoadReq();                                          /* 412 */
        break;

    case MENU_STEP_FILE:
        MenuFileTexBackGroundLoad();                                    /* 416 */
        break;

    case MENU_STEP_MEMO:
        GetMenuMemoTexMem();                                            /* 420 */
        MenuMemoTexLoadReq();                                           /* 422 */
        break;

    case MENU_STEP_RADIO:
        GetMenuRadioTexMem();                                           /* 426 */
        MenuRadioTexLoadReq();                                          /* 428 */
        break;

    case MENU_STEP_SOUL:
        GetMenuSoulTexMem();                                            /* 432 */
        MenuSoulTexLoadReq();                                           /* 434 */
        break;
    }
}                                                                       /* 435 */

/* ==========================================================================
 *  Which rows are available
 * ======================================================================== */

/* Anything the switch has no case for -- map (0), item (1), photo (3) and
 * everything past the list -- takes the default and is always available.
 * GCC cross-jumped all six `res = 1` bodies into one store, which is why only
 * the default's line survives. */
static int CheckMenuCondition(int menu_label)                           /* 451 */
{
    int res = 0;

    switch (menu_label)                                                 /* 458 */
    {
    case MENU_STEP_CAM:
        if (CheckMenuCameraCondition() != 0) {                          /* 461 */
            res = 1;                                                    /* 462 */
        }
        break;                                                          /* 464 */

    case MENU_STEP_FILE:
        if (CheckMenuFileCondition() != 0) {                            /* 467 */
            res = 1;                                                    /* 468 */
        }
        break;                                                          /* 470 */

    case MENU_STEP_MEMO:
        if (CheckMenuMemoCondition() != 0) {                            /* 473 */
            res = 1;                                                    /* 474 */
        }
        break;                                                          /* 476 */

    case MENU_STEP_RADIO:
        if (CheckMenuRadioCondition() != 0) {                           /* 479 */
            res = 1;                                                    /* 480 */
        }
        break;                                                          /* 482 */

    case MENU_STEP_SOUL:
        if (CheckMenuSoulCondition() != 0) {                            /* 485 */
            res = 1;                                                    /* 486 */
        }
        break;

    default:
        res = 1;                                                        /* 491 */
        break;
    }

    return res;                                                         /* 495 */
}

static int CheckMenuCameraCondition(void)                               /* 503 */
{
    if (GetPlyrItemHaveNum(ITEM_CAMERA) <= 0) {                         /* 511 */
        return 0;
    }

    return 1;                                                           /* 519 */
}

/* The file and memo pages are story-only; Mission Mode hides both. */
static int CheckMenuFileCondition(void)                                 /* 528 */
{
    if (CheckIngameMission() != 0) {                                    /* 536 */
        return 0;
    }

    return 1;                                                           /* 541 */
}

static int CheckMenuMemoCondition(void)                                 /* 549 */
{
    if (CheckIngameMission() != 0) {                                    /* 557 */
        return 0;
    }

    return 1;                                                           /* 562 */
}

static int CheckMenuRadioCondition(void)                                /* 570 */
{
    if (GetPlyrItemHaveNum(ITEM_RADIO) <= 0) {                          /* 578 */
        return 0;
    }

    return 1;                                                           /* 586 */
}

/* The ghost list appears once the game has been cleared at least once.
 *
 * The whole body is attributed to variable.h 167 -- the inlined
 * CVariable::Get() moved the line and the rest of the statement got no fresh
 * note, so the test's own line is not recoverable.  Lines 596..614 hold no
 * code at all. */
static int CheckMenuSoulCondition(void)                                 /* 595 */
{
    if (ingame_wrk.mClearCnt.Get() <= 0) {
        return 0;
    }

    return 1;                                                           /* 615 */
}

/* ==========================================================================
 *  Drawing
 * ======================================================================== */

/* move_flg is deliberately not cleared here -- MenuTopInit() owns it, so the
 * flag the exit set survives into the return fade. */
static void MenuTopDispInit(void)                                       /* 674 */
{
    menu_top_disp.anim_step = MENU_TOP_ANIM_START;                      /* 677 */
    menu_top_disp.anim_timer = 0;                                       /* 678 */
    menu_top_disp.now_time_cnt = 0;                                     /* 679 */
    memset(&menu_top_disp.now_time, 0, sizeof(DATE_INFO));              /* 680 */
}

void MenuTopDisp(void)                                                  /* 688 */
{
    u_char alpha;

    if (menu_wrk.step == MENU_TOP_STEP_MAIN
        || menu_wrk.step == MENU_TOP_STEP_EXIT
        || menu_wrk.step == MENU_TOP_STEP_MOVE) {                       /* 695 */
        alpha = MenuTopAnimCtrl();                                      /* 700 */

        /* AnimCtrl still has to run on the frame the fade lands, to move the
         * step on; only the drawing stops. */
        if (menu_top_disp.anim_step != MENU_TOP_ANIM_END) {             /* 702 */
            MenuTopTitleDisp(0, 0, alpha);                              /* 704 */

            MenuTopSelectDisp(0, 0, alpha);                             /* 706 */

            MenuTopPlayDataDisp(0, 0, alpha);                           /* 708 */

            MenuTopPlyrDataDisp(0, 0, alpha);                           /* 710 */

            MenuTopCaptionDisp(0, 0, alpha);                            /* 712 */

            MenuTopChapterTitleDisp(0, 0, alpha);                       /* 714 */

            MenuTopWindowDisp(0, 0, alpha);                             /* 717 */
        }
    }
}

/* The page's alpha, and the step machine behind it.
 *
 * anim_step 2 has an arm of its own with an empty body: the page is up and
 * alpha simply stays at the 0x80 seeded at 752. */
static u_char MenuTopAnimCtrl(void)                                     /* 728 */
{
    u_char alpha;

    static const ALPHA_ANIM_TBL alpha_tbl[2] =              /* rdata 3bedf8 */
    {
        {  0, 128,  0, MENU_TOP_IN_TIME },
        { -1,  -1, -1, -1 },
    };

    static const ALPHA_ANIM_TBL move_in_alpha[2] =          /* rdata 3bee08 */
    {
        {  0, 128,  0, MENU_TOP_MOVE_IN_TIME },
        { -1,  -1, -1, -1 },
    };

    static const ALPHA_ANIM_TBL exit_menu_alpha[2] =        /* rdata 3bee18 */
    {
        { 128,  0,  0, MENU_TOP_OUT_TIME },
        {  -1, -1, -1, -1 },
    };

    static const ALPHA_ANIM_TBL move_menu_alpha[2] =        /* rdata 3bee28 */
    {
        { 128,  0,  0, MENU_TOP_MOVE_OUT_TIME },
        {  -1, -1, -1, -1 },
    };

    alpha = 0x80;                                                       /* 752 */

    if (menu_top_disp.anim_step == MENU_TOP_ANIM_START) {               /* 755 */
        menu_top_disp.anim_timer = 0;                                   /* 756 */
        menu_top_disp.anim_step = MENU_TOP_ANIM_IN;                     /* 757 */
    }

    if (menu_top_disp.anim_step == MENU_TOP_ANIM_IN) {                  /* 760 */
        if (menu_top_disp.move_flg != 0) {                              /* 762 */
            alpha = Anim2D_CalcNowAlpha(move_in_alpha,
                                        menu_top_disp.anim_timer);      /* 764 */

            menu_top_disp.anim_timer++;                                 /* 766 */
            if (menu_top_disp.anim_timer >= MENU_TOP_MOVE_IN_TIME) {    /* 768 */
                menu_top_disp.anim_step = MENU_TOP_ANIM_SHOW;           /* 769 */
            }
        }
        else {
            alpha = Anim2D_CalcNowAlpha(alpha_tbl,
                                        menu_top_disp.anim_timer);      /* 775 */

            menu_top_disp.anim_timer++;                                 /* 777 */
            if (menu_top_disp.anim_timer >= MENU_TOP_IN_TIME) {         /* 779 */
                menu_top_disp.anim_step = MENU_TOP_ANIM_SHOW;           /* 780 */
            }
        }
    }
    else if (menu_top_disp.anim_step == MENU_TOP_ANIM_SHOW) {           /* 784 */
        /* held open at 0x80 */
    }
    else if (menu_top_disp.anim_step == MENU_TOP_ANIM_OUT) {            /* 787 */
        if (menu_top_disp.move_flg != 0) {                              /* 789 */
            alpha = Anim2D_CalcNowAlpha(move_menu_alpha,
                                        menu_top_disp.anim_timer);      /* 791 */

            menu_top_disp.anim_timer++;                                 /* 793 */
            if (menu_top_disp.anim_timer >= MENU_TOP_MOVE_OUT_TIME) {   /* 795 */
                menu_top_disp.anim_step = MENU_TOP_ANIM_END;            /* 796 */
            }
        }
        else {
            alpha = Anim2D_CalcNowAlpha(exit_menu_alpha,
                                        menu_top_disp.anim_timer);      /* 802 */

            menu_top_disp.anim_timer++;                                 /* 804 */
            if (menu_top_disp.anim_timer >= MENU_TOP_OUT_TIME) {        /* 806 */
                menu_top_disp.anim_step = MENU_TOP_ANIM_END;            /* 807 */
            }
        }
    }

    return alpha;                                                       /* 813 */
}

/* The header plates and the two rules.  The vertical rule is sized from the
 * two menu_cmn_dat heights plus 30 pixels per available row, so it always
 * reaches exactly to the bottom of the list. */
static void MenuTopTitleDisp(int off_x, int off_y, u_char alpha)         /* 822 */
{
    DISP_SPRT top_ds;
    int       i;
    float     title_line_tate;

    title_line_tate = (float)(menu_cmn_dat[40].h + menu_cmn_dat[42].h);  /* 828 */

    for (i = 0; i < MENU_TOP_CSR_NUM; i++) {                             /* 831 */
        if (CheckMenuCondition(i) != 0) {                                /* 833 */
            title_line_tate += MENU_TOP_LINE_STEP;                       /* 834 */
        }
    }                                                                    /* 836 */

    PK2SendVram(MENU_BG_TEX_ADRS, -1, -1, 0);                            /* 838 */

    CopySprDToSpr(&top_ds, &menu_top[MT_TOP_PLATE]);                     /* 840 */
    top_ds.x += (float)off_x;   top_ds.y += (float)off_y;                /* 841 */
    top_ds.alpha = (u_char)(menu_top[MT_TOP_PLATE].alpha * alpha >> 7);  /* 842 */
    DispSprD(&top_ds);                                                   /* 843 */

    CopySprDToSpr(&top_ds, &menu_top[MT_TOP_PLATE + 1]);                 /* 844 */
    top_ds.x += (float)off_x;   top_ds.y += (float)off_y;                /* 845 */
    top_ds.alpha = (u_char)(menu_top[MT_TOP_PLATE + 1].alpha * alpha >> 7); /* 846 */
    DispSprD(&top_ds);                                                   /* 847 */

    MenuCmnLineYokoDisp(0.0f, 56.0f, 307.0f, alpha, 0xa0);               /* 851 */

    MenuCmnLineTateDisp(22.0f, 0.0f, title_line_tate, alpha, 0xa0);      /* 855 */

    /* The word itself lives in the play-data pak, not the background one. */
    PK2SendVram(MENU_PLAYDATA_TEX_ADRS, -1, -1, 0);                      /* 857 */
    CopySprDToSpr(&top_ds, &menu_top[MT_TOP_TITLE]);                     /* 858 */
    top_ds.x += (float)off_x;   top_ds.y += (float)off_y;                /* 859 */
    top_ds.alpha = (u_char)(menu_top[MT_TOP_TITLE].alpha * alpha >> 7);  /* 860 */
    DispSprD(&top_ds);                                                   /* 861 */
}

/* The eight rows: first every frame, then every label.
 *
 * Both passes walk all eight table rows but place by `disp_menu_cnt`, which
 * only advances for rows CheckMenuCondition() allows -- so a hidden row leaves
 * no gap.  The two passes are separate because the frames are all drawn from
 * the background pak and the labels each need their own PK2SendVram().
 *
 * Rows 5 and 6 take their selected art from the status pak and their
 * unselected art from the background one; every other row uses the play-data
 * pak for both. */
static void MenuTopSelectDisp(int off_x, int off_y, u_char alpha)        /* 872 */
{
    int       i;
    int       disp_menu_cnt;
    DISP_SPRT sel_ds_1;
    DISP_SPRT sel_ds_2;

    static const int select_tbl[MENU_TOP_CSR_NUM][3] =      /* rdata 3bee38 */
    {
        { 22, -1, 14 },
        { 23, -1, 15 },
        { 26, 27, 18 },
        { 24, -1, 16 },
        { 25, -1, 17 },
        { 29, -1, 20 },
        { 30, -1, 21 },
        { 28, -1, 19 },
    };

    static const u_int sel_pk2_tbl[MENU_TOP_CSR_NUM] =      /* rdata 3bee98 */
    {
        MENU_PLAYDATA_TEX_ADRS, MENU_PLAYDATA_TEX_ADRS,
        MENU_PLAYDATA_TEX_ADRS, MENU_PLAYDATA_TEX_ADRS,
        MENU_PLAYDATA_TEX_ADRS, MENU_STATUS_TEX_ADRS,
        MENU_STATUS_TEX_ADRS,   MENU_PLAYDATA_TEX_ADRS,
    };

    static const u_int non_sel_pk2_tbl[MENU_TOP_CSR_NUM] =  /* rdata 3beeb8 */
    {
        MENU_PLAYDATA_TEX_ADRS, MENU_PLAYDATA_TEX_ADRS,
        MENU_PLAYDATA_TEX_ADRS, MENU_PLAYDATA_TEX_ADRS,
        MENU_PLAYDATA_TEX_ADRS, MENU_BG_TEX_ADRS,
        MENU_BG_TEX_ADRS,       MENU_PLAYDATA_TEX_ADRS,
    };

    disp_menu_cnt = 0;                                                   /* 912 */

    for (i = 0; i < MENU_TOP_CSR_NUM; i++) {                             /* 916 */
        if (CheckMenuCondition(i) != 0) {                                /* 918 */
            if (i == menu_wrk.cursor) {                                  /* 920 */
                MenuCmnSelFrameDisp(MENU_TOP_ROW_X,
                                    (float)disp_menu_cnt * MENU_TOP_ROW_PITCH
                                        + MENU_TOP_ROW_TOP,
                                    MENU_TOP_ROW_W, alpha, 0xa0);        /* 922 */
            }
            else {
                MenuCmnNonSelFrameDisp(MENU_TOP_ROW_X,
                                       (float)disp_menu_cnt * MENU_TOP_ROW_PITCH
                                           + MENU_TOP_ROW_TOP,
                                       MENU_TOP_ROW_W, alpha, 0xa0);     /* 927 */
            }

            disp_menu_cnt++;                                             /* 930 */
        }
    }                                                                    /* 932 */

    disp_menu_cnt = 0;                                                   /* 935 */

    for (i = 0; i < MENU_TOP_CSR_NUM; i++) {                             /* 938 */
        if (CheckMenuCondition(i) != 0) {                                /* 940 */
            if (i == menu_wrk.cursor) {                                  /* 942 */
                PK2SendVram(sel_pk2_tbl[i], -1, -1, 0);                  /* 943 */

                CopySprDToSpr(&sel_ds_1, &menu_top[select_tbl[i][0]]);   /* 945 */
                sel_ds_1.x += (float)off_x;
                sel_ds_1.y = (float)disp_menu_cnt * MENU_TOP_ROW_PITCH
                             + MENU_TOP_ROW_TOP + (float)off_y;          /* 946 */
                sel_ds_1.alpha = (u_char)(sel_ds_1.alpha * alpha >> 7);  /* 947 */
                DispSprD(&sel_ds_1);                                     /* 948 */

                /* Only the camera row has a second plate. */
                if (select_tbl[i][1] != -1) {                            /* 950 */
                    CopySprDToSpr(&sel_ds_2,
                                  &menu_top[select_tbl[i][1]]);          /* 951 */
                    sel_ds_2.x += (float)off_x;
                    sel_ds_2.y = (float)disp_menu_cnt * MENU_TOP_ROW_PITCH
                                 + MENU_TOP_ROW_TOP + (float)off_y;      /* 952 */
                    sel_ds_2.alpha =
                        (u_char)(sel_ds_2.alpha * alpha >> 7);           /* 953 */
                    DispSprD(&sel_ds_2);                                 /* 954 */
                }
            }
            else {
                PK2SendVram(non_sel_pk2_tbl[i], -1, -1, 0);              /* 959 */

                CopySprDToSpr(&sel_ds_1, &menu_top[select_tbl[i][2]]);   /* 961 */
                sel_ds_1.x += (float)off_x;
                sel_ds_1.y = (float)disp_menu_cnt * MENU_TOP_ROW_PITCH
                             + MENU_TOP_ROW_TOP + (float)off_y;          /* 962 */
                sel_ds_1.alpha = (u_char)(sel_ds_1.alpha * alpha >> 7);  /* 963 */
                DispSprD(&sel_ds_1);                                     /* 964 */
            }

            disp_menu_cnt++;                                             /* 967 */
        }
    }                                                                    /* 969 */
}

/* The play-data panel: two plates and six readouts.  Note the alpha here is
 * taken from the working copy, where MenuTopTitleDisp() reads it back out of
 * menu_top[] -- the same value, spelled two ways. */
static void MenuTopPlayDataDisp(int off_x, int off_y, u_char alpha)      /* 981 */
{
    DISP_SPRT top_ds;

    PK2SendVram(MENU_PLAYDATA_TEX_ADRS, -1, -1, 0);                      /* 986 */

    CopySprDToSpr(&top_ds, &menu_top[MT_PLAYDATA_PLATE]);                /* 990 */
    top_ds.x += (float)off_x;   top_ds.y += (float)off_y;                /* 991 */
    top_ds.alpha = (u_char)(top_ds.alpha * alpha >> 7);                  /* 992 */
    DispSprD(&top_ds);                                                   /* 993 */

    CopySprDToSpr(&top_ds, &menu_top[MT_PLAYDATA_PLATE + 1]);            /* 994 */
    top_ds.x += (float)off_x;   top_ds.y += (float)off_y;                /* 995 */
    top_ds.alpha = (u_char)(top_ds.alpha * alpha >> 7);                  /* 996 */
    DispSprD(&top_ds);                                                   /* 997 */

    MenuTopPlayTimeDisp(off_x, off_y, alpha);                            /* 1000 */

    MenuTopScoreDisp(off_x, off_y, alpha);                               /* 1002 */

    MenuTopPhotoNumDisp(off_x, off_y, alpha);                            /* 1004 */

    MenuTopBusterNumDisp(off_x, off_y, alpha);                           /* 1006 */

    MenuTopMaxScoreDisp(off_x, off_y, alpha);                            /* 1008 */

    MenuTopNowDateDisp(off_x, off_y, alpha);                             /* 1011 */
}

/* h:mm:ss, with the two colons drawn as message 0 of bank 8.  The minutes and
 * seconds are zero-padded and the hours are not. */
static void MenuTopPlayTimeDisp(int off_x, int off_y, u_char alpha)      /* 1022 */
{
    TIME_INFO play_time;

    play_time = GetPlayTime();                                           /* 1027 */

    PrintNumber_N(play_time.hour, 3, off_x + 396, off_y + 170,
                  2, alpha, 0, 1, 0);                                    /* 1032 */
    PrintMsg(8, 0, off_x + 439, off_y + 170, 2, alpha, 0);               /* 1035 */
    PrintNumber_N(play_time.min, 2, off_x + 449, off_y + 170,
                  2, alpha, 0, 1, 1);                                    /* 1038 */
    PrintMsg(8, 0, off_x + 478, off_y + 170, 2, alpha, 0);               /* 1041 */
    PrintNumber_N(play_time.sec, 2, off_x + 488, off_y + 170,
                  2, alpha, 0, 1, 1);                                    /* 1044 */
}

static void MenuTopScoreDisp(int off_x, int off_y, u_char alpha)         /* 1056 */
{
    PrintNumber_N(GetPlayData_TotalScore(), 6,                           /* 1060 */
                  off_x + 432, off_y + 194, 2, alpha, 0, 1, 0);          /* 1066 */
}

static void MenuTopPhotoNumDisp(int off_x, int off_y, u_char alpha)      /* 1076 */
{
    PrintNumber_N(GetPhotoNum(), 5,                                      /* 1080 */
                  off_x + 446, off_y + 218, 2, alpha, 0, 1, 0);          /* 1085 */
}

static void MenuTopBusterNumDisp(int off_x, int off_y, u_char alpha)     /* 1096 */
{
    PrintNumber_N(GetBusterGhostNum(), 4,                                /* 1100 */
                  off_x + 460, off_y + 242, 2, alpha, 0, 1, 0);          /* 1105 */
}

static void MenuTopMaxScoreDisp(int off_x, int off_y, u_char alpha)      /* 1116 */
{
    PrintNumber_N(GetMaxScore(), 5,                                      /* 1120 */
                  off_x + 446, off_y + 266, 2, alpha, 0, 1, 0);          /* 1125 */
}

/* The console clock, re-read every half second -- 25 frames on PAL, 30 on
 * NTSC.  Printed day / month / year, then h:mm:ss. */
static void MenuTopNowDateDisp(int off_x, int off_y, u_char alpha)       /* 1136 */
{
    if (menu_top_disp.now_time_cnt == 0) {                               /* 1139 */
        GetSystemTime(&menu_top_disp.now_time);                          /* 1141 */
    }

    menu_top_disp.now_time_cnt++;                                        /* 1144 */

    if (GetPALMode() != 0) {                                             /* 1147 */
        if (menu_top_disp.now_time_cnt >= MENU_TOP_CLOCK_PAL) {          /* 1149 */
            menu_top_disp.now_time_cnt = 0;
        }
    }
    else {
        if (menu_top_disp.now_time_cnt >= MENU_TOP_CLOCK_NTSC) {         /* 1153 */
            menu_top_disp.now_time_cnt = 0;
        }
    }

    PrintNumber_N(menu_top_disp.now_time.day.day, 2, off_x + 299,
                  off_y + 310, 2, alpha, 0, 1, 1);                       /* 1161 */
    PrintNumber_N(menu_top_disp.now_time.day.month, 2, off_x + 338,
                  off_y + 310, 2, alpha, 0, 1, 1);                       /* 1164 */
    PrintNumber_N(menu_top_disp.now_time.day.year, 2, off_x + 377,
                  off_y + 310, 2, alpha, 0, 1, 1);                       /* 1167 */

    PrintNumber_N(menu_top_disp.now_time.time.hour, 2, off_x + 421,
                  off_y + 310, 2, alpha, 0, 1, 1);                       /* 1171 */
    PrintMsg(8, 0, off_x + 449, off_y + 310, 2, alpha, 0);               /* 1174 */
    PrintNumber_N(menu_top_disp.now_time.time.min, 2, off_x + 460,
                  off_y + 310, 2, alpha, 0, 1, 1);                       /* 1177 */
    PrintMsg(8, 0, off_x + 488, off_y + 310, 2, alpha, 0);               /* 1180 */
    PrintNumber_N(menu_top_disp.now_time.time.sec, 2, off_x + 499,
                  off_y + 310, 2, alpha, 0, 1, 1);                       /* 1183 */
}

/* Both of these ignore their offsets and place at fixed coordinates. */
static void MenuTopPlyrDataDisp(int off_x, int off_y, u_char alpha)      /* 1195 */
{
    (void)off_x;
    (void)off_y;

    MenuPlyrDataDisp(206, 74, alpha);                                    /* 1196 */
}

static void MenuTopCaptionDisp(int off_x, int off_y, u_char alpha)       /* 1205 */
{
    (void)off_x;
    (void)off_y;

    DrawCmnCapGroup_W(0, 0, alpha, 0);                                   /* 1208 */
}

/* The chapter plate down the far edge: three stacked strips out of the
 * chapter's own pak.  Mission Mode draws the eleventh chapter's group, which
 * is where the mission plate is loaded. */
static void MenuTopChapterTitleDisp(int off_x, int off_y, u_char alpha)  /* 1255 */
{
    DISP_SPRT chapter_ds;
    int       i;

    static const int chapter_tbl[11][3] =                   /* rdata 3beef0 */
    {
        { 35, 36, 37 },
        { 38, 39, 40 },
        { 41, 42, 43 },
        { 44, 45, 46 },
        { 47, 48, 49 },
        { 50, 51, 52 },
        { 53, 54, 55 },
        { 56, 57, 58 },
        { 59, 60, 61 },
        { 62, 63, 64 },
        { 65, 66, 67 },
    };

    if (chapter_title_tex_addr == nullptr) {                             /* 1276 */
        PRINT_ASSERT("Error! MenuTopChapterTitleDisp");                  /* 1277 */
    }

    PK2SendVram((uintptr_t)chapter_title_tex_addr, -1, -1, 0);           /* 1280 */

    for (i = 0; i < 3; i++) {                                            /* 1282 */
        if (CheckIngameMission() != 0) {                                 /* 1284 */
            CopySprDToSpr(&chapter_ds,
                          &menu_top[MT_CHAPTER_MISSION + i]);            /* 1285 */
            chapter_ds.x += (float)off_x;
            chapter_ds.y += (float)off_y;                                /* 1286 */
            chapter_ds.alpha = (u_char)(chapter_ds.alpha * alpha >> 7);  /* 1287 */
            DispSprD(&chapter_ds);                                       /* 1288 */
        }
        else {
            CopySprDToSpr(&chapter_ds,
                &menu_top[chapter_tbl[ingame_wrk.mChapterNo.Get()][i]]);
            chapter_ds.x += (float)off_x;
            chapter_ds.y += (float)off_y;                                /* 1292 */
            chapter_ds.alpha = (u_char)(chapter_ds.alpha * alpha >> 7);  /* 1293 */
            DispSprD(&chapter_ds);                                       /* 1294 */
        }
    }                                                                    /* 1296 */
}

/* The caption strip along the bottom: one window and one line of help text
 * per row.  msg_id[]'s trailing -1 is never reached -- the cursor cannot
 * leave 0..7 -- so it is the table's terminator rather than a real state. */
static void MenuTopWindowDisp(int off_x, int off_y, u_char alpha)        /* 1307 */
{
    static const int msg_id[9] =                            /* rdata 3befd8 */
    {
        0, 1, 2, 3, 4, 5, 6, 7, -1,
    };

    DrawCmnWindow(0xa0, (float)(off_x + 24), (float)(off_y + 346),
                  592.0f, 100.0f, alpha, 0x66);                          /* 1323 */

    if (msg_id[menu_wrk.cursor] != -1) {                                 /* 1325 */
        PrintMsg(0x3a, msg_id[menu_wrk.cursor], off_x + 48, off_y + 370,
                 1, alpha, 0);                                           /* 1327 */
    }
}

/* ==========================================================================
 *  Debug switches
 * ======================================================================== */

/* Held on controller 2: pad[1].now bit 0x100 plus one of nine pad[1].one
 * bits.  The first test is separate from the chain below it, so R2 + that bit
 * runs DebugAllItemGet() and returns; everything else falls through one
 * else-if ladder.
 *
 * The clear-flag switch is the only one that does two things, and its second
 * statement leaves no line of its own -- CVariable::Set() was inlined and its
 * variable.h 39/49 notes replaced it.  Set(1) is in range for a
 * CVariable<char,0,99>, so both of Set()'s bounds tests folded away. */
static void DebugMenuTopDebugSwitchPad(void)                             /* 1339 */
{
    if ((pad[DBG_PAD].now & DBG_HOLD) && (pad[DBG_PAD].one & 0x20)) {    /* 1343 */
        DebugAllItemGet();                                               /* 1344 */
        return;
    }

    if (pad[DBG_PAD].now & DBG_HOLD) {                                   /* 1347 */
        if (pad[DBG_PAD].one & 0x10) {
            DebugAllFileGet();                                           /* 1348 */
        }
        else if (pad[DBG_PAD].one & 0x80) {                              /* 1351 */
            DebugAllCrystalGet();                                        /* 1352 */
        }
        else if (pad[DBG_PAD].one & 0x40) {                              /* 1355 */
            DebugGetAllSoulList();                                       /* 1356 */
        }
        else if (pad[DBG_PAD].one & 0x4) {                               /* 1359 */
            DebugSetLevelGemMaxNum();                                    /* 1360 */
        }
        else if (pad[DBG_PAD].one & 0x1) {                               /* 1363 */
            DebugSetPlayScoreMaxNum();                                   /* 1364 */
        }
        else if (pad[DBG_PAD].one & 0x8) {                               /* 1367 */
            DebugAllFirstMemoGet();                                      /* 1368 */
        }
        else if (pad[DBG_PAD].one & 0x2) {                               /* 1371 */
            DebugAllSecondMemoGet();                                     /* 1372 */
        }
        else if (pad[DBG_PAD].one & 0x200) {                             /* 1375 */
            DebugAllClearFlgUp();                                        /* 1376 */
            ingame_wrk.mClearCnt.Set(1);                                 /* 1377 */
        }
        else if (pad[DBG_PAD].one & 0x400) {                             /* 1380 */
            DebugAllMapDisp();                                           /* 1381 */
        }
    }
}
