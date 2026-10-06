/* ==========================================================================
 *  system/mc/prg/mc_icon.h
 *
 *  Write the save icon onto the card (mc_icon.o, .text 0x1e04a8).
 *
 *  The icon is an ordinary CD file: this job loads it into a heap buffer, makes
 *  a card file of exactly that size, and releases the buffer.  It is the only
 *  job in system/mc that touches the CD as well as the card.
 *
 *  Reconstructed from the Feb 6 2004 prototype (SLES_523.84).
 * ======================================================================== */

#ifndef _SYSTEM_MC_PRG_MC_ICON_H
#define _SYSTEM_MC_PRG_MC_ICON_H

void MemoryCardIconInit(int port, int slot, int dir_label, int icon_type); /* 0x1e04a8 */
int  MemoryCardIconMain(void);                                            /* 0x1e04e0 */

/* Drop the staging buffer.  Called by Init() before it starts, by Main() when
 * it finishes either way, and by MemoryCardEnd() -- so an abandoned job never
 * leaks its icon. */
void LiberateMemoryCardIconDataMem(void);                                 /* 0x1e0648 */

#endif /* _SYSTEM_MC_PRG_MC_ICON_H */
