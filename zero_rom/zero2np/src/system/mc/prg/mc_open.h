/* ==========================================================================
 *  system/mc/prg/mc_open.h
 *
 *  sceMcOpen() wrapper (mc_open.o, .text 0x1e1190).
 *
 *  Every primitive in system/mc has this shape: an Init() that fills a
 *  file-static control block and clears its step, a Main() that pumps the
 *  three-step machine (issue / poll / drain-a-stale-request), and a Req() that
 *  is nothing but the libmc call.  Main() returns 1 on success, 0 while still
 *  working, and a negative code otherwise -- see mc.h for the code table.
 *
 *  Reconstructed from the Feb 6 2004 prototype (SLES_523.84).
 * ======================================================================== */

#ifndef _SYSTEM_MC_PRG_MC_OPEN_H
#define _SYSTEM_MC_PRG_MC_OPEN_H

/* `mode` is a sceMcFileAttr* / sceMcFileCreateFile bitmask: mc_load passes 1
 * (read), mc_save 3 (read+write) and mc_make_file 0x203 (create + read+write). */
void MemoryCardFileOpenInit(int port, int slot, char *name, int mode);   /* 0x1e1190 */

/* On success stores the file descriptor through `fd`.  Init() does not touch
 * it; Main() writes -1 into it as it issues the request, so a caller that only
 * checks the return value still gets a defined handle. */
int  MemoryCardFileOpenMain(int *fd);                                   /* 0x1e11b8 */

int  MemoryCardFileOpenReq(int port, int slot, char *name, int mode);    /* 0x1e1350 */

#endif /* _SYSTEM_MC_PRG_MC_OPEN_H */
