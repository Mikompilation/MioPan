#ifndef MIOPAN_RML_MENU_CAMERA_H
#define MIOPAN_RML_MENU_CAMERA_H

/*
 * The camera menu's top page, in RmlUi, over the game's own drawing of it.
 * Everything on this page is the game's art -- the camera, the upgrade
 * ladder, the film plate, the function slots, their lettering and digits --
 * and menu_cam_top.c still draws it; this page puts up the title, the window
 * along the bottom with the line for what is under the cursor, and the hints,
 * and takes the player's input.
 *
 * menu_cam_top.c runs the page: each frame it puts the screen up with what it
 * holds, and it acts on the player's input with its own code -- the cursor,
 * the three selectors, loading a film, fitting a part, opening the editor,
 * leaving.  A frame that does not ask hides the screen.
 */

/* What the page is showing, as menu_cam_top.c's modes. */
#define MIOPAN_RML_MENU_CAMERA_ADD_FUNC   0   /* the addition functions      */
#define MIOPAN_RML_MENU_CAMERA_FILM       1   /* the film                    */
#define MIOPAN_RML_MENU_CAMERA_EQUIP_FUNC 2   /* the camera parts            */
#define MIOPAN_RML_MENU_CAMERA_TOP        3

/* What the player did. */
#define MIOPAN_RML_MENU_CAMERA_NONE   0
#define MIOPAN_RML_MENU_CAMERA_UP     1
#define MIOPAN_RML_MENU_CAMERA_DOWN   2
#define MIOPAN_RML_MENU_CAMERA_LEFT   3
#define MIOPAN_RML_MENU_CAMERA_RIGHT  4
#define MIOPAN_RML_MENU_CAMERA_CHOOSE 5
#define MIOPAN_RML_MENU_CAMERA_BACK   6
#define MIOPAN_RML_MENU_CAMERA_SET    7   /* the cursor to the item in the arg */

typedef struct
{
    int mode;           /* MIOPAN_RML_MENU_CAMERA_ADD_FUNC..TOP            */
    int closing;        /* on its way out: no input                        */
    int cursor;         /* the top page's: 0 the camera, 1..3 the rows;
                         * a selector's: its slot                          */
    int owned;          /* a selector's slots the player owns, as a mask   */
    int line;           /* the window's line, message 0x31's               */
    int fitted;         /* the camera parts' selector: the slot is fitted  */
} MIOPAN_RML_MENU_CAMERA;

#ifdef __cplusplus
extern "C" {
#endif

/* Put the screen up for this frame.  1 when it is up; 0 when RmlUi is
 * unavailable and menu_cam_top.c should draw and read its own. */
int MioPan_RmlMenuCameraShow(const MIOPAN_RML_MENU_CAMERA *page);
/* Whether the screen was up last frame: menu_cam_top.c reads its answers
 * before it draws. */
int MioPan_RmlMenuCameraIsShown(void);
/* The command, and for SET the item. */
int MioPan_RmlMenuCameraConsumeCommand(int *arg);

#ifdef __cplusplus
}

namespace Rml
{
class Context;
}

namespace MioPanRmlMenuCamera
{
bool Init(Rml::Context *context);
void Shutdown();
void Frame();
}
#endif

#endif
