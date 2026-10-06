/* ==========================================================================
 *  system/iop/iop_types.h
 *
 *  Records shared across iopsys.irx -- the IOP half of the EE<->IOP sound and
 *  file system.  The EE side of each of these lives under
 *  zero_rom/zero2np/src/system/eeiop/; where the two must agree byte for byte
 *  the EE's name is noted beside it.
 *
 *  PORT NOTE ON PLACEMENT: the ROM's own header names are not recoverable.
 *  iopsys.irx's debug info gives types, globals and function signatures but no
 *  per-header attribution, and Ghidra reports no source line numbers for this
 *  module at all -- so unlike the EE files these carry no trailing ROM-line
 *  annotations.  Layouts are verbatim from iopsys_types.h.
 *
 *  Reconstructed from iopsys.irx (Feb 6 2004 prototype, SLES_523.84).
 * ======================================================================== */

#ifndef _SYSTEM_IOP_IOP_TYPES_H
#define _SYSTEM_IOP_IOP_TYPES_H

#include <stdint.h>

/* --------------------------------------------------------------------------
 *  Hardware and scheduling vocabulary
 * ------------------------------------------------------------------------ */

/* Which of the SPU2's two cores.  The value is also the low bit of every libsd
 * register entry, so `core` is OR'd straight into them. */
typedef enum _enum__SPU_CORE
{
    SPU_CORE_1   = 0,
    SPU_CORE_2   = 1,
    SPU_CORE_NUM = 2
} enum_SPU_CORE;

/* Every thread the module creates, in one place.  Smaller is more urgent, so
 * this list reads worst-to-best: the two stream threads outrank the disc
 * reader, which outranks the loaders, which outrank the main command queue.
 * PRI_IOP_SND is far below everything -- it only ramps volumes, and the hard
 * timer wakes it 2000 times a second regardless. */
typedef enum _THREAD_PRIORITY_ENUM
{
    PRI_DUMMY             = 10,
    PRI_STREAM_VOICE      = 11,
    PRI_IOP_READ          = 12,
    PRI_LOAD_STREAM_TRANS = 13,
    PRI_PCM_STREAM_READ   = 14,
    PRI_STREAM_READ       = 15,
    PRI_QUERY_RPC         = 16,
    PRI_LOAD_IOP          = 17,
    PRI_LOAD_TRANS        = 18,
    PRI_MAIN              = 19,
    PRI_IOP_SND           = 90
} THREAD_PRIORITY_ENUM;

/* --------------------------------------------------------------------------
 *  SPU ADPCM blocks
 * ------------------------------------------------------------------------ */

/* The flag byte at the head of every 16-byte ADPCM block.  SPU_STOP_BLOCK is
 * loop-start | loop-end | end all at once, which is what makes the silent
 * block iop_snd.c parks retired voices on loop on itself forever. */
typedef enum _enumSPU_LOOP
{
    SPU_LOOP_NON    = 0,
    SPU_END         = 1,
    SPU_LOOP        = 2,
    SPU_LOOP_END    = 3,
    SPU_LOOP_START  = 6,
    SPU_STOP_BLOCK  = 7
} enumSPU_LOOP;

typedef struct _SPU_BLOCK_HEADER    /* 0x2 */
{
    /* 0x0 */ unsigned char decode_param;
    /* 0x1 */ unsigned char loop;   /* an enumSPU_LOOP */
} SPU_BLOCK_HEADER;

typedef struct _SPU_BLOCK_DATA      /* 0x10 */
{
    /* 0x0 */ SPU_BLOCK_HEADER header;
    /* 0x2 */ unsigned short   data[7];
} SPU_BLOCK_DATA;

/* --------------------------------------------------------------------------
 *  Stream state reported back to the EE
 * ------------------------------------------------------------------------ */

