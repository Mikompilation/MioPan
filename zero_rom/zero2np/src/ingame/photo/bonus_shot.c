// FILE: /home/zero_rom/zero2np/src/ingame/photo/bonus_shot.c
//
// The post-shot score breakdown.
//
// Up to nine lines can appear -- one per bonus the shot earned -- and each is
// its own little animation, which is what CBonusShotOne is for.  Req() takes
// the whole BONUS_SHOT_SCORE at once, packs the non-zero entries down into the
// first mDispNum slots, and arms a timer; Work() then reads that timer's value
// on the way down and starts each pass as it goes past.
//
// Two sequences, chosen by whether the shot had a combo:
//
//   no combo    42 underlines out, 37 text in, 0 everything out
//   combo       52 combo mark in, 42 underlines out, 38 text in,
//               18 combo mark out, 0 everything out
//
// The three staggered passes (underline, in, out) each advance one line every
// fourth frame; the combo path skips the stagger and starts all the lines at
// once.  Once a line's score is added the old total fades out and the new one
// fades in, which is the score counting up.
//
// bonus_shot.o also owns the six CBonusShotOne bodies.
//
// Reconstructed from the Feb 6 2004 prototype (SLES_523.84), bonus_shot.o.
// All 13 ZERO2.MAP .text symbols plus aShotTexTbl and both point tables.

#include "m_plyr_camera.h"
#include "finder.h"                             /* SetNumerousDisp         */
#include "n_finder_dat.h"                       /* n_finder_dat            */
#include "../../graphics/graph2d/g2d_draw.h"    /* DISP_SPRT / DispSprD    */
#include "../../system/os/system.h"             /* GetLanguage             */

/* n_finder_dat[] indices. */
#define FD_BONUS_UNDERLINE  0x82    /* the rule each line slides out from    */
#define FD_COMBO_X1         0x84    /* the plain "no combo" mark             */
#define FD_COMBO_X2         0x85    /* +mComboNum: the x2..x9 marks          */
#define FD_COMBO_CHAR       0x8f    /* the word beside them                  */
#define FD_SCORE_DIGIT      0x90    /* ten consecutive digit records         */
#define FD_SCORE_PTS        0x9a    /* the "pts" plate after the number      */

#define BS_LINE_MAX         9

/* Timer values the sequence keys off, counting down.  Req() adds
 * BS_TIME_BASE to the caller's hold, and another BS_TIME_COMBO_EXTRA when
 * there is a combo mark to show first. */
#define BS_TIME_BASE            42
#define BS_TIME_COMBO_EXTRA     10
#define BS_T_UNDERLINE          42
#define BS_T_IN                 37
#define BS_T_COMBO_IN           52
#define BS_T_COMBO_UNDERLINE    42
#define BS_T_COMBO_TEXT_IN      38
#define BS_T_COMBO_OUT          18

/* One line every fourth frame while a pass is running. */
#define BS_STEP_MASK        3

/* Row pitch, and how far right a line starts before it slides in. */
#define BS_ROW_PITCH        4       /* iYPos = index << BS_ROW_PITCH         */
#define BS_SLIDE_X          10
#define BS_SLIDE_TIME       4
#define BS_OUT_TIME         8
#define BS_TEXT_GAP         9       /* between a line's two texture pieces   */

/* Alpha and scale ramps. */
#define BS_TEXT_FADE_IN     0x1f
#define BS_TEXT_FADE_OUT  (-0x0f)
#define BS_LINE_FADE_IN     0x0f
#define BS_LINE_FADE_OUT  (-0x1f)
#define BS_LINE_SCALE_SPD   0x19
#define BS_SCORE_FADE_IN    0x0c
#define BS_SCORE_FADE_OUT (-0x0c)

/* The old total is slammed to "full" the moment a bonus lands.  128 is one
 * past the <char,0,127> range, so it stores as -128 and CWrkVariable::Work()
 * -- which reads mValue signed -- clamps it to 0 on the very next tick.  The
 * old number is therefore drawn at full alpha for exactly one frame and then
 * gone, rather than fading over the ten frames the -12 step implies.  A ROM
 * bug; reproduced as found. */
#define BS_SCORE_FULL_ALPHA ((char)128)
#define BS_COMBO_FADE_IN    0x0f
#define BS_COMBO_FADE_OUT (-0x0f)

/* How long the whole readout holds after the last line lands, before the
 * scores fade.  The combo path uses the shorter one. */
