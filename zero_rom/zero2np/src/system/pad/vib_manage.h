/* ==========================================================================
 *  system/pad/vib_manage.h
 *
 *  Controller vibration manager interface.
 *
 *  Reconstructed from the Feb 6 2004 prototype (SLES_523.84).
 * ======================================================================== */

#ifndef _SYSTEM_PAD_VIB_MANAGE_H
#define _SYSTEM_PAD_VIB_MANAGE_H

#ifdef __cplusplus
extern "C" {
#endif

void InitVibrate(void);
void SetVibrate(int type, int time, int pow);
void CallVibrate(void);

#ifdef __cplusplus
}
#endif

#endif /* _SYSTEM_PAD_VIB_MANAGE_H */
