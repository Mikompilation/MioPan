/* ==========================================================================
 *  outgame/title_room.h
 *
 *  PORT ADDITION -- the title screen's room backdrop.
 *
 *  The original title draws a scrolling 2D background behind a framed picture
 *  of the twins with the logo printed on it, and the menu over that.  This
 *  puts one of the game's own rooms there instead, loaded and lit by the same
 *  code the game plays it with, while the camera travels between two points
 *  -- the way the Xbox release's title screen does.  The picture goes, and
 *  PRESS START, the menu and every screen behind it are the ROM's own,
 *  untouched: title.c is still the whole title, and this file only sits
 *  around it.
 *
 *  It is wired in two places.  GID_TITLE_MODE's init / pre / end slots in
 *  gphase.c's tables name the three wrappers below, each of which calls the
 *  ROM's own callback; and DispTitleZeroLogo() / DispTitleTecmoLogo() in
 *  title_disp.c ask TitleRoomHidesTitleArt() before drawing.  With [title]
 *  backdrop = classic in miopan.ini neither does anything else, and the title
 *  is exactly the original.
 *
 *  Everything it reads is miopan_config.title; the F1 overlay's
 *  Settings > Title screen window edits that live and calls in here to act on
 *  a change.  Both run on the game thread, so neither needs a lock.
 * ======================================================================== */

#ifndef _OUTGAME_TITLE_ROOM_H
#define _OUTGAME_TITLE_ROOM_H

#include "../main/gphase.h"         /* GPHASE_ENUM */

/* --------------------------------------------------------------------------
 *  GID_TITLE_MODE's callbacks, as gphase.c's tables name them.  Each runs the
 *  ROM's own (init_Title_Mode / pre_Title_Mode / end_Title_Mode) and adds the
 *  backdrop and the music around it.
 * ------------------------------------------------------------------------ */
void        init_TitleRoom_Mode(void);
GPHASE_ENUM pre_TitleRoom_Mode(GPHASE_ENUM dummy);
void        end_TitleRoom_Mode(void);

/* --------------------------------------------------------------------------
 *  For title_disp.c.  Non-zero while the room is the backdrop (loading or
 *  shown): the original's title picture -- the twins in the burnt frame, with
 *  the logo printed on it, one 510x448 plate -- and the copyright line under
 *  it are not drawn, so the room is the picture and the screens' own text is
 *  all that sits on it.  DispTitleZeroLogo() and DispTitleTecmoLogo() ask.
 * ------------------------------------------------------------------------ */
int TitleRoomHidesTitleArt(void);

/* --------------------------------------------------------------------------
 *  For the overlay.
 * ------------------------------------------------------------------------ */
typedef enum
{
    TITLE_ROOM_OFF,             /* classic backdrop, or not on the title       */
    TITLE_ROOM_LOADING,         /* streaming in                                */
    TITLE_ROOM_SHOWN,           /* resident and drawn                          */
    TITLE_ROOM_RELEASED,        /* on a screen that needs the room memory      */
    TITLE_ROOM_MISSING,         /* that room is not on this disc               */
} TITLE_ROOM_STATE;

TITLE_ROOM_STATE TitleRoomGetState(void);

/* The rooms, by the number RYxx_PK2 counts from (room n is files
 * RY00_PK2 + n * 5 ..+4).  Code is the file's own name ("RKS00"), place the
 * house or area it belongs to. */
int         TitleRoomCount(void);
const char *TitleRoomCode(int room);
const char *TitleRoomPlace(int room);
int         TitleRoomIsOnDisc(int room);

/* Drop what is resident and load miopan_config.title.room again; also how a
 * backdrop change takes effect.  With camera_set at 0 the room is framed by
 * itself once it is in. */
void TitleRoomReload(void);

/* Re-frame the resident room now (clears camera_set if nothing is resident,
 * so the next load does it). */
void TitleRoomFrameAutomatically(void);

/* Copy the live camera into point 0 (A) or 1 (B).  In a game, the room Mio is
 * in becomes the title's room as well -- and if that differs from the one the
 * other point was taken in, both points take this view.  Returns 1 when a
 * room change came with it. */
int  TitleRoomCaptureView(int point);

/* The camera path.  Progress is 0..1 over one whole cycle (out and back for
 * ping-pong). */
float TitleRoomGetProgress(void);
void  TitleRoomSetProgress(float progress);
int   TitleRoomGetPaused(void);
void  TitleRoomSetPaused(int paused);
/* -1 runs the path; 0 or 1 holds the camera on that point. */
int   TitleRoomGetPreview(void);
void  TitleRoomSetPreview(int point);
/* Steer the camera by hand with the free camera's controls (Assists > Free
 * camera lists them).  The path stops while it is on. */
int   TitleRoomGetFly(void);
void  TitleRoomSetFly(int fly);

/* The tracks the overlay offers.  Entry 0 is "the game's own title music";
 * the rest are stream files, looped. */
int         TitleRoomMusicCount(void);
int         TitleRoomMusicFile(int index);
const char *TitleRoomMusicName(int index);
const char *TitleRoomMusicNote(int index);
int         TitleRoomMusicIndex(int file);
/* Apply miopan_config.title.music / music_volume now rather than on the next
 * screen change. */
void        TitleRoomMusicChanged(void);

#endif /* _OUTGAME_TITLE_ROOM_H */
