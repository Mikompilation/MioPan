/* ==========================================================================
 *  gra3dConst.c
 *
 *  The graph3d engine's file-scope constant data: the canonical basis /
 *  permutation vectors (g_v1000 .. g_v1111), the named debug colours, the
 *  identity / VU / scaled identity matrices, the null material and null light,
 *  the screen-image <-> playstation-image conversion vectors/matrices and the
 *  default camera.  g_uiMustBeSetValue is the sentinel the build's link-time
 *  "must be initialised" check keys its global-constructor list on.
 *
 *  The compiler emits a translation-unit static copy of the inlined
 *  ctl/fixed_array.h template helpers (_fixed_array_assert /
 *  _fixed_array_verifyrange<T>) and the file-scope object constructors
 *  (__static_initialization_and_destruction_0 / "global constructors keyed to
 *  g_uiMustBeSetValue") for the writable g_xv0000 / g_xmatConvert* objects;
 *  those are not hand-written and are represented here as the natural
 *  file-scope object definitions.
 *
 *  Reconstructed from the Feb 6 2004 prototype (SLES_523.84).
 * ======================================================================== */

#include "gra3dConst.h"         /* our own declarations (checked against the definitions) */
#include "eetypes.h"
#include "g3dMath.h"            /* XVECTOR / XMATRIX */
#include "g3dLight.h"           /* G3DLIGHT / G3DLIGHTTYPE */
#include "gra3dTypes.h"         /* G3DCOLOR, G3DMATERIAL, GRA3DCAMERA */

/* --------------------------------------------------------------------------
 *  Link-time "must be initialised" sentinel.  The C++ global-constructor list
 *  for this translation unit is keyed to this object.
 * ------------------------------------------------------------------------ */
unsigned int g_uiMustBeSetValue = 0xdeadbeef;

/* --------------------------------------------------------------------------
 *  Named debug colours (G3DCOLOR is 0xAABBGGRR).
 * ------------------------------------------------------------------------ */
G3DCOLOR g_colWhite   = 0xffffffff;
G3DCOLOR g_colBlack   = 0xff000000;
G3DCOLOR g_colGray    = 0xff808080;
G3DCOLOR g_colRed     = 0xff0000ff;
G3DCOLOR g_colGreen   = 0xff00ff00;
G3DCOLOR g_colBlue    = 0xffff0000;
G3DCOLOR g_colSkyblue = 0xffffff00;
G3DCOLOR g_colPurple  = 0xffff00ff;
G3DCOLOR g_colYellow  = 0xff00ffff;

/* --------------------------------------------------------------------------
 *  Canonical xyzw vectors.  The four-digit suffix gives the component values
 *  (g_v1001 = {1,0,0,1}); g_v111_1 = {1,1,1,-1}.
 * ------------------------------------------------------------------------ */
float g_v1000[4]  = { 1.0f, 0.0f, 0.0f, 0.0f };
float g_v0100[4]  = { 0.0f, 1.0f, 0.0f, 0.0f };
float g_v0010[4]  = { 0.0f, 0.0f, 1.0f, 0.0f };
float g_v1001[4]  = { 1.0f, 0.0f, 0.0f, 1.0f };
float g_v0101[4]  = { 0.0f, 1.0f, 0.0f, 1.0f };
float g_v0011[4]  = { 0.0f, 0.0f, 1.0f, 1.0f };
float g_v0111[4]  = { 0.0f, 1.0f, 1.0f, 1.0f };
float g_v1011[4]  = { 1.0f, 0.0f, 1.0f, 1.0f };
float g_v1101[4]  = { 1.0f, 1.0f, 0.0f, 1.0f };
float g_v0110[4]  = { 0.0f, 1.0f, 1.0f, 0.0f };
float g_v1010[4]  = { 1.0f, 0.0f, 1.0f, 0.0f };
float g_v1100[4]  = { 1.0f, 1.0f, 0.0f, 0.0f };
float g_v0001[4]  = { 0.0f, 0.0f, 0.0f, 1.0f };
float g_v1110[4]  = { 1.0f, 1.0f, 1.0f, 0.0f };
float g_v1111[4]  = { 1.0f, 1.0f, 1.0f, 1.0f };
float g_v0000[4]  = { 0.0f, 0.0f, 0.0f, 0.0f };
float g_v111_1[4] = { 1.0f, 1.0f, 1.0f, -1.0f };

