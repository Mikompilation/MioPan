/* ==========================================================================
 *  g3dGeom.c
 *
 *  Geometry helpers for the zero2np 3D engine: angle measurement, the matrix
 *  "set position / direction / target / roll" builders that orient a transform
 *  from a forward vector, axis-angle rotation, rigid-transform inverse, plane
 *  extraction, and ray/sphere & ray/ellipsoid intersection.
 *
 *  As in the other reconstructed graph3d sources, the inlined PS2 VU0
 *  (macro-mode) blocks the compiler emitted from g3dxVu0.h are written here as
 *  the corresponding SCE EE VU0 library / g3dxVu0 calls where the operation is
 *  a standard vector/matrix primitive (normalise, cross product, dot, matrix
 *  multiply, matrix inverse).
 *
 *  Shift-JIS assert messages are documented with the decoded text in a comment
 *  above the assert; here they are all the empty "" format.
 *
 *  Reconstructed from the Feb 6 2004 prototype (SLES_523.84).
 * ======================================================================== */

#include "g3dGeom.h"
#include "g3dMath.h"
#include "g3dxVu0.h"            /* VU0 macro-mode vector/matrix helpers */
#include "g3ddbg.h"
#include "gra3dConst.h"        /* g_v0001 / g_matUnit */
#include <libvu0.h>            /* sceVu0* */
#include <mathf.h>            /* sinf/cosf/acosf/atan2f/fmodf */

/* --------------------------------------------------------------------------
 *  g3dCalcAngle
 *
 *  Angle between vDirection and the (vOrigin -> vTarget) direction.  Both
 *  vectors are normalised, dotted, and the result is acos(-dot) so that a
 *  direction pointing straight at the target reads as 0.
 * ------------------------------------------------------------------------ */
float g3dCalcAngle(float *vDirection, float *vOrigin, float *vTarget)
{
    float vDir0[4];
    float vDir1[4];
    float fDot;

    /* vDir0 = normalize(vOrigin - vTarget) */
    sceVu0SubVector(vDir0, vOrigin, vTarget);
    sceVu0Normalize(vDir0, vDir0);

    /* vDir1 = normalize(vDirection) */
    sceVu0Normalize(vDir1, vDirection);

    fDot = sceVu0InnerProduct(vDir0, vDir1);

    return acosf(-fDot);
}

/* --------------------------------------------------------------------------
 *  g3dMatrixSetPosition
 *
 *  Copy vPos into the translation row (row 3) of mat.
 * ------------------------------------------------------------------------ */
void g3dMatrixSetPosition(float (*mat)[4], float *vPos)
{
    float *pv0;

    pv0 = mat[3];
    pv0[0] = vPos[0];
    pv0[1] = vPos[1];
    pv0[2] = vPos[2];
    pv0[3] = vPos[3];
}

/* --------------------------------------------------------------------------
 *  g3dMatrixSetDirection
 *
 *  Orient the rotation part of mat so its local +Z points along vDir.  The
 *  right (X) and up (Y) axes are rebuilt by Gram-Schmidt against an "up" hint:
 *  the world up (0,1,0) when bFixUp is set, otherwise the matrix's current
 *  Y row.  All three axes are renormalised.
 * ------------------------------------------------------------------------ */
void g3dMatrixSetDirection(float (*mat)[4], float *vDir, int bFixUp)
{
    float vFixUp[4];
    float *rvY;
    float *rvZ;

    rvY = mat[1];
    rvZ = mat[2];

    /* default up hint = world +Y */
    vFixUp[0] = 0.0f;
    vFixUp[1] = 1.0f;
    vFixUp[2] = 0.0f;
    vFixUp[3] = 1.0f;

    if (bFixUp == 0)
    {
        vFixUp[0] = rvY[0];
        vFixUp[1] = rvY[1];
        vFixUp[2] = rvY[2];
        vFixUp[3] = rvY[3];
    }

    /* Z = normalize(vDir) */
    sceVu0Normalize(rvZ, vDir);

    /* X = normalize(up x Z) */
    sceVu0OuterProduct(mat[0], vFixUp, rvZ);
    sceVu0Normalize(mat[0], mat[0]);

    /* Y = normalize(Z x X) */
    sceVu0OuterProduct(rvY, rvZ, mat[0]);
    sceVu0Normalize(rvY, rvY);
}

/* --------------------------------------------------------------------------
 *  g3dMatrixSetTarget
 *
 *  Aim mat at the world-space point vTarget: build the forward direction
 *  (vTarget - position) and defer to g3dMatrixSetDirection.
 * ------------------------------------------------------------------------ */
