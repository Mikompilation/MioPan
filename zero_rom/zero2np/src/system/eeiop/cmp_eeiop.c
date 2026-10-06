// FILE: /home/akira_koide/zero2np/src/system/eeiop/cmp_eeiop.c
//
// STUB - EE-side decompression path for the file loader.  Not yet reverse-
// engineered; placeholder bodies for the entry points fileload.c calls.
// Signatures from functions.txt (addresses 0x275c90 .. 0x2760c0); bodies await
// reconstruction.
//
// Reconstructed from the Feb 6 2004 prototype (SLES_523.84).

#include "cmp_eeiop.h"              // this file's public API + LOAD_REQ_NEW

// TODO: reverse-engineer cmp_eeiop.c (decode thread, ring-buffer inflate,
//       cmp_eeiopDecodeLoadMain / intr_cmp / ...).

void *cmp_eeiopInit(void *wrk_buffer)
{
    return wrk_buffer;
}

int cmp_eeiopGetWrkSize(void)
{
    return 0;
}

LOAD_REQ_NEW cmp_eeiopCreateDecodeThread(int size, intptr_t adrs, int start_sector, int priority)
{
    LOAD_REQ_NEW req;

    req.type        = FILE_LOAD_TYPE_DECODE_EE;
    req.tmp_ee_adrs = 0;
    req.adrs        = adrs;
    req.ld.one_buf_size = 0;
    req.ld.ring_buf_num = 0;
    req.ld.start_sector = start_sector;
    req.ld.size         = size;
    req.ld.file_name[0] = '\0';

    return req;
}

void cmp_eeiopWaitSema(void)
{
}

int cmp_eeiopIsLate(void)
{
    return 0;
}

void cmp_eeiopCancel(void)
{
}

void cmp_eeiopChangePriority(int priority)
{
}
