/* ==========================================================================
 *  system/iop/iop_pcmstream.c
 *
 *  Straight PCM streaming.  Two slots, each owning a reader thread and a
 *  128 KB ring (0x80 slots of 0x400 bytes) in IOP RAM.  The SPU2's streaming
 *  DMA -- sceSdBlockTrans() -- plays straight out of that ring, so no voice
 *  is ever allocated and the only per-frame work is the volume ramp.
 *
 *  The pacing is entirely interrupt-driven: sceSdSetTransIntrHandler() points
 *  the block-transfer completion at _intr_SignalSema(), which signals
 *  `block_trans_sema`, and the reader thread's loop ends on a WaitSema() for
 *  it.  One refill per DMA interrupt, and the ring can never overrun the
 *  hardware.
 *
 *  Pause is a four-state handshake rather than a flag, because the volume has
 *  to be ramped to zero *before* the DMA is parked on silence and ramped back
 *  up after it restarts -- see PCMStreamMain() and the `pause_phase` note in
 *  the header.
 *
 *  Reconstructed from iopsys.irx (Feb 6 2004 prototype, SLES_523.84).
 *  No source line numbers are available for this module, so there are no
 *  trailing ROM-line annotations.
 * ======================================================================== */

#include "iop_pcmstream.h"

#include <stdio.h>                  /* printf                          */
#include <intrman.h>                /* CpuSuspendIntr / CpuResumeIntr  */
#include <libsd.h>                  /* sceSd*                          */
#include <sysclib.h>                /* memset / strcpy                 */
#include <sysmem.h>                 /* AllocSysMemory / FreeSysMemory  */
#include <thbase.h>                 /* threads                         */
#include <thsemap.h>                /* semaphores                      */

#include "iop.h"                    /* iopReqRead, WaitSpuTransSema, ... */
#include "utility2i.h"              /* GetAlignUp, RingBufAdd            */

/* 0x02000000 is TH_C. */
#define PCM_TH_ATTR         0x02000000
#define PCM_TH_STACK        0x800

#define PCM_STREAM_MAX      2
#define PCM_RB_NUM          0x80    /* ring slots                       */
#define PCM_RB_SIZE         0x400   /* bytes per slot                   */

/* Block-transfer volume registers; the low bit of the id selects the core. */
#define SD_P_BVOLL          0x0f80
#define SD_P_BVOLR          0x1080

/* sceSdBlockTrans modes. */
#define SD_BLOCK_TRANS_STAT  0x02   /* query the play position          */
#define SD_BLOCK_TRANS_LOOP  0x10   /* start, looping over the buffer   */
#define SD_BLOCK_TRANS_CONT  0x13   /* resume from a given address      */
/* libsd takes five arguments; `start_addr` is only read for CONT, and the
 * ROM leaves it unset at the other sites.  Passed as 0 here so the call is
 * well-formed C -- the callee ignores it in those modes. */

/* PollSema()'s "already zero" result. */
#define KE_SEMA_ZERO        (-419)

/* Ramp steps, and the thresholds below which the target is taken directly.
 * Down is nearly three times faster than up. */
#define PCM_VOL_STEP_UP     300
#define PCM_VOL_STEP_DOWN   800

PCM_STREAM_WRK pcm_stream_wrk[PCM_STREAM_MAX];                               /* data 330 */

static void PCMStreamReadThread(void);
static void PCMStreamRelease(PCM_STREAM_WRK *stp);
static void PCMStreamPauseSet(PCM_STREAM_WRK *wrk, int flg);
static int  iopPauseSubB(int set, int core, short target_vol);

void PCMStreamCreate(void)
{
    SemaParam      spara;
    ThreadParam    param;
    int            wrk_id;
    IOP_STREAM_RET ret;

    param.attr      = PCM_TH_ATTR;
    param.stackSize = PCM_TH_STACK;

    spara.attr      = 1;
    spara.option    = 0;
    spara.initCount = 0;
    spara.maxCount  = 1;

    for (wrk_id = 0; wrk_id < PCM_STREAM_MAX; wrk_id++)
    {
        PCM_STREAM_WRK *stp = &pcm_stream_wrk[wrk_id];

        printf("pcm_stream create\n");

        /* The reader takes no argument; its slot travels in ThreadParam.option
         * and comes back out through ReferThreadStatus(). */
        param.option       = (uintptr_t)stp;
        param.entry        = (void *)PCMStreamReadThread;
        param.initPriority = PRI_PCM_STREAM_READ;

        stp->read_th_idx = CreateThread(&param);
        stp->id          = (char)wrk_id;
        stp->use         = 0;

        stp->block_trans_sema = CreateSema(&spara);
        stp->rb_top           = 0;

        ret.status = ST_STREAM_NO_USE;
        ret.offset = 0;
        SetPCMStreamRet(wrk_id, ret);
    }
}

