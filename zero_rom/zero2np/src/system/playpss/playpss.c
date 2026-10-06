// FILE: /home/akira_koide/zero2np/src/system/playpss/playpss.c
//
// The PSS movie player's driver.  It owns no codec of its own: libmpeg does the
// demux and the IPU decode, ldimage.c builds the GS transfer, audiodec.c runs
// the audio, and this file is the machine that turns them into a movie.
//
// Four rings and a state flag word.  muxBuff holds one 16 KB block of the raw
// program stream that my_strfile.c read; sceMpegDemuxPss() takes bytes out of
// it and calls back with each elementary-stream payload, which videoCallback()
// copies into demuxBuff and audioCallback() hands to audiodec.c.  demuxBuff is
// a three-pointer ring -- Past is where the demuxer writes, Present..Future the
// window currently being DMA'd into the IPU -- and nodataCallback() advances it
// whenever the IPU runs dry.
//
// The whole thing is driven by refilling in the gaps.  Timer 0 is programmed to
// count H-BLANKs, and both playPssSetPacket() and backgroundCallback() spend a
// measured number of scanlines calling fillBuff() before giving the frame back:
// bgDuration is that budget, and it scales with the picture area.
//
// There are two decode paths and which one runs is decided once, by
// playPssSetNtsc2Pal():
//
//   NTSC  playPssMain() -> playPssSetPacket() decodes, transfers and refills
//         inline, once per game frame, and reports 1 at end of stream.
//
//   PAL   the console runs at 50 Hz and the film at 29.97, so a vblank handler
//         (vblankHandler) counts 11988/20000 of a frame per field and posts a
//         semaphore at the film's rate; a thread (videoDecMain) parks on it and
//         does the same work.  playPssSetPacket() then does nothing but report
//         whether that thread has finished.
//
// PORT: nothing here is stubbed.  The one lie in the chain is one level down,
// in sdk/libmpeg.cpp -- the IPU is hardware the host does not have, so
// sceMpegIsEnd() reports an empty stream and a cutscene ends on its first
// frame.  ldimage.c is a stub for the same reason.  Both say so at the site.
//
// Reconstructed from the Feb 6 2004 prototype (SLES_523.84).
// playpss.a(playpss.o), .text 0x278eb0..0x279c84; all 16 exports plus the
// 7 statics.  Line annotations measured from the object's own $LM records; a
// handful of them pile up at basic-block heads under -O2, so where two land at
// one address only the first is used.

#include "playpss.h"

#include "audiodec.h"
#include "ldimage.h"

#include "../../sdk/eekernel.h"     /* threads, semaphores, INTC             */
#include "../../sdk/eeregs.h"       /* REG_RCNT0_*, REG_DMAC_4_IPU_TO_*      */
#include "../../sdk/libgraph.h"     /* sceGsSyncPath                          */
#include "../../sdk/libsdr.h"       /* sceSdRemoteInit                        */

/* One block of program stream per file read. */
#define PSS_MUX_BUFF_SIZE       0x4000

/* Quadwords of RGB32 the IPU may write per sceMpegGetPicture() -- 0x460 is
 * 16 macroblocks' worth, the widest picture the player supports. */
#define PSS_PICTURE_QWC         0x460

/* The IPU is fed in 4 KB slices and never less than 4 KB is queued for it. */
#define PSS_IPU_DMA_SIZE        0x1000

/* The screen the centred form of playPssSetPacket() centres in. */
#define PSS_SCREEN_W            640
#define PSS_SCREEN_H            448

/* Scanlines of refill per pixel of picture (0.000151f as EE GCC rounded it --
 * the literal is one ulp under the decimal, see the note in the skill file).
 * 640x448 comes out at 43 lines; the 500-line budget in playPssSetPacket()
 * scales the same way. */
#define PSS_REFILL_PER_PIXEL    0.000150999986f

/* INTC cause 2 -- vblank start. */
#define PSS_INTC_VBLANK         2

/* The PAL divider: 11988/20000 of a film frame per 50 Hz field is 29.97 Hz. */
#define PSS_PAL_STEP            11988
#define PSS_PAL_PERIOD          20000

/* Stop states.  0 is running; playPssStop() asks for 1; whichever decode path
 * is live answers 2 once it has drained. */
#define PSS_STOP_NONE           0
#define PSS_STOP_REQ            1
#define PSS_STOP_DONE           2

/* The EE's uncached-accelerated window.  videoCallback() writes the demux ring
 * through it so the IPU's DMA sees the bytes without a cache flush.
 *
 * PORT: identity here.  Masking a host pointer to 28 bits and relocating it to
 * 0x20000000 does not produce another view of the same memory, it produces a
 * wild pointer -- and there is no cache between this store and the reader
 * anyway. */
#define PSS_UNCACHED(p)         (p)

/* The flag word.  sStopPhase is a short and the five bits share the halfword
 * after it, which is what makes the four clears in playPssStartNoWait() one
 * `andi`. */
