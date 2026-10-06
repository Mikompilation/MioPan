// FILE: /home/zero_rom/zero2np/src/system/compress/compress.c
//
// compress.o -- the photo codec.  Seven exports and eleven file statics,
// 0x10d4 of .text at 0x130b78.  Everything between "the player pressed the
// shutter" and "there is a 384x128 picture in the album".
//
// THE ALGORITHM IS MARK NELSON'S DCT.C, from "The Data Compression Book",
// adapted in three ways: it reads and writes memory instead of stdio (BIT_FILE
// carries a `char *` cursor, not a FILE *), it works one colour plane at a
// time out of packed 0x00RRGGBB words, and ExpandFile() is resumable -- it
// does two macroblock rows per call and a picture takes twenty-four of them.
// (The resumability is what the photo_expand handshake is for; the only
// caller in the tree, UncompressData(), actually spends thirty in one go and
// so finishes a picture in a single frame.)  Every function name here
// is his, and recognising that is what makes the file readable -- so where the
// ROM deviates from the book, the deviation is called out at the site.
//
// The shape, once per colour plane c:
//
//     384x128 pixels -> 16 strips of 8 rows -> 48 blocks of 8x8 per strip
//     block -> ForwardDCT -> quantise by quantum[][] -> zig-zag -> OutputCode
//     OutputCode -> run-length zeros + a variable-width code -> OutputBits
//
// and the reverse coming back.  `quality` scales quantum[][]: bigger means a
// coarser quantiser and a smaller file.  CompressData() in photo_make.c walks
// quality 1..4 until the result fits.
//
// A BLOCK'S TWO SIZES, and the header.  SLIDE_ENCODE_HEADER::sizeRGB[c] is the
// byte offset from the head of the header at which plane c's bit stream
// starts; ::size is the whole file.  CompressFile() fills them in as it goes
// and pads each plane up to a 16-byte boundary.
//
// TWO ROM ODDITIES, both reproduced and both harmless:
//
//   * ExpandFile() writes a four-word header at out_header[0..3] on the first
//     slice and then decodes the picture starting at the same address, so the
//     R plane's first four pixels overwrite it in the same call.  `output`,
//     `top` and `out_header` are all just `output2`; nothing reads those four
//     words in between.  See the note at line 190.
//
//   * `repair_flg` is written on every macroblock row and read NOWHERE -- a
//     scan of both PT_LOADs finds two `sb` and not one `lb`/`lbu` against
//     0x3f4b24.  It is kept because it is a file-scope static that globals.txt
//     lists, but nothing observes it.  Same flavour as spirit_gage.o's `mFlg`.
//
// CMP_Decode's sibling fact: CompressFile() divides by `max_size` at the end
// without guarding it, even though the printf just above IS guarded.  A zero
// max_size returns an infinity rather than tripping the printf.  Kept.
//
// THE /* NNN */ ANNOTATIONS are measured, not guessed: function opening lines
// are the $LM that precedes each PROC/STATICPROC record in symbols.txt, and
// statement lines come from the same table.  Two conventions worth knowing
// while reading them.  A `for (...) {` in this file is K&R, and GCC tags the
// increment-and-test with the CLOSING brace line -- which is why the DCT loops
// show pairs like 295/296 alternating in address order, body then bottom (see
// [[for-increment-carries-closing-brace-line]]).  And a statement whose value
// is already in the return register emits nothing, so it leaves no $LM:
// InputCode()'s `return result;` at 395 is inferred from the branch target,
// not measured.
//
// LINES 468-475 HOLD NO CODE.  WritePixelStrip()'s type chain ends at 467 and
// its loop bottom is 476; .text is complete without those 8 lines, so they are
// comment or a disabled fourth case and nothing of them is recoverable.
//
// Reconstructed from the Feb 6 2004 prototype (SLES_523.84), compress.o.

#include "compress.h"

#include <math.h>                       /* cosf / sqrtf                      */
#include <stdint.h>                     /* uintptr_t (see CompressFile)      */
#include <stdio.h>                      /* printf                            */

