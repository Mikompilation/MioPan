/* ==========================================================================
 *  system/mc/prg/mc_load.h
 *
 *  Read a whole card file (mc_load.o, .text 0x1e0880).
 *
 *  A six-step job over the primitives: open, read, close.  Main() returns 1
 *  only after the close, so a caller that sees 1 owns a complete buffer and an
 *  already-released descriptor.
 *
 *  Reconstructed from the Feb 6 2004 prototype (SLES_523.84).
 * ======================================================================== */

#ifndef _SYSTEM_MC_PRG_MC_LOAD_H
#define _SYSTEM_MC_PRG_MC_LOAD_H

/* `size` must be GetMemoryCardDataSize()'s answer for the file: mc_read.c
 * treats a short read as corruption, so a wrong size fails the load. */
void MemoryCardFileLoadInit(int port, int slot, char *name,
                            void *data_addr, int size);              /* 0x1e0880 */

/* 1 when the buffer is filled, 0 while still going, negative on error -- the
 * primitives' codes passed straight through. */
int  MemoryCardFileLoadMain(void);                                  /* 0x1e08f0 */

#endif /* _SYSTEM_MC_PRG_MC_LOAD_H */
