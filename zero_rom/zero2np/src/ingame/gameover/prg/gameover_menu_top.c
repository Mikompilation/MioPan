// FILE: /home/zero_rom/zero2np/src/ingame/gameover/prg/gameover_menu_top.c
//
// The game-over menu's top screen: three rows -- continue from a save, open
// the album, or give up and go back to the title -- each behind a yes-no
// confirm window.
//
// This is savepoint_top.o's twin and it borrows that screen's entire drawing
// layer: SavePoint_MenuWinDisp(), SavePointTopCaptionDisp() and
// SavePoint_MenuConfWinDisp() are savepoint_disp.o's and savepoint_top.o's
// own exports, called straight from here.  The only art this file owns is the
// two-piece "GAME OVER" plate in gameover_dat.c.  Even the message rows are
// laid out the same way -- three lines centre-arranged on x = 320, 30 pixels
// apart -- except that savepoint reads its y values out of a table and this
// file computes them, which is why its .rodata holds one int[3] where
// savepoint's holds two.
//
// The two-level step/mode pair is what makes the confirm window work.  `step`
// is the screen (loading / open / decided) and `mode` says which pad handler
// is live while it is open: GameOverMenuTopPad() for the row cursor,
// GameOverMenuTopConfPad() for the yes-no.  Each is additionally gated on its
// own window having finished animating, so input is dead during both fades.
//
// Reconstructed from the Feb 6 2004 prototype (SLES_523.84),
// gameover_menu_top.o.
//
// Trailing /* NNN */ comments are the original source line numbers, measured
// from the $LM stabs.

#include "gameover_menu_top.h"

#include "gameover_dat.h"                           /* gameover_tex           */
#include "gameover_menu.h"                          /* GetGameOverMenuTexMem  */

#include "../../savepoint/savepoint_disp.h"         /* SavePoint_MenuWinDisp  */
#include "../../menu/zero2_anim2d.h"                /* Zero2Anim2D_InOut...   */
#include "../../../common/utility2.h"               /* PRINT_ASSERT           */
#include "../../../graphics/graph2d/draw_cmn.h"     /* DrawCmnSelCsr          */
#include "../../../graphics/graph2d/g2d_draw.h"     /* DISP_SPRT / DispSprD   */
#include "../../../graphics/graph2d/message.h"      /* PrintMsg               */
#include "../../../graphics/graph2d/tim2.h"         /* PK2SendVram            */
#include "../../../main/gphase.h"                   /* SetNextGPhase          */
#include "../../../system/eeiop/cddat.h"            /* GAMEOVER_PK2           */
#include "../../../system/eeiop/fileload.h"         /* FileLoadIsEnd2         */
#include "../../../system/eeiop/snd3d.h"            /* SND_3D_SET             */
#include "../../../system/os/system.h"              /* GetLanguage / SystemBankPlay */
#include "../../../system/pad/pad.h"                /* pad / paddat           */

/* The message group every game-over string comes out of. */
#define GAMEOVER_MSG_TYPE           0x23

/* Window open / close times, in frames, for both the menu and the confirm. */
#define GAMEOVER_MENU_TOP_ANIM_IN_TIME  10
#define GAMEOVER_MENU_TOP_ANIM_OUT_TIME 5

static void GameOverMenuTopCtrlInit(void);
static int  GameOverMenuTopTexLoadWait(void);
static void GameOverMenuTopPad(void);
static void GameOverMenuEndReq(void);
static void GameOverMenuTopConfPad(void);
static void GameOverMenuTopExeDecision(void);
static void GameOverMenuTopDispInit(void);
static void GameOverMenuTopTitleDisp(int off_x, int off_y, u_char alpha);
static void GameOverMenuTopSelMsgDisp(int off_x, int off_y, u_char alpha);
static void GameOverMenuTopCsrDisp(int off_x, int off_y, u_char alpha);

static void                  *gameover_menu_tex_addr;       /* sdata 3f1020 */
static char                   gameover_menu_top_init_flg;   /* sdata 3f1024 */
static GAMEOVER_MENU_TOP_CTRL gameover_menu_top_ctrl;       /* sbss  3f4cb8 */
static GAMEOVER_MENU_TOP_DISP gameover_menu_top_disp;       /* sbss  3f4cc0 */

/* ==========================================================================
 *  Set-up
 * ======================================================================== */

/* Once per visit, from init_GameOver_Menu().  Steps straight past the display
 * reset to step 1 and anim_step 2, i.e. "pak already requested, window already
 * open" -- the pak was requested in the same breath and the menu is meant to
 * be there the moment the parent's fade clears, not to animate in behind it. */
