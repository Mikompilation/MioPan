// FILE: /home/akira_koide/zero2np/src/system/encodes/cmp.c
//
// cmp.o -- the CMP archive block decoder.  Three exports, 0x12c bytes of .text
// at 0x278d00, and no data sections at all.  It is the whole of the game's
// compressed-file format: walk a table of blocks, and either memcpy a block or
// hand it to encodes.c's LZSS decoder.
//
// The ROM's own consumer is NOT CMP_Decode().  cmp_eeiop.c's decode thread
// (the static body at 0x275f78) calls CMP_Init() once and then drives
// CMP_DecodeOne() from a hand-rolled loop of its own, so that it can yield
// between blocks and track how far behind the disc read it is.  A jal/j scan
// of both PT_LOADs finds exactly three call sites -- CMP_Init from 0x275fa0,
// CMP_DecodeOne from 0x276024 and from CMP_Decode itself -- so **CMP_Decode is
// exported but dead in this build**.  The port does use it: miopan_fileload.cpp
// reads a whole .cmp into a host buffer and expands it in one go, which is the
// job CMP_Decode already does.
//
// A BLOCK'S TWO SIZES.  ENCODE_DIV_SECTION::size is the *compressed* extent, so
// it steps the input cursor; every block expands to exactly CMP_HEADER::div_size,
// which steps the output cursor.  The input step is rounded with
// GetAlignUp(size, 4) -- and that second argument is a shift count, not a
// modulus, so blocks start on 16-byte boundaries, not 4.
//
// PORT DEVIATION -- CMP_Init()'s in-place fixup.
//   In the ROM, `data_offset` and `div_p` arrive as byte offsets from the head
//   of the header and CMP_Init() rewrites both into absolute addresses:
//
//        header->data_offset = (int)header + header->data_offset;    /* 62 */
//        header->div_p       = (int)header + (int)header->div_p;     /* 63 */
//
//   Both are 32-bit fields of an on-disc record, so on a 64-bit host they
//   cannot hold a pointer, and widening them would move `mapping` and
//   `cmp_size` off their file offsets.  The port therefore leaves the two
//   fields header-relative -- the representation the file already uses -- and
//   adds the base at each of the four use sites instead.  CMP_Init() keeps its
//   `mapping` guard so a doubled call is still harmless, and because the only
//   readers of either field are the two functions below, the two
//   representations are interchangeable.  Nothing outside this file follows
//   them: the IOP side parses its own copy of the header before any CMP_Init(),
//   and cmp_eeiop.c is still a stub.
//
// CmpDataTop()/CmpDivTbl() below exist only to carry that deviation.  Neither
// is a ROM symbol -- ZERO2.MAP and functions.txt list three functions for this
// object and no statics -- and they are deliberately not spelled CMP_* so they
// cannot be mistaken for one.  Where they appear, the ROM wrote
// `header->data_offset` and `header->div_p[no]` directly.
//
// The /* NNN */ annotations are measured: function opening lines are the $LM
// that precedes each PROC record in symbols.txt, statement lines come from the
// same table.  CMP_Decode's two initialisers sit inside the run of line-32
// notes GCC emitted for the prologue and carry no $LM of their own, so they are
// annotated with the opening line; there is no line note anywhere between 32
// and 38, so lines 33-37 produced no code.
//
// Reconstructed from the Feb 6 2004 prototype (SLES_523.84).

#include "cmp.h"

#include <string.h>                     /* memcpy                            */

#include "encodes.h"                    /* SlideDecode                       */
#include "../../common/utility2.h"      /* GetAlignUp                        */

// --------------------------------------------------------------------------
// PORT: resolve the two header-relative fields.  See the banner -- the ROM has
// no such helpers, because after CMP_Init() its own fields are already
// absolute addresses.

static u_char *CmpDataTop(const CMP_HEADER *header)
{
    return (u_char *)header + header->data_offset;
}

static ENCODE_DIV_SECTION *CmpDivTbl(const CMP_HEADER *header)
{
    return (ENCODE_DIV_SECTION *)((u_char *)header + header->div_p);
}

// --------------------------------------------------------------------------
// Expand block `no` from `from_adrs` to `to_adrs`.  The block's entry in the
// div table says which of the two ways to do it; both read the same `size`,
// which is the block's compressed extent.
//
// There is no local for the table entry -- functions.txt lists no locals at
// all for this function, and the ROM re-reads `header->div_p[no]` in the
// branch it takes.  GCC kept the computed address in t0 across the test.

void CMP_DecodeOne(CMP_HEADER *header, int no, uintptr_t from_adrs, uintptr_t to_adrs)
{                                                                       /* 19 */
    if (CmpDivTbl(header)[no].type == ENCODE_TYPE_SLIDE) {              /* 20 */

        SlideDecode((u_char *)from_adrs, (u_char *)to_adrs,
                    CmpDivTbl(header)[no].size);                        /* 22 */

    } else {
        memcpy((void *)to_adrs, (void *)from_adrs,
               CmpDivTbl(header)[no].size);                             /* 25 */
    }
}

// --------------------------------------------------------------------------
// Expand the whole file into `decode_buf` and answer its expanded size.
//
// Note the output cursor moves by div_size per block unconditionally, so the
// last block writes a full div_size even when header->size stops short of it:
// the buffer has to be div_num * div_size bytes, not header->size.  The port's
// caller in miopan_fileload.cpp checks exactly that.

int CMP_Decode(CMP_HEADER *header, void *decode_buf)
{                                                                       /* 32 */
    int       i;
    uintptr_t now_adrs = (uintptr_t)decode_buf;                         /* 32 */
    int       now_size = 0;                                             /* 32 */

    for (i = 0; i < header->div_num; i++) {                             /* 38 */

        CMP_DecodeOne(header, i,
                      (uintptr_t)(CmpDataTop(header) + now_size),
                      now_adrs);                                        /* 40 */

        now_adrs += (uintptr_t)header->div_size;                        /* 43 */
        now_size += (int)GetAlignUp(CmpDivTbl(header)[i].size, 4);      /* 44 */
    }

    return header->size;                                                /* 49 */
}

// --------------------------------------------------------------------------
// Resolve the header's two offsets, once.  See the banner: the port keeps them
// header-relative, so only the `mapping` latch survives here.

void CMP_Init(CMP_HEADER *header)
{                                                                       /* 55 */
    if (header->mapping == 0) {                                         /* 57 */

        /* PORT DEVIATION.  The ROM turns both fields into absolute EE
         * addresses at this point:
         *      header->data_offset = (int)header + header->data_offset;    62
         *      header->div_p       = (int)header + (int)header->div_p;     63
         * A host pointer does not fit in either 32-bit on-disc field, so the
         * port leaves them as the offsets the file already holds and resolves
         * them at each use.  The latch below still makes the call idempotent,
         * which is the only thing outside this file depends on. */

        header->mapping = 1;                                            /* 66 */
    }                                                                   /* 67 */
}
