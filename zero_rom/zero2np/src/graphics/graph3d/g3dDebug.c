/* ==========================================================================
 *  g3dDebug.c
 *
 *  The engine's debug-output and verification layer.  It implements the
 *  printf-style assert/warning machinery behind the G3DASSERT / G3DWARNING /
 *  G3DRETURN macros (g3ddbg.h): _SetLineInfo stashes the file/line/function/
 *  expression, g3ddbgAssert formats the message and forces a halt via _Assert,
 *  g3ddbgWarning formats and logs via _Warning.  It also provides the on-screen
 *  exception console (g3ddbgPrintConsole, used by eeException.c), the VU0 FP
 *  register load/store/dump helpers, and the VIF1/DMA-packet validators that
 *  walk a chain and assert on malformed VIFcodes.
 *
 *  Strings here are plain ASCII English debug text (no Shift-JIS).
 *
 *  Reconstructed from the Feb 6 2004 prototype (SLES_523.84).
 * ======================================================================== */

#include "g3dDebug.h"
#include "gra3dTypes.h"         /* tVIF_CODE / G3DDMACHAINTAG / REG_VIF1_STAT */
#include "g3dDma.h"             /* g3dGsSyncPath / g3dGsSwapDBuff */
#include "g3dGsWrapper.h"
#include "eeregs.h"
#include "g3dxVu0.h"            /* _lqc2 / _sqc2 */
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <libgraph.h>
#include <libvif.h>
#include <libdma.h>
#include <sifdev.h>

/* --------------------------------------------------------------------------
 *  Module state (see globals.txt).
 * ------------------------------------------------------------------------ */
static int             s_bPrintConsoleInfinitely;
static _G3DLINEINFO    s_LineInfo;
static sceGsDBuff      db;

/* GIFtag emitted ahead of the alpha-env DIRECT data in g3ddbgPrintConsole.
 * This symbol is absent from globals.txt / symbols.txt / ZERO2.MAP and is only
 * referenced here, so keep the placeholder file-local until its value is found. */
static u_long128       g_DirectGifTag;

static int _VSyncCallback(int);

static void _strcatf(char *strDest, const char *pStr, ...)
{
    va_list VaList;
    char str[4096];

    va_start(VaList, pStr);
    vsprintf(str, pStr, VaList);
    va_end(VaList);
    strcat(strDest, str);
}

/* --------------------------------------------------------------------------
 *  _Assert
 *
 *  Fatal-assert presenter: build the DEBUG ASSERT banner with the stashed
 *  line info (or just the message if no line info was set), print it to both
 *  the screen console and the host, then spin forever.
 * ------------------------------------------------------------------------ */
static void _Assert(char *pStr)
{
    char str[2048];

    memset(str, 0, sizeof(str));
    _strcatf(str, "========== D E B U G   A S S E R T ==========\n");
    if ((s_LineInfo.pFileName == NULL) && (s_LineInfo.iLine == 0))
    {
        _strcatf(str, "%s\n", pStr);
    }
    else
    {
        _strcatf(str, "At : %s(%d)\n", s_LineInfo.pFileName, s_LineInfo.iLine);
        _strcatf(str, "Function : %s\n", s_LineInfo.pFunctionName);
        _strcatf(str, "\"%s\" Failed\n", s_LineInfo.pExpression);
        _strcatf(str, "Message : %s\n", pStr);
    }
    _strcatf(str, "=============================================\n");
    _strcatf(str, "==== p r o g r a m   f o r c e   e x i t ====\n");
    _strcatf(str, "=============================================\n");
    g3ddbgPrintConsole(str);
    g3ddbgPrintf("%s", str);
    exit(-1);
}

/* --------------------------------------------------------------------------
 *  _Warning
 *
 *  Non-fatal presenter: log the DEBUG WARNING banner with line info.
 * ------------------------------------------------------------------------ */
static void _Warning(char *pStr)
{
    g3ddbgPrintf("--------- D E B U G   W A R N I N G ---------\n");
    g3ddbgPrintf("At : %s(%d)\n", s_LineInfo.pFileName, s_LineInfo.iLine);
    g3ddbgPrintf("Function : %s\n", s_LineInfo.pFunctionName);
    g3ddbgPrintf("\"%s\" Failed\n", s_LineInfo.pExpression);
    g3ddbgPrintf("Message : %s\n", pStr);
    g3ddbgPrintf("---------------------------------------------\n");
}

