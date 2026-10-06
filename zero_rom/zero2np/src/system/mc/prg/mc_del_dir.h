/* ==========================================================================
 *  system/mc/prg/mc_del_dir.h
 *
 *  Delete a whole card directory (mc_del_dir.o, .text 0x1dffd0).
 *
 *  Empty it first, then delete the directory itself -- the card will not remove
 *  a non-empty one.
 *
 *  Reconstructed from the Feb 6 2004 prototype (SLES_523.84).
 * ======================================================================== */

#ifndef _SYSTEM_MC_PRG_MC_DEL_DIR_H
#define _SYSTEM_MC_PRG_MC_DEL_DIR_H

void MemoryCardDirDelInit(int port, int slot, int dir_label);   /* 0x1dffd0 */
int  MemoryCardDirDelMain(void);                               /* 0x1dfff0 */

#endif /* _SYSTEM_MC_PRG_MC_DEL_DIR_H */
