/* ==========================================================================
 *  iop_host.cpp  (IOP kernel services — PC-port implementation)
 *
 *  iopsys.irx assumed a preemptive kernel with threads, counting semaphores
 *  and a hardware timer.  All three map onto SDL directly, so the IOP half of
 *  the reconstruction runs as written rather than being restructured into the
 *  EE's frame loop.
 *
 *  What does NOT map is priority.  The ROM leans on it: PRI_STREAM_VOICE (11)
 *  must outrun PRI_MAIN (19) or a stream underruns.  Host threads take the
 *  priority as a hint (SDL_SetCurrentThreadPriority) and are otherwise left to
 *  the OS scheduler -- the streams are paced by the SPU interrupt and by
 *  semaphores, not by priority alone, so this is a fidelity gap rather than a
 *  correctness one.  It is the first thing to suspect if audio stutters.
 *
 *  Shutdown is cooperative: MioPan_IopHostShutdown() raises a flag, releases
 *  every semaphore and parked thread so their blocking calls return, and joins
 *  them.  Without it the stream threads outlive the buffers they are reading.
 * ======================================================================== */

#include "iop_host.h"

#if defined(_WIN32)
/* Declared by hand rather than via <windows.h>: that header defines u_long and
 * a CreateThread/GetThreadId family that collide head-on with the EE kernel
 * shim (eekernel.h) and scetypes.h, both of which are force-included here. */
extern "C" __declspec(dllimport) void *__stdcall VirtualAlloc(
    void *addr, size_t size, unsigned long type, unsigned long protect);

/* MEMORY_BASIC_INFORMATION, likewise by hand.  The two __alignment members are
 * real: the 64-bit layout pads after AllocationProtect and after Type. */
typedef struct
{
    void         *BaseAddress;
    void         *AllocationBase;
    unsigned long AllocationProtect;
#if defined(_WIN64)
    unsigned long __alignment1;
#endif
    size_t        RegionSize;
    unsigned long State;
    unsigned long Protect;
    unsigned long Type;
#if defined(_WIN64)
    unsigned long __alignment2;
#endif
} IopMemInfo;

extern "C" __declspec(dllimport) size_t __stdcall VirtualQuery(
    const void *addr, IopMemInfo *info, size_t length);

#define IOP_MEM_COMMIT      0x00001000
#define IOP_MEM_RESERVE     0x00002000
#define IOP_MEM_FREE        0x00010000
#define IOP_PAGE_READWRITE  0x00000004
#else
#include <sys/mman.h>
#endif

#include <SDL3/SDL_mutex.h>
#include <SDL3/SDL_thread.h>
#include <SDL3/SDL_timer.h>

#include <stdlib.h>
#include <string.h>

#define IOP_MAX_THREADS 32
#define IOP_MAX_SEMA    64
#define IOP_MAX_TIMERS  4

typedef struct
{
    int             id;
    void           *entry;
    uintptr_t       option;          /* the ROM's thread argument channel */
    int             priority;
    SDL_Thread     *thread;
    SDL_Semaphore  *wake;            /* SleepThread / WakeupThread        */
    int             started;
    int             exited;
    int             starting;        /* a relaunch is joining the old instance */
} IopThread;

typedef struct
{
    int             id;
    SDL_Semaphore  *sem;
    int             maxCount;
} IopSema;

typedef struct
{
    int             id;
    u_int           compare;         /* microseconds between ticks */
    TimerHandler    handler;
    void           *common;
    SDL_Thread     *thread;
    int             running;
} IopTimer;

static IopThread     iop_threads[IOP_MAX_THREADS];
static IopSema       iop_semas[IOP_MAX_SEMA];
static IopTimer      iop_timers[IOP_MAX_TIMERS];
static SDL_TLSID     iop_thread_tls;
static SDL_Mutex    *iop_table_lock;
static volatile int  iop_shutdown;

/* The big kernel lock, and this thread's recursion depth in it.  See the long
 * note in iop_host.h for what it is for and who holds it. */
static SDL_Mutex    *iop_bkl;
static SDL_TLSID     iop_bkl_tls;

/* --------------------------------------------------------------------------
 *  The big kernel lock
 *
 *  SDL mutexes are recursive, but recursion alone is not enough: a thread that
 *  blocks has to give the lock back *completely*, however deeply it had
 *  nested, and take the same depth again afterwards.  Hence the per-thread
 *  depth counter and the Suspend/Resume pair.
 * ------------------------------------------------------------------------ */

static int BklDepth(void)
{
    return (int)(intptr_t)SDL_GetTLS(&iop_bkl_tls);
}

static void BklSetDepth(int depth)
{
    SDL_SetTLS(&iop_bkl_tls, (void *)(intptr_t)depth, NULL);
}

/* Created eagerly from MioPan_IopBoot(), before start() and therefore before
 * any IOP thread or RPC dispatch exists.  Everything below treats a null lock
 * as "the IOP is not up", which is the only state in which it can be seen. */
void MioPan_IopBklInit(void)
{
    if (iop_bkl == NULL)
        iop_bkl = SDL_CreateMutex();
}

void MioPan_IopBklEnter(void)
{
    if (iop_bkl == NULL)
        return;

    SDL_LockMutex(iop_bkl);
    BklSetDepth(BklDepth() + 1);
}

