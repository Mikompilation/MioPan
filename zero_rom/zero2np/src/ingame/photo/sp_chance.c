// FILE: /home/zero_rom/zero2np/src/ingame/photo/sp_chance.c
//
// The shutter-chance lamp: the indicator in the top-right of the viewfinder
// that lights when a ghost is open to a chance shot, and the looping cue that
// goes with it.
//
// Three sprites, one state bit and two independent alpha ramps:
//
//   the plate   (n_finder_dat[31]) is always drawn, at plain master alpha
//   the core    (n_finder_dat[33]) rides mLampAlpha, which slams to full the
//                                  frame the lamp comes on and bleeds away at
//                                  8 a frame when it goes off
//   the halo    (n_finder_dat[32]) rides mAlpha, a CBlinkSwitchVariable whose
//                                  sweep time is 1 -- a full 0..128 step per
//                                  tick, so it strobes rather than pulses
//
// mbSeFlg and mSPFlg are two bits of one word.  mSPFlg is the chance itself,
// which player.c raises and clears through Set(); mbSeFlg is the photo phase's
// mute, so photo.c can silence the cue over a shot without disturbing the lamp.
// Work() is handed the fitted-part flag every frame rather than being gated on
// it, which is what lets the lamp tell "no chance right now" from "this camera
// cannot show one".
//
// The lamp owns a sound handle of its own, which is why CNPlyrCamera::Release()
// exists at all: Release() and SEDisable() both have to hand it back.
//
// Reconstructed from the Feb 6 2004 prototype (SLES_523.84), sp_chance.o.
// All seven `.text` exports; `.text` is accounted for byte-for-byte
// (0x263288..0x263718) once the fixed_array.h boilerplate at the head of the
// object file is set aside.  There is no `.data` and no `.rodata` beyond the
// compiler's own assert literals.
//
// Almost every statement in here is a single inlined accessor, so its own line
// note is superseded by the header's and leaves nothing behind; the annotations
// name the header and line where that is all there is to measure.

#include "m_plyr_camera.h"
#include "../../graphics/graph2d/g2d_draw.h"    /* DISP_SPRT / DispSprD        */
#include "finder.h"                             /* CFINDER_SND_BUF_PLAY        */
#include "n_finder_dat.h"                       /* n_finder_dat                */

/* The finder's usual masked ZBUF; the plate blends and the two lit pieces are
 * added on top of it. */
#define SPC_ZBUF_MASK       0x000000010a000118ULL
#define SPC_ALPHA_BLEND     0x44
#define SPC_ALPHA_ADD       0x48

/* n_finder_dat[] indices.  32 (33x34) encloses 31 (16x16), and 33 (6x6) sits
 * inside it -- halo, plate, core. */
#define FD_SP_CHANCE_PLATE  31
#define FD_SP_CHANCE_HALO   32
#define FD_SP_CHANCE_CORE   33

/* The looping cue, out of the finder's own bank. */
#define SPC_SE_NO           13

/* How fast mLampAlpha moves: straight to full when the chance opens, and a
 * sixteen-frame bleed when it closes. */
#define SPC_LAMP_UP        128
#define SPC_LAMP_DOWN       (-8)

/* --------------------------------------------------------------------------
 *  Init
 *
 *  Note it does not touch mSe.  CSND_BUF_PLAY's constructor is the only thing
 *  that seeds play_id, and CSND_BUF_PLAY_NO_ID is 0x300000 rather than 0 -- so
 *  an object that reached here zero-filled instead of constructed would read as
 *  "a voice is already playing" and the cue would never start.
 * ------------------------------------------------------------------------ */
void CSPChance::Init(void)
{                                                                        /* 4 */
    mAlpha.Init();                                       /* variable.h 639 */
    mLampAlpha.Init();                                   /* variable.h 317 */
    mSPFlg  = 0;                                                         /* 7 */
    mbSeFlg = 1;                                                         /* 8 */
}

/* --------------------------------------------------------------------------
 *  Release
 *
 *  Hands the voice back.  CNPlyrCamera::Release() exists for this one call.
 * ------------------------------------------------------------------------ */
void CSPChance::Release(void)
{                                                                       /* 11 */
    mSe.Stop(1);                                       /* zero2_util.h 56 */
}

/* --------------------------------------------------------------------------
 *  Set
 *
 *  player.c raises and clears the chance here.  Note the cue is stopped on the
 *  way *down* only -- raising it leaves the voice to Work(), which will not
 *  start one unless the camera has the part fitted and the phase has not muted
 *  it.
 * ------------------------------------------------------------------------ */