#define N       8

/* .lit4 3ed928 holds 0x40490fda, which is one ulp BELOW the correctly-rounded
 * float pi (0x40490fdb).  The source wrote Nelson's `3.14159265358979` and EE
 * GCC truncated it; `3.1415925f` is the decimal that reproduces the ROM's word
 * exactly on the host.  See [[ee-gcc-truncates-float-literals]]. */
#define PI      3.1415925f

/* Nelson's ROUND().  Both arms are emitted at every use site even where the
 * caller has already proved the sign -- InverseDCT()'s clamped `else` branch
 * still tests, which is what identifies this as a macro rather than a helper. */
#define ROUND(a)    (((a) < 0) ? (int)((a) - 0.5f) : (int)((a) + 0.5f))

/* --------------------------------------------------------------------------
 *  File data.  ZigZag is the only initialised object in the module; the rest
 *  is .bss/.sbss scratch.  ZigZag is NOT static in the ROM (ZERO2.MAP lists it
 *  as a .data export at 2d8d38), so it is not static here either.
 * ------------------------------------------------------------------------ */

/* The standard JPEG zig-zag scan, as (row, col) pairs -- verified against the
 * ROM's 128 bytes at 2d8d38. */
DCT_ROOT ZigZag[64] =                                       /* data  2d8d38 */
{
    {0, 0}, {0, 1}, {1, 0}, {2, 0}, {1, 1}, {0, 2}, {0, 3}, {1, 2},
    {2, 1}, {3, 0}, {4, 0}, {3, 1}, {2, 2}, {1, 3}, {0, 4}, {0, 5},
    {1, 4}, {2, 3}, {3, 2}, {4, 1}, {5, 0}, {6, 0}, {5, 1}, {4, 2},
    {3, 3}, {2, 4}, {1, 5}, {0, 6}, {0, 7}, {1, 6}, {2, 5}, {3, 4},
    {4, 3}, {5, 2}, {6, 1}, {7, 0}, {7, 1}, {6, 2}, {5, 3}, {4, 4},
    {3, 5}, {2, 6}, {1, 7}, {2, 7}, {3, 6}, {4, 5}, {5, 4}, {6, 3},
    {7, 2}, {7, 3}, {6, 4}, {5, 5}, {4, 6}, {3, 7}, {4, 7}, {5, 6},
    {6, 5}, {7, 4}, {7, 5}, {6, 6}, {5, 7}, {6, 7}, {7, 6}, {7, 7}
};

PHOTO_EXPAND photo_expand;                                  /* sdata 3ef8a0 */

/* One 8-row strip of one colour plane, 384 pixels wide -- the working set both
 * directions pass through. */
static u_char pixelstrip[8][384];                           /* bss   422170 */

/* The separable DCT basis and its transpose, and the quantiser.  All three are
 * rebuilt by InitCompress() whenever the quality changes. */
static float  C[8][8];                                      /* bss   422d70 */
static float  Ct[8][8];                                     /* bss   422e70 */
static int    quantum[8][8];                                /* bss   422f70 */

static int    InputRunLength;                               /* sbss  3f4b1c */
static int    OutputRunLength;                              /* sbss  3f4b20 */

/* Written by ExpandFile() once per macroblock row and never read -- see the
 * banner. */
static char   repair_flg;                                   /* sbss  3f4b24 */

/* --------------------------------------------------------------------------
 *  The eleven statics.  The ROM defines them after the two entry points, so
 *  they are forward-declared here the way the source must have been.
 * ------------------------------------------------------------------------ */
static void    InitCompress(char quality);
static void    ForwardDCT(u_char **input, int (*output)[8]);
static void    InverseDCT(int (*input)[8], u_char **output);
static void    WriteDCTData(BIT_FILE *output_file, int (*output_data)[8]);
static u_int  *ReadPixelStrip(u_int *input, u_char (*strip)[384], u_char type);
static int     InputCode(BIT_FILE *input_file);
static void    ReadDCTData(BIT_FILE *input_file, int (*input_data)[8]);
static void    OutputCode(BIT_FILE *output_file, int code);
static u_int  *WritePixelStrip(u_int *output, u_char (*strip)[384], u_char type);
static void    OutputBits(BIT_FILE *bit_file, u_int code, int count);
static int     InputBits(BIT_FILE *bit_file, int bit_count);

