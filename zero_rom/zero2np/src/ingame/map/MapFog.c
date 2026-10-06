// FILE: /home/zero_rom/zero2np/src/ingame/map/MapFog.c
//
// Map fog.  Three MAP_FOG_HEAD slots drive everything:
//
//   MapFogDat[0]  interpolation start
//   MapFogDat[1]  interpolation end -- the setting being moved towards
//   MapFogDat[2]  the setting in force this frame
//
// Every frame MapFogProc() asks which fog rectangle the *camera* stands in --
// not the player; the `pos` parameter is accepted and never read -- and then
// either snaps MapFogDat[2] to that region's setting or arms a 16-frame walk
// towards it.  It snaps when the camera jumped (701 units or more since last
// frame, i.e. a cut) or when finder mode was just entered or left, and walks
// otherwise.  MapFogAnim() does one step of the walk and pushes the result
// into the GS through gra3dSetFog()/gra3dSetFogColor()/gra3dApplyFog().
//
// Two rectangle types carry fog.  Type 13 (MDAT_FOG_SW) is tested first and
// wins where the two overlap; it hides the sky, and type 5 (MDAT_FOG) shows
// it.  Crossing between the two cross-fades MapSky's alpha across the same
// 16 frames -- but only while finder mode is up: both MapFogAnim() and
// MapFogSetNowDat() force the sky back to full opacity whenever
// MAPFOG_F_FINDER is clear, so out of the finder the fade never lands.
//
// Each region carries *two* settings, MDAT_FOG::dat[0] and dat[1].  dat[1] is
// the one picked while plyr_wrk.cmn_wrk.mode is 6, i.e. while the player is
// looking through the camera obscura.
//
// Naming note: the ROM inlined eight small statics here -- the vector copy,
// the finder-mode flag update, the two setters, the struct copy, the gra3d
// push, the region-to-slot fill and the camera-position fetch.  No symbol
// survives for any of them, so the names below are ours; their bodies and
// line numbers are recovered from the call sites that expand them, and every
// recovered range (133..208, 413..453, 569..572) falls in a gap between the
// file's real functions, which is what identifies them as file-local inlines
// rather than lines borrowed from a header.
//
// Reconstructed from the Feb 6 2004 prototype (SLES_523.84), MapFog.o
// 0x001081b0..0x00108b83.

#include "MapFog.h"

#include "MapSky.h"                             /* MapSkySetAlpha       */
#include "RegDat.h"                             /* MDAT_FOG / rectangles */
#include "map_rectangle.h"                      /* MrecIsInRectangle    */

#include "../plyr/player.h"                     /* PlyrOutsideCheck     */
#include "../../common/utility.h"               /* GetDistV             */
#include "../../common/variable.h"              /* plyr_wrk             */
#include "../../graphics/graph3d/gra3d.h"       /* camera / fog         */

#include <libvu0.h>

/* MapFogFlg bits. */
#define MAPFOG_F_ANIM       0x01    /* interpolation running                  */
#define MAPFOG_F_HOLD       0x02    /* see MapFogProc(): only ever cleared    */
#define MAPFOG_F_FINDER     0x04    /* player is in finder mode -> dat[1]     */
#define MAPFOG_F_EVENT      0x08    /* scripted override up, regions ignored  */
#define MAPFOG_F_CHANGE     0x10    /* finder mode flipped this frame -> snap */
#define MAPFOG_F_SKY        0x20    /* camera is in a type-5 region           */
#define MAPFOG_F_FOG        0x40    /* camera is in a type-13 region          */
#define MAPFOG_F_NOFADE     0x80    /* region class unchanged, no cross-fade  */

/* Rectangle types the fog regions are registered under.  13 is tested first
 * and overrides 5 where they overlap. */
#define MAPFOG_REG_TYPE_FOG_SW  13
#define MAPFOG_REG_TYPE_FOG     5

/* Every interpolation in this file is 16 frames long. */
#define MAPFOG_ANIM_FRAME       16

/* A camera move of at least this much in one frame is a cut, not a walk, so
 * the new region's fog is snapped in rather than faded to. */
#define MAPFOG_CUT_DIST         701

/* data 2c8c88 -- [0] is the interpolation start, [1] the end and [2] the
 * value in force this frame.  All three carry the same initialiser, which is
 * what the very first room sees before any region is sampled. */
