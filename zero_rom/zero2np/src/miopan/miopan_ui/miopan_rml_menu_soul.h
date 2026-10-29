#ifndef MIOPAN_RML_MENU_SOUL_H
#define MIOPAN_RML_MENU_SOUL_H

/*
 * The in-game menu's Spirit List page, in RmlUi, composed as the game
 * composes it: the lacquered frame with its completion rate and "Complete!"
 * stamp, the nine rows with their ranking badges and unread marks under the
 * cursor's amber frame, the spirit's best score and where it was met, and its
 * photograph cross-fading on the parchment with what is known of it.  The art
 * is the game's own; the words are RmlUi's, in the game's own line breaks.
 *
 * Stands in for what menu_soul.c draws.  menu_soul.c still runs the page:
 * each frame it puts the screen up with the list as it holds it, and it acts
 * on the player's input with its own code -- the cursor and its photographs'
 * cross-fade, reading an entry, the congratulation, leaving.  A frame that
 * does not ask hides the screen.
 */

/* Rows the list shows at once, as MENU_SOUL_DISP_NUM. */
#define MIOPAN_RML_MENU_SOUL_ROWS 9

/* What the page is showing. */
#define MIOPAN_RML_MENU_SOUL_LIST     0
#define MIOPAN_RML_MENU_SOUL_CONGRATS 1   /* the list completed, over it   */
#define MIOPAN_RML_MENU_SOUL_EMPTY    2

/* What the player did. */
#define MIOPAN_RML_MENU_SOUL_NONE      0
#define MIOPAN_RML_MENU_SOUL_UP        1
#define MIOPAN_RML_MENU_SOUL_DOWN      2
#define MIOPAN_RML_MENU_SOUL_PAGE_UP   3
#define MIOPAN_RML_MENU_SOUL_PAGE_DOWN 4
#define MIOPAN_RML_MENU_SOUL_ROW       5   /* the cursor to the row in the arg */
#define MIOPAN_RML_MENU_SOUL_CHOOSE    6   /* read the entry under the cursor  */
#define MIOPAN_RML_MENU_SOUL_BACK      7
#define MIOPAN_RML_MENU_SOUL_ACK       8   /* the congratulation dismissed     */

typedef struct
{
    int mode;           /* MIOPAN_RML_MENU_SOUL_LIST..EMPTY                 */
    int closing;        /* on its way back to the hub: no input             */
    int cursor;         /* the row under the cursor                         */
    int start;          /* the list's first row shown                       */
    int count;          /* how many entries are held                        */
    /* The rows shown: the spirit, whether it is one of the enhancing ones
     * (the other ink), whether it is marked unread, and its ranking badge --
     * 0..2 for 1st..3rd, -1 for none. */
    int label[MIOPAN_RML_MENU_SOUL_ROWS];
    int enhancing[MIOPAN_RML_MENU_SOUL_ROWS];
    int unread[MIOPAN_RML_MENU_SOUL_ROWS];
    int order[MIOPAN_RML_MENU_SOUL_ROWS];
    int selected;       /* the spirit under the cursor, its best score      */
    int score;
    int rate;           /* how much of the list is complete, in percent    */
    /* The cross-fade's two photographs, the one going out first, each with
     * its alpha (0..0x80) and the message it carries, -1 for none. */
    const void *picture[2];
    int picture_alpha[2];
    int picture_msg[2];
    const void *pak;    /* the page's own pak, menu_soul.c's               */
} MIOPAN_RML_MENU_SOUL;

#ifdef __cplusplus
extern "C" {
#endif

/* Put the screen up for this frame.  1 when it is up; 0 when RmlUi is
 * unavailable and menu_soul.c should draw and read its own. */
int MioPan_RmlMenuSoulShow(const MIOPAN_RML_MENU_SOUL *page);
/* Whether the screen was up last frame: menu_soul.c reads its answers before
 * it draws. */
int MioPan_RmlMenuSoulIsShown(void);
/* The command, and for a row its number. */
int MioPan_RmlMenuSoulConsumeCommand(int *arg);

#ifdef __cplusplus
}

namespace Rml
{
class Context;
}

namespace MioPanRmlMenuSoul
{
bool Init(Rml::Context *context);
void Shutdown();
void Frame();
}
#endif

#endif
