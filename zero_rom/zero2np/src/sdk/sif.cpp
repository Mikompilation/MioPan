/* ==========================================================================
 *  sif.cpp  (SCE SIF core -- PC-port shim)
 * ======================================================================== */

#include "sif.h"

#include "sifman.h"
#include "iop_host.h"

#include <stdlib.h>
#include <string.h>
#include <stdint.h>

extern "C" {

void *sceSifAllocSysMemory(int mode, u_int size, void *addr)
{
    return NULL;

    (void)mode;
    if (addr != 0)
    {
        return addr;
    }

    return malloc(size);
}

/* --------------------------------------------------------------------------
 *  SIF DMA, EE -> IOP
 *
 *  On hardware this pushed EE memory across the bus into IOP RAM.  Here the
 *  two live in one address space, so a landed transfer is a memcpy -- but only
 *  when the destination is memory this process actually owns.
 *
 *  It usually is not.  sceSifAllocIopHeap() (sifdev.cpp) hands out addresses
 *  from a bump cursor inside the IOP's own 2 MB map -- 0x10000 upward -- which
 *  is exactly what the ROM expects and exactly what nothing here has mapped.
 *  movie.c's iopalloc() is the whole supply and audiodec.c's rings are all it
 *  feeds, so this is the common case, not the exception.  Copying there would
 *  fault in code that has nothing to do with sound, so the transfer is dropped
 *  and named instead -- and the completion still reports done, because
 *  sendToIOP() spins on sceSifDmaStat() and would otherwise never come back.
 *
 * ------------------------------------------------------------------------ */
u_int sceSifSetDma(sceSifDmaData *sdd, int len)
{
    if (sdd == NULL)
        return 1;

    for (int i = 0; i < len; i++)
    {
        void *dst = sdd[i].addr;

        if (sdd[i].data == NULL || dst == NULL || sdd[i].size <= 0)
            continue;

        if (MioPan_IopMemIsBareOffset(dst))
        {
            static int reported;

            if (reported < 8)
            {
                reported++;
                printf("sceSifSetDma: dst=%p size=%#x is not mapped IOP memory"
                       " -- transfer dropped\n", dst, (u_int)sdd[i].size);
            }

            continue;
        }

        memcpy(dst, sdd[i].data, (size_t)sdd[i].size);
    }

    return 1;                       /* any non-zero id; see sceSifDmaStat */
}

}
