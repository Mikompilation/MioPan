/* ==========================================================================
 *  system/mc/prg/mc_make_dir.h
 *
 *  sceMcMkdir() wrapper (mc_make_dir.o, .text 0x1e0e18).
 *
 *  Reconstructed from the Feb 6 2004 prototype (SLES_523.84).
 * ======================================================================== */

#ifndef _SYSTEM_MC_PRG_MC_MAKE_DIR_H
#define _SYSTEM_MC_PRG_MC_MAKE_DIR_H

/* `name` is a bare directory name from MemoryCardSetDirName(), no slashes. */
void MemoryCardMakeNewDirInit(int port, int slot, char *name);     /* 0x1e0e18 */
int  MemoryCardMakeNewDirMain(void);                               /* 0x1e0e80 */
int  MemoryCardMakeNewDirReq(int port, int slot, char *name);       /* 0x1e0fd0 */

#endif /* _SYSTEM_MC_PRG_MC_MAKE_DIR_H */
