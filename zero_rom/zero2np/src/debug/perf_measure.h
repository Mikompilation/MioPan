/* ==========================================================================
 *  debug/perf_measure.h
 *
 *  C_PERFORMANCE_MEASURE -- the one stopwatch the whole engine measures itself
 *  with.  It owns no state but a single mark: EE timer 1 is the clock, and the
 *  object just remembers a count to subtract from.
 *
 *  This is its own translation unit in the ROM (perf_measure.o, 0x70 of .text
 *  and no data at all).  It is a separate header from zero2_perf.h too: of the
 *  17 objects whose stabs carry C_PERFORMANCE_MEASURE, perf_measure.c is the
 *  only one that does *not* also carry C_ZERO2_PERF_CNT and CZero2PerfDisplay
 *  -- so zero2_perf.h includes this one, and not the other way round.
 *
 *  Reconstructed from the Feb 6 2004 prototype (SLES_523.84).
 * ======================================================================== */

#ifndef _DEBUG_PERF_MEASURE_H
#define _DEBUG_PERF_MEASURE_H

/* Single cycle-count stopwatch (0x4). */
class C_PERFORMANCE_MEASURE
{
private:
    int m_MarkCnt;                              /* 0x0 */

public:
    void FrameStart();
    int  Get();
    void SetMark();
    int  GetFromMark();
};

#endif /* _DEBUG_PERF_MEASURE_H */
