// FILE: /home/zero_rom/zero2np/src/outgame/setup_menu.c
//
// The setup menu: the screen GID_TITLE_SETUPMENU shows on top of setup.c's
// background.  Three rows on the left -- Story, Mission, Exit -- and, for the
// first two, a five-row settings column on the right: costume, Mio's
// accessory, Mayu's accessory, difficulty, and the Game Start / Mission Select
// button that commits everything.  Exit opens a yes/no window instead.
//
// The file is built from the same five pieces every outgame screen uses, but
// `mode` plays the part `now_place` does elsewhere: it indexes
// setup_menu_pad_func[] and setup_menu_disp_func[] in step, and the three
// entries are top menu / settings column / exit window.
//
// Two things this screen owns that no other outgame screen does.  The clear
// flags gate the choices -- the costume and difficulty cursors *skip* locked
// entries rather than stopping on them, which is why both walk in a bounded
// loop instead of one step.  And the commit is the only place in outgame/ that
// writes the ingame model numbers: SetPlyrMdlNo / SetSisterMdlNo and the two
// accessory setters, out of costume_tbl[].
//
// Reconstructed from the Feb 6 2004 prototype (SLES_523.84).
// Trailing /* NNN */ are ROM source line numbers, measured from
// disassemble_function and symbols.txt.  The ROM's own brace style here is
// K&R -- `{` on the `if`/`else`/`for` line, which is what makes every measured
// line land on a statement; this file follows the folder's Allman style
// instead, so the annotations are per statement rather than per source line.

#include "setup_menu.h"

#include "setup.h"                          // SetupReturnTitleReq / GetSetup*Pk2Addr
#include "title.h"                          // SetTitleLoadFlg / GetOutGameCmnTexAddr
#include "tim_dat/setup_dat.h"              // setup_tex[]
#include "../common/utility2.h"              // PRINT_ASSERT
#include "../common/variable.h"              // pad[] / ingame_wrk
#include "../graphics/graph2d/draw_cmn.h"   // DrawCmnWindow / CapGroup_W / YesNoSel
#include "../graphics/graph2d/g2d_draw.h"   // DISP_SPRT / CopySprDToSpr / DispSprD
#include "../graphics/graph2d/message.h"    // PrintMsg
#include "../graphics/graph2d/tim2.h"       // PK2SendVram
#include "../ingame/clear/prg/clear_flg.h"  // clear_flg_ctrl
#include "../ingame/menu/zero2_anim2d.h"    // Zero2Anim2D_*
#include "../ingame/plyr/plyr_mdl.h"        // SetPlyrMdlNo / SetSisterMdlNo / ...
#include "../main/gphase.h"                 // SetNextGPhase / GID_*
#include "../system/eeiop/cddat.h"          // BGM010_OMAKE_STR / _HXD
#include "../system/eeiop/stream_auto.h"    // StreamAutoPlay / StreamAutoFadeOut
#include "../system/os/system.h"            // SystemBankPlay
#include "../system/pad/pad.h"              // paddat / GetPadAnalogRpt

#include <stdint.h>                         // uintptr_t

/* SETUP_MENU_CTRL::step */
#define SETUP_MENU_DISP_INIT    0
#define SETUP_MENU_MAIN         1
#define SETUP_MENU_EXIT         2

/* SETUP_MENU_CTRL::mode -- indexes both dispatch tables. */
#define SETUP_MENU_MODE_SEL_MENU    0
#define SETUP_MENU_MODE_SEL_SETUP   1
#define SETUP_MENU_MODE_EXIT_CONF   2

/* SETUP_MENU_CTRL::menu_csr -- the three rows on the left. */
#define SETUP_MENU_CSR_STORY    0
#define SETUP_MENU_CSR_MISSION  1
#define SETUP_MENU_CSR_EXIT     2
#define SETUP_MENU_CSR_MAX      3

/* SETUP_MENU_CTRL::setup_csr -- the five rows of the settings column. */
#define SETUP_MENU_SETUP_CSR_COSTUME    0
#define SETUP_MENU_SETUP_CSR_MIO_ACS    1
#define SETUP_MENU_SETUP_CSR_MAYU_ACS   2
#define SETUP_MENU_SETUP_CSR_DIFFICULTY 3
#define SETUP_MENU_SETUP_CSR_START      4
#define SETUP_MENU_SETUP_CSR_MAX        5

/* SETUP_MENU_CTRL::next_place -- what SetupMenuMain() does once the fade is
 * over.  0 is "back to the title", and it is also the value SetupMenuExitReq()
 * leaves behind, so the title hand-off needs no state of its own. */
#define SETUP_MENU_NEXT_TITLE   0
#define SETUP_MENU_NEXT_GAME    1
#define SETUP_MENU_NEXT_MISSION 2

/* Zero2Anim2D_InOutAnimCtrl() states this file tests by hand. */
#define SETUP_MENU_ANIM_IN_END  2           /* fully open, pad handling live  */
#define SETUP_MENU_ANIM_END     4           /* fade-out finished              */

/* SystemBankPlay() cue numbers, the same four every outgame screen uses. */
#define SE_CURSOR   0
#define SE_CANCEL   1
#define SE_ERROR    2
#define SE_DECIDE   3

/* Costume counts.  Nine costumes, three accessory bits (0 unused, 1 Mio,
 * 2 Mayu -- which is why the accessory tests index by setup_csr) and four
 * difficulties. */
#define SETUP_MENU_COSTUME_MAX      9
#define SETUP_MENU_DIFFICULTY_MAX   4

/* Frames the fade into the game runs for before the phase changes. */
#define SETUP_MENU_FADE_TIME    30

/* The paired model numbers one costume selects.  types.txt names the struct
 * and its four members; nothing else in the build references it, so it is
 * declared here beside its only table. */
struct _COSTUME_TWIN_TBL            /* 0x8 */
{
    /* 0x0 */ short mMioMdl;
    /* 0x2 */ short mMayuMdl;
    /* 0x4 */ short mMioAcsMdl;
    /* 0x6 */ short mMayuAcsMdl;
};

typedef struct _COSTUME_TWIN_TBL COSTUME_TWIN_TBL;

static void SetupMenuPad(void);
static void SetupMenuSetupSelPad(void);
static void SetupMenuExitConfPad(void);
static void SetupMenuExitReq(void);
static void SetupMenuGoToGame(void);
static void SetupMenuGotoMission(void);
static void SetupMenuDispInit(void);
static void SetupMenuSelMenuDisp(int off_x, int off_y, u_char alpha);
static void SetupMenuSelSetupDisp(int off_x, int off_y, u_char alpha);
static void SetupMenuExitConfDisp(int off_x, int off_y, u_char alpha);
static void SetupMenuTitleFrameDisp(int off_x, int off_y, u_char alpha, void *pk2_addr);
static void SetupMenuTitleDisp(int off_x, int off_y, u_char alpha, void *pk2_addr);
static void SetupMenuSelMenuItemDisp(int menu_label, int off_x, int off_y,
                                     u_char alpha, u_char rgb);
static void SetupMenuTopCursorFrameDisp(int off_x, int off_y, u_char alpha, u_char rgb);
static void SetupMenuTopCursorDisp(int off_x, int off_y, u_char alpha, u_char rgb);
static void SetupMenuCostumeDisp(int off_x, int off_y, u_char alpha, u_char rgb, u_char flg);
static void SetupMenuCostumeTypeDisp(int costume_type, int off_x, int off_y,
                                     u_char alpha, u_char rgb, u_char flg);
static void SetupMenuAccessoryDisp(int off_x, int off_y, u_char alpha, u_char rgb, u_char flg);
static void SetupMenuMioDisp(int off_x, int off_y, u_char alpha, u_char rgb, u_char flg);
static void SetupMenuMayuDisp(int off_x, int off_y, u_char alpha, u_char rgb, u_char flg);
static void SetupMenuDifficultyDisp(int difficulty_label, int off_x, int off_y,
                                    u_char alpha, u_char rgb, u_char flg);
static void SetupMenuGameStartDisp(int off_x, int off_y, u_char alpha, u_char rgb, u_char flg);
static void SetupMenuMissionSelectDisp(int off_x, int off_y, u_char alpha, u_char rgb, u_char flg);
static void SetupMenuCursorDisp(int off_x, int off_y, u_char alpha, u_char rgb);
static void SetupMenuOnDisp(int x, int y, u_char alpha, u_char rgb, u_char flg);
static void SetupMenuOffDisp(int x, int y, u_char alpha, u_char rgb, u_char flg);
static void SetupMenuCsrLDisp(int x, int y, u_char alpha, u_char rgb);
static void SetupMenuCsrRDisp(int x, int y, u_char alpha, u_char rgb);
static void SetupMenuCsrFlareLDisp(int x, int y, u_char alpha, u_char rgb);
static void SetupMenuCsrFlareRDisp(int x, int y, u_char alpha, u_char rgb);
static void SetupMenuLineDisp(int off_x, int off_y, u_char alpha, u_char rgb);
static void SetupMenuMsgWinDisp(int off_x, int off_y, u_char alpha);
static void SetupMenuCaptionDisp(int off_x, int off_y, u_char alpha);

/* Both tables are indexed by SETUP_MENU_CTRL::mode. */
static void (*setup_menu_pad_func[3])(void) =                           /* data 345050 */
{
    SetupMenuPad,
    SetupMenuSetupSelPad,
    SetupMenuExitConfPad
};

static void (*setup_menu_disp_func[3])(int off_x, int off_y, u_char alpha) = /* data 345060 */
{
    SetupMenuSelMenuDisp,
    SetupMenuSelSetupDisp,
    SetupMenuExitConfDisp
};

/* Indexed by the on/off cursor itself, so index 0 is Off. */
static void (*setup_on_off_disp_func[2])(int x, int y, u_char alpha, u_char rgb, u_char flg) =
{                                                                       /* sdata 3f42d0 */
    SetupMenuOffDisp,
    SetupMenuOnDisp
};

static SETUP_MENU_CTRL setup_menu_ctrl;                                 /* bss 4bbfe0 */
static SETUP_MENU_DISP setup_menu_disp;                                 /* bss 4bbff0 */

