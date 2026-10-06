/* ==========================================================================
 *  ingame/event/prg/ev_talk.h
 *
 *  Event dialogue (ev_talk.c): eight independently addressable talk tables,
 *  each a queue of up to sixteen lines.
 *
 *  A macro program fills a table -- TalkTblInit() to clear it, TalkDataAdd()
 *  per line, TalkTypeChange() / TalkCamSet() for the two options -- and then
 *  calls TalkExeMain() every frame until it returns non-zero.  Only one table
 *  can be playing at a time: the run state (talk_ctrl) is a single global, not
 *  one per table.
 *
 *  A line is a (msg_id, subtitle_label) pair and the label decides how it is
 *  delivered.  With no label the line is printed as an on-screen message box
 *  the player pages through; with one it is a voice stream and the message box
 *  is skipped entirely, the line ending when the stream does.
 *
 *  Reconstructed from the Feb 6 2004 prototype (SLES_523.84).
 * ======================================================================== */

#ifndef _INGAME_EVENT_PRG_EV_TALK_H
#define _INGAME_EVENT_PRG_EV_TALK_H

#include "eetypes.h"
#include "../../../common/save_data.h"                  /* MC_SAVE_DATA */
#include "../../../graphics/graph3d/ctl/fixed_array.h"  /* fixed_array */

/* One line of dialogue.  msg_id == -1 terminates playback of the table;
 * subtitle_label == -1 means "no voice stream, print a message box instead". */
typedef struct                      /* 0x8 */
{
    /* 0x0 */ int msg_id;
    /* 0x4 */ int subtitle_label;
} TALK_DATA;

#define TALK_DATA_MAX 16

/* One talk table.  data_pos is the table's own cursor and only advances for
 * talk_type 0; talk_type 1 picks at random and leaves it alone. */
typedef struct                      /* 0x88 */
{
    /* 0x00 */ char                        data_num;    /* lines filled in    */
    /* 0x01 */ char                        data_pos;    /* sequential cursor  */
    /* 0x02 */ u_char                      cam_flg;     /* pull the talk camera */
    /* 0x04 */ int                         talk_type;   /* 0 sequential, 1 random */
    /* 0x08 */ fixed_array<TALK_DATA, TALK_DATA_MAX> data;
} TALK_TBL;

#define TALK_TBL_MAX 8

/* Playback state of whichever table is currently running.  stream_id == -1
 * means the line is being shown as a message box rather than spoken. */
typedef struct                      /* 0x10 */
{
    /* 0x0 */ u_char talk_step;             /* 0 = start a line, 1 = running  */
    /* 0x4 */ int    stream_id;
    /* 0x8 */ int    data_pos;              /* line being played             */
    /* 0xc */ int    pad_accept_counter;    /* frames before skip is allowed */
} TALK_EXE_CTRL;

void EvTalkInit(void);

/* Clear table tbl_id back to empty, camera flag on. */
void TalkTblInit(u_char tbl_id);

/* Append one line.  subtitle_label must be -1 (no voice) or in [0, 249]. */
void TalkDataAdd(u_char tbl_id, int msg_id, int subtitle_label);

/* 0 = play the table's lines in order, 1 = pick one at random. */
void TalkTypeChange(u_char tbl_id, int talk_type);

/* Run one frame of table tbl_id.  Non-zero once the line has finished. */
int TalkExeMain(u_char tbl_id);

/* Whether starting the table pulls the talk camera in (default on). */
void TalkCamSet(u_char tbl_id, u_char on_off);

/* The whole table array goes into the save block verbatim. */
void SetSave_EvTalkTbl(MC_SAVE_DATA *data);

#endif /* _INGAME_EVENT_PRG_EV_TALK_H */
