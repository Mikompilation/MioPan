/* ==========================================================================
 *  ingame/event/prg/ev_change.h
 *
 *  Compulsory (forced) event state changes (ev_change.c).
 *
 *  SetEventState() is the one place an event's EV_STATE_* is written, and it
 *  refuses to move an event that has reached EV_STATE_LOCK.  Everything else
 *  in this module exists to move an event that is *not* safely stoppable
 *  right now: Req_CompulsionSetEventState() parks the request in a 30-slot
 *  queue, and CompulsionSetEventStateMain() applies it once the event has
 *  dropped off ev_exe.c's execution list, so a running macro program is never
 *  torn down underneath itself.
 *
 *  Reconstructed from the Feb 6 2004 prototype (SLES_523.84).
 * ======================================================================== */

#ifndef _INGAME_EVENT_PRG_EV_CHANGE_H
#define _INGAME_EVENT_PRG_EV_CHANGE_H

#include "eetypes.h"
#include "../../../common/save_data.h"

/* One parked state change.  change_id == -1 marks a free slot; set_event_id
 * records which event asked for it (only the debug dump reads it back). */
typedef struct                      /* 0xc */
{
    /* 0x0 */ int    set_event_id;
    /* 0x4 */ int    change_id;
    /* 0x8 */ u_char change_state;
} EV_CHANGE_CTRL;

#define EV_CHANGE_CTRL_MAX 30

void EvChangeCtrlInit(void);
void CompulsionSetEventStateMain(void);

/* Move one event to a new EV_STATE_*.  An event that has reached
 * EV_STATE_LOCK is never moved again. */
void SetEventState(int event_id, u_char state);

/* Global "an event is holding the game" flag, read back by GetEvWrkWaitFlg(). */
void SetEventWaitFlg(u_char flg);

/* Apply a state change to change_id now, cascading into its sub-events.
 * Callers that cannot guarantee the event is idle queue through
 * Req_CompulsionSetEventState() instead. */
void CompulsionSetEventState(int change_id, u_char state);

/* Queue / cancel a state change for change_id. */
void Req_CompulsionSetEventState(int set_event_id, int change_id, u_char state);
void Del_CompulsionSetEventState(int change_id);

void SetSave_EvChangeCtrl(MC_SAVE_DATA *data);

/* Debug dumps (ev_debug.c drives these). */
void EvDbg_EventStatePrint(void);
void EvDbg_CompulsionSetPrint(void);

#endif /* _INGAME_EVENT_PRG_EV_CHANGE_H */
