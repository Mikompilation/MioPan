/* ==========================================================================
 *  ingame/event/prg/ev_exe.h
 *
 *  Event execution (ev_exe.c): the list of macro programs currently running.
 *
 *  ev_exe_ctrl[150] is the execution list.  An entry holds the event's id, a
 *  cursor into its macro program, and the interpreter's per-event state;
 *  EventExe() steps every live entry once a frame through
 *  EventExeFuncCall() (ev_macro.c).  A slot with event_id == -1 is free.
 *
 *  The three Set*Status entry points are the transitions the condition centre
 *  drives: an event whose open condition passed runs its init program, one
 *  that starts for real is put on the list with its main program, and one
 *  whose close condition passed is put on the list with its end program.
 *  Each of them also re-parents the event's sub-events.
 *
 *  Reconstructed from the Feb 6 2004 prototype (SLES_523.84).
 * ======================================================================== */

#ifndef _INGAME_EVENT_PRG_EV_EXE_H
#define _INGAME_EVENT_PRG_EV_EXE_H

#include "eetypes.h"
#include "../../../common/save_data.h"

/* One running macro program.  event_id == -1 marks a free slot. */
typedef struct                      /* 0x10 */
{
    /* 0x0 */ int     event_id;
    /* 0x4 */ u_int   stop_timer;
    /* 0x8 */ u_char *event_addr;   /* cursor into the macro program */
    /* 0xc */ u_char  if_state;
    /* 0xd */ u_char  process;
} EV_EXE_CTRL;

#define EV_EXE_CTRL_MAX 150

void EventExeInit(void);
void EventExe(void);

/* The three transitions.  ev_open.c's control centre drives the first and
 * third -- an event whose open condition passed runs its init program, one
 * whose close condition passed runs its end program.  The middle one is
 * driven by the interpreter instead: the EV_END opcode at the tail of an init
 * program (Event_EvEnd, ev_macro.c) is what promotes the event to its main
 * program. */
void SetEventInitStatus(int event_id);
void SetEventExeStatus(int event_id);
void SetEventEndExeStatus(int event_id);

/* Park every sub-event of a finishing event: those still waiting to open are
 * moved to EV_STATE_SUSPEND, those already running recurse. */
void EventEnd_SubStateChange(int event_id);

/* Put event_id on the execution list running table `use_table` (an EV_TBL_*
 * program index).  Refuses silently when the event is already on the list. */
void SetEventExeCtrl(int event_id, u_char use_table);

/* Drop event_id's entry from the execution list (no-op when it has none). */
void EventExeRelease(int event_id);

/* 1 when event_id currently holds an execution-list entry, -1 when it does
 * not.  ev_change.c waits for -1 before forcing a state change, so that a
 * running macro program is never torn down underneath itself. */
int  CheckEventExeEntry(int event_id);

void SetSave_EvExeCtrl(MC_SAVE_DATA *data);

/* PORT: converts EV_EXE_CTRL::event_addr between a live host pointer and
 * the EE address the save file holds.  See the body in ev_exe.c. */
void EvExeCtrlSavePtrFixup(int to_host);

#endif /* _INGAME_EVENT_PRG_EV_EXE_H */