static MAP_FOG_HEAD MapFogDat[3] =
{
    { 40, 40, 40, 2000, 18550, 50, 200 },
    { 40, 40, 40, 2000, 18550, 50, 200 },
    { 40, 40, 40, 2000, 18550, 50, 200 },
};

/* sdata 3eeda0 */ static int MapFogFrame;      /* interpolation length      */
/* sdata 3eeda4 */ static int MapFogNowFrame;   /* frames done so far        */
/* sdata 3eeda8 */ static int MapFogFlg;
/* sdata 3eedac */ static float (*MapFogRect)[4]; /* rect the fog came from  */
/* sdata 3eedb0 */ static int MapFogLastRoom;

/* bss 400af0 -- the camera position from the previous frame, used only to
 * measure how far the camera moved. */
static float MapFogBfoCam[4];

static MDAT_FOG *MapFogGetRegStat(int buff_id, MB_OUT_RECT *dat);
static MB_OUT_RECT *MapFogGetRegDat(int buff_id, float *pos, int type);
static MDAT_FOG *MapFogUpdate(int buff_id, float *pos);
static MDAT_FOG *MapFogGetNewDat(int room_no, int floor, float *pos);
static void MapFogAnimNowDat(int room_no, int floor, float *pos);
static void MapFogSetNowDat(int room_no, int floor, float *pos);

/* --------------------------------------------------------------------------
 *  MapFogCopyVec (133..136)   [inlined -- name recovered, not from the ROM]
 * ------------------------------------------------------------------------ */
static void MapFogCopyVec(float *dst, const float *src)
{                                                                       /* 134 */
    sceVu0CopyVector(dst, (float *)src);                                /* 135 */
}

/* --------------------------------------------------------------------------
 *  MapFogChkFinderMode (155..162)   [inlined]
 *
 *  Track finder mode in MAPFOG_F_FINDER and raise MAPFOG_F_CHANGE on the
 *  frame it flips, so MapFogProc() snaps instead of fading: raising the
 *  camera obscura swaps the whole room to its dat[1] setting at once.
 * ------------------------------------------------------------------------ */
static void MapFogChkFinderMode(void)
{                                                                       /* 155 */
    int w;

    w = (plyr_wrk.cmn_wrk.mode == 6) ? MAPFOG_F_FINDER : 0;             /* 156 */

    if ((MapFogFlg ^ w) & MAPFOG_F_FINDER)                              /* 158 */
    {
        MapFogFlg |= MAPFOG_F_CHANGE;                                   /* 159 */
    }

    MapFogFlg = (MapFogFlg & ~MAPFOG_F_FINDER) | w;                     /* 161 */
}

/* --------------------------------------------------------------------------
 *  MapFogGetColor (179)
 * ------------------------------------------------------------------------ */

/* The whole colour triple is read through this: callers index rc[0..2]. */
int *MapFogGetColor(void)
{                                                                       /* 179 */
    return &MapFogDat[2].r;
}

/* --------------------------------------------------------------------------
 *  MapFogSetDat (186..195)   [inlined]
 * ------------------------------------------------------------------------ */
static void MapFogSetDat(MAP_FOG_HEAD *dat, int r, int g, int b,
                         int st, int en, int min, int max)
{                                                                       /* 187 */
    dat->r    = r;                                                      /* 188 */
    dat->g    = g;                                                      /* 189 */
    dat->b    = b;                                                      /* 190 */
    dat->near = st;                                                     /* 191 */
    dat->far  = en;                                                     /* 192 */
    dat->min  = min;                                                    /* 193 */
    dat->max  = max;                                                    /* 194 */
}

/* --------------------------------------------------------------------------
 *  MapFogCopyDat (198..200)   [inlined]
 * ------------------------------------------------------------------------ */
static void MapFogCopyDat(MAP_FOG_HEAD *dst, const MAP_FOG_HEAD *src)
{
    *dst = *src;                                                        /* 199 */
}

/* --------------------------------------------------------------------------
 *  MapFogSetFog (202..208)   [inlined]
 *
 *  Push one setting at the GS.  The colour is truncated to a byte on the way
 *  out -- the ROM loads r/g/b with lbu, so a setting above 255 wraps rather
 *  than clamping.
 * ------------------------------------------------------------------------ */
