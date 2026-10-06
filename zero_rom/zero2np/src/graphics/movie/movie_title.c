// FILE: /home/zero_rom/zero2np/src/graphics/movie/movie_title.c
//
// Movie captions.  Two jobs in one file:
//
//   - the movie half (MovieTitleInit / Main / End).  InitMovieWithTitle()
//     latches the scene number here, and PlayMovieWithTitle() calls
//     MovieTitleMain() once a frame with the movie's own frame counter.  The
//     counter is looked up in that scene's movie_title_dat[] list and whatever
//     caption window it is inside gets drawn.
//
//   - the drawing half (MovieTitleDispMain / MovieTitleBaseDisp), which
//     subtitle.c also calls directly for spoken lines during play.  It centres
//     the message on x = 320, draws it a line at a time downwards, and puts a
//     three-piece plate behind each line.
//
// A caption is only drawn if subtitles are on (opt_wrk.credits, the option
// screen's row 3, ON by default) or the message id is in
// every_disp_subtitles[] -- ids 486..517, the block of lines that are shown
// even with subtitles turned off.
//
// Reconstructed from the Feb 6 2004 prototype (SLES_523.84).
// .text 0x221840..0x221eb8; all six ZERO2.MAP exports plus the one static.

#include "movie_title.h"

#include "movie_title_dat.h"                    // movie_title_dat[], base tex
#include "../../common/utility2.h"              // PRINT_ASSERT
#include "../../common/variable.h"              // opt_wrk
#include "../../system/os/system.h"             // GetPALMode
#include "../graph2d/g2d_draw.h"                // DISP_SPRT, CopySprDToSpr
#include "../graph2d/message.h"                 // GetMsgDataAddr / line print

#include <stddef.h>                             // NULL

/* Screen centre the caption is laid out around, and the floor on the plate.
 *
 * A line narrower than the floor is not centred on itself: the plate is pinned
 * at 204 wide and 218 across, so short lines all get the same plate in the same
 * place instead of one that jitters with the text.  The two constants are
 * consistent -- 218 is exactly where a 146-pixel line's plate would start
 * (320 - 146/2 - 29), and 146 + 29 + 29 is 204 -- so the clamp is seamless
 * rather than a jump. */
#define MOVIE_TITLE_CENTER_X        320.0f
#define MOVIE_TITLE_BASE_MIN_W      204.0f
#define MOVIE_TITLE_BASE_MIN_X      218.0f

/* sbss 3f4e98 */
static MOVIE_TITLE_CTRL movie_title_ctrl;

/* PORT: whether GetMovieTitleDatTblPos() applies the ROM's PAL 5/6 frame
 * conversion.  Zero here -- see the note at the comparison itself. */
static const int kMovieTitlePalFrameScale = 0;

static MOVIE_TITLE_DAT *GetMovieTitleDat(int scene_no);

/* --------------------------------------------------------------------------
 *  MovieTitleInit  (0x221918)
 *
 *  Latch the scene whose caption list MovieTitleMain() will walk.  The assert
 *  does not stop the store -- an out-of-range number is reported and then used
 *  anyway, which is the ROM's own order.
 * ------------------------------------------------------------------------ */
void MovieTitleInit(int scene_no)                                       /* 84 */
{
    if (scene_no > MOVIE_TITLE_SCENE_MAX)                               /* 88 */
    {
        PRINT_ASSERT("Error! MovieTitleInit scene_no %d", scene_no);     /* 89 */
    }

    movie_title_ctrl.scene_no = scene_no;                               /* 93 */
}

/* --------------------------------------------------------------------------
 *  MovieTitleMain  (0x221978)
 *
 *  One frame of the movie's captions.  `movie_timer` is the movie frame
 *  counter, already halved by PlayMovieWithTitle() in PAL.
 * ------------------------------------------------------------------------ */
void MovieTitleMain(int movie_timer)                                    /* 113 */
{
    MOVIE_TITLE_DAT *data;
    int              tbl_pos;

    data = GetMovieTitleDat(movie_title_ctrl.scene_no);                 /* 118 */

    tbl_pos = GetMovieTitleDatTblPos(data, movie_timer);                /* 123 */

    if (tbl_pos != -1)                                                  /* 125 */
    {
        if (data[tbl_pos].msg_id > MOVIE_TITLE_MSG_MAX)                 /* 128 */
        {
            PRINT_ASSERT("Error! MovieTitleMain scene_no %d msg_id %d",  /* 129 */
                         movie_title_ctrl.scene_no, data[tbl_pos].msg_id);
        }

        MovieTitleDispMain(MOVIE_TITLE_MSG_TYPE, data[tbl_pos].msg_id,  /* 135 */
                           MOVIE_TITLE_DISP_Y, MOVIE_TITLE_MSG_COL, 1);
    }
}

