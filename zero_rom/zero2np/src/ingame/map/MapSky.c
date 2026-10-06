// FILE: /home/zero_rom/zero2np/src/ingame/map/MapSky.c
//
// Outdoor sky.  Three things stack up on screen, all of them 2D GIF packets
// pushed through the PK2D ring rather than through gra3d:
//
//   1. MapSkySetBg()    - a flat fog-coloured sprite from the top of the
//                         screen down to the projected horizon.
//   2. MapSkyDrawTen()  - the "ten" (heaven) layer: a 23x23 grid of quads
//                         transformed by MapSkyBlockPoly() through a
//                         private perspective matrix, scrolling in U over
//                         time so the cloud page drifts.
//   3. MapSkyDraw()     - the horizon strip, a row of sprites tiled across
//                         the screen and scrolled by the camera's yaw delta.
//
// The sky never uses the camera's own view-screen matrix.  MapSkyProc()
// builds MapSkyPers with a fixed 0.774479 rad field of view, so the dome
// keeps a constant apparent size no matter what the game camera is doing;
// only the *rotation* is taken from the camera, and only pitch (X) for the
// horizon point and pitch+yaw for the dome.
//
// Which of the 18 database rows is in force comes from the room number, not
// from any per-room record -- MapSkyGetDatNowArea() is a hand-written switch.
// Rooms outside that switch have no sky and MapSkyProc() does nothing.
//
// Naming note: the ROM inlined seven small statics here (the packet writers,
// the fog-colour picker, the back-face test, the draw-env push, the clip
// mask, the yaw-delta wrap and the horizon transform).  No symbol survives
// for any of them, so the names below are ours; their bodies and source line
// numbers are recovered from the call sites that expand them.
//
// Reconstructed from the Feb 6 2004 prototype (SLES_523.84), MapSky.o
// 0x00111d30..0x001133c7.

#include "MapSky.h"

#include "MapFog.h"                             /* MapFogGetColor       */
#include "MapLoad.h"                            /* MapLoadGetRoomNoNow  */
#include "MapSp.h"                              /* MapSpAraCheck        */

#include "../plyr/unit_ctl.h"                   /* GetTrgtRot           */
#include "../../common/packfile.h"              /* GetFileInPak         */
#include "../../graphics/draw_env.h"            /* DRAW_ENV_5/SetDrawEnv */
#include "../../graphics/graph2d/g2d_draw.h"    /* Q_WORDDATA / PK2D    */
#include "../../graphics/graph2d/tim2.h"        /* MakeTim2Direct       */
#include "../../graphics/graph3d/ctl/fixed_array.h"
#include "../../graphics/graph3d/g3dCamera.h"   /* g3dCalcViewScreenMatrixPerspective */
#include "../../graphics/graph3d/gra3d.h"       /* camera / clip volume */
#include "../../miopan/miopan_profiler.h"
#include "../../miopan/rendering/miopan_renderer.h"
#include "../../sdk/libgraph.h"                 /* GIFtag / GS builders */
#include "../../system/os/eecdvd.h"             /* LoadReqGetAddr       */

#include <libvu0.h>
#include <math.h>
#include <stdio.h>

/* Load-request id of the sky pak (mst.pak's sky slot). */
#define MAPSKY_LOAD_REQ         0x9d

/* Files the pak is walked into: 0..9 are the "ten" pages (five texture/clut
 * pairs), and the last two are the horizon strip.  MapSkyProc() only ever
 * uses pair 1 (index 2/3) and the trailing pair (index 10/11). */
#define MAPSKY_FILE_NUM         12

/* Dome tessellation: 23x23 transformed points -> 22x22 quads. */
#define MAPSKY_GRID_W           23
#define MAPSKY_GRID_H           23
#define MAPSKY_POINT_NUM        (MAPSKY_GRID_W * MAPSKY_GRID_H)   /* 529 */

/* VRAM the sky page and its clut are blasted to every frame. */
#define MAPSKY_TEX_TBP          0x2bc4
#define MAPSKY_CLUT_CBP         0x2bc0
#define MAPSKY_TEX_TBW          4

/* GS XYOFFSET origin.  Screen (0,0) is (0x6c0, 0x720) in whole pixels, i.e.
 * 2048 - 320 and 2048 - 224 for the 640x448 frame -- the same pair every other
 * GS->screen bridge in the tree takes its coordinates off (effect_scr.c,
 * effect_obj.c, effect_ene.c all subtract 1728 / 1824).  It has to be this
 * pair: the projection lands on fCenterX/fCenterY == 2048 (gra3dConst.c) and
 * the frame's half-extents are clip_volumev[0]/[1] == 320 / 224.
 *
 * MAPSKY_OFS_X is also the ROM's own X literal -- MapSkyDraw() builds the
 * horizon strip as (MapSkyX + 0x6c0) * 16 -- so the two uses coincide.  There
 * is no such coincidence on Y, hence MAPSKY_BG_TOP_Y below. */
#define MAPSKY_OFS_X            0x6c0
#define MAPSKY_OFS_Y            0x720

/* The fog band's top edge, in whole GS pixels, exactly as the ROM writes it
 * (0x7100 at MapSkySetBg+0x24).  1808 is 1824 - 16: the band deliberately
 * starts one 16-pixel row above the top of the frame so it cannot leave a gap.
 * It is NOT the screen origin, and using it as one -- which this file did --
 * drops the whole sky 16 pixels below the 3D horizon it is supposed to meet. */
#define MAPSKY_BG_TOP_Y         0x710

/* Guard band the projected points are rejected outside of, in 12.4 GS
 * coordinates: 0x4000..0xc000 is 1024..3072 whole pixels. */
#define MAPSKY_CLIP_MIN         0x4000
#define MAPSKY_CLIP_MAX         0xc000

/* lit4 3ed888..3ed8a0 -- four copies of pi (one per inline expansion of the
 * yaw wrap), then the two thresholds and the sky's own field of view.  The
 * exact bit patterns matter here: pi is 0x40490fd7, a digit short of the
 * correctly-rounded 0x40490fdb, and the two thresholds are likewise the
 * float below the decimal literal they were written as. */
#define MAPSKY_PI               3.1415918f      /* 0x40490fd7 */
#define MAPSKY_ROT_LIMIT        0.099999994f    /* 0x3dcccccc */
#define MAPSKY_TEN_PITCH_MIN    -0.29999998f    /* 0xbe999999 */
#define MAPSKY_FOV              0.7744794f      /* 0x3f464448 */

/* rdata 39f4b0 -- the paddsw operand that steps all four grid indices. */
static const int s_iv1111[4] = { 1, 1, 1, 1 };

/* --------------------------------------------------------------------------
 *  data 2cba90 -- the sky database, indexed by MapSkyGetDatNowArea().
 *
 *  sky_stat[1] of every row is overwritten at lookup time from the common
 *  pair below, so the values kept here for that layer are only what the
 *  linker happened to place; sky_stat[0] and [2] are never drawn in this
 *  build.  Read straight out of SLES_523.84.
 * ------------------------------------------------------------------------ */
