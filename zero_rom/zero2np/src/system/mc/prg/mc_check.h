/* ==========================================================================
 *  system/mc/prg/mc_check.h
 *
 *  "Is this card usable and is our directory on it" (mc_check.o, .text
 *  0x1df098).
 *
 *  Every save/load screen starts here: it chains the card query and the
 *  directory listing, so a caller that sees 1 has both a formatted PS2 card and
 *  a fresh listing to hand to MemoryCardCheckDirBroken().
 *
 *  Reconstructed from the Feb 6 2004 prototype (SLES_523.84).
 * ======================================================================== */

#ifndef _SYSTEM_MC_PRG_MC_CHECK_H
#define _SYSTEM_MC_PRG_MC_CHECK_H

/* `path_name` is the "<dir>/*" wildcard MemoryCardMakeSearchDirPath() built. */
void MemoryCardCheckInit(int port, int slot, char *name);        /* 0x1df098 */

/* 1 when the card is usable and the directory has been listed, 0 while still
 * working, and a negative code otherwise -- the sub-jobs' codes passed through
 * unchanged, so -2 is "unformatted", -4/-6 name the listing's complaints and
 * -0x14 is the catch-all.  The screens map those to message ids. */
int  MemoryCardCheckMain(void);                                 /* 0x1df0f8 */

#endif /* _SYSTEM_MC_PRG_MC_CHECK_H */
