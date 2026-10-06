// FILE: /home/zero_rom/zero2np/src/ingame/photo/spirit_gage.c
//
// The per-ghost spirit gauge -- the ring of blips that fills in the viewfinder
// while the camera is draining a ghost, and the multiplier the shot is scored
// with when the shutter finally goes.
//
// The whole widget is two numbers.  mPercent is the fill, 0..99, rebuilt from
// scratch every finder frame out of three rates the player controls (how close
// the ghost is, how centred it is, how solid it is).  mAlpha is a CWrkVariable
// the camera fades in and out as this ghost becomes, or stops being, the one
// being tracked.
//
// Two facts about the fill are worth having before reading Work():
//
//   * without a shutter chance it saturates at exactly 90 (70 + 15 + 5), and
//     with one it is compressed into 90..99.  That is why Draw() switches to
//     the shutter-chance colour at 90 and why CalcDamageRate() divides by 90:
//     "90" is a full gauge, and the ten values above it are the bonus.
//
//   * the ring is drawn as 13 whole blips plus one partial blip whose *alpha*
//     carries the fraction, in six steps.  Both the whole count and the
//     fraction come out of the same mPercent * 13 / 100.
//
// Reconstructed from the Feb 6 2004 prototype (SLES_523.84), spirit_gage.o.
// All six .text symbols plus both .data colour vectors and the .rodata damage
// table; .text is accounted for byte-for-byte (0x263718..0x263de0, once the
// fixed_array.h boilerplate at the head of the object file is set aside).
//
// One reading is not settled.  Draw()'s stabs name exactly six locals, and the
// value the loop runs on -- mPercent * 13 / 100 -- is not one of them, so it is
// a common subexpression of the blip count and iDivNum rather than a variable
// of its own.  GCC hoisted it into the entry block and tagged it line 62, the
// line iAlpha is on; the arithmetic below is identical either way.

#include "spirit_gage.h"
#include "../../common/utility2.h"              /* GetClampValF                */
#include "../../graphics/graph2d/g2d_draw.h"    /* DISP_SPRT / DispSprD        */
#include "../../sdk/libvu0.h"                   /* sceVu0CopyVector            */
#include "n_finder_dat.h"                       /* n_finder_dat                */

/* The GS register words the ring draws with.  Same ZBUF the rest of the finder
 * uses (mask bit set, so nothing writes depth); ALPHA 0x48 is additive, where
 * the backing plate under it is drawn 0x44 source-over. */
#define SG_ZBUF_MASK        0x000000010a000118ULL
#define SG_ALPHA_ADD        0x48

/* n_finder_dat[] indices.  Two sprites make one blip -- and the first of them
 * is a degenerate record in this build: entry 12 is all zeroes but for its pri
 * and alpha, so it has no texture window and covers no pixels.  The art was
 * never filled in; every blip you actually see is entry 13.  The ROM draws
 * both regardless, and so does this. */
#define FD_SPIRIT_GAGE_UNIT 12

/* The ring: 13 blips, 20 degrees apart, starting 100 degrees anticlockwise of
 * top and sweeping 260 degrees round.  SG_UNIT_ALPHA is how finely the last,
 * partial blip is faded -- six steps between empty and lit. */
#define SG_UNIT_NUM         13
#define SG_UNIT_ALPHA        6
#define SG_UNIT_ROT         20
#define SG_ROT_ORG        (-100)

/* Everything rotates and scales about the gauge's centre rather than about its
 * own anchor, so the ring turns as a ring.  Same offset from the finder origin
 * CNPlyrCamera::DrawSpiritGageBase() uses for the plate underneath. */
#define SG_CENTER_X        319
#define SG_CENTER_Y        225

/* A gauge this full is a full gauge: the colour changes here, and this is what
 * the damage ramp is measured against. */
#define SG_FULL_PERCENT     90

