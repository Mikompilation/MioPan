/* ==========================================================================
 *  system/eeiop/ee_iop_q.h
 *
 *  Public interface for the EE<->IOP synchronous "query" channel (ee_iop_q.c) -
 *  a second SIF RPC binding used for small request/reply pokes that run
 *  alongside the main command queue: file-size queries, the load-cancel poke
 *  the file loader issues to stop an in-flight transfer, and SPU transfer-core
 *  reservation.
 *
 *  STUB: the bodies in ee_iop_q.c are placeholders (signatures from the ELF /
 *  functions.txt); this module has not been reverse-engineered yet.
 *
 *  Reconstructed from the Feb 6 2004 prototype (SLES_523.84).
 * ======================================================================== */

#ifndef _SYSTEM_EEIOP_EE_IOP_Q_H
#define _SYSTEM_EEIOP_EE_IOP_Q_H

void ee_iopQueryInit(void);
void QueryFileSize(int file_no, unsigned int *ps);
void ReqQueryLoadCancel(void);
void ReqQuerySPUTransCoreGet(int *ps);
void ReqQuerySPUTransCoreRelease(int core);

#endif /* _SYSTEM_EEIOP_EE_IOP_Q_H */
