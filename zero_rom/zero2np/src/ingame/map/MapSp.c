// FILE: /home/zero_rom/zero2np/src/ingame/map/MapSp.c
//
// Special map processing.  Four placed models need a per-frame job of their own
// rather than an animation clip, and MapSpObjReg() arms them as the room's
// registration records are walked -- it is a switch on FurnCtlGetID(), i.e. on
// the three-digit number in the model name.
//
//   kaza    (405)          wind-blown paper charms.  Up to 64 at once; each
//                          spins about the placement's own Z at one of five
//                          shared speeds, so a group gusts together.
//   movi    (218 / 236)    the projector reel, spun about X or Z at the rate
//                          the OBJ SP debug row sets.
//   kage    (262)          the shadow object, drawn with its own shadow pass
//                          suppressed while its action plays.
//   fusuma  (28 / 330)     the sliding screen, re-lit every frame its action
//                          runs and released when the action reaches step 2.
//
// The jobs live in MapSpFuncList[], which MapSpProc() pumps once a frame; each
// job clears its own slot when its object goes away, so an empty room costs
// three null tests.
//
// Reconstructed from the Feb 6 2004 prototype (SLES_523.84), MapSp.o
// 0x001133c8..0x0011436b.
//
// Two notes on the line annotations.  MapSpKazDeleteGroup() and MapSpKazProc()
// each subscript MapSpKazList twice but emit only one _fixed_array_verifyrange
// call -- GCC CSE'd the second, which MapSpKazGetFreeArea (two subscripts, two
// calls) shows it does not always do.  The source is written with both
// subscripts.  And the statement that takes a fixed_array or a MapGeom.h inline
// leaves no MapSp.c $LM at all, so a few lines here are interpolated into a
// measured gap rather than read off the disassembly.

#include "MapSp.h"

#include "../../common/utility.h"       /* _SetVector, GetRandValF/I         */
#include "../../common/utility2.h"      /* PRINT_ERROR                       */
#include "../../common/variable.h"      /* ingame_wrk                        */

#include "MapGeom.h"
#include "MapDraw.h"                    /* MapDrawObj, MapDrawGetLightPtr... */
#include "MapLight.h"                   /* MapLightPreRenderObj              */
#include "MapPut.h"
#include "MhCtl.h"                      /* MhCtlDrawShadow, dbg_room_main    */
#include "FurnCtl.h"                    /* FurnCtlGetID                      */
#include "FurnLoad.h"                   /* FurnLoadGetAttr                   */
#include "RegDat.h"                     /* MDAT_OBJ                          */

#include "../../graphics/graph3d/ctl/fixed_array.h"
#include "../../graphics/graph3d/gra3dMisc.h"   /* gra3dSetObjectIdDrawNo... */
#include "../../debug/debug_menu.h"

#include <libvu0.h>
#include <stdio.h>
#include "../../graphics/graph3d/g3dxVu0.h" /* g3dxVu0CopyVector */

/* lit4 3ed8a4 / 3ed8ac -- two copies of the same word, one per expansion of the
 * radians-back-to-degrees step.  0x40490fda is 3.1415925f, not the libm
 * constant; the same spelling the rest of the tree uses. */
#define MAPSP_PI        3.1415925f

/* lit4 3ed8b4 / 3ed8b8 / 3ed8bc -- three copies again, all 30000.0f.  Read them
 * out of the ELF rather than assuming three different offsets. */
#define MAPSP_ZOFFSET   30000.0f

/* The reel and the charms both wrap their angle through [0, 360). */
#define MAPSP_DEG_MIN   0.0f
#define MAPSP_DEG_MAX   360.0f

typedef int (*MAPSP_FUNC)(void);

