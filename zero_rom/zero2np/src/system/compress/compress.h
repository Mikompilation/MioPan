/* ==========================================================================
 *  system/compress/compress.h
 *
 *  compress.o -- the photo codec: an 8x8 DCT, a quality-driven quantiser and
 *  a run-length-coded bit stream, plus the three-byte handshake the album
 *  decodes photos through.  Seven exports and eleven file statics.
 *
 *  The algorithm is Mark Nelson's DCT.C from "The Data Compression Book",
 *  adapted to memory buffers instead of stdio and to the game's packed-RGB
 *  picture format.  Every function name below is his; where the ROM deviates,
 *  compress.c says so at the site.
 *
 *  ExpandFile() does two macroblock rows per call and a picture takes
 *  twenty-four of them, so it is resumable and the caller does not drive it
 *  directly.  It posts a request with ReqPhotoExpand(), and
 *  then each frame asks CheckPhotoExpandEnd() whether the picture is ready;
 *  while it is not, GetPhotoExpand() returning 0 is the "you own this slice,
 *  do some work" token, which is what makes the handshake safe against two
 *  viewers.
 *
 *  Reconstructed from the Feb 6 2004 prototype (SLES_523.84), compress.o.
 * ======================================================================== */

#ifndef _SYSTEM_COMPRESS_COMPRESS_H
#define _SYSTEM_COMPRESS_COMPRESS_H

#include <sys/types.h>

/* --------------------------------------------------------------------------
 *  The 0x20-byte header every stored photo carries.
 *
 *  `type` is what the decoder switches on: 0 lossless (LZSS, and `size` is
 *  the compressed byte count), 1 lossy (the DCT codec below), 2 "no image" --
 *  the marker CompressData() writes when nothing would fit, which decodes to
 *  black.
 *
 *  For a lossy picture `sizeRGB[c]` is the byte offset, from the head of the
 *  header, at which colour plane c's bit stream starts, and `quality` is the
 *  quantiser scale the encoder settled on.  CompressFile() fills all of them.
 * ------------------------------------------------------------------------ */
typedef struct _SLIDE_ENCODE_HEADER /* 0x20 */
{
    /* 0x00 */ int size;
    /* 0x04 */ int type;
    /* 0x08 */ int pad[2];
    /* 0x10 */ int sizeRGB[3];
    /* 0x1c */ int quality;
} SLIDE_ENCODE_HEADER;

/* --------------------------------------------------------------------------
 *  The decode handshake.
 *
 *  sta: 0 = requested and untouched, 1 = a slice is in flight, 2 = finished.
 *  cnt: how far ExpandFile() has got -- 0..23, eight steps per colour plane.
 *  no:  the album slot the request was made for.
 * ------------------------------------------------------------------------ */
typedef struct                      /* 0x3 */
{
    /* 0x0 */ u_char sta;
    /* 0x1 */ u_char cnt;
    /* 0x2 */ u_char no;
} PHOTO_EXPAND;

/* --------------------------------------------------------------------------
 *  One entry of the zig-zag scan order: the (row, col) of the n'th
 *  coefficient.  Nelson's `struct zigzag`.
 * ------------------------------------------------------------------------ */
typedef struct                      /* 0x2 */
{
    /* 0x0 */ char row;
    /* 0x1 */ char col;
} DCT_ROOT;

/* --------------------------------------------------------------------------
 *  Nelson's BIT_FILE, with the FILE * replaced by a plain buffer cursor --
 *  the codec reads and writes memory, never a stream.
 *
 *  `rack` is the byte being assembled or consumed and `mask` walks it from
 *  0x80 down.  ROM size 0xc; on the host `file` is 8 bytes, so it measures
 *  0x10.  Nothing reads this struct out of a file, so the drift is harmless.
 * ------------------------------------------------------------------------ */
typedef struct                      /* 0xc on the EE */
{
    /* 0x0 */ char  *file;
    /* 0x4 */ u_char mask;
    /* 0x8 */ int    rack;
} BIT_FILE;

extern PHOTO_EXPAND photo_expand;                   /* sdata 3ef8a0 */
extern DCT_ROOT     ZigZag[64];                     /* data  2d8d38 */

/* Non-zero once the requested picture has been fully expanded. */
char   CheckPhotoExpandEnd(void);

/* Claim the next slice: returns the state as it was, and moves 0 -> 1 so the
 * caller that saw the 0 is the only one that does the work. */
u_char GetPhotoExpand(void);

/* Post a request for album slot `no`.  ReqPhotoExpand() is the public name;
 * InitPhotoExpand() is the same thing and is what it calls. */
void   InitPhotoExpand(u_char no);
void   ReqPhotoExpand(u_char no);

/* The slot the outstanding request names. */
u_char GetPhotoExpandNo(void);

/* The codec itself.
 *
 * CompressFile() encodes one 384x128 picture from `input` (packed 0x00RRGGBB
 * words) into `output`, writing the header as it goes, and returns the
 * output/input ratio so CompressData() can decide whether the result was
 * worth keeping.  `max_size` is the uncompressed byte count -- the ratio's
 * denominator -- and is NOT a bound on what gets written.
 *
 * ExpandFile() decodes one slice of the picture at `input` into `output2`,
 * advancing photo_expand; call it until CheckPhotoExpandEnd() is true. */
float  CompressFile(u_int *input, char *output, u_int max_size, char quality);
void   ExpandFile(char *input, u_int *output2);

#endif /* _SYSTEM_COMPRESS_COMPRESS_H */