// --------------------------------------------------------------------------
// Encode one whole 384x128 picture, all three colour planes, in one call.
// Returns the compressed/uncompressed ratio; CompressData() keeps the result
// only if that came out small enough.
//
// `max_size` is only the ratio's denominator -- it does NOT bound the output.
// The caller guarantees the buffer.

float CompressFile(u_int *input, char *output, u_int max_size, char quality)
{                                                                       /* 65 */
    int       row;                                                      /* 66 */
    int       col;
    int       i;
    int       j;
    u_char   *input_array[8];
    int       output_array[8][8];
    BIT_FILE  bit_file;
    u_int     size;
    u_int    *addr;
    /* PORT: `tmp` is `unsigned int` in the ROM, where it holds a 4-byte EE
     * pointer while the 16-byte alignment is computed.  A host pointer does
     * not survive that round trip, so it widens.  See
     * [[int-pointer-out-params-must-widen]]. */
    uintptr_t tmp;                                                      /* 74 */

    printf("START COMPRESS\n");                                         /* 75 */

    ((SLIDE_ENCODE_HEADER *)output)->type    = 1;                       /* 79 */
    ((SLIDE_ENCODE_HEADER *)output)->quality = quality;                 /* 80 */

    bit_file.file = output + sizeof(SLIDE_ENCODE_HEADER);               /* 83 */
    bit_file.mask = 0x80;                                               /* 84 */
    bit_file.rack = 0;                                                  /* 85 */

    InitCompress(quality);                                              /* 89 */

    for (i = 0; i < 3; i++)                                             /* 92 */
    {
        addr            = input;                                        /* 93 */
        OutputRunLength = 0;                                            /* 94 */
        InputRunLength  = 0;                                            /* 95 */
        bit_file.mask   = 0x80;                                         /* 96 */
        bit_file.rack   = 0;                                            /* 97 */

        /* Where this plane's bit stream begins, relative to the header. */
        ((SLIDE_ENCODE_HEADER *)output)->sizeRGB[i] =
                                        (int)(bit_file.file - output);  /* 98 */

        for (row = 0; row < 128; row += 8)                              /* 101 */
        {
            addr = ReadPixelStrip(addr, pixelstrip, (u_char)i);         /* 104 */

            for (col = 0; col < 384; col += 8)                          /* 107 */
            {
                for (j = 0; j < N; j++)                                 /* 108 */
                {
                    input_array[j] = &pixelstrip[j][col];               /* 109 */
                }                                                       /* 110 */

                ForwardDCT(input_array, output_array);                  /* 113 */

                WriteDCTData(&bit_file, output_array);                  /* 116 */
            }                                                           /* 117 */
        }                                                               /* 118 */

        /* A non-zero code flushes any pending run of zeros, which is the only
         * way the last one gets written. */
        OutputCode(&bit_file, 1);                                       /* 119 */
        bit_file.file++;                                                /* 120 */

        tmp = (uintptr_t)bit_file.file;                                 /* 122 */
        if ((tmp & 0xf) != 0)                                           /* 123 */
        {
            tmp = tmp - (tmp & 0xf) + 0x10;                             /* 124 */
        }
        bit_file.file = (char *)tmp;                                    /* 126 */
    }                                                                   /* 128 */

    size = (u_int)(bit_file.file - output);                             /* 131 */
    ((SLIDE_ENCODE_HEADER *)output)->size = (int)size;                  /* 132 */

    printf("========= PHOTO INFORMATION =========\n");                  /* 135 */
    printf("quality    : %d\n", quality);                               /* 136 */
    printf("size : %d\n", size);                                        /* 137 */
    if (max_size != 0)                                                  /* 138 */
    {
        printf("Out/In : %f\n", (float)size / (float)max_size);         /* 139 */
    }

    /* Deliberately unguarded where the printf above is guarded -- a zero
     * max_size returns an infinity.  The ROM's own asymmetry. */
    return (float)size / (float)max_size;                               /* 143 */
}

