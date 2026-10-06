/* ==========================================================================
 *  system/mc/prg/mc_check_broken.h
 *
 *  Integrity tests (mc_check_broken.o, .text 0x1df218).
 *
 *  Three yes/no answers, all built out of what other modules already know: the
 *  directory test compares the listing against the save layout, and the two
 *  file tests compare a staging buffer's trailing checksum against a fresh sum
 *  of its contents.
 *
 *  Reconstructed from the Feb 6 2004 prototype (SLES_523.84).
 * ======================================================================== */

#ifndef _SYSTEM_MC_PRG_MC_CHECK_BROKEN_H
#define _SYSTEM_MC_PRG_MC_CHECK_BROKEN_H

/* Non-zero when the directory on the card matches what this build expects:
 * same entry count and same cluster cost.  Needs a listing -- call it after
 * MemoryCardCheckMain() reports 1. */
int MemoryCardCheckDirBroken(int dir_label);                /* 0x1df218 */

/* Non-zero when the buffer's trailing four-byte checksum agrees with its
 * contents.  A freshly-made, never-saved file (all zeroes with 0xffffffff in
 * the checksum) passes as well: that is a valid empty file, not damage. */
int MemoryCardCheckFileBroken(void *data_addr, int size);    /* 0x1df280 */

/* Non-zero when the buffer is exactly that freshly-made file -- checksum
 * 0xffffffff *and* not one non-zero byte in it.  That is how the load screen
 * tells "this slot has never been saved to" from "this slot has a save".
 * Despite the name it has nothing to do with newer builds. */
int MemoryCardCheckNewFileLoad(void *data_addr, int size);   /* 0x1df320 */

#endif /* _SYSTEM_MC_PRG_MC_CHECK_BROKEN_H */
