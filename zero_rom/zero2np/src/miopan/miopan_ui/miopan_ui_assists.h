#ifndef MIOPAN_UI_ASSISTS_H
#define MIOPAN_UI_ASSISTS_H

namespace MioPanUi
{
/* The overlay's "Assists" menu: the useful parts of the prototype's own debug
 * menu, reachable without entering it.  Call between BeginMainMenuBar() and
 * EndMainMenuBar().  It writes game state directly, which is safe because the
 * overlay is drawn on the game thread from after_super(), between one game
 * frame and the next. */
void DrawAssistsMenu();

/* Rows for the overlay's "Debug" menu that write game state, on the same
 * terms: the ROM menu's DISP switches as a submenu, and the two rows that lead
 * into the ROM menu itself.  Call from inside an open menu. */
void DrawSceneLayersMenu();
void DrawDeveloperItems();
}

#endif
