// FILE: /home/zero_rom/zero2np/src/ingame/photo/filament.c
//
// The camera's viewfinder filament -- the glowing needle that deflects when
// something photographable is in shot.
//
// Three sprites make it up: a dial plate that never moves, and a two-piece
// needle (a bright "core" laid over a broader "whole") whose alpha carries the
// deflection.  There is no rotation and no scaling -- mRate simply scales the
// needle's alpha, so a strong reading reads as a brighter filament rather than
// as a larger angle.  Two independent blink switches per piece, with different
// sweep times, give the irregular flicker of a real tungsten filament.
//
// Everything the ROM's RT (real-time / scripted) path would do is dead in this
// prototype: RTModeOn() and RTModeOff() are empty bodies, so mRTFlg never goes
// up, and rt_ev_wrk / fillament_wrk / mMode / mRTTime are cleared and then
// never read.  FadeIn() and FadeOut() have zero call sites anywhere in the
// loadable segments -- exported and dead, the same pattern as ene_mot_ctrl.o's
// InitEneMotAlgCtrl and fly_ctrl.o's FlyInit.
//
// Reconstructed from the Feb 6 2004 prototype (SLES_523.84), filament.o.

#include <string.h>                             /* memset                      */

#include "filament.h"
#include "../../graphics/graph2d/g2d_draw.h"    /* DISP_SPRT / DispSprD        */
#include "n_finder_dat.h"                       /* n_finder_dat                */

/* The GS register words the needle draws with.  Same pair finder.c and
 * n_plyr_camera.c use: ZBUF with the mask bit set writes no depth, ALPHA 0x44
 * is source-over and 0x48 additive.  The plate is blended, the two needle
 * pieces are added on top of it. */
#define FIL_ZBUF_MASK       0x000000010a000118ULL
#define FIL_ALPHA_BLEND     0x44
#define FIL_ALPHA_ADD       0x48

/* n_finder_dat[] indices.  0x9e is the dial plate the needle sits on; the other
 * four are two variants of the same two-piece needle -- the `_HINT` pair for
 * the hint and passive-ghost deflections, the `_BTL` pair for a fight (and for
 * the scripted RT mode).  159/161 and 160/162 differ only in their v origin, so
 * they are two rows of one sprite sheet. */
#define FD_FILAMENT_PLATE       0x9e
#define FD_FILAMENT_CORE_HINT   0x9f
#define FD_FILAMENT_WHOLE_HINT  0xa0
#define FD_FILAMENT_CORE_BTL    0xa1
#define FD_FILAMENT_WHOLE_BTL   0xa2

/* Added to each blink value before it scales the alpha, so a switch that has
 * decayed to its floor still passes ~80% through rather than going black. */
#define FIL_BLINK_BIAS      115

/* The four flicker switches.  File-scope and *not* static -- ZERO2.MAP lists
 * all four as global symbols in filament.o's .sdata even though nothing outside
 * this file names them.  They hold zero in the ROM image (no constructor, and
 * filament.o has no static-initialisation function at all); CFilament::Init()
 * is what seeds them.
 *
 * The two "core" switches multiply together onto the core sprite and the two
 * "whole" switches onto the body, so each piece flickers on the beat of two
 * sweeps of different length and the result never repeats on a short cycle. */
CBlinkSwitchVariable<char, 90, 118,  6, 90> mBlinkAlphaCore1;   /* sdata 3f0758 */
CBlinkSwitchVariable<char, 90, 112, 11, 90> mBlinkAlphaCore2;   /* sdata 3f0760 */
CBlinkSwitchVariable<char, 90, 116, 13, 90> mBlinkAlphaWhole1;  /* sdata 3f0768 */
CBlinkSwitchVariable<char, 75, 112, 17, 75> mBlinkAlphaWhole2;  /* sdata 3f0770 */

/* --------------------------------------------------------------------------
 *  The scripted override.  Both bodies really are empty in this build -- eight
 *  bytes each, `jr ra` and a bare nop, with nothing hidden in the delay slot.
 *  finder.c's RTFillamentModeOn() / Off() forward the event macro interpreter
 *  onto them, so FILAMENT_CALL and FILAMENT_RELEASE currently do nothing.
 * ------------------------------------------------------------------------ */
void CFilament::RTModeOn(int type, int time)
{
    (void)type; (void)time;                                             /* 28 */
}

void CFilament::RTModeOff(void)
{
                                                                        /* 36 */
}

