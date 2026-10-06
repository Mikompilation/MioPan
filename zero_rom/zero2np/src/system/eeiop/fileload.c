// FILE: /home/akira_koide/zero2np/src/system/eeiop/fileload.c
//
// EE-side asynchronous file loader.  A dedicated thread (thFileLoad) owns a
// queue of FILE_LOAD_WRK requests; each frame's worth of requests is sorted by
// priority (descending) and, within a priority band, by start sector, so the
// disc head sweeps monotonically.  The thread hands the head request to the IOP
// file server over SIF RPC (rpcFileLoadReqSub), waits for the transfer, and -
// for compressed files - runs the EE-side decode in cmp_eeiop.c.
//
//   * Queue        - FileLoadReqEE (raw or, for a compressed file, decode),
//                    FileLoadReqSPU, FileDecodeLoadReqEE, FileLoadReqEEWait
//                    (block the caller until its load finishes).
//   * Poll         - FileLoadIsEnd (by packed id), FileLoadIsEnd2 (by file/adrs),
//                    AllFileLoadIsEnd.
//   * Cancel       - FileLoadCancel[2/SPU2] (fire a callback), the *Wait
//                    variants (block until withdrawn) and FileLoadCancelAll.
//   * Bring-up     - FileLoadGetNeedSize / FileLoadInit lay the work array and
//                    ring buffers out of the IOP work buffer and spin up the
//                    loader thread; FileLoadChangeThreadPriority retunes it.
//
// A request id is packed as (wrk.id << 16) | wrk.temp_id: id is the fixed slot
// index, temp_id a rolling serial that disambiguates reused slots.
//
// Reconstructed from the Feb 6 2004 prototype (SLES_523.84).

#include "fileload.h"               // this file's public API + FILE_LOAD_ENUM

#include <eekernel.h>               // threads / semaphores / FlushCache / SYNC / EI
#include <libcdvd.h>                // sceCdReadFile / sceCdSearchFile
#include <sifrpc.h>                 // sceSifClientData / sceSifBindRpc / sceSifCallRpc

#include <stdio.h>                  // printf
#include <stdlib.h>                 // qsort
#include <string.h>                 // strcat

#include "cddat.h"                  // GetFileStartSector / GetFileCmpSize / cddatIsCmpFile / GetFileNameBuffer / GetFileName
#include "ee_iop.h"                 // EEIOP_DEF
#include "ee_iop_q.h"               // ReqQueryLoadCancel (IOP query layer)
#include "cmp_eeiop.h"              // EE-side decode path (compressed files)
#include "../../miopan/io/miopan_fileload.h" // MioPan_FileLoadServe (PC host loader)
#include "../../common/utility2.h"  // PRINT_ASSERT + SetAssertPreMessage / PrintAssertReal + GetAlignUp

// ──────────────────────────────────────────────────────────────────────
// The per-request work block and the loader-wide state.  FILE_LOAD_TYPE and
// the IOP LOAD_REQ_NEW payload are shared with cmp_eeiop.c, so they live in
// fileload.h.

typedef enum _LOAD_CANCEL_TYPE
{
    LOAD_CANCEL_NONE   = 0,
    LOAD_CANCEL_STORE  = 1,
    LOAD_CANCEL_NORMAL = 2,
    LOAD_CANCEL_WAIT   = 3
} LOAD_CANCEL_TYPE;

typedef struct                      /* 0x28 */
{
    /* 0x00 */ int             file_no;
    /* 0x04 */ int             start_sector;
    /* 0x08 */ char           *buffer;
    /* 0x0c */ FILE_LOAD_CALLBACK func;
    /* 0x10 */ FILE_LOAD_TYPE  type;
    /* 0x14 */ int             priority;
    /* 0x18 */ void           *arg;
    /* 0x1c */ int             read_size;
    /* 0x20 */ int             size;
    /* 0x24 */ unsigned short  temp_id;
    /* 0x26 */ short           id;
} FILE_LOAD_WRK;

typedef struct                      /* 0x28 */
{
    /* 0x00 */ int              yet_files;      // requests still queued
    /* 0x04 */ LOAD_CANCEL_TYPE cancel_type;    // pending cancel action
    /* 0x08 */ FILE_LOAD_CALLBACK cancel_func;  // fired when a cancel completes
    /* 0x0c */ void            *cancel_arg;
    /* 0x10 */ int              master_thread_id;
    /* 0x14 */ int              ring_buf_num;   // IOP ring-buffer segment count
    /* 0x18 */ int              one_buf_size;   // IOP ring-buffer segment size
    /* 0x1c */ int              file_load_wrk_max;
    /* 0x20 */ int              load_th_priority;
    /* 0x24 */ int              decode_th_priority;
} FILE_LOAD_SYS;

