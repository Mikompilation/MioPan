/* ==========================================================================
 *  g3dMemory.c
 *
 *  The g3d engine's allocator shim.  Every g3d allocation routes through here
 *  so it lands in the project debug heap (debug/mem_dbg.c), which tracks the
 *  block sizes for leak / overrun reporting.  g3dMalloc takes a tag string for
 *  that bookkeeping; the prototype build passes it through but mem_dbgGetMem
 *  only consumes the size.
 *
 *  Reconstructed from the Feb 6 2004 prototype (SLES_523.84).
 * ======================================================================== */

#include "g3dMemory.h"

#include "../../debug/mem_dbg.h"    /* mem_dbgGetMem / mem_dbgFreeMem */

/* --------------------------------------------------------------------------
 *  g3dMalloc
 *
 *  Allocate size bytes from the debug heap.  pStr is the caller's tag (kept
 *  for the debug heap's leak tracking).
 * ------------------------------------------------------------------------ */
void *g3dMalloc(size_t size, char *pStr)
{
    return mem_dbgGetMem(size);
}

/* --------------------------------------------------------------------------
 *  g3dFree
 * ------------------------------------------------------------------------ */
void g3dFree(void *p)
{
    mem_dbgFreeMem(p);
}
