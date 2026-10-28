#ifndef MIOPAN_RML_SETUP_H
#define MIOPAN_RML_SETUP_H

/*
 * The setup menu a cleared save opens, in RmlUi: Story, Mission or back to
 * the title on the left, and for the first two the settings beside them --
 * costume, Mio's and Mayu's accessories, the difficulty -- with Game Start or
 * Mission Select under them.  The Xbox costumes and accessory sets are among
 * the choices once they have been imported.  The game's own captions say what
 * each row is for.
 *
 * Stands in for what setup_menu.c draws over setup.c's background.
 * setup_menu.c still runs the menu: each frame it says where it is and what
 * is chosen, and it acts on the player's input with its own code -- which
 * costumes and difficulties are open, the cues, starting the game.  A frame
 * that does not ask hides the screen.
 */

/* Where the menu is. */
#define MIOPAN_RML_SETUP_MENU     0   /* the three rows on the left            */
#define MIOPAN_RML_SETUP_SETTINGS 1   /* the settings, for Story or Mission    */

/* The settings, as setup_menu.c numbers its setup_csr. */
#define MIOPAN_RML_SETUP_ROW_COSTUME    0
#define MIOPAN_RML_SETUP_ROW_MIO_ACS    1
#define MIOPAN_RML_SETUP_ROW_MAYU_ACS   2
#define MIOPAN_RML_SETUP_ROW_DIFFICULTY 3
#define MIOPAN_RML_SETUP_ROW_START      4

/* What the player did. */
#define MIOPAN_RML_SETUP_NONE   0
#define MIOPAN_RML_SETUP_ENTER  1   /* into the settings of the row chosen      */
#define MIOPAN_RML_SETUP_EXIT   2   /* back to the title, and said yes to it    */
#define MIOPAN_RML_SETUP_STEP   3   /* the setting under the cursor, by the arg */
#define MIOPAN_RML_SETUP_START  4   /* Game Start / Mission Select              */
#define MIOPAN_RML_SETUP_BACK   5   /* out of the settings                      */

typedef struct
{
    int mode;           /* MIOPAN_RML_SETUP_MENU / _SETTINGS                 */
    int menu_csr;       /* 0 Story, 1 Mission, 2 back to the title           */
    int setup_csr;      /* MIOPAN_RML_SETUP_ROW_*                            */
    int costume;        /* the ROM's nine, then the Xbox costumes            */
    int mio_acs;        /* 0 none, 1 the glasses, then the Xbox sets         */
    int mayu_acs;
    int difficulty;     /* 0 Easy .. 3 Nightmare                             */
    int has_difficulty; /* Story: the row is there                           */
    int difficulty_open;/* a save made at the clear: it can be changed       */
    int closing;        /* on its way to the game or the title: no input     */
} MIOPAN_RML_SETUP;

#ifdef __cplusplus
extern "C" {
#endif

/* Put the screen up for this frame.  The cursors are taken each time the mode
 * changes; after that the screen moves them and setup_menu.c reads them back.
 * 1 when it is up; 0 when RmlUi is unavailable and setup_menu.c should draw
 * and read its own. */
int MioPan_RmlSetupShow(const MIOPAN_RML_SETUP *setup);
/* Whether the screen was up last frame: setup_menu.c reads its input before
 * it draws. */
int MioPan_RmlSetupIsShown(void);
/* The command, and for a step its direction, -1 or 1. */
int MioPan_RmlSetupConsumeCommand(int *arg);
int MioPan_RmlSetupMenuCursor(void);
int MioPan_RmlSetupSettingCursor(void);

#ifdef __cplusplus
}

namespace Rml
{
class Context;
}

namespace MioPanRmlSetup
{
bool Init(Rml::Context *context);
void Shutdown();
void Frame();
}
#endif

#endif
