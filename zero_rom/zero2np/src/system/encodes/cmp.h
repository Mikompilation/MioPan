/* ==========================================================================
 *  system/encodes/cmp.h
 *
 *  cmp.o -- the CMP archive block decoder.  Three functions, 0x12c bytes of
 *  .text at 0x278d00, no static data at all.
 *
 *  A .cmp file is CMP_HEADER, then an ENCODE_DIV_SECTION table, then the
 *  payload.  The payload is cut into `div_num` blocks that each expand to
 *  exactly `div_size` bytes; the table says, per block, whether it was stored
 *  raw or squeezed with the LZSS coder in encodes.c, and how many *compressed*
 *  bytes it occupies.
 *
 *  `data_offset` and `div_p` arrive as byte offsets from the head of the
 *  header.  CMP_Init() rewrites both in place into absolute EE addresses and
 *  raises `mapping` so it only ever happens once -- which is why every other
 *  function here treats them as addresses rather than offsets.  The port has
 *  to deviate on that point; cmp.c's banner says how and why.
 *
 *  Reconstructed from the Feb 6 2004 prototype (SLES_523.84).
 * ======================================================================== */

#ifndef _SYSTEM_ENCODES_CMP_H
#define _SYSTEM_ENCODES_CMP_H

#include <stdint.h>
#include <sys/types.h>

typedef enum _ENCODE_TYPE
{
    ENCODE_TYPE_NONE       = 0,     /* stored -- memcpy the block straight out */
    ENCODE_TYPE_SLIDE      = 1,     /* LZSS   -- hand the block to SlideDecode */
    ENCODE_TYPE_FORCE_WORD = 0xffff /* widens the enum to 32 bits; never stored */
} ENCODE_TYPE;

typedef struct _ENCODE_DIV_SECTION      /* 0x4 */
{
    /* 0x0 */ short int      type;   /* an ENCODE_TYPE, stored as a halfword */
    /* 0x2 */ unsigned short size;   /* COMPRESSED size; the expanded size is
                                      * always CMP_HEADER::div_size */
} ENCODE_DIV_SECTION;

typedef struct _CMP_HEADER              /* 0x20 */
{
    /* 0x00 */ int size;            /* expanded byte count of the whole file */
    /* 0x04 */ int ext;
    /* 0x08 */ int div_size;        /* expanded byte count of ONE block */
    /* 0x0c */ int div_num;         /* number of blocks */
    /* 0x10 */ int data_offset;     /* -> payload  (see div_p) */
    /* 0x14 */ int div_p;           /* -> ENCODE_DIV_SECTION[div_num].
                                     *
                                     * The ROM declares this `ENCODE_DIV_SECTION *`
                                     * (types.txt), which is 4 bytes on the EE.  It
                                     * stays an int here so the struct keeps its
                                     * on-disc 0x20 layout on a 64-bit host -- a
                                     * real pointer would push it to 0x28 and move
                                     * `mapping` and `cmp_size`.  Both this and
                                     * data_offset hold a byte offset from the head
                                     * of the header; see cmp.c's banner. */
    /* 0x18 */ int mapping;         /* 0 = offsets not yet resolved */
    /* 0x1c */ int cmp_size;        /* compressed byte count of the whole file */
} CMP_HEADER;

/* from_adrs / to_adrs are plain `int` in the ROM -- EE pointers are 4 bytes.
 * They carry addresses, so they have to widen on the host. */
void CMP_DecodeOne(CMP_HEADER *header, int no, uintptr_t from_adrs, uintptr_t to_adrs);
int  CMP_Decode(CMP_HEADER *header, void *decode_buf);
void CMP_Init(CMP_HEADER *header);

#endif /* _SYSTEM_ENCODES_CMP_H */
