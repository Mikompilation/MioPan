/* ==========================================================================
 *  ingame/menu/zero2_anim2d.h
 *
 *  Shared 2D menu animation helpers (zero2_anim2d.o, .text 0x26d010).
 *
 *  Five canned curves that every menu screen in the game drives its fades and
 *  cursors from.  Each one owns the ALPHA_ANIM_TBL / RGB_ANIM_TBL it feeds to
 *  anim_2d.c and advances the caller's counter in place, so a screen keeps
 *  nothing but the counter (and, for the in/out pair, a step).
 *
 *  Reconstructed from the Feb 6 2004 prototype (SLES_523.84).
 * ======================================================================== */

#ifndef _INGAME_MENU_ZERO2_ANIM2D_H
#define _INGAME_MENU_ZERO2_ANIM2D_H

#include "eetypes.h"

/* Step values *anim_step walks through in Zero2Anim2D_InOutAnimCtrl().
 * 0 is "not started"; the function rewrites it to IN on the first call. */
#define ZERO2_ANIM2D_STEP_START     0   /* seed: reset the timer and go to IN */
#define ZERO2_ANIM2D_STEP_IN        1   /* fading up,   alpha 0 -> 128        */
#define ZERO2_ANIM2D_STEP_SHOW      2   /* held open,   alpha 128             */
#define ZERO2_ANIM2D_STEP_OUT       3   /* fading down, alpha 128 -> 0        */
#define ZERO2_ANIM2D_STEP_END       4   /* closed,      alpha 0               */

/* Two-phase open/close fade.  Returns this frame's alpha and advances
 * *anim_step / *anim_timer.  A screen requests the close by storing
 * ZERO2_ANIM2D_STEP_OUT into *anim_step and zeroing *anim_timer, then waits
 * for ZERO2_ANIM2D_STEP_END.  in_anim_time / out_anim_time are the two
 * durations in frames; both counters are char, so neither may exceed 127. */
u_char Zero2Anim2D_InOutAnimCtrl(char *anim_step, char *anim_timer,
                                 short in_anim_time, short out_anim_time);

/* Cursor pulse.  Advances the caller's frame counter in place and writes the
 * current intensity to *rgb; the save/load screens seed *rgb at 0x80 and pass
 * the result straight to SaveLoadCursorDisp().  45-frame loop. */
void Zero2Anim2D_CsrAnimCtrl(char *timer, u_char *rgb);         /* 0x26d2d0 */

/* Selected-item pulse: a 30-frame 128 -> 64 -> 128 alpha loop. */
u_char Zero2Anim2D_SelAnimCtrl(char *timer);                    /* 0x26d330 */

/* Screen-black ramps.  FadeIn drives 0 -> 128 (black closing over the scene),
 * FadeOut 128 -> 0 (black clearing off it), both over fade_time frames, and
 * both post-increment *timer.  Unlike the pair above these take an int
 * counter, so a caller can let the timer run past fade_time. */
u_char Zero2Anim2D_FadeInAnimCtrl(int *timer, short fade_in_time);   /* 0x26d380 */
u_char Zero2Anim2D_FadeOutAnimCtrl(int *timer, short fade_out_time); /* 0x26d410 */

#endif /* _INGAME_MENU_ZERO2_ANIM2D_H */
