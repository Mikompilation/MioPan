/* ==========================================================================
 *  album/prg/album_edit.h
 *
 *  The album's edit page -- the two albums side by side, sixteen thumbnail
 *  slots each, the five-row action menu and its confirm windows.  At 0x4298
 *  of .text it is the largest translation unit in the folder, and it is the
 *  thing that drives album_save.o, album_load.o and album_view.o: its ten
 *  modes dispatch through four parallel tables, and modes 7 and 8 hand whole
 *  frames to the save and load screens.
 *
 *  Only these five symbols are exported.  Everything else -- the ten mode
 *  handlers, the five menu-condition tests, the sixteen drawing routines --
 *  is static to album_edit.c.
 *
 *  Reconstructed from the Feb 6 2004 prototype (SLES_523.84).
 * ======================================================================== */

#ifndef _ALBUM_PRG_ALBUM_EDIT_H
#define _ALBUM_PRG_ALBUM_EDIT_H

/* ---- album.c's phase machine -------------------------------------------- *
 * album.c dispatches these three through album_mode_*_func[ALBUM_MODE_EDIT]
 * every frame the edit page owns the screen.  AlbumEditMain() returns
 * non-zero exactly once, on the frame the closing fade reaches step 4 --
 * which happens either because the player confirmed "leave the album" or
 * because AlbumEditMoveViewReq() is handing over to album_view.o. */

void AlbumEditCtrlInit(void);   /* 0x120250 */
int  AlbumEditMain(void);       /* 0x120358 */
void AlbumEditDispMain(void);   /* 0x1226e8 */

/* ---- the two entry points the card screens call ------------------------- *
 * Both are reached from album_load.o and album_save.o rather than from
 * album.c.  The first drops the cached enlargement so the next frame
 * re-inflates whatever the cursor now points at -- every cursor move, album
 * flip and card load calls it.  The second closes the action menu, which the
 * card screens do on their way out so the page comes back to a bare top
 * view. */

void AlbumEditUncompressPhotoReq(void);     /* 0x120918 */
void AlbumEditMenuDelete(void);             /* 0x122300 */

#endif /* _ALBUM_PRG_ALBUM_EDIT_H */