/* --------------------------------------------------------------------------
 *  _IsVifcodeMpg
 *
 *  True when the VIFcode CMD field is MPG (0x4a).
 * ------------------------------------------------------------------------ */
static int _IsVifcodeMpg(tVIF_CODE *pVC)
{
    return (pVC->cmd & 0x7f) == 0x4a;
}

/* --------------------------------------------------------------------------
 *  _IsVifcodeUnpack
 *
 *  True when the VIFcode CMD field (masked 0x6f) is one of the UNPACK forms.
 * ------------------------------------------------------------------------ */
static int _IsVifcodeUnpack(tVIF_CODE *pVC)
{
    int aiVif1CommandUnpack[13];
    int i;

    aiVif1CommandUnpack[0]  = 0x60;
    aiVif1CommandUnpack[1]  = 0x61;
    aiVif1CommandUnpack[2]  = 0x62;
    aiVif1CommandUnpack[3]  = 0x64;
    aiVif1CommandUnpack[4]  = 0x65;
    aiVif1CommandUnpack[5]  = 0x66;
    aiVif1CommandUnpack[6]  = 0x68;
    aiVif1CommandUnpack[7]  = 0x69;
    aiVif1CommandUnpack[8]  = 0x6a;
    aiVif1CommandUnpack[9]  = 0x6c;
    aiVif1CommandUnpack[10] = 0x6d;
    aiVif1CommandUnpack[11] = 0x6e;
    aiVif1CommandUnpack[12] = 0x6f;

    for (i = 0; i <= 12; i++)
    {
        if ((pVC->cmd & 0x6f) == aiVif1CommandUnpack[i])
        {
            return 1;
        }
    }
    return 0;
}

/* --------------------------------------------------------------------------
 *  _ParseDmaPacket
 *
 *  Given a VIFcode, return the address of the VIFcode that follows it
 *  (skipping its inline data).  Asserts on alignment violations and on
 *  unknown / unsupported CMDs.
 * ------------------------------------------------------------------------ */
static int *_ParseDmaPacket(tVIF_CODE *pVC)
{
    int *piNext;
    int iNumInstruct;

    g3ddbgVerifyVifCode(pVC);
    switch (pVC->cmd & 0x7f)
    {
    case SCE_VIF1_STMASK:                          /* STMASK   */
        {
            piNext = (int *)(pVC + 2);
            break;
        }
    case SCE_VIF1_STROW:                          /* STROW    */
    case SCE_VIF1_STCOL:                          /* STCOL    */
        {
            piNext = (int *)(pVC + 5);
            break;
        }
    case SCE_VIF1_MPG:                          /* MPG      */
        {
            iNumInstruct = pVC->num;
            if (iNumInstruct == 0)
            {
                iNumInstruct = 0x100;
            }
            piNext = (int *)(pVC + 1) + iNumInstruct * 2;
            break;
        }
    case SCE_VIF1_DIRECT:                          /* DIRECT   */
        {
            G3DASSERT(!((int)(pVC + 1) & 0xf), "");
            piNext = (int *)(pVC + 1 + pVC->immediate * 4);
            G3DASSERT(!((int)piNext & 0xf), "");
            break;
        }
    case SCE_VIF1_DIRECTHL:                          /* DIRECTHL */
        {
            G3DASSERT(0, "");
            piNext = NULL;
            break;
        }
    default:                            /* UNPACK group */
        {
            if (_IsVifcodeUnpack(pVC) == 0)
            {
                piNext = (int *)(pVC + 1);
            }
            else
            {
                switch (pVC->cmd & 0x6f)
                {
                case SCE_VIF1_UNPACK:              /* S-32 */
                    {
                        piNext = (int *)(pVC + pVC->num + 1);
                        break;
                    }
                case 0x64:              /* V2-32 */
                    {
                        piNext = (int *)(pVC + pVC->num * 2 + 1);
                        break;
                    }
                case 0x68:              /* V3-32 */
                    {
                        piNext = (int *)(pVC + pVC->num * 3 + 1);
                        break;
                    }
                case 0x6c:              /* V4-32 */
                    {
                        piNext = (int *)(pVC + pVC->num * 4 + 1);
                        break;
                    }
                default:
                    {
                        G3DASSERT(0, "Unknown Unpack CMD : %d(0x%x)", pVC->cmd, pVC->cmd);
                        piNext = NULL;
                        break;
                    }
                }
            }
            break;
        }
    }
    return piNext;
}

