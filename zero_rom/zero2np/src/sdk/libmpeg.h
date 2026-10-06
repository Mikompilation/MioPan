/* ==========================================================================
 *  libmpeg.h  (MPEG1 system demux + IPU video decode -- PC-port shim)
 *
 *  libmpeg.a is SCE's, not the game's: sceMpegDemuxPss() splits a PSS program
 *  stream into its elementary streams and hands each to a registered callback,
 *  and sceMpegGetPicture() drives the EE's IPU to turn the video stream into
 *  RGB32 macroblocks.  system/playpss/playpss.c is the only caller in the ROM.
 *
 *  The IPU is hardware the host does not have, so the implementation here is a
 *  shim -- see libmpeg.cpp.  It is deliberately the ONE lie in the movie chain:
 *  playpss.c above it is a full reconstruction, and a real decoder dropped in
 *  behind these nine entry points needs no change anywhere else.
 *
 *  Types verbatim from the prototype's own debug info (types.txt).  The
 *  timestamps are spelled int64_t rather than `long`: they are 64-bit on the
 *  EE and `long` is 32-bit on this host, which would move every field after
 *  them and shrink sceMpeg from the ROM's 0x48 to 0x38.
 * ======================================================================== */

#ifndef _LIBMPEG_H
#define _LIBMPEG_H

#include "scetypes.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* One IPU output macroblock, 16x16 pixels of RGBA32.  libipu's type; it lives
 * here because sceMpegGetPicture() is the only thing in the tree that names
 * it. */
typedef struct                      /* 0x400 */
{
    /* 0x000 */ u_int pix[256];
} sceIpuRGB32;

/* Which elementary stream a sceMpegAddStrCallback() registration is for. */
typedef enum
{
    sceMpegStrM2V   = 0,            /* MPEG video                            */
    sceMpegStrIPU   = 1,
    sceMpegStrPCM   = 2,            /* the PSS audio stream -- see below     */
    sceMpegStrADPCM = 3,
    sceMpegStrDATA  = 4
} sceMpegStrType;

/* Which event a sceMpegAddCallback() registration is for. */
typedef enum
{
    sceMpegCbError      = 0,
    sceMpegCbNodata     = 1,        /* the IPU wants another block of ES     */
    sceMpegCbStopDMA    = 2,
    sceMpegCbRestartDMA = 3,
    sceMpegCbBackground = 4,        /* idle time while a picture decodes     */
    sceMpegCbTimeStamp  = 5,
    sceMpegCbStr        = 6
} sceMpegCbType;

typedef struct                      /* 0x8 */
{
    /* 0x0 */ sceMpegCbType type;
    /* 0x4 */ char         *errMessage;
} sceMpegCbDataError;

typedef struct                      /* 0x18 */
{
    /* 0x00 */ sceMpegCbType type;
    /* 0x08 */ int64_t       pts;
    /* 0x10 */ int64_t       dts;
} sceMpegCbDataTimeStamp;

/* What a stream callback is handed: one PES payload, still in the demux
 * buffer libmpeg was created with. */
typedef struct                      /* 0x20 */
{
    /* 0x00 */ sceMpegCbType type;
    /* 0x04 */ u_char       *header;
    /* 0x08 */ u_char       *data;
    /* 0x0c */ u_int         len;
    /* 0x10 */ int64_t       pts;
    /* 0x18 */ int64_t       dts;
} sceMpegCbDataStr;

typedef union                       /* 0x20 */
{
    sceMpegCbType          type;
    sceMpegCbDataError     error;
    sceMpegCbDataTimeStamp ts;
    sceMpegCbDataStr       str;
} sceMpegCbData;

/* The decoder handle.  `width`/`height` come out of the MPEG sequence header
 * and `frameCount` counts pictures decoded -- playpss.c keys its one-time
 * per-movie setup on frameCount being 0. */
typedef struct                      /* 0x48 */
{
    /* 0x00 */ int    width;
    /* 0x04 */ int    height;
    /* 0x08 */ int     frameCount;
    /* 0x10 */ int64_t pts;
    /* 0x18 */ int64_t dts;
    /* 0x20 */ u_long  flags;
    /* 0x28 */ int64_t pts2nd;
    /* 0x30 */ int64_t dts2nd;
    /* 0x38 */ u_long  flags2nd;
    /* 0x40 */ void   *sys;
} sceMpeg;

typedef int (*sceMpegCallback)(sceMpeg *mp, sceMpegCbData *data, void *arg);

/* ---- the nine entry points playpss.o links against --------------------- */

void sceMpegInit(void);

int  sceMpegCreate(sceMpeg *mp, void *buff, int size);
void sceMpegDelete(sceMpeg *mp);
void sceMpegReset(sceMpeg *mp);

/* Non-zero once the stream has run out. */
int  sceMpegIsEnd(sceMpeg *mp);

/* Decode one picture into `rgb32` (`qwc` quadwords of output space).  < 0 if
 * no picture could be produced. */
int  sceMpegGetPicture(sceMpeg *mp, sceIpuRGB32 *rgb32, int qwc);

/* Demultiplex one 16 KB block of program stream, delivering each PES payload
 * to the callback registered for its stream.  Returns the byte count consumed;
 * 0 means "nothing more can be taken from this block". */
int  sceMpegDemuxPss(sceMpeg *mp, void *buff);

int  sceMpegAddCallback(sceMpeg *mp, sceMpegCbType type,
                        sceMpegCallback func, void *arg);
int  sceMpegAddStrCallback(sceMpeg *mp, sceMpegStrType type, int ch,
                           sceMpegCallback func, void *arg);

#ifdef __cplusplus
}
#endif

#endif /* _LIBMPEG_H */
