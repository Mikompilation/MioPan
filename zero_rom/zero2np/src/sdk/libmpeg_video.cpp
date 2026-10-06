/* ==========================================================================
 *  libmpeg_video.cpp  (the MPEG-2 decoder behind sceMpegGetPicture)
 *
 *  The one piece of the movie chain that was never software on the PS2: the
 *  IPU decoded macroblocks in hardware.  These streams are MPEG-2 -- every
 *  sequence header on the disc carries a sequence_extension, every picture a
 *  picture_coding_extension, and the GOPs are full IPB -- so this is FFmpeg's
 *  avcodec, fed the raw elementary stream that sdk/libmpeg.cpp's demultiplexer
 *  has already separated out.  No avformat: there is no container left by the
 *  time the bytes get here.
 *
 *  THERE ARE TWO OUTPUT PATHS, and the direct one is the default.
 *
 *  Direct (miopan/rendering/miopan_video.h): the scaler writes its linear RGBA
 *  straight into a buffer the renderer uploads to one persistent texture, and
 *  every TEX0 naming the movie's GS block is answered with it.  Nothing is
 *  repacked, nothing enters emulated GS memory, and nothing is hashed -- which
 *  is what stops a cutscene creating one uncached 1024x512 host texture per
 *  frame.  That header carries the whole of the reasoning.
 *
 *  The ROM's, used verbatim whenever the direct path cannot take a frame.
 *  Two things make its output the IPU's rather than a normal decoder's:
 *
 *    - The picture is repacked into 16x16 RGBA32 macroblocks in Y-first
 *      (column-major) order, because that is what ldimage.c's transfer chain
 *      indexes.  See setLoadImageTagsYX(): `i` walks columns, `j` rows, and the
 *      source pointer advances one macroblock per step.
 *    - Alpha is forced to 0x80.  That is the PS2's opaque, not 0xff, and the
 *      GS reads texture alpha at that scale.
 *
 *  The direct path leaves alpha at whatever swscale produced (0xff), because
 *  it never passes through the GS's 0..0x80 texture-alpha scale.
 *
 *  PACING IS A PORT DEVIATION, and a necessary one.  sceMpegGetPicture() is
 *  called once per game frame -- 60 Hz in NTSC -- while the films are 25 or
 *  29.97 fps, and on hardware the IPU and the PTS machinery held the rate.
 *  Here the frame_rate_code out of the sequence header does it: a picture is
 *  decoded only when one is due, and between times the previous one is left in
 *  the buffer, which is what a held frame looks like to the transfer chain.
 *
 *  Built only when cmake/ffmpeg.cmake found the prebuilt tree; without it every
 *  entry point below reports "no picture" and movies play their audio over a
 *  black screen.
 * ======================================================================== */

#include "libmpeg.h"
#include "libmpeg_video.h"

#include "../miopan/rendering/miopan_video.h"

#include <string.h>

#if MIOPAN_HAVE_FFMPEG

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/imgutils.h>
#include <libavutil/time.h>
#include <libswscale/swscale.h>
}

#include <vector>

namespace {

struct MpegVideo
{
    const AVCodec   *codec   = nullptr;
    AVCodecContext  *ctx     = nullptr;
    AVCodecParserContext *parser = nullptr;
    AVPacket        *pkt     = nullptr;
    AVFrame         *frame   = nullptr;
    SwsContext      *sws     = nullptr;
    int              sws_w   = 0;
    int              sws_h   = 0;
    unsigned char   *rgba    = nullptr;
    int              rgba_sz = 0;

    /* The elementary stream as it arrives, and how far the parser has got. */
    std::vector<unsigned char> es;
    size_t                     es_read = 0;

    /* Pacing: nanoseconds per picture, and when the next one is due. */
    double  frame_ns   = 0.0;
    double  next_due   = 0.0;
    bool    have_first = false;

    /* The first call primes and produces nothing -- see GetPicture(). */
    bool    primed     = false;

