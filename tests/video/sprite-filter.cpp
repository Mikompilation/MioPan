#include <cassert>
#include <cstdint>
#include <limits>
#include <vector>
using Uint32 = uint32_t;
struct SDL_GPUTexture {};
struct SpriteVertex { int value; };
enum DrawSource { DRAW_SOURCE_GENERIC, DRAW_SOURCE_WORLD };
enum GsDepthCompare { GS_DEPTH_GEQUAL };
constexpr int CAMERA_FOLLOW_NONE = 0;
struct DrawCommand {
    DrawSource source;
    SDL_GPUTexture *texture;
    int texture_width, texture_height;
    Uint32 first_vertex, vertex_count;
    bool depth_test, repeat_uv, vertex_fog, min_linear, mag_linear;
    bool preserve_original_aspect, clip_z_reversed;
    GsDepthCompare depth_compare;
};
static struct { int token; } g_mesh_stream;
static std::vector<SpriteVertex> g_vertices;
static std::vector<DrawCommand> g_draws;
static bool g_smooth_2d = true;
static void ApplyGsDrawEnv(DrawCommand &) {}
static void SetCameraFollow(DrawCommand &, int) {}
static void RecordDrawSourceMetrics(DrawCommand &) {}
#include "sprite-quad.inc"
int main()
{
    SDL_GPUTexture texture;
    SpriteVertex corners[4]{};
    for (bool enabled : {false, true})
    for (DrawSource source : {DRAW_SOURCE_GENERIC, DRAW_SOURCE_WORLD})
    for (bool minify : {false, true})
    for (bool magnify : {false, true})
    {
        g_smooth_2d = enabled;
        QueueQuad(&texture, 32, 32, corners, true, true, minify, magnify, source);
        const auto &draw = g_draws.back();
        const bool smooth = enabled && source == DRAW_SOURCE_GENERIC;
        assert(draw.min_linear == (smooth || minify));
        assert(draw.mag_linear == (smooth || magnify));
        assert(draw.repeat_uv && draw.vertex_count == 6);
    }
}
