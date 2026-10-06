/* ==========================================================================
 *  sce_gs.h  (SCE EE GS / VIF1 register types)
 *
 *  The PS2 GS privileged/general registers are 64-bit; the EE SDK exposes one
 *  bitfield struct per register (sceGsXxx).  The VIF1 registers (tVIFx) and the
 *  GS privileged registers exposed to the EE (tGS_xxx) follow the same idea.
 *  The bit layouts here are reproduced verbatim from the prototype's debug type
 *  info (types.txt) -- the engine reads/writes individual bitfields (e.g.
 *  TEX0.PSM, VIFcode.CMD), so the real structs are required, not a scalar shim.
 *
 *  ABI note: on the EE (ee-gcc) `long`/`long unsigned int` is 64-bit, so the
 *  GS register structs below are 8 bytes and the VIF/`unsigned int` ones 4.
 *
 *  Reconstructed from the Feb 6 2004 prototype (SLES_523.84).
 * ======================================================================== */

#ifndef _SCE_GS_H
#define _SCE_GS_H

#include "eetypes.h"

/* ---- 64-bit GS general registers (verbatim from types.txt) ------------- */
struct sceGsPrim                               /* 0x8 */
{
    u_long PRIM  : 3;                          /* 0x0:0 */
    u_long IIP   : 1;                          /* 0x0:3 */
    u_long TME   : 1;                          /* 0x0:4 */
    u_long FGE   : 1;                          /* 0x0:5 */
    u_long ABE   : 1;                          /* 0x0:6 */
    u_long AA1   : 1;                          /* 0x0:7 */
    u_long FST   : 1;                          /* 0x1:0 */
    u_long CTXT  : 1;                          /* 0x1:1 */
    u_long FIX   : 1;                          /* 0x1:2 */
    u_long pad11 : 53;                         /* 0x1:3 */
};
typedef struct sceGsPrim sceGsPrim;

struct sceGsRgbaq                              /* 0x8 */
{
    u_int R : 8;                               /* 0x0:0 */
    u_int G : 8;                               /* 0x1:0 */
    u_int B : 8;                               /* 0x2:0 */
    u_int A : 8;                               /* 0x3:0 */
    float Q;                                   /* 0x4 */
};
typedef struct sceGsRgbaq sceGsRgbaq;

struct sceGsUv                                 /* 0x8 */
{
    u_long U     : 14;                         /* 0x0:0 */
    u_long pad14 : 2;                          /* 0x1:6 */
    u_long V     : 14;                         /* 0x2:0 */
    u_long pad30 : 34;                         /* 0x3:6 */
};
typedef struct sceGsUv sceGsUv;

struct sceGsXyzf                               /* 0x8 */
{
    u_long X : 16;                             /* 0x0:0 */
    u_long Y : 16;                             /* 0x2:0 */
    u_long Z : 24;                             /* 0x4:0 */
    u_long F : 8;                              /* 0x7:0 */
};
typedef struct sceGsXyzf sceGsXyzf;

/* GS ST texture coordinate, accessed as a float pair (S, T). */
struct sceGsSt                                 /* 0x8 */
{
    float S;                                   /* 0x0 */
    float T;                                   /* 0x4 */
};
typedef struct sceGsSt sceGsSt;

/* GS XYZ primitive coordinate (12.4 fixed-point xy, 32-bit z). */
struct sceGsXyz                                /* 0x8 */
{
    u_long X : 16;                             /* 0x0:0 */
    u_long Y : 16;                             /* 0x2:0 */
    u_long Z : 32;                             /* 0x4:0 */
};
typedef struct sceGsXyz sceGsXyz;

struct sceGsTex0                               /* 0x8 */
{
    u_long TBP0  : 14;                         /* 0x0:0 */
    u_long TBW   : 6;                          /* 0x1:6 */
    u_long PSM   : 6;                          /* 0x2:4 */
    u_long TW    : 4;                          /* 0x3:2 */
    u_long TH    : 4;                          /* 0x3:6 */
    u_long TCC   : 1;                          /* 0x4:2 */
    u_long TFX   : 2;                          /* 0x4:3 */
    u_long CBP   : 14;                         /* 0x4:5 */
    u_long CPSM  : 4;                          /* 0x6:3 */
    u_long CSM   : 1;                          /* 0x6:7 */
    u_long CSA   : 5;                          /* 0x7:0 */
    u_long CLD   : 3;                          /* 0x7:5 */
};
typedef struct sceGsTex0 sceGsTex0;

struct sceGsTex1                               /* 0x8 */
{
    u_long LCM   : 1;                          /* 0x0:0 */
    u_long pad01 : 1;                          /* 0x0:1 */
    u_long MXL   : 3;                          /* 0x0:2 */
    u_long MMAG  : 1;                          /* 0x0:5 */
    u_long MMIN  : 3;                          /* 0x0:6 */
    u_long MTBA  : 1;                          /* 0x1:1 */
    u_long pad10 : 9;                          /* 0x1:2 */
    u_long L     : 2;                          /* 0x2:3 */
    u_long pad21 : 11;                         /* 0x2:5 */
    u_long K     : 12;                         /* 0x4:0 */
    u_long pad44 : 20;                         /* 0x5:4 */
};
typedef struct sceGsTex1 sceGsTex1;

