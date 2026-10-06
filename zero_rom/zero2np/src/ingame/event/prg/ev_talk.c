// FILE: /home/zero_rom/zero2np/src/ingame/event/prg/ev_talk.c
//
// Event dialogue: eight talk tables of up to sixteen lines each.
//
// A macro program builds a table with TalkTblInit / TalkDataAdd and then calls
// TalkExeMain() every frame until it returns non-zero.  Only one table plays
// at a time -- talk_ctrl is a single global, not one per table -- so the eight
// tables are a way of keeping several pre-built conversations around, not a
// way of running them concurrently.
//
// The two-step shape of TalkExeMain is worth keeping in mind: step 0 starts a
// line (picks which one, pulls the camera, locks the finder) and step 1 waits
// for it.  How it waits depends entirely on the line's subtitle_label:
//
//   no label (-1)  ->  stream_id stays -1, TalkDispMain() prints a message box
//                      through talk_info[tbl_id] and the player pages it
//   a label        ->  SubTitleReq() starts a voice stream, no message box is
//                      drawn at all, and the line ends when the stream ends
//                      (or when the player skips it after 30 frames)
//
// Reconstructed from the Feb 6 2004 prototype (SLES_523.84).

#include "ev_talk.h"

#include <stdio.h>                                  // printf (error banners)
#include <string.h>                                 // memset

#include "../dat/ev_talk_dat.h"                     // talk_info
#include "../../../common/utility.h"                // GetRandValI
#include "../../../common/utility2.h"               // PRINT_ASSERT
#include "../../../graphics/graph2d/message.h"      // PrintMsgDef_W / MesStatusCheck / MesSetNextPage
#include "../../../system/pad/pad.h"                // paddat
#include "../../camera/map_camera.h"                // ReqPlyrTalkCameraCtrl / EndPlyrApproachCameraCtrl
#include "../../photo/finder.h"                     // FilamentDrawLock / FilamentDrawUnlock
#include "../../plyr/player.h"                      // SetPlyrAnime / PlayerFinderLock / PlayerFinderUnlock
#include "../../subtitle/subtitle.h"                // SubTitleReq / SubTitleStop / SubTitleIsEnd

static fixed_array<TALK_TBL, TALK_TBL_MAX> talk_tbl;    /* bss 47b930 */
static TALK_EXE_CTRL                       talk_ctrl;   /* bss 47bd70 */

/* PAD_DECIDE is paddat[3] -- the confirm button.  It both pages a message box
 * and skips a voice line. */
#define PAD_DECIDE 3

/* Talk camera framing, in the units ReqPlyrTalkCameraCtrl takes. */
#define TALK_CAMERA_OFFY (-200.0f)
#define TALK_CAMERA_DIST (700.0f)

/* Frames a voice line must run before the confirm button will cut it short. */
#define TALK_PAD_WAIT_FRAMES 30

static void TalkExeCtrlInit(void);
static int  TalkExeMainSub(u_char tbl_id);
static int  TalkPadWait(void);
static int  TalkDispMain(u_char tbl_id);
static void SetTalkDataPos(u_char tbl_id);

void EvTalkInit(void)
{                                                                       /* 84 */
    int i;

    TalkExeCtrlInit();                                                  /* 89 */

    for (i = 0; i < TALK_TBL_MAX; i++)                                  /* 93 */
    {
        TalkTblInit((u_char)i);                                         /* 92 */
    }
}

/* Playback state only -- the tables themselves are left alone, which is what
 * lets TalkExeMain() clear the run state at the end of a line without losing
 * the conversation. */
static void TalkExeCtrlInit(void)
{
    talk_ctrl.talk_step         = 0;                                    /* 104 */
    talk_ctrl.stream_id         = -1;                                   /* 105 */
    talk_ctrl.data_pos          = 0;                                    /* 106 */
    talk_ctrl.pad_accept_counter = 0;                                   /* 107 */
}

void TalkTblInit(u_char tbl_id)
{                                                                       /* 115 */
    if (tbl_id >= TALK_TBL_MAX)                                         /* 119 */
    {
        /* The ROM's message names TalkDataAdd() here -- a copy-paste from the
         * function below.  Kept as-is; the assert banner carries the real
         * __FUNCTION__ anyway. */
        printf("tbl_id ERROR!! TalkDataAdd()\n");                       /* 120 */
        PRINT_ASSERT("");                                               /* 121 */
    }

    memset(&talk_tbl[tbl_id], 0, sizeof(TALK_TBL));
    talk_tbl[tbl_id].cam_flg = 1;
}

