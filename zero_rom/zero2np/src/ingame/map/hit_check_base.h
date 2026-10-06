/* ==========================================================================
 *  ingame/map/hit_check_base.h
 *
 *  Shared geometric hit-test primitives.
 * ======================================================================== */

#ifndef _INGAME_MAP_HIT_CHECK_BASE_H
#define _INGAME_MAP_HIT_CHECK_BASE_H

/* Non-zero when `target` is inside triangle tri0-tri1-tri2 in the floor
 * plane (either winding; edges count as inside). */
int HcBaseIsInTriXZ(const float *target, const float *tri0,
                    const float *tri1, const float *tri2);

/* Perpendicular distance from p0 to the line through p1 with direction v,
 * measured in the floor plane -- Y is ignored, as the name says. */
float HcBasePointLineXZ(float *p0, float *p1, float *v);

/* Segment/segment crossing in the floor plane, sign-of-determinant flavour.
 * DEAD CODE in the ROM (no caller in the loadable segments), along with its
 * two helpers below -- the live test is HcBaseLineIntersect2(). */
int HcBaseLineIntersect(const float *line1_1, const float *line1_2,
                        const float *line2_1, const float *line2_2);

/* Non-zero when a and b sit on opposite sides of (or touch) the line
 * through e1-e2, in the floor plane.  Dead code with its caller. */
int HcBaseLineStraddle(const float *e1, const float *e2,
                       const float *a, const float *b);

/* Which side of the directed line e1->e2 is p on, in the floor plane?
 * Returns +1 / -1 / 0.  Dead code with its callers. */
int HcBaseLineSide(const float *p, const float *e1, const float *e2);

/* Non-zero when segment line1_1..line1_2 crosses segment line2_1..line2_2 in
 * the floor plane.  Touching counts as a crossing. */
int HcBaseLineIntersect2(const float *line1_1, const float *line1_2,
                         const float *line2_1, const float *line2_2);

/* Non-zero when segment a..b crosses the plane of triangle v0-v1-v2; the
 * crossing point lands in `pos` (w = 1).  Plane only -- containment in the
 * triangle is the caller's business. */
int HcBaseIsLineHitFace(float *pos, const float *a, const float *b,
                        const float *v0, const float *v1, const float *v2);

/* AABB overlap reject between segment pos1..pos2 and triangle tri0-tri1-tri2,
 * on all three axes. */
int HcBaseIsNearSegTri(const float *pos1, const float *pos2,
                       const float *tri0, const float *tri1, const float *tri2);

/* Closest feature of rectangle `vec` to `pos`, if within radius r.
 * Returns 0 = clear, 1 = nearest is a corner (written to `a`),
 * 2 = nearest is an edge (endpoints written to `a` and `b`).
 * `len` receives the distance, measured in the floor plane: `pos` carries a
 * real height while the rectangles are flattened to y == 0, so the test has
 * to ignore Y or nothing is ever in range. */
int HcBasePointRectangle(float *len, float *pos, float *a, float *b,
                         float (*vec)[4], float r);

#endif /* _INGAME_MAP_HIT_CHECK_BASE_H */
