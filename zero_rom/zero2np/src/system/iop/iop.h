/* ==========================================================================
 *  system/iop/iop.h
 *
 *  iopsys.irx's core: module entry, the two SIF RPC service loops, the
 *  command dispatcher the EE's iopCommandRegister() queue arrives at, the
 *  shared CD reader, and the SPU transfer arbitration.
 *
 *  Reconstructed from iopsys.irx (Feb 6 2004 prototype, SLES_523.84).
 * ======================================================================== */

#ifndef _SYSTEM_IOP_IOP_H
#define _SYSTEM_IOP_IOP_H

#include <stdint.h>

#include "iop_types.h"

/* ---- EE transfer ------------------------------------------------------- */
/* DMAs `size` bytes from IOP address `iop_buf` to EE address `ee_buf` and
 * blocks until the transfer has landed. */
/* PORT: the two addresses widened from `unsigned int`. */
void MyTransEEWait(uintptr_t iop_buf, uintptr_t ee_buf, unsigned int size);

/* ---- disc -------------------------------------------------------------- */
/* Queues a read for the shared reader thread and waits for it. */
void iopReqRead(unsigned int offset_sector, unsigned int read_sector_num,
                void *buf, char *pname);
/* The blocking read itself, retried until the drive answers. */
void MyCdRead(unsigned int start_sector, unsigned int n_sector, void *buf);
void MyCdSeek(unsigned int sector);

/* Host-PC read path.  Compiled out in this build -- all four are empty, the
 * same way the EE side's are. */
void MyPcRead(unsigned int offset, unsigned int n_sector, void *buf, char *pname);
int  MyOpen(char *fname);
void MyClose(int hndl);

/* ---- SPU transfer arbitration ------------------------------------------ */
/* Claims whichever SPU core is free and returns it. */
int  WaitSpuTransSema(void);
void SignalSpuTransSema(int core);
void WaitSPUTransEnd(int core);

/* ---- status reported back to the EE ------------------------------------ */
void SetEndVoices(int core, unsigned int end_voice);
void SetStreamRet(int wrk_id, IOP_STREAM_RET ret);
void SetPCMStreamRet(int wrk_id, IOP_STREAM_RET ret);

/* ---- misc -------------------------------------------------------------- */
int  ReferSemaNowCount(int sema_id);
void PrintIOPMem(char *name);
void CreateRPCQueryThread(void);
void CreateiopReadTH(void);

/* Interrupt-time callbacks; taken by address, never called directly. */
void _intr_SifSetDma(void *data);
int  _intr_SignalSema(int core_bit, void *sema);
int  _intr_SignalTransCore(int core, void *dmy);
void cdvd_callback(int cb_reason);

#endif /* _SYSTEM_IOP_IOP_H */
