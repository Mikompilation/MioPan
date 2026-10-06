/* ==========================================================================
 *  g3dGsWrapper.c
 *
 *  The lowest layer of the renderer: thin wrappers that drive the raw EE
 *  DMAC / VIF1 / GIF / GS registers directly.  They push a GS draw / display
 *  environment, swap the double buffer, spin-wait for the whole GS path to go
 *  idle (g3dGsSyncPath), and perform a GS->EE-local store-image read-back.
 *
 *  Every spin loop carries a fixed iteration budget (0x1000000); on overrun it
 *  dumps the relevant DMAC / VIF1 / GIF registers and reports the offending
 *  stage to the installed debug handler via _CallHandler.
 *
 *  Reconstructed from the Feb 6 2004 prototype (SLES_523.84).
 * ======================================================================== */

#include "g3dGsWrapper.h"
#include "g3dCore.h"            /* g3dSetGsRegisters */
#include "eeregs.h"
#include "miopan/gs/miopan_gs_c.h"
#include <eekernel.h>          /* scePrintf, sceGsGetIMR / sceGsPutIMR, _cfc2 */
#include <string.h>

/* The spin budget every wait loop allows before declaring a timeout. */
#define G3DGS_SPIN_LIMIT   0x1000000

/* VIF1 codes the store-image packet must carry (validated below). */
#define tSCE_VIF1_NOP       0x00000000
#define tSCE_VIF1_MSKPATH3  0x06008000      /* MSKPATH3(0x8000) */
#define tSCE_VIF1_FLUSHA    0x13000000
#define tSCE_VIF1_DIRECT(n) 0x50000000 | n      /* DIRECT(6) */

/* --------------------------------------------------------------------------
 *  Module state (see globals.txt).
 * ------------------------------------------------------------------------ */
static LPFUNC_ONDETECTPACKETDOESNOTTERMINATED s_pFuncOnDetectedPacketDoesNotTerminated;

/* MSKPATH3-off VIF1 reset packet, restored into the VIF1 FIFO after a read. */
static u_int init_mp3[4] = { 0x06000000, 0, 0, 0 };

static u_long _ReadGsReg64(const void *pReg)
{
    u_long val;

    memcpy(&val, pReg, sizeof(val));
    return val;
}

/* --------------------------------------------------------------------------
 *  _DumpPathRegs
 *
 *  The 10-register dump every g3dGsSyncPath timeout branch prints (DMAC ch.1 /
 *  ch.2 control + transfer state, VIF1 + GIF status).
 * ------------------------------------------------------------------------ */
static void _DumpPathRegs(void)
{
    scePrintf("\t<D1_CHCR=%08x:", REG_DMAC_1_VIF1_CHCR);
    scePrintf("D1_TADR=%08x:",    REG_DMAC_1_VIF1_TADR);
    scePrintf("D1_MADR=%08x:",    REG_DMAC_1_VIF1_MADR);
    scePrintf("D1_QWC=%08x>\r\n", REG_DMAC_1_VIF1_QWC);
    scePrintf("\t<D2_CHCR=%08x:", REG_DMAC_2_GIF_CHCR);
    scePrintf("D2_TADR=%08x:",    REG_DMAC_2_GIF_TADR);
    scePrintf("D2_MADR=%08x:",    REG_DMAC_2_GIF_MADR);
    scePrintf("D2_QWC=%08x>\r\n", REG_DMAC_2_GIF_QWC);
    scePrintf("\t<VIF1_STAT=%08x:", REG_VIF1_STAT);
    scePrintf("GIF_STAT=%08x>\r\n", REG_GIF_STAT);
}

/* --------------------------------------------------------------------------
 *  _SetGsDrawEnv1
 *
 *  Push the 8-register GS draw environment (FRAME/ZBUF/XYOFFSET/SCISSOR/
 *  PRMODECONT/COLCLAMP/DTHE/TEST) into the local GS-register shadow without
 *  going down the DMA chain (iDmaChan == -1).
 * ------------------------------------------------------------------------ */
static void _SetGsDrawEnv1(void *pDE1)
{
    g3dSetGsRegisters((sceGifPackAd *)pDE1, 8, -1);
}

