// FILE: /home/zero_rom/zero2np/src/ingame/puzzle/roku/puzzle_roku_dat.c
//
// The rokumen (six-face) puzzle's answer, book titles and sprite table.
//
// A data-only translation unit.  The five books are numbered 8..12 -- that is
// their puzzle_roku_tex[] index and also what roku_pzl.c stores in a shelf
// slot -- and six_puzzle_answer[] is the order they have to end up in.
//
// six_pzl_book_label[] is the (file_type, file_id) pair for each book's text,
// which SixPuzzleBookReadWinDisp() hands to DrawCmnFileWindow() and
// SixPuzzleBookTitleWinDisp() turns into a title through its own msg_type_tbl.
//
// six_puzzle_answer is a reference_fixed_array<int,5>, i.e. a bounds-checked
// view onto the plain int[5] below, so it is built by a static constructor --
// which is why puzzle_roku_dat.o has a .ctors entry and 0x120 of .text despite
// exporting no function.
//
// Extracted from .data 0x33df30 / .rodata 0x3c46a8 and verified byte-for-byte
// against the ROM.  The tex0 values are GS TEX0 register words naming the
// pak's texture pages and are reproduced as stored rather than decoded.
//
// Reconstructed from the Feb 6 2004 prototype (SLES_523.84), puzzle_roku_dat.o.

#include "puzzle_roku_dat.h"

static int six_puzzle_answer_dat[5] = { 9, 10, 12, 8, 11 };     /* rdata 3c46a8 */

reference_fixed_array<int, 5> six_puzzle_answer(six_puzzle_answer_dat); /* sdata 3f3b88 */

/* (file_type, file_id) per book.  Every book is file type 2; the ids are in
 * shelf order, not answer order. */
int six_pzl_book_label[5][2] =                              /* data 33df30 */
{
    { 2, 3 },
    { 2, 0 },
    { 2, 1 },
    { 2, 4 },
    { 2, 2 },
};

/* Where each shelf slot sits.  The y is hardcoded to 174 at the draw site. */
float shelf_book_x[5] =                                     /* data 33df58 */
{
    197.0f, 254.0f, 308.0f, 361.0f, 416.0f,
};

SPRT_DAT puzzle_roku_tex[PUZZLE_ROKU_TEX_NUM] =             /* data 33df70 */
{
    /*         tex0                    u    v    w    h     x     y  pri  alp fl bl */
    /*  0 */ { 0x2007620665322bc0ULL,   1,   1, 510, 448,    0,    0,   0, 128, 0, 1 },
    /*  1 */ { 0x2007628665322fc0ULL,   1,   1, 130, 448,  510,    0,   0, 128, 0, 1 },
    /*  2 */ { 0x2007628665322fc0ULL, 133,   1, 280,  70,  175,   63,   0, 128, 0, 0 },
    /*  3 */ { 0x2007628665322fc0ULL, 134,  80,  63, 173,  171,  134,   0, 128, 0, 1 },
    /*  4 */ { 0x2007628665322fc0ULL, 200,  80,  62, 173,  231,  134,   0, 128, 0, 1 },
    /*  5 */ { 0x2007628665322fc0ULL, 265,  80,  59, 173,  287,  134,   0, 128, 0, 1 },
    /*  6 */ { 0x2007628665322fc0ULL, 327,  80,  59, 173,  340,  134,   0, 128, 0, 1 },
    /*  7 */ { 0x2007628665322fc0ULL, 389,  80,  64, 172,  395,  134,   0, 128, 0, 1 },
    /*  8 */ { 0x20076605dd30b9c0ULL,  61,   1,  18, 126,    0,    0,   0, 128, 0, 0 },
    /*  9 */ { 0x20076605dd30b9c0ULL,   1,   1,  18, 126,    0,    0,   0, 128, 0, 0 },
    /* 10 */ { 0x20076605dd30b9c0ULL,  21,   1,  18, 126,    0,    0,   0, 128, 0, 0 },
    /* 11 */ { 0x20076605dd30b9c0ULL,  81,   1,  18, 126,    0,    0,   0, 128, 0, 0 },
    /* 12 */ { 0x20076605dd30b9c0ULL,  41,   1,  18, 126,    0,    0,   0, 128, 0, 0 },
    /* 13 */ { 0x2007670621313a10ULL,   1,   1, 230, 203,  206,   61,   0, 128, 0, 1 },
    /* 14 */ { 0x20076386213134c0ULL,   1,   1, 196, 175,  228,   78,   0, 128, 0, 0 },
    /* 15 */ { 0x20076406213135c0ULL,   1,   1, 196, 175,  228,   78,   0, 128, 0, 0 },
    /* 16 */ { 0x20076486213136c0ULL,   1,   1, 196, 175,  228,   78,   0, 128, 0, 0 },
    /* 17 */ { 0x20076506213137c0ULL,   1,   1, 196, 175,  228,   78,   0, 128, 0, 0 },
    /* 18 */ { 0x20076586213138c0ULL,   1,   1, 196, 175,  228,   78,   0, 128, 0, 0 },
    /* 19 */ { 0x200766859930ba00ULL,   1,   1,  23,  24,  221,  151,   0, 128, 0, 1 },
    /* 20 */ { 0x200766859930ba00ULL,  26,   1,  23,  24,  395,  151,   0, 128, 0, 1 },
    /* 21 */ { 0x200766859930ba00ULL,   1,  27,  12,  14,  226,  156,   0, 128, 0, 1 },
    /* 22 */ { 0x200766859930ba00ULL,  15,  27,  12,  14,  401,  156,   0, 128, 0, 1 },
    /* 23 */ { 0x2007628665322fc0ULL, 133, 255, 272,  56,  190,  273,   0, 128, 0, 1 },
    /* 24 */ { 0x20076306213133c0ULL, 139,   1,  28, 101,   23,   29,   0, 128, 0, 1 },
    /* 25 */ { 0x20076306213133c0ULL,   1,   6, 129,  23,   52,   34,   0, 128, 0, 1 },
    /* 26 */ { 0x20076306213133c0ULL,   1, 136,  63,  23,   52,   83,   0, 128, 0, 1 },
    /* 27 */ { 0x20076306213133c0ULL,   1,  56, 136,  23,   52,   83,   0, 128, 0, 1 },
    /* 28 */ { 0x20076306213133c0ULL,   1,  31, 105,  23,   52,   58,   0, 128, 0, 1 },
    /* 29 */ { 0x20076306213133c0ULL,   1, 161, 138,  23,   52,   83,   0, 128, 0, 1 },
    /* 30 */ { 0x20076306213133c0ULL,   1,  81, 105,  23,   52,  108,   0, 128, 0, 1 },
    /* 31 */ { 0x20076306213133c0ULL,   1, 106, 176,  28,   52,   29,   0, 128, 0, 1 },
    /* 32 */ { 0x20076306213133c0ULL,   1, 186, 108,  23,   52,  108,   0, 128, 0, 1 },
};
