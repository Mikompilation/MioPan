/* ==========================================================================
 *  gra3dBoundingBox.c
 *
 *  Axis-aligned bounding-box helpers for the gra3d layer.  A bounding box is
 *  the eight homogeneous corner points of the min/max bounds, in the canonical
 *  order produced by gra3dbbFromBounds:
 *
 *      [0] (min.x, min.y, min.z)   [4] (min.x, min.y, max.z)
 *      [1] (max.x, min.y, min.z)   [5] (max.x, min.y, max.z)
 *      [2] (min.x, max.y, min.z)   [6] (min.x, max.y, max.z)
 *      [3] (max.x, max.y, min.z)   [7] (max.x, max.y, max.z)
 *
 *  The transform helpers run the box's eight corners through the bound
 *  CVu0Matrix (LoadMatrix once, ApplyWithoutTrans per corner); the view-volume
 *  test transforms each corner by the world->clip matrix and ANDs the
 *  per-vertex clip flags so a box that is fully outside any single plane is
 *  rejected.
 *
 *  Reconstructed from the Feb 6 2004 prototype (SLES_523.84).
 * ======================================================================== */

#include "gra3dBoundingBox.h"
#include "gra3d.h"              /* g3dCalcFA / g3dCalcFB */
#include "gra3dTypes.h"         /* CVu0Matrix, G3DFOG */
#include "g3dxVu0.h"            /* VU0 macro-mode intrinsics */
#include "gra3dVu0.h"           /* gra3dVu0ClipFlags (the inlined VU0 CLIP) */
#include "ctl/fixed_array.h"
#include <mathf.h>

/* Compiler-generated copies of _fixed_array_assert / _fixed_array_verifyrange<T>
 * (001b4db8..001b4e8f) are the inlined ctl/fixed_array.h template; not emitted. */

/* --------------------------------------------------------------------------
 *  gra3dbbApplyMatrix
 *
 *  Transform the eight corners of avSrc by mat into avDest.  The corners are
 *  walked back-to-front (7..0) with a one-ahead prefetch.  ApplyWithoutTrans
 *  applies the full matrix, translation row included -- see the note on
 *  _Vu0ApplyMatrixWithoutTrans_4_5_6_7; only the source w is dropped.
 * ------------------------------------------------------------------------ */
void gra3dbbApplyMatrix(float (*avDest)[4], float (*avSrc)[4], float (*mat)[4])
{
    int    i;
    float *vDest;

    CVu0Matrix::LoadMatrix(mat);

    for (i = 7; i >= 0; i = i - 1)
    {
        float *vSrc = avSrc[i];

        prefetch(vSrc + 4, 0);

        vDest = avDest[i];
        CVu0Matrix::ApplyWithoutTrans(vDest, vSrc);
    }
}

/* --------------------------------------------------------------------------
 *  gra3dbbIsInFogArea
 *
 *  Return non-zero when any corner of the world-space box falls inside the
 *  fog ramp: each corner is run through the world->screen matrix and its
 *  screen-z is fed through the linear fog (FA*z + FB); if that exceeds zero
 *  for any corner the box touches the fog volume.  Disabled when fNear <= 0.
 * ------------------------------------------------------------------------ */
int gra3dbbIsInFogArea(float (*matWorldScreen)[4], G3DFOG *pFog, float (*avBBWorld)[4])
{
    float vWork[4];
    int   i;
    float f;

    if (pFog->fNear > 0.0f)
    {
        CVu0Matrix::LoadMatrix(matWorldScreen);

        f = 0.0f;
        for (i = 7; i >= 0; i--)
        {
            float *vBB = avBBWorld[i];

            prefetch(avBBWorld + i + 1, 0);

            CVu0Matrix::ApplyWithoutTrans(vWork, vBB);

            if (g3dCalcFA(pFog) * vWork[3] + g3dCalcFB(pFog) > f)
            {
                return 1;
            }
        }
    }

    return 0;
}

/* --------------------------------------------------------------------------
 *  gra3dbbIsInViewvolume
 *
 *  Walk the box's eight corners back-to-front and run each through two
 *  matrices at once:
 *
 *      matClip      -> a clip-space point that is CLIPped, its six-bit outside
 *                      mask ANDed into the running flags.  A non-zero result
 *                      after all eight corners means one plane rejects every
 *                      corner, so the whole box lies outside the volume.
 *      matTransform -> stored to avBBTransformed[i] for the caller to reuse
 *                      (CheckBoundingBox re-CLIPs it against the guard band to
 *                      derive the edge check).  May be NULL, see below.
 *
 *  Both products force w to 1 rather than reading the corner's own w -- the ROM
 *  accumulates with `vmaddw.xyzw vf, vf7, vf0`, and vf0.w is the constant 1.0.
 *
 *  PORT DEVIATION.  The two matrices were the VU0 register groups vf4..vf7 and
 *  vf8..vf11, loaded by the caller and never named in the signature; they are
 *  parameters here.  matTransform == NULL reproduces CheckModelBoundingBox,
 *  which loads only vf4..vf7 and leaves vf8..vf11 holding whatever the previous
 *  caller left there -- it never reads avBBTransformed, so rather than invent a
 *  transform the port simply skips the store.
 * ------------------------------------------------------------------------ */
