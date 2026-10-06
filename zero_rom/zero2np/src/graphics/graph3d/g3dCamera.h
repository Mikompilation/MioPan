/* ==========================================================================
 *  g3dCamera.h
 *
 *  Projection-matrix builders for the g3d core (perspective / orthographic
 *  view->screen and view->clip transforms), implemented in g3dCamera.c.
 *
 *  Reconstructed from the Feb 6 2004 prototype (SLES_523.84).
 * ======================================================================== */

#ifndef _G3DCAMERA_H
#define _G3DCAMERA_H

#ifdef __cplusplus
extern "C" {
#endif

void  g3dCalcScreenGSPrimitiveMatrix(float (*mat)[4], float fAspectX, float fAspectY,
                                     float fAspectZ, float fCenterX, float fCenterY,
                                     float fCenterZ);
void  g3dCalcViewScreenMatrixPerspective(float (*mat)[4], float fScrZ, float fAspectX,
                                         float fAspectY, float fCenterX, float fCenterY,
                                         float fZmin, float fZmax, float fNearZ, float fFarZ);
void  g3dCalcViewScreenMatrixOrtho(float (*mat)[4], float fScrZ, float fAspectX,
                                   float fAspectY, float fCenterX, float fCenterY,
                                   float fZmin, float fZmax, float fNearZ, float fFarZ);
void  g3dCalcViewClipMatrixPerspective(float (*mat)[4], float fScrZ, float fAspectX,
                                       float fAspectY, float fNearZ, float fFarZ,
                                       float fClipVolumeX, float fClipVolumeY);
void  g3dCalcViewClipMatrixOrtho(float (*mat)[4], float fScrZ, float fAspectX,
                                 float fAspectY, float fNearZ, float fFarZ,
                                 float fClipVolumeX, float fClipVolumeY);
float g3dCalcDistanceToScreen(float fFov, float fClipValueAbs);

#ifdef __cplusplus
}
#endif

#endif /* _G3DCAMERA_H */