/* Costume 0 is the pair the game ships with; 1..8 are unlocks.  All nine share
 * the same two accessory models, so the accessory toggle is a straight yes/no
 * rather than a per-costume choice. */
static COSTUME_TWIN_TBL costume_tbl[SETUP_MENU_COSTUME_MAX] =           /* rodata 3c6e48 */
{
    {  0,  1, 6, 7 },
    { 64, 65, 6, 7 },
    { 66, 67, 6, 7 },
    { 68, 69, 6, 7 },
    { 70, 71, 6, 7 },
    { 72, 73, 6, 7 },
    { 74, 75, 6, 7 },
    { 62, 63, 6, 7 },
    { 76, 77, 6, 7 }
};

// ──────────────────────────────────────────────────────────────────────
// Mode entry points.

void SetupMenuInit(void)                                                /* 265 */
{
    setup_menu_ctrl.step = SETUP_MENU_DISP_INIT;                        /* 268 */
    setup_menu_ctrl.mode = SETUP_MENU_MODE_SEL_MENU;                    /* 269 */
    setup_menu_ctrl.conf_csr = 1;                                       /* 270 */
    setup_menu_ctrl.menu_csr = SETUP_MENU_CSR_STORY;                    /* 271 */
    setup_menu_ctrl.setup_csr = SETUP_MENU_SETUP_CSR_COSTUME;           /* 272 */
    setup_menu_ctrl.costume_csr = 0;                                    /* 273 */
    setup_menu_ctrl.mio_csr = 0;                                        /* 274 */
    setup_menu_ctrl.mayu_csr = 0;                                       /* 275 */
    setup_menu_ctrl.difficulty_csr = ingame_wrk.mDifficulty.Get();      /* 276 */
    setup_menu_ctrl.next_place = SETUP_MENU_NEXT_TITLE;                 /* 277 */
    setup_menu_ctrl.stream_id = -1;                                     /* 278 */
}

void SetupMenuMain(void)                                                /* 290 */
{
    if (setup_menu_ctrl.step == SETUP_MENU_DISP_INIT)                   /* 292 */
    {
        SetupMenuDispInit();                                            /* 294 */

        /* The menu's own BGM.  Nothing stops it on the way into the game --
         * StreamAutoFadeOut() in the two hand-offs is what does that. */
        setup_menu_ctrl.stream_id =
            StreamAutoPlay(BGM010_OMAKE_STR, BGM010_OMAKE_HXD, 0xb, 0, 1, 0x3200, 0,
                           (SND_3D_SET *)0);                            /* 298 */

        setup_menu_ctrl.step = SETUP_MENU_MAIN;                         /* 300 */
    }

    if ((setup_menu_ctrl.step == SETUP_MENU_MAIN) &&                    /* 304 */
        (setup_menu_disp.anim_step == SETUP_MENU_ANIM_IN_END))          /* 305 */
    {
        setup_menu_pad_func[setup_menu_ctrl.mode]();                    /* 307 */
    }

    if (setup_menu_ctrl.step == SETUP_MENU_EXIT)                        /* 311 */
    {
        switch (setup_menu_ctrl.next_place)                             /* 312 */
        {
        case SETUP_MENU_NEXT_TITLE:
            /* setup.c owns the fade back to the title; nothing to do here. */
            break;

        case SETUP_MENU_NEXT_GAME:
            if (setup_menu_disp.fade_anim_timer >= SETUP_MENU_FADE_TIME) /* 317 */
            {
                SetTitleLoadFlg(0);                                     /* 318 */
                SetNextGPhase(GID_STORY_LOAD_MISSION_SAVE);             /* 319 */
            }
            break;

        case SETUP_MENU_NEXT_MISSION:
            if (setup_menu_disp.anim_step == SETUP_MENU_ANIM_END)       /* 323 */
            {
                SetNextGPhase(GID_MISSION_SEL);                         /* 324 */
            }
            break;

        default:
            PRINT_ASSERT("Error! %s", __FUNCTION__);                    /* 328 */
            break;
        }
    }
}

// ──────────────────────────────────────────────────────────────────────
// Pad handlers, one per mode.

/* The three rows on the left.  UP/DOWN wrap; CROSS on Story or Mission drops
 * into the settings column, CROSS on Exit opens the window.  TRIANGLE does not
 * leave -- it moves the cursor onto Exit, so backing out is always two
 * presses. */
static void SetupMenuPad(void)                                          /* 338 */
{
    if (((pad[0].rpt & 0x1000U) != 0) || (GetPadAnalogRpt(0) != 0))     /* 342 */
    {
        SystemBankPlay(SE_CURSOR, 1, 0, 0, (SND_3D_SET *)0, 0x3200, 0x1000);  /* 343 */
        setup_menu_ctrl.menu_csr =
            (char)((setup_menu_ctrl.menu_csr + (SETUP_MENU_CSR_MAX - 1)) % SETUP_MENU_CSR_MAX);  /* 344 */
    }
    else if (((pad[0].rpt & 0x4000U) != 0) || (GetPadAnalogRpt(1) != 0)) /* 347 */
    {
        SystemBankPlay(SE_CURSOR, 1, 0, 0, (SND_3D_SET *)0, 0x3200, 0x1000);  /* 348 */
        setup_menu_ctrl.menu_csr =
            (char)((setup_menu_ctrl.menu_csr + 1) % SETUP_MENU_CSR_MAX); /* 349 */
    }
    else if (*paddat[0] == 1)                                           /* 351 */
    {
        SystemBankPlay(SE_DECIDE, 1, 0, 0, (SND_3D_SET *)0, 0x3200, 0x1000);  /* 352 */

        if (setup_menu_ctrl.menu_csr == SETUP_MENU_CSR_EXIT)            /* 354 */
        {
            /* SETUP_MENU_CSR_EXIT and SETUP_MENU_MODE_EXIT_CONF are both 2,
             * and the ROM copies the cursor across rather than writing the
             * mode literal -- GCC keeps the loaded byte, so this is the
             * source, not a fold. */
            setup_menu_ctrl.mode = setup_menu_ctrl.menu_csr;            /* 355 */
            setup_menu_ctrl.conf_csr = 1;                               /* 356 */
        }
        else
        {
            setup_menu_ctrl.mode = SETUP_MENU_MODE_SEL_SETUP;           /* 360 */
            setup_menu_ctrl.setup_csr = SETUP_MENU_SETUP_CSR_COSTUME;   /* 361 */
        }
    }
    else if (*paddat[1] == 1)                                           /* 365 */
    {
        SystemBankPlay(SE_CANCEL, 1, 0, 0, (SND_3D_SET *)0, 0x3200, 0x1000);  /* 366 */
        setup_menu_ctrl.menu_csr = SETUP_MENU_CSR_EXIT;                 /* 368 */
    }
}

/* Ask for the fade back to the title.  This is the only one of the three exits
 * that also calls setup.c -- SetupReturnTitleReq() is what actually takes the
 * background away, and SetupMenuMain()'s next_place 0 case has nothing left to
 * do.  next_place is stored again here even though nothing reachable could
 * have moved it: the pad handlers stop running the moment step leaves
 * SETUP_MENU_MAIN, so the two hand-offs cannot be followed by this one. */
static void SetupMenuExitReq(void)                                      /* 377 */
{
    setup_menu_ctrl.step = SETUP_MENU_EXIT;                             /* 380 */

    setup_menu_disp.anim_step = 3;                                      /* 382 */
    setup_menu_disp.anim_timer = 0;                                     /* 383 */
    setup_menu_ctrl.next_place = SETUP_MENU_NEXT_TITLE;                 /* 384 */

    StreamAutoFadeOut(setup_menu_ctrl.stream_id, 5);                    /* 386 */

    SetupReturnTitleReq();                                              /* 389 */
}

/* The settings column.  UP/DOWN walk the five rows, LEFT/RIGHT change the row
 * under the cursor, CROSS on the last row commits, TRIANGLE goes back up. */
