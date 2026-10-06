// FILE: /home/zero_rom/zero2np/src/outgame/title_movie.c
//
// The attract movie: the idle timer that decides when the title rolls into it,
// and the GID_TITLE_MOVIE_MODE phase that plays it.
//
// CheckMoveTitleMovie() does double duty -- while the lock is held (any title
// sub-screen), it does not count at all and instead keeps the appropriate BGM
// alive, which is why every child phase can call it without also owning the
// stream.
//
// title_movie.o has no static data beyond the fixed_array<> assert
// boilerplate; TITLE_MOVIE_WRK is its only state.
//
// Reconstructed from the Feb 6 2004 prototype (SLES_523.84).
// Trailing /* NNN */ are ROM source line numbers, measured from
// disassemble_function.

#include "title_movie.h"

#include "title.h"                          // Get/SetTitleStreamID
#include "../common/variable.h"             // pad[]
#include "../graphics/movie/movie.h"        // InitMovieWithTitle / Play / End / Cancel
#include "../ingame/ingame.h"               // CheckIngameMission
#include "../main/gphase.h"                 // SetNextGPhase / GID_TITLE_MODE
#include "../main/phasefunc.h"              // GPHASE_ENUM + phase-callback prototypes
#include "../system/eeiop/cddat.h"          // BGM000_TITLE_* / BGM005_MENU2_*
#include "../system/eeiop/stream_auto.h"    // StreamAutoPlay / FadeOut / IsPlaying
#include "../system/os/system.h"            // GetPALMode

#include <string.h>                         // memset

/* The title movie proper.  title_movie_flg alternates between this and the
 * demo list on every entry. */
#define TITLE_MOVIE_SCENE       0x43

/* 15 seconds of idle either way. */
#define MOVE_MOVIE_WAIT_NTSC    900
#define MOVE_MOVIE_WAIT_PAL     750

static TITLE_MOVIE_WRK title_movie_wrk;                                 /* sbss 3f4fe8 */

void TitleMovieInit(void)
{
    title_movie_wrk.move_movie_timer = 0;                               /* 77 */
    title_movie_wrk.title_movie_flg = '\0';                             /* 78 */
    title_movie_wrk.play_demo_cnt = '\0';                               /* 79 */
    title_movie_wrk.movie_lock_flg = '\0';                              /* 80 */
}

/* Non-zero once the title has been idle long enough to hand over.
 *
 * The locked branch never answers yes -- it is the BGM keep-alive instead.
 * Which stream depends on whether a mission is in progress, because the
 * mission menus sit under the same title mode. */
int CheckMoveTitleMovie(void)
{
    int res = 0;                                                        /* 98 */

    if (title_movie_wrk.movie_lock_flg == '\0')                         /* 102 */
    {
        title_movie_wrk.move_movie_timer++;                             /* 103 */

        if (GetPALMode() != 0)                                          /* 107 */
        {
            if (title_movie_wrk.move_movie_timer >= MOVE_MOVIE_WAIT_PAL)/* 110 */
            {
                res = 1;
            }
        }
        else
        {
            if (title_movie_wrk.move_movie_timer >= MOVE_MOVIE_WAIT_NTSC)/* 115 */
            {
                res = 1;                                                /* 116 */
            }
        }
    }
    else if (CheckIngameMission() != 0)                                 /* 135 */
    {
        if (StreamAutoIsPlaying(GetTitleStreamID()) == 0)               /* 136 */
        {
            SetTitleStreamID(StreamAutoPlay(BGM005_MENU2_STR, BGM005_MENU2_HXD,
                                            0x15, 0, 0, 0x3200, 0,
                                            (SND_3D_SET *)0));          /* 139 */
        }
    }
    else
    {
        if (StreamAutoIsPlaying(GetTitleStreamID()) == 0)               /* 144 */
        {
            SetTitleStreamID(StreamAutoPlay(BGM000_TITLE_STR, BGM000_TITLE_HXD,
                                            0xc, 0, 0, 0x3200, 0,
                                            (SND_3D_SET *)0));          /* 147 */
        }
    }

    return res;                                                         /* 152 */
}

void MoveTitleMovieTimerRestart(void)
{
    title_movie_wrk.move_movie_timer = 0;                               /* 161 */
}

void LockMoveTitleMovie(void)
{
    title_movie_wrk.movie_lock_flg = '\x01';                            /* 172 */
    MoveTitleMovieTimerRestart();                                       /* 175 */
}

void UnlockMoveTitleMovie(void)
{
    title_movie_wrk.movie_lock_flg = '\0';                              /* 186 */
}

// ──────────────────────────────────────────────────────────────────────
// GPhase per-phase callbacks, invoked via the main/gphase.c dispatch tables.

/* play_demo_movie[] is genuinely all zeroes in this build -- the ROM emits a
 * memset over it and then indexes it, so the demo branch always plays scene 0.
 * The rotation through play_demo_cnt is therefore dead as shipped, but it is
 * the ROM's own code. */
void init_Title_Movie_Mode(void)
{
    int play_demo_movie[3] = { 0, 0, 0 };                               /* 199 */

    StreamAutoFadeOut(GetTitleStreamID(), 0);                           /* 207 */

    if (title_movie_wrk.title_movie_flg == '\0')                        /* 211 */
    {
        InitMovieWithTitle(TITLE_MOVIE_SCENE, 1);                       /* 213 */
    }
    else if (title_movie_wrk.title_movie_flg == '\x01')                 /* 216 */
    {
        InitMovieWithTitle(play_demo_movie[title_movie_wrk.play_demo_cnt], 1); /* 218 */
        title_movie_wrk.play_demo_cnt = (char)((title_movie_wrk.play_demo_cnt + 1) % 3); /* 220 */
    }

    title_movie_wrk.title_movie_flg ^= 1;                               /* 225 */
}

GPHASE_ENUM one_Title_Movie_Mode(GPHASE_ENUM dummy)
{
    (void)dummy;

    /* Any button at all skips it -- the whole `one` word, not a mask. */
    if (pad[0].one != 0)                                                /* 229 */
    {
        MovieCancel();                                                  /* 230 */
    }

    if (PlayMovieWithTitle() != 0)                                      /* 233 */
    {
        SetNextGPhase(GID_TITLE_MODE);                                  /* 235 */
    }

    return GPHASE_CONTINUE;                                             /* 239 */
}

void end_Title_Movie_Mode(void)
{
    EndMovieWithTitle();                                                /* 243 */
    MoveTitleMovieTimerRestart();                                       /* 246 */
}
