// FILE: /home/zero_rom/zero2np/src/ingame/door/dat/door_lock_dat.c
//
// The lock table door.c indexes with a door's lock_id: which "it is locked"
// message to print, and which rattle to play while the message is up.
//
// Row 0 is the unlocked slot and is never reached -- DoorLockStateExe() only
// runs for doors EvDoorOpen() found a non-zero lock_id on.  The two sound IDs
// are the two door-rattle banks; msg_id 0 covers the several lock kinds that
// share the generic "it will not open" line.
//
// Transcribed from the Feb 6 2004 prototype (SLES_523.84), door_lock_dat.o
// .data 0x002db360 (0x38 bytes).

#include "../prg/door.h"

DOOR_LOCK_INFO door_lock_info[DOOR_LOCK_MAX_NUM] =      /* data 2db360 */
{
    /* msg_id, sound_id */
    {  0,    0 },       /* 0 -- unlocked          */
    {  0, 3147 },
    {  0, 3139 },
    {  2, 3139 },
    {  3, 3139 },
    {  4, 3139 },
    {  2, 3147 },
};