void GameOverMenuTopFirstInit(void)                                     /* 127 */
{
    GameOverMenuTopCtrlInit();                                          /* 131 */
    GameOverMenuTopDispInit();                                          /* 134 */

    gameover_menu_top_ctrl.step  = GAMEOVER_MENU_TOP_STEP_LOAD_WAIT;    /* 136 */
    gameover_menu_top_ctrl.csr   = GAMEOVER_MENU_TOP_CSR_LOAD;          /* 137 */

    gameover_menu_top_disp.anim_step = ZERO2_ANIM2D_STEP_SHOW;          /* 139 */

    gameover_menu_top_init_flg   = 0;                                   /* 141 */
}

/* Once per entry to the phase.  The first entry is the one FirstInit() just
 * set up, so it only claims the flag; every later entry -- coming back from
 * the load screen or the album -- resets the control block, which sends the
 * screen back through its open animation. */
void GameOverMenuTopInit(void)                                          /* 149 */
{
    if (gameover_menu_top_init_flg != 0) {                              /* 152 */
        GameOverMenuTopCtrlInit();                                      /* 154 */
    }
    else {
        gameover_menu_top_init_flg = 1;                                 /* 157 */
    }
}

/* csr is deliberately not reset -- coming back from the album should leave
 * the cursor where the player left it. */
static void GameOverMenuTopCtrlInit(void)                               /* 166 */
{
    gameover_menu_top_ctrl.step     = GAMEOVER_MENU_TOP_STEP_INIT;      /* 169 */
    gameover_menu_top_ctrl.mode     = GAMEOVER_MENU_TOP_MODE_MENU;      /* 170 */
    gameover_menu_top_ctrl.conf_csr = 1;                                /* 171 */
}

/* ==========================================================================
 *  Text pak
 *
 *  GAMEOVER_PK2's per-language variants sit immediately after it in the file
 *  table, so the language index is simply added to the base file number.  It
 *  is recomputed at all three use sites and never clamped -- the same shape
 *  title.o's logo pak has.
 * ======================================================================== */

void GameOverMenuTopBackGroundLoadReq(void)                             /* 179 */
{
    if (gameover_menu_tex_addr != nullptr) {                            /* 181 */
        LiberateGameOverMenuTexMem(&gameover_menu_tex_addr);            /* 182 */
    }

    GetGameOverMenuTexMem(&gameover_menu_tex_addr,
                          GAMEOVER_PK2 + (char)GetLanguage());          /* 187 */
    GameOverMenuTexLoadReq(gameover_menu_tex_addr,
                           GAMEOVER_PK2 + (char)GetLanguage());         /* 190 */
}

static int GameOverMenuTopTexLoadWait(void)                             /* 200 */
{
    if (FileLoadIsEnd2(GAMEOVER_PK2 + (char)GetLanguage(),
                       gameover_menu_tex_addr) != 0) {                  /* 208 */
        return 1;
    }

    return 0;                                                           /* 213 */
}

/* ==========================================================================
 *  Per-frame control
 * ======================================================================== */

/* Both pad handlers are gated on their own window being fully open, so input
 * is dead through every fade.  Line 250 is a zero-length marker -- its $LM
 * sits on the same address as case 3's first test. */