void g3dMatrixSetTarget(float (*mat)[4], float *vTarget, int bFixUp)
{
    float v[4];

    sceVu0SubVector(v, vTarget, mat[3]);
    g3dMatrixSetDirection(mat, v, bFixUp);
}

/* --------------------------------------------------------------------------
 *  g3dMatrixSetRoll
 *
 *  Roll mat about its local Z axis by fRoll radians.  The angle is negated and
 *  wrapped into [-PI,PI] before building the Z-rotation matrix, which then
 *  pre-multiplies mat.
 * ------------------------------------------------------------------------ */
void g3dMatrixSetRoll(float (*mat)[4], float fRoll)
{
    float matRot[4][4];
    float *pm1[4];
    float rz;
    float f;
    float fRange;
    float f1;
    float f2;

    f      = -fRoll;
    f2     = 3.141592502593994f;     /* PI  */
    fRange = 6.283185005187988f;     /* 2PI */
    f1     = f2;

    /* wrap into [-PI, PI] */
    if (f2 < f)
    {
        f = fmodf(f, fRange);
        if (f1 < f)
        {
            f = f - fRange;
        }
        else if (f < -f2)
        {
            f = f + fRange;
        }
    }
    rz = f;

    sceVu0RotMatrixZ(matRot, g_matUnit, rz);
    sceVu0MulMatrix(mat, mat, matRot);
}

/* --------------------------------------------------------------------------
 *  g3dMatrixGetRoll
 *
 *  Recover the roll angle previously applied to mat from its X row.
 * ------------------------------------------------------------------------ */
float g3dMatrixGetRoll(float (*mat)[4])
{
    float fLen;

    /* length of the projection of the X row onto the xz plane */
    fLen = g3dxVu0Sqrt2(mat[0][0], mat[0][2]);

    return -atan2f(mat[0][1], fLen);
}

/* --------------------------------------------------------------------------
 *  g3dMatrixRotationByAxis
 *
 *  Rotate mat about the (unit) axis vAxis by fAngle radians using the
 *  quaternion->matrix expansion, then re-orthonormalise via SetDirection.
 * ------------------------------------------------------------------------ */
void g3dMatrixRotationByAxis(float (*mat)[4], float *vAxis, float fAngle)
{
    float matRot[4][4];
    float qx;
    float qy;
    float qz;
    float qw;
    float fSin;

    fSin = sinf(fAngle * 0.5f);
    qx   = vAxis[0] * fSin;
    qy   = vAxis[1] * fSin;
    qz   = vAxis[2] * fSin;
    qw   = cosf(fAngle * 0.5f);

    /* quaternion (qx,qy,qz,qw) -> rotation matrix, built column-wise */
    matRot[0][0] = qw;
    matRot[0][1] = qz;
    matRot[0][2] = -qy;
    matRot[0][3] = qx;
    matRot[1][0] = -qz;
    matRot[1][1] = qw;
    matRot[1][2] = qx;
    matRot[1][3] = qy;
    matRot[2][0] = qy;
    matRot[2][1] = -qx;
    matRot[2][2] = qw;
    matRot[2][3] = qz;
    matRot[3][0] = -qx;
    matRot[3][1] = -qy;
    matRot[3][2] = -qz;
    matRot[3][3] = qw;

    sceVu0MulMatrix(matRot, matRot, matRot);
    sceVu0MulMatrix(mat, mat, matRot);

    g3dMatrixSetDirection(mat, mat[2], 1);
}

/* --------------------------------------------------------------------------
 *  g3dMatrixInverseTransform
 *
 *  Inverse of a scale+rotation+translation transform.  Each rotation row of
 *  matSrc is normalised (removing scale), the transpose gives the inverse
 *  rotation, and the inverse translation is -trans rotated by it.
 * ------------------------------------------------------------------------ */