#define BS_FADEOUT_WAIT     40
#define BS_FADEOUT_WAIT_CB  20

/* How much bigger a value is at the moment it appears, as a fraction. */
#define BS_SCORE_POP        0.099999994f    /* lit4 3ed910, one ulp below 0.1 */
#define BS_COMBO_POP        0.19999999f     /* lit4 3ed914, one ulp below 0.2 */
#define BS_COMBO_NUM_POP    0.099999994f    /* lit4 3ed918 */
#define BS_COMBO_CHAR_POP   0.099999994f    /* lit4 3ed91c */

/* Digit pitch for both score readouts. */
#define BS_SCORE_WIDTH      14

/* CWrkVariable::GetState() == 3 is "falling"; the combo mark stretches
 * sideways on the way out rather than shrinking. */
#define BS_STATE_FALLING    3

/* Language 4 is Italian, whose word is wider; both combo plates shift for it. */
#define BS_LANG_ITALIAN     4

/* Additive, and depth-masked like the rest of the finder HUD. */
#define BS_ZBUF_NO_WRITE    0x000000010a000118ULL
#define BS_ALPHA_ADD        0x48

/* Where the combo mark and its word start, indexed by "is the language
 * Italian". */
static MyPoint aComboNumPoint[2]  = { { 49 }, { 118 } };    /* sdata 3ef570 */
static MyPoint aComboCharPoint[2] = { { 63 }, {  49 } };    /* sdata 3ef578 */

/* Which two n_finder_dat[] records spell a bonus's name, per language and per
 * bonus index.  Every line is two pieces -- a leading word and a trailing one
 * -- which is why they come in pairs and why CBonusShotOne::Draw() places the
 * second one off the width of the first. */
                                                            /* rdata 3a1a88 */
static const SHOT_NAME_TEX aShotTexTbl[5][BS_LINE_MAX] =
{
    { { 77, 76}, { 74, 76}, { 79, 76}, { 73, 76}, { 78, 75},
      { 80, 76}, { 81, 76}, { 80, 75}, { 81, 75} },
    { { 85, 86}, { 85, 83}, { 85, 88}, { 85, 82}, { 84, 87},
      { 89, 85}, { 90, 85}, { 84, 89}, { 84, 90} },
    { { 95, 94}, { 92, 94}, { 97, 94}, { 91, 94}, { 96, 93},
      { 98, 94}, { 99, 94}, { 98, 93}, { 99, 93} },
    { {103,104}, {103,101}, {103,106}, {103,100}, {105,102},
      {103,107}, {103,108}, {102,107}, {102,108} },
    { {112,113}, {112,110}, {112,115}, {112,109}, {111,114},
      {116,112}, {117,112}, {118,111}, {119,111} },
};

/* ==========================================================================
 *  CBonusShotOne -- one line of the breakdown
 * ======================================================================== */

void CBonusShotOne::Init(void)
{
    mAlpha.Init();
    mUnderLineAlpha.Init();
}

/* Park the line on row iYPos with its text invisible and off to the right,
 * and start the underline growing.  mYPosSave is the row the underline stays
 * on when the text later slides away. */
void CBonusShotOne::InReqUnderLine(int iYPos)                           /* 16 */
{
    mXOffset.Set(BS_SLIDE_X);
    mAlpha.Init();
    mYPos.Set((short)iYPos);
    mYPosSave = (short)iYPos;

    mUnderLineAlpha.Set(0);
    mUnderLineAlpha.SetAddVal(BS_LINE_FADE_IN);
    mUnderLineScale.Set(0);
    mUnderLineScale.SetAddVal(BS_LINE_SCALE_SPD);
}

void CBonusShotOne::InReq(void)                                         /* 24 */
{
    mXOffset.Fade(0, BS_SLIDE_TIME);                                    /* 25 */
    mAlpha.SetAddVal(BS_TEXT_FADE_IN);
}

void CBonusShotOne::OutReq(int iTargetYPos)                             /* 28 */
{
    mAlpha.SetAddVal(BS_TEXT_FADE_OUT);
    mYPos.Fade((short)iTargetYPos, BS_OUT_TIME);                        /* 30 */
    mUnderLineAlpha.SetAddVal(BS_LINE_FADE_OUT);
}