static char s_dbg_kaza_title[] = "ObjData_Main";
static char s_dbg_kaz_min_speed[] = "KAZ_MIN_SPEED";
static char s_dbg_kaz_max_speed[] = "KAZ_MAX_SPEED";
static char s_dbg_kaz_min_frame[] = "KAZ_MIN_FRAME";
static char s_dbg_kaz_max_frame[] = "KAZ_MAX_FRAME";
static char s_dbg_reel_speed[] = "REEL_SPEED";
static char s_dbg_save[] = "SAVE";
static char s_dbg_end[] = "_end_";

/* Tunables the debug menu edits in place.  fMaxSpeed's default is 21.0f as
 * nudged by the 0.2f menu step and saved back, which is why it is not exactly
 * 21 -- the ROM's .data holds 0x41a7ffef. */
static MAPSP_KAZ_DB MapSpDbDat =                                        /* data 2cc610 */
{
    5.0f,
    20.999968f,
    10,
    50,
    -10.0f,
    0,
};

/* The two speed steps are 0x3e4ccccc, one ULP below 0.2f -- the table was
 * itself edited through the menu and written back by DbmSave(), so the
 * literals here are chosen to reproduce the ROM's exact bits. */
DEBUG_MENU dbg_kaza_main =                                              /* data 2cc628 */
{
    &dbg_room_main,
    nullptr,
    s_dbg_kaza_title,
    {
        { s_dbg_kaz_min_speed, DBM_ATTR_VALUE | DBM_ATTR_FLOAT,
                               &MapSpDbDat.fMinSpeed,     0.0f, 1000.0f, 0.19999999f },
        { s_dbg_kaz_max_speed, DBM_ATTR_VALUE | DBM_ATTR_FLOAT,
                               &MapSpDbDat.fMaxSpeed,     0.0f, 1000.0f, 0.19999999f },
        { s_dbg_kaz_min_frame, DBM_ATTR_VALUE,
                               &MapSpDbDat.iMinFrame,     1.0f, 1000.0f, 1.0f },
        { s_dbg_kaz_max_frame, DBM_ATTR_VALUE,
                               &MapSpDbDat.iMaxFrame,     1.0f, 1000.0f, 1.0f },
        { s_dbg_reel_speed,    DBM_ATTR_VALUE | DBM_ATTR_FLOAT,
                               &MapSpDbDat.fReelSpeed, -360.0f,  360.0f, 0.5f },
        { s_dbg_save,          DBM_ATTR_SWITCH,
                               &MapSpDbDat.iSave,         0.0f,    1.0f, 1.0f },
        { s_dbg_end,           0, nullptr,                0.0f,    0.0f, 0.0f },
    },
    0,
    0,
    0,
    0,
};

/* --- projector reel ------------------------------------------------------ */
static int   MapSpMoviFlg;                                              /* sdata 3ef0d0 */
static void *MapSpMoviHdl;                                              /* sdata 3ef0d4 */
static float MapSpMoviRot;                                              /* sdata 3ef0d8 */
static int   MapSpMoveFlg;                                              /* sdata 3ef0dc */

/* --- shadow object ------------------------------------------------------- */
static MDAT_OBJ *MapSpKageObjPtr;                                       /* sdata 3ef0e0 */
static int       MapSpKageBuffID = -1;                                  /* sdata 3ef0e4 */

/* --- sliding screen ------------------------------------------------------ */
static MAPOBJ_DAT *MapSpFusumaPtr;                                      /* sdata 3ef0e8 */
static int         MapSpFusumaBuffID = -1;                              /* sdata 3ef0ec */

/* Both BuffIDs really are -1 in the ROM's .sdata, not 0 -- MapSpInit() rewrites
 * MapSpKageBuffID but nothing ever seeds MapSpFusumaBuffID, so a zero there
 * would make buffer 0 release the screen it never registered. */

static fixed_array<MAPSP_KAZ_HEAD, MAPSP_KAZ_NUM>   MapSpKazList;       /* bss 420d00 */
static fixed_array<MAPSP_KAZ_SPEED, MAPSP_SPEED_NUM> MapSpKazSpeed;     /* bss 421900 */

