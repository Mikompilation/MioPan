// FILE: /home/zero_rom/zero2np/src/ingame/event/prg/ev_open.c
//
// Event open/close condition evaluation -- the "event control centre".
//
// Every event carries two condition streams in the macro pak.  A stream is a
// byte-tagged list: each entry starts with a condition label that indexes
// ev_cond_wrk[] for the test function and the entry's size, EV_COND_OR (0x30)
// separates alternative groups, EV_COND_END (0xff) terminates.  Entries
// inside a group are ANDed; groups are ORed.
//
// EventSetOpenCondition() / EventSetCloseCondition() walk a stream and post
// its *first* entry of each group into ev_ctrl_center[], a flat 250-slot
// table.  EventCtrlCenterMain() then re-runs those posted entries once a
// frame; when one passes it re-walks the rest of that entry's group through
// EventConditionJudge() before declaring the whole condition met.  The
// two-stage split is what keeps the per-frame cost to one test per event
// rather than one per condition.
//
// Events that pass their open condition in the same frame do not all start:
// they queue in ev_exe_wait[] and EventPriorityJudge() lets through only
// those matching the highest priority any of them registered.
//
// Reconstructed from the Feb 6 2004 prototype (SLES_523.84).

#include "ev_open.h"

#include <stdio.h>
#include <stdlib.h>                                 // rand

#include "ev_change.h"                              // SetEventState
#include "ev_exe.h"                                 // SetEventInitStatus / SetEventEndExeStatus
#include "ev_get.h"                                 // Get1Byte / Get2Byte / Get4Byte / GetEvState / ...
#include "ev_macro.h"                               // GetSynchroModeFlg
#include "ev_main.h"                                // ev_wrk / EV_STATE_*
#include "../../ingame.h"                           // SendIngameEventLoadEndFlg
#include "../../enemy/enemy.h"                      // IsEnemyOn / GetEneDatStatus
#include "../../enemy/enemy_dat.h"                  // release_typeGetReleaseType
#include "../../item/prg/item.h"                    // GetPlyrItemHaveNum
#include "../../map/MapLoad.h"                      // MapLoadGetOffset / MapLoadMain
#include "../../map/map_rectangle.h"                // MrecIsInEvent
#include "../../photo/photo.h"                      // photo_datIsUp
#include "../../plyr/player.h"                      // GetPlyrAreaNo / InFinderMode / InDamageState
#include "../../plyr/unit_ctl.h"                    // OutSightChk
#include "../../puzzle/puzzle.h"                    // GetPuzzleClearInfo
#include "../../../common/utility.h"                // GetDistV
#include "../../../common/zero2_util.h"             // GetObjectPos
#include "../../../common/utility2.h"               // PRINT_ASSERT / PRINT_WARNING
#include "../../../common/variable.h"               // plyr_wrk / sis_wrk / ingame_wrk
#include "../../../graphics/graph3d/ctl/fixed_array.h"
#include "../../../system/pad/pad.h"                // paddat
#include "../../../miopan/miopan_memory.h"                 // MioPan_GetPs2Address

/* --------------------------------------------------------------------------
 *  Stream vocabulary
 * ------------------------------------------------------------------------ */

/* Condition labels 0x00..0x2f index ev_cond_wrk[]; these two are structural. */
#define EV_COND_OR   0x30           /* alternative-group separator, 4 bytes */
#define EV_COND_END  0xff           /* end of stream                        */

#define EV_COND_MAX  48             /* ev_cond_wrk[] entries                */

/* Object-type tags shared with GetObjectPos() (common/zero2_util.c): 0/1/2/5
 * resolve through GetEnePos(), 3 and 4 are rejected, 6 is the player, 7 the
 * sister, 8 and 9 a registered map-data label, 0xff means "none". */
#define EV_OBJ_PLYR   6
#define EV_OBJ_SIS    7
#define EV_OBJ_REGDAT 8

/* --------------------------------------------------------------------------
 *  Tables
 * ------------------------------------------------------------------------ */

#define EV_CTRL_CENTER_MAX 250      /* posted conditions, all events        */
#define EV_PRI_CTRL_MAX    250      /* priority claims this frame           */
#define EV_EXE_WAIT_MAX    250      /* events whose open condition passed    */

/* One posted condition.  condition == -1 marks a free slot. */
typedef struct                      /* 0x8 */
{
    /* 0x0 */ short int condition;
    /* 0x2 */ short int event_id;
    /* 0x4 */ u_char   *exe_addr;
} EV_CTRL_CENTER;

/* A priority an area/NPC condition claimed for its event this frame. */
typedef struct                      /* 0x8 */
{
    /* 0x0 */ int    event_id;
    /* 0x4 */ u_char priority;
} EV_PRI_CTRL;

/* Master enables for the two halves of the centre. */
typedef struct                      /* 0x2 */
{
    /* 0x0 */ u_char open_switch;
    /* 0x1 */ u_char end_switch;
} EV_COND_CTRL;

/* One row of the condition dispatch table.  cond_size is how far the stream
 * pointer advances past this entry, so it covers the label byte and the
 * entry's payload. */
typedef struct                      /* 0x2c */
{
    /* 0x00 */ u_char cond_label;
    /* 0x04 */ int  (*judge_func)(int event_id, u_char *exe_addr);
    /* 0x08 */ int    cond_size;
    /* 0x0c */ char   label_name[32];
} EV_COND_WRK;

/* Sight cone the *_INTO_SIGHT / OBJ_INTO_PLYR_SIGHT conditions use: 60 degrees
 * wide (OutSightChk halves it), out to 500 units, and never closer than 100 --
 * something already on top of you is not "coming into sight". */
#define EV_PI          3.1415926f
#define EV_SIGHT_ANGLE 1.0471975f
#define EV_SIGHT_DIST  500.0f
#define EV_SIGHT_NEAR  100.0f

/* Rotation windows are authored in whole degrees over [0, 360). */
#define EV_ROT_FULL 360
#define EV_ROT_HALF 180

/* --------------------------------------------------------------------------
 *  Condition tests.  Each returns 1 when its condition holds.  They are
 *  forward-declared here because ev_cond_wrk[] is defined ahead of them, in
 *  the same order as the ROM.
 * ------------------------------------------------------------------------ */

