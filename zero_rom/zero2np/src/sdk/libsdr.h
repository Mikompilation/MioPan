/* ==========================================================================
 *  libsdr.h  (remote SPU2 library -- EE half -- PC-port shim)
 *
 *  libsdr.a is the EE's window onto the IOP's libsd.  Every entry point is one
 *  variadic call -- sceSdRemote(wait, fno, ...) -- that marshals up to six
 *  argument words into a buffer and RPCs them across the SIF; `fno` names which
 *  libsd function the IOP is to run, and `wait` 1 blocks for its return value.
 *
 *  The function numbers are libsd's export index times 0x10, plus 0x8000.  That
 *  is not a guess: sceSdRemote() itself (0x2972a0) special-cases 0x8160 and
 *  0x8170 to stash a transfer/SPU2 interrupt handler, which pins indices 22 and
 *  23 -- sceSdSetTransIntrHandler and sceSdSetSpu2IntrHandler -- and every
 *  other number the ROM uses falls out of the same table.  The four below are
 *  the ones audiodec.c reaches; their argument shapes match libsd.h exactly.
 *
 *  On the host the IOP is not across a bus, it is the same process, so the
 *  "remote" call dispatches straight into the libsd shim.  What does not
 *  survive is the ROM's blanket six-word marshal: it copies six argument
 *  registers whether the callee wants them or not, which is free on the EE and
 *  undefined behaviour through va_arg here.  So the shim reads exactly as many
 *  arguments as each function takes -- see libsdr.cpp, which documents the one
 *  place the ROM passes fewer than the callee's arity.
 * ======================================================================== */

#ifndef _LIBSDR_H
#define _LIBSDR_H

#include "scetypes.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ---- sceSdRemote function numbers -------------------------------------- */

#define SDR_SET_PARAM           0x8010  /* sceSdSetParam(entry, value)       */
#define SDR_VOICE_TRANS         0x80d0  /* sceSdVoiceTrans(chan, mode,
                                         *   iopaddr, spuaddr, size)         */
#define SDR_BLOCK_TRANS         0x80e0  /* sceSdBlockTrans(chan, mode,
                                         *   iopaddr, size, start_addr)      */
#define SDR_BLOCK_TRANS_STATUS  0x8100  /* sceSdBlockTransStatus(chan, flag) */

/* Brings the RPC channel up.  playpss.o calls it once before audioDecCreate();
 * nothing here needs doing, so it reports success. */
int sceSdRemoteInit(void);

/* `wait` 1 runs the call and returns what the IOP-side function returned;
 * 0 posts it and returns immediately.  Every site in the ROM passes 1. */
int sceSdRemote(int wait, u_int fno, ...);

#ifdef __cplusplus
}
#endif

#endif /* _LIBSDR_H */
