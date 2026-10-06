// FILE: /home/zero_rom/zero2np/src/graphics/graph2d/fade.c
//
// Full-screen colour fade.  A single fade-control block (fade_ctrl) holds the
// fade colour, the current overlay alpha, the per-frame alpha step and a small
// state machine; FadeMain() draws a full-screen square of the fade colour at
// the current alpha each frame and advances the state.
//
//   state 0 = idle (no step)        state 1 = fading in  (alpha -> 0)
//   state 3 = fading out (alpha -> 0x80)
//
//   * FadeCtrlInit() - reset the block (idle, alpha 0x80).
//   * FadeMain()     - per-frame: step the alpha and draw the overlay.
//   * GetFadeState() - 1 when idle (fade complete).
//   * FadeInReq()    - start a fade-in over fade_in_time frames.
//   * FadeOutReq()   - start a fade-out over fade_out_time frames.
//
// Reconstructed from the Feb 6 2004 prototype (SLES_523.84).

#include "g2d_draw.h"               // DISP_SQAR / SQAR_DAT / CopySqrDToSqr / DispSqrD
#include "fade.h"                   // this file's API + FADE_MODE_CTRL

#include <string.h>                 // memset
#include <stdio.h>                  // printf

// ──────────────────────────────────────────────────────────────────────
// Fade-control state block.

FADE_MODE_CTRL fade_ctrl;           // data 312220

// ──────────────────────────────────────────────────────────────────────
// Reset to idle with the overlay fully transparent (alpha 0x80 = clear).

void FadeCtrlInit(void)
{
    memset(&fade_ctrl, 0, sizeof(FADE_MODE_CTRL));
    fade_ctrl.alpha = 0x80;
}

// ──────────────────────────────────────────────────────────────────────
// Per-frame fade.  Build a full-screen square of the fade colour at the
// current alpha, step the state machine, and draw it unless fully clear.

void FadeMain(void)
{
    DISP_SQAR dsq;
    SQAR_DAT  fade_bg;

    fade_bg.w = 640;
    fade_bg.h = 448;
    fade_bg.x = 0;
    fade_bg.y = 0;
    fade_bg.pri = 0;
    fade_bg.r = fade_ctrl.r;
    fade_bg.g = fade_ctrl.g;
    fade_bg.b = fade_ctrl.b;
    fade_bg.alpha = fade_ctrl.alpha;
    CopySqrDToSqr(&dsq, &fade_bg);

    switch (fade_ctrl.fade_state)
    {
    case 0:
    case 6:
    case '\a':
        {
            break;
        }
    case 1:
        {
            fade_ctrl.alpha = fade_ctrl.alpha - fade_ctrl.change_alp;
            if ((int)((u_int)fade_ctrl.alpha - (u_int)fade_ctrl.change_alp) < 1)
            {
                fade_ctrl.fade_state = 0;
                fade_ctrl.alpha = 0;
            }
            break;
        }
    case 3:
        {
            fade_ctrl.alpha = fade_ctrl.alpha + fade_ctrl.change_alp;
            if ((int)((u_int)fade_ctrl.alpha << 0x18) < 0)
            {
                fade_ctrl.fade_state = 0;
                fade_ctrl.alpha = 0x80;
            }
            break;
        }
    case 2:
    case 4:
    case 5:
    default:
        {
            printf("ERROR!! FadeMain()\n");
            break;
        }
    }

    if (fade_ctrl.alpha != 0)
    {
        DispSqrD(&dsq);
    }
}

// ──────────────────────────────────────────────────────────────────────
// True (1) once the fade has finished (state returned to idle).

int GetFadeState(void)
{
    return (int)(fade_ctrl.fade_state == 0);
}

// ──────────────────────────────────────────────────────────────────────
// Request a fade-in of the given colour over fade_in_time frames.

void FadeInReq(u_char r, u_char g, u_char b, u_int fade_in_time)
{
    fade_ctrl.alpha = 0x80;
    fade_ctrl.r = r;
    fade_ctrl.g = g;
    fade_ctrl.b = b;
    fade_ctrl.fade_state = 1;

    if (fade_in_time == 0)
    {
        fade_ctrl.alpha = 0;
        fade_ctrl.fade_state = 0;
        fade_ctrl.change_alp = 0;
        return;
    }

    if (0x80 < fade_in_time)
    {
        fade_ctrl.change_alp = 1;
        return;
    }

    fade_ctrl.change_alp = (u_char)(0x80 / (int)fade_in_time);
}

// ──────────────────────────────────────────────────────────────────────
// Request a fade-out to the given colour over fade_out_time frames.

void FadeOutReq(u_char r, u_char g, u_char b, u_int fade_out_time)
{
    fade_ctrl.fade_state = 3;
    fade_ctrl.r = r;
    fade_ctrl.g = g;
    fade_ctrl.b = b;
    fade_ctrl.alpha = 0;

    if (fade_out_time == 0)
    {
        fade_ctrl.fade_state = 0;
        fade_ctrl.change_alp = 0;
        fade_ctrl.alpha = 0x80;
        return;
    }

    if (0x80 < fade_out_time)
    {
        fade_ctrl.change_alp = 1;
        return;
    }

    fade_ctrl.change_alp = (u_char)(0x80 / (int)fade_out_time);
    fade_ctrl.alpha = (u_char)(0x80 % (int)fade_out_time);
}
