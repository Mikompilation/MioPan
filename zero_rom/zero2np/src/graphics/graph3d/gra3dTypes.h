/* ==========================================================================
 *  gra3dTypes.h
 *
 *  The master graphics type cluster for the zero2np 3D engine: render/global
 *  state enums, the VU1 micro-memory layouts (packed + direct views), the GS
 *  register file image, the g3d core object, camera/material/light data, the
 *  VIF1 command encodings and the DMA chain tag.
 *
 *  Every struct/enum is transcribed verbatim (offsets + sizes preserved) from
 *  the prototype's debug type info.  SGD on-disc structures live in
 *  sgd_types.h; the G3DLIGHT primitives in g3dLight.h; XVECTOR/XMATRIX and the
 *  float[] aliases in g3dMath.h.
 *
 *  Reconstructed from the Feb 6 2004 prototype (SLES_523.84).
 * ======================================================================== */

#ifndef _GRA3DTYPES_H
#define _GRA3DTYPES_H

#include "eetypes.h"
#include <libvu0.h>
#include "sce_gs.h"             /* sceGs* / tVIF* register types, SCEGIFTAG_EOP */
#include "g3dMath.h"            /* XVECTOR, VECTOR3, LMATRIX */
#include "g3dLight.h"           /* G3DLIGHT, _LIGHTDATA, G3DVU1LIGHTSTATUS */
#include "ctl/fixed_array.h"    /* fixed_array<G3DLIGHT,39>, ...           */

/* G3DCOLOR: packed 32-bit RGBA. */
typedef unsigned int G3DCOLOR;

/* ---- forward declarations for resource-interface pointers -------------- *
 * These are opaque to the graph3d layer (defined by the resource manager).  */
struct CTexture;
struct IG3DVertexBuffer;
struct IG3DIndexBuffer;

/* The GS register file and VIF1 register file also have "array" views that
 * overlay the named-register structs; only the named view is reconstructed,
 * so the array view is an opaque same-size placeholder. */
struct G3DGSREGISTERLAYOUT_ARRAY  { long int alReg[128]; };   /* 0x400 */
struct G3DVIF1REGISTERLAYOUT_ARRAY { unsigned int auiReg[22]; }; /* 0x58 */

struct G3DMATERIAL          /* size 0x50 */
{
    sceVu0FVECTOR vDiffuse;   /* 0x00 */
    sceVu0FVECTOR vAmbient;   /* 0x10 */
    sceVu0FVECTOR vSpecular;   /* 0x20 */
    sceVu0FVECTOR vEmissive;   /* 0x30 */
    float fPower;   /* 0x40 */
    int aiPad[3];   /* 0x44 */
};

/* G3DVIF1CODE_DIRECT is defined once with the rest of the VIF1CODE group below
   (see the G3DVIF1CODE union); the earlier duplicate here was removed. */

enum G3DRENDERSTATETYPE
{
    G3DRS_LIGHTING = 0,
    G3DRS_COLORVERTEX = 1,
    G3DRS_SPECULARENABLE = 2,
    G3DRS_DIFFUSEMATERIALSOURCE = 3,
    G3DRS_SPECULARMATERIALSOURCE = 4,
    G3DRS_AMBIENTMATERIALSOURCE = 5,
    G3DRS_EMISSIVEMATERIALSOURCE = 6,
    G3DRS_FOGCOLOR = 7,
    NUM_G3DRENDERSTATETYPE = 8,
    G3DRENDERSTATE_FORCE_DWORD = 2147483647,
};

enum G3DGLOBALSTATETYPE
{
    G3DGS_LIGHTATTENUATIONTYPE = 0,
    G3DGS_LIGHTINGTYPE = 1,
    NUM_G3DGLOBALSTATETYPE = 2,
    G3DGLOBALSTATE_FORCE_DWORD = 2147483647,
};

enum G3DMATERIALCOLORSOURCE
{
    G3DMCS_MATERIAL = 0,
    G3DMCS_COLOR1 = 1,
    G3DMCS_COLOR2 = 2,
    G3DMCS_FORCE_DWORD = 2147483647,
};

enum G3DLIGHTATTENUATIONTYPE
{
    G3DLAT_LINEAR = 0,
    G3DLAT_HYPERBOLIC = 1,
    G3DLAT_FORCE_DWORD = 2147483647,
};

enum G3DLIGHTINGTYPE
{
    G3DLT_CONSTANT = 0,
    G3DLT_LAMBERT = 1,
    G3DLT_PHONG = 2,
    NUM_G3DLIGHTINGTYPE = 3,
    G3DLIGHTINGTYPE_FORCE_DWORD = 2147483647,
};

enum G3DLIGHTINDEX
{
    G3DLIDX_DIRECTIONAL_0 = 0,
    G3DLIDX_DIRECTIONAL_1 = 1,
    G3DLIDX_DIRECTIONAL_2 = 2,
    G3DLIDX_POINT_0 = 3,
    G3DLIDX_POINT_1 = 4,
    G3DLIDX_POINT_2 = 5,
    G3DLIDX_SPOT_0 = 6,
    G3DLIDX_SPOT_1 = 7,
    G3DLIDX_SPOT_2 = 8,
    NUM_G3DLIGHTINDEX = 9,
    G3D_START_LIGHT_DIRECTIONAL = 0,
    G3D_END_LIGHT_DIRECTIONAL = 2,
    G3D_NUM_LIGHT_DIRECTIONAL = 3,
    G3D_START_LIGHT_POINT = 3,
    G3D_END_LIGHT_POINT = 5,
    G3D_NUM_LIGHT_POINT = 3,
    G3D_START_LIGHT_SPOT = 6,
    G3D_END_LIGHT_SPOT = 8,
    G3D_NUM_LIGHT_SPOT = 3,
    G3D_MAX_LIGHT_PER_TYPE = 3,
    INVALID_G3DLIGHTINDEX = 2147483647,
    G3DLIGHTINDEX_FORCE_DWORD = 2147483647,
};

struct G3DSCREEN          /* size 0x10 */
{
    float fWidth;   /* 0x0 */
    float fHeight;   /* 0x4 */
    float fDistance;   /* 0x8 */
    float fDepth;   /* 0xc */
};

struct G3DWINDOW          /* size 0x10 */
{
    float fAspectX;   /* 0x0 */
    float fAspectY;   /* 0x4 */
    float fCenterX;   /* 0x8 */
    float fCenterY;   /* 0xc */
};

struct G3DDEPTH          /* size 0x10 */
{
    float fZmax;   /* 0x0 */
    float fZmin;   /* 0x4 */
    float fNearZ;   /* 0x8 */
    float fFarZ;   /* 0xc */
};

struct G3DVIEWPORT          /* size 0x18 */
{
    float fX;   /* 0x00 */
    float fY;   /* 0x04 */
    float fWidth;   /* 0x08 */
    float fHeight;   /* 0x0c */
    float fMinZ;   /* 0x10 */
    float fMaxZ;   /* 0x14 */
};

enum G3DCAMPROJECTIONTYPE
{
    PT_PERSPECTIVE = 0,
    PT_ORTHO = 1,
    NUM_G3DCAMPROJECTIONTYPE = 2,
    G3DCAMPROJECTIONTYPE_FORCE_DWORD = -1,
};

enum G3DTRANSFORMSTATETYPE
{
    G3DTS_VIEW = 0,
    G3DTS_PROJECTION = 1,
    G3DTS_WORLD = 2,
    G3DTS_WORLD1 = 3,
    G3DTS_WORLDCLIP = 4,
    NUM_G3DTRANSFORMSTATETYPE = 5,
    G3DTS_FORCE_DWORD = 2147483647,
};

struct G3DFOG          /* size 0x10 */
{
    float fMin;   /* 0x0 */
    float fMax;   /* 0x4 */
    float fNear;   /* 0x8 */
    float fFar;   /* 0xc */
};

struct G3DFPOINT          /* size 0x8 */
{
    float fX;   /* 0x0 */
    float fY;   /* 0x4 */
};

struct G3DSIZE          /* size 0x8 */
{
    float fWidth;   /* 0x0 */
    float fHeight;   /* 0x4 */
};

struct G3DFRECT          /* size 0x10 */
{
    float fLeft;   /* 0x0 */
    float fTop;   /* 0x4 */
    float fRight;   /* 0x8 */
    float fBottom;   /* 0xc */
};

struct G3DFREGION          /* size 0x10 */
{
    float fLeft;   /* 0x0 */
    float fTop;   /* 0x4 */
    float fWidth;   /* 0x8 */
    float fHeight;   /* 0xc */
};

union G3DINTFLOAT          /* size 0x4 */
{
    int i;   /* 0x0 */
    float f;   /* 0x0 */
};

struct G3D_EMULATE_DIRECTIONALLIGHT_DATA          /* size 0x30 */
{
    XVECTOR vDiffuse;   /* 0x00 */
    XVECTOR vDirection;   /* 0x10 */
    float fLength;   /* 0x20 */
    int aiPad[3];   /* 0x24 */

    struct greater
    {
        bool operator()(const G3D_EMULATE_DIRECTIONALLIGHT_DATA &a,
                        const G3D_EMULATE_DIRECTIONALLIGHT_DATA &b) const
        {
            return a.fLength > b.fLength;
        }
    };
};

typedef void (*LPFUNC_VIEWSCREENMATRIX)(float (*mat)[4], float fScrZ,
                                        float fAspectX, float fAspectY,
                                        float fCenterX, float fCenterY,
                                        float fZmin, float fZmax,
                                        float fNearZ, float fFarZ);
typedef void (*LPFUNC_VIEWCLIPMATRIX)(float (*mat)[4], float fScrZ,
                                      float fAspectX, float fAspectY,
                                      float fNearZ, float fFarZ,
                                      float fClipVolumeX, float fClipVolumeY);

