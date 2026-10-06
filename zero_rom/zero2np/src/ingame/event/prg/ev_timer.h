/* ==========================================================================
 *  ingame/event/prg/ev_timer.h
 *
 *  Event timers (ev_timer.c).
 *
 *  One free-running frame counter plus 30 registration slots.  A slot does
 *  not hold a countdown: EvTimerRegist() converts the requested delay into an
 *  absolute value of the counter, and EvTimerExe() fires the slot on the
 *  frame the counter is exactly equal to it.  Firing hands the work to
 *  Req_CompulsionSetEventState(), so the target event is moved through
 *  ev_change.c's deferred queue rather than torn down from under a running
 *  macro program.
 *
 *  Reconstructed from the Feb 6 2004 prototype (SLES_523.84).
 * ======================================================================== */

#ifndef _INGAME_EVENT_PRG_EV_TIMER_H
#define _INGAME_EVENT_PRG_EV_TIMER_H

#include "eetypes.h"
#include "../../../common/save_data.h"

/* One registered timer.  event_id == -1 marks a free slot; it records the
 * event that asked for the timer, and (req_event_id, req_state) is the state
 * change to post when `timer` comes up. */
typedef struct                      /* 0x10 */
{
    /* 0x0 */ int    event_id;
    /* 0x4 */ u_int  timer;
    /* 0x8 */ int    req_event_id;
    /* 0xc */ u_char req_state;
} REGIST_TIMER;

#define REGIST_TIMER_MAX 30

void EvTimerCtrlInit(void);
void EvTimerMain(void);

/* Post `state` to req_event_id once `timer` frames have elapsed.  Reached
 * from the event macro op EvEvSetTimer(). */
void EvTimerRegist(int event_id, u_int timer, int req_event_id, u_char state);

/* Drop every timer event_id registered. */
void EvTimerRelease(int event_id);

void SetSave_EvTimerCtrl(MC_SAVE_DATA *data);

#endif /* _INGAME_EVENT_PRG_EV_TIMER_H */
