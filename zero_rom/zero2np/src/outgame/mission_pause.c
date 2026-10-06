// FILE: /home/zero_rom/zero2np/src/outgame/mission_pause.c
//
// The mission-mode pause menu.
//
// Three rows -- resume, return to the mission list, vibration on/off -- drawn
// over a captured copy of the frame the game was on when START was pressed.
// The capture is why the menu can dim the game behind it without the game
// still running: MisPauseInit() blits the front buffer into a VRAM scratch and
// every draw blits it back, so what is behind the window is a still.
//
// The third row is live rather than a setting to confirm: toggling it calls
// OptionVibChange() straight away and buzzes the pad for twenty frames, which
// is what MisPauseMain()'s VibrateRequest() loop is for.
//
// Unplugging the pad forces step 2, a modal notice that will not clear until a
// DUALSHOCK2 answers again -- and `before_step` is how it knows whether to put
// the return-title window back afterwards.
//
// Reconstructed from the Feb 6 2004 prototype (SLES_523.84).
// Trailing /* NNN */ are ROM source line numbers, measured from
// disassemble_function and symbols.txt.

#include "mission_pause.h"

#include "option.h"                         // GetOptionVib / OptionVibChange
#include "../common/utility2.h"             // PRINT_ASSERT / PRINT_WARNING
#include "../common/variable.h"             // pad[] / key_now[] / sys_wrk
#include "../graphics/graph2d/draw_cmn.h"   // DrawCmn* windows and widgets
#include "../graphics/graph2d/g2d_draw.h"   // DISP_SPRT / DISP_SQAR / Copy* / Disp*
#include "../graphics/graph2d/message.h"    // PrintMsg / PrintMsg_Arrange
#include "../ingame/ingame.h"               // SetIngamePauseMode / IngameDecideNextPhase
#include "../ingame/pause/prg/pause_dat.h"  // pause_tex[]
#include "../main/gphase.h"                 // SetNextGPhase / GID_*
#include "../system/eeiop/snd_buffer.h"     // SndBufAllPause
#include "../system/eeiop/stream_auto.h"    // StreamAutoAllPause
#include "../system/os/system.h"            // SystemBankPlay
#include "../system/pad/pad.h"              // paddat / padIsConnected / ...

/* PAUSE_CTRL::step */
#define MISPAUSE_STEP_MENU          0
#define MISPAUSE_STEP_RETURN_TITLE  1
#define MISPAUSE_STEP_PAD_ERROR     2

/* PAUSE_CTRL::csr -- the three rows. */
#define MISPAUSE_CSR_CONTINUE   0
#define MISPAUSE_CSR_TITLE      1
#define MISPAUSE_CSR_VIB        2
#define MISPAUSE_CSR_MAX        3

/* The screen capture.  The two display buffers are 0x1180 apart in VRAM and
 * this mode's scratch sits at 0x2bc0 -- a different one from the ingame
 * pause's, which is why the copy type is 0 here and 1 there. */
#define MISPAUSE_FRAME_BUF_ADRS 0x1180
#define MISPAUSE_CAPTURE_ADRS   0x2bc0

/* Frames the pad buzzes for when vibration is switched on. */
#define MISPAUSE_VIB_TIME       20

/* SystemBankPlay() cue numbers, as everywhere else in outgame/. */
#define SE_CURSOR   0
#define SE_CANCEL   1
#define SE_DECIDE   3

static void MisPauseCtrlInit(void);
static void MisPauseInDispCaptuer(void);
static void MisPausePad(void);
static void MisPauseMenuSel(void);
static void MisPauseReturnTitlePad(void);
static void MisPausePadErrorPad(void);
static void MisPauseCaptureDataDisp(void);
static void MisPauseWinDisp(int off_x, int off_y, u_char alpha);
static void MisPauseTitleDisp(int off_x, int off_y, u_char alpha);
static void MisPauseMenuCsrDisp(int off_x, int off_y, u_char alpha);
static void MisPauseMenuDisp(int off_x, int off_y, u_char alpha);
static void MisPauseMenuReturnTitleWinDisp(int off_x, int off_y, u_char alpha);
static void MisPausePadErrorMsgDisp(int off_x, int off_y, u_char alpha);

static PAUSE_CTRL pause_ctrl;                                           /* bss 4b6448 */

void MisPauseInit(void)                                                 /* 127 */
{
    MisPauseCtrlInit();                                                 /* 131 */
    MisPauseInDispCaptuer();                                            /* 134 */

    StreamAutoAllPause();                                               /* 137 */
    SndBufAllPause();                                                   /* 138 */
}