static MAPSP_FUNC MapSpFuncList[MAPSP_FUNC_NUM];                        /* bss 421968 */
static sceVu0FVECTOR MapSpMoviPos;                                      /* bss 421980 */

/* ==========================================================================
 *  MapSpKayaDrawCallback
 *
 *  MapPut draw callback for the mosquito-net model (id 9).  The net is drawn
 *  with a Z offset that puts it in front of everything, so the room's shadow
 *  pass has to be laid down first or it would land on top of the net.
 * ======================================================================== */
void MapSpKayaDrawCallback(void)                                        /* 50 */
{                                                                       /* 51 */
    void   *pHdl   = MapPutGetNowHdl();                                 /* 52 */
    u_int  *pModel = MapPutGetModelPtr(pHdl);                           /* 53 */

    MhCtlDrawShadow();                                                  /* 56 */

    MapDrawObj(pModel, *MapPutGetMatrixPtr(pHdl));                      /* 59 */
}                                                                       /* 60 */

/* ==========================================================================
 *  MapSpKazGetFreeArea
 *
 *  First slot whose handle is NULL.  Neither loop here nor in the two delete
 *  routines is braced in the ROM -- there are no LBRAC records at all.
 * ======================================================================== */
static MAPSP_KAZ_HEAD *MapSpKazGetFreeArea(void)                        /* 157 */
{                                                                       /* 158 */
    int i;

    for (i = 0; i < MAPSP_KAZ_NUM; i++)                                 /* 161 */
        if (MapSpKazList[i].pHdl == nullptr)                            /* 162 */
            return &MapSpKazList[i];                                    /* 163 */

    PRINT_ERROR("NO_SPACE\n");                                          /* 165 */

    return nullptr;                                                     /* 166 */
}                                                                       /* 167 */

/* ==========================================================================
 *  MapSpKazRegistObj
 *
 *  `pfMstRot` is kept as a pointer into the caller's rotation, not copied, so
 *  a placement that is re-aimed later re-aims its charm too.  The position is
 *  latched here because MapSpKazSetMatrix() rebuilds the matrix from scratch
 *  every frame and would otherwise lose it.
 * ======================================================================== */
int MapSpKazRegistObj(int iGroup, void *pHdl, int iType, float *pfMstRot)/* 170 */
{                                                                       /* 171 */
    MAPSP_KAZ_HEAD *pKazHead = MapSpKazGetFreeArea();                   /* 176 */

    if (pKazHead == nullptr)
    {
        PRINT_ERROR("NO_FREE_SPACE group[%d] type[%d]\n", iGroup, iType);/* 177 */
        return -1;                                                      /* 178 */
    }

    pKazHead->pfMstRot = pfMstRot;                                      /* 181 */
    pKazHead->pHdl     = pHdl;                                          /* 182 */
    pKazHead->iGroupID = iGroup;                                        /* 183 */
    pKazHead->iType    = iType;                                         /* 184 */
    pKazHead->fRot     = GetRandValF(360.0f);                           /* 185 */

    g3dxVu0CopyVector(pKazHead->vPos, (*MapPutGetMatrixPtr(pKazHead->pHdl))[3]);
                                                                        /* 188 */
    pKazHead->vPos[3] = 1.0f;                                           /* 190 */

    return 0;                                                           /* 191 */
}                                                                       /* 192 */

void MapSpKazDeleteGroup(int iGroupID)                                  /* 196 */
{                                                                       /* 197 */
    int i;

    for (i = 0; i < MAPSP_KAZ_NUM; i++)                                 /* 200 */
    {
        if (MapSpKazList[i].iGroupID == iGroupID)                       /* 202 */
            MapSpKazList[i].pHdl = nullptr;                             /* 203 */
    }                                                                   /* 204 */
}                                                                       /* 205 */

void MapSpKazDeleteAll(void)                                            /* 208 */
{                                                                       /* 209 */
    int i;

    for (i = 0; i < MAPSP_KAZ_NUM; i++)                                 /* 212 */
    {
        MapSpKazList[i].pHdl = nullptr;                                 /* 214 */
    }                                                                   /* 215 */
}                                                                       /* 216 */