typedef struct                      /* 0x4 */
{
    /* 0x0   */ short    sStopPhase;
    /* 0x2:0 */ u_short  bStarted       : 1;   /* audio has been started     */
    /* 0x2:1 */ u_short  bPauseFlg      : 1;
    /* 0x2:2 */ u_short  bNtsc2Pal      : 1;   /* the threaded decode path   */
    /* 0x2:3 */ u_short  bSendImage     : 1;   /* that thread published one  */
    /* 0x2:4 */ u_short  bErrorCallback : 1;
} PLAY_PSS_FLAGS;

/* sdata 3f49e0 */ int iPalVBlankCounter = 0;
/* sdata 3f49f0 */ int iVTCounter        = 0;
/* sdata 3f49f4 */ int iMovieDecSema     = 0;
/* sdata 3f49f8 */ int hid_vblank        = 0;

/* bss 4c2000 */ char videoDecStack[2048];

/* sbss 3f50b8 */ static PLAY_PSS_FLAGS  Flags;
/* sbss 3f50bc */ static int             videoDecTh;
/* bss  4c2800 */ static AudioDec        audioDec;
/* sbss 3f50c0 */ static playPssReadFunc fileRead;
/* sbss 3f50c4 */ static int             bgDuration;
/* sbss 3f50c8 */ static char           *muxBuff;
/* sbss 3f50cc */ static int             muxBuffFullness;
/* sbss 3f50d0 */ static char           *demuxBuff;
/* sbss 3f50d4 */ static int             demuxBuffSize;
/* sbss 3f50d8 */ static char           *demuxBuffPast;
/* sbss 3f50dc */ static char           *demuxBuffPresent;
/* sbss 3f50e0 */ static char           *demuxBuffFuture;
/* sbss 3f50e4 */ static sceIpuRGB32    *rsrcs_rgb32;
/* sbss 3f50e8 */ static u_int          *rsrcs_path3tag;
/* sbss 3f50ec */ static int             rsrcs_vram_adrs;
/* bss  4c2860 */ static sceMpeg         playpss_mp;
/* sbss 3f50f0 */ static int             audio_play_flg;

static int videoCallback(sceMpeg *mp, sceMpegCbDataStr *cbstr, void *data);
static int audioCallback(sceMpeg *mp, sceMpegCbDataStr *cbstr, void *data);
static int nodataCallback(sceMpeg *mp, sceMpegCbData *cbstr, void *data);
static int backgroundCallback(sceMpeg *mp, sceMpegCbData *cbstr, void *data);
static int errorCallback(sceMpeg *mp, sceMpegCbData *cbstr, void *data);
static int fillBuff(sceMpeg *mp, int blocking);

/* --------------------------------------------------------------------------
 *  playPssInit  (0x278eb0)
 *
 *  Once, at boot.  Clearing bNtsc2Pal here and not in playPssStart() is what
 *  makes playPssSetNtsc2Pal() a per-movie decision that survives until the
 *  next one -- InitMovie() sets it before every PAL movie and EndMovie() clears
 *  it after.
 * ------------------------------------------------------------------------ */
void playPssInit(void)                                                  /* 113 */
{
    Flags.bNtsc2Pal = 0;                                                /* 115 */
    sceMpegInit();                                                      /* 116 */
    sceSdRemoteInit();                                                  /* 117 */
}

/* --------------------------------------------------------------------------
 *  videoDecMain  (0x278ed8)
 *
 *  The PAL decode thread.  One picture per semaphore post, which vblankHandler
 *  makes at the film's rate rather than the console's, and then whatever is
 *  left of the frame goes into refilling.
 *
 *  The extra posts it drains at the top are the ones it could not keep up with;
 *  each prints, because falling behind means the film is about to stutter.
 * ------------------------------------------------------------------------ */
