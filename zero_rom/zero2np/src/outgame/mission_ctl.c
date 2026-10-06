// FILE: /home/zero_rom/zero2np/src/outgame/mission_ctl.c
//
// Mission mode's controller: the start banner, the score/time accumulators,
// and the four result screens.
//
// It is a function-pointer machine rather than a step enum -- MisSetNextFunc()
// installs the next state and resets MisAnimTime, and MisProc() calls whatever
// is installed once a frame, ticking that timer.  A state returns -1 to say
// "the mode is over"; everything else returns 0.
//
// Two independent sequences share the machine: MisStInit() runs the mission's
// opening banner and MisEnInit() the ending.  ingame code calls one or the
// other, never both.
//
// Reconstructed from the Feb 6 2004 prototype (SLES_523.84).
// Trailing /* NNN */ are ROM source line numbers, measured from
// disassemble_function.

#include "mission_ctl.h"

#include "SpriteCmn.h"                      // SpCmnTexMemLoad / ReleaseSub / Screen
#include "mission_disp.h"                   // MisDisp*
#include "mission_sel.h"                    // Mission* records
#include "../common/utility2.h"             // PRINT_ASSERT
#include "../graphics/effect/effect_oth.h"  // EffectSaeHazSetNoDrawFlg
#include "../ingame/clear/prg/clear_flg.h"  // ClearFlg_*
#include "../ingame/ingame.h"               // IngameLoopSEPause
#include "../ingame/menu/anim_2d.h"         // ALPHA_ANIM_TBL
#include "../ingame/menu/play_data.h"       // PlayData_ScoreCount
#include "../ingame/plyr/player.h"          // ReleasePlayer
#include "../system/eeiop/fileload.h"       // FileLoadIsEnd2
#include "../system/eeiop/stream_auto.h"    // StreamAutoPlay / AllStop
#include "../system/os/system.h"            // GetLanguage / GetPALMode
#include "../system/pad/pad.h"              // paddat
#include "../system/pad/vib_manage.h"       // SetVibrate

/* Per-language mission paks. */
#define MIS_START_PK2       0x1120          /* + language */
#define MIS_END_PK2         0x1125          /* + language */
#define MIS_CLEAR_CHR_PK2   0x1158          /* + language */

/* MisClearType, and the index MisEnSubAllDisp() dispatches on. */
#define MIS_TYPE_MISS       1
#define MIS_TYPE_CLEAR      2
#define MIS_TYPE_CLEAR_RANK 3

/* The screen the mission result is composited onto. */
#define MIS_SCREEN_ADDR     0x2bc0

static int (*MisFunc)(void);                                            /* sdata 3f3108 */
static int MisClearTime;                                                /* sdata 3f310c */
static int MisTotalScore;                                               /* sdata 3f3110 */
static int MisBestShot;                                                 /* sdata 3f3114 */
static int MisClearType;                                                /* sdata 3f3118 */
static int MisAnimTime;                                                 /* sdata 3f311c */
static void *MisStTexPtr;                                               /* sdata 3f3128 */
static void *MisEnTexPtr;                                               /* sdata 3f312c */
static void *MisEnClearChrTexPtr;                                       /* sdata 3f3130 */
static void *MisEnScreenPtr;                                            /* sdata 3f3134 */
static int MisDispID[2];                                                /* sbss  3f4e58 */

static void MisSetNextFunc(int (*pFunc)(void));
static int  MisStSubInit(void);
static int  MisStSubLoadWait(void);
static int  MisStSubExec(void);
static int  MisStSubEnd(void);
static void MisEnSubAllDisp(int iTime);
static void MisEnRegistData(void);
static int  MisCheckClearAllS(void);
static void MisEnReleaseTexAll(void);
static int  MisEnSubInit(void);
static int  MisEnSubLoadWait(void);
static int  MisEnSubEnd(void);
static int  MisEnSubMiss(void);
static int  MisEnSubExec(void);
static int  MisEnSubAllClear(void);
static int  MisEnSubAllClearS(void);

