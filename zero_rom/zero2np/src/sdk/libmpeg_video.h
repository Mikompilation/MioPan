/* ==========================================================================
 *  libmpeg_video.h  (the MPEG-2 decoder behind sceMpegGetPicture)
 *
 *  Kept apart from libmpeg.cpp so the demultiplexer -- which is codec-agnostic
 *  byte parsing and is verified against the disc -- carries no FFmpeg headers,
 *  and so the whole decoder compiles out cleanly when the prebuilt tree is
 *  absent.  See libmpeg_video.cpp.
 * ======================================================================== */

#ifndef _LIBMPEG_VIDEO_H
#define _LIBMPEG_VIDEO_H

#include "libmpeg.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Drop everything and start a new movie. */
void MioPanMpegVideoReset(void);

/* Elementary-stream bytes, as sceMpegDemuxPss() separates them out. */
void MioPanMpegVideoFeed(const u_char *data, u_int len);

/* The stream's own rate, from the sequence header's frame_rate_code.  Only the
 * first call counts; it is what paces the decode. */
void MioPanMpegVideoSetRate(int frame_rate_code);

/* Elementary-stream bytes fed but not yet parsed into pictures.  The
 * demultiplexer runs ahead of the decoder -- far ahead for a film with no
 * audio, which has nothing throttling it -- so this is what says whether the
 * stream really is finished or merely fully read.  See sceMpegIsEnd(). */
int  MioPanMpegVideoPending(void);

/* The last decode attempt ran out of elementary stream.  Combined with the
 * demuxer having seen the terminator, this is the real end of a film. */
int  MioPanMpegVideoStarved(void);

/* Decode one picture into `rgb32` as 16x16 RGBA32 macroblocks in Y-first
 * order, `qwc` of them at most.  Returns 1 if a picture was produced -- 0
 * means "not yet", either because none is due or because the stream has not
 * delivered enough, and is not an error. */
int  MioPanMpegVideoGetPicture(sceMpeg *mp, sceIpuRGB32 *rgb32, int qwc);

#ifdef __cplusplus
}
#endif

#endif /* _LIBMPEG_VIDEO_H */