int gra3dbbIsInViewvolume(float (*avBBTransformed)[4], float (*avBBWorld)[4],
                          float (*matClip)[4], float (*matTransform)[4])
{
    u_int uClipAnd;
    int   i;
    int   c;

    uClipAnd = 0x3f;

    for (i = 7; i >= 0; i = i - 1)
    {
        float *vBB = avBBWorld[i];
        float  vClip[4];

        prefetch(avBBWorld + i + 1, 0);

        for (c = 0; c < 4; c++)
        {
            vClip[c] = vBB[0] * matClip[0][c] + vBB[1] * matClip[1][c]
                     + vBB[2] * matClip[2][c] + matClip[3][c];
        }

        if (matTransform != NULL)
        {
            float *vDest = avBBTransformed[i];

            for (c = 0; c < 4; c++)
            {
                vDest[c] = vBB[0] * matTransform[0][c] + vBB[1] * matTransform[1][c]
                         + vBB[2] * matTransform[2][c] + matTransform[3][c];
            }
        }

        uClipAnd = uClipAnd & gra3dVu0ClipFlags(vClip);
    }

    return uClipAnd == 0;
}

/* --------------------------------------------------------------------------
 *  gra3dbbCalcCenter
 *
 *  vC = (avBB[0] + avBB[7]) * 0.5   (the box centre), w forced to 1.
 * ------------------------------------------------------------------------ */
void gra3dbbCalcCenter(float *vC, float (*avBB)[4])
{
    int c;

    /* vC = (avBB[0] + avBB[7]) * 0.5 */
    for (c = 0; c < 4; c++)
        vC[c] = (avBB[0][c] + avBB[7][c]) * 0.5f;

    vC[3] = 1.0f;
}

/* --------------------------------------------------------------------------
 *  gra3dbbCalcCenterBase
 *
 *  Centre of the box's lower face: midpoint of the two corners along the
 *  lowest edge in Y (avBB[0]/avBB[5] or avBB[2]/avBB[7] depending on which
 *  Y bound is lower), w forced to 1.
 * ------------------------------------------------------------------------ */
void gra3dbbCalcCenterBase(float *vCB, float (*avBB)[4])
{
    int iVertexIndex0;
    int iVertexIndex1;
    int c;

    if (avBB[0][1] > avBB[2][1])
    {
        iVertexIndex0 = 0;
        iVertexIndex1 = 5;
    }
    else
    {
        iVertexIndex0 = 2;
        iVertexIndex1 = 7;
    }

    /* vCB = (avBB[iVertexIndex0] + avBB[iVertexIndex1]) * 0.5 */
    for (c = 0; c < 4; c++)
        vCB[c] = (avBB[iVertexIndex0][c] + avBB[iVertexIndex1][c]) * 0.5f;

    vCB[3] = 1.0f;
}

/* --------------------------------------------------------------------------
 *  gra3dbbCalcRadiusXZ
 *
 *  The XZ-plane radius: distance (in xy after the add-across) from the base
 *  centre to the box's near-bottom corner avBB[0].
 * ------------------------------------------------------------------------ */
float gra3dbbCalcRadiusXZ(float (*avBB)[4])
{
    float vCenterBase[4];
    float dx, dz;

    gra3dbbCalcCenterBase(vCenterBase, avBB);

    /* XZ-plane radius: distance from the base centre to the near-bottom corner
       projected onto the ground plane. */
    dx = vCenterBase[0] - avBB[0][0];
    dz = vCenterBase[2] - avBB[0][2];

    return g3dxVu0Sqrt2(dx, dz);
}

/* --------------------------------------------------------------------------
 *  gra3dbbCalcInnerEllipse
 *
 *  Half-extents of the box on each axis (the inner ellipse radii); w cleared.
 * ------------------------------------------------------------------------ */
void gra3dbbCalcInnerEllipse(float *vEllipse, float (*avBB)[4])
{
    vEllipse[0] = fabsf(avBB[0][0] - avBB[1][0]) * 0.5f;
    vEllipse[1] = fabsf(avBB[0][1] - avBB[2][1]) * 0.5f;
    vEllipse[3] = 0.0f;
    vEllipse[2] = fabsf(avBB[0][2] - avBB[4][2]) * 0.5f;
}

/* --------------------------------------------------------------------------
 *  gra3dbbCopy
 *
 *  Copy the eight corner quadwords of avBBSrc into avBBDest (back-to-front,
 *  one-ahead prefetch).
 * ------------------------------------------------------------------------ */
void gra3dbbCopy(float (*avBBDest)[4], float (*avBBSrc)[4])
{
    int i;

    for (i = 7; i >= 0; i--)
    {
        float *vSrc  = avBBSrc[i];
        float *vDest = avBBDest[i];

        prefetch(vSrc + 4, 0);

        vDest[0] = vSrc[0];
        vDest[1] = vSrc[1];
        vDest[2] = vSrc[2];
        vDest[3] = vSrc[3];
    }
}