static MAP_SKY_DB MapSkyDatList[18] =
{
    /*  0 */ { 3100.0f, 1.099999f, 0.0f, -75, 255, 1.9199979f, 37, 55, 51, 75,
               { { -3000.0f, 1.0f, 1.0f, 0.0f, 0, 0, 128, 128, 128, 96 },
                 { -3000.0f, 3.5f, 1.0f, 0.0f, 0, 2029, 121, 122, 106, 120 },
                 { -3000.0f, 1.0f, 1.0f, 0.0f, 0, 0, 128, 128, 128, 96 } } },
    /*  1 */ { 3000.0f, 3.0f, -400.0f, 0, 255, 1.0f, 128, 128, 128, 90,
               { { -3000.0f, 1.0f, 1.0f, 0.0f, 0, 0, 128, 128, 128, 96 },
                 { -3000.0f, 1.0f, 1.0f, 0.0f, 0, 0, 128, 128, 128, 96 },
                 { -3000.0f, 1.0f, 1.0f, 0.0f, 0, 0, 128, 128, 128, 96 } } },
    /*  2 */ { 3000.0f, 0.79999995f, 0.0f, -262, 255, 1.92f, 48, 63, 51, 45,
               { { -3000.0f, 4.309971f, 3.669988f, 0.0f, 0, 1983, 128, 128, 128, 62 },
                 { -3000.0f, 3.5f, 1.0f, 0.0f, 0, 2029, 121, 122, 106, 120 },
                 { -3000.0f, 1.0f, 1.0f, 0.0f, 0, 0, 128, 128, 128, 96 } } },
    /*  3 */ { 3000.0f, 2.1999998f, -400.0f, 0, 255, 2.1999989f, 80, 82, 81, 45,
               { { -3000.0f, 4.819975f, 3.669988f, 0.0f, 0, 1983, 128, 128, 128, 62 },
                 { -3000.0f, 3.099998f, 0.61999995f, 0.0f, 0, 1198, 225, 223, 228, 23 },
                 { -3000.0f, 1.0f, 1.0f, 0.0f, 0, 0, 128, 128, 128, 96 } } },
    /*  4 */ { 3000.0f, 0.69999695f, 0.0f, -154, 255, 1.4399999f, 48, 63, 51, 48,
               { { -3000.0f, 1.0f, 1.0f, 0.0f, 0, 0, 128, 128, 128, 96 },
                 { -3000.0f, 3.5f, 1.0f, 0.0f, 0, 2029, 121, 122, 106, 120 },
                 { -3000.0f, 1.0f, 1.0f, 0.0f, 0, 0, 128, 128, 128, 96 } } },
    /*  5 */ { 3000.0f, 3.0f, -400.0f, 0, 255, 1.0f, 128, 128, 128, 90,
               { { -3000.0f, 1.0f, 1.0f, 0.0f, 0, 0, 128, 128, 128, 96 },
                 { -3000.0f, 1.0f, 1.0f, 0.0f, 0, 0, 128, 128, 128, 96 },
                 { -3000.0f, 1.0f, 1.0f, 0.0f, 0, 0, 128, 128, 128, 96 } } },
    /*  6 */ { 2990.0f, 0.699986f, 0.0f, -139, 161, 2.5199988f, 45, 54, 48, 48,
               { { -3000.0f, 1.0f, 1.0f, 0.0f, 0, 0, 128, 128, 128, 96 },
                 { -3000.0f, 3.5f, 1.0f, 0.0f, 0, 2029, 121, 122, 106, 120 },
                 { -3000.0f, 1.0f, 1.0f, 0.0f, 0, 0, 128, 128, 128, 96 } } },
    /*  7 */ { 3000.0f, 3.0f, -400.0f, 0, 255, 1.0f, 128, 128, 128, 90,
               { { -3000.0f, 1.0f, 1.0f, 0.0f, 0, 0, 128, 128, 128, 96 },
                 { -3000.0f, 1.0f, 1.0f, 0.0f, 0, 0, 128, 128, 128, 96 },
                 { -3000.0f, 1.0f, 1.0f, 0.0f, 0, 0, 128, 128, 128, 96 } } },
    /*  8 */ { 3000.0f, 0.79999995f, 0.0f, -75, 255, 3.3799958f, 45, 57, 54, 86,
               { { -3000.0f, 1.0f, 1.0f, 0.0f, 0, 0, 128, 128, 128, 96 },
                 { -3000.0f, 3.5f, 1.0f, 0.0f, 0, 2029, 121, 122, 106, 120 },
                 { -3000.0f, 1.0f, 1.0f, 0.0f, 0, 0, 128, 128, 128, 96 } } },
    /*  9 */ { 3000.0f, 0.799999f, -124.0f, -129, 255, 1.979998f, 45, 60, 56, 94,
               { { -3000.0f, 1.0f, 1.0f, 0.0f, 0, 0, 128, 128, 128, 96 },
                 { -3000.0f, 1.0f, 1.0f, 0.0f, 0, 0, 128, 128, 128, 96 },
                 { -3000.0f, 1.0f, 1.0f, 0.0f, 0, 0, 128, 128, 128, 96 } } },
    /* 10 */ { 3000.0f, 3.0f, -400.0f, 0, 255, 1.0f, 128, 128, 128, 90,
               { { -3000.0f, 1.0f, 1.0f, 0.0f, 0, 0, 128, 128, 128, 96 },
                 { -3000.0f, 1.0f, 1.0f, 0.0f, 0, 0, 128, 128, 128, 96 },
                 { -3000.0f, 1.0f, 1.0f, 0.0f, 0, 0, 128, 128, 128, 96 } } },
    /* 11 */ { 3000.0f, 1.3999989f, 3452.0f, -144, 255, 1.9999989f, 55, 65, 60, 83,
               { { -3000.0f, 2.559999f, 2.4399989f, 0.0f, 0, 0, 128, 128, 128, 96 },
                 { -2640.0f, 1.499954f, 0.0f, 0.0f, 0, 0, 128, 128, 128, 96 },
                 { -3000.0f, 1.0f, 1.0f, 0.0f, 0, 0, 128, 128, 128, 96 } } },
    /* 12 */ { 3000.0f, 2.1999998f, -400.0f, 0, 255, 2.1999989f, 80, 82, 81, 45,
               { { -3000.0f, 1.0f, 1.0f, 0.0f, 0, 0, 255, 128, 128, 96 },
                 { -3000.0f, 3.109998f, 0.64f, 0.0f, 0, 1948, 225, 223, 226, 53 },
                 { -3000.0f, 1.0f, 1.0f, 0.0f, 0, 0, 128, 128, 128, 96 } } },
    /* 13 */ { 3000.0f, 0.89999896f, -3364.0f, 93, 255, 1.5399989f, 78, 85, 80, 54,
               { { -3000.0f, 3.129998f, 1.0f, 0.0f, 0, 1539, 120, 128, 128, 96 },
                 { -3000.0f, 2.9999979f, 1.0f, 0.0f, 0, 2016, 123, 128, 128, 96 },
                 { -3000.0f, 1.0f, 1.0f, 0.0f, 0, 0, 128, 128, 128, 96 } } },
    /* 14 */ { 3000.0f, 3.1999848f, -400.0f, 0, 255, 1.0f, 128, 128, 128, 90,
               { { -3000.0f, 3.8799968f, 1.0f, 0.0f, 0, 0, 128, 128, 128, 96 },
                 { -3000.0f, 3.759997f, 1.0f, 0.0f, 0, 0, 128, 124, 128, 96 },
                 { -3000.0f, 1.0f, 1.0f, 0.0f, 0, 0, 128, 128, 128, 96 } } },
    /* 15 */ { 3000.0f, 1.8f, -1306.0f, -150, 255, 2.4599988f, 14, 31, 31, 72,
               { { -3000.0f, 1.0f, 1.0f, 0.0f, 0, 0, 128, 128, 128, 96 },
                 { -3000.0f, 3.609998f, 1.0f, 0.0f, 0, 1950, 102, 130, 98, 78 },
                 { -3000.0f, 1.0f, 1.0f, 0.0f, 0, 0, 128, 128, 128, 96 } } },
    /* 16 */ { 3000.0f, 0.799999f, 0.0f, -324, 255, 2.4599988f, 48, 54, 60, 93,
               { { -3000.0f, 1.0f, 1.0f, 0.0f, 0, 0, 128, 128, 128, 96 },
                 { -3000.0f, 3.609998f, 1.0f, 0.0f, 0, 1950, 102, 130, 98, 54 },
                 { -3000.0f, 1.0f, 1.0f, 0.0f, 0, 0, 128, 128, 128, 96 } } },
    /* 17 */ { 3000.0f, 0.7f, 48.0f, -156, 255, 2.4599988f, 50, 50, 50, 128,
               { { -3000.0f, 1.0f, 1.0f, 0.0f, 0, 0, 128, 128, 128, 96 },
                 { -3000.0f, 3.5f, 1.0f, 0.0f, 0, 2029, 121, 122, 106, 90 },
                 { -3000.0f, 1.0f, 1.0f, 0.0f, 0, 0, 128, 128, 128, 96 } } },
};

