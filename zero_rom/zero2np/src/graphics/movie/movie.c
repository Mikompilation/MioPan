// FILE: /home/zero_rom/zero2np/src/graphics/movie/movie.c
//
// The PSS movie player's game-side wrapper.  playpss.a does the decoding; this
// file owns everything around it -- the 2.7 MB of EE buffers and the two IOP
// ones the decoder needs, the stream file, the SPU core the audio is DMA'd
// through, the skip and pause keys, and the sprite the decoded frame is blitted
// with.
//
// The frame counter iMovieCnt is the other half of its job.  It is -1 while no
// movie is up and counts frames the movie was not paused for, and MovieCountGet()
// hands it out halved in PAL -- which is what lets the caption table
// (movie_title_dat.c) and the scene-effect script both be authored against NTSC
// frame numbers.
//
// PORT NOTE: playpss.a's decoder is a shim on the host (see
// system/playpss/playpss.c).  playPssMain() reports end-of-stream on its first
// call, so a movie plays for one frame and the caller tears it down.  Nothing
// in this file is stubbed for that -- the buffers, the stream and the frame
// counter all do their real work.
//
// Reconstructed from the Feb 6 2004 prototype (SLES_523.84).
// .text 0x21ea40..0x21f158; all nine ZERO2.MAP exports.

#include "movie.h"

#include "movie_title.h"                        // MovieTitleInit / Main / End
#include "../../common/mem_util.h"              // mem_utilGetMem / FreeMem
#include "../../common/utility2.h"              // PRINT_ASSERT
#include "../../common/variable.h"              // pad, key_now
#include "../../sdk/sifdev.h"                   // sceSif*IopHeap / QueryFree
#include "../../system/eeiop/ee_iop_q.h"        // ReqQuerySPUTransCore*
#include "../../system/eeiop/snd.h"             // SndGetGroupVolume
#include "../../system/os/system.h"             // GetPALMode / SetVBlankWaitNum
#include "../../system/pad/pad.h"               // paddat
#include "../../system/playpss/playpss.h"       // playPss* / MyStr*
#include "../graph2d/g2d_draw.h"                // SPRT_DAT / DISP_SPRT
#include "../graph3d/gra3d.h"                   // gra3dUseScratchpad
#include "../scene/scene.h"                     // GetFileNoFromSceneNo
#include "../scene/scene_dat.h"                 // scene_data_cmn

#include <stdio.h>                              // printf

/* The decoder wants this much heap free before it will start.  A movie is the
 * largest single claim the game makes. */
#define MOVIE_NEED_HEAP_SIZE    0x2b1400

/* GS local-memory block the decoded frame is loaded into, and the sprite that
 * blits it back out. */
#define MOVIE_VRAM_ADRS         0x2bc0
#define MOVIE_SCREEN_W          0x280
#define MOVIE_SCREEN_H          0x1c0

/* sdata 3f3348 -- -1 in the ROM's image, not 0: PlayMovie() and EndMovie() both
 * gate on it being non-negative, so a zeroed copy would let them run before
 * InitMovie() ever did. */
static int iMovieCnt = -1;

/* bss 4bb280 */
static playPssRsrcs rsrcs;

/* bss 4bb2c0 -- one 2048-byte sector of silence.  Its IOP-side twin is what the
 * decoder DMAs while the audio ring is starved, so it is never written. */
static u_char temp_zero_bffer[2048];

/* sbss 3f4e70 */
static int movie_audio_flg;
/* sbss 3f4e74 */
static int bPause;

/* --------------------------------------------------------------------------
 *  iopalloc  (0x21eb18)
 *
 *  Claim IOP memory or stop dead.  Both buffers it is used for are small and
 *  the IOP has 2 MB, so a failure here means the IOP heap is corrupt -- there
 *  is nothing to recover to, and the ROM hangs rather than pretending.
 * ------------------------------------------------------------------------ */
