/* ==========================================================================
 *  gra3d.h
 *
 *  Public interface to the gra3d facade layer -- the high-level wrapper the
 *  game talks to that sits on top of the g3d core (g3dCore.c).  It owns the
 *  module camera, fog, the 39-slot light bank (s_aLight / s_LightManage), the
 *  per-light-type VU1 light/material data builders and the GS-register and
 *  transform forwarding into the VU1 scratchpad image.
 *
 *  Reconstructed from the Feb 6 2004 prototype (SLES_523.84).
 * ======================================================================== */

#ifndef _GRA3D_H
#define _GRA3D_H

#include "eetypes.h"
#include "g3dMath.h"            /* sceVu0FVECTOR / sceVu0FMATRIX aliases       */
#include "g3dLight.h"           /* G3DLIGHT / G3DLIGHTTYPE                     */
#include "gra3dTypes.h"         /* GRA3DCAMERA, GRA3DLIGHTDATA, render enums   */
#include "sgd_types.h"          /* SGDFILEHEADER / SGDMATERIAL / SGDCOORDINATE */
#include "sce_gs.h"             /* sceGifPackAd                                */

/* ---- initialisation / frame ------------------------------------------- */
void  gra3dInit(void *pPacket, int iSize);
void  gra3dDraw(void);

/* ---- scratchpad ------------------------------------------------------- */
void  gra3dUseScratchpad(int b);
int   gra3dIsUsingScratchpad(void);

/* ---- light bank ------------------------------------------------------- */
void  gra3dSetLight(int iLightId, G3DLIGHT *pLight);
G3DLIGHT &gra3dGetLightRef(int iLightId);
void  gra3dLightEnable(int iLightId, int bEnable);
int   gra3dIsLightEnable(int iLightId);
void  gra3dLightEnableAll(int bEnable);
void  gra3dLightEnablePush(void);
void  gra3dLightEnablePop(void);
void  gra3dSetLightStatus(int iLightId, GRA3DLIGHTSTATUS *pS);
GRA3DLIGHTSTATUS &gra3dGetLightStatusRef(int iLightId);
void  gra3dEnableLightType(G3DLIGHTTYPE type, int bEnable);
int   gra3dIsLightTypeEnable(G3DLIGHTTYPE type);
int   gra3dGetNumEnableLight(int iLightType);
void  gra3dApplyLight(void);

/* ---- light-data records ----------------------------------------------- */
void  gra3dLightPushData(void);
void  gra3dLightPopData(void);
void  gra3dSetLightData(GRA3DLIGHTDATA *pLightData, float *vTrans);
void  gra3dLightDataAddOffsetPosition(GRA3DLIGHTDATA *pDest, const GRA3DLIGHTDATA *pSrc,
                                      const float *vPosition);
void  gra3dEmulateLightData(GRA3DLIGHTDATA *pLDDest, GRA3DLIGHTDATA *pLDSrc,
                            float *vPosition, float fMagnification);
void  gra3dEmulateLightDataObj(GRA3DLIGHTDATA *pLDDest, GRA3DLIGHTDATA *pLDSrc,
                               float *vPosition, float fMagnification);
int   gra3dGetNumLightEnable(GRA3DLIGHTDATA *pLD, int iLightType);
int   gra3dGetNumLightInitial(GRA3DLIGHTDATA *pLD, int iLightType);
void  utilSetGRA3DLIGHTDATADefault(GRA3DLIGHTDATA *pLD);

/* ---- ambient / material ----------------------------------------------- */
void  gra3dSetAmbient(float *vAmbient);
float (&gra3dGetAmbientRef(void))[4];
void  gra3dSetMaterial(SGDMATERIAL *pMat);

/* ---- VU1 light / material data builders ------------------------------- */
void  g3dSetVu1LightData(GRA3DVU1LIGHTDATA *pVu1LightData, SGDCOORDINATE *cp0,
                         SGDCOORDINATE *cp1);
void *g3dGetVu1MaterialCache(G3DLIGHTTYPE type, int iIndex);
void  gra3dCalcVu1MaterialDataDirectional(GRA3DVU1MATERIALDATA_DIRECTIONAL *_pDirectionalData);
void  gra3dCalcVu1MaterialDataPoint(GRA3DVU1MATERIALDATA_POINT *_pPointData);
void  gra3dCalcVu1MaterialDataSpot(GRA3DVU1MATERIALDATA_SPOT *_pSpotData);
void  gra3dVu1TransGTEOP(void);
void  SetVU1Header(void);

/* PORT ADDITION -- hand the current VU1 light image to the host renderer, one
 * float4 per light.  This is the realtime model the microcode implements, not
 * g3dSnapshotVertexLighting()'s prelight mirror; see vu1/LIGHTING.md. */
void  gra3dSnapshotVu1Lighting(GRA3DVU1LIGHTSNAPSHOT *out);

/* PORT ADDITION -- the microcode's three lighting kernels on the CPU, for host
 * paths that light vertices before submitting them.  Local vVertex/vNormal
 * through GRA3DTS_WORLD; vSrcColor and the result in GS 0..255 units, NULL to
 * seed from black.  Take the snapshot once per mesh, not per vertex. */
/* PORT ADDITION -- the three-lane light derivation, run over the whole bank.
 * See the long note on the definition in gra3d.c. */