// --------------------------------------------------------------------------
// The decode handshake.  ExpandFile() does a slice at a time, so a viewer
// polls these rather than calling the codec directly.

char CheckPhotoExpandEnd(void)
{                                                                       /* 148 */
    return photo_expand.sta == 2;                                       /* 149 */
}

/* Returns the state as it was and claims it: the caller that sees 0 is the
 * one that owns this frame's slice of the decode. */
u_char GetPhotoExpand(void)
{                                                                       /* 152 */
    u_char ret = photo_expand.sta;                                      /* 153 */

    if (photo_expand.sta == 0) photo_expand.sta = 1;                    /* 154 */

    return ret;
}

void InitPhotoExpand(u_char no)
{                                                                       /* 158 */
    photo_expand.sta = 0;                                               /* 159 */
    photo_expand.cnt = 0;                                               /* 160 */
    photo_expand.no  = no;                                              /* 161 */
}

void ReqPhotoExpand(u_char no)
{                                                                       /* 164 */
    InitPhotoExpand(no);                                                /* 165 */
}

u_char GetPhotoExpandNo(void)
{                                                                       /* 168 */
    return photo_expand.no;                                             /* 169 */
}

// --------------------------------------------------------------------------
// Decode one slice.  photo_expand.cnt runs 0..23: three colour planes of eight
// steps, each step two macroblock rows (16 pixel rows) of the 128-row picture.
// The bit-stream cursor and the output cursor are statics because they have to
// survive between calls.

void ExpandFile(char *input, u_int *output2)
{                                                                       /* 172 */
    static u_int               *out_header;                             /* 173 */
    static SLIDE_ENCODE_HEADER *sheader;
    static u_int               *top;
    static BIT_FILE             bit_file;
    static u_int               *output;
    int      row;
    int      col;
    int      i;
    int      j;
    int      input_array[8][8];
    u_char  *output_array[8];
    u_int    quality;
    u_int    end;                                                       /* 182 */

    if (photo_expand.sta != 2)                                          /* 183 */
    {
        if (photo_expand.cnt == 0)                                      /* 185 */
        {
            output     = output2;                                       /* 186 */

            /* These four words are overwritten by the R plane's first four
             * pixels later in this very call -- `top` is `output` is
             * `out_header` is `output2`.  Reproduced as found; nothing reads
             * them in between.  See the banner. */
            out_header = output2;                                       /* 189 */
            out_header[0] = *(u_int *)input;                            /* 190 */
            out_header[1] = out_header[2] = out_header[3] = 0;          /* 191 */

            sheader = (SLIDE_ENCODE_HEADER *)input;                     /* 194 */
            quality = (u_int)sheader->quality;                          /* 195 */

            bit_file.file = input + sizeof(SLIDE_ENCODE_HEADER);        /* 198 */
            bit_file.mask = 0x80;                                       /* 199 */
            bit_file.rack = 0;                                          /* 200 */

            top = output;                                               /* 201 */

            InitCompress(quality);                                      /* 204 */
        }

        i = photo_expand.cnt >> 3;                                      /* 208 */
        if (i < 3)                                                      /* 209 */
        {
            /* First step of a plane: rewind to the top of the picture and
             * re-seat the bit reader on that plane's stream. */
            if ((photo_expand.cnt & 7) == 0)                            /* 211 */
            {
                OutputRunLength = 0;                                    /* 212 */
                InputRunLength  = 0;                                    /* 213 */
                output          = top;                                  /* 214 */
                bit_file.file   = input + sheader->sizeRGB[i];          /* 215 */
                bit_file.mask   = 0x80;                                 /* 216 */
                bit_file.rack   = 0;                                    /* 217 */
            }

            row = (photo_expand.cnt & 7) * 16;                          /* 219 */
            end = (u_int)row + 16;                                      /* 220 */

            /* `end` is unsigned in the stabs but the ROM's compare is signed
             * (`slt`); the cast reproduces it.  Identical over 0..128. */
            for (; row < (int)end; row += 8)                            /* 222 */
            {
                if (row < 120) repair_flg = 0;                          /* 224 */
                else           repair_flg = 1;                          /* 225 */

                for (col = 0; col < 384; col += 8)                      /* 227 */
                {
                    for (j = 0; j < N; j++)                             /* 228 */
                    {
                        output_array[j] = &pixelstrip[j][col];          /* 229 */
                    }                                                   /* 230 */

                    ReadDCTData(&bit_file, input_array);                /* 231 */

                    InverseDCT(input_array, output_array);              /* 232 */
                }                                                       /* 233 */

                output = WritePixelStrip(output, pixelstrip, (u_char)i); /* 234 */
            }                                                           /* 235 */

            photo_expand.cnt++;                                         /* 236 */
        }

        if (photo_expand.cnt >= 24)                                     /* 238 */
        {
            photo_expand.sta = 2;                                       /* 239 */
            photo_expand.cnt = 0;                                       /* 240 */
        }
    }
}                                                                       /* 242 */

