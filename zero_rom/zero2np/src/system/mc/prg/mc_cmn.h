/* ==========================================================================
 *  system/mc/prg/mc_cmn.h
 *
 *  The four shared helpers every other mc module leans on (mc_cmn.o,
 *  .text 0x1dfc88):
 *
 *    MemoryCardExeEndSync()      the polling half of libmc's async handshake,
 *                                called from all fourteen step machines
 *    GetMemoryCardFreeSizeForBrowser()
 *    CalcMemoryCardDataCheckSum() / SetMemoryCardDataCheckSum()
 *
 *  Reconstructed from the Feb 6 2004 prototype (SLES_523.84).
 * ======================================================================== */

#ifndef _SYSTEM_MC_PRG_MC_CMN_H
#define _SYSTEM_MC_PRG_MC_CMN_H

/* Poll the outstanding libmc request.  Returns 1 when it finished (and stores
 * its result code through `result`), 0 while it is still running, and -1 when
 * nothing is running at all -- which every caller reads as "the request was
 * lost, go back to step 0 and re-issue it". */
int MemoryCardExeEndSync(int *result);          /* 0x1dfc88 */

/* Free clusters as the PS2 browser would report them: the card's own free
 * count less the two clusters the browser needs to make a directory. */
int GetMemoryCardFreeSizeForBrowser(void);      /* 0x1dfca8 */

/* Every card file ends with a four-byte checksum over the bytes before it:
 * the plain sum of `size` signed bytes.  Calc computes it, Set writes it into
 * the four bytes at `addr` little-end first. */
int  CalcMemoryCardDataCheckSum(char *data_addr, int size);   /* 0x1dfcd0 */
void SetMemoryCardDataCheckSum(char *addr, int check_sum);    /* 0x1dfd00 */

#endif /* _SYSTEM_MC_PRG_MC_CMN_H */