static MAP_SKY_DB *MapSkyDp;            /* sdata 3ef048 -- row in force     */

/* Base of the sky pak.  4 bytes on the EE; a host pointer here. */
static uintptr_t   MapSkyTopAddr;       /* sdata 3ef04c                     */

static u_long      MapSkyTex0;          /* sdata 3ef050 -- page in VRAM     */
static float       MapSkyX;             /* sdata 3ef058 -- horizon scroll   */
static float       MapSkyRotY;          /* sdata 3ef05c -- last camera yaw  */
static int         MapSkyFlg;           /* sdata 3ef060 -- bit0 = reloading */
static int         MapSkyAlpha = 128;   /* sdata 3ef064                     */
static int         MapSkyFrame;         /* sdata 3ef068 -- dome scroll      */

/* bss 420c90.  `unsigned int` in the ROM; these are file addresses inside the
 * pak, so on the 64-bit host they have to be uintptr_t or GetFileInPak()'s
 * result is truncated. */
static fixed_array<uintptr_t, MAPSKY_FILE_NUM> MapSkyDatAddr;

/* bss 420cc0 -- the sky's own view-screen matrix, rebuilt every frame. */
static float MapSkyPers[4][4];

/* ==========================================================================
 *  Inlined helpers.
 * ======================================================================== */

/* --------------------------------------------------------------------------
 *  MapSkyGetDrawColor (398..405)
 *
 *  Copy a colour triple into the packet's working set, folding it to grey
 *  first when the monotone (black-and-white photo) draw mode is up.
 * ------------------------------------------------------------------------ */
static void MapSkyGetDrawColor(int *out, const int *rc)
{
    int fr = rc[0];                                                     /* 398 */
    int fg = rc[1];
    int fb = rc[2];

    if (gra3dIsMonotoneDrawEnable())                                    /* 400 */
    {
        out[0] = out[1] = out[2] = (fr + fg + fb) / 3;                  /* 401 */
    }
    else
    {
        out[0] = fr;                                                    /* 403 */
        out[1] = fg;                                                    /* 404 */
        out[2] = fb;                                                    /* 405 */
    }
}

/* --------------------------------------------------------------------------
 *  MapSkySetIVec (420..423) / MapSkySetQWord (429..431)
 *
 *  The two qword writers every packet in this file is built out of.
 * ------------------------------------------------------------------------ */
static Q_WORDDATA *MapSkySetIVec(Q_WORDDATA *pbuf, int x, int y, int z, int w)
{
    pbuf->iv[0] = x;                                                    /* 420 */
    pbuf->iv[1] = y;                                                    /* 421 */
    pbuf->iv[2] = z;                                                    /* 422 */
    pbuf->iv[3] = w;                                                    /* 423 */

    return pbuf + 1;
}

static Q_WORDDATA *MapSkySetQWord(Q_WORDDATA *pbuf, u_long lo, u_long hi)
{
    pbuf->ul64[0] = lo;                                                 /* 430 */
    pbuf->ul64[1] = hi;                                                 /* 431 */

    return pbuf + 1;
}

/* --------------------------------------------------------------------------
 *  MapSkyCheckFace (493..498)
 *
 *  Screen-space winding test for one dome cell: the Z of the cross product of
 *  the two leading edges.  Negative means the quad has turned away and
 *  MapSkyBlockPoly() drops it.  The ROM does the two edge subtractions with
 *  psubsw out of g3dxVu0.h (header line 1248); saturation cannot bite here
 *  because the inputs are already inside the 12.4 guard band.
 * ------------------------------------------------------------------------ */
static int MapSkyCheckFace(const int *p0, const int *p1, const int *p2)
{
    int a[4];
    int b[4];
    int i;

    for (i = 0; i < 4; i++)
    {
        a[i] = p1[i] - p0[i];
        b[i] = p2[i] - p1[i];
    }

    return a[0] * b[1] - a[1] * b[0];                                   /* 498 */
}

/* --------------------------------------------------------------------------
 *  MapSkySetSkyDrawEnv (525..533)
 *
 *  The horizon strip's draw env: ALPHA 0x44 (Cs*As + Cd*(1-As)), TEX1 0x161,
 *  CLAMP 0x3fcff0 (WMS/WMT = REPEAT, which is what lets the strip tile),
 *  TEST 0x5000d (alpha >= 0, Z >= dest) and ZBUF 0x0a000118.
 * ------------------------------------------------------------------------ */
static void MapSkySetSkyDrawEnv(void)
{
    DRAW_ENV_5 de;                                                      /* 525 */

    de.alpha = 0x44;
    de.tex1  = 0x161;
    de.clamp = 0x3fcff0;
    de.test  = 0x5000d;
    de.zbuf  = 0x0a000118;

    SetDrawEnv(0, &de);                                                 /* 533 */
}

/* --------------------------------------------------------------------------
 *  MapSkyGetClip (614..627)
 *
 *  Six-bit off-screen mask for the projected horizon point.  MapSkySetBg()
 *  only looks at bits 2/3 (the Y pair) and MapSkyProc() suppresses the
 *  horizon strip entirely unless the mask is clear.
 * ------------------------------------------------------------------------ */
static int MapSkyGetClip(const int *ivec)
{
    int clip = 0;

    if (ivec[0] < MAPSKY_CLIP_MIN)          clip |= 0x01;               /* 622 */
    if (ivec[0] > MAPSKY_CLIP_MAX)          clip |= 0x02;               /* 623 */
    if (ivec[1] < MAPSKY_CLIP_MIN)          clip |= 0x04;               /* 624 */
    if (ivec[1] > MAPSKY_CLIP_MAX)          clip |= 0x08;               /* 625 */
    if ((u_int)ivec[2] < 0xf)               clip |= 0x10;               /* 626 */
    if ((u_int)ivec[2] > 0xffffff)          clip |= 0x20;               /* 627 */

    return clip;
}

/* --------------------------------------------------------------------------
 *  MapSkyGetRotDiff (633..649)
 *
 *  Frame-to-frame yaw delta, wrapped so the horizon strip never jumps a whole
 *  turn.  The +-pi arm handles the seam the camera yaw is wrapped at; the
 *  second arm is the ordinary case, where the delta is expressed in turns
 *  rather than radians and 2.0 is one full revolution.
 * ------------------------------------------------------------------------ */
static float MapSkyGetRotDiff(float now, float old)
{
    float sa = now * old;                                               /* 633 */
    float r2 = old - now;                                               /* 634 */

    if ((sa < 0.0f && r2 <= -MAPSKY_PI) || r2 >= MAPSKY_PI)             /* 637 */
    {
        if (now < 0.0f)                                                 /* 639 */
        {
            while (r2 >= 0.0f)                                          /* 640 */
            {
                r2 -= MAPSKY_PI;
            }
        }
        else
        {
            while (r2 < 0.0f)                                           /* 642 */
            {
                r2 += MAPSKY_PI;
            }
        }
    }
    else if (r2 >= MAPSKY_ROT_LIMIT || r2 <= -1.0f)                     /* 647 */
    {
        if (r2 > 0.0f)                                                  /* 648 */
        {
            r2 -= 2.0f;
        }
        else
        {
            r2 += 2.0f;                                                 /* 649 */
        }
    }

    return r2;                                                          /* 643 */
}