const CMIN_MAX<float> CSpiritGage::aDmgMultipleTbl[SHUTTER_CHANCE_STATE_MAX] =
{                                                              /* rodata 3c7ce0 */
    /* SHUTTER_CHANCE_NONE   */ { 0.29999998f, 1.0f },
    /* SHUTTER_CHANCE_NORMAL */ { 1.8f,        2.0f },
    /* SHUTTER_CHANCE_SP     */ { 2.0f,        2.0f },
};

/* --------------------------------------------------------------------------
 *  Init
 *
 *  The one method the ROM emitted out of line for enemy.o's benefit: the
 *  global constructor over ene_wrk[10] calls it on each slot's gauge.
 *
 *  mFlg is cleared here and never touched again -- nothing else in
 *  spirit_gage.o reads or writes offset 8.
 * ------------------------------------------------------------------------ */
void CSpiritGage::Init(void)
{                                                                       /* 36 */
    mPercent = 0;                                                       /* 37 */
    mAlpha.Init();                                                      /* 38 */
    mFlg = 0;                                                           /* 39 */
}

/* --------------------------------------------------------------------------
 *  CalcDamageRate
 *
 *  The multiplier this shot earns, read off the ramp for the shutter chance
 *  the field is in.  fRate is deliberately not clamped: a shutter chance puts
 *  mPercent above 90, so it arrives above 1.0 and the result lands above the
 *  band's own maximum (a normal chance can reach 2.02 rather than 2.0).
 * ------------------------------------------------------------------------ */
float CSpiritGage::CalcDamageRate(SHUTTER_CHANCE_STATE SPState)
{                                                                       /* 45 */
    float fRate = (float)mPercent / (float)SG_FULL_PERCENT;             /* 46 */

    return aDmgMultipleTbl[SPState].GetByProportion(fRate);             /* 48 */
}

/* --------------------------------------------------------------------------
 *  Draw
 *
 *  13 whole blips laid round the ring at 20 degree steps, then one more at the
 *  next step whose alpha carries the fraction that the whole count truncated
 *  away.  iDivNum is that fraction already scaled into 0..5:
 *
 *      mPercent * 13 * 6 / 100  -  (mPercent * 13 / 100) * 6
 *
 *  is six times the fractional part of mPercent * 13 / 100, so dividing the
 *  alpha back by six at line 122 leaves the trailing blip at exactly the right
 *  brightness.  At 100% the fraction is zero and the fourteenth blip is drawn
 *  invisible.
 *
 *  fx / fy / fcx / fcy are float locals the ROM keeps in $f20-$f27 across every
 *  call in here.  They leave no stab -- this build records int locals and not
 *  float ones -- which is why the local list below runs one short of the code.
 * ------------------------------------------------------------------------ */
