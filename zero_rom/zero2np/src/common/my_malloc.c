/* ==========================================================================
 *  common/my_malloc.c
 *
 *  Low-level intrusive heap allocator.  MY_MALLOC.wrk is the sentinel node for
 *  a linked list of used blocks; each block's MALLOC_HEADER is stored directly
 *  before the pointer returned to the caller.  vac_size is the free span after
 *  a node and before the next node (or the end of the heap region).
 *
 *  Reconstructed from the Feb 6 2004 prototype (SLES_523.84).
 * ======================================================================== */

#include "my_malloc.h"

#include "utility2.h"
#include "../miopan/miopan_memory.h"

#include <stdint.h>

#define MY_MALLOC_HEADER_SIZE sizeof(MALLOC_HEADER)

static uintptr_t my_mallocGetAlignUp(uintptr_t a, int power)
{
    uintptr_t mask;

    if ((a >> 32) == 0)
    {
        return (uintptr_t)GetAlignUp((unsigned int)a, power);
    }

    mask = ~((uintptr_t)-1 << (power & 0x1f));
    return ((a + mask) >> (power & 0x1f)) << (power & 0x1f);
}

/* --------------------------------------------------------------------------
 *  Reset the allocator over a caller-supplied region.
 * ------------------------------------------------------------------------ */
void my_mallocInit(MY_MALLOC *my_malloc, void *adrs, int size)
{
    (my_malloc->wrk).pre = (MALLOC_HEADER *)0;
    (my_malloc->wrk).next = (MALLOC_HEADER *)0;
    (my_malloc->wrk).vac_size = size;
    (my_malloc->wrk).use_size = 0;
    my_malloc->adrs = MioPan_GetHostPointer((uintptr_t)adrs);

    if (((uintptr_t)adrs & 0xf) != 0)
    {
        printf("my_mallocInit() adrs is not aligned [%d] bit\n", 4);
        while (1)
        {
        }
    }

    my_malloc->size = size;
}

/* --------------------------------------------------------------------------
 *  Allocate a block aligned to 1 << align_bit.
 * ------------------------------------------------------------------------ */
void *my_mallocMalloc(MY_MALLOC *my_malloc, int size, int align_bit)
{
    MALLOC_HEADER *wrk;
    MALLOC_HEADER *new_wrk;
    MALLOC_HEADER *next_wrk;
    uintptr_t      free_adrs;
    void          *ret;
    int            use_size;
    int            align_size;
    int            amari;

    if (align_bit < 4)
    {
        align_bit = 4;
    }

    use_size = size + (int)MY_MALLOC_HEADER_SIZE;
    align_size = ((use_size >> (align_bit & 0x1f)) + 2) << (align_bit & 0x1f);

    for (wrk = &my_malloc->wrk; wrk != (MALLOC_HEADER *)0; wrk = wrk->next)
    {
        if (align_size > wrk->vac_size)
        {
            continue;
        }

        /* The root node owns the region base; every other node's free span
         * starts just past its own used bytes. */
        if (wrk->pre == (MALLOC_HEADER *)0)
        {
            free_adrs = (uintptr_t)my_malloc->adrs;
        }
        else
        {
            free_adrs = (uintptr_t)wrk + (uintptr_t)wrk->use_size;
        }

        ret = (void *)my_mallocGetAlignUp(free_adrs + MY_MALLOC_HEADER_SIZE, align_bit);
        new_wrk = (MALLOC_HEADER *)((uintptr_t)ret - MY_MALLOC_HEADER_SIZE);
        amari = (int)((uintptr_t)new_wrk - free_adrs);

        next_wrk = wrk->next;
        if (next_wrk != (MALLOC_HEADER *)0)
        {
            next_wrk->pre = new_wrk;
        }

        new_wrk->use_size = use_size;
        new_wrk->next     = next_wrk;
        new_wrk->pre      = wrk;
        new_wrk->vac_size = wrk->vac_size - amari - use_size;

        wrk->next     = new_wrk;
        wrk->vac_size = amari;

        return ret;
    }

    printf("my_mallocMalloc() cannot Get Memory size[%07x]\n", use_size);
    return (void *)0;
}

/* --------------------------------------------------------------------------
 *  Free a returned pointer and merge its used+vacant span into the previous
 *  node's vacant span.
 * ------------------------------------------------------------------------ */
void my_mallocFree(MY_MALLOC *my_malloc, void *adrs)
{
    if (adrs == (void *)nullptr)
    {
        printf("my_mallocFree() free adrs is NULL!\n");
        return;
    }

    for (MALLOC_HEADER *wrk = &my_malloc->wrk; wrk != (MALLOC_HEADER *)nullptr; wrk = wrk->next)
    {
        if ((void *)((uintptr_t)wrk + MY_MALLOC_HEADER_SIZE) != adrs)
        {
            continue;
        }

        if (wrk->next != (MALLOC_HEADER *)nullptr)
        {
            wrk->next->pre = wrk->pre;
        }

        /* Only the root has a NULL pre and it is never handed out, so the
         * original merges into pre unconditionally. */
        if (wrk->pre != (MALLOC_HEADER *)nullptr)
        {
            wrk->pre->next = wrk->next;
            wrk->pre->vac_size += wrk->use_size + wrk->vac_size;
        }

        return;
    }

    printf("my_mallocFree() there is no free_able adrs[0x%x]\n", (uintptr_t)adrs);
}

/* --------------------------------------------------------------------------
 *  Sum all vacant spans.
 * ------------------------------------------------------------------------ */
int my_mallocQueryTotalFreeMem(MY_MALLOC *my_malloc)
{
    int free_size = 0;
    MALLOC_HEADER *wrk = &my_malloc->wrk;

    while (wrk != (MALLOC_HEADER *)nullptr)
    {
        free_size += wrk->vac_size;
        wrk = wrk->next;
    }

    return free_size;
}

/* --------------------------------------------------------------------------
 *  Return the largest vacant span.
 * ------------------------------------------------------------------------ */
int my_mallocQueryMaxFreeMem(MY_MALLOC *my_malloc)
{
    int max_free_size = 0;
    MALLOC_HEADER *wrk = &my_malloc->wrk;

    while (wrk != (MALLOC_HEADER *)nullptr)
    {
        if (max_free_size < wrk->vac_size)
        {
            max_free_size = wrk->vac_size;
        }

        wrk = wrk->next;
    }

    return max_free_size;
}

/* --------------------------------------------------------------------------
 *  Draw a border and one occupied-span rectangle per used block.
 * ------------------------------------------------------------------------ */
void my_mallocDrawMemory(MY_MALLOC *my_malloc, HEAP_DRAW_FUNC draw_rect_func,
                         HEAP_DRAW_FUNC draw_line_func, int xx, int yy,
                         int ww, int hh)
{
    int ex = xx + ww + 1;
    int sx = xx - 1;
    int sy = yy - 1;
    int ey = yy + hh + 1;

    draw_line_func(sx, sy, ex, sy, 0xa0a0a0ff);
    draw_line_func(sx, ey, ex, ey, 0xa0a0a0ff);
    draw_line_func(sx, sy, sx, ey, 0xa0a0a0ff);
    draw_line_func(ex, sy, ex, ey, 0xa0a0a0ff);

    MALLOC_HEADER *wrk = &my_malloc->wrk;
    while (wrk != (MALLOC_HEADER *)nullptr)
    {
        if (wrk->use_size != 0)
        {
            int rect_y;
            int rect_h;

            if (my_malloc->size == 0)
            {
                trap(7);
            }

            rect_y = yy + (hh * (int)((uintptr_t)wrk - (uintptr_t)my_malloc->adrs)) /
                          my_malloc->size;
            rect_h = (hh * wrk->use_size) / my_malloc->size;
            draw_rect_func(xx, rect_y, ww, rect_h, 0xffffff20);
        }

        wrk = wrk->next;
    }
}
