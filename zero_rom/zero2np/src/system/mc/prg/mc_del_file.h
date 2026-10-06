/* ==========================================================================
 *  system/mc/prg/mc_del_file.h
 *
 *  sceMcDelete() wrapper (mc_del_file.o, .text 0x1e0120).  Deletes a file or,
 *  once it is empty, a directory -- mc_del_dir.c uses it for exactly that.
 *
 *  Reconstructed from the Feb 6 2004 prototype (SLES_523.84).
 * ======================================================================== */

#ifndef _SYSTEM_MC_PRG_MC_DEL_FILE_H
#define _SYSTEM_MC_PRG_MC_DEL_FILE_H

void MemoryCardFileDelInit(int port, int slot, char *name);       /* 0x1e0120 */
int  MemoryCardFileDelMain(void);                                 /* 0x1e0188 */
int  MemoryCardFileDelReq(int port, int slot, char *name);         /* 0x1e0318 */

#endif /* _SYSTEM_MC_PRG_MC_DEL_FILE_H */
