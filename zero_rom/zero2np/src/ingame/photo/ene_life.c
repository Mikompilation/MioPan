// FILE: /home/zero_rom/zero2np/src/ingame/photo/ene_life.c
//
// The ghost's health readout across the top of the viewfinder.
//
// Three values, not one.  now_hp_percent is where the ghost actually is,
// disp_hp_percent the bright bar that chases it down, and old_hp_percent a
// second, darker bar that lags further behind still -- so a hit shows as a
// bright bar dropping immediately and a red tail draining after it.  The tail
// is held for EL_RED_BAR_WAIT frames before it starts moving, which is what
// makes a burst of damage read as one wound rather than several.
//
// enemy.c drives it through finder.c: FrameLenSet() says how wide the bar is
// on screen at all (0 when no ghost qualifies, which is what suppresses the
// whole readout), Set() snaps all three when the tracked ghost changes, and
// Decrease() feeds a new value in.  SetDamage() is the shot's total, pushed in
// by CNPlyrCamera::PhotoInfoDispNew().
//
// Reconstructed from the Feb 6 2004 prototype (SLES_523.84), ene_life.o.
// All six ZERO2.MAP .text symbols.

#include "m_plyr_camera.h"
#include "finder.h"                             /* SetNumerousDisp         */
#include "n_finder_dat.h"                       /* n_finder_dat            */
#include "../../graphics/graph2d/g2d_draw.h"    /* DISP_SPRT / DispSprD    */

/* n_finder_dat[] indices.  The bar is built from a left cap, a middle piece
 * stretched to length, a right cap that rides on the end of it, and a plate
 * above; 0x37 is the one-pixel fill drawn twice, once per value. */
#define FD_ENELIFE_CAP_L    0x33
#define FD_ENELIFE_PLATE    0x34
#define FD_ENELIFE_CAP_R    0x35
#define FD_ENELIFE_MIDDLE   0x36
#define FD_ENELIFE_FILL     0x37
#define FD_ENELIFE_DMG_PLATE 0x48
#define FD_ENELIFE_DMG_NUM  0x78

/* How many copies of the middle piece a full-length bar is worth. */
#define EL_BAR_UNITS        14.0f

/* The chase rates, per frame, as a fraction of full health.  The bright bar
 * moves eight times faster than the red tail behind it. */
#define EL_DISP_STEP        0.04f       /* lit4 3ee104 */
#define EL_OLD_STEP         0.005f      /* lit4 3ee108 */

/* How long the red tail waits before it starts draining. */
#define EL_RED_BAR_WAIT     35

/* How fast the damage number fades in and out. */
#define EL_DMG_FADE_IN      10
#define EL_DMG_FADE_OUT   (-10)

/* Digit pitch for the damage number. */
#define EL_DMG_CHARA_WIDTH  11

/* The red tail's colour. */
#define EL_OLD_BAR_R        0x60

/* The bar draws with plain depth-tested source-over; the damage number over it
 * is additive and depth-masked. */
#define EL_TEST_ALWAYS      0x0000000000030003ULL
#define EL_TEX1             0x0000000000000161ULL
#define EL_ZBUF_NO_WRITE    0x000000010a000118ULL
#define EL_ALPHA_ADD        0x48