/* --------------------------------------------------------------------------
 *  g3ddbgPrintf
 *
 *  printf to the host (scePrintf) through a fixed 2 KiB buffer.
 * ------------------------------------------------------------------------ */
int g3ddbgPrintf(const char *pStr, ...)
{
    char str[2048];
    va_list VaList;

    va_start(VaList, pStr);
    vsnprintf(str, sizeof(str), pStr, VaList);
    va_end(VaList);
    str[sizeof(str) - 1] = '\0';
    return scePrintf("%s", str);
}

/* --------------------------------------------------------------------------
 *  g3ddbgPrintReturn
 *
 *  Print the G3DRETURN/G3DRETURNVAL trailer using the current line info.  The
 *  warning call clears s_LineInfo, so the macros re-stash it before calling
 *  this helper.
 * ------------------------------------------------------------------------ */
void g3ddbgPrintReturn(int has_retval, int retval)
{
    const char *pFileName;
    const char *pFunctionName;
    const char *pExpression;

    pFileName = s_LineInfo.pFileName != 0 ? s_LineInfo.pFileName : "";
    pFunctionName = s_LineInfo.pFunctionName != 0 ? s_LineInfo.pFunctionName : "";
    pExpression = s_LineInfo.pExpression != 0 ? s_LineInfo.pExpression : "";

    if (has_retval != 0)
    {
        g3ddbgPrintf("[G3DRETURN:%d]%s(%d)(%s):%s\n",
                     retval, pFileName, s_LineInfo.iLine, pFunctionName,
                     pExpression);
    }
    else
    {
        g3ddbgPrintf("[G3DRETURN]%s(%d)(%s):%s\n",
                     pFileName, s_LineInfo.iLine, pFunctionName, pExpression);
    }

    memset(&s_LineInfo, 0, sizeof(s_LineInfo));
}

/* --------------------------------------------------------------------------
 *  g3ddbgDumpMemoryCompare
 *
 *  Byte-compare two buffers; if they differ, dump a nibble-by-nibble diff
 *  ('-' equal, '*' differs) in 16-byte rows with both base addresses.
 *  Returns non-zero if any nibble differed.
 * ------------------------------------------------------------------------ */
int g3ddbgDumpMemoryCompare(void *p0, void *p1, int iSize)
{
    int bRet;
    int i;

    bRet = 0;
    if (memcmp(p0, p1, iSize) != 0)
    {
        g3ddbgPrintf("---_DumpMemoryCompare start---\n");
        for (i = 0; i < iSize; i++)
        {
            if ((i & 0xf) == 0)
            {
                g3ddbgPrintf("0x%08x ", i);
            }
            if ((((u_char *)p0)[i] & 0xf0) == (((u_char *)p1)[i] & 0xf0))
            {
                g3ddbgPrintf("-");
            }
            else
            {
                bRet = 1;
                g3ddbgPrintf("*");
            }
            if ((((u_char *)p0)[i] & 0xf) == (((u_char *)p1)[i] & 0xf))
            {
                g3ddbgPrintf("-");
            }
            else
            {
                bRet = 1;
                g3ddbgPrintf("*");
            }
            if (((i + 1) & 0xf) == 0)
            {
                g3ddbgPrintf(" 0x%08x", (char *)p0 + (i & ~0xf));
                g3ddbgPrintf(" 0x%08x", (char *)p1 + (i & ~0xf));
                g3ddbgPrintf("\n");
            }
            else if (((i + 1) & 3) == 0)
            {
                g3ddbgPrintf(" ");
            }
        }
    }
    return bRet;
}

/* --------------------------------------------------------------------------
 *  g3ddbgAssert
 *
 *  G3DASSERT back end: when the condition failed (b == false) format the
 *  variadic message and hand it to the fatal _Assert presenter.
 * ------------------------------------------------------------------------ */
