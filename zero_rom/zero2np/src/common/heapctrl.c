/* ==========================================================================
 *  common/heapctrl.c
 *
 *  Sema-guarded heap controller wrapper around the shared MY_MALLOC block-list
 *  allocator.  Each HEAP_WRK owns one allocator instance and tracks the number
 *  of live allocations handed out through heapCtrlMalloc()/heapCtrlFree().
 *
 *  Reconstructed from the Feb 6 2004 prototype (SLES_523.84).
 * ======================================================================== */
#include "heapctrl.h"
#include "my_malloc.h"
#include <stdint.h>
#include <eekernel.h>

/* --------------------------------------------------------------------------
 *  Reset an existing heap over [adrs, adrs + size).
 * ------------------------------------------------------------------------ */
void heapCtrlReset(HEAP_WRK *wrk, void *adrs, unsigned int size)
{
    WaitSema(wrk->memory_sema);

    my_mallocInit(&wrk->malloc, adrs, size);
    wrk->heap_num = 0;

    SignalSema(wrk->memory_sema);
}

/* --------------------------------------------------------------------------
 *  Create the heap's access semaphore, then lay out the allocator.
 * ------------------------------------------------------------------------ */
void heapCtrlInit(HEAP_WRK *wrk, void *adrs, unsigned int size)
{
    SemaParam semap;

    semap.maxCount = 1;
    semap.initCount = 1;

    wrk->memory_sema = CreateSema(&semap);

    printf("WRK[%x] heapCtrlInit() adrs = %x size = %x\n",
           (unsigned int)(uintptr_t)wrk, (unsigned int)(uintptr_t)adrs, size);

    heapCtrlReset(wrk, adrs, size);
}

/* --------------------------------------------------------------------------
 *  Allocate a 64-byte-aligned block and update the live-block counter.
 * ------------------------------------------------------------------------ */
void *heapCtrlMalloc(HEAP_WRK *wrk, size_t size)
{
    void *ret;

    WaitSema(wrk->memory_sema);

    ret = my_mallocMalloc(&wrk->malloc, (int)size, 6);

    SignalSema(wrk->memory_sema);

    if (ret != (void *)0)
    {
        wrk->heap_num++;
    }

    return ret;
}

/* --------------------------------------------------------------------------
 *  Return a block to the allocator.
 * ------------------------------------------------------------------------ */
void heapCtrlFree(HEAP_WRK *wrk, void *ap)
{
    WaitSema(wrk->memory_sema);

    my_mallocFree(&wrk->malloc, ap);

    SignalSema(wrk->memory_sema);

    wrk->heap_num--;
}

/* --------------------------------------------------------------------------
 *  Query total free bytes.  The prototype accepts a mode but this build's
 *  function body ignores it and always asks the underlying allocator for total
 *  vacant space.
 * ------------------------------------------------------------------------ */
unsigned int heapCtrlMemSize(HEAP_WRK *wrk, HEAP_MEMMODE mode)
{
    unsigned int ret;

    (void)mode;

    WaitSema(wrk->memory_sema);

    ret = my_mallocQueryTotalFreeMem(&wrk->malloc);

    SignalSema(wrk->memory_sema);

    return ret;
}

/* --------------------------------------------------------------------------
 *  Query the largest single free span.
 * ------------------------------------------------------------------------ */
unsigned int heapCtrlQueryMaxOneSize(HEAP_WRK *wrk)
{
    unsigned int ret;

    WaitSema(wrk->memory_sema);

    ret = my_mallocQueryMaxFreeMem(&wrk->malloc);

    SignalSema(wrk->memory_sema);

    return ret;
}

/* --------------------------------------------------------------------------
 *  Allocate only when the caller's destination pointer is empty.
 * ------------------------------------------------------------------------ */
void *SAFE_MALLOC(HEAP_WRK *wrk, void *buf, int size)
{
    if (buf != (void *)0)
    {
        printf("Overlap Malloc!!\n");
        while (1)
        {
        }
    }

    return heapCtrlMalloc(wrk, size);
}

/* --------------------------------------------------------------------------
 *  Draw the heap occupancy through the caller-provided callbacks.
 * ------------------------------------------------------------------------ */
void heapCtrlDrawMemory(HEAP_WRK *wrk, HEAP_DRAW_FUNC draw_rect_func,
                        HEAP_DRAW_FUNC draw_line_func, int xx, int yy,
                        int ww, int hh)
{
    WaitSema(wrk->memory_sema);

    my_mallocDrawMemory(&wrk->malloc, draw_rect_func, draw_line_func,
                        xx, yy, ww, hh);

    SignalSema(wrk->memory_sema);
}