/* ==========================================================================
 *  MapSpKazGetSpeed
 *
 *  The range test is a single `sltiu`, so it is one unsigned compare in the
 *  source rather than a signed pair.
 * ======================================================================== */
static float MapSpKazGetSpeed(int iType)                                /* 225 */
{                                                                       /* 226 */
    if ((u_int)iType >= MAPSP_SPEED_NUM)                                /* 227 */
    {
        PRINT_ERROR("NO_SPEED_TYPE[%d]\n", iType);                      /* 228 */
        return 0.0f;                                                    /* 229 */
    }

    return MapSpKazSpeed[iType].fSpeed;                                 /* 231 */
}                                                                       /* 232 */

/* ==========================================================================
 *  MapSpKazAnim
 *
 *  Steps the five shared speed generators.  Each one eases fSpeed from
 *  fMstSpeed to fNextSpeed over iNextFrame frames; when the count runs out it
 *  adopts the target and, on the *following* cycle, draws a new one.  That
 *  alternation is what the pair of identical `fMstSpeed = fNextSpeed` stores at
 *  lines 260 and 264 buys: the not-equal arm only adopts, which leaves the two
 *  equal so the next expiry takes the other arm and re-randomises.
 * ======================================================================== */
static int MapSpKazAnim(void)                                           /* 236 */
{                                                                       /* 237 */
    int i;

    for (i = 0; i < MAPSP_SPEED_NUM; i++)                               /* 240 */
    {
        MAPSP_KAZ_SPEED *pSpeed = &MapSpKazSpeed[i];                    /* 242 */

        if (pSpeed->fMstSpeed != pSpeed->fNextSpeed)                    /* 244 */
        {
            pSpeed->fSpeed = (pSpeed->fNextSpeed - pSpeed->fMstSpeed)
                             * (float)pSpeed->iFrame
                             / (float)pSpeed->iNextFrame
                             + pSpeed->fMstSpeed;                       /* 247 */
        }

        if (++pSpeed->iFrame >= pSpeed->iNextFrame)                     /* 251 */
        {
            pSpeed->iNextFrame =
                GetRandValI(MapSpDbDat.iMaxFrame - MapSpDbDat.iMinFrame)
                + MapSpDbDat.iMinFrame;                                 /* 252 */

            pSpeed->iFrame = 0;                                         /* 255 */
            pSpeed->fSpeed = pSpeed->fNextSpeed;                        /* 256 */

            if (pSpeed->fMstSpeed != pSpeed->fNextSpeed)                /* 259 */
            {
                pSpeed->fMstSpeed = pSpeed->fNextSpeed;                 /* 260 */
            }
            else
            {
                pSpeed->fMstSpeed = pSpeed->fNextSpeed;                 /* 264 */
                pSpeed->fNextSpeed =
                    GetRandValF(MapSpDbDat.fMaxSpeed - MapSpDbDat.fMinSpeed)
                    + MapSpDbDat.fMinSpeed;                             /* 265 */

                /* The draw is over [fMinSpeed, fMaxSpeed), so a negative
                 * fMinSpeed can produce a negative speed -- reflected rather
                 * than clamped, which keeps the distribution's shape. */
                if (pSpeed->fNextSpeed < 0.0f)                          /* 268 */
                {
                    pSpeed->fNextSpeed = -pSpeed->fNextSpeed;
                }
            }
        }
    }                                                                   /* 271 */

    return 0;                                                           /* 273 */
}                                                                       /* 274 */

/* ==========================================================================
 *  MapSpKazSetMatrix
 *
 *  Rebuilds one charm's placement matrix.  The angle round-trip at 287 --
 *  degrees to radians and straight back -- is not pointless: MapGeomDegToRad()
 *  folds [180, 360) onto [-pi, 0), so the pair wraps fRot into [-180, 180),
 *  which is the range the Z rotation wants.
 * ======================================================================== */
