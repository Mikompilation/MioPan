/* ==========================================================================
 *  g3dGsWrapper.h
 *
 *  Thin wrappers over the raw EE DMAC / VIF1 / GIF / GS registers: push a draw
 *  / display environment, swap the double buffer, spin until the GS path
 *  drains (g3dGsSyncPath), and run a GS->local store-image transfer.  Each
 *  routine has a timeout guard that, on expiry, reports the offending stage
 *  through the installed debug handler.
 *
 *  Reconstructed from the Feb 6 2004 prototype (SLES_523.84).
 * ======================================================================== */

#ifndef _G3DGSWRAPPER_H
#define _G3DGSWRAPPER_H

#include "eetypes.h"
#include "sce_gs.h"             /* sceGifTag / sceGsDispEnv / sceGsDBuff / sceGsStoreImage */
#include "gra3dTypes.h"         /* G3DGSSYNCPATHTIMEOUTREASON */

/* Callback invoked when a packet fails to terminate within the spin budget. */
typedef void (*LPFUNC_ONDETECTPACKETDOESNOTTERMINATED)(G3DGSSYNCPATHTIMEOUTREASON r);

int  g3dGsPutDrawEnv(void *pGT);
void g3dGsPutDispEnv(sceGsDispEnv *pDE);
int  g3dGsSwapDBuff(sceGsDBuff *pDB, int id);
int  g3dGsSyncPath(int mode, u_short timeout);
int  g3dGsExecStoreImage(void *sp, u_long128 *dstaddr);
void g3dGsSetDebugHandler(LPFUNC_ONDETECTPACKETDOESNOTTERMINATED pFunc);

#endif /* _G3DGSWRAPPER_H */