void MioPan_IopBklLeave(void)
{
    int depth = BklDepth();

    if (iop_bkl == NULL || depth <= 0)
        return;

    BklSetDepth(depth - 1);
    SDL_UnlockMutex(iop_bkl);
}

int MioPan_IopBklSuspend(void)
{
    int depth = BklDepth();

    if (iop_bkl == NULL || depth <= 0)
        return 0;

    BklSetDepth(0);
    for (int i = 0; i < depth; i++)
        SDL_UnlockMutex(iop_bkl);

    return depth;
}

void MioPan_IopBklResume(int depth)
{
    if (iop_bkl == NULL || depth <= 0)
        return;

    for (int i = 0; i < depth; i++)
        SDL_LockMutex(iop_bkl);
    BklSetDepth(depth);
}

/* --------------------------------------------------------------------------
 *  Table helpers
 * ------------------------------------------------------------------------ */

static void IopLock(void)
{
    if (iop_table_lock == NULL)
        iop_table_lock = SDL_CreateMutex();
    SDL_LockMutex(iop_table_lock);
}

static void IopUnlock(void)
{
    SDL_UnlockMutex(iop_table_lock);
}

static IopThread *FindThread(int thid)
{
    for (int i = 0; i < IOP_MAX_THREADS; i++)
    {
        if (iop_threads[i].id == thid)
            return &iop_threads[i];
    }
    return NULL;
}

static IopSema *FindSema(int semid)
{
    for (int i = 0; i < IOP_MAX_SEMA; i++)
    {
        if (iop_semas[i].id == semid)
            return &iop_semas[i];
    }
    return NULL;
}

static IopTimer *FindTimer(int timer_id)
{
    for (int i = 0; i < IOP_MAX_TIMERS; i++)
    {
        if (iop_timers[i].id == timer_id)
            return &iop_timers[i];
    }
    return NULL;
}

/* --------------------------------------------------------------------------
 *  Threads
 * ------------------------------------------------------------------------ */

static int SDLCALL IopThreadMain(void *data)
{
    IopThread *th = (IopThread *)data;

    SDL_SetTLS(&iop_thread_tls, th, NULL);

    /* A hint only -- see the note at the top of this file. */
    if (th->priority <= 12)
        SDL_SetCurrentThreadPriority(SDL_THREAD_PRIORITY_HIGH);
    else if (th->priority >= 64)
        SDL_SetCurrentThreadPriority(SDL_THREAD_PRIORITY_LOW);

    /* Held for the whole body, so this thread's IOP code cannot interleave
     * with any other IOP context's -- and dropped inside every blocking
     * primitive, so the ROM's own scheduling points still work. */
    MioPan_IopBklEnter();

    if (th->entry != NULL)
        ((void (*)(void))th->entry)();

    th->exited = 1;

    MioPan_IopBklLeave();
    return 0;
}

int MioPan_IopCreateThread(IopThreadParam *param)
{
    if (param == NULL || param->entry == NULL)
        return -1;

    IopLock();
    for (int i = 0; i < IOP_MAX_THREADS; i++)
    {
        if (iop_threads[i].id == 0)
        {
            iop_threads[i].id       = i + 1;
            iop_threads[i].entry    = param->entry;
            iop_threads[i].option   = param->option;
            iop_threads[i].priority = param->initPriority;
            iop_threads[i].thread   = NULL;
            iop_threads[i].wake     = SDL_CreateSemaphore(0);
            iop_threads[i].started  = 0;
            iop_threads[i].exited   = 0;
            IopUnlock();
            return i + 1;
        }
    }
    IopUnlock();

    return -1;
}

int MioPan_IopDeleteThread(int thid)
{
    IopLock();
    IopThread *th = FindThread(thid);
    if (th != NULL && !th->started)
    {
        if (th->wake != NULL)
            SDL_DestroySemaphore(th->wake);
        memset(th, 0, sizeof(*th));
    }
    IopUnlock();

    return 0;
}

/* StreamStop() restarts a voice thread that may have already run, and reads
 * the result to tell the two cases apart: 0 means it started it, non-zero that
 * it was already going. */
