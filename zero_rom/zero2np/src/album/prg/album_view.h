/* ==========================================================================
 *  album/prg/album_view.h
 *
 *  The album's photo viewer (album_view.o) -- the page you reach by picking a
 *  photo in the edit view.  One picture blown up to 346x230 with its date,
 *  score, room and subject lines beside it, and the previous / next photos as
 *  thumbnails either side.
 *
 *  Six exports, all driven by album.c's phase machine: an init, a background
 *  loader, a per-frame main, a per-frame draw, and the two texture-teardown
 *  entry points.  Everything else in the file is file-local.
 *
 *  Reconstructed from the Feb 6 2004 prototype (SLES_523.84).
 * ======================================================================== */

#ifndef _ALBUM_PRG_ALBUM_VIEW_H
#define _ALBUM_PRG_ALBUM_VIEW_H

/* Reset the work block and rebuild the list of photos worth showing.  Reads
 * the current album out of album.c, so AlbumBackGroundLoadReq() for the right
 * album type has to have happened first. */
void AlbumViewCtrlInit(void);

/* Claim and post the per-album-type background pak (one of the six
 * ALBM_KKD_PAT*_PK2 files, plus the language offset). */
void AlbumViewBackGroundLoadReq(int album_type);

/* One frame of the viewer.  Returns non-zero on the frame it hands control
 * back to the edit view, having stored the photo the cursor left on. */
int  AlbumViewMain(void);

/* One frame of drawing.  Draws nothing outside steps 2 and 3, and nothing once
 * the closing fade has finished. */
void AlbumViewDispMain(void);

/* Give the background pak back, and withdraw its load if it is still in
 * flight. */
void LiberateAlbumViewTex(void);
void AlbumViewTexLoadCancel(void);

#endif /* _ALBUM_PRG_ALBUM_VIEW_H */
