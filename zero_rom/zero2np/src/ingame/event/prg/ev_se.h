/* ==========================================================================
 *  ingame/event/prg/ev_se.h
 *
 *  Per-room event sound-bank management (ev_se.c).
 *
 *  Reconstructed from the Feb 6 2004 prototype (SLES_523.84).
 * ======================================================================== */

#ifndef _INGAME_EVENT_PRG_EV_SE_H
#define _INGAME_EVENT_PRG_EV_SE_H

#include "../../../common/save_data.h"

void ev_seInit(void);
void ev_seSetSave(MC_SAVE_DATA *save);
void ev_seRelease(void);
void ev_seRegisterFile(int room_id, int file_no);
void ev_seDeleteFile(int room_id, int file_no);
int  ev_seGetBankID(int file_no);
void ev_seChangeRoom(int room_id);
int  ev_seIsReady(void);

#endif /* _INGAME_EVENT_PRG_EV_SE_H */
