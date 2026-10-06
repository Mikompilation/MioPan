/* ==========================================================================
 *  eecdvd.h
 *
 *  Public interface for system/os/eecdvd.c - the CD/DVD file-load front end.
 *  These wrap the lower file loader (FileLoadReqEE / FileLoadIsEnd / ...);
 *  the *_L variants bias the file id by the current language index.
 *
 *  Reconstructed from the Feb 6 2004 prototype (SLES_523.84).
 * ======================================================================== */

#ifndef _SYSTEM_OS_EECDVD_H
#define _SYSTEM_OS_EECDVD_H

#include <stdint.h>

/* --------------------------------------------------------------------------
 *  File-load front end (eecdvd.c)
 * ------------------------------------------------------------------------ */

/* Queue a load of `file_no` to `addr` at the default priority; -1 if absent. */
int LoadReq(int file_no, uintptr_t addr);

/* As LoadReq, but write the load id to *id (if non-NULL) and return the next
 * free address (addr + size rounded up to 64 bytes). */
uintptr_t LoadReqGetAddr(int file_no, uintptr_t addr, int *id);

/* Sound-effect load request (stubbed in this build). */
int LoadReqSe(int file_no, unsigned char se_type);

/* Completion polling. */
int IsLoadEndAll(void);
int IsLoadEnd(int id);

/* Language-aware wrappers (file_no is biased by GetLanguage()). */
int          FileLoadReqEE_L(int file_no, void *adrs, int priority,
                             void (*func)(void *, void *), void *arg);
int          FileLoadIsEnd2_L(int file_no, void *adrs);
unsigned int GetFileSize_L(int file_no);

#endif /* _SYSTEM_OS_EECDVD_H */