static void MisSetNextFunc(int (*pFunc)(void))
{
    MisFunc = pFunc;                                                    /* 89 */
    MisAnimTime = 0;                                                    /* 90 */
}

/* The timer saturates at 10000 rather than wrapping, so a state left running
 * for ever keeps answering "well past any threshold". */
int MisProc(void)
{
    int iRet;

    if (MisFunc == (int (*)(void))0)                                    /* 98 */
    {
        PRINT_ASSERT("NO_FILE_PTR");                                    /* 99 */
        iRet = -1;                                                      /* 100 */
    }
    else
    {
        iRet = (*MisFunc)();                                            /* 103 */

        if (MisAnimTime < 10000)                                        /* 104 */
        {
            MisAnimTime++;
        }
    }

    return iRet;                                                        /* 106 */
}

/* Called per shot: the running total climbs and the best single shot is kept
 * alongside it. */
void MisSetScore(int iTotalScore)
{
    if (MisBestShot < iTotalScore)                                      /* 114 */
    {
        MisBestShot = iTotalScore;                                      /* 115 */
    }

    MisTotalScore += iTotalScore;                                       /* 116 */
}

/* Which of the three accumulators the mission is scored on depends on its
 * type: 1 time, 2 total score, 3 best single shot. */
int MisGetRankLast(int iMissionID, int iTime, int iScore, int iShot)
{
    int iRank;

    (void)iTime;
    (void)iScore;
    (void)iShot;

    switch (MissionGetType(iMissionID))                                 /* 123 */
    {
    case 1:
        iRank = MissionGetRankPoint(iMissionID, MisClearTime);          /* 125 */
        break;
    case 2:
        iRank = MissionGetRankPoint(iMissionID, MisTotalScore);         /* 127 */
        break;
    case 3:
        iRank = MissionGetRankPoint(iMissionID, MisBestShot);           /* 129 */
        break;
    default:
        iRank = -1;                                                     /* 131 */
        break;
    }

    return iRank;
}

/* The clear time is rounded down to whole seconds -- 30 frames NTSC, 25 PAL.
 * Note that MisGetRankLast() is handed MisClearType as its `iTime`, which it
 * ignores; the three arguments after the id are all dead there. */
void MisSetClearType(int iType)
{
    int iMissionID;
    int iRank;
    int iFrames;
    int iCnt;

    iMissionID = MissionGetID();                                        /* 137 */

    iCnt = MisDispGetTimerCnt();                                        /* 140 */
    MisClearTime = iCnt;

    iFrames = (GetPALMode() == 0) ? 30 : 25;                            /* 144 */
    MisClearTime = iCnt - iCnt % iFrames;                               /* 146 */

    if (iType == 0)                                                     /* 151 */
    {
        MisClearType = MIS_TYPE_MISS;                                   /* 154 */
    }
    else
    {
        iRank = MisGetRankLast(iMissionID, MisClearType,
                               MisTotalScore, MisBestShot);             /* 160 */

        MisClearType = MIS_TYPE_MISS;
        if (iRank != -1)                                                /* 162 */
        {
            MisClearType = (iRank < 1) ? MIS_TYPE_CLEAR
                                       : MIS_TYPE_CLEAR_RANK;           /* 165 */
        }
    }
}

// ──────────────────────────────────────────────────────────────────────
// The mission-start banner.

static int MisStSubInit(void)
{
    MisStTexPtr = SpCmnTexMemLoad(MIS_START_PK2 + GetLanguage());       /* 192 */
    MisSetNextFunc(MisStSubLoadWait);                                   /* 193 */

    return 0;                                                           /* 194 */
}

static int MisStSubLoadWait(void)
{
    if (FileLoadIsEnd2(MIS_START_PK2 + GetLanguage(), MisStTexPtr) != 0)/* 203 */
    {
        MisSetNextFunc(MisStSubExec);                                   /* 204 */
    }

    return 0;                                                           /* 205 */
}

/* CROSS skips the banner by jumping the counter to 0x4b, which is 20 frames
 * short of the 0x5f the state ends on -- so a skip still fades out. */
