#ifndef MIOPAN_RML_MENU_RADIO_H
#define MIOPAN_RML_MENU_RADIO_H

/*
 * The in-game menu's Radio page, in RmlUi, composed as the game composes it:
 * the crystal radio on its bench, the spirit stones on their frames with the
 * unheard ones bracketed, the stone under the cursor glowing in the radio's
 * window -- brightening, with its halo, while its recording plays -- the
 * recording's subtitles, and the window with what is known of the stone.  The
 * art is the game's own; the words are RmlUi's, in the game's own line
 * breaks.
 *
 * Stands in for what menu_radio.c draws.  menu_radio.c still runs the page:
 * each frame it puts the screen up with the list as it holds it, and it acts
 * on the player's input with its own code -- the cursor and its stones'
 * cross-fade, playing and stopping a recording, leaving.  A frame that does
 * not ask hides the screen.
 */

/* Rows the list shows at once, as MENU_RADIO_DISP_NUM. */
#define MIOPAN_RML_MENU_RADIO_ROWS 6

/* What the page is showing. */
#define MIOPAN_RML_MENU_RADIO_LIST  0
#define MIOPAN_RML_MENU_RADIO_PLAY  1   /* a recording is playing          */
#define MIOPAN_RML_MENU_RADIO_EMPTY 2

/* What the player did. */
#define MIOPAN_RML_MENU_RADIO_NONE      0
#define MIOPAN_RML_MENU_RADIO_UP        1
#define MIOPAN_RML_MENU_RADIO_DOWN      2
#define MIOPAN_RML_MENU_RADIO_PAGE_UP   3
#define MIOPAN_RML_MENU_RADIO_PAGE_DOWN 4
#define MIOPAN_RML_MENU_RADIO_ROW       5   /* the cursor to the row in the arg */
#define MIOPAN_RML_MENU_RADIO_CHOOSE    6   /* play the stone under the cursor  */
#define MIOPAN_RML_MENU_RADIO_BACK      7
#define MIOPAN_RML_MENU_RADIO_STOP      8   /* stop the recording               */

typedef struct
{
    int mode;           /* MIOPAN_RML_MENU_RADIO_LIST..EMPTY                */
    int closing;        /* on its way back to the hub: no input             */
    int cursor;         /* the row under the cursor                         */
    int start;          /* the list's first row shown                       */
    int count;          /* how many stones are held                         */
    /* The rows shown: the stone, and whether it is still unheard. */
    int crystal[MIOPAN_RML_MENU_RADIO_ROWS];
    int unheard[MIOPAN_RML_MENU_RADIO_ROWS];
    int selected;       /* the stone under the cursor                       */
    /* The stone in the radio's window as menu_radio.c draws it this frame:
     * up to two of them, the outgoing one first, each out of its own pak at
     * its alpha (0..0x80), and the halo on the live one. */
    const void *stone[2];
    int stone_alpha[2];
    const void *flare;
    int flare_alpha;
    int subtitle;       /* the recording's line now, message 9's, or -1     */
    const void *pak;    /* the page's own pak, menu_radio.c's              */
} MIOPAN_RML_MENU_RADIO;

#ifdef __cplusplus
extern "C" {
#endif

/* Whether the screen can be put up: menu_radio.c works out the stone's
 * animation for it only then, rather than drawing its own. */
int MioPan_RmlMenuRadioReady(void);
/* Put the screen up for this frame.  1 when it is up; 0 when RmlUi is
 * unavailable and menu_radio.c should draw and read its own. */
int MioPan_RmlMenuRadioShow(const MIOPAN_RML_MENU_RADIO *page);
/* Whether the screen was up last frame: menu_radio.c reads its answers before
 * it draws. */
int MioPan_RmlMenuRadioIsShown(void);
/* The command, and for a row its number. */
int MioPan_RmlMenuRadioConsumeCommand(int *arg);

#ifdef __cplusplus
}

namespace Rml
{
class Context;
}

namespace MioPanRmlMenuRadio
{
bool Init(Rml::Context *context);
void Shutdown();
void Frame();
}
#endif

#endif
