// FILE: /home/zero_rom/zero2np/src/common/utility.c
//
// PARTIAL.  Most of the shared vector/math helpers are not reconstructed yet;
// GetDistV() is, because the event open-condition tests depend on it.
//
// Reconstructed from the Feb 6 2004 prototype (SLES_523.84).

#include "utility.h"

#include <libvu0.h>
#include <math.h>
#include <stdlib.h>

/* GetTrgtRot() used to be stubbed here; ZERO2.MAP puts it in unit_ctl.o, so
 * the reconstruction lives in ingame/plyr/unit_ctl.c. */

/* Rotates tv in place by the Euler triple rot[], applied Z then X then Y.
 *
 * Each axis is built against the *identity* (work), not against the previous
 * result, so rot_matrix is always a pure single-axis rotation and the three
 * are composed by re-applying them to the vector rather than by concatenating
 * the matrices.  work is therefore only ever read, never written after
 * sceVu0UnitMatrix().
 *
 * The zero tests are the ROM's, not an optimisation: each axis is skipped
 * outright when its angle is exactly 0.0f, which is the common case for the
 * camera callers that only ever yaw. */
void RotFvector(float *rot, float *tv)                                  /* 142 */
{
    sceVu0FMATRIX work;
    sceVu0FMATRIX rot_matrix;

    sceVu0UnitMatrix(work);                                             /* 145 */

    if (rot[2] != 0.0f)                                                 /* 146 */
    {
        sceVu0RotMatrixZ(rot_matrix, work, rot[2]);                     /* 147 */
        sceVu0ApplyMatrix(tv, rot_matrix, tv);                          /* 148 */
    }
    if (rot[0] != 0.0f)                                                 /* 149 */
    {
        sceVu0RotMatrixX(rot_matrix, work, rot[0]);                     /* 150 */
        sceVu0ApplyMatrix(tv, rot_matrix, tv);                          /* 151 */
    }
    if (rot[1] != 0.0f)                                                 /* 152 */
    {
        sceVu0RotMatrixY(rot_matrix, work, rot[1]);                     /* 153 */
        sceVu0ApplyMatrix(tv, rot_matrix, tv);                          /* 154 */
    }
}

/* Fold an angle into the same [-pi, pi] interval used by the EE helper. */
float CombRotate(float rot)                                             /* 162 */
{
    const float pi = 3.1415927f;
    const float two_pi = 6.2831855f;

    while (rot > pi)
    {
        rot -= two_pi;
    }
    while (rot < -pi)
    {
        rot += two_pi;
    }
    return rot;
}

/* The ROM builds a vector of |dx|, 0, |dz|, 0 and squares it through the VU0
 * helper GetSquare() before taking the root; with no VU on the host the two
 * lanes that matter are multiplied directly.  Y is intentionally dropped --
 * every caller is testing floor-plane proximity. */
float GetDistV(const float *p0, const float *p1)
{
    float dx;
    float dz;

    dx = fabsf(p0[0] - p1[0]);
    dz = fabsf(p0[2] - p1[2]);

    return sqrtf((dx * dx) + (dz * dz));
}

/* The ROM does this in two GetSquare()/root passes: the XZ distance first,
 * then that result against |dy|.  Algebraically the same as a plain 3D length,
 * but kept in two steps so the rounding matches. */
float GetDistV2(const float *p0, const float *p1)
{
    float dx = fabsf(p0[0] - p1[0]);
    float dz = fabsf(p0[2] - p1[2]);
    float dxz;
    float dy;

    dxz = sqrtf((dx * dx) + (dz * dz));
    dy  = fabsf(p0[1] - p1[1]);

    return sqrtf((dxz * dxz) + (dy * dy));
}

/* As GetDistV, but taking the two components directly.  The ROM squares them
 * through the VU0 helper GetSquare() before the root. */
float GetDist(float x, float z)
{
    float ax = fabsf(x);
    float az = fabsf(z);

    return sqrtf((ax * ax) + (az * az));
}

/* --------------------------------------------------------------------------
 *  log_10 / log_10sub -- how many decimal digits `num` has.
 *
 *  Not a logarithm despite the name: the pair recurses, dividing by ten and
 *  counting, so log_10(0) is 1 and log_10(9999) is 4.  finder.c's score
 *  readout uses it to decide how many digit sprites to place.
 *
 *  The float round trip is the ROM's, not an artifact -- log_10sub takes and
 *  returns a float and log_10 converts at both ends.  The unsigned-to-float
 *  shuffling the decompiler shows around those conversions is GCC's (the EE
 *  has no direct u32 conversion), not something the source asked for.
 * ------------------------------------------------------------------------ */
/* --------------------------------------------------------------------------
 *  log_2 (343..349)
 *
 *  Bit width, rounded up: the smallest k with 2^k >= n.  DrawPhotoBuffer()
 *  uses it to fill a TEX0's TW/TH, which are log2 texel counts.
 *
 *  The scan starts at bit 31 and walks down to the highest set bit, so
 *  log_2(0) is 0 and any n with the sign bit set answers 31 (+1 unless it is
 *  exactly 0x80000000, and the signed compare makes that unreachable).
 * ------------------------------------------------------------------------ */
u_int log_2(u_int n)
{                                                                       /* 343 */
    int i;

    for (i = 31; i > 0 && (((int)n >> i) & 1) == 0; i--)                /* 344 */
    {                                                                   /* 347 */
    }

    return (u_int)(i + ((1 << i) < (int)n));                            /* 348 */
}                                                                       /* 349 */