typedef void (*LPFUNC_VU0LOADMATRIX)(float (*mat)[4]);
typedef void (*LPFUNC_VU0APPLYMATRIXWITHOUTTRANS)(float *vDest, float *vSrc);

struct CVu0Matrix
{
    static LPFUNC_VU0LOADMATRIX s_pFuncLoadMatrix;
    static LPFUNC_VU0APPLYMATRIXWITHOUTTRANS s_pFuncApplyMatrixWithoutTrans;

    static void LoadMatrix(float (*mat)[4])
    {
        if (s_pFuncLoadMatrix != 0)
        {
            s_pFuncLoadMatrix(mat);
        }
    }

    static void ApplyWithoutTrans(float *vDest, float *vSrc)
    {
        if (s_pFuncApplyMatrixWithoutTrans != 0)
        {
            s_pFuncApplyMatrixWithoutTrans(vDest, vSrc);
        }
    }
};

enum G3DGSSYNCPATHTIMEOUTREASON
{
    SPTR_D1_START = 0,
    SPTR_D2_START = 1,
    SPTR_VIF1_ACTIVE = 2,
    SPTR_VU0_STAT = 3,
    SPTR_GIF_STAT = 4,
    SPTR_GS_CSR_FINISH = 5,
};

enum G3DVU1MEMADDRESS
{
    MA_VF01 = 1,
    MA_LWMATRIX0_0 = 2,
    MA_LWMATRIX0_1 = 3,
    MA_LWMATRIX0_2 = 4,
    MA_LWMATRIX0_3 = 5,
    MA_LWMATRIX1_0 = 6,
    MA_LWMATRIX1_1 = 7,
    MA_LWMATRIX1_2 = 8,
    MA_LWMATRIX1_3 = 9,
    MA_CAMERAMATRIX0 = 10,
    MA_CAMERAMATRIX1 = 11,
    MA_CAMERAMATRIX2 = 12,
    MA_CAMERAMATRIX3 = 13,
    MA_WSMATRIX0 = 14,
    MA_WSMATRIX1 = 15,
    MA_WSMATRIX2 = 16,
    MA_WSMATRIX3 = 17,
    MA_WCMATRIX0 = 18,
    MA_WCMATRIX1 = 19,
    MA_WCMATRIX2 = 20,
    MA_WCMATRIX3 = 21,
    MA_VERTEX = 22,
    MA_NORMAL = 23,
    MA_DIRCOLAMB0 = 24,
    MA_DIRCOLAMB1 = 25,
    MA_DIRCOLAMB2 = 26,
    MA_DIRCOLDIF0 = 27,
    MA_DIRCOLDIF1 = 28,
    MA_DIRCOLDIF2 = 29,
    MA_DIRCOLSPC0 = 30,
    MA_DIRCOLSPC1 = 31,
    MA_DIRCOLSPC2 = 32,
    MA_POINTCOLAMB0 = 33,
    MA_POINTCOLAMB1 = 34,
    MA_POINTCOLAMB2 = 35,
    MA_POINTCOLDIF0 = 36,
    MA_POINTCOLDIF1 = 37,
    MA_POINTCOLDIF2 = 38,
    MA_POINTCOLSPC0 = 39,
    MA_POINTCOLSPC1 = 40,
    MA_POINTCOLSPC2 = 41,
    MA_SPOTCOLAMB0 = 42,
    MA_SPOTCOLAMB1 = 43,
    MA_SPOTCOLAMB2 = 44,
    MA_SPOTCOLDIF0 = 45,
    MA_SPOTCOLDIF1 = 46,
    MA_SPOTCOLDIF2 = 47,
    MA_SPOTCOLSPC0 = 48,
    MA_SPOTCOLSPC1 = 49,
    MA_SPOTCOLSPC2 = 50,
    MA_LIGHTSTATUS = 51,
    MA_DIRDIR0 = 52,
    MA_DIRDIR1 = 53,
    MA_DIRDIR2 = 54,
    MA_POINTPOS0 = 55,
    MA_POINTPOS1 = 56,
    MA_POINTPOS2 = 57,
    MA_POINTMAXRANGE = 58,
    MA_POINTMAXMININVRANGE = 59,
    MA_SPOTPOS0 = 60,
    MA_SPOTPOS1 = 61,
    MA_SPOTPOS2 = 62,
    MA_SPOTDIR0 = 63,
    MA_SPOTDIR1 = 64,
    MA_SPOTDIR2 = 65,
    MA_SPOTMAXRANGE = 66,
    MA_SPOTMAXMININVRANGE = 67,
    MA_SPOTCOSOUT = 68,
    MA_SPOTCOSINOUTINV = 69,
    MA_MATERIALAMB = 70,
    MA_MATERIALDIF = 71,
    MA_MATERIALSPC = 72,
    MA_MATERIALEMI = 73,
    MA_MATERIALALPHA = 74,
    MA_GLOBALAMBIENT = 75,
    MA_TEMP = 76,
};

struct G3DVU1DIRECTIONALLIGHT          /* size 0x30 */
{
    sceVu0FVECTOR avDirection[3];   /* 0x00 */
};

struct G3DVU1POINTLIGHT          /* size 0x50 */
{
    sceVu0FVECTOR avPosition[3];   /* 0x00 */
    sceVu0FVECTOR vMaxRange;   /* 0x30 */
    sceVu0FVECTOR vMax_Min_InverseRange;   /* 0x40 */
};

struct G3DVU1SPOTLIGHT          /* size 0xa0 */
{
    sceVu0FVECTOR avPosition[3];   /* 0x00 */
    sceVu0FVECTOR avDirection[3];   /* 0x30 */
    sceVu0FVECTOR vMaxRange;   /* 0x60 */
    sceVu0FVECTOR vMax_Min_InverseRange;   /* 0x70 */
    sceVu0FVECTOR vCosOutside;   /* 0x80 */
    sceVu0FVECTOR vCosIn_Out_Inverse;   /* 0x90 */
};

struct G3DVU1COLOR_DIRECTIONAL          /* size 0x90 */
{
    sceVu0FVECTOR avAmbient[3];   /* 0x00 */
    sceVu0FVECTOR avDiffuse[3];   /* 0x30 */
    sceVu0FVECTOR avSpecular[3];   /* 0x60 */
};

struct G3DVU1COLOR_POINT          /* size 0x90 */
{
    sceVu0FVECTOR avAmbient[3];   /* 0x00 */
    sceVu0FVECTOR avDiffuse[3];   /* 0x30 */
    sceVu0FVECTOR avSpecular[3];   /* 0x60 */
};

struct G3DVU1COLOR_SPOT          /* size 0x90 */
{
    sceVu0FVECTOR avAmbient[3];   /* 0x00 */
    sceVu0FVECTOR avDiffuse[3];   /* 0x30 */
    sceVu0FVECTOR avSpecular[3];   /* 0x60 */
};

struct G3DVU1COLOR          /* size 0x1b0 */
{
    G3DVU1COLOR_DIRECTIONAL dir;   /* 0x000 */
    G3DVU1COLOR_POINT point;   /* 0x090 */
    G3DVU1COLOR_SPOT spot;   /* 0x120 */
};

struct G3DVU1COLOR_PHONG          /* size 0x90 */
{
    sceVu0FVECTOR avAmbient[3];   /* 0x00 */
    sceVu0FVECTOR avDiffuse[3];   /* 0x30 */
    sceVu0FVECTOR avSpecular[3];   /* 0x60 */
};

struct G3DVU1COLOR_LAMBERT          /* size 0x60 */
{
    sceVu0FVECTOR avAmbient[3];   /* 0x00 */
    sceVu0FVECTOR avDiffuse[3];   /* 0x30 */
};

struct G3DVU1COLOR_CONSTANT          /* size 0x30 */
{
    sceVu0FVECTOR avAmbient[3];   /* 0x00 */
};

struct G3DVU1CALCULATED          /* size 0x20 */
{
    sceVu0FVECTOR vAmbient;   /* 0x00 */
    sceVu0FVECTOR vMisc;   /* 0x10 */
};

struct G3DVU1CONSTANT          /* size 0x20 */
{
    sceVu0FVECTOR v0001;   /* 0x00 */
    sceVu0FVECTOR v1111;   /* 0x10 */
};

struct G3DVU1FOG          /* size 0x10 */
{
    float fMin;   /* 0x0 */
    float fMax;   /* 0x4 */
    float FA;   /* 0x8 */
    float FB;   /* 0xc */
};

struct G3DVU1GLOBAL          /* size 0x60 */
{
    sceVu0FVECTOR vAmbient;   /* 0x00 */
    sceVu0FVECTOR vTemp;   /* 0x10 */
    sceVu0IVECTOR ivFogColor;   /* 0x20 */
    G3DVU1FOG Fog;   /* 0x30 */
    qword gtPrimitve;   /* 0x40 */
    qword gtVertexBuffer;   /* 0x50 */
};

struct G3DVU1TRANSFORM          /* size 0x160 */
{
    sceVu0FMATRIX matLocalWorld;   /* 0x000 */
    sceVu0FMATRIX matLocalWorld1;   /* 0x040 */
    sceVu0FMATRIX matCamera;   /* 0x080 */
    sceVu0FMATRIX matWorldScreen;   /* 0x0c0 */
    sceVu0FMATRIX matWorldClip;   /* 0x100 */
    sceVu0FVECTOR vVertex;   /* 0x140 */
    sceVu0FVECTOR vNormal;   /* 0x150 */
};

