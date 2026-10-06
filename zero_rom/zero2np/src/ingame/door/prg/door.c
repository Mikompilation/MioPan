// FILE: /home/zero_rom/zero2np/src/ingame/door/prg/door.c
//
// Doors: the lock state of every door in the game, and the two reactions the
// event interpreter can ask for when the player walks into one.
//
// The state itself is tiny -- door_ctrl is 208 bytes, one lock kind per door
// label, and it is handed to the save system verbatim by SetSave_DoorCtrl().
// Everything else here is sequencing.
//
// EvDoorOpen() (ev_macro.c) drives both paths.  Unlocked: DoorOpenInit()
// swings the room data over and latches the motion, then DoorOpen() is polled
// until the load, the motion and the sound have all settled.  Locked:
// DoorLockStateExeInit() latches the message/rattle pair for that lock kind
// out of door_lock_info[], and DoorLockStateExe() plays it until the message
// window closes.  DoorClose() is the far side of the transition -- it is
// called once the player is through, and re-homes the SE, sister and enemy
// bookkeeping onto whichever area she actually landed in.
//
// Reconstructed from the Feb 6 2004 prototype (SLES_523.84), door.o
// 0x00136098..0x001368b0.

#include "door.h"

#include "../../ingame.h"                       /* SetIngameDoorMode    */
#include "../../enemy/enemy.h"                  /* EnemyEffectPosUpdate */
#include "../../event/prg/ev_ene.h"             /* ev_eneChangeRoom     */
#include "../../event/prg/ev_se.h"              /* ev_seChangeRoom      */
#include "../../event/prg/ev_sis.h"             /* ev_sisChangeRoom     */
#include "../../map/MapDoor.h"                  /* MapDoorAnimOpen      */
#include "../../map/MapLoad.h"                  /* MapLoadUpdatRoomDat  */
#include "../../map/MhCtl.h"                    /* MhCtlGetRoomNo       */
#include "../../plyr/player.h"                  /* InDamageState        */
#include "../../plyr/plyr_mdl.h"                /* playerUseDoorLight   */
#include "../../../common/utility2.h"           /* PRINT_ASSERT         */
#include "../../../common/variable.h"           /* plyr_wrk             */
#include "../../../graphics/graph2d/message.h"  /* PrintMsgDef_W        */
#include "../../../graphics/graph3d/ctl/fixed_array.h"
#include "../../../system/eeiop/snd_util.h"     /* snd_utilAutoBDPlay   */
#include "../../../system/pad/pad.h"            /* paddat               */

#include <stdio.h>

/* Message type the locked-door text is looked up under. */
#define DOOR_LOCK_MSG_TYPE  11

/* Volume / pitch the rattle is always played at. */
#define DOOR_LOCK_SE_VOL    0x3200
#define DOOR_LOCK_SE_PITCH  0x1000

/* paddat[] slot for the "advance the message" action. */
#define PAD_MSG_NEXT        3

static fixed_array<DOOR_CTRL, DOOR_MAX_NUM> door_ctrl;      /* bss 423098  */
static u_char                               lock_exe_step;  /* sbss 3f4b5c */

/* Non-static in the ROM even though only this file touches it. */
DOOR_LOCK_STATE_CTRL                        lock_state_ctrl; /* sdata 3efb60 */


void DoorCtrlInit(void)
{                                                                       /* 78 */
    int i;

    for (i = 0; i < DOOR_MAX_NUM; i++)                                  /* 83 */
    {
        door_ctrl[i].lock_id = 0;                                       /* 85 */
    }

    lock_exe_step = 0;                                                  /* 87 */
}


/* ==========================================================================
 *  Opening
 * ======================================================================== */

/* Puts the far room in place and latches the door motion.  Returns -1 when
 * the transition cannot start; EvDoorOpen() treats that as "opcode done"
 * rather than stranding the event on a door that will never open. */