static void MapFogSetFog(MAP_FOG_HEAD *mp)
{                                                                       /* 203 */
    gra3dSetFog((float)mp->min, (float)mp->max,
                (float)mp->near, (float)mp->far);                       /* 204 */
    gra3dSetFogColor((u_char)mp->r, (u_char)mp->g, (u_char)mp->b);

    gra3dApplyFog();                                                    /* 207 */
}

/* --------------------------------------------------------------------------
 *  MapFogAnimStart (217..221)
 * ------------------------------------------------------------------------ */
void MapFogAnimStart(int frame)
{
    MapFogFrame    = frame;                                             /* 218 */
    MapFogNowFrame = 0;                                                 /* 219 */
    MapFogFlg     |= MAPFOG_F_ANIM;                                     /* 220 */

    MapFogCopyDat(&MapFogDat[0], &MapFogDat[2]);                        /* 221 */
}

/* --------------------------------------------------------------------------
 *  MapFogAnim (227..282)
 *
 *  One step of the walk from MapFogDat[0] to MapFogDat[1].  All seven ints
 *  of MAP_FOG_HEAD are interpolated as a block, colour and ramp alike, which
 *  is why the member order in the header matters.
 *
 *  The terminal case ignores `now` and `en` and copies MapFogDat[1] into
 *  MapFogDat[2] by name -- that is the ROM's code, not a simplification.
 * ------------------------------------------------------------------------ */
void MapFogAnim(MAP_FOG_HEAD *now, MAP_FOG_HEAD *st, MAP_FOG_HEAD *en)
{                                                                       /* 227 */
    int    i;
    int   *w_now;
    int   *w_st;
    int   *w_en;
    float  wf;

    if (MapFogFrame <= MapFogNowFrame + 1)                              /* 237 */
    {
        MapFogCopyDat(&MapFogDat[2], &MapFogDat[1]);
        MapFogFlg &= ~MAPFOG_F_ANIM;                                    /* 240 */

        if (MapFogFlg & MAPFOG_F_FOG)                                   /* 243 */
        {
            MapSkySetAlpha(0);                                          /* 244 */
        }
        else if (MapFogFlg & MAPFOG_F_SKY)                              /* 246 */
        {
            MapSkySetAlpha(0xFF);                                       /* 247 */
        }

        /* Out of finder mode the sky is always fully opaque, so the fade
         * above only ever survives while the camera obscura is raised. */
        if ((MapFogFlg & MAPFOG_F_FINDER) == 0)                         /* 250 */
        {
            MapSkySetAlpha(0xFF);                                       /* 251 */
        }
    }
    else
    {
        wf    = (float)MapFogNowFrame / (float)MapFogFrame;             /* 256 */
        w_now = &now->r;
        w_st  = &st->r;
        w_en  = &en->r;

        for (i = 0; i < 7; i++)                                         /* 257 */
        {
            *w_now = (int)((float)(*w_en - *w_st) * wf + (float)*w_st); /* 258 */

            w_now++;                                                    /* 260 */
            w_st++;                                                     /* 261 */
            w_en++;                                                     /* 262 */
        }

        if ((MapFogFlg & MAPFOG_F_NOFADE) == 0)                         /* 266 */
        {
            if (MapFogFlg & MAPFOG_F_FOG)                               /* 268 */
            {
                MapSkySetAlpha(0x100 -
                               (MapFogNowFrame << 8) / MapFogFrame);    /* 269 */
            }
            else if (MapFogFlg & MAPFOG_F_SKY)                          /* 270 */
            {
                MapSkySetAlpha((MapFogNowFrame << 8) / MapFogFrame);    /* 272 */
            }
        }

        if ((MapFogFlg & MAPFOG_F_FINDER) == 0)                         /* 277 */
        {
            MapSkySetAlpha(0xFF);                                       /* 278 */
        }

        MapFogNowFrame++;                                               /* 281 */
    }
}                                                                       /* 282 */

/* --------------------------------------------------------------------------
 *  MapFogGetRegStat (287..292)
 * ------------------------------------------------------------------------ */
