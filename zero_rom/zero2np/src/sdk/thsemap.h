/* thsemap.h  (IOP semaphore manager — PC-port shim; see iop_host.h) */
#ifndef _IOP_THSEMAP_H
#define _IOP_THSEMAP_H

#include "iop_host.h"

/* The EE kernel shim declares a different struct under this name; see
 * iop_host.h. */
#define SemaParam           IopSemaParam

#define CreateSema          MioPan_IopCreateSema
#define DeleteSema          MioPan_IopDeleteSema
#define SignalSema          MioPan_IopSignalSema
/* The interrupt-context form; there is no separate one on the host. */
#define iSignalSema         MioPan_IopSignalSema
#define WaitSema            MioPan_IopWaitSema
#define PollSema            MioPan_IopPollSema
#define ReferSemaStatus     MioPan_IopReferSemaStatus

#endif /* _IOP_THSEMAP_H */