static int EvPlyrAreaIn(int event_id, u_char *exe_addr);
static int EvPlyrAreaOut(int event_id, u_char *exe_addr);
static int EvPlyrRoomIn(int event_id, u_char *exe_addr);
static int EvPlyrRoomOut(int event_id, u_char *exe_addr);
static int EvPlyrRot(int event_id, u_char *exe_addr);
static int EvPlyrIntoSight(int event_id, u_char *exe_addr);
static int EvSisAreaIn(int event_id, u_char *exe_addr);
static int EvSisAreaOut(int event_id, u_char *exe_addr);
static int EvSisRot(int event_id, u_char *exe_addr);
static int EvSisIntoSight(int event_id, u_char *exe_addr);
static int EvGhostExist(int event_id, u_char *exe_addr);
static int EvGhostLost(int event_id, u_char *exe_addr);
static int EvGhostReleaseType(int event_id, u_char *exe_addr);
static int EvNpcAreaIn(int event_id, u_char *exe_addr);
static int EvNpcAreaOut(int event_id, u_char *exe_addr);
static int EvNpcRot(int event_id, u_char *exe_addr);
static int EvNpcDistance(int event_id, u_char *exe_addr);
static int EvPushPad(int event_id, u_char *exe_addr);
static int EvPlyrItemUse(int event_id, u_char *exe_addr);
static int EvPlyrItemHave(int event_id, u_char *exe_addr);
static int EvEvState(int event_id, u_char *exe_addr);
static int EvEvNotState(int event_id, u_char *exe_addr);
static int EvEvRandom(int event_id, u_char *exe_addr);
static int EvPhotoObj(int event_id, u_char *exe_addr);
static int EvNotPhotoObj(int event_id, u_char *exe_addr);
static int EvFinderMode(int event_id, u_char *exe_addr);
static int EvObjIntoFinder(int event_id, u_char *exe_addr);
static int EvObjIntoPlyrSight(int event_id, u_char *exe_addr);
static int EvBattleEnd(int event_id, u_char *exe_addr);
static int EvBattleMode(int event_id, u_char *exe_addr);
static int EvChapterLoadWait(int event_id, u_char *exe_addr);
static int EvEvFailure(int event_id, u_char *exe_addr);
static int EvSetEvPriority(int event_id, u_char *exe_addr);
static int EvPuzzleSuccess(int event_id, u_char *exe_addr);
static int EvNowChapter(int event_id, u_char *exe_addr);
static int EvNotNowChapter(int event_id, u_char *exe_addr);
static int EvNotHaveItem(int event_id, u_char *exe_addr);
static int EvCheckGameDifficulty(int event_id, u_char *exe_addr);
static int EvAboveClearNum(int event_id, u_char *exe_addr);
static int EvFollowClearNum(int event_id, u_char *exe_addr);
static int EvNowSynchroMode(int event_id, u_char *exe_addr);
static int EvNotNowSynchroMode(int event_id, u_char *exe_addr);

static void SetEvCtrlCenter(EV_CTRL_CENTER *center, short int condition,
                            short int event_id, u_char *event_addr);
static void EvPriCtrlInit(void);
static void SetEventPriority(int event_id, u_char priority);
static int  EventSetCondition(int event_id, u_char *exe_addr);
static void EventAutoStart(int event_id);
static void EventPriorityJudge(int *ev_exe_wait, int ev_wait_cnt);
static int  EventOpenJudge(int event_id, u_char *exe_addr);
static int  EventCloseJudge(int event_id, u_char *exe_addr);
static int  EventConditionJudge(int event_id, u_char *exe_addr);

/* The dispatch table.  Several labels share a test function: the "_DEF"
 * variants are authoring conveniences (the default-priority form of the same
 * condition) and resolve to identical behaviour. */
static EV_COND_WRK ev_cond_wrk[EV_COND_MAX] =   /* data 3119e0 */
{
    { 0x00, EvPlyrAreaIn,          8, "PLYR_AREAIN"           },
    { 0x01, EvPlyrAreaIn,          8, "PLYR_AREAIN_DEF"       },
    { 0x02, EvPlyrAreaOut,         8, "PLYR_AREAOUT"          },
    { 0x03, EvPlyrAreaOut,         8, "PLYR_AREAOUT_DEF"      },
    { 0x04, EvPlyrRoomIn,          4, "PLYR_ROOMIN"           },
    { 0x05, EvPlyrRoomOut,         4, "PLYR_ROOMOUT"          },
    { 0x06, EvPlyrRot,             8, "PLYR_ROT"              },
    { 0x07, EvPlyrIntoSight,       8, "PLYR_INTO_SIGHT"       },
    { 0x08, EvSisAreaIn,           8, "SIS_AREAIN"            },
    { 0x09, EvSisAreaIn,           8, "SIS_AREAIN_DEF"        },
    { 0x0a, EvSisAreaOut,          8, "SIS_AREAOUT"           },
    { 0x0b, EvSisAreaOut,          8, "SIS_AREAOUT_DEF"       },
    { 0x0c, EvSisRot,              8, "SIS_ROT"               },
    { 0x0d, EvSisIntoSight,        8, "SIS_INTO_SIGHT"        },
    { 0x0e, EvGhostExist,          4, "GHOST_EXIST"           },
    { 0x0f, EvGhostLost,           4, "GHOST_LOST"            },
    { 0x10, EvGhostReleaseType,    8, "GHOST_RELEASE_TYPE"    },
    { 0x11, EvNpcAreaIn,           8, "NPC_AREAIN"            },
    { 0x12, EvNpcAreaIn,           8, "NPC_AREAIN_DEF"        },
    { 0x13, EvNpcAreaOut,          8, "NPC_AREAOUT"           },
    { 0x14, EvNpcAreaOut,          8, "NPC_AREAOUT_DEF"       },
    { 0x15, EvNpcRot,              8, "NPC_ROT"               },
    { 0x16, EvNpcDistance,         8, "NPC_DISTANCE"          },
    { 0x17, EvPushPad,             4, "PUSH_PAD"              },
    { 0x18, EvPlyrItemUse,         4, "PLYR_ITEM_USE"         },
    { 0x19, EvPlyrItemHave,        4, "PLYR_ITEM_HAVE"        },
    { 0x1a, EvEvState,             4, "EV_STATE"              },
    { 0x1b, EvEvNotState,          4, "EV_NOT_STATE"          },
    { 0x1c, EvEvRandom,            4, "EV_RANDOM"             },
    { 0x1d, EvPhotoObj,            8, "PHOTO_OBJ"             },
    { 0x1e, EvNotPhotoObj,         8, "NOT_PHOTO_OBJ"         },
    { 0x1f, EvFinderMode,          4, "FINDER_MODE"           },
    { 0x20, EvObjIntoFinder,       8, "OBJ_INTO_FINDER"       },
    { 0x21, EvObjIntoPlyrSight,    8, "OBJ_INTO_PLYR_SIGHT"   },
    { 0x22, EvBattleEnd,           4, "BATTLE_END"            },
    { 0x23, EvBattleMode,          4, "BATTLE_MODE"           },
    { 0x24, EvChapterLoadWait,     4, "CHAPTER_LOAD_WAIT"     },
    { 0x25, EvEvFailure,           4, "EV_FAILURE"            },
    { 0x26, EvSetEvPriority,       4, "SET_EV_PRIORITY"       },
    { 0x27, EvPuzzleSuccess,       4, "PUZZLE_SUCCESS"        },
    { 0x28, EvNowChapter,          4, "EV_NOW_CHAPTER"        },
    { 0x29, EvNotNowChapter,       4, "EV_NOT_NOW_CHAPTER"    },
    { 0x2a, EvNotHaveItem,         4, "EV_NOT_HAVE_ITEM"      },
    { 0x2b, EvCheckGameDifficulty, 4, "CHECK_GAME_DIFFICULTY" },
    { 0x2c, EvAboveClearNum,       4, "ABOVE_CLEAR_NUM"       },
    { 0x2d, EvFollowClearNum,      4, "FOLLOW_CLEAR_NUM"      },
    { 0x2e, EvNowSynchroMode,      4, "NOW_SYNCHRO_MODE"      },
    { 0x2f, EvNotNowSynchroMode,   4, "NOT_NOW_SYNCHRO_MODE"  }
};