int MioPan_IopStartThread(int thid, void *arg)
{
    (void)arg;

    IopLock();
    IopThread *th = FindThread(thid);
    if (th == NULL)
    {
        IopUnlock();
        return -1;
    }

    /* PORT: a thread that has signalled completion but has not yet reached
     * ExitThread() still reads as running here.  On the IOP that window was
     * unobservable, because a thread that had signalled its waiters ran at a
     * priority above them and always went dormant first.  Priority is only a
     * hint on this host, so a waiter released by the signal can reach
     * StartThread() inside the window, be told "already running", and then wait
     * on a completion that will never be posted again.
     *
     * iopReqRead() used to be the classic victim -- it ignores this return
     * value -- but it no longer starts a thread at all (see iop.c).  The
     * ring-buffer reader and the two stream threads are still relaunched this
     * way, so the grace period stays.
     *
     * Both locks are dropped around the wait: nothing may block while holding
     * the table lock, and the thread being waited for needs the BKL to finish
     * unwinding.  A genuinely long-running thread (StreamStop() poking the
     * voice thread) just costs the full 10 ms and still answers "already
     * running". */
    if (th->started && !th->exited)
    {
        int bkl = MioPan_IopBklSuspend();

        for (int spin = 0; spin < 40; spin++)
        {
            IopUnlock();
            SDL_DelayNS(250000);        /* 250 us */
            IopLock();

            if (th->exited)
                break;
        }

        IopUnlock();
        MioPan_IopBklResume(bkl);
        IopLock();
    }

    if (th->started && !th->exited)
    {
        IopUnlock();
        return -1;                  /* already running */
    }

    if (th->starting)
    {
        IopUnlock();
        return -1;                  /* another relaunch is already in progress */
    }

    /* A finished thread has to be relaunchable, which means joining the old
     * instance before spawning the new one.
     *
     * That join MUST NOT happen under iop_table_lock, nor under the BKL.
     * ExitThread() sets `exited` from inside the thread body, so a thread can
     * be flagged exited while it is still unwinding -- and an IOP thread's
     * unwind runs the ROM's release path (StreamReleaseSub -> FreeSysMemory,
     * DeleteSema), which takes the table lock and needs the BKL.  Joining while
     * holding either deadlocks the pair, and every other IOP thread then piles
     * up behind it.  Observed at the end of a scene, where a stream's threads
     * exit just as the next stream starts one.
     *
     * `starting` keeps the slot claimed across the unlocked window so a second
     * StartThread() cannot spawn a duplicate. */
    th->starting = 1;

    SDL_Thread *stale = th->thread;
    th->thread = NULL;
    IopUnlock();

    int join_bkl = MioPan_IopBklSuspend();
    if (stale != NULL)
        SDL_WaitThread(stale, NULL);
    MioPan_IopBklResume(join_bkl);

    IopLock();
    th->exited = 0;
    th->thread = SDL_CreateThread(IopThreadMain, "iop", th);
    th->starting = 0;
    if (th->thread == NULL)
    {
        IopUnlock();
        return -1;
    }

    th->started = 1;
    IopUnlock();

    return 0;
}

int MioPan_IopExitThread(void)
{
    IopThread *th = (IopThread *)SDL_GetTLS(&iop_thread_tls);
    if (th != NULL)
        th->exited = 1;

    /* The ROM's threads call this as their last statement and never return
     * from it; here the entry function simply unwinds and IopThreadMain ends. */
    return 0;
}

int MioPan_IopTerminateThread(int thid)
{
    /* SDL has no thread kill, and there is nowhere safe to cut an IOP thread
     * anyway -- it could be holding the ring buffer.  So this only nudges the
     * thread and returns; the thread lives on.
     *
     * StreamAbort() is the only caller, and unlike in the ROM it IS reachable
     * here (the port wired up iopCommand()'s missing REQ_STREAM_ABORT case).
     * It has been rewritten to suit: it no longer assumes the threads are gone
     * afterwards, waits for them to exit on their own, and leaks what they hold
     * if they do not.  Anything else added here must make the same assumption --
     * that this call changes nothing except that the thread has been woken. */
    IopLock();
    IopThread *th = FindThread(thid);
    if (th != NULL && th->wake != NULL)
        SDL_SignalSemaphore(th->wake);
    IopUnlock();

    return 0;
}

int MioPan_IopGetThreadId(void)
{
    IopThread *th = (IopThread *)SDL_GetTLS(&iop_thread_tls);
    return th != NULL ? th->id : 0;
}

/* thid 0 means "the calling thread", which is how every IOP thread body
 * recovers the work pointer its creator put in ThreadParam::option. */
int MioPan_IopReferThreadStatus(int thid, ThreadInfo *info)
{
    if (info == NULL)
        return -1;

    memset(info, 0, sizeof(*info));

    IopThread *th = (thid == 0)
        ? (IopThread *)SDL_GetTLS(&iop_thread_tls)
        : FindThread(thid);

    if (th == NULL)
        return -1;

    info->option       = th->option;
    info->entry        = th->entry;
    info->initPriority = th->priority;
    info->currentPriority = th->priority;

    return 0;
}

int MioPan_IopSleepThread(void)
{
    IopThread *th = (IopThread *)SDL_GetTLS(&iop_thread_tls);

    if (iop_shutdown)
        return -1;

    int bkl;

    if (th == NULL || th->wake == NULL)
    {
        bkl = MioPan_IopBklSuspend();
        SDL_Delay(1);
        MioPan_IopBklResume(bkl);
        return iop_shutdown ? -1 : 0;
    }

    bkl = MioPan_IopBklSuspend();
    SDL_WaitSemaphore(th->wake);
    MioPan_IopBklResume(bkl);

    return iop_shutdown ? -1 : 0;
}

int MioPan_IopWakeupThread(int thid)
{
    IopThread *th = FindThread(thid);
    if (th == NULL || th->wake == NULL)
        return -1;

    SDL_SignalSemaphore(th->wake);

    return 0;
}

int MioPan_IopDelayThread(int usec)
{
    if (iop_shutdown)
        return -1;

    /* The ROM's retry loops -- StreamReleaseSub()'s stop-block wait above all
     * -- are built on this, and they are waiting for something another IOP
     * context has to do.  Holding the lock across the delay would make them
     * spin for ever. */
    int bkl = MioPan_IopBklSuspend();
    SDL_DelayNS((Uint64)(usec > 0 ? usec : 0) * 1000ULL);
    MioPan_IopBklResume(bkl);

    return 0;
}