void CEneLife::Work(void)                                               /* 8 */
{
    if (now_hp_percent < old_hp_percent)                                /* 10 */
    {
        if (now_hp_percent < disp_hp_percent)                           /* 12 */
        {
            disp_hp_percent -= EL_DISP_STEP;                            /* 13 */

            if (disp_hp_percent < now_hp_percent)                       /* 14 */
            {
                disp_hp_percent = now_hp_percent;                       /* 16 */
                red_bar_wait    = EL_RED_BAR_WAIT;                      /* 17 */
                mDamageAlpha.SetAddVal(EL_DMG_FADE_IN);
            }
        }
        else
        {
            /* The bright bar has caught up; hold, then start the tail. */
            if (red_bar_wait > 0)                                       /* 23 */
            {
                red_bar_wait--;                                         /* 24 */
            }
            else
            {
                old_hp_percent -= EL_OLD_STEP;                          /* 27 */

                if (old_hp_percent < now_hp_percent)                    /* 28 */
                {
                    old_hp_percent = now_hp_percent;                    /* 30 */
                    mDamageAlpha.SetAddVal(EL_DMG_FADE_OUT);
                }
            }
        }
    }
    else
    {
        /* Healed, or nothing to show: both tails snap forward. */
        old_hp_percent = now_hp_percent;                                /* 36 */
        red_bar_wait   = EL_RED_BAR_WAIT;                               /* 37 */
    }

    mDamageAlpha.Work();                                                /* 39 */
}

void CEneLife::SetDamage(int iDamage)                                   /* 43 */
{
    mDamage = (short)iDamage;
}

