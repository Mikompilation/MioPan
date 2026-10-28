#ifndef MIOPAN_RML_LOAD_H
#define MIOPAN_RML_LOAD_H

/*
 * The title's Load Game screen, in RmlUi: the five saves side by side with
 * their rooms' pictures, what the chosen one holds -- chapter, room, play
 * time, clears -- and the game's own memory-card messages, in its words.
 *
 * Stands in for what loadgame.c draws through save_load_disp.c.  loadgame.c
 * still runs the memory card: each frame it says what state it is in, and it
 * acts on the player's answers with its own code -- the loads, the cues, the
 * phase changes.  A frame that does not ask hides the screen.
 */

/* What the screen is for, this frame. */
#define MIOPAN_RML_LOAD_BUSY   0   /* the card is being read: a message      */
#define MIOPAN_RML_LOAD_SELECT 1   /* pick a save                            */
#define MIOPAN_RML_LOAD_NOTICE 2   /* a message to acknowledge               */

/* What the player did. */
#define MIOPAN_RML_LOAD_NONE   0
#define MIOPAN_RML_LOAD_CHOSEN 1   /* load the save at MioPan_RmlLoadCursor() */
#define MIOPAN_RML_LOAD_BACK   2   /* back to the title menu                  */
#define MIOPAN_RML_LOAD_ACK    3   /* the notice was acknowledged             */

#ifdef __cplusplus
extern "C" {
#endif

/* Put the screen up for this frame: `mode` as above, the memory-card message
 * `msg_id` (type 0x50), the cursor loadgame.c holds -- taken when choosing
 * starts -- and whether the saves are known yet.  1 when it is up; 0 when
 * RmlUi is unavailable and loadgame.c should draw and read its own. */
int  MioPan_RmlLoadShow(int mode, int msg_id, int cursor, int list);
/* Whether the screen was up last frame: loadgame.c reads its answers before
 * it draws. */
int  MioPan_RmlLoadIsShown(void);
/* Save `slot`'s room picture, the pak loadgame.c read for it (file `label`),
 * once it has arrived; null when the slot has none. */
void MioPan_RmlLoadSetPicture(int slot, const void *pk2, int label);
int  MioPan_RmlLoadConsumeCommand(void);
int  MioPan_RmlLoadCursor(void);

#ifdef __cplusplus
}

namespace Rml
{
class Context;
}

namespace MioPanRmlLoad
{
bool Init(Rml::Context *context);
void Shutdown();
void Frame();
}
#endif

#endif
