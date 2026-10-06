/* ==========================================================================
 *  system/iop/iop_stream.c
 *
 *  ADPCM streaming: two slots, each with a pair of threads.
 *
 *    StreamReadThread()  pulls interleaved packets off the disc into a 64-slot
 *                        ring buffer in IOP RAM, and rewrites the loop flags in
 *                        the ADPCM block headers as it goes.
 *    StreamVoiceThread() copies one packet at a time from the ring into SPU
 *                        RAM and keys the voices.
 *
 *  What paces the whole thing is the SPU itself.  Each channel plays out of two
 *  alternating SPU buffers (`spu_packet[ch][0..1]`); the voice thread points the
 *  core's IRQ address at the buffer that is *not* playing, enables the core
 *  interrupt, and sleeps.  When playback crosses into that buffer the SPU raises
 *  its interrupt, _intr_SignalSemaSPUAdrs() signals the slot's semaphore, and
 *  the thread wakes up with exactly one buffer's worth of time to refill the
 *  other.  If it ever finds the semaphore already signalled it has missed that
 *  window, which is the "Trans Is Not in Time" banner.
 *
 *  Looping has two cases and they are quite different.  A stream whose loop
 *  point lands on a packet boundary (`just_loop`) simply has the reader seek
 *  back.  One that loops mid-packet cannot, so StreamStart() reserves a third
 *  SPU buffer per channel and PreloadLoopPacketSub() fills it with the two
 *  packets that straddle the loop point; the voice thread swings the voices onto
 *  that buffer for one packet and back.
 *
 *  Teardown is cooperative from both ends: the `stop` bit is checked after every
 *  blocking wait, each thread raises its own read_end/voice_end bit on the way
 *  out, and whichever finishes second calls StreamReleaseSub().
 *
 *  Reconstructed from iopsys.irx (Feb 6 2004 prototype, SLES_523.84).
 *  No source line numbers are available for this module, so there are no
 *  trailing ROM-line annotations.
 * ======================================================================== */

#include "iop_stream.h"

#include <stdio.h>                  /* printf                          */
#include <intrman.h>                /* CpuSuspendIntr / CpuResumeIntr  */
#include <libsd.h>                  /* sceSd*                          */
#include <sysclib.h>                /* strcpy                          */
#include <sysmem.h>                 /* AllocSysMemory / FreeSysMemory  */
#include <thbase.h>                 /* threads                         */
#include <thsemap.h>                /* semaphores                      */

#include "iop.h"                    /* iopReqRead, WaitSpuTransSema    */
#include "iop_snd.h"                /* MyOnVoice, VolSet, EffectMix    */
#include "../../sdk/iop_voice.h"    /* PORT: MioPan_VoiceSetStereoPair */
#include "utility2i.h"              /* RingBufAdd / RingBufCalcDiff    */

/* libsd entries used here; iop_snd.c has the full list. */
#define SD_A_IRQA           0x1f00  /* raise an interrupt at this address */
#define SD_VA_LSAX          0x2140
#define SD_VA_NAX           0x2240
#define SD_C_IRQ_ENABLE     0x04

#define SECTOR_SHIFT        11      /* 2048-byte disc sectors    */
#define SECTOR_SIZE         2048
#define SPU_BLOCK_SHIFT     4       /* 16-byte ADPCM blocks      */
#define SPU_BLOCK_SIZE      16

#define STREAM_WRK_NUM      2

/* 0x02000000 is TH_C. */
#define STREAM_TH_ATTR      0x02000000
#define STREAM_TH_STACK     0x800

/* Ring geometry.  The reader tops up in bursts of up to RB_1READ_SLOTS slots
 * and parks once it is within that many slots of catching the voice thread. */
#define RB_NUM              0x40
#define RB_1READ_SLOTS      16

/* PORT: how long StreamAbort() gives the stream's threads to get themselves out
 * before it gives up on them.  Spent on the RPC thread, so the EE stalls for it;
 * a thread that is going to notice its wake does so in single-digit ms. */
#define ABORT_POLL_US       2000
#define ABORT_WAIT_US       300000

STREAM_WRK stream_wrk[STREAM_WRK_NUM];                                       /* data 10   */
static int sema_Voice_array[STREAM_WRK_NUM];                                 /* bss 1d00  */

static void StreamReleaseSub(STREAM_WRK *stp);
static void StreamReadThread(void);
static void StreamVoiceThread(void);
static int  _intr_SignalSemaSPUAdrs(int core_bit, void *dumy);
static void StreamTransSub(STREAM_WRK *stp, int toggle);
static void PreloadLoopPacketSub(STREAM_WRK *stp);

/* --------------------------------------------------------------------------
 *  Slot lifetime
 * ------------------------------------------------------------------------ */

