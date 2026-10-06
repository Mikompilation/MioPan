/* ==========================================================================
 *  eekernel.h  (EE kernel / libgraph entry points — PC-port shim)
 *
 *  Declarations (not implementations) for the handful of EE runtime services
 *  the reconstruction calls: semaphores, cache/exception/DMAC handlers, the GS
 *  interrupt-mask register, and scePrintf.  On the PC these are stubbed/ported
 *  under src/sdk as the port progresses; here they only need to be declared so
 *  the callers compile.
 * ======================================================================== */

#ifndef _EEKERNEL_H
#define _EEKERNEL_H

#include "scetypes.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ---- threads / semaphores (libkernel) --------------------------------- */
/* Field layout verbatim from the prototype's debug info (types.txt). */
struct SemaParam
{
    int          currentCount;
    int          maxCount;
    int          initCount;
    int          numWaitThreads;
    unsigned int attr;
    unsigned int option;
};

int MioPan_CreateSema(struct SemaParam *param);
int MioPan_DeleteSema(int sema_id);
int MioPan_SignalSema(int sema_id);
int MioPan_iSignalSema(int sema_id);
int MioPan_WaitSema(int sema_id);
int MioPan_PollSema(int sema_id);

/* ---- threads (libkernel) ---------------------------------------------- */
struct ThreadParam
{
    int          status;

    void (*entry)(void *dummy);
    void        *stack;
    int          stackSize;
    void        *gpReg;
    int          initPriority;
    int          currentPriority;
    unsigned int attr;
    unsigned int option;
    int          waitType;
    int          waitId;
    int          wakeupCount;
};

int  MioPan_CreateThread(struct ThreadParam *param);
int  MioPan_StartThread(int thread_id, void *arg);

/* PORT: a host thread cannot be pre-empted from outside, so Terminate only
 * marks the slot dormant and Delete detaches it and gives the slot back.  Both
 * are safe for the one caller in the tree -- playPssEnd() tears down
 * videoDecMain(), which returns of its own accord as soon as the decoder
 * reports end of stream. */
int  MioPan_TerminateThread(int thread_id);
int  MioPan_DeleteThread(int thread_id);

int  MioPan_SuspendThread(int thread_id);

/* Claim the suspend mutex for the span the loader thread spends reading load
 * queue entries -- see the comment on these in eekernel.cpp. */
void MioPan_LoaderCriticalEnter(void);
void MioPan_LoaderCriticalLeave(void);
int  MioPan_ResumeThread(int thread_id);
int  MioPan_GetThreadId(void);
int  MioPan_SleepThread(void);
int  MioPan_WakeupThread(int thread_id);
int  MioPan_ChangeThreadPriority(int thread_id, int priority);
int  MioPan_ReferThreadStatus(int thread_id, struct ThreadParam *info);

/* ---- cache / exception / DMAC handlers -------------------------------- */
typedef void (*MioPanDebugHandler)(u_int stat, u_int cause, u_int epc,
                                   u_int bva, u_int bpa, u_long128 *gpr);
typedef int (*MioPanDmacHandler)(int channel);
typedef int (*MioPanDmacHandler2)(int channel, void *arg, void *addr);

void MioPan_FlushCache(int mode);
int  MioPan_SetDebugHandler(int cause, MioPanDebugHandler handler);
int  MioPan_AddDmacHandler(int channel, MioPanDmacHandler handler, int next);
int  MioPan_AddDmacHandler2(int channel, MioPanDmacHandler2 handler, int next, void *arg);
int  MioPan_EnableDmac(int channel);

/* ---- INTC handlers ----------------------------------------------------- */
/* Cause 2 is INTC_VBLANK_START, the only one the tree registers for: playpss.c
 * hangs its PAL frame-rate divider off it.
 *
 * PORT: the registry is real -- Add returns a handle Remove takes back, and
 * MioPan_IntcRaise() dispatches -- but NOTHING calls MioPan_IntcRaise() yet,
 * because the port's frame loop has no vblank interrupt to hang it off.  The
 * consequence is confined and named at the call site: videoDecMain() would park
 * on its semaphore for ever if a real decoder ever ran it in PAL mode. */
typedef int (*MioPanIntcHandler)(int cause);

int  MioPan_AddIntcHandler(int cause, MioPanIntcHandler handler, int next);
int  MioPan_RemoveIntcHandler(int cause, int handler_id);
int  MioPan_EnableIntc(int cause);
int  MioPan_DisableIntc(int cause);
void MioPan_IntcRaise(int cause);

/* Device reset helpers. */
int  MioPan_sceDevVif0Reset(void);
int  MioPan_sceDevVu0Reset(void);

/* EE COP0 pipeline sync + interrupt enable (asm intrinsics on the target). */
#ifndef SYNC
#define SYNC(t) ((void)0)
#endif
#ifndef EI
#define EI()    ((void)0)
#endif

/* ---- GS interrupt-mask register --------------------------------------- */
u_long MioPan_sceGsGetIMR(void);
u_long MioPan_sceGsPutIMR(u_long imr);

/* scePrintf is provided as an ambient `#define scePrintf printf` by
 * pc_prefix.h (force-included), so it is intentionally not declared here. */

#ifdef __cplusplus
}
#endif

/* VU0 CFC2 (read a COP2 control register) — no VU0 on the host. */
#ifndef MIOPAN_EEKERNEL_NO_COMPAT_MACROS
#define CreateSema             MioPan_CreateSema
#define DeleteSema             MioPan_DeleteSema
#define SignalSema             MioPan_SignalSema
#define iSignalSema            MioPan_iSignalSema
#define WaitSema               MioPan_WaitSema
#define PollSema               MioPan_PollSema
#define CreateThread           MioPan_CreateThread
#define StartThread            MioPan_StartThread
#define TerminateThread        MioPan_TerminateThread
#define DeleteThread           MioPan_DeleteThread
#define SuspendThread          MioPan_SuspendThread
#define ResumeThread           MioPan_ResumeThread
#define GetThreadId            MioPan_GetThreadId
#define SleepThread            MioPan_SleepThread
#define WakeupThread           MioPan_WakeupThread
#define ChangeThreadPriority   MioPan_ChangeThreadPriority
#define ReferThreadStatus      MioPan_ReferThreadStatus
#define FlushCache             MioPan_FlushCache
#define SetDebugHandler        MioPan_SetDebugHandler
#define AddDmacHandler         MioPan_AddDmacHandler
#define AddDmacHandler2        MioPan_AddDmacHandler2
#define EnableDmac             MioPan_EnableDmac
#define AddIntcHandler         MioPan_AddIntcHandler
#define RemoveIntcHandler      MioPan_RemoveIntcHandler
#define EnableIntc             MioPan_EnableIntc
#define DisableIntc            MioPan_DisableIntc
#define sceDevVif0Reset        MioPan_sceDevVif0Reset
#define sceDevVu0Reset         MioPan_sceDevVu0Reset
#define sceGsGetIMR            MioPan_sceGsGetIMR
#define sceGsPutIMR            MioPan_sceGsPutIMR
#endif

#ifndef _cfc2
#define _cfc2(reg) (0u)
#endif

#endif /* _EEKERNEL_H */
