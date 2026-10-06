/* ==========================================================================
 *  sifrpc.h  (PS2 SIF RPC — EE<->IOP remote procedure call — PC-port shim)
 *
 *  Declarations for the SIF RPC client API the reconstruction calls: the
 *  client-data block a caller binds to an IOP service, plus the bind/call
 *  entry points.  Layouts are verbatim from the prototype's debug info
 *  (types.txt); the transport is stubbed/ported under src/sdk as the port
 *  progresses, so these only need to be declared for callers to compile.
 * ======================================================================== */

#ifndef _SIFRPC_H
#define _SIFRPC_H

#include "scetypes.h"

#ifdef __cplusplus
extern "C" {
#endif

/* SIF DMA / RPC transfer descriptor (embedded in the client-data block). */
typedef struct _sif_rpc_data        /* 0x10 */
{
    /* 0x0 */ void        *paddr;
    /* 0x4 */ unsigned int pid;
    /* 0x8 */ int          tid;
    /* 0xc */ unsigned int mode;
} sceSifRpcData;

typedef void (*sceSifEndFunc)();

struct _sif_serve_data;

/* Per-client RPC binding: which IOP service, its send/receive scratch and the
 * completion callback. */
typedef struct _sif_client_data     /* 0x28 */
{
    /* 0x00 */ sceSifRpcData            rpcd;
    /* 0x10 */ unsigned int             command;
    /* 0x14 */ void                    *buff;
    /* 0x18 */ void                    *gp;
    /* 0x1c */ sceSifEndFunc            func;
    /* 0x20 */ void                    *para;
    /* 0x24 */ struct _sif_serve_data  *serve;
} sceSifClientData;

/* sceSifCallRpc() modes.  Only these two are used in the tree: 0 waits for
 * the reply, NOWAIT posts the call and leaves sceSifCheckStatRpc() to report
 * when it has landed. */
#define SIF_RPC_M_NOWAIT    0x01
#define SIF_RPC_M_NOWBDC    0x02

void sceSifInitRpc(unsigned int mode);

/* Bind a client to IOP service `command`; returns < 0 on failure. */
int sceSifBindRpc(sceSifClientData *bd, unsigned int command, unsigned int mode);

/* Non-zero while a call on this descriptor is still in flight. */
int sceSifCheckStatRpc(sceSifRpcData *bd);

/* Invoke function `fno` on the bound service; `func` fires on completion. */
int sceSifCallRpc(sceSifClientData *bd, unsigned int fno, unsigned int mode,
                  void *send, int ssize, void *receive, int rsize,
                  sceSifEndFunc func, void *para);

#ifdef __cplusplus
}
#endif

#endif /* _SIFRPC_H */