struct sceGsTex2                               /* 0x8 */
{
    u_long pad00 : 20;                         /* 0x0:0 */
    u_long PSM   : 6;                          /* 0x2:4 */
    u_long pad26 : 11;                         /* 0x3:2 */
    u_long CBP   : 14;                         /* 0x4:5 */
    u_long CPSM  : 4;                          /* 0x6:3 */
    u_long CSM   : 1;                          /* 0x6:7 */
    u_long CSA   : 5;                          /* 0x7:0 */
    u_long CLD   : 3;                          /* 0x7:5 */
};
typedef struct sceGsTex2 sceGsTex2;

struct sceGsClamp                              /* 0x8 */
{
    u_long WMS   : 2;                          /* 0x0:0 */
    u_long WMT   : 2;                          /* 0x0:2 */
    u_long MINU  : 10;                         /* 0x0:4 */
    u_long MAXU  : 10;                         /* 0x1:6 */
    u_long MINV  : 10;                         /* 0x3:0 */
    u_long MAXV  : 10;                         /* 0x4:2 */
    u_long pad44 : 20;                         /* 0x5:4 */
};
typedef struct sceGsClamp sceGsClamp;

struct sceGsFog                                /* 0x8 */
{
    u_long pad00 : 56;                         /* 0x0:0 */
    u_long F     : 8;                          /* 0x7:0 */
};
typedef struct sceGsFog sceGsFog;

struct sceGsXyoffset                           /* 0x8 */
{
    u_long OFX   : 16;                         /* 0x0:0 */
    u_long pad16 : 16;                         /* 0x2:0 */
    u_long OFY   : 16;                         /* 0x4:0 */
    u_long pad48 : 16;                         /* 0x6:0 */
};
typedef struct sceGsXyoffset sceGsXyoffset;

struct sceGsPrmodecont                         /* 0x8 */
{
    u_long AC    : 1;                          /* 0x0:0 */
    u_long pad01 : 63;                         /* 0x0:1 */
};
typedef struct sceGsPrmodecont sceGsPrmodecont;

struct sceGsPrmode                             /* 0x8 */
{
    u_long pad00 : 3;                          /* 0x0:0 */
    u_long IIP   : 1;                          /* 0x0:3 */
    u_long TME   : 1;                          /* 0x0:4 */
    u_long FGE   : 1;                          /* 0x0:5 */
    u_long ABE   : 1;                          /* 0x0:6 */
    u_long AA1   : 1;                          /* 0x0:7 */
    u_long FST   : 1;                          /* 0x1:0 */
    u_long CTXT  : 1;                          /* 0x1:1 */
    u_long FIX   : 1;                          /* 0x1:2 */
    u_long pad11 : 53;                         /* 0x1:3 */
};
typedef struct sceGsPrmode sceGsPrmode;

struct sceGsTexclut                            /* 0x8 */
{
    u_long CBW   : 6;                          /* 0x0:0 */
    u_long COU   : 6;                          /* 0x0:6 */
    u_long COV   : 10;                         /* 0x1:4 */
    u_long pad22 : 42;                         /* 0x2:6 */
};
typedef struct sceGsTexclut sceGsTexclut;

struct sceGsScanmsk                            /* 0x8 */
{
    u_long MSK   : 2;                          /* 0x0:0 */
    u_long pad02 : 62;                         /* 0x0:2 */
};
typedef struct sceGsScanmsk sceGsScanmsk;

struct sceGsMiptbp1                            /* 0x8 */
{
    u_long TBP1  : 14;                         /* 0x0:0 */
    u_long TBW1  : 6;                          /* 0x1:6 */
    u_long TBP2  : 14;                         /* 0x2:4 */
    u_long TBW2  : 6;                          /* 0x4:2 */
    u_long TBP3  : 14;                         /* 0x5:0 */
    u_long TBW3  : 6;                          /* 0x6:6 */
    u_long pad60 : 4;                          /* 0x7:4 */
};
typedef struct sceGsMiptbp1 sceGsMiptbp1;

struct sceGsMiptbp2                            /* 0x8 */
{
    u_long TBP4  : 14;                         /* 0x0:0 */
    u_long TBW4  : 6;                          /* 0x1:6 */
    u_long TBP5  : 14;                         /* 0x2:4 */
    u_long TBW5  : 6;                          /* 0x4:2 */
    u_long TBP6  : 14;                         /* 0x5:0 */
    u_long TBW6  : 6;                          /* 0x6:6 */
    u_long pad60 : 4;                          /* 0x7:4 */
};
typedef struct sceGsMiptbp2 sceGsMiptbp2;