static void MisPauseCtrlInit(void)                                      /* 147 */
{
    pause_ctrl.step = MISPAUSE_STEP_MENU;                               /* 150 */
    pause_ctrl.before_step = MISPAUSE_STEP_MENU;                        /* 151 */
    pause_ctrl.csr = MISPAUSE_CSR_CONTINUE;                             /* 152 */
    pause_ctrl.title_csr = 0;                                           /* 153 */

    /* The vibration row starts on whatever the option screen has, and is
     * written straight back to it as the player toggles. */
    pause_ctrl.vib_csr = GetOptionVib();                                /* 154 */
    pause_ctrl.vib_time = 0;                                            /* 155 */
}

/* Grab the frame that is about to be shown into the scratch buffer. */
static void MisPauseInDispCaptuer(void)                                 /* 163 */
{
    LocalCopyLtoL(0, (int)(((sys_wrk.count + 1) & 1) * MISPAUSE_FRAME_BUF_ADRS),
                  MISPAUSE_CAPTURE_ADRS);                               /* 166 */
}

/* Always reports 0.  ingame.c calls MisPauseDispMain() only on 0, so the menu
 * is drawn every frame it is up. */
int MisPauseMain(void)                                                  /* 179 */
{
    /* Losing the pad mid-pause forces the notice, whatever else was open. */
    if ((padIsConnected(0) == 0) || (GetPadDUALSHOCK2(0) == 0))         /* 192 */
    {
        pause_ctrl.step = MISPAUSE_STEP_PAD_ERROR;                      /* 193 */
    }

    switch (pause_ctrl.step)                                            /* 196 */
    {
    case MISPAUSE_STEP_MENU:
        MisPausePad();                                                  /* 198 */
        break;                                                          /* 199 */

    case MISPAUSE_STEP_RETURN_TITLE:
        MisPauseReturnTitlePad();                                       /* 201 */
        break;                                                          /* 202 */

    case MISPAUSE_STEP_PAD_ERROR:
        MisPausePadErrorPad();                                          /* 204 */
        break;                                                          /* 205 */

    default:
        PRINT_ASSERT("Error! %s\n", __FUNCTION__);                      /* 207 */
        break;
    }

    if (pause_ctrl.vib_time != 0)                                       /* 210 */
    {
        VibrateRequest(0, 0xb4, 0xb4);                                  /* 211 */
        pause_ctrl.vib_time--;                                          /* 212 */
    }

    return 0;                                                           /* 216 */
}

/* The menu itself.  START resumes outright; SELECT drops into the debug
 * phase. */
static void MisPausePad(void)                                           /* 226 */
{
    if (((pad[0].rpt & 0x1000U) != 0) || (GetPadAnalogRpt(0) != 0))     /* 230 */
    {
        SystemBankPlay(SE_CURSOR, 1, 0, 0, (SND_3D_SET *)0, 0x3200, 0x1000);  /* 231 */
        pause_ctrl.csr =
            (char)((pause_ctrl.csr + (MISPAUSE_CSR_MAX - 1)) % MISPAUSE_CSR_MAX);  /* 232 */
    }
    else if (((pad[0].rpt & 0x4000U) != 0) || (GetPadAnalogRpt(1) != 0))  /* 235 */
    {
        SystemBankPlay(SE_CURSOR, 1, 0, 0, (SND_3D_SET *)0, 0x3200, 0x1000);  /* 236 */
        pause_ctrl.csr = (char)((pause_ctrl.csr + 1) % MISPAUSE_CSR_MAX);     /* 237 */
    }
    else if (*paddat[0] == 1)                                           /* 240 */
    {
        SystemBankPlay(SE_DECIDE, 1, 0, 0, (SND_3D_SET *)0, 0x3200, 0x1000);  /* 241 */
        MisPauseMenuSel();                                              /* 242 */
    }
    else if (*key_now[12] == 1)                                         /* 245 */
    {
        SetIngamePauseMode(0);                                          /* 246 */
        SetNextGPhase(IngameDecideNextPhase());                         /* 248 */
    }
    else if (*key_now[13] == 1)                                         /* 252 */
    {
        SetNextGPhase(GID_STORY_DEBUG);                                 /* 253 */
    }
}

static void MisPauseMenuSel(void)                                       /* 263 */
{
    switch (pause_ctrl.csr)                                             /* 266 */
    {
    case MISPAUSE_CSR_CONTINUE:
        SetIngamePauseMode(0);                                          /* 268 */
        SetNextGPhase(IngameDecideNextPhase());                         /* 270 */
        break;

    case MISPAUSE_CSR_TITLE:
        /* All three take the cursor's own value, which is 1 -- so the
         * confirmation opens with "no" selected. */
        pause_ctrl.step = pause_ctrl.csr;                               /* 273 */
        pause_ctrl.title_csr = pause_ctrl.csr;                          /* 275 */
        pause_ctrl.before_step = pause_ctrl.csr;                        /* 276 */
        break;

    case MISPAUSE_CSR_VIB:
        pause_ctrl.vib_csr ^= 1;                                        /* 278 */
        OptionVibChange(pause_ctrl.vib_csr);                            /* 279 */

        if (pause_ctrl.vib_csr != 0)                                    /* 282 */
        {
            pause_ctrl.vib_time = MISPAUSE_VIB_TIME;                    /* 283 */
        }
        else
        {
            pause_ctrl.vib_time = 0;                                    /* 289 */
        }
        break;

    default:
        PRINT_WARNING("Error!! MisPauseMenuSel()");                     /* 291 */
        break;
    }
}

