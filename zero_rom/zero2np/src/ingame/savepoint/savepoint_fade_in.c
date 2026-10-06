// FILE: /home/zero_rom/zero2np/src/ingame/savepoint/savepoint_fade_in.c
//
// "Do you want to save?" -- the prompt in front of the save-point menu.
//
// GID_SAVEPOINT_FADEIN is still the room: one_SavePoint_FadeIn() runs the
// player, the sister, the ghosts, the fog and the whole 3D draw every frame,
// and this module only puts a message window on top of it.  Confirm starts a
// 30-frame fade to black; when the counter is spent the phase hands over to
// GID_SAVEPOINT_TOP, which is where the menu proper lives.
//
// The window is message type 0x4b entry 6, positioned entirely from the
// message system's own defaults (SetMsgDefData / SetMsgWinDefData) rather
// than from coordinates here.
//
// Reconstructed from the Feb 6 2004 prototype (SLES_523.84),
// savepoint_fade_in.o.
//
// Trailing /* NNN */ comments are the original source line numbers, measured
// from the $LM stabs.

#include "savepoint_fade_in.h"

#include "savepoint_disp.h"                         /* SavePoint_BlackBgDisp  */

#include "../menu/zero2_anim2d.h"                   /* Zero2Anim2D_FadeIn...  */
#include "../../common/utility2.h"                  /* PRINT_ASSERT           */
#include "../../graphics/graph2d/draw_cmn.h"        /* DrawCmnWindow          */
#include "../../graphics/graph2d/g2d_draw.h"        /* DISP_STR               */
#include "../../graphics/graph2d/message.h"         /* SetMsgDefData/PrintMsg */
#include "../../main/gphase.h"                      /* SetNextGPhase          */
#include "../../system/os/system.h"                 /* SystemBankPlay         */
#include "../../system/pad/pad.h"                   /* paddat                 */

/* The message group every save-point string comes out of. */
#define SAVEPOINT_MSG_TYPE          0x4b
#define SAVEPOINT_MSG_ID_ASK_SAVE   6

/* Frames the fade to black takes. */
#define SAVEPOINT_FADE_IN_TIME      30

static void SavePointFadeInMsgDispPad(void);
static void SavePointFadeInMsgWinDisp(int off_x, int off_y, u_char alpha);

static SAVE_POINT_FADE_IN_CTRL save_point_fade_ctrl;        /* sbss 3f4f30 */

/* ==========================================================================
 *  Set-up
 * ======================================================================== */

void SavePointFadeInCtrlInit(void)                                      /* 76 */
{
    save_point_fade_ctrl.step       = SAVEPOINT_FADE_IN_STEP_MSG;       /* 79 */
    save_point_fade_ctrl.fade_timer = 0;                                /* 80 */
}

/* ==========================================================================
 *  Per-frame control
 * ======================================================================== */

void SavePointFadeInMain(void)                                          /* 92 */
{
    switch (save_point_fade_ctrl.step) {                                /* 95 */
    case SAVEPOINT_FADE_IN_STEP_MSG:
        SavePointFadeInMsgDispPad();                                    /* 98 */
        break;

    case SAVEPOINT_FADE_IN_STEP_FADE:
        /* The counter is advanced by the fade helper in
         * SavePointFadeInDispMain(), not here. */
        if (save_point_fade_ctrl.fade_timer >= SAVEPOINT_FADE_IN_TIME) { /* 101 */
            SetNextGPhase(GID_SAVEPOINT_TOP);                           /* 103 */
        }
        break;

    default:
        PRINT_ASSERT("Error! %s", __FUNCTION__);                        /* 107 */
        break;
    }
}

/* Confirm (paddat[3]) opens the menu; there is no cancel -- the player is
 * already committed by the time the prompt is up, and backing out is done
 * from the menu's own exit row. */
static void SavePointFadeInMsgDispPad(void)                             /* 116 */
{
    if (*paddat[3] == 1) {                                              /* 120 */
        SystemBankPlay(3, 1, 0, 0, (SND_3D_SET *)nullptr, 0x3200, 0x1000); /* 121 */

        save_point_fade_ctrl.step       = SAVEPOINT_FADE_IN_STEP_FADE;  /* 124 */
        save_point_fade_ctrl.fade_timer = 0;                            /* 125 */
    }
}

/* ==========================================================================
 *  Per-frame draw
 * ======================================================================== */

void SavePointFadeInDispMain(void)                                      /* 138 */
{
    u_char alpha;

    /* The prompt stays up through the fade -- it is the black quad drawn over
     * it that takes it away. */
    SavePointFadeInMsgWinDisp(0, 0, 0x80);                              /* 146 */

    if (save_point_fade_ctrl.step == SAVEPOINT_FADE_IN_STEP_FADE) {     /* 148 */
        alpha = Zero2Anim2D_FadeInAnimCtrl(&save_point_fade_ctrl.fade_timer,
                                           SAVEPOINT_FADE_IN_TIME);     /* 150 */

        SavePoint_BlackBgDisp(alpha);                                   /* 153 */
    }
}

/* Everything about this window comes from the message type's own defaults:
 * the frame from SetMsgWinDefData(), the text origin from SetMsgDefData(). */
static void SavePointFadeInMsgWinDisp(int off_x, int off_y, u_char alpha) /* 165 */
{
    DISP_STR    ds;
    MSG_WIN_DAT win_dat;

    (void)off_x;                /* both offsets are dead in the ROM */
    (void)off_y;

    SetMsgDefData(&ds, SAVEPOINT_MSG_TYPE);                             /* 171 */
    SetMsgWinDefData(&win_dat, SAVEPOINT_MSG_TYPE);                     /* 172 */

    DrawCmnWindow(0, win_dat.x, win_dat.y, win_dat.w, win_dat.h,
                  alpha, 0x80);                                         /* 176 */

    PrintMsg(SAVEPOINT_MSG_TYPE, SAVEPOINT_MSG_ID_ASK_SAVE,
             ds.pos_x, ds.pos_y, 1, (int)alpha, 0);                     /* 178 */
}