    /* The last decode attempt found no more elementary stream.  With the
     * demuxer finished this is the real "the film is over" -- see
     * sceMpegIsEnd(). */
    bool    starved    = false;
};

MpegVideo g_video;

/* The stream's own rate, from the sequence header's frame_rate_code. */
double FrameRateNs(int code)
{
    switch (code)
    {
    case 1:  return 1e9 / (24000.0 / 1001.0);
    case 2:  return 1e9 / 24.0;
    case 3:  return 1e9 / 25.0;
    case 4:  return 1e9 / (30000.0 / 1001.0);
    case 5:  return 1e9 / 30.0;
    case 6:  return 1e9 / 50.0;
    case 7:  return 1e9 / (60000.0 / 1001.0);
    case 8:  return 1e9 / 60.0;
    default: return 1e9 / (30000.0 / 1001.0);
    }
}

double NowNs(void)
{
    return (double)av_gettime_relative() * 1000.0;
}

/* YUV -> RGBA once per picture, then straight into macroblock order. */
void RepackMacroblocks(const unsigned char *rgba, int stride,
                       int width, int height, sceIpuRGB32 *out, int capacity)
{
    const int mbx = width  >> 4;
    const int mby = height >> 4;

    if (mbx <= 0 || mby <= 0 || mbx * mby > capacity)
        return;

    for (int i = 0; i < mbx; i++)
    {
        for (int j = 0; j < mby; j++)
        {
            /* Column-major: the block index has to match the order
             * setLoadImageTagsYX() walks its source pointer in. */
            u_int *dst = out[i * mby + j].pix;

            for (int row = 0; row < 16; row++)
            {
                const unsigned char *src =
                    rgba + (size_t)(j * 16 + row) * stride + (size_t)(i * 16) * 4;

                for (int col = 0; col < 16; col++)
                {
                    /* RGBA bytes little-endian == 0xAABBGGRR, and the PS2's
                     * opaque alpha is 0x80. */
                    dst[row * 16 + col] = ((u_int)src[col * 4 + 0])
                                        | ((u_int)src[col * 4 + 1] << 8)
                                        | ((u_int)src[col * 4 + 2] << 16)
                                        | ((u_int)0x80u << 24);
                }
            }
        }
    }
}

bool EnsureOpen(void)
{
    if (g_video.ctx != nullptr)
        return true;

    g_video.codec = avcodec_find_decoder(AV_CODEC_ID_MPEG2VIDEO);
    if (g_video.codec == nullptr)
        return false;

    g_video.ctx = avcodec_alloc_context3(g_video.codec);
    if (g_video.ctx == nullptr)
        return false;

    if (avcodec_open2(g_video.ctx, g_video.codec, nullptr) < 0)
        return false;

    /* The demuxer hands over arbitrary PES payloads, not frame-aligned ones,
     * so the parser is what finds the picture boundaries. */
    g_video.parser = av_parser_init(AV_CODEC_ID_MPEG2VIDEO);
    g_video.pkt    = av_packet_alloc();
    g_video.frame  = av_frame_alloc();

    return g_video.parser != nullptr && g_video.pkt != nullptr &&
           g_video.frame != nullptr;
}

/* `need_staging` is false on the direct path, where the scaler writes into the
 * buffer miopan_video.cpp handed over and this file needs none of its own. */
bool EnsureScaler(int w, int h, bool need_staging)
{
    if (g_video.sws == nullptr || g_video.sws_w != w || g_video.sws_h != h)
    {
        if (g_video.sws != nullptr)
        {
            sws_freeContext(g_video.sws);
            g_video.sws = nullptr;
        }

        g_video.sws = sws_getContext(w, h, g_video.ctx->pix_fmt,
                                     w, h, AV_PIX_FMT_RGBA,
                                     SWS_BILINEAR, nullptr, nullptr, nullptr);
        if (g_video.sws == nullptr)
            return false;

        g_video.sws_w = w;
        g_video.sws_h = h;
    }

    if (!need_staging)
        return true;

    const int need = av_image_get_buffer_size(AV_PIX_FMT_RGBA, w, h, 1);
    if (need > g_video.rgba_sz)
    {
        av_free(g_video.rgba);
        g_video.rgba    = (unsigned char *)av_malloc((size_t)need);
        g_video.rgba_sz = (g_video.rgba != nullptr) ? need : 0;
    }

    return g_video.rgba != nullptr;
}

/* Push what the parser can take out of the accumulated stream until a picture
 * comes back, or the stream runs dry. */
bool DecodeOne(void)
{
    for (;;)
    {
        if (avcodec_receive_frame(g_video.ctx, g_video.frame) == 0)
            return true;

        if (g_video.es_read >= g_video.es.size())
            return false;

        unsigned char *out      = nullptr;
        int            out_size = 0;
        const size_t   avail    = g_video.es.size() - g_video.es_read;

        const int used = av_parser_parse2(
            g_video.parser, g_video.ctx, &out, &out_size,
            g_video.es.data() + g_video.es_read, (int)avail,
            AV_NOPTS_VALUE, AV_NOPTS_VALUE, 0);

        if (used < 0)
            return false;

        g_video.es_read += (size_t)used;

        if (out_size > 0)
        {
            g_video.pkt->data = out;
            g_video.pkt->size = out_size;
            avcodec_send_packet(g_video.ctx, g_video.pkt);
        }
        else if (used == 0)
        {
            /* The parser wants more than we have. */
            return false;
        }

        /* Keep the backlog bounded: everything before the parser's cursor has
         * been consumed and a movie is megabytes long. */
        if (g_video.es_read > (1u << 20))
        {
            g_video.es.erase(g_video.es.begin(),
                             g_video.es.begin() + (long)g_video.es_read);
            g_video.es_read = 0;
        }
    }
}

}  /* namespace */

