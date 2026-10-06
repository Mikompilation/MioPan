/* ==========================================================================
 *  g3dMemory.h
 *
 *  The g3d engine's thin allocator shim.  g3dMalloc / g3dFree forward to the
 *  debug heap (mem_dbgGetMem / mem_dbgFreeMem); g3dMalloc carries a tag string
 *  for the debug heap's leak tracking.  Implemented in g3dMemory.c.
 *
 *  Reconstructed from the Feb 6 2004 prototype (SLES_523.84).
 * ======================================================================== */

#ifndef _G3DMEMORY_H
#define _G3DMEMORY_H

#include <stddef.h>             /* size_t */

void *g3dMalloc(size_t size, char *pStr);
void  g3dFree(void *p);

#endif /* _G3DMEMORY_H */