void g3dMatrixInverseTransform(float (*matDest)[4], float (*matSrc)[4])
{
    float matWork[4][4];
    float vScale[4];
    float vTrans[4];

    /* transpose the rotation part into matWork */
    sceVu0TransposeMatrix(matWork, matSrc);
    g3dMatrixSetColumn(matWork, g_v0001, 3);

    /* Recover the per-axis scale.  The ROM applies this reciprocal twice:
     * first to normalise the transposed basis, then again to put the inverse
     * scale into the finished inverse matrix. */
    vScale[0] = 1.0f / g3dxVu0CalcLength(matWork[0]);
    vScale[1] = 1.0f / g3dxVu0CalcLength(matWork[1]);
    vScale[2] = 1.0f / g3dxVu0CalcLength(matWork[2]);

    sceVu0ScaleVector(matWork[0], matWork[0], vScale[0]);
    sceVu0ScaleVector(matWork[1], matWork[1], vScale[1]);
    sceVu0ScaleVector(matWork[2], matWork[2], vScale[2]);

    sceVu0ScaleVector(matWork[0], matWork[0], vScale[0]);
    sceVu0ScaleVector(matWork[1], matWork[1], vScale[1]);
    sceVu0ScaleVector(matWork[2], matWork[2], vScale[2]);

    /* The EE uses vsub.xyz here: W remains 1 so the following matrix apply
     * includes the inverse matrix's translation row. */
    vTrans[0] = g_v0001[0] - matSrc[3][0];
    vTrans[1] = g_v0001[1] - matSrc[3][1];
    vTrans[2] = g_v0001[2] - matSrc[3][2];
    vTrans[3] = g_v0001[3];
    sceVu0ApplyMatrix(matDest[3], matWork, vTrans);

    sceVu0CopyVector(matDest[0], matWork[0]);
    sceVu0CopyVector(matDest[1], matWork[1]);
    sceVu0CopyVector(matDest[2], matWork[2]);
}

/* --------------------------------------------------------------------------
 *  g3dMatrixSetColumn
 *
 *  Write the 4-vector v into column iCol of mat.
 * ------------------------------------------------------------------------ */
void g3dMatrixSetColumn(float (*mat)[4], float *v, int iCol)
{
    float *pv;

    G3DASSERT(iCol < 4, "iCol : %d", iCol);

    pv = mat[0] + iCol;
    pv[0]  = v[0];
    pv[4]  = v[1];
    pv[8]  = v[2];
    pv[12] = v[3];
}

/* --------------------------------------------------------------------------
 *  g3dMatrixSetColumnXYZ
 *
 *  Write the xyz of v into column iCol of mat, leaving w (row 3) untouched.
 * ------------------------------------------------------------------------ */
void g3dMatrixSetColumnXYZ(float (*mat)[4], float *v, int iCol)
{
    float *pv;

    G3DASSERT(iCol < 3, "iCol : %d", iCol);

    pv = mat[0] + iCol;
    pv[0] = v[0];
    pv[4] = v[1];
    pv[8] = v[2];
}

/* --------------------------------------------------------------------------
 *  _PlaneFromMatrix
 *
 *  Read column iCol of mat out as a 4-component plane (xyz normal + d).
 * ------------------------------------------------------------------------ */
static void _PlaneFromMatrix(float *plane, float (*mat)[4], int iCol)
{
    float *pv;

    G3DASSERT(iCol < 3, "iCol : %d", iCol);

    pv = mat[0] + iCol;
    plane[0] = pv[0];
    plane[1] = pv[4];
    plane[2] = pv[8];
    plane[3] = pv[12];
}

/* --------------------------------------------------------------------------
 *  g3dPlaneFromMatrixZ
 *
 *  Extract the Z column of mat as a plane.
 * ------------------------------------------------------------------------ */
void g3dPlaneFromMatrixZ(float *plane, float (*mat)[4])
{
    _PlaneFromMatrix(plane, mat, 2);
}

/* --------------------------------------------------------------------------
 *  g3dCalcPlaneFromPointNormal
 *
 *  Build the plane (n, -dot(n,p)) through vPoint with normal vNormal.
 * ------------------------------------------------------------------------ */
void g3dCalcPlaneFromPointNormal(float *plane, float *vPoint, float *vNormal)
{
    plane[0] = vNormal[0];
    plane[1] = vNormal[1];
    plane[2] = vNormal[2];
    plane[3] = vNormal[3];

    plane[3] = -sceVu0InnerProduct(vNormal, vPoint);
}

/* --------------------------------------------------------------------------
 *  g3dCalcPlaneFromPoints
 *
 *  Plane through three points: normal = normalize((P1-P0) x (P2-P0)).
 * ------------------------------------------------------------------------ */
void g3dCalcPlaneFromPoints(float *plane, float *vP0, float *vP1, float *vP2)
{
    float vNorm[4];
    float vCross[4];
    float vDir01[4];
    float vDir02[4];

    sceVu0SubVector(vDir01, vP1, vP0);
    sceVu0Normalize(vDir01, vDir01);

    sceVu0SubVector(vDir02, vP2, vP0);
    sceVu0Normalize(vDir02, vDir02);

    sceVu0OuterProduct(vCross, vDir01, vDir02);
    sceVu0Normalize(vNorm, vCross);

    g3dCalcPlaneFromPointNormal(plane, vP0, vNorm);
}