/* Writable zero vector (initialised by the file-scope constructor). */
XVECTOR g_xv0000;

/* --------------------------------------------------------------------------
 *  Identity matrices.  g_VUmatUnit is the VU-form identity (the columns held
 *  in the order the VU1 transform code expects); g_matUnitScaled is identity
 *  scaled by 25 (the screen-image -> playstation-image scale).
 * ------------------------------------------------------------------------ */
float g_matUnit[4][4] =
{
    { 1.0f, 0.0f, 0.0f, 0.0f },
    { 0.0f, 1.0f, 0.0f, 0.0f },
    { 0.0f, 0.0f, 1.0f, 0.0f },
    { 0.0f, 0.0f, 0.0f, 1.0f },
};

float g_VUmatUnit[4][4] =
{
    { 0.0f, 0.0f, 0.0f, 1.0f },
    { 0.0f, 0.0f, 1.0f, 0.0f },
    { 0.0f, 1.0f, 0.0f, 0.0f },
    { 1.0f, 0.0f, 0.0f, 0.0f },
};

/* --------------------------------------------------------------------------
 *  Null material / null light (everything zero except homogeneous w / power /
 *  falloff and the invalid-type tag).
 * ------------------------------------------------------------------------ */
G3DMATERIAL g_NullMaterial =
{
    { 0.0f, 0.0f, 0.0f, 0.0f },                 /* vDiffuse  */
    { 0.0f, 0.0f, 0.0f, 0.0f },                 /* vAmbient  */
    { 0.0f, 0.0f, 0.0f, 0.0f },                 /* vSpecular */
    { 0.0f, 0.0f, 0.0f, 0.0f },                 /* vEmissive */
    1.0f,                                       /* fPower    */
    { 0, 0, 0 },                                /* aiPad     */
};

G3DLIGHT g_NullLight =
{
    { 0.0f, 0.0f, 0.0f, 0.0f },                     /* vDiffuse        */
    { 0.0f, 0.0f, 0.0f, 0.0f },                    /* vSpecular       */
    { 0.0f, 0.0f, 0.0f, 0.0f },                    /* vAmbient        */
    { 0.0f, 0.0f, 0.0f, 1.0f },                     /* vPosition       */
    { 0.0f, 0.0f, 0.0f, 0.0f },                    /* vDirection      */
    G3DLIGHTTYPE_FORCE_DWORD,                            /* Type            */
    0.0f,                                           /* fAngleInside    */
    0.0f,                                          /* fAngleOutside   */
    0.0f,                                            /* fMaxRange       */
    0.0f,                                            /* fMinRange       */
    1.0f,                                               /* fFalloff        */
    { 0.0f, 0.0f },                                  /* afPad0          */
};

float g_matUnitScaled[4][4] =
{
    { 25.0f,  0.0f,  0.0f, 0.0f },
    {  0.0f, 25.0f,  0.0f, 0.0f },
    {  0.0f,  0.0f, 25.0f, 0.0f },
    {  0.0f,  0.0f,  0.0f, 1.0f },
};

/* --------------------------------------------------------------------------
 *  Screen-image <-> playstation-image conversion (the SI2PS scale is 25, the
 *  PS2SI scale is 1/25 = 0.04; y and z are mirrored).
 * ------------------------------------------------------------------------ */
float g_vConvertSI2PS[4] = { 25.0f, -25.0f, -25.0f, 1.0f };
float g_vConvertPS2SI[4] = { 0.04f, -0.04f, -0.04f, 1.0f };

float g_matConvertSI2PS[4][4] =
{
    { 25.0f,   0.0f,   0.0f, 0.0f },
    {  0.0f, -25.0f,   0.0f, 0.0f },
    {  0.0f,   0.0f, -25.0f, 0.0f },
    {  0.0f,   0.0f,   0.0f, 1.0f },
};

