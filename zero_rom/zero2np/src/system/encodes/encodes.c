// FILE: /home/akira_koide/zero2np/src/system/encodes/encodes.c
//
// encodes.o -- the LZSS coder.  Five exported functions, no statics, 0x8d8 of
// .text at 0x278428.  It is the game's lossless compressor: cmp.o expands
// every ENCODE_TYPE_SLIDE block through SlideDecode(), and photo_make.c tries
// SlideEncode() on a photo before falling back to the DCT codec.
//
// THE ALGORITHM IS HARUHIKO OKUMURA'S LZSS.C, near-verbatim -- `init_tree`,
// `insert_node`, `delete_node`, `code_buf`/`code`, `text_buf`/`text`, the
// N/F/THRESHOLD/NIL constants and the binary-search-tree window are all his.
// Recognising that is what makes the tree functions readable; they are fiddly
// and the ROM changed none of their logic.  The ROM's four deviations:
//
//   * getc/putc become plain buffer cursors, and SlideEncode() answers the
//     compressed byte count instead of leaving it in a global.
//   * every index is `short` rather than `int`, so NIL is 0x1000 in a
//     halfword and the trees are 2 bytes per entry.
//   * **the window is pre-filled with 0, not ' '.**  Okumura uses spaces;
//     `sb zero` at 0x27849c and 0x278830 says this build uses zeros, and both
//     halves agree, so round-tripping is unaffected -- but a stream produced
//     by stock LZSS.C would decode differently for the first few matches.
//   * SlideEncode() counts `size++` inside the flush loop where Okumura does
//     `codesize += code_buf_ptr` after it, and it accumulates `incount`.
//
// NO EXTERNAL CALLS.  A jal/j scan over the object finds only init_tree,
// insert_node and delete_node -- no printf, nothing. That matters at ROM lines
// 129-130, where a guarded integer division by `incount` survives with its
// quotient folded away; see the note at the end of SlideEncode().
//
// ASYMMETRY WORTH KNOWING: SlideEncode() works in the file-scope `text[]`
// window, but SlideDecode() keeps its own 4113-byte window ON THE STACK.  That
// is the ROM's own split, and it is what lets a decode run while an encode is
// half-finished -- which the photo path relies on.
//
// THE DATA IS ALL ZERO BUT LIVES IN .data, NOT .bss.  dad/lson/rson/text and
// the matchpos/matchlen pair are 0x7229 + 4 bytes of zeros at 0x35ec28 and
// 0x3f49d8 (read out of the ELF and confirmed).  GCC 2.96 places an object
// with an explicit initialiser in .data whatever its value, so the source
// wrote them out with `= {0}`; kept here for that reason alone.
//
// ON THE /* NNN */ ANNOTATIONS.  Function opening lines are the $LM preceding
// each PROC record in symbols.txt.  Statement lines are measured where the
// mapping is unambiguous and OMITTED where it is not -- a wrong annotation is
// worse than none.  Two regions are deliberately left unannotated:
// insert_node()'s lines 237-254, where GCC cross-jumped the two symmetric
// rson/lson arms into one code path so only one arm's numbers survive; and
// SlideDecode()'s loop body, which was rotated so hard that its tail (source
// 156-169) sits after its head (170-199) in address order.
//
// Reconstructed from the Feb 6 2004 prototype (SLES_523.84), encodes.o.

#include "encodes.h"

#define N           ENCODES_N
#define F           ENCODES_F
#define THRESHOLD   ENCODES_THRESHOLD
#define NIL         ENCODES_NIL

/* --------------------------------------------------------------------------
 *  The search tree over the window, and the window itself.  `dad[i]` is the
 *  parent of node i, `lson`/`rson` its children; rson[N+1 .. N+256] are the
 *  256 tree roots, one per possible first byte.  See the .data note above for
 *  why these carry explicit zero initialisers.
 * ------------------------------------------------------------------------ */
short  dad[N + 1]      = {0};                               /* data  35ec28 */
short  lson[N + 1]     = {0};                               /* data  360c30 */
short  rson[N + 257]   = {0};                               /* data  362c38 */
u_char text[N + F - 1] = {0};                               /* data  364e40 */

