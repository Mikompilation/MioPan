/* ==========================================================================
 *  miopan_pacing.cpp  (host frame-pacing override)
 * ======================================================================== */

#include "miopan_pacing.h"

#include "miopan_thread.h"

#include <atomic>

/* Written from the UI thread's frame (inside the game loop, between ticks) and
 * read by vfunc() on the same thread, so no synchronisation is needed -- the
 * V-blank thread never looks at this. */
static int s_vblank_wait_override;
static int s_last_resolved_wait;

/*
 * Constructed at static-init time, long before sceGsSyncVCallback() starts the
 * V-blank thread, so the signaller and the waiter never race to create it.
 */
static MioPan_Sema *const s_field_sema = MioPan_SemaCreate(0);
static std::atomic<uint64_t> s_field_signals{0};

extern "C" {

void MioPan_SetVBlankWaitOverride(int fields)
{
    if (fields <= 0)
    {
        s_vblank_wait_override = 0;
        return;
    }

    /* Eight fields is 7.5 fps; past that the loop stops being a measurement
     * and starts being a hang that looks like one. */
    s_vblank_wait_override = fields > 8 ? 8 : fields;
}

int MioPan_GetVBlankWaitOverride(void)
{
    return s_vblank_wait_override;
}

int MioPan_PacingLastVBlankWait(void)
{
    return s_last_resolved_wait;
}

int MioPan_ResolveVBlankWait(int game_requested)
{
    if (s_vblank_wait_override > 0)
    {
        s_last_resolved_wait = s_vblank_wait_override;
        return s_vblank_wait_override;
    }

    /* vfunc()'s wait loop divides by nothing but does subtract, and a zero or
     * negative request would turn its `do { remain--; } while (remain != 0)`
     * into a very long wait.  The game never asks for one; a future caller
     * might. */
    s_last_resolved_wait = game_requested > 0 ? game_requested : 1;
    return s_last_resolved_wait;
}

void MioPan_PacingSignalField(void)
{
    if (s_field_sema == 0)
    {
        return;
    }

    s_field_signals.fetch_add(1, std::memory_order_relaxed);
    MioPan_SemaSignal(s_field_sema);
}

void MioPan_PacingWaitField(void)
{
    if (s_field_sema == 0 ||
        s_field_signals.load(std::memory_order_relaxed) == 0)
    {
        return;
    }

    while (MioPan_SemaTryWait(s_field_sema))
    {
    }
    MioPan_SemaWait(s_field_sema);
}

}
