// FILE: /home/zero_rom/zero2np/src/outgame/mission_disp.c
//
// The mission-mode HUD and the four result screens.
//
// Everything here draws out of `mission_tex[]` through the SpriteCmn label
// helpers, and everything animates off one counter: MisDispTimer, which each
// *Init() zeroes and each draw advances.  The start banner scales two title
// plates down onto the screen against sTScale/sTAlpha; the clear screen walks
// iDatList[] to bring its four rows in one after another, each on the same
// fade curve offset by ten frames.
//
// The in-mission readout is separate and runs off MisDispTimerCnt, which is a
// real frame count -- MisDispTimeProc() advances it every frame the mission is
// live, and what it draws depends on how the mission is scored: the elapsed
// time for a MISSION_TYPE_TIME, the running score or shot count otherwise.
//
// Reconstructed from the Feb 6 2004 prototype (SLES_523.84).
// Trailing /* NNN */ are ROM source line numbers, measured from
// disassemble_function and symbols.txt; they mark statements, not source
// lines, where GCC cross-jumped or wrapped a call.

#include "mission_disp.h"

#include "SpriteCmn.h"                      // SpCmnStart / Draw* / Print* / BlackOut
#include "mis_sel_disp.h"                   // MissionDrawTime
#include "mission_ctl.h"                    // MisGetScore / MisGetShot / MisGetRankLast
#include "mission_sel.h"                    // MissionGet* / MissionSetTimePal
#include "tim_dat/mission_dat.h"            // mission_tex[]
#include "../graphics/graph2d/draw_cmn.h"   // DrawCmnCapGroup_W
#include "../graphics/graph2d/message.h"    // PrintMsg
#include "../graphics/graph2d/tim2.h"       // PK2SendVram
#include "../ingame/ingame.h"               // CheckIngameMission
#include "../ingame/menu/anim_2d.h"         // Anim2D_CalcNowAlpha / _CalcNowScale

#include <stdint.h>                         // uintptr_t

/* Sprite labels in mission_tex[]. */
#define MISDISP_NUM_BASE        0x8e        /* the ten digits            */
#define MISDISP_TITLE_PLATE     0x5e        /* start banner, two layers  */
#define MISDISP_RANK_BASE       0x7f        /* + MisGetRankLast()        */

/* Digit pitch, in pixels. */
#define MISDISP_NUM_PITCH       18

/* The result rows come in ten frames apart and each fade lasts fifteen. */
#define MISDISP_ROW_FADE_TIME   15

/* Both counters stop here rather than wrapping. */
#define MISDISP_TIMER_MAX       1000

static int MisDispTimer;                                                /* sdata 3f3178 */
static int MisDispTimerCnt;                                             /* sdata 3f317c */
static int MisDispTimerSw;                                              /* sdata 3f3180 */

void MisDispSetTime(int iTime)                                          /* 56 */
{
    MisDispTimer = iTime;
}

int MisDispGetTime(void)                                                /* 59 */
{
    return MisDispTimer;
}

/* Right-aligned, so the cursor starts at the last column and walks back.  Note
 * both `iNum` and `iOffX` are the ROM's own parameters used as working
 * variables -- there is no local for either. */
void MisDispNum(int iNum, int iKeta, int iOffX, int iOffY,
                u_char ucAlpha, int iFlg)                               /* 63 */
{
    int i;

    iOffX += iKeta * MISDISP_NUM_PITCH - MISDISP_NUM_PITCH;             /* 66 */

    for (i = 0; i < iKeta; i++) {                                       /* 67 */
        SpCmnDrawSprite(iNum % 10 + MISDISP_NUM_BASE, iOffX, iOffY, ucAlpha, 0);  /* 68 */

        iOffX -= MISDISP_NUM_PITCH;                                     /* 69 */
        iNum /= 10;                                                     /* 70 */

        if (iFlg == 0) {                                                /* 71 */
            if (iNum == 0) {                                            /* 72 */
                break;
            }
        }
    }                                                                   /* 74 */
}

