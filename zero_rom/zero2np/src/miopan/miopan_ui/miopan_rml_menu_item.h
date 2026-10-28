#ifndef MIOPAN_RML_MENU_ITEM_H
#define MIOPAN_RML_MENU_ITEM_H

/*
 * The in-game menu's Item page, in RmlUi, composed as the game composes it:
 * the list of what Mio carries on its row bars with the scrollbar beside it,
 * the status plate, the item's picture cross-fading in its round frame under
 * the magnifier glass, its description on the striped window below, and the
 * game's "use this?" and "can't use" windows.  The art is the game's own;
 * the words and numbers are RmlUi's.
 *
 * Stands in for what menu_item.c draws.  menu_item.c still runs the page:
 * each frame it puts the screen up with the list as it holds it, and it acts
 * on the player's input with its own code -- the cursor and the scrolling,
 * the cross-fade, which items can be used and using them, leaving.  A frame
 * that does not ask hides the screen.
 */

/* Rows the list shows at once, as MENU_ITEM_DISP_NUM. */
#define MIOPAN_RML_MENU_ITEM_ROWS 7

/* What the page is showing. */
#define MIOPAN_RML_MENU_ITEM_LIST    0   /* the list                          */
#define MIOPAN_RML_MENU_ITEM_NO_USE  1   /* "can't use that"                   */
#define MIOPAN_RML_MENU_ITEM_USE_SEL 2   /* "use this?" -- yes or no          */
#define MIOPAN_RML_MENU_ITEM_EMPTY   3   /* nothing carried                    */

/* What the player did. */
#define MIOPAN_RML_MENU_ITEM_NONE      0
#define MIOPAN_RML_MENU_ITEM_UP        1
#define MIOPAN_RML_MENU_ITEM_DOWN      2
#define MIOPAN_RML_MENU_ITEM_PAGE_UP   3
#define MIOPAN_RML_MENU_ITEM_PAGE_DOWN 4
#define MIOPAN_RML_MENU_ITEM_ROW       5   /* the cursor to the row in the arg  */
#define MIOPAN_RML_MENU_ITEM_CHOOSE    6   /* CROSS on the row                  */
#define MIOPAN_RML_MENU_ITEM_BACK      7   /* TRIANGLE                          */
#define MIOPAN_RML_MENU_ITEM_YES       8   /* the window answered               */
#define MIOPAN_RML_MENU_ITEM_NO        9
#define MIOPAN_RML_MENU_ITEM_ACK       10  /* the message dismissed             */

typedef struct
{
    int mode;           /* MIOPAN_RML_MENU_ITEM_LIST..EMPTY                 */
    int closing;        /* on its way back to the hub: no input             */
    int cursor;         /* the row under the cursor, 0..6                   */
    int start;          /* the list's first row shown                       */
    int count;          /* how many different items there are               */
    int item[MIOPAN_RML_MENU_ITEM_ROWS];   /* the rows shown, item ids      */
    int have[MIOPAN_RML_MENU_ITEM_ROWS];   /* and how many of each          */
    int selected;       /* the item under the cursor                        */
    const void *pak;    /* the page's own pak, menu_item.c's                */
    /* The cross-fade's two pictures, in drawing order -- the one going out
     * first -- each a TIM2 (or null) and its alpha, 0..0x80. */
    const void *picture[2];
    int picture_alpha[2];
} MIOPAN_RML_MENU_ITEM;

#ifdef __cplusplus
extern "C" {
#endif

/* Put the screen up for this frame.  1 when it is up; 0 when RmlUi is
 * unavailable and menu_item.c should draw and read its own. */
int MioPan_RmlMenuItemShow(const MIOPAN_RML_MENU_ITEM *page);
/* Whether the screen was up last frame: menu_item.c reads its answers before
 * it draws. */
int MioPan_RmlMenuItemIsShown(void);
/* The command, and for a row its number. */
int MioPan_RmlMenuItemConsumeCommand(int *arg);

#ifdef __cplusplus
}

namespace Rml
{
class Context;
}

namespace MioPanRmlMenuItem
{
bool Init(Rml::Context *context);
void Shutdown();
void Frame();
}
#endif

#endif
