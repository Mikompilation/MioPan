// FILE: /home/zero_rom/zero2np/src/outgame/mis_sel_disp.c
//
// The mission-select screen's drawing half.
//
// Six list rows out of 25, each showing its mission's name, rank plate and
// either a best time or a best score depending on how the mission is scored.
// Above them are the three achievement counters (how much of the list has been
// cleared at S, at A-or-better, and at all), beside them a scrollbar, and over
// them the cursor and the Album/Save menu that slides out of it.  At the bottom
// sits the caption window, which cross-fades between two messages whenever the
// cursor lands on a different mission.
//
// A value the list has no number for is drawn as dashes rather than zeros:
// that is what PrintNull_K() is, and SpCmnPrintNumber_NK2() is the wrapper
// that picks between the two on the sign of its argument.
//
// Reconstructed from the Feb 6 2004 prototype (SLES_523.84).
// Trailing /* NNN */ are ROM source line numbers, measured from
// disassemble_function and symbols.txt.

#include "mis_sel_disp.h"

#include "SpriteCmn.h"                      // SpCmn* draw / print helpers
#include "mission_sel.h"                    // MissionGet* / MISSION_*
#include "../graphics/graph2d/draw_cmn.h"   // DrawCmnCapGroup_W / Window / Sel*
#include "../graphics/graph2d/g2d_draw.h"   // DISP_SPRT / DISP_STR / DispSprD
#include "../graphics/graph2d/message.h"    // MSG_WIN_DAT / SetMsg*DefData / PrintMsg
#include "../graphics/graph2d/tim2.h"       // PK2SendVram
#include "../ingame/menu/zero2_anim2d.h"    // Zero2Anim2D_CsrAnimCtrl

#include <stdint.h>                         // uintptr_t

/* Message bank 8 is the list's own small font: id 0 is the ':' between the
 * time fields and id 3 the '-' that stands in for a missing number. */
#define MISSEL_MSG_NUM      8
#define MISSEL_MSG_COLON    0
#define MISSEL_MSG_DASH     3

/* Bank 0x3c is the mission text; three messages per mission (name, caption,
 * banner), which is where the * 3 in every id comes from. */
#define MISSEL_MSG_MISSION  0x3c
#define MISSEL_MSG_PER_ROW  3

/* The two confirmation captions, the only ones drawn with a yes/no widget. */
#define MISSEL_MSG_START    0x55
#define MISSEL_MSG_EXIT     0x56

/* The caption cross-fade runs over ten frames. */
#define MISSEL_FADE_TIME    10

/* Achievement-counter glyphs: 1..9 are 0x41..0x49 and zero is 0x4a, after the
 * nine rather than before them. */
#define MISSEL_DIGIT_BASE   0x40
#define MISSEL_DIGIT_ZERO   0x4a

/* The three floats below are the ROM's own words, which sit one or more ulp
 * under the decimals the source wrote (EE GCC truncates float literals); see
 * the .lit4 addresses.  0.692307651f is 9/13 truncated. */
#define MISSEL_CSR_SCALE_L  21.4499989f     /* lit4 3ee5b0, written 21.45   */
#define MISSEL_CSR_SCALE_R  20.3499985f     /* lit4 3ee5b4, written 20.35   */
#define MISSEL_MENU_SCALE   0.692307651f    /* lit4 3ee5bc, written 9/13    */
#define MISSEL_MENU_SEL_SCL 1.29999995f     /* lit4 3ee5c0, written 1.3     */
#define MISSEL_MENU_WIDE    2.39999986f     /* lit4 3ee5c8, written 2.4     */

static void PrintNull_K(int iNum, int iX, int iY, u_char ucColLabel,
                        u_char ucAlpha, int iPri);
static void SpCmnPrintNumber_NK2(int iData, int iNum, int iX, int iY,
                                 u_char ucColLabel, u_char ucAlpha, int iPri,
                                 u_char ucMsgType, int ucZeroFlg);
