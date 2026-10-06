// FILE: /home/zero_rom/zero2np/src/ingame/savepoint/savepoint_fade_out.c
//
// Coming back out of the save point.
//
// one_SavePoint_FadeOut() has already restarted the room -- player, sister,
// ghosts, fog and the 3D draw all run again -- so all this does is clear the
// black quad off the top of it over 30 frames and then return to
// GID_STORY_NORMAL.
//
// Unlike the fade-in side there is no step: the counter alone is the state,
// and it starts at 0 because init_SavePoint_FadeOut() calls Init() first.
//
// Reconstructed from the Feb 6 2004 prototype (SLES_523.84),
// savepoint_fade_out.o.
//
// Trailing /* NNN */ comments are the original source line numbers, measured
// from the $LM stabs.

#include "savepoint_fade_out.h"

#include "savepoint_disp.h"                         /* SavePoint_BlackBgDisp  */

#include "../menu/zero2_anim2d.h"                   /* Zero2Anim2D_FadeOut... */
#include "../../main/gphase.h"                      /* SetNextGPhase          */

/* Frames the black takes to clear. */
#define SAVEPOINT_FADE_OUT_TIME     30

static int save_point_fade_timer;                           /* sdata 3f3c98 */

void SavePointFadeOutInit(void)                                         /* 34 */
{
    save_point_fade_timer = 0;                                          /* 37 */
}

/* Zero2Anim2D_FadeOutAnimCtrl() post-increments the counter, so the test
 * below sees the value for the *next* frame: the phase change is requested on
 * the same frame the ramp reaches 0, which is exactly what keeps that
 * helper's return-0x80 bug off screen. */
void SavePointFadeOutDispMain(void)                                     /* 49 */
{
    u_char alpha;

    alpha = Zero2Anim2D_FadeOutAnimCtrl(&save_point_fade_timer,
                                        SAVEPOINT_FADE_OUT_TIME);       /* 57 */

    SavePoint_BlackBgDisp(alpha);                                       /* 60 */

    if (save_point_fade_timer >= SAVEPOINT_FADE_OUT_TIME) {             /* 62 */
        SetNextGPhase(GID_STORY_NORMAL);                                /* 64 */
    }
}