static fixed_array<EV_CTRL_CENTER, EV_CTRL_CENTER_MAX> ev_ctrl_center; /* bss 47a430 */
static fixed_array<EV_PRI_CTRL, EV_PRI_CTRL_MAX>       ev_pri_ctrl;    /* bss 47ac00 */
static EV_COND_CTRL                                    ev_cond_ctrl;   /* sbss 3f4c50 */
static short int                                       ev_pri_num;     /* sbss 3f4c52 */

EV_PHOTO_OBJ ev_photo_obj;                                             /* sdata 3f0520 */

/* --------------------------------------------------------------------------
 *  Control-centre bookkeeping
 * ------------------------------------------------------------------------ */

void EvCtrlCenterInit(void)                                             /* 251 */
{
    int i;

    for (i = 0; i < EV_CTRL_CENTER_MAX; i++) {                          /* 256 */
        SetEvCtrlCenter(&ev_ctrl_center[i], -1, -1, (u_char *)0);       /* 257 */
    }                                                                   /* 258 */
}

static void SetEvCtrlCenter(EV_CTRL_CENTER *center, short int condition,
                            short int event_id, u_char *event_addr)
{
    center->condition = condition;                                      /* 272 */
    center->event_id = event_id;                                        /* 273 */
    center->exe_addr = event_addr;                                      /* 274 */
}

void EvPhotoObjInit(void)
{
    ev_photo_obj.obj_type = 0xff;                                       /* 284 */
    ev_photo_obj.obj_id = 0;                                            /* 285 */
}

void EvCondCtrlInit(void)
{
    ev_cond_ctrl.open_switch = 1;                                       /* 296 */
    ev_cond_ctrl.end_switch = 1;                                        /* 297 */
}

static void EvPriCtrlInit(void)                                         /* 309 */
{
    int i;

    ev_pri_num = 0;                                                     /* 315 */

    for (i = 0; i < EV_PRI_CTRL_MAX; i++) {                             /* 317 */
        ev_pri_ctrl[i].event_id = -1;
        ev_pri_ctrl[i].priority = 0;
    }                                                                   /* 320 */
}

/* Claim `priority` for event_id this frame.  Only one claim per event
 * survives: a repeat overwrites the earlier entry. */
static void SetEventPriority(int event_id, u_char priority)             /* 331 */
{
    int i;

    for (i = 0; i < EV_PRI_CTRL_MAX; i++) {                             /* 341 */
        if (ev_pri_ctrl[i].event_id == event_id) {                      /* 343 */
            ev_pri_ctrl[i].event_id = event_id;
            ev_pri_ctrl[i].priority = priority;
            break;                                                      /* 347 */
        }
    }                                                                   /* 349 */

    if (i == EV_PRI_CTRL_MAX) {                                         /* 352 */
        ev_pri_ctrl[ev_pri_num].event_id = event_id;                    /* 353 */
        ev_pri_ctrl[ev_pri_num].priority = priority;                    /* 354 */
        ev_pri_num++;                                                   /* 355 */
    }
}

/* --------------------------------------------------------------------------
 *  Posting a stream into the centre
 * ------------------------------------------------------------------------ */

void EventSetOpenCondition(int event_id)                                /* 370 */
{
    u_char *open_addr;
    fixed_array<int, EV_SUB_ID_MAX> id_tbl;
    int i;

    open_addr = EvGetExeAddr(EV_TBL_OPEN_COND, event_id);               /* 380 */
    EvGetSubId(event_id, id_tbl.data());                                /* 382 */

    if (EventSetCondition(event_id, open_addr) == 0)                    /* 386 */
    {                  
        printf("EVENT%d OPEN CONDITION SET ERROR!!\n", event_id);       /* 388 */
    }

    /* Re-arming a parent re-arms any sub-event that was parked when the
     * parent last ended. */
    for (i = 0; i < EV_SUB_ID_MAX; i++) {                               /* 392 */
        if (id_tbl[i] != -1) {                                          /* 394 */
            if (GetEvState(id_tbl[i]) == EV_STATE_SUSPEND) {            /* 399 */
                SetEventState(id_tbl[i], EV_STATE_WAIT_OPEN);           /* 401 */
            }
        }
    }                                                                   /* 404 */
}

void EventSetCloseCondition(int event_id)                               /* 413 */
{
    u_char *close_addr;

    close_addr = EvGetExeAddr(EV_TBL_CLOSE_COND, event_id);             /* 420 */
    SetEventState(event_id, EV_STATE_WAIT_END);                         /* 422 */

    if (EventSetCondition(event_id, close_addr) == 0) {                 /* 424 */
        printf("EVENT%d CLOSE CONDITION SET ERROR!!\n", event_id);      /* 426 */
    }
}                                                                       /* 428 */

/* Walk a condition stream and post the first entry of each alternative group
 * into the centre.  Only the first entry of a group needs polling: once it
 * passes, EventConditionJudge() re-walks the rest of that group.
 *
 * Returns 0 when the centre was full, 1 otherwise.  A stream that registers
 * nothing at all (no conditions -- the event is unconditional) falls through
 * to EventAutoStart(). */
static int EventSetCondition(int event_id, u_char *exe_addr)            /* 441 */
{
    u_char regist_flg;
    int regist_res;
    int condition;
    int i;

    /* PORT: the ROM never checks this, because on the EE the macro pak is
     * always resident and EvGetExeAddr() always yields a real stream.  On the
     * host it answers NULL until EventDataLoadReq() has landed the pak, so
     * guard the walk rather than fault on a null stream. */
    if (exe_addr == (u_char *)0) {
        return 0;
    }

    regist_flg = 0;                                                     /* 449 */
    regist_res = 1;                                                     /* 451 */

    while (1) {                                                         /* 457 */
        condition = Get1Byte(exe_addr);                                 /* 459 */

        if (condition == EV_COND_END) {                                 /* 462 */
            if (regist_flg == 0) {                                      /* 465 */
                EventAutoStart(event_id);                               /* 467 */
            }
            break;
        }

        if (condition == EV_COND_OR) {                                  /* 472 */
            /* New group: the next entry needs posting too. */
            regist_flg = 0;                                             /* 477 */
            exe_addr = exe_addr + 4;                                    /* 479 */
            continue;
        }

        if (regist_flg == 0) {                                          /* 482 */
            for (i = 0; i < EV_CTRL_CENTER_MAX; i++) {                  /* 490 */
                if (ev_ctrl_center[i].condition == -1) {
                    SetEvCtrlCenter(&ev_ctrl_center[i], (short int)condition,
                                    (short int)event_id, exe_addr);
                    regist_flg = 1;
                    break;                                              /* 499 */
                }
            }                                                           /* 501 */

            if (i == EV_CTRL_CENTER_MAX) {                              /* 504 */
                PRINT_ASSERT("EVENT CENTER REGIST ERROR! EventSetCondition()\n"); /* 506 */
                regist_res = 0;
                return regist_res;                                      /* 509 */
            }
        }

        exe_addr = exe_addr + ev_cond_wrk[condition].cond_size;         /* 513 */
    }

    return regist_res;                                                  /* 518 */
}

/* An event with no open condition at all starts (or ends) immediately. */
static void EventAutoStart(int event_id)                                /* 532 */
{
    u_char ev_state;

    ev_state = GetEvState(event_id);                                    /* 536 */

    if (ev_state == EV_STATE_WAIT_OPEN) {                               /* 538 */
        SetEventInitStatus(event_id);                                   /* 539 */
    } else if (ev_state == EV_STATE_WAIT_END) {                         /* 540 */
        SetEventEndExeStatus(event_id);                                 /* 541 */
    }
}                                                                       /* 543 */

