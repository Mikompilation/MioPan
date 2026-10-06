/* ==========================================================================
 *  system/iop/iop_load_stream.c
 *
 *  The file-streaming RPC service, RPC number 4.  Three calls -- start, read,
 *  stop -- over a single STM_IOP_WRK, so exactly one file can be streamed at
 *  a time.  Everything below the wrapper is iop_ring_buf.c: START lays out a
 *  ring and spawns its reader, READ drains it to the EE, STOP asks the reader
 *  to finish and tears it down.
 *
 *  The reply buffer is a separate, 16-byte-aligned window into stm_ret_f
 *  rather than the receive buffer, because the SIF DMAs the reply back while
 *  the request is still resident.
 *
 *  Reconstructed from iopsys.irx (Feb 6 2004 prototype, SLES_523.84).
 *  No source line numbers are available for this module, so there are no
 *  trailing ROM-line annotations.
 * ======================================================================== */

#include "iop_load_stream.h"

#include <stdio.h>                  /* printf                         */
#include <sifcmd.h>                 /* sceSifSetRpcQueue / RegisterRpc */
#include <thbase.h>                 /* CreateThread / StartThread      */

#include "iop_ring_buf.h"
#include "utility2i.h"              /* GetAlignUp */

/* 0x02000000 is TH_C. */
#define LOAD_STM_TH_ATTR      0x02000000
#define LOAD_STM_TH_STACK     0x500

#define RPC_FILE_STREAM       4

static STM_IOP_WRK load_stm_wrk;                                             /* bss 1a20 */
static char       *stm_ret_f16;                                              /* bss 1b50 */
static char        iop_receive_buffer_s[336];                                /* bss 1b58 */
static char        stm_ret_f[80];                                            /* bss 1ca8 */

static void  iopFileLoadStmLoop(void);
static void *iopCommandLoadStm(unsigned int command, void *data, int size);
static void  CdvdStmStartSub(STM_IOP_WRK *rim, strSTM_START *req_new);
static void  CdvdStmStopSub(STM_IOP_WRK *rim);
static int   CdvdStmRead(STM_IOP_WRK *rim, int read_byte, uintptr_t ee_buf);

void CreateRPCLoadStmThread(void)
{
    /* PORT: called inline rather than started as a thread -- see start() in
     * iop.c.  iopFileLoadStmLoop()'s only blocking call was sceSifRpcLoop(). */
    iopFileLoadStmLoop();
}

static void iopFileLoadStmLoop(void)
{
    sceSifQueueData qdata;
    sceSifServeData sdata;

    sceSifSetRpcQueue(&qdata, GetThreadId());
    sceSifRegisterRpc(&sdata, RPC_FILE_STREAM, iopCommandLoadStm,
                      (void *)GetAlignUp((uintptr_t)iop_receive_buffer_s, 4),
                      0, 0, &qdata);

    /* Both windows are pushed up to a 16-byte boundary; the SIF will not DMA
     * to a misaligned address. */
    stm_ret_f16 = (char *)GetAlignUp((uintptr_t)stm_ret_f, 4);

    printf("FileStreamRpc iop_receive_buffer = %x\n", iop_receive_buffer_s);
    printf("(int)stm_ret_f16 = 0x%x\n", stm_ret_f16);

    sceSifRpcLoop(&qdata);
}

static void *iopCommandLoadStm(unsigned int command, void *data, int size)
{
    STM_IOP_WRK *rim = &load_stm_wrk;
    strSTM_READ *req;

    if (command == REQ_STM_READ)
    {
        /* PORT: read as the struct rather than as two ints.  The ROM does the
         * latter because an EE pointer is one word there; here `ee_buf` is
         * eight bytes and taking word 1 of the payload would hand the transfer
         * the low half of a host pointer. */
        req = (strSTM_READ *)data;
        /* How much actually moved goes back as the reply. */
        *(int *)stm_ret_f16 = CdvdStmRead(rim, req->read_size,
                                          (uintptr_t)req->ee_buf);
    }
    else if (command == REQ_STM_START)
    {
        CdvdStmStartSub(rim, (strSTM_START *)data);
    }
    else if (command == REQ_STM_STOP)
    {
        CdvdStmStopSub(rim);
    }

    return stm_ret_f16;
}

static void CdvdStmStopSub(STM_IOP_WRK *rim)
{
    /* Waits for the reader to acknowledge before freeing anything under it. */
    ReleaseRingBufSubWait(&rim->rb_wrk, rim->read_th);
}

static void CdvdStmStartSub(STM_IOP_WRK *rim, strSTM_START *req_new)
{
    rim->rb_wrk.ld = req_new->ld;

    rim->read_th = InitRingBufSub(&rim->rb_wrk, PRI_LOAD_STREAM_TRANS);
    StartThread(rim->read_th, 0);
}

static int CdvdStmRead(STM_IOP_WRK *rim, int read_byte, uintptr_t ee_buf)
{
    return RingBufTransEE(&rim->rb_wrk, read_byte, ee_buf);
}
