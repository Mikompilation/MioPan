// FILE: /home/zero_rom/zero2np/src/outgame/mission_sel.c
//
// The mission-select screen, and the mission mode's own bookkeeping.
//
// Three things live here.  MissionTblList[25] is what each mission *is* --
// starting HP, item stock, prize and the five rank thresholds -- and
// MissionList[25][4] is what the player has done with it, the one block the
// mode saves to the card.  Around them sits the list screen: six visible rows
// out of 25, a slide-out mini menu, two yes/no windows and the hand-off into
// GID_STORY_LOAD_MISSION.
//
// The third thing is the reason the mode can be entered from a story game at
// all.  MissionSelInit() marshals the whole live game state into a heap buffer
// (MissionKeepSaveData) and MissionSelEnd() puts it back, while the camera's
// upgrade state, the spirit gauge, the mission records and the level gems are
// carried *across* the reset by the Push/Pop pair -- so a mission neither sees
// nor disturbs the story save, but the camera you earned still comes with you.
//
// Reconstructed from the Feb 6 2004 prototype (SLES_523.84).
// Trailing /* NNN */ are ROM source line numbers, measured from
// disassemble_function and symbols.txt.

#include "mission_sel.h"

#include "SpriteCmn.h"                      // SpCmnStart / BlackOut / TexMem*
#include "mis_sel_disp.h"                   // MissionDraw* / MisFade*
#include "setup.h"                          // GetSetupMsnslPk2Addr
#include "title.h"                          // SetTitleLoadFlg
#include "tim_dat/mission_dat.h"            // mission_tex[]
#include "../common/utility2.h"             // PRINT_ASSERT
#include "../common/variable.h"             // pad[]
#include "../graphics/effect/effect_oth.h"  // EffectSaeHazSetNoDrawFlg
#include "../graphics/graph2d/fade.h"       // FadeOutReq
#include "../ingame/ingame.h"               // CheckIngameMission / IngameWrkInit
#include "../ingame/item/dat/item_dat.h"    // ItemFilmEquip
#include "../ingame/item/prg/item.h"        // plyr_item
#include "../ingame/item/prg/level_gem.h"   // SetSave_PlyrLevelGem
#include "../ingame/menu/play_data.h"       // Get/SetPlayData_Score
#include "../ingame/menu/zero2_anim2d.h"    // Zero2Anim2D_InOutAnimCtrl
#include "../ingame/mission.h"              // MisDispDeleteFlg
#include "../ingame/photo/m_plyr_camera.h"  // m_plyr_camera / m_plyr_cameraSetSave*
#include "../ingame/photo/photo.h"          // InitPhotoWrk
#include "../ingame/plyr/plyr_mdl.h"        // Get/SetPlyrMdlNo / ...
#include "../main/gphase.h"                 // SetNextGPhase / GID_*
#include "../system/eeiop/cddat.h"          // BGM005_MENU2_* / MISSION_OBJ / ...
#include "../system/eeiop/fileload.h"       // FileLoadIsEnd2 / FileLoadCancel2
#include "../system/eeiop/stream_auto.h"    // StreamAutoPlay / FadeOut / AllStop
#include "../system/mc/prg/mc_load.h"       // GetMemoryCardDataSize
#include "../system/mc/prg/mc_set_data.h"   // GetDataMemoryArea / Liberate / ...
#include "../system/os/eecdvd.h"            // LoadReq
#include "../system/os/system.h"            // GetLanguage / GetPALMode / SystemBankPlay
#include "../system/pad/pad.h"              // paddat / GetPadAnalogRpt

#include <stdint.h>                         // uintptr_t
#include <stdio.h>                          // printf
#include <string.h>                         // memcpy

/* Per-language mission-select pak; the same base setup.c loads. */
#define MISSION_SEL_PK2 0x111b              /* + language */

/* Where MissionSubOK() parks the mission object pak while the screen fades. */
#define MISSION_OBJ_ADDR    0xd4ec00

/* MISSION_SEL_CTRL::step */
#define MISSION_SEL_STEP_LOAD_WAIT  0
#define MISSION_SEL_STEP_MAIN       1
#define MISSION_SEL_STEP_OUT        2
#define MISSION_SEL_STEP_MINI_ANIM  3
#define MISSION_SEL_STEP_BLACK_OUT  4
#define MISSION_SEL_STEP_MAX        5

/* MISSION_SEL_CTRL::next_phase -- what SetMissionSelNextPhase() does when the
 * fade-out is over.  There is no 1; the ROM's switch has no case for it and a
 * twelve-line gap in the source where one would go. */
#define MISSION_SEL_NEXT_SETUP      0
#define MISSION_SEL_NEXT_ALBUM      2
#define MISSION_SEL_NEXT_SAVE       3
#define MISSION_SEL_NEXT_GAME       4

/* MissionMode -- which of the four pad handlers step 1 runs. */
#define MISSION_MODE_SELECT     0
#define MISSION_MODE_MINI       1
#define MISSION_MODE_OK         2
#define MISSION_MODE_EXIT_OK    3

/* MissionWindowYesNo() answers. */
#define MISSION_YESNO_NONE      0
#define MISSION_YESNO_YES       1
#define MISSION_YESNO_NO        2
#define MISSION_YESNO_CANCEL   (-1)

/* Six rows are visible at a time, so the top of the window runs 0..19 -- the
 * last window shows missions 19..24. */
#define MISSION_ROW_NUM         6
#define MISSION_LIST_TOP_MAX    (MISSION_NUM - MISSION_ROW_NUM)

/* The mini menu slides over six frames. */
#define MISSION_MINI_STEP_MAX   6

/* Frames the screen holds black before the phase changes. */
#define MISSION_BLACK_OUT_TIME  10

/* SystemBankPlay() cue numbers, as everywhere else in outgame/. */
#define SE_CURSOR   0
#define SE_CANCEL   1
#define SE_DECIDE   3

/* The two save-state helpers move a marshalled block into a scratch buffer and
 * back; MIS_SAVE_DATA is the descriptor plus that buffer's address. */
typedef struct                      /* 0xc */
{
    /* 0x0 */ MC_SAVE_DATA aMC;
    /* 0x8 */ void        *pAddr;
} MIS_SAVE_DATA;

static void MissionKeepSaveData(void);
static void MissionResetSaveData(void);
static void MissionPushCameraDataEQ(MIS_SAVE_DATA *pSaveData);
static void MissionPushCameraData(MIS_SAVE_DATA *pSaveData);
static void MissionPushMissionData(MIS_SAVE_DATA *pSaveData);
static void MissionPushGen(MIS_SAVE_DATA *pSaveData);
static void MissionResetSaveDataKeepCam(void);
static void MissionInitData(int iMissionID, int iLevel);
static int  MissionSelTexLoadWait(void);
static void MissionSubSelMiniMenu(void);
static void MissionSubSelect(void);
static int  MissionWindowYesNo(void);
static void MissionSubOK(void);
static void MissionSubExitOK(void);
static void MissionSelOutReq(int iNextPhase);
static void SetMissionSelNextPhase(void);
static void MissionSelDispInit(void);

