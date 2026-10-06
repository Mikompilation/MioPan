#include "miopan_renderer.h"

#include "../gs/miopan_gs.h"
#include "../io/miopan_file.h"
#include "../io/miopan_input.h"
#include "../io/miopan_paths.h"
#include "../miopan_config.h"
#include "../miopan_profiler.h"
#include "../os/miopan_pacing.h"
#include "../miopan_ui/miopan_ui.h"
#include "miopan_video.h"

#include <SDL3/SDL.h>
#include <SDL3/SDL_gpu.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <functional>
#include <limits>
#include <memory>
#include <new>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace
{
constexpr int kLogicalWidth = 640;
constexpr int kLogicalHeight = 448;
constexpr int kFontTextureBankCount = 6;
/* Reserve ordinary and live mesh streams at renderer startup too.  The old
 * 4K-record colour/animated buffers were small enough that a room with more
 * visible geometry synchronously crossed a GPU-buffer high-water mark during
 * its first draw. */
constexpr Uint32 kInitialVertexBufferSize = 65536 * 12 * sizeof(float);
constexpr Uint32 kInitialMeshColourBufferSize = 65536 * sizeof(float) * 4;
constexpr Uint32 kInitialAnimatedMeshBufferSize =
    65536 * sizeof(float) * 8;
constexpr size_t kMeshCacheBudgetBytes = 128u * 1024u * 1024u;
constexpr size_t kMeshCacheEntryLimit = 4096;
constexpr size_t kMeshCacheMaxEntryBytes = 32u * 1024u * 1024u;
constexpr uint64_t kMeshCacheFailureRetryFrames = 120;
constexpr size_t kMeshCacheBuildBudgetBytes = 2u * 1024u * 1024u;
constexpr size_t kMeshCacheUploadBudgetBytes = 2u * 1024u * 1024u;
constexpr unsigned int kMeshCacheBuildBudgetEntries = 4;
constexpr unsigned int kMeshCacheUploadBudgetEntries = 4;
constexpr unsigned int kMeshArenaEvictionBudgetEntries = 8;
constexpr double kMeshCacheBuildBudgetMilliseconds = 1.0;
constexpr Uint32 kMeshArenaPageBytes = 8u * 1024u * 1024u;
/* Reserve the complete GPU arena at renderer startup.  Growing it on a cold
 * room's first draw would put SDL_CreateGPUBuffer back on the exact transition
 * path this cache is intended to smooth.  Static cache records are vertex
 * heavy (48-byte vertex versus at most 12 index bytes per strip vertex), while
 * every entry also retains an equal-sized CPU fallback copy.  Thus at most
 * half of the 128 MiB logical cache is GPU payload; a 64/32 MiB split covers
 * its worst supported vertex/index ratios plus fragmentation headroom. */
constexpr Uint32 kInitialMeshVertexArenaBytes = 64u * 1024u * 1024u;
constexpr Uint32 kInitialMeshIndexArenaBytes = 32u * 1024u * 1024u;
constexpr size_t kMeshArenaBudgetBytes = 128u * 1024u * 1024u;
constexpr Uint32 kMeshArenaAlignment = 16u;

struct TextureEntry
{
    SDL_GPUTexture *texture;
    int width;
    int height;
    uint64_t hash;
    bool ready;
};

struct PendingTextureUpload
{
    uint64_t hash;
    SDL_GPUTexture *texture;
    Uint32 width;
    Uint32 height;
    std::vector<unsigned char> rgba;
};

struct Tex0CacheEntry
{
    TextureEntry *texture;
    int gs_addr;
    int gs_size;
};

struct FontTextureEntry
{
    TextureEntry *texture;
    uint64_t tex0_value;
    int gs_addr;
    int gs_size;
    bool valid;
};

struct SpriteVertex
{
    float uv[4];
    float colour[4];
    float position[4];
};

struct MeshStaticVertex
{
    float uv[4];
    float position[4];
    float normal[4];
};

/* A resident mesh is drawn through the cached-static layout, so the public
 * vertex struct has to be this one byte for byte. */
static_assert(sizeof(MioPanResidentVertex) == sizeof(MeshStaticVertex) &&
                  offsetof(MioPanResidentVertex, uv) ==
                      offsetof(MeshStaticVertex, uv) &&
                  offsetof(MioPanResidentVertex, position) ==
                      offsetof(MeshStaticVertex, position) &&
                  offsetof(MioPanResidentVertex, normal) ==
                      offsetof(MeshStaticVertex, normal),
              "MioPanResidentVertex must match MeshStaticVertex");

/*
 * One resident mesh's GPU storage (see MioPan_RendererCreateResidentMesh()).
 *
 * Shared between g_resident_meshes and every draw queued against it, which is
 * the whole lifetime story: releasing a handle mid-frame leaves the queued
 * draws their own reference, the buffers go back to SDL when the last one
 * drops, and SDL in turn holds them past any submission still reading them.
 * That is why this needs none of the fence and pin machinery the old mesh
 * cache's shared arenas did -- nothing here is ever recycled in place.
 */
struct ResidentMesh
{
    SDL_GPUBuffer *vertex_buffer = nullptr;
    SDL_GPUBuffer *index_buffer = nullptr;
    Uint32 vertex_count = 0;
    Uint32 index_count = 0;
    size_t bytes = 0;

    ResidentMesh() = default;
    ResidentMesh(const ResidentMesh &) = delete;
    ResidentMesh &operator=(const ResidentMesh &) = delete;
    ~ResidentMesh();
};

using ResidentMeshPtr = std::shared_ptr<ResidentMesh>;

struct MeshAnimatedStaticVertex
{
    float uv[4];
};

struct MeshColour
{
    float rgba[4];
};

struct AnimatedMeshVertex
{
    float normal[4];
    float position[4];
};

enum MeshPipelineLayout
{
    MESH_PIPELINE_STREAMED = 0,
    MESH_PIPELINE_CACHED_STATIC,
    MESH_PIPELINE_CACHED_ANIMATED,
};

struct MeshCacheKey
{
    const void *owner;
    const void *vuvn;
    const void *mesh;
    unsigned int layout_kind;
    uint64_t owner_generation;

    bool operator==(const MeshCacheKey &other) const
    {
        return owner == other.owner && vuvn == other.vuvn &&
               mesh == other.mesh && layout_kind == other.layout_kind &&
               owner_generation == other.owner_generation;
    }
};

struct MeshCacheKeyHash
{
    size_t operator()(const MeshCacheKey &key) const
    {
        size_t h = std::hash<const void *>{}(key.owner);
        h ^= std::hash<const void *>{}(key.vuvn) + (h << 6) + (h >> 2);
        h ^= std::hash<const void *>{}(key.mesh) + (h << 6) + (h >> 2);
        h ^= std::hash<unsigned int>{}(key.layout_kind) +
             (h << 6) + (h >> 2);
        h ^= std::hash<uint64_t>{}(key.owner_generation) +
             (h << 6) + (h >> 2);
        return h;
    }
};

struct MeshCacheRetryExpiry
{
    MeshCacheKey key;
    uint64_t retry_frame;
};

struct MeshArenaRange
{
    Uint32 offset;
    Uint32 size;
};

struct MeshArenaPage
{
    SDL_GPUBuffer *buffer = nullptr;
    Uint32 size = 0;
    std::vector<MeshArenaRange> free_ranges;
};

struct MeshArena
{
    SDL_GPUBufferUsageFlags usage = 0;
    std::vector<std::unique_ptr<MeshArenaPage>> pages;
    size_t capacity_bytes = 0;
};

struct MeshArenaSlice
{
    MeshArenaPage *page = nullptr;
    Uint32 offset = 0;
    Uint32 size = 0;
};

struct MeshCacheEntry
{
    MeshCacheKey key{};
    SDL_GPUBuffer *vertex_buffer = nullptr;
    SDL_GPUBuffer *index_buffer = nullptr;
    Uint32 vertex_buffer_offset = 0;
    Uint32 index_buffer_offset = 0;
    MeshArenaSlice vertex_slice{};
    MeshArenaSlice index_slice{};
    Uint32 vertex_count = 0;
    Uint32 index_count = 0;
    Uint32 source_triangle_count = 0;
    Uint32 clipped_triangle_count = 0;
    size_t cached_bytes = 0;
    size_t resident_bytes = 0;
    uint64_t last_used_frame = 0;
    uint64_t upload_recorded_frame = 0;
    bool ready = false;
    std::vector<MeshStaticVertex> pending_vertices;
    std::vector<MeshAnimatedStaticVertex> pending_animated_vertices;
    std::vector<Uint32> pending_indices;
};

using MeshCacheEntryPtr = std::shared_ptr<MeshCacheEntry>;

struct InFlightMeshSubmission
{
    SDL_GPUFence *fence = nullptr;
    std::vector<MeshCacheEntryPtr> entries;
};

/*
 * The GS blend equation is (A - B) * C >> 7 + D, where A/B/D each select the
 * source colour, the destination colour or zero, and C selects source alpha,
 * destination alpha or the register's own FIX field.  That is 81 combinations,
 * of which the engine uses a handful and SDL_GPU can express a slightly larger
 * handful; GsBlendModeFromAlpha() classifies a register value into one of
 * these and falls back to GS_BLEND_ALPHA for anything else.
 */
/*
 * ZTST in the GS TEST register.  The values are the register's own, so the
 * enum can be indexed straight from it.
 */
enum GsDepthCompare
{
    GS_DEPTH_NEVER = 0,
    GS_DEPTH_ALWAYS = 1,
    GS_DEPTH_GEQUAL = 2,
    GS_DEPTH_GREATER = 3,
    GS_DEPTH_COMPARE_COUNT = 4
};

enum GsBlendMode
{
    GS_BLEND_ALPHA = 0,   /* Cs*As + Cd*(1-As)  -- the default, and the fallback */
    GS_BLEND_ADD,         /* Cs*As + Cd         -- additive, weighted by alpha   */
    GS_BLEND_ADD_ONE,     /* Cs + Cd            -- additive at full strength     */
    GS_BLEND_SUB,         /* Cs*As - Cd*As      -- subtractive                   */
    GS_BLEND_DST_DECAY,   /* Cd*(1-As)          -- darken the target only        */
    GS_BLEND_SRC_ONLY,    /* Cs*As              -- ignore the target             */
    GS_BLEND_DST_ADD,     /* Cd*As + Cd         -- brighten the target only; the
                           * source colour is unused by the GS, so the pipeline
                           * carries As in the colour channels (DST_COLOR/ONE)
                           * and DrawSolidQuad encodes it there                  */
    GS_BLEND_COUNT
};

/* The PS2 scissor box, in 640x448 framebuffer pixels.  SCAX1/SCAY1 are
 * inclusive on hardware. */
struct GsScissor
{
    int x0;
    int y0;
    int x1;
    int y1;
};

enum DrawSource
{
    DRAW_SOURCE_GENERIC = 0,
    DRAW_SOURCE_BILLBOARD,
    DRAW_SOURCE_SKY,
    DRAW_SOURCE_COUNT
};

struct DrawSourceFrameMetrics
{
    uint64_t commands = 0;
    uint64_t vertices = 0;
    uint64_t stream_bytes = 0;
    uint64_t compatible_joins = 0;
    uint64_t texture_lookups = 0;
    uint64_t texture_misses = 0;
};

struct DrawCommand
{
    DrawSource source;
    SDL_GPUTexture *texture;
    int texture_width;
    int texture_height;
    Uint32 first_vertex;
    Uint32 vertex_count;
    bool depth_test;
    /* ZTST, honoured only while depth_test is on.  Mesh draws leave it at the
     * engine's GEQUAL default; 2D primitives carry the GS register, which is
     * what makes the depth-buffer-as-stencil passes work. */
    GsDepthCompare depth_compare = GS_DEPTH_GEQUAL;
    /* Force the wrapping sampler on a 2D (depth-less) draw.  The GS picks its
     * address mode from the CLAMP register, not from the depth test, so a
     * screen-space primitive can still want REPEAT -- the sky dome in
     * MapSky.c walks its UVs across several copies of a 256x256 tile. */
    bool repeat_uv;
    bool min_linear;
    bool mag_linear;
    bool preserve_original_aspect;
    bool transform_mesh;
    /* A clip-space draw whose vertex z is already the host's reversed depth,
     * so sprite.vert must not run MikuPanFixClipZ() over it.  Raised by the
     * world-space bridges, which rebuild z from w with the meshes' own depth
     * row -- see ReversedDepthRow().  Reaches the shader as uClipZ.x. */
    bool clip_z_reversed = false;
    bool indexed_mesh;
    bool animated_mesh;
    bool fragment_lighting;
    Uint32 first_mesh_colour;
    Uint32 first_animated_mesh_vertex;
    MeshCacheEntryPtr cached_mesh;
    /* A resident mesh draw: indices [first_index, +index_count) of
     * `resident`, referencing vertices [resident_first_vertex,
     * +resident_vertex_count), whose colours start at g_mesh_colours
     * [first_mesh_colour].  Drawn through the cached-static layout.  Kept
     * apart from indexed_mesh, which belongs to the old cache and has its own
     * readiness and fallback rules. */
    bool resident_mesh = false;
    ResidentMeshPtr resident;
    Uint32 resident_first_vertex = 0;
    Uint32 resident_vertex_count = 0;
    Uint32 first_index = 0;
    Uint32 index_count = 0;
    float mvp[16];
    float model[16];
    /* GS alpha test, as the shader wants it: x = enable, y = reference in the
     * shader's 0..1 alpha space, z = ATST comparison, w unused. */
    float alpha_test[4];
    /* The rest of the GS draw environment, snapshotted when the draw was
     * queued.  Blend and depth-write are baked into the pipeline, so they pick
     * a variant; the scissor box is dynamic pass state. */
    GsBlendMode blend_mode;
    bool depth_write;
    GsScissor scissor;

    /* Index into g_fragment_light_states of the VU1 light image this draw's
     * fragment stage evaluates, and which of its three kernels it is
     * responsible for (config[2] there).  Only meaningful while
     * fragment_lighting is set.
     *
     * An index rather than a copy because the block is ~2.5 KB once the
     * positional lanes are widened past the VU1's three -- copying that into
     * every DrawCommand, and memcmp'ing it in the redundancy check, would cost
     * more than the lighting it describes.  Same reason, and the same shape, as
     * vertex_light_index below. */
    Uint32 fragment_light_index;

    Uint32 vertex_light_index;

    /*
     * Shadow pass tags.  A caster draw is the geometry gra3dShadow.c's
     * _RenderShadow() rendered into its off-screen target; here it is replayed
     * into the shadow map by RecordShadowMapPass() and skipped by the main
     * pass.  A receiver draw is the ROM's SRT_MAPSHADOW second pass over a
     * registered receiver block: same geometry as the room draw, replayed with
     * the projective receiver pipeline instead of its own material.
     */
    bool shadow_caster = false;
    bool shadow_receiver = false;

    /*
     * The viewfinder surround, queued by MioPan_RendererDrawFinderMask().  A
     * fullscreen quad sampling the scene capture taken immediately before it,
     * run through a pipeline of its own rather than a GetPipeline() variant --
     * its blend and depth state are fixed, so a key in that seven-dimensional
     * cache would only ever hold one entry.  Same reasoning, and the same
     * shape, as the two shadow pipelines.
     *
     * finder_mask[] and finder_mask2[] are this draw's copy of the two
     * uniforms the shader reads; they are per-draw rather than per-frame
     * because the aperture follows the camera's sway.
     */
    bool finder_mask = false;
    float finder_mask_rect[4];
    float finder_mask_params[4];
    /* Which cast shadow this draw belongs to -- the map it writes if it is a
     * caster, the projector it samples if it is a receiver.  -1 for the vast
     * majority of draws, which are neither. */
    int shadow_episode = -1;
};

static_assert(sizeof(MioPanLightState) == (3u + 12u + 9u * MIOPAN_VU1_MAX_LANES)
                                              * 16u,
              "light cbuffer layout must stay float4-aligned");
static_assert(sizeof(MioPanLightState) % 16u == 0,
              "light cbuffer must be a whole number of float4s");

struct MeshStreamState
{
    unsigned int token = 0;
    Uint32 first_vertex = 0;
    Uint32 max_vertices = 0;
    int submitted_triangles = 0;
    int clipped_triangles = 0;
    bool discard_all = false;
    DrawCommand command{};
};

struct ShaderFormatInfo
{
    SDL_GPUShaderFormat format;
    const char *dir;
    const char *extension;
    const char *entrypoint;
};

struct SpriteUniformBlock
{
    float model[16];
    float view[16];
    float projection[16];
    float mvp[16];
    float modelView[16];
    float viewProj[16];
    float shadowMatrix[16];
    float worldClipView[16];

    float normalMatrix[12];
    float viewNormalMatrix[12];

    float color[4];
    float fog[4];
    float fogColor[4];
    float shadowSize[4];
    float textureSize[4];
    float outputSize[4];
    float photoNegativeContentRect[4];
    float photoNegativeRect[4];
    float framebufferUvOffset[4];
    float framebufferUvScale[4];
    float framebufferContentUvMax[4];
    float renderSize[4];

    float params0[4];
    float crt0[4];
    float crt1[4];
    float crt2[4];
    float crt3[4];
    float params1[4];
    float ps2Feedback[4];
    float screenNegative[4];

    int flags0[4];
    int flags1[4];
    int flags2[4];
    int padFlags[4];

    float hdrOutput[4];

    /* Appended, so shader bytecode built before this field still reads every
     * member above it at the right offset.  Mirrors `uAlphaTest` in
     * resources/shaders/hlsl/mikupan_common.hlsli. */
    float alphaTest[4];

    /* x = MIOPAN_SHADOW_FILTER_*, y = one shadow-atlas texel in uShadowSize's
     * UV space.  Mirrors `uShadowFilter`; appended for the same reason. */
    float shadowFilter[4];

    /* The viewfinder surround.  xy = aperture centre, zw = half size, in the
     * ROM's 640x448 frame units; then feather, darkening, and the defocus
     * radius as a fraction of the target's width and height.  Mirror
     * `uFinderMask` / `uFinderMask2`; appended for the same reason. */
    float finderMask[4];
    float finderMask2[4];

    /* x = cut-out sharpening strength.  Mirrors `uAlphaSharpen`; appended for
     * the same reason as the fields above it. */
    float alphaSharpen[4];

    /* x = 1 when the draw's clip z is already reversed (DrawCommand::
     * clip_z_reversed).  Mirrors `uClipZ`; appended for the same reason. */
    float clipZ[4];
};

static const ShaderFormatInfo kShaderFormats[] = {
    {SDL_GPU_SHADERFORMAT_SPIRV, "spirv", ".spv", "main"},
    {SDL_GPU_SHADERFORMAT_DXIL, "dxil", ".dxil", "main"},
    {SDL_GPU_SHADERFORMAT_MSL, "msl", ".msl", "main0"},
};

SDL_Window *g_window;
SDL_GPUDevice *g_device;

/* ------------------------------------------------------------------------
 *  Multisampling.
 *
 *  Sample counts are stored as the plain integer the player chose -- 1, 2, 4
 *  or 8 -- and turned into SDL's enum and into a cache slot by the two helpers
 *  below.  Four slots is the whole range SDL_GPU offers.
 * --------------------------------------------------------------------- */
constexpr int kSampleSlotCount = 4;          /* 1x, 2x, 4x, 8x */
constexpr int kMaxSampleCount = 8;

int SampleSlotForCount(int samples)
{
    switch (samples)
    {
    case 8:
        return 3;
    case 4:
        return 2;
    case 2:
        return 1;
    default:
        return 0;
    }
}

int SampleCountForSlot(int slot)
{
    return 1 << slot;
}

SDL_GPUSampleCount SdlSampleCount(int samples)
{
    switch (samples)
    {
    case 8:
        return SDL_GPU_SAMPLECOUNT_8;
    case 4:
        return SDL_GPU_SAMPLECOUNT_4;
    case 2:
        return SDL_GPU_SAMPLECOUNT_2;
    default:
        return SDL_GPU_SAMPLECOUNT_1;
    }
}

/*
 * Every pipeline variant, indexed by
 *   [sample count][vertex layout][object-space mesh][depth test][depth write]
 *   [wireframe][fragment spot lighting][blend].
 *
 * Layout picks the vertex streams: STREAMED is one interleaved buffer, while
 * the indexed preset and animated layouts bind an immutable stream in slot 0
 * and the frame's per-vertex data in slot 1.  "Object-space mesh" selects the
 * transforming vertex shader, so clip-space debug primitives can request depth
 * testing without running through it.
 *
 * Blend and depth-write come from the GS ALPHA and ZBUF registers, which the
 * game rewrites constantly, so the product is far too large to build up front.
 * GetPipeline() fills slots on demand and never releases one, which is what
 * makes this safe to call while a command buffer is still in flight -- the
 * pointer a recorded buffer holds stays valid for the process's lifetime.
 * CreatePipelines() still builds the default-blend set at init so a broken
 * shader fails startup rather than a frame halfway into the game.
 *
 * The sample count is the outermost index for exactly that reason.  A pipeline
 * is built against one sample count and cannot render into a target with
 * another, so changing the MSAA setting needs a different set -- and because
 * nothing here is ever released, the old set simply stays where it is and any
 * command buffer still holding one of its pointers stays valid.  The cost of
 * keeping all four slots is 4 x 2688 pointers, ~86 KB, which is cheaper than
 * the alternative of waiting for the GPU to go idle mid-session.
 */
constexpr int kPipelineLayoutCount = 3;
SDL_GPUGraphicsPipeline
    *g_pipeline_cache[kSampleSlotCount][kPipelineLayoutCount][2][2]
                     [GS_DEPTH_COMPARE_COUNT][2][2][2][GS_BLEND_COUNT];
bool g_pipeline_failed[kSampleSlotCount][kPipelineLayoutCount][2][2]
                      [GS_DEPTH_COMPARE_COUNT][2][2][2][GS_BLEND_COUNT];

struct CachedShaderInfo
{
    const char *name;
    SDL_GPUShaderStage stage;
    Uint32 sampler_count;
    SDL_GPUShader *shader;
};

/* Pipeline state varies far more often than shader code.  Keep the seven
 * shader objects shared by every pipeline variant instead of rereading their
 * bytecode and recreating two shader modules on every cold material. */
CachedShaderInfo g_cached_shaders[] = {
    {"sprite.vert", SDL_GPU_SHADERSTAGE_VERTEX, 0, nullptr},
    {"mesh.vert", SDL_GPU_SHADERSTAGE_VERTEX, 0, nullptr},
    {"mesh_spot_streamed.vert", SDL_GPU_SHADERSTAGE_VERTEX, 0, nullptr},
    {"mesh_spot.vert", SDL_GPU_SHADERSTAGE_VERTEX, 0, nullptr},
    {"mesh_animated_lit.vert", SDL_GPU_SHADERSTAGE_VERTEX, 0, nullptr},
    {"sprite.frag", SDL_GPU_SHADERSTAGE_FRAGMENT, 2, nullptr},
    {"mesh_spot.frag", SDL_GPU_SHADERSTAGE_FRAGMENT, 2, nullptr},
    /* Projected shadows.  Both fragment stages keep two samplers declared --
     * shadercross is deliberately run without --cull, so uTexture stays in the
     * signature even where only uAuxTexture is read. */
    {"shadow_silhouette.vert", SDL_GPU_SHADERSTAGE_VERTEX, 0, nullptr},
    {"shadow_silhouette.frag", SDL_GPU_SHADERSTAGE_FRAGMENT, 2, nullptr},
    {"shadow_receiver.vert", SDL_GPU_SHADERSTAGE_VERTEX, 0, nullptr},
    {"shadow_receiver.frag", SDL_GPU_SHADERSTAGE_FRAGMENT, 2, nullptr},
    /* The viewfinder surround.  Two samplers for the same reason the shadow
     * pair has them: it reads only uTexture, but shadercross runs without
     * --cull so uAuxTexture stays in the signature and the count has to match
     * what the pipeline declares. */
    {"finder_mask.frag", SDL_GPU_SHADERSTAGE_FRAGMENT, 2, nullptr},
};

void ReleaseCachedShaders();
void ReleaseDepthProbeResources();
void ReleasePresentResources();

/* Debug view state, controlled by the host renderer menu.
 *   wireframe -- edges only: shows geometry that is present but shaded away
 *   nodepth   -- ignore the depth buffer: shows geometry hidden behind
 *                something drawn in front of it; this diagnostic path also
 *                uses an unculled depth-off pipeline. */
bool g_dbg_wireframe;
bool g_dbg_nodepth;
bool g_dbg_disable_billboard_host;
bool g_dbg_skip_billboard_legacy_packets;
bool g_dbg_disable_sky_dome;
bool g_dbg_disable_sky_horizon;

/* Flat colour swaps the model texture for the 1x1 white texture and forces
 * alpha to 1, so a transparent or wrongly-bound texture cannot hide
 * geometry that is otherwise being submitted correctly.  Vertex colour is
 * replaced per block in miopan_graph3d.cpp so each part reads distinctly. */
extern "C" bool g_dbg_flatcolor;   /* defined in miopan_graph3d.cpp */
/* Disable lighting submits the realtime path's vertices flat white instead of
 * lighting them, which is what the port did before per-vertex lighting
 * existed.  Tells "this model is unlit" apart from "this model is not reaching
 * the renderer". */
extern "C" bool g_dbg_nolighting;  /* defined in miopan_graph3d.cpp */
/*
 * Indexed by [anisotropy][repeat][linear minification][linear magnification].
 * The PS2 keeps minification and magnification filtering separate in TEX1, so
 * one global nearest sampler cannot reproduce its low-resolution textures.
 *
 * Anisotropy is the outermost index for the same reason the sample count is
 * outermost on the pipeline cache: a recorded command buffer holds sampler
 * pointers, so a live setting change has to add a set rather than replace one.
 * Slot 0 is always plain (anisotropy off) and is what every fixed internal
 * binding uses -- the 1x1 white texture, the shadow atlas, and the present
 * pass's 1:1 read of the finished frame, none of which is a minified surface.
 * Only material textures go through the anisotropic set.
 */
constexpr int kAnisoSlotCount = 5;           /* 1x, 2x, 4x, 8x, 16x */
constexpr int kMaxAnisotropy = 16;
SDL_GPUSampler *g_samplers[kAnisoSlotCount][2][2][2];
SDL_GPUTexture *g_white_texture;
SDL_GPUTexture *g_depth_texture;
SDL_GPUTexture *g_pause_capture_texture;

/*
 * The multisampled colour target.
 *
 * MSAA is applied to the frame the game is rasterised into and to nothing
 * else: this is the only multisampled colour texture in the renderer, and it
 * is resolved into the ordinary scene target (or straight into the swapchain
 * when the frame is rendered direct) at the end of every pass segment.  The
 * shadow atlas, the GS capture slots, the pause still, the screen mirror, the
 * composited output and the host UI all stay single-sampled, so nothing
 * downstream of the resolve has to know MSAA exists.
 *
 * Sized like the depth buffer -- to the internal render resolution, not the
 * window -- so it costs samples x render pixels of colour and the same again
 * of depth.  4x at 4K is about 500 MB of attachment memory between the two,
 * which is the number to have in mind before choosing 8x.
 */
SDL_GPUTexture *g_msaa_color_texture;
Uint32 g_msaa_width;
Uint32 g_msaa_height;
int g_msaa_texture_samples = 1;
/* The sample count the depth buffer was built at, so a change in the setting
 * reallocates it the way a change in size does. */
int g_depth_samples = 1;

/* What the player asked for, what the device granted, and what this frame's
 * pipelines are keyed on.  The three differ while a request is unsupported or
 * while a target has failed to allocate; GetPipeline() reads the third, which
 * RecordFrame settles before the first draw is replayed. */
int g_msaa_request = 1;
int g_msaa_active = 1;
int g_msaa_max_supported = 1;
int g_pipeline_sample_count = 1;

/* Anisotropic filtering on material textures.  1 is off.
 *
 * SDL_GPU has no query for the maximum, and 16 is the floor every desktop
 * Vulkan and D3D12 implementation guarantees, so this starts optimistic and is
 * lowered to 1 by CreateSamplerSet() if a device turns out to have been built
 * without samplerAnisotropy. */
int g_anisotropy_request = 1;
int g_anisotropy_active = 1;
int g_anisotropy_max_supported = kMaxAnisotropy;

/*
 * The scene target -- everything the game draws, at the internal render
 * resolution rather than the window's.
 *
 * Null in MATCH_WINDOW mode, where the swapchain texture is the scene target
 * and no resample happens at all; that keeps the default path byte-identical
 * to what it was before the split.  In the other modes the whole frame is
 * rasterised here and blitted to the swapchain after the last game draw, which
 * is also what puts the host UI pass outside the resample -- the debug overlay
 * stays crisp over a chunky picture.
 *
 * Everything downstream of a frame reads this rather than the swapchain: the
 * GS capture slots an effect samples, the pause still, and the 640x448 screen
 * mirror a photograph is read back out of.  They were already parameterised on
 * a source texture and its size, so the split reaches them by argument.
 */
SDL_GPUTexture *g_scene_texture;
Uint32 g_scene_width;
Uint32 g_scene_height;

/*
 * Projected shadows.  gra3dShadow.c is a two-pass renderer -- draw the caster
 * into an off-screen target from the light's point of view, then project that
 * texture back over the receivers -- and the second pass is VU1 microcode
 * (SgSuShadow_dma_main, vu1/ff2_03.vsm) the host has no interpreter for.  This
 * is the host stand-in for both passes.
 *
 * The ROM's target is 8-bit and lives in the frame buffer's alpha byte
 * (PSMT8H); here it is a small colour target in the swapchain format, so the
 * existing pipeline plumbing applies unchanged and the receiver shader reads
 * the silhouette out of .r.
 */
SDL_GPUTexture *g_shadow_texture;
/* The caster draws into the atlas, which is never multisampled, so its
 * pipelines are built once at 1x.  The receiver draws into the frame, so it
 * needs one set per sample count -- same shape and same reasoning as the main
 * pipeline cache. */
SDL_GPUGraphicsPipeline *g_shadow_caster_pipeline[kPipelineLayoutCount];
SDL_GPUGraphicsPipeline
    *g_shadow_receiver_pipeline[kSampleSlotCount][kPipelineLayoutCount];
bool g_shadow_receiver_failed[kSampleSlotCount];

/* The viewfinder surround's pipeline.  Built the first time finder mode asks
 * for it rather than at startup: most of a session never raises the camera,
 * and a player who leaves the effect off never pays for it at all.  Per sample
 * count, because it draws into the frame. */
SDL_GPUGraphicsPipeline *g_finder_mask_pipeline[kSampleSlotCount];
bool g_finder_mask_pipeline_failed[kSampleSlotCount];

/*
 * The viewfinder surround's settings.  `blur` and `darken` are 0..1 knobs
 * rather than physical quantities so the two ends of each are meaningful on
 * their own: darken 1 with no blur is a plain black surround, blur 1 with a
 * little darkening is the defocused one, and both together sit in between.
 *
 * `scale` resizes the aperture around the rectangle the game itself hands
 * over.  The default of 1 is that rectangle exactly -- photo_frame_tbl, the
 * 436x300 box FrameInsideChk() decides a shot against -- which is the only
 * size with any claim to being correct; it is adjustable because the
 * viewfinder art around it is a soft painted edge rather than a hard border,
 * so where the surround should begin is a matter of taste.
 */
bool  g_finder_mask_enabled = true;
float g_finder_mask_blur = 0.75f;
float g_finder_mask_darken = 0.45f;
float g_finder_mask_tint = 0.55f;
float g_finder_mask_scale = 1.0f;

/*
 * Crimson, for the Crimson Butterfly.
 *
 * A multiplier, never an addition, so the tint can only ever take light away:
 * red is left alone and the other two channels are pulled down, which casts
 * the surround without any chance of clipping however bright it started.  The
 * same shape MikuPan's lens tint has -- its (0.72, 0.67, 0.82) is that port's
 * violet -- and it travels in the quad's vertex colour rather than a uniform of
 * its own, because uColor.rgb was otherwise sitting there unused at white.
 */
const float kFinderMaskCrimson[3] = {1.0f, 0.38f, 0.42f};

/* The film grain, read by effect_scr.c's two dither draws.  It lives here
 * rather than in effect.o's own eff_wrk because it is a host setting the
 * player owns, and the ROM's dith_off beside it is the game's -- keeping them
 * apart means a future event script that raises dith_off still works, and
 * neither one can be surprised by the other. */
int g_film_grain_mode = MIOPAN_FILM_GRAIN_NATIVE;

/*
 * The native-resolution grain sheet.
 *
 * Square, and sized to the output rounded up to a power of two.  It wraps --
 * the grain draws are the one place in the tree that asks the GS for REPEAT,
 * and white noise tiles with no seam -- so a sheet smaller than the screen is
 * correct, just periodic; the size decides how long the field runs before it
 * repeats, not whether the grain is the right size.  Covering the output
 * outright is the point at which it stops repeating at all.
 *
 * Powers of two are what make it affordable to track the window: a resize drag
 * from 640 to 2048 crosses two boundaries rather than rebuilding every frame.
 * The ceiling costs 16 MB and 7 ms to fill (measured), and buys 1080p and
 * everything under it a field that never repeats; 4K gets two tiles across.
 */
constexpr int kFilmGrainSheetMin = 512;
constexpr int kFilmGrainSheetMax = 2048;

SDL_GPUTexture *g_film_grain_sheet;
int   g_film_grain_sheet_size;
int   g_film_grain_sheet_alpmx = -1;
int   g_film_grain_sheet_colmx = -1;
bool  g_film_grain_sheet_failed;

/* Set between MioPan_RendererFilmGrainBegin() and ...End(), with the effective
 * texture size the quad's UVs are normalised against while it is. */
bool  g_film_grain_active;
float g_film_grain_tex_w = 1.0f;
float g_film_grain_tex_h = 1.0f;

/* The Gaussian's half-window, in frame units, at blur 1.  The kernel is
 * truncated at two sigma (see finder_mask.frag.hlsl), so the sigma this really
 * buys is half of it -- 16 frame units, which on a 640x448 picture is already a
 * very soft blur.  MikuPan's equivalent reaches 36 texels with a sigma equal to
 * its whole window, i.e. nearly a box; this is the same reach shaped as an
 * actual Gaussian. */
const float kFinderMaskBlurRadius = 32.0f;
/*
 * How far the frame line is feathered, as a fraction of the clear rectangle's
 * shorter half axis -- about 16 frame units at the default scale.
 *
 * Sized against how little room there is to work in rather than by eye: the
 * band outside the frame is only ~59 units wide at 16:10 and ~69 at 16:9, so a
 * feather much wider than this would leave most of the surround still ramping
 * up and never reaching the look it was set to.  Narrower than this and the
 * frame line reads as a hard seam, which is worse -- the point is to stop a
 * wide window re-framing the shot, not to draw a box around it.
 */
const float kFinderMaskFeather = 0.07f;

/*
 * Several shadows are cast per frame and each has its own map and projector:
 * the player and the sister always, and one per object block whenever the
 * flashlight lights a room (gra3dDrawSGDShadowEveryObject).  The ROM reuses one
 * render target because it renders and consumes each shadow before setting up
 * the next; the host queues the whole frame and replays it later, so the maps
 * have to coexist.  They share one texture as a grid of tiles -- an atlas --
 * which keeps the whole thing to a single extra render pass.
 *
 * Shadows past the last tile are dropped rather than aliased onto an occupied
 * one: a missing shadow is a far better failure than one object wearing
 * another's silhouette.
 */
/* 512 rather than the 256 this started at.  The atlas is 16 tiles, so this is
 * a 2048x2048 target instead of 1024x1024 -- a few MB, against a silhouette
 * edge that is projected across a large part of the screen and was the
 * coarsest thing in the shadow path.  Raising it is independent of
 * MIOPAN_SHADOW_FILTER_SOFT and the two compose: more texels to soften. */
constexpr Uint32 kShadowTileSize = 512;
constexpr Uint32 kShadowAtlasTiles = 4;   /* 4x4 tiles */
constexpr Uint32 kShadowAtlasSize = kShadowTileSize * kShadowAtlasTiles;
constexpr int kShadowMaxEpisodes =
    (int)(kShadowAtlasTiles * kShadowAtlasTiles);

/* One cast shadow: the caster draws that build its map, the projector that
 * samples it, and the tile it lives in. */
struct ShadowEpisode
{
    size_t first_caster_draw;
    size_t last_caster_draw;
    /* World -> light clip, snapshotted from the shadow camera _RenderShadow()
     * applied.  Plays the part s_matIP does on VU1. */
    float matrix[16];
    /* _CalcColor()'s projector alpha: the light's diffuse luminance at the
     * shadow target, capped at half. */
    float strength;
    bool have_casters;
};
std::vector<ShadowEpisode> g_shadow_episodes;

/* Non-zero while gra3dShadow.c is inside _RenderShadow()/_DrawShadow(), so
 * MioPan_RendererBeginMeshStream() can tag what it queues.  Counted rather
 * than flagged because a caster is drawn through the ordinary SGD walker,
 * which the shadow code re-enters. */
int g_shadow_caster_depth;
int g_shadow_receiver_depth;
/* The episode currently being built, or -1 between shadows. */
int g_shadow_current_episode = -1;
/* At least one episode ended up with a usable map this frame. */
bool g_shadow_valid;
/* Debug view: paint receivers by what the projector resolves to, instead
 * of blending a shadow. */
bool g_dbg_shadow_view;
/* Per-frame tallies, for the debug readout.  These answer the first
 * question a missing shadow raises -- whether gra3dShadow.c is being
 * entered at all -- without a printf probe. */
int g_shadow_stat_casters;
int g_shadow_stat_receivers;
/* The game camera, saved across a shadow the way gra3dshadowDrawSGD() saves
 * camOrigin.  _gra3dSetCameraForce() puts the ROM's copy back by assigning the
 * struct, which never reaches MioPan_Graph3dApplyCamera() -- so without this
 * the receiver pass would still be drawing from the light's viewpoint. */
float g_shadow_saved_view[16];
float g_shadow_saved_projection[16];
bool g_shadow_saved_camera_valid;
SDL_GPUBuffer *g_vertex_buffer;
Uint32 g_vertex_buffer_size;
SDL_GPUBuffer *g_mesh_colour_buffer;
Uint32 g_mesh_colour_buffer_size;
SDL_GPUBuffer *g_animated_mesh_buffer;
Uint32 g_animated_mesh_buffer_size;
Uint32 g_depth_width;
Uint32 g_depth_height;
Uint32 g_pause_capture_width;
Uint32 g_pause_capture_height;
unsigned int g_pause_capture_addr;
bool g_pause_capture_valid;
bool g_pause_capture_pending;

/*
 * The screen mirror -- the last presented frame, kept at the PS2's own
 * 640x448, for the paths that read the frame buffer back into EE memory
 * (g2d_draw.c's LocalCopy*toB family, and through it photo.c's picture
 * capture and SpriteCmn.c's screen snapshot).
 *
 * A capture slot cannot serve those: it lives on the GPU and is filled during
 * the present, while the game reads its EE buffer immediately -- PictureCapture
 * compresses the picture in the same call that asks for it.  Emulated GS memory
 * cannot either: nothing ever draws into it, which is what made a saved
 * photograph a page of unrelated VRAM.
 *
 * The ROM reads the *displayed* page (`(count + 1) & 1`), so "the frame that
 * was last presented" is exactly what it is asking for, and no mid-frame
 * synchronisation is needed.  Kept at 640x448 rather than at the output's size:
 * every read-back is 640x448 or smaller, so the downscaling blit is what makes
 * the cost independent of the window.  It holds the ROM's own frame, not the
 * whole window -- see RecordScreenMirror().
 */
SDL_GPUTexture *g_screen_mirror_texture;
Uint32 g_screen_mirror_width;
Uint32 g_screen_mirror_height;
bool g_screen_mirror_valid;
SDL_GPUTransferBuffer *g_screen_mirror_download;
Uint32 g_screen_mirror_download_size;
/*
 * GS capture slots.
 *
 * A slot stands in for one page of GS memory that the game fills by copying
 * the frame buffer into it and later samples as a texture.  `addr` is the GS
 * block address the game uses (0x2bc0, 0x3aa0, 0x3480, or one of the two EE
 * staging buffers for the round-trip in mechanism C); `logical_w/h` is the size
 * the PS2 copy produced, which is what the effect's UVs are written against.
 *
 * The texture is allocated at the output's scale times logical/640x448, so a
 * half-width PS2 copy really is half-width here and blurs the same way when the
 * effect stretches it back over the screen.
 */
struct CaptureSlot
{
    unsigned int addr;
    SDL_GPUTexture *texture;
    Uint32 tex_w;
    Uint32 tex_h;
    int logical_w;
    int logical_h;
    bool valid;         /* something has been captured into it this session */
};

/* The live frame as drawn so far -- mechanism A's source, and the source of
 * every capture.  Not a GS address the game uses, so it cannot collide. */
const unsigned int kCaptureAddrScene = 0xFFFFFFFEu;
/* "Copy from the colour target" rather than from another slot. */
const unsigned int kCaptureSrcLive = 0xFFFFFFFFu;

std::vector<CaptureSlot> g_capture_slots;

/* One requested capture, in draw order: before replaying g_draws[draw_index],
 * copy `src_slot` -- or the live colour target, when it is -1 -- into
 * `dst_slot`.  Slot *indices*, not pointers: declaring a slot can reallocate
 * g_capture_slots and invalidate every pointer into it. */
struct CapturePoint
{
    size_t draw_index;
    int dst_slot;
    int src_slot;
};

std::vector<CapturePoint> g_capture_points;

/* Raised the first time a draw asks to sample the frame buffer, and never
 * lowered.  The texture is a full output-sized target, so it is allocated on
 * demand rather than for every session -- most of the game never draws a
 * screen filter.  Costs the very first such draw one frame. */
bool g_scene_capture_requested;
/* Frame-buffer sampling draws seen this frame, for the profiler/HUD. */
int g_scene_capture_count;
SDL_GPUTextureFormat g_swapchain_format;
/*
 * The format the game itself is rasterised in -- every pipeline's colour
 * target, the scene texture, the GS capture slots, the pause still and the
 * shadow atlas.
 *
 * Split from the swapchain's format because HDR changes the swapchain and must
 * not change the game.  The pipelines are built against this once, at startup,
 * so it has to stay fixed for the life of the renderer; the swapchain
 * composition on the other hand is re-negotiated freely at runtime, and the
 * present pass bridges whatever gap that opens.
 *
 * In an SDR session it is simply the SDR swapchain format, which lets the
 * frame be drawn straight into the swapchain and costs nothing.  With HDR
 * asked for at startup it is R10G10B10A2_UNORM instead: still a UNORM target,
 * so every blend equation saturates at white exactly as an 8-bit one did and
 * the picture is unchanged, but with four times the precision -- which is what
 * keeps this game's stack of full-screen blend passes from banding once the
 * present pass stretches the result into a display's headroom.
 */
SDL_GPUTextureFormat g_scene_format = SDL_GPU_TEXTUREFORMAT_INVALID;
/* The composited output the present pass reads: bars, the resampled scene and
 * the host UI, at swapchain size.  Equal to g_scene_format, and separate only
 * because ImGui is initialised against it and cannot be re-initialised when
 * the swapchain composition changes underneath. */
SDL_GPUTextureFormat g_output_format = SDL_GPU_TEXTUREFORMAT_INVALID;
/* The 640x448 mirror a photograph is read back out of.  Pinned to 8-bit RGBA
 * rather than following the scene: MioPan_RendererReadScreen() walks the
 * download as four bytes per pixel in R,G,B,A order, which R10G10B10A2 -- the
 * same four bytes, packed -- would silently scramble. */
const SDL_GPUTextureFormat kScreenMirrorFormat =
    SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
SDL_GPUTextureFormat g_depth_format = SDL_GPU_TEXTUREFORMAT_INVALID;
bool g_frame_active;
/*
 * What a presented frame needs out of the frame's upload stage.
 *
 * A repeated present -- the same draw list re-recorded through a reprojected
 * camera -- reuses the buffers the upload stage already filled instead of
 * filling them a second time, so its readiness flags have to outlive the upload
 * locals.  `valid` is what says an upload stage has run for the frame currently
 * queued; MioPan_RendererBeginFrame() clears it along with the draw list.
 */
struct FrameUploadState
{
    bool valid;
    bool vertices_uploaded;
    bool colours_ready;
    bool animated_vertices_ready;
};

FrameUploadState g_frame_upload;
/*
 * Frame smoothing -- menu bar, Profiler > Frame smoothing.
 *
 * Additional presents of each logical frame, 0..3.  The game keeps simulating
 * at its own rate; these are the same frame put on screen again through a
 * camera interpolated toward it from the previous one.  With interpolation off
 * they are identity copies instead, which is the check that the renderer
 * survives a second record pass at all -- a separate question from what camera
 * an in-between should use.
 */
int g_extra_presents;
bool g_interpolate_presents = true;
/*
 * How far the eye travelled between the previous logical frame's camera and
 * this one's, in world units; negative when there was no camera pair to
 * measure.
 *
 * This is the answer to "smoothing is on and I cannot see any difference".
 * Most of this game is fixed camera angles, and a camera that did not move
 * makes blend(N-1, N, t) equal to N for every t -- so every in-between is
 * byte-identical to the real frame and there is, correctly, nothing to see.
 * Zero here means working-as-intended, not broken; it is finder mode and
 * camera pans that have something to interpolate.
 */
float g_camera_motion = -1.0f;
/*
 * Geometry smoothing -- the second half of an in-between frame.
 *
 * A reprojected camera moves the eye; it does not move anything the eye is
 * looking at, so with the camera smoothed the remaining 30 Hz cue is whatever
 * the world itself was doing.  This blends that too, and it splits in two
 * because the engine puts an object's motion in one of two places:
 *
 *  - A rigidly bound block carries its motion in `model`, the bone or object
 *    coordinate gra3dSGD.c's GetHostRuntimeMeshTransform() hands over, and its
 *    vertices are constant in that space.  Blending the MATRIX moves it, and
 *    costs nothing beyond the mvp product the camera reprojection already does.
 *  - A skinned block has already had its bone pair folded into every vertex by
 *    CalcVertexBuffer(), so it arrives in world space with an identity `model`.
 *    Nothing but the VERTICES can move it, and moving them means re-uploading
 *    the streamed vertex buffer for each in-between.
 *
 * Both need to know that draw k of the previous logical frame is the same
 * geometry as draw k of this one, which the draw list does not say -- it is
 * rebuilt from scratch every frame.  g_geo_prev_draws is last frame's identity
 * list and g_geo_match_count the length of the prefix that still corresponds;
 * everything past the first mismatch keeps this frame's own data, which is
 * exactly increment 3's behaviour.  Room and characters are built before
 * effects, so the churn that ends the prefix is usually behind them.
 */
bool g_interpolate_geometry = true;

/* Identity of one draw, for frame-to-frame correspondence.  It has to be
 * strong enough that two different objects cannot alias -- blending one
 * object's vertices toward another's would be spectacular -- and `first_vertex`
 * is what makes that so: any insertion, removal or size change shifts it for
 * every draw after it, so the match ends there rather than sliding. */
struct GeometryDrawKey
{
    /* A resident draw has no span of g_vertices; it keys on its index span
     * and on the mesh it belongs to instead, which is the same guarantee --
     * a different object cannot present the same pair. */
    Uint32 first_vertex;
    Uint32 vertex_count;
    const void *texture;
    const void *mesh;
    unsigned int flags;

    bool operator==(const GeometryDrawKey &other) const
    {
        return first_vertex == other.first_vertex &&
               vertex_count == other.vertex_count &&
               texture == other.texture && mesh == other.mesh &&
               flags == other.flags;
    }
};

struct GeometrySnapshotDraw
{
    GeometryDrawKey key;
    float model[16];
};

/* The previous logical frame, and the one being presented.  Swapped at the end
 * of every frame that produced a usable snapshot. */
std::vector<GeometrySnapshotDraw> g_geo_prev_draws;
std::vector<GeometrySnapshotDraw> g_geo_curr_draws;
/* Four floats per vertex, indexed the same way g_vertices is.  The current
 * frame's copy exists because the blend writes into g_vertices in place: the
 * present that lands on the simulation tick restores the true positions out of
 * here rather than trusting `a + (b - a) * 1.0f` to be exactly `b`. */
std::vector<float> g_geo_prev_positions;
std::vector<float> g_geo_curr_positions;
/* Draws [0, g_geo_match_count) correspond to the previous frame's. */
size_t g_geo_match_count;
/* This frame's snapshot is complete and may become the next frame's previous. */
bool g_geo_snapshot_valid;
/* Vertex blending specifically is safe -- there is a previous position buffer
 * and nothing has invalidated the vertex indices since it was taken. */
bool g_geo_vertices_valid;
/*
 * Share of the frame's 3D draws that actually blend, or negative when geometry
 * smoothing had nothing to work with.  Same job `g_camera_motion` does for the
 * camera half: it is what distinguishes "off", "on with nothing to show" and
 * "on and working" without setting up a test.
 */
float g_geometry_coverage = -1.0f;
bool g_renderer_initialized;
bool g_mesh_cache_available;
int g_lighting_mode = MIOPAN_LIGHTING_VERTEX;
int g_shadow_filter = MIOPAN_SHADOW_FILTER_SOFT;
bool g_lighting_mode_initialized;
int g_animated_lighting_backend = MIOPAN_ANIMATED_LIGHTING_GPU;
/*
 * Presented frames per logical frame.  1 is the only value the game ever asks
 * for: the 30 Hz cadence is the game loop's own two-field V-blank wait
 * (SYSTEM_VBLANK_WAIT_NUM), and both InitVBlank() and SetVBlankWaitNum() call
 * MioPan_RendererSetPresentInterval(1) so the renderer does not divide a second
 * time.  It used to default to 2, from when the renderer owned that division --
 * which no longer described the boot state, since the first of those calls lands
 * before any frame runs.
 */
int g_present_interval = 1;
int g_present_counter;
Uint64 g_fps_window_start_counter;
int g_fps_game_frames;
int g_fps_present_frames;
int g_texture_l1_lookups;
int g_texture_l1_hits;
int g_texture_l2_hits;
int g_texture_downloads;
int g_texture_creates;
int g_texture_invalidations;
int g_font_texture_hits;
int g_font_texture_selects;
int g_font_texture_creates;
int g_font_texture_invalidations;
int g_mesh_triangles_submitted;
int g_mesh_triangles_clipped;
int g_mesh_cache_hits;
int g_mesh_cache_misses;
int g_mesh_cache_creates;
int g_mesh_cache_evictions;
int g_mesh_cache_invalidations;
int g_mesh_cache_upload_failures;
int g_mesh_cache_build_deferred;
int g_mesh_cache_upload_deferred;
int g_mesh_cache_promotions;
uint64_t g_mesh_cache_upload_bytes;
uint64_t g_mesh_colour_upload_bytes;
uint64_t g_animated_mesh_upload_bytes;
uint64_t g_mesh_expanded_vertices_avoided;
uint64_t g_animated_mesh_vertices;
uint64_t g_animated_mesh_expanded_vertices_avoided;
uint64_t g_mesh_direct_stream_vertices;
size_t g_mesh_cache_bytes;
uint64_t g_mesh_cache_frame;
unsigned int g_mesh_cache_build_entries;
size_t g_mesh_cache_build_bytes;
Uint64 g_mesh_cache_build_start;

std::unordered_map<uint64_t, TextureEntry> g_texture_cache;
std::unordered_map<uint64_t, Tex0CacheEntry> g_tex0_cache;
std::vector<PendingTextureUpload> g_pending_texture_uploads;
std::unordered_map<MeshCacheKey, MeshCacheEntryPtr, MeshCacheKeyHash>
    g_mesh_cache;
std::unordered_map<MeshCacheKey, uint64_t, MeshCacheKeyHash>
    g_mesh_cache_retry_after;
std::deque<MeshCacheRetryExpiry> g_mesh_cache_retry_expiries;
std::unordered_map<MeshCacheKey, size_t, MeshCacheKeyHash>
    g_mesh_cache_build_reservations;
std::unordered_map<const void *, uint64_t> g_mesh_cache_owner_generations;
size_t g_mesh_cache_reserved_bytes;
unsigned int g_mesh_arena_evictions_this_frame;
MeshArena g_mesh_vertex_arena;
MeshArena g_mesh_index_arena;
std::vector<InFlightMeshSubmission> g_inflight_mesh_submissions;
FontTextureEntry g_font_textures[kFontTextureBankCount];
TextureEntry *g_current_font_texture;
int g_current_font_bank = -1;
std::vector<SpriteVertex> g_vertices;
std::vector<MeshColour> g_mesh_colours;
std::vector<AnimatedMeshVertex> g_animated_mesh_vertices_stream;
std::vector<MioPanLightState> g_vertex_light_states;
/* Fragment-stage light images, deduplicated against the previous entry -- the
 * walker changes lights per coordinate block, so consecutive meshes in one
 * block share a state and the common case is one push per block. */
std::vector<MioPanLightState> g_fragment_light_states;
std::vector<DrawCommand> g_draws;

/* Resident meshes by handle.  The registry's reference is the one Release
 * drops; queued draws hold their own. */
std::unordered_map<unsigned int, ResidentMeshPtr> g_resident_meshes;
unsigned int g_next_resident_mesh_id = 1;
bool g_resident_meshes_enabled = true;
/* MioPan_RendererSetResidentTextures().  The renderer only stores it; the SGD
 * bridge, which owns the per-model bindings, is what reads it. */
bool g_resident_textures_enabled = true;
/* GPU bytes held by live ResidentMesh objects, queued draws' included. */
size_t g_resident_mesh_bytes;
/* Per stats window: units the walker handed over, and the draws they became
 * after consecutive ones were folded together. */
int g_resident_units;
int g_resident_draws;
int g_resident_creates;
/* Per stats window: models whose textures were resolved, TRI2 uploads that
 * were then skipped, skipped runs sent after all because a draw sampled what
 * they leave, and what the GS was sent anyway -- the figure all of this
 * exists to bring down. */
int g_resident_texture_captures;
int g_resident_texture_skips;
int g_resident_texture_replays;
uint64_t g_window_gs_uploads;
uint64_t g_window_gs_upload_bytes;
/* MioPan_RendererGetFrameIndex(). */
unsigned long long g_frame_index;

ResidentMesh::~ResidentMesh()
{
    /* SDL_ReleaseGPUBuffer defers the free past any submitted command buffer
     * that still references the buffer, so this is safe whenever the last
     * reference happens to drop -- the draw list clearing at BeginFrame, or a
     * Release while no draw holds one. */
    if (g_device != nullptr)
    {
        if (vertex_buffer != nullptr)
        {
            SDL_ReleaseGPUBuffer(g_device, vertex_buffer);
        }
        if (index_buffer != nullptr)
        {
            SDL_ReleaseGPUBuffer(g_device, index_buffer);
        }
    }
    g_resident_mesh_bytes =
        bytes <= g_resident_mesh_bytes ? g_resident_mesh_bytes - bytes : 0;
}

DrawSourceFrameMetrics g_draw_source_metrics[DRAW_SOURCE_COUNT];
MeshStreamState g_mesh_stream;
unsigned int g_next_mesh_stream_token = 1;
SpriteUniformBlock g_uniforms;
float g_3d_view[16];
float g_3d_projection[16];
float g_3d_view_projection[16];
bool g_3d_camera_valid;

/*
 * The 3D camera as it stood at the end of the previous logical frame, latched
 * by BeginFrame before the game installs this frame's.  It is the other
 * endpoint frame-rate decoupling interpolates from -- see
 * MioPan_RendererBlendCameraFromPrevious().
 */
float g_prev_3d_view[16];
float g_prev_3d_projection[16];
bool g_prev_3d_camera_valid;

/*
 * Live GS fog state, in the form the shader consumes: (fMin, fMax, FA, FB)
 * and FOGCOL, both already divided by 255 so MikuPanApplyGsFog() can work in
 * its own 0..1 space.  MioPan_RendererSetFog() is the only writer.
 *
 * These start at "no fog" rather than at gra3d's own defaults, because a draw
 * queued before the game has ever pushed a setting -- the logo and title, and
 * anything drawn while a room is loading -- had no VU1 fog block on hardware
 * either.
 */
float g_gs_fog[4] = {0.0f, 1.0f, 1.0f, 0.0f};
float g_gs_fog_color[4] = {0.0f, 0.0f, 0.0f, 0.0f};

/*
 * Live GS alpha-test state, in the form the shader consumes.
 *
 * The GS compares the post-texture-function fragment alpha against AREF in raw
 * 0..255 units, where 128 is "opaque".  Host alpha reaches the shader already
 * divided by that 128 (AdjustPS2Alpha on the texel, Ps2AlphaToFloat on the
 * vertex colour), so the reference is scaled the same way here and the shader
 * compares in its own 0..1 space.  A consequence worth knowing: host texel
 * alpha saturates at 1.0, so an AREF above 128 can never be passed -- no
 * content in this game uses one, but a silently-empty draw would look like a
 * missing model rather than a clamp.
 */
constexpr float kPs2AlphaScale = 128.0f;
float g_gs_alpha_test[4];

/*
 * Cut-out edge sharpening -- a port setting, not GS state.
 *
 * The register above is the hardware's own alpha test and this is not: the ROM
 * asks for "discard fully transparent" and nothing stronger anywhere (measured
 * over a full session -- 574,618 TEST writes, highest AREF 0x01), which was
 * the whole story at 640x448 where a texel was a pixel.  Magnify that texture
 * to a modern window and the same one-texel alpha step is spread across
 * several pixels by the bilinear filter, dragging the RGB behind the cut-out
 * into the silhouette with it.  See MikuPanSharpenAlpha() in
 * resources/shaders/hlsl/mikupan_common.hlsli for what the shader does with
 * this; 0 disables it and restores the previous behaviour exactly.
 */
float g_alpha_sharpen = 0.0f;

/* The hard cut-out threshold that rides beside it in uAlphaSharpen.y.  See
 * MioPan_RendererSetAlphaCutoff() in the header for why the two are
 * separate knobs rather than one. */
float g_alpha_cutoff = 0.0f;

/* The rest of the GS draw environment, mirrored from draw_env.c.  These start
 * at the values ClearDrawEnv() lays down every frame, so a draw queued before
 * anything writes the registers behaves as it would on hardware. */
GsBlendMode g_gs_blend_mode = GS_BLEND_ALPHA;
bool g_gs_depth_write = true;
/* ZTST from the GS TEST register, and the Z of the 2D primitive being built.
 * Both are per-draw state the 2D layer refreshes immediately before the draw,
 * the same way the blend mode and ZMSK already are.
 *
 * ALWAYS is the resting value -- CopySprDToSpr / CopySqrDToSqr write
 * SCE_GS_SET_TEST(1, 1, 0, 0, 0, 0, 1, 1) into every primitive they build --
 * so a draw that says nothing behaves as it did before any of this existed. */
GsDepthCompare g_gs_ztst = GS_DEPTH_ALWAYS;
unsigned int g_gs_2d_z = 0xffffffu;
/*
 * Armed by MioPan_RendererSetGs2dDepth() and consumed by the very next quad,
 * rather than left standing as draw-env state.
 *
 * The quad bridges have callers that are not GS 2D primitives at all --
 * MapSky.c's fog band and dome go through the same two entry points, and so do
 * graphics.c's flat fills.  Those supply no Z, and letting them inherit the
 * last sprite's put the sky at the front of the depth buffer and hid the whole
 * room behind it.  One-shot means a draw that did not ask for depth is exactly
 * as depth-less as it was before any of this existed.
 */
bool g_gs_2d_depth_armed;
bool g_dbg_no_2d_depth;
GsScissor g_gs_scissor = {0, 0, kLogicalWidth - 1, kLogicalHeight - 1};

TextureEntry *GetTexture(const sceGsTex0 *tex0);
/* Defined beside the pause capture, used from UpdateViewExtend() above it. */
bool EnsureSceneCaptureTexture(Uint32 width, Uint32 height);
int FindCaptureSlotIndex(unsigned int addr, int logical_w, int logical_h);
int DeclareCaptureSlot(unsigned int addr, int logical_w, int logical_h);

/*
 * A GS vertex colour means two different things depending on PRIM's TME bit,
 * and the two divisors are not interchangeable.
 *
 *   TME=1, MODULATE   Cv = (Ct * Cf) >> 7, so Cf == 0x80 is "unchanged" and
 *                     the divisor is 128.
 *   TME=0, untextured Cv = Cf, written straight to the framebuffer, so the
 *                     divisor is 255.
 *
 * Alpha is 128 either way -- the blend is (((A - B) * C) >> 7) + D, so As ==
 * 0x80 is 1.0 with or without a texture.
 */
float Ps2ColorToFloat(unsigned char value)
{
    return std::min(1.0f, (float)value / 128.0f);
}

float Ps2UntexturedColorToFloat(unsigned char value)
{
    return (float)value / 255.0f;
}

float Ps2AlphaToFloat(unsigned char value)
{
    return std::min(1.0f, (float)value / 128.0f);
}

float Clamp(float value, float low, float high)
{
    return std::max(low, std::min(high, value));
}

float Clamp01(float value)
{
    return Clamp(value, 0.0f, 1.0f);
}

template <typename T>
void ReserveGeometric(std::vector<T> &values, size_t required,
                      size_t minimum_capacity)
{
    if (required <= values.capacity())
    {
        return;
    }

    size_t capacity = std::max(values.capacity(), minimum_capacity);
    while (capacity < required)
    {
        if (capacity > std::numeric_limits<size_t>::max() / 2u)
        {
            capacity = required;
            break;
        }
        capacity *= 2u;
    }
    values.reserve(capacity);
}

void SetIdentityMatrix(float *m)
{
    std::memset(m, 0, sizeof(float) * 16);
    for (int i = 0; i < 4; i++)
    {
        m[i * 4 + i] = 1.0f;
    }
}

void MulMatrixRowMajor(float *out, const float *a, const float *b)
{
    float r[16];

    for (int row = 0; row < 4; row++)
    {
        for (int col = 0; col < 4; col++)
        {
            float v = 0.0f;
            for (int k = 0; k < 4; k++)
            {
                v += a[row * 4 + k] * b[k * 4 + col];
            }
            r[row * 4 + col] = v;
        }
    }

    std::memcpy(out, r, sizeof(r));
}

bool MatrixIsFinite(const float *m)
{
    for (int i = 0; i < 16; i++)
    {
        if (!std::isfinite(m[i]))
        {
            return false;
        }
    }
    return true;
}

void ApplyMatrixRowVector(float *out, const float *v, const float *m)
{
    for (int col = 0; col < 4; col++)
    {
        out[col] = v[0] * m[col] + v[1] * m[4 + col] +
                   v[2] * m[8 + col] + v[3] * m[12 + col];
    }
}

/* ------------------------------------------------------------------------
 *  Camera blending, for frame-rate decoupling.
 *
 *  Everything here is in this file's row-vector convention (p' = p * M, see
 *  ApplyMatrixRowVector): the rotation is the upper-left 3x3 with the basis
 *  vectors in ROWS, and the translation is row 3.
 *
 *  A view matrix must not be blended element-wise.  Its 3x3 is a rotation, and
 *  the average of two rotation matrices is not one -- it shears and shrinks,
 *  which on a turning camera reads as the world breathing.  So the rotation
 *  goes through a quaternion slerp, and the translation is converted back to a
 *  camera position first: row 3 of a view matrix is -eye * R, not the eye, and
 *  lerping that instead swings the camera along an arc around the origin.
 * ---------------------------------------------------------------------- */

/* Rotation 3x3 (rows) -> quaternion (x, y, z, w). */
void RotationRowsToQuat(const float *m, float *q)
{
#define MIOPAN_M(r, c) m[(r) * 4 + (c)]
    const float trace = MIOPAN_M(0, 0) + MIOPAN_M(1, 1) + MIOPAN_M(2, 2);

    if (trace > 0.0f)
    {
        const float s = std::sqrt(trace + 1.0f) * 2.0f;   /* 4w */
        q[3] = 0.25f * s;
        q[0] = (MIOPAN_M(1, 2) - MIOPAN_M(2, 1)) / s;
        q[1] = (MIOPAN_M(2, 0) - MIOPAN_M(0, 2)) / s;
        q[2] = (MIOPAN_M(0, 1) - MIOPAN_M(1, 0)) / s;
    }
    else if (MIOPAN_M(0, 0) > MIOPAN_M(1, 1) &&
             MIOPAN_M(0, 0) > MIOPAN_M(2, 2))
    {
        const float s = std::sqrt(1.0f + MIOPAN_M(0, 0) - MIOPAN_M(1, 1) -
                                  MIOPAN_M(2, 2)) * 2.0f;  /* 4x */
        q[3] = (MIOPAN_M(1, 2) - MIOPAN_M(2, 1)) / s;
        q[0] = 0.25f * s;
        q[1] = (MIOPAN_M(0, 1) + MIOPAN_M(1, 0)) / s;
        q[2] = (MIOPAN_M(0, 2) + MIOPAN_M(2, 0)) / s;
    }
    else if (MIOPAN_M(1, 1) > MIOPAN_M(2, 2))
    {
        const float s = std::sqrt(1.0f + MIOPAN_M(1, 1) - MIOPAN_M(0, 0) -
                                  MIOPAN_M(2, 2)) * 2.0f;  /* 4y */
        q[3] = (MIOPAN_M(2, 0) - MIOPAN_M(0, 2)) / s;
        q[0] = (MIOPAN_M(0, 1) + MIOPAN_M(1, 0)) / s;
        q[1] = 0.25f * s;
        q[2] = (MIOPAN_M(1, 2) + MIOPAN_M(2, 1)) / s;
    }
    else
    {
        const float s = std::sqrt(1.0f + MIOPAN_M(2, 2) - MIOPAN_M(0, 0) -
                                  MIOPAN_M(1, 1)) * 2.0f;  /* 4z */
        q[3] = (MIOPAN_M(0, 1) - MIOPAN_M(1, 0)) / s;
        q[0] = (MIOPAN_M(0, 2) + MIOPAN_M(2, 0)) / s;
        q[1] = (MIOPAN_M(1, 2) + MIOPAN_M(2, 1)) / s;
        q[2] = 0.25f * s;
    }
#undef MIOPAN_M
}

/* Quaternion -> rotation 3x3 (rows), written into the upper-left of `m`.
 * The exact inverse of RotationRowsToQuat; the two must stay a pair. */
void QuatToRotationRows(const float *q, float *m)
{
    const float x = q[0];
    const float y = q[1];
    const float z = q[2];
    const float w = q[3];

    m[0] = 1.0f - 2.0f * (y * y + z * z);
    m[1] = 2.0f * (x * y + w * z);
    m[2] = 2.0f * (x * z - w * y);

    m[4] = 2.0f * (x * y - w * z);
    m[5] = 1.0f - 2.0f * (x * x + z * z);
    m[6] = 2.0f * (y * z + w * x);

    m[8] = 2.0f * (x * z + w * y);
    m[9] = 2.0f * (y * z - w * x);
    m[10] = 1.0f - 2.0f * (x * x + y * y);
}

void SlerpQuat(const float *a, const float *b, float t, float *out)
{
    float dot = a[0] * b[0] + a[1] * b[1] + a[2] * b[2] + a[3] * b[3];
    float sign = 1.0f;

    /* q and -q are the same orientation; take the short way round. */
    if (dot < 0.0f)
    {
        dot = -dot;
        sign = -1.0f;
    }

    float wa;
    float wb;
    if (dot > 0.9995f)
    {
        /* Near-parallel: sin(theta) underflows, and a straight lerp is within
         * float precision of the arc anyway. */
        wa = 1.0f - t;
        wb = t;
    }
    else
    {
        const float theta = std::acos(dot);
        const float inv_sin = 1.0f / std::sin(theta);
        wa = std::sin((1.0f - t) * theta) * inv_sin;
        wb = std::sin(t * theta) * inv_sin;
    }
    wb *= sign;

    float len2 = 0.0f;
    for (int i = 0; i < 4; i++)
    {
        out[i] = a[i] * wa + b[i] * wb;
        len2 += out[i] * out[i];
    }

    if (len2 > 0.0f && std::isfinite(len2))
    {
        const float inv = 1.0f / std::sqrt(len2);
        for (int i = 0; i < 4; i++)
        {
            out[i] *= inv;
        }
    }
    else
    {
        out[0] = out[1] = out[2] = 0.0f;
        out[3] = 1.0f;
    }
}

/* Row 3 of a view matrix is -eye * R.  Recover eye = -(row j of R) . t.
 *
 * In double: a room camera sits a few thousand units out, where a float carries
 * about 2.4e-4 of absolute resolution, and this value is immediately fed back
 * through the same product to rebuild the row.  Accumulating in float made the
 * round trip lose ~1e-3 units even at t == 0.  This runs once per presented
 * frame, not once per draw, so the width is free. */
void ViewMatrixEye(const float *view, double *eye)
{
    for (int j = 0; j < 3; j++)
    {
        eye[j] = -((double)view[12] * (double)view[j * 4 + 0] +
                   (double)view[13] * (double)view[j * 4 + 1] +
                   (double)view[14] * (double)view[j * 4 + 2]);
    }
}

/* Blend two view matrices: slerp the orientation, lerp the eye, recompose. */
void BlendViewMatrix(const float *a, const float *b, float t, float *out)
{
    /* The endpoints are the inputs.  Worth short-circuiting for its own sake --
     * a presented frame that lands exactly on a simulation tick must be
     * bit-identical to the undecoupled one, or the seam shows. */
    if (t <= 0.0f)
    {
        std::memcpy(out, a, sizeof(float) * 16);
        return;
    }
    if (t >= 1.0f)
    {
        std::memcpy(out, b, sizeof(float) * 16);
        return;
    }

    float qa[4];
    float qb[4];
    float q[4];
    double eye_a[3];
    double eye_b[3];
    double eye[3];

    RotationRowsToQuat(a, qa);
    RotationRowsToQuat(b, qb);
    SlerpQuat(qa, qb, t, q);

    ViewMatrixEye(a, eye_a);
    ViewMatrixEye(b, eye_b);
    for (int i = 0; i < 3; i++)
    {
        eye[i] = eye_a[i] + (eye_b[i] - eye_a[i]) * (double)t;
    }

    std::memset(out, 0, sizeof(float) * 16);
    QuatToRotationRows(q, out);

    /* t = -eye * R, with R the rotation just written. */
    for (int col = 0; col < 3; col++)
    {
        out[12 + col] = (float)-(eye[0] * (double)out[col] +
                                 eye[1] * (double)out[4 + col] +
                                 eye[2] * (double)out[8 + col]);
    }

    /* The fourth column of a view matrix is (0,0,0,1); memset laid down the
     * zeroes, so only the corner is left. */
    out[15] = 1.0f;
}

float Normalize3(float *out, const float *value)
{
    const float length2 = value[0] * value[0] + value[1] * value[1] +
                          value[2] * value[2];
    if (!(length2 > 0.0f) || !std::isfinite(length2))
    {
        out[0] = out[1] = out[2] = 0.0f;
        return 0.0f;
    }
    const float inverse_length = 1.0f / std::sqrt(length2);
    out[0] = value[0] * inverse_length;
    out[1] = value[1] * inverse_length;
    out[2] = value[2] * inverse_length;
    return 1.0f / inverse_length;
}

float Dot3(const float *a, const float *b)
{
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

/*
 * Beyond this much rotation in one tick the pair is treated as a cut and the
 * frame's own matrix is used unblended.  A quaternion dot is cos(theta/2), so
 * 0.7071 is 90 degrees -- generous for anything the animation system produces
 * and short of 180, where the slerp arc stops being defined at all.
 */
constexpr float kGeometryMaxTickRotationCos = 0.70710678f;

/*
 * Blend two object transforms.
 *
 * Not the same problem as BlendViewMatrix, and not solvable with it.  Row 3 of
 * a local->world matrix IS the position, so the translation lerps directly and
 * there is no eye to recover.  What it carries that a view matrix does not is
 * SCALE -- MapGeom.h's MapGeomSetScaleMatrix() writes one and gra3dSGD.c's
 * _CalcWeightedLocalWorldMatrix() exists to normalise it back out -- and
 * lerping a scaled rotation element-wise shears it exactly as it would a
 * camera's.  So each row is split into a length and a direction, the directions
 * go through the same quaternion slerp the camera uses, and the lengths lerp on
 * their own.
 *
 * Returns false when the pair cannot be blended, and the caller then uses the
 * frame's own matrix -- which for that draw is increment 3's behaviour, so the
 * failure mode is invisible rather than wrong.  Four reasons: a degenerate row
 * (no direction to slerp), a mirrored basis (the quaternion extraction assumes
 * a right-handed one), a non-affine matrix, and a rotation big enough to be a
 * cut.
 */
bool BlendModelMatrix(const float *a, const float *b, float t, float *out)
{
    if (t <= 0.0f)
    {
        std::memcpy(out, a, sizeof(float) * 16);
        return true;
    }
    if (t >= 1.0f)
    {
        std::memcpy(out, b, sizeof(float) * 16);
        return true;
    }

    /* Affine only.  Nothing in the engine hands a 3D draw a projective local
     * transform, and the decomposition below has no meaning for one. */
    if (a[3] != 0.0f || a[7] != 0.0f || a[11] != 0.0f || a[15] != 1.0f ||
        b[3] != 0.0f || b[7] != 0.0f || b[11] != 0.0f || b[15] != 1.0f)
    {
        return false;
    }

    float basis_a[16];
    float basis_b[16];
    float scale_a[3];
    float scale_b[3];

    std::memset(basis_a, 0, sizeof(basis_a));
    std::memset(basis_b, 0, sizeof(basis_b));
    for (int row = 0; row < 3; row++)
    {
        scale_a[row] = Normalize3(&basis_a[row * 4], &a[row * 4]);
        scale_b[row] = Normalize3(&basis_b[row * 4], &b[row * 4]);
        if (scale_a[row] <= 0.0f || scale_b[row] <= 0.0f)
        {
            return false;
        }
    }

    /* Right-handed and orthogonal, or the quaternion round trip is not the
     * identity it is being used as.  The cross-product test covers both: for
     * an orthonormal right-handed basis row0 x row1 is row2 exactly. */
    const float *bases[2] = {basis_a, basis_b};
    for (int which = 0; which < 2; which++)
    {
        const float *basis = bases[which];
        const float cross[3] = {
            basis[1] * basis[6] - basis[2] * basis[5],
            basis[2] * basis[4] - basis[0] * basis[6],
            basis[0] * basis[5] - basis[1] * basis[4],
        };
        if (Dot3(cross, &basis[8]) < 0.99f)
        {
            return false;
        }
    }

    float quat_a[4];
    float quat_b[4];
    float quat[4];
    RotationRowsToQuat(basis_a, quat_a);
    RotationRowsToQuat(basis_b, quat_b);

    const float dot = quat_a[0] * quat_b[0] + quat_a[1] * quat_b[1] +
                      quat_a[2] * quat_b[2] + quat_a[3] * quat_b[3];
    if (std::fabs(dot) < kGeometryMaxTickRotationCos)
    {
        return false;
    }
    SlerpQuat(quat_a, quat_b, t, quat);

    std::memset(out, 0, sizeof(float) * 16);
    QuatToRotationRows(quat, out);
    for (int row = 0; row < 3; row++)
    {
        const float scale = scale_a[row] + (scale_b[row] - scale_a[row]) * t;
        out[row * 4 + 0] *= scale;
        out[row * 4 + 1] *= scale;
        out[row * 4 + 2] *= scale;
    }
    out[12] = a[12] + (b[12] - a[12]) * t;
    out[13] = a[13] + (b[13] - a[13]) * t;
    out[14] = a[14] + (b[14] - a[14]) * t;
    out[15] = 1.0f;

    return MatrixIsFinite(out);
}

void AddLightRgb(float *result, const float *colour, float scale)
{
    result[0] += colour[0] * scale;
    result[1] += colour[1] * scale;
    result[2] += colour[2] * scale;
}

/* Exceptional upload/cache failures expand an animated indexed draw through
 * the ordinary streamed pipeline.  Re-evaluate its snapshotted light block on
 * the CPU so a transient GPU-buffer failure costs time rather than making the
 * characters disappear or flash white.  The common path performs this exact
 * arithmetic in mesh_animated_lit.vert.hlsl. */
void EvaluateAnimatedVertexLighting(float *out,
                                    const DrawCommand &draw,
                                    const AnimatedMeshVertex &vertex)
{
    if (draw.vertex_light_index >= g_vertex_light_states.size())
    {
        out[0] = out[1] = out[2] = out[3] = 1.0f;
        return;
    }
    const MioPanLightState &lights =
        g_vertex_light_states[draw.vertex_light_index];
    if ((lights.config[1] & 1) == 0)
    {
        out[0] = out[1] = out[2] = out[3] = 1.0f;
        return;
    }

    float world[4];
    float local[4] = {vertex.position[0], vertex.position[1],
                      vertex.position[2], 1.0f};
    ApplyMatrixRowVector(world, local, draw.model);

    float transformed_normal[3] = {
        vertex.normal[0] * draw.model[0] +
            vertex.normal[1] * draw.model[4] +
            vertex.normal[2] * draw.model[8],
        vertex.normal[0] * draw.model[1] +
            vertex.normal[1] * draw.model[5] +
            vertex.normal[2] * draw.model[9],
        vertex.normal[0] * draw.model[2] +
            vertex.normal[1] * draw.model[6] +
            vertex.normal[2] * draw.model[10],
    };
    float normal[3];
    Normalize3(normal, transformed_normal);

    /* The VU1 model, kept in step with mesh_animated_lit.vert.hlsl.  Colours
     * are GS 0..255 units with gra3dCalcVu1MaterialData*()'s scales -- and
     * monotone -- already folded in, so nothing is scaled or greyed here. */
    float result[3] = {lights.ambient[0], lights.ambient[1], lights.ambient[2]};

    /* directional: PLOOP_TYPE2 0x6c8-0x7d8.  Fixed eighth power against a
     * half-vector built once per frame from the camera forward axis. */
    for (int i = 0; i < 3; i++)
    {
        const float nd = std::max(
            Dot3(normal, lights.directional_diffuse_dir[i]), 0.0f);
        float ns = std::max(
            Dot3(normal, lights.directional_specular_dir[i]), 0.0f);
        ns *= ns;
        ns *= ns;
        ns *= ns;
        AddLightRgb(result, lights.directional_diffuse[i], nd);
        AddLightRgb(result, lights.directional_specular[i], ns);
    }

    /* The light-TYPE enable is what gates a kernel -- the microcode skips the
     * whole thing for a disabled type and VU1 memory keeps whatever was there
     * before.  Within an enabled type the live lane COUNT bounds the loop,
     * which is the host's replacement for the VU's three fixed lanes; see
     * MIOPAN_VU1_MAX_LANES. */

    /* spot: CalcIntens 0x018-0x208 */
    if ((lights.config[0] & 1) != 0)
    {
        const int count = std::min(lights.counts[0], (int)MIOPAN_VU1_MAX_LANES);
        for (int i = 0; i < count; i++)
        {
            const float L[3] = {lights.spot_position[i][0] - world[0],
                                lights.spot_position[i][1] - world[1],
                                lights.spot_position[i][2] - world[2]};
            const float len2 = Dot3(L, L);
            if (len2 <= 0.0f)
            {
                continue;
            }
            const float inv_len2 = 1.0f / len2;

            /* Negated against the microcode: the port keeps vDirection as the
             * BEAM engine-wide and reconciles the VU1's opposite sense here.
             * Same note as mesh_animated_lit.vert.hlsl; vu1/LIGHTING.md 3.3. */
            const float cone_dot =
                std::max(-Dot3(lights.spot_direction[i], L), 0.0f);
            const float cone =
                std::max(cone_dot * cone_dot * inv_len2 -
                             lights.spot_params[i][1],
                         0.0f) * lights.spot_params[i][2];

            /* Capped BEFORE the cone multiplies it, and the cone is not
             * capped at all (VU: MINIw at 0x178, then MUL at 0x1b0). */
            const float c = std::min(
                std::max(Dot3(normal, L), 0.0f) * lights.spot_params[i][0] *
                    inv_len2, 1.0f);
            AddLightRgb(result, lights.spot_diffuse[i], c * cone);

            float e = c * c;
            e *= e;
            e *= e;
            AddLightRgb(result, lights.spot_specular[i], e * cone);
        }
    }

    /* point: CalcPoint 0x220-0x3e0 */
    if ((lights.config[0] & 2) != 0)
    {
        const int count = std::min(lights.counts[1], (int)MIOPAN_VU1_MAX_LANES);
        for (int i = 0; i < count; i++)
        {
            const float L[3] = {lights.point_position[i][0] - world[0],
                                lights.point_position[i][1] - world[1],
                                lights.point_position[i][2] - world[2]};
            const float len2 = Dot3(L, L);
            if (len2 <= 0.0f)
            {
                continue;
            }
            const float inv_len2 = 1.0f / len2;

            /* ROM BUG, reproduced -- CalcPoint omits CalcIntens' MR32.z at
             * 0x0e0, so lane 2's diffuse dot picks up lane 1's Lz in place of
             * its own Ly.  See vu1/LIGHTING.md section 3.2.  It is a property
             * of the VU's three-lane transpose, so with the lanes widened it
             * repeats every group of three rather than applying once; kept in
             * step with MioPanVu1Point() in miopan_vu1_lighting.hlsli. */
            float Ldot[3] = {L[0], L[1], L[2]};
            if ((i % 3) == 2)
            {
                Ldot[1] = lights.point_position[i - 1][1] - world[1];
            }

            const float c = std::min(
                std::max(Dot3(normal, Ldot), 0.0f) *
                    lights.point_params[i][0] * inv_len2, 1.0f);
            AddLightRgb(result, lights.point_diffuse[i], c);

            /* Point specular is the coefficient to the FOURTH -- two squarings
             * against the spot kernel's three. */
            float e = c * c;
            e *= e;
            AddLightRgb(result, lights.point_specular[i], e);
        }
    }

    /* MINIw against GLOBALAMBIENT.w (255) -- the microcode's only clamp -- and
     * then the GS MODULATE convention, where 128 is unity. */
    const float clamp_max = lights.ambient[3];
    constexpr float kGsModulateUnity = 1.0f / 128.0f;
    for (int c = 0; c < 3; c++)
    {
        out[c] = std::min(std::max(result[c], 0.0f), clamp_max) *
                 kGsModulateUnity;
    }
    out[3] = 1.0f;
}

/* The direct mesh stream has two spare UV components.  Use octahedral
 * encoding there so the rare cache-fallback fragment path gets a full normal
 * without making every 2D SpriteVertex 16 bytes larger. */
void EncodeMeshNormal(float *uv, const float *normal)
{
    float x = 0.0f;
    float y = 0.0f;
    float z = 1.0f;
    if (normal != nullptr && std::isfinite(normal[0]) &&
        std::isfinite(normal[1]) && std::isfinite(normal[2]))
    {
        const float length = std::sqrt(normal[0] * normal[0] +
                                       normal[1] * normal[1] +
                                       normal[2] * normal[2]);
        if (length > 0.000001f)
        {
            x = normal[0] / length;
            y = normal[1] / length;
            z = normal[2] / length;
        }
    }

    const float inverse_l1 = 1.0f /
        (std::fabs(x) + std::fabs(y) + std::fabs(z));
    x *= inverse_l1;
    y *= inverse_l1;
    z *= inverse_l1;
    if (z < 0.0f)
    {
        const float old_x = x;
        x = (1.0f - std::fabs(y)) * (old_x >= 0.0f ? 1.0f : -1.0f);
        y = (1.0f - std::fabs(old_x)) * (y >= 0.0f ? 1.0f : -1.0f);
    }
    uv[2] = x * 0.5f + 0.5f;
    uv[3] = y * 0.5f + 0.5f;
}

bool ProjectMeshVertex(float *clip,
                       const float *position,
                       const float *local_world)
{
    float local[4];
    float world[4];
    float view[4];

    if (position == nullptr || local_world == nullptr)
    {
        return false;
    }

    local[0] = position[0];
    local[1] = position[1];
    local[2] = position[2];
    local[3] = 1.0f;

    ApplyMatrixRowVector(world, local, local_world);
    ApplyMatrixRowVector(view, world, g_3d_view);
    ApplyMatrixRowVector(clip, view, g_3d_projection);

    /*
     * Do not reject a mesh vertex merely because it is behind the eye.  A
     * triangle can straddle that plane, and SDL's homogeneous clipper must see
     * all three vertices to retain the visible portion.  Callers that divide
     * by W (the immediate-mode line/point helpers) check it themselves.
     */
    return std::isfinite(clip[0]) && std::isfinite(clip[1]) &&
           std::isfinite(clip[2]) && std::isfinite(clip[3]);
}

float ScreenXToClip(float x)
{
    return (x / (float)kLogicalWidth) * 2.0f - 1.0f;
}

float ScreenYToClip(float y)
{
    return 1.0f - (y / (float)kLogicalHeight) * 2.0f;
}

/*
 * How much wider (or taller) the output is than the original 640x448 frame,
 * measured in original-frame units: 1.0 on a 4:3 output, about 1.21 on 16:9.
 * Only one of the two is ever above 1.
 *
 * Everything that has to agree about "how much of the world fits on screen"
 * reads these -- the 3D projection widens by them so the extra area shows more
 * world instead of black bars, the sky backdrop stretches its geometry out to
 * the same bounds, and the 2D pass shrinks by their reciprocal so overlays
 * stay centred at their original proportions.
 *
 * Sampled from the window once per BeginFrame rather than from the swapchain
 * at present time: the game builds its camera in the middle of the frame, long
 * before the swapchain is acquired, and 2D and 3D must not disagree about the
 * aspect within one frame.  The cost is that a resize lands one frame late.
 */
float g_view_extend_x = 1.0f;
float g_view_extend_y = 1.0f;

/*
 * Internal render resolution policy.  See MioPan_RendererSetRenderResolution()
 * in the header for what the modes mean; the short version is that these decide
 * pixel density only, never framing -- the aspect always comes from the window
 * through g_view_extend_* above, so the scene target and the swapchain are the
 * same shape and the present blit never letterboxes.
 */
int g_render_res_mode = MIOPAN_RENDER_RES_MATCH_WINDOW;
float g_render_res_scale = 1.0f;
int g_upscale_filter = MIOPAN_RENDER_FILTER_LINEAR;
int g_window_mode = MIOPAN_WINDOW_MODE_WINDOWED;
int g_aspect_mode = MIOPAN_ASPECT_AUTO;
float g_aspect_custom = 16.0f / 9.0f;
/* Last size the window had while not fullscreen; seeded from the config before
 * the window is created and re-latched on every windowed resize. */
bool g_config_loaded;
int g_windowed_width = kLogicalWidth * 2;
int g_windowed_height = kLogicalHeight * 2;
/* Where the picture landed in the window last frame, for the UI read-out. */
int g_present_rect[4];

/*
 * Output transform state -- HDR and the display grade.  See the block comment
 * on MioPan_RendererSetHdrMode() in the header for what any of it means.
 *
 * g_hdr_mode is what the player asked for; g_hdr_composition is what the
 * swapchain actually got, which differs whenever AUTO lands on an SDR display
 * or a forced composition is refused.  Everything else is a slider.
 */
int g_hdr_mode = MIOPAN_HDR_OFF;
SDL_GPUSwapchainComposition g_hdr_composition =
    SDL_GPU_SWAPCHAINCOMPOSITION_SDR;
SDL_GPUPresentMode g_present_mode = SDL_GPU_PRESENTMODE_VSYNC;
/* Chosen at startup and never changed: whether the pipelines were built
 * against the 10-bit scene target HDR wants. */
bool g_hdr_full_precision;
/* 0 means "follow the display", which is the default for both. */
float g_hdr_paper_white = 200.0f;
float g_hdr_peak;
float g_hdr_expansion = 0.35f;
float g_hdr_expansion_knee = 0.75f;
/* What the window last told us about the display it is on, refreshed on
 * SDL_EVENT_WINDOW_HDR_STATE_CHANGED and once per BeginFrame.  `sdr_white` is
 * SDR white in scRGB units, so nits is that times 80. */
bool g_display_hdr_enabled;
float g_display_sdr_white = 1.0f;
float g_display_headroom = 1.0f;

float g_grade_brightness = 1.0f;
float g_grade_contrast = 1.0f;
float g_grade_gamma = 1.0f;
float g_grade_saturation = 1.0f;

/* The composited SDR output the present pass reads, allocated only when the
 * pass is actually taken. */
SDL_GPUTexture *g_output_texture;
Uint32 g_output_width;
Uint32 g_output_height;
SDL_GPUGraphicsPipeline *g_present_pipeline;
SDL_GPUShader *g_present_vertex_shader;
SDL_GPUShader *g_present_fragment_shader;
SDL_GPUTextureFormat g_present_pipeline_format = SDL_GPU_TEXTUREFORMAT_INVALID;
bool g_present_pipeline_failed;

/* Mirrors PresentUniforms in resources/shaders/hlsl/present.frag.hlsl. */
struct PresentUniformBlock
{
    float grade[4];
    float hdr[4];
    float output[4];
    float target[4];
};

/*
 * The aspect the frame is composed for.  AUTO is the window's own, which is
 * what makes it bar-free; every other mode is a fixed number and the difference
 * from the window is taken up by black.
 */
float ResolveAspect(int window_w, int window_h)
{
    switch (g_aspect_mode)
    {
    case MIOPAN_ASPECT_ORIGINAL:
        return (float)kLogicalWidth / (float)kLogicalHeight;
    case MIOPAN_ASPECT_4_3:
        return 4.0f / 3.0f;
    case MIOPAN_ASPECT_16_9:
        return 16.0f / 9.0f;
    case MIOPAN_ASPECT_16_10:
        return 16.0f / 10.0f;
    case MIOPAN_ASPECT_CUSTOM:
        return g_aspect_custom;
    default:
        break;
    }
    return window_h > 0 ? (float)window_w / (float)window_h
                        : (float)kLogicalWidth / (float)kLogicalHeight;
}

/*
 * The largest rect of aspect `aspect` that fits in `avail_w` x `avail_h`,
 * centred.  Serves both ends of the pipe: it shapes the scene target inside the
 * window's pixel budget, and it places that target inside the swapchain at
 * present.  One function so the two can never disagree about where the picture
 * is or how big it should be.
 */
void FitAspect(double avail_w, double avail_h, double aspect,
               double *out_x, double *out_y, double *out_w, double *out_h)
{
    if (!(aspect > 0.0) || avail_w <= 0.0 || avail_h <= 0.0)
    {
        aspect = (double)kLogicalWidth / (double)kLogicalHeight;
    }

    double w = avail_w;
    double h = avail_w / aspect;
    if (h > avail_h)
    {
        h = avail_h;
        w = avail_h * aspect;
    }

    if (out_w != nullptr)
    {
        *out_w = w;
    }
    if (out_h != nullptr)
    {
        *out_h = h;
    }
    if (out_x != nullptr)
    {
        *out_x = (avail_w - w) * 0.5;
    }
    if (out_y != nullptr)
    {
        *out_y = (avail_h - h) * 0.5;
    }
}
/* Resolved size of the last presented frame, for the UI read-out. */
Uint32 g_render_width;
Uint32 g_render_height;

/*
 * Resolve the render size for an output of `out_w` x `out_h`.
 *
 * Must be called with g_view_extend_* already up to date for the frame:
 * NATIVE_PS2 builds its size out of them, which is what makes "1x" mean PS2
 * pixel density on any window shape rather than only on 4:3.
 *
 * The result is clamped to the device's maximum texture dimension, so an
 * absurd scale degrades to the largest target the GPU will hold instead of
 * failing the allocation and dropping the frame.
 */
void ComputeRenderSize(Uint32 out_w, Uint32 out_h, Uint32 *rw, Uint32 *rh)
{
    double w = (double)out_w;
    double h = (double)out_h;

    switch (g_render_res_mode)
    {
    case MIOPAN_RENDER_RES_NATIVE_PS2:
        /* Already the composed aspect: the extends encode it. */
        w = (double)kLogicalWidth * (double)g_view_extend_x *
            (double)g_render_res_scale;
        h = (double)kLogicalHeight * (double)g_view_extend_y *
            (double)g_render_res_scale;
        break;
    case MIOPAN_RENDER_RES_WINDOW_SCALE:
        w *= (double)g_render_res_scale;
        h *= (double)g_render_res_scale;
        /* fall through */
    default:
        /* These two take their pixel budget from the window, so the composed
         * aspect has to be imposed on it -- otherwise the target would carry
         * the window's shape and the picture would be stretched into the
         * chosen one rather than fitted to it.
         *
         * Skipped outright in AUTO, where the composed aspect IS the window's
         * and the fit is the identity.  Not merely an optimisation: computing
         * it anyway would round, come back a pixel short, and stop the render
         * size matching the swapchain -- which would silently take the default
         * path off the direct-to-swapchain route it is supposed to keep. */
        if (g_aspect_mode != MIOPAN_ASPECT_AUTO)
        {
            FitAspect(w, h,
                      (double)kLogicalWidth * (double)g_view_extend_x /
                          ((double)kLogicalHeight * (double)g_view_extend_y),
                      nullptr, nullptr, &w, &h);
        }
        break;
    }

    /* SDL_GPU has no query for this; 16384 is the floor across every backend
     * the port targets, and the swapchain itself is never larger. */
    const double kMaxDimension = 16384.0;
    long clamped_w = std::lround(std::min(std::max(w, 1.0), kMaxDimension));
    long clamped_h = std::lround(std::min(std::max(h, 1.0), kMaxDimension));

    *rw = (Uint32)std::max(1L, clamped_w);
    *rh = (Uint32)std::max(1L, clamped_h);
}

void UpdateViewExtend()
{
    int w = 0;
    int h = 0;

    g_view_extend_x = 1.0f;
    g_view_extend_y = 1.0f;

    if (g_window == nullptr || !SDL_GetWindowSizeInPixels(g_window, &w, &h) ||
        w <= 0 || h <= 0)
    {
        return;
    }

    float logical_aspect = (float)kLogicalWidth / (float)kLogicalHeight;
    /* The composed aspect, which is the window's only in AUTO mode.  Everything
     * downstream -- the 3D projection's widening, the 2D contraction, the sky's
     * bounds, the shape of the scene target -- follows from these two, so
     * choosing the aspect here is the whole of the feature; the rest is
     * arithmetic and one clear pass for the bars. */
    float output_aspect = ResolveAspect(w, h);

    if (output_aspect > logical_aspect)
    {
        g_view_extend_x = output_aspect / logical_aspect;
    }
    else
    {
        g_view_extend_y = logical_aspect / output_aspect;
    }

    /* The one point in the frame where the scene capture may be (re)allocated:
     * no draw holds a pointer to it yet.  RecordGsCapture never allocates,
     * for exactly that reason.
     *
     * Sized against the render resolution, not the window: a capture slot is a
     * copy of the scene target, and its own size is a fraction of it (a 320x448
     * GS page is half-width whatever the frame is).  Sizing these off the window
     * while the scene was rendered smaller would upscale the copy on the way in
     * and back down on the way out. */
    if (g_scene_capture_requested)
    {
        Uint32 capture_w = 0;
        Uint32 capture_h = 0;
        ComputeRenderSize((Uint32)w, (Uint32)h, &capture_w, &capture_h);
        EnsureSceneCaptureTexture(capture_w, capture_h);
    }
}

void LogSdlError(const char *what)
{
    SDL_Log("MioPan SDL_GPU: %s failed: %s", what, SDL_GetError());
}

/*
 * Put the window into the mode g_window_mode names.
 *
 * A NULL fullscreen mode is what makes this borderless-desktop rather than
 * exclusive: SDL leaves the display alone and just sizes the window over it, so
 * the compositor stays in charge and alt-tab costs nothing.  Passing a real
 * SDL_DisplayMode here instead is what would take the display exclusively, and
 * nothing in the port does.
 *
 * SDL applies window state asynchronously -- the resize arrives as an event
 * some frames later -- so SyncWindow blocks until it has actually happened.
 * Without it the first frame after a toggle acquires a swapchain at the old
 * size while UpdateViewExtend has already read the new one, and the scene
 * target is allocated at a size that is thrown away one frame later.
 */
void ApplyWindowMode()
{
    if (g_window == nullptr)
    {
        return;
    }

    if (g_window_mode == MIOPAN_WINDOW_MODE_BORDERLESS)
    {
        SDL_SetWindowFullscreenMode(g_window, nullptr);
        if (!SDL_SetWindowFullscreen(g_window, true))
        {
            LogSdlError("SDL_SetWindowFullscreen(true)");
            g_window_mode = MIOPAN_WINDOW_MODE_WINDOWED;
            return;
        }
    }
    else if (!SDL_SetWindowFullscreen(g_window, false))
    {
        LogSdlError("SDL_SetWindowFullscreen(false)");
        return;
    }

    SDL_SyncWindow(g_window);
}

/* ------------------------------------------------------------------------
 *  HDR output.
 * --------------------------------------------------------------------- */

/* scRGB's definition: 1.0 in SDL_COLORSPACE_SRGB_LINEAR is 80 cd/m2. */
const float kScrgbWhiteNits = 80.0f;

/*
 * Re-read what the window says about the display it is on.
 *
 * Every one of these is dynamic -- dragging the window to a second monitor, or
 * the player turning HDR on in the OS, moves them -- which is why SDL sends
 * SDL_EVENT_WINDOW_HDR_STATE_CHANGED and why AUTO is re-evaluated from here
 * rather than decided once at startup.
 */
void RefreshDisplayHdrInfo()
{
    if (g_window == nullptr)
    {
        return;
    }

    SDL_PropertiesID props = SDL_GetWindowProperties(g_window);
    if (props == 0)
    {
        return;
    }

    g_display_hdr_enabled =
        SDL_GetBooleanProperty(props, SDL_PROP_WINDOW_HDR_ENABLED_BOOLEAN,
                               false);
    g_display_sdr_white = SDL_GetFloatProperty(
        props, SDL_PROP_WINDOW_SDR_WHITE_LEVEL_FLOAT, 1.0f);
    g_display_headroom =
        SDL_GetFloatProperty(props, SDL_PROP_WINDOW_HDR_HEADROOM_FLOAT, 1.0f);

    if (!(g_display_sdr_white > 0.0f))
    {
        g_display_sdr_white = 1.0f;
    }
    if (!(g_display_headroom >= 1.0f))
    {
        g_display_headroom = 1.0f;
    }
}

/* SDR white in nits, as the OS reports it.  Apple platforms always answer 1.0
 * for the scRGB level, which works out to the 80 nits scRGB is defined
 * against; Windows reports the real level the player set. */
float DisplaySdrWhiteNits()
{
    return g_display_sdr_white * kScrgbWhiteNits;
}

/* Paper white in nits: the slider, or the display's own SDR level when the
 * slider is left on auto. */
float ResolvePaperWhiteNits()
{
    if (g_hdr_paper_white > 0.0f)
    {
        return g_hdr_paper_white;
    }
    const float nits = DisplaySdrWhiteNits();
    return nits > 0.0f ? nits : 200.0f;
}

/* Peak in nits: the slider, or SDR white times the headroom the display
 * reports.  Never allowed below paper white -- a peak under paper white would
 * make the expansion darken highlights instead of lifting them. */
float ResolvePeakNits()
{
    const float paper = ResolvePaperWhiteNits();
    float peak = g_hdr_peak;
    if (!(peak > 0.0f))
    {
        peak = DisplaySdrWhiteNits() * g_display_headroom;
    }
    return peak > paper ? peak : paper;
}

/*
 * Which composition the current mode should get, given what the window and the
 * device will actually accept.
 *
 * AUTO asks the display first: no headroom over SDR white means an SDR monitor
 * (or HDR switched off in the OS), and forcing an HDR swapchain there produces
 * a washed-out picture rather than an error.  A forced mode skips that test but
 * still has to pass SDL_WindowSupportsGPUSwapchainComposition, so a display
 * that cannot do PQ falls back rather than failing the swapchain.
 */
SDL_GPUSwapchainComposition ResolveHdrComposition()
{
    if (g_device == nullptr || g_window == nullptr ||
        g_hdr_mode == MIOPAN_HDR_OFF)
    {
        return SDL_GPU_SWAPCHAINCOMPOSITION_SDR;
    }

    if (g_hdr_mode == MIOPAN_HDR_AUTO && !g_display_hdr_enabled)
    {
        return SDL_GPU_SWAPCHAINCOMPOSITION_SDR;
    }

    /* scRGB first for AUTO and for SCRGB; HDR10 first only when it was asked
     * for by name.  Both lists end at SDR, which is always supported. */
    SDL_GPUSwapchainComposition candidates[2];
    if (g_hdr_mode == MIOPAN_HDR_HDR10)
    {
        candidates[0] = SDL_GPU_SWAPCHAINCOMPOSITION_HDR10_ST2084;
        candidates[1] = SDL_GPU_SWAPCHAINCOMPOSITION_HDR_EXTENDED_LINEAR;
    }
    else
    {
        candidates[0] = SDL_GPU_SWAPCHAINCOMPOSITION_HDR_EXTENDED_LINEAR;
        candidates[1] = SDL_GPU_SWAPCHAINCOMPOSITION_HDR10_ST2084;
    }

    for (SDL_GPUSwapchainComposition candidate : candidates)
    {
        if (SDL_WindowSupportsGPUSwapchainComposition(g_device, g_window,
                                                      candidate))
        {
            return candidate;
        }
    }
    return SDL_GPU_SWAPCHAINCOMPOSITION_SDR;
}

const char *HdrCompositionName(SDL_GPUSwapchainComposition composition)
{
    switch (composition)
    {
    case SDL_GPU_SWAPCHAINCOMPOSITION_SDR_LINEAR:
        return "SDR linear";
    case SDL_GPU_SWAPCHAINCOMPOSITION_HDR_EXTENDED_LINEAR:
        return "HDR scRGB";
    case SDL_GPU_SWAPCHAINCOMPOSITION_HDR10_ST2084:
        return "HDR10 PQ";
    case SDL_GPU_SWAPCHAINCOMPOSITION_SDR:
    default:
        return "SDR";
    }
}

/*
 * Hand the resolved composition to the swapchain and re-read the format it
 * produced.
 *
 * Safe to call every frame: it returns immediately when nothing changed, which
 * is what lets BeginFrame use it to follow a display the player switched to
 * HDR without the renderer having to be told.
 *
 * A refused composition is not fatal.  The requested mode is left alone --
 * the player asked for HDR and the display may yet supply it -- and the
 * swapchain simply stays SDR, which the UI reads back through
 * MioPan_RendererGetHdrActive().
 */
void ApplyHdrComposition()
{
    if (g_device == nullptr || g_window == nullptr)
    {
        return;
    }

    const SDL_GPUSwapchainComposition wanted = ResolveHdrComposition();
    if (wanted == g_hdr_composition &&
        g_swapchain_format != SDL_GPU_TEXTUREFORMAT_INVALID)
    {
        return;
    }

    if (!SDL_SetGPUSwapchainParameters(g_device, g_window, wanted,
                                       g_present_mode))
    {
        LogSdlError("SDL_SetGPUSwapchainParameters(composition)");
        if (wanted == SDL_GPU_SWAPCHAINCOMPOSITION_SDR)
        {
            return;
        }
        /* Put the swapchain back on something known-good rather than leaving
         * it in whatever state the failed call left behind. */
        if (!SDL_SetGPUSwapchainParameters(g_device, g_window,
                                           SDL_GPU_SWAPCHAINCOMPOSITION_SDR,
                                           g_present_mode))
        {
            LogSdlError("SDL_SetGPUSwapchainParameters(SDR recovery)");
            return;
        }
        g_hdr_composition = SDL_GPU_SWAPCHAINCOMPOSITION_SDR;
    }
    else
    {
        g_hdr_composition = wanted;
    }

    g_swapchain_format = SDL_GetGPUSwapchainTextureFormat(g_device, g_window);
    SDL_Log("MioPan SDL_GPU: swapchain composition %s",
            HdrCompositionName(g_hdr_composition));
}

bool HdrCompositionIsHdr(SDL_GPUSwapchainComposition composition)
{
    return composition == SDL_GPU_SWAPCHAINCOMPOSITION_HDR_EXTENDED_LINEAR ||
           composition == SDL_GPU_SWAPCHAINCOMPOSITION_HDR10_ST2084;
}

/* Whether the grade would change a single pixel.  All four at 1.0 is identity,
 * and an identity grade is what keeps a default SDR session off the present
 * pass entirely. */
bool GradeIsIdentity()
{
    return g_grade_brightness == 1.0f && g_grade_contrast == 1.0f &&
           g_grade_gamma == 1.0f && g_grade_saturation == 1.0f;
}

/*
 * Does this frame need the present pass?
 *
 * Two reasons, and they are independent: the swapchain is in an encoding the
 * game's own output is not in, or the player moved a grade slider.  Either one
 * costs one full-screen pass over the window; neither being true is the
 * default and costs nothing.
 *
 * The third clause is the awkward one.  If the scene was built for a format the
 * swapchain no longer has -- which is exactly what a runtime HDR toggle does --
 * the frame cannot be drawn into the swapchain directly and the pass is the
 * thing that bridges it.
 */
bool NeedsPresentPass()
{
    return HdrCompositionIsHdr(g_hdr_composition) || !GradeIsIdentity() ||
           (g_output_format != SDL_GPU_TEXTUREFORMAT_INVALID &&
            g_output_format != g_swapchain_format);
}

Uint32 AlignMeshArenaSize(size_t value)
{
    if (value == 0 ||
        value > (size_t)std::numeric_limits<Uint32>::max() -
                    (kMeshArenaAlignment - 1u))
    {
        return 0;
    }
    return (Uint32)((value + kMeshArenaAlignment - 1u) &
                    ~(size_t)(kMeshArenaAlignment - 1u));
}

size_t MeshArenaCapacityBytes()
{
    return g_mesh_vertex_arena.capacity_bytes +
           g_mesh_index_arena.capacity_bytes;
}

bool AddMeshArenaPage(MeshArena &arena, size_t minimum_size)
{
    Uint32 required = AlignMeshArenaSize(minimum_size);
    if (required == 0)
    {
        return false;
    }

    size_t page_size = std::max<size_t>(kMeshArenaPageBytes, required);
    const size_t remainder = page_size % kMeshArenaPageBytes;
    if (remainder != 0)
    {
        if (page_size > std::numeric_limits<Uint32>::max() -
                            (kMeshArenaPageBytes - remainder))
        {
            return false;
        }
        page_size += kMeshArenaPageBytes - remainder;
    }
    if (page_size > std::numeric_limits<Uint32>::max() ||
        MeshArenaCapacityBytes() > kMeshArenaBudgetBytes - page_size)
    {
        return false;
    }

    SDL_GPUBufferCreateInfo info{};
    info.usage = arena.usage;
    info.size = (Uint32)page_size;
    SDL_GPUBuffer *buffer = SDL_CreateGPUBuffer(g_device, &info);
    if (buffer == nullptr)
    {
        LogSdlError(arena.usage == SDL_GPU_BUFFERUSAGE_VERTEX
                        ? "SDL_CreateGPUBuffer(mesh vertex arena)"
                        : "SDL_CreateGPUBuffer(mesh index arena)");
        return false;
    }

    try
    {
        std::unique_ptr<MeshArenaPage> page(new MeshArenaPage{});
        page->buffer = buffer;
        page->size = (Uint32)page_size;
        page->free_ranges.reserve(kMeshCacheEntryLimit + 1u);
        page->free_ranges.push_back({0, (Uint32)page_size});
        arena.pages.push_back(std::move(page));
        arena.capacity_bytes += page_size;
    }
    catch (const std::bad_alloc &)
    {
        SDL_ReleaseGPUBuffer(g_device, buffer);
        return false;
    }
    return true;
}

bool AllocateMeshArenaSlice(MeshArena &arena, size_t byte_count,
                            MeshArenaSlice *out)
{
    if (out == nullptr)
    {
        return false;
    }
    *out = {};
    const Uint32 required = AlignMeshArenaSize(byte_count);
    if (required == 0)
    {
        return false;
    }

    MeshArenaPage *best_page = nullptr;
    size_t best_range = 0;
    Uint32 best_size = std::numeric_limits<Uint32>::max();
    for (const std::unique_ptr<MeshArenaPage> &page_ptr : arena.pages)
    {
        MeshArenaPage *page = page_ptr.get();
        for (size_t i = 0; i < page->free_ranges.size(); i++)
        {
            const MeshArenaRange &range = page->free_ranges[i];
            if (range.size >= required && range.size < best_size)
            {
                best_page = page;
                best_range = i;
                best_size = range.size;
            }
        }
    }
    if (best_page == nullptr)
    {
        /* Both arenas are fully reserved during renderer initialization.
         * Never put a driver buffer allocation back on a cold model's first
         * draw; promotion may evict an unpinned LRU entry and retry instead. */
        return false;
    }

    MeshArenaRange &range = best_page->free_ranges[best_range];
    out->page = best_page;
    out->offset = range.offset;
    out->size = required;
    range.offset += required;
    range.size -= required;
    if (range.size == 0)
    {
        best_page->free_ranges.erase(
            best_page->free_ranges.begin() + (ptrdiff_t)best_range);
    }
    return true;
}

void FreeMeshArenaSlice(MeshArenaSlice *slice)
{
    if (slice == nullptr || slice->page == nullptr || slice->size == 0)
    {
        return;
    }

    MeshArenaPage *page = slice->page;
    const uint64_t end = (uint64_t)slice->offset + slice->size;
    if (end > page->size)
    {
        *slice = {};
        return;
    }

    try
    {
        page->free_ranges.push_back({slice->offset, slice->size});
        std::sort(page->free_ranges.begin(), page->free_ranges.end(),
                  [](const MeshArenaRange &a, const MeshArenaRange &b) {
                      return a.offset < b.offset;
                  });
        size_t write = 0;
        for (const MeshArenaRange &range : page->free_ranges)
        {
            if (write != 0)
            {
                MeshArenaRange &previous = page->free_ranges[write - 1];
                if ((uint64_t)previous.offset + previous.size == range.offset)
                {
                    previous.size += range.size;
                    continue;
                }
            }
            page->free_ranges[write++] = range;
        }
        page->free_ranges.resize(write);
    }
    catch (const std::bad_alloc &)
    {
        /* Losing one free range is preferable to reusing a slice while a
         * custom shared_ptr deleter is unwinding.  The page is reclaimed at
         * renderer shutdown and the cache simply has slightly less capacity. */
    }
    *slice = {};
}

void ReleaseMeshArena(MeshArena &arena);

bool CreateMeshArenas()
{
    g_mesh_vertex_arena.usage = SDL_GPU_BUFFERUSAGE_VERTEX;
    g_mesh_index_arena.usage = SDL_GPU_BUFFERUSAGE_INDEX;
    if (!AddMeshArenaPage(g_mesh_vertex_arena,
                          kInitialMeshVertexArenaBytes) ||
        !AddMeshArenaPage(g_mesh_index_arena,
                          kInitialMeshIndexArenaBytes))
    {
        ReleaseMeshArena(g_mesh_vertex_arena);
        ReleaseMeshArena(g_mesh_index_arena);
        return false;
    }
    return true;
}

void ReleaseMeshArena(MeshArena &arena)
{
    if (g_device != nullptr)
    {
        for (const std::unique_ptr<MeshArenaPage> &page : arena.pages)
        {
            if (page != nullptr && page->buffer != nullptr)
            {
                SDL_ReleaseGPUBuffer(g_device, page->buffer);
            }
        }
    }
    arena.pages.clear();
    arena.capacity_bytes = 0;
}

void CollectInFlightMeshSubmissions()
{
    if (g_device == nullptr)
    {
        return;
    }
    for (size_t i = 0; i < g_inflight_mesh_submissions.size();)
    {
        InFlightMeshSubmission &submission =
            g_inflight_mesh_submissions[i];
        if (submission.fence == nullptr ||
            SDL_QueryGPUFence(g_device, submission.fence))
        {
            if (submission.fence != nullptr)
            {
                SDL_ReleaseGPUFence(g_device, submission.fence);
            }
            g_inflight_mesh_submissions.erase(
                g_inflight_mesh_submissions.begin() + (ptrdiff_t)i);
            continue;
        }
        i++;
    }
}

void ReleaseMeshCacheEntry(MeshCacheEntry *entry)
{
    if (entry == nullptr)
    {
        return;
    }

    /* Submission fences retain the entry until no encoded draw or upload can
     * still reference these shared arena ranges.  Only then may another mesh
     * reuse them. */
    FreeMeshArenaSlice(&entry->vertex_slice);
    FreeMeshArenaSlice(&entry->index_slice);

    if (entry->resident_bytes <= g_mesh_cache_bytes)
    {
        g_mesh_cache_bytes -= entry->resident_bytes;
    }
    else
    {
        g_mesh_cache_bytes = 0;
    }
    delete entry;
}

MeshCacheEntryPtr AdoptMeshCacheEntry(MeshCacheEntry *entry)
{
    if (entry == nullptr)
    {
        return {};
    }
    g_mesh_cache_bytes += entry->resident_bytes;
    return MeshCacheEntryPtr(entry, ReleaseMeshCacheEntry);
}

uint64_t MeshCacheOwnerGeneration(const void *owner)
{
    const auto generation = g_mesh_cache_owner_generations.find(owner);
    return generation != g_mesh_cache_owner_generations.end()
        ? generation->second : 0;
}

bool PreferMeshCacheVictim(const MeshCacheEntry &candidate,
                           const MeshCacheEntry &current)
{
    const bool candidate_stale = candidate.key.owner_generation !=
        MeshCacheOwnerGeneration(candidate.key.owner);
    const bool current_stale = current.key.owner_generation !=
        MeshCacheOwnerGeneration(current.key.owner);
    return candidate_stale != current_stale
        ? candidate_stale
        : candidate.last_used_frame < current.last_used_frame;
}

void TrimMeshCache()
{
    while (g_mesh_cache_bytes > kMeshCacheBudgetBytes ||
           g_mesh_cache.size() > kMeshCacheEntryLimit)
    {
        auto victim = g_mesh_cache.end();
        for (auto it = g_mesh_cache.begin(); it != g_mesh_cache.end(); ++it)
        {
            /* A queued DrawCommand owns a second reference.  SDL can defer a
             * released GPU buffer, but it cannot protect a command that has
             * not been encoded yet, so only map-owned entries are evictable. */
            if (it->second.use_count() != 1)
            {
                continue;
            }
            if (victim == g_mesh_cache.end() ||
                PreferMeshCacheVictim(*it->second, *victim->second))
            {
                victim = it;
            }
        }

        if (victim == g_mesh_cache.end())
        {
            break;
        }
        g_mesh_cache.erase(victim);
        g_mesh_cache_evictions++;
    }
}

bool AdmitMeshCacheEntry(size_t entry_bytes)
{
    if (entry_bytes == 0 || entry_bytes > kMeshCacheBudgetBytes ||
        g_mesh_cache_reserved_bytes >
            kMeshCacheBudgetBytes - entry_bytes)
    {
        return false;
    }

    const size_t available_after_reservations =
        kMeshCacheBudgetBytes - g_mesh_cache_reserved_bytes - entry_bytes;
    while (g_mesh_cache_bytes > available_after_reservations ||
           g_mesh_cache.size() + g_mesh_cache_build_reservations.size() >=
               kMeshCacheEntryLimit)
    {
        if (g_mesh_arena_evictions_this_frame >=
            kMeshArenaEvictionBudgetEntries)
        {
            return false;
        }
        auto victim = g_mesh_cache.end();
        for (auto it = g_mesh_cache.begin(); it != g_mesh_cache.end(); ++it)
        {
            if (it->second.use_count() != 1)
            {
                continue;
            }
            if (victim == g_mesh_cache.end() ||
                PreferMeshCacheVictim(*it->second, *victim->second))
            {
                victim = it;
            }
        }
        if (victim == g_mesh_cache.end())
        {
            return false;
        }
        g_mesh_cache.erase(victim);
        g_mesh_cache_evictions++;
        g_mesh_arena_evictions_this_frame++;
    }
    return true;
}

MeshCacheKey MakeMeshCacheKey(const void *owner, const void *vuvn,
                              const void *mesh,
                              unsigned int layout_kind)
{
    return {owner, vuvn, mesh, layout_kind,
            MeshCacheOwnerGeneration(owner)};
}

bool MeshCacheRetryBlocked(const MeshCacheKey &key)
{
    auto retry = g_mesh_cache_retry_after.find(key);
    if (retry == g_mesh_cache_retry_after.end())
    {
        return false;
    }
    if (g_mesh_cache_frame < retry->second)
    {
        return true;
    }
    g_mesh_cache_retry_after.erase(retry);
    return false;
}

void DisableMeshCacheEntries(const std::vector<MeshCacheEntryPtr> &entries)
{
    const uint64_t retry_frame =
        g_mesh_cache_frame + kMeshCacheFailureRetryFrames;
    for (const MeshCacheEntryPtr &entry : entries)
    {
        if (entry == nullptr)
        {
            continue;
        }
        const MeshCacheKey key = entry->key;
        try
        {
            g_mesh_cache_retry_after.insert_or_assign(key, retry_frame);
            g_mesh_cache_retry_expiries.push_back({key, retry_frame});
        }
        catch (const std::bad_alloc &)
        {
            /* Retry suppression is only a performance backoff.  If its
             * bookkeeping cannot be retained, prefer trying the cache again
             * over terminating during an already exceptional upload path. */
            g_mesh_cache_retry_after.erase(key);
        }
        auto cached = g_mesh_cache.find(key);
        if (cached != g_mesh_cache.end() && cached->second == entry)
        {
            g_mesh_cache.erase(cached);
        }
    }
    g_mesh_cache_upload_failures += (int)entries.size();
}

void ShutdownMeshCache()
{
    /* Registered after SDL_Init, so this runs while the device is still
     * alive.  Emptying the maps here also makes their static destructors inert
     * if SDL installs an earlier atexit cleanup of its own. */
    if (g_device != nullptr)
    {
        SDL_WaitForGPUIdle(g_device);
    }
    for (InFlightMeshSubmission &submission : g_inflight_mesh_submissions)
    {
        if (g_device != nullptr && submission.fence != nullptr)
        {
            SDL_ReleaseGPUFence(g_device, submission.fence);
        }
    }
    g_inflight_mesh_submissions.clear();
    g_draws.clear();
    /* After the draws, which hold references of their own: this drops the
     * last ones while the device is still alive. */
    g_resident_meshes.clear();
    g_pending_texture_uploads.clear();
    g_mesh_cache.clear();
    g_mesh_cache_retry_after.clear();
    g_mesh_cache_retry_expiries.clear();
    g_mesh_cache_build_reservations.clear();
    g_mesh_cache_owner_generations.clear();
    g_mesh_cache_reserved_bytes = 0;
    ReleaseMeshArena(g_mesh_vertex_arena);
    ReleaseMeshArena(g_mesh_index_arena);
    g_mesh_cache_available = false;
    if (g_device != nullptr && g_mesh_colour_buffer != nullptr)
    {
        SDL_ReleaseGPUBuffer(g_device, g_mesh_colour_buffer);
        g_mesh_colour_buffer = nullptr;
        g_mesh_colour_buffer_size = 0;
    }
    if (g_device != nullptr && g_animated_mesh_buffer != nullptr)
    {
        SDL_ReleaseGPUBuffer(g_device, g_animated_mesh_buffer);
        g_animated_mesh_buffer = nullptr;
        g_animated_mesh_buffer_size = 0;
    }
    g_animated_mesh_vertices_stream.clear();
    g_vertex_light_states.clear();
    g_fragment_light_states.clear();
    ReleaseDepthProbeResources();
    ReleaseCachedShaders();
    ReleasePresentResources();
}

void AbortActiveMeshStream()
{
    if (g_mesh_stream.token != 0 &&
        g_vertices.size() >= g_mesh_stream.first_vertex)
    {
        g_vertices.resize(g_mesh_stream.first_vertex);
    }
    g_mesh_stream = MeshStreamState{};
}

size_t AlignUploadOffset(size_t offset)
{
    return (offset + 15u) & ~size_t(15u);
}

/* Separate from g_lighting_mode_initialized, which SetLightingMode also raises.
 * Sharing them meant that once the config layer applied a saved lighting mode
 * through the setter, this returned early and MIOPAN_LIGHTING_MODE was silently
 * ignored -- the environment has to stay the outermost override. */
bool g_lighting_env_checked;

void InitLightingModeFromEnvironment()
{
    if (g_lighting_env_checked)
    {
        return;
    }
    g_lighting_env_checked = true;
    g_lighting_mode_initialized = true;

    const char *value = std::getenv("MIOPAN_LIGHTING_MODE");
    if (value == nullptr)
    {
        return;
    }
    if (SDL_strcasecmp(value, "fragment-all") == 0 ||
        SDL_strcasecmp(value, "all") == 0 || std::strcmp(value, "2") == 0)
    {
        g_lighting_mode = MIOPAN_LIGHTING_FRAGMENT_ALL;
    }
    else if (SDL_strcasecmp(value, "fragment") == 0 ||
             SDL_strcasecmp(value, "pixel") == 0 ||
             std::strcmp(value, "1") == 0)
    {
        g_lighting_mode = MIOPAN_LIGHTING_FRAGMENT;
    }
    else if (SDL_strcasecmp(value, "vertex") == 0 ||
             std::strcmp(value, "0") == 0)
    {
        g_lighting_mode = MIOPAN_LIGHTING_VERTEX;
    }
}

/*
 * Render-resolution overrides, same shape as the lighting mode above and for
 * the same reason: there is no settings file yet, and until there is this is
 * the only way to start the game in a mode other than the default.  Retire
 * these once the config layer can carry renderer.render.
 *
 *   MIOPAN_RENDER_RES     match | native | window   (default match)
 *   MIOPAN_RENDER_SCALE   0.25 .. 8.0                (default 1.0)
 *   MIOPAN_RENDER_FILTER  nearest | linear           (default linear)
 */
bool g_render_res_env_initialized;

void InitRenderResolutionFromEnvironment()
{
    if (g_render_res_env_initialized)
    {
        return;
    }
    g_render_res_env_initialized = true;

    const char *mode = std::getenv("MIOPAN_RENDER_RES");
    if (mode != nullptr)
    {
        if (SDL_strcasecmp(mode, "native") == 0 ||
            SDL_strcasecmp(mode, "ps2") == 0 || std::strcmp(mode, "1") == 0)
        {
            g_render_res_mode = MIOPAN_RENDER_RES_NATIVE_PS2;
        }
        else if (SDL_strcasecmp(mode, "window") == 0 ||
                 std::strcmp(mode, "2") == 0)
        {
            g_render_res_mode = MIOPAN_RENDER_RES_WINDOW_SCALE;
        }
        else
        {
            g_render_res_mode = MIOPAN_RENDER_RES_MATCH_WINDOW;
        }
    }

    const char *scale = std::getenv("MIOPAN_RENDER_SCALE");
    if (scale != nullptr)
    {
        /* Through the setter so one clamp serves the env var, the UI and the
         * config layer alike. */
        MioPan_RendererSetRenderResolution(g_render_res_mode,
                                           (float)std::atof(scale));
    }

    const char *filter = std::getenv("MIOPAN_RENDER_FILTER");
    if (filter != nullptr)
    {
        g_upscale_filter = (SDL_strcasecmp(filter, "nearest") == 0 ||
                            std::strcmp(filter, "0") == 0)
                               ? MIOPAN_RENDER_FILTER_NEAREST
                               : MIOPAN_RENDER_FILTER_LINEAR;
    }

    /* MIOPAN_ASPECT: auto | original | 4:3 | 16:9 | 16:10 | <ratio> */
    const char *aspect = std::getenv("MIOPAN_ASPECT");
    if (aspect != nullptr)
    {
        if (SDL_strcasecmp(aspect, "original") == 0 ||
            SDL_strcasecmp(aspect, "ps2") == 0)
        {
            g_aspect_mode = MIOPAN_ASPECT_ORIGINAL;
        }
        else if (std::strcmp(aspect, "4:3") == 0 ||
                 std::strcmp(aspect, "4_3") == 0)
        {
            g_aspect_mode = MIOPAN_ASPECT_4_3;
        }
        else if (std::strcmp(aspect, "16:9") == 0 ||
                 std::strcmp(aspect, "16_9") == 0)
        {
            g_aspect_mode = MIOPAN_ASPECT_16_9;
        }
        else if (std::strcmp(aspect, "16:10") == 0 ||
                 std::strcmp(aspect, "16_10") == 0)
        {
            g_aspect_mode = MIOPAN_ASPECT_16_10;
        }
        else if (SDL_strcasecmp(aspect, "auto") == 0)
        {
            g_aspect_mode = MIOPAN_ASPECT_AUTO;
        }
        else
        {
            /* Anything else is a custom ratio, as either "W:H" or a bare
             * number.  Through the setter so the one clamp covers the env var,
             * the UI and the config layer alike.  NB: adding an override here
             * means adding its name to EnvOverrideActive() in miopan_config.cpp
             * too, or a debug run will start writing itself into the settings
             * file. */
            const char *colon = std::strchr(aspect, ':');
            double w = std::atof(aspect);
            double h = colon != nullptr ? std::atof(colon + 1) : 1.0;
            MioPan_RendererSetAspectMode(
                MIOPAN_ASPECT_CUSTOM,
                (h > 0.0 && w > 0.0) ? (float)(w / h) : 0.0f);
        }
    }

    /* Recorded only -- the window does not exist yet.  EnsureRenderer() applies
     * it straight after SDL_CreateWindow, so a fullscreen start never shows a
     * windowed frame first. */
    const char *window_mode = std::getenv("MIOPAN_WINDOW_MODE");
    if (window_mode != nullptr)
    {
        g_window_mode = (SDL_strcasecmp(window_mode, "borderless") == 0 ||
                         SDL_strcasecmp(window_mode, "fullscreen") == 0 ||
                         std::strcmp(window_mode, "1") == 0)
                            ? MIOPAN_WINDOW_MODE_BORDERLESS
                            : MIOPAN_WINDOW_MODE_WINDOWED;
    }

    if (g_render_res_mode != MIOPAN_RENDER_RES_MATCH_WINDOW)
    {
        SDL_Log("MioPan SDL_GPU: render resolution mode %d, scale %.2f, "
                "%s upscale",
                g_render_res_mode, (double)g_render_res_scale,
                g_upscale_filter == MIOPAN_RENDER_FILTER_NEAREST ? "nearest"
                                                                 : "linear");
    }
}

/* ==========================================================================
 *  Point-visibility probes.
 *
 *  effect_sub.c's CheckPointDepth() wants to know whether scene geometry
 *  stands between the camera and a world point -- on hardware, by reading an
 *  8x1 strip of the GS Z buffer back over the GS->EE bus and comparing it with
 *  the point's own Z.  Neither half of that exists here: the depth buffer is an
 *  SDL_GPU texture, and nothing mirrors it into emulated GS memory.
 *
 *  So the comparison moves to this side, and stays in the renderer's OWN
 *  reversed-Z space (near = 1, far = 0, GEQUAL).  That is the whole reason this
 *  is a bridge rather than a memory mirror: mapping host float depth back into
 *  the PS2's 16-bit Z would be a second conversion to get wrong, and a wrong
 *  one would make ghosts vanish behind nothing -- worse than the always-visible
 *  behaviour it replaces.
 *
 *  Shape:
 *
 *    frame N    a query projects its point, compares against whatever frame
 *               N-1 read back, and registers its pixel
 *    frame N    end of frame: the bounding box of every registered pixel is
 *               downloaded in its own copy-only command buffer, with a fence
 *    frame N+1  if the fence has signalled, the transfer buffer is mapped and
 *               the probes take their new depths
 *
 *  Nothing waits on the GPU.  A probe with no answer reports
 *  MIOPAN_DEPTH_PROBE_UNAVAILABLE and the caller keeps the behaviour it had.
 * ====================================================================== */

/* One download per frame, not one per probe: a hundred 8x1 copies would each
 * pay D3D12's 256-byte row-pitch realignment.  Ghosts on screen cluster, so
 * their bounding box is normally small; it is capped, and a probe outside the
 * capped box simply has no answer that frame. */
constexpr int kDepthProbeRegionMax = 256;
/* Round the downloaded width up to this so the row pitch is a whole number of
 * 256-byte lines for a 4-byte format, which is what D3D12 wants and what keeps
 * SDL off its realignment path. */
constexpr int kDepthProbeRowAlign = 64;

struct DepthProbe
{
    /* Pixel registered this frame, in depth-texture space. */
    int  x = 0;
    int  y = 0;
    /* Reversed-Z depth of the point itself, from the same projection. */
    float depth = 0.0f;
    bool posted = false;
    /* Depth read back for this probe's pixel, and whether it is meaningful. */
    float read_depth = 0.0f;
    bool  read_valid = false;
};

DepthProbe g_depth_probes[MIOPAN_DEPTH_PROBE_SLOTS];
SDL_GPUTransferBuffer *g_depth_probe_transfer;
Uint32 g_depth_probe_transfer_size;
SDL_GPUFence *g_depth_probe_fence;
/* The region the in-flight download covers. */
int g_depth_probe_pending_rect[4];
/* The depth texture the in-flight download was issued against.  EnsureDepth
 * Texture() releases and recreates it on a resize, and a pending read-back
 * describes pixels of a surface that no longer exists -- so the answer is
 * dropped rather than decoded against the new geometry. */
SDL_GPUTexture *g_depth_probe_pending_texture;
Uint32 g_depth_probe_pending_w;
Uint32 g_depth_probe_pending_h;
bool g_depth_probe_supported = true;
bool g_depth_probe_logged;

void ReleaseDepthProbeResources()
{
    if (g_depth_probe_fence != nullptr)
    {
        SDL_WaitForGPUFences(g_device, true, &g_depth_probe_fence, 1);
        SDL_ReleaseGPUFence(g_device, g_depth_probe_fence);
        g_depth_probe_fence = nullptr;
    }
    if (g_depth_probe_transfer != nullptr)
    {
        SDL_ReleaseGPUTransferBuffer(g_device, g_depth_probe_transfer);
        g_depth_probe_transfer = nullptr;
        g_depth_probe_transfer_size = 0;
    }
    for (DepthProbe &probe : g_depth_probes)
    {
        probe = DepthProbe{};
    }
}

/* Take the previous frame's download, if the GPU has finished with it.  Called
 * once at the top of a frame, before any query. */
void ResolveDepthProbes()
{
    if (g_depth_probe_fence == nullptr)
    {
        return;
    }
    if (!SDL_QueryGPUFence(g_device, g_depth_probe_fence))
    {
        /* Still in flight.  Keep last frame's answers rather than blocking --
         * they are one frame older, which is exactly the tolerance this whole
         * path is built on. */
        return;
    }

    SDL_ReleaseGPUFence(g_device, g_depth_probe_fence);
    g_depth_probe_fence = nullptr;

    if (g_depth_probe_pending_texture != g_depth_texture ||
        g_depth_probe_pending_w != g_depth_width ||
        g_depth_probe_pending_h != g_depth_height)
    {
        /* The buffer was resized while this was in flight.  Every probe's
         * pixel was measured against the old one, so throw the frame away. */
        for (DepthProbe &probe : g_depth_probes)
        {
            probe.read_valid = false;
        }
        return;
    }

    const int rx = g_depth_probe_pending_rect[0];
    const int ry = g_depth_probe_pending_rect[1];
    const int rw = g_depth_probe_pending_rect[2];
    const int rh = g_depth_probe_pending_rect[3];
    if (rw <= 0 || rh <= 0 || g_depth_probe_transfer == nullptr)
    {
        return;
    }

    const float *pixels = (const float *)SDL_MapGPUTransferBuffer(
        g_device, g_depth_probe_transfer, false);
    if (pixels == nullptr)
    {
        LogSdlError("SDL_MapGPUTransferBuffer(depth probe)");
        return;
    }

    const int pitch = (rw + kDepthProbeRowAlign - 1) /
                      kDepthProbeRowAlign * kDepthProbeRowAlign;
    for (DepthProbe &probe : g_depth_probes)
    {
        probe.read_valid = false;
        if (!probe.posted)
        {
            continue;
        }
        const int px = probe.x - rx;
        const int py = probe.y - ry;
        if (px < 0 || py < 0 || px >= rw || py >= rh)
        {
            continue;                 /* outside the box that was downloaded */
        }
        probe.read_depth = pixels[(size_t)py * (size_t)pitch + (size_t)px];
        probe.read_valid = true;
    }

    SDL_UnmapGPUTransferBuffer(g_device, g_depth_probe_transfer);
}

/* Issue this frame's download.  Its own command buffer, submitted after the
 * frame's: the render pass has ended, so the depth texture is no longer being
 * written and a copy pass may read it. */
void RecordDepthProbeDownload()
{
    if (!g_depth_probe_supported || g_depth_probe_fence != nullptr ||
        g_depth_texture == nullptr || g_depth_width == 0 ||
        g_depth_height == 0)
    {
        return;
    }
    /*
     * PORT LIMIT, and the one thing MSAA costs beyond bandwidth: a
     * multisampled texture cannot be downloaded, and SDL_GPU resolves colour
     * targets only -- there is no depth resolve to read instead.  So while
     * MSAA is on this probe stands down and MioPan_RendererQueryPointOccluded()
     * answers UNAVAILABLE, which effect_sub.o's CheckPointDepth() already
     * treats as "not occluded".  A ghost stays a finder target through a wall
     * it would otherwise be hidden by -- exactly the degradation the port had
     * before the probe existed.  Not latched: turning MSAA off restores it on
     * the next frame.
     */
    if (g_depth_samples > 1)
    {
        /* Drop the stored answers rather than leaving them.  Nothing else
         * clears them -- ResolveDepthProbes() returns early when there is no
         * fence -- so without this the last read-back before MSAA came on
         * would keep being reported as this frame's. */
        for (DepthProbe &probe : g_depth_probes)
        {
            probe.read_valid = false;
        }
        return;
    }
    const int depth_w = (int)g_depth_width;
    const int depth_h = (int)g_depth_height;

    /* Only D32_FLOAT is decoded here.  The others are 24- or 16-bit normalised
     * and would each need their own unpack; reversed-Z already wants
     * D32_FLOAT, and the renderer logs a warning at startup when it does not
     * get it. */
    if (g_depth_format != SDL_GPU_TEXTUREFORMAT_D32_FLOAT)
    {
        if (!g_depth_probe_logged)
        {
            g_depth_probe_logged = true;
            SDL_Log("MioPan SDL_GPU: point-visibility probes need a "
                    "D32_FLOAT depth buffer; occlusion tests will report "
                    "\"cannot tell\"");
        }
        g_depth_probe_supported = false;
        return;
    }

    int min_x = std::numeric_limits<int>::max();
    int min_y = std::numeric_limits<int>::max();
    int max_x = std::numeric_limits<int>::min();
    int max_y = std::numeric_limits<int>::min();
    bool any = false;
    for (const DepthProbe &probe : g_depth_probes)
    {
        if (!probe.posted)
        {
            continue;
        }
        any = true;
        min_x = std::min(min_x, probe.x);
        min_y = std::min(min_y, probe.y);
        max_x = std::max(max_x, probe.x);
        max_y = std::max(max_y, probe.y);
    }
    if (!any)
    {
        return;
    }

    int rw = std::min(max_x - min_x + 1, (int)kDepthProbeRegionMax);
    int rh = std::min(max_y - min_y + 1, (int)kDepthProbeRegionMax);
    int rx = std::max(std::min(min_x, depth_w - rw), 0);
    int ry = std::max(std::min(min_y, depth_h - rh), 0);
    rw = std::min(rw, depth_w - rx);
    rh = std::min(rh, depth_h - ry);
    if (rw <= 0 || rh <= 0)
    {
        return;
    }

    const int pitch = (rw + kDepthProbeRowAlign - 1) /
                      kDepthProbeRowAlign * kDepthProbeRowAlign;
    const Uint32 needed = (Uint32)pitch * (Uint32)rh * 4u;
    if (g_depth_probe_transfer == nullptr ||
        g_depth_probe_transfer_size < needed)
    {
        if (g_depth_probe_transfer != nullptr)
        {
            SDL_ReleaseGPUTransferBuffer(g_device, g_depth_probe_transfer);
            g_depth_probe_transfer = nullptr;
            g_depth_probe_transfer_size = 0;
        }
        SDL_GPUTransferBufferCreateInfo info{};
        info.usage = SDL_GPU_TRANSFERBUFFERUSAGE_DOWNLOAD;
        info.size = needed;
        g_depth_probe_transfer = SDL_CreateGPUTransferBuffer(g_device, &info);
        if (g_depth_probe_transfer == nullptr)
        {
            LogSdlError("SDL_CreateGPUTransferBuffer(depth probe)");
            g_depth_probe_supported = false;
            return;
        }
        g_depth_probe_transfer_size = needed;
    }

    SDL_GPUCommandBuffer *cmd = SDL_AcquireGPUCommandBuffer(g_device);
    if (cmd == nullptr)
    {
        LogSdlError("SDL_AcquireGPUCommandBuffer(depth probe)");
        return;
    }
    SDL_GPUCopyPass *pass = SDL_BeginGPUCopyPass(cmd);
    if (pass == nullptr)
    {
        LogSdlError("SDL_BeginGPUCopyPass(depth probe)");
        SDL_SubmitGPUCommandBuffer(cmd);
        return;
    }

    SDL_GPUTextureRegion source{};
    source.texture = g_depth_texture;
    source.x = (Uint32)rx;
    source.y = (Uint32)ry;
    source.w = (Uint32)rw;
    source.h = (Uint32)rh;
    source.d = 1;

    SDL_GPUTextureTransferInfo destination{};
    destination.transfer_buffer = g_depth_probe_transfer;
    destination.offset = 0;
    destination.pixels_per_row = (Uint32)pitch;
    destination.rows_per_layer = (Uint32)rh;

    SDL_DownloadFromGPUTexture(pass, &source, &destination);
    SDL_EndGPUCopyPass(pass);

    SDL_GPUFence *fence = SDL_SubmitGPUCommandBufferAndAcquireFence(cmd);
    if (fence == nullptr)
    {
        /* Without a fence there is no safe moment to map the buffer.  Give up
         * on probing rather than read data the GPU may still be writing. */
        LogSdlError("SDL_SubmitGPUCommandBufferAndAcquireFence(depth probe)");
        g_depth_probe_supported = false;
        return;
    }

    g_depth_probe_fence = fence;
    g_depth_probe_pending_texture = g_depth_texture;
    g_depth_probe_pending_w = g_depth_width;
    g_depth_probe_pending_h = g_depth_height;
    g_depth_probe_pending_rect[0] = rx;
    g_depth_probe_pending_rect[1] = ry;
    g_depth_probe_pending_rect[2] = rw;
    g_depth_probe_pending_rect[3] = rh;
}

void ClearDepthProbePostings()
{
    for (DepthProbe &probe : g_depth_probes)
    {
        probe.posted = false;
    }
}

/* The caller has already decided whether this draw wants a per-pixel pass and
 * which terms it covers -- BuildFragmentLightState() in miopan_graph3d.cpp is
 * the one place that reads the lighting mode.  A NULL source, or a term mask of
 * zero, means the CPU did all of it and the draw takes the plain sprite.frag
 * path.
 *
 * Interns the block rather than copying it into the command; see
 * DrawCommand::fragment_light_index. */
void SnapshotFragmentLights(DrawCommand &command,
                            const MioPanLightState *source)
{
    command.fragment_lighting = source != nullptr && source->config[2] != 0;
    command.fragment_light_index = 0;
    if (!command.fragment_lighting)
    {
        return;
    }

    if (!g_fragment_light_states.empty() &&
        std::memcmp(&g_fragment_light_states.back(), source,
                    sizeof(*source)) == 0)
    {
        command.fragment_light_index =
            (Uint32)(g_fragment_light_states.size() - 1u);
        return;
    }

    if (g_fragment_light_states.size() >=
        (size_t)std::numeric_limits<Uint32>::max())
    {
        command.fragment_lighting = false;
        return;
    }

    command.fragment_light_index = (Uint32)g_fragment_light_states.size();
    g_fragment_light_states.push_back(*source);
}

void PumpEvents()
{
    SDL_Event event;

    while (SDL_PollEvent(&event))
    {
        MioPanUi::ProcessEvent(&event);

        if (event.type == SDL_EVENT_QUIT)
        {
            std::exit(0);
        }

        /* A pad plugged in or pulled out.  The input layer picks the device by
         * GUID, so it has to re-run that choice rather than keep whatever was
         * open -- and a hotplug handled here lands the same frame instead of
         * waiting for the next poll to notice. */
        if (event.type == SDL_EVENT_GAMEPAD_ADDED ||
            event.type == SDL_EVENT_GAMEPAD_REMOVED)
        {
            MioPan_InputRefreshDevices();
        }

        /* Alt+Enter, the platform convention.  Not gated on ImGui's keyboard
         * capture: it is a window-manager action rather than input the game or
         * the overlay competes for, and `repeat` is what stops a held Return
         * flipping the mode every frame. */
        if (event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat &&
            (event.key.key == SDLK_RETURN || event.key.key == SDLK_KP_ENTER) &&
            (event.key.mod & SDL_KMOD_ALT) != 0)
        {
            MioPan_RendererToggleFullscreen();
        }

        /* The display's HDR state moved -- the window was dragged to another
         * monitor, or the player turned HDR on or off in the OS.  Re-reading
         * here rather than polling is what lets AUTO follow it, and what keeps
         * an auto paper white pinned to the OS's current SDR level. */
        if (event.type == SDL_EVENT_WINDOW_HDR_STATE_CHANGED)
        {
            RefreshDisplayHdrInfo();
        }

        /* Latch the windowed size so the config can persist it.  Gated on the
         * window not being fullscreen, because going borderless also produces a
         * resize event -- and storing the desktop size from it is exactly how a
         * window forgets the shape it should come back to.  Screen coordinates,
         * not pixels: this is fed back to SDL_CreateWindow, which takes them. */
        if (event.type == SDL_EVENT_WINDOW_RESIZED &&
            g_window_mode == MIOPAN_WINDOW_MODE_WINDOWED && g_window != nullptr)
        {
            int width = 0;
            int height = 0;
            if (SDL_GetWindowSize(g_window, &width, &height) &&
                width > 0 && height > 0)
            {
                g_windowed_width = width;
                g_windowed_height = height;
            }
        }
    }
}

void InitDefaultUniforms()
{
    std::memset(&g_uniforms, 0, sizeof(g_uniforms));
    if (!g_3d_camera_valid)
    {
        SetIdentityMatrix(g_3d_view);
        SetIdentityMatrix(g_3d_projection);
        SetIdentityMatrix(g_3d_view_projection);
    }

    for (int i = 0; i < 4; i++)
    {
        g_uniforms.model[i * 4 + i] = 1.0f;
        g_uniforms.view[i * 4 + i] = 1.0f;
        g_uniforms.projection[i * 4 + i] = 1.0f;
        g_uniforms.mvp[i * 4 + i] = 1.0f;
        g_uniforms.modelView[i * 4 + i] = 1.0f;
        g_uniforms.viewProj[i * 4 + i] = 1.0f;
        g_uniforms.shadowMatrix[i * 4 + i] = 1.0f;
        g_uniforms.worldClipView[i * 4 + i] = 1.0f;
    }

    /* gra3dInit can apply the initial camera before SDL creates the renderer.
     * Preserve that pending state instead of replacing it with identities on
     * the first BeginFrame. */
    if (g_3d_camera_valid)
    {
        std::memcpy(g_uniforms.view, g_3d_view, sizeof(g_uniforms.view));
        std::memcpy(g_uniforms.projection, g_3d_projection,
                    sizeof(g_uniforms.projection));
        std::memcpy(g_uniforms.viewProj, g_3d_view_projection,
                    sizeof(g_uniforms.viewProj));
    }

    g_uniforms.normalMatrix[0] = 1.0f;
    g_uniforms.normalMatrix[5] = 1.0f;
    g_uniforms.normalMatrix[10] = 1.0f;
    g_uniforms.viewNormalMatrix[0] = 1.0f;
    g_uniforms.viewNormalMatrix[5] = 1.0f;
    g_uniforms.viewNormalMatrix[10] = 1.0f;

    g_uniforms.color[0] = 1.0f;
    g_uniforms.color[1] = 1.0f;
    g_uniforms.color[2] = 1.0f;
    g_uniforms.color[3] = 1.0f;
    g_uniforms.textureSize[0] = 1.0f;
    g_uniforms.textureSize[1] = 1.0f;
    g_uniforms.outputSize[0] = (float)kLogicalWidth;
    g_uniforms.outputSize[1] = (float)kLogicalHeight;
    g_uniforms.photoNegativeContentRect[2] = 1.0f;
    g_uniforms.photoNegativeContentRect[3] = 1.0f;
    g_uniforms.photoNegativeRect[2] = 1.0f;
    g_uniforms.photoNegativeRect[3] = 1.0f;
    g_uniforms.framebufferUvScale[0] = 1.0f;
    g_uniforms.framebufferUvScale[1] = 1.0f;
    g_uniforms.framebufferContentUvMax[0] = 1.0f;
    g_uniforms.framebufferContentUvMax[1] = 1.0f;
    g_uniforms.renderSize[0] = (float)kLogicalWidth;
    g_uniforms.renderSize[1] = (float)kLogicalHeight;
    g_uniforms.screenNegative[0] = 0.5f;
    g_uniforms.screenNegative[1] = 0.5f;
    g_uniforms.screenNegative[2] = 0.5f;
    g_uniforms.hdrOutput[0] = 1.0f;
    g_uniforms.hdrOutput[1] = 1.0f;
    g_uniforms.params0[1] = 0.6f;
    g_uniforms.params0[2] = 1.0f;
    g_uniforms.params0[3] = 1.0f;
    g_uniforms.params1[2] = 1.0f;
    g_uniforms.params1[3] = 1.0f;
    g_uniforms.ps2Feedback[2] = 1.0f;
}

const ShaderFormatInfo *PickShaderFormat()
{
    SDL_GPUShaderFormat supported = SDL_GetGPUShaderFormats(g_device);
    for (const ShaderFormatInfo &format : kShaderFormats)
    {
        if ((supported & format.format) != 0)
        {
            return &format;
        }
    }
    return nullptr;
}

bool BuildShaderPath(const char *name, const ShaderFormatInfo *format,
                     char *out, size_t out_size)
{
    char relative[256];
    int written = std::snprintf(relative, sizeof(relative),
                                "resources/shaders/%s/%s%s", format->dir,
                                name, format->extension);
    if (written < 0 || written >= (int)sizeof(relative))
    {
        return false;
    }

    return MioPan_PathResolveAsset(relative, out, out_size) != 0;
}

bool ReadWholeFile(const char *path, std::vector<Uint8> *out)
{
    int64_t size = MioPan_FileSize(path);
    if (size <= 0 || size > std::numeric_limits<int>::max())
    {
        return false;
    }

    out->resize((size_t)size);
    return MioPan_FileReadAt(path, 0, out->data(), out->size()) != 0;
}

/*
 * `uniform_count` is the number of uniform slots the stage actually declares,
 * and it has to be exact rather than generous: SDL builds the pipeline's
 * resource layout from it and then expects every slot in that layout to have
 * been pushed before a draw.  Three is right for everything in
 * g_cached_shaders -- they all include mikupan_common.hlsli, which declares the
 * uniform, light and material blocks -- and wrong for the present pass, whose
 * vertex stage declares none and whose fragment stage declares one.
 */
SDL_GPUShader *LoadShader(const char *name, SDL_GPUShaderStage stage,
                          Uint32 sampler_count, Uint32 uniform_count = 3)
{
    SDL_Log("Loading shader %s\n", name);

    const ShaderFormatInfo *format = PickShaderFormat();
    if (format == nullptr)
    {
        SDL_Log("MioPan SDL_GPU: no supported shipped shader format");
        return nullptr;
    }

    char path[1024];
    if (!BuildShaderPath(name, format, path, sizeof(path)))
    {
        SDL_Log("MioPan SDL_GPU: missing shader bytecode for %s", name);
        return nullptr;
    }

    std::vector<Uint8> bytecode;
    if (!ReadWholeFile(path, &bytecode))
    {
        SDL_Log("MioPan SDL_GPU: could not read shader bytecode %s", path);
        return nullptr;
    }

    SDL_GPUShaderCreateInfo info{};
    info.code_size = bytecode.size();
    info.code = bytecode.data();
    info.entrypoint = format->entrypoint;
    info.format = format->format;
    info.stage = stage;
    info.num_samplers = sampler_count;
    info.num_storage_textures = 0;
    info.num_storage_buffers = 0;
    info.num_uniform_buffers = uniform_count;

    SDL_GPUShader *shader = SDL_CreateGPUShader(g_device, &info);
    if (shader == nullptr)
    {
        SDL_Log("MioPan SDL_GPU: shader %s failed: %s", path, SDL_GetError());
    }
    return shader;
}

SDL_GPUShader *GetCachedShader(const char *name, SDL_GPUShaderStage stage,
                               Uint32 sampler_count)
{
    for (CachedShaderInfo &cached : g_cached_shaders)
    {
        if (cached.stage == stage && cached.sampler_count == sampler_count &&
            std::strcmp(cached.name, name) == 0)
        {
            if (cached.shader == nullptr)
            {
                cached.shader = LoadShader(name, stage, sampler_count);
            }
            return cached.shader;
        }
    }

    SDL_Log("MioPan SDL_GPU: uncached pipeline shader %s", name);
    return nullptr;
}

void ReleaseCachedShaders()
{
    if (g_device == nullptr)
    {
        return;
    }
    for (CachedShaderInfo &cached : g_cached_shaders)
    {
        if (cached.shader != nullptr)
        {
            SDL_ReleaseGPUShader(g_device, cached.shader);
            cached.shader = nullptr;
        }
    }
}

SDL_GPUTextureFormat PickSupportedDepthFormat()
{
    const SDL_GPUTextureFormat candidates[] = {
        SDL_GPU_TEXTUREFORMAT_D32_FLOAT,
        SDL_GPU_TEXTUREFORMAT_D24_UNORM,
        SDL_GPU_TEXTUREFORMAT_D16_UNORM,
    };

    for (SDL_GPUTextureFormat candidate : candidates)
    {
        if (SDL_GPUTextureSupportsFormat(
                g_device, candidate, SDL_GPU_TEXTURETYPE_2D,
                SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET))
        {
            return candidate;
        }
    }

    return SDL_GPU_TEXTUREFORMAT_D16_UNORM;
}

/* One quantisation step of the depth buffer in force, in NDC.  A bias smaller
 * than this cannot survive the write, whatever the ROM asked for. */
float DepthUnitSize(void)
{
    switch (g_depth_format)
    {
    case SDL_GPU_TEXTUREFORMAT_D16_UNORM:
        return 1.0f / 65535.0f;
    case SDL_GPU_TEXTUREFORMAT_D24_UNORM:
        return 1.0f / 16777215.0f;
    case SDL_GPU_TEXTUREFORMAT_D32_FLOAT:
    default:
        /* Reversed-Z puts distant geometry near 0 where float is dense, so
         * this is a conservative floor rather than a real quantum. */
        return 1.0f / 16777215.0f;
    }
}

const char *DepthFormatName(SDL_GPUTextureFormat format)
{
    switch (format)
    {
    case SDL_GPU_TEXTUREFORMAT_D32_FLOAT:
        return "D32_FLOAT";
    case SDL_GPU_TEXTUREFORMAT_D24_UNORM:
        return "D24_UNORM";
    case SDL_GPU_TEXTUREFORMAT_D16_UNORM:
        return "D16_UNORM";
    default:
        return "unknown";
    }
}

/* The colour formats this renderer can actually end up with -- the swapchain's,
 * and the scene target's, which is the same thing plus the 10-bit HDR case. */
const char *ColorFormatName(SDL_GPUTextureFormat format)
{
    switch (format)
    {
    case SDL_GPU_TEXTUREFORMAT_B8G8R8A8_UNORM:
        return "B8G8R8A8_UNORM";
    case SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM:
        return "R8G8B8A8_UNORM";
    case SDL_GPU_TEXTUREFORMAT_B8G8R8A8_UNORM_SRGB:
        return "B8G8R8A8_UNORM_SRGB";
    case SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM_SRGB:
        return "R8G8B8A8_UNORM_SRGB";
    case SDL_GPU_TEXTUREFORMAT_R10G10B10A2_UNORM:
        return "R10G10B10A2_UNORM";
    case SDL_GPU_TEXTUREFORMAT_R16G16B16A16_FLOAT:
        return "R16G16B16A16_FLOAT";
    default:
        return "unknown";
    }
}

const char *PresentModeName(SDL_GPUPresentMode mode)
{
    switch (mode)
    {
    case SDL_GPU_PRESENTMODE_IMMEDIATE:
        return "immediate";
    case SDL_GPU_PRESENTMODE_MAILBOX:
        return "mailbox";
    case SDL_GPU_PRESENTMODE_VSYNC:
        return "vsync";
    default:
        return "unknown";
    }
}

/*
 * The GS equations of GsBlendMode, expressed as SDL blend factors.  Only the
 * colour channels vary: the GS's ALPHA register governs RGB only, and the
 * framebuffer alpha the swapchain never shows is left on the original
 * source-alpha rule so the modes stay comparable.
 */
SDL_GPUColorTargetBlendState BlendStateForMode(GsBlendMode mode)
{
    SDL_GPUColorTargetBlendState blend{};
    blend.color_blend_op = SDL_GPU_BLENDOP_ADD;
    blend.src_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
    blend.dst_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
    blend.alpha_blend_op = SDL_GPU_BLENDOP_ADD;
    blend.color_write_mask = SDL_GPU_COLORCOMPONENT_R | SDL_GPU_COLORCOMPONENT_G |
                             SDL_GPU_COLORCOMPONENT_B | SDL_GPU_COLORCOMPONENT_A;
    blend.enable_blend = true;
    blend.enable_color_write_mask = true;

    switch (mode)
    {
    case GS_BLEND_ADD:                                  /* Cs*As + Cd */
        blend.src_color_blendfactor = SDL_GPU_BLENDFACTOR_SRC_ALPHA;
        blend.dst_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
        break;
    case GS_BLEND_ADD_ONE:                              /* Cs + Cd */
        blend.src_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
        blend.dst_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
        break;
    case GS_BLEND_SUB:                                  /* Cs*As - Cd*As */
        blend.src_color_blendfactor = SDL_GPU_BLENDFACTOR_SRC_ALPHA;
        blend.dst_color_blendfactor = SDL_GPU_BLENDFACTOR_SRC_ALPHA;
        blend.color_blend_op = SDL_GPU_BLENDOP_SUBTRACT;
        break;
    case GS_BLEND_DST_DECAY:                            /* Cd*(1-As) */
        blend.src_color_blendfactor = SDL_GPU_BLENDFACTOR_ZERO;
        blend.dst_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
        break;
    case GS_BLEND_SRC_ONLY:                             /* Cs*As */
        blend.src_color_blendfactor = SDL_GPU_BLENDFACTOR_SRC_ALPHA;
        blend.dst_color_blendfactor = SDL_GPU_BLENDFACTOR_ZERO;
        break;
    case GS_BLEND_DST_ADD:                              /* Cd*As + Cd */
        /* Cv = Cd*(1 + Cs): the As/128 weight arrives in the fragment's
         * colour, put there by the untextured-quad path -- see
         * MioPan_RendererDrawSolidQuad(). */
        blend.src_color_blendfactor = SDL_GPU_BLENDFACTOR_DST_COLOR;
        blend.dst_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
        break;
    case GS_BLEND_ALPHA:
    default:                                            /* Cs*As + Cd*(1-As) */
        blend.src_color_blendfactor = SDL_GPU_BLENDFACTOR_SRC_ALPHA;
        blend.dst_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
        break;
    }

    return blend;
}

/*
 * Tag a draw with whichever shadow pass gra3dShadow.c is currently inside.
 * Both counters are raised around a _gra3dDrawSGD() call, and that walker is
 * what ultimately queues the draw, so the tag reaches every mesh the pass
 * produced without the shadow code having to know how geometry is submitted.
 */
void TagShadowPass(DrawCommand &command)
{
    if (g_shadow_current_episode < 0 ||
        g_shadow_current_episode >= (int)g_shadow_episodes.size())
    {
        return;
    }
    command.shadow_caster = g_shadow_caster_depth > 0;
    command.shadow_receiver = g_shadow_receiver_depth > 0;
    if (!command.shadow_caster && !command.shadow_receiver)
    {
        return;
    }
    command.shadow_episode = g_shadow_current_episode;
    if (command.shadow_caster)
    {
        g_shadow_episodes[g_shadow_current_episode].have_casters = true;
        g_shadow_stat_casters++;
    }
    else
    {
        g_shadow_stat_receivers++;
    }
}

/*
 * The vertex input state for one mesh layout.  Shared by the material
 * pipelines and the projected-shadow ones, which replay the very same queued
 * draws and so must describe their buffers identically -- a copy here that
 * drifted from CreatePipelineVariant()'s would read one stream as another.
 *
 * Attribute locations are the same three in every layout (0 uv, 1 colour or
 * normal, 2 position); only which buffer they come out of changes, which is
 * why one shader can serve all three.
 */
void BuildMeshVertexInput(MeshPipelineLayout mesh_layout,
                          bool fragment_lighting,
                          SDL_GPUVertexBufferDescription buffer_desc[2],
                          SDL_GPUVertexAttribute attributes[4],
                          Uint32 *out_num_buffers,
                          Uint32 *out_num_attributes)
{
    if (mesh_layout == MESH_PIPELINE_CACHED_STATIC)
    {
        buffer_desc[0].slot = 0;
        buffer_desc[0].pitch = sizeof(MeshStaticVertex);
        buffer_desc[0].input_rate = SDL_GPU_VERTEXINPUTRATE_VERTEX;
        buffer_desc[1].slot = 1;
        buffer_desc[1].pitch = sizeof(MeshColour);
        buffer_desc[1].input_rate = SDL_GPU_VERTEXINPUTRATE_VERTEX;

        attributes[0].location = 0;
        attributes[0].buffer_slot = 0;
        attributes[0].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4;
        attributes[0].offset = offsetof(MeshStaticVertex, uv);
        attributes[1].location = 1;
        attributes[1].buffer_slot = 1;
        attributes[1].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4;
        attributes[1].offset = offsetof(MeshColour, rgba);
        attributes[2].location = 2;
        attributes[2].buffer_slot = 0;
        attributes[2].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4;
        attributes[2].offset = offsetof(MeshStaticVertex, position);
        if (fragment_lighting)
        {
            attributes[3].location = 3;
            attributes[3].buffer_slot = 0;
            attributes[3].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4;
            attributes[3].offset = offsetof(MeshStaticVertex, normal);
        }
    }
    else if (mesh_layout == MESH_PIPELINE_CACHED_ANIMATED)
    {
        buffer_desc[0].slot = 0;
        buffer_desc[0].pitch = sizeof(MeshAnimatedStaticVertex);
        buffer_desc[0].input_rate = SDL_GPU_VERTEXINPUTRATE_VERTEX;
        buffer_desc[1].slot = 1;
        buffer_desc[1].pitch = sizeof(AnimatedMeshVertex);
        buffer_desc[1].input_rate = SDL_GPU_VERTEXINPUTRATE_VERTEX;

        attributes[0].location = 0;
        attributes[0].buffer_slot = 0;
        attributes[0].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4;
        attributes[0].offset = offsetof(MeshAnimatedStaticVertex, uv);
        attributes[1].location = 1;
        attributes[1].buffer_slot = 1;
        attributes[1].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4;
        attributes[1].offset = offsetof(AnimatedMeshVertex, normal);
        attributes[2].location = 2;
        attributes[2].buffer_slot = 1;
        attributes[2].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4;
        attributes[2].offset = offsetof(AnimatedMeshVertex, position);
    }
    else
    {
        buffer_desc[0].slot = 0;
        buffer_desc[0].pitch = sizeof(SpriteVertex);
        buffer_desc[0].input_rate = SDL_GPU_VERTEXINPUTRATE_VERTEX;

        attributes[0].location = 0;
        attributes[0].buffer_slot = 0;
        attributes[0].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4;
        attributes[0].offset = offsetof(SpriteVertex, uv);
        attributes[1].location = 1;
        attributes[1].buffer_slot = 0;
        attributes[1].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4;
        attributes[1].offset = offsetof(SpriteVertex, colour);
        attributes[2].location = 2;
        attributes[2].buffer_slot = 0;
        attributes[2].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4;
        attributes[2].offset = offsetof(SpriteVertex, position);
    }

    *out_num_buffers = mesh_layout == MESH_PIPELINE_STREAMED ? 1 : 2;
    *out_num_attributes =
        fragment_lighting && mesh_layout == MESH_PIPELINE_CACHED_STATIC ? 4 : 3;
}

bool CreatePipelineVariant(SDL_GPUGraphicsPipeline **out_pipeline,
                           bool transform_mesh, bool enable_depth,
                           GsDepthCompare depth_compare,
                           bool enable_depth_write, bool wireframe,
                           GsBlendMode blend_mode,
                           MeshPipelineLayout mesh_layout,
                           bool fragment_lighting, int sample_count)
{
    fragment_lighting = fragment_lighting && transform_mesh &&
                        mesh_layout != MESH_PIPELINE_CACHED_ANIMATED;
    const char *vertex_shader_name =
        mesh_layout == MESH_PIPELINE_CACHED_ANIMATED
            ? "mesh_animated_lit.vert"
            : (transform_mesh ? "mesh.vert" : "sprite.vert");
    if (fragment_lighting)
    {
        vertex_shader_name = mesh_layout == MESH_PIPELINE_STREAMED
            ? "mesh_spot_streamed.vert" : "mesh_spot.vert";
    }
    SDL_GPUShader *vertex_shader = GetCachedShader(
        vertex_shader_name, SDL_GPU_SHADERSTAGE_VERTEX, 0);
    SDL_GPUShader *fragment_shader = GetCachedShader(
        fragment_lighting ? "mesh_spot.frag" : "sprite.frag",
        SDL_GPU_SHADERSTAGE_FRAGMENT, 2);
    if (vertex_shader == nullptr || fragment_shader == nullptr)
    {
        return false;
    }

    SDL_GPUVertexBufferDescription buffer_desc[2]{};
    SDL_GPUVertexAttribute attributes[4]{};
    Uint32 num_vertex_buffers = 0;
    Uint32 num_vertex_attributes = 0;
    BuildMeshVertexInput(mesh_layout, fragment_lighting, buffer_desc,
                         attributes, &num_vertex_buffers,
                         &num_vertex_attributes);

    SDL_GPUColorTargetBlendState blend = BlendStateForMode(blend_mode);

    SDL_GPUColorTargetDescription color_target{};
    color_target.format = g_scene_format;
    color_target.blend_state = blend;

    SDL_GPUGraphicsPipelineCreateInfo info{};
    info.vertex_shader = vertex_shader;
    info.fragment_shader = fragment_shader;
    info.vertex_input_state.vertex_buffer_descriptions = buffer_desc;
    info.vertex_input_state.num_vertex_buffers = num_vertex_buffers;
    info.vertex_input_state.vertex_attributes = attributes;
    info.vertex_input_state.num_vertex_attributes = num_vertex_attributes;
    info.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
    info.rasterizer_state.fill_mode =
        wireframe ? SDL_GPU_FILLMODE_LINE : SDL_GPU_FILLMODE_FILL;
    /* Screen-space quads use clockwise winding, while projected meshes use
     * counter-clockwise fronts.  Cull only the mesh variants so UI and
     * fullscreen passes cannot disappear.
     *
     * The test is the geometry kind, not the depth test.  It used to be
     * `enable_depth`, which held while the only depth-tested draws were
     * meshes; an effect particle billboard is depth-tested screen-space
     * geometry and would have been culled away entirely by that rule.  Culling
     * a quad is meaningless in any case -- the GS had no back-face cull at all,
     * VU1 did it per mesh -- so a sprite-shader pipeline never culls. */
    bool sprite_geometry =
        !transform_mesh && mesh_layout != MESH_PIPELINE_CACHED_ANIMATED;
    info.rasterizer_state.cull_mode =
        (enable_depth && !sprite_geometry) ? SDL_GPU_CULLMODE_BACK
                                           : SDL_GPU_CULLMODE_NONE;
    info.rasterizer_state.front_face = SDL_GPU_FRONTFACE_COUNTER_CLOCKWISE;
    info.rasterizer_state.enable_depth_clip = true;
    info.multisample_state.sample_count = SdlSampleCount(sample_count);
    info.depth_stencil_state.enable_depth_test = enable_depth;
    /* ZMSK in the GS ZBUF register: a depth-tested pass can still be asked not
     * to write, which is how transparent geometry avoids occluding whatever is
     * drawn behind it later in the frame. */
    info.depth_stencil_state.enable_depth_write = enable_depth && enable_depth_write;
    /* Reversed-Z: MioPan_Graph3dApplyCamera() hands us a projection with near
     * at w and far at 0, so nearer is greater -- which is how the GS ran too.
     * Pairs with the 0.0f depth clear in the render-pass setup.
     *
     * GEQUAL is the engine's own default (ClearDrawEnv writes ZTST 2) and the
     * only value any mesh draw uses; the other three arrive from 2D primitives
     * that use the depth buffer as a stencil. */
    switch (depth_compare)
    {
    case GS_DEPTH_NEVER:
        info.depth_stencil_state.compare_op = SDL_GPU_COMPAREOP_NEVER;
        break;
    case GS_DEPTH_ALWAYS:
        info.depth_stencil_state.compare_op = SDL_GPU_COMPAREOP_ALWAYS;
        break;
    case GS_DEPTH_GREATER:
        info.depth_stencil_state.compare_op = SDL_GPU_COMPAREOP_GREATER;
        break;
    case GS_DEPTH_GEQUAL:
    default:
        info.depth_stencil_state.compare_op = SDL_GPU_COMPAREOP_GREATER_OR_EQUAL;
        break;
    }
    info.target_info.color_target_descriptions = &color_target;
    info.target_info.num_color_targets = 1;
    info.target_info.has_depth_stencil_target = true;
    info.target_info.depth_stencil_format = g_depth_format;

    *out_pipeline = SDL_CreateGPUGraphicsPipeline(g_device, &info);

    if (*out_pipeline == nullptr)
    {
        LogSdlError("SDL_CreateGPUGraphicsPipeline");
        return false;
    }

    return true;
}

/*
 * Fetch a pipeline, building it the first time that exact combination is asked
 * for.  A creation failure falls back to the default-blend, depth-writing
 * variant that CreatePipelines() already proved works, so an exotic blend mode
 * degrades to the wrong equation rather than to a dropped draw -- the same
 * "err towards drawing" rule the alpha test follows.
 */
SDL_GPUGraphicsPipeline *GetPipeline(MeshPipelineLayout mesh_layout,
                                     bool transform_mesh, bool depth_test,
                                     GsDepthCompare depth_compare,
                                     bool depth_write, bool wireframe,
                                     GsBlendMode blend_mode,
                                     bool fragment_lighting)
{
    fragment_lighting = fragment_lighting && transform_mesh &&
                        mesh_layout != MESH_PIPELINE_CACHED_ANIMATED;
    /* A pipeline with depth testing disabled cannot write depth, and its
     * comparison is unobservable.  Canonicalize the otherwise-identical keys so
     * cold GS state cannot compile a duplicate depth-off pipeline.  The
     * write-on depth-off slots are part of the renderer's required startup
     * set. */
    if (!depth_test)
    {
        depth_write = true;
        depth_compare = GS_DEPTH_GEQUAL;
    }
    /* One frame's draws all key on the same count.  RecordFrame settles it
     * before the queue is replayed, so a setting change never lands between
     * two draws of one frame. */
    const int samples = g_pipeline_sample_count;
    const int sample_slot = SampleSlotForCount(samples);
    SDL_GPUGraphicsPipeline *&slot =
        g_pipeline_cache[sample_slot][mesh_layout][transform_mesh ? 1 : 0]
                        [depth_test ? 1 : 0][depth_compare]
                        [depth_write ? 1 : 0][wireframe ? 1 : 0]
                        [fragment_lighting ? 1 : 0][blend_mode];
    bool &failed =
        g_pipeline_failed[sample_slot][mesh_layout][transform_mesh ? 1 : 0]
                         [depth_test ? 1 : 0][depth_compare]
                         [depth_write ? 1 : 0][wireframe ? 1 : 0]
                         [fragment_lighting ? 1 : 0][blend_mode];
    if (slot != nullptr)
    {
        return slot;
    }

    if (!failed &&
        CreatePipelineVariant(&slot, transform_mesh, depth_test, depth_compare,
                              depth_write, wireframe, blend_mode, mesh_layout,
                              fragment_lighting, samples))
    {
        return slot;
    }

    failed = true;
    slot = nullptr;
    if (blend_mode == GS_BLEND_ALPHA && depth_write)
    {
        return nullptr;
    }
    return GetPipeline(mesh_layout, transform_mesh, depth_test, depth_compare,
                       true, wireframe, GS_BLEND_ALPHA, fragment_lighting);
}

/*
 * The two projected-shadow pipelines.  Both take the streamed mesh vertex
 * layout, because both replay draws the ordinary SGD walker queued: the caster
 * is what _RenderShadow() drew with SRT_REALTIME, and the receiver is what the
 * room draw queued for a block gra3dShadow.c registered.
 *
 * Neither goes through GetPipeline()'s cache -- their state is fixed, so a
 * seven-dimensional key would only ever hold one entry each.
 */
bool CreateShadowPipeline(SDL_GPUGraphicsPipeline **out_pipeline,
                          MeshPipelineLayout mesh_layout,
                          const char *vertex_shader_name,
                          const char *fragment_shader_name,
                          bool receiver, int sample_count)
{
    SDL_GPUShader *vertex_shader =
        GetCachedShader(vertex_shader_name, SDL_GPU_SHADERSTAGE_VERTEX, 0);
    SDL_GPUShader *fragment_shader =
        GetCachedShader(fragment_shader_name, SDL_GPU_SHADERSTAGE_FRAGMENT, 2);
    if (vertex_shader == nullptr || fragment_shader == nullptr)
    {
        return false;
    }

    SDL_GPUVertexBufferDescription buffer_desc[2]{};
    SDL_GPUVertexAttribute attributes[4]{};
    Uint32 num_vertex_buffers = 0;
    Uint32 num_vertex_attributes = 0;
    BuildMeshVertexInput(mesh_layout, false, buffer_desc, attributes,
                         &num_vertex_buffers, &num_vertex_attributes);

    SDL_GPUColorTargetDescription color_target{};
    color_target.format = g_scene_format;
    /* The receiver blends a dark fragment over the finished room, the way the
     * ROM's projection pass blends over what the room draw already laid down.
     * The caster writes an opaque silhouette into a cleared map, so it wants no
     * blending at all -- which is not one of the GS modes, hence the explicit
     * default-constructed state rather than BlendStateForMode(). */
    if (receiver)
    {
        color_target.blend_state = BlendStateForMode(GS_BLEND_ALPHA);
    }

    SDL_GPUGraphicsPipelineCreateInfo info{};
    info.vertex_shader = vertex_shader;
    info.fragment_shader = fragment_shader;
    info.vertex_input_state.vertex_buffer_descriptions = buffer_desc;
    info.vertex_input_state.num_vertex_buffers = num_vertex_buffers;
    info.vertex_input_state.vertex_attributes = attributes;
    info.vertex_input_state.num_vertex_attributes = num_vertex_attributes;
    info.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
    info.rasterizer_state.fill_mode = SDL_GPU_FILLMODE_FILL;
    /* The caster pass wants the silhouette, so both faces contribute and the
     * winding does not matter; the receiver matches the mesh pipelines. */
    info.rasterizer_state.cull_mode =
        receiver ? SDL_GPU_CULLMODE_BACK : SDL_GPU_CULLMODE_NONE;
    info.rasterizer_state.front_face = SDL_GPU_FRONTFACE_COUNTER_CLOCKWISE;
    info.rasterizer_state.enable_depth_clip = true;
    info.multisample_state.sample_count = SdlSampleCount(sample_count);
    info.target_info.color_target_descriptions = &color_target;
    info.target_info.num_color_targets = 1;

    if (receiver)
    {
        /* Reversed-Z, GEQUAL, and no depth write -- the shadow must not
         * occlude anything drawn after it. */
        info.depth_stencil_state.enable_depth_test = true;
        info.depth_stencil_state.enable_depth_write = false;
        info.depth_stencil_state.compare_op = SDL_GPU_COMPAREOP_GREATER_OR_EQUAL;
        info.target_info.has_depth_stencil_target = true;
        info.target_info.depth_stencil_format = g_depth_format;
    }
    else
    {
        /* No depth buffer on the shadow map: a silhouette is a coverage mask,
         * so overdraw between the caster's own faces is not observable. */
        info.target_info.has_depth_stencil_target = false;
    }

    *out_pipeline = SDL_CreateGPUGraphicsPipeline(g_device, &info);
    if (*out_pipeline == nullptr)
    {
        LogSdlError("SDL_CreateGPUGraphicsPipeline(shadow)");
        return false;
    }
    return true;
}

bool CreateShadowResources()
{
    SDL_GPUTextureCreateInfo info{};
    info.type = SDL_GPU_TEXTURETYPE_2D;
    info.format = g_scene_format;
    info.usage = SDL_GPU_TEXTUREUSAGE_SAMPLER |
                 SDL_GPU_TEXTUREUSAGE_COLOR_TARGET;
    info.width = kShadowAtlasSize;
    info.height = kShadowAtlasSize;
    info.layer_count_or_depth = 1;
    info.num_levels = 1;
    info.sample_count = SDL_GPU_SAMPLECOUNT_1;

    g_shadow_texture = SDL_CreateGPUTexture(g_device, &info);
    if (g_shadow_texture == nullptr)
    {
        LogSdlError("SDL_CreateGPUTexture(shadow)");
        return false;
    }

    /* One of each per vertex layout: a caster or receiver draw is whatever the
     * SGD walker happened to queue, so it can arrive streamed, from the static
     * mesh cache, or from the animated one. */
    for (int layout = 0; layout < kPipelineLayoutCount; layout++)
    {
        const MeshPipelineLayout mesh_layout = (MeshPipelineLayout)layout;
        if (!CreateShadowPipeline(&g_shadow_caster_pipeline[layout],
                                  mesh_layout, "shadow_silhouette.vert",
                                  "shadow_silhouette.frag", false, 1))
        {
            return false;
        }
    }
    return true;
}

/*
 * The receiver set for one sample count, built the first time a frame at that
 * count wants it.  Lazy rather than up front so that turning MSAA on does not
 * have to be a failure path at startup: a count whose set will not build
 * simply draws no projected shadows that frame, which is the same degradation
 * a room with no shadow map already has.
 */
bool EnsureShadowReceiverPipelines(int sample_count)
{
    const int slot = SampleSlotForCount(sample_count);
    if (g_device == nullptr || g_shadow_texture == nullptr ||
        g_shadow_receiver_failed[slot])
    {
        return false;
    }
    if (g_shadow_receiver_pipeline[slot][kPipelineLayoutCount - 1] != nullptr)
    {
        return true;
    }

    for (int layout = 0; layout < kPipelineLayoutCount; layout++)
    {
        if (g_shadow_receiver_pipeline[slot][layout] != nullptr)
        {
            continue;
        }
        if (!CreateShadowPipeline(&g_shadow_receiver_pipeline[slot][layout],
                                  (MeshPipelineLayout)layout,
                                  "shadow_receiver.vert",
                                  "shadow_receiver.frag", true, sample_count))
        {
            /* Latched: a device that will not build this set will not build it
             * next frame either, and asking again every frame is how a missing
             * shader turns into a per-frame allocation. */
            g_shadow_receiver_failed[slot] = true;
            return false;
        }
    }
    return true;
}

/*
 * The viewfinder surround's pipeline: the ordinary sprite vertex stage feeding
 * finder_mask.frag.
 *
 * It reuses sprite.vert because the quad really is an ordinary screen-space
 * sprite -- what makes the pass unusual is only what the fragment stage does
 * with it.  That also means the surround travels through
 * ApplyOriginalAspectToVertices() with every other 2D draw, which is what
 * keeps the aperture on the same pixels as the HUD art at any window shape.
 *
 * Fixed state, so it stays out of GetPipeline()'s cache the same way the two
 * shadow pipelines do: straight alpha blending (the mask is the alpha, so an
 * aperture pixel writes nothing), and no depth at all -- the surround is drawn
 * over a finished frame and must neither test against it nor occlude the HUD
 * queued after it.
 */
bool EnsureFinderMaskPipeline()
{
    const int sample_count = g_pipeline_sample_count;
    const int sample_slot = SampleSlotForCount(sample_count);
    if (g_finder_mask_pipeline[sample_slot] != nullptr)
    {
        return true;
    }
    if (g_finder_mask_pipeline_failed[sample_slot] || g_device == nullptr)
    {
        return false;
    }

    SDL_GPUShader *vertex_shader =
        GetCachedShader("sprite.vert", SDL_GPU_SHADERSTAGE_VERTEX, 0);
    SDL_GPUShader *fragment_shader =
        GetCachedShader("finder_mask.frag", SDL_GPU_SHADERSTAGE_FRAGMENT, 2);
    if (vertex_shader == nullptr || fragment_shader == nullptr)
    {
        /* Said out loud, once.  This pass only ever runs with the camera
         * raised, so a silent failure here shows up as "the viewfinder looks
         * the same as it always did" many minutes into a session, with nothing
         * anywhere to say why -- a missing finder_mask.frag next to the
         * executable being the likeliest reason. */
        SDL_Log("MioPan: the viewfinder surround is disabled -- "
                "finder_mask.frag could not be loaded.");
        g_finder_mask_pipeline_failed[sample_slot] = true;
        return false;
    }

    SDL_GPUVertexBufferDescription buffer_desc[2]{};
    SDL_GPUVertexAttribute attributes[4]{};
    Uint32 num_vertex_buffers = 0;
    Uint32 num_vertex_attributes = 0;
    BuildMeshVertexInput(MESH_PIPELINE_STREAMED, false, buffer_desc,
                         attributes, &num_vertex_buffers,
                         &num_vertex_attributes);

    SDL_GPUColorTargetBlendState blend{};
    blend.enable_blend = true;
    blend.src_color_blendfactor = SDL_GPU_BLENDFACTOR_SRC_ALPHA;
    blend.dst_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
    blend.color_blend_op = SDL_GPU_BLENDOP_ADD;
    blend.src_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
    blend.dst_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ZERO;
    blend.alpha_blend_op = SDL_GPU_BLENDOP_ADD;

    SDL_GPUColorTargetDescription color_target{};
    color_target.format = g_scene_format;
    color_target.blend_state = blend;

    SDL_GPUGraphicsPipelineCreateInfo info{};
    info.vertex_shader = vertex_shader;
    info.fragment_shader = fragment_shader;
    info.vertex_input_state.vertex_buffer_descriptions = buffer_desc;
    info.vertex_input_state.num_vertex_buffers = num_vertex_buffers;
    info.vertex_input_state.vertex_attributes = attributes;
    info.vertex_input_state.num_vertex_attributes = num_vertex_attributes;
    info.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
    info.rasterizer_state.fill_mode = SDL_GPU_FILLMODE_FILL;
    info.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_NONE;
    info.rasterizer_state.front_face = SDL_GPU_FRONTFACE_COUNTER_CLOCKWISE;
    info.rasterizer_state.enable_depth_clip = true;
    info.multisample_state.sample_count = SdlSampleCount(sample_count);
    info.target_info.color_target_descriptions = &color_target;
    info.target_info.num_color_targets = 1;
    /* Declared exactly as every main-pass pipeline declares it -- the depth
     * stage is off, but the pass this runs in carries a depth attachment and
     * the pipeline has to agree with it. */
    info.target_info.has_depth_stencil_target = true;
    info.target_info.depth_stencil_format = g_depth_format;

    g_finder_mask_pipeline[sample_slot] =
        SDL_CreateGPUGraphicsPipeline(g_device, &info);
    if (g_finder_mask_pipeline[sample_slot] == nullptr)
    {
        LogSdlError("SDL_CreateGPUGraphicsPipeline(finder mask)");
        g_finder_mask_pipeline_failed[sample_slot] = true;
        return false;
    }
    return true;
}

/*
 * The present pipeline: one triangle, no vertex buffer, no depth, no blending.
 *
 * Built lazily and keyed on the swapchain's format, because that format is the
 * one thing about this pass that moves at runtime -- switching the composition
 * between SDR and HDR replaces the swapchain's textures with a different type
 * entirely, and a pipeline built for the old one cannot render into the new.
 * A failure is latched so a device that will not build it is not asked again
 * every frame; the caller then falls back to the plain blit, which is the
 * pre-HDR behaviour.
 */
bool EnsurePresentPipeline()
{
    if (g_device == nullptr ||
        g_swapchain_format == SDL_GPU_TEXTUREFORMAT_INVALID)
    {
        return false;
    }

    if (g_present_pipeline != nullptr &&
        g_present_pipeline_format == g_swapchain_format)
    {
        return true;
    }

    if (g_present_pipeline_failed &&
        g_present_pipeline_format == g_swapchain_format)
    {
        return false;
    }

    if (g_present_pipeline != nullptr)
    {
        SDL_ReleaseGPUGraphicsPipeline(g_device, g_present_pipeline);
        g_present_pipeline = nullptr;
    }
    g_present_pipeline_format = g_swapchain_format;
    g_present_pipeline_failed = false;

    /* Not through GetCachedShader(): these two are outside the material
     * pipelines' shader table, are loaded exactly once, and neither has that
     * table's resource shape -- the vertex stage reads nothing at all and the
     * fragment stage one sampler and one uniform block, against two and three. */
    if (g_present_vertex_shader == nullptr)
    {
        g_present_vertex_shader =
            LoadShader("present.vert", SDL_GPU_SHADERSTAGE_VERTEX, 0, 0);
    }
    if (g_present_fragment_shader == nullptr)
    {
        g_present_fragment_shader =
            LoadShader("present.frag", SDL_GPU_SHADERSTAGE_FRAGMENT, 1, 1);
    }
    if (g_present_vertex_shader == nullptr ||
        g_present_fragment_shader == nullptr)
    {
        g_present_pipeline_failed = true;
        return false;
    }

    SDL_GPUColorTargetDescription color_target{};
    color_target.format = g_swapchain_format;

    SDL_GPUGraphicsPipelineCreateInfo info{};
    info.vertex_shader = g_present_vertex_shader;
    info.fragment_shader = g_present_fragment_shader;
    info.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
    info.rasterizer_state.fill_mode = SDL_GPU_FILLMODE_FILL;
    info.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_NONE;
    info.rasterizer_state.front_face = SDL_GPU_FRONTFACE_COUNTER_CLOCKWISE;
    info.multisample_state.sample_count = SDL_GPU_SAMPLECOUNT_1;
    info.target_info.color_target_descriptions = &color_target;
    info.target_info.num_color_targets = 1;

    g_present_pipeline = SDL_CreateGPUGraphicsPipeline(g_device, &info);
    if (g_present_pipeline == nullptr)
    {
        LogSdlError("SDL_CreateGPUGraphicsPipeline(present)");
        g_present_pipeline_failed = true;
        return false;
    }
    return true;
}

/* Called from the same atexit teardown that releases the cached shaders, so
 * the device is still alive when these go. */
void ReleasePresentResources()
{
    if (g_device == nullptr)
    {
        return;
    }
    if (g_present_pipeline != nullptr)
    {
        SDL_ReleaseGPUGraphicsPipeline(g_device, g_present_pipeline);
        g_present_pipeline = nullptr;
    }
    if (g_present_vertex_shader != nullptr)
    {
        SDL_ReleaseGPUShader(g_device, g_present_vertex_shader);
        g_present_vertex_shader = nullptr;
    }
    if (g_present_fragment_shader != nullptr)
    {
        SDL_ReleaseGPUShader(g_device, g_present_fragment_shader);
        g_present_fragment_shader = nullptr;
    }
    if (g_output_texture != nullptr)
    {
        SDL_ReleaseGPUTexture(g_device, g_output_texture);
        g_output_texture = nullptr;
        g_output_width = 0;
        g_output_height = 0;
    }
    g_present_pipeline_format = SDL_GPU_TEXTUREFORMAT_INVALID;
    g_present_pipeline_failed = false;
}

/*
 * Fill the present shader's uniforms for the composition currently in force.
 *
 * Everything the shader needs in absolute nits is divided out here instead of
 * there, so the fragment stage never has to know which encoding it is writing
 * beyond a mode number: paper white arrives already expressed in the output's
 * own units, and peak arrives as a ratio against it.
 */
void FillPresentUniforms(PresentUniformBlock *out, Uint32 width, Uint32 height)
{
    std::memset(out, 0, sizeof(*out));

    out->grade[0] = g_grade_brightness;
    out->grade[1] = g_grade_contrast;
    out->grade[2] = g_grade_gamma;
    out->grade[3] = g_grade_saturation;

    out->target[0] = (float)width;
    out->target[1] = (float)height;
    out->target[2] = width != 0 ? 1.0f / (float)width : 0.0f;
    out->target[3] = height != 0 ? 1.0f / (float)height : 0.0f;

    /* The expansion shoulder.  Fixed rather than exposed: it decides how
     * abruptly the lift arrives across the knee, and 2.0 is the value that
     * keeps a highlight's edge from reading as a hard step without flattening
     * the lift into something invisible.  One more slider here would be a knob
     * with no picture attached to it. */
    out->output[2] = 2.0f;

    const float paper = ResolvePaperWhiteNits();
    const float peak = ResolvePeakNits();

    switch (g_hdr_composition)
    {
    case SDL_GPU_SWAPCHAINCOMPOSITION_HDR_EXTENDED_LINEAR:
        out->output[0] = 1.0f; /* OUTPUT_SCRGB */
        out->output[1] = 0.0f; /* 16-bit float target: nothing to dither */
        out->hdr[0] = paper / kScrgbWhiteNits;
        break;

    case SDL_GPU_SWAPCHAINCOMPOSITION_HDR10_ST2084:
        out->output[0] = 2.0f; /* OUTPUT_PQ */
        /* A2R10G10B10, so one code value is 1/1023 of the PQ curve. */
        out->output[1] = 1.0f / 1023.0f;
        out->hdr[0] = paper / 10000.0f;
        break;

    case SDL_GPU_SWAPCHAINCOMPOSITION_SDR:
    case SDL_GPU_SWAPCHAINCOMPOSITION_SDR_LINEAR:
    default:
        out->output[0] = 0.0f; /* OUTPUT_SDR */
        out->output[1] = 1.0f / 255.0f;
        out->hdr[0] = 1.0f;
        break;
    }

    if (HdrCompositionIsHdr(g_hdr_composition))
    {
        out->hdr[1] = peak / paper;
        out->hdr[2] = g_hdr_expansion;
        out->hdr[3] = g_hdr_expansion_knee;
    }
    else
    {
        /* An SDR present is the grade and the dither and nothing else: there
         * is no headroom to expand into, so the shader's HDR half is left
         * neutral rather than being given numbers it will not reach. */
        out->hdr[1] = 1.0f;
    }
}

/*
 * The present pass itself.  Reads the composited output target, writes the
 * swapchain.  DONT_CARE because the triangle covers every pixel of the
 * swapchain and there is nothing underneath worth preserving.
 */
bool RecordPresentPass(SDL_GPUCommandBuffer *cmd,
                       SDL_GPUTexture *swapchain_texture,
                       SDL_GPUTexture *source, Uint32 width, Uint32 height)
{
    if (cmd == nullptr || swapchain_texture == nullptr || source == nullptr ||
        !EnsurePresentPipeline())
    {
        return false;
    }

    SDL_GPUColorTargetInfo target{};
    target.texture = swapchain_texture;
    target.load_op = SDL_GPU_LOADOP_DONT_CARE;
    target.store_op = SDL_GPU_STOREOP_STORE;
    target.cycle = false;

    SDL_GPURenderPass *pass = SDL_BeginGPURenderPass(cmd, &target, 1, nullptr);
    if (pass == nullptr)
    {
        LogSdlError("SDL_BeginGPURenderPass(present)");
        return false;
    }

    SDL_BindGPUGraphicsPipeline(pass, g_present_pipeline);

    PresentUniformBlock uniforms;
    FillPresentUniforms(&uniforms, width, height);
    SDL_PushGPUFragmentUniformData(cmd, 0, &uniforms, (Uint32)sizeof(uniforms));

    /* The output target is the swapchain's own size, so the sample is 1:1 and
     * the filter is never called on to interpolate.  Nearest keeps it that way
     * even if a driver rounds the mapping differently. */
    SDL_GPUTextureSamplerBinding binding{};
    binding.texture = source;
    binding.sampler = g_samplers[0][0][0][0];
    SDL_BindGPUFragmentSamplers(pass, 0, &binding, 1);

    SDL_DrawGPUPrimitives(pass, 3, 1, 0, 0);
    SDL_EndGPURenderPass(pass);
    return true;
}

bool CreatePipelines()
{
    /*
     * Build the default-blend, depth-writing set up front.  That is every
     * variant the port had before the GS draw environment was wired in, so a
     * missing or malformed shader still fails at startup; the blend and
     * depth-write variants above it are filled in lazily as the game asks.
     */
    for (int transform_mesh = 0; transform_mesh < 2; transform_mesh++)
    {
        for (int enable_depth = 0; enable_depth < 2; enable_depth++)
        {
            for (int wireframe = 0; wireframe < 2; wireframe++)
            {
                if (GetPipeline(MESH_PIPELINE_STREAMED, transform_mesh != 0,
                                enable_depth != 0, GS_DEPTH_GEQUAL, true,
                                wireframe != 0,
                                GS_BLEND_ALPHA, false) == nullptr)
                {
                    return false;
                }
                if (transform_mesh != 0 &&
                    GetPipeline(MESH_PIPELINE_STREAMED, true,
                                enable_depth != 0, GS_DEPTH_GEQUAL, true,
                                wireframe != 0,
                                GS_BLEND_ALPHA, true) == nullptr)
                {
                    return false;
                }
            }
        }
    }

    for (int enable_depth = 0; enable_depth < 2; enable_depth++)
    {
        for (int wireframe = 0; wireframe < 2; wireframe++)
        {
            if (GetPipeline(MESH_PIPELINE_CACHED_STATIC, true,
                            enable_depth != 0, GS_DEPTH_GEQUAL, true,
                            wireframe != 0,
                            GS_BLEND_ALPHA, false) == nullptr ||
                GetPipeline(MESH_PIPELINE_CACHED_STATIC, true,
                            enable_depth != 0, GS_DEPTH_GEQUAL, true,
                            wireframe != 0,
                            GS_BLEND_ALPHA, true) == nullptr ||
                GetPipeline(MESH_PIPELINE_CACHED_ANIMATED, true,
                            enable_depth != 0, GS_DEPTH_GEQUAL, true,
                            wireframe != 0,
                            GS_BLEND_ALPHA, false) == nullptr)
            {
                return false;
            }
        }
    }

    /* A room's first visible frame must not become a shader/pipeline compiler
     * workload.  Warm every canonical, non-wireframe state used by the six
     * production shader/layout families.  Debug wireframe states remain lazy;
     * their one-time cost is user-triggered rather than a map-transition
     * hitch.  Exotic state failures are memoized by GetPipeline and fall back
     * to the required alpha/depth-writing variants above. */
    struct PipelineFamily
    {
        MeshPipelineLayout layout;
        bool transform_mesh;
        bool fragment_lighting;
    };
    const PipelineFamily families[] = {
        {MESH_PIPELINE_STREAMED, false, false},
        {MESH_PIPELINE_STREAMED, true, false},
        {MESH_PIPELINE_STREAMED, true, true},
        {MESH_PIPELINE_CACHED_STATIC, true, false},
        {MESH_PIPELINE_CACHED_STATIC, true, true},
        {MESH_PIPELINE_CACHED_ANIMATED, true, false},
    };
    for (const PipelineFamily &family : families)
    {
        for (int blend = 0; blend < GS_BLEND_COUNT; blend++)
        {
            const GsBlendMode blend_mode = (GsBlendMode)blend;
            (void)GetPipeline(family.layout, family.transform_mesh, false,
                              GS_DEPTH_GEQUAL, true, false, blend_mode,
                              family.fragment_lighting);
            (void)GetPipeline(family.layout, family.transform_mesh, true,
                              GS_DEPTH_GEQUAL, false, false, blend_mode,
                              family.fragment_lighting);
            (void)GetPipeline(family.layout, family.transform_mesh, true,
                              GS_DEPTH_GEQUAL, true, false, blend_mode,
                              family.fragment_lighting);
        }
    }
    return true;
}

bool UploadTexturePixels(SDL_GPUTexture *texture, const unsigned char *rgba,
                         int width, int height)
{
    const size_t byte_count = (size_t)width * (size_t)height * 4u;
    if (byte_count == 0 || byte_count > std::numeric_limits<Uint32>::max())
    {
        return false;
    }

    SDL_GPUTransferBufferCreateInfo transfer_info{};
    transfer_info.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
    transfer_info.size = (Uint32)byte_count;
    SDL_GPUTransferBuffer *transfer =
        SDL_CreateGPUTransferBuffer(g_device, &transfer_info);
    if (transfer == nullptr)
    {
        LogSdlError("SDL_CreateGPUTransferBuffer");
        return false;
    }

    void *mapped = SDL_MapGPUTransferBuffer(g_device, transfer, true);
    if (mapped == nullptr)
    {
        LogSdlError("SDL_MapGPUTransferBuffer");
        SDL_ReleaseGPUTransferBuffer(g_device, transfer);
        return false;
    }
    std::memcpy(mapped, rgba, byte_count);
    SDL_UnmapGPUTransferBuffer(g_device, transfer);

    SDL_GPUCommandBuffer *cmd = SDL_AcquireGPUCommandBuffer(g_device);
    if (cmd == nullptr)
    {
        LogSdlError("SDL_AcquireGPUCommandBuffer");
        SDL_ReleaseGPUTransferBuffer(g_device, transfer);
        return false;
    }

    SDL_GPUTextureTransferInfo src{};
    src.transfer_buffer = transfer;
    src.offset = 0;
    src.pixels_per_row = (Uint32)width;
    src.rows_per_layer = (Uint32)height;

    SDL_GPUTextureRegion dst{};
    dst.texture = texture;
    dst.mip_level = 0;
    dst.layer = 0;
    dst.x = 0;
    dst.y = 0;
    dst.z = 0;
    dst.w = (Uint32)width;
    dst.h = (Uint32)height;
    dst.d = 1;

    SDL_GPUCopyPass *copy = SDL_BeginGPUCopyPass(cmd);
    SDL_UploadToGPUTexture(copy, &src, &dst, false);
    SDL_EndGPUCopyPass(copy);

    bool submitted = SDL_SubmitGPUCommandBuffer(cmd);
    if (!submitted)
    {
        LogSdlError("SDL_SubmitGPUCommandBuffer");
    }
    SDL_ReleaseGPUTransferBuffer(g_device, transfer);
    return submitted;
}

SDL_GPUTexture *CreateTextureResource(int width, int height)
{
    if (width <= 0 || height <= 0)
    {
        return nullptr;
    }

    SDL_GPUTextureCreateInfo info{};
    info.type = SDL_GPU_TEXTURETYPE_2D;
    info.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
    info.usage = SDL_GPU_TEXTUREUSAGE_SAMPLER;
    info.width = (Uint32)width;
    info.height = (Uint32)height;
    info.layer_count_or_depth = 1;
    info.num_levels = 1;
    info.sample_count = SDL_GPU_SAMPLECOUNT_1;

    SDL_GPUTexture *texture = SDL_CreateGPUTexture(g_device, &info);
    if (texture == nullptr)
    {
        LogSdlError("SDL_CreateGPUTexture");
        return nullptr;
    }

    return texture;
}

SDL_GPUTexture *CreateTexture(int width, int height, const unsigned char *rgba)
{
    if (rgba == nullptr)
    {
        return nullptr;
    }

    SDL_GPUTexture *texture = CreateTextureResource(width, height);
    if (texture == nullptr)
    {
        return nullptr;
    }

    if (!UploadTexturePixels(texture, rgba, width, height))
    {
        SDL_ReleaseGPUTexture(g_device, texture);
        return nullptr;
    }

    return texture;
}

/*
 * Build the grain sheet the ROM's MakeRDither3() would have built, at
 * kFilmGrainSheetSize instead of 128.
 *
 * The ROM's sheet is an 8-bit index field plus a 256-entry CLUT, and the two
 * carry different things: the index IS the alpha (CLUT entry i has alpha i),
 * while the colour is a grey drawn once per index rather than once per texel.
 * So a texel's brightness is a function of its opacity, and reproducing that
 * pairing -- not just "random grey" -- is what makes the two modes look like
 * the same effect.  Indices run 0..alpmx and greys 0..colmx, both floored from
 * a uniform, exactly as MakeRDither3() floors its float.
 *
 * Alpha is doubled on the way out because that is what AdjustPS2Alpha() does to
 * every other texture the GS path decodes: the PS2's opaque is 128, the host's
 * is 255.  Without it the whole sheet would be half as strong as the ROM's.
 */
bool BuildFilmGrainSheet(int size, int alpmx, int colmx)
{

    /* A private generator.  MioPan_Rand() would be the faithful source, but the
     * game's own draws come off that stream and pulling a megatexel out of it
     * would move every one of them -- see the note on the Begin() entry point.
     * Seeded from the parameters so a given look is reproducible run to run. */
    uint32_t rng = 0x9e3779b9u ^ ((uint32_t)alpmx << 8) ^ (uint32_t)colmx;
    auto next = [&rng]() {
        rng ^= rng << 13;
        rng ^= rng >> 17;
        rng ^= rng << 5;
        return rng;
    };
    /* 24 bits of mantissa is the most a float can carry, and matches the ROM's
     * rand() / RAND_MAX in range if not in sequence. */
    auto uniform = [&next]() {
        return (float)(next() >> 8) * (1.0f / 16777216.0f);
    };

    unsigned char grey[256];
    for (int i = 0; i < 256; i++)
    {
        grey[i] = (unsigned char)((unsigned int)((float)colmx * uniform()) &
                                  0xffu);
    }

    std::vector<unsigned char> rgba((size_t)size * (size_t)size * 4u);
    for (size_t i = 0; i < rgba.size(); i += 4)
    {
        const unsigned int index =
            (unsigned int)((float)alpmx * uniform()) & 0xffu;
        const unsigned char c = grey[index];

        rgba[i + 0] = c;
        rgba[i + 1] = c;
        rgba[i + 2] = c;
        rgba[i + 3] = index <= 127 ? (unsigned char)(index << 1)
                                   : (unsigned char)0xff;
    }

    if (g_film_grain_sheet != nullptr)
    {
        SDL_ReleaseGPUTexture(g_device, g_film_grain_sheet);
        g_film_grain_sheet = nullptr;
    }

    g_film_grain_sheet = CreateTexture(size, size, rgba.data());
    g_film_grain_sheet_size = g_film_grain_sheet != nullptr ? size : 0;
    return g_film_grain_sheet != nullptr;
}

constexpr size_t kTextureRowPitchAlignment = 256u;
constexpr size_t kTextureOffsetAlignment = 512u;
constexpr size_t kTextureUploadChunkBytes = 32u * 1024u * 1024u;

bool AlignTextureUploadSize(size_t value, size_t alignment, size_t *out)
{
    if (out == nullptr || alignment == 0 ||
        value > std::numeric_limits<size_t>::max() - (alignment - 1u))
    {
        return false;
    }
    *out = (value + alignment - 1u) & ~(alignment - 1u);
    return true;
}

TextureEntry *CreatePendingTexture(uint64_t hash, int width, int height,
                                   std::vector<unsigned char> &&rgba)
{
    if (hash == 0 || width <= 0 || height <= 0)
    {
        return nullptr;
    }
    const size_t pixel_count = (size_t)width * (size_t)height;
    if (pixel_count > std::numeric_limits<size_t>::max() / 4u)
    {
        return nullptr;
    }

    const size_t byte_count = pixel_count * 4u;
    if (rgba.size() != byte_count)
    {
        return nullptr;
    }

    PendingTextureUpload pending{};
    pending.hash = hash;
    pending.width = (Uint32)width;
    pending.height = (Uint32)height;
    pending.rgba = std::move(rgba);

    pending.texture = CreateTextureResource(width, height);
    if (pending.texture == nullptr)
    {
        return nullptr;
    }

    TextureEntry entry{pending.texture, width, height, hash, false};
    try
    {
        auto inserted = g_texture_cache.emplace(hash, entry);
        if (!inserted.second)
        {
            SDL_ReleaseGPUTexture(g_device, pending.texture);
            return &inserted.first->second;
        }

        try
        {
            g_pending_texture_uploads.push_back(std::move(pending));
        }
        catch (const std::bad_alloc &)
        {
            g_texture_cache.erase(inserted.first);
            SDL_ReleaseGPUTexture(g_device, entry.texture);
            return nullptr;
        }
        return &inserted.first->second;
    }
    catch (const std::bad_alloc &)
    {
        SDL_ReleaseGPUTexture(g_device, pending.texture);
        return nullptr;
    }
}

/*
 * PORT-ONLY: the movie picture, served without going through the GS.
 *
 * See miopan/rendering/miopan_video.h for the whole of the reasoning.  The
 * short version is that a movie's GS block holds different pixels every frame,
 * and the ordinary path keys a host texture on the hash of its GS content --
 * so a cutscene creates one texture per frame in a cache that never evicts.
 * Here the decoder's own linear RGBA is uploaded into one persistent texture
 * instead, and every TEX0 naming the movie's block is answered with it.
 *
 * The texture is allocated at the size the TEX0 declares (1 << TW by 1 << TH,
 * a 1024x512 page for both of the game's two players) and the picture goes
 * into its top-left corner, because that is where the transfer chain put it --
 * so movie.c's 0..640 x 0..448 sprite UVs and CMovieRoom::Draw()'s texel
 * rectangle both still land exactly where they did.
 *
 * The hash is a constant rather than content-derived: nothing else may share
 * the entry, and the value has to be stable so CommitTextureUploads() can find
 * it again.  "MIOPANVD" in ASCII, which no GS region will hash to.
 */
constexpr uint64_t kVideoTextureHash = 0x4D494F50414E5644ull;

bool IsVideoTex0(const sceGsTex0 *tex0)
{
    if (tex0 == nullptr || MioPan_VideoIsLive() == 0)
    {
        return false;
    }
    /* Address AND format, the same pairing IsCaptureSlotAddr() needs and for
     * the same reason: the game reuses these pages.  ScreenSaverDraw() samples
     * 0x2bc0 as a 256 px PSMT8 image with its own CLUT, which is a real
     * texture that must still reach the decoder below -- and which would
     * otherwise make this re-create the video texture at another size on every
     * frame it drew. */
    if (tex0->PSM != MioPan::GS::PSMCT32 && tex0->PSM != MioPan::GS::PSMCT24)
    {
        return false;
    }
    return (int)tex0->TBP0 == MioPan_VideoGsAddr();
}

TextureEntry *GetVideoTexture(const sceGsTex0 *tex0)
{
    const int want_w = 1 << tex0->TW;
    const int want_h = 1 << tex0->TH;

    if (want_w <= 0 || want_h <= 0 || want_w > 4096 || want_h > 4096)
    {
        return nullptr;
    }

    auto entry = g_texture_cache.find(kVideoTextureHash);

    /* A TEX0 declaring a different page than the one this was built for.  Both
     * of the game's callers say 1024x512, so this is a re-create that has
     * never happened -- SDL_GPU defers the release behind whatever command
     * buffers still reference the old texture, so it is safe where it does. */
    if (entry != g_texture_cache.end() &&
        (entry->second.texture == nullptr || entry->second.width != want_w ||
         entry->second.height != want_h))
    {
        if (entry->second.texture != nullptr)
        {
            /* A queued upload still names it.  Drop those first: the copy pass
             * runs later in the frame and would write into a released
             * texture. */
            g_pending_texture_uploads.erase(
                std::remove_if(g_pending_texture_uploads.begin(),
                               g_pending_texture_uploads.end(),
                               [&entry](const PendingTextureUpload &pending) {
                                   return pending.texture ==
                                          entry->second.texture;
                               }),
                g_pending_texture_uploads.end());
            SDL_ReleaseGPUTexture(g_device, entry->second.texture);
        }
        g_texture_cache.erase(entry);
        entry = g_texture_cache.end();
    }

    if (entry == g_texture_cache.end())
    {
        SDL_GPUTexture *texture = CreateTextureResource(want_w, want_h);
        if (texture == nullptr)
        {
            return nullptr;
        }

        TextureEntry fresh{texture, want_w, want_h, kVideoTextureHash, false};
        try
        {
            entry = g_texture_cache.emplace(kVideoTextureHash, fresh).first;

            /* Blacken it before anything samples it.  A film is drawn for a
             * frame or two before its first picture decodes -- playpss.c only
             * arms this path once it knows the geometry -- and a freshly
             * created GPU texture holds undefined memory.  sceMpegGetPicture()
             * blanks the ROM's picture buffer once per movie for exactly the
             * same reason. */
            PendingTextureUpload blank{};
            blank.hash = kVideoTextureHash;
            blank.texture = texture;
            blank.width = (Uint32)want_w;
            blank.height = (Uint32)want_h;
            blank.rgba.assign((size_t)want_w * (size_t)want_h * 4u, 0);
            g_pending_texture_uploads.push_back(std::move(blank));
        }
        catch (const std::bad_alloc &)
        {
            if (entry != g_texture_cache.end())
            {
                g_texture_cache.erase(entry);
            }
            SDL_ReleaseGPUTexture(g_device, texture);
            return nullptr;
        }
    }

    /* One picture per decoded frame.  A second draw sampling the same block in
     * the same frame finds nothing waiting and reuses what is already there,
     * which is the held frame the ROM's transfer chain would also have left. */
    std::vector<unsigned char> rgba;
    int frame_w = 0;
    int frame_h = 0;

    if (MioPan::Video::AcquireFrame(rgba, &frame_w, &frame_h) &&
        frame_w > 0 && frame_h > 0 && frame_w <= want_w && frame_h <= want_h &&
        rgba.size() == (size_t)frame_w * (size_t)frame_h * 4u)
    {
        PendingTextureUpload pending{};
        pending.hash = kVideoTextureHash;
        pending.texture = entry->second.texture;
        pending.width = (Uint32)frame_w;
        pending.height = (Uint32)frame_h;
        pending.rgba = std::move(rgba);

        try
        {
            g_pending_texture_uploads.push_back(std::move(pending));
        }
        catch (const std::bad_alloc &)
        {
            /* Keep last frame's picture rather than dropping the draw. */
        }
    }

    return &entry->second;
}

struct TextureUploadSlice
{
    size_t pending_index;
    Uint32 offset;
    Uint32 row_pitch;
};

struct TextureUploadResult
{
    std::vector<SDL_GPUTexture *> recorded_textures;
    std::vector<SDL_GPUTransferBuffer *> transfers;
};

TextureUploadResult UploadPendingTextures(SDL_GPUCommandBuffer *cmd)
{
    TextureUploadResult result;
    if (cmd == nullptr || g_pending_texture_uploads.empty())
    {
        return result;
    }

    const size_t max_upload_bytes = std::numeric_limits<Uint32>::max();
    std::vector<TextureUploadSlice> slices;
    try
    {
        slices.reserve(g_pending_texture_uploads.size());
        result.recorded_textures.reserve(g_pending_texture_uploads.size());
        result.transfers.reserve(g_pending_texture_uploads.size());
    }
    catch (const std::bad_alloc &)
    {
        return result;
    }

    size_t next_pending = 0;
    while (next_pending < g_pending_texture_uploads.size())
    {
        slices.clear();
        size_t total_bytes = 0;

        while (next_pending < g_pending_texture_uploads.size())
        {
            const PendingTextureUpload &pending =
                g_pending_texture_uploads[next_pending];
            if (pending.texture == nullptr || pending.width == 0 ||
                pending.height == 0)
            {
                next_pending++;
                continue;
            }

            const size_t row_bytes = (size_t)pending.width * 4u;
            size_t row_pitch = 0;
            size_t offset = 0;
            if (!AlignTextureUploadSize(row_bytes,
                                        kTextureRowPitchAlignment,
                                        &row_pitch) ||
                !AlignTextureUploadSize(total_bytes,
                                        kTextureOffsetAlignment, &offset) ||
                row_pitch > max_upload_bytes ||
                pending.height > max_upload_bytes / row_pitch)
            {
                next_pending++;
                continue;
            }
            const size_t slice_bytes = row_pitch * (size_t)pending.height;
            if (offset > max_upload_bytes - slice_bytes ||
                pending.rgba.size() != row_bytes * (size_t)pending.height)
            {
                next_pending++;
                continue;
            }

            const size_t end = offset + slice_bytes;
            if (!slices.empty() && end > kTextureUploadChunkBytes)
            {
                break;
            }

            /* An individual texture larger than the normal chunk still gets a
             * single attempt of its own; otherwise one asset could block every
             * smaller upload behind it forever. */
            slices.push_back(
                {next_pending, (Uint32)offset, (Uint32)row_pitch});
            total_bytes = end;
            next_pending++;
        }

        if (slices.empty() || total_bytes == 0)
        {
            continue;
        }

        SDL_GPUTransferBufferCreateInfo transfer_info{};
        transfer_info.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
        transfer_info.size = (Uint32)total_bytes;
        SDL_GPUTransferBuffer *transfer =
            SDL_CreateGPUTransferBuffer(g_device, &transfer_info);
        if (transfer == nullptr)
        {
            LogSdlError("SDL_CreateGPUTransferBuffer(texture batch)");
            continue;
        }

        unsigned char *mapped = static_cast<unsigned char *>(
            SDL_MapGPUTransferBuffer(g_device, transfer, true));
        if (mapped == nullptr)
        {
            LogSdlError("SDL_MapGPUTransferBuffer(texture batch)");
            SDL_ReleaseGPUTransferBuffer(g_device, transfer);
            continue;
        }
        for (const TextureUploadSlice &slice : slices)
        {
            const PendingTextureUpload &pending =
                g_pending_texture_uploads[slice.pending_index];
            const size_t row_bytes = (size_t)pending.width * 4u;
            for (Uint32 y = 0; y < pending.height; y++)
            {
                std::memcpy(
                    mapped + slice.offset + (size_t)y * slice.row_pitch,
                    pending.rgba.data() + (size_t)y * row_bytes, row_bytes);
            }
        }
        SDL_UnmapGPUTransferBuffer(g_device, transfer);

        SDL_GPUCopyPass *copy = SDL_BeginGPUCopyPass(cmd);
        if (copy == nullptr)
        {
            LogSdlError("SDL_BeginGPUCopyPass(texture batch)");
            SDL_ReleaseGPUTransferBuffer(g_device, transfer);
            continue;
        }
        for (const TextureUploadSlice &slice : slices)
        {
            const PendingTextureUpload &pending =
                g_pending_texture_uploads[slice.pending_index];

            SDL_GPUTextureTransferInfo src{};
            src.transfer_buffer = transfer;
            src.offset = slice.offset;
            src.pixels_per_row = slice.row_pitch / 4u;
            src.rows_per_layer = pending.height;

            SDL_GPUTextureRegion dst{};
            dst.texture = pending.texture;
            dst.mip_level = 0;
            dst.layer = 0;
            dst.x = 0;
            dst.y = 0;
            dst.z = 0;
            dst.w = pending.width;
            dst.h = pending.height;
            dst.d = 1;
            SDL_UploadToGPUTexture(copy, &src, &dst, false);
            result.recorded_textures.push_back(pending.texture);
        }
        SDL_EndGPUCopyPass(copy);
        result.transfers.push_back(transfer);

        /* Bound transition work as well as each allocation.  Any remaining
         * decoded textures stay pending and another copy-only/present command
         * drains the next chunk on a later logical frame. */
        break;
    }
    return result;
}

/*
 * A pending upload whose draw must NOT be swapped to the white texture.
 *
 * The fallback below exists for a texture that has no content yet -- drawing
 * white beats sampling undefined memory.  The movie texture is the opposite
 * case: it is persistent and already holds the previous picture, so an upload
 * that misses this frame's copy pass should simply show that frame again.
 * Swapping it puts a white flash over the whole screen mid-cutscene, which is
 * far worse than the repeated frame it replaces.
 */
bool PendingUploadKeepsPreviousFrame(const PendingTextureUpload &pending)
{
    return pending.hash == kVideoTextureHash;
}

void FallbackUnrecordedPendingTextures(const TextureUploadResult &result)
{
    if (g_pending_texture_uploads.empty())
    {
        return;
    }
    if (result.recorded_textures.empty())
    {
        for (DrawCommand &draw : g_draws)
        {
            for (const PendingTextureUpload &pending :
                 g_pending_texture_uploads)
            {
                if (draw.texture == pending.texture &&
                    !PendingUploadKeepsPreviousFrame(pending))
                {
                    draw.texture = g_white_texture;
                    draw.texture_width = 1;
                    draw.texture_height = 1;
                    break;
                }
            }
        }
        return;
    }
    if (result.recorded_textures.size() == g_pending_texture_uploads.size())
    {
        return;
    }

    for (DrawCommand &draw : g_draws)
    {
        for (const PendingTextureUpload &pending : g_pending_texture_uploads)
        {
            if (draw.texture == pending.texture &&
                !PendingUploadKeepsPreviousFrame(pending) &&
                std::find(result.recorded_textures.begin(),
                          result.recorded_textures.end(), pending.texture) ==
                    result.recorded_textures.end())
            {
                draw.texture = g_white_texture;
                draw.texture_width = 1;
                draw.texture_height = 1;
                break;
            }
        }
    }
}

void CommitTextureUploads(const TextureUploadResult &result)
{
    if (result.recorded_textures.empty())
    {
        return;
    }
    if (result.recorded_textures.size() == g_pending_texture_uploads.size())
    {
        for (const PendingTextureUpload &pending : g_pending_texture_uploads)
        {
            auto entry = g_texture_cache.find(pending.hash);
            if (entry != g_texture_cache.end() &&
                entry->second.texture == pending.texture)
            {
                entry->second.ready = true;
            }
        }
        g_pending_texture_uploads.clear();
        return;
    }

    for (const PendingTextureUpload &pending : g_pending_texture_uploads)
    {
        if (std::find(result.recorded_textures.begin(),
                      result.recorded_textures.end(), pending.texture) ==
            result.recorded_textures.end())
        {
            continue;
        }
        auto entry = g_texture_cache.find(pending.hash);
        if (entry != g_texture_cache.end() &&
            entry->second.texture == pending.texture)
        {
            entry->second.ready = true;
        }
    }
    g_pending_texture_uploads.erase(
        std::remove_if(g_pending_texture_uploads.begin(),
                       g_pending_texture_uploads.end(),
                       [&result](const PendingTextureUpload &pending) {
                           return std::find(result.recorded_textures.begin(),
                                            result.recorded_textures.end(),
                                            pending.texture) !=
                               result.recorded_textures.end();
                       }),
        g_pending_texture_uploads.end());
}

/*
 * The depth buffer, at the sample count the frame will be rasterised at.
 *
 * Both attachments in a render pass must agree on sample count, so this
 * follows the colour target rather than being a setting of its own -- and a
 * change in the MSAA setting reallocates it exactly the way a change in the
 * render resolution does.
 */
bool EnsureDepthTexture(Uint32 width, Uint32 height, int sample_count)
{
    if (width == 0 || height == 0)
    {
        return false;
    }

    if (g_depth_texture != nullptr &&
        g_depth_width == width && g_depth_height == height &&
        g_depth_samples == sample_count)
    {
        return true;
    }

    if (g_depth_texture != nullptr)
    {
        SDL_ReleaseGPUTexture(g_device, g_depth_texture);
        g_depth_texture = nullptr;
    }

    SDL_GPUTextureCreateInfo info{};
    info.type = SDL_GPU_TEXTURETYPE_2D;
    info.format = g_depth_format;
    info.usage = SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET;
    info.width = width;
    info.height = height;
    info.layer_count_or_depth = 1;
    info.num_levels = 1;
    info.sample_count = SdlSampleCount(sample_count);

    g_depth_texture = SDL_CreateGPUTexture(g_device, &info);
    if (g_depth_texture == nullptr)
    {
        LogSdlError("SDL_CreateGPUTexture(depth)");
        g_depth_width = 0;
        g_depth_height = 0;
        g_depth_samples = 1;
        return false;
    }

    g_depth_width = width;
    g_depth_height = height;
    g_depth_samples = sample_count;
    return true;
}

/*
 * The multisampled colour target, for the frame and nothing else.
 *
 * COLOR_TARGET only: a multisampled texture cannot carry SAMPLER usage, which
 * is the whole reason the resolve exists.  Everything that reads the frame --
 * the GS capture slots, the pause still, the screen mirror, the present blit
 * -- reads the resolve destination instead, so none of them changes.
 */
bool EnsureMsaaColorTexture(Uint32 width, Uint32 height, int sample_count)
{
    if (width == 0 || height == 0 || sample_count <= 1 ||
        g_scene_format == SDL_GPU_TEXTUREFORMAT_INVALID)
    {
        return false;
    }

    if (g_msaa_color_texture != nullptr && g_msaa_width == width &&
        g_msaa_height == height && g_msaa_texture_samples == sample_count)
    {
        return true;
    }

    if (g_msaa_color_texture != nullptr)
    {
        SDL_ReleaseGPUTexture(g_device, g_msaa_color_texture);
        g_msaa_color_texture = nullptr;
    }

    SDL_GPUTextureCreateInfo info{};
    info.type = SDL_GPU_TEXTURETYPE_2D;
    info.format = g_scene_format;
    info.usage = SDL_GPU_TEXTUREUSAGE_COLOR_TARGET;
    info.width = width;
    info.height = height;
    info.layer_count_or_depth = 1;
    info.num_levels = 1;
    info.sample_count = SdlSampleCount(sample_count);

    g_msaa_color_texture = SDL_CreateGPUTexture(g_device, &info);
    if (g_msaa_color_texture == nullptr)
    {
        LogSdlError("SDL_CreateGPUTexture(MSAA colour target)");
        g_msaa_width = 0;
        g_msaa_height = 0;
        g_msaa_texture_samples = 1;
        return false;
    }

    g_msaa_width = width;
    g_msaa_height = height;
    g_msaa_texture_samples = sample_count;
    SDL_Log("MioPan SDL_GPU: MSAA target %ux%u at %dx", width, height,
            sample_count);
    return true;
}

/* Drop the multisampled target when MSAA goes off, so turning it off actually
 * gives the memory back rather than parking it for the session. */
void ReleaseMsaaColorTexture()
{
    if (g_device != nullptr && g_msaa_color_texture != nullptr)
    {
        SDL_ReleaseGPUTexture(g_device, g_msaa_color_texture);
    }
    g_msaa_color_texture = nullptr;
    g_msaa_width = 0;
    g_msaa_height = 0;
    g_msaa_texture_samples = 1;
}

/*
 * The scene target.  Swapchain format, because everything that copies out of it
 * -- the capture slots, the pause still, the screen mirror -- is already in that
 * format, and because the present blit is then a straight resample with no
 * conversion.  COLOR_TARGET to draw into, SAMPLER because SDL_BlitGPUTexture
 * reads it as one.
 */
bool EnsureSceneTexture(Uint32 width, Uint32 height)
{
    if (width == 0 || height == 0 ||
        g_scene_format == SDL_GPU_TEXTUREFORMAT_INVALID)
    {
        return false;
    }

    if (g_scene_texture != nullptr &&
        g_scene_width == width && g_scene_height == height)
    {
        return true;
    }

    if (g_scene_texture != nullptr)
    {
        SDL_ReleaseGPUTexture(g_device, g_scene_texture);
        g_scene_texture = nullptr;
    }

    SDL_GPUTextureCreateInfo info{};
    info.type = SDL_GPU_TEXTURETYPE_2D;
    info.format = g_scene_format;
    info.usage = SDL_GPU_TEXTUREUSAGE_SAMPLER |
                 SDL_GPU_TEXTUREUSAGE_COLOR_TARGET;
    info.width = width;
    info.height = height;
    info.layer_count_or_depth = 1;
    info.num_levels = 1;
    info.sample_count = SDL_GPU_SAMPLECOUNT_1;

    g_scene_texture = SDL_CreateGPUTexture(g_device, &info);
    if (g_scene_texture == nullptr)
    {
        LogSdlError("SDL_CreateGPUTexture(scene target)");
        g_scene_width = 0;
        g_scene_height = 0;
        return false;
    }

    g_scene_width = width;
    g_scene_height = height;
    SDL_Log("MioPan SDL_GPU: scene target %ux%u", width, height);
    return true;
}

/*
 * The composited output the present pass reads.
 *
 * This stands in for the swapchain whenever the present pass is taken: the
 * bars, the resampled scene and the host UI all land here in the game's own SDR
 * encoding, and the pass then converts the finished picture once.  Compositing
 * before the transform rather than after is what keeps the host UI's alpha
 * blending -- and the game's own letterbox black -- defined in the space they
 * were authored in.
 *
 * g_output_format rather than the swapchain's, and that is not a detail: ImGui
 * builds its pipeline against a colour format at Init and has no way to be
 * told about a new one, so the target it draws into has to keep the format the
 * renderer started with however the swapchain composition later changes.
 */
bool EnsureOutputTexture(Uint32 width, Uint32 height)
{
    if (width == 0 || height == 0 ||
        g_output_format == SDL_GPU_TEXTUREFORMAT_INVALID)
    {
        return false;
    }

    if (g_output_texture != nullptr && g_output_width == width &&
        g_output_height == height)
    {
        return true;
    }

    if (g_output_texture != nullptr)
    {
        SDL_ReleaseGPUTexture(g_device, g_output_texture);
        g_output_texture = nullptr;
    }

    SDL_GPUTextureCreateInfo info{};
    info.type = SDL_GPU_TEXTURETYPE_2D;
    info.format = g_output_format;
    info.usage = SDL_GPU_TEXTUREUSAGE_SAMPLER |
                 SDL_GPU_TEXTUREUSAGE_COLOR_TARGET;
    info.width = width;
    info.height = height;
    info.layer_count_or_depth = 1;
    info.num_levels = 1;
    info.sample_count = SDL_GPU_SAMPLECOUNT_1;

    g_output_texture = SDL_CreateGPUTexture(g_device, &info);
    if (g_output_texture == nullptr)
    {
        LogSdlError("SDL_CreateGPUTexture(output target)");
        g_output_width = 0;
        g_output_height = 0;
        return false;
    }

    g_output_width = width;
    g_output_height = height;
    SDL_Log("MioPan SDL_GPU: output target %ux%u", width, height);
    return true;
}

bool EnsurePauseCaptureTexture(Uint32 width, Uint32 height)
{
    if (width == 0 || height == 0)
    {
        return false;
    }

    if (g_pause_capture_texture != nullptr &&
        g_pause_capture_width == width && g_pause_capture_height == height)
    {
        return true;
    }

    if (g_pause_capture_texture != nullptr)
    {
        SDL_ReleaseGPUTexture(g_device, g_pause_capture_texture);
        g_pause_capture_texture = nullptr;
    }

    SDL_GPUTextureCreateInfo info{};
    info.type = SDL_GPU_TEXTURETYPE_2D;
    info.format = g_scene_format;
    info.usage = SDL_GPU_TEXTUREUSAGE_SAMPLER |
                 SDL_GPU_TEXTUREUSAGE_COLOR_TARGET;
    info.width = width;
    info.height = height;
    info.layer_count_or_depth = 1;
    info.num_levels = 1;
    info.sample_count = SDL_GPU_SAMPLECOUNT_1;

    g_pause_capture_texture = SDL_CreateGPUTexture(g_device, &info);
    if (g_pause_capture_texture == nullptr)
    {
        LogSdlError("SDL_CreateGPUTexture(pause capture)");
        g_pause_capture_width = 0;
        g_pause_capture_height = 0;
        return false;
    }

    g_pause_capture_width = width;
    g_pause_capture_height = height;
    return true;
}

/*
 * The mid-frame scene capture, for GS frame-buffer sampling.
 *
 * On the PS2 an effect that samples TBP0 = 0x0000 / 0x1180 is reading the
 * display buffer itself, so it sees everything the GS has rasterised up to
 * that instant.  The host renderer is deferred -- draws are queued and replayed
 * at present -- so the equivalent is to split the replay at the sampling draw,
 * copy the colour target into this texture, and let the draw sample it.
 *
 * PORT DEVIATION, and the honest limit of the approach: the PS2 has two
 * display pages and the effects sample both (`(count & 1) * 0x1180` is the page
 * being drawn into, `(count + 1 & 1) * 0x1180` the one on screen).  The host
 * has a single output surface, so both resolve to "the frame as drawn so far".
 * For the page being drawn into that is exact; for the displayed page it is one
 * composite early, which is what the effects wanted anyway -- they use it to
 * smear or tint the picture the player is looking at.
 */
bool EnsureSceneCaptureTexture(Uint32 width, Uint32 height)
{
    if (width == 0 || height == 0 ||
        g_scene_format == SDL_GPU_TEXTUREFORMAT_INVALID)
    {
        return false;
    }

    bool all_ready = true;
    for (CaptureSlot &slot : g_capture_slots)
    {
        /* The PS2 copy's own resolution, carried over to the output's scale.
         * A 320x448 page really is half-width here, so stretching it back over
         * the screen softens exactly as it did on the GS. */
        Uint32 want_w = (Uint32)std::max(
            1L, std::lround((double)width * (double)slot.logical_w /
                            (double)kLogicalWidth));
        Uint32 want_h = (Uint32)std::max(
            1L, std::lround((double)height * (double)slot.logical_h /
                            (double)kLogicalHeight));

        if (slot.texture != nullptr && slot.tex_w == want_w &&
            slot.tex_h == want_h)
        {
            continue;
        }

        if (slot.texture != nullptr)
        {
            SDL_ReleaseGPUTexture(g_device, slot.texture);
            slot.texture = nullptr;
            slot.valid = false;
        }

        SDL_GPUTextureCreateInfo info{};
        info.type = SDL_GPU_TEXTURETYPE_2D;
        info.format = g_scene_format;
        info.usage = SDL_GPU_TEXTUREUSAGE_SAMPLER |
                     SDL_GPU_TEXTUREUSAGE_COLOR_TARGET;
        info.width = want_w;
        info.height = want_h;
        info.layer_count_or_depth = 1;
        info.num_levels = 1;
        info.sample_count = SDL_GPU_SAMPLECOUNT_1;

        slot.texture = SDL_CreateGPUTexture(g_device, &info);
        if (slot.texture == nullptr)
        {
            LogSdlError("SDL_CreateGPUTexture(GS capture slot)");
            slot.tex_w = 0;
            slot.tex_h = 0;
            all_ready = false;
            continue;
        }
        slot.tex_w = want_w;
        slot.tex_h = want_h;
    }
    return all_ready;
}

/*
 * Slots are keyed by GS address *and* logical size, not address alone: the
 * game reuses one page for differently-shaped copies.  0x2bc0 is the effects'
 * 320x448 refraction scratch and also pause.c's 640x448 screen grab, and a
 * single slot would thrash between the two sizes every time the player paused.
 *
 * Indices rather than pointers throughout, because declaring a slot can
 * reallocate the vector.
 */
int FindCaptureSlotIndex(unsigned int addr, int logical_w, int logical_h)
{
    for (size_t i = 0; i < g_capture_slots.size(); i++)
    {
        const CaptureSlot &slot = g_capture_slots[i];
        if (slot.addr == addr && slot.logical_w == logical_w &&
            slot.logical_h == logical_h)
        {
            return (int)i;
        }
    }
    return -1;
}

/*
 * Declare a slot for `addr` at the logical size the PS2 copy produces, and
 * return its index.
 *
 * Only records the request: the texture itself is created in
 * UpdateViewExtend(), the one point in the frame where no queued draw holds a
 * slot texture yet.
 */
int DeclareCaptureSlot(unsigned int addr, int logical_w, int logical_h)
{
    if (logical_w <= 0 || logical_h <= 0)
    {
        return -1;
    }

    int index = FindCaptureSlotIndex(addr, logical_w, logical_h);
    if (index >= 0)
    {
        return index;
    }

    CaptureSlot fresh{};
    fresh.addr = addr;
    fresh.logical_w = logical_w;
    fresh.logical_h = logical_h;
    g_capture_slots.push_back(fresh);
    g_scene_capture_requested = true;
    return (int)g_capture_slots.size() - 1;
}

/*
 * Execute one capture point: copy the live colour target, or another slot,
 * into `dst`.  Called between two render passes, so it is ordered against
 * both -- the draws before it have landed, and the draws after it sample the
 * result.
 *
 * Deliberately allocates nothing.  Draws queued earlier in the frame already
 * hold slot texture pointers, so reallocating here on a resize would leave
 * them dangling; the sizes settled at BeginFrame are used as they are.
 *
 * A scaling blit rather than a straight copy, because the PS2 copy scales:
 * type 1 takes 640x448 down to 320x224, and that resolution loss is part of
 * how the effect looks.
 */
bool RecordGsCapture(SDL_GPUCommandBuffer *cmd, SDL_GPUTexture *live,
                     Uint32 live_w, Uint32 live_h, const CapturePoint &point)
{
    if (cmd == nullptr)
    {
        return false;
    }

    if (point.dst_slot < 0 || (size_t)point.dst_slot >= g_capture_slots.size())
    {
        return false;
    }
    CaptureSlot *dst = &g_capture_slots[(size_t)point.dst_slot];
    if (dst->texture == nullptr)
    {
        return false;
    }

    SDL_GPUTexture *source = live;
    Uint32 source_w = live_w;
    Uint32 source_h = live_h;
    if (point.src_slot >= 0)
    {
        if ((size_t)point.src_slot >= g_capture_slots.size())
        {
            return false;
        }
        const CaptureSlot &src = g_capture_slots[(size_t)point.src_slot];
        if (src.texture == nullptr || !src.valid)
        {
            return false;
        }
        source = src.texture;
        source_w = src.tex_w;
        source_h = src.tex_h;
    }

    if (source == nullptr || source_w == 0 || source_h == 0)
    {
        return false;
    }

    SDL_GPUBlitInfo blit{};
    blit.source.texture = source;
    blit.source.w = source_w;
    blit.source.h = source_h;
    blit.destination.texture = dst->texture;
    blit.destination.w = dst->tex_w;
    blit.destination.h = dst->tex_h;
    blit.load_op = SDL_GPU_LOADOP_DONT_CARE;
    blit.filter = SDL_GPU_FILTER_LINEAR;
    blit.cycle = false;
    SDL_BlitGPUTexture(cmd, &blit);

    dst->valid = true;
    return true;
}

bool RecordPauseScreenCapture(SDL_GPUCommandBuffer *cmd,
                              SDL_GPUTexture *source,
                              Uint32 width, Uint32 height)
{
    if (!g_pause_capture_pending)
    {
        return false;
    }

    g_pause_capture_pending = false;
    g_pause_capture_valid = false;
    if (cmd == nullptr || source == nullptr ||
        !EnsurePauseCaptureTexture(width, height))
    {
        return false;
    }

    SDL_GPUTextureLocation src{};
    src.texture = source;
    SDL_GPUTextureLocation dst{};
    dst.texture = g_pause_capture_texture;

    SDL_GPUCopyPass *copy = SDL_BeginGPUCopyPass(cmd);
    if (copy == nullptr)
    {
        LogSdlError("SDL_BeginGPUCopyPass(pause capture)");
        return false;
    }
    SDL_CopyGPUTextureToTexture(copy, &src, &dst, width, height, 1, false);
    SDL_EndGPUCopyPass(copy);
    return true;
}

/*
 * Keep the screen mirror.  One downscaling blit of the finished frame into a
 * 640x448 texture, recorded where the pause capture is -- after every game
 * draw and before the host UI pass, so the debug overlay never lands in a
 * photograph.
 *
 * Unconditional rather than armed on demand, and that is deliberate: the
 * read-back has to answer on the frame it is asked, and by then the frame it
 * wants has already been presented.  Arming lazily would hand the first
 * photograph of a session an empty mirror.  The cost is a filtered blit down to
 * 286 KB, which does not scale with the window.
 */
bool EnsureScreenMirrorTexture()
{
    if (g_scene_format == SDL_GPU_TEXTUREFORMAT_INVALID)
    {
        return false;
    }
    if (g_screen_mirror_texture != nullptr)
    {
        return true;
    }

    SDL_GPUTextureCreateInfo info{};
    info.type = SDL_GPU_TEXTURETYPE_2D;
    /* kScreenMirrorFormat, not the scene's: the read-back below indexes the
     * download as bytes, and it is filled by a blit, which converts. */
    info.format = kScreenMirrorFormat;
    info.usage = SDL_GPU_TEXTUREUSAGE_SAMPLER |
                 SDL_GPU_TEXTUREUSAGE_COLOR_TARGET;
    info.width = (Uint32)kLogicalWidth;
    info.height = (Uint32)kLogicalHeight;
    info.layer_count_or_depth = 1;
    info.num_levels = 1;
    info.sample_count = SDL_GPU_SAMPLECOUNT_1;

    g_screen_mirror_texture = SDL_CreateGPUTexture(g_device, &info);
    if (g_screen_mirror_texture == nullptr)
    {
        LogSdlError("SDL_CreateGPUTexture(screen mirror)");
        return false;
    }

    g_screen_mirror_width = (Uint32)kLogicalWidth;
    g_screen_mirror_height = (Uint32)kLogicalHeight;
    g_screen_mirror_valid = false;
    return true;
}

bool RecordScreenMirror(SDL_GPUCommandBuffer *cmd, SDL_GPUTexture *source,
                        Uint32 width, Uint32 height)
{
    if (cmd == nullptr || source == nullptr || width == 0 || height == 0 ||
        !EnsureScreenMirrorTexture())
    {
        return false;
    }

    /*
     * The source is the part of the window the ROM's 640x448 frame occupies,
     * not the whole window.
     *
     * ApplyOriginalAspectToVertices() shrinks every screen-space draw by
     * 1/g_view_extend_* about the centre, so on a window that is not 4:3 the
     * 2D layer lives inside a centred band and the 3D projection's extra world
     * spills past it.  Everything read back out of this mirror is addressed in
     * those 640x448 coordinates -- photo.c crops (128,96)-(512,352) out of it
     * -- so taking the whole window instead put a chunk of the world from
     * outside the photo frame into the photograph, squashed.
     *
     * Capturing the band alone also keeps the picture's field of view right:
     * the projection widened by exactly g_view_extend_x, so the central
     * 1/g_view_extend_x of it is the original 4:3 view.
     */
    Uint32 src_w = (Uint32)std::max(
        1L, std::lround((double)width / (double)g_view_extend_x));
    Uint32 src_h = (Uint32)std::max(
        1L, std::lround((double)height / (double)g_view_extend_y));
    src_w = std::min(src_w, width);
    src_h = std::min(src_h, height);

    SDL_GPUBlitInfo blit{};
    blit.source.texture = source;
    blit.source.x = (width - src_w) / 2;
    blit.source.y = (height - src_h) / 2;
    blit.source.w = src_w;
    blit.source.h = src_h;
    blit.destination.texture = g_screen_mirror_texture;
    blit.destination.w = g_screen_mirror_width;
    blit.destination.h = g_screen_mirror_height;
    blit.load_op = SDL_GPU_LOADOP_DONT_CARE;
    blit.filter = SDL_GPU_FILTER_LINEAR;
    blit.cycle = false;
    SDL_BlitGPUTexture(cmd, &blit);
    return true;
}

/*
 * The eight filter/address combinations for one anisotropy level.
 *
 * Built per level rather than reconfigured, because a recorded command buffer
 * holds sampler pointers and a live setting change must not pull one out from
 * under it -- the same rule the pipeline caches follow.  Slot 0 is anisotropy
 * off and is required; every other slot is built the first time the setting
 * selects it, and a level the device refuses simply leaves the previous one in
 * use.
 *
 * On mipmap_mode: LINEAR is what these should have said all along, but with
 * num_levels 1 everywhere it selects nothing today.  What anisotropy buys
 * without mip levels is still real -- the LOD clamps to 0 and the hardware
 * takes up to N taps along the major axis of the texel footprint and averages
 * them, which is texture-space supersampling and is exactly what a tatami mat
 * or a corridor floor at a grazing angle needs.  What it cannot do without
 * mips is help the far end of that floor, where the minor axis has minified
 * past level 0 as well.  Generating mip chains for GS-decoded pages is a
 * separate question and a much larger one: these are packed sheets, and a
 * level 1 would bleed one sub-image into the next.
 */
bool CreateSamplerSet(int aniso_slot)
{
    if (aniso_slot < 0 || aniso_slot >= kAnisoSlotCount)
    {
        return false;
    }
    /* The last one the loops below fill, so a set that failed part way is not
     * mistaken for a complete one on the next attempt. */
    if (g_samplers[aniso_slot][1][1][1] != nullptr)
    {
        return true;
    }

    const int anisotropy = 1 << aniso_slot;
    for (int repeat = 0; repeat < 2; repeat++)
    {
        for (int min_linear = 0; min_linear < 2; min_linear++)
        {
            for (int mag_linear = 0; mag_linear < 2; mag_linear++)
            {
                SDL_GPUSamplerCreateInfo sampler_info{};
                sampler_info.min_filter = min_linear
                    ? SDL_GPU_FILTER_LINEAR : SDL_GPU_FILTER_NEAREST;
                sampler_info.mag_filter = mag_linear
                    ? SDL_GPU_FILTER_LINEAR : SDL_GPU_FILTER_NEAREST;
                sampler_info.mipmap_mode = SDL_GPU_SAMPLERMIPMAPMODE_LINEAR;
                sampler_info.address_mode_u = repeat
                    ? SDL_GPU_SAMPLERADDRESSMODE_REPEAT
                    : SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
                sampler_info.address_mode_v = sampler_info.address_mode_u;
                sampler_info.address_mode_w = sampler_info.address_mode_u;
                /*
                 * Only on an all-LINEAR filter, and that is a hard
                 * requirement rather than a preference.
                 *
                 * SDL's D3D12 backend builds a basic filter from the three
                 * modes and then ORs D3D12_ANISOTROPIC_FILTERING_BIT onto it,
                 * but D3D12 defines exactly one anisotropic filter value --
                 * MIN_MAG_MIP_LINEAR plus that bit.  Ask for anisotropy on,
                 * say, min LINEAR / mag POINT and the OR produces a value that
                 * is not a D3D12_FILTER at all.  ID3D12Device::CreateSampler
                 * returns void, so nothing reports it: the sampler appears to
                 * be created and the device then answers DXGI_ERROR_INVALID_CALL
                 * to every later CreateCommittedResource and
                 * CreateGraphicsPipelineState, which reads as the renderer
                 * failing to start for no reason at all.
                 *
                 * It is also the right rule on its own terms.  A NEAREST
                 * minification is the PS2's TEX1 asking for point sampling,
                 * and averaging N taps under it would quietly turn it into
                 * something else.
                 */
                if (anisotropy > 1 && min_linear && mag_linear)
                {
                    sampler_info.enable_anisotropy = true;
                    sampler_info.max_anisotropy = (float)anisotropy;
                }
                SDL_GPUSampler *sampler =
                    SDL_CreateGPUSampler(g_device, &sampler_info);
                if (sampler == nullptr && sampler_info.enable_anisotropy)
                {
                    /* A device built without samplerAnisotropy.  Fall back to
                     * the plain sampler rather than failing the level: the
                     * setting then does nothing, which is what the UI reports
                     * once the granted level comes back as 1. */
                    sampler_info.enable_anisotropy = false;
                    sampler_info.max_anisotropy = 0.0f;
                    sampler = SDL_CreateGPUSampler(g_device, &sampler_info);
                    g_anisotropy_max_supported = 1;
                }
                if (sampler == nullptr)
                {
                    LogSdlError("SDL_CreateGPUSampler");
                    return false;
                }
                g_samplers[aniso_slot][repeat][min_linear][mag_linear] =
                    sampler;
            }
        }
    }
    return true;
}

/* The set this frame's material draws sample through.  Falls back to the plain
 * set whenever the chosen level could not be built. */
int ActiveAnisoSlot()
{
    int slot = 0;
    for (int bit = 1; bit < kAnisoSlotCount; bit++)
    {
        if ((1 << bit) <= g_anisotropy_active)
        {
            slot = bit;
        }
    }
    if (g_samplers[slot][1][1][1] == nullptr)
    {
        return 0;
    }
    return slot;
}

bool CreateCommonResources()
{
    if (!CreateSamplerSet(0))
    {
        return false;
    }

    const unsigned char white[4] = {0xff, 0xff, 0xff, 0xff};
    g_white_texture = CreateTexture(1, 1, white);
    if (g_white_texture == nullptr)
    {
        return false;
    }

    return true;
}

/*
 * What the device will actually give us.
 *
 * Both attachments have to agree, so a sample count counts as supported only
 * when the scene colour format and the depth format both take it.  Asked once
 * the two formats are settled and again if the depth format ever moves.
 */
void RefreshMsaaSupport()
{
    g_msaa_max_supported = 1;
    if (g_device == nullptr ||
        g_scene_format == SDL_GPU_TEXTUREFORMAT_INVALID)
    {
        return;
    }
    for (int samples = 2; samples <= kMaxSampleCount; samples <<= 1)
    {
        const SDL_GPUSampleCount count = SdlSampleCount(samples);
        if (SDL_GPUTextureSupportsSampleCount(g_device, g_scene_format,
                                              count) &&
            SDL_GPUTextureSupportsSampleCount(g_device, g_depth_format, count))
        {
            g_msaa_max_supported = samples;
        }
        else
        {
            break;      /* counts are supported downward, not in gaps */
        }
    }
}

/*
 * Resolve the MSAA request against what the device grants.
 *
 * Does not build anything: pipelines for the new count are warmed by the
 * caller, and the target itself is allocated in RecordFrame, which is also
 * where a failed allocation falls back to 1x for the frame.
 */
void ApplyMsaaSetting()
{
    int granted = g_msaa_request;
    if (granted < 1)
    {
        granted = 1;
    }
    if (granted > g_msaa_max_supported)
    {
        granted = g_msaa_max_supported;
    }
    /* Round down to a power of two the enum has a value for. */
    granted = SampleCountForSlot(SampleSlotForCount(granted));

    if (granted == g_msaa_active)
    {
        return;
    }
    g_msaa_active = granted;
    if (granted <= 1)
    {
        ReleaseMsaaColorTexture();
    }
    SDL_Log("MioPan SDL_GPU: MSAA %dx%s", granted,
            granted != g_msaa_request ? " (requested level unsupported)" : "");
}

/* The same for anisotropy, and it does build: a level's eight samplers are
 * needed before the first draw that would use them. */
void ApplyAnisotropySetting()
{
    int granted = g_anisotropy_request;
    if (granted < 1)
    {
        granted = 1;
    }
    if (granted > kMaxAnisotropy)
    {
        granted = kMaxAnisotropy;
    }
    if (granted > g_anisotropy_max_supported)
    {
        granted = g_anisotropy_max_supported;
    }

    int slot = 0;
    for (int bit = 1; bit < kAnisoSlotCount; bit++)
    {
        if ((1 << bit) <= granted)
        {
            slot = bit;
        }
    }
    if (g_device != nullptr && !CreateSamplerSet(slot))
    {
        slot = 0;
    }
    granted = g_samplers[slot][1][1][1] != nullptr ? (1 << slot) : 1;

    if (granted == g_anisotropy_active)
    {
        return;
    }
    g_anisotropy_active = granted;
    SDL_Log("MioPan SDL_GPU: anisotropic filtering %dx", granted);
}

uint64_t ReadTex0Value(const sceGsTex0 *tex0)
{
    uint64_t value = 0;

    if (tex0 != nullptr)
    {
        std::memcpy(&value, tex0, std::min(sizeof(value), sizeof(*tex0)));
    }

    return value;
}

bool RangesOverlap(int a_addr, int a_size, int b_addr, int b_size)
{
    if (a_size <= 0 || b_size <= 0)
    {
        return false;
    }

    const int a_end = a_addr + a_size;
    const int b_end = b_addr + b_size;
    return a_addr < b_end && b_addr < a_end;
}

void InvalidateTex0CacheRange(int addr, int size, void *ud)
{
    (void)ud;

    if (size <= 0)
    {
        return;
    }

    for (auto it = g_tex0_cache.begin(); it != g_tex0_cache.end();)
    {
        if (RangesOverlap(addr, size, it->second.gs_addr, it->second.gs_size))
        {
            it = g_tex0_cache.erase(it);
            g_texture_invalidations++;
        }
        else
        {
            ++it;
        }
    }

    for (int i = 0; i < kFontTextureBankCount; i++)
    {
        FontTextureEntry &font = g_font_textures[i];
        if (font.valid && RangesOverlap(addr, size, font.gs_addr, font.gs_size))
        {
            font.valid = false;
            font.texture = nullptr;
            g_font_texture_invalidations++;
            if (g_current_font_bank == i)
            {
                g_current_font_texture = nullptr;
            }
        }
    }
}

void ConsumeTextureInvalidations()
{
    if (MioPan_GsHasPendingUploads() != 0)
    {
        MioPanProfileScope profile_scope(MIOPAN_PROFILE_TEXTURE_INVALIDATE);
        MioPan_GsConsumePendingUploads(InvalidateTex0CacheRange, nullptr);
    }
}

void RegisterTex0CacheEntry(uint64_t tex0_value, const sceGsTex0 *tex0,
                            TextureEntry *entry)
{
    int gs_addr = 0;
    int gs_size = 0;

    if (tex0 == nullptr || entry == nullptr)
    {
        return;
    }

    MioPan_GetTextureGsRegion((sceGsTex0 *)tex0, &gs_addr, &gs_size);
    if (gs_size <= 0)
    {
        return;
    }

    g_tex0_cache[tex0_value] = {entry, gs_addr, gs_size};
}

TextureEntry *GetFontTexture(int bank, const sceGsTex0 *tex0)
{
    if (bank < 0 || bank >= kFontTextureBankCount || tex0 == nullptr)
    {
        return nullptr;
    }

    ConsumeTextureInvalidations();

    const uint64_t tex0_value = ReadTex0Value(tex0);
    FontTextureEntry &font = g_font_textures[bank];
    if (font.valid && font.tex0_value == tex0_value && font.texture != nullptr &&
        font.texture->texture != nullptr)
    {
        g_font_texture_hits++;
        return font.texture;
    }

    TextureEntry *entry = GetTexture(tex0);
    if (entry == nullptr || entry->texture == nullptr)
    {
        font.valid = false;
        font.texture = nullptr;
        return nullptr;
    }

    int gs_addr = 0;
    int gs_size = 0;
    MioPan_GetTextureGsRegion((sceGsTex0 *)tex0, &gs_addr, &gs_size);
    if (gs_size <= 0)
    {
        font.valid = false;
        font.texture = nullptr;
        return nullptr;
    }

    font.texture = entry;
    font.tex0_value = tex0_value;
    font.gs_addr = gs_addr;
    font.gs_size = gs_size;
    font.valid = true;
    g_font_texture_creates++;
    return entry;
}

bool EnsureVertexBuffer(size_t byte_count)
{
    if (byte_count == 0)
    {
        return true;
    }

    if (byte_count > std::numeric_limits<Uint32>::max())
    {
        return false;
    }

    if (g_vertex_buffer != nullptr && byte_count <= g_vertex_buffer_size)
    {
        return true;
    }

    Uint32 new_size = kInitialVertexBufferSize;
    while (new_size < byte_count && new_size <= (Uint32)1 << 30)
    {
        new_size *= 2;
    }
    if (new_size < byte_count)
    {
        return false;
    }

    SDL_GPUBufferCreateInfo info{};
    info.usage = SDL_GPU_BUFFERUSAGE_VERTEX;
    info.size = new_size;
    SDL_GPUBuffer *replacement = SDL_CreateGPUBuffer(g_device, &info);
    if (replacement == nullptr)
    {
        LogSdlError("SDL_CreateGPUBuffer");
        return false;
    }

    if (g_vertex_buffer != nullptr)
    {
        /* SDL retains submitted resources until the GPU is finished with
         * them.  Swapping the grow-only stream therefore needs no global GPU
         * drain; the old buffer can be released just like the colour and
         * animated streams below.  Door transitions often render both rooms
         * and cross this high-water mark in one frame. */
        SDL_ReleaseGPUBuffer(g_device, g_vertex_buffer);
    }
    g_vertex_buffer = replacement;
    g_vertex_buffer_size = new_size;
    return true;
}

bool UploadVertexBuffer(SDL_GPUCommandBuffer *cmd,
                        SDL_GPUTransferBuffer **out_transfer)
{
    const size_t byte_count = g_vertices.size() * sizeof(SpriteVertex);
    if (out_transfer != nullptr)
    {
        *out_transfer = nullptr;
    }
    if (byte_count == 0)
    {
        return true;
    }

    if (!EnsureVertexBuffer(byte_count))
    {
        return false;
    }

    SDL_GPUTransferBufferCreateInfo transfer_info{};
    transfer_info.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
    transfer_info.size = (Uint32)byte_count;
    SDL_GPUTransferBuffer *transfer =
        SDL_CreateGPUTransferBuffer(g_device, &transfer_info);
    if (transfer == nullptr)
    {
        LogSdlError("SDL_CreateGPUTransferBuffer");
        return false;
    }

    void *mapped = SDL_MapGPUTransferBuffer(g_device, transfer, true);
    if (mapped == nullptr)
    {
        LogSdlError("SDL_MapGPUTransferBuffer");
        SDL_ReleaseGPUTransferBuffer(g_device, transfer);
        return false;
    }
    std::memcpy(mapped, g_vertices.data(), byte_count);
    SDL_UnmapGPUTransferBuffer(g_device, transfer);

    SDL_GPUTransferBufferLocation src{};
    src.transfer_buffer = transfer;
    src.offset = 0;

    SDL_GPUBufferRegion dst{};
    dst.buffer = g_vertex_buffer;
    dst.offset = 0;
    dst.size = (Uint32)byte_count;

    SDL_GPUCopyPass *copy = SDL_BeginGPUCopyPass(cmd);
    SDL_UploadToGPUBuffer(copy, &src, &dst, true);
    SDL_EndGPUCopyPass(copy);
    if (out_transfer != nullptr)
    {
        *out_transfer = transfer;
    }
    else
    {
        SDL_ReleaseGPUTransferBuffer(g_device, transfer);
    }
    return true;
}

bool EnsureMeshColourBuffer(size_t byte_count)
{
    if (byte_count == 0)
    {
        return true;
    }
    if (byte_count > std::numeric_limits<Uint32>::max())
    {
        return false;
    }
    if (g_mesh_colour_buffer != nullptr &&
        byte_count <= g_mesh_colour_buffer_size)
    {
        return true;
    }

    Uint32 new_size = kInitialMeshColourBufferSize;
    while (new_size < byte_count && new_size <= ((Uint32)1 << 30))
    {
        new_size *= 2;
    }
    if (new_size < byte_count)
    {
        return false;
    }

    SDL_GPUBufferCreateInfo info{};
    info.usage = SDL_GPU_BUFFERUSAGE_VERTEX;
    info.size = new_size;
    SDL_GPUBuffer *replacement = SDL_CreateGPUBuffer(g_device, &info);
    if (replacement == nullptr)
    {
        LogSdlError("SDL_CreateGPUBuffer(mesh colours)");
        return false;
    }

    if (g_mesh_colour_buffer != nullptr)
    {
        /* SDL defers destruction while submitted work still references the
         * old backing store; growing this stream does not need a GPU stall. */
        SDL_ReleaseGPUBuffer(g_device, g_mesh_colour_buffer);
    }
    g_mesh_colour_buffer = replacement;
    g_mesh_colour_buffer_size = new_size;
    return true;
}

bool EnsureAnimatedMeshBuffer(size_t byte_count)
{
    if (byte_count == 0)
    {
        return true;
    }
    if (byte_count > std::numeric_limits<Uint32>::max())
    {
        return false;
    }
    if (g_animated_mesh_buffer != nullptr &&
        byte_count <= g_animated_mesh_buffer_size)
    {
        return true;
    }

    Uint32 new_size = kInitialAnimatedMeshBufferSize;
    while (new_size < byte_count && new_size <= ((Uint32)1 << 30))
    {
        new_size *= 2;
    }
    if (new_size < byte_count)
    {
        return false;
    }

    SDL_GPUBufferCreateInfo info{};
    info.usage = SDL_GPU_BUFFERUSAGE_VERTEX;
    info.size = new_size;
    SDL_GPUBuffer *replacement = SDL_CreateGPUBuffer(g_device, &info);
    if (replacement == nullptr)
    {
        LogSdlError("SDL_CreateGPUBuffer(animated meshes)");
        return false;
    }

    if (g_animated_mesh_buffer != nullptr)
    {
        SDL_ReleaseGPUBuffer(g_device, g_animated_mesh_buffer);
    }
    g_animated_mesh_buffer = replacement;
    g_animated_mesh_buffer_size = new_size;
    return true;
}

size_t MeshCachePendingVertexBytes(const MeshCacheEntry &entry)
{
    return entry.key.layout_kind == MIOPAN_MESH_CACHE_ANIMATED
        ? entry.pending_animated_vertices.size() *
              sizeof(MeshAnimatedStaticVertex)
        : entry.pending_vertices.size() * sizeof(MeshStaticVertex);
}

const void *MeshCachePendingVertexData(const MeshCacheEntry &entry)
{
    return entry.key.layout_kind == MIOPAN_MESH_CACHE_ANIMATED
        ? static_cast<const void *>(entry.pending_animated_vertices.data())
        : static_cast<const void *>(entry.pending_vertices.data());
}

bool MeshCacheHasPendingVertices(const MeshCacheEntry &entry)
{
    return entry.key.layout_kind == MIOPAN_MESH_CACHE_ANIMATED
        ? !entry.pending_animated_vertices.empty()
        : !entry.pending_vertices.empty();
}

bool MeshCacheHasGpuStorage(const MeshCacheEntry &entry)
{
    return entry.vertex_buffer != nullptr && entry.index_buffer != nullptr &&
           entry.vertex_slice.page != nullptr &&
           entry.index_slice.page != nullptr;
}

bool EvictMeshArenaVictim(const MeshCacheEntry *exclude)
{
    if (g_mesh_arena_evictions_this_frame >=
        kMeshArenaEvictionBudgetEntries)
    {
        return false;
    }
    auto victim = g_mesh_cache.end();
    for (auto it = g_mesh_cache.begin(); it != g_mesh_cache.end(); ++it)
    {
        const MeshCacheEntryPtr &candidate = it->second;
        if (candidate == nullptr || candidate.get() == exclude ||
            candidate.use_count() != 1 ||
            !MeshCacheHasGpuStorage(*candidate))
        {
            continue;
        }
        if (victim == g_mesh_cache.end() ||
            PreferMeshCacheVictim(*candidate, *victim->second))
        {
            victim = it;
        }
    }
    if (victim == g_mesh_cache.end())
    {
        return false;
    }
    g_mesh_cache.erase(victim);
    g_mesh_cache_evictions++;
    g_mesh_arena_evictions_this_frame++;
    return true;
}

bool PromoteMeshCacheEntry(MeshCacheEntry *entry)
{
    if (entry == nullptr || !MeshCacheHasPendingVertices(*entry) ||
        entry->pending_indices.empty())
    {
        return false;
    }

    const size_t vertex_bytes = MeshCachePendingVertexBytes(*entry);
    const size_t index_bytes = entry->pending_indices.size() * sizeof(Uint32);
    if (vertex_bytes > std::numeric_limits<Uint32>::max() ||
        index_bytes > std::numeric_limits<Uint32>::max() ||
        vertex_bytes + index_bytes > kMeshCacheMaxEntryBytes)
    {
        return false;
    }

    if (MeshCacheHasGpuStorage(*entry))
    {
        return true;
    }

    for (;;)
    {
        MeshArenaSlice vertex_slice{};
        MeshArenaSlice index_slice{};
        const bool vertex_allocated = AllocateMeshArenaSlice(
            g_mesh_vertex_arena, vertex_bytes, &vertex_slice);
        const bool index_allocated = vertex_allocated &&
            AllocateMeshArenaSlice(g_mesh_index_arena, index_bytes,
                                   &index_slice);
        if (vertex_allocated && index_allocated)
        {
            entry->vertex_slice = vertex_slice;
            entry->index_slice = index_slice;
            entry->vertex_buffer = vertex_slice.page->buffer;
            entry->index_buffer = index_slice.page->buffer;
            entry->vertex_buffer_offset = vertex_slice.offset;
            entry->index_buffer_offset = index_slice.offset;
            return true;
        }

        /* Treat the pair transactionally.  In particular, an index-arena
         * miss must not strand the vertex slice while an LRU entry is evicted
         * to make room for a retry. */
        FreeMeshArenaSlice(&vertex_slice);
        FreeMeshArenaSlice(&index_slice);
        if (!EvictMeshArenaVictim(entry))
        {
            return false;
        }
    }
}

struct MeshUploadResult
{
    bool colours_ready = true;
    bool animated_vertices_ready = true;
    size_t colour_bytes = 0;
    size_t animated_bytes = 0;
    std::vector<MeshCacheEntryPtr> uploaded_entries;
    std::vector<MeshCacheEntryPtr> failed_entries;
    std::vector<MeshCacheEntryPtr> retry_entries;
    std::vector<MeshCacheEntryPtr> deferred_entries;
    std::vector<SDL_GPUTransferBuffer *> transfers;
};

bool UploadMeshColours(SDL_GPUCommandBuffer *cmd, MeshUploadResult &result)
{
    const size_t colour_bytes = g_mesh_colours.size() * sizeof(MeshColour);
    if (colour_bytes == 0)
    {
        return true;
    }
    if (colour_bytes > std::numeric_limits<Uint32>::max() ||
        !EnsureMeshColourBuffer(colour_bytes))
    {
        return false;
    }

    SDL_GPUTransferBufferCreateInfo transfer_info{};
    transfer_info.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
    transfer_info.size = (Uint32)colour_bytes;
    SDL_GPUTransferBuffer *transfer =
        SDL_CreateGPUTransferBuffer(g_device, &transfer_info);
    if (transfer == nullptr)
    {
        LogSdlError("SDL_CreateGPUTransferBuffer(mesh colours)");
        return false;
    }

    void *mapped = SDL_MapGPUTransferBuffer(g_device, transfer, true);
    if (mapped == nullptr)
    {
        LogSdlError("SDL_MapGPUTransferBuffer(mesh colours)");
        SDL_ReleaseGPUTransferBuffer(g_device, transfer);
        return false;
    }
    std::memcpy(mapped, g_mesh_colours.data(), colour_bytes);
    SDL_UnmapGPUTransferBuffer(g_device, transfer);

    SDL_GPUCopyPass *copy = SDL_BeginGPUCopyPass(cmd);
    if (copy == nullptr)
    {
        LogSdlError("SDL_BeginGPUCopyPass(mesh colours)");
        SDL_ReleaseGPUTransferBuffer(g_device, transfer);
        return false;
    }
    SDL_GPUTransferBufferLocation src{};
    src.transfer_buffer = transfer;
    src.offset = 0;
    SDL_GPUBufferRegion dst{};
    dst.buffer = g_mesh_colour_buffer;
    dst.offset = 0;
    dst.size = (Uint32)colour_bytes;
    SDL_UploadToGPUBuffer(copy, &src, &dst, true);
    SDL_EndGPUCopyPass(copy);

    result.colour_bytes = colour_bytes;
    result.transfers.push_back(transfer);
    return true;
}

bool UploadAnimatedMeshVertices(SDL_GPUCommandBuffer *cmd,
                                MeshUploadResult &result)
{
    const size_t byte_count =
        g_animated_mesh_vertices_stream.size() * sizeof(AnimatedMeshVertex);
    if (byte_count == 0)
    {
        return true;
    }
    if (byte_count > std::numeric_limits<Uint32>::max() ||
        !EnsureAnimatedMeshBuffer(byte_count))
    {
        return false;
    }

    SDL_GPUTransferBufferCreateInfo transfer_info{};
    transfer_info.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
    transfer_info.size = (Uint32)byte_count;
    SDL_GPUTransferBuffer *transfer =
        SDL_CreateGPUTransferBuffer(g_device, &transfer_info);
    if (transfer == nullptr)
    {
        LogSdlError("SDL_CreateGPUTransferBuffer(animated meshes)");
        return false;
    }

    void *mapped = SDL_MapGPUTransferBuffer(g_device, transfer, true);
    if (mapped == nullptr)
    {
        LogSdlError("SDL_MapGPUTransferBuffer(animated meshes)");
        SDL_ReleaseGPUTransferBuffer(g_device, transfer);
        return false;
    }
    std::memcpy(mapped, g_animated_mesh_vertices_stream.data(), byte_count);
    SDL_UnmapGPUTransferBuffer(g_device, transfer);

    SDL_GPUCopyPass *copy = SDL_BeginGPUCopyPass(cmd);
    if (copy == nullptr)
    {
        LogSdlError("SDL_BeginGPUCopyPass(animated meshes)");
        SDL_ReleaseGPUTransferBuffer(g_device, transfer);
        return false;
    }
    SDL_GPUTransferBufferLocation src{};
    src.transfer_buffer = transfer;
    src.offset = 0;
    SDL_GPUBufferRegion dst{};
    dst.buffer = g_animated_mesh_buffer;
    dst.offset = 0;
    dst.size = (Uint32)byte_count;
    SDL_UploadToGPUBuffer(copy, &src, &dst, true);
    SDL_EndGPUCopyPass(copy);

    result.animated_bytes = byte_count;
    result.transfers.push_back(transfer);
    return true;
}

struct MeshUploadSlice
{
    MeshCacheEntryPtr entry;
    Uint32 vertex_offset;
    Uint32 index_offset;
};

void UploadPendingMeshCacheEntries(
    SDL_GPUCommandBuffer *cmd,
    const std::vector<MeshCacheEntryPtr> &pending,
    MeshUploadResult &result)
{
    const size_t max_upload_bytes = std::numeric_limits<Uint32>::max();
    size_t total_bytes = 0;
    size_t attempted_bytes = 0;
    unsigned int attempted_entries = 0;
    std::vector<MeshUploadSlice> slices;
    slices.reserve(pending.size());

    for (const MeshCacheEntryPtr &entry : pending)
    {
        if (entry == nullptr || !MeshCacheHasPendingVertices(*entry) ||
            entry->pending_indices.empty())
        {
            result.failed_entries.push_back(entry);
            continue;
        }

        const size_t vertex_bytes = MeshCachePendingVertexBytes(*entry);
        const size_t index_bytes =
            entry->pending_indices.size() * sizeof(Uint32);
        const size_t entry_bytes = vertex_bytes + index_bytes;
        const bool budget_exhausted =
            attempted_entries >= kMeshCacheUploadBudgetEntries ||
            entry_bytes > kMeshCacheUploadBudgetBytes -
                              std::min(attempted_bytes,
                                       kMeshCacheUploadBudgetBytes);
        if (budget_exhausted)
        {
            result.deferred_entries.push_back(entry);
            continue;
        }
        attempted_entries++;
        attempted_bytes += entry_bytes;

        const bool was_promoted = MeshCacheHasGpuStorage(*entry);
        if (!PromoteMeshCacheEntry(entry.get()))
        {
            /* Arena pressure can be temporary while current/in-flight draws
             * pin otherwise-evictable slices.  Preserve the CPU topology and
             * retry under a later frame's quota instead of rebuilding it. */
            result.retry_entries.push_back(entry);
            g_mesh_cache_upload_failures++;
            continue;
        }
        if (!was_promoted)
        {
            g_mesh_cache_promotions++;
        }

        if (total_bytes > max_upload_bytes - 15u)
        {
            result.deferred_entries.push_back(entry);
            continue;
        }
        const size_t vertex_offset = AlignUploadOffset(total_bytes);
        if (vertex_bytes > max_upload_bytes - vertex_offset ||
            vertex_offset + vertex_bytes > max_upload_bytes - 15u)
        {
            result.deferred_entries.push_back(entry);
            continue;
        }
        const size_t index_offset =
            AlignUploadOffset(vertex_offset + vertex_bytes);
        if (index_bytes > max_upload_bytes - index_offset)
        {
            result.deferred_entries.push_back(entry);
            continue;
        }

        total_bytes = index_offset + index_bytes;
        slices.push_back({entry, (Uint32)vertex_offset,
                          (Uint32)index_offset});
    }

    g_mesh_cache_upload_deferred += (int)result.deferred_entries.size();
    if (slices.empty())
    {
        return;
    }

    SDL_GPUTransferBufferCreateInfo transfer_info{};
    transfer_info.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
    transfer_info.size = (Uint32)total_bytes;
    SDL_GPUTransferBuffer *transfer =
        SDL_CreateGPUTransferBuffer(g_device, &transfer_info);
    if (transfer == nullptr)
    {
        LogSdlError("SDL_CreateGPUTransferBuffer(mesh cache batch)");
        for (const MeshUploadSlice &slice : slices)
        {
            result.retry_entries.push_back(slice.entry);
        }
        g_mesh_cache_upload_failures += (int)slices.size();
        return;
    }

    unsigned char *mapped = static_cast<unsigned char *>(
        SDL_MapGPUTransferBuffer(g_device, transfer, true));
    if (mapped == nullptr)
    {
        LogSdlError("SDL_MapGPUTransferBuffer(mesh cache batch)");
        SDL_ReleaseGPUTransferBuffer(g_device, transfer);
        for (const MeshUploadSlice &slice : slices)
        {
            result.retry_entries.push_back(slice.entry);
        }
        g_mesh_cache_upload_failures += (int)slices.size();
        return;
    }
    for (const MeshUploadSlice &slice : slices)
    {
        const size_t vertex_bytes =
            MeshCachePendingVertexBytes(*slice.entry);
        const size_t index_bytes =
            slice.entry->pending_indices.size() * sizeof(Uint32);
        std::memcpy(mapped + slice.vertex_offset,
                    MeshCachePendingVertexData(*slice.entry), vertex_bytes);
        std::memcpy(mapped + slice.index_offset,
                    slice.entry->pending_indices.data(), index_bytes);
    }
    SDL_UnmapGPUTransferBuffer(g_device, transfer);

    SDL_GPUCopyPass *copy = SDL_BeginGPUCopyPass(cmd);
    if (copy == nullptr)
    {
        LogSdlError("SDL_BeginGPUCopyPass(mesh cache batch)");
        SDL_ReleaseGPUTransferBuffer(g_device, transfer);
        for (const MeshUploadSlice &slice : slices)
        {
            result.retry_entries.push_back(slice.entry);
        }
        g_mesh_cache_upload_failures += (int)slices.size();
        return;
    }

    for (const MeshUploadSlice &slice : slices)
    {
        const Uint32 vertex_bytes =
            (Uint32)MeshCachePendingVertexBytes(*slice.entry);
        const Uint32 index_bytes = (Uint32)(
            slice.entry->pending_indices.size() * sizeof(Uint32));

        SDL_GPUTransferBufferLocation vertex_src{};
        vertex_src.transfer_buffer = transfer;
        vertex_src.offset = slice.vertex_offset;
        SDL_GPUBufferRegion vertex_dst{};
        vertex_dst.buffer = slice.entry->vertex_buffer;
        vertex_dst.offset = slice.entry->vertex_buffer_offset;
        vertex_dst.size = vertex_bytes;
        SDL_UploadToGPUBuffer(copy, &vertex_src, &vertex_dst, false);

        SDL_GPUTransferBufferLocation index_src{};
        index_src.transfer_buffer = transfer;
        index_src.offset = slice.index_offset;
        SDL_GPUBufferRegion index_dst{};
        index_dst.buffer = slice.entry->index_buffer;
        index_dst.offset = slice.entry->index_buffer_offset;
        index_dst.size = index_bytes;
        SDL_UploadToGPUBuffer(copy, &index_src, &index_dst, false);
    }
    SDL_EndGPUCopyPass(copy);

    for (const MeshUploadSlice &slice : slices)
    {
        slice.entry->upload_recorded_frame = g_mesh_cache_frame;
        result.uploaded_entries.push_back(slice.entry);
    }
    result.transfers.push_back(transfer);
}

void CollectPendingMeshCacheEntries(
    std::vector<MeshCacheEntryPtr> *pending)
{
    if (pending == nullptr)
    {
        return;
    }
    pending->clear();
    std::unordered_set<MeshCacheEntry *> seen;
    for (const DrawCommand &draw : g_draws)
    {
        if (!draw.indexed_mesh || draw.cached_mesh == nullptr ||
            draw.cached_mesh->ready ||
            !seen.insert(draw.cached_mesh.get()).second)
        {
            continue;
        }
        pending->push_back(draw.cached_mesh);
    }
}

bool HasPendingMeshCacheEntries()
{
    for (const DrawCommand &draw : g_draws)
    {
        if (draw.indexed_mesh && draw.cached_mesh != nullptr &&
            !draw.cached_mesh->ready)
        {
            return true;
        }
    }
    return false;
}

MeshUploadResult UploadMeshCacheOnly(SDL_GPUCommandBuffer *cmd)
{
    MeshUploadResult result;
    std::vector<MeshCacheEntryPtr> pending;
    CollectPendingMeshCacheEntries(&pending);
    UploadPendingMeshCacheEntries(cmd, pending, result);
    return result;
}

MeshUploadResult UploadMeshData(SDL_GPUCommandBuffer *cmd)
{
    MeshUploadResult result;
    result.colours_ready = UploadMeshColours(cmd, result);
    result.animated_vertices_ready =
        UploadAnimatedMeshVertices(cmd, result);

    std::vector<MeshCacheEntryPtr> pending;
    CollectPendingMeshCacheEntries(&pending);
    UploadPendingMeshCacheEntries(cmd, pending, result);
    return result;
}

void ConvertMeshDrawsToStreamed(
    const std::vector<MeshCacheEntryPtr> &entries)
{
    std::unordered_set<MeshCacheEntry *> fallback_entries;
    fallback_entries.reserve(entries.size());
    for (const MeshCacheEntryPtr &entry : entries)
    {
        if (entry != nullptr)
        {
            fallback_entries.insert(entry.get());
        }
    }

    for (DrawCommand &draw : g_draws)
    {
        if (!draw.indexed_mesh || draw.cached_mesh == nullptr ||
            fallback_entries.find(draw.cached_mesh.get()) ==
                fallback_entries.end())
        {
            continue;
        }

        const MeshCacheEntryPtr entry = draw.cached_mesh;
        const bool was_animated = draw.animated_mesh;
        const size_t fallback_vertex_count = entry->pending_indices.size();
        const bool pending_vertices_valid = draw.animated_mesh
            ? entry->pending_animated_vertices.size() == entry->vertex_count
            : entry->pending_vertices.size() == entry->vertex_count;
        if (!pending_vertices_valid ||
            fallback_vertex_count == 0 ||
            fallback_vertex_count > std::numeric_limits<Uint32>::max() ||
            g_vertices.size() > std::numeric_limits<Uint32>::max() -
                fallback_vertex_count)
        {
            continue;
        }

        const size_t colour_start = draw.first_mesh_colour;
        const size_t animated_start = draw.first_animated_mesh_vertex;
        if ((!draw.animated_mesh &&
             (colour_start > g_mesh_colours.size() ||
              entry->vertex_count > g_mesh_colours.size() - colour_start)) ||
            (draw.animated_mesh &&
             (animated_start > g_animated_mesh_vertices_stream.size() ||
              entry->vertex_count >
                  g_animated_mesh_vertices_stream.size() - animated_start)))
        {
            continue;
        }

        bool valid = true;
        for (Uint32 index : entry->pending_indices)
        {
            if (index >= entry->vertex_count)
            {
                valid = false;
                break;
            }
        }
        if (!valid)
        {
            continue;
        }

        const Uint32 first_vertex = (Uint32)g_vertices.size();
        ReserveGeometric(g_vertices,
                         g_vertices.size() + fallback_vertex_count, 4096);
        for (Uint32 index : entry->pending_indices)
        {
            SpriteVertex vertex{};
            if (draw.animated_mesh)
            {
                const MeshAnimatedStaticVertex &cached =
                    entry->pending_animated_vertices[index];
                std::memcpy(vertex.uv, cached.uv, sizeof(vertex.uv));
                const AnimatedMeshVertex &animated =
                    g_animated_mesh_vertices_stream[
                        animated_start + (size_t)index];
                EvaluateAnimatedVertexLighting(vertex.colour, draw, animated);
                std::memcpy(vertex.position, animated.position,
                            sizeof(vertex.position));
            }
            else
            {
                const MeshStaticVertex &cached =
                    entry->pending_vertices[index];
                std::memcpy(vertex.uv, cached.uv, sizeof(vertex.uv));
                const MeshColour &colour =
                    g_mesh_colours[colour_start + (size_t)index];
                std::memcpy(vertex.colour, colour.rgba,
                            sizeof(vertex.colour));
                std::memcpy(vertex.position, cached.position,
                            sizeof(vertex.position));
                if (draw.fragment_lighting)
                {
                    EncodeMeshNormal(vertex.uv, cached.normal);
                }
            }
            g_vertices.push_back(vertex);
        }

        draw.first_vertex = first_vertex;
        draw.vertex_count = (Uint32)fallback_vertex_count;
        draw.indexed_mesh = false;
        draw.animated_mesh = false;
        draw.cached_mesh.reset();
        g_mesh_direct_stream_vertices += fallback_vertex_count;

        const uint64_t expanded_vertices =
            (uint64_t)entry->source_triangle_count * 3u;
        const uint64_t avoided = expanded_vertices > entry->vertex_count
            ? expanded_vertices - entry->vertex_count : 0;
        g_mesh_expanded_vertices_avoided =
            avoided > g_mesh_expanded_vertices_avoided
                ? 0 : g_mesh_expanded_vertices_avoided - avoided;
        if (was_animated)
        {
            g_animated_mesh_vertices =
                entry->vertex_count > g_animated_mesh_vertices
                    ? 0 : g_animated_mesh_vertices - entry->vertex_count;
            g_animated_mesh_expanded_vertices_avoided =
                avoided > g_animated_mesh_expanded_vertices_avoided
                    ? 0
                    : g_animated_mesh_expanded_vertices_avoided - avoided;
        }
    }
}

void CommitMeshUploads(const std::vector<MeshCacheEntryPtr> &entries,
                       size_t colour_bytes, size_t animated_bytes)
{
    g_mesh_colour_upload_bytes += colour_bytes;
    g_animated_mesh_upload_bytes += animated_bytes;
    for (const MeshCacheEntryPtr &entry : entries)
    {
        if (entry == nullptr || entry->ready)
        {
            continue;
        }
        entry->ready = true;
        entry->upload_recorded_frame = 0;
        g_mesh_cache_upload_bytes += entry->cached_bytes;
        /* Keep the compact CPU topology for both layouts.  It is included in
         * resident_bytes and lets a transient colour/pose-stream upload
         * failure render this frame through the ordinary streamed pipeline. */
    }
}

SDL_GPUViewport BuildOutputViewport(Uint32 swapchain_width,
                                    Uint32 swapchain_height)
{
    SDL_GPUViewport viewport{};
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.w = (float)swapchain_width;
    viewport.h = (float)swapchain_height;
    viewport.min_depth = 0.0f;
    viewport.max_depth = 1.0f;
    return viewport;
}

void ApplyOriginalAspectToVertices()
{
    /*
     * Screen-space coordinates target the PS2's 640x448 framebuffer, so a
     * quad written at 0..640 must keep those proportions however the window is
     * shaped.  Shrink the excess axis around the centre to put it back inside
     * the original frame.
     *
     * Only draws that opt in are touched.  3D geometry does not: its
     * projection already widened by g_view_extend_*, so scaling it here would
     * cancel that out and put the wider view back inside a 4:3 box.  The sky
     * backdrop does opt in, and reaches the window edge because MapSky.c
     * extends its geometry past 640 to exactly the bounds this scale maps to.
     */
    float horizontal_scale = 1.0f / g_view_extend_x;
    float vertical_scale = 1.0f / g_view_extend_y;

    if (horizontal_scale == 1.0f && vertical_scale == 1.0f)
    {
        return;
    }

    for (const DrawCommand &draw : g_draws)
    {
        if (draw.indexed_mesh || !draw.preserve_original_aspect)
        {
            continue;
        }

        size_t first = (size_t)draw.first_vertex;
        size_t end = first + (size_t)draw.vertex_count;
        for (size_t vertex = first; vertex < end; vertex++)
        {
            g_vertices[vertex].position[0] *= horizontal_scale;
            g_vertices[vertex].position[1] *= vertical_scale;
        }
    }
}

/* ------------------------------------------------------------------------
 *  Geometry smoothing
 *
 *  Everything here is per logical frame except ApplyGeometryVertexBlend(),
 *  which runs once per present.  See the state block at the top of the file
 *  for what the two halves -- model matrices and streamed vertices -- are for.
 * ---------------------------------------------------------------------- */

/* `extra` is the clamped count the present loop will actually use, not the
 * setting: a snapshot costs a copy of every vertex position in the frame, and
 * there is no point paying for one when the field budget leaves no room for an
 * in-between to spend it on. */
bool GeometrySmoothingActive(int extra)
{
    return extra > 0 && g_interpolate_presents && g_interpolate_geometry;
}

unsigned int GeometryDrawFlags(const DrawCommand &draw)
{
    unsigned int flags = 0;
    flags |= draw.transform_mesh ? 0x01u : 0u;
    flags |= draw.indexed_mesh ? 0x02u : 0u;
    flags |= draw.animated_mesh ? 0x04u : 0u;
    flags |= draw.shadow_caster ? 0x08u : 0u;
    flags |= draw.depth_test ? 0x10u : 0u;
    flags |= draw.fragment_lighting ? 0x20u : 0u;
    flags |= ((unsigned int)draw.source & 3u) << 6;
    flags |= ((unsigned int)draw.blend_mode & 7u) << 8;
    flags |= draw.resident_mesh ? 0x800u : 0u;
    return flags;
}

/* A draw the blend may touch: a 3D mesh aimed at the main camera, which is the
 * same test the camera reprojection uses, minus the indexed path -- an indexed
 * draw's vertices live in the mesh-cache buffers rather than g_vertices, and
 * ConvertMeshDrawsToStreamed() can move them mid-upload. */
bool GeometryDrawIsEligible(const DrawCommand &draw)
{
    return draw.transform_mesh && !draw.shadow_caster;
}

void InvalidateGeometrySnapshot()
{
    g_geo_match_count = 0;
    g_geo_snapshot_valid = false;
    g_geo_vertices_valid = false;
    g_geometry_coverage = -1.0f;
}

/*
 * Record this frame's draw identities and vertex positions, and find how much
 * of the list still corresponds to the previous frame's.
 *
 * Called once, before the first present of the frame.  It has to be before,
 * because the model blend feeds the very first reprojection -- and it can be,
 * because nothing between here and the upload stage touches g_draws except
 * ConvertMeshDrawsToStreamed(), which announces itself.
 */
void CaptureGeometrySnapshot(int extra)
{
    InvalidateGeometrySnapshot();
    g_geo_curr_draws.clear();
    g_geo_curr_positions.clear();

    if (!GeometrySmoothingActive(extra) || g_draws.empty())
    {
        return;
    }

    try
    {
        g_geo_curr_draws.resize(g_draws.size());
        g_geo_curr_positions.resize(g_vertices.size() * 4u);
    }
    catch (const std::bad_alloc &)
    {
        /* Smoothing is a luxury; a frame without it is the frame the game has
         * always had.  Drop the snapshot rather than the frame. */
        g_geo_curr_draws.clear();
        g_geo_curr_positions.clear();
        return;
    }

    for (size_t i = 0; i < g_draws.size(); i++)
    {
        const DrawCommand &draw = g_draws[i];
        GeometrySnapshotDraw &snapshot = g_geo_curr_draws[i];
        snapshot.key.first_vertex =
            draw.resident_mesh ? draw.first_index : draw.first_vertex;
        snapshot.key.vertex_count =
            draw.resident_mesh ? draw.index_count : draw.vertex_count;
        snapshot.key.texture = draw.texture;
        snapshot.key.mesh = draw.resident_mesh ? draw.resident.get() : nullptr;
        snapshot.key.flags = GeometryDrawFlags(draw);
        std::memcpy(snapshot.model, draw.model, sizeof(snapshot.model));
    }
    for (size_t vertex = 0; vertex < g_vertices.size(); vertex++)
    {
        std::memcpy(&g_geo_curr_positions[vertex * 4u],
                    g_vertices[vertex].position, sizeof(float) * 4u);
    }
    g_geo_snapshot_valid = true;

    /* Prefix match, and it stops at the first mismatch rather than trying to
     * resynchronise.  A key that differs means the lists have diverged, and
     * pairing past that point is how one object's geometry would get blended
     * toward another's. */
    const size_t limit = g_geo_prev_draws.size() < g_geo_curr_draws.size()
        ? g_geo_prev_draws.size() : g_geo_curr_draws.size();
    size_t match = 0;
    while (match < limit &&
           g_geo_prev_draws[match].key == g_geo_curr_draws[match].key)
    {
        match++;
    }
    g_geo_match_count = match;
    /* Only that there IS a previous position buffer.  It does not have to be
     * the same size as this frame's -- a matched draw's first_vertex and
     * vertex_count are equal by construction, so its span is in range of both
     * whatever an effect did to the tail of the list, and the per-draw bounds
     * check in ApplyGeometryVertexBlend() confirms it. */
    g_geo_vertices_valid = match > 0 && !g_geo_prev_positions.empty();

    size_t eligible = 0;
    size_t blended = 0;
    for (size_t i = 0; i < g_draws.size(); i++)
    {
        if (!GeometryDrawIsEligible(g_draws[i]))
        {
            continue;
        }
        eligible++;
        blended += i < match ? 1u : 0u;
    }
    g_geometry_coverage = eligible != 0
        ? (float)((double)blended / (double)eligible) : -1.0f;
}

/*
 * Swap this frame's snapshot into place as the next frame's previous.
 *
 * A frame whose snapshot never completed clears the pair instead, so the next
 * frame compares against nothing and blends nothing rather than pairing tick N
 * with tick N-2.
 */
void FinishGeometrySnapshot()
{
    if (!g_geo_snapshot_valid)
    {
        g_geo_prev_draws.clear();
        g_geo_prev_positions.clear();
        g_geo_curr_draws.clear();
        g_geo_curr_positions.clear();
        return;
    }

    g_geo_prev_draws.swap(g_geo_curr_draws);
    g_geo_prev_positions.swap(g_geo_curr_positions);
    g_geo_curr_draws.clear();
    g_geo_curr_positions.clear();
    g_geo_snapshot_valid = false;
}

/*
 * Move the streamed vertex positions of every corresponding draw to `t`.
 *
 * Written into g_vertices in place, so this has to run before the vertex
 * upload of every present -- including the one that lands on the simulation
 * tick, which restores the frame's own positions out of the snapshot rather
 * than blending to 1.  `a + (b - a) * 1.0f` is not exactly `b` in float, and a
 * frame on the tick has to be identical to the unsmoothed one or the seam
 * shows every tick; it is the same reason BlendViewMatrix short-circuits.
 *
 * Only positions move.  UVs and colours are the frame's own throughout: the
 * vertex colour of a skinned character is its VU1 lighting, which is evaluated
 * against the pose the game simulated, and interpolating that as well would
 * buy nothing visible for another pass over the buffer.
 */
void ApplyGeometryVertexBlend(float t)
{
    /* Negative is the caller saying it wants the frame exactly as it was
     * built, which is also what it asks for when there is no previous camera
     * to interpolate against.  It is not a t: running the lerp with one would
     * extrapolate backwards past the previous frame. */
    if (t < 0.0f || !g_geo_vertices_valid)
    {
        return;
    }

    const size_t curr_limit = g_geo_curr_positions.size() / 4u;
    const size_t prev_limit = g_geo_prev_positions.size() / 4u;
    for (size_t i = 0; i < g_geo_match_count && i < g_draws.size(); i++)
    {
        const DrawCommand &draw = g_draws[i];
        /* Only streamed vertices can move here.  A resident draw's geometry is
         * object space in a GPU buffer; its motion is all in `model`, which
         * ReprojectDrawsAt() blends. */
        if (!GeometryDrawIsEligible(draw) || draw.indexed_mesh ||
            draw.resident_mesh)
        {
            continue;
        }
        const size_t first = (size_t)draw.first_vertex;
        const size_t count = (size_t)draw.vertex_count;
        if (count == 0 || count > curr_limit || first > curr_limit - count ||
            count > prev_limit || first > prev_limit - count ||
            count > g_vertices.size() || first > g_vertices.size() - count)
        {
            continue;
        }

        if (t >= 1.0f)
        {
            for (size_t vertex = first; vertex < first + count; vertex++)
            {
                std::memcpy(g_vertices[vertex].position,
                            &g_geo_curr_positions[vertex * 4u],
                            sizeof(float) * 4u);
            }
            continue;
        }

        for (size_t vertex = first; vertex < first + count; vertex++)
        {
            const float *from = &g_geo_prev_positions[vertex * 4u];
            const float *to = &g_geo_curr_positions[vertex * 4u];
            float *position = g_vertices[vertex].position;
            position[0] = from[0] + (to[0] - from[0]) * t;
            position[1] = from[1] + (to[1] - from[1]) * t;
            position[2] = from[2] + (to[2] - from[2]) * t;
            /* w is 1 on every mesh vertex the SGD walker emits, and a blended
             * one would divide the other three.  Take the frame's. */
            position[3] = to[3];
        }
    }
}

/*
 * Re-aim every eligible draw at `view`/`projection`, optionally with its own
 * transform blended toward this frame's.
 *
 * `model_t` under zero means "use the frame's own model", which is the plain
 * camera reprojection MioPan_RendererReprojectDraws() exposes.  In [0, 1) it
 * additionally moves the rigidly bound blocks -- see BlendModelMatrix() for
 * why a transform cannot be lerped element-wise, and the geometry-smoothing
 * state block for why a skinned block is not among them.
 */
int ReprojectDrawsAt(const float *view, const float *projection, float model_t)
{
    if (view == nullptr || projection == nullptr || !g_frame_active ||
        !MatrixIsFinite(view) || !MatrixIsFinite(projection))
    {
        return 0;
    }

    /* Installs g_3d_view / g_3d_projection / g_3d_view_projection *and* the
     * matching uniform-block copies, which the record pass reads for the
     * shaders that want the camera in pieces rather than as one mvp. */
    MioPan_RendererSet3DViewProjection(view, projection);

    const bool blend_models =
        model_t >= 0.0f && model_t < 1.0f && g_geo_match_count != 0;

    int reprojected = 0;
    for (size_t i = 0; i < g_draws.size(); i++)
    {
        DrawCommand &draw = g_draws[i];
        /* transform_mesh is set by the mesh-cache queue path and by
         * MioPan_RendererBeginMeshStream(), and all of them store `model`
         * beside the mvp; every QueueTriangleList caller passes a null mvp and
         * is screen-space.  So this flag is exactly "has a model matrix built
         * against the main camera". */
        if (!draw.transform_mesh)
        {
            continue;
        }

        /* A caster's mvp was built against its light, not the eye, and its
         * pixels belong to the shadow map -- which a reprojected frame reuses
         * rather than re-rendering.  Leave it exactly as it was. */
        if (draw.shadow_caster)
        {
            continue;
        }

        if (blend_models && i < g_geo_match_count)
        {
            float model[16];
            if (BlendModelMatrix(g_geo_prev_draws[i].model, draw.model,
                                 model_t, model))
            {
                /* draw.model is left alone deliberately.  The present that
                 * lands on the tick has to rebuild its mvp from the frame's
                 * own transform, and the snapshot taken from it becomes the
                 * next frame's previous. */
                MulMatrixRowMajor(draw.mvp, model, g_3d_view_projection);
                reprojected++;
                continue;
            }
        }

        MulMatrixRowMajor(draw.mvp, draw.model, g_3d_view_projection);
        reprojected++;
    }

    return reprojected;
}

/*
 * The half of the startup banner only the renderer knows.
 *
 * miopan_log.cpp writes the build stamp and the machine as the log opens, long
 * before there is a device; this adds what a graphics bug report is actually
 * read for -- which backend and adapter SDL chose, what the swapchain ended up
 * as, and the two resolutions, which are not the same number and are the usual
 * source of "it looks wrong on my monitor".  Through SDL_Log, so it goes
 * wherever the rest of the port layer goes.
 */
void LogRendererBanner()
{
    const SDL_PropertiesID props = SDL_GetGPUDeviceProperties(g_device);
    const char *adapter =
        SDL_GetStringProperty(props, SDL_PROP_GPU_DEVICE_NAME_STRING, "unknown");
    const char *driver_version = SDL_GetStringProperty(
        props, SDL_PROP_GPU_DEVICE_DRIVER_VERSION_STRING, "");

    int window_w = 0;
    int window_h = 0;
    int pixel_w  = 0;
    int pixel_h  = 0;
    SDL_GetWindowSize(g_window, &window_w, &window_h);
    SDL_GetWindowSizeInPixels(g_window, &pixel_w, &pixel_h);

    SDL_Log("MioPan gpu: %s on %s%s%s",
            SDL_GetGPUDeviceDriver(g_device), adapter,
            driver_version[0] != '\0' ? ", driver " : "", driver_version);
    SDL_Log("MioPan gpu: swapchain %s %s, present %s, scene %s",
            ColorFormatName(g_swapchain_format),
            HdrCompositionName(g_hdr_composition),
            PresentModeName(g_present_mode), ColorFormatName(g_scene_format));
    /* The render resolution is not settled here -- it comes out of the
     * swapchain size once a frame is actually recorded -- so it is reported
     * from there instead, on the first frame and after every change. */
    SDL_Log("MioPan gpu: window %dx%d (%dx%d px, %s), depth %s", window_w,
            window_h, pixel_w, pixel_h,
            g_window_mode == MIOPAN_WINDOW_MODE_BORDERLESS ? "borderless"
                                                           : "windowed",
            DepthFormatName(g_depth_format));
    SDL_Log("MioPan gpu: hdr mode %d, active %d, 10-bit scene %d",
            g_hdr_mode, g_hdr_composition != SDL_GPU_SWAPCHAINCOMPOSITION_SDR,
            (int)g_hdr_full_precision);
}

bool EnsureRenderer()
{
    /* Settings first, environment second: the file supplies the baseline and
     * the MIOPAN_* variables override it, so a test run never has to disturb
     * -- or silently persist into -- what the player saved. */
    if (!g_config_loaded)
    {
        g_config_loaded = true;
        MioPan_ConfigLoad();
        MioPan_ConfigApply();
        g_windowed_width = miopan_config.renderer.window_width;
        g_windowed_height = miopan_config.renderer.window_height;
    }
    InitLightingModeFromEnvironment();
    InitRenderResolutionFromEnvironment();
    if (g_renderer_initialized)
    {
        return true;
    }
    /* A partial initialization is not a usable renderer.  In particular, a
     * failed cached-layout pipeline must never be treated as a valid null
     * pipeline merely because device creation succeeded. */
    if (g_device != nullptr)
    {
        return false;
    }

    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS | SDL_INIT_GAMEPAD))
    {
        LogSdlError("SDL_Init");
        return false;
    }

    g_window = SDL_CreateWindow(
        "MioPan", g_windowed_width, g_windowed_height,
        SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY);
    if (g_window == nullptr)
    {
        LogSdlError("SDL_CreateWindow");
        return false;
    }

    /* Before the device claims the window, so the swapchain is created at the
     * final size rather than at the windowed one and immediately resized. */
    ApplyWindowMode();

    SDL_GPUShaderFormat formats = SDL_GPU_SHADERFORMAT_SPIRV |
                                  SDL_GPU_SHADERFORMAT_DXIL |
                                  SDL_GPU_SHADERFORMAT_MSL;
    g_device = SDL_CreateGPUDevice(formats, false, nullptr);
    if (g_device == nullptr)
    {
        LogSdlError("SDL_CreateGPUDevice");
        return false;
    }

    if (!SDL_ClaimWindowForGPUDevice(g_device, g_window))
    {
        LogSdlError("SDL_ClaimWindowForGPUDevice");
        return false;
    }

    SDL_GPUPresentMode present_mode = SDL_GPU_PRESENTMODE_IMMEDIATE;
    if (!SDL_WindowSupportsGPUPresentMode(g_device, g_window, present_mode))
    {
        present_mode = SDL_GPU_PRESENTMODE_MAILBOX;
        if (!SDL_WindowSupportsGPUPresentMode(g_device, g_window,
                                              present_mode))
        {
            present_mode = SDL_GPU_PRESENTMODE_VSYNC;
        }
    }
    g_present_mode = present_mode;

    if (!SDL_SetGPUSwapchainParameters(g_device, g_window,
                                       SDL_GPU_SWAPCHAINCOMPOSITION_SDR,
                                       present_mode))
    {
        LogSdlError("SDL_SetGPUSwapchainParameters");
        return false;
    }
    g_hdr_composition = SDL_GPU_SWAPCHAINCOMPOSITION_SDR;

    /*
     * Read the SDR swapchain's format BEFORE any HDR composition is asked for.
     *
     * That format is what the game is rasterised in and what ImGui builds its
     * pipeline against, and neither can follow the swapchain when the
     * composition changes later -- so it has to be sampled here, while the
     * swapchain is still the SDR one, rather than after.
     */
    g_swapchain_format = SDL_GetGPUSwapchainTextureFormat(g_device, g_window);
    g_scene_format = g_swapchain_format;

    /*
     * With HDR asked for at startup, rasterise into 10 bits instead of 8.
     *
     * Still a UNORM target, which is the point: every GS blend equation the
     * port reproduces saturates at white, and a float target would let the
     * additive ones run past it and change the picture.  What the extra two
     * bits buy is headroom against banding -- this game layers a great many
     * full-screen blends over near-black rooms, and the present pass then
     * stretches that result across a display's whole range, which is exactly
     * the recipe for contour rings out of an 8-bit buffer.
     *
     * Decided once, here, because every pipeline is built against it.  A
     * session that turns HDR on later still gets correct HDR output, just from
     * an 8-bit scene; MioPan_RendererHdrIsFullPrecision() is how the UI tells
     * the two apart.
     */
    RefreshDisplayHdrInfo();
    if (g_hdr_mode != MIOPAN_HDR_OFF &&
        SDL_GPUTextureSupportsFormat(g_device,
                                     SDL_GPU_TEXTUREFORMAT_R10G10B10A2_UNORM,
                                     SDL_GPU_TEXTURETYPE_2D,
                                     SDL_GPU_TEXTUREUSAGE_SAMPLER |
                                         SDL_GPU_TEXTUREUSAGE_COLOR_TARGET))
    {
        g_scene_format = SDL_GPU_TEXTUREFORMAT_R10G10B10A2_UNORM;
        g_hdr_full_precision = true;
        SDL_Log("MioPan SDL_GPU: 10-bit scene target for HDR");
    }
    g_output_format = g_scene_format;

    /* Now the composition the mode actually asks for.  Left SDR when the mode
     * is OFF, or when AUTO finds an SDR display. */
    ApplyHdrComposition();

    g_depth_format = PickSupportedDepthFormat();
    SDL_Log("MioPan SDL_GPU: using %s depth buffer",
            DepthFormatName(g_depth_format));
    if (g_depth_format != SDL_GPU_TEXTUREFORMAT_D32_FLOAT)
    {
        /* Reversed-Z buys its precision from the float exponent: a normalised
         * integer buffer distributes codes uniformly, so mirroring it gains
         * nothing and the 0.1 near plane against a 65535 far plane will fight
         * on anything distant.  Not fatal, but worth knowing before chasing it. */
        SDL_Log("MioPan SDL_GPU: warning - reversed-Z depth needs D32_FLOAT to "
                "help; expect z-fighting at distance on %s",
                DepthFormatName(g_depth_format));
    }
    InitDefaultUniforms();

    /* Before CreatePipelines(), which warms the set for whatever sample count
     * this settles on.  Both formats are known by now, which is what the
     * support query needs. */
    RefreshMsaaSupport();
    SDL_Log("MioPan SDL_GPU: MSAA up to %dx supported", g_msaa_max_supported);
    ApplyMsaaSetting();
    g_pipeline_sample_count = g_msaa_active;

    if (!CreateCommonResources() || !CreatePipelines() ||
        !CreateShadowResources())
    {
        return false;
    }
    /* After the plain set exists, so a level that will not build can fall back
     * to it. */
    ApplyAnisotropySetting();
    /* These helpers use deferred release when they later grow.  Seeding their
     * normal high-water range here moves the first SDL_CreateGPUBuffer calls
     * out of room visibility and into one-time renderer initialization. */
    (void)EnsureVertexBuffer(kInitialVertexBufferSize);
    (void)EnsureMeshColourBuffer(kInitialMeshColourBufferSize);
    (void)EnsureAnimatedMeshBuffer(kInitialAnimatedMeshBufferSize);
    g_mesh_cache_available = CreateMeshArenas();
    if (!g_mesh_cache_available)
    {
        /* The indexed cache is optional.  A machine unable to reserve its
         * startup arenas must still render correctly through the established
         * streamed paths instead of becoming stuck in partial initialization. */
        SDL_Log("MioPan SDL_GPU: mesh cache arenas unavailable; using streamed meshes");
    }
    /* g_output_format, not the swapchain's.  ImGui bakes this into its
     * pipeline at Init and has no way to be handed a new one, so it draws into
     * the composited output target -- which keeps that format for the life of
     * the renderer however the swapchain composition later moves. */
    if (!MioPanUi::Init(g_window, g_device, g_output_format))
    {
        return false;
    }
    /* atexit is LIFO: shut ImGui down first, then release cache resources. */
    std::atexit(ShutdownMeshCache);
    std::atexit(MioPanUi::Shutdown);

    g_vertices.reserve(kInitialVertexBufferSize / sizeof(SpriteVertex));
    g_mesh_colours.reserve(kInitialMeshColourBufferSize /
                           sizeof(MeshColour));
    g_animated_mesh_vertices_stream.reserve(
        kInitialAnimatedMeshBufferSize / sizeof(AnimatedMeshVertex));
    g_draws.reserve(1024);
    g_mesh_cache.reserve(kMeshCacheEntryLimit);
    g_mesh_cache_retry_after.reserve(128);
    g_mesh_cache_build_reservations.reserve(
        kMeshCacheBuildBudgetEntries * 2u);
    g_mesh_cache_owner_generations.reserve(kMeshCacheEntryLimit);
    g_inflight_mesh_submissions.reserve(8);
    g_pending_texture_uploads.reserve(64);
    g_renderer_initialized = true;
    LogRendererBanner();
    return true;
}

TextureEntry *GetTexture(const sceGsTex0 *tex0)
{
    uint64_t tex0_value;
    uint64_t hash;

    if (tex0 == nullptr || !EnsureRenderer())
    {
        return nullptr;
    }

    ConsumeTextureInvalidations();

    /* A movie's picture never reaches emulated GS memory -- the decoder hands
     * it straight to GetVideoTexture().  Taken ahead of both caches because
     * neither can hold it: the L1 entry would be stale the moment the next
     * picture lands, and the L2 key is a hash of GS content that is no longer
     * written.
     *
     * No fall-through on failure.  The GS block a movie names holds whatever
     * the last thing to use that page left there, so decoding it would draw
     * stale garbage -- and put one more never-evicted texture in the cache for
     * every frame of the film.  A null answer draws nothing, which is what the
     * callers already do with one. */
    if (IsVideoTex0(tex0))
    {
        return GetVideoTexture(tex0);
    }

    tex0_value = ReadTex0Value(tex0);
    g_texture_l1_lookups++;

    auto tex0_cached = g_tex0_cache.find(tex0_value);
    if (tex0_cached != g_tex0_cache.end() &&
        tex0_cached->second.texture != nullptr &&
        tex0_cached->second.texture->texture != nullptr)
    {
        g_texture_l1_hits++;
        return tex0_cached->second.texture;
    }

    /* Hashing GS memory, decoding a miss and creating its host texture happen
     * synchronously here.  Pixel copies are queued and packed into the frame's
     * single pre-render copy phase below, so this path no longer submits one
     * GPU command buffer per cold texture. */
    MioPanProfileScope texture_miss_scope(MIOPAN_PROFILE_TEXTURE_MISS);

    hash = MioPan_GetTextureHash((sceGsTex0 *)tex0);
    if (hash == 0)
    {
        return nullptr;
    }

    auto cached = g_texture_cache.find(hash);
    if (cached != g_texture_cache.end())
    {
        g_texture_l2_hits++;
        RegisterTex0CacheEntry(tex0_value, tex0, &cached->second);
        return &cached->second;
    }

    /* The content hash above already covers the source texels and CLUT.  Asking
     * the decoder for it again doubled the hashing cost of every cold miss. */
    if (MioPan_GsDownloadTexture((sceGsTex0 *)tex0, nullptr) == nullptr)
    {
        return nullptr;
    }
    g_texture_downloads++;

    std::vector<unsigned char> rgba = MioPan::GS::TakeDownloadedTexture();
    TextureEntry *entry = CreatePendingTexture(
        hash, 1 << tex0->TW, 1 << tex0->TH, std::move(rgba));
    if (entry == nullptr)
    {
        return nullptr;
    }

    g_texture_creates++;
    RegisterTex0CacheEntry(tex0_value, tex0, entry);
    return entry;
}

/*
 * The texture a mesh draw samples.
 *
 * `texture`, when the caller has one, is a handle from
 * MioPan_RendererResolveTexture() -- a model whose textures were resolved once
 * and whose GS upload is now skipped.  It has to be used as it is: that model's
 * texels are no longer in GS memory, so resolving its TEX0 again would decode
 * whatever the last upload left at that address.  Otherwise the TEX0 goes
 * through the caches as it always has.  Either way F3's flat colour wins.
 */
void ApplyMeshTexture(DrawCommand &command, const sceGsTex0 *tex0,
                      const void *texture)
{
    const TextureEntry *entry = texture != nullptr
        ? static_cast<const TextureEntry *>(texture)
        : (tex0 != nullptr ? GetTexture(tex0) : nullptr);
    if (entry != nullptr && entry->texture != nullptr && !g_dbg_flatcolor)
    {
        command.texture = entry->texture;
        command.texture_width = entry->width;
        command.texture_height = entry->height;
    }
    else
    {
        command.texture = g_white_texture;
        command.texture_width = 1;
        command.texture_height = 1;
    }
}

/* `ndc_z` is the quad's depth in host NDC, and defaults to the depth-less
 * 0.0f every screen-space caller wants.  Only a screen-space quad that stands
 * for a 3D primitive passes one -- an effect particle billboard, which the GS
 * positioned in screen space but still depth-tested. */
void BuildQuadVertices(SpriteVertex *vertices, const float *xy, const float *uv,
                       float tex_w, float tex_h, float r, float g, float b,
                       float a, float ndc_z = 0.0f)
{
    for (int i = 0; i < 4; i++)
    {
        vertices[i].uv[0] = uv != nullptr ? uv[i * 2 + 0] / tex_w : 0.0f;
        vertices[i].uv[1] = uv != nullptr ? uv[i * 2 + 1] / tex_h : 0.0f;
        vertices[i].uv[2] = 0.0f;
        vertices[i].uv[3] = 0.0f;

        vertices[i].colour[0] = r;
        vertices[i].colour[1] = g;
        vertices[i].colour[2] = b;
        vertices[i].colour[3] = a;

        vertices[i].position[0] = ScreenXToClip(xy[i * 2 + 0]);
        vertices[i].position[1] = ScreenYToClip(xy[i * 2 + 1]);
        vertices[i].position[2] = ndc_z;
        vertices[i].position[3] = 1.0f;
    }
}

/*
 * The GS SCISSOR box, converted to swapchain pixels for one draw.
 *
 * Screen-space primitives were shrunk about the centre by AdjustAspect() to
 * keep the 640x448 frame's proportions, so their scissor box has to move with
 * them; 3D geometry was not touched there, and its 0..640 range already spans
 * the whole output, so the unscaled mapping is the correct one for it.
 *
 * Returns false when the box clips nothing.  That deliberately includes the
 * degenerate box ClearDrawEnv() writes -- it zeroes SCISSOR every frame, and
 * main.c installs the real one immediately afterwards, but a zero box taken
 * literally is 1x1 and would blank the screen for any draw that slipped
 * between the two.
 */
bool BuildScissorRect(const DrawCommand &draw, int target_width,
                      int target_height, SDL_Rect *out)
{
    if (draw.scissor.x1 <= draw.scissor.x0 || draw.scissor.y1 <= draw.scissor.y0)
    {
        return false;
    }
    if (draw.scissor.x0 <= 0 && draw.scissor.y0 <= 0 &&
        draw.scissor.x1 >= kLogicalWidth - 1 &&
        draw.scissor.y1 >= kLogicalHeight - 1)
    {
        return false;
    }

    float scale_x = draw.preserve_original_aspect ? 1.0f / g_view_extend_x : 1.0f;
    float scale_y = draw.preserve_original_aspect ? 1.0f / g_view_extend_y : 1.0f;

    /* SCAX1 / SCAY1 are inclusive on the GS, so the box ends one past them. */
    float left = (ScreenXToClip((float)draw.scissor.x0) * scale_x * 0.5f + 0.5f) *
                 (float)target_width;
    float right = (ScreenXToClip((float)(draw.scissor.x1 + 1)) * scale_x * 0.5f + 0.5f) *
                  (float)target_width;
    float top = (0.5f - ScreenYToClip((float)draw.scissor.y0) * scale_y * 0.5f) *
                (float)target_height;
    float bottom = (0.5f - ScreenYToClip((float)(draw.scissor.y1 + 1)) * scale_y * 0.5f) *
                   (float)target_height;

    int x0 = std::max(0, (int)std::floor(left));
    int y0 = std::max(0, (int)std::floor(top));
    int x1 = std::min(target_width, (int)std::ceil(right));
    int y1 = std::min(target_height, (int)std::ceil(bottom));

    if (x1 <= x0 || y1 <= y0)
    {
        return false;
    }

    out->x = x0;
    out->y = y0;
    out->w = x1 - x0;
    out->h = y1 - y0;
    return true;
}

/*
 * Snapshot the GS draw environment into a draw command.  The GS held blend,
 * depth-write and scissor as global state, so a primitive drew with whatever
 * was written last; recording the shadow at queue time reproduces that against
 * a renderer that submits the whole frame at the end.  ClearDrawEnv() rewrites
 * all three at the top of every frame, so a stale value cannot outlive one.
 */
void ApplyGsDrawEnv(DrawCommand &command)
{
    command.blend_mode = g_gs_blend_mode;
    command.depth_write = g_gs_depth_write;
    command.scissor = g_gs_scissor;

    /*
     * PORT DEVIATION: a light-adding world-space billboard does not write
     * depth, whatever ZMSK says.
     *
     * This game's GS depth buffer is 16 bit -- every ZBUF the engine writes is
     * 0x..0a000118, whose PSM field 0xA is PSMZ16S, and effect_sub.c reads it
     * back at GS 0x2300 in exactly that format.  Depth therefore quantises
     * hard, and with ZTST GEQUAL two coplanar quads land on the same integer Z
     * and the later one always passes.  The host depth target is D32_FLOAT,
     * where the same two quads land an ULP apart in a direction that varies
     * per pixel, so roughly half the later quad's fragments are rejected and
     * the effect fights itself wherever its own sprites overlap.
     *
     * ItemEffectDrawOne()'s item glint is the clearest case: its two sprites
     * share one LocalWorld and differ only in size (ppos[i][2] is 0.0f for
     * every corner), so they are exactly coplanar, and its DRAW_ENV is one of
     * the few in the effect layer with ZMSK clear.
     *
     * Dropping the write and keeping the test preserves the half that is
     * visible -- the effect is still occluded by walls -- and drops the half
     * that is not: an additive or subtractive pass adds or removes light, so
     * nothing it stamps into the depth buffer is meant to occlude anything.
     *
     * Deliberately scoped to the billboard bridge.  Several 2D passes lay a
     * depth wedge on purpose with ZMSK clear -- effect_scr's stacked screen
     * copies, n_equip_tray's accumulator dial, photo_make's picture quad --
     * and every one of those goes through the sprite paths, not this one.
     */
    if (command.source == DRAW_SOURCE_BILLBOARD &&
        (command.blend_mode == GS_BLEND_ADD ||
         command.blend_mode == GS_BLEND_ADD_ONE ||
         command.blend_mode == GS_BLEND_SUB))
    {
        command.depth_write = false;
    }
}

bool DrawCommandsAreMergeCompatible(const DrawCommand &previous,
                                    const DrawCommand &next)
{
    if (previous.source != next.source ||
        previous.source == DRAW_SOURCE_GENERIC ||
        previous.indexed_mesh || next.indexed_mesh ||
        (uint64_t)previous.first_vertex + previous.vertex_count !=
            next.first_vertex ||
        previous.texture != next.texture ||
        previous.texture_width != next.texture_width ||
        previous.texture_height != next.texture_height ||
        previous.depth_test != next.depth_test ||
        previous.depth_compare != next.depth_compare ||
        previous.repeat_uv != next.repeat_uv ||
        previous.min_linear != next.min_linear ||
        previous.mag_linear != next.mag_linear ||
        previous.preserve_original_aspect !=
            next.preserve_original_aspect ||
        previous.transform_mesh != next.transform_mesh ||
        previous.clip_z_reversed != next.clip_z_reversed ||
        previous.animated_mesh != next.animated_mesh ||
        previous.fragment_lighting != next.fragment_lighting ||
        previous.blend_mode != next.blend_mode ||
        previous.depth_write != next.depth_write ||
        std::memcmp(&previous.scissor, &next.scissor,
                    sizeof(previous.scissor)) != 0 ||
        std::memcmp(previous.alpha_test, next.alpha_test,
                    sizeof(previous.alpha_test)) != 0)
    {
        return false;
    }
    if (previous.transform_mesh &&
        (std::memcmp(previous.mvp, next.mvp, sizeof(previous.mvp)) != 0 ||
         std::memcmp(previous.model, next.model,
                     sizeof(previous.model)) != 0))
    {
        return false;
    }
    if (previous.fragment_lighting &&
        previous.fragment_light_index != next.fragment_light_index)
    {
        return false;
    }
    return true;
}

void RecordDrawSourceMetrics(const DrawCommand &command)
{
    if (command.source <= DRAW_SOURCE_GENERIC ||
        command.source >= DRAW_SOURCE_COUNT)
    {
        return;
    }

    DrawSourceFrameMetrics &metrics =
        g_draw_source_metrics[(int)command.source];
    metrics.commands++;
    metrics.vertices += command.vertex_count;
    metrics.stream_bytes +=
        (uint64_t)command.vertex_count * sizeof(SpriteVertex);
    if (!g_draws.empty() &&
        DrawCommandsAreMergeCompatible(g_draws.back(), command))
    {
        metrics.compatible_joins++;
    }
}

/* TEMPORARY DIAGNOSTIC -- stutter attribution.  Publishes this frame's cold
 * work (cache builds, promotions, deferrals, texture decodes) so the profiler's
 * long-frame dump can say which cold path a slow frame took.  Every counter
 * here is a window total, so the note carries per-frame deltas. */
void PublishStutterFrameNote()
{
    static int last_hits, last_misses, last_creates, last_promotions;
    static int last_build_deferred, last_upload_deferred, last_evictions;
    static int last_invalidations, last_downloads, last_creates_tex;
    char note[512];

    /* The UI's stats window zeroes these counters, so a raw subtraction can go
     * negative on the frame after it consumes them. */
    const auto delta = [](int now, int last) {
        return now >= last ? now - last : now;
    };

    SDL_snprintf(note, sizeof(note),
                 "hit %d miss %d new %d promo %d bdefer %d udefer %d "
                 "evict %d inval %d | texdl %d texnew %d texpend %zu | "
                 "draws %zu verts %zu meshtri %d resident %zu",
                 delta(g_mesh_cache_hits, last_hits),
                 delta(g_mesh_cache_misses, last_misses),
                 delta(g_mesh_cache_creates, last_creates),
                 delta(g_mesh_cache_promotions, last_promotions),
                 delta(g_mesh_cache_build_deferred, last_build_deferred),
                 delta(g_mesh_cache_upload_deferred, last_upload_deferred),
                 delta(g_mesh_cache_evictions, last_evictions),
                 delta(g_mesh_cache_invalidations, last_invalidations),
                 delta(g_texture_downloads, last_downloads),
                 delta(g_texture_creates, last_creates_tex),
                 g_pending_texture_uploads.size(),
                 g_draws.size(), g_vertices.size(),
                 g_mesh_triangles_submitted, g_mesh_cache.size());
    MioPan_ProfilerSetFrameNote(note);

    last_hits = g_mesh_cache_hits;
    last_misses = g_mesh_cache_misses;
    last_creates = g_mesh_cache_creates;
    last_promotions = g_mesh_cache_promotions;
    last_build_deferred = g_mesh_cache_build_deferred;
    last_upload_deferred = g_mesh_cache_upload_deferred;
    last_evictions = g_mesh_cache_evictions;
    last_invalidations = g_mesh_cache_invalidations;
    last_downloads = g_texture_downloads;
    last_creates_tex = g_texture_creates;
}

void FlushDrawSourceMetricsToProfiler()
{
    const DrawSourceFrameMetrics &billboard =
        g_draw_source_metrics[DRAW_SOURCE_BILLBOARD];
    MioPan_ProfilerAddCounter(MIOPAN_PROFILER_COUNTER_BILLBOARD_COMMANDS,
                              billboard.commands);
    MioPan_ProfilerAddCounter(MIOPAN_PROFILER_COUNTER_BILLBOARD_QUEUED,
                              billboard.commands);
    MioPan_ProfilerAddCounter(MIOPAN_PROFILER_COUNTER_BILLBOARD_VERTICES,
                              billboard.vertices);
    MioPan_ProfilerAddCounter(MIOPAN_PROFILER_COUNTER_BILLBOARD_STREAM_BYTES,
                              billboard.stream_bytes);
    MioPan_ProfilerAddCounter(
        MIOPAN_PROFILER_COUNTER_BILLBOARD_COMPATIBLE_JOINS,
        billboard.compatible_joins);
    MioPan_ProfilerAddCounter(
        MIOPAN_PROFILER_COUNTER_BILLBOARD_TEXTURE_LOOKUPS,
        billboard.texture_lookups);
    MioPan_ProfilerAddCounter(
        MIOPAN_PROFILER_COUNTER_BILLBOARD_TEXTURE_MISSES,
        billboard.texture_misses);

    const DrawSourceFrameMetrics &sky =
        g_draw_source_metrics[DRAW_SOURCE_SKY];
    MioPan_ProfilerAddCounter(MIOPAN_PROFILER_COUNTER_SKY_COMMANDS,
                              sky.commands);
    MioPan_ProfilerAddCounter(MIOPAN_PROFILER_COUNTER_SKY_VERTICES,
                              sky.vertices);
    MioPan_ProfilerAddCounter(MIOPAN_PROFILER_COUNTER_SKY_STREAM_BYTES,
                              sky.stream_bytes);
    MioPan_ProfilerAddCounter(MIOPAN_PROFILER_COUNTER_SKY_COMPATIBLE_JOINS,
                              sky.compatible_joins);
}

void QueueQuad(SDL_GPUTexture *texture, int texture_width, int texture_height,
               const SpriteVertex *corners,
               bool preserve_original_aspect = true,
               bool repeat_uv = false,
               bool min_linear = true,
               bool mag_linear = true,
               DrawSource source = DRAW_SOURCE_GENERIC,
               bool depth_test = false,
               GsDepthCompare depth_compare = GS_DEPTH_GEQUAL,
               bool clip_z_reversed = false)
{
    static const int indices[6] = {0, 1, 2, 2, 1, 3};

    if (texture == nullptr || corners == nullptr || g_mesh_stream.token != 0 ||
        g_vertices.size() > (size_t)std::numeric_limits<Uint32>::max() - 6u)
    {
        return;
    }

    DrawCommand command{};
    command.source = source;
    command.texture = texture;
    command.texture_width = texture_width;
    command.texture_height = texture_height;
    command.first_vertex = (Uint32)g_vertices.size();
    command.vertex_count = 6;
    command.depth_test = depth_test;
    command.depth_compare = depth_compare;
    command.repeat_uv = repeat_uv;
    command.min_linear = min_linear;
    command.mag_linear = mag_linear;
    command.preserve_original_aspect = preserve_original_aspect;
    command.clip_z_reversed = clip_z_reversed;
    ApplyGsDrawEnv(command);

    for (int index : indices)
    {
        g_vertices.push_back(corners[index]);
    }
    RecordDrawSourceMetrics(command);
    g_draws.push_back(command);
}

/* `preserve_original_aspect` is false for anything already in 3D clip space,
 * or for an object-space mesh whose GPU projection widens to the output
 * aspect.  Screen-space callers leave it true.  Passing `mvp` selects the
 * object-space mesh shader and snapshots the camera state for this draw.
 * `clip_z_reversed` says a clip-space caller has already put its z in the
 * host's reversed convention (DrawCommand::clip_z_reversed). */
void QueueTriangleList(SDL_GPUTexture *texture, int texture_width,
                       int texture_height, const SpriteVertex *vertices,
                       int vertex_count, bool depth_test = true,
                       bool repeat_uv = true,
                       bool preserve_original_aspect = true,
                       const float *mvp = nullptr,
                       const float *alpha_test = nullptr,
                       DrawSource source = DRAW_SOURCE_GENERIC,
                       bool clip_z_reversed = false)
{
    if (texture == nullptr || vertices == nullptr || vertex_count <= 0 ||
        g_mesh_stream.token != 0 ||
        (vertex_count % 3) != 0 ||
        g_vertices.size() >
            (size_t)std::numeric_limits<Uint32>::max() - (size_t)vertex_count)
    {
        return;
    }

    DrawCommand command{};
    command.source = source;
    command.texture = texture;
    command.texture_width = texture_width;
    command.texture_height = texture_height;
    command.first_vertex = (Uint32)g_vertices.size();
    command.vertex_count = (Uint32)vertex_count;
    command.depth_test = depth_test;
    /* Addressing is the GS CLAMP register's business, not the depth test's.
     * The two shared a flag here because every caller that tested depth was a
     * tiling mesh and every caller that did not was a clamped sprite; the
     * effect bridge is neither -- a screen-space frame-buffer sample that
     * clamps (CLAMP 0x5) *and* depth-tests (ZTST GEQUAL). */
    command.repeat_uv = repeat_uv;
    /* gra3d's default TEX1 is 0x60: linear minification and magnification. */
    command.min_linear = true;
    command.mag_linear = true;
    command.preserve_original_aspect = preserve_original_aspect;
    command.transform_mesh = mvp != nullptr;
    command.clip_z_reversed = clip_z_reversed;
    ApplyGsDrawEnv(command);
    if (mvp != nullptr)
    {
        std::memcpy(command.mvp, mvp, sizeof(command.mvp));
    }
    /*
     * Only the callers that actually carry GS state opt in.  A 2D or UI draw
     * inherits the real GS's TEST register on hardware too, but nothing on the
     * host writes the register for those paths, so applying the last mesh's
     * alpha test to them would cut up the interface for no reason.
     */
    if (alpha_test != nullptr)
    {
        std::memcpy(command.alpha_test, alpha_test, sizeof(command.alpha_test));
    }

    g_vertices.insert(g_vertices.end(), vertices, vertices + vertex_count);
    RecordDrawSourceMetrics(command);
    g_draws.push_back(command);
}

/* Whether the buffers a draw reads were filled by this frame's upload stage.
 * Shared by the main pass and the shadow-map pass, which replay the same
 * queue and must skip exactly the same draws. */
bool DrawGeometryReady(const DrawCommand &draw, bool streamed_ready,
                       bool mesh_colours_ready, bool animated_vertices_ready)
{
    if (draw.resident_mesh)
    {
        /* The geometry was uploaded when the mesh was created; only the
         * colour stream is this frame's. */
        return mesh_colours_ready && draw.resident != nullptr;
    }
    if (!draw.indexed_mesh)
    {
        return streamed_ready;
    }
    return (draw.animated_mesh ? animated_vertices_ready
                               : mesh_colours_ready) &&
           draw.cached_mesh != nullptr &&
           (draw.cached_mesh->ready ||
            draw.cached_mesh->upload_recorded_frame == g_mesh_cache_frame);
}

MeshPipelineLayout DrawPipelineLayout(const DrawCommand &draw)
{
    if (draw.resident_mesh)
    {
        return MESH_PIPELINE_CACHED_STATIC;
    }
    if (draw.indexed_mesh)
    {
        return draw.animated_mesh ? MESH_PIPELINE_CACHED_ANIMATED
                                  : MESH_PIPELINE_CACHED_STATIC;
    }
    return MESH_PIPELINE_STREAMED;
}

/*
 * Bind and draw one resident mesh draw.
 *
 * The indices are relative to the start of the mesh, but both vertex streams
 * are bound at this draw's first vertex: the static one because the draw only
 * reads its own span, and the colour one because that is where this draw's
 * colours begin in the frame's stream.  So the draw is rebased by the same
 * amount, negatively -- index v then fetches element v - first of each stream,
 * which is static vertex v and the right colour both.  Every backend takes a
 * signed base vertex, and the element it lands on is never negative.
 */
bool DrawResidentMesh(SDL_GPURenderPass *pass, const DrawCommand &draw)
{
    if (draw.resident == nullptr || draw.resident->vertex_buffer == nullptr ||
        draw.resident->index_buffer == nullptr ||
        g_mesh_colour_buffer == nullptr || draw.index_count == 0)
    {
        return false;
    }

    SDL_GPUBufferBinding vertex_bindings[2]{};
    vertex_bindings[0].buffer = draw.resident->vertex_buffer;
    vertex_bindings[0].offset =
        draw.resident_first_vertex * (Uint32)sizeof(MeshStaticVertex);
    vertex_bindings[1].buffer = g_mesh_colour_buffer;
    vertex_bindings[1].offset =
        draw.first_mesh_colour * (Uint32)sizeof(MeshColour);
    SDL_BindGPUVertexBuffers(pass, 0, vertex_bindings, 2);

    SDL_GPUBufferBinding index_binding{};
    index_binding.buffer = draw.resident->index_buffer;
    index_binding.offset = 0;
    SDL_BindGPUIndexBuffer(pass, &index_binding,
                           SDL_GPU_INDEXELEMENTSIZE_32BIT);
    SDL_DrawGPUIndexedPrimitives(pass, draw.index_count, 1, draw.first_index,
                                 -(Sint32)draw.resident_first_vertex, 0);
    return true;
}

/*
 * Whether `next` can be folded into `previous`, the draw queued immediately
 * before it, so the two go to the GPU as one.
 *
 * The spans have to continue each other in all three streams -- indices,
 * static vertices and colours -- which is what lets the merged draw keep
 * DrawResidentMesh()'s single rebase.  Consecutive walker units of one model
 * satisfy that by construction; a unit skipped in between (a culled block)
 * breaks it, and so it should.
 *
 * Everything else has to be identical, and deliberately so: no reordering, no
 * sorting by texture.  Draw order is what this game's transparency and its
 * coplanar decals rely on -- a later draw at equal depth wins under GEQUAL --
 * so only draws that were going to be adjacent anyway are ever joined.
 *
 * A capture queued since `previous` is a hard stop: the capture copies the
 * frame as it stands after draws [0, draw_index), and growing the last of
 * those would put this draw's pixels into it.
 */
bool ResidentDrawContinues(const DrawCommand &previous, const DrawCommand &next)
{
    if (!previous.resident_mesh || !next.resident_mesh ||
        previous.resident != next.resident)
    {
        return false;
    }
    if (!g_capture_points.empty() &&
        g_capture_points.back().draw_index >= g_draws.size())
    {
        return false;
    }
    if (previous.first_index + previous.index_count != next.first_index ||
        previous.resident_first_vertex + previous.resident_vertex_count !=
            next.resident_first_vertex ||
        previous.first_mesh_colour + previous.resident_vertex_count !=
            next.first_mesh_colour)
    {
        return false;
    }
    return previous.source == next.source &&
           previous.texture == next.texture &&
           previous.texture_width == next.texture_width &&
           previous.texture_height == next.texture_height &&
           previous.depth_test == next.depth_test &&
           previous.depth_compare == next.depth_compare &&
           previous.repeat_uv == next.repeat_uv &&
           previous.min_linear == next.min_linear &&
           previous.mag_linear == next.mag_linear &&
           previous.preserve_original_aspect ==
               next.preserve_original_aspect &&
           previous.transform_mesh == next.transform_mesh &&
           previous.fragment_lighting == next.fragment_lighting &&
           (!next.fragment_lighting ||
            previous.fragment_light_index == next.fragment_light_index) &&
           previous.blend_mode == next.blend_mode &&
           previous.depth_write == next.depth_write &&
           std::memcmp(&previous.scissor, &next.scissor,
                       sizeof(next.scissor)) == 0 &&
           std::memcmp(previous.alpha_test, next.alpha_test,
                       sizeof(next.alpha_test)) == 0 &&
           std::memcmp(previous.mvp, next.mvp, sizeof(next.mvp)) == 0 &&
           std::memcmp(previous.model, next.model, sizeof(next.model)) == 0 &&
           previous.shadow_caster == next.shadow_caster &&
           previous.shadow_receiver == next.shadow_receiver &&
           previous.shadow_episode == next.shadow_episode &&
           !previous.finder_mask && !next.finder_mask;
}

/* Replays g_draws[first, last).  The range exists so a draw that samples the
 * GS frame buffer can break the pass: the caller replays up to it, copies the
 * colour target into the scene capture, and resumes.  Everything else passes
 * the whole queue. */
void DrawQueuedSprites(SDL_GPURenderPass *pass, SDL_GPUCommandBuffer *cmd,
                       Uint32 target_width, Uint32 target_height,
                       bool streamed_ready, bool mesh_colours_ready,
                       bool animated_vertices_ready,
                       size_t first, size_t last)
{
    if (first >= last || last > g_draws.size())
    {
        return;
    }

    SDL_GPUViewport viewport =
        BuildOutputViewport(target_width, target_height);
    SDL_SetGPUViewport(pass, &viewport);

    /* renderSize is the scene target; outputSize is the swapchain, and is set
     * once per frame by the caller because this runs once per pass segment.
     * They differ in every mode but MATCH_WINDOW -- which is exactly the ratio
     * the CRT filter needs to know its scanline pitch and phosphor triad
     * width, so the two fields must not be collapsed back together. */
    g_uniforms.renderSize[0] = (float)target_width;
    g_uniforms.renderSize[1] = (float)target_height;

    g_uniforms.flags1[1] = g_shadow_valid ? 1 : 0;
    g_uniforms.flags1[2] = g_dbg_shadow_view ? 1 : 0;

    /* Once per segment rather than per draw: the setting cannot move inside a
     * frame, and ActiveAnisoSlot() walks the table. */
    const int material_aniso_slot = ActiveAnisoSlot();

    SDL_GPUGraphicsPipeline *bound_pipeline = nullptr;
    Uint32 bound_vertex_light_index = std::numeric_limits<Uint32>::max();
    Uint32 bound_fragment_light_index = std::numeric_limits<Uint32>::max();
    bool scissor_active = false;
    bool streamed_vertices_bound = false;
    for (size_t draw_index = first; draw_index < last; draw_index++)
    {
        const DrawCommand &draw = g_draws[draw_index];
        if (!DrawGeometryReady(draw, streamed_ready, mesh_colours_ready,
                               animated_vertices_ready))
        {
            continue;
        }
        /* The caster's pixels belong to the shadow map, not the frame.
         * RecordShadowMapPass() has already replayed it. */
        if (draw.shadow_caster)
        {
            continue;
        }
        /* A receiver draw only exists to carry the projection; with no map to
         * project it would blend a flat dark sheet over the room. */
        if (draw.shadow_receiver)
        {
            if (!g_shadow_valid || g_shadow_texture == nullptr ||
                draw.shadow_episode < 0 ||
                draw.shadow_episode >= (int)g_shadow_episodes.size() ||
                !g_shadow_episodes[draw.shadow_episode].have_casters)
            {
                continue;
            }

            /* The projector this receiver was registered against, and the atlas
             * tile its map went into.  shadowSize carries the tile as
             * xy = scale, zw = offset, so the shader maps a 0..1 projected
             * coordinate onto the tile it owns. */
            const ShadowEpisode &episode = g_shadow_episodes[draw.shadow_episode];
            std::memcpy(g_uniforms.shadowMatrix, episode.matrix,
                        sizeof(g_uniforms.shadowMatrix));
            g_uniforms.params0[1] = episode.strength;

            const float inv_tiles = 1.0f / (float)kShadowAtlasTiles;
            g_uniforms.shadowSize[0] = inv_tiles;
            g_uniforms.shadowSize[1] = inv_tiles;
            g_uniforms.shadowSize[2] =
                (float)(draw.shadow_episode % (int)kShadowAtlasTiles) * inv_tiles;
            g_uniforms.shadowSize[3] =
                (float)(draw.shadow_episode / (int)kShadowAtlasTiles) * inv_tiles;

            /* One atlas texel in the same UV space shadowSize works in, so the
             * receiver can step by texels without knowing the tile size.  A
             * tile spans inv_tiles of the atlas and is kShadowTileSize across,
             * so a texel is the quotient. */
            g_uniforms.shadowFilter[0] = (float)g_shadow_filter;
            g_uniforms.shadowFilter[1] =
                inv_tiles / (float)kShadowTileSize;
        }
        else
        {
            g_uniforms.shadowFilter[0] = 0.0f;
            g_uniforms.shadowFilter[1] = 0.0f;
        }
        bool depth_test = draw.depth_test && !g_dbg_nodepth;
        const MeshPipelineLayout layout = DrawPipelineLayout(draw);
        SDL_GPUGraphicsPipeline *pipeline;
        if (draw.finder_mask)
        {
            /* Nothing to fall back to: without its own shader this draw would
             * paint a flat copy of the scene over the whole frame, which is
             * far worse than simply not running. */
            pipeline =
                EnsureFinderMaskPipeline()
                    ? g_finder_mask_pipeline[SampleSlotForCount(
                          g_pipeline_sample_count)]
                    : nullptr;
        }
        else if (draw.shadow_receiver)
        {
            pipeline =
                EnsureShadowReceiverPipelines(g_pipeline_sample_count)
                    ? g_shadow_receiver_pipeline
                          [SampleSlotForCount(g_pipeline_sample_count)][layout]
                    : nullptr;
        }
        else
        {
            pipeline = GetPipeline(layout,
                                   draw.indexed_mesh ? true
                                                     : draw.transform_mesh,
                                   depth_test, draw.depth_compare,
                                   draw.depth_write, g_dbg_wireframe,
                                   draw.blend_mode, draw.fragment_lighting);
        }
        if (pipeline == nullptr)
        {
            continue;
        }
        if (pipeline != bound_pipeline)
        {
            SDL_BindGPUGraphicsPipeline(pass, pipeline);
            bound_pipeline = pipeline;
        }

        /* GS SCISSOR.  Pass state, so it has to be restored to the full frame
         * for any draw that is not scissored rather than left set. */
        SDL_Rect scissor_rect;
        bool want_scissor = BuildScissorRect(draw, (int)target_width,
                                             (int)target_height,
                                             &scissor_rect);
        if (want_scissor || scissor_active)
        {
            SDL_Rect full{0, 0, (int)target_width, (int)target_height};
            SDL_SetGPUScissor(pass, want_scissor ? &scissor_rect : &full);
            scissor_active = want_scissor;
        }

        if (draw.transform_mesh)
        {
            std::memcpy(g_uniforms.mvp, draw.mvp, sizeof(g_uniforms.mvp));
            /* The receiver shader needs local->world of its own, because the
             * projector is expressed in world space -- the same product the
             * ROM forms as matLIP = s_matIP * matLocalWorld. */
            if (draw.fragment_lighting || draw.animated_mesh ||
                draw.shadow_receiver)
            {
                std::memcpy(g_uniforms.model, draw.model,
                            sizeof(g_uniforms.model));
            }
        }
        std::memcpy(g_uniforms.alphaTest, draw.alpha_test,
                    sizeof(g_uniforms.alphaTest));

        /* Per frame in effect -- it is a settings value, not draw state -- but
         * written here so it cannot be missed by a pass that builds its
         * uniforms from a different path. */
        g_uniforms.alphaSharpen[0] = g_alpha_sharpen;
        g_uniforms.alphaSharpen[1] = g_alpha_cutoff;
        g_uniforms.alphaSharpen[2] = 0.0f;
        g_uniforms.alphaSharpen[3] = 0.0f;

        /* Per draw, and written for every draw: sprite.vert serves the 2D
         * layer and the world-space billboards alike, and only the latter
         * arrive with their depth already reversed. */
        g_uniforms.clipZ[0] = draw.clip_z_reversed ? 1.0f : 0.0f;
        g_uniforms.clipZ[1] = 0.0f;
        g_uniforms.clipZ[2] = 0.0f;
        g_uniforms.clipZ[3] = 0.0f;

        /* Per draw rather than per frame: the aperture rides the camera's own
         * sway, so it moves between one finder frame and the next. */
        if (draw.finder_mask)
        {
            std::memcpy(g_uniforms.finderMask, draw.finder_mask_rect,
                        sizeof(g_uniforms.finderMask));
            std::memcpy(g_uniforms.finderMask2, draw.finder_mask_params,
                        sizeof(g_uniforms.finderMask2));
        }

        /* GS fogging.  _ModifyFogParam() folds gra3dIsFogEnable() into the
         * FOGE bit of all four SGD primitive tags, so on hardware fog covered
         * exactly the geometry that came through the transforming mesh path --
         * and nothing 2D, whose clip w is a meaningless 1.0.  transform_mesh
         * is that same split here. */
        std::memcpy(g_uniforms.fog, g_gs_fog, sizeof(g_uniforms.fog));
        std::memcpy(g_uniforms.fogColor, g_gs_fog_color,
                    sizeof(g_uniforms.fogColor));
        if (!draw.transform_mesh)
        {
            g_uniforms.fogColor[3] = 0.0f;
        }

        g_uniforms.textureSize[0] = (float)draw.texture_width;
        g_uniforms.textureSize[1] = (float)draw.texture_height;

        SDL_PushGPUVertexUniformData(cmd, 0, &g_uniforms,
                                     (Uint32)sizeof(g_uniforms));
        SDL_PushGPUFragmentUniformData(cmd, 0, &g_uniforms,
                                       (Uint32)sizeof(g_uniforms));
        if (draw.fragment_lighting)
        {
            if (draw.fragment_light_index >= g_fragment_light_states.size())
            {
                continue;
            }
            if (draw.fragment_light_index != bound_fragment_light_index)
            {
                SDL_PushGPUFragmentUniformData(
                    cmd, 1,
                    &g_fragment_light_states[draw.fragment_light_index],
                    (Uint32)sizeof(MioPanLightState));
                bound_fragment_light_index = draw.fragment_light_index;
            }
        }
        if (draw.animated_mesh)
        {
            if (draw.vertex_light_index >= g_vertex_light_states.size())
            {
                continue;
            }
            if (draw.vertex_light_index != bound_vertex_light_index)
            {
                SDL_PushGPUVertexUniformData(
                    cmd, 1, &g_vertex_light_states[draw.vertex_light_index],
                    (Uint32)sizeof(MioPanLightState));
                bound_vertex_light_index = draw.vertex_light_index;
            }
        }

        SDL_GPUTextureSamplerBinding samplers[2]{};
        samplers[0].texture = draw.texture;
        samplers[0].sampler =
            g_samplers[material_aniso_slot]
                      [draw.repeat_uv ? 1 : 0]
                      [draw.min_linear ? 1 : 0]
                      [draw.mag_linear ? 1 : 0];
        /* Slot 1 is uAuxTexture.  A receiver samples the shadow map through it
         * -- clamped, so the projector's edge does not wrap round the map --
         * and everything else leaves it on the white 1x1. */
        if (draw.shadow_receiver)
        {
            samplers[1].texture = g_shadow_texture;
            samplers[1].sampler = g_samplers[0][0][1][1];
        }
        else
        {
            samplers[1].texture = g_white_texture;
            samplers[1].sampler = g_samplers[0][0][1][1];
        }
        SDL_BindGPUFragmentSamplers(pass, 0, samplers, 2);

        if (draw.resident_mesh)
        {
            DrawResidentMesh(pass, draw);
            streamed_vertices_bound = false;
        }
        else if (draw.indexed_mesh)
        {
            if (draw.cached_mesh == nullptr ||
                draw.cached_mesh->vertex_buffer == nullptr ||
                draw.cached_mesh->index_buffer == nullptr ||
                (draw.animated_mesh
                     ? g_animated_mesh_buffer == nullptr
                     : g_mesh_colour_buffer == nullptr))
            {
                continue;
            }

            SDL_GPUBufferBinding vertex_bindings[2]{};
            vertex_bindings[0].buffer = draw.cached_mesh->vertex_buffer;
            vertex_bindings[0].offset =
                draw.cached_mesh->vertex_buffer_offset;
            if (draw.animated_mesh)
            {
                vertex_bindings[1].buffer = g_animated_mesh_buffer;
                vertex_bindings[1].offset =
                    draw.first_animated_mesh_vertex *
                    (Uint32)sizeof(AnimatedMeshVertex);
            }
            else
            {
                vertex_bindings[1].buffer = g_mesh_colour_buffer;
                vertex_bindings[1].offset =
                    draw.first_mesh_colour * (Uint32)sizeof(MeshColour);
            }
            SDL_BindGPUVertexBuffers(pass, 0, vertex_bindings, 2);

            SDL_GPUBufferBinding index_binding{};
            index_binding.buffer = draw.cached_mesh->index_buffer;
            index_binding.offset = draw.cached_mesh->index_buffer_offset;
            SDL_BindGPUIndexBuffer(pass, &index_binding,
                                   SDL_GPU_INDEXELEMENTSIZE_32BIT);
            SDL_DrawGPUIndexedPrimitives(
                pass, draw.cached_mesh->index_count, 1, 0, 0, 0);
            streamed_vertices_bound = false;
        }
        else
        {
            if (!streamed_vertices_bound)
            {
                SDL_GPUBufferBinding vertex_binding{};
                vertex_binding.buffer = g_vertex_buffer;
                vertex_binding.offset = 0;
                SDL_BindGPUVertexBuffers(pass, 0, &vertex_binding, 1);
                streamed_vertices_bound = true;
            }
            SDL_DrawGPUPrimitives(pass, draw.vertex_count, 1,
                                  draw.first_vertex, 0);
        }
    }
}

/*
 * The caster pass -- the host stand-in for gra3dShadow.c's _RenderShadow().
 *
 * The ROM points a camera down the light direction, renders the caster into an
 * off-screen PSMT8H target and scissors two pixels in from its edge so the
 * projection cannot sample a half-covered border texel.  Everything except the
 * target format is reproduced here: the mvp each tagged draw carries is already
 * the light camera's, because _ApplyCamera(&s_Camera) reaches the host through
 * MioPan_Graph3dApplyCamera() before the caster is drawn.
 *
 * Only coverage matters, so the material, blend mode, alpha test and fog the
 * draw was queued with are all ignored: the silhouette pipeline writes white.
 */
bool RecordShadowMapPass(SDL_GPUCommandBuffer *cmd, bool streamed_ready,
                         bool mesh_colours_ready, bool animated_vertices_ready)
{
    if (g_shadow_texture == nullptr || g_shadow_episodes.empty())
    {
        return false;
    }

    bool any_casters = false;
    for (const ShadowEpisode &episode : g_shadow_episodes)
    {
        any_casters = any_casters || episode.have_casters;
    }
    if (!any_casters)
    {
        return false;
    }

    SDL_GPUColorTargetInfo target{};
    target.texture = g_shadow_texture;
    target.load_op = SDL_GPU_LOADOP_CLEAR;
    target.store_op = SDL_GPU_STOREOP_STORE;
    target.clear_color.r = 0.0f;
    target.clear_color.g = 0.0f;
    target.clear_color.b = 0.0f;
    target.clear_color.a = 0.0f;

    SDL_GPURenderPass *pass = SDL_BeginGPURenderPass(cmd, &target, 1, nullptr);
    if (pass == nullptr)
    {
        return false;
    }

    SDL_GPUGraphicsPipeline *bound_pipeline = nullptr;
    bool drew_any = false;

    for (size_t index = 0; index < g_shadow_episodes.size(); index++)
    {
        const ShadowEpisode &episode = g_shadow_episodes[index];
        if (!episode.have_casters)
        {
            continue;
        }

        const Uint32 tile_x = (Uint32)(index % kShadowAtlasTiles) * kShadowTileSize;
        const Uint32 tile_y = (Uint32)(index / kShadowAtlasTiles) * kShadowTileSize;

        SDL_GPUViewport viewport{};
        viewport.x = (float)tile_x;
        viewport.y = (float)tile_y;
        viewport.w = (float)kShadowTileSize;
        viewport.h = (float)kShadowTileSize;
        viewport.min_depth = 0.0f;
        viewport.max_depth = 1.0f;
        SDL_SetGPUViewport(pass, &viewport);

        /* The ROM's two-pixel inset (SCISSOR_1 in _RenderShadow), for the same
         * reason and one more: the receiver's bounds test passes right up to
         * the edge of the tile, so a caster touching it would smear its last
         * texel -- and here it would smear into the neighbouring shadow. */
        SDL_Rect scissor{(int)tile_x + 2, (int)tile_y + 2,
                         (int)kShadowTileSize - 4, (int)kShadowTileSize - 4};
        SDL_SetGPUScissor(pass, &scissor);

        bool streamed_vertices_bound = false;
        const size_t last = std::min(episode.last_caster_draw, g_draws.size());
        for (size_t draw_index = episode.first_caster_draw; draw_index < last;
             draw_index++)
        {
            const DrawCommand &draw = g_draws[draw_index];
            if (!draw.shadow_caster || draw.shadow_episode != (int)index)
            {
                continue;
            }
            if (!DrawGeometryReady(draw, streamed_ready, mesh_colours_ready,
                                   animated_vertices_ready))
            {
                continue;
            }

            const MeshPipelineLayout layout = DrawPipelineLayout(draw);
            SDL_GPUGraphicsPipeline *pipeline = g_shadow_caster_pipeline[layout];
            if (pipeline == nullptr)
            {
                continue;
            }
            if (pipeline != bound_pipeline)
            {
                SDL_BindGPUGraphicsPipeline(pass, pipeline);
                bound_pipeline = pipeline;
                streamed_vertices_bound = false;
            }

            std::memcpy(g_uniforms.mvp, draw.mvp, sizeof(g_uniforms.mvp));
            std::memcpy(g_uniforms.model, draw.model, sizeof(g_uniforms.model));
            SDL_PushGPUVertexUniformData(cmd, 0, &g_uniforms,
                                         (Uint32)sizeof(g_uniforms));
            SDL_PushGPUFragmentUniformData(cmd, 0, &g_uniforms,
                                           (Uint32)sizeof(g_uniforms));

            /* Both fragment stages declare two samplers, so both slots have to
             * be filled even though the silhouette reads neither. */
            SDL_GPUTextureSamplerBinding samplers[2]{};
            samplers[0].texture = g_white_texture;
            samplers[0].sampler = g_samplers[0][0][1][1];
            samplers[1].texture = g_white_texture;
            samplers[1].sampler = g_samplers[0][0][1][1];
            SDL_BindGPUFragmentSamplers(pass, 0, samplers, 2);

            if (draw.resident_mesh)
            {
                if (!DrawResidentMesh(pass, draw))
                {
                    continue;
                }
                streamed_vertices_bound = false;
            }
            else if (draw.indexed_mesh)
            {
                if (draw.cached_mesh->vertex_buffer == nullptr ||
                    draw.cached_mesh->index_buffer == nullptr ||
                    (draw.animated_mesh ? g_animated_mesh_buffer == nullptr
                                        : g_mesh_colour_buffer == nullptr))
                {
                    continue;
                }

                SDL_GPUBufferBinding vertex_bindings[2]{};
                vertex_bindings[0].buffer = draw.cached_mesh->vertex_buffer;
                vertex_bindings[0].offset =
                    draw.cached_mesh->vertex_buffer_offset;
                if (draw.animated_mesh)
                {
                    vertex_bindings[1].buffer = g_animated_mesh_buffer;
                    vertex_bindings[1].offset =
                        draw.first_animated_mesh_vertex *
                        (Uint32)sizeof(AnimatedMeshVertex);
                }
                else
                {
                    vertex_bindings[1].buffer = g_mesh_colour_buffer;
                    vertex_bindings[1].offset =
                        draw.first_mesh_colour * (Uint32)sizeof(MeshColour);
                }
                SDL_BindGPUVertexBuffers(pass, 0, vertex_bindings, 2);

                SDL_GPUBufferBinding index_binding{};
                index_binding.buffer = draw.cached_mesh->index_buffer;
                index_binding.offset = draw.cached_mesh->index_buffer_offset;
                SDL_BindGPUIndexBuffer(pass, &index_binding,
                                       SDL_GPU_INDEXELEMENTSIZE_32BIT);
                SDL_DrawGPUIndexedPrimitives(
                    pass, draw.cached_mesh->index_count, 1, 0, 0, 0);
                streamed_vertices_bound = false;
            }
            else
            {
                if (!streamed_vertices_bound)
                {
                    SDL_GPUBufferBinding vertex_binding{};
                    vertex_binding.buffer = g_vertex_buffer;
                    vertex_binding.offset = 0;
                    SDL_BindGPUVertexBuffers(pass, 0, &vertex_binding, 1);
                    streamed_vertices_bound = true;
                }
                SDL_DrawGPUPrimitives(pass, draw.vertex_count, 1,
                                      draw.first_vertex, 0);
            }
            drew_any = true;
        }
    }

    SDL_EndGPURenderPass(pass);
    return drew_any;
}

bool BuildMeshSubmissionPins(
    const std::vector<MeshCacheEntryPtr> &uploaded_entries,
    bool include_draw_entries, std::vector<MeshCacheEntryPtr> *out)
{
    if (out == nullptr)
    {
        return false;
    }
    out->clear();
    try
    {
        out->reserve((include_draw_entries ? g_draws.size() : 0u) +
                     uploaded_entries.size());
        const auto append = [out](const MeshCacheEntryPtr &entry) {
            if (entry == nullptr || !MeshCacheHasGpuStorage(*entry))
            {
                return;
            }
            out->push_back(entry);
        };
        if (include_draw_entries)
        {
            for (const DrawCommand &draw : g_draws)
            {
                if (draw.indexed_mesh)
                {
                    append(draw.cached_mesh);
                }
            }
        }
        for (const MeshCacheEntryPtr &entry : uploaded_entries)
        {
            append(entry);
        }
        std::sort(out->begin(), out->end(),
                  [](const MeshCacheEntryPtr &a,
                     const MeshCacheEntryPtr &b) {
                      return std::less<MeshCacheEntry *>{}(a.get(), b.get());
                  });
        out->erase(std::unique(out->begin(), out->end(),
                              [](const MeshCacheEntryPtr &a,
                                 const MeshCacheEntryPtr &b) {
                                  return a.get() == b.get();
                              }),
                   out->end());
    }
    catch (const std::bad_alloc &)
    {
        out->clear();
        return false;
    }
    return true;
}

bool SubmitMeshCommandBuffer(SDL_GPUCommandBuffer *cmd,
                             std::vector<MeshCacheEntryPtr> &&pins)
{
    if (cmd == nullptr)
    {
        return false;
    }
    if (pins.empty())
    {
        return SDL_SubmitGPUCommandBuffer(cmd);
    }

    try
    {
        if (g_inflight_mesh_submissions.size() ==
            g_inflight_mesh_submissions.capacity())
        {
            g_inflight_mesh_submissions.reserve(
                std::max<size_t>(8u,
                    g_inflight_mesh_submissions.size() * 2u));
        }
    }
    catch (const std::bad_alloc &)
    {
        /* An allocation failure in lifetime bookkeeping must never permit an
         * arena slice to be recycled early.  Submit normally and take the
         * exceptional drain instead of risking corruption. */
        const bool submitted = SDL_SubmitGPUCommandBuffer(cmd);
        /* A backend may have queued native work even when presentation or
         * bookkeeping makes SDL report failure.  Drain before the local pins
         * unwind in either case. */
        SDL_WaitForGPUIdle(g_device);
        return submitted;
    }

    SDL_GPUFence *fence =
        SDL_SubmitGPUCommandBufferAndAcquireFence(cmd);
    if (fence == nullptr)
    {
        /* Some backends can execute the native command list before fence
         * creation/signalling reports failure.  Keep the arena slices pinned
         * until an exceptional full drain proves that either outcome is safe;
         * the caller still treats the upload as uncommitted and retries it. */
        SDL_WaitForGPUIdle(g_device);
        return false;
    }
    InFlightMeshSubmission submission{};
    submission.fence = fence;
    submission.entries = std::move(pins);
    g_inflight_mesh_submissions.push_back(std::move(submission));
    return true;
}

bool ShouldPresentFrame()
{
    int interval = g_present_interval <= 0 ? 1 : g_present_interval;
    bool present = (g_present_counter % interval) == 0;

    g_present_counter++;
    return present;
}

void CountGameFrame()
{
    if (g_fps_window_start_counter == 0)
    {
        g_fps_window_start_counter = SDL_GetPerformanceCounter();
    }

    g_fps_game_frames++;
}

void UpdatePerformanceStats(bool presented)
{
    Uint64 now;
    Uint64 frequency;
    double elapsed;
    MioPanUi::PerformanceStats stats{};

    if (g_fps_window_start_counter == 0)
    {
        return;
    }

    /* Every logical frame, before the window check: the live GS figures are
     * zeroed right after this call. */
    g_window_gs_uploads += MioPan_GsGetUploadCountLive();
    g_window_gs_upload_bytes += MioPan_GsGetUploadBytesLive();

    if (presented)
    {
        g_fps_present_frames++;
    }

    now = SDL_GetPerformanceCounter();
    frequency = SDL_GetPerformanceFrequency();
    elapsed = (double)(now - g_fps_window_start_counter) / (double)frequency;
    if (elapsed < 1.0)
    {
        return;
    }

    stats.game_fps = (double)g_fps_game_frames / elapsed;
    stats.present_fps = (double)g_fps_present_frames / elapsed;
    MioPan_ProfilerConsumeStats(&stats.profiler);
    stats.draw_count = g_draws.size();
    stats.vertex_count = g_vertices.size() + g_mesh_colours.size() +
                         g_animated_mesh_vertices_stream.size();
    stats.mesh_triangles_submitted = g_mesh_triangles_submitted;
    stats.mesh_triangles_clipped = g_mesh_triangles_clipped;
    stats.mesh_cache_hits = g_mesh_cache_hits;
    stats.mesh_cache_misses = g_mesh_cache_misses;
    stats.mesh_cache_creates = g_mesh_cache_creates;
    stats.mesh_cache_evictions = g_mesh_cache_evictions;
    stats.mesh_cache_invalidations = g_mesh_cache_invalidations;
    stats.mesh_cache_upload_failures = g_mesh_cache_upload_failures;
    stats.mesh_cache_build_deferred = g_mesh_cache_build_deferred;
    stats.mesh_cache_upload_deferred = g_mesh_cache_upload_deferred;
    stats.mesh_cache_promotions = g_mesh_cache_promotions;
    stats.mesh_cache_entries = g_mesh_cache.size();
    stats.mesh_cache_pending_entries = 0;
    for (const auto &cached : g_mesh_cache)
    {
        if (cached.second != nullptr && !cached.second->ready)
        {
            stats.mesh_cache_pending_entries++;
        }
    }
    stats.mesh_cache_bytes = g_mesh_cache_bytes;
    stats.mesh_arena_bytes = MeshArenaCapacityBytes();
    stats.mesh_arena_pages = g_mesh_vertex_arena.pages.size() +
                             g_mesh_index_arena.pages.size();
    stats.mesh_cache_upload_bytes = (size_t)g_mesh_cache_upload_bytes;
    stats.mesh_colour_upload_bytes = (size_t)g_mesh_colour_upload_bytes;
    stats.animated_mesh_upload_bytes =
        (size_t)g_animated_mesh_upload_bytes;
    stats.mesh_expanded_vertices_avoided =
        (size_t)g_mesh_expanded_vertices_avoided;
    stats.mesh_direct_stream_vertices =
        (size_t)g_mesh_direct_stream_vertices;
    stats.animated_mesh_vertices = (size_t)g_animated_mesh_vertices;
    stats.animated_mesh_expanded_vertices_avoided =
        (size_t)g_animated_mesh_expanded_vertices_avoided;
    stats.resident_meshes = g_resident_meshes.size();
    stats.resident_bytes = g_resident_mesh_bytes;
    stats.resident_units = g_resident_units;
    stats.resident_draws = g_resident_draws;
    stats.resident_creates = g_resident_creates;
    stats.resident_texture_captures = g_resident_texture_captures;
    stats.resident_texture_skips = g_resident_texture_skips;
    stats.resident_texture_replays = g_resident_texture_replays;
    stats.gs_uploads = (size_t)g_window_gs_uploads;
    stats.gs_upload_bytes = (size_t)g_window_gs_upload_bytes;
    stats.window_frames = g_fps_game_frames;
    stats.texture_l1_hits = g_texture_l1_hits;
    stats.texture_l1_lookups = g_texture_l1_lookups;
    stats.texture_l2_hits = g_texture_l2_hits;
    stats.texture_creates = g_texture_creates;
    stats.texture_downloads = g_texture_downloads;
    stats.texture_invalidations = g_texture_invalidations;
    stats.font_texture_hits = g_font_texture_hits;
    stats.font_texture_selects = g_font_texture_selects;
    stats.font_texture_creates = g_font_texture_creates;
    stats.font_texture_invalidations = g_font_texture_invalidations;
    MioPanUi::SetPerformanceStats(stats);

    g_fps_window_start_counter = now;
    g_fps_game_frames = 0;
    g_fps_present_frames = 0;
    g_texture_l1_lookups = 0;
    g_texture_l1_hits = 0;
    g_texture_l2_hits = 0;
    g_texture_downloads = 0;
    g_texture_creates = 0;
    g_texture_invalidations = 0;
    g_font_texture_hits = 0;
    g_font_texture_selects = 0;
    g_font_texture_creates = 0;
    g_font_texture_invalidations = 0;
    g_mesh_triangles_submitted = 0;
    g_mesh_triangles_clipped = 0;
    g_mesh_cache_hits = 0;
    g_mesh_cache_misses = 0;
    g_mesh_cache_creates = 0;
    g_mesh_cache_evictions = 0;
    g_mesh_cache_invalidations = 0;
    g_mesh_cache_upload_failures = 0;
    g_mesh_cache_build_deferred = 0;
    g_mesh_cache_upload_deferred = 0;
    g_mesh_cache_promotions = 0;
    g_mesh_cache_upload_bytes = 0;
    g_mesh_colour_upload_bytes = 0;
    g_animated_mesh_upload_bytes = 0;
    g_mesh_expanded_vertices_avoided = 0;
    g_animated_mesh_vertices = 0;
    g_animated_mesh_expanded_vertices_avoided = 0;
    g_mesh_direct_stream_vertices = 0;
    g_resident_units = 0;
    g_resident_draws = 0;
    g_resident_creates = 0;
    g_resident_texture_captures = 0;
    g_resident_texture_skips = 0;
    g_resident_texture_replays = 0;
    g_window_gs_uploads = 0;
    g_window_gs_upload_bytes = 0;
}

/*
 * Bounded texture and mesh-cache warming with nothing drawn.
 *
 * Two callers want exactly this: a logical frame the present interval skips,
 * and a frame that acquired a command buffer but missed the swapchain.  In
 * both, cold texture pixels and immutable mesh topology need not wait for the
 * next presented frame, so they move now and that frame does not inherit the
 * burst.  `submit_label` only names which of the two a submit failure came
 * from.
 */
void RecordCopyOnlyUploads(SDL_GPUCommandBuffer *cmd, const char *submit_label)
{
    const uint64_t upload_start =
        MioPan_ProfilerBeginPhase(MIOPAN_PROFILE_RENDERER_UPLOAD);
    TextureUploadResult texture_upload = UploadPendingTextures(cmd);
    MeshUploadResult mesh_upload = UploadMeshCacheOnly(cmd);
    DisableMeshCacheEntries(mesh_upload.failed_entries);
    MioPan_ProfilerEndPhase(MIOPAN_PROFILE_RENDERER_UPLOAD, upload_start);

    std::vector<MeshCacheEntryPtr> submission_pins;
    const bool pins_ready = BuildMeshSubmissionPins(
        mesh_upload.uploaded_entries, false, &submission_pins);
    const uint64_t submit_start =
        MioPan_ProfilerBeginPhase(MIOPAN_PROFILE_RENDERER_SUBMIT);
    const bool submitted = pins_ready
        ? SubmitMeshCommandBuffer(cmd, std::move(submission_pins))
        : SDL_SubmitGPUCommandBuffer(cmd);
    if (!pins_ready)
    {
        SDL_WaitForGPUIdle(g_device);
    }
    MioPan_ProfilerEndPhase(MIOPAN_PROFILE_RENDERER_SUBMIT, submit_start);
    if (submitted)
    {
        CommitTextureUploads(texture_upload);
        CommitMeshUploads(mesh_upload.uploaded_entries, 0, 0);
    }
    else
    {
        LogSdlError(submit_label);
    }
    for (SDL_GPUTransferBuffer *transfer : texture_upload.transfers)
    {
        SDL_ReleaseGPUTransferBuffer(g_device, transfer);
    }
    for (SDL_GPUTransferBuffer *transfer : mesh_upload.transfers)
    {
        SDL_ReleaseGPUTransferBuffer(g_device, transfer);
    }
}

/*
 * The frame's upload stage: everything that writes a GPU buffer or mutates the
 * draw queue, in the order the record stage needs it.
 *
 * It runs once per logical frame, and a repeated present must not run any of it
 * again -- ApplyOriginalAspectToVertices() applies its scale in place, so a
 * second pass would shrink every screen-space draw twice, and
 * ConvertMeshDrawsToStreamed() rewrites g_draws.  All the record stage needs
 * out of it afterwards is the three readiness flags, which is why they are
 * latched into g_frame_upload rather than carried down as locals.
 */
void UploadFrameResources(SDL_GPUCommandBuffer *cmd,
                          TextureUploadResult *texture_upload,
                          MeshUploadResult *mesh_upload,
                          SDL_GPUTransferBuffer **vertex_transfer,
                          float geometry_t)
{
    const uint64_t upload_start =
        MioPan_ProfilerBeginPhase(MIOPAN_PROFILE_RENDERER_UPLOAD);
    ApplyOriginalAspectToVertices();

    *texture_upload = UploadPendingTextures(cmd);
    /* A transfer allocation/copy-pass failure must not sample an
     * uninitialized texture.  Keep the decoded pixels queued for the next
     * presented frame and degrade only affected draws to the established white
     * texture for this submission. */
    FallbackUnrecordedPendingTextures(*texture_upload);

    *mesh_upload = UploadMeshData(cmd);
    std::vector<MeshCacheEntryPtr> fallback_entries =
        mesh_upload->failed_entries;
    fallback_entries.insert(fallback_entries.end(),
                            mesh_upload->retry_entries.begin(),
                            mesh_upload->retry_entries.end());
    fallback_entries.insert(fallback_entries.end(),
                            mesh_upload->deferred_entries.begin(),
                            mesh_upload->deferred_entries.end());
    if (!mesh_upload->colours_ready)
    {
        /* Every preset entry retains a budgeted CPU topology copy, so both
         * warm and newly promoted draws can use the established interleaved
         * path in this frame. */
        for (const DrawCommand &draw : g_draws)
        {
            if (draw.indexed_mesh && !draw.animated_mesh &&
                draw.cached_mesh != nullptr &&
                draw.cached_mesh->key.layout_kind == MIOPAN_MESH_CACHE_PRESET)
            {
                fallback_entries.push_back(draw.cached_mesh);
            }
        }
        if (!g_mesh_colours.empty())
        {
            g_mesh_cache_upload_failures++;
        }
    }
    if (!mesh_upload->animated_vertices_ready)
    {
        /* Every animated entry retains one budgeted CPU UV/index copy, so a
         * transient shared-stream failure degrades to the option-6 expansion
         * instead of hiding characters. */
        for (const DrawCommand &draw : g_draws)
        {
            if (draw.indexed_mesh && draw.animated_mesh &&
                draw.cached_mesh != nullptr &&
                draw.cached_mesh->key.layout_kind == MIOPAN_MESH_CACHE_ANIMATED)
            {
                fallback_entries.push_back(draw.cached_mesh);
            }
        }
        if (!g_animated_mesh_vertices_stream.empty())
        {
            g_mesh_cache_upload_failures++;
        }
    }
    if (!fallback_entries.empty())
    {
        /* ConvertMeshDrawsToStreamed() appends to g_vertices and rewrites
         * first_vertex/vertex_count, which invalidates a snapshot taken before
         * the upload stage.  Never reached in this build -- the mesh cache has
         * no callers and g_draws carries no indexed draw -- but the snapshot's
         * whole safety rests on those indices, so it is checked rather than
         * assumed. */
        InvalidateGeometrySnapshot();
    }
    ConvertMeshDrawsToStreamed(fallback_entries);
    DisableMeshCacheEntries(mesh_upload->failed_entries);

    /* Last, and after the conversion: the blend writes positions into
     * g_vertices and the upload has to carry them. */
    ApplyGeometryVertexBlend(geometry_t);
    const bool vertices_uploaded = UploadVertexBuffer(cmd, vertex_transfer);
    MioPan_ProfilerEndPhase(MIOPAN_PROFILE_RENDERER_UPLOAD, upload_start);

    g_frame_upload.valid = true;
    g_frame_upload.vertices_uploaded = vertices_uploaded;
    g_frame_upload.colours_ready = mesh_upload->colours_ready;
    g_frame_upload.animated_vertices_ready =
        mesh_upload->animated_vertices_ready;
}

/*
 * The frame's record stage.  Read-only over the draw queue -- DrawQueuedSprites
 * takes each command by const reference -- which is what lets it run more than
 * once against a single upload stage.
 *
 * `repeat` marks a present that is re-recording a draw list this logical frame
 * has already presented:
 *
 *  - The shadow map is light-relative and did not move with the camera, so it
 *    is left in place, g_shadow_valid along with it.
 *  - The GS block captures are reused rather than retaken.  They hold this
 *    same frame's composite, taken at the right point in the queue, and
 *    skipping them collapses the scene back to a single pass segment.  A slot
 *    captured twice in one frame keeps only its last capture, which a repeated
 *    present's earlier draws then sample -- accepted, because the frames that
 *    repeat are in-betweens.
 *  - The pause still and the screen mirror are the game's own frame and were
 *    already taken by the real present; retaking them from an in-between would
 *    hand the game a picture it never simulated.
 */
void RecordFramePasses(SDL_GPUCommandBuffer *cmd,
                       SDL_GPUTexture *swapchain_texture,
                       Uint32 swapchain_width, Uint32 swapchain_height,
                       bool repeat, bool *capture_recorded,
                       bool *mirror_recorded)
{
    const bool vertices_uploaded = g_frame_upload.vertices_uploaded;
    const bool colours_ready = g_frame_upload.colours_ready;
    const bool animated_vertices_ready = g_frame_upload.animated_vertices_ready;

    const uint64_t record_start =
        MioPan_ProfilerBeginPhase(MIOPAN_PROFILE_RENDERER_RECORD);

    /*
     * Where the frame is composited.
     *
     * Ordinarily that is the swapchain itself and everything below reads as it
     * always did.  When the present pass has work to do -- an HDR swapchain, or
     * a grade the player has moved off identity -- the bars, the resampled
     * scene and the host UI go into the output target instead, in the game's
     * own SDR encoding, and the pass converts the finished composite once at
     * the end.  Compositing before the transform rather than after is what
     * keeps the UI's alpha blending and the letterbox black defined in the
     * space they were authored in.
     *
     * A pass that is needed and cannot be built -- a missing shader, or a
     * target that will not allocate -- gives up on HDR rather than limping.
     * Compositing an SDR picture straight into an HDR swapchain would not just
     * look wrong, it would put pipelines built for one format in front of a
     * target with another; turning the mode off puts the swapchain back on a
     * composition everything already agrees on, at the next frame's
     * ApplyHdrComposition().
     */
    SDL_GPUTexture *output_texture = swapchain_texture;
    if (NeedsPresentPass())
    {
        if (EnsurePresentPipeline() &&
            EnsureOutputTexture(swapchain_width, swapchain_height))
        {
            output_texture = g_output_texture;
        }
        else if (g_hdr_mode != MIOPAN_HDR_OFF)
        {
            SDL_Log("MioPan SDL_GPU: present pass unavailable; disabling HDR");
            g_hdr_mode = MIOPAN_HDR_OFF;
        }
    }
    const SDL_GPUTextureFormat output_texture_format =
        output_texture == swapchain_texture ? g_swapchain_format
                                            : g_output_format;

    /*
     * Resolve the internal render resolution and pick what the frame is
     * rasterised into.
     *
     * When it comes out equal to the output -- MATCH_WINDOW, or a scale that
     * happens to land on it -- the scene target is skipped and drawing goes
     * straight into the swapchain, so the default path costs nothing and
     * behaves exactly as it did before the split.  A target that will not
     * allocate falls back to the same direct path rather than dropping the
     * frame.
     *
     * The format test is the other half of "equal to the output": every mesh
     * pipeline was built against g_scene_format, so drawing straight into the
     * composite is only legal when the composite has that format.  It always
     * does when the composite is the output target; it does not when the
     * composite is an HDR swapchain, which is what forces the scene target on
     * a session that turned HDR on after startup.
     */
    Uint32 render_width = 0;
    Uint32 render_height = 0;
    ComputeRenderSize(swapchain_width, swapchain_height, &render_width,
                      &render_height);

    SDL_GPUTexture *scene_texture = output_texture;
    bool render_direct = render_width == swapchain_width &&
                         render_height == swapchain_height &&
                         output_texture_format == g_scene_format;
    if (!render_direct)
    {
        if (EnsureSceneTexture(render_width, render_height))
        {
            scene_texture = g_scene_texture;
        }
        else if (output_texture_format == g_scene_format)
        {
            render_direct = true;
            render_width = swapchain_width;
            render_height = swapchain_height;
        }
        else
        {
            /* No scene target, and the composite is a format the pipelines
             * cannot render into -- so there is nowhere legal to draw this
             * frame.  Clear the swapchain and give up on it; the next frame
             * re-runs the whole resolution above with HDR already turned off
             * by the block that could not build the present pass. */
            SDL_GPUColorTargetInfo blank{};
            blank.texture = swapchain_texture;
            blank.clear_color = {0.0f, 0.0f, 0.0f, 1.0f};
            blank.load_op = SDL_GPU_LOADOP_CLEAR;
            blank.store_op = SDL_GPU_STOREOP_STORE;
            SDL_GPURenderPass *blank_pass =
                SDL_BeginGPURenderPass(cmd, &blank, 1, nullptr);
            if (blank_pass != nullptr)
            {
                SDL_EndGPURenderPass(blank_pass);
            }
            MioPan_ProfilerEndPhase(MIOPAN_PROFILE_RENDERER_RECORD,
                                    record_start);
            return;
        }
    }
    g_render_width = render_width;
    g_render_height = render_height;

    /* The one number in the startup banner that is not known until a frame has
     * been recorded, so it is reported here -- once, and again after a resize
     * or a resolution change, which is exactly when a bug report needs it. */
    {
        static Uint32 logged_render_width;
        static Uint32 logged_render_height;
        static Uint32 logged_swapchain_width;
        static Uint32 logged_swapchain_height;

        if (render_width != logged_render_width ||
            render_height != logged_render_height ||
            swapchain_width != logged_swapchain_width ||
            swapchain_height != logged_swapchain_height)
        {
            logged_render_width = render_width;
            logged_render_height = render_height;
            logged_swapchain_width = swapchain_width;
            logged_swapchain_height = swapchain_height;
            SDL_Log("MioPan gpu: render %ux%u -> swapchain %ux%u%s",
                    render_width, render_height, swapchain_width,
                    swapchain_height, render_direct ? " (direct)" : "");
        }
    }

    /*
     * Settle this frame's sample count before anything asks for a pipeline.
     *
     * MSAA covers the frame the game is rasterised into and stops there: the
     * multisampled colour buffer is resolved into `scene_texture` at the end
     * of every pass segment, and everything that reads the frame afterwards --
     * the GS capture slots the effects sample, the pause still, the 640x448
     * screen mirror a photograph comes out of, the present blit and the host
     * UI -- reads the resolved, single-sampled copy exactly as before.
     *
     * That resolve is the cost, and it is not free on a frame the effects
     * break up: each capture point already ends a pass so the frame so far can
     * be copied, and with MSAA on each of those ends now resolves the whole
     * target as well.  A frame with several refraction or smear effects
     * therefore pays several full-resolution resolves, on top of the
     * rasterisation itself being N times the shading work at the edges.  4x at
     * a high internal resolution is the setting to reach for first.
     *
     * A target that will not allocate falls back to 1x for this frame rather
     * than dropping it, which is the same rule the scene target follows above.
     */
    int frame_samples = g_msaa_active;
    /* Both attachments or neither: a pass whose colour target is multisampled
     * and whose depth target is not is invalid, so a depth buffer that will
     * not allocate at this count takes the whole frame back to 1x rather than
     * leaving the pass without depth. */
    bool msaa_on = frame_samples > 1 &&
                   EnsureMsaaColorTexture(render_width, render_height,
                                          frame_samples) &&
                   EnsureDepthTexture(render_width, render_height,
                                      frame_samples);
    if (!msaa_on)
    {
        frame_samples = 1;
    }
    g_pipeline_sample_count = frame_samples;

    /* A no-op when the test above already built it; the rebuild at 1x is what
     * the fallback needs. */
    bool depth_ready =
        EnsureDepthTexture(render_width, render_height, frame_samples);

    /* Set once per frame: DrawQueuedSprites runs per pass segment and owns
     * renderSize, but outputSize is the swapchain and does not change between
     * segments. */
    g_uniforms.outputSize[0] = (float)swapchain_width;
    g_uniforms.outputSize[1] = (float)swapchain_height;

    SDL_GPUColorTargetInfo target{};
    target.texture = msaa_on ? g_msaa_color_texture : scene_texture;
    target.mip_level = 0;
    target.layer_or_depth_plane = 0;
    target.clear_color = {0.0f, 0.0f, 0.0f, 1.0f};
    target.load_op = SDL_GPU_LOADOP_CLEAR;
    /* RESOLVE_AND_STORE while more segments may follow -- a capture point
     * needs the resolve, and the segment after it resumes with LOADOP_LOAD on
     * the multisample contents, which plain RESOLVE is allowed to discard.
     * The last segment drops to RESOLVE, which is the cheap one. */
    target.store_op =
        msaa_on ? SDL_GPU_STOREOP_RESOLVE_AND_STORE : SDL_GPU_STOREOP_STORE;
    target.resolve_texture = msaa_on ? scene_texture : nullptr;
    target.resolve_mip_level = 0;
    target.resolve_layer = 0;
    target.cycle_resolve_texture = false;
    target.cycle = false;

    SDL_GPUDepthStencilTargetInfo depth{};
    SDL_GPUDepthStencilTargetInfo *depth_ptr = nullptr;
    if (depth_ready)
    {
        depth.texture = g_depth_texture;
        /* Reversed-Z: far is 0, so the empty buffer clears to 0 and the
         * GREATER_OR_EQUAL test lets anything through. */
        depth.clear_depth = 0.0f;
        depth.load_op = SDL_GPU_LOADOP_CLEAR;
        depth.store_op = SDL_GPU_STOREOP_STORE;
        depth.stencil_load_op = SDL_GPU_LOADOP_CLEAR;
        depth.stencil_store_op = SDL_GPU_STOREOP_DONT_CARE;
        depth.clear_stencil = 0;
        depth_ptr = &depth;
    }

    /*
     * The shadow map, before anything is drawn to the frame.  On hardware
     * _RenderShadow() ran inline, between the receivers being registered and
     * the projection pass; here the caster draws are tagged as they are queued
     * and replayed into their own target now, which puts the map in place
     * before the first receiver samples it.
     */
    if (!repeat)
    {
        g_shadow_valid = RecordShadowMapPass(cmd, vertices_uploaded,
                                             colours_ready,
                                             animated_vertices_ready);
    }

    /*
     * The queue is replayed in one pass unless an effect samples the GS frame
     * buffer.  Each such draw breaks the pass: the segment before it is drawn
     * and ended, the colour target is copied into the scene capture, and the
     * next segment resumes with LOADOP_LOAD so nothing is lost.  A copy pass
     * between two render passes is ordered against both, so the draw sees
     * exactly the composite that preceded it -- which is what the GS gave it
     * when it sampled the display buffer directly.
     *
     * With no such draw this is a single segment and behaves as before, so the
     * cost is one vector walk on an ordinary frame.
     */
    const size_t capture_count = repeat ? 0u : g_capture_points.size();
    if (vertices_uploaded || colours_ready || animated_vertices_ready)
    {
        /*
         * Two cursors walk together: `segment_start` over the draw queue,
         * `next_point` over the capture list.  The next capture point's draw
         * index is the next place the pass has to break; between breaks the
         * queue replays normally.  Capture points are appended in draw order
         * and several can share an index (a LocalCopyLtoL and the draw that
         * samples it, with nothing queued between), so every point at a break
         * is executed there, in order.
         */
        size_t segment_start = 0;
        size_t next_point = 0;
        for (;;)
        {
            size_t segment_end = g_draws.size();
            if (next_point < capture_count)
            {
                segment_end = std::min(g_capture_points[next_point].draw_index,
                                       g_draws.size());
            }
            if (segment_end < segment_start)
            {
                segment_end = segment_start;
            }

            /* No capture left to take means no further segment, so the
             * multisample contents are dead after this pass and the driver may
             * throw them away instead of writing them out. */
            if (msaa_on && next_point >= capture_count)
            {
                target.store_op = SDL_GPU_STOREOP_RESOLVE;
            }

            SDL_GPURenderPass *pass =
                SDL_BeginGPURenderPass(cmd, &target, 1, depth_ptr);
            if (pass != nullptr)
            {
                DrawQueuedSprites(pass, cmd, render_width, render_height,
                                  vertices_uploaded, colours_ready,
                                  animated_vertices_ready, segment_start,
                                  segment_end);
                SDL_EndGPURenderPass(pass);
            }

            /* Everything after the first pass adds to the frame rather than
             * restarting it. */
            target.load_op = SDL_GPU_LOADOP_LOAD;
            if (depth_ptr != nullptr)
            {
                depth.load_op = SDL_GPU_LOADOP_LOAD;
                depth.stencil_load_op = SDL_GPU_LOADOP_LOAD;
            }

            /* Every capture registered at this break.  Consuming at least one
             * is what guarantees progress when a point sits at segment_start
             * and the segment is empty. */
            while (next_point < capture_count &&
                   g_capture_points[next_point].draw_index <= segment_end)
            {
                RecordGsCapture(cmd, scene_texture, render_width, render_height,
                                g_capture_points[next_point]);
                next_point++;
            }

            if (segment_end >= g_draws.size() && next_point >= capture_count)
            {
                break;
            }
            segment_start = segment_end;
        }
    }
    else
    {
        /* Nothing to replay, but the frame still has to be cleared so the UI
         * pass below has a defined target -- and with MSAA on the clear still
         * has to be resolved, or the scene texture keeps the last frame. */
        if (msaa_on)
        {
            target.store_op = SDL_GPU_STOREOP_RESOLVE;
        }
        SDL_GPURenderPass *pass =
            SDL_BeginGPURenderPass(cmd, &target, 1, depth_ptr);
        if (pass != nullptr)
        {
            SDL_EndGPURenderPass(pass);
        }
        target.load_op = SDL_GPU_LOADOP_LOAD;
    }

    /* Both read the scene, not the output: a photograph and the pause still
     * are the game's own frame, and taking them here keeps them independent of
     * how the frame is later resampled -- as well as keeping the host UI out of
     * them, which is why they already sat in front of the UI pass. */
    if (!repeat)
    {
        *capture_recorded = RecordPauseScreenCapture(cmd, scene_texture,
                                                     render_width,
                                                     render_height);
        *mirror_recorded = RecordScreenMirror(cmd, scene_texture, render_width,
                                              render_height);
    }

    /*
     * Present: resample the scene onto the swapchain.  DONT_CARE because the
     * blit covers the whole surface, and the two are the same aspect by
     * construction, so there is nothing to letterbox and no previous contents
     * to preserve.
     */
    if (render_direct)
    {
        g_present_rect[0] = 0;
        g_present_rect[1] = 0;
        g_present_rect[2] = (int)swapchain_width;
        g_present_rect[3] = (int)swapchain_height;
    }
    else
    {
        /* Place the picture at its own aspect inside the output.  Equal to the
         * whole swapchain whenever the two agree, so an aspect-matched frame
         * costs nothing extra here. */
        double dst_x = 0.0;
        double dst_y = 0.0;
        double dst_w = 0.0;
        double dst_h = 0.0;
        FitAspect((double)swapchain_width, (double)swapchain_height,
                  (double)render_width / (double)render_height, &dst_x, &dst_y,
                  &dst_w, &dst_h);

        Uint32 rect_w = (Uint32)std::max(1L, std::lround(dst_w));
        Uint32 rect_h = (Uint32)std::max(1L, std::lround(dst_h));
        Uint32 rect_x = (Uint32)std::max(0L, std::lround(dst_x));
        Uint32 rect_y = (Uint32)std::max(0L, std::lround(dst_y));
        rect_w = std::min(rect_w, swapchain_width - rect_x);
        rect_h = std::min(rect_h, swapchain_height - rect_y);

        if (g_present_rect[0] != (int)rect_x ||
            g_present_rect[1] != (int)rect_y ||
            g_present_rect[2] != (int)rect_w ||
            g_present_rect[3] != (int)rect_h)
        {
            SDL_Log("MioPan SDL_GPU: presenting %ux%u at +%u+%u in %ux%u",
                    rect_w, rect_h, rect_x, rect_y, swapchain_width,
                    swapchain_height);
        }
        g_present_rect[0] = (int)rect_x;
        g_present_rect[1] = (int)rect_y;
        g_present_rect[2] = (int)rect_w;
        g_present_rect[3] = (int)rect_h;

        /* The bars.  A blit's LOADOP_CLEAR clears only its own destination
         * region, so it cannot paint them -- the area outside the picture
         * needs a pass of its own, and without it the letterbox is whatever
         * the swapchain image happened to contain, which on a recycled buffer
         * is the previous frame smeared down the sides. */
        if (rect_w != swapchain_width || rect_h != swapchain_height)
        {
            SDL_GPUColorTargetInfo bars{};
            bars.texture = output_texture;
            bars.clear_color = {0.0f, 0.0f, 0.0f, 1.0f};
            bars.load_op = SDL_GPU_LOADOP_CLEAR;
            bars.store_op = SDL_GPU_STOREOP_STORE;
            SDL_GPURenderPass *bar_pass =
                SDL_BeginGPURenderPass(cmd, &bars, 1, nullptr);
            if (bar_pass != nullptr)
            {
                SDL_EndGPURenderPass(bar_pass);
            }
        }

        SDL_GPUBlitInfo blit{};
        blit.source.texture = scene_texture;
        blit.source.w = render_width;
        blit.source.h = render_height;
        blit.destination.texture = output_texture;
        blit.destination.x = rect_x;
        blit.destination.y = rect_y;
        blit.destination.w = rect_w;
        blit.destination.h = rect_h;
        /* LOAD, not DONT_CARE: the bars were just drawn into this same image
         * and must survive the blit. */
        blit.load_op = SDL_GPU_LOADOP_LOAD;
        blit.filter = g_upscale_filter == MIOPAN_RENDER_FILTER_NEAREST
                          ? SDL_GPU_FILTER_NEAREST
                          : SDL_GPU_FILTER_LINEAR;
        blit.cycle = false;
        SDL_BlitGPUTexture(cmd, &blit);
    }

    /* The host UI draws into the composite at its native size, after the
     * resample rather than through it, so the debug overlay stays legible over
     * a 640x448 picture.  One ImGui frame feeds every present of it, so a
     * repeated present carries the overlay instead of flickering it at the
     * logical rate. */
    if (MioPanUi::PrepareDrawData(cmd))
    {
        SDL_GPUColorTargetInfo ui_target{};
        ui_target.texture = output_texture;
        ui_target.mip_level = 0;
        ui_target.layer_or_depth_plane = 0;
        ui_target.load_op = SDL_GPU_LOADOP_LOAD;
        ui_target.store_op = SDL_GPU_STOREOP_STORE;
        ui_target.cycle = false;
        SDL_GPURenderPass *ui_pass =
            SDL_BeginGPURenderPass(cmd, &ui_target, 1, nullptr);
        if (ui_pass != nullptr)
        {
            MioPanUi::RenderDrawData(cmd, ui_pass);
            SDL_EndGPURenderPass(ui_pass);
        }
    }

    /*
     * The output transform, last of all, over the finished composite.
     *
     * Only reached when the composite went somewhere other than the swapchain,
     * which is exactly the condition NeedsPresentPass() decided at the top --
     * so a default SDR session never gets here and never pays for it.
     */
    if (output_texture != swapchain_texture)
    {
        RecordPresentPass(cmd, swapchain_texture, output_texture,
                          swapchain_width, swapchain_height);
    }

    MioPan_ProfilerEndPhase(MIOPAN_PROFILE_RENDERER_RECORD, record_start);
}

/*
 * One presented frame: acquire, upload, record, submit.
 *
 * `repeat` re-presents the draw list this logical frame already uploaded --
 * everything it replays is still in the GPU buffers the upload stage filled,
 * so nothing here refills them and there is nothing to commit or release.
 *
 * `geometry_t` is where on the previous-to-this-frame interval this present
 * sits, or negative when it wants the frame exactly as it was built.  It is
 * the one thing a repeat can still have to upload: geometry smoothing moves
 * the streamed positions per present, so every present of the frame needs its
 * own copy of the vertex buffer -- the in-betweens blended, the last one the
 * frame's own.  Everything else about a repeat is unchanged.
 *
 * Returns true when a frame reached the swapchain.
 */
bool PresentFrame(bool repeat, float geometry_t = -1.0f)
{
    const uint64_t acquire_start =
        MioPan_ProfilerBeginPhase(MIOPAN_PROFILE_RENDERER_ACQUIRE);
    SDL_GPUCommandBuffer *cmd = SDL_AcquireGPUCommandBuffer(g_device);
    if (cmd == nullptr)
    {
        MioPan_ProfilerEndPhase(MIOPAN_PROFILE_RENDERER_ACQUIRE, acquire_start);
        LogSdlError("SDL_AcquireGPUCommandBuffer");
        return false;
    }

    SDL_GPUTexture *swapchain_texture = nullptr;
    Uint32 swapchain_width = 0;
    Uint32 swapchain_height = 0;
    const bool acquired = SDL_AcquireGPUSwapchainTexture(
        cmd, g_window, &swapchain_texture, &swapchain_width,
        &swapchain_height);
    MioPan_ProfilerEndPhase(MIOPAN_PROFILE_RENDERER_ACQUIRE, acquire_start);
    MioPan_ProfilerRecordSwapchainAcquire(acquired &&
                                          swapchain_texture == nullptr);

    if (!acquired)
    {
        LogSdlError("SDL_AcquireGPUSwapchainTexture");
        SDL_CancelGPUCommandBuffer(cmd);
        return false;
    }
    if (swapchain_texture == nullptr)
    {
        /* A swapchain miss is GPU backpressure, but the command buffer is
         * still usable.  Keep bounded texture and mesh-cache warming moving so
         * the first available frame does not inherit a cold upload burst.  A
         * repeated present has nothing left to warm -- this frame's upload
         * stage already ran -- so it just hands the buffer back. */
        if (repeat)
        {
            SDL_CancelGPUCommandBuffer(cmd);
        }
        else
        {
            RecordCopyOnlyUploads(cmd, "SDL_SubmitGPUCommandBuffer");
        }
        return false;
    }

    /* Left empty by a repeated present, which makes every commit and release
     * below a no-op without a second copy of the tail. */
    TextureUploadResult texture_upload;
    MeshUploadResult mesh_upload;
    SDL_GPUTransferBuffer *vertex_transfer = nullptr;
    if (!repeat)
    {
        UploadFrameResources(cmd, &texture_upload, &mesh_upload,
                             &vertex_transfer, geometry_t);
    }
    else if (geometry_t >= 0.0f && g_geo_vertices_valid)
    {
        /* The vertex-only upload a repeat can still owe.  Nothing else in the
         * upload stage is re-run: the textures, the mesh cache and the animated
         * stream all describe the logical frame and have not moved. */
        const uint64_t upload_start =
            MioPan_ProfilerBeginPhase(MIOPAN_PROFILE_RENDERER_UPLOAD);
        ApplyGeometryVertexBlend(geometry_t);
        const bool uploaded = UploadVertexBuffer(cmd, &vertex_transfer);
        MioPan_ProfilerEndPhase(MIOPAN_PROFILE_RENDERER_UPLOAD, upload_start);
        if (!uploaded)
        {
            /* Recording now would draw this frame's meshes against whatever
             * pose the previous present left in the buffer.  Drop the extra
             * instead -- one fewer in-between costs nothing. */
            SDL_CancelGPUCommandBuffer(cmd);
            if (vertex_transfer != nullptr)
            {
                SDL_ReleaseGPUTransferBuffer(g_device, vertex_transfer);
            }
            return false;
        }
    }

    bool capture_recorded = false;
    bool mirror_recorded = false;
    RecordFramePasses(cmd, swapchain_texture, swapchain_width,
                      swapchain_height, repeat, &capture_recorded,
                      &mirror_recorded);

    bool presented = false;
    std::vector<MeshCacheEntryPtr> submission_pins;
    const bool pins_ready = BuildMeshSubmissionPins(
        mesh_upload.uploaded_entries, true, &submission_pins);
    const uint64_t submit_start =
        MioPan_ProfilerBeginPhase(MIOPAN_PROFILE_RENDERER_SUBMIT);
    const bool submitted = pins_ready
        ? SubmitMeshCommandBuffer(cmd, std::move(submission_pins))
        : SDL_SubmitGPUCommandBuffer(cmd);
    if (!pins_ready)
    {
        /* Same low-memory safety fallback as the submit helper: without
         * retained entry references, do not let an arena range become reusable
         * until this command has finished. */
        SDL_WaitForGPUIdle(g_device);
    }
    MioPan_ProfilerEndPhase(MIOPAN_PROFILE_RENDERER_SUBMIT, submit_start);
    if (!submitted)
    {
        LogSdlError("SDL_SubmitGPUCommandBuffer");
        if (capture_recorded)
        {
            g_pause_capture_valid = false;
        }
    }
    else
    {
        presented = true;
        CommitTextureUploads(texture_upload);
        CommitMeshUploads(mesh_upload.uploaded_entries,
                          mesh_upload.colour_bytes, mesh_upload.animated_bytes);
        if (capture_recorded)
        {
            g_pause_capture_valid = true;
        }
        if (mirror_recorded)
        {
            g_screen_mirror_valid = true;
        }
    }
    if (vertex_transfer != nullptr)
    {
        SDL_ReleaseGPUTransferBuffer(g_device, vertex_transfer);
    }
    for (SDL_GPUTransferBuffer *transfer : texture_upload.transfers)
    {
        SDL_ReleaseGPUTransferBuffer(g_device, transfer);
    }
    for (SDL_GPUTransferBuffer *transfer : mesh_upload.transfers)
    {
        SDL_ReleaseGPUTransferBuffer(g_device, transfer);
    }
    return presented;
}

/*
 * Present this logical frame's draw list again, re-recorded through whatever
 * camera is installed now.  The frame's upload stage has to have run, which
 * means EndFrame() had a present due for it.
 */
bool RepeatPresentAt(float geometry_t)
{
    if (!EnsureRenderer() || !g_frame_upload.valid)
    {
        return false;
    }

    MioPan_ProfilerBeginRenderer();
    const bool presented = PresentFrame(true, geometry_t);
    if (presented)
    {
        /* Only the frame count.  The rest of the stats block describes the
         * logical frame and is published once, by EndFrame. */
        g_fps_present_frames++;
    }
    return presented;
}

/*
 * The camera blend, against explicit endpoints.
 *
 * MioPan_RendererBlendCameraFromPrevious() is this with the live camera as the
 * destination.  The present loop cannot use that form: its own first
 * reprojection installs an in-between as the live camera, so the frame's real
 * one has to be held aside and passed back in here.
 */
bool BlendCameraBetween(const float *from_view, const float *from_projection,
                        const float *to_view, const float *to_projection,
                        float t, float *view, float *projection)
{
    t = t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t);

    BlendViewMatrix(from_view, to_view, t, view);

    /*
     * The projection is not a rigid transform and is very nearly always
     * identical between two frames -- gra3dApplyCamera() rebuilds it from the
     * same near/far/aspect unless the FOV moved.  Element-wise is both correct
     * for a constant one and well behaved for the zooms that do happen.
     *
     * The endpoints are copied rather than computed, for the same reason
     * BlendViewMatrix() copies them: `a + (b - a) * 1.0f` is not exactly `b`
     * in float, and the present that lands on a simulation tick has to be
     * bit-identical to the undecoupled one or the seam shows every tick.
     */
    if (t <= 0.0f)
    {
        std::memcpy(projection, from_projection, 16 * sizeof(float));
    }
    else if (t >= 1.0f)
    {
        std::memcpy(projection, to_projection, 16 * sizeof(float));
    }
    else
    {
        for (int i = 0; i < 16; i++)
        {
            projection[i] = from_projection[i] +
                            (to_projection[i] - from_projection[i]) * t;
        }
    }

    return MatrixIsFinite(view) && MatrixIsFinite(projection);
}

/*
 * Every present of one logical frame.
 *
 * With smoothing off this is the single present the game has always had.  With
 * it on the frame is presented `1 + g_extra_presents` times, and the whole
 * point is which way round they go: the in-betweens come *first*, on cameras
 * blended from the previous logical frame's toward this one's, and the frame's
 * own camera goes last.  There is nothing else available -- at this point in
 * the tick the only two cameras that exist are N-1 and N, so an in-between can
 * only sit between them, and it has to be shown before N to be in order.
 *
 * The geometry moves with it when geometry smoothing is on, and stands still
 * when it is not.  Standing still is the increment-3 arrangement: the world is
 * posed for tick N in every present, so an in-between shows tick N's world half
 * a tick early rather than tick N-1's a full tick late, which costs no input
 * latency and needs no draw-list retention.  Blending it puts the world on the
 * same clock as the camera instead -- at t the world is t of the way from tick
 * N-1 to tick N, which is where the camera is -- and the two then agree about
 * what moment is being shown.  Either way the present that lands on the tick is
 * exactly tick N, so nothing is ever shown late.
 *
 * Each present but the last is followed by one CRTC field of wait.  Those
 * fields are not spent from the game's budget: vfunc() measures the logical
 * frame by draining its own V-blank semaphore and counting elapsed fields, so
 * it finds them already banked and waits correspondingly less.  The tick still
 * takes exactly the number of fields the game asked for -- 2, unless a movie
 * or the pacing harness says otherwise -- and the simulation does not speed up.
 *
 * The last blend is t = 1, which BlendViewMatrix short-circuits to a memcpy, so
 * the frame's own present is bit-identical to the unsmoothed one and nothing
 * shows at the seam.  It also leaves the true camera installed as the live one,
 * which is what the next MioPan_RendererBeginFrame() latches as "previous" --
 * an in-between must never become the frame a later blend is measured from.
 */
bool PresentLogicalFrame()
{
    /*
     * Only the fields vfunc() can be made to skip are available, and that is
     * one fewer than it waits for: after draining the banked ones it always
     * performs a final unconditional WaitSema().  At the game's usual two
     * fields exactly one is spendable -- 60 Hz presentation out of a 30 Hz
     * tick, which is the ceiling.
     *
     * Asking for more is clamped rather than obeyed.  It would not raise the
     * frame rate: the tick would simply grow by the fields it could not
     * borrow, which is the simulation slowing down.  Measured, 2 extra
     * presents took the tick from 33.34 ms to 50.00 ms -- a 20 Hz game at the
     * same 60 Hz output.
     */
    int extra = g_extra_presents;
    const int budget = MioPan_PacingLastVBlankWait() - 1;
    if (extra > budget)
    {
        extra = budget;
    }

    /* Before any present: the model blend feeds the very first reprojection,
     * and the vertex blend needs this frame's positions recorded before it
     * starts writing over them.  A frame that will not be smoothed clears the
     * snapshot rather than paying for one. */
    CaptureGeometrySnapshot(extra);

    if (extra <= 0)
    {
        return PresentFrame(false);
    }

    const int total = extra + 1;

    /* Held aside before the first reprojection replaces it. */
    float true_view[16];
    float true_projection[16];
    const bool interpolate =
        g_interpolate_presents && MioPan_RendererHavePreviousCamera() != 0;
    if (interpolate)
    {
        std::memcpy(true_view, g_3d_view, sizeof(true_view));
        std::memcpy(true_projection, g_3d_projection,
                    sizeof(true_projection));
    }

    if (interpolate)
    {
        double from_eye[3];
        double to_eye[3];
        ViewMatrixEye(g_prev_3d_view, from_eye);
        ViewMatrixEye(true_view, to_eye);
        const double dx = to_eye[0] - from_eye[0];
        const double dy = to_eye[1] - from_eye[1];
        const double dz = to_eye[2] - from_eye[2];
        g_camera_motion = (float)std::sqrt(dx * dx + dy * dy + dz * dz);
    }
    else
    {
        g_camera_motion = -1.0f;
    }

    /* Geometry follows the camera: an in-between whose world is blended but
     * whose eye is not would be worse than either, so this rides on the same
     * toggle and additionally needs a corresponding previous frame. */
    const bool blend_geometry =
        interpolate && g_interpolate_geometry && g_geo_match_count != 0;

    bool presented = false;
    for (int i = 0; i < total; i++)
    {
        const float t = (float)(i + 1) / (float)total;
        if (interpolate)
        {
            float view[16];
            float projection[16];
            if (BlendCameraBetween(g_prev_3d_view, g_prev_3d_projection,
                                   true_view, true_projection, t, view,
                                   projection))
            {
                ReprojectDrawsAt(view, projection,
                                 blend_geometry ? t : -1.0f);
            }
        }

        /* Negative asks for the frame exactly as it was built, which is what
         * every present did before geometry smoothing existed. */
        const float geometry_t = blend_geometry ? t : -1.0f;

        if (i == 0)
        {
            /* The one present that runs the whole upload stage.  A swapchain
             * miss here leaves the buffers unfilled, so there is nothing for
             * the repeats to replay. */
            presented = PresentFrame(false, geometry_t);
            if (!presented)
            {
                break;
            }
        }
        else if (!RepeatPresentAt(geometry_t))
        {
            break;
        }

        if (i + 1 < total)
        {
            const uint64_t wait_start = MioPan_ProfilerNow();
            MioPan_PacingWaitField();
            MioPan_ProfilerRecordPresentWait(MioPan_ProfilerNow() - wait_start);
        }
    }

    return presented;
}
}

extern "C" {

void MioPan_RendererGetViewExtend(float *ext_x, float *ext_y)
{
    if (ext_x != nullptr)
    {
        *ext_x = g_view_extend_x;
    }
    if (ext_y != nullptr)
    {
        *ext_y = g_view_extend_y;
    }
}

void MioPan_RendererGetViewBounds(float *x0, float *y0, float *x1, float *y1)
{
    float half_w = (float)kLogicalWidth * 0.5f;
    float half_h = (float)kLogicalHeight * 0.5f;

    if (x0 != nullptr)
    {
        *x0 = half_w - half_w * g_view_extend_x;
    }
    if (x1 != nullptr)
    {
        *x1 = half_w + half_w * g_view_extend_x;
    }
    if (y0 != nullptr)
    {
        *y0 = half_h - half_h * g_view_extend_y;
    }
    if (y1 != nullptr)
    {
        *y1 = half_h + half_h * g_view_extend_y;
    }
}

void MioPan_RendererSetPresentInterval(int interval)
{
    g_present_interval = interval < 1 ? 1 : interval;
    g_present_counter = 0;
    g_fps_window_start_counter = 0;
    g_fps_game_frames = 0;
    g_fps_present_frames = 0;
    MioPan_ProfilerReset();
}

void MioPan_RendererSetRenderResolution(int mode, float scale)
{
    if (mode != MIOPAN_RENDER_RES_NATIVE_PS2 &&
        mode != MIOPAN_RENDER_RES_WINDOW_SCALE)
    {
        mode = MIOPAN_RENDER_RES_MATCH_WINDOW;
    }
    /* Written as a negated range test on purpose: a NaN fails it, and then
     * fails both arms of the fix-up too, so it lands on 1.0 rather than
     * propagating into a texture dimension. */
    if (!(scale >= 0.25f && scale <= 8.0f))
    {
        scale = scale > 8.0f ? 8.0f : (scale < 0.25f ? 0.25f : 1.0f);
    }
    g_render_res_mode = mode;
    g_render_res_scale = scale;
}

void MioPan_RendererGetRenderResolution(int *mode, float *scale)
{
    if (mode != nullptr)
    {
        *mode = g_render_res_mode;
    }
    if (scale != nullptr)
    {
        *scale = g_render_res_scale;
    }
}

void MioPan_RendererGetRenderSize(int *width, int *height)
{
    if (width != nullptr)
    {
        *width = (int)g_render_width;
    }
    if (height != nullptr)
    {
        *height = (int)g_render_height;
    }
}

void MioPan_RendererSetAspectMode(int mode, float custom_ratio)
{
    if (mode < MIOPAN_ASPECT_AUTO || mode > MIOPAN_ASPECT_CUSTOM)
    {
        mode = MIOPAN_ASPECT_AUTO;
    }
    /* Negated range test, so a NaN ratio lands on the default rather than
     * reaching FitAspect -- which would divide by it. */
    if (!(custom_ratio >= 0.5f && custom_ratio <= 5.0f))
    {
        custom_ratio = custom_ratio > 5.0f
                           ? 5.0f
                           : (custom_ratio < 0.5f ? 0.5f : 16.0f / 9.0f);
    }
    g_aspect_mode = mode;
    g_aspect_custom = custom_ratio;
}

void MioPan_RendererGetAspectMode(int *mode, float *custom_ratio)
{
    if (mode != nullptr)
    {
        *mode = g_aspect_mode;
    }
    if (custom_ratio != nullptr)
    {
        *custom_ratio = g_aspect_custom;
    }
}

void MioPan_RendererGetPresentRect(int *x, int *y, int *width, int *height)
{
    if (x != nullptr)
    {
        *x = g_present_rect[0];
    }
    if (y != nullptr)
    {
        *y = g_present_rect[1];
    }
    if (width != nullptr)
    {
        *width = g_present_rect[2];
    }
    if (height != nullptr)
    {
        *height = g_present_rect[3];
    }
}

/* ------------------------------------------------------------------------
 *  Output transform: HDR and the display grade.
 * --------------------------------------------------------------------- */

void MioPan_RendererSetHdrMode(int mode)
{
    if (mode < MIOPAN_HDR_OFF || mode > MIOPAN_HDR_HDR10)
    {
        mode = MIOPAN_HDR_OFF;
    }
    if (mode == g_hdr_mode)
    {
        return;
    }
    g_hdr_mode = mode;

    /* Before the device exists this only records the choice -- and it is the
     * choice EnsureRenderer() reads to decide whether the scene target gets
     * its ten bits, which is why the config is applied before the window is
     * created.  Afterwards the swapchain is re-negotiated on the spot. */
    if (g_device != nullptr && g_window != nullptr)
    {
        RefreshDisplayHdrInfo();
        ApplyHdrComposition();
    }
}

int MioPan_RendererGetHdrMode(void)
{
    return g_hdr_mode;
}

int MioPan_RendererGetHdrActive(void)
{
    return HdrCompositionIsHdr(g_hdr_composition) ? 1 : 0;
}

int MioPan_RendererHdrIsAvailable(void)
{
    return g_display_hdr_enabled ? 1 : 0;
}

int MioPan_RendererHdrIsFullPrecision(void)
{
    return g_hdr_full_precision ? 1 : 0;
}

void MioPan_RendererGetHdrDisplayInfo(float *sdr_white_nits, float *peak_nits)
{
    /* Zero rather than a made-up number when the display says nothing: the UI
     * shows these beside the sliders, and "unknown" has to be tellable from
     * "80 nits". */
    const float sdr_white = g_display_hdr_enabled ? DisplaySdrWhiteNits() : 0.0f;
    if (sdr_white_nits != nullptr)
    {
        *sdr_white_nits = sdr_white;
    }
    if (peak_nits != nullptr)
    {
        *peak_nits = sdr_white * g_display_headroom;
    }
}

void MioPan_RendererGetHdrEffective(float *paper_nits, float *peak_nits)
{
    if (paper_nits != nullptr)
    {
        *paper_nits = ResolvePaperWhiteNits();
    }
    if (peak_nits != nullptr)
    {
        *peak_nits = ResolvePeakNits();
    }
}

void MioPan_RendererSetHdrPaperWhite(float nits)
{
    if (!(nits > 0.0f))
    {
        g_hdr_paper_white = 0.0f; /* follow the OS */
        return;
    }
    g_hdr_paper_white = std::min(std::max(nits, 50.0f), 1000.0f);
}

float MioPan_RendererGetHdrPaperWhite(void)
{
    return g_hdr_paper_white;
}

void MioPan_RendererSetHdrPeak(float nits)
{
    if (!(nits > 0.0f))
    {
        g_hdr_peak = 0.0f; /* follow the display's reported headroom */
        return;
    }
    g_hdr_peak = std::min(std::max(nits, 100.0f), 10000.0f);
}

float MioPan_RendererGetHdrPeak(void)
{
    return g_hdr_peak;
}

void MioPan_RendererSetHdrExpansion(float strength, float knee)
{
    g_hdr_expansion = std::min(std::max(strength, 0.0f), 1.0f);
    g_hdr_expansion_knee = std::min(std::max(knee, 0.5f), 1.0f);
}

void MioPan_RendererGetHdrExpansion(float *strength, float *knee)
{
    if (strength != nullptr)
    {
        *strength = g_hdr_expansion;
    }
    if (knee != nullptr)
    {
        *knee = g_hdr_expansion_knee;
    }
}

void MioPan_RendererSetGrade(float brightness, float contrast, float gamma,
                             float saturation)
{
    g_grade_brightness = std::min(std::max(brightness, 0.25f), 2.0f);
    g_grade_contrast = std::min(std::max(contrast, 0.5f), 2.0f);
    g_grade_gamma = std::min(std::max(gamma, 0.5f), 2.5f);
    g_grade_saturation = std::min(std::max(saturation, 0.0f), 2.0f);
}

void MioPan_RendererGetGrade(float *brightness, float *contrast, float *gamma,
                             float *saturation)
{
    if (brightness != nullptr)
    {
        *brightness = g_grade_brightness;
    }
    if (contrast != nullptr)
    {
        *contrast = g_grade_contrast;
    }
    if (gamma != nullptr)
    {
        *gamma = g_grade_gamma;
    }
    if (saturation != nullptr)
    {
        *saturation = g_grade_saturation;
    }
}

void MioPan_RendererSetWindowMode(int mode)
{
    mode = mode == MIOPAN_WINDOW_MODE_BORDERLESS
               ? MIOPAN_WINDOW_MODE_BORDERLESS
               : MIOPAN_WINDOW_MODE_WINDOWED;
    if (mode == g_window_mode)
    {
        return;
    }
    g_window_mode = mode;
    /* Before the window exists this only records the choice; EnsureRenderer()
     * applies it once there is something to apply it to. */
    ApplyWindowMode();
}

int MioPan_RendererGetWindowMode(void)
{
    return g_window_mode;
}

void MioPan_RendererGetWindowedSize(int *width, int *height)
{
    if (width != nullptr)
    {
        *width = g_windowed_width;
    }
    if (height != nullptr)
    {
        *height = g_windowed_height;
    }
}

void MioPan_RendererToggleFullscreen(void)
{
    MioPan_RendererSetWindowMode(
        g_window_mode == MIOPAN_WINDOW_MODE_BORDERLESS
            ? MIOPAN_WINDOW_MODE_WINDOWED
            : MIOPAN_WINDOW_MODE_BORDERLESS);
}

void MioPan_RendererSetUpscaleFilter(int filter)
{
    g_upscale_filter = filter == MIOPAN_RENDER_FILTER_NEAREST
                           ? MIOPAN_RENDER_FILTER_NEAREST
                           : MIOPAN_RENDER_FILTER_LINEAR;
}

int MioPan_RendererGetUpscaleFilter(void)
{
    return g_upscale_filter;
}

void MioPan_RendererSetMsaaSamples(int samples)
{
    if (samples < 1)
    {
        samples = 1;
    }
    if (samples > kMaxSampleCount)
    {
        samples = kMaxSampleCount;
    }
    /* Store what was asked for, not what is granted: a device that cannot do
     * 8x today must not have the player's choice quietly rewritten to 4x and
     * saved back that way. */
    g_msaa_request = SampleCountForSlot(SampleSlotForCount(samples));

    if (g_device == nullptr)
    {
        return;         /* EnsureRenderer() resolves it once there is a device */
    }
    ApplyMsaaSetting();
    /* Warm the new count's set here rather than letting the first frame at it
     * compile 126 pipelines mid-scene.  Slots already built are skipped, so
     * going back to a count used earlier in the session costs nothing. */
    const int previous = g_pipeline_sample_count;
    g_pipeline_sample_count = g_msaa_active;
    if (!CreatePipelines())
    {
        /* A count whose required set will not build is not usable at all.  Go
         * back to the one that was working rather than drawing nothing. */
        SDL_Log("MioPan SDL_GPU: MSAA %dx pipelines unavailable; staying at %dx",
                g_msaa_active, previous);
        g_msaa_active = previous;
        g_pipeline_sample_count = previous;
        if (previous <= 1)
        {
            ReleaseMsaaColorTexture();
        }
    }
}

int MioPan_RendererGetMsaaSamples(void)
{
    return g_msaa_request;
}

int MioPan_RendererGetMsaaActiveSamples(void)
{
    return g_msaa_active;
}

int MioPan_RendererGetMsaaMaxSamples(void)
{
    return g_msaa_max_supported;
}

void MioPan_RendererSetAnisotropy(int max_anisotropy)
{
    if (max_anisotropy < 1)
    {
        max_anisotropy = 1;
    }
    if (max_anisotropy > kMaxAnisotropy)
    {
        max_anisotropy = kMaxAnisotropy;
    }
    /* Round down to a power of two, the only levels the sampler table holds. */
    int level = 1;
    while (level * 2 <= max_anisotropy)
    {
        level *= 2;
    }
    g_anisotropy_request = level;

    if (g_device == nullptr)
    {
        return;
    }
    ApplyAnisotropySetting();
}

int MioPan_RendererGetAnisotropy(void)
{
    return g_anisotropy_request;
}

int MioPan_RendererGetAnisotropyActive(void)
{
    return g_anisotropy_active;
}

int MioPan_RendererGetAnisotropyMax(void)
{
    return g_anisotropy_max_supported;
}

int MioPan_RendererGetLightingMode(void)
{
    InitLightingModeFromEnvironment();
    return g_lighting_mode;
}

/* Project a world point with the frame's camera, answer from the previous
 * frame's read-back, and register the pixel for the next one.  See the block
 * comment on the probe machinery above for why the comparison happens here
 * rather than through a mirror of GS memory. */
int MioPan_RendererQueryPointOccluded(int slot, const float *world_pos)
{
    if (slot < 0 || slot >= MIOPAN_DEPTH_PROBE_SLOTS || world_pos == nullptr ||
        !g_depth_probe_supported || !g_3d_camera_valid ||
        g_depth_width == 0 || g_depth_height == 0)
    {
        return MIOPAN_DEPTH_PROBE_UNAVAILABLE;
    }
    const int depth_w = (int)g_depth_width;
    const int depth_h = (int)g_depth_height;

    float clip[4];
    const float local[4] = {world_pos[0], world_pos[1], world_pos[2], 1.0f};
    ApplyMatrixRowVector(clip, local, g_3d_view_projection);
    if (!std::isfinite(clip[3]) || clip[3] <= 0.0f)
    {
        return MIOPAN_DEPTH_PROBE_UNAVAILABLE;      /* behind the eye */
    }

    const float inv_w = 1.0f / clip[3];
    const float ndc_x = clip[0] * inv_w;
    const float ndc_y = clip[1] * inv_w;
    const float depth = clip[2] * inv_w;
    if (!std::isfinite(ndc_x) || !std::isfinite(ndc_y) ||
        !std::isfinite(depth) ||
        ndc_x < -1.0f || ndc_x > 1.0f || ndc_y < -1.0f || ndc_y > 1.0f ||
        depth < 0.0f || depth > 1.0f)
    {
        return MIOPAN_DEPTH_PROBE_UNAVAILABLE;      /* off screen */
    }

    /* Render-target convention: NDC +Y is up, pixel Y counts down.  Same flip
     * shadow_receiver.frag applies to its own projected coordinate. */
    int px = (int)((ndc_x * 0.5f + 0.5f) * (float)depth_w);
    int py = (int)((0.5f - ndc_y * 0.5f) * (float)depth_h);
    px = std::min(std::max(px, 0), depth_w - 1);
    py = std::min(std::max(py, 0), depth_h - 1);

    DepthProbe &probe = g_depth_probes[slot];
    const bool had_answer = probe.read_valid;
    const float stored = probe.read_depth;

    probe.x = px;
    probe.y = py;
    probe.depth = depth;
    probe.posted = true;

    if (!had_answer)
    {
        return MIOPAN_DEPTH_PROBE_UNAVAILABLE;
    }

    /* Reversed Z: larger is nearer, which is the same sense the PS2's Z had --
     * so this is the ROM's own `stored >= point` test, in the host's units.
     * The epsilon is against a point sitting exactly on the surface it is
     * being tested against, where float depth would otherwise flicker; the
     * ROM's integer buffer had no such case. */
    constexpr float kDepthProbeEpsilon = 1.0e-6f;
    return stored > depth + kDepthProbeEpsilon ? 1 : 0;
}

int MioPan_RendererGetShadowFilter(void)
{
    return g_shadow_filter;
}

void MioPan_RendererSetShadowFilter(int filter)
{
    g_shadow_filter = filter == MIOPAN_SHADOW_FILTER_SOFT
        ? MIOPAN_SHADOW_FILTER_SOFT : MIOPAN_SHADOW_FILTER_NONE;
}

void MioPan_RendererSetLightingMode(int mode)
{
    g_lighting_mode_initialized = true;
    g_lighting_mode = (mode == MIOPAN_LIGHTING_FRAGMENT ||
                       mode == MIOPAN_LIGHTING_FRAGMENT_ALL)
        ? mode : MIOPAN_LIGHTING_VERTEX;
}

int MioPan_RendererGetAnimatedLightingBackend(void)
{
    return g_animated_lighting_backend;
}

void MioPan_RendererSetAnimatedLightingBackend(int backend)
{
    g_animated_lighting_backend =
        backend == MIOPAN_ANIMATED_LIGHTING_CPU
            ? MIOPAN_ANIMATED_LIGHTING_CPU
            : MIOPAN_ANIMATED_LIGHTING_GPU;
}

unsigned int MioPan_RendererGetDebugViewFlags(void)
{
    unsigned int flags = 0;
    flags |= g_dbg_wireframe ? MIOPAN_RENDERER_DEBUG_WIREFRAME : 0;
    flags |= g_dbg_nodepth ? MIOPAN_RENDERER_DEBUG_DISABLE_DEPTH : 0;
    flags |= g_dbg_flatcolor ? MIOPAN_RENDERER_DEBUG_FLAT_COLOUR : 0;
    flags |= g_dbg_nolighting ? MIOPAN_RENDERER_DEBUG_DISABLE_LIGHTING : 0;
    flags |= g_dbg_disable_billboard_host
        ? MIOPAN_RENDERER_DEBUG_DISABLE_BILLBOARD_HOST : 0;
    flags |= g_dbg_skip_billboard_legacy_packets
        ? MIOPAN_RENDERER_DEBUG_SKIP_BILLBOARD_LEGACY_PACKETS : 0;
    flags |= g_dbg_disable_sky_dome
        ? MIOPAN_RENDERER_DEBUG_DISABLE_SKY_DOME : 0;
    flags |= g_dbg_disable_sky_horizon
        ? MIOPAN_RENDERER_DEBUG_DISABLE_SKY_HORIZON : 0;
    flags |= g_dbg_no_2d_depth
        ? MIOPAN_RENDERER_DEBUG_DISABLE_2D_DEPTH : 0;
    flags |= g_dbg_shadow_view
        ? MIOPAN_RENDERER_DEBUG_SHADOW_VIEW : 0;
    return flags;
}

void MioPan_RendererSetDebugViewFlags(unsigned int flags)
{
    g_dbg_shadow_view = (flags & MIOPAN_RENDERER_DEBUG_SHADOW_VIEW) != 0;
    g_dbg_wireframe = (flags & MIOPAN_RENDERER_DEBUG_WIREFRAME) != 0;
    g_dbg_nodepth = (flags & MIOPAN_RENDERER_DEBUG_DISABLE_DEPTH) != 0;
    g_dbg_flatcolor = (flags & MIOPAN_RENDERER_DEBUG_FLAT_COLOUR) != 0;
    g_dbg_nolighting =
        (flags & MIOPAN_RENDERER_DEBUG_DISABLE_LIGHTING) != 0;
    g_dbg_disable_billboard_host =
        (flags & MIOPAN_RENDERER_DEBUG_DISABLE_BILLBOARD_HOST) != 0;
    g_dbg_skip_billboard_legacy_packets =
        (flags & MIOPAN_RENDERER_DEBUG_SKIP_BILLBOARD_LEGACY_PACKETS) != 0;
    g_dbg_disable_sky_dome =
        (flags & MIOPAN_RENDERER_DEBUG_DISABLE_SKY_DOME) != 0;
    g_dbg_disable_sky_horizon =
        (flags & MIOPAN_RENDERER_DEBUG_DISABLE_SKY_HORIZON) != 0;
    g_dbg_no_2d_depth =
        (flags & MIOPAN_RENDERER_DEBUG_DISABLE_2D_DEPTH) != 0;
}

void MioPan_RendererSetFontTexture(int bank, const sceGsTex0 *tex0)
{
    g_font_texture_selects++;

    if (bank < 0 || bank >= kFontTextureBankCount || tex0 == nullptr ||
        !EnsureRenderer())
    {
        g_current_font_texture = nullptr;
        g_current_font_bank = -1;
        return;
    }

    g_current_font_bank = bank;
    g_current_font_texture = GetFontTexture(bank, tex0);
}

void MioPan_RendererBeginFrame(void)
{
    if (!EnsureRenderer())
    {
        return;
    }

    /*
     * Latch the camera the last logical frame ended on, before the game
     * installs this frame's.  Shadow rendering swaps the camera mid-frame but
     * always restores it, so what is live here is the main eye.
     */
    if (g_3d_camera_valid)
    {
        std::memcpy(g_prev_3d_view, g_3d_view, sizeof(g_prev_3d_view));
        std::memcpy(g_prev_3d_projection, g_3d_projection,
                    sizeof(g_prev_3d_projection));
        g_prev_3d_camera_valid = true;
    }

    MioPan_ProfilerBeginFrame();
    PumpEvents();
    /* After PumpEvents, so an HDR state change the pump just read is acted on
     * this frame, and before anything acquires a swapchain texture -- the
     * composition cannot be re-negotiated with one in flight. */
    ApplyHdrComposition();
    UpdateViewExtend();
    MioPanUi::BeginFrame();
    /* Drop last frame's pins before trimming the persistent cache. */
    CollectInFlightMeshSubmissions();
    AbortActiveMeshStream();
    g_draws.clear();
    /* The buffers a repeated present replays are about to be refilled. */
    g_frame_upload = FrameUploadState{};
    /* Episodes index into g_draws, so they cannot outlive it.  gra3dShadow.c
     * clears them too (via gra3dshadowClearProjectModel), but only while the
     * game is in a map -- this bounds them everywhere else. */
    g_shadow_episodes.clear();
    g_shadow_current_episode = -1;
    g_shadow_caster_depth = 0;
    g_shadow_receiver_depth = 0;
    g_shadow_valid = false;
    g_shadow_stat_casters = 0;
    g_shadow_stat_receivers = 0;
    g_vertices.clear();
    g_mesh_colours.clear();
    g_animated_mesh_vertices_stream.clear();
    g_vertex_light_states.clear();
    g_fragment_light_states.clear();
    /* Take last frame's read-back before anything queries a probe, and drop
     * the postings so a probe whose caller stopped asking stops being
     * downloaded for. */
    ResolveDepthProbes();
    ClearDepthProbePostings();
    for (DrawSourceFrameMetrics &metrics : g_draw_source_metrics)
    {
        metrics = {};
    }
    g_scene_capture_count = 0;
    g_capture_points.clear();
    g_frame_index++;
    g_mesh_cache_frame++;
    /* Generation invalidation makes old retry keys unreachable.  Their
     * expiry queue retires them without putting a full retry-map scan back on
     * the room-transition path; cap maintenance work per logical frame. */
    for (unsigned int reclaimed = 0;
         reclaimed < kMeshArenaEvictionBudgetEntries &&
         !g_mesh_cache_retry_expiries.empty();)
    {
        const MeshCacheRetryExpiry expiry =
            g_mesh_cache_retry_expiries.front();
        if (expiry.retry_frame > g_mesh_cache_frame)
        {
            break;
        }
        g_mesh_cache_retry_expiries.pop_front();
        auto retry = g_mesh_cache_retry_after.find(expiry.key);
        if (retry != g_mesh_cache_retry_after.end() &&
            retry->second == expiry.retry_frame)
        {
            g_mesh_cache_retry_after.erase(retry);
        }
        reclaimed++;
    }
    g_mesh_cache_build_entries = 0;
    g_mesh_cache_build_bytes = 0;
    g_mesh_cache_build_start = 0;
    g_mesh_cache_build_reservations.clear();
    g_mesh_cache_reserved_bytes = 0;
    g_mesh_arena_evictions_this_frame = 0;
    TrimMeshCache();
    g_frame_active = true;
    CountGameFrame();
}

void MioPan_RendererEndFrame(void)
{
    bool presented;
    bool present_due;

    if (!EnsureRenderer())
    {
        return;
    }

    MioPan_ProfilerBeginRenderer();

    /* A malformed decoder path must not leave an unpublished vertex tail in
     * the frame stream.  Normal mesh scopes always commit before this point. */
    AbortActiveMeshStream();
    PumpEvents();
    MioPanUi::Draw();
    MioPanUi::EndFrame();
    presented = false;
    present_due = g_frame_active && ShouldPresentFrame();
    if (g_frame_active && !present_due && !g_pause_capture_pending &&
        (!g_pending_texture_uploads.empty() ||
         HasPendingMeshCacheEntries()))
    {
        /* Presentation interval 2 intentionally skips every other render, but
         * cold texture pixels and immutable mesh topology need not wait with
         * it.  Submit a bounded copy-only command now so cache warming keeps
         * pace with logical frames instead of accumulating for the next
         * presented one. */
        const uint64_t acquire_start =
            MioPan_ProfilerBeginPhase(MIOPAN_PROFILE_RENDERER_ACQUIRE);
        SDL_GPUCommandBuffer *upload_cmd =
            SDL_AcquireGPUCommandBuffer(g_device);
        MioPan_ProfilerEndPhase(MIOPAN_PROFILE_RENDERER_ACQUIRE,
                                acquire_start);
        if (upload_cmd == nullptr)
        {
            LogSdlError("SDL_AcquireGPUCommandBuffer(texture batch)");
        }
        else
        {
            RecordCopyOnlyUploads(upload_cmd,
                                  "SDL_SubmitGPUCommandBuffer(texture batch)");
        }
    }
    if (g_frame_active && (present_due || g_pause_capture_pending))
    {
        presented = PresentLogicalFrame();
    }
    /* After the frame's own submit: the render pass has ended, so a copy pass
     * may read the depth texture.  Its own command buffer, so the present path
     * keeps its plain submit and needs no fence of its own. */
    if (presented)
    {
        RecordDepthProbeDownload();
    }
    g_frame_active = false;
    /* This tick's geometry becomes the next one's "previous", or the pair is
     * cleared if the snapshot never completed -- pairing tick N+1 with tick
     * N-1 would blend across a frame that was never shown. */
    FinishGeometrySnapshot();
    FlushDrawSourceMetricsToProfiler();
    PublishStutterFrameNote();
    MioPan_ProfilerEndFrame();
    UpdatePerformanceStats(presented);
    MioPan_GsResetFrameMetrics();
}

/*
 * Present this logical frame's draw list again, re-recorded through whatever
 * camera is installed now -- MioPan_RendererReprojectDraws() rewrites the mvp
 * of every eligible draw, and this puts the result on screen without the game
 * having simulated another tick.
 *
 * The frame's upload stage has to have run, which means EndFrame() had a
 * present due for it: everything replayed here is already in GPU buffers and
 * nothing in this path refills them.  Returns non-zero if a frame reached the
 * swapchain.
 */
void MioPan_RendererSetExtraPresents(int extra)
{
    g_extra_presents = extra < 0 ? 0 : (extra > 3 ? 3 : extra);
}

int MioPan_RendererGetExtraPresents(void)
{
    return g_extra_presents;
}

void MioPan_RendererSetPresentInterpolation(int enable)
{
    g_interpolate_presents = enable != 0;
}

int MioPan_RendererGetPresentInterpolation(void)
{
    return g_interpolate_presents ? 1 : 0;
}

float MioPan_RendererGetCameraMotion(void)
{
    return g_camera_motion;
}

void MioPan_RendererSetGeometryInterpolation(int enable)
{
    g_interpolate_geometry = enable != 0;
    if (!g_interpolate_geometry)
    {
        /* Stop paying for a snapshot the moment it stops being wanted, and do
         * not leave a stale one to be blended against if it is turned back on
         * several ticks later. */
        InvalidateGeometrySnapshot();
        g_geo_prev_draws.clear();
        g_geo_prev_positions.clear();
        g_geo_curr_draws.clear();
        g_geo_curr_positions.clear();
    }
}

int MioPan_RendererGetGeometryInterpolation(void)
{
    return g_interpolate_geometry ? 1 : 0;
}

float MioPan_RendererGetGeometryCoverage(void)
{
    return g_geometry_coverage;
}

int MioPan_RendererGetMaxExtraPresents(void)
{
    const int budget = MioPan_PacingLastVBlankWait() - 1;
    if (budget < 0)
    {
        return 0;
    }
    return budget > 3 ? 3 : budget;
}

int MioPan_RendererRepeatPresent(void)
{
    /* The frame exactly as it was built.  The present loop uses the internal
     * form, which can additionally hand the repeat a blended vertex buffer. */
    return RepeatPresentAt(-1.0f) ? 1 : 0;
}

/*
 * The host side of a GS local-to-local image copy (g2d_draw.c's
 * LocalCopyLtoL / LocalCopyBtoLAdrs).
 *
 * `dst_addr` is the GS block the game will later sample as a texture, and
 * `logical_w/h` the size the PS2 copy produces -- scpw[type]'s destination
 * rect.  The capture is recorded at this point in the draw queue, so it holds
 * the frame exactly as the GS would have had it when the copy ran.
 *
 * `src_addr` is either MIOPAN_GS_CAPTURE_LIVE for a copy out of the frame
 * buffer, or another block previously captured -- which is how mechanism C's
 * EE round-trip (LocalCopyLtoB, halve on the EE, LocalCopyBtoL) is served
 * without a GPU readback.
 *
 * PORT DEVIATION: that round-trip's EffImageHalf32() still runs, on EE memory
 * nothing here reads.  Its effect on the picture -- a half-width image -- is
 * reproduced by the slot's own size instead, so the result matches while the
 * readback it would have needed is skipped.
 */
void MioPan_RendererCaptureGsBlock(unsigned int dst_addr,
                                   unsigned int src_addr,
                                   int logical_w, int logical_h)
{
    if (!EnsureRenderer() || !g_frame_active)
    {
        return;
    }

    int dst_slot = DeclareCaptureSlot(dst_addr, logical_w, logical_h);
    if (dst_slot < 0)
    {
        return;
    }

    /* A slot still waiting on its texture cannot be captured into yet; the
     * declaration above is what gets it allocated for the next frame. */
    if (g_capture_slots[(size_t)dst_slot].texture == nullptr)
    {
        return;
    }

    /* A source block must already exist -- the LocalCopyLtoB that filled it
     * ran earlier.  Its size is whatever that copy declared, which is what
     * makes the scaling blit stand in for EffImageHalf32(). */
    int src_slot = -1;
    if (src_addr != kCaptureSrcLive)
    {
        for (size_t i = 0; i < g_capture_slots.size(); i++)
        {
            if (g_capture_slots[i].addr == src_addr &&
                g_capture_slots[i].texture != nullptr &&
                g_capture_slots[i].valid)
            {
                src_slot = (int)i;
                break;
            }
        }
        if (src_slot < 0)
        {
            return;
        }
    }

    CapturePoint point{};
    point.draw_index = g_draws.size();
    point.dst_slot = dst_slot;
    point.src_slot = src_slot;
    g_capture_points.push_back(point);
    /* Valid from the moment the capture is queued -- see the note in
     * DrawTexturedQuadImpl.  Both the destination texture and, for a slot to
     * slot copy, the source's validity were checked above, so the recorded
     * copy cannot fail after this point. */
    g_capture_slots[(size_t)dst_slot].valid = true;
}

void MioPan_RendererCaptureScreen(unsigned int addr)
{
    if (!EnsureRenderer())
    {
        return;
    }

    g_pause_capture_addr = addr;
    g_pause_capture_valid = false;
    g_pause_capture_pending = true;
}

/*
 * Read the last presented frame back into EE memory, as PS2 PSMCT32 words.
 *
 * This is the host half of a GS LOCAL->HOST store (g2d_draw.c's
 * LocalCopyLtoBAdrs).  On hardware the GS reads its own local memory back over
 * the bus; here nothing has ever drawn into emulated GS memory, so the store
 * would hand the caller whatever texture data happened to share the page --
 * which is what a saved photograph used to be.
 *
 * `src_*` is a rectangle in the ROM's own 640x448 frame coordinates, and it is
 * box-filtered down into a `dst_w` x `dst_h` image `dst_pitch` pixels to the
 * row.  That covers every shape the LocalCopy types ask for: the 2:1 vertical
 * squash a picture is stored at is a 640x448 source into a 640x224
 * destination, and the album thumbnail is the photo window into a 45x15 corner
 * of a 640-wide page.
 *
 * Synchronous, and deliberately so: the caller compresses the result in the
 * same call.  One stall per photograph, against a frame the GPU finished long
 * ago, so the fence is already signalled in practice.
 */
int MioPan_RendererReadbackScreen(unsigned char *dst, int dst_pitch,
                                  int src_x, int src_y, int src_w, int src_h,
                                  int dst_w, int dst_h)
{
    if (dst_pitch <= 0)
    {
        dst_pitch = dst_w;
    }
    if (dst == nullptr || dst_w <= 0 || dst_h <= 0 || dst_pitch < dst_w ||
        src_w <= 0 ||
        src_h <= 0 || !EnsureRenderer() || !g_screen_mirror_valid ||
        g_screen_mirror_texture == nullptr || g_screen_mirror_width == 0 ||
        g_screen_mirror_height == 0)
    {
        return 0;
    }

    const Uint32 mirror_w = g_screen_mirror_width;
    const Uint32 mirror_h = g_screen_mirror_height;
    const Uint32 needed = mirror_w * mirror_h * 4;

    if (g_screen_mirror_download != nullptr &&
        g_screen_mirror_download_size < needed)
    {
        SDL_ReleaseGPUTransferBuffer(g_device, g_screen_mirror_download);
        g_screen_mirror_download = nullptr;
        g_screen_mirror_download_size = 0;
    }
    if (g_screen_mirror_download == nullptr)
    {
        SDL_GPUTransferBufferCreateInfo info{};
        info.usage = SDL_GPU_TRANSFERBUFFERUSAGE_DOWNLOAD;
        info.size = needed;
        g_screen_mirror_download = SDL_CreateGPUTransferBuffer(g_device, &info);
        if (g_screen_mirror_download == nullptr)
        {
            LogSdlError("SDL_CreateGPUTransferBuffer(screen readback)");
            return 0;
        }
        g_screen_mirror_download_size = needed;
    }

    SDL_GPUCommandBuffer *cmd = SDL_AcquireGPUCommandBuffer(g_device);
    if (cmd == nullptr)
    {
        LogSdlError("SDL_AcquireGPUCommandBuffer(screen readback)");
        return 0;
    }

    SDL_GPUCopyPass *copy = SDL_BeginGPUCopyPass(cmd);
    if (copy == nullptr)
    {
        LogSdlError("SDL_BeginGPUCopyPass(screen readback)");
        SDL_CancelGPUCommandBuffer(cmd);
        return 0;
    }

    SDL_GPUTextureRegion region{};
    region.texture = g_screen_mirror_texture;
    region.w = mirror_w;
    region.h = mirror_h;
    region.d = 1;

    SDL_GPUTextureTransferInfo transfer{};
    transfer.transfer_buffer = g_screen_mirror_download;
    transfer.pixels_per_row = mirror_w;
    transfer.rows_per_layer = mirror_h;

    SDL_DownloadFromGPUTexture(copy, &region, &transfer);
    SDL_EndGPUCopyPass(copy);

    SDL_GPUFence *fence = SDL_SubmitGPUCommandBufferAndAcquireFence(cmd);
    if (fence == nullptr)
    {
        LogSdlError("SDL_SubmitGPUCommandBufferAndAcquireFence(screen readback)");
        return 0;
    }
    SDL_WaitForGPUFences(g_device, true, &fence, 1);
    SDL_ReleaseGPUFence(g_device, fence);

    const unsigned char *pixels = (const unsigned char *)SDL_MapGPUTransferBuffer(
        g_device, g_screen_mirror_download, false);
    if (pixels == nullptr)
    {
        LogSdlError("SDL_MapGPUTransferBuffer(screen readback)");
        return 0;
    }

    /* Both sides are R, G, B, A in that order now: the mirror is created as
     * kScreenMirrorFormat rather than inheriting whichever channel order the
     * backend gave the swapchain, and the EE buffer is always PSMCT32.  The
     * indices are kept as named constants because the loop below reads three
     * of the four channels by name and a bare 0/2 there reads as a typo. */
    const int ir = 0;
    const int ib = 2;

    for (int dy = 0; dy < dst_h; dy++)
    {
        int y0 = src_y + (int)((long long)dy * src_h / dst_h);
        int y1 = src_y + (int)((long long)(dy + 1) * src_h / dst_h);
        if (y1 <= y0)
        {
            y1 = y0 + 1;
        }
        y0 = std::max(0, std::min(y0, (int)mirror_h - 1));
        y1 = std::max(y0 + 1, std::min(y1, (int)mirror_h));

        for (int dx = 0; dx < dst_w; dx++)
        {
            int x0 = src_x + (int)((long long)dx * src_w / dst_w);
            int x1 = src_x + (int)((long long)(dx + 1) * src_w / dst_w);
            if (x1 <= x0)
            {
                x1 = x0 + 1;
            }
            x0 = std::max(0, std::min(x0, (int)mirror_w - 1));
            x1 = std::max(x0 + 1, std::min(x1, (int)mirror_w));

            unsigned int r = 0;
            unsigned int g = 0;
            unsigned int b = 0;
            unsigned int n = 0;
            for (int y = y0; y < y1; y++)
            {
                const unsigned char *row =
                    pixels + ((size_t)y * mirror_w + (size_t)x0) * 4;
                for (int x = x0; x < x1; x++, row += 4)
                {
                    r += row[ir];
                    g += row[1];
                    b += row[ib];
                    n++;
                }
            }

            unsigned char *out =
                dst + ((size_t)dy * (size_t)dst_pitch + (size_t)dx) * 4;
            out[0] = (unsigned char)(r / n);
            out[1] = (unsigned char)(g / n);
            out[2] = (unsigned char)(b / n);
            /* PSMCT32 alpha on the PS2 scale: 0x80 is opaque. */
            out[3] = 0x80;
        }
    }

    SDL_UnmapGPUTransferBuffer(g_device, g_screen_mirror_download);
    return 1;
}

void MioPan_RendererDrawCapturedScreen(unsigned int addr,
                                       unsigned char r,
                                       unsigned char g,
                                       unsigned char b,
                                       unsigned char a)
{
    SpriteVertex vertices[4];
    float xy[8];
    float uv[8];

    if (!g_frame_active || !g_pause_capture_valid ||
        addr != g_pause_capture_addr || g_pause_capture_texture == nullptr ||
        g_pause_capture_width == 0 || g_pause_capture_height == 0)
    {
        return;
    }

    xy[0] = 0.0f;
    xy[1] = 0.0f;
    xy[2] = (float)kLogicalWidth;
    xy[3] = 0.0f;
    xy[4] = 0.0f;
    xy[5] = (float)kLogicalHeight;
    xy[6] = (float)kLogicalWidth;
    xy[7] = (float)kLogicalHeight;
    uv[0] = 0.0f;
    uv[1] = 0.0f;
    uv[2] = (float)g_pause_capture_width;
    uv[3] = 0.0f;
    uv[4] = 0.0f;
    uv[5] = (float)g_pause_capture_height;
    uv[6] = (float)g_pause_capture_width;
    uv[7] = (float)g_pause_capture_height;

    BuildQuadVertices(vertices, xy, uv,
                      (float)g_pause_capture_width,
                      (float)g_pause_capture_height,
                      Ps2ColorToFloat(r), Ps2ColorToFloat(g),
                      Ps2ColorToFloat(b), Ps2AlphaToFloat(a));
    QueueQuad(g_pause_capture_texture, (int)g_pause_capture_width,
              (int)g_pause_capture_height, vertices, false);
}

void MioPan_RendererDrawFontQuad(const float *xy,
                                 const float *uv,
                                 unsigned char r,
                                 unsigned char g,
                                 unsigned char b,
                                 unsigned char a)
{
    SpriteVertex vertices[4];
    TextureEntry *entry;

    if (xy == nullptr || uv == nullptr || !g_frame_active)
    {
        return;
    }

    entry = g_current_font_texture;
    if (entry == nullptr && g_current_font_bank >= 0 &&
        g_current_font_bank < kFontTextureBankCount &&
        g_font_textures[g_current_font_bank].valid)
    {
        entry = g_font_textures[g_current_font_bank].texture;
        g_current_font_texture = entry;
    }

    if (entry == nullptr || entry->texture == nullptr)
    {
        return;
    }

    BuildQuadVertices(vertices, xy, uv, (float)entry->width,
                      (float)entry->height, Ps2ColorToFloat(r),
                      Ps2ColorToFloat(g), Ps2ColorToFloat(b),
                      Ps2AlphaToFloat(a));
    /* SetFontEnv uses TEX1 MMAG=0, MMIN=5: nearest when enlarged, linear
     * within a minified mip level.  The host texture has only level zero. */
    QueueQuad(entry->texture, entry->width, entry->height, vertices,
              true, false, true, false);
}

/*
 * Is this TEX0 pointing at one of the two GS display buffers?
 *
 * The frame buffers are 640x448 PSMCT24 at blocks 0x0000 and 0x1180, so
 * together they occupy 0x0000..0x2300 -- and the Z buffer takes 0x2300..0x2bc0
 * straight after, which is why the first address any effect scratch page can
 * use is 0x2bc0.  Nothing else can live below that: it would be overwritten
 * every frame.  So "TBP0 below the Z buffer, at the frame buffer's own stride"
 * identifies a frame-buffer read with no risk of catching a real texture.
 *
 * effect_scr.c's SubBlur / SubFocus / SubContrast2 / SetOverRap /
 * SetForcusDepth are the callers, via DispSprD2().
 */
bool IsFramebufferTex0(const sceGsTex0 *tex0)
{
    if (tex0 == nullptr)
    {
        return false;
    }

    if (tex0->TBW != 10)                    /* 10 * 64 = 640 px stride */
    {
        return false;
    }

    if (tex0->PSM != MioPan::GS::PSMCT32 && tex0->PSM != MioPan::GS::PSMCT24)
    {
        return false;
    }

    return tex0->TBP0 == 0x0000 || tex0->TBP0 == 0x1180;
}

/*
 * Is this TEX0 reading one of the effect scratch pages a LocalCopy* filled?
 *
 * The slot has to have been declared by the copy that fills it, so an address
 * nothing has captured into reads as an ordinary texture and takes the normal
 * path.  The width test is what keeps ScreenSaverDraw() out: it samples
 * 0x2bc0 -- the same block the deform scratch uses -- as a 256 px PSMT8 image
 * with its own CLUT, at a time when no effect owns the page.  Matching TBW
 * against the slot's own logical width separates the two without hardcoding
 * either.
 */
/*
 * Does this TEX0 name a declared capture slot, ready or not?
 *
 * A capture TEX0 whose slot is not up yet must not fall through to the ordinary
 * texture path: nothing wrote that GS address, so GetTexture() would hash and
 * decode hundreds of KB of unrelated VRAM as if it were an image -- the garbage
 * an effect used to draw on its first appearance.
 *
 * Matched on address *and* width, not address alone.  The game reuses a page
 * for unrelated things: ScreenSaverDraw() samples 0x2bc0 -- the deform
 * scratch's address -- as a 256 px PSMT8 image with its own CLUT, and that is a
 * real texture that must still reach GetTexture(). Only the readiness checks
 * separate this from CaptureSlotForTex0().
 */
bool IsCaptureSlotAddr(const sceGsTex0 *tex0)
{
    if (tex0 == nullptr)
    {
        return false;
    }
    if (tex0->PSM != MioPan::GS::PSMCT32 && tex0->PSM != MioPan::GS::PSMCT24)
    {
        return false;
    }
    for (const CaptureSlot &slot : g_capture_slots)
    {
        if (slot.addr == tex0->TBP0 &&
            slot.logical_w == (int)tex0->TBW * 64)
        {
            return true;
        }
    }
    return false;
}

int CaptureSlotForTex0(const sceGsTex0 *tex0)
{
    if (tex0 == nullptr)
    {
        return -1;
    }

    if (tex0->PSM != MioPan::GS::PSMCT32 && tex0->PSM != MioPan::GS::PSMCT24)
    {
        return -1;
    }

    for (size_t i = 0; i < g_capture_slots.size(); i++)
    {
        const CaptureSlot &slot = g_capture_slots[i];
        if (slot.addr == tex0->TBP0 && slot.texture != nullptr && slot.valid &&
            slot.logical_w == (int)tex0->TBW * 64)
        {
            return (int)i;
        }
    }
    return -1;
}

/* GS CLAMP with WMS = WMT = CLAMP, which is CopySprDToSpr2()'s default and
 * what every bridge without a register of its own has always used. */
static const uint64_t kClampBothAxes = 5;

/*
 * A 2D primitive's GS Z, in the clip convention BuildQuadVertices() wants.
 *
 * The engine's depth buffer is PSMZ16S -- every ZBUF it writes is
 * 0x..0a000118 -- so the GS compared the low 16 bits of the primitive's Z, and
 * CopySprDToSpr derives that Z as 0xfffff - pri.  Every 2D draw in the game
 * therefore lands in the top 256 of the 16-bit range, above anything the 3D
 * pass writes, which is exactly why the 2D layer could be drawn over the world
 * without testing depth at all.  Dividing the same 16 bits by 0xffff puts the
 * band at 0.996..1.0 of the depth range, with a whole priority step ~1.5e-5
 * apart -- four orders of magnitude above a D32_FLOAT ULP up there -- and a 3D
 * fragment can only reach it by sitting on the near plane.
 *
 * The value handed to the vertex is NOT that, though: sprite.vert runs every
 * position through MikuPanFixClipZ(), `clip.z = clip.w/2 - clip.z/2`, so what
 * goes in is the engine's symmetric clip convention with near at -1 and far at
 * +1.  Writing the reversed-Z figure straight in inverts the whole ordering,
 * which flips a depth wedge from masking the rectangle to masking everything
 * except it -- the photo phase's negative pass then fails on every pixel
 * instead of on the picture alone.
 */
static float Gs2dZToNdc(unsigned int gs_z)
{
    const float reversed = (float)(gs_z & 0xffffu) * (1.0f / 65535.0f);

    return 1.0f - 2.0f * reversed;
}

/*
 * Whether a 2D quad needs the depth-stencil stage at all, and with what.
 *
 * ZTST ALWAYS with ZMSK set -- what CopySprDToSpr and CopySqrDToSqr write into
 * every primitive unless the caller overrides them -- neither reads nor writes
 * depth, so it keeps using the depth-off pipeline it always did.  Only the
 * handful of passes that deliberately override one of the two registers pay
 * for a depth-tested pipeline.
 */
static bool Gs2dDepthStageNeeded()
{
    if (g_dbg_no_2d_depth)
    {
        return false;
    }
    return g_gs_ztst != GS_DEPTH_ALWAYS || g_gs_depth_write;
}

/* One 2D primitive's depth state, taken and cleared.  Called at the top of
 * every quad entry point, before any early-out, so an armed Z cannot survive
 * into an unrelated draw. */
struct Gs2dDepth
{
    float ndc_z;
    bool test;
    GsDepthCompare compare;
};

static Gs2dDepth ConsumeGs2dDepth()
{
    Gs2dDepth out{0.0f, false, GS_DEPTH_GEQUAL};

    if (!g_gs_2d_depth_armed)
    {
        return out;
    }
    g_gs_2d_depth_armed = false;

    if (!Gs2dDepthStageNeeded())
    {
        return out;
    }

    out.ndc_z = Gs2dZToNdc(g_gs_2d_z);
    out.test = true;
    out.compare = g_gs_ztst;
    return out;
}

/*
 * The depth row of the projection every mesh is drawn with:
 *
 *     clip.z = view.z * z_scale + z_offset,    clip.w = view.z
 *
 * i.e. the reversed-Z column MioPan_Graph3dApplyCamera() wrote into
 * g_3d_projection, read back out of it.  A world-space billboard the CPU
 * projected already has clip.w -- the view depth -- so this rebuilds its z
 * with the very coefficients the GPU applies to the surface behind it:
 * `w * z_scale + z_offset`, two small terms, nothing cancelling.
 *
 * Why it is needed at all: those billboards are projected through the
 * engine's symmetric matWorldClipObject, where z ~ 1.000003w - 0.2, and the
 * old route reversed that on the GPU as 0.5w - 0.5z.  Both terms carry an ulp
 * of the world coordinates, and ~10^4 units from the origin -- every outdoor
 * map -- that left the depth wrong by 4 / 12 / 33 / 107 units (median) at
 * 2k / 5k / 10k / 18k, flickering hazes, glows and flames in front of and
 * behind the room.  Rebuilt here the error is the meshes' own, < 0.01.
 *
 * False -- and the caller keeps its symmetric z and MikuPanFixClipZ() -- for
 * anything but the perspective form g3dCalcViewClipMatrixPerspective() builds
 * (view depth into w and nothing else), and before any camera is installed.
 */
static bool ReversedDepthRow(float *z_scale, float *z_offset)
{
    const float *p = g_3d_projection;

    if (!g_3d_camera_valid ||
        p[0 * 4 + 2] != 0.0f || p[1 * 4 + 2] != 0.0f ||
        p[0 * 4 + 3] != 0.0f || p[1 * 4 + 3] != 0.0f ||
        p[2 * 4 + 3] != 1.0f || p[3 * 4 + 3] != 0.0f)
    {
        return false;
    }

    *z_scale = p[2 * 4 + 2];
    *z_offset = p[3 * 4 + 2];
    return true;
}

static void DrawTexturedQuadImpl(const sceGsTex0 *tex0,
                                 const sceGsTex1 *tex1,
                                 const float *xy,
                                 const float *uv,
                                 unsigned char r,
                                 unsigned char g,
                                 unsigned char b,
                                 unsigned char a,
                                 float ndc_z,
                                 bool depth_test,
                                 uint64_t clamp,
                                 GsDepthCompare depth_compare,
                                 bool clip_z_reversed = false)
{
    SpriteVertex vertices[4];

    if (xy == nullptr || uv == nullptr || !g_frame_active)
    {
        return;
    }

    /* The native grain sheet stands in for whatever TEX0 names while the grain
     * bracket is open.  Nothing else draws between Begin and End -- the three
     * quads are consecutive in SubDither3()/SubDither4() -- so this needs no
     * finer test than the flag.
     *
     * Taken ahead of the capture-slot resolution below, and skipping it,
     * because the ROM parks its noise sheet at GS 0x2200, which is inside frame
     * buffer 1: this draw must never be answered with a copy of the frame. */
    const bool grain_draw =
        g_film_grain_active && g_film_grain_sheet != nullptr;

    /* movie.c's full-screen blit, which names GS 0x2bc0 -- an address the
     * screen-page registry also declares a capture slot for, so the resolution
     * below would answer this draw with a copy of the frame buffer.  The movie
     * has its own texture; skip straight to GetTexture(), which serves it. */
    const bool video_draw = IsVideoTex0(tex0);

    /*
     * A quad sampling GS memory the renderer owns -- the live frame buffer, or
     * one of the scratch pages a LocalCopy* captured into.  There is nothing
     * decodable at those addresses in emulated GS memory (the host never writes
     * the frame back there), so the draw is served from a capture slot.
     *
     * The frame buffer resolves to the live scene: the draw is marked to break
     * the render pass and a capture is taken at that point, so it samples
     * exactly the composite that preceded it.  A scratch page was already
     * captured by its LocalCopy*, earlier in the queue, and just samples it.
     */
    if (!grain_draw && !video_draw)
    {
        int slot_index = -1;

        if (IsFramebufferTex0(tex0))
        {
            slot_index = DeclareCaptureSlot(kCaptureAddrScene, kLogicalWidth,
                                            kLogicalHeight);
            if (slot_index < 0 ||
                g_capture_slots[(size_t)slot_index].texture == nullptr)
            {
                /* First one of the session: UpdateViewExtend() allocates the
                 * target now that it has been asked for, and this draw resumes
                 * next frame.  Drawing here would sample an undefined
                 * texture. */
                return;
            }
            /* Capture the frame as it stands at this point in the queue. */
            CapturePoint point{};
            point.draw_index = g_draws.size();
            point.dst_slot = slot_index;
            point.src_slot = -1;            /* the live colour target */
            g_capture_points.push_back(point);
            /* Valid from here, not from when the copy is recorded at present:
             * the capture precedes this draw in the ordered stream, so by the
             * time the GPU runs the draw the slot holds the frame.  Waiting for
             * RecordGsCapture() to set it left every draw in the slot's first
             * frame seeing valid == false. */
            g_capture_slots[(size_t)slot_index].valid = true;
        }
        else
        {
            slot_index = CaptureSlotForTex0(tex0);
            if (slot_index < 0 && IsCaptureSlotAddr(tex0))
            {
                /* Declared but nothing captured into it yet.  Falling through
                 * to GetTexture() here would decode whatever happens to be at
                 * that GS address as an image -- the garbage this used to draw
                 * on an effect's first appearance. */
                return;
            }
        }

        if (slot_index >= 0)
        {
            const CaptureSlot &slot = g_capture_slots[(size_t)slot_index];
            float extended[8];

            /*
             * The capture covers the whole output, including the extra world a
             * wide window shows, while the effect's quad is written in 640x448
             * frame coordinates.  Grow it about the frame centre so
             * ApplyOriginalAspectToVertices() maps it back out to the window
             * bounds -- the same trick MioPan_RendererDrawSolidQuad uses for a
             * full-screen fill -- and normalise the UVs against the slot's own
             * logical size, which is the space the ROM wrote them in.  Window
             * maps onto window, so a 1:1 filter stays 1:1 whatever the aspect.
             */
            for (int i = 0; i < 4; i++)
            {
                extended[i * 2 + 0] =
                    (float)kLogicalWidth * 0.5f +
                    (xy[i * 2 + 0] - (float)kLogicalWidth * 0.5f) *
                        g_view_extend_x;
                extended[i * 2 + 1] =
                    (float)kLogicalHeight * 0.5f +
                    (xy[i * 2 + 1] - (float)kLogicalHeight * 0.5f) *
                        g_view_extend_y;
            }

            BuildQuadVertices(vertices, extended, uv, (float)slot.logical_w,
                              (float)slot.logical_h, Ps2ColorToFloat(r),
                              Ps2ColorToFloat(g), Ps2ColorToFloat(b),
                              Ps2AlphaToFloat(a), ndc_z);
            QueueQuad(slot.texture, (int)slot.tex_w, (int)slot.tex_h,
                      vertices, true, false, true, true, DRAW_SOURCE_GENERIC,
                      depth_test, depth_compare, clip_z_reversed);
            g_scene_capture_count++;
            return;
        }
    }

    int   tex_width;
    int   tex_height;
    float uv_scale_w;
    float uv_scale_h;
    SDL_GPUTexture *texture;

    if (grain_draw)
    {
        texture = g_film_grain_sheet;
        tex_width = g_film_grain_sheet_size;
        tex_height = g_film_grain_sheet_size;
        uv_scale_w = g_film_grain_tex_w;
        uv_scale_h = g_film_grain_tex_h;
    }
    else
    {
        TextureEntry *entry = GetTexture(tex0);
        if (entry == nullptr || entry->texture == nullptr)
        {
            return;
        }
        texture = entry->texture;
        tex_width = entry->width;
        tex_height = entry->height;
        uv_scale_w = (float)entry->width;
        uv_scale_h = (float)entry->height;
    }

    BuildQuadVertices(vertices, xy, uv, uv_scale_w, uv_scale_h,
                      Ps2ColorToFloat(r), Ps2ColorToFloat(g),
                      Ps2ColorToFloat(b), Ps2AlphaToFloat(a), ndc_z);
    bool min_linear = true;
    bool mag_linear = true;
    if (tex1 != nullptr)
    {
        /* MMIN 1/4/5 linearly filters within a mip level; 0/2/3 use the
         * nearest texel.  MioPan currently uploads only the base level. */
        min_linear = tex1->MMIN == 1 || tex1->MMIN >= 4;
        mag_linear = tex1->MMAG != 0;
    }
    /* GS CLAMP: WMS in bits 0..1, WMT in bits 2..3, 0 = REPEAT.  The sampler
     * table carries one address mode for both axes, so only a matched REPEAT
     * pair wraps -- see MioPan_RendererDrawTexturedQuadClamp's note. */
    const bool repeat_uv = (clamp & 0x3ull) == 0 && ((clamp >> 2) & 0x3ull) == 0;

    QueueQuad(texture, tex_width, tex_height, vertices,
              true, repeat_uv, min_linear, mag_linear, DRAW_SOURCE_GENERIC,
              depth_test, depth_compare, clip_z_reversed);
}

void MioPan_RendererDrawTexturedQuad(const sceGsTex0 *tex0,
                                     const sceGsTex1 *tex1,
                                     const float *xy,
                                     const float *uv,
                                     unsigned char r,
                                     unsigned char g,
                                     unsigned char b,
                                     unsigned char a)
{
    /* The GS drew a 2D sprite through the same depth unit as everything else,
     * and photo_make.c / n_equip_tray.c depend on it -- see the note on
     * MioPan_RendererSetGs2dDepth(). */
    const Gs2dDepth depth = ConsumeGs2dDepth();

    DrawTexturedQuadImpl(tex0, tex1, xy, uv, r, g, b, a, depth.ndc_z,
                         depth.test, kClampBothAxes, depth.compare);
}

/* DispSprD2()'s path: the sprite's own CLAMP register decides the wrap. */
void MioPan_RendererDrawTexturedQuadClamp(const sceGsTex0 *tex0,
                                          const sceGsTex1 *tex1,
                                          const float *xy,
                                          const float *uv,
                                          uint64_t clamp,
                                          unsigned char r,
                                          unsigned char g,
                                          unsigned char b,
                                          unsigned char a)
{
    const Gs2dDepth depth = ConsumeGs2dDepth();

    DrawTexturedQuadImpl(tex0, tex1, xy, uv, r, g, b, a, depth.ndc_z,
                         depth.test, clamp, depth.compare);
}

/*
 * The same quad, depth-tested against the 3D scene.
 *
 * A GS sprite carries a Z of its own and the effect layer draws its particle
 * billboards that way: screen-space geometry, world-space depth, ZTE on and
 * ZMSK set so the pass tests without writing.  The screen-space bridge above
 * had nowhere to put that Z, so every flame, spark and heat haze drew over the
 * room whatever stood in front of it.  Depth-write follows the live ZBUF
 * shadow, so a ZMSK the effect set is still honoured.
 *
 * The depth comes from `view_depth`, the particle's clip w, rebuilt into the
 * reversed convention with the meshes' own depth row (ReversedDepthRow()).
 * `ndc_z` -- the engine's symmetric z/w, which the caller had first -- is used
 * only when no perspective camera is installed: reversing it on the GPU cancels
 * all but a few of its digits, and that was tens of units of depth noise at
 * outdoor distances, enough to flip a flame in front of or behind its torch.
 */
void MioPan_RendererDrawTexturedQuadDepth(const sceGsTex0 *tex0,
                                          const sceGsTex1 *tex1,
                                          const float *xy,
                                          const float *uv,
                                          float ndc_z,
                                          float view_depth,
                                          unsigned char r,
                                          unsigned char g,
                                          unsigned char b,
                                          unsigned char a)
{
    float z_scale;
    float z_offset;

    /* An effect billboard supplies its own clip-space depth and the engine's
     * GEQUAL; the GS 2D Z belongs to the sprite path, not here. */
    if (view_depth > 0.0f && ReversedDepthRow(&z_scale, &z_offset))
    {
        const float reversed_ndc_z =
            (view_depth * z_scale + z_offset) / view_depth;

        DrawTexturedQuadImpl(tex0, tex1, xy, uv, r, g, b, a, reversed_ndc_z,
                             true, kClampBothAxes, GS_DEPTH_GEQUAL, true);
        return;
    }

    DrawTexturedQuadImpl(tex0, tex1, xy, uv, r, g, b, a, ndc_z, true,
                         kClampBothAxes, GS_DEPTH_GEQUAL);
}

/* MapSky.c's own quad path: no TEX1, so always the nearest-filter sampler.
 *
 * Clamped, not wrapped, even though the GS CLAMP register the sky draws under
 * says WMS/WMT = REPEAT.  Both sky pages were read out of the pak to settle
 * it: the dome page is a single soft decal whose alpha is 0 along all four
 * edges (centre ~43), so tiling it puts a hard transparent seam at every
 * boundary -- which is exactly the lattice that showed up on screen.  The
 * horizon strip does wrap left-to-right (both edge columns sit at alpha 119),
 * but each sprite covers exactly one page, UV 0..0x1000, so the panorama
 * repeats by laying sprites side by side and never leans on the sampler.
 * Clamping is therefore right for the dome and a no-op for the strip. */
void MioPan_RendererDrawSkyQuad(const sceGsTex0 *tex0,
                                const float *xy,
                                const float *uv,
                                unsigned char r,
                                unsigned char g,
                                unsigned char b,
                                unsigned char a)
{
    SpriteVertex vertices[4];
    TextureEntry *entry;

    if (xy == nullptr || uv == nullptr || !g_frame_active)
    {
        return;
    }

    entry = GetTexture(tex0);
    if (entry == nullptr || entry->texture == nullptr)
    {
        return;
    }

    /* TEMP PROBE -- remove.  Which decoded page each sky layer actually gets.
     * The dome and the horizon strip upload different images to the same TBP
     * with an identical TEX0, so a stale cache entry would silently hand one
     * layer the other's page. */
    {
        static int dbg_dome = 0;
        static int dbg_strip = 0;
        float ylo = xy[1], yhi = xy[1];
        float xlo = xy[0], xhi = xy[0];
        for (int n = 1; n < 4; n++)
        {
            ylo = std::min(ylo, xy[n * 2 + 1]);
            yhi = std::max(yhi, xy[n * 2 + 1]);
            xlo = std::min(xlo, xy[n * 2 + 0]);
            xhi = std::max(xhi, xy[n * 2 + 0]);
        }
        const bool is_strip = (yhi - ylo) > 200.0f;
        /* Dome: only cells that actually land in the frame.  Strip: all of
         * them, so the tiling across x is visible in the log. */
        const bool want = is_strip ? (dbg_strip < 6)
                                   : (dbg_dome < 4 && yhi > 0.0f && ylo < 448.0f);
        if (want)
        {
            if (is_strip) dbg_strip++; else dbg_dome++;
            SDL_Log("SKYQUAD %s x=%.0f..%.0f y=%.0f..%.0f "
                    "uv=(%.1f,%.1f)..(%.1f,%.1f) tex=%dx%d entry=%p tex0=%016llx",
                    is_strip ? "STRIP" : "dome ", xlo, xhi, ylo, yhi,
                    uv[0], uv[1], uv[6], uv[7],
                    entry->width, entry->height, (void *)entry,
                    (unsigned long long)ReadTex0Value(tex0));
        }
    }

    BuildQuadVertices(vertices, xy, uv, (float)entry->width,
                      (float)entry->height, Ps2ColorToFloat(r),
                      Ps2ColorToFloat(g), Ps2ColorToFloat(b),
                      Ps2AlphaToFloat(a));
    QueueQuad(entry->texture, entry->width, entry->height, vertices, true,
              true, true, true, DRAW_SOURCE_SKY);
}

/*
 * A flat quad that covers exactly the original 640x448 frame is a full-screen
 * fill -- a fade, a flash, a letterbox wipe -- not an overlay pinned to the
 * 4:3 image.  Now that 3D fills the window, leaving those inside the original
 * frame would fade the middle of the screen and leave the sides untouched, so
 * stretch them out to the view bounds instead.
 *
 * Only applied to the untextured path: a flat colour cannot be distorted by
 * being stretched, whereas a full-frame *image* legitimately wants to keep its
 * proportions and still goes through MioPan_RendererDrawTexturedQuad.
 */
bool IsFullFrameQuad(const float *xy)
{
    /*
     * 1.0, not 0.5, and the half pixel is the whole point: the effect module
     * writes its full-screen fills at (-0.5, -0.5) 640x448 -- the PS2's
     * pixel-centre convention -- so DispSprD2 hands this a quad spanning
     * -0.5..639.5 by -0.5..447.5 and every one of the four tests came out
     * exactly *equal* to 0.5 rather than under it.  Those fills are the only
     * untextured sprite draws in the tree (SubContrast3 and SubNega's first
     * pass), so at 0.5 this branch never fired for a sprite at all and both
     * were left inside the pillarbox -- SubNega visibly so, since its second
     * pass samples a capture slot and *is* widened.
     */
    const float epsilon = 1.0f;
    float min_x = xy[0];
    float max_x = xy[0];
    float min_y = xy[1];
    float max_y = xy[1];

    for (int i = 1; i < 4; i++)
    {
        min_x = std::min(min_x, xy[i * 2 + 0]);
        max_x = std::max(max_x, xy[i * 2 + 0]);
        min_y = std::min(min_y, xy[i * 2 + 1]);
        max_y = std::max(max_y, xy[i * 2 + 1]);
    }

    return std::fabs(min_x) < epsilon &&
           std::fabs(max_x - (float)kLogicalWidth) < epsilon &&
           std::fabs(min_y) < epsilon &&
           std::fabs(max_y - (float)kLogicalHeight) < epsilon;
}

void MioPan_RendererDrawSolidQuad(const float *xy, const unsigned char *rgba)
{
    SpriteVertex vertices[4];
    bool full_frame;

    /* Before the guards below: an armed Z belongs to this draw whether or not
     * it survives them. */
    const Gs2dDepth depth = ConsumeGs2dDepth();

    if (xy == nullptr || rgba == nullptr || !g_frame_active || !EnsureRenderer())
    {
        return;
    }

    full_frame = IsFullFrameQuad(xy);

    for (int i = 0; i < 4; i++)
    {
        float x = xy[i * 2 + 0];
        float y = xy[i * 2 + 1];

        if (full_frame)
        {
            /* About the centre, so the fill lands exactly on the bounds
             * ApplyOriginalAspectToVertices() will map back to the window. */
            x = (float)kLogicalWidth * 0.5f +
                (x - (float)kLogicalWidth * 0.5f) * g_view_extend_x;
            y = (float)kLogicalHeight * 0.5f +
                (y - (float)kLogicalHeight * 0.5f) * g_view_extend_y;
        }

        vertices[i].uv[0] = 0.0f;
        vertices[i].uv[1] = 0.0f;
        vertices[i].uv[2] = 0.0f;
        vertices[i].uv[3] = 0.0f;

        /* Untextured: every caller of this entry point builds a GS primitive
         * with TME=0 -- MapSky's fog band, g2d_draw's DRAW_ENV_NOTEX quads and
         * graphics.c's flat fills -- so the RGB divisor is 255. */
        vertices[i].colour[0] = Ps2UntexturedColorToFloat(rgba[i * 4 + 0]);
        vertices[i].colour[1] = Ps2UntexturedColorToFloat(rgba[i * 4 + 1]);
        vertices[i].colour[2] = Ps2UntexturedColorToFloat(rgba[i * 4 + 2]);
        vertices[i].colour[3] = Ps2AlphaToFloat(rgba[i * 4 + 3]);

        vertices[i].position[0] = ScreenXToClip(x);
        vertices[i].position[1] = ScreenYToClip(y);
        /* Same GS depth unit the textured sprite path uses: a DISP_SQAR with
         * ZMSK clear is how photo_make.c and n_equip_tray.c lay the depth
         * wedge their later passes are masked against.  0 for a caller that
         * supplied none -- MapSky's fog band, graphics.c's fills -- which is
         * mid-range in the symmetric convention and, with the depth stage off
         * for them too, is the same unused value they carried before. */
        vertices[i].position[2] = depth.ndc_z;
        vertices[i].position[3] = 1.0f;
    }

    if (g_gs_blend_mode == GS_BLEND_DST_ADD)
    {
        /* The GS equation behind this mode never reads the source colour:
         * Cv = (Cd - 0)*As/128 + Cd.  The pipeline expresses it as
         * Cv = Cd*(1 + Cs) (DST_COLOR / ONE), so the As/128 weight has to
         * travel in the colour channels instead -- and since the blend stage
         * clamps Cs to 1.0 while As/128 runs to ~2, a second quad carries the
         * remainder: (1 + 1)*(1 + (k - 1)/2) == 1 + k.  The intermediate
         * clamp loses nothing: whenever pass one saturates a channel, the GS
         * value for it is past white as well. */
        SpriteVertex remainder[4];
        bool needs_remainder = false;

        for (int i = 0; i < 4; i++)
        {
            float k = (float)rgba[i * 4 + 3] / 128.0f;
            float rest = std::max(0.0f, (k - 1.0f) * 0.5f);

            vertices[i].colour[0] = std::min(1.0f, k);
            vertices[i].colour[1] = std::min(1.0f, k);
            vertices[i].colour[2] = std::min(1.0f, k);
            remainder[i] = vertices[i];
            remainder[i].colour[0] = rest;
            remainder[i].colour[1] = rest;
            remainder[i].colour[2] = rest;
            if (rest > 0.0f)
            {
                needs_remainder = true;
            }
        }

        QueueQuad(g_white_texture, 1, 1, vertices, true, false, true, true,
                  DRAW_SOURCE_GENERIC, depth.test, depth.compare);
        if (needs_remainder)
        {
            QueueQuad(g_white_texture, 1, 1, remainder, true, false, true, true,
                      DRAW_SOURCE_GENERIC, depth.test, depth.compare);
        }
        return;
    }

    QueueQuad(g_white_texture, 1, 1, vertices, true, false, true, true,
              DRAW_SOURCE_GENERIC, depth.test, depth.compare);
}

/* ------------------------------------------------------------------------
 *  The viewfinder surround
 *
 *  A port addition.  See resources/shaders/hlsl/finder_mask.frag.hlsl for what
 *  the pass does and why the PS2 did not do it.
 * --------------------------------------------------------------------- */

void MioPan_RendererDrawFinderMask(int alpha)
{
    if (!g_finder_mask_enabled || !g_frame_active || !EnsureRenderer())
    {
        return;
    }
    /* The pass is not free -- it splits the render pass in two and copies the
     * frame -- so anything that would come out invisible skips it entirely. */
    if (alpha <= 0 || (g_finder_mask_blur <= 0.0f &&
                       g_finder_mask_darken <= 0.0f &&
                       g_finder_mask_tint <= 0.0f))
    {
        return;
    }

    /*
     * The aperture is the ROM's own 640x448 frame, so what this treats is
     * exactly the world a wide window shows beyond it -- geometry the game was
     * never composed to display.  Inside the frame nothing is touched at all:
     * the box SDF below is negative there, which discards before a single tap
     * is gathered.
     *
     * g_finder_mask_scale moves that edge.  1.0 is the frame exactly; above it
     * the surround starts further out, and below it the blur climbs over the
     * frame line and into the picture the game did compose, which is the one
     * setting here that can spoil the original look.
     */
    const float centre_x = (float)kLogicalWidth * 0.5f;
    const float centre_y = (float)kLogicalHeight * 0.5f;
    const float half_width = centre_x * g_finder_mask_scale;
    const float half_height = centre_y * g_finder_mask_scale;

    /* On a 4:3 window there is no world outside the frame, so there is nothing
     * for this to do and it costs nothing.  The same is true of any aperture
     * the player has widened past the view itself. */
    if (half_width >= centre_x * g_view_extend_x &&
        half_height >= centre_y * g_view_extend_y)
    {
        return;
    }

    /*
     * The scene as it stands: everything gra3dDraw() and the effect passes
     * have put down, and none of the HUD, because the game calls this from the
     * top of CNPlyrCamera::Draw().  The capture is registered against the next
     * draw index, so the main pass breaks there, copies the colour target into
     * the slot, and resumes with the quad below sampling it.
     */
    int slot_index = DeclareCaptureSlot(kCaptureAddrScene, kLogicalWidth,
                                        kLogicalHeight);
    if (slot_index < 0 ||
        g_capture_slots[(size_t)slot_index].texture == nullptr)
    {
        /* First call of the session: UpdateViewExtend() allocates the target
         * at the next BeginFrame and this resumes then.  One frame without the
         * surround, exactly as the other frame-buffer effects behave. */
        return;
    }

    /*
     * A full-frame quad, grown about the centre so that after
     * ApplyOriginalAspectToVertices() shrinks it back it covers the whole
     * output -- the same trick MioPan_RendererDrawSolidQuad uses for a
     * full-screen fill, and DrawTexturedQuadImpl for a frame-buffer effect.
     *
     * The extended corner coordinates do double duty.  They are the positions,
     * and they are also handed to the shader as vDstUV, because the frame
     * coordinate an ordinary 2D sprite would need in order to land at this
     * corner is exactly that extended value: both go through the same
     * 1/extend shrink, so equal NDC means equal frame coordinate.  That is
     * what lets the fragment stage compare against an aperture written in the
     * game's own 640x448 units and have it land on the HUD art whatever shape
     * the window is.
     */
    static const float kCorners[4][2] = {
        {0.0f, 0.0f},
        {(float)kLogicalWidth, 0.0f},
        {0.0f, (float)kLogicalHeight},
        {(float)kLogicalWidth, (float)kLogicalHeight}
    };

    const int clamped_alpha = alpha < 0 ? 0 : (alpha > 128 ? 128 : alpha);
    SpriteVertex vertices[4];
    for (int i = 0; i < 4; i++)
    {
        const float frame_x = kCorners[i][0];
        const float frame_y = kCorners[i][1];
        const float extended_x = (float)kLogicalWidth * 0.5f +
            (frame_x - (float)kLogicalWidth * 0.5f) * g_view_extend_x;
        const float extended_y = (float)kLogicalHeight * 0.5f +
            (frame_y - (float)kLogicalHeight * 0.5f) * g_view_extend_y;

        /* The capture spans the whole target, so the frame's own corners are
         * its 0 and 1. */
        vertices[i].uv[0] = frame_x / (float)kLogicalWidth;
        vertices[i].uv[1] = frame_y / (float)kLogicalHeight;
        vertices[i].uv[2] = extended_x;
        vertices[i].uv[3] = extended_y;

        /* The crimson cast, faded in by strength: at tint 0 this is white and
         * the multiply in the shader is the identity. */
        for (int c = 0; c < 3; c++)
        {
            vertices[i].colour[c] =
                1.0f + (kFinderMaskCrimson[c] - 1.0f) * g_finder_mask_tint;
        }
        /* The HUD's own master alpha, so the surround arrives and leaves with
         * the viewfinder rather than snapping on. */
        vertices[i].colour[3] =
            Ps2AlphaToFloat((unsigned char)clamped_alpha);

        vertices[i].position[0] = ScreenXToClip(extended_x);
        vertices[i].position[1] = ScreenYToClip(extended_y);
        vertices[i].position[2] = 0.0f;
        vertices[i].position[3] = 1.0f;
    }

    const CaptureSlot &slot = g_capture_slots[(size_t)slot_index];
    const size_t queued_before = g_draws.size();
    QueueQuad(slot.texture, (int)slot.tex_w, (int)slot.tex_h, vertices,
              true, false, true, true, DRAW_SOURCE_GENERIC, false,
              GS_DEPTH_GEQUAL);
    if (g_draws.size() != queued_before + 1)
    {
        /* QueueQuad refused it -- mid mesh stream, or out of vertex room.
         * Checked by size rather than by looking at the back of the list,
         * which on a refusal is some other draw entirely and must not be
         * turned into a fullscreen surround. */
        return;
    }

    /* Only now that the draw is certainly ours: the capture costs one blit and
     * a pass break, which is wasted if nothing samples it, but marking the
     * wrong draw would paint over the frame. */
    CapturePoint point{};
    point.draw_index = queued_before;
    point.dst_slot = slot_index;
    point.src_slot = -1;                    /* the live colour target */
    g_capture_points.push_back(point);
    /* Valid from here rather than from when RecordGsCapture() runs: the
     * capture precedes this draw in the ordered stream, so by the time the GPU
     * reaches the draw the slot holds the frame. */
    g_capture_slots[(size_t)slot_index].valid = true;

    DrawCommand &command = g_draws.back();
    command.finder_mask = true;

    command.finder_mask_rect[0] = centre_x;
    command.finder_mask_rect[1] = centre_y;
    command.finder_mask_rect[2] = half_width;
    command.finder_mask_rect[3] = half_height;

    command.finder_mask_params[0] =
        std::max(1.0f, std::min(half_width, half_height) *
                           kFinderMaskFeather);
    command.finder_mask_params[1] = g_finder_mask_darken;

    /*
     * The defocus radius converts to target UV here rather than in the shader,
     * because only this side knows how far the view was extended.  The frame
     * spans kLogicalWidth * g_view_extend_x frame units across the target, so
     * one frame unit is the reciprocal of that -- which is also what keeps the
     * kernel round in frame space rather than in target space.
     */
    const float radius_frame = g_finder_mask_blur * kFinderMaskBlurRadius;
    command.finder_mask_params[2] =
        radius_frame / ((float)kLogicalWidth * g_view_extend_x);
    command.finder_mask_params[3] =
        radius_frame / ((float)kLogicalHeight * g_view_extend_y);

    /* GS SCISSOR belongs to whatever the game last set, and this pass is the
     * port's own and covers the frame.  An empty box reads as "no scissor" to
     * BuildScissorRect(). */
    command.scissor.x0 = 0;
    command.scissor.y0 = 0;
    command.scissor.x1 = 0;
    command.scissor.y1 = 0;

    g_scene_capture_count++;
}

void MioPan_RendererSetFinderMask(int enable, float blur, float darken,
                                  float tint, float scale)
{
    g_finder_mask_enabled = enable != 0;
    g_finder_mask_blur = std::clamp(blur, 0.0f, 1.0f);
    g_finder_mask_darken = std::clamp(darken, 0.0f, 1.0f);
    g_finder_mask_tint = std::clamp(tint, 0.0f, 1.0f);
    g_finder_mask_scale = std::clamp(scale, 0.25f, 2.0f);
}

void MioPan_RendererGetFinderMask(int *enable, float *blur, float *darken,
                                  float *tint, float *scale)
{
    if (enable != nullptr)
    {
        *enable = g_finder_mask_enabled ? 1 : 0;
    }
    if (blur != nullptr)
    {
        *blur = g_finder_mask_blur;
    }
    if (darken != nullptr)
    {
        *darken = g_finder_mask_darken;
    }
    if (tint != nullptr)
    {
        *tint = g_finder_mask_tint;
    }
    if (scale != nullptr)
    {
        *scale = g_finder_mask_scale;
    }
}

void MioPan_RendererSetAlphaSharpen(float strength)
{
    g_alpha_sharpen = std::clamp(strength, 0.0f, 1.0f);
}

float MioPan_RendererGetAlphaSharpen(void)
{
    return g_alpha_sharpen;
}

void MioPan_RendererSetAlphaCutoff(float cutoff)
{
    g_alpha_cutoff = std::clamp(cutoff, 0.0f, 1.0f);
}

float MioPan_RendererGetAlphaCutoff(void)
{
    return g_alpha_cutoff;
}

void MioPan_RendererSetFilmGrain(int mode)
{
    if (mode < MIOPAN_FILM_GRAIN_OFF || mode > MIOPAN_FILM_GRAIN_NATIVE)
    {
        mode = MIOPAN_FILM_GRAIN_NATIVE;
    }
    g_film_grain_mode = mode;
}

int MioPan_RendererGetFilmGrain(void)
{
    return g_film_grain_mode;
}

int MioPan_RendererFilmGrainBegin(int alpmx, int colmx)
{
    g_film_grain_active = false;

    if (g_film_grain_mode != MIOPAN_FILM_GRAIN_NATIVE || g_device == nullptr ||
        g_film_grain_sheet_failed)
    {
        return 0;
    }

    /* Cover the output, rounded up to a power of two and clamped.  Both
     * dimensions off the longer edge, so a portrait window is served too. */
    int wanted = kFilmGrainSheetMin;
    const int longest = (int)(g_render_width > g_render_height
                                  ? g_render_width : g_render_height);
    while (wanted < longest && wanted < kFilmGrainSheetMax)
    {
        wanted *= 2;
    }

    if (g_film_grain_sheet == nullptr || wanted != g_film_grain_sheet_size ||
        alpmx != g_film_grain_sheet_alpmx ||
        colmx != g_film_grain_sheet_colmx)
    {
        if (!BuildFilmGrainSheet(wanted, alpmx, colmx))
        {
            /* Latched rather than retried every frame: the sheet is one
             * allocation, and a device that cannot hold it now will not hold it
             * in sixteen milliseconds either.  The grain falls back to the GS
             * sheet, which is a coarser picture and not a missing one. */
            g_film_grain_sheet_failed = true;
            SDL_Log("MioPan: film grain sheet could not be created; "
                    "falling back to the PS2 128x128 sheet");
            return 0;
        }
        g_film_grain_sheet_alpmx = alpmx;
        g_film_grain_sheet_colmx = colmx;
        /* Once per build, and a build is rare: it says which mode is really
         * live and where the VRAM went. */
        SDL_Log("MioPan: film grain sheet %dx%d (alpmx %d, colmx %d)",
                wanted, wanted, alpmx, colmx);
    }

    /*
     * The quads arrive in the ROM's frame units and cover the whole output --
     * EffScrFullScreenRect() sizes them from MioPan_RendererGetViewBounds() --
     * so one frame unit is (render size / extended frame) output pixels, and
     * the UVs the game wrote in 128x128 texels are in frame units too.
     *
     * Handing BuildQuadVertices() a texture size divided by that ratio is what
     * turns "one texel per PS2 pixel" into "one texel per output pixel": the
     * span stays where it was on screen and simply resolves finer.  Note it
     * also carries the ROM's own 512-over-448 vertical stretch through
     * untouched, because that lives in the UVs rather than here.
     */
    float extend_x = 1.0f;
    float extend_y = 1.0f;
    MioPan_RendererGetViewExtend(&extend_x, &extend_y);

    const float frame_w = (float)kLogicalWidth * extend_x;
    const float frame_h = (float)kLogicalHeight * extend_y;
    const float render_w = (float)(g_render_width > 0 ? g_render_width
                                                      : (Uint32)kLogicalWidth);
    const float render_h = (float)(g_render_height > 0 ? g_render_height
                                                       : (Uint32)kLogicalHeight);

    g_film_grain_tex_w = (float)g_film_grain_sheet_size * frame_w / render_w;
    g_film_grain_tex_h = (float)g_film_grain_sheet_size * frame_h / render_h;
    g_film_grain_active = true;
    return 1;
}

void MioPan_RendererFilmGrainEnd(void)
{
    g_film_grain_active = false;
}

/*
 * The effect packet builders' geometry: a screen-space mesh with a UV per
 * vertex, sampling a frame-buffer copy.
 *
 * MakeScrDeformPacket (the screen warps), MakePartsDeformPacket (the refracting
 * grid), EneDmgLargeHitMakePacket (the camera flash and the ghost's heavy blow)
 * and draw_distortion_particles all build raw GIF packets that dmaVif1 throws
 * away.  Each one converts its own topology -- grid strip, fan, diamond -- to a
 * triangle list and queues it here.
 *
 * The GS carries texture coordinates two ways and the builders use both: the UV
 * registers are texels, the ST registers are normalised and divided by Q.
 * `uv_normalised` says which arrived, rather than making every caller convert
 * into a size it would have to ask for.
 */
void MioPan_RendererDrawTexturedTriangles2D(const sceGsTex0 *tex0,
                                            const float *xy,
                                            const float *uv,
                                            const unsigned char *rgba,
                                            const float *ndc_z,
                                            int vertex_count,
                                            int uv_normalised,
                                            int projected,
                                            int depth_test)
{
    std::vector<SpriteVertex> vertices;
    SDL_GPUTexture *texture = nullptr;
    float u_scale = 1.0f;
    float v_scale = 1.0f;
    int tex_w = 1;
    int tex_h = 1;

    if (xy == nullptr || uv == nullptr || rgba == nullptr ||
        vertex_count <= 0 || (vertex_count % 3) != 0 || !g_frame_active)
    {
        return;
    }

    int slot_index = CaptureSlotForTex0(tex0);
    if (slot_index < 0 && IsCaptureSlotAddr(tex0))
    {
        /* Declared but not captured into yet -- the first frame of a new slot.
         * Skip rather than decode the address as a texture. */
        return;
    }
    if (slot_index >= 0)
    {
        const CaptureSlot &slot = g_capture_slots[(size_t)slot_index];
        texture = slot.texture;
        tex_w = (int)slot.tex_w;
        tex_h = (int)slot.tex_h;
        /* Normalise against the copy's own logical size, which is the space the
         * ROM wrote these coordinates in -- not the host texture's pixels. */
        /* GS-native, both ways round.  The UV registers are texels straight
         * into the buffer, so they only need dividing by the copy's width.  The
         * ST registers are normalised against the TEX0 *page* -- the GS forms
         * texel = ST * 2^TW -- and the page is bigger than the copy inside it
         * (512 against 320 for the deform scratch).  Passing ST through at 1:1
         * under-samples by that ratio and drags the sample toward the left of
         * the frame, which is what a refraction reading the wrong scenery looks
         * like. */
        u_scale = (uv_normalised ? (float)(1 << tex0->TW) : 1.0f)
                / (float)slot.logical_w;
        v_scale = (uv_normalised ? (float)(1 << tex0->TH) : 1.0f)
                / (float)slot.logical_h;
    }
    else
    {
        /* Not a capture -- an ordinary texture, so the usual cache path. */
        TextureEntry *entry = GetTexture(tex0);
        if (entry == nullptr || entry->texture == nullptr)
        {
            return;
        }
        texture = entry->texture;
        tex_w = entry->width;
        tex_h = entry->height;
        u_scale = uv_normalised ? 1.0f : 1.0f / (float)entry->width;
        v_scale = uv_normalised ? 1.0f : 1.0f / (float)entry->height;
    }

    vertices.resize((size_t)vertex_count);
    for (int i = 0; i < vertex_count; i++)
    {
        SpriteVertex &vertex = vertices[(size_t)i];

        /* Grown about the frame centre for the same reason the quad path grows:
         * the capture covers the whole output, so the mesh has to as well or it
         * samples the wide view through a 4:3 window.
         * ApplyOriginalAspectToVertices() maps it back out. */
        float x = xy[i * 2 + 0];
        float y = xy[i * 2 + 1];
        float u = uv[i * 2 + 0] * u_scale;
        float v = uv[i * 2 + 1] * v_scale;

        if (projected == 0)
        {
            /* A whole-screen filter, authored in 640x448.  Grow it so
             * ApplyOriginalAspectToVertices() maps it back out to the window
             * bounds, and let the UVs span the capture: window onto window. */
            x = (float)kLogicalWidth * 0.5f +
                (x - (float)kLogicalWidth * 0.5f) * g_view_extend_x;
            y = (float)kLogicalHeight * 0.5f +
                (y - (float)kLogicalHeight * 0.5f) * g_view_extend_y;
        }
        else
        {
            /* Geometry standing in the scene.  gra3d builds matWorldScreen
             * *without* the widening -- only the clip matrices get fExtend --
             * so these coordinates are the ROM's unextended 640x448, while the
             * renderer draws 3D through the extended matViewClipObject.  Passed
             * through, ApplyOriginalAspectToVertices()'s 1/extend puts them
             * exactly where the 3D scene is.
             *
             * The UVs have to make the same trip: they name a point on the
             * original frame, and the capture holds the whole window, so they
             * contract about the centre by the same factor.  Without this the
             * mesh samples the wrong part of the frame on any non-4:3 output. */
            u = (u - 0.5f) / g_view_extend_x + 0.5f;
            v = (v - 0.5f) / g_view_extend_y + 0.5f;
        }

        vertex.uv[0] = u;
        vertex.uv[1] = v;
        vertex.uv[2] = 0.0f;
        vertex.uv[3] = 0.0f;
        vertex.colour[0] = Ps2ColorToFloat(rgba[i * 4 + 0]);
        vertex.colour[1] = Ps2ColorToFloat(rgba[i * 4 + 1]);
        vertex.colour[2] = Ps2ColorToFloat(rgba[i * 4 + 2]);
        vertex.colour[3] = Ps2AlphaToFloat(rgba[i * 4 + 3]);
        vertex.position[0] = ScreenXToClip(x);
        vertex.position[1] = ScreenYToClip(y);
        /* The GS carried a Z per vertex in every XYZF2 and the deform's draw
         * env tests it (ZTST GEQUAL, ZMSK -- tested against the room, writing
         * nothing).  Passed through in the engine's symmetric clip convention;
         * the sprite shader's MikuPanFixClipZ() reverses it.  A caller with no
         * depth passes NULL and lands on the depth-less 0. */
        vertex.position[2] = ndc_z != nullptr ? ndc_z[i] : 0.0f;
        vertex.position[3] = 1.0f;
    }

    /* CLAMP, never REPEAT: every builder that reaches here samples a frame
     * copy under the GS's CLAMP 0x5, and wrapping a sample that ran off the
     * frame would fetch the opposite edge of the screen. */
    QueueTriangleList(texture, tex_w, tex_h, vertices.data(), vertex_count,
                      depth_test != 0, false);
}

void MioPan_RendererDrawSolidTriangles2D(const float *xy,
                                         const unsigned char *rgba,
                                         int vertex_count)
{
    std::vector<SpriteVertex> vertices;

    if (xy == nullptr || rgba == nullptr || vertex_count <= 0 ||
        (vertex_count % 3) != 0 || !g_frame_active || !EnsureRenderer())
    {
        return;
    }

    vertices.resize((size_t)vertex_count);
    for (int i = 0; i < vertex_count; i++)
    {
        SpriteVertex &vertex = vertices[(size_t)i];

        std::memset(vertex.uv, 0, sizeof(vertex.uv));
        /* Untextured, as above: effect_oth's diamond particles are PRIM 0x4d
         * with TME clear, and graphics.c's 3D shapes carry no texture either. */
        vertex.colour[0] = Ps2UntexturedColorToFloat(rgba[i * 4 + 0]);
        vertex.colour[1] = Ps2UntexturedColorToFloat(rgba[i * 4 + 1]);
        vertex.colour[2] = Ps2UntexturedColorToFloat(rgba[i * 4 + 2]);
        vertex.colour[3] = Ps2AlphaToFloat(rgba[i * 4 + 3]);
        vertex.position[0] = ScreenXToClip(xy[i * 2 + 0]);
        vertex.position[1] = ScreenYToClip(xy[i * 2 + 1]);
        vertex.position[2] = 0.0f;
        vertex.position[3] = 1.0f;
    }

    QueueTriangleList(g_white_texture, 1, 1, vertices.data(), vertex_count,
                      false, false);
}

static void BuildSolidVertex(SpriteVertex *vertex,
                             const float *position,
                             const unsigned char *rgba)
{
    std::memset(vertex->uv, 0, sizeof(vertex->uv));
    /* Untextured, as above -- the lines and points built on this are flat
     * fills, whether they come from graphics.c or from a host debug overlay. */
    vertex->colour[0] = Ps2UntexturedColorToFloat(rgba[0]);
    vertex->colour[1] = Ps2UntexturedColorToFloat(rgba[1]);
    vertex->colour[2] = Ps2UntexturedColorToFloat(rgba[2]);
    vertex->colour[3] = Ps2AlphaToFloat(rgba[3]);
    std::memcpy(vertex->position, position, sizeof(vertex->position));
}

static bool BuildLineQuad(SpriteVertex *vertices,
                          const float *p0,
                          const float *p1,
                          const unsigned char *rgba,
                          float width)
{
    static const int indices[6] = {0, 1, 2, 2, 1, 3};
    SpriteVertex corners[4];
    float dx;
    float dy;
    float length;
    float half_width;
    float offset_x;
    float offset_y;
    float corner_positions[4][4];

    dx = (p1[0] - p0[0]) * ((float)kLogicalWidth * 0.5f);
    dy = -(p1[1] - p0[1]) * ((float)kLogicalHeight * 0.5f);
    length = std::sqrt(dx * dx + dy * dy);
    if (!std::isfinite(length) || length <= 0.0001f)
    {
        return false;
    }

    half_width = std::max(width, 1.0f) * 0.5f;
    offset_x = (-dy / length) * half_width * 2.0f /
               (float)kLogicalWidth;
    offset_y = -(dx / length) * half_width * 2.0f /
               (float)kLogicalHeight;

    std::memcpy(corner_positions[0], p0, sizeof(corner_positions[0]));
    std::memcpy(corner_positions[1], p0, sizeof(corner_positions[1]));
    std::memcpy(corner_positions[2], p1, sizeof(corner_positions[2]));
    std::memcpy(corner_positions[3], p1, sizeof(corner_positions[3]));
    corner_positions[0][0] += offset_x;
    corner_positions[0][1] += offset_y;
    corner_positions[1][0] -= offset_x;
    corner_positions[1][1] -= offset_y;
    corner_positions[2][0] += offset_x;
    corner_positions[2][1] += offset_y;
    corner_positions[3][0] -= offset_x;
    corner_positions[3][1] -= offset_y;

    BuildSolidVertex(&corners[0], corner_positions[0], rgba + 0);
    BuildSolidVertex(&corners[1], corner_positions[1], rgba + 0);
    BuildSolidVertex(&corners[2], corner_positions[2], rgba + 4);
    BuildSolidVertex(&corners[3], corner_positions[3], rgba + 4);

    for (int i = 0; i < 6; i++)
    {
        vertices[i] = corners[indices[i]];
    }
    return true;
}

void MioPan_RendererDrawLine2D(const float *xy,
                               const unsigned char *rgba,
                               float width)
{
    SpriteVertex vertices[6];
    float p0[4] = {ScreenXToClip(xy != nullptr ? xy[0] : 0.0f),
                   ScreenYToClip(xy != nullptr ? xy[1] : 0.0f),
                   0.0f, 1.0f};
    float p1[4] = {ScreenXToClip(xy != nullptr ? xy[2] : 0.0f),
                   ScreenYToClip(xy != nullptr ? xy[3] : 0.0f),
                   0.0f, 1.0f};

    if (xy == nullptr || rgba == nullptr || !g_frame_active ||
        !EnsureRenderer() || !BuildLineQuad(vertices, p0, p1, rgba, width))
    {
        return;
    }

    QueueTriangleList(g_white_texture, 1, 1, vertices, 6, false, false);
}

void MioPan_RendererDrawClipLine(const float *positions,
                                 const unsigned char *rgba,
                                 float width,
                                 int depth_test)
{
    SpriteVertex vertices[6];
    float p0[4];
    float p1[4];
    float z_scale;
    float z_offset;
    bool reversed;

    if (positions == nullptr || rgba == nullptr || !g_frame_active ||
        !EnsureRenderer())
    {
        return;
    }

    std::memcpy(p0, positions + 0, sizeof(p0));
    std::memcpy(p1, positions + 4, sizeof(p1));
    if (!std::isfinite(p0[0]) || !std::isfinite(p0[1]) ||
        !std::isfinite(p0[2]) || !std::isfinite(p0[3]) ||
        !std::isfinite(p1[0]) || !std::isfinite(p1[1]) ||
        !std::isfinite(p1[2]) || !std::isfinite(p1[3]) ||
        p0[3] <= 0.0001f || p1[3] <= 0.0001f)
    {
        return;
    }

    /* Depth from w, whichever convention z arrived in -- see
     * ReversedDepthRow().  graphics.c hands over the engine's symmetric z,
     * DrawWorldLine() the host's reversed one, and both used to go through
     * MikuPanFixClipZ(): the first converted with most of its digits
     * cancelled, the second converted a second time. */
    reversed = ReversedDepthRow(&z_scale, &z_offset);
    if (reversed)
    {
        p0[2] = p0[3] * z_scale + z_offset;
        p1[2] = p1[3] * z_scale + z_offset;
    }

    p0[0] /= p0[3];
    p0[1] /= p0[3];
    p0[2] /= p0[3];
    p0[3] = 1.0f;
    p1[0] /= p1[3];
    p1[1] /= p1[3];
    p1[2] /= p1[3];
    p1[3] = 1.0f;

    if (!BuildLineQuad(vertices, p0, p1, rgba, width))
    {
        return;
    }
    QueueTriangleList(g_white_texture, 1, 1, vertices, 6,
                      depth_test != 0, depth_test != 0, false, nullptr,
                      nullptr, DRAW_SOURCE_GENERIC, reversed);
}

void MioPan_RendererDrawWorldLine(const float *positions,
                                  const unsigned char *rgba,
                                  float width,
                                  int depth_test)
{
    float identity[16];
    float clip[8];

    if (positions == nullptr || rgba == nullptr || !g_3d_camera_valid ||
        !g_frame_active || !EnsureRenderer())
    {
        return;
    }

    SetIdentityMatrix(identity);
    if (!ProjectMeshVertex(clip + 0, positions + 0, identity) ||
        !ProjectMeshVertex(clip + 4, positions + 3, identity))
    {
        return;
    }

    MioPan_RendererDrawClipLine(clip, rgba, width, depth_test);
}

void MioPan_RendererDrawClipPoint(const float *position,
                                  const unsigned char *rgba,
                                  float size,
                                  int depth_test)
{
    static const int indices[6] = {0, 1, 2, 2, 1, 3};
    SpriteVertex corners[4];
    SpriteVertex vertices[6];
    float clip[4];
    float positions[4][4];
    float half_x;
    float half_y;
    float z_scale;
    float z_offset;
    bool reversed;

    if (position == nullptr || rgba == nullptr || !g_frame_active ||
        !EnsureRenderer())
    {
        return;
    }

    std::memcpy(clip, position, sizeof(clip));
    if (!std::isfinite(clip[0]) || !std::isfinite(clip[1]) ||
        !std::isfinite(clip[2]) || !std::isfinite(clip[3]) ||
        clip[3] <= 0.0001f)
    {
        return;
    }
    /* Depth from w, as MioPan_RendererDrawClipLine() does and for the same
     * reasons. */
    reversed = ReversedDepthRow(&z_scale, &z_offset);
    if (reversed)
    {
        clip[2] = clip[3] * z_scale + z_offset;
    }
    clip[0] /= clip[3];
    clip[1] /= clip[3];
    clip[2] /= clip[3];
    clip[3] = 1.0f;

    half_x = std::max(size, 1.0f) / (float)kLogicalWidth;
    half_y = std::max(size, 1.0f) / (float)kLogicalHeight;
    for (int i = 0; i < 4; i++)
    {
        std::memcpy(positions[i], clip, sizeof(positions[i]));
    }
    positions[0][0] -= half_x;
    positions[0][1] += half_y;
    positions[1][0] += half_x;
    positions[1][1] += half_y;
    positions[2][0] -= half_x;
    positions[2][1] -= half_y;
    positions[3][0] += half_x;
    positions[3][1] -= half_y;

    for (int i = 0; i < 4; i++)
    {
        BuildSolidVertex(&corners[i], positions[i], rgba);
    }
    for (int i = 0; i < 6; i++)
    {
        vertices[i] = corners[indices[i]];
    }
    QueueTriangleList(g_white_texture, 1, 1, vertices, 6,
                      depth_test != 0, depth_test != 0, false, nullptr,
                      nullptr, DRAW_SOURCE_GENERIC, reversed);
}

void MioPan_RendererDrawWorldPoint(const float *position,
                                   const unsigned char *rgba,
                                   float size,
                                   int depth_test)
{
    float identity[16];
    float clip[4];

    if (position == nullptr || rgba == nullptr || !g_3d_camera_valid ||
        !g_frame_active || !EnsureRenderer())
    {
        return;
    }

    SetIdentityMatrix(identity);
    if (!ProjectMeshVertex(clip, position, identity))
    {
        return;
    }

    MioPan_RendererDrawClipPoint(clip, rgba, size, depth_test);
}

/* =======================================================================
 *  Projected shadows -- the host side of gra3dShadow.c.
 *
 *  The ROM renders the caster into an off-screen target with the light camera
 *  applied and then projects that texture over every registered receiver on
 *  VU1.  Both halves are bracketed here rather than reimplemented: the shadow
 *  code keeps calling _gra3dDrawSGD(), and these calls say which pass the
 *  geometry it produces belongs to.
 * ==================================================================== */

/*
 * Save and restore the host camera across one cast shadow.  The pair mirrors
 * gra3dshadowDrawSGD()'s `camOrigin = *gra3dGetCamera()` and _DrawShadow()'s
 * `_gra3dSetCameraForce(pCamera)`: on hardware that assignment is enough,
 * because every matrix the projection pass needs is a member of the struct it
 * copies, but the host's view and projection live in the renderer and are only
 * written by gra3dApplyCamera().
 */
void MioPan_RendererShadowSaveCamera(void)
{
    if (!g_3d_camera_valid)
    {
        g_shadow_saved_camera_valid = false;
        return;
    }
    std::memcpy(g_shadow_saved_view, g_3d_view, sizeof(g_shadow_saved_view));
    std::memcpy(g_shadow_saved_projection, g_3d_projection,
                sizeof(g_shadow_saved_projection));
    g_shadow_saved_camera_valid = true;
}

void MioPan_RendererShadowRestoreCamera(void)
{
    if (!g_shadow_saved_camera_valid)
    {
        return;
    }
    MioPan_RendererSet3DViewProjection(g_shadow_saved_view,
                                       g_shadow_saved_projection);
}

void MioPan_RendererShadowBeginCaster(void)
{
    if (g_shadow_caster_depth++ != 0)
    {
        return;
    }

    /* A new shadow.  Past the last atlas tile it is dropped outright rather
     * than sharing a tile with another caster. */
    if ((int)g_shadow_episodes.size() >= kShadowMaxEpisodes)
    {
        g_shadow_current_episode = -1;
        return;
    }

    ShadowEpisode episode{};
    episode.first_caster_draw = g_draws.size();
    episode.last_caster_draw = g_draws.size();
    episode.strength = 0.0f;
    episode.have_casters = false;
    /* Identity until EndCaster() snapshots the light camera. */
    for (int i = 0; i < 4; i++)
    {
        episode.matrix[i * 4 + i] = 1.0f;
    }
    g_shadow_episodes.push_back(episode);
    g_shadow_current_episode = (int)g_shadow_episodes.size() - 1;
}

void MioPan_RendererShadowEndCaster(void)
{
    if (g_shadow_caster_depth > 0)
    {
        g_shadow_caster_depth--;
    }
    if (g_shadow_caster_depth != 0 || g_shadow_current_episode < 0)
    {
        return;
    }

    /* Every caster draw is queued by now -- _gra3dDrawSGD() has returned -- so
     * this bound is exact. */
    g_shadow_episodes[g_shadow_current_episode].last_caster_draw =
        g_draws.size();

    /*
     * Snapshot the light camera while it is still applied -- _RenderShadow()
     * is about to return and _DrawShadow() restores the game camera as its
     * first act.  This matrix is what the receiver shader projects through,
     * the part s_matIP plays on VU1.
     */
    if (g_3d_camera_valid)
    {
        ShadowEpisode &episode = g_shadow_episodes[g_shadow_current_episode];
        std::memcpy(episode.matrix, g_3d_view_projection,
                    sizeof(episode.matrix));
    }
}

void MioPan_RendererShadowBeginReceiver(void)
{
    g_shadow_receiver_depth++;
}

void MioPan_RendererShadowEndReceiver(void)
{
    if (g_shadow_receiver_depth > 0)
    {
        g_shadow_receiver_depth--;
    }
}

/*
 * The projector strength: _CalcColor()'s alpha, which is the light's diffuse
 * luminance at the shadow target divided by sqrt(3) and halved.  The ROM packs
 * it into the VU1 scratchpad as a 0..255 byte; the shader wants 0..1.
 */
void MioPan_RendererShadowSetStrength(float strength)
{
    if (g_shadow_current_episode < 0 ||
        g_shadow_current_episode >= (int)g_shadow_episodes.size())
    {
        return;
    }
    if (!std::isfinite(strength))
    {
        strength = 0.0f;
    }
    g_shadow_episodes[g_shadow_current_episode].strength =
        std::min(std::max(strength, 0.0f), 1.0f);
}

/*
 * Called once a frame, before the room registers anything.  Matches
 * gra3dshadowClearProjectModel() at the top of MhCtlDraw().
 */
void MioPan_RendererShadowReset(void)
{
    g_shadow_caster_depth = 0;
    g_shadow_receiver_depth = 0;
    g_shadow_current_episode = -1;
    g_shadow_valid = false;
    g_shadow_saved_camera_valid = false;
    g_shadow_stat_casters = 0;
    g_shadow_stat_receivers = 0;
    g_shadow_episodes.clear();
}

/* Last frame's shadow activity: how many shadows were cast, how much
 * geometry went into their maps, how many receiver blocks sampled them,
 * and whether a map survived to be projected. */
void MioPan_RendererGetShadowStats(int *episodes, int *casters,
                                   int *receivers, int *valid)
{
    if (episodes != nullptr) *episodes = (int)g_shadow_episodes.size();
    if (casters != nullptr) *casters = g_shadow_stat_casters;
    if (receivers != nullptr) *receivers = g_shadow_stat_receivers;
    if (valid != nullptr) *valid = g_shadow_valid ? 1 : 0;
}

void MioPan_RendererSet3DViewProjection(const float *view,
                                        const float *projection)
{
    if (view == nullptr || projection == nullptr)
    {
        return;
    }

    std::memcpy(g_3d_view, view, sizeof(g_3d_view));
    std::memcpy(g_3d_projection, projection, sizeof(g_3d_projection));
    MulMatrixRowMajor(g_3d_view_projection, g_3d_view, g_3d_projection);
    g_3d_camera_valid = true;

    std::memcpy(g_uniforms.view, g_3d_view, sizeof(g_uniforms.view));
    std::memcpy(g_uniforms.projection, g_3d_projection,
                sizeof(g_uniforms.projection));
    std::memcpy(g_uniforms.viewProj, g_3d_view_projection,
                sizeof(g_uniforms.viewProj));
}

/* ==========================================================================
 *  Camera reprojection
 *
 *  The game simulates at a fixed 30 Hz -- one GPhaseSysMain() iteration is one
 *  animation step, one physics step and one input sample, with no delta time
 *  anywhere in the engine -- so extra presented frames cannot come from running
 *  it faster.  They come from re-aiming the frame that has already been built.
 *
 *  Every 3D draw carries both its own `model` and the `mvp` that was
 *  model * view_projection when it was queued, so a second camera needs one
 *  matrix product per draw and no vertex work at all: positions are model-space
 *  and the transform is a uniform (see mesh.vert.hlsl).
 * ======================================================================== */

int MioPan_RendererHavePreviousCamera(void)
{
    return (g_prev_3d_camera_valid && g_3d_camera_valid) ? 1 : 0;
}

int MioPan_RendererBlendCameraFromPrevious(float t, float *view,
                                           float *projection)
{
    if (view == nullptr || projection == nullptr ||
        !MioPan_RendererHavePreviousCamera())
    {
        return 0;
    }

    return BlendCameraBetween(g_prev_3d_view, g_prev_3d_projection, g_3d_view,
                              g_3d_projection, t, view, projection)
               ? 1
               : 0;
}

int MioPan_RendererReprojectDraws(const float *view, const float *projection)
{
    /* The camera alone: every draw keeps the transform the frame built it
     * with.  The present loop uses the internal form, which can additionally
     * move a rigidly bound block toward this frame's pose. */
    return ReprojectDrawsAt(view, projection, -1.0f);
}

void MioPan_RendererSetFog(int enable,
                           float f_min,
                           float f_max,
                           float fa,
                           float fb,
                           int r,
                           int g,
                           int b)
{
    /* gra3d's own idle state is fNear == fFar == 0, which makes both ramp
     * coefficients non-finite.  The ROM handed that to a VU that simply
     * produced garbage nobody looked at, because nothing was being drawn yet;
     * here it would poison every fragment, so treat it as fog-off. */
    if (!std::isfinite(f_min) || !std::isfinite(f_max) ||
        !std::isfinite(fa) || !std::isfinite(fb))
    {
        enable = 0;
    }

    const float scale = 1.0f / 255.0f;

    g_gs_fog[0] = f_min * scale;
    g_gs_fog[1] = f_max * scale;
    g_gs_fog[2] = fa * scale;
    g_gs_fog[3] = fb * scale;

    g_gs_fog_color[0] = (float)r * scale;
    g_gs_fog_color[1] = (float)g * scale;
    g_gs_fog_color[2] = (float)b * scale;
    g_gs_fog_color[3] = enable ? 1.0f : 0.0f;
}

static void DrawClipTrianglesImpl(const sceGsTex0 *tex0,
                                  const float *positions,
                                  const float *uv,
                                  const float *rgba,
                                  int vertex_count,
                                  int depth_test,
                                  DrawSource source,
                                  float ndc_z_bias = 0.0f)
{
    TextureEntry *entry = nullptr;
    SDL_GPUTexture *texture;
    int texture_width;
    int texture_height;
    float z_scale;
    float z_offset;

    if (positions == nullptr || vertex_count <= 0 || (vertex_count % 3) != 0 ||
        !g_frame_active || !EnsureRenderer())
    {
        return;
    }

    /* Every caller projects with the engine's symmetric matWorldClipObject
     * (graphics.c's ProjectWorldToHostClip()), so z cancels to a handful of
     * digits if the GPU reverses it.  Rebuild it from w instead -- see
     * ReversedDepthRow() -- and tell sprite.vert to leave it alone. */
    const bool reversed = ReversedDepthRow(&z_scale, &z_offset);

    if (tex0 != nullptr)
    {
        int l1_hits_before = g_texture_l1_hits;
        if (source == DRAW_SOURCE_BILLBOARD)
        {
            g_draw_source_metrics[DRAW_SOURCE_BILLBOARD].texture_lookups++;
        }
        entry = GetTexture(tex0);
        if (source == DRAW_SOURCE_BILLBOARD &&
            g_texture_l1_hits == l1_hits_before)
        {
            g_draw_source_metrics[DRAW_SOURCE_BILLBOARD].texture_misses++;
        }
    }
    if (entry != nullptr && entry->texture != nullptr)
    {
        texture = entry->texture;
        texture_width = entry->width;
        texture_height = entry->height;
    }
    else
    {
        texture = g_white_texture;
        texture_width = 1;
        texture_height = 1;
    }

    std::vector<SpriteVertex> vertices;
    vertices.reserve((size_t)vertex_count);
    for (int i = 0; i < vertex_count; i += 3)
    {
        SpriteVertex tri[3];
        bool valid = true;

        for (int j = 0; j < 3; j++)
        {
            int index = i + j;
            SpriteVertex &v = tri[j];

            for (int component = 0; component < 4; component++)
            {
                float value = positions[index * 4 + component];
                v.position[component] = value;
                valid = valid && std::isfinite(value);
            }
            if (reversed)
            {
                v.position[2] = v.position[3] * z_scale + z_offset;
            }

            /* The ROM's GS-Z bias, applied in clip space: scaling by w makes
             * it a constant offset in NDC after the perspective divide, which
             * is the space the caller converted it into.  MakePacket3D()'s
             * iZOffset is the only source of one.
             *
             * Floored to a few units of whatever depth buffer we actually got.
             * The ROM's +100 is 100/0xffffff = 6e-6 of the range, which is
             * meaningful in the GS's 24-bit Z but is BELOW one unit of a
             * D16_UNORM buffer (1.5e-5) -- there it quantises to nothing and
             * the movie-room screen, which is coplanar with the projector
             * screen mesh, loses the tie and disappears behind it. */
            if (ndc_z_bias != 0.0f)
            {
                float bias = ndc_z_bias;
                const float floor_bias = 4.0f * DepthUnitSize();

                if (bias > 0.0f && bias < floor_bias)
                    bias = floor_bias;
                else if (bias < 0.0f && bias > -floor_bias)
                    bias = -floor_bias;

                /* `bias` is in the caller's symmetric NDC.  A reversed z skips
                 * MikuPanFixClipZ(), whose 0.5w - 0.5z turned the symmetric
                 * offset into -0.5 * bias, so apply exactly that here and the
                 * effect on the depth test is unchanged. */
                if (reversed)
                    v.position[2] -= 0.5f * bias * v.position[3];
                else
                    v.position[2] += bias * v.position[3];
            }

            v.uv[0] = uv != nullptr ? uv[index * 2 + 0] : 0.0f;
            v.uv[1] = uv != nullptr ? uv[index * 2 + 1] : 0.0f;
            v.uv[2] = 0.0f;
            v.uv[3] = 0.0f;

            v.colour[0] = rgba != nullptr ? Clamp01(rgba[index * 4 + 0]) : 1.0f;
            v.colour[1] = rgba != nullptr ? Clamp01(rgba[index * 4 + 1]) : 1.0f;
            v.colour[2] = rgba != nullptr ? Clamp01(rgba[index * 4 + 2]) : 1.0f;
            v.colour[3] = rgba != nullptr ? Clamp01(rgba[index * 4 + 3]) : 1.0f;
        }

        if (valid)
        {
            vertices.push_back(tri[0]);
            vertices.push_back(tri[1]);
            vertices.push_back(tri[2]);
        }
    }

    if (!vertices.empty())
    {
        /*
         * Scene geometry, so it carries the GS TEST register the way the mesh
         * stream does.  Both callers -- textured billboards and the untextured
         * gouraud strips -- are drawn inside the 3D pass under whatever draw
         * env the builder last published, which on hardware is exactly what
         * the alpha test would have used.  Passing nullptr here (as this did)
         * is the 2D/UI rule applied one layer too far out.
         *
         * Safe by measurement rather than by argument: every TEST value this
         * game writes is GREATER/AREF=0, GEQUAL/AREF=1 or ALWAYS, so the
         * strictest cutoff reachable is "discard fully transparent" and no
         * value in the ROM can blank a primitive.
         */
        QueueTriangleList(texture, texture_width, texture_height,
                          vertices.data(), (int)vertices.size(),
                          depth_test != 0, depth_test != 0, false, nullptr,
                          g_gs_alpha_test, source, reversed);
    }
}

void MioPan_RendererDrawClipTriangles(const sceGsTex0 *tex0,
                                      const float *positions,
                                      const float *uv,
                                      const float *rgba,
                                      int vertex_count,
                                      int depth_test)
{
    DrawClipTrianglesImpl(tex0, positions, uv, rgba, vertex_count,
                          depth_test, DRAW_SOURCE_GENERIC);
}

void MioPan_RendererDrawBillboardTriangles(const sceGsTex0 *tex0,
                                           const float *positions,
                                           const float *uv,
                                           const float *rgba,
                                           int vertex_count,
                                           int depth_test,
                                           float ndc_z_bias)
{
    DrawClipTrianglesImpl(tex0, positions, uv, rgba, vertex_count,
                          depth_test, DRAW_SOURCE_BILLBOARD, ndc_z_bias);
}

/* The comparison the shader will run, evaluated on the CPU.  Kept in step with
 * MikuPanAlphaTestFails() in resources/shaders/hlsl/mikupan_common.hlsli. */
bool GsAlphaTestPasses(float alpha, float ref, unsigned int atst)
{
    switch (atst)
    {
    case 0:  return false;              /* NEVER    */
    case 1:  return true;               /* ALWAYS   */
    case 2:  return alpha <  ref;       /* LESS     */
    case 3:  return alpha <= ref;       /* LEQUAL   */
    case 4:  return alpha == ref;       /* EQUAL    */
    case 5:  return alpha >= ref;       /* GEQUAL   */
    case 6:  return alpha >  ref;       /* GREATER  */
    default: return alpha != ref;       /* NOTEQUAL */
    }
}

void MioPan_RendererSetGsTestRegister(unsigned long long test)
{
    /*
     * Unpacked by hand rather than through sceGsTest.  That struct is a
     * u_long bitfield sized for the EE, and how a host compiler lays its
     * storage units out is exactly the kind of thing that silently shifts a
     * field by four bits; the register's own layout is fixed and short enough
     * to read directly.
     */
    const unsigned int ate = (unsigned int)(test & 0x1u);
    const unsigned int atst = (unsigned int)((test >> 1) & 0x7u);
    const unsigned int aref = (unsigned int)((test >> 4) & 0xffu);
    const unsigned int afail = (unsigned int)((test >> 12) & 0x3u);
    const unsigned int zte = (unsigned int)((test >> 16) & 0x1u);
    const unsigned int ztst = (unsigned int)((test >> 17) & 0x3u);

    /*
     * ZTST, for the 2D primitives that use the depth buffer as a stencil.
     * Recorded before the alpha-test bail-outs below, which are about ATE/ATST
     * alone and say nothing about the depth side.
     *
     * ZTE off means the test is skipped entirely, which is ALWAYS here.  The
     * GS documents ZTE=0 as undefined and the engine never writes it, so this
     * is the harmless reading rather than a guess at hardware behaviour.
     */
    g_gs_ztst = zte != 0 ? (GsDepthCompare)ztst : GS_DEPTH_ALWAYS;

    /*
     * AFAIL says what a failing fragment still writes: KEEP (0) is the total
     * discard a cut-out wants; FB_ONLY (1) and RGB_ONLY (3) still write colour
     * and so are not a discard at all; ZB_ONLY (2) *is* a colour discard, but
     * one that keeps writing depth and cannot be spelled as a plain `discard`.
     * Treat all three as "no test", so a texture relying on them cannot
     * suddenly vanish.
     *
     * MEASURED: over a full session (574,618 TEST writes, 7 distinct values)
     * AFAIL was KEEP every single time, so this branch never fires in this
     * game.  An earlier note here claimed it was turning away a "more common
     * FB_ONLY form" -- there is no such traffic.  Kept as a guard for data
     * outside that sample, not as a live path.
     */
    const float ref = (float)aref / kPs2AlphaScale;

    /*
     * Refuse any configuration that would throw away a fully opaque fragment.
     *
     * AdjustPS2Alpha saturates PS2 alpha 128..255 to host 1.0, so the top half
     * of the GS's alpha range does not survive texture download.  A test the
     * hardware could satisfy up there becomes unsatisfiable here -- ATST
     * GREATER against AREF 0x80 turns into `alpha > 1.0`, which nothing passes,
     * and the whole surface disappears.  Checking the comparison against a
     * fully opaque fragment catches every such case in one rule, and errs
     * towards drawing: an un-discarded edge is a cosmetic artifact, a blanked
     * mesh looks like the model failed to load.
     *
     * The deliberate casualty is ATST=NEVER with AFAIL=KEEP, a genuine "draw
     * nothing".  The session sample carries no NEVER at all, and the highest
     * AREF anywhere in it is 0x01, so nothing in the ROM comes close to the
     * saturation edge this rule exists to catch.
     */
    if (ate == 0 || afail != 0 || !GsAlphaTestPasses(1.0f, ref, atst))
    {
        g_gs_alpha_test[0] = 0.0f;
        return;
    }

    g_gs_alpha_test[0] = 1.0f;
    g_gs_alpha_test[1] = ref;
    g_gs_alpha_test[2] = (float)atst;
    g_gs_alpha_test[3] = 0.0f;
}

void MioPan_RendererSetGsAlphaRegister(unsigned long long alpha)
{
    /* A / B / D: 0 = source colour, 1 = destination colour, 2 (and the
     * reserved 3, which the GS treats the same) = zero.
     * C:         0 = source alpha,  1 = destination alpha,  2/3 = FIX. */
    enum { COL_SRC = 0, COL_DST = 1, COL_ZERO = 2 };
    enum { CO_SRC_ALPHA = 0, CO_DST_ALPHA = 1, CO_FIX = 2 };

    unsigned int a = (unsigned int)(alpha & 0x3u);
    unsigned int b = (unsigned int)((alpha >> 2) & 0x3u);
    unsigned int c = (unsigned int)((alpha >> 4) & 0x3u);
    unsigned int d = (unsigned int)((alpha >> 6) & 0x3u);
    unsigned int fix = (unsigned int)((alpha >> 32) & 0xffu);

    if (a == 3) { a = COL_ZERO; }
    if (b == 3) { b = COL_ZERO; }
    if (d == 3) { d = COL_ZERO; }
    if (c == 3) { c = CO_FIX; }

    g_gs_blend_mode = GS_BLEND_ALPHA;

    /*
     * Only the source-alpha and FIX families are classified.  A destination-
     * alpha equation (C = Ad, which g2d_draw's shadowed-text path uses) is
     * expressible in SDL factors, but only if the target's alpha channel means
     * what the PS2's framebuffer alpha meant -- and it does not here, because
     * the swapchain's alpha is a by-product of the blend rules above rather
     * than something the engine maintains.  Reproducing the equation against
     * the wrong Ad would look worse than the ordinary alpha blend, so those
     * fall through to the default.
     */
    if (c == CO_SRC_ALPHA)
    {
        if (a == COL_SRC && b == COL_DST && d == COL_DST)
        {
            g_gs_blend_mode = GS_BLEND_ALPHA;        /* Cs*As + Cd*(1-As) */
        }
        else if (a == COL_SRC && b == COL_ZERO && d == COL_DST)
        {
            g_gs_blend_mode = GS_BLEND_ADD;          /* Cs*As + Cd        */
        }
        else if (a == COL_SRC && b == COL_DST && d == COL_ZERO)
        {
            g_gs_blend_mode = GS_BLEND_SUB;          /* Cs*As - Cd*As     */
        }
        else if (a == COL_ZERO && b == COL_DST && d == COL_DST)
        {
            g_gs_blend_mode = GS_BLEND_DST_DECAY;    /* Cd*(1-As)         */
        }
        else if (a == COL_SRC && b == COL_ZERO && d == COL_ZERO)
        {
            g_gs_blend_mode = GS_BLEND_SRC_ONLY;     /* Cs*As             */
        }
        else if (a == COL_DST && b == COL_ZERO && d == COL_DST)
        {
            /* Cd*(1 + As/128): the frame brightened by its own colour.
             * BrightnessAdjustmentFilterDraw's upper half (alphar 0x49) is
             * the only user; left on the fallback, its black quad *darkened*
             * the frame instead, which inverted the top half of the
             * brightness slider. */
            g_gs_blend_mode = GS_BLEND_DST_ADD;      /* Cd*As + Cd        */
        }
    }
    else if (c == CO_FIX)
    {
        /*
         * SDL_GPU has no blend-constant, so a FIX-weighted equation is only
         * reproducible at the extremes.  0x80 is 1.0 in the GS's 7-bit alpha
         * scale, and is what the engine uses when it wants a plain add; treat
         * anything at or above that as full strength and anything below as
         * the ordinary alpha blend rather than silently doubling an effect.
         */
        if (fix >= 0x80 && a == COL_SRC && b == COL_ZERO && d == COL_DST)
        {
            g_gs_blend_mode = GS_BLEND_ADD_ONE;      /* Cs + Cd */
        }
    }

}

void MioPan_RendererSetGsZbufRegister(unsigned long long zbuf)
{
    /*
     * ZMSK (bit 32) disables depth writes.  This is the register that lets a
     * transparent pass test against the depth buffer without occluding what is
     * drawn after it -- with depth-write tied to depth-test, as it was before
     * this, every alpha-blended surface stamped itself into the buffer.
     */
    g_gs_depth_write = ((zbuf >> 32) & 0x1u) == 0;
}

void MioPan_RendererSetGs2dDepth(unsigned int gs_z)
{
    g_gs_2d_z = gs_z;
    g_gs_2d_depth_armed = true;
}

void MioPan_RendererSetGsScissorRegister(unsigned long long scissor)
{
    g_gs_scissor.x0 = (int)(scissor & 0x7ffu);
    g_gs_scissor.x1 = (int)((scissor >> 16) & 0x7ffu);
    g_gs_scissor.y0 = (int)((scissor >> 32) & 0x7ffu);
    g_gs_scissor.y1 = (int)((scissor >> 48) & 0x7ffu);
}

int MioPan_RendererHasIndexedMesh(const void *owner,
                                  const void *vuvn,
                                  const void *mesh,
                                  unsigned int layout_kind,
                                  int estimated_vertex_count)
{
    if (owner == nullptr || vuvn == nullptr || mesh == nullptr ||
        (layout_kind != MIOPAN_MESH_CACHE_PRESET &&
         layout_kind != MIOPAN_MESH_CACHE_ANIMATED) || !g_frame_active ||
        !EnsureRenderer())
    {
        return 0;
    }
    if (!g_mesh_cache_available)
    {
        g_mesh_cache_misses++;
        g_mesh_cache_build_deferred++;
        return MIOPAN_MESH_CACHE_DEFER;
    }

    MeshCacheKey key = MakeMeshCacheKey(owner, vuvn, mesh, layout_kind);
    if (MeshCacheRetryBlocked(key))
    {
        g_mesh_cache_misses++;
        g_mesh_cache_build_deferred++;
        return MIOPAN_MESH_CACHE_DEFER;
    }
    auto cached = g_mesh_cache.find(key);
    if (cached != g_mesh_cache.end() && cached->second != nullptr &&
        ((cached->second->ready &&
          MeshCacheHasGpuStorage(*cached->second)) ||
         (!cached->second->ready &&
          MeshCacheHasPendingVertices(*cached->second) &&
          !cached->second->pending_indices.empty())))
    {
        cached->second->last_used_frame = g_mesh_cache_frame;
        g_mesh_cache_hits++;
        return 1;
    }
    if (cached != g_mesh_cache.end())
    {
        g_mesh_cache.erase(cached);
    }

    g_mesh_cache_misses++;
    if (g_mesh_cache_build_reservations.find(key) !=
        g_mesh_cache_build_reservations.end())
    {
        return 0;
    }
    if (estimated_vertex_count <= 0)
    {
        g_mesh_cache_build_deferred++;
        return MIOPAN_MESH_CACHE_DEFER;
    }

    const size_t vertex_size = layout_kind == MIOPAN_MESH_CACHE_ANIMATED
        ? sizeof(MeshAnimatedStaticVertex) : sizeof(MeshStaticVertex);
    const size_t vertex_count = (size_t)estimated_vertex_count;
    if (vertex_count > std::numeric_limits<size_t>::max() /
                           (vertex_size + 3u * sizeof(Uint32)))
    {
        g_mesh_cache_build_deferred++;
        return MIOPAN_MESH_CACHE_DEFER;
    }
    const size_t estimated_gpu_bytes =
        vertex_count * (vertex_size + 3u * sizeof(Uint32));
    if (estimated_gpu_bytes > std::numeric_limits<size_t>::max() / 2u)
    {
        g_mesh_cache_build_deferred++;
        return MIOPAN_MESH_CACHE_DEFER;
    }
    const size_t estimated_bytes = estimated_gpu_bytes * 2u;

    if (g_mesh_cache_build_start == 0)
    {
        g_mesh_cache_build_start = SDL_GetPerformanceCounter();
    }
    const Uint64 now = SDL_GetPerformanceCounter();
    const Uint64 frequency = SDL_GetPerformanceFrequency();
    const double elapsed_ms = frequency != 0
        ? (double)(now - g_mesh_cache_build_start) * 1000.0 /
              (double)frequency
        : 0.0;
    const bool entry_budget_exhausted =
        g_mesh_cache_build_entries >= kMeshCacheBuildBudgetEntries;
    const bool byte_budget_exhausted =
        estimated_bytes > kMeshCacheBuildBudgetBytes -
                              std::min(g_mesh_cache_build_bytes,
                                       kMeshCacheBuildBudgetBytes);
    const bool time_budget_exhausted =
        g_mesh_cache_build_entries != 0 &&
        elapsed_ms >= kMeshCacheBuildBudgetMilliseconds;
    if (entry_budget_exhausted || byte_budget_exhausted ||
        time_budget_exhausted)
    {
        g_mesh_cache_build_deferred++;
        return MIOPAN_MESH_CACHE_DEFER;
    }
    if (!AdmitMeshCacheEntry(estimated_bytes))
    {
        /* Current or in-flight draws can temporarily pin the logical budget.
         * Defer before topology decoding and retry next frame; this is normal
         * pressure, not a cache failure warranting a long suppression. */
        g_mesh_cache_build_deferred++;
        return MIOPAN_MESH_CACHE_DEFER;
    }

    try
    {
        auto inserted =
            g_mesh_cache_build_reservations.emplace(key, estimated_bytes);
        if (!inserted.second)
        {
            return 0;
        }
    }
    catch (const std::bad_alloc &)
    {
        g_mesh_cache_build_deferred++;
        return MIOPAN_MESH_CACHE_DEFER;
    }
    g_mesh_cache_reserved_bytes += estimated_bytes;
    g_mesh_cache_build_entries++;
    g_mesh_cache_build_bytes += estimated_bytes;
    return 0;
}

int MioPan_RendererDrawIndexedMesh(const void *owner,
                                   const void *vuvn,
                                   const void *mesh,
                                   unsigned int layout_kind,
                                   const sceGsTex0 *tex0,
                                   const float *positions,
                                   const float *uv,
                                   const float *normals,
                                   const float *colors,
                                   int vertex_count,
                                   const unsigned int *indices,
                                   int index_count,
                                   const float *local_world,
                                   const MioPanLightState *vertex_lights,
                                   const MioPanLightState *fragment_lights)
{
    const bool animated = layout_kind == MIOPAN_MESH_CACHE_ANIMATED;
    if (owner == nullptr || vuvn == nullptr || mesh == nullptr ||
        (layout_kind != MIOPAN_MESH_CACHE_PRESET && !animated) ||
        vertex_count <= 0 ||
        (animated && (positions == nullptr || normals == nullptr ||
                      vertex_lights == nullptr)) ||
        local_world == nullptr || !g_3d_camera_valid || !g_frame_active ||
        g_mesh_stream.token != 0 || !EnsureRenderer() ||
        !g_mesh_cache_available)
    {
        return 0;
    }

    MeshCacheKey key = MakeMeshCacheKey(owner, vuvn, mesh, layout_kind);
    if (MeshCacheRetryBlocked(key))
    {
        return 0;
    }

    /* Runtime positions can be redirected by morphing or rebuilt by skinning
     * on every draw.  A static index buffer cannot reproduce the old path's
     * pose-dependent null/nonfinite triangle filtering, so reject the entire
     * indexed fast path before mutating cache/frame state and let the caller
     * use its option-6 stream fallback. */
    if (animated)
    {
        for (int i = 0; i < vertex_count; i++)
        {
            const float *position = positions + (size_t)i * 3;
            const float *normal = normals + (size_t)i * 3;
            if (!std::isfinite(position[0]) ||
                !std::isfinite(position[1]) ||
                !std::isfinite(position[2]) ||
                !std::isfinite(normal[0]) ||
                !std::isfinite(normal[1]) ||
                !std::isfinite(normal[2]))
            {
                return 0;
            }
        }
    }
    auto cached = g_mesh_cache.find(key);
    MeshCacheEntryPtr entry;

    if (cached == g_mesh_cache.end())
    {
        auto reservation = g_mesh_cache_build_reservations.find(key);
        if (reservation == g_mesh_cache_build_reservations.end())
        {
            return 0;
        }
        const size_t reserved_bytes = reservation->second;
        g_mesh_cache_build_reservations.erase(reservation);
        g_mesh_cache_reserved_bytes =
            reserved_bytes <= g_mesh_cache_reserved_bytes
                ? g_mesh_cache_reserved_bytes - reserved_bytes : 0;
        if ((!animated && (positions == nullptr || normals == nullptr)) ||
            uv == nullptr ||
            indices == nullptr || index_count <= 0 ||
            (index_count % 3) != 0)
        {
            return positions == nullptr
                ? MIOPAN_MESH_DRAW_REBUILD : 0;
        }

        const size_t requested_vertex_bytes =
            (size_t)vertex_count * (animated
                ? sizeof(MeshAnimatedStaticVertex)
                : sizeof(MeshStaticVertex));
        const size_t requested_index_bytes =
            (size_t)index_count * sizeof(Uint32);
        if (requested_vertex_bytes > kMeshCacheMaxEntryBytes ||
            requested_index_bytes > kMeshCacheMaxEntryBytes ||
            requested_vertex_bytes + requested_index_bytes >
                kMeshCacheMaxEntryBytes)
        {
            return 0;
        }
        const size_t requested_gpu_bytes =
            requested_vertex_bytes + requested_index_bytes;
        if (requested_gpu_bytes >
            std::numeric_limits<size_t>::max() / 2u)
        {
            return 0;
        }
        const size_t requested_resident_bytes = requested_gpu_bytes * 2u;
        if (requested_resident_bytes > reserved_bytes)
        {
            /* The probe deliberately overestimates strip indices, so this is
             * only possible for malformed/mismatched metadata.  Stream this
             * draw and allow a clean probe next frame. */
            return 0;
        }

        std::unique_ptr<MeshCacheEntry> pending(new MeshCacheEntry{});
        pending->key = key;
        pending->vertex_count = (Uint32)vertex_count;
        pending->source_triangle_count = (Uint32)(index_count / 3);
        pending->last_used_frame = g_mesh_cache_frame;
        if (animated)
        {
            pending->pending_animated_vertices.resize((size_t)vertex_count);
        }
        else
        {
            pending->pending_vertices.resize((size_t)vertex_count);
        }

        std::vector<unsigned char> finite_positions;
        if (!animated)
        {
            finite_positions.resize((size_t)vertex_count, 0);
        }
        for (int i = 0; i < vertex_count; i++)
        {
            const float *position = positions + (size_t)i * 3;
            bool finite = true;
            float *cached_uv;
            if (animated)
            {
                MeshAnimatedStaticVertex &vertex =
                    pending->pending_animated_vertices[(size_t)i];
                cached_uv = vertex.uv;
            }
            else
            {
                MeshStaticVertex &vertex =
                    pending->pending_vertices[(size_t)i];
                finite = std::isfinite(position[0]) &&
                         std::isfinite(position[1]) &&
                         std::isfinite(position[2]);
                finite_positions[(size_t)i] = finite ? 1 : 0;
                vertex.position[0] = finite ? position[0] : 0.0f;
                vertex.position[1] = finite ? position[1] : 0.0f;
                vertex.position[2] = finite ? position[2] : 0.0f;
                vertex.position[3] = 1.0f;
                const float *normal = normals + (size_t)i * 3;
                const bool finite_normal = std::isfinite(normal[0]) &&
                                           std::isfinite(normal[1]) &&
                                           std::isfinite(normal[2]);
                vertex.normal[0] = finite_normal ? normal[0] : 0.0f;
                vertex.normal[1] = finite_normal ? normal[1] : 0.0f;
                vertex.normal[2] = finite_normal ? normal[2] : 1.0f;
                vertex.normal[3] = 0.0f;
                cached_uv = vertex.uv;
            }

            for (int component = 0; component < 2; component++)
            {
                const float value = uv != nullptr
                    ? uv[(size_t)i * 2 + (size_t)component] : 0.0f;
                cached_uv[component] =
                    std::isfinite(value) ? value : 0.0f;
            }
            cached_uv[2] = 0.0f;
            cached_uv[3] = 0.0f;
        }

        pending->pending_indices.reserve((size_t)index_count);
        for (int triangle = 0; triangle < index_count; triangle += 3)
        {
            Uint32 i0 = indices[triangle + 0];
            Uint32 i1 = indices[triangle + 1];
            Uint32 i2 = indices[triangle + 2];
            const bool indices_valid = i0 < (Uint32)vertex_count &&
                                       i1 < (Uint32)vertex_count &&
                                       i2 < (Uint32)vertex_count;
            if (animated && !indices_valid)
            {
                return 0;
            }
            const bool valid = indices_valid &&
                               (animated ||
                                (finite_positions[i0] != 0 &&
                                 finite_positions[i1] != 0 &&
                                 finite_positions[i2] != 0));
            if (!valid)
            {
                pending->clipped_triangle_count++;
                continue;
            }
            pending->pending_indices.push_back(i0);
            pending->pending_indices.push_back(i1);
            pending->pending_indices.push_back(i2);
        }

        if (pending->pending_indices.empty())
        {
            g_mesh_triangles_submitted +=
                (int)pending->source_triangle_count;
            g_mesh_triangles_clipped +=
                (int)pending->source_triangle_count;
            return 1;
        }
        pending->index_count = (Uint32)pending->pending_indices.size();
        pending->cached_bytes = requested_gpu_bytes;
        /* Every entry retains one CPU topology copy after promotion.  It is a
         * compact, bounded insurance path: a transient shared-stream upload
         * failure can expand the current draw instead of hiding a room or
         * character for a frame. */
        pending->resident_bytes = requested_resident_bytes;

        entry = AdoptMeshCacheEntry(pending.release());
        auto inserted = g_mesh_cache.emplace(key, entry);
        if (!inserted.second)
        {
            entry = inserted.first->second;
        }
        else
        {
            g_mesh_cache_creates++;
        }
    }
    else
    {
        entry = cached->second;
    }

    if (entry == nullptr || entry->vertex_count != (Uint32)vertex_count ||
        (!entry->ready &&
         (!MeshCacheHasPendingVertices(*entry) ||
          entry->pending_indices.empty())) ||
        (entry->ready && !MeshCacheHasGpuStorage(*entry)))
    {
        return MIOPAN_MESH_DRAW_REBUILD;
    }

    float mvp[16];
    MulMatrixRowMajor(mvp, local_world, g_3d_view_projection);
    if (!MatrixIsFinite(mvp))
    {
        g_mesh_triangles_submitted += (int)entry->source_triangle_count;
        g_mesh_triangles_clipped += (int)entry->source_triangle_count;
        return 1;
    }

    Uint32 first_colour = 0;
    Uint32 first_animated_vertex = 0;
    Uint32 vertex_light_index = 0;
    if (animated)
    {
        const size_t max_vertices =
            (size_t)std::numeric_limits<Uint32>::max() /
            sizeof(AnimatedMeshVertex);
        const bool reuse_light_state =
            !g_vertex_light_states.empty() &&
            std::memcmp(&g_vertex_light_states.back(), vertex_lights,
                        sizeof(*vertex_lights)) == 0;
        if ((size_t)vertex_count > max_vertices ||
            g_animated_mesh_vertices_stream.size() >
                max_vertices - (size_t)vertex_count ||
            (!reuse_light_state && g_vertex_light_states.size() >=
                (size_t)std::numeric_limits<Uint32>::max()))
        {
            return 0;
        }
        first_animated_vertex =
            (Uint32)g_animated_mesh_vertices_stream.size();
        g_animated_mesh_vertices_stream.resize(
            g_animated_mesh_vertices_stream.size() + (size_t)vertex_count);
        for (int i = 0; i < vertex_count; i++)
        {
            AnimatedMeshVertex &vertex =
                g_animated_mesh_vertices_stream[
                    (size_t)first_animated_vertex + (size_t)i];
            const float *position = positions + (size_t)i * 3;
            vertex.position[0] = position[0];
            vertex.position[1] = position[1];
            vertex.position[2] = position[2];
            vertex.position[3] = 1.0f;

            const float *normal = normals + (size_t)i * 3;
            vertex.normal[0] = normal[0];
            vertex.normal[1] = normal[1];
            vertex.normal[2] = normal[2];
            vertex.normal[3] = 0.0f;
        }
        if (reuse_light_state)
        {
            vertex_light_index =
                (Uint32)(g_vertex_light_states.size() - 1u);
        }
        else
        {
            vertex_light_index = (Uint32)g_vertex_light_states.size();
            g_vertex_light_states.push_back(*vertex_lights);
        }
    }
    else
    {
        const size_t max_vertices =
            (size_t)std::numeric_limits<Uint32>::max() / sizeof(MeshColour);
        if ((size_t)vertex_count > max_vertices ||
            g_mesh_colours.size() > max_vertices - (size_t)vertex_count)
        {
            return 0;
        }
        first_colour = (Uint32)g_mesh_colours.size();
        g_mesh_colours.resize(g_mesh_colours.size() + (size_t)vertex_count);
        for (int i = 0; i < vertex_count; i++)
        {
            MeshColour &colour =
                g_mesh_colours[(size_t)first_colour + (size_t)i];
            const float r = colors != nullptr
                ? colors[(size_t)i * 4 + 0] : 1.0f;
            const float g = colors != nullptr
                ? colors[(size_t)i * 4 + 1] : 1.0f;
            const float b = colors != nullptr
                ? colors[(size_t)i * 4 + 2] : 1.0f;
            const float a = colors != nullptr
                ? colors[(size_t)i * 4 + 3] : 1.0f;
            colour.rgba[0] = std::isfinite(r)
                ? Clamp(r, 0.0f, 2.0f) : 1.0f;
            colour.rgba[1] = std::isfinite(g)
                ? Clamp(g, 0.0f, 2.0f) : 1.0f;
            colour.rgba[2] = std::isfinite(b)
                ? Clamp(b, 0.0f, 2.0f) : 1.0f;
            colour.rgba[3] = std::isfinite(a) ? Clamp01(a) : 1.0f;
        }
    }

    TextureEntry *texture_entry = tex0 != nullptr ? GetTexture(tex0) : nullptr;
    SDL_GPUTexture *texture = texture_entry != nullptr
        ? texture_entry->texture : g_white_texture;
    int texture_width = texture_entry != nullptr ? texture_entry->width : 1;
    int texture_height = texture_entry != nullptr ? texture_entry->height : 1;
    if (g_dbg_flatcolor)
    {
        texture = g_white_texture;
        texture_width = 1;
        texture_height = 1;
    }

    DrawCommand command{};
    command.texture = texture;
    command.texture_width = texture_width;
    command.texture_height = texture_height;
    command.depth_test = true;
    command.repeat_uv = true;
    command.min_linear = true;
    command.mag_linear = true;
    command.preserve_original_aspect = false;
    command.transform_mesh = true;
    command.indexed_mesh = true;
    command.animated_mesh = animated;
    TagShadowPass(command);
    command.first_mesh_colour = first_colour;
    command.first_animated_mesh_vertex = first_animated_vertex;
    command.cached_mesh = entry;
    command.vertex_light_index = vertex_light_index;
    /* Animated draws take the vertex path for now: CreatePipelineVariant()
     * has no fragment-lit variant of the animated layout. */
    SnapshotFragmentLights(command, animated ? nullptr : fragment_lights);
    ApplyGsDrawEnv(command);
    std::memcpy(command.mvp, mvp, sizeof(command.mvp));
    std::memcpy(command.model, local_world, sizeof(command.model));
    std::memcpy(command.alpha_test, g_gs_alpha_test,
                sizeof(command.alpha_test));
    g_draws.push_back(std::move(command));

    entry->last_used_frame = g_mesh_cache_frame;
    g_mesh_triangles_submitted += (int)entry->source_triangle_count;
    g_mesh_triangles_clipped += (int)entry->clipped_triangle_count;
    const uint64_t expanded_vertices =
        (uint64_t)entry->source_triangle_count * 3u;
    if (expanded_vertices > entry->vertex_count)
    {
        g_mesh_expanded_vertices_avoided +=
            expanded_vertices - entry->vertex_count;
    }
    if (animated)
    {
        g_animated_mesh_vertices += entry->vertex_count;
        if (expanded_vertices > entry->vertex_count)
        {
            g_animated_mesh_expanded_vertices_avoided +=
                expanded_vertices - entry->vertex_count;
        }
    }
    return 1;
}

void MioPan_RendererInvalidateMeshCache(const void *owner)
{
    if (owner == nullptr)
    {
        return;
    }

    bool generation_advanced = false;
    auto generation = g_mesh_cache_owner_generations.find(owner);
    if (generation != g_mesh_cache_owner_generations.end())
    {
        if (generation->second != std::numeric_limits<uint64_t>::max())
        {
            generation->second++;
            generation_advanced = true;
        }
        else
        {
            g_mesh_cache_owner_generations.erase(generation);
        }
    }
    else
    {
        try
        {
            g_mesh_cache_owner_generations.emplace(owner, 1u);
            generation_advanced = true;
        }
        catch (const std::bad_alloc &)
        {
            /* Fall through to the old exact-owner erase.  Allocation failure
             * must cost time, never permit pointer-reused geometry to hit. */
        }
    }

    if (!generation_advanced)
    {
        for (auto it = g_mesh_cache.begin(); it != g_mesh_cache.end();)
        {
            if (it->first.owner == owner)
            {
                it = g_mesh_cache.erase(it);
            }
            else
            {
                ++it;
            }
        }
        for (auto it = g_mesh_cache_retry_after.begin();
             it != g_mesh_cache_retry_after.end();)
        {
            if (it->first.owner == owner)
            {
                it = g_mesh_cache_retry_after.erase(it);
            }
            else
            {
                ++it;
            }
        }
    }

    /* Reservations are capped at four, so cancelling them synchronously is
     * bounded.  Cached GPU entries are instead retired by the generation bump
     * and reclaimed progressively by the existing eight-victim LRU budget. */
    for (auto it = g_mesh_cache_build_reservations.begin();
         it != g_mesh_cache_build_reservations.end();)
    {
        if (it->first.owner == owner)
        {
            const size_t reserved_bytes = it->second;
            it = g_mesh_cache_build_reservations.erase(it);
            g_mesh_cache_reserved_bytes =
                reserved_bytes <= g_mesh_cache_reserved_bytes
                    ? g_mesh_cache_reserved_bytes - reserved_bytes : 0;
        }
        else
        {
            ++it;
        }
    }
    g_mesh_cache_invalidations++;
}

unsigned int MioPan_RendererBeginMeshStream(const sceGsTex0 *tex0,
                                              int expected_vertex_count,
                                              const float *local_world,
                                              const MioPanLightState *fragment_lights,
                                              const void *texture)
{
    if (expected_vertex_count <= 0 || (expected_vertex_count % 3) != 0 ||
        local_world == nullptr || !g_3d_camera_valid || !g_frame_active ||
        g_mesh_stream.token != 0 || !EnsureRenderer() ||
        g_vertices.size() >
            (size_t)std::numeric_limits<Uint32>::max() -
                (size_t)expected_vertex_count)
    {
        return 0;
    }

    MeshStreamState pending{};
    pending.first_vertex = (Uint32)g_vertices.size();
    pending.max_vertices = (Uint32)expected_vertex_count;
    pending.command.first_vertex = pending.first_vertex;
    pending.command.depth_test = true;
    pending.command.repeat_uv = true;
    pending.command.min_linear = true;
    pending.command.mag_linear = true;
    pending.command.preserve_original_aspect = false;
    pending.command.transform_mesh = true;
    TagShadowPass(pending.command);
    SnapshotFragmentLights(pending.command, fragment_lights);
    ApplyGsDrawEnv(pending.command);

    /* Snapshot all draw state before the SGD walker can advance to another
     * mesh.  Option 1 reduced this to one matrix product per mesh. */
    MulMatrixRowMajor(pending.command.mvp, local_world,
                      g_3d_view_projection);
    std::memcpy(pending.command.model, local_world,
                sizeof(pending.command.model));
    pending.discard_all = !MatrixIsFinite(pending.command.mvp);
    std::memcpy(pending.command.alpha_test, g_gs_alpha_test,
                sizeof(pending.command.alpha_test));

    if (!pending.discard_all)
    {
        ApplyMeshTexture(pending.command, tex0, texture);
    }
    else
    {
        pending.command.texture = g_white_texture;
        pending.command.texture_width = 1;
        pending.command.texture_height = 1;
    }

    try
    {
        if (!pending.discard_all)
        {
            ReserveGeometric(g_vertices,
                             g_vertices.size() +
                                 (size_t)expected_vertex_count,
                             4096);
        }
        ReserveGeometric(g_draws, g_draws.size() + 1, 1024);
    }
    catch (const std::bad_alloc &)
    {
        return 0;
    }

    do
    {
        pending.token = g_next_mesh_stream_token++;
    } while (pending.token == 0);
    g_mesh_stream = pending;
    return pending.token;
}

void MioPan_RendererAppendMeshTriangle(
    unsigned int stream,
    const MioPanMeshVertexInput vertices[3])
{
    if (stream == 0 || stream != g_mesh_stream.token || vertices == nullptr)
    {
        return;
    }

    g_mesh_stream.submitted_triangles++;
    const size_t emitted =
        g_vertices.size() - (size_t)g_mesh_stream.first_vertex;
    bool valid = !g_mesh_stream.discard_all &&
                 emitted <= (size_t)g_mesh_stream.max_vertices - 3u;
    for (int i = 0; i < 3; i++)
    {
        const float *position = vertices[i].position;
        valid = valid && position != nullptr &&
                std::isfinite(position[0]) &&
                std::isfinite(position[1]) &&
                std::isfinite(position[2]);
    }
    if (!valid)
    {
        g_mesh_stream.clipped_triangles++;
        return;
    }

    for (int i = 0; i < 3; i++)
    {
        const MioPanMeshVertexInput &input = vertices[i];
        SpriteVertex vertex;
        vertex.position[0] = input.position[0];
        vertex.position[1] = input.position[1];
        vertex.position[2] = input.position[2];
        vertex.position[3] = 1.0f;
        vertex.uv[0] = input.s;
        vertex.uv[1] = input.t;
        vertex.uv[2] = 0.0f;
        vertex.uv[3] = 0.0f;
        if (g_mesh_stream.command.fragment_lighting)
        {
            EncodeMeshNormal(vertex.uv, input.normal);
        }

        const float r = input.rgb != nullptr ? input.rgb[0] : 1.0f;
        const float g = input.rgb != nullptr ? input.rgb[1] : 1.0f;
        const float b = input.rgb != nullptr ? input.rgb[2] : 1.0f;
        vertex.colour[0] = std::isfinite(r) ? Clamp(r, 0.0f, 2.0f) : 1.0f;
        vertex.colour[1] = std::isfinite(g) ? Clamp(g, 0.0f, 2.0f) : 1.0f;
        vertex.colour[2] = std::isfinite(b) ? Clamp(b, 0.0f, 2.0f) : 1.0f;
        vertex.colour[3] = std::isfinite(input.alpha)
            ? Clamp01(input.alpha) : 1.0f;
        g_vertices.push_back(vertex);
    }
}

void MioPan_RendererCommitMeshStream(unsigned int stream)
{
    if (stream == 0 || stream != g_mesh_stream.token)
    {
        return;
    }

    g_mesh_triangles_submitted += g_mesh_stream.submitted_triangles;
    g_mesh_triangles_clipped += g_mesh_stream.clipped_triangles;
    const size_t vertex_count =
        g_vertices.size() - (size_t)g_mesh_stream.first_vertex;
    if (vertex_count != 0 && (vertex_count % 3) == 0 &&
        vertex_count <= std::numeric_limits<Uint32>::max())
    {
        g_mesh_stream.command.vertex_count = (Uint32)vertex_count;
        g_draws.push_back(g_mesh_stream.command);
        g_mesh_direct_stream_vertices += vertex_count;
    }
    else if (vertex_count != 0)
    {
        g_vertices.resize(g_mesh_stream.first_vertex);
    }
    g_mesh_stream = MeshStreamState{};
}

void MioPan_RendererAbortMeshStream(unsigned int stream)
{
    if (stream != 0 && stream == g_mesh_stream.token)
    {
        AbortActiveMeshStream();
    }
}

void MioPan_RendererDrawMeshTriangles(const sceGsTex0 *tex0,
                                      const float *positions,
                                      const float *uv,
                                      const float *rgba,
                                      int vertex_count,
                                      const float *local_world)
{
    if (positions == nullptr || vertex_count <= 0 ||
        (vertex_count % 3) != 0)
    {
        return;
    }

    const unsigned int stream = MioPan_RendererBeginMeshStream(
        tex0, vertex_count, local_world, nullptr);
    if (stream == 0)
    {
        return;
    }

    for (int i = 0; i < vertex_count; i += 3)
    {
        MioPanMeshVertexInput triangle[3]{};
        for (int j = 0; j < 3; j++)
        {
            const int index = i + j;
            triangle[j].position = positions + (size_t)index * 3;
            triangle[j].rgb = rgba != nullptr
                ? rgba + (size_t)index * 4 : nullptr;
            triangle[j].s = uv != nullptr ? uv[(size_t)index * 2 + 0] : 0.0f;
            triangle[j].t = uv != nullptr ? uv[(size_t)index * 2 + 1] : 0.0f;
            triangle[j].alpha = rgba != nullptr
                ? rgba[(size_t)index * 4 + 3] : 1.0f;
        }
        MioPan_RendererAppendMeshTriangle(stream, triangle);
    }
    MioPan_RendererCommitMeshStream(stream);
}

unsigned int MioPan_RendererCreateResidentMesh(
    const MioPanResidentVertex *vertices,
    unsigned int vertex_count,
    const unsigned int *indices,
    unsigned int index_count)
{
    if (vertices == nullptr || indices == nullptr || vertex_count == 0 ||
        index_count == 0 || (index_count % 3u) != 0 || !EnsureRenderer())
    {
        return 0;
    }

    const size_t vertex_bytes =
        (size_t)vertex_count * sizeof(MeshStaticVertex);
    const size_t index_bytes = (size_t)index_count * sizeof(Uint32);
    const size_t index_offset = AlignUploadOffset(vertex_bytes);
    if (vertex_bytes > std::numeric_limits<Uint32>::max() ||
        index_bytes > std::numeric_limits<Uint32>::max() ||
        index_offset > std::numeric_limits<Uint32>::max() - index_bytes)
    {
        return 0;
    }
    /* Validated here, once, so no draw ever has to: an index past the end
     * would read another mesh's vertices, or nothing at all. */
    for (unsigned int i = 0; i < index_count; i++)
    {
        if (indices[i] >= vertex_count)
        {
            return 0;
        }
    }

    ResidentMeshPtr mesh;
    try
    {
        mesh = std::make_shared<ResidentMesh>();
    }
    catch (const std::bad_alloc &)
    {
        return 0;
    }

    SDL_GPUBufferCreateInfo info{};
    info.usage = SDL_GPU_BUFFERUSAGE_VERTEX;
    info.size = (Uint32)vertex_bytes;
    mesh->vertex_buffer = SDL_CreateGPUBuffer(g_device, &info);
    info.usage = SDL_GPU_BUFFERUSAGE_INDEX;
    info.size = (Uint32)index_bytes;
    mesh->index_buffer = mesh->vertex_buffer != nullptr
        ? SDL_CreateGPUBuffer(g_device, &info) : nullptr;
    /* From here on the destructor owns whatever was created, so every
     * failure below is a plain return. */
    mesh->vertex_count = vertex_count;
    mesh->index_count = index_count;
    mesh->bytes = vertex_bytes + index_bytes;
    g_resident_mesh_bytes += mesh->bytes;
    if (mesh->vertex_buffer == nullptr || mesh->index_buffer == nullptr)
    {
        LogSdlError("SDL_CreateGPUBuffer(resident mesh)");
        return 0;
    }

    SDL_GPUTransferBufferCreateInfo transfer_info{};
    transfer_info.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
    transfer_info.size = (Uint32)(index_offset + index_bytes);
    SDL_GPUTransferBuffer *transfer =
        SDL_CreateGPUTransferBuffer(g_device, &transfer_info);
    if (transfer == nullptr)
    {
        LogSdlError("SDL_CreateGPUTransferBuffer(resident mesh)");
        return 0;
    }
    unsigned char *mapped = static_cast<unsigned char *>(
        SDL_MapGPUTransferBuffer(g_device, transfer, false));
    if (mapped == nullptr)
    {
        LogSdlError("SDL_MapGPUTransferBuffer(resident mesh)");
        SDL_ReleaseGPUTransferBuffer(g_device, transfer);
        return 0;
    }
    std::memcpy(mapped, vertices, vertex_bytes);
    std::memcpy(mapped + index_offset, indices, index_bytes);
    SDL_UnmapGPUTransferBuffer(g_device, transfer);

    /*
     * Its own submission, now, rather than a slot in the frame's upload stage.
     * Two reasons.  The caller is the SGD walker in the middle of building the
     * frame, and a mesh that cannot be made has to be known right here, where
     * it can still stream this unit, not later in EndFrame where it could only
     * be dropped.  And nothing then depends on which present -- or which
     * skipped one -- first runs an upload stage.  Command buffers execute in
     * submission order, so the frame's own buffer, submitted later, sees the
     * data; RecordCopyOnlyUploads() already relies on exactly that.
     */
    SDL_GPUCommandBuffer *cmd = SDL_AcquireGPUCommandBuffer(g_device);
    if (cmd == nullptr)
    {
        LogSdlError("SDL_AcquireGPUCommandBuffer(resident mesh)");
        SDL_ReleaseGPUTransferBuffer(g_device, transfer);
        return 0;
    }
    SDL_GPUCopyPass *copy = SDL_BeginGPUCopyPass(cmd);
    if (copy == nullptr)
    {
        LogSdlError("SDL_BeginGPUCopyPass(resident mesh)");
        SDL_CancelGPUCommandBuffer(cmd);
        SDL_ReleaseGPUTransferBuffer(g_device, transfer);
        return 0;
    }
    SDL_GPUTransferBufferLocation src{};
    src.transfer_buffer = transfer;
    src.offset = 0;
    SDL_GPUBufferRegion dst{};
    dst.buffer = mesh->vertex_buffer;
    dst.offset = 0;
    dst.size = (Uint32)vertex_bytes;
    SDL_UploadToGPUBuffer(copy, &src, &dst, false);
    src.offset = (Uint32)index_offset;
    dst.buffer = mesh->index_buffer;
    dst.size = (Uint32)index_bytes;
    SDL_UploadToGPUBuffer(copy, &src, &dst, false);
    SDL_EndGPUCopyPass(copy);
    const bool submitted = SDL_SubmitGPUCommandBuffer(cmd);
    SDL_ReleaseGPUTransferBuffer(g_device, transfer);
    if (!submitted)
    {
        LogSdlError("SDL_SubmitGPUCommandBuffer(resident mesh)");
        return 0;
    }

    unsigned int id = g_next_resident_mesh_id;
    while (id == 0 || g_resident_meshes.find(id) != g_resident_meshes.end())
    {
        id++;
    }
    try
    {
        g_resident_meshes.emplace(id, std::move(mesh));
    }
    catch (const std::bad_alloc &)
    {
        return 0;
    }
    g_next_resident_mesh_id = id + 1u;
    g_resident_creates++;
    return id;
}

void MioPan_RendererReleaseResidentMesh(unsigned int mesh)
{
    if (mesh != 0)
    {
        g_resident_meshes.erase(mesh);
    }
}

int MioPan_RendererDrawResidentMesh(unsigned int mesh_id,
                                    unsigned int first_vertex,
                                    unsigned int vertex_count,
                                    unsigned int first_index,
                                    unsigned int index_count,
                                    const float *rgba,
                                    const sceGsTex0 *tex0,
                                    const float *local_world,
                                    const MioPanLightState *fragment_lights,
                                    const void *texture)
{
    if (!g_resident_meshes_enabled || rgba == nullptr ||
        local_world == nullptr || vertex_count == 0 || index_count == 0 ||
        (index_count % 3u) != 0 || !g_3d_camera_valid || !g_frame_active ||
        g_mesh_stream.token != 0 || !EnsureRenderer())
    {
        return 0;
    }

    const auto found = g_resident_meshes.find(mesh_id);
    if (found == g_resident_meshes.end() || found->second == nullptr)
    {
        return 0;
    }
    const ResidentMeshPtr &mesh = found->second;
    const size_t colour_limit =
        (size_t)std::numeric_limits<Uint32>::max() / sizeof(MeshColour);
    if ((uint64_t)first_vertex + vertex_count > mesh->vertex_count ||
        (uint64_t)first_index + index_count > mesh->index_count ||
        g_mesh_colours.size() > colour_limit - vertex_count)
    {
        return 0;
    }
    try
    {
        ReserveGeometric(g_mesh_colours,
                         g_mesh_colours.size() + (size_t)vertex_count, 4096);
        ReserveGeometric(g_draws, g_draws.size() + 1, 1024);
    }
    catch (const std::bad_alloc &)
    {
        return 0;
    }

    /* The same state capture MioPan_RendererBeginMeshStream() makes, in the
     * same order, so a resident draw and a streamed one of the same unit
     * differ in where their vertices live and in nothing else. */
    DrawCommand command{};
    command.depth_test = true;
    command.repeat_uv = true;
    command.min_linear = true;
    command.mag_linear = true;
    command.preserve_original_aspect = false;
    command.transform_mesh = true;
    TagShadowPass(command);
    SnapshotFragmentLights(command, fragment_lights);
    ApplyGsDrawEnv(command);
    MulMatrixRowMajor(command.mvp, local_world, g_3d_view_projection);
    std::memcpy(command.model, local_world, sizeof(command.model));
    std::memcpy(command.alpha_test, g_gs_alpha_test,
                sizeof(command.alpha_test));

    const int triangles = (int)(index_count / 3u);
    if (!MatrixIsFinite(command.mvp))
    {
        /* The streamed path clips every triangle of a draw like this one. */
        g_mesh_triangles_submitted += triangles;
        g_mesh_triangles_clipped += triangles;
        return 1;
    }

    ApplyMeshTexture(command, tex0, texture);

    command.resident_mesh = true;
    command.resident = mesh;
    command.resident_first_vertex = first_vertex;
    command.resident_vertex_count = vertex_count;
    command.first_index = first_index;
    command.index_count = index_count;
    command.first_mesh_colour = (Uint32)g_mesh_colours.size();

    /* The streamed path's own clamps (MioPan_RendererAppendMeshTriangle). */
    for (unsigned int i = 0; i < vertex_count; i++)
    {
        const float *in = rgba + (size_t)i * 4u;
        MeshColour colour;
        colour.rgba[0] = std::isfinite(in[0]) ? Clamp(in[0], 0.0f, 2.0f) : 1.0f;
        colour.rgba[1] = std::isfinite(in[1]) ? Clamp(in[1], 0.0f, 2.0f) : 1.0f;
        colour.rgba[2] = std::isfinite(in[2]) ? Clamp(in[2], 0.0f, 2.0f) : 1.0f;
        colour.rgba[3] = std::isfinite(in[3]) ? Clamp01(in[3]) : 1.0f;
        g_mesh_colours.push_back(colour);
    }
    g_mesh_triangles_submitted += triangles;
    g_resident_units++;

    if (!g_draws.empty() && ResidentDrawContinues(g_draws.back(), command))
    {
        DrawCommand &previous = g_draws.back();
        previous.index_count += index_count;
        previous.resident_vertex_count += vertex_count;
        return 1;
    }
    g_draws.push_back(std::move(command));
    g_resident_draws++;
    return 1;
}

void MioPan_RendererSetResidentMeshes(int enable)
{
    g_resident_meshes_enabled = enable != 0;
}

int MioPan_RendererGetResidentMeshes(void)
{
    return g_resident_meshes_enabled ? 1 : 0;
}

unsigned long long MioPan_RendererGetFrameIndex(void)
{
    return g_frame_index;
}

const void *MioPan_RendererResolveTexture(const sceGsTex0 *tex0)
{
    if (tex0 == nullptr || !EnsureRenderer() || IsVideoTex0(tex0))
    {
        return nullptr;
    }
    /* Every entry GetTexture() hands out for a non-video TEX0 lives in
     * g_texture_cache, which is node-based and never evicts, so the pointer
     * is good for the rest of the session. */
    TextureEntry *entry = GetTexture(tex0);
    return entry != nullptr && entry->texture != nullptr ? entry : nullptr;
}

void MioPan_RendererSetResidentTextures(int enable)
{
    g_resident_textures_enabled = enable != 0;
}

int MioPan_RendererGetResidentTextures(void)
{
    return g_resident_textures_enabled ? 1 : 0;
}

void MioPan_RendererCountTextureCapture(void)
{
    g_resident_texture_captures++;
}

void MioPan_RendererCountSkippedTextureUpload(void)
{
    g_resident_texture_skips++;
}

void MioPan_RendererCountTextureReplay(void)
{
    g_resident_texture_replays++;
}
}
