/* ==========================================================================
 *  outgame/mission_sel.h
 *
 *  The mission-select screen (mission_sel.o): the 25-mission list, the record
 *  each mission keeps, and the save/restore that lets a mission run on top of
 *  a story game without disturbing it.
 *
 *  Reconstructed from the Feb 6 2004 prototype (SLES_523.84).
 * ======================================================================== */

#ifndef _OUTGAME_MISSION_SEL_H
#define _OUTGAME_MISSION_SEL_H

#include "../common/save_data.h"    /* MC_SAVE_DATA */

/* MISSION_TBL::sType -- which MissionList[] slot scores this mission, and so
 * which way MissionGetRankPoint() compares against iRank[].  Type 1 is a time
 * (lower is better and iRank[] ascends); 2 and 3 are counts (higher is better
 * and iRank[] descends).  Nothing in the build uses type 0. */
#define MISSION_TYPE_TIME   1
#define MISSION_TYPE_SCORE  2
#define MISSION_TYPE_SHOT   3

#define MISSION_NUM         25      /* missions in the list        */
#define MISSION_STAT_NUM    4       /* MissionList[] slots each    */
#define MISSION_RANK_NUM    5       /* rank thresholds each        */

/* One mission's fixed parameters (rodata/bss 4b6458).  MissionList[][] holds
 * the mutable half. */
typedef struct                      /* 0x24 */
{
    /* 0x00 */ short  sType;                    /* MISSION_TYPE_*            */
    /* 0x02 */ u_short usPlyrHp;                /* Mio's starting HP         */
    /* 0x04 */ u_short usSisHp;                 /* Mayu's starting HP        */
    /* 0x06 */ char   cManyouNum;               /* stock of the herbal medicine */
    /* 0x07 */ char   cFilm[5];                 /* stock of each film type   */
    /* 0x0c */ int    iPrize;                   /* points the clear is worth */
    /* 0x10 */ int    iRank[MISSION_RANK_NUM];  /* S..D thresholds           */
} MISSION_TBL;

/* mission_sel.o's two work blocks (sbss 3f4e60 / 3f4e68). */
typedef struct                      /* 0x8 */
{
    /* 0x0 */ char step;            /* MISSION_SEL_STEP_*                    */
    /* 0x1 */ char next_phase;      /* MISSION_SEL_NEXT_*                    */
    /* 0x4 */ int  stream_id;       /* the mission-mode BGM                  */
} MISSION_SEL_CTRL;

typedef struct                      /* 0x2 */
{
    /* 0x0 */ char anim_step;
    /* 0x1 */ char anim_timer;
} MISSION_SEL_DISP;

/* Frame counts <-> h/m/s, scaled by the video mode.  Both are used to build
 * the rank thresholds, so a PAL disc gets 50 frames a second's worth. */
int  MissionGetTimePal(int *pHour, int *pMin, int *pSec, int iFrame); /* 0x215ed8 */
int  MissionSetTimePal(int iHour, int iMin, int iSec);                /* 0x215f88 */

/* Screen state read by the drawing half and by mission_ctl.c. */
int  MissionGetYesNo(void);                 /* 0x216008 */
int  MissionGetID(void);                    /* 0x216010 */
int  MissionCheckEnd(void);                 /* 0x216078 */

/* Per-mission fixed parameters. */
int  MissionGetPrize(int iMissionID);       /* 0x2160b8 */
int  MissionGetType(int iMissionID);        /* 0x216130 */

/* Percentage-of-25 achievement figure for a clear grade. */
int  MissionGetTassei(int iClearType);      /* 0x2161a8 */

/* Per-mission records.  Slot 0 is the clear state, 1 the best time, 2 and 3
 * the two counts. */
int  MissionSetStat(int iMissionID, int iStat);                 /* 0x216230 */
int  MissionGetScore(int iMissionID);                           /* 0x2162d8 */
int  MissionGetStat(int iMissionID, int iType);                 /* 0x216368 */
int  MissionGetRankPoint(int iMissionID, int iNum);             /* 0x216430 */
int  MissionGetRank(int iMissionID, int iNum);                  /* 0x2164c8 */
int  MissionGetRank3(int iMissionID);                           /* 0x216508 */
int  MissionCheckRecord(int iMissionID, int iType, int iNum);   /* 0x216548 */
void MissionSetNewRecord(int iMissionID, int iType, int iNum);  /* 0x216660 */

/* The records are one save block; newgame.c clears them. */
void MissionSelSave(MC_SAVE_DATA *data);    /* 0x2166c0 */
void MissionSelTblInit(void);               /* 0x2166d8 */

/* Give back the buffer MissionSelInit() parked the story game in. */
void MissionReleaseSaveData(void);          /* 0x216bc0 */

/* Load one mission's item stock and starting HP into the live game state. */
void MissionSetItem(int iMissionID);        /* 0x216bf0 */
void MissionSetPlyrStat(int iMissionID);    /* 0x216ef8 */

/* The mode's own BGM; init_Title_Mission() starts it. */
void PlayMissionSelBGM(void);               /* 0x216f30 */

void MissionSelInit(void);                  /* 0x216f78 */
void MissionSelEnd(void);                   /* 0x217028 */
void MissionSelMain(void);                  /* 0x217738 */
void MissionSelDisp(void);                  /* 0x217af8 */

#endif /* _OUTGAME_MISSION_SEL_H */