/* The "return to the mission list?" window.  Answering no, or cancelling,
 * lands on the same pair of stores -- which is why they sit in a shared tail
 * rather than in each arm. */
static void MisPauseReturnTitlePad(void)                                /* 299 */
{
    if (((pad[0].rpt & 0x8000U) != 0) || (GetPadAnalogRpt(2) != 0) ||   /* 303 */
        ((pad[0].rpt & 0x2000U) != 0) || (GetPadAnalogRpt(3) != 0))     /* 308 */
    {
        SystemBankPlay(SE_CURSOR, 1, 0, 0, (SND_3D_SET *)0, 0x3200, 0x1000);  /* 309 */
        pause_ctrl.title_csr = (char)(pause_ctrl.title_csr ^ 1);        /* 310 */
        return;
    }

    if (*paddat[0] == 1)                                                /* 313 */
    {
        SystemBankPlay(SE_DECIDE, 1, 0, 0, (SND_3D_SET *)0, 0x3200, 0x1000);  /* 314 */

        if (pause_ctrl.title_csr == 0)                                  /* 316 */
        {
            SetNextGPhase(GID_MISSION_SEL);                             /* 320 */
            return;
        }
    }
    else if (*paddat[1] == 1)                                           /* 330 */
    {
        SystemBankPlay(SE_CANCEL, 1, 0, 0, (SND_3D_SET *)0, 0x3200, 0x1000);  /* 331 */
    }
    else
    {
        return;
    }

    pause_ctrl.step = MISPAUSE_STEP_MENU;                               /* 332 */
    pause_ctrl.before_step = MISPAUSE_STEP_MENU;                        /* 333 */
}

/* The pad-disconnected notice.  Nothing clears it but a DUALSHOCK2 answering
 * again *and* CROSS -- and it goes back to whatever was open before. */
static void MisPausePadErrorPad(void)                                   /* 344 */
{
    if ((padIsConnected(0) != 0) && (GetPadDUALSHOCK2(0) != 0) &&       /* 348 */
        (*paddat[0] == 1))                                              /* 349 */
    {
        SystemBankPlay(SE_DECIDE, 1, 0, 0, (SND_3D_SET *)0, 0x3200, 0x1000);  /* 350 */
        pause_ctrl.step = pause_ctrl.before_step;                       /* 352 */
    }
}

// ──────────────────────────────────────────────────────────────────────
// Drawing.  Everything is at a fixed 0x80, so the three off_x / off_y / alpha
// parameters are only ever passed the same values.

void MisPauseDispMain(void)                                             /* 366 */
{
    MisPauseCaptureDataDisp();                                          /* 369 */

    MisPauseWinDisp(0, 0, 0x80);                                        /* 372 */
    MisPauseTitleDisp(0, 0, 0x80);                                      /* 374 */
    MisPauseMenuDisp(0, 0, 0x80);                                       /* 376 */
    MisPauseMenuCsrDisp(0, 0, 0x80);                                    /* 378 */

    if (pause_ctrl.step == MISPAUSE_STEP_RETURN_TITLE)                  /* 381 */
    {
        MisPauseMenuReturnTitleWinDisp(0, 0, 0x80);                     /* 382 */
    }

    if (pause_ctrl.step == MISPAUSE_STEP_PAD_ERROR)                     /* 385 */
    {
        if (pause_ctrl.before_step == MISPAUSE_STEP_RETURN_TITLE)       /* 386 */
        {
            MisPauseMenuReturnTitleWinDisp(0, 0, 0x80);                 /* 387 */
        }

        MisPausePadErrorMsgDisp(0, 0, 0x80);                            /* 390 */
    }
}

/* Put the captured frame back, into whichever buffer is being drawn now. */
static void MisPauseCaptureDataDisp(void)                               /* 399 */
{
    LocalCopyLtoL(0, MISPAUSE_CAPTURE_ADRS,
                  (int)((sys_wrk.count & 1) * MISPAUSE_FRAME_BUF_ADRS));  /* 402 */
}

