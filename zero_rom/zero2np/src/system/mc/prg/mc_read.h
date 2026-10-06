/* ==========================================================================
 *  system/mc/prg/mc_read.h
 *
 *  sceMcRead() wrapper (mc_read.o, .text 0x1e1368).
 *
 *  Reconstructed from the Feb 6 2004 prototype (SLES_523.84).
 * ======================================================================== */

#ifndef _SYSTEM_MC_PRG_MC_READ_H
#define _SYSTEM_MC_PRG_MC_READ_H

void MemoryCardFileReadInit(int fd, void *data_addr, int size);  /* 0x1e1368 */
int  MemoryCardFileReadMain(void);                               /* 0x1e1390 */
int  MemoryCardFileReadReq(int fd, void *data_addr, int size);    /* 0x1e1530 */

#endif /* _SYSTEM_MC_PRG_MC_READ_H */
