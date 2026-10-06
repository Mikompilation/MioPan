/* ==========================================================================
 *  gra3dBoundingBox.h
 *
 *  Axis-aligned bounding-box helpers for the gra3d layer.  A box is stored as
 *  eight homogeneous corner points (float[8][4]); the routines here build a
 *  box from min/max bounds, transform it by a matrix, derive its centre /
 *  base-centre / XZ radius / inner ellipse, and test it against the view
 *  volume and the fog volume.
 *
 *  Reconstructed from the Feb 6 2004 prototype (SLES_523.84).
 * ======================================================================== */

#ifndef _GRA3DBOUNDINGBOX_H
#define _GRA3DBOUNDINGBOX_H

#include "gra3dTypes.h"         /* G3DFOG */

#ifdef __cplusplus
extern "C" {
#endif

void  gra3dbbApplyMatrix(float (*avDest)[4], float (*avSrc)[4], float (*mat)[4]);
int   gra3dbbIsInFogArea(float (*matWorldScreen)[4], G3DFOG *pFog, float (*avBBWorld)[4]);
/* matClip drives the cull; matTransform produces avBBTransformed and may be
 * NULL when the caller does not read it (see the definition). */
int   gra3dbbIsInViewvolume(float (*avBBTransformed)[4], float (*avBBWorld)[4],
                            float (*matClip)[4], float (*matTransform)[4]);
void  gra3dbbCalcCenter(float *vC, float (*avBB)[4]);
void  gra3dbbCalcCenterBase(float *vCB, float (*avBB)[4]);
float gra3dbbCalcRadiusXZ(float (*avBB)[4]);
void  gra3dbbCalcInnerEllipse(float *vEllipse, float (*avBB)[4]);
void  gra3dbbCopy(float (*avBBDest)[4], float (*avBBSrc)[4]);
void  gra3dbbApplyFromBounds(float (*avBBDest)[4], float *vMin, float *vMax, float (*mat)[4]);
void  gra3dbbFromBounds(float (*avBBDest)[4], float *vMin, float *vMax);

#ifdef __cplusplus
}
#endif

#endif /* _GRA3DBOUNDINGBOX_H */
