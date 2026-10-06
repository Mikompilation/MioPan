// FILE: /home/zero_rom/zero2np/src/ingame/event/prg/ev_change.c
//
// Compulsory (forced) event state changes.
//
// An event can be forced to a new state from anywhere, but not at any time:
// while its macro program is on ev_exe.c's execution list, tearing it down
// would pull the program out from under itself.  So the module has two halves.
// CompulsionSetEventState() is the immediate path -- it retires the event's
// posted conditions, releases its execution entry, moves it, and recurses into
// its sub-events through CompulsionSet_ForcedEndState().  Req_/Del_ are the
// deferred path: a request parks in ev_change_ctrl[] and
// CompulsionSetEventStateMain() polls it once a frame, applying it only when
// the target has reached a quiet state AND has no execution entry left.
//
// Reconstructed from the Feb 6 2004 prototype (SLES_523.84).

#include "ev_change.h"

#include <stdio.h>                                  // printf (debug dumps)

#include "ev_exe.h"                                 // SetEventInitStatus / EventExeRelease / CheckEventExeEntry
#include "ev_get.h"                                 // GetEvState / EvGetParentID / EvGetSubId / EV_SUB_ID_MAX
#include "ev_main.h"                                // ev_wrk / EVENT_STATE_MAX / EV_STATE_*
#include "ev_open.h"                                // EventDelCondition / EventSetOpenCondition
#include "../../../common/utility2.h"               // PRINT_ASSERT
#include "../../../graphics/graph3d/ctl/fixed_array.h"

static void CompulsionSet_ForcedEndState(int event_id);
static int  GetEmpty_EvChangeCtrl(int change_id);

/* Parked state-change requests.  Saved to the memory card as one block, so
 * a change queued before a save survives the reload. */
static fixed_array<EV_CHANGE_CTRL, EV_CHANGE_CTRL_MAX> ev_change_ctrl;  /* bss 478e40 */

void EvChangeCtrlInit(void)
{                                                                       /* 53 */

    for (int i = 0; i < EV_CHANGE_CTRL_MAX; i++) {                          /* 58 */
        ev_change_ctrl[i].set_event_id = -1;
        ev_change_ctrl[i].change_id    = -1;
        ev_change_ctrl[i].change_state = 0;
    }                                                                   /* 62 */
}

void SetEventState(int event_id, u_char state)
{                                                                       /* 76 */
    if ((u_int)event_id >= EVENT_STATE_MAX) {                           /* 80 */
        PRINT_ASSERT("Error! %s event_id %d", __FUNCTION__, event_id);  /* 81 */
    }

    if (state > EV_STATE_LOCK) {                                        /* 83 */
        PRINT_ASSERT("Error! %s state %d", __FUNCTION__, state);        /* 84 */
    }

    /* A sealed event stays sealed. */
    if (ev_wrk.ev_state[event_id].state != EV_STATE_LOCK) {
        ev_wrk.ev_state[event_id].state = state;
    }
}

void SetEventWaitFlg(u_char flg)
{                                                                       /* 100 */
    if (flg > 1) {                                                      /* 103 */
        PRINT_ASSERT("Error! %s", __FUNCTION__);                        /* 104 */
    }

    ev_wrk.wait_flg = flg;                                              /* 108 */
}

void CompulsionSetEventStateMain(void)
{                                                                       /* 121 */

    for (int i = 0; i < EV_CHANGE_CTRL_MAX; i++) {                          /* 127 */
        if (ev_change_ctrl[i].change_id != -1) {

            /* Only these two states are quiet enough to be overwritten
             * from outside; anything else is mid-program. */
            switch (GetEvState(ev_change_ctrl[i].change_id)) {          /* 134 */
            case EV_STATE_WAIT_OPEN:
            case EV_STATE_END:
                /* ... and only once the execution list has let it go. */
                if (CheckEventExeEntry(ev_change_ctrl[i].change_id) == -1) {
                    CompulsionSetEventState(ev_change_ctrl[i].change_id,
                                            ev_change_ctrl[i].change_state);
                    Del_CompulsionSetEventState(ev_change_ctrl[i].change_id);
                }
                break;
            }
        }
    }                                                                   /* 144 */
}