void PCMStreamInit(PCM_STREAM_INIT *p)
{
    int            wrk_id = p->wrk_id;
    IOP_STREAM_RET ret;

    printf("wrk->id = %d is Initialized\n", wrk_id);

    pcm_stream_wrk[wrk_id].use         = 0;
    pcm_stream_wrk[wrk_id].pause_phase = 0;

    ret.status = ST_STREAM_NO_USE;
    ret.offset = 0;
    SetPCMStreamRet(wrk_id, ret);
}

void PCMStreamStart(PCM_STREAM_START *p)
{
    int             oldstat;
    PCM_STREAM_WRK *stp = &pcm_stream_wrk[p->wrk_id];
    IOP_STREAM_RET  ret;

    printf("wrk->id = %d is Started\n", p->wrk_id);

    /* Nothing recovers from this -- the EE is supposed to have released the
     * slot first, so a double start is a protocol error. */
    if (stp->use)
    {
        printf("Illegal!! PCMStreamWrk Is Used\n");
        for (;;)
            SleepThread();
    }

    stp->trans_core = -1;

    /* `loop` is deliberately not cleared: it is set by the EE side alongside
     * the start and would be lost here. */
    stp->play_ok = 0;
    stp->stop    = 0;
    stp->pause   = 0;
    stp->use     = 1;

    ret.status = ST_STREAM_PRE_LOAD;
    ret.offset = 0;
    SetPCMStreamRet(stp->id, ret);

    stp->offset_sector = 0;
    stp->nchannel      = (char)p->nchannel;
    stp->start_sector  = p->start_sector;
    stp->size          = p->size;
    strcpy(stp->file_name, p->file_name);

    stp->rb_num  = PCM_RB_NUM;
    stp->rb_size = PCM_RB_SIZE;
    stp->rb_read = 0;

    while (1)
    {
        CpuSuspendIntr(&oldstat);
        stp->rb_top = (uintptr_t)AllocSysMemory(0, stp->rb_size * stp->rb_num, 0);
        CpuResumeIntr(oldstat);

        if (stp->rb_top != 0)
            break;

        printf("PCMPCMStreamStart() cannot get memory\n");
        DelayThread(500000);
    }

    printf("Start pcm_stream_read Thread!   alloc mem[%x]\n", stp->rb_top);

    StartThread(stp->read_th_idx, 0);
}

void PCMStreamPlay(PCM_STREAM_PLAY *p)
{
    PCM_STREAM_WRK *stp = &pcm_stream_wrk[p->wrk_id];

    /* A file that is not a whole number of ring slots leaves a short final
     * read, which the ring cannot express -- warn but carry on. */
    if (stp->size % stp->rb_size != 0)
        printf("File[%s] Size Is Illegal\n", stp->file_name);

    stp->vol.r = p->vol;
    stp->vol.l = p->vol;

    stp->play_ok = 1;
    printf("play_ok\n");

    /* The reader parks itself until this arrives. */
    WakeupThread(stp->read_th_idx);
}

