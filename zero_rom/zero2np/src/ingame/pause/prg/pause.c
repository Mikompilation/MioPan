// FILE: /home/zero_rom/zero2np/src/ingame/pause/prg/pause.c
//
// Ingame pause screen (pause.o).  GID_STORY_PAUSE owns it: init_Story_Pause()
// calls PauseInit(), one_Story_Pause() calls PauseMain() and PauseDispMain()
// every frame and acts on PauseMain()'s return code.
//
//   0  stay paused
//   1  resume the game        (one_Story_Pause -> SetIngamePauseMode(0))
//   2  return to the title     (GID_TITLE_TOP)
//   3  open the debug menu     (GID_STORY_DEBUG)
//
// The screen is a still frame plus an overlay: PauseInit() blits the just-
// finished back buffer into the VRAM scratch at 0x2bc0 (PauseInDispCaptuer)
// and PauseCaptureDataDisp() copies it back into the buffer being drawn each
// frame, so the paused world underneath never re-renders.  end_Story_Pause()
// in ingame.c performs the matching restore on the way out.
//
// pause_ctrl.step drives three sub-states: 0 the menu, 1 the "return to
// title?" confirm, 2 the controller-unplugged message.  before_step remembers
// what to go back to once the pad is plugged in again.
//
// Reconstructed from the Feb 6 2004 prototype (SLES_523.84).

#include "pause.h"

#include "pause_dat.h"                              // pause_tex[]
#include "../../../common/utility2.h"               // PRINT_ASSERT / PRINT_WARNING
#include "../../../common/variable.h"               // sys_wrk / pad / key_now
#include "../../../graphics/graph2d/draw_cmn.h"     // DrawCmn* widgets
#include "../../../graphics/graph2d/g2d_draw.h"     // DISP_SPRT / SQAR_DAT / LocalCopyLtoL
#include "../../../graphics/graph2d/message.h"      // PrintMsg / PrintMsg_Arrange
#include "../../../outgame/option.h"                // GetOptionVib / OptionVibChange
#include "../../../system/eeiop/snd_buffer.h"       // SndBufAllPause
#include "../../../system/eeiop/stream_auto.h"      // StreamAutoAllPause
#include "../../../system/os/system.h"              // SystemBankPlay / SND_3D_SET
#include "../../../system/pad/pad.h"                // paddat / pad / VibrateRequest

/* Rows of the pause menu: continue / return to title / vibration toggle. */
#define PAUSE_MENU_COUNT 3

/* VRAM word address of the odd frame buffer; the even one starts at 0.  The
 * ROM reaches it as `(count & 1) * 0x1180`, which GCC strength-reduced into
 * the dsll/dsubu chain Ghidra prints as `* 0x23 << 0x27 >> 0x20`. */
#define PAUSE_FRAME_BUF_ADRS 0x1180

/* Scratch VRAM the paused frame is parked in while the overlay is drawn. */
#define PAUSE_CAPTURE_ADRS 0x2bc0

static PAUSE_CTRL pause_ctrl;       /* bss 4bbaf8 */

static void PauseCtrlInit(void);
static void PauseInDispCaptuer(void);
static int  PausePad(void);
static int  PauseMenuSel(void);
static int  PauseReturnTitlePad(void);
static void PausePadErrorPad(void);
static void PauseCaptureDataDisp(void);
static void PauseWinDisp(int off_x, int off_y, u_char alpha);
static void PauseTitleDisp(int off_x, int off_y, u_char alpha);
static void PauseMenuCsrDisp(int off_x, int off_y, u_char alpha);
static void PauseMenuDisp(int off_x, int off_y, u_char alpha);
static void PauseMenuReturnTitleWinDisp(int off_x, int off_y, u_char alpha);
static void PausePadErrorMsgDisp(int off_x, int off_y, u_char alpha);

/* ==========================================================================
 *  Entry / control
 * ======================================================================== */

void PauseInit(void)
{
    PauseCtrlInit();                                                    /* 132 */
    PauseInDispCaptuer();                                               /* 135 */

    /* Streams first, then the sound buffers -- the stream mixer feeds the
     * buffers, so pausing it the other way round leaves a tail playing. */
    StreamAutoAllPause();                                               /* 138 */
    SndBufAllPause();                                                   /* 139 */
}

static void PauseCtrlInit(void)
{
    pause_ctrl.step = 0;                                                /* 151 */
    pause_ctrl.before_step = 0;                                         /* 152 */
    pause_ctrl.csr = 0;                                                 /* 153 */
    pause_ctrl.title_csr = 0;                                           /* 154 */
    pause_ctrl.vib_csr = GetOptionVib();                                /* 155 */
    pause_ctrl.vib_time = 0;                                            /* 156 */
}