// --------------------------------------------------------------------------
// Build the quantiser and the DCT basis for one quality setting.  Nelson's
// Initialize() plus InitializeDCT(), fused.
//
// The quantiser is where the ROM departs from the book: Nelson ramps by
// (1 + i + j), this ramps by (1 + i*2/3 + j*2/3), which is a gentler slope
// towards the high frequencies -- photographs of dark rooms, not test images.
// Both divisions are real `div`/`divu1` instructions with the EE's zero trap,
// so the `/ 3` spelling is the source's own.

static void InitCompress(char quality)
{                                                                       /* 246 */
    u_int i;                                                            /* 247 */
    u_int j;
    float val;                                                          /* 249 */

    for (i = 0; i < N; i++)                                             /* 251 */
    {
        for (j = 0; j < N; j++)                                         /* 252 */
        {
            quantum[i][j] = 1 + ((1 + (i * 2) / 3 + (j * 2) / 3) * quality); /* 254 */
        }                                                               /* 255 */
    }                                                                   /* 256 */

    /* Row 0 of the basis is flat.  GCC folds the 8.0f but keeps the sqrt as an
     * inline `sqrt.s` with a NaN check that falls back to a sqrtf call -- which
     * is the only reason compress.o pulls sqrtf in at all. */
    val = 1.0f / sqrtf((float)N);                                       /* 259 */
    for (j = 0; j < N; j++)                                             /* 260 */
    {
        C[0][j]  = val;                                                 /* 262 */
        Ct[j][0] = val;                                                 /* 263 */
    }                                                                   /* 264 */

    val = sqrtf(2.0f / (float)N);                                       /* 265 */
    for (i = 1; i < N; i++)                                             /* 266 */
    {
        for (j = 0; j < N; j++)                                         /* 267 */
        {
            C[i][j]  = val * cosf((2 * j + 1) * PI * i / (2.0f * N));    /* 269 */
            Ct[j][i] = C[i][j];                                         /* 270 */
        }                                                               /* 271 */
    }                                                                   /* 272 */
}

// --------------------------------------------------------------------------
// 8x8 forward DCT, separable: Ct applied across, then C down.  Input pixels
// are biased to -128..127 on the way in.