void MapSpKazSetMatrix(MAPSP_KAZ_HEAD *pKazHead)                        /* 277 */
{                                                                       /* 278 */
    float          vRot[4];
    float          fRot;
    MAPPUT_MATRIX *mat;

    pKazHead->fRot += MapSpKazGetSpeed(pKazHead->iType);                /* 282 */
    pKazHead->fRot  = MapGeomLoopValue(pKazHead->fRot,
                                       MAPSP_DEG_MIN, MAPSP_DEG_MAX);   /* 285 */

    fRot = MapGeomDegToRad(pKazHead->fRot) * 180.0f / MAPSP_PI;         /* 287 */

    _SetVector(vRot, pKazHead->pfMstRot[0], pKazHead->pfMstRot[1],
               fRot, 0.0f);                                             /* 290 */

    mat = MapPutGetMatrixPtr(pKazHead->pHdl);                           /* 291 */
    sceVu0UnitMatrix(*mat);                                             /* 292 */
    MapGeomSetScaleMatrix(*mat, MAPOBJ_SCALE, -MAPOBJ_SCALE,
                                -MAPOBJ_SCALE);                         /* 293 */

    sceVu0RotMatrixZ(*mat, *mat,  (vRot[2] * MAPOBJ_DEG2RAD));          /* 296 */
    sceVu0RotMatrixX(*mat, *mat, -(vRot[0] * MAPOBJ_DEG2RAD));          /* 297 */
    sceVu0RotMatrixY(*mat, *mat, -(vRot[1] * MAPOBJ_DEG2RAD));          /* 298 */

    g3dxVu0CopyVector((*mat)[3], pKazHead->vPos);                       /* 300 */
}                                                                       /* 301 */

/* ==========================================================================
 *  MapSpKazProc                                    MapSpFuncList[0]
 * ======================================================================== */
int MapSpKazProc(void)                                                  /* 304 */
{                                                                       /* 305 */
    int i;
    int cnt = 0;

    MapSpKazAnim();                                                     /* 310 */

    for (i = 0; i < MAPSP_KAZ_NUM; i++)                                 /* 312 */
    {
        if (MapSpKazList[i].pHdl != nullptr)                            /* 314 */
        {
            cnt++;                                                      /* 315 */
            MapSpKazSetMatrix(&MapSpKazList[i]);                        /* 316 */
        }
    }                                                                   /* 318 */

    if (cnt == 0)                                                       /* 321 */
    {
        MapSpFuncList[0] = nullptr;                                     /* 322 */
    }

    if (MapSpDbDat.iSave != 0)                                          /* 328 */
    {
        DbmSave(&dbg_kaza_main, "host0:../src/ingame/map/", "MapSpDat.h", "MAPSP_");                                /* 331 */
        MapSpDbDat.iSave = 0;                                           /* 332 */
    }

    return 0;                                                           /* 336 */
}                                                                       /* 337 */

/* ==========================================================================
 *  MapSpMoviRegistObj
 * ======================================================================== */
static void MapSpMoviRegistObj(void *pHdl, int iFlg)                    /* 353 */
{                                                                       /* 354 */
    MapSpMoviHdl = pHdl;                                                /* 357 */
    MapSpMoviRot = 0.0f;                                                /* 358 */
    MapSpMoveFlg = iFlg;                                                /* 359 */

    g3dxVu0CopyVector(MapSpMoviPos, (*MapPutGetMatrixPtr(pHdl))[3]);    /* 361 */
}                                                                       /* 362 */

float *MapSpGetReelPos(void)                                            /* 366 */
{                                                                       /* 367 */
    return MapSpMoviPos;                                                /* 368 */
}

void MapSpMoviSetFlg(int iOn_Off)                                       /* 371 */
{
    MapSpMoviFlg = iOn_Off;                                             /* 372 */
}