static void MissionDrawRank(int iRank, int off_x, int iCsr, u_char alpha);
static void MissionDrawClearTime(int iHour, int iMin, int iSec, int iCsr,
                                 u_char ucAlpha);
static void MissionDrawClearPoint(int iPoint, int iCsr, u_char ucAlpha);
static void MissionDrawTassei(int iRank, int iOffY, u_char ucAlpha);
static void MissionDrawScrollbar(int iTop, u_char ucAlpha);
static void MissionDrawCsrOnly(int iLabel, int iOffX, int iOffY, u_char ucAlpha,
                               float fScale);
static void MissionDrawCsr(int iCnt, u_char ucAlpha);
static void MisFadePrint(int iMsgID, int iX, int iY, u_char ucAlpha);

/* Both start at -1, not 0 -- an important difference, because the screen's
 * first MisFadeSetMsg(-1) then reports "nothing changed" and leaves the fade
 * counter alone. */
static int MisFadeNew = -1;                                             /* sdata 3f30b4 */
static int MisFadeOld = -1;                                             /* sdata 3f30b8 */
static int MisFadeCnt;                                                  /* sdata 3f30bc */

/* `iNum` dashes in place of a number the list has none of. */
static void PrintNull_K(int iNum, int iX, int iY, u_char ucColLabel,
                        u_char ucAlpha, int iPri)                       /* 42 */
{
    int i;

    for (i = 0; i < iNum; i++) {                                        /* 45 */
        SpCmnPrintMsg_K(MISSEL_MSG_NUM, MISSEL_MSG_DASH, iX, iY,
                        ucColLabel, ucAlpha, iPri);                     /* 47 */
        iX += 16;                                                       /* 48 */
    }                                                                   /* 49 */
}

/* SpCmnPrintNumber_NK() that prints dashes for a negative value. */
static void SpCmnPrintNumber_NK2(int iData, int iNum, int iX, int iY,
                                 u_char ucColLabel, u_char ucAlpha, int iPri,
                                 u_char ucMsgType, int ucZeroFlg)       /* 57 */
{
    if (iData < 0)                                                      /* 58 */
    {
        PrintNull_K(iNum, iX, iY, ucColLabel, ucAlpha, iPri);           /* 59 */
        return;
    }

    SpCmnPrintNumber_NK(iData, iNum, iX, iY, ucColLabel, ucAlpha, iPri,
                        ucMsgType, (u_char)ucZeroFlg);                  /* 62 */
}

/* One row's rank plate: a letter over a backing tile, or two dashes where the
 * mission has never been cleared.  Rank 0 (S) gets an extra highlight layer,
 * and ranks 0 and 1 a brighter tile than the rest. */
static void MissionDrawRank(int iRank, int off_x, int iCsr, u_char alpha)  /* 69 */
{
    static int iSumiY[6] =                                              /* data 32d290 */
    {
        112, 147, 182, 217, 252, 287
    };
    static int iNon[6] =                                                /* data 32d2a8 */
    {
        116, 151, 186, 221, 256, 291
    };

    if (iRank == -1)                                                    /* 74 */
    {
        PrintNull_K(2, 0x20c, iNon[iCsr], 0x15, alpha, 0xa0);           /* 76 */
        return;
    }

    if (iRank == 0)                                                     /* 81 */
    {
        SpCmnDrawSprite(0x3b, off_x, iSumiY[iCsr] + 2, alpha, 1);       /* 82 */
        SpCmnDrawSprite(0x3c, off_x, iSumiY[iCsr] + 1, alpha, 1);       /* 83 */
    }
    else
    {
        SpCmnDrawSprite(iRank + 0x3c, off_x, iSumiY[iCsr] + 1, alpha, 1);  /* 86 */
    }

    if (iRank < 2)                                                      /* 90 */
    {
        SpCmnDrawSprite(0x39, off_x, iSumiY[iCsr], alpha, 1);           /* 92 */
    }
    else
    {
        SpCmnDrawSprite(0x38, off_x, iSumiY[iCsr], alpha, 1);           /* 95 */
    }
}