void CBonusShotOne::Work(void)                                          /* 33 */
{
    mYPos.Work();                                                       /* 34 */
    mXOffset.Work();                                                    /* 35 */
    mAlpha.Work();                                                      /* 36 */
    mUnderLineAlpha.Work();                                             /* 37 */
    mUnderLineScale.Work();                                             /* 38 */
}

/* Each of the two text pieces is drawn twice, once at +mXOffset and once at
 * -mXOffset, so the line reads as two halves converging on their final
 * position as the offset fades to zero. */
void CBonusShotOne::Draw(int iOffX, int iOffY, int iPreSprtDat,
                         int iAfterSprtDat)                             /* 42 */
{
    DISP_SPRT ds;
    float     fXScl;

    CopySprDToSpr(&ds, &n_finder_dat[iPreSprtDat]);                     /* 46 */
    ds.zbuf   = BS_ZBUF_NO_WRITE;                                       /* 47 */
    ds.alphar = BS_ALPHA_ADD;                                           /* 48 */
    ds.alpha  = (u_char)mAlpha.Get();
    ds.x     += (float)(iOffX + mXOffset.Get());
    ds.y     += (float)(iOffY + mYPos.Get());
    DispSprD(&ds);                                                      /* 52 */

    ds.x += (float)(mXOffset.Get() * -2);
    ds.y += 0.0f;
    DispSprD(&ds);                                                      /* 54 */

    CopySprDToSpr(&ds, &n_finder_dat[iAfterSprtDat]);                   /* 57 */
    ds.zbuf   = BS_ZBUF_NO_WRITE;                                       /* 58 */
    ds.alphar = BS_ALPHA_ADD;                                           /* 59 */
    ds.alpha  = (u_char)mAlpha.Get();
    ds.x      = (float)(iOffX + n_finder_dat[iPreSprtDat].w
                        + mXOffset.Get() + BS_TEXT_GAP
                        + n_finder_dat[iPreSprtDat].x);                 /* 63 */
    ds.y     += (float)(iOffY + mYPos.Get());
    DispSprD(&ds);                                                      /* 64 */

    ds.x += (float)(mXOffset.Get() * -2);
    ds.y += 0.0f;
    DispSprD(&ds);                                                      /* 66 */

    /* The underline.  fXScl divided by itself is 1.0 whatever the scale
     * counter holds -- a ROM bug: mUnderLineScale ramps 0..100 and clearly
     * meant to drive a width here, but the divisor is the same expression as
     * the dividend, so the rule is always drawn full length (and NaN-wide on
     * any frame the counter is still zero).  Reproduced as found. */
    fXScl = (float)mUnderLineScale.Get();

    CopySprDToSpr(&ds, &n_finder_dat[FD_BONUS_UNDERLINE]);              /* 71 */
    ds.zbuf   = BS_ZBUF_NO_WRITE;                                       /* 72 */
    ds.alphar = BS_ALPHA_ADD;                                           /* 73 */
    ds.alpha  = (u_char)mUnderLineAlpha.Get();
    ds.x     += (float)iOffX;
    ds.y     += (float)(iOffY + mYPosSave);                             /* 75 */
    ds.scw    = fXScl / fXScl;
    ds.sch    = 1.0f;
    ds.csx    = ds.x + (float)(ds.w / 2);
    ds.csy    = ds.y + (float)(ds.h / 2);                               /* 76 */
    DispSprD(&ds);                                                      /* 77 */
}

/* ==========================================================================
 *  CBonusShot -- the whole breakdown
 * ======================================================================== */

/* Pack the bonuses the shot actually earned into the first mDispNum lines and
 * arm the sequence.  iBaseScore is the running total both readouts start on;
 * the per-bonus scores are added to mNewScore one at a time as each line
 * leaves. */
