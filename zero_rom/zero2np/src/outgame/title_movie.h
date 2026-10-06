/* ==========================================================================
 *  outgame/title_movie.h
 *
 *  The attract movie (title_movie.c): the idle timer that hands the title
 *  over to it, and the lock every title sub-screen holds to stop that.
 *
 *  Reconstructed from the Feb 6 2004 prototype (SLES_523.84).
 * ======================================================================== */

#ifndef _OUTGAME_TITLE_MOVIE_H
#define _OUTGAME_TITLE_MOVIE_H

/* title_movie.c's only static (sbss 3f4fe8).  The type is here rather than in
 * the .c because types.txt carries it alongside TITLE_WRK / TITLE_MENU_CTRL. */
typedef struct                      /* 0x6 */
{
    /* 0x0 */ short move_movie_timer;
    /* 0x2 */ char  title_movie_flg;    /* alternates title movie / demo list */
    /* 0x3 */ char  play_demo_cnt;      /* rotates through the demo list, 0..2 */
    /* 0x4 */ char  movie_lock_flg;
} TITLE_MOVIE_WRK;

void TitleMovieInit(void);              /* 0x26af70 */

/* Non-zero once the title has been idle 15 s.  While the lock is held it
 * always answers 0 and keeps the BGM alive instead. */
int  CheckMoveTitleMovie(void);         /* 0x26af88 */

void MoveTitleMovieTimerRestart(void);  /* 0x26b0a8 */
void LockMoveTitleMovie(void);          /* 0x26b0b0 */
void UnlockMoveTitleMovie(void);        /* 0x26b0d8 */

#endif /* _OUTGAME_TITLE_MOVIE_H */
