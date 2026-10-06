/* ==========================================================================
 *  ingame/map/MapSky.h
 *
 *  Outdoor sky: the per-area database, the two texture pages it draws with,
 *  and the entry points MapLoad.o / MhCtl.o / MapFog.o reach for.
 *
 *  Reconstructed from the Feb 6 2004 prototype (SLES_523.84).
 * ======================================================================== */

#ifndef _INGAME_MAP_MAPSKY_H
#define _INGAME_MAP_MAPSKY_H

#include <stdint.h>                 /* uintptr_t */
#include <sys/types.h>              /* u_int / u_long */

#include "../../graphics/graph2d/g2d_draw.h"     /* Q_WORDDATA */

/* --------------------------------------------------------------------------
 *  One layer of the sky dome.
 *
 *  `frame` is the scroll period in fields; zero means the layer is static and
 *  MapSkyDrawTen() draws it without stepping MapSkyFrame.  `scale_min` sizes
 *  the UV sweep across the 23x23 grid, so it is really a tile count, not a
 *  scale: 3.5 lays roughly three and a half copies of the 256x256 page across
 *  the dome.  `scale_max` / `scale_now` are unused by the draw path in this
 *  build.
 * ------------------------------------------------------------------------ */
typedef struct                      /* 0x28 */
{
    /* 0x00 */ float hight;         /* dome plane Y (sic -- ROM spelling)   */
    /* 0x04 */ float scale_min;
    /* 0x08 */ float scale_max;
    /* 0x0c */ float scale_now;
    /* 0x10 */ int   frame_now;
    /* 0x14 */ int   frame;
    /* 0x18 */ int   fr;
    /* 0x1c */ int   fg;
    /* 0x20 */ int   fb;
    /* 0x24 */ int   fa;
} MAP_SKY_ST;

/* --------------------------------------------------------------------------
 *  Per-area sky record.
 *
 *  `len` is how far in front of the camera the horizon point is projected,
 *  `speed` scales the yaw-driven horizontal scroll, `move_y` and `offset_y2`
 *  push the projected horizon up or down, and `scale` is the horizon strip's
 *  height in pixels once multiplied by 255.  fog_* is the strip's colour and
 *  its alpha.  sky_stat[1] is the only layer this build draws.
 * ------------------------------------------------------------------------ */
typedef struct                      /* 0xa0 */
{
    /* 0x00 */ float      len;
    /* 0x04 */ float      speed;
    /* 0x08 */ float      offset_y;
    /* 0x0c */ int        offset_y2;
    /* 0x10 */ int        move_y;
    /* 0x14 */ float      scale;
    /* 0x18 */ int        fog_r;
    /* 0x1c */ int        fog_g;
    /* 0x20 */ int        fog_b;
    /* 0x24 */ int        fog_al;
    /* 0x28 */ MAP_SKY_ST sky_stat[3];
} MAP_SKY_DB;

/* ---- lifecycle --------------------------------------------------------- */
uintptr_t MapSkyInit(uintptr_t mst_addr);
void      MapSkyRegist(void);

/* ---- per-frame --------------------------------------------------------- */
void      MapSkyProc(void);

/* ---- pieces the rest of the engine drives ------------------------------ */
u_long    MapSkyGetTex0(uintptr_t top_addr);
void      MapSkySetTim2Vram(uintptr_t *top_addr);
void      MapSkySetAlpha(int iAlpha);
void      MapSkyDraw(int (&ivec)[4], float ry, MAP_SKY_DB *ep);

#endif /* _INGAME_MAP_MAPSKY_H */
