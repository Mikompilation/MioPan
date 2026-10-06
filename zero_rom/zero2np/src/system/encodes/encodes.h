/* ==========================================================================
 *  system/encodes/encodes.h
 *
 *  encodes.o -- the LZSS coder, five functions and 0x8d8 of .text at 0x278428.
 *  It is the game's lossless compressor: `cmp.a` uses it for every block of a
 *  .cmp file marked ENCODE_TYPE_SLIDE, and photo_make.c tries it on a photo
 *  before falling back to the DCT codec in system/compress.
 *
 *  The algorithm is Haruhiko Okumura's LZSS.C, near-verbatim: a 4 KB ring
 *  buffer, matches of 3..18 bytes, and a binary search tree over the window so
 *  the encoder can find the longest match in log time.  The ROM's changes are
 *  small and listed in encodes.c.
 *
 *  Format, for anyone reading a .cmp block by hand: the stream is groups of
 *  one flag byte followed by up to eight items.  Flag bit set = the next item
 *  is one literal byte.  Flag bit clear = the next item is two bytes
 *  (position_low, ((position_high << 4) | (length - 3))), naming a match of
 *  length+3 bytes at `position` in the 4 KB window.
 *
 *  Reconstructed from the Feb 6 2004 prototype (SLES_523.84), encodes.o.
 * ======================================================================== */

#ifndef _SYSTEM_ENCODES_ENCODES_H
#define _SYSTEM_ENCODES_ENCODES_H

#include <sys/types.h>

/* Okumura's four constants.  NIL is N, which is why lson/dad are N+1 entries
 * and rson is N+257 -- the top 256 slots of rson are the per-first-byte tree
 * roots. */
#define ENCODES_N           4096    /* ring-buffer size                       */
#define ENCODES_F           18      /* longest match                          */
#define ENCODES_THRESHOLD   2       /* shorter than this is cheaper as literals */
#define ENCODES_NIL         ENCODES_N

/* The search tree and the encoder's window.  All four are file-scope in the
 * ROM and all four are exported, so they are not static here either.  Note
 * SlideDecode() does NOT use `text` -- it keeps its own window on the stack,
 * which is what lets a decode run while an encode is half-finished. */
extern short  dad[ENCODES_N + 1];               /* data  35ec28 */
extern short  lson[ENCODES_N + 1];              /* data  360c30 */
extern short  rson[ENCODES_N + 257];            /* data  362c38 */
extern u_char text[ENCODES_N + ENCODES_F - 1];  /* data  364e40 */

extern short  matchpos;                         /* sdata 3f49d8 */
extern short  matchlen;                         /* sdata 3f49da */

/* The tree.  Exported by the ROM although only SlideEncode() calls them. */
void init_tree(void);
void insert_node(short r);
void delete_node(short p);

/* Expand one LZSS block.  `size` is the COMPRESSED byte count; the caller
 * knows the expanded size from its own header (CMP_HEADER::div_size, or
 * SLIDE_ENCODE_HEADER for a photo). */
void SlideDecode(u_char *base, u_char *addrs, int size);

/* Compress `max_size` bytes at `base` into `addrs`, and answer how many bytes
 * were written.  Answers 0 for an empty input.  There is no bound on the
 * output: LZSS can expand incompressible data by roughly 1/8, so the
 * destination must have room for that. */
int  SlideEncode(u_char *base, u_char *addrs, int max_size);

#endif /* _SYSTEM_ENCODES_ENCODES_H */
