/* ==========================================================================
 *  album/prg/album_save.h
 *
 *  The album's memory-card save screen (album_save.o) -- the page reached from
 *  the edit view's "Save" row.  Pick a MEMORY CARD slot, pick one of the five
 *  album positions on the card, confirm, and the current album is written
 *  there.
 *
 *  Four exports, driven by album_edit.o's own mode tables
 *  (album_edit_mode_ctrl[] / album_edit_mode_disp[], data 2d7de8 / 2d7e40) in
 *  the same order every album page uses: CtrlInit on entry, Main and DispMain
 *  every frame, End when the page is left.
 *
 *  album_load.o is the read-side twin: same two-level step machine, same
 *  drawing layer, same staging buffer.  Where the load screen only reads, this
 *  one has to cope with an album directory that does not exist yet, one that
 *  is broken, and a card that is not formatted at all -- which is what turns
 *  album_load's 15 card states into 29 here.
 *
 *  Reconstructed from the Feb 6 2004 prototype (SLES_523.84).
 * ======================================================================== */

#ifndef _ALBUM_PRG_ALBUM_SAVE_H
#define _ALBUM_PRG_ALBUM_SAVE_H

/* Reset the work block, hand the current album's picture pages to system/mc as
 * the save-data source, and take the card lock (MemoryCardExeInit()). */
void AlbumSaveCtrlInit(void);                           /* 0x126190 */

/* One frame of the screen.  Returns non-zero on the frame the closing fade has
 * finished *and* the staging buffer has been given back. */
int  AlbumSaveMain(void);                               /* 0x126258 */

/* One frame of drawing.  Draws nothing outside steps 2 and 3, and nothing once
 * the closing fade has reached ZERO2_ANIM2D_STEP_END. */
void AlbumSaveDispMain(void);                           /* 0x127c88 */

/* Drop the card lock. */
void AlbumSaveEnd(void);                                /* 0x127c50 */

#endif /* _ALBUM_PRG_ALBUM_SAVE_H */
