/* ==========================================================================
 *  iop_sif.cpp  (SIF command / DMA — IOP half — PC-port implementation)
 *
 *  There is no bus here.  The EE and IOP halves of the reconstruction are one
 *  process sharing one address space, which collapses both SIF services:
 *
 *    - RPC becomes a table lookup and a direct call.  sceSifRegisterRpc()
 *      records (rpc number -> handler); the EE's sceSifCallRpc() finds it
 *      through MioPan_IopRpcDispatch() and runs the handler on the caller's
 *      own thread.  sceSifRpcLoop() therefore has nothing to serve and returns
 *      at once -- which is why the four service "threads" are not threads at
 *      all here: their creators call them inline and they run to completion.
 *
 *    - DMA becomes memcpy.  A transfer has always completed by the time anyone
 *      asks about it.
 *
 *  What is deliberately preserved is the completion callback: MyTransEEWait()
 *  parks on a semaphore that sceSifSetDmaIntr()'s handler signals, so the
 *  handler has to fire or that function deadlocks.
 *
 *  Because the handler runs on the EE's thread it is IOP code executing
 *  concurrently with the IOP's own threads, which the ROM never allowed for.
 *  MioPan_IopRpcDispatch() therefore runs it under the IOP big kernel lock --
 *  see the note in iop_host.h.
 * ======================================================================== */

#include "sifcmd.h"
#include "sifman.h"
#include "iop_host.h"               /* the IOP big kernel lock */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* --------------------------------------------------------------------------
 *  RPC registry
 * ------------------------------------------------------------------------ */

#define IOP_MAX_RPC 8

typedef struct
{
    int              used;
    u_int            number;        /* the RPC number the EE binds to */
    sceSifRpcFunc    func;
    void            *buff;          /* the service's receive scratch  */
    sceSifQueueData *queue;
    int              ready;         /* its thread reached sceSifRpcLoop */
} IopRpcBinding;

static IopRpcBinding iop_rpc[IOP_MAX_RPC];

/* sceSifSetRpcQueue() runs first and carries the thread id, but nothing about
 * the binding; the queue is only a handle for sceSifRegisterRpc() to attach to
 * and for sceSifRpcLoop() to be handed back, so it is recorded and ignored. */
void sceSifSetRpcQueue(sceSifQueueData *q, int tid)
{
    if (q == NULL)
        return;

    memset(q, 0, sizeof(*q));
    q->key    = tid;
    q->active = 1;
}

void sceSifRegisterRpc(sceSifServeData *sd, u_int command, sceSifRpcFunc func,
                       void *buff, sceSifRpcFunc cfunc, void *cbuff,
                       sceSifQueueData *q)
{
    if (sd != NULL)
    {
        memset(sd, 0, sizeof(*sd));
        sd->command = command;
        sd->func    = func;
        sd->buff    = buff;
        sd->cfunc   = cfunc;
        sd->cbuff   = cbuff;
        sd->base    = q;
    }

    for (int i = 0; i < IOP_MAX_RPC; i++)
    {
        if (!iop_rpc[i].used || iop_rpc[i].number == command)
        {
            iop_rpc[i].used   = 1;
            iop_rpc[i].number = command;
            iop_rpc[i].func   = func;
            iop_rpc[i].buff   = buff;
            iop_rpc[i].queue  = q;
            iop_rpc[i].ready  = 0;
            return;
        }
    }
}

/* The IOP's service threads end here.  On hardware this never returned and was
 * where requests were actually served; here returning is correct -- the EE
 * calls the handler directly from now on.
 *
 * Reaching this, rather than sceSifRegisterRpc(), is what marks a service
 * usable, and the distinction matters: iopRpcLoop() registers RPC 1 and only
 * afterwards waits for the disc and sets pIopRet16.  A request accepted in
 * that window would reach iopCommand() with a null status block. */
void sceSifRpcLoop(sceSifQueueData *q)
{
    for (int i = 0; i < IOP_MAX_RPC; i++)
    {
        if (iop_rpc[i].used && iop_rpc[i].queue == q)
            iop_rpc[i].ready = 1;
    }
}

/* iopsys.irx's module entry.  Compiled as C++ like the rest of the
 * reconstruction, so it is declared here the same way rather than extern "C". */
int start(void);

/* Set MIOPAN_IOP=0 in the environment to leave the IOP unbooted, so every
 * sceSifCallRpc() falls through and the EE behaves as it did before the IOP
 * was wired in.  Useful for telling an IOP-side regression apart from an
 * EE-side one -- turning the IOP on changes what the EE reads back, and that
 * alone moves the EE's sound code onto paths it never took while the status
 * block came back all zeros. */
