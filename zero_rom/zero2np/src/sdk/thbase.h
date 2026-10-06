/* thbase.h  (IOP thread manager — PC-port shim; see iop_host.h) */
#ifndef _IOP_THBASE_H
#define _IOP_THBASE_H

#include "iop_host.h"

/* TH_C — the only attribute iopsys.irx ever asks for. */
#define TH_C 0x02000000

/* The EE kernel shim declares a different struct under this name; see
 * iop_host.h. */
#define ThreadParam         IopThreadParam

/* The IOP SDK's names.  Redirected because CreateThread, ExitThread and
 * TerminateThread are Win32 API functions on this host. */
#define CreateThread        MioPan_IopCreateThread
#define DeleteThread        MioPan_IopDeleteThread
#define StartThread         MioPan_IopStartThread
#define ExitThread          MioPan_IopExitThread
#define TerminateThread     MioPan_IopTerminateThread
#define GetThreadId         MioPan_IopGetThreadId
#define ReferThreadStatus   MioPan_IopReferThreadStatus
#define SleepThread         MioPan_IopSleepThread
#define WakeupThread        MioPan_IopWakeupThread
#define iWakeupThread       MioPan_IopWakeupThread
#define DelayThread         MioPan_IopDelayThread
#define USec2SysClock       MioPan_IopUSec2SysClock

#endif /* _IOP_THBASE_H */