/* hh:mm:ss.  `iOffX` is the ROM's own running cursor, stepped between the
 * fields rather than recomputed. */
void MissionDrawTime(int iHour, int iMin, int iSec, int iOffX, int iOffY,
                     u_char ucAlpha)                                    /* 104 */
{
    SpCmnPrintNumber_NK2(iHour, 2, iOffX, iOffY, 0x15, ucAlpha, 0, 0, 1);  /* 110 */
    iOffX += 0x24;                                                      /* 111 */

    SpCmnPrintMsg_K(MISSEL_MSG_NUM, MISSEL_MSG_COLON, iOffX, iOffY, 0x15,
                    ucAlpha, 0xa0);                                     /* 114 */
    iOffX += 0xe;                                                       /* 115 */

    SpCmnPrintNumber_NK2(iMin, 2, iOffX, iOffY, 0x15, ucAlpha, 0, 0, 1);   /* 118 */
    iOffX += 0x24;                                                      /* 119 */

    SpCmnPrintMsg_K(MISSEL_MSG_NUM, MISSEL_MSG_COLON, iOffX, iOffY, 0x15,
                    ucAlpha, 0xa0);                                     /* 122 */
    iOffX += 0xe;

    SpCmnPrintNumber_NK2(iSec, 2, iOffX, iOffY, 0x15, ucAlpha, 0, 0, 1);   /* 126 */
}

static void MissionDrawClearTime(int iHour, int iMin, int iSec, int iCsr,
                                 u_char ucAlpha)                        /* 134 */
{
    static int iPosY[6] =                                               /* data 32d2c0 */
    {
        116, 151, 186, 221, 256, 291
    };

    MissionDrawTime(iHour, iMin, iSec, 0x15e, iPosY[iCsr], ucAlpha);    /* 136 */
}

static void MissionDrawClearPoint(int iPoint, int iCsr, u_char ucAlpha) /* 142 */
{
    static int iPosY[6] =                                               /* data 32d2d8 */
    {
        116, 151, 186, 221, 256, 291
    };
    static int iWakuY[6] =                                              /* data 32d2f0 */
    {
        124, 159, 194, 229, 264, 299
    };

    if (iPoint < 0)                                                     /* 146 */
    {
        PrintNull_K(6, 0x15e, iPosY[iCsr], 0x15, ucAlpha, 0xa0);        /* 148 */
    }
    else
    {
        SpCmnPrintNumber_NK(iPoint, 6, 0x15e, iPosY[iCsr], 0x15, ucAlpha,
                            0, 0, 0);                                   /* 151 */
    }

    SpCmnDrawSprite(0x3a, 0, iWakuY[iCsr], ucAlpha, 1);                 /* 154 */
}

/* One of the three achievement percentages across the top.  `iRank` is the
 * grade being counted -- 0, 1 or 5 -- and picks both the column triple in
 * iPos[] and the suffix plate; anything else is not drawn at all. */