int DoorOpenInit(int door_id)
{
    if (MapLoadUpdatRoomDat(door_id) < 0) return -1;                    /* 106 */

    if (InDamageState() != 0)                                           /* 107 */
    {
        /* The ROM really does pass an empty message here (sdata 3efb58);
         * only the FILE/LINE banner carries any information. */
        PRINT_WARNING("");                                              /* 108 */
        return -1;                                                      /* 109 */
    }

    MapDoorSetAnimID(door_id);                                          /* 113 */

    SetIngameDoorMode(1);                                               /* 115 */

    return 0;                                                           /* 117 */
}                                                                       /* 118 */


/* Polled every frame while the transition runs.  The four gates are the room
 * load, the player's door motion, the sound bank and the door model itself --
 * all four have to be ready before the door is allowed to swing. */
int DoorOpen(int door_id)
{
    if (door_id >= DOOR_MAX_NUM)                                        /* 132 */
    {
        PRINT_ASSERT("Error! DoorOpen door id %d\n", door_id);          /* 133 */
    }

    if (MapLoadCheckLoadNow() != 0) return 0;                           /* 139 */

    if (DoorMotionIsEnd() == 0) return 0;                               /* 141 */

    if (DoorSEIsReady() == 0) return 0;                                 /* 143 */

    if (MapDoorCheckLoad() == 0) return 0;                              /* 145 */

    MapDoorAnimOpen();                                                  /* 151 */

    return 1;                                                           /* 153 */
}                                                                       /* 154 */


/* ==========================================================================
 *  Closing
 * ======================================================================== */

/* Runs once the player is through.  Where she ended up decides the area the
 * SE, sister and enemy tables are re-homed to: MhCtlGetRoomNo() answers from
 * her actual position, and only falls back on the recorded area when the
 * position is outside every room of that floor. */
void DoorClose(int door_id)
{                                                                       /* 164 */
    int room_no;

    if (door_id >= DOOR_MAX_NUM)                                        /* 170 */
    {
        PRINT_ASSERT("Error! DoorClose door id %d\n", door_id);         /* 171 */
    }

    room_no = MhCtlGetRoomNo(plyr_wrk.cmn_wrk.floor,                    /* 177 */
                             plyr_wrk.cmn_wrk.mbox.pos);
    if (room_no >= 0)                                                   /* 178 */
    {
        SetPlyrAreaNo(room_no);                                         /* 179 */
    }
    else
    {
        room_no = GetPlyrAreaNo();                                      /* 181 */
    }

    ev_seChangeRoom(room_no);                                           /* 186 */
    ev_sisChangeRoom(room_no);                                          /* 187 */
    ev_eneChangeRoom(room_no);                                          /* 188 */

    MapDoorAnimClose(door_id);                                          /* 190 */

    playerUseDoorLight(0);                                              /* 192 */
}


/* ==========================================================================
 *  Lock state
 * ======================================================================== */

void DoorLock(int door_id, u_char lock_id)
{                                                                       /* 204 */
    if (door_id >= DOOR_MAX_NUM)                                        /* 208 */
    {
        PRINT_ASSERT("Error! DoorLock door id %d lock id %d\n",
                     door_id, lock_id);                                 /* 209 */
    }
    if (lock_id >= DOOR_LOCK_MAX_NUM)                                   /* 211 */
    {
        PRINT_ASSERT("Error! DoorLock door id %d lock id %d\n",
                     door_id, lock_id);                                 /* 212 */
    }

    door_ctrl[door_id].lock_id = lock_id;
}


void DoorUnlock(int door_id)
{
    if (door_id >= DOOR_MAX_NUM)                                        /* 231 */
    {
        PRINT_ASSERT("Error! DoorUnLock door id %d\n", door_id);        /* 232 */
    }

    door_ctrl[door_id].lock_id = 0;
}


/* ==========================================================================
 *  Locked-door reaction
 * ======================================================================== */

/* Latches the message / rattle pair for this door's lock kind.  Note the
 * range check does not return -- the ROM falls through and indexes anyway,
 * which the fixed_array bound check then catches a second time. */
