// FILE: /home/zero_rom/zero2np/src/graphics/dmaVif1.c
//
// PC-port shim for the EE VIF1 DMA ring allocator (double-buffered ref/call tag
// chain feeding VIF1->GS).  The real EE implementation is not reconstructed
// yet; this host implementation gives callers a valid 16-byte-aligned packet
// stream and keeps enough state for cache/toggle checks while treating the DMA
// kick itself as already complete.
//
// Reconstructed from the Feb 6 2004 prototype (SLES_523.84).

#include "dmaVif1.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define VIF1_FLUSH                  0x11000000
#define VIF1_DIRECT                 0x50000000

#define PCPORT_DMAVIF1_DEFAULT_QWORDS       0x40000
#define PCPORT_DMAVIF1_PACKET_GUARD_QWORDS  0x4000

#ifdef _MSC_VER
__declspec(align(16)) static qword s_DefaultPacketBuffer[PCPORT_DMAVIF1_DEFAULT_QWORDS];
#else
static qword s_DefaultPacketBuffer[PCPORT_DMAVIF1_DEFAULT_QWORDS] __attribute__((aligned(16)));
#endif


static qword *s_pPacketTop;
static qword *s_pPacketEnd;
static qword *s_pPacketCurrent;
static qword *s_pPacketOpenBase;
static qword *s_pPacketOpenPayload;
static int    s_iOpenPrefixQW;
static int    s_iToggle;
static int    s_iRefTagCount;
static int    s_iResizeRequest;

static int DmaVif1PointerLooksHostBacked(const void *p)
{
    return ((uintptr_t)p) >= ((uintptr_t)0x100000000ULL);
}

static qword *DmaVif1AlignQwordPointer(const void *p)
{
    uintptr_t addr;

    addr = ((uintptr_t)p + 0xf) & ~(uintptr_t)0xf;
    return (qword *)(void *)addr;
}

static int DmaVif1PointerInBuffer(const void *p)
{
    uintptr_t addr;
    uintptr_t top;
    uintptr_t end;

    addr = (uintptr_t)p;
    top  = (uintptr_t)s_pPacketTop;
    end  = (uintptr_t)s_pPacketEnd;
    return top <= addr && addr <= end;
}

static void DmaVif1UseBuffer(qword *pBuffer, int iQWSize)
{
    s_pPacketTop         = pBuffer;
    s_pPacketEnd         = pBuffer + iQWSize;
    s_pPacketCurrent     = pBuffer;
    s_pPacketOpenBase    = NULL;
    s_pPacketOpenPayload = NULL;
    s_iOpenPrefixQW      = 0;
    s_iRefTagCount       = 0;
}

static void DmaVif1EnsureInit(void)
{
    if (s_pPacketTop == NULL)
    {
        DmaVif1UseBuffer(s_DefaultPacketBuffer, PCPORT_DMAVIF1_DEFAULT_QWORDS);
    }
}

static qword *DmaVif1OpenPacket(int iPrefixQW)
{
    DmaVif1EnsureInit();

    if (s_pPacketCurrent + iPrefixQW + PCPORT_DMAVIF1_PACKET_GUARD_QWORDS >= s_pPacketEnd)
    {
        s_pPacketCurrent = s_pPacketTop;
    }

    s_pPacketOpenBase    = s_pPacketCurrent;
    s_pPacketOpenPayload = s_pPacketCurrent + iPrefixQW;
    s_iOpenPrefixQW      = iPrefixQW;

    if (iPrefixQW > 0)
    {
        memset(s_pPacketOpenBase, 0, (size_t)iPrefixQW * sizeof(qword));
    }

    return s_pPacketOpenPayload;
}

static int DmaVif1OpenPayloadQW(qword *pEnd)
{
    if (s_pPacketOpenPayload == NULL || pEnd < s_pPacketOpenPayload)
    {
        return 0;
    }

    return (int)(pEnd - s_pPacketOpenPayload);
}