void GameOverMenuTopMain(void)                                          /* 223 */
{
    switch (gameover_menu_top_ctrl.step) {                              /* 226 */
    case GAMEOVER_MENU_TOP_STEP_INIT:
        GameOverMenuTopDispInit();                                      /* 229 */

        gameover_menu_top_ctrl.step = GAMEOVER_MENU_TOP_STEP_LOAD_WAIT; /* 232 */
        break;

    case GAMEOVER_MENU_TOP_STEP_LOAD_WAIT:
        if (GameOverMenuTopTexLoadWait() != 0) {                        /* 234 */
            gameover_menu_top_ctrl.step = GAMEOVER_MENU_TOP_STEP_OPEN;  /* 237 */
        }
        break;

    case GAMEOVER_MENU_TOP_STEP_OPEN:
        if (gameover_menu_top_disp.anim_step == ZERO2_ANIM2D_STEP_SHOW) { /* 239 */
            switch (gameover_menu_top_ctrl.mode) {                      /* 240 */
            case GAMEOVER_MENU_TOP_MODE_MENU:
                /* The confirm window is closed, so the rows take input. */
                if (gameover_menu_top_disp.conf_anim_step ==
                        ZERO2_ANIM2D_STEP_END) {                        /* 242 */
                    GameOverMenuTopPad();                               /* 243 */
                }
                break;

            case GAMEOVER_MENU_TOP_MODE_CONF:
                if (gameover_menu_top_disp.conf_anim_step ==
                        ZERO2_ANIM2D_STEP_SHOW) {                       /* 247 */
                    GameOverMenuTopConfPad();                           /* 248 */
                }
                break;                                                  /* 250 */

            default:
                PRINT_ASSERT("Error! %s", __FUNCTION__);                /* 252 */
                break;
            }
        }
        break;

    case GAMEOVER_MENU_TOP_STEP_DECIDED:
        /* Wait for the menu window to finish closing, then act. */
        if (gameover_menu_top_disp.anim_step == ZERO2_ANIM2D_STEP_END) { /* 257 */
            switch (gameover_menu_top_ctrl.csr) {                       /* 258 */
            case GAMEOVER_MENU_TOP_CSR_LOAD:
                SetNextGPhase(GID_GAMEOVER_MENU_LOAD);                  /* 260 */
                break;

            case GAMEOVER_MENU_TOP_CSR_ALBUM:
                SetNextGPhase(GID_GAMEOVER_MENU_ALBUM);                 /* 263 */
                break;

            case GAMEOVER_MENU_TOP_CSR_EXIT:
                /* Nothing to do: GameOverMenuEndReq() already started the
                 * parent's closing fade, and that is what leaves. */
                break;

            default:
                PRINT_ASSERT("Error! %s", __FUNCTION__);                /* 269 */
                break;
            }
        }
        break;

    default:
        PRINT_ASSERT("Error! %s", __FUNCTION__);                        /* 274 */
        break;
    }
}

/* UP/DOWN walk the three rows; CROSS opens the confirm window on whichever is
 * selected, and TRIANGLE jumps the cursor to the exit row without confirming
 * anything.  csr + 2 rather than csr - 1 so the modulo stays positive. */
static void GameOverMenuTopPad(void)                                    /* 283 */
{
    if ((pad[0].rpt & 0x1000) || GetPadAnalogRpt(0)) {                  /* 287 */
        SystemBankPlay(0, 1, 0, 0, (SND_3D_SET *)nullptr, 0x3200, 0x1000); /* 288 */

        gameover_menu_top_ctrl.csr =
            (char)((gameover_menu_top_ctrl.csr + 2) % GAMEOVER_MENU_TOP_CSR_NUM); /* 290 */
    }
    else if ((pad[0].rpt & 0x4000) || GetPadAnalogRpt(1)) {             /* 293 */
        SystemBankPlay(0, 1, 0, 0, (SND_3D_SET *)nullptr, 0x3200, 0x1000); /* 294 */

        gameover_menu_top_ctrl.csr =
            (char)((gameover_menu_top_ctrl.csr + 1) % GAMEOVER_MENU_TOP_CSR_NUM); /* 296 */
    }
    else if (*paddat[0] == 1) {                                         /* 299 */
        SystemBankPlay(3, 1, 0, 0, (SND_3D_SET *)nullptr, 0x3200, 0x1000); /* 300 */

        gameover_menu_top_ctrl.mode           = GAMEOVER_MENU_TOP_MODE_CONF; /* 302 */
        gameover_menu_top_ctrl.conf_csr       = 1;                      /* 303 */
        gameover_menu_top_disp.conf_anim_step = ZERO2_ANIM2D_STEP_START; /* 304 */
        gameover_menu_top_disp.conf_anim_timer = 0;                     /* 305 */
    }
    else if (*paddat[1] == 1) {                                         /* 308 */
        SystemBankPlay(1, 1, 0, 0, (SND_3D_SET *)nullptr, 0x3200, 0x1000); /* 309 */

        gameover_menu_top_ctrl.csr = GAMEOVER_MENU_TOP_CSR_EXIT;        /* 311 */
    }
}

/* The player is leaving.  step 3 stops the menu taking input while the
 * parent's fade runs; the parent is what actually changes phase. */
static void GameOverMenuEndReq(void)                                    /* 320 */
{
    gameover_menu_top_ctrl.step = GAMEOVER_MENU_TOP_STEP_DECIDED;       /* 323 */

    GameOverMenuFadeOutReq();                                           /* 325 */
}

/* LEFT and RIGHT both just flip the two-way cursor, hence the xor.  CROSS on
 * "no" and TRIANGLE do the same thing -- close the window and hand the rows
 * back -- and the ROM writes that tail out twice rather than sharing it.  GCC
 * cross-jumped the two copies into the second one's code, so the first copy
 * keeps $LMs only for 359 and 360; 361 below is inferred from the shape of
 * its twin at 369 and is the one annotation in this folder that the stabs do
 * not confirm. */