/* --------------------------------------------------------------------------
 *  GetMovieTitleDat  (0x221a38, static)
 *
 *  No bounds check of its own; MovieTitleInit() is where the scene number is
 *  vetted.
 * ------------------------------------------------------------------------ */
static MOVIE_TITLE_DAT *GetMovieTitleDat(int scene_no)                  /* 155 */
{
    return movie_title_dat[scene_no];                                   /* 159 */
}

/* --------------------------------------------------------------------------
 *  GetMovieTitleDatTblPos  (0x221a50)
 *
 *  Index of the caption whose window contains `timer`, or -1.
 *
 *  The frames in the table are authored for NTSC, so in PAL both ends are
 *  scaled by 5/6 -- the same conversion subtitle.c does on its own counters.
 *  The two arms are written out in full rather than scaling `timer`, which is
 *  why the multiply and divide appear twice per entry.
 * ------------------------------------------------------------------------ */
int GetMovieTitleDatTblPos(MOVIE_TITLE_DAT *data, int timer)            /* 169 */
{
    int i;
    int tbl_pos;

    tbl_pos = -1;                                                       /* 174 */

    for (i = 0; data[i].msg_id != -1; i++)                              /* 178 */
    {                                                                   /* 179 */
        /* PORT: the PAL conversion is skipped, and the NTSC arm runs for both
         * regions.
         *
         * The 5/6 exists because `timer` is a count of console frames.  A PAL
         * console runs the movie loop at 50 Hz where an NTSC one runs it at
         * 60, so a table authored in NTSC frames has to be scaled down to be
         * compared against a PAL counter.
         *
         * The host has no such difference: the movie loop runs at 60 Hz
         * whatever region the disc is, so PlayMovieWithTitle()'s
         * `iMovieCnt >> 1` already advances at ~30/s -- which is the unit the
         * table is authored in.  Measured: scene 0's last caption ends at
         * frame 4282 and its movie is 154.4 s long (29 643 776 bytes of 48 kHz
         * stereo), so the table's own rate is 4282 / 142.9 s = 29.97 fps, the
         * film rate.  Applying the 5/6 as well made every caption fire 1.2x
         * early and drift further in the further the movie ran.
         *
         * Kept as a runtime constant rather than #if'd out so the ROM's arm
         * stays compiled and correct for the day the loop is region-paced. */
        if (kMovieTitlePalFrameScale && GetPALMode() != 0)               /* 184 */
        {
            if (data[i].start_frame * 5 / 6 <= timer &&                 /* 185 */
                timer < data[i].end_frame * 5 / 6)
            {
                tbl_pos = i;                                            /* 187 */
                break;
            }
        }
        else
        {
            if (data[i].start_frame <= timer &&                         /* 191 */
                timer < data[i].end_frame)
            {
                tbl_pos = i;
                break;
            }
        }
    }

    return tbl_pos;                                                     /* 206 */
}

/* --------------------------------------------------------------------------
 *  MovieTitleDispMain  (0x221b50)
 *
 *  Draw one message, wrapped a line at a time downwards from `y`.
 *  GetMsgLineLength() both measures the current line and hands back where the
 *  next one starts, so the loop is driven by that pointer going NULL.
 *
 *  The plate is sized from the line, but never below MOVIE_TITLE_BASE_MIN_W --
 *  and when it is clamped so is its x, so every short line gets the same plate
 *  in the same place instead of a plate that jitters with the text width.
 * ------------------------------------------------------------------------ */