static int IopEnabled(void)
{
    static int cached = -1;

    if (cached < 0)
    {
        const char *env = getenv("MIOPAN_IOP");
        cached = (env != NULL && env[0] == '0') ? 0 : 1;
        if (!cached)
            printf("iop: disabled by MIOPAN_IOP=0\n");
    }

    return cached;
}

/* Idempotent.  On hardware the IRX loader ran start() when the module was
 * loaded; ee_iop.c's loader path is compiled out on this port
 * (ee_iop_boot_iop is 0), so the first bind brings the IOP up instead.
 *
 * This does not wait for the services to come up.  It does not need to: both
 * of the ROM's bind sites already retry until sceSifBindRpc() reports the
 * service bound, which is exactly the handshake hardware needed too. */
void MioPan_IopBoot(void)
{
    static int booted;

    if (booted || !IopEnabled())
        return;

    booted = 1;

    /* Created before start() and therefore before any IOP thread or RPC
     * dispatch exists, which is what lets everything else treat a null lock as
     * "the IOP is not up". */
    MioPan_IopBklInit();

    /* start() is IOP code and runs under the lock like any other IOP context,
     * even though nothing can contend with it yet. */
    MioPan_IopBklEnter();
    start();
    MioPan_IopBklLeave();
}

void *MioPan_IopRpcDispatch(u_int rpc_number, u_int command, void *data, int size)
{
    for (int i = 0; i < IOP_MAX_RPC; i++)
    {
        if (iop_rpc[i].used && iop_rpc[i].ready &&
            iop_rpc[i].number == rpc_number && iop_rpc[i].func != NULL)
        {
            /* The ROM's handlers read out of the buffer they registered, and
             * the EE hands us its own send buffer.  Copying keeps both sides
             * seeing what they expect. */
            void *arg = data;
            if (iop_rpc[i].buff != NULL && data != NULL && size > 0)
            {
                memcpy(iop_rpc[i].buff, data, (size_t)size);
                arg = iop_rpc[i].buff;
            }

            /* The handler is IOP code running on the EE's thread, so it takes
             * the IOP's lock like any IOP thread would.  Without it iopCommand()
             * mutates STREAM_WRK while StreamVoiceThread() is reading it, and
             * writes IOP_RET_STATUS while ee_iopMain() is copying the struct
             * out -- the ROM assumed one CPU and never guarded either. */
            MioPan_IopBklEnter();
            void *ret = iop_rpc[i].func(command, arg, size);
            MioPan_IopBklLeave();

            return ret;
        }
    }

    return NULL;
}

int MioPan_IopRpcIsReady(u_int rpc_number)
{
    for (int i = 0; i < IOP_MAX_RPC; i++)
    {
        if (iop_rpc[i].used && iop_rpc[i].ready && iop_rpc[i].number == rpc_number)
            return 1;
    }

    return 0;
}

/* --------------------------------------------------------------------------
 *  SIF command packets
 * ------------------------------------------------------------------------ */

void sceSifInitCmd(void)
{
}

void sceSifSetCmdBuffer(sceSifCmdData *db, int size)
{
    (void)db;
    (void)size;
}

/* thTransMemDecode()'s per-block doorbell.  On hardware the EE had an
 * interrupt handler bound to this packet that decompressed the block the IOP
 * had just written, and the IOP waited on sceSifDmaStat() for the handshake.
 *
 * Nothing is bound on this side yet, so the doorbell is dropped: compressed
 * loads (FILE_LOAD_TYPE_DECODE_EE) will transfer but never be decoded.  Plain
 * loads do not come through here. */
u_int sceSifSendCmd(u_int fno, void *pkt, int psize, void *src, void *dst, int size)
{
    (void)fno;
    (void)pkt;
    (void)psize;

    if (src != NULL && dst != NULL && size > 0)
        memcpy(dst, src, (size_t)size);

    return 1;                       /* any non-zero id; see sceSifDmaStat */
}

/* --------------------------------------------------------------------------
 *  SIF DMA
 * ------------------------------------------------------------------------ */

int sceSifDmaStat(u_int id)
{
    (void)id;

    /* Callers spin `while (sceSifDmaStat(id) >= 0);`, so anything negative
     * means "already landed". */
    return -1;
}

u_int sceSifSetDmaIntr(sceSifDmaData *sdd, int len, void *func, void *param)
{
    if (sdd != NULL)
    {
        for (int i = 0; i < len; i++)
        {
            if (sdd[i].data != NULL && sdd[i].addr != NULL && sdd[i].size > 0)
                memcpy(sdd[i].addr, sdd[i].data, (size_t)sdd[i].size);
        }
    }

    /* MyTransEEWait() is parked on the semaphore this signals. */
    if (func != NULL)
        ((void (*)(void *))func)(param);

    return 1;
}
