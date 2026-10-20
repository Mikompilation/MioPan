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
}

#endif