void MovieTitleDispMain(int msg_type, int msg_id, int y, int msg_col,   /* 223 */
                        char base_disp_flg)
{
    int      i;
    u_char   disp_flg;
    float    msg_x;
    float    msg_length;
    float    base_x;
    float    base_length;
    u_char  *p_now_line_adrs;
    u_char  *p_next_line_adrs;

    p_now_line_adrs = GetMsgDataAddr(msg_type, msg_id);                 /* 235 */

    disp_flg = 0;

    if (opt_wrk.credits == 1)                                           /* 247 */
    {
        /* Cross-jumped with the store at 260 -- one `li a3,1` serves both. */
        disp_flg = 1;
    }
    else                                                                /* 249 */
    {
        for (i = 0; every_disp_subtitles[i] != -1; i++)                 /* 254 */
        {
            if (every_disp_subtitles[i] == msg_id)                      /* 259 */
            {
                disp_flg = 1;                                           /* 260 */
                break;                                                  /* 261 */
            }
        }
    }

    if (disp_flg == 1)                                                  /* 269 */
    {
        do                                                              /* 270 */
        {
            msg_length = (float)GetMsgLineLength(p_now_line_adrs,       /* 272 */
                                                 &p_next_line_adrs);

            msg_x       = MOVIE_TITLE_CENTER_X - msg_length * 0.5f;     /* 274 */

            base_x      = msg_x - (float)movie_title_base_tex[0].w;     /* 277 */
            base_length = msg_length + (float)(movie_title_base_tex[0].w /* 279 */
                                             + movie_title_base_tex[2].w);

            if (base_length < MOVIE_TITLE_BASE_MIN_W)                   /* 282 */
            {
                base_x      = MOVIE_TITLE_BASE_MIN_X;                   /* 283 */
                base_length = MOVIE_TITLE_BASE_MIN_W;                   /* 285 */
            }

            if (base_disp_flg == 1)                                     /* 288 */
            {
                MovieTitleBaseDisp(base_x, (float)(y - 2),              /* 290 */
                                   base_length, 0x80, 0);
            }

            PrintMsg_ArrangeOneLine(p_now_line_adrs, 320, y, msg_col,   /* 295 */
                                    0x80, 0, 0, 0, 2);

            /* base_tex[0].h is the plate's height, and doubles as the line
             * pitch -- the lines stack exactly as tall as their plates. */
            y = y + movie_title_base_tex[0].h;                          /* 297 */

            p_now_line_adrs = p_next_line_adrs;                         /* 299 */

        } while (p_now_line_adrs != NULL);                              /* 300 */
    }
}

/* --------------------------------------------------------------------------
 *  MovieTitleBaseDisp  (0x221d18)
 *
 *  The plate behind one caption line: left cap, right cap, then the middle
 *  scaled horizontally to bridge them.  `w` is the total width the caps and
 *  the middle have to cover.
 *
 *  The middle is drawn last and is the only piece that scales; its scale
 *  centre is the seam behind the left cap, so it grows to the right.
 * ------------------------------------------------------------------------ */
void MovieTitleBaseDisp(float x, float y, float w, u_char alp, u_int pri) /* 314 */
{
    DISP_SPRT ds;
    float     line_w;
    float     line_scr;

    if (w < MOVIE_TITLE_BASE_MIN_W)                                     /* 321 */
    {
        w = MOVIE_TITLE_BASE_MIN_W;                                     /* 323 */
    }

    /* What is left for the middle once both caps have taken their width, and
     * the horizontal scale that stretches one middle tile across it. */
    line_w   = w - (float)(movie_title_base_tex[0].w                    /* 327 */
                         + movie_title_base_tex[2].w);
    line_scr = line_w / (float)movie_title_base_tex[1].w;               /* 329 */

    CopySprDToSpr(&ds, &movie_title_base_tex[0]);                       /* 333 */
    ds.pri = pri; ds.z = 0xfffff - (pri & 0xfffff);                     /* 334 */
    ds.alpha = ds.alpha * alp >> 7;                                     /* 335 */
    ds.x = x; ds.y = y;                                                 /* 336 */
    DispSprD(&ds);                                                      /* 337 */

    CopySprDToSpr(&ds, &movie_title_base_tex[2]);                       /* 339 */
    ds.pri = pri; ds.z = 0xfffff - (pri & 0xfffff);                     /* 340 */
    ds.alpha = ds.alpha * alp >> 7;                                     /* 341 */
    ds.x = x + line_w + (float)movie_title_base_tex[0].w; ds.y = y;     /* 342 */
    DispSprD(&ds);                                                      /* 343 */

    CopySprDToSpr(&ds, &movie_title_base_tex[1]);                       /* 345 */
    ds.pri = pri; ds.z = 0xfffff - (pri & 0xfffff);                     /* 346 */
    ds.alpha = ds.alpha * alp >> 7;                                     /* 347 */
    ds.x = x + (float)movie_title_base_tex[0].w; ds.y = y;              /* 348 */
    ds.csx = ds.x; ds.csy = ds.y; ds.scw = line_scr; ds.sch = 1.0f;     /* 349 */
    DispSprD(&ds);                                                      /* 350 */
}

/* --------------------------------------------------------------------------
 *  MovieTitleEnd  (0x221eb0)
 *
 *  Eight bytes: `jr ra` and a bare nop.  The scene number is left where it is;
 *  MovieTitleInit() overwrites it on the next movie and nothing reads it in
 *  between.  Lines 364..370 hold no code.
 * ------------------------------------------------------------------------ */
void MovieTitleEnd(void)                                                /* 363 */
{
}                                                                       /* 371 */
