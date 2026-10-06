/* --------------------------------------------------------------------------
 *  ingame/movie_room.h
 *
 *  CMovieRoom -- the movie-room film player.  One instance, `movie_room`,
 *  which belongs to ingame.o (.data 3186c0) and is therefore defined in
 *  ingame/ingame.c rather than here.
 *
 *  WHERE THIS FILE LIVES.  symbols.txt carries exactly one `SOL movie_room.h`
 *  record, at 1ca9b8 inside ingame.o's __static_initialization_and_destruction_0,
 *  and ingame.o's enclosing `SO` directory is /home/zero_rom/zero2np/src/ingame/.
 *  So the header sits beside movie_room.c in src/ingame/, NOT in
 *  movie_room_menu/prg/ where an earlier pass put it -- movie_projecter.o calls
 *  the six methods out of line and never inlines anything from here, which is
 *  why it carries no SOL of its own.
 *
 *  That same record pins the constructor: $LM851 at 1ca9b8 is header line 12
 *  and the instruction it labels is the `jal CMovieRoom::Init`.  The whole
 *  expansion is that one call, so the ctor body really is `{ Init(); }`.
 *
 *  Reconstructed from the Feb 6 2004 prototype (SLES_523.84).
 * ------------------------------------------------------------------------ */
#ifndef _INGAME_MOVIE_ROOM_H
#define _INGAME_MOVIE_ROOM_H

#include <sys/types.h>                          /* u_char */

#include "../system/playpss/playpss.h"          /* playPssRsrcs */

/* types.txt gives CMovieRoom as 0x4c.  The two leading flags are declared
 * `unsigned int` bitfields there; u_char reproduces the same byte 0x00 and
 * keeps mSndId at 0x04, which `unsigned int` would not on the host.
 *
 * mpaVec is a *single* pointer to a four-component vector, not an array of
 * four pointers: it sits at 0x48 in a 0x4c struct, so only 4 bytes remain.
 * types.txt renders `float (*)[4]` as `float *[4]`, which reads like four
 * pointers and is not, and ZERO2.MAP's demangler prints the bound as the max
 * index -- `PreLoad(int, float const (*)[3])` is `const float (*)[4]`.
 * See [[gnu-v2-demangler-array-bound-is-max-index]].
 *
 * Constness of the member itself is not recoverable (types.txt never prints
 * const).  PreLoad's parameter is const in the mangled name and that is the
 * one observable fact, so the member follows it and CMovieRoom::Draw() casts
 * at the MakePacket3D() boundary, which takes a non-const `float (*)[4]`.
 *
 * Host offsets drift from 0x0c onward, and an offsetof harness confirms
 * exactly where: playPssRsrcs opens with a pointer, so it wants 8-byte
 * alignment here against the EE's 4, which puts ing_rsrcs at 0x10 rather than
 * 0x0c and shifts everything after it.  The three scalars in front of it --
 * the flag byte, mSndId and mBankNo -- are at the ROM's offsets, and they are
 * the only members anything touches other than by name.  The offset comments
 * below are the ROM's and are documentation. */
class CMovieRoom                    /* 0x4c on target */
{
private:
    /* 0x00:0 */ u_char        mActFlg     : 1; /* a film is playing         */
    /* 0x00:1 */ u_char        mPreloadFlg : 1; /* buffers claimed, stream up */
    /* 0x04 */ int             mSndId;          /* projector-motor voice     */
    /* 0x08 */ int             mBankNo;         /* its sound bank, -1 = none */
    /* 0x0c */ playPssRsrcs    ing_rsrcs;
    /* 0x48 */ const float   (*mpaVec)[4];      /* the screen's four corners */

public:
    CMovieRoom(void) { Init(); }                                        /* 12 */

    void Init(void);
    void Release(void);
    int  PreLoad(int iPssFileNo, const float (*paVec)[4]);
    int  PlayFilm(void);
    int  Work(void);
    void Draw(void);
};

/* Defined in ingame/ingame.c -- see the note at the top of this header. */
extern CMovieRoom movie_room;       /* data 3186c0 (owner: ingame.o) */

#endif /* _INGAME_MOVIE_ROOM_H */
