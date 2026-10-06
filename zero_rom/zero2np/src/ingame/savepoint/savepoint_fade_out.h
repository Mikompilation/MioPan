/* ==========================================================================
 *  ingame/savepoint/savepoint_fade_out.h
 *
 *  The way back into the room (savepoint_fade_out.o, .text 0x247aa0).
 *
 *  GID_SAVEPOINT_FADEOUT is the mirror of GID_SAVEPOINT_FADEIN: the room is
 *  running again underneath, a black quad clears off it over 30 frames, and
 *  the phase then returns to GID_STORY_NORMAL.  The smallest translation unit
 *  in the folder -- two functions and one counter.
 *
 *  Reconstructed from the Feb 6 2004 prototype (SLES_523.84).
 * ======================================================================== */

#ifndef _INGAME_SAVEPOINT_SAVEPOINT_FADE_OUT_H
#define _INGAME_SAVEPOINT_SAVEPOINT_FADE_OUT_H

void SavePointFadeOutInit(void);
void SavePointFadeOutDispMain(void);

#endif /* _INGAME_SAVEPOINT_SAVEPOINT_FADE_OUT_H */