static void MissionDrawTassei(int iRank, int iOffY, u_char ucAlpha)     /* 160 */
{
    int i;
    int iRid;
    int iKurai;
    int iNum;

    static int iPos[3][3] =                                             /* data 32d308 */
    {
        { 204, 214, 224 },
        { 273, 283, 293 },
        { 117, 127, 137 }
    };

    iRid = 0;                                                           /* 162 */
    iKurai = 100;                                                       /* 168 */

    iNum = MissionGetTassei(iRank);                                     /* 169 */

    switch (iRank)                                                      /* 171 */
    {
    case 0:
        break;

    case 1:
        iRid = 1;                                                       /* 173 */
        break;

    case 5:
        iRid = 2;                                                       /* 174 */
        break;

    default:
        return;
    }

    for (i = 0; i < 3; i++) {                                           /* 179 */
        int iDigit = iNum / iKurai;                                     /* 180 */
        int iLabel = iDigit + MISSEL_DIGIT_BASE;                        /* 181 */

        /* Zero is at the far end of the glyph run, and so is anything out of
         * range -- one unsigned compare covers both. */
        if ((u_int)(iDigit - 1) >= 9u)                                  /* 183 */
        {
            iLabel = MISSEL_DIGIT_ZERO;
        }

        SpCmnDrawSprite(iLabel, iPos[iRid][i], iOffY, ucAlpha, 0);      /* 187 */

        iNum -= iDigit * iKurai;                                        /* 188 */
        iKurai /= 10;                                                   /* 189 */
    }                                                                   /* 190 */

    SpCmnDrawSprite(iRid + 0x4b, 0, 0, ucAlpha, 0);                     /* 192 */
}                                                                       /* 193 */

/* The bar down the right-hand side.  Its travel is the gap between the two
 * ends of iMovY[], divided across the 19 scroll positions. */
static void MissionDrawScrollbar(int iTop, u_char ucAlpha)              /* 199 */
{
    int iList;
    int iMovY[2] = { 129, 261 };                                        /* 201 */

    SpCmnDrawRange(0x36, 0x37, 0, 0, ucAlpha, 0);                       /* 205 */

    iList = (iMovY[1] - iMovY[0]) * iTop / 19;                          /* 207 */

    SpCmnDrawSprite(0x33, 0, iList, ucAlpha, 1);                        /* 209 */
    SpCmnDrawSprite(0x34, 0, iList, ucAlpha, 1);                        /* 210 */
    SpCmnDrawSprite(0x35, 0, iList, ucAlpha, 1);                        /* 211 */
}

/* A three-piece cursor: two fixed caps with a middle bar stretched to
 * `fScale` about its own left edge. */
static void MissionDrawCsrOnly(int iLabel, int iOffX, int iOffY, u_char ucAlpha,
                               float fScale)                            /* 218 */
{
    DISP_SPRT ds;

    SpCmnDrawSprite(iLabel, iOffX, iOffY, ucAlpha, 1);                  /* 221 */

    SpCmnSetSprite(&ds, iLabel + 1, iOffX, iOffY, ucAlpha, 1);          /* 223 */

    ds.csx = ds.x;
    ds.csy = ds.y;
    ds.scw = fScale;
    ds.sch = 1.0f;                                                      /* 224 */

    DispSprD(&ds);                                                      /* 225 */

    SpCmnDrawSprite(iLabel + 2, iOffX, iOffY, ucAlpha, 1);              /* 226 */
}

/* The screen's caption group.  Both offsets are ignored -- another of the
 * folder's display helpers that takes them and reads neither.
 *
 * Lines 235..275 hold no code: the object is complete without them, so that
 * forty-line span is comment or a disabled older draw. */
void MissionCaptionDisp(int off_x, int off_y, u_char alpha)             /* 231 */
{
    (void)off_x;
    (void)off_y;

    DrawCmnCapGroup_W(8, 8, alpha, 0);                                  /* 234 */
}

/* The list cursor: the wide bar on the selected row, plus the two scroll
 * arrows -- each suppressed at the end of the list it points past. */