static void PCMStreamReadThread(void)
{
    int             remain_sector;
    int             end_flg;
    int             first_read;
    int             next_read_num;
    int             read_sector_num;
    int             bb;
    int             nsector;
    int             ring_buf_1read_max;
    int             zero_buf;
    int             oldstat;
    PCM_STREAM_WRK *stp;
    ThreadInfo      info;
    int             ret;
    int             i;
    IOP_STREAM_RET  sret;

    end_flg    = 0;
    first_read = 1;

    /* A page of silence, kept for the whole life of the stream: it is what the
     * SPU is pointed at whenever the ring stops being valid. */
    CpuSuspendIntr(&oldstat);
    zero_buf = (uintptr_t)AllocSysMemory(0, 0x1000, 0);
    CpuResumeIntr(oldstat);
    memset((void *)zero_buf, 0, 0x1000);

    ReferThreadStatus(0, &info);
    stp = (PCM_STREAM_WRK *)info.option;

    /* Drain counts left over from whatever played last. */
    while (PollSema(stp->block_trans_sema) != KE_SEMA_ZERO)
        ;

    ring_buf_1read_max = (stp->rb_size << 6) >> 11;

    /* ---- prime read -----------------------------------------------------
     * NOTE: this issues a read at `offset_sector` and advances `rb_read`, but
     * does NOT advance `offset_sector` -- only the loop below does that, at
     * its end.  The loop's first read therefore fetches the same sectors
     * again, into the slots the prime read moved past.  Reproduced as found. */
    remain_sector = (stp->size >> 11) - stp->offset_sector;
    next_read_num = stp->rb_num - stp->rb_read;

    if (next_read_num < 0x40)
        read_sector_num = GetAlignUp(stp->rb_size * next_read_num, 11) >> 11;
    else
        read_sector_num = ring_buf_1read_max;

    if (read_sector_num >= remain_sector)
    {
        end_flg         = 1;
        read_sector_num = remain_sector;
    }

    iopReqRead(stp->start_sector + stp->offset_sector, read_sector_num,
               (void *)(stp->rb_top + stp->rb_read * stp->rb_size), 0);

    nsector = (read_sector_num << 11) / stp->rb_size;
    for (bb = 0; bb < nsector; bb++)
        stp->rb_read = RingBufAdd(stp->rb_read, stp->rb_num);

    for (;;)
    {
        remain_sector = (stp->size >> 11) - stp->offset_sector;
        next_read_num = stp->rb_num - stp->rb_read;

        /* Near the end of the ring, read only as far as the wrap. */
        if (next_read_num < 0x40)
            read_sector_num = GetAlignUp(stp->rb_size * next_read_num, 11) >> 11;
        else
            read_sector_num = ring_buf_1read_max;

        if (read_sector_num >= remain_sector)
        {
            end_flg         = 1;
            read_sector_num = remain_sector;
        }

        iopReqRead(stp->start_sector + stp->offset_sector, read_sector_num,
                   (void *)(stp->rb_top + stp->rb_read * stp->rb_size), 0);

        if (stp->pause)
        {
            if (stp->stop)
            {
                printf("stp->stop pre SetSPU_PCMZeroBlock\n");
                goto release;
            }

            /* Remember where the DMA had got to, then park it on silence so
             * the ring can sit still without repeating its last slot. */
            ret               = sceSdBlockTrans(stp->trans_core, SD_BLOCK_TRANS_STAT, 0, 0, 0);
            stp->pause_offset = (ret & 0xffffff) - stp->rb_top;
            sceSdVoiceTrans(stp->trans_core, 0, (unsigned char *)zero_buf, 0x4000, 0x1000);

            while (stp->pause)
            {
                SleepThread();
                if (stp->stop)
                {
                    printf("stp->stop after SetSPU_PCMZeroBlock\n");
                    goto release;
                }
            }

            if (stp->stop)
            {
                printf("stp->stop after SetSPU_PCMZeroBlock\n");
                goto release;
            }

            sceSdBlockTrans(stp->trans_core, SD_BLOCK_TRANS_CONT,
                            (unsigned char *)stp->rb_top, stp->rb_size << 7,
                            stp->rb_top + stp->rb_read * stp->rb_size);
        }

        nsector = (read_sector_num << 11) / stp->rb_size;
        for (bb = 0; bb < nsector; bb++)
            stp->rb_read = RingBufAdd(stp->rb_read, stp->rb_num);

        if (end_flg)
        {
            /* Silence half the ring ahead of the play position so the tail
             * runs out quietly, then let the DMA consume both halves. */
            memset((void *)(stp->rb_top + stp->rb_read * stp->rb_size), 0,
                   (stp->rb_size * stp->rb_num) / 2);

            for (i = 0; i < stp->rb_num / 2; i++)
                stp->rb_read = RingBufAdd(stp->rb_read, stp->rb_num);

            WaitSema(stp->block_trans_sema);
            WaitSema(stp->block_trans_sema);

        release:
            ret               = sceSdBlockTrans(stp->trans_core, SD_BLOCK_TRANS_STAT, 0, 0, 0);
            stp->pause_offset = (ret & 0xffffff) - stp->rb_top;
            sceSdVoiceTrans(stp->trans_core, 0, (unsigned char *)zero_buf, 0x4000, 0x1000);

            CpuSuspendIntr(&oldstat);
            FreeSysMemory((void *)zero_buf);
            CpuResumeIntr(oldstat);

            PCMStreamRelease(stp);
            return;
        }

        stp->offset_sector += read_sector_num;

        if (first_read)
        {
            /* The ring is primed, so a core can be claimed and playback can
             * start -- but only once the EE has actually said Play. */
            stp->trans_core = WaitSpuTransSema();

            sret.status = ST_STREAM_PLAYING;
            sret.offset = 0;
            SetPCMStreamRet(stp->id, sret);

            while (!stp->play_ok)
                SleepThread();

            first_read = 0;

            /* From here the DMA completion interrupt paces the loop. */
            sceSdSetTransIntrHandler(stp->trans_core, _intr_SignalSema,
                                     &stp->block_trans_sema);
            sceSdBlockTrans(stp->trans_core, SD_BLOCK_TRANS_LOOP,
                            (unsigned char *)stp->rb_top, stp->rb_size << 7, 0);

            sceSdSetParam(stp->trans_core | SD_P_BVOLL, stp->vol.l);
            sceSdSetParam(stp->trans_core | SD_P_BVOLR, stp->vol.r);
        }

        WaitSema(stp->block_trans_sema);
    }
}

