/* ==========================================================================
 *  system/eeiop/spu_mem.c
 *
 *  SPU2 RAM allocator.  50 SPU_BUF records describe the 2 MB of sound RAM as
 *  a chain of (used run, free run) pairs; allocating splits a free run and
 *  releasing folds it back into the previous record.  spu_buf[0] is the head
 *  and holds the SPU2's own reserved 0x5010 bytes, so the list is never
 *  empty and no allocation ever lands at address 0.
 *
 *  Everything here deals in SPU addresses, not host pointers: the values
 *  travel to the IOP as integers and are only ever compared, never
 *  dereferenced.
 *
 *  Reconstructed from the Feb 6 2004 prototype (SLES_523.84).
 * ======================================================================== */

#include <stdint.h>
#include <stdio.h>

#include "spu_mem.h"

#include "../../common/utility2.h"  /* GetAlignUp */
#include "../../sdk/eekernel.h"     /* CreateSema / WaitSema / SignalSema */

#define SPU_BUF_MAX 50
static SPU_BUF spu_buf[SPU_BUF_MAX];                                         /* bss 4bd9a8 */
static int     spu_mem_sema;                                                 /* sbss 3f5014 */

SPU_BUF *GetNewSPU_BUF(void)
{
    for (int i = 0; i < SPU_BUF_MAX; i++)                                    /* 34 */
    {
        if (spu_buf[i].use == 0)                                             /* 35 */
        {
            spu_buf[i].use = 1;
            return &spu_buf[i];                                              /* 36 */
        }
    }

    return nullptr;                                                     /* 42 */
}

void ReleaseSPU_BUF(SPU_BUF *wrk)
{
    wrk->use = false;                                                            /* 45 */
}

/* 49 */
void SPUMemoryInit(void)
{
    SemaParam semap = {0};

    for (int i = 0; i < SPU_BUF_MAX; i++) {                                  /* 53 */
        spu_buf[i].use = false;                                              /* 54 */
    }

    semap.maxCount  = 1;                                                     /* 60 */
    semap.initCount = 1;                                                     /* 61 */
    spu_mem_sema    = CreateSema(&semap);                                    /* 62 */

    printf("SPUMemoryInit\n");                                               /* 66 */
    SPU_BUF *first = GetNewSPU_BUF();                                        /* 67 */
    first->adrs         = 0;                                                 /* 68 */
    first->size         = SPU_HEAD_RECORD_SIZE;                              /* 69 */
    first->next_buf_len = SPU_MEMORY_SIZE - SPU_HEAD_RECORD_SIZE;            /* 70 */
    first->next         = nullptr;                                           /* 72 */
}                                                                            /* 73 */

/* 77 */
void *GetSPUMemory(int size)
{
    void    *ret;
    SPU_BUF *wrk;
    SPU_BUF *best_wrk;
    int      best_remain;
    int      remain;
    SPU_BUF *new_wrk;

    best_wrk    = nullptr;                                                   /* 80 */
    best_remain = 0;

    WaitSema(spu_mem_sema);                                                  /* 84 */

    wrk  = spu_buf;                                                          /* 86 */
    size = GetAlignUp(size, 6);                                              /* 88 */

    /* Best fit: the free run that leaves the least behind. */
    while (wrk != nullptr)                                                   /* 91 */
    {
        remain = wrk->next_buf_len - size;                                   /* 93 */

        if (remain >= 0)                                                     /* 94 */
        {
            if (best_wrk == nullptr || remain < best_remain)                 /* 97 */
            {
                best_wrk    = wrk;                                           /* 100 */
                best_remain = remain;                                        /* 101 */
            }
        }

        wrk = wrk->next;                                                     /* 106 */
    }                                                                        /* 107 */

    ret = nullptr;

    if (best_wrk != nullptr)                                                 /* 112 */
    {
        new_wrk = GetNewSPU_BUF();                                           /* 113 */

        /* ROM BUG, reproduced: this path returns without SignalSema(), so
         * running out of SPU_BUF records wedges every later allocation.  In
         * practice 50 records was always enough. */
        if (new_wrk == nullptr)                                              /* 116 */
        {
            printf("There is no Frea SPU_BUFFER_WRK!\n");                    /* 117 */
            return nullptr;                                                  /* 118 */
        }

        new_wrk->size         = size;
        new_wrk->adrs         = best_wrk->adrs + best_wrk->size;             /* 123 */
        new_wrk->next_buf_len = best_wrk->next_buf_len - size;               /* 124 */
        new_wrk->next         = best_wrk->next;                              /* 126 */

        best_wrk->next         = new_wrk;                                    /* 128 */
        best_wrk->next_buf_len = 0;                                          /* 129 */

        ret = (void *)(uintptr_t)new_wrk->adrs;                              /* 135 */
    }

    SignalSema(spu_mem_sema);                                                /* 138 */

    return ret;                                                              /* 139 */
}                                                                            /* 140 */