static void ForwardDCT(u_char **input, int (*output)[8])
{                                                                       /* 277 */
    float temp[8][8];                                                   /* 278 */
    float temp1;
    u_int i, j, k;                                                      /* 280 */

    for (i = 0; i < N; i++)                                             /* 282 */
    {
        for (j = 0; j < N; j++)                                         /* 283 */
        {
            temp[i][j] = 0.0f;                                          /* 284 */
            for (k = 0; k < N; k++)                                     /* 285 */
            {
                temp[i][j] += (input[i][k] - 128) * Ct[k][j];           /* 286 */
            }                                                           /* 287 */
        }                                                               /* 288 */
    }                                                                   /* 289 */

    for (i = 0; i < N; i++)                                             /* 291 */
    {
        for (j = 0; j < N; j++)                                         /* 292 */
        {
            temp1 = 0.0f;                                               /* 293 */
            for (k = 0; k < N; k++)                                     /* 294 */
            {
                temp1 += C[i][k] * temp[k][j];                          /* 295 */
            }                                                           /* 296 */
            output[i][j] = ROUND(temp1);                                /* 297 */
        }                                                               /* 298 */
    }                                                                   /* 299 */
}

// --------------------------------------------------------------------------
// 8x8 inverse DCT.  The +128 un-biases, and the result is clamped before the
// round -- so the ROUND() in the else arm can never take its negative branch,
// yet still compiles both.

static void InverseDCT(int (*input)[8], u_char **output)
{                                                                       /* 304 */
    float temp[8][8];                                                   /* 305 */
    float temp1;
    u_int i, j, k;                                                      /* 307 */

    for (i = 0; i < N; i++)                                             /* 309 */
    {
        for (j = 0; j < N; j++)                                         /* 310 */
        {
            temp[i][j] = 0.0f;                                          /* 311 */
            for (k = 0; k < N; k++)                                     /* 312 */
            {
                temp[i][j] += input[i][k] * C[k][j];                    /* 313 */
            }                                                           /* 314 */
        }                                                               /* 315 */
    }                                                                   /* 316 */

    for (i = 0; i < N; i++)                                             /* 318 */
    {
        for (j = 0; j < N; j++)                                         /* 319 */
        {
            temp1 = 0.0f;                                               /* 320 */
            for (k = 0; k < N; k++)                                     /* 321 */
            {
                temp1 += Ct[i][k] * temp[k][j];                         /* 322 */
            }                                                           /* 323 */
            temp1 += 128.0f;                                            /* 324 */
            if (temp1 < 0)                                              /* 325 */
                output[i][j] = 0;                                       /* 326 */
            else if (temp1 > 255)                                       /* 327 */
                output[i][j] = 255;                                     /* 328 */
            else                                                        /* 329 */
                output[i][j] = (u_char)ROUND(temp1);                    /* 330 */
        }                                                               /* 332 */
    }                                                                   /* 333 */
}

// --------------------------------------------------------------------------
// Quantise a transformed block and hand its 64 coefficients to OutputCode in
// zig-zag order, so that the high-frequency zeros arrive in one run.

static void WriteDCTData(BIT_FILE *output_file, int (*output_data)[8])
{                                                                       /* 339 */
    int   i;                                                            /* 340 */
    int   row;
    int   col;
    float result;

    for (i = 0; i < 64; i++)                                            /* 343 */
    {
        row    = ZigZag[i].row;                                         /* 344 */
        col    = ZigZag[i].col;                                         /* 345 */
        result = output_data[row][col] / (float)quantum[row][col];      /* 346 */
        OutputCode(output_file, ROUND(result));                         /* 347 */
    }                                                                   /* 348 */
}

// --------------------------------------------------------------------------
// Pull one 8-row strip of one colour plane out of the packed RGB source.
//
// The zero clamp at line 366 is the ROM's, not Nelson's: a component that
// decodes to 0 is forced to 1.  Zero is the picture's "nothing here" value
// once the three planes are recombined, so a genuinely black pixel has to be
// nudged to survive it.

static u_int *ReadPixelStrip(u_int *input, u_char (*strip)[384], u_char type)
{                                                                       /* 353 */
    int   row;                                                          /* 354 */
    int   col;
    u_int c;

    for (row = 0; row < N; row++)                                       /* 356 */
    {
        for (col = 0; col < 384; col++)                                 /* 357 */
        {
            c = *input++;                                               /* 358 */

            if (type == 0)                                              /* 359 */
                strip[row][col] = c >> 16;                              /* 360 */
            else if (type == 1)                                         /* 361 */
                strip[row][col] = c >> 8;                               /* 362 */
            else if (type == 2)                                         /* 363 */
                strip[row][col] = c;                                    /* 364 */

            if (strip[row][col] == 0) strip[row][col] = 1;              /* 366 */
        }                                                               /* 369 */
    }                                                                   /* 370 */

    return input;                                                       /* 371 */
}

