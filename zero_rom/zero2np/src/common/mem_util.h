/* ==========================================================================
 *  common/mem_util.h
 *
 *  Public interface for the general-purpose scratch heap (mem_util.c): a single
 *  HEAP_WRK the engine allocates transient buffers from (screen snapshots, UI
 *  textures, ...).  mem_utilInit() carves the region out at boot; the get/free
 *  pair hand blocks out and back; the query/draw helpers report occupancy.
 *
 *  Reconstructed from the Feb 6 2004 prototype (SLES_523.84).
 * ======================================================================== */

#ifndef _COMMON_MEM_UTIL_H
#define _COMMON_MEM_UTIL_H

#include "my_malloc.h"

void         mem_utilInit(void *adrs, int size);
void         mem_utilReset(void *adrs, int size);
void        *mem_utilGetMem(int size);
void         mem_utilFreeMem(void *adrs);
void         mem_utiDrawMemory(HEAP_DRAW_FUNC draw_rect_func,
                               HEAP_DRAW_FUNC draw_line_func,
                               int xx, int yy, int ww, int hh);
unsigned int mem_utilQueryMaxFreeSize(void);
unsigned int mem_utilQueryTotalFreeSize(void);

#endif /* _COMMON_MEM_UTIL_H */