/* ==========================================================================
 *  Database lookup
 * ======================================================================== */

/* --------------------------------------------------------------------------
 *  MapSkyGetDatNowArea (307..361)
 *
 *  The room -> sky row switch.  Rooms 0..12 map straight through; the rest is
 *  a scatter of one-offs, with 53 and 41 folded onto 40 first.  Whichever row
 *  wins gets its drawn layer (sky_stat[1]) replaced with one of the two
 *  common settings, picked by MapSpAraCheck() -- the "ara" (rough/stormy)
 *  variant only differs in alpha, 120 against 90.
 * ------------------------------------------------------------------------ */
static MAP_SKY_DB *MapSkyGetDatNowArea(void)
{
    /* rodata 39f438 */
    MAP_SKY_ST aSkyCmn[2] =                                             /* 309 */
    {
        { -3000.0f, 3.5f, 1.0f, 0.0f, 0, 2029, 121, 122, 106,  90 },
        { -3000.0f, 3.5f, 1.0f, 0.0f, 0, 2029, 121, 122, 106, 120 },
    };
    MAP_SKY_DB *pSkyDat = (MAP_SKY_DB *)0;                              /* 334 */
    int         room_no = MapLoadGetRoomNoNow();                        /* 335 */

    if (room_no == 53)  room_no = 40;                                   /* 338 */
    if (room_no == 41)  room_no = 40;                                   /* 339 */

    if ((u_int)room_no < 13)                                            /* 342 */
    {
        pSkyDat = &MapSkyDatList[room_no];                              /* 343 */
    }

    if (room_no == 40)  pSkyDat = &MapSkyDatList[13];                   /* 345 */
    if (room_no == 44)  pSkyDat = &MapSkyDatList[14];                   /* 346 */
    if (room_no == 65)  pSkyDat = &MapSkyDatList[15];                   /* 347 */
    if (room_no == 48)  pSkyDat = &MapSkyDatList[16];                   /* 348 */

    if (room_no == 30)  pSkyDat = &MapSkyDatList[17];                   /* 350 */
    if (room_no == 29)  pSkyDat = &MapSkyDatList[17];                   /* 351 */
    if (room_no == 38)  pSkyDat = &MapSkyDatList[17];                   /* 352 */

    if (pSkyDat == (MAP_SKY_DB *)0)                                     /* 354 */
    {
        return (MAP_SKY_DB *)0;
    }

    pSkyDat->sky_stat[1] = aSkyCmn[MapSpAraCheck() & 1];                /* 356 */

    return pSkyDat;                                                     /* 357 */
}

/* ==========================================================================
 *  Texture upload
 * ======================================================================== */

/* --------------------------------------------------------------------------
 *  MapSkyGetTex0 (372..386)
 *
 *  Build the sky page's TEX0 from a TIM2 in memory.  TW/TH are log2 of the
 *  picture's dimensions, counted the ROM's way: shift until the low bit is
 *  set.  Everything else is fixed -- PSMT8 at 0x2bc4 with its clut at 0x2bc0.
 * ------------------------------------------------------------------------ */
u_long MapSkyGetTex0(uintptr_t top_addr)
{
    TIM2_PICTUREHEADER *tph;
    u_int               li;
    int                 cx;
    int                 cy;

    /* FormatId 0 puts the picture header straight after the 0x10-byte file
     * header; format 1 aligns it to 0x80.  Anything else is not a TIM2. */
    tph = (TIM2_PICTUREHEADER *)(top_addr + 0x10);                      /* 372 */

    if (((TIM2_FILEHEADER *)top_addr)->FormatId != 0)
    {
        tph = (TIM2_PICTUREHEADER *)(top_addr + 0x80);

        if (((TIM2_FILEHEADER *)top_addr)->FormatId != 1)
        {
            tph = (TIM2_PICTUREHEADER *)0;
        }
    }

    cx = 0;                                                             /* 379 */
    for (li = tph->ImageWidth; ((li ^ 1) & 1) != 0; li >>= 1)
    {
        cx++;
    }

    cy = 0;                                                             /* 382 */
    for (li = tph->ImageHeight; ((li ^ 1) & 1) != 0; li >>= 1)
    {
        cy++;
    }

    return SCE_GS_SET_TEX0(MAPSKY_TEX_TBP, MAPSKY_TEX_TBW, SCE_GS_PSMT8,
                           cx, cy, 1, 0,
                           MAPSKY_CLUT_CBP, 0, 0, 0, 1);                /* 386 */
}

/* --------------------------------------------------------------------------
 *  MapSkySetAlpha (390..392)
 *
 *  MapFog.o scales the horizon strip down with this while a fog event runs.
 * ------------------------------------------------------------------------ */
void MapSkySetAlpha(int iAlpha)
{
    MapSkyAlpha = iAlpha;
}

/* --------------------------------------------------------------------------
 *  MapSkySetTim2Vram (508..518)
 *
 *  Push one texture/clut pair into VRAM.  top_addr[0] is the picture and
 *  top_addr[1] the alternate clut used by the monotone draw mode.
 * ------------------------------------------------------------------------ */
void MapSkySetTim2Vram(uintptr_t *top_addr)
{
    /* Host guard, not in the ROM: a pak short of files leaves the slot null,
     * and where the EE would have read low memory and uploaded rubbish we
     * would take a segfault.  Keep the last page bound instead. */
    if (top_addr[0] == 0)
    {
        return;
    }

    MapSkyTex0 = MapSkyGetTex0(top_addr[0]);                            /* 508 */

    MakeTim2Direct((u_int *)top_addr[0], MAPSKY_TEX_TBP, 0);            /* 511 */

    if (gra3dIsMonotoneDrawEnable())                                    /* 514 */
    {
        MakeClutDirect((u_int *)top_addr[1], MAPSKY_CLUT_CBP, 0);       /* 515 */
    }
    else
    {
        MakeClutDirect((u_int *)top_addr[0], MAPSKY_CLUT_CBP, 0);       /* 518 */
    }
}

/* ==========================================================================
 *  Primitives
 * ======================================================================== */

/* --------------------------------------------------------------------------
 *  MapSkySprite (541..557)
 *
 *  One textured GS sprite: {RGBAQ, UV, XYZF2, UV, XYZF2}.  Six qwords.
 * ------------------------------------------------------------------------ */