int MioPan_IopUSec2SysClock(u_int usec, SysClock *clock)
{
    /* The ROM only ever uses the low half, as the timer compare value.  Keeping
     * it in microseconds is what lets IopTimerMain treat it as an interval. */
    if (clock != NULL)
    {
        clock->lo = usec;
        clock->hi = 0;
    }

    return 0;
}

/* --------------------------------------------------------------------------
 *  Semaphores
 * ------------------------------------------------------------------------ */

int MioPan_IopCreateSema(IopSemaParam *param)
{
    if (param == NULL)
        return -1;

    IopLock();
    for (int i = 0; i < IOP_MAX_SEMA; i++)
    {
        if (iop_semas[i].id == 0)
        {
            iop_semas[i].id       = i + 1;
            iop_semas[i].sem      = SDL_CreateSemaphore((Uint32)param->initCount);
            iop_semas[i].maxCount = param->maxCount;
            IopUnlock();
            return i + 1;
        }
    }
    IopUnlock();

    return -1;
}

int MioPan_IopDeleteSema(int semid)
{
    IopLock();
    IopSema *s = FindSema(semid);
    if (s != NULL)
    {
        if (s->sem != NULL)
            SDL_DestroySemaphore(s->sem);
        memset(s, 0, sizeof(*s));
    }
    IopUnlock();

    return 0;
}

int MioPan_IopSignalSema(int semid)
{
    IopSema *s = FindSema(semid);
    if (s == NULL || s->sem == NULL)
        return -1;

    /* The IOP kernel caps a semaphore at maxCount; SDL's has no ceiling, and
     * without this a repeatedly-signalled completion would let a later waiter
     * through without waiting. */
    if (s->maxCount > 0 && (int)SDL_GetSemaphoreValue(s->sem) >= s->maxCount)
        return semid;

    SDL_SignalSemaphore(s->sem);

    return semid;
}

int MioPan_IopWaitSema(int semid)
{
    IopSema *s = FindSema(semid);
    if (s == NULL || s->sem == NULL)
        return -1;

    if (iop_shutdown)
        return -1;

    /* A blocked IOP thread was not running, so it must not be holding the
     * CPU: this is one of the ROM's own scheduling points. */
    int bkl = MioPan_IopBklSuspend();
    SDL_WaitSemaphore(s->sem);
    MioPan_IopBklResume(bkl);

    return iop_shutdown ? -1 : semid;
}

/* Answers KE_SEMA_ZERO when the count is already zero -- which is the answer
 * the drain loops in MyCdRead() and iopReqRead() are actually waiting for. */
int MioPan_IopPollSema(int semid)
{
    IopSema *s = FindSema(semid);
    if (s == NULL || s->sem == NULL)
        return -1;

    if (!SDL_TryWaitSemaphore(s->sem))
        return KE_SEMA_ZERO;

    return semid;
}

int MioPan_IopReferSemaStatus(int semid, SemaInfo *info)
{
    IopSema *s = FindSema(semid);
    if (s == NULL || info == NULL)
        return -1;

    memset(info, 0, sizeof(*info));
    info->maxCount     = s->maxCount;
    info->currentCount = s->sem != NULL ? (int)SDL_GetSemaphoreValue(s->sem) : 0;

    return 0;
}

/* --------------------------------------------------------------------------
 *  Hard timers
 * ------------------------------------------------------------------------ */

static int SDLCALL IopTimerMain(void *data)
{
    IopTimer *timer   = (IopTimer *)data;
    Uint64    interval = (Uint64)(timer->compare ? timer->compare : 1000) * 1000ULL;
    Uint64    next     = SDL_GetTicksNS() + interval;

    while (timer->running && !iop_shutdown)
    {
        Uint64 now = SDL_GetTicksNS();
        if (now < next)
            SDL_DelayNS(next - now);

        if (!timer->running || iop_shutdown)
            break;

        if (timer->handler != NULL)
        {
            /* The handler returns the next compare value, which is how a
             * timrman handler asks to keep firing at the same interval. */
            u_int next_compare = timer->handler(timer->common);
            if (next_compare != 0)
                interval = (Uint64)next_compare * 1000ULL;
        }

        next += interval;
    }

    return 0;
}

int MioPan_IopAllocHardTimer(int source, int size, int prescale)
{
    (void)source;
    (void)size;
    (void)prescale;

    IopLock();
    for (int i = 0; i < IOP_MAX_TIMERS; i++)
    {
        if (iop_timers[i].id == 0)
        {
            iop_timers[i].id = i + 1;
            IopUnlock();
            return i + 1;
        }
    }
    IopUnlock();

    return -1;
}

int MioPan_IopSetTimerHandler(int timer_id, u_int compare, TimerHandler handler, void *common)
{
    IopTimer *timer = FindTimer(timer_id);
    if (timer == NULL)
        return -1;

    timer->compare = compare;
    timer->handler = handler;
    timer->common  = common;

    return 0;
}

int MioPan_IopSetupHardTimer(int timer_id, int source, int mode, int prescale)
{
    (void)timer_id;
    (void)source;
    (void)mode;
    (void)prescale;

    return 0;
}

