/* ==========================================================================
 *  system/iop/iop.c
 *
 *  iopsys.irx's core -- the module entry and the three threads everything else
 *  in the module hangs off:
 *
 *    - iopRpcLoop()      RPC 1, the main command channel.  The EE fills a
 *                        buffer with tagged commands (system/eeiop/ee_iop.c's
 *                        iopCommandRegister) and calls once a frame;
 *                        iopCommand() walks the queue and answers with the
 *                        IOP_RET_STATUS block the EE reads voice state from.
 *    - iopRpcQueryLoop() RPC 2, the synchronous query channel -- one small
 *                        request and one small reply per call, served by
 *                        iopCommandQuery().  The EE end is ee_iop_q.c.
 *    - iopRead()         the shared disc reader.  It is deliberately not a
 *                        loop: it reads once and exits, and iopReqRead()
 *                        restarts it per request, so there is never more than
 *                        one read in flight.  sema_CD serialises the callers.
 *
 *  Everything that touches the drive goes through MyCdRead(), and everything
 *  that pushes bytes at the EE goes through MyTransEEWait().  The two SPU2
 *  cores are handed out one at a time by WaitSpuTransSema(), which is why
 *  iop_load.c and iop_stream.c can both start a transfer without knowing about
 *  each other.
 *
 *  Reconstructed from iopsys.irx (Feb 6 2004 prototype, SLES_523.84).
 *  No source line numbers are available for this module, so there are no
 *  trailing ROM-line annotations.
 * ======================================================================== */

#include <stdio.h>                  /* printf                             */
#include <intrman.h>                /* EnableIntr / CpuSuspendIntr / ...  */
#include <ioman.h>                  /* lseek                              */
#include <libcdvd.h>                /* sceCd*                             */
#include <libsd.h>                  /* sceSdGetAddr / sceSdSetTransIntr.. */
#include <loadcore.h>               /* ModuleInfo / FlushDcache           */
#include <sifcmd.h>                 /* sceSifInitRpc / RegisterRpc / ...  */
#include <sifman.h>                 /* sceSifSetDmaIntr                   */
#include <sysclib.h>                /* memset / sprintf                   */
#include <sysmem.h>                 /* QueryMaxFreeMemSize / Kprintf      */
#include <thbase.h>                 /* threads                            */
#include <thsemap.h>                /* semaphores                         */

#include "iop.h"

#include "iop_load.h"               /* ClearLoadReq / CreateRPCLoadThread */
#include "iop_load_stream.h"        /* CreateRPCLoadStmThread             */
#include "iop_pcmstream.h"
#include "iop_sb.h"
#include "iop_snd.h"
#include "iop_snd_def.h"
#include "iop_stream.h"
#include "utility2i.h"              /* GetAlignUp                         */

ModuleInfo Module = { "iopsys.irx", 0x0100 };                                /* data 0 */

/* 0x02000000 is TH_C. */
#define IOP_TH_ATTR         0x02000000
#define IOP_TH_STACK        0x500

#define RPC_MAIN            1
#define RPC_QUERY           2

/* SPU2 core 0 and core 1 DMA completion, plus the SPU interrupt itself.  The
 * IRX is loaded with these masked, so nothing arrives until the service thread
 * is actually up. */
#define IOP_IRQ_SPU         0x09
#define IOP_IRQ_DMA_SPU     0x24
#define IOP_IRQ_DMA_SPU2    0x28

/* libsd voice-address entries.  The low bits of an entry are the voice key,
 * (voice_no << 1) | core. */
#define SD_VA_LSAX          0x2140  /* loop start address   */
#define SD_VA_NAX           0x2240  /* current play address */

#define SPU_VOICE_NUM       24

/* How many iopCommand() calls -- frames -- a read may sit unanswered before
 * the drive is declared hung. */
#define CD_ABORT_FRAMES     300
/* After a failed read the drive is asked to retry on its own. */
#define CD_RETRY_TRYCOUNT   0x14
/* ...and after this many failures on our side the read is broken off. */
#define CD_RETRY_MAX        4