static void SetupMenuSetupSelPad(void)                                  /* 397 */
{
    int i;
    int csr_back_up;

    if (((pad[0].rpt & 0x1000U) != 0) || (GetPadAnalogRpt(0) != 0))     /* 406 */
    {
        SystemBankPlay(SE_CURSOR, 1, 0, 0, (SND_3D_SET *)0, 0x3200, 0x1000);  /* 407 */
        setup_menu_ctrl.setup_csr =
            (char)((setup_menu_ctrl.setup_csr + (SETUP_MENU_SETUP_CSR_MAX - 1)) %
                   SETUP_MENU_SETUP_CSR_MAX);                           /* 409 */

        /* Mission Mode has no difficulty row, and neither does a first
         * playthrough -- the cursor steps straight over it. */
        if ((setup_menu_ctrl.menu_csr == SETUP_MENU_CSR_MISSION) ||
            (ingame_wrk.clear_save_flg == 0))                           /* 412 */
        {
            if (setup_menu_ctrl.setup_csr == SETUP_MENU_SETUP_CSR_DIFFICULTY) /* 413 */
            {
                setup_menu_ctrl.setup_csr = SETUP_MENU_SETUP_CSR_MAYU_ACS;    /* 414 */
            }
        }
    }
    else if (((pad[0].rpt & 0x4000U) != 0) || (GetPadAnalogRpt(1) != 0)) /* 419 */
    {
        SystemBankPlay(SE_CURSOR, 1, 0, 0, (SND_3D_SET *)0, 0x3200, 0x1000);  /* 420 */
        setup_menu_ctrl.setup_csr =
            (char)((setup_menu_ctrl.setup_csr + 1) % SETUP_MENU_SETUP_CSR_MAX);  /* 422 */

        if ((setup_menu_ctrl.menu_csr == SETUP_MENU_CSR_MISSION) ||
            (ingame_wrk.clear_save_flg == 0))                           /* 425 */
        {
            if (setup_menu_ctrl.setup_csr == SETUP_MENU_SETUP_CSR_DIFFICULTY) /* 426 */
            {
                setup_menu_ctrl.setup_csr = SETUP_MENU_SETUP_CSR_START; /* 427 */
            }
        }
    }
    else if (((pad[0].rpt & 0x8000U) != 0) || (GetPadAnalogRpt(2) != 0)) /* 432 */
    {
        switch (setup_menu_ctrl.setup_csr)                              /* 433 */
        {
        case SETUP_MENU_SETUP_CSR_COSTUME:
            csr_back_up = setup_menu_ctrl.costume_csr;                  /* 436 */

            for (i = 0; i < SETUP_MENU_COSTUME_MAX; i++) {              /* 438 */
                setup_menu_ctrl.costume_csr =
                    (char)((setup_menu_ctrl.costume_csr + (SETUP_MENU_COSTUME_MAX - 1)) %
                           SETUP_MENU_COSTUME_MAX);                     /* 439 */

                if (clear_flg_ctrl.costume_flg.IsUp(setup_menu_ctrl.costume_csr) != 0)
                {
                    break;
                }
            }                                                           /* 445 */

            if (csr_back_up != setup_menu_ctrl.costume_csr)             /* 448 */
            {
                SystemBankPlay(SE_CURSOR, 1, 0, 0, (SND_3D_SET *)0, 0x3200, 0x1000);  /* 449 */
            }
            else
            {
                SystemBankPlay(SE_ERROR, 1, 0, 0, (SND_3D_SET *)0, 0x3200, 0x1000);   /* 452 */
            }
            break;

        case SETUP_MENU_SETUP_CSR_MIO_ACS:
            csr_back_up = setup_menu_ctrl.mio_csr;                      /* 457 */

            setup_menu_ctrl.mio_csr = (char)(setup_menu_ctrl.mio_csr ^ 1);  /* 459 */

            /* accessory_flg bit 1 is Mio's and bit 2 Mayu's, which is exactly
             * the row index -- the ROM passes setup_csr rather than a literal
             * (the bit position is computed at run time, so it cannot be a
             * constant that GCC folded). */
            if ((setup_menu_ctrl.mio_csr == 1) &&
                (clear_flg_ctrl.accessory_flg.IsUp(setup_menu_ctrl.setup_csr) == 0))  /* 461 */
            {
                setup_menu_ctrl.mio_csr = 0;
            }

            if (csr_back_up != setup_menu_ctrl.mio_csr)                 /* 468 */
            {
                SystemBankPlay(SE_CURSOR, 1, 0, 0, (SND_3D_SET *)0, 0x3200, 0x1000);  /* 469 */
            }
            else
            {
                SystemBankPlay(SE_ERROR, 1, 0, 0, (SND_3D_SET *)0, 0x3200, 0x1000);   /* 472 */
            }
            break;

        case SETUP_MENU_SETUP_CSR_MAYU_ACS:
            csr_back_up = setup_menu_ctrl.mayu_csr;                     /* 476 */

            setup_menu_ctrl.mayu_csr = (char)(setup_menu_ctrl.mayu_csr ^ 1);  /* 478 */

            if ((setup_menu_ctrl.mayu_csr == 1) &&
                (clear_flg_ctrl.accessory_flg.IsUp(setup_menu_ctrl.setup_csr) == 0))  /* 481 */
            {
                setup_menu_ctrl.mayu_csr = 0;
            }

            if (csr_back_up != setup_menu_ctrl.mayu_csr)                /* 489 */
            {
                SystemBankPlay(SE_CURSOR, 1, 0, 0, (SND_3D_SET *)0, 0x3200, 0x1000);  /* 490 */
            }
            else
            {
                SystemBankPlay(SE_ERROR, 1, 0, 0, (SND_3D_SET *)0, 0x3200, 0x1000);   /* 493 */
            }
            break;

        case SETUP_MENU_SETUP_CSR_DIFFICULTY:
            csr_back_up = setup_menu_ctrl.difficulty_csr;               /* 497 */

            for (i = 0; i < SETUP_MENU_DIFFICULTY_MAX; i++) {           /* 499 */
                setup_menu_ctrl.difficulty_csr =
                    (char)((setup_menu_ctrl.difficulty_csr + (SETUP_MENU_DIFFICULTY_MAX - 1)) %
                           SETUP_MENU_DIFFICULTY_MAX);                  /* 500 */

                if (clear_flg_ctrl.difficulty_flg.IsUp(setup_menu_ctrl.difficulty_csr) != 0)
                {
                    break;
                }
            }                                                           /* 506 */

            if (csr_back_up != setup_menu_ctrl.difficulty_csr)          /* 509 */
            {
                SystemBankPlay(SE_CURSOR, 1, 0, 0, (SND_3D_SET *)0, 0x3200, 0x1000);  /* 510 */
            }
            else
            {
                SystemBankPlay(SE_ERROR, 1, 0, 0, (SND_3D_SET *)0, 0x3200, 0x1000);   /* 513 */
            }
            break;

        case SETUP_MENU_SETUP_CSR_START:
            /* Nothing to slide left on the commit row. */
            break;

        default:
            PRINT_ASSERT("Error! %s", __FUNCTION__);                    /* 517 */
            break;
        }
    }
    else if (((pad[0].rpt & 0x2000U) != 0) || (GetPadAnalogRpt(3) != 0)) /* 522 */
    {
        switch (setup_menu_ctrl.setup_csr)                              /* 523 */
        {
        case SETUP_MENU_SETUP_CSR_COSTUME:
            csr_back_up = setup_menu_ctrl.costume_csr;                  /* 526 */

            for (i = 0; i < SETUP_MENU_COSTUME_MAX; i++) {              /* 528 */
                setup_menu_ctrl.costume_csr =
                    (char)((setup_menu_ctrl.costume_csr + 1) % SETUP_MENU_COSTUME_MAX);  /* 529 */

                if (clear_flg_ctrl.costume_flg.IsUp(setup_menu_ctrl.costume_csr) != 0)
                {
                    break;
                }
            }                                                           /* 535 */

            if (csr_back_up != setup_menu_ctrl.costume_csr)             /* 538 */
            {
                SystemBankPlay(SE_CURSOR, 1, 0, 0, (SND_3D_SET *)0, 0x3200, 0x1000);  /* 539 */
            }
            else
            {
                SystemBankPlay(SE_ERROR, 1, 0, 0, (SND_3D_SET *)0, 0x3200, 0x1000);   /* 542 */
            }
            break;

        case SETUP_MENU_SETUP_CSR_MIO_ACS:
            csr_back_up = setup_menu_ctrl.mio_csr;                      /* 546 */

            setup_menu_ctrl.mio_csr = (char)(setup_menu_ctrl.mio_csr ^ 1);  /* 548 */

            if ((setup_menu_ctrl.mio_csr == 1) &&
                (clear_flg_ctrl.accessory_flg.IsUp(setup_menu_ctrl.setup_csr) == 0))  /* 551 */
            {
                setup_menu_ctrl.mio_csr = 0;
            }

            if (csr_back_up != setup_menu_ctrl.mio_csr)                 /* 558 */
            {
                SystemBankPlay(SE_CURSOR, 1, 0, 0, (SND_3D_SET *)0, 0x3200, 0x1000);  /* 559 */
            }
            else
            {
                SystemBankPlay(SE_ERROR, 1, 0, 0, (SND_3D_SET *)0, 0x3200, 0x1000);   /* 562 */
            }
            break;

        case SETUP_MENU_SETUP_CSR_MAYU_ACS:
            csr_back_up = setup_menu_ctrl.mayu_csr;                     /* 566 */

            setup_menu_ctrl.mayu_csr = (char)(setup_menu_ctrl.mayu_csr ^ 1);  /* 568 */

            if ((setup_menu_ctrl.mayu_csr == 1) &&
                (clear_flg_ctrl.accessory_flg.IsUp(setup_menu_ctrl.setup_csr) == 0))  /* 571 */
            {
                setup_menu_ctrl.mayu_csr = 0;
            }

            if (csr_back_up != setup_menu_ctrl.mayu_csr)                /* 579 */
            {
                SystemBankPlay(SE_CURSOR, 1, 0, 0, (SND_3D_SET *)0, 0x3200, 0x1000);  /* 580 */
            }
            else
            {
                SystemBankPlay(SE_ERROR, 1, 0, 0, (SND_3D_SET *)0, 0x3200, 0x1000);   /* 583 */
            }
            break;

        case SETUP_MENU_SETUP_CSR_DIFFICULTY:
            csr_back_up = setup_menu_ctrl.difficulty_csr;               /* 587 */

            for (i = 0; i < SETUP_MENU_DIFFICULTY_MAX; i++) {           /* 589 */
                setup_menu_ctrl.difficulty_csr =
                    (char)((setup_menu_ctrl.difficulty_csr + 1) % SETUP_MENU_DIFFICULTY_MAX);  /* 590 */

                if (clear_flg_ctrl.difficulty_flg.IsUp(setup_menu_ctrl.difficulty_csr) != 0)
                {
                    break;
                }
            }                                                           /* 596 */

            if (csr_back_up != setup_menu_ctrl.difficulty_csr)          /* 599 */
            {
                SystemBankPlay(SE_CURSOR, 1, 0, 0, (SND_3D_SET *)0, 0x3200, 0x1000);  /* 600 */
            }
            else
            {
                SystemBankPlay(SE_ERROR, 1, 0, 0, (SND_3D_SET *)0, 0x3200, 0x1000);   /* 603 */
            }
            break;

        case SETUP_MENU_SETUP_CSR_START:
            break;

        default:
            PRINT_ASSERT("Error! %s", __FUNCTION__);                    /* 607 */
            break;
        }
    }
    else if (*paddat[0] == 1)                                           /* 611 */
    {
        if (setup_menu_ctrl.setup_csr == SETUP_MENU_SETUP_CSR_START)    /* 612 */
        {
            SystemBankPlay(SE_DECIDE, 1, 0, 0, (SND_3D_SET *)0, 0x3200, 0x1000);  /* 613 */

            switch (setup_menu_ctrl.menu_csr)                           /* 615 */
            {
            case SETUP_MENU_CSR_STORY:
                SetupMenuGoToGame();                                    /* 617 */
                break;                                                  /* 618 */

            case SETUP_MENU_CSR_MISSION:
                SetupMenuGotoMission();                                 /* 620 */
                break;                                                  /* 621 */

            default:
                PRINT_ASSERT("Error! %s", __FUNCTION__);                /* 623 */
                break;
            }

            /* The commit.  Everything the ingame side reads comes from here;
             * the menu keeps no copy of its own. */
            SetPlyrMdlNo(costume_tbl[setup_menu_ctrl.costume_csr].mMioMdl);   /* 627 */
            SetSisterMdlNo(costume_tbl[setup_menu_ctrl.costume_csr].mMayuMdl); /* 628 */

            if (setup_menu_ctrl.mio_csr == 1)                           /* 631 */
            {
                SetPlyrAcsNo(costume_tbl[setup_menu_ctrl.costume_csr].mMioAcsMdl); /* 632 */
            }
            else
            {
                SetPlyrAcsNo(-1);                                       /* 635 */
            }

            if (setup_menu_ctrl.mayu_csr == 1)                          /* 637 */
            {
                SetSisterAcsNo(costume_tbl[setup_menu_ctrl.costume_csr].mMayuAcsMdl); /* 638 */
            }
            else
            {
                SetSisterAcsNo(-1);                                     /* 641 */
            }

            ingame_wrk.mDifficulty.Set(setup_menu_ctrl.difficulty_csr); /* 644 */
        }
    }
    else if (*paddat[1] == 1)                                           /* 649 */
    {
        SystemBankPlay(SE_CANCEL, 1, 0, 0, (SND_3D_SET *)0, 0x3200, 0x1000);  /* 650 */
        setup_menu_ctrl.mode = SETUP_MENU_MODE_SEL_MENU;                /* 651 */
    }
}

