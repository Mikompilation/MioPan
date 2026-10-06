// FILE: /home/zero_rom/zero2np/src/album/prg/album_mem.c
//
// The album's memory-card staging buffer.  All four ZERO2.MAP exports and the
// one static are reconstructed; the object has no other code and no data
// beyond the two __FUNCTION__ strings, so this is the whole translation unit.
//
// .text is accounted for byte-for-byte -- 0x125f10..0x1260b4 is 0x19c of code
// in four bodies plus two 4-byte alignment fills, so there is no unlisted
// body.  .rodata (0x3a0dd0, 0x95) is exactly the six strings below, ending on
// "AlbumMemFree"'s NUL, and .bss (0x422120, 0xc) is album_mem_ctrl alone.
// (The ROM's __FILE__ expanded to the bare "album_mem.c"; the host's expands
// to the full path, so that one string's bytes differ by build convention.)
//
// Copying an album to or from a card needs a megabyte-scale block, and by the
// time the album is open every heap in the map is already claimed.  So the
// load and save screens borrow one from the top of the 3D DMA packet ring --
// which is safe because a card transfer runs with almost nothing being drawn.
//
// The arithmetic, and where its two constants come from:
//
//   * main.c hands dmaVif1Init() the ring as (PACKET3D_ADDR | 0x30000000)
//     with 0x1ec30 tags.  0x1ec30 == 126000, and 126000 * 32 == 0x3d8600 ==
//     PACKET2D_ADDR - PACKET3D_ADDR -- so a tag is 32 bytes and the ring is
//     exactly the gap between the two symbols.  That is the ROM's literal
//     0x3d8600 in AlbumMemInit's bound and in AlbumMemMain's shift.
//   * The block is handed out at PACKET2D_ADDR - mem_size, i.e. the ring's
//     tail, immediately under the 2D packet area.  That is the ROM's literal
//     0x1e79b00.
//
// Both are spelled symbolically here.  The values are identical to the ROM's
// words; system.h already carries the map.
//
// AlbumMemMain() is the ONLY caller of dmaVif1Resize() anywhere in the ROM --
// a jal/j scan over the loadable segments finds no other.  So the ring is
// never restored by anything else, and in particular AlbumMemFree() issues no
// resize of its own: it clears the control block and leaves the ring short.
// Because Free() also puts mem_size back to 0 and step back to 0, the next
// AlbumMemMain() to run would resize to (0x3d8600 - 0) >> 5 -- the full 126000
// tags -- so the tags do come back, but only via a later Main(), never via
// Free() itself.  Reproduced as found.
//
// Its callers are album_load.o and album_save.o, which are source-level twins
// (the call sites sit at matching offsets in AlbumLoadMain / AlbumSaveMain and
// in AlbumLoadOutReq / AlbumSaveOutReq).  Neither is reconstructed yet, so
// nothing in the port reaches this file so far.  album_save.c asks for
// 0x100000 -- a quarter of the ring.
//
// Reconstructed from the Feb 6 2004 prototype (SLES_523.84).
//
// Trailing /* NNN */ comments are the original source line numbers, measured
// from symbols.txt's $LM/SOL records rather than guessed.  Two gaps in them
// are real and hold no code: GetAlbumMemAddr() opens at 113 and its return is
// 117, and AlbumMemMain()'s `res = 1` arm sits somewhere in 99..104 with no
// note of its own (it compiles to a single `li` in a branch-likely delay
// slot).  `int res = 0;` likewise leaves no note, which is what marks it a
// declaration initialiser rather than a statement.

#include "album_mem.h"

#include "../../common/utility2.h"                  // PRINT_ASSERT
#include "../../graphics/dmaVif1.h"                 // dmaVif1Resize / dmaVif1IsResizeOK
#include "../../miopan/miopan_memory.h"             // MioPan_GetHostPointer
#include "../../system/os/system.h"                 // PACKET3D_ADDR / PACKET2D_ADDR

#include <stdio.h>                                  // printf
#include <string.h>                                 // memset

/* The 3D DMA packet ring, which is what the album borrows from: 0x3d8600
 * bytes between the two map symbols, or 126000 tags of 32 -- exactly the
 * 0x1ec30 main.c hands dmaVif1Init(). */
#define ALBUM_MEM_AREA_SIZE     ((u_int)(PACKET2D_ADDR - PACKET3D_ADDR))
#define ALBUM_MEM_AREA_END      PACKET2D_ADDR
#define ALBUM_MEM_TAG_SIZE      32u

/* ALBUM_MEM_CTRL::step. */
#define ALBUM_MEM_STEP_RESIZE_REQ   0   /* ask dmaVif1 to shrink the ring  */
#define ALBUM_MEM_STEP_RESIZE_WAIT  1   /* poll it, then clear the block   */
#define ALBUM_MEM_STEP_READY        2   /* the block is the caller's       */

/* ---- state -------------------------------------------------------------- */

