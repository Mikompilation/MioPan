/* ==========================================================================
 *  system/mc/prg/mc_del_all_file.h
 *
 *  Empty a card directory (mc_del_all_file.o, .text 0x1dfd38).
 *
 *  Lists the directory and deletes every entry from index 2 onwards, so it
 *  clears files this build knows nothing about too -- which is what makes the
 *  "replace a foreign or damaged save directory" path work.
 *
 *  Reconstructed from the Feb 6 2004 prototype (SLES_523.84).
 * ======================================================================== */

#ifndef _SYSTEM_MC_PRG_MC_DEL_ALL_FILE_H
#define _SYSTEM_MC_PRG_MC_DEL_ALL_FILE_H

void MemoryCardAllFileDelInit(int port, int slot, int dir_label);  /* 0x1dfd38 */
int  MemoryCardAllFileDelMain(void);                              /* 0x1dfd60 */

#endif /* _SYSTEM_MC_PRG_MC_DEL_ALL_FILE_H */
