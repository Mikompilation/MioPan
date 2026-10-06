/* ==========================================================================
 *  ingame/menu/tim_dat/save_point_dat.h
 *
 *  Where the save points sit on the map sheets (save_point_dat.o).
 *
 *  Reconstructed from the Feb 6 2004 prototype (SLES_523.84).
 * ======================================================================== */

#ifndef _INGAME_MENU_TIM_DAT_SAVE_POINT_DAT_H
#define _INGAME_MENU_TIM_DAT_SAVE_POINT_DAT_H

typedef struct                                          /* 0x10 */
{
    /* 0x0 */ float x;                  /* position on the sheet, map units  */
    /* 0x4 */ float y;
    /* 0x8 */ int   map_label;          /* which sheet it belongs to         */
    /* 0xc */ int   room_label;         /* only drawn once this room is seen */
} MAP_SAVE_POINT;

/* Walked to the all -1 terminator, so the bound is documentation only. */
extern MAP_SAVE_POINT map_save_point[13];               /* data 33ec18 */

#endif /* _INGAME_MENU_TIM_DAT_SAVE_POINT_DAT_H */