/* ==========================================================================
 *  MapSpMoviProc
 *
 *  The reel is the one special object with no job slot of its own -- nothing
 *  installs it into MapSpFuncList[], so this only runs while something else
 *  calls it.  MapSpMoviSetFlg() is the on/off the projector event drives.
 * ======================================================================== */
int MapSpMoviProc(void)                                                 /* 376 */
{                                                                       /* 377 */
    float fRot;
    float vRot[4];
    /* rodata 39f630 -- a local aggregate initialiser, not a static table:
     * the ROM copies 16 bytes onto the stack at the head of the scope. */
    float vScale[4] = { 1.0f, 1.0f, 1.0f, 1.0f };                       /* 380 */

    if (MapSpMoviHdl == nullptr || MapSpMoviFlg == 0)                   /* 382 */
    {
        return 0;                                                       /* 383 */
    }

    MapSpMoviRot = MapGeomLoopValue(MapSpMoviRot + MapSpDbDat.fReelSpeed,
                                    MAPSP_DEG_MIN, MAPSP_DEG_MAX);      /* 386 */

    fRot = MapGeomDegToRad(MapSpMoviRot) * 180.0f / MAPSP_PI;           /* 390 */

    if (MapSpMoveFlg != 0)                                              /* 392 */
    {
        _SetVector(vRot, fRot, 0.0f, 0.0f, 0.0f);                       /* 393 */
    }
    else
    {
        _SetVector(vRot, 0.0f, 0.0f, fRot, 0.0f);                       /* 395 */
    }

    MapObjSetPutMatrix(*MapPutGetMatrixPtr(MapSpMoviHdl), MapSpMoviPos,
                       vRot, vScale[0], vScale[1], vScale[2]);          /* 398 */

    return 0;                                                           /* 400 */
}                                                                       /* 401 */

/* ==========================================================================
 *  MapSpAraCheck
 *
 *  The whole body is attributed to variable.h 167 -- CVariable::Get() moved the
 *  line and the comparison inherited it, so the return statement leaves no
 *  MapSp.c $LM of its own.
 * ======================================================================== */
int MapSpAraCheck(void)                                                 /* 408 */
{                                                                       /* 409 */
    return (ingame_wrk.mChapterNo.Get() >= 7);                          /* 410 */
}                                                                       /* 411 */

/* ==========================================================================
 *  MapSpKageProc                                   MapSpFuncList[1]
 *
 *  gra3dSetObjectIdDrawNoShadow(-1) means "no object is exempt"; passing the
 *  shadow-suppression id of 1 exempts this one while its action plays.
 *
 *  ROM ORDER, kept: the NULL test at 423 clears the job slot but does not
 *  return, so the very next line dereferences the pointer it just found NULL.
 *  On the EE that reads whatever sits at 0x54; on the host it would fault.  It
 *  is unreachable in practice -- MapSpObjRelease() and MapSpInit() clear the
 *  pointer and the slot together, so the job can only run with a live pointer.
 *  Same shape as MapObjRegistFurn()'s early MapObjUpdateFlg() call; do not
 *  "fix" it without deciding that deviation deliberately.
 * ======================================================================== */
int MapSpKageProc(void)                                                 /* 421 */
{                                                                       /* 422 */
    if (MapSpKageObjPtr == nullptr)                                     /* 423 */
    {
        MapSpFuncList[1] = nullptr;                                     /* 424 */
    }

    if (MapSpKageObjPtr->Action > 0)                                    /* 428 */
    {
        if ((u_int)(MapSpKageObjPtr->ActionType + 1) < 2)               /* 429 */
        {
            gra3dSetObjectIdDrawNoShadow(-1);                           /* 433 */
            return 0;                                                   /* 434 */
        }
    }

    gra3dSetObjectIdDrawNoShadow(1);                                    /* 438 */

    return 0;                                                           /* 439 */
}                                                                       /* 440 */

/* ==========================================================================
 *  MapSpGetActionStep
 *
 *  ROM lines 449..452, a header-less static inline: it leaves line notes but no
 *  symbol, so the name is this port's.  The screen encodes the closing action
 *  as the complement of the opening one, and this folds the two onto the same
 *  step numbers -- ~(-1) is 0, ~(-2) is 1, ~(-3) is 2.
 * ======================================================================== */