static void MissionDrawCsr(int iCnt, u_char ucAlpha)                    /* 276 */
{
    u_char ucRgb;
    int    iEndCheck;

    static int iPosY[4][6] =                                            /* data 32d330 */
    {
        { 113, 148, 183, 218, 253, 288 },
        {  95, 130, 165, 200, 235, 270 },
        { 146, 181, 216, 251, 286, 321 },
        { 107, 142, 177, 212, 247, 282 }
    };
    static char iCsrTime;                                               /* sdata 3f30b0 */

    iEndCheck = MissionCheckEnd();                                      /* 285 */

    Zero2Anim2D_CsrAnimCtrl(&iCsrTime, &ucRgb);                         /* 288 */
    ucRgb = (u_char)(ucAlpha * ucRgb >> 7);                             /* 289 */

    MissionDrawCsrOnly(0x4e, 0, iPosY[3][iCnt], ucAlpha, MISSEL_CSR_SCALE_L);  /* 293 */
    MissionDrawCsrOnly(0x55, 0, iPosY[0][iCnt], ucAlpha, MISSEL_CSR_SCALE_R);  /* 296 */

    if (iEndCheck != 1)                                                 /* 299 */
    {
        SpCmnDrawSprite(0x53, 0, iPosY[1][iCnt], ucAlpha, 1);           /* 300 */
        SpCmnDrawSprite(0x51, 0, iPosY[1][iCnt] - 2, ucRgb, 1);         /* 302 */
    }

    if (iEndCheck != 2)                                                 /* 305 */
    {
        SpCmnDrawSprite(0x54, 0, iPosY[2][iCnt], ucAlpha, 1);           /* 307 */
        SpCmnDrawSprite(0x52, 0, iPosY[2][iCnt] - 4, ucRgb, 1);         /* 309 */
    }
}

/* The whole list.  Note the second loop bound is redundant -- `i` cannot reach
 * 25 when it is already held under 6 -- but it is the ROM's own. */
void MissionDrawSelect(void *pMisTexAddr, int iTopID, int iCsr, u_char ucAlpha)  /* 317 */
{
    int i;
    int iNum;
    int iType;
    int iRank;
    int iHour;
    int iMin;
    int iSec;

    static int iPosY[6] =                                               /* data 32d390 */
    {
        116, 151, 186, 221, 256, 291
    };

    PK2SendVram((uintptr_t)pMisTexAddr, -1, -1, 0);              /* 321 */

    SpCmnDrawRange(0, 0x32, 0, 0, ucAlpha, 0);                          /* 324 */

    MissionCaptionDisp(0, 0, ucAlpha);                                  /* 326 */

    MissionDrawTassei(0, 0x56, ucAlpha);                                /* 329 */
    MissionDrawTassei(1, 0x56, ucAlpha);                                /* 330 */
    MissionDrawTassei(5, 0x56, ucAlpha);                                /* 331 */

    MissionDrawScrollbar(iTopID, ucAlpha);                              /* 333 */

    for (i = 0; (i < 6) && (i < MISSION_NUM); i++) {                    /* 335, 339 */
        iNum = MissionGetScore(iTopID + i);                             /* 341 */
        iType = MissionGetType(iTopID + i);                             /* 342 */
        iRank = MissionGetRank3(iTopID + i);                            /* 343 */

        SpCmnPrintMsg_K(MISSEL_MSG_MISSION, (iTopID + i) * MISSEL_MSG_PER_ROW,
                        0xa0, iPosY[i], 0x15, ucAlpha, 0xa0);           /* 347 */

        MissionDrawRank(iRank, 0, i, ucAlpha);                          /* 350 */

        if (iType == MISSION_TYPE_TIME)                                 /* 352 */
        {
            if (iRank == -1)                                            /* 355 */
            {
                iHour = -1;
                iMin = -1;
                iSec = -1;                                              /* 356 */
            }
            else
            {
                MissionGetTimePal(&iHour, &iMin, &iSec, iNum);          /* 358 */
            }

            MissionDrawClearTime(iHour, iMin, iSec, i, ucAlpha);        /* 360 */
        }                                                               /* 361 */
        else if ((0 < iType) && (iType < 4))
        {
            if (iRank == -1)                                            /* 364 */
            {
                MissionDrawClearPoint(-1, i, ucAlpha);                  /* 365 */
            }
            else
            {
                MissionDrawClearPoint(iNum, i, ucAlpha);                /* 367 */
            }
        }
    }                                                                   /* 371 */

    MissionDrawCsr(iCsr, ucAlpha);                                      /* 374 */
}