// The IOP load-completion reply.
typedef struct                      /* 0x8 */
{
    /* 0x0 */ int read_size;
    /* 0x4 */ int cancel_flg;
} FILE_LOAD_RET;

// ──────────────────────────────────────────────────────────────────────
// Statics.

static unsigned short   file_load_wrk_id;       // sbss 3f5000 : rolling temp_id serial
static FILE_LOAD_WRK    *file_load_wrk;          // sbss 3f5004 : the request array
static FILE_LOAD_SYS     file_load_sys;          // bss  4bd7c0 : loader-wide state
static char              iop_fileload_ret[64];   // bss  4bd800 : SIF RPC receive scratch
static char              iop_fileload_cmd[320];  // bss  4bd840 : SIF RPC send scratch
static sceSifClientData  sif_cli_data_f;         // bss  4bd980 : IOP file-server binding
static int               load_th_idx;            // sbss 3f5008 : loader thread id
static int               load_req_sema_id;       // sbss 3f500c : counts queued requests
static int               rpc_sema_id;            // sbss 3f5010 : RPC-completion sema

static char              file_load_stack[0x1000];    // bss 4bc7c0 : loader thread stack

// ──────────────────────────────────────────────────────────────────────
// Forward declarations for the file-static helpers.

static int  FileLoadReq(int file_no, char *adrs, int priority, FILE_LOAD_CALLBACK func,
                        void *arg, FILE_LOAD_TYPE type);
static void FileLoadCancelSub(int i, FILE_LOAD_CALLBACK func, void *arg);
static void FileLoadCancelWaitSub(int i);
static void SwapFileLoadWrk(int one, int two);
static void rpcFileLoadReqSub(FILE_LOAD_WRK *wrk);
static int  cmpFileWrkPri(const void *a, const void *b);
static int  cmpFileWrkSector(const void *a, const void *b);
static void intr_SifEnd_FileLoad(void *data);
static void thFileLoad(void *dummy);
static void _intr_wait_load_func(void *buffer, void *arg);

// ──────────────────────────────────────────────────────────────────────
// Queue a raw EE load, or - if the file is stored compressed - an EE-decode
// load, of file_no into adrs.

int FileLoadReqEE(int file_no, void *adrs, int priority, FILE_LOAD_CALLBACK func, void *arg)
{
    if (cddatIsCmpFile(file_no) == 0)
    {
        return FileLoadReq(file_no, (char *)adrs, priority, func, arg, FILE_LOAD_TYPE_EE);
    }

    return FileLoadReq(file_no, (char *)adrs, priority, func, arg, FILE_LOAD_TYPE_DECODE_EE);
}

// ──────────────────────────────────────────────────────────────────────
// Queue an SPU-target load (compressed files are not supported on this path).

int FileLoadReqSPU(int file_no, void *adrs, int priority, FILE_LOAD_CALLBACK func, void *arg)
{
    if (cddatIsCmpFile(file_no) != 0)
    {
        PRINT_ASSERT("Cannot Do SPU Decode Load", "");
    }

    return FileLoadReq(file_no, (char *)adrs, priority, func, arg, FILE_LOAD_TYPE_SPU);
}

// ──────────────────────────────────────────────────────────────────────
// Queue an explicit EE-decode load.

int FileDecodeLoadReqEE(int file_no, void *adrs, int priority, FILE_LOAD_CALLBACK func, void *arg)
{
    return FileLoadReq(file_no, (char *)adrs, priority, func, arg, FILE_LOAD_TYPE_DECODE_EE);
}

// ──────────────────────────────────────────────────────────────────────
// Load-completion callback (used by FileLoadReqEEWait): wake the sleeping
// requester thread.

static void _intr_wait_load_func(void *buffer, void *arg)
{
    WakeupThread(*(int *)arg);
}

// ──────────────────────────────────────────────────────────────────────
// Queue a load and block the calling thread until it completes.

void FileLoadReqEEWait(int file_no, void *adrs)
{
    static int this_thread_id;      // sbss 3f4ffc

    this_thread_id = GetThreadId();
    FileLoadReqEE(file_no, adrs, 10, _intr_wait_load_func, &this_thread_id);
    SleepThread();
}

// ──────────────────────────────────────────────────────────────────────
// Withdraw the request at slot i (blocking Wait variant).  When it is the head
// (in-flight) request, ask the loader to cancel and sleep until it does; other-
// wise slide it to the tail and drop it.

