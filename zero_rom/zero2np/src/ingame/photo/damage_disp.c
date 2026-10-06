// FILE: /home/zero_rom/zero2np/src/ingame/photo/damage_disp.c
//
// The damage number that flies off a hit ghost.
//
// The older, pre-CBonusShot post-shot readout: a plate and a number that pulse
// once per second for as long as the request's hold lasts.  The timer is what
// drives the pulse -- CWaitVariable::Work() returns non-zero only on the frame
// the countdown lands on zero, and that is the frame a new Blink() is armed --
// so the number keeps flashing until Req() stops being called.
//
// CNPlyrCamera::PhotoInfoDisp() is the only caller and the shipped game uses
// PhotoInfoDispNew() and CBonusShot instead, so nothing reaches Req() in
// normal play.
//
// Reconstructed from the Feb 6 2004 prototype (SLES_523.84), damage_disp.o.
// All four ZERO2.MAP .text symbols.

#include "m_plyr_camera.h"
#include "finder.h"                             /* SetNumerousDisp         */
#include "n_finder_dat.h"                       /* n_finder_dat            */
#include "../../graphics/graph2d/g2d_draw.h"    /* DISP_SPRT / DispSprD    */

/* The "DAMAGE" plate, and the ten digit records the number is laid out from. */
#define FD_DAMAGE_PLATE     0x48
#define FD_DAMAGE_NUM       0x78

/* One pulse every eight frames' worth of ramp -- CBlinkVariable::Blink() takes
 * the time to reach Max, and the return trip costs the same again. */
#define DD_BLINK_TIME       8

/* Digit pitch, in pixels. */
#define DD_CHARA_WIDTH      11

/* Additive, and depth-masked like the rest of the finder HUD. */
#define DD_ZBUF_NO_WRITE    0x000000010a000118ULL
#define DD_ALPHA_ADD        0x48

void CDamageDisp::Init(void)
{
    mWaiter.Reset();
    mOneBlink.Init();
}

void CDamageDisp::Work(void)                                            /* 8 */
{
    /* Non-zero only on the frame the hold expires, which re-arms the pulse
     * and leaves the counter at zero for the next Req() to reload. */
    if (mWaiter.Work())                                                 /* 10 */
    {
        mOneBlink.Blink(DD_BLINK_TIME);                                 /* 11 */
    }

    mOneBlink.Work();                                                   /* 12 */
}

/* iWaitCnt is how long the number holds before it stops pulsing;
 * PhotoInfoDisp() always passes 60, one NTSC second.  Wait() only seeds a
 * counter that has already run out, so re-requesting mid-hold does not
 * restart it. */
void CDamageDisp::Req(int iDamage, int iScore, int iWaitCnt)            /* 16 */
{
    mWaiter.Wait((short)iWaitCnt);
    mDamage = (short)iDamage;                                           /* 17 */
    mScore  = iScore;                                                   /* 18 */
}

void CDamageDisp::Draw(int fndr_mx, int fndr_my, int iAlpha)            /* 23 */
{
    DISP_SPRT ds;

    /* The pulse alpha is written out twice rather than kept in a local -- the
     * stabs name only `ds`, so this is one subexpression the ROM repeats. */
    CopySprDToSpr(&ds, &n_finder_dat[FD_DAMAGE_PLATE]);                 /* 29 */
    ds.zbuf   = DD_ZBUF_NO_WRITE;                                       /* 30 */
    ds.alphar = DD_ALPHA_ADD;                                           /* 31 */
    ds.x     += (float)fndr_mx;
    ds.y     += (float)fndr_my;                                         /* 32 */
    ds.alpha  = (u_char)(iAlpha * mOneBlink.Get() / mOneBlink.GetMax());/* 33 */
    DispSprD(&ds);                                                      /* 34 */

    SetNumerousDisp(&n_finder_dat[FD_DAMAGE_NUM], mDamage,
                    iAlpha * mOneBlink.Get() / mOneBlink.GetMax(),
                    DD_CHARA_WIDTH, fndr_mx, fndr_my, 1.0f, 0, 1);      /* 38 */
}
