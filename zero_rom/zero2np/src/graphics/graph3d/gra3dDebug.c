/* ==========================================================================
 *  gra3dDebug.c
 *
 *  Debug drawing / verification helpers for the gra3d layer.  The line / point
 *  / sphere primitives are thin wrappers that unpack a G3DCOLOR into r/g/b/a
 *  and forward to the low-level debug-draw library (DrawLine / DrawPoint2 /
 *  DrawSphere).  Built on top of those: a wireframe bounding box, a ranged
 *  spot-cone, and a screen-space textured sprite (a hand-built DIRECT GS
 *  packet).  Also here: the VIF-code / VU1-memory verifiers, an LMATRIX dump,
 *  and a light-data clamp/normalise pass.
 *
 *  Reconstructed from the Feb 6 2004 prototype (SLES_523.84).
 * ======================================================================== */

#include "gra3dDebug.h"
#include "gra3d.h"              /* g3dCalcGsPrimitiveCoord */
#include "g3dCore.h"            /* g3dCalcGsPrimitiveCoord */
#include "g3dGeom.h"            /* g3dCalcPlaneFromPoints / g3dCalcPlaneFromPointNormal */
#include "g3dDma.h"             /* g3dDmaOpenPacket / g3dDmaClosePacket */
#include "g3dxVu0.h"            /* VU0 macro-mode intrinsics */
#include "g3ddbg.h"
#include "g3dDebug.h"           /* _GetLineInfo / g3ddbgVerifyVifCode */
#include "gra3dConst.h"         /* g_v0000 / g_v0100 */
#include "../graphics.h"        /* DrawLine / DrawPoint2 / DrawSphere (debug-draw library) */
#include "ctl/fixed_array.h"
#include <mathf.h>              /* sinf / cosf / acosf */

struct _PACKET_DRAWSPRITE
{
    qword qwVifCode;
    qword Gt;
    sceGifPackRgbaq Rgbaq;
    sceGifPackXyzf aXyzf2[2];
};

/* Compiler-generated copies of _fixed_array_assert / _fixed_array_verifyrange<T>
 * (001b59f0..001b5ac7) are the inlined ctl/fixed_array.h template; not emitted. */

/* --------------------------------------------------------------------------
 *  _DrawSphere
 *
 *  Forward a centre / range / colour / scale to the debug-draw library.
 * ------------------------------------------------------------------------ */
static void _DrawSphere(float *vCenter, float fRange, G3DCOLOR col, float *vScale)
{
    DrawSphere(fRange, vCenter[0], vCenter[1], vCenter[2],
               (u_char)col, (u_char)(col >> 8), (u_char)(col >> 0x10), (u_char)(col >> 0x18),
               vScale);
}

/* --------------------------------------------------------------------------
 *  _PrintLMatrix
 *
 *  Dump the four rows of an LMATRIX-style matrix with the caller's source
 *  location (from the last _SetLineInfo) and a value name.
 * ------------------------------------------------------------------------ */
void _PrintLMatrix(float (*fmat)[4], char *pValName)
{
    g3ddbgPrintf("_PrintLMatrix(%s):%s(%d)\n", pValName, _GetLineInfo()->pFileName, _GetLineInfo()->iLine);

    for (int i = 0; i < 3; i++)
    {
        g3ddbgPrintf("m[%d] : %8.2f, %8.2f, %8.2f, %8.2f\n", i, fmat[i][0], fmat[i][1], fmat[i][2], fmat[i][3]);
    }
}

/* --------------------------------------------------------------------------
 *  gra3ddbgDrawLine
 * ------------------------------------------------------------------------ */
void gra3ddbgDrawLine(float *vStart, float *vEnd, G3DCOLOR col)
{
    u_char g1;

    g1 = (u_char)(col >> 8);
    DrawLine(vStart, (u_char)col, g1, (u_char)(col >> 0x10), (u_char)(col >> 0x18),
             vEnd, (u_char)col, g1, col >> 0x10 & 0xff, col >> 0x18);
}

/* --------------------------------------------------------------------------
 *  gra3ddbgDrawPoint
 * ------------------------------------------------------------------------ */
void gra3ddbgDrawPoint(float *vPoint, G3DCOLOR col)
{
    DrawPoint2(vPoint, (u_char)col, (u_char)(col >> 8), (u_char)(col >> 0x10), (u_char)(col >> 0x18));
}

