/* ==========================================================================
 *  ingame/savepoint/savepoint_fade_in.h
 *
 *  The prompt that stands between the player and the save-point menu
 *  (savepoint_fade_in.o, .text 0x2477f0).
 *
 *  GID_SAVEPOINT_FADEIN keeps the room running underneath: the player, the
 *  sister, the ghosts and the camera all still tick, and this module only
 *  draws a message window over the top and waits for the confirm button.
 *  Pressing it fades to black over 30 frames and hands off to
 *  GID_SAVEPOINT_TOP.
 *
 *  Reconstructed from the Feb 6 2004 prototype (SLES_523.84).
 * ======================================================================== */

#ifndef _INGAME_SAVEPOINT_SAVEPOINT_FADE_IN_H
#define _INGAME_SAVEPOINT_SAVEPOINT_FADE_IN_H

#include "eetypes.h"

/* save_point_fade_ctrl.step */
#define SAVEPOINT_FADE_IN_STEP_MSG      0   /* prompt up, waiting on the pad  */
#define SAVEPOINT_FADE_IN_STEP_FADE     1   /* fading to black, 30 frames     */

typedef struct                      /* 0x8 */
{
    /* 0x0 */ char step;
    /* 0x4 */ int  fade_timer;
} SAVE_POINT_FADE_IN_CTRL;

/* Reset for a fresh visit.  Called from SavePointBackGroundLoadReq(), i.e.
 * once per room load, not once per save point. */
void SavePointFadeInCtrlInit(void);

/* Per-frame control and draw, both driven from one_SavePoint_FadeIn(). */
void SavePointFadeInMain(void);
void SavePointFadeInDispMain(void);

#endif /* _INGAME_SAVEPOINT_SAVEPOINT_FADE_IN_H */