static int            sema_SPU_trans2;                                       /* bss 0    */
static char           spu_trans_use[SPU_CORE_NUM];                           /* bss 4    */
static int            spu_trans_end_core[SPU_CORE_NUM];                      /* bss 8    */
static sceCdlFILE     iop_cdl_file;                                          /* bss 10   */
static int            cd_read_mode;                                          /* bss 34   */
static int            sema_CD;                                               /* bss 38   */
static int            sema_Sync_CD;                                          /* bss 3c   */
static int            iop_read_th_idx;                                       /* bss 40   */
static IOP_RET_STATUS *pIopRet16;                                            /* bss 44   */
static char           IopRetBuf[440];                                        /* bss 48   */
static int            CD_Read_flg;                                           /* bss 200  */
static int            CD_Abort_count;                                        /* bss 204  */
static int            sif_dma_sema;                                          /* bss 208  */
static char           iop_receive_buffer[4112];                              /* bss 210  */
static int            read_end_sema;                                         /* bss 1220 */
static IOP_READ_WRK   iop_read_wrk;                                          /* bss 1228 */
static char          *iop_ret_q16;                                           /* bss 1238 */
static char           iop_receive_buffer_q[528];                             /* bss 1240 */
static char           iop_ret_q[528];                                        /* bss 1450 */

static void *iopCommand(unsigned int command, void *data, int size);
static void  iopRpcLoop(void);
static void  iopRead(void);
static void  iopReadOnce(void);         /* PORT: see iopReqRead()          */
static void  iopRpcQueryLoop(void);
static void *iopCommandQuery(unsigned int command, void *data, int size);

/* --------------------------------------------------------------------------
 *  EE transfer
 * ------------------------------------------------------------------------ */

/* sceSifSetDmaIntr()'s completion callback; `data` is the &sif_dma_sema handed
 * to it below. */
void _intr_SifSetDma(void *data)
{
    iSignalSema(*(int *)data);
}

void MyTransEEWait(uintptr_t iop_buf, uintptr_t ee_buf, unsigned int size)
{
    int           old;
    sceSifDmaData dat;

    dat.data = (void *)iop_buf;
    dat.addr = (void *)ee_buf;
    dat.size = size;
    dat.mode = 0;

    /* sif_dma_sema does double duty.  It is created with a count of one, so
     * this first WaitSema() is the lock that serialises callers; the second is
     * the wait for the transfer, released by _intr_SifSetDma() above; and the
     * SignalSema() puts the count back for the next caller. */
    WaitSema(sif_dma_sema);

    FlushDcache();

    CpuSuspendIntr(&old);
    sceSifSetDmaIntr(&dat, 1, _intr_SifSetDma, &sif_dma_sema);
    CpuResumeIntr(old);

    WaitSema(sif_dma_sema);
    SignalSema(sif_dma_sema);
}

/* --------------------------------------------------------------------------
 *  Disc
 * ------------------------------------------------------------------------ */

/* Uncalled anywhere in the module. */
int ReferSemaNowCount(int sema_id)
{
    SemaInfo sinfo;

    ReferSemaStatus(sema_id, &sinfo);

    return sinfo.currentCount;
}

void MyCdRead(unsigned int start_sector, unsigned int n_sector, void *buf)
{
    int        count = 0;
    sceCdRMode crm   = { 0, cd_read_mode, 0, 0 };

    /* Drain any completion left over from a read that was declared hung:
     * iopCommand() signals sema_Sync_CD itself when CD_Abort_count runs out,
     * so the drive's own callback can still arrive afterwards. */
    while (PollSema(sema_Sync_CD) != KE_SEMA_ZERO)
        ;

    sceCdCallback(cdvd_callback);

    for (;;)
    {
        if (sceCdRead(iop_cdl_file.lsn + start_sector, n_sector, buf, &crm) == 0)
        {
            /* The command was not even accepted -- usually the tray.  Wait for
             * a disc rather than counting this as a read failure. */
            printf("*****************************************************\n");
            printf("\t\tDisk Not Ready OR ReadCommand Err\n");
            printf("*****************************************************\n");

            do
                DelayThread(3000);
            while (sceCdDiskReady(SCECdNonblock) != SCECdComplete);
        }
        else
        {
            CD_Read_flg    = 1;
            CD_Abort_count = CD_ABORT_FRAMES;
            WaitSema(sema_Sync_CD);
            CD_Read_flg    = 0;

            if (CD_Abort_count > 0)
            {
                if (sceCdGetError() == SCECdErNO)
                    return;

                count++;
                printf("CD READ ERR[%x] read fail count %d start_sect = %d size_sect = %d buf = %x\n",
                       sceCdGetError(), count, iop_cdl_file.lsn + start_sector,
                       n_sector, buf);

                /* A position error means the sector asked for does not exist,
                 * so retrying cannot help.  Park the thread instead of
                 * spinning on it forever. */
                if (sceCdGetError() == SCECdErIPI)
                    SleepThread();

                if (count >= CD_RETRY_MAX)
                {
                    while (sceCdBreak() == 0)
                        ;

                    printf("*****************************************************\n");
                    printf("\t\tREAD ERR!!  COUNT OVER!! CD_BREAK!!\n");
                    printf("*****************************************************\n");
                }
            }
            else
            {
                /* iopCommand() gave up on us: the completion above was its
                 * SignalSema(), not the drive's. */
                printf("*****************************************************\n");
                printf("\t\tREAD ERR!!  TIME OVER!! CD_BREAK!!\n");
                printf("*****************************************************\n");

                while (sceCdBreak() == 0)
                    ;
            }
        }

        /* Every retry -- including the ones that gave up above -- goes back
         * round with the drive's own retry count raised and the spindle
         * control cleared, whatever cd_read_mode asked for originally. */
        crm.trycount   = CD_RETRY_TRYCOUNT;
        crm.spindlctrl = 0;
    }
}