void g3ddbgAssert(int b, const char *pStr, ...)
{
    char str[2048];
    va_list VaList;

    if (!b)
    {
        va_start(VaList, pStr);
        vsprintf(str, pStr, VaList);
        va_end(VaList);
        _Assert(str);
    }
}

/* --------------------------------------------------------------------------
 *  g3ddbgWarning
 *
 *  G3DWARNING back end: format the variadic message, log it via _Warning,
 *  then clear the stashed line info.
 * ------------------------------------------------------------------------ */
void g3ddbgWarning(int b, const char *pStr, ...)
{
    char str[2048];
    va_list VaList;

    if (!b)
    {
        va_start(VaList, pStr);
        vsprintf(str, pStr, VaList);
        va_end(VaList);
        _Warning(str);
        memset(&s_LineInfo, 0, sizeof(s_LineInfo));
    }
}

/* --------------------------------------------------------------------------
 *  _SetLineInfo
 *
 *  Stash __FILE__/__LINE__/__FUNCTION__ and the stringized condition for the
 *  next assert/warning (called by the G3DASSERT/G3DWARNING macros).
 * ------------------------------------------------------------------------ */
void _SetLineInfo(const char *pFileName, int iLine, const char *pFunctionName, const char *pExpression)
{
    s_LineInfo.pExpression   = pExpression;
    s_LineInfo.pFileName     = pFileName;
    s_LineInfo.iLine         = iLine;
    s_LineInfo.pFunctionName = pFunctionName;
}

/* --------------------------------------------------------------------------
 *  _GetLineInfo
 * ------------------------------------------------------------------------ */
_G3DLINEINFO *_GetLineInfo(void)
{
    return &s_LineInfo;
}

/* --------------------------------------------------------------------------
 *  g3ddbgWaitVU1
 *
 *  Spin until VU1 (COP2) clears its busy condition.
 * ------------------------------------------------------------------------ */
void g3ddbgWaitVU1(void)
{
    while (getCopCondition(2, 0) != 0)
    {
    }
}

/* --------------------------------------------------------------------------
 *  g3ddbgDumpVu1MicroMemory / DispVUMemory
 *
 *  Stubbed out in the prototype.
 * ------------------------------------------------------------------------ */
void g3ddbgDumpVu1MicroMemory(void)
{
}

void DispVUMemory(void)
{
}

/* --------------------------------------------------------------------------
 *  _PrintVector
 *
 *  Print a 4-float vector with its name and the current line info.
 * ------------------------------------------------------------------------ */
void _PrintVector(float *fv, char *pValName)
{
    g3ddbgPrintf("_PrintVector(%s):%s(%d)", pValName, s_LineInfo.pFileName, s_LineInfo.iLine);
    g3ddbgPrintf("%8.6f,%8.6f,%8.6f,%8.6f\n", (double)fv[0], (double)fv[1], (double)fv[2], (double)fv[3]);
}

/* --------------------------------------------------------------------------
 *  _PrintMatrix
 *
 *  Print a 4x4 float matrix row by row.
 * ------------------------------------------------------------------------ */
void _PrintMatrix(float fmat[4][4], char *pValName)
{
    int i;

    g3ddbgPrintf("_PrintMatrix(%s):%s(%d)\n", pValName, s_LineInfo.pFileName, s_LineInfo.iLine);
    for (i = 0; i < 4; i++)
    {
        g3ddbgPrintf("m[%d] : %8.2f, %8.2f, %8.2f, %8.2f\n", i,
                     (double)fmat[i][0], (double)fmat[i][1], (double)fmat[i][2], (double)fmat[i][3]);
    }
}

/* --------------------------------------------------------------------------
 *  g3ddbgLoadVu0FloatingPointRegisters
 *
 *  Load all 32 VU0 FP registers from a 32x4 float array.
 * ------------------------------------------------------------------------ */
void g3ddbgLoadVu0FloatingPointRegisters(float av[][4])
{
    /* On the EE this _lqc2'd each row into the physical vf0..vf31 registers.
       The host has no VU0 register file to load into, so this is a no-op. */
    (void)av;
}