float g_matConvertPS2SI[4][4] =
{
    { 0.04f,   0.0f,   0.0f, 0.0f },
    { 0.0f,  -0.04f,   0.0f, 0.0f },
    { 0.0f,    0.0f, -0.04f, 0.0f },
    { 0.0f,    0.0f,   0.0f, 1.0f },
};

/* Writable XMATRIX conversion forms (initialised by the file-scope ctor). */
XMATRIX g_xmatConvertSI2PS;
XMATRIX g_xmatConvertPS2SI;

/* --------------------------------------------------------------------------
 *  Default camera: ~44 degree fov, near 0.1, far 65535, 4:3.5 aspect, the GS
 *  framebuffer centre (2048,2048), perspective projection, identity matrices.
 * ------------------------------------------------------------------------ */
GRA3DCAMERA g_CameraDefault =
{
    0.7683706879615784f,                        /* fFov     */
    0.1f,                                       /* fNearZ   */
    65535.0f,                                   /* fFarZ    */
    1.0f,                                        /* fAspectX */
    0.875f,                                      /* fAspectY */
    2048.0f,                                     /* fCenterX */
    2048.0f,                                     /* fCenterY */
    0.0f,                                        /* fZmin    */
    16777215.0f,                                 /* fZmax    */
    PT_PERSPECTIVE,                              /* type     */
    { 0, 0 },                                    /* aiPad    */
    { 0.0f, 0.0f, 0.0f, 1.0f },                 /* vTarget      */
    { 0.0f, 0.0f, 0.0f, 1.0f },                 /* vPositionOld */
    { 0.0f, 0.0f, 0.0f, 1.0f },                 /* vTargetOld   */
    {                                            /* matViewClipPolygon */
        { 1.0f, 0.0f, 0.0f, 0.0f },
        { 0.0f, 1.0f, 0.0f, 0.0f },
        { 0.0f, 0.0f, 1.0f, 0.0f },
        { 0.0f, 0.0f, 0.0f, 1.0f },
    },
    {                                            /* matViewClipObject */
        { 1.0f, 0.0f, 0.0f, 0.0f },
        { 0.0f, 1.0f, 0.0f, 0.0f },
        { 0.0f, 0.0f, 1.0f, 0.0f },
        { 0.0f, 0.0f, 0.0f, 1.0f },
    },
    {                                            /* matWorldScreen */
        { 1.0f, 0.0f, 0.0f, 0.0f },
        { 0.0f, 1.0f, 0.0f, 0.0f },
        { 0.0f, 0.0f, 1.0f, 0.0f },
        { 0.0f, 0.0f, 0.0f, 1.0f },
    },
    {                                            /* matWorldClipPolygon */
        { 1.0f, 0.0f, 0.0f, 0.0f },
        { 0.0f, 1.0f, 0.0f, 0.0f },
        { 0.0f, 0.0f, 1.0f, 0.0f },
        { 0.0f, 0.0f, 0.0f, 1.0f },
    },
    {                                            /* matWorldClipObject */
        { 1.0f, 0.0f, 0.0f, 0.0f },
        { 0.0f, 1.0f, 0.0f, 0.0f },
        { 0.0f, 0.0f, 1.0f, 0.0f },
        { 0.0f, 0.0f, 0.0f, 1.0f },
    },
    {                                            /* matCoord */
        { 1.0f, 0.0f, 0.0f, 0.0f },
        { 0.0f, 1.0f, 0.0f, 0.0f },
        { 0.0f, 0.0f, 1.0f, 0.0f },
        { 0.0f, 0.0f, 0.0f, 1.0f },
    },
};

/* --------------------------------------------------------------------------
 *  File-scope object constructor (compiler-generated).
 *
 *  __static_initialization_and_destruction_0 / "global constructors keyed to
 *  g_uiMustBeSetValue" initialise the writable .data objects above:
 *
 *      g_xv0000          = {0, 0, 0, 0};
 *      g_xmatConvertSI2PS = diag(25, -25, -25, 1);
 *      g_xmatConvertPS2SI = diag(0.04, -0.04, -0.04, 1);
 *
 *  They are emitted by the compiler from those definitions and are not
 *  reproduced here as hand-written functions.
 * ------------------------------------------------------------------------ */