/* 144 */
static int ReleaseSPUMemorySub(void *adrs, int (*cond_func)(SPU_BUF *wrk, void *adrs))
{
    SPU_BUF *wrk;
    SPU_BUF *pre;
    int      ret;

    ret = 0;                                                                 /* 147 */

    WaitSema(spu_mem_sema);                                                  /* 149 */

    pre = spu_buf;                                                           /* 156 */
    wrk = pre->next;                                                         /* 158 */

    while (wrk != nullptr)                                              /* 160 */
    {
        if (cond_func(wrk, adrs))                                            /* 165 */
        {
            pre->next          = wrk->next;                                  /* 167 */
            pre->next_buf_len += wrk->size + wrk->next_buf_len;              /* 168 */

            ReleaseSPU_BUF(wrk);                                             /* 171 */

            ret = 1;                                                         /* 174 */
        }

        /* NOTE: `pre` advances onto `wrk` even when `wrk` was just released,
         * so a second match in the same walk would splice through a free
         * record.  Only one block ever matches either predicate, which is
         * why it never showed. */
        pre = wrk;
        wrk = wrk->next;                                                     /* 176 */
    }

    if (ret == 0)                                                            /* 178 */
        printf("Adrs To Be Released Is Not Found!!\n");                      /* 179 */

    SignalSema(spu_mem_sema);                                                /* 181 */

    return ret;                                                              /* 182 */
}

static int NormalMemoryCond(SPU_BUF *wrk, void *adrs)
{
    return (void *)(uintptr_t)wrk->adrs == adrs;                                        /* 189 */
}

/* 197 */
int ReleaseSPUMemory(void *adrs)
{
    return ReleaseSPUMemorySub(adrs, NormalMemoryCond);                      /* 198 */
}

/* 218 */
void *GetSPUEffectMemory(int size)
{
    void    *ret;
    SPU_BUF *wrk;
    int      effect_align_size;
    int      end_adrs;
    int      next_adrs;
    int      next_align;
    int      remain_size;
    int      final_end_adrs;
    SPU_BUF *best_wrk;
    SPU_BUF *new_wrk;

    remain_size       = 0;                                                   /* 223 */
    final_end_adrs    = 0;                                                   /* 224 */
    best_wrk          = nullptr;                                        /* 225 */
    effect_align_size = SPU_EFFECT_ALIGN;

    WaitSema(spu_mem_sema);                                                  /* 227 */

    wrk = spu_buf;                                                           /* 229 */

    /* Last fit rather than best fit: every suitable block overwrites the
     * previous candidate, so the reverb area ends up as high in SPU RAM as
     * the free list allows. */
    while (wrk != nullptr)                                              /* 231 */
    {
        end_adrs   = wrk->adrs + wrk->size;                                  /* 232 */
        next_adrs  = end_adrs + wrk->next_buf_len;                           /* 233 */
        next_align = GetAlignUp(end_adrs, 0x11);                             /* 234 */

        if (size < next_adrs - end_adrs && next_align <= next_adrs)          /* 238 */
        {
            /* The reverb area has to *end* on a 128 KB boundary, so walk the
             * alignment past the free run and step back one boundary. */
            do                                                               /* 244 */
            {
                next_align = GetAlignUp(next_align + 1, 0x11);
            } while (next_align <= next_adrs);                               /* 246 */

            final_end_adrs = next_align - effect_align_size;                 /* 248 */
            remain_size    = next_adrs - final_end_adrs;                     /* 254 */
            best_wrk       = wrk;                                            /* 255 */
        }

        wrk = wrk->next;                                                     /* 258 */
    }                                                                        /* 259 */

    if (final_end_adrs != 0)                                                 /* 262 */
    {
        new_wrk = GetNewSPU_BUF();                                           /* 263 */

        new_wrk->adrs         = final_end_adrs - size;                       /* 266 */
        new_wrk->size         = size;                                        /* 267 */
        new_wrk->next_buf_len = remain_size;                                 /* 268 */
        new_wrk->next         = best_wrk->next;                              /* 269 */

        best_wrk->next          = new_wrk;                                   /* 271 */
        best_wrk->next_buf_len -= size + remain_size;                        /* 272 */

        ret = (void *)(uintptr_t)(final_end_adrs - 1);                                  /* 275 */
        printf("effect finaladrs = %p size = %x\n", ret, size);
    }
    else
    {
        ret = nullptr;
    }

    SignalSema(spu_mem_sema);                                                /* 282 */

    return ret;                                                              /* 284 */
}

