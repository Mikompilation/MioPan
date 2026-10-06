/* ==========================================================================
 *  g3dVif1.c
 *
 *  VIF1 register shadow/parser for the 3D engine.  g3dVif1Init() snapshots the
 *  live VIF1 register file into s_Vif1RegisterLayout; g3dVif1SetRegister()
 *  decodes a list of G3DVIF1CMDDATA register-set commands, updates the local
 *  shadow, and emits the corresponding VIF1 packet; g3dVif1Unpack() builds an
 *  UNPACK packet that streams a quadword buffer into VU1 micro-memory.
 *
 *  Reconstructed from the Feb 6 2004 prototype (SLES_523.84).
 * ======================================================================== */

#include "g3dVif1.h"
#include "g3dDma.h"
#include "eeregs.h"
#include "gra3dTypes.h"
#include "g3ddbg.h"
#include <string.h>             /* memset */

/* Number of distinct VIF1 register-set commands recognised by the parser. */
#define G3D_NUM_VIF1CMD  10

/* Live VIF1 register file mirror (filled by g3dVif1Init). */
static G3DVIF1REGISTERLAYOUT s_Vif1RegisterLayout;

#define G3D_VIF1_HW_BASE_ADDR  0x10003c00
#define G3D_VIF1_HW_STRIDE     0x10

static qword *G3D_VIF1_REGISTER_QWORD(unsigned int uiHwAddress)
{
    unsigned int iReg;

    iReg = (uiHwAddress - G3D_VIF1_HW_BASE_ADDR) / G3D_VIF1_HW_STRIDE;
    G3DASSERT(iReg < PCPORT_VIF1_REGISTER_COUNT, "uiHwAddress:0x%08x", uiHwAddress);
    return (qword *)(void *)&g_pcport_REG_VIF1_REGISTER_FILE[iReg][0];
}

/* --------------------------------------------------------------------------
 *  _GetVif1CmdInfo
 *
 *  Look up the command-info record for the VIF1 opcode carried in pVCD and
 *  copy it to *pRet.  The table maps each settable register (STCYCLE, OFFSET,
 *  BASE, ITOP, STMOD, MARK, NUM, R0..R3, C0..C3) to its hardware register
 *  address, its sub-packet length, and the word(s) it shadows in
 *  s_Vif1RegisterLayout.  Built on the stack because the local-register
 *  pointers reference the runtime register-layout mirror.
 * ------------------------------------------------------------------------ */