static inline int MapSpGetActionStep(const MDAT_OBJ *pObjDat)
{
    int iType = pObjDat->ActionType;                                    /* 450 */

    if (iType < 0) { iType = ~iType; }                                  /* 451 */

    return iType;
}

static void MapSpFusumaEnd(int iGroupID)                                /* 456 */
{                                                                       /* 457 */
    if (iGroupID == MapSpFusumaBuffID)                                  /* 458 */
    {
        MapSpFusumaPtr    = nullptr;                                    /* 459 */
        MapSpFusumaBuffID = -1;                                         /* 460 */
        MapSpFuncList[2]  = nullptr;                                    /* 461 */
    }
}                                                                       /* 462 */

/* ==========================================================================
 *  MapSpFusumaProc                                 MapSpFuncList[2]
 *
 *  Steps 1 and 2 of the screen's action get a fresh per-object light each
 *  frame, because the screen sweeps across the room's light gradient while it
 *  slides; step 2 is the last one, so the job releases itself there.
 *
 *  Carries the same null-test-then-dereference as MapSpKageProc(); see there.
 * ======================================================================== */
int MapSpFusumaProc(void)                                               /* 466 */
{                                                                       /* 467 */
    MDAT_OBJ *pObjDat;

    if (MapSpFusumaPtr == nullptr)                                      /* 470 */
    {
        MapSpFuncList[2] = nullptr;                                     /* 471 */
    }

    pObjDat = (MDAT_OBJ *)MapSpFusumaPtr->obj_ptr;                      /* 474 */

    if (pObjDat->Action > 0)                                            /* 477 */
    {
        /* Written twice in the ROM and CSE'd into one register -- the step is
         * not a source local, there is no stab for it. */
        if ((u_int)(MapSpGetActionStep(pObjDat) - 1) < 2)               /* 480 */
        {
            MapLightPreRenderObj(MapSpFusumaPtr->obj_hdl,
                                 MapSpFusumaBuffID);                    /* 484 */

            if (MapSpGetActionStep(pObjDat) == 2)                       /* 486 */
            {
                MapSpFusumaEnd(MapSpFusumaBuffID);                      /* 488 */
            }
        }
    }

    return 0;                                                           /* 492 */
}                                                                       /* 493 */

/* ==========================================================================
 *  MapSpObjReg
 *
 *  Called from MapObjRegistFurn() for every placed model.  FurnCtlGetID()
 *  returns the three-digit number out of the model name, so these cases are
 *  model numbers shared between the f- and d- key spaces.
 *
 *  The ROM compiles the switch as a binary-search if-tree; the case order below
 *  is the source order, recovered from the line numbers rather than from the
 *  branch layout.
 * ======================================================================== */
