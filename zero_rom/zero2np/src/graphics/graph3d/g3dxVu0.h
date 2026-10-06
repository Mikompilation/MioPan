/* ==========================================================================
 *  g3dxVu0.h
 *
 *  Inline PS2 VU0 (macro-mode) math helpers for the zero2np 3D engine.
 *
 *  Only the pieces the reconstructed graph3d sources actually inline are
 *  defined here.  The matrix/vector primitives (sceVu0*) are provided by the
 *  SCE EE VU0 library; this header adds the small game-side inline helpers
 *  the decompiler attributed to "g3dxVu0.h" (most notably EqualMemory128,
 *  used by sgdVerifyLightData) plus the thin g3dxVu0* wrappers used by the
 *  shadow renderer.
 *
 *  Reconstructed from the Feb 6 2004 prototype (SLES_523.84).
 * ======================================================================== */

#ifndef _G3DXVU0_H
#define _G3DXVU0_H

#include <math.h>               /* sqrtf */
#include <string.h>             /* memcmp */
#include <libvu0.h>             /* sceVu0FVECTOR, sceVu0IVECTOR, sceVu0*Matrix */

/* --------------------------------------------------------------------------
 *  EqualMemory128
 *
 *  Compare n quadwords (16 bytes each).  Inlined into sgdVerifyLightData to
 *  test whether a point light's direction is still the canonical zero vector
 *  (EqualMemory128(pLight->vDirection, g_v0000, 1)).  Returns non-zero when
 *  the two regions are identical.
 * ------------------------------------------------------------------------ */
static inline int EqualMemory128(const void *p0, const void *p1, int nQuadwords)
{
    return memcmp(p0, p1, (size_t)nQuadwords * 16) == 0;
}

/* --------------------------------------------------------------------------
 *  Scalar helpers used by the shadow projection math.
 * ------------------------------------------------------------------------ */

/* VU0 square root of a single value.  Not a library call in the original --
 * it is inlined COP2, which is why it shows up in the middle of its callers:
 *
 *     qmtc2  x -> vf         move the scalar into a vector field
 *     vsqrt  Q, vf           Q = sqrt(|x|)
 *     vwaitq                 stall until Q is ready
 *     vaddq  vf, vf0, Q      vf0 is (0,0,0,1), so vf.x = 0 + Q = Q
 *     qmfc2  vf -> x         and back out to a GPR
 *
 * The vaddq is only there to land Q in a readable field; the whole sequence
 * is an ordinary square root -- of the ABSOLUTE value: VU0's SQRT takes |x|
 * (raising only a status flag for a negative input) where the host's sqrtf
 * returns NaN.  The fabsf matters in practice: HcBasePointLineXZ() computes
 * a squared distance as d2 - dv*dv/vv, which cancellation rounds to a tiny
 * negative when the point sits exactly on the line, and a NaN distance would
 * make a touching wall unresolvable.  HcBasePointRectangle() likewise takes
 * the root of its -1.0 no-corner sentinel before testing the flag. */
static inline float g3dxVu0Sqrt(float x)
{
    return sqrtf(fabsf(x));
}

/* sqrt of the sum of two squares -- used for 2D magnitudes. */
static inline float g3dxVu0Sqrt2(float a, float b)
{
    return g3dxVu0Sqrt(a * a + b * b);
}

static inline float g3dxVu0Sqrt2(float a)
{
    return g3dxVu0Sqrt(a);
}

/* Length of a 3-component vector (w ignored). */
static inline float g3dxVu0CalcLength(const float *v)
{
    return g3dxVu0Sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
}

/* Distance between two 3-component points (|a - b|). */
static inline float g3dxVu0Length3(const float *a, const float *b)
{
    float dx = a[0] - b[0];
    float dy = a[1] - b[1];
    float dz = a[2] - b[2];
    return g3dxVu0Sqrt(dx * dx + dy * dy + dz * dz);
}