/* The host-PC read path.  Compiled out in this build, the same way the EE
 * side's is -- all four are empty and MyOpen() answers 0, which is why
 * iopCommandQuery()'s REQ_FILE_SIZE always reports the size of fd 0. */
void MyPcRead(unsigned int offset, unsigned int n_sector, void *buf, char *pname)
{
}

/* Uncalled: every read goes through MyCdRead(), which lets sceCdRead() do its
 * own seeking. */
void MyCdSeek(unsigned int sector)
{
    sceCdSeek(sector);
}

void MyClose(int hndl)
{
}

int MyOpen(char *fname)
{
    return 0;
}

/* --------------------------------------------------------------------------
 *  The main command queue (RPC 1)
 * ------------------------------------------------------------------------ */

/* The EE's queue is a run of { IOP_COMMAND_ENUM, payload } pairs terminated by
 * IOP_COM_END.  The command word is stepped over before the switch, so every
 * case has to step over its own payload as well -- a case that forgets leaves
 * the cursor pointing into its payload and the rest of the frame's queue is
 * read as garbage.
 *
 * ROM BUG: there is no case for REQ_STREAM_ABORT (14), so its 4-byte
 * STREAM_ABORT payload is never skipped and `wrk_id` is read as the next
 * command -- 0 reads as IOP_COM_END and drops the rest of the frame's queue,
 * 1 reads as REQ_IOP_REBOOT.  The EE really does send it: SndStreamMain()'s
 * WAIT_END arm in system/eeiop/snd_stream.c forces one after 600 frames.
 * StreamAbort() exists in iop_stream.c and has no caller anywhere in the
 * module, so the case was simply never wired up.  Cross-noted at the EE send
 * site.  REQ_CDVD_INIT (2) and REQ_SB_INIT (19) are missing too, but neither
 * has a payload and the EE never sends either, so they cost nothing. */
