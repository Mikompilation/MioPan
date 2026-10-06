// FILE: /home/zero_rom/zero2np/src/ingame/photo/n_plyr_camera.c
//
// The player camera's per-frame work and its draw.
//
// m_plyr_camera.o owns the object; this file is everything it does.  Three
// separable things live here:
//
//   * The lifecycle -- SetUp() loads the two finder texture paks once, Init()
//     resets every sub-object, FinderIn()/FinderOut() run as the viewfinder
//     comes up and down, and Release() drops the shutter-chance voice.
//   * Main().  One pass over every widget in the finder, then the spirit-gauge
//     block: the drain noise's volume and pitch are computed from the tracked
//     ghost's gauge and the gauge's own alpha is faded between three levels
//     depending on whether a ghost is there and the shutter is charged.
//   * Draw().  Two completely different frames -- outside finder mode only the
//     filament and the HP bar are drawn, at fixed screen positions; inside it
//     the whole overlay is drawn against the finder's drifting centre.
//
// The two texture paks are the reason SetUp() exists at all.  They are loaded
// straight into the 2D texture region rather than through a heap, so the file
// has to check by hand that the second one fits -- that is the printf and the
// assert at the end of it.
//
// Reconstructed from the Feb 6 2004 prototype (SLES_523.84), n_plyr_camera.o.
// Trailing /* NNN */ comments are the original source line numbers.  Beware
// that several of the numbers Ghidra reports for this file are the *header's*
// -- common/variable.h's widget templates occupy 313..500 and its BIT_FLAGS
// 800..860, which overlap this file's own Main() and Draw().  Only lines that
// read monotonically against the .text order are annotated below.

#include "n_plyr_camera.h"

#include <stdio.h>                              /* printf                      */

#include "../../common/utility2.h"              /* GetAlignUp / PRINT_ASSERT   */
#include "../../graphics/effect/effect.h"       /* SetEffects_FOCUS            */
#include "../../graphics/graph2d/g2d_draw.h"    /* DISP_SPRT / CopySprDToSpr   */
#include "../../graphics/graph2d/tim2.h"        /* PK2SendVram                 */
#include "../../ingame/item/prg/item.h"         /* GetPlyrItemHaveNum          */
#include "../../miopan/rendering/miopan_renderer.h" /* the viewfinder surround */
#include "../../system/eeiop/cddat.h"           /* GetFileSize / CD_FILE_DAT   */
#include "../../system/os/eecdvd.h"             /* LoadReq                     */
#include "../../system/os/system.h"             /* GetLanguage / TEX_2D_ADDR   */
#include "../../system/pad/pad.h"               /* paddat                      */
#include "../plyr/player.h"                     /* PlayerModeIsFinder          */
#include "finder.h"                             /* FinderBankPlay              */
#include "freq_camera.h"                        /* GetFreqCamera               */
#include "n_finder_dat.h"                       /* n_finder_dat                */

/* The GS register words the overlay draws with.  Same pair finder.c uses:
 * ZBUF with the mask bit set writes no depth, ALPHA 0x44 is source-over. */
#define NPC_ZBUF_MASK       0x000000010a000118ULL
#define NPC_ALPHA_BLEND     0x44

/* TEST with the alpha test on: keeps the border pieces from writing their own
 * transparent texels over what is behind them. */
#define NPC_TEST_ATE        0x0000000000030003ULL

/* The finder is centred on the 640x448 screen; GetFinderMovePos() returns the
 * drift away from here. */
#define NPC_CENTER_X        320.0f
#define NPC_CENTER_Y        224.0f

/* The two finder texture paks share the 2D texture region.  The ROM's own
 * printf names the limit PL_FNDR_ADDR + PL_FNDR_SIZE + PL_LIFE_SIZE, but the
 * compiler folded the sum into one constant, so the split between the two
 * sizes is not recoverable -- only the end address is. */
#define PL_FNDR_ADDR        TEX_2D_ADDR                     /* 0x018ad000 */
#define PL_FNDR_BUF_END     0x019368c0

/* Both paks are localised: the file table lays the five language variants out
 * consecutively from the base id, so GetLanguage() is the index. */
#define PK_N_FINDER         N_FINDER_PK2                    /* 9  */
#define PK_N_LIFE           N_LIFE_PK2                      /* 14 */

