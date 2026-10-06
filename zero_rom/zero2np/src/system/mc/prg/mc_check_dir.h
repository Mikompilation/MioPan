/* ==========================================================================
 *  system/mc/prg/mc_check_dir.h
 *
 *  sceMcGetDir() wrapper plus the accessors that read the listing back
 *  (mc_check_dir.o, .text 0x1df778).
 *
 *  The listing is kept in this module's .bss: eighteen sceMcTblGetDir entries
 *  and the count sceMcGetDir() returned.  Two of those entries are always "."
 *  and "..", which is why every consumer starts at index 2.
 *
 *  Reconstructed from the Feb 6 2004 prototype (SLES_523.84).
 * ======================================================================== */

#ifndef _SYSTEM_MC_PRG_MC_CHECK_DIR_H
#define _SYSTEM_MC_PRG_MC_CHECK_DIR_H

/* Clear the listing.  Not called by anything in the reconstructed tree -- the
 * listing is always overwritten by the next MemoryCardGetDirInfoReq(). */
void MemoryCardDirInfoCtrlInit(void);                                /* 0x1df778 */

/* `name` is the "<dir>/*" wildcard MemoryCardMakeSearchDirPath() builds. */
void MemoryCardGetDirInfoInit(int port, int slot, char *name);        /* 0x1df7a0 */
int  MemoryCardGetDirInfoMain(void);                                 /* 0x1df808 */
int  MemoryCardGetDirInfoReq(int port, int slot, char *name);          /* 0x1df990 */

/* Entries in the last listing, "." and ".." included. */
int  GetMemoryCardCheckDirFileNum(void);                             /* 0x1df9b8 */

/* Name of entry `data_pos` (so 2 is the first real file).  Asserts if the
 * index is past the count.  `name` needs 55 bytes. */
void GetMemoryCardCheckDirEntryName(char *name, int data_pos);        /* 0x1df9c8 */

/* Cluster cost of the directory as it actually sits on the card, derived from
 * the listing.  MemoryCardCheckDirBroken() compares this against
 * GetMemoryCardDirSizeCluster()'s prediction; the two agree exactly for an
 * intact directory. */
int  GetMemoryCardCheckDirSize(void);                                /* 0x1dfa38 */

#endif /* _SYSTEM_MC_PRG_MC_CHECK_DIR_H */
