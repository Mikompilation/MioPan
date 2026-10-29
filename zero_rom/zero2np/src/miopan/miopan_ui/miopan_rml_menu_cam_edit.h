#ifndef MIOPAN_RML_MENU_CAM_EDIT_H
#define MIOPAN_RML_MENU_CAM_EDIT_H

/*
 * The camera menu's upgrade editor, in RmlUi, over the game's own drawing of
 * it.  The rows, gauges, gems, lenses and their lettering are the game's art
 * and menu_cam_edit.c still draws them; this page puts up the title, the
 * window along the bottom with the line for what is under the cursor, the
 * Yes / No of its questions, and the hints, and takes the player's input.
 *
 * menu_cam_edit.c still decides everything: the screen hands it each command
 * as the one press of the game's own pad it stands for, and the editor's own
 * pad code walks its cursors, spends the gems and points, and asks.  The
 * places the pointer can go -- the rows, the slots -- are the editor's,
 * passed in with where its cursor is, and a row pointed at is walked to with
 * those same presses.  A frame that does not ask hides the screen.
 */

#define MIOPAN_RML_MENU_CAM_EDIT_HITS 8

/* What kind of thing the editor is showing. */
#define MIOPAN_RML_MENU_CAM_EDIT_PICK    0   /* rows or slots to choose from */
#define MIOPAN_RML_MENU_CAM_EDIT_CONFIRM 1   /* a question, Yes or No        */
#define MIOPAN_RML_MENU_CAM_EDIT_NOTICE  2   /* a refusal, any button        */

/* What the player did. */
#define MIOPAN_RML_MENU_CAM_EDIT_NONE   0
#define MIOPAN_RML_MENU_CAM_EDIT_UP     1
#define MIOPAN_RML_MENU_CAM_EDIT_DOWN   2
#define MIOPAN_RML_MENU_CAM_EDIT_LEFT   3
#define MIOPAN_RML_MENU_CAM_EDIT_RIGHT  4
#define MIOPAN_RML_MENU_CAM_EDIT_CHOOSE 5   /* CROSS                         */
#define MIOPAN_RML_MENU_CAM_EDIT_BACK   6   /* TRIANGLE                      */
#define MIOPAN_RML_MENU_CAM_EDIT_ROW    7   /* walk to the place in the arg  */
#define MIOPAN_RML_MENU_CAM_EDIT_ANSWER 8   /* the arg: 0 Yes, 1 No          */

typedef struct
{
    int kind;           /* MIOPAN_RML_MENU_CAM_EDIT_PICK..NOTICE            */
    int closing;        /* on its way out: no input                         */
    int busy;           /* a gem's award playing: the editor is frozen      */
    int line;           /* the window's line, message 0x31's, or -1         */
    /* The places the pointer can choose, in the game's pixels, which of
     * them the cursor is on, and whether they run across (LEFT / RIGHT walk
     * them) rather than down. */
    int hits;
    float hit[MIOPAN_RML_MENU_CAM_EDIT_HITS][4];
    int hit_at;
    int across;
    int answer;         /* a question's cursor: 0 Yes, 1 No                 */
} MIOPAN_RML_MENU_CAM_EDIT;

#ifdef __cplusplus
extern "C" {
#endif

/* Whether the screen can be put up: menu_cam_edit.c leaves its title, window,
 * line, Yes / No and hints to it only then. */
int MioPan_RmlMenuCamEditReady(void);
/* Put the screen up for this frame.  1 when it is up. */
int MioPan_RmlMenuCamEditShow(const MIOPAN_RML_MENU_CAM_EDIT *page);
/* Whether the screen was up last frame: menu_cam_edit.c reads its answers
 * before it draws. */
int MioPan_RmlMenuCamEditIsShown(void);
/* The command, and for ROW / ANSWER its arg. */
int MioPan_RmlMenuCamEditConsumeCommand(int *arg);

#ifdef __cplusplus
}

namespace Rml
{
class Context;
}

namespace MioPanRmlMenuCamEdit
{
bool Init(Rml::Context *context);
void Shutdown();
void Frame();
}
#endif

#endif
