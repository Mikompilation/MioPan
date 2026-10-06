/* ==========================================================================
 *  system/mc/prg/mc_make_file.h
 *
 *  Create a card file and write it (mc_make_file.o, .text 0x1e0fe8).
 *
 *  mc_save.c with the create bit set (open mode 0x203).  Used only while a
 *  directory is being built: every data file, the icon and icon.sys are made
 *  through here, after which ordinary saves go through mc_save.c.
 *
 *  Reconstructed from the Feb 6 2004 prototype (SLES_523.84).
 * ======================================================================== */

#ifndef _SYSTEM_MC_PRG_MC_MAKE_FILE_H
#define _SYSTEM_MC_PRG_MC_MAKE_FILE_H

void MemoryCardMakeNewFileInit(int port, int slot, char *name,
                               void *data_addr, int size);           /* 0x1e0fe8 */
int  MemoryCardMakeNewFileMain(void);                               /* 0x1e1058 */

#endif /* _SYSTEM_MC_PRG_MC_MAKE_FILE_H */