/* Dot product of the xyz parts of two vectors. */
static inline float g3dxVu0InnerProduct(const float *a, const float *b)
{
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

/* Dot product on the floor plane only: a.x*b.x + a.z*b.z.
 *
 *     vmul.xz  vf12, vf12, vf13     multiply the x and z lanes only
 *     vaddz.x  vf12, vf12, vf12     x += z
 *
 * ROM: g3dxVu0.h 433-436.  hit_check_base.o leans on this everywhere: the
 * map's hit rectangles are flattened to y == 0 by RegDatAddOffset() while
 * positions carry the real floor height, so a 3D dot would put every wall
 * outside any character's hit radius and nothing would ever collide. */
static inline float g3dxVu0InnerProductXZ(const float *a, const float *b)
{
    return a[0] * b[0] + a[2] * b[2];
}

/* Plane x homogeneous point: a.x*b.x + a.y*b.y + a.z*b.z + a.w.
 *
 * The first operand's w rides through the masked multiply untouched and is
 * folded in by the final broadcast (vmul.xyz / vadday.x / vmaddaz.x /
 * vmaddw.x), so the second operand's w never matters -- the point is treated
 * as w == 1.  HcBaseIsLineHitFace() evaluates its face plane, whose w is the
 * plane constant, against a segment end with this.  ROM: g3dxVu0.h 2095. */
static inline float g3dxVu0PlaneDot(const float *plane, const float *p)
{
    return plane[0] * p[0] + plane[1] * p[1] + plane[2] * p[2] + plane[3];
}

/* True when |v| is within an epsilon of 1.0 (unit-length check). */
static inline int g3dxVu0VectorIsNormalized(const float *v)
{
    float f;

    f = g3dxVu0CalcLength(v) - 1.0f;
    if (f < 0.0f)
    {
        f = -f;
    }
    return f < 0.001f;
}

static inline float sceVu0DiffusePower(const float *v)
{
    return v[0] + v[1] + v[2];
}

static inline float sceVu0DistanceToBB(const float *v, const float *vBBCenter)
{
    float d = g3dxVu0Length3(v, vBBCenter);
    return (d > 0.0f) ? d : 1.0f;
}

static inline void g3dxVu0LoadMatrix(float (*mat)[4])
{
    (void)mat;
}

static inline void g3dxVu0ScaleMatrixColumns(float (*matDest)[4], const float (*matSrc)[4],
                                             const float *vScale)
{
    float tmp[4][4];
    int r, c;

    for (r = 0; r < 4; r++)
        for (c = 0; c < 4; c++)
            tmp[r][c] = matSrc[r][c] * vScale[c];

    sceVu0CopyMatrix(matDest, tmp);
}

static inline void g3dxVu0TranslateMatrix(float (*matDest)[4], float (*matSrc)[4],
                                          const float *vTrans)
{
    float tmp[4][4];

    sceVu0CopyMatrix(tmp, matSrc);
    tmp[3][0] += vTrans[0];
    tmp[3][1] += vTrans[1];
    tmp[3][2] += vTrans[2];
    tmp[3][3] += vTrans[3];
    sceVu0CopyMatrix(matDest, tmp);
}

/* Quadword copy (lq / sq through a GPR).  ROM: g3dxVu0.h 134-135. */
static inline void g3dxVu0CopyVector(float* dst, const float* src)
{
    dst[0] = src[0];
    dst[1] = src[1];
    dst[2] = src[2];
    dst[3] = src[3];
}

/* Store the VU0 hardware constant vf0 -- a bare `sqc2 vf0, 0x0(reg)` with no
 * load in front of it.  vf0 is (0,0,0,1), NOT (0,0,0,0), so this is not
 * sceVu0CopyVector(v, g_v0000): w comes out as 1.
 *
 * The ROM's expansions carry the *caller's* file and line (g3dCore.c 240-243 in
 * _ApplyLightDirectional, 276-279 in _ApplyLightPoint), with no N_SOL switch
 * into this header, so the original was a macro rather than a header inline.
 * Its name is not recoverable; a static inline is used here because the
 * distinction has no effect on the port.  Compare gra3dVu0ClearMatrix(), which
 * is the same idiom applied to the three rows of an LMATRIX. */
static inline void g3dxVu0ClearVector(float *v0)
{
    v0[0] = 0.0f;
    v0[1] = 0.0f;
    v0[2] = 0.0f;
    v0[3] = 1.0f;
}

/* Full four-lane multiply: v0 = v1 * v2 (lqc2 / vmul.xyzw / sqc2), w included.
 *
 * The same arithmetic as the out-of-line sceVu0MulVector(), but the ROM inlines
 * this form at some call sites: MhHitLineCheck() scales both segment endpoints
 * by g_vConvertPS2SI with inline COP2, while the rest of map_height.c calls the
 * library routine.  Kept separate so those call sites stay distinguishable. */
static inline void g3dxVu0MulVector(float *v0, const float *v1, const float *v2)
{
    v0[0] = v1[0] * v2[0];
    v0[1] = v1[1] * v2[1];
    v0[2] = v1[2] * v2[2];
    v0[3] = v1[3] * v2[3];
}

/* xyz-masked multiply: v0.xyz = v1.xyz * v2.xyz (lqc2 / vmul.xyz / sqc2), w
 * untouched.  ROM: g3dxVu0.h 229-230.  _CalcAmbient() folds the global ambient
 * into the material ambient with it, and the w mask is what leaves
 * Calc.vAmbient.w alone -- the same reason g3dxVu0AddVectorXYZ() exists beside
 * the full-width g3dxVu0AddVector(). */
static inline void g3dxVu0MulVectorXYZ(float *v0, const float *v1, const float *v2)
{
    v0[0] = v1[0] * v2[0];
    v0[1] = v1[1] * v2[1];
    v0[2] = v1[2] * v2[2];
}

/* xyz-masked broadcast multiply: v0.xyz = v1.xyz * s.
 *
 *     qmtc2 s -> vf13            the scalar lands in every lane
 *     vmulx.xyz vf12, vf12, vf13 broadcast .x, write only xyz
 *
 * The field mask is why w survives: MapAnimGetRstMix() scales a RST_DATA
 * rotation and translation this way and leaves their w alone. */
static inline void g3dxVu0MulScaleVectorXYZ(float *v0, const float *v1, float s)
{
    v0[0] = v1[0] * s;
    v0[1] = v1[1] * s;
    v0[2] = v1[2] * s;
}

/* xyz-masked add: v0.xyz = v1.xyz + v2.xyz (vadd.xyz), w untouched.
 * ROM: g3dxVu0.h 172-173.  _CalcAmbient() and g3dCalcVertexColor()'s final
 * global-ambient fold both expand it; the w mask is what leaves the vertex
 * alpha alone there. */
static inline void g3dxVu0AddVectorXYZ(float *v0, const float *v1, const float *v2)
{
    v0[0] = v1[0] + v2[0];
    v0[1] = v1[1] + v2[1];
    v0[2] = v1[2] + v2[2];
}

/* Full four-lane add: v0 = v1 + v2 (lqc2 / vadd.xyzw / sqc2), w included.
 * The inline twin of the out-of-line sceVu0AddVector(), the same way
 * g3dxVu0MulVector() twins sceVu0MulVector().  ROM: g3dxVu0.h 160-161. */
static inline void g3dxVu0AddVector(float *v0, const float *v1, const float *v2)
{
    v0[0] = v1[0] + v2[0];
    v0[1] = v1[1] + v2[1];
    v0[2] = v1[2] + v2[2];
    v0[3] = v1[3] + v2[3];
}

/* Full four-lane subtract: v0 = v1 - v2 (lqc2 / vsub.xyzw / sqc2), w included.
 * The inline twin of sceVu0SubVector(); hit_check_base.o uses both forms and
 * each call site there preserves which one the ROM emitted.
 * ROM: g3dxVu0.h 184-185. */
static inline void g3dxVu0SubVector(float *v0, const float *v1, const float *v2)
{
    v0[0] = v1[0] - v2[0];
    v0[1] = v1[1] - v2[1];
    v0[2] = v1[2] - v2[2];
    v0[3] = v1[3] - v2[3];
}

/* Four-lane clamp: v0 = min(max(v1, fMin), fMax), w included.
 *
 *     qmtc2 fMin -> vf4 / qmtc2 fMax -> vf5   the two bounds, one lane each
 *     vmaxx.xyzw    vf6, vf6, vf4             broadcast .x, low bound
 *     vminibcx.xyzw vf6, vf6, vf5             broadcast .x, high bound
 *
 * The inline twin of sceVu0ClampVector(); g3dCalcVertexColor() ends on this
 * form rather than the library call.  The bounds keep the ROM's own parameter
 * names -- they survive as hoisted RSYMs in the caller's stab list, which is
 * what identifies the helper.  Applied in the ROM's order (low bound first),
 * so a caller passing fMin > fMax gets fMax, as the hardware does.
 * ROM: g3dxVu0.h 736-737. */
static inline void g3dxVu0ClampVector(float *v0, const float *v1, float fMin, float fMax)
{
    int c;

    for (c = 0; c < 4; c++)
    {
        float f = v1[c];

        if (f < fMin) f = fMin;
        if (f > fMax) f = fMax;
        v0[c] = f;
    }
}

/* Weighted blend, xyz only: v0 = v0 * (1 - rate) + v1 * rate.
 *
 * Destructive on *both* arguments: the ROM scales v1 in place before folding
 * it in rather than using a temporary.  Its one caller (MapAnimGetRstMix)
 * passes a scratch RST_DATA as v1, so nothing notices -- but it is not
 * interchangeable with sceVu0InterVector(), which is non-destructive and also
 * blends w. */
static inline void g3dxVu0MixVectorXYZ(float *v0, float *v1, float rate)
{
    g3dxVu0MulScaleVectorXYZ(v0, v0, 1.0f - rate);
    g3dxVu0MulScaleVectorXYZ(v1, v1, rate);
    g3dxVu0AddVectorXYZ(v0, v0, v1);
}

/* Normalise in place-style: v0 = v1 / |v1.xyz|.
 *
 *     vmul/vaddabc/vmaddbc   x²+y²+z²
 *     vrsqrt                 Q = 1/|v1|
 *     vmulq  vf, Q           no field mask -- all four lanes scaled
 *
 * Distinct from sceVu0Normalize(), which forces w to 0.  Here w is scaled
 * along with xyz, so a direction row with w == 0 is unaffected either way. */
static inline void g3dxVu0Normalize(float *v0, const float *v1)
{
    float inv = 1.0f / g3dxVu0Sqrt(v1[0] * v1[0] + v1[1] * v1[1] + v1[2] * v1[2]);

    v0[0] = v1[0] * inv;
    v0[1] = v1[1] * inv;
    v0[2] = v1[2] * inv;
    v0[3] = v1[3] * inv;
}

/* Direction from v2 to v1, normalised: v0 = (v1 - v2) / |v1 - v2|.
 *
 * The length is taken over xyz only (vaddabc/vmaddbc), but the reciprocal is
 * applied to all four lanes (vmulq with no field mask), so w ends up as
 * (v1[3] - v2[3]) / |d| rather than being forced to 0 or 1.  For the usual
 * point-minus-point case both w's are 1 and it falls out as 0 anyway.
 *
 * No zero-length guard, as in the original -- callers that care check with
 * g3dxVu0CalcLength() afterwards (see g3dEmulateDirectionalLight). */
static inline void g3dxVu0NormalizeVector(float *v0, const float *v1, const float *v2)
{
    float d[4];
    float inv;

    d[0] = v1[0] - v2[0];
    d[1] = v1[1] - v2[1];
    d[2] = v1[2] - v2[2];
    d[3] = v1[3] - v2[3];

    inv = 1.0f / g3dxVu0Sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);

    v0[0] = d[0] * inv;
    v0[1] = d[1] * inv;
    v0[2] = d[2] * inv;
    v0[3] = d[3] * inv;
}