void MisDispTime(int iHour, int iMin, int iSec, int iOffX, int iOffY,
                 u_char ucAlpha)                                        /* 80 */
{
    MisDispNum(iHour, 2, iOffX + 0x15a, iOffY + 0x81, ucAlpha, 1);      /* 81 */
    MisDispNum(iMin, 2, iOffX + 0x18f, iOffY + 0x81, ucAlpha, 1);       /* 82 */
    MisDispNum(iSec, 2, iOffX + 0x1c4, iOffY + 0x81, ucAlpha, 1);       /* 83 */
}

/* Anim2D_CalcNowAlpha() with both ends held: before the first segment starts
 * the table's opening alpha, after the last one ends its closing alpha.  The
 * terminator is a start_alpha of -1, not a start_time of -1. */
u_char MisDispGetAnimAlpha(const ALPHA_ANIM_TBL *pAnimList, int iTime)  /* 88 */
{
    int i;

    if (iTime <= pAnimList[0].start_time)                               /* 92 */
    {
        return (u_char)pAnimList[0].start_alpha;                        /* 93 */
    }

    for (i = 0; pAnimList[i].start_alpha != -1; i++) {                  /* 97 */
    }

    i--;                                                                /* 98 */

    if (pAnimList[i].end_time <= iTime)                                 /* 99 */
    {
        return (u_char)pAnimList[i].end_alpha;                          /* 100 */
    }

    return Anim2D_CalcNowAlpha(pAnimList, iTime);                       /* 103 */
}

// ──────────────────────────────────────────────────────────────────────
// The in-mission readout.

void MisDispTimeInit(void)                                              /* 112 */
{
    MisDispTimerCnt = 0;
}

int MisDispGetTimerCnt(void)                                            /* 115 */
{
    return MisDispTimerCnt;
}

void MisDispSetFlg(int iFlg)                                            /* 118 */
{
    MisDispTimerSw |= iFlg;
}

void MisDispDeleteFlg(int iFlg)                                         /* 121 */
{
    MisDispTimerSw &= ~iFlg;
}

/* What the corner of the screen shows while a mission runs, and the only place
 * MisDispTimerCnt advances -- so it counts frames the readout was enabled for,
 * not frames the mission was live for. */
void MisDispTimeProc(void)                                              /* 125 */
{
    int iHour;
    int iMin;
    int iSec;

    if ((CheckIngameMission() != 0) &&                                  /* 130 */
        ((MisDispTimerSw & MISDISP_FLG_TIMER) != 0))                    /* 132 */
    {
        switch (MissionGetType(MissionGetID()))                         /* 135 */
        {
        case MISSION_TYPE_TIME:
            MissionGetTimePal(&iHour, &iMin, &iSec, MisDispTimerCnt);   /* 138 */

            MissionDrawTime(iHour, iMin, iSec, 0x1a6, 400, 0x80);       /* 140 */
            break;                                                      /* 141 */

        case MISSION_TYPE_SCORE:
            SpCmnPrintNumber_NK(MisGetScore(), 6, 0x19c, 400, 0x15, 0x80, 0, 0, 0);  /* 145 */

            SpCmnPrintMsg_K(0x3c, 0x5b, 0x208, 400, 0x15, 0x80, 0xa0);  /* 149 */
            break;                                                      /* 151 */

        case MISSION_TYPE_SHOT:
            SpCmnPrintNumber_NK(MisGetShot(), 6, 0x19c, 400, 0x15, 0x80, 0, 0, 0);   /* 155 */

            SpCmnPrintMsg_K(0x3c, 0x5b, 0x208, 400, 0x15, 0x80, 0xa0);  /* 159 */
            break;
        }

        /* 99:59:59 is the same ceiling MissionSelTblInit() seeds a record
         * with, so a timer that runs out parks on "no time set". */
        if (MisDispTimerCnt < MissionSetTimePal(99, 59, 59))            /* 165 */
        {
            MisDispTimerCnt++;
        }
    }
}                                                                       /* 166 */

