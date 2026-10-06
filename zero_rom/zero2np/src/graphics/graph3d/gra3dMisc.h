/* ==========================================================================
 *  gra3dMisc.h
 *
 *  Miscellaneous high-level gra3d services that sit above the core renderer
 *  and the shadow subsystem: scene/character prelighting, character and
 *  object shadow casting, the per-character "blend the strongest shadow-cast
 *  lights into one directional light" pass, character light-data generation
 *  (flashlight / self-reflection emulation) and the projector spot-light.
 *
 *  Reconstructed from the Feb 6 2004 prototype (SLES_523.84).
 * ======================================================================== */

#ifndef _GRA3DMISC_H
#define _GRA3DMISC_H

#include "gra3dTypes.h"         /* GRA3DLIGHTDATA, GRA3DLIGHTID, GRA3DEMULATIONLIGHTDATACREATIONDATA */
#include "g3dLight.h"           /* G3DLIGHT */
#include "sgd_types.h"          /* SGDFILEHEADER / SGDCOORDINATE */

struct ANI_CTRL;

#ifdef __cplusplus
extern "C" {
#endif

void         gra3dCalcShadowLight(G3DLIGHT *pLight, float *vTarget);
void         gra3dPrelightScene(int RoomNo);
void         gra3dPrelight(void);
void         gra3dDrawSGDShadow(SGDFILEHEADER *pSGDTop, SGDCOORDINATE *pCoord,
                                G3DLIGHT *pLight, float (*avBBWorld)[4]);
void         gra3dSetObjectIdDrawNoShadow(int iId);
void         gra3dDrawSGDShadowEveryObject(SGDFILEHEADER *pShadowModel, G3DLIGHT *pLight);
void         gra3dGenerateLightDataToChar(GRA3DLIGHTDATA *pLDDest, GRA3DLIGHTDATA *pLDSrc,
                                          GRA3DEMULATIONLIGHTDATACREATIONDATA *pData);
void         gra3dDrawSGDShadowCharacter(ANI_CTRL *pAC, SGDCOORDINATE *pCoord,
                                         float (*avBBWorld)[4],
                                         GRA3DEMULATIONLIGHTDATACREATIONDATA *pELDCD);
void         gra3dStartSpecialLight(void);
void         gra3dEndSpecialLight(void);
void         gra3dUpdateSpecialLight(void);
int          gra3dIsSpecialLightActive(void);
G3DLIGHT    &gra3dGetProjectorSpot(void);
GRA3DLIGHTID gra3dGetProjectorSpotId(void);

#ifdef __cplusplus
}
#endif

#endif /* _GRA3DMISC_H */
