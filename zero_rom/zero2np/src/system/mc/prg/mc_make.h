/* ==========================================================================
 *  system/mc/prg/mc_make.h
 *
 *  Create a whole card directory (mc_make.o, .text 0x1e0a28).
 *
 *  The top of the make chain: mkdir, then mc_make_all_file.c to fill it.
 *
 *  Reconstructed from the Feb 6 2004 prototype (SLES_523.84).
 * ======================================================================== */

#ifndef _SYSTEM_MC_PRG_MC_MAKE_H
#define _SYSTEM_MC_PRG_MC_MAKE_H

void MemoryCardNewMakeInit(int port, int slot, int dir_label,
                           void *buff_addr, int buff_size);          /* 0x1e0a28 */
int  MemoryCardNewMakeMain(void);                                   /* 0x1e0a78 */

#endif /* _SYSTEM_MC_PRG_MC_MAKE_H */
