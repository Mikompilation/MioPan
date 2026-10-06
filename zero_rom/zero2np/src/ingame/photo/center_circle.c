// FILE: /home/zero_rom/zero2np/src/ingame/photo/center_circle.c
//
// The coloured ring at the centre of the viewfinder, and the ripples that run
// out of it.
//
// Four flags say what the finder is looking at -- enemy.c raises the auto flag
// for a passive ghost within reach, the battle flag for a hostile one and the
// enemy-catch flag for one already inside the ring; photo_dat.c raises the
// hint flag for a photographable object.  Work() collapses them to one of
// three modes, and the ring's colour and brightness fade towards that mode's
// entry in aRgb / aMainAlpha over CC_FADE_TIME frames.  Nothing snaps.
//
// The ripples are four copies of the same sprite on staggered scale ramps:
// each grows CC_RIPPLE_SCALE past the ring and fades out as it goes, and the
// four are seeded a quarter of the range apart so they leave evenly.  They
// only run outward while the ring is in hint/auto mode -- otherwise
// mRippleAlpha's step is negative and they fade away where they are.
//
// The bodies moved here from n_plyr_camera.c, which is where an earlier pass
// parked them; center_circle.o owns all nine.
//
// Reconstructed from the Feb 6 2004 prototype (SLES_523.84), center_circle.o.
// All nine ZERO2.MAP .text symbols plus both colour tables.

#include "m_plyr_camera.h"
#include "n_finder_dat.h"                       /* n_finder_dat            */
#include "../../graphics/graph2d/g2d_draw.h"    /* DISP_SPRT / DispSprD    */

/* The ring sprite; the ripples are the same one at a larger scale. */
#define FD_CENTER_CIRCLE    0x26

/* How many ripples are in flight at once -- also the divisor that spaces their
 * starting scales evenly across the range. */
#define CC_RIPPLE_NUM       4

/* Frames for a ripple to cross the whole scale range, and for the ring's
 * colour to reach a new mode. */
#define CC_RIPPLE_TIME      45
#define CC_FADE_TIME        15

/* Frames for the ripple alpha to reach either end. */
#define CC_RIPPLE_FADE      10

/* How far past the ring a ripple grows, as a fraction of the master scale. */
#define CC_RIPPLE_SCALE     0.14999999f     /* lit4 3ed920, one ulp below 0.15 */

/* The ring's own alpha is a percentage. */
#define CC_ALPHA_PERCENT    100

/* The modes Work() collapses the flags into. */
#define CC_MODE_IDLE        0
#define CC_MODE_HOSTILE     1
#define CC_MODE_HINT        2

/* Centre of the finder, in finder-local coordinates. */
#define CC_CENTER_X         319
#define CC_CENTER_Y         225

/* Additive, and depth-masked like the rest of the finder HUD. */
#define CC_ZBUF_NO_WRITE    0x000000010a000118ULL
#define CC_ALPHA_ADD        0x48
#define CC_TEX1             0x0000000000000161ULL

/* Idle amber, hostile red, hint blue.  Entry 3 is a copy of entry 2 that
 * nothing selects -- Work() never produces a 3. */
u_char CCenterCircle::aRgb[4][3] =                          /* data 2d8d10 */
{
    { 0xbc, 0x8c, 0x3d },
    { 0xd4, 0x15, 0x03 },
    { 0x0c, 0x81, 0xd5 },
    { 0x0c, 0x81, 0xd5 },
};

char CCenterCircle::aMainAlpha[4] =                         /* sdata 3ef638 */
{
    40, 75, 100, 100
};

void CCenterCircle::Init(void)                                          /* 25 */
{
    int iWidth = 0;                                                     /* 29 */

    /* Every statement in the loop goes through an inline accessor, so its own
     * line is swallowed by variable.h; only the loop header is measurable. */
    for (int i = 0; i < CC_RIPPLE_NUM; i++)                             /* 32 */
    {
        maScale[i].Set((char)(iWidth / CC_RIPPLE_NUM));
        maScale[i].SetAddVal((char)(maScale[i].GetMax() / CC_RIPPLE_TIME));
        iWidth += maScale[i].GetWidth();
    }

    mRippleAlpha.Init();
    mR.Set(aRgb[CC_MODE_IDLE][0]);
    mG.Set(aRgb[CC_MODE_IDLE][1]);
    mB.Set(aRgb[CC_MODE_IDLE][2]);
    mMainAlpha.Set(aMainAlpha[CC_MODE_IDLE]);
}

/* Genuinely empty in the ROM -- eight bytes, `jr ra` and a `nop`.  The
 * per-frame flags are not cleared here after all; every one of them is
 * re-driven every frame by its owner. */
void CCenterCircle::FrameReset(void)                                    /* 45 */
{
}

/* Also empty.  The mode is derived from the flags in Work(), so nothing is
 * left for a setter to do. */
void CCenterCircle::SetMode(int iMode)                                  /* 49 */
{
    (void)iMode;
}

void CCenterCircle::SetHintFlg(int flg)                                 /* 52 */
{
    mHintFlg = flg & 1;
}

void CCenterCircle::SetAutoFlg(int flg)                                 /* 55 */
{
    mAutoFlg = flg & 1;
}