struct sceGsTexa                               /* 0x8 */
{
    u_long TA0   : 8;                          /* 0x0:0 */
    u_long pad08 : 7;                          /* 0x1:0 */
    u_long AEM   : 1;                          /* 0x1:7 */
    u_long pad16 : 16;                         /* 0x2:0 */
    u_long TA1   : 8;                          /* 0x4:0 */
    u_long pad40 : 24;                         /* 0x5:0 */
};
typedef struct sceGsTexa sceGsTexa;

struct sceGsFogcol                             /* 0x8 */
{
    u_long FCR   : 8;                          /* 0x0:0 */
    u_long FCG   : 8;                          /* 0x1:0 */
    u_long FCB   : 8;                          /* 0x2:0 */
    u_long pad24 : 40;                         /* 0x3:0 */
};
typedef struct sceGsFogcol sceGsFogcol;

struct sceGsTexflush                           /* 0x8 */
{
    u_long pad00;                              /* 0x0 */
};
typedef struct sceGsTexflush sceGsTexflush;

struct sceGsScissor                            /* 0x8 */
{
    u_long SCAX0 : 11;                         /* 0x0:0 */
    u_long pad11 : 5;                          /* 0x1:3 */
    u_long SCAX1 : 11;                         /* 0x2:0 */
    u_long pad27 : 5;                          /* 0x3:3 */
    u_long SCAY0 : 11;                         /* 0x4:0 */
    u_long pad43 : 5;                          /* 0x5:3 */
    u_long SCAY1 : 11;                         /* 0x6:0 */
    u_long pad59 : 5;                          /* 0x7:3 */
};
typedef struct sceGsScissor sceGsScissor;

struct sceGsAlpha                              /* 0x8 */
{
    u_long A     : 2;                          /* 0x0:0 */
    u_long B     : 2;                          /* 0x0:2 */
    u_long C     : 2;                          /* 0x0:4 */
    u_long D     : 2;                          /* 0x0:6 */
    u_long pad8  : 24;                         /* 0x1:0 */
    u_long FIX   : 8;                          /* 0x4:0 */
    u_long pad40 : 24;                         /* 0x5:0 */
};
typedef struct sceGsAlpha sceGsAlpha;

struct sceGsDimx                               /* 0x8 */
{
    u_long DIMX00 : 3;                         /* 0x0:0 */
    u_long pad00  : 1;                         /* 0x0:3 */
    u_long DIMX01 : 3;                         /* 0x0:4 */
    u_long pad01  : 1;                         /* 0x0:7 */
    u_long DIMX02 : 3;                         /* 0x1:0 */
    u_long pad02  : 1;                         /* 0x1:3 */
    u_long DIMX03 : 3;                         /* 0x1:4 */
    u_long pad03  : 1;                         /* 0x1:7 */
    u_long DIMX10 : 3;                         /* 0x2:0 */
    u_long pad10  : 1;                         /* 0x2:3 */
    u_long DIMX11 : 3;                         /* 0x2:4 */
    u_long pad11  : 1;                         /* 0x2:7 */
    u_long DIMX12 : 3;                         /* 0x3:0 */
    u_long pad12  : 1;                         /* 0x3:3 */
    u_long DIMX13 : 3;                         /* 0x3:4 */
    u_long pad13  : 1;                         /* 0x3:7 */
    u_long DIMX20 : 3;                         /* 0x4:0 */
    u_long pad20  : 1;                         /* 0x4:3 */
    u_long DIMX21 : 3;                         /* 0x4:4 */
    u_long pad21  : 1;                         /* 0x4:7 */
    u_long DIMX22 : 3;                         /* 0x5:0 */
    u_long pad22  : 1;                         /* 0x5:3 */
    u_long DIMX23 : 3;                         /* 0x5:4 */
    u_long pad23  : 1;                         /* 0x5:7 */
    u_long DIMX30 : 3;                         /* 0x6:0 */
    u_long pad30  : 1;                         /* 0x6:3 */
    u_long DIMX31 : 3;                         /* 0x6:4 */
    u_long pad31  : 1;                         /* 0x6:7 */
    u_long DIMX32 : 3;                         /* 0x7:0 */
    u_long pad32  : 1;                         /* 0x7:3 */
    u_long DIMX33 : 3;                         /* 0x7:4 */
    u_long pad33  : 1;                         /* 0x7:7 */
};
typedef struct sceGsDimx sceGsDimx;

struct sceGsDthe                               /* 0x8 */
{
    u_long DTHE  : 1;                          /* 0x0:0 */
    u_long pad01 : 63;                         /* 0x0:1 */
};
typedef struct sceGsDthe sceGsDthe;

struct sceGsColclamp                           /* 0x8 */
{
    u_long CLAMP : 1;                          /* 0x0:0 */
    u_long pad01 : 63;                         /* 0x0:1 */
};
typedef struct sceGsColclamp sceGsColclamp;

