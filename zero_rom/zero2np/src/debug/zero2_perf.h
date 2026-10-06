/* ==========================================================================
 *  debug/zero2_perf.h
 *
 *  C_ZERO2_PERF_CNT -- one C_PERFORMANCE_MEASURE plus the row counter for the
 *  per-frame debug read-out.  The global instance c_zero2_perf_cnt is reset
 *  once a frame from the system frame loop, and g2d_debug.c's frame meter
 *  reads GetPercent() off it.
 *
 *  Included by 15 translation units in the ROM, all of which see this class,
 *  C_PERFORMANCE_MEASURE (via perf_measure.h) and CZero2PerfDisplay together.
 *
 *  CZero2PerfDisplay is deliberately absent.  types.txt has it -- 0x8, a
 *  `char *m_pFuncName` and an `int m_iLine`, with a (int, const char *) ctor
 *  and a destructor, i.e. a scoped block timer -- but ZERO2.MAP carries no
 *  body for any of its members and no object expands one, so nothing about
 *  what it measured or printed survives.  It is left out rather than invented.
 *
 *  Reconstructed from the Feb 6 2004 prototype (SLES_523.84).
 * ======================================================================== */

#ifndef _DEBUG_ZERO2_PERF_H
#define _DEBUG_ZERO2_PERF_H

#include "perf_measure.h"           /* C_PERFORMANCE_MEASURE */

/* Per-frame performance counter (0x8): draw-call count + one stopwatch. */
class C_ZERO2_PERF_CNT
{
private:
    int                   m_NowCnt;             /* 0x0 */
    C_PERFORMANCE_MEASURE c_performance_measure;/* 0x4 */

public:
    void  FrameInit();
    void  SetMark();
    void  AddDraw(const char *str);
    float GetPercent();
    float GetPercentFromMark();
};

/* The global per-frame counter (sdata 3f4908). */
extern C_ZERO2_PERF_CNT c_zero2_perf_cnt;

#endif /* _DEBUG_ZERO2_PERF_H */
