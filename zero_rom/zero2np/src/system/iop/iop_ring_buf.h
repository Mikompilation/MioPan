/* ==========================================================================
 *  system/iop/iop_ring_buf.h
 *
 *  The producer/consumer ring buffer every IOP-side loader is built on
 *  (iop_ring_buf.c).  InitRingBufSub() allocates the buffer and spawns a
 *  reader thread that fills it off the disc; RingBufTransEE() drains it into
 *  EE memory over SIF.  Two counting semaphores keep the two apart:
 *  `ring_buf_sema` counts free slots and starts full, `read_dat_sema` counts
 *  filled slots and starts empty.
 *
 *  Reconstructed from iopsys.irx (Feb 6 2004 prototype, SLES_523.84).
 * ======================================================================== */

#ifndef _SYSTEM_IOP_IOP_RING_BUF_H
#define _SYSTEM_IOP_IOP_RING_BUF_H

#include "iop_types.h"

/* Allocates wrk->ring_buf_top, creates both semaphores and starts the reader
 * thread at `priority`.  Returns the thread id, which is what the two release
 * entry points want back.  wrk->ld must already be filled in. */
int  InitRingBufSub(RING_BUF_WRK *wrk, int priority);

/* Tears down without waiting -- only safe once the reader has stopped. */
void ReleaseRingBufSub(RING_BUF_WRK *wrk, int read_th_idx);
/* Asks the reader to stop, waits for it to acknowledge, then tears down. */
void ReleaseRingBufSubWait(RING_BUF_WRK *wrk, int read_th_idx);

/* Copies up to `read_byte` bytes from the ring to EE address `ee_buf`,
 * blocking on data that has not arrived.  Returns how much it actually moved,
 * which is less than asked for at end of file. */
/* PORT: `ee_buf` is widened from int.  It is an EE-side destination address,
 * 4 bytes on the PS2 and 8 here, and it walks slot by slot inside the body --
 * an int loses the top half of a real host pointer and the transfer lands on
 * a wild address.  See [[int-pointer-out-params-must-widen]]. */
int  RingBufTransEE(RING_BUF_WRK *wrk, int read_byte, uintptr_t ee_buf);

/* The reader thread body.  Takes no argument: it recovers its RING_BUF_WRK
 * from its own ThreadInfo.option. */
void thRingBufRead(void);

#endif /* _SYSTEM_IOP_IOP_RING_BUF_H */