void videoDecMain(void *pvd)                                            /* 120 */
{
    while (1)                                                           /* 122 */
    {
        if (Flags.bErrorCallback != 0 ||                                /* 123 */
            sceMpegIsEnd(&playpss_mp) != 0 ||
            Flags.sStopPhase == PSS_STOP_REQ)
        {
            Flags.sStopPhase = PSS_STOP_DONE;                           /* 124 */
            return;                                                     /* 125 */
        }

        if (PollSema(iMovieDecSema) == -1)                              /* 129 */
        {
            WaitSema(iMovieDecSema);                                    /* 131 */
        }

        while (PollSema(iMovieDecSema) != -1)                           /* 136 */
        {
            printf("dame\n");                                           /* 137 */
        }                                                               /* 138 */

        REG_RCNT0_MODE  = 0x83;                                         /* 140 */
        REG_RCNT0_COUNT = 0;                                            /* 141 */

        if (Flags.bPauseFlg == 0 &&                                     /* 144 */
            sceMpegGetPicture(&playpss_mp, rsrcs_rgb32,                 /* 149 */
                              PSS_PICTURE_QWC) < 0)
        {
            printf("sceMpegGetPicture failed\n");                       /* 150 */
            Flags.sStopPhase    = PSS_STOP_DONE;                        /* 151 */
            Flags.bErrorCallback = 1;                                   /* 152 */
        }

        /* The transfer chain only has to be built once: the picture size and
         * the destination do not change inside a movie, only the pixels the
         * chain points at. */
        if (playpss_mp.frameCount == 0)                                 /* 161 */
        {
            setLoadImageTags(rsrcs_path3tag, rsrcs_rgb32, 0, 0,         /* 164 */
                             playpss_mp.width, playpss_mp.height,
                             rsrcs_vram_adrs);
            bgDuration = (int)((float)playpss_mp.width                  /* 166 */
                               * (float)playpss_mp.height
                               * PSS_REFILL_PER_PIXEL);
        }

        loadImage(rsrcs_path3tag);                                      /* 174 */
        Flags.bSendImage = 1;                                           /* 175 */

        if (audio_play_flg != 0)                                        /* 181 */
        {
            audioDecSendToIOP(&audioDec);                               /* 182 */

            if (Flags.bStarted == 0 && audioDecIsPreset(&audioDec) != 0)/* 184 */
            {
                audioDecStart(&audioDec);                               /* 185 */
                Flags.bStarted = 1;                                     /* 186 */
            }
        }

        if (Flags.sStopPhase != PSS_STOP_NONE)                          /* 190 */
        {
            break;
        }

        while (fillBuff(&playpss_mp, 1) != 0)                           /* 194 */
        {
        }
    }

    Flags.sStopPhase = PSS_STOP_DONE;
}                                                                       /* 199 */

/* --------------------------------------------------------------------------
 *  playPssStartNoWait  (0x2790c8)
 *
 *  Bring the decoder up and post the first read, without waiting for it.
 *
 *  The audio half is optional and the four-way test is how it is declared: a
 *  movie InitMovie() gave no audio buffers to plays silently, and the PCM
 *  stream callback is simply never registered.
 * ------------------------------------------------------------------------ */
void playPssStartNoWait(playPssReadFunc cbFileRead, playPssRsrcs *rsrcs) /* 204 */
{
    muxBuff = rsrcs->read_buf;                                          /* 205 */

    Flags.bStarted       = 0;                                           /* 206 */
    Flags.bPauseFlg      = 0;                                           /* 207 */
    Flags.bSendImage     = 0;                                           /* 208 */
    Flags.bErrorCallback = 0;                                           /* 209 */

    sceMpegCreate(&playpss_mp, rsrcs->mpegBuff, rsrcs->mpegBuffSize);   /* 215 */

    sceMpegAddStrCallback(&playpss_mp, sceMpegStrM2V, 0,                /* 217 */
                          (sceMpegCallback)videoCallback, NULL);

    sceMpegAddCallback(&playpss_mp, sceMpegCbNodata,                    /* 220 */
                       (sceMpegCallback)nodataCallback, NULL);
    sceMpegAddCallback(&playpss_mp, sceMpegCbBackground,                /* 221 */
                       (sceMpegCallback)backgroundCallback, NULL);
    sceMpegAddCallback(&playpss_mp, sceMpegCbError,                     /* 222 */
                       (sceMpegCallback)errorCallback, NULL);

    if (rsrcs->audioBuff != NULL && rsrcs->iopBuff != 0 &&              /* 226 */
        rsrcs->zeroBuff != NULL && rsrcs->iopZeroBuff != 0)
    {
        audio_play_flg = 1;                                             /* 231 */

        audioDecCreate(&audioDec, rsrcs->audioBuff, rsrcs->audioBuffSize,
                       rsrcs->iopBuff, rsrcs->iopBuffSize,              /* 232 */
                       rsrcs->zeroBuff, rsrcs->iopZeroBuff, 0x800,
                       rsrcs->audio_vol_percent, rsrcs->auto_dma_core);

        sceMpegAddStrCallback(&playpss_mp, sceMpegStrPCM, 0,            /* 237 */
                              (sceMpegCallback)audioCallback, NULL);
    }                                                                   /* 241 */
    else
    {
        audio_play_flg = 0;                                             /* 242 */
    }

    muxBuffFullness  = 0;                                               /* 251 */
    Flags.sStopPhase = PSS_STOP_NONE;                                   /* 253 */

    demuxBuff        = (char *)rsrcs->demuxBuff;                        /* 259 */
    demuxBuffSize    = rsrcs->demuxBuffSize;                            /* 260 */
    demuxBuffPast    = demuxBuff + 16;                                  /* 261 */
    demuxBuffPresent = demuxBuff;                                       /* 262 */
    demuxBuffFuture  = demuxBuffPast;                                   /* 263 */

    fileRead = cbFileRead;                                              /* 269 */

    fillBuff(&playpss_mp, 0);                                           /* 271 */
}

