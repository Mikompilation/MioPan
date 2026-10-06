/* ==========================================================================
 *  system/eeiop/spu_mem.h
 *
 *  SPU2 RAM allocator (spu_mem.c).  The 2 MB of sound RAM is described by a
 *  singly-linked list of SPU_BUF records, each "a used run followed by a free
 *  run": `size` bytes in use at `adrs`, then `next_buf_len` bytes free before
 *  the next record starts.  spu_buf[0] is the head and is never released, so
 *  its `size` (0x5010) is the region the SPU2 reserves for itself.
 *
 *  Reconstructed from the Feb 6 2004 prototype (SLES_523.84).
 * ======================================================================== */

#ifndef _SYSTEM_EEIOP_SPU_MEM_H
#define _SYSTEM_EEIOP_SPU_MEM_H

/* Total SPU2 RAM. */
#define SPU_MEMORY_SIZE     0x200000
/* Reverb areas have to end on a 128 KB boundary. */
#define SPU_EFFECT_ALIGN    0x20000

/* The head record is the SPU2's own reserved area, so it is claimed before anything else can ask and never released. */
#define SPU_HEAD_RECORD_SIZE 0x5010

typedef struct _SPU_BUF             /* 0x18 */
{
    /* 0x00 */ int              use;
    /* 0x04 */ int              adrs;
    /* 0x08 */ int              end_adrs;    /* never read by this module */
    /* 0x0c */ int              size;
    /* 0x10 */ int              next_buf_len; /* free run after this block   */
    /* 0x14 */ struct _SPU_BUF *next;
} SPU_BUF;

SPU_BUF *GetNewSPU_BUF(void);
void     ReleaseSPU_BUF(SPU_BUF *wrk);

void     SPUMemoryInit(void);

/* Best fit, rounded up to 64 bytes.  Returns the SPU address, not a host
 * pointer -- callers pass it straight into an IOP command. */
void    *GetSPUMemory(int size);
int      ReleaseSPUMemory(void *adrs);

/* Reverb buffer.  Returns its *end* address minus one, which is what the
 * SPU2's EEA register wants and what ReleaseSPUEffectMemory() matches on. */
void    *GetSPUEffectMemory(int size);
int      ReleaseSPUEffectMemory(void *end_adrs);

void         PrintBufferState(void);
unsigned int SPUQueryMaxFreeSize(void);
unsigned int SPUQueryTotalFreeSize(void);

/* Debug bar chart of the free list.  The colour argument is 64-bit on the EE
 * (the ROM materialises -1 as a full doubleword) but only the low 32 bits are
 * ever meaningful, so it is narrowed here. */
void DrawSPUMemory(void (*draw_rect_func)(int x, int y, int w, int h, unsigned int col),
                   void (*draw_line_func)(int x0, int y0, int x1, int y1, unsigned int col),
                   int xx, int yy, int ww, int hh);

#endif /* _SYSTEM_EEIOP_SPU_MEM_H */
