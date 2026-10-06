/* ==========================================================================
 *  libgraph.cpp  (SCE GS library -- PC-port shim)
 * ======================================================================== */

#include "libgraph.h"
#include "miopan/gs/miopan_gs_c.h"

#include "../miopan/os/miopan_time.h"   // MioPan_Sleep
#include "../miopan/os/miopan_thread.h" // vblank thread + mutex
#include "../miopan/os/miopan_pacing.h"  // host field signal

#include <atomic>
#include <stdint.h>
#include <string.h>

static MioPan_Mutex  *s_vblank_mutex;
static MioPan_Thread *s_vblank_thread;
static sceGsVCallbackFunc s_vblank_callback;

/*
 * The CRTC field period, in nanoseconds.
 *
 * On hardware the V-blank interrupt fires at the field rate of the video
 * standard sceGsResetGraph() was given, so the shim takes it from the same
 * place -- see the omode note there.  60 Hz is the boot default because
 * InitSysWrk() boots NTSC; nothing reads this before the first
 * SetGsResetGraph().
 *
 * Written by the game thread (through sceGsResetGraph) and read by the V-blank
 * thread, hence the atomic.
 */
static std::atomic<uint64_t> s_vblank_period_ns{1000000000ull / 60};

struct VblankLimiter
{
    uint64_t frequency;
    uint64_t next_counter;
};

static VblankLimiter s_vblank_thread_limiter;
static VblankLimiter s_sync_v_limiter;

static uint64_t SecondsToCounterTicks(const VblankLimiter *limiter,
                                      double seconds)
{
    if (limiter == 0 || limiter->frequency == 0 || seconds <= 0.0)
    {
        return 0;
    }

    return (uint64_t)(seconds * (double)limiter->frequency + 0.5);
}

/* The field period as a fallback sleep, for the paths that cannot use the
 * performance counter. */
static unsigned int VblankPeriodMs(void)
{
    const uint64_t period_ns =
        s_vblank_period_ns.load(std::memory_order_relaxed);

    return (unsigned int)(period_ns / 1000000ull);
}

static void WaitForVblank(VblankLimiter *limiter)
{
    uint64_t now;
    uint64_t frame_ticks;
    uint64_t spin_ticks;
    uint64_t target_counter;

    if (limiter == 0)
    {
        MioPan_Sleep(VblankPeriodMs());
        return;
    }

    if (limiter->frequency == 0)
    {
        limiter->frequency = MioPan_GetPerformanceFrequency();
        limiter->next_counter = MioPan_GetPerformanceCounter();
    }

    if (limiter->frequency == 0)
    {
        MioPan_Sleep(VblankPeriodMs());
        return;
    }

    /* Re-read every field: ChangeVideoMode() may have switched standards since
     * the last one, and the catch-up clamp at the bottom absorbs the single
     * mistimed field that costs. */
    frame_ticks = SecondsToCounterTicks(
        limiter,
        (double)s_vblank_period_ns.load(std::memory_order_relaxed) / 1.0e9);
    spin_ticks = SecondsToCounterTicks(limiter, 0.0005);
    target_counter = limiter->next_counter + frame_ticks;

    for (;;)
    {
        uint64_t remaining_ticks;

        now = MioPan_GetPerformanceCounter();
        if (now >= target_counter)
        {
            break;
        }

        remaining_ticks = target_counter - now;
        if (remaining_ticks > spin_ticks)
        {
            uint64_t sleep_ns =
                ((remaining_ticks - spin_ticks) * 1000000000ull) /
                limiter->frequency;
            if (sleep_ns > 0)
            {
                MioPan_SleepNs(sleep_ns);
            }
        }
        else
        {
            while (MioPan_GetPerformanceCounter() < target_counter)
            {
            }
            break;
        }
    }

    limiter->next_counter = target_counter;
    now = MioPan_GetPerformanceCounter();
    if (now > limiter->next_counter + frame_ticks)
    {
        limiter->next_counter = now;
    }
}