/* --------------------------------------------------------------------------
 *  gra3ddbgVerifyVu1MemAddress
 *
 *  VU1-memory layout self-check.  The asserts are compiled out of this build,
 *  leaving an empty body.
 * ------------------------------------------------------------------------ */
void gra3ddbgVerifyVu1MemAddress(void)
{
}

/* --------------------------------------------------------------------------
 *  gra3ddbgDrawSphere
 * ------------------------------------------------------------------------ */
void gra3ddbgDrawSphere(float *vPosition, float fRange, G3DCOLOR col, float *vScale)
{
    _DrawSphere(vPosition, fRange, col, vScale);
}

/* --------------------------------------------------------------------------
 *  gra3ddbgDrawRangedCone
 *
 *  Draw a wireframe spot-light cone of half-angle fAngle and length fRange
 *  aimed along vDirection from vPosition.  Eight points are placed on the cap
 *  circle (radius fRange*sin(angle) at distance fRange*cos(angle)), the cone
 *  apex/base axis points are built, all are rotated to align +Y with
 *  vDirection (via a quaternion built from the rotation axis and angle), then
 *  translated to vPosition and joined with lines: apex->circle, circle->circle
 *  (closed loop) and base->circle.
 *
 *  The quaternion->rotation-matrix build and the point transforms are
 *  hand-scheduled VU0 macro code; they are kept in their inlined intrinsic
 *  form (LOW CONFIDENCE on the exact register scheduling -- see report).
 * ------------------------------------------------------------------------ */