/* The exit window.  conf_csr 0 is Yes and 1 is No, and LEFT/RIGHT just flip
 * the bit -- there is no clamp because there are only two answers. */
static void SetupMenuExitConfPad(void)                                  /* 660 */
{
    if (((pad[0].rpt & 0x8000U) != 0) || (GetPadAnalogRpt(2) != 0) ||   /* 664 */
        ((pad[0].rpt & 0x2000U) != 0) || (GetPadAnalogRpt(3) != 0))     /* 669 */
    {
        SystemBankPlay(SE_CURSOR, 1, 0, 0, (SND_3D_SET *)0, 0x3200, 0x1000);  /* 670 */
        setup_menu_ctrl.conf_csr = (char)(setup_menu_ctrl.conf_csr ^ 1);      /* 671 */
    }
    else if (*paddat[0] == 1)                                           /* 673 */
    {
        SystemBankPlay(SE_DECIDE, 1, 0, 0, (SND_3D_SET *)0, 0x3200, 0x1000);  /* 674 */

        if (setup_menu_ctrl.conf_csr == 0)                              /* 677 */
        {
            SetupMenuExitReq();                                         /* 678 */
        }
        else
        {
            setup_menu_ctrl.mode = SETUP_MENU_MODE_SEL_MENU;            /* 682 */
        }
    }
    else if (*paddat[1] == 1)                                           /* 686 */
    {
        SystemBankPlay(SE_CANCEL, 1, 0, 0, (SND_3D_SET *)0, 0x3200, 0x1000);  /* 687 */
        setup_menu_ctrl.mode = SETUP_MENU_MODE_SEL_MENU;                /* 689 */
    }
}

// ──────────────────────────────────────────────────────────────────────
// The two hand-offs out of the menu.  Both leave step at SETUP_MENU_EXIT and
// let SetupMenuMain() do the phase change once the matching animation is over
// -- the black fade for the story, the menu's own fade-out for Mission Mode.

static void SetupMenuGoToGame(void)                                     /* 698 */
{
    setup_menu_ctrl.step = SETUP_MENU_EXIT;                             /* 701 */
    setup_menu_ctrl.next_place = SETUP_MENU_NEXT_GAME;                  /* 702 */
    setup_menu_disp.fade_anim_timer = 0;                                /* 703 */

    StreamAutoFadeOut(setup_menu_ctrl.stream_id, 5);                    /* 706 */
}

static void SetupMenuGotoMission(void)                                  /* 714 */
{
    setup_menu_ctrl.step = SETUP_MENU_EXIT;                             /* 717 */
    setup_menu_ctrl.next_place = SETUP_MENU_NEXT_MISSION;               /* 718 */
    setup_menu_disp.anim_step = 3;                                      /* 719 */
    setup_menu_disp.anim_timer = 0;                                     /* 720 */

    StreamAutoFadeOut(setup_menu_ctrl.stream_id, 5);                    /* 723 */
}

// ──────────────────────────────────────────────────────────────────────
// Drawing.

static void SetupMenuDispInit(void)                                     /* 739 */
{
    setup_menu_disp.anim_step = 0;                                      /* 742 */
    setup_menu_disp.anim_timer = 0;                                     /* 743 */
    setup_menu_disp.csr_timer = 0;                                      /* 744 */
    setup_menu_disp.menu_csr_timer = 0;                                 /* 745 */
    setup_menu_disp.sel_anim_timer = 0;                                 /* 746 */
    setup_menu_disp.fade_anim_timer = 0;                                /* 747 */
}

void SetupMenuDispMain(void)                                            /* 755 */
{
    u_char alpha;

    if ((setup_menu_ctrl.step == SETUP_MENU_MAIN) ||
        (setup_menu_ctrl.step == SETUP_MENU_EXIT))                      /* 763 */
    {
        alpha = Zero2Anim2D_InOutAnimCtrl(&setup_menu_disp.anim_step,
                                          &setup_menu_disp.anim_timer, 10, 5);  /* 764 */

        if (setup_menu_disp.anim_step != SETUP_MENU_ANIM_END)           /* 766 */
        {
            /* The frame is the shared OUTGAME part; the title plate beneath it
             * comes out of this mode's own background pak. */
            SetupMenuTitleFrameDisp(0, 0, alpha, GetOutGameCmnTexAddr()); /* 768 */
            SetupMenuTitleDisp(0, 0, alpha, GetSetupBgPk2Addr());       /* 771 */

            setup_menu_disp_func[setup_menu_ctrl.mode](0, 0, alpha);    /* 773 */

            SetupMenuCaptionDisp(0, 0, alpha);                          /* 776 */
        }

        if ((setup_menu_ctrl.step == SETUP_MENU_EXIT) &&                /* 779 */
            (setup_menu_ctrl.next_place == SETUP_MENU_NEXT_GAME))       /* 781 */
        {
            alpha = Zero2Anim2D_FadeInAnimCtrl(&setup_menu_disp.fade_anim_timer,
                                               SETUP_MENU_FADE_TIME);   /* 782 */
            SetupBlackBgDisp(0, 0, alpha);                              /* 784 */
        }
    }
}

/* Mode 0: the left-hand menu on its own, with the settings column behind it at
 * a fixed dim.  msg_id_tbl is the caption under each row.
 *
 * Note the two alpha terms: the ROM writes the non-selected menu alpha with a
 * shift and the settings-column alpha with a divide, and GCC's output keeps
 * them apart (a plain srl against the signed-division bias sequence).  They
 * agree for every u_char input. */
static void SetupMenuSelMenuDisp(int off_x, int off_y, u_char alpha)     /* 798 */
{
    int i;
    u_char non_sel_alpha;
    u_char setup_alpha;
    u_char rgb;
    static int msg_id_tbl[SETUP_MENU_CSR_MAX] =                         /* rodata 3c6d08 */
    {
        0, 1, 2
    };

    non_sel_alpha = (u_char)((int)alpha * 51 >> 7);                     /* 813 */
    setup_alpha = (u_char)((int)alpha * 38 / 128);                      /* 814 */
    rgb = 0x80;                                                         /* 815 */

    PK2SendVram((uintptr_t)GetSetupFontPk2Addr(), -1, -1, 0);    /* 818 */

    for (i = 0; i < SETUP_MENU_CSR_MAX; i++) {                          /* 819 */
        if (setup_menu_ctrl.menu_csr == i)                              /* 820 */
        {
            SetupMenuSelMenuItemDisp(i, off_x, off_y, alpha, 0x80);     /* 822 */
        }
        else
        {
            SetupMenuSelMenuItemDisp(i, off_x, off_y, non_sel_alpha, 0x80); /* 826 */
        }
    }                                                                   /* 828 */

    /* Every settings-column part is drawn at setup_alpha with flg 0, i.e. the
     * ordinary blend -- nothing on this screen is selected yet.  The costume
     * and difficulty plates take 0, 0 for their offsets while the label beside
     * them takes off_x / off_y; both callers pass 0, so it never shows. */
    SetupMenuCostumeDisp(off_x, off_y, setup_alpha, 0x80, 0);           /* 831 */
    SetupMenuCostumeTypeDisp(setup_menu_ctrl.costume_csr, 0, 0, setup_alpha, 0x80, 0); /* 834 */
    SetupMenuAccessoryDisp(off_x, off_y, setup_alpha, 0x80, 0);         /* 837 */
    SetupMenuMioDisp(off_x, off_y, setup_alpha, 0x80, 0);               /* 840 */
    SetupMenuMayuDisp(off_x, off_y, setup_alpha, 0x80, 0);              /* 843 */

    setup_on_off_disp_func[setup_menu_ctrl.mio_csr](off_x + 444, off_y + 211,
                                                    setup_alpha, 0x80, 0);  /* 846 */
    setup_on_off_disp_func[setup_menu_ctrl.mayu_csr](off_x + 444, off_y + 241,
                                                     setup_alpha, 0x80, 0); /* 849 */

    if (setup_menu_ctrl.menu_csr == SETUP_MENU_CSR_STORY)               /* 852 */
    {
        SetupMenuDifficultyDisp(setup_menu_ctrl.difficulty_csr, off_x, off_y,
                                setup_alpha, 0x80, 0);                  /* 854 */
    }

    for (i = 0; i < 2; i++) {                                           /* 857 */
        SetupMenuLineDisp(off_x, off_y + i * 171, setup_alpha, 0x80);   /* 858 */
    }                                                                   /* 859 */

    SetupMenuTopCursorFrameDisp(off_x, off_y, alpha, 0x80);             /* 862 */

    Zero2Anim2D_CsrAnimCtrl(&setup_menu_disp.menu_csr_timer, &rgb);     /* 864 */

    SetupMenuTopCursorDisp(0, 0, alpha, rgb);                           /* 867 */

    SetupMenuMsgWinDisp(off_x, off_y, alpha);                           /* 870 */

    PrintMsg(0x4c, msg_id_tbl[setup_menu_ctrl.menu_csr], 0x44, 0x171, 1, alpha, 0);  /* 874 */
}

