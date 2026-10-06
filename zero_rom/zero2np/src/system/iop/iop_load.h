/* ==========================================================================
 *  system/iop/iop_load.h
 *
 *  The file-load RPC service (iop_load.c), RPC number 3.  It is the IOP end
 *  of the EE's fileload.c: one request loads one file, either straight to EE
 *  memory, straight into SPU RAM, or through the EE-side decompressor.
 *
 *  Reconstructed from iopsys.irx (Feb 6 2004 prototype, SLES_523.84).
 * ======================================================================== */

#ifndef _SYSTEM_IOP_IOP_LOAD_H
#define _SYSTEM_IOP_IOP_LOAD_H

#include <stdint.h>

#include "iop_types.h"

/* Where a load is to end up.  DECODE_EE is the compressed path; DECODE_IOP is
 * declared but never reached -- thTransMem() asserts on anything but the
 * first two, and iopCommandLoad() routes 2 to thTransMemDecode(). */
typedef enum _FILE_LOAD_TYPE
{
    FILE_LOAD_TYPE_EE          = 0,
    FILE_LOAD_TYPE_SPU         = 1,
    FILE_LOAD_TYPE_DECODE_EE   = 2,
    FILE_LOAD_TYPE_DECODE_IOP  = 3,
    FILE_LOAD_TYPE_FORCE_DWORD = -1
} FILE_LOAD_TYPE;

typedef struct _LOAD_REQ_NEW        /* 0x11c */
{
    /* 0x000 */ FILE_LOAD_TYPE  type;
    /* 0x004 */ intptr_t        tmp_ee_adrs;   /* scratch, decode path only */
    /* 0x008 */ intptr_t        adrs;          /* EE or SPU destination     */
    /* PORT: both widened from `int`; they must match the EE's copy in
     * system/eeiop/fileload.h, which was widened the same way. */
    /* 0x00c */ LOAD_DEF_STRUCT ld;
} LOAD_REQ_NEW;

typedef struct _FILE_LOAD_RET       /* 0x8 */
{
    /* 0x0 */ int read_size;
    /* 0x4 */ int cancel_flg;
} FILE_LOAD_RET;

/* One entry per division of a compressed file. */
typedef struct _ENCODE_DIV_SECTION  /* 0x4 */
{
    /* 0x0 */ short          type;
    /* 0x2 */ unsigned short size;
} ENCODE_DIV_SECTION;

/* The header at the front of a compressed file.  This is read straight off
 * the disc image -- thTransMemDecode() casts a ring-buffer slot to it and
 * steps past it by sizeof(CMP_HEADER) to reach the division table -- so the
 * layout has to stay exactly 0x20 bytes.  The EE's own copy of this record is
 * in system/encodes/cmp.h and matches field for field. */
typedef struct _CMP_HEADER          /* 0x20 */
{
    /* 0x00 */ int size;
    /* 0x04 */ int ext;
    /* 0x08 */ int div_size;
    /* 0x0c */ int div_num;
    /* 0x10 */ int data_offset;
    /* 0x14 */ int div_p;           /* -> ENCODE_DIV_SECTION[div_num].
                                     *
                                     * PORT: the ROM declares this
                                     * `ENCODE_DIV_SECTION *` (iopsys_types.h),
                                     * which is 4 bytes on the IOP.  It stays
                                     * an int here so the struct keeps its
                                     * on-disc 0x20 layout on a 64-bit host --
                                     * a real pointer is aligned to 8, which
                                     * puts div_p itself at 0x18, mapping at
                                     * 0x20, cmp_size at 0x24 and sizeof at
                                     * 0x28.  That last one is the damaging
                                     * part: the division table is then read
                                     * from 8 bytes past its real start.
                                     * Nothing on this side follows the value.
                                     * Same change, same reason, as cmp.h. */
    /* 0x18 */ int mapping;         /* 0 = offsets not yet resolved */
    /* 0x1c */ int cmp_size;
} CMP_HEADER;

/* Asks the in-flight load to give up; the reply carries cancel_flg set and
 * how much had already been transferred. */
void ClearLoadReq(void);

/* Spawns the service thread. */
void CreateRPCLoadThread(void);

#endif /* _SYSTEM_IOP_IOP_LOAD_H */
