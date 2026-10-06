/* ==========================================================================
 *  eeregs.h  (EE memory-mapped register accessors -- minimal shim)
 *
 *  The PS2 EE DMAC / VIF1 / GIF / GS privileged registers are memory-mapped
 *  volatiles.  Normally these REG_* lvalue macros come from the EE SDK's
 *  <eeregs.h>; only the registers the graph3d GS wrapper touches are declared
 *  here so the tree has a self-contained, compilable include graph.
 *
 *  On the EE these macros are volatile lvalues at fixed physical addresses.
 *  On the PC port they expand to host-backed volatile variables so the original
 *  register-shaped code can compile and run without dereferencing PS2 MMIO
 *  addresses.
 *
 *  Reconstructed from the Feb 6 2004 prototype (SLES_523.84).
 * ======================================================================== */

#ifndef _EEREGS_H
#define _EEREGS_H

#include "eetypes.h"

#ifdef __cplusplus
extern "C" {
#endif

extern volatile unsigned int g_pcport_REG_RCNT1_MODE;
extern volatile unsigned int g_pcport_REG_RCNT0_MODE;
extern volatile unsigned int g_pcport_REG_DMAC_4_IPU_TO_CHCR;
extern volatile unsigned int g_pcport_REG_DMAC_4_IPU_TO_MADR;
extern volatile unsigned int g_pcport_REG_DMAC_4_IPU_TO_QWC;

/* EE Timer 0, which playpss.c programs with CLKS = 3 so it counts H-BLANKs --
 * scanlines -- and then uses as a work budget: it zeroes the count and refills
 * the demux buffer until a scanline target is reached.  A plain variable would
 * never advance and those loops would never end, so the count is derived from
 * the host clock at the NTSC line rate; writing it moves the origin. */
unsigned int MioPan_Rcnt0Read(void);
void         MioPan_Rcnt0Write(unsigned int value);

/* EE Timer 1, which perf_measure.c programs with CLKS = 2 so it counts
 * BUSCLK/256 -- 147.456 MHz / 256 = 576 kHz -- and then uses as the engine's
 * stopwatch: FrameStart() zeroes it once a frame and GetPercent() reads it
 * back against a 20480-tick frame budget.  system.c snapshots the same
 * counter from the VIF1 DMA-completion handler.  A plain variable would sit
 * at whatever was last written and every reading would be zero, so the count
 * is derived from the host clock at that tick rate; writing it moves the
 * origin, and it wraps at 16 bits as the hardware counter does. */
unsigned int MioPan_Rcnt1Read(void);
void         MioPan_Rcnt1Write(unsigned int value);

extern volatile unsigned int g_pcport_REG_DMAC_1_VIF1_CHCR;
extern volatile unsigned int g_pcport_REG_DMAC_1_VIF1_MADR;
extern volatile unsigned int g_pcport_REG_DMAC_1_VIF1_QWC;
extern volatile unsigned int g_pcport_REG_DMAC_1_VIF1_TADR;
extern volatile unsigned int g_pcport_REG_DMAC_2_GIF_CHCR;
extern volatile unsigned int g_pcport_REG_DMAC_2_GIF_MADR;
extern volatile unsigned int g_pcport_REG_DMAC_2_GIF_QWC;
extern volatile unsigned int g_pcport_REG_DMAC_2_GIF_TADR;
/* 22 VIF1 hardware registers, exposed on EE at 0x10003c00 with 0x10 stride.
 * Word 0 of each qword carries the 32-bit VIF1 register value. */
#define PCPORT_VIF1_REGISTER_COUNT        22
#define PCPORT_VIF1_REGISTER_QWORD_WORDS  4

extern volatile unsigned int g_pcport_REG_VIF1_REGISTER_FILE
    [PCPORT_VIF1_REGISTER_COUNT][PCPORT_VIF1_REGISTER_QWORD_WORDS];
extern volatile u_long       g_pcport_REG_VIF1_FIFO;
extern volatile unsigned int g_pcport_REG_GIF_CTRL;
extern volatile unsigned int g_pcport_REG_GIF_STAT;
extern volatile u_long       g_pcport_REG_GS_PMODE;
extern volatile u_long       g_pcport_REG_GS_SMODE2;
extern volatile u_long       g_pcport_REG_GS_DISPFB1;
extern volatile u_long       g_pcport_REG_GS_DISPLAY1;
extern volatile u_long       g_pcport_REG_GS_DISPFB2;
extern volatile u_long       g_pcport_REG_GS_DISPLAY2;
extern volatile u_long       g_pcport_REG_GS_CSR;
extern volatile u_long       g_pcport_REG_GS_BGCOLOR;
extern volatile u_long       g_pcport_REG_GS_BUSDIR;

#ifdef __cplusplus
}
#endif

