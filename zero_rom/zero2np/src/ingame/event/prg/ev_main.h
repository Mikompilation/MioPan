/* ==========================================================================
 *  ingame/event/prg/ev_main.h
 *
 *  Event runtime top level (ev_main.c): the work block every event subsystem
 *  hangs off, plus the per-frame driver ingame.c calls.
 *
 *  Reconstructed from the Feb 6 2004 prototype (SLES_523.84).
 * ======================================================================== */

#ifndef _INGAME_EVENT_PRG_EV_MAIN_H
#define _INGAME_EVENT_PRG_EV_MAIN_H

#include "../../../common/save_data.h"
#include "../../../graphics/graph3d/ctl/fixed_array.h"
#include "../../../sdk/libvu0.h"

/* Values EVENT_STATE::state takes.  SetEventState() rejects anything above
 * EV_STATE_LOCK and refuses to move an event that has already reached it.
 *
 * All seven are now confirmed: EvDbg_EventStatePrint() (ev_change.c) carries
 * the ROM's own label for each one, quoted below.  The enumerator names here
 * are the port's -- kept because they say what the state means rather than
 * what it was called -- but the ordering is observed, not inferred. */
enum EV_STATE
{
    EV_STATE_WAIT_OPEN = 0,     /* "EV_UNSTART"    armed: open conds polled */
    EV_STATE_INIT      = 1,     /* "EV_EXE_INIT"   init program queued/running */
    EV_STATE_EXE       = 2,     /* "EV_EXE_NOW"    main program running     */
    EV_STATE_WAIT_END  = 3,     /* "EV_END_WAIT"   running: close conds polled */
    EV_STATE_SUSPEND   = 4,     /* "EV_EXE_SLEEP"  parked; re-armed on re-open */
    EV_STATE_END       = 5,     /* "EV_EXE_END"    finished                 */
    EV_STATE_LOCK      = 6      /* "EV_FORCED_END" sealed: never moved again */
};

/* Per-event run state.  1931 of them -- one per event id. */
typedef struct                      /* 0x4 */
{
    /* 0x0 */ u_char    state;
    /* 0x1 */ u_char    dumy1;
    /* 0x2 */ short int dumy2;
} EVENT_STATE;

#define EVENT_STATE_MAX 1931

typedef struct                      /* 0x1e38 */
{
    /* 0x0000 */ int                                     step;
    /* 0x0004 */ int                                     ev_no;
    /* 0x0008 */ fixed_array<EVENT_STATE, EVENT_STATE_MAX> ev_state;
    /* 0x1e34 */ u_char                                  wait_flg;
} EV_WRK;

/* Gaze target an event can pin the sister (or player) to. */
struct CEventGazeWrk                /* 0x20 */
{
protected:
    /* 0x00:0 */ u_char        mActive     : 1 = 0;
    /* 0x00:1 */ u_char        mObjAppoint : 1 = 0;
    /* 0x01   */ char          mObjType = 0;
    /* 0x04   */ int           mObjId = 0;
    /* 0x10   */ sceVu0FVECTOR mPos = { 0 };

public:
    void Init();
    void SetObjType(int iObjType, int iObjId);
    void SetPoint(float *Pos);
};

struct CEventSisterGazeWrk : CEventGazeWrk  /* 0x20 */
{
    void Work();
};

extern EV_WRK              ev_wrk;          /* data 30fb80 */
extern CEventSisterGazeWrk ev_sister_gaze;  /* data 3119c0 */

void EventInit(void);
void EventDataLoadReq(void);
void EventRootStart(void);
void EventMain(void);
void EventEnd(void);

void SetSave_EvWrk(MC_SAVE_DATA *data);
void ev_gazeSisSetSave(MC_SAVE_DATA *save);

#endif /* _INGAME_EVENT_PRG_EV_MAIN_H */