/* Park the frame that was just finished (the one about to be shown) in the
 * VRAM scratch area, so every paused frame can be rebuilt from it. */
static void PauseInDispCaptuer(void)
{
    LocalCopyLtoL(0,                                                    /* 167 */
                  (int)(((sys_wrk.count + 1) & 1) * PAUSE_FRAME_BUF_ADRS),
                  PAUSE_CAPTURE_ADRS);
}

int PauseMain(void)
{
    int ret;

    ret = 0;

    /* Losing the pad -- or having something that is not a DUALSHOCK2 plugged
     * in -- forces the error message over whatever step was running. */
    if ((padIsConnected(0) == 0) || (GetPadDUALSHOCK2(0) == 0)) {       /* 193 */
        pause_ctrl.step = 2;                                            /* 194 */
    }

    switch (pause_ctrl.step) {                                          /* 197 */
    case 0:
        ret = PausePad();                                               /* 199 */
        break;                                                          /* 200 */
    case 1:
        ret = PauseReturnTitlePad();                                    /* 202 */
        break;                                                          /* 203 */
    case 2:
        PausePadErrorPad();                                             /* 205 */
        break;                                                          /* 206 */
    default:
        PRINT_ASSERT("Error! %s", __FUNCTION__);                        /* 208 */
        break;
    }

    /* Toggling vibration on buzzes the pad for 20 frames so the setting can
     * be felt straight away. */
    if (pause_ctrl.vib_time != 0) {                                     /* 211 */
        VibrateRequest(0, 0xb4, 0xb4);                                  /* 212 */
        pause_ctrl.vib_time--;                                          /* 213 */
    }

    return ret;                                                         /* 217 */
}

static int PausePad(void)
{
    int ret;

    ret = 0;

    /* pad.rpt bits are the remapped word PadReadFunc() builds: 0x1000 UP,
     * 0x2000 RIGHT, 0x4000 DOWN, 0x8000 LEFT.  GetPadAnalogRpt() indices run
     * in the same order (0 up, 1 down, 2 left, 3 right), so the stick repeats
     * drive the cursor exactly like the d-pad. */
    if (((pad[0].rpt & 0x1000U) != 0) || (GetPadAnalogRpt(0) != 0)) {   /* 232 */
        SystemBankPlay(0, 1, 0, 0, (SND_3D_SET *)nullptr, 0x3200, 0x1000);   /* 233 */
        pause_ctrl.csr =                                                /* 234 */
            (char)((pause_ctrl.csr + (PAUSE_MENU_COUNT - 1)) % PAUSE_MENU_COUNT);
    } else if (((pad[0].rpt & 0x4000U) != 0) || (GetPadAnalogRpt(1) != 0)) {  /* 237 */
        SystemBankPlay(0, 1, 0, 0, (SND_3D_SET *)nullptr, 0x3200, 0x1000);   /* 238 */
        pause_ctrl.csr = (char)((pause_ctrl.csr + 1) % PAUSE_MENU_COUNT);    /* 239 */
    } else if (*paddat[0] == 1) {                                       /* 242 */
        SystemBankPlay(3, 1, 0, 0, (SND_3D_SET *)nullptr, 0x3200, 0x1000);   /* 243 */
        ret = PauseMenuSel();                                           /* 244 */
    } else if (*key_now[0xc] == 1) {                                    /* 247 */
        /* key_now[12] is pad[0].cnt[12], the START hold counter: the button
         * that opened the pause screen also closes it. */
        ret = 1;                                                        /* 248 */
    } else if (*key_now[0xd] == 1) {                                    /* 252 */
        /* key_now[13] is SELECT -- the same debug-menu shortcut ingame.c
         * honours during normal play. */
        ret = 3;                                                        /* 253 */
    }

    return ret;                                                         /* 258 */
}