void CompulsionSetEventState(int change_id, u_char state)
{                                                                       /* 158 */
    u_char parent_state;
    int    event_id;

    if ((u_int)change_id >= EVENT_STATE_MAX) {                          /* 165 */
        PRINT_ASSERT("Error! %s change_id %d", __FUNCTION__, change_id); /* 166 */
    }

    if (state > EV_STATE_LOCK) {                                        /* 168 */
        PRINT_ASSERT("Error! %s state %d", __FUNCTION__, state);        /* 169 */
    }

    event_id     = EvGetParentID(change_id);                            /* 174 */
    parent_state = GetEvState(event_id);                                /* 176 */

    /* A sealed event is not forced anywhere. */
    if (GetEvState(change_id) == EV_STATE_LOCK) {                       /* 180 */
        return;
    }

    SetEventState(change_id, state);                                    /* 182 */
    EventDelCondition(change_id);                                       /* 186 */
    EventExeRelease(change_id);                                         /* 188 */

    switch (state) {                                                    /* 191 */
    case EV_STATE_WAIT_OPEN:
        /* Re-post the open conditions -- unless the parent is itself still
         * waiting to open, in which case it will re-arm this event when it
         * does (EventSetOpenCondition() walks the sub-event list). */
        if (parent_state != EV_STATE_WAIT_OPEN) {                       /* 197 */
            EventSetOpenCondition(change_id);                           /* 199 */
        }
        break;

    case EV_STATE_INIT:
        SetEventInitStatus(change_id);                                  /* 210 */
        break;

    case EV_STATE_SUSPEND:
        /* Parking under a parent that has already finished means nothing
         * will ever wake this event, so it stays parked without conditions;
         * otherwise it is parked *and* re-armed. */
        if (parent_state == EV_STATE_END ||
            parent_state == EV_STATE_LOCK) {                            /* 214 */
            SetEventState(change_id, EV_STATE_SUSPEND);                 /* 217 */
        } else {
            SetEventState(change_id, EV_STATE_SUSPEND);                 /* 222 */
            EventSetOpenCondition(change_id);                           /* 224 */
        }
        break;

    case EV_STATE_LOCK:
        CompulsionSet_ForcedEndState(change_id);                        /* 229 */
        break;

    case EV_STATE_WAIT_END:
    case EV_STATE_END:
        break;

    default:
        PRINT_ASSERT("ERROR!! CompulsionSetEventState() EventID %d\n", change_id);  /* 241 */
        break;
    }
}                                                                       /* 243 */

/* Seal event_id's whole sub-tree.  Each sub-event is retired according to how
 * far it had got: one still waiting has its posted conditions dropped first,
 * one already running is queued rather than cut off, and every one of them
 * recurses so the seal reaches the leaves.
 *
 * The assert text names EventEnd_SubStateChange() -- the ROM's own copy/paste
 * from ev_exe.c, preserved verbatim. */
static void CompulsionSet_ForcedEndState(int event_id)
{                                                                       /* 257 */
    fixed_array<int, EV_SUB_ID_MAX> id_tbl;

    if ((u_int)event_id >= EVENT_STATE_MAX) {                           /* 266 */
        PRINT_ASSERT("Error! %s event_id %d", __FUNCTION__, event_id);  /* 267 */
    }

    EvGetSubId(event_id, &id_tbl[0]);

    for (int i = 0; i < EV_SUB_ID_MAX; i++) {
        if (id_tbl[i] == -1) {
            continue;
        }

        switch (GetEvState(id_tbl[i])) {                                /* 281 */
        case EV_STATE_WAIT_OPEN:
            EventDelCondition(id_tbl[i]);
            /* fall through */
        case EV_STATE_SUSPEND:
            SetEventState(id_tbl[i], EV_STATE_LOCK);
            /* fall through */
        case EV_STATE_INIT:
        case EV_STATE_EXE:
        case EV_STATE_WAIT_END:
            Req_CompulsionSetEventState(event_id, id_tbl[i], EV_STATE_LOCK);
            /* fall through */
        case EV_STATE_END:
            CompulsionSet_ForcedEndState(id_tbl[i]);
            break;                                                      /* 299 */

        default:
            PRINT_ASSERT("ERROR!! EventEnd_SubStateChange() event id = %d, "
                         "sub event id = %d\n", event_id, id_tbl[i]);   /* 305 */
            /* fall through */
        case EV_STATE_LOCK:
            break;
        }
    }                                                                   /* 310 */
}

void Req_CompulsionSetEventState(int set_event_id, int change_id, u_char state)
{                                                                       /* 322 */

    if ((u_int)set_event_id >= EVENT_STATE_MAX) {                       /* 328 */
        PRINT_ASSERT("Error! %s event_id %d", __FUNCTION__, set_event_id); /* 329 */
    }

    if ((u_int)change_id >= EVENT_STATE_MAX) {                          /* 331 */
        PRINT_ASSERT("Error! %s event_id %d", __FUNCTION__, change_id); /* 332 */
    }

    if (state > EV_STATE_LOCK) {                                        /* 334 */
        PRINT_ASSERT("Error! %s state %d", __FUNCTION__, state);        /* 335 */
    }

    int empty = GetEmpty_EvChangeCtrl(change_id);                           /* 339 */

    if (empty != -1) {                                                  /* 342 */
        ev_change_ctrl[empty].set_event_id = set_event_id;
        ev_change_ctrl[empty].change_id    = change_id;
        ev_change_ctrl[empty].change_state = state;
    } else {
        PRINT_ASSERT("ERROR!! EV_CHANGE_CTRL NO EMPTY!!!!\n");          /* 351 */
    }
}