/* Same enum as the EE's EEIOP_STREAM_STATUS (system/eeiop/snd_def.h). */
typedef enum _EEIOP_STREAM_STATUS
{
    ST_STREAM_NO_USE      = 0,
    ST_STREAM_HEADER_LOAD = 1,
    ST_STREAM_PRE_LOAD    = 2,
    ST_STREAM_PLAYING     = 3,
    ST_STREAM_WAIT_END    = 4,
    ST_STREAM_END         = 5,
    ST_STREAM_START       = 6,
    ST_STREAM_PRE_NO_USE  = 7,
    ST_STREAM_FORCE_DWORD = -1
} EEIOP_STREAM_STATUS;

typedef struct _IOP_STREAM_RET      /* 0x8 */
{
    /* 0x0 */ int status;           /* an EEIOP_STREAM_STATUS */
    /* 0x4 */ int offset;
} IOP_STREAM_RET;

/* The block DMA'd back to the EE once a frame; the EE reads it through
 * ee_iop.c's CheckEndPointThrough() / GetStreamWrkRet() / GetVoiceNowAdrs().
 * `voice_end` is a bit per voice, raised once that voice has run past its end
 * point. */
typedef struct _IOP_RET_STATUS      /* 0x1a8 */
{
    /* 0x000 */ int            voice_end[2];
    /* 0x008 */ IOP_STREAM_RET stream_ret[2];
    /* 0x018 */ IOP_STREAM_RET pcm_stream_ret[2];
    /* 0x028 */ int            mpLoopAdrs[2][24];
    /* 0x0e8 */ int            mpNowAdrs[2][24];
} IOP_RET_STATUS;

/* --------------------------------------------------------------------------
 *  Disc reading
 * ------------------------------------------------------------------------ */

/* What to read and where to put it.  Identical to the EE's LOAD_DEF_STRUCT. */
typedef struct _LOAD_DEF_STRUCT     /* 0x110 */
{
    /* 0x000 */ int  one_buf_size;
    /* 0x004 */ int  ring_buf_num;
    /* 0x008 */ int  start_sector;
    /* 0x00c */ int  size;
    /* 0x010 */ char file_name[256];
} LOAD_DEF_STRUCT;

/* One outstanding sector read, handed to the CD read thread. */
typedef struct _IOP_READ_WRK        /* 0x10 */
{
    /* 0x0 */ unsigned int offset_sector;
    /* 0x4 */ unsigned int read_sector_num;
    /* 0x8 */ void        *buf;
    /* 0xc */ char        *pname;
} IOP_READ_WRK;

/* --------------------------------------------------------------------------
 *  Ring buffer (iop_ring_buf.c)
 *
 *  A producer thread fills `ring_buf_num` slots of `one_buf_size` bytes off
 *  the disc while a consumer drains them; `trans_idx`/`trans_offset` are the
 *  consumer's cursor and `now_adrs` the producer's.
 * ------------------------------------------------------------------------ */
typedef struct _RING_BUF_WRK        /* 0x12c */
{
    /* 0x000 */ int             ring_buf_sema;
    /* 0x004 */ int             read_dat_sema;
    /* 0x008 */ uintptr_t       ring_buf_top;   /* PORT: was int -- an address */
    /* 0x00c */ int             trans_idx;
    /* 0x010 */ int             trans_offset;
    /* 0x014 */ uintptr_t       now_adrs;       /* PORT: was int -- an address */
    /* 0x018 */ int             load_end_flg;
    /* 0x01c */ LOAD_DEF_STRUCT ld;
} RING_BUF_WRK;

/* iop_load.c: a ring buffer plus where the decoded result goes. */
typedef struct _LOAD_IOP_WRK        /* 0x134 */
{
    /* 0x000 */ RING_BUF_WRK wrk;
    /* 0x12c */ intptr_t     adrs;          /* PORT: was int -- an address */
    /* 0x130 */ intptr_t     tmp_ee_adrs;   /* PORT: was int -- an address */
} LOAD_IOP_WRK;

/* iop_load_stream.c: the same, plus its own reader thread id. */
typedef struct _STM_IOP_WRK         /* 0x130 */
{
    /* 0x000 */ int          read_th;
    /* 0x004 */ RING_BUF_WRK rb_wrk;
} STM_IOP_WRK;

#endif /* _SYSTEM_IOP_IOP_TYPES_H */