/* n_finder_dat[] indices this file draws.  0..5 are the six border pieces of
 * the finder frame, 9 the frame itself, 0x22..0x25 the spirit gauge's backing
 * plate and 0x39 the capture ring. */
#define FD_KAKIWARI_TOP     0
#define FD_KAKIWARI_NUM     6
#define FD_FINDER_FRAME     9
#define FD_SPIRIT_GAGE_BASE 0x22
#define FD_CAPTURE_CIRCLE   0x39

/* Sound-buffer volume runs 0..0x3fff. */
#define SND_VOL_MAX         0x3fff
/* ... and pitch is a 12.4 fixed multiplier, so 0x1000 is unity. */
#define SND_PITCH_UNITY     0x1000

/* The paks are loaded once for the life of the process; nothing ever clears
 * this. */
static int mIsSetup;                                        /* sbss 3f4e9c */

/* --------------------------------------------------------------------------
 *  The finder frame
 *
 *  Stateless -- CFinderBase exists only to hang these two off, and both live
 *  in this object file rather than in one of their own.
 * ------------------------------------------------------------------------ */

/* 描き割り -- the painted flat.  Six pieces that mask everything outside the
 * viewfinder aperture, drawn before the frame itself so the frame's edge sits
 * on top of them. */
void CFinderBase::DrawKakiwari(int off_x, int off_y, int iAlpha)
{                                                                       /* 56 */
    DISP_SPRT ds;
    int       i;

    for (i = 0; i < FD_KAKIWARI_NUM; i++)                               /* 58 */
    {
        CopySprDToSpr(&ds, &n_finder_dat[FD_KAKIWARI_TOP + i]);         /* 59 */
        ds.alphar = NPC_ALPHA_BLEND;                                    /* 60 */
        ds.alpha  = (u_char)iAlpha;                                     /* 61 */
        ds.test   = NPC_TEST_ATE;                                       /* 62 */
        ds.zbuf   = NPC_ZBUF_MASK;                                      /* 63 */
        ds.x     += (float)off_x;                                       /* 64 */
        ds.y     += (float)off_y;
        DispSprD(&ds);                                                  /* 65 */
    }                                                                   /* 66 */
}

void CFinderBase::Draw(int off_x, int off_y, int iAlpha)
{
    DISP_SPRT ds;

    DrawKakiwari(off_x, off_y, iAlpha);                                 /* 40 */

    CopySprDToSpr(&ds, &n_finder_dat[FD_FINDER_FRAME]);                 /* 46 */
    ds.zbuf   = NPC_ZBUF_MASK;                                          /* 47 */
    ds.alphar = NPC_ALPHA_BLEND;                                        /* 48 */
    ds.alpha  = (u_char)iAlpha;                                         /* 49 */
    ds.x     += (float)off_x;                                           /* 50 */
    ds.y     += (float)off_y;
    DispSprD(&ds);                                                      /* 51 */
}

/* --------------------------------------------------------------------------
 *  Lifecycle
 * ------------------------------------------------------------------------ */

void CNPlyrCamera::Release(void)
{                                                                       /* 69 */
    sp.Release();                                                       /* 70 */
}

void CNPlyrCamera::SetUp(void)
{                                                                       /* 74 */
    int adrs;

    if (mIsSetup != 0)                                                  /* 77 */
    {
        return;
    }

    mIsSetup = 1;                                                       /* 78 */

    finder_buf = (void *)PL_FNDR_ADDR;                                  /* 80 */
    LoadReq(PK_N_FINDER + GetLanguage(), (uintptr_t)finder_buf);        /* 81 */
    pl_life_buf = (void *)GetAlignUp(                                   /* 82 */
        (u_int)(uintptr_t)finder_buf
            + GetFileSize(PK_N_FINDER + GetLanguage()), 6);             /* 84 */

    LoadReq(PK_N_LIFE + GetLanguage(), (uintptr_t)pl_life_buf);         /* 85 */
    adrs = (int)GetAlignUp(                                             /* 86 */
        (u_int)(uintptr_t)pl_life_buf
            + GetFileSize(PK_N_LIFE + GetLanguage()), 6);               /* 89 */

    if (PL_FNDR_BUF_END < adrs)                                         /* 90 */
    {
        printf("PL_FNDR_ADDR + PL_FNDR_SIZE + PL_LIFE_SIZE = 0x%x\n",
               PL_FNDR_BUF_END);                                        /* 91 */
        PRINT_ASSERT("Finder PK Is Over Size 0x%x", adrs);              /* 92 */
        return;
    }

    printf("Finder Pk2 End Adrs[0x%x] buffer End Adrs[0x%x]\n",
           adrs, PL_FNDR_BUF_END);                                      /* 94 */
}

