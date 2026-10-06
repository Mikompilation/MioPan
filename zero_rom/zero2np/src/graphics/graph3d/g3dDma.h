/* ==========================================================================
 *  g3dDma.h
 *
 *  The low-level VIF1 DMA packet interface for the zero2np 3D engine: open a
 *  packet from the VIF1 ring, append/copy quadwords into it, close it, and the
 *  VU1 micro-program / micro-subroutine kick helpers.  These build on the SCE
 *  EE dmaVif1* ring allocator (declared below).
 *
 *  Reconstructed from the Feb 6 2004 prototype (SLES_523.84).
 * ======================================================================== */

#ifndef _G3DDMA_H
#define _G3DDMA_H

#include "eetypes.h"
#include "sce_gs.h"             /* sceGifPackAd */
#include <libdma.h>
#include "../dmaVif1.h"         /* dmaVif1* ring allocator (full API) */

/* NOTE: g3dDmaSetGsRegister is overloaded, so this header (like the rest of
 * the graph3d sources) is consumed by the C++ compiler -- no extern "C". */

/* ---- packet builder ---------------------------------------------------- */
void *g3dDmaOpenPacket(void);
int   g3dDmaCancelPacket(void);
int   g3dDmaClosePacket(const void *pPacket);
int   g3dDmaAddPacket(const void *pPacket, int iQWSize);
int   g3dDmaFlush(void);
int   g3dDmaCopyPacket(const void *pPacket, int iQWSize);

/* ---- GS register writes ------------------------------------------------ */
int   g3dDmaSetGsRegister(u_long ulGsData, u_long ulGsAddress);
int   g3dDmaSetGsRegister(const sceGifPackAd *pGPA);
int   g3dDmaSetGsRegisters(const sceGifPackAd *aGPA, int iNum);
/* aGPA is treated read-only by the upload loop. */

/* ---- VU1 micro program / subroutine kicks ------------------------------ */
int   g3dDmaLoadVu1MicroProgram(const unsigned int *pMPG, int bImmediately);
int   g3dDmaCallVu1MicroSubroutine(const unsigned int *pMS, int bImmediately);
int   g3dDmaContinueVu1MicroSubroutine(const unsigned int *pMS);

#endif /* _G3DDMA_H */