/* Mode 1: the settings column live.  Everything is drawn twice over -- once
 * bright with flg 1 for the row under the cursor and once at half alpha with
 * flg 0 for the rest -- so the highlight is an alpha and a blend mode, not a
 * separate sprite. */
static void SetupMenuSelSetupDisp(int off_x, int off_y, u_char alpha)   /* 885 */
{
    int i;
    u_char menu_sel_alpha;
    u_char menu_non_sel_alpha;
    u_char non_sel_alpha;
    u_char line_alpha;
    static int msg_id_tbl[SETUP_MENU_SETUP_CSR_MAX] =                   /* rodata 3c6d18 */
    {
        3, 4, 4, 5, 6
    };

    /* The left-hand row keeps pulsing while the column is open, but only once
     * the screen has finished opening. */
    if (setup_menu_disp.anim_step == SETUP_MENU_ANIM_IN_END)            /* 902 */
    {
        menu_sel_alpha = Zero2Anim2D_SelAnimCtrl(&setup_menu_disp.sel_anim_timer);  /* 903 */
    }
    else
    {
        menu_sel_alpha = alpha;                                         /* 906 */
    }

    menu_non_sel_alpha = (u_char)((int)alpha * 51 >> 7);                /* 908 */
    non_sel_alpha = (u_char)((int)alpha >> 1);                          /* 910 */
    line_alpha = (u_char)((int)alpha * 76 / 128);                       /* 911 */

    PK2SendVram((uintptr_t)GetSetupFontPk2Addr(), -1, -1, 0);    /* 914 */

    for (i = 0; i < SETUP_MENU_CSR_MAX; i++) {                          /* 917 */
        if (setup_menu_ctrl.menu_csr == i)                              /* 918 */
        {
            SetupMenuSelMenuItemDisp(i, off_x, off_y, menu_sel_alpha, 0x80);      /* 919 */
        }
        else
        {
            SetupMenuSelMenuItemDisp(i, off_x, off_y, menu_non_sel_alpha, 0x80);  /* 922 */
        }
    }                                                                   /* 924 */

    SetupMenuTopCursorFrameDisp(off_x, off_y, alpha, 0x80);             /* 927 */

    if (setup_menu_ctrl.setup_csr == SETUP_MENU_SETUP_CSR_COSTUME)      /* 929 */
    {
        SetupMenuCostumeDisp(0, 0, alpha, 0x80, 1);                     /* 931 */
        SetupMenuCostumeTypeDisp(setup_menu_ctrl.costume_csr, 0, 0, alpha, 0x80, 1);  /* 933 */
    }
    else
    {
        SetupMenuCostumeDisp(0, 0, non_sel_alpha, 0x80, 0);             /* 937 */
        SetupMenuCostumeTypeDisp(setup_menu_ctrl.costume_csr, 0, 0, non_sel_alpha, 0x80, 0);  /* 939 */
    }

    /* The "ACCESSORY" heading lights up for either of its two rows, which is
     * why this is a switch over the pair rather than two independent tests. */
    switch (setup_menu_ctrl.setup_csr)                                  /* 943 */
    {
    case SETUP_MENU_SETUP_CSR_MIO_ACS:
        SetupMenuAccessoryDisp(0, 0, alpha, 0x80, 1);                   /* 946 */
        SetupMenuMioDisp(0, 0, alpha, 0x80, 1);                         /* 949 */
        SetupMenuMayuDisp(0, 0, non_sel_alpha, 0x80, 0);                /* 952 */
        break;                                                          /* 953 */

    case SETUP_MENU_SETUP_CSR_MAYU_ACS:
        SetupMenuAccessoryDisp(0, 0, alpha, 0x80, 1);                   /* 956 */
        SetupMenuMioDisp(0, 0, non_sel_alpha, 0x80, 0);                 /* 959 */
        SetupMenuMayuDisp(0, 0, alpha, 0x80, 1);                        /* 962 */
        break;                                                          /* 963 */

    default:
        SetupMenuAccessoryDisp(0, 0, non_sel_alpha, 0x80, 0);           /* 966 */
        SetupMenuMioDisp(0, 0, non_sel_alpha, 0x80, 0);                 /* 969 */
        SetupMenuMayuDisp(0, 0, non_sel_alpha, 0x80, 0);                /* 972 */
        break;
    }

    if (setup_menu_ctrl.menu_csr == SETUP_MENU_CSR_STORY)               /* 977 */
    {
        if (setup_menu_ctrl.setup_csr == SETUP_MENU_SETUP_CSR_DIFFICULTY)  /* 978 */
        {
            SetupMenuDifficultyDisp(setup_menu_ctrl.difficulty_csr, 0, 0, alpha, 0x80, 1);  /* 979 */
        }
        else
        {
            SetupMenuDifficultyDisp(setup_menu_ctrl.difficulty_csr, 0, 0,
                                    non_sel_alpha, 0x80, 0);            /* 982 */
        }
    }

    if (setup_menu_ctrl.setup_csr == SETUP_MENU_SETUP_CSR_MIO_ACS)      /* 987 */
    {
        setup_on_off_disp_func[setup_menu_ctrl.mio_csr](off_x + 444, off_y + 211,
                                                        alpha, 0x80, 1);    /* 988 */
    }
    else
    {
        setup_on_off_disp_func[setup_menu_ctrl.mio_csr](off_x + 444, off_y + 211,
                                                        non_sel_alpha, 0x80, 0);  /* 991 */
    }

    if (setup_menu_ctrl.setup_csr == SETUP_MENU_SETUP_CSR_MAYU_ACS)     /* 995 */
    {
        setup_on_off_disp_func[setup_menu_ctrl.mayu_csr](off_x + 444, off_y + 241,
                                                         alpha, 0x80, 1);   /* 996 */
    }
    else
    {
        setup_on_off_disp_func[setup_menu_ctrl.mayu_csr](off_x + 444, off_y + 241,
                                                         non_sel_alpha, 0x80, 0);  /* 999 */
    }

    if (setup_menu_ctrl.setup_csr == SETUP_MENU_SETUP_CSR_START)        /* 1002 */
    {
        if (setup_menu_ctrl.menu_csr == SETUP_MENU_CSR_STORY)           /* 1003 */
        {
            SetupMenuGameStartDisp(off_x, off_y, alpha, 0x80, 1);       /* 1005 */
        }
        else
        {
            SetupMenuMissionSelectDisp(off_x, off_y, alpha, 0x80, 1);   /* 1009 */
        }
    }
    else
    {
        if (setup_menu_ctrl.menu_csr == SETUP_MENU_CSR_STORY)           /* 1013 */
        {
            SetupMenuGameStartDisp(off_x, off_y, non_sel_alpha, 0x80, 0);      /* 1015 */
        }
        else
        {
            SetupMenuMissionSelectDisp(off_x, off_y, non_sel_alpha, 0x80, 0);  /* 1019 */
        }
    }

    SetupMenuCursorDisp(off_x, off_y, alpha, 0x80);                     /* 1024 */

    for (i = 0; i < 2; i++) {                                           /* 1027 */
        SetupMenuLineDisp(off_x, off_y + i * 171, line_alpha, 0x80);    /* 1028 */
    }                                                                   /* 1029 */

    SetupMenuMsgWinDisp(off_x, off_y, alpha);                           /* 1032 */

    /* Mission Mode's commit row has a caption of its own; every other row
     * shares the story table, and the two calls really are two separate
     * bodies in the ROM. */
    if (setup_menu_ctrl.menu_csr == SETUP_MENU_CSR_MISSION)             /* 1035 */
    {
        if (setup_menu_ctrl.setup_csr == SETUP_MENU_SETUP_CSR_START)    /* 1036 */
        {
            PrintMsg(0x4c, 7, 0x44, 0x171, 1, alpha, 0);                /* 1038 */
        }
        else
        {
            PrintMsg(0x4c, msg_id_tbl[setup_menu_ctrl.setup_csr], 0x44, 0x171, 1, alpha, 0);  /* 1042 */
        }
    }
    else
    {
        PrintMsg(0x4c, msg_id_tbl[setup_menu_ctrl.setup_csr], 0x44, 0x171, 1, alpha, 0);  /* 1047 */
    }
}

/* Mode 2: the whole screen dimmed with the yes/no window on top.  The settings
 * column is drawn exactly as mode 0 draws it -- the difference is that the
 * left-hand row keeps its pulse and the difficulty row is left out entirely. */