/* --------------------------------------------------------------------------
 *  The spirit gauge's backing plate
 *
 *  Four pieces of one ring, the last two rotated a quarter turn so the same
 *  two textures make the whole of it.  Everything scales about the point the
 *  gauge is centred on rather than about each piece's own anchor, which is
 *  what the csx/csy assignments are for.
 * ------------------------------------------------------------------------ */
void CNPlyrCamera::DrawSpiritGageBase(int off_x, int off_y, int iAlpha,
                                      float fMasterScale)
{                                                                       /* 111 */
    DISP_SPRT ds;
    float     fx  = (float)off_x;
    float     fy  = (float)off_y;
    float     fcx = (float)(off_x + 319);
    float     fcy = (float)(off_y + 225);

    CopySprDToSpr(&ds, &n_finder_dat[FD_SPIRIT_GAGE_BASE + 0]);         /* 115 */
    ds.zbuf   = NPC_ZBUF_MASK;                                          /* 116 */
    ds.alphar = NPC_ALPHA_BLEND;                                        /* 117 */
    ds.alpha  = (u_char)iAlpha;                                         /* 118 */
    ds.x     += fx;                                                     /* 119 */
    ds.y     += fy;
    ds.tex1   = 0x161;                                                  /* 120 */
    ds.csx    = fcx;                                                    /* 121 */
    ds.csy    = fcy;
    ds.scw    = fMasterScale;
    ds.sch    = fMasterScale;
    DispSprD(&ds);                                                      /* 123 */

    CopySprDToSpr(&ds, &n_finder_dat[FD_SPIRIT_GAGE_BASE + 1]);         /* 125 */
    ds.zbuf   = NPC_ZBUF_MASK;                                          /* 126 */
    ds.alphar = NPC_ALPHA_BLEND;                                        /* 127 */
    ds.alpha  = (u_char)iAlpha;                                         /* 128 */
    ds.x     += fx;                                                     /* 129 */
    ds.y     += fy;
    ds.tex1   = 0x161;                                                  /* 130 */
    ds.csx    = fcx;                                                    /* 131 */
    ds.csy    = fcy;
    ds.scw    = fMasterScale;
    ds.sch    = fMasterScale;
    DispSprD(&ds);                                                      /* 133 */

    CopySprDToSpr(&ds, &n_finder_dat[FD_SPIRIT_GAGE_BASE + 2]);         /* 135 */
    ds.zbuf   = NPC_ZBUF_MASK;                                          /* 136 */
    ds.alphar = NPC_ALPHA_BLEND;                                        /* 137 */
    ds.alpha  = (u_char)iAlpha;                                         /* 138 */
    ds.x     += fx;                                                     /* 139 */
    ds.y     += fy;
    ds.tex1   = 0x161;                                                  /* 140 */
    ds.crx    = fcx;                                                    /* 141 */
    ds.cry    = fcy;
    ds.rot    = 270.0f;
    ds.csx    = fcx;                                                    /* 143 */
    ds.csy    = fcy;
    ds.scw    = fMasterScale;
    ds.sch    = fMasterScale;
    DispSprD(&ds);                                                      /* 145 */

    CopySprDToSpr(&ds, &n_finder_dat[FD_SPIRIT_GAGE_BASE + 3]);         /* 147 */
    ds.zbuf   = NPC_ZBUF_MASK;                                          /* 148 */
    ds.alphar = NPC_ALPHA_BLEND;                                        /* 149 */
    ds.alpha  = (u_char)iAlpha;                                         /* 150 */
    ds.x     += fx;                                                     /* 151 */
    ds.y     += fy;
    ds.tex1   = 0x161;                                                  /* 152 */
    ds.crx    = fcx;                                                    /* 153 */
    ds.cry    = fcy;
    ds.rot    = 270.0f;
    ds.csx    = fcx;                                                    /* 155 */
    ds.csy    = fcy;
    ds.scw    = fMasterScale;
    ds.sch    = fMasterScale;
    DispSprD(&ds);                                                      /* 157 */
}

