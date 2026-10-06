/* ==========================================================================
 *  ingame/movie_room_menu/prg/movie_projecter.h
 *
 *  movie_projecter.o -- the two projectors standing in the movie room.
 *
 *  This is the room-side half of the movie room: it knows which of the two
 *  projectors the player is standing in front of (from the room ID alone),
 *  which reel is loaded in each, and it drives CMovieRoom's
 *  PreLoad -> PlayFilm -> Work -> Release loop for whichever one is armed.
 *  movie_room_menu.o is the other half -- the reel-selection menu that calls
 *  SetFilmNo/GetFilmNo/TakeFilm/Play.
 *
 *  Reconstructed from the Feb 6 2004 prototype (SLES_523.84).
 * ======================================================================== */

#ifndef _INGAME_MOVIE_PROJECTER_H
#define _INGAME_MOVIE_PROJECTER_H

#include "../../../common/save_data.h"          /* MC_SAVE_DATA */

void movie_projecterInit(void);                 /* 0x21f230 */
void movie_projecterRelease(void);              /* 0x21f290 -- dead code    */

/* Save-block descriptor for the two film slots the projectors are loaded
 * with -- entry 40 of system/mc/dat/save_data.c's save_game_data[]. */
void movie_projecterSetSave(MC_SAVE_DATA *save);                /* 0x21f2f0 */

void movie_projecterSetFilmNo(int iFilmNo);     /* 0x21f330 */
int  movie_projecterGetFilmNo(void);            /* 0x21f380 */
int  movie_projecterTakeFilm(void);             /* 0x21f3d0 */

void movie_projecterStop(void);                 /* 0x21f440 */
int  movie_projecterIsReq(void);                /* 0x21f468 */
int  movie_projecterPlay(void);                 /* 0x21f4c0 */
int  movie_projecterWork(void);                 /* 0x21f530 */
void movie_projecterDraw(void);                 /* 0x21f628 */

#endif /* _INGAME_MOVIE_PROJECTER_H */