struct sceGsTest                               /* 0x8 */
{
    u_long ATE   : 1;                          /* 0x0:0 */
    u_long ATST  : 3;                          /* 0x0:1 */
    u_long AREF  : 8;                          /* 0x0:4 */
    u_long AFAIL : 2;                          /* 0x1:4 */
    u_long DATE  : 1;                          /* 0x1:6 */
    u_long DATM  : 1;                          /* 0x1:7 */
    u_long ZTE   : 1;                          /* 0x2:0 */
    u_long ZTST  : 2;                          /* 0x2:1 */
    u_long pad19 : 45;                         /* 0x2:3 */
};
typedef struct sceGsTest sceGsTest;

struct sceGsPabe                               /* 0x8 */
{
    u_long PABE  : 1;                          /* 0x0:0 */
    u_long pad01 : 63;                         /* 0x0:1 */
};
typedef struct sceGsPabe sceGsPabe;

struct sceGsFba                                /* 0x8 */
{
    u_long FBA   : 1;                          /* 0x0:0 */
    u_long pad01 : 63;                         /* 0x0:1 */
};
typedef struct sceGsFba sceGsFba;

struct sceGsFrame                              /* 0x8 */
{
    u_long FBP   : 9;                          /* 0x0:0 */
    u_long pad09 : 7;                          /* 0x1:1 */
    u_long FBW   : 6;                          /* 0x2:0 */
    u_long pad22 : 2;                          /* 0x2:6 */
    u_long PSM   : 6;                          /* 0x3:0 */
    u_long pad30 : 2;                          /* 0x3:6 */
    u_long FBMSK : 32;                         /* 0x4:0 */
};
typedef struct sceGsFrame sceGsFrame;

struct sceGsZbuf                               /* 0x8 */
{
    u_long ZBP   : 9;                          /* 0x0:0 */
    u_long pad09 : 15;                         /* 0x1:1 */
    u_long PSM   : 4;                          /* 0x3:0 */
    u_long pad28 : 4;                          /* 0x3:4 */
    u_long ZMSK  : 1;                          /* 0x4:0 */
    u_long pad33 : 31;                         /* 0x4:1 */
};
typedef struct sceGsZbuf sceGsZbuf;

struct sceGsBitbltbuf                          /* 0x8 */
{
    u_long SBP   : 14;                         /* 0x0:0 */
    u_long pad14 : 2;                          /* 0x1:6 */
    u_long SBW   : 6;                          /* 0x2:0 */
    u_long pad22 : 2;                          /* 0x2:6 */
    u_long SPSM  : 6;                          /* 0x3:0 */
    u_long pad30 : 2;                          /* 0x3:6 */
    u_long DBP   : 14;                         /* 0x4:0 */
    u_long pad46 : 2;                          /* 0x5:6 */
    u_long DBW   : 6;                          /* 0x6:0 */
    u_long pad54 : 2;                          /* 0x6:6 */
    u_long DPSM  : 6;                          /* 0x7:0 */
    u_long pad62 : 2;                          /* 0x7:6 */
};
typedef struct sceGsBitbltbuf sceGsBitbltbuf;

struct sceGsTrxpos                             /* 0x8 */
{
    u_long SSAX  : 11;                         /* 0x0:0 */
    u_long pad11 : 5;                          /* 0x1:3 */
    u_long SSAY  : 11;                         /* 0x2:0 */
    u_long pad27 : 5;                          /* 0x3:3 */
    u_long DSAX  : 11;                         /* 0x4:0 */
    u_long pad43 : 5;                          /* 0x5:3 */
    u_long DSAY  : 11;                         /* 0x6:0 */
    u_long DIR   : 2;                          /* 0x7:3 */
    u_long pad61 : 3;                          /* 0x7:5 */
};
typedef struct sceGsTrxpos sceGsTrxpos;

struct sceGsTrxreg                             /* 0x8 */
{
    u_long RRW   : 12;                         /* 0x0:0 */
    u_long pad12 : 20;                         /* 0x1:4 */
    u_long RRH   : 12;                         /* 0x4:0 */
    u_long pad44 : 20;                         /* 0x5:4 */
};
typedef struct sceGsTrxreg sceGsTrxreg;

struct sceGsTrxdir                             /* 0x8 */
{
    u_long XDR   : 2;                          /* 0x0:0 */
    u_long pad02 : 62;                         /* 0x0:2 */
};
typedef struct sceGsTrxdir sceGsTrxdir;

struct sceGsHwreg                              /* 0x8 */
{
    u_long WDATA;                              /* 0x0 */
};
typedef struct sceGsHwreg sceGsHwreg;

struct sceGsSignal                             /* 0x8 */
{
    u_int ID;                                  /* 0x0 */
    u_int IDMSK;                               /* 0x4 */
};
typedef struct sceGsSignal sceGsSignal;