/* --------------------------------------------------------------------------
 *  _CallHandler
 *
 *  Forward a "packet did not terminate" notification to the installed debug
 *  handler, if any.
 * ------------------------------------------------------------------------ */
static void _CallHandler(G3DGSSYNCPATHTIMEOUTREASON r)
{
    if (s_pFuncOnDetectedPacketDoesNotTerminated != 0)
    {
        s_pFuncOnDetectedPacketDoesNotTerminated(r);
    }
}

/* --------------------------------------------------------------------------
 *  g3dGsPutDrawEnv
 *
 *  Mirror the draw-env half of the GIFtag's packet into the GS shadow, then
 *  kick DMAC ch.2 (GIF) to transfer it.  Waits for any in-flight ch.2 chain to
 *  finish first; on timeout reports SPTR_D2_START.
 * ------------------------------------------------------------------------ */
int g3dGsPutDrawEnv(void *pGT)
{
    u_int vcnt;

    _SetGsDrawEnv1((void *)((int64_t)pGT + 0x10));

    vcnt = 0;
    while ((REG_DMAC_2_GIF_CHCR & 0x100) != 0)
    {
        if (vcnt > G3DGS_SPIN_LIMIT)
        {
            scePrintf("sceGsPutDrawEnv: DMA Ch.2 does not terminate\r\n");
            _CallHandler(SPTR_D2_START);
            return -1;
        }
        vcnt = vcnt + 1;
    }

    REG_DMAC_2_GIF_QWC = (*(u_int *)pGT & 0x7fff) + 1;
    if (((u_int)pGT & 0x70000000) == 0x70000000)
    {
        REG_DMAC_2_GIF_MADR = (u_int)pGT & 0xfffffff | 0x80000000;
    }
    else
    {
        REG_DMAC_2_GIF_MADR = (u_int)pGT & 0xfffffff;
    }
    REG_DMAC_2_GIF_CHCR = 0x101;
    REG_DMAC_2_GIF_CHCR &= ~0x100;

    return 0;
}

/* --------------------------------------------------------------------------
 *  g3dGsPutDispEnv
 *
 *  Write the display environment straight to the GS privileged registers.
 * ------------------------------------------------------------------------ */
void g3dGsPutDispEnv(sceGsDispEnv *pDE)
{
    REG_GS_PMODE    = _ReadGsReg64(&pDE->pmode);
    REG_GS_SMODE2   = _ReadGsReg64(&pDE->smode2);
    REG_GS_DISPFB2  = _ReadGsReg64(&pDE->dispfb);
    REG_GS_DISPLAY2 = _ReadGsReg64(&pDE->display);
    REG_GS_BGCOLOR  = _ReadGsReg64(&pDE->bgcolor);
}

/* --------------------------------------------------------------------------
 *  g3dGsSwapDBuff
 *
 *  Present buffer `id`: push its display env, then its draw env (giftag0 for
 *  even ids, giftag1 for odd).
 * ------------------------------------------------------------------------ */
int g3dGsSwapDBuff(sceGsDBuff *pDB, int id)
{
    int ret;

    g3dGsPutDispEnv(&pDB->disp[id & 1]);

    if ((id & 1) == 0)
    {
        ret = g3dGsPutDrawEnv(&pDB->giftag0);
    }
    else
    {
        ret = g3dGsPutDrawEnv(&pDB->giftag1);
    }

    return ret;
}

/* --------------------------------------------------------------------------
 *  g3dGsSyncPath
 *
 *  mode == 0: block until the whole GS path is idle -- DMAC ch.1 (VIF1), DMAC
 *  ch.2 (GIF), VIF1, VU1 and GIF, in that order.  Any stage that exceeds the
 *  spin budget dumps the path registers and reports its reason.
 *
 *  mode != 0: non-blocking poll -- return a bitmask of the busy stages
 *  (bit0 D1, bit1 D2, bit2 VIF1, bit3 VU1, bit4 GIF).
 * ------------------------------------------------------------------------ */
