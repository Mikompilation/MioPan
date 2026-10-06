/* ==========================================================================
 *  system/iop/iop_ring_buf.c
 *
 *  The ring buffer under every IOP-side loader.  A reader thread pulls
 *  `ring_buf_num` slots of `one_buf_size` bytes off the disc while the caller
 *  drains them to the EE; the two counting semaphores are the whole
 *  synchronisation.  `ring_buf_sema` counts free slots and is created full,
 *  `read_dat_sema` counts filled slots and is created empty, so the producer
 *  blocks when it laps the consumer and the consumer blocks when it catches up.
 *
 *  The reader thread takes no argument.  InitRingBufSub() smuggles the
 *  RING_BUF_WRK pointer through ThreadParam.option and thRingBufRead() reads
 *  it back out of its own ThreadInfo -- which is why that function has an
 *  otherwise unexplained ThreadInfo local.
 *
 *  Reconstructed from iopsys.irx (Feb 6 2004 prototype, SLES_523.84).
 *  No source line numbers are available for this module, so there are no
 *  trailing ROM-line annotations.
 * ======================================================================== */

#include "iop_ring_buf.h"

#include <stdio.h>                  /* printf                          */
#include <thbase.h>                 /* CreateThread / DelayThread / ... */
#include <thsemap.h>                /* CreateSema / WaitSema / ...      */
#include <intrman.h>                /* CpuSuspendIntr / CpuResumeIntr   */
#include <sysmem.h>                 /* AllocSysMemory / FreeSysMemory   */

#include "iop.h"                    /* iopReqRead, MyTransEEWait        */
#include "utility2i.h"              /* RingBufAdd, GetAlignUp           */

/* 0x02000000 is TH_C -- an ordinary C thread. */
#define RING_BUF_TH_ATTR    0x02000000
#define RING_BUF_TH_STACK   0x500

/* Everything the reader does is in whole 2048-byte sectors. */
#define SECTOR_SHIFT        11

int InitRingBufSub(RING_BUF_WRK *wrk, int priority)
{
    SemaParam        spara;
    ThreadParam      param;
    int              oldstat;
    int              th;
    LOAD_DEF_STRUCT *pld = &wrk->ld;

    param.attr         = RING_BUF_TH_ATTR;
    /* The reader has no parameter of its own, so the work area travels in
     * `option` and comes back out through ReferThreadStatus(). */
    param.option       = (uintptr_t)wrk;
    param.entry        = (void *)thRingBufRead;
    param.stackSize    = RING_BUF_TH_STACK;
    param.initPriority = priority;

    while ((th = CreateThread(&param)) < 1)
    {
        printf("LoadReqEE() Cannot Create Thread\n");
        DelayThread(500000);
    }

    /* Interrupts are held off across the allocation because the IOP heap is
     * shared with interrupt-time callers. */
    for (;;)
    {
        CpuSuspendIntr(&oldstat);
        wrk->ring_buf_top = (uintptr_t)AllocSysMemory(0, pld->ring_buf_num * pld->one_buf_size, 0);
        CpuResumeIntr(oldstat);

        if (wrk->ring_buf_top != 0)
            break;

        printf("iopCommandLoad() Wait Memory Released!\n");
        DelayThread(500000);
    }

    spara.attr      = 1;
    spara.option    = 0;

    /* Filled slots: none yet. */
    spara.initCount = 0;
    spara.maxCount  = pld->ring_buf_num;
    while ((wrk->read_dat_sema = CreateSema(&spara)) < 0)
    {
        printf("iopCommandLoadStm() Wait Memory Released Sema!\n");
        DelayThread(500000);
    }

    /* Free slots: all of them. */
    spara.initCount = pld->ring_buf_num;
    while ((wrk->ring_buf_sema = CreateSema(&spara)) < 0)
    {
        printf("iopCommandLoadStm() Wait Memory Released Sema!\n");
        DelayThread(500000);
    }

    wrk->trans_idx    = 0;
    wrk->trans_offset = 0;
    wrk->load_end_flg = 0;
    wrk->now_adrs     = wrk->ring_buf_top;

    return th;
}

/* load_end_flg is the handshake: 1 asks the reader to stop, and the reader
 * answers 2 on its way out.  The semaphore is signalled each time round in
 * case the reader is parked waiting for a free slot that will never come. */
