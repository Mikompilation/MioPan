#ifndef MIOPAN_RML_MENU_TOP_H
#define MIOPAN_RML_MENU_TOP_H

/*
 * The in-game menu's hub, in RmlUi, composed as the game composes it: the
 * pages down the left on their gold bars -- Map, Item, Camera, Photo, File,
 * Memo, Radio, Spirit List, those the game has open -- the status plate, the
 * play data on its sheet, the chapter's pillar and the caption window with the
 * game's line for the page under the cursor.  The art is the game's own; the
 * words and numbers are RmlUi's.
 *
 * Stands in for what menu_top.c draws over the menu's shoji backdrop.
 * menu_top.c still runs the hub: each frame it puts the screen up, and it acts
 * on the player's choice with its own code -- the page's paks, the fades, the
 * page change, closing the menu.  A frame that does not ask hides the screen.
 */

/* What the player did. */
#define MIOPAN_RML_MENU_TOP_NONE   0
#define MIOPAN_RML_MENU_TOP_CHOSEN 1   /* open the page at MioPan_RmlMenuTopCursor() */
#define MIOPAN_RML_MENU_TOP_BACK   2   /* close the menu                            */

#ifdef __cplusplus
extern "C" {
#endif

/* Put the screen up for this frame with the cursor menu_top.c holds -- taken
 * each time the hub opens -- the pages open, a bit each in MENU_STEP_* order,
 * whether the hub is on its way out, when it takes no input, and the
 * chapter's plate: the pak menu_top.c loaded for it and the first of its
 * three strips in menu_top[].  1 when it is up; 0 when RmlUi is unavailable
 * and menu_top.c should draw and read its own. */
int MioPan_RmlMenuTopShow(int cursor, unsigned open, int closing,
                          const void *chapter_pk2, int chapter_sprite);
/* Whether the screen was up last frame: menu_top.c reads its answers before
 * it draws. */
int MioPan_RmlMenuTopIsShown(void);
int MioPan_RmlMenuTopConsumeCommand(void);
/* The page under the cursor, MENU_STEP_*. */
int MioPan_RmlMenuTopCursor(void);

#ifdef __cplusplus
}

namespace Rml
{
class Context;
}

namespace MioPanRmlMenuTop
{
bool Init(Rml::Context *context);
void Shutdown();
void Frame();
}
#endif

#endif