void DoorLockStateExeInit(int door_id)
{                                                                       /* 247 */
    /* The ROM emits one sltiu here, i.e. the signed and unsigned halves of
     * the range test were folded; the older functions above only check the
     * upper bound. */
    if (door_id < 0 || door_id >= DOOR_MAX_NUM)                         /* 250 */
    {
        PRINT_ASSERT("Error! %s door id %d\n", __FUNCTION__, door_id);  /* 251 */
    }

    lock_state_ctrl.msg_id   = door_lock_info[door_ctrl[door_id].lock_id].msg_id;    /* 255 */
    lock_state_ctrl.sound_id = door_lock_info[door_ctrl[door_id].lock_id].sound_id;  /* 256 */
}


/* Runs the reaction until the message window is dismissed.  lock_exe_step
 * keeps the rattle to one shot across the frames this is polled for, and is
 * cleared again on the way out so the next locked door starts clean. */
int DoorLockStateExe(int door_id)
{                                                                       /* 267 */
    int res;
    int msg_id;
    int msg_state;

    if (door_id < 0 || door_id >= DOOR_MAX_NUM)                         /* 274 */
    {
        PRINT_ASSERT("Error! %s door id %d\n", __FUNCTION__, door_id);  /* 275 */
        return 1;                                                       /* 276 */
    }

    if (door_ctrl[door_id].lock_id >= DOOR_LOCK_MAX_NUM)
    {
        PRINT_ASSERT("Error! %s door id %d lock id %d", __FUNCTION__,
                     door_id, door_ctrl[door_id].lock_id);              /* 285 */
        return 1;                                                       /* 286 */
    }

    res    = 0;                                                         /* 289 */
    msg_id = lock_state_ctrl.msg_id;                                    /* 291 */

    if (lock_exe_step == 0)                                             /* 293 */
    {
        /* The bank header sits one file below the sound itself. */
        snd_utilAutoBDPlay(lock_state_ctrl.sound_id,                    /* 295 */
                           lock_state_ctrl.sound_id - 1,
                           1, 0, DOOR_LOCK_SE_VOL, DOOR_LOCK_SE_PITCH,
                           0, (SND_3D_SET *)0);
        lock_exe_step = 1;                                              /* 297 */

        EnemyEffectPosUpdate();                                         /* 299 */
    }

    PrintMsgDef_W(DOOR_LOCK_MSG_TYPE, msg_id);                          /* 303 */

    msg_state = MesStatusCheck();                                       /* 305 */
    if (msg_state == 0)                                                 /* 307 */
    {
        lock_exe_step = 0;                                              /* 308 */
        res           = 1;                                              /* 309 */
    }
    else if (msg_state == 1)                                            /* 312 */
    {
        if (*paddat[PAD_MSG_NEXT] == 1)                                 /* 313 */
        {
            MesSetNextPage();                                           /* 314 */
        }
    }

    return res;                                                         /* 319 */
}                                                                       /* 320 */


/* ==========================================================================
 *  Accessors
 * ======================================================================== */

DOOR_CTRL *GetDoorInfo(int door_id)
{
    if (door_id >= DOOR_MAX_NUM)                                        /* 335 */
    {
        PRINT_ASSERT("Error! GetDoorInfo door id %d\n", door_id);       /* 336 */
    }

    return &door_ctrl[door_id];
}


u_char GetDoorLockState(int door_id)
{
    if (door_id >= DOOR_MAX_NUM)                                        /* 354 */
    {
        PRINT_ASSERT("Error! GetDoorLockState door id %d\n", door_id);  /* 355 */
    }

    return door_ctrl[door_id].lock_id;
}


/* Registration records carry a door *data* label; the door label the rest of
 * this file uses is the doorID inside the record MapDoor.c parked. */
int GetDoorDataLabelToDoorLabel(int door_data_label)
{
    MLOAD_DOOR_DAT *dp = MapDoorGetDatListPtr(door_data_label);         /* 375 */

    return dp->doorID;                                                  /* 378 */
}


/* The whole lock table goes into the save block verbatim. */
void SetSave_DoorCtrl(MC_SAVE_DATA *data)
{                                                                       /* 389 */
    data->addr = (u_char *)&door_ctrl[0];
    data->size = sizeof(door_ctrl);                                     /* 393 */
}
