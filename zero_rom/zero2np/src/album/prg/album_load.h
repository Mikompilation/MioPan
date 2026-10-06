/* ==========================================================================
 *  album/prg/album_load.h
 *
 *  The album's memory-card load screen (album_load.o) -- the page reached from
 *  the edit view's "Load" row.  Pick a MEMORY CARD slot, let the screen list
 *  the five album directories on it, pick one of those, confirm, and the
 *  album is read back into the current album slot.
 *
 *  Four exports, driven by album_edit.o's own mode tables
 *  (album_edit_mode_ctrl[] / album_edit_mode_disp[], data 2d7de8 / 2d7e40) in
 *  the same order every album page uses: CtrlInit on entry, Main and DispMain
 *  every frame, End when the page is left.
 *
 *  album_save.o is the write-side twin: same two-level step machine, same
 *  drawing layer, same staging buffer.  The two files differ only in the
 *  direction of the transfer and in album_save.o's extra recovery branches
 *  for a card with no album directory yet.
 *
 *  Reconstructed from the Feb 6 2004 prototype (SLES_523.84).
 * ======================================================================== */

#ifndef _ALBUM_PRG_ALBUM_LOAD_H
#define _ALBUM_PRG_ALBUM_LOAD_H

/* Reset the work block, hand the current album's picture pages to system/mc as
 * the save-data destination, and take the card lock (MemoryCardExeInit()).
 * The five per-album "this directory exists" flags start clear. */
void AlbumLoadCtrlInit(void);                           /* 0x1244e8 */

/* One frame of the screen.  Returns non-zero on the frame the closing fade has
 * finished *and* the staging buffer has been given back -- both are required,
 * because AlbumLoadOutReq() frees the buffer and the ring only comes back on a
 * later AlbumMemMain(). */
int  AlbumLoadMain(void);                               /* 0x1245f0 */

/* One frame of drawing.  Draws nothing outside steps 2 and 3, and nothing once
 * the closing fade has reached ZERO2_ANIM2D_STEP_END. */
void AlbumLoadDispMain(void);                           /* 0x125818 */

/* Drop the card lock.  Nothing else -- the staging buffer is released by
 * AlbumLoadOutReq() and the paks belong to album.o. */
void AlbumLoadEnd(void);                                /* 0x1257e0 */

#endif /* _ALBUM_PRG_ALBUM_LOAD_H */
