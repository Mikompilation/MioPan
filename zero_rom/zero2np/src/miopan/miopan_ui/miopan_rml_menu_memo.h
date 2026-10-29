#ifndef MIOPAN_RML_MENU_MEMO_H
#define MIOPAN_RML_MENU_MEMO_H

/*
 * The in-game menu's Memo page, in RmlUi, composed as the game composes it:
 * the notebook on the desk with every memo's title handwritten on its pages
 * -- the one under the cursor in red ink -- the list on the row frames with
 * the unread ones bracketed, the caption window, and the reader that opens
 * over it, a page at a time.  The art is the game's own; the words are
 * RmlUi's, in the game's own line breaks.
 *
 * Stands in for what menu_memo.c draws.  menu_memo.c still runs the page:
 * each frame it puts the screen up with the list as it holds it, and it acts
 * on the player's input with its own code -- the cursor, marking a memo read,
 * turning the reader's pages, leaving.  A frame that does not ask hides the
 * screen.
 */

/* Rows the list shows at once, as MENU_MEMO_DISP_NUM, and every memo. */
#define MIOPAN_RML_MENU_MEMO_ROWS 8
#define MIOPAN_RML_MENU_MEMO_MAX  20

/* What the page is showing. */
#define MIOPAN_RML_MENU_MEMO_LIST  0
#define MIOPAN_RML_MENU_MEMO_READ  1
#define MIOPAN_RML_MENU_MEMO_EMPTY 2

/* What the player did. */
#define MIOPAN_RML_MENU_MEMO_NONE      0
#define MIOPAN_RML_MENU_MEMO_UP        1
#define MIOPAN_RML_MENU_MEMO_DOWN      2
#define MIOPAN_RML_MENU_MEMO_PAGE_UP   3
#define MIOPAN_RML_MENU_MEMO_PAGE_DOWN 4
#define MIOPAN_RML_MENU_MEMO_ROW       5   /* the cursor to the row in the arg */
#define MIOPAN_RML_MENU_MEMO_CHOOSE    6   /* open the reader                  */
#define MIOPAN_RML_MENU_MEMO_BACK      7
#define MIOPAN_RML_MENU_MEMO_NEXT      8   /* the reader's next page           */
#define MIOPAN_RML_MENU_MEMO_PREV      9

typedef struct
{
    int mode;           /* MIOPAN_RML_MENU_MEMO_LIST..EMPTY                */
    int closing;        /* on its way back to the hub: no input            */
    int busy;           /* changing between the list and the reader        */
    int mode_alpha;     /* that change's fade, 0..0x80                     */
    int cursor;         /* the row under the cursor                        */
    int start;          /* the list's first row shown                      */
    int count;          /* how many memos are held                         */
    /* Every memo held, in the list's order: its label, which revision of it
     * the player has, whether it is unread, and its title as the notebook
     * carries it -- up to three tiles of menu_memo_tex[], -1 after the last,
     * and the ink they are brushed in, 0xRRGGBB in the game's 0..0x80. */
    int label[MIOPAN_RML_MENU_MEMO_MAX];
    int step[MIOPAN_RML_MENU_MEMO_MAX];
    int unread[MIOPAN_RML_MENU_MEMO_MAX];
    int ink[MIOPAN_RML_MENU_MEMO_MAX][3];
    int ink_rgb[MIOPAN_RML_MENU_MEMO_MAX];
    int page;           /* the reader's page, from 0, and how many         */
    int pages;
    const void *pak;    /* the page's own pak, menu_memo.c's               */
} MIOPAN_RML_MENU_MEMO;

#ifdef __cplusplus
extern "C" {
#endif

/* Whether the screen can be put up: menu_memo.c runs the mode's fade for it
 * only then, rather than drawing its own. */
int MioPan_RmlMenuMemoReady(void);
/* Put the screen up for this frame.  1 when it is up; 0 when RmlUi is
 * unavailable and menu_memo.c should draw and read its own. */
int MioPan_RmlMenuMemoShow(const MIOPAN_RML_MENU_MEMO *page);
/* Whether the screen was up last frame: menu_memo.c reads its answers before
 * it draws. */
int MioPan_RmlMenuMemoIsShown(void);
/* The command, and for a row its number. */
int MioPan_RmlMenuMemoConsumeCommand(int *arg);

#ifdef __cplusplus
}

namespace Rml
{
class Context;
}

namespace MioPanRmlMenuMemo
{
bool Init(Rml::Context *context);
void Shutdown();
void Frame();
}
#endif

#endif
