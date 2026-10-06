/* ==========================================================================
 *  system/pad/pad.h
 *
 *  Controller-pad subsystem (pad.c) cross-module interface.  paddat/pushdat
 *  point at the current FF2 action tables selected from opt_wrk.pad_type, so
 *  paddat[n] is the n-th mapped action's current hold count and pushdat[n] is
 *  its DualShock2 pressure byte.
 *
 *  Reconstructed from the Feb 6 2004 prototype (SLES_523.84).
 * ======================================================================== */

#ifndef _SYSTEM_PAD_PAD_H
#define _SYSTEM_PAD_PAD_H

#include "eetypes.h"
#include "../../common/variable.h"
#include "key_cnf.h"

/* Current logical button-counter table (sdata 3f36d4). */
extern u_short **paddat;
extern u_char  **pushdat;
extern u_short   sce_pad[16];

int  InitPad(void);
int  padIsConnected(int iId);
int  GetPadDUALSHOCK2(int iId);
int  GetPadStateStable(int iId);
int  PadSyncCallback(void);
int  PadReadFunc(PAD_STRUCT *psp, int p_id);
void SetAnlgInfo(PAD_STRUCT *psp, int p_id);
u_short VibrateRequest(u_short p_id, u_short act1, u_short act2);
u_short VibrateRequest1(u_short p_id, u_short act_1);
u_short VibrateRequest2(u_short p_id, u_short act_2);
void PadClearCount(int p_id);
void PadAnalogInit(void);
void PadAnalogMain(void);
int  GetPadAnalogRpt(int pad_label);

#endif /* _SYSTEM_PAD_PAD_H */