int MioPan_IopStartHardTimer(int timer_id)
{
    IopTimer *timer = FindTimer(timer_id);
    if (timer == NULL || timer->running)
        return -1;

    timer->running = 1;
    timer->thread  = SDL_CreateThread(IopTimerMain, "iop-timer", timer);

    return timer->thread != NULL ? 0 : -1;
}

int MioPan_IopStopHardTimer(int timer_id)
{
    IopTimer *timer = FindTimer(timer_id);
    if (timer == NULL)
        return -1;

    timer->running = 0;

    return 0;
}

/* --------------------------------------------------------------------------
 *  Interrupts and cache
 * ------------------------------------------------------------------------ */

int MioPan_IopEnableIntr(int intr)
{
    (void)intr;
    return 0;
}

int MioPan_IopCpuSuspendIntr(int *oldstat)
{
    if (oldstat != NULL)
        *oldstat = 0;
    return 0;
}

int MioPan_IopCpuResumeIntr(int oldstat)
{
    (void)oldstat;
    return 0;
}

void MioPan_IopFlushDcache(void)
{
}

/* --------------------------------------------------------------------------
 *  System memory
 * ------------------------------------------------------------------------ */

/* IOP RAM has to live below 4 GB.
 *
 * The ROM keeps IOP addresses in `int` fields -- RING_BUF_WRK::ring_buf_top,
 * STREAM_WRK::rb_top, LOAD_IOP_WRK::adrs -- and passes them through `unsigned
 * int` parameters, because on the IOP a pointer was 32 bits.  Those are
 * faithful and should stay, so instead of widening the reconstruction the
 * allocator guarantees the assumption: every block it hands out comes from a
 * reservation made low enough that `(int)` round-trips it losslessly.
 *
 * A plain malloc would not do -- on Win64 the heap sits far above 4 GB, and
 * truncating one of those pointers is what crashes the first file load. */

#define IOP_ARENA_SIZE  (4 * 1024 * 1024)   /* the IOP had 2 MB; double it */

/* One past the top of the IOP's own address map, doubled for headroom.  No
 * host allocation lands below this on either platform. */
#define IOP_MAP_LIMIT   0x00400000u
#define IOP_MAX_BLOCKS  32

typedef struct
{
    u_char *base;
    int     size;
    int     used;
} IopBlock;

static u_char  *iop_arena;
static int      iop_arena_used;
/* What was actually reserved.  The low window is scarce, so the reservation
 * shrinks to fit rather than giving up -- see IopArenaInit(). */
static int      iop_arena_size;
static IopBlock iop_blocks[IOP_MAX_BLOCKS];

/* Try to reserve `size` bytes at exactly `hint`; NULL if that address is
 * taken.  On success the caller records `size` in iop_arena_size. */
static u_char *IopArenaTry(uintptr_t hint, int size)
{
#if defined(_WIN32)
    void *p = VirtualAlloc((void *)hint, (size_t)size,
                           IOP_MEM_RESERVE | IOP_MEM_COMMIT,
                           IOP_PAGE_READWRITE);
    return (u_char *)p;
#else
    void *p = mmap((void *)hint, (size_t)size, PROT_READ | PROT_WRITE,
                   MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);

    if (p == MAP_FAILED)
        return NULL;

    /* Without MAP_FIXED the kernel may place it elsewhere, and elsewhere is
     * exactly what we are trying to avoid. */
    if (hint != 0 && (uintptr_t)p != hint)
    {
        munmap(p, (size_t)size);
        return NULL;
    }

    return (u_char *)p;
#endif
}

/*
 * The arena has to land inside a window, not merely "low".
 *
 * Ceiling: the IOP's address space is 24 bits and the ROM relies on it.
 * audioDecSendToIOP() and audioDecPause() both read the auto-DMA play position
 * back as `(status & 0xffffff) - ad->iopBuff`.  On hardware the mask is a
 * no-op because every IOP address is under 2 MB.  Above 16 MB it strips the
 * top bits, the subtraction goes hugely negative, iopGetArea() reports a
 * negative writable span, and audiodec.c stops sending after the preload --
 * the movie plays its first ring-full of audio on a loop forever.
 *
 * Floor: MioPan_IopMemIsBareOffset() calls anything under IOP_MAP_LIMIT a
 * fake IOP address rather than host memory.  An arena below that line would
 * have every block it hands out rejected by sif.cpp and iop_libsd.cpp as
 * un-writable -- so the arena must sit above the fake map, not beneath it.
 */
#define IOP_ARENA_FLOOR IOP_MAP_LIMIT   /* clear of the fake bare-offset map */
#define IOP_ARENA_CEIL  0x01000000u     /* 16 MB: where the 24-bit mask bites */

/* Reserve `size` bytes somewhere in [IOP_ARENA_FLOOR, IOP_ARENA_CEIL), or NULL.
 *
 * Walks the real free regions rather than guessing round numbers: by the time a
 * movie starts, bottom-up ASLR has scattered small allocations through low
 * memory, and a blind fixed-step walk misses the gaps between them. */
