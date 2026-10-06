// FILE: /home/zero_rom/zero2np/src/ingame/map/hit_check_base.c
//
// Shared geometric hit-test primitives used by the height mesh and the map's
// hit rectangles.
//
// Reconstructed from the Feb 6 2004 prototype (SLES_523.84), hit_check_base.o
// 0x001c7498..0x001c8217.  All nine ZERO2.MAP exports plus the one static;
// .text is accounted for byte-for-byte (four 4-byte alignment fills, no gaps),
// and the object has no data sections at all.
//
// The ROM mixes real libvu0 calls with inline COP2 from g3dxVu0.h, and each
// call site below preserves which form the object emitted: sceVu0* is a jal
// in the ROM, g3dxVu0* an inline (the quadword copy at g3dxVu0.h 134-135, the
// four-lane add at 161, the four-lane subtract at 184-185, the xyz dot at
// 389-392, the XZ dot at 433-436, the plane evaluation at 2095, and two
// identical scalar square-root inlines at 2247 and 2252).
//
// HcBaseLineIntersect() and its two helpers are dead code in this build: a
// jal scan of the loadable segments finds no caller of HcBaseLineIntersect,
// and HcBaseLineStraddle / HcBaseLineSide are reached only from inside the
// chain.  The live segment-crossing test is HcBaseLineIntersect2().

#include "hit_check_base.h"

#include <libvu0.h>

#include "graphics/graph3d/g3dxVu0.h"

/* The AABB rejects in HcBaseLineIntersect() and HcBaseLineIntersect2() emit
 * plain FPU compare-selects attributed to the if's own source line (95-98 and
 * 137-140 respectively).  An inline function -- whether in this file or in a
 * header -- would carry its own line numbers instead, so the original spelled
 * the selects inside the expression; macros reproduce that. */
#define HC_MIN(a, b)    (((a) < (b)) ? (a) : (b))
#define HC_MAX(a, b)    (((a) < (b)) ? (b) : (a))

static int HcBaseIsNearRectangle(float (*vec)[4], float *pos, float r);

/* Is `target` inside triangle tri0-tri1-tri2, looking down the Y axis?
 * The three corner->target cross products share a Y sign exactly when the
 * point is inside (either winding is accepted).  An edge counts as inside:
 * both tests are >= / <=. */
int HcBaseIsInTriXZ(const float *target, const float *tri0,
                    const float *tri1, const float *tri2)
{                                                                       /* 34 */
    float p[3][4];
    float avTemp[3][4];
    int i;

    g3dxVu0SubVector(p[0], tri0, target);
    g3dxVu0SubVector(p[1], tri1, target);
    g3dxVu0SubVector(p[2], tri2, target);

    for (i = 0; i < 3; i++)                                             /* 43 */
    {
        sceVu0OuterProduct(avTemp[i], p[i], p[(i + 1) % 3]);            /* 44 */
    }                                                                   /* 45 */

    if (avTemp[0][1] >= 0.0f && avTemp[1][1] >= 0.0f && avTemp[2][1] >= 0.0f)   /* 49 */
    {
        return 1;
    }
    if (avTemp[0][1] <= 0.0f && avTemp[1][1] <= 0.0f && avTemp[2][1] <= 0.0f)   /* 52 */
    {
        return 1;
    }

    return 0;                                                           /* 56 */
}                                                                       /* 57 */

/* Perpendicular distance from p0 to the line through p1 along v, measured in
 * the floor plane.  Derived as |p0-p1|^2 - ((p0-p1).v)^2/|v|^2, i.e.
 * Pythagoras on the projection -- every dot is XZ-only, which is what keeps
 * the flattened hit rectangles reachable from a position that carries the
 * real floor height (see g3dxVu0InnerProductXZ).
 *
 * The repeated inner products are spelled out in the ROM too: the pair inside
 * the if is CSEd with the condition's copy (same extended basic block), which
 * is why only three expansions appear in the object.  No local holds them --
 * the stabs list only r and p1p0. */
