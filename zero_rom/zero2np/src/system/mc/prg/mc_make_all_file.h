/* ==========================================================================
 *  system/mc/prg/mc_make_all_file.h
 *
 *  Populate a fresh card directory (mc_make_all_file.o, .text 0x1e0ba8).
 *
 *  Creates every data file the directory needs, then the icon, then icon.sys.
 *  Each data file is written as all zeroes with 0xffffffff in its checksum
 *  slot, which is the pattern MemoryCardCheckNewFileLoad() recognises as
 *  "made but never saved to" -- that is how the load screen tells an empty
 *  slot from a corrupt one.
 *
 *  Reconstructed from the Feb 6 2004 prototype (SLES_523.84).
 * ======================================================================== */

#ifndef _SYSTEM_MC_PRG_MC_MAKE_ALL_FILE_H
#define _SYSTEM_MC_PRG_MC_MAKE_ALL_FILE_H

/* `buff_addr` must be at least as large as the biggest file in the directory;
 * Init() and Main() both assert if it is NULL or too small. */
void MemoryCardAllFileMakeInit(int port, int slot, int dir_label,
                               void *buff_addr, int buff_size);      /* 0x1e0ba8 */
int  MemoryCardAllFileMakeMain(void);                               /* 0x1e0c28 */

#endif /* _SYSTEM_MC_PRG_MC_MAKE_ALL_FILE_H */
