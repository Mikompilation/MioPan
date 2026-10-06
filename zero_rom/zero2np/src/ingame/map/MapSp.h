/* ==========================================================================
 *  ingame/map/MapSp.h
 *
 *  Special map processing: the four placed objects that need a per-frame job
 *  of their own rather than an animation clip.  MapSpObjReg() recognises them
 *  by FurnCtl id as they are registered and arms the matching job.
 *
 *      kaza    the wind-blown paper charms -- up to 64 at once, each spun
 *              about the placement's own Z by one of five shared speeds
 *      movi    the projector reel, spun about X or Z at a debug-menu rate
 *      kage    the shadow object, which suppresses its own shadow pass
 *      fusuma  the sliding screen, re-lit while its animation plays
 *
 *  Reconstructed from the Feb 6 2004 prototype (SLES_523.84).
 * ======================================================================== */

#ifndef _INGAME_MAP_MAPSP_H
#define _INGAME_MAP_MAPSP_H

#include "eetypes.h"

#include "MapObjReg.h"                  /* MAPOBJ_DAT */
#include "../../debug/debug_menu.h"

enum
{
    MAPSP_KAZ_NUM   = 64,       /* MapSpKazList slots            */
    MAPSP_SPEED_NUM = 5,        /* shared kaza speed generators  */
    MAPSP_FUNC_NUM  = 3         /* kaza / kage / fusuma jobs     */
};

/* Spawn parameters for the "kaza" props, all live-editable from OBJ SP. */
typedef struct                      /* 0x18 */
{
    /* 0x00 */ float fMinSpeed;
    /* 0x04 */ float fMaxSpeed;
    /* 0x08 */ int   iMinFrame;
    /* 0x0c */ int   iMaxFrame;
    /* 0x10 */ float fReelSpeed;
    /* 0x14 */ int   iSave;
} MAPSP_KAZ_DB;

/* One registered kaza.  `pHdl` is the MapPut handle and doubles as the free
 * marker (NULL = empty slot); `pfMstRot` points into the registration record's
 * own rotation, so a moved placement re-aims the charm without a re-register.
 *
 * The ROM packs the scalars at 0x00..0x13 and puts vPos at 0x20 -- the 12-byte
 * gap is the EE SDK's quadword alignment on sceVu0FVECTOR.  On the host the
 * two pointers widen to 8 bytes and land vPos back at 0x20 with sizeof 0x30,
 * so no explicit padding is needed; the offset comments still read true. */
typedef struct MAPSP_KAZ_HEAD       /* 0x30 */
{
    /* 0x00 */ void  *pHdl;
    /* 0x04 */ float  fRot;         /* degrees, wrapped into [0, 360)      */
    /* 0x08 */ float *pfMstRot;     /* the placement's authored rotation   */
    /* 0x0c */ int    iGroupID;     /* room buffer that registered it      */
    /* 0x10 */ int    iType;        /* which MapSpKazSpeed generator       */
    /* 0x20 */ sceVu0FVECTOR vPos;  /* placement position, latched once    */
} MAPSP_KAZ_HEAD;

/* One of the five shared speed generators.  Every kaza of a given type reads
 * `fSpeed`, so they all gust together; the generator eases from fMstSpeed to
 * fNextSpeed over iNextFrame frames and then picks a new target. */
typedef struct MAPSP_KAZ_SPEED      /* 0x14 */
{
    /* 0x00 */ float fSpeed;
    /* 0x04 */ float fMstSpeed;
    /* 0x08 */ float fNextSpeed;
    /* 0x0c */ int   iFrame;
    /* 0x10 */ int   iNextFrame;
} MAPSP_KAZ_SPEED;

extern DEBUG_MENU dbg_kaza_main;    /* data 2cc628 */

/* --- kaza ---------------------------------------------------------------- */

/* Claims a slot for `pHdl` and gives it a random starting angle.  Returns 0,
 * or -1 when all 64 slots are taken. */
int  MapSpKazRegistObj(int iGroup, void *pHdl, int iType, float *pfMstRot);
void MapSpKazDeleteGroup(int iGroupID);
void MapSpKazDeleteAll(void);
/* Rebuilds one kaza's placement matrix from its current angle. */
void MapSpKazSetMatrix(MAPSP_KAZ_HEAD *pKazHead);
/* MapSpFuncList[0]: steps the five speed generators, then re-matrices every
 * live kaza.  Disarms itself the frame it finds none. */
int  MapSpKazProc(void);

/* --- projector reel ------------------------------------------------------ */

/* The reel's placement position, handed out so the projector event can aim a
 * beam at it. */
float *MapSpGetReelPos(void);
void   MapSpMoviSetFlg(int iOn_Off);
int    MapSpMoviProc(void);

/* --- shadow / sliding screen --------------------------------------------- */

int  MapSpKageProc(void);
int  MapSpFusumaProc(void);

/* --- drawing ------------------------------------------------------------- */

/* MapPut draw callback for the "kaya" (mosquito net) model: lays the room's
 * shadow pass down first so the net draws over it. */
void MapSpKayaDrawCallback(void);

/* --- module -------------------------------------------------------------- */

/* Non-zero from chapter 7 on -- MapObjRegistTreeAnim() swaps to the windier
 * foliage motion and MapSky picks the rough-weather settings. */
int  MapSpAraCheck(void);
/* Hooks a placed object into the special-processing list, if its FurnCtl id
 * names one; released per buffer on teardown. */
void MapSpObjReg(int iGroup, char *sName, MAPOBJ_DAT *pMObjDat, float *pfRot);
void MapSpObjRelease(int iGroupID);

void MapSpProc(void);
void MapSpInit(void);

#endif /* _INGAME_MAP_MAPSP_H */