static void *iopCommand(unsigned int command, void *data, int size)
{
    char             *pointer;
    IOP_COMMAND_ENUM *com;
    char              file_name[20];
    int               core;
    int               voice;

    /* The reader thread is parked in MyCdRead()'s WaitSema(sema_Sync_CD).
     * Counting the frames down here rather than in MyCdRead() is what lets a
     * drive that never answers be given up on: releasing it from the outside
     * is the only way out of that wait. */
    if (CD_Read_flg != 0)
    {
        if (CD_Abort_count-- <= 0)
            SignalSema(sema_Sync_CD);
    }

    com = (IOP_COMMAND_ENUM *)data;

    while (*com != IOP_COM_END)
    {
        pointer = (char *)(com + 1);

        switch (*com)
        {
        case IOP_COM_END:
            /* Unreachable -- the loop condition above catches it first. */
            printf("iopCommand() illegal\n");
            break;

        case REQ_IOP_REBOOT:
            /* The EE's first round trip.  Everything below the main queue is
             * created here rather than in start(), so the services only exist
             * once the EE is far enough along to drive them. */
            cd_read_mode = 0;
            CreateRPCQueryThread();
            CreateRPCLoadThread();
            CreateRPCLoadStmThread();
            CreateiopReadTH();
            CD_Read_flg = 0;
            break;

        case REQ_SET_CD_DAT:
            /* The file-table image.  Every later read is relative to this
             * file's LSN, so the whole game data set is one ISO file. */
            sprintf(file_name, "\\%s;1", pointer);
            printf("file_name = %s\n", file_name);
            sceCdSearchFile(&iop_cdl_file, file_name);
            pointer += 16;
            break;

        case REQ_CD_READ_MODE_CHANGE:
            cd_read_mode = ((CD_READ_MODE_CHANGE *)pointer)->mode;
            pointer += sizeof(CD_READ_MODE_CHANGE);
            break;

        case REQ_IOP_SND_INIT:
            iopSndInit((IOP_SND_INIT *)pointer);
            pointer += sizeof(IOP_SND_INIT);
            break;

        case REQ_SET_SND_EFFECT:
            iopSndSetEffect((SET_SND_EFFECT *)pointer);
            pointer += sizeof(SET_SND_EFFECT);
            break;

        case REQ_VOICE_STOP:
            SndVoiceStop((VOICE_STOP *)pointer);
            pointer += sizeof(VOICE_STOP);
            break;

        case REQ_VOICE_LOOP_SET:
            SndVoiceLoopSet((VOICE_LOOP_SET *)pointer);
            pointer += sizeof(VOICE_LOOP_SET);
            break;

        case REQ_STREAM_CREATE:
            StreamCreate();
            break;

        case REQ_STREAM_RELEASE:
            StreamRelease((STREAM_RELEASE *)pointer);
            pointer += sizeof(STREAM_RELEASE);
            break;

        case REQ_STREAM_START:
            StreamStart((STREAM_START *)pointer);
            pointer += sizeof(STREAM_START);
            break;

        case REQ_STREAM_PLAY:
            StreamPlay((STREAM_PLAY *)pointer);
            pointer += sizeof(STREAM_PLAY);
            break;

        /* PORT: the ROM has no case for REQ_STREAM_ABORT (14) at all -- its
         * jump-table slot is the switch's break target -- so the command did
         * nothing AND its 4-byte payload was never skipped, leaving `wrk_id` to
         * be read as the next command id (0 = IOP_COM_END drops the rest of the
         * frame's queue, 1 = REQ_IOP_REBOOT).  StreamAbort() below was written
         * and left with zero references anywhere in the module.
         *
         * It matters because this is the ROM's only recovery path: the EE's
         * SndStreamMain() ST_STREAM_WAIT_END arm sends it after 600 frames when
         * the IOP has not reported the stream released, and without it a slot
         * stuck in WAIT_END is stuck for good -- the next stream then spins on
         * "StreamAutoIsPreload() Wait Other Stream End" and the scene never
         * loads.  snd_stream.c's note on that arm says the fix belongs here.
         *
         * The ROM's StreamAbort() relied on TerminateThread() actually killing
         * the two stream threads, which the host shim cannot do -- it only
         * signals their wake semaphore.  Releasing the stream on that
         * assumption freed the ring buffer and zeroed rb_top under threads that
         * were still running, and the voice thread then handed sceSdVoiceTrans()
         * a bare ring offset; that is what made the IOP crash in a different
         * place every run.  StreamAbort() has been rewritten to escalate the
         * cooperative stop and wait for the threads instead, leaking what they
         * hold if they will not leave.  See the comment on it in iop_stream.c.
         *
         * Still a rescue path, though, not a licence to leave the reason the
         * release was missed unfixed. */
        case REQ_STREAM_ABORT:
            StreamAbort((STREAM_STOP *)pointer);
            pointer += sizeof(STREAM_ABORT);
            break;

        case REQ_STREAM_STOP:
            StreamStop((STREAM_STOP *)pointer);
            pointer += sizeof(STREAM_STOP);
            break;

        case REQ_STREAM_PAUSE:
            StreamPause((STREAM_PAUSE *)pointer);
            pointer += sizeof(STREAM_PAUSE);
            break;

        case REQ_STREAM_RESTART:
            StreamRestart((STREAM_RESTART *)pointer);
            pointer += sizeof(STREAM_RESTART);
            break;

        case REQ_STREAM_SETVOL:
            StreamVolSet((STREAM_SETVOL *)pointer);
            pointer += sizeof(STREAM_SETVOL);
            break;

        case REQ_STREAM_SETPITCH:
            StreamPitchSet((STREAM_SETPITCH *)pointer);
            pointer += sizeof(STREAM_SETPITCH);
            break;

        case REQ_SB_PLAY:
            SoundBufferPlay((SOUND_BUF_PLAY *)pointer);
            pointer += sizeof(SOUND_BUF_PLAY);
            break;

        case REQ_SB_STOP:
            SoundBufferStop((SOUND_BUF_STOP *)pointer);
            pointer += sizeof(SOUND_BUF_STOP);
            break;

        case REQ_SB_PAUSE:
            SoundBufferPause((SOUND_BUF_PAUSE *)pointer);
            pointer += sizeof(SOUND_BUF_PAUSE);
            break;

        case REQ_SB_RESTART:
            SoundBufferRestart((SOUND_BUF_RESTART *)pointer);
            pointer += sizeof(SOUND_BUF_RESTART);
            break;

        case REQ_SB_SETVOL:
            SoundBufferSetVol((SOUND_BUF_SETVOL *)pointer);
            pointer += sizeof(SOUND_BUF_SETVOL);
            break;

        case REQ_SB_SETPITCH:
            SoundBufferSetPitch((SOUND_BUF_SETPITCH *)pointer);
            pointer += sizeof(SOUND_BUF_SETPITCH);
            break;

        case REQ_PCM_STREAMCREATE:
            PCMStreamCreate();
            break;

        case REQ_PCM_STREAMINIT:
            PCMStreamInit((PCM_STREAM_INIT *)pointer);
            pointer += sizeof(PCM_STREAM_INIT);
            break;

        case REQ_PCM_STREAMSTART:
            PCMStreamStart((PCM_STREAM_START *)pointer);
            pointer += sizeof(PCM_STREAM_START);
            break;

        case REQ_PCM_STREAMPLAY:
            PCMStreamPlay((PCM_STREAM_PLAY *)pointer);
            pointer += sizeof(PCM_STREAM_PLAY);
            break;

        case REQ_PCM_STREAMSETVOL:
            PCMStreamVolSet((PCM_STREAM_SETVOL *)pointer);
            pointer += sizeof(PCM_STREAM_SETVOL);
            break;

        case REQ_PCM_STREAMSTOP:
            PCMStreamStop((PCM_STREAM_STOP *)pointer);
            pointer += sizeof(PCM_STREAM_STOP);
            break;

        case REQ_PCM_STREAMPAUSE:
            PCMStreamPause((PCM_STREAM_PAUSE *)pointer);
            pointer += sizeof(PCM_STREAM_PAUSE);
            break;

        case REQ_PCM_STREAMRESTART:
            PCMStreamRestart((PCM_STREAM_RESTART *)pointer);
            pointer += sizeof(PCM_STREAM_RESTART);
            break;
        }

        com = (IOP_COMMAND_ENUM *)pointer;
    }

    /* The reply.  FrameWrkVoices() advances the voice bookkeeping and fills
     * voice_end[]; the loop then samples every voice's play and loop address
     * so the EE can see where each one has got to. */
    FrameWrkVoices();

    for (core = 0; core < SPU_CORE_NUM; core++)
    {
        for (voice = 0; voice < SPU_VOICE_NUM; voice++)
        {
            pIopRet16->mpNowAdrs[core][voice] =
                sceSdGetAddr(SD_VA_NAX | core | (voice << 1));
            pIopRet16->mpLoopAdrs[core][voice] =
                sceSdGetAddr(SD_VA_LSAX | core | (voice << 1));
        }
    }

    return pIopRet16;
}

