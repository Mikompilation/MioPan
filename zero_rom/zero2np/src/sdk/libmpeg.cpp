/* ==========================================================================
 *  libmpeg.cpp  (MPEG1 system demux + IPU video decode -- PC-port shim)
 *
 *  Two halves, and only one of them is a shim now.
 *
 *  The DEMULTIPLEXER is real.  Splitting a PSS into its elementary streams is
 *  byte parsing that never looks inside either stream, so it is the same work
 *  whatever the video codec is -- and it is all the audio path needs, because
 *  the samples arrive whole and only the container is in the way.  With it,
 *  playpss.c's rings fill, audiodec.c's pump runs, and a movie lasts its real
 *  length instead of ending on frame one.
 *
 *  The VIDEO DECODER lives in libmpeg_video.cpp and is FFmpeg's avcodec.  It
 *  had to be: these streams are MPEG-2, not MPEG-1.  Every sequence header on
 *  the disc is followed by a sequence_extension and every picture carries a
 *  picture_coding_extension with alternate_scan, intra_vlc_format and
 *  q_scale_type set; the GOPs are full IPB (one 160x128 film measures 56 I, 222
 *  P and 552 B pictures), so no MPEG-1 decoder can be bent into it.  Only
 *  avcodec, avutil and swscale are linked -- there is no container left by the
 *  time the bytes reach it, so avformat is not needed.
 *
 *  Build without the prebuilt FFmpeg tree (see cmake/ffmpeg.cmake) and the
 *  decoder compiles out: movies then play their audio over a black screen.
 * ======================================================================== */

#include "libmpeg.h"
#include "libmpeg_video.h"

#include "../miopan/rendering/miopan_video.h"

#include <string.h>