static void *mission_sel_tex_addr;                                      /* sdata 3f3208 */
static void *out_game_cmn_tex;                                          /* sdata 3f320c */
static int   MissionYesNo;                                              /* sdata 3f3210 */
static int   MissionListTop;                                            /* sdata 3f3214 */
static int   MissionCsrY;                                               /* sdata 3f3218 */
static int   MissionMode;                                               /* sdata 3f321c */
static int   MissionMiniYCnt;                                           /* sdata 3f3220 */
static int   MissionMiniCsr;                                            /* sdata 3f3224 */
static int   MissionBlackOutCnt;                                        /* sdata 3f3228 */
static void *MissionSaveDatPtr;                                         /* sdata 3f322c */

static MISSION_SEL_CTRL mission_sel_ctrl;                               /* sbss 3f4e60 */
static MISSION_SEL_DISP mission_sel_disp;                               /* sbss 3f4e68 */

/* The mutable half: [0] clear state, [1] best time, [2] and [3] the two
 * counts.  MissionSelSave() hands the whole 400 bytes to the card. */
static int MissionList[MISSION_NUM][MISSION_STAT_NUM];                  /* bss 4b67e0 */

/* Latched across a mission so MissionResetSaveData() can put them back. */
static int MissionMdlWork[4];                                           /* bss 4b6970 */
static int MissionGage[4];                                              /* bss 4b6980 */

/* --------------------------------------------------------------------------
 *  The mission list itself.
 *
 *  PORT NOTE: this lives in .bss with a dynamic initialiser in the ROM, not in
 *  .data, because the time missions' thresholds are MissionSetTimePal() calls
 *  -- so the whole array is built by the object's static-init helper at
 *  construction time, and the frame counts come out scaled by whatever
 *  GetPALMode() answers *then*.  Reproduced as found; the fifteen constant
 *  entries were read out of the ROM's .rodata blobs at 0x3c0710, and the ten
 *  dynamic ones out of the helper itself.
 *
 *  sType 1 is a time mission and its iRank[] ascends (rank 0 is the fastest);
 *  2 and 3 are counts and theirs descend.  That is the split
 *  MissionGetRankPoint() keys on.
 * ------------------------------------------------------------------------ */
static MISSION_TBL MissionTblList[MISSION_NUM] =                        /* bss 4b6458 */
{
    /*  0 */ { 3, 10000, 10000, 3, { 30, 0, 0, 0, 0 }, 3000,
               { 10000, 8000, 5000, 3000, 2000 } },
    /*  1 */ { 1, 5000, 10000, 0, { 10, 0, 0, 0, 0 }, 1500,
               {
                   MissionSetTimePal(0, 0, 20),
                   MissionSetTimePal(0, 0, 25),
                   MissionSetTimePal(0, 0, 30),
                   MissionSetTimePal(0, 0, 35),
                   MissionSetTimePal(0, 0, 45) } },
    /*  2 */ { 3, 10000, 10000, 1, { 15, 0, 0, 0, 0 }, 2500,
               { 8000, 5000, 4500, 3000, 1000 } },
    /*  3 */ { 2, 10000, 10000, 1, { 50, 0, 0, 5, 0 }, 3000,
               { 40000, 30000, 18000, 10000, 5000 } },
    /*  4 */ { 3, 10000, 10000, 1, { 20, 0, 0, 0, 0 }, 3000,
               { 10000, 8000, 5000, 3000, 2000 } },
    /*  5 */ { 1, 10000, 10000, 1, { 30, 0, 0, 4, 0 }, 3000,
               {
                   MissionSetTimePal(0, 0, 55),
                   MissionSetTimePal(0, 1, 30),
                   MissionSetTimePal(0, 1, 45),
                   MissionSetTimePal(0, 3, 0),
                   MissionSetTimePal(0, 5, 0) } },
    /*  6 */ { 3, 10000, 10000, 1, { 30, 10, 0, 5, 0 }, 5000,
               { 15000, 8000, 5000, 3000, 2000 } },
    /*  7 */ { 3, 10000, 10000, 1, { 40, 10, 0, 0, 0 }, 5000,
               { 15000, 8000, 6000, 3000, 1000 } },
    /*  8 */ { 1, 10000, 10000, 1, { 20, 0, 0, 3, 0 }, 0,
               {
                   MissionSetTimePal(0, 0, 40),
                   MissionSetTimePal(0, 0, 45),
                   MissionSetTimePal(0, 0, 50),
                   MissionSetTimePal(0, 1, 30),
                   MissionSetTimePal(0, 2, 0) } },
    /*  9 */ { 2, 10000, 1000, 1, { 50, 0, 0, 3, 0 }, 3000,
               { 39000, 33000, 20000, 10000, 8000 } },
    /* 10 */ { 1, 5000, 10000, 1, { 20, 0, 0, 5, 0 }, 3000,
               {
                   MissionSetTimePal(0, 3, 45),
                   MissionSetTimePal(0, 4, 0),
                   MissionSetTimePal(0, 5, 0),
                   MissionSetTimePal(0, 6, 30),
                   MissionSetTimePal(0, 7, 0) } },
    /* 11 */ { 1, 5000, 10000, 1, { 20, 0, 0, 1, 0 }, 5000,
               {
                   MissionSetTimePal(0, 0, 40),
                   MissionSetTimePal(0, 1, 0),
                   MissionSetTimePal(0, 1, 30),
                   MissionSetTimePal(0, 2, 0),
                   MissionSetTimePal(0, 3, 0) } },
    /* 12 */ { 3, 10000, 10000, 1, { 1, 0, 0, 0, 0 }, 3000,
               { 2500, 1500, 800, 600, 500 } },
    /* 13 */ { 3, 10000, 10000, 1, { 30, 0, 0, 0, 0 }, 4000,
               { 10000, 8000, 5000, 2000, 1000 } },
    /* 14 */ { 2, 10000, 10000, 1, { 30, 0, 0, 0, 0 }, 4000,
               { 23000, 20000, 10000, 8000, 5000 } },
    /* 15 */ { 3, 1000, 10000, 0, { 1, 0, 0, 0, 0 }, 5000,
               { 3500, 3300, 2500, 1200, 1000 } },
    /* 16 */ { 2, 10000, 10000, 1, { 30, 0, 0, 0, 0 }, 4000,
               { 35000, 30000, 20000, 15000, 8000 } },
    /* 17 */ { 1, 10000, 10000, 1, { 30, 0, 0, 3, 0 }, 5000,
               {
                   MissionSetTimePal(0, 1, 10),
                   MissionSetTimePal(0, 1, 45),
                   MissionSetTimePal(0, 2, 30),
                   MissionSetTimePal(0, 3, 0),
                   MissionSetTimePal(0, 4, 0) } },
    /* 18 */ { 3, 10000, 10000, 1, { 50, 0, 0, 0, 0 }, 5000,
               { 12000, 8000, 5000, 4000, 3000 } },
    /* 19 */ { 3, 10000, 1000, 1, { 30, 0, 0, 0, 0 }, 4000,
               { 10000, 8000, 4000, 3000, 2000 } },
    /* 20 */ { 1, 10000, 10000, 1, { 50, 0, 0, 3, 0 }, 8000,
               {
                   MissionSetTimePal(0, 0, 25),
                   MissionSetTimePal(0, 1, 20),
                   MissionSetTimePal(0, 2, 0),
                   MissionSetTimePal(0, 3, 0),
                   MissionSetTimePal(0, 4, 0) } },
    /* 21 */ { 1, 10000, 1000, 1, { 30, 0, 0, 1, 0 }, 0,
               {
                   MissionSetTimePal(0, 0, 55),
                   MissionSetTimePal(0, 1, 20),
                   MissionSetTimePal(0, 2, 0),
                   MissionSetTimePal(0, 3, 0),
                   MissionSetTimePal(0, 4, 0) } },
    /* 22 */ { 2, 10000, 10000, 1, { 30, 0, 0, 5, 0 }, 10,
               { 100000, 80000, 50000, 45000, 40000 } },
    /* 23 */ { 1, 10000, 10000, 1, { 30, 0, 0, 0, 0 }, 0,
               {
                   MissionSetTimePal(0, 2, 0),
                   MissionSetTimePal(0, 2, 30),
                   MissionSetTimePal(0, 3, 30),
                   MissionSetTimePal(0, 4, 0),
                   MissionSetTimePal(0, 5, 0) } },
    /* 24 */ { 1, 10000, 10000, 1, { 30, 0, 0, 0, 0 }, 0,
               {
                   MissionSetTimePal(0, 3, 30),
                   MissionSetTimePal(0, 5, 0),
                   MissionSetTimePal(0, 6, 0),
                   MissionSetTimePal(0, 7, 0),
                   MissionSetTimePal(0, 8, 0) } }
};