/* --------------------------------------------------------------------------
 *  Entering and leaving the viewfinder
 * ------------------------------------------------------------------------ */

void CNPlyrCamera::FinderIn(void)
{
    mCntFinder = 0;                                                     /* 165 */
    eq_tray.Reset();
    mSpiritGageScale.SetMax();
    mBonusShot.Init();                                                  /* 167 */
    mMasterAlpha.SetAddVal(0x10);
    hp.FadeIn();
    center_circle.Init();                                               /* 170 */
    mNoSpiritGageTimer = 0;                                             /* 171 */
}

void CNPlyrCamera::FinderOut(void)
{                                                                       /* 174 */
    mMasterAlpha.SetAddVal(-0x10);
    eq_tray.End();                                                      /* 176 */
    hp.FadeOut();
}

void CNPlyrCamera::DrawLock(void)
{
    mDrawLockCnt = (char)(mDrawLockCnt + 1);                            /* 182 */
}

void CNPlyrCamera::DrawUnlock(void)
{
    mDrawLockCnt = (char)(mDrawLockCnt - 1);                            /* 187 */
}

void CNPlyrCamera::InCircleDrawLock(int iCnt)
{
    mInWaiter.Wait((short)iCnt);
    mInAlpha.SetMin();
}

/* --------------------------------------------------------------------------
 *  Reset
 *
 *  Runs from the constructor as well as from ingame.c, so the camera is in
 *  this state before the first frame.  Note ResetCharge()'s body is written
 *  out again here rather than called -- the ROM inlines the same expression in
 *  both places.
 * ------------------------------------------------------------------------ */
void CNPlyrCamera::Init(void)
{
    mDrawLockCnt = 0;
    filament.Init();                                                    /* 199 */
    hp.Init();
    sp.Init();                                                          /* 201 */
    center_cross.Init();
    mInAlpha.SetMax();
    mMasterAlpha.SetMin();
    ene_life.FrameLenSet(0.0f);                                         /* 206 */
    film_no.Init();                                                     /* 207 */
    charger.Reset(camera_film.GetFilmChargeSpd());                      /* 209 */
    mSpiritGageAlpha.Set(0);
    mpSpiritGage = (CSpiritGage *)0;
    search_mark.Init();                                                 /* 212 */
    mInWaiter.Reset();
    mDmgDisp.Init();                                                    /* 214 */
    ReqNoiseReset();                                                    /* 216 */
    eq_tray.Init();                                                     /* 217 */
    mFcs.Init();
    mZoomRate = 1.0f;                                                   /* 220 */
    mFOV.Set(1.0f);
    mFOVTimer.Reset();
    mBonusShot.Init();                                                  /* 223 */
}

/* --------------------------------------------------------------------------
 *  Lens zoom
 *
 *  ReqZoomIn() narrows the field to 0.8 over 4.5 frames and holds it there;
 *  ReqZoomOut() arms a 60-frame timer that Main() turns back into a fade to
 *  1.0.  Re-issuing the zoom-in every frame is free -- Fade2() ignores a
 *  request for the target it already has -- which is what lets the caller
 *  drive it from a per-frame condition and forget about it.
 * ------------------------------------------------------------------------ */
void CNPlyrCamera::ReqZoomIn(void)
{
    mFOV.Fade2(0.8f, 4.5f);                                             /* 227 */
    mFOVTimer.Reset();
}

void CNPlyrCamera::ReqZoomOut(void)
{
    mFOVTimer.Wait(60);
}

/* The manual zoom the zoom lens adds, stepped off the pad.  Note the two
 * bounds are the wrong way round from the FOV fade above: mZoomRate multiplies
 * the field, so 1.2 is wide and 0.8 is tight. */