static int PauseMenuSel(void)
{
    int ret;

    ret = 0;

    switch (pause_ctrl.csr) {                                           /* 269 */
    case 0:
        ret = 1;                                                        /* 271 */
        break;                                                          /* 272 */
    case 1:
        /* Ghidra prints these three as `= pause_ctrl.csr`: GCC knew the
         * switch had already proved the register equal to 1 and reused it. */
        pause_ctrl.step = 1;                                            /* 274 */
        pause_ctrl.before_step = 1;                                     /* 275 */
        pause_ctrl.title_csr = 1;                                       /* 276 */
        break;                                                          /* 277 */
    case 2:
        pause_ctrl.vib_csr ^= 1;                                        /* 279 */
        OptionVibChange(pause_ctrl.vib_csr);                            /* 280 */

        if (pause_ctrl.vib_csr != 0) {                                  /* 283 */
            pause_ctrl.vib_time = 20;                                   /* 284 */
        } else {
            pause_ctrl.vib_time = 0;                                    /* 290 */
        }
        break;
    default:
        PRINT_WARNING("Error!! PauseMenuSel()");                        /* 292 */
        break;
    }

    return ret;                                                         /* 295 */
}

static int PauseReturnTitlePad(void)
{
    int ret;

    ret = 0;

    /* Left (0x8000) or right (0x2000) just flips between YES and NO. */
    if ((((pad[0].rpt & 0x8000U) != 0) || (GetPadAnalogRpt(2) != 0)) ||  /* 307 */
        (((pad[0].rpt & 0x2000U) != 0) || (GetPadAnalogRpt(3) != 0))) {  /* 312 */
        SystemBankPlay(0, 1, 0, 0, (SND_3D_SET *)nullptr, 0x3200, 0x1000);   /* 313 */
        pause_ctrl.title_csr ^= 1;                                      /* 314 */
    } else if (*paddat[0] == 1) {                                       /* 317 */
        SystemBankPlay(3, 1, 0, 0, (SND_3D_SET *)nullptr, 0x3200, 0x1000);   /* 318 */

        if (pause_ctrl.title_csr == 0) {                                /* 320 */
            ret = 2;                                                    /* 321 */
        } else {
            /* NO: fall back to the menu.  GCC cross-jumped this into the
             * cancel branch below, which is why both carry lines 332/333. */
            pause_ctrl.step = 0;
            pause_ctrl.before_step = 0;
        }
    } else if (*paddat[1] == 1) {                                       /* 330 */
        SystemBankPlay(1, 1, 0, 0, (SND_3D_SET *)nullptr, 0x3200, 0x1000);   /* 331 */
        pause_ctrl.step = 0;                                            /* 332 */
        pause_ctrl.before_step = 0;                                     /* 333 */
    }

    return ret;                                                         /* 337 */
}

/* The pad-error step is the only one that keeps polling the connection: once
 * a DUALSHOCK2 is back and the player acknowledges, step returns to whatever
 * was running when the pad went away. */
static void PausePadErrorPad(void)
{
    if (((padIsConnected(0) != 0) && (GetPadDUALSHOCK2(0) != 0)) &&     /* 348 */
        (*paddat[0] == 1)) {                                            /* 349 */
        SystemBankPlay(3, 1, 0, 0, (SND_3D_SET *)nullptr, 0x3200, 0x1000);   /* 350 */
        pause_ctrl.step = pause_ctrl.before_step;                       /* 352 */
    }
}

/* ==========================================================================
 *  Display
 * ======================================================================== */

void PauseDispMain(void)
{
    PauseCaptureDataDisp();                                             /* 370 */

    PauseWinDisp(0, 0, 0x80);                                           /* 373 */
    PauseTitleDisp(0, 0, 0x80);                                         /* 375 */
    PauseMenuDisp(0, 0, 0x80);                                          /* 377 */
    PauseMenuCsrDisp(0, 0, 0x80);                                       /* 379 */

    if (pause_ctrl.step == 1) {                                         /* 382 */
        PauseMenuReturnTitleWinDisp(0, 0, 0x80);                        /* 383 */
    }

    if (pause_ctrl.step == 2) {                                         /* 386 */
        /* Keep the confirm window on screen behind the error, so unplugging
         * the pad mid-confirm does not make it vanish. */
        if (pause_ctrl.before_step == 1) {                              /* 387 */
            PauseMenuReturnTitleWinDisp(0, 0, 0x80);                    /* 388 */
        }
        PausePadErrorMsgDisp(0, 0, 0x80);                               /* 391 */
    }
}

/* Restore the captured frame into the buffer currently being drawn. */
static void PauseCaptureDataDisp(void)
{
    LocalCopyLtoL(0, PAUSE_CAPTURE_ADRS,                                /* 403 */
                  (int)((sys_wrk.count & 1) * PAUSE_FRAME_BUF_ADRS));
}

