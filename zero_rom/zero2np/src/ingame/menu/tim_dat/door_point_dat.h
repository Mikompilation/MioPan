/* ==========================================================================
 *  ingame/menu/tim_dat/door_point_dat.h
 *
 *  Where each map sheet's doors sit (door_point_dat.o).  One MAP_DOOR_POINT
 *  array per sheet, reached through menu_map_door_data[map_label].
 *
 *  Reconstructed from the Feb 6 2004 prototype (SLES_523.84).
 * ======================================================================== */

#ifndef _INGAME_MENU_TIM_DAT_DOOR_POINT_DAT_H
#define _INGAME_MENU_TIM_DAT_DOOR_POINT_DAT_H

typedef struct                                          /* 0x18 */
{
    /* 0x00 */ float x;                 /* position on the sheet, map units  */
    /* 0x04 */ float y;
    /* 0x08 */ int ghost_seal_door_label;   /* -1 = an ordinary door         */
    /* 0x0c */ int room_label1;         /* either side being seen draws it   */
    /* 0x10 */ int room_label2;
    /* 0x14 */ int door_type_label;     /* 0..4 single, 5..8 double, 9 none  */
} MAP_DOOR_POINT;

/* Indexed by map_label; entry 17 is the NULL terminator, and a sheet with no
 * doors would be NULL here too -- MenuMapDoorDisp() tests before walking.
 * The pointers are writable but everything they point at is const. */
extern const MAP_DOOR_POINT *menu_map_door_data[18];    /* data 2db398 */

#endif /* _INGAME_MENU_TIM_DAT_DOOR_POINT_DAT_H */