void CNPlyrCamera::ZoomWork(void)
{
    /* Bit 3 of the fitted-parts set is the zoom lens.  Without it the manual
     * zoom is pinned at 1.0 rather than merely frozen, so an upgrade lost
     * mid-shot snaps back instead of leaving the lens where it was. */
    if (camera_power_up.mCamPartsSetFlg.IsUp(3) != 0)
    {
        /* PORT DEVIATION, deliberate.  The ROM reads paddat[0x30] and
         * paddat[0x2f] here -- indices 48 and 47 of a 26-wide row.  Verified
         * against the ROM at 0x225728 and 0x225734: lw v0, 192(a1) and
         * lw v0, 188(a1) off the paddat pointer, so the over-index is the
         * original.  On the PS2 it walks into the *next* preset row, and
         * because all three rows of paddat_m carry L2 at [21] and R2 at [22]
         * it still resolves to L2/R2 for pad_type 0 and 1.  pad_type 2 runs
         * off the end of paddat_m altogether and lands in pushdat_m, reading
         * &push[9] and &push[11] -- pressure bytes -- as hold counters.
         *
         * Here paddat_m and pushdat_m are separate objects with no guaranteed
         * adjacency, so the pad_type 2 read is unspecified rather than merely
         * wrong; and the over-index would silently rebind the zoom the moment
         * a fourth preset row is added.  Read the current row's own L2/R2 slots
         * instead: bit-identical for pad_type 0 and 1, and what the table
         * plainly intends for 2. */
        if (*paddat[22] != 0)               /* R2; ROM: paddat[0x30] */  /* 241 */
        {
            mZoomRate += 0.02f;                                         /* 242 */
            if (1.2f < mZoomRate)                                       /* 243 */
            {
                mZoomRate = 1.2f;                                       /* 244 */
            }
        }
        else if (*paddat[21] != 0)          /* L2; ROM: paddat[0x2f] */  /* 246 */
        {
            mZoomRate -= 0.02f;                                         /* 247 */
            if (mZoomRate < 0.8f)                                       /* 248 */
            {
                mZoomRate = 0.8f;                                       /* 249 */
            }
        }
    }
    else
    {
        mZoomRate = 1.0f;                                               /* 253 */
    }
}

/* What finder_camera.c multiplies the finder's base field of view by. */
float CNPlyrCamera::GetFOVRate(void)
{
    return mZoomRate * mFOV.Get();
}

/* --------------------------------------------------------------------------
 *  Shutter charge and the drain noise
 * ------------------------------------------------------------------------ */

void CNPlyrCamera::ResetCharge(void)
{
    charger.Reset(camera_film.GetFilmChargeSpd());                      /* 258 */
}

/* Fired shot: slide the drain noise up in pitch and fade it out, and hold the
 * gauge off for 70 frames so it does not immediately start humming again. */
void CNPlyrCamera::ReqNoiseUp(void)
{                                                                       /* 265 */
    mNoSpiritGageTimer = 70;                                            /* 266 */
    mSpiritNoise.PitchFade(0x3800, 6);
    mSpiritNoise.Fade(0, 11);
}

/* Empty in the ROM -- the body is commented out.  Init() still calls it. */
void CNPlyrCamera::ReqNoiseReset(void)
{                                                                       /* 272 */
}

/* --------------------------------------------------------------------------
 *  Per-frame work
 * ------------------------------------------------------------------------ */