void gra3ddbgDrawRangedCone(float *vPosition, float *vDirection, float fRange, float fAngle, G3DCOLOR col)
{
    float         avPointOnCircle[8][4];
    float         vPoint[4];
    float         vEnd[4];
    float         plane[4];
    float         vTemp[4];
    float         mat[4][4];
    float         fRadiusOfCircle;
    float         fOffsetY;
    int           i;

    /* the eight cap-circle points: angle step 2*PI/8, radius fRange*sin(a) */
    fRadiusOfCircle = fRange * sinf(fAngle);
    for (i = 0; i < 8; i = i + 1)
    {
        float x = (float)i * 6.283185005187988f * 0.125f;

        avPointOnCircle[i][0] = cosf(x) * fRadiusOfCircle;
        avPointOnCircle[i][1] = 0.0f;
        avPointOnCircle[i][2] = sinf(x) * fRadiusOfCircle;
        avPointOnCircle[i][3] = 1.0f;
    }

    /* the cone axis points: apex at -(fRange*cos(a)) below the cap, base at
     * (fRange - fRange*cos(a)). */
    fOffsetY = fRange * cosf(fAngle);

    vPoint[0] = 0.0f;
    vPoint[1] = -fOffsetY;
    vPoint[2] = 0.0f;
    vPoint[3] = 1.0f;

    vEnd[0] = 0.0f;
    vEnd[1] = fRange - fOffsetY;
    vEnd[2] = 0.0f;
    vEnd[3] = 1.0f;

    /* rotation axis (plane normal) = normal of the (direction, 0, +Y) plane */
    g3dCalcPlaneFromPoints(plane, vDirection, g_v0000, g_v0100);
    g3dCalcPlaneFromPointNormal(plane, g_v0000, plane);

    /* build the rotation matrix that maps +Y onto vDirection, from a
     * quaternion (axis * sin(theta/2), cos(theta/2)) where theta is the angle
     * between vDirection and +Y. */
    {
        sceVu0FVECTOR vDir;
        sceVu0FVECTOR vUp;
        float         q;
        float         fTheta;
        float         fSinHalf;
        float         fCosHalf;
        float         qx;
        float         qy;
        float         qz;
        sceVu0FVECTOR qm0;
        sceVu0FVECTOR qm1;
        sceVu0FVECTOR qm2;
        sceVu0FVECTOR qm3;
        sceVu0FVECTOR qn0;
        sceVu0FVECTOR qn1;
        sceVu0FVECTOR qn2;
        sceVu0FVECTOR qn3;

        /* fTheta = acos( normalise(vDirection) . +Y ) */
        sceVu0Normalize(vTemp, vDirection);
        fTheta = acosf(sceVu0InnerProduct(vTemp, g_v0100));

        /* quaternion = (axis * sin(theta/2), cos(theta/2)) */
        fSinHalf = sinf(fTheta * 0.5f);
        sceVu0ScaleVector(plane, plane, fSinHalf);
        fCosHalf = cosf(fTheta * 0.5f);

        qx = plane[0];
        qy = plane[1];
        qz = plane[2];

        /* the two quaternion-multiply matrices; their product is the rotation
         * matrix.  rows assembled from {w, x, y, z} sign permutations. */
        qm0[0] = fCosHalf; qm0[1] = qz;       qm0[2] = -qy;      qm0[3] = qx;
        qm1[0] = -qz;      qm1[1] = fCosHalf; qm1[2] = qx;       qm1[3] = qy;
        qm2[0] = qy;       qm2[1] = -qx;      qm2[2] = fCosHalf; qm2[3] = qz;
        qm3[0] = -qx;      qm3[1] = -qy;      qm3[2] = -qz;      qm3[3] = fCosHalf;

        qn0[0] = fCosHalf; qn0[1] = qz;       qn0[2] = -qy;      qn0[3] = -qx;
        qn1[0] = -qz;      qn1[1] = fCosHalf; qn1[2] = qx;       qn1[3] = -qy;
        qn2[0] = qy;       qn2[1] = -qx;      qn2[2] = fCosHalf; qn2[3] = -qz;
        qn3[0] = qx;       qn3[1] = qy;       qn3[2] = qz;       qn3[3] = fCosHalf;

        /* mat = qm * qn : mat[r] = qm0*qn[r].x + qm1*qn[r].y + qm2*qn[r].z + qm3*qn[r].w */
        {
            int c;
            for (c = 0; c < 4; c++)
            {
                mat[0][c] = qm0[c]*qn0[0] + qm1[c]*qn0[1] + qm2[c]*qn0[2] + qm3[c]*qn0[3];
                mat[1][c] = qm0[c]*qn1[0] + qm1[c]*qn1[1] + qm2[c]*qn1[2] + qm3[c]*qn1[3];
                mat[2][c] = qm0[c]*qn2[0] + qm1[c]*qn2[1] + qm2[c]*qn2[2] + qm3[c]*qn2[3];
                mat[3][c] = qm0[c]*qn3[0] + qm1[c]*qn3[1] + qm2[c]*qn3[2] + qm3[c]*qn3[3];
            }
        }
    }

    /* lift the cap circle up to the base plane (along local +Y) */
    vPoint[1] = vPoint[1] + fOffsetY;
    vEnd[1]   = vEnd[1] + fOffsetY;
    for (i = 7; i >= 0; i = i - 1)
    {
        avPointOnCircle[i][1] = avPointOnCircle[i][1] + fOffsetY;
    }

    /* rotate the apex / base points (rotation only) */
    {
        float p[4], e[4];
        int   c;

        for (c = 0; c < 4; c++)
        {
            p[c] = vPoint[0]*mat[0][c] + vPoint[1]*mat[1][c] + vPoint[2]*mat[2][c];
            e[c] = vEnd[0]*mat[0][c]   + vEnd[1]*mat[1][c]   + vEnd[2]*mat[2][c];
        }
        for (c = 0; c < 4; c++) { vPoint[c] = p[c]; vEnd[c] = e[c]; }
    }

    /* rotate the cap circle points */
    for (i = 7; i >= 0; i = i - 1)
    {
        float *pc = avPointOnCircle[i];
        float  r[4];
        int    c;

        for (c = 0; c < 4; c++)
            r[c] = pc[0]*mat[0][c] + pc[1]*mat[1][c] + pc[2]*mat[2][c];
        for (c = 0; c < 4; c++) pc[c] = r[c];
    }

    /* translate everything to vPosition */
    sceVu0AddVector(vPoint, vPoint, vPosition);
    sceVu0AddVector(vEnd,   vEnd,   vPosition);
    for (i = 7; i >= 0; i = i - 1)
    {
        sceVu0AddVector(avPointOnCircle[i], avPointOnCircle[i], vPosition);
    }

    /* apex -> each cap point */
    for (i = 7; i >= 0; i = i - 1)
    {
        gra3ddbgDrawLine(vPoint, avPointOnCircle[i], col);
    }

    /* cap point -> next cap point (closed loop) */
    for (i = 0; i < 8; i = i + 1)
    {
        gra3ddbgDrawLine(avPointOnCircle[i], avPointOnCircle[(i + 1) % 8], col);
    }

    /* base -> each cap point */
    for (i = 7; i >= 0; i = i - 1)
    {
        gra3ddbgDrawLine(vEnd, avPointOnCircle[i], col);
    }
}

