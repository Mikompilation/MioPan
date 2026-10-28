#ifndef MIOPAN_RML_TITLE_H
#define MIOPAN_RML_TITLE_H

/*
 * The title screen, in RmlUi: PRESS START, then the menu as one column --
 * New Game, Load Game, Album, Gallery, Options, Frame Rate and, the port's
 * own, Exit Game; Chapter Select and the debug menu join them with
 * [developer] debug_menu on.
 *
 * Stands in for what title_top.c and title_menu.c draw (one entry at a time
 * between two arrows).  The game still owns the phases: each frame it wants
 * the screen it asks for it, and it acts on what the player chose with its own
 * code -- the phase changes, the cues -- exactly as on its own input.  A frame
 * that does not ask hides it, so the screen goes with the phase.
 */

/* What the player did, for the phase to act on. */
#define MIOPAN_RML_TITLE_NONE   0
#define MIOPAN_RML_TITLE_START  1   /* PRESS START answered                  */
#define MIOPAN_RML_TITLE_BACK   2   /* the menu backed out to PRESS START    */
#define MIOPAN_RML_TITLE_CHOOSE 3   /* an entry: MioPan_RmlTitleMenuCursor() */

#ifdef __cplusplus
extern "C" {
#endif

/* Put the screen up for this frame, as PRESS START or as the menu with the
 * cursor on title_menu.c's entry `csr` when it opens.  1 when it is up; 0
 * when RmlUi is unavailable and the game should draw and read its own. */
int MioPan_RmlTitleShowPress(void);
int MioPan_RmlTitleShowMenu(int csr);
int MioPan_RmlTitleConsumeCommand(void);
/* The menu's cursor, as title_menu.c's entry number. */
int MioPan_RmlTitleMenuCursor(void);

#ifdef __cplusplus
}

namespace Rml
{
class Context;
}

namespace MioPanRmlTitle
{
bool Init(Rml::Context *context);
void Shutdown();
void Frame();
}
#endif

#endif