void EventDelCondition(int event_id)                                    /* 566 */
{
    int i;

    for (i = 0; i < EV_CTRL_CENTER_MAX; i++) {                          /* 571 */
        if (ev_ctrl_center[i].event_id == event_id) {                   /* 572 */
            SetEvCtrlCenter(&ev_ctrl_center[i], -1, -1, (u_char *)0);   /* 573 */
        }
    }                                                                   /* 575 */
}

/* --------------------------------------------------------------------------
 *  Per-frame evaluation
 * ------------------------------------------------------------------------ */

void EventCtrlCenterMain(void)                                          /* 589 */
{
    int center_res;
    int i;
    int j;
    u_char *exe_addr;
    int event_id;
    u_char ev_state;
    fixed_array<int, EV_EXE_WAIT_MAX> ev_exe_wait;
    int ev_wait_cnt;

    ev_wait_cnt = 0;                                                    /* 602 */

    for (i = 0; i < EV_EXE_WAIT_MAX; i++) {                             /* 603 */
        ev_exe_wait[i] = -1;
    }                                                                   /* 605 */

    EvPriCtrlInit();                                                    /* 608 */

    for (i = 0; i < EV_CTRL_CENTER_MAX; i++) {                          /* 612 */
        if (ev_ctrl_center[i].event_id != -1) {
            event_id = ev_ctrl_center[i].event_id;
            exe_addr = ev_ctrl_center[i].exe_addr;

            if (ev_cond_wrk[ev_ctrl_center[i].condition].judge_func != 0) { /* 625 */
                center_res =
                    ev_cond_wrk[ev_ctrl_center[i].condition].judge_func(event_id, exe_addr);

                /* Step past the posted entry: whatever follows inside this
                 * group is what EventOpenJudge()/EventCloseJudge() walk. */
                exe_addr = exe_addr + ev_cond_wrk[ev_ctrl_center[i].condition].cond_size;

                if (center_res == 1) {                                  /* 632 */
                    ev_state = GetEvState(event_id);                    /* 634 */

                    if (ev_state == EV_STATE_WAIT_OPEN) {               /* 637 */
                        if (ev_cond_ctrl.open_switch != 0) {            /* 639 */
                            if (EventOpenJudge(event_id, exe_addr) == 1) { /* 641 */
                                /* One entry per event: an event can post the
                                 * same condition from several groups. */
                                for (j = 0; j < ev_wait_cnt; j++) {     /* 646 */
                                    if (ev_exe_wait[j] == event_id) {
                                        break;
                                    }
                                }                                       /* 650 */

                                if (j == ev_wait_cnt) {                 /* 653 */
                                    ev_exe_wait[ev_wait_cnt] = event_id; /* 655 */
                                    ev_wait_cnt++;                      /* 656 */
                                }
                            }
                        }
                    } else if (ev_state == EV_STATE_WAIT_END) {         /* 662 */
                        if (ev_cond_ctrl.end_switch != 0) {             /* 664 */
                            if (EventCloseJudge(event_id, exe_addr) == 1) { /* 666 */
                                SetEventEndExeStatus(event_id);         /* 671 */
                            }
                        }
                    } else {
                        PRINT_ASSERT("Error!! EventCtrlCenterMain!!");  /* 677 */
                        break;                                          /* 679 */
                    }
                }
            } else {
                PRINT_ASSERT("Error!! EventCtrlCenterMain() Condition ID %3d", /* 686 */
                             ev_ctrl_center[i].condition);
                break;                                                  /* 689 */
            }
        }
    }                                                                   /* 691 */

    EventPriorityJudge(ev_exe_wait.data(), ev_wait_cnt);

    /* The photo hit only lives for the frame it was reported in. */
    EvPhotoObjInit();                                                   /* 698 */
}

/* Of the events that passed their open condition this frame, start only those
 * whose registered priority equals the highest any of them claimed.  Events
 * that claimed no priority at all are always started. */
static void EventPriorityJudge(int *ev_exe_wait, int ev_wait_cnt)       /* 715 */
{
    int i;
    int j;
    u_char ev_pri_high;
    u_char priority_flg;
    int set_priority;

    ev_pri_high = 0;                                                    /* 734 */

    for (i = 0; i < ev_pri_num; i++) {                                  /* 741 */
        if (ev_pri_ctrl[i].event_id != -1) {
            for (j = 0; j < ev_wait_cnt; j++) {                         /* 746 */
                if (ev_exe_wait[j] == ev_pri_ctrl[i].event_id) {
                    if (ev_pri_high < ev_pri_ctrl[i].priority) {
                        ev_pri_high = ev_pri_ctrl[i].priority;
                    }
                }
            }                                                           /* 756 */
        }
    }                                                                   /* 759 */

    for (i = 0; i < ev_wait_cnt; i++) {                                 /* 769 */
        priority_flg = 0;                                               /* 770 */
        set_priority = 0;

        for (j = 0; j < ev_pri_num; j++) {                              /* 774 */
            if (ev_pri_ctrl[j].event_id != -1) {
                if (ev_pri_ctrl[j].event_id == ev_exe_wait[i]) {
                    set_priority = j;
                    priority_flg = 1;
                    break;                                              /* 783 */
                }
            }
        }                                                               /* 785 */

        if (priority_flg != 0) {                                        /* 788 */
            if (ev_pri_ctrl[set_priority].priority == ev_pri_high) {
                SetEventInitStatus(ev_exe_wait[i]);                     /* 792 */
            }
        } else {
            SetEventInitStatus(ev_exe_wait[i]);                         /* 797 */
        }
    }                                                                   /* 799 */
}

static int EventOpenJudge(int event_id, u_char *exe_addr)
{
    return EventConditionJudge(event_id, exe_addr);                     /* 818 */
}

static int EventCloseJudge(int event_id, u_char *exe_addr)
{
    return EventConditionJudge(event_id, exe_addr);                     /* 839 */
}

/* Walk the remainder of the stream from the entry after the posted one.
 * Entries within a group AND together; EV_COND_OR restarts the accumulator,
 * and reaching a separator with the accumulator still set means the group
 * that just ended already passed, so the whole condition is met. */
static int EventConditionJudge(int event_id, u_char *exe_addr)          /* 854 */
{
    int judge_res;
    u_int condition;

    judge_res = 1;                                                      /* 860 */

    while (1) {                                                         /* 865 */
        condition = Get1Byte(exe_addr);                                 /* 867 */

        if (condition == EV_COND_END) {                                 /* 870 */
            break;
        }

        if (condition == EV_COND_OR) {                                  /* 874 */
            if (judge_res == 1) {                                       /* 876 */
                break;
            }

            judge_res = 1;
            exe_addr = exe_addr + 4;                                    /* 884 */
            continue;
        }

        /* Once a group has failed, the rest of it is skipped -- only the
         * stream pointer keeps advancing. */
        if (judge_res == 1) {                                           /* 890 */
            if (ev_cond_wrk[condition].judge_func != 0) {               /* 891 */
                judge_res = ev_cond_wrk[condition].judge_func(event_id, exe_addr); /* 892 */
            } else {
                PRINT_ASSERT("Error!! EventConditionJudge");            /* 910 */
                judge_res = 0;
                return judge_res;                                       /* 912 */
            }
        }

        exe_addr = exe_addr + ev_cond_wrk[condition].cond_size;         /* 916 */
    }

    return judge_res;                                                   /* 921 */
}