static MDAT_FOG *MapFogGetRegStat(int buff_id, MB_OUT_RECT *dat)
{
    if (dat != NULL)                                                    /* 289 */
    {
        return (MDAT_FOG *)RegDatGetStPtr(buff_id, dat->reg_id);        /* 290 */
    }

    return NULL;                                                        /* 291 */
}

/* --------------------------------------------------------------------------
 *  MapFogGetRegDat (294..313)
 *
 *  First rectangle of `type` in `buff_id` that contains `pos`.
 * ------------------------------------------------------------------------ */
static MB_OUT_RECT *MapFogGetRegDat(int buff_id, float *pos, int type)
{                                                                       /* 296 */
    int          i;
    int          reg_num;
    MB_OUT_RECT *region_p;

    region_p = RegDatGetVecPtr(buff_id, type);                          /* 300 */
    reg_num  = RegDatGetVecNum(buff_id, type);                          /* 301 */

    for (i = 0; i < reg_num; i++)                                       /* 307 */
    {
        if (MrecIsInRectangle(pos, region_p->vec))                      /* 308 */
        {
            return region_p;                                            /* 310 */
        }

        region_p++;
    }

    return NULL;                                                        /* 312 */
}

/* --------------------------------------------------------------------------
 *  MapFogUpdate (317..361)
 *
 *  Resolve `pos` against one registration buffer.  A type-13 rectangle wins
 *  over a type-5 one; MAPFOG_F_NOFADE is set when the class did not change,
 *  which is what stops MapFogAnim() from cross-fading the sky for a move
 *  between two regions of the same kind.
 *
 *  Note that when neither type matches, MapFogFlg is left completely alone --
 *  the caller keeps whatever region class it was already in.
 * ------------------------------------------------------------------------ */
static MDAT_FOG *MapFogUpdate(int buff_id, float *pos)
{                                                                       /* 317 */
    MB_OUT_RECT *rp;
    MDAT_FOG    *fop;

    rp = MapFogGetRegDat(buff_id, pos, MAPFOG_REG_TYPE_FOG_SW);         /* 322 */
    if (rp != NULL)
    {
        if (MapFogFlg & MAPFOG_F_SKY)                                   /* 326 */
        {
            MapFogFlg &= ~(MAPFOG_F_SKY | MAPFOG_F_NOFADE);             /* 327 */
            MapFogFlg |= MAPFOG_F_FOG;                                  /* 328 */
        }
        else
        {
            MapFogFlg |= MAPFOG_F_FOG | MAPFOG_F_NOFADE;                /* 330 */
        }
    }
    else
    {
        rp = MapFogGetRegDat(buff_id, pos, MAPFOG_REG_TYPE_FOG);        /* 335 */
        if (rp == NULL)
        {
            return NULL;
        }

        if (MapFogFlg & MAPFOG_F_FOG)                                   /* 337 */
        {
            MapFogFlg &= ~(MAPFOG_F_FOG | MAPFOG_F_NOFADE);             /* 338 */
            MapFogFlg |= MAPFOG_F_SKY;                                  /* 339 */
        }
        else
        {
            MapFogFlg |= MAPFOG_F_SKY | MAPFOG_F_NOFADE;                /* 342 */
        }
    }

    fop = MapFogGetRegStat(buff_id, rp);                                /* 350 */
    if (fop != NULL)
    {
        MapFogRect = rp->vec;                                           /* 353 */
    }

    return fop;                                                         /* 361 */
}

/* --------------------------------------------------------------------------
 *  MapFogStartFogEv (367..372)
 * ------------------------------------------------------------------------ */
void MapFogStartFogEv(int r, int g, int b, int st, int en, int min, int max)
{                                                                       /* 367 */
    MapFogAnimStart(MAPFOG_ANIM_FRAME);                                 /* 369 */
    MapFogSetDat(&MapFogDat[1], r, g, b, st, en, min, max);             /* 370 */

    MapFogFlg |= MAPFOG_F_EVENT;                                        /* 371 */
}

/* --------------------------------------------------------------------------
 *  MapFogEndFogEv (376..379)
 *
 *  `frame` is accepted and never read -- dropping the event flag simply lets
 *  the next MapFogProc() sample the map again, and that resample arms its own
 *  16-frame walk.  player.c passes 30 regardless.
 * ------------------------------------------------------------------------ */