void CEneLife::Draw(int fndr_mx, int fndr_my, int iAlpha)               /* 49 */
{
    int x;
    int y;

    /* A zero-length bar is how the caller says "no ghost worth showing". */
    if (ene_hp_len != 0.0f)                                             /* 55 */
    {
        {
            DISP_SPRT ds;

            CopySprDToSpr(&ds, &n_finder_dat[FD_ENELIFE_PLATE]);        /* 65 */
            ds.x    += (float)fndr_mx;
            ds.y    += (float)fndr_my;                                  /* 66 */
            ds.test  = EL_TEST_ALWAYS;                                  /* 67 */
            ds.alpha = (u_char)iAlpha;                                  /* 68 */
            ds.tex1  = EL_TEX1;                                         /* 69 */
            DispSprD(&ds);                                              /* 70 */

            /* The middle piece, stretched to the bar's length. */
            CopySprDToSpr(&ds, &n_finder_dat[FD_ENELIFE_MIDDLE]);       /* 73 */
            ds.csx   = ds.x + (float)fndr_mx;
            ds.csy   = ds.y + (float)fndr_my;
            ds.x     = ds.csx;
            ds.y     = ds.csy;                                          /* 74 */
            ds.scw   = ene_hp_len * EL_BAR_UNITS;
            ds.sch   = 1.0f;                                            /* 75 */
            ds.test  = EL_TEST_ALWAYS;                                  /* 76 */
            ds.alpha = (u_char)iAlpha;                                  /* 77 */
            ds.tex1  = EL_TEX1;                                         /* 78 */
            DispSprD(&ds);                                              /* 79 */

            /* The right cap rides on the end of it. */
            CopySprDToSpr(&ds, &n_finder_dat[FD_ENELIFE_CAP_R]);        /* 82 */
            ds.x    += (float)fndr_mx
                     + (float)n_finder_dat[FD_ENELIFE_MIDDLE].w
                       * ene_hp_len * EL_BAR_UNITS;
            ds.y    += (float)fndr_my;                                  /* 83 */
            ds.test  = EL_TEST_ALWAYS;                                  /* 86 */
            ds.alpha = (u_char)iAlpha;                                  /* 87 */
            ds.tex1  = EL_TEX1;                                         /* 88 */
            DispSprD(&ds);                                              /* 89 */

            CopySprDToSpr(&ds, &n_finder_dat[FD_ENELIFE_CAP_L]);        /* 92 */
            ds.x    += (float)fndr_mx;
            ds.y    += (float)fndr_my;                                  /* 93 */
            ds.test  = EL_TEST_ALWAYS;                                  /* 94 */
            ds.alpha = (u_char)iAlpha;                                  /* 95 */
            ds.tex1  = EL_TEX1;                                         /* 96 */
            DispSprD(&ds);                                              /* 97 */
        }

        x = n_finder_dat[FD_ENELIFE_FILL].x + fndr_mx;                  /* 101 */
        y = n_finder_dat[FD_ENELIFE_FILL].y + fndr_my;                  /* 102 */

        {
            /* The red tail, behind the bright bar. */
            DISP_SPRT ds;

            CopySprDToSpr(&ds, &n_finder_dat[FD_ENELIFE_FILL]);         /* 108 */
            ds.csx   = (float)x;   ds.csy = (float)y;
            ds.x     = (float)x;   ds.y   = (float)y;                   /* 109 */
            ds.scw   = old_hp_percent * ene_hp_len * EL_BAR_UNITS;
            ds.sch   = 1.0f;                                            /* 110 */
            ds.r     = EL_OLD_BAR_R;   ds.g = 0;   ds.b = 0;            /* 111 */
            ds.test  = EL_TEST_ALWAYS;                                  /* 112 */
            ds.alpha = (u_char)iAlpha;                                  /* 113 */
            ds.tex1  = EL_TEX1;                                         /* 114 */
            DispSprD(&ds);                                              /* 115 */
        }

        {
            /* The bright bar. */
            DISP_SPRT ds;

            CopySprDToSpr(&ds, &n_finder_dat[FD_ENELIFE_FILL]);         /* 121 */
            ds.csx   = (float)x;   ds.csy = (float)y;
            ds.x     = (float)x;   ds.y   = (float)y;                   /* 122 */
            ds.scw   = disp_hp_percent * ene_hp_len * EL_BAR_UNITS;
            ds.sch   = 1.0f;                                            /* 123 */
            ds.test  = EL_TEST_ALWAYS;                                  /* 124 */
            ds.alpha = (u_char)iAlpha;                                  /* 125 */
            ds.tex1  = EL_TEX1;                                         /* 126 */
            DispSprD(&ds);                                              /* 127 */
        }

        {
            /* The damage number, on its own fade. */
            DISP_SPRT ds;

            CopySprDToSpr(&ds, &n_finder_dat[FD_ENELIFE_DMG_PLATE]);    /* 137 */
            ds.zbuf   = EL_ZBUF_NO_WRITE;                               /* 138 */
            ds.alphar = EL_ALPHA_ADD;                                   /* 139 */
            ds.x     += (float)fndr_mx;
            ds.y     += (float)fndr_my;                                 /* 140 */
            ds.alpha  = (u_char)(iAlpha * mDamageAlpha.Get()
                                 / mDamageAlpha.GetMax());              /* 141 */
            DispSprD(&ds);                                              /* 142 */
        }

        SetNumerousDisp(&n_finder_dat[FD_ENELIFE_DMG_NUM], mDamage,
                        iAlpha * mDamageAlpha.Get() / mDamageAlpha.GetMax(),
                        EL_DMG_CHARA_WIDTH, fndr_mx, fndr_my,
                        1.0f, 0, 1);                                    /* 146 */
    }                                                                   /* 149 */
}

/* A new, lower value: the bright bar starts where the old one was so it has
 * something to fall from, and the damage number is reset to invisible. */
void CEneLife::Decrease(float new_hp_per)                               /* 153 */
{
    if (now_hp_percent != new_hp_per)                                   /* 154 */
    {
        old_hp_percent  = now_hp_percent;
        now_hp_percent  = new_hp_per;
        disp_hp_percent = now_hp_percent;                               /* 155 */
        mDamageAlpha.Init();
    }
}

/* A different ghost: all three snap to it, so nothing animates across. */
void CEneLife::Set(float new_hp_per)                                    /* 162 */
{
    if (now_hp_percent != new_hp_per)                                   /* 162 */
    {
        now_hp_percent  = new_hp_per;
        disp_hp_percent = new_hp_per;
        old_hp_percent  = new_hp_per;                                   /* 163 */
        mDamageAlpha.Init();
    }
}

void CEneLife::FrameLenSet(float len)                                   /* 169 */
{
    ene_hp_len = len;
}