int iopalloc(unsigned int size, char *desc)                             /* 47 */
{
    void *ret;

    ret = sceSifAllocIopHeap(size);                                     /* 50 */
    if (ret == NULL)                                                    /* 51 */
    {
        printf("Cannot allocate %d bytes of IOP memory for %s\n", size, desc);
        for (;;)                                                        /* 52 */
        {
        }
    }

    printf("Allocated %7d bytes of IOP memory for %s\n", size, desc);   /* 55 */

    return (int)ret;                                                    /* 56 */
}

/* --------------------------------------------------------------------------
 *  InitMovie  (0x21eba8)
 *
 *  Claim everything the decoder needs and open the stream.
 *
 *  `vol_percentage` is the per-scene volume out of scene_data_cmn[]; it scales
 *  the BGM group volume rather than replacing it, so the option screen's music
 *  slider still applies to a cutscene.
 *
 *  With `audio_flg` 0 the four audio pointers are nulled instead of allocated
 *  and no SPU core is claimed -- so a silent movie costs 48 KB less EE heap and
 *  nothing at all on the IOP.  Only those four: the sizes, the volume and the
 *  SPU core keep whatever the last movie with audio left in them, because
 *  `rsrcs` is a static.  EndMovie() frees on the pointers, so that is safe.
 * ------------------------------------------------------------------------ */
void InitMovie(int no, int vol_percentage, int audio_flg)               /* 63 */
{
    int iMaxFreeSize;

    movie_audio_flg = audio_flg;                                        /* 65 */
    iMovieCnt       = -1;                                               /* 66 */

    iMaxFreeSize = mem_utilQueryMaxFreeSize();                          /* 68 */

    /* The ROM really does assert here rather than warn -- SetAssertPreMessage
     * plus PrintAssertReal, so PRINT_ASSERT and not PRINT_ERROR.  It is a
     * recoverable path: iMovieCnt stays -1, so PlayMovie() reports finished
     * and EndMovie() no-ops. */
    if (iMaxFreeSize < MOVIE_NEED_HEAP_SIZE)                            /* 71 */
    {
        PRINT_ASSERT("Movie Memory Is Not Vacant Remain[0x%x]", iMaxFreeSize); /* 72 */
        return;
    }

    printf("mem_utilQueryMaxFreeSize() = 0x%x\n", mem_utilQueryMaxFreeSize());     /* 76 */
    printf("mem_utilQueryTotalFreeSize() = 0x%x\n", mem_utilQueryTotalFreeSize()); /* 77 */

    rsrcs.mpegBuff       = (u_char *)mem_utilGetMem(0x13d800);          /* 79 */
    rsrcs.mpegBuffSize   = 0x13d800;                                    /* 80 */

    rsrcs.rgb32          = (sceIpuRGB32 *)mem_utilGetMem(0x118000);     /* 83 */

    rsrcs.path3tag       = (u_int *)mem_utilGetMem(0x1a500);            /* 85 */

    rsrcs.read_buf       = (char *)mem_utilGetMem(0x4000);              /* 87 */

    rsrcs.demuxBuff      = mem_utilGetMem(0x30000);                     /* 89 */
    rsrcs.demuxBuffSize  = 0x30000;                                     /* 90 */

    if (movie_audio_flg != 0)                                           /* 92 */
    {
        rsrcs.audioBuff     = (u_char *)mem_utilGetMem(0xc000);         /* 93 */
        rsrcs.audioBuffSize = 0xc000;                                   /* 94 */
        rsrcs.iopBuff       = iopalloc(0x6000, "audio buffer");         /* 95 */
        rsrcs.iopBuffSize   = 0x6000;                                   /* 96 */

        rsrcs.zeroBuff      = temp_zero_bffer;                          /* 100 */

        rsrcs.iopZeroBuff   = iopalloc(0x800, "zero buffer");           /* 102 */

        rsrcs.audio_vol_percent =                                       /* 106 */
            vol_percentage * SndGetGroupVolume(SND_GROUP_BGM) / 256;

        ReqQuerySPUTransCoreGet(&rsrcs.auto_dma_core);                  /* 108 */
    }
    else
    {
        rsrcs.audioBuff   = NULL;                                       /* 110 */
        rsrcs.iopBuff     = 0;                                          /* 111 */
        rsrcs.zeroBuff    = NULL;                                       /* 112 */
        rsrcs.iopZeroBuff = 0;                                          /* 113 */
    }

    /* The streamer is asked for the file until it takes it -- a busy IOP is
     * the only reason it refuses, and there is nothing else to do here. */
    while (MyStrStart(no, 10, 8) == 0)                                  /* 120 */
    {
    }

    printf("sceSifQueryMaxFreeMemSize() = 0x%x\n", sceSifQueryMaxFreeMemSize());     /* 124 */
    printf("sceSifQueryTotalFreeMemSize() = 0x%x\n", sceSifQueryTotalFreeMemSize()); /* 125 */

    if (GetPALMode() != 0)                                              /* 127 */
    {
        playPssSetNtsc2Pal(1, MOVIE_VRAM_ADRS);                         /* 128 */
    }
    playPssStart(MyStrRead, &rsrcs);                                    /* 130 */

    iMovieCnt = 0;                                                      /* 132 */
}

