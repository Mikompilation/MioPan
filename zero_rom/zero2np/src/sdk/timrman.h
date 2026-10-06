/* timrman.h  (IOP hardware timers — PC-port shim; see iop_host.h) */
#ifndef _IOP_TIMRMAN_H
#define _IOP_TIMRMAN_H

#include "iop_host.h"

#define AllocHardTimer      MioPan_IopAllocHardTimer
#define SetTimerHandler     MioPan_IopSetTimerHandler
#define SetupHardTimer      MioPan_IopSetupHardTimer
#define StartHardTimer      MioPan_IopStartHardTimer
#define StopHardTimer       MioPan_IopStopHardTimer

#endif /* _IOP_TIMRMAN_H */