void MapSpObjReg(int iGroup, char *sName, MAPOBJ_DAT *pMObjDat, float *pfRot)
                                                                        /* 502 */
{                                                                       /* 503 */
    void *pHdl = pMObjDat->obj_hdl;                                     /* 504 */

    if (pHdl == nullptr) { return; }                                    /* 505 */

    /* Attribute 1 is cloth: pushed well forward so it never z-fights the
     * surface it hangs on. */
    if (FurnLoadGetAttr(sName) == 1)                                    /* 508 */
    {
        MapPutSetZoffset(pHdl, MAPSP_ZOFFSET);                          /* 509 */
    }

    switch (FurnCtlGetID(sName))                                        /* 512 */
    {
    case 9:             /* mosquito net */
        MapPutSetZoffset(pHdl, MAPSP_ZOFFSET);                          /* 514 */
        MapPutChangeFunc(pHdl, MapSpKayaDrawCallback);                  /* 516 */
        break;                                                          /* 517 */

    case 20:
        MapPutSetFirst(pHdl, 100);                                      /* 520 */
        break;                                                          /* 521 */

    case 28:            /* sliding screen */
    case 330:
        MapSpFusumaPtr    = pMObjDat;                                   /* 525 */
        MapSpFusumaBuffID = iGroup;                                     /* 526 */
        MapSpFuncList[2]  = MapSpFusumaProc;                            /* 527 */
        break;                                                          /* 528 */

    case 262:           /* shadow object */
        MapSpKageObjPtr  = (MDAT_OBJ *)pMObjDat->obj_ptr;               /* 531 */
        MapSpKageBuffID  = iGroup;                                      /* 532 */
        MapSpFuncList[1] = MapSpKageProc;                               /* 533 */
        break;                                                          /* 534 */

    case 24:
    case 302:
    case 362:
        MapPutSetZoffset(pHdl, MAPSP_ZOFFSET);                          /* 539 */
        break;                                                          /* 540 */

    case 329:
        /* Clears bits 5, 11 and 12 of the draw flags and hands the object the
         * room's own light block instead of its baked one. */
        *(u_int *)MapPutGetFlgPtr(pHdl) &= ~0x1820u;                    /* 545 */
        MapPutSetLitPtr(pHdl, MapDrawGetLightPtr4BuffID(iGroup));       /* 546 */
        break;                                                          /* 547 */

    case 705:
    case 706:
        MapPutSetFirst(pHdl, 100);                                      /* 553 */
        break;

    case 405:           /* wind-blown charm */
        MapSpKazRegistObj(iGroup, pHdl, GetRandValI(MAPSP_SPEED_NUM),
                          pfRot);                                       /* 557 */
        MapSpFuncList[0] = MapSpKazProc;                                /* 559 */
        break;                                                          /* 560 */

    case 218:           /* projector reel, spun about X */
        MapSpMoviRegistObj(pHdl, 1);                                    /* 562 */
        break;                                                          /* 563 */

    case 236:           /* projector reel, spun about Z */
        MapSpMoviRegistObj(pHdl, 0);                                    /* 565 */
        /* fall through -- the reel also wants the flag below */

    case 527:
    case 528:
        *(u_int *)MapPutGetFlgPtr(pHdl) |= 0x800u;                      /* 568 */
        break;

    default:
        break;
    }
}                                                                       /* 571 */

void MapSpObjRelease(int iGroupID)                                      /* 574 */
{                                                                       /* 575 */
    MapSpKazDeleteGroup(iGroupID);                                      /* 577 */

    MapSpFusumaEnd(iGroupID);                                           /* 580 */

    if (MapSpKageBuffID == iGroupID)                                    /* 583 */
    {
        MapSpKageObjPtr  = nullptr;                                     /* 584 */
        MapSpKageBuffID  = -1;                                          /* 585 */
        MapSpFuncList[1] = nullptr;                                     /* 586 */
    }
}                                                                       /* 587 */

void MapSpProc(void)                                                    /* 592 */
{                                                                       /* 593 */
    int i;

    for (i = 0; i < MAPSP_FUNC_NUM; i++)                                /* 596 */
    {
        if (MapSpFuncList[i] == nullptr) continue;                      /* 597 */

        MapSpFuncList[i]();                                             /* 599 */
    }                                                                   /* 600 */
}                                                                       /* 601 */

void MapSpInit(void)                                                    /* 604 */
{                                                                       /* 605 */
    int i;

    for (i = 0; i < MAPSP_FUNC_NUM; i++)                                /* 609 */
    {
        MapSpFuncList[i] = nullptr;                                     /* 610 */
    }                                                                   /* 611 */

    MapSpKazDeleteAll();                                                /* 613 */

    /* The screen's pointer and buffer id are deliberately not reset here --
     * only MapSpFusumaEnd() clears those, and it is reached through
     * MapSpObjRelease(). */
    MapSpKageObjPtr = nullptr;                                          /* 616 */
    MapSpKageBuffID = -1;                                               /* 617 */
}                                                                       /* 618 */
