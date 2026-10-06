/* ==========================================================================
 *  sdk/libpad.h  (SCE PS2 EE <libpad.h> -- pad library surface)
 *
 *  Minimal PC-port declaration header for modules that include the PS2 SDK pad
 *  library.  The current reconstruction only needs the header to resolve; the
 *  concrete scePad* declarations can be filled in as pad.c is reconstructed.
 * ======================================================================== */

#ifndef _SDK_LIBPAD_H
#define _SDK_LIBPAD_H

#include "eetypes.h"

#define scePadDmaBufferMax      16

#define scePadStateDiscon       0
#define scePadStateFindPad      1
#define scePadStateFindCTP1     2
#define scePadStateExecCmd      5
#define scePadStateStable       6
#define scePadStateError        7
#define scePadStateClosed       99

#define scePadReqStateComplete  0
#define scePadReqStateFaild     1
#define scePadReqStateFailed    1
#define scePadReqStateBusy      2

#define InfoModeCurID           1
#define InfoModeCurExID         2
#define InfoModeCurExOffs       3
#define InfoModeIdTable         4

#ifdef __cplusplus
extern "C" {
#endif

int scePadInit(int mode);
int scePadEnd(void);
int scePadPortOpen(int port, int slot, void *addr);
int scePadPortClose(int port, int slot);
int scePadRead(int port, int slot, unsigned char *rdata);
int scePadGetState(int port, int slot);
int scePadGetReqState(int port, int slot);
int scePadInfoAct(int port, int slot, int actno, int term);
int scePadInfoComb(int port, int slot, int listno, int offs);
int scePadInfoMode(int port, int slot, int term, int offs);
int scePadSetMainMode(int port, int slot, int offs, int lock);
int scePadSetActDirect(int port, int slot, const unsigned char *data);
int scePadSetActAlign(int port, int slot, const unsigned char *data);
int scePadInfoPressMode(int port, int slot);
int scePadEnterPressMode(int port, int slot);
int scePadExitPressMode(int port, int slot);
int scePadGetSlotMax(int port);
int scePadSetWarningLevel(int level);

#ifdef __cplusplus
}
#endif

#endif /* _SDK_LIBPAD_H */