void MioPanMpegVideoReset(void)
{
    if (g_video.sws != nullptr)    { sws_freeContext(g_video.sws); g_video.sws = nullptr; }
    if (g_video.parser != nullptr) { av_parser_close(g_video.parser); g_video.parser = nullptr; }
    if (g_video.frame != nullptr)  { av_frame_free(&g_video.frame); }
    if (g_video.pkt != nullptr)    { av_packet_free(&g_video.pkt); }
    if (g_video.ctx != nullptr)    { avcodec_free_context(&g_video.ctx); }
    av_free(g_video.rgba);

    g_video.rgba       = nullptr;
    g_video.rgba_sz    = 0;
    g_video.sws_w      = 0;
    g_video.sws_h      = 0;
    g_video.es.clear();
    g_video.es_read    = 0;
    g_video.frame_ns   = 0.0;
    g_video.next_due   = 0.0;
    g_video.have_first = false;
    g_video.primed     = false;
    g_video.starved    = false;
}

int MioPanMpegVideoStarved(void)
{
    return g_video.starved ? 1 : 0;
}

int MioPanMpegVideoPending(void)
{
    if (g_video.es.size() <= g_video.es_read)
        return 0;

    return (int)(g_video.es.size() - g_video.es_read);
}

void MioPanMpegVideoFeed(const u_char *data, u_int len)
{
    if (data == NULL || len == 0)
        return;

    g_video.es.insert(g_video.es.end(), data, data + len);
}

void MioPanMpegVideoSetRate(int frame_rate_code)
{
    if (g_video.frame_ns == 0.0)
        g_video.frame_ns = FrameRateNs(frame_rate_code);
}

