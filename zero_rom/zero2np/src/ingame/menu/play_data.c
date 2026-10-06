// FILE: /home/zero_rom/zero2np/src/ingame/menu/play_data.c
//
// Play data: the running clock plus the five counters the results screen
// reports.  All of it lives in one 0x20 struct that goes to the memory card
// whole; play_timer is the sub-second frame counter and saves separately.
//
// Two scores are tracked.  `score` is the chapter's, and the caller may set it
// outright; `total_score` only ever grows, and only from positive deltas, so
// it survives whatever the chapter score does.
//
// Reconstructed from the Feb 6 2004 prototype (SLES_523.84).

#include "play_data.h"

#include "../../common/utility2.h"      /* PRINT_ASSERT */
#include "../../system/os/system.h"     /* GetPALMode */

#include <string.h>                     /* memset */

static void PlayData_TotalScoreCount(int score);

static PLAY_DATA_CTRL play_data;                                    /* bss 4bbbd0 */
static u_char         play_timer;                                   /* sbss 3f4ee0 */

/* --------------------------------------------------------------------------
 *  Init
 * ------------------------------------------------------------------------ */
void PlayData_Init(void)
{                                                                       /* 46 */
    memset(&play_data, 0, sizeof(play_data));                           /* 51 */
    play_timer = 0;
}

/* Per chapter rather than per game: wipes the clock (the first 0xc bytes) and
 * total_score, and deliberately leaves score, photo_num, buster_num and
 * max_score standing. */
void PlayData_PlayTimeInit(void)
{                                                                       /* 64 */
    memset(&play_data, 0, sizeof(TIME_INFO));                           /* 67 */
    play_timer            = 0;                                          /* 68 */

    play_data.total_score = 0;                                          /* 71 */
}

/* --------------------------------------------------------------------------
 *  PlayData_PlayTimeCount
 *
 *  One displayed frame.  Once the clock reads 999:59:59 it stops entirely --
 *  the tick is skipped and play_timer just keeps getting cleared.
 * ------------------------------------------------------------------------ */
void PlayData_PlayTimeCount(void)
{                                                                       /* 80 */
    play_timer++;                                                       /* 84 */

    if ((play_data.play_time.hour == PLAY_TIME_HOUR_MAX) &&
        (play_data.play_time.min  == PLAY_TIME_MIN_MAX)  &&
        (play_data.play_time.sec  == PLAY_TIME_SEC_MAX))                /* 88 */
    {
        play_timer = 0;                                                 /* 92 */
        return;
    }

    /* The ROM has a switch here (line 96) whose non-zero cases -- 99, 106,
     * 113 and 119 -- all carry the same test and were cross-jumped into one
     * block, so their labels are not recoverable.  What it emits is exactly
     * the two-way test below, since GetPALMode() only ever answers 0 or 1. */
    if (GetPALMode() == 0)                                              /* 96 */
    {
        if (play_timer < PLAY_TIME_TICK_NTSC)                           /* 128 */
        {
            return;
        }
    }
    else
    {
        if (play_timer < PLAY_TIME_TICK_PAL)                            /* 119 */
        {
            return;
        }
    }

    play_data.play_time.sec++;                                          /* 129 */

    play_timer = 0;                                                     /* 132 */

    if (PLAY_TIME_SEC_MAX < play_data.play_time.sec)                    /* 135 */
    {
        play_data.play_time.min++;                                      /* 136 */

        play_data.play_time.sec = 0;                                    /* 139 */

        if (PLAY_TIME_MIN_MAX < play_data.play_time.min)                /* 142 */
        {
            play_data.play_time.hour++;                                 /* 143 */

            play_data.play_time.min = 0;                                /* 145 */

            if (PLAY_TIME_HOUR_MAX < play_data.play_time.hour)          /* 148 */
            {
                play_data.play_time.hour = 0;                           /* 149 */
            }
        }
    }
}

/* --------------------------------------------------------------------------
 *  Scores
 * ------------------------------------------------------------------------ */
void PlayData_ScoreCount(int score)
{                                                                       /* 171 */
    play_data.score += score;                                           /* 174 */

    if (PLAY_DATA_SCORE_MAX <= play_data.score)                         /* 175 */
    {
        play_data.score = PLAY_DATA_SCORE_MAX;                          /* 176 */
    }

    /* A penalty still comes off the chapter score but never off the total. */
    if (0 < score)                                                      /* 180 */
    {
        PlayData_TotalScoreCount(score);                                /* 181 */
    }

    if (play_data.score < 0)                                            /* 186 */
    {
        PRINT_ASSERT("Error! %s", __FUNCTION__);                        /* 187 */
    }
}

static void PlayData_TotalScoreCount(int score)
{                                                                       /* 197 */
    if (0 < score)                                                      /* 200 */
    {
        play_data.total_score += score;                                 /* 201 */

        if (PLAY_DATA_SCORE_MAX <= play_data.total_score)               /* 202 */
        {
            play_data.total_score = PLAY_DATA_SCORE_MAX;                /* 203 */
        }
    }

    if (play_data.total_score < 0)                                      /* 209 */
    {
        PRINT_ASSERT("Error! %s", __FUNCTION__);                        /* 210 */
    }
}