// ──────────────────────────────────────────────────────────────────────
// The mission-start banner.

void MisDispStartInit(void)                                             /* 173 */
{
    MisDispTimer = 0;
}

/* Two copies of the title plate scaled down onto the screen over the first
 * fifteen frames and back up over the last ten, against a half-black mask.
 * The two rows of sTScale are the two layers -- the back one comes from 3x and
 * the front from 1.5x -- and the offset keeps the growth centred. */
void MisDispStart(u_char ucAlpha, void *pTexPtr)                        /* 177 */
{
    int    i;
    u_char ucLAlpha;
    int    iMsgID;
    float  fScale;
    float  fX;
    float  fY;

    static const ALPHA_ANIM_TBL sTAlpha[5] =                            /* rdata 3c0280 */
    {
        {   0,  64,  0, 15 },
        {  64, 128, 15, 30 },
        { 128, 128, 30, 75 },
        { 128,   0, 75, 85 },
        {  -1,  -1, -1, -1 }
    };
    static const ALPHA_ANIM_TBL sTAlpha2[5] =                           /* rdata 3c02a8 */
    {
        {   0,   0,  0, 15 },
        {   0, 128, 15, 30 },
        { 128, 128, 30, 75 },
        { 128,   0, 75, 95 },
        {  -1,  -1, -1, -1 }
    };
    static const ALPHA_ANIM_TBL sMskAlpha[3] =                          /* rdata 3c02d0 */
    {
        {  64,  64,  0, 75 },
        {  64,   0, 75, 95 },
        {  -1,  -1, -1, -1 }
    };
    static const SCL_ANIM_TBL sTScale[2][5] =                           /* rdata 3c02e8 */
    {
        {
            { 3.0f, 1.0f,  0, 15 },
            { 1.0f, 1.0f, 15, 75 },
            { 1.0f, 2.0f, 75, 85 },
            { -1.0f, -1.0f, -1, -1 },
            { 0.0f, 0.0f, 0, 0 }
        },
        {
            { 1.5f, 1.0f,  0, 15 },
            { 1.0f, 1.0f, 15, 75 },
            { 1.0f, 2.0f, 75, 85 },
            { -1.0f, -1.0f, -1, -1 },
            { 0.0f, 0.0f, 0, 0 }
        }
    };

    SpCmnStart(mission_tex);                                            /* 213 */
    PK2SendVram((uintptr_t)(uintptr_t)pTexPtr, -1, -1, 0);                  /* 214 */

    ucLAlpha = Anim2D_CalcNowAlpha(sMskAlpha, MisDispTimer);            /* 217 */
    SpCmnBlackOut(ucLAlpha);                                            /* 218 */

    ucLAlpha = Anim2D_CalcNowAlpha(sTAlpha, MisDispTimer);              /* 220 */

    for (i = 0; i < 2; i++) {                                           /* 221 */
        fScale = Anim2D_CalcNowScale(sTScale[i], MisDispTimer);         /* 222 */

        fX = (float)mission_tex[MISDISP_TITLE_PLATE].w * (fScale - 1.0f) * 0.5f;  /* 223 */
        fY = (float)mission_tex[MISDISP_TITLE_PLATE].h * (fScale - 1.0f) * 0.5f;  /* 224 */

        if (fX < 0.0f)                                                  /* 226 */
        {
            fX = 0.0f;
        }

        if (fY < 0.0f)                                                  /* 227 */
        {
            fY = 0.0f;
        }

        SpCmnDrawSpriteScale(MISDISP_TITLE_PLATE, (int)-fX, (int)-fY,
                             fScale, fScale, ucLAlpha, 0);              /* 229 */
        SpCmnDrawSpriteScale(MISDISP_TITLE_PLATE + 1, (int)-fX, (int)-fY,
                             fScale, fScale, ucLAlpha, 0);              /* 230 */
    }                                                                   /* 231 */

    ucLAlpha = Anim2D_CalcNowAlpha(sTAlpha2, MisDispTimer);             /* 233 */
    SpCmnDrawRange(MISDISP_TITLE_PLATE, 99, 0, 0, ucLAlpha, 0);         /* 234 */

    iMsgID = MissionGetID() * 3 + 2;                                    /* 236 */

    PrintMsg(0x3c, iMsgID, SpCmnGetCenterX(0x3c, iMsgID, 0x140),        /* 237 */
             0x8f, 0x21, ucLAlpha, 0xa0);                               /* 240 */

    if (MisDispTimer < MISDISP_TIMER_MAX)                               /* 242 */
    {
        MisDispTimer++;
    }
}