/* The Album/Save menu, drawn as two mirrored halves of one plate opening out
 * from the cursor.  `fMove` runs 0..1 across the slide; while it is still
 * moving (`iFlg` non-zero) the plates are scaled by it, and once it is over
 * the plates go full width and the alpha is scaled instead. */
void MissionDrawMiniMenu(void *pOutGameTex, void *pMisTexAddr, int iCsr,
                         u_char ucMstAlpha, float fMove, int iSelCsr, int iFlg)  /* 382 */
{
    int       i;
    DISP_SPRT aDs;
    int       iPosID;
    u_char    ucAlpha;
    float     fCsrScale;
    int       iMenuY;
    int       iCsrY;
    float     fW;

    /* Rows 0..2 are the two menu lines and rows 3..5 their shadows, four
     * pixels above; the second half is the same set 130 pixels down, for a
     * cursor in the bottom half of the list. */
    int iPosY[2][6] =                                                   /* rodata 3c0120 */
    {
        { 100, 134, 169,  96, 131, 166 },
        { 230, 264, 299, 226, 261, 296 }
    };                                                                  /* 388 */

    if (fMove < 0.0f)                                                   /* 394 */
    {
        fMove = 0.0f;
    }

    if (1.0f < fMove)                                                   /* 395 */
    {
        fMove = 1.0f;
    }

    PK2SendVram((uintptr_t)(uintptr_t)pOutGameTex, -1, -1, 0);              /* 398 */

    SpCmnDrawRange(0x5b, 0x5d, 0, 0, ucMstAlpha, 0);                    /* 400 */

    ucAlpha = (u_char)((float)ucMstAlpha * fMove);                      /* 402 */

    if (iFlg == 0)                                                      /* 403 */
    {
        fMove = 1.0f;                                                   /* 404 */
        ucMstAlpha = ucAlpha;                                           /* 405 */
    }

    iPosID = (iCsr < 3);                                                /* 407 */

    for (i = 0; i < 2; i++) {                                           /* 410 */
        fCsrScale = 1.0f;                                               /* 411 */
        iMenuY = iPosY[iPosID][3 + i];                                  /* 412 */

        if (i == iSelCsr)                                               /* 417 */
        {
            fCsrScale = MISSEL_MENU_SEL_SCL;                            /* 418 */
        }

        /* The same plate twice, mirrored -- a negative scw opens it to the
         * left of the shared centre. */
        SpCmnSetSprite(&aDs, 0x5c, 0x1c5, iMenuY - 0x13, ucMstAlpha, 0);  /* 423 */

        aDs.csx = aDs.x;
        aDs.csy = aDs.y;
        aDs.scw = fMove * -MISSEL_MENU_SCALE * fCsrScale;
        aDs.sch = 1.0f;                                                 /* 424 */

        DispSprD(&aDs);                                                 /* 425 */

        SpCmnSetSprite(&aDs, 0x5c, 0x1c5, iMenuY - 0x13, ucMstAlpha, 0);  /* 426 */

        aDs.csx = aDs.x;
        aDs.csy = aDs.y;
        aDs.scw = fMove * MISSEL_MENU_SCALE * fCsrScale;
        aDs.sch = 1.0f;                                                 /* 427 */

        DispSprD(&aDs);                                                 /* 428 */
    }                                                                   /* 429 */

    if (fMove != 0.0f)                                                  /* 431 */
    {
        iCsrY = iPosY[iPosID][iSelCsr];                                 /* 433 */

        fW = (float)aDs.w * MISSEL_MENU_SCALE * MISSEL_MENU_WIDE;       /* 434 */

        /* The bar grows leftwards out of its right-hand end, so the x it is
         * drawn at slides back by half the width it has gained. */
        DrawCmnSelCsr(0, (fW * 0.5f + 471.0f) - fW * 0.5f * fMove,
                      (float)(iCsrY - 3), ucMstAlpha, fW * fMove, 0);   /* 438 */
    }

    PK2SendVram((uintptr_t)(uintptr_t)pMisTexAddr, -1, -1, 0);              /* 441 */

    for (i = 0; i < 2; i++) {                                           /* 443 */
        SpCmnDrawSprite(0x59 + i, 0, iPosY[iPosID][i], ucAlpha, 1);     /* 444 */
    }                                                                   /* 446 */
}