static void SetupMenuExitConfDisp(int off_x, int off_y, u_char alpha)   /* 1059 */
{
    int i;
    u_char sel_alpha;
    u_char non_sel_alpha;
    u_char setup_alpha;

    if (setup_menu_disp.anim_step == SETUP_MENU_ANIM_IN_END)            /* 1069 */
    {
        sel_alpha = Zero2Anim2D_SelAnimCtrl(&setup_menu_disp.sel_anim_timer);  /* 1070 */
    }
    else
    {
        sel_alpha = alpha;                                              /* 1073 */
    }

    non_sel_alpha = (u_char)((int)alpha * 51 >> 7);                     /* 1076 */
    setup_alpha = (u_char)((int)alpha * 38 / 128);                      /* 1077 */

    PK2SendVram((uintptr_t)GetSetupFontPk2Addr(), -1, -1, 0);    /* 1081 */

    for (i = 0; i < SETUP_MENU_CSR_MAX; i++) {                          /* 1082 */
        if (setup_menu_ctrl.menu_csr == i)                              /* 1083 */
        {
            SetupMenuSelMenuItemDisp(i, off_x, off_y, sel_alpha, 0x80);      /* 1085 */
        }
        else
        {
            SetupMenuSelMenuItemDisp(i, off_x, off_y, non_sel_alpha, 0x80);  /* 1089 */
        }
    }                                                                   /* 1091 */

    SetupMenuCostumeDisp(off_x, off_y, setup_alpha, 0x80, 0);           /* 1094 */
    SetupMenuCostumeTypeDisp(setup_menu_ctrl.costume_csr, 0, 0, setup_alpha, 0x80, 0);  /* 1097 */
    SetupMenuAccessoryDisp(off_x, off_y, setup_alpha, 0x80, 0);         /* 1100 */
    SetupMenuMioDisp(off_x, off_y, setup_alpha, 0x80, 0);               /* 1103 */
    SetupMenuMayuDisp(off_x, off_y, setup_alpha, 0x80, 0);              /* 1106 */

    setup_on_off_disp_func[setup_menu_ctrl.mio_csr](off_x + 444, off_y + 211,
                                                    setup_alpha, 0x80, 0);  /* 1109 */
    setup_on_off_disp_func[setup_menu_ctrl.mayu_csr](off_x + 444, off_y + 241,
                                                     setup_alpha, 0x80, 0); /* 1112 */

    for (i = 0; i < 2; i++) {                                           /* 1117 */
        SetupMenuLineDisp(off_x, off_y + i * 171, setup_alpha, 0x80);   /* 1118 */
    }                                                                   /* 1119 */

    SetupMenuTopCursorFrameDisp(off_x, off_y, alpha, 0x80);             /* 1122 */

    SetupMenuMsgWinDisp(off_x, off_y, alpha);                           /* 1130 */

    PrintMsg(0x4c, 8, 0x44, 0x171, 1, alpha, 0);                        /* 1134 */

    DrawCmnYesNoSel(setup_menu_ctrl.conf_csr, 396.0f, alpha, 0);        /* 1137 */
}

// ──────────────────────────────────────────────────────────────────────
// Per-part drawing.  All of these follow the folder's shape: copy a
// setup_tex[] record into a DISP_SPRT, offset it, scale its alpha, draw.
//
// `flg` is "this row is under the cursor" and its only effect is to swap the
// blend to additive (alphar 0x48); the brightening itself comes from the
// caller handing over a bigger alpha.

static void SetupMenuTitleFrameDisp(int off_x, int off_y, u_char alpha, void *pk2_addr) /* 1150 */
{
    int i;
    DISP_SPRT frame_ds;

    PK2SendVram((uintptr_t)pk2_addr, -1, -1, 0);                 /* 1155 */

    for (i = 0; i < 2; i++) {                                           /* 1158 */
        CopySprDToSpr(&frame_ds, &setup_tex[6 + i]);                    /* 1159 */

        frame_ds.x = frame_ds.x + (float)off_x;                         /* 1160 */
        frame_ds.y = frame_ds.y + (float)off_y;                         /* 1160 */

        frame_ds.alpha = (u_char)(((int)frame_ds.alpha * (int)alpha) >> 7);  /* 1161 */

        DispSprD(&frame_ds);                                            /* 1162 */
    }                                                                   /* 1163 */
}

static void SetupMenuTitleDisp(int off_x, int off_y, u_char alpha, void *pk2_addr) /* 1175 */
{
    DISP_SPRT title_ds;

    PK2SendVram((uintptr_t)pk2_addr, -1, -1, 0);                 /* 1179 */

    CopySprDToSpr(&title_ds, &setup_tex[8]);                            /* 1182 */

    title_ds.x = title_ds.x + (float)off_x;                             /* 1183 */
    title_ds.y = title_ds.y + (float)off_y;                             /* 1183 */

    title_ds.alpha = (u_char)(((int)title_ds.alpha * (int)alpha) >> 7); /* 1184 */

    DispSprD(&title_ds);                                                /* 1185 */
}

/* One of the three left-hand rows.  Row 2 (Exit) is two sprites wide, the
 * other two are one, which is what the -1 in the table means. */
static void SetupMenuSelMenuItemDisp(int menu_label, int off_x, int off_y,
                                     u_char alpha, u_char rgb)          /* 1198 */
{
    int i;
    DISP_SPRT menu_ds;
    static int menu_tex_tbl[SETUP_MENU_CSR_MAX][2] =                    /* rodata 3c6d50 */
    {
        { 12, -1 },
        { 11, -1 },
        {  9, 10 }
    };

    /* The three range checks in this file compile to a single `sltiu`, unlike
     * BIT_FLAGS::IsUp()'s signed `slti` -- so the ROM's own test is unsigned
     * (or a folded two-sided one), not the `N <= x` the template uses. */
    if ((u_int)menu_label >= (u_int)SETUP_MENU_CSR_MAX)                 /* 1211 */
    {
        PRINT_ASSERT("Error! %s", __FUNCTION__);                        /* 1212 */
    }

    for (i = 0; i < 2; i++) {                                           /* 1216 */
        if (menu_tex_tbl[menu_label][i] != -1)                          /* 1217 */
        {
            CopySprDToSpr(&menu_ds, &setup_tex[menu_tex_tbl[menu_label][i]]);  /* 1218 */

            menu_ds.x = menu_ds.x + (float)off_x;                       /* 1219 */
            menu_ds.y = menu_ds.y + (float)off_y;                       /* 1219 */

            menu_ds.alpha = (u_char)(((int)menu_ds.alpha * (int)alpha) >> 7);  /* 1220 */

            menu_ds.r = rgb;                                            /* 1221 */
            menu_ds.g = rgb;                                            /* 1221 */
            menu_ds.b = rgb;                                            /* 1221 */

            if (setup_menu_ctrl.menu_csr == menu_label)                 /* 1222 */
            {
                menu_ds.alphar = 0x48;
            }

            DispSprD(&menu_ds);                                         /* 1226 */
        }
    }                                                                   /* 1228 */
}

/* The frame around the left-hand cursor.  Four parts, all stepped 40 pixels
 * per row -- the rows are 45 / 85 / 125. */
static void SetupMenuTopCursorFrameDisp(int off_x, int off_y, u_char alpha, u_char rgb) /* 1241 */
{
    int i;
    DISP_SPRT csr_ds;

    for (i = 0; i < 4; i++) {                                           /* 1247 */
        CopySprDToSpr(&csr_ds, &setup_tex[47 + i]);                     /* 1248 */

        csr_ds.x = csr_ds.x + (float)off_x;                             /* 1249 */
        csr_ds.y = csr_ds.y + (float)off_y + (float)(setup_menu_ctrl.menu_csr * 40);  /* 1249 */

        csr_ds.alpha = (u_char)(((int)csr_ds.alpha * (int)alpha) >> 7); /* 1250 */

        csr_ds.r = rgb;                                                 /* 1251 */
        csr_ds.g = rgb;                                                 /* 1251 */
        csr_ds.b = rgb;                                                 /* 1251 */

        csr_ds.alphar = 0x48;                                           /* 1252 */

        DispSprD(&csr_ds);                                              /* 1254 */
    }                                                                   /* 1255 */
}

/* The left-hand cursor itself: a flare and the arrow, twice (left and right of
 * the row).  Both are rotated a quarter turn about a centre that is the
 * sprite's own position offset down by its width, which is what `rot` needs
 * after the 270-degree turn.  Only the flare carries rgb and the additive
 * blend -- the arrow keeps whatever CopySprDToSpr() left, i.e. white and the
 * ordinary blend. */
static void SetupMenuTopCursorDisp(int off_x, int off_y, u_char alpha, u_char rgb) /* 1268 */
{
    int i;
    DISP_SPRT csr_ds;
    static int csr_tex_tbl[2] = { 51, 52 };                             /* sdata 3f42f8 */
    static int csr_flare_tex_tbl[2] = { 53, 54 };                       /* sdata 3f4300 */

    for (i = 0; i < 2; i++) {                                           /* 1284 */
        CopySprDToSpr(&csr_ds, &setup_tex[csr_flare_tex_tbl[i]]);       /* 1286 */

        csr_ds.x = csr_ds.x + (float)off_x;                             /* 1287 */
        csr_ds.y = csr_ds.y + (float)csr_ds.w + (float)off_y +
                   (float)(setup_menu_ctrl.menu_csr * 40);              /* 1287 */

        csr_ds.rot = 270.0f;                                            /* 1288 */
        csr_ds.crx = csr_ds.x;                                          /* 1288 */
        csr_ds.cry = csr_ds.y;                                          /* 1288 */

        csr_ds.alpha = (u_char)(((int)csr_ds.alpha * (int)alpha) >> 7); /* 1289 */

        csr_ds.r = rgb;                                                 /* 1290 */
        csr_ds.g = rgb;                                                 /* 1290 */
        csr_ds.b = rgb;                                                 /* 1290 */

        csr_ds.alphar = 0x48;                                           /* 1291 */

        DispSprD(&csr_ds);                                              /* 1293 */

        CopySprDToSpr(&csr_ds, &setup_tex[csr_tex_tbl[i]]);             /* 1295 */

        csr_ds.x = csr_ds.x + (float)off_x;                             /* 1296 */
        csr_ds.y = csr_ds.y + (float)csr_ds.w + (float)off_y +
                   (float)(setup_menu_ctrl.menu_csr * 40);              /* 1296 */

        csr_ds.rot = 270.0f;                                            /* 1297 */
        csr_ds.crx = csr_ds.x;                                          /* 1297 */
        csr_ds.cry = csr_ds.y;                                          /* 1297 */

        csr_ds.alpha = (u_char)(((int)csr_ds.alpha * (int)alpha) >> 7); /* 1298 */

        DispSprD(&csr_ds);                                              /* 1299 */
    }                                                                   /* 1300 */
}