static void FileLoadCancelWaitSub(int i)
{
    int two;

    if (i == file_load_sys.yet_files - 1)
    {
        file_load_sys.cancel_type = LOAD_CANCEL_WAIT;
        ReqQueryLoadCancel();
        if (file_load_wrk[i].type == FILE_LOAD_TYPE_DECODE_EE)
        {
            cmp_eeiopCancel();
        }
        ResumeThread(load_th_idx);
        SleepThread();
        return;
    }

    while (i < file_load_sys.yet_files - 1)
    {
        two = i + 1;
        SwapFileLoadWrk(i, two);
        i = two;
    }

    WaitSema(load_req_sema_id);
    file_load_sys.yet_files = file_load_sys.yet_files - 1;
    ResumeThread(load_th_idx);
}

// ──────────────────────────────────────────────────────────────────────
// Cancel-and-wait by file/address.

FILE_LOAD_ENUM FileLoadCancelWait2(int file_no, void *adrs)
{
    FILE_LOAD_WRK *wrk;
    int            i;

    SuspendThread(load_th_idx);
    wrk = file_load_wrk;
    for (i = 0; i < file_load_sys.yet_files; i++)
    {
        if ((wrk->file_no == file_no) && (wrk->buffer == (char *)adrs) &&
            (wrk->type != FILE_LOAD_TYPE_SPU))
        {
            FileLoadCancelWaitSub(i);
            return FILE_LOAD_OK;
        }
        wrk++;
    }

    ResumeThread(load_th_idx);
    return FILE_LOAD_ID_NOT_EXIST;
}

// ──────────────────────────────────────────────────────────────────────
// Cancel-and-wait by packed id.

FILE_LOAD_ENUM FileLoadCancelWait(int id)
{
    FILE_LOAD_WRK *wrk;
    int            i;

    SuspendThread(load_th_idx);
    for (i = 0; i < file_load_sys.yet_files; i++)
    {
        wrk = file_load_wrk + i;
        if (((int)wrk->id == (id >> 0x10)) &&
            ((unsigned int)wrk->temp_id == (id & 0xffffU)))
        {
            FileLoadCancelWaitSub(i);
            return FILE_LOAD_OK;
        }
    }

    ResumeThread(load_th_idx);
    return FILE_LOAD_ID_NOT_EXIST;
}

// ──────────────────────────────────────────────────────────────────────
// Withdraw the request at slot i (non-blocking variant).  When it is the head
// (in-flight) request, arm a normal cancel with the caller's callback; other-
// wise fire the callback now, slide the entry to the tail and drop it.

static void FileLoadCancelSub(int i, FILE_LOAD_CALLBACK func, void *arg)
{
    int two;

    if (i == file_load_sys.yet_files - 1)
    {
        file_load_sys.cancel_type = LOAD_CANCEL_NORMAL;
        file_load_sys.cancel_func = func;
        file_load_sys.cancel_arg  = arg;
        printf("load cancel req\n");
        ReqQueryLoadCancel();
        if (file_load_wrk[i].type == FILE_LOAD_TYPE_DECODE_EE)
        {
            cmp_eeiopCancel();
        }
    }
    else
    {
        if (func != (FILE_LOAD_CALLBACK)0)
        {
            (*func)(file_load_wrk[i].buffer, arg);
        }

        for (two = i + 1; i < file_load_sys.yet_files - 1; two++)
        {
            SwapFileLoadWrk(i, two);
            i = two;
        }
        
        WaitSema(load_req_sema_id);
        file_load_sys.yet_files = file_load_sys.yet_files - 1;
    }

    ResumeThread(load_th_idx);
}

// ──────────────────────────────────────────────────────────────────────
// Cancel by file/address (non-SPU), firing func on withdrawal.

FILE_LOAD_ENUM FileLoadCancel2(int file_no, void *adrs, FILE_LOAD_CALLBACK func, void *arg)
{
    FILE_LOAD_WRK *wrk;
    int            i;

    SuspendThread(load_th_idx);
    wrk = file_load_wrk;
    for (i = 0; i < file_load_sys.yet_files; i++)
    {
        if ((wrk->file_no == file_no) && (wrk->buffer == (char *)adrs) &&
            (wrk->type != FILE_LOAD_TYPE_SPU))
        {
            FileLoadCancelSub(i, func, arg);
            return FILE_LOAD_OK;
        }
        wrk++;
    }

    ResumeThread(load_th_idx);
    return FILE_LOAD_ID_NOT_EXIST;
}

// ──────────────────────────────────────────────────────────────────────
// Cancel an SPU load by file/address.

