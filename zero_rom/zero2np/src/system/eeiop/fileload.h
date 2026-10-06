/* ==========================================================================
 *  system/eeiop/fileload.h
 *
 *  Public interface for the EE-side asynchronous file loader (fileload.c).
 *  A background thread (thFileLoad) services a priority/sector-ordered queue of
 *  FILE_LOAD_WRK requests, driving the IOP file server over SIF RPC and (for
 *  compressed files) the EE decode path in cmp_eeiop.c.  Callers queue a load
 *  with FileLoadReq{EE,SPU} / FileDecodeLoadReqEE, poll it with
 *  FileLoadIsEnd{,2} / AllFileLoadIsEnd, and can withdraw it with the
 *  FileLoadCancel* family.  Each request returns a packed id: (id << 16) |
 *  temp_id.
 *
 *  Reconstructed from the Feb 6 2004 prototype (SLES_523.84).
 * ======================================================================== */

#ifndef _SYSTEM_EEIOP_FILELOAD_H
#define _SYSTEM_EEIOP_FILELOAD_H

#include <stdint.h>

#include "ee_iop.h"                 /* EEIOP_DEF (bring-up parameter) */

/* Result of a cancel / all-loaded query. */
typedef enum _FILE_LOAD_ENUM
{
    FILE_LOAD_OK               = 0,
    FILE_LOAD_ALREADY_LOAD     = 1,
    FILE_LOAD_ALREADY_ALL_LOAD = 2,
    FILE_LOAD_ID_NOT_EXIST     = 3
} FILE_LOAD_ENUM;

/* How a queued file is delivered: raw to EE, to SPU RAM, or EE/IOP decode. */
typedef enum _FILE_LOAD_TYPE
{
    FILE_LOAD_TYPE_EE          = 0,
    FILE_LOAD_TYPE_SPU         = 1,
    FILE_LOAD_TYPE_DECODE_EE   = 2,
    FILE_LOAD_TYPE_DECODE_IOP  = 3,
    FILE_LOAD_TYPE_FORCE_DWORD = -1
} FILE_LOAD_TYPE;

/* The IOP load-request payload (shared with the EE decode path, cmp_eeiop.c). */
typedef struct                      /* 0x110 */
{
    /* 0x000 */ int  one_buf_size;
    /* 0x004 */ int  ring_buf_num;
    /* 0x008 */ int  start_sector;
    /* 0x00c */ int  size;
    /* 0x010 */ char file_name[256];
} LOAD_DEF_STRUCT;

typedef struct                      /* 0x11c */
{
    /* 0x000 */ FILE_LOAD_TYPE  type;
    /* 0x004 */ intptr_t        tmp_ee_adrs;
    /* 0x008 */ intptr_t        adrs;
    /* 0x00c */ LOAD_DEF_STRUCT ld;
} LOAD_REQ_NEW;

typedef void (*FILE_LOAD_CALLBACK)(void *buffer, void *arg);

/* ---- boot-time sizing / bring-up -------------------------------------- */
int   FileLoadGetNeedSize(EEIOP_DEF *def);
void *FileLoadInit(EEIOP_DEF *def, void *wrk_buffer);

/* ---- queue a load ----------------------------------------------------- */
int   FileLoadReqEE(int file_no, void *adrs, int priority, FILE_LOAD_CALLBACK func, void *arg);
int   FileLoadReqSPU(int file_no, void *adrs, int priority, FILE_LOAD_CALLBACK func, void *arg);
int   FileDecodeLoadReqEE(int file_no, void *adrs, int priority, FILE_LOAD_CALLBACK func, void *arg);
void  FileLoadReqEEWait(int file_no, void *adrs);

/* ---- poll completion -------------------------------------------------- */
int   FileLoadIsEnd(int id);
int   FileLoadIsEnd2(int file_no, void *adrs);
int   AllFileLoadIsEnd(void);

/* ---- cancel ----------------------------------------------------------- */
FILE_LOAD_ENUM FileLoadCancel(int id, FILE_LOAD_CALLBACK func, void *arg);
FILE_LOAD_ENUM FileLoadCancel2(int file_no, void *adrs, FILE_LOAD_CALLBACK func, void *arg);
FILE_LOAD_ENUM FileLoadCancelSPU2(int file_no, void *adrs, FILE_LOAD_CALLBACK func, void *arg);
FILE_LOAD_ENUM FileLoadCancelWait(int id);
FILE_LOAD_ENUM FileLoadCancelWait2(int file_no, void *adrs);
FILE_LOAD_ENUM FileLoadCancelAll(void);

/* ---- misc ------------------------------------------------------------- */
void  FileLoadChangeThreadPriority(int load_th_priority, int decode_th_priority);
void  PrintFileLoadWrk(void);

#endif /* _SYSTEM_EEIOP_FILELOAD_H */