/* --------------------------------------------------------------------------
 *  vblankHandler  (0x279258)
 *
 *  The PAL frame-rate divider, on INTC cause 2.  11988/20000 of a film frame
 *  per 50 Hz field is 29.97 Hz, so the decode thread is released at the film's
 *  own rate rather than the console's -- which is the whole of the NTSC->PAL
 *  conversion this player does.
 *
 *  PORT: nothing raises INTC on the host (see MioPan_AddIntcHandler), so this
 *  body never runs.  It would matter the day a real decoder ran the PAL path:
 *  videoDecMain() would park on the semaphore for ever.
 * ------------------------------------------------------------------------ */
int vblankHandler(int val)                                              /* 299 */
{
    iPalVBlankCounter += PSS_PAL_STEP;                                  /* 313 */

    if (iPalVBlankCounter >= PSS_PAL_PERIOD)                            /* 314 */
    {
        iPalVBlankCounter -= PSS_PAL_PERIOD;                            /* 315 */
        iSignalSema(iMovieDecSema);                                     /* 316 */
    }                                                                   /* 317 */

    iVTCounter++;                                                       /* 319 */

    SYNC(0);                                                            /* 321 */
    EI();

    return 0;                                                           /* 322 */
}

/* --------------------------------------------------------------------------
 *  playPssSetNtsc2Pal  (0x2792b0)
 * ------------------------------------------------------------------------ */
void playPssSetNtsc2Pal(int lbNtsc2Pal, int iGSImageAdrs)               /* 326 */
{
    /* PORT DEVIATION: the threaded path is never armed here, so a PAL disc
     * decodes inline exactly as an NTSC one does.
     *
     * The ROM's reason for having two paths is a clock, not a codec.  A PAL
     * console runs at 50 Hz and the film at 29.97, so it cannot decode one
     * picture per frame; vblankHandler() counts 11988/20000 of a film frame
     * per field and releases videoDecMain() at the film's own rate.  That is
     * the whole of the "NTSC2PAL conversion" -- no scaling, no field doubling,
     * just a divider.
     *
     * Neither half of it survives here.  Nothing raises INTC on the host (see
     * MioPan_AddIntcHandler), so vblankHandler() never runs and videoDecMain()
     * parks on iMovieDecSema for ever -- playPssSetPacket() then returns 0 on
     * every frame without decoding, transferring, or pumping the audio, and a
     * movie produces no picture and no sound while the phase waits for it.
     * That is what a PAL disc did before this line.
     *
     * The divider is also redundant now: MioPanMpegVideoGetPicture() paces
     * itself off the stream's own frame_rate_code, so calling it once per game
     * frame yields the film's rate whatever the console's is (measured at
     * 29.97 fps over 811 pictures).  The inline path is therefore both the
     * working one and the correct one.
     *
     * The parameter is kept in the signature -- it is the ROM's -- and the
     * VRAM address is still recorded, so the PAL machinery stays compilable
     * and correct for the day INTC vblank is wired up. */
    (void)lbNtsc2Pal;

    Flags.bNtsc2Pal = 0;                                                /* 328 */
    rsrcs_vram_adrs = iGSImageAdrs;                                     /* 329 */
}

/* --------------------------------------------------------------------------
 *  playPssAlreadySendImage  (0x2792d8)
 *
 *  In NTSC the decode ran inline a moment ago, so the answer is always yes.
 *  In PAL it is whether the decode thread has published a frame since the last
 *  ask -- and note that nothing ever clears bSendImage, so it is really "has
 *  the thread produced its first frame yet".
 * ------------------------------------------------------------------------ */
int playPssAlreadySendImage(void)                                       /* 332 */
{
    if (Flags.bNtsc2Pal == 0)                                           /* 335 */
    {
        return 1;
    }

    return (Flags.bSendImage != 0);                                     /* 338 */
}                                                                       /* 344 */

/* --------------------------------------------------------------------------
 *  playPssStart  (0x2792f8)
 *
 *  playPssStartNoWait() plus the wait: demux until the stream stops giving,
 *  which is enough of a head start that the first frame is ready.  In PAL it
 *  then builds the machine that replaces the inline decode -- semaphore,
 *  thread, vblank handler -- and zeroes the divider.
 * ------------------------------------------------------------------------ */
