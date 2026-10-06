/* ==========================================================================
 *  g3dCore.h
 *
 *  Public interface to the g3d core object: initialise the engine against a
 *  caller-supplied G3DCOREOBJECT, push/read render-state / global-state /
 *  transform / material / light / texture / GS-register state, apply the
 *  current light set into the VU1 memory image, and the CPU-side
 *  vertex-colour / screen-coordinate helpers.
 *
 *  Every entry point operates on the single module-static object set by
 *  g3dInitialize (s_pObject); the Get*Ref accessors hand back a writable
 *  reference into that object so callers can edit state in place.
 *
 *  Reconstructed from the Feb 6 2004 prototype (SLES_523.84).
 * ======================================================================== */

#ifndef _G3DCORE_H
#define _G3DCORE_H

#include "eetypes.h"
#include "sce_gs.h"             /* sceGsXyz */
#include "g3dMath.h"            /* sceVu0FVECTOR / sceVu0FMATRIX aliases */
#include "g3dLight.h"           /* G3DLIGHT */
#include "gra3dTypes.h"         /* G3DCOREOBJECT, render-state enums, G3DMATERIAL, CTexture */

/* --------------------------------------------------------------------------
 *  Creation.
 * ------------------------------------------------------------------------ */
void g3dInitialize(const G3DCREATIONDATA *pCD);

/* --------------------------------------------------------------------------
 *  Render state (per-object boolean / enum flags).
 * ------------------------------------------------------------------------ */
int            g3dSetRenderState(G3DRENDERSTATETYPE State, unsigned int uiValue);
unsigned int  &g3dGetRenderStateRef(G3DRENDERSTATETYPE State);

/* --------------------------------------------------------------------------
 *  Global state (lighting type / attenuation type).
 * ------------------------------------------------------------------------ */
int            g3dSetGlobalState(G3DGLOBALSTATETYPE State, unsigned int uiValue);
unsigned int  &g3dGetGlobalStateRef(G3DGLOBALSTATETYPE State);

/* --------------------------------------------------------------------------
 *  Global ambient colour.
 * ------------------------------------------------------------------------ */
void           g3dSetAmbient(float *vAmbient);
float        (&g3dGetAmbientRef())[4];

/* --------------------------------------------------------------------------
 *  Transform matrices (view / projection / world / world1 / worldclip).
 * ------------------------------------------------------------------------ */
int            g3dSetTransform(G3DTRANSFORMSTATETYPE State, float (*mat)[4]);
float        (&g3dGetTransformRef(G3DTRANSFORMSTATETYPE State))[4][4];

/* --------------------------------------------------------------------------
 *  Material.
 * ------------------------------------------------------------------------ */
int            g3dSetMaterial(const G3DMATERIAL *pMaterial);
G3DMATERIAL   &g3dGetMaterialRef(void);

/* --------------------------------------------------------------------------
 *  Lights.
 * ------------------------------------------------------------------------ */
int            g3dLightEnable(int iLightId, int bEnable);
int            g3dIsLightEnable(int iLightId);
int            g3dSetLight(int iLightId, const G3DLIGHT *pLight);
G3DLIGHT      &g3dGetLightRef(int iLightId);
int            g3dApplyLight(void);

/* --------------------------------------------------------------------------
 *  Texture.
 * ------------------------------------------------------------------------ */
int            g3dSetTexture(int iStage, CTexture *pTexture);

/* --------------------------------------------------------------------------
 *  CPU-side per-vertex colour evaluation (mirror of the VU1 lighting).
 * ------------------------------------------------------------------------ */
/* ZERO2.MAP mangles this `g3dCalcVertexColor__FPfPCfN21` -- one float* and
 * three const float* -- so the colour source is const in the ROM too. */
void           g3dCalcVertexColor(float *vDest, const float *vVertex,
                                  const float *vNormal, const float *vColorSource);

/* Host renderer snapshot of the already-derived VU1 lighting image.  Keeping
 * this narrow copy API avoids exposing G3DCOREOBJECT while allowing animated
 * vertex lighting to move from the CPU mirror above into a vertex shader. */
struct G3DVERTEXLIGHTINGSTATE
{
    /* x=lighting type, y=attenuation type, z=VU enable mask, w=reserved. */
    unsigned int config[4];
    /* xyz=view transform row 3, w=material specular power. */
    float eye_position[4];
    float ambient[4];

    float directional_direction[3][4];
    float directional_ambient[3][4];
    float directional_diffuse[3][4];
    float directional_specular[3][4];

    float point_position[3][4];
    float point_params[3][4];
    float point_ambient[3][4];
    float point_diffuse[3][4];
    float point_specular[3][4];

    float spot_position[3][4];
    float spot_direction[3][4];
    float spot_params[3][4];
    float spot_ambient[3][4];
    float spot_diffuse[3][4];
    float spot_specular[3][4];
};

int            g3dSnapshotVertexLighting(G3DVERTEXLIGHTINGSTATE *out);

/* Selects whether g3dCalcVertexColor() emulates a VU1 draw (1) or the EE-side
 * prelight bake (0).  The two differ in the point attenuation law and in the
 * per-term colour scales; see the note in g3dCore.c. */
/* Returns the previous setting, so a caller can restore it rather than
 * assuming the ambient state was the prelight one. */
int            g3dSetRealtimeLighting(int bRealtime);

/* The per-term GS scales gra3dCalcVu1MaterialData*() folds into the VU1's
 * colour packets -- fAmbientScale, fDiffuseScale and the directional
 * fSpecularSum * fSpecularScale.  gra3dSetMaterial() pushes them here so the
 * host paths can reproduce them; a VU1 draw does not use one flat 255. */
void           g3dSetVu1ColourScales(float fAmbient, float fDiffuse,
                                     float fSpecular);

/* --------------------------------------------------------------------------
 *  GS register shadow / upload.
 * ------------------------------------------------------------------------ */
int            g3dSetGsRegister(long int lData, long int lAddress, int iDmaChan);
int            g3dSetGsRegisters(const sceGifPackAd *aGPA, int iNum, int iDmaChan);
long int      &g3dGetGsRegisterRef(long int lAddress);

/* --------------------------------------------------------------------------
 *  Screen <-> GS-primitive coordinate conversion.
 * ------------------------------------------------------------------------ */
void           g3dCalcGsPrimitiveCoord(sceGsXyz *pGsXyz, const float *vScreenCoord);
void           g3dCalcScreenCoord(float *vScreenCoord, const sceGsXyz *pGsXyz);

#endif /* _G3DCORE_H */
