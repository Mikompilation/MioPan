/* ==========================================================================
 *  sifcmd.h  (SIF command / RPC server side — IOP half — PC-port shim)
 *
 *  The EE half of this transport is sifrpc.h (sceSifBindRpc / sceSifCallRpc).
 *  This is the side iopsys.irx registers services on.
 *
 *  On the host there is no SIF and no second CPU: sceSifCallRpc() dispatches
 *  straight into the registered handler on the caller's thread.  So
 *  sceSifRegisterRpc() records the binding in a table sceSifCallRpc() looks up,
 *  and sceSifRpcLoop() returns immediately instead of servicing a queue -- the
 *  four IOP service threads exist only to run their setup and then retire.
 * ======================================================================== */

#ifndef _SIFCMD_H
#define _SIFCMD_H

#include "scetypes.h"

#ifdef __cplusplus
extern "C" {
#endif

/* An RPC handler: (command, data, size) -> reply buffer. */
typedef void *(*sceSifRpcFunc)(u_int command, void *data, int size);

typedef struct _sif_queue_data      /* 0x18 */
{
    int                     key;
    int                     active;
    struct _sif_serve_data *link;
    struct _sif_serve_data *start;
    struct _sif_serve_data *end;
    struct _sif_queue_data *next;
} sceSifQueueData;

typedef struct _sif_serve_data      /* 0x48 */
{
    u_int                    command;
    sceSifRpcFunc            func;
    void                    *buff;
    int                      size;
    sceSifRpcFunc            cfunc;
    void                    *cbuff;
    int                      csize;
    struct _sif_client_data *client;
    void                    *paddr;
    u_int                    fno;
    void                    *receive;
    int                      rsize;
    int                      rmode;
    int                      rid;
    struct _sif_serve_data  *link;
    struct _sif_serve_data  *next;
    struct _sif_queue_data  *base;
} sceSifServeData;

/* The bare doorbell packet thTransMemDecode() rings between blocks. */
typedef struct
{
    u_int  psize;
    u_int  dsize;
    void  *dest;
    void  *opt;
    u_int  cid;
} sceSifCmdHdr;

typedef struct
{
    u_int reserved[8];
} sceSifCmdData;

void   sceSifInitCmd(void);
void   sceSifSetCmdBuffer(sceSifCmdData *db, int size);
u_int  sceSifSendCmd(u_int fno, void *pkt, int psize, void *src, void *dst, int size);

/* Also declared by sifrpc.h for the EE side -- same function, one transport. */
void   sceSifInitRpc(u_int mode);

void   sceSifSetRpcQueue(sceSifQueueData *q, int tid);
void   sceSifRegisterRpc(sceSifServeData *sd, u_int command, sceSifRpcFunc func,
                         void *buff, sceSifRpcFunc cfunc, void *cbuff,
                         sceSifQueueData *q);
void   sceSifRpcLoop(sceSifQueueData *q);

/* --------------------------------------------------------------------------
 *  Host bridge
 * ------------------------------------------------------------------------ */

/* Invokes the handler registered for `command` under RPC number `rpc_number`,
 * returning its reply buffer, or NULL if nothing is bound.  sceSifCallRpc()
 * on the EE side goes through this. */
void *MioPan_IopRpcDispatch(u_int rpc_number, u_int command, void *data, int size);

/* True once the service for `rpc_number` has reached sceSifRpcLoop(), i.e. its
 * thread has finished setting itself up.  Registration alone is not enough --
 * see the note on sceSifRpcLoop(). */
int   MioPan_IopRpcIsReady(u_int rpc_number);

/* Runs iopsys.irx's start().  Idempotent; the first sceSifBindRpc() calls it. */
void  MioPan_IopBoot(void);

#ifdef __cplusplus
}
#endif

#endif /* _SIFCMD_H */