/* Unlike PlayData_ScoreCount() this floors at 0 instead of asserting, and
 * never touches total_score. */
void SetPlayData_Score(int score)
{                                                                       /* 220 */
    play_data.score = score;                                            /* 223 */

    if (PLAY_DATA_SCORE_MAX <= score)                                   /* 224 */
    {
        play_data.score = PLAY_DATA_SCORE_MAX;                          /* 225 */
    }

    if (play_data.score < 0)                                            /* 227 */
    {
        play_data.score = 0;                                            /* 228 */
    }
}

/* --------------------------------------------------------------------------
 *  Counters
 * ------------------------------------------------------------------------ */
void PlayData_PhotoNumCount(void)
{                                                                       /* 238 */
    play_data.photo_num++;                                              /* 241 */

    if (PLAY_DATA_PHOTO_NUM_MAX < play_data.photo_num)                  /* 243 */
    {
        play_data.photo_num = PLAY_DATA_PHOTO_NUM_MAX;                  /* 244 */
    }
}

void PlayData_BusterNumCount(void)
{                                                                       /* 254 */
    play_data.buster_num++;                                             /* 257 */

    if (PLAY_DATA_BUSTER_NUM_MAX < play_data.buster_num)                /* 259 */
    {
        play_data.buster_num = PLAY_DATA_BUSTER_NUM_MAX;                /* 260 */
    }
}

/* The clamp sits inside the "is it a new best" arm, so a score above the cap
 * is stored and then trimmed rather than rejected. */
void PlayData_MaxScoreUpdate(int score)
{                                                                       /* 271 */
    if (play_data.max_score < score)                                    /* 276 */
    {
        play_data.max_score = score;

        if (PLAY_DATA_MAX_SCORE_MAX < score)                            /* 280 */
        {
            play_data.max_score = PLAY_DATA_MAX_SCORE_MAX;              /* 281 */
        }
    }
}

/* --------------------------------------------------------------------------
 *  Accessors
 * ------------------------------------------------------------------------ */
TIME_INFO GetPlayTime(void)
{                                                                       /* 297 */
    return play_data.play_time;                                         /* 301 */
}                                                                       /* 302 */

int GetPlayData_Score(void)
{                                                                       /* 308 */
    return play_data.score;                                             /* 312 */
}

int GetPlayData_TotalScore(void)
{                                                                       /* 319 */
    return play_data.total_score;                                       /* 323 */
}

int GetPhotoNum(void)
{                                                                       /* 330 */
    return play_data.photo_num;                                         /* 334 */
}

int GetBusterGhostNum(void)
{                                                                       /* 341 */
    return play_data.buster_num;                                        /* 345 */
}

int GetMaxScore(void)
{                                                                       /* 352 */
    return play_data.max_score;                                         /* 356 */
}

/* --------------------------------------------------------------------------
 *  Real-time clock
 * ------------------------------------------------------------------------ */
void GetSystemTime(DATE_INFO *date)
{                                                                       /* 367 */
    sceCdCLOCK rtc;

    sceCdReadClock(&rtc);                                               /* 372 */

    SetDateInfoType(date, &rtc);                                        /* 377 */
}

/* The RTC hands back two-digit BCD, so `year` is an offset from 2000 and stays
 * that way -- nothing here widens it. */
void SetDateInfoType(DATE_INFO *date, sceCdCLOCK *rtc)
{                                                                       /* 395 */
    date->day.year   = Bcd2Int(rtc->year);                              /* 399 */
    date->day.month  = Bcd2Int(rtc->month);                             /* 400 */
    date->day.day    = Bcd2Int(rtc->day);                               /* 401 */
    date->time.hour  = Bcd2Int(rtc->hour);                              /* 402 */
    date->time.min   = Bcd2Int(rtc->minute);                            /* 403 */
    date->time.sec   = Bcd2Int(rtc->second);                            /* 404 */
}

u_int Bcd2Int(char bcd)
{                                                                       /* 413 */
    u_int work;

    work = ((u_char)bcd >> 4) * 10;                                     /* 418 */

    return work + (bcd & 0x0f);                                         /* 421 */
}

/* --------------------------------------------------------------------------
 *  Save / debug
 * ------------------------------------------------------------------------ */
void SetSave_PlayData(MC_SAVE_DATA *data)
{                                                                       /* 432 */
    data->addr = (u_char *)&play_data;                                  /* 436 */
    data->size = sizeof(play_data);                                     /* 437 */
}

void SetSave_PlayTimer(MC_SAVE_DATA *data)
{                                                                       /* 445 */
    data->addr = &play_timer;                                           /* 449 */
    data->size = sizeof(play_timer);                                    /* 450 */
}

void DebugSetPlayScoreMaxNum(void)
{                                                                       /* 461 */
    play_data.score       = PLAY_DATA_SCORE_MAX;                        /* 464 */
    play_data.total_score = PLAY_DATA_SCORE_MAX;                        /* 465 */
    play_data.photo_num   = PLAY_DATA_PHOTO_NUM_MAX;                    /* 466 */
    play_data.buster_num  = PLAY_DATA_BUSTER_NUM_MAX;                   /* 467 */
    play_data.max_score   = PLAY_DATA_MAX_SCORE_MAX;                    /* 468 */
}