struct sceGsFinish                             /* 0x8 */
{
    u_long pad00;                              /* 0x0 */
};
typedef struct sceGsFinish sceGsFinish;

struct sceGsLabel                              /* 0x8 */
{
    u_int ID;                                  /* 0x0 */
    u_int IDMSK;                               /* 0x4 */
};
typedef struct sceGsLabel sceGsLabel;

/* ---- 32-bit VIF1 registers (verbatim from types.txt) ------------------- */
struct tVIF1_STAT                              /* 0x4 */
{
    u_int VPS : 2;                             /* 0x0:0 */
    u_int VEW : 1;                             /* 0x0:2 */
    u_int VGW : 1;                             /* 0x0:3 */
    u_int p0  : 2;                             /* 0x0:4 */
    u_int MRK : 1;                             /* 0x0:6 */
    u_int DBF : 1;                             /* 0x0:7 */
    u_int VSS : 1;                             /* 0x1:0 */
    u_int VFS : 1;                             /* 0x1:1 */
    u_int VIS : 1;                             /* 0x1:2 */
    u_int INT : 1;                             /* 0x1:3 */
    u_int ERO : 1;                             /* 0x1:4 */
    u_int ER1 : 1;                             /* 0x1:5 */
    u_int p1  : 9;                             /* 0x1:6 */
    u_int FDR : 1;                             /* 0x2:7 */
    u_int FQC : 5;                             /* 0x3:0 */
    u_int p2  : 3;                             /* 0x3:5 */
};
typedef struct tVIF1_STAT tVIF1_STAT;

struct tVIF1_FBRST                             /* 0x4 */
{
    u_int RST : 1;                             /* 0x0:0 */
    u_int FBK : 1;                             /* 0x0:1 */
    u_int STP : 1;                             /* 0x0:2 */
    u_int STC : 1;                             /* 0x0:3 */
    u_int p0  : 28;                            /* 0x0:4 */
};
typedef struct tVIF1_FBRST tVIF1_FBRST;

struct tVIF1_ERR                               /* 0x4 */
{
    u_int MII : 1;                             /* 0x0:0 */
    u_int ME0 : 1;                             /* 0x0:1 */
    u_int ME1 : 1;                             /* 0x0:2 */
    u_int p0  : 29;                            /* 0x0:3 */
};
typedef struct tVIF1_ERR tVIF1_ERR;

struct tVIF_MARK                               /* 0x4 */
{
    u_int MARK : 16;                           /* 0x0:0 */
    u_int p0   : 16;                           /* 0x2:0 */
};
typedef struct tVIF_MARK tVIF_MARK;

struct tVIF_CYCLE                              /* 0x4 */
{
    u_int CL : 8;                              /* 0x0:0 */
    u_int WL : 8;                              /* 0x1:0 */
    u_int p0 : 16;                             /* 0x2:0 */
};
typedef struct tVIF_CYCLE tVIF_CYCLE;

struct tVIF_MODE                               /* 0x4 */
{
    u_int MOD : 2;                             /* 0x0:0 */
    u_int p0  : 30;                            /* 0x0:2 */
};
typedef struct tVIF_MODE tVIF_MODE;

struct tVIF1_NUM                               /* 0x4 */
{
    u_int num : 8;                             /* 0x0:0 */
    u_int p0  : 24;                            /* 0x1:0 */
};
typedef struct tVIF1_NUM tVIF1_NUM;

struct tVIF_MASK                               /* 0x4 */
{
    u_int m0  : 2;                             /* 0x0:0 */
    u_int m1  : 2;                             /* 0x0:2 */
    u_int m2  : 2;                             /* 0x0:4 */
    u_int m3  : 2;                             /* 0x0:6 */
    u_int m4  : 2;                             /* 0x1:0 */
    u_int m5  : 2;                             /* 0x1:2 */
    u_int m6  : 2;                             /* 0x1:4 */
    u_int m7  : 2;                             /* 0x1:6 */
    u_int m8  : 2;                             /* 0x2:0 */
    u_int m9  : 2;                             /* 0x2:2 */
    u_int m10 : 2;                             /* 0x2:4 */
    u_int m11 : 2;                             /* 0x2:6 */
    u_int m12 : 2;                             /* 0x3:0 */
    u_int m13 : 2;                             /* 0x3:2 */
    u_int m14 : 2;                             /* 0x3:4 */
    u_int m15 : 2;                             /* 0x3:6 */
};
typedef struct tVIF_MASK tVIF_MASK;

struct tVIF_CODE                               /* 0x4 */
{
    u_int immediate : 16;                      /* 0x0:0 */
    u_int num       : 8;                       /* 0x2:0 */
    u_int cmd       : 8;                       /* 0x3:0 */
};
typedef struct tVIF_CODE tVIF_CODE;

struct tVIF_ITOPS                              /* 0x4 */
{
    u_int ITOPS : 10;                          /* 0x0:0 */
    u_int p0    : 22;                          /* 0x1:2 */
};
typedef struct tVIF_ITOPS tVIF_ITOPS;

