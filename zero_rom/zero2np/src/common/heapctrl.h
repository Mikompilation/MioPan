/* ==========================================================================
 *  system/os/heapctrl.h
 *
 *  Public interface for the engine's general-purpose heap controller
 *  (heapctrl.c).  A HEAP_WRK owns a doubly-linked free/used block list
 *  (MALLOC_HEADER) over a caller-supplied memory region; heapCtrlInit() lays
 *  the region out, heapCtrlMalloc()/heapCtrlFree() (and the SAFE_MALLOC
 *  wrapper) hand out and return blocks, and the query/draw helpers report and
 *  visualise occupancy.  The system heap, the outgame load heap (ol_load.c) and
 *  several subsystem heaps are all HEAP_WRK instances.
 *
 *  Types verbatim from the prototype's debug info (types.txt).
 *
 *  Reconstructed from the Feb 6 2004 prototype (SLES_523.84).
 * ======================================================================== */

#ifndef _SYSTEM_OS_HEAPCTRL_H
#define _SYSTEM_OS_HEAPCTRL_H

#include <stddef.h>

/* --------------------------------------------------------------------------
 *  Heap block bookkeeping.
 * ------------------------------------------------------------------------ */
/* pre/next are real pointers, as in the prototype's debug info.  This header
 * is written into the managed region itself (never read from a file), so it is
 * safe to let it grow to 24 bytes on a 64-bit host -- my_malloc.c derives every
 * offset from sizeof(MALLOC_HEADER).  Encoding them as 32-bit "links" instead
 * cannot work here: a host pointer does not fit, and the sentinel values such
 * a scheme needs collide with real offsets and turn the list walk into a cycle.
 *
 * The root node is the one with pre == NULL (see my_mallocInit); a NULL next
 * terminates the list. */
typedef struct _MALLOC_HEADER       /* 0x10 on PS2 */
{
    /* 0x0 */ struct _MALLOC_HEADER *pre;      /* previous block, NULL at root  */
    /* 0x4 */ struct _MALLOC_HEADER *next;     /* next block, NULL at list end  */
    /* 0x8 */ int use_size;                    /* bytes in use                 */
    /* 0xc */ int vac_size;                    /* free bytes trailing the block */
} MALLOC_HEADER;

typedef struct                      /* 0x18 */
{
    /* 0x00 */ MALLOC_HEADER wrk;               /* the region's block list head */
    /* 0x10 */ void         *adrs;              /* region base                  */
    /* 0x14 */ int           size;              /* region byte size             */
} MY_MALLOC;

typedef struct _HEAP_WRK            /* 0x20 */
{
    /* 0x00 */ MY_MALLOC malloc;                /* block list + region          */
    /* 0x18 */ int       heap_num;              /* live allocation count        */
    /* 0x1c */ int       memory_sema;           /* per-heap access sema         */
} HEAP_WRK;

typedef void (*HEAP_DRAW_FUNC)(int, int, int, int, int);

/* --------------------------------------------------------------------------
 *  heapCtrlMemSize() report mode.
 * ------------------------------------------------------------------------ */
enum HEAP_MEMMODE
{
    HEAPMEM_USED_SIZE  = 0,
    HEAPMEM_LEAVE_SIZE = 1
};

/* --------------------------------------------------------------------------
 *  Heap API (heapctrl.c).
 * ------------------------------------------------------------------------ */
void         heapCtrlInit(HEAP_WRK *wrk, void *adrs, unsigned int size);
void         heapCtrlReset(HEAP_WRK *wrk, void *adrs, unsigned int size);
void        *heapCtrlMalloc(HEAP_WRK *wrk, size_t size);
void         heapCtrlFree(HEAP_WRK *wrk, void *ap);
unsigned int heapCtrlMemSize(HEAP_WRK *wrk, HEAP_MEMMODE mode);
unsigned int heapCtrlQueryMaxOneSize(HEAP_WRK *wrk);
void         heapCtrlDrawMemory(HEAP_WRK *wrk, HEAP_DRAW_FUNC draw_rect_func,
                                HEAP_DRAW_FUNC draw_line_func, int xx, int yy,
                                int ww, int hh);

/* Allocate `size` bytes from `wrk`; `buf` selects a reuse hint (NULL = fresh). */
void        *SAFE_MALLOC(HEAP_WRK *wrk, void *buf, int size);

#endif /* _SYSTEM_OS_HEAPCTRL_H */