/* Hands the draw lock to the memory-card block -- four bytes, not the whole
 * object.  Everything else is rebuilt by Init() on load. */
void CFilament::SetSave(MC_SAVE_DATA *save)
{
    save->addr = (u_char *)&mLockCnt;                                   /* 40 */
    save->size = sizeof(mLockCnt);                                      /* 41 */
}

void CFilament::Init(void)
{                                                                       /* 50 */
    memset(&rt_ev_wrk, 0, sizeof(rt_ev_wrk));                           /* 51 */
    memset(&fillament_wrk, 0, sizeof(fillament_wrk));                   /* 52 */

    mLockCnt = 0;                                                       /* 54 */
    mMasterAlp.Set(mMasterAlp.GetMax());

    /* Init() leaves the switch off; the BlinkOn() that follows each one is
     * what starts it running.  GCC folds the pair into a single `ori 3` per
     * switch, which is why the two calls leave no separate stores. */
    mBlinkAlphaCore1.Init();
    mBlinkAlphaCore1.BlinkOn();
    mBlinkAlphaCore2.Init();
    mBlinkAlphaCore2.BlinkOn();
    mBlinkAlphaWhole1.Init();
    mBlinkAlphaWhole1.BlinkOn();
    mBlinkAlphaWhole2.Init();
    mBlinkAlphaWhole2.BlinkOn();

    FrameReset();                                                       /* 69 */
    mRTFlg = 0;                                                         /* 70 */
}

/* Dropped every frame by CNPlyrCamera::FrameReset(); enemy.c and photo_dat.c
 * re-raise whichever of the three still applies. */
void CFilament::FrameReset(void)
{
    mHintRate = mAutoRate = mBattleRate = 0.0f;                         /* 76 */
}

void CFilament::DrawLock(void)
{
    mLockCnt++;                                                         /* 80 */
}

void CFilament::DrawUnlock(void)
{
    mLockCnt--;                                                         /* 84 */
}

/* Dead code -- see the note in filament.h. */
void CFilament::FadeIn(void)
{
    mMasterAlp.SetAddVal(16);                              /* variable.h 343 */
}

void CFilament::FadeOut(void)
{
    mMasterAlp.SetAddVal(-16);                             /* variable.h 343 */
}

void CFilament::SetBattle(float fRate)
{
    mBattleRate = fRate;                                                /* 98 */
}

void CFilament::SetHint(float fRate)
{
    mHintRate = fRate;                                                 /* 102 */
}

void CFilament::SetAuto(float fRate)
{
    mAutoRate = fRate;                                                 /* 106 */
}

void CFilament::Work(void)
{                                                                      /* 110 */
    mMasterAlp.Work();                                                 /* 113 */

    mBlinkAlphaCore1.Work();                                           /* 115 */
    mBlinkAlphaCore2.Work();                                           /* 116 */
    mBlinkAlphaWhole1.Work();                                          /* 117 */
    mBlinkAlphaWhole2.Work();                                          /* 118 */
}

/* --------------------------------------------------------------------------
 *  Draw
 *
 *  filament.c lines 120..210 hold no code at all -- filament.o's .text is fully
 *  accounted for by the bodies above and below -- so that span is comment or a
 *  disabled older draw.  Nothing of it survives to be recovered.
 *
 *  `bFlip` is the out-of-viewfinder layout: the same three sprites mirrored and
 *  turned a quarter turn.  All three rotate about the *plate's* position, not
 *  about their own, which is why fcx/fcy are taken once from the first sprite
 *  and reused -- the ROM keeps them in $f20/$f21 across the two later
 *  CopySprDToSpr() calls.  (They leave no stab; this build records int locals
 *  but not float ones, the same way CNPlyrCamera::DrawSpiritGageBase's fcx/fcy
 *  are absent from its own local list.)
 * ------------------------------------------------------------------------ */