/* --------------------------------------------------------------------------
 *  MovieCancel  (0x21edf8)
 *
 *  What title_movie.c calls to drop the attract movie.  The same pair START and
 *  CROSS run inline in PlayMovie().
 * ------------------------------------------------------------------------ */
void MovieCancel(void)                                                  /* 138 */
{
    printf("===================== Movie Skip ==================\n");    /* 139 */
    playPssStop();                                                      /* 140 */
}

/* --------------------------------------------------------------------------
 *  PlayMovie  (0x21ee20)
 *
 *  One frame.  Returns the decoder's own answer -- 0 while it is running,
 *  non-zero once it is finished.
 *
 *  Two extra things happen here that are worth naming.  SetVBlankWaitNum(1)
 *  drops the game to one field per frame for the duration: the decoder needs
 *  every field it can get and there is no 3D to draw.  And gra3dUseScratchpad(0)
 *  hands the scratchpad back, because the IPU DMA wants it.
 * ------------------------------------------------------------------------ */
int PlayMovie(void)                                                     /* 145 */
{
    int       ret = 1;
    DISP_SPRT ds;

    if (iMovieCnt < 0)                                                  /* 148 */
    {
        return ret;
    }

    SetVBlankWaitNum(1);                                                /* 152 */

    /* START or CROSS skips; SELECT toggles the pause. */
    if ((pad[0].one & 0x800U) != 0 || *paddat[0] == 1)                  /* 161 */
    {
        printf("===================== Movie Skip ==================\n"); /* 162 */
        playPssStop();                                                  /* 163 */
    }
    else if (*key_now[0xd] == 1)                                        /* 168 */
    {
        if (playPssIsPause() != 0)                                      /* 169 */
        {
            playPssRestart();                                           /* 170 */
        }
        else
        {
            playPssPause();                                             /* 172 */
        }
    }

    bPause = playPssIsPause();                                          /* 176 */
    if (bPause == 0)                                                    /* 177 */
    {
        iMovieCnt++;                                                    /* 178 */
    }

    gra3dUseScratchpad(0);                                              /* 183 */

    ret = playPssMain(&rsrcs, MOVIE_VRAM_ADRS);                         /* 185 */

    if (playPssAlreadySendImage() != 0)                                 /* 186 */
    {
        /* Whole-screen blit of the block the decoder just loaded.  The three
         * GS registers below are what make it work and are not in SPRT_DAT:
         * TEX0 points the sampler at MOVIE_VRAM_ADRS as a 640-wide PSMCT32
         * page, TEX1 selects unfiltered point sampling (0x161), and TEST
         * 0x30003 turns the alpha test off so the decoder's opaque output is
         * not punched through. */
        SPRT_DAT sd = { 0, 0, 0, MOVIE_SCREEN_W, MOVIE_SCREEN_H,
                        0, 0, 0xe0, 0x80, 0, 1 };                       /* 187 */

        CopySprDToSpr(&ds, &sd);                                        /* 190 */
        ds.tex0 = 0x200578026802abc0ULL;                                /* 191 */
        ds.tex1 = 0x161;                                                /* 193 */
        ds.test = 0x30003;                                              /* 194 */
        DispSprD(&ds);                                                  /* 201 */
    }

    return ret;                                                         /* 204 */
}