float HcBasePointLineXZ(float *p0, float *p1, float *v)
{                                                                       /* 66 */
    float r;
    float p1p0[4];

    sceVu0SubVector(p1p0, p0, p1);                                      /* 70 */

    r = g3dxVu0InnerProductXZ(p1p0, p1p0);

    if (g3dxVu0InnerProductXZ(v, v) != 0.0f)                            /* 76 */
    {
        r -= g3dxVu0InnerProductXZ(p1p0, v) * g3dxVu0InnerProductXZ(p1p0, v)
           / g3dxVu0InnerProductXZ(v, v);                               /* 77 */
    }

    return g3dxVu0Sqrt(r);                                              /* 80 */
}

/* Segment/segment crossing in the floor plane, sign-of-determinant flavour.
 *
 * DEAD CODE in this build -- no caller anywhere in the loadable segments.
 * The shipped test is HcBaseLineIntersect2() below, which does the same job
 * with cross products; this trio is the scalar prototype of it. */
int HcBaseLineIntersect(const float *line1_1, const float *line1_2,
                        const float *line2_1, const float *line2_2)
{                                                                       /* 93 */
    if (HC_MIN(line1_1[0], line1_2[0]) > HC_MAX(line2_1[0], line2_2[0]))    /* 95 */
    {
        return 0;
    }
    if (HC_MIN(line1_1[2], line1_2[2]) > HC_MAX(line2_1[2], line2_2[2]))    /* 96 */
    {
        return 0;
    }
    if (HC_MIN(line2_1[0], line2_2[0]) > HC_MAX(line1_1[0], line1_2[0]))    /* 97 */
    {
        return 0;
    }
    if (HC_MIN(line2_1[2], line2_2[2]) > HC_MAX(line1_1[2], line1_2[2]))    /* 98 */
    {
        return 0;
    }

    return HcBaseLineStraddle(line1_1, line1_2, line2_1, line2_2) &&
           HcBaseLineStraddle(line2_1, line2_2, line1_1, line1_2);      /* 101 */
}                                                                       /* 102 */

/* Non-zero when a and b sit on opposite sides of (or touch) the line through
 * e1-e2.  Dead code with its caller, see HcBaseLineIntersect(). */
int HcBaseLineStraddle(const float *e1, const float *e2,
                       const float *a, const float *b)
{                                                                       /* 110 */
    return HcBaseLineSide(a, e1, e2) * HcBaseLineSide(b, e1, e2) <= 0;  /* 112 */
}

/* Which side of the directed line e1->e2 is p on, in the floor plane?
 * +1 / -1 / 0 for exactly-on.  Dead code with its callers. */
int HcBaseLineSide(const float *p, const float *e1, const float *e2)
{                                                                       /* 122 */
    float n = p[0] * (e1[2] - e2[2]) + e1[0] * (e2[2] - p[2])
            + e2[0] * (p[2] - e1[2]);                                   /* 124 */

    if (n > 0.0f)                                                       /* 126 */
    {
        return 1;
    }
    if (n < 0.0f)                                                       /* 127 */
    {
        return -1;
    }
    return 0;                                                           /* 128 */
}                                                                       /* 129 */

/* Segment/segment crossing in the floor plane (0x001c78d0).
 *
 * Two stages, and the first is not just an optimisation -- it is what makes
 * the collinear case behave.  The AABBs of the two segments are overlap-tested
 * on X and Z in both directions; only then does the sign test run.
 *
 * The sign test is the standard "each segment straddles the other's line",
 * done with cross products rather than a determinant because the ROM has VU0:
 * outer[1] is the Y component of the cross product, which for two vectors in
 * the XZ plane is the 2D perp-dot.  Opposite signs on the two endpoints means
 * they lie either side.  Both tests admit zero, so an endpoint exactly on the
 * other line counts as a hit -- that is deliberate, since map rectangles are
 * authored edge-to-edge and a strict test would leak through the seams. */
