// FILE: /home/zero_rom/zero2np/src/ingame/movie_room_menu/prg/movie_projecter.c
//
// movie_projecter.o -- the two projectors standing in the movie room.
//
// Eleven exported functions, one static, four file-scope variables, and no
// work block of its own: everything the projectors need is either a room ID
// (which projector am I in front of), a reel number (what is loaded in it),
// or a three-value state.  The actual film player is CMovieRoom in
// ingame/movie_room.c; this file is the state machine that drives it.
//
// THE WHOLE MACHINE.  now_state is NONE / PRELOAD / PLAY.
//
//   movie_projecterPlay()  arms it -- NONE -> PRELOAD.
//   movie_projecterWork()  runs it, once a frame, from ingame.c and photo.c:
//        PRELOAD  re-enters movie_room.PreLoad() every frame (its answer is
//                 discarded) and calls PlayFilm() until that answers 1;
//                 then PRELOAD -> PLAY and *falls through* into the PLAY arm
//                 in the same frame.
//        PLAY     calls movie_room.Work() until it answers 1, then
//                 Release()s and goes back to NONE.
//   movie_projecterStop()  tears it down from anywhere -- Release(), NONE.
//
// TWO PROJECTORS, ONE INDEX.  movie_projecterGetIndex() is the only thing
// that decides which projector is being talked to, and it does so purely from
// the player's room ID: 0xbf is projector 0, 0x92 is projector 1, anything
// else is -1 ("the player is not in the movie room") and every public entry
// point turns that into a no-op.  Which is why there is no "current
// projector" variable anywhere in the file.
//
// FILM NUMBERS ARE CD FILE NUMBERS.  SetFilmNo[] holds a CD_FILE_DAT index
// straight out of cddat.h, -1 for an empty projector, and the PAL film is
// always the NTSC one plus one (MOVIE_ROOM_000_PSS / MOVIE_ROOM_000PAL_PSS),
// which is the whole of the GetPALMode() test in Work().
//
// A NOTE ON THE /* NNN */ ANNOTATIONS.  Function opening lines are the $LM
// that precedes each PROC record in symbols.txt; statement lines come from the
// same table rather than from Ghidra's `; Line` comments.  A repeated number
// separated by an instruction of another line is the -O2 scheduler
// interleaving the prologue with the first statement, not two statements --
// movie_projecterStop() is the clearest case (134/135/134/134/135).
//
// Statements whose only memory access goes through SetFilmNo[i] carry
// fixed_array.h's own 124/125 instead of a movie_projecter.c line, so they are
// annotated with the header line and their real line is unrecoverable.  That
// is nine of the file's statements.
//
// VERIFIED.  11/11 ZERO2.MAP .text exports and 12/12 functions.txt entries.
// .text is accounted for byte-for-byte: 0x4f8 of code plus twelve 4-byte
// alignment fills = the section's 0x528, ending exactly at 0x21f680, so there
// is no unlisted body.  ScreenPosition (128 bytes) and InitFilmNo (8) are
// byte-identical to the ROM, diffed out of the compiled .obj.
//
// The object has no other static data.  Its .rodata (0xcd) is fixed_array.h's
// assert literal, the "unsigned int*" type name, PRINT_ASSERT's banner format
// plus "movie_projecter.c" / "movie_projecterPlay" / "There Is no PlayFilm";
// its .sdata (0x48) is ten compiler `_$tmp_N` slots, _fixed_array_assert's own
// `str`, the "void*" / "char*" type names, and InitFilmNo.  The sizes look
// like there ought to be a table -- read them out of the ELF before assuming
// one.
//
// Reconstructed from the Feb 6 2004 prototype (SLES_523.84).

#include "movie_projecter.h"

#include "../../../common/utility2.h"           /* PRINT_ASSERT              */
#include "../../../graphics/graph3d/ctl/fixed_array.h"
#include "../../../sdk/libvu0.h"                /* sceVu0FVECTOR             */
#include "../../../system/eeiop/cddat.h"        /* MOVIE_ROOM_00x_PSS        */
#include "../../../system/os/system.h"          /* GetPALMode                */

#include "../../movie_room.h"                   /* CMovieRoom / movie_room   */
#include "../../plyr/player.h"                  /* GetPlyrRoomID             */