void CBonusShot::Req(int iBaseScore, BONUS_SHOT_SCORE BonusScore,
                     int iWaitTime)                                     /* 199 */
{
    short sWait;

    mOldScore = iBaseScore;
    mNewScore = iBaseScore;                                             /* 201 */
    mBonus    = BonusScore;                                             /* 202 */
    mDispNum  = 0;                                                      /* 203 */

    mReqUnderLineCnt   = -1;                                            /* 207 */
    mReqUnderLineTimer = 0;                                             /* 208 */
    mInReqCnt          = -1;                                            /* 209 */
    mInReqTimer        = 0;                                             /* 210 */
    mOutReqCnt         = -1;                                            /* 211 */
    mOutReqTimer       = 0;                                             /* 212 */

    mNewScoreAlpha.Init();
    mOldScoreAlpha.Init();
    mScorePtsAlpha.Init();
    mComboAlpha.Init();
    mFadeOutWaiter.Reset();

    for (int i = 0; i < BS_LINE_MAX; i++)                               /* 224 */
    {
        if (mBonus.mScore[i] != 0)
        {
            mAnim[mDispNum].mIndex.Set((char)i);
            mDispNum++;                                                 /* 227 */
        }
    }                                                                   /* 229 */

    /* A combo mark has to come and go before the lines start. */
    sWait = (short)iWaitTime;
    if (mBonus.mComboNum != 0)                                          /* 233 */
    {
        sWait = (short)(iWaitTime + BS_TIME_COMBO_EXTRA);
    }

    mTimer = (short)(sWait + BS_TIME_BASE);                             /* 239 */
}

/* One line every fourth frame, until mDispNum of them have gone out. */
void CBonusShot::ReqUnderLineWrk(void)                                  /* 247 */
{
    char cnt = mReqUnderLineCnt;

    if (cnt >= 0)                                                       /* 249 */
    {
        if ((mReqUnderLineTimer & BS_STEP_MASK) == 0)                   /* 250 */
        {
            mReqUnderLineCnt = cnt + 1;                                 /* 251 */
            mAnim[cnt].InReqUnderLine((int)mReqUnderLineCnt << BS_ROW_PITCH);
            cnt = mReqUnderLineCnt;
        }

        mReqUnderLineTimer++;                                           /* 253 */

        if (cnt >= mDispNum)                                            /* 254 */
        {
            mReqUnderLineCnt = -1;                                      /* 255 */
        }
    }
}

void CBonusShot::InReqWrk(void)                                         /* 261 */
{
    char cnt = mInReqCnt;

    if (cnt >= 0)                                                       /* 263 */
    {
        if ((mInReqTimer & BS_STEP_MASK) == 0)                          /* 264 */
        {
            mInReqCnt = cnt + 1;                                        /* 265 */
            mAnim[cnt].InReq();
            cnt = mInReqCnt;
        }

        mInReqTimer++;                                                  /* 267 */

        if (cnt >= mDispNum)                                            /* 268 */
        {
            mInReqCnt = -1;                                             /* 269 */
        }
    }
}

/* As each line leaves, its score joins the running total: the old number
 * fades out at full brightness and the new one fades in from nothing, which
 * is what makes the total look like it counted up. */
void CBonusShot::OutReqWrk(void)                                        /* 274 */
{
    char cnt = mOutReqCnt;

    if (cnt >= 0)                                                       /* 276 */
    {
        if ((mOutReqTimer & BS_STEP_MASK) == 0)                         /* 277 */
        {
            mNewScore += mBonus.mScore[mAnim[cnt].mIndex.Get()];

            mNewScoreAlpha.Set(0);
            mNewScoreAlpha.SetAddVal(BS_SCORE_FADE_IN);
            mOldScoreAlpha.Set(BS_SCORE_FULL_ALPHA);
            mOldScoreAlpha.SetAddVal(BS_SCORE_FADE_OUT);
            mScorePtsAlpha.SetAddVal(BS_SCORE_FADE_IN);
            mFadeOutWaiter.Wait(BS_FADEOUT_WAIT);

            mAnim[cnt].OutReq(0);                                       /* 288 */
            cnt = mOutReqCnt + 1;
            mOutReqCnt = cnt;                                           /* 289 */
        }

        mOutReqTimer++;                                                 /* 291 */

        if (cnt >= mDispNum)                                            /* 292 */
        {
            mOutReqCnt = -1;                                            /* 293 */
        }
    }
}