static void PCMStreamRelease(PCM_STREAM_WRK *stp)
{
    int            oldstat;
    IOP_STREAM_RET ret;

    if (stp->trans_core >= 0)
        SignalSpuTransSema(stp->trans_core);

    if (stp->rb_top != 0)
    {
        CpuSuspendIntr(&oldstat);
        FreeSysMemory((void *)stp->rb_top);
        CpuResumeIntr(oldstat);

        stp->rb_top = 0;
    }

    /* Terminates its own thread -- everything after this is unreachable. */
    TerminateThread(stp->read_th_idx);

    ret.status = ST_STREAM_END;
    ret.offset = 0;
    SetPCMStreamRet(stp->id, ret);
}

static void PCMStreamPauseSet(PCM_STREAM_WRK *wrk, int flg)
{
    wrk->pause = flg;
}

void PCMStreamPause(PCM_STREAM_PAUSE *p)
{
    PCM_STREAM_WRK *stp = &pcm_stream_wrk[p->wrk_id];

    /* Already paused or mid-restart: collapse to "paused". */
    if ((unsigned char)(stp->pause_phase - 2) < 2)
        stp->pause_phase = 2;
    else
        stp->pause_phase = 1;
}

void PCMStreamRestart(PCM_STREAM_RESTART *p)
{
    PCM_STREAM_WRK *stp = &pcm_stream_wrk[p->wrk_id];

    if ((unsigned char)(stp->pause_phase - 2) < 2)
        stp->pause_phase = 3;
    else
        stp->pause_phase = 0;
}

void PCMStreamStop(PCM_STREAM_STOP *p)
{
    PCM_STREAM_WRK *stp = &pcm_stream_wrk[p->wrk_id];

    stp->pause_phase = ((unsigned char)(stp->pause_phase - 2) < 2) ? 2 : 1;
    stp->stop        = 1;

    /* The reader may be parked in the pause wait; wake it so it sees stop. */
    WakeupThread(stp->read_th_idx);
}

void PCMStreamVolSet(PCM_STREAM_SETVOL *p)
{
    PCM_STREAM_WRK *stp = &pcm_stream_wrk[p->wrk_id];

    stp->vol.r = p->vol;
    stp->vol.l = p->vol;
}

/* ROM BUG: nothing calls this and nothing takes its address -- 0x61b8 appears
 * nowhere in the module, as a jal, as a lui/addiu pair or as a data word.  It
 * is the PCM counterpart of iop_snd.c's iopSndMain(), which iopSndInit() does
 * start as a thread; this one was never wired to anything.
 *
 * So the whole pause state machine below is unreachable, and with it
 * PCMStreamPauseSet(), iopPauseSubB() and the volume ramp.  `pause_phase` is
 * the only thing this reads, and this is the only thing that reads it -- the
 * reader thread watches `stop` and nothing else.  Two consequences:
 *
 *   - REQ_PCM_STREAMPAUSE and REQ_PCM_STREAMRESTART reach PCMStreamPause() /
 *     PCMStreamRestart() from iopCommand() and set `pause_phase`, but nothing
 *     ever acts on it, so pause and restart do nothing at all.
 *   - Stopping still works, through PCMStreamStop()'s `stop` flag.  But that
 *     also sets `pause_phase` to ask for a fade first, and since no one runs
 *     the fade the stream cuts off at full volume instead of ramping down.
 *
 * Left as found. */