void TalkDataAdd(u_char tbl_id, int msg_id, int subtitle_label)
{                                                                       /* 137 */
    if (tbl_id >= TALK_TBL_MAX)                                         /* 142 */
    {
        PRINT_ASSERT("tbl_id Error!! %s", __FUNCTION__);                /* 143 */
    }

    /* -1 (no voice) or a label in [0, 249].  The unsigned compare is the
     * ROM's own way of accepting -1 and rejecting everything below it. */
    if ((u_int)(subtitle_label + 1) > 0xfa)                             /* 145 */
    {
        PRINT_ASSERT("subtitle label Error!! %s : subtitle_label = %d", /* 146 */
                     __FUNCTION__, subtitle_label);
    }

    if (talk_tbl[tbl_id].data_num < TALK_DATA_MAX)
    {
        talk_tbl[tbl_id].data[talk_tbl[tbl_id].data_num].msg_id = msg_id;
        talk_tbl[tbl_id].data[talk_tbl[tbl_id].data_num].subtitle_label = subtitle_label;
        talk_tbl[tbl_id].data_num++;
    }
    else
    {
        PRINT_ASSERT("MAX OVER ERROR!! %s", __FUNCTION__);              /* 158 */
    }
}

void TalkTypeChange(u_char tbl_id, int talk_type)
{                                                                       /* 169 */
    if (tbl_id >= TALK_TBL_MAX)                                         /* 172 */
    {
        PRINT_ASSERT("tbl_id ERROR!! TalkTypeChange()");                /* 173 */
    }

    if (talk_type > 1)                                                  /* 175 */
    {
        PRINT_ASSERT("talk_type ERROR!! TalkTypeChange()");             /* 176 */
    }

    talk_tbl[tbl_id].talk_type = talk_type;
}

int TalkExeMain(u_char tbl_id)
{                                                                       /* 190 */
    int res = 0;

    if (talk_tbl[tbl_id].data_num == 0)
    {
        PRINT_ASSERT("ERROR!! TalkExeMain()");                          /* 197 */
    }

    if (talk_ctrl.talk_step == 0)                                       /* 200 */
    {
        SetTalkDataPos(tbl_id);                                         /* 202 */

        if (talk_tbl[tbl_id].cam_flg != 0)
        {
            ReqPlyrTalkCameraCtrl(TALK_CAMERA_OFFY, TALK_CAMERA_DIST);  /* 206 */
        }

        /* Deciding how this line is delivered.  -1 leaves stream_id at -1 so
         * TalkDispMain() draws a message box; 0xffff is a labelled line whose
         * voice is deliberately suppressed, so it also falls back to -1 but
         * without a SubTitleReq. */
        if (talk_tbl[tbl_id].data[talk_ctrl.data_pos].subtitle_label == -1)
        {
            talk_ctrl.stream_id = talk_tbl[tbl_id].data[talk_ctrl.data_pos].subtitle_label;
        }
        else if (talk_tbl[tbl_id].data[talk_ctrl.data_pos].subtitle_label == 0xffff)
        {
            talk_ctrl.stream_id = -1;                                   /* 215 */
        }
        else
        {
            SubTitleReq(talk_tbl[tbl_id].data[talk_ctrl.data_pos].subtitle_label);
            talk_ctrl.stream_id =                                       /* 219 */
                talk_tbl[tbl_id].data[talk_ctrl.data_pos].subtitle_label;
        }

        talk_ctrl.talk_step = 1;                                        /* 222 */

        SetPlyrAnime(0, 10);                                            /* 224 */
        PlayerFinderLock();                                             /* 225 */
        FilamentDrawLock();                                             /* 229 */
    }

    if (talk_ctrl.talk_step == 1)                                       /* 232 */
    {
        if (TalkExeMainSub(tbl_id) != 0)                                /* 233 */
        {
            TalkExeCtrlInit();                                          /* 234 */

            if (talk_tbl[tbl_id].cam_flg != 0)
            {
                EndPlyrApproachCameraCtrl();                            /* 238 */
            }

            PlayerFinderUnlock();                                       /* 242 */
            FilamentDrawUnlock();                                       /* 246 */
            res = 1;
        }
    }

    return res;                                                         /* 252 */
}