static void PauseWinDisp(int off_x, int off_y, u_char alpha)
{
    DISP_SQAR dsq;
    SQAR_DAT  pause_bg = { 640, 448, 0, 0, 0xa0, 0, 0, 0, 0x26 };       /* 415 */

    /* Full-screen black wash over the captured frame, alpha 0x26 of 0x80. */
    CopySqrDToSqr(&dsq, &pause_bg);                                     /* 420 */
    DispSqrD(&dsq);                                                     /* 421 */

    DrawCmnWindow(0xa0, (float)(off_x + 150), (float)(off_y + 130),     /* 425 */
                  340.0f, 172.0f, alpha, 0x59);

    /* Rule under the caption, at 76/128 of the window alpha. */
    DrawCmnLine(150.0f, 172.0f, 340.0f, 1,                              /* 429 */
                (u_char)(alpha * 76 / 128), 0xa0);
}

static void PauseTitleDisp(int off_x, int off_y, u_char alpha)
{
    DISP_SPRT title_ds;

    CopySprDToSpr(&title_ds, &pause_tex[0]);                            /* 444 */
    title_ds.x += (float)off_x;    title_ds.y += (float)off_y;          /* 445 */
    title_ds.alpha = (u_char)(title_ds.alpha * alpha >> 7);             /* 446 */
    DispSprD(&title_ds);                                                /* 447 */
}

/* off_x / off_y are ignored here in the ROM -- the cursor is pinned to the
 * window, which never moves. */
static void PauseMenuCsrDisp(int off_x, int off_y, u_char alpha)
{
    (void)off_x;
    (void)off_y;

    DrawCmnSelCsr(0x80, 162.0f,                                         /* 462 */
                  (float)(pause_ctrl.csr * 30 + 184), alpha, 316.0f, 1);
}

static void PauseMenuDisp(int off_x, int off_y, u_char alpha)
{
    int i;
    int msg_id_tbl[3] = { 0, 2, 0 };                                    /* 475 */

    (void)off_x;

    /* Rows 0 and 1 are fixed; row 2 swaps message id with the vibration
     * setting, so the row itself reads back the current state. */
    if (pause_ctrl.vib_csr != 0) {                                      /* 485 */
        msg_id_tbl[2] = 5;                                              /* 486 */
    } else {
        msg_id_tbl[2] = 4;                                              /* 490 */
    }

    for (i = 0; i < 3; i++) {                                           /* 494 */
        int col_label = (pause_ctrl.csr == i) ? 7 : 1;                  /* 495 */

        PrintMsg_Arrange(0x42, msg_id_tbl[i], 320,                      /* 503 */
                         off_y + 188 + i * 30, col_label, (int)alpha,
                         0, 0, 0, 2);
    }
}

static void PauseMenuReturnTitleWinDisp(int off_x, int off_y, u_char alpha)
{
    DrawCmnWindow(0xa0, (float)(off_x + 24), (float)(off_y + 325),      /* 520 */
                  592.0f, 112.0f, alpha, 0x59);

    /* Width 0 / flg 0: the confirm cursor is the small YES-NO highlight, not
     * the wide menu bar the main list uses.  207 is the YES-to-NO pitch. */
    DrawCmnSelCsr(0xa0,                                                 /* 524 */
                  (float)(pause_ctrl.title_csr * 207 + off_x + 155),
                  (float)(off_y + 381), alpha, 0.0f, 0);

    DrawCmnSelYes(0xa0, (float)(off_x + 153), (float)(off_y + 382), alpha);      /* 527 */
    DrawCmnSelNo(0xa0, (float)(off_x + 360), (float)(off_y + 382), alpha);       /* 528 */

    PrintMsg(0x42, 3, off_x + 68, off_y + 346, 1, (int)alpha, 0xa0);             /* 532 */
}

/* Fixed layout -- the pad-error message must land in the same place whatever
 * the caller passes, so none of the three arguments are read. */
static void PausePadErrorMsgDisp(int off_x, int off_y, u_char alpha)
{
    DISP_SQAR dsq;
    SQAR_DAT  bg_base = { 640, 448, 0, 0, 0, 0, 0, 0, 0x40 };           /* 546 */

    (void)off_x;
    (void)off_y;
    (void)alpha;

    CopySqrDToSqr(&dsq, &bg_base);                                      /* 551 */
    DispSqrD(&dsq);                                                     /* 552 */

    DrawCmnTwoLineWindow(0, 45.0f, 126.0f, 550.0f, 216.0f, 0x80, 0x80); /* 556 */
    PrintMsg(0x41, 0x35, 92, 142, 1, 0x80, 0);                          /* 558 */
}