static void _GetVif1CmdInfo(_VIF1CMDINFO *pRet, const G3DVIF1CMDDATA *pVCD)
{
    _VIF1CMDINFO aVCI[G3D_NUM_VIF1CMD];
    int          i;

    aVCI[0].uiCmd = 1;
    aVCI[0].pqwSubstantialRegister = G3D_VIF1_REGISTER_QWORD(0x10003c40);
    aVCI[0].iLengthSubPacket = 0;
    memset(aVCI[0].apiLocalRegister, 0, sizeof(aVCI[0].apiLocalRegister));
    aVCI[0].apiLocalRegister[0] = (int *)&s_Vif1RegisterLayout.Array.auiReg[4];

    aVCI[1].uiCmd = 2;
    aVCI[1].pqwSubstantialRegister = G3D_VIF1_REGISTER_QWORD(0x10003cb0);
    aVCI[1].iLengthSubPacket = 0;
    memset(aVCI[1].apiLocalRegister, 0, sizeof(aVCI[1].apiLocalRegister));
    aVCI[1].apiLocalRegister[0] = (int *)&s_Vif1RegisterLayout.Array.auiReg[0xb];

    aVCI[2].uiCmd = 3;
    aVCI[2].pqwSubstantialRegister = G3D_VIF1_REGISTER_QWORD(0x10003ca0);
    aVCI[2].iLengthSubPacket = 0;
    memset(aVCI[2].apiLocalRegister, 0, sizeof(aVCI[2].apiLocalRegister));
    aVCI[2].apiLocalRegister[0] = (int *)&s_Vif1RegisterLayout.Array.auiReg[10];

    aVCI[3].uiCmd = 4;
    aVCI[3].pqwSubstantialRegister = G3D_VIF1_REGISTER_QWORD(0x10003cd0);
    aVCI[3].iLengthSubPacket = 0;
    memset(aVCI[3].apiLocalRegister, 0, sizeof(aVCI[3].apiLocalRegister));
    aVCI[3].apiLocalRegister[0] = (int *)&s_Vif1RegisterLayout.Array.auiReg[9];

    aVCI[4].uiCmd = 5;
    aVCI[4].pqwSubstantialRegister = G3D_VIF1_REGISTER_QWORD(0x10003c50);
    aVCI[4].iLengthSubPacket = 0;
    memset(aVCI[4].apiLocalRegister, 0, sizeof(aVCI[4].apiLocalRegister));
    aVCI[4].apiLocalRegister[0] = (int *)&s_Vif1RegisterLayout.Array.auiReg[5];

    aVCI[5].uiCmd = 7;
    aVCI[5].pqwSubstantialRegister = G3D_VIF1_REGISTER_QWORD(0x10003c30);
    aVCI[5].iLengthSubPacket = 0;
    memset(aVCI[5].apiLocalRegister, 0, sizeof(aVCI[5].apiLocalRegister));
    aVCI[5].apiLocalRegister[0] = (int *)&s_Vif1RegisterLayout.Array.auiReg[3];

    aVCI[6].uiCmd = 0x20;
    aVCI[6].pqwSubstantialRegister = G3D_VIF1_REGISTER_QWORD(0x10003c70);
    aVCI[6].iLengthSubPacket = 1;
    memset(aVCI[6].apiLocalRegister, 0, sizeof(aVCI[6].apiLocalRegister));
    aVCI[6].apiLocalRegister[0] = (int *)&s_Vif1RegisterLayout.Array.auiReg[7];

    aVCI[7].uiCmd = 0x30;
    aVCI[7].pqwSubstantialRegister = G3D_VIF1_REGISTER_QWORD(0x10003d00);
    aVCI[7].iLengthSubPacket = 4;
    aVCI[7].apiLocalRegister[0] = (int *)&s_Vif1RegisterLayout.Array.auiReg[0xe];
    aVCI[7].apiLocalRegister[1] = (int *)&s_Vif1RegisterLayout.Array.auiReg[0xf];
    aVCI[7].apiLocalRegister[2] = (int *)&s_Vif1RegisterLayout.Array.auiReg[0x10];
    aVCI[7].apiLocalRegister[3] = (int *)&s_Vif1RegisterLayout.Array.auiReg[0x11];

    aVCI[8].uiCmd = 0x31;
    aVCI[8].pqwSubstantialRegister = G3D_VIF1_REGISTER_QWORD(0x10003d40);
    aVCI[8].iLengthSubPacket = 4;
    aVCI[8].apiLocalRegister[0] = (int *)&s_Vif1RegisterLayout.Array.auiReg[0x12];
    aVCI[8].apiLocalRegister[1] = (int *)&s_Vif1RegisterLayout.Array.auiReg[0x13];
    aVCI[8].apiLocalRegister[2] = (int *)&s_Vif1RegisterLayout.Array.auiReg[0x14];
    aVCI[8].apiLocalRegister[3] = (int *)&s_Vif1RegisterLayout.Array.auiReg[0x15];

    memset(&aVCI[9], 0, sizeof(aVCI[0]));
    aVCI[9].uiCmd = 0x11;

    for (i = 0; i < G3D_NUM_VIF1CMD; i = i + 1)
    {
        if ((aVCI[i].uiCmd & 0x7f) == (unsigned int)*((const unsigned char *)&pVCD->uiCmd + 3))
        {
            *pRet = aVCI[i];
            return;
        }
    }

    G3DASSERT(0, "");
}