static int MisStSubExec(void)
{
    int iTime;

    iTime = MisDispGetTime();                                           /* 223 */
    MisDispStart(0x80, MisStTexPtr);                                    /* 226 */

    if ((*paddat[0] == 1) && (iTime < 0x4b))                            /* 227 */
    {
        MisDispSetTime(0x4b);                                           /* 230 */
    }

    if (iTime > 0x5f)                                                   /* 231 */
    {
        MisSetNextFunc(MisStSubEnd);                                    /* 233 */
    }

    return 0;
}

static int MisStSubEnd(void)
{
    MisStTexPtr = SpCmnTexMemReleaseSub(MisStTexPtr);                   /* 210 */

    return -1;                                                          /* 211 */
}

int MisStInit(void)
{
    MisDispStartInit();                                                 /* 246 */
    StreamAutoPlay(0x9db, 0x9da, 0xc, 0, 0, 0x3200, 0,
                   (SND_3D_SET *)0);                                    /* 248 */

    MisClearTime = 0;                                                   /* 249 */
    MisTotalScore = 0;                                                  /* 253 */
    MisBestShot = 0;                                                    /* 254 */

    MisSetNextFunc(MisStSubInit);                                       /* 255 */

    if (MisStTexPtr != (void *)0)                                       /* 256 */
    {
        SpCmnTexMemReleaseSub(MisStTexPtr);                             /* 258 */
        MisStTexPtr = (void *)0;
    }

    return 0;
}

void MisStTerm(void)
{
    EffectSaeHazSetNoDrawFlg(0);                                        /* 265 */
    MisStSubEnd();                                                      /* 266 */
}

// ──────────────────────────────────────────────────────────────────────
// The mission-end sequence.  MisDispID[] holds the two result screens being
// cross-faded: [1] is the one coming in and [0] the one going out.

void MisCtlSetDisp(int iInDisp, int iOutDisp)
{
    MisDispID[1] = iInDisp;                                             /* 294 */
    MisDispID[0] = iOutDisp;                                            /* 295 */
}

/* Slot 0 fades out (128 -> 0 over frames 0..25) and slot 1 fades in, which is
 * what the two rows of sTAlpha are. */
static void MisEnSubAllDisp(int iTime)
{
    static const ALPHA_ANIM_TBL sTAlpha[2][3] =                         /* rdata 3c01f8 */
    {
        { { 128,   0,  0, 10 }, {   0,   0, 10, 25 }, { -1, -1, -1, -1 } },
        { {   0,   0,  0, 10 }, {   0, 128, 10, 25 }, { -1, -1, -1, -1 } },
    };
    u_char ucAlpha;
    int i;

    for (i = 0; i < 2; i++)                                             /* 318 */
    {
        if (MisDispID[i] < 0)                                           /* 319 */
        {
            continue;
        }

        ucAlpha = MisDispGetAnimAlpha(sTAlpha[i], iTime);               /* 320 */

        switch (MisDispID[i])                                           /* 322 */
        {
        case 0:
            MisDispBadEnd(ucAlpha, MisClearTime, MisTotalScore, MisBestShot,
                          MisEnTexPtr, MisEnClearChrTexPtr);            /* 327 */
            break;
        case 1:
            MisDispClear(ucAlpha, MisClearTime, MisTotalScore, MisBestShot,
                         MisEnTexPtr, MisEnClearChrTexPtr);             /* 332 */
            break;
        case 2:
            MisDispClearAll(ucAlpha, MisClearTime, MisTotalScore, MisBestShot,
                            MisEnTexPtr, MisEnClearChrTexPtr);          /* 337 */
            break;
        case 3:
            MisDispClearAllS(ucAlpha, MisClearTime, MisTotalScore, MisBestShot,
                             MisEnTexPtr, MisEnClearChrTexPtr);         /* 341 */
            break;
        }
    }                                                                   /* 344 */
}

void *MisGetTexPtr(void)
{
    return MisStTexPtr;
}

int MisGetScore(void)
{
    return MisTotalScore;
}

int MisGetShot(void)
{
    return MisBestShot;
}

