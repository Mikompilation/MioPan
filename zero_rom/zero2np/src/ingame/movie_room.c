// FILE: /home/zero_rom/zero2np/src/ingame/movie_room.c
//
// movie_room.o -- CMovieRoom, the film player behind the movie room's two
// projectors.  Six methods and one static: everything between "the player put
// a reel in" and "a 160x128 MPEG is playing on the screen model".
//
// It is a thin object.  The decoding is playpss.a's; what lives here is the
// resource contract around it -- claim 2.7 MB of EE heap and a slice of IOP
// RAM, open the stream, start the projector's motor cue, hand the room's
// special-light rig over to the movie, and give all of it back.  The one piece
// of drawing it does is the screen itself: a four-corner quad, textured from
// the VRAM block the decoder writes into.
//
// The state machine is movie_projecter.o's, not this file's, and it is worth
// knowing while reading:  PreLoad() -> PlayFilm() until it answers 1 ->
// Work() every frame until *it* answers 1 -> Release().  Both flags are
// idempotent guards, so a re-entered PreLoad() or a doubled Release() is
// harmless -- which is what makes that loop safe to drive from a phase that
// may be entered twice.
//
// A NOTE ON THE /* NNN */ ANNOTATIONS.  Function opening lines are the $LM
// that precedes each PROC record in symbols.txt; statement lines come from the
// same table rather than Ghidra's `; Line` comments.  Two places carry two
// statements on one ROM source line -- Release()'s 37 and Draw()'s 198 -- and
// are annotated per statement rather than per line, the convention the rest of
// the tree uses.  `final` only emits a $LM when the line number *changes*, so
// a repeated number is one label, not two.
//
// LINES 114-155 HOLD NO CODE.  PlayFilm() opens at 113 and its first statement
// is 156; .text is complete without those 42 lines, so they are comment or a
// disabled older play path and nothing of them is recoverable.
//
// Reconstructed from the Feb 6 2004 prototype (SLES_523.84).

#include "movie_room.h"

#include <string.h>                             /* memset */

#include "../common/mem_util.h"                 /* mem_utilGetMem / FreeMem  */
#include "../common/utility2.h"                 /* PRINT_ASSERT/PRINT_WARNING*/
#include "../sdk/libvu0.h"                      /* sceVu0FVECTOR             */
#include "../sdk/sce_gs.h"                      /* sceGsTex0                 */
#include "../sdk/sifdev.h"                      /* sceSifQueryMaxFreeMemSize */

#include "../graphics/dmaVif1.h"                /* dmaVif1WaitPath3          */
#include "../graphics/draw_env.h"               /* DRAW_ENV_5 / SetDrawEnv   */
#include "../graphics/graphics.h"               /* MakePacket3D              */
#include "../graphics/graph2d/message.h"        /* SetString2                */
#include "../graphics/graph3d/gra3d.h"          /* gra3dUseScratchpad        */
#include "../graphics/graph3d/gra3dMisc.h"      /* gra3d*SpecialLight        */

#include "../system/eeiop/snd3d.h"              /* SND_3D_SET                */
#include "../system/eeiop/snd_buffer.h"         /* SndBufFadeStop            */
#include "../system/eeiop/snd_util.h"           /* snd_utilAutoRelease       */
#include "../system/eeiop/sndbank.h"            /* SndBank*                  */

#include "map/MapSp.h"                          /* MapSpMovi* / GetReelPos   */

/* The GS block the decoder writes its RGB32 output into, and that Draw() then
 * samples.  Same page ingame.c's pause capture uses (INGAME_CAPTURE_ADRS) --
 * the two are never live at once, since the pause screen stops the room.
 * The ROM spells the literal out at both use sites; the name is the port's. */
#define MOVIE_ROOM_VRAM_ADRS    0x3aa0

/* PreLoad()'s two admission tests.  The EE figure is the five buffers below
 * (0x2a3d00) plus headroom; it is the same 0x2b1400 savepoint.o refuses to
 * open a save screen under, which is not a coincidence -- both are "is there
 * a spare 2.7 MB on the ingame heap".  The IOP figure is the stream ring the
 * SPU2 side of playpss.a claims. */
#define MOVIE_EE_NEED_SIZE      0x2b1400
#define MOVIE_IOP_NEED_SIZE     0x79000