void StreamCreate(void)
{
    int            i;
    SemaParam      spara;
    ThreadParam    param;
    int            oldstat;
    int            wrk_id;
    int            offset;
    IOP_STREAM_RET ret;

    param.attr      = STREAM_TH_ATTR;
    param.stackSize = STREAM_TH_STACK;

    spara.attr      = 1;
    spara.option    = 0;
    spara.initCount = 0;
    spara.maxCount  = 1;

    PrintIOPMem("pre create iop_stream");

    for (i = 0; i < STREAM_WRK_NUM; i++)
    {
        /* Both threads take their work pointer through ThreadParam::option --
         * IOP thread entries take no argument, so they recover it with
         * ReferThreadStatus(0, &info). */
        param.option       = (uintptr_t)&stream_wrk[i];

        param.entry        = (void *)StreamReadThread;
        param.initPriority = PRI_STREAM_READ;
        stream_wrk[i].read_th_idx = CreateThread(&param);
        printf("stream_read thread[%d] = 0x%x\n", i, stream_wrk[i].read_th_idx);

        param.entry        = (void *)StreamVoiceThread;
        param.initPriority = PRI_STREAM_VOICE;
        stream_wrk[i].voice_th_idx = CreateThread(&param);
        printf("stream_voice thread[%d] = 0x%x\n", i, stream_wrk[i].voice_th_idx);

        stream_wrk[i].id            = i;
        stream_wrk[i].use           = 0;
        stream_wrk[i].read_end      = 0;
        stream_wrk[i].voice_end     = 0;
        stream_wrk[i].rb_top        = 0;
        stream_wrk[i].abandoned     = 0;                                     /* PORT */
        stream_wrk[i].offset_sector = 0;

        sema_Voice_array[i] = CreateSema(&spara);

        CpuSuspendIntr(&oldstat);
        sceSdSetCoreAttr(i | SD_C_IRQ_ENABLE, 0);
        CpuResumeIntr(oldstat);

        wrk_id     = i;
        offset     = stream_wrk[i].offset_sector;
        ret.status = ST_STREAM_NO_USE;
        ret.offset = offset;
        SetStreamRet(wrk_id, ret);
    }

    /* One handler for both cores; it tells them apart by the bit it is given. */
    sceSdSetSpu2IntrHandler(_intr_SignalSemaSPUAdrs, nullptr);

    PrintIOPMem("after create iopstream");
}

/* Called by whichever of the two threads exits second, so the ring buffer is
 * only freed once nothing can still be reading it. */
static void StreamReleaseSub(STREAM_WRK *stp)
{
    int            oldstat;
    int            i;
    int            offset;
    IOP_STREAM_RET ret;

    /* PORT: `abandoned` means StreamAbort() could not get this stream's threads
     * out and they are still live on this work struct.  Zeroing rb_top under one
     * of them is what turns a healthy transfer source into a small integer --
     * StreamTransSub() computes rb_top + rb_voice * rb_size, so rb_top == 0 hands
     * sceSdVoiceTrans() an address like 0x12000, and the memcpy faults somewhere
     * that looks unrelated.  Leak the block instead: 64 ring slots is cheap next
     * to a crash, and the stranded thread goes on writing somewhere valid. */
    if (stp->rb_top != 0 && !stp->abandoned)
    {
        CpuSuspendIntr(&oldstat);
        FreeSysMemory((void *)stp->rb_top);
        CpuResumeIntr(oldstat);

        printf("FreeSysMemory[%p] wrk %d\n", stp->rb_top, stp->id);
        stp->rb_top = 0;
    }
    else if (stp->rb_top != 0)
    {
        printf("iop: wrk %d abandoned -- leaking ring buffer %p\n",
               stp->id, (void *)stp->rb_top);
    }

    if (stp->play_ok)
    {
        stp->play_ok = 0;

        /* The voices are still running out of SPU memory that is about to be
         * handed to someone else, so park them on the stop block and do not
         * come back until they are demonstrably inside it. */
        do
        {
            for (i = 0; i < stp->nchannel; i++)
            {
                SetLoopAdrsStopBlock(stp->core[i], stp->voice[i]);
                SetAdrsStopBlock(stp->core[i], stp->voice[i]);
            }

            DelayThread(100000);

            printf("stop voice wait id %d adrs %x\n", stp->id,
                   sceSdGetAddr(SD_VA_NAX | stp->core[0] | (stp->voice[0] << 1)));
        } while (!IsInStopBlock(stp->core[0], stp->voice[0]));
    }

    stp->use = 0;

    offset     = stp->offset_sector;
    ret.status = ST_STREAM_NO_USE;
    ret.offset = offset;
    SetStreamRet(stp->id, ret);

    printf("Release End\n");
}

/* Empty in this build.  REQ_STREAM_RELEASE therefore does nothing -- the slot
 * is actually released by StreamReleaseSub() when the threads wind down. */
void StreamRelease(STREAM_RELEASE *p)
{
}