void SetOpenCondSwitch(u_char flg)
{
    ev_cond_ctrl.open_switch = flg;                                     /* 935 */
}

void SetEndCondSwitch(u_char flg)
{
    ev_cond_ctrl.end_switch = flg;                                      /* 946 */
}

/* --------------------------------------------------------------------------
 *  The condition tests
 *
 *  Stream layout is per-condition, but the shape repeats: byte 0 is the
 *  label, byte 1 a priority or sub-type, bytes 2-3 a half-word, bytes 4-7 a
 *  word.  Each test reads only what it needs.
 * ------------------------------------------------------------------------ */

static int EvPlyrAreaIn(int event_id, u_char *exe_addr)                 /* 962 */
{
    int res;
    u_char priority;
    u_int label;

    priority = Get1Byte(exe_addr + 1);                                  /* 972 */
    label = Get4Byte(exe_addr + 4);                                     /* 977 */

    res = 0;                                                            /* 990 */

    if (MrecIsInEvent(plyr_wrk.cmn_wrk.mbox.pos, label,                 /* 981 */
                      plyr_wrk.cmn_wrk.floor) != 0) {
        if (GetEvState(event_id) == EV_STATE_WAIT_OPEN) {               /* 983 */
            SetEventPriority(event_id, priority);                       /* 985 */
        }
        res = 1;                                                        /* 987 */
    }

    return res;                                                         /* 994 */
}

static int EvPlyrAreaOut(int event_id, u_char *exe_addr)                /* 1004 */
{
    int res;
    u_char priority;
    u_int label;

    priority = Get1Byte(exe_addr + 1);                                  /* 1014 */
    label = Get4Byte(exe_addr + 4);                                     /* 1019 */

    res = 0;                                                            /* 1036 */

    if (MrecIsInEvent(plyr_wrk.cmn_wrk.mbox.pos, label,                 /* 1026 */
                      plyr_wrk.cmn_wrk.floor) == 0) {
        if (GetEvState(event_id) == EV_STATE_WAIT_OPEN) {               /* 1029 */
            SetEventPriority(event_id, priority);                       /* 1031 */
        }
        res = 1;                                                        /* 1033 */
    }

    return res;                                                         /* 1040 */
}

static int EvPlyrRoomIn(int event_id, u_char *exe_addr)                 /* 1050 */
{
    /* Narrowed to a byte here but kept as a half-word in ROOMOUT below --
     * the ROM really is inconsistent about it. */
    u_char room_no;

    (void)event_id;

    room_no = (u_char)Get2Byte(exe_addr + 2);                           /* 1059 */

    return (GetPlyrAreaNo() == room_no);                                /* 1063, 1071 */
}

static int EvPlyrRoomOut(int event_id, u_char *exe_addr)                /* 1081 */
{
    u_short room_no;

    (void)event_id;

    room_no = Get2Byte(exe_addr + 2);                                   /* 1090 */

    return (GetPlyrAreaNo() != room_no);                                /* 1094, 1102 */
}

static int EvPlyrRot(int event_id, u_char *exe_addr)                    /* 1112 */
{
    int res;
    short int min;
    short int max;
    u_short rot;
    u_short range;
    short int plyr_rot;

    (void)event_id;

    rot = Get2Byte(exe_addr + 2);                                       /* 1124 */
    range = Get2Byte(exe_addr + 4);                                     /* 1126 */

    if (rot > EV_ROT_FULL) {                                            /* 1129 */
        rot = (u_short)(rot % EV_ROT_FULL);                             /* 1130 */
    }
    if (range > EV_ROT_FULL) {                                          /* 1132 */
        range = (u_short)(range % EV_ROT_FULL);                         /* 1133 */
    }

    min = (short int)(rot - range);                                     /* 1136 */
    max = (short int)(rot + range);                                     /* 1137 */

    /* Model heading is radians in (-PI, PI]; the window is degrees in
     * [0, 360).  The +180 / conditional -+180 dance below is the ROM's way of
     * writing "negative degrees wrap to the top of the circle": after the
     * first line the value is 0..360, so the low arm fires exactly when the
     * heading was negative.  Kept in its original shape, including the
     * unsigned 16-bit comparisons.
     *
     * EvSisRot() skips this fold-up entirely and compares the raw signed
     * degrees -- a genuine asymmetry between the two, not a slip here. */
    plyr_rot = (short int)((plyr_wrk.cmn_wrk.mbox.rot[1] * 180.0f) / EV_PI); /* 1140 */
    plyr_rot = (short int)(plyr_rot + EV_ROT_HALF);                     /* 1142 */

    if ((u_short)plyr_rot < EV_ROT_HALF) {                              /* 1145 */
        plyr_rot = (short int)(plyr_rot + EV_ROT_HALF);                 /* 1146 */
    } else if ((u_short)(plyr_rot - EV_ROT_HALF) <= EV_ROT_HALF) {      /* 1148 */
        plyr_rot = (short int)(plyr_rot - EV_ROT_HALF);                 /* 1149 */
    }

    res = 0;                                                            /* 1155 */

    if (range < EV_ROT_HALF) {
        if (min < 0) {                                                  /* 1160 */
            /* Window wraps below 0. */
            if (((min + EV_ROT_FULL) <= plyr_rot) &&                    /* 1161 */
                (plyr_rot <= EV_ROT_FULL)) {
                res = 1;
            } else if ((0 <= plyr_rot) && (plyr_rot <= max)) {          /* 1163 */
                res = 1;
            }
        } else if (max > EV_ROT_FULL) {                                 /* 1166 */
            /* Window wraps above 360. */
            if ((min <= plyr_rot) && (plyr_rot <= EV_ROT_FULL)) {       /* 1167 */
                res = 1;
            } else if ((0 <= plyr_rot) &&                               /* 1169 */
                       (plyr_rot <= (max - EV_ROT_FULL))) {
                res = 1;
            }
        } else {
            if (min <= plyr_rot) {                                      /* 1173 */
                res = (plyr_rot <= max);
            }
        }
    } else {
        /* A half-range of 180 degrees or more covers the whole circle. */
        res = 1;
    }

    return res;                                                         /* 1180 */
}

static int EvPlyrIntoSight(int event_id, u_char *exe_addr)              /* 1190 */
{
    int res;
    float target_pos[4];
    float *room_off;

    (void)event_id;

    /* The point is authored room-local, so it needs the room's world offset. */
    room_off = MapLoadGetOffset(GetPlyrAreaNo());                       /* 1200 */

    if (room_off == 0) {                                                /* 1202 */
        res = 0;                                                        /* 1223 */
    } else {
        res = 0;

        target_pos[0] = (float)(short int)Get2Byte(exe_addr + 2) + room_off[0]; /* 1204 */
        target_pos[1] = (float)(short int)Get2Byte(exe_addr + 4) + room_off[1]; /* 1206 */
        target_pos[2] = (float)(short int)Get2Byte(exe_addr + 6) + room_off[2]; /* 1208 */
        target_pos[3] = 0.0f;                                           /* 1210 */

        if (OutSightChk(target_pos, plyr_wrk.cmn_wrk.mbox.pos,          /* 1216 */
                        plyr_wrk.cmn_wrk.mbox.rot[1],
                        EV_SIGHT_ANGLE, EV_SIGHT_DIST) == 0) {
            if (GetDistV(target_pos, plyr_wrk.cmn_wrk.mbox.pos) < EV_SIGHT_NEAR) { /* 1217 */
                res = 0;
            } else {
                res = 1;                                                /* 1218 */
            }
        }
    }

    return res;                                                         /* 1227 */
}