static Q_WORDDATA *MapSkySprite(Q_WORDDATA *pbuf, int x1, int y1, int x2, int y2,
                                int u1, int v1, int u2, int v2,
                                int r, int g, int b, int a)
{
    float render_xy[8];
    float render_uv[8];

    /* PC bridge: the same sprite in screen pixels.  Each one covers exactly
     * one whole page (UV 0..0x1000 = 0..256 texels), so the panorama wraps by
     * laying sprites side by side rather than by the sampler -- clamp and
     * repeat are indistinguishable here. */
    render_xy[0] = (float)(x1 / 16 - MAPSKY_OFS_X);
    render_xy[1] = (float)(y1 / 16 - MAPSKY_OFS_Y);
    render_xy[2] = (float)(x2 / 16 - MAPSKY_OFS_X);
    render_xy[3] = render_xy[1];
    render_xy[4] = render_xy[0];
    render_xy[5] = (float)(y2 / 16 - MAPSKY_OFS_Y);
    render_xy[6] = render_xy[2];
    render_xy[7] = render_xy[5];

    render_uv[0] = (float)u1 / 16.0f;
    render_uv[1] = (float)v1 / 16.0f;
    render_uv[2] = (float)u2 / 16.0f;
    render_uv[3] = render_uv[1];
    render_uv[4] = render_uv[0];
    render_uv[5] = (float)v2 / 16.0f;
    render_uv[6] = render_uv[2];
    render_uv[7] = render_uv[5];

    MioPan_RendererDrawSkyQuad((const sceGsTex0 *)&MapSkyTex0, render_xy,
                               render_uv, (unsigned char)r, (unsigned char)g,
                               (unsigned char)b, (unsigned char)a);

    pbuf = MapSkySetQWord(pbuf,
                          SCE_GIF_SET_TAG(1, 1, 1,
                                          SCE_GS_SET_PRIM(SCE_GS_PRIM_SPRITE,
                                                          0, 1, 0, 1, 0, 1, 0, 0),
                                          SCE_GIF_PACKED, 5),
                          (u_long)SCE_GIF_PACKED_RGBAQ
                        | ((u_long)SCE_GIF_PACKED_UV    <<  4)
                        | ((u_long)SCE_GIF_PACKED_XYZF2 <<  8)
                        | ((u_long)SCE_GIF_PACKED_UV    << 12)
                        | ((u_long)SCE_GIF_PACKED_XYZF2 << 16));        /* 545 */

    pbuf = MapSkySetIVec(pbuf, r,  g,  b, a);                           /* 547 */
    pbuf = MapSkySetIVec(pbuf, u1, v1, 0, 0);                           /* 549 */
    pbuf = MapSkySetIVec(pbuf, x1, y1, 0, 0);                           /* 551 */
    pbuf = MapSkySetIVec(pbuf, u2, v2, 0, 0);                           /* 553 */
    pbuf = MapSkySetIVec(pbuf, x2, y2, 0, 0);                           /* 555 */

    return pbuf;                                                        /* 557 */
}

/* --------------------------------------------------------------------------
 *  MapSkySetBg (580..604)
 *
 *  Flood the band above the horizon with the fog colour.  `ey` is the
 *  projected horizon in 12.4 and `eh` the strip height in pixels, so the
 *  band runs from the top of the screen down to the bottom of the strip;
 *  when the horizon has clipped off the Y edge and the camera is pitched up,
 *  it covers the whole screen instead.
 * ------------------------------------------------------------------------ */
static int MapSkySetBg(int ey, int eh, float *rot, int clip)
{
    int        *rc;
    Q_WORDDATA *pbuf;
    int         bg_y = ey + eh * 16;                                    /* 583 */
    int         fc[3];
    float       render_xy[8];
    unsigned char render_rgba[16];
    int         i;
    float       view_x0;
    float       view_y0;
    float       view_x1;

    rc = MapFogGetColor();                                              /* 581 */
    MapSkyGetDrawColor(fc, rc);

    if ((clip & 0xc) != 0)                                              /* 590 */
    {
        if (*rot < 0.0f)
        {
            return 0;                                                   /* 591 */
        }

        bg_y = 0x8f00;                                                  /* 592 */
    }

    for (i = 0; i < 4; i++)
    {
        render_rgba[i * 4 + 0] = (unsigned char)fc[0];
        render_rgba[i * 4 + 1] = (unsigned char)fc[1];
        render_rgba[i * 4 + 2] = (unsigned char)fc[2];
        render_rgba[i * 4 + 3] = 0x80;
    }
    /* PC bridge.  The GS packet below is the ROM's, pinned to the 640x448
     * framebuffer; the host has to cover whatever the window exposes instead,
     * or the band stops at the original frame and leaves the widened 3D view
     * showing through beside it.  Only the top and the sides move -- the
     * bottom edge is the projected horizon and belongs where it is. */
    MioPan_RendererGetViewBounds(&view_x0, &view_y0, &view_x1, NULL);

    render_xy[0] = view_x0;
    render_xy[1] = view_y0;
    render_xy[2] = view_x1;
    render_xy[3] = view_y0;
    render_xy[4] = view_x0;
    render_xy[5] = (float)(bg_y / 16 - MAPSKY_OFS_Y);
    render_xy[6] = view_x1;
    render_xy[7] = render_xy[5];
    MioPan_RendererDrawSolidQuad(render_xy, render_rgba);

    pbuf = GetPK2Dbuf();                                                /* 595 */

    /* Untextured blended sprite: {RGBAQ, XYZF2, XYZF2}. */
    pbuf = MapSkySetQWord(pbuf,
                          SCE_GIF_SET_TAG(1, 1, 1,
                                          SCE_GS_SET_PRIM(SCE_GS_PRIM_SPRITE,
                                                          0, 0, 0, 1, 0, 0, 0, 0),
                                          SCE_GIF_PACKED, 3),
                          (u_long)SCE_GIF_PACKED_RGBAQ
                        | ((u_long)SCE_GIF_PACKED_XYZF2 << 4)
                        | ((u_long)SCE_GIF_PACKED_XYZF2 << 8));         /* 597 */

    pbuf = MapSkySetIVec(pbuf, fc[0], fc[1], fc[2], 0x80);              /* 599 */
    pbuf = MapSkySetIVec(pbuf, MAPSKY_OFS_X * 16, MAPSKY_BG_TOP_Y * 16, 0, 0);
    pbuf = MapSkySetIVec(pbuf, (MAPSKY_OFS_X + 640) * 16, bg_y, 0, 0);

    EndPK2Dbuf(pbuf);                                                   /* 601 */

    return 0;                                                           /* 603 */
}

/* --------------------------------------------------------------------------
 *  MapSkyGetPerspectiveMatrix (657..664)
 *
 *  The sky's own view-screen matrix.  The vertical clip half-extent divided
 *  by tan(fov/2) is the projection distance; every other term comes from the
 *  live camera so the sky lands in the same viewport as the room.
 * ------------------------------------------------------------------------ */
static void MapSkyGetPerspectiveMatrix(float (*mat)[4], const GRA3DCAMERA *pCam,
                                       float fov)
{
    float (*pv)[4] = _GetClipVolumeV();                                 /* 657 */

    g3dCalcViewScreenMatrixPerspective(mat,
                                       (*pv)[1] / tanf(fov * 0.5f),     /* 659 */
                                       pCam->fAspectX, pCam->fAspectY,
                                       pCam->fCenterX, pCam->fCenterY,
                                       pCam->fZmin,    pCam->fZmax,
                                       pCam->fNearZ,   pCam->fFarZ);    /* 664 */
}

/* --------------------------------------------------------------------------
 *  MapSkyGetHorizon (666..685)
 *
 *  Project a point `len` in front of the camera, pitched by the camera's X
 *  rotation only, through the sky matrix.  The Y that comes back is squashed
 *  towards the screen centre by move_y/256 and then nudged by offset_y2, which
 *  is how each area tunes where its horizon sits.
 * ------------------------------------------------------------------------ */
static void MapSkyGetHorizon(int *ivec, const float *rot)
{
    float       mat[4][4];
    float       vec[4];
    MAP_SKY_DB *dp = MapSkyDp;                                          /* 670 */

    vec[0] = 0.0f;                                                      /* 672 */
    vec[1] = 0.0f;
    vec[2] = dp->len;
    vec[3] = 1.0f;

    sceVu0UnitMatrix(mat);                                              /* 675 */
    sceVu0RotMatrixX(mat, mat, -rot[0]);                                /* 676 */

    /* ROM: inlined from g3dxVu0.h (header line 1324).  Row-major mat * Pers,
     * which sceVu0MulMatrix spells with the operands this way round. */
    sceVu0MulMatrix(mat, MapSkyPers, mat);

    sceVu0RotTransPers(ivec, mat, vec, 0);                              /* 679 */

    ivec[1] = ((ivec[1] - 0x8000) * dp->move_y >> 8) + 0x8000;          /* 683 */
    ivec[1] += dp->offset_y2 * 16;                                      /* 684 */
}