void CBonusShot::Work(void)                                             /* 299 */
{
    ReqUnderLineWrk();                                                  /* 300 */
    InReqWrk();                                                         /* 302 */
    OutReqWrk();                                                        /* 304 */

    if (mBonus.mComboNum != 0)                                          /* 307 */
    {
        /* With a combo the lines do not stagger: the whole set goes out at
         * once, and the combo mark comes and goes around them. */
        if (mTimer.Work())                                              /* 309 */
        {
            for (int i = 0; i < mDispNum; i++)                          /* 310 */
            {
                mAnim[i].OutReq(0);
                mNewScore += mBonus.mScore[mAnim[i].mIndex.Get()];
            }                                                           /* 313 */

            mNewScoreAlpha.Set(0);
            mNewScoreAlpha.SetAddVal(BS_SCORE_FADE_IN);
            mOldScoreAlpha.Set(BS_SCORE_FULL_ALPHA);
            mOldScoreAlpha.SetAddVal(BS_SCORE_FADE_OUT);
            mScorePtsAlpha.SetAddVal(BS_SCORE_FADE_IN);
            mFadeOutWaiter.Wait(BS_FADEOUT_WAIT_CB);
        }
        else if (mTimer.Get() == BS_T_COMBO_OUT)
        {
            mComboAlpha.SetAddVal(BS_COMBO_FADE_OUT);
        }
        else if (mTimer.Get() == BS_T_COMBO_TEXT_IN)
        {
            for (int i = 0; i < mDispNum; i++)                          /* 329 */
            {
                mAnim[i].InReq();
            }                                                           /* 331 */
        }
        else if (mTimer.Get() == BS_T_COMBO_UNDERLINE)                  /* 335 */
        {
            for (int i = 0; i < mDispNum; i++)                          /* 337 */
            {
                mAnim[i].InReqUnderLine(i << BS_ROW_PITCH);
            }
        }
        else if (mTimer.Get() == BS_T_COMBO_IN)
        {
            mComboAlpha.SetAddVal(BS_COMBO_FADE_IN);
        }
    }
    else
    {
        if (mTimer.Work())                                              /* 346 */
        {
            mOutReqCnt = 0;                                             /* 347 */
        }
        else if (mTimer.Get() == BS_T_IN)
        {
            mInReqCnt = 0;                                              /* 351 */
        }
        else if (mTimer.Get() == BS_T_UNDERLINE)
        {
            mReqUnderLineCnt = 0;
        }
    }

    mComboAlpha.Work();                                                 /* 360 */

    for (int i = 0; i < mDispNum; i++)                                  /* 363 */
    {
        mAnim[i].Work();
    }                                                                   /* 365 */

    mNewScoreAlpha.Work();                                              /* 368 */
    mOldScoreAlpha.Work();                                              /* 369 */
    mScorePtsAlpha.Work();                                              /* 370 */

    /* The hold after the last line: everything fades together. */
    if (mFadeOutWaiter.Work())                                          /* 372 */
    {
        mNewScoreAlpha.SetAddVal(BS_SCORE_FADE_OUT);
        mOldScoreAlpha.SetAddVal(BS_SCORE_FADE_OUT);
        mScorePtsAlpha.SetAddVal(BS_SCORE_FADE_OUT);
    }
}

