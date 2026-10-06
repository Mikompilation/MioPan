/* ==========================================================================
 *  ingame/savepoint/savepoint_disp.h
 *
 *  Save-point screen drawing primitives (savepoint_disp.o, .text 0x246e28).
 *
 *  The pieces the rest of the folder draws itself out of: the animated
 *  background, the black screen fill both fades run through, the menu window
 *  and its confirm window, and the caption plate.
 *
 *  Reconstructed from the Feb 6 2004 prototype (SLES_523.84).
 * ======================================================================== */

#ifndef _INGAME_SAVEPOINT_SAVEPOINT_DISP_H
#define _INGAME_SAVEPOINT_SAVEPOINT_DISP_H

#include "eetypes.h"

/* The menu frame the three options sit in, plus its divider rule. */
void SavePoint_MenuWinDisp(int off_x, int off_y, u_char alpha);

/* The yes/no confirm window drawn over it.  `cursor` is 0 = yes, 1 = no and
 * is asserted in range. */
void SavePoint_MenuConfWinDisp(int cursor, int off_x, int off_y, u_char alpha);

/* One frame of the animated background, out of the pak at `pk2_addr`.  The
 * three timers are owned by the caller (savepoint_main.c) and advanced here;
 * bg wraps at 900 frames, moyou1 at 900 and moyou2 at 600. */
void SavePoint_BgDisp(int *bg_anim_timer, int *moyou1_anim_timer,
                      int *moyou2_anim_timer, void *pk2_addr);

/* Full-screen black quad.  Both fades and the background's own base layer go
 * through this one call. */
void SavePoint_BlackBgDisp(u_char alpha);

/* The screen's caption plate (caption group 0, world 0). */
void SavePointTopCaptionDisp(int off_x, int off_y, u_char alpha);

#endif /* _INGAME_SAVEPOINT_SAVEPOINT_DISP_H */