struct tVIF1_BASE                              /* 0x4 */
{
    u_int BASE : 10;                           /* 0x0:0 */
    u_int p0   : 22;                           /* 0x1:2 */
};
typedef struct tVIF1_BASE tVIF1_BASE;

struct tVIF1_OFST                              /* 0x4 */
{
    u_int OFFSET : 10;                         /* 0x0:0 */
    u_int p0     : 22;                         /* 0x1:2 */
};
typedef struct tVIF1_OFST tVIF1_OFST;

struct tVIF1_TOPS                              /* 0x4 */
{
    u_int TOPS : 10;                           /* 0x0:0 */
    u_int p0   : 22;                           /* 0x1:2 */
};
typedef struct tVIF1_TOPS tVIF1_TOPS;

struct tVIF_R0 { u_int R0; };                  /* 0x4 */
struct tVIF_R1 { u_int R1; };                  /* 0x4 */
struct tVIF_R2 { u_int R2; };                  /* 0x4 */
struct tVIF_R3 { u_int R3; };                  /* 0x4 */
struct tVIF_C0 { u_int C0; };                  /* 0x4 */
struct tVIF_C1 { u_int C1; };                  /* 0x4 */
struct tVIF_C2 { u_int C2; };                  /* 0x4 */
struct tVIF_C3 { u_int C3; };                  /* 0x4 */
typedef struct tVIF_R0 tVIF_R0;
typedef struct tVIF_R1 tVIF_R1;
typedef struct tVIF_R2 tVIF_R2;
typedef struct tVIF_R3 tVIF_R3;
typedef struct tVIF_C0 tVIF_C0;
typedef struct tVIF_C1 tVIF_C1;
typedef struct tVIF_C2 tVIF_C2;
typedef struct tVIF_C3 tVIF_C3;

/* ---- embedded GIF tag used as an end-of-packet marker (16 bytes) ------- */
union SCEGIFTAG_EOP                             /* 0x10 */
{
    qword qw;
    struct
    {
        long int lTag;
        long int lRegs;
    };
};

/* ---- GIF "PACKED" A+D register-write item (16 bytes) ------------------- *
 * The SCE EE GIF helper type for an address+data pair pushed through the
 * GIF in PACKED mode; used by g3dDmaSetGsRegister(s) to stuff GS registers. */
struct _sceGifPackAd                            /* 0x10 */
{
    u_long DATA;                                /* 0x0 */
    u_long ADDR;                                /* 0x8 */
};

typedef struct _sceGifPackAd sceGifPackAd;

/* ---- remaining GIF "PACKED" register items (16 bytes each) ------------- *
 * The per-register PACKED-mode payloads the sprite path stuffs directly into
 * the DMA chain (RGBAQ colour, ST/UV texture coords, XYZF/XYZ vertex pos). */
struct _sceGifPackRgbaq                         /* 0x10 */
{
    u_int R;                                    /* 0x0 */
    u_int G;                                    /* 0x4 */
    u_int B;                                    /* 0x8 */
    u_int A;                                    /* 0xc */
};
typedef struct _sceGifPackRgbaq sceGifPackRgbaq;

struct _sceGifPackSt                            /* 0x10 */
{
    float S;                                    /* 0x0 */
    float T;                                    /* 0x4 */
    float Q;                                    /* 0x8 */
    u_int pad96;                                /* 0xc */
};
typedef struct _sceGifPackSt sceGifPackSt;

struct _sceGifPackUv                            /* 0x10 */
{
    int      U;                                 /* 0x0 */
    int      V;                                 /* 0x4 */
    long int pad64;                             /* 0x8 */
};
typedef struct _sceGifPackUv sceGifPackUv;

struct _sceGifPackXyzf                          /* 0x10 */
{
    int   X;                                    /* 0x0 */
    int   Y;                                    /* 0x4 */
    u_int Z;                                    /* 0x8 */
    u_int F : 12;                               /* 0xc:0 */
    u_int pad108 : 3;                           /* 0xd:4 */
    u_int ADC : 1;                              /* 0xd:7 */
    u_int pad112 : 16;                          /* 0xe:0 */
};
typedef struct _sceGifPackXyzf sceGifPackXyzf;

/* ---- GS privileged registers exposed to the EE (verbatim from types.txt) */
struct tGS_PMODE                               /* 0x8 */
{
    u_int EN1   : 1;                           /* 0x0:0 */
    u_int EN2   : 1;                           /* 0x0:1 */
    u_int CRTMD : 3;                           /* 0x0:2 */
    u_int MMOD  : 1;                           /* 0x0:5 */
    u_int AMOD  : 1;                           /* 0x0:6 */
    u_int SLBG  : 1;                           /* 0x0:7 */
    u_int ALP   : 8;                           /* 0x1:0 */
    u_int p0    : 16;                          /* 0x2:0 */
    u_int p1;                                  /* 0x4 */
};
typedef struct tGS_PMODE tGS_PMODE;

