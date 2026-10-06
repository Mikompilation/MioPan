/* ==========================================================================
 *  ingame/event/prg/ev_open.h
 *
 *  Event open/close condition evaluation -- the "event control centre".
 *
 *  Every event owns two condition streams in the macro pak: an open stream
 *  that decides when it starts and a close stream that decides when it ends.
 *  EventSetOpenCondition() / EventSetCloseCondition() parse a stream and post
 *  each condition it names into the 250-slot control centre;
 *  EventCtrlCenterMain() re-evaluates every posted slot once a frame and
 *  hands the winners to ev_exe.c.
 *
 *  Reconstructed from the Feb 6 2004 prototype (SLES_523.84).
 * ======================================================================== */

#ifndef _INGAME_EVENT_PRG_EV_OPEN_H
#define _INGAME_EVENT_PRG_EV_OPEN_H

#include "eetypes.h"
#include "../../../common/save_data.h"

/* Object the player last photographed, as seen by the event conditions.
 * Reset every frame at the end of EventCtrlCenterMain(). */
typedef struct                      /* 0x8 */
{
    /* 0x0 */ u_char obj_type;
    /* 0x4 */ int    obj_id;
} EV_PHOTO_OBJ;

extern EV_PHOTO_OBJ ev_photo_obj;   /* sdata 3f0520 */

void EvCtrlCenterInit(void);
void EvCondCtrlInit(void);
void EvPhotoObjInit(void);

void EventSetOpenCondition(int event_id);
void EventSetCloseCondition(int event_id);
void EventDelCondition(int event_id);
void EventCtrlCenterMain(void);

/* Master enables for the two halves of the control centre.  Cut scenes drop
 * these so nothing new triggers while a scripted sequence is running. */
void SetOpenCondSwitch(u_char flg);
void SetEndCondSwitch(u_char flg);

void SetSave_EvCtrlCenter(MC_SAVE_DATA *data);

/* PORT: the EV_CTRL_CENTER::exe_addr half of the same fixup. */
void EvCtrlCenterSavePtrFixup(int to_host);
void EvDbgDispCenter(void);

#endif /* _INGAME_EVENT_PRG_EV_OPEN_H */