void PCMStreamMain(void)
{
    int             wrk_id;
    PCM_STREAM_WRK *stp;
    int             ret;

    for (wrk_id = 0; wrk_id < PCM_STREAM_MAX; wrk_id++)
    {
        stp = &pcm_stream_wrk[wrk_id];

        if (!stp->use)
            continue;

        if (stp->pause_phase == 1)
        {
            /* Both channels have to reach zero before the reader is allowed
             * to park the DMA, or the cut would be audible. */
            ret = iopPauseSubB(stp->trans_core | SD_P_BVOLL, stp->trans_core, 0)
                & iopPauseSubB(stp->trans_core | SD_P_BVOLR, stp->trans_core, 0)
                & 1;

            if (ret)
            {
                stp->pause_phase = 2;
                PCMStreamPauseSet(stp, 1);
            }
        }
        else if (stp->pause_phase == 3)
        {
            PCMStreamPauseSet(stp, 0);
            WakeupThread(stp->read_th_idx);
            stp->pause_phase = 0;

            /* Falls straight into the ramp back up. */
            iopPauseSubB(stp->trans_core | SD_P_BVOLL, stp->trans_core, stp->vol.l);
            iopPauseSubB(stp->trans_core | SD_P_BVOLR, stp->trans_core, stp->vol.r);
        }
        else if (stp->pause_phase == 0)
        {
            iopPauseSubB(stp->trans_core | SD_P_BVOLL, stp->trans_core, stp->vol.l);
            iopPauseSubB(stp->trans_core | SD_P_BVOLR, stp->trans_core, stp->vol.r);
        }
        /* phase 2 (paused) has nothing to do. */
    }
}

/* Steps one volume register towards `target_vol`; returns 1 once it is there.
 * The SPU2 reports volume in a 15-bit field whose top bit is bit 14, so the
 * read-back is sign-extended by hand before it can be compared.
 *
 * Only PCMStreamMain() calls this, so in this build it never runs -- see the
 * note there.  Its +300/-800 asymmetry mirrors iop_snd.c's iopPauseSub()
 * (+150/-400) at exactly double scale, so the intent is clear even though the
 * code is unreachable. */
static int iopPauseSubB(int set, int core, short target_vol)
{
    short now_vol;
    int   ret;

    now_vol = sceSdGetParam(set);
    now_vol = now_vol | ((now_vol & 0x4000) << 1);

    ret = (short)(target_vol - now_vol);

    if (ret > 0)
    {
        if (ret < PCM_VOL_STEP_UP + 1)
        {
            sceSdSetParam(set, target_vol);
            return 1;
        }
        now_vol += PCM_VOL_STEP_UP;
    }
    else
    {
        if (ret >= 0)
            return 0;

        if (ret > -(PCM_VOL_STEP_DOWN + 1))
        {
            sceSdSetParam(set, target_vol);
            return 1;
        }
        now_vol -= PCM_VOL_STEP_DOWN;
    }

    sceSdSetParam(set, now_vol);

    return 0;
}

/* Uncalled: PCMStreamReadThread() open-codes the same sequence at its two stop
 * points instead (the "stp->stop pre/after SetSPU_PCMZeroBlock" printfs there
 * still carry this function's name).  Another refactor that was written but
 * never applied, like iop_snd.c's IsValidVoice(). */
void SetSPU_PCMZeroBlock(PCM_STREAM_WRK *stp, int zero_buf)
{
    int ret;

    ret               = sceSdBlockTrans(stp->trans_core, SD_BLOCK_TRANS_STAT, 0, 0, 0);
    stp->pause_offset = (ret & 0xffffff) - stp->rb_top;

    sceSdVoiceTrans(stp->trans_core, 0, (unsigned char *)zero_buf, 0x4000, 0x1000);
}