void CNPlyrCamera::Main(void)
{                                                                       /* 280 */
    int fcs;
    int iPitch;

    mMasterAlpha.Work();                                                /* 282 */
    mInAlpha.Work();                                                    /* 283 */
    hp.Work();                                                          /* 285 */
    filament.Work();                                                    /* 286 */
    search_mark.Work();                                                 /* 287 */

    if (mFOVTimer.Work() != 0)                                          /* 289 */
    {
        mFOV.Fade(1.0f, 15.0f);                                         /* 290 */
    }
    mFOV.Work();                                                        /* 292 */

    /* State 3 is "mSpeed < 0", i.e. the field is still narrowing.  While that
     * lasts the depth-of-field blur is pinned wide open; the moment the zoom
     * settles or reverses it walks back down 42 a frame. */
    if (mFOV.GetState() == 3)
    {
        mFcs.SetMax();
    }
    else
    {
        mFcs.SetAddVal(-42);
    }
    mFcs.Work();                                                        /* 298 */

    if (PlayerModeIsFinder() != 0)                                      /* 302 */
    {
        /* The blur ramps in over the first 20 frames of finder mode and then
         * hands over to mFcs, which is where the zoom drives it from. */
        fcs = ((float)mCntFinder <= 20.0f)
                  ? 0x80 - (mCntFinder * 0x80) / 20
                  : 0;                                                  /* 303 */

        if (fcs != 0)                                                   /* 305 */
        {
            SetEffects_FOCUS(1, fcs);                                   /* 306 */
        }
        else
        {
            SetEffects_FOCUS(1, mFcs.Get());                            /* 308 */
        }

        if (charger.IsReady() != 0)                                     /* 310 */
        {
            film_no.BlinkOn();                                          /* 311 */
        }
        else
        {
            /* Init(), not BlinkOff() -- the ROM's own asymmetry. */
            film_no.Init();                                             /* 313 */
        }

        mBonusShot.Work();                                              /* 317 */
        mDmgDisp.Work();                                                /* 318 */
        charger.Work(1);                                                /* 319 */
        ene_life.Work();                                                /* 320 */
        center_circle.Work();                                           /* 321 */
        center_cross.Work();                                            /* 322 */
        eq_tray.Work();                                                 /* 323 */
        film_no.Work();                                                 /* 324 */
        ZoomWork();                                                     /* 325 */
        /* Bit 2 of the fitted-parts set is the shutter-chance lamp. */
        sp.Work(camera_power_up.mCamPartsSetFlg.IsUp(2));

        if (mInWaiter.Work() != 0)                                      /* 328 */
        {
            mInAlpha.SetAddVal(0x10);
        }

        if (mNoSpiritGageTimer != 0)                                    /* 331 */
        {
            /* Still inside the post-shot blackout: the gauge stays down and
             * the drain noise is left alone -- ReqNoiseUp() already faded it. */
            mNoSpiritGageTimer = (char)(mNoSpiritGageTimer - 1);        /* 333 */

            if (charger.IsReady() == 0)                                 /* 336 */
            {
                mSpiritGageAlpha.Set(0);
            }
        }
        else if (mpSpiritGage == (CSpiritGage *)0
                 || charger.IsReady() == 0)                             /* 341 */
        {
            /* No ghost to drain, or nothing to drain it with. */
            mSpiritGageAlpha.Fade2(0, 4);
            mSpiritNoise.Stop(10);
            mSpiritGageScale.SetMax();
        }
        else
        {
            /* The drain noise tracks the gauge: volume from 30% of full at an
             * empty gauge to 100% at 70, and pitch up from unity by 40 a
             * percent.
             *
             * iVol has no entry in the stabs where fcs and iPitch both do, so
             * the ROM either wrote the expression out in each branch below and
             * let GCC fold the two copies together, or the local was optimised
             * away entirely.  It is kept as one here because the branches are
             * otherwise identical in it. */
            int iVol = SND_VOL_MAX
                       * (mpSpiritGage->GetPercent() * 70 / 100 + 30) / 100;
                                                                        /* 344 */
            iPitch = mpSpiritGage->GetPercent() * 40 + SND_PITCH_UNITY; /* 350 */

            if (mpSpiritGage->GetPercent() == 0)                        /* 351 */
            {
                mSpiritNoise.Stop(10);
                mSpiritGageAlpha.Fade2(0x28, 4);
            }
            else
            {
                if (mSpiritNoise.IsPlaying() == 0)
                {
                    mSpiritNoise.Play(15, 1, 1, 10, (SND_3D_SET *)0,
                                      iVol, iPitch);
                }
                else
                {
                    mSpiritNoise.Fade(iVol, 10);
                    mSpiritNoise.PitchFade(iPitch, 0);
                }

                mSpiritGageAlpha.Fade2(0x50, 4);
            }

            mSpiritGageScale.SetAddVal(-0x10);
        }

        mSpiritGageAlpha.Work();                                        /* 372 */
        mSpiritGageScale.Work();                                        /* 373 */

        if ((float)mCntFinder <= 20.0f)                                 /* 375 */
        {
            mCntFinder = (short)(mCntFinder + 1);                       /* 376 */
        }
    }
    else
    {
        charger.Work(0);                                                /* 379 */
        mSpiritNoise.Stop(10);
    }
}

/* --------------------------------------------------------------------------
 *  Draw
 * ------------------------------------------------------------------------ */

/* The plain ring the shot is framed in.  Drawn at half intensity -- the
 * coloured CCenterCircle that reacts to what is inside it goes on top. */
