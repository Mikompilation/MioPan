/* ==========================================================================
 *  save_load/prg/game_data_save.h
 *
 *  The game-data save screen (game_data_save.o, .text 0x1a7d58).
 *
 *  The write-side twin of outgame/loadgame.c: same five slots, same drawing
 *  layer (save_load_disp.o), same 31-state memory-card machine -- but where
 *  the load screen only ever reads, this one has to create the directory,
 *  format the card, and merge the card's clear flags back into the live ones.
 *
 *  Two callers drive it, both through the same four entry points: setup.c
 *  (Mission Mode's own save) and savepoint_main.c (GID_SAVEPOINT_SAVE).
 *  GameDataSaveBackGroundLoadReq()/GameDataSaveTexMemFree() are separate
 *  because the assets are claimed at room load, long before the screen opens,
 *  and the allocator is passed in so the same code serves the outgame heap and
 *  the ingame one.
 *
 *  Reconstructed from the Feb 6 2004 prototype (SLES_523.84).
 * ======================================================================== */

#ifndef _SAVE_LOAD_PRG_GAME_DATA_SAVE_H
#define _SAVE_LOAD_PRG_GAME_DATA_SAVE_H

/* Claim and load the screen's two paks through the caller's allocator, and
 * release them again.  The pair must bracket: BackGroundLoadReq() asserts if
 * the allocator is already set, and only TexMemFree() clears it. */
void GameDataSaveBackGroundLoadReq(void *(*mem_get)(int), void (*mem_free)(void *));
void GameDataSaveTexMemFree(void);

/* Open the screen.  exe_label picks what the save records as its clear state:
 *      0  an ordinary save          -- clears ingame_wrk.clear_save_flg
 *      1  a post-clear save         -- raises it
 *      2  keep whatever it already is
 * Anything else asserts inside the two save steps. */
void GameDataSaveInit(char exe_label);

/* Per-frame.  Non-zero once the screen has finished animating out, which is
 * the caller's cue to leave the phase. */
int  GameDataSaveMain(void);
void GameDataSaveDispMain(void);

/* Release the work buffers and hand the card back.  Does not free the paks --
 * that is GameDataSaveTexMemFree()'s job. */
void GameDataSaveEnd(void);

#endif /* _SAVE_LOAD_PRG_GAME_DATA_SAVE_H */