static void GameOverMenuTopConfPad(void)                                /* 333 */
{
    if ((pad[0].rpt & 0x8000) || GetPadAnalogRpt(2) ||                  /* 337 */
        (pad[0].rpt & 0x2000) || GetPadAnalogRpt(3)) {                  /* 343 */
        SystemBankPlay(0, 1, 0, 0, (SND_3D_SET *)nullptr, 0x3200, 0x1000); /* 344 */

        gameover_menu_top_ctrl.conf_csr =
            (char)(gameover_menu_top_ctrl.conf_csr ^ 1);                /* 346 */
    }
    else if (*paddat[0] == 1) {                                         /* 349 */
        SystemBankPlay(3, 1, 0, 0, (SND_3D_SET *)nullptr, 0x3200, 0x1000); /* 350 */

        if (gameover_menu_top_ctrl.conf_csr == 0) {                     /* 353 */
            GameOverMenuTopExeDecision();                               /* 354 */
        }
        else {
            gameover_menu_top_ctrl.mode            = GAMEOVER_MENU_TOP_MODE_MENU; /* 359 */
            gameover_menu_top_disp.conf_anim_step  = ZERO2_ANIM2D_STEP_OUT; /* 360 */
            gameover_menu_top_disp.conf_anim_timer = 0;                 /* 361 */
        }
    }
    else if (*paddat[1] == 1) {                                         /* 364 */
        SystemBankPlay(1, 1, 0, 0, (SND_3D_SET *)nullptr, 0x3200, 0x1000); /* 365 */

        gameover_menu_top_ctrl.mode            = GAMEOVER_MENU_TOP_MODE_MENU; /* 367 */
        gameover_menu_top_disp.conf_anim_step  = ZERO2_ANIM2D_STEP_OUT; /* 368 */
        gameover_menu_top_disp.conf_anim_timer = 0;                     /* 369 */
    }
}

/* "Yes" on the confirm window.  Load and Album close the menu and let step 3
 * dispatch the phase; Exit goes through GameOverMenuEndReq() instead, which
 * closes the whole screen rather than just this window. */
static void GameOverMenuTopExeDecision(void)                            /* 378 */
{
    switch (gameover_menu_top_ctrl.csr) {                               /* 381 */
    case GAMEOVER_MENU_TOP_CSR_LOAD:
    case GAMEOVER_MENU_TOP_CSR_ALBUM:
        gameover_menu_top_ctrl.step = GAMEOVER_MENU_TOP_STEP_DECIDED;   /* 384 */

        gameover_menu_top_disp.anim_step       = ZERO2_ANIM2D_STEP_OUT; /* 386 */
        gameover_menu_top_disp.anim_timer      = 0;                     /* 387 */
        gameover_menu_top_disp.conf_anim_step  = ZERO2_ANIM2D_STEP_OUT; /* 388 */
        gameover_menu_top_disp.conf_anim_timer = 0;                     /* 389 */
        break;                                                          /* 390 */

    case GAMEOVER_MENU_TOP_CSR_EXIT:
        GameOverMenuEndReq();                                           /* 392 */
        break;

    default:
        PRINT_ASSERT("Error! %s", __FUNCTION__);                        /* 395 */
        break;
    }
}

void GameOverMenuTopMemFree(void)                                       /* 408 */
{
    LiberateGameOverMenuTexMem(&gameover_menu_tex_addr);                /* 412 */
}

/* ==========================================================================
 *  Per-frame draw
 * ======================================================================== */

/* conf_anim_step starts at END (closed) rather than START, so the confirm
 * window is not drawn until the player asks for it. */
static void GameOverMenuTopDispInit(void)                               /* 424 */
{
    gameover_menu_top_disp.anim_step       = ZERO2_ANIM2D_STEP_START;   /* 427 */
    gameover_menu_top_disp.anim_timer      = 0;                         /* 428 */
    gameover_menu_top_disp.conf_anim_step  = ZERO2_ANIM2D_STEP_END;     /* 429 */
    gameover_menu_top_disp.conf_anim_timer = 0;                         /* 430 */
}

/* PK2SendVram() re-uploads this screen's own text pak every frame, after the
 * shared window art has been drawn -- the two paks share VRAM, so the order
 * matters. */
