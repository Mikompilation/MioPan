/* ==========================================================================
 *  graphics/dmaVif1.h
 *
 *  The EE VIF1 DMA ring allocator (dmaVif1.c): a double-buffered chain of DMA
 *  ref/call tags feeding VIF1->GS.  Callers grab a quadword run from the ring
 *  (dmaVif1GetPacket* -> qword *), fill it, then commit the used length
 *  (dmaVif1SetPacket*); the *VIF / *FLUSH_DIRECT variants wrap the run in the
 *  matching VIFcode (plain VIF, or a FLUSH + DIRECT).  AddRefTag[VIF] splice an
 *  external buffer into the chain by reference; Kick / CheckDMA / CheckSync /
 *  WaitPath3 drive and synchronise the transfer.
 *
 *  qword is the raw 128-bit quadword (sdk/scetypes.h).  The 2D layer views the
 *  same storage through its Q_WORDDATA union (graph2d/g2d_draw.h); callers cast
 *  between the two as needed.
 *
 *  dmaVif1.c is not reverse-engineered yet; the bodies live in the stub
 *  dmaVif1.c until then.
 *
 *  Reconstructed from the Feb 6 2004 prototype (SLES_523.84).
 * ======================================================================== */

#ifndef _GRAPHICS_DMAVIF1_H
#define _GRAPHICS_DMAVIF1_H

#include <stdint.h>

#include "eetypes.h"
#include "../sdk/scetypes.h"        /* qword */

/* ---- lifecycle --------------------------------------------------------- */
void  dmaVif1Init(void *pPacketBuffer, int iSizePacketBuffer, void *pTagBuffer, int iNumTag);
int   dmaVif1Resize(int iNumTag);
int   dmaVif1IsResizeOK(void);
void  dmaVif1Clear(void);
void  dmaVif1ClearDMA(void);

/* ---- packet allocation / commit ---------------------------------------- */
qword *dmaVif1GetPacket(void);
void   dmaVif1SetPacket(const void *end_adrs);
qword *dmaVif1GetPacketVIF(void);
void   dmaVif1SetPacketVIF(qword *end_adrs, int vifcode1, int vifcode2);
qword *dmaVif1GetPacketFLUSH_DIRECT(void);
void   dmaVif1SetPacketFLUSH_DIRECT(qword *end_adrs);

/* ---- reference / call tags --------------------------------------------- */
void  dmaVif1AddRefTag(uintptr_t addr, int size);
void  dmaVif1AddRefTagVIF(uintptr_t addr, int size, int vif1code1, int vif1code2);
void  dmaVif1AddCallTag(uintptr_t next_tag_addr);

/* ---- kick / sync ------------------------------------------------------- */
void  dmaVif1Kick(void);
void  dmaVif1CheckDMA(void);
void  dmaVif1CheckSync(void);
void  dmaVif1WaitPath3(void);
int   dmaVif1GetToggle(void);

#endif /* _GRAPHICS_DMAVIF1_H */