void CCenterCircle::SetBattleFlg(int flg)                               /* 58 */
{
    mBattleFlg = flg & 1;
}

void CCenterCircle::SetEneCatchFlg(int flg)                             /* 61 */
{
    mEnemyFlg = flg & 1;
}

void CCenterCircle::Work(void)                                          /* 64 */
{
    int mMode;

    /* A ghost already inside the ring beats everything; a hint or an
     * in-reach ghost beats being in a fight. */
    if (mEnemyFlg)                                                      /* 67 */
    {
        mMode = CC_MODE_HOSTILE;
    }
    else if (mHintFlg || mAutoFlg)                                      /* 70 */
    {
        mMode = CC_MODE_HINT;
    }
    else
    {
        mMode = mBattleFlg;                                             /* 73 */
    }

    /* Fade2() is a no-op when the target has not changed, so re-issuing the
     * same request every frame does not restart the ramp. */
    mR.Fade2(aRgb[mMode][0], CC_FADE_TIME);
    mG.Fade2(aRgb[mMode][1], CC_FADE_TIME);
    mB.Fade2(aRgb[mMode][2], CC_FADE_TIME);
    mMainAlpha.Fade2(aMainAlpha[mMode], CC_FADE_TIME);

    /* The ripples only run outward while the ring is calling attention to
     * something; otherwise they bleed away.  Mode 3 is unreachable -- the
     * ladder above never produces it -- but the ROM tests for it, so the
     * literals are kept rather than folded to a single compare. */
    if (mMode == 2 || mMode == 3)                                       /* 86 */
    {
        mRippleAlpha.SetAddVal((short)(mRippleAlpha.GetMax() / CC_RIPPLE_FADE));
    }
    else
    {
        mRippleAlpha.SetAddVal((short)(-mRippleAlpha.GetMax() / CC_RIPPLE_FADE));
    }

    for (int i = 0; i < CC_RIPPLE_NUM; i++)                             /* 93 */
    {
        maScale[i].LoopWork();                                          /* 94 */
    }                                                                   /* 95 */

    mR.Work();                                                          /* 96 */
    mG.Work();                                                          /* 97 */
    mB.Work();                                                          /* 98 */
    mMainAlpha.Work();                                                  /* 99 */
    mRippleAlpha.Work();                                                /* 100 */
}

void CCenterCircle::Draw(int fndr_mx, int fndr_my, int iMasterAlpha,
                         float fMasterScale)                            /* 108 */
{
    DISP_SPRT ds;

    CopySprDToSpr(&ds, &n_finder_dat[FD_CENTER_CIRCLE]);                /* 122 */
    ds.zbuf   = CC_ZBUF_NO_WRITE;                                       /* 123 */
    ds.alphar = CC_ALPHA_ADD;                                           /* 124 */
    ds.r      = (u_char)mR.Get();
    ds.g      = (u_char)mG.Get();
    ds.b      = (u_char)mB.Get();
    ds.alpha  = (u_char)(mMainAlpha.Get() * iMasterAlpha / CC_ALPHA_PERCENT);
    ds.x     += (float)fndr_mx;
    ds.y     += (float)fndr_my;                                         /* 127 */
    ds.csx    = (float)(fndr_mx + CC_CENTER_X);
    ds.csy    = (float)(fndr_my + CC_CENTER_Y);
    ds.scw    = fMasterScale;
    ds.sch    = fMasterScale;                                           /* 128 */
    ds.tex1   = CC_TEX1;                                                /* 130 */
    DispSprD(&ds);                                                      /* 131 */

    for (int j = 0; j < CC_RIPPLE_NUM; j++)                             /* 138 */
    {
        /* Scale runs from the ring's own size out to CC_RIPPLE_SCALE past
         * it, and the alpha runs the other way, so a ripple thins out as it
         * grows.  Both come off the same counter. */
        float fScale = ((float)maScale[j].Get() / (float)maScale[j].GetMax())
                       * CC_RIPPLE_SCALE * fMasterScale + fMasterScale;

        CopySprDToSpr(&ds, &n_finder_dat[FD_CENTER_CIRCLE]);            /* 146 */
        ds.zbuf   = CC_ZBUF_NO_WRITE;                                   /* 147 */
        ds.alphar = CC_ALPHA_ADD;                                       /* 148 */
        ds.alpha  = (u_char)(iMasterAlpha * mRippleAlpha.Get()
                             / mRippleAlpha.GetMax()
                             * (maScale[j].GetMax() - maScale[j].Get())
                             / maScale[j].GetMax());                    /* 149 */
        ds.r      = aRgb[CC_MODE_HINT][0];
        ds.g      = aRgb[CC_MODE_HINT][1];
        ds.b      = aRgb[CC_MODE_HINT][2];                              /* 150 */
        ds.x     += (float)fndr_mx;
        ds.y     += (float)fndr_my;                                     /* 151 */
        ds.csx    = (float)(fndr_mx + CC_CENTER_X);
        ds.csy    = (float)(fndr_my + CC_CENTER_Y);
        ds.scw    = fScale;
        ds.sch    = fScale;                                             /* 152 */
        ds.tex1   = CC_TEX1;                                            /* 154 */
        DispSprD(&ds);                                                  /* 155 */
    }                                                                   /* 156 */
}
