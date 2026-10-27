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

/* The game's own Options settings the Controls window's movement rows
 * override -- opt_wrk.move_operate (1 "vehicle") and opt_wrk.ana_replace (1
 * the right stick aims) -- so it can say what "Game setting" means now. */
int GameMoveOperate();
int GameAnaReplace();
}

#endif