FILE_LOAD_ENUM FileLoadCancelSPU2(int file_no, void *adrs, FILE_LOAD_CALLBACK func, void *arg)
{
    FILE_LOAD_WRK *wrk;
    int            i;

    SuspendThread(load_th_idx);
    wrk = file_load_wrk;
    for (i = 0; i < file_load_sys.yet_files; i++)
    {
        if ((wrk->file_no == file_no) && (wrk->buffer == (char *)adrs) &&
            (wrk->type == FILE_LOAD_TYPE_SPU))
        {
            FileLoadCancelSub(i, func, arg);
            return FILE_LOAD_OK;
        }
        wrk++;
    }

    ResumeThread(load_th_idx);
    return FILE_LOAD_ID_NOT_EXIST;
}

// ──────────────────────────────────────────────────────────────────────
// Cancel by packed id.

FILE_LOAD_ENUM FileLoadCancel(int id, FILE_LOAD_CALLBACK func, void *arg)
{
    FILE_LOAD_WRK *wrk;
    int            i;

    SuspendThread(load_th_idx);
    for (i = 0; i < file_load_sys.yet_files; i++)
    {
        wrk = file_load_wrk + i;
        if (((int)wrk->id == (id >> 0x10)) &&
            ((unsigned int)wrk->temp_id == (id & 0xffffU)))
        {
            FileLoadCancelSub(i, func, arg);
            return FILE_LOAD_OK;
        }
    }

    printf("FileLoadCancel() FILE_LOAD_ID IS NOT EXIST\n");
    ResumeThread(load_th_idx);
    return FILE_LOAD_ID_NOT_EXIST;
}

// ──────────────────────────────────────────────────────────────────────
// Cancel every outstanding load and wait for the queue to drain.

FILE_LOAD_ENUM FileLoadCancelAll(void)
{
    if (file_load_sys.yet_files == 0)
    {
        return FILE_LOAD_ALREADY_ALL_LOAD;
    }

    SuspendThread(load_th_idx);
    file_load_sys.cancel_type = LOAD_CANCEL_WAIT;
    SwapFileLoadWrk(0, file_load_sys.yet_files - 1);

    for (; 1 < file_load_sys.yet_files; file_load_sys.yet_files = file_load_sys.yet_files - 1)
    {
        WaitSema(load_req_sema_id);
    }

    ReqQueryLoadCancel();
    if (file_load_wrk->type == FILE_LOAD_TYPE_DECODE_EE)
    {
        cmp_eeiopCancel();
    }
    ResumeThread(load_th_idx);
    SleepThread();
    printf("FileLoadCancelAll End()\n");
    return FILE_LOAD_OK;
}

// ──────────────────────────────────────────────────────────────────────
// qsort comparators: priority descending, then start sector descending.

static int cmpFileWrkPri(const void *a, const void *b)
{
    return ((FILE_LOAD_WRK *)b)->priority - ((FILE_LOAD_WRK *)a)->priority;
}

static int cmpFileWrkSector(const void *a, const void *b)
{
    return ((FILE_LOAD_WRK *)b)->start_sector - ((FILE_LOAD_WRK *)a)->start_sector;
}

// ──────────────────────────────────────────────────────────────────────
// Non-SPU poll by file/address: 1 if no matching request is still queued.

int FileLoadIsEnd2(int file_no, void *adrs)
{
    FILE_LOAD_WRK *wrk;
    int            i;
    int            ret;

    ret = 1;
    SuspendThread(load_th_idx);
    wrk = file_load_wrk;
    for (i = 0; i < file_load_sys.yet_files; i++)
    {
        if ((wrk->file_no == file_no) && (wrk->buffer == (char *)adrs) &&
            (wrk->type != FILE_LOAD_TYPE_SPU))
        {
            ret = 0;
        }
        wrk++;
    }
    ResumeThread(load_th_idx);

    return ret;
}

// ──────────────────────────────────────────────────────────────────────
// Poll by packed id: 1 if that request is no longer queued.

int FileLoadIsEnd(int id)
{
    FILE_LOAD_WRK *wrk;
    int            i;
    int            ret;

    ret = 1;
    SuspendThread(load_th_idx);
    wrk = file_load_wrk;
    for (i = 0; i < file_load_sys.yet_files; i++)
    {
        if (((int)wrk->id == (id >> 0x10)) &&
            ((unsigned int)wrk->temp_id == (id & 0xffffU)))
        {
            ret = 0;
        }
        wrk++;
    }
    ResumeThread(load_th_idx);

    return ret;
}

// ──────────────────────────────────────────────────────────────────────
// 1 once the queue is empty.

int AllFileLoadIsEnd(void)
{
    int yet;

    SuspendThread(load_th_idx);
    yet = file_load_sys.yet_files;
    ResumeThread(load_th_idx);

    return (yet < 1);
}

