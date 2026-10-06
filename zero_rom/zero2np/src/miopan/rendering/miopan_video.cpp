/* ==========================================================================
 *  miopan_video.cpp  --  the direct movie picture path
 *
 *  See miopan_video.h for what this replaces and why.  There is no GPU code
 *  here on purpose: this is a handoff between the decoder and the renderer,
 *  and keeping it free of SDL is what lets sdk/libmpeg_video.cpp -- which is
 *  compiled out entirely when FFmpeg is absent -- call into it.
 *
 *  Two buffers and a flag.  The decoder owns `staging` between BeginFrame and
 *  EndFrame; `published` is the last complete picture, waiting for the
 *  renderer.  EndFrame swaps them and AcquireFrame swaps the published one out
 *  into the caller's vector, so a picture moves from the scaler to a GPU
 *  transfer buffer without ever being copied here.
 * ======================================================================== */

#include "miopan_video.h"

#include <atomic>
#include <cstdlib>
#include <mutex>
#include <new>
#include <vector>

namespace
{
/* Static storage duration, so there is no initialisation race with whichever
 * thread calls first. */
std::mutex g_lock;

std::vector<unsigned char> g_staging;      /* the decoder is writing this   */
std::vector<unsigned char> g_published;    /* the renderer has not taken it */

int  g_staging_w   = 0;
int  g_staging_h   = 0;
int  g_published_w = 0;
int  g_published_h = 0;
bool g_has_frame   = false;

/* The GS block the movie's TEX0 names, or -1 when no movie is up.
 *
 * Atomic and deliberately outside the lock: the renderer asks whether a TEX0
 * is the movie's once for every textured draw in the game, so this is on the
 * hot path of a frame that has nothing to do with movies. */
std::atomic<int> g_gs_addr{-1};
}

extern "C" {

void MioPan_VideoBegin(int gs_addr)
{
    if (gs_addr < 0)
    {
        return;
    }

    /* A second movie starting at the same address keeps whatever picture is
     * published; a different one drops it, because the frame that is waiting
     * belongs to the film that has just ended. */
    if (g_gs_addr.load(std::memory_order_relaxed) == gs_addr)
    {
        return;
    }

    {
        std::lock_guard<std::mutex> guard(g_lock);
        g_has_frame = false;
    }

    g_gs_addr.store(gs_addr, std::memory_order_release);
}

void MioPan_VideoEnd(void)
{
    g_gs_addr.store(-1, std::memory_order_release);

    std::lock_guard<std::mutex> guard(g_lock);
    g_has_frame = false;
}

int MioPan_VideoIsLive(void)
{
    return g_gs_addr.load(std::memory_order_acquire) >= 0 ? 1 : 0;
}

int MioPan_VideoGsAddr(void)
{
    return g_gs_addr.load(std::memory_order_acquire);
}

unsigned char *MioPan_VideoBeginFrame(int width, int height)
{
    if (width <= 0 || height <= 0)
    {
        return nullptr;
    }

    const size_t need = (size_t)width * (size_t)height * 4u;

    std::lock_guard<std::mutex> guard(g_lock);

    if (g_staging.size() != need)
    {
        try
        {
            g_staging.resize(need);
        }
        catch (const std::bad_alloc &)
        {
            g_staging.clear();
            return nullptr;
        }
    }

    g_staging_w = width;
    g_staging_h = height;

    /* The write itself happens outside the lock.  Only `published` is read by
     * the renderer, so there is nothing for it to race with. */
    return g_staging.data();
}

void MioPan_VideoEndFrame(void)
{
    std::lock_guard<std::mutex> guard(g_lock);

    if (g_staging.empty())
    {
        return;
    }

    g_staging.swap(g_published);
    g_published_w = g_staging_w;
    g_published_h = g_staging_h;
    g_has_frame   = true;

    /* `staging` now holds whatever the renderer last handed back -- possibly
     * empty, possibly the wrong size.  BeginFrame sizes it before the decoder
     * writes a byte, so that is not a state anything can observe. */
    g_staging_w = 0;
    g_staging_h = 0;
}

}  /* extern "C" */

namespace MioPan
{
namespace Video
{

bool AcquireFrame(std::vector<unsigned char> &out, int *width, int *height)
{
    std::lock_guard<std::mutex> guard(g_lock);

    if (!g_has_frame || g_published.empty())
    {
        return false;
    }

    g_has_frame = false;
    g_published.swap(out);

    if (width != nullptr)
    {
        *width = g_published_w;
    }
    if (height != nullptr)
    {
        *height = g_published_h;
    }

    return true;
}

}
}