// ──────────────────────────────────────────────────────────────────────
// The result screens.

void MisDispClearInit(void)                                             /* 254 */
{
    MisDispTimer = 0;
}

/* The ordinary mission-clear screen: time, score, shots and then the prize and
 * rank, brought in one row at a time.  Each row of iDatList[] is
 * { first sprite, last sprite, start offset } -- the offsets are 0, -10, -20
 * and -40, so the rows arrive ten frames apart with the rank plate last.
 *
 * A mission that has been cleared before (iStat >= 2) does not show the prize
 * row at all, which is the one thing the fourth row's two extra tests are for.
 *
 * The row alphas are kept in ucAAlpha[] because the second pass -- the "new
 * record" markers -- has to fade in step with the rows it annotates. */
void MisDispClear(u_char ucAlpha, int iTime, int iScore, int iShot,
                  void *pMissionTex, void *pRsCmnTex)                   /* 259 */
{
    int    i;
    int    iMissionID;
    int    iStat;
    u_char ucLAlpha;
    u_char ucAAlpha[8];
    int    iNowTime;

    static const ALPHA_ANIM_TBL sAlDat[2] =                             /* rdata 3c0360 */
    {
        {   0, 128,  0, 15 },
        {  -1,  -1, -1, -1 }
    };
    /* Non-const in the ROM -- it is in .data, not .rodata -- though nothing
     * writes to it. */
    static int iDatList[5][3] =                                         /* data 32e968 */
    {
        { 0x78, 0x79,   0 },
        { 0x7a, 0x7a, -10 },
        { 0x7b, 0x7d, -20 },
        { 0x86, 0x86, -40 },
        {   -1,   -1,  -1 }
    };

    iMissionID = MissionGetID();                                        /* 262 */
    iStat = MissionGetStat(iMissionID, 0);                              /* 263 */

    SpCmnStart(mission_tex);                                            /* 280 */
    PK2SendVram((uintptr_t)(uintptr_t)pRsCmnTex, -1, -1, 0);                /* 281 */

    DrawCmnCapGroup_W(0xc, 0xc, ucAlpha, 0);                            /* 286 */

    for (i = 0; iDatList[i][0] != -1; i++) {                            /* 292 */
        iNowTime = MisDispTimer + iDatList[i][2];                       /* 293 */

        if (iNowTime <= 0)                                              /* 295 */
        {
            ucAAlpha[i] = 0;                                            /* 297 */
            continue;
        }

        if (MISDISP_ROW_FADE_TIME - 1 < iNowTime)                       /* 299 */
        {
            iNowTime = MISDISP_ROW_FADE_TIME - 1;
        }

        ucLAlpha = (u_char)(Anim2D_CalcNowAlpha(sAlDat, iNowTime) * ucAlpha / 128);  /* 301 */

        if ((i != 3) || (iStat < 2))                                    /* 304 */
        {
            SpCmnDrawRange(iDatList[i][0], iDatList[i][1], 0, 0, ucLAlpha, 0);  /* 305 */
        }

        ucAAlpha[i] = ucLAlpha;

        switch (i)                                                      /* 309 */
        {
        case 0:
        {
            /* Block-scope in the ROM: functions.txt lists no h/m/s local for
             * this function, but the stack slots are there. */
            int iHour;
            int iMin;
            int iSec;

            MissionGetTimePal(&iHour, &iMin, &iSec, iTime);             /* 312 */
            MisDispTime(iHour, iMin, iSec, 0, 0, ucLAlpha);             /* 313 */
            SpCmnDrawRange(0x98, 0x99, 0, 0, ucLAlpha, 0);              /* 314 */
            break;                                                      /* 315 */
        }

        case 1:
            MisDispNum(iScore, 6, 0x15a, 0xb1, ucLAlpha, 0);            /* 317 */
            SpCmnDrawSprite(0x9a, 0, 0, ucLAlpha, 0);                   /* 318 */
            break;                                                      /* 319 */

        case 2:
            MisDispNum(iShot, 6, 0x15a, 0xda, ucLAlpha, 0);             /* 321 */
            SpCmnDrawSprite(0x9b, 0x12, 0, ucLAlpha, 0);                /* 322 */
            break;                                                      /* 323 */

        case 3:
            if (iStat < 2)                                              /* 326 */
            {
                MisDispNum(MissionGetPrize(iMissionID), 5, 0x143, 0x163,
                           ucLAlpha, 0);                                /* 328 */
                SpCmnDrawSprite(0x86, 0, 0, ucLAlpha, 0);               /* 329 */
                SpCmnDrawSprite(0x9c, 0, 0, ucLAlpha, 0);               /* 330 */
            }

            SpCmnDrawSprite(0x7e, 0, 0, ucLAlpha, 0);                   /* 333 */

            SpCmnDrawSprite(MisGetRankLast(iMissionID, iTime, iScore, iShot)  /* 335 */
                                + MISDISP_RANK_BASE,
                            0, 0, ucLAlpha, 0);                         /* 338 */
            break;
        }
    }                                                                   /* 341 */

    PK2SendVram((uintptr_t)(uintptr_t)pMissionTex, -1, -1, 0);              /* 343 */
    SpCmnDrawRange(0x73, 0x77, 0, 0, ucAlpha, 0);                       /* 345 */

    /* Second pass: the "new record" markers, each at its row's alpha. */
    for (i = 0; iDatList[i][0] != -1; i++) {                            /* 347 */
        ucLAlpha = ucAAlpha[i];                                         /* 348 */

        switch (i)                                                      /* 350 */
        {
        case 0:
            if (MissionCheckRecord(iMissionID, MISSION_TYPE_TIME, iTime) != 0)  /* 352 */
            {
                SpCmnDrawSprite(0x8b, 0, 0, ucLAlpha, 0);               /* 355 */
            }
            break;

        case 1:
            if (MissionCheckRecord(iMissionID, MISSION_TYPE_SCORE, iScore) != 0)  /* 357 */
            {
                SpCmnDrawSprite(0x8c, 0, 0, ucLAlpha, 0);               /* 360 */
            }
            break;

        case 2:
            if (MissionCheckRecord(iMissionID, MISSION_TYPE_SHOT, iShot) != 0)  /* 362 */
            {
                SpCmnDrawSprite(0x8d, 0, 0, ucLAlpha, 0);               /* 363 */
            }
            break;                                                      /* 365 */

        case 3:
            if (iStat < 2)                                              /* 368 */
            {
                SpCmnDrawRange(0x87, 0x8a, 0, 0, ucLAlpha, 0);          /* 370 */
            }
            break;
        }
    }                                                                   /* 374 */

    if (MisDispTimer < MISDISP_TIMER_MAX)                               /* 375 */
    {
        MisDispTimer++;
    }
}

