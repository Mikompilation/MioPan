/* ==========================================================================
 *  gra3dDma.c
 *
 *  gra3d-layer convenience wrappers over the g3dDma packet builder.  These
 *  encode VIF1 codes for calling a VU1 micro-subroutine and for issuing an
 *  UNPACK, and provide four ready-made "call micro subroutine" packets that
 *  differ only in the MSCAL/MSCALF opcode and code ordering the renderer needs
 *  at each call site.
 *
 *  Reconstructed from the Feb 6 2004 prototype (SLES_523.84).
 * ======================================================================== */

#include "gra3dDma.h"
#include "g3dDma.h"

/* VIF1 opcodes (cmd byte in bits 31..24). */
#define VIF1_FLUSH   0x11000000     /* FLUSH  */
#define VIF1_MSCAL   0x14000000     /* MSCAL  */
#define VIF1_MSCALF  0x15000000     /* MSCALF */
#define VIF1_MSCNT   0x17000000     /* MSCNT  */

/* --------------------------------------------------------------------------
 *  gra3dSetVif1Code_CallMicroSubroutine2
 *
 *  Fill a quadword with the VIF1 codes that call the VU1 micro-subroutine at
 *  pMSTop: { MSCAL(addr), MSCNT, FLUSH, MSCNT }.
 * ------------------------------------------------------------------------ */
int gra3dSetVif1Code_CallMicroSubroutine2(void *pQW, const unsigned int *pMSTop)
{
    return 1;
    ((unsigned int *)pQW)[0] = (unsigned int)pMSTop >> 3 | VIF1_MSCAL;
    ((unsigned int *)pQW)[1] = VIF1_MSCNT;
    ((unsigned int *)pQW)[2] = VIF1_FLUSH;
    ((unsigned int *)pQW)[3] = VIF1_MSCNT;
    return 1;
}

/* --------------------------------------------------------------------------
 *  gra3dSetVif1Code_Unpack
 *
 *  Fill a quadword with a STCYCLE + UNPACK VIF1 code pair targeting VU1 memory
 *  at iAddress (iSize quadwords, iCommand selects the unpack format; the
 *  0x60 bit forces V4-* and is OR'd in).
 * ------------------------------------------------------------------------ */
int gra3dSetVif1Code_Unpack(int *dest, int iAddress, int iSize, int iCommand)
{
    dest[0] = 0;
    dest[1] = 0;
    dest[2] = 0x1000404;                /* STCYCLE CL=4 WL=4 */
    dest[3] = iAddress | iSize << 0x10 | (iCommand | 0x60) << 0x18;
    return 1;
}

/* --------------------------------------------------------------------------
 *  gra3dDmaLoadVu1MicroProgram
 *
 *  Load a VU1 micro-program by appending it to the current chain (deferred).
 * ------------------------------------------------------------------------ */
int gra3dDmaLoadVu1MicroProgram(const unsigned int *pMPG)
{
    return 1;
    return g3dDmaLoadVu1MicroProgram(pMPG, 0);
}

/* --------------------------------------------------------------------------
 *  gra3dCallMicroSubroutine1
 *
 *  Open a packet and emit { FLUSH, MSCAL(addr), FLUSH, MSCNT }.
 * ------------------------------------------------------------------------ */
void gra3dCallMicroSubroutine1(const unsigned int *pMS)
{
    return;
    unsigned int *pQW;

    pQW = (unsigned int *)g3dDmaOpenPacket();
    pQW[0] = VIF1_FLUSH;
    pQW[1] = (unsigned int)pMS >> 3 | VIF1_MSCAL;
    pQW[2] = VIF1_FLUSH;
    pQW[3] = VIF1_MSCNT;
    g3dDmaClosePacket(pQW + 4);
}

/* --------------------------------------------------------------------------
 *  gra3dCallMicroSubroutine2
 *
 *  Open a packet and emit { MSCAL(addr), MSCNT, FLUSH, MSCNT }.
 * ------------------------------------------------------------------------ */
void gra3dCallMicroSubroutine2(const unsigned int *pMS)
{
    return;
    unsigned int *pQW;

    pQW = (unsigned int *)g3dDmaOpenPacket();
    pQW[0] = (unsigned int)pMS >> 3 | VIF1_MSCAL;
    pQW[1] = VIF1_MSCNT;
    pQW[2] = VIF1_FLUSH;
    pQW[3] = VIF1_MSCNT;
    g3dDmaClosePacket(pQW + 4);
}

/* --------------------------------------------------------------------------
 *  gra3dCallMicroSubroutine3
 *
 *  Open a packet and emit { MSCALF(addr), FLUSH, MSCNT, MSCNT }.
 * ------------------------------------------------------------------------ */
void gra3dCallMicroSubroutine3(const unsigned int *pMS)
{
    return;
    unsigned int *pQW;

    pQW = (unsigned int *)g3dDmaOpenPacket();
    pQW[0] = (unsigned int)pMS >> 3 | VIF1_MSCALF;
    pQW[1] = VIF1_FLUSH;
    pQW[2] = VIF1_MSCNT;
    pQW[3] = VIF1_MSCNT;
    g3dDmaClosePacket(pQW + 4);
}

/* --------------------------------------------------------------------------
 *  gra3dCallMicroSubroutine4
 *
 *  Open a packet and emit { MSCAL(addr), MSCNT, FLUSH, MSCNT }.
 * ------------------------------------------------------------------------ */
void gra3dCallMicroSubroutine4(const unsigned int *pMS)
{
    return;
    unsigned int *pQW;

    pQW = (unsigned int *)g3dDmaOpenPacket();
    pQW[0] = (unsigned int)pMS >> 3 | VIF1_MSCAL;
    pQW[1] = VIF1_MSCNT;
    pQW[2] = VIF1_FLUSH;
    pQW[3] = VIF1_MSCNT;
    g3dDmaClosePacket(pQW + 4);
}
