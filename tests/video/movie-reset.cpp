#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <unordered_map>
#include <vector>
#include "zero_rom/zero2np/src/miopan/rendering/miopan_video.h"
using Uint32 = uint32_t;
struct SDL_GPUTexture {};
struct sceGsTex0 { int TW, TH; };
struct TextureEntry { SDL_GPUTexture *texture; int width, height; uint64_t hash; bool unused; };
struct PendingTextureUpload { uint64_t hash; SDL_GPUTexture *texture; Uint32 width, height; std::vector<unsigned char> rgba; };
static std::unordered_map<uint64_t, TextureEntry> g_texture_cache;
static std::vector<PendingTextureUpload> g_pending_texture_uploads;
static void *g_device;
constexpr uint64_t kVideoTextureHash = 0x4D494F50414E5644ull;
static SDL_GPUTexture *CreateTextureResource(int, int) { return new SDL_GPUTexture; }
static void ReleaseFrameTexture(SDL_GPUTexture *p) { delete p; }
static void SDL_ReleaseGPUTexture(void *, SDL_GPUTexture *p) { delete p; }
#include "video-texture.inc"
static void ExpectBlank()
{
    assert(g_pending_texture_uploads.size() == 1);
    const auto &p = g_pending_texture_uploads.back();
    assert(p.width == 1024 && p.height == 512);
    assert(std::all_of(p.rgba.begin(), p.rgba.end(), [](unsigned char x) { return x == 0; }));
    g_pending_texture_uploads.clear();
}
static void Publish(unsigned char value)
{
    auto p = MioPan_VideoBeginFrame(2, 2);
    assert(p); std::memset(p, value, 16); MioPan_VideoEndFrame();
}
int main()
{
    sceGsTex0 tex0{10, 9};
    MioPan_VideoBegin(0x2bc0);
    auto texture = GetVideoTexture(&tex0)->texture; ExpectBlank();
    Publish(0xaa); GetVideoTexture(&tex0);
    assert(g_pending_texture_uploads.size() == 1 && g_pending_texture_uploads[0].rgba[0] == 0xaa);
    g_pending_texture_uploads.clear();
    // Repeated draws/arming during the same movie retain its picture.
    MioPan_VideoBegin(0x2bc0); GetVideoTexture(&tex0); assert(g_pending_texture_uploads.empty());
    MioPan_VideoEnd(); MioPan_VideoBegin(0x2bc0);
    assert(GetVideoTexture(&tex0)->texture == texture); ExpectBlank();
    GetVideoTexture(&tex0); assert(g_pending_texture_uploads.empty());
    // If a new picture is already ready, it is uploaded after the clear.
    MioPan_VideoEnd(); MioPan_VideoBegin(0x2bc0); Publish(0xbb); GetVideoTexture(&tex0);
    assert(g_pending_texture_uploads.size() == 2);
    assert(g_pending_texture_uploads[0].rgba[0] == 0 && g_pending_texture_uploads[1].rgba[0] == 0xbb);
    ReleaseFrameTexture(texture);
}