/* Rotation of `angle` about the unit axis `axis`, as a full 4x4.
 *
 * The original does not use Rodrigues here: it builds the quaternion
 *
 *     q = ( axis * sin(angle/2), cos(angle/2) )
 *
 *         vmulbc  vf, Q      axis scaled by the broadcast sin
 *
 * and expands it with the two-matrix trick the VU0 code uses throughout --
 * eight quadwords laid out as
 *
 *     L = |  w  z -y  x |      R = |  w  z -y -x |
 *         | -z  w  x  y |          | -z  w  x -y |
 *         |  y -x  w  z |          |  y -x  w -z |
 *         | -x -y -z  w |          |  x  y  z  w |
 *
 * then vmulabc/vmaddabc/vmaddbc to form R * L, which collapses to the ordinary
 * quaternion-to-matrix form written out below.  There is no VU0 to feed here,
 * so the collapsed form is used directly; it is exact, not an approximation.
 *
 * ROM: inlined into motInversKinematics from g3dxVu0.h:2128-2152, with the
 * matrix build itself a second inline at g3dxVu0.h:1312-1324.  Neither inline
 * left a symbol, so these names are ours.  Note this is the same rotation
 * motInterpMatrix builds longhand via Rodrigues -- the two agree term for term,
 * the original simply wrote it two different ways. */
static inline void g3dxVu0RotMatrixAxis(float (*m)[4], const float *axis, float angle)
{
    float s = sinf(angle * 0.5f);
    float x = axis[0] * s;
    float y = axis[1] * s;
    float z = axis[2] * s;
    float w = cosf(angle * 0.5f);

    m[0][0] = 1.0f - 2.0f * (y * y + z * z);
    m[0][1] = 2.0f * (x * y + w * z);
    m[0][2] = 2.0f * (x * z - w * y);
    m[0][3] = 0.0f;

    m[1][0] = 2.0f * (x * y - w * z);
    m[1][1] = 1.0f - 2.0f * (x * x + z * z);
    m[1][2] = 2.0f * (y * z + w * x);
    m[1][3] = 0.0f;

    m[2][0] = 2.0f * (x * z + w * y);
    m[2][1] = 2.0f * (y * z - w * x);
    m[2][2] = 1.0f - 2.0f * (x * x + y * y);
    m[2][3] = 0.0f;

    m[3][0] = 0.0f;
    m[3][1] = 0.0f;
    m[3][2] = 0.0f;
    m[3][3] = 1.0f;
}

#endif /* _G3DXVU0_H */
