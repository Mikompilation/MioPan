// FILE: /home/zero_rom/zero2np/src/ingame/photo/photo_charger.c
//
// The shutter's charge accumulator -- the row of ticks along the top right of
// the viewfinder that fills while the camera winds on, and the flare that pops
// when it finishes.
//
// Two counters and a flag.  mNowWaitCnt counts frames down from the loaded
// film's charge time (mWaitCnt, already PAL-scaled by
// CCameraFilm::GetFilmChargeSpd()); mFlare is a one-shot CBlinkVariable pulse
// fired the frame it lands; mReady latches at 1 and stays there until the next
// Reset(), which is what gates the charged shot, the film-number blink and the
// spirit gauge.
//
// Three things read differently from the way the stub described them:
//
//   * Work()'s argument is `bSeFlg`, not "am I counting".  It suppresses only
//     the cue three frames from full -- the countdown, the flare and mReady all
//     advance whether the finder is up or not.
//
//   * the tick row is drawn from the *elapsed* fraction, so it fills as the
//     charge counts down, sixteen ticks marching leftwards five pixels apart.
//
//   * mFlare's floor is 50, not 0.  Reset() seeds it there and the blink
//     bounces back down to it, so the ticks never go fully dark -- they sit at
//     about 40% and flash to full for one pulse when the charge completes.
//
// Reconstructed from the Feb 6 2004 prototype (SLES_523.84), photo_charger.o.
// All four `.text` exports; `.text` is accounted for byte-for-byte
// (0x232020..0x2322c4) once the fixed_array.h boilerplate at the head of the
// object file is set aside.  The two linkonce bodies it carries --
// CBlinkVariable<char,50,127>::Blink() and ::Work() -- live in common/variable.h
// and are what finally settled that CBlinkVariable seeds from Min.
//
// CPhotoCharger::Init() is declared by the ROM but never emitted or expanded;
// see the note beside the class in m_plyr_camera.h.

#include "m_plyr_camera.h"
#include "../../graphics/graph2d/g2d_draw.h"    /* DISP_SPRT / DispSprD        */
#include "finder.h"                             /* FinderBankPlay              */
#include "n_finder_dat.h"                       /* n_finder_dat                */

/* The GS register words the tick row draws with -- the finder's usual masked
 * ZBUF, and ALPHA 0x48 (additive), so the ticks add over whatever is behind
 * them rather than blending. */
#define PC_ZBUF_MASK        0x000000010a000118ULL
#define PC_ALPHA_ADD        0x48

/* n_finder_dat[] index: one 6x4 tick at (547, 46), the top-right corner of the
 * finder.  The whole row is that one sprite drawn up to sixteen times. */
#define FD_CHARGE_TICK      39

/* The row: sixteen ticks at full charge, each five pixels left of the last. */
#define PC_TICK_NUM         16
#define PC_TICK_PITCH        5

/* The cue fires three frames before the charge completes, so the sound lands
 * with the flare rather than after it. */
#define PC_SE_LEAD           3
#define PC_SE_NO            10

/* How many frames the flare pulse takes to reach mFlare's maximum. */
#define PC_FLARE_TIME       15

/* --------------------------------------------------------------------------
 *  Work
 *
 *  Photo_charger.c lines 20..23 hold no code: the ROM's `if` body is the Blink
 *  at 19 and the latch at 24, with four lines between them that compile to
 *  nothing.  Comment, or a disabled older cue.
 * ------------------------------------------------------------------------ */
void CPhotoCharger::Work(int bSeFlg)
{                                                                       /* 10 */
    if (bSeFlg != 0 && mNowWaitCnt.Get() == mWaitCnt - PC_SE_LEAD)      /* 11 */
    {
        FinderBankPlay(PC_SE_NO, 1, 0, 0, NULL, 0x3200, 0x1000);        /* 13 */
    }

    if (mNowWaitCnt.Work() != 0)                                        /* 18 */
    {
        mFlare.Blink(PC_FLARE_TIME);                                    /* 19 */

        mReady = 1;                                                     /* 24 */
    }

    mFlare.Work();                                                      /* 26 */
}

/* --------------------------------------------------------------------------
 *  Reset
 *
 *  Note the countdown is *not* restarted if one is already running --
 *  CWaitVariable::Wait() only seeds a counter that has reached zero.  That is
 *  what stops a film change mid-wind from handing the player a free charge.
 * ------------------------------------------------------------------------ */
void CPhotoCharger::Reset(int iWaitCnt)
{                                                                       /* 29 */
    mWaitCnt = (short)iWaitCnt;                                         /* 30 */
    mReady   = 0;                                                       /* 31 */
    mFlare.Init();                                        /* variable.h 553 */
    mNowWaitCnt.Wait(mWaitCnt);                           /* variable.h 425 */
}

int CPhotoCharger::IsReady(void)
{                                                                       /* 36 */
    return mReady;                                                      /* 37 */
}

/* --------------------------------------------------------------------------
 *  Draw
 *
 *  The tick count is the elapsed fraction of the wind, not the remaining one:
 *  mNowWaitCnt walks mWaitCnt down to 0, so (mWaitCnt - now) * 16 / mWaitCnt
 *  runs 0..16 and the row fills left to right as the camera charges.
 *
 *  mWaitCnt is the divisor and is never tested first -- Reset() is the only
 *  thing that writes it, and a film with a zero charge time would trap here.
 * ------------------------------------------------------------------------ */
void CPhotoCharger::Draw(int fndr_mx, int fndr_my, int iAlpha)
{                                                                       /* 41 */
    DISP_SPRT ds;
    int       i = (mWaitCnt - mNowWaitCnt.Get()) * PC_TICK_NUM
                / mWaitCnt;                                             /* 43 */

    iAlpha = iAlpha * mFlare.Get() / 128;                 /* variable.h 567 */

    while (0 < i)                                                       /* 47 */
    {
        CopySprDToSpr(&ds, &n_finder_dat[FD_CHARGE_TICK]);              /* 48 */
        ds.zbuf   = PC_ZBUF_MASK;                                       /* 49 */
        ds.alphar = PC_ALPHA_ADD;                                       /* 50 */
        ds.alpha  = (u_char)iAlpha;                                     /* 51 */
        ds.x     += (float)fndr_mx;                                     /* 52 */
        ds.y     += (float)fndr_my;
        DispSprD(&ds);                                                  /* 53 */

        i--;  fndr_mx -= PC_TICK_PITCH;                                 /* 54 */
    }
}