/* One frame of a line in flight.  The two delivery paths never mix: a line
 * with a voice stream is never also drawn as a message box. */
static int TalkExeMainSub(u_char tbl_id)
{                                                                       /* 258 */
    int Ret = 0;                                                        /* 259 */

    if (talk_ctrl.stream_id != -1)                                      /* 261 */
    {
        if (TalkPadWait() != 0)                                         /* 263 */
        {
            SubTitleStop();                                             /* 264 */
        }

        if (SubTitleIsEnd() != 0)                                       /* 267 */
        {
            Ret = 1;                                                    /* 268 */
        }
    }
    else
    {
        Ret = (TalkDispMain(tbl_id) != 0);                              /* 273 */
    }

    return Ret;                                                         /* 278 */
}

/* A voice line cannot be skipped for its first 30 frames.  Note the counter
 * stops climbing the moment it is past the threshold -- it is a one-shot
 * delay, not a running timer. */
static int TalkPadWait(void)
{
    int RetVal = 0;

    if (talk_ctrl.pad_accept_counter > TALK_PAD_WAIT_FRAMES - 1)        /* 289 */
    {
        if (*paddat[PAD_DECIDE] == 1)                                   /* 290 */
        {
            RetVal = 1;                                                 /* 291 */
        }
    }
    else
    {
        talk_ctrl.pad_accept_counter++;                                 /* 295 */
    }

    return RetVal;                                                      /* 298 */
}

/* One frame of an unvoiced line: draw it as a message box and page it.
 * Returns non-zero when the box is done with. */
static int TalkDispMain(u_char tbl_id)
{                                                                       /* 307 */
    int res = 0;
    int msg_type;
    int msg_id;
    int msg_state;

    msg_type = talk_info[tbl_id];
    msg_id   = talk_tbl[tbl_id].data[talk_ctrl.data_pos].msg_id;

    /* -1 terminates the table: report finished without drawing anything. */
    if (msg_id == -1)                                                   /* 320 */
    {
        return 1;
    }

    PrintMsgDef_W(msg_type, msg_id);                                    /* 325 */
    msg_state = MesStatusCheck();                                       /* 327 */

    if (msg_state == 0)                                                 /* 329 */
    {
        res = 1;                                                        /* 332 */
    }
    else if (msg_state == 1)                                            /* 335 */
    {
        /* Another page waiting -- confirm turns it, but the line is not over
         * either way, so res stays 0. */
        if (*paddat[PAD_DECIDE] == 1)                                   /* 338 */
        {
            MesSetNextPage();                                           /* 339 */
        }
    }

    return res;                                                         /* 348 */
}

/* Choose which line of the table plays next and publish it in talk_ctrl. */
static void SetTalkDataPos(u_char tbl_id)
{                                                                       /* 355 */
    if (talk_tbl[tbl_id].talk_type == 0)
    {
        /* Sequential: play the line at the table's own cursor, then advance
         * it, wrapping so the table can be replayed. */
        talk_ctrl.data_pos = talk_tbl[tbl_id].data_pos;                 /* 361 */
        talk_tbl[tbl_id].data_pos++;

        if (talk_tbl[tbl_id].data_pos >= talk_tbl[tbl_id].data_num)
        {
            talk_tbl[tbl_id].data_pos = 0;                              /* 367 */
        }
    }
    else if (talk_tbl[tbl_id].talk_type == 1)
    {
        /* Random: draw over 100x the line count and divide back down, which
         * is how the ROM gets a uniform line index out of GetRandValI. */
        talk_ctrl.data_pos =                                            /* 374 */
            GetRandValI(talk_tbl[tbl_id].data_num * 100 - 1) / 100;
    }
    else
    {
        printf("ERROR!! SetTalkDataPos()\n");                           /* 376 */
    }
}

void TalkCamSet(u_char tbl_id, u_char on_off)
{                                                                       /* 388 */
    talk_tbl[tbl_id].cam_flg = on_off;
}

void SetSave_EvTalkTbl(MC_SAVE_DATA *data)
{                                                                       /* 406 */
    data->addr = (u_char *)&talk_tbl[0];
    data->size = sizeof(talk_tbl);                                      /* 410 */
}