void ReleaseRingBufSubWait(RING_BUF_WRK *wrk, int read_th_idx)
{
    if (wrk->load_end_flg == 0)
        wrk->load_end_flg = 1;

    while (wrk->load_end_flg != 2)
    {
        SignalSema(wrk->ring_buf_sema);
        DelayThread(100000);
        printf("=========Wait Read End========\n");
    }

    ReleaseRingBufSub(wrk, read_th_idx);
}

void ReleaseRingBufSub(RING_BUF_WRK *wrk, int read_th_idx)
{
    int oldstat;

    CpuSuspendIntr(&oldstat);
    FreeSysMemory((void *)wrk->ring_buf_top);
    CpuResumeIntr(oldstat);

    DeleteSema(wrk->ring_buf_sema);
    DeleteSema(wrk->read_dat_sema);

    TerminateThread(read_th_idx);
    DeleteThread(read_th_idx);
}

int RingBufTransEE(RING_BUF_WRK *wrk, int read_byte, uintptr_t ee_buf)
{
    int              size;
    int              remain_byte;
    int              align_size;
    int              ret;
    LOAD_DEF_STRUCT *pld = &wrk->ld;
    int              end_adrs;
    int              next_iop_buf_end;

    if (read_byte == 0)
        return 0;

    align_size = read_byte;

    /* Less than a full request left in the file: take what remains, rounded
     * up to a quadword so the SIF transfer stays aligned. */
    if (pld->size - wrk->trans_offset <= read_byte)
        align_size = GetAlignUp(pld->size - wrk->trans_offset, 4);

    ret         = align_size;
    remain_byte = align_size;

    do
    {
        /* Landing exactly on a slot boundary means the next slot has to have
         * been filled before it can be read. */
        if ((wrk->now_adrs - wrk->ring_buf_top) % pld->one_buf_size == 0)
            WaitSema(wrk->read_dat_sema);

        end_adrs         = wrk->now_adrs;
        next_iop_buf_end = wrk->ring_buf_top +
                           (wrk->trans_idx + 1) * pld->one_buf_size;

        /* The whole remainder fits inside the current slot. */
        if (end_adrs + remain_byte < next_iop_buf_end)
        {
            MyTransEEWait(end_adrs, ee_buf, remain_byte);
            wrk->now_adrs += remain_byte;
            break;
        }

        /* Otherwise take the rest of this slot, hand it back to the reader
         * and carry on in the next one. */
        size = next_iop_buf_end - end_adrs;
        MyTransEEWait(end_adrs, ee_buf, size);

        remain_byte   -= size;
        wrk->trans_idx = RingBufAdd(wrk->trans_idx, pld->ring_buf_num);
        SignalSema(wrk->ring_buf_sema);

        ee_buf       += size;
        wrk->now_adrs = wrk->ring_buf_top + wrk->trans_idx * pld->one_buf_size;
    } while (remain_byte != 0);

    wrk->trans_offset += ret;

    return ret;
}

void thRingBufRead(void)
{
    unsigned int     iop_buf;
    int              size;
    int              offset;
    int              end_flg;
    int              read_idx;
    ThreadInfo       info;
    RING_BUF_WRK    *wrk;
    LOAD_DEF_STRUCT *pld;

    offset   = 0;
    end_flg  = 0;
    read_idx = 0;

    /* Thread id 0 means "this thread". */
    ReferThreadStatus(0, &info);
    wrk = (RING_BUF_WRK *)info.option;
    pld = &wrk->ld;

    do
    {
        WaitSema(wrk->ring_buf_sema);

        size = pld->one_buf_size;
        /* Computed from the untruncated slot size, so the tail read still
         * lands at the right slot. */
        iop_buf = wrk->ring_buf_top + read_idx * size;

        if (pld->size - offset <= size)
        {
            end_flg = 1;
            size    = GetAlignUp(pld->size - offset, SECTOR_SHIFT);
        }

        /* Checked after the wait, so a release that arrives while the reader
         * is parked is still seen. */
        if (wrk->load_end_flg == 1)
            break;

        iopReqRead(pld->start_sector + (offset >> SECTOR_SHIFT),
                   size >> SECTOR_SHIFT, (void *)iop_buf, 0);
        offset += size;

        read_idx = RingBufAdd(read_idx, pld->ring_buf_num);
        SignalSema(wrk->read_dat_sema);
    } while (!end_flg);

    wrk->load_end_flg = 2;

    ExitThread();
}
