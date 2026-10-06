/* ==========================================================================
 *  ingame/photo/camera_power_up.h
 *
 *  What the camera has been upgraded with, and what that is worth.
 *
 *  Three gems and ten sub-function gems are the currency the upgrade menu
 *  spends; the two grades are what the rest of the game reads.  Everything the
 *  grades are worth lives in the three tables below, all indexed 0..3:
 *
 *    aRadiusTbl    the capture ring's size, 0.85 .. 1.1 of stock.
 *    aDistanceTbl  how far the spirit gauge still registers a ghost, in world
 *                  units.  SpiritGageCalc() gates the whole gauge on it and
 *                  fades the fill linearly from 800 out to this value.
 *    aDmgTbl       the damage multiplier at that grade.  PhotoDmgChkSub2()
 *                  multiplies it into the film's own damage.
 *
 *  This really is its own header in the ROM, not part of m_plyr_camera.h:
 *  player.o's PhotoDmgChkSub2() and SpiritGageCalc() carry
 *  `SOL ../photo/camera_power_up.h` stabs for the three inline accessors, which
 *  is what places them -- and their line numbers -- below.
 *
 *  Reconstructed from the Feb 6 2004 prototype (SLES_523.84),
 *  camera_power_up.o.
 * ======================================================================== */

#ifndef _INGAME_PHOTO_CAMERA_POWER_UP_H
#define _INGAME_PHOTO_CAMERA_POWER_UP_H

#include "../../common/variable.h"      /* CVariable / BIT_FLAGS */

struct CCameraPowerUp               /* 0x20 */
{
    /* 0x00 */ CVariable<char, 0, 3> mSensitiveGrade;
    /* 0x01 */ CVariable<char, 0, 3> mRadiusGrade;
    /* 0x02 */ CVariable<char, 0, 3> mRadiusGem;
    /* 0x03 */ CVariable<char, 0, 3> mSensiteiveGem;
    /* 0x04 */ CVariable<char, 0, 3> mAccumGem;
    /* 0x05 */ CVariable<char, 0, 3> mSubFuncGem[10];
    /* 0x10 */ BIT_FLAGS<4>    mAdditionFlg;
    /* 0x14 */ BIT_FLAGS<4>    mCamPartsFlg;
    /* 0x18 */ BIT_FLAGS<4>    mCamPartsSetFlg;
    /* 0x1c */ BIT_FLAGS<10>   mTemperedRenzFlg;

private:
    /* camera_power_up.o's .rdata, reproduced in camera_power_up.c.  Private in
     * the ROM -- everything outside goes through the four accessors. */
    static float aRadiusTbl[4];                     /* rdata 3a1c88 */
    static float aDistanceTbl[4];                   /* rdata 3a1c68 */
    static float aDmgTbl[4];                        /* rdata 3a1c78 */

public:
    /* The finder's capture ring in screen units.  108 is the stock radius, so
     * GetRadiusRate() is what the lens actually changes; photo_dat.c,
     * EneFrameHitChk() and PlayerTakePictJob() all take the ring from here. */
    float GetRadius(void)                                                    /* 36 */
    {
        return GetRadiusRate() * 108.0f;                                     /* 37 */
    }

    /* Out of line in the ROM -- camera_power_up.o's only non-Init export. */
    float GetRadiusRate(void);

    float GetDistance(void)                                                  /* 41 */
    {
        return aDistanceTbl[mSensitiveGrade.Get()];                          /* 42 */
    }

    float GetDmgRate(void)                                                   /* 44 */
    {
        return aDmgTbl[mSensitiveGrade.Get()];                               /* 45 */
    }

    void Init(void);
    static void Init(CCameraPowerUp *self) { if (self) self->Init(); }

    /* Maxes out every upgrade at once -- the debug "give me everything"
     * combo in EachDebugMain() is the only caller. */
    void AllRelease(void);
    static void AllRelease(CCameraPowerUp *self) { if (self) self->AllRelease(); }
};

#endif /* _INGAME_PHOTO_CAMERA_POWER_UP_H */