static void DmaVif1CommitPacket(const void *end_adrs, int iMode, int vifcode1, int vifcode2)
{
    qword *pEnd;
    int   iPayloadQW;

    DmaVif1EnsureInit();

    pEnd = DmaVif1AlignQwordPointer(end_adrs);
    if (!DmaVif1PointerInBuffer(pEnd))
    {
        s_pPacketOpenBase    = NULL;
        s_pPacketOpenPayload = NULL;
        s_iOpenPrefixQW      = 0;
        return;
    }

    if (s_pPacketOpenBase == NULL)
    {
        s_pPacketOpenBase    = s_pPacketCurrent;
        s_pPacketOpenPayload = s_pPacketCurrent;
        s_iOpenPrefixQW      = 0;
    }

    iPayloadQW = DmaVif1OpenPayloadQW(pEnd);

    if (s_iOpenPrefixQW > 0)
    {
        int *pVifCode = (int *)s_pPacketOpenBase;

        pVifCode[0] = 0;
        pVifCode[1] = 0;
        if (iMode == 1)
        {
            pVifCode[2] = vifcode1;
            pVifCode[3] = vifcode2;
        }
        else if (iMode == 2)
        {
            pVifCode[2] = VIF1_FLUSH;
            pVifCode[3] = VIF1_DIRECT | (iPayloadQW & 0xffff);
        }
    }

    s_pPacketCurrent     = pEnd;
    s_pPacketOpenBase    = NULL;
    s_pPacketOpenPayload = NULL;
    s_iOpenPrefixQW      = 0;

    if (s_pPacketCurrent + PCPORT_DMAVIF1_PACKET_GUARD_QWORDS >= s_pPacketEnd)
    {
        s_pPacketCurrent = s_pPacketTop;
    }
}

// ──────────────────────────────────────────────────────────────────────
// Lifecycle (stubs).

void dmaVif1Init(void *pPacketBuffer, int iSizePacketBuffer, void *pTagBuffer, int iNumTag)
{
    qword *pAligned;
    int    iAdjust;
    int    iQWSize;

    (void)pTagBuffer;
    (void)iNumTag;

    if (pPacketBuffer != NULL && iSizePacketBuffer > 0 && DmaVif1PointerLooksHostBacked(pPacketBuffer))
    {
        pAligned = DmaVif1AlignQwordPointer(pPacketBuffer);
        iAdjust  = (int)((char *)pAligned - (char *)pPacketBuffer);
        iQWSize  = (iSizePacketBuffer - iAdjust) / (int)sizeof(qword);
        if (iQWSize > 0)
        {
            DmaVif1UseBuffer(pAligned, iQWSize);
            return;
        }
    }

    DmaVif1UseBuffer(s_DefaultPacketBuffer, PCPORT_DMAVIF1_DEFAULT_QWORDS);
}

int  dmaVif1Resize(int iNumTag)   { s_iResizeRequest = iNumTag; return 1; }
int  dmaVif1IsResizeOK(void)      { return 1; }
void dmaVif1Clear(void)
{
    DmaVif1EnsureInit();
    s_pPacketCurrent     = s_pPacketTop;
    s_pPacketOpenBase    = NULL;
    s_pPacketOpenPayload = NULL;
    s_iOpenPrefixQW      = 0;
    s_iRefTagCount       = 0;
}
void dmaVif1ClearDMA(void)        { dmaVif1Clear(); }

// ──────────────────────────────────────────────────────────────────────
// Packet allocation / commit (stubs).

qword *dmaVif1GetPacket(void)                 { return DmaVif1OpenPacket(0); }
void   dmaVif1SetPacket(const void *end_adrs) { DmaVif1CommitPacket(end_adrs, 0, 0, 0); }
qword *dmaVif1GetPacketVIF(void)              { return DmaVif1OpenPacket(1); }
void   dmaVif1SetPacketVIF(qword *end_adrs, int vifcode1, int vifcode2)
{
    DmaVif1CommitPacket(end_adrs, 1, vifcode1, vifcode2);
}
qword *dmaVif1GetPacketFLUSH_DIRECT(void)     { return DmaVif1OpenPacket(1); }
void   dmaVif1SetPacketFLUSH_DIRECT(qword *end_adrs) { DmaVif1CommitPacket(end_adrs, 2, 0, 0); }

// ──────────────────────────────────────────────────────────────────────
// Reference / call tags (stubs).

void dmaVif1AddRefTag(uintptr_t addr, int size) { (void)addr; (void)size; s_iRefTagCount++; }
void dmaVif1AddRefTagVIF(uintptr_t addr, int size, int vif1code1, int vif1code2)
{
    (void)addr; (void)size; (void)vif1code1; (void)vif1code2;
    s_iRefTagCount++;
}
void dmaVif1AddCallTag(uintptr_t next_tag_addr) { (void)next_tag_addr; s_iRefTagCount++; }

// ──────────────────────────────────────────────────────────────────────
// Kick / sync (stubs).

void dmaVif1Kick(void)
{
    (void)s_iResizeRequest;
    s_iToggle ^= 1;
    dmaVif1Clear();
}
void dmaVif1CheckDMA(void)  { }
void dmaVif1CheckSync(void) { }
void dmaVif1WaitPath3(void) { }
int  dmaVif1GetToggle(void) { return s_iToggle; }