void StreamStart(STREAM_START *p)
{
    int            i;
    int            j;
    int            oldstat;
    STREAM_WRK    *stp = &stream_wrk[p->wrk_id];
    IOP_STREAM_RET ret;

    PrintIOPMem("pre start iopstream");

    if (stp->use)
    {
        /* Nothing to recover to: the EE has asked for a slot that is still
         * playing, so park the service thread rather than corrupt it. */
        printf("Illegal!! StreamWrk %d Is Used\n", p->wrk_id);
        for (;;)
            SleepThread();
    }

    /* PORT: a slot StreamAbort() gave up on comes back with abandoned set and
     * its ring buffer leaked.  Clearing it here lets the slot be reused
     * normally rather than leaking one buffer per stream for the rest of the
     * run -- but say so, because a stranded thread from the previous stream may
     * still be reading stp, and this is the moment the two collide. */
    if (stp->abandoned)
    {
        printf("iop: StreamStart() reusing abandoned wrk %d\n", p->wrk_id);
        stp->abandoned = 0;
    }

    stp->stop      = 0;
    stp->pause     = 0;
    stp->play_ok   = 0;
    stp->file_end  = 0;
    stp->read_end  = 0;
    stp->read_th   = 0;
    stp->voice_end = 0;
    stp->voice_th  = 0;
    stp->use       = 1;

    stp->nchannel        = p->nchannel;
    stp->interleave_byte = p->interleave_byte;

    for (i = 0; i < stp->nchannel; i++)
    {
        for (j = 0; j < 2; j++)
        {
            stp->spu_packet[i][j] = p->spu_packet[i][j];
            printf("spu_packet = %x\n", stp->spu_packet[i][j]);
        }
    }

    stp->offset_sector = p->offset;

    ret.status = ST_STREAM_PRE_LOAD;
    ret.offset = p->offset;
    SetStreamRet(p->wrk_id, ret);

    /* Where in the file this stream is starting, counted in ADPCM blocks of
     * one channel -- the unit the loop points are given in. */
    stp->block_offset =
        ((stp->offset_sector << SECTOR_SHIFT) >> SPU_BLOCK_SHIFT) / stp->nchannel;

    stp->start_sector = p->start_sector;
    stp->size         = p->size;
    strcpy(stp->file_name, p->file_name);

    stp->rb_num   = RB_NUM;
    stp->rb_voice = 0;
    stp->rb_read  = 0;
    /* Half a ring in hand either way: the reader tops up once the voice thread
     * has drained past this, and starts the voice thread once it is this far
     * ahead. */
    stp->rb_read_diff  = stp->rb_num / 2;
    stp->rb_voice_diff = stp->rb_num / 2;

    /* One ring slot holds one interleave unit for every channel. */
    stp->rb_size = stp->interleave_byte * stp->nchannel;

    for (i = 0; i < stp->nchannel; i++)
    {
        stp->spu_loop_packet[i] = p->spu_loop_packet[i];
        printf("spu_loop_packet = %x\n", stp->spu_loop_packet[i]);
    }

    stp->loop_start_block    = p->loop_start_block;
    stp->loop_end_block      = p->loop_end_block;
    stp->loop_start_fraction = p->loop_start_fraction;
    stp->loop_end_fraction   = p->loop_end_fraction;

    printf("loop_start = %d, loop_start_fraction = %d\n",
           stp->loop_start_block, stp->loop_start_fraction);
    printf("loop_end = %d, loop_end_fraction = %d\n",
           stp->loop_end_block, stp->loop_end_fraction);

    if (stp->loop_start_block == stp->loop_end_block)
        stp->loop = 0;

    /* ROM BUG: `i` is left at stp->nchannel by the copy loop above, so this
     * reads one past spu_loop_packet[] -- for the usual nchannel of 2 that is
     * spu_packet[0][0], which is never zero.  The "no loop packet was
     * allocated, so treat this as a whole-packet loop" half of the test can
     * therefore never fire, and just_loop ends up decided purely by the two
     * fractions.  A fractional loop with no loop packet reserved then reaches
     * PreloadLoopPacketSub() with a null destination.  Almost certainly meant
     * spu_loop_packet[0].  Left as found. */
    if (stp->spu_loop_packet[i] == 0 ||
        (stp->loop_start_fraction == 0 && stp->loop_end_fraction == 0))
        stp->just_loop = 1;
    else
        stp->just_loop = 0;

    printf("stp->just_loop = %d loop = %d\n", stp->just_loop, stp->loop);

    /* The one allocation a stream makes.  Retried forever: there is no way to
     * report failure back to the EE from here. */
    for (;;)
    {
        CpuSuspendIntr(&oldstat);
        stp->rb_top = (uintptr_t)AllocSysMemory(0, stp->rb_size * stp->rb_num, 0);
        CpuResumeIntr(oldstat);

        if (stp->rb_top != 0)
            break;

        DelayThread(500000);
        printf("StreamStart() wrk%d cannot get memory\n", p->wrk_id);
    }

    PrintIOPMem("after start iopstream");

    /* Only the reader starts here; it starts the voice thread once the ring
     * has enough in it to play from. */
    StartThread(stp->read_th_idx, 0);
}

/* --------------------------------------------------------------------------
 *  EE commands
 * ------------------------------------------------------------------------ */

void StreamPlay(STREAM_PLAY *p)
{
    int         i;
    int         set;
    STREAM_WRK *stp = &stream_wrk[p->wrk_id];

    /* Dead: nothing in the module ever sets `ready`, so this never prints. */
    if (stp->ready)
        printf("StreamPlay() Stream Not Ready\n");

    stp->irq_core = p->irq_core;
    /* VOICE_ATTR::loop and STREAM_WRK::loop happen to sit at the same bit, so
     * this compiles to a masked copy in place rather than a test and a store. */
    stp->loop     = p->attr[0].loop;

    for (i = 0; i < stp->nchannel; i++)
    {
        stp->core[i]  = p->attr[i].core;
        stp->voice[i] = p->voice[i];

        EffectMix(stp->core[i], 1 << stp->voice[i], p->attr[i].effect);

        set = stp->core[i] | (stp->voice[i] << 1);

        VolSetDirect(stp->core[i], stp->voice[i], p->vol[i]);
        PitchSet(set, p->pitch);
        AdsrSet(set, p->adsr1[i], p->adsr2[i]);
    }

    /* PORT: tell the voice engine these two voices are one stereo stream.
     *
     * On hardware nothing needs to know -- the SPU plays every voice off the
     * same clock, so the channels cannot drift.  Here each voice would own an
     * independent SDL audio stream whose fill rounds to a whole ADPCM block, so
     * the pair skews by up to ~0.6 ms and the skew moves every decode pass:
     * a sweeping comb filter, heard as flanging on speech.  Pairing makes the
     * engine decode both in lockstep onto one stream.  Nothing else reads it,
     * and it is cleared when the voices stop. */
    if (stp->nchannel == 2)
    {
        MioPan_VoiceSetStereoPair(stp->core[0], stp->voice[0],
                                  stp->core[1], stp->voice[1]);
    }

    stp->play_ok = 1;

    /* The voice thread is parked waiting for exactly this bit. */
    WakeupThread(stp->voice_th_idx);
}

