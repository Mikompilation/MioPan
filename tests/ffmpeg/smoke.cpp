// Keep checks active in Release builds too.
#undef NDEBUG
#include <cassert>
extern "C" {
#include <libavcodec/avcodec.h>
#include <libswscale/swscale.h>
}

int main()
{
    const AVCodec *codec = avcodec_find_decoder(AV_CODEC_ID_MPEG2VIDEO);
    assert(codec);
    AVCodecContext *context = avcodec_alloc_context3(codec);
    assert(context && avcodec_open2(context, codec, nullptr) == 0);
    AVCodecParserContext *parser = av_parser_init(AV_CODEC_ID_MPEG2VIDEO);
    assert(parser);
    SwsContext *scaler = sws_getContext(16, 16, AV_PIX_FMT_YUV420P,
                                      16, 16, AV_PIX_FMT_RGBA,
                                      SWS_BILINEAR, nullptr, nullptr, nullptr);
    assert(scaler);
    sws_freeContext(scaler);
    av_parser_close(parser);
    avcodec_free_context(&context);
}
