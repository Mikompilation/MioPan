/* ==========================================================================
 *  ingame/menu/tim_dat/map_room_dat.h
 *
 *  Declares the room-registry record types and the four tables defined in
 *  map_room_dat.c.  Every one of them is walked to an all -1 terminator
 *  rather than by a count, so the array bounds below are documentation only.
 *
 *  Reconstructed from the Feb 6 2004 prototype (SLES_523.84).
 * ======================================================================== */

#ifndef _INGAME_MENU_TIM_DAT_MAP_ROOM_DAT_H
#define _INGAME_MENU_TIM_DAT_MAP_ROOM_DAT_H

typedef struct                                          /* 0x8 */
{
    /* 0x0 */ int map_label;
    /* 0x4 */ int map_tex_id;
} MAP_INFO_DAT;

typedef struct                                          /* 0x14 */
{
    /* 0x00 */ int map_label;
    /* 0x04 */ int room_label;
    /* 0x08 */ int room_group_label;
    /* 0x0c */ int snap_tex_label;
    /* 0x10 */ int room_tex_label;
} ROOM_INFO_DAT;

typedef struct                                          /* 0x10 */
{
    /* 0x0 */ int room_label;       /* the room that stands for the house  */
    /* 0x4 */ int house_tex_label;
    /* 0x8 */ int start_room_label; /* inclusive range that marks it seen  */
    /* 0xc */ int end_room_label;
} HOUSE_INFO_DAT;

typedef struct                                          /* 0xc */
{
    /* 0x0 */ int area_label;
    /* 0x4 */ int floor_label;
    /* 0x8 */ int map_label;
} AREA_MAP_TBL;

extern MAP_INFO_DAT   map_info_dat[17];                 /* data 31c860 */
extern ROOM_INFO_DAT  room_info_dat[265];               /* data 31c8e8 */
extern HOUSE_INFO_DAT house_info_dat[11];               /* data 31dda0 */
extern AREA_MAP_TBL   area_map_tbl[80];                 /* data 31de50 */

#endif /* _INGAME_MENU_TIM_DAT_MAP_ROOM_DAT_H */