void Del_CompulsionSetEventState(int change_id)
{                                                                       /* 362 */

    if ((u_int)change_id >= EVENT_STATE_MAX) {                          /* 368 */
        PRINT_ASSERT("Error! %s event_id %d", __FUNCTION__, change_id); /* 369 */
    }

    for (int i = 0; i < EV_CHANGE_CTRL_MAX; i++) {                          /* 374 */
        if (ev_change_ctrl[i].change_id == change_id) {
            ev_change_ctrl[i].set_event_id = -1;
            ev_change_ctrl[i].change_id    = -1;
            ev_change_ctrl[i].change_state = 0;
        }
    }                                                                   /* 380 */
}

/* Slot Req_CompulsionSetEventState() should write.  An existing request for
 * the same event wins over the first free slot, so re-requesting a change
 * overwrites in place instead of queueing the event twice.  -1 when the queue
 * is full and change_id is not already in it. */
static int GetEmpty_EvChangeCtrl(int change_id)
{                                                                       /* 393 */
    u_char found_flg;
    int    empty;

    if ((u_int)change_id >= EVENT_STATE_MAX) {                          /* 401 */
        PRINT_ASSERT("Error! %s event_id %d", __FUNCTION__, change_id); /* 402 */
    }

    found_flg = 0;                                                      /* 406 */
    empty     = -1;                                                     /* 407 */

    for (int i = 0; i < EV_CHANGE_CTRL_MAX; i++) {                          /* 410 */
        if (found_flg == 0 && ev_change_ctrl[i].change_id == -1) {      /* 411 */
            empty     = i;                                             /* 413 */
            found_flg = 1;                                             /* 414 */
        }

        if (ev_change_ctrl[i].change_id == change_id) {
            empty = i;                                                 /* 421 */
            break;
        }
    }                                                                   /* 423 */

    return empty;                                                       /* 426 */
}

void SetSave_EvChangeCtrl(MC_SAVE_DATA *data)
{                                                                       /* 437 */
    data->size = sizeof(EV_CHANGE_CTRL) * EV_CHANGE_CTRL_MAX;                                                 /* 441 */
    data->addr = (u_char *)&ev_change_ctrl[0];
}

/* --------------------------------------------------------------------------
 *  Debug dumps
 *
 *  The label table is the ROM's own naming for EV_STATE_*, which is why the
 *  strings do not match the enumerator names in ev_main.h.
 * ------------------------------------------------------------------------ */

void EvDbg_EventStatePrint(void)
{                                                                       /* 453 */
    char *state[7] = {                                                  /* 455 */
        "EV_UNSTART",
        "EV_EXE_INIT",
        "EV_EXE_NOW",
        "EV_END_WAIT",
        "EV_EXE_SLEEP",
        "EV_EXE_END",
        "EV_FORCED_END"
    };

    printf("**************************************\n");                /* 469 */
    printf("*          ALL EVENT STATUS          *\n");                 /* 470 */
    printf("**************************************\n");                 /* 471 */

    for (int i = 0; i < EVENT_STATE_MAX; i++) {                             /* 472 */
        printf("Event %d Status  [ %s ]\n", i, state[ev_wrk.ev_state[i].state]);
    }                                                                   /* 474 */
}

void EvDbg_CompulsionSetPrint(void)
{                                                                       /* 482 */
    char *state[7] = {                                                  /* 484 */
        "EV_UNSTART",
        "EV_EXE_INIT",
        "EV_EXE_NOW",
        "EV_END_WAIT",
        "EV_EXE_SLEEP",
        "EV_EXE_END",
        "EV_FORCED_END"
    };

    printf("*************************************************\n");      /* 498 */
    printf("*          Compulsion Set Event Status          *\n");      /* 499 */
    printf("*************************************************\n");      /* 500 */

    for (int i = 0; i < EV_CHANGE_CTRL_MAX; i++) {                          /* 501 */
        if (ev_change_ctrl[i].change_id != -1) {
            printf("RequestID %d, ChangeID %d, ChangeState [ %s ]\n",
                   ev_change_ctrl[i].set_event_id,
                   ev_change_ctrl[i].change_id,
                   state[ev_change_ctrl[i].change_state]);
        }
    }                                                                   /* 507 */
}
