/* ==========================================================================
 *  ingame/savepoint/savepoint.h
 *
 *  The save point's outer bracket (savepoint.o, .text 0x246a78).
 *
 *  Three things live here: the request an event script makes to open a save
 *  point, the room-load hook that claims every asset the screen needs, and
 *  the two "still in the room" phases that bracket the menu -- FADEIN, which
 *  shows the prompt, and FADEOUT, which puts the player back.
 *
 *  The five phases in order:
 *
 *      GID_SAVEPOINT_FADEIN    room running, prompt up, fade to black
 *      GID_SAVEPOINT_MAIN      parent of the next three (savepoint_main.c)
 *        GID_SAVEPOINT_TOP       the menu          (savepoint_top.c)
 *        GID_SAVEPOINT_SAVE      the card screen   (game_data_save.c)
 *        GID_SAVEPOINT_ALBUM     the photo album   (album.c)
 *      GID_SAVEPOINT_FADEOUT   room running again, fade back in
 *
 *  Reconstructed from the Feb 6 2004 prototype (SLES_523.84).
 * ======================================================================== */

#ifndef _INGAME_SAVEPOINT_SAVEPOINT_H
#define _INGAME_SAVEPOINT_SAVEPOINT_H

#include "../../main/phasefunc.h"

/* Open a save point.  Called from the event script; refuses while a ghost is
 * active or the heap is too tight to hold the screen's assets. */
void SavePointStartReq(void);

/* Room-load hooks: claim/free every pak the save-point screen needs, plus the
 * album's and the save screen's.  Driven from the room loader, so the whole
 * screen is resident before the player ever touches the save point. */
void SavePointBackGroundLoadReq(void);
void SavePointEnd(void);

/* GPhase callbacks.  savepoint.c owns the two fade phases; savepoint_main.c
 * owns the other four sets. */
void        init_SavePoint_FadeIn(void);
GPHASE_ENUM one_SavePoint_FadeIn(GPHASE_ENUM dummy);
void        end_SavePoint_FadeIn(void);
void        init_SavePoint_FadeOut(void);
GPHASE_ENUM one_SavePoint_FadeOut(GPHASE_ENUM dummy);
void        end_SavePoint_FadeOut(void);

void        init_SavePoint_Main(void);
GPHASE_ENUM pre_SavePoint_Main(GPHASE_ENUM dummy);
GPHASE_ENUM after_SavePoint_Main(GPHASE_ENUM result);
void        end_SavePoint_Main(void);
void        init_SavePoint_Top(void);
GPHASE_ENUM one_SavePoint_Top(GPHASE_ENUM dummy);
void        end_SavePoint_Top(void);
void        init_SavePoint_Save(void);
GPHASE_ENUM one_SavePoint_Save(GPHASE_ENUM dummy);
void        end_SavePoint_Save(void);
void        init_SavePoint_Album(void);
GPHASE_ENUM one_SavePoint_Album(GPHASE_ENUM dummy);
void        end_SavePoint_Album(void);

#endif /* _INGAME_SAVEPOINT_SAVEPOINT_H */
