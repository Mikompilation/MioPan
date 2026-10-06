/* ==========================================================================
 *  system/iop/iop_load_stream.h
 *
 *  The file-streaming RPC service (iop_load_stream.c), RPC number 4.  It is
 *  the IOP end of the EE's file_stream.c: start a stream on a file, pull
 *  bytes out of it on demand, stop.  All the work is the ring buffer's; this
 *  file is only the RPC wrapper and the one STM_IOP_WRK it owns.
 *
 *  Reconstructed from iopsys.irx (Feb 6 2004 prototype, SLES_523.84).
 * ======================================================================== */

#ifndef _SYSTEM_IOP_IOP_LOAD_STREAM_H
#define _SYSTEM_IOP_IOP_LOAD_STREAM_H

#include "iop_types.h"

/* RPC function numbers for service 4. */
typedef enum _FILE_STREAM_REQ_ENUM
{
    REQ_STM_START = 0,
    REQ_STM_READ  = 1,
    REQ_STM_STOP  = 2
} FILE_STREAM_REQ_ENUM;

typedef struct _strSTM_START        /* 0x110 */
{
    /* 0x000 */ LOAD_DEF_STRUCT ld;
} strSTM_START;

typedef struct _strSTM_READ         /* 0x8 */
{
    /* 0x0 */ int   read_size;
    /* 0x4 */ void *ee_buf;
} strSTM_READ;

/* Spawns the service thread. */
void CreateRPCLoadStmThread(void);

#endif /* _SYSTEM_IOP_IOP_LOAD_STREAM_H */
