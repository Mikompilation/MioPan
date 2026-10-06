/* ==========================================================================
 *  ingame/photo/spirit_gage.h
 *
 *  The per-ghost spirit gauge -- the ring of blips that fills as the camera
 *  drains a ghost.  One lives inside every ENE_WRK; the camera keeps a pointer
 *  to whichever ghost is currently nearest (CNPlyrCamera::mpSpiritGage) and
 *  draws that one over the backing plate DrawSpiritGageBase() puts down.
 *
 *  Split out of m_plyr_camera.h because enemy.h needs the class by value and
 *  must not drag the whole camera in.
 *
 *  Reconstructed from the Feb 6 2004 prototype (SLES_523.84), spirit_gage.o.
 * ======================================================================== */

#ifndef _INGAME_PHOTO_SPIRIT_GAGE_H
#define _INGAME_PHOTO_SPIRIT_GAGE_H

#include "../../common/variable.h"      /* CWrkVariable / CMIN_MAX /
                                         * SHUTTER_CHANCE_STATE            */

struct CSpiritGage                  /* 0xc */
{
    /* 0x0 */ int mPercent;
    /* 0x4 */ CWrkVariable<char, 0, 60> mAlpha;
    /* 0x8 */ int mFlg;

    /* The shot multiplier per shutter-chance state, as {min, max} of the ramp
     * CalcDamageRate() walks with the gauge's own fill.  .rodata, so const --
     * globals.txt never prints const, but the section is the tell.
     *
     * NONE runs 0.3 .. 1.0, NORMAL 1.8 .. 2.0 and SP is a flat 2.0. */
    static const CMIN_MAX<float> aDmgMultipleTbl[SHUTTER_CHANCE_STATE_MAX];

    /* Zeroes the gauge.  enemy.c's global constructor runs this over all ten
     * slots, which is why the ROM has an out-of-line copy at all. */
    void Init(void);

    /* Raise / drop the gauge as this ghost becomes or stops being the one the
     * camera is pointed at.  NearestBattleEneDoJob() drives both; each is a
     * single mAlpha.SetAddVal(), so the ring fades in over three frames and
     * out over three. */
    void FadeIn(void);
    void FadeOut(void);

    /* The per-frame fill.  player.c's SpiritGageCalc() runs this over every
     * acting ghost: fDistanceRate is 1.0 inside 800 units falling to 0 at the
     * lens's reach, fCenterRate 1.0 dead-centre in the capture ring falling to
     * 0 at its rim, and fAlphaRate the ghost's own transparency.  SState is
     * the shutter chance the whole field is in and iMinPercent the floor the
     * loaded film guarantees.  A ghost that fails the range test gets
     * (0,0,0,NONE,0), which is what empties the ring again. */
    void Work(float fDistanceRate, float fCenterRate, float fAlphaRate,
              SHUTTER_CHANCE_STATE SPState, int iMinPercent);

    /* How much of the film's damage this shot earns, as a multiplier.  Zero
     * means "no hit at all", which is how PhotoDmgChkSub2() tells a
     * sub-function-only shot from a damaging one -- and note the table's own
     * floor is 0.3, so only a gauge that never came up returns zero. */
    float CalcDamageRate(SHUTTER_CHANCE_STATE SPState);

    /* How full the gauge is, 0..100.  Inline in the ROM -- n_plyr_camera.o's
     * Main() expands it to a bare load of offset 0 -- and it is the only thing
     * the camera reads out of the tracked ghost's gauge: the drain noise's
     * volume and pitch are both computed from it. */
    int GetPercent(void) const { return mPercent; }

    /* The gauge draws itself into the finder; CNPlyrCamera::Draw() calls this
     * for whichever ghost mpSpiritGage points at, under the same alpha and
     * scale it hands to DrawSpiritGageBase(). */
    void Draw(int fndr_mx, int fndr_my, int iMasterAlpha, float fScale);
};

#endif /* _INGAME_PHOTO_SPIRIT_GAGE_H */