int HcBaseLineIntersect2(const float *line1_1, const float *line1_2,
                         const float *line2_1, const float *line2_2)
{                                                                       /* 133 */
    float line12[4];
    float line1p[4];
    float outer[4];
    float outer2[4];

    if (HC_MIN(line1_1[0], line1_2[0]) > HC_MAX(line2_1[0], line2_2[0]))    /* 137 */
    {
        return 0;
    }
    if (HC_MIN(line1_1[2], line1_2[2]) > HC_MAX(line2_1[2], line2_2[2]))    /* 138 */
    {
        return 0;
    }
    if (HC_MIN(line2_1[0], line2_2[0]) > HC_MAX(line1_1[0], line1_2[0]))    /* 139 */
    {
        return 0;
    }
    if (HC_MIN(line2_1[2], line2_2[2]) > HC_MAX(line1_1[2], line1_2[2]))    /* 140 */
    {
        return 0;
    }

    /* Does line2 straddle line1? */
    g3dxVu0SubVector(line12, line1_2, line1_1);
    g3dxVu0SubVector(line1p, line2_1, line1_1);
    sceVu0OuterProduct(outer, line12, line1p);                          /* 145 */
    g3dxVu0SubVector(line1p, line2_2, line1_1);
    sceVu0OuterProduct(outer2, line12, line1p);                         /* 147 */
    if ((outer[1] * outer2[1]) > 0.0f)                                  /* 148 */
    {
        return 0;
    }

    /* ...and line1 straddle line2? */
    g3dxVu0SubVector(line12, line2_2, line2_1);
    g3dxVu0SubVector(line1p, line1_1, line2_1);
    sceVu0OuterProduct(outer, line12, line1p);                          /* 151 */
    g3dxVu0SubVector(line1p, line1_2, line2_1);
    sceVu0OuterProduct(outer2, line12, line1p);                         /* 153 */
    if ((outer[1] * outer2[1]) <= 0.0f)                                 /* 154 */
    {
        return 1;
    }

    return 0;                                                           /* 158 */
}                                                                       /* 160 */

/* Where does segment a-b cross the plane of triangle v0-v1-v2?  Non-zero when
 * it does, with the crossing point in `pos` (w = 1).  Face orientation is not
 * checked here -- the caller (MhIsHitHeightTri) re-filters with the XZ
 * containment test.
 *
 * `face` is the plane as a four-vector: normal in xyz, -(n.v0) in w.  The
 * negation runs through doubles -- the ROM multiplies by a double -1.0 via
 * fptodp/dpmul/dptofp -- and `d` is a real source local the stabs do not
 * list (it survives in $f20 across the copy call; float locals routinely
 * leave no stab). */
int HcBaseIsLineHitFace(float *pos, const float *a, const float *b,
                        const float *v0, const float *v1, const float *v2)
{                                                                       /* 169 */
    float n[4];
    float v01[4];
    float v02[4];
    float face[4];
    float ab[4];
    float t;
    float d;

    g3dxVu0SubVector(v01, v1, v0);
    g3dxVu0SubVector(v02, v2, v0);
    sceVu0OuterProduct(n, v01, v02);                                    /* 176 */

    d = g3dxVu0InnerProduct(n, v0) * -1.0;                              /* 180 */
    sceVu0CopyVector(face, n);                                          /* 182 */
    face[3] = d;                                                        /* 183 */

    g3dxVu0SubVector(ab, b, a);

    /* The denominator dot is written twice in the source as well: the second
     * copy sits past the join point, where GCC 2.96's CSE cannot reach, which
     * is why the object carries two expansions of it. */
    if (g3dxVu0InnerProduct(face, ab) == 0.0f)
    {
        return 0;
    }

    t = -(g3dxVu0PlaneDot(face, a) / g3dxVu0InnerProduct(face, ab));

    if (t >= 0.0f && t <= 1.0f)                                         /* 196 */
    {
        sceVu0ScaleVector(ab, ab, t);                                   /* 198 */
        g3dxVu0AddVector(pos, a, ab);
        pos[3] = 1.0f;                                                  /* 200 */
    }
    else
    {
        return 0;
    }
    return 1;                                                           /* 206 */
}                                                                       /* 207 */

/* Cheap proximity reject: do the axis-aligned boxes of segment pos1-pos2 and
 * triangle tri0-tri1-tri2 overlap on all three axes?  Touching boxes count as
 * separate (>=), unlike the rectangle test below. */