void CFilament::Draw(int off_x, int off_y, int iAlpha, int bFlip)
{                                                                      /* 213 */
    int       g, b;
    int       iWholePK2 = FD_FILAMENT_WHOLE_BTL;
    int       iCorePK2  = FD_FILAMENT_CORE_BTL;                        /* 216 */
    float     fcx, fcy;
    DISP_SPRT ds;

    if (mLockCnt != 0)                                                 /* 219 */
    {
        return;
    }

    iAlpha = mMasterAlp.Get() * iAlpha / 128;

    if (mRTFlg != 0)                                                   /* 231 */
    {
        mRate = fillament_wrk.bright / 100.0f;                         /* 232 */
        g = 255;  b = 0;                                               /* 233 */
    }
    else
    {
        /* Priority order: a fight beats a passive ghost beats a hint, and the
         * two lower ones also swap the needle to its other sprite pair. */
        if (mBattleRate != 0.0f)                                       /* 235 */
        {
            mRate = mBattleRate;                                       /* 237 */
        }
        else if (mAutoRate != 0.0f)                                    /* 239 */
        {
            mRate     = mAutoRate;                                     /* 240 */
            iWholePK2 = FD_FILAMENT_WHOLE_HINT;                        /* 241 */

            iCorePK2  = FD_FILAMENT_CORE_HINT;                         /* 243 */
        }
        else if (mHintRate != 0.0f)                                    /* 245 */
        {
            mRate     = mHintRate;                                     /* 246 */
            iWholePK2 = FD_FILAMENT_WHOLE_HINT;                        /* 247 */

            iCorePK2  = FD_FILAMENT_CORE_HINT;                         /* 249 */
        }
        else
        {
            mRate = 0.0f;
        }

        g = 128;  b = 128;                                             /* 253 */
    }

    /* The dial plate: blended, at full master alpha, and the piece whose
     * position becomes the rotation centre for the two above it. */
    CopySprDToSpr(&ds, &n_finder_dat[FD_FILAMENT_PLATE]);              /* 259 */
    ds.zbuf   = FIL_ZBUF_MASK;                                         /* 260 */
    ds.alphar = FIL_ALPHA_BLEND;                                       /* 261 */
    ds.alpha  = (u_char)iAlpha;                                        /* 262 */
    fcx  = ds.x + (float)off_x;                                        /* 263 */
    fcy  = ds.y + (float)off_y;
    ds.x = fcx;
    ds.y = fcy;
    if (bFlip != 0)                                                    /* 265 */
    {
        ds.att |= 2;                                                   /* 266 */
        ds.rot = -90.0f;  ds.crx = fcx;  ds.cry = fcy;                 /* 267 */
    }
    DispSprD(&ds);                                                     /* 269 */

    /* From here the deflection scales the alpha, so a weak reading leaves the
     * plate lit and the needle nearly invisible. */
    iAlpha = (int)((float)iAlpha * mRate);                             /* 272 */

    CopySprDToSpr(&ds, &n_finder_dat[iCorePK2]);                       /* 273 */
    ds.zbuf   = FIL_ZBUF_MASK;                                         /* 274 */
    ds.alphar = FIL_ALPHA_ADD;                                         /* 275 */
    ds.alpha  = (u_char)(iAlpha * (mBlinkAlphaCore1.Get() + FIL_BLINK_BIAS) / 256
                                * (mBlinkAlphaCore2.Get() + FIL_BLINK_BIAS) / 256);
                                                                       /* 276 */
    ds.x += (float)off_x;                                              /* 277 */
    ds.y += (float)off_y;
    ds.r = 0x80;  ds.g = (u_char)g;  ds.b = (u_char)b;                 /* 278 */
    if (bFlip != 0)                                                    /* 279 */
    {
        ds.att |= 2;                                                   /* 280 */
        ds.rot = -90.0f;  ds.crx = fcx;  ds.cry = fcy;                 /* 281 */
    }
    DispSprD(&ds);                                                     /* 283 */

    CopySprDToSpr(&ds, &n_finder_dat[iWholePK2]);                      /* 285 */
    ds.zbuf   = FIL_ZBUF_MASK;                                         /* 286 */
    ds.alphar = FIL_ALPHA_ADD;                                         /* 287 */
    ds.alpha  = (u_char)(iAlpha * (mBlinkAlphaWhole1.Get() + FIL_BLINK_BIAS) / 256
                                * (mBlinkAlphaWhole2.Get() + FIL_BLINK_BIAS) / 256);
                                                                       /* 288 */
    ds.x += (float)off_x;                                              /* 289 */
    ds.y += (float)off_y;
    ds.r = 0x80;  ds.g = (u_char)g;  ds.b = (u_char)b;                 /* 290 */
    if (bFlip != 0)                                                    /* 291 */
    {
        ds.att |= 2;                                                   /* 292 */
        ds.rot = -90.0f;  ds.crx = fcx;  ds.cry = fcy;                 /* 293 */
    }
    DispSprD(&ds);                                                     /* 295 */
}                                                                      /* 296 */
