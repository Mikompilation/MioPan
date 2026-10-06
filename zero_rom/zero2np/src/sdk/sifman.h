/* ==========================================================================
 *  sifman.h  (SIF DMA — IOP half — PC-port shim)
 *
 *  On the PS2 these moved bytes across the bus between IOP RAM and EE RAM.
 *  Here both live in the same address space, so a "DMA" is a memcpy and it has
 *  always already completed by the time anyone asks.
 * ======================================================================== */

#ifndef _SIFMAN_H
#define _SIFMAN_H

#include "scetypes.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct
{
    void *data;                     /* source, in IOP RAM  */
    void *addr;                     /* destination, in EE RAM */
    int   size;
    int   mode;
} sceSifDmaData;

/* Negative once the transfer has landed -- callers spin on
 * `while (sceSifDmaStat(id) >= 0);`, so this always reports done. */
int sceSifDmaStat(u_int id);

/* The EE half of the same transport, going the other way: `data` is an EE
 * pointer and `addr` an IOP address.  audiodec.c's sendToIOP() is the only
 * caller in the tree.  Implemented in sif.cpp rather than iop_sif.cpp because
 * it runs on the EE side; the header is shared because the PS2 SDK spells both
 * halves the same way.  Returns a transfer id for sceSifDmaStat(). */
u_int sceSifSetDma(sceSifDmaData *sdd, int len);

/* Performs the transfers, then calls `func(param)`.  MyTransEEWait() relies on
 * that callback firing: it parks on a semaphore that _intr_SifSetDma() signals,
 * and would deadlock if the completion never arrived. */
u_int sceSifSetDmaIntr(sceSifDmaData *sdd, int len, void *func, void *param);

#ifdef __cplusplus
}
#endif

#endif /* _SIFMAN_H */
