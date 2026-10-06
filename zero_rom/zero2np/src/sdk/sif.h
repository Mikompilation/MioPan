/* sif.h — PS2 SIF (EE<->IOP) core — PC-port shim. */
#ifndef _SIF_H
#define _SIF_H
#include "scetypes.h"
#include "libexcep.h"          /* sceExcepSetDebugIOPHandler compatibility */

#ifdef __cplusplus
extern "C" {
#endif

void *sceSifAllocSysMemory(int mode, u_int size, void *addr);

#ifdef __cplusplus
}
#endif

#endif /* _SIF_H */
