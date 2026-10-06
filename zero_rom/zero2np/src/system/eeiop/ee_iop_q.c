// FILE: /home/akira_koide/zero2np/src/system/eeiop/ee_iop_q.c
//
// STUB - EE<->IOP synchronous query channel.  Not yet reverse-engineered;
// placeholder bodies for the entry points other modules call (cddat.c /
// fileload.c reach ReqQueryLoadCancel, etc.).  Signatures from functions.txt
// (addresses 0x2711c0 .. 0x2713f8); bodies await reconstruction.
//
// Reconstructed from the Feb 6 2004 prototype (SLES_523.84).

#include "ee_iop_q.h"               // this file's public API

// TODO: reverse-engineer ee_iop_q.c (SIF RPC query binding + the request pokes).

void ee_iopQueryInit(void)
{
}

void QueryFileSize(int file_no, unsigned int *ps)
{
}

void ReqQueryLoadCancel(void)
{
}

void ReqQuerySPUTransCoreGet(int *ps)
{
}

void ReqQuerySPUTransCoreRelease(int core)
{
}
