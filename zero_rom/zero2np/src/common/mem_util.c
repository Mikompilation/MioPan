// FILE: /home/zero_rom/zero2np/src/common/mem_util.c
//
// General-purpose scratch heap: a single HEAP_WRK (mem_util_heap_wrk) carved
// out of main memory by mem_utilInit() at boot.  Transient buffers - screen
// snapshots, UI textures, temporary decode space - are allocated here so the
// churn stays off the long-lived system heap.  Every entry point is a thin
// wrapper over the shared heap controller (heapctrl.c).
//
//   * mem_utilInit / mem_utilReset      - lay out / re-lay the region.
//   * mem_utilGetMem / mem_utilFreeMem  - allocate (SAFE_MALLOC) / free a
//                                         buffer; free is NULL-safe.
//   * mem_utiDrawMemory                 - visualise occupancy via callbacks.
//   * mem_utilQueryMaxFreeSize / ...TotalFreeSize - largest single free block /
//                                         total free bytes.
//
// Reconstructed from the Feb 6 2004 prototype (SLES_523.84).

#include "mem_util.h"               // this file's public API

#include "heapctrl.h"  // HEAP_WRK / heapCtrl* / SAFE_MALLOC / HEAPMEM_LEAVE_SIZE

// ──────────────────────────────────────────────────────────────────────
// The one scratch heap.

static HEAP_WRK mem_util_heap_wrk;  // bss 4b5370

// ──────────────────────────────────────────────────────────────────────
// Lay the heap out over [adrs, adrs+size).

void mem_utilInit(void *adrs, int size)
{
    heapCtrlInit(&mem_util_heap_wrk, adrs, size);
}

// ──────────────────────────────────────────────────────────────────────
// Re-lay the heap over a new region (drops every outstanding allocation).

void mem_utilReset(void *adrs, int size)
{
    heapCtrlReset(&mem_util_heap_wrk, adrs, size);
}

// ──────────────────────────────────────────────────────────────────────
// Allocate a buffer of `size` bytes from the scratch heap.

void *mem_utilGetMem(int size)
{
    return SAFE_MALLOC(&mem_util_heap_wrk, (void *)0, size);
}

// ──────────────────────────────────────────────────────────────────────
// Return a buffer to the scratch heap (NULL-safe).

void mem_utilFreeMem(void *adrs)
{
    if (adrs != (void *)0)
    {
        heapCtrlFree(&mem_util_heap_wrk, adrs);
    }
}

// ──────────────────────────────────────────────────────────────────────
// Draw the heap's occupancy map through the caller's rect / line callbacks.

void mem_utiDrawMemory(HEAP_DRAW_FUNC draw_rect_func,
                       HEAP_DRAW_FUNC draw_line_func,
                       int xx, int yy, int ww, int hh)
{
    heapCtrlDrawMemory(&mem_util_heap_wrk, draw_rect_func, draw_line_func,
                       xx, yy, ww, hh);
}

// ──────────────────────────────────────────────────────────────────────
// Largest single allocation the heap can still satisfy.

unsigned int mem_utilQueryMaxFreeSize(void)
{
    return heapCtrlQueryMaxOneSize(&mem_util_heap_wrk);
}

// ──────────────────────────────────────────────────────────────────────
// Total free bytes remaining in the heap.

unsigned int mem_utilQueryTotalFreeSize(void)
{
    return heapCtrlMemSize(&mem_util_heap_wrk, HEAPMEM_LEAVE_SIZE);
}