void CSPChance::Set(int bFlg)
{                                                                       /* 17 */
    mSPFlg = bFlg;                                                      /* 18 */

    if (bFlg == 0)                                                      /* 21 */
    {
        mSe.Stop(1);                                   /* zero2_util.h 56 */
    }
}

void CSPChance::SEEnable(void)
{                                                                       /* 28 */
    mbSeFlg = 1;                                                        /* 29 */
}

void CSPChance::SEDisable(void)
{                                                                       /* 32 */
    mbSeFlg = 0;                                                        /* 33 */
    mSe.Stop(1);                                       /* zero2_util.h 56 */
}

/* --------------------------------------------------------------------------
 *  Work
 *
 *  bSeFlg is the fitted-part test, not a "should I run" gate: the lamp still
 *  ramps down and the strobe still stops when it is 0, only the cue is held
 *  back.  Between it and mbSeFlg the voice has two independent mutes.
 *
 *  The cue is started only when nothing is already playing, so the assert
 *  inside CFINDER_SND_BUF_PLAY::Play() is dead here -- the same pattern every
 *  other call site in the build uses.
 * ------------------------------------------------------------------------ */
void CSPChance::Work(int bSeFlg)
{                                                                       /* 36 */
    if (mSPFlg != 0)                                                    /* 38 */
    {
        if (bSeFlg != 0 && mbSeFlg != 0)                                /* 39 */
        {
            if (mSe.IsPlaying() == 0)                  /* zero2_util.h 52 */
            {
                mSe.Play(SPC_SE_NO, 1, 1, 0, NULL, 0x3200, 0x1000);
                                                          /* finder.h 219 */
            }
        }
        else
        {
            mSe.Stop(1);                               /* zero2_util.h 56 */
        }

        mAlpha.BlinkOn();                                /* variable.h 645 */
        mLampAlpha.SetAddVal(SPC_LAMP_UP);               /* variable.h 342 */
    }
    else
    {
        mSe.Stop(15);                                  /* zero2_util.h 56 */

        mAlpha.BlinkOff();                               /* variable.h 648 */
        mLampAlpha.SetAddVal(SPC_LAMP_DOWN);             /* variable.h 342 */
    }

    mAlpha.Work();                                                      /* 56 */
    mLampAlpha.Work();                                                  /* 57 */
}

/* --------------------------------------------------------------------------
 *  Draw
 *
 *  fx / fy are float locals the ROM keeps in $f20/$f21 across all three calls;
 *  they leave no stab, so the local list below runs two short of the code.
 * ------------------------------------------------------------------------ */
void CSPChance::Draw(int fndr_mx, int fndr_my, int iAlpha)
{                                                                       /* 61 */
    int       iDispAlpha;
    DISP_SPRT ds;

    float     fx = (float)fndr_mx;
    float     fy = (float)fndr_my;

    /* The plate is unconditional: the lamp housing is part of the finder art
     * whether a chance is running or not. */
    CopySprDToSpr(&ds, &n_finder_dat[FD_SP_CHANCE_PLATE]);              /* 66 */
    ds.zbuf   = SPC_ZBUF_MASK;                                          /* 67 */
    ds.alphar = SPC_ALPHA_BLEND;                                        /* 68 */
    ds.alpha  = (u_char)iAlpha;                                         /* 69 */
    ds.x     += fx;                                                     /* 70 */
    ds.y     += fy;
    DispSprD(&ds);                                                      /* 71 */

    iDispAlpha = iAlpha * mLampAlpha.Get() / 128;        /* variable.h 352 */

    CopySprDToSpr(&ds, &n_finder_dat[FD_SP_CHANCE_CORE]);               /* 81 */
    ds.zbuf   = SPC_ZBUF_MASK;                                          /* 82 */
    ds.alphar = SPC_ALPHA_ADD;                                          /* 83 */
    ds.alpha  = (u_char)iDispAlpha;                                     /* 84 */
    ds.x     += fx;                                                     /* 85 */
    ds.y     += fy;
    DispSprD(&ds);                                                      /* 86 */

    iDispAlpha = iAlpha * mAlpha.Get() / 128;            /* variable.h 657 */

    CopySprDToSpr(&ds, &n_finder_dat[FD_SP_CHANCE_HALO]);               /* 90 */
    ds.zbuf   = SPC_ZBUF_MASK;                                          /* 91 */
    ds.alphar = SPC_ALPHA_ADD;                                          /* 92 */
    ds.alpha  = (u_char)iDispAlpha;                                     /* 93 */
    ds.x     += fx;                                                     /* 94 */
    ds.y     += fy;
    DispSprD(&ds);                                                      /* 95 */
}