void CSpiritGage::Draw(int fndr_mx, int fndr_my, int iMasterAlpha, float fScale)
{                                                                       /* 60 */
    int       iAlpha  = mAlpha.Get() * iMasterAlpha / 128;              /* 62 */
    int       iDivNum = mPercent * SG_UNIT_NUM * SG_UNIT_ALPHA / 100
                      - mPercent * SG_UNIT_NUM / 100 * SG_UNIT_ALPHA;   /* 63 */

    int       iRot    = SG_ROT_ORG;                                     /* 65 */
    DISP_SPRT ds;
    int       r, g, b;

    float     fx  = (float)fndr_mx;
    float     fy  = (float)fndr_my;
    float     fcx = (float)(fndr_mx + SG_CENTER_X);
    float     fcy = (float)(fndr_my + SG_CENTER_Y);

    /* Held as float vectors and copied a quadword at a time, then truncated to
     * the three bytes the GS wants -- both are over 128 on at least one
     * channel, so the ring is drawn brighter than the plate it sits on. */
    static float ShutterChanceRgb[4] = { 212.0f,  21.0f,  3.0f, 0.0f }; /* data 34fe60 */
    static float NormalRgb[4]        = { 188.0f, 140.0f, 61.0f, 0.0f }; /* data 34fe70 */
    float        Rgb[4];
    int          i;

    if (SG_FULL_PERCENT <= mPercent)                                    /* 83 */
    {
        sceVu0CopyVector(Rgb, ShutterChanceRgb);                        /* 84 */
    }
    else
    {
        sceVu0CopyVector(Rgb, NormalRgb);                               /* 86 */
    }

    r = (int)Rgb[0];                                                    /* 90 */
    g = (int)Rgb[1];                                                    /* 91 */
    b = (int)Rgb[2];                                                    /* 92 */

    for (i = 0; i < mPercent * SG_UNIT_NUM / 100; i++)                  /* 96 */
    {
        CopySprDToSpr(&ds, &n_finder_dat[FD_SPIRIT_GAGE_UNIT + 0]);     /* 97 */
        ds.zbuf   = SG_ZBUF_MASK;                                       /* 98 */
        ds.alphar = SG_ALPHA_ADD;                                       /* 99 */
        ds.alpha  = (u_char)iAlpha;                                     /* 100 */
        ds.r = (u_char)r;  ds.g = (u_char)g;  ds.b = (u_char)b;         /* 101 */
        ds.x     += fx;                                                 /* 102 */
        ds.y     += fy;
        ds.crx    = fcx;                                                /* 103 */
        ds.cry    = fcy;
        ds.rot    = (float)iRot;
        ds.csx    = fcx;                                                /* 104 */
        ds.csy    = fcy;
        ds.scw    = fScale;
        ds.sch    = fScale;
        ds.tex1   = 0x161;                                              /* 105 */
        DispSprD(&ds);                                                  /* 106 */

        CopySprDToSpr(&ds, &n_finder_dat[FD_SPIRIT_GAGE_UNIT + 1]);     /* 108 */
        ds.zbuf   = SG_ZBUF_MASK;                                       /* 109 */
        ds.alphar = SG_ALPHA_ADD;                                       /* 110 */
        ds.alpha  = (u_char)iAlpha;                                     /* 111 */
        ds.r = (u_char)r;  ds.g = (u_char)g;  ds.b = (u_char)b;         /* 112 */
        ds.x     += fx;                                                 /* 113 */
        ds.y     += fy;
        ds.crx    = fcx;                                                /* 114 */
        ds.cry    = fcy;
        ds.rot    = (float)iRot;
        ds.csx    = fcx;                                                /* 115 */
        ds.csy    = fcy;
        ds.scw    = fScale;
        ds.sch    = fScale;
        ds.tex1   = 0x161;                                              /* 116 */
        DispSprD(&ds);                                                  /* 117 */

        iRot += SG_UNIT_ROT;                                            /* 119 */
    }                                                                   /* 120 */

    /* The trailing blip, at the step the loop stopped on. */
    iAlpha = iAlpha * iDivNum / SG_UNIT_ALPHA;                          /* 122 */

    CopySprDToSpr(&ds, &n_finder_dat[FD_SPIRIT_GAGE_UNIT + 0]);         /* 123 */
    ds.zbuf   = SG_ZBUF_MASK;                                           /* 124 */
    ds.alphar = SG_ALPHA_ADD;                                           /* 125 */
    ds.alpha  = (u_char)iAlpha;                                         /* 126 */
    ds.r = (u_char)r;  ds.g = (u_char)g;  ds.b = (u_char)b;             /* 127 */
    ds.x     += fx;                                                     /* 128 */
    ds.y     += fy;
    ds.crx    = fcx;                                                    /* 129 */
    ds.cry    = fcy;
    ds.rot    = (float)iRot;
    ds.csx    = fcx;                                                    /* 130 */
    ds.csy    = fcy;
    ds.scw    = fScale;
    ds.sch    = fScale;
    ds.tex1   = 0x161;                                                  /* 131 */
    DispSprD(&ds);                                                      /* 132 */

    CopySprDToSpr(&ds, &n_finder_dat[FD_SPIRIT_GAGE_UNIT + 1]);         /* 134 */
    ds.zbuf   = SG_ZBUF_MASK;                                           /* 135 */
    ds.alphar = SG_ALPHA_ADD;                                           /* 136 */
    ds.alpha  = (u_char)iAlpha;                                         /* 137 */
    ds.r = (u_char)r;  ds.g = (u_char)g;  ds.b = (u_char)b;             /* 138 */
    ds.x     += fx;                                                     /* 139 */
    ds.y     += fy;
    ds.crx    = fcx;                                                    /* 140 */
    ds.cry    = fcy;
    ds.rot    = (float)iRot;
    ds.csx    = fcx;                                                    /* 141 */
    ds.csy    = fcy;
    ds.scw    = fScale;
    ds.sch    = fScale;
    ds.tex1   = 0x161;                                                  /* 142 */
    DispSprD(&ds);                                                      /* 143 */
}

