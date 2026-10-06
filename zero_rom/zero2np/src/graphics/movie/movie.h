/* ==========================================================================
 *  graphics/movie/movie.h
 *
 *  The PSS movie player's game-side wrapper (movie.o, .text
 *  0x21eba8..0x21f158).  It owns the buffers the decoder needs, opens the
 *  stream, pumps playPss once a frame and blits the decoded image; the decoder
 *  itself is playpss.a (system/playpss/).
 *
 *  Every caller uses the *WithTitle trio, which pairs the player with
 *  movie_title.c's captions.  The bare InitMovie/PlayMovie/EndMovie exist for
 *  a movie with no caption track and have no reconstructed caller.
 *
 *  Reconstructed from the Feb 6 2004 prototype (SLES_523.84).
 * ======================================================================== */

#ifndef _GRAPHICS_MOVIE_MOVIE_H
#define _GRAPHICS_MOVIE_MOVIE_H

#ifdef __cplusplus
extern "C" {
#endif

/* Claim `size` bytes of IOP memory or hang.  Exported, but movie.c is its only
 * caller. */
int  iopalloc(unsigned int size, char *desc);                       /* 0x21eb18 */

/* Claim the decoder's buffers and start streaming file `no`.  `vol_percentage`
 * scales the BGM group volume for this movie; `audio_flg` 0 plays it silent
 * and skips every audio allocation. */
void InitMovie(int no, int vol_percentage, int audio_flg);          /* 0x21eba8 */

/* Stop the stream where it is -- the same thing START/CROSS does. */
void MovieCancel(void);                                             /* 0x21edf8 */

/* One frame.  Returns 0 while the movie is still running and non-zero once it
 * has finished (1 end of stream or decoder error, 2 a requested stop has
 * completed).  Every caller treats "non-zero" as "tear it down". */
int  PlayMovie(void);                                               /* 0x21ee20 */

void EndMovie(void);                                                /* 0x21ef90 */

void InitMovieWithTitle(int scene_no, int audio_flg);               /* 0x21f068 */
int  PlayMovieWithTitle(void);                                      /* 0x21f0b8 */
void EndMovieWithTitle(void);                                       /* 0x21f108 */

/* The movie's frame counter, halved in PAL so the scene-effect script and the
 * caption table can both be authored against NTSC frames.  -1 before the first
 * frame and after EndMovie(). */
int  MovieCountGet(void);                                           /* 0x21f130 */

#ifdef __cplusplus
}
#endif

#endif /* _GRAPHICS_MOVIE_MOVIE_H */