int g3dGsSyncPath(int mode, u_short timeout)
{
    u_int reg;
    u_int vcnt;
    int   ret;

    vcnt = 0;

    if (mode == 0)
    {
        while ((REG_DMAC_1_VIF1_CHCR & 0x100) != 0)
        {
            if (vcnt > G3DGS_SPIN_LIMIT)
            {
                scePrintf("sceGsSyncPath: DMA Ch.1 does not terminate\r\n");
                _DumpPathRegs();
                _CallHandler(SPTR_D1_START);
                return -1;
            }
            vcnt++;
        }

        while ((REG_DMAC_2_GIF_CHCR & 0x100) != 0)
        {
            if (vcnt > G3DGS_SPIN_LIMIT)
            {
                scePrintf("sceGsSyncPath: DMA Ch.2 does not terminate\r\n");
                _DumpPathRegs();
                _CallHandler(SPTR_D2_START);
                return -1;
            }
            vcnt++;
        }

        while ((REG_VIF1_STAT & 0x1f000003) != 0)
        {
            if (vcnt > G3DGS_SPIN_LIMIT)
            {
                scePrintf("sceGsSyncPath: VIF1 does not terminate\r\n");
                _DumpPathRegs();
                _CallHandler(SPTR_VIF1_ACTIVE);
                return -1;
            }
            vcnt++;
        }

        while ((_cfc2(/* VPU-STAT */) & 0x100) != 0)
        {
            if (vcnt > G3DGS_SPIN_LIMIT)
            {
                scePrintf("sceGsSyncPath: VU1 does not terminate\r\n");
                _DumpPathRegs();
                _CallHandler(SPTR_VU0_STAT);
                return -1;
            }
            vcnt++;
        }

        while ((REG_GIF_STAT & 0xc00) != 0)
        {
            if (vcnt > G3DGS_SPIN_LIMIT)
            {
                scePrintf("sceGsSyncPath: GIF does not terminate\r\n");
                _DumpPathRegs();
                _CallHandler(SPTR_GIF_STAT);
                return -1;
            }
            vcnt++;
        }

        ret = 0;
    }
    else
    {
        ret = 0;
        if ((REG_DMAC_1_VIF1_CHCR & 0x100) != 0)
        {
            ret = ret | 1;
        }
        if ((REG_DMAC_2_GIF_CHCR & 0x100) != 0)
        {
            ret = ret | 2;
        }
        if ((REG_VIF1_STAT & 0x1f000003) != 0)
        {
            ret = ret | 4;
        }
        if ((_cfc2(/* VPU-STAT */) & 0x100) != 0)
        {
            ret = ret | 8;
        }
        reg = REG_GIF_STAT;
        if ((reg & 0xc00) != 0)
        {
            ret = ret | 0x10;
        }
    }

    return ret;
}

/* --------------------------------------------------------------------------
 *  g3dGsExecStoreImage
 *
 *  Run a GS->local "store image" transfer: validate the caller's packet, kick
 *  it down DMAC ch.1, wait for the GS FINISH, flip the bus direction to
 *  GS->EE and drain the resulting quadwords from the VIF1 FIFO into dstaddr
 *  (handling the leading whole-quadword run, a partial-quadword tail and any
 *  trailing skip), then restore the VIF1 FIFO MSKPATH3 reset packet.
 *
 *  The pixel-count arithmetic depends on the GS PSM in the BITBLTBUF register;
 *  the switch below covers the formats the engine reads back (32/24/16-bit,
 *  PSMT8/8H and the PSMT4 family).
 * ------------------------------------------------------------------------ */
