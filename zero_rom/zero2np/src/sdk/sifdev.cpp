/* ==========================================================================
 *  sifdev.cpp  (SCE SIF device / directory I/O -- PC-port shim)
 * ======================================================================== */

#include "sifdev.h"

#include "iop_host.h"   /* the IOP arena -- see sceSifAllocIopHeap */

#include <string.h>

extern "C" {

int sceDopen(const char *name)
{
    (void)name;
    return -1;
}

int sceDread(int fd, sce_dirent *buf)
{
    (void)fd;
    if (buf != 0)
    {
        memset(buf, 0, sizeof(*buf));
    }
    return 0;
}

int sceDclose(int fd)
{
    (void)fd;
    return 0;
}

/* ---- IOP bring-up ------------------------------------------------------
 *
 * No IOP exists on the host, so every one of these reports success and does
 * nothing.  The IOP heap in particular is a fiction: sceSifAllocSysMemory()
 * hands back the requested address (or NULL for "anywhere"), because the only
 * consumer -- ee_iop.c's IRX loader -- never dereferences it on this side. */

int sceSifRebootIop(const char *img)
{
    (void)img;
    return 1;
}

int sceSifSyncIop(void)
{
    return 1;
}

void sceSifLoadFileReset(void)
{
}

void sceFsReset(void)
{
}

int sceSifLoadModuleBuffer(void *addr, int arglen, const char *args)
{
    (void)addr;
    (void)arglen;
    (void)args;
    return 0;
}

void sceSifInitIopHeap(void)
{
}

/* Real IOP memory, out of the same arena MioPan_IopAllocSysMemory() serves.
 *
 * This used to hand out addresses from a fictional 2 MB map, on the reasoning
 * that nothing on this side dereferences them -- the buffers are for the IOP's
 * DMA.  That stopped being true when playpss.o and audiodec.o became real
 * reconstructions: movie.c's iopalloc() takes the audio ring from here,
 * sendToIOP() DMAs the demuxed samples into it, and the SPU2 auto-DMA plays out
 * of it.  All three have to be looking at the same bytes.
 *
 * The arena is reserved from 16 MB upward and under 4 GB precisely so an
 * address survives the round trip through `int` that the ROM puts it through
 * (iopalloc() returns one, playPssRsrcs::iopBuff and AudioDec::iopBuff hold
 * one). */
void *sceSifAllocIopHeap(unsigned int size)
{
    /* The low pool, not the main arena.  movie.c's iopalloc() is the only
     * caller, and audiodec.c reads the address it gets back through the ROM's
     * 24-bit IOP mask -- which only works below 16 MB.  See
     * MioPan_IopAllocIopHeap(). */
    return MioPan_IopAllocIopHeap((int)size);
}

int sceSifFreeIopHeap(void *addr)
{
    return MioPan_IopFreeSysMemory(addr);
}

int sceSifLoadIopHeap(const char *name, void *addr)
{
    (void)name;
    (void)addr;
    return 0;
}

/* The PS2's IOP has 2 MB; report it all free so the ROM's banners read
 * sensibly rather than claiming an exhausted heap. */
unsigned int sceSifQueryMaxFreeMemSize(void)
{
    return 0x200000;
}

unsigned int sceSifQueryTotalFreeMemSize(void)
{
    return 0x200000;
}

}
