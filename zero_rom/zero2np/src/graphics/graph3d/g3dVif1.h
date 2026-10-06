/* ==========================================================================
 *  g3dVif1.h
 *
 *  VIF1 register-shadow interface for the 3D engine: snapshot the live VIF1
 *  registers, push a batch of register-set commands as a VIF1 packet, and
 *  UNPACK a quadword buffer into VU1 micro-memory.
 *
 *  Reconstructed from the Feb 6 2004 prototype (SLES_523.84).
 * ======================================================================== */

#ifndef _G3DVIF1_H
#define _G3DVIF1_H

#include "gra3dTypes.h"         /* G3DVIF1CMDDATA */

#ifdef __cplusplus
extern "C" {
#endif

void g3dVif1Init(void);
void g3dVif1SetRegister(const G3DVIF1CMDDATA *aVCD, int iNumPacket);
int  g3dVif1Unpack(int iVu1MemAddress, const void *pData, int iQWSize);

#ifdef __cplusplus
}
#endif

#endif /* _G3DVIF1_H */