/* The projector-motor cue: one bank, played 3D at the reel's own position and
 * left looping for as long as the film runs.  The header file is the CD file
 * minus one, the convention the whole sound tree uses. */
#define MOVIE_ROOM_SND_FILE     0xd13
#define MOVIE_ROOM_SND_HEADER   0xd12

/* rdata 3c1790.  alpha / tex1 / clamp / test / zbuf, verbatim from the ROM's
 * .rodata image.  tex1 0x161 is MMAG=LINEAR, which is why Draw() insets its
 * UVs below rather than sampling the image edge to edge. */
static const DRAW_ENV_5 MovieDrawEnv =
{
    0x44,                       /* alpha: Cs*As + Cd*(1-As), fix unused       */
    0x161,                      /* tex1 : bilinear magnify                    */
    0x5,                        /* clamp: CLAMP on S and T                    */
    0x5000d,                    /* test : ATE, AFAIL=FB_ONLY, ZTE, ZTST=GEQ   */
    0xa000118,                  /* zbuf : ZBP 0x118, PSMZ24, ZMSK set         */
};

/* ------------------------------------------------------------------------ *
 *  Init -- called by the inline constructor in movie_room.h, nowhere else.
 * ------------------------------------------------------------------------ */
void CMovieRoom::Init(void)                                             /* 26 */
{
    mActFlg     = 0;                                                    /* 27 */
    mPreloadFlg = 0;                                                    /* 28 */
    mBankNo     = -1;                                                   /* 29 */
    mSndId      = CSND_BUF_PLAY_NO_ID;                                  /* 30 */
}

/* ------------------------------------------------------------------------ *
 *  Release -- give back the cue, the flags, the heap and the light rig.
 *
 *  Note the asymmetry with PreLoad(): the sound bank is torn down every time,
 *  the buffers only under mPreloadFlg.  A PreLoad() that failed its admission
 *  test has already claimed the bank and nothing else, and this is what
 *  reclaims it.
 * ------------------------------------------------------------------------ */
void CMovieRoom::Release(void)                                          /* 33 */
{
    SndBufFadeStop(mSndId, 1);                                          /* 34 */

    /* snd_utilAutoRelease() queues the bank behind the still-fading voice.
     * If it will not take it -- its own table is full -- the bank has to be
     * dropped now, which cuts the fade short.  The `!=` here is the ROM's
     * own: it expands std::operator!= (stl_relops.h 37) over the AUTO_BD_ERR
     * enum, which is what Ghidra reports as an inlined section. */
    if (snd_utilAutoRelease(mSndId, mBankNo) != AUTO_BD_ERR_OK) {       /* 37 */
        PRINT_WARNING("Cannot Get snd_utilAutoRelease()");              /* 37 */
        SndBankRelease(mBankNo);                                        /* 38 */
    }

    mSndId  = CSND_BUF_PLAY_NO_ID;                                      /* 40 */
    mBankNo = -1;                                                       /* 41 */

    mActFlg = 0;                                                        /* 43 */

    MapSpMoviSetFlg(0);                                                 /* 46 */

    gra3dUseScratchpad(1);                                              /* 48 */

    if (mPreloadFlg) {                                                  /* 49 */
        playPssEnd();                                                   /* 50 */
        MyStrStop();                                                    /* 51 */
        mem_utilFreeMem(ing_rsrcs.mpegBuff);                            /* 52 */
        mem_utilFreeMem(ing_rsrcs.rgb32);                               /* 53 */
        mem_utilFreeMem(ing_rsrcs.demuxBuff);                           /* 54 */
        mem_utilFreeMem(ing_rsrcs.read_buf);                            /* 55 */
        mem_utilFreeMem(ing_rsrcs.path3tag);                            /* 56 */
        mPreloadFlg = 0;                                                /* 57 */

        gra3dEndSpecialLight();                                         /* 60 */
    }
}

/* ------------------------------------------------------------------------ *
 *  PreLoad -- claim everything and open the stream.  1 = ready to try
 *  PlayFilm(), 0 = there was not enough memory and nothing was claimed.
 *
 *  `paVec` is the screen model's four corners in world space; it is only
 *  stored, and only Draw() reads it.  movie_projecter.o passes one of the two
 *  rows of its own ScreenPosition[2][4][4], so the pointer outlives the film.
 * ------------------------------------------------------------------------ */