void GameOverMenuTopDisp(void)                                          /* 438 */
{
    u_char alpha;

    /* The confirm question differs per row: 3 load, 4 album, 5 exit. */
    static const int msg_id_tbl[GAMEOVER_MENU_TOP_CSR_NUM] =            /* rdata 3b4170 */
    {
        3,      /* load  */
        4,      /* album */
        5,      /* exit  */
    };

    if (gameover_menu_top_ctrl.step >= GAMEOVER_MENU_TOP_STEP_OPEN &&   /* 452 */
        gameover_menu_top_disp.anim_step != ZERO2_ANIM2D_STEP_END) {    /* 453 */
        alpha = Zero2Anim2D_InOutAnimCtrl(&gameover_menu_top_disp.anim_step,
                                          &gameover_menu_top_disp.anim_timer,
                                          GAMEOVER_MENU_TOP_ANIM_IN_TIME,
                                          GAMEOVER_MENU_TOP_ANIM_OUT_TIME); /* 455 */

        SavePoint_MenuWinDisp(0, 0, alpha);                             /* 458 */

        PK2SendVram((uintptr_t)gameover_menu_tex_addr, -1, -1, 0); /* 460 */

        GameOverMenuTopTitleDisp(0, 0, alpha);                          /* 463 */
        SavePointTopCaptionDisp(0, 0, alpha);                           /* 466 */
        GameOverMenuTopSelMsgDisp(0, 0, alpha);                         /* 469 */
        GameOverMenuTopCsrDisp(0, 0, alpha);                            /* 472 */

        if (gameover_menu_top_disp.conf_anim_step != ZERO2_ANIM2D_STEP_END) { /* 474 */
            alpha = Zero2Anim2D_InOutAnimCtrl(&gameover_menu_top_disp.conf_anim_step,
                                              &gameover_menu_top_disp.conf_anim_timer,
                                              GAMEOVER_MENU_TOP_ANIM_IN_TIME,
                                              GAMEOVER_MENU_TOP_ANIM_OUT_TIME); /* 476 */

            SavePoint_MenuConfWinDisp((int)gameover_menu_top_ctrl.conf_csr,
                                      0, 0, alpha);                     /* 479 */

            PrintMsg(GAMEOVER_MSG_TYPE, msg_id_tbl[gameover_menu_top_ctrl.csr],
                     68, 309, 1, (int)alpha, 0);                        /* 483 */
        }
    }
}

/* The two-piece screen title. */
static void GameOverMenuTopTitleDisp(int off_x, int off_y, u_char alpha) /* 498 */
{
    DISP_SPRT title_ds;
    int       i;

    for (i = 0; i < 2; i++) {                                           /* 504 */
        CopySprDToSpr(&title_ds, &gameover_tex[i]);                     /* 505 */

        title_ds.x = title_ds.x + (float)off_x;                         /* 506 */
        title_ds.y = title_ds.y + (float)off_y;                         /* 506 */

        title_ds.alpha = (u_char)(((int)title_ds.alpha * (int)alpha) >> 7); /* 507 */

        DispSprD(&title_ds);                                            /* 508 */
    }                                                                   /* 509 */
}

/* The three rows, centre-arranged on x = 320 and stepping 30 pixels from
 * y = 146.  savepoint_top.o reads the same y values out of a table; this file
 * computes them, which is the one real difference between the two. */
static void GameOverMenuTopSelMsgDisp(int off_x, int off_y, u_char alpha) /* 520 */
{
    int i;

    static const int msg_id_tbl[GAMEOVER_MENU_TOP_CSR_NUM] =            /* rdata 3b4180 */
    {
        0,      /* load  */
        1,      /* album */
        2,      /* exit  */
    };

    for (i = 0; i < GAMEOVER_MENU_TOP_CSR_NUM; i++) {                   /* 533 */
        /* No local for this in the stabs -- the compare is line 534 and the
         * select folds into the call at 542. */
        PrintMsg_Arrange(GAMEOVER_MSG_TYPE, msg_id_tbl[i],
                         off_x + 320, off_y + 146 + i * 30,
                         (gameover_menu_top_ctrl.csr == i) ? 7 : 1,     /* 534 */
                         (int)alpha, 0, 0, 0, 2);                       /* 542 */
    }                                                                   /* 543 */
}

/* The row highlight, 272 wide, stepping 30 pixels per row from y = 142.
 * Unlike savepoint_top.o's, this one really does honour both offsets. */
static void GameOverMenuTopCsrDisp(int off_x, int off_y, u_char alpha)  /* 554 */
{
    DrawCmnSelCsr(0, (float)(off_x + 184),
                  (float)(gameover_menu_top_ctrl.csr * 30 + off_y + 142),
                  alpha, 272.0f, 1);                                    /* 559 */
}