void CNPlyrCamera::CaptureCircleDraw(int fndr_mx, int fndr_my, int iAlpha)
{                                                                       /* 388 */
    DISP_SPRT ds;

    CopySprDToSpr(&ds, &n_finder_dat[FD_CAPTURE_CIRCLE]);               /* 393 */
    ds.r     = 0x80;                                                    /* 394 */
    ds.g     = 0x80;
    ds.b     = 0x80;
    ds.alpha = (u_char)iAlpha;                                          /* 395 */
    ds.x    += (float)fndr_mx;                                          /* 396 */
    ds.y    += (float)fndr_my;
    DispSprD(&ds);                                                      /* 397 */
}

/* `bonus` is genuinely unread -- no instruction in the ROM body touches the
 * register it arrives in.  PhotoInfoDispNew() is what the shipped game calls. */
void CNPlyrCamera::PhotoInfoDisp(int iDamage, int iScore, PHOTO_BONUS_SHOT bonus)
{                                                                       /* 401 */
    mDmgDisp.Req(iDamage, iScore, 60);                                  /* 402 */

    (void)bonus;
}

void CNPlyrCamera::PhotoInfoDispNew(int iDamage, int iScore,
                                    BONUS_SHOT_SCORE bonus)
{
    ene_life.SetDamage(iDamage);                                        /* 407 */
    mBonusShot.Req(iScore, bonus, 2);                                   /* 408 */
}

/* Where the finder has drifted to.  GetPlayerFinderPos() reports an absolute
 * screen position; this is that minus the screen centre, quantised through
 * short so every widget lands on the same integer offset. */
void CNPlyrCamera::GetFinderMovePos(float *fx, float *fy)
{                                                                       /* 498 */
    float fp[2];

    GetPlayerFinderPos(&fp[0], &fp[1]);                                 /* 500 */
    *fx = (float)(short)(int)(fp[0] - NPC_CENTER_X);                    /* 501 */
    *fy = (float)(short)(int)(fp[1] - NPC_CENTER_Y);                    /* 502 */
}