// ──────────────────────────────────────────────────────────────────────
// Frames <-> h/m/s.  Every stored time is a frame count, so both directions
// have to know the video mode; the rank thresholds above are built with the
// second of these at construction time.

int MissionGetTimePal(int *pHour, int *pMin, int *pSec, int iFrame)     /* 208 */
{
    int iBaseSec = (GetPALMode() != 0) ? 25 : 30;                       /* 209 */

    *pHour = iFrame / (iBaseSec * 60 * 60);                             /* 214 */
    *pMin = (iFrame % (iBaseSec * 60 * 60)) / (iBaseSec * 60);          /* 215 */
    *pSec = ((iFrame % (iBaseSec * 60 * 60)) % (iBaseSec * 60)) / iBaseSec;  /* 216 */

    return 0;                                                           /* 217 */
}

int MissionSetTimePal(int iHour, int iMin, int iSec)                    /* 222 */
{
    int iBaseSec = (GetPALMode() != 0) ? 25 : 30;                       /* 223 */

    return iHour * iBaseSec * 60 * 60 + iMin * iBaseSec * 60 + iSec * iBaseSec;  /* 228 */
}

// ──────────────────────────────────────────────────────────────────────
// Screen state.

int MissionGetYesNo(void)                                               /* 233 */
{
    return MissionYesNo;
}

int MissionGetID(void)                                                  /* 238 */
{
    int i;

    i = MissionListTop + MissionCsrY;                                   /* 239 */

    if ((u_int)i >= (u_int)MISSION_NUM)                                 /* 241 */
    {
        PRINT_ASSERT("NO Mission No[%d]", i);                           /* 242 */
        i = -1;                                                         /* 243 */
    }

    return i;                                                           /* 246 */
}

/* 1 at the top of the list, 2 at the bottom, 0 anywhere in between -- what the
 * scroll arrows are drawn from. */
int MissionCheckEnd(void)                                               /* 251 */
{
    if ((MissionCsrY == 0) && (MissionListTop == 0))                    /* 252 */
    {
        return 1;
    }

    if ((MissionCsrY == MISSION_ROW_NUM - 1) &&
        (MissionListTop == MISSION_LIST_TOP_MAX))                       /* 253 */
    {
        return 2;
    }

    return 0;                                                           /* 255 */
}

// ──────────────────────────────────────────────────────────────────────
// Per-mission fixed parameters.

int MissionGetPrize(int iMissionID)                                     /* 261 */
{
    if ((u_int)iMissionID >= (u_int)MISSION_NUM)                        /* 262 */
    {
        PRINT_ASSERT("no MissionID[%d]", iMissionID);                   /* 263 */
        return -1;                                                      /* 264 */
    }

    return MissionTblList[iMissionID].iPrize;                           /* 266, 267 */
}

int MissionGetType(int iMissionID)                                      /* 272 */
{
    if ((u_int)iMissionID >= (u_int)MISSION_NUM)                        /* 273 */
    {
        PRINT_ASSERT("no MissionID[%d]", iMissionID);                   /* 274 */
        return -1;                                                      /* 275 */
    }

    return MissionTblList[iMissionID].sType;                            /* 277, 278 */
}

/* How much of the list has been cleared to at least `iClearType`, as a
 * percentage -- 25 missions, so each one is worth 4.  Clear type 5 is folded
 * onto 4 because the rank table only goes that far. */
int MissionGetTassei(int iClearType)                                    /* 286 */
{
    int i;
    int iCnt = 0;                                                       /* 287 */
    int iRank;

    if (iClearType == 5)                                                /* 289 */
    {
        iClearType = 4;
    }

    for (i = 0; i < MISSION_NUM; i++) {                                 /* 293 */
        iRank = MissionGetRank3(i);                                     /* 294 */

        if ((iRank != -1) && (iRank <= iClearType))                     /* 296, 299 */
        {
            iCnt++;
        }
    }                                                                   /* 300 */

    /* The guard is the ROM's; iCnt * 4 is already 0 when iCnt is. */
    return (iCnt != 0) ? (iCnt * 4) : 0;                                /* 302 */
}

// ──────────────────────────────────────────────────────────────────────
// Per-mission records.

/* Raise the clear state, and report what changed: 0 nothing, 1 a better clear,
 * 2 the first clear proper (state 2 or better, from below it). */
