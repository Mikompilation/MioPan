/* loadcore.h  (IOP module loader — PC-port shim; see iop_host.h) */
#ifndef _IOP_LOADCORE_H
#define _IOP_LOADCORE_H

#include "iop_host.h"

/* iopsys.irx's own module record: `ModuleInfo Module = { "iopsys.irx", 0x0100 }`
 * at the top of iop.c.  The loader read it; on the host nothing does, but it
 * is kept so the reconstruction stays faithful. */
typedef struct
{
    char    *name;
    u_short  version;
} ModuleInfo;

/* start()'s return value: 0 keeps the module resident, 1 unloads it. */
#define MODULE_RESIDENT_END     0
#define MODULE_NO_RESIDENT_END  1

#define FlushDcache MioPan_IopFlushDcache

#endif /* _IOP_LOADCORE_H */
