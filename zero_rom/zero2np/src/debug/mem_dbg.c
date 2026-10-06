// FILE: /home/zero_rom/zero2np/src/debug/mem_dbg.c
//
// Project debug heap: a single HEAP_WRK (mem_dbg_heap_wrk) laid out at boot by
// mem_dbgInit().  The g3d allocator shim (graphics/graph3d/g3dMemory.c) and
// other debug-build consumers route their allocations here so block sizes are
// tracked for leak / overrun reporting.  Every entry point is a thin wrapper
// over the shared heap controller (heapctrl.c) - the same shape as the
// general-purpose scratch heap in common/mem_util.c, over its own region.
//
//   * mem_dbgInit / mem_dbgReset      - lay out / re-lay the region.
//   * mem_dbgGetMem / mem_dbgFreeMem  - allocate (SAFE_MALLOC) / free a
//                                       buffer; free is NULL-safe.
//   * mem_dbgDrawMemory               - visualise occupancy via callbacks.
//   * mem_dbgQueryMaxFreeSize / ...TotalFreeSize - largest single free block /
//                                       total free bytes.
//
// Reconstructed from the Feb 6 2004 prototype (SLES_523.84).

#include "mem_dbg.h"                // this file's public API

#include "heapctrl.h"  // HEAP_WRK / heapCtrl* / SAFE_MALLOC / HEAPMEM_LEAVE_SIZE

// ──────────────────────────────────────────────────────────────────────
// The one debug heap.

static HEAP_WRK mem_dbg_heap_wrk;   // bss 4b5350

// ──────────────────────────────────────────────────────────────────────
// Lay the heap out over [adrs, adrs+size).

void mem_dbgInit(void *adrs, int size)
{
    heapCtrlInit(&mem_dbg_heap_wrk, adrs, size);
}

// ──────────────────────────────────────────────────────────────────────
// Re-lay the heap over a new region (drops every outstanding allocation).

void mem_dbgReset(void *adrs, int size)
{
    heapCtrlReset(&mem_dbg_heap_wrk, adrs, size);
}

// ──────────────────────────────────────────────────────────────────────
// Allocate a buffer of `size` bytes from the debug heap.

void *mem_dbgGetMem(int size)
{
    return SAFE_MALLOC(&mem_dbg_heap_wrk, (void *)0, size);
}

// ──────────────────────────────────────────────────────────────────────
// Return a buffer to the debug heap (NULL-safe).

void mem_dbgFreeMem(void *adrs)
{
    if (adrs != (void *)0)
    {
        heapCtrlFree(&mem_dbg_heap_wrk, adrs);
    }
}

// ──────────────────────────────────────────────────────────────────────
// Draw the heap's occupancy map through the caller's rect / line callbacks.

void mem_dbgDrawMemory(HEAP_DRAW_FUNC draw_rect_func,
                       HEAP_DRAW_FUNC draw_line_func,
                       int xx, int yy, int ww, int hh)
{
    heapCtrlDrawMemory(&mem_dbg_heap_wrk, draw_rect_func, draw_line_func,
                       xx, yy, ww, hh);
}

// ──────────────────────────────────────────────────────────────────────
// Largest single allocation the heap can still satisfy.

unsigned int mem_dbgQueryMaxFreeSize(void)
{
    return heapCtrlQueryMaxOneSize(&mem_dbg_heap_wrk);
}

// ──────────────────────────────────────────────────────────────────────
// Total free bytes remaining in the heap.

unsigned int mem_dbgQueryTotalFreeSize(void)
{
    return heapCtrlMemSize(&mem_dbg_heap_wrk, HEAPMEM_LEAVE_SIZE);
}
