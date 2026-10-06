/* ==========================================================================
 *  system/mc/prg/mc_format.h
 *
 *  sceMcFormat() wrapper (mc_format.o, .text 0x1e0330).
 *
 *  Reconstructed from the Feb 6 2004 prototype (SLES_523.84).
 * ======================================================================== */

#ifndef _SYSTEM_MC_PRG_MC_FORMAT_H
#define _SYSTEM_MC_PRG_MC_FORMAT_H

void MemoryCardFormatInit(int port, int slot);   /* 0x1e0330 */
int  MemoryCardFormatMain(void);                 /* 0x1e0350 */
int  MemoryCardFormatReq(int port, int slot);    /* 0x1e0490 */

#endif /* _SYSTEM_MC_PRG_MC_FORMAT_H */