/* ==========================================================================
 *  Horizon strip
 * ======================================================================== */

/* --------------------------------------------------------------------------
 *  MapSkyDraw (689..737)
 *
 *  Tile the horizon page across the screen.  MapSkyX is the scroll offset in
 *  pixels, kept inside [-p_scale, 0) so the first tile always starts just off
 *  the left edge; it advances by the wrapped yaw delta scaled by the strip
 *  width and the area's `speed`.
 * ------------------------------------------------------------------------ */
static float MapSkyAdvanceHorizon(float ry, MAP_SKY_DB *ep)
{
    const float p_scale = ep->scale * 255.0f;
    const float r2 = MapSkyGetRotDiff(ry, MapSkyRotY);
    MapSkyRotY = ry;

    MapSkyX += r2 * p_scale * ep->speed;
    while (MapSkyX < -p_scale)
    {
        MapSkyX += p_scale;
    }
    while (MapSkyX >= 0.0f)
    {
        MapSkyX -= p_scale;
    }
    return p_scale;
}

void MapSkyDraw(int (&ivec)[4], float ry, MAP_SKY_DB *ep)
{
    Q_WORDDATA *pbuf;
    float       p_scale;
    u_int       x1;
    u_int       x2;
    u_int       y1;
    u_int       y2;
    int         iPscale;
    int         fc[3];
    uint64_t    horizon_quads = 0;

    if (MapSkyAlpha == 0)                                               /* 696 */
    {
        return;
    }

    p_scale = MapSkyAdvanceHorizon(ry, ep);                              /* 699 */
    iPscale  = (int)p_scale;                                            /* 705 */

    x1 = (u_int)(((int)MapSkyX + MAPSKY_OFS_X) * 16);                   /* 709 */
    x2 = (u_int)(((int)(MapSkyX + p_scale) + MAPSKY_OFS_X) * 16);       /* 710 */
    iPscale *= 16;                                                      /* 712 */
    y1 = (u_int)ivec[1];
    y2 = y1 + iPscale;                                                  /* 713 */

    MapSkySetSkyDrawEnv();

    pbuf = GetPK2Dbuf();                                                /* 716 */

    /* A+D: flush the texture cache, then point TEX0_1 at the strip page. */
    pbuf = MapSkySetQWord(pbuf,
                          SCE_GIF_SET_TAG(1, 1, 0, 0, SCE_GIF_PACKED, 2),
                          (u_long)SCE_GIF_PACKED_AD
                        | ((u_long)SCE_GIF_PACKED_AD << 4)
                        | ((u_long)SCE_GIF_PACKED_AD << 8));            /* 719 */
    pbuf = MapSkySetQWord(pbuf, 0,          SCE_GS_TEXFLUSH);           /* 721 */
    pbuf = MapSkySetQWord(pbuf, MapSkyTex0, SCE_GS_TEX0_1);             /* 723 */

    MapSkyGetDrawColor(fc, &ep->fog_r);

    while (x1 < MAPSKY_CLIP_MAX)                                        /* 727 */
    {
        pbuf = MapSkySprite(pbuf, (int)x1, (int)y1, (int)x2, (int)y2,
                            0, 0, 0x1000, 0x1000,
                            fc[0], fc[1], fc[2],
                            ep->fog_al * MapSkyAlpha >> 8);             /* 730 */
        horizon_quads++;
        x1 = x2;                                                        /* 731 */
        x2 = x1 + iPscale;                                              /* 732 */
    }

    MioPan_ProfilerAddCounter(MIOPAN_PROFILER_COUNTER_SKY_HORIZON_QUADS,
                              horizon_quads);
    EndPK2Dbuf(pbuf);                                                   /* 736 */
}

/* ==========================================================================
 *  Sky dome
 * ======================================================================== */

/* --------------------------------------------------------------------------
 *  MapSkyBlockPoly (745..831)
 *
 *  Transform a wn x hn grid spanning `vec` and emit a triangle fan per cell.
 *  Only vec[0] (the near-left corner), vec[1][0] and vec[3][2] are read: the
 *  quad is axis-aligned in XZ at a single Y, which is why MapSkyDrawTen()
 *  only ever patches tes[0][1].
 *
 *  The transformed points live in a file-static array, so the two passes --
 *  transform-and-cull, then emit -- can walk it by index.
 * ------------------------------------------------------------------------ */
