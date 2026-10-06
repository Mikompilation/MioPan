/* ==========================================================================
 *  ingame/menu/tim_dat/map_area_dat.h
 *
 *  Declares MAP_AREA_DAT and the room floor-plan table defined in
 *  map_area_dat.c.  Each record is one room's quad on the in-game map sheet,
 *  in map units; RoomInfoRoomHitCheck() (plyr_room_info.c) is the only reader.
 *
 *  Reconstructed from the Feb 6 2004 prototype (SLES_523.84).
 * ======================================================================== */

#ifndef _INGAME_MENU_TIM_DAT_MAP_AREA_DAT_H
#define _INGAME_MENU_TIM_DAT_MAP_AREA_DAT_H

/* map_label / area_label are single bytes and room_label a short in the ROM;
 * keeping those widths matters because area_label and map_label are compared
 * against -1 terminators and room_label is sign-extended on the way out. */
typedef struct                                          /* 0x24 */
{
    /* 0x00 */ char  map_label;
    /* 0x01 */ char  area_label;    /* -1 = applies on every area          */
    /* 0x02 */ short room_label;
    /* 0x04 */ float pos[4][2];     /* quad corners, map units             */
} MAP_AREA_DAT;

extern MAP_AREA_DAT map_area_dat[260];                  /* data 319e28 */

#endif /* _INGAME_MENU_TIM_DAT_MAP_AREA_DAT_H */