static void MisPauseWinDisp(int off_x, int off_y, u_char alpha)         /* 412 */
{
    DISP_SQAR dsq;
    SQAR_DAT  pause_bg = { 640, 448, 0, 0, 0xa0, 0, 0, 0, 0x26 };       /* 414 */

    CopySqrDToSqr(&dsq, &pause_bg);                                     /* 419 */
    DispSqrD(&dsq);                                                     /* 420 */

    DrawCmnWindow(0xa0, (float)(off_x + 0x96), (float)(off_y + 0x82),
                  340.0f, 172.0f, alpha, 0x59);                         /* 424 */

    DrawCmnLine(150.0f, 172.0f, 340.0f, 1,
                (u_char)((int)alpha * 76 / 128), 0xa0);                 /* 428 */
}

static void MisPauseTitleDisp(int off_x, int off_y, u_char alpha)       /* 439 */
{
    DISP_SPRT title_ds;

    CopySprDToSpr(&title_ds, &pause_tex[0]);                            /* 443 */

    title_ds.x = title_ds.x + (float)off_x;                             /* 444 */
    title_ds.y = title_ds.y + (float)off_y;                             /* 444 */

    title_ds.alpha = (u_char)(((int)title_ds.alpha * (int)alpha) >> 7); /* 445 */

    DispSprD(&title_ds);                                                /* 446 */
}

/* Ignores both offsets -- the row pitch is 30 pixels off a fixed base. */
static void MisPauseMenuCsrDisp(int off_x, int off_y, u_char alpha)     /* 457 */
{
    (void)off_x;
    (void)off_y;

    DrawCmnSelCsr(0x80, 162.0f, (float)(pause_ctrl.csr * 30 + 0xb8),
                  alpha, 316.0f, 1);                                    /* 461 */
}

/* The three rows.  The vibration row draws out of a different message bank and
 * at a fixed y, which is why it is the one case the loop special-cases. */
static void MisPauseMenuDisp(int off_x, int off_y, u_char alpha)        /* 471 */
{
    int i;
    int col_label;
    int msg_id_tbl[3] = { 0x57, 0x59, 0x57 };                           /* 473 */

    (void)off_x;

    if (pause_ctrl.vib_csr != 0)                                        /* 482 */
    {
        msg_id_tbl[2] = 5;                                              /* 483 */
    }
    else
    {
        msg_id_tbl[2] = 4;                                              /* 487 */
    }

    for (i = 0; i < MISPAUSE_CSR_MAX; i++) {                            /* 491 */
        col_label = (pause_ctrl.csr == i) ? 7 : 1;                      /* 492 */

        if (i == MISPAUSE_CSR_VIB)                                      /* 499 */
        {
            PrintMsg_Arrange(0x42, msg_id_tbl[2], 0x140, off_y + 0xf8,
                             col_label, alpha, 0, 0, 0, 2);             /* 501 */
        }
        else
        {
            PrintMsg_Arrange(0x3c, msg_id_tbl[i], 0x140,
                             off_y + 0xbc + i * 0x1e,
                             col_label, alpha, 0, 0, 0, 2);             /* 505 */
        }
    }                                                                   /* 507 */
}

static void MisPauseMenuReturnTitleWinDisp(int off_x, int off_y, u_char alpha)  /* 517 */
{
    DrawCmnWindow(0xa0, (float)(off_x + 0x18), (float)(off_y + 0x145),
                  592.0f, 112.0f, alpha, 0x59);                         /* 522 */

    DrawCmnSelCsr(0xa0, (float)(pause_ctrl.title_csr * 0xcf + off_x + 0x9b),
                  (float)(off_y + 0x17d), alpha, 0.0f, 0);              /* 526 */

    DrawCmnSelYes(0xa0, (float)(off_x + 0x99), (float)(off_y + 0x17e), alpha);  /* 529 */
    DrawCmnSelNo(0xa0, (float)(off_x + 0x168), (float)(off_y + 0x17e), alpha);  /* 530 */

    PrintMsg(0x3c, 0x5a, off_x + 0x44, off_y + 0x15a, 1, alpha, 0xa0);  /* 535 */
}

/* The pad-disconnected notice sits over its own black fill at priority 0, in
 * front of everything else, and is drawn at a hardcoded full alpha rather than
 * the caller's. */
static void MisPausePadErrorMsgDisp(int off_x, int off_y, u_char alpha) /* 547 */
{
    DISP_SQAR dsq;
    SQAR_DAT  bg_base = { 640, 448, 0, 0, 0, 0, 0, 0, 0x40 };           /* 549 */

    (void)off_x;
    (void)off_y;
    (void)alpha;

    CopySqrDToSqr(&dsq, &bg_base);                                      /* 554 */
    DispSqrD(&dsq);                                                     /* 555 */

    DrawCmnTwoLineWindow(0, 45.0f, 126.0f, 550.0f, 216.0f, 0x80, 0x80); /* 559 */

    PrintMsg(0x41, 0x35, 0x5c, 0x8e, 1, 0x80, 0);                       /* 561 */
}
