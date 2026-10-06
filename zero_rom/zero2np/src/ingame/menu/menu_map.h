/* ==========================================================================
 *  ingame/menu/menu_map.h
 *
 *  The in-game menu's map page (menu_map.o).  Only six of its forty-six
 *  bodies are exported; everything else -- the paging, the scrolling, the
 *  door and room drawing -- is file-local, which is why this header is so
 *  much smaller than the module.
 *
 *  Reconstructed from the Feb 6 2004 prototype (SLES_523.84).
 * ======================================================================== */

#ifndef _INGAME_MENU_MENU_MAP_H
#define _INGAME_MENU_MENU_MAP_H

#include "eetypes.h"
#include "../../graphics/graph3d/ctl/fixed_array.h"

/* --------------------------------------------------------------------------
 *  Work blocks
 *
 *  Both are file-static in menu_map.o; they are described here because the
 *  page's whole state is these two structs and nothing else.
 * ------------------------------------------------------------------------ */

/* One outstanding texture load.  step is 0 "not started", 1 "in flight",
 * 2 "resident"; tex_label is the CD file, -1 when there is nothing to load. */
typedef struct                      /* 0x8 */
{
    /* 0x0 */ char step;
    /* 0x4 */ int  tex_label;
} MENU_MAP_LOAD_CTRL;

/* What the page is looking at. */
typedef struct                      /* 0x14 */
{
    /* 0x00 */ char plyr_map;       /* the sheet the player is actually on   */
    /* 0x01 */ char cross_fade_flg; /* which menu_cmn.o cross-fade slot      */
    /* 0x04 */ MENU_MAP_LOAD_CTRL snap_load;    /* the room snapshot         */
    /* 0x0c */ int  hit_room;       /* room under the crosshair, -1 for none */
    /* 0x10 */ int  map_area_id;
} MENU_MAP_CTRL;

/* How it is being drawn. */
typedef struct                      /* 0x18 */
{
    /* 0x00 */ char anim_step;      /* the shared MenuInOutAnimCtrl ladder   */
    /* 0x01 */ char anim_timer;
    /* 0x02 */ fixed_array<char, 4> tri_pad_flg;    /* up/right/left/down    */
    /* 0x06 */ char   tri_anim_step;    /* the scroll arrows' own animation  */
    /* 0x07 */ u_char tri_alpha;
    /* 0x08 */ char   tri_timer;
    /* 0x0c */ float  map_off_x;    /* scroll, in screen pixels              */
    /* 0x10 */ float  map_off_y;
    /* 0x14 */ u_char map_scall_flg;    /* 0 = 1x, 1 = 2x                    */
    /* 0x15 */ char   disp_map_label;   /* the sheet on screen               */
    /* 0x16 */ char   before_disp_map;  /* the one it is fading out of       */
} MENU_MAP_DISP;

/* --------------------------------------------------------------------------
 *  Exports
 * ------------------------------------------------------------------------ */

/* menu_ctrl[] row 0: one frame of the page, and its drawing.  MenuMap() is
 * one of only two callers of MenuOutReq() in the whole build. */
void MenuMap(void);                     /* 0x1fb8a8 */
void MenuMapDisp(void);                 /* 0x1fcd80 */

/* Drop the page's three texture buffers.  MenuRelease() calls it, and so
 * does MenuMap()'s own close step. */
void MenuMapRelease(void);              /* 0x1fcb88 */

/* Non-zero when `target` lies inside the quad tri0-tri1-tri2-tri3, tested in
 * the floor plane.  The quad is split into the two triangles 0-1-2 and 2-3-0,
 * which is why a concave quad would test wrong -- the map data is all convex.
 * (menu_map.o 0x001fc940) */
int MenuMapHitCheck(float *target, float *tri0, float *tri1,
                    float *tri2, float *tri3);

/* Map sheet holding the given area on the given floor, or -1 (with a printf)
 * when the pair is not in area_map_tbl[].  (menu_map.o 0x001fcaf0) */
int GetMapLabelFromAreaLabel(int area_label, int floor_label);

/* The sheet the player is standing on, via the two accessors above. */
int GetPlyrMapLabel(void);              /* 0x1fcab8 */

#endif /* _INGAME_MENU_MENU_MAP_H */
