/* ==========================================================================
 *  ingame/gameover/prg/gameover.h
 *
 *  The game-over sequence (gameover.o, .text 0x1ac4b8).
 *
 *  Three phases run back to back once the player dies, and none of them is a
 *  menu -- the menu proper is GID_GAMEOVER_MENU, which gameover_menu.o owns:
 *
 *      GID_STORY_GAMEOVER_EFF    one frame of the room, then hand on
 *      GID_STORY_GAMEOVER_FADE   the same frame plus a 30-frame fade to black
 *      GID_STORY_GAMEOVER_MOVIE  the "game over" movie, then the menu
 *
 *  The room never stops during the first two: both phases run the player, the
 *  sister, the ghosts, the fog and the whole 3D draw every frame, exactly as
 *  savepoint.o's two fade phases do.  Only the black quad takes it away.
 *
 *  Reconstructed from the Feb 6 2004 prototype (SLES_523.84).
 * ======================================================================== */

#ifndef _INGAME_GAMEOVER_PRG_GAMEOVER_H
#define _INGAME_GAMEOVER_PRG_GAMEOVER_H

#include "eetypes.h"
#include "../../../main/phasefunc.h"                /* GPHASE_ENUM */

/* Frames the fade to black takes.  The same literal drives both the phase
 * change in GameOverFadeMain() and fade_alpha_tbl[]'s end_time. */
#define GAMEOVER_FADE_TIME      30

/* Full-screen black quad behind the fade and behind the menu.  Exported, but
 * the only `jal` to it in the whole loadable image is GameOverFadeDispMain()'s
 * -- gameover_menu.o draws its own background out of the pak instead. */
void GameOverScreenBgDisp(u_char alpha, u_char r, u_char g, u_char b,
                          u_int pri);

#endif /* _INGAME_GAMEOVER_PRG_GAMEOVER_H */
