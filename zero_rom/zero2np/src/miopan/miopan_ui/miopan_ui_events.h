#ifndef MIOPAN_UI_EVENTS_H
#define MIOPAN_UI_EVENTS_H

namespace MioPanUi
{
/* The Debug menu's row that opens the event browser.  Call from inside an
 * open menu. */
void DrawEventsMenuItem();

/* The event browser -- every event in the game by chapter, and the way to
 * jump to any of them (miopan_event_jump.cpp does the work).  Call once a
 * frame from Draw(), on the game thread: the jump driver steps here whether
 * the window is open or not. */
void DrawEventsWindow();
}

#endif