static u_char *IopReserveLow(int size)
{
#if defined(_WIN32)
    IopMemInfo mbi;
    uintptr_t  addr = IOP_ARENA_FLOOR;

    while (addr + (uintptr_t)size <= IOP_ARENA_CEIL &&
           VirtualQuery((void *)addr, &mbi, sizeof(mbi)) != 0)
    {
        uintptr_t region_end = (uintptr_t)mbi.BaseAddress + mbi.RegionSize;

        if (mbi.State == IOP_MEM_FREE)
        {
            /* Reservations are 64 KB-granular, so the start of a free region
             * is not necessarily a legal base. */
            uintptr_t base = (addr + 0xffffu) & ~(uintptr_t)0xffffu;

            while (base + (uintptr_t)size <= region_end &&
                   base + (uintptr_t)size <= IOP_ARENA_CEIL)
            {
                u_char *p = IopArenaTry(base, size);
                if (p != NULL)
                    return p;
                base += 0x10000u;
            }
        }

        addr = region_end;
    }

    return NULL;
#else
    for (uintptr_t hint = IOP_ARENA_FLOOR;
         hint + (uintptr_t)size <= IOP_ARENA_CEIL; hint += 0x00100000)
    {
        u_char *p = IopArenaTry(hint, size);
        if (p != NULL)
            return p;
    }

    return NULL;
#endif
}

/*
 * The main arena.  It has to be BIG, and it does not have to be low.
 *
 * Big because the game's own view of IOP RAM is 2 MB and it uses it: the sound
 * streamer alone asks for 256 KB ring buffers, and starving that is what makes
 * StreamStart() fail after a few effects.  Not low because nothing this
 * allocator hands out is ever pushed through the ROM's 24-bit IOP address mask
 * -- ring_buf_top and friends are only ever required to round-trip through an
 * `int`.  The one allocator whose addresses DO meet that mask has its own pool;
 * see IopLowPoolInit().
 */
static void IopArenaInit(void)
{
    if (iop_arena != NULL)
        return;

    /* Low is still preferred -- it keeps every IOP address in one small,
     * easily-recognised range -- but it is no longer load-bearing. */
    static const int kSizes[] = { IOP_ARENA_SIZE, 2 * 1024 * 1024 };

    for (size_t k = 0; k < sizeof(kSizes) / sizeof(kSizes[0]); k++)
    {
        iop_arena = IopReserveLow(kSizes[k]);
        if (iop_arena != NULL)
        {
            iop_arena_size = kSizes[k];
            return;
        }
    }

    for (uintptr_t hint = IOP_ARENA_CEIL; hint < 0xf0000000u;
         hint += 0x01000000)
    {
        iop_arena = IopArenaTry(hint, IOP_ARENA_SIZE);
        if (iop_arena != NULL)
        {
            iop_arena_size = IOP_ARENA_SIZE;
            return;
        }
    }

    /* Last resort.  Everything still works until an address is squeezed
     * through an `int`, which is exactly when it will not. */
    iop_arena = (u_char *)calloc(1, IOP_ARENA_SIZE);
    iop_arena_size = (iop_arena != NULL) ? IOP_ARENA_SIZE : 0;
    if ((uintptr_t)iop_arena > 0xffffffffu)
        printf("iop: arena at %p is above 4GB -- IOP addresses will truncate\n",
               (void *)iop_arena);
}

/* --------------------------------------------------------------------------
 *  The EE->IOP heap, and why it gets a pool of its own.
 *
 *  audiodec.c reads the SPU2 auto-DMA play position back as
 *
 *      pos = (sceSdRemote(SDR_BLOCK_TRANS_STATUS, core) & 0xffffff) - iopBuff
 *
 *  That mask is the IOP's 24-bit address space.  On hardware it is a no-op,
 *  because every IOP address is under 2 MB.  Here it is a no-op only while the
 *  ring sits below 16 MB: above it, `pos` goes hugely negative, iopGetArea()
 *  reports a negative writable span, and the pump stops dead after the preload
 *  -- and the movie loses its PICTURE as well as its sound, because the
 *  demuxer cannot step past the audio packet its full ring is refusing.
 *
 *  The main arena cannot promise that.  It needs megabytes, the whole window
 *  below 16 MB is 12 MB, and bottom-up ASLR decides run by run whether a block
 *  that size is free there -- which is exactly what made a movie play on one
 *  launch and not the next.
 *
 *  But only ONE allocator's addresses ever reach that mask: sceSifAllocIopHeap(),
 *  whose single caller in the whole game is movie.c's iopalloc(), asking for
 *  0x6000 + 0x800.  So it gets a small dedicated reservation instead.  128 KB
 *  is two allocation granules; finding that free somewhere in a 12 MB window is
 *  not a gamble the way four megabytes was.
 * ------------------------------------------------------------------------ */
#define IOP_LOW_POOL_SIZE   (128 * 1024)

static u_char *iop_low_pool;
static int     iop_low_pool_size;
static int     iop_low_used;

static void IopLowPoolInit(void)
{
    if (iop_low_pool != NULL)
        return;

    static const int kSizes[] = { IOP_LOW_POOL_SIZE, 64 * 1024 };

    for (size_t k = 0; k < sizeof(kSizes) / sizeof(kSizes[0]); k++)
    {
        iop_low_pool = IopReserveLow(kSizes[k]);
        if (iop_low_pool != NULL)
        {
            iop_low_pool_size = kSizes[k];
            return;
        }
    }

    printf("iop: WARNING no free memory below 16MB for the EE->IOP heap --"
           " movie audio will stall and the movie will show no picture\n");
}

