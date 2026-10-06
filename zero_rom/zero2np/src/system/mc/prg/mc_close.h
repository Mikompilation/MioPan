/* ==========================================================================
 *  system/mc/prg/mc_close.h
 *
 *  sceMcClose() wrapper (mc_close.o, .text 0x1dfb20).
 *
 *  Reconstructed from the Feb 6 2004 prototype (SLES_523.84).
 * ======================================================================== */

#ifndef _SYSTEM_MC_PRG_MC_CLOSE_H
#define _SYSTEM_MC_PRG_MC_CLOSE_H

void MemoryCardFileCloseInit(int fd);           /* 0x1dfb20 */
int  MemoryCardFileCloseMain(void);             /* 0x1dfb38 */
int  MemoryCardFileCloseReq(int fd);            /* 0x1dfc70 */

#endif /* _SYSTEM_MC_PRG_MC_CLOSE_H */
