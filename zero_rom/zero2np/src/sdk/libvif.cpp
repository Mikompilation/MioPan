/* ==========================================================================
 *  libvif.cpp  (SCE VIF1 packet builder -- PC-port shim)
 * ======================================================================== */

#include "libvif.h"

#include <stdint.h>
#include <string.h>

ATTRIBUTE_ALIGNED(16, static u_long128 s_fallbackPacketBuffer[0x4000]);

static int SceVif1IsEeAlias(u_int addr)
{
    return (addr & 0x30000000u) != 0;
}

extern "C" {

void sceVif1PkInit(sceVif1Packet *packet, u_int addr)
{
    if (packet != 0)
    {
        memset(packet, 0, sizeof(*packet));
        if (SceVif1IsEeAlias(addr))
        {
            packet->pBase = s_fallbackPacketBuffer;
        }
        else
        {
            packet->pBase = (u_long128 *)(uintptr_t)addr;
        }
        packet->pCurrent = packet->pBase;
    }
}

void sceVif1PkReset(sceVif1Packet *packet)
{
    if (packet != 0)
    {
        packet->pCurrent = packet->pBase;
        packet->pGifTag = 0;
        packet->pDirect = 0;
        packet->nElement = 0;
        packet->flag = 0;
    }
}

void sceVif1PkCnt(sceVif1Packet *packet, int irq)
{
    (void)irq;
    if (packet != 0)
    {
        packet->nElement = 0;
    }
}

void sceVif1PkOpenDirectCode(sceVif1Packet *packet, int irq)
{
    (void)irq;
    if (packet != 0)
    {
        packet->pDirect = packet->pCurrent;
    }
}

void sceVif1PkCloseDirectCode(sceVif1Packet *packet)
{
    if (packet != 0)
    {
        packet->pDirect = 0;
    }
}

void sceVif1PkOpenGifTag(sceVif1Packet *packet, u_long128 *pGifTag)
{
    if (packet != 0)
    {
        packet->pGifTag = pGifTag;
    }
}

void sceVif1PkCloseGifTag(sceVif1Packet *packet)
{
    if (packet != 0)
    {
        packet->pGifTag = 0;
    }
}

void sceVif1PkReserve(sceVif1Packet *packet, int nBytes)
{
    if (packet != 0 && packet->pCurrent != 0)
    {
        packet->pCurrent += (nBytes + 15) / 16;
    }
}

void sceVif1PkEnd(sceVif1Packet *packet, int irq)
{
    (void)irq;
    if (packet != 0)
    {
        packet->flag |= 1;
    }
}

void sceVif1PkTerminate(sceVif1Packet *packet)
{
    if (packet != 0)
    {
        packet->pGifTag = 0;
        packet->pDirect = 0;
    }
}

}
