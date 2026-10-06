/* ==========================================================================
 *  g3dCamera.c
 *
 *  Projection-matrix builders for the zero2np 3D engine.  These compose the
 *  view->screen and view->clip transforms for both the perspective and the
 *  orthographic cases, plus the small "GS primitive" matrix that maps NDC into
 *  the GS rasteriser's screen space (centre + aspect scale).  All matrices are
 *  4x4 row-major (sceVu0FMATRIX); the VU0 matrix work is the inlined EE VU0
 *  library (sceVu0UnitMatrix / sceVu0MulMatrix).
 *
 *  Reconstructed from the Feb 6 2004 prototype (SLES_523.84).
 * ======================================================================== */

#include "g3dCamera.h"
#include "g3dMath.h"
#include "g3dxVu0.h"            /* VU0 macro-mode matrix helpers */
#include "gra3dConst.h"        /* g_matUnit */
#include <libvu0.h>            /* sceVu0UnitMatrix / sceVu0MulMatrix */
#include <mathf.h>            /* tanf */

/* --------------------------------------------------------------------------
 *  g3dCalcScreenGSPrimitiveMatrix
 *
 *  Build the diagonal scale + translate matrix that maps NDC xyz into the GS
 *  rasteriser's screen coordinates: aspect scale on the diagonal and the
 *  screen centre in the translation row.
 * ------------------------------------------------------------------------ */
void g3dCalcScreenGSPrimitiveMatrix(float (*mat)[4], float fAspectX, float fAspectY,
                                    float fAspectZ, float fCenterX, float fCenterY,
                                    float fCenterZ)
{
    sceVu0UnitMatrix(mat);

    mat[3][2] = fCenterZ;
    mat[0][0] = fAspectX;
    mat[1][1] = fAspectY;
    mat[2][2] = fAspectZ;
    mat[3][0] = fCenterX;
    mat[3][1] = fCenterY;
}

/* --------------------------------------------------------------------------
 *  g3dCalcViewScreenMatrixPerspective
 *
 *  Perspective view->screen matrix: a perspective divide (w = z) projection
 *  combined with the GS-primitive matrix.  The Z range [fZmin,fZmax] is mapped
 *  across the depth slab [fNearZ,fFarZ].
 * ------------------------------------------------------------------------ */
void g3dCalcViewScreenMatrixPerspective(float (*mat)[4], float fScrZ, float fAspectX,
                                        float fAspectY, float fCenterX, float fCenterY,
                                        float fZmin, float fZmax, float fNearZ, float fFarZ)
{
    float fCenterZ;
    float fAspectZ;
    float matScreenGSPrimitive[4][4];

    fAspectZ = (fFarZ * fNearZ * (fZmax - fZmin)) / (fFarZ - fNearZ);
    fCenterZ = (-fZmax * fNearZ + fZmin * fFarZ) / (fFarZ - fNearZ);

    sceVu0UnitMatrix(mat);

    mat[1][1] = fScrZ;
    mat[2][3] = 1.0f;
    mat[3][2] = 1.0f;
    mat[0][0] = fScrZ;
    mat[2][2] = 0.0f;
    mat[3][3] = 0.0f;

    g3dCalcScreenGSPrimitiveMatrix(matScreenGSPrimitive, fAspectX, fAspectY, fAspectZ,
                                   fCenterX, fCenterY, fCenterZ);

    sceVu0MulMatrix(mat, matScreenGSPrimitive, mat);
}

/* --------------------------------------------------------------------------
 *  g3dCalcViewScreenMatrixOrtho
 *
 *  Orthographic view->screen matrix: there is no perspective divide, so the
 *  result is simply the GS-primitive matrix (multiplied through the unit
 *  matrix to land it in mat).
 * ------------------------------------------------------------------------ */
void g3dCalcViewScreenMatrixOrtho(float (*mat)[4], float fScrZ, float fAspectX,
                                  float fAspectY, float fCenterX, float fCenterY,
                                  float fZmin, float fZmax, float fNearZ, float fFarZ)
{
    float fAspectZ;
    float fCenterZ;
    float matScreenGSPrimitive[4][4];
    float *pm2[4];

    fAspectZ = (fFarZ * fNearZ * (fZmax - fZmin)) / (fFarZ - fNearZ);
    fCenterZ = (-fZmax * fNearZ + fZmin * fFarZ) / (fFarZ - fNearZ);

    g3dCalcScreenGSPrimitiveMatrix(matScreenGSPrimitive, fAspectX, fAspectY, fAspectZ,
                                   fCenterX, fCenterY, fCenterZ);

    sceVu0MulMatrix(mat, matScreenGSPrimitive, g_matUnit);
}

