#ifndef MIOPAN_RML_SAVEPOINT_H
#define MIOPAN_RML_SAVEPOINT_H

/*
 * The save point's menu, in RmlUi: save, album and leave, in the game's own
 * words, with its "save?" and "album?" questions as yes / no windows.
 *
 * Stands in for what savepoint_top.c draws over the save point's background.
 * savepoint_top.c still runs the menu: each frame it puts the screen up, and
 * it acts on the player's choice with its own code -- the windows closing,
 * the phase changes, leaving.  A frame that does not ask hides the screen.
 */

/* What the player did. */
#define MIOPAN_RML_SAVEPOINT_NONE   0
#define MIOPAN_RML_SAVEPOINT_CHOSEN 1   /* the row at MioPan_RmlSavePointCursor(),
                                           its question answered yes */

#ifdef __cplusplus
extern "C" {
#endif

/* Put the screen up for this frame with the cursor savepoint_top.c holds --
 * taken each time the menu opens -- and whether it is closing, when it takes
 * no input.  1 when it is up; 0 when RmlUi is unavailable and savepoint_top.c
 * should draw and read its own. */
int  MioPan_RmlSavePointShow(int cursor, int closing);
/* Whether the screen was up last frame: savepoint_top.c reads its answers
 * before it draws. */
int  MioPan_RmlSavePointIsShown(void);
int  MioPan_RmlSavePointConsumeCommand(void);
/* 0 save, 1 album, 2 leave: savepoint_top.c's own rows. */
int  MioPan_RmlSavePointCursor(void);

#ifdef __cplusplus
}

namespace Rml
{
class Context;
}

namespace MioPanRmlSavePoint
{
bool Init(Rml::Context *context);
void Shutdown();
void Frame();
}
#endif

#endif