/* --------------------------------------------------------------------------
 *  gra3dbbApplyFromBounds
 *
 *  Build the eight box corners from the (vMin, vMax) bounds and transform each
 *  by mat into avBBDest.  The corner permutation matches gra3dbbFromBounds;
 *  corners [0] and [7] reuse vMin / vMax directly, the middle six are assembled
 *  on the stack.  The assembled six set w = 1 and vMin / vMax carry whatever w
 *  the caller supplied, but none of it reaches the arithmetic: the transform
 *  forces w to 1 (see _Vu0ApplyMatrixWithoutTrans_4_5_6_7), which is why
 *  charBB.c's bounds table can leave w at 0.
 * ------------------------------------------------------------------------ */
void gra3dbbApplyFromBounds(float (*avBBDest)[4], float *vMin, float *vMax, float (*mat)[4])
{
    float v[4];

    CVu0Matrix::LoadMatrix(mat);

    /* [0] = (min.x, min.y, min.z) */
    CVu0Matrix::ApplyWithoutTrans(avBBDest[0], vMin);

    /* [1] = (max.x, min.y, min.z) */
    v[0] = vMax[0];
    v[1] = vMin[1];
    v[2] = vMin[2];
    v[3] = 1.0f;
    CVu0Matrix::ApplyWithoutTrans(avBBDest[1], v);

    /* [2] = (min.x, max.y, min.z) */
    v[0] = vMin[0];
    v[1] = vMax[1];
    v[2] = vMin[2];
    v[3] = 1.0f;
    CVu0Matrix::ApplyWithoutTrans(avBBDest[2], v);

    /* [3] = (max.x, max.y, min.z) */
    v[0] = vMax[0];
    v[1] = vMax[1];
    v[2] = vMin[2];
    v[3] = 1.0f;
    CVu0Matrix::ApplyWithoutTrans(avBBDest[3], v);

    /* [4] = (min.x, min.y, max.z) */
    v[0] = vMin[0];
    v[1] = vMin[1];
    v[2] = vMax[2];
    v[3] = 1.0f;
    CVu0Matrix::ApplyWithoutTrans(avBBDest[4], v);

    /* [5] = (max.x, min.y, max.z) */
    v[0] = vMax[0];
    v[1] = vMin[1];
    v[2] = vMax[2];
    v[3] = 1.0f;
    CVu0Matrix::ApplyWithoutTrans(avBBDest[5], v);

    /* [6] = (min.x, max.y, max.z) */
    v[0] = vMin[0];
    v[1] = vMax[1];
    v[2] = vMax[2];
    v[3] = 1.0f;
    CVu0Matrix::ApplyWithoutTrans(avBBDest[6], v);

    /* [7] = (max.x, max.y, max.z) */
    CVu0Matrix::ApplyWithoutTrans(avBBDest[7], vMax);
}

/* --------------------------------------------------------------------------
 *  gra3dbbFromBounds
 *
 *  Expand the (vMin, vMax) bounds into the eight homogeneous corner points
 *  (w == 1 on the assembled corners; [0]/[7] carry vMin/vMax's own w).
 * ------------------------------------------------------------------------ */
void gra3dbbFromBounds(float (*avBBDest)[4], float *vMin, float *vMax)
{
    float *pv0;

    /* [0] = vMin */
    avBBDest[0][0] = vMin[0];
    avBBDest[0][1] = vMin[1];
    avBBDest[0][2] = vMin[2];
    avBBDest[0][3] = vMin[3];

    /* [1] = (max.x, min.y, min.z) */
    avBBDest[1][0] = vMax[0];
    avBBDest[1][1] = vMin[1];
    avBBDest[1][3] = 1.0f;
    avBBDest[1][2] = vMin[2];

    /* [2] = (min.x, max.y, min.z) */
    avBBDest[2][0] = vMin[0];
    avBBDest[2][1] = vMax[1];
    avBBDest[2][3] = 1.0f;
    avBBDest[2][2] = vMin[2];

    /* [3] = (max.x, max.y, min.z) */
    avBBDest[3][0] = vMax[0];
    avBBDest[3][1] = vMax[1];
    avBBDest[3][3] = 1.0f;
    avBBDest[3][2] = vMin[2];

    /* [4] = (min.x, min.y, max.z) */
    avBBDest[4][0] = vMin[0];
    avBBDest[4][1] = vMin[1];
    avBBDest[4][3] = 1.0f;
    avBBDest[4][2] = vMax[2];

    /* [5] = (max.x, min.y, max.z) */
    avBBDest[5][0] = vMax[0];
    avBBDest[5][1] = vMin[1];
    avBBDest[5][2] = vMax[2];
    avBBDest[5][3] = 1.0f;

    /* [6] = (min.x, max.y, max.z) */
    avBBDest[6][0] = vMin[0];
    avBBDest[6][1] = vMax[1];
    avBBDest[6][3] = 1.0f;
    avBBDest[6][2] = vMax[2];

    /* [7] = vMax */
    avBBDest[7][0] = vMax[0];
    avBBDest[7][1] = vMax[1];
    avBBDest[7][2] = vMax[2];
    avBBDest[7][3] = vMax[3];

    (void)pv0;
}