static Q_WORDDATA *MapSkyBlockPoly(Q_WORDDATA *pbuf, float (*mat)[4],
                                   float (*vec)[4], int *u, int *v,
                                   int wn, int hn, int ox, int oy,
                                   int r, int g, int b, int a)
{
    int   iWj = 0;                                                      /* 755 */
    int   i;
    int   j;
    /* bss 41eb80 */
    static int iout[MAPSKY_POINT_NUM][4];
    float fYPos;
    float fXPos;
    float vOut[4];
    int   id[4];
    int   viUV[4];
    /* The ROM keeps these two in compiler temporaries, so no debug record
     * survives for them; they are hoisted out of the emit loop all the same. */
    int   iAddU;
    int   iAddV;
    float render_xy[8];
    float render_uv[8];
    uint64_t points_transformed = 0;
    uint64_t cells_tested = 0;
    uint64_t visible_cells = 0;

    fYPos = (vec[3][2] - vec[0][2]) / (float)hn;                        /* 749 */
    fXPos = (vec[1][0] - vec[0][0]) / (float)wn;                        /* 750 */
    iAddV = ((v[3] - v[0]) * 16) / hn;                                  /* 751 */
    iAddU = ((u[1] - u[0]) * 16) / wn;                                  /* 752 */

    vOut[0] = vec[0][0];                                                /* 753 */
    vOut[1] = vec[0][1];
    vOut[2] = vec[0][2];
    vOut[3] = 1.0f;

    /* One RGBAQ for the whole dome; PRIM asks for gouraud but only this
     * colour is ever supplied, so every vertex gets it. */
    pbuf = MapSkySetQWord(pbuf,
                          SCE_GIF_SET_TAG(1, 1, 0, 0, SCE_GIF_PACKED, 1),
                          SCE_GIF_PACKED_RGBAQ);                        /* 757 */
    pbuf = MapSkySetIVec(pbuf, r, g, b, a);                             /* 759 */

    for (j = 0; j < hn; j++)                                            /* 763 */
    {
        vOut[0] = vec[0][0];                                            /* 764 */

        for (i = 0; i < wn; i++)                                        /* 767 */
        {
            sceVu0RotTransPers(iout[iWj], mat, vOut, 0);                /* 769 */
            points_transformed++;
            vOut[0] += fXPos;                                           /* 770 */

            if ((u_int)iout[iWj][0] > 60000 ||                          /* 773 */
                (u_int)iout[iWj][1] > 45000)                            /* 774 */
            {
                iout[iWj][0] = -1;                                      /* 775 */
            }
            else
            {
                iout[iWj][2] = 0;                                       /* 776 */
                /* XYZF2's fourth word carries F in bits 4..11: nearer
                 * points fog less.  The host renderer has no GS fog, so
                 * this only reaches the packet. */
                iout[iWj][3] = 4000 - (iout[iWj][3] >> 6);              /* 777 */

                if (iout[iWj][3] < 0)                                   /* 778 */
                {
                    iout[iWj][3] = 0;
                }
            }

            iWj++;                                                      /* 779 */
        }

        vOut[2] += fYPos;                                               /* 780 */
    }

    /* Cell corners, walked as a sliding window over the point grid. */
    id[0] = 0;                                                          /* 783 */
    id[1] = 1;
    id[2] = wn + 1;
    id[3] = wn;

    viUV[0] = 0;                                                        /* 784 */
    viUV[1] = v[0] * 16 + oy;
    viUV[2] = 0;
    viUV[3] = 0;

    for (j = hn - 1; j > 0; j--)                                        /* 787 */
    {
        viUV[0] = u[0] * 16 + ox;                                       /* 788 */

        for (i = wn - 1; i > 0; i--)                                    /* 790 */
        {
            cells_tested++;
            /* A single -1 in any corner poisons the OR and drops the cell. */
            if ((iout[id[0]][0] | iout[id[1]][0] |                      /* 794 */
                 iout[id[2]][0] | iout[id[3]][0]) >= 0 &&
                MapSkyCheckFace(iout[id[0]], iout[id[1]],               /* 493 */
                                iout[id[2]]) >= 0)
            {
                visible_cells++;
                /* Textured gouraud fan with fog: 4 x {UV, XYZF2}. */
                pbuf = MapSkySetQWord(pbuf,
                                      SCE_GIF_SET_TAG(1, 1, 1,
                                                      SCE_GS_SET_PRIM(SCE_GS_PRIM_TRIFAN,
                                                                      1, 1, 1, 1, 0, 1, 0, 0),
                                                      SCE_GIF_PACKED, 8),
                                      (u_long)SCE_GIF_PACKED_UV
                                    | ((u_long)SCE_GIF_PACKED_XYZF2 <<  4)
                                    | ((u_long)SCE_GIF_PACKED_UV    <<  8)
                                    | ((u_long)SCE_GIF_PACKED_XYZF2 << 12)
                                    | ((u_long)SCE_GIF_PACKED_UV    << 16)
                                    | ((u_long)SCE_GIF_PACKED_XYZF2 << 20)
                                    | ((u_long)SCE_GIF_PACKED_UV    << 24)
                                    | ((u_long)SCE_GIF_PACKED_XYZF2 << 28));

                render_uv[0] = (float)viUV[0] / 16.0f;
                render_uv[1] = (float)viUV[1] / 16.0f;
                render_xy[0] = (float)(iout[id[0]][0] / 16 - MAPSKY_OFS_X);
                render_xy[1] = (float)(iout[id[0]][1] / 16 - MAPSKY_OFS_Y);
                pbuf = MapSkySetIVec(pbuf, viUV[0], viUV[1], viUV[2], viUV[3]);
                pbuf = MapSkySetIVec(pbuf, iout[id[0]][0], iout[id[0]][1],
                                     iout[id[0]][2], iout[id[0]][3]);

                viUV[0] += iAddU;                                       /* 812 */

                render_uv[2] = (float)viUV[0] / 16.0f;
                render_uv[3] = render_uv[1];
                render_xy[2] = (float)(iout[id[1]][0] / 16 - MAPSKY_OFS_X);
                render_xy[3] = (float)(iout[id[1]][1] / 16 - MAPSKY_OFS_Y);
                pbuf = MapSkySetIVec(pbuf, viUV[0], viUV[1], viUV[2], viUV[3]);
                pbuf = MapSkySetIVec(pbuf, iout[id[1]][0], iout[id[1]][1],
                                     iout[id[1]][2], iout[id[1]][3]);

                viUV[1] += iAddV;                                       /* 816 */

                render_uv[6] = render_uv[2];
                render_uv[7] = (float)viUV[1] / 16.0f;
                render_xy[6] = (float)(iout[id[2]][0] / 16 - MAPSKY_OFS_X);
                render_xy[7] = (float)(iout[id[2]][1] / 16 - MAPSKY_OFS_Y);
                pbuf = MapSkySetIVec(pbuf, viUV[0], viUV[1], viUV[2], viUV[3]);
                pbuf = MapSkySetIVec(pbuf, iout[id[2]][0], iout[id[2]][1],
                                     iout[id[2]][2], iout[id[2]][3]);

                viUV[0] -= iAddU;                                       /* 820 */

                render_uv[4] = render_uv[0];
                render_uv[5] = render_uv[7];
                render_xy[4] = (float)(iout[id[3]][0] / 16 - MAPSKY_OFS_X);
                render_xy[5] = (float)(iout[id[3]][1] / 16 - MAPSKY_OFS_Y);
                pbuf = MapSkySetIVec(pbuf, viUV[0], viUV[1], viUV[2], viUV[3]);
                pbuf = MapSkySetIVec(pbuf, iout[id[3]][0], iout[id[3]][1],
                                     iout[id[3]][2], iout[id[3]][3]);

                viUV[1] -= iAddV;                                       /* 824 */

                /* PC bridge.  The fan is TL, TR, BR, BL; the host quad
                 * wants TL, TR, BL, BR, which is why the third corner was
                 * written into slots 6/7 above and the fourth into 4/5.
                 *
                 * Clamped, not wrapped, even though the ROM's CLAMP register
                 * says REPEAT.  The dome page is a single soft decal whose
                 * alpha is 0 along all four edges, so it cannot tile without
                 * a hard seam at every boundary; the U sweep of scale_min*255
                 * texels only exceeds one page well past the visible band,
                 * and clamping to a transparent border is what makes the
                 * cloud fade out instead of repeating. */
                MioPan_RendererDrawSkyQuad((const sceGsTex0 *)&MapSkyTex0,
                                           render_xy, render_uv,
                                           (unsigned char)r, (unsigned char)g,
                                           (unsigned char)b, (unsigned char)a);
            }

            viUV[0] += iAddU;                                           /* 826 */

            id[0] += s_iv1111[0];
            id[1] += s_iv1111[1];
            id[2] += s_iv1111[2];
            id[3] += s_iv1111[3];
        }

        viUV[1] += iAddV;                                               /* 827 */

        id[0] += s_iv1111[0];
        id[1] += s_iv1111[1];
        id[2] += s_iv1111[2];
        id[3] += s_iv1111[3];
    }

    MioPan_ProfilerAddCounter(MIOPAN_PROFILER_COUNTER_SKY_POINTS_TRANSFORMED,
                              points_transformed);
    MioPan_ProfilerAddCounter(MIOPAN_PROFILER_COUNTER_SKY_CELLS_TESTED,
                              cells_tested);
    MioPan_ProfilerAddCounter(MIOPAN_PROFILER_COUNTER_SKY_VISIBLE_CELLS,
                              visible_cells);
    return pbuf;                                                        /* 831 */
}

/* --------------------------------------------------------------------------
 *  MapSkyDrawTen (837..894)
 *
 *  Draw the dome layer.  `move` is the U scroll for this field, derived from
 *  MapSkyFrame stepping once per field and wrapping at `frame`; the "ara"
 *  weather shortens the period to 100/170 of it.  Everything is dropped when
 *  the camera pitches far enough down that the dome is behind the viewer.
 * ------------------------------------------------------------------------ */
static int MapSkyAdvanceDomeFrame(const MAP_SKY_ST *skp)
{
    int move = 0;
    if (skp->frame > 0)
    {
        const int master_frame = MapSpAraCheck()
            ? (skp->frame * 100) / 170 : skp->frame;
        move = (MapSkyFrame << 12) / master_frame & 0xfff;
        if (++MapSkyFrame >= master_frame)
        {
            MapSkyFrame -= master_frame;
        }
    }
    return move;
}

