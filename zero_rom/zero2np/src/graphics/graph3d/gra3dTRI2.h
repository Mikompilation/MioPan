/* ==========================================================================
 *  gra3dTRI2.h
 *
 *  TRI2 embedded-texture-file services for the gra3d layer.  A TRI2 file is a
 *  run of SGDTRI2FILEHEADER records, each a VIF1 DIRECT packet wrapping a GS
 *  local<->host image-transfer descriptor (BITBLTBUF / TRXPOS / TRXREG /
 *  TRXDIR A+D items).  These routines size a TRI2 file's VRAM footprint, push
 *  it to VRAM down the DMA chain (or directly), and regenerate a TRI2 file
 *  from the current VRAM image.
 *
 *  Reconstructed from the Feb 6 2004 prototype (SLES_523.84).
 * ======================================================================== */

#ifndef _GRA3DTRI2_H
#define _GRA3DTRI2_H

#include "sgd_types.h"          /* SGDTRI2FILEHEADER / TRI2SIZEDATA */

#ifdef __cplusplus
extern "C" {
#endif

void gra3dGetTRI2SizeData(TRI2SIZEDATA *pSD, TRI2SIZEDATA *pSDPrev, SGDTRI2FILEHEADER *pTRI2Head);
void gra3dLoadTRI2FileToVRAM(int iNumTexture, SGDTRI2FILEHEADER *pTRI2HeadTop, int iDmaChan);
int  gra3dGenerateTRI2FileFromVRAM(SGDTRI2FILEHEADER *pTRI2HeadTop, TRI2SIZEDATA *pSD);

/* PORT: host only.  Every GS image transfer a TRI2 unit's iNumTexture headers
 * carry, in upload order; and the host half of gra3dLoadTRI2FileToVRAM() on
 * its own -- the unit sent to emulated GS memory, no DMA packet built. */
void gra3dHostForEachTRI2Image(int iNumTexture, SGDTRI2FILEHEADER *pTRI2HeadTop,
                               void (*pfnImage)(sceGsLoadImage *, void *),
                               void *pUser);
void gra3dHostLoadTRI2(int iNumTexture, SGDTRI2FILEHEADER *pTRI2HeadTop);

#ifdef __cplusplus
}
#endif

#endif /* _GRA3DTRI2_H */
