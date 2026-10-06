/* ==========================================================================
 *  outgame/newgame.h
 *
 *  The new-game (difficulty select) screen, newgame.c.
 *
 *  Reconstructed from the Feb 6 2004 prototype (SLES_523.84).
 * ======================================================================== */

#ifndef _OUTGAME_NEWGAME_H
#define _OUTGAME_NEWGAME_H

#include <sys/types.h>              /* u_char */

/* newgame.c's only static (sbss 3f4ea0). */
typedef struct                      /* 0x4 */
{
    /* 0x0 */ char  mode;           /* 0 = selecting, 1 = fading out       */
    /* 0x1 */ char  cursor;         /* 0 = easy, 1 = normal; starts on 1   */
    /* 0x2 */ short wait_timer;
} NEW_GAME_CTRL;

void NewGameCtrlInit(void);         /* 0x226328 */
void NewGameMain(void);             /* 0x226340 */
void NewGameDispMain(void);         /* 0x2265c0 */

#endif /* _OUTGAME_NEWGAME_H */
