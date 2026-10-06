/* ==========================================================================
 *  iop_host.h  (IOP kernel services — PC-port shim, shared declarations)
 *
 *  The reconstruction under system/iop/ is iopsys.irx: code that ran on the
 *  PS2's *other* CPU, against the IOP kernel rather than the EE one.  On the
 *  host both halves are one process, so the IOP's threads become real host
 *  threads and its kernel calls land here.
 *
 *  Every entry point is declared twice: once under its own MioPan_Iop* name,
 *  and once as a compatibility macro carrying the IOP SDK's name.  That is not
 *  cosmetic -- CreateThread, ExitThread and TerminateThread are all Win32 API
 *  functions, so the reconstructed sources cannot call them under those names
 *  on Windows without colliding with <windows.h>.  The macros let the sources
 *  keep the ROM's spelling.
 *
 *  The SDK-named headers (thbase.h, thsemap.h, ...) are thin wrappers over
 *  this one, so a source that includes <thbase.h> sees exactly what iopsys.irx
 *  saw and nothing more.
 *
 *  ThreadParam and SemaParam are spelled IopThreadParam / IopSemaParam here and
 *  macroed back in those wrappers: the EE kernel shim (eekernel.h, which
 *  pc_prefix.h force-includes everywhere) has its own different structs under
 *  the same two names.  On the PS2 they belonged to two separate CPUs and never
 *  met; in one process they do.
 * ======================================================================== */

#ifndef _IOP_HOST_H
#define _IOP_HOST_H

#include "scetypes.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* --------------------------------------------------------------------------
 *  Threads (thbase)
 * ------------------------------------------------------------------------ */

/* IOP thread entries take no argument.  The work pointer travels in
 * ThreadParam::option, and the thread recovers it with
 * ReferThreadStatus(0, &info) -> info.option -- see InitRingBufSub(),
 * PCMStreamCreate() and StreamCreate().
 *
 * On the IOP `option` was a 32-bit word and a pointer fitted in it.  Here it
 * has to be pointer-width or every one of those threads starts life with a
 * truncated `stp`. */
typedef struct
{
    u_int      attr;
    /* PORT: widened from u_int.  This carries a work POINTER -- see below --
     * and truncating it hands the thread a garbage record. */
    uintptr_t  option;
    void      *entry;
    int    stackSize;
    int    initPriority;
} IopThreadParam;

typedef struct
{
    u_int      attr;
    uintptr_t  option;              /* PORT: widened, as in IopThreadParam */
    int        status;
    void  *entry;
    void  *stack;
    int    stackSize;
    void  *gpReg;
    int    initPriority;
    int    currentPriority;
    int    waitType;
    int    waitId;
    int    wakeupCount;
    u_long *regContext;
    int    reserved1, reserved2, reserved3, reserved4;
} ThreadInfo;

typedef struct
{
    u_int lo;
    u_int hi;
} SysClock;

int  MioPan_IopCreateThread(IopThreadParam *param);
int  MioPan_IopDeleteThread(int thid);
int  MioPan_IopStartThread(int thid, void *arg);
int  MioPan_IopExitThread(void);
int  MioPan_IopTerminateThread(int thid);
int  MioPan_IopGetThreadId(void);
int  MioPan_IopReferThreadStatus(int thid, ThreadInfo *info);
int  MioPan_IopSleepThread(void);
int  MioPan_IopWakeupThread(int thid);
int  MioPan_IopDelayThread(int usec);
int  MioPan_IopUSec2SysClock(u_int usec, SysClock *clock);

/* --------------------------------------------------------------------------
 *  Semaphores (thsemap)
 * ------------------------------------------------------------------------ */

typedef struct
{
    u_int attr;
    u_int option;
    int   initCount;
    int   maxCount;
} IopSemaParam;

typedef struct
{
    u_int attr;
    u_int option;
    int   initCount;
    int   maxCount;
    int   currentCount;
    int   numWaitThreads;
} SemaInfo;

/* PollSema() answers this instead of the id when the count is already zero.
 * MyCdRead() and iopReqRead() both drain a semaphore by polling for it. */
#define KE_SEMA_ZERO (-419)

int MioPan_IopCreateSema(IopSemaParam *param);
int MioPan_IopDeleteSema(int semid);
int MioPan_IopSignalSema(int semid);
int MioPan_IopWaitSema(int semid);
int MioPan_IopPollSema(int semid);
int MioPan_IopReferSemaStatus(int semid, SemaInfo *info);

/* --------------------------------------------------------------------------
 *  Hard timers (timrman)
 * ------------------------------------------------------------------------ */

/* Returns the next compare value, or 0 to keep the current one. */
typedef u_int (*TimerHandler)(void *common);

int MioPan_IopAllocHardTimer(int source, int size, int prescale);
int MioPan_IopSetTimerHandler(int timer_id, u_int compare, TimerHandler handler, void *common);
int MioPan_IopSetupHardTimer(int timer_id, int source, int mode, int prescale);
int MioPan_IopStartHardTimer(int timer_id);
int MioPan_IopStopHardTimer(int timer_id);

