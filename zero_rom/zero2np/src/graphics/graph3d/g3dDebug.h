/* ==========================================================================
 *  g3dDebug.h
 *
 *  Public API for the g3d debug layer: the on-screen exception console, VU0
 *  FP register load/store/dump helpers, the VIF1/DMA packet validators, and
 *  the VIF1_STAT / VIF1_CODE register dumps.  The printf-style assert/warning
 *  back ends (g3ddbgAssert / g3ddbgWarning / g3ddbgPrintf / _SetLineInfo) and
 *  the G3DASSERT family of macros live in g3ddbg.h.
 *
 *  Reconstructed from the Feb 6 2004 prototype (SLES_523.84).
 * ======================================================================== */

#ifndef _G3DDEBUG_H
#define _G3DDEBUG_H

#include "g3ddbg.h"             /* g3ddbgAssert / g3ddbgWarning / _SetLineInfo */
#include "gra3dTypes.h"         /* tVIF_CODE */

#ifdef __cplusplus
extern "C" {
#endif

_G3DLINEINFO *_GetLineInfo(void);

int  g3ddbgDumpMemoryCompare(void *p0, void *p1, int iSize);

void g3ddbgWaitVU1(void);
void g3ddbgDumpVu1MicroMemory(void);
void DispVUMemory(void);

void g3ddbgLoadVu0FloatingPointRegisters(float av[][4]);
void g3ddbgStoreVu0FloatingPointRegisters(float av[][4]);
void g3ddbgDumpVu0FloatingPointRegisters(float av[][4]);

void g3ddbgVerifyVu1MemAddress(void);
void g3ddbgVerifyGsRegisterAddress(void);
void g3ddbgVerifyVifCode(tVIF_CODE *pVC);
void g3ddbgVerifyDmaPacket(void *pDmaPacket);
void g3ddbgVerifyDmaBuffer(void *pBuffer);

void g3ddbgDumpVif1Stat(void);
void g3ddbgDumpVif1Code(void);

void g3ddbgInfinitePrintConsole(int b);
void g3ddbgPrintConsole(char *pStr);

#ifdef __cplusplus
}
#endif

#endif /* _G3DDEBUG_H */
