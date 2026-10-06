/* ==========================================================================
 *  g3dBoundingVolume.h
 *
 *  Bounding-volume builders for the g3d core (box / inner ellipsoid shape
 *  matrices and ellipsoid inclusion test).  Implemented in
 *  g3dBoundingVolume.cpp.
 *
 *  Reconstructed from the Feb 6 2004 prototype (SLES_523.84).
 * ======================================================================== */

#ifndef _G3DBOUNDINGVOLUME_H
#define _G3DBOUNDINGVOLUME_H

void g3dbvBoxFromVertices(float (*box)[4], float (*av)[4]);
void g3dbvInnerEllipseFromVertices(float (*e)[4], float (*avVertices)[4]);
int  g3dbvIsEllipseInclude(float (*e)[4], float *v);

#endif /* _G3DBOUNDINGVOLUME_H */
