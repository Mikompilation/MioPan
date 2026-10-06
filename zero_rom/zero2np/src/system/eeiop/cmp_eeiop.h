/* ==========================================================================
 *  system/eeiop/cmp_eeiop.h
 *
 *  Public interface for the EE-side decompression path (cmp_eeiop.c) used by
 *  the file loader for compressed files.  cmp_eeiopInit() carves the decode
 *  scratch out of the loader work buffer and spins up the decode thread;
 *  cmp_eeiopCreateDecodeThread() builds the IOP load-request that streams a
 *  compressed file into that scratch for the thread to inflate.
 *
 *  STUB: the bodies in cmp_eeiop.c are placeholders (signatures from the ELF /
 *  functions.txt); this module has not been reverse-engineered yet.
 *
 *  Reconstructed from the Feb 6 2004 prototype (SLES_523.84).
 * ======================================================================== */

#ifndef _SYSTEM_EEIOP_CMP_EEIOP_H
#define _SYSTEM_EEIOP_CMP_EEIOP_H

#include "fileload.h"               /* LOAD_REQ_NEW (returned by value below) */

void        *cmp_eeiopInit(void *wrk_buffer);
int          cmp_eeiopGetWrkSize(void);
LOAD_REQ_NEW cmp_eeiopCreateDecodeThread(int size, intptr_t adrs, int start_sector, int priority);
void         cmp_eeiopWaitSema(void);
int          cmp_eeiopIsLate(void);
void         cmp_eeiopCancel(void);
void         cmp_eeiopChangePriority(int priority);

#endif /* _SYSTEM_EEIOP_CMP_EEIOP_H */