/* --------------------------------------------------------------------------
 *  Module entry
 * ------------------------------------------------------------------------ */

/* The drive's command-completion callback.  Only a finished read is
 * interesting; the other commands the module issues -- seek and break -- are
 * either waited on differently or not waited on at all. */
void cdvd_callback(int cb_reason)
{
    switch (cb_reason)
    {
    case SCECdFuncRead:
        iSignalSema(sema_Sync_CD);
        break;

    case SCECdFuncSeek:
    case SCECdFuncStandby:
    case SCECdFuncStop:
    case SCECdFuncPause:
        break;

    default:
        Kprintf("Other Ended \n");
        break;
    }
}

int start(void)
{
    SemaParam   spara;
    int         i;
    int         disk_type;

    sceSifInitRpc(0);

    /* Counting semaphore over both SPU2 cores: two transfers may be in flight,
     * and WaitSpuTransSema() decides which core each one gets. */
    spara.attr      = 1;
    spara.option    = 0;
    spara.initCount = SPU_CORE_NUM;
    spara.maxCount  = SPU_CORE_NUM;
    sema_SPU_trans2 = CreateSema(&spara);

    spara.initCount = 0;
    spara.maxCount  = 1;
    for (i = 0; i < SPU_CORE_NUM; i++)
    {
        spu_trans_use[i]      = 0;
        spu_trans_end_core[i] = CreateSema(&spara);
    }

    spara.initCount = 1;
    spara.maxCount  = 1;
    sif_dma_sema = CreateSema(&spara);

    /* PORT: iopRpcLoop() is no longer a thread.  The ROM needed one because
     * sceSifRpcLoop() never returned -- the thread WAS the RPC server.  There
     * is no bus here: sceSifCallRpc() dispatches straight to iopCommand() on
     * the EE's own thread, so sceSifRpcLoop() only marks the binding usable and
     * returns, and the thread would do its setup and immediately die.
     *
     * Calling it inline also closes a start-up race for free.  The ROM
     * registers RPC 1 and only afterwards waits for the disc and sets
     * pIopRet16, so a request accepted in that window reached iopCommand()
     * with a null status block.  Run from start(), everything is in place
     * before start() returns and the EE's bind-retry loop passes first time.
     *
     * The call sits at the tail of start(), where StartThread() used to be, so
     * its ordering against FrameInitVoices() and the disc probe is unchanged. */

    PrintIOPMem("after rpc_main");

    FrameInitVoices();

    disk_type = sceCdGetDiskType();
    switch (disk_type)
    {
    case SCECdIllgalMedia:
        printf("Disk Type= IllgalMedia\n");
        break;
    case SCECdPS2DVD:
        printf("Disk Type= PlayStation2 DVD\n");
        break;
    case SCECdPS2CD:
        printf("Disk Type= PlayStation2 CD\n");
        break;
    case SCECdPS2CDDA:
        printf("Disk Type= PlayStation2 CD with CDDA\n");
        break;
    case SCECdPSCD:
        printf("Disk Type= PlayStation CD\n");
        break;
    case SCECdPSCDDA:
        printf("Disk Type= PlayStation CD with CDDA\n");
        break;
    case SCECdDVDV:
        printf("Disk Type= DVD video\n");
        break;
    case SCECdCDDA:
        printf("Disk Type= CD-DA\n");
        break;
    case SCECdDETCT:
        printf("Working\n");
        break;
    case SCECdNODISC:
        printf("Disk Type= No Disc\n");
        break;
    default:
        printf("Disk Type= OTHER DISK\n");
        break;
    }

    PrintIOPMem("after start");

    iopRpcLoop();                       /* PORT: was StartThread(th, 0) */

    return MODULE_RESIDENT_END;
}

