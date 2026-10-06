/* ==========================================================================
 *  ingame/event/prg/ev_disp.h
 *
 *  Event 2D overlays (ev_disp.c): the full-screen event image, the chapter
 *  title card, and the item-name banner.
 *
 *  The three are independent -- each has its own control block, its own
 *  disp_flg gate and its own fade state machine, and EvDispMain() ticks
 *  whichever are up.  The first two load a TIM2 of their own and free it
 *  again; the item-name banner draws through the message system and owns no
 *  memory.
 *
 *  Reconstructed from the Feb 6 2004 prototype (SLES_523.84).
 * ======================================================================== */

#ifndef _INGAME_EVENT_PRG_EV_DISP_H
#define _INGAME_EVENT_PRG_EV_DISP_H

#include "eetypes.h"
#include "../../../graphics/graph2d/g2d_draw.h"     /* SPRT_DAT */

/* Full-screen event image.  step drives the load/draw/release sequence
 * (0 request, 1 waiting, 2 drawing, 3 done) and anim_step the fade
 * (0 start, 1 fading in, 2 held, 3 fading out). */
typedef struct                      /* 0x30 */
{
    /* 0x00 */ u_char   step;
    /* 0x01 */ u_char   anim_step;
    /* 0x02 */ u_char   disp_flg;
    /* 0x03 */ u_char   win_flg;    /* also draw the common window behind it */
    /* 0x04 */ int      file_label;
    /* 0x08 */ int      fade_time;
    /* 0x0c */ int      timer;
    /* 0x10 */ SPRT_DAT sprt;
} EV_DISP2D_CTRL;

/* Chapter title card.  Same two-axis shape, but the fade timings are fixed
 * rather than caller-supplied. */
typedef struct                      /* 0x8 */
{
    /* 0x0 */ u_char step;
    /* 0x1 */ u_char chapter_num;
    /* 0x2 */ u_char disp_flg;
    /* 0x3 */ u_char anim_step;
    /* 0x4 */ int    timer;
} EV_CHAPTER_DISP;

/* Item-name banner.  No step: there is nothing to load, so anim_step alone
 * drives it and disp_flg going low is what ends it. */
typedef struct                      /* 0x14 */
{
    /* 0x00 */ u_char anim_step;
    /* 0x01 */ u_char disp_flg;
    /* 0x04 */ int    msg_type;
    /* 0x08 */ int    msg_id;
    /* 0x0c */ int    fade_time;
    /* 0x10 */ int    timer;
} EV_ITEM_NAME_DISP;

#define EV_CHAPTER_MAX 11           /* chapter_tim_file[] entries */

void EvDispInit(void);
void EvDispMain(void);

/* Non-zero once the event image's TIM2 has finished loading. */
int  CheckEvDisp2DDataLoad(void);

/* Show a full-screen event image.  base_label picks an ev_disp2d_dat[]
 * framing; x/y override that entry's position.  fade_in_time <= 0 skips the
 * fade.  win_flg also draws the common window behind the image. */
void EvDisp2DStartReq(int x, int y, int file_label, int fade_in_time, u_char win_flg, int base_label);

/* Fade the event image out over fade_out_time frames; <= 0 drops it at once. */
void EvDisp2DEndReq(int fade_out_time);

/* Free the event image and clear its state.  Safe when nothing is up. */
void EvDisp2DEndRelease(void);

/* Show the chapter title card.  It closes itself: 10 frames in, 90 held,
 * 30 out. */
void EvChapterDispStartReq(u_char chapter_num);
void EvChapterDispEndRelease(void);

/* Non-zero while the chapter card is up. */
int  EvChapterIsDisp(void);

/* Show an item-name banner: a two-line common window with the message drawn
 * centred over it. */
void EvItemNameDispStartReq(int msg_type, int msg_id, int fade_in_time);
void EvItemNameDispEndReq(int fade_out_time);

#endif /* _INGAME_EVENT_PRG_EV_DISP_H */