/* --------------------------------------------------------------------------
 *  g3dCalcIntersectionSphereAndLine
 *
 *  Intersect the ray (vStart, dir) with the sphere (vCenter, fRadius).  The
 *  ray direction is normalised and the quadratic a*t^2 + b*t + c = 0 solved;
 *  0, 1 or 2 hit points are written (sphere-local) to avRet and the count
 *  returned.
 * ------------------------------------------------------------------------ */
int g3dCalcIntersectionSphereAndLine(float (*avRet)[4], float *vCenter, float fRadius,
                                     float *vStart, float *vDir)
{
    float vStartTrans[4];
    float vDirNormalized[4];
    float fA;
    float fB;
    float fC;
    float fD;
    int   iRet;

    /* translate the ray start into sphere space, normalise the direction */
    sceVu0SubVector(vStartTrans, vStart, vCenter);
    sceVu0Normalize(vDirNormalized, vDir);

    fA = sceVu0InnerProduct(vDirNormalized, vDirNormalized);
    fB = sceVu0InnerProduct(vStartTrans, vDirNormalized) * 2.0f;
    fC = sceVu0InnerProduct(vStartTrans, vStartTrans);

    fD = fB * fB - fA * 4.0f * (fC - fRadius * fRadius);

    iRet = 0;
    if (0.0f <= fD)
    {
        if (fD == 0.0f)
        {
            sceVu0ScaleVector(avRet[0], vDirNormalized, -fB / (fA + fA));
            sceVu0AddVector(avRet[0], vStart, avRet[0]);
            iRet = 1;
        }
        else
        {
            float fSqrtD;

            fSqrtD = g3dxVu0Sqrt(fD);

            sceVu0ScaleVector(avRet[0], vDirNormalized, (-fB - fSqrtD) / (fA + fA));
            sceVu0AddVector(avRet[0], vStart, avRet[0]);

            sceVu0ScaleVector(avRet[1], vDirNormalized, (fSqrtD - fB) / (fA + fA));
            sceVu0AddVector(avRet[1], vStart, avRet[1]);

            /* sanity: every returned point must lie within the radius */
            G3DASSERT(avRet[0][0] <= fRadius, "");
            G3DASSERT(avRet[0][1] <= fRadius, "");
            G3DASSERT(avRet[0][2] <= fRadius, "");
            G3DASSERT(avRet[1][0] <= fRadius, "");
            G3DASSERT(avRet[1][1] <= fRadius, "");
            G3DASSERT(avRet[1][2] <= fRadius, "");

            iRet = 2;
        }
    }

    return iRet;
}

/* --------------------------------------------------------------------------
 *  g3dCalcEllipseEffectiveRadius
 *
 *  Effective radius of the ellipsoid e along the direction of plane: the
 *  length of the ellipsoid's three axis-projections onto the plane normal.
 * ------------------------------------------------------------------------ */
float g3dCalcEllipseEffectiveRadius(float (*e)[4], float *plane)
{
    float vvv[4];

    vvv[0] = sceVu0InnerProduct(e[0], plane);
    vvv[1] = sceVu0InnerProduct(e[1], plane);
    vvv[2] = sceVu0InnerProduct(e[2], plane);

    return g3dxVu0CalcLength(vvv);
}

/* --------------------------------------------------------------------------
 *  g3dCalcIntersectionEllipseAndLine
 *
 *  Intersect the ray (vStart, vDir) with the ellipsoid e by transforming the
 *  ray into the ellipsoid's unit-sphere space (inverse of e), running the
 *  sphere/line test, and mapping the hit points back through e.
 * ------------------------------------------------------------------------ */
int g3dCalcIntersectionEllipseAndLine(float (*avRet)[4], float (*e)[4], float *vStart, float *vDir)
{
    float matInv[4][4];
    float vStartInv[4];
    float vDirInv[4];
    int   iNumIntersection;
    int   i;

    /* matInv = inverse(e) */
    sceVu0InversMatrix(matInv, e);

    sceVu0ApplyMatrix(vStartInv, matInv, vStart);
    sceVu0Normalize(vDirInv, vDir);
    sceVu0ApplyMatrix(vDirInv, matInv, vDirInv);

    iNumIntersection = g3dCalcIntersectionSphereAndLine(avRet, g_v0001, 1.0f, vStartInv, vDirInv);

    if (iNumIntersection != 0)
    {
        for (i = iNumIntersection; 0 < i; i = i - 1)
        {
            /* map each unit-sphere hit back into ellipsoid space */
            sceVu0ApplyMatrix(avRet[0], e, avRet[0]);
            avRet = avRet + 1;
        }
    }

    return iNumIntersection;
}