static void iopRpcLoop(void)
{
    sceSifQueueData qdata;
    sceSifServeData sdata;

    printf("iop_receive_buffer main = %x\n", iop_receive_buffer);

    /* Unmasked here rather than in start() so nothing can fire before this
     * thread exists to service it. */
    EnableIntr(IOP_IRQ_DMA_SPU);
    EnableIntr(IOP_IRQ_DMA_SPU2);
    EnableIntr(IOP_IRQ_SPU);

    sceSifInitRpc(0);

    sceSifSetRpcQueue(&qdata, GetThreadId());
    sceSifRegisterRpc(&sdata, RPC_MAIN, iopCommand,
                      (void *)GetAlignUp((uintptr_t)iop_receive_buffer, 4),
                      0, 0, &qdata);

    /* The EE is already waiting on the RPC binding above, so blocking here
     * costs it nothing and gets the "no disc" case out of the way before the
     * first read. */
    while (sceCdDiskReady(SCECdNonblock) != SCECdComplete)
    {
        DelayThread(500000);
        printf("DiskReady DAME\n");
    }
    printf("OK\n");

    pIopRet16 = (IOP_RET_STATUS *)GetAlignUp((uintptr_t)IopRetBuf, 4);
    printf("(int)pIopRet16 = 0x%x\n", pIopRet16);

    sceSifRpcLoop(&qdata);
}

/* --------------------------------------------------------------------------
 *  SPU transfer arbitration
 * ------------------------------------------------------------------------ */

/* Claims a free SPU2 core and returns it.  The semaphore is what blocks when
 * both are busy, so by the time the scan runs one of the two slots is always
 * clear -- the loop cannot fall off the end. */
