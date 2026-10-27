#ifndef MIOPAN_RML_OPTIONS_H
#define MIOPAN_RML_OPTIONS_H

/*
 * The options screen, in RmlUi: the game's own Options and the port's
 * settings on one page, in place of outgame/option.c's screen.
 *
 * Sections run across the top -- Game, Display, Graphics, Audio, Controls --
 * and each is a column of rows: a choice (left / right steps it), a slider
 * (left / right moves it, the mouse can drag it) or an action.  Up / down move
 * the cursor, L1 / R1 (Q / E, Page Up / Down) change section, Triangle or
 * Escape backs out.  The mouse does all of it too: hover moves the cursor,
 * the arrows and the slider take clicks, the sections and the prompts are
 * buttons.
 *
 * Everything applies the moment it changes.  The port's settings save
 * themselves to miopan.ini as they do from the overlay; the game's own are
 * written to opt_wrk and to the title screen's working copy alike, so the
 * option phase's OptionEnd() has nothing left to commit, and they reach the
 * memory card the way they always did -- through the system-file save, which
 * the title's Options offers as a row.
 *
 * The opener drives it with the C entry points below: Open() it, poll
 * ConsumeRequest() once a frame, and act on what comes back.
 */

/* Where the screen was opened from.  Only the title offers the memory-card
 * save, because only the title's option phase can run it. */
#define MIOPAN_RML_OPTIONS_FROM_TITLE   0
#define MIOPAN_RML_OPTIONS_FROM_PAUSE   1
#define MIOPAN_RML_OPTIONS_FROM_OVERLAY 2

/* What the screen asks of its opener. */
#define MIOPAN_RML_OPTIONS_REQ_NONE  0
#define MIOPAN_RML_OPTIONS_REQ_CLOSE 1  /* the player backed out; it is closed */
#define MIOPAN_RML_OPTIONS_REQ_SAVE  2  /* run the system-file save, then
                                           Close() on success or SetHidden(0)
                                           to come back */

#ifdef __cplusplus
extern "C" {
#endif

/* 1 with the screen up; 0 when RmlUi is unavailable and the caller should
 * fall back to its own screen. */
int  MioPan_RmlOptionsOpen(int from);
void MioPan_RmlOptionsClose(void);
int  MioPan_RmlOptionsIsOpen(void);
int  MioPan_RmlOptionsConsumeRequest(void);
/* Step aside for something the game draws -- the memory-card save -- without
 * closing. */
void MioPan_RmlOptionsSetHidden(int hidden);

#ifdef __cplusplus
}

namespace Rml
{
class Context;
}

namespace MioPanRmlOptions
{
/* The backend's hooks: load the document, release it, and once per frame
 * before RmlUi updates, refresh what the rows show. */
bool Init(Rml::Context *context);
void Shutdown();
void Frame();
}
#endif

#endif