void StreamPause(STREAM_PAUSE *p)
{
    STREAM_WRK *stp = &stream_wrk[p->wrk_id];
    int         i;

    if (stp->pause)
        return;

    if (!stp->play_ok)
    {
        printf("StreamPause() %d play_ok Not Yet\n", p->wrk_id);
    }
    else
    {
        for (i = 0; i < stp->nchannel; i++)
            MyPauseVoice(stp->core[i], stp->voice[i]);
    }

    /* Raised even when there was nothing to pause, so a restart still pairs. */
    stp->pause = 1;
}

void StreamRestart(STREAM_RESTART *p)
{
    STREAM_WRK *stp = &stream_wrk[p->wrk_id];
    int         iChannel;
    int         iOffset;

    if (!stp->pause)
        return;

    if (!stp->play_ok)
    {
        printf("StreamRestart() %d play_ok Not Yet\n", p->wrk_id);
    }
    else
    {
        /* Channel 0 resumes where it was paused; the rest resume at a fixed
         * distance from it, because the channels have to stay sample-aligned
         * and each one's own saved address may have drifted. */
        MyRestartVoice(stp->core[0], stp->voice[0]);

        for (iChannel = 1; iChannel < stp->nchannel; iChannel++)
        {
            iOffset = stp->spu_packet[iChannel][0] - stp->spu_packet[0][0];

            MyRestartVoiceOffset(stp->core[iChannel], stp->voice[iChannel],
                                 stp->core[0], stp->voice[0], iOffset);
        }
    }

    stp->pause = 0;
    WakeupThread(stp->voice_th_idx);
}

void StreamStop(STREAM_STOP *p)
{
    STREAM_WRK *stp = &stream_wrk[p->wrk_id];

    stp->stop = 1;

    WakeupThread(stp->read_th_idx);

    /* The voice thread may never have been started -- the stream can be
     * stopped while the ring is still filling -- so start it here so it can
     * run its own teardown.  A non-zero result means it was already running,
     * and then it only needs waking. */
    if (StartThread(stp->voice_th_idx, 0) == 0)
    {
        printf("Voice Thread Start From Stop\n");
    }
    else
    {
        WakeupThread(stp->voice_th_idx);
        printf("Voice Thread Already Started\n");
    }

    /* Release it from the SPU interrupt wait as well; the stop bit is checked
     * on the way out of that. */
    SignalSema(sema_Voice_array[stp->irq_core]);
}

/* PORT: the ROM's StreamAbort() calls TerminateThread() on both stream threads
 * and then releases the stream itself, on the assumption that nothing is left
 * running.  That assumption does not survive the host: SDL cannot kill a thread
 * and MioPan_IopTerminateThread() only signals its wake semaphore, so both
 * threads are still alive -- and, having just been signalled, are about to run.
 * StreamReleaseSub() then frees the ring buffer and sets rb_top to 0 under them,
 * the voice thread comes round to StreamTransSub(), and rb_top + rb_voice *
 * rb_size hands sceSdVoiceTrans() a bare offset like 0x12000.  Nothing is mapped
 * there, so the memcpy faults -- at a different place every run, depending on
 * where the two threads happened to be when the abort landed.  That is the
 * "IOP crashes inconsistently" symptom.
 *
 * So abort escalates the cooperative stop instead of pre-empting it, and the
 * threads do their own release exactly as they do for StreamStop().  It still
 * differs from StreamStop() in the two ways that matter on an error path: it
 * signals BOTH voice semaphores rather than only irq_core's (irq_core is a
 * stream-slot index, not a core, and the two agree only by convention), and it
 * waits for the threads to actually get out.
 *
 * If they do not get out, they are genuinely wedged and there is no safe way to
 * reclaim what they hold.  The slot is then handed back to the EE anyway -- that
 * is the whole point of the command -- but `abandoned` keeps StreamReleaseSub()
 * from freeing the ring buffer out from under them.  Leaking it is the price of
 * not being able to kill a thread; a stranded thread writing into memory that is
 * still its own is survivable, and one writing to 0x12000 is not. */
void StreamAbort(STREAM_STOP *p)
{
    STREAM_WRK *stp = &stream_wrk[p->wrk_id];
    int         i;
    int         waited;

    printf("*************ABORT STREAM***********\n");

    if (stp->play_ok)
    {
        for (i = 0; i < stp->nchannel; i++)
            MyOffVoice(stp->core[i], stp->voice[i]);
    }

    /* Already 1 in the case this exists for -- the EE only sends the abort after
     * REQ_STREAM_STOP has gone unanswered for 600 frames -- but the command is
     * also legal on a stream that was never stopped. */
    stp->stop = 1;

    if (!stp->read_end)
        WakeupThread(stp->read_th_idx);

    /* The voice thread may never have been started -- a stream can be aborted
     * while the ring is still filling -- and then it has to be, so that it can
     * run its own teardown and raise voice_end.  A non-zero result means it was
     * already running and only needs waking.
     *
     * Guarded on voice_end, because the case this whole path exists for is
     * precisely the asymmetric one: the voice thread has finished and the read
     * thread has not (a PRESTREAM VOICE END RELEASE with no matching PRE STREAM
     * READ END RELEASE).  Starting a thread that has already exited would run
     * StreamVoiceThread() a second time over a stream that is being torn down. */
    if (!stp->voice_end)
    {
        if (StartThread(stp->voice_th_idx, 0) != 0)
            WakeupThread(stp->voice_th_idx);
    }

    for (i = 0; i < STREAM_WRK_NUM; i++)
        SignalSema(sema_Voice_array[i]);

    /* Bounded: this runs on the RPC thread, so the EE is blocked for as long as
     * it takes.  A thread that is going to notice does so in a few milliseconds;
     * anything past that is wedged and more waiting will not help. */
    for (waited = 0; waited < ABORT_WAIT_US; waited += ABORT_POLL_US)
    {
        if (stp->read_end && stp->voice_end)
            break;

        DelayThread(ABORT_POLL_US);
    }

    if (stp->read_end && stp->voice_end)
    {
        /* Both got out, so whichever finished second has already released the
         * stream the ordinary way.  Nothing left to do. */
        printf("ABORT STREAM %d released cleanly after %d us\n", stp->id, waited);
        return;
    }

    printf("iop: ABORT wrk %d threads did not exit (read_end=%d voice_end=%d)"
           " -- abandoning\n", stp->id, stp->read_end, stp->voice_end);

    stp->abandoned = 1;
    StreamReleaseSub(stp);
}

