/* ==========================================================================
 *  ingame/map/MapLight.h
 *
 *  Room lighting: the player's flashlight, the per-object pre-render (baked)
 *  light, the "pick the strongest N lights" selection the GS budget forces,
 *  the two-room blend a door standing in an opening needs, and the Mei room's
 *  animated flicker.
 *
 *  Reconstructed from the Feb 6 2004 prototype (SLES_523.84), MapLight.o.
 * ======================================================================== */

#ifndef _INGAME_MAP_MAPLIGHT_H
#define _INGAME_MAP_MAPLIGHT_H

#include "MapLoad.h"            /* MLOAD_HEAD, GRA3DLIGHTDATA, G3DLIGHT */

/* Room-light tuning, exposed on the ROOM debug menu (MhCtl.c).  Index 0 is
 * the normal light, index 1 the flashlight.  The odd MapLightPower default is
 * genuine: it is 1.0f nudged down by a 0.01 debug-menu step and saved back. */
extern float MapLightPower;         /* sdata 3eee40 */
extern float MapLightIntens[2];     /* sdata 3eee48 */
extern float MapLightDiff[2];       /* sdata 3eee50 */

/* --------------------------------------------------------------------------
 *  Working records for the two-room blend.
 *
 *  MapLightMakeDual() cannot simply merge two GRA3DLIGHTDATA blocks: the GS
 *  has one slot per light id, so the two rooms' lights compete for the same
 *  16 point and 16 spot slots.  MAP_LIGHT_SORT is the candidate list built
 *  from both rooms and ranked by MapLightGetPower(); MAP_LIGHT_DAT is the
 *  merged result that MapLightUpdate() folds back into the output block.
 * ------------------------------------------------------------------------ */
typedef struct                          /* 0x90 */
{
    /* 0x00 */ int              flg;
    /* 0x04 */ int              aiPad[3];
    /* 0x10 */ G3DLIGHT         ldat;
    /* 0x80 */ GRA3DLIGHTSTATUS lstat;
} MAP_LIGHT_DAT;

/* 0x10 on target; the two pointers make it 0x18 on the host. */
typedef struct
{
    /* 0x0 */ int               flg;
    /* 0x4 */ G3DLIGHT         *addr;
    /* 0x8 */ GRA3DLIGHTSTATUS *st_addr;
    /* 0xc */ float             power;
} MAP_LIGHT_SORT;

/* Per-slot scratch for MapLightSelect(): iFlg starts as "already excluded"
 * and doubles as the "already picked" mark once selection starts. */
typedef struct                          /* 0x8 */
{
    /* 0x0 */ int   iFlg;
    /* 0x4 */ float fPow;
} MAP_LIGHT_HEAD;

/* Most lights the GS budget lets MapLightSelect() turn on in one range. */
#define MAP_LIGHT_SELECT_MAX    14

/* ---- Mei (flicker) light ------------------------------------------------
 *
 * The one room whose lighting is animated off a key-frame table rather than
 * held static.  MapMeiList is split into MAPMEI_HEAD_NUM sequences by
 * separators with max < 0 and closed by max == MAPMEI_LIST_END. */
#define MAPMEI_LIST_NUM         25      /* data 2c8d20, 0x1f4 bytes */
#define MAPMEI_HEAD_NUM         4       /* bss 4066f0 */
#define MAPMEI_LIGHTONE_NUM     39      /* bss 406730 */
#define MAPMEI_LIST_END         (-1000)

/* Brightness the flicker rests at between key frames, in MAPMEI_FRAME::max's
 * units (percent).  MapMeiAnimFrame() ramps from it up to `max` and back. */
#define MAPMEI_BASE_POWER       50

typedef struct                          /* 0x14 */
{
    /* 0x00 */ int max;
    /* 0x04 */ int frame[4];
} MAPMEI_FRAME;

typedef struct                          /* 0x10 */
{
    /* 0x0 */ MAPMEI_FRAME *top_dat_p;
    /* 0x4 */ MAPMEI_FRAME *now_dat_p;
    /* 0x8 */ int           stat;
    /* 0xc */ int           frame;
} MAPMEI_HEAD;