int MioPanMpegVideoGetPicture(sceMpeg *mp, sceIpuRGB32 *rgb32, int qwc)
{
    if (mp == NULL || rgb32 == NULL || qwc <= 0)
        return 0;

    if (!EnsureOpen())
        return 0;

    /* THE FIRST CALL MUST PRODUCE NOTHING, and this is not an optimisation.
     *
     * playPssSetPacket() and videoDecMain() both build the GS transfer chain
     * under `if (playpss_mp.frameCount == 0)`, *after* calling this -- so a
     * decoder that returns a picture on its first call leaves frameCount at 1
     * and the chain is never built at all.  loadImage() then walks a zeroed
     * path3tag, nothing is uploaded, and the screen stays black.
     *
     * The ROM gets away with the ordering because the IPU is a pipeline: its
     * first sceMpegGetPicture() feeds the hardware and the picture arrives on
     * a later call.  Mirroring that latency here is what keeps the ROM's own
     * sequencing correct, and it costs one frame.
     *
     * The geometry the chain needs is already known by now -- the demultiplexer
     * reads it out of the sequence header long before this. */
    if (!g_video.primed)
    {
        g_video.primed = true;
        return 0;
    }

    /* Hold the previous picture until this one is due -- see the banner. */
    const double now = NowNs();

    if (!g_video.have_first)
    {
        g_video.next_due = now;
    }
    else if (now < g_video.next_due)
    {
        return 0;
    }

    if (!DecodeOne())
    {
        g_video.starved = true;
        return 0;
    }

    g_video.starved = false;

    /* CATCH UP rather than falling permanently behind.
     *
     * One picture per call is enough while the game frame rate is at or above
     * the film's -- 30 Hz against these 29.97 fps streams.  It is not enough
     * after a slow frame: the schedule below used to be reset to `now` when it
     * had slipped, which silently discarded the deficit and left the film
     * running late for the rest of its length, drifting further from the audio
     * every time it happened.
     *
     * So when more than one picture is already due, decode through the backlog
     * and keep only the newest -- the same thing a dropped frame is.  Bounded,
     * because a long stall must not turn into an unbounded decode loop inside
     * one frame; whatever is left over is given up on by the reset below. */
    {
        const int kMaxCatchUp = 4;
        int       skipped     = 0;

        while (skipped < kMaxCatchUp &&
               g_video.have_first &&
               now >= g_video.next_due + g_video.frame_ns)
        {
            av_frame_unref(g_video.frame);
            if (!DecodeOne())
                break;
            g_video.next_due += g_video.frame_ns;
            skipped++;
        }
    }

    const int w = g_video.frame->width;
    const int h = g_video.frame->height;

    /* The direct path: scale straight into the buffer the renderer will
     * upload, and skip the macroblock repack entirely -- nothing downstream
     * reads `rgb32` while it is live.  A NULL here (the path disabled, or the
     * buffer could not be sized) falls back to the ROM's GS transfer, which is
     * still complete and correct.  See miopan/rendering/miopan_video.h. */
    unsigned char *direct = MioPan_VideoBeginFrame(w, h);

    if (w <= 0 || h <= 0 || !EnsureScaler(w, h, direct == nullptr))
    {
        av_frame_unref(g_video.frame);
        return 0;
    }

    unsigned char *target = (direct != nullptr) ? direct : g_video.rgba;
    unsigned char *dst[4]     = { target, nullptr, nullptr, nullptr };
    int            dst_line[4] = { w * 4, 0, 0, 0 };

    sws_scale(g_video.sws, g_video.frame->data, g_video.frame->linesize,
              0, h, dst, dst_line);

    if (direct != nullptr)
        MioPan_VideoEndFrame();
    else
        RepackMacroblocks(g_video.rgba, w * 4, w, h, rgb32, qwc);

    av_frame_unref(g_video.frame);

    mp->width  = w;
    mp->height = h;
    mp->frameCount++;

    if (g_video.frame_ns <= 0.0)
        g_video.frame_ns = FrameRateNs(4);

    /* Advance by whole frame periods so the film keeps its own timeline.  The
     * clamp is the backstop for a stall longer than the catch-up above could
     * absorb: past that the deficit is written off rather than chased for ever,
     * which is what stops a single long hitch turning into a permanent run of
     * maximum-rate decoding. */
    g_video.next_due += g_video.frame_ns;
    if (g_video.next_due < now - g_video.frame_ns * 4.0)
        g_video.next_due = now;

    g_video.have_first = true;

    return 1;
}

#else   /* !MIOPAN_HAVE_FFMPEG */

void MioPanMpegVideoReset(void) {}
void MioPanMpegVideoFeed(const u_char *data, u_int len) { (void)data; (void)len; }
int  MioPanMpegVideoPending(void) { return 0; }
int  MioPanMpegVideoStarved(void) { return 1; }
void MioPanMpegVideoSetRate(int frame_rate_code) { (void)frame_rate_code; }

int MioPanMpegVideoGetPicture(sceMpeg *mp, sceIpuRGB32 *rgb32, int qwc)
{
    (void)mp; (void)rgb32; (void)qwc;
    return 0;
}

#endif  /* MIOPAN_HAVE_FFMPEG */
