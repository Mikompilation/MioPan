/* ==========================================================================
 *  ingame/door/prg/door.h
 *
 *  Door bookkeeping: one lock byte per door, the open/close handshake the
 *  event interpreter drives, and the "it will not budge" reaction.
 *
 *  Reconstructed from the Feb 6 2004 prototype (SLES_523.84), door.o
 *  0x00136098..0x001368b0.
 * ======================================================================== */

#ifndef _INGAME_DOOR_PRG_DOOR_H
#define _INGAME_DOOR_PRG_DOOR_H

#include "eetypes.h"
#include "../../../common/save_data.h"          /* MC_SAVE_DATA */

/* Doors are addressed by a global label.  door_ctrl (bss 423098) is 0xd0
 * bytes, one DOOR_CTRL each. */
#define DOOR_MAX_NUM        208

/* Lock kinds, indexing door_lock_info[] (door/dat/door_lock_dat.c).  0 is
 * "not locked", so only 1..6 ever reach DoorLockStateExe(). */
#define DOOR_LOCK_MAX_NUM   7

typedef struct                          /* 0x1 */
{
    /* 0x0 */ u_char lock_id;
} DOOR_CTRL;

/* One row per lock kind: which "it is locked" message to print and which
 * rattle to play while it is up. */
typedef struct                          /* 0x8 */
{
    /* 0x0 */ int msg_id;
    /* 0x4 */ int sound_id;
} DOOR_LOCK_INFO;

/* The row DoorLockStateExeInit() latched, held live across the frames
 * DoorLockStateExe() runs for. */
typedef struct                          /* 0x8 */
{
    /* 0x0 */ int msg_id;
    /* 0x4 */ int sound_id;
} DOOR_LOCK_STATE_CTRL;

extern DOOR_LOCK_INFO       door_lock_info[DOOR_LOCK_MAX_NUM];  /* data 2db360 */
extern DOOR_LOCK_STATE_CTRL lock_state_ctrl;                    /* sdata 3efb60 */

void       DoorCtrlInit(void);

/* Begins a transition: swings the room data over to the far side and latches
 * the door motion.  Returns -1 when the door cannot be used right now. */
int        DoorOpenInit(int door_id);

/* Per-frame half of the transition; returns non-zero once the door is open. */
int        DoorOpen(int door_id);
void       DoorClose(int door_id);

void       DoorLock(int door_id, u_char lock_id);
void       DoorUnlock(int door_id);

/* The locked-door reaction: DoorLockStateExeInit() picks the message/sound
 * pair, DoorLockStateExe() runs it and returns non-zero when done. */
void       DoorLockStateExeInit(int door_id);
int        DoorLockStateExe(int door_id);

DOOR_CTRL *GetDoorInfo(int door_id);
u_char     GetDoorLockState(int door_id);

int        GetDoorDataLabelToDoorLabel(int door_data_label);
void       SetSave_DoorCtrl(MC_SAVE_DATA *data);

#endif /* _INGAME_DOOR_PRG_DOOR_H */
