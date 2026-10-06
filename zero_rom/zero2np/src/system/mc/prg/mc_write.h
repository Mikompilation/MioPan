/* ==========================================================================
 *  system/mc/prg/mc_write.h
 *
 *  sceMcWrite() wrapper (mc_write.o, .text 0x1e2db0).
 *
 *  Reconstructed from the Feb 6 2004 prototype (SLES_523.84).
 * ======================================================================== */

#ifndef _SYSTEM_MC_PRG_MC_WRITE_H
#define _SYSTEM_MC_PRG_MC_WRITE_H

void MemoryCardFileWriteInit(int fd, void *data_addr, int size);  /* 0x1e2db0 */
int  MemoryCardFileWriteMain(void);                               /* 0x1e2dd8 */
int  MemoryCardFileWriteReq(int fd, void *data_addr, int size);    /* 0x1e2f60 */

#endif /* _SYSTEM_MC_PRG_MC_WRITE_H */