static int EvSisAreaIn(int event_id, u_char *exe_addr)                  /* 1237 */
{
    int res;
    u_char priority;
    u_int label;

    priority = Get1Byte(exe_addr + 1);                                  /* 1247 */
    label = Get4Byte(exe_addr + 4);                                     /* 1252 */

    res = 0;                                                            /* 1269 */

    if (MrecIsInEvent(sis_wrk.cmn_wrk.mbox.pos, label,                  /* 1259 */
                      sis_wrk.cmn_wrk.floor) != 0) {
        if (GetEvState(event_id) == EV_STATE_WAIT_OPEN) {               /* 1262 */
            SetEventPriority(event_id, priority);                       /* 1264 */
        }
        res = 1;                                                        /* 1266 */
    }

    return res;                                                         /* 1273 */
}

static int EvSisAreaOut(int event_id, u_char *exe_addr)                 /* 1283 */
{
    int res;
    u_char priority;
    u_int label;

    priority = Get1Byte(exe_addr + 1);                                  /* 1293 */
    label = Get4Byte(exe_addr + 4);                                     /* 1298 */

    res = 0;                                                            /* 1315 */

    if (MrecIsInEvent(sis_wrk.cmn_wrk.mbox.pos, label,                  /* 1305 */
                      sis_wrk.cmn_wrk.floor) == 0) {
        if (GetEvState(event_id) == EV_STATE_WAIT_OPEN) {               /* 1308 */
            SetEventPriority(event_id, priority);                       /* 1310 */
        }
        res = 1;                                                        /* 1312 */
    }

    return res;                                                         /* 1319 */
}

static int EvSisRot(int event_id, u_char *exe_addr)                     /* 1329 */
{
    int res;
    short int min;
    short int max;
    u_short rot;
    u_short range;
    short int sis_rot;

    (void)event_id;

    rot = Get2Byte(exe_addr + 2);                                       /* 1341 */
    range = Get2Byte(exe_addr + 4);                                     /* 1343 */

    if (rot > EV_ROT_FULL) {                                            /* 1346 */
        rot = (u_short)(rot % EV_ROT_FULL);                             /* 1347 */
    }
    if (range > EV_ROT_FULL) {                                          /* 1349 */
        range = (u_short)(range % EV_ROT_FULL);                         /* 1350 */
    }

    min = (short int)(rot - range);                                     /* 1353 */
    max = (short int)(rot + range);                                     /* 1354 */

    /* Unlike EvPlyrRot() this never folds the heading into [0, 360) -- the
     * ROM compares the raw signed degree value against the window. */
    sis_rot = (short int)((sis_wrk.cmn_wrk.mbox.rot[1] * 180.0f) / EV_PI); /* 1357 */

    res = 0;                                                            /* 1359 */

    if (range < EV_ROT_HALF) {                                          /* 1362 */
        if (min < 0) {                                                  /* 1367 */
            if (((min + EV_ROT_FULL) <= sis_rot) &&                     /* 1368 */
                (sis_rot <= EV_ROT_FULL)) {
                res = 1;
            } else if ((0 <= sis_rot) && (sis_rot <= max)) {            /* 1370 */
                res = 1;
            }
        } else if (max > EV_ROT_FULL) {                                 /* 1373 */
            if ((min <= sis_rot) && (sis_rot <= EV_ROT_FULL)) {         /* 1374 */
                res = 1;
            } else if ((0 <= sis_rot) &&                                /* 1376 */
                       (sis_rot <= (max - EV_ROT_FULL))) {
                res = 1;
            }
        } else {
            if (min <= sis_rot) {                                       /* 1380 */
                res = (sis_rot <= max);
            }
        }
    } else {
        res = 1;
    }

    return res;                                                         /* 1387 */
}

static int EvSisIntoSight(int event_id, u_char *exe_addr)               /* 1397 */
{
    int res;
    float target_pos[4];

    (void)event_id;

    res = 0;

    /* No room offset here -- the sister variant takes the point as world
     * space where the player variant takes it room-local. */
    target_pos[0] = (float)(short int)Get2Byte(exe_addr + 2);           /* 1406 */
    target_pos[1] = (float)(short int)Get2Byte(exe_addr + 4);           /* 1408 */
    target_pos[2] = (float)(short int)Get2Byte(exe_addr + 6);           /* 1410 */
    target_pos[3] = 0.0f;                                               /* 1412 */

    if (OutSightChk(target_pos, sis_wrk.cmn_wrk.mbox.pos,               /* 1418 */
                    sis_wrk.cmn_wrk.mbox.rot[1],
                    EV_SIGHT_ANGLE, EV_SIGHT_DIST) == 0) {
        if (GetDistV(target_pos, sis_wrk.cmn_wrk.mbox.pos) >= EV_SIGHT_NEAR) { /* 1419 */
            res = 1;
        }
    }

    return res;                                                         /* 1425 */
}

/* dat_no reaches the enemy tables sign-extended from 16 bits (the ROM's
 * sll/sra pair), unlike the event-id and puzzle-id conditions below which
 * pass their half-word through unsigned. */
static int EvGhostExist(int event_id, u_char *exe_addr)                 /* 1435 */
{
    u_char ene_type;
    short int dat_no;

    (void)event_id;

    ene_type = Get1Byte(exe_addr + 1);                                  /* 1445 */
    dat_no = (short int)Get2Byte(exe_addr + 2);                         /* 1448 */

    return (GetEneDatStatus(ene_type, dat_no) != ENE_STATUS_NO_USE);    /* 1462 */
}

static int EvGhostLost(int event_id, u_char *exe_addr)                  /* 1472 */
{
    u_char ene_type;
    short int dat_no;

    (void)event_id;

    ene_type = Get1Byte(exe_addr + 1);                                  /* 1482 */
    dat_no = (short int)Get2Byte(exe_addr + 2);                         /* 1485 */

    return (GetEneDatStatus(ene_type, dat_no) == ENE_STATUS_NO_USE);    /* 1501 */
}

static int EvGhostReleaseType(int event_id, u_char *exe_addr)           /* 1511 */
{
    u_char ene_type;
    short int dat_no;
    u_char release_type;

    (void)event_id;

    ene_type = Get1Byte(exe_addr + 1);                                  /* 1522 */
    dat_no = (short int)Get2Byte(exe_addr + 2);                         /* 1525 */
    release_type = Get1Byte(exe_addr + 4);                              /* 1528 */

    return (release_typeGetReleaseType(ene_type, dat_no) == release_type); /* 1534 */
}                                                                       /* 1539 */

/* The NPC area tests were never finished: they read their operands, register
 * the event's priority and then pass unconditionally.  Kept faithful -- the
 * shipped event data relies on them always succeeding. */