void StreamVolSet(STREAM_SETVOL *p)
{
    STREAM_WRK *stp = &stream_wrk[p->wrk_id];
    int         i;

    for (i = 0; i < stp->nchannel; i++)
        VolSet(stp->core[i], stp->voice[i], p->vol[i]);
}

void StreamPitchSet(STREAM_SETPITCH *p)
{
    STREAM_WRK *stp = &stream_wrk[p->wrk_id];
    int         i;

    for (i = 0; i < stp->nchannel; i++)
        PitchSet(stp->core[i] | (stp->voice[i] << 1), p->pitch);
}

/* --------------------------------------------------------------------------
 *  The reader
 * ------------------------------------------------------------------------ */

static void StreamReadThread(void)
{
    int         rb_remain;
    int         rb_diff;
    int         remain_sector;
    uintptr_t         ring_buf_adrs;
    int         end_flg;
    int         toggle;
    int         first_read;
    int         read_sector_num;
    int         bb;
    int         nsector;
    int         block_in_1read;
    int         remain_block;
    int         just_loop_flg;
    int         fract_loop_flg;
    int         ring_buf_1read_max;
    int         block_in_1packet;
    STREAM_WRK *stp;
    ThreadInfo  info;
    int         loop_read_block;

    toggle  = 0;
    end_flg = 0;

    ReferThreadStatus(0, &info);
    stp = (STREAM_WRK *)info.option;

    stp->read_th = 1;

    nsector            = stp->size >> SECTOR_SHIFT;
    ring_buf_1read_max = (stp->rb_size >> SECTOR_SHIFT) << 4;
    block_in_1packet   = stp->interleave_byte >> SPU_BLOCK_SHIFT;
    block_in_1read     =
        ((ring_buf_1read_max << SECTOR_SHIFT) >> SPU_BLOCK_SHIFT) / stp->nchannel;

    first_read     = 1;
    just_loop_flg  = 0;
    fract_loop_flg = 0;

    /* A mid-packet loop needs its straddling packets in SPU RAM before
     * playback can ever reach them. */
    if (!stp->just_loop)
        PreloadLoopPacketSub(stp);

    do
    {
        /* Never read past the end of the ring array in one go -- the read
         * lands in contiguous slots, so it has to stop at the wrap. */
        rb_remain       = stp->rb_num - stp->rb_read;
        read_sector_num = ring_buf_1read_max;
        if (rb_remain < RB_1READ_SLOTS)
            read_sector_num = (stp->rb_size >> SECTOR_SHIFT) * rb_remain;

        if (!stp->loop)
        {
            remain_sector = nsector - stp->offset_sector;
            if (remain_sector <= read_sector_num)
            {
                end_flg         = 1;
                read_sector_num = remain_sector;
            }
        }
        else
        {
            remain_block = stp->loop_end_block
                         - (((stp->offset_sector << SECTOR_SHIFT) >> SPU_BLOCK_SHIFT)
                            / stp->nchannel);

            if (remain_block <= block_in_1read)
            {
                /* This read is the one that reaches the loop point, so it is
                 * cut short there rather than run on into the tail. */
                loop_read_block = remain_block * SPU_BLOCK_SIZE;

                if (stp->just_loop)
                {
                    just_loop_flg   = 1;
                    read_sector_num = stp->nchannel * (loop_read_block >> SECTOR_SHIFT);
                    printf("just_loop read_sector_num = %d\n", read_sector_num);
                }
                else
                {
                    /* The loop point is inside a sector; round up so the whole
                     * of it arrives. */
                    fract_loop_flg   = 1;
                    loop_read_block += SECTOR_SIZE - 1;
                    read_sector_num  = stp->nchannel * (loop_read_block >> SECTOR_SHIFT);
                    printf("fract_loop read_sector_num = %d\n", read_sector_num);
                }
            }
        }

        rb_diff = RingBufCalcDiff(stp->rb_read, stp->rb_voice, stp->rb_num);

        if (first_read)
        {
            /* Enough in hand to survive a seek: hand over to the voice thread
             * and never come back through here. */
            if (rb_diff > stp->rb_voice_diff)
            {
                first_read = 0;
                StartThread(stp->voice_th_idx, 0);
            }
        }
        else if (rb_diff >= stp->rb_num - RB_1READ_SLOTS)
        {
            /* Ring nearly full.  The voice thread wakes us when it has taken
             * enough out; the stop check is after the sleep so a stop that
             * lands while parked is still seen. */
            do
            {
                if (stp->stop)
                    goto stopped;

                SleepThread();

                rb_diff = RingBufCalcDiff(stp->rb_read, stp->rb_voice, stp->rb_num);
            } while (rb_diff >= stp->rb_num - RB_1READ_SLOTS);
        }

        if (stp->stop)
            break;

        ring_buf_adrs = stp->rb_top + stp->rb_read * stp->rb_size;
        iopReqRead(stp->start_sector + stp->offset_sector, read_sector_num,
                   (void *)ring_buf_adrs, 0);

        /* Every packet gets its loop flags rewritten so the SPU ping-pongs
         * between the two buffers instead of stopping at the end of one:
         * even packets get a LOOP_START on their first block, odd ones a
         * LOOP_END on their last.  The final slot of the read is handled
         * separately below because it may be a file end or a loop point. */
        for (bb = 0; bb < ((read_sector_num << SECTOR_SHIFT) / stp->rb_size) - 1; bb++)
        {
            if (toggle)
                SetStreamLoopFlgSub(stp,
                                    (SPU_BLOCK_DATA *)(ring_buf_adrs + stp->rb_size * bb),
                                    block_in_1packet - 1, SPU_LOOP_END);
            else
                SetStreamLoopFlgSub(stp,
                                    (SPU_BLOCK_DATA *)(ring_buf_adrs + stp->rb_size * bb),
                                    0, SPU_LOOP_START);

            toggle ^= 1;

            stp->rb_read = RingBufAdd(stp->rb_read, stp->rb_num);
        }

        if (end_flg)
        {
            SetStreamLoopFlgSub(stp,
                                (SPU_BLOCK_DATA *)(ring_buf_adrs + stp->rb_size * bb),
                                block_in_1packet - 1, SPU_END);

            stp->offset_sector = 0;
            stp->file_end      = 1;
            printf("File End rb_read = %d, bb = %d\n", stp->rb_read, bb);
        }
        else if (fract_loop_flg)
        {
            printf("Fract stp->rb_read = %d\n", stp->rb_read);

            /* The loop point is partway into this packet, so the LOOP_END goes
             * on the block it actually falls in. */
            SetStreamLoopFlgSub(stp,
                                (SPU_BLOCK_DATA *)(ring_buf_adrs + stp->rb_size * bb),
                                stp->loop_end_fraction - 1, SPU_LOOP_END);

            /* Resume two packets before the loop start: those two are already
             * in the preloaded loop packet, so reading resumes after them. */
            stp->offset_sector =
                stp->nchannel
                * ((((stp->loop_start_block - stp->loop_start_fraction)
                     + block_in_1packet * 2) * SPU_BLOCK_SIZE) >> SECTOR_SHIFT);

            fract_loop_flg = 0;
            toggle         = 0;
        }
        else
        {
            if (toggle)
                SetStreamLoopFlgSub(stp,
                                    (SPU_BLOCK_DATA *)(ring_buf_adrs + stp->rb_size * bb),
                                    block_in_1packet - 1, SPU_LOOP_END);
            else
                SetStreamLoopFlgSub(stp,
                                    (SPU_BLOCK_DATA *)(ring_buf_adrs + stp->rb_size * bb),
                                    0, SPU_LOOP_START);

            toggle ^= 1;

            if (just_loop_flg)
            {
                printf("Just stp->rb_read = %d\n", stp->rb_read);

                stp->offset_sector =
                    stp->nchannel * ((stp->loop_start_block * SPU_BLOCK_SIZE) >> SECTOR_SHIFT);

                just_loop_flg = 0;
                toggle        = 0;
            }
            else
            {
                stp->offset_sector += read_sector_num;
            }
        }

        stp->rb_read = RingBufAdd(stp->rb_read, stp->rb_num);
    } while (!stp->file_end && !stp->stop);

stopped:
    /* Stopped before the ring ever filled, so the voice thread was never
     * started.  Start it anyway -- it is the one that reports the slot free. */
    if (first_read)
        StartThread(stp->voice_th_idx, 0);

    ReadThreadExit(stp);
}

