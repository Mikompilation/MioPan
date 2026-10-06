/* ==========================================================================
 *  common/utility.h
 *
 *  Shared vector/math helpers.
 *
 *  Reconstructed from the Feb 6 2004 prototype (SLES_523.84).
 * ======================================================================== */

#ifndef _COMMON_UTILITY_H
#define _COMMON_UTILITY_H

#ifdef __cplusplus
extern "C" {
#endif

/* GetTrgtRot() is declared in ingame/plyr/unit_ctl.h -- it is a unit_ctl.o
 * symbol, not a utility.o one. */

void RotFvector(float *rot, float *tv);
float CombRotate(float rot);

/* Horizontal (XZ) distance between two points -- Y is deliberately ignored. */
float GetDistV(const float *p0, const float *p1);

/* Full 3D distance.  Unlike GetDistV(), Y is included -- the ROM gets there
 * by taking the XZ distance first and then folding Y in against it. */
float GetDistV2(const float *p0, const float *p1);

/* Length of the 2D vector (x, z). */
float GetDist(float x, float z);

/* Bit width, rounded up: the smallest k with 2^k >= n, which is the form a
 * GS TEX0's TW/TH fields want. */
u_int log_2(u_int n);

/* Decimal digit count, not a logarithm: log_10(0) is 1 and log_10(9999) is 4.
 * The two recurse into each other; log_10sub() is exported too. */
u_int log_10(u_int num);
float log_10sub(float num);

/* Component-wise vector setters.  The ROM emits these out of line (they are
 * real functions in utility.o, not macros). */
void _SetVector(float *v, float x, float y, float z, float w);
void _ClearVector(float *v0);

/* Normalises vec into unit and returns its original length.  Callers use the
 * length to decide whether the direction is meaningful at all. */
float GetLenUnitFromVec(float *unit, float *vec);

/* Uniform random in [0, max).  GetRandValI() is the integer truncation of
 * GetRandValF(); player.c drives the blink timer off it. */
float GetRandValF(float max);
int   GetRandValI(int max);

/* --------------------------------------------------------------------------
 *  2D segment geometry.  These work in the XZ plane -- POINT_T::y is really
 *  the world Z, which is how sister.c fills them when it tests whether a
 *  trace route crosses itself.
 * ------------------------------------------------------------------------ */
typedef struct                      /* 0x8 */
{
    /* 0x0 */ float x;
    /* 0x4 */ float y;
} POINT_T;

typedef struct                      /* 0x10 */
{
    /* 0x0 */ POINT_T a;
    /* 0x8 */ POINT_T b;
} LINE_T;

/* Non-zero when v1 and v2 have the same x, y and z (w is not compared). */
int CompVector(float *v1, float *v2);

/* Which side of e the point p falls on: +1 / -1, or 0 when exactly on it. */
int LineSide(POINT_T *p, LINE_T *e);

/* Non-zero when a and b lie on opposite sides of e (or either is on it). */
int LineStraddle(LINE_T *e, POINT_T *a, POINT_T *b);

/* Non-zero when the two segments cross: bounding boxes overlap on both axes
 * and each segment straddles the other. */
int LineIntersect(LINE_T *e1, LINE_T *e2);

#ifdef __cplusplus
}
#endif

#endif /* _COMMON_UTILITY_H */
