#ifndef MIOPAN_PROFILER_H
#define MIOPAN_PROFILER_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum MioPanProfilePhase
{
    MIOPAN_PROFILE_SGD_CPU = 0,
    MIOPAN_PROFILE_BILLBOARD_CPU,
    MIOPAN_PROFILE_BILLBOARD_HOST_BRIDGE,
    MIOPAN_PROFILE_SKY_CPU,
    MIOPAN_PROFILE_MAPPUT_SORT,
    MIOPAN_PROFILE_MAPPUT_CALLBACK,
    MIOPAN_PROFILE_ROOM_LOAD,
    MIOPAN_PROFILE_ROOM_INIT,
    MIOPAN_PROFILE_ROOM_FURN,
    MIOPAN_PROFILE_ROOM_MODEL_INIT,
    MIOPAN_PROFILE_ROOM_REGISTRATION,
    MIOPAN_PROFILE_MESH_CPU,
    MIOPAN_PROFILE_MESH_LIGHTING,
    MIOPAN_PROFILE_MESH_SKINNING,
    MIOPAN_PROFILE_GS_UPLOAD,
    MIOPAN_PROFILE_TEXTURE_INVALIDATE,
    MIOPAN_PROFILE_TEXTURE_MISS,
    MIOPAN_PROFILE_RENDERER_UPLOAD,
    MIOPAN_PROFILE_RENDERER_RECORD,
    MIOPAN_PROFILE_RENDERER_ACQUIRE,
    MIOPAN_PROFILE_RENDERER_SUBMIT,
    MIOPAN_PROFILE_PHASE_COUNT
} MioPanProfilePhase;

typedef enum MioPanProfilerCounter
{
    MIOPAN_PROFILER_COUNTER_BILLBOARD_REQUESTS = 0,
    MIOPAN_PROFILER_COUNTER_BILLBOARD_QUEUED,
    MIOPAN_PROFILER_COUNTER_BILLBOARD_REJECTED,
    MIOPAN_PROFILER_COUNTER_BILLBOARD_SUPPRESSED,
    MIOPAN_PROFILER_COUNTER_BILLBOARD_COMMANDS,
    MIOPAN_PROFILER_COUNTER_BILLBOARD_VERTICES,
    MIOPAN_PROFILER_COUNTER_BILLBOARD_STREAM_BYTES,
    MIOPAN_PROFILER_COUNTER_BILLBOARD_COMPATIBLE_JOINS,
    MIOPAN_PROFILER_COUNTER_BILLBOARD_TEXTURE_LOOKUPS,
    MIOPAN_PROFILER_COUNTER_BILLBOARD_TEXTURE_MISSES,
    MIOPAN_PROFILER_COUNTER_BILLBOARD_LEGACY_SKIPS,
    MIOPAN_PROFILER_COUNTER_SKY_COMMANDS,
    MIOPAN_PROFILER_COUNTER_SKY_VERTICES,
    MIOPAN_PROFILER_COUNTER_SKY_STREAM_BYTES,
    MIOPAN_PROFILER_COUNTER_SKY_COMPATIBLE_JOINS,
    MIOPAN_PROFILER_COUNTER_SKY_POINTS_TRANSFORMED,
    MIOPAN_PROFILER_COUNTER_SKY_CELLS_TESTED,
    MIOPAN_PROFILER_COUNTER_SKY_VISIBLE_CELLS,
    MIOPAN_PROFILER_COUNTER_SKY_HORIZON_QUADS,
    MIOPAN_PROFILER_COUNTER_SKY_DOME_SUPPRESSED,
    MIOPAN_PROFILER_COUNTER_SKY_HORIZON_SUPPRESSED,
    MIOPAN_PROFILER_COUNTER_MAPPUT_CANDIDATES,
    MIOPAN_PROFILER_COUNTER_MAPPUT_DRAWABLE,
    MIOPAN_PROFILER_COUNTER_MAPPUT_COMPARISONS,
    MIOPAN_PROFILER_COUNTER_MAPPUT_CALLBACKS,
    MIOPAN_PROFILER_COUNTER_MAPPUT_OBJECT_DRAWS,
    MIOPAN_PROFILER_COUNTER_COUNT
} MioPanProfilerCounter;

typedef struct MioPanProfilerCounterStats
{
    double per_frame;
    uint64_t max;
} MioPanProfilerCounterStats;

