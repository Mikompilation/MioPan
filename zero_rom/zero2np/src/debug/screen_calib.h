/* ==========================================================================
 *  debug/screen_calib.h
 *
 *  Screen calibration test pattern.  PORT ADDITION -- see screen_calib.c.
 * ======================================================================== */

#ifndef _DEBUG_SCREEN_CALIB_H
#define _DEBUG_SCREEN_CALIB_H

/* Reset to page 0.  Called once, from debug.c's ScreenCalib() wrapper, the
 * first frame after the row is chosen -- the same shape SceneTestInit() has. */
void ScreenCalibInit(void);

/* One frame of the tool.  Draws the whole page itself and returns non-zero on
 * the frame the player leaves, which is what puts DebugMain() back in the
 * menu. */
int  ScreenCalibMain(void);

#endif /* _DEBUG_SCREEN_CALIB_H */