void MapFogEndFogEv(int frame)
{
    MapFogRect = NULL;                                                  /* 377 */
    MapFogFlg &= ~MAPFOG_F_EVENT;                                       /* 378 */
}

/* --------------------------------------------------------------------------
 *  MapFogSetFogDat (413..418)   [inlined]
 *
 *  Copy one region's setting into `dat`, picking dat[1] in finder mode.
 * ------------------------------------------------------------------------ */
static void MapFogSetFogDat(MAP_FOG_HEAD *dat, MDAT_FOG *fop)
{                                                                       /* 414 */
    int i;

    i = (MapFogFlg >> 2) & 1;                                           /* 416 */

    MapFogSetDat(dat,
                 fop->dat[i].Color[0], fop->dat[i].Color[1],
                 fop->dat[i].Color[2],
                 (int)fop->dat[i].Start, (int)fop->dat[i].End,
                 fop->dat[i].Min, fop->dat[i].Max);                     /* 417 */
}

/* --------------------------------------------------------------------------
 *  MapFogSetAnimDat (448..453)   [inlined]
 * ------------------------------------------------------------------------ */
static void MapFogSetAnimDat(MDAT_FOG *fop)
{
    MapFogAnimStart(MAPFOG_ANIM_FRAME);                                 /* 451 */
    MapFogSetFogDat(&MapFogDat[1], fop);                                /* 452 */
}

/* --------------------------------------------------------------------------
 *  MapFogGetNewDat (456..486)
 *
 *  Find the fog record covering `pos`.  `room_no` is accepted and never read.
 *  RegDatGetBuffIDG() answers with a buffer id, or -2/-3 when the position is
 *  covered by more than one loaded buffer -- in which case every buffer on
 *  the hit list is tried in turn and the first answer wins -- or -1 for
 *  "nothing here".
 * ------------------------------------------------------------------------ */
static MDAT_FOG *MapFogGetNewDat(int room_no, int floor, float *pos)
{
    int       buff_id;
    int      *lp;
    int       i;
    MDAT_FOG *fop;

    buff_id = RegDatGetBuffIDG(floor, pos);                             /* 466 */

    if (-4 < buff_id && buff_id < -1)                                   /* 467 */
    {
        lp = RegDatGetHitList();                                        /* 473 */

        for (i = 0; i < RegDatGetHitNum(); i++)                         /* 474 */
        {
            fop = MapFogUpdate(*lp++, pos);                             /* 476 */
            if (fop != NULL)
            {
                return fop;
            }
        }

        return NULL;
    }

    if (buff_id == -1)
    {
        return NULL;
    }

    return MapFogUpdate(buff_id, pos);                                  /* 482 */
}

/* --------------------------------------------------------------------------
 *  MapFogAnimNowDat (490..502)
 *
 *  Arm a walk towards the region `pos` is in.  MapFogDat[2] is left alone --
 *  MapFogAnim() moves it from here.
 * ------------------------------------------------------------------------ */
static void MapFogAnimNowDat(int room_no, int floor, float *pos)
{
    MDAT_FOG *fop;

    fop = MapFogGetNewDat(room_no, floor, pos);                         /* 492 */

    if (fop != NULL)                                                    /* 495 */
    {
        MapFogSetAnimDat(fop);

        MapFogFlg |= MAPFOG_F_ANIM;                                     /* 498 */
        MapFogFlg &= ~MAPFOG_F_HOLD;                                    /* 499 */
    }
}                                                                       /* 502 */

/* --------------------------------------------------------------------------
 *  MapFogSetNowDat (513..547)
 *
 *  The snap path: write the region's setting straight into MapFogDat[2] and
 *  put the sky at its end state instead of fading to it.  Any walk in flight
 *  is cancelled, whether or not a region was found.
 * ------------------------------------------------------------------------ */
