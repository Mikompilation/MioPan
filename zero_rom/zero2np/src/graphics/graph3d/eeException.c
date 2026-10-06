/* ==========================================================================
 *  eeException.c
 *
 *  EE/IOP CPU exception reporting.  Installs a debug handler for every
 *  PS2 exception code (and a no-op DMAC handler for the three SPR/cache
 *  channels) that dumps the full register file -- status/cause/EPC plus all
 *  general-purpose registers -- to the screen console, then halts.  The IOP
 *  side mirrors the same dump from the exception block left by the IOP-side
 *  handler installed via sceExcepSetDebugIOPHandler.
 *
 *  Reconstructed from the Feb 6 2004 prototype (SLES_523.84).
 * ======================================================================== */

#include "eetypes.h"
#include "eeException.h"
#include "g3ddbg.h"
#include "g3dDebug.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <eekernel.h>          /* SetDebugHandler / AddDmacHandler2 */
#include <libdma.h>
#include <sif.h>              /* sceExcepSetDebugIOPHandler */

/* --------------------------------------------------------------------------
 *  Module state (see globals.txt).
 *
 *  s_abEnableExcCode : per-PS2EXCEPTION enable flags (all enabled by default).
 *  s_astrException   : human-readable name for each exception code.
 *  s_aiDmaCatchChannel: the DMA channels we hook with the (no-op) handler.
 * ------------------------------------------------------------------------ */
static int s_abEnableExcCode[14] =
{
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
};

static const char *s_astrException[14] =
{
    "Interrupt",
    "TLB modification exception",
    "TLB exception (load or instruction fetch)",
    "TLB exception (store)",
    "Address error exception (load or instruction fetch)",
    "Address error exception (store)",
    "Bus error exception (instruction fetch)",
    "Bus error exception (data reference: load or store)",
    "Syscall exception",
    "Breakpint exception",
    "Reserved instruction exception",
    "Coprocessor Unusable exception",
    "Arithmetic overflow exception",
    "Trap exception",
};

static int s_aiDmaCatchChannel[3] =
{
    13, 14, 15,
};

/* --------------------------------------------------------------------------
 *  _strcatf
 *
 *  printf-style append: format pStr (and its varargs) into a temporary and
 *  strcat it onto strDest.  Used to build the multi-line register dump.
 * ------------------------------------------------------------------------ */
static void _strcatf(char *strDest, const char *pStr, ...)
{
    va_list VaList;
    char str[256];

    va_start(VaList, pStr);
    vsprintf(str, pStr, VaList);
    va_end(VaList);
    strcat(strDest, str);
}

/* --------------------------------------------------------------------------
 *  _EEExceptionHandler
 *
 *  Debug handler invoked on an EE exception.  cause's ExcCode field selects
 *  the entry in the name/enable tables; if the code is enabled we disable all
 *  further reporting (so we do not recurse), build the register dump from the
 *  saved status word and the GPR save area, print it and exit.
 * ------------------------------------------------------------------------ */
static void _EEExceptionHandler(u_int stat, u_int cause, u_int epc, u_int bva,
                                u_int bpa, u_long128 *gpr)
{
    char str[512];

    if (s_abEnableExcCode[(cause & 0x7c) >> 2] != 0)
    {
        eeexceptionEnableExcCodeAll(0);
        memset(str, 0, sizeof(str));
        _strcatf(str, "*** EE Exception [%s] ***\n", s_astrException[(cause & 0x7c) >> 2]);
        _strcatf(str, "stat=0x%08x cause=0x%08x epc=0x%08x bva=0x%08x bpa=0x%08x\n",
                 stat, cause, epc, bva, bpa);
        _strcatf(str, "at  =0x%08x     v0-1=0x%08x,0x%08x \n",
                 gpr[1], gpr[2], gpr[3]);
        _strcatf(str, "a0-3=0x%08x,0x%08x,0x%08x,0x%08x\n",
                 gpr[4], gpr[5], gpr[6], gpr[7]);
        _strcatf(str, "t0-7=0x%08x,0x%08x,0x%08x,0x%08x,\n",
                 gpr[8], gpr[9], gpr[10], gpr[11]);
        _strcatf(str, "     0x%08x,0x%08x,0x%08x,0x%08x\n",
                 gpr[12], gpr[13], gpr[14], gpr[15]);
        _strcatf(str, "s0-7=0x%08x,0x%08x,0x%08x,0x%08x,\n",
                 gpr[16], gpr[17], gpr[18], gpr[19]);
        _strcatf(str, "     0x%08x,0x%08x,0x%08x,0x%08x\n",
                 gpr[20], gpr[21], gpr[22], gpr[23]);
        _strcatf(str, "t8-9=0x%08x,0x%08x     ",
                 gpr[24], gpr[25]);
        _strcatf(str, "k0-1=0x%08x,0x%08x\n",
                 gpr[26], gpr[27]);
        _strcatf(str, "gp=0x%08x sp=0x%08x fp=0x%08x ra=0x%08x\n",
                 gpr[28], gpr[29], gpr[30], gpr[31]);
        scePrintf(str);
        g3ddbgPrintConsole(str);
        exit(1);
    }
}

/* --------------------------------------------------------------------------
 *  _IOPExceptionHandler
 *
 *  Dump the IOP exception block (left at 0x004231d0 by the IOP-side handler)
 *  in the same format as the EE dump, then leave the console up forever.
 * ------------------------------------------------------------------------ */