/* --------------------------------------------------------------------------
 *  _UpdateValue
 *
 *  Apply one register-set command (pVCD) to its shadow word(s) described by
 *  pVCI.  Returns non-zero if the command actually wrote a register (the FLUSH
 *  opcode 0x11 is recognised but writes nothing; an unknown opcode asserts and
 *  returns 0).
 * ------------------------------------------------------------------------ */
static int _UpdateValue(_VIF1CMDINFO *pVCI, const G3DVIF1CMDDATA *pVCD)
{
    int iRet;

    switch (pVCI->uiCmd & 0x7f)
    {
    case 1:     /* STCYCLE: CL + WL (two bytes) */
    {
        iRet = 1;
        *(char *)pVCI->apiLocalRegister[0] = (char)pVCD->uiCmd;
        *((char *)pVCI->apiLocalRegister[0] + 1) = *((char *)&pVCD->uiCmd + 1);
        break;
    }
    case 2:     /* OFFSET: 10-bit field */
    {
        iRet = 1;
        *(unsigned int *)pVCI->apiLocalRegister[0] =
            (*(unsigned int *)pVCI->apiLocalRegister[0] & 0xfffffc00) | (pVCD->uiCmd & 0x3ff);
        break;
    }
    case 3:     /* BASE: 8-bit field */
    case 4:     /* ITOP: 8-bit field */
    {
        iRet = 1;
        *pVCI->apiLocalRegister[0] =
            *pVCI->apiLocalRegister[0] & 0xfffffc00 | (unsigned int)(unsigned char)pVCD->uiCmd;
        break;
    }
    case 5:     /* STMOD: 2-bit field */
    {
        iRet = 1;
        *(unsigned int *)pVCI->apiLocalRegister[0] =
            (*(unsigned int *)pVCI->apiLocalRegister[0] & 0xfffffffc) | (pVCD->uiCmd & 3);
        break;
    }
    case 7:     /* MARK: 16-bit field */
    {
        iRet = 1;
        *(short *)pVCI->apiLocalRegister[0] = (short)pVCD->uiCmd;
        break;
    }
    case 0x11:  /* FLUSH: no shadow */
    {
        iRet = 1;
        break;
    }
    case 0x20:  /* MSKPATH3 / NUM: 2-bit field from sub-packet */
    {
        iRet = 1;
        *(unsigned int *)pVCI->apiLocalRegister[0] =
            (*(unsigned int *)pVCI->apiLocalRegister[0] & 0xfffffffc) |
            (pVCD->auiSubPacket[0] & 3);
        break;
    }
    case 0x30:  /* STROW: 4 sub-packet words (R0..R3) */
    case 0x31:  /* STCOL: 4 sub-packet words (C0..C3) */
    {
        iRet = 1;
        *pVCI->apiLocalRegister[0] = pVCD->auiSubPacket[0];
        *pVCI->apiLocalRegister[1] = pVCD->auiSubPacket[1];
        *pVCI->apiLocalRegister[2] = pVCD->auiSubPacket[2];
        *pVCI->apiLocalRegister[3] = pVCD->auiSubPacket[3];
        break;
    }
    default:
    {
        G3DASSERT(0, "");
        iRet = 0;
        break;
    }
    }

    return iRet;
}

/* --------------------------------------------------------------------------
 *  g3dVif1Init
 *
 *  Snapshot the host-backed VIF1 register file into the local register-layout
 *  mirror.  On EE this register file lives at 0x10003c00 with a 0x10 stride.
 * ------------------------------------------------------------------------ */
void g3dVif1Init(void)
{
    int i;

    for (i = 0; i < PCPORT_VIF1_REGISTER_COUNT; i = i + 1)
    {
        s_Vif1RegisterLayout.Array.auiReg[i] = g_pcport_REG_VIF1_REGISTER_FILE[i][0];
    }
}