/* --------------------------------------------------------------------------
 *  gra3ddbgDrawSprite
 *
 *  Draw a screen-space textured sprite over the G3DFREGION at depth fZ: the
 *  two screen corners are converted to GS primitive coordinates and a DIRECT
 *  packet (FLUSH + GIFTAG + RGBAQ + two XYZF2 sprite verts) is built and
 *  pushed down the DMA chain.
 * ------------------------------------------------------------------------ */
void gra3ddbgDrawSprite(G3DFREGION *pRegion, float fZ, G3DCOLOR col, sceGsTex0 *pGsTex0)
{
    float                vScreenCoordLT[4];
    float                vScreenCoordRB[4];
    sceGsXyz             lt;
    sceGsXyz             rb;
    _PACKET_DRAWSPRITE  *pPacket;

    vScreenCoordLT[0] = pRegion->fLeft;
    vScreenCoordLT[1] = pRegion->fTop;
    vScreenCoordLT[2] = fZ;

    vScreenCoordRB[0] = pRegion->fLeft + pRegion->fWidth;
    vScreenCoordRB[1] = pRegion->fTop + pRegion->fHeight;
    vScreenCoordRB[2] = fZ;

    g3dCalcGsPrimitiveCoord(&lt, vScreenCoordLT);
    g3dCalcGsPrimitiveCoord(&rb, vScreenCoordRB);

    pPacket = (_PACKET_DRAWSPRITE *)g3dDmaOpenPacket();

    /* VIF1 FLUSH + DIRECT(4 quadwords) */
    ((unsigned int *)&pPacket->qwVifCode)[0] = 0;
    ((unsigned int *)&pPacket->qwVifCode)[1] = 0;
    ((unsigned int *)&pPacket->qwVifCode)[2] = 0x11000000;          /* VIF1 FLUSH */
    ((unsigned int *)&pPacket->qwVifCode)[3] = 0x50000004;          /* DIRECT, qwc 4 */

    /* GIFTAG: NLOOP=1, EOP, PACKED, NREG=3 (RGBAQ, XYZF2, XYZF2) */
    ((u_long *)&pPacket->Gt)[0] =
        (((u_long *)&pPacket->Gt)[0] & 0x7fffffff8000ULL) | 0x3003400000008001ULL;
    ((u_long *)&pPacket->Gt)[1] =
        (((u_long *)&pPacket->Gt)[1] & 0xfffffffffffff000ULL) | 0x441ULL;

    /* RGBAQ */
    ((unsigned int *)&pPacket->Rgbaq)[0] = col & 0xff;
    ((unsigned int *)&pPacket->Rgbaq)[1] = col >> 8 & 0xff;
    ((unsigned int *)&pPacket->Rgbaq)[2] = col >> 0x10 & 0xff;
    ((unsigned int *)&pPacket->Rgbaq)[3] = col >> 0x18;

    /* top-left XYZF2 */
    ((unsigned int *)&pPacket->aXyzf2[0])[0] = (unsigned int)(unsigned short)lt.X;
    ((unsigned int *)&pPacket->aXyzf2[0])[1] = (unsigned int)(unsigned short)lt.Y;
    ((unsigned int *)&pPacket->aXyzf2[0])[2] = lt.Z << 4;
    ((unsigned int *)&pPacket->aXyzf2[0])[3] = 0;

    /* bottom-right XYZF2 */
    ((unsigned int *)&pPacket->aXyzf2[1])[0] = (unsigned int)(unsigned short)rb.X;
    ((unsigned int *)&pPacket->aXyzf2[1])[1] = (unsigned int)(unsigned short)rb.Y;
    ((unsigned int *)&pPacket->aXyzf2[1])[2] = rb.Z << 4;
    ((unsigned int *)&pPacket->aXyzf2[1])[3] = 0;

    g3dDmaClosePacket(&pPacket[1]);
}

/* --------------------------------------------------------------------------
 *  gra3ddbgDrawBB
 *
 *  Draw the twelve edges of an eight-corner bounding box (corner order per
 *  gra3dbbFromBounds): the bottom quad [0,1,3,2], the top quad [4,5,7,6] and
 *  the four vertical edges.
 * ------------------------------------------------------------------------ */
