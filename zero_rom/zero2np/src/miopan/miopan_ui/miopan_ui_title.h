#ifndef MIOPAN_UI_TITLE_H
#define MIOPAN_UI_TITLE_H

namespace MioPanUi
{
/* The Settings menu's row that opens the title screen window.  Call from
 * inside an open menu. */
void DrawTitleMenuItem();

/* The title screen window: the backdrop, the room, the two camera points and
 * the path between them, and the music.  It edits miopan_config.title and
 * tells outgame/title_room.c when a change needs acting on; the overlay's
 * settle-and-save writes it to miopan.ini.  Call once a frame from Draw(), on
 * the game thread. */
void DrawTitleWindow();
}

#endif