/* Where insert_node() left the longest match it found, and how long it was. */
short  matchpos = 0;                                        /* sdata 3f49d8 */
short  matchlen = 0;                                        /* sdata 3f49da */

// --------------------------------------------------------------------------
// Compress `max_size` bytes at `base` into `addrs`; answer the byte count
// written.  Okumura's Encode().
//
// The shape: keep a 4 KB sliding window in text[], with `s` the oldest byte
// and `r` the insertion point F bytes ahead.  Each step asks the tree for the
// longest match at r, emits either one literal or a (position, length) pair,
// then slides the window that far -- deleting and re-inserting tree nodes as
// bytes leave and enter.  Output is buffered into groups of one flag byte plus
// up to eight items, which is what `code`/`codeptr`/`mask` are.

int SlideEncode(u_char *base, u_char *addrs, int max_size)
{                                                                   /* 35 */
    short  s;                                                       /* 36 */
    short  r;
    short  len;
    short  codeptr;
    short  i;
    short  lastmatchlen;
    u_char c;
    u_char code[17];
    u_char mask;
    u_int  incount;
    u_int  size;                                                    /* 39 */

    size = 0;                                                       /* 40 */

    init_tree();                                                    /* 43 */
    code[0] = 0;                                                    /* 44 */
    codeptr = mask = 1;                                             /* 45 */
    s = 0;                                                          /* 46 */
    r = N - F;                                                      /* 47 */

    /* Okumura fills this with ' '.  This build fills it with 0 -- see the
     * banner; SlideDecode() does the same, so the two agree. */
    for (i = s; i < r; i++)                                         /* 48 */
        text[i] = 0;                                                /* 49 */

    /* Prime the look-ahead with up to F bytes. */
    for (len = 0; len < F; len++)                                   /* 52 */
    {
        if (max_size == 0) break;                                   /* 53 */

        c = *base++;                                                /* 56 */
        max_size--;                                                 /* 57 */
        text[r + len] = c;                                          /* 58 */
    }

    incount = len;                                                  /* 60 */
    if (incount == 0) return 0;                                     /* 61 */

    /* Seed the tree with the F strings that end at r, then r itself. */
    for (i = 1; i <= F; i++)                                        /* 64 */
        insert_node(r - i);                                         /* 65 */

    insert_node(r);                                                 /* 67 */

    do
    {
        if (matchlen > len)                                         /* 69 */
            matchlen = len;                                         /* 70 */

        if (matchlen <= THRESHOLD)                                  /* 72 */
        {
            matchlen = 1;                                           /* 73 */
            code[0] |= mask;                                        /* 74 */
            code[codeptr++] = text[r];                              /* 75 */
        }
        else                                                        /* 76 */
        {
            code[codeptr++] = (u_char)matchpos;                     /* 78 */
            code[codeptr++] = (u_char)(((matchpos >> 4) & 0xf0)
                                       | (matchlen - (THRESHOLD + 1))); /* 79 */
        }

        /* mask is a u_char, so the ninth shift is what flushes the group. */
        if ((mask <<= 1) == 0)                                      /* 82 */
        {
            for (i = 0; i < codeptr; i++)                           /* 83 */
            {
                *addrs++ = code[i];                                 /* 84 */
                size++;                                             /* 85 */
            }

            code[0] = 0;                                            /* 88 */
            codeptr = mask = 1;                                     /* 89 */
        }

        lastmatchlen = matchlen;                                    /* 91 */

        /* Slide the window forward by the match length, feeding new bytes in
         * at r and retiring old ones at s. */
        for (i = 0; i < lastmatchlen; i++)                          /* 92 */
        {
            if (max_size == 0) break;                               /* 93 */

            c = *base++;                                            /* 95 */
            max_size--;                                             /* 96 */
            delete_node(s);                                         /* 97 */

            text[s] = c;                                            /* 99 */
            if (s < F - 1)                                          /* 100 */
                text[s + N] = c;                                    /* 101 */

            s = (s + 1) & (N - 1);                                  /* 104 */
            r = (r + 1) & (N - 1);                                  /* 105 */
            insert_node(r);                                         /* 106 */
        }

        incount += i;                                               /* 108 */

        /* Ran out of input mid-match: drain the rest of the look-ahead. */
        while (i++ < lastmatchlen)                                  /* 112 */
        {
            delete_node(s);                                         /* 113 */
            s = (s + 1) & (N - 1);                                  /* 114 */
            r = (r + 1) & (N - 1);                                  /* 115 */
            if (--len)                                              /* 116 */
                insert_node(r);                                     /* 117 */
        }
    }
    while (len > 0);                                                /* 119 */

    if (codeptr > 1)                                                /* 122 */
    {
        for (i = 0; i < codeptr; i++)                               /* 123 */
        {
            *addrs++ = code[i];                                     /* 124 */
            size++;                                                 /* 125 */
        }
    }

    /* ROM LINES 129-130, NOT REPRODUCED.  What is left there is
     * `if (incount != 0)` guarding an integer division by `incount` whose
     * quotient GCC folded away, leaving only the divide-by-zero trap check
     * (`beql` + `break 0x7` at 0x2787e0) -- which is the only reason the
     * divisor is recoverable at all.  It is what remains of Okumura's trailing
     * "Out/In" ratio report: the printf is gone (this object makes no external
     * calls of any kind), and the quotient's destination is not recoverable
     * from a trap alone.  Documented rather than invented; nothing observes
     * it, and the guard means it could never have trapped. */

    return size;                                                    /* 133 */
}                                                                   /* 134 */