/* MissionSetStat() answers 2 when the mission has just been beaten for the
 * first time, which is when the prize is paid out. */
static void MisEnRegistData(void)
{
    int iMissionID;

    iMissionID = MissionGetID();                                        /* 362 */

    if (MissionSetStat(iMissionID, MisClearType) == 2)                  /* 365 */
    {
        PlayData_ScoreCount(MissionGetPrize(iMissionID));               /* 367 */
    }

    MissionSetNewRecord(iMissionID, 1, MisClearTime);                   /* 371 */
    MissionSetNewRecord(iMissionID, 2, MisTotalScore);                  /* 372 */
    MissionSetNewRecord(iMissionID, 3, MisBestShot);                    /* 373 */
}

/* Both checks are "100% of the missions of this kind are done AND the flag has
 * not been raised yet" -- the ClearFlg_Check* pair answer 0 once awarded. */
int MisCheckClearAll(void)
{
    int iRet = 0;                                                       /* 380 */

    if (MissionGetTassei(5) == 100)                                     /* 383 */
    {
        iRet = (ClearFlg_CheckMissionAllClear() == 0);
    }

    return iRet;                                                        /* 386 */
}

static int MisCheckClearAllS(void)
{
    int iRet = 0;                                                       /* 392 */

    if (MissionGetTassei(1) == 100)                                     /* 395 */
    {
        iRet = (ClearFlg_CheckAllRankS_MissionClear() == 0);
    }

    return iRet;                                                        /* 398 */
}

static void MisEnReleaseTexAll(void)
{
    MisEnTexPtr = SpCmnTexMemReleaseSub(MisEnTexPtr);                   /* 405 */
    MisEnClearChrTexPtr = SpCmnTexMemReleaseSub(MisEnClearChrTexPtr);   /* 406 */
    MisEnScreenPtr = SpCmnTexMemReleaseSub(MisEnScreenPtr);             /* 407 */
}

int MisEnSubInit(void)
{
    SpCmnDrawScreen(MisEnScreenPtr, MIS_SCREEN_ADDR);                   /* 417 */

    MisEnTexPtr = SpCmnTexMemLoad(MIS_END_PK2 + GetLanguage());         /* 418 */
    MisEnClearChrTexPtr = SpCmnTexMemLoad(MIS_CLEAR_CHR_PK2 +
                                          GetLanguage());               /* 419 */

    MisSetNextFunc(MisEnSubLoadWait);                                   /* 420 */

    return 0;                                                           /* 421 */
}

/* The whole game is stopped here -- streams, vibration, the looping SE and
 * the player -- because the result screen owns the frame from now on. */
static int MisEnSubLoadWait(void)
{
    SpCmnDrawScreen(MisEnScreenPtr, MIS_SCREEN_ADDR);                   /* 428 */

    if ((FileLoadIsEnd2(MIS_END_PK2 + GetLanguage(), MisEnTexPtr) != 0) &&
        (FileLoadIsEnd2(MIS_CLEAR_CHR_PK2 + GetLanguage(),
                        MisEnClearChrTexPtr) != 0))                     /* 429 */
    {
        StreamAutoAllStop();                                            /* 432 */
        SetVibrate(0, 0, 0);                                            /* 433 */
        IngameLoopSEPause();                                            /* 434 */
        ReleasePlayer();                                                /* 435 */

        if (MisClearType < MIS_TYPE_CLEAR)                              /* 438 */
        {
            StreamAutoPlay(0x9d9, 0x9d8, 0xc, 0, 0, 0x3200, 0,
                           (SND_3D_SET *)0);                            /* 441 */
            MisCtlSetDisp(0, -1);                                       /* 443 */
            MisSetNextFunc(MisEnSubMiss);                               /* 444 */
        }
        else
        {
            StreamAutoPlay(0x9d7, 0x9d6, 0xc, 0, 0, 0x3200, 0,
                           (SND_3D_SET *)0);                            /* 448 */
            MisSetNextFunc(MisEnSubExec);                               /* 449 */
        }
    }

    return 0;                                                           /* 452 */
}