int MissionSetStat(int iMissionID, int iStat)                           /* 309 */
{
    int *pStat;
    int  ret = 1;                                                       /* 311 */

    if ((u_int)iMissionID >= (u_int)MISSION_NUM)                        /* 313 */
    {
        PRINT_ASSERT("no MissionID[%d]", iMissionID);                   /* 314 */
        return 0;                                                       /* 315 */
    }

    pStat = &MissionList[iMissionID][0];                                /* 319 */

    if (*pStat < iStat)                                                 /* 320 */
    {
        /* Both tests read the *old* state -- the store below is in the
         * branch's delay slot, so it happens either way once we are here. */
        if ((*pStat < 2) && (1 < iStat))                                /* 322 */
        {
            ret = 2;
        }

        *pStat = iStat;

        return ret;                                                     /* 328 */
    }

    return 0;                                                           /* 329 */
}

int MissionGetScore(int iMissionID)                                     /* 335 */
{
    int iType;

    iType = MissionGetType(iMissionID);                                 /* 336 */

    if ((u_int)iType >= (u_int)MISSION_STAT_NUM)                        /* 338 */
    {
        PRINT_ASSERT("NO Score Type [%d]", iType);                      /* 339 */
        return -1;                                                      /* 340 */
    }

    return MissionList[iMissionID][iType];                              /* 342, 343 */
}

int MissionGetStat(int iMissionID, int iType)                           /* 348 */
{
    if ((u_int)iMissionID >= (u_int)MISSION_NUM)                        /* 349 */
    {
        PRINT_ASSERT("no MissionID[%d]", iMissionID);                   /* 350 */
        return -1;                                                      /* 351 */
    }

    if ((u_int)iType >= (u_int)MISSION_STAT_NUM)                        /* 353 */
    {
        PRINT_ASSERT("NO Score Type [%d]", iType);                      /* 354 */
        return -2;                                                      /* 355 */
    }

    return MissionList[iMissionID][iType];                              /* 357, 358 */
}

/* Which of the five rank bands a value falls in, or -1 for none.  A time
 * mission is graded the other way up, which is the only thing sType changes
 * here. */
int MissionGetRankPoint(int iMissionID, int iNum)                       /* 363 */
{
    int i;
    int iType;

    iType = MissionGetType(iMissionID);                                 /* 365 */

    for (i = 0; i < MISSION_RANK_NUM; i++) {                            /* 367 */
        if (iType == MISSION_TYPE_TIME)                                 /* 369 */
        {
            if (iNum <= MissionTblList[iMissionID].iRank[i])            /* 370 */
            {
                return i;
            }
        }
        else if (MissionTblList[iMissionID].iRank[i] <= iNum)           /* 373 */
        {
            return i;
        }
    }                                                                   /* 375 */

    return -1;                                                          /* 376 */
}

/* As above, but a mission that has not been cleared has no rank at all. */
int MissionGetRank(int iMissionID, int iNum)                            /* 382 */
{
    if (1 < MissionList[iMissionID][0])                                 /* 384 */
    {
        return MissionGetRankPoint(iMissionID, iNum);                   /* 388 */
    }

    return -1;                                                          /* 389 */
}

int MissionGetRank3(int iMissionID)                                     /* 394 */
{
    return MissionGetRank(iMissionID,
                          MissionList[iMissionID][MissionGetType(iMissionID)]);  /* 395, 396 */
}

/* Is `iNum` better than what is stored?  Type 1 is a time, so lower wins;
 * types 2 and 3 are counts.  Type 0 has no record and falls to the assert. */
int MissionCheckRecord(int iMissionID, int iType, int iNum)             /* 402 */
{
    if ((u_int)iMissionID >= (u_int)MISSION_NUM)                        /* 403 */
    {
        PRINT_ASSERT("no MissionID[%d]", iMissionID);                   /* 404, 405 */
        return -1;
    }

    switch (iType)                                                      /* 411 */
    {
    case MISSION_TYPE_TIME:
        return (iNum < MissionList[iMissionID][iType]);                 /* 413 */

    case MISSION_TYPE_SCORE:
    case MISSION_TYPE_SHOT:
        return (MissionList[iMissionID][iType] < iNum);                 /* 417 */

    default:
        PRINT_ASSERT("NO Score Type [%d]", iType);                      /* 419, 420 */
        break;
    }

    return -1;                                                          /* 421 */
}

void MissionSetNewRecord(int iMissionID, int iType, int iNum)           /* 426 */
{
    if (MissionCheckRecord(iMissionID, iType, iNum) == 1)               /* 428 */
    {
        MissionList[iMissionID][iType] = iNum;                          /* 429 */
    }
}

// ──────────────────────────────────────────────────────────────────────
// The records as a save block.

void MissionSelSave(MC_SAVE_DATA *data)                                 /* 436 */
{
    data->addr = (u_char *)MissionList;                                 /* 437 */
    data->size = sizeof(MissionList);                                   /* 438 */
}

/* Every mission starts uncleared, with the worst possible time and no counts. */
void MissionSelTblInit(void)                                            /* 444 */
{
    int i;

    for (i = 0; i < MISSION_NUM; i++) {                                 /* 447 */
        MissionList[i][0] = 0;                                          /* 448 */
        MissionList[i][1] = MissionSetTimePal(99, 59, 59);              /* 449 */
        MissionList[i][2] = -1;                                         /* 450 */
        MissionList[i][3] = -1;                                         /* 451 */
    }                                                                   /* 452 */
}

// ──────────────────────────────────────────────────────────────────────
// Entering and leaving a mission from a story game.
//
// MissionKeepSaveData() marshals the whole live game state into one heap
// buffer and latches the four costume model numbers and the spirit gauge
// alongside it; MissionResetSaveData() unpacks it again.  Everything the
// player should keep across the reset travels through the Push/Pop pair
// below instead.

static void MissionKeepSaveData(void)                                   /* 462 */
{
    int iSize;

    iSize = GetMemoryCardDataSize(0, 2);                                /* 470 */

    if (0xefff < iSize)                                                 /* 472 */
    {
        PRINT_ASSERT("BackUp Size Over[%x]", iSize);                    /* 473 */
    }

    MissionMdlWork[0] = GetPlyrMdlNo();                                 /* 477 */
    MissionMdlWork[1] = GetPlyrAcsNo();                                 /* 478 */
    MissionMdlWork[2] = GetSisterMdlNo();                               /* 479 */
    MissionMdlWork[3] = GetSisterAcsNo();                               /* 480 */

    m_plyr_camera.eq_tray.mSave.PushGage(MissionGage);                  /* 483 */

    MissionSaveDatPtr = GetDataMemoryArea(iSize);                       /* 486 */
    SetMemoryCardSaveDataToBuff((char*)MissionSaveDatPtr, 0, 2);               /* 489 */
}