/* --------------------------------------------------------------------------
 *  g3ddbgStoreVu0FloatingPointRegisters
 *
 *  Store all 32 VU0 FP registers into a 32x4 float array.
 * ------------------------------------------------------------------------ */
void g3ddbgStoreVu0FloatingPointRegisters(float av[][4])
{
    int i, j;

    /* On the EE this _sqc2'd the physical vf0..vf31 registers into av.  The
       host has no VU0 register file to read back, so report zeros — a defined
       snapshot rather than uninitialised memory for any subsequent dump. */
    for (i = 0; i < 32; i++)
        for (j = 0; j < 4; j++)
            av[i][j] = 0.0f;
}

/* --------------------------------------------------------------------------
 *  g3ddbgDumpVu0FloatingPointRegisters
 *
 *  Dump the 32 VU0 FP registers (live registers if av is NULL, else the
 *  supplied snapshot) one row per register.  Each lane prints with a wide
 *  format when non-zero and a terse one when zero.
 * ------------------------------------------------------------------------ */
void g3ddbgDumpVu0FloatingPointRegisters(float av[][4])
{
    float vf[32][4];
    int i;
    int j;
    char *pFormat;
    float f;

    if (av == NULL)
    {
        g3ddbgStoreVu0FloatingPointRegisters(vf);
    }
    else
    {
        for (i = 0; i < 32; i++)
        {
            vf[i][0] = av[i][0];
            vf[i][1] = av[i][1];
            vf[i][2] = av[i][2];
            vf[i][3] = av[i][3];
        }
    }

    g3ddbgPrintf("--- g3ddbgDumpVu0FloatingPointRegisters ---\n");
    for (i = 0; i < 32; i++)
    {
        g3ddbgPrintf("v0f%02d ", i);
        for (j = 3; 0 <= j; j--)
        {
            f = vf[i][j];
            if (f != 0.0)
            {
                pFormat = "%8.7f";
            }
            else
            {
                pFormat = "%1.1f       ";
            }
            g3ddbgPrintf(pFormat, (double)f);
            g3ddbgPrintf(" ");
        }
        g3ddbgPrintf("\n");
    }
}

/* --------------------------------------------------------------------------
 *  g3ddbgVerifyVu1MemAddress / g3ddbgVerifyGsRegisterAddress
 *
 *  Layout cross-checks compiled out in the prototype (the __FUNCTION__
 *  string is the only residue).
 * ------------------------------------------------------------------------ */
void g3ddbgVerifyVu1MemAddress(void)
{
}

void g3ddbgVerifyGsRegisterAddress(void)
{
}

/* --------------------------------------------------------------------------
 *  g3ddbgVerifyVifCode
 *
 *  Assert that a VIFcode CMD is one of the recognized opcodes (or an UNPACK).
 * ------------------------------------------------------------------------ */
void g3ddbgVerifyVifCode(tVIF_CODE *pVC)
{
    int aiVif1Command[20];
    int i;

    aiVif1Command[0]  = 0x00;           /* NOP        */
    aiVif1Command[1]  = 0x01;           /* STCYCL     */
    aiVif1Command[2]  = 0x02;           /* OFFSET     */
    aiVif1Command[3]  = 0x03;           /* BASE       */
    aiVif1Command[4]  = 0x04;           /* ITOP       */
    aiVif1Command[5]  = 0x05;           /* STMOD      */
    aiVif1Command[6]  = 0x06;           /* MSKPATH3   */
    aiVif1Command[7]  = 0x07;           /* MARK       */
    aiVif1Command[8]  = 0x10;           /* FLUSHE     */
    aiVif1Command[9]  = 0x11;           /* FLUSH      */
    aiVif1Command[10] = 0x13;           /* FLUSHA     */
    aiVif1Command[11] = 0x14;           /* MSCAL      */
    aiVif1Command[12] = 0x17;           /* MSCNT      */
    aiVif1Command[13] = 0x15;           /* MSCALF     */
    aiVif1Command[14] = 0x20;           /* STMASK     */
    aiVif1Command[15] = 0x30;           /* STROW      */
    aiVif1Command[16] = 0x31;           /* STCOL      */
    aiVif1Command[17] = 0x4a;           /* MPG        */

    for (i = 0; i <= 0x13; i++)
    {
        if ((pVC->cmd & 0x7f) == aiVif1Command[i])
        {
            return;
        }
    }

    if (_IsVifcodeUnpack(pVC) != 0)
    {
        return;
    }

    G3DASSERT(0, "illegal VifCode Command : %d", pVC->cmd);
}

