/* ==========================================================================
 *  ingame/menu/tim_dat/map_size_dat.h
 *
 *  Declares the per-map sheet geometry defined in map_size_dat.c: the sheet
 *  origin, its extent, and the two zoom factors it is drawn at.  Indexed by
 *  map_label (0..16) with no terminator -- unlike the map_room_dat tables.
 *
 *  Reconstructed from the Feb 6 2004 prototype (SLES_523.84).
 * ======================================================================== */

#ifndef _INGAME_MENU_TIM_DAT_MAP_SIZE_DAT_H
#define _INGAME_MENU_TIM_DAT_MAP_SIZE_DAT_H

typedef struct                                          /* 0x8 */
{
    /* 0x0 */ float x;
    /* 0x4 */ float y;
} MAP_WORLD_POINT;

typedef struct                                          /* 0x8 */
{
    /* 0x0 */ int w;
    /* 0x4 */ int h;
} MAP_SIZE_DAT;

typedef struct                                          /* 0x8 */
{
    /* 0x0 */ float normal;
    /* 0x4 */ float big;
} MAP_SCALL_DAT;

extern MAP_WORLD_POINT map_world_point[17];             /* data 31e210 */
extern MAP_SIZE_DAT    map_size_dat[17];                /* data 31e298 */
extern MAP_SCALL_DAT   map_scall_dat[17];               /* data 31e320 */

#endif /* _INGAME_MENU_TIM_DAT_MAP_SIZE_DAT_H */