/* ---- EE timer / counter 1 (RCNT1) ------------------------------------- */
/* Same shape as RCNT0 below: MODE is a write-only latch, COUNT is a proxy so
 * that `REG_RCNT1_COUNT = 0;` and `x = REG_RCNT1_COUNT;` keep their exact ROM
 * spellings while the value actually advances. */
#define REG_RCNT1_MODE        g_pcport_REG_RCNT1_MODE

#ifdef __cplusplus
struct MioPanRcnt1Count
{
    operator unsigned int() const  { return MioPan_Rcnt1Read(); }
    unsigned int operator=(unsigned int v) { MioPan_Rcnt1Write(v); return v; }
};
extern MioPanRcnt1Count g_pcport_REG_RCNT1_COUNT;
#define REG_RCNT1_COUNT       g_pcport_REG_RCNT1_COUNT
#endif

/* ---- EE timer / counter 0 (RCNT0) ------------------------------------- */
/* MODE is write-only state; COUNT has to be both readable and assignable, and
 * has to move on its own -- see MioPan_Rcnt0Read() above.  A proxy gives the
 * ROM's `REG_RCNT0_COUNT = 0;` and `while (REG_RCNT0_COUNT < n)` their exact
 * spellings; every translation unit in this tree is compiled as C++, so the
 * one class here costs nothing. */
#define REG_RCNT0_MODE        g_pcport_REG_RCNT0_MODE

#ifdef __cplusplus
struct MioPanRcnt0Count
{
    operator unsigned int() const  { return MioPan_Rcnt0Read(); }
    unsigned int operator=(unsigned int v) { MioPan_Rcnt0Write(v); return v; }
};
extern MioPanRcnt0Count g_pcport_REG_RCNT0_COUNT;
#define REG_RCNT0_COUNT       g_pcport_REG_RCNT0_COUNT
#endif

/* ---- DMAC channel 4 (IPU-to) ------------------------------------------ */
/* Write-only here: the IPU is not modelled, so a chain kicked at it is
 * recorded and dropped.  playpss.c's nodataCallback() is the only writer. */
#define REG_DMAC_4_IPU_TO_CHCR  g_pcport_REG_DMAC_4_IPU_TO_CHCR
#define REG_DMAC_4_IPU_TO_MADR  g_pcport_REG_DMAC_4_IPU_TO_MADR
#define REG_DMAC_4_IPU_TO_QWC   g_pcport_REG_DMAC_4_IPU_TO_QWC

/* ---- DMAC channel 1 (VIF1) -------------------------------------------- */
#define REG_DMAC_1_VIF1_CHCR  g_pcport_REG_DMAC_1_VIF1_CHCR
#define REG_DMAC_1_VIF1_MADR  g_pcport_REG_DMAC_1_VIF1_MADR
#define REG_DMAC_1_VIF1_QWC   g_pcport_REG_DMAC_1_VIF1_QWC
#define REG_DMAC_1_VIF1_TADR  g_pcport_REG_DMAC_1_VIF1_TADR

/* ---- DMAC channel 2 (GIF) --------------------------------------------- */
#define REG_DMAC_2_GIF_CHCR   g_pcport_REG_DMAC_2_GIF_CHCR
#define REG_DMAC_2_GIF_MADR   g_pcport_REG_DMAC_2_GIF_MADR
#define REG_DMAC_2_GIF_QWC    g_pcport_REG_DMAC_2_GIF_QWC
#define REG_DMAC_2_GIF_TADR   g_pcport_REG_DMAC_2_GIF_TADR

/* ---- VIF1 ------------------------------------------------------------- */
#define REG_VIF1_STAT         g_pcport_REG_VIF1_REGISTER_FILE[0][0]
#define REG_VIF1_FBRST        g_pcport_REG_VIF1_REGISTER_FILE[1][0]
#define REG_VIF1_CODE         g_pcport_REG_VIF1_REGISTER_FILE[8][0]
#define REG_VIF1_FIFO         g_pcport_REG_VIF1_FIFO

/* ---- GIF -------------------------------------------------------------- */
#define REG_GIF_CTRL          g_pcport_REG_GIF_CTRL
#define REG_GIF_STAT          g_pcport_REG_GIF_STAT

/* ---- GS privileged registers ------------------------------------------ */
#define REG_GS_PMODE          g_pcport_REG_GS_PMODE
#define REG_GS_SMODE2         g_pcport_REG_GS_SMODE2
#define REG_GS_DISPFB1        g_pcport_REG_GS_DISPFB1
#define REG_GS_DISPLAY1       g_pcport_REG_GS_DISPLAY1
#define REG_GS_DISPFB2        g_pcport_REG_GS_DISPFB2
#define REG_GS_DISPLAY2       g_pcport_REG_GS_DISPLAY2
#define REG_GS_CSR            g_pcport_REG_GS_CSR
#define REG_GS_BGCOLOR        g_pcport_REG_GS_BGCOLOR
#define REG_GS_BUSDIR         g_pcport_REG_GS_BUSDIR

#endif /* _EEREGS_H */
