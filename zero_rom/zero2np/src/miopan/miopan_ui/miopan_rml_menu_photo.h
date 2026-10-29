#ifndef MIOPAN_RML_MENU_PHOTO_H
#define MIOPAN_RML_MENU_PHOTO_H

/*
 * The in-game menu's Photo page, in RmlUi, composed as the game composes it:
 * the album's sixteen cells down the left with the player's own photographs
 * in them, the one under the cursor enlarged in its frame, what it caught and
 * when on the data window, and the pop-ups over it all -- the shot's menu,
 * the sort menu, and the windows that ask before a photograph is deleted.
 * The art is the game's own; the words are RmlUi's.
 *
 * Stands in for what menu_photo.c draws.  menu_photo.c still runs the page:
 * each frame it puts the screen up with the album as it holds it, and it
 * acts on the player's input with its own code -- the cursor and the
 * enlarged picture's decoding, protecting, deleting and sorting, leaving.  A
 * frame that does not ask hides the screen.
 */

/* The album's slots and the rows of the two pop-ups. */
#define MIOPAN_RML_MENU_PHOTO_SLOTS 16
#define MIOPAN_RML_MENU_PHOTO_ROWS  3
#define MIOPAN_RML_MENU_PHOTO_SUBJECTS 3

/* What the page is showing. */
#define MIOPAN_RML_MENU_PHOTO_GRID      0
#define MIOPAN_RML_MENU_PHOTO_MENU      1   /* the shot's menu               */
#define MIOPAN_RML_MENU_PHOTO_SORT      2
#define MIOPAN_RML_MENU_PHOTO_DELETE    3   /* "delete this photo?"          */
#define MIOPAN_RML_MENU_PHOTO_PROTECTED 4   /* "it is protected"             */
#define MIOPAN_RML_MENU_PHOTO_EMPTY     5

/* What the player did. */
#define MIOPAN_RML_MENU_PHOTO_NONE   0
#define MIOPAN_RML_MENU_PHOTO_UP     1
#define MIOPAN_RML_MENU_PHOTO_DOWN   2
#define MIOPAN_RML_MENU_PHOTO_SIDE   3   /* the grid's other column           */
#define MIOPAN_RML_MENU_PHOTO_CELL   4   /* the grid to the slot in the arg   */
#define MIOPAN_RML_MENU_PHOTO_ROW    5   /* a pop-up to the row in the arg    */
#define MIOPAN_RML_MENU_PHOTO_CHOOSE 6   /* the grid's menu; a pop-up's row   */
#define MIOPAN_RML_MENU_PHOTO_BACK   7
#define MIOPAN_RML_MENU_PHOTO_YES    8
#define MIOPAN_RML_MENU_PHOTO_NO     9
#define MIOPAN_RML_MENU_PHOTO_ACK    10  /* the "protected" window dismissed  */

typedef struct
{
    int mode;           /* MIOPAN_RML_MENU_PHOTO_GRID..EMPTY                */
    int closing;        /* on its way back to the hub: no input             */
    int selected;       /* the slot under the cursor                        */
    /* Each slot's state -- 1 a photograph is there, 2 it is protected --
     * and where its thumbnail's texels are, 0 for an empty slot. */
    int state[MIOPAN_RML_MENU_PHOTO_SLOTS];
    uintptr_t thumb[MIOPAN_RML_MENU_PHOTO_SLOTS];
    uintptr_t large;    /* the enlarged picture's texels, once decoded      */
    /* What the selected photograph knows of itself: its room (message
     * 0x4a's, or -1), when it was taken, its score and what it caught --
     * up to three (message type, id) pairs. */
    int room;
    int day, month, year, hour, minute, second;
    int score;
    int subjects;
    int subject_type[MIOPAN_RML_MENU_PHOTO_SUBJECTS];
    int subject_id[MIOPAN_RML_MENU_PHOTO_SUBJECTS];
    /* The pop-ups: the menu's row and its opening scales and fade, the sort
     * menu's row. */
    int menu_row;
    float menu_scale;
    float menu_dim_scale;
    int menu_alpha;
    int sort_row;
    const void *pak;    /* the page's own pak, menu_photo.c's              */
} MIOPAN_RML_MENU_PHOTO;

#ifdef __cplusplus
extern "C" {
#endif

/* Whether the screen can be put up: menu_photo.c runs the shot's menu's
 * fade for it only then, rather than drawing its own. */
int MioPan_RmlMenuPhotoReady(void);
/* Put the screen up for this frame.  1 when it is up; 0 when RmlUi is
 * unavailable and menu_photo.c should draw and read its own. */
int MioPan_RmlMenuPhotoShow(const MIOPAN_RML_MENU_PHOTO *page);
/* Whether the screen was up last frame: menu_photo.c reads its answers before
 * it draws. */
int MioPan_RmlMenuPhotoIsShown(void);
/* The command, and for a cell or a row its number. */
int MioPan_RmlMenuPhotoConsumeCommand(int *arg);

#ifdef __cplusplus
}

namespace Rml
{
class Context;
}

namespace MioPanRmlMenuPhoto
{
bool Init(Rml::Context *context);
void Shutdown();
void Frame();
}
#endif

#endif