static void VblankThreadMain(void *data)
{
    (void)data;

    for (;;)
    {
        sceGsVCallbackFunc callback;

        WaitForVblank(&s_vblank_thread_limiter);

        /* The host's own copy of the field edge.  The game's semaphore is
         * posted by the callback below; this one is never drained by vfunc(),
         * so the renderer can pace against it without spending the game's
         * fields.  See miopan_pacing.h. */
        MioPan_PacingSignalField();

        MioPan_MutexLock(s_vblank_mutex);
        callback = s_vblank_callback;
        MioPan_MutexUnlock(s_vblank_mutex);

        if (callback != 0)
        {
            callback(0);
        }
    }
}

static void EnsureVblankThread(void)
{
    if (s_vblank_mutex == 0)
    {
        s_vblank_mutex = MioPan_MutexCreate();
        if (s_vblank_mutex == 0)
        {
            return;
        }
    }

    if (s_vblank_thread == 0)
    {
        s_vblank_thread = MioPan_ThreadCreate(VblankThreadMain, 0, "gs-vblank");
    }
}

extern "C" {

/*
 * omode is the CRTC's video standard, and on hardware it is what decides the
 * rate the V-blank interrupt fires at -- 50 Hz for PAL, 60 for NTSC and 480p.
 * system.c's SetGsResetGraph() already passes the right one for
 * sys_wrk.video_mode, so taking the host tick from here needs no new API and
 * keeps the two in step through ChangeVideoMode().
 *
 * Getting this from the mode is not cosmetic.  GetPALMode() divides every
 * authored frame count by 1.2 at 64 call sites, and fod.c steps the scene
 * clock by 1.19999993 rather than 1.0, both on the assumption that a PAL field
 * is 1/50 s.  While this shim ticked at 60 Hz unconditionally, picking 50Hz on
 * the frame-rate screen left the rescaling in place with nothing to pay for it
 * and ran the whole game 20% fast.
 *
 * Only 2 and 3 are reachable in practice (SetGsResetGraph passes SCE_GS_DTV480P
 * for video_mode 'P', which nothing selects); anything else keeps 60.
 */
void sceGsResetGraph(short mode, short inter, short omode, short ffmode)
{
    (void)mode;
    (void)inter;
    (void)ffmode;

    s_vblank_period_ns.store(omode == SCE_GS_PAL ? 1000000000ull / 50
                                                 : 1000000000ull / 60,
                             std::memory_order_relaxed);
}

void sceGsResetPath(void)
{
}

int sceGsSyncV(int mode)
{
    (void)mode;
    WaitForVblank(&s_sync_v_limiter);
    return 1;
}

int sceGsSyncPath(int mode, u_short timeout)
{
    (void)mode;
    (void)timeout;
    return 0;
}

sceGsVCallbackFunc sceGsSyncVCallback(sceGsVCallbackFunc func)
{
    sceGsVCallbackFunc old;

    EnsureVblankThread();
    if (s_vblank_mutex == 0)
    {
        old = s_vblank_callback;
        s_vblank_callback = func;
        return old;
    }

    MioPan_MutexLock(s_vblank_mutex);
    old = s_vblank_callback;
    s_vblank_callback = func;
    MioPan_MutexUnlock(s_vblank_mutex);
    return old;
}

void sceGsSetDefDBuff(sceGsDBuff *dp, short psm, short w, short h,
                      short ztest, short zpsm, short clear)
{
    (void)psm;
    (void)w;
    (void)h;
    (void)ztest;
    (void)zpsm;
    (void)clear;
    if (dp != 0)
    {
        memset(dp, 0, sizeof(*dp));
    }
}

int sceGsSwapDBuff(sceGsDBuff *db, int id)
{
    (void)db;
    (void)id;
    return 0;
}

static int GetLoadImageQwc(short dpsm, short w, short h)
{
    int bits_per_pixel;
    int bits;

    switch (dpsm)
    {
        case SCE_GS_PSMT4:
        case SCE_GS_PSMT4HL:
        case SCE_GS_PSMT4HH:
            bits_per_pixel = 4;
            break;
        case SCE_GS_PSMT8:
        case SCE_GS_PSMT8H:
            bits_per_pixel = 8;
            break;
        case SCE_GS_PSMCT16:
        case SCE_GS_PSMZ16:
            bits_per_pixel = 16;
            break;
        case SCE_GS_PSMCT24:
        case SCE_GS_PSMZ24:
            bits_per_pixel = 24;
            break;
        case SCE_GS_PSMCT32:
        case SCE_GS_PSMZ32:
        default:
            bits_per_pixel = 32;
            break;
    }

    if (w <= 0 || h <= 0)
    {
        return 0;
    }

    bits = (int)w * (int)h * bits_per_pixel;
    return (bits + 127) >> 7;
}

int sceGsSetDefLoadImage(sceGsLoadImage *lp, short dbp, short dbw, short dpsm,
                         short x, short y, short w, short h)
{
    if (lp != 0)
    {
        memset(lp, 0, sizeof(*lp));
        lp->giftag.NLOOP = 1;
        lp->giftag.EOP = 0;
        lp->giftag.FLG = SCE_GIF_PACKED;
        lp->giftag.NREG = 4;
        lp->giftag.REGS0 = SCE_GIF_PACKED_AD;
        lp->giftag.REGS1 = SCE_GIF_PACKED_AD;
        lp->giftag.REGS2 = SCE_GIF_PACKED_AD;
        lp->giftag.REGS3 = SCE_GIF_PACKED_AD;
        lp->bitbltbuf.DBP = dbp;
        lp->bitbltbuf.DBW = dbw;
        lp->bitbltbuf.DPSM = dpsm;
        lp->bitbltbufaddr = SCE_GS_BITBLTBUF;
        lp->trxpos.DSAX = x;
        lp->trxpos.DSAY = y;
        lp->trxposaddr = SCE_GS_TRXPOS;
        lp->trxreg.RRW = w;
        lp->trxreg.RRH = h;
        lp->trxregaddr = SCE_GS_TRXREG;
        lp->trxdir.XDR = 0;
        lp->trxdiraddr = SCE_GS_TRXDIR;
        lp->giftag1.NLOOP = GetLoadImageQwc(dpsm, w, h);
        lp->giftag1.EOP = 1;
        lp->giftag1.FLG = SCE_GIF_IMAGE;
    }
    return 0;
}

int sceGsExecLoadImage(sceGsLoadImage *lp, u_long128 *srcaddr)
{
    MioPan_GsUpload(lp, (unsigned char *)srcaddr);
    return 0;
}

int sceGsSetDefStoreImage(sceGsStoreImage *sp, short sbp, short sbw, short spsm,
                          short x, short y, short w, short h)
{
    if (sp != 0)
    {
        memset(sp, 0, sizeof(*sp));
        sp->bitbltbuf.SBP = sbp;
        sp->bitbltbuf.SBW = sbw;
        sp->bitbltbuf.SPSM = spsm;
        sp->bitbltbufaddr = SCE_GS_BITBLTBUF;
        sp->trxpos.SSAX = x;
        sp->trxpos.SSAY = y;
        sp->trxposaddr = SCE_GS_TRXPOS;
        sp->trxreg.RRW = w;
        sp->trxreg.RRH = h;
        sp->trxregaddr = SCE_GS_TRXREG;
        sp->trxdir.XDR = 1;
        sp->trxdiraddr = SCE_GS_TRXDIR;
    }
    return 0;
}

int sceGsExecStoreImage(sceGsStoreImage *sp, u_long128 *dstaddr)
{
    MioPan_GsStore(sp, (unsigned char *)dstaddr);
    return 0;
}

int sceGsSetDefAlphaEnv(u_long128 *addr, int mode)
{
    (void)mode;
    if (addr != 0)
    {
        memset(addr, 0, sizeof(*addr));
    }
    return 0;
}

void sceGsSetHalfOffset(sceGsDrawEnv1 *draw, short centerx, short centery, short halfoff)
{
    (void)halfoff;
    if (draw != 0)
    {
        draw->xyoffset1.OFX = (u_short)(centerx << 4);
        draw->xyoffset1.OFY = (u_short)(centery << 4);
        draw->xyoffset1addr = SCE_GS_XYOFFSET_1;
    }
}

}
