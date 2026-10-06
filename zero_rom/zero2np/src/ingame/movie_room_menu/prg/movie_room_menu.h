/* ==========================================================================
 *  ingame/movie_room_menu/prg/movie_room_menu.h
 *
 *  movie_room_menu.o -- the reel-selection menu that stands in front of the
 *  movie room's projectors.  ingame.c drives it as a sub-phase of the story
 *  mode (SetIngameMovieRoomMenu), and it hands its result to
 *  movie_projecter.o, which is what actually plays the film.
 *
 *  Reconstructed from the Feb 6 2004 prototype (SLES_523.84).
 * ======================================================================== */

#ifndef _INGAME_MOVIE_ROOM_MENU_H
#define _INGAME_MOVIE_ROOM_MENU_H

#include "eetypes.h"

/* One row of the strip of reels the player can scroll through: an inventory
 * item id, the TIM2 file that pictures it, and where that file was loaded.
 * Only the first `have_num` entries are live -- SetDispFilmData() compacts
 * the seven possible reels down to the ones actually in the inventory. */
typedef struct                      /* 0xc */
{
    /* 0x0 */ int   item_label;     /* ITM_REEL1..7 = item id 34..40      */
    /* 0x4 */ int   tex_label;      /* ITEM_0NN_TM2 -- its inventory icon */
    /* 0x8 */ void *data_addr;      /* where that TIM2 was loaded, or NULL*/
} DISP_FILM_REEL;

/* The menu's whole state.  `mode` indexes both dispatch tables in step, the
 * way every outgame screen's now_place does; `step` is the outer 0 load /
 * 1 run / 2 leave sequence MovieRoomMenuMain() walks. */
typedef struct                      /* 0x7 */
{
    /* 0x0 */ u_char step;
    /* 0x1 */ u_char mode;          /* MOVIE_ROOM_MENU_MODE_*             */
    /* 0x2 */ u_char cursor;        /* which reel is centred              */
    /* 0x3 */ u_char next_cursor;   /* the one scrolling in               */
    /* 0x4 */ u_char conf_csr;      /* cursor inside a yes/no or 3-row box*/
    /* 0x5 */ u_char play_flg;      /* never written or read in this build*/
    /* 0x6 */ u_char have_num;      /* reels in the inventory, 0..7       */
} MOVIE_ROOM_MENU_CTRL;

/* Drawing-side state.  anim_step/anim_timer are the window fade every other
 * menu screen has; here nothing but MovieRoomMenuDispInit() ever writes them
 * (see the note in the .c), so only the move_* trio is live. */
typedef struct                      /* 0x8 */
{
    /* 0x0 */ char      anim_step;
    /* 0x1 */ char      anim_timer;
    /* 0x2 */ char      cursor_timer;    /* Zero2Anim2D_CsrAnimCtrl phase */
    /* 0x3 */ char      move_anim_step;  /* 0 start, 1 running, 2 settled */
    /* 0x4 */ short int move_anim_timer; /* 0..8                          */
    /* 0x6 */ char      move_rot;        /* 0 = scrolling left, 1 = right */
} MOVIE_ROOM_MENU_DISP;

/* MOVIE_ROOM_MENU_CTRL::mode -- indexes movie_room_menu_pad_func[] and
 * movie_room_menu_disp_func[] together.  The ROM's debug info carries no enum
 * for these; the names come from the handler each slot points at. */
#define MOVIE_ROOM_MENU_MODE_START_MSG      0   /* "use the projector?"   */
#define MOVIE_ROOM_MENU_MODE_FILM_SEL       1   /* scroll the reels       */
#define MOVIE_ROOM_MENU_MODE_FILM_PLAY_CONF 2   /* "play this one?"       */
#define MOVIE_ROOM_MENU_MODE_EXIT_CONF      3   /* "stop watching?"       */
#define MOVIE_ROOM_MENU_MODE_NO_HAVE_FILM   4   /* "you have no reels"    */
#define MOVIE_ROOM_MENU_MODE_FILM_SET       5   /* a reel is already in   */

#define MOVIE_ROOM_MENU_FILM_MAX  7             /* ITM_REEL1..ITM_REEL7   */

void MovieRoomMenuInit(void);                   /* 0x21fe10 */
int  MovieRoomMenuMain(void);                   /* 0x2200e8 */
void MovieRoomMenuEnd(void);                    /* 0x220a50 */
void MovieRoomMenuDisp(void);                   /* 0x220b18 */

#endif /* _INGAME_MOVIE_ROOM_MENU_H */
