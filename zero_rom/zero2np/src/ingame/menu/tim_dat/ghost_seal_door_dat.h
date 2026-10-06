/* ==========================================================================
 *  ingame/menu/tim_dat/ghost_seal_door_dat.h
 *
 *  Declares the per-seal table defined in ghost_seal_door_dat.c: for each of
 *  the nine ghost-sealed doors, the room whose entry retires the seal and the
 *  map-space position its marker is drawn at.
 *
 *  Reconstructed from the Feb 6 2004 prototype (SLES_523.84).
 * ======================================================================== */

#ifndef _INGAME_MENU_TIM_DAT_GHOST_SEAL_DOOR_DAT_H
#define _INGAME_MENU_TIM_DAT_GHOST_SEAL_DOOR_DAT_H

/* Nine seals.  The same count sizes ghost_seal_door_state in
 * ingame/menu/ghost_seal_door.c and bounds every seal_label there. */
#define GHOST_SEAL_DOOR_MAX_NUM 9

typedef struct                                          /* 0xc */
{
    /* 0x0 */ int   end_room_label;
    /* 0x4 */ float pos_x;
    /* 0x8 */ float pos_y;
} GHOST_SEAL_DOOR_DATA;

extern GHOST_SEAL_DOOR_DATA ghost_seal_door_data[GHOST_SEAL_DOOR_MAX_NUM];  /* rodata 3b4288 */

#endif /* _INGAME_MENU_TIM_DAT_GHOST_SEAL_DOOR_DAT_H */