struct G3DVU1LIGHT          /* size 0x130 */
{
    G3DVU1LIGHTSTATUS status;   /* 0x000 */
    G3DVU1DIRECTIONALLIGHT dir;   /* 0x010 */
    G3DVU1POINTLIGHT point;   /* 0x040 */
    G3DVU1SPOTLIGHT spot;   /* 0x090 */
};

struct G3DVU1MEMLAYOUT_DIRECT          /* size 0x6e0 */
{
    sceVu0FVECTOR v0001;   /* 0x000 */
    sceVu0FVECTOR v1111;   /* 0x010 */
    sceVu0FMATRIX matLocalWorld;   /* 0x020 */
    sceVu0FMATRIX matLocalWorld1;   /* 0x060 */
    sceVu0FMATRIX matCamera;   /* 0x0a0 */
    sceVu0FMATRIX matWorldScreen;   /* 0x0e0 */
    sceVu0FMATRIX matWorldClip;   /* 0x120 */
    sceVu0FVECTOR vVertex;   /* 0x160 */
    sceVu0FVECTOR vNormal;   /* 0x170 */
    sceVu0FVECTOR avAmbientDirectional[3];   /* 0x180 */
    sceVu0FVECTOR avDiffuseDirectional[3];   /* 0x1b0 */
    sceVu0FVECTOR avSpecularDirectional[3];   /* 0x1e0 */
    sceVu0FVECTOR avAmbientPoint[3];   /* 0x210 */
    sceVu0FVECTOR avDiffusePoint[3];   /* 0x240 */
    sceVu0FVECTOR avSpecularPoint[3];   /* 0x270 */
    sceVu0FVECTOR avAmbientSpot[3];   /* 0x2a0 */
    sceVu0FVECTOR avDiffuseSpot[3];   /* 0x2d0 */
    sceVu0FVECTOR avSpecularSpot[3];   /* 0x300 */
    G3DVU1LIGHTSTATUS lightstatus;   /* 0x330 */
    sceVu0FVECTOR avDirectionDirectional[3];   /* 0x340 */
    sceVu0FVECTOR avPositionPoint[3];   /* 0x370 */
    sceVu0FVECTOR vMaxRangePoint;   /* 0x3a0 */
    sceVu0FVECTOR vMax_Min_RangeInversePoint;   /* 0x3b0 */
    sceVu0FVECTOR avPositionSpot[3];   /* 0x3c0 */
    sceVu0FVECTOR avDirectionSpot[3];   /* 0x3f0 */
    sceVu0FVECTOR vMaxRangeSpot;   /* 0x420 */
    sceVu0FVECTOR vMax_Min_RangeInverseSpot;   /* 0x430 */
    sceVu0FVECTOR vCosOutsideSpot;   /* 0x440 */
    sceVu0FVECTOR vCosIn_Out_InverseSpot;   /* 0x450 */
    sceVu0FVECTOR vAmbientMaterial;   /* 0x460 */
    sceVu0FVECTOR vDiffuseMaterial;   /* 0x470 */
    sceVu0FVECTOR vSpecularMaterial;   /* 0x480 */
    sceVu0FVECTOR vEmissiveMaterial;   /* 0x490 */
    sceVu0FVECTOR vAlphaMaterial;   /* 0x4a0 */
    sceVu0FVECTOR vAmbientGlobal;   /* 0x4b0 */
    sceVu0FVECTOR vTemp;   /* 0x4c0 */
    sceVu0IVECTOR ivFogColor;   /* 0x4d0 */
    G3DVU1FOG Fog;   /* 0x4e0 */
    qword gtPrimitve;   /* 0x4f0 */
    qword gtVertexBuffer;   /* 0x500 */
    sceVu0FVECTOR vAmbientCalculated;   /* 0x510 */
    sceVu0FVECTOR vMisc;   /* 0x520 */
    sceVu0FVECTOR avAmbientDirectionalOrigin[3];   /* 0x530 */
    sceVu0FVECTOR avDiffuseDirectionalOrigin[3];   /* 0x560 */
    sceVu0FVECTOR avSpecularDirectionalOrigin[3];   /* 0x590 */
    sceVu0FVECTOR avAmbientPointOrigin[3];   /* 0x5c0 */
    sceVu0FVECTOR avDiffusePointOrigin[3];   /* 0x5f0 */
    sceVu0FVECTOR avSpecularPointOrigin[3];   /* 0x620 */
    sceVu0FVECTOR avAmbientSpotOrigin[3];   /* 0x650 */
    sceVu0FVECTOR avDiffuseSpotOrigin[3];   /* 0x680 */
    sceVu0FVECTOR avSpecularSpotOrigin[3];   /* 0x6b0 */
};

struct G3DVU1MEMLAYOUT_PACKED          /* size 0x6e0 */
{
    G3DVU1CONSTANT Constant;   /* 0x000 */
    G3DVU1TRANSFORM Transform;   /* 0x020 */
    G3DVU1COLOR Color;   /* 0x180 */
    G3DVU1LIGHT Light;   /* 0x330 */
    G3DMATERIAL Material;   /* 0x460 */
    G3DVU1GLOBAL Global;   /* 0x4b0 */
    G3DVU1CALCULATED Calc;   /* 0x510 */
    G3DVU1COLOR ColorOrigin;   /* 0x530 */
};

union G3DVU1MEMLAYOUT          /* size 0x6e0 */
{
    G3DVU1MEMLAYOUT_PACKED Packed;   /* 0x000 */
    G3DVU1MEMLAYOUT_DIRECT Direct;   /* 0x000 */
};

enum GRA3DLIGHTID
{
    LID_DIRECTIONAL_0 = 0,
    LID_DIRECTIONAL_1 = 1,
    LID_DIRECTIONAL_2 = 2,
    LID_POINT_0 = 3,
    LID_POINT_1 = 4,
    LID_POINT_2 = 5,
    LID_POINT_3 = 6,
    LID_POINT_4 = 7,
    LID_POINT_5 = 8,
    LID_POINT_6 = 9,
    LID_POINT_7 = 10,
    LID_POINT_8 = 11,
    LID_POINT_9 = 12,
    LID_POINT_10 = 13,
    LID_POINT_11 = 14,
    LID_POINT_12 = 15,
    LID_POINT_13 = 16,
    LID_POINT_14 = 17,
    LID_POINT_15 = 18,
    LID_POINT_FLASHLIGHT_0 = 19,
    LID_POINT_FLASHLIGHT_1 = 20,
    LID_POINT_SELFREFLECTION = 21,
    LID_SPOT_0 = 22,
    LID_SPOT_1 = 23,
    LID_SPOT_2 = 24,
    LID_SPOT_3 = 25,
    LID_SPOT_4 = 26,
    LID_SPOT_5 = 27,
    LID_SPOT_6 = 28,
    LID_SPOT_7 = 29,
    LID_SPOT_8 = 30,
    LID_SPOT_9 = 31,
    LID_SPOT_10 = 32,
    LID_SPOT_11 = 33,
    LID_SPOT_12 = 34,
    LID_SPOT_13 = 35,
    LID_SPOT_14 = 36,
    LID_SPOT_15 = 37,
    LID_SPOT_FLASHLIGHT = 38,
    NUM_GRA3DLIGHTID = 39,
    GRA3D_START_LIGHT_DIRECTIONAL = 0,
    GRA3D_END_LIGHT_DIRECTIONAL = 2,
    GRA3D_NUM_LIGHT_DIRECTIONAL = 3,
    GRA3D_START_LIGHT_POINT = 3,
    GRA3D_END_LIGHT_POINT = 21,
    GRA3D_NUM_LIGHT_POINT = 19,
    GRA3D_NUM_LIGHT_POINT_STATIC = 16,
    GRA3D_START_LIGHT_SPOT = 22,
    GRA3D_END_LIGHT_SPOT = 38,
    GRA3D_NUM_LIGHT_SPOT = 17,
    GRA3D_NUM_LIGHT_SPOT_STATIC = 16,
    INVALID_GRA3DLIGHTID = 2147483647,
    GRA3DLIGHTID_FORCE_DWORD = 2147483647,
};

struct GRA3DCAMERA          /* size 0x1e0 */
{
    float fFov;   /* 0x000 */
    float fNearZ;   /* 0x004 */
    float fFarZ;   /* 0x008 */
    float fAspectX;   /* 0x00c */
    float fAspectY;   /* 0x010 */
    float fCenterX;   /* 0x014 */
    float fCenterY;   /* 0x018 */
    float fZmin;   /* 0x01c */
    float fZmax;   /* 0x020 */
    G3DCAMPROJECTIONTYPE type;   /* 0x024 */
    int aiPad[2];   /* 0x028 */
    float vTarget[4];   /* 0x030 */
    float vPositionOld[4];   /* 0x040 */
    float vTargetOld[4];   /* 0x050 */
    float matViewClipPolygon[4][4];   /* 0x060 */
    float matViewClipObject[4][4];   /* 0x0a0 */
    float matWorldScreen[4][4];   /* 0x0e0 */
    float matWorldClipPolygon[4][4];   /* 0x120 */
    float matWorldClipObject[4][4];   /* 0x160 */
    float matCoord[4][4];   /* 0x1a0 */
};

struct GRA3DMATERIALINDEXCACHE          /* size 0x10 */
{
    int bEnable;   /* 0x0 */
    int aiIndex[3];   /* 0x4 */
};

struct CoordCache          /* size 0x2c */
{
    int cache_on;   /* 0x00 */
    int edge_check;   /* 0x04 */
    int cn0;   /* 0x08 */
    GRA3DMATERIALINDEXCACHE Point;   /* 0x0c */
    GRA3DMATERIALINDEXCACHE Spot;   /* 0x1c */
};

