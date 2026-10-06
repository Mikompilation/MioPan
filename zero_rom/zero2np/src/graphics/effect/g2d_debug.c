// FILE: /home/zero_rom/zero2np/src/graphics/effect/g2d_debug.c
//
// The 2D debug overlays.  What survives into this build is one of them: the
// frame performance meter DrawPerformanceCounter2() draws over the finished
// frame, out of SendDMAMain().
//
// Almost the whole file is gone.  ZERO2.MAP gives .text as 0x680 and the four
// bodies account for it byte for byte (8 + 8 + 8 + 0x668, with no alignment
// fill and no unlisted body), yet the $LM stabs put the first function's
// opening brace at line 1716 and the last one's closing brace at 2187 -- so
// something like 2100 lines of tooling were commented out or #if 0'd before
// this build.  SetShibataSet() opens at 1720 and its `jr ra` carries line
// 1855; CheckHintTex() opens at 1977 and closes at 2107.  That is 134 and 129
// lines respectively compiling to nothing but a return, and none of it is
// recoverable -- the object holds no trace of what was there.
//
// The three survivors of that purge are dead code too: a jal/j scan over the
// loadable segments finds no caller of InitShibataSet, SetShibataSet or
// CheckHintTex anywhere.  So are the four globals -- dither_alp, dither_col,
// hint_test_sw and hint_test_posx are not referenced from any object, and
// sbtset_old is written by InitShibataSet and never read.  They are kept
// because they are real symbols carrying real initialisers, and because the
// two non-zero ones (64 and 128) are the only thing left of whatever dither
// tool used to live here.
//
// The meter itself is a two-bar graph plus three numeric read-outs:
//
//     -152                  -52      48      148                 248
//       +--------------------|--------|-------|--------------------+  y 190
//       |######## now (coloured, y 198..208) ...                   |
//       |#### max (grey,    y 204..214) ...                        |
//       +----------------------------------------------------------+  y 220
//
// with the gauge running 0..200% of a frame across those 400 pixels and the
// brighter grid line at 48 marking 100%.  The bar coordinates are the drawing
// layer's screen-centred space and the text coordinates SetString2()'s
// top-left one, which is why 190..220 and 401..431 describe the same band.
//
// The "now" bar's colour is the load.  Below 100% r and g ramp up together
// while b ramps down, so it runs blue (0,0,255) through grey at half a frame
// to yellow (254,254,1); between 100% and 200% only g falls, taking it from
// yellow to red; past 200% it holds flat red.  perf_max is the peak hold, and
// key_now[14] drops it.
//
// Two things in here are ROM bugs and are reproduced as found; both are in the
// draw_percount block and both are noted at the site.
//
// Reconstructed from the Feb 6 2004 prototype (SLES_523.84), g2d_debug.o.
// All 4 ZERO2.MAP exports plus the one function-local static; .rodata (0x34),
// .lit4 (0xc) and .sdata (0x20) are all read out of the ELF and accounted for.
//
// Trailing /* NNN */ comments are the original source line numbers, measured
// from the $LM stabs.

#include "../graph2d/graph2d.h"

#include "../graphics.h"                    /* SetSquareS / SetLine2          */
#include "../graph2d/message.h"             /* SetString2                     */
#include "../../common/variable.h"          /* debug_var / key_now            */
#include "../../debug/zero2_perf.h"         /* c_zero2_perf_cnt               */
#include "../../sdk/scetypes.h"             /* u_char                         */

/* --------------------------------------------------------------------------
 *  File-scope state.
 *
 *  All five live in .sdata, so every one of them was written with an explicit
 *  initialiser in the source -- GCC only sends a small object to .sbss when it
 *  has none at all.  The ROM's emission order is dither_alp, dither_col,
 *  sbtset_old, perf_max (DrawPerformanceCounter2's static), the "%4d/448"
 *  literal, then hint_test_sw and hint_test_posx; the addresses below are the
 *  authority, since that order does not have to be the declaration order.
 * ------------------------------------------------------------------------ */
int dither_alp     = 64;                                        /* sdata 3f09e0 */
int dither_col     = 128;                                       /* sdata 3f09e4 */
int hint_test_sw   = 0;                                         /* sdata 3f09f8 */
int hint_test_posx = 0;                                         /* sdata 3f09fc */

static int sbtset_old = 0;                                      /* sdata 3f09e8 */

/* ==========================================================================
 *  The three emptied-out tools
 *
 *  InitShibataSet is eight bytes with its only statement in the `jr ra` delay
 *  slot; the other two are eight bytes of `jr ra` + `nop`.  See the file
 *  banner: their bodies were removed at the source level, not by the
 *  optimiser, and nothing calls any of them.
 * ======================================================================== */

void InitShibataSet(void)                                               /* 1716 */
{
    sbtset_old = 0;                                                     /* 1717 */
}

void SetShibataSet(void)                                                /* 1720 */
{
    /* 1721..1854 compile to nothing -- 134 lines, unrecoverable. */
}                                                                       /* 1855 */

void CheckHintTex(void)                                                 /* 1977 */
{
    /* 1978..2106 compile to nothing -- 129 lines, unrecoverable. */
}                                                                       /* 2107 */

/* ==========================================================================
 *  Frame performance meter
 *
 *  Called once a frame from SendDMAMain() with the RCNT1 snapshot taken at
 *  draw start.  c_zero2_perf_cnt.GetPercent() supplies the frame-time figure
 *  as a fraction (1.0 == one whole frame), which is why 200.0f spans the bar
 *  and 448.0f turns it into scanlines.
 * ======================================================================== */

