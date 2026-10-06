// FILE: /home/zero_rom/zero2np/src/ingame/photo/camera_power_up.c
//
// The camera's upgrade state: three .rdata tables and the two functions that
// reset it.  Everything else about the class is inline, in camera_power_up.h.
//
// Reconstructed from the Feb 6 2004 prototype (SLES_523.84), camera_power_up.o.
// All three `.text` exports and all three tables, the latter read out of the
// ELF and verified byte-for-byte.
//
// Two things about Init() are worth reading before the body.  It writes four
// of the five scalar gems/grades *twice* -- once through CVariable::Init()
// (variable.h 19/20) and once through CVariable::Set(0) (variable.h 49) -- and
// mAccumGem only through the second.  That is not a decompiler artefact:
// nine stores are emitted, and this object file's .rodata carries Set()'s
// "Set Value is Illegal" assert strings, which only get there if Set() really
// was expanded here.  The split between the two groups is the one thing not
// pinned exactly; the resulting state is.
//
// Lines 55..67 and 79..84 carry no `; Line` markers of their own, but they are
// not commented-out source: a statement whose entire body is an inlined
// accessor has its own line note superseded by the header's and leaves nothing
// behind.  The gaps are exactly the right size for the statements below.

#include "camera_power_up.h"

/* The capture ring's size relative to stock, by mRadiusGrade.  0.85 and 1.1
 * sit one ulp below the round decimal -- EE GCC truncating the literal, so the
 * exact values are spelled out to keep the bits.  0.9 and 1.0 are exact. */
float CCameraPowerUp::aRadiusTbl[4] =                           /* rdata 3a1c88 */
{
    0.84999996f, 0.9f, 1.0f, 1.0999999f
};

/* How far the spirit gauge still registers a ghost, by mSensitiveGrade. */
float CCameraPowerUp::aDistanceTbl[4] =                         /* rdata 3a1c68 */
{
    1500.0f, 1800.0f, 2000.0f, 2500.0f
};

/* The shot's damage multiplier, by mSensitiveGrade.  All four are exact --
 * 1.3f and 1.8f really do round to the ROM's words. */
float CCameraPowerUp::aDmgTbl[4] =                              /* rdata 3a1c78 */
{
    1.0f, 1.3f, 1.5f, 1.8f
};

/* --------------------------------------------------------------------------
 *  Init
 *
 *  Back to a stock camera.  ingame.c calls it once per new game.
 * ------------------------------------------------------------------------ */
void CCameraPowerUp::Init(void)
{                                                                       /* 54 */
    int i;

    mSensitiveGrade.Init();                               /* variable.h 19/20 */
    mRadiusGrade.Init();
    mRadiusGem.Init();
    mSensiteiveGem.Init();

    mSensitiveGrade.Set(0);                                  /* variable.h 49 */
    mRadiusGrade.Set(0);
    mRadiusGem.Set(0);
    mSensiteiveGem.Set(0);
    mAccumGem.Set(0);

    for (i = 0; i < 10; i++)                                            /* 68 */
    {
        mSubFuncGem[i].Init();                                          /* 70 */
    }

    mAdditionFlg.AllDown();                                 /* variable.h 801 */
    mCamPartsFlg.AllDown();
    mCamPartsSetFlg.AllDown();
    mTemperedRenzFlg.AllDown();
}

/* --------------------------------------------------------------------------
 *  AllRelease
 *
 *  The debug "give me everything" combo.  Note the two *grades* are
 *  deliberately untouched -- only the gems and the four flag sets are raised,
 *  so the menu still has to be visited to spend them.
 * ------------------------------------------------------------------------ */
void CCameraPowerUp::AllRelease(void)
{                                                                       /* 78 */
    int i;

    mRadiusGem.SetMax();                                     /* variable.h 24 */
    mSensiteiveGem.SetMax();
    mAccumGem.SetMax();

    for (i = 0; i < 10; i++)                                            /* 85 */
    {
        mSubFuncGem[i].SetMax();                                        /* 87 */
    }

    mAdditionFlg.AllUp();                               /* variable.h 808/809 */
    mCamPartsFlg.AllUp();
    mCamPartsSetFlg.AllUp();
    mTemperedRenzFlg.AllUp();
}

/* --------------------------------------------------------------------------
 *  GetRadiusRate
 *
 *  The only accessor the ROM emitted out of line, and the one GetRadius()
 *  inlines on top of.  1.0 is the stock ring.
 * ------------------------------------------------------------------------ */
float CCameraPowerUp::GetRadiusRate(void)
{                                                                       /* 99 */
    return aRadiusTbl[mRadiusGrade.Get()];                 /* variable.h 167 */
}