struct GRA3DLIGHTSTATUS          /* size 0x10 */
{
    int bEnable;   /* 0x0 */
    int bEnableToChar;   /* 0x4 */
    int bEnableToShadow;   /* 0x8 */
    int bEmulateToDirectionalLight;   /* 0xc */
};

struct GRA3DLIGHTDATA          /* size 0x13a0 */
{
    float vAmbient[4];   /* 0x0000 */
    fixed_array<G3DLIGHT,NUM_GRA3DLIGHTID> aLight;   /* 0x0010 */
    fixed_array<GRA3DLIGHTSTATUS,NUM_GRA3DLIGHTID> aStatus;   /* 0x1120 */
    fixed_array<int,3> aiNumInitial;   /* 0x1390 */
    int aiPad[1];   /* 0x139c */
};

enum GRA3DTRANSFORMSTATETYPE
{
    GRA3DTS_VIEW = 0,
    GRA3DTS_PROJECTION = 1,
    GRA3DTS_WORLD = 2,
    GRA3DTS_WORLD1 = 3,
    GRA3DTS_WORLDCLIP = 4,
    GRA3DTS_WORLDSCREEN = 5,
    NUM_GRA3DTRANSFORMSTATETYPE = 6,
    GRA3DTS_FORCE_DWORD = 2147483647,
};

enum GRA3DVU1MEMADDRESS
{
    GRA3DVU1MEM_TOP = 0,
    GRA3DVU1MEM_VF01 = 0,
    GRA3DVU1MEM_VF02 = 1,
    GRA3DVU1MEM_DBADDRESS = 2,
    GRA3DVU1MEM_GTTRISTRIP_NOTEX = 3,
    GRA3DVU1MEM_GTTRISTRIP_TEX = 4,
    GRA3DVU1MEM_GTEOP = 5,
    GRA3DVU1MEM_GTTRIFAN_NOTEX = 6,
    GRA3DVU1MEM_GTTRIFAN_TEX = 7,
    GRA3DVU1MEM_WSMATRIX0 = 8,
    GRA3DVU1MEM_WSMATRIX1 = 9,
    GRA3DVU1MEM_WSMATRIX2 = 10,
    GRA3DVU1MEM_WSMATRIX3 = 11,
    GRA3DVU1MEM_WCMATRIX0 = 12,
    GRA3DVU1MEM_WCMATRIX1 = 13,
    GRA3DVU1MEM_WCMATRIX2 = 14,
    GRA3DVU1MEM_WCMATRIX3 = 15,
    GRA3DVU1MEM_LWMATRIX0 = 16,
    GRA3DVU1MEM_LWMATRIX1 = 17,
    GRA3DVU1MEM_LWMATRIX2 = 18,
    GRA3DVU1MEM_LWMATRIX3 = 19,
    GRA3DVU1MEM_LWMATRIXNONORMALIZED0 = 20,
    GRA3DVU1MEM_LWMATRIXNONORMALIZED1 = 21,
    GRA3DVU1MEM_LWMATRIXNONORMALIZED2 = 22,
    GRA3DVU1MEM_LWMATRIXNONORMALIZED3 = 23,
    GRA3DVU1MEM_FOG = 24,
    GRA3DVU1MEM_DIRLIGHTDIF0 = 25,
    GRA3DVU1MEM_DIRLIGHTDIF1 = 26,
    GRA3DVU1MEM_DIRLIGHTDIF2 = 27,
    GRA3DVU1MEM_DIRLIGHTSPC0 = 28,
    GRA3DVU1MEM_DIRLIGHTSPC1 = 29,
    GRA3DVU1MEM_DIRLIGHTSPC2 = 30,
    GRA3DVU1MEM_SPOTPOS0 = 31,
    GRA3DVU1MEM_SPOTPOS1 = 32,
    GRA3DVU1MEM_SPOTPOS2 = 33,
    GRA3DVU1MEM_SPOTINTENS = 34,
    GRA3DVU1MEM_SPOTINTENSB = 35,
    GRA3DVU1MEM_SPOTLIGHTDIF0 = 36,
    GRA3DVU1MEM_SPOTLIGHTDIF1 = 37,
    GRA3DVU1MEM_SPOTLIGHTDIF2 = 38,
    GRA3DVU1MEM_SPOTLIGHTSPC0 = 39,
    GRA3DVU1MEM_SPOTLIGHTSPC1 = 40,
    GRA3DVU1MEM_SPOTLIGHTSPC2 = 41,
    GRA3DVU1MEM_POINTPOS0 = 42,
    GRA3DVU1MEM_POINTPOS1 = 43,
    GRA3DVU1MEM_POINTPOS2 = 44,
    GRA3DVU1MEM_EYEVECTOR = 45,
    GRA3DVU1MEM_GLOBALAMBIENT = 46,
    GRA3DVU1MEM_DIRCOLDIF0 = 47,
    GRA3DVU1MEM_DIRCOLDIF1 = 48,
    GRA3DVU1MEM_DIRCOLDIF2 = 49,
    GRA3DVU1MEM_DIRCOLSPC0 = 50,
    GRA3DVU1MEM_DIRCOLSPC1 = 51,
    GRA3DVU1MEM_DIRCOLSPC2 = 52,
    GRA3DVU1MEM_SPOTBTIMES = 53,
    GRA3DVU1MEM_SPOTCOLDIF0 = 54,
    GRA3DVU1MEM_SPOTCOLDIF1 = 55,
    GRA3DVU1MEM_SPOTCOLDIF2 = 56,
    GRA3DVU1MEM_SPOTCOLSPC0 = 57,
    GRA3DVU1MEM_SPOTCOLSPC1 = 58,
    GRA3DVU1MEM_SPOTCOLSPC2 = 59,
    GRA3DVU1MEM_POINTBTIMES = 60,
    GRA3DVU1MEM_POINTCOLDIF0 = 61,
    GRA3DVU1MEM_POINTCOLDIF1 = 62,
    GRA3DVU1MEM_POINTCOLDIF2 = 63,
    GRA3DVU1MEM_POINTCOLSPC0 = 64,
    GRA3DVU1MEM_POINTCOLSPC1 = 65,
    GRA3DVU1MEM_POINTCOLSPC2 = 66,
    GRA3DVU1MEM_COORDMATRIX0 = 67,
    GRA3DVU1MEM_COORDMATRIX1 = 68,
    GRA3DVU1MEM_COORDMATRIX2 = 69,
    GRA3DVU1MEM_COORDMATRIX3 = 70,
    GRA3DVU1MEM_MAPSHADOW_VF01 = 0,
    GRA3DVU1MEM_MAPSHADOW_VF02 = 1,
    GRA3DVU1MEM_MAPSHADOW_DBADDRESS = 2,
    GRA3DVU1MEM_MAPSHADOW_OFFSETDATA = 3,
    GRA3DVU1MEM_MAPSHADOW_GTTRISTRIP = 4,
    GRA3DVU1MEM_MAPSHADOW_GTTRIFAN = 5,
    GRA3DVU1MEM_MAPSHADOW_LSMATRIX0 = 8,
    GRA3DVU1MEM_MAPSHADOW_LSMATRIX1 = 9,
    GRA3DVU1MEM_MAPSHADOW_LSMATRIX2 = 10,
    GRA3DVU1MEM_MAPSHADOW_LSMATRIX3 = 11,
    GRA3DVU1MEM_MAPSHADOW_LCMATRIX0 = 12,
    GRA3DVU1MEM_MAPSHADOW_LCMATRIX1 = 13,
    GRA3DVU1MEM_MAPSHADOW_LCMATRIX2 = 14,
    GRA3DVU1MEM_MAPSHADOW_LCMATRIX3 = 15,
    GRA3DVU1MEM_MAPSHADOW_LIPMATRIX0 = 19,
    GRA3DVU1MEM_MAPSHADOW_LIPMATRIX1 = 20,
    GRA3DVU1MEM_MAPSHADOW_LIPMATRIX2 = 21,
    GRA3DVU1MEM_MAPSHADOW_LIPMATRIX3 = 22,
    GRA3DVU1MEM_MAPSHADOW_SHADOWCOLOR = 23,
    GRA3DVU1MEM_MAPSHADOW_FOGDATA = 24,
    GRA3DVU1MEM_DBBASE = 96,
    GRA3DVU1MEM_DBEND = 1024,
};

struct GRA3DVU1LIGHTDATA_DIRECTIONAL          /* size 0x60 */
{
    LMATRIX lmDiffuse;   /* 0x00 */
    LMATRIX lmSpecular;   /* 0x30 */
};

struct GRA3DVU1LIGHTDATA_POINT          /* size 0x40 */
{
    LMATRIX lmPosition;   /* 0x00 */
    float _vDirectionInverse[4];   /* 0x30 */
};

struct GRA3DVU1LIGHTDATA_SPOT          /* size 0xb0 */
{
    LMATRIX lmPosition;   /* 0x00 */
    float vIntens[4];   /* 0x30 */
    float vIntensB[4];   /* 0x40 */
    LMATRIX lmDirection;   /* 0x50 */
    LMATRIX lmSpecular;   /* 0x80 */
};

struct GRA3DVU1LIGHTDATA          /* size 0x150 */
{
    GRA3DVU1LIGHTDATA_DIRECTIONAL dir;   /* 0x000 */
    GRA3DVU1LIGHTDATA_SPOT spot;   /* 0x060 */
    GRA3DVU1LIGHTDATA_POINT point;   /* 0x110 */
};

struct GRA3DVU1MATERIALDATA_POINT          /* size 0x70 */
{
    float vPower[4];   /* 0x00 */
    LMATRIX lmDiffuse;   /* 0x10 */
    LMATRIX lmSpecular;   /* 0x40 */
};

