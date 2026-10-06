/* ==========================================================================
 *  system/mc/prg/mc.h
 *
 *  Memory-card subsystem brackets (mc.o, .text 0x1df010, 0x84 bytes).
 *
 *  MemoryCardInit() runs once at boot; MemoryCardExeInit() / MemoryCardEnd()
 *  bracket a screen's use of the card.
 *
 *  ---- the folder's return-code convention -------------------------------
 *
 *  Every Main() in system/mc answers the same way:
 *
 *      1        finished, successfully
 *      0        still working
 *     -1        the card was swapped (only from the card query)
 *     -2        no format
 *     -3        card full / short read
 *     -4        no such entry
 *     -5        access denied
 *     -6        directory not empty
 *     -7        out of file handles
 *     -8        replace failed
 *    -10        the request was rejected four times running -- give up
 *    -0x14      anything else the card reported
 *
 *  -1 .. -8 are libmc's own result codes passed through unchanged; -10 and
 *  -0x14 are the folder's.  The screens map them to message ids, so a code
 *  reaching a caller unmapped shows the generic error rather than nothing.
 *
 *  Reconstructed from the Feb 6 2004 prototype (SLES_523.84).
 * ======================================================================== */

#ifndef _SYSTEM_MC_PRG_MC_H
#define _SYSTEM_MC_PRG_MC_H

/* Bring libmc up.  main.c calls this once, before anything else touches the
 * card -- every primitive asserts "sceMcInit forgets" if it did not. */
void MemoryCardInit(void);      /* 0x1df010 */

/* Bracket a screen's use of the card.  ExeInit() takes the soft-reset lock,
 * forgets the cached card facts and clears the play-data header; End() drops
 * any icon buffer, blocks until the outstanding request finishes, and releases
 * the lock. */
void MemoryCardExeInit(void);   /* 0x1df030 */
void MemoryCardEnd(void);       /* 0x1df060 */

#endif /* _SYSTEM_MC_PRG_MC_H */