int HcBaseIsNearSegTri(const float *pos1, const float *pos2,
                       const float *tri0, const float *tri1, const float *tri2)
{                                                                       /* 215 */
    int j;
    float trimax[4];
    float trimin[4];
    float segmax[4];
    float segmin[4];

    g3dxVu0CopyVector(trimax, tri0);
    g3dxVu0CopyVector(trimin, tri0);
    g3dxVu0CopyVector(segmax, pos1);
    g3dxVu0CopyVector(segmin, pos1);

    for (j = 0; j < 3; j++)                                             /* 224 */
    {
        if (trimax[j] < tri1[j])                                        /* 226 */
        {
            trimax[j] = tri1[j];                                        /* 227 */
        }
        else if (tri1[j] < trimin[j])                                   /* 229 */
        {
            trimin[j] = tri1[j];
        }

        if (trimax[j] < tri2[j])                                        /* 232 */
        {
            trimax[j] = tri2[j];                                        /* 233 */
        }
        else if (tri2[j] < trimin[j])                                   /* 235 */
        {
            trimin[j] = tri2[j];
        }

        if (segmax[j] < pos2[j])                                        /* 240 */
        {
            segmax[j] = pos2[j];                                        /* 241 */
        }
        else if (pos2[j] < segmin[j])                                   /* 243 */
        {
            segmin[j] = pos2[j];
        }

        if (segmin[j] >= trimax[j] || trimin[j] >= segmax[j])           /* 247 */
        {
            return 0;
        }
    }

    return 1;                                                           /* 254 */
}                                                                       /* 255 */

/* Nearest feature of rectangle `vec` to `pos`, within radius r.
 * Returns 0 = clear, 1 = nearest is a corner (written to `a`),
 * 2 = nearest is an edge (endpoints written to `a` and `b`), with the
 * distance in *len.  Everything is measured in the floor plane.
 *
 * Each edge is classified by the sign of the dot products at its two ends:
 * if `pos` projects beyond an endpoint the nearest feature there is that
 * corner, otherwise it is the edge's perpendicular.  Corner distances are
 * kept SQUARED in p_len all the way through the loop -- both candidates and
 * the running best -- and only delinearised into tmp_len after it, which is
 * also why the corner-vs-edge comparison below uses tmp_len.  The sqrt runs
 * before p_flg is tested, so a corner-less rectangle takes the square root
 * of the -1.0 initialiser; the result is never read on that path.
 *
 * An edge beats a corner when both are in range because sliding along a wall
 * is the behaviour you want when standing exactly on a corner. */