struct GRA3DVU1MATERIALDATA_SPOT          /* size 0x70 */
{
    float vPower[4];   /* 0x00 */
    LMATRIX lmDiffuse;   /* 0x10 */
    LMATRIX lmSpecular;   /* 0x40 */
};

struct GRA3DVU1MATERIALDATA_DIRECTIONAL          /* size 0x70 */
{
    float vAmbient[4];   /* 0x00 */
    LMATRIX lmDiffuse;   /* 0x10 */
    LMATRIX lmSpecular;   /* 0x40 */
};

struct GRA3DVU1MATERIALCACHE_POINT          /* size 0x80 */
{
    GRA3DVU1MATERIALDATA_POINT Data;   /* 0x00 */
    GRA3DMATERIALINDEXCACHE Index;   /* 0x70 */
};

struct GRA3DVU1MATERIALCACHE_SPOT          /* size 0x80 */
{
    GRA3DVU1MATERIALDATA_SPOT Data;   /* 0x00 */
    GRA3DMATERIALINDEXCACHE Index;   /* 0x70 */
};

struct GRA3DVU1MATERIALDATA          /* size 0x150 */
{
    GRA3DVU1MATERIALDATA_DIRECTIONAL dir;   /* 0x000 */
    GRA3DVU1MATERIALDATA_SPOT spot;   /* 0x070 */
    GRA3DVU1MATERIALDATA_POINT point;   /* 0x0e0 */
};

/* --------------------------------------------------------------------------
 *  Host-renderer view of the VU1 light image.
 *
 *  Everything the four microprograms in vu1/ actually read while lighting a
 *  vertex, already de-interleaved into one float4 per light so a GPU shader
 *  does not have to repeat CalcIntens' MR32 transpose.  All colours are in GS
 *  0..255 units, with the 128/192/255/43/86 scales gra3dCalcVu1MaterialData*()
 *  applies already folded in.
 *
 *  This is NOT the same encoding as g3dCore.c's G3DVU1MEMLAYOUT_DIRECT, which
 *  is the VU0/prelight image (fMaxRange + 1/(max-min), cos(outside) +
 *  1/(cosIn-cosOut)) that g3dCalcVertexColor() consumes.  The two lighting
 *  models genuinely differ; see vu1/LIGHTING.md.
 * ------------------------------------------------------------------------ */
/* --------------------------------------------------------------------------
 *  GRA3DVU1LANE                                              [PORT ADDITION]
 *
 *  One positional light, derived exactly as the VU1's three-lane image derives
 *  its own, but without the three-lane cap.  gra3dCalcVu1WideLanes() fills an
 *  array of these from gra3d's whole bank; the renderer copies them straight
 *  into its per-pixel light block.
 *
 *  vParams is the packing the shader wants: x bTimes, y cos^2(cone half-angle),
 *  z 1/sin^2, w unused.  Point lanes leave y and z zero and never read them.
 *  fRank is scratch for the over-budget displacement in the builder and is not
 *  uploaded.
 * ------------------------------------------------------------------------ */
struct GRA3DVU1LANE
{
    float vPosition[4];
    float vDirection[4];
    float vColDif[4];
    float vColSpc[4];
    float vParams[4];
    float fRank;
};

struct GRA3DVU1LIGHTSNAPSHOT          /* size 0x270, 39 x float4 */
{
    /* x = light-type enables, bit 0 spot / bit 1 point (VU mem 5 .y/.z, which
     *     gra3dVu1TransGTEOP() stamps into the EOP GIFtag).
     * y = flags, bit 0 "lighting enabled".
     * zw reserved. */
    int   aiConfig[4];                          /* 0x000 */

    /* GLOBALAMBIENT (VU 46): xyz is the ambient term, w is the 255.0 the
     * microcode's single closing MINIw clamps against. */
    float vAmbient[4];                          /* 0x010 */

    float vSpotBTimes[4];                       /* 0x020  VU 53 */
    float vSpotIntens[4];                       /* 0x030  VU 34, cos^2(theta) */
    float vSpotIntensB[4];                      /* 0x040  VU 35, 1/sin^2(theta) */
    float vPointBTimes[4];                      /* 0x050  VU 60 */

    /* Directional.  The direction pair is taken from gra3d's own world-space
     * matrices rather than the packet's, which _SetVu1LightData_Directional
     * has already rotated into the object's local frame -- dotting a
     * world-space direction against a world-space normal is the same product
     * and saves the shader a second basis. */
    float avDirLightDif[3][4];                  /* 0x060  VU 25..27, per light */
    float avDirLightSpc[3][4];                  /* 0x090  VU 28..30, half-vector */
    float avDirColDif[3][4];                    /* 0x0c0  VU 47..49 */
    float avDirColSpc[3][4];                    /* 0x0f0  VU 50..52 */

    float avSpotPos[3][4];                      /* 0x120  VU 31..33 */
    float avSpotDir[3][4];                      /* 0x150  VU 36..38 */
    float avSpotColDif[3][4];                   /* 0x180  VU 54..56 */
    float avSpotColSpc[3][4];                   /* 0x1b0  VU 57..59 */

    float avPointPos[3][4];                     /* 0x1e0  VU 42..44 */
    float avPointColDif[3][4];                  /* 0x210  VU 61..63 */
    float avPointColSpc[3][4];                  /* 0x240  VU 64..66 */
};

struct GRA3DVU1LIGHTPACKET          /* size 0x160 */
{
    qword qwVif1Code;   /* 0x000 */
    GRA3DVU1LIGHTDATA Data;   /* 0x010 */
};

struct GRA3DVU1MATERIALPACKET_DIRECTIONAL          /* size 0x80 */
{
    qword qwVif1Code;   /* 0x00 */
    GRA3DVU1MATERIALDATA_DIRECTIONAL Data;   /* 0x10 */
};

struct GRA3DVU1MATERIALPACKET_POINT          /* size 0x80 */
{
    qword qwVif1Code;   /* 0x00 */
    GRA3DVU1MATERIALDATA_POINT Data;   /* 0x10 */
};

struct GRA3DVU1MATERIALPACKET_SPOT          /* size 0x80 */
{
    qword qwVif1Code;   /* 0x00 */
    GRA3DVU1MATERIALDATA_SPOT Data;   /* 0x10 */
};

struct GRA3DVU1DBADDRESS          /* size 0x10 */
{
    unsigned int uiContext0;   /* 0x0 */
    unsigned int uiContext1;   /* 0x4 */
    unsigned int auiPad[2];   /* 0x8 */
};

struct GRA3DVU1TRANSFORMDATA          /* size 0x100 */
{
    float matWorldScreen[4][4];   /* 0x00 */
    float matWorldClip[4][4];   /* 0x40 */
    float matLocalWorld[4][4];   /* 0x80 */
    float matLocalWorldNoNormalized[4][4];   /* 0xc0 */
};

struct GRA3DVU1CONSTDATA          /* size 0x80 */
{
    float _vVF1[4];   /* 0x00 */
    float _vVF2[4];   /* 0x10 */
    GRA3DVU1DBADDRESS DBAddress;   /* 0x20 */
    qword gtTRISTRIP_NOTEXTURE;   /* 0x30 */
    qword gtTRISTRIP_TEXTURE;   /* 0x40 */
    SCEGIFTAG_EOP gtEOP;   /* 0x50 */
    qword gtTRIFAN_NOTEXTURE;   /* 0x60 */
    qword gtTRIFAN_TEXTURE;   /* 0x70 */
};

struct GRA3DVU1MEMLAYOUT_DIRECT          /* size 0x430 */
{
    float _vVF1[4];   /* 0x000 */
    float _vVF2[4];   /* 0x010 */
    GRA3DVU1DBADDRESS DBAddress;   /* 0x020 */
    qword gtTRISTRIP_NOTEXTURE;   /* 0x030 */
    qword gtTRISTRIP_TEXTURE;   /* 0x040 */
    SCEGIFTAG_EOP gtEOP;   /* 0x050 */
    qword gtTRIFAN_NOTEXTURE;   /* 0x060 */
    qword gtTRIFAN_TEXTURE;   /* 0x070 */
    float matWorldScreen[4][4];   /* 0x080 */
    float matWorldClip[4][4];   /* 0x0c0 */
    float matLocalWorld[4][4];   /* 0x100 */
    float matLocalWorldNoNormalized[4][4];   /* 0x140 */
    G3DVU1FOG Fog;   /* 0x180 */
    LMATRIX lmatDiffuse_Directional;   /* 0x190 */
    LMATRIX lmatSpecular_Directional;   /* 0x1c0 */
    float vPosition_Spot0[4];   /* 0x1f0 */
    float vPosition_Spot1[4];   /* 0x200 */
    float vPosition_Spot2[4];   /* 0x210 */
    float vIntension_Spot[4];   /* 0x220 */
    float vIntensionB_Spot[4];   /* 0x230 */
    LMATRIX lmatDiffuse_Spot;   /* 0x240 */
    LMATRIX lmatSpecular_Spot;   /* 0x270 */
    float vPosition_Point0[4];   /* 0x2a0 */
    float vPosition_Point1[4];   /* 0x2b0 */
    float vPosition_Point2[4];   /* 0x2c0 */
    float _vEyeDirection_Point[4];   /* 0x2d0 */
    float vAmbient_Directional[4];   /* 0x2e0 */
    LMATRIX lmatDiffuse_Directional_Material;   /* 0x2f0 */
    LMATRIX lmatSpecular_Directional_Material;   /* 0x320 */
    float vBTimes_Spot[4];   /* 0x350 */
    LMATRIX lmatDiffuse_Spot_Material;   /* 0x360 */
    LMATRIX lmatSpecular_Spot_Material;   /* 0x390 */
    float vBTimes_Point[4];   /* 0x3c0 */
    LMATRIX lmatDiffuse_Point_Material;   /* 0x3d0 */
    LMATRIX lmatSpecular_Point_Material;   /* 0x400 */
};

