/* ==========================================================================
 *  gra3dSGDData.h
 *
 *  Public interface for the SGD data layer: offset<->pointer relocation
 *  (sgdRemap / sgdRemapInverse), bone-coordinate evaluation, light-data
 *  verification, bounding-box extraction and the various accessors.
 *
 *  Reconstructed from the Feb 6 2004 prototype (SLES_523.84).
 * ======================================================================== */

#ifndef _GRA3DSGDDATA_H
#define _GRA3DSGDDATA_H

#include "sgd_types.h"
#include "gra3dLightData.h"     /* GRA3DLIGHTDATA, ZERO2LIGHTDATAFILE */

#ifdef __cplusplus
extern "C" {
#endif

/* ---- relocation: file-relative offsets <-> live pointers --------------- */
void sgdRemap(SGDFILEHEADER *pSGDHead);
void sgdRemapInverse(SGDFILEHEADER *pSGDHead);

/* ---- material cache ---------------------------------------------------- */
void sgdResetMaterialCache(SGDFILEHEADER *pSGDData);

/* ---- coordinate / bone matrices ---------------------------------------- */
void sgdCalcBoneCoordinate(SGDCOORDINATE *pCoord, int iNumBlock);
void sgdCalcCoordinate(SGDFILEHEADER *pSGDData, float matLocalWorld[4][4]);
void sgdCalcCoordinateMatrix(SGDCOORDINATE *pCoord);
void sgdGetLocalWorldMatrix(const void *pSGDTop, float mat[4][4], int iObjectId);

void sgdClearCoordCalcFlg(void *pSGDData, int bone_no);
void sgdClearCoordCalcFlgAll(void *pSGDData);
void sgdClearCoordCalcFlgParents(void *pSGDData, int bone_no);

/* ---- light data -------------------------------------------------------- */
void sgdVerifyLightData(GRA3DLIGHTDATA *pRet, ZERO2LIGHTDATAFILE *pZLD);

/* ---- queries ----------------------------------------------------------- */
SGDPROCUNITHEADER *sgdGetProcUnit(SGDPROCUNITHEADER *pPUHead, int iProcUnitId, int iUnitIndex);
void               sgdGetBoundingBox(SGDFILEHEADER *pFH, float avBB[8][4]);

/* ---- options ----------------------------------------------------------- */
void sgdEnableOptimizeTexture(int b);

#ifdef __cplusplus
}
#endif

#endif /* _GRA3DSGDDATA_H */
