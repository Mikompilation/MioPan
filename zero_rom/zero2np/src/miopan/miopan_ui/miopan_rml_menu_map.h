#ifndef MIOPAN_RML_MENU_MAP_H
#define MIOPAN_RML_MENU_MAP_H

/*
 * The in-game menu's Map page, in RmlUi, composed as the game composes it --
 * around the map itself.  The sheet of rooms, doors and markers scrolling
 * under the crosshair stays the game's own drawing, clipped to its viewport
 * as menu_map.c clips it; this page lays over it the frame with its compass
 * points, the crosshair's arrows, the snapshot of the room under the
 * crosshair, and the area's and the room's names.  The art is the game's
 * own; the words are RmlUi's.
 *
 * menu_map.c still runs the page: each frame it puts the screen up with what
 * it holds, and it moves the map with its own code -- scrolling while a way is
 * held, dragging, zooming, changing floor and area, leaving.  A frame that
 * does not ask hides the screen.
 */

/* The ways the map can be held moving, as a mask. */
#define MIOPAN_RML_MENU_MAP_HELD_UP    0x1
#define MIOPAN_RML_MENU_MAP_HELD_DOWN  0x2
#define MIOPAN_RML_MENU_MAP_HELD_LEFT  0x4
#define MIOPAN_RML_MENU_MAP_HELD_RIGHT 0x8

/* What the player did, besides holding a way or dragging. */
#define MIOPAN_RML_MENU_MAP_NONE       0
#define MIOPAN_RML_MENU_MAP_ZOOM       1   /* CROSS: enlarge or reduce       */
#define MIOPAN_RML_MENU_MAP_FLOOR_PREV 2   /* L1 / R1                        */
#define MIOPAN_RML_MENU_MAP_FLOOR_NEXT 3
#define MIOPAN_RML_MENU_MAP_AREA_PREV  4   /* L2 / R2                        */
#define MIOPAN_RML_MENU_MAP_AREA_NEXT  5
#define MIOPAN_RML_MENU_MAP_BACK       6

typedef struct
{
    int closing;        /* on its way out: no input                         */
    int has_map;        /* the player is somewhere the map has a sheet for  */
    int area;           /* the sheet's name, message 0x3d's, or -1          */
    int room;           /* the room under the crosshair, 0x4a's, or -1      */
    int zoomed;         /* at 2x                                            */
    int arrows_alpha;   /* the crosshair's arrows' fade, 0..0x80            */
    const void *snap;   /* the room's snapshot's pak, once it is shown      */
    const void *pak;    /* the page's own pak, menu_map.c's                 */
} MIOPAN_RML_MENU_MAP;

#ifdef __cplusplus
extern "C" {
#endif

/* Whether the screen can be put up: menu_map.c leaves its frame, its title
 * and its readouts to it only then. */
int MioPan_RmlMenuMapReady(void);
/* Put the screen up for this frame.  1 when it is up; 0 when RmlUi is
 * unavailable and menu_map.c should draw and read its own. */
int MioPan_RmlMenuMapShow(const MIOPAN_RML_MENU_MAP *page);
/* Whether the screen was up last frame: menu_map.c reads its answers before
 * it draws. */
int MioPan_RmlMenuMapIsShown(void);
/* The ways held down now, MIOPAN_RML_MENU_MAP_HELD_*. */
int MioPan_RmlMenuMapHeld(void);
/* How far the pointer has dragged the map since last asked, in the game's
 * pixels; 0 when it is not dragging. */
int MioPan_RmlMenuMapConsumeDrag(float *dx, float *dy);
/* The command, MIOPAN_RML_MENU_MAP_*. */
int MioPan_RmlMenuMapConsumeCommand(void);

#ifdef __cplusplus
}

namespace Rml
{
class Context;
}

namespace MioPanRmlMenuMap
{
bool Init(Rml::Context *context);
void Shutdown();
void Frame();
}
#endif

#endif