struct tGS_SMODE2                              /* 0x8 */
{
    u_int INT  : 1;                            /* 0x0:0 */
    u_int FFMD : 1;                            /* 0x0:1 */
    u_int DPMS : 2;                            /* 0x0:2 */
    u_int p0   : 28;                           /* 0x0:4 */
    u_int p1;                                  /* 0x4 */
};
typedef struct tGS_SMODE2 tGS_SMODE2;

struct tGS_DISPFB2                             /* 0x8 */
{
    u_int FBP : 9;                             /* 0x0:0 */
    u_int FBW : 6;                             /* 0x1:1 */
    u_int PSM : 5;                             /* 0x1:7 */
    u_int p0  : 12;                            /* 0x2:4 */
    u_int DBX : 11;                            /* 0x4:0 */
    u_int DBY : 11;                            /* 0x5:3 */
    u_int p1  : 10;                            /* 0x6:6 */
};
typedef struct tGS_DISPFB2 tGS_DISPFB2;

struct tGS_DISPLAY2                            /* 0x8 */
{
    u_int DX   : 12;                           /* 0x0:0 */
    u_int DY   : 11;                           /* 0x1:4 */
    u_int MAGH : 4;                            /* 0x2:7 */
    u_int MAGV : 2;                            /* 0x3:3 */
    u_int p0   : 3;                            /* 0x3:5 */
    u_int DW   : 12;                           /* 0x4:0 */
    u_int DH   : 11;                           /* 0x5:4 */
    u_int p1   : 9;                            /* 0x6:7 */
};
typedef struct tGS_DISPLAY2 tGS_DISPLAY2;

struct tGS_BGCOLOR                             /* 0x8 */
{
    u_int R  : 8;                              /* 0x0:0 */
    u_int G  : 8;                              /* 0x1:0 */
    u_int B  : 8;                              /* 0x2:0 */
    u_int p0 : 8;                              /* 0x3:0 */
    u_int p1;                                  /* 0x4 */
};
typedef struct tGS_BGCOLOR tGS_BGCOLOR;

/* ---- GIFtag (16 bytes), bitfield view elided to the raw 128 bits ------- */
struct sceGifTag { // 0x10
	/* 0x0:0 */ long unsigned int NLOOP : 15;
	/* 0x1:7 */ long unsigned int EOP : 1;
	/* 0x2:0 */ long unsigned int pad16 : 16;
	/* 0x4:0 */ long unsigned int id : 14;
	/* 0x5:6 */ long unsigned int PRE : 1;
	/* 0x5:7 */ long unsigned int PRIM : 11;
	/* 0x7:2 */ long unsigned int FLG : 2;
	/* 0x7:4 */ long unsigned int NREG : 4;
	/* 0x8:0 */ long unsigned int REGS0 : 4;
	/* 0x8:4 */ long unsigned int REGS1 : 4;
	/* 0x9:0 */ long unsigned int REGS2 : 4;
	/* 0x9:4 */ long unsigned int REGS3 : 4;
	/* 0xa:0 */ long unsigned int REGS4 : 4;
	/* 0xa:4 */ long unsigned int REGS5 : 4;
	/* 0xb:0 */ long unsigned int REGS6 : 4;
	/* 0xb:4 */ long unsigned int REGS7 : 4;
	/* 0xc:0 */ long unsigned int REGS8 : 4;
	/* 0xc:4 */ long unsigned int REGS9 : 4;
	/* 0xd:0 */ long unsigned int REGS10 : 4;
	/* 0xd:4 */ long unsigned int REGS11 : 4;
	/* 0xe:0 */ long unsigned int REGS12 : 4;
	/* 0xe:4 */ long unsigned int REGS13 : 4;
	/* 0xf:0 */ long unsigned int REGS14 : 4;
	/* 0xf:4 */ long unsigned int REGS15 : 4;
};
typedef struct sceGifTag sceGifTag;

/* ---- libgraph double-buffer / display / draw env descriptors ----------- *
 * Only the members the GS wrapper reads are spelled out; the rest of each
 * struct is preserved as opaque padding so the offsets stay exact. */
struct sceGsDispEnv                            /* 0x28 */
{
    tGS_PMODE    pmode;                        /* 0x00 */
    tGS_SMODE2   smode2;                        /* 0x08 */
    tGS_DISPFB2  dispfb;                        /* 0x10 */
    tGS_DISPLAY2 display;                       /* 0x18 */
    tGS_BGCOLOR  bgcolor;                       /* 0x20 */
};
typedef struct sceGsDispEnv sceGsDispEnv;

