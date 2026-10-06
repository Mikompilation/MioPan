/* ==========================================================================
 *  ingame/map/map_hit_check.h
 *
 *  Map-hit debug drawing interface.
 * ======================================================================== */

#ifndef _INGAME_MAP_MAP_HIT_CHECK_H
#define _INGAME_MAP_MAP_HIT_CHECK_H

/* DrawMapHitRect / DrawMapHitRectOne are declared in map_rectangle.h -- they
 * are map_rectangle.o symbols, not map_hit_check.o ones. */

/* Resolves `now` out of any wall rectangle it ended up inside, using `old`
 * (the last known-clear position) as the fallback when it cannot.  Writes the
 * result to v0; non-zero when v0 differs from `now`. */
int MapHitCheck(float *v0, float *now, float *old, float r, int kai);

/* Push-out primitives, shared with MapHit.c's default hit callback.  `a` is a
 * corner, `a`/`b` an edge, `len` the distance HcBasePointRectangle() measured;
 * v1 is moved to clear `r` and written to v0. */
int MapHitCollisionPoint(float *v0, float *v1, float *a, float len, float r);
int MapHitCollisionLine(float *v0, float *v1, float *a, float *b,
                        float len, float r);

/* Line-of-sight test between two points on (possibly different) floors.
 * Non-zero when a wall or a closed door blocks the segment.  A non-zero r
 * widens the segment into a capsule-ish quad of half-width r and tests all
 * four of its edges instead; r == 0 tests the bare segment. */
int MapHitLineCheck(float *pos1, int kai1, float *pos2, int kai2, float r);

#endif /* _INGAME_MAP_MAP_HIT_CHECK_H */