/* --------------------------------------------------------------------------
 *  The two projectors' screens, in world space: four corners each, in the
 *  order MakePacket3D() wants them.  Only CMovieRoom::Draw() reads these, and
 *  it keeps the pointer across the whole film, which is why the table is
 *  file-scope rather than a local.
 *
 *  Projector 0's screen lies in the YZ plane at x = 17255 and projector 1's
 *  in the XY plane at z = 10230; both are 1075 x 819 world units.
 *
 *  Every one of the eight distinct coordinates sits exactly one ulp below a
 *  round value (17255.0, -1409.5, 8898.75, ...), the EE-GCC literal
 *  truncation of a level editor's four-decimal export.  The literals here are
 *  the shortest decimals that round-trip to the ROM's words under the host's
 *  round-to-nearest, so the emitted .data is byte-identical -- writing the
 *  round values instead would each be one ulp high.
 *  See [[ee-gcc-truncates-float-literals]].
 * ------------------------------------------------------------------------ */
static sceVu0FVECTOR ScreenPosition[2][4] =                 /* data 336dc0 */
{
    {   /* projector 0 -- the screen on the east wall */
        { 17254.998f, -1409.4999f,  8898.749f, 1.0f },
        { 17254.998f, -1409.4999f,  9973.749f, 1.0f },
        { 17254.998f,  -590.49994f, 8898.749f, 1.0f },
        { 17254.998f,  -590.49994f, 9973.749f, 1.0f },
    },
    {   /* projector 1 -- the screen on the north wall */
        {  8123.7495f, -1409.4999f, 10229.999f, 1.0f },
        {  7048.7495f, -1409.4999f, 10229.999f, 1.0f },
        {  8123.7495f,  -590.49994f, 10229.999f, 1.0f },
        {  7048.7495f,  -590.49994f, 10229.999f, 1.0f },
    },
};

/* Which reel is loaded in each projector; -1 = empty.  This is the file's
 * only saved state -- movie_projecterSetSave() hands these eight bytes to the
 * card, so a projector keeps its reel across a save/load. */
static fixed_array<int, 2> SetFilmNo;                       /* sbss 3f4e78 */

/* What each projector starts the game loaded with.  Note the crossover:
 * projector 0 gets film 001 and projector 1 gets film 000. */
static int InitFilmNo[2] = { MOVIE_ROOM_001_PSS,
                             MOVIE_ROOM_000_PSS };          /* sdata 3f3398 */

/* The ROM's own enum, out of types.txt.  Nothing outside this file names it,
 * so it stays here rather than in the header. */
enum _MOVIE_PROJECTER_STATE
{
    MOVIE_PROJECTER_STATE_NONE    = 0,
    MOVIE_PROJECTER_STATE_PRELOAD = 1,
    MOVIE_PROJECTER_STATE_PLAY    = 2
};
typedef _MOVIE_PROJECTER_STATE MOVIE_PROJECTER_STATE;

static MOVIE_PROJECTER_STATE now_state;                     /* sbss 3f4e80 */


/* ------------------------------------------------------------------------ *
 *  Init -- called once per room load, from IngameWrkInit().
 * ------------------------------------------------------------------------ */
void movie_projecterInit(void)                                          /* 61 */
{
    for (int i = 0; i < 2; i++)                                         /* 62 */
    {
        SetFilmNo[i] = InitFilmNo[i];                              /* 124/125 */
    }                                                                   /* 64 */

    now_state = MOVIE_PROJECTER_STATE_NONE;                             /* 65 */
}

/* ------------------------------------------------------------------------ *
 *  Release -- DEAD CODE.  Exported, but a jal/j scan over the loadable
 *  segments finds no call site anywhere in the ROM; movie_projecterStop() is
 *  what everything actually calls.  Same pattern as ene_mot_ctrl.o's and
 *  fly_ctrl.o's exported-but-unused pairs.  Kept because ZERO2.MAP has it.
 * ------------------------------------------------------------------------ */
void movie_projecterRelease(void)                                       /* 69 */
{
    movie_projecterStop();                                              /* 70 */
}

/* ------------------------------------------------------------------------ *
 *  GetIndex -- which projector the player is standing at, or -1.
 *
 *  There is no state behind this: the two projectors occupy two rooms, so the
 *  player's room ID alone names one of them.  Every public entry point in the
 *  file starts here and gives up on -1, which is what makes the whole module
 *  inert outside the movie room.
 * ------------------------------------------------------------------------ */
