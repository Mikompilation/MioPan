#ifndef MIOPAN_RML_PAUSE_H
#define MIOPAN_RML_PAUSE_H

/*
 * The pause screen, in RmlUi: Continue, Options, Return to Title.
 *
 * Stands in for the panel ingame/pause/prg/pause.c draws, which offers
 * continue / return to title / a vibration toggle in the game's own message
 * strings -- with no string for "Options" there is no row to add to it.  The
 * vibration toggle moves into Options with the rest of the settings.  The game
 * still owns the phase: it parks and redraws the paused frame, asks this
 * screen what the player chose, and acts on it exactly as on its own codes.
 */

/* What the player chose -- the same codes PauseMain() returns. */
#define MIOPAN_RML_PAUSE_NONE   0
#define MIOPAN_RML_PAUSE_RESUME 1
#define MIOPAN_RML_PAUSE_TITLE  2
#define MIOPAN_RML_PAUSE_DEBUG  3   /* SELECT, with [developer] debug_menu on */

#ifdef __cplusplus
extern "C" {
#endif

/* 1 with the screen up; 0 when RmlUi is unavailable and the game's own panel
 * should run instead. */
int  MioPan_RmlPauseOpen(void);
void MioPan_RmlPauseClose(void);
int  MioPan_RmlPauseIsOpen(void);
int  MioPan_RmlPauseConsumeResult(void);
/* Step aside for a message the game draws -- the controller-unplugged one --
 * without closing. */
void MioPan_RmlPauseSetHidden(int hidden);

#ifdef __cplusplus
}

namespace Rml
{
class Context;
}

namespace MioPanRmlPause
{
bool Init(Rml::Context *context);
void Shutdown();
void Frame();
}
#endif

#endif
