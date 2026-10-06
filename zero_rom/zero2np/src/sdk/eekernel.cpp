/* ==========================================================================
 *  eekernel.cpp  (EE kernel / interrupt helpers -- PC-port shim)
 * ======================================================================== */

#define MIOPAN_EEKERNEL_NO_COMPAT_MACROS
#include "eekernel.h"

#include "../miopan/os/miopan_time.h"   // MioPan_Sleep
#include "../miopan/os/miopan_thread.h" // threads / semaphores / mutex / TLS

#include <string.h>

#define MIOPAN_MAX_SEMAS   128
#define MIOPAN_MAX_THREADS 64
#define MIOPAN_MAIN_THREAD_ID 1

typedef struct HostSema
{
    int id;
    MioPan_Sema *sem;
    int maxCount;
} HostSema;

typedef struct HostThread
{
    int id;
    struct ThreadParam param;
    void *arg;
    MioPan_Thread *thread;
    MioPan_Sema *wake;
    int started;
} HostThread;

static HostSema s_semas[MIOPAN_MAX_SEMAS];
static HostThread s_threads[MIOPAN_MAX_THREADS];
static MioPan_Tls *s_threadTls;
static MioPan_Mutex *s_loaderSuspendMutex;
static int s_loaderSuspendDepth;
static u_long s_gsImr = 0;

static HostSema *FindSema(int sema_id)
{
    int i;

    for (i = 0; i < MIOPAN_MAX_SEMAS; i++)
    {
        if (s_semas[i].id == sema_id)
        {
            return &s_semas[i];
        }
    }

    return 0;
}

static HostThread *FindThread(int thread_id)
{
    int i;

    for (i = 0; i < MIOPAN_MAX_THREADS; i++)
    {
        if (s_threads[i].id == thread_id)
        {
            return &s_threads[i];
        }
    }

    return 0;
}

static MioPan_Tls *GetThreadTls(void)
{
    if (s_threadTls == 0)
    {
        s_threadTls = MioPan_TlsCreate();
    }
    return s_threadTls;
}

static MioPan_Mutex *GetLoaderSuspendMutex(void)
{
    if (s_loaderSuspendMutex == 0)
    {
        s_loaderSuspendMutex = MioPan_MutexCreate();
    }

    return s_loaderSuspendMutex;
}

/* On the PS2, SuspendThread() genuinely freezes the loader thread, so the game
 * thread could safely reorder the load queue underneath it.  Here the loader is
 * a real host thread, and SuspendThread() is only a mutex -- which excludes
 * nothing unless the loader holds it too.  These let the loader claim that same
 * mutex for the span in which it is reading queue entries, so a concurrent
 * FileLoadReq() cannot swap slot contents out from under it. */
void MioPan_LoaderCriticalEnter(void)
{
    MioPan_Mutex *mutex = GetLoaderSuspendMutex();

    if (mutex != 0)
    {
        MioPan_MutexLock(mutex);
    }
}

void MioPan_LoaderCriticalLeave(void)
{
    if (s_loaderSuspendMutex != 0)
    {
        MioPan_MutexUnlock(s_loaderSuspendMutex);
    }
}

static void ThreadPassSuspendGate(void)
{
    MioPan_Mutex *mutex = s_loaderSuspendMutex;

    if (mutex != 0)
    {
        MioPan_MutexLock(mutex);
        MioPan_MutexUnlock(mutex);
    }
}

static void HostThreadMain(void *data)
{
    HostThread *thread = (HostThread *)data;

    MioPan_TlsSet(GetThreadTls(), thread);

    if (thread->param.entry != 0)
    {
        void (*entry)(void *) = (void (*)(void *))thread->param.entry;
        entry(thread->arg);
    }
}