/* --------------------------------------------------------------------------
 *  g3dVif1SetRegister
 *
 *  Emit a VIF1 packet that applies iNumPacket register-set commands (aVCD).
 *  Each command updates the local shadow via _UpdateValue and, if it produced
 *  a register write, appends its command word + sub-packet words to the
 *  packet.  The packet is prefixed with the default { NOP, FLUSH } VIF1 code,
 *  padded to a quadword, and closed; if nothing was emitted the packet is
 *  cancelled.
 * ------------------------------------------------------------------------ */
void g3dVif1SetRegister(const G3DVIF1CMDDATA *aVCD, int iNumPacket)
{
    unsigned int   *rauiPacket;
    int             iWSize;
    int             iFraction;
    int             i;
    int             j;
    const G3DVIF1CMDDATA *rVCD;
    _VIF1CMDINFO    VCI;
    /* default VIF1 code preamble: { NOP, FLUSH } */
    unsigned int    auiDefaultVif1Code[2] = { 0, 0x11000000 };

    if (iNumPacket > 0)
    {
        G3DASSERT(aVCD, "");

        rauiPacket = (unsigned int *)g3dDmaOpenPacket();
        rauiPacket[0] = auiDefaultVif1Code[0];
        rauiPacket[1] = auiDefaultVif1Code[1];

        iWSize = 2;
        for (i = 0; i < iNumPacket; i = i + 1)
        {
            rVCD = (const G3DVIF1CMDDATA *)(&aVCD[i]);
            _GetVif1CmdInfo(&VCI, rVCD);

            if (_UpdateValue(&VCI, rVCD) != 0)
            {
                rauiPacket[iWSize] = rVCD->uiCmd;
                iWSize = iWSize + 1;

                G3DASSERT(VCI.iLengthSubPacket <= G3D_MAX_VIF1SUBPACKETLENGTH, "");

                for (j = 0; j < VCI.iLengthSubPacket; j = j + 1)
                {
                    rauiPacket[iWSize] = rVCD->auiSubPacket[j];
                    iWSize = iWSize + 1;
                }
            }
        }

        if (iWSize < 3)
        {
            g3dDmaCancelPacket();
        }
        else
        {
            iFraction = 4 - (iWSize & 3);
            if (iFraction != 0 && iFraction > 0)
            {
                for (; iFraction != 0; iFraction = iFraction - 1)
                {
                    rauiPacket[iWSize] = 0;
                    iWSize = iWSize + 1;
                }
            }
            g3dDmaClosePacket((void *)((char *)rauiPacket + iWSize * 4));
        }
    }
}

/* --------------------------------------------------------------------------
 *  g3dVif1Unpack
 *
 *  Build a packet that UNPACKs iQWSize quadwords from pData into VU1
 *  micro-memory at iVu1MemAddress (V4-32, unsigned).  The source is copied
 *  inline behind the VIF1 codes so the caller's buffer can be reused.
 * ------------------------------------------------------------------------ */
int g3dVif1Unpack(int iVu1MemAddress, const void *pData, int iQWSize)
{
    int   *rQW;
    qword *pQWSrc;
    qword *pQWDst;
    int    i;

    rQW = (int *)g3dDmaOpenPacket();
    rQW[2] = 0x1000404;                                         /* STCYCLE CL=4 WL=4 */
    rQW[3] = iVu1MemAddress | iQWSize << 0x10 | 0x6c000000;     /* UNPACK V4-32 */
    rQW[0] = 0;
    rQW[1] = 0;

    pQWSrc = (qword *)pData;
    pQWDst = (qword *)rQW + 1;
    for (i = iQWSize; i > 0; i = i - 1)
    {
        (*pQWDst)[0] = (*pQWSrc)[0];
        (*pQWDst)[1] = (*pQWSrc)[1];
        (*pQWDst)[2] = (*pQWSrc)[2];
        (*pQWDst)[3] = (*pQWSrc)[3];
        pQWSrc = pQWSrc + 1;
        pQWDst = pQWDst + 1;
    }

    g3dDmaClosePacket(rQW + iQWSize * 4 + 4);
    return 1;
}
