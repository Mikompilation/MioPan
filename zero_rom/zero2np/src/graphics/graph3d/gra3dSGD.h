/* ==========================================================================
 *  gra3dSGD.h
 *
 *  Public interface for the SGD model renderer: init/teardown, the master
 *  draw entry (_gra3dDrawSGD) and its preset variant, ST-coordinate and
 *  per-vertex-colour editing, plus the global accessors (current SGD, coord
 *  cache, VN buffers, block count) shared with gra3dSGDData.c / gra3dShadow.c.
 *
 *  Reconstructed from the Feb 6 2004 prototype (SLES_523.84).
 * ======================================================================== */

#ifndef _GRA3DSGD_H
#define _GRA3DSGD_H

#include "sgd_types.h"

struct CoordCache;
struct GRA3DSGDCREATIONDATA;

#ifdef __cplusplus
extern "C" {
#endif

/* ---- lifecycle --------------------------------------------------------- */
void gra3dsgdInit(GRA3DSGDCREATIONDATA *pCD);
void gra3dsgdSetupVu1(void);

/* ---- current SGD data -------------------------------------------------- */
void           gra3dsgdSetData(SGDFILEHEADER *pSGDTop);
SGDFILEHEADER *gra3dsgdGetData(void);
int            gra3dsgdGetNumBlock(void);

/* ---- coordinate access ------------------------------------------------- */
SGDCOORDINATE *gra3dsgdGetCoordinate(int iIndex);
void           gra3dsgdSetCoordinate(SGDCOORDINATE *pCU, int iIndex);

/* ---- draw -------------------------------------------------------------- */
void _gra3dDrawSGD(SGDFILEHEADER *pSGDTop, SGDRENDERTYPE type, SGDCOORDINATE *pCoord, int pnum);
void gra3dsgdDrawPresetDataObject(SGDPROCUNITHEADER *pPUHead);
void SgSortPreProcess(u_int *_prim);
int  BoundingBoxCalcP(SGDPROCUNITHEADER *_prim);

/* ---- editing ----------------------------------------------------------- */
void gra3dChangeST(SGDFILEHEADER *pSGDTop, float fAddS, float fAddT);
void gra3dSetVertexColorPreset(SGDFILEHEADER *pSGDTop, int iVertexNo, float *vSetColor);

/* ---- TRI2 / VRAM helpers ----------------------------------------------- */
unsigned int gra3dCalcVRAMPageSize(unsigned int uiBlockSize);

/* ---- internal globals (shared across the SGD sources) ------------------ */
void               _SetVUVNPRIM(SGDPROCUNITHEADER *ppuhVUVN);
SGDPROCUNITHEADER *_GetVUVNPRIM(void);
void               _SetPREVIOUSTRI2PRIM(SGDPROCUNITHEADER *p);
SGDPROCUNITHEADER *_GetPREVIOUSTRI2PRIM(void);

CoordCache *_GetCoordCache(void);
void        _SetCoordCache(CoordCache *pCC);

float (*_GetGlobalVertexBuffer(void))[4];
float (*_GetGlobalNormalBuffer(void))[4];
int     _GetGlobalBufferSize(void);

int  _GetEdgeCheck(void);
void _SetEdgeCheck(int ec);

#ifdef __cplusplus
}
#endif

#endif /* _GRA3DSGD_H */