// ──────────────────────────────────────────────────────────────────────
// Work-buffer bytes the loader needs for `def` (the work array, plus the EE
// decode scratch when compressed files are enabled).

int FileLoadGetNeedSize(EEIOP_DEF *def)
{
    unsigned int size;

    size = GetAlignUp(def->file_load_wrk_num * sizeof(FILE_LOAD_WRK), 6);
    if (def->cmp_use_flg != 0)
    {
        size = size + cmp_eeiopGetWrkSize();
    }

    return size;
}

// ──────────────────────────────────────────────────────────────────────
// Swap request slots `one` and `two`.

static void SwapFileLoadWrk(int one, int two)
{
    FILE_LOAD_WRK temp;

    temp                = file_load_wrk[one];
    file_load_wrk[one]  = file_load_wrk[two];
    file_load_wrk[two]  = temp;
}

// ──────────────────────────────────────────────────────────────────────
// Register a request in the queue.  New requests are appended; the last two
// slots are swapped so the freshly added entry sits at the tail (the loader
// services the tail), preserving the previous head.  If the new request
// out-prioritises the in-flight one, poke a cancel so it can be re-picked.
// Returns the packed id (id << 16) | temp_id.

static int FileLoadReq(int file_no, char *adrs, int priority, FILE_LOAD_CALLBACK func,
                       void *arg, FILE_LOAD_TYPE type)
{
    FILE_LOAD_WRK *now_load_wrk;
    FILE_LOAD_WRK *wrk;
    int            ret;

    if (file_no < 0)
    {
        PRINT_ASSERT("Requested FileNo Is Under Zero", "");
    }

    SuspendThread(load_th_idx);

    if (file_load_sys.file_load_wrk_max <= file_load_sys.yet_files + 1)
    {
        PRINT_ASSERT("RegisteredFiles Is Over LoadWrkNum %d", file_load_sys.file_load_wrk_max);
    }

    if (file_load_sys.yet_files == 0)
    {
        now_load_wrk = (FILE_LOAD_WRK *)0;
        wrk          = file_load_wrk;
    }
    else
    {
        // Swap the current head with the slot after it, then take that slot.
        SwapFileLoadWrk(file_load_sys.yet_files, file_load_sys.yet_files - 1);
        now_load_wrk = file_load_wrk + (file_load_sys.yet_files - 1);
        wrk          = file_load_wrk + file_load_sys.yet_files - 1;
    }

    wrk->buffer   = adrs;
    wrk->func     = func;
    wrk->arg      = arg;
    wrk->file_no  = file_no;
    wrk->start_sector = GetFileStartSector(file_no);

    if (wrk->start_sector < 0)
    {
        PRINT_ASSERT("StartSector(File[%d]) Is Under 0", wrk->start_sector);
    }

    wrk->type      = type;
    wrk->priority  = priority;
    wrk->read_size = 0;
    wrk->size      = GetFileCmpSize(file_no);

    file_load_sys.yet_files++;
    wrk->temp_id = file_load_wrk_id;
    file_load_wrk_id++;
    SignalSema(load_req_sema_id);

    if (now_load_wrk == (FILE_LOAD_WRK *)0)
    {
        ret = wrk->id;
    }
    else if ((file_load_sys.cancel_type == LOAD_CANCEL_NONE) &&
             ((unsigned int)now_load_wrk->type < 2) &&
             (wrk->priority < now_load_wrk->priority))
    {
        ReqQueryLoadCancel();
        file_load_sys.cancel_type = LOAD_CANCEL_STORE;
        ret = wrk->id;
    }
    else
    {
        ret = wrk->id;
    }

    ResumeThread(load_th_idx);
    return ret << 0x10 | wrk->temp_id;
}

// ──────────────────────────────────────────────────────────────────────
// SIF-RPC completion interrupt: signal the RPC sema.

static void intr_SifEnd_FileLoad(void *data)
{
    iSignalSema(rpc_sema_id);
    SYNC(0);
    EI();
}

// ──────────────────────────────────────────────────────────────────────
// Build the IOP load-request command for `wrk` and fire it over SIF RPC.  For
// a decode load the request is produced by cmp_eeiop; otherwise the raw ring-
// buffer transfer is described directly.  Compressed files load from
// "<name-with-dot-as-underscore>.cmp".


