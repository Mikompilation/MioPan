// FILE: /home/zero_rom/zero2np/src/graphics/graph2d/draw_cmn_num.c
//
// Common number drawing using a sprite digit strip (zero_dat[0..9]).  Draws
// `num` digits of `data` left-to-right, advancing x by the digit width.  Leading
// zeros are suppressed unless zero_flg requests them (or the value is a single
// zero digit).
//
// Reconstructed from the Feb 6 2004 prototype (SLES_523.84).

#include "g2d_draw.h"               // DISP_SPRT / SPRT_DAT / CopySprDToSpr / DispSprD
#include "draw_cmn.h"               // DrawCmnNumberTex prototype

#include "../../common/utility2.h" // PRINT_ASSERT + SetAssertPreMessage / PrintAssertReal

static void DrawCmnNumberTex_One(int data, SPRT_DAT *zero_dat, int x, int y,
                                 u_char alpha, int pri);

// ──────────────────────────────────────────────────────────────────────
// Draw `num` digits of `data` starting at (x,y).

void DrawCmnNumberTex(int data, int num, SPRT_DAT *zero_dat, int x, int y,
                      u_char alpha, int pri, u_char zero_flg)
{
    int    i;
    int    j;
    int    tmp;
    int    ten_tmp;
    int    off_x;
    u_char set_flg;

    set_flg = (zero_flg == '\x01');
    off_x = (u_int)zero_dat->w;

    if (0 < num)
    {
        do
        {
            // ten_tmp = 10^(num-1), the place value of the current digit.
            ten_tmp = 1;
            i = num - 1;
            j = i;
            if (i < 1)
            {
                ten_tmp = 1;
            }
            else
            {
                do
                {
                    j = j - 1;
                    ten_tmp = ten_tmp * 10;
                } while (j != 0);
            }

            // Once a non-zero leading digit is seen, draw the rest.
            if (data / ten_tmp != 0)
            {
                set_flg = 1;
            }
            if (((zero_flg == '\0') && (data == 0)) && (num == 1))
            {
                set_flg = 1;
            }

            if (num == 1)
            {
                tmp = data % 10;
            }
            else
            {
                tmp = (data / ten_tmp) % 10;
            }

            if (set_flg)
            {
                DrawCmnNumberTex_One(tmp, zero_dat, x, y, alpha, pri);
            }

            x = x + (u_int)off_x;
            num = i;
        } while (0 < i);
    }
}

// ──────────────────────────────────────────────────────────────────────
// Draw a single digit (0..9) from the strip.

static void DrawCmnNumberTex_One(int data, SPRT_DAT *zero_dat, int x, int y,
                                 u_char alpha, int pri)
{
    DISP_SPRT num_ds;

    if (9 < data)
    {
        PRINT_ASSERT("Error!! %s", __FUNCTION__);
    }

    CopySprDToSpr(&num_ds, zero_dat + data);
    num_ds.x = (float)x;
    num_ds.y = (float)y;
    num_ds.z = 0xfffff - (pri & 0xfffff);
    num_ds.pri = pri;
    num_ds.alpha = alpha;
    DispSprD(&num_ds);
}
