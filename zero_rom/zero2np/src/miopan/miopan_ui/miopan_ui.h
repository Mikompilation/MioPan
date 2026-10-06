#ifndef MIOPAN_UI_H
#define MIOPAN_UI_H

#include <SDL3/SDL_events.h>
#include <SDL3/SDL_gpu.h>
#include <SDL3/SDL_video.h>

#include "../miopan_profiler.h"

#include <cstddef>

namespace MioPanUi
{
struct PerformanceStats
{
    double game_fps;
    double present_fps;
    MioPanProfilerStats profiler;
    std::size_t draw_count;
    std::size_t vertex_count;
    int mesh_triangles_submitted;
    int mesh_triangles_clipped;
    int mesh_cache_hits;
    int mesh_cache_misses;
    int mesh_cache_creates;
    int mesh_cache_evictions;
    int mesh_cache_invalidations;
    int mesh_cache_upload_failures;
    int mesh_cache_build_deferred;
    int mesh_cache_upload_deferred;
    int mesh_cache_promotions;
    std::size_t mesh_cache_entries;
    std::size_t mesh_cache_pending_entries;
    std::size_t mesh_cache_bytes;
    std::size_t mesh_arena_bytes;
    std::size_t mesh_arena_pages;
    std::size_t mesh_cache_upload_bytes;
    std::size_t mesh_colour_upload_bytes;
    std::size_t animated_mesh_upload_bytes;
    std::size_t mesh_expanded_vertices_avoided;
    std::size_t mesh_direct_stream_vertices;
    std::size_t animated_mesh_vertices;
    std::size_t animated_mesh_expanded_vertices_avoided;
    /* Resident meshes: how many are live and their GPU size, and over the
     * stats window how many walker units they served, how many draws those
     * units became, and how many meshes were built. */
    std::size_t resident_meshes;
    std::size_t resident_bytes;
    int resident_units;
    int resident_draws;
    int resident_creates;
    /* Resolved textures over the window: models bound, TRI2 uploads then
     * skipped, and skipped runs replayed for a draw that sampled GS memory;
     * and everything the GS was sent anyway. */
    int resident_texture_captures;
    int resident_texture_skips;
    int resident_texture_replays;
    std::size_t gs_uploads;
    std::size_t gs_upload_bytes;
    /* Logical frames in the stats window, to turn the counts above into
     * per-frame figures. */
    int window_frames;
    int texture_l1_hits;
    int texture_l1_lookups;
    int texture_l2_hits;
    int texture_creates;
    int texture_downloads;
    int texture_invalidations;
    int font_texture_hits;
    int font_texture_selects;
    int font_texture_creates;
    int font_texture_invalidations;
};

bool Init(SDL_Window *window, SDL_GPUDevice *device,
          SDL_GPUTextureFormat swapchain_format);
void Shutdown();

void SetPerformanceStats(const PerformanceStats &stats);
void ProcessEvent(const SDL_Event *event);
void BeginFrame();
void Draw();
void EndFrame();

bool PrepareDrawData(SDL_GPUCommandBuffer *command_buffer);
void RenderDrawData(SDL_GPUCommandBuffer *command_buffer,
                    SDL_GPURenderPass *render_pass);
}

#endif