static void MapSkyDrawTen(MAP_SKY_ST *skp, float *rot)
{
    Q_WORDDATA *pbuf;
    float       mat[4][4];
    /* data 2cc5d0 -- the dome quad, 48000 units square in XZ.  Only [0][1]
     * is patched, because MapSkyBlockPoly() takes its Y from that corner. */
    static float tes[4][4] =
    {
        { -24000.0f, 0.0f, -24000.0f, 1.0f },
        {  24000.0f, 0.0f, -24000.0f, 1.0f },
        {  24000.0f, 0.0f,  24000.0f, 1.0f },
        { -24000.0f, 0.0f,  24000.0f, 1.0f },
    };
    int move;
    int iWork;
    int tu[4];
    int tv[4];
    int fc[3];
    move = MapSkyAdvanceDomeFrame(skp);                                  /* 848 */

    if (*rot < MAPSKY_TEN_PITCH_MIN)                                    /* 866 */
    {
        return;
    }

    iWork = (int)(skp->scale_min * 255.0f);                             /* 869 */

    tu[0] = 0;      tu[1] = iWork;  tu[2] = iWork;  tu[3] = 0;          /* 870 */
    tv[0] = 0;      tv[1] = 0;      tv[2] = iWork;  tv[3] = iWork;      /* 871 */

    tes[0][1] = skp->hight;                                             /* 872 */

    sceVu0UnitMatrix(mat);                                              /* 875 */
    sceVu0RotMatrixY(mat, mat, -rot[1]);                                /* 876 */
    sceVu0RotMatrixX(mat, mat, -rot[0]);                                /* 877 */

    /* ROM: inlined from g3dxVu0.h (header line 1324). */
    sceVu0MulMatrix(mat, MapSkyPers, mat);

    pbuf = GetPK2Dbuf();                                                /* 880 */

    pbuf = MapSkySetQWord(pbuf,
                          SCE_GIF_SET_TAG(2, 1, 0, 0, SCE_GIF_PACKED, 1),
                          SCE_GIF_PACKED_AD);                           /* 882 */
    pbuf = MapSkySetQWord(pbuf, 0,          SCE_GS_TEXFLUSH);           /* 884 */
    pbuf = MapSkySetQWord(pbuf, MapSkyTex0, SCE_GS_TEX0_1);             /* 886 */

    MapSkyGetDrawColor(fc, &skp->fr);

    pbuf = MapSkyBlockPoly(pbuf, mat, tes, tu, tv,
                           MAPSKY_GRID_W, MAPSKY_GRID_H, move, 0,
                           fc[0], fc[1], fc[2], skp->fa);               /* 892 */

    EndPK2Dbuf(pbuf);                                                   /* 893 */
}

/* ==========================================================================
 *  Entry points
 * ======================================================================== */

/* --------------------------------------------------------------------------
 *  MapSkyRegist (903..922)
 *
 *  Resolve the sky pak's files once the load has landed.  The first ten
 *  entries are the dome pages, taken two at a time (picture + monotone clut)
 *  and stopping early if the pak holds fewer; the last two are always the
 *  horizon strip, so they are fetched from the end regardless.  Clearing bit
 *  0 of MapSkyFlg is what lets MapSkyProc() start drawing.
 * ------------------------------------------------------------------------ */
void MapSkyRegist(void)
{
    int i;
    int sora_max = *(int *)MapSkyTopAddr - 2;                           /* 905 */

    for (i = 0; i < MAPSKY_FILE_NUM; i++)                               /* 908 */
    {
        MapSkyDatAddr[i] = 0;                                           /* 910 */
    }

    if (sora_max > 0)                                                   /* 912 */
    {
        for (i = 0; i < 10; i += 2)                                     /* 913 */
        {
            MapSkyDatAddr[i]     = (uintptr_t)GetFileInPak((void *)MapSkyTopAddr, i);
            MapSkyDatAddr[i + 1] = (uintptr_t)GetFileInPak((void *)MapSkyTopAddr, i + 1);

            if (i + 2 >= sora_max)                                      /* 916 */
            {
                break;
            }
        }
    }

    MapSkyDatAddr[10] = (uintptr_t)GetFileInPak((void *)MapSkyTopAddr, sora_max);
    MapSkyDatAddr[11] = (uintptr_t)GetFileInPak((void *)MapSkyTopAddr, sora_max + 1);

    MapSkyFlg &= ~1;                                                    /* 922 */
}

/* --------------------------------------------------------------------------
 *  MapSkyProc (927..970)
 *
 *  Once per frame from MhCtlDraw(), ahead of the room, so the three layers
 *  land behind everything else.
 * ------------------------------------------------------------------------ */
void MapSkyProc(void)
{
    MioPanProfileScope profile(MIOPAN_PROFILE_SKY_CPU);
    int          ivec[4];
    GRA3DCAMERA *pCam;
    float        c_rot[4];
    float       *p0;
    int          clip;
    unsigned int debug_flags;

    if ((MapSkyFlg & 1) != 0 ||                                         /* 932 */
        (MapSkyDp = MapSkyGetDatNowArea()) == (MAP_SKY_DB *)0)          /* 934 */
    {
        return;
    }

    pCam = gra3dGetCamera();                                            /* 936 */
    p0   = gra3dcamGetPosition();                                       /* 937 */

    GetTrgtRot(p0, pCam->vTarget, c_rot, 3);                            /* 941 */
    MapSkyGetPerspectiveMatrix(MapSkyPers, pCam, MAPSKY_FOV);           /* 943 */

    MapSkyGetHorizon(ivec, c_rot);
    clip = MapSkyGetClip(ivec);

    MapSkySetBg(ivec[1], (int)(MapSkyDp->scale * 255.0f), c_rot, clip); /* 952 */

    debug_flags = MioPan_RendererGetDebugViewFlags();
    if ((debug_flags & MIOPAN_RENDERER_DEBUG_DISABLE_SKY_DOME) != 0)
    {
        MapSkyAdvanceDomeFrame(&MapSkyDp->sky_stat[1]);
        MioPan_ProfilerAddCounter(MIOPAN_PROFILER_COUNTER_SKY_DOME_SUPPRESSED,
                                  1);
    }
    else
    {
        MapSkySetTim2Vram(&MapSkyDatAddr[2]);
        MapSkyDrawTen(&MapSkyDp->sky_stat[1], c_rot);                   /* 956 */
    }

    if (clip == 0)                                                      /* 959 */
    {
        if ((debug_flags & MIOPAN_RENDERER_DEBUG_DISABLE_SKY_HORIZON) != 0)
        {
            /* Preserve scrolling and the persistent GS shadow so this A/B
             * switch removes geometry without changing subsequent draws. */
            if (MapSkyAlpha != 0)
            {
                MapSkyAdvanceHorizon(c_rot[1], MapSkyDp);
                MapSkySetSkyDrawEnv();
            }
            MioPan_ProfilerAddCounter(
                MIOPAN_PROFILER_COUNTER_SKY_HORIZON_SUPPRESSED, 1);
        }
        else
        {
            MapSkySetTim2Vram(&MapSkyDatAddr[10]);
            MapSkyDraw(ivec, c_rot[1], MapSkyDp);                       /* 961 */
        }
    }
}

/* --------------------------------------------------------------------------
 *  MapSkyInit (974..981)
 *
 *  Claim the sky pak's slice of the load buffer and hand the cursor on.
 *  Raising bit 0 parks MapSkyProc() until MapSkyRegist() clears it again.
 * ------------------------------------------------------------------------ */
uintptr_t MapSkyInit(uintptr_t mst_addr)
{
    MapSkyTopAddr = mst_addr;                                           /* 975 */
    MapSkyFlg |= 1;                                                     /* 976 */
    MapSkyX    = 0.0f;                                                  /* 977 */
    MapSkyRotY = 0.0f;                                                  /* 978 */

    return LoadReqGetAddr(MAPSKY_LOAD_REQ, mst_addr, (int *)0);         /* 981 */
}