// ──────────────────────────────────────────────────────────────────────
// The caption window.

int MisFadeSetMsg(int iNewMsg)                                          /* 460 */
{
    MisFadeOld = MisFadeNew;                                            /* 461 */
    MisFadeNew = iNewMsg;                                               /* 462 */

    if (MisFadeOld != iNewMsg)                                          /* 464 */
    {
        MisFadeCnt = 0;                                                 /* 465 */
        return 1;                                                       /* 466 */
    }

    return 0;                                                           /* 467 */
}

/* One side of the cross-fade.  The two confirmation captions bring a yes/no
 * widget with them; everything else is just the line of text. */
static void MisFadePrint(int iMsgID, int iX, int iY, u_char ucAlpha)    /* 471 */
{
    static int iPosX[2] = { 155, 362 };                                 /* sdata 3f30c0 */

    if (iMsgID < 0)                                                     /* 478 */
    {
        return;
    }

    if ((u_int)(iMsgID - MISSEL_MSG_START) < 2u)                        /* 481 */
    {
        DrawCmnSelCsr(0, (float)iPosX[MissionGetYesNo()], 390.0f,
                      ucAlpha, 0.0f, 0);                                /* 485 */
        DrawCmnSelYes(0, 153.0f, 390.0f, ucAlpha);                      /* 487 */
        DrawCmnSelNo(0, 361.0f, 390.0f, ucAlpha);                       /* 488 */
    }

    PrintMsg(MISSEL_MSG_MISSION, iMsgID, iX, iY, 1, ucAlpha, 0xa0);     /* 496 */
}                                                                       /* 497 */

/* The window plus both captions: the new one fading up over ten frames and the
 * old one fading down over the same, so the two always sum to full. */
void MisFadeProc(u_char ucMstAlpha)                                     /* 501 */
{
    int         iAlpha;
    MSG_WIN_DAT sWinDat;
    DISP_STR    sDispStr;

    iAlpha = 0x80;                                                      /* 502 */

    if (MisFadeCnt < MISSEL_FADE_TIME)                                  /* 506 */
    {
        iAlpha = MisFadeCnt * 128 / MISSEL_FADE_TIME;                   /* 507 */
        MisFadeCnt++;                                                   /* 508 */
    }

    iAlpha = iAlpha * ucMstAlpha >> 7;                                  /* 511 */

    SetMsgWinDefData(&sWinDat, MISSEL_MSG_MISSION);                     /* 513 */

    sWinDat.y = sWinDat.y - 6.0f;                                       /* 515 */
    sWinDat.h = sWinDat.h + 6.0f;                                       /* 516 */

    DrawCmnWindow(0, sWinDat.x, sWinDat.y, sWinDat.w, sWinDat.h,
                  ucMstAlpha, 0x80);                                    /* 519 */

    SetMsgDefData(&sDispStr, MISSEL_MSG_MISSION);                       /* 520 */

    sDispStr.pos_y = sDispStr.pos_y - 5;                                /* 521 */

    MisFadePrint(MisFadeNew, sDispStr.pos_x, sDispStr.pos_y, (u_char)iAlpha);  /* 523 */
    MisFadePrint(MisFadeOld, sDispStr.pos_x, sDispStr.pos_y,
                 (u_char)((0x80 - iAlpha) * ucMstAlpha >> 7));          /* 524, 525 */
}