static void MissionResetSaveData(void)                                  /* 494 */
{
    DevelopMemoryCardLoadData((char*)MissionSaveDatPtr, 0, 2);                 /* 502 */

    m_plyr_camera.eq_tray.mSave.PopGage(MissionGage);                   /* 505 */

    SetPlyrMdlNo(MissionMdlWork[0]);                                    /* 508 */
    SetPlyrAcsNo(MissionMdlWork[1]);                                    /* 509 */
    SetSisterMdlNo(MissionMdlWork[2]);                                  /* 510 */
    SetSisterAcsNo(MissionMdlWork[3]);                                  /* 511 */
}

/* The two halves of "carry this block across the reset".  Both are `inline` in
 * the ROM -- there is no out-of-line copy and no symbol, only their own source
 * lines showing up inside every caller -- so the names here are the port's. */
static inline void MissionPushSaveData(MIS_SAVE_DATA *pSaveData)
{
    pSaveData->pAddr = GetDataMemoryArea(pSaveData->aMC.size);          /* 523 */
    memcpy(pSaveData->pAddr, pSaveData->aMC.addr, pSaveData->aMC.size); /* 524 */
}

static inline void MissionPopSaveData(MIS_SAVE_DATA *pSaveData)
{
    if (pSaveData->pAddr != (void *)0)                                  /* 530 */
    {
        memcpy(pSaveData->aMC.addr, pSaveData->pAddr, pSaveData->aMC.size);  /* 531 */
        LiberateDataMemoryArea(pSaveData->pAddr);                       /* 532 */
        pSaveData->pAddr = (void *)0;                                   /* 533 */
    }
}

static void MissionPushCameraDataEQ(MIS_SAVE_DATA *pSaveData)           /* 538 */
{
    m_plyr_cameraSetSaveEQ(&pSaveData->aMC);                            /* 539 */
    MissionPushSaveData(pSaveData);
}

static void MissionPushCameraData(MIS_SAVE_DATA *pSaveData)             /* 545 */
{
    m_plyr_cameraSetSavePowrUp(&pSaveData->aMC);                        /* 546 */
    MissionPushSaveData(pSaveData);
}

static void MissionPushMissionData(MIS_SAVE_DATA *pSaveData)            /* 552 */
{
    MissionSelSave(&pSaveData->aMC);                                    /* 553 */
    MissionPushSaveData(pSaveData);
}

static void MissionPushGen(MIS_SAVE_DATA *pSaveData)                    /* 559 */
{
    SetSave_PlyrLevelGem(&pSaveData->aMC);                              /* 560 */
    MissionPushSaveData(pSaveData);
}

/* Leaving a mission: put the story game back, but keep the camera, the mission
 * records, the level gems and the running score. */
static void MissionResetSaveDataKeepCam(void)                           /* 566 */
{
    int           iScore;
    MIS_SAVE_DATA aMcCam;
    MIS_SAVE_DATA aMcCamEQ;
    MIS_SAVE_DATA aMcMission;
    MIS_SAVE_DATA aGenNum;

    MissionPushCameraDataEQ(&aMcCamEQ);                                 /* 572 */
    MissionPushCameraData(&aMcCam);                                     /* 574 */
    MissionPushMissionData(&aMcMission);                                /* 576 */
    MissionPushGen(&aGenNum);                                           /* 578 */

    iScore = GetPlayData_Score();                                       /* 581 */

    IngameWrkInit(0, 0);                                                /* 584 */
    MissionResetSaveData();                                             /* 586 */

    SetPlayData_Score(iScore);                                          /* 589 */

    MissionPopSaveData(&aMcCam);
    MissionPopSaveData(&aMcCamEQ);
    MissionPopSaveData(&aMcMission);
    MissionPopSaveData(&aGenNum);
}

/* Starting one: the same shape, but the game state is re-initialised for the
 * chosen mission rather than restored. */
static void MissionInitData(int iMissionID, int iLevel)                 /* 598 */
{
    int           iScore;
    MIS_SAVE_DATA aMcCam;
    MIS_SAVE_DATA aMcCamEQ;
    MIS_SAVE_DATA aMcMission;
    MIS_SAVE_DATA aGenNum;

    MissionPushCameraDataEQ(&aMcCamEQ);                                 /* 603 */
    MissionPushCameraData(&aMcCam);                                     /* 605 */
    MissionPushMissionData(&aMcMission);                                /* 607 */
    MissionPushGen(&aGenNum);                                           /* 609 */

    iScore = GetPlayData_Score();                                       /* 612 */

    IngameWrkInit(iMissionID, iLevel);                                  /* 615 */

    SetPlayData_Score(iScore);                                          /* 618 */

    MissionPopSaveData(&aMcCam);
    MissionPopSaveData(&aMcCamEQ);
    MissionPopSaveData(&aMcMission);
    MissionPopSaveData(&aGenNum);
}

void MissionReleaseSaveData(void)                                       /* 628 */
{
    if (MissionSaveDatPtr != (void *)0)                                 /* 629 */
    {
        LiberateDataMemoryArea(MissionSaveDatPtr);                      /* 630 */
        MissionSaveDatPtr = (void *)0;                                  /* 631 */
    }
}

// ──────────────────────────────────────────────────────────────────────
// Loading a mission's starting state.

/* One item slot: present with its count, or cleared out.  Inline in the ROM,
 * like the Push/Pop pair -- every expansion is attributed to these two lines
 * and there is no symbol of its own. */
static inline void MissionSetItemDat(int iItemID, int iNum)             /* 636 */
{                                                                       /* 637 */
    if (0 < iNum)                                                       /* 638 */
    {
        plyr_item[iItemID].item_id = iItemID;
    }
    else
    {
        plyr_item[iItemID].item_id = 0xff;
    }

    plyr_item[iItemID].have_num = iNum;
}

void MissionSetItem(int iMissionID)                                     /* 648 */
{
    int          i;
    MISSION_TBL *pTblDat;
    int          iFilList[5] = { 1, 2, 3, 4, -1 };                      /* 665 */

    pTblDat = &MissionTblList[iMissionID];                              /* 650 */

    for (i = 0; i < 58; i++) {                                          /* 653 */
        plyr_item[i].item_id = 0xff;
        plyr_item[i].have_num = 0;                                      /* 654 */
    }                                                                   /* 655 */

    MissionSetItemDat(1, pTblDat->cFilm[0]);
    MissionSetItemDat(2, pTblDat->cFilm[1]);
    MissionSetItemDat(3, pTblDat->cFilm[2]);
    MissionSetItemDat(4, pTblDat->cFilm[3]);
    MissionSetItemDat(5, pTblDat->cManyouNum);

    /* Equip the weakest film the mission actually gives you; if it gives none,
     * fall back to the unlimited Type-07. */
    for (i = 0; iFilList[i] != -1; i++) {                               /* 668 */
        if (0 < plyr_item[iFilList[i]].have_num)
        {
            printf("plyr_item[iFilList[%d]].have_num =  %d\n",
                   i, plyr_item[iFilList[i]].have_num);

            ItemFilmEquip(iFilList[i]);                                 /* 672 */
            break;                                                      /* 673 */
        }
    }                                                                   /* 674 */

    if (iFilList[i] == -1)                                              /* 675 */
    {
        ItemFilmEquip(0);                                               /* 676 */
    }
}