// --------------------------------------------------------------------------
// Expand one LZSS block.  Okumura's Decode(), with its own stack window.
//
// `size` is the compressed byte count and is the only terminator -- there is
// no end marker in the stream, so every read is guarded and the function
// simply returns when the input runs out.
//
// The loop was rotated hard by the compiler (its source tail sits after its
// head in address order), so the per-statement lines are not reliably
// separable and are left off below.

void SlideDecode(u_char *base, u_char *addrs, int size)
{                                                                   /* 145 */
    u_char textbuf[N + F - 1];
    int    i;
    int    j;
    int    k;
    int    r;
    int    c;
    u_short flags;
    u_int   rest;

    rest = (u_int)size;

    for (i = 0; i < N - F; i++)                                     /* 150 */
        textbuf[i] = 0;                                             /* 151 */

    r = N - F;                                                      /* 153 */
    flags = 0;                                                      /* 154 */

    for (;;)
    {
        /* The flag byte is refilled every eight items; the 0xff00 makes the
         * count fall out of the shift reaching bit 8. */
        if (((flags >>= 1) & 0x100) == 0)
        {
            if (rest == 0) return;

            c = *base++;
            rest--;
            flags = (u_short)(c | 0xff00);
        }

        if ((flags & 1) != 0)
        {
            /* One literal byte. */
            if (rest == 0) return;

            c = *base++;
            rest--;

            *addrs++ = (u_char)c;
            textbuf[r] = (u_char)c;
            r = (r + 1) & (N - 1);
        }
        else
        {
            /* A (position, length) pair.  Note the copy reads through the
             * window as it writes, so an overlapping match repeats -- which is
             * how a run of one byte is encoded. */
            if (rest == 0) return;

            i = *base++;
            rest--;

            if (rest == 0) return;

            j = *base++;
            rest--;

            i |= (j & 0xf0) << 4;
            j = (j & 0x0f) + THRESHOLD;

            for (k = 0; k <= j; k++)
            {
                c = textbuf[(i + k) & (N - 1)];
                *addrs++ = (u_char)c;
                textbuf[r] = (u_char)c;
                r = (r + 1) & (N - 1);
            }
        }
    }
}                                                                   /* 204 */

// --------------------------------------------------------------------------
// Empty the search tree.  The 256 roots go in rson[N+1 .. N+256]; a dad[] of
// NIL marks a window slot as not currently in any tree.

