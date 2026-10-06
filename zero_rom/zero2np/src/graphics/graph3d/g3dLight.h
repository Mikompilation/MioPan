/* ==========================================================================
 *  g3dLight.h
 *
 *  Light primitives for the g3d core: the light-type enum, the 0x70-byte
 *  G3DLIGHT record, the SGD light-type alias, and the lighting helpers
 *  implemented in g3dLight.c.
 *
 *  Reconstructed from the Feb 6 2004 prototype (SLES_523.84).
 * ======================================================================== */

#ifndef _G3DLIGHT_H
#define _G3DLIGHT_H

#include <libvu0.h>             /* sceVu0FVECTOR */

/* G3D_EMULATE_DIRECTIONALLIGHT_DATA is defined in gra3dTypes.h (which includes
 * this header); the g3dLightEx prototypes below only use it by pointer. */
struct G3D_EMULATE_DIRECTIONALLIGHT_DATA;

/* Maximum number of source lights g3dBlendLight / the N-way blend accept. */
#define G3D_MAX_BLENDLIGHT      6

enum G3DLIGHTTYPE
{
    G3DLIGHT_DIRECTIONAL  = 0,
    G3DLIGHT_POINT        = 1,
    G3DLIGHT_SPOT         = 2,
    NUM_G3DLIGHTTYPE      = 3,
    G3DLIGHT_AMBIENT      = 3,
    INVALID_G3DLIGHTTYPE  = 2147483647,
    G3DLIGHTTYPE_FORCE_DWORD = 2147483647
};

/* SGD reuses the g3d light-type enum on disc. */
typedef G3DLIGHTTYPE SGDLIGHTTYPE;

struct G3DLIGHT                                 /* 0x70 */
{
    sceVu0FVECTOR vDiffuse;                     /* 0x00 */
    sceVu0FVECTOR vSpecular;                    /* 0x10 */
    sceVu0FVECTOR vAmbient;                     /* 0x20 */
    sceVu0FVECTOR vPosition;                    /* 0x30 */
    sceVu0FVECTOR vDirection;                   /* 0x40 */
    G3DLIGHTTYPE  Type;                         /* 0x50 */
    float         fAngleInside;                 /* 0x54 */
    float         fAngleOutside;                /* 0x58 */
    float         fMaxRange;                    /* 0x5c */
    float         fMinRange;                    /* 0x60 */
    float         fFalloff;                     /* 0x64 */
    float         afPad0[2];                    /* 0x68 */
};

/* --------------------------------------------------------------------------
 *  g3dutilCopyLight
 *
 *  Whole-record light copy.  Inline in the original header, which is why its
 *  assert strings ("g3dutilCopyLight", "sizeof( G3DLIGHT ) == 112") sit in the
 *  .rodata of every object that uses it -- scene.o among them, where it is
 *  expanded at three call sites.  The size assert folds away at compile time,
 *  so no check is emitted; the ROM's body is seven lq/sq quadword copies, i.e.
 *  a plain 112-byte structure copy.
 * ------------------------------------------------------------------------ */
inline void g3dutilCopyLight(G3DLIGHT *pDst, const G3DLIGHT *pSrc)      /* 56 */
{
    *pDst = *pSrc;                                                      /* 61 */
}

/* Light slot as stored inside the g3d core object (G3DLIGHT + enable flag). */
struct _LIGHTDATA                               /* 0x80 */
{
    G3DLIGHT L;                                 /* 0x00 */
    int      bEnable;                           /* 0x70 */
};

/* VU1 light-status word; the union lets the engine load it as one long. */
struct G3DVU1LIGHTACTIVITYSTATUS                /* 0x4 */
{
    unsigned int uiEnableDir0   : 1;            /* 0x0:0 */
    unsigned int uiEnableDir1   : 1;
    unsigned int uiEnableDir2   : 1;
    unsigned int _uiPad0        : 1;
    unsigned int uiEnablePoint0 : 1;
    unsigned int uiEnablePoint1 : 1;
    unsigned int uiEnablePoint2 : 1;
    unsigned int _uiPad1        : 1;
    unsigned int uiEnableSpot0  : 1;            /* 0x1:0 */
    unsigned int uiEnableSpot1  : 1;
    unsigned int uiEnableSpot2  : 1;
    unsigned int _uiPad2        : 1;
    unsigned int _uiPad         : 4;
};

struct G3DVU1LIGHTSTATUS                        /* 0x10 */
{
    unsigned int auiPad[2];                     /* 0x0 */
    union                                       /* 0x8 */
    {
        G3DVU1LIGHTACTIVITYSTATUS as;
        long int                  lAS;
    };
};

/* Per-light-type "does this light reach the box" predicate (g3dLightEx.c). */
typedef int (*LPFUNC_ISBBLIGHTINGUP)(G3DLIGHT *pLight, float (*avBB)[4]);

/* Sort record produced by g3dSortLightForBoundingBoxByPowerOrder: a light, its
 * power reaching the box, and its original index.  Sorted by descending power
 * via the nested `greater` comparator (a std::binary_function). */
struct LIGHTCOMPAREDATA                          /* 0xc */
{
    float     fPower;                            /* 0x0 */
    G3DLIGHT *pLight;                            /* 0x4 */
    int       iIndex;                            /* 0x8 */
    struct greater
    {
        bool operator()(const LIGHTCOMPAREDATA &a, const LIGHTCOMPAREDATA &b) const
        {
            return a.fPower > b.fPower;
        }
    };
};

/* ---- g3dLight.c -------------------------------------------------------- */
void  g3dSetLightStatus(G3DVU1LIGHTSTATUS *pLS);
void  g3dutilSetLightDefault(G3DLIGHT *pLight, G3DLIGHTTYPE iLightType);
float g3dCalcSpotlightFalloff(G3DLIGHT *pLight, float *vVertexPosition);
float g3dCalcLightDistanceAttenuation(G3DLIGHT *pLight, float *vVertexPosition);
float g3dCalcLightAttenuation(G3DLIGHT *pLight, float *vVertex);

#endif /* _G3DLIGHT_H */