void MissionSetPlyrStat(int iMissionID)                                 /* 682 */
{
    MISSION_TBL *pTblDat = &MissionTblList[iMissionID];                 /* 683 */

    sis_wrk.cmn_wrk.st.hp = pTblDat->usSisHp;                           /* 685 */
    plyr_wrk.cmn_wrk.st.hp = pTblDat->usPlyrHp;                         /* 686 */
}

// ──────────────────────────────────────────────────────────────────────
// The screen.

void PlayMissionSelBGM(void)                                            /* 695 */
{
    StreamAutoAllStop();                                                /* 697 */

    mission_sel_ctrl.stream_id =
        StreamAutoPlay(BGM005_MENU2_STR, BGM005_MENU2_HXD, 0xb, 0, 1, 0x3200, 0,
                       (SND_3D_SET *)0);                                /* 701 */
}

void MissionSelInit(void)                                               /* 708 */
{
    mission_sel_ctrl.step = MISSION_SEL_STEP_LOAD_WAIT;                 /* 709 */
    mission_sel_ctrl.next_phase = MISSION_SEL_NEXT_SETUP;               /* 710 */

    MissionSelDispInit();                                               /* 713 */

    /* Only on the way in from the setup menu.  Coming back from the album or
     * the save screen the story game is already parked and the cursor should
     * stay where it was. */
    if (CheckIngameMission() == 0)                                      /* 716 */
    {
        MissionKeepSaveData();                                          /* 718 */
        InitPhotoWrk();                                                 /* 720 */

        MissionListTop = 0;                                             /* 722 */
        MissionCsrY = 0;                                                /* 723 */
    }

    mission_sel_tex_addr = GetSetupMsnslPk2Addr();                      /* 744 */

    if (out_game_cmn_tex == (void *)0)                                  /* 747 */
    {
        out_game_cmn_tex = SpCmnTexMemLoad(OUTGAME_PK2);                /* 748 */
    }

    MissionYesNo = 0;                                                   /* 751 */
    MissionMode = MISSION_MODE_SELECT;                                  /* 752 */
    MissionMiniYCnt = 0;                                                /* 753 */
    MissionMiniCsr = 0;                                                 /* 754 */
    MissionBlackOutCnt = 0;                                             /* 755 */

    /* -1 first so the caption cross-fade starts from nothing. */
    MisFadeSetMsg(-1);                                                  /* 757 */
    MisFadeSetMsg((MissionListTop + MissionCsrY) * 3 + 1);              /* 758 */

    SetIngameMission(1);                                                /* 759 */
}

void MissionSelEnd(void)                                                /* 765 */
{
    if (CheckIngameMission() == 0)                                      /* 768 */
    {
        MissionResetSaveDataKeepCam();                                  /* 771 */
        MissionReleaseSaveData();                                       /* 773 */
        InitPhotoWrk();                                                 /* 775 */

        m_plyr_camera.eq_tray.mSave.PopGage(MissionGage);               /* 777 */
    }

    out_game_cmn_tex = SpCmnTexMemReleaseSub(out_game_cmn_tex);         /* 781 */
}

static int MissionSelTexLoadWait(void)                                  /* 789 */
{
    if (FileLoadIsEnd2(MISSION_SEL_PK2 + GetLanguage(), mission_sel_tex_addr) == 0)  /* 792 */
    {
        return 0;                                                       /* 793 */
    }

    return (FileLoadIsEnd2(OUTGAME_PK2, out_game_cmn_tex) != 0);        /* 795 */
}

/* The slide-out menu: Album and Save.  SELECT closes it again. */
static void MissionSubSelMiniMenu(void)                                 /* 802 */
{
    if ((*paddat[18] == 1) || (*paddat[1] == 1))                        /* 805 */
    {
        mission_sel_ctrl.step = MISSION_SEL_STEP_MINI_ANIM;             /* 807 */
        SystemBankPlay(SE_CANCEL, 1, 0, 0, (SND_3D_SET *)0, 0x3200, 0x1000);  /* 808 */
        return;
    }

    if (((pad[0].rpt & 0x1000U) != 0) || (GetPadAnalogRpt(0) != 0))     /* 811 */
    {
        MissionMiniCsr--;                                               /* 812 */

        if (MissionMiniCsr < 0)                                         /* 813 */
        {
            MissionMiniCsr = 1;
        }

        SystemBankPlay(SE_CURSOR, 1, 0, 0, (SND_3D_SET *)0, 0x3200, 0x1000);  /* 814 */
    }
    else if (((pad[0].rpt & 0x4000U) != 0) || (GetPadAnalogRpt(1) != 0))  /* 816 */
    {
        MissionMiniCsr++;                                               /* 817 */

        if (1 < MissionMiniCsr)                                         /* 818 */
        {
            MissionMiniCsr = 0;
        }

        SystemBankPlay(SE_CURSOR, 1, 0, 0, (SND_3D_SET *)0, 0x3200, 0x1000);  /* 819 */
    }
    else if (*paddat[0] == 1)                                           /* 821 */
    {
        if (MissionMiniCsr == 0)                                        /* 822 */
        {
            MissionSelOutReq(MISSION_SEL_NEXT_ALBUM);                   /* 829 */
        }
        else if (MissionMiniCsr == 1)                                   /* 830 */
        {
            MissionSelOutReq(MISSION_SEL_NEXT_SAVE);                    /* 832 */
        }

        SystemBankPlay(SE_DECIDE, 1, 0, 0, (SND_3D_SET *)0, 0x3200, 0x1000);  /* 835 */
    }
}

/* The list itself.  UP/DOWN move one row and scroll at the ends; L1/R1 page by
 * six.  Every move that lands on a different mission re-cues the caption, and
 * MisFadeSetMsg() reporting a change is what gates the cursor SE. */