void init_tree(void)
{                                                                   /* 210 */
    short i;

    for (i = N + 1; i <= N + 256; i++)                              /* 212 */
        rson[i] = NIL;                                              /* 213 */

    for (i = 0; i < N; i++)                                         /* 215 */
        dad[i] = NIL;                                               /* 216 */
}                                                                   /* 218 */

// --------------------------------------------------------------------------
// Insert the string at text[r] into the tree, and leave the longest match it
// passed on the way down in matchpos/matchlen.  Finding the match and doing
// the insert are the same descent, which is the point of the structure.
//
// If a full-length (F byte) match turns up, the old node is *replaced* rather
// than a new one added -- the tail of the function is that surgery.
//
// LINES 237-254 ARE NOT ANNOTATED: GCC cross-jumped the symmetric rson and
// lson arms into a single code path (it selects the array with a conditional
// move and shares the rest), so only one arm's line numbers survive and they
// cannot be attributed to a side.

void insert_node(short r)
{                                                                   /* 225 */
    short   p;
    short   cmp;
    short   i;
    u_char *key;

    cmp = 1;                                                        /* 229 */
    key = &text[r];                                                 /* 230 */
    p = N + 1 + key[0];                                             /* 231 */
    rson[r] = lson[r] = NIL;                                        /* 232 */
    matchlen = 0;                                                   /* 233 */

    for (;;)                                                        /* 235 */
    {
        if (cmp >= 0)                                               /* 236 */
        {
            if (rson[p] != NIL)
            {
                p = rson[p];
            }
            else
            {
                rson[p] = r;
                dad[r] = p;
                return;
            }
        }
        else
        {
            if (lson[p] != NIL)
            {
                p = lson[p];
            }
            else
            {
                lson[p] = r;
                dad[r] = p;
                return;
            }
        }

        for (i = 1; i < F; i++)
            if ((cmp = key[i] - text[p + i]) != 0) break;

        if (i > matchlen)                                           /* 263 */
        {
            matchpos = p;                                           /* 264 */
            if ((matchlen = i) >= F) break;                         /* 265 */
        }
    }

    /* A full-length match: put r where p was and drop p out of the tree. */
    dad[r]  = dad[p];                                               /* 269 */
    lson[r] = lson[p];                                              /* 270 */
    rson[r] = rson[p];                                              /* 271 */
    dad[lson[p]] = r;                                               /* 273 */
    dad[rson[p]] = r;                                               /* 274 */

    if (rson[dad[p]] == p) rson[dad[p]] = r;                        /* 277 */
    else                   lson[dad[p]] = r;                        /* 279 */

    dad[p] = NIL;                                                   /* 282 */
}

// --------------------------------------------------------------------------
// Remove node p from the tree.  Standard binary-tree deletion: splice out the
// child if there is only one, otherwise promote the right-most descendant of
// the left subtree.

void delete_node(short p)
{                                                                   /* 289 */
    short q;

    if (dad[p] == NIL) return;      /* not in a tree */             /* 292 */

    if (rson[p] == NIL)                                             /* 295 */
    {
        q = lson[p];                                                /* 296 */
    }
    else if (lson[p] == NIL)                                        /* 297 */
    {
        q = rson[p];                                                /* 298 */
    }
    else                                                            /* 299 */
    {
        q = lson[p];                                                /* 300 */
        if (rson[q] != NIL)                                         /* 302 */
        {
            do
            {
                q = rson[q];                                        /* 303 */
            }
            while (rson[q] != NIL);                                 /* 305 */

            rson[dad[q]] = lson[q];                                 /* 306 */
            dad[lson[q]] = dad[q];                                  /* 307 */
            lson[q] = lson[p];                                      /* 309 */
            dad[lson[p]] = q;                                       /* 310 */
        }

        rson[q] = rson[p];                                          /* 312 */
        dad[rson[p]] = q;                                           /* 313 */
    }

    dad[q] = dad[p];                                                /* 315 */

    if (rson[dad[p]] == p) rson[dad[p]] = q;                        /* 316 */
    else                   lson[dad[p]] = q;                        /* 320 */

    dad[p] = NIL;                                                   /* 322 */
}                                                                   /* 323 */