int g3dGsExecStoreImage(void *sp_, u_long128 *dstaddr)
{
    sceGsStoreImage *sp = (sceGsStoreImage *)sp_;

    MioPan_GsStore(sp, (unsigned char *)dstaddr);
    return 0;

    u_int   vcnt;
    int     w;
    int     h;
    int     i;
    int     dmasizeq;          /* whole-quadword run kicked by DMA   */
    int     allsizeq;          /* total quadwords (unused remainder) */
    int     rsizeq;            /* skip count after the partial tail  */
    int     remq;              /* remaining quadwords, FIFO-drained  */
    int     remb;              /* remaining bytes in the tail qword  */
    int     ah;                /* PSM-rounded height                 */
    int     sizeb;
    u_char  tmpbuf[16];
    u_long  oldIMR;
    u_int  *p;
    u_long  giftag;

    /* ----- validate the fixed VIFcode / GIFtag header ----- */
    G3DASSERT(sp->vifcode[0] == tSCE_VIF1_NOP, "");
    G3DASSERT(sp->vifcode[1] == tSCE_VIF1_MSKPATH3, "");
    G3DASSERT(sp->vifcode[2] == tSCE_VIF1_FLUSHA, "");
    G3DASSERT(sp->vifcode[3] == tSCE_VIF1_DIRECT(6), "");
    G3DASSERT(sp->giftag.NLOOP == 5, "");   /* NLOOP == 5 */
    G3DASSERT(sp->giftag.EOP == 1, "");     /* EOP == 1   */
    G3DASSERT(sp->giftag.NREG == 1, "");    /* NREG == 1 */
    G3DASSERT(sp->giftag.REGS0 == 0xe, ""); /* REGS0 A+D */
    G3DASSERT(sp->bitbltbufaddr == (long)SCE_GS_BITBLTBUF, "");
    G3DASSERT(sp->trxposaddr   == (long)SCE_GS_TRXPOS, "");
    G3DASSERT(sp->trxregaddr   == (long)SCE_GS_TRXREG, "");
    G3DASSERT(*(u_long *)&sp->finish == (u_long) 0, "");
    G3DASSERT(sp->finishaddr   == (long)SCE_GS_FINISH, "");
    G3DASSERT(*(u_long *)&sp->trxdir == SCE_GS_SET_TRXDIR(1), "");
    G3DASSERT(sp->trxdiraddr   == (long)SCE_GS_TRXDIR, "");

    /* ----- derive the read-back quadword counts from TRXREG (w,h) ----- */
    remq     = 0;
    rsizeq   = 0;
    w        = (int)((sceGsStoreImage *)sp)->trxreg.RRW;
    h        = (int)((sceGsStoreImage *)sp)->trxreg.RRH;
    sizeb    = 0;
    dmasizeq = 0;
    ah       = 0;
    remb     = 0;

    switch ((int)((sceGsStoreImage *)sp)->bitbltbuf.SPSM)
    {
        case SCE_GS_PSMCT32:      /* PSMCT32  */
        case SCE_GS_PSMZ32:      /* PSMZ32   */
        {
            sizeb = w * h * 4;
            remq  = sizeb >> 4;
            remb  = sizeb & 0xf;
            dmasizeq = remq & 0xfffffff8;
            remq     = remq & 7;
            if (remb != 0)
            {
                ah     = h + 3 & 0x1ffc;
                rsizeq = (w * ah) >> 2;
                rsizeq = ((rsizeq - dmasizeq) - remq) + -1;
            }
            break;
        }
        case SCE_GS_PSMCT24:      /* PSMCT24  */
        case SCE_GS_PSMZ24:      /* PSMZ24   */
        {
            sizeb = w * h * 3;
            remq  = sizeb >> 4;
            remb  = sizeb & 0xf;
            dmasizeq = remq & 0xfffffff8;
            remq     = remq & 7;
            if (remb != 0)
            {
                ah     = h + 0xf & 0x1ff0;
                rsizeq = ((w * ah * 3 >> 4) - dmasizeq) - remq + -1;
            }
            break;
        }
        case SCE_GS_PSMCT16:      /* PSMCT16  */
        case SCE_GS_PSMCT16S:      /* PSMCT16S */
        case SCE_GS_PSMZ16:      /* PSMZ16   */
        case SCE_GS_PSMZ16S:      /* PSMZ16S  */
        {
            sizeb = w * h * 2;
            remq  = sizeb >> 4;
            remb  = sizeb & 0xf;
            dmasizeq = remq & 0xfffffff8;
            remq     = remq & 7;
            if (remb != 0)
            {
                ah     = h + 7 & 0xfffffff8;
                rsizeq = (w * ah) >> 3;
                rsizeq = ((rsizeq - dmasizeq) - remq) + -1;
            }
            break;
        }
        case SCE_GS_PSMT8:      /* PSMT8    */
        case SCE_GS_PSMT8H:      /* PSMT8H   */
        {
            remq = (w * h) >> 4;
            remb = w * h & 0xf;
            dmasizeq = remq & 0xfffffff8;
            remq     = remq & 7;
            if (remb != 0)
            {
                ah     = h + 7 & 0xfffffff8;
                rsizeq = (w * ah) >> 4;
                rsizeq = ((rsizeq - dmasizeq) - remq) + -1;
            }
            break;
        }
        case SCE_GS_PSMT4:      /* PSMT4    */
        case SCE_GS_PSMT4HL:      /* PSMT4HL  */
        case SCE_GS_PSMT4HH:      /* PSMT4HH  */
        {
            remq = (w * h) >> 5;
            remb = ((w * h) >> 1) & 0xf;
            dmasizeq = remq & 0xfffffff8;
            remq     = remq & 7;
            if (remb != 0)
            {
                ah     = h + 7 & 0xfffffff8;
                rsizeq = (w * ah) >> 5;
                rsizeq = ((rsizeq - dmasizeq) - remq) + -1;
            }
            break;
        }
        default:
        {
            break;
        }
    }

    if (remb == 0)
    {
        dmasizeq = 0;
        ah       = h;
        rsizeq   = 0;
    }

    /* if there was a partial-quadword tail, patch TRXREG's height to ah
     * (written through the uncached address alias so the GS sees it). */
    if (remb != 0)
    {
        *(u_long *)((u_int)((int)sp + 0x40) | 0x20000000) =
            (u_long)((sceGsStoreImage *)sp)->trxreg.RRW | (u_long)ah << 0x20;
    }

    /* ----- wait for DMAC ch.1 to be free, then kick the store packet ----- */
    vcnt = 0;
    while ((REG_DMAC_1_VIF1_CHCR & 0x100) != 0)
    {
        if (vcnt > G3DGS_SPIN_LIMIT)
        {
            scePrintf("sceGsExecStoreImage: DMA Ch.1 does not terminate\r\n");
            _CallHandler(SPTR_D1_START);
            return -1;
        }
        vcnt = vcnt + 1;
    }

    oldIMR = sceGsGetIMR();
    sceGsPutIMR(oldIMR | 0x200);
    REG_GS_CSR = 2;                         /* clear FINISH */

    REG_DMAC_1_VIF1_QWC = 7;
    if (((u_int)sp & 0x70000000) == 0x70000000)
    {
        REG_DMAC_1_VIF1_MADR = (u_int)sp & 0xfffffff | 0x80000000;
    }
    else
    {
        REG_DMAC_1_VIF1_MADR = (u_int)sp & 0xfffffff;
    }
    REG_DMAC_1_VIF1_CHCR = 0x101;
    REG_DMAC_1_VIF1_CHCR &= ~0x100;

    while ((REG_DMAC_1_VIF1_CHCR & 0x100) != 0)
    {
        if (vcnt > G3DGS_SPIN_LIMIT)
        {
            scePrintf("sceGsExecStoreImage: DMA Ch.1 does not terminate\r\n");
            _CallHandler(SPTR_D1_START);
            return -1;
        }
        vcnt = vcnt + 1;
    }

    /* wait for the GS FINISH event */
    while ((REG_GS_CSR & 2) == 0)
    {
        if (vcnt > G3DGS_SPIN_LIMIT)
        {
            scePrintf("sceGsExecStoreImage: GS does not terminate\r\n");
            REG_VIF1_FIFO = *(u_long *)init_mp3;
            _CallHandler(SPTR_GS_CSR_FINISH);
            return -1;
        }
        vcnt = vcnt + 1;
    }

    REG_VIF1_STAT  = 0x800000;              /* VIF1 in FIFO-read mode */
    REG_GS_BUSDIR  = 1;                     /* GS -> EE */

    /* ----- whole-quadword run, kicked through DMAC ch.1 (GS->MEM) ----- */
    if (dmasizeq != 0)
    {
        REG_DMAC_1_VIF1_QWC = dmasizeq;
        if (((u_int)dstaddr & 0x70000000) == 0x70000000)
        {
            REG_DMAC_1_VIF1_MADR = (u_int)dstaddr & 0xfffffff | 0x80000000;
        }
        else
        {
            REG_DMAC_1_VIF1_MADR = (u_int)dstaddr & 0xfffffff;
        }
        REG_DMAC_1_VIF1_CHCR = 0x100;
        REG_DMAC_1_VIF1_CHCR &= ~0x100;

        while ((REG_DMAC_1_VIF1_CHCR & 0x100) != 0)
        {
            if (vcnt > G3DGS_SPIN_LIMIT)
            {
                scePrintf("sceGsExecStoreImage: DMA Ch.1 (GS->MEM) does not terminate\r\n");
                REG_GS_CSR    = 0x100;
                REG_GS_BUSDIR = 0;
                _CallHandler(SPTR_D1_START);
                goto restore_fifo;
            }
            vcnt = vcnt + 1;
        }
    }

    /* ----- remaining whole quadwords, drained from the VIF1 FIFO ----- */
    i = 0;
    if (remq != 0)
    {
        p = (u_int *)((u_long128 *)dstaddr + dmasizeq);
        do
        {
            while ((REG_VIF1_STAT & 0x1f000000) == 0)
            {
                if (vcnt > G3DGS_SPIN_LIMIT)
                {
                    goto fifo_underrun;
                }
                vcnt = vcnt + 1;
            }
            *(u_long *)p       = REG_VIF1_FIFO;
            *(u_long *)(p + 2) = REG_VIF1_FIFO;
            i = i + 1;
            p = p + 4;
        }
        while (i < remq);
    }

    /* ----- partial-quadword tail (remb bytes) + trailing skip ----- */
    if (remb != 0)
    {
        while ((REG_VIF1_STAT & 0x1f000000) == 0)
        {
            if (vcnt > G3DGS_SPIN_LIMIT)
            {
fifo_underrun:
                scePrintf("sceGsExecStoreImage: Enough data does not reach VIF1\n");
                REG_GS_CSR    = 0x100;
                REG_GS_BUSDIR = 0;
                REG_GIF_CTRL  = 1;
                REG_VIF1_FBRST = 1;
                _CallHandler(SPTR_VIF1_ACTIVE);
                return -1;
            }
            vcnt = vcnt + 1;
        }

        *(u_long *)&tmpbuf[0] = REG_VIF1_FIFO;
        *(u_long *)&tmpbuf[8] = REG_VIF1_FIFO;
        for (i = 0; i < remb; i = i + 1)
        {
            ((u_char *)dstaddr)[i + (dmasizeq + remq) * 0x10] = tmpbuf[i];
        }

        /* drain (and discard) the trailing skip quadwords */
        for (i = 0; i < rsizeq; i = i + 1)
        {
            while ((REG_VIF1_STAT & 0x1f000000) == 0)
            {
                if (vcnt > G3DGS_SPIN_LIMIT)
                {
                    goto fifo_underrun;
                }
                vcnt = vcnt + 1;
            }
            *(u_long *)&tmpbuf[0] = REG_VIF1_FIFO;
            *(u_long *)&tmpbuf[8] = REG_VIF1_FIFO;
        }
    }

    REG_VIF1_STAT = 0;
    REG_GS_BUSDIR = 0;
    sceGsPutIMR(oldIMR);
    REG_GS_CSR = 2;

restore_fifo:
    /* restore the MSKPATH3-off reset packet into the VIF1 FIFO */
    REG_VIF1_FIFO = *(u_long *)init_mp3;
    return 0;
}

/* --------------------------------------------------------------------------
 *  g3dGsSetDebugHandler
 *
 *  Install the "packet did not terminate" callback.
 * ------------------------------------------------------------------------ */
void g3dGsSetDebugHandler(LPFUNC_ONDETECTPACKETDOESNOTTERMINATED pFunc)
{
    s_pFuncOnDetectedPacketDoesNotTerminated = pFunc;
}