/* --------------------------------------------------------------------------
 *  Interrupts (intrman) and cache (loadcore)
 *
 *  All no-ops.  The IOP used CpuSuspendIntr/CpuResumeIntr to keep a register
 *  write and its key-on from being split by an interrupt; host threads are
 *  preemptible anyway, and the SPU shim's own locking covers what matters.
 * ------------------------------------------------------------------------ */

int  MioPan_IopEnableIntr(int intr);
int  MioPan_IopCpuSuspendIntr(int *oldstat);
int  MioPan_IopCpuResumeIntr(int oldstat);
void MioPan_IopFlushDcache(void);

/* --------------------------------------------------------------------------
 *  System memory (sysmem)
 * ------------------------------------------------------------------------ */

void *MioPan_IopAllocSysMemory(int mode, int size, void *ptr);

/* What sceSifAllocIopHeap() serves from -- a small reservation guaranteed to
 * sit below 16 MB, because these are the only IOP addresses the ROM pushes
 * through its 24-bit mask (audiodec.c's auto-DMA play position).  Freed with
 * MioPan_IopFreeSysMemory() like any other block. */
void *MioPan_IopAllocIopHeap(int size);
int   MioPan_IopFreeSysMemory(void *ptr);
int   MioPan_IopQueryMaxFreeMemSize(void);
int   MioPan_IopQueryTotalFreeMemSize(void);

/* True if `addr` is non-NULL but below the IOP arena -- that is, it is an
 * offset into an IOP buffer that lost its base rather than an address.
 *
 * The ROM computes IOP addresses as base + index * size out of fields it keeps
 * in ints; when the base has been zeroed the result is small, non-NULL, and
 * mapped to nothing, so an ordinary "if (p != NULL)" waves it through.  The
 * arena is deliberately reserved at 16 MB or above and is 4 MB long, while such
 * an offset is by construction smaller than one buffer, so "below the arena"
 * separates the two cleanly.  Host pointers -- statics like iop_snd.c's
 * stop_block, which sceSdVoiceTrans() is legitimately handed -- sit far above
 * the arena in this build and are not rejected. */
int   MioPan_IopMemIsBareOffset(const void *addr);

/* --------------------------------------------------------------------------
 *  File I/O (ioman)
 * ------------------------------------------------------------------------ */

int MioPan_IopLseek(int fd, int offset, int whence);

/* --------------------------------------------------------------------------
 *  The IOP big kernel lock
 *
 *  The IOP was one CPU.  Exactly one of its threads ran at a time, and a
 *  switch could only happen where the ROM itself blocked -- WaitSema,
 *  SleepThread, DelayThread -- or in an interrupt.  Every critical section in
 *  iopsys.irx is written against that guarantee, which is why the ROM's own
 *  protection is CpuSuspendIntr(): against another *thread* it needed nothing.
 *
 *  On this host those threads are real, genuinely parallel host threads, and
 *  the EE calls iopCommand() directly on its own thread on top of that.  So
 *  the guarantee is gone and the ROM's records are raced: STREAM_WRK is
 *  written by iopCommand() while StreamVoiceThread() reads it, and
 *  IOP_RET_STATUS is written by StreamVoiceThread() while ee_iopMain() copies
 *  the whole struct out.
 *
 *  This lock puts the guarantee back.  Every IOP context holds it while it is
 *  running and drops it only inside the blocking primitives above, so IOP code
 *  is serialised against IOP code exactly where the ROM assumed it was, with
 *  no change to the reconstruction itself.
 *
 *  Held by: IopThreadMain (whole thread body), MioPan_IopBoot (start()) and
 *  MioPan_IopRpcDispatch (the RPC handler, on the EE's thread).
 *  Dropped by: WaitSema, SleepThread, DelayThread, StartThread's join, and
 *  sceCdRead's host read -- all points where the IOP was parked anyway.
 *
 *  NOT held by the SPU2 voice engine's decode thread.  That models hardware,
 *  which really did run alongside the IOP; and it calls into the ROM's SPU
 *  interrupt handlers while holding the voice engine's own lock, which an IOP
 *  context takes *under* this one -- taking this lock there would invert the
 *  order and deadlock.  What those handlers touch is a semaphore signal and a
 *  single register word; see the note in iop_libsd.cpp.
 * ------------------------------------------------------------------------ */

void MioPan_IopBklInit(void);
void MioPan_IopBklEnter(void);
void MioPan_IopBklLeave(void);

/* Drop every level held by this thread and report how many, so a blocking
 * call can give the CPU back and then restore its own nesting depth. */
int  MioPan_IopBklSuspend(void);
void MioPan_IopBklResume(int depth);

/* --------------------------------------------------------------------------
 *  Host control
 * ------------------------------------------------------------------------ */

/* Releases every parked IOP thread and joins them.  Call before tearing the
 * process down, or the stream threads keep running against freed buffers. */
void MioPan_IopHostShutdown(void);
int  MioPan_IopHostShouldShutdown(void);

#ifdef __cplusplus
}
#endif

#endif /* _IOP_HOST_H */
