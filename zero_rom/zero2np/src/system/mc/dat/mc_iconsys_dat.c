/* ==========================================================================
 *  system/mc/dat/mc_iconsys_dat.c
 *
 *  icon.sys lighting rig, read straight out of .rodata at 0x3bb570 in the
 *  Feb 6 2004 prototype (SLES_523.84).  mc_iconsys_dat.o has no .text.
 *
 *  Every float here is one ulp below the round decimal it was written as
 *  (0xbe4ccccc rather than 0xbe4ccccd for -0.2), which is EE GCC 2.96
 *  truncating instead of rounding its literals.  The spellings below
 *  reproduce the ROM's words bit for bit; the intended value is in the
 *  comment.
 *
 *  Reconstructed from the Feb 6 2004 prototype (SLES_523.84).
 * ======================================================================== */

#include "mc_iconsys_dat.h"

/* Background gradient, RGBA 0..255 per screen corner.  Three corners black and
 * one dark red, so the icon sits on a single wash out of that corner. */
int mc_bgcolor[4][4] =                          /* rdata 3bb570 */
{
    {   0,   0,   0, 0 },
    {   0,   0,   0, 0 },
    {   0,   0,   0, 0 },
    { 176,  64,  64, 0 }
};

/* Three light directions.  Not normalised -- the browser scales by the colour
 * below, so the short vectors are part of the intensity. */
float mc_lightdir[3][4] =                       /* rdata 3bb5b0 */
{
    { -0.19999999f,  0.5f,         0.5f,         0.0f },  /* -0.2, 0.5, 0.5   */
    {  0.29999998f, -0.39999998f, -0.099999994f, 0.0f },  /*  0.3,-0.4,-0.1   */
    {  0.29999998f, -0.5f,         0.5f,         0.0f }   /*  0.3,-0.5, 0.5   */
};

/* Matching colours: a neutral key, a slightly cool fill, and a dimmer rim. */
float mc_lightcol[3][4] =                       /* rdata 3bb5e0 */
{
    { 0.39999998f, 0.39999998f, 0.39999998f, 0.0f },      /* 0.4,  0.4,  0.4  */
    { 0.25f,       0.29999998f, 0.29999998f, 0.0f },      /* 0.25, 0.3,  0.3  */
    { 0.14999999f, 0.19999999f, 0.19999999f, 0.0f }       /* 0.15, 0.2,  0.2  */
};

float mc_ambient[4] =                           /* rdata 3bb610 */
{
    0.29999998f, 0.29999998f, 0.29999998f, 0.0f           /* 0.3,  0.3,  0.3  */
};