static void MapFogSetNowDat(int room_no, int floor, float *pos)
{
    MDAT_FOG *fop;

    fop = MapFogGetNewDat(room_no, floor, pos);                         /* 515 */

    if (fop != NULL)                                                    /* 517 */
    {
        MapFogSetFogDat(&MapFogDat[2], fop);

        MapFogFlg &= ~MAPFOG_F_HOLD;                                    /* 522 */

        if (MapFogFlg & MAPFOG_F_SKY)                                   /* 523 */
        {
            MapSkySetAlpha(0xFF);                                       /* 525 */
            MapFogFlg |= MAPFOG_F_NOFADE;
        }
        else if (MapFogFlg & MAPFOG_F_FOG)                              /* 527 */
        {
            MapSkySetAlpha(0);                                          /* 528 */
            MapFogFlg |= MAPFOG_F_NOFADE;                               /* 529 */
        }

        if ((MapFogFlg & MAPFOG_F_FINDER) == 0)                         /* 533 */
        {
            MapSkySetAlpha(0xFF);                                       /* 534 */
        }
    }

    MapFogFlg &= ~MAPFOG_F_ANIM;                                        /* 546 */
}

/* --------------------------------------------------------------------------
 *  MapFogReset (558..566)
 * ------------------------------------------------------------------------ */
void MapFogReset(void)
{
    MapFogFrame    = 0;                                                 /* 560 */
    MapFogNowFrame = 0;                                                 /* 561 */
    MapFogFlg      = 0;                                                 /* 562 */

    MapFogRect     = NULL;                                              /* 564 */
    MapFogLastRoom = -1;                                                /* 565 */
}

/* --------------------------------------------------------------------------
 *  MapFogGetCamPos (569..572)   [inlined]
 * ------------------------------------------------------------------------ */
static float (&MapFogGetCamPos(void))[4]
{
    return gra3dcamGetPosition();                                       /* 571 */
}

/* --------------------------------------------------------------------------
 *  MapFogProc (617..671)
 *
 *  `pos` is accepted and never read: the fog region is sampled at the camera,
 *  so the fog changes as the camera crosses a boundary rather than as the
 *  player does.
 * ------------------------------------------------------------------------ */
void MapFogProc(int room_no, int floor, float *pos)
{
    float (&rvPos)[4] = MapFogGetCamPos();
    int    id;

    MapFogFlg &= ~MAPFOG_F_CHANGE;                                      /* 623 */
    MapFogChkFinderMode();

    if (MapFogLastRoom != room_no)                                      /* 628 */
    {
        id = PlyrOutsideCheck();                                        /* 630 */
        if (id == 0)
        {
            /* MAPFOG_F_HOLD is cleared here and in both *NowDat setters, and
             * is set nowhere in MapFog.o -- it reads as a leftover. */
            MapFogFlg &= ~MAPFOG_F_HOLD;                                /* 631 */
        }
        else
        {
            /* Outdoors the cached rectangle is worthless, so force a fresh
             * region lookup on the next frame. */
            MapFogRect = NULL;                                          /* 633 */
        }

        MapFogLastRoom = room_no;                                       /* 635 */
    }

    if ((MapFogFlg & MAPFOG_F_EVENT) == 0)                              /* 639 */
    {
        if (MAPFOG_CUT_DIST <= (int)GetDistV(rvPos, MapFogBfoCam) ||
            (MapFogFlg & MAPFOG_F_CHANGE))                              /* 643 */
        {
            MapFogSetNowDat(room_no, floor, rvPos);                     /* 646 */
        }
        /* While the camera is still inside the rectangle the current setting
         * came from there is nothing to look up, so the common case costs one
         * rectangle test and no region search at all. */
        else if (MapFogRect == NULL ||
                 MrecIsInRectangle(rvPos, MapFogRect) == 0)             /* 650 */
        {
            MapFogAnimNowDat(room_no, floor, rvPos);                    /* 656 */
        }
    }

    MapFogCopyVec(MapFogBfoCam, rvPos);

    if (MapFogFlg & MAPFOG_F_ANIM)                                      /* 667 */
    {
        MapFogAnim(&MapFogDat[2], &MapFogDat[0], &MapFogDat[1]);        /* 669 */
    }

    MapFogSetFog(&MapFogDat[2]);
}

/* --------------------------------------------------------------------------
 *  MapFogDbProc (736..739)
 *
 *  Debug-mode entry.  In this build it is a straight forward to MapFogProc().
 * ------------------------------------------------------------------------ */
void MapFogDbProc(int room_no, int floor, float *pos)
{
    MapFogProc(room_no, floor, pos);                                    /* 738 */
}