int CMovieRoom::PreLoad(int iPssFileNo, const float (*paVec)[4])        /* 67 */
{
    if (mBankNo == -1) {                                                /* 68 */

        mBankNo = SndBankNew(MOVIE_ROOM_SND_FILE, MOVIE_ROOM_SND_HEADER, -1); /* 70 */
    }

    if (mPreloadFlg == 0) {                                             /* 73 */
        int iMaxFreeSize = mem_utilQueryMaxFreeSize();                  /* 74 */

        /* This assert sits on a *recoverable* path: movie_projecterWork()
         * discards PreLoad()'s answer and re-enters it every frame, so a
         * genuinely short heap reports once per frame rather than once.
         * Kept as the ROM has it. */
        if (iMaxFreeSize < MOVIE_EE_NEED_SIZE) {                        /* 76 */
            PRINT_ASSERT("Movie Memory Is Not Vacant Remain[0x%x]", iMaxFreeSize); /* 77 */
            return 0;                                                   /* 78 */
        }

        /* The IOP side has no assert banner -- it draws the numbers on the
         * screen instead, and queries the free size a second time to do it.
         * That second call is the ROM's own, not a CSE the port lost. */
        if (sceSifQueryMaxFreeMemSize() < MOVIE_IOP_NEED_SIZE) {        /* 85 */

            SetString2(0, 30.0f, 30.0f, 1, 0x80, 0, 0,
                       "Movie Memory Over Remain %d Need %d!!",
                       sceSifQueryMaxFreeMemSize(),
                       MOVIE_IOP_NEED_SIZE);                            /* 88 */

            return 0;                                                   /* 90 */
        }

        mpaVec = paVec;                                                 /* 93 */
        gra3dUseScratchpad(0);                                          /* 94 */
        ing_rsrcs.mpegBuff      = (u_char *)mem_utilGetMem(0x13d800);   /* 95 */
        ing_rsrcs.mpegBuffSize  = 0x13d800;                             /* 96 */
        ing_rsrcs.rgb32         = (sceIpuRGB32 *)mem_utilGetMem(0x118000);
                                                                        /* 97 */
        ing_rsrcs.read_buf      = (char *)mem_utilGetMem(0x4000);       /* 98 */
        ing_rsrcs.demuxBuff     = mem_utilGetMem(0x30000);              /* 99 */
        ing_rsrcs.path3tag      = (u_int *)mem_utilGetMem(0x1a500);     /* 100 */
        ing_rsrcs.demuxBuffSize = 0x30000;                              /* 101 */
        ing_rsrcs.audioBuff     = NULL;                                 /* 102 */

        /* MyStrStart() only fails while the previous stream is still shutting
         * down, so the ROM spins on it rather than propagating the failure.
         * It is a busy wait on a one-frame condition. */
        while (MyStrStart(iPssFileNo, 10, 24) == 0)                     /* 105 */
            ;
        playPssStartNoWait(MyStrRead, &ing_rsrcs);                      /* 106 */
        mPreloadFlg = 1;                                                /* 107 */
    }

    return 1;                                                           /* 110 */
}                                                                       /* 111 */

/* ------------------------------------------------------------------------ *
 *  PlayFilm -- start the film once the decoder has buffered and the cue's
 *  bank has loaded.  0 means "not yet, ask again next frame"; the caller
 *  loops on it, so neither test may be skipped.
 * ------------------------------------------------------------------------ */
