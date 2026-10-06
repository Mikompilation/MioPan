/* ==========================================================================
 *  eeregs.cpp  (host-backed EE memory-mapped registers)
 * ======================================================================== */

#include "eeregs.h"

#include <chrono>

/* EE Timer 0 with CLKS = 3 counts H-BLANKs.  NTSC draws 525 lines 59.94 times a
 * second, so a scanline is 1/15734.264 s; the count playpss.c budgets against
 * is in those units. */
static const double kEeHBlankHz = 15734.264;

/* EE Timer 1 with CLKS = 2 counts BUSCLK/256.  The EE bus runs at
 * 147.456 MHz, so a tick is 1/576000 s -- which is what makes
 * perf_measure.c's 20480-tick budget 35.6 ms, just over one 30 fps frame. */
static const double kEeBusClk256Hz = 576000.0;

static std::chrono::steady_clock::time_point s_rcnt0_origin =
    std::chrono::steady_clock::now();
static unsigned int s_rcnt0_base;

MioPanRcnt0Count g_pcport_REG_RCNT0_COUNT;

static std::chrono::steady_clock::time_point s_rcnt1_origin =
    std::chrono::steady_clock::now();
static unsigned int s_rcnt1_base;

MioPanRcnt1Count g_pcport_REG_RCNT1_COUNT;

extern "C" {

unsigned int MioPan_Rcnt0Read(void)
{
    const std::chrono::duration<double> dt =
        std::chrono::steady_clock::now() - s_rcnt0_origin;

    /* The hardware counter is 16 bits and wraps; so does this. */
    return (s_rcnt0_base + (unsigned int)(dt.count() * kEeHBlankHz)) & 0xffffu;
}

void MioPan_Rcnt0Write(unsigned int value)
{
    s_rcnt0_origin = std::chrono::steady_clock::now();
    s_rcnt0_base   = value & 0xffffu;
}

unsigned int MioPan_Rcnt1Read(void)
{
    const std::chrono::duration<double> dt =
        std::chrono::steady_clock::now() - s_rcnt1_origin;

    /* The hardware counter is 16 bits and wraps; so does this. */
    return (s_rcnt1_base + (unsigned int)(dt.count() * kEeBusClk256Hz)) & 0xffffu;
}

void MioPan_Rcnt1Write(unsigned int value)
{
    s_rcnt1_origin = std::chrono::steady_clock::now();
    s_rcnt1_base   = value & 0xffffu;
}

volatile unsigned int g_pcport_REG_RCNT1_MODE = 0;
volatile unsigned int g_pcport_REG_RCNT0_MODE = 0;
volatile unsigned int g_pcport_REG_DMAC_4_IPU_TO_CHCR = 0;
volatile unsigned int g_pcport_REG_DMAC_4_IPU_TO_MADR = 0;
volatile unsigned int g_pcport_REG_DMAC_4_IPU_TO_QWC = 0;
volatile unsigned int g_pcport_REG_DMAC_1_VIF1_CHCR = 0;
volatile unsigned int g_pcport_REG_DMAC_1_VIF1_MADR = 0;
volatile unsigned int g_pcport_REG_DMAC_1_VIF1_QWC = 0;
volatile unsigned int g_pcport_REG_DMAC_1_VIF1_TADR = 0;
volatile unsigned int g_pcport_REG_DMAC_2_GIF_CHCR = 0;
volatile unsigned int g_pcport_REG_DMAC_2_GIF_MADR = 0;
volatile unsigned int g_pcport_REG_DMAC_2_GIF_QWC = 0;
volatile unsigned int g_pcport_REG_DMAC_2_GIF_TADR = 0;
alignas(16) volatile unsigned int g_pcport_REG_VIF1_REGISTER_FILE
    [PCPORT_VIF1_REGISTER_COUNT][PCPORT_VIF1_REGISTER_QWORD_WORDS] = {};
volatile u_long       g_pcport_REG_VIF1_FIFO = 0;
volatile unsigned int g_pcport_REG_GIF_CTRL = 0;
volatile unsigned int g_pcport_REG_GIF_STAT = 0;
volatile u_long       g_pcport_REG_GS_PMODE = 0;
volatile u_long       g_pcport_REG_GS_SMODE2 = 0;
volatile u_long       g_pcport_REG_GS_DISPFB1 = 0;
volatile u_long       g_pcport_REG_GS_DISPLAY1 = 0;
volatile u_long       g_pcport_REG_GS_DISPFB2 = 0;
volatile u_long       g_pcport_REG_GS_DISPLAY2 = 0;
volatile u_long       g_pcport_REG_GS_CSR = 0;
volatile u_long       g_pcport_REG_GS_BGCOLOR = 0;
volatile u_long       g_pcport_REG_GS_BUSDIR = 0;

}
