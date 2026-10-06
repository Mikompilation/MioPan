/* ==========================================================================
 *  system/mc/prg/mc_save.h
 *
 *  Overwrite an existing card file (mc_save.o, .text 0x1e1548).
 *
 *  mc_load.c's mirror: open / write / close.  It opens mode 3 (read+write) and
 *  not 0x203, so the file must already exist -- creating one is
 *  mc_make_file.c's job, and the game makes every file up front when the
 *  directory is created.
 *
 *  Reconstructed from the Feb 6 2004 prototype (SLES_523.84).
 * ======================================================================== */

#ifndef _SYSTEM_MC_PRG_MC_SAVE_H
#define _SYSTEM_MC_PRG_MC_SAVE_H

void MemoryCardFileSaveInit(int port, int slot, char *name,
                            void *data_addr, int size);              /* 0x1e1548 */
int  MemoryCardFileSaveMain(void);                                  /* 0x1e15b8 */

#endif /* _SYSTEM_MC_PRG_MC_SAVE_H */