int   gra3dCalcVu1WideLanes(GRA3DVU1LANE *aLanes, int iMaxLanes,
                            G3DLIGHTTYPE type, const float *vRefPos);
unsigned int gra3dGetMaterialPrimType(void);

void  gra3dCalcVu1VertexColor(float *vDest, const GRA3DVU1LIGHTSNAPSHOT *snap,
                              const float *vVertex, const float *vNormal,
                              const float *vSrcColor);

/* ---- clip volume ------------------------------------------------------- *
 * The screen-space half-extents the view-screen matrices are built from.
 * External in the ROM; MapSky.c builds its own perspective matrix from
 * _GetClipVolumeV()[0][1]. */
float (*_GetClipVolume(void))[4];
float (*_GetClipVolumeV(void))[4];

/* ---- camera ----------------------------------------------------------- */
GRA3DCAMERA *gra3dGetCamera(void);
float (&gra3dcamGetPosition(void))[4];
float (&gra3dcamGetDirection(void))[4];
float (&gra3dcamGetTarget(void))[4];
float (&gra3dcamGetPositionOld(void))[4];
void  gra3dcamSetPosition(float *vPos);
void  gra3dcamSetPosition(float x, float y, float z);
void  gra3dcamSetCoord(float (*mat)[4]);
void  gra3dcamSetTarget(float *vTarget, int bFixUp);
void  gra3dcamSetTarget(float x, float y, float z, int bFixUp);
void  gra3dcamSetRoll(float fRad);
float gra3dcamGetRoll(void);
void  gra3dcamRotationByAxis(float *vAxis, float fAngle);
void  gra3dcamSetFov(float fFov);
float gra3dcamGetFov(void);
void  gra3dcamSetAspect(float fX, float fY);
void  gra3dcamSetDepth(float fMinZ, float fMaxZ);
void  gra3dcamSetClip(float fNearZ, float fFarZ);
void  gra3dcamSetType(G3DCAMPROJECTIONTYPE type);
void  gra3dCalcWorldScreenMatrix(float (*mat)[4], GRA3DCAMERA *pCam, int bFixup);
void  gra3dApplyCamera(GRA3DCAMERA *pCam, int bFixup);
void  _gra3dSetCameraForce(GRA3DCAMERA *pCamera);

/* ---- bounding-box visibility ------------------------------------------ */
/* matClip drives the cull; matTransform produces avBBTransformed and may be
 * NULL when the caller does not read it.  Both were VU0 registers in the ROM. */
int   gra3dIsBBInViewvolume(float (*avBBTransformed)[4], float (*avBBClipped)[4],
                            float (*avBBWorld)[4],
                            float (*matClip)[4], float (*matTransform)[4]);
int   CheckModelBoundingBox(float (*avBBWorld)[4]);

/* ---- prelight --------------------------------------------------------- */
void  gra3dExecPrelight(SGDFILEHEADER *pSGDHead, float *vTrans, float *vRot);
void  gra3dExecPrelight(SGDFILEHEADER *pSGDHead, float (*mat)[4]);

/* ---- fog -------------------------------------------------------------- */
void  gra3dSetFog(float fMin, float fMax, float fNear, float fFar);
void  gra3dSetFog(G3DFOG *pFog);
void  gra3dSetFogColor(int r, int g, int b);
void  gra3dApplyFog(void);
G3DFOG &gra3dGetFogRef(void);
int   gra3dIsFogEnable(void);
void  gra3dEnableFog(int b);
void  g3dCalcVu1Fog(G3DVU1FOG *pVu1Fog, G3DFOG *pFog);
float g3dCalcFA(G3DFOG *pFog);
float g3dCalcFB(G3DFOG *pFog);

/* ---- monotone draw ---------------------------------------------------- */
int   gra3dIsMonotoneDrawEnable(void);
void  gra3dMonotoneDrawEnable(int bEnable);

/* ---- GS register / transform forwarding ------------------------------- */
int   gra3dSetGsRegister(long int lData, long int lAddress);
int   gra3dSetGsRegisters(sceGifPackAd *aGPA, int iNum);
long int &gra3dGetGsRegisterRef(long int lAddress);
void  gra3dSetGsRegisterDefault(void);
int   gra3dSetTransform(GRA3DTRANSFORMSTATETYPE state, float (*mat)[4]);
float (&gra3dGetTransformRef(GRA3DTRANSFORMSTATETYPE state))[4][4];

/* ---- light intensity -------------------------------------------------- */
void  gra3dSetLightIntens(G3DLIGHT *pLight, float fIntens);

/* ---- misc ------------------------------------------------------------- */
void  gra3dCalcVertexColor(float *vDest, float *vVertex, float *vNormal, float *vSrcColor);
void  gra3dSetValidLightId(int iLightType, unsigned int iIndex, int iLightTypeIndex);
void  gra3dDrawPrimitive(void *pData, int iSize);

/* ---- TEMPORARY PROBE -- remove with its definition in gra3d.c --------- *
 *  Dumps every enabled positional light at a character draw, with its
 *  distance to vRef.  Diagnosing the cutscene-only blown-out hand: compare
 *  the "scene" and "gameplay" dumps and look for a slot the scene bank has
 *  that gameplay does not, or one sitting ~0 units from the torch.
 *  Capped at 8 lines per label.  grep MIOPAN_PROBE to strip.           */
void  MioPan_ProbeDumpCharLights(const char *where, const float *vRef);

#endif /* _GRA3D_H */