/* Matches on the end address, which is what GetSPUEffectMemory() handed out. */
static int EffectMemoryCond(SPU_BUF *wrk, void *adrs)
{
    printf("adrs = %x, wrk_adrs = %x\n", adrs, wrk->adrs + wrk->size - 1);   /* 291 */
    return (void *)(uintptr_t)(wrk->adrs + wrk->size - 1) == adrs;           /* 292 */
}

/* 300 */
int ReleaseSPUEffectMemory(void *end_adrs)
{
    return ReleaseSPUMemorySub(end_adrs, EffectMemoryCond);                  /* 302 */
}

/* 308 */
void PrintBufferState(void)
{
    SPU_BUF *wrk;

    printf("\n\nSPU_BufferState (******* > use, +++++++ > vacant)\n\n");     /* 310 */

    wrk = spu_buf;                                                           /* 312 */
    while (wrk != nullptr)                                                   /* 313 */
    {
        printf("*******0x%06x********\n", wrk->size);                        /* 314 */

        if (wrk->next_buf_len != 0)                                          /* 315 */
            printf("+++++++0x%06x++++++++\n", wrk->next_buf_len);            /* 316 */

        wrk = wrk->next;                                                     /* 318 */
    }                                                                        /* 319 */

    printf("\n\n");
}                                                                            /* 320 */

/* 326 */
unsigned int SPUQueryMaxFreeSize(void)
{
    SPU_BUF *wrk;
    int      ret;

    ret = 0;                                                                 /* 330 */

    WaitSema(spu_mem_sema);                                                  /* 332 */

    wrk = spu_buf;                                                           /* 334 */
    do                                                                       /* 335 */
    {
        if (ret < wrk->next_buf_len)                                         /* 338 */
            ret = wrk->next_buf_len;

        wrk = wrk->next;                                                     /* 339 */
    } while (wrk != nullptr);                                           /* 340 */

    SignalSema(spu_mem_sema);

    return ret;                                                              /* 342 */
}

/* 346 */
unsigned int SPUQueryTotalFreeSize(void)
{
    SPU_BUF *wrk;
    int      ret;

    ret = 0;                                                                 /* 350 */

    WaitSema(spu_mem_sema);                                                  /* 352 */

    wrk = spu_buf;                                                           /* 354 */
    do                                                                       /* 355 */
    {
        ret += wrk->next_buf_len;                                            /* 356 */
        wrk  = wrk->next;                                                    /* 357 */
    } while (wrk != nullptr);

    SignalSema(spu_mem_sema);                                                /* 359 */

    return ret;                                                              /* 360 */
}

/* 367 */
void DrawSPUMemory(void (*draw_rect_func)(int x, int y, int w, int h, unsigned int col),
                   void (*draw_line_func)(int x0, int y0, int x1, int y1, unsigned int col),
                   int xx, int yy, int ww, int hh)
{
    SPU_BUF *wrk;
    int      now_y;
    int      gage;

    WaitSema(spu_mem_sema);                                                  /* 373 */

    wrk   = spu_buf;                                                         /* 375 */
    now_y = yy;

    while (wrk != nullptr)                                              /* 377 */
    {
        gage = hh * wrk->size / SPU_MEMORY_SIZE;                             /* 378 */

        draw_line_func(xx, now_y, xx + ww, now_y, 0xffffffff);               /* 379 */
        draw_rect_func(xx, now_y + 1, ww, gage, 0x502020ff);                 /* 380 */
        now_y += gage;                                                       /* 381 */

        if (wrk->next_buf_len != 0)                                          /* 383 */
        {
            gage = hh * wrk->next_buf_len / SPU_MEMORY_SIZE;                 /* 384 */

            draw_line_func(xx, now_y, xx + ww, now_y, 0xffffffff);           /* 385 */
            now_y += gage;                                                   /* 387 */
        }

        wrk = wrk->next;                                                     /* 389 */
    }                                                                        /* 390 */

    draw_line_func(xx, now_y, xx + ww, now_y, 0xffffffff);                   /* 391 */
    draw_line_func(xx, yy, xx, now_y, 0xffffffff);                           /* 394 */
    draw_line_func(xx + ww, yy, xx + ww, now_y, 0xffffffff);                 /* 395 */

    SignalSema(spu_mem_sema);                                                /* 397 */
}