extern "C" {

/* --------------------------------------------------------------------------
 *  Per-handle state
 *
 *  The callback registry is kept for real even though nothing dispatches from
 *  it yet: playpss.c registers five callbacks and a real decoder would need
 *  every one, so dropping them would hide a wiring mistake rather than a
 *  missing decoder.  `sys` carries it, exactly as the real library uses that
 *  field for its private context.
 * ------------------------------------------------------------------------ */

#define MPEG_MAX_STR_CB     8
#define MPEG_MAX_EVENT_CB   8

typedef struct
{
    sceMpegStrType  type;
    int             ch;
    sceMpegCallback func;
    void           *arg;
} MpegStrCb;

typedef struct
{
    sceMpegCbType   type;
    sceMpegCallback func;
    void           *arg;
} MpegEventCb;

typedef struct
{
    void       *buff;
    int         size;
    int         str_cb_num;
    int         event_cb_num;
    MpegStrCb   str_cb[MPEG_MAX_STR_CB];
    MpegEventCb event_cb[MPEG_MAX_EVENT_CB];
} MpegSys;

/* One handle exists in the ROM (playpss.o's playpss_mp) and one movie plays at
 * a time, so a single context is enough and keeps sceMpegCreate() allocation
 * free -- it is called from InitMovie(), which has already taken everything it
 * can from the movie heap. */
static MpegSys mpeg_sys;

/* Raised by the 0x000001B9 end-of-stream code; see sceMpegIsEnd(). */
static int     mpeg_end;
/* The picture buffer is blanked once per movie -- see sceMpegGetPicture(). */
static int     mpeg_cleared;
/* End-of-stream drain tracking; see sceMpegIsEnd(). */
static int     mpeg_end_pending;
static int     mpeg_end_stall;

void sceMpegInit(void)
{
    memset(&mpeg_sys, 0, sizeof(mpeg_sys));
    mpeg_end     = 0;
    mpeg_cleared = 0;
}

int sceMpegCreate(sceMpeg *mp, void *buff, int size)
{
    if (mp == NULL)
        return -1;

    memset(mp, 0, sizeof(*mp));
    memset(&mpeg_sys, 0, sizeof(mpeg_sys));

    mpeg_sys.buff = buff;
    mpeg_sys.size = size;
    mp->sys       = &mpeg_sys;
    mpeg_end         = 0;
    mpeg_cleared     = 0;
    mpeg_end_pending = 0;
    mpeg_end_stall   = 0;

    MioPanMpegVideoReset();

    /* PORT: nothing is on screen for the new movie yet, and the block its
     * picture will land in is whatever the last one left there.  setLoadImageTags()
     * arms the override again on the first frame. */
    MioPan_VideoEnd();

    return 0;
}

void sceMpegDelete(sceMpeg *mp)
{
    if (mp != NULL)
        mp->sys = NULL;

    MioPan_VideoEnd();
}

void sceMpegReset(sceMpeg *mp)
{
    if (mp == NULL)
        return;

    mp->width      = 0;
    mp->height     = 0;
    mp->frameCount = 0;
    mpeg_end         = 0;
    mpeg_cleared     = 0;
    mpeg_end_pending = 0;
    mpeg_end_stall   = 0;

    MioPanMpegVideoReset();
    MioPan_VideoEnd();
    mp->pts        = 0;
    mp->dts        = 0;
    mp->flags      = 0;
}

int sceMpegAddCallback(sceMpeg *mp, sceMpegCbType type,
                       sceMpegCallback func, void *arg)
{
    MpegSys *sys = (mp != NULL) ? (MpegSys *)mp->sys : NULL;

    if (sys == NULL || sys->event_cb_num >= MPEG_MAX_EVENT_CB)
        return -1;

    sys->event_cb[sys->event_cb_num].type = type;
    sys->event_cb[sys->event_cb_num].func = func;
    sys->event_cb[sys->event_cb_num].arg  = arg;
    sys->event_cb_num++;

    return 0;
}

int sceMpegAddStrCallback(sceMpeg *mp, sceMpegStrType type, int ch,
                          sceMpegCallback func, void *arg)
{
    MpegSys *sys = (mp != NULL) ? (MpegSys *)mp->sys : NULL;

    if (sys == NULL || sys->str_cb_num >= MPEG_MAX_STR_CB)
        return -1;

    sys->str_cb[sys->str_cb_num].type = type;
    sys->str_cb[sys->str_cb_num].ch   = ch;
    sys->str_cb[sys->str_cb_num].func = func;
    sys->str_cb[sys->str_cb_num].arg  = arg;
    sys->str_cb_num++;

    return 0;
}

/* --------------------------------------------------------------------------
 *  sceMpegDemuxPss  --  the program-stream demultiplexer
 *
 *  This half is real.  Splitting a PSS into its elementary streams is byte
 *  parsing that never looks inside either stream, so it is the same work
 *  whether the video is MPEG-1 or MPEG-2 -- and it is what the audio path
 *  needs, since the samples arrive whole and only the container is in the way.
 *
 *  The layout, read off the disc rather than assumed: 16 KB packs, each an
 *  MPEG-2 pack header (14 bytes, pack_stuffing_length 0) followed by PES
 *  packets -- 0xE0 video, 0xBD private stream 1 carrying SCE's SPU stream,
 *  0xBB the system header, 0xBE padding out to the pack boundary.  movie.c
 *  reads exactly 16 KB at a time (MyStrStart's one_buf_sector is 8), so one
 *  fillBuff() read is one pack.
 *
 *  The contract with fillBuff() is what shapes this: it is handed a cursor, not
 *  a length, and returns how many bytes it consumed, which the caller subtracts
 *  from muxBuffFullness.  One call therefore takes exactly one packet -- and if
 *  the callback refuses the payload (videoCallback and audioCallback both do
 *  when their ring is full) nothing is consumed and the answer is 0, so the
 *  same packet is offered again next frame.  That is why the callback's own
 *  return value has to be honoured rather than ignored.
 * ------------------------------------------------------------------------ */

/* Stream ids.  0xBD is "private stream 1", which is where SCE puts the SPU
 * stream; its payload opens with a 4-byte sub-stream header that playpss.c's
 * audioCallback() steps over. */
#define PES_ID_PACK             0xba
#define PES_ID_END              0xb9
#define PES_ID_SYSTEM           0xbb
#define PES_ID_PADDING          0xbe
#define PES_ID_PRIVATE1         0xbd
#define PES_ID_VIDEO            0xe0

static sceMpegCallback FindStrCallback(MpegSys *sys, sceMpegStrType type,
                                       void **arg)
{
    for (int i = 0; i < sys->str_cb_num; i++)
    {
        if (sys->str_cb[i].type == type)
        {
            *arg = sys->str_cb[i].arg;
            return sys->str_cb[i].func;
        }
    }

    *arg = NULL;
    return NULL;
}

/* The sequence header carries the picture size in 12 bits each.  Reading it
 * here rather than in the decoder is what lets playPssGetMpegInfo() answer
 * correctly with no decoder behind it -- CMovieRoom::Draw() sizes its screen
 * quad from that. */
static void ReadSequenceHeader(sceMpeg *mp, const u_char *data, u_int len)
{
    if (mp->width != 0 || len < 12)
        return;

    for (u_int i = 0; i + 8 <= len; i++)
    {
        if (data[i] == 0x00 && data[i + 1] == 0x00 &&
            data[i + 2] == 0x01 && data[i + 3] == 0xb3)
        {
            mp->width  = ((int)data[i + 4] << 4) | (data[i + 5] >> 4);
            mp->height = (((int)data[i + 5] & 0x0f) << 8) | data[i + 6];

            /* Low nibble of the next byte is frame_rate_code. */
            MioPanMpegVideoSetRate(data[i + 7] & 0x0f);
            return;
        }
    }
}

/* A 33-bit timestamp spread over five bytes with marker bits between. */
static long ReadTimeStamp(const u_char *p)
{
    return ((long)(p[0] & 0x0e) << 29)
         | ((long)p[1] << 22) | ((long)(p[2] & 0xfe) << 14)
         | ((long)p[3] << 7)  | ((long)(p[4] & 0xfe) >> 1);
}

int sceMpegDemuxPss(sceMpeg *mp, void *buff)
{
    MpegSys      *sys = (mp != NULL) ? (MpegSys *)mp->sys : NULL;
    const u_char *p   = (const u_char *)buff;
    u_int         len;
    u_int         hdr_len;
    u_int         flags;
    u_int         id;

    if (sys == NULL || p == NULL)
        return 0;

    if (p[0] != 0x00 || p[1] != 0x00 || p[2] != 0x01)
    {
        /* Not on a packet boundary.  The ROM's demuxer is always handed one --
         * fillBuff() only ever resumes where the last call stopped -- so this
         * means the stream is not what we think it is.  Report nothing
         * consumed rather than guessing, which stops the caller's loop. */
        return 0;
    }

    id = p[3];

    if (id == PES_ID_PACK)
    {
        /* MPEG-2 pack header: 14 bytes plus pack_stuffing_length. */
        return 14 + (int)(p[13] & 7u);
    }

    if (id == PES_ID_END)
    {
        mpeg_end = 1;
        return 4;
    }

    len = ((u_int)p[4] << 8) | p[5];

    if (id != PES_ID_VIDEO && id != PES_ID_PRIVATE1)
    {
        /* System header, padding, anything else: skipped whole. */
        return (int)(6 + len);
    }

    /* MPEG-2 PES header: two flag bytes then the header length. */
    flags   = p[7];
    hdr_len = p[8];

    {
        sceMpegCbDataStr  cb;
        sceMpegCallback   func;
        void             *arg;
        int               ret;

        memset(&cb, 0, sizeof(cb));
        cb.type   = sceMpegCbStr;
        cb.header = (u_char *)(uintptr_t)p;
        cb.data   = (u_char *)(uintptr_t)(p + 9 + hdr_len);
        cb.len    = len - 3 - hdr_len;

        if ((flags & 0xc0) != 0)
            cb.pts = ReadTimeStamp(p + 9);
        if ((flags & 0xc0) == 0xc0)
            cb.dts = ReadTimeStamp(p + 14);

        func = FindStrCallback(sys,
                               (id == PES_ID_VIDEO) ? sceMpegStrM2V
                                                    : sceMpegStrPCM,
                               &arg);

        /* A stream with no callback registered -- a silent movie does not
         * register the audio one -- is consumed and dropped. */
        if (func != NULL)
        {
            ret = func(mp, (sceMpegCbData *)&cb, arg);

            /* 0 is back-pressure: the ring is full, so leave the packet where
             * it is and let the caller come back to it. */
            if (ret == 0)
                return 0;
        }

        /* Only now is the payload certainly consumed, and only now may it go
         * to the decoder.  Feeding it above -- before the callback had a say
         * -- put every back-pressured payload into the elementary stream
         * twice, once for each time the caller re-offered the packet.  ffmpeg
         * reported the duplicated slices as "ac-tex damaged" and eventually
         * died on a parser index assertion.
         *
         * The decoder keeps its own copy: playpss.c's ring belongs to the
         * ROM's IPU path and is consumed by nodataCallback(), not by us. */
        if (id == PES_ID_VIDEO)
        {
            ReadSequenceHeader(mp, cb.data, cb.len);
            MioPanMpegVideoFeed(cb.data, cb.len);
        }

        return (int)(6 + len);
    }
}

/* --------------------------------------------------------------------------
 *  sceMpegIsEnd
 *
 *  Set by the 0x000001B9 end-of-stream code the muxer writes after the last
 *  pack.  Nothing else reports the end: MyStrRead() answers "size bytes" for
 *  ever whatever the streamer did, so the terminator in the data is the only
 *  signal there is.
 * ------------------------------------------------------------------------ */
int sceMpegIsEnd(sceMpeg *mp)
{
    (void)mp;

    if (mpeg_end == 0)
        return 0;

    /* PORT: the terminator means the stream has been fully READ, not fully
     * PLAYED, and here those are far apart.
     *
     * On the console the IPU was fed straight out of the demux ring, so the
     * two coincided.  Here the decoder keeps its own elementary-stream buffer
     * and paces itself off the film's frame rate, while the demultiplexer runs
     * as fast as back-pressure allows.  For a cutscene the audio ring supplies
     * that back-pressure and the two stay roughly together -- but a movie-room
     * film has no audio at all, so nothing throttles the demuxer and it reads
     * a 3 MB file in a couple of seconds.  Reporting the end there tore the
     * film down almost immediately, with thirty seconds of pictures still
     * buffered: CMovieRoom::Work() saw 1 and movie_projecter called Release().
     *
     * So the end is not reported until the decoder has drained too.  The
     * backlog can only shrink once the demuxer has stopped feeding, and the
     * stall guard covers a tail the parser can never complete -- without it a
     * truncated stream would hold the phase open for ever. */
    /* The decoder still has pictures to give.  Not "pending bytes have
     * stopped changing", which is what this used to test and which was wrong:
     * an MPEG-2 stream is full of B-frames, so avcodec hands back several
     * buffered pictures without the parser advancing at all.  The counter
     * reached its limit while the film was still playing perfectly, and every
     * projector reel was cut off after about twelve seconds -- roughly the
     * three the demuxer takes to race through the file plus the three hundred
     * calls the guard allowed.
     *
     * `starved` is set by the decoder itself when a decode attempt finds no
     * more elementary stream, so this is the exact condition rather than a
     * proxy for it. */
    if (MioPanMpegVideoStarved() == 0)
    {
        mpeg_end_stall = 0;
        return 0;
    }

    /* Starved and fully read, but give the parser a few frames to surrender
     * anything it is still holding before declaring the film finished. */
    if (++mpeg_end_stall < 8)
        return 0;

    return 1;
}

/* --------------------------------------------------------------------------
 *  sceMpegGetPicture  --  STILL A SHIM, and now the only one
 *
 *  The video in these streams is MPEG-2, not MPEG-1: every sequence header on
 *  the disc is followed by a sequence_extension, and every picture carries a
 *  picture_coding_extension with alternate_scan, intra_vlc_format and
 *  q_scale_type set.  The GOPs are full IPB (one 160x128 film measures 56 I,
 *  222 P and 552 B pictures).  So this needs a real MPEG-2 decoder; an MPEG-1
 *  one cannot be bent into it.
 *
 *  < 0 = no picture, which playpss.c reports and then gives up on the frame.
 *  Note the movie still runs its full length now -- sceMpegIsEnd() only answers
 *  yes at the real terminator -- so a cutscene plays its audio to the end over
 *  a blank screen, and START or CROSS still skips it.
 *
 *  Whatever decoder lands here has to write the IPU's output layout, because
 *  that is what ldimage.c's transfer chain indexes: 16x16 RGBA32 macroblocks in
 *  Y-first (column-major) order, `qwc` quadwords of room.
 * ------------------------------------------------------------------------ */
int sceMpegGetPicture(sceMpeg *mp, sceIpuRGB32 *rgb32, int qwc)
{
    MpegSys *sys = (mp != NULL) ? (MpegSys *)mp->sys : NULL;

    /* 0, NOT a negative.  Both call sites read < 0 as a decoder failure and
     * raise Flags.bErrorCallback, which ends the movie on the very next frame
     * -- before the audio has started.  "No picture this frame" is not an
     * error, and the negative return stays reserved for a decoder that really
     * did fail. */

    /* Blank the picture once per movie, before anything is decoded into it.
     * The transfer chain is a real one, so without this the first frames would
     * walk uninitialised heap into GS memory -- and with no decoder at all it
     * is what keeps the screen black rather than full of garbage. */
    if (!mpeg_cleared && rgb32 != NULL && qwc > 0)
    {
        memset(rgb32, 0, (size_t)qwc * sizeof(sceIpuRGB32));
        mpeg_cleared = 1;
    }

    /* The decoder.  It writes the IPU's own layout -- 16x16 RGBA32 macroblocks
     * in Y-first order -- because that is what ldimage.c's chain indexes, and
     * it paces itself off the stream's frame rate because this is called once
     * per game frame rather than once per picture. */
    {
        const int got = MioPanMpegVideoGetPicture(mp, rgb32, qwc);
    }

    /* The housekeeping is the real library's own: the Nodata callback retires
     * the slice of demuxBuff the IPU has finished with and queues the next.
     * Skipping it deadlocks the pipeline rather than just losing the video --
     * a ring that never drains makes videoCallback() refuse for ever, and the
     * demuxer cannot step past a refused packet to reach the audio behind it.
     *
     * Several slices per call, not one: a 640x448 picture in these streams
     * averages about 9.5 KB, and draining less than a frame's worth leaves the
     * ring full, which turns playPssSetPacket()'s refill budget into a busy
     * wait on a demuxer that can no longer make progress. */
    if (sys != NULL)
    {
        for (int i = 0; i < sys->event_cb_num; i++)
        {
            if (sys->event_cb[i].type == sceMpegCbNodata &&
                sys->event_cb[i].func != NULL)
            {
                for (int slice = 0; slice < 8; slice++)
                {
                    sceMpegCbData cb;

                    memset(&cb, 0, sizeof(cb));
                    cb.type = sceMpegCbNodata;
                    sys->event_cb[i].func(mp, &cb, sys->event_cb[i].arg);
                }
                break;
            }
        }
    }

    return 0;
}

}