float log_10sub(float num)
{                                                                       /* 352 */
    float ret = 0.0f;                                                   /* 353 */

    if (1.0f <= num / 10.0f)                                            /* 354 */
    {
        ret = (float)log_10((u_int)(num / 10.0f));
    }

    return ret + 1.0f;
}

u_int log_10(u_int num)
{                                                                       /* 361 */
    return (u_int)log_10sub((float)num);                                /* 362 */
}

/* The ROM stores w first, then x/y/z -- it is filling a quadword through the
 * VU0 store path.  Order is immaterial on the host. */
void _SetVector(float *v, float x, float y, float z, float w)
{
    v[3] = w;
    v[0] = x;
    v[1] = y;
    v[2] = z;
}

void _ClearVector(float *v0)
{
    v0[0] = 0.0f;
    v0[1] = 0.0f;
    v0[2] = 0.0f;
    v0[3] = 0.0f;
}

/* The ROM does this entirely in VU0 macro mode: vmul/vaddbc/vaddbc for the
 * xyz dot product, vsqrt+vwaitq for the length, then vdiv/vmulq to scale.  On
 * the host the same thing falls out of plain float maths.  A zero-length
 * vector divides by zero on the VU too, so callers are expected to test the
 * returned length rather than relying on unit being finite. */
float GetLenUnitFromVec(float *unit, float *vec)
{
    float len = sqrtf((vec[0] * vec[0]) + (vec[1] * vec[1]) + (vec[2] * vec[2]));
    float inv = 1.0f / len;

    unit[0] = vec[0] * inv;
    unit[1] = vec[1] * inv;
    unit[2] = vec[2] * inv;
    unit[3] = vec[3] * inv;

    return len;
}

/* The ROM divides by lit4 3ee9e8 == 2147483520.0f, GCC 2.96-ee's truncation of
 * (float)RAND_MAX for the EE's RAND_MAX of 0x7fffffff.  MioPan_Rand() gives
 * back that 31-bit range on the host, so MIOPAN_RAND_MAXF is the ROM's own
 * divisor rather than a substitute for it. */
float GetRandValF(float max)                                            /* 498 */
{
    return (max * (float)MioPan_Rand()) / MIOPAN_RAND_MAXF;             /* 499 */
}

int GetRandValI(int max)                                                /* 502 */
{
    return (int)GetRandValF((float)max);                                /* 503 */
}

/* --------------------------------------------------------------------------
 *  2D segment geometry (utility.o 0x0026bf50..0x0026c1f0).
 *
 *  sister.c is the heavy user: SisterTraceMove() and DrawSisDummy() run
 *  LineIntersect() over the trace-point ring to find where the recorded route
 *  doubles back on itself, and SisterNoMove() uses LineSide() to decide which
 *  way to slide the player around a companion who is standing still.
 * ------------------------------------------------------------------------ */

/* w is deliberately not compared -- the callers pass MOVE_BOX positions whose
 * w is a homogeneous 1.0 that carries no information. */
int CompVector(float *v1, float *v2)                                    /* 176 */
{
    return *v1 == *v2 && v1[1] == v2[1] && v1[2] == v2[2];
}

int LineSide(POINT_T *p, LINE_T *e)                                     /* 241 */
{
    POINT_T p1 = *p;
    POINT_T p2 = e->a;
    POINT_T p3 = e->b;

    /* Twice the signed area of the triangle (p, e.a, e.b). */
    float d = p1.x * (p2.y - p3.y) +
              p2.x * (p3.y - p1.y) +
              p3.x * (p1.y - p2.y);                                     /* 249 */

    if (d > 0.0f)
    {
        return 1;
    }
    if (d < 0.0f)
    {
        return -1;
    }
    return 0;                                                           /* 251 */
}

/* "Straddle" is inclusive: a product of 0 -- one endpoint exactly on the line
 * -- counts as straddling, which is what makes LineIntersect() report
 * touching segments as crossing. */
int LineStraddle(LINE_T *e, POINT_T *a, POINT_T *b)
{
    return LineSide(a, e) * LineSide(b, e) < 1;
}

int LineIntersect(LINE_T *e1, LINE_T *e2)                               /* 213 */
{
    float e1x_min = e1->a.x < e1->b.x ? e1->a.x : e1->b.x;
    float e1x_max = e1->a.x < e1->b.x ? e1->b.x : e1->a.x;
    float e1y_min = e1->a.y < e1->b.y ? e1->a.y : e1->b.y;
    float e1y_max = e1->a.y < e1->b.y ? e1->b.y : e1->a.y;

    float e2x_min = e2->a.x < e2->b.x ? e2->a.x : e2->b.x;
    float e2x_max = e2->a.x < e2->b.x ? e2->b.x : e2->a.x;
    float e2y_min = e2->a.y < e2->b.y ? e2->a.y : e2->b.y;
    float e2y_max = e2->a.y < e2->b.y ? e2->b.y : e2->a.y;

    /* Cheap separating-axis reject on both axes before the two straddle
     * tests, which is the expensive part. */
    if (e1x_min > e2x_max || e2x_min > e1x_max ||
        e1y_min > e2y_max || e2y_min > e1y_max)
    {
        return 0;
    }

    return LineStraddle(e1, &e2->a, &e2->b) != 0 &&
           LineStraddle(e2, &e1->a, &e1->b) != 0;                       /* 220 */
}