struct GRA3DVU1MEMLAYOUT_PACKED          /* size 0x430 */
{
    GRA3DVU1CONSTDATA Const;   /* 0x000 */
    GRA3DVU1TRANSFORMDATA Transform;   /* 0x080 */
    G3DVU1FOG Fog;   /* 0x180 */
    GRA3DVU1LIGHTDATA Light;   /* 0x190 */
    GRA3DVU1MATERIALDATA Material;   /* 0x2e0 */
};

union GRA3DVU1MEMLAYOUT          /* size 0x430 */
{
    GRA3DVU1MEMLAYOUT_DIRECT Direct;   /* 0x000 */
    GRA3DVU1MEMLAYOUT_PACKED Packed;   /* 0x000 */
};

struct GRA3DVU1MEMLAYOUT_MAPSHADOW_DIRECT          /* size 0x190 */
{
    float _vf01[4];   /* 0x000 */
    float _vf02[4];   /* 0x010 */
    GRA3DVU1DBADDRESS DataAddress;   /* 0x020 */
    float vOffsetData[4];   /* 0x030 */
    qword gtTRISTRIP;   /* 0x040 */
    qword gtTRIFAN;   /* 0x050 */
    qword qw0x06;   /* 0x060 */
    qword qw0x07;   /* 0x070 */
    float matLocalScreen[4][4];   /* 0x080 */
    float matLocalClip[4][4];   /* 0x0c0 */
    qword qw0x10;   /* 0x100 */
    qword qw0x11;   /* 0x110 */
    qword qw0x12;   /* 0x120 */
    float matLIP[4][4];   /* 0x130 */
    sceVu0IVECTOR ivColor;   /* 0x170 */
    G3DVU1FOG Fog;   /* 0x180 */
};

union GRA3DVU1MEMLAYOUT_MAPSHADOW          /* size 0x190 */
{
    GRA3DVU1MEMLAYOUT_MAPSHADOW_DIRECT Direct;   /* 0x000 */
};

struct GRA3DEMULATIONLIGHTDATACREATIONDATA          /* size 0x40 */
{
    float vStaticDirLightColor[4];   /* 0x00 */
    float fAngleScale;   /* 0x10 */
    float fDiffuseScale;   /* 0x14 */
    float fMaplightScale;   /* 0x18 */
    int bEnableSelfreflection;   /* 0x1c */
    int bEmulateSelfreflection;   /* 0x20 */
    int bEnableFlashlight;   /* 0x24 */
    int bEmulateFlashlight;   /* 0x28 */
    int bEnableFlashlight2;   /* 0x2c */
    int bEmulateFlashlight2;   /* 0x30 */
    int bEnableStaticDirLight;   /* 0x34 */
};

struct G3DVIF1CODE_STCYCLE          /* size 0x4 */
{
    unsigned int CL : 8;   /* 0x0:0 */
    unsigned int WL : 8;   /* 0x1:0 */
    unsigned int NUM : 8;   /* 0x2:0 */
    unsigned int CMD : 7;   /* 0x3:0 */
    unsigned int irq : 1;   /* 0x3:7 */
};

struct G3DVIF1CODE_OFFSET          /* size 0x4 */
{
    unsigned int OFFSET : 10;   /* 0x0:0 */
    unsigned int __ : 6;   /* 0x1:2 */
    unsigned int NUM : 8;   /* 0x2:0 */
    unsigned int CMD : 7;   /* 0x3:0 */
    unsigned int irq : 1;   /* 0x3:7 */
};

struct G3DVIF1CODE_BASE          /* size 0x4 */
{
    unsigned int BASE : 8;   /* 0x0:0 */
    unsigned int __ : 8;   /* 0x1:0 */
    unsigned int NUM : 8;   /* 0x2:0 */
    unsigned int CMD : 7;   /* 0x3:0 */
    unsigned int irq : 1;   /* 0x3:7 */
};

struct G3DVIF1CODE_ITOP          /* size 0x4 */
{
    unsigned int ADDR : 8;   /* 0x0:0 */
    unsigned int __ : 8;   /* 0x1:0 */
    unsigned int NUM : 8;   /* 0x2:0 */
    unsigned int CMD : 7;   /* 0x3:0 */
    unsigned int irq : 1;   /* 0x3:7 */
};

struct G3DVIF1CODE_STMOD          /* size 0x4 */
{
    unsigned int MODE : 2;   /* 0x0:0 */
    unsigned int __ : 14;   /* 0x0:2 */
    unsigned int NUM : 8;   /* 0x2:0 */
    unsigned int CMD : 7;   /* 0x3:0 */
    unsigned int irq : 1;   /* 0x3:7 */
};

struct G3DVIF1CODE_MARK          /* size 0x4 */
{
    unsigned int MARK : 16;   /* 0x0:0 */
    unsigned int NUM : 8;   /* 0x2:0 */
    unsigned int CMD : 7;   /* 0x3:0 */
    unsigned int irq : 1;   /* 0x3:7 */
};

struct G3DVIF1CODE_DIRECT          /* size 0x4 */
{
    unsigned int size : 16;   /* 0x0:0 */
    unsigned int num : 8;   /* 0x2:0 */
    unsigned int cmd : 7;   /* 0x3:0 */
    unsigned int irq : 1;   /* 0x3:7 */
};

struct G3DVIF1CODE_UNPACK          /* size 0x4 */
{
    unsigned int ADDR : 10;   /* 0x0:0 */
    unsigned int pad : 4;   /* 0x1:2 */
    unsigned int USN : 1;   /* 0x1:6 */
    unsigned int FLG : 1;   /* 0x1:7 */
    unsigned int NUM : 8;   /* 0x2:0 */
    unsigned int CMD : 8;   /* 0x3:0 */
};

union G3DVIF1CODE          /* size 0x4 */
{
    G3DVIF1CODE_STCYCLE stcycle;   /* 0x0 */
    G3DVIF1CODE_OFFSET offset;   /* 0x0 */
    G3DVIF1CODE_BASE base;   /* 0x0 */
    G3DVIF1CODE_ITOP itop;   /* 0x0 */
    G3DVIF1CODE_STMOD stmod;   /* 0x0 */
    G3DVIF1CODE_MARK mark;   /* 0x0 */
    G3DVIF1CODE_DIRECT direct;   /* 0x0 */
    G3DVIF1CODE_UNPACK unpack;   /* 0x0 */
};

struct G3DVIF1REGISTERLAYOUT_DIRECT          /* size 0x58 */
{
    tVIF1_STAT stat;   /* 0x00 */
    tVIF1_FBRST fbrst;   /* 0x04 */
    tVIF1_ERR err;   /* 0x08 */
    tVIF_MARK mark;   /* 0x0c */
    tVIF_CYCLE cycle;   /* 0x10 */
    tVIF_MODE mode;   /* 0x14 */
    tVIF1_NUM num;   /* 0x18 */
    tVIF_MASK mask;   /* 0x1c */
    tVIF_CODE code;   /* 0x20 */
    tVIF_ITOPS itop;   /* 0x24 */
    tVIF1_BASE base;   /* 0x28 */
    tVIF1_OFST ofst;   /* 0x2c */
    tVIF1_TOPS tops;   /* 0x30 */
    int _aiReserved0[1];   /* 0x34 */
    tVIF_R0 r0;   /* 0x38 */
    tVIF_R1 r1;   /* 0x3c */
    tVIF_R2 r2;   /* 0x40 */
    tVIF_R3 r3;   /* 0x44 */
    tVIF_C0 c0;   /* 0x48 */
    tVIF_C1 c1;   /* 0x4c */
    tVIF_C2 c2;   /* 0x50 */
    tVIF_C3 c3;   /* 0x54 */
};

union G3DVIF1REGISTERLAYOUT          /* size 0x58 */
{
    G3DVIF1REGISTERLAYOUT_DIRECT Direct;   /* 0x00 */
    G3DVIF1REGISTERLAYOUT_ARRAY Array;   /* 0x00 */
};

struct G3DVIF1CMDDATA          /* size 0x14 */
{
    unsigned int uiCmd;   /* 0x00 */
    unsigned int auiSubPacket[4];   /* 0x04 */
};

/* Maximum number of sub-packet words a single VIF1 register command carries. */
#define G3D_MAX_VIF1SUBPACKETLENGTH  4

/* Lookup-table entry describing one settable VIF1 register: the opcode, the
 * GS/VIF address of the "substantial" (hardware) register, how many sub-packet
 * words it consumes, and the local-shadow word(s) in s_Vif1RegisterLayout it
 * maps onto. */
struct _VIF1CMDINFO          /* size 0x1c */
{
    unsigned int uiCmd;   /* 0x00 */
    qword *pqwSubstantialRegister;   /* 0x04 */
    int iLengthSubPacket;   /* 0x08 */
    int *apiLocalRegister[4];   /* 0x0c */
};

/* Generic DIRECT/PACKED-A+D register-write packet built on the fly by
 * g3dSetGsRegisters: a VIF1 code quadword, a GIFTAG and a run of A+D items. */
struct _PACKET          /* size 0x30 */
{
    qword         qwVif1Code;   /* 0x00 */
    SCEGIFTAG_EOP gt;           /* 0x10 */
    sceGifPackAd  aGPA[1];      /* 0x20 */
};

/* Pre-built DMA packet that writes a single GS register via a DIRECT GIFTAG
 * A+D item; g3dDmaSetGsRegister patches gpa and copies it into the chain. */
struct _PACKET_SETGSREGISTER          /* size 0x30 */
{
    qword qwVif1Code;   /* 0x00 */
    SCEGIFTAG_EOP GT;   /* 0x10 */
    sceGifPackAd gpa;   /* 0x20 */
};