// --------------------------------------------------------------------------
// Read one coefficient.  Nelson's variable-width code: a 2-bit class, then
// either a run of zeros or a magnitude of 1..15 bits.

static int InputCode(BIT_FILE *input_file)
{                                                                       /* 375 */
    int bit_count;                                                      /* 376 */
    int result;

    if (InputRunLength > 0)                                             /* 379 */
    {
        InputRunLength--;                                               /* 380 */
        return 0;                                                       /* 381 */
    }

    if ((bit_count = InputBits(input_file, 2)) == 0)                    /* 383 */
    {
        InputRunLength = InputBits(input_file, 4);                      /* 384 */
        return 0;                                                       /* 385 */
    }                                                                   /* 386 */

    if (bit_count == 1)                                                 /* 388 */
        bit_count = InputBits(input_file, 1) + 1;                       /* 389 */
    else
        bit_count = InputBits(input_file, 2) + (bit_count << 2) - 5;    /* 391 */

    result = InputBits(input_file, bit_count);                          /* 393 */

    /* The ROM tests the sign bit with a shift-and-mask (`srav` + `andi 1`)
     * where the book writes `result & (1 << (bit_count - 1))`; the `1 <<
     * bit_count` on the next line does emit a `sllv`, so this really is the
     * source's own spelling rather than a compiler rewrite. */
    if ((result >> (bit_count - 1)) & 1)                                /* 394 */
        return result;                                                  /* 395 */

    return result - (1 << bit_count) + 1;                               /* 396 */
}                                                                       /* 397 */

// --------------------------------------------------------------------------
// Read one block's 64 coefficients in zig-zag order and de-quantise them.

static void ReadDCTData(BIT_FILE *input_file, int (*input_data)[8])
{                                                                       /* 400 */
    int i;                                                              /* 401 */
    int row;
    int col;

    for (i = 0; i < 64; i++)                                            /* 403 */
    {
        row = ZigZag[i].row;                                            /* 404 */
        col = ZigZag[i].col;                                            /* 405 */
        input_data[row][col] = InputCode(input_file) * quantum[row][col]; /* 406 */
    }                                                                   /* 407 */
}

// --------------------------------------------------------------------------
// Write one coefficient.  Zeros are accumulated into a run and only flushed
// when a non-zero arrives, in chunks of at most 16.
//
// The format ceilings a coefficient at +-1023: `bit_count + 5` is written in
// four bits, so bit_count cannot exceed 10, and 1024 would encode as 16 and
// alias.  Nothing checks it, and nothing needs to -- a quantised DCT
// coefficient here cannot get there (DC gain 8 over +-128, then divided by a
// quantiser of at least 1 + quality).

static void OutputCode(BIT_FILE *output_file, int code)
{                                                                       /* 411 */
    int top_of_range;                                                   /* 412 */
    int abs_code;
    int bit_count;

    if (code == 0)                                                      /* 415 */
    {
        OutputRunLength++;                                              /* 416 */
        return;                                                         /* 417 */
    }

    if (OutputRunLength != 0)                                           /* 419 */
    {
        while (OutputRunLength > 0)                                     /* 420 */
        {
            OutputBits(output_file, 0, 2);                              /* 421 */
            if (OutputRunLength <= 16)                                  /* 422 */
            {
                OutputBits(output_file, OutputRunLength - 1, 4);        /* 423 */
                OutputRunLength = 0;                                    /* 424 */
            }
            else
            {
                OutputBits(output_file, 15, 4);                         /* 426 */
                OutputRunLength -= 16;                                  /* 427 */
            }
        }                                                               /* 431 */
    }

    /* One line and one `movn` in the ROM, where the book uses an if/else. */
    abs_code = (code < 0) ? -code : code;                               /* 431 */

    top_of_range = 1;                                                   /* 436 */
    bit_count    = 1;                                                   /* 437 */
    while (abs_code > top_of_range)                                     /* 439 */
    {
        bit_count++;                                                    /* 440 */
        top_of_range = ((top_of_range + 1) * 2) - 1;                    /* 441 */
    }                                                                   /* 442 */

    if (bit_count < 3)                                                  /* 443 */
        OutputBits(output_file, bit_count + 1, 3);                      /* 444 */
    else
        OutputBits(output_file, bit_count + 5, 4);                      /* 446 */

    if (code > 0)                                                       /* 448 */
        OutputBits(output_file, code, bit_count);                       /* 449 */
    else
        OutputBits(output_file, code + top_of_range, bit_count);        /* 451 */
}                                                                       /* 453 */

