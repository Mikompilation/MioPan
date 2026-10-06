/* libvif.h — PS2 EE VIF1 packet-builder library — PC-port declarations.
 *
 * FF2 uses the sceVif1Pk* packet builder (the FF1 port does not, so these are
 * declared here from the prototype's call sites + the PS2 SDK <libvif.h>).  The
 * sceVif1Packet state is treated as opaque by the callers (they only pass its
 * address); the field set below is the SDK's and is sufficient to compile.
 * Implementations are a later (link-time) concern.
 */
#ifndef _LIBVIF_H
#define _LIBVIF_H

#include "scetypes.h"
#include "sce_gs.h"             /* sceGifTag */

typedef struct {
    u_long128 *pBase;          /* packet buffer base                          */
    u_long128 *pCurrent;       /* current write position                      */
    u_long128 *pGifTag;        /* GIF tag currently being filled              */
    u_long128 *pDirect;        /* open DIRECT VIFcode being filled            */
    u_int      nElement;       /* elements written since the last open        */
    u_int      flag;
} sceVif1Packet;

#ifdef __cplusplus
extern "C" {
#endif

void sceVif1PkInit(sceVif1Packet *packet, u_int addr);
void sceVif1PkReset(sceVif1Packet *packet);
void sceVif1PkCnt(sceVif1Packet *packet, int irq);
void sceVif1PkOpenDirectCode(sceVif1Packet *packet, int irq);
void sceVif1PkCloseDirectCode(sceVif1Packet *packet);
void sceVif1PkOpenGifTag(sceVif1Packet *packet, u_long128 *pGifTag);
void sceVif1PkCloseGifTag(sceVif1Packet *packet);
void sceVif1PkReserve(sceVif1Packet *packet, int nBytes);
void sceVif1PkEnd(sceVif1Packet *packet, int irq);
void sceVif1PkTerminate(sceVif1Packet *packet);

#ifdef __cplusplus
}
#endif

#endif /* _LIBVIF_H */
