/* ==========================================================================
 *  ingame/event/prg/ev_sis.h
 *
 *  Per-area sister presence (ev_sis.c).
 *
 *  Reconstructed from the Feb 6 2004 prototype (SLES_523.84).
 * ======================================================================== */

#ifndef _INGAME_EVENT_PRG_EV_SIS_H
#define _INGAME_EVENT_PRG_EV_SIS_H

#include "../../../common/save_data.h"

void ev_sisSetSave(MC_SAVE_DATA *save);
void ev_sisInit(void);
void ev_sisRegister(int area_no, int mdl_no, int anm_no);
void ev_sisDelete(int area_no);
void ev_sisRelease(void);
void ev_sisChangeRoom(int room_id);

#endif /* _INGAME_EVENT_PRG_EV_SIS_H */