/* --------------------------------------------------------------------------
 *  g3ddbgVerifyDmaPacket
 *
 *  Walk a single source-chain DMA tag's VIF payload, validating every
 *  VIFcode and checking that the data length matches the tag's QWC.
 * ------------------------------------------------------------------------ */
void g3ddbgVerifyDmaPacket(void *pDmaPacket)
{
    G3DDMACHAINTAG *pDCT;
    int *pSendData;
    int *piCur;
    int iSize;

    pDCT = (G3DDMACHAINTAG *)pDmaPacket;
    piCur = (int *)((char *)pDmaPacket + 8);
    while ((int)piCur - (int)pDmaPacket < 0x10)
    {
        if (_IsVifcodeMpg((tVIF_CODE *)piCur) != 0)
        {
            return;
        }
        piCur = _ParseDmaPacket((tVIF_CODE *)piCur);
    }

    if ((((pDCT->ID & 0x70000000) == 0x30000000) || ((pDCT->ID & 0x70000000) == 0)) &&
        (0x10 < (int)piCur - (int)pDmaPacket))
    {
        G3DASSERT(0, "");
    }

    switch (pDCT->ID & 0x70000000)
    {
    case 0x10000000:                    /* CNT  */
        {
            iSize = pDCT->QWC;
            pSendData = (int *)((char *)pDmaPacket + 0x10);
            break;
        }
    case 0x00000000:                    /* REFE */
    case 0x30000000:                    /* REF  */
        {
            piCur = (int *)pDCT->ADDR;
            iSize = pDCT->QWC;
            pSendData = piCur;
            G3DASSERT(pDCT->ADDR, "");
            break;
        }
    default:
        {
            G3DASSERT(0, "");
            pSendData = (int *)pDmaPacket;
            iSize = 0x10;
            break;
        }
    }

    while (iSize != (int)piCur - (int)pSendData)
    {
        G3DASSERT(iSize > (int)piCur - (int)pSendData, "");
        if (_IsVifcodeMpg((tVIF_CODE *)piCur) != 0)
        {
            break;
        }
        piCur = _ParseDmaPacket((tVIF_CODE *)piCur);
    }
}

/* --------------------------------------------------------------------------
 *  g3ddbgVerifyDmaBuffer
 *
 *  Walk a whole DMA source chain, verifying each tag's packet and warning
 *  on tag IDs that should not appear in a source chain.
 * ------------------------------------------------------------------------ */
void g3ddbgVerifyDmaBuffer(void *pBuffer)
{
    G3DDMACHAINTAG *pDCT;

    for (;;)
    {
        pDCT = (G3DDMACHAINTAG *)pBuffer;
        printf("0x%08x:0x%08x, id:%d", pDCT->ADDR, pDCT->QWC, pDCT->ID);
        g3ddbgVerifyDmaPacket(pBuffer);

        switch (pDCT->ID)
        {
        case 0:                         /* REFE */
            {
                G3DWARNING(0, "");
                break;
            }
        case 1:                         /* CNT  */
            {
                pBuffer = (char *)pBuffer + (pDCT->QWC * 2 + 2) * 8;
                break;
            }
        case 2:                         /* NEXT */
            {
                G3DWARNING(0, "");
                break;
            }
        case 3:                         /* REF  */
            {
                pBuffer = (char *)pBuffer + 0x10;
                break;
            }
        case 4:                         /* REFS */
            {
                G3DWARNING(0, "");
                break;
            }
        case 5:                         /* CALL */
            {
                G3DWARNING(0, "");
                break;
            }
        case 6:                         /* RET  */
            {
                G3DWARNING(0, "");
                break;
            }
        case 7:                         /* END  */
            {
                G3DWARNING(0, "");
                break;
            }
        default:
            {
                break;
            }
        }
        
        printf("\n");
    }
}

/* --------------------------------------------------------------------------
 *  g3ddbgDumpVif1Stat
 *
 *  Decode and print the VIF1_STAT register field by field.
 * ------------------------------------------------------------------------ */
