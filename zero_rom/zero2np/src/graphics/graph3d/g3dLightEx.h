#ifndef _G3DLIGHTEX_H
#define _G3DLIGHTEX_H

#include "gra3dTypes.h"
#include "g3dLight.h"

void g3dEmulateDirectionalLight(G3D_EMULATE_DIRECTIONALLIGHT_DATA *pEDD, G3DLIGHT *pLight, float *vPos);
void g3dGenerateDirectionalLightByEmulatedData(G3DLIGHT *aDest, G3D_EMULATE_DIRECTIONALLIGHT_DATA *aSrc, int iNumEmulated);
void g3dBlendLight(G3DLIGHT *pLight, G3DLIGHT **apSrc, int iNumSrc, float *vTarget);
int g3dIsBBLightingup(G3DLIGHT *pLight, float (*avBB)[4]);
float g3dCalcLightPower(G3DLIGHT *pLight, float *vPos);
void g3dSortLightForBoundingBoxByPowerOrder(int iNum, LIGHTCOMPAREDATA *aCD, G3DLIGHT **apLightSrc, float (*avBB)[4]);

#endif /* _G3DLIGHTEX_H */