// FILE: /home/zero_rom/zero2np/src/ingame/photo/film_no.c
//
// The film readout in the corner of the viewfinder: how many shots are left,
// and which film is loaded.
//
// One blink switch is the whole of its state -- CNPlyrCamera::Main() turns it
// on while the shutter is charged and calls Init(), not BlinkOff(), to turn it
// off again, which is the ROM's own asymmetry.  The switch runs 70..90, so the
// readout never fades below about 90 of 128; it pulses rather than blinks.
//
// Two special cases in Draw(), both about Type-07 and the unnumbered fifth
// film.  Type-07 has unlimited shots, so its count is drawn as two dashes
// instead of a number; the fifth film has no number at all, so a word plate
// goes out where the digits would be.  Both plates shift left in German, which
// is what the two two-entry point tables are for.
//
// Reconstructed from the Feb 6 2004 prototype (SLES_523.84), film_no.o.
// All five ZERO2.MAP .text symbols plus both .sdata point tables.

#include "m_plyr_camera.h"
#include "finder.h"                             /* SetNumerousDisp         */
#include "n_finder_dat.h"                       /* n_finder_dat            */
#include "../../graphics/graph2d/g2d_draw.h"    /* DISP_SPRT / DispSprD    */
#include "../../system/os/system.h"             /* GetLanguage             */

/* n_finder_dat[] indices. */
#define FD_FILM_PLATE       0x28    /* the frame the whole readout sits in   */
#define FD_FILM_DIGIT       0x29    /* ten consecutive digit records         */
#define FD_FILM_TYPE_CHAR   0x9b    /* the fifth film's word, in place of a number */
#define FD_FILM_INF_BAR     0x9c    /* one dash; drawn twice for Type-07     */

/* Type-07 is the unlimited film -- the count reads "--" rather than a number.
 * Type number 0 is the fifth film, which has no number to print. */
#define FILM_TYPE_UNLIMITED 7
#define FILM_TYPE_NONAME    0

/* The blink switch runs 70..90 as a percentage; the sprite alpha it drives is
 * out of 128. */
#define FN_ALPHA_MAX        128
#define FN_ALPHA_PERCENT    100

/* Layout.  The count sits at the right-hand end of the plate at full size; the
 * type number is smaller and sits under it. */
#define FN_NUM_X            0x24d
#define FN_NUM_Y            0x1d
#define FN_NUM_WIDTH        13
#define FN_TYPE_Y           0x22
#define FN_TYPE_WIDTH       6
#define FN_TYPE_SCALE       0.59999996f     /* lit4 3ee284, one ulp below 0.6 */
#define FN_TYPE_FIGURES     2               /* "07" keeps its leading zero    */
#define FN_BAR_PITCH        13.0f

/* Language 2 is German, whose words are wider; both plates shift left for it. */
#define FN_LANG_GERMAN      2

/* Depth-masked like the rest of the finder HUD. */
#define FN_ZBUF_NO_WRITE    0x000000010a000118ULL

/* Where the type readout starts, indexed by "is this German".  MyPoint carries
 * only an x -- the y is the same either way. */
static MyPoint aTypeNumPoint[2]  = { { 555 }, { 507 } };    /* sdata 3f0800 */
static MyPoint aTypeCharPoint[2] = { { 547 }, { 496 } };    /* sdata 3f0808 */

void CFilmNo::Work(void)                                                /* 8 */
{
    mBlinkAlpha.Work();                                                 /* 9 */
}

void CFilmNo::Init(void)
{
    mBlinkAlpha.Init();
}

void CFilmNo::BlinkOn(void)
{
    mBlinkAlpha.BlinkOn();
}

void CFilmNo::BlinkOff(void)
{
    mBlinkAlpha.BlinkOff();
}

/* `number` is the shots left and `iFilmTypeNo` the printed film number
 * (7/14/61/90, or 0 for the fifth film) -- not the film index.
 *
 * The pulse alpha is written out at every use rather than kept in a local:
 * the stabs name only `n`, and variable.h line 657 appears three separate
 * times in the prologue, so the ROM really did repeat the expression. */
void CFilmNo::Draw(int number, int iFilmTypeNo, int fndr_mx, int fndr_my,
                   int iAlpha)                                          /* 72 */
{
    int n = number < 100 ? number : 99;                                 /* 75 */

    (void)iAlpha;               /* the pulse is the only alpha this readout has */

    if (iFilmTypeNo == FILM_TYPE_UNLIMITED)                             /* 82 */
    {
        DISP_SPRT ds;

        CopySprDToSpr(&ds, &n_finder_dat[FD_FILM_INF_BAR]);             /* 84 */
        ds.zbuf  = FN_ZBUF_NO_WRITE;                                    /* 85 */
        ds.x    += (float)fndr_mx;
        ds.y    += (float)fndr_my;                                      /* 86 */
        ds.alpha = (u_char)(mBlinkAlpha.Get() * FN_ALPHA_MAX
                            / FN_ALPHA_PERCENT);                        /* 87 */
        DispSprD(&ds);                                                  /* 88 */

        ds.x += FN_BAR_PITCH;
        ds.y += 0.0f;                                                   /* 89 */
        DispSprD(&ds);                                                  /* 90 */
    }
    else
    {
        SetNumerousDisp(&n_finder_dat[FD_FILM_DIGIT], n,
                        mBlinkAlpha.Get() * FN_ALPHA_MAX / FN_ALPHA_PERCENT,
                        FN_NUM_WIDTH, fndr_mx + FN_NUM_X, fndr_my + FN_NUM_Y,
                        1.0f, 0, 0);                                    /* 93 */
    }

    {
        DISP_SPRT ds;

        CopySprDToSpr(&ds, &n_finder_dat[FD_FILM_PLATE]);               /* 101 */
        ds.zbuf  = FN_ZBUF_NO_WRITE;                                    /* 102 */
        ds.x    += (float)fndr_mx;
        ds.y    += (float)fndr_my;                                      /* 103 */
        ds.alpha = (u_char)(mBlinkAlpha.Get() * FN_ALPHA_MAX
                            / FN_ALPHA_PERCENT);                        /* 104 */
        DispSprD(&ds);                                                  /* 105 */
    }

    if (iFilmTypeNo == FILM_TYPE_NONAME)                                /* 109 */
    {
        DISP_SPRT ds;

        CopySprDToSpr(&ds, &n_finder_dat[FD_FILM_TYPE_CHAR]);           /* 111 */
        ds.zbuf  = FN_ZBUF_NO_WRITE;                                    /* 112 */
        ds.x     = (float)aTypeCharPoint[GetLanguage() == FN_LANG_GERMAN].x
                 + (float)fndr_mx;                                      /* 113 */
        ds.y    += (float)fndr_my;                                      /* 114 */
        ds.alpha = (u_char)(mBlinkAlpha.Get() * FN_ALPHA_MAX
                            / FN_ALPHA_PERCENT);                        /* 115 */
        DispSprD(&ds);                                                  /* 116 */
    }
    else
    {
        SetNumerousDisp(&n_finder_dat[FD_FILM_DIGIT], iFilmTypeNo,
                        mBlinkAlpha.Get() * FN_ALPHA_MAX / FN_ALPHA_PERCENT,
                        FN_TYPE_WIDTH,
                        aTypeNumPoint[GetLanguage() == FN_LANG_GERMAN].x + fndr_mx,
                        fndr_my + FN_TYPE_Y,
                        FN_TYPE_SCALE, FN_TYPE_FIGURES, 0);             /* 123 */
    }
}