void g3ddbgDumpVif1Stat(void)
{
    printf("----==== VIF1_STAT ====----\n");

    printf("VIF1 pipeline status : ");
    switch (REG_VIF1_STAT & 3)
    {
    case 0:
        {
            printf("idle");
            break;
        }
    case 1:
        {
            printf("waiting for data");
            break;
        }
    case 2:
        {
            printf("decode");
            break;
        }
    case 3:
        {
            printf("processing");
            break;
        }
    }
    printf("\n");

    printf("VIF1 E-bit wait : ");
    if (((REG_VIF1_STAT >> 2) & 1) == 0)
    {
        printf("not-wait");
    }
    else
    {
        printf("wait");
    }
    printf("\n");

    printf("VIF1 GIF wait : ");
    if (((REG_VIF1_STAT >> 3) & 1) == 0)
    {
        printf("not-wait");
    }
    else
    {
        printf("wait");
    }
    printf("\n");

    printf("VIF1 MARK detect : ");
    if (((REG_VIF1_STAT >> 6) & 1) == 0)
    {
        printf("not-detect");
    }
    else
    {
        printf("detect");
    }
    printf("\n");

    printf("Duble Buffer Flag : ");
    if (((REG_VIF1_STAT >> 23) & 1) == 0)
    {
        printf("TOPS=BASE");
    }
    else
    {
        printf("TOPS=BASE+OFFSET");
    }
    printf("\n");

    printf("VIF1 stop stall : ");
    if (((REG_VIF1_STAT >> 8) & 1) == 0)
    {
        printf("not-stall");
    }
    else
    {
        printf("stall");
    }
    printf("\n");

    printf("VIF1 ForceBreak stall : ");
    if (((REG_VIF1_STAT >> 9) & 1) == 0)
    {
        printf("not-stall");
    }
    else
    {
        printf("stall");
    }
    printf("\n");

    printf("VIF1 interrupt stall : ");
    if (((REG_VIF1_STAT >> 10) & 1) == 0)
    {
        printf("not-stall");
    }
    else
    {
        printf("stall");
    }
    printf("\n");

    printf("Interrupt bit detected flag : ");
    if (((REG_VIF1_STAT >> 11) & 1) == 0)
    {
        printf("not-detect");
    }
    else
    {
        printf("detect");
    }
    printf("\n");

    printf("Mismatch Error detected flag : ");
    if (((REG_VIF1_STAT >> 12) & 1) == 0)
    {
        printf("no error");
    }
    else
    {
        printf("error");
    }
    printf("\n");

    printf("Reserved Instruction Error detected flag : ");
    if (((REG_VIF1_STAT >> 13) & 1) == 0)
    {
        printf("not-detect");
    }
    else
    {
        printf("detect");
    }
    printf("\n");

    printf("VIF1-FIFO direction : ");
    if (((REG_VIF1_STAT >> 23) & 1) == 0)
    {
        printf("Main memory/SPRAM -> VIF1");
    }
    else
    {
        printf("VIF1 -> Main memory/SPRAM");
    }
    printf("\n");

    printf("VIF1-FIFO valid data counter : %d qword", (REG_VIF1_STAT >> 24) & 0x1f);
    printf("\n");
}

/* --------------------------------------------------------------------------
 *  g3ddbgDumpVif1Code
 *
 *  Print the current VIF1_CODE register's CMD/NUM/IMMEDIATE fields.
 * ------------------------------------------------------------------------ */
void g3ddbgDumpVif1Code(void)
{
    tVIF_CODE *pVif1Code;

    pVif1Code = (tVIF_CODE *)&REG_VIF1_CODE;
    printf("----==== VIF1_CODE ====----\n");
    printf(" CMD : %d, num : %d, immediate : %d, ( 0x%08x )\n", pVif1Code->cmd, pVif1Code->num, pVif1Code->immediate, *(int *)pVif1Code);
}

/* --------------------------------------------------------------------------
 *  g3ddbgInfinitePrintConsole
 *
 *  When set, g3ddbgPrintConsole keeps refreshing the screen forever.
 * ------------------------------------------------------------------------ */