static void rpcFileLoadReqSub(FILE_LOAD_WRK *wrk)
{
    LOAD_REQ_NEW *cmd;
    LOAD_REQ_NEW  req;
    char         *name;
    char         *dot_ptr;

    cmd = (LOAD_REQ_NEW *)iop_fileload_cmd;

    if (wrk->type == FILE_LOAD_TYPE_DECODE_EE)
    {
        req = cmp_eeiopCreateDecodeThread(wrk->size, (intptr_t)wrk->buffer,
                                          wrk->start_sector, file_load_sys.decode_th_priority);
        *cmd = req;
    }
    else
    {
        cmd->type         = wrk->type;
        cmd->adrs         = (intptr_t)wrk->buffer + wrk->read_size;
        cmd->ld.size      = wrk->size - wrk->read_size;
        cmd->ld.start_sector = wrk->start_sector + (wrk->read_size >> 0xb);
    }

    cmd->ld.ring_buf_num = file_load_sys.ring_buf_num;
    cmd->ld.one_buf_size = file_load_sys.one_buf_size;

    if (cddatIsCmpFile(wrk->file_no) == 0)
    {
        GetFileNameBuffer(wrk->file_no, cmd->ld.file_name);
    }
    else
    {
        GetFileNameBuffer(wrk->file_no, cmd->ld.file_name);

        // Turn the last '.' into '_', then append the ".cmp" extension.
        dot_ptr = (char *)0;
        for (name = cmd->ld.file_name; *name != '\0'; name++)
        {
            if (*name == '.')
            {
                dot_ptr = name;
            }
        }
        if (dot_ptr != (char *)0)
        {
            *dot_ptr = '_';
        }
        strcat(cmd->ld.file_name, ".cmp");
    }

    if (cmd->adrs == 0)
    {
        if (wrk != (FILE_LOAD_WRK *)0 && wrk->buffer != (char *)0)
        {
            cmd->adrs = (intptr_t)wrk->buffer + wrk->read_size;
        }
    }

    if (cmd->adrs == 0)
    {
        PRINT_ASSERT("Load Adrs Is NULL!!", "");
    }

    FlushCache(0);

    // On the PS2 the command above is dispatched to the IOP file server over
    // SIF RPC; the PC port hands the request to the miopan host loader instead
    // (see miopan/io/miopan_fileload.cpp).  The RPC-completion bookkeeping
    // (reply buffer + intr_SifEnd) stays here on the loader side.
    //
    // KEEP IT THIS WAY.  system/iop/ now really runs, and its RPC 3 service
    // (iopCommandLoad) is registered and reachable -- but routing loads back
    // through it would mean the ROM's ring buffer, its per-frame command queue
    // and a thread hand-off per block.  The host loader serves the request
    // outright on this thread, which is what makes loading here fast and
    // responsive; the IOP is wired up for sound, not for file I/O.
    {
        FILE_LOAD_RET      *fret = (FILE_LOAD_RET *)iop_fileload_ret;
        MioPan_FileLoadReq  hreq;
        char                raw_file_name[256];

        fret->read_size  = 0;
        fret->cancel_flg = 0;

        if (wrk == (FILE_LOAD_WRK *)0)
        {
            intr_SifEnd_FileLoad((void *)0);
            return;
        }

        // A sound bank body goes to SPU memory, so cmd->adrs is an SPU
        // address rather than an EE one and the host loader has to be told.
        hreq.is_spu = (cmd->type == FILE_LOAD_TYPE_SPU);
        hreq.is_cmp = cddatIsCmpFile(wrk->file_no);
        if (hreq.is_cmp != 0)
        {
            GetFileNameBuffer(wrk->file_no, raw_file_name);
        }
        else
        {
            raw_file_name[0] = '\0';
        }

        hreq.ee_adrs       = (uintptr_t)cmd->adrs;
        hreq.request_size  = cmd->ld.size;
        hreq.start_sector  = cmd->ld.start_sector;
        hreq.cmp_file_name = cmd->ld.file_name;
        hreq.raw_file_name = raw_file_name;
        hreq.raw_size      = GetFileSize(wrk->file_no);
        hreq.read_offset   = wrk->read_size;
        hreq.wrk_size      = wrk->size;
        hreq.file_no       = wrk->file_no;

        fret->read_size = MioPan_FileLoadServe(&hreq);

        FlushCache(0);
        intr_SifEnd_FileLoad((void *)0);
    }
}

// ──────────────────────────────────────────────────────────────────────
// The loader thread.  Each pass: wait for a queued request, sort the queue
// (priority, then sector), fire the tail request to the IOP, wait for the RPC
// to complete, then honour any pending cancel or - on completion - print the
// load, drop the entry and fire its callback.

