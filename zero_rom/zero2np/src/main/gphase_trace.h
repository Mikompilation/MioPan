/* ==========================================================================
 *  main/gphase_trace.h
 *
 *  PORT-ONLY diagnostic.  Not part of the ROM -- there is no equivalent in
 *  SLES_523.84.  It exists because a half-reconstructed build routinely
 *  parks in a phase that is waiting on a subsystem that is still stubbed,
 *  and from the outside that is indistinguishable from a hang or a black
 *  screen.  Several phases (GID_STORY_LOAD_MISSION_EVENT, the loaders, the
 *  gameover waits) draw nothing at all by design, so "black" is the expected
 *  look while parked -- the trace is the only cheap way to tell which one.
 *
 *  Output goes to stdout, one line per phase change plus a stall report:
 *
 *    [gphase]     123  SUPER > STORY_MAIN > STORY_NOWLOADING > STORY_LOAD_MISSION_EVENT(65)
 *    [gphase]  STALL   after 180 frames in STORY_LOAD_MISSION_EVENT(65)
 *
 *  Set MIOPAN_GPHASE_TRACE=0 in the environment to silence it without a
 *  rebuild; anything else (or unset) leaves it on.
 * ======================================================================== */

#ifndef _MAIN_GPHASE_TRACE_H
#define _MAIN_GPHASE_TRACE_H

#include "gphase.h"

/* Short name for a phase id ("STORY_NORMAL"), without the GID_ prefix.
 * Returns "NONE" for GPHASE_ID_NONE and "?" for anything out of range, so it
 * is always safe to print. */
const char *GPhaseIdName(int id);

/* Call once per frame, after now[] has been advanced.  Prints only when the
 * layer vector changes, or when it has not changed for a long time. */
void GPhaseTraceMain(const GPHASE_ID_ENUM *now);

#endif /* _MAIN_GPHASE_TRACE_H */