static int EvNpcAreaIn(int event_id, u_char *exe_addr)                  /* 1549 */
{
    u_char priority;

    priority = Get1Byte(exe_addr + 1);                                  /* 1560 */
    Get2Byte(exe_addr + 2);     /* npc id     -- read and discarded */  /* 1563 */
    Get4Byte(exe_addr + 4);     /* area label -- read and discarded */  /* 1566 */

    if (GetEvState(event_id) == EV_STATE_WAIT_OPEN) {                   /* 1573 */
        SetEventPriority(event_id, priority);                           /* 1575 */
    }

    return 1;                                                           /* 1579 */
}

static int EvNpcAreaOut(int event_id, u_char *exe_addr)                 /* 1589 */
{
    u_char priority;

    priority = Get1Byte(exe_addr + 1);                                  /* 1600 */
    Get2Byte(exe_addr + 2);                                             /* 1603 */
    Get4Byte(exe_addr + 4);                                             /* 1606 */

    if (GetEvState(event_id) == EV_STATE_WAIT_OPEN) {                   /* 1613 */
        SetEventPriority(event_id, priority);                           /* 1615 */
    }

    return 1;                                                           /* 1619 */
}

static int EvNpcRot(int event_id, u_char *exe_addr)
{
    (void)event_id; (void)exe_addr;
    return 1;                                                           /* 1639 */
}

static int EvNpcDistance(int event_id, u_char *exe_addr)
{
    (void)event_id; (void)exe_addr;
    return 1;                                                           /* 1659 */
}

static int EvPushPad(int event_id, u_char *exe_addr)                    /* 1669 */
{
    int res;
    int pad_label;

    (void)event_id;

    res = 0;

    pad_label = Get2Byte(exe_addr + 2);                                 /* 1678 */

    /* Viewfinder, damage reactions and a waiting event all swallow input. */
    if ((InFinderMode() == 0) && (InDamageState() == 0)) {              /* 1682 */
        if (GetEvWrkWaitFlg() == 0) {                                   /* 1690 */
            /* paddat[n] is the hold count: 1 is the frame it went down. */
            res = (*paddat[pad_label] == 1);                            /* 1692 */
        }
    }

    return res;                                                         /* 1705 */
}

static int EvPlyrItemUse(int event_id, u_char *exe_addr)
{
    (void)event_id; (void)exe_addr;
    return 1;                                                           /* 1725 */
}

static int EvPlyrItemHave(int event_id, u_char *exe_addr)               /* 1735 */
{
    u_char need_num;
    u_char item_id;

    (void)event_id;

    need_num = Get1Byte(exe_addr + 1);                                  /* 1745 */
    item_id = Get1Byte(exe_addr + 2);                                   /* 1749 */

    return (GetPlyrItemHaveNum(item_id) >= need_num);                   /* 1752 */
}

static int EvEvState(int event_id, u_char *exe_addr)                    /* 1770 */
{
    u_char state;
    u_short other_id;

    (void)event_id;

    state = Get1Byte(exe_addr + 1);                                     /* 1780 */
    other_id = Get2Byte(exe_addr + 2);                                  /* 1784 */

    return (state == GetEvState(other_id));                             /* 1788 */
}

static int EvEvNotState(int event_id, u_char *exe_addr)                 /* 1806 */
{
    u_char state;
    u_short other_id;

    (void)event_id;

    state = Get1Byte(exe_addr + 1);                                     /* 1816 */
    other_id = Get2Byte(exe_addr + 2);                                  /* 1820 */

    return (state != GetEvState(other_id));                             /* 1824 */
}

static int EvEvRandom(int event_id, u_char *exe_addr)                   /* 1842 */
{
    int roll;

    (void)event_id;

    /* Rolled before the threshold is read, so the draw happens even when the
     * comparison is about to fail. */
    roll = MioPan_Rand() % 100;                                         /* 1852 */

    return (roll < Get2Byte(exe_addr + 2));                             /* 1854 */
}

static int EvPhotoObj(int event_id, u_char *exe_addr)                   /* 1872 */
{
    int res;
    u_char obj_type;
    u_int photo_dat_no;

    (void)event_id;

    res = 0;

    obj_type = Get1Byte(exe_addr + 1);                                  /* 1882 */
    photo_dat_no = Get4Byte(exe_addr + 4);                              /* 1887 */

    if (obj_type == EV_OBJ_REGDAT) {                                    /* 1899 */
        res = (photo_datIsUp(photo_dat_no) != 0);                       /* 1902 */
    } else {
        PRINT_WARNING("EvPhotoObj Illegalobj_id");                      /* 1907 */
    }

    return res;                                                         /* 1940 */
}

static int EvNotPhotoObj(int event_id, u_char *exe_addr)                /* 1950 */
{
    int res;
    u_char obj_type;
    u_int photo_dat_no;

    (void)event_id;

    res = 0;

    obj_type = Get1Byte(exe_addr + 1);                                  /* 1960 */
    photo_dat_no = Get4Byte(exe_addr + 4);                              /* 1965 */

    if (obj_type == EV_OBJ_REGDAT) {                                    /* 1976 */
        res = (photo_datIsUp(photo_dat_no) == 0);                       /* 1979 */
    } else {
        /* The ROM reuses EvPhotoObj's wording here. */
        PRINT_WARNING("EvPhotoObj Illegalobj_id");                      /* 1984 */
    }

    return res;                                                         /* 1989 */
}

static int EvFinderMode(int event_id, u_char *exe_addr)                 /* 1999 */
{
    int res;
    u_char finder_flg;

    (void)event_id;

    res = 0;

    finder_flg = Get1Byte(exe_addr + 2);                                /* 2008 */

    if (InFinderMode() != 0) {                                          /* 2015 */
        if (finder_flg == 1) {                                          /* 2016 */
            res = 1;
        }
    } else {                                                            /* 2017 */
        res = (finder_flg == 0);                                        /* 2022 */
    }

    return res;                                                         /* 2028 */
}

/* Unimplemented in this prototype: every recognised object type falls through
 * to "not in the viewfinder", and only an unrecognised one is reported. */
static int EvObjIntoFinder(int event_id, u_char *exe_addr)              /* 2038 */
{
    u_char obj_type;

    (void)event_id;

    obj_type = Get1Byte(exe_addr + 1);                                  /* 2048 */
    Get4Byte(exe_addr + 4);     /* obj id -- read and discarded */      /* 2053 */

    switch (obj_type) {                                                 /* 2057 */
    case 0:                     /* enemy classes, via GetEnePos()       */
    case 1:
    case 2:
    case 5:
    case EV_OBJ_SIS:            /* 7                                    */
    case EV_OBJ_REGDAT:         /* 8                                    */
    case 9:                     /* registered map data                  */
    case 0xff:                  /* none                                 */
        break;

    default:                    /* 3, 4 and 6 are not accepted here     */
        printf("ERROR!! EvObjIntoFinder()\n");                          /* 2068 */
        break;
    }

    return 0;                                                           /* 2072 */
}