void playPssStart(playPssReadFunc cbFileRead, playPssRsrcs *rsrcs)      /* 346 */
{
    ThreadParam      th_param;
    /* PORT: the ROM has this local too -- CreateSema() is handed a stack
     * SemaParam -- but the debug info carries no stab for it, so the name is
     * the port's.  Note the ROM fills only two of its six fields; currentCount,
     * numWaitThreads, attr and option go to the kernel uninitialised. */
    struct SemaParam sema_param;

    playPssStartNoWait(cbFileRead, rsrcs);                              /* 348 */

    while (fillBuff(&playpss_mp, 1) != 0)                               /* 349 */
    {
    }

    if (Flags.bNtsc2Pal != 0)                                           /* 351 */
    {
        sema_param.maxCount  = 4;                                       /* 356 */
        sema_param.initCount = 0;                                       /* 357 */
        iMovieDecSema = CreateSema(&sema_param);                        /* 358 */

        rsrcs_rgb32    = rsrcs->rgb32;                                  /* 361 */
        rsrcs_path3tag = rsrcs->path3tag;                               /* 362 */

        th_param.entry        = videoDecMain;                           /* 363 */
        th_param.stack        = videoDecStack;                          /* 364 */
        th_param.stackSize    = sizeof(videoDecStack);                  /* 365 */
        th_param.initPriority = 3;                                      /* 366 */
        /* PORT: the ROM hands the thread the EE's $gp so its globals resolve.
         * There is no $gp here and MioPan_CreateThread() does not read the
         * field, so it goes over as NULL. */
        th_param.gpReg        = NULL;                                   /* 367 */
        th_param.option       = 0;                                      /* 368 */

        videoDecTh = CreateThread(&th_param);                           /* 369 */
        StartThread(videoDecTh, 0);                                     /* 370 */

        hid_vblank = AddIntcHandler(PSS_INTC_VBLANK, vblankHandler, 0); /* 372 */
        EnableIntc(PSS_INTC_VBLANK);                                    /* 373 */

        iPalVBlankCounter = 0;                                          /* 377 */
        iVTCounter        = 0;                                          /* 378 */
    }
}                                                                       /* 386 */

/* --------------------------------------------------------------------------
 *  playPssIsReady  (0x2793e8)
 *
 *  "Enough has buffered to start."  One non-blocking demux pass: if it takes
 *  nothing, either the stream is not delivering or the decoder has all it can
 *  hold -- and the second is what movie_projecter.c is waiting for.
 * ------------------------------------------------------------------------ */
int playPssIsReady(void)                                                /* 389 */
{
    return (fillBuff(&playpss_mp, 0) == 0);                             /* 391 */
}

/* --------------------------------------------------------------------------
 *  playPssMain  (0x279410)
 * ------------------------------------------------------------------------ */
int playPssMain(playPssRsrcs *rsrcs, int vram_adrs)                     /* 403 */
{
    return playPssSetPacket(rsrcs, vram_adrs, NULL);                    /* 405 */
}

/* --------------------------------------------------------------------------
 *  playPssGetMpegInfo  (0x279428)
 * ------------------------------------------------------------------------ */
void playPssGetMpegInfo(PLAY_PSS_MPEG_INFO *pInfo)                      /* 408 */
{
    pInfo->width  = playpss_mp.width;                                   /* 410 */
    pInfo->height = playpss_mp.height;                                  /* 411 */
}

/* --------------------------------------------------------------------------
 *  playPssSetPacket  (0x279448)
 *
 *  One frame of the NTSC path: decode a picture, get it to the GS, keep the
 *  audio moving, then spend what is left of the budget refilling.
 *
 *  Two forms, chosen by pTagEndAdrs.  NULL means "send it yourself", and the
 *  image goes where setLoadImageTags() was last told; non-NULL means "here is
 *  the chain, splice it into your own list", and then the image is centred in
 *  a 640x448 screen.  CMovieRoom is the caller that wants the second.
 *
 *  In PAL none of that happens here -- videoDecMain() is doing it -- so the
 *  function is only a report on whether that thread has finished.
 * ------------------------------------------------------------------------ */