int CMovieRoom::PlayFilm(void)                                          /* 113 */
{
    /* --- lines 114-155 hold no code; see the file banner ---------------- */

    if (mPreloadFlg == 0)               return 0;                       /* 156 */

    if (playPssIsReady() == 0)          return 0;                       /* 159 */

    if (SndBankIsReady(mBankNo) == 0)   return 0;                       /* 162 */

    /* MapSp's reel job turns the projector's spool from here on, and its
     * position is what the cue is placed at. */
    MapSpMoviSetFlg(1);                                                 /* 167 */
    mActFlg = 1;                                                        /* 168 */

    /* The stabs give this aggregate no name -- PlayFilm's $LBB13 block holds
     * no LSYM at all, unlike Draw()'s Info/Tex0 -- so `Snd3d` is the port's,
     * matching the rest of the tree.  The memset is the ROM's; it zeroes all
     * three pointers before two of them are written again below.  Sized off
     * the host struct, not the ROM's literal 0xc: SND_3D_SET is three
     * pointers and they are 8 bytes here. */
    SND_3D_SET Snd3d;

    memset(&Snd3d, 0, sizeof(SND_3D_SET));                              /* 170 */
    Snd3d.pos = (sceVu0FVECTOR *)MapSpGetReelPos();                     /* 171 */
    Snd3d.dir = NULL;                                                   /* 172 */
    Snd3d.vel = NULL;                                                   /* 173 */

    mSndId = SndBankPlay(mBankNo, 0, 1, 1, 0x3200, 0x1000, 0, &Snd3d);  /* 174 */

    gra3dStartSpecialLight();                                           /* 177 */

    return 1;                                                           /* 178 */
}                                                                       /* 180 */

/* ------------------------------------------------------------------------ *
 *  Draw -- the screen quad.
 *
 *  The TEX0 built here describes the whole 1024x512 PSMCT32 page at
 *  MOVIE_ROOM_VRAM_ADRS, and the UV rectangle then picks the decoded image
 *  out of its top-left corner.  Every one of the twelve fields is written,
 *  including the four CLUT ones that PSMCT32 cannot use -- reproduced as
 *  found, because Tex0 is an uninitialised stack local and the ROM is
 *  relying on writing all 64 bits rather than on the fields being dead.
 *
 *  The UVs are texels, not fractions: MakePacket3D() divides them by
 *  1 << TW / 1 << TH itself.  So 0.02 .. 0.02 + w*0.96 crops 4% off the
 *  right and bottom of a 160x128 frame, which is what hides the decoder's
 *  macroblock edge under a bilinear magnify.
 * ------------------------------------------------------------------------ */
void CMovieRoom::Draw(void)                                             /* 192 */
{
    PLAY_PSS_MPEG_INFO Info;
    sceGsTex0          Tex0;

    playPssGetMpegInfo(&Info);                                          /* 196 */

    Tex0.TBP0 = MOVIE_ROOM_VRAM_ADRS;                                   /* 198 */
    Tex0.TBW  = 10;                                                     /* 198 */
    Tex0.PSM  = 0;                                                      /* 199 */
    Tex0.TW   = 10;                                                     /* 200 */
    Tex0.TH   = 9;                                                      /* 201 */
    Tex0.TCC  = 0;                                                      /* 202 */
    Tex0.TFX  = 0;                                                      /* 203 */
    Tex0.CBP  = MOVIE_ROOM_VRAM_ADRS;                                   /* 204 */
    Tex0.CPSM = 0;                                                      /* 205 */
    Tex0.CSM  = 0;                                                      /* 206 */
    Tex0.CSA  = 0;                                                      /* 207 */
    Tex0.CLD  = 1;                                                      /* 208 */

    SetDrawEnv(0, &MovieDrawEnv);                                       /* 210 */

    /* The decoder pushes its macroblocks over PATH3; the quad below goes out
     * over PATH1/2.  Draining PATH3 first is what stops a half-written frame
     * being sampled. */
    dmaVif1WaitPath3();                                                 /* 212 */

    /* mpaVec is const here and MakePacket3D()'s first parameter is not; the
     * ROM's own signatures disagree the same way.  See movie_room.h. */
    MakePacket3D((float (*)[4])mpaVec, 4, 0xff, 0xff, 0xff, 0x50,
                 0.02f, 0.02f,
                 (float)Info.width * 0.96f, (float)Info.height * 0.96f,
                 Tex0, 100);                                            /* 220 */
}

/* ------------------------------------------------------------------------ *
 *  Work -- one frame.  1 means "the film is over (or was never running)",
 *  which is the caller's cue to Release().
 * ------------------------------------------------------------------------ */
int CMovieRoom::Work(void)                                              /* 225 */
{
    if (mActFlg) {                                                      /* 226 */

        gra3dUpdateSpecialLight();                                      /* 230 */

        MapSpMoviProc();                                                /* 233 */

        return (playPssMain(&ing_rsrcs, MOVIE_ROOM_VRAM_ADRS) != 0);    /* 235 */
    }

    return 1;                                                           /* 238 */
}                                                                       /* 240 */
