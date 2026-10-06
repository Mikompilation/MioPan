// FILE: /home/zero_rom/zero2np/src/ingame/clear/prg/ending.c
//
// The three ending phases: play a movie, unlock it in the gallery, and hand on
// to the result screen.
//
// The whole file is four GPhase callback sets over the same three calls --
// InitMovieWithTitle / PlayMovieWithTitle / EndMovieWithTitle.  There is no
// work block, no static data and nothing that runs between frames; the movie
// player owns all the state.
//
// The chain, and which movie each phase plays:
//
//   ev_macro.c  SendIngameEndingNormal(1)   -> GID_ENDING_NORMAL1  scene 51 (S1020)
//                                           -> GID_ENDING_NORMAL2  scene 53 (S1040)
//                                           -> GID_GAMERESULT_TOP
//   ev_macro.c  SendIngameEndingHard(1)     -> GID_ENDING_HARD     scene 52 (S1030)
//                                           -> GID_GAMERESULT_TOP
//
// so the normal ending is two movies with a phase change between them and the
// hard ending is one.  The scene number goes through
// GetFileNoFromSceneNo() (n * 3 + S0010_PSS), which is why the three files are
// S1020 / S1030 / S1040 rather than anything ending-shaped -- and why the hard
// ending's movie sits between the two halves of the normal one in file order.
//
// GID_ENDING_MOVIE is the parent phase and all four of its callbacks are
// genuinely empty (8 bytes each, `jr ra` plus a delay slot).  It exists only to
// own the three children in the phase tree.
//
// Reconstructed from the Feb 6 2004 prototype (SLES_523.84), ending.o.
// All 13 ZERO2.MAP exports; the object has no statics.
//
// Trailing /* NNN */ comments are the original source line numbers, measured
// from the $LM stabs.

#include "clear_flg.h"                              /* ClearFlgEnding*Exe     */

#include "../../ingame.h"                           /* SendIngameEnding*      */
#include "../../../graphics/movie/movie.h"          /* *MovieWithTitle        */
#include "../../../main/gphase.h"                   /* SetNextGPhase          */
#include "../../../main/phasefunc.h"                /* GPHASE_ENUM            */

/* The scene numbers, which are indices into scene_data_cmn[] rather than CD
 * file ids.  Names are the port's -- the ROM writes the literals. */
#define ENDING_SCENE_NORMAL1        51      /* S1020 */
#define ENDING_SCENE_HARD           52      /* S1030 */
#define ENDING_SCENE_NORMAL2        53      /* S1040 */

/* ==========================================================================
 *  GID_ENDING_MOVIE -- the parent phase
 *
 *  All four bodies are empty in the ROM.  Unlike GID_CLEARMENU or
 *  GID_SAVEPOINT_MAIN, this parent owns no background, no fade and no BGM:
 *  each child's movie fills the screen on its own, so there is nothing for the
 *  parent to draw under or over it.
 * ======================================================================== */

void init_Ending_Movie(void)                                            /* 46 */
{
}

GPHASE_ENUM pre_Ending_Movie(GPHASE_ENUM dummy)                         /* 48 */
{
    (void)dummy;

    return GPHASE_CONTINUE;                                             /* 49 */
}

GPHASE_ENUM after_Ending_Movie(GPHASE_ENUM result)                      /* 52 */
{
    (void)result;

    return GPHASE_CONTINUE;                                             /* 53 */
}

void end_Ending_Movie(void)                                             /* 56 */
{
}

/* ==========================================================================
 *  GID_ENDING_NORMAL1 -- the normal ending, first half
 * ======================================================================== */

/* The gallery unlock happens here, on the way *in*, not when the movie
 * finishes -- so an ending skipped with START still counts as watched.
 * ClearFlgEndingNormalExe() raises ending_movie_flg bit 0. */
void init_Ending_Normal1(void)                                          /* 62 */
{
    ClearFlgEndingNormalExe();                                          /* 64 */

    InitMovieWithTitle(ENDING_SCENE_NORMAL1, 1);                        /* 67 */
}

GPHASE_ENUM one_Ending_Normal1(GPHASE_ENUM dummy)                       /* 70 */
{
    (void)dummy;

    if (PlayMovieWithTitle() != 0) {                                    /* 73 */
        SetNextGPhase(GID_ENDING_NORMAL2);                              /* 74 */
    }

    return GPHASE_CONTINUE;                                             /* 78 */
}

/* SendIngameEndingNormal(0) clears the request flag ev_macro.c raised.  Without
 * it IngameGetNextPhase() keeps answering GID_ENDING_NORMAL1 and the ending
 * restarts for ever -- which is why the clear sits at the end of the *first*
 * half, where the request is finally spent, rather than in end_Ending_Normal2()
 * where nothing set it. */
void end_Ending_Normal1(void)                                           /* 81 */
{
    EndMovieWithTitle();                                                /* 82 */

    SendIngameEndingNormal(0);                                          /* 84 */
}

/* ==========================================================================
 *  GID_ENDING_NORMAL2 -- the normal ending, second half
 * ======================================================================== */

/* No clear-flag call: init_Ending_Normal1() already raised the bit, and the
 * two movies are one ending. */
void init_Ending_Normal2(void)                                          /* 87 */
{
    InitMovieWithTitle(ENDING_SCENE_NORMAL2, 1);                        /* 89 */
}

GPHASE_ENUM one_Ending_Normal2(GPHASE_ENUM dummy)                       /* 92 */
{
    (void)dummy;

    if (PlayMovieWithTitle() != 0) {                                    /* 95 */
        SetNextGPhase(GID_GAMERESULT_TOP);                              /* 96 */
    }

    return GPHASE_CONTINUE;                                             /* 100 */
}

void end_Ending_Normal2(void)                                           /* 103 */
{
    EndMovieWithTitle();                                                /* 104 */
}

/* ==========================================================================
 *  GID_ENDING_HARD -- the hard ending
 * ======================================================================== */

/* ClearFlgEndingHardExe() raises ending_movie_flg bit 1, the gallery's other
 * ending row. */
void init_Ending_Hard(void)                                             /* 110 */
{
    ClearFlgEndingHardExe();                                            /* 112 */

    InitMovieWithTitle(ENDING_SCENE_HARD, 1);                           /* 115 */
}

GPHASE_ENUM one_Ending_Hard(GPHASE_ENUM dummy)                          /* 118 */
{
    (void)dummy;

    if (PlayMovieWithTitle() != 0) {                                    /* 119 */
        SetNextGPhase(GID_GAMERESULT_TOP);                              /* 121 */
    }

    return GPHASE_CONTINUE;                                             /* 124 */
}

void end_Ending_Hard(void)                                              /* 127 */
{
    EndMovieWithTitle();                                                /* 128 */

    SendIngameEndingHard(0);                                            /* 130 */
}