/* --------------------------------------------------------------------------
 *  The player
 * ------------------------------------------------------------------------ */

static void StreamVoiceThread(void)
{
    int            i;
    int            rb_diff;
    int            last_packet;
    int            toggle;
    int            block_in_1packet;
    STREAM_WRK    *stp;
    ThreadInfo     info;
    int            offset;
    IOP_STREAM_RET ret;
    int            oldstat;

    toggle = 0;

    ReferThreadStatus(0, &info);
    stp = (STREAM_WRK *)info.option;

    last_packet      = 0;
    block_in_1packet = stp->interleave_byte >> SPU_BLOCK_SHIFT;

    stp->voice_th = 1;

    if (stp->stop)
        VoiceThreadExit(stp);

    /* Fill both SPU buffers before anything is keyed on. */
    for (i = 0; i < 2; i++)
    {
        StreamTransSub(stp, toggle);
        toggle ^= 1;

        stp->block_offset += block_in_1packet;
        stp->rb_voice      = RingBufAdd(stp->rb_voice, stp->rb_num);

        RingBufCalcDiff(stp->rb_read, stp->rb_voice, stp->rb_num);
    }

    offset     = stp->offset_sector;
    ret.status = ST_STREAM_PLAYING;
    ret.offset = offset;
    SetStreamRet(stp->id, ret);

    /* Loaded and waiting for StreamPlay(); a pause that arrived first holds it
     * here too. */
    while (!(stp->play_ok && !stp->pause))
    {
        if (stp->stop)
            VoiceThreadExit(stp);

        SleepThread();
    }

    if (stp->stop)
        VoiceThreadExit(stp);

    /* Drain any interrupt left over from a previous stream on this core. */
    while (PollSema(sema_Voice_array[stp->irq_core]) != KE_SEMA_ZERO)
        ;

    for (i = 0; i < stp->nchannel; i++)
    {
        CpuSuspendIntr(&oldstat);
        sceSdSetAddr(SD_VA_LSAX | stp->core[i] | (stp->voice[i] << 1),
                     stp->spu_packet[i][toggle]);
        CpuResumeIntr(oldstat);
    }

    for (i = 0; i < stp->nchannel; i++)
    {
        printf("id[%d]>>> channel[%d] core[%d] voice[%d] addr = [%x] ",
               stp->id, i, stp->core[i], stp->voice[i], stp->spu_packet[i][toggle]);

        MyOnVoice(stp->core[i], stp->voice[i], stp->spu_packet[i][toggle]);
    }
    printf("\n");

    for (;;)
    {
        /* An empty loop: whatever it used to do is gone from this build, but
         * the compiler still counts it out.  Left as found. */
        for (i = 0; i < stp->nchannel; i++)
            ;

        /* Ask the SPU to interrupt when playback crosses into the buffer we
         * are about to refill.  That interrupt is the clock for this loop. */
        sceSdSetAddr(SD_A_IRQA | stp->irq_core, stp->spu_packet[0][toggle ^ 1]);

        CpuSuspendIntr(&oldstat);
        sceSdSetCoreAttr(stp->irq_core | SD_C_IRQ_ENABLE, 1);
        CpuResumeIntr(oldstat);

        if (stp->stop)
            break;

        if (PollSema(sema_Voice_array[stp->irq_core]) == KE_SEMA_ZERO)
        {
            WaitSema(sema_Voice_array[stp->irq_core]);
        }
        else
        {
            /* Already signalled: the SPU reached the buffer before we finished
             * filling it, so what is playing now is stale. */
            printf("======================================\n");
            printf("======================================\n");
            printf("======================================\n");
            printf("======================================\n");
            printf("=========Trans Is Not in Time=========\n");
            printf("======================================\n");
            printf("======================================\n");
            printf("======================================\n");
            printf("======================================\n");
            printf("======================================\n");
        }

        while (stp->pause)
        {
            if (stp->stop)
                goto voice_off;

            SleepThread();
        }

        if (stp->stop)
            break;

        if (stp->loop && !stp->just_loop && stp->loop_end_block <= stp->block_offset)
        {
            /* Mid-packet loop.  Swing every voice onto the preloaded loop
             * packet, at the block the loop actually starts on. */
            for (i = 0; i < stp->nchannel; i++)
            {
                CpuSuspendIntr(&oldstat);
                sceSdSetAddr(SD_VA_LSAX | stp->core[i] | (stp->voice[i] << 1),
                             stp->spu_loop_packet[i]
                             + stp->loop_start_fraction * SPU_BLOCK_SIZE);
                CpuResumeIntr(oldstat);

                printf("LOOP packet_adrs = %x adrs = %x loop_start_fraction = %x\n",
                       stp->spu_loop_packet[i],
                       stp->spu_loop_packet[i]
                       + stp->loop_start_fraction * SPU_BLOCK_SIZE,
                       stp->loop_start_fraction);
            }

            /* One more interrupt, at the end of the loop packet's first half,
             * to time the swing back onto the ordinary buffers. */
            sceSdSetAddr(SD_A_IRQA | stp->irq_core,
                         stp->spu_loop_packet[stp->irq_core] + stp->interleave_byte);

            CpuSuspendIntr(&oldstat);
            sceSdSetCoreAttr(stp->irq_core | SD_C_IRQ_ENABLE, 1);
            CpuResumeIntr(oldstat);

            if (stp->stop)
                break;

            WaitSema(sema_Voice_array[stp->irq_core]);

            toggle = 0;

            /* The loop packet covers the two packets after the loop point, so
             * ordinary playback resumes past them. */
            stp->block_offset = (stp->loop_start_block - stp->loop_start_fraction)
                              + block_in_1packet * 2;

            for (i = 0; i < stp->nchannel; i++)
            {
                CpuSuspendIntr(&oldstat);
                sceSdSetAddr(SD_VA_LSAX | stp->core[i] | (stp->voice[i] << 1),
                             stp->spu_packet[i][0]);
                CpuResumeIntr(oldstat);

                printf("Revive Loop Adrs = %x\n", stp->spu_packet[i][0]);
            }
        }
        else if (last_packet)
        {
            break;
        }

        StreamTransSub(stp, toggle);
        toggle ^= 1;

        stp->block_offset += block_in_1packet;
        stp->rb_voice      = RingBufAdd(stp->rb_voice, stp->rb_num);

        rb_diff = RingBufCalcDiff(stp->rb_read, stp->rb_voice, stp->rb_num);

        offset     = stp->offset_sector;
        ret.status = ST_STREAM_PLAYING;
        ret.offset = offset;
        SetStreamRet(stp->id, ret);

        if (stp->file_end && rb_diff == 0)
            /* Nothing left in the ring and no more coming: one more packet
             * plays out and then this loop ends. */
            last_packet = 1;
        else if (rb_diff < stp->rb_read_diff)
            WakeupThread(stp->read_th_idx);
    }

voice_off:
    for (i = 0; i < stp->nchannel; i++)
        MyOffVoice(stp->core[i], stp->voice[i]);

    VoiceThreadExit(stp);
}

