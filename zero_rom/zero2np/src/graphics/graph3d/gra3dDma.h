/* ==========================================================================
 *  gra3dDma.h
 *
 *  Thin gra3d-layer wrappers over the g3dDma packet builder: VIF1 code
 *  emitters for calling a VU1 micro-subroutine / issuing an UNPACK, plus the
 *  four "call micro subroutine" packet helpers used by the renderer.
 *
 *  Reconstructed from the Feb 6 2004 prototype (SLES_523.84).
 * ======================================================================== */

#ifndef _GRA3DDMA_H
#define _GRA3DDMA_H

#ifdef __cplusplus
extern "C" {
#endif

int  gra3dSetVif1Code_CallMicroSubroutine2(void *pQW, const unsigned int *pMSTop);
int  gra3dSetVif1Code_Unpack(int *dest, int iAddress, int iSize, int iCommand);
int  gra3dDmaLoadVu1MicroProgram(const unsigned int *pMPG);
void gra3dCallMicroSubroutine1(const unsigned int *pMS);
void gra3dCallMicroSubroutine2(const unsigned int *pMS);
void gra3dCallMicroSubroutine3(const unsigned int *pMS);
void gra3dCallMicroSubroutine4(const unsigned int *pMS);

#ifdef __cplusplus
}
#endif

#endif /* _GRA3DDMA_H */