static void MissionSubSelect(void)                                      /* 842 */
{
    if (*paddat[18] == 1)                                               /* 845 */
    {
        SystemBankPlay(SE_DECIDE, 1, 0, 0, (SND_3D_SET *)0, 0x3200, 0x1000);  /* 846 */
        mission_sel_ctrl.step = MISSION_SEL_STEP_MINI_ANIM;             /* 847 */
    }
    else if (((pad[0].rpt & 0x1000U) != 0) || (GetPadAnalogRpt(0) != 0))  /* 850 */
    {
        MissionCsrY--;                                                  /* 851 */

        if (MissionCsrY < 0)                                            /* 852 */
        {
            MissionCsrY = 0;                                            /* 853 */
            MissionListTop--;                                           /* 854 */

            if (MissionListTop < 0)                                     /* 857 */
            {
                MissionListTop = 0;
            }
        }
    }
    else if (((pad[0].rpt & 0x4000U) != 0) || (GetPadAnalogRpt(1) != 0))  /* 861 */
    {
        MissionCsrY++;                                                  /* 862 */

        if (MISSION_ROW_NUM - 1 < MissionCsrY)                          /* 863 */
        {
            MissionCsrY = MISSION_ROW_NUM - 1;                          /* 864 */
            MissionListTop++;                                           /* 865 */

            if (MISSION_LIST_TOP_MAX < MissionListTop)                  /* 867, 873 */
            {
                MissionListTop = MISSION_LIST_TOP_MAX;                  /* 874 */
            }
        }
    }
    else if ((pad[0].rpt & 8U) != 0)                                    /* 871 */
    {
        MissionListTop += MISSION_ROW_NUM;                              /* 872 */

        if (MISSION_LIST_TOP_MAX < MissionListTop)                      /* 873 */
        {
            MissionListTop = MISSION_LIST_TOP_MAX;                      /* 874 */
        }
    }
    else if ((pad[0].rpt & 4U) != 0)                                    /* 877 */
    {
        MissionListTop -= MISSION_ROW_NUM;                              /* 878 */

        if (MissionListTop < 0)                                         /* 879 */
        {
            MissionListTop = 0;                                         /* 880 */
        }
    }
    else if (*paddat[0] == 1)                                           /* 884 */
    {
        MissionYesNo = 1;                                               /* 885 */
        MissionMode = MISSION_MODE_OK;                                  /* 886 */
        MisFadeSetMsg(0x55);                                            /* 888 */
        SystemBankPlay(SE_DECIDE, 1, 0, 0, (SND_3D_SET *)0, 0x3200, 0x1000);  /* 889 */
    }
    else if (*paddat[1] == 1)                                           /* 892 */
    {
        MissionYesNo = *paddat[1];                                      /* 894 */
        MissionMode = MISSION_MODE_EXIT_OK;                             /* 895 */
        MisFadeSetMsg(0x56);                                            /* 896 */
        SystemBankPlay(SE_CANCEL, 1, 0, 0, (SND_3D_SET *)0, 0x3200, 0x1000);  /* 897 */
    }

    if ((((pad[0].rpt & 0xcU) != 0) ||
         ((pad[0].rpt & 0x1000U) != 0) || (GetPadAnalogRpt(0) != 0) ||
         ((pad[0].rpt & 0x4000U) != 0) || (GetPadAnalogRpt(1) != 0)) &&
        (MisFadeSetMsg((MissionListTop + MissionCsrY) * 3 + 1) != 0))   /* 902, 903 */
    {
        SystemBankPlay(SE_CURSOR, 1, 0, 0, (SND_3D_SET *)0, 0x3200, 0x1000);  /* 905 */
    }
}

/* Shared by both confirmation windows.  0 while it is still open, 1 yes,
 * 2 no, -1 cancelled with TRIANGLE. */
static int MissionWindowYesNo(void)                                     /* 913 */
{
    /* One andi of 0xa000: GCC merged the LEFT and RIGHT tests on the same
     * 16-bit field. */
    if (((pad[0].one & 0xa000U) != 0) ||
        (GetPadAnalogRpt(3) != 0) || (GetPadAnalogRpt(2) != 0))         /* 915 */
    {
        MissionYesNo ^= 1;                                              /* 919 */
        SystemBankPlay(SE_CURSOR, 1, 0, 0, (SND_3D_SET *)0, 0x3200, 0x1000);  /* 920 */
    }

    if (*paddat[0] == 1)                                                /* 923 */
    {
        SystemBankPlay(SE_DECIDE, 1, 0, 0, (SND_3D_SET *)0, 0x3200, 0x1000);  /* 924 */

        if (MissionYesNo == 0)                                          /* 926 */
        {
            return MISSION_YESNO_YES;                                   /* 928 */
        }

        return MISSION_YESNO_NO;
    }

    if (*paddat[1] == 1)                                                /* 931 */
    {
        SystemBankPlay(SE_CANCEL, 1, 0, 0, (SND_3D_SET *)0, 0x3200, 0x1000);  /* 932 */
        return MISSION_YESNO_CANCEL;                                    /* 933 */
    }

    return MISSION_YESNO_NONE;                                          /* 937 */
}

/* "Start this mission?"  Yes drops the two event object paks and pulls in the
 * mission one over the same address, then blacks the screen out while it
 * loads. */
static void MissionSubOK(void)                                          /* 942 */
{
    int iRet;

    iRet = MissionWindowYesNo();                                        /* 944 */

    if (iRet == MISSION_YESNO_YES)
    {
        mission_sel_ctrl.step = MISSION_SEL_STEP_BLACK_OUT;             /* 946 */

        FileLoadCancel2(EVENT_OBJ, (void *)MISSION_OBJ_ADDR, (FILE_LOAD_CALLBACK)0, (void *)0);     /* 948 */
        FileLoadCancel2(EVENT_50_OBJ, (void *)MISSION_OBJ_ADDR, (FILE_LOAD_CALLBACK)0, (void *)0);  /* 950 */
        LoadReq(MISSION_OBJ, MISSION_OBJ_ADDR);                         /* 951 */

        MissionBlackOutCnt = 0;                                         /* 953 */
    }
    else if ((iRet == MISSION_YESNO_NO) || (iRet == MISSION_YESNO_CANCEL))
    {
        MissionMode = MISSION_MODE_SELECT;                              /* 956 */
        MisFadeSetMsg((MissionListTop + MissionCsrY) * 3 + 1);          /* 957, 958 */
    }
}

/* "Leave mission mode?" */
static void MissionSubExitOK(void)                                      /* 965 */
{
    int iRet;

    iRet = MissionWindowYesNo();                                        /* 967 */

    if (iRet == MISSION_YESNO_YES)
    {
        MissionSelOutReq(MISSION_SEL_NEXT_SETUP);                       /* 969 */
    }
    else if ((iRet == MISSION_YESNO_NO) || (iRet == MISSION_YESNO_CANCEL))
    {
        MissionMode = MISSION_MODE_SELECT;                              /* 973 */
        MisFadeSetMsg((MissionListTop + MissionCsrY) * 3 + 1);          /* 974, 975 */
    }
}