extern "C" {

int MioPan_CreateSema(struct SemaParam *param)
{
    int i;
    int initCount = param != 0 ? param->initCount : 0;

    for (i = 0; i < MIOPAN_MAX_SEMAS; i++)
    {
        if (s_semas[i].id == 0)
        {
            s_semas[i].sem = MioPan_SemaCreate(initCount);
            if (s_semas[i].sem == 0)
            {
                return -1;
            }

            s_semas[i].id = i + 1;
            s_semas[i].maxCount = param != 0 ? param->maxCount : 0;
            return s_semas[i].id;
        }
    }

    return -1;
}

int MioPan_DeleteSema(int sema_id)
{
    HostSema *sema = FindSema(sema_id);

    if (sema == 0)
    {
        return -1;
    }

    if (sema->sem != 0)
    {
        MioPan_SemaDestroy(sema->sem);
    }

    memset(sema, 0, sizeof(*sema));
    return 0;
}

int MioPan_SignalSema(int sema_id)
{
    HostSema *sema = FindSema(sema_id);

    if (sema == 0 || sema->sem == 0)
    {
        return -1;
    }

    MioPan_SemaSignal(sema->sem);
    return 0;
}

int MioPan_iSignalSema(int sema_id)
{
    return MioPan_SignalSema(sema_id);
}

int MioPan_WaitSema(int sema_id)
{
    HostSema *sema = FindSema(sema_id);

    if (sema == 0 || sema->sem == 0)
    {
        return -1;
    }

    MioPan_SemaWait(sema->sem);
    ThreadPassSuspendGate();
    return 0;
}

int MioPan_PollSema(int sema_id)
{
    HostSema *sema = FindSema(sema_id);

    if (sema == 0 || sema->sem == 0)
    {
        return -1;
    }

    return MioPan_SemaTryWait(sema->sem) ? 0 : -1;
}

int MioPan_CreateThread(struct ThreadParam *param)
{
    int i;

    if (param == 0 || param->entry == 0)
    {
        return -1;
    }

    for (i = 0; i < MIOPAN_MAX_THREADS; i++)
    {
        if (s_threads[i].id == 0)
        {
            s_threads[i].id = i + 2;
            s_threads[i].param = *param;
            s_threads[i].param.status = 0;
            s_threads[i].param.currentPriority = param->initPriority;
            s_threads[i].arg = 0;
            s_threads[i].thread = 0;
            s_threads[i].wake = MioPan_SemaCreate(0);
            s_threads[i].started = 0;

            if (s_threads[i].wake == 0)
            {
                memset(&s_threads[i], 0, sizeof(s_threads[i]));
                return -1;
            }

            return s_threads[i].id;
        }
    }

    return -1;
}

int MioPan_StartThread(int thread_id, void *arg)
{
    HostThread *thread = FindThread(thread_id);

    if (thread == 0 || thread->started)
    {
        return -1;
    }

    thread->arg = arg;
    thread->thread = MioPan_ThreadCreate(HostThreadMain, thread, "ee-thread");
    if (thread->thread == 0)
    {
        return -1;
    }

    thread->started = 1;
    thread->param.status = 1;
    return 0;
}

/* PORT: there is no way to pre-empt a host thread from outside, so this marks
 * the slot dormant and leaves the thread to return on its own.  playPssEnd() is
 * the only caller and videoDecMain() has already returned by then -- it exits
 * the moment the decoder reports end of stream. */
int MioPan_TerminateThread(int thread_id)
{
    HostThread *thread = FindThread(thread_id);

    if (thread == 0)
    {
        return -1;
    }

    thread->param.status = 0;
    return 0;
}

int MioPan_DeleteThread(int thread_id)
{
    HostThread *thread = FindThread(thread_id);

    if (thread == 0)
    {
        return -1;
    }

    if (thread->thread != 0)
    {
        MioPan_ThreadDetach(thread->thread);
    }

    if (thread->wake != 0)
    {
        MioPan_SemaDestroy(thread->wake);
    }

    memset(thread, 0, sizeof(*thread));
    return 0;
}

int MioPan_SuspendThread(int thread_id)
{
    MioPan_Mutex *mutex;

    if (FindThread(thread_id) == 0)
    {
        return -1;
    }

    mutex = GetLoaderSuspendMutex();
    if (mutex == 0)
    {
        return -1;
    }

    MioPan_MutexLock(mutex);
    s_loaderSuspendDepth++;
    return 0;
}

int MioPan_ResumeThread(int thread_id)
{
    if (FindThread(thread_id) == 0 || s_loaderSuspendMutex == 0)
    {
        return -1;
    }

    if (s_loaderSuspendDepth > 0)
    {
        s_loaderSuspendDepth--;
        MioPan_MutexUnlock(s_loaderSuspendMutex);
    }

    return 0;
}

int MioPan_GetThreadId(void)
{
    HostThread *thread = (HostThread *)MioPan_TlsGet(GetThreadTls());
    return thread != 0 ? thread->id : MIOPAN_MAIN_THREAD_ID;
}

int MioPan_SleepThread(void)
{
    HostThread *thread = (HostThread *)MioPan_TlsGet(GetThreadTls());

    if (thread == 0 || thread->wake == 0)
    {
        MioPan_Sleep(1);
        return 0;
    }

    MioPan_SemaWait(thread->wake);
    ThreadPassSuspendGate();
    return 0;
}

int MioPan_WakeupThread(int thread_id)
{
    HostThread *thread = FindThread(thread_id);

    if (thread == 0 || thread->wake == 0)
    {
        return -1;
    }

    MioPan_SemaSignal(thread->wake);
    return 0;
}

int MioPan_ChangeThreadPriority(int thread_id, int priority)
{
    HostThread *thread = FindThread(thread_id);

    if (thread == 0)
    {
        return -1;
    }

    thread->param.currentPriority = priority;
    return 0;
}

int MioPan_ReferThreadStatus(int thread_id, struct ThreadParam *info)
{
    HostThread *thread;

    if (info != 0)
    {
        memset(info, 0, sizeof(*info));
        thread = FindThread(thread_id);
        if (thread != 0)
        {
            *info = thread->param;
        }
        else if (thread_id == MIOPAN_MAIN_THREAD_ID)
        {
            info->status = 1;
            info->initPriority = 32;
            info->currentPriority = 32;
        }
    }

    return 0;
}

void MioPan_FlushCache(int mode)
{
    (void)mode;
}

int MioPan_SetDebugHandler(int cause, MioPanDebugHandler handler)
{
    (void)cause;
    (void)handler;
    return 0;
}

int MioPan_AddDmacHandler(int channel, MioPanDmacHandler handler, int next)
{
    (void)channel;
    (void)handler;
    (void)next;
    return 0;
}

int MioPan_AddDmacHandler2(int channel, MioPanDmacHandler2 handler, int next, void *arg)
{
    (void)channel;
    (void)handler;
    (void)next;
    (void)arg;
    return 0;
}

int MioPan_EnableDmac(int channel)
{
    (void)channel;
    return 0;
}

/* --------------------------------------------------------------------------
 *  INTC handlers
 *
 *  Real registry, no source of interrupts.  See the note in eekernel.h: the
 *  one registration in the tree is playpss.c's PAL frame-rate divider on cause
 *  2 (vblank start), and nothing calls MioPan_IntcRaise() because the port's
 *  frame loop has no vblank interrupt.  Keeping the registry honest means a
 *  future vblank hook is one call away rather than a rewrite.
 * ------------------------------------------------------------------------ */

#define MIOPAN_MAX_INTC 16

typedef struct
{
    int               id;
    int               cause;
    MioPanIntcHandler handler;
} HostIntc;

static HostIntc s_intc[MIOPAN_MAX_INTC];
static int      s_intc_enabled[32];
static int      s_intc_next_id = 1;

int MioPan_AddIntcHandler(int cause, MioPanIntcHandler handler, int next)
{
    int i;

    (void)next;

    if (handler == 0)
    {
        return -1;
    }

    for (i = 0; i < MIOPAN_MAX_INTC; i++)
    {
        if (s_intc[i].id == 0)
        {
            s_intc[i].id      = s_intc_next_id++;
            s_intc[i].cause   = cause;
            s_intc[i].handler = handler;
            return s_intc[i].id;
        }
    }

    return -1;
}

int MioPan_RemoveIntcHandler(int cause, int handler_id)
{
    int i;

    for (i = 0; i < MIOPAN_MAX_INTC; i++)
    {
        if (s_intc[i].id == handler_id && s_intc[i].cause == cause)
        {
            memset(&s_intc[i], 0, sizeof(s_intc[i]));
            return 0;
        }
    }

    return -1;
}

int MioPan_EnableIntc(int cause)
{
    if (cause >= 0 && cause < 32)
    {
        s_intc_enabled[cause] = 1;
    }

    return 0;
}

int MioPan_DisableIntc(int cause)
{
    if (cause >= 0 && cause < 32)
    {
        s_intc_enabled[cause] = 0;
    }

    return 0;
}

void MioPan_IntcRaise(int cause)
{
    int i;

    if (cause < 0 || cause >= 32 || s_intc_enabled[cause] == 0)
    {
        return;
    }

    for (i = 0; i < MIOPAN_MAX_INTC; i++)
    {
        if (s_intc[i].id != 0 && s_intc[i].cause == cause)
        {
            s_intc[i].handler(cause);
        }
    }
}

int MioPan_sceDevVif0Reset(void)
{
    return 0;
}

int MioPan_sceDevVu0Reset(void)
{
    return 0;
}

u_long MioPan_sceGsGetIMR(void)
{
    return s_gsImr;
}

u_long MioPan_sceGsPutIMR(u_long imr)
{
    u_long old = s_gsImr;
    s_gsImr = imr;
    return old;
}

}