int MisEnSubEnd(void)
{
    SpCmnDrawScreen(MisEnScreenPtr, MIS_SCREEN_ADDR);                   /* 458 */
    MisEnReleaseTexAll();                                               /* 459 */

    return -1;                                                          /* 460 */
}

int MisEnSubMiss(void)
{
    SpCmnDrawScreen(MisEnScreenPtr, MIS_SCREEN_ADDR);                   /* 467 */
    MisEnSubAllDisp(MisAnimTime);                                       /* 470 */

    if ((*paddat[1] == 1) && (MisAnimTime > 0x1d))                      /* 472 */
    {
        MisSetNextFunc(MisEnSubEnd);                                    /* 473 */
    }

    return 0;                                                           /* 476 */
}

/* The clear screen is drawn directly rather than through MisEnSubAllDisp() --
 * there is nothing to cross-fade with yet. */
int MisEnSubExec(void)
{
    SpCmnDrawScreen(MisEnScreenPtr, MIS_SCREEN_ADDR);                   /* 485 */
    MisDispClear(0x80, MisClearTime, MisTotalScore, MisBestShot,
                 MisEnTexPtr, MisEnClearChrTexPtr);                     /* 490 */

    if ((*paddat[1] == 1) && (MisAnimTime > 0x3b))                      /* 492 */
    {
        MisEnRegistData();                                              /* 495 */

        if (MisCheckClearAll() != 0)                                    /* 501 */
        {
            ClearFlg_MissionAllClearExe();                              /* 512 */
            MisCtlSetDisp(2, 1);                                        /* 513 */
            MisSetNextFunc(MisEnSubAllClear);                           /* 515 */
        }
        else if (MisCheckClearAllS() != 0)                              /* 503 */
        {
            ClearFlg_AllRankS_MissionClearExe();                        /* 508 */
            MisCtlSetDisp(3, 1);                                        /* 510 */
            MisSetNextFunc(MisEnSubAllClearS);                          /* 510 */
        }
        else
        {
            MisSetNextFunc(MisEnSubEnd);                                /* 506 */
        }
    }

    return 0;                                                           /* 520 */
}

/* Both all-clear banners can be shown back to back; the rank-S one always
 * comes second. */
int MisEnSubAllClear(void)
{
    SpCmnDrawScreen(MisEnScreenPtr, MIS_SCREEN_ADDR);                   /* 527 */
    MisEnSubAllDisp(MisAnimTime);                                       /* 530 */

    if ((*paddat[1] == 1) && (MisAnimTime > 0x18))                      /* 532 */
    {
        if (MisCheckClearAllS() != 0)                                   /* 536 */
        {
            ClearFlg_AllRankS_MissionClearExe();                        /* 540 */
            MisCtlSetDisp(3, 2);                                        /* 541 */
            MisSetNextFunc(MisEnSubAllClearS);                          /* 543 */
        }
        else
        {
            MisSetNextFunc(MisEnSubEnd);                                /* 538 */
        }
    }

    return 0;                                                           /* 546 */
}

int MisEnSubAllClearS(void)
{
    SpCmnDrawScreen(MisEnScreenPtr, MIS_SCREEN_ADDR);                   /* 553 */
    MisEnSubAllDisp(MisAnimTime);                                       /* 556 */

    if ((*paddat[1] == 1) && (MisAnimTime > 0x18))                      /* 558 */
    {
        MisSetNextFunc(MisEnSubEnd);                                    /* 560 */
    }

    return 0;                                                           /* 563 */
}

int MisEnInit(void)
{
    MisDispClearInit();                                                 /* 576 */
    MisSetNextFunc(MisEnSubInit);                                       /* 578 */
    MisEnReleaseTexAll();                                               /* 581 */

    MisEnScreenPtr = SpCmnGetScreen();                                  /* 584 */

    MisDispID[0] = -1;                                                  /* 596 */
    MisDispID[1] = -1;

    return 0;
}

void MisEnTerm(void)
{
    MisEnReleaseTexAll();                                               /* 602 */
}