struct SGDVUVNDATA          /* size 0x20 */
{
    qword qwVif1Code;   /* 0x00 */
    unsigned int uiVTop;   /* 0x10 */
    unsigned int uiPTop;   /* 0x14 */
    unsigned int uiWTop;   /* 0x18 */
    unsigned int uiNumMesh;   /* 0x1c */
};

struct _SGDVUMESHCOLORDATA          /* size 0x10 */
{
    G3DVIF1CODE_UNPACK VifUnpack;   /* 0x0 */
    VECTOR3 avColor[1];             /* 0x4 */
};

struct SGDMESHVERTEXDATA_TYPE2          /* size 0x18 */
{
    VECTOR3 vVertex;   /* 0x00 */
    VECTOR3 vNormal;   /* 0x0c */
};

struct SGDMESHVERTEXDATA_TYPE2F          /* size 0xc */
{
    VECTOR3 avNormal[1];   /* 0x0 */
};

struct SGDVUVNDATA_PRESET          /* size 0x40 */
{
    unsigned int aui[10];   /* 0x00 */
    /* 0x28 */ union { // 0x18
        SGDMESHVERTEXDATA_TYPE2 avt2[1];   /* 0x28 */
        SGDMESHVERTEXDATA_TYPE2F vt2f;   /* 0x28 */
    };
};

struct SGDVUVNDATA_WEIGHTED          /* size 0x20 */
{
    unsigned char auc0[28];   /* 0x00 */
    unsigned char ucBoneId0;   /* 0x1c */
    unsigned char ucBoneId1;   /* 0x1d */
    unsigned char auc1[2];   /* 0x1e */
};

struct SGDVUMESHDATA          /* size 0x20 */
{
    qword qwVif1Code;   /* 0x00 */
    qword GifTag;   /* 0x10 */
};

struct SGDVUMESHDATA_PRESET          /* size 0x18 */
{
    short int asPad0[2];   /* 0x00 */
    short int sOffsetToST;   /* 0x04 */
    short int sOffsetToPrim;   /* 0x06 */
    int aiPad1[2];   /* 0x08 */
    long int alData[1];   /* 0x10 */
};

struct SGDGSIMAGEDATA          /* size 0x30 */
{
    unsigned int auiVifCode[4];   /* 0x00 */
    qword GT;   /* 0x10 */
    unsigned char aucData[1];   /* 0x20 */
};

struct SGDLIGHTDATA_DIRECTIONAL          /* size 0x20 */
{
    float vColor[4];   /* 0x00 */
    float vDirection[4];   /* 0x10 */
};

struct SGDLIGHTDATA_POINT          /* size 0x20 */
{
    float vColor[4];   /* 0x00 */
    float vPosition[4];   /* 0x10 */
};

struct SGDLIGHTDATA_SPOT          /* size 0x30 */
{
    float vColor[4];   /* 0x00 */
    float vPosition[4];   /* 0x10 */
    float vTarget[4];   /* 0x20 */
};

struct SGDLIGHTDATA_AMBIENT          /* size 0x10 */
{
    float vColor[4];   /* 0x0 */
};

union SGDPROCUNITDATA          /* size 0x80 */
{
    SGDVUVNDATA VUVNData;   /* 0x00 */
    SGDVUMESHDATA VUMeshData;   /* 0x00 */
    SGDVUVNDATA_PRESET VUVNData_Preset;   /* 0x00 */
    SGDVUMESHDATA_PRESET VUMeshData_Preset;   /* 0x00 */
    unsigned char aucGSImage;   /* 0x00 */
    float avBB[8][4];   /* 0x00 */
    SGDLIGHTDATA_DIRECTIONAL alightDirectional[1];   /* 0x00 */
    SGDLIGHTDATA_POINT alightPoint[1];   /* 0x00 */
    SGDLIGHTDATA_SPOT alightSpot[1];   /* 0x00 */
    SGDLIGHTDATA_AMBIENT lightAmbient;   /* 0x00 */
    SGDGSIMAGEDATA GSImage;   /* 0x00 */
};

struct GRA3DSGDCREATIONDATA          /* size 0x8 */
{
    sceVu0FVECTOR *vnarray;   /* 0x0 */
    int size;   /* 0x4 */
};

struct GRA3DSCRATCHPADLAYOUT          /* size 0x440 */
{
    qword qwVif1Code0;   /* 0x000 */
    GRA3DVU1MEMLAYOUT Vu1Mem;   /* 0x010 */
};

struct GRA3DSCRATCHPADLAYOUT_MAPSHADOW          /* size 0x1a0 */
{
    qword qwVif1Code;   /* 0x000 */
    GRA3DVU1MEMLAYOUT_MAPSHADOW Vu1Mem;   /* 0x010 */
};

struct GRA3DSHADOWCREATIONDATA          /* size 0x4 */
{
    GRA3DSCRATCHPADLAYOUT_MAPSHADOW *pSL;   /* 0x0 */
};

struct GRA3DSHADOWDEBUG          /* size 0x1c */
{
    int bDrawShadowModelBB;   /* 0x00 */
    int bDrawCastShadowOnBB;   /* 0x04 */
    int bDrawLightDir;   /* 0x08 */
    int bTextureMapEnable;   /* 0x0c */
    int bFogEnable;   /* 0x10 */
    int bDrawCharShadow;   /* 0x14 */
    int bDrawObjectShadow;   /* 0x18 */
};

struct G3DGSREGISTERLAYOUT_DIRECT          /* size 0x400 */
{
    sceGsPrim gsPrim;   /* 0x000 */
    sceGsRgbaq gsRgbaq;   /* 0x008 */
    sceGsSt gsSt;   /* 0x010 */
    sceGsUv gsUv;   /* 0x018 */
    sceGsXyzf gsXyzf2;   /* 0x020 */
    sceGsXyzf gsXyz2;   /* 0x028 */
    sceGsTex0 gsTex0_1;   /* 0x030 */
    sceGsTex0 gsTex0_2;   /* 0x038 */
    sceGsClamp gsClamp_1;   /* 0x040 */
    sceGsClamp gsClamp_2;   /* 0x048 */
    sceGsFog gsFog;   /* 0x050 */
    long int gsBlank0x0b;   /* 0x058 */
    sceGsXyzf gsXyzf3;   /* 0x060 */
    sceGsXyzf gsXyz3;   /* 0x068 */
    long int gsBlank0x0e;   /* 0x070 */
    long int gsBlank0x0f;   /* 0x078 */
    long int gsBlank0x10;   /* 0x080 */
    long int gsBlank0x11;   /* 0x088 */
    long int gsBlank0x12;   /* 0x090 */
    long int gsBlank0x13;   /* 0x098 */
    sceGsTex1 gsTex1_1;   /* 0x0a0 */
    sceGsTex1 gsTex1_2;   /* 0x0a8 */
    sceGsTex2 gsTex2_1;   /* 0x0b0 */
    sceGsTex2 gsTex2_2;   /* 0x0b8 */
    sceGsXyoffset gsXyoffset_1;   /* 0x0c0 */
    sceGsXyoffset gsXyoffset_2;   /* 0x0c8 */
    sceGsPrmodecont gsPrmodecont;   /* 0x0d0 */
    sceGsPrmode gsPrmode;   /* 0x0d8 */
    sceGsTexclut gsTexclut;   /* 0x0e0 */
    long int gsBlank0x1d;   /* 0x0e8 */
    long int gsBlank0x1e;   /* 0x0f0 */
    long int gsBlank0x1f;   /* 0x0f8 */
    long int gsBlank0x20;   /* 0x100 */
    long int gsBlank0x21;   /* 0x108 */
    sceGsScanmsk gsScanmsk;   /* 0x110 */
    long int gsBlank0x23;   /* 0x118 */
    long int gsBlank0x24;   /* 0x120 */
    long int gsBlank0x25;   /* 0x128 */
    long int gsBlank0x26;   /* 0x130 */
    long int gsBlank0x27;   /* 0x138 */
    long int gsBlank0x28;   /* 0x140 */
    long int gsBlank0x29;   /* 0x148 */
    long int gsBlank0x2a;   /* 0x150 */
    long int gsBlank0x2b;   /* 0x158 */
    long int gsBlank0x2c;   /* 0x160 */
    long int gsBlank0x2d;   /* 0x168 */
    long int gsBlank0x2e;   /* 0x170 */
    long int gsBlank0x2f;   /* 0x178 */
    long int gsBlank0x30;   /* 0x180 */
    long int gsBlank0x31;   /* 0x188 */
    long int gsBlank0x32;   /* 0x190 */
    long int gsBlank0x33;   /* 0x198 */
    sceGsMiptbp1 gsMiptbp1_1;   /* 0x1a0 */
    sceGsMiptbp1 gsMiptbp1_2;   /* 0x1a8 */
    sceGsMiptbp2 gsMiptbp2_1;   /* 0x1b0 */
    sceGsMiptbp2 gsMiptbp2_2;   /* 0x1b8 */
    long int gsBlank0x38;   /* 0x1c0 */
    long int gsBlank0x39;   /* 0x1c8 */
    long int gsBlank0x3a;   /* 0x1d0 */
    sceGsTexa gsTexa;   /* 0x1d8 */
    long int gsBlank0x3c;   /* 0x1e0 */
    sceGsFogcol gsFogcol;   /* 0x1e8 */
    long int gsBlank0x3e;   /* 0x1f0 */
    sceGsTexflush gsTexflush;   /* 0x1f8 */
    sceGsScissor gsScissor_1;   /* 0x200 */
    sceGsScissor gsScissor_2;   /* 0x208 */
    sceGsAlpha gsAlpha_1;   /* 0x210 */
    sceGsAlpha gsAlpha_2;   /* 0x218 */
    sceGsDimx gsDimx;   /* 0x220 */
    sceGsDthe gsDthe;   /* 0x228 */
    sceGsColclamp gsColclamp;   /* 0x230 */
    sceGsTest gsTest_1;   /* 0x238 */
    sceGsTest gsTest_2;   /* 0x240 */
    sceGsPabe gsPabe;   /* 0x248 */
    sceGsFba gsFba_1;   /* 0x250 */
    sceGsFba gsFba_2;   /* 0x258 */
    sceGsFrame gsFrame_1;   /* 0x260 */
    sceGsFrame gsFrame_2;   /* 0x268 */
    sceGsZbuf gsZbuf_1;   /* 0x270 */
    sceGsZbuf gsZbuf_2;   /* 0x278 */
    sceGsBitbltbuf gsBitbltbuf;   /* 0x280 */
    sceGsTrxpos gsTrxpos;   /* 0x288 */
    sceGsTrxreg gsTrxreg;   /* 0x290 */
    sceGsTrxdir gsTrxdir;   /* 0x298 */
    sceGsHwreg gsHwreg;   /* 0x2a0 */
    long int gsBlank0x55;   /* 0x2a8 */
    long int gsBlank0x56;   /* 0x2b0 */
    long int gsBlank0x57;   /* 0x2b8 */
    long int gsBlank0x58;   /* 0x2c0 */
    long int gsBlank0x59;   /* 0x2c8 */
    long int gsBlank0x5a;   /* 0x2d0 */
    long int gsBlank0x5b;   /* 0x2d8 */
    long int gsBlank0x5c;   /* 0x2e0 */
    long int gsBlank0x5d;   /* 0x2e8 */
    long int gsBlank0x5e;   /* 0x2f0 */
    long int gsBlank0x5f;   /* 0x2f8 */
    sceGsSignal gsSignal;   /* 0x300 */
    sceGsFinish gsFinish;   /* 0x308 */
    sceGsLabel gsLabel;   /* 0x310 */
    long int gsBlank0x63;   /* 0x318 */
    long int gsBlank0x64;   /* 0x320 */
    long int gsBlank0x65;   /* 0x328 */
    long int gsBlank0x66;   /* 0x330 */
    long int gsBlank0x67;   /* 0x338 */
    long int gsBlank0x68;   /* 0x340 */
    long int gsBlank0x69;   /* 0x348 */
    long int gsBlank0x6a;   /* 0x350 */
    long int gsBlank0x6b;   /* 0x358 */
    long int gsBlank0x6c;   /* 0x360 */
    long int gsBlank0x6d;   /* 0x368 */
    long int gsBlank0x6e;   /* 0x370 */
    long int gsBlank0x6f;   /* 0x378 */
    long int gsBlank0x70;   /* 0x380 */
    long int gsBlank0x71;   /* 0x388 */
    long int gsBlank0x72;   /* 0x390 */
    long int gsBlank0x73;   /* 0x398 */
    long int gsBlank0x74;   /* 0x3a0 */
    long int gsBlank0x75;   /* 0x3a8 */
    long int gsBlank0x76;   /* 0x3b0 */
    long int gsBlank0x77;   /* 0x3b8 */
    long int gsBlank0x78;   /* 0x3c0 */
    long int gsBlank0x79;   /* 0x3c8 */
    long int gsBlank0x7a;   /* 0x3d0 */
    long int gsBlank0x7b;   /* 0x3d8 */
    long int gsBlank0x7c;   /* 0x3e0 */
    long int gsBlank0x7d;   /* 0x3e8 */
    long int gsBlank0x7e;   /* 0x3f0 */
    long int gsNop;   /* 0x3f8 */
};

