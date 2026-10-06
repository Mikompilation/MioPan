/* ==========================================================================
 *  ingame/clear/dat/rank_time_dat.h
 *
 *  The seven clear-time rank thresholds (rank_time_dat.o).
 *
 *  Reconstructed from the Feb 6 2004 prototype (SLES_523.84).
 * ======================================================================== */

#ifndef _INGAME_CLEAR_DAT_RANK_TIME_DAT_H
#define _INGAME_CLEAR_DAT_RANK_TIME_DAT_H

/* rank_time_tbl[rank] = { hour, min, sec } -- the slowest clear time that
 * still earns `rank`, ascending.  GameResultTopRankCheck() walks it from 0 and
 * takes the first entry the play time does not exceed; a play time past the
 * last entry is rank 6.
 *
 * The ROM declares this as a plain int[7][3], not a TIME_INFO[7]: the stabs
 * type is `ar50;0;6` of `ar50;0;2` of int with no struct name, so the source
 * really did subscript it rather than name .hour/.min/.sec. */
extern int rank_time_tbl[7][3];             /* rdata 3c46c0 */

#endif /* _INGAME_CLEAR_DAT_RANK_TIME_DAT_H */