int playPssSetPacket(playPssRsrcs *rsrcs, int vram_adrs, int *pTagEndAdrs)
{                                                                       /* 415 */
    int origin_x;
    int origin_y;

    if (Flags.sStopPhase == PSS_STOP_DONE)                              /* 418 */
    {
        return 2;
    }

    if (Flags.bNtsc2Pal != 0)                                           /* 422 */
    {
        return 0;
    }

    if (Flags.bErrorCallback != 0 || sceMpegIsEnd(&playpss_mp) != 0)    /* 425 */
    {
        return 1;
    }

    REG_RCNT0_MODE  = 0x83;                                             /* 429 */
    REG_RCNT0_COUNT = 0;                                                /* 430 */

    if (Flags.bPauseFlg == 0 &&                                         /* 434 */
        sceMpegGetPicture(&playpss_mp, rsrcs->rgb32,                    /* 439 */
                          PSS_PICTURE_QWC) < 0)
    {
        printf("sceMpegGetPicture failed\n");                           /* 440 */
        Flags.bErrorCallback = 1;                                       /* 441 */
    }

    if (pTagEndAdrs != NULL)                                            /* 450 */
    {
        origin_x = (PSS_SCREEN_W - playpss_mp.width) / 2;               /* 451 */
        origin_y = (PSS_SCREEN_H - playpss_mp.height) / 2;              /* 452 */

        if (playpss_mp.frameCount == 0)                                 /* 453 */
        {
            bgDuration = (int)((float)playpss_mp.width                  /* 454 */
                               * (float)playpss_mp.height
                               * PSS_REFILL_PER_PIXEL);
        }

        *pTagEndAdrs = (int)setLoadImageTags(rsrcs->path3tag,           /* 456 */
                                             rsrcs->rgb32,
                                             origin_x, origin_y,
                                             playpss_mp.width,
                                             playpss_mp.height,
                                             vram_adrs);
    }                                                                   /* 459 */
    else
    {
        if (playpss_mp.frameCount == 0)                                 /* 466 */
        {
            setLoadImageTags(rsrcs->path3tag, rsrcs->rgb32, 0, 0,       /* 469 */
                             playpss_mp.width, playpss_mp.height,
                             vram_adrs);
            bgDuration = (int)((float)playpss_mp.width                  /* 471 */
                               * (float)playpss_mp.height
                               * PSS_REFILL_PER_PIXEL);
        }

        /* PATH3 is shared with the game's own GS traffic, so the transfer has
         * to wait for whatever was in flight. */
        sceGsSyncPath(0, 0);                                            /* 478 */
        loadImage(rsrcs->path3tag);                                     /* 479 */
    }

    if (audio_play_flg != 0)                                            /* 487 */
    {
        audioDecSendToIOP(&audioDec);                                   /* 488 */

        if (Flags.bStarted == 0 && audioDecIsPreset(&audioDec) != 0)    /* 490 */
        {
            audioDecStart(&audioDec);                                   /* 491 */
            Flags.bStarted = 1;                                         /* 492 */
        }
    }

    if (Flags.sStopPhase != PSS_STOP_NONE)                              /* 496 */
    {
        Flags.sStopPhase = PSS_STOP_DONE;                               /* 497 */
    }

    /* Spend the rest of the budget refilling.  500 scanlines for a full 640x448
     * picture, scaled by area -- about two fields, which is what a movie frame
     * has to spare when the game frame it is inside does nothing else.
     *
     * PORT: the break is not the ROM's.  REG_RCNT0_COUNT is a real host clock
     * scaled to the EE's H-BLANK rate, so the budget is 31.8 ms of wall time
     * for a 640x448 picture -- a whole frame.  On the console spinning it out
     * cost nothing and more stream could arrive from the IOP mid-loop; here
     * this is the game's only thread, and a non-blocking fillBuff() that took
     * nothing has nothing more to give this frame.  Without the break the
     * player burns the frame it was supposed to be leaving spare, which is
     * what starves the audio pump one level up. */
    while (REG_RCNT0_COUNT <                                            /* 499 */
           (u_int)(playpss_mp.width * playpss_mp.height * 500           /* 500 */
                   / (PSS_SCREEN_W * PSS_SCREEN_H)))
    {
        if (fillBuff(&playpss_mp, 0) == 0)                              /* 502 */
        {
            break;
        }
    }                                                                   /* 503 */

    return 0;                                                           /* 504 */
}                                                                       /* 507 */

/* --------------------------------------------------------------------------
 *  playPssStop  (0x279700)
 *
 *  Ask for a stop.  The audio is silenced here because it is the one part that
 *  would keep playing on its own -- the SPU2's auto-DMA does not need the EE --
 *  and the picture stops when whichever decode path is live notices the phase.
 * ------------------------------------------------------------------------ */
void playPssStop(void)                                                  /* 510 */
{
    if (Flags.sStopPhase == PSS_STOP_NONE)                              /* 512 */
    {
        if (audio_play_flg != 0)                                        /* 513 */
        {
            audioDecPause(&audioDec);                                   /* 514 */
        }

        Flags.sStopPhase = PSS_STOP_REQ;                                /* 516 */
    }
}                                                                       /* 517 */

/* --------------------------------------------------------------------------
 *  playPssIsPause  (0x279740)
 * ------------------------------------------------------------------------ */
int playPssIsPause(void)                                                /* 521 */
{
    return Flags.bPauseFlg;                                             /* 522 */
}

/* --------------------------------------------------------------------------
 *  playPssPause  (0x279750)
 *
 *  Only once the audio has started.  Before that there is nothing to hold in
 *  step with, and pausing would strand the preload.
 * ------------------------------------------------------------------------ */
void playPssPause(void)                                                 /* 525 */
{
    if (Flags.bStarted != 0)                                            /* 527 */
    {
        Flags.bPauseFlg = 1;                                            /* 528 */

        if (audio_play_flg != 0)                                        /* 529 */
        {
            audioDecPause(&audioDec);                                   /* 530 */
        }
    }
}                                                                       /* 532 */

/* --------------------------------------------------------------------------
 *  playPssRestart  (0x2797a0)
 *
 *  Note the asymmetry with playPssPause(): the flag is cleared whether the
 *  audio ever started or not.
 * ------------------------------------------------------------------------ */
void playPssRestart(void)                                               /* 534 */
{
    Flags.bPauseFlg = 0;                                                /* 536 */

    if (audio_play_flg != 0)                                            /* 537 */
    {
        audioDecResume(&audioDec);                                      /* 538 */
    }
}                                                                       /* 539 */

/* --------------------------------------------------------------------------
 *  playPssEnd  (0x2797e8)
 *
 *  Tear the PAL machine down first -- the handler before the thread, so no
 *  post can land on a semaphore that is about to go -- then the audio, then
 *  the decoder.  The buffers belong to InitMovie() and are freed there.
 * ------------------------------------------------------------------------ */
