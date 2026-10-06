/* ==========================================================================
 *  common/my_malloc.h
 *
 *  Public interface for the low-level intrusive heap allocator used by
 *  heapctrl.c.  The allocator stores one MALLOC_HEADER before every returned
 *  pointer and threads the used blocks through MY_MALLOC.wrk.
 *
 *  Reconstructed from the Feb 6 2004 prototype (SLES_523.84).
 * ======================================================================== */

#ifndef _COMMON_MY_MALLOC_H
#define _COMMON_MY_MALLOC_H

#include "heapctrl.h"

void  my_mallocInit(MY_MALLOC *my_malloc, void *adrs, int size);
void *my_mallocMalloc(MY_MALLOC *my_malloc, int size, int align_bit);
void  my_mallocFree(MY_MALLOC *my_malloc, void *adrs);
int   my_mallocQueryTotalFreeMem(MY_MALLOC *my_malloc);
int   my_mallocQueryMaxFreeMem(MY_MALLOC *my_malloc);
void  my_mallocDrawMemory(MY_MALLOC *my_malloc, HEAP_DRAW_FUNC draw_rect_func,
                          HEAP_DRAW_FUNC draw_line_func, int xx, int yy,
                          int ww, int hh);

#endif /* _COMMON_MY_MALLOC_H */