static void SetupMenuCostumeDisp(int off_x, int off_y, u_char alpha, u_char rgb, u_char flg) /* 1313 */
{
    DISP_SPRT costume_ds;

    CopySprDToSpr(&costume_ds, &setup_tex[13]);                         /* 1318 */

    costume_ds.x = costume_ds.x + (float)off_x;                         /* 1319 */
    costume_ds.y = costume_ds.y + (float)off_y;                         /* 1319 */

    costume_ds.alpha = (u_char)(((int)costume_ds.alpha * (int)alpha) >> 7);  /* 1320 */

    costume_ds.r = rgb;                                                 /* 1321 */
    costume_ds.g = rgb;                                                 /* 1321 */
    costume_ds.b = rgb;                                                 /* 1321 */

    if (flg == 1)                                                       /* 1322 */
    {
        costume_ds.alphar = 0x48;                                       /* 1323 */
    }

    DispSprD(&costume_ds);                                              /* 1325 */
}

/* Costume 0 is a single plate; 1..8 are a shared word plus a digit. */
static void SetupMenuCostumeTypeDisp(int costume_type, int off_x, int off_y,
                                     u_char alpha, u_char rgb, u_char flg)  /* 1339 */
{
    int i;
    DISP_SPRT costume_ds;
    static int costume_type_tbl[SETUP_MENU_COSTUME_MAX][2] =            /* rodata 3c6d88 */
    {
        { 14, -1 },
        { 15, 16 },
        { 15, 17 },
        { 15, 18 },
        { 15, 19 },
        { 15, 20 },
        { 15, 21 },
        { 15, 22 },
        { 15, 23 }
    };

    if ((u_int)costume_type >= (u_int)SETUP_MENU_COSTUME_MAX)           /* 1357 */
    {
        PRINT_ASSERT("Error! %s", __FUNCTION__);                        /* 1358 */
    }

    for (i = 0; i < 2; i++) {                                           /* 1362 */
        if (costume_type_tbl[costume_type][i] != -1)                    /* 1363 */
        {
            CopySprDToSpr(&costume_ds, &setup_tex[costume_type_tbl[costume_type][i]]);  /* 1364 */

            costume_ds.x = costume_ds.x + (float)off_x;                 /* 1365 */
            costume_ds.y = costume_ds.y + (float)off_y;                 /* 1365 */

            costume_ds.alpha = (u_char)(((int)costume_ds.alpha * (int)alpha) >> 7);  /* 1366 */

            costume_ds.r = rgb;                                         /* 1367 */
            costume_ds.g = rgb;                                         /* 1367 */
            costume_ds.b = rgb;                                         /* 1367 */

            if (flg == 1)                                               /* 1368 */
            {
                costume_ds.alphar = 0x48;
            }

            DispSprD(&costume_ds);                                      /* 1371 */
        }
    }                                                                   /* 1373 */
}

static void SetupMenuAccessoryDisp(int off_x, int off_y, u_char alpha, u_char rgb, u_char flg) /* 1386 */
{
    DISP_SPRT accessory_ds;

    CopySprDToSpr(&accessory_ds, &setup_tex[24]);                       /* 1391 */

    accessory_ds.x = accessory_ds.x + (float)off_x;                     /* 1392 */
    accessory_ds.y = accessory_ds.y + (float)off_y;                     /* 1392 */

    accessory_ds.alpha = (u_char)(((int)accessory_ds.alpha * (int)alpha) >> 7);  /* 1393 */

    accessory_ds.r = rgb;                                               /* 1394 */
    accessory_ds.g = rgb;                                               /* 1394 */
    accessory_ds.b = rgb;                                               /* 1394 */

    if (flg == 1)                                                       /* 1395 */
    {
        accessory_ds.alphar = 0x48;                                     /* 1396 */
    }

    DispSprD(&accessory_ds);                                            /* 1398 */
}

static void SetupMenuMioDisp(int off_x, int off_y, u_char alpha, u_char rgb, u_char flg) /* 1411 */
{
    DISP_SPRT mio_ds;

    CopySprDToSpr(&mio_ds, &setup_tex[25]);                             /* 1416 */

    mio_ds.x = mio_ds.x + (float)off_x;                                 /* 1417 */
    mio_ds.y = mio_ds.y + (float)off_y;                                 /* 1417 */

    mio_ds.alpha = (u_char)(((int)mio_ds.alpha * (int)alpha) >> 7);     /* 1418 */

    mio_ds.r = rgb;                                                     /* 1419 */
    mio_ds.g = rgb;                                                     /* 1419 */
    mio_ds.b = rgb;                                                     /* 1419 */

    if (flg == 1)                                                       /* 1420 */
    {
        mio_ds.alphar = 0x48;                                           /* 1421 */
    }

    DispSprD(&mio_ds);                                                  /* 1423 */
}

static void SetupMenuMayuDisp(int off_x, int off_y, u_char alpha, u_char rgb, u_char flg) /* 1436 */
{
    DISP_SPRT mayu_ds;

    CopySprDToSpr(&mayu_ds, &setup_tex[26]);                            /* 1441 */

    mayu_ds.x = mayu_ds.x + (float)off_x;                               /* 1442 */
    mayu_ds.y = mayu_ds.y + (float)off_y;                               /* 1442 */

    mayu_ds.alpha = (u_char)(((int)mayu_ds.alpha * (int)alpha) >> 7);   /* 1443 */

    mayu_ds.r = rgb;                                                    /* 1444 */
    mayu_ds.g = rgb;                                                    /* 1444 */
    mayu_ds.b = rgb;                                                    /* 1444 */

    if (flg == 1)                                                       /* 1445 */
    {
        mayu_ds.alphar = 0x48;                                          /* 1446 */
    }

    DispSprD(&mayu_ds);                                                 /* 1448 */
}

/* The heading is unconditional; the word itself comes out of the table. */
static void SetupMenuDifficultyDisp(int difficulty_label, int off_x, int off_y,
                                    u_char alpha, u_char rgb, u_char flg)  /* 1462 */
{
    int i;
    DISP_SPRT difficulty_ds;
    static int difficulty_tex_tbl[SETUP_MENU_DIFFICULTY_MAX][2] =       /* rodata 3c6de8 */
    {
        { 30, -1 },
        { 31, -1 },
        { 32, 33 },
        { 34, -1 }
    };

    if ((u_int)difficulty_label >= (u_int)SETUP_MENU_DIFFICULTY_MAX)    /* 1476 */
    {
        PRINT_ASSERT("Error! %s", __FUNCTION__);                        /* 1477 */
    }

    CopySprDToSpr(&difficulty_ds, &setup_tex[29]);                      /* 1481 */

    difficulty_ds.x = difficulty_ds.x + (float)off_x;                   /* 1482 */
    difficulty_ds.y = difficulty_ds.y + (float)off_y;                   /* 1482 */

    difficulty_ds.alpha = (u_char)(((int)difficulty_ds.alpha * (int)alpha) >> 7);  /* 1483 */

    difficulty_ds.r = rgb;                                              /* 1484 */
    difficulty_ds.g = rgb;                                              /* 1484 */
    difficulty_ds.b = rgb;                                              /* 1484 */

    if (flg == 1)                                                       /* 1485 */
    {
        difficulty_ds.alphar = 0x48;                                    /* 1486 */
    }

    DispSprD(&difficulty_ds);                                           /* 1488 */

    for (i = 0; i < 2; i++) {                                           /* 1490 */
        if (difficulty_tex_tbl[difficulty_label][i] != -1)              /* 1491 */
        {
            CopySprDToSpr(&difficulty_ds,
                          &setup_tex[difficulty_tex_tbl[difficulty_label][i]]);  /* 1492 */

            difficulty_ds.x = difficulty_ds.x + (float)off_x;           /* 1493 */
            difficulty_ds.y = difficulty_ds.y + (float)off_y;           /* 1493 */

            difficulty_ds.alpha = (u_char)(((int)difficulty_ds.alpha * (int)alpha) >> 7);  /* 1494 */

            difficulty_ds.r = rgb;                                      /* 1495 */
            difficulty_ds.g = rgb;                                      /* 1495 */
            difficulty_ds.b = rgb;                                      /* 1495 */

            if (flg == 1)                                               /* 1496 */
            {
                difficulty_ds.alphar = 0x48;
            }

            DispSprD(&difficulty_ds);                                   /* 1500 */
        }
    }                                                                   /* 1502 */
}

static void SetupMenuGameStartDisp(int off_x, int off_y, u_char alpha, u_char rgb, u_char flg) /* 1515 */
{
    int i;
    DISP_SPRT gamestart_ds;

    for (i = 0; i < 2; i++) {                                           /* 1521 */
        CopySprDToSpr(&gamestart_ds, &setup_tex[37 + i]);               /* 1522 */

        gamestart_ds.x = gamestart_ds.x + (float)off_x;                 /* 1523 */
        gamestart_ds.y = gamestart_ds.y + (float)off_y;                 /* 1523 */

        gamestart_ds.alpha = (u_char)(((int)gamestart_ds.alpha * (int)alpha) >> 7);  /* 1524 */

        gamestart_ds.r = rgb;                                           /* 1525 */
        gamestart_ds.g = rgb;                                           /* 1525 */
        gamestart_ds.b = rgb;                                           /* 1525 */

        if (flg == 1)                                                   /* 1526 */
        {
            gamestart_ds.alphar = 0x48;
        }

        DispSprD(&gamestart_ds);                                        /* 1529 */
    }                                                                   /* 1530 */
}

static void SetupMenuMissionSelectDisp(int off_x, int off_y, u_char alpha, u_char rgb, u_char flg) /* 1543 */
{
    int i;
    DISP_SPRT mission_ds;

    for (i = 0; i < 2; i++) {                                           /* 1549 */
        CopySprDToSpr(&mission_ds, &setup_tex[35 + i]);                 /* 1550 */

        mission_ds.x = mission_ds.x + (float)off_x;                     /* 1551 */
        mission_ds.y = mission_ds.y + (float)off_y;                     /* 1551 */

        mission_ds.alpha = (u_char)(((int)mission_ds.alpha * (int)alpha) >> 7);  /* 1552 */

        mission_ds.r = rgb;                                             /* 1553 */
        mission_ds.g = rgb;                                             /* 1553 */
        mission_ds.b = rgb;                                             /* 1553 */

        if (flg == 1)                                                   /* 1554 */
        {
            mission_ds.alphar = 0x48;
        }

        DispSprD(&mission_ds);                                          /* 1557 */
    }                                                                   /* 1558 */
}

