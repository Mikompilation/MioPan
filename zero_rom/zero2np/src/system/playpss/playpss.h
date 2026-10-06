/* ==========================================================================
 *  system/playpss/playpss.h
 *
 *  The PSS movie player, linked into the ROM out of playpss.a -- four objects
 *  (playpss.o, audiodec.o, ldimage.o, my_strfile.o) built from
 *  /home/akira_koide/zero2np/src/system/playpss/.
 *
 *  It is a hardware decoder: the EE demuxes a PSS stream into an MPEG1 video
 *  elementary stream and an ADPCM audio one, hands the video to the IPU, pushes
 *  the decoded RGB32 macroblocks to the GS over PATH3, and DMAs the audio to
 *  the IOP.
 *
 *  playpss.o and audiodec.o are full reconstructions; my_strfile.o is too.  The
 *  hardware they drive is not: the IPU lives behind sdk/libmpeg.cpp and the GS
 *  transfer chain behind ldimage.c, and both are shims that say so.
 *
 *  Declarations from the prototype's call sites, ZERO2.MAP and types.txt.
 *  Reconstructed from the Feb 6 2004 prototype (SLES_523.84).
 * ======================================================================== */

#ifndef _SYSTEM_PLAYPSS_PLAYPSS_H
#define _SYSTEM_PLAYPSS_PLAYPSS_H

#include "eetypes.h"
#include "../../sdk/libmpeg.h"      /* sceMpeg / sceIpuRGB32 / the callbacks */

#ifdef __cplusplus
extern "C" {
#endif

/* Everything the decoder needs, filled in by InitMovie() and handed to
 * playPssStart() / playPssMain().  The iop* members are IOP-side addresses,
 * which is why they are ints rather than pointers. */
typedef struct                      /* 0x3c */
{
    /* 0x00 */ u_char      *mpegBuff;           /* demuxed video ES ring     */
    /* 0x04 */ int          mpegBuffSize;
    /* 0x08 */ sceIpuRGB32 *rgb32;              /* IPU output                */
    /* 0x0c */ u_int       *path3tag;           /* GS load-image DMA chain   */
    /* 0x10 */ void        *demuxBuff;          /* raw PSS sectors           */
    /* 0x14 */ int          demuxBuffSize;
    /* 0x18 */ u_char      *audioBuff;          /* demuxed audio ES          */
    /* 0x1c */ int          audioBuffSize;
    /* 0x20 */ int          iopBuff;            /* its IOP-side twin         */
    /* 0x24 */ int          iopBuffSize;
    /* 0x28 */ u_char      *zeroBuff;           /* one sector of silence     */
    /* 0x2c */ int          iopZeroBuff;
    /* 0x30 */ char        *read_buf;           /* stream read staging       */
    /* 0x34 */ int          audio_vol_percent;
    /* 0x38 */ int          auto_dma_core;      /* SPU core claimed for it   */
} playPssRsrcs;

/* The stream reader playPssStart() is handed; my_strfile.c's MyStrRead. */
typedef int (*playPssReadFunc)(void *buff, int size);

/* The decoded frame's dimensions, read out of the MPEG sequence header.  The
 * movie-room films are 160x128; the full-screen cutscenes are larger. */
typedef struct _PLAY_PSS_MPEG_INFO   /* 0x8 */
{
    /* 0x0 */ int width;
    /* 0x4 */ int height;
} PLAY_PSS_MPEG_INFO;

/* ---- playpss.o -- the decoder driver (reconstructed) ------------------- */

void playPssInit(void);
void playPssStart(playPssReadFunc read, playPssRsrcs *rsrcs);

/* The same start, without blocking until the first frame has decoded.  The
 * caller then polls playPssIsReady() -- that pair is what lets movie_room.o
 * preload a film across several frames while the room keeps running, where
 * movie.c's full-screen path can afford to stall. */
void playPssStartNoWait(playPssReadFunc read, playPssRsrcs *rsrcs);
int  playPssIsReady(void);

void playPssGetMpegInfo(PLAY_PSS_MPEG_INFO *info);

/* One frame.  0 while decoding, 1 at end of stream or on a decoder error,
 * 2 once a requested stop has drained. */
int  playPssMain(playPssRsrcs *rsrcs, int vram_adrs);

/* What playPssMain() is: decode a frame and either send it to the GS itself
 * (pTagEndAdrs NULL) or leave the transfer chain for the caller to splice into
 * its own display list, writing one past its last word through pTagEndAdrs.
 * The second form also centres the image in a 640x448 screen; the first takes
 * whatever origin setLoadImageTags() was last given. */
int  playPssSetPacket(playPssRsrcs *rsrcs, int vram_adrs, int *pTagEndAdrs);

void playPssStop(void);
void playPssEnd(void);
void playPssPause(void);
void playPssRestart(void);
int  playPssIsPause(void);

/* Non-zero when there is a decoded image in VRAM to blit.  In NTSC that is
 * unconditional -- playPssMain() decoded and sent it synchronously a moment
 * ago -- and in PAL it is the decoder thread's own "a frame landed" flag,
 * because there the decode runs off the vblank divider instead. */
int  playPssAlreadySendImage(void);

/* Arm (or disarm) the NTSC->PAL conversion and tell it which VRAM block to
 * scale through. */
void playPssSetNtsc2Pal(int on, int vram_adrs);

/* ---- my_strfile.o -- the stream reader (reconstructed) ----------------- */

int  MyStrStart(int file_no, int ring_buf_num, int one_buf_sector);
int  MyStrRead(void *buff, int size);
int  MyStrStop(void);

#ifdef __cplusplus
}
#endif

#endif /* _SYSTEM_PLAYPSS_PLAYPSS_H */