/* All missions cleared.  No numbers at all -- three rule pairs and two lines of
 * text over the congratulation plates. */
void MisDispClearAll(u_char ucAlpha, int iTime, int iScore, int iShot,
                     void *pMissionTex, void *pRsCmnTex)                /* 382 */
{
    (void)iTime;
    (void)iScore;
    (void)iShot;
    (void)pRsCmnTex;

    SpCmnStart(mission_tex);                                            /* 383 */
    PK2SendVram((uintptr_t)(uintptr_t)pMissionTex, -1, -1, 0);              /* 384 */

    DrawCmnCapGroup_W(0xc, 0xc, ucAlpha, 0);                            /* 389 */

    SpCmnDrawRange(0x64, 0x65, 0, 0, ucAlpha, 0);                       /* 396 */
    SpCmnDrawRange(0x6d, 0x6f, 0, 0, ucAlpha, 0);                       /* 397 */
    SpCmnDrawRange(0x6a, 0x6c, 0, 0, ucAlpha, 0);                       /* 398 */

    SpCmnDrawRange(0x70, 0x71, 0, 0xdb, ucAlpha, 0);                    /* 401 */
    SpCmnDrawRange(0x70, 0x71, 0, 0x103, ucAlpha, 0);                   /* 402 */
    SpCmnDrawRange(0x70, 0x71, 0, 299, ucAlpha, 0);                     /* 403 */

    PrintMsg(0x3c, 0x4b, 0x8c, 0xe6, 0x22, ucAlpha, 0xa0);              /* 407 */
    PrintMsg(0x3c, 0x4c, 0x8c, 0x10e, 0x22, ucAlpha, 0xa0);             /* 409 */
}