void CNPlyrCamera::Draw(void)
{
    float fx;
    float fy;
    int   fndr_mx;
    int   fndr_my;
    int   iAlpha;
    int   iSpiritGageAlpha;
    float iSpiritGageScale;
    /* Film types 0..4 read Type-07, -14, -61, -90 and a fifth the readout has
     * no number for.  CFilmNo::Draw() takes the printed number, not the
     * index. */
    static char aFilmTypeNo[5] = { 7, 14, 61, 90, 0 };   /* sdata 3f3568 */

    if (mDrawLockCnt != 0)                                              /* 412 */
    {
        return;
    }

    GetFinderMovePos(&fx, &fy);                                         /* 418 */
    fndr_mx = (int)fx;                                                  /* 419 */
    /* The vertical drift is damped to a quarter of the camera's own shake --
     * the HUD is attached to the camera body, not to the world. */
    fndr_my = (int)(fy - GetFreqCamera() * 0.25f);                      /* 420 */

    if (PlayerModeIsFinder() != 0)                                      /* 423 */
    {
        /* The finder's own alpha is the master fade narrowed by the post-shot
         * blackout, and the gauge fades again on top of that.  Both
         * denominators are GetMax() expansions rather than literals -- the ROM
         * emits three of them here, two 128s and the 64 below -- but which
         * widget each 128 names is not recoverable, since GCC folds the bound
         * and shares the register. */
        iAlpha           = mMasterAlpha.Get() * mInAlpha.Get()
                           / mInAlpha.GetMax();
        iSpiritGageAlpha = mSpiritGageAlpha.Get() * iAlpha
                           / mMasterAlpha.GetMax();
        /* mSpiritGageScale walks down from its maximum as the gauge comes up,
         * so this is 1.00 at rest and grows by up to a fifth; the fitted radius
         * lens scales the whole thing. */
        iSpiritGageScale = ((float)(mSpiritGageScale.Get() * 20
                                    / mSpiritGageScale.GetMax()) / 100.0f
                            + 1.0f)
                           * camera_power_up.GetRadiusRate();           /* 434 */

        /* Parked at its maximum means the gauge is not up at all -- drawn at
         * zero alpha rather than skipped, so the two draws stay
         * unconditional. */
        if (mSpiritGageScale.Get() == mSpiritGageScale.GetMax())
        {
            iSpiritGageAlpha = 0;
        }

        PK2SendVram((uintptr_t)finder_buf, -1, -1, 0);                  /* 441 */

        /*
         * PORT ADDITION -- not in the ROM.  Defocus, darken and cast toward
         * crimson everything a wide window shows outside the ROM's own 640x448
         * frame.
         *
         * It goes here, between the VRAM upload and the first HUD sprite,
         * because that is the one moment in the frame when the world is
         * finished and none of the overlay has been drawn: the surround
         * samples the former, and the frame art, the gauges and the reticle
         * all land sharp on top of it.  The renderer no-ops the call when the
         * effect is switched off -- and on a 4:3 window, where there is
         * nothing outside the frame to treat -- so this costs nothing by
         * default.
         *
         * No rectangle is passed.  The region treated is everything beyond
         * 640x448, which is the renderer's own business rather than the
         * game's; the 640x448 the artists composed is left exactly as it was
         * rendered.
         */
        MioPan_RendererDrawFinderMask(mMasterAlpha.Get());

        eq_tray.RenzMarkDraw(fndr_mx, fndr_my, iAlpha);                 /* 443 */
        finder_base.Draw(fndr_mx, fndr_my, mMasterAlpha.Get());         /* 444 */
        film_no.Draw(GetPlyrItemHaveNum(camera_film.mFilmType),
                     aFilmTypeNo[camera_film.mFilmType],
                     fndr_mx, fndr_my, mMasterAlpha.Get());

        /* Bit 0 of the fitted-parts set is the enemy-life readout, bit 0 of
         * the addition set the shutter-chance lamp.  The ROM does not pass
         * either bit number as a literal -- GCC substituted mDrawLockCnt,
         * which the test at the top has already proved is zero, and then left
         * the whole word/bit split in.  The result is bit 0 of word 0 either
         * way, and the shift amount is a multiple of 32 so the mask is 1
         * whatever mDrawLockCnt held. */
        if (camera_power_up.mCamPartsSetFlg.IsUp(0) != 0)
        {
            ene_life.Draw(fndr_mx, fndr_my, mMasterAlpha.Get());        /* 451 */
        }

        if (camera_power_up.mAdditionFlg.IsUp(0) != 0)
        {
            sp.Draw(fndr_mx, fndr_my, mMasterAlpha.Get());              /* 455 */
        }

        eq_tray.Draw(fndr_mx, fndr_my, mMasterAlpha.Get());             /* 457 */
        CaptureCircleDraw(fndr_mx, fndr_my, iAlpha);                    /* 459 */
        center_circle.Draw(fndr_mx, fndr_my, iAlpha,
                           camera_power_up.GetRadiusRate());            /* 460 */
        center_cross.Draw(fndr_mx, fndr_my, iAlpha);                    /* 461 */
        charger.Draw(fndr_mx, fndr_my, mMasterAlpha.Get());             /* 462 */
        search_mark.Draw(fndr_mx, fndr_my, mMasterAlpha.Get(),
                         camera_power_up.GetRadiusRate());              /* 463 */

        DrawSpiritGageBase(fndr_mx, fndr_my, iSpiritGageAlpha,
                           iSpiritGageScale);                           /* 466 */
        if (mpSpiritGage != (CSpiritGage *)0)                           /* 467 */
        {
            mpSpiritGage->Draw(fndr_mx, fndr_my, iSpiritGageAlpha,
                               iSpiritGageScale);                       /* 469 */
        }

        /* The needle and the HP bar come out of the other pak, so the texture
         * upload has to be re-issued before them. */
        PK2SendVram((uintptr_t)pl_life_buf, -1, -1, 0);                 /* 472 */
        filament.Draw(fndr_mx, fndr_my, mMasterAlpha.Get(), 0);         /* 473 */
        hp.Draw(fndr_mx, fndr_my, mMasterAlpha.Get());                  /* 474 */
        mBonusShot.Draw(fndr_mx, fndr_my);                              /* 475 */
    }
    else
    {
        /* Out of the finder only the needle and the HP bar are drawn, and at
         * fixed screen positions rather than against the finder centre. */
        PK2SendVram((uintptr_t)pl_life_buf, -1, -1, 0);                 /* 477 */
        filament.Draw(0x130, 0x1a2, 0x80, 1);                           /* 481 */
        hp.Draw(0x15, 0x1b, 0x80);                                      /* 482 */
    }                                                                   /* 486 */
}

void CNPlyrCamera::FrameReset(void)
{                                                                       /* 489 */
    center_circle.FrameReset();                                         /* 490 */
    filament.FrameReset();                                              /* 491 */
}