void g3ddbgInfinitePrintConsole(int b)
{
    s_bPrintConsoleInfinitely = b;
}

/* --------------------------------------------------------------------------
 *  g3ddbgPrintConsole
 *
 *  Take over the GS, reset the graph/DMA paths, set up a default double
 *  buffer and alpha env, then draw the supplied text via the exception
 *  console (used by the EE/IOP exception dumps).  Loops forever when the
 *  infinite flag is set, otherwise renders both buffers and returns.
 * ------------------------------------------------------------------------ */
void g3ddbgPrintConsole(char *pStr)
{
    char str[1024];
    int frame;
    sceVif1Packet packet;
    sceDmaEnv env;
    sceDmaChan *p1;
    u_long giftagAD[2];
    u_long128 buff[10];
    int iLen;

    memset(str, 0, sizeof(str));
    pStr[0x3ff] = '\0';
    /* The ROM hands vsprintf the uninitialised giftagAD as its va_list --
     * harmless on the EE, where every caller passes finished text.  On the
     * host the cast is undefined, and does not compile at all where va_list
     * is an array type (glibc x86-64), so copy the text the way vsprintf
     * would for a string with no conversions in it. */
    snprintf(str, sizeof(str), "%s", pStr);

    g3dGsSyncPath(0, 0);
    sceGsSyncVCallback((sceGsVCallbackFunc)_VSyncCallback);
    sceGsSyncV(0);

    sceGsResetPath();
    sceDmaReset(1);
    sceVif1PkInit(&packet, (u_int)buff | 0x20000000);
    sceDmaGetEnv(&env);
    env.notify = 2;
    sceDmaPutEnv(&env);
    p1 = sceDmaGetChan(1);
    p1->chcr.bits |= 0x40;

    sceGsResetGraph(0, SCE_GS_INTERLACE, SCE_GS_PAL, SCE_GS_FRAME);
    sceGsSetDefDBuff(&db, 0, 0x280, 0xe0, 2, SCE_GS_PSMCT32, 1);

    sceVif1PkReset(&packet);
    sceVif1PkCnt(&packet, 0);
    sceVif1PkOpenDirectCode(&packet, 0);
    sceVif1PkOpenGifTag(&packet, &g_DirectGifTag);
    iLen = sceGsSetDefAlphaEnv((u_long128 *)packet.pCurrent, 0);
    sceVif1PkReserve(&packet, iLen << 2);
    sceVif1PkCloseGifTag(&packet);
    sceVif1PkCloseDirectCode(&packet);
    sceVif1PkEnd(&packet, 0);
    sceVif1PkTerminate(&packet);
    sceDmaSend(p1, (void *)((u_int)packet.pBase & 0x8fffffff));
    g3dGsSyncPath(0, 0);
    while (sceGsSyncV(0) == 0)
    {
    }

    sceExcepConsOpen(0x6d00, 0x7980, 0x4d, 0x1b);
    memset(&db.disp[0].pmode, 0, sizeof(db.disp[0].pmode));
    memset(&db.disp[1].pmode, 0, sizeof(db.disp[1].pmode));
    memset(&db.draw0.dthe, 0, sizeof(db.draw0.dthe));
    memset(&db.draw1.dthe, 0, sizeof(db.draw1.dthe));
    frame = 0;
    do
    {
        sceExcepConsLocate(0, 0);
        sceExcepConsPrintf("%s", str);
        if ((frame & 1) == 0)
        {
            sceGsSetHalfOffset(&db.draw0, 0x800, 0x800, sceGsSyncV(0) ^ 1);
        }
        else
        {
            sceGsSetHalfOffset(&db.draw1, 0x800, 0x800, sceGsSyncV(0) ^ 1);
        }
        FlushCache(0);
        g3dGsSyncPath(0, 0);
        g3dGsSwapDBuff(&db, frame);
        frame++;
        break; // yeah I dont really want my console to be spammed
    } while ((s_bPrintConsoleInfinitely != 0) || (frame != 2));
}

/* --------------------------------------------------------------------------
 *  _VSyncCallback
 *
 *  Empty VSync callback installed during console takeover.
 * ------------------------------------------------------------------------ */
static int _VSyncCallback(int)
{
    return 0;
}