int HcBasePointRectangle(float *len, float *pos, float *a, float *b,
                         float (*vec)[4], float r)
{                                                                       /* 304 */
    int   i, p_flg = 0, l_flg = 0;                                      /* 305 */
    float line_p1[4], line_p2[4], tmp1[4], tmp2[4], p_vec[4], l_vec[2][4];
    float p_len = -1.0f, l_len = -1.0f, tmp_len;                        /* 307 */

    if (HcBaseIsNearRectangle(vec, pos, r) == 0)                        /* 310 */
    {
        return 0;
    }

    for (i = 0; i < 4; i++)                                             /* 314 */
    {
        sceVu0CopyVector(line_p1, vec[i]);                              /* 315 */
        sceVu0CopyVector(line_p2, vec[(i + 1) % 4]);                    /* 316 */

        sceVu0SubVector(tmp1, pos, line_p1);                            /* 317 */
        sceVu0SubVector(tmp2, line_p2, line_p1);                        /* 318 */

        /* Behind the near end: the corner at line_p1 is the feature. */
        if (g3dxVu0InnerProductXZ(tmp1, tmp2) < 0.0f)
        {
            tmp_len = g3dxVu0InnerProductXZ(tmp1, tmp1);
            if (p_flg == 0)                                             /* 321 */
            {
                p_len = tmp_len;
                sceVu0CopyVector(p_vec, line_p1);                       /* 324 */
                p_flg = 1;
            }
            else if (tmp_len < p_len)                                   /* 326 */
            {
                p_len = tmp_len;
                sceVu0CopyVector(p_vec, line_p1);                       /* 328 */
            }
            continue;
        }

        /* Behind the far end: the corner at line_p2. */
        sceVu0SubVector(tmp1, pos, line_p2);                            /* 332 */
        sceVu0SubVector(tmp2, line_p1, line_p2);                        /* 333 */

        if (g3dxVu0InnerProductXZ(tmp1, tmp2) < 0.0f)
        {
            tmp_len = g3dxVu0InnerProductXZ(tmp1, tmp1);
            if (p_flg == 0)                                             /* 336 */
            {
                p_len = tmp_len;
                sceVu0CopyVector(p_vec, line_p2);                       /* 338 */
                p_flg = 1;                                              /* 339 */
            }
            else if (tmp_len < p_len)                                   /* 341 */
            {
                p_len = tmp_len;
                sceVu0CopyVector(p_vec, line_p2);                       /* 343 */
            }
            continue;
        }

        /* Between the ends: the perpendicular to the edge is the distance. */
        tmp_len = HcBasePointLineXZ(pos, line_p1, tmp2);                /* 348 */
        if (l_flg == 0)                                                 /* 349 */
        {
            l_len = tmp_len;
            sceVu0CopyVector(l_vec[0], line_p1);                        /* 351 */
            sceVu0CopyVector(l_vec[1], line_p2);                        /* 352 */
            l_flg = 1;                                                  /* 353 */
        }
        else if (tmp_len < l_len)                                       /* 355 */
        {
            l_len = tmp_len;
            sceVu0CopyVector(l_vec[0], line_p1);                        /* 357 */
            sceVu0CopyVector(l_vec[1], line_p2);                        /* 358 */
        }
    }                                                                   /* 362 */

    /* ROM: the second of the two identical sqrt inlines, g3dxVu0.h 2252
     * (HcBasePointLineXZ uses the 2247 one). */
    tmp_len = g3dxVu0Sqrt(p_len);

    if (p_flg == 0)                                                     /* 366 */
    {
        /* Every edge lands in one of the three arms, so p_flg == 0 implies
         * l_flg != 0 and l_len is real. */
        if (l_len < r)                                                  /* 367 */
        {
            *len = l_len;                                               /* 371 */
            sceVu0CopyVector(a, l_vec[0]);
            sceVu0CopyVector(b, l_vec[1]);
            return 2;
        }
        return 0;
    }

    if (l_flg != 0)                                                     /* 374 */
    {
        if (l_len < tmp_len && l_len < r)                               /* 382 */
        {
            *len = l_len;                                               /* 383 */
            sceVu0CopyVector(a, l_vec[0]);                              /* 384 */
            sceVu0CopyVector(b, l_vec[1]);                              /* 385 */
            return 2;                                                   /* 386 */
        }
    }

    if (tmp_len < r)                                                    /* 388 */
    {
        *len = tmp_len;                                                 /* 389 */
        sceVu0CopyVector(a, p_vec);                                     /* 390 */
        return 1;                                                       /* 391 */
    }

    return 0;                                                           /* 394 */
}                                                                       /* 395 */

/* Cheap AABB reject in XZ before HcBasePointRectangle()'s per-edge work.
 * Defined after its caller in the ROM too -- the object emits it last.
 * The loop deliberately starts at vec[0], re-testing the corner the bounds
 * were seeded from; the acceptance test is strict (a point exactly r away
 * on the box is rejected). */
static int HcBaseIsNearRectangle(float (*vec)[4], float *pos, float r)
{                                                                       /* 404 */
    float max_x, min_x, max_z, min_z;
    int i;

    max_x = vec[0][0];                                                  /* 408 */
    min_x = vec[0][0];                                                  /* 409 */
    max_z = vec[0][2];                                                  /* 410 */
    min_z = vec[0][2];                                                  /* 411 */

    for (i = 0; i < 4; i++)                                             /* 413 */
    {
        if (max_x < vec[i][0])                                          /* 414 */
        {
            max_x = vec[i][0];                                          /* 415 */
        }
        else if (vec[i][0] < min_x)                                     /* 417 */
        {
            min_x = vec[i][0];
        }

        if (max_z < vec[i][2])                                          /* 420 */
        {
            max_z = vec[i][2];                                          /* 421 */
        }
        else if (vec[i][2] < min_z)                                     /* 423 */
        {
            min_z = vec[i][2];
        }
    }                                                                   /* 426 */

    max_x += r;                                                         /* 428 */
    max_z += r;                                                         /* 429 */
    min_x -= r;                                                         /* 430 */
    min_z -= r;                                                         /* 431 */

    if (min_x < pos[0] && pos[0] < max_x && min_z < pos[2] && pos[2] < max_z)   /* 433 */
    {
        return 1;
    }
    return 0;                                                           /* 437 */
}                                                                       /* 439 */