static int movie_projecterGetIndex(void)                                /* 74 */
{
    int iRoomNo = GetPlyrRoomID();                                      /* 75 */

    if (iRoomNo == 0xbf) return 0;                                      /* 77 */

    if (iRoomNo == 0x92) return 1;                                      /* 79 */

    return -1;                                                          /* 82 */
}                                                                       /* 84 */

/* ------------------------------------------------------------------------ *
 *  SetSave -- entry 40 of save_game_data[].
 *
 *  The ROM takes the address through SetFilmNo[0], so the bounds check for
 *  index 0 against capacity 2 is inlined here too.
 * ------------------------------------------------------------------------ */
void movie_projecterSetSave(MC_SAVE_DATA *save)                         /* 87 */
{
    save->addr = (u_char *)&SetFilmNo[0];                          /* 124/125 */
    save->size = sizeof(SetFilmNo);                                     /* 89 */
}

/* ------------------------------------------------------------------------ *
 *  SetFilmNo -- put a reel in the projector the player is at.
 * ------------------------------------------------------------------------ */
void movie_projecterSetFilmNo(int iFilmNo)                              /* 93 */
{
    int iIndex = movie_projecterGetIndex();                             /* 94 */

    if (iIndex >= 0)                                                    /* 96 */
    {
        SetFilmNo[iIndex] = iFilmNo;                               /* 124/125 */
    }
}                                                                      /* 100 */

/* ------------------------------------------------------------------------ *
 *  GetFilmNo -- what is loaded in the projector the player is at, -1 for an
 *  empty one *and* for "not in the movie room".  movie_room_menu.o cannot
 *  tell those apart and does not need to.
 * ------------------------------------------------------------------------ */
int movie_projecterGetFilmNo(void)                                     /* 104 */
{
    int iIndex = movie_projecterGetIndex();                            /* 105 */

    if (iIndex < 0) return -1;                                         /* 108 */

    return SetFilmNo[iIndex];                                          /* 112 */
}                                                                      /* 113 */

/* ------------------------------------------------------------------------ *
 *  TakeFilm -- take the reel back out, and stop whatever it was playing.
 *  Returns the reel that was in there so the menu can put it back in the
 *  player's hands.
 * ------------------------------------------------------------------------ */
int movie_projecterTakeFilm(void)                                      /* 116 */
{
    int iIndex = movie_projecterGetIndex();                            /* 117 */
    int ret;

    if (iIndex < 0) return -1;                                         /* 120 */

    ret = SetFilmNo[iIndex];                                      /* 124/125 */
    SetFilmNo[iIndex] = -1;                                       /* 124/125 */

    movie_projecterStop();                                             /* 128 */

    return ret;                                                        /* 130 */
}                                                                      /* 131 */

/* ------------------------------------------------------------------------ *
 *  Stop -- tear the film down from anywhere.  Called on a door transition, on
 *  a battle starting, when the story mode ends, and by TakeFilm().
 *
 *  Unconditional: CMovieRoom::Release() is idempotent, so calling this with
 *  nothing playing costs nothing.
 * ------------------------------------------------------------------------ */
void movie_projecterStop(void)                                         /* 134 */
{
    movie_room.Release();                                              /* 135 */
    now_state = MOVIE_PROJECTER_STATE_NONE;                            /* 136 */
}

/* ------------------------------------------------------------------------ *
 *  IsReq -- is there a reel in the projector the player is standing at?
 *
 *  SetPlyrRoomID() polls this on every room change and starts the film when
 *  it answers 1, which is how walking up to a loaded projector plays it
 *  without any further input.
 * ------------------------------------------------------------------------ */
int movie_projecterIsReq(void)                                         /* 140 */
{
    int iIndex = movie_projecterGetIndex();                            /* 143 */

    if (iIndex >= 0)                                                   /* 144 */
    {
        if (SetFilmNo[iIndex] >= 0) return 1;                     /* 124/125 */
    }

    return 0;                                                          /* 148 */
}                                                                      /* 150 */

