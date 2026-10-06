// FILE: /home/zero_rom/zero2np/src/debug/zero2_perf.c
//
// The per-frame performance counter, and the one global instance of it.  Five
// functions over 0x240 of .text, one 2-byte string in .sdata, one 10-byte
// format string in .rodata and one .lit4 float -- that is the whole object.
//
// Two things live in here.  FrameInit() is the frame reset: system.c calls it
// straight after the VBlank wait (and again out of init_super()), and it zeros
// the row counter and restarts EE timer 1 through C_PERFORMANCE_MEASURE.
// GetPercent() is the read-back g2d_debug.c's frame meter draws, the elapsed
// tick count over a 20480-tick frame budget.  Those two, plus the
// C_PERFORMANCE_MEASURE halves they call, are the live path.
//
// AddDraw() is the other thing, and it is dead code.  A jal/j scan over the
// loadable segments finds no caller of it, and none of ::SetMark() either;
// ::GetPercentFromMark() has exactly one call site and it is inside AddDraw.
// So the whole mark/row-list half of this file is exported and unreachable --
// the same pattern as ene_mot_ctrl.o's and fly_ctrl.o's exported-but-unused
// pairs.  What it drew was a list, one row per call, 25 pixels apart:
//
//     x 50    the caller's label, in light green
//     x 300   a ':' separator, in grey
//     x 320   the time since the last mark, "%4d.%2d%%" in white
//
// with the marks set by ::SetMark() between the things being measured.  The
// name string is passed straight through to SetASCIIString2(), which is why
// AddDraw's parameter is `const char *` while every other string argument in
// the file is a literal -- and why the call needs the cast, since ZERO2.MAP
// gives SetASCIIString2()'s last parameter as a plain `char *`.
//
// The 0xa5 in the format string is the font's decimal point, the same glyph
// g2d_debug.c's "Now:%4d\xa5%2d%%" uses; the byte is reproduced exactly
// because it indexes the glyph table.  The whole percent/two-decimals idiom --
// (int)(x * 100.0f) and (int)(x * 10000.0f) % 100, the 10000.0f coming out of
// this object's own .lit4 -- is shared verbatim with that file.  The modulo is
// a real `div` with its divide-by-zero trap: EE GCC 2.96 has no magic-number
// division, so a `% 100` spelling survives into the output.
//
// Reconstructed from the Feb 6 2004 prototype (SLES_523.84), zero2_perf.o.
// All 5 ZERO2.MAP exports plus the global; .text is accounted for byte for
// byte (0x26d4a0..0x26d6e0 = 0x34 + 0x14c + 0x1c + 0x38 + 0x38 of bodies, four
// 4-byte alignment fills, and the 8 + 0x20 of static-init machinery below).
//
// .sdata is 0x10: the ":" literal at 3f4900 and c_zero2_perf_cnt at 3f4908.
// .rodata is the 0xa of "%4d\xa5%2d%%", .lit4 the 10000.0f at 3ee9fc.  The
// object also carries a .ctors entry and an *empty* (8-byte, `jr ra`)
// __static_initialization_and_destruction_0 -- compiler machinery for a
// class-type global with nothing to construct.  Nothing is reproduced for it:
// a default-initialised C_ZERO2_PERF_CNT here needs no dynamic init either.
//
// Trailing /* NNN */ comments are the original source line numbers, measured
// from the $LM stabs.  The ROM's brace style is K&R, and this file proves it:
// ::GetPercent's brace is line 40 and its one statement 41, which leaves 42
// for its `}` -- and ::GetPercentFromMark's brace is 43, with no room for a
// signature line of its own.  So a brace line below carries the signature's
// number, the folder's Allman style notwithstanding.

#include "zero2_perf.h"                     // this file's public API

#include "../graphics/graph2d/message.h"    // SetASCIIString2 / SetString2

// ──────────────────────────────────────────────────────────────────────
// The global per-frame counter (sdata 3f4908).

C_ZERO2_PERF_CNT c_zero2_perf_cnt;

// ──────────────────────────────────────────────────────────────────────
// Frame reset: empty the row list and restart the clock, marked at zero.

void C_ZERO2_PERF_CNT::FrameInit()                                      /* 5 */
{
    m_NowCnt = 0;                                                       /* 6 */
    c_performance_measure.FrameStart();                                 /* 7 */
    c_performance_measure.SetMark();                                    /* 8 */
}

// ──────────────────────────────────────────────────────────────────────
// Add one labelled row to the frame's read-out and re-mark for the next one.
//
// DEAD CODE -- no caller anywhere in the loadable segments.
//
// Lines 13..17, 19..22, 24..25, 27..30 and 33 hold no code at all -- 16 of the
// body's 24 lines -- and none of it is recoverable; the object holds no trace
// of what was there.  The y expression is written out at all three call sites
// in the ROM -- three separate reloads of m_NowCnt, with calls in between --
// not hoisted into a local: functions.txt lists Cnt as the only local here.

void C_ZERO2_PERF_CNT::AddDraw(const char *str)                         /* 12 */
{
    float Cnt;

    /* The cast is the ROM's: AddDraw takes `const char *` (PCc in the mangled
     * name) and SetASCIIString2's last parameter is `char *`. */
    SetASCIIString2(0, 50.0f, (float)(m_NowCnt * 25 + 20), 0,
                    160, 255, 160, (char *)str);                        /* 18 */
    SetASCIIString2(0, 300.0f, (float)(m_NowCnt * 25 + 20), 0,
                    160, 160, 160, ":");                                /* 23 */

    Cnt = GetPercentFromMark();                                         /* 26 */

    SetString2(0, 320.0f, (float)(m_NowCnt * 25 + 20), 0,
               255, 255, 255, "%4d\xa5%2d%%",
               (int)(Cnt * 100.0f),
               (int)(Cnt * 10000.0f) % 100);                            /* 31 */

    m_NowCnt++;                                                         /* 32 */
    c_performance_measure.SetMark();                                    /* 34 */
}

// ──────────────────────────────────────────────────────────────────────
// Move the mark to now.  DEAD CODE -- no caller anywhere.

void C_ZERO2_PERF_CNT::SetMark()                                        /* 37 */
{
    c_performance_measure.SetMark();                                    /* 38 */
}

// ──────────────────────────────────────────────────────────────────────
// Elapsed frame time as a fraction of the budget.  20480 timer-1 ticks is
// 35.6 ms at BUSCLK/256, so 1.0 is a little over one 30 fps frame -- which is
// what puts a healthy frame just under g2d_debug.c's 100% grid line.

float C_ZERO2_PERF_CNT::GetPercent()                                    /* 40 */
{
    return (float)c_performance_measure.Get() / 20480.0f;               /* 41 */
}

// ──────────────────────────────────────────────────────────────────────
// The same, measured from the mark instead of from the frame start.  Reached
// only from AddDraw(), so only from dead code.

float C_ZERO2_PERF_CNT::GetPercentFromMark()                            /* 43 */
{
    return (float)c_performance_measure.GetFromMark() / 20480.0f;       /* 44 */
}