union G3DGSREGISTERLAYOUT          /* size 0x400 */
{
    G3DGSREGISTERLAYOUT_DIRECT Direct;   /* 0x000 */
    G3DGSREGISTERLAYOUT_ARRAY Array;   /* 0x000 */
};

struct G3DRESOURCE          /* size 0xc */
{
    CTexture *apTexture[1];   /* 0x0 */
    IG3DVertexBuffer *pVertexBuffer;   /* 0x4 */
    IG3DIndexBuffer *pIndexBuffer;   /* 0x8 */
};

struct G3DCOREOBJECT          /* size 0x1170 */
{
    unsigned int auiRenderState[8];   /* 0x0000 */
    unsigned int auiGlobalState[2];   /* 0x0020 */
    _LIGHTDATA aLightData[9];   /* 0x0030 */
    float vAmbient[4];   /* 0x04b0 */
    G3DMATERIAL Material;   /* 0x04c0 */
    sceVu0FMATRIX amatTransform[5];   /* 0x0510 */
    G3DVIEWPORT Viewport;   /* 0x0650 */
    G3DWINDOW Window;   /* 0x0670 */
    G3DRESOURCE Resource;   /* 0x0680 */
    G3DVU1MEMLAYOUT Vu1Mem;   /* 0x0690 */
    G3DGSREGISTERLAYOUT GsRegister;   /* 0x0d70 */
};

struct G3DCREATIONDATA          /* size 0x4 */
{
    G3DCOREOBJECT *pObj;   /* 0x0 */
};

struct G3DDMACHAINTAG          /* size 0x10 */
{
    long unsigned int QWC : 16;   /* 0x0:0 */
    long unsigned int pad0 : 10;   /* 0x2:0 */
    long unsigned int PCE : 2;   /* 0x3:2 */
    long unsigned int ID : 3;   /* 0x3:4 */
    long unsigned int IRQ : 1;   /* 0x3:7 */
    long unsigned int ADDR : 31;   /* 0x4:0 */
    long unsigned int SPR : 1;   /* 0x7:7 */
    long unsigned int pad1;   /* 0x8 */
};

struct G3DDMACREATIONDATA          /* size 0x8 */
{
    unsigned int uiBufferTop;   /* 0x0 */
    unsigned int uiBufferSize;   /* 0x4 */
};

enum GRA3DBOUNDINGBOXVERTEXINDEX
{
    BBVI_MMM = 0,
    BBVI_PMM = 1,
    BBVI_MPM = 2,
    BBVI_PPM = 3,
    BBVI_MMP = 4,
    BBVI_PMP = 5,
    BBVI_MPP = 6,
    BBVI_PPP = 7,
    BBVI_CENTER = 8,
    NUM_GRA3DBOUNDINGBOXVERTEXINDEX = 9,
};

enum G3DBOUNINGVOLUMETYPE
{
    BVT_BOX = 0,
    BVT_SPHERE = 1,
    BVT_ELLIPSE = 2,
    BVT_COLUMN = 3,
};

enum G3DRESOURCETYPE
{
    G3DRTYPE_TEXTURE = 1,
    G3DRTYPE_VERTEXBUFFER = 2,
    G3DRTYPE_INDEXBUFFER = 3,
    INVALID_G3DRESOURCETYPE = 2147483647,
    G3DRTYPE_FORCE_DWORD = 2147483647,
};

struct G3DTEXTUREDATA_LONG          /* size 0x10 */
{
    long int lTex0;   /* 0x0 */
    long int lTex1;   /* 0x8 */
};

struct G3DTEXTUREDATA_GS          /* size 0x10 */
{
    sceGsTex0 gsTex0;   /* 0x0 */
    sceGsTex1 gsTex1;   /* 0x8 */
};

union G3DTEXTUREDATA          /* size 0x10 */
{
    G3DTEXTUREDATA_LONG l;   /* 0x0 */
    G3DTEXTUREDATA_GS gs;   /* 0x0 */
};

struct G3DSPRITEDATA          /* size 0x30 */
{
    G3DFRECT Rect;   /* 0x00 */
    G3DCOLOR Color;   /* 0x10 */
    float fZ;   /* 0x14 */
    sceGsSt StLT;   /* 0x18 */
    sceGsSt StRB;   /* 0x20 */
};

struct RENDERTARGETCREATIONDATA          /* size 0x10 */
{
    sceGsTex0 gsTex0;   /* 0x0 */
    G3DCOLOR ClearColor;   /* 0x8 */
    float fZMax;   /* 0xc */
};

struct G3DLIGHTMANAGE          /* size 0x270 */
{
    fixed_array<GRA3DLIGHTSTATUS,NUM_GRA3DLIGHTID> aStatus;   /* 0x000 */
};

struct SGDVUMESHTEXGIFTAG          /* size 0x28 */
{
    unsigned int auiVifCode[2];   /* 0x00 */
    unsigned int auiGifTag[4];   /* 0x08 */
    long unsigned int aulData[2];   /* 0x18 */
};

struct SGDVUMESHPOINTNUMREGSET          /* size 0x8 */
{
    unsigned int auiVifCode[2];   /* 0x0 */
};

struct SGDVUMESHPOINTNUM          /* size 0x8 */
{
    G3DVIF1CODE_UNPACK VifUnpack;   /* 0x0 */
    unsigned int uiPointNum;   /* 0x4 */
};

struct SGDVUMESHSTREGSET          /* size 0xc */
{
    unsigned int auiVifCode[3];   /* 0x0 */
};

struct SGDVUMESHST          /* size 0x8 */
{
    float fS;   /* 0x0 */
    float fT;   /* 0x4 */
};

struct SGDVUMESHSTDATA          /* size 0xc */
{
    G3DVIF1CODE_UNPACK VifUnpack;   /* 0x0 */
    SGDVUMESHST astData[1];   /* 0x4 */
};

struct BoundLine          /* size 0x8 */
{
    int s;   /* 0x0 */
    int e;   /* 0x4 */
};

#endif /* _GRA3DTYPES_H */
