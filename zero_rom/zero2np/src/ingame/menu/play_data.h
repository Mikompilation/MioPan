/* ==========================================================================
 *  ingame/menu/play_data.h
 *
 *  Play data (play_data.o): the running clock, photo scores, and the counters
 *  the results screen reports.  Also the wrapper that turns the drive's BCD
 *  real-time clock into a DATE_INFO for save-file timestamps.
 *
 *  Reconstructed from the Feb 6 2004 prototype (SLES_523.84).
 * ======================================================================== */

#ifndef _INGAME_PLAY_DATA_H
#define _INGAME_PLAY_DATA_H

#include "../../common/save_data.h"                     /* MC_SAVE_DATA */
#include "../../common/variable.h"                      /* TIME_INFO / DATE_INFO */
#include "../../sdk/libcdvd.h"                          /* sceCdCLOCK */
#include "eetypes.h"

/* Caps.  score and total_score share one; the rest differ. */
#define PLAY_DATA_SCORE_MAX      999999
#define PLAY_DATA_PHOTO_NUM_MAX   99999
#define PLAY_DATA_BUSTER_NUM_MAX   9999
#define PLAY_DATA_MAX_SCORE_MAX   99999     /* cap on the best single shot */

/* The clock rolls over rather than sticking, but PlayData_PlayTimeCount()
 * stops advancing once it reads 999:59:59. */
#define PLAY_TIME_HOUR_MAX 999
#define PLAY_TIME_MIN_MAX   59
#define PLAY_TIME_SEC_MAX   59

/* play_timer ticks per second of play time.  The function is called once a
 * displayed frame, so these are the two field rates halved. */
#define PLAY_TIME_TICK_NTSC 30
#define PLAY_TIME_TICK_PAL  25

/* types.txt.  Only play_data.c touches the instance (`static play_data` at
 * bss 4bbbd0); the layout is here because SetSave_PlayData() hands the whole
 * struct to the save system. */
typedef struct                      /* 0x20 */
{
    /* 0x00 */ TIME_INFO play_time;
    /* 0x0c */ int       score;         /* current chapter's photo score  */
    /* 0x10 */ int       total_score;   /* whole playthrough, never spent */
    /* 0x14 */ int       photo_num;
    /* 0x18 */ int       buster_num;    /* ghosts destroyed               */
    /* 0x1c */ int       max_score;     /* best single photo              */
} PLAY_DATA_CTRL;

void PlayData_Init(void);

/* Clears the clock and total_score, but leaves score/photo/buster/max alone --
 * this runs per chapter, PlayData_Init() runs per new game. */
void PlayData_PlayTimeInit(void);

/* Advance the clock.  Call once per displayed frame. */
void PlayData_PlayTimeCount(void);

/* Add to the chapter score, and to total_score when positive.  Asserts if the
 * running score ever goes negative. */
void PlayData_ScoreCount(int score);

/* Overwrite rather than accumulate; clamps into 0..PLAY_DATA_SCORE_MAX. */
void SetPlayData_Score(int score);

void PlayData_PhotoNumCount(void);
void PlayData_BusterNumCount(void);

/* Raise max_score if `score` beats it. */
void PlayData_MaxScoreUpdate(int score);

TIME_INFO GetPlayTime(void);
int  GetPlayData_Score(void);
int  GetPlayData_TotalScore(void);
int  GetPhotoNum(void);
int  GetBusterGhostNum(void);
int  GetMaxScore(void);

/* Read the drive's real-time clock into `date`. */
void GetSystemTime(DATE_INFO *date);

/* Convert a raw sceCdCLOCK (BCD fields) into a DATE_INFO (plain ints). */
void SetDateInfoType(DATE_INFO *date, sceCdCLOCK *rtc);

/* One packed BCD byte to its decimal value. */
u_int Bcd2Int(char bcd);

void SetSave_PlayData(MC_SAVE_DATA *data);
void SetSave_PlayTimer(MC_SAVE_DATA *data);
void DebugSetPlayScoreMaxNum(void);

/* AllPlyrItemInit / AllPlyrEventItemLost live in item.o -- see
 * ingame/item/prg/item.h. */
/* AllPlyrFileInit lives in file.o -- see ingame/item/prg/file.h. */
/* PlyrCrystalInit lives in crystal.o -- see ingame/item/prg/crystal.h. */
/* PlyrLevelGemInit lives in level_gem.o -- see ingame/item/prg/level_gem.h. */
/* PlyrMemoInit lives in memo.o -- see ingame/item/prg/memo.h. */
/* PlyrSoulListInit lives in soul_list.o -- see ingame/item/prg/soul_list.h. */

#endif /* _INGAME_PLAY_DATA_H */