int WaitSpuTransSema(void)
{
    int i;
    int oldstat;

    WaitSema(sema_SPU_trans2);

    CpuSuspendIntr(&oldstat);
    for (i = 0; i < SPU_CORE_NUM; i++)
    {
        if (spu_trans_use[i] == 0)
        {
            spu_trans_use[i] = 1;
            break;
        }
    }
    CpuResumeIntr(oldstat);

    return i;
}

void SignalSpuTransSema(int core)
{
    spu_trans_use[core] = 0;
    SignalSema(sema_SPU_trans2);
}

/* Transfer-completion handlers, installed with sceSdSetTransIntrHandler() and
 * never called directly.  The plain one only releases the caller's wait; the
 * TransCore form also hands the core back, so a caller that used
 * WaitSpuTransSema() does not have to. */
int _intr_SignalSema(int core_bit, void *sema)
{
    iSignalSema(*(int *)sema);

    return 0;
}

int _intr_SignalTransCore(int core, void *dmy)
{
    spu_trans_use[core] = 0;
    iSignalSema(sema_SPU_trans2);
    iSignalSema(spu_trans_end_core[core]);

    return 0;
}

void WaitSPUTransEnd(int core)
{
    WaitSema(spu_trans_end_core[core]);
}

/* --------------------------------------------------------------------------
 *  The status block the EE reads back
 * ------------------------------------------------------------------------ */

void SetEndVoices(int core, unsigned int end_voice)
{
    pIopRet16->voice_end[core] = end_voice;
}

void SetStreamRet(int wrk_id, IOP_STREAM_RET ret)
{
    pIopRet16->stream_ret[wrk_id] = ret;
}

void SetPCMStreamRet(int wrk_id, IOP_STREAM_RET ret)
{
    pIopRet16->pcm_stream_ret[wrk_id] = ret;
}

void PrintIOPMem(char *name)
{
    int freesize;
    int maxblock;

    maxblock = QueryMaxFreeMemSize();
    freesize = QueryTotalFreeMemSize();

    printf("IOP system memory  0x%x(%d) byte free, Max free block size 0x%x [%s]\n",
           freesize, freesize, maxblock, name);
}

/* --------------------------------------------------------------------------
 *  The shared disc reader
 * ------------------------------------------------------------------------ */

void iopReqRead(unsigned int offset_sector, unsigned int read_sector_num,
                void *buf, char *pname)
{
    /* One reader, one request at a time: sema_CD is held from here until
     * iopRead() has finished with iop_read_wrk. */
    WaitSema(sema_CD);

    iop_read_wrk.offset_sector   = offset_sector;
    iop_read_wrk.read_sector_num = read_sector_num;
    iop_read_wrk.buf             = buf;
    iop_read_wrk.pname           = pname;

    /* Drain first: an aborted read still ends up signalling this. */
    while (PollSema(read_end_sema) != KE_SEMA_ZERO)
        ;

    /* PORT: was StartThread(iop_read_th_idx, 0).  sceCdRead() is a synchronous
     * host read that fires cdvd_callback() -- and so signals sema_Sync_CD --
     * before it returns, so MyCdRead()'s wait is already satisfied on arrival
     * and the reader thread never actually blocks.  It existed only to hide
     * drive latency that does not exist here.
     *
     * Running the read on the caller also removes the respawn race the thread
     * created.  iopReqRead() ignores StartThread()'s return value, so a caller
     * arriving while the previous instance had signalled but not yet reached
     * ExitThread() was told "already running" and then waited on read_end_sema
     * for ever -- and with stp->stop untested there, the stream slot was held
     * for good.  The 10 ms grace period in MioPan_IopStartThread() existed only
     * to narrow that window.
     *
     * The semaphore accounting is unchanged: iopReadOnce() signals
     * read_end_sema and puts sema_CD back, and the WaitSema() below consumes
     * the one it just posted. */
    iopReadOnce();

    WaitSema(read_end_sema);
}

/* The reader thread.  It exits after one read, and iopReqRead() starts it
 * again for the next -- which is also why iop_read_wrk can be a single
 * record.  `pname` is only ever read by the host-PC path, so it goes unused
 * here. */
/* PORT: iopRead()'s body without its ExitThread(), so iopReqRead() can run the
 * read on the calling thread.  ExitThread() cannot be part of it: on this host
 * it marks the CALLING thread exited, and the caller here is a live stream or
 * ring-buffer thread, which would then be relaunched underneath itself. */
