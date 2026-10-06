/* ==========================================================================
 *  graphics/movie/movie_title.h
 *
 *  Scene movie-title interface (movie_title.o, .text 0x221840..0x221eb8).
 *
 *  The movie captions: while a PSS cutscene plays, MovieTitleMain() is called
 *  once a frame with the movie's frame counter, looks the scene up in
 *  movie_title_dat[], finds the (start_frame, end_frame) window the counter
 *  is inside, and draws that window's message.
 *
 *  MovieTitleDispMain() is the drawing half and is shared: subtitle.c calls it
 *  directly for spoken lines in the running game, with a different message
 *  bank and its own base-plate flag.
 *
 *  Reconstructed from the Feb 6 2004 prototype (SLES_523.84).
 * ======================================================================== */

#ifndef _GRAPHICS_MOVIE_MOVIE_TITLE_H
#define _GRAPHICS_MOVIE_MOVIE_TITLE_H

#include "eetypes.h"

#ifdef __cplusplus
extern "C" {
#endif

/* One caption: message `msg_id` is on screen for movie frames
 * [start_frame, end_frame).  A list is terminated by { -1, -1, -1 }. */
typedef struct                      /* 0xc */
{
    /* 0x0 */ int msg_id;
    /* 0x4 */ int start_frame;
    /* 0x8 */ int end_frame;
} MOVIE_TITLE_DAT;

/* sbss 3f4e98 */
typedef struct                      /* 0x4 */
{
    /* 0x0 */ int scene_no;
} MOVIE_TITLE_CTRL;

/* The highest scene number movie_title_dat[] has a list for.  MovieTitleInit()
 * asserts above this; slot 71 is the NULL tail. */
#define MOVIE_TITLE_SCENE_MAX       0x46

/* Message bank the movie captions come out of, and where they sit. */
#define MOVIE_TITLE_MSG_TYPE        0x4d
#define MOVIE_TITLE_MSG_MAX         0x22d
#define MOVIE_TITLE_DISP_Y          0x17c
#define MOVIE_TITLE_MSG_COL         0xc

void MovieTitleInit(int scene_no);                                  /* 0x221918 */
void MovieTitleMain(int movie_timer);                               /* 0x221978 */
int  GetMovieTitleDatTblPos(MOVIE_TITLE_DAT *data, int timer);      /* 0x221a50 */

/* Draws message `msg_id` from bank `msg_type` centred on x = 320, one line at
 * a time downwards from `y`, in colour `msg_col`.  `base_disp_flg` puts the
 * three-piece plate (movie_title_base_tex[]) behind each line.
 *
 * Nothing is drawn unless the message is one the player has asked to always
 * see: opt_wrk.credits, or an id listed in every_disp_subtitles[]. */
void MovieTitleDispMain(int msg_type, int msg_id, int y, int msg_col,
                        char base_disp_flg);                        /* 0x221b50 */
void MovieTitleBaseDisp(float x, float y, float w, u_char alp, u_int pri);
                                                                    /* 0x221d18 */
void MovieTitleEnd(void);                                           /* 0x221eb0 */

#ifdef __cplusplus
}
#endif

#endif /* _GRAPHICS_MOVIE_MOVIE_TITLE_H */
