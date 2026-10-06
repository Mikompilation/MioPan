/* ==========================================================================
 *  album/prg/album_disp.h
 *
 *  The photo album's drawing layer (album_disp.o).  Thirty-five primitives
 *  that between them compose every album page: album_edit.o's two-album
 *  edit view, album_view.o's photo viewer, album_save.o / album_load.o's
 *  memory-card screens and the slot-select window.
 *
 *  The module holds no state at all -- no file statics, no work block.  Every
 *  entry point is "copy an album_tex[] record into a DISP_SPRT, offset it,
 *  scale its alpha by the caller's, draw it", and the caller owns the
 *  animation counters that produce `alpha`, `rgb` and `scl`.
 *
 *  Conventions shared by the whole file:
 *    - off_x / off_y are the page's slide offset.  Several routines take them
 *      and read neither; see album_disp.c.
 *    - alpha is the caller's master alpha, folded in as
 *      `ds.alpha * alpha >> 7`.
 *    - rgb is a pulse intensity written into r/g/b, usually alongside an
 *      additive alphar.
 *    - album_type indexes the seven album kinds (0..4 are the five
 *      memory-card albums, 5 and 6 the two that never reach a card).
 *    - data_label is 0 for album A (the upper half of the edit page) and 1
 *      for album B (the lower half).
 *
 *  Reconstructed from the Feb 6 2004 prototype (SLES_523.84).
 * ======================================================================== */

#ifndef _ALBUM_PRG_ALBUM_DISP_H
#define _ALBUM_PRG_ALBUM_DISP_H

#include <sys/types.h>                      /* u_char */

/* The seven album kinds.  Only 0..4 have a save-screen presence, which is why
 * every per-type table in album_disp.c has -1 in rows 5 and 6 and every
 * AlbumSave* routine rejects an album_type >= 5 with a PRINT_WARNING. */
#define ALBUM_TYPE_MAX          7
#define ALBUM_SAVE_TYPE_MAX     5

/* Album A / album B -- the two halves of the edit page, and the index into
 * album.c's album_info[]. */
#define ALBUM_DATA_A            0
#define ALBUM_DATA_B            1
#define ALBUM_DATA_MAX          2

/* Sixteen photos an album holds, as two rows of eight. */
#define ALBUM_THUMB_X_NUM       8
#define ALBUM_THUMB_Y_NUM       2

/* ---- animation ---------------------------------------------------------- *
 * Both drive the caller's ZERO2_ANIM2D_STEP_* pair (see zero2_anim2d.h) and
 * write this frame's alpha through it. */

/* The page open/close fade: 10 frames in, 5 frames out. */
void AlbumInOutAnimCtrl(char *anim_step, char *anim_timer, u_char *alpha);

/* The edit menu's own fade, 5 frames each way, which additionally produces
 * the selected and unselected row scales for AlbumMenu{,Non}SelFrameDisp. */
void AlbumEditMenuAnimCtrl(char *anim_step, char *anim_timer, u_char *alpha,
                           float *sel_scl, float *non_sel_scl);

/* ---- shared furniture --------------------------------------------------- */

/* A full-screen black quad at max_alpha, scaled by alpha. */
void AlbumBlackBgDisp(int off_x, int off_y, u_char alpha, u_char max_alpha);

void AlbumTitleFrameDisp(int off_x, int off_y, u_char alpha);
void AlbumTitleDisp(int off_x, int off_y, u_char alpha);

/* ---- the edit page ------------------------------------------------------ */

/* The whole static frame of both albums in one call. */
void AlbumEditFrameDisp(int off_x, int off_y, u_char alpha);

/* The pulsing outline round whichever album is current. */
void AlbumA_CurrentFrameFlareDisp(int off_x, int off_y, u_char alpha, u_char rgb);
void AlbumB_CurrentFrameFlareDisp(int off_x, int off_y, u_char alpha, u_char rgb);

/* The sixteen empty thumbnail plates and their 1..16 slot numbers. */
void AlbumThumbnailBaseDisp(int data_label, int off_x, int off_y, u_char alpha);
void AlbumThumbnailBaseNumberDisp(int data_label, int off_x, int off_y, u_char alpha);

/* The thumbnail cursor, placed from a flat photo number (row = no / 8). */
void AlbumEditAlbumACursorDisp(int photo_no, int off_x, int off_y, u_char alpha, u_char rgb);
void AlbumEditAlbumBCursorDisp(int photo_no, int off_x, int off_y, u_char alpha, u_char rgb);

/* The album's own spine art, two pieces per type. */
void AlbumEditAlbumDisp(int data_label, int album_type, int off_x, int off_y, u_char alpha);

/* The info window: body, item rows, album number and photo number. */
void AlbumEditAlbumInfoWinDisp(int album_type, int off_x, int off_y, u_char alpha);
void AlbumEditAlbumInfoWinItemDisp(int album_type, int off_x, int off_y, u_char alpha);
void AlbumEditInfoNoDisp(int album_type, int off_x, int off_y, u_char alpha);
void AlbumEditInfoPhotoNoDisp(int album_type, int csr_num, int off_x, int off_y, u_char alpha);

/* The enlarged-photo frame, and the badge over a protected photo. */
void AlbumEditPhotoFrameDisp(int off_x, int off_y, u_char alpha, void *pk2_addr);
void AlbumEditPhotoProtectionFrameDisp(int off_x, int off_y, u_char alpha);

/* One edit-menu row.  `scl` opens the frame from its seam; data_label picks
 * which of the two rows' art to use. */
void AlbumMenuSelFrameDisp(int data_label, int x, int y, u_char alpha, float scl, u_char rgb);
void AlbumMenuNonSelFrameDisp(int data_label, int x, int y, u_char alpha, float scl);
void AlbumMenuItemDisp(int menu_label, int x, int y, u_char alpha);

void AlbumEditCaptionDisp(int off_x, int off_y, u_char alpha);

/* The yes / no window's plates, flare and cursor. */
void AlbumConfYesNoDisp(int conf_csr, int off_x, int off_y, u_char alpha, u_char rgb);

/* ---- the slot-select window --------------------------------------------- */

void AlbumSlotSelWinDisp(int cursor, int off_x, int off_y, u_char alpha);
void AlbumSlotSelCaptionDisp(int off_x, int off_y, u_char alpha);

/* ---- the memory-card save / load screen --------------------------------- */

void AlbumSaveSelAlbumDisp(int album_type, int off_x, int off_y, u_char alpha);
void AlbumSaveSelAlbumCsrDisp(int album_type, int off_x, int off_y, u_char alpha, u_char rgb);
void AlbumSaveNonSelAlbumCsrDisp(int album_type, int off_x, int off_y, u_char alpha, u_char rgb);
void AlbumSaveSelAlbumCsrFlareDisp(int album_type, int off_x, int off_y, u_char alpha, u_char rgb);
void AlbumSaveSelAlbumNameDisp(int album_type, int off_x, int off_y, u_char alpha, int col_label);
void AlbumSaveAlbumMaskDisp(int album_type, int off_x, int off_y, u_char alpha, u_char rgb);
void AlbumSaveSelSlotDisp(int sel_slot, int off_x, int off_y, u_char alpha, u_char rgb);

void AlbumSaveMsgWinDisp(int off_x, int off_y, u_char alpha);
void AlbumMcMsgWinDisp(int off_x, int off_y, u_char alpha);

#endif /* _ALBUM_PRG_ALBUM_DISP_H */