/* --------------------------------------------------------------------------
 *  EndMovie  (0x21ef90)
 *
 *  Give everything back and put the frame rate and the scratchpad back where
 *  they were.  The three IOP-side blocks are freed individually because a
 *  silent movie never claimed them.
 * ------------------------------------------------------------------------ */
void EndMovie(void)                                                     /* 207 */
{
    if (iMovieCnt < 0)                                                  /* 208 */
    {
        return;
    }

    SetVBlankWaitNum(2);                                                /* 212 */
    playPssEnd();                                                       /* 213 */
    playPssSetNtsc2Pal(0, MOVIE_VRAM_ADRS);                             /* 214 */
    MyStrStop();                                                        /* 215 */
    gra3dUseScratchpad(1);                                              /* 216 */

    mem_utilFreeMem(rsrcs.mpegBuff);                                    /* 218 */
    mem_utilFreeMem(rsrcs.rgb32);                                       /* 219 */
    mem_utilFreeMem(rsrcs.demuxBuff);                                   /* 220 */
    mem_utilFreeMem(rsrcs.path3tag);                                    /* 221 */
    mem_utilFreeMem(rsrcs.read_buf);                                    /* 222 */

    if (movie_audio_flg != 0)                                           /* 224 */
    {
        ReqQuerySPUTransCoreRelease(rsrcs.auto_dma_core);               /* 225 */

        if (rsrcs.audioBuff != NULL)                                    /* 227 */
        {
            mem_utilFreeMem(rsrcs.audioBuff);                           /* 228 */
        }
        if (rsrcs.iopBuff != 0)                                         /* 229 */
        {
            sceSifFreeIopHeap((void *)(uintptr_t)rsrcs.iopBuff);        /* 230 */
        }
        /* Lines 231..234 hold no code. */
        if (rsrcs.iopZeroBuff != 0)                                     /* 235 */
        {
            sceSifFreeIopHeap((void *)(uintptr_t)rsrcs.iopZeroBuff);    /* 236 */
        }
    }

    iMovieCnt = -1;                                                     /* 238 */
}

/* --------------------------------------------------------------------------
 *  InitMovieWithTitle  (0x21f068)
 *
 *  The scene number is both the movie's file (three files per scene: the PSS,
 *  its ADPCM and its effect script) and the index into the caption table.
 * ------------------------------------------------------------------------ */
void InitMovieWithTitle(int scene_no, int audio_flg)                    /* 243 */
{
    InitMovie(GetFileNoFromSceneNo(scene_no),                           /* 244 */
              scene_data_cmn[scene_no].vol, audio_flg);

    MovieTitleInit(scene_no);                                           /* 246 */
}

/* --------------------------------------------------------------------------
 *  PlayMovieWithTitle  (0x21f0b8)
 * ------------------------------------------------------------------------ */
int PlayMovieWithTitle(void)                                            /* 251 */
{
    int ret;

    ret = PlayMovie();                                                  /* 254 */

    MovieTitleMain(GetPALMode() ? iMovieCnt >> 1 : iMovieCnt);          /* 257 */

    return ret;                                                         /* 259 */
}

/* --------------------------------------------------------------------------
 *  EndMovieWithTitle  (0x21f108)
 * ------------------------------------------------------------------------ */
void EndMovieWithTitle(void)                                            /* 263 */
{
    MovieTitleEnd();                                                    /* 264 */
    EndMovie();                                                         /* 265 */
}

/* --------------------------------------------------------------------------
 *  MovieCountGet  (0x21f130)
 *
 *  The counter halved in PAL.  A right shift, not a divide -- so the -1 an
 *  idle player reports stays -1 rather than rounding towards zero.
 * ------------------------------------------------------------------------ */
int MovieCountGet(void)                                                 /* 270 */
{
    return GetPALMode() ? iMovieCnt >> 1 : iMovieCnt;                   /* 272 */
}