static void thFileLoad(void *dummy)
{
    FILE_LOAD_WRK *wrk;
    int            i;
    int            end_flg;
    int            part_sort_start;
    int            priority;
    char           aStr[300];

    for (;;)
    {
        WaitSema(load_req_sema_id);

        // Everything from here to the end of rpcFileLoadReqSub() reads queue
        // entries, and FileLoadReq() reorders them (SwapFileLoadWrk moves the
        // in-flight entry out of the tail slot and puts the new request there).
        // On the PS2 SuspendThread() froze this thread outright; here it is only
        // a mutex, so claim it or the request gets built from one file's slot
        // and served with another's.
        MioPan_LoaderCriticalEnter();

        // Sort the whole queue by priority (descending) ...
        qsort(file_load_wrk, file_load_sys.yet_files, sizeof(FILE_LOAD_WRK), cmpFileWrkPri);

        // ... then sort each equal-priority run by start sector.
        part_sort_start = 0;
        priority = file_load_wrk[0].priority;
        for (i = 0; i < file_load_sys.yet_files; i++)
        {
            if (file_load_wrk[i].priority != priority)
            {
                qsort(file_load_wrk + part_sort_start, i - part_sort_start, sizeof(FILE_LOAD_WRK),
                      cmpFileWrkSector);
                part_sort_start = i;
                priority = file_load_wrk[i].priority;
            }
        }
        qsort(file_load_wrk + part_sort_start, i - part_sort_start, sizeof(FILE_LOAD_WRK), cmpFileWrkSector);

        // Fire the tail request.
        wrk = file_load_wrk + file_load_sys.yet_files - 1;
        {
            FILE_LOAD_TYPE fired_type = wrk->type;

            rpcFileLoadReqSub(wrk);
            MioPan_LoaderCriticalLeave();

            if (fired_type == FILE_LOAD_TYPE_DECODE_EE)
            {
                cmp_eeiopWaitSema();
            }
        }

        end_flg = 0;
        WaitSema(rpc_sema_id);

        // PORT: re-claim the loader mutex before deriving the in-flight entry
        // again.  On the PS2 SuspendThread() froze this thread for the whole
        // span, so recomputing it from yet_files was atomic with FileLoadReq().
        // Here the mutex was dropped above, and a request landing in this window
        // runs SwapFileLoadWrk() + yet_files++ while this thread is indexing --
        // the re-read then lands on a *different* request's slot, firing that
        // entry's callback a second time and dropping the served entry's
        // callback entirely.  Observed as a sound bank whose .bd half never went
        // m_Ready, roughly two runs in three (base_sys_bd.bd is big enough that
        // other requests reliably pile up behind it).
        MioPan_LoaderCriticalEnter();

        wrk = file_load_wrk + file_load_sys.yet_files - 1;

        // Both callbacks are captured under the lock and fired after it is
        // released: they run arbitrary game code (frees, WakeupThread) and must
        // not hold the loader mutex while doing it.
        FILE_LOAD_CALLBACK done_func   = (FILE_LOAD_CALLBACK)0;
        char              *done_buffer = (char *)0;
        void              *done_arg    = (void *)0;
        FILE_LOAD_CALLBACK cxl_func    = (FILE_LOAD_CALLBACK)0;
        char              *cxl_buffer  = (char *)0;
        void              *cxl_arg     = (void *)0;

        if (file_load_sys.cancel_type == LOAD_CANCEL_STORE)
        {
            // Partial transfer: account the bytes read; requeue if more remain.
            wrk->read_size = wrk->read_size + ((FILE_LOAD_RET *)iop_fileload_ret)->read_size;
            if (wrk->read_size < wrk->size)
            {
                SignalSema(load_req_sema_id);
            }
            else
            {
                end_flg = 1;
            }
        }
        else if (file_load_sys.cancel_type == LOAD_CANCEL_NONE)
        {
            end_flg = 1;
        }
        else
        {
            if (file_load_sys.cancel_type == LOAD_CANCEL_WAIT)
            {
                WakeupThread(file_load_sys.master_thread_id);
            }
            else if (file_load_sys.cancel_type != LOAD_CANCEL_NORMAL)
            {
                PRINT_ASSERT("load_cancel_type err", "");
                goto cancel_done;
            }

            if (file_load_sys.cancel_func != (FILE_LOAD_CALLBACK)0)
            {
                cxl_func   = file_load_sys.cancel_func;
                cxl_buffer = wrk->buffer;
                cxl_arg    = file_load_sys.cancel_arg;
            }
            file_load_sys.yet_files = file_load_sys.yet_files - 1;
        }

cancel_done:
        file_load_sys.cancel_type = LOAD_CANCEL_NONE;

        if (end_flg != 0)
        {
            GetFileNameBuffer(wrk->file_no, aStr);
            if (cddatIsCmpFile(wrk->file_no) == 0)
            {
                printf("LoadEnd no[% 4d] id[%06x] adrs[%p] size[%x] [%s]\n",
                       wrk->file_no, (wrk->id << 0x10) | wrk->temp_id,
                       wrk->buffer, wrk->size, aStr);
            }
            else
            {
                printf("LoadEnd no[% 4d] id[%06x] adrs[%p] size[%x] [%s]"
                       " << This Is Compressed File\n",
                       wrk->file_no, (wrk->id << 0x10) | wrk->temp_id,
                       wrk->buffer, wrk->size, aStr);
            }
            file_load_sys.yet_files = file_load_sys.yet_files - 1;
            done_func   = wrk->func;
            done_buffer = wrk->buffer;
            done_arg    = wrk->arg;
        }

        MioPan_LoaderCriticalLeave();

        if (cxl_func != (FILE_LOAD_CALLBACK)0)
        {
            (*cxl_func)(cxl_buffer, cxl_arg);
        }
        if (done_func != (FILE_LOAD_CALLBACK)0)
        {
            (*done_func)(done_buffer, done_arg);
        }
    }
}