#if defined(__GNUC__)
/*
 * Claim the arena before anything else in the program can.
 *
 * Creating it on demand is far too late -- the first IOP allocation happens
 * when a movie starts, long after the address space has filled up.  So was
 * doing it at the top of main(): MioPan compiles as C++, so every file-scope
 * object in the program is constructed before main() is entered, and on
 * Windows the CRT heap grows bottom-up through exactly the addresses the arena
 * needs.  44 MB of the window were already gone by the first statement of
 * main(), which is how the arena ended up at 0x03000000 and movie audio
 * looped its first buffer.
 *
 * Priority 101 is the earliest an application may ask for (GCC reserves 0-100
 * for the implementation), and prioritised .init_array entries sort ahead of
 * unprioritised ones -- so this runs before any global constructor, with the
 * address space still empty.  Single-threaded at this point, hence no lock.
 */
__attribute__((constructor(101)))
static void IopArenaEarlyReserve(void)
{
    /* The low pool first: it is the one with a hard placement requirement, and
     * the arena would otherwise be free to take the block it needs. */
    IopLowPoolInit();
    IopArenaInit();
}
#elif defined(_MSC_VER)
static int IopArenaEarlyReserve(void)
{
    /* The low pool first -- see the GNU branch above. */
    IopLowPoolInit();
    IopArenaInit();
    return 0;
}

/* The same thing for MSVC, which has no constructor priorities.  The CRT runs
 * the .CRT$XC* sections in name order and puts C++ dynamic initialisers in
 * .CRT$XCU, so an entry in .CRT$XCT runs before all of them. */
#pragma section(".CRT$XCT", long, read)
__declspec(allocate(".CRT$XCT")) static int (*iop_arena_early_reserve)(void) =
    IopArenaEarlyReserve;
#endif

/* --------------------------------------------------------------------------
 *  MioPan_IopAllocIopHeap  --  what sceSifAllocIopHeap() serves from.
 *
 *  The low pool, because these addresses meet the ROM's 24-bit mask.  Blocks go
 *  into the shared table so MioPan_IopFreeSysMemory() frees them unchanged; the
 *  first-fit scan is restricted to blocks that came from this pool so a high
 *  arena block can never be handed back for a low request.
 * ------------------------------------------------------------------------ */
void *MioPan_IopAllocIopHeap(int size)
{
    if (size <= 0)
        return NULL;

    IopLock();
    IopLowPoolInit();

    size = (size + 63) & ~63;

    for (int i = 0; i < IOP_MAX_BLOCKS; i++)
    {
        if (iop_blocks[i].base != NULL && !iop_blocks[i].used &&
            iop_blocks[i].size >= size &&
            iop_low_pool != NULL &&
            iop_blocks[i].base >= iop_low_pool &&
            iop_blocks[i].base < iop_low_pool + iop_low_pool_size)
        {
            iop_blocks[i].used = 1;
            memset(iop_blocks[i].base, 0, (size_t)size);
            IopUnlock();
            return iop_blocks[i].base;
        }
    }

    if (iop_low_pool != NULL && iop_low_used + size <= iop_low_pool_size)
    {
        for (int i = 0; i < IOP_MAX_BLOCKS; i++)
        {
            if (iop_blocks[i].base == NULL)
            {
                iop_blocks[i].base = iop_low_pool + iop_low_used;
                iop_blocks[i].size = size;
                iop_blocks[i].used = 1;
                iop_low_used += size;
                memset(iop_blocks[i].base, 0, (size_t)size);
                IopUnlock();
                return iop_blocks[i].base;
            }
        }
    }

    IopUnlock();

    /* Falling back to the main arena keeps the movie playing rather than
     * hanging iopalloc(), but if the arena is high the 24-bit mask will stop
     * the audio pump -- so say so instead of failing quietly. */
    printf("iop: EE->IOP heap exhausted (%d bytes); falling back to the main"
           " arena -- movie audio may stall\n", size);

    return MioPan_IopAllocSysMemory(1, size, NULL);
}

void *MioPan_IopAllocSysMemory(int mode, int size, void *ptr)
{
    (void)mode;
    (void)ptr;

    if (size <= 0)
        return NULL;

    IopLock();
    IopArenaInit();

    size = (size + 63) & ~63;

    /* First fit over the freed blocks, then bump.  The ROM allocates a handful
     * of ring buffers and frees them when a stream ends, so this never has to
     * be clever. */
    for (int i = 0; i < IOP_MAX_BLOCKS; i++)
    {
        if (iop_blocks[i].base != NULL && !iop_blocks[i].used && iop_blocks[i].size >= size)
        {
            iop_blocks[i].used = 1;
            memset(iop_blocks[i].base, 0, (size_t)size);
            IopUnlock();
            return iop_blocks[i].base;
        }
    }

    if (iop_arena != NULL && iop_arena_used + size <= iop_arena_size)
    {
        for (int i = 0; i < IOP_MAX_BLOCKS; i++)
        {
            if (iop_blocks[i].base == NULL)
            {
                iop_blocks[i].base = iop_arena + iop_arena_used;
                iop_blocks[i].size = size;
                iop_blocks[i].used = 1;
                iop_arena_used += size;
                memset(iop_blocks[i].base, 0, (size_t)size);
                IopUnlock();
                return iop_blocks[i].base;
            }
        }
    }

    IopUnlock();

    /* StreamStart() retries forever on NULL rather than failing, which is the
     * ROM's own behaviour -- there is no way to report this to the EE. */
    printf("iop: out of IOP memory (%d bytes)\n", size);
    return NULL;
}