/* The ROM pads `lip` out to 0x10 so m_light starts quadword-aligned for the
 * VU copy; the host's 8-byte pointer only reaches 0x8, so the gap is spelled
 * out rather than left to the compiler. */
typedef struct                          /* 0x80 */
{
    /* 0x00 */ G3DLIGHT *lip;
    /* 0x08 */ char      pad[8];
    /* 0x10 */ G3DLIGHT  m_light;
} MAPMEI_LIGHTONE;

/* ---- Player / room lights ----------------------------------------------- */

/* Debug: force the diffuse red of every point and spot slot to full. */
void MapLightLed(GRA3DLIGHTDATA *light);

/* Bakes the room light into one placed object's model, or hands it the room's
 * live light block.  pre_flg 0 pre-lights now, 1 switches to the live light. */
void MapLightSetLight(int buff_id, int *mdl_addr, void *obj_hdl, int pre_flg);
void MapLightPreRenderObj(void *hdl, int buff_id);
/* Same, addressed by room number rather than buffer id. */
void MapLightPreRenderObj2(void *hdl, int room_no);

/* Enables only the flashlight slots / restores them with live values. */
void MapLightSetPlayerOnly(void);
void MapLightSetPlayerReal(void);
/* Copies `mst` to `LD` and folds the player's flashlight into it. */
void MapLightMakeRoomReal(GRA3DLIGHTDATA *LD, GRA3DLIGHTDATA *mst);

/* Re-bakes the pre-rendered lighting for every door, object and put-item
 * registered under `reg_id`.  MapDrawPreLight() runs it per registration. */
void MapLightRePreRender(int buff_id, int reg_id);

/* Leaves only the MAP_LIGHT_SELECT_MAX strongest lights of [iStart, iEnd]
 * enabled, ranked by their power at `vPos`. */
void MapLightSelect(GRA3DLIGHTDATA *pLightMst, float *vPos, int iStart, int iEnd);
/* MapLightSelect() over both the point and the spot range. */
void MapLightSelectEnable(GRA3DLIGHTDATA *pLightMst, float *vPos);

/* Blends two rooms' lights at `vCenPos`, which is what gives a door standing
 * in the opening a light from both sides at once. */
void MapLightMakeDual(GRA3DLIGHTDATA *out, GRA3DLIGHTDATA *light1,
                      GRA3DLIGHTDATA *light2, float *vCenPos);
/* The directional half of that blend, run only when both rooms have one:
 * the three directional slots are kept by brightness. */
void MapLightMakeDualDirect(GRA3DLIGHTDATA *pLightOut, GRA3DLIGHTDATA *pLight1,
                            GRA3DLIGHTDATA *pLight2);
/* Folds the merged MAP_LIGHT_DAT run back into `pOutLight`; returns how many
 * slots ended up enabled. */
int  MapLightUpdate(GRA3DLIGHTDATA *pOutLight, MAP_LIGHT_DAT *pUpLight,
                    int iStart, int iEnd);
/* Copies `mst` into `out`. */
void MapLightSetScale(GRA3DLIGHTDATA *out, GRA3DLIGHTDATA *mst);

/* ---- Mei (flicker) light ------------------------------------------------ */

/* The Mei room's animated light block; valid before MapMeiInit() has run. */
GRA3DLIGHTDATA *MapMeiGetLight(void);
/* Non-zero when the room is the "Mei" room (label 0x326 == room 50), which
 * the renderer lights differently. */
int  MapMeiCheck(MLOAD_HEAD *hp);
/* Adds one light living outside the room block to the flicker, so effects and
 * placed lamps dim along with the room. */
void MapMeiRegistLightOne(G3DLIGHT *lp);
/* Seeds the flicker state from the room's light data. */
void MapMeiInit(GRA3DLIGHTDATA *mst);
void MapMeiTerm(void);
/* Advances the flicker one frame.  -1 = never initialised, -2 = no sequence
 * bound, 0 = stepped. */
int  MapMeiProc(void);

#endif /* _INGAME_MAP_MAPLIGHT_H */