/* --------------------------------------------------------------------------
 *  Transfers into SPU RAM
 * ------------------------------------------------------------------------ */

/* The SPU interrupt handler.  One handler serves both cores; `core_bit` says
 * which fired.  The interrupt is disabled again here because the voice thread
 * re-arms it each time round with a new address. */
static int _intr_SignalSemaSPUAdrs(int core_bit, void *dumy)
{
    if (core_bit & 1)
    {
        sceSdSetCoreAttr(SPU_CORE_1 | SD_C_IRQ_ENABLE, 0);
        iSignalSema(sema_Voice_array[SPU_CORE_1]);
    }

    if (core_bit & 2)
    {
        sceSdSetCoreAttr(SPU_CORE_2 | SD_C_IRQ_ENABLE, 0);
        iSignalSema(sema_Voice_array[SPU_CORE_2]);
    }

    return 0;
}

/* Copies one interleave unit per channel out of the ring buffer's current
 * voice slot into the SPU buffer selected by `toggle`. */
static void StreamTransSub(STREAM_WRK *stp, int toggle)
{
    int i;
    int trans_core;
    uintptr_t now_voice_adrs;

    now_voice_adrs = stp->rb_top + stp->rb_voice * stp->rb_size;

    for (i = 0; i < stp->nchannel; i++)
    {
        trans_core = WaitSpuTransSema();
        sceSdSetTransIntrHandler(trans_core, _intr_SignalTransCore, nullptr);

        while (sceSdVoiceTrans((short)trans_core, 0,
                               (unsigned char *)(now_voice_adrs
                                                 + i * stp->interleave_byte),
                               stp->spu_packet[i][toggle],
                               stp->interleave_byte) < 0)
        {
            printf("wrk[%d] cannot trans core[%d]\n", stp->id, trans_core);
            DelayThread(100);
        }

        WaitSPUTransEnd(trans_core);
    }
}

