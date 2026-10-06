// FILE: /home/zero_rom/zero2np/src/debug/perf_measure.c
//
// The engine's stopwatch.  Four functions, 0x70 of .text, and no data sections
// at all -- perf_measure.o has no .rodata, .data, .bss, .sdata or .sbss, which
// is why globals.txt has no section for the file.  Everything it reads is in
// EE timer 1.
//
// FrameStart() programs T1_MODE with 0xc82 and that single literal is the
// whole clock specification:
//
//     bits 1..0  CLKS = 2   count BUSCLK/256, i.e. 147.456 MHz / 256 = 576 kHz
//     bit  7     CUE  = 1   start counting
//     bit  10    EQUF = 1   write-one-to-clear the compare flag
//     bit  11    OVFF = 1   write-one-to-clear the overflow flag
//
// so a tick is 1/576000 s and the 16-bit count wraps every 113.8 ms.  It is
// zeroed once a frame from c_zero2_perf_cnt.FrameInit(), called out of the
// system frame loop straight after the VBlank wait, and read back by
// C_ZERO2_PERF_CNT::GetPercent() against a divisor of 20480.  That divisor is
// the frame budget: 20480 ticks is 35.6 ms, so a 30 fps frame (33.4 ms, 19219
// ticks) reads as 93.8% and the meter's 100% grid line sits just past it.
//
// Only FrameStart() and Get() are live.  A jal/j scan over the loadable
// segments finds SetMark() called three times and GetFromMark() once, but all
// four of those sites are inside zero2_perf.o, and the two that reach them
// (C_ZERO2_PERF_CNT::SetMark and ::AddDraw) have no caller anywhere -- so the
// mark half of this class is only exercised by dead code.
//
// PORT: on the host REG_RCNT1_COUNT is a proxy over the steady clock, ticking
// at the same 576 kHz and wrapping at 16 bits, so this file is unchanged.  See
// sdk/eeregs.h.
//
// Reconstructed from the Feb 6 2004 prototype (SLES_523.84), perf_measure.o.
// All 4 ZERO2.MAP exports; .text is accounted for byte for byte
// (0x22dea8..0x22df18 = 0x28 + 0x14 + 0x14 + 0x18 + one 4-byte alignment fill
// after Get and after SetMark, so there is no unlisted body).
//
// Trailing /* NNN */ comments are the original source line numbers, measured
// from the $LM stabs.  The ROM's own brace style here is K&R -- SetMark's
// opening brace is line 42 and Get's closing brace is line 39, which leaves no
// room for a signature line of its own -- so a brace line below carries the
// signature's number, the folder's Allman style notwithstanding.

#include "perf_measure.h"           // this file's public API

#include "../sdk/eeregs.h"          // REG_RCNT1_COUNT / REG_RCNT1_MODE

// ──────────────────────────────────────────────────────────────────────
// Start a fresh measurement window: reprogram the timer, zero it, and take a
// mark at zero.  Line 29 and line 32 hold no code.

void C_PERFORMANCE_MEASURE::FrameStart()                                /* 28 */
{
    REG_RCNT1_MODE  = 0xc82;                                            /* 30 */
    REG_RCNT1_COUNT = 0;                                                /* 31 */

    m_MarkCnt = REG_RCNT1_COUNT;                                        /* 33 */
}

// ──────────────────────────────────────────────────────────────────────
// Ticks since FrameStart() zeroed the counter.

int C_PERFORMANCE_MEASURE::Get()                                        /* 36 */
{
    int i_MarkCnt = REG_RCNT1_COUNT;                                    /* 37 */
    return i_MarkCnt;                                                   /* 38 */
}                                                                       /* 39 */

// ──────────────────────────────────────────────────────────────────────
// Move the mark to now.

void C_PERFORMANCE_MEASURE::SetMark()                                   /* 42 */
{
    m_MarkCnt = REG_RCNT1_COUNT;                                        /* 43 */
}

// ──────────────────────────────────────────────────────────────────────
// Ticks since the mark.  No wrap correction: the caller is expected to read it
// inside the same frame the mark was taken in, well short of the counter's
// 113.8 ms period.

int C_PERFORMANCE_MEASURE::GetFromMark()                                /* 46 */
{
    int i_MarkCnt = REG_RCNT1_COUNT;                                    /* 47 */
    return i_MarkCnt - m_MarkCnt;                                       /* 48 */
}