static int EvObjIntoPlyrSight(int event_id, u_char *exe_addr)           /* 2082 */
{
    int res;
    float target_pos[4];
    u_char obj_type;
    u_int obj_id;

    (void)event_id;

    res = 0;

    obj_type = Get1Byte(exe_addr + 1);                                  /* 2093 */
    obj_id = Get4Byte(exe_addr + 4);                                    /* 2098 */

    if (GetObjectPos(target_pos, obj_type, obj_id) == 0) {              /* 2102 */
        printf("ERROR!! EvObjIntoPlyrSight()\n");                       /* 2111 */
    } else {
        if (OutSightChk(target_pos, plyr_wrk.cmn_wrk.mbox.pos,          /* 2104 */
                        plyr_wrk.cmn_wrk.mbox.rot[1],
                        EV_SIGHT_ANGLE, EV_SIGHT_DIST) == 0) {
            if (GetDistV(target_pos, plyr_wrk.cmn_wrk.mbox.pos) < EV_SIGHT_NEAR) { /* 2105 */
                res = 0;
            } else {
                res = 1;                                                /* 2106 */
            }
        }
    }

    return res;                                                         /* 2115 */
}

static int EvBattleEnd(int event_id, u_char *exe_addr)
{
    (void)event_id; (void)exe_addr;
    return (IsEnemyOn() == 0);                                          /* 2140 */
}

static int EvBattleMode(int event_id, u_char *exe_addr)
{
    (void)event_id; (void)exe_addr;
    return (IsEnemyOn() != 0);                                          /* 2173 */
}

static int EvChapterLoadWait(int event_id, u_char *exe_addr)            /* 2191 */
{
    int res;

    (void)event_id; (void)exe_addr;

    res = 0;

    if (MapLoadMain() == 0) {                                           /* 2197 */
        SendIngameEventLoadEndFlg(1);                                   /* 2198 */
        res = 1;
    }

    return res;                                                         /* 2203 */
}

/* Never passes: authored into a stream to make a group permanently false. */
static int EvEvFailure(int event_id, u_char *exe_addr)
{
    (void)event_id; (void)exe_addr;
    return 0;                                                           /* 2218 */
}

/* Not really a test -- it exists purely to attach a priority to an event
 * whose other conditions carry none. */
static int EvSetEvPriority(int event_id, u_char *exe_addr)              /* 2228 */
{
    u_char priority;

    priority = Get1Byte(exe_addr + 1);                                  /* 2236 */
    SetEventPriority(event_id, priority);                               /* 2240 */

    return 1;                                                           /* 2243 */
}

static int EvPuzzleSuccess(int event_id, u_char *exe_addr)              /* 2253 */
{
    u_char want;
    u_short puzzle_id;

    (void)event_id;

    want = Get1Byte(exe_addr + 1);                                      /* 2263 */
    puzzle_id = Get2Byte(exe_addr + 2);                                 /* 2266 */

    return (want == GetPuzzleClearInfo(puzzle_id));                     /* 2270 */
}

static int EvNowChapter(int event_id, u_char *exe_addr)                 /* 2285 */
{
     u_char chapter_no = Get1Byte(exe_addr + 1);                        /* 2294 */

    return (ingame_wrk.mChapterNo == chapter_no);                /* 2304 */
}

static int EvNotNowChapter(int event_id, u_char *exe_addr)              /* 2314 */
{
    u_char chapter_no;

    (void)event_id;

    chapter_no = Get1Byte(exe_addr + 1);                                /* 2323 */

    return !(ingame_wrk.mChapterNo == chapter_no);                /* 2336 */
}

static int EvNotHaveItem(int event_id, u_char *exe_addr)                /* 2346 */
{
    u_char item_id;

    (void)event_id;

    item_id = Get1Byte(exe_addr + 2);                                   /* 2355 */

    return (GetPlyrItemHaveNum(item_id) == 0);                          /* 2360 */
}

static int EvCheckGameDifficulty(int event_id, u_char *exe_addr)        /* 2378 */
{
    u_char difficulty;

    (void)event_id;

    difficulty = Get1Byte(exe_addr + 2);                                /* 2387 */

    return (ingame_wrk.mDifficulty == difficulty);               /* 2400 */
}

static int EvAboveClearNum(int event_id, u_char *exe_addr)              /* 2410 */
{
    u_char clear_num;

    (void)event_id;

    clear_num = Get1Byte(exe_addr + 2);                                 /* 2419 */

    return (ingame_wrk.mClearCnt >= clear_num);                  /* 2432 */
}

static int EvFollowClearNum(int event_id, u_char *exe_addr)             /* 2442 */
{
    u_char clear_num;

    (void)event_id;

    clear_num = Get1Byte(exe_addr + 2);                                 /* 2451 */

    return (ingame_wrk.mClearCnt <= clear_num);                  /* 2464 */
}

static int EvNowSynchroMode(int event_id, u_char *exe_addr)
{
    (void)event_id; (void)exe_addr;
    return (GetSynchroModeFlg() == 1);                                  /* 2479 */
}

static int EvNotNowSynchroMode(int event_id, u_char *exe_addr)
{
    (void)event_id; (void)exe_addr;
    return (GetSynchroModeFlg() == 0);                                  /* 2502 */
}

/* --------------------------------------------------------------------------
 *  Save / debug
 * ------------------------------------------------------------------------ */

void SetSave_EvCtrlCenter(MC_SAVE_DATA *data)                           /* 2521 */
{
    data->addr = (u_char *)ev_ctrl_center.data();
    /* PORT: the ROM hard-codes 0x7d0 (250 * 8).  EV_CTRL_CENTER holds a live
     * pointer, so the entry is wider on a 64-bit host and the literal would
     * under-describe the block. */
    data->size = sizeof(ev_ctrl_center);                                /* 2525 */
}

/* PORT DEVIATION -- no ROM counterpart.  EV_CTRL_CENTER::exe_addr is the same
 * hazard EvExeCtrlSavePtrFixup() documents in ev_exe.c: a live pointer into
 * the event macro pak, saved verbatim into a file that outlives the process
 * whose RAM base it was built from. */
void EvCtrlCenterSavePtrFixup(int to_host)
{
    int i;

    for (i = 0; i < EV_CTRL_CENTER_MAX; i++) {
        u_char *p = ev_ctrl_center[i].exe_addr;

        if (p == nullptr) {
            continue;
        }

        if (to_host != 0) {
            ev_ctrl_center[i].exe_addr =
                (u_char *)MioPan_GetHostPointer((uintptr_t)p);
        } else {
            uintptr_t ee = MioPan_GetPs2Address(p);

            if (ee != 0) {
                ev_ctrl_center[i].exe_addr = (u_char *)ee;
            }
        }
    }
}

void EvDbgDispCenter(void)                                              /* 2536 */
{
    int i;
    int regist_num;

    regist_num = 0;                                                     /* 2541 */

    printf("*************************************************\n");      /* 2544 */
    printf("*          EVENT CONTROL CENTER STATUS          *\n");      /* 2545 */
    printf("*************************************************\n");      /* 2546 */

    for (i = 0; i < EV_CTRL_CENTER_MAX; i++) {                          /* 2547 */
        if (ev_ctrl_center[i].event_id != -1) {
            regist_num++;
            printf("Condition Label [ %s ]", ev_cond_wrk[ev_ctrl_center[i].condition].label_name);
            printf("  Event ID %d\n", ev_ctrl_center[i].event_id);
        }
    }                                                                   /* 2554 */

    printf("***** Event Control Center Regist Num [%d / %d] *****\n", regist_num, EV_CTRL_CENTER_MAX); /* 2556 */
}