// ──────────────────────────────────────────────────────────────────────
// Retune the loader / decode thread priorities at runtime.

void FileLoadChangeThreadPriority(int load_th_priority, int decode_th_priority)
{
    ChangeThreadPriority(load_th_idx, load_th_priority);
    file_load_sys.load_th_priority = load_th_priority;
    cmp_eeiopChangePriority(decode_th_priority);
    file_load_sys.decode_th_priority = decode_th_priority;
}

// ──────────────────────────────────────────────────────────────────────
// Dump every still-queued request (debug).

void PrintFileLoadWrk(void)
{
    char *name;
    int   i;

    for (i = 0; i < file_load_sys.yet_files; i++)
    {
        name = GetFileName(file_load_wrk[i].file_no);
        printf("yet[%d] name[%s], func[%x], id[%d] sector[%d]\n",
               i, name, file_load_wrk[i].func, file_load_wrk[i].id,
               file_load_wrk[i].start_sector);
    }
}

// ──────────────────────────────────────────────────────────────────────
// Lay the work array (and, if enabled, the decode scratch) out of wrk_buffer,
// bind the IOP file service, create the loader semaphores and thread, and
// return the next free byte of the work buffer.

void *FileLoadInit(EEIOP_DEF *def, void *wrk_buffer)
{
    struct SemaParam  semap;
    struct ThreadParam thp;
    void             *next;
    int               i;

    file_load_sys.file_load_wrk_max = def->file_load_wrk_num;
    file_load_wrk = (FILE_LOAD_WRK *)wrk_buffer;
    next = (void *)((intptr_t)wrk_buffer +
                    GetAlignUp(def->file_load_wrk_num * sizeof(FILE_LOAD_WRK), 6));

    // Stamp each slot's fixed id.
    for (i = 0; i < file_load_sys.file_load_wrk_max; i++)
    {
        file_load_wrk[i].id = i;
    }

    file_load_sys.yet_files        = 0;
    file_load_sys.master_thread_id = GetThreadId();
    file_load_sys.one_buf_size     = 0xa000;
    file_load_sys.ring_buf_num     = 4;
    file_load_sys.cancel_type      = LOAD_CANCEL_NONE;

    // Bind the IOP file server (spin until the service registers).
    do
    {
        if (sceSifBindRpc(&sif_cli_data_f, 3, 0) < 0)
        {
            printf("error: sceSifBindRpc QUERY\n");
            for (;;)
            {
            }
        }
        for (i = 0x270b; i != -1; i -= 4)
        {
        }
    } while (sif_cli_data_f.serve == (struct _sif_serve_data *)0);

    semap.initCount = 0;
    semap.maxCount  = 1;
    rpc_sema_id = CreateSema(&semap);

    semap.initCount = 0;
    semap.maxCount  = file_load_sys.file_load_wrk_max;
    load_req_sema_id = CreateSema(&semap);

    if (def->cmp_use_flg != 0)
    {
        next = cmp_eeiopInit(next);
    }

    // The loader runs one priority below its creator; decode one above.
    ReferThreadStatus(file_load_sys.master_thread_id, &thp);
    file_load_sys.decode_th_priority = thp.currentPriority + 1;
    file_load_sys.load_th_priority   = thp.currentPriority - 1;

    thp.entry        = thFileLoad;
    thp.stack        = file_load_stack;
    thp.stackSize    = 0x1000;
    thp.gpReg        = (void *)0x3f57f0;    // loader thread's $gp
    thp.initPriority = file_load_sys.load_th_priority;
    thp.option       = 0;
    load_th_idx = CreateThread(&thp);
    StartThread(load_th_idx, 0);

    return next;
}