// --------------------------------------------------------------------------
// Merge one decoded 8-row strip back into the packed RGB destination.
//
// The R pass initialises the word (including the 0x01 top byte the picture
// format carries) and G and B OR into it, which is why the planes must be
// written in order.  The ROM stores the constant and then stores the OR'd
// value again -- two source statements, two `sw` to the same address.

static u_int *WritePixelStrip(u_int *output, u_char (*strip)[384], u_char type)
{                                                                       /* 456 */
    int row;                                                            /* 457 */
    int col;

    for (row = 0; row < N; row++)                                       /* 458 */
    {
        for (col = 0; col < 384; col++)                                 /* 459 */
        {
            if (type == 0)                                              /* 461 */
            {
                *output  = 0x01000000;                                  /* 462 */
                *output |= strip[row][col] << 16;                       /* 463 */
            }
            else if (type == 1)                                         /* 464 */
            {
                *output |= strip[row][col] << 8;                        /* 465 */
            }
            else if (type == 2)                                         /* 466 */
            {
                *output |= strip[row][col];                             /* 467 */
            }

            output++;                                                   /* 476 */
        }                                                               /* 477 */
    }                                                                   /* 478 */

    return output;                                                      /* 480 */
}

// --------------------------------------------------------------------------
// The bit layer.  Nelson's OutputBits/InputBits with putc/getc replaced by a
// walking buffer cursor.

static void OutputBits(BIT_FILE *bit_file, u_int code, int count)
{                                                                       /* 484 */
    u_int mask;                                                         /* 485 */

    mask = 1 << (count - 1);                                            /* 486 */
    while (mask != 0)                                                   /* 487 */
    {
        if (mask & code)                                                /* 488 */
            bit_file->rack |= bit_file->mask;                           /* 489 */

        bit_file->mask >>= 1;                                           /* 490 */
        if (bit_file->mask == 0)                                        /* 491 */
        {
            *bit_file->file = (char)bit_file->rack;                     /* 492 */
            bit_file->rack  = 0;                                        /* 493 */
            bit_file->mask  = 0x80;                                     /* 494 */
            bit_file->file++;                                           /* 495 */
        }
        mask >>= 1;                                                     /* 497 */
    }
}

static int InputBits(BIT_FILE *bit_file, int bit_count)
{                                                                       /* 502 */
    u_int mask;                                                         /* 503 */
    int   return_value;

    mask         = 1 << (bit_count - 1);                                /* 506 */
    return_value = 0;                                                   /* 507 */
    while (mask != 0)                                                   /* 508 */
    {
        if (bit_file->mask == 0x80)                                     /* 509 */
            bit_file->rack = *bit_file->file++;                         /* 510 */

        if (bit_file->rack & bit_file->mask)                            /* 512 */
            return_value |= mask;                                       /* 513 */

        mask >>= 1;                                                     /* 514 */
        bit_file->mask >>= 1;                                           /* 515 */
        if (bit_file->mask == 0)                                        /* 516 */
            bit_file->mask = 0x80;                                      /* 517 */
    }

    return return_value;                                                /* 519 */
}
