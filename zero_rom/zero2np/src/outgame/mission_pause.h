/* ==========================================================================
 *  outgame/mission_pause.h
 *
 *  The mission-mode pause menu (mission_pause.o): three rows over a captured
 *  copy of the frame the game was on, a "return to the mission list?" window,
 *  and the pad-disconnected notice.
 *
 *  Reconstructed from the Feb 6 2004 prototype (SLES_523.84).
 * ======================================================================== */

#ifndef _OUTGAME_MISSION_PAUSE_H
#define _OUTGAME_MISSION_PAUSE_H

/* The work block is a PAUSE_CTRL -- the same layout ingame/pause/prg/pause.c
 * uses, which is why the type lives in that header rather than either .c.
 * This file's instance is bss 4b6448; pause.c's is 4bbaf8. */
#include "../ingame/pause/prg/pause.h"      /* PAUSE_CTRL */

void MisPauseInit(void);            /* 0x2151d8 */

/* Always returns 0; ingame.c draws the menu only on 0, so the pause screen is
 * never skipped. */
int  MisPauseMain(void);            /* 0x215298 */

void MisPauseDispMain(void);        /* 0x2157c0 */

#endif /* _OUTGAME_MISSION_PAUSE_H */