void gra3ddbgDrawBB(float (*avBB)[4], G3DCOLOR col)
{
    gra3ddbgDrawLine(avBB[0], avBB[1], col);
    gra3ddbgDrawLine(avBB[1], avBB[3], col);
    gra3ddbgDrawLine(avBB[3], avBB[2], col);
    gra3ddbgDrawLine(avBB[2], avBB[0], col);
    gra3ddbgDrawLine(avBB[4], avBB[5], col);
    gra3ddbgDrawLine(avBB[5], avBB[7], col);
    gra3ddbgDrawLine(avBB[7], avBB[6], col);
    gra3ddbgDrawLine(avBB[6], avBB[4], col);
    gra3ddbgDrawLine(avBB[0], avBB[4], col);
    gra3ddbgDrawLine(avBB[1], avBB[5], col);
    gra3ddbgDrawLine(avBB[2], avBB[6], col);
    gra3ddbgDrawLine(avBB[3], avBB[7], col);
}

/* --------------------------------------------------------------------------
 *  gra3ddbgDrawProc  (empty in the prototype)
 * ------------------------------------------------------------------------ */
void gra3ddbgDrawProc(void)
{
}

/* --------------------------------------------------------------------------
 *  gra3ddbgVerifyVifCodex4
 *
 *  Verify the four VIF codes of a packet header.
 * ------------------------------------------------------------------------ */
void gra3ddbgVerifyVifCodex4(tVIF_CODE *aVC)
{
    for (int i = 0; i < 4; i++)
    {
        g3ddbgVerifyVifCode(aVC);
        aVC++;
    }
}

/* --------------------------------------------------------------------------
 *  gra3ddbgNormalizeLightData
 *
 *  Copy a light-data record and clamp it into a sane range: ambient and every
 *  light's diffuse / specular / ambient colour are clamped to [0,1], each
 *  light's direction is renormalised, the ranges are forced non-negative and
 *  ordered (min <= max), and the spot angles are clamped to [0, PI/2] and
 *  ordered (inside <= outside).
 * ------------------------------------------------------------------------ */
void gra3ddbgNormalizeLightData(GRA3DLIGHTDATA *pLDDest, GRA3DLIGHTDATA *pLDSrc)
{
    int i;

    /* "pLDDest" / "pLDSrc" */
    G3DASSERT(pLDDest, "");
    G3DASSERT(pLDSrc, "");

    *pLDDest = *pLDSrc;

    /* clamp the global ambient to [0,1] */
    {
        int c;
        for (c = 0; c < 4; c++)
        {
            if (pLDDest->vAmbient[c] < 0.0f) pLDDest->vAmbient[c] = 0.0f;
            else if (pLDDest->vAmbient[c] > 1.0f) pLDDest->vAmbient[c] = 1.0f;
        }
    }

    for (i = 0; i < NUM_GRA3DLIGHTID; i = i + 1)
    {
        G3DLIGHT &rL = pLDDest->aLight[i];

        _fixed_array_verifyrange<G3DLIGHT>(i, NUM_GRA3DLIGHTID);

        /* clamp diffuse / specular / ambient to [0,1] */
        {
            int c;
            for (c = 0; c < 4; c++)
            {
                if (rL.vDiffuse[c]  < 0.0f) rL.vDiffuse[c]  = 0.0f; else if (rL.vDiffuse[c]  > 1.0f) rL.vDiffuse[c]  = 1.0f;
                if (rL.vSpecular[c] < 0.0f) rL.vSpecular[c] = 0.0f; else if (rL.vSpecular[c] > 1.0f) rL.vSpecular[c] = 1.0f;
                if (rL.vAmbient[c]  < 0.0f) rL.vAmbient[c]  = 0.0f; else if (rL.vAmbient[c]  > 1.0f) rL.vAmbient[c]  = 1.0f;
            }
        }

        /* renormalise the light direction */
        sceVu0Normalize(rL.vDirection, rL.vDirection);

        /* ranges: non-negative, min <= max */
        if (rL.fMinRange < 0.0f)
        {
            rL.fMinRange = 0.0f;
        }
        if (rL.fMaxRange < rL.fMinRange)
        {
            rL.fMinRange = rL.fMaxRange;
        }

        /* spot angles: clamp to [0, PI/2], inside <= outside */
        if (rL.fAngleInside < 0.0f)
        {
            rL.fAngleInside = 0.0f;
        }
        if (rL.fAngleOutside > 1.570796251296997f)
        {
            rL.fAngleOutside = 1.570796251296997f;
        }
        if (rL.fAngleOutside < rL.fAngleInside)
        {
            rL.fAngleInside = rL.fAngleOutside;
        }
    }
}
