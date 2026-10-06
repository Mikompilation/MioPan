/* ==========================================================================
 *  debug/test2d.h
 *
 *  2D drawing-layer test pattern.  PORT ADDITION -- see test2d.c.
 * ======================================================================== */

#ifndef _DEBUG_TEST2D_H
#define _DEBUG_TEST2D_H

/* Reset to page 0 and rebuild the glyph sheet.  Called once, from debug.c's
 * Test2d() wrapper, the first frame after the row is chosen. */
void Test2dInit(void);

/* One frame of the tool.  Draws the whole page itself and returns non-zero on
 * the frame the player leaves. */
int  Test2dMain(void);

#endif /* _DEBUG_TEST2D_H */