/* ------------------------------------------------------------------------ *
 *  Play -- arm the projector.  Always answers 1; the 0 is the "the player is
 *  not in the movie room" path, which is a programming error rather than a
 *  runtime condition -- hence the assert.
 *
 *  Its `iIndex` has no stab, unlike the identical local in the five functions
 *  around it: here the value never has to survive a call, so the allocator
 *  left it in v0 and dbxout skipped the pseudo.  The line numbers are what
 *  place it -- 154 for the call and 156 for the test, the same one-blank-line
 *  gap the rest of the file has.
 * ------------------------------------------------------------------------ */
int movie_projecterPlay(void)                                          /* 153 */
{
    int iIndex = movie_projecterGetIndex();                            /* 154 */

    if (iIndex < 0)                                                    /* 156 */
    {
        /* Recoverable in the ROM -- a plain SetAssertPreMessage /
         * PrintAssertReal pair with no break; it only fires on a genuine
         * caller bug. */
        PRINT_ASSERT("There Is no PlayFilm");                          /* 157 */
        return 0;                                                      /* 158 */
    }

    if (now_state != MOVIE_PROJECTER_STATE_NONE) return 1;             /* 162 */
    now_state = MOVIE_PROJECTER_STATE_PRELOAD;                         /* 163 */

    return 1;                                                          /* 164 */
}                                                                      /* 165 */

/* ------------------------------------------------------------------------ *
 *  Work -- one frame of the film.  Driven every frame by ingame.c (three call
 *  sites) and by photo.c's finder phase.
 *
 *  Returns 1 while the projector is idle or busy and 0 only for a now_state
 *  the switch does not know, which nothing can produce.  Every caller in the
 *  build discards it.
 *
 *  THE PRELOAD ARM FALLS THROUGH INTO THE PLAY ARM.  A failed PlayFilm()
 *  breaks out, but a successful one sets PLAY and drops straight into
 *  movie_room.Work() in the same frame -- the object emits no branch there,
 *  and the two `case` bodies are back to back.  So the frame a film starts is
 *  also its first playing frame, not a wasted one.
 *
 *  PreLoad()'s answer is discarded, deliberately: it is re-entered every frame
 *  until PlayFilm() succeeds, and both of its flags are idempotent guards.
 * ------------------------------------------------------------------------ */
int movie_projecterWork(void)                                          /* 169 */
{
    int ret = 1;                                                       /* 170 */

    switch (now_state)                                                 /* 172 */
    {
    case MOVIE_PROJECTER_STATE_PRELOAD:
        {
            /* iIndex is block-scoped in the ROM, not a function local beside
             * `ret`: the stabs put it inside $LBB33, a block that opens at
             * 21f564 -- the head of this case, not the head of the function.
             * The braces are what C++ requires for an initialised local under
             * a `case` that a later `case` jumps past, and $LBE33 landing at
             * the switch exit (21f608) rather than at 21f5e8 is stmt.c's
             * squeeze_notes() shifting the block-end note past the next case
             * label, which is exactly what it exists to do. */
            int iIndex = movie_projecterGetIndex();                    /* 174 */

            /* PAL gets the film one CD file up -- MOVIE_ROOM_000_PSS ->
             * MOVIE_ROOM_000PAL_PSS.  The subscript is written out on both
             * arms in the ROM (two inlined bounds checks, not one). */
            movie_room.PreLoad(GetPALMode() ? SetFilmNo[iIndex] + 1
                                            : SetFilmNo[iIndex],
                               ScreenPosition[iIndex]);                /* 176 */

            if (movie_room.PlayFilm() == 0) break;                     /* 177 */
            now_state = MOVIE_PROJECTER_STATE_PLAY;                    /* 178 */
        }
        /* fall through into PLAY -- see the banner above */

    case MOVIE_PROJECTER_STATE_PLAY:                                   /* 181 */

        if (movie_room.Work() == 0) break;                             /* 185 */
        movie_room.Release();                                          /* 186 */

        now_state = MOVIE_PROJECTER_STATE_NONE;                        /* 189 */
        break;

    default:
        ret = 0;                                                       /* 192 */
        break;
    }

    return ret;                                                        /* 195 */
}

/* ------------------------------------------------------------------------ *
 *  Draw -- the screen quad, only while a film is actually running.
 * ------------------------------------------------------------------------ */
void movie_projecterDraw(void)                                         /* 199 */
{
    if (now_state == MOVIE_PROJECTER_STATE_PLAY)                       /* 200 */
    {
        movie_room.Draw();                                             /* 201 */
    }
}