void MissionSelMain(void)                                               /* 981 */
{
    /* Note the case order: the ROM writes 3 before 2. */
    switch (mission_sel_ctrl.step)                                      /* 987 */
    {
    case MISSION_SEL_STEP_LOAD_WAIT:
        if (MissionSelTexLoadWait() != 0)                               /* 989 */
        {
            mission_sel_ctrl.step = MISSION_SEL_STEP_MAIN;              /* 993 */
        }
        break;

    case MISSION_SEL_STEP_MAIN:
        switch (MissionMode)                                            /* 995 */
        {
        case MISSION_MODE_SELECT:
            MissionSubSelect();                                         /* 997 */
            break;

        case MISSION_MODE_MINI:
            MissionSubSelMiniMenu();                                    /* 1000 */
            break;

        case MISSION_MODE_OK:
            MissionSubOK();                                             /* 1003 */
            break;

        case MISSION_MODE_EXIT_OK:
            MissionSubExitOK();                                         /* 1006 */
            break;
        }
        break;

    case MISSION_SEL_STEP_MINI_ANIM:
        if (MissionMode == MISSION_MODE_MINI)                           /* 1012 */
        {
            MissionMiniYCnt--;
        }
        else
        {
            MissionMiniYCnt++;
        }

        /* One unsigned compare for "off either end of the slide". */
        if ((u_int)(MissionMiniYCnt - 1) >= (u_int)(MISSION_MINI_STEP_MAX - 1))  /* 1013 */
        {
            mission_sel_ctrl.step = MISSION_SEL_STEP_MAIN;              /* 1015 */

            if (MissionMode == MISSION_MODE_SELECT)                     /* 1017 */
            {
                MissionMode = MISSION_MODE_MINI;                        /* 1022 */
            }
            else if (MissionMode == MISSION_MODE_MINI)
            {
                MissionMode = MISSION_MODE_SELECT;
            }
        }
        break;

    case MISSION_SEL_STEP_OUT:
        if (mission_sel_disp.anim_step == 4)                            /* 1028 */
        {
            SetMissionSelNextPhase();                                   /* 1030 */
        }
        break;

    case MISSION_SEL_STEP_BLACK_OUT:
        if (MissionBlackOutCnt < MISSION_BLACK_OUT_TIME)                /* 1035 */
        {
            MissionBlackOutCnt++;                                       /* 1036, 1037 */
        }
        else if (FileLoadIsEnd2(MISSION_OBJ, (void *)MISSION_OBJ_ADDR) != 0)  /* 1039 */
        {
            MissionSelOutReq(MISSION_SEL_NEXT_GAME);                    /* 1041 */
        }
        break;

    default:
        PRINT_ASSERT("Error! %s", __FUNCTION__);                        /* 1045 */
        break;
    }
}

static void MissionSelOutReq(int iNextPhase)                            /* 1054 */
{
    /* The BGM only stops on the two exits that leave the mode; the album and
     * the save screen come back to this one. */
    if ((iNextPhase == MISSION_SEL_NEXT_SETUP) ||
        (iNextPhase == MISSION_SEL_NEXT_GAME))                          /* 1058 */
    {
        StreamAutoFadeOut(mission_sel_ctrl.stream_id, 5);               /* 1062 */
    }

    mission_sel_ctrl.next_phase = (char)iNextPhase;                     /* 1066 */
    mission_sel_ctrl.step = MISSION_SEL_STEP_OUT;                       /* 1067 */
    mission_sel_disp.anim_step = 3;                                     /* 1068 */
    mission_sel_disp.anim_timer = 0;                                    /* 1069 */
}

static void SetMissionSelNextPhase(void)                                /* 1077 */
{
    int iMissionID;

    /* There is no case 1 -- the compare tree has no test for it and the source
     * has a twelve-line gap where one would sit. */
    switch (mission_sel_ctrl.next_phase)                                /* 1082 */
    {
    case MISSION_SEL_NEXT_SETUP:
        SetNextGPhase(GID_TITLE_SETUPMENU);                             /* 1085 */
        SetIngameMission(0);                                            /* 1086 */
        StreamAutoAllStop();                                            /* 1087 */
        break;

    case MISSION_SEL_NEXT_ALBUM:
        SetNextGPhase(GID_MISSION_ALBUM);                               /* 1100 */
        break;

    case MISSION_SEL_NEXT_SAVE:
        /* The save screen writes the story game's block, so the story state
         * has to be back in place before it runs. */
        MissionResetSaveDataKeepCam();                                  /* 1104 */
        m_plyr_camera.eq_tray.mSave.PopGage(MissionGage);               /* 1106 */
        SetNextGPhase(GID_MISSION_SAVE);                                /* 1107 */
        break;

    case MISSION_SEL_NEXT_GAME:
        iMissionID = MissionListTop + MissionCsrY;                      /* 1110 */

        MissionInitData(iMissionID, 1);                                 /* 1113 */
        MissionSetItem(iMissionID);                                     /* 1115 */
        MissionSetPlyrStat(iMissionID);                                 /* 1117 */

        EffectSaeHazSetNoDrawFlg(1);                                    /* 1119 */
        FadeOutReq(0, 0, 0, 0);                                         /* 1121 */
        MisDispDeleteFlg(3);                                            /* 1123 */

        m_plyr_camera.eq_tray.mSave.ResetGage();                        /* 1125 */

        SetTitleLoadFlg(0);                                             /* 1127 */
        SetNextGPhase(GID_STORY_LOAD_MISSION);                          /* 1128 */
        break;

    default:
        PRINT_ASSERT("Error! %s", __FUNCTION__);                        /* 1132 */
        break;
    }
}

static void MissionSelDispInit(void)                                    /* 1144 */
{
    mission_sel_disp.anim_step = 0;                                     /* 1147 */
    mission_sel_disp.anim_timer = 0;                                    /* 1148 */
}

void MissionSelDisp(void)                                               /* 1154 */
{
    if ((mission_sel_ctrl.step < MISSION_SEL_STEP_MAX) &&
        (0 < mission_sel_ctrl.step))                                    /* 1160 */
    {
        u_char ucAlpha = Zero2Anim2D_InOutAnimCtrl(&mission_sel_disp.anim_step,
                                                   &mission_sel_disp.anim_timer,
                                                   10, 5);              /* 1166 */

        SpCmnStart(mission_tex);                                        /* 1168 */

        MissionDrawSelect(mission_sel_tex_addr, MissionListTop, MissionCsrY, ucAlpha);  /* 1171 */
        MissionDrawMiniMenu(out_game_cmn_tex, mission_sel_tex_addr, MissionCsrY,
                            ucAlpha, (float)MissionMiniYCnt / 6.0f, MissionMiniCsr,
                            (MissionMode != MISSION_MODE_MINI));        /* 1174, 1176, 1179 */

        MisFadeProc(ucAlpha);                                           /* 1182 */
    }

    if (0 < MissionBlackOutCnt)                                         /* 1191 */
    {
        SpCmnBlackOut((u_char)(MissionBlackOutCnt * 128 / MISSION_BLACK_OUT_TIME));  /* 1192, 1195 */
    }
}
