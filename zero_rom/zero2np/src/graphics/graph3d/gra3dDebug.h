/* ==========================================================================
 *  gra3dDebug.h
 *
 *  Debug drawing / verification helpers for the gra3d layer: world-space line
 *  / point / sphere / bounding-box / ranged-cone primitives (forwarded to the
 *  low-level debug-draw library), a screen-space textured sprite, VIF-code and
 *  VU1-memory verifiers, an LMATRIX dump, and a light-data normaliser.
 *
 *  Reconstructed from the Feb 6 2004 prototype (SLES_523.84).
 * ======================================================================== */

#ifndef _GRA3DDEBUG_H
#define _GRA3DDEBUG_H

#include "gra3dTypes.h"         /* G3DCOLOR, G3DFREGION, GRA3DLIGHTDATA, tVIF_CODE */
#include "sce_gs.h"             /* sceGsTex0 */

#ifdef __cplusplus
extern "C" {
#endif

void gra3ddbgDrawLine(float *vStart, float *vEnd, G3DCOLOR col);
void gra3ddbgDrawPoint(float *vPoint, G3DCOLOR col);
void gra3ddbgVerifyVu1MemAddress(void);
void gra3ddbgDrawSphere(float *vPosition, float fRange, G3DCOLOR col, float *vScale);
void gra3ddbgDrawRangedCone(float *vPosition, float *vDirection, float fRange, float fAngle, G3DCOLOR col);
void gra3ddbgDrawSprite(G3DFREGION *pRegion, float fZ, G3DCOLOR col, sceGsTex0 *pGsTex0);
void gra3ddbgDrawBB(float (*avBB)[4], G3DCOLOR col);
void gra3ddbgDrawProc(void);
void gra3ddbgVerifyVifCodex4(tVIF_CODE *aVC);
void gra3ddbgNormalizeLightData(GRA3DLIGHTDATA *pLDDest, GRA3DLIGHTDATA *pLDSrc);

#ifdef __cplusplus
}
#endif

#endif /* _GRA3DDEBUG_H */