/* The settings column's own cursor: a left arrow at a fixed x, and a right
 * arrow whose x depends on how wide the value beside it is.  The commit row
 * gets no cursor at all.
 *
 * The difficulty branch's left-hand y is the literal 280 rather than the
 * `off_y + 190 + setup_csr * 30` its three siblings use -- the ROM computes it
 * with no reference to setup_csr, and the flare on the very next line does
 * recompute, so the asymmetry is the source's, not a fold. */
static void SetupMenuCursorDisp(int off_x, int off_y, u_char alpha, u_char rgb)  /* 1570 */
{
    u_char flare_rgb;
    /* The x each cursor arrow sits at, per row.  The commit row has no entry
     * -- the test below returns before either table would be indexed. */
    static int csr_right_x_tbl[3] = { 485, 540, 540 };                  /* rodata 3c6e08 */
    static int difficulty_csr_right_x_tbl[SETUP_MENU_DIFFICULTY_MAX] =  /* rodata 3c6e18 */
    {
        444, 485, 475, 550
    };
    static int flare_right_x_tbl[3] = { 482, 537, 537 };                /* rodata 3c6e28 */
    static int difficulty_flare_right_x_tbl[SETUP_MENU_DIFFICULTY_MAX] =/* rodata 3c6e38 */
    {
        441, 482, 472, 547
    };

    Zero2Anim2D_CsrAnimCtrl(&setup_menu_disp.csr_timer, &flare_rgb);    /* 1601 */

    if (setup_menu_ctrl.setup_csr != SETUP_MENU_SETUP_CSR_START)        /* 1605 */
    {
        SetupMenuCsrLDisp(off_x + 338, off_y + 190 + setup_menu_ctrl.setup_csr * 30,
                          alpha, rgb);                                  /* 1608 */
        SetupMenuCsrFlareLDisp(off_x + 334, off_y + 187 + setup_menu_ctrl.setup_csr * 30,
                               alpha, flare_rgb);                       /* 1612 */

        if (setup_menu_ctrl.setup_csr == SETUP_MENU_SETUP_CSR_DIFFICULTY)  /* 1614 */
        {
            SetupMenuCsrRDisp(off_x + difficulty_csr_right_x_tbl[setup_menu_ctrl.difficulty_csr],
                              off_y + 280, alpha, rgb);                 /* 1617 */
            SetupMenuCsrFlareRDisp(
                off_x + difficulty_flare_right_x_tbl[setup_menu_ctrl.difficulty_csr],
                off_y + 187 + setup_menu_ctrl.setup_csr * 30, alpha, flare_rgb);  /* 1621 */
        }
        else
        {
            SetupMenuCsrRDisp(off_x + csr_right_x_tbl[setup_menu_ctrl.setup_csr],
                              off_y + 190 + setup_menu_ctrl.setup_csr * 30, alpha, rgb);  /* 1626 */
            SetupMenuCsrFlareRDisp(off_x + flare_right_x_tbl[setup_menu_ctrl.setup_csr],
                                   off_y + 187 + setup_menu_ctrl.setup_csr * 30,
                                   alpha, flare_rgb);                   /* 1630 */
        }
    }
}

/* Both of these place the sprite outright rather than offsetting it -- the
 * caller has already worked out where the row is. */
static void SetupMenuOnDisp(int x, int y, u_char alpha, u_char rgb, u_char flg)  /* 1645 */
{
    DISP_SPRT on_ds;

    CopySprDToSpr(&on_ds, &setup_tex[27]);                              /* 1650 */

    on_ds.x = (float)x;                                                 /* 1651 */
    on_ds.y = (float)y;                                                 /* 1651 */

    on_ds.alpha = (u_char)(((int)on_ds.alpha * (int)alpha) >> 7);       /* 1652 */

    on_ds.r = rgb;                                                      /* 1653 */
    on_ds.g = rgb;                                                      /* 1653 */
    on_ds.b = rgb;                                                      /* 1653 */

    if (flg == 1)                                                       /* 1654 */
    {
        on_ds.alphar = 0x48;                                            /* 1655 */
    }

    DispSprD(&on_ds);                                                   /* 1658 */
}

static void SetupMenuOffDisp(int x, int y, u_char alpha, u_char rgb, u_char flg) /* 1671 */
{
    DISP_SPRT off_ds;

    CopySprDToSpr(&off_ds, &setup_tex[28]);                             /* 1676 */

    off_ds.x = (float)x;                                                /* 1677 */
    off_ds.y = (float)y;                                                /* 1677 */

    off_ds.alpha = (u_char)(((int)off_ds.alpha * (int)alpha) >> 7);     /* 1678 */

    off_ds.r = rgb;                                                     /* 1679 */
    off_ds.g = rgb;                                                     /* 1679 */
    off_ds.b = rgb;                                                     /* 1679 */

    if (flg == 1)                                                       /* 1680 */
    {
        off_ds.alphar = 0x48;                                           /* 1681 */
    }

    DispSprD(&off_ds);                                                  /* 1684 */
}

static void SetupMenuCsrLDisp(int x, int y, u_char alpha, u_char rgb)   /* 1696 */
{
    DISP_SPRT csr_ds;

    CopySprDToSpr(&csr_ds, &setup_tex[43]);                             /* 1701 */

    csr_ds.x = (float)x;                                                /* 1702 */
    csr_ds.y = (float)y;                                                /* 1702 */

    csr_ds.alpha = (u_char)(((int)csr_ds.alpha * (int)alpha) >> 7);     /* 1703 */

    csr_ds.r = rgb;                                                     /* 1704 */
    csr_ds.g = rgb;                                                     /* 1704 */
    csr_ds.b = rgb;                                                     /* 1704 */

    DispSprD(&csr_ds);                                                  /* 1706 */
}

static void SetupMenuCsrRDisp(int x, int y, u_char alpha, u_char rgb)   /* 1718 */
{
    DISP_SPRT csr_ds;

    CopySprDToSpr(&csr_ds, &setup_tex[44]);                             /* 1723 */

    csr_ds.x = (float)x;                                                /* 1724 */
    csr_ds.y = (float)y;                                                /* 1724 */

    csr_ds.alpha = (u_char)(((int)csr_ds.alpha * (int)alpha) >> 7);     /* 1725 */

    csr_ds.r = rgb;                                                     /* 1726 */
    csr_ds.g = rgb;                                                     /* 1726 */
    csr_ds.b = rgb;                                                     /* 1726 */

    DispSprD(&csr_ds);                                                  /* 1728 */
}

static void SetupMenuCsrFlareLDisp(int x, int y, u_char alpha, u_char rgb)  /* 1740 */
{
    DISP_SPRT flare_ds;

    CopySprDToSpr(&flare_ds, &setup_tex[45]);                           /* 1745 */

    flare_ds.x = (float)x;                                              /* 1746 */
    flare_ds.y = (float)y;                                              /* 1746 */

    flare_ds.alpha = (u_char)(((int)flare_ds.alpha * (int)alpha) >> 7); /* 1747 */

    flare_ds.r = rgb;                                                   /* 1748 */
    flare_ds.g = rgb;                                                   /* 1748 */
    flare_ds.b = rgb;                                                   /* 1748 */

    flare_ds.alphar = 0x48;                                             /* 1749 */

    DispSprD(&flare_ds);                                                /* 1751 */
}

static void SetupMenuCsrFlareRDisp(int x, int y, u_char alpha, u_char rgb)  /* 1763 */
{
    DISP_SPRT flare_ds;

    CopySprDToSpr(&flare_ds, &setup_tex[46]);                           /* 1768 */

    flare_ds.x = (float)x;                                              /* 1769 */
    flare_ds.y = (float)y;                                              /* 1769 */

    flare_ds.alpha = (u_char)(((int)flare_ds.alpha * (int)alpha) >> 7); /* 1770 */

    flare_ds.r = rgb;                                                   /* 1771 */
    flare_ds.g = rgb;                                                   /* 1771 */
    flare_ds.b = rgb;                                                   /* 1771 */

    flare_ds.alphar = 0x48;                                             /* 1772 */

    DispSprD(&flare_ds);                                                /* 1774 */
}

/* The two horizontal rules, drawn 171 pixels apart by the caller. */
static void SetupMenuLineDisp(int off_x, int off_y, u_char alpha, u_char rgb)  /* 1786 */
{
    int i;
    DISP_SPRT line_ds;

    for (i = 0; i < 2; i++) {                                           /* 1792 */
        CopySprDToSpr(&line_ds, &setup_tex[39 + i]);                    /* 1793 */

        line_ds.x = line_ds.x + (float)off_x;                           /* 1794 */
        line_ds.y = line_ds.y + (float)off_y;                           /* 1794 */

        line_ds.alpha = (u_char)(((int)line_ds.alpha * (int)alpha) >> 7);  /* 1795 */

        line_ds.r = rgb;                                                /* 1796 */
        line_ds.g = rgb;                                                /* 1796 */
        line_ds.b = rgb;                                                /* 1796 */

        line_ds.alphar = 0x48;                                          /* 1797 */

        DispSprD(&line_ds);                                             /* 1799 */
    }                                                                   /* 1800 */
}

static void SetupMenuMsgWinDisp(int off_x, int off_y, u_char alpha)     /* 1811 */
{
    (void)off_x;
    (void)off_y;

    DrawCmnWindow(0, 24.0f, 345.0f, 592.0f, 100.0f, alpha, 0x40);       /* 1815 */
}

static void SetupMenuCaptionDisp(int off_x, int off_y, u_char alpha)    /* 1826 */
{
    (void)off_x;
    (void)off_y;

    DrawCmnCapGroup_W(0, 0, alpha, 0);                                  /* 1829 */
}
