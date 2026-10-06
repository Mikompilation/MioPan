/* ==========================================================================
 *  sifrpc.cpp  (SCE SIF RPC -- PC-port shim)
 * ======================================================================== */

#include "sifrpc.h"
#include "sifcmd.h"                 /* the IOP half of this transport */

#include <string.h>

extern "C" {

void sceSifInitRpc(unsigned int mode)
{
    (void)mode;
}

/* Nothing is ever in flight: sceSifCallRpc() completes synchronously here. */
int sceSifCheckStatRpc(sceSifRpcData *bd)
{
    (void)bd;
    return 0;
}

/* Binds to a service iopsys.irx registered.  `serve` is left null until that
 * service is actually up, which is what the callers' own retry loops are
 * waiting on -- ee_iopInitSub() and FileLoadInit() both spin
 * `do { bind; ... } while (serve == NULL)`, exactly as they did against real
 * hardware, and that is what covers the IOP's start-up race here. */
int sceSifBindRpc(sceSifClientData *bd, unsigned int command, unsigned int mode)
{
    (void)mode;

    /* The port compiles ee_iop.c's IRX loader out (ee_iop_boot_iop is 0), so
     * this is where the IOP comes up. */
    MioPan_IopBoot();

    if (bd != 0)
    {
        bd->command = command;
        bd->serve   = MioPan_IopRpcIsReady(command)
                    ? (struct _sif_serve_data *)bd
                    : (struct _sif_serve_data *)0;
    }

    return 0;
}

/* One process, so this is a direct call rather than a bus round trip: the
 * handler runs on the caller's own thread and has finished by the time this
 * returns.  `bd->command` is the RPC number bound above; `fno` is the function
 * within it, which the ROM only ever uses as 0 for the queue services. */
int sceSifCallRpc(sceSifClientData *bd, unsigned int fno, unsigned int mode,
                  void *send, int ssize, void *receive, int rsize,
                  sceSifEndFunc func, void *para)
{
    void *ret;

    (void)mode;
    (void)para;

    if (bd == 0 || bd->serve == 0)
    {
        return -1;
    }

    ret = MioPan_IopRpcDispatch(bd->command, fno, send, ssize);

    if (receive != 0 && ret != 0 && rsize > 0)
    {
        memcpy(receive, ret, (size_t)rsize);
    }

    /* Completion is immediate, so the end function fires here rather than from
     * an interrupt.  sceSifCheckStatRpc() already answers "nothing in flight". */
    if (func != 0)
    {
        func();
    }

    return 0;
}

}
