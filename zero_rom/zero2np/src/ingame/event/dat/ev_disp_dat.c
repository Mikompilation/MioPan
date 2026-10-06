// FILE: /home/zero_rom/zero2np/src/ingame/event/dat/ev_disp_dat.c
//
// Pure data file: the sprite placements and file numbers ev_disp.c's three
// overlay layers draw with.  The object contributes no code of its own -- its
// whole .text is fixed_array template boilerplate plus the constructor that
// binds chapter_tim_file to its table.
//
// As in ev_talk_dat.c, the ROM's array sits at line 231 and the bound object
// at line 232, so ~230 lines of the original precede them and generate
// nothing.
//
// Reconstructed from the Feb 6 2004 prototype (SLES_523.84).

#include "ev_disp_dat.h"

#include "../../../system/eeiop/cddat.h"             // CHAPTERn_TM2

/* Two framings of the full-screen event image.  Both are drawn at pri 160
 * with blending on; [0] is a square 256x256 crop and [1] the full 275x238
 * picture, sitting slightly lower and further left.  x/y are placeholders --
 * EvDisp2DStartReq() replaces them with the caller's coordinates. */
SPRT_DAT ev_disp2d_dat[2] =                                         /* data 30dbf8 */
{
    { .tex0 = 0, .u = 0, .v = 0, .w = 256, .h = 256, .x = 194, .y = 49, .pri = 160, .alpha = 128, .flip = 0, .bln = 1 },
    { .tex0 = 0, .u = 0, .v = 0, .w = 275, .h = 238, .x = 184, .y = 58, .pri = 160, .alpha = 128, .flip = 0, .bln = 1 }
};

/* The chapter title card is 510x128 on screen but its texture is only 255
 * wide, so it ships as two 255x128 bands stacked in the TIM2 (v = 0 and
 * v = 128) and drawn side by side at x = 0 and x = 255.  EvChapterDispExe()
 * issues them as two DispSprD() calls sharing one alpha. */
SPRT_DAT ev_chapter_dat[2] =                                        /* data 30dc38 */
{
    { .tex0 = 0, .u = 0, .v = 0,   .w = 255, .h = 128, .x = 0,   .y = 320, .pri = 160, .alpha = 128, .flip = 0, .bln = 1 },
    { .tex0 = 0, .u = 0, .v = 128, .w = 255, .h = 128, .x = 255, .y = 320, .pri = 160, .alpha = 128, .flip = 0, .bln = 1 }
};

/* rodata 3a9a08.  Base file of each chapter's title card.  EvChapterDataLoadReq
 * adds GetLanguage() to the entry, and the file table is laid out to suit:
 * CHAPTERn_TM2 is immediately followed by its _F_ / _G_ / _S_ / _I_ variants,
 * which is why the entries step by five.  Note the indices are chapter_num,
 * so entry 0 is CHAPTER1.  const because that is the section the ROM used --
 * see ev_talk_dat.c for why the cast below is spelled out. */
static const int chapter_tim_file_dat[11] =                         /* 231 */
{
    CHAPTER1_TM2,
    CHAPTER2_TM2,
    CHAPTER3_TM2,
    CHAPTER4_TM2,
    CHAPTER5_TM2,
    CHAPTER6_TM2,
    CHAPTER7_TM2,
    CHAPTER8_TM2,
    CHAPTER9_TM2,
    CHAPTER10_TM2,
    CHAPTER11_TM2
};

/* Bound at static-init time; this object's single .ctors entry (0x2c3b80). */
reference_fixed_array<int, 11> chapter_tim_file(const_cast<int *>(chapter_tim_file_dat));  /* 232 */