/* Fills the third SPU buffer with the two packets that straddle a mid-packet
 * loop point, so the voice thread has somewhere to jump to when it gets there.
 * Uses the ring buffer as scratch -- this runs before the reader's first real
 * read, so nothing is lost. */
static void PreloadLoopPacketSub(STREAM_WRK *stp)
{
    int             i;
    int             j;
    int             trans_core;
    uintptr_t       iop_buf;
    SPU_BLOCK_DATA *data;
    int             file_offset;
    int             packet_size;

    file_offset = stp->nchannel
                * (((stp->loop_start_block - stp->loop_start_fraction)
                    * SPU_BLOCK_SIZE) >> SECTOR_SHIFT);
    packet_size = stp->interleave_byte * 2;

    printf("LoopPacketSub() packet_size = %x file_offset = %x\n",
           packet_size, file_offset);

    iopReqRead(stp->start_sector + file_offset,
               stp->nchannel * (packet_size >> SECTOR_SHIFT),
               (void *)stp->rb_top, 0);

    /* Every block in the loop packet is marked plain LOOP: the voices are
     * moved onto and off it by address, not by running off its end. */
    for (data = (SPU_BLOCK_DATA *)stp->rb_top;
         (uintptr_t)data < stp->rb_top + stp->nchannel * packet_size;
         data++)
        data->header.loop = SPU_LOOP;

    iop_buf = stp->rb_top;

    for (j = 0; j < 2; j++)
    {
        for (i = 0; i < stp->nchannel; i++)
        {
            trans_core = WaitSpuTransSema();
            sceSdSetTransIntrHandler(trans_core, _intr_SignalTransCore, nullptr);

            while (sceSdVoiceTrans((short)trans_core, 0, (unsigned char *)iop_buf,
                                   stp->spu_loop_packet[i] + j * stp->interleave_byte,
                                   stp->interleave_byte) < 0)
            {
                printf("wrk[%d] cannot trans\n", stp->id);
                DelayThread(100);
            }

            WaitSPUTransEnd(trans_core);

            iop_buf += stp->interleave_byte;
        }
    }
}

/* --------------------------------------------------------------------------
 *  Block-header rewriting and thread teardown
 * ------------------------------------------------------------------------ */

/* Stamps `loop_flg` on block `id` of every channel's plane within one ring
 * slot.  An SPU_END marker is never overwritten -- that one really is the end
 * of the sample. */
void SetStreamLoopFlgSub(STREAM_WRK *stp, SPU_BLOCK_DATA *data, int id, int loop_flg)
{
    int i;

    for (i = 0; i < stp->nchannel; i++)
    {
        if (data[id].header.loop != SPU_END)
            data[id].header.loop = loop_flg;

        data += stp->interleave_byte >> SPU_BLOCK_SHIFT;
    }
}

/* Both exits do the same thing from opposite ends: raise your own bit, and if
 * the other thread has already raised its own, do the release. */
void VoiceThreadExit(STREAM_WRK *stp)
{
    stp->voice_end = 1;
    printf("PRESTREAM VOICE END RELEASE %d\n", stp->id);

    if (stp->read_end)
    {
        printf("STREAM VOICE END RELEASE %d\n", stp->id);
        StreamReleaseSub(stp);
    }

    ExitThread();
}

void ReadThreadExit(STREAM_WRK *stp)
{
    stp->read_end = 1;
    printf("PRE STREAM READ END RELEASE %d\n", stp->id);

    if (stp->voice_end)
    {
        printf("STREAM READ END RELEASE %d\n", stp->id);
        StreamReleaseSub(stp);
    }

    ExitThread();
}