static void iopReadOnce(void)
{
    MyCdRead(iop_read_wrk.offset_sector, iop_read_wrk.read_sector_num,
             iop_read_wrk.buf);

    SignalSema(read_end_sema);
    SignalSema(sema_CD);
}

/* Kept as the ROM wrote it, and still what CreateiopReadTH() registers as the
 * thread entry -- but nothing starts that thread any more. */
static void iopRead(void)
{
    iopReadOnce();

    ExitThread();
}

/* --------------------------------------------------------------------------
 *  The query channel (RPC 2)
 * ------------------------------------------------------------------------ */

static void iopRpcQueryLoop(void)
{
    sceSifQueueData qdata;
    sceSifServeData sdata;

    sceSifSetRpcQueue(&qdata, GetThreadId());
    sceSifRegisterRpc(&sdata, RPC_QUERY, iopCommandQuery,
                      (void *)GetAlignUp((uintptr_t)iop_receive_buffer_q, 4),
                      0, 0, &qdata);

    printf("SubRpc iop_receive_buffer = %x\n", iop_receive_buffer_q);

    iop_ret_q16 = (char *)GetAlignUp((uintptr_t)iop_ret_q, 4);

    sceSifRpcLoop(&qdata);
}

/* One command per call, unlike iopCommand()'s queue -- so no payload skipping
 * and no terminator.  The four cases are the EE's QueryFileSize(),
 * ReqQueryLoadCancel(), ReqQuerySPUTransCoreGet() and
 * ReqQuerySPUTransCoreRelease() in ee_iop_q.c. */
static void *iopCommandQuery(unsigned int command, void *data, int size)
{
    void *pointer = iop_ret_q16;
    int   hndl;
    int   core;

    switch (command)
    {
    case REQ_FILE_SIZE:
        /* Shares sema_CD with the reader so a size query cannot cut in on a
         * read in progress.  With the host-PC path compiled out MyOpen()
         * always answers 0, so this reports whatever fd 0 happens to be. */
        WaitSema(sema_CD);

        hndl = MyOpen((char *)data);
        if (hndl < 0)
        {
            *(int *)pointer = -1;
        }
        else
        {
            *(int *)pointer = lseek(hndl, 0, 2);
            MyClose(hndl);
        }

        SignalSema(sema_CD);
        break;

    case REQ_Q_LOAD_CANCEL:
        ClearLoadReq();
        break;

    case REQ_SPU_TRANS_CORE_GET:
        /* The EE wants to drive the transfer itself, so the core is handed
         * over with no completion handler on it. */
        *(int *)pointer = WaitSpuTransSema();
        sceSdSetTransIntrHandler(*(int *)pointer, 0, 0);
        break;

    case REQ_SPU_TRANS_CORE_RELEASE:
        core = *(int *)data;
        /* Clears the transfer status the EE left behind, then puts the
         * module's own completion handler back before releasing the core. */
        sceSdVoiceTransStatus(core, 1);
        sceSdSetTransIntrHandler(core, _intr_SignalTransCore, 0);
        SignalSpuTransSema(core);
        break;
    }

    return pointer;
}

/* --------------------------------------------------------------------------
 *  Thread creation, from REQ_IOP_REBOOT
 * ------------------------------------------------------------------------ */

void CreateRPCQueryThread(void)
{
    /* PORT: called inline rather than started as a thread -- see start().
     * iopRpcQueryLoop()'s only blocking call was sceSifRpcLoop(). */
    PrintIOPMem("after rpc_query");

    iopRpcQueryLoop();
}

void CreateiopReadTH(void)
{
    ThreadParam param;
    SemaParam   spara;

    spara.attr      = 1;
    spara.option    = 0;
    spara.initCount = 0;
    spara.maxCount  = 1;
    read_end_sema = CreateSema(&spara);
    sema_Sync_CD  = CreateSema(&spara);

    /* sema_CD starts free -- it is the reader's mutex, not a completion. */
    spara.initCount = 1;
    sema_CD = CreateSema(&spara);

    param.attr         = IOP_TH_ATTR;
    param.option       = 0;
    param.entry        = (void *)iopRead;
    param.stackSize    = IOP_TH_STACK;
    param.initPriority = PRI_IOP_READ;

    /* Not started here: iopReqRead() starts it once per request. */
    iop_read_th_idx = CreateThread(&param);
}