void DrawPerformanceCounter2(int draw_counter)                          /* 2118 */
{
    /* x1..x5 are the five fixed bar abscissae and they really are ints in the
     * ROM: every one is materialised with li/mtc1/cvt.s.w rather than a float
     * immediate, which is what an int variable converted at the call site
     * looks like after GCC propagated its constant into the use.  They are
     * numbered in first-use order, not left to right. */
    float        percount;
    float        draw_percount = 0.0f;                                  /* 2119 */
    static float perf_max      = 0.0f;                          /* sdata 3f09ec */
    u_char       r, g, b;
    int          x1 = -152, x2 = 248, x3 = 48, x4 = -52, x5 = 148;
    int          xx1, xx2;

    if (debug_var.perf_count_sw != 0) {                                 /* 2126 */
        /* key_now[14] -- L3 in the pad mapping -- drops the peak hold. */
        if (*key_now[14] == 1) { perf_max = 0.0f; }                     /* 2130 */

        percount = c_zero2_perf_cnt.GetPercent();                       /* 2140 */
        if (perf_max < percount) { perf_max = percount; }               /* 2142 */

        /* Three separate `percount * 255.0f` expansions, not one CSE'd copy:
         * each conversion carries its own float-to-unsigned fixup branch, so
         * the three sit in different basic blocks and GCC 2.96's local CSE
         * cannot reach across them.  The source wrote it out three times.
         *
         * Note 1.0f against 2.0: the first test compiles to c.lt.s and the
         * second goes through __fptodp/__dpcmp, so the second literal really
         * is a double in the source. */
        if (percount < 1.0f) {                                          /* 2144 */
            r =  (u_char)(percount * 255.0f);                           /* 2145 */
            g =  (u_char)(percount * 255.0f);                           /* 2146 */
            b = ~(u_char)(percount * 255.0f);                           /* 2147 */
        } else if (percount < 2.0) {                                    /* 2148 */
            r = 255;                                                    /* 2149 */
            g = ~(u_char)((percount - 1.0f) * 255.0f);                  /* 2150 */
            b = 0;                                                      /* 2151 */
        } else {                                                        /* 2152 */
            r = 255;                                                    /* 2153 */
            g = 0;                                                      /* 2154 */
            b = 0;                                                      /* 2155 */
        }

        /* The clamps are emitted as slti/movz, i.e. GCC if-converted them and
         * deleted the branch -- and with it whatever line note the statement
         * carried, which is why neither has an $LM of its own. */
        xx1 = x1 + (int)(perf_max * 200.0f);                            /* 2157 */
        if (xx1 > 2047) { xx1 = 2047; }
        xx2 = x1 + (int)(percount * 200.0f);                            /* 2158 */
        if (xx2 > 2047) { xx2 = 2047; }

        /* Peak hold underneath, current frame over it and six pixels higher,
         * so both stay readable when they are the same length. */
        SetSquareS(0x10, x1, 204.0f, xx1, 214.0f, 0x40, 0x40, 0x40, 0x80); /* 2160 */
        SetSquareS(0x10, x1, 198.0f, xx2, 208.0f, r,    g,    b,    0x80); /* 2161 */

        SetLine2(0x10, x1, 190.0f, x2, 190.0f, 0x80, 0x80, 0x80, 0x80); /* 2163 */
        SetLine2(0x10, x1, 190.0f, x1, 220.0f, 0x80, 0x80, 0x80, 0x80); /* 2164 */
        SetLine2(0x10, x2, 190.0f, x2, 220.0f, 0x80, 0x80, 0x80, 0x80); /* 2165 */
        SetLine2(0x10, x1, 220.0f, x2, 220.0f, 0x80, 0x80, 0x80, 0x80); /* 2166 */

        /* 100% first and brighter, then the 50% and 150% marks. */
        SetLine2(0x10, x3, 190.0f, x3, 220.0f, 0x60, 0x60, 0x80, 0x80); /* 2168 */
        SetLine2(0x10, x4, 190.0f, x4, 220.0f, 0x40, 0x40, 0x60, 0x80); /* 2169 */
        SetLine2(0x10, x5, 190.0f, x5, 220.0f, 0x40, 0x40, 0x60, 0x80); /* 2170 */

        /* 0xa5 is the font's decimal point.  The third read-out below uses a
         * plain '.' for the same job -- the ROM's own inconsistency, and the
         * byte is reproduced exactly because it indexes the glyph table. */
        SetString2(0x10, 12.0f, 401.0f, 0, 0x80, 0x80, 0x80,
                   "%4d/448",
                   (int)(percount * 448.0f));                           /* 2172 */
        SetString2(0x10, 12.0f, 416.0f, 0, 0x80, 0x80, 0x80,
                   "Now:%4d\xa5%2d%%",
                   (int)(percount * 100.0f),
                   (int)(percount * 10000.0f) % 100);                   /* 2173 */
        SetString2(0x10, 12.0f, 431.0f, 0, 0x80, 0x80, 0x80,
                   "Max:%4d\xa5%2d%%",
                   (int)(perf_max * 100.0f),
                   (int)(perf_max * 10000.0f) % 100);                   /* 2174 */

        /* ROM BUG, reproduced twice over.  The 80 test is written before the
         * 100 test, so the red arm below is unreachable whatever the input;
         * and the two thresholds are in the unscaled units while the figure
         * printed beside them is draw_percount * 100, so neither would fire
         * at the load they name even in the right order.  Every frame prints
         * white. */
        draw_percount = (float)draw_counter / 20480.0f;                 /* 2178 */
        if (draw_percount > 80.0f) {                                    /* 2179 */
            SetString2(0, 120.0f, 400.0f, 0, 0x50, 0x80, 0x80,
                       "Draw Perf:%4d.%2d%%",
                       (int)(draw_percount * 100.0f),
                       (int)(draw_percount * 10000.0f) % 100);          /* 2180 */
        } else if (draw_percount > 100.0f) {                            /* 2181 */
            SetString2(0, 120.0f, 400.0f, 0, 0x80, 0, 0,
                       "Draw Perf:%4d.%2d%%",
                       (int)(draw_percount * 100.0f),
                       (int)(draw_percount * 10000.0f) % 100);          /* 2182 */
        } else {                                                        /* 2183 */
            SetString2(0, 120.0f, 400.0f, 0, 0x80, 0x80, 0x80,
                       "Draw Perf:%4d.%2d%%",
                       (int)(draw_percount * 100.0f),
                       (int)(draw_percount * 10000.0f) % 100);          /* 2184 */
        }
    }                                                                   /* 2186 */
}                                                                       /* 2187 */