void CBonusShot::Draw(int fndr_mx, int fndr_my)                         /* 398 */
{
    for (int i = 0; i < mDispNum; i++)                                  /* 401 */
    {
        mAnim[i].Draw(fndr_mx, fndr_my,
                      aShotTexTbl[GetLanguage()][mAnim[i].mIndex.Get()].mPreTexNo,
                      aShotTexTbl[GetLanguage()][mAnim[i].mIndex.Get()].mAfterTexNo);
                                                                        /* 405, 406 */
    }

    /* The new total pops in a tenth bigger than final size and settles as its
     * alpha comes up; the old one is drawn under it at plain scale. */
    SetNumerousDisp(&n_finder_dat[FD_SCORE_DIGIT], mNewScore,
                    mNewScoreAlpha.Get(), BS_SCORE_WIDTH, fndr_mx, fndr_my,
                    (1.0f - (float)mNewScoreAlpha.Get()
                            / (float)mNewScoreAlpha.GetMax())
                    * BS_SCORE_POP + 1.0f, 0, 1);                       /* 415 */
    SetNumerousDisp(&n_finder_dat[FD_SCORE_DIGIT], mOldScore,
                    mOldScoreAlpha.Get(), BS_SCORE_WIDTH, fndr_mx, fndr_my,
                    1.0f, 0, 1);                                        /* 418 */

    {
        DISP_SPRT ds;

        CopySprDToSpr(&ds, &n_finder_dat[FD_SCORE_PTS]);                /* 421 */
        ds.zbuf   = BS_ZBUF_NO_WRITE;                                   /* 422 */
        ds.alphar = BS_ALPHA_ADD;                                       /* 423 */
        ds.x     += (float)fndr_mx;
        ds.y     += (float)fndr_my;                                     /* 424 */
        ds.alpha  = (u_char)mScorePtsAlpha.Get();                       /* 425 */
        DispSprD(&ds);                                                  /* 426 */
    }

    {
        DISP_SPRT ds;
        float     fScale = (float)mComboAlpha.Get()
                           / (float)mComboAlpha.GetMax();

        if (mBonus.mComboNum == 1)                                      /* 436 */
        {
            /* The plain mark: it shrinks onto its final size as it fades in. */
            float fScl = (1.0f - fScale) * BS_COMBO_POP + 1.0f;         /* 437 */

            CopySprDToSpr(&ds, &n_finder_dat[FD_COMBO_X1]);             /* 439 */
            ds.zbuf   = BS_ZBUF_NO_WRITE;                               /* 440 */
            ds.alphar = BS_ALPHA_ADD;                                   /* 441 */
            ds.x     += (float)fndr_mx;
            ds.y     += (float)fndr_my;                                 /* 442 */
            ds.scw    = fScl;
            ds.sch    = fScl;
            ds.csx    = ds.x + (float)(ds.w / 2);
            ds.csy    = ds.y + (float)(ds.h / 2);                       /* 443 */
            ds.alpha  = (u_char)mComboAlpha.Get();
            DispSprD(&ds);                                              /* 445 */
        }
        else if (mBonus.mComboNum > 1)                                  /* 447 */
        {
            /* x2..x9, plus the word beside it.  Coming in, both squash
             * vertically; going out they stretch sideways instead. */
            CopySprDToSpr(&ds,
                          &n_finder_dat[FD_COMBO_X2 + mBonus.mComboNum]); /* 451 */
            ds.zbuf   = BS_ZBUF_NO_WRITE;                               /* 452 */
            ds.alphar = BS_ALPHA_ADD;                                   /* 453 */
            ds.x      = (float)aComboNumPoint[GetLanguage() == BS_LANG_ITALIAN].x
                      + (float)fndr_mx;                                 /* 454 */
            ds.y     += (float)fndr_my;                                 /* 455 */

            if (mComboAlpha.GetState() == BS_STATE_FALLING)             /* 456 */
            {
                ds.sch = 1.0f;
                ds.scw = (1.0f - fScale) * BS_COMBO_NUM_POP + 1.0f;     /* 457 */
            }
            else
            {
                ds.scw = 1.0f;
                ds.sch = fScale;                                        /* 459 */
            }

            ds.csx   = ds.x + (float)(ds.w / 2);
            ds.csy   = ds.y + (float)(ds.h / 2);
            ds.alpha = (u_char)mComboAlpha.Get();                       /* 461 */
            DispSprD(&ds);                                              /* 462 */

            CopySprDToSpr(&ds, &n_finder_dat[FD_COMBO_CHAR]);           /* 465 */
            ds.zbuf   = BS_ZBUF_NO_WRITE;                               /* 466 */
            ds.alphar = BS_ALPHA_ADD;                                   /* 467 */
            ds.x      = (float)aComboCharPoint[GetLanguage() == BS_LANG_ITALIAN].x
                      + (float)fndr_mx;                                 /* 468 */
            ds.y     += (float)fndr_my;                                 /* 469 */

            if (mComboAlpha.GetState() == BS_STATE_FALLING)             /* 470 */
            {
                ds.sch = 1.0f;
                ds.scw = (1.0f - fScale) * BS_COMBO_CHAR_POP + 1.0f;    /* 471 */
            }
            else
            {
                ds.scw = 1.0f;
                ds.sch = fScale;                                        /* 473 */
            }

            ds.csx   = ds.x + (float)(ds.w / 2);
            ds.csy   = ds.y + (float)(ds.h / 2);
            ds.alpha = (u_char)mComboAlpha.Get();                       /* 475 */
            DispSprD(&ds);                                              /* 476 */
        }
    }
}

void CBonusShot::Init(void)                                             /* 483 */
{
    mDispNum = 0;                                                       /* 484 */

    for (int i = 0; i < BS_LINE_MAX; i++)                               /* 485 */
    {
        mAnim[i].Init();
    }                                                                   /* 487 */

    mScorePtsAlpha.Init();
    mNewScoreAlpha.Init();
    mOldScoreAlpha.Init();
    mComboAlpha.Init();
    mFadeOutWaiter.Reset();
    mTimer.Reset();

    mReqUnderLineCnt = -1;                                              /* 496 */
    mInReqCnt        = -1;                                              /* 497 */
    mOutReqCnt       = -1;                                              /* 498 */
}
