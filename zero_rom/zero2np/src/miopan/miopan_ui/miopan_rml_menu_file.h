#ifndef MIOPAN_RML_MENU_FILE_H
#define MIOPAN_RML_MENU_FILE_H

/*
 * The in-game menu's File page, in RmlUi, composed as the game composes it:
 * the five tabs over the list of what is held of each kind, the picture of
 * the one under the cursor in its round window, and the three viewers a file
 * opens into -- the paged reader for the notebooks, scraps and old books, and
 * the photograph and map viewers with their neighbours either side.  The art
 * is the game's own; the words are RmlUi's, in the game's own line breaks.
 *
 * Stands in for what menu_file.c draws.  menu_file.c still runs the page:
 * each frame it puts the screen up with the list or the viewer as it holds
 * it, and it acts on the player's input with its own code -- the cursor, the
 * tabs and the pictures' cross-fade, opening and reading a file, turning the
 * pages, leaving.  A frame that does not ask hides the screen.
 */

/* Rows the list shows at once, as MENU_FILE_DISP_NUM, and the tabs. */
#define MIOPAN_RML_MENU_FILE_ROWS 6
#define MIOPAN_RML_MENU_FILE_TABS 5

/* What the page is showing. */
#define MIOPAN_RML_MENU_FILE_LIST     0
#define MIOPAN_RML_MENU_FILE_DOCUMENT 1   /* a notebook, a scrap, an old book */
#define MIOPAN_RML_MENU_FILE_PHOTO    2
#define MIOPAN_RML_MENU_FILE_MAP      3
#define MIOPAN_RML_MENU_FILE_EMPTY    4

/* What the player did. */
#define MIOPAN_RML_MENU_FILE_NONE     0
#define MIOPAN_RML_MENU_FILE_UP       1
#define MIOPAN_RML_MENU_FILE_DOWN     2
#define MIOPAN_RML_MENU_FILE_ROW      3   /* the cursor to the row in the arg  */
#define MIOPAN_RML_MENU_FILE_TAB_PREV 4
#define MIOPAN_RML_MENU_FILE_TAB_NEXT 5
#define MIOPAN_RML_MENU_FILE_TAB      6   /* the tab in the arg                */
#define MIOPAN_RML_MENU_FILE_CHOOSE   7   /* open the file under the cursor    */
#define MIOPAN_RML_MENU_FILE_BACK     8
#define MIOPAN_RML_MENU_FILE_PREV     9   /* a viewer's page or file back     */
#define MIOPAN_RML_MENU_FILE_NEXT     10  /* and on                           */

typedef struct
{
    int mode;           /* MIOPAN_RML_MENU_FILE_LIST..EMPTY                 */
    int closing;        /* on its way back to the hub: no input             */
    int busy;           /* changing between the list and a viewer           */
    int mode_alpha;     /* that change's fade, 0..0x80                      */
    int tab;            /* the tab, which is the file type                  */
    int tab_count[MIOPAN_RML_MENU_FILE_TABS];  /* what is held of each     */
    int cursor;         /* the row under the cursor                         */
    int start;          /* the list's first row shown                       */
    int count;          /* how many of this kind are held                   */
    /* The rows shown: the file, and whether it is still unread. */
    int file[MIOPAN_RML_MENU_FILE_ROWS];
    int unread[MIOPAN_RML_MENU_FILE_ROWS];
    int bank;           /* the tab's message bank                           */
    int selected;       /* the file under the cursor                        */
    int caption;        /* whether its line under the list is shown         */
    int page;           /* the reader's page, from 0, and how many          */
    int pages;
    int prev_file;      /* a viewer's neighbours either side, -1 for none   */
    int next_file;
    /* The picture's cross-fade, the outgoing one first, each with its alpha
     * (0..0x80). */
    const void *picture[2];
    int picture_alpha[2];
    /* The page's paks: the common one, the list's, the open viewer's and its
     * thumbnails'. */
    const void *pak_cmn;
    const void *pak_top;
    const void *pak_view;
    const void *pak_small;
} MIOPAN_RML_MENU_FILE;

#ifdef __cplusplus
extern "C" {
#endif

/* Whether the screen can be put up: menu_file.c runs the mode's fade for it
 * only then, rather than drawing its own. */
int MioPan_RmlMenuFileReady(void);
/* Put the screen up for this frame.  1 when it is up; 0 when RmlUi is
 * unavailable and menu_file.c should draw and read its own. */
int MioPan_RmlMenuFileShow(const MIOPAN_RML_MENU_FILE *page);
/* Whether the screen was up last frame: menu_file.c reads its answers before
 * it draws. */
int MioPan_RmlMenuFileIsShown(void);
/* The command, and for a row or a tab its number. */
int MioPan_RmlMenuFileConsumeCommand(int *arg);

#ifdef __cplusplus
}

namespace Rml
{
class Context;
}

namespace MioPanRmlMenuFile
{
bool Init(Rml::Context *context);
void Shutdown();
void Frame();
}
#endif

#endif