void playPssEnd(void)                                                   /* 542 */
{
    if (Flags.bNtsc2Pal != 0)                                           /* 544 */
    {
        RemoveIntcHandler(PSS_INTC_VBLANK, hid_vblank);                 /* 547 */

        TerminateThread(videoDecTh);                                    /* 549 */
        DeleteThread(videoDecTh);                                       /* 550 */

        DeleteSema(iMovieDecSema);                                      /* 552 */
    }

    if (audio_play_flg != 0)                                            /* 555 */
    {
        audioDecReset(&audioDec);                                       /* 556 */
    }

    sceMpegReset(&playpss_mp);                                          /* 563 */
    sceMpegDelete(&playpss_mp);                                         /* 569 */
}

/* --------------------------------------------------------------------------
 *  videoCallback  (0x279860)  static
 *
 *  One video PES payload into the demux ring.  Returns 0 to refuse it, which
 *  makes libmpeg hand the same payload back on the next demux pass -- so the
 *  ring filling up is a stall, not a loss.
 *
 *  The free span is Present - Past, wrapped; equal pointers are ambiguous, and
 *  the Present == Future test is what separates "empty" from "full".
 * ------------------------------------------------------------------------ */
static int videoCallback(sceMpeg *mp, sceMpegCbDataStr *cbstr, void *data)
{                                                                       /* 574 */
    int availSpace;
    int spill;

    availSpace = (int)(demuxBuffPresent - demuxBuffPast);               /* 578 */

    if (availSpace < 0)                                                 /* 579 */
    {
        availSpace += demuxBuffSize;
    }

    if (availSpace == 0 && demuxBuffPresent == demuxBuffFuture)         /* 581 */
    {
        availSpace = demuxBuffSize;
    }

    if ((int)cbstr->len > availSpace)                                   /* 584 */
    {
        return 0;
    }

    spill = (int)(demuxBuffPast + cbstr->len - (demuxBuff + demuxBuffSize));
                                                                        /* 586 */
    if (spill <= 0)                                                     /* 588 */
    {
        memcpy(PSS_UNCACHED(demuxBuffPast), cbstr->data, cbstr->len);   /* 589 */
        demuxBuffPast += cbstr->len;                                    /* 590 */
    }
    else                                                                /* 592 */
    {
        memcpy(PSS_UNCACHED(demuxBuffPast), cbstr->data,                /* 593 */
               cbstr->len - spill);
        memcpy(PSS_UNCACHED(demuxBuff),                                 /* 596 */
               cbstr->data + (cbstr->len - spill), spill);
        demuxBuffPast = demuxBuff + spill;                              /* 597 */
    }

    return 1;                                                           /* 600 */
}                                                                       /* 601 */

/* --------------------------------------------------------------------------
 *  audioCallback  (0x279970)  static
 *
 *  One audio PES payload into audiodec.c's ring, through the two-span producer
 *  API so a wrap costs nothing extra.  The four bytes taken off the front are
 *  the PSS sub-stream header, which is not sample data.
 * ------------------------------------------------------------------------ */
static int audioCallback(sceMpeg *mp, sceMpegCbDataStr *cbstr, void *data)
{                                                                       /* 604 */
    u_char *pd0;
    u_char *pd1;
    int     d0;
    int     d1;
    int     spill;

    audioDecBeginPut(&audioDec, &pd0, &d0, &pd1, &d1);                  /* 609 */

    cbstr->len  -= 4;                                                   /* 611 */
    cbstr->data += 4;                                                   /* 612 */

    spill = (int)cbstr->len - d0;                                       /* 614 */

    if ((int)cbstr->len > d0 + d1)                                      /* 616 */
    {
        return 0;
    }

    if (spill <= 0)                                                     /* 618 */
    {
        memcpy(pd0, cbstr->data, cbstr->len);                           /* 619 */
    }
    else                                                                /* 620 */
    {
        memcpy(pd0, cbstr->data, cbstr->len - spill);                   /* 621 */
        memcpy(pd1, cbstr->data + (cbstr->len - spill), spill);         /* 623 */
    }

    audioDecEndPut(&audioDec, cbstr->len);                              /* 625 */

    return 1;                                                           /* 626 */
}                                                                       /* 627 */

/* --------------------------------------------------------------------------
 *  nodataCallback  (0x279a40)  static
 *
 *  The IPU has run dry: hand it the next slice of the demux ring.
 *
 *  Present..Future is the slice currently being read, so retiring it is one
 *  assignment -- Present catches up to Future -- and then the new Future is
 *  placed by whatever the demuxer has written since, rounded down to a
 *  quadword, capped at 4 KB and clipped at the end of the ring.  It will not
 *  hand over less than 4 KB: below that it demuxes more first, and only a
 *  stream that has run out gets a short final slice.
 * ------------------------------------------------------------------------ */
