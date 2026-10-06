/* ==========================================================================
 *  debug/motion_viewer.h
 *
 *  Character model / animation viewer.  PORT ADDITION -- see motion_viewer.c.
 * ======================================================================== */

#ifndef _DEBUG_MOTION_VIEWER_H
#define _DEBUG_MOTION_VIEWER_H

/* Reset to the select page and reset the load heap.  Called once, from
 * debug.c's MotionViewer() wrapper, the first frame after the row is chosen. */
void MotionViewerInit(void);

/* One frame of the tool.  Returns non-zero on the frame the player leaves, at
 * which point every resource it claimed has already been given back. */
int  MotionViewerMain(void);

#endif /* _DEBUG_MOTION_VIEWER_H */
