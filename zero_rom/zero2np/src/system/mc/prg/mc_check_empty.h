/* ==========================================================================
 *  system/mc/prg/mc_check_empty.h
 *
 *  "Is there room for this directory" (mc_check_empty.o, .text 0x1dfaa0).
 *
 *  Both answers are yes/no rather than a code: the caller only needs to know
 *  whether to offer the save at all.
 *
 *  MemoryCardCheckEmpty() was previously declared in mc_check_card.h, which is
 *  not its module -- the ninth instance of that hazard in this tree.
 *
 *  Reconstructed from the Feb 6 2004 prototype (SLES_523.84).
 * ======================================================================== */

#ifndef _SYSTEM_MC_PRG_MC_CHECK_EMPTY_H
#define _SYSTEM_MC_PRG_MC_CHECK_EMPTY_H

/* Non-zero when the card's free space covers a *new* directory for dir_label. */
int MemoryCardCheckEmpty(int dir_label);                            /* 0x1dfaa0 */

/* Non-zero when it covers a *replacement* for one that is already there: the
 * space the existing directory would give back counts towards the total.  The
 * caller must have listed that directory first -- this reads
 * GetMemoryCardCheckDirSize(). */
int MemoryCardCheckEmptyBroken(int dir_label);                      /* 0x1dfad8 */

#endif /* _SYSTEM_MC_PRG_MC_CHECK_EMPTY_H */
