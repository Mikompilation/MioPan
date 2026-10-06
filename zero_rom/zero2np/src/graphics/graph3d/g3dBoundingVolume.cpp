/* ==========================================================================
 *  g3dBoundingVolume.cpp
 *
 *  Bounding-volume builders for the zero2np 3D engine.  A bounding box / inner
 *  ellipsoid is stored as a 4x4 "shape matrix": rows 0..2 are the three
 *  (half-extent scaled) axis vectors and row 3 is the centre, so a point is
 *  inside the ellipsoid when |inverse(box) * point| <= 1.
 *
 *  The inlined PS2 VU0 (macro-mode) blocks are written here as the equivalent
 *  SCE EE VU0 library / g3dxVu0 calls, matching the other reconstructed
 *  graph3d sources.
 *
 *  Reconstructed from the Feb 6 2004 prototype (SLES_523.84).
 * ======================================================================== */

#include "g3dBoundingVolume.h"
#include "g3dMath.h"
#include "g3dxVu0.h"            /* VU0 macro-mode helpers */
#include <libvu0.h>            /* sceVu0* */
#include <string.h>            /* memset */

/* --------------------------------------------------------------------------
 *  g3dbvBoxFromVertices
 *
 *  Build the box shape matrix from the eight corner vertices av[0..7].  The
 *  three axes are the normalised edges out of corner 0 (av[1], av[2], av[4]);
 *  each is scaled to half the corresponding edge length, and the centre is the
 *  midpoint of the opposite corners av[0] and av[7].
 * ------------------------------------------------------------------------ */
void g3dbvBoxFromVertices(float (*box)[4], float (*av)[4])
{
    float  vScale[4];
    float *pv0;
    float *pv2;

    memset(box, 0, sizeof(float[4][4]));

    /// GRA3DBOUNDINGBOXVERTEXINDEX
    
    /* axis directions = normalised edges from corner 0 */
    pv0 = box[0];
    pv2 = av[0];
    sceVu0SubVector(box[0], av[1], av[0]);
    sceVu0Normalize(box[0], box[0]);
    sceVu0SubVector(box[1], av[2], av[0]);
    sceVu0Normalize(box[1], box[1]);
    sceVu0SubVector(box[2], av[4], av[0]);
    sceVu0Normalize(box[2], box[2]);

    /* half extents along each edge */
    vScale[0] = g3dxVu0Length3(av[1], av[0]) * 0.5f;
    vScale[1] = g3dxVu0Length3(av[2], av[0]) * 0.5f;
    vScale[2] = g3dxVu0Length3(av[4], av[0]) * 0.5f;

    sceVu0ScaleVector(box[0], box[0], vScale[0]);
    sceVu0ScaleVector(box[1], box[1], vScale[1]);
    sceVu0ScaleVector(box[2], box[2], vScale[2]);

    /* centre = midpoint of opposite corners av[0] and av[7] */
    sceVu0ScaleVector(box[3], av[0], 0.5f);
    sceVu0ScaleVector(vScale, av[7], 0.5f);
    sceVu0AddVector(box[3], box[3], vScale);
}

/* --------------------------------------------------------------------------
 *  g3dbvInnerEllipseFromVertices
 *
 *  The inner ellipsoid of a box uses the same shape matrix as the box itself.
 * ------------------------------------------------------------------------ */
void g3dbvInnerEllipseFromVertices(float (*e)[4], float (*avVertices)[4])
{
    g3dbvBoxFromVertices(e, avVertices);
}

/* --------------------------------------------------------------------------
 *  g3dbvIsEllipseInclude
 *
 *  True when point v lies inside the ellipsoid e: transform v into the
 *  ellipsoid's unit-sphere space (inverse of e) and test |v'| <= 1.
 * ------------------------------------------------------------------------ */
int g3dbvIsEllipseInclude(float (*e)[4], float *v)
{
    float vInv[4];
    float matInv[4][4];

    sceVu0InversMatrix(matInv, e);
    sceVu0ApplyMatrix(vInv, matInv, v);

    return g3dxVu0CalcLength(vInv) <= 1.0f;
}
