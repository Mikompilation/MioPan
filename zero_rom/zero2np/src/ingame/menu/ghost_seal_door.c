// FILE: /home/zero_rom/zero2np/src/ingame/menu/ghost_seal_door.c
//
// Ghost-sealed doors.  Nine state bytes and the five entry points that move
// them; the per-seal constants live in tim_dat/ghost_seal_door_dat.c.
// See ghost_seal_door.h for the state machine.
//
// Reconstructed from the Feb 6 2004 prototype (SLES_523.84), ghost_seal_door.o
// (.text 1af5e8, .bss 4af6d0).

#include "ghost_seal_door.h"

#include "../../common/utility2.h"                      /* PRINT_ASSERT */
#include "../../graphics/graph3d/ctl/fixed_array.h"     /* fixed_array  */

#include <string.h>                                     /* memset */


/* One byte per seal.  fixed_array, not u_char[9]: the ROM inlines
 * _fixed_array_verifyrange<unsigned char>(i, 9) ahead of every subscript
 * below, including the two that index with an already-asserted seal_label. */
/* bss 4af6d0 */
static fixed_array<u_char, GHOST_SEAL_DOOR_MAX_NUM> ghost_seal_door_state;


/* Wipes every seal back to NONE.  The ROM clears the block with one memset
 * rather than fixed_array::fill(), which is why a single verifyrange(0, 9)
 * precedes it -- that is the &state[0] in the argument, not a loop. */
void GhostSealDoorInit(void)
{                                                                       /* 49 */
    memset(&ghost_seal_door_state[0], 0, sizeof(ghost_seal_door_state));
}


/* Retires the seals whose end room the player has just walked into.  Only
 * RELEASE advances -- a seal still SET stays put even if its end room is
 * somehow reachable, so the script must break it first. */
void GhostSealDoorMain(int room_label)
{                                                                       /* 66 */
    int i;

    for (i = 0; i < GHOST_SEAL_DOOR_MAX_NUM; i++)                       /* 71 */
    {
        if (ghost_seal_door_state[i] == GHOST_SEAL_DOOR_RELEASE)
        {
            if (ghost_seal_door_data[i].end_room_label == room_label)   /* 75 */
            {
                ghost_seal_door_state[i] = GHOST_SEAL_DOOR_END;
            }
        }
    }                                                                   /* 79 */
}


/* SET_GHOST_SEAL_DOOR.  Seals a door that has never been sealed; a seal
 * already past NONE is left alone, so re-running the event is a no-op. */
void GhostSealDoorAppear(int seal_label)
{                                                                       /* 89 */
    /* The ROM emits one sltiu, i.e. the signed and unsigned halves of the
     * range test were folded.  Like the door.c asserts this does not return:
     * the subscript below runs anyway and fixed_array catches it a second
     * time.  PRINT_ASSERT also breaks into the debugger, which the ROM's
     * plain printf pair did not -- an out-of-range label is a script bug, so
     * stopping on it is the useful behaviour here. */
    if (seal_label < 0 || seal_label >= GHOST_SEAL_DOOR_MAX_NUM)        /* 93 */
    {
        PRINT_ASSERT("Error! %s ghost_seal_door_label [%d]",
                     __FUNCTION__, seal_label);                         /* 94 */
    }

    if (ghost_seal_door_state[seal_label] == GHOST_SEAL_DOOR_NONE)
    {
        ghost_seal_door_state[seal_label] = GHOST_SEAL_DOOR_SET;
    }
}


/* RELEASE_GHOST_SEAL_DOOR.  Breaks a seal that is currently SET.  The door
 * keeps its sealed art on the map until GhostSealDoorMain() sees the player
 * reach the far room. */
void GhostSealDoorRelease(int seal_label)
{                                                                       /* 112 */
    if (seal_label < 0 || seal_label >= GHOST_SEAL_DOOR_MAX_NUM)        /* 116 */
    {
        PRINT_ASSERT("Error! %s ghost_seal_door_label [%d]",
                     __FUNCTION__, seal_label);                         /* 117 */
    }

    if (ghost_seal_door_state[seal_label] == GHOST_SEAL_DOOR_SET)
    {
        ghost_seal_door_state[seal_label] = GHOST_SEAL_DOOR_RELEASE;
    }
}


/* Read back for menu_map.c.  No range assert of its own -- unlike the two
 * setters above, this one leans entirely on fixed_array's bound check. */
int GetGhostSealDoorState(int seal_label)
{                                                                       /* 138 */
    return ghost_seal_door_state[seal_label];
}


/* Hands the raw state block to the save system.  Nine bytes of plain u_char,
 * so unlike the pointer-bearing blocks elsewhere this one needs no widening
 * on the host and the ROM's literal 9 still describes it exactly. */
void SetSave_GhostSealDoor(MC_SAVE_DATA *data)
{                                                                       /* 153 */
    data->addr = &ghost_seal_door_state[0];
    data->size = sizeof(ghost_seal_door_state);                         /* 157 */
}