int MioPan_IopFreeSysMemory(void *ptr)
{
    if (ptr == NULL)
        return 0;

    IopLock();
    for (int i = 0; i < IOP_MAX_BLOCKS; i++)
    {
        if (iop_blocks[i].base == ptr)
        {
            iop_blocks[i].used = 0;
            break;
        }
    }
    IopUnlock();

    return 0;
}

int MioPan_IopMemIsBareOffset(const void *addr)
{
    if (addr == NULL)
        return 0;

    /* An address inside the IOP's own 2 MB map is not host memory whether an
     * arena exists or not.  This half matters because the arena is created
     * lazily by MioPan_IopAllocSysMemory() and one whole supply of IOP
     * addresses never goes through it: sceSifAllocIopHeap() (sifdev.cpp) is a
     * bump cursor over that map, and it is where movie.c's iopalloc() gets
     * audiodec.c's rings.  Without this the arena-NULL case waved a 0x16000
     * straight into a memcpy. */
    if ((uintptr_t)addr < IOP_MAP_LIMIT)
        return 1;

    /* Inside either real reservation is real host memory, and has to be said
     * explicitly now that there are two of them.
     *
     * The fallback below is a heuristic -- "IOP addresses are small numbers,
     * host memory starts at the arena" -- and it was correct while the arena
     * was the only pool.  It is not any more: the EE->IOP heap gets its own
     * reservation, claimed BEFORE the arena precisely so it wins the scarce
     * space under 16 MB, which puts it at a lower address than the arena.
     * Without this whitelist every movie audio buffer read as a bare offset,
     * sceSifSetDma() refused to write it, and movies played silently. */
    if (iop_low_pool != NULL &&
        (const u_char *)addr >= iop_low_pool &&
        (const u_char *)addr < iop_low_pool + iop_low_pool_size)
        return 0;

    if (iop_arena != NULL &&
        (const u_char *)addr >= iop_arena &&
        (const u_char *)addr < iop_arena + iop_arena_size)
        return 0;

    return iop_arena != NULL && (uintptr_t)addr < (uintptr_t)iop_arena;
}

/* The IOP had 2 MB, and PrintIOPMem() only ever prints these. */
int MioPan_IopQueryMaxFreeMemSize(void)
{
    return 2 * 1024 * 1024;
}

int MioPan_IopQueryTotalFreeMemSize(void)
{
    return 2 * 1024 * 1024;
}

/* --------------------------------------------------------------------------
 *  Host control
 * ------------------------------------------------------------------------ */

int MioPan_IopHostShouldShutdown(void)
{
    return iop_shutdown;
}

void MioPan_IopHostShutdown(void)
{
    if (iop_shutdown)
        return;

    iop_shutdown = 1;

    for (int i = 0; i < IOP_MAX_TIMERS; i++)
        iop_timers[i].running = 0;

    /* Release everything anyone could be parked on, so the blocking calls
     * return and each thread runs off the end of its own loop. */
    for (int i = 0; i < IOP_MAX_SEMA; i++)
    {
        if (iop_semas[i].sem != NULL)
        {
            for (int n = 0; n < IOP_MAX_THREADS; n++)
                SDL_SignalSemaphore(iop_semas[i].sem);
        }
    }

    for (int i = 0; i < IOP_MAX_THREADS; i++)
    {
        if (iop_threads[i].wake != NULL)
            SDL_SignalSemaphore(iop_threads[i].wake);
    }

    for (int i = 0; i < IOP_MAX_TIMERS; i++)
    {
        if (iop_timers[i].thread != NULL)
        {
            SDL_WaitThread(iop_timers[i].thread, NULL);
            iop_timers[i].thread = NULL;
        }
        memset(&iop_timers[i], 0, sizeof(iop_timers[i]));
    }

    for (int i = 0; i < IOP_MAX_THREADS; i++)
    {
        if (iop_threads[i].thread != NULL)
        {
            SDL_WaitThread(iop_threads[i].thread, NULL);
            iop_threads[i].thread = NULL;
        }
        if (iop_threads[i].wake != NULL)
        {
            SDL_DestroySemaphore(iop_threads[i].wake);
            iop_threads[i].wake = NULL;
        }
        memset(&iop_threads[i], 0, sizeof(iop_threads[i]));
    }

    for (int i = 0; i < IOP_MAX_SEMA; i++)
    {
        if (iop_semas[i].sem != NULL)
        {
            SDL_DestroySemaphore(iop_semas[i].sem);
            iop_semas[i].sem = NULL;
        }
        memset(&iop_semas[i], 0, sizeof(iop_semas[i]));
    }

    if (iop_table_lock != NULL)
    {
        SDL_DestroyMutex(iop_table_lock);
        iop_table_lock = NULL;
    }
}

/* --------------------------------------------------------------------------
 *  ioman
 * ------------------------------------------------------------------------ */

int MioPan_IopLseek(int fd, int offset, int whence)
{
    (void)fd;
    (void)offset;
    (void)whence;

    /* Unreachable in practice: MyOpen() is an empty stub in this build, so
     * REQ_FILE_SIZE never has a real descriptor to seek. */
    return 0;
}
