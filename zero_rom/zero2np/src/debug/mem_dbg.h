/* ==========================================================================
 *  debug/mem_dbg.h
 *
 *  Public interface for the project debug heap (mem_dbg.c): a single HEAP_WRK
 *  the g3d allocator shim (graphics/graph3d/g3dMemory.c) and other debug-build
 *  consumers route allocations through so block sizes are tracked for leak /
 *  overrun reporting.  mem_dbgInit() carves the region out at boot; the
 *  get/free pair hand blocks out and back; the query/draw helpers report
 *  occupancy.  It is the same thin heapctrl wrapper as common/mem_util.c, over
 *  its own private heap.
 *
 *  Reconstructed from the Feb 6 2004 prototype (SLES_523.84).
 * ======================================================================== */

#ifndef _DEBUG_MEM_DBG_H
#define _DEBUG_MEM_DBG_H

#include "heapctrl.h"

void         mem_dbgInit(void *adrs, int size);
void         mem_dbgReset(void *adrs, int size);
void        *mem_dbgGetMem(int size);
void         mem_dbgFreeMem(void *adrs);
void         mem_dbgDrawMemory(HEAP_DRAW_FUNC draw_rect_func,
                               HEAP_DRAW_FUNC draw_line_func,
                               int xx, int yy, int ww, int hh);
unsigned int mem_dbgQueryMaxFreeSize(void);
unsigned int mem_dbgQueryTotalFreeSize(void);

#endif /* _DEBUG_MEM_DBG_H */