/* --------------------------------------------------------------------------
 *  FadeIn / FadeOut
 *
 *  One store each.  60 is mAlpha's own maximum, so +/-20 is three frames end
 *  to end in either direction.
 * ------------------------------------------------------------------------ */
void CSpiritGage::FadeIn(void)
{                                                                       /* 147 */
    mAlpha.SetAddVal(20);                                 /* variable.h 342/343 */
}

void CSpiritGage::FadeOut(void)
{                                                                       /* 151 */
    mAlpha.SetAddVal(-20);                                /* variable.h 342/343 */
}

/* --------------------------------------------------------------------------
 *  Work
 *
 *  Rebuilds the fill from scratch each finder frame -- there is no
 *  accumulation, so a ghost that leaves the frame empties its ring at once.
 *  The three terms are weighted 70 / 15 / 5, which is what makes distance the
 *  thing the player is really steering.
 *
 *  A shutter chance replaces the whole scale: whatever the three rates came to
 *  is squeezed into 0..9 and lifted onto a floor of 90, so any shot taken in a
 *  chance scores at least a full gauge.
 *
 *  iMinPercent -- the floor the loaded film guarantees -- deliberately does not
 *  apply to an empty gauge: a ghost that scored nothing at all stays at zero
 *  rather than being lifted to the film's minimum.
 * ------------------------------------------------------------------------ */
void CSpiritGage::Work(float fDistanceRate, float fCenterRate, float fAlphaRate,
                       SHUTTER_CHANCE_STATE SPState, int iMinPercent)
{                                                                       /* 156 */
    int iPercent;

    mAlpha.Work();                                                      /* 159 */

    fDistanceRate = GetClampValF(fDistanceRate, 1.0f, 0.0f);            /* 162 */
    fCenterRate   = GetClampValF(fCenterRate,   1.0f, 0.0f);            /* 163 */
    fAlphaRate    = GetClampValF(fAlphaRate,    1.0f, 0.0f);            /* 164 */

    /* Each term is truncated on its own before they are added, so the sum can
     * sit up to two below the rounded total.  90 is the ceiling. */
    iPercent = (int)(fDistanceRate * 70.0f)                             /* 168 */
             + (int)(fCenterRate   * 15.0f)                             /* 169 */
             + (int)(fAlphaRate    *  5.0f);                            /* 170 */

    switch (SPState)                                                    /* 173 */
    {
    case SHUTTER_CHANCE_NONE:
        break;

    case SHUTTER_CHANCE_NORMAL:
    case SHUTTER_CHANCE_SP:
        /* 0..90 becomes 90..99, which is what puts the gauge into its
         * shutter-chance colour and takes CalcDamageRate() past 1.0. */
        iPercent = iPercent / 10 + SG_FULL_PERCENT;                     /* 182 */
        break;

    default:
        break;
    }

    if (iPercent != 0 && iPercent < iMinPercent)                        /* 187 */
    {
        mPercent = iMinPercent;                                         /* 188 */
    }
    else
    {
        mPercent = iPercent;                                            /* 190 */
    }
}