/* All missions cleared at rank S -- the same screen with one plate range
 * widened, one rule pair and one text line dropped. */
void MisDispClearAllS(u_char ucAlpha, int iTime, int iScore, int iShot,
                      void *pMissionTex, void *pRsCmnTex)               /* 416 */
{
    (void)iTime;
    (void)iScore;
    (void)iShot;
    (void)pRsCmnTex;

    SpCmnStart(mission_tex);                                            /* 417 */
    PK2SendVram((uintptr_t)(uintptr_t)pMissionTex, -1, -1, 0);              /* 418 */

    DrawCmnCapGroup_W(0xc, 0xc, ucAlpha, 0);                            /* 423 */

    SpCmnDrawRange(0x64, 0x69, 0, 0, ucAlpha, 0);                       /* 430 */
    SpCmnDrawRange(0x6d, 0x6f, 0, 0, ucAlpha, 0);                       /* 431 */

    SpCmnDrawRange(0x70, 0x71, 0, 0xdb, ucAlpha, 0);                    /* 434 */
    SpCmnDrawRange(0x70, 0x71, 0, 0x103, ucAlpha, 0);                   /* 435 */

    PrintMsg(0x3c, 0x4b, 0x8c, 0xe6, 0x22, ucAlpha, 0xa0);              /* 440 */
}

/* Failed.  The same three readouts as the clear screen but with no fade-in
 * schedule, no prize and no rank -- everything at the caller's alpha. */
void MisDispBadEnd(u_char ucAlpha, int iTime, int iScore, int iShot,
                   void *pMissionTex, void *pRsCmnTex)                  /* 452 */
{
    int iHour;
    int iMin;
    int iSec;

    SpCmnStart(mission_tex);                                            /* 455 */
    PK2SendVram((uintptr_t)(uintptr_t)pMissionTex, -1, -1, 0);              /* 456 */

    DrawCmnCapGroup_W(0xc, 0xc, ucAlpha, 0);                            /* 461 */

    SpCmnDrawRange(0x9d, 0xa1, 0, 0, ucAlpha, 0);                       /* 467 */

    PK2SendVram((uintptr_t)(uintptr_t)pRsCmnTex, -1, -1, 0);                /* 469 */
    SpCmnDrawRange(0xa2, 0xa9, 0, 0, ucAlpha, 0);                       /* 470 */

    MissionGetTimePal(&iHour, &iMin, &iSec, iTime);                     /* 473 */
    MisDispTime(iHour, iMin, iSec, 0, 0, ucAlpha);                      /* 475 */

    MisDispNum(iScore, 6, 0x15a, 0xb1, ucAlpha, 0);                     /* 477 */
    SpCmnDrawRange(0xaa, 0xac, 0, 0, ucAlpha, 0);                       /* 478 */

    MisDispNum(iShot, 6, 0x15a, 0xda, ucAlpha, 0);                      /* 480 */
    SpCmnDrawSprite(0xad, 0x12, 0, ucAlpha, 0);                         /* 481 */
}