/* --------------------------------------------------------------------------
 *  g3dCalcViewClipMatrixPerspective
 *
 *  Perspective view->clip (projection) matrix mapping the view frustum into
 *  the canonical clip volume.  fClipVolumeX/Y describe the clip-space extents.
 *
 *  PORT DEVIATION.  mat[1][1] is negated against the ROM.  This is the engine's
 *  only real projection matrix and it is what drives the host renderer -- the
 *  matrices it feeds (matViewClipObject, and matWorldClipObject through it) are
 *  otherwise bit-for-bit what a hand-rolled host projection produces, differing
 *  purely in that the GS rasterises with +Y up while the host clip space has +Y
 *  down.  Flipping it here rather than in the bridge keeps every clip-space
 *  matrix in the engine in one orientation, so no consumer has to remember to
 *  compensate.  The bounding-box cull does read clip Y (gra3dVu0ClipFlags), but
 *  the flip only swaps its "above top" and "below bottom" bits, and both the
 *  AND cull and the OR edge test are invariant under a bit permutation applied
 *  to every corner alike -- so neither changes.  Otherwise matLocalClip only
 *  reaches VU1 microcode that never runs.
 * ------------------------------------------------------------------------ */
void g3dCalcViewClipMatrixPerspective(float (*mat)[4], float fScrZ, float fAspectX,
                                      float fAspectY, float fNearZ, float fFarZ,
                                      float fClipVolumeX, float fClipVolumeY)
{
    float fGSX;
    float fGSY;
    float fRScrZ;

    fRScrZ = fNearZ / fScrZ;
    fGSX = fClipVolumeX * fRScrZ;
    fGSY = fClipVolumeY * fRScrZ;

    sceVu0UnitMatrix(mat);

    mat[3][3] = 0.0f;
    mat[2][3] = 1.0f;
    mat[3][2] = (fFarZ * fNearZ * -2.0f) / (fFarZ - fNearZ);
    mat[1][1] = -(((fNearZ + fNearZ) * fAspectY) / (fGSY + fGSY));
    mat[0][0] = ((fNearZ + fNearZ) * fAspectX) / (fGSX + fGSX);
    mat[2][2] = (fFarZ + fNearZ) / (fFarZ - fNearZ);
}

/* --------------------------------------------------------------------------
 *  g3dCalcViewClipMatrixOrtho
 *
 *  Orthographic view->clip (projection) matrix.
 *
 *  PORT DEVIATION, same reason as the perspective builder above, plus one
 *  more: the ROM's orthographic depth runs the other way round from its own
 *  perspective one, mapping near to +1 and far to -1.  The host depth buffer
 *  wants near at -1 like the perspective case, so the two Z terms are negated
 *  as well.  Untested in practice -- nothing in this build ever selects
 *  PT_ORTHO, so this path has no way to have been observed working.
 * ------------------------------------------------------------------------ */
void g3dCalcViewClipMatrixOrtho(float (*mat)[4], float fScrZ, float fAspectX,
                                float fAspectY, float fNearZ, float fFarZ,
                                float fClipVolumeX, float fClipVolumeY)
{
    sceVu0UnitMatrix(mat);

    mat[0][0] = fAspectX / fClipVolumeX;
    mat[1][1] = -(fAspectY / fClipVolumeY);
    mat[3][2] = -((fFarZ + fNearZ) / (fFarZ - fNearZ));
    mat[2][2] = 2.0f / (fFarZ - fNearZ);
}

/* --------------------------------------------------------------------------
 *  g3dCalcDistanceToScreen
 *
 *  Distance from the eye at which a clip-plane half-extent fClipValueAbs
 *  subtends the field-of-view fFov: fClipValueAbs / tan(fFov/2).
 * ------------------------------------------------------------------------ */
float g3dCalcDistanceToScreen(float fFov, float fClipValueAbs)
{
    return fClipValueAbs / tanf(fFov * 0.5f);
}
