/* ==========================================================================
 *  system/iop/iop_load.c
 *
 *  The file-load RPC service, RPC number 3 -- the IOP end of the EE's
 *  fileload.c.  A request names a file and a destination; the ring buffer
 *  pulls it off the disc and one of two transfer loops empties it:
 *
 *    - thTransMem() for the plain cases, straight to EE memory over SIF DMA
 *      or straight into SPU RAM through sceSdVoiceTrans();
 *    - thTransMemDecode() for compressed files, which cannot simply be
 *      copied: the decompressor runs on the EE, so the IOP has to feed it one
 *      division at a time and wait for it between blocks.
 *
 *  That handshake is what the bare sceSifSendCmd()/sceSifDmaStat() pairs are.
 *  The packet carries no payload -- `sch` is a doorbell, and the EE decodes
 *  the block the IOP has just written.
 *
 *  Cancellation is cooperative: ClearLoadReq() raises load_clear_flg and both
 *  loops check it, so a load in progress unwinds at the next block boundary
 *  and reports how far it got.
 *
 *  Reconstructed from iopsys.irx (Feb 6 2004 prototype, SLES_523.84).
 *  No source line numbers are available for this module, so there are no
 *  trailing ROM-line annotations.
 * ======================================================================== */

#include "iop_load.h"

#include <stdio.h>                  /* printf                          */
#include <intrman.h>                /* CpuSuspendIntr / CpuResumeIntr  */
#include <libsd.h>                  /* sceSdVoiceTrans                 */
#include <sifcmd.h>                 /* sceSifSendCmd / RegisterRpc     */
#include <sifman.h>                 /* sceSifDmaStat                   */
#include <sysclib.h>                /* memcpy                          */
#include <sysmem.h>                 /* AllocSysMemory / FreeSysMemory  */
#include <thbase.h>                 /* CreateThread / DelayThread      */
#include <thsemap.h>                /* WaitSema / SignalSema           */

#include "iop.h"                    /* MyTransEEWait, WaitSpuTransSema */
#include "iop_ring_buf.h"
#include "utility2i.h"              /* GetAlignUp, RingBufAdd, PrintAssertReal */

/* 0x02000000 is TH_C. */
#define LOAD_TH_ATTR        0x02000000
#define LOAD_TH_STACK       0x500

#define RPC_FILE_LOAD       3

/* One 4 KB scratch page is enough for any file's division table. */
#define ENCODE_DIV_BUF_SIZE 0x1000

static LOAD_IOP_WRK   load_iop_wrk;                                          /* bss 1660 */
static int            load_clear_flg;                                        /* bss 1794 */
static int            read_th_idx;                                           /* bss 1798 */
static sceSifCmdHdr   sch;                                                   /* bss 17a0 */
static sceSifCmdData  cmdbuffer[16];                                         /* bss 17b0 */
static char          *iop_ret_f16;                                           /* bss 1870 */
static char           iop_receive_buffer_f[336];                             /* bss 1878 */
static char           iop_ret_f[80];                                         /* bss 19c8 */

static int   thTransMemDecode(LOAD_IOP_WRK *rim, ENCODE_DIV_SECTION *encode_div_tbl);
static int   thTransMem(LOAD_IOP_WRK *rim, FILE_LOAD_TYPE type);
static void  iopFileLoadLoop(void);
static void *iopCommandLoad(unsigned int command, void *data, int size);

void ClearLoadReq(void)
{
    printf("load_clear_flg on\n");
    load_clear_flg = 1;
}

static int thTransMemDecode(LOAD_IOP_WRK *rim, ENCODE_DIV_SECTION *encode_div_tbl)
{
    int        block;
    CMP_HEADER header;
    int        last2block_ee_adrs;
    int        id;
    int        align_size;
    int        ee_adrs;
    int        last_2block;

    /* The first slot holds the header and the division table. */
    WaitSema(rim->wrk.read_dat_sema);

    header = *(CMP_HEADER *)rim->wrk.now_adrs;

    memcpy(encode_div_tbl, (void *)(rim->wrk.now_adrs + sizeof(CMP_HEADER)),
           header.div_num * sizeof(ENCODE_DIV_SECTION));

    /* The header has to fit in one slot, or the table would be split across a
     * boundary the copy above does not handle. */
    if (rim->wrk.ld.one_buf_size < header.data_offset)
        PrintAssertReal("thTransMemDecode() header size is too large");

    MyTransEEWait(rim->wrk.now_adrs, rim->tmp_ee_adrs, header.data_offset);

    rim->wrk.trans_offset  = 0;
    rim->wrk.now_adrs     += header.data_offset;

    /* The last two compressed divisions are staged in the EE scratch area
     * just past the header, because the decode runs in place at `adrs` and
     * would otherwise overwrite them before it read them. */
    last2block_ee_adrs = GetAlignUp(rim->tmp_ee_adrs + header.data_offset, 6);

    id = sceSifSendCmd(0, &sch, 0x10, 0, 0, 0);
    while (sceSifDmaStat(id) >= 0)
        ;

    for (block = 0; block < header.div_num; block++)
    {
        align_size = GetAlignUp(encode_div_tbl[block].size, 4);

        last_2block = block - (header.div_num - 2);
        if (last_2block < 0)
            /* +1 leaves the first division's worth of room at the front for
             * the decoded output to grow into. */
            ee_adrs = rim->adrs + (block + 1) * header.div_size;
        else
            ee_adrs = last2block_ee_adrs + last_2block * header.div_size;

        if (load_clear_flg != 0)
            return 0;

        RingBufTransEE(&rim->wrk, align_size, ee_adrs);

        /* Ring the doorbell and wait for the EE to finish this block before
         * writing the next one. */
        id = sceSifSendCmd(0, &sch, 0x10, 0, 0, 0);
        printf("block = %d\n", block);
        while (sceSifDmaStat(id) >= 0)
            ;
    }

    return 0;
}