typedef struct MioPanProfilerStats
{
    double frame_ms;
    double frame_max_ms;
    double workload_ms;
    double workload_max_ms;
    double game_cpu_ms;
    double game_cpu_max_ms;
    double vblank_wait_ms;
    double vblank_wait_max_ms;
    double sgd_cpu_ms;
    double sgd_cpu_max_ms;
    double billboard_cpu_ms;
    double billboard_cpu_max_ms;
    double billboard_host_bridge_ms;
    double billboard_host_bridge_max_ms;
    double sky_cpu_ms;
    double sky_cpu_max_ms;
    double mapput_sort_ms;
    double mapput_sort_max_ms;
    double mapput_callback_ms;
    double mapput_callback_max_ms;
    double room_load_ms;
    double room_load_max_ms;
    double room_init_ms;
    double room_init_max_ms;
    double room_furn_ms;
    double room_furn_max_ms;
    double room_model_init_ms;
    double room_model_init_max_ms;
    double room_registration_ms;
    double room_registration_max_ms;
    double mesh_cpu_ms;
    double mesh_cpu_max_ms;
    double mesh_lighting_ms;
    double mesh_lighting_max_ms;
    double mesh_skinning_ms;
    double mesh_skinning_max_ms;
    double gs_upload_ms;
    double gs_upload_max_ms;
    double texture_invalidate_ms;
    double texture_invalidate_max_ms;
    double texture_miss_ms;
    double texture_miss_max_ms;
    double renderer_ms;
    double renderer_max_ms;
    double renderer_upload_ms;
    double renderer_upload_max_ms;
    double renderer_record_ms;
    double renderer_record_max_ms;
    double renderer_acquire_ms;
    double renderer_acquire_max_ms;
    double renderer_submit_ms;
    double renderer_submit_max_ms;
    double gpu_idle_wait_ms;
    double gpu_idle_wait_max_ms;
    int gpu_idle_waits;
    int swapchain_acquire_attempts;
    int swapchain_unavailable;
    int vblank_drained;
    int vblank_wait_calls;
    int vblank_deadline_misses;
    int vblank_target;
    MioPanProfilerCounterStats billboard_requests;
    MioPanProfilerCounterStats billboard_queued;
    MioPanProfilerCounterStats billboard_rejected;
    MioPanProfilerCounterStats billboard_suppressed;
    MioPanProfilerCounterStats billboard_commands;
    MioPanProfilerCounterStats billboard_vertices;
    MioPanProfilerCounterStats billboard_stream_bytes;
    MioPanProfilerCounterStats billboard_compatible_joins;
    MioPanProfilerCounterStats billboard_texture_lookups;
    MioPanProfilerCounterStats billboard_texture_misses;
    MioPanProfilerCounterStats billboard_legacy_skips;
    MioPanProfilerCounterStats sky_commands;
    MioPanProfilerCounterStats sky_vertices;
    MioPanProfilerCounterStats sky_stream_bytes;
    MioPanProfilerCounterStats sky_compatible_joins;
    MioPanProfilerCounterStats sky_points_transformed;
    MioPanProfilerCounterStats sky_cells_tested;
    MioPanProfilerCounterStats sky_visible_cells;
    MioPanProfilerCounterStats sky_horizon_quads;
    MioPanProfilerCounterStats sky_dome_suppressed;
    MioPanProfilerCounterStats sky_horizon_suppressed;
    MioPanProfilerCounterStats mapput_candidates;
    MioPanProfilerCounterStats mapput_drawable;
    MioPanProfilerCounterStats mapput_comparisons;
    MioPanProfilerCounterStats mapput_callbacks;
    MioPanProfilerCounterStats mapput_object_draws;
} MioPanProfilerStats;

/* The profiler follows the renderer/game loop and is intentionally
 * main-thread-only. */
void MioPan_ProfilerReset(void);
void MioPan_ProfilerBeginFrame(void);
void MioPan_ProfilerBeginRenderer(void);
void MioPan_ProfilerEndFrame(void);

uint64_t MioPan_ProfilerNow(void);
uint64_t MioPan_ProfilerBeginPhase(MioPanProfilePhase phase);
void MioPan_ProfilerEndPhase(MioPanProfilePhase phase,
                             uint64_t start_counter);
void MioPan_ProfilerAddCounter(MioPanProfilerCounter counter,
                               uint64_t amount);

/* TEMPORARY DIAGNOSTIC -- stutter attribution.
 *
 * Set MIOPAN_STUTTER_MS in the environment to a millisecond threshold; every
 * logical frame whose wall time exceeds it appends one fully attributed line
 * to miopan_stutter.log.  The renderer publishes its per-frame cache/texture
 * counters through MioPan_ProfilerSetFrameNote() so that the dump can say
 * *which* cold work the slow frame did, not just how long it took.  Costs one
 * comparison per frame when the variable is unset. */
void MioPan_ProfilerSetFrameNote(const char *text);

void MioPan_ProfilerRecordGpuIdleWait(uint64_t wait_ticks);

/* Time the renderer spent deliberately idle, spacing the presents of one
 * logical frame a CRTC field apart.  Subtracted from the renderer total the
 * same way the V-blank wait is subtracted from the game's, so `renderer_ms`
 * keeps meaning work done and the workload budget stays a budget. */
void MioPan_ProfilerRecordPresentWait(uint64_t wait_ticks);
void MioPan_ProfilerRecordSwapchainAcquire(int unavailable);
void MioPan_ProfilerRecordVblankPacing(uint64_t wait_ticks,
                                       int drained_vblanks,
                                       int wait_calls,
                                       int target_vblanks);

/* Converts the accumulated window to milliseconds per logical frame and
 * resets it.  Maxima are the worst individual logical frame in the window. */
void MioPan_ProfilerConsumeStats(MioPanProfilerStats *out);

#ifdef __cplusplus
}

class MioPanProfileScope
{
public:
    explicit MioPanProfileScope(MioPanProfilePhase phase,
                                bool enabled = true)
        : phase_(phase),
          start_(enabled ? MioPan_ProfilerBeginPhase(phase) : 0)
    {
    }

    ~MioPanProfileScope()
    {
        if (start_ != 0)
        {
            MioPan_ProfilerEndPhase(phase_, start_);
        }
    }

    MioPanProfileScope(const MioPanProfileScope &) = delete;
    MioPanProfileScope &operator=(const MioPanProfileScope &) = delete;

private:
    MioPanProfilePhase phase_;
    uint64_t start_;
};
#endif

#endif