void _IOPExceptionHandler(void *pArg, void *pAddr)
{
    int *raiReg = (int *)0x004231d0;
    char str[1024];

    memset(str, 0, sizeof(str));
    _strcatf(str, "*** IOP Exception [%s] ***\n",
             s_astrException[(raiReg[37] & 0x7c) >> 2]);
    _strcatf(str, "Module [%s] Version [%02x.%02x] Offset [0x%08x]\n",
             (char *)&raiReg[47], (int)raiReg[45] >> 8, raiReg[45] & 0xf, raiReg[46]);
    _strcatf(str, "stat=0x%08x cause=0x%08x epc=0x%08x\n",
             raiReg[34], raiReg[37], raiReg[35]);
    _strcatf(str, "at  =0x%08x     v0-1=0x%08x,0x%08x \n",
             raiReg[1], raiReg[2], raiReg[3]);
    _strcatf(str, "a0-3=0x%08x,0x%08x,0x%08x,0x%08x\n",
             raiReg[4], raiReg[5], raiReg[6], raiReg[7]);
    _strcatf(str, "t0-7=0x%08x,0x%08x,0x%08x,0x%08x,\n",
             raiReg[8], raiReg[9], raiReg[10], raiReg[11]);
    _strcatf(str, "     0x%08x,0x%08x,0x%08x,0x%08x\n",
             raiReg[12], raiReg[13], raiReg[14], raiReg[15]);
    _strcatf(str, "s0-7=0x%08x,0x%08x,0x%08x,0x%08x,\n",
             raiReg[16], raiReg[17], raiReg[18], raiReg[19]);
    _strcatf(str, "     0x%08x,0x%08x,0x%08x,0x%08x\n",
             raiReg[20], raiReg[21], raiReg[22], raiReg[23]);
    _strcatf(str, "t8-9=0x%08x,0x%08x     ",
             raiReg[24], raiReg[25]);
    _strcatf(str, "k0-1=0x%08x,0x%08x\n",
             raiReg[26], raiReg[27]);
    _strcatf(str, "gp=0x%08x sp=0x%08x fp=0x%08x ra=0x%08x\n",
             raiReg[28], raiReg[29], raiReg[30], raiReg[31]);
    _strcatf(str, "hi=0x%08x lo=0x%08x sr=0x%08x epc =0x%08x\n",
             raiReg[32], raiReg[33], raiReg[34], raiReg[35]);
    _strcatf(str, "cause=0x%08x tar=0x%08x badadr=0x%08x\n",
             raiReg[37], raiReg[38], raiReg[39]);
    _strcatf(str, "dcic=0x%08x bpc=0x%08x bpcm=0x%08x bda=0x%08x bpam=0x%08x\n",
             raiReg[40], raiReg[41], raiReg[42], raiReg[43], raiReg[44]);
    g3ddbgInfinitePrintConsole(1);
    g3ddbgPrintConsole(str);
}

/* --------------------------------------------------------------------------
 *  _DmacHandler
 *
 *  No-op handler hooked onto the SPR/cache DMA channels; just logs.
 * ------------------------------------------------------------------------ */
static int _DmacHandler(int iChan, void *pArg, void *pAddr)
{
    printf("iChan : %d, pAddr : %p\n", iChan, pAddr);
    return 0;
}

/* --------------------------------------------------------------------------
 *  _InitDmacHandler
 *
 *  Install _DmacHandler on each of the three caught DMA channels.
 * ------------------------------------------------------------------------ */
static void _InitDmacHandler(void)
{
    for (int i = 0; i < 3; i++)
    {
        AddDmacHandler2(s_aiDmaCatchChannel[i], _DmacHandler, 0, 0);
    }
}

/* --------------------------------------------------------------------------
 *  eeexceptionInitialize
 *
 *  Install _EEExceptionHandler for every enabled exception code, then hook
 *  the DMAC channels.
 * ------------------------------------------------------------------------ */
void eeexceptionInitialize(void)
{
    int i;

    for (i = 0; i <= 0xd; i++)
    {
        if (s_abEnableExcCode[i] != 0)
        {
            SetDebugHandler(i, _EEExceptionHandler);
        }
    }
    _InitDmacHandler();
}

/* --------------------------------------------------------------------------
 *  eeexceptionEnableExcCode
 *
 *  Enable/disable reporting of a single exception code.
 * ------------------------------------------------------------------------ */
void eeexceptionEnableExcCode(PS2EXCEPTION eee, int bEnable)
{
    G3DASSERT(eee < NUM_PS2EXCEPTION, "");
    s_abEnableExcCode[eee] = bEnable;
}

/* --------------------------------------------------------------------------
 *  eeexceptionEnableExcCodeAll
 *
 *  Enable/disable reporting of every exception code at once.
 * ------------------------------------------------------------------------ */
void eeexceptionEnableExcCodeAll(int bEnable)
{
    int i;

    for (i = 0xd; -1 < i; i--)
    {
        s_abEnableExcCode[i] = bEnable;
    }
}

/* --------------------------------------------------------------------------
 *  iopexceptionInitialize
 *
 *  Register the IOP-side debug exception handler for the named module.
 * ------------------------------------------------------------------------ */
void iopexceptionInitialize(IOPEXCEPTIONCREATIONDATA *pCD)
{
    sceExcepSetDebugIOPHandler(pCD->pModuleName, _IOPExceptionHandler, (void *)0x004231d0);
}