static int nodataCallback(sceMpeg *mp, sceMpegCbData *cbstr, void *data)
{                                                                       /* 630 */
    int dmaSize;
    int availData;

    demuxBuffPresent = demuxBuffFuture;                                 /* 634 */
    availData        = (int)(demuxBuffPast - demuxBuffFuture);          /* 636 */

    while (1)                                                           /* 639 */
    {
        if (availData < 0)                                              /* 655 */
        {
            availData += demuxBuffSize;
        }

        if (availData >= PSS_IPU_DMA_SIZE)                              /* 656 */
        {
            break;
        }

        if (fillBuff(mp, 1) == 0)                                       /* 640 */
        {
            /* The stream is finished; round the tail up so the last partial
             * quadword still goes. */
            availData += 15;                                            /* 650 */
            break;
        }

        availData = (int)(demuxBuffPast - demuxBuffFuture);             /* 654 */
    }

    dmaSize = availData & ~15;                                          /* 658 */

    if (dmaSize > PSS_IPU_DMA_SIZE)                                     /* 659 */
    {
        dmaSize = PSS_IPU_DMA_SIZE;
    }

    demuxBuffFuture = demuxBuffPresent + dmaSize;                       /* 661 */

    if (demuxBuffFuture > demuxBuff + demuxBuffSize)                    /* 662 */
    {
        dmaSize         = (int)(demuxBuff + demuxBuffSize - demuxBuffPresent);
        demuxBuffFuture = demuxBuffPresent + dmaSize;
    }

    REG_DMAC_4_IPU_TO_QWC  = dmaSize / 16;                              /* 668 */
    REG_DMAC_4_IPU_TO_MADR = (u_int)(uintptr_t)demuxBuffPresent;        /* 669 */
    REG_DMAC_4_IPU_TO_CHCR = 0x101;                                     /* 670 */

    if (demuxBuffFuture >= demuxBuff + demuxBuffSize)                   /* 674 */
    {
        demuxBuffFuture -= demuxBuffSize;                               /* 675 */
    }

    return 1;                                                           /* 676 */
}

/* --------------------------------------------------------------------------
 *  backgroundCallback  (0x279b48)  static
 *
 *  libmpeg is waiting on the IPU and has nothing to do; spend the wait keeping
 *  the demux ring fed.  bgDuration is how many scanlines that is worth, and it
 *  was measured from the picture area when the first frame was set up.
 * ------------------------------------------------------------------------ */
static int backgroundCallback(sceMpeg *mp, sceMpegCbData *cbstr, void *data)
{                                                                       /* 680 */
    int dueTime;

    dueTime = (int)REG_RCNT0_COUNT + bgDuration;                        /* 681 */

    while ((int)REG_RCNT0_COUNT < dueTime)                              /* 682 */
    {
        fillBuff(mp, 0);
    }

    return 1;                                                           /* 683 */
}

/* --------------------------------------------------------------------------
 *  errorCallback  (0x279bc0)  static
 *
 *  Returning 0 tells libmpeg not to continue.  The flag is what both decode
 *  paths poll to end the movie.
 * ------------------------------------------------------------------------ */
static int errorCallback(sceMpeg *mp, sceMpegCbData *cbstr, void *data)
{                                                                       /* 686 */
    printf("MPEG decoding error: '%s'\n", cbstr->error.errMessage);     /* 691 */

    Flags.bErrorCallback = 1;                                           /* 694 */

    return 0;                                                           /* 695 */
}

/* --------------------------------------------------------------------------
 *  fillBuff  (0x279bf8)  static
 *
 *  Demux one pass out of the mux buffer, reading a fresh block first if it is
 *  spent.  Returns the byte count the demuxer took; 0 means it took nothing,
 *  which is what every caller loops until.
 *
 *  `blocking` is what separates the two uses: 1 spins until the stream reader
 *  delivers, 0 gives up and reports 0 so the frame can go on.  Note the block
 *  is demuxed from its *end* -- muxBuffFullness counts down as the demuxer
 *  eats it, and the cursor is derived from that rather than kept.
 * ------------------------------------------------------------------------ */
static int fillBuff(sceMpeg *mp, int blocking)                          /* 703 */
{
    char *startPos;
    /* PORT: the ROM has this too -- the demux result feeds both the subtraction
     * and the return -- but the debug info carries no stab for it. */
    int   size;

    if (muxBuffFullness == 0)                                           /* 708 */
    {
        while ((muxBuffFullness = fileRead(muxBuff, PSS_MUX_BUFF_SIZE)) == 0)
        {                                                               /* 712 */
            if (blocking == 0)                                          /* 713 */
            {
                return 0;
            }
        }

        startPos = muxBuff;                                             /* 717 */
    }
    else
    {
        startPos = muxBuff + (PSS_MUX_BUFF_SIZE - muxBuffFullness);
    }

    size = sceMpegDemuxPss(mp, startPos);                               /* 720 */
    muxBuffFullness -= size;                                            /* 722 */

    return size;                                                        /* 725 */
}
