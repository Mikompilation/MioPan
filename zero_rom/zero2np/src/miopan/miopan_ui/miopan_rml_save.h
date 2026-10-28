#ifndef MIOPAN_RML_SAVE_H
#define MIOPAN_RML_SAVE_H

/*
 * The save screen, in RmlUi: the load screen's five saves and detail, the
 * game's own memory-card messages, "save here?" before saving, and the card's
 * own questions -- format it, make the save folder, remake a broken one -- as
 * yes / no windows.  One screen for every place the game saves: the save
 * points, the clear menu and Mission Mode.
 *
 * Stands in for what game_data_save.c draws through save_load_disp.c.
 * game_data_save.c still runs the memory card: each frame it says what state
 * it is in, and it acts on the player's answers with its own code -- the
 * writes, the cues, leaving.  A frame that does not ask hides the screen.
 */

/* What the screen is for, this frame. */
#define MIOPAN_RML_SAVE_BUSY     0   /* the card is being read or written      */
#define MIOPAN_RML_SAVE_SELECT   1   /* pick a save to write over, or a new one */
#define MIOPAN_RML_SAVE_NOTICE   2   /* a message to acknowledge               */
#define MIOPAN_RML_SAVE_QUESTION 3   /* the message is a yes / no question     */

/* What the player did. */
#define MIOPAN_RML_SAVE_NONE     0
#define MIOPAN_RML_SAVE_CHOSEN   1   /* save to MioPan_RmlSaveCursor()         */
#define MIOPAN_RML_SAVE_BACK     2   /* leave without saving                    */
#define MIOPAN_RML_SAVE_ACK      3   /* the notice was acknowledged             */
#define MIOPAN_RML_SAVE_YES      4   /* the answer to the question              */
#define MIOPAN_RML_SAVE_NO       5

#ifdef __cplusplus
extern "C" {
#endif

/* Put the screen up for this frame: `mode` as above, the memory-card message
 * `msg_id` (type 0x50), the cursor game_data_save.c holds -- taken when
 * choosing starts -- whether the saves are known yet, and which of them can
 * take the cursor, a bit each.  1 when it is up; 0 when RmlUi is unavailable
 * and game_data_save.c should draw and read its own. */
int  MioPan_RmlSaveShow(int mode, int msg_id, int cursor, int list,
                        unsigned open);
/* Whether the screen was up last frame: game_data_save.c reads its answers
 * before it draws. */
int  MioPan_RmlSaveIsShown(void);
/* Save `slot`'s room picture, the pak game_data_save.c read for it (file
 * `label`); null when the slot has none, or the picture is not its room's. */
void MioPan_RmlSaveSetPicture(int slot, const void *pk2, int label);
int  MioPan_RmlSaveConsumeCommand(void);
int  MioPan_RmlSaveCursor(void);

#ifdef __cplusplus
}

namespace Rml
{
class Context;
}

namespace MioPanRmlSave
{
bool Init(Rml::Context *context);
void Shutdown();
void Frame();
}
#endif

#endif