/* bss 422120.  A file static of album_mem.c, so the type belongs here rather
 * than in the header.  All three offsets match the ROM's; only sizeof differs,
 * 0x10 against 0xc, because mem_addr widens from 4 bytes to 8. */
typedef struct                                      /* 0xc */
{
    /* 0x0 */ u_int mem_size;
    /* 0x4 */ char  step;
    /* 0x8 */ void *mem_addr;
} ALBUM_MEM_CTRL;

static ALBUM_MEM_CTRL album_mem_ctrl;

/* ==========================================================================
 *  Entry points
 * ======================================================================== */

/* Record a request for `get_size` bytes off the top of the packet ring.  The
 * ring is not touched here -- AlbumMemMain() is what resizes it.
 *
 * The bound reads `>= ALBUM_MEM_AREA_SIZE` because GCC canonicalises an
 * unsigned `x >= K` into `x > K - 1` to reach MIPS's single sltu, and the ROM
 * compares against 0x3d85ff.  The two spellings are indistinguishable in the
 * output; this one is the meaningful constant -- a request for the whole ring
 * would leave it with no tags.  Note the assert does not return: an oversized
 * request is reported and then stored anyway. */
void AlbumMemInit(u_int get_size, const char *file, int line)           /* 58 */
{
    printf("Call %s File[%s] Line[%d]\n", __FUNCTION__, file, line);    /* 60 */

    if (get_size >= ALBUM_MEM_AREA_SIZE) {                              /* 63 */
        PRINT_ASSERT("Error! %s Size Over", __FUNCTION__);              /* 64 */
    }

    album_mem_ctrl.step     = ALBUM_MEM_STEP_RESIZE_REQ;                /* 68 */
    album_mem_ctrl.mem_size = get_size;                                 /* 69 */
    album_mem_ctrl.mem_addr = NULL;                                     /* 70 */
}

/* Drive the claim, a step per frame.  Returns 0 while the resize is in
 * flight, 1 once the block is reserved and cleared.
 *
 * iNumTag is computed unconditionally, ahead of the step test -- its $LM (85)
 * is below the test's (89) and a hoisted copy would have kept the line it was
 * written on, so that is the source's own order rather than the scheduler's.
 *
 * The ROM emits a bare `srl ...,5`.  Both operands are unsigned here, so a
 * `/ 32` and a `>> 5` reach that same single instruction -- neither the bias
 * sequence that would mark a signed divide nor an `sra` appears, so the two
 * spellings are indistinguishable and the byte-size-to-tag-count one is
 * written.  See [[shift-vs-divide-by-power-of-two]]. */
int AlbumMemMain(void)                                                  /* 80 */
{
    int iNumTag;
    int res = 0;

    iNumTag = (ALBUM_MEM_AREA_SIZE - album_mem_ctrl.mem_size)
              / ALBUM_MEM_TAG_SIZE;                                     /* 85 */

    if (album_mem_ctrl.step == ALBUM_MEM_STEP_RESIZE_REQ) {             /* 89 */
        if (dmaVif1Resize(iNumTag) != 0) {                              /* 90 */
            /* PORT: the ROM stores the raw EE address
               (PACKET2D_ADDR - mem_size).  The host has no such window, so it
               is translated here rather than at each use -- the memset below
               and every caller of GetAlbumMemAddr() write through it.
               MioPan_GetHostPointer() is idempotent, so a caller that
               translates again is still safe. */
            album_mem_ctrl.mem_addr = MioPan_GetHostPointer(
                ALBUM_MEM_AREA_END - album_mem_ctrl.mem_size);          /* 91 */
            album_mem_ctrl.step = ALBUM_MEM_STEP_RESIZE_WAIT;           /* 92 */
        }
    } else if (album_mem_ctrl.step == ALBUM_MEM_STEP_RESIZE_WAIT) {     /* 95 */
        if (dmaVif1IsResizeOK() != 0) {                                 /* 96 */
            memset(album_mem_ctrl.mem_addr, 0, album_mem_ctrl.mem_size);/* 97 */
            album_mem_ctrl.step = ALBUM_MEM_STEP_READY;                 /* 98 */
        }
    } else {
        res = 1;
    }

    return res;                                                         /* 106 */
}

/* The block, or NULL until AlbumMemMain() has reported 1.  Lines 114..116
 * hold no code. */
void *GetAlbumMemAddr(void)                                             /* 113 */
{
    return album_mem_ctrl.mem_addr;                                     /* 117 */
}

/* Give the block back.  This only clears the control block -- see the file
 * banner: no resize is issued, so the ring stays short until a later
 * AlbumMemMain() runs with mem_size back at 0. */
void AlbumMemFree(const char *file, int line)                           /* 125 */
{
    printf("Call %s File[%s] Line[%d]\n", __FUNCTION__, file, line);    /* 127 */

    album_mem_ctrl.step     = ALBUM_MEM_STEP_RESIZE_REQ;                /* 130 */
    album_mem_ctrl.mem_size = 0;                                        /* 131 */
    album_mem_ctrl.mem_addr = NULL;                                     /* 132 */
}