static int thTransMem(LOAD_IOP_WRK *rim, FILE_LOAD_TYPE type)
{
    unsigned int     iop_buf;
    int              size;
    int              offset;
    int              end_flg;
    int              remain_byte;
    int              trans_idx;
    LOAD_DEF_STRUCT *pld = &rim->wrk.ld;
    int              trans_spu_core;

    offset    = 0;
    end_flg   = 0;
    trans_idx = 0;

    do
    {
        WaitSema(rim->wrk.read_dat_sema);

        /* Checked after the wait, so a cancel that lands while this is parked
         * still unwinds; the caller gets the count so far. */
        if (load_clear_flg != 0)
            return offset;

        size        = pld->one_buf_size;
        iop_buf     = rim->wrk.ring_buf_top + trans_idx * size;
        remain_byte = pld->size - offset;

        /* The last slot is short.  Rounding the transfer up to a word keeps
         * both destinations happy -- sceSdVoiceTrans() will not take an
         * unaligned length -- and the few bytes past the end are ignored. */
        if (remain_byte <= size)
        {
            end_flg = 1;
            size    = GetAlignUp(remain_byte, 4);
        }

        if (type == FILE_LOAD_TYPE_EE)
        {
            MyTransEEWait(iop_buf, rim->adrs + offset, size);
        }
        else if (type == FILE_LOAD_TYPE_SPU)
        {
            /* SPU transfers go through whichever core is free, and the
             * completion interrupt releases it again. */
            trans_spu_core = WaitSpuTransSema();
            sceSdSetTransIntrHandler(trans_spu_core, _intr_SignalTransCore, 0);

            while (sceSdVoiceTrans((short)trans_spu_core, 0, (unsigned char *)iop_buf,
                                   rim->adrs + offset, size) < 0)
            {
                printf("cannot trans core[%d]\n", trans_spu_core);
                DelayThread(100);
            }

            WaitSPUTransEnd(trans_spu_core);
        }
        else
        {
            PrintAssertReal("thLoadEESPU() Load Type Is Illegal");
        }

        offset   += size;
        trans_idx = RingBufAdd(trans_idx, pld->ring_buf_num);
        SignalSema(rim->wrk.ring_buf_sema);
    } while (!end_flg);

    return offset;
}

void CreateRPCLoadThread(void)
{
    /* PORT: called inline rather than started as a thread -- see start() in
     * iop.c.  iopFileLoadLoop()'s only blocking call was sceSifRpcLoop(). */
    PrintIOPMem("after load init");

    iopFileLoadLoop();
}

static void iopFileLoadLoop(void)
{
    sceSifQueueData qdata;
    sceSifServeData sdata;
    int             oldisEI;

    /* This service owns the SIF command buffer as well as its RPC queue --
     * thTransMemDecode()'s per-block doorbell goes out through it. */
    sceSifInitCmd();

    CpuSuspendIntr(&oldisEI);
    sceSifSetCmdBuffer(cmdbuffer, 16);
    CpuResumeIntr(oldisEI);

    sceSifSetRpcQueue(&qdata, GetThreadId());
    sceSifRegisterRpc(&sdata, RPC_FILE_LOAD, iopCommandLoad,
                      (void *)GetAlignUp((uintptr_t)iop_receive_buffer_f, 4),
                      0, 0, &qdata);

    printf("FileloadRpc iop_receive_buffer = %x\n", iop_receive_buffer_f);

    iop_ret_f16 = (char *)GetAlignUp((uintptr_t)iop_ret_f, 4);

    sceSifRpcLoop(&qdata);
}

static void *iopCommandLoad(unsigned int command, void *data, int size)
{
    FILE_LOAD_RET *ret = (FILE_LOAD_RET *)iop_ret_f16;
    LOAD_REQ_NEW  *req_new = (LOAD_REQ_NEW *)data;
    LOAD_IOP_WRK  *rim = &load_iop_wrk;
    int            trans_size = 0;
    void          *encode_div_buf;
    int            oldstat;

    load_clear_flg = 0;

    rim->wrk.ld = req_new->ld;
    rim->adrs   = req_new->adrs;

    read_th_idx = InitRingBufSub(&rim->wrk, PRI_LOAD_IOP);

    if (req_new->type < FILE_LOAD_TYPE_DECODE_EE)
    {
        StartThread(read_th_idx, 0);
        trans_size = thTransMem(rim, req_new->type);
    }
    else if (req_new->type == FILE_LOAD_TYPE_DECODE_EE)
    {
        rim->tmp_ee_adrs = req_new->tmp_ee_adrs;

        CpuSuspendIntr(&oldstat);
        encode_div_buf = AllocSysMemory(0, ENCODE_DIV_BUF_SIZE, 0);
        CpuResumeIntr(oldstat);

        StartThread(read_th_idx, 0);
        thTransMemDecode(rim, (ENCODE_DIV_SECTION *)encode_div_buf);

        CpuSuspendIntr(&oldstat);
        FreeSysMemory(encode_div_buf);
        CpuResumeIntr(oldstat);
    }

    /* Not the Wait form: both transfer loops only return once the reader has
     * finished or been cancelled. */
    ReleaseRingBufSub(&rim->wrk, read_th_idx);

    if (load_clear_flg == 0)
    {
        ret->cancel_flg = 0;
    }
    else
    {
        printf("cancel trans size = %x\n", trans_size);
        ret->read_size  = trans_size;
        ret->cancel_flg = 1;
    }

    return ret;
}