/* draw-env half (the 0x80-byte block g3dGsPutDrawEnv pushes via a GIFtag). */
struct sceGsDrawEnv1                           /* 0x80 */
{
    sceGsFrame      frame1;                     /* 0x00 */
    u_long          frame1addr;                 /* 0x08 */
    sceGsZbuf       zbuf1;                       /* 0x10 */
    long int        zbuf1addr;                   /* 0x18 */
    sceGsXyoffset   xyoffset1;                    /* 0x20 */
    long int        xyoffset1addr;                /* 0x28 */
    sceGsScissor    scissor1;                     /* 0x30 */
    long int        scissor1addr;                 /* 0x38 */
    sceGsPrmodecont prmodecont;                   /* 0x40 */
    long int        prmodecontaddr;               /* 0x48 */
    sceGsColclamp   colclamp;                     /* 0x50 */
    long int        colclampaddr;                 /* 0x58 */
    sceGsDthe       dthe;                         /* 0x60 */
    long int        dtheaddr;                     /* 0x68 */
    sceGsTest       test1;                        /* 0x70 */
    long int        test1addr;                    /* 0x78 */
};
typedef struct sceGsDrawEnv1 sceGsDrawEnv1;

struct sceGsClear                              /* 0x60 */
{
    sceGsTest       test1;                       /* 0x00 */
    long int        test1addr;                   /* 0x08 */
    sceGsPrim       prim;                        /* 0x10 */
    long int        primaddr;                    /* 0x18 */
    sceGifPackRgbaq rgbaq;                       /* 0x20 */
    sceGsXyz        xyz2a;                       /* 0x30 */
    long int        xyz2aaddr;                   /* 0x38 */
    sceGsXyz        xyz2b;                       /* 0x40 */
    long int        xyz2baddr;                   /* 0x48 */
    unsigned char   pad[0x10];                   /* 0x50 */
};
typedef struct sceGsClear sceGsClear;

struct sceGsDBuff                              /* 0x230 */
{
    sceGsDispEnv disp[2];                       /* 0x000 */
    sceGifTag    giftag0;                       /* 0x050 */
    sceGsDrawEnv1 draw0;                        /* 0x060 */
    sceGsClear    clear0;                       /* 0x0e0 */
    sceGifTag    giftag1;                       /* 0x140 */
    sceGsDrawEnv1 draw1;                        /* 0x150 */
    sceGsClear    clear1;                       /* 0x1d0 */
};
typedef struct sceGsDBuff sceGsDBuff;

/* GS->local store-image packet (VIFcode + GIFtag + BITBLTBUF/TRXPOS/TRXREG/
 * FINISH/TRXDIR A+D items) handed straight to g3dGsExecStoreImage. */
struct sceGsStoreImage                         /* 0x70 */
{
    u_int          vifcode[4];                  /* 0x00 */
    sceGifTag      giftag;                       /* 0x10 */
    sceGsBitbltbuf bitbltbuf;                    /* 0x20 */
    u_long         bitbltbufaddr;                /* 0x28 */
    sceGsTrxpos    trxpos;                        /* 0x30 */
    u_long         trxposaddr;                    /* 0x38 */
    sceGsTrxreg    trxreg;                        /* 0x40 */
    u_long         trxregaddr;                    /* 0x48 */
    sceGsFinish    finish;                        /* 0x50 */
    u_long         finishaddr;                    /* 0x58 */
    sceGsTrxdir    trxdir;                         /* 0x60 */
    u_long         trxdiraddr;                     /* 0x68 */
};
typedef struct sceGsStoreImage sceGsStoreImage;

/* local->GS load-image packet.  The final GIFtag is the IMAGE payload tag
 * whose NLOOP is the texel qwc count; SGDTRI2FILEHEADER bodies begin after it. */
struct sceGsLoadImage                          /* 0x60 */
{
    sceGifTag      giftag;                       /* 0x00 */
    sceGsBitbltbuf bitbltbuf;                    /* 0x10 */
    u_long         bitbltbufaddr;                /* 0x18 */
    sceGsTrxpos    trxpos;                        /* 0x20 */
    u_long         trxposaddr;                    /* 0x28 */
    sceGsTrxreg    trxreg;                        /* 0x30 */
    u_long         trxregaddr;                    /* 0x38 */
    sceGsTrxdir    trxdir;                         /* 0x40 */
    u_long         trxdiraddr;                     /* 0x48 */
    sceGifTag      giftag1;                       /* 0x50 */
};
typedef struct sceGsLoadImage sceGsLoadImage;

static_assert(sizeof(sceGsStoreImage) == 0x70, "sceGsStoreImage must match PS2 packet layout");
static_assert(sizeof(sceGsLoadImage) == 0x60, "sceGsLoadImage must match PS2 packet layout");

/* GS register-address constants and the SCE_GS_SET_* / SCE_GIF_* value builders
 * are part of the SCE EE <libgraph.h> SDK surface, not this engine type header;
 * they live in sdk/libgraph.h so the SDK definitions are not duplicated here.
 * (This header still owns the register *bitfield structs* above, which come from
 * the prototype's own debug type info.) */
#include "../../sdk/libgraph.h"

#endif /* _SCE_GS_H */
