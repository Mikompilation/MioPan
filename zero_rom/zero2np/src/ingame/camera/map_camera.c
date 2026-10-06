/* ==========================================================================
 *  ingame/camera/map_camera.c
 *
 *  Story-mode camera controller.
 *
 *  Reconstructed from the Feb 6 2004 prototype (SLES_523.84), map_camera.o
 *  0x001d9260..0x001db7d7.  Trailing /-* NNN *-/ comments are the original
 *  source line numbers recovered from the ROM's line table.
 *
 *  CameraMain() picks exactly one of five drivers per frame:
 *
 *    pFinCamTarget set     -> keep re-aiming at the pinned target
 *    plyr_wrk.mode == 5/6  -> finder_camera.c
 *    app_camera.mode != 0  -> PlyrApproachCameraCtrl()  (approach/talk/death)
 *    otherwise             -> MapCamNormalCameraCtrl()  (event / map data)
 *
 *  The map-data path itself splits again on the rectangle the player stands
 *  in: type 9 rectangles carry a four-corner "special" camera (MDAT_CAM_SP)
 *  that is bilinearly blended across the rectangle, everything else uses the
 *  single MDAT_CAM record and one of six follow modes.
 * ======================================================================== */

#include "map_camera.h"

#include "event_camera.h"
#include "finder_camera.h"

#include "../../common/utility.h"
#include "../../common/variable.h"
#include "../../graphics/effect/effect.h"
#include "../../graphics/effect/effect_sub.h"
#include "../../graphics/graph3d/g3ddbg.h"
#include "../../graphics/graph3d/gra3d.h"
#include "../../ingame/enemy/enemy.h"
#include "../../ingame/enemy/fene_entry.h"
#include "../../ingame/map/MapLoad.h"
#include "../../ingame/map/RegDat.h"
#include "../../ingame/map/map_rectangle.h"
#include "../../ingame/plyr/player.h"
#include "../../ingame/plyr/unit_ctl.h"
#include "../../system/eeiop/cddat.h"
#include "../../system/eeiop/snd_util.h"
#include "../../system/os/system.h"
#include "../../system/pad/pad.h"

#include <libvu0.h>
#include <stdlib.h>
#include <string.h>
#include "../../graphics/graph3d/g3dxVu0.h" /* g3dxVu0CopyVector */

/* Effect id the overlap (cross-fade) handler is registered under.  The ROM
 * has an enum for this in effect.h; only this one member is needed here. */

/* plyr_wrk.cmn_wrk.st.sta -- set while the player is engaged in battle.  The
 * type-8 (battle) camera rectangles are only honoured in that state. */
#define PLST_BATTLE 0x20

/* Player mode values CameraMain() dispatches on. */
#define PLMODE_FINDER_IN 5
#define PLMODE_FINDER    6

/* Frames the approach camera takes to slide into place, NTSC.  PAL runs at
 * 50Hz, so the same wall-clock duration is 20 * 50/60 frames. */
#define APPROACH_MOVE_FRAME_NTSC 20.0f
#define APPROACH_MOVE_FRAME_PAL  16.666666f

typedef struct                      /* 0x24 */
{
    /* 0x00 */ int EventCamFlg;      /* event_camera.c owns the camera        */
    /* 0x04 */ int SpecialCamFlg;    /* last frame used an MDAT_CAM_SP        */
    /* 0x08 */ int RoomNo;           /* room the offset vector comes from     */
    /* 0x0c */ const float (*pFinCamTarget)[3];
    /* 0x10 */ void *pRectStat;      /* MDAT_CAM* or MDAT_CAM_SP*, see below  */
    /* 0x14 */ int CamDataType;      /* rectangle type in force, -1 = none    */
    /* 0x18 */ int BattleCamAccept;  /* player has left the battle rectangle  */
    /* 0x1c */ int RectCamLastTime;  /* the rectangle path ran last frame     */
    /* 0x20 */ int CamTarget;        /* 0 = player, else sister               */
} MAP_CAMERA_CTRL;

typedef struct                      /* 0x14 */
{
    /* 0x00 */ float Power[2];       /* [0] position shake, [1] target shake  */
    /* 0x08 */ int   Counter;
    /* 0x0c */ int   AllTime;
    /* 0x10 */ short RequestFlg;
    /* 0x12 */ short LoopNum;
} QUAKE_CAMERA_CTRL;

typedef struct                      /* 0xb0 */
{
    /* 0x00 */ int   mode;           /* MAP_CAM_MODE                          */
    /* 0x04 */ int   crossfade;
    /* 0x08 */ float cnt;
    /* 0x0c */ int   flow;           /* APPROACH_CAMERA_FLOW                  */
    /* 0x10 */ float offy;
    /* 0x14 */ float dist;
    /* 0x18 */ float offy2;
    /* 0x20 */ float ipos[4];        /* point of interest                     */
    /* 0x30 */ float pmv[4];         /* per-frame position delta              */
    /* 0x40 */ float npos[4];        /* current camera position               */
    /* 0x50 */ float imv[4];         /* per-frame target delta                */
    /* 0x60 */ float inpos[4];       /* current camera target                 */
    /* 0x70 */ float trot[4];
    /* 0x80 */ float NeckInitVector[4];
    /* 0x90 */ float PosInitVector[4];
    /* 0xa0 */ ENE_WRK *pEneWrk;
} APPROACH_CAMERA;

/* sdata 3f1a60 / 3f1a62 */
u_short fior_tm;
u_short ori_fior_tm;

/* bss 4b4610 / 4b4640 / 4b46f0 */
static MAP_CAMERA_CTRL   map_camera_ctrl;
static APPROACH_CAMERA   app_camera;
static QUAKE_CAMERA_CTRL QuakeCameraCtrl;

static void MapCamNormalCameraCtrl(int RectCamFlg);
static void MapCamSpCameraCtrl(MDAT_CAM_SP *pCamSp, MB_OUT_RECT *pRect, float *Position);
static void MapCamSpCameraDiagonal(MDAT_CAM_SP *pCamSp, MB_OUT_RECT *pRect, float *Position);
static void MapCamSpCameraParallel(MDAT_CAM_SP *pCamSp, MB_OUT_RECT *pRect, float *Position);
static void MapCamGetPerpendicularNode(float *Node, float *Line0, float *Line1, float *Point);
static void MapCamRectangleCameraCtrl(PLCMN_WRK *pPlayerCmnWrk, int DataType, int RectCamFlg);
static void MapCamSetFovRoll(void);
static void MapCamTypeFix(void);
static void MapCamTypePositionFollow(int init);
static void MapCamTypeTargetFollow(int init);
static void MapCamTypePositionTargetFollow(int init);
static void MapCamTypeTargetFollowYFix(int init);
static void MapCamTypePositionFollowYFix(int init);
static void FinderInOverRapCtrl(void);
static int  MapCamCalcFollowPointWithMargin(float *NextPos, const float *TargetPos,
                                            const float *NowPos, float margin);
static void MapCamUpdateRoomNo(MAP_CAMERA_CTRL *pMcCtrl);
static void PlyrApproachCameraCtrl(int RectCamFlg);
static int  PlyrShoulderCameraTypeGet(int ModelNo);
static void PlyrShoulderCameraPositionGet(float *Position, int Type);
static void PlyrShoulderCameraSet(float *Position, float *Target);
static void QuakeCameraInit(void);
static int  QuakeCameraMain(void);


void CameraMain(void)                                                   /* 136 */
{
    MAP_CAMERA_CTRL *pMcCtrl = &map_camera_ctrl;                        /* 137 */
    int RectCamFlg = pMcCtrl->RectCamLastTime;                          /* 138 */

    pMcCtrl->RectCamLastTime = 0;                                       /* 139 */

    if (pMcCtrl->pFinCamTarget != 0)                                    /* 142 */
    {
        gra3dcamSetTarget((float *)*pMcCtrl->pFinCamTarget, 1);         /* 143 */
        gra3dApplyCamera(0, 1);                                         /* 144 */
    }
    else
    {
        if (plyr_wrk.cmn_wrk.mode == PLMODE_FINDER_IN)                  /* 148 */
        {
            FinderInCameraCtrl();                                       /* 151 */
        }
        else if (plyr_wrk.cmn_wrk.mode == PLMODE_FINDER)                /* 153 */
        {
            FinderModeCameraCtrl();                                     /* 155 */
        }
        else
        {
            if (app_camera.mode != MAP_CAM_MODE_NORMAL)                 /* 159 */
            {
                PlyrApproachCameraCtrl(RectCamFlg);                     /* 160 */
            }
            else
            {
                MapCamNormalCameraCtrl(RectCamFlg);                     /* 162 */
            }
        }
    }

    if (QuakeCameraCtrl.RequestFlg != 0)                                /* 168 */
    {
        QuakeCameraMain();                                              /* 169 */
    }

    FinderInOverRapCtrl();                                              /* 173 */
}

void CameraMainInit(void)                                               /* 188 */
{
    MAP_CAMERA_CTRL *pMcCtrl = &map_camera_ctrl;                        /* 189 */

    pMcCtrl->EventCamFlg     = 0;                                       /* 191 */
    pMcCtrl->SpecialCamFlg   = 0;                                       /* 192 */
    pMcCtrl->RoomNo          = -1;                                      /* 193 */
    pMcCtrl->pFinCamTarget   = 0;                                       /* 194 */
    pMcCtrl->pRectStat       = 0;                                       /* 195 */
    pMcCtrl->CamDataType     = -1;                                      /* 196 */
    pMcCtrl->BattleCamAccept = 0;                                       /* 197 */
    pMcCtrl->RectCamLastTime = 0;                                       /* 198 */
    pMcCtrl->CamTarget       = 0;                                       /* 199 */

    memset(&app_camera, 0, sizeof(APPROACH_CAMERA));                    /* 201 */
    QuakeCameraInit();                                                  /* 202 */
}

void MapCamSetEventCameraFlg(int Flg)                                   /* 212 */
{
    MAP_CAMERA_CTRL *pMcCtrl = &map_camera_ctrl;                        /* 213 */

    if (Flg != 0)                                                       /* 215 */
    {
        pMcCtrl->EventCamFlg = 1;                                       /* 216 */
    }
    else
    {
        pMcCtrl->EventCamFlg = 0;                                       /* 219 */
        EventCameraInitCtrlReq();                                       /* 220 */
    }
}

int MapCamGetEventCameraFlg(void)                                       /* 231 */
{
    MAP_CAMERA_CTRL *pMcCtrl = &map_camera_ctrl;                        /* 232 */

    return pMcCtrl->EventCamFlg;                                        /* 234 */
}

/* Map-data camera.  Decides which rectangle type the followed unit is in and
 * hands off to either the four-corner special camera or the ordinary
 * rectangle camera.  RectCamFlg says the rectangle path also ran last frame,
 * which suppresses the snap-to-position that a fresh entry would do. */
static void MapCamNormalCameraCtrl(int RectCamFlg)                      /* 243 */
{
    MAP_CAMERA_CTRL *pMcCtrl = &map_camera_ctrl;                        /* 244 */
    MDAT_CAM_SP *pCamSp;
    MB_OUT_RECT *pRect;
    PLCMN_WRK *pPlayerCmnWrk;
    int GetCamDataType;

    if (pMcCtrl->EventCamFlg != 0)                                      /* 246 */
    {
        if (EventCameraMain() != 0)                                     /* 248 */
        {
            pMcCtrl->EventCamFlg = 0;                                   /* 249 */
        }
    }
    else
    {
        GetCamDataType = -1;                                            /* 255 */

        pPlayerCmnWrk = (pMcCtrl->CamTarget == 0)                       /* 258 */
                            ? &plyr_wrk.cmn_wrk
                            : &sis_wrk.cmn_wrk;

        if ((plyr_wrk.cmn_wrk.st.sta & PLST_BATTLE) != 0)               /* 260 */
        {
            /* Battle rectangles only take over once the player has stepped
             * out of one and back in -- otherwise entering battle inside one
             * would snap the camera immediately. */
            if (MrecCheckHitRect(pPlayerCmnWrk->floor,                  /* 263 */
                                 pPlayerCmnWrk->mbox.pos, 8) != 0)
            {
                if (pMcCtrl->BattleCamAccept != 0)                      /* 265 */
                {
                    GetCamDataType = 8;                                 /* 266 */
                }
            }
            else
            {
                pMcCtrl->BattleCamAccept = 1;                           /* 271 */
            }
        }
        else
        {
            pMcCtrl->BattleCamAccept = 0;                               /* 275 */
        }

        if (GetCamDataType == -1)                                       /* 278 */
        {
            if (pMcCtrl->CamDataType != -1 &&                           /* 281 */
                MrecCheckOnSameCamRect(pMcCtrl->pRectStat, pPlayerCmnWrk->floor,
                                       pPlayerCmnWrk->mbox.pos,
                                       pMcCtrl->CamDataType) != 0)
            {
                GetCamDataType = pMcCtrl->CamDataType;                  /* 283 */
            }
            else
            {
                /* The camera cut is what re-seeds the floating ghosts. */
                fene_entry.CamChangeFlg(1);               /* 287 */

                GetCamDataType = MrecCheckHitRect(pPlayerCmnWrk->floor, /* 290 */
                                                  pPlayerCmnWrk->mbox.pos, 9)
                                     ? 9
                                     : 1;
            }
        }

        if (GetCamDataType == 9)                                        /* 299 */
        {
            if (pMcCtrl->SpecialCamFlg == 0)                            /* 300 */
            {
                MapCamUpdateRoomNo(pMcCtrl);                            /* 301 */
            }
            pMcCtrl->SpecialCamFlg = 1;                                 /* 303 */

            pCamSp = MrecGetCameraSpInfo(&pRect, pMcCtrl->pRectStat,    /* 306 */
                                         pPlayerCmnWrk->floor,
                                         pPlayerCmnWrk->mbox.pos);
            if (pCamSp == 0 || pRect == 0)                              /* 307 */
            {
                G3DWARNING(pRect, "");                                  /* 308 */
                G3DWARNING(pCamSp, "");                                 /* 309 */
                return;                                                 /* 310 */
            }
            MapCamSpCameraCtrl(pCamSp, pRect, pPlayerCmnWrk->mbox.pos); /* 312 */
            pMcCtrl->pRectStat = pCamSp;                                /* 313 */
        }
        else
        {
            if (pMcCtrl->SpecialCamFlg == 1)                            /* 316 */
            {
                MapCamUpdateRoomNo(pMcCtrl);                            /* 317 */
            }

            pMcCtrl->SpecialCamFlg = 0;                                 /* 320 */

            MapCamRectangleCameraCtrl(pPlayerCmnWrk, GetCamDataType,    /* 322 */
                                      RectCamFlg);
            pMcCtrl->pRectStat = MrecGetCameraInfo();                   /* 323 */
        }

        pMcCtrl->CamDataType = GetCamDataType;                          /* 325 */
    }
}

static void MapCamSpCameraCtrl(MDAT_CAM_SP *pCamSp, MB_OUT_RECT *pRect, /* 330 */
                               float *Position)
{
    MAP_CAMERA_CTRL *pMcCtrl = &map_camera_ctrl;                        /* 331 */

    if (pCamSp != 0 && pRect != 0)                                      /* 333 */
    {
        MapCamUpdateRoomNo(pMcCtrl);                                    /* 335 */

        if (pCamSp->type == 0)                                          /* 338 */
        {
            MapCamSpCameraParallel(pCamSp, pRect, Position);            /* 339 */
        }
        else
        {
            MapCamSpCameraDiagonal(pCamSp, pRect, Position);            /* 342 */
        }
    }
}

/* Diagonal blend: the two rectangle diagonals (P0-P2, P1-P3) are each
 * parameterised by the perpendicular foot of the followed unit, giving four
 * corner weights that sum to two -- hence the 0.5 at the end. */
static void MapCamSpCameraDiagonal(MDAT_CAM_SP *pCamSp,                 /* 357 */
                                   MB_OUT_RECT *pRect, float *Position)
{
    MAP_CAMERA_CTRL *pMcCtrl = &map_camera_ctrl;                        /* 358 */
    float CamPosition[4];
    float CamTarget[4];
    float Node0[4];
    float Node1[4];
    float Point0[4];
    float Point1[4];
    float Point2[4];
    float Point3[4];
    float Length02;
    float Length13;
    float Length0N0;
    float Length1N1;
    float Influence0;
    float Influence1;
    float Influence2;
    float Influence3;

    if (pCamSp == 0)                                                    /* 367 */
    {
        return;
    }
    if (pRect == 0)                                                     /* 368 */
    {
        return;
    }

    Point0[0] = pRect->vec[0][0]; Point0[1] = 0.0f; Point0[2] = pRect->vec[0][2]; Point0[3] = 1.0f;   /* 372 */
    Point1[0] = pRect->vec[1][0]; Point1[1] = 0.0f; Point1[2] = pRect->vec[1][2]; Point1[3] = 1.0f;   /* 373 */
    Point2[0] = pRect->vec[2][0]; Point2[1] = 0.0f; Point2[2] = pRect->vec[2][2]; Point2[3] = 1.0f;   /* 374 */
    Point3[0] = pRect->vec[3][0]; Point3[1] = 0.0f; Point3[2] = pRect->vec[3][2]; Point3[3] = 1.0f;   /* 375 */

    MapCamGetPerpendicularNode(Node0, Point0, Point2, Position);        /* 376 */
    MapCamGetPerpendicularNode(Node1, Point1, Point3, Position);        /* 377 */

    Length02 = GetDistV(Point0, Point2);                                /* 380 */
    if (Length02 != 0.0f)                                               /* 381 */
    {
        Length0N0  = GetDistV(Point0, Node0);                           /* 382 */
        Influence0 = (Length02 - Length0N0) / Length02;                 /* 383 */
        Influence2 = Length0N0 / Length02;                              /* 384 */
    }
    else
    {
        Influence0 = 1.0f;                                              /* 387 */
        Influence2 = 0.0f;                                              /* 388 */
    }

    Length13 = GetDistV(Point1, Point3);                                /* 391 */
    if (Length13 != 0.0f)                                               /* 392 */
    {
        Length1N1  = GetDistV(Point1, Node1);                           /* 393 */
        Influence1 = (Length13 - Length1N1) / Length13;                 /* 394 */
        Influence3 = Length1N1 / Length13;                              /* 395 */
    }
    else
    {
        Influence1 = 1.0f;                                              /* 398 */
        Influence3 = 0.0f;                                              /* 399 */
    }

    if (Influence0 < 0.0f) { Influence0 = 0.0f; }                       /* 401 */
    if (Influence1 < 0.0f) { Influence1 = 0.0f; }                       /* 402 */
    if (Influence2 < 0.0f) { Influence2 = 0.0f; }                       /* 403 */
    if (Influence3 < 0.0f) { Influence3 = 0.0f; }                       /* 404 */
    if (Influence0 > 1.0f) { Influence0 = 1.0f; }                       /* 405 */
    if (Influence1 > 1.0f) { Influence1 = 1.0f; }                       /* 406 */
    if (Influence2 > 1.0f) { Influence2 = 1.0f; }                       /* 407 */
    if (Influence3 > 1.0f) { Influence3 = 1.0f; }                       /* 408 */

    CamPosition[0] = (pCamSp->st[0].Cam_Pos_X * Influence0 + pCamSp->st[1].Cam_Pos_X * Influence1 +
                      pCamSp->st[2].Cam_Pos_X * Influence2 + pCamSp->st[3].Cam_Pos_X * Influence3) * 0.5f;  /* 412 */
    CamPosition[1] = (pCamSp->st[0].Cam_Pos_Y * Influence0 + pCamSp->st[1].Cam_Pos_Y * Influence1 +
                      pCamSp->st[2].Cam_Pos_Y * Influence2 + pCamSp->st[3].Cam_Pos_Y * Influence3) * 0.5f;  /* 414 */
    CamPosition[2] = (pCamSp->st[0].Cam_Pos_Z * Influence0 + pCamSp->st[1].Cam_Pos_Z * Influence1 +
                      pCamSp->st[2].Cam_Pos_Z * Influence2 + pCamSp->st[3].Cam_Pos_Z * Influence3) * 0.5f;  /* 416 */
    MapCamGra3dcamSetPositionAddOffset(CamPosition, pMcCtrl->RoomNo);   /* 417 */

    CamTarget[0] = (pCamSp->st[0].View_Pos_X * Influence0 + pCamSp->st[1].View_Pos_X * Influence1 +
                    pCamSp->st[2].View_Pos_X * Influence2 + pCamSp->st[3].View_Pos_X * Influence3) * 0.5f;  /* 421 */
    CamTarget[1] = (pCamSp->st[0].View_Pos_Y * Influence0 + pCamSp->st[1].View_Pos_Y * Influence1 +
                    pCamSp->st[2].View_Pos_Y * Influence2 + pCamSp->st[3].View_Pos_Y * Influence3) * 0.5f;  /* 423 */
    CamTarget[2] = (pCamSp->st[0].View_Pos_Z * Influence0 + pCamSp->st[1].View_Pos_Z * Influence1 +
                    pCamSp->st[2].View_Pos_Z * Influence2 + pCamSp->st[3].View_Pos_Z * Influence3) * 0.5f;  /* 425 */
    MapCamGra3dcamSetTargetAddOffset(CamTarget, pMcCtrl->RoomNo);       /* 426 */

    gra3dcamSetRoll((pCamSp->st[0].RotZ * Influence0 + pCamSp->st[1].RotZ * Influence1 +          /* 430 */
                     pCamSp->st[2].RotZ * Influence2 + pCamSp->st[3].RotZ * Influence3) * 0.5f); /* 431 */
    gra3dcamSetFov((pCamSp->st[0].Proj * Influence0 + pCamSp->st[1].Proj * Influence1 +           /* 435 */
                    pCamSp->st[2].Proj * Influence2 + pCamSp->st[3].Proj * Influence3) * 0.5f);  /* 436 */

    gra3dApplyCamera(0, 0);                                             /* 438 */
}

/* Parallel blend: the two adjacent edges (P0-P1, P1-P2) each give a 0..1
 * parameter, and the four corner weights are their bilinear products, so
 * they already sum to one. */
static void MapCamSpCameraParallel(MDAT_CAM_SP *pCamSp,                 /* 450 */
                                   MB_OUT_RECT *pRect, float *Position)
{
    MAP_CAMERA_CTRL *pMcCtrl = &map_camera_ctrl;                        /* 451 */
    float CamPosition[4];
    float CamTarget[4];
    float Node0[4];
    float Node1[4];
    float Point0[4];
    float Point1[4];
    float Point2[4];
    float Point3[4];
    float Length01;
    float Length12;
    float Length0N0;
    float Length1N1;
    float Influence01;
    float Influence23;
    float Influence03;
    float Influence12;
    float Influence0;
    float Influence1;
    float Influence2;
    float Influence3;

    G3DASSERT(pCamSp, "");                                              /* 462 */
    G3DASSERT(pRect, "");                                               /* 463 */

    Point0[0] = pRect->vec[0][0]; Point0[1] = 0.0f; Point0[2] = pRect->vec[0][2]; Point0[3] = 1.0f;   /* 465 */
    Point1[0] = pRect->vec[1][0]; Point1[1] = 0.0f; Point1[2] = pRect->vec[1][2]; Point1[3] = 1.0f;   /* 466 */
    Point2[0] = pRect->vec[2][0]; Point2[1] = 0.0f; Point2[2] = pRect->vec[2][2]; Point2[3] = 1.0f;   /* 467 */
    Point3[0] = pRect->vec[3][0]; Point3[1] = 0.0f; Point3[2] = pRect->vec[3][2]; Point3[3] = 1.0f;   /* 468 */

    MapCamGetPerpendicularNode(Node0, Point0, Point1, Position);        /* 469 */
    MapCamGetPerpendicularNode(Node1, Point1, Point2, Position);        /* 470 */

    Length01 = GetDistV(Point0, Point1);                                /* 473 */
    if (Length01 != 0.0f)                                               /* 474 */
    {
        Length0N0   = GetDistV(Point0, Node0);                          /* 475 */
        Influence03 = (Length01 - Length0N0) / Length01;                /* 476 */
        Influence12 = Length0N0 / Length01;                             /* 477 */
    }
    else
    {
        Influence03 = 1.0f;                                             /* 480 */
        Influence12 = 0.0f;                                             /* 481 */
    }

    Length12 = GetDistV(Point1, Point2);                                /* 484 */
    if (Length12 != 0.0f)                                               /* 485 */
    {
        Length1N1   = GetDistV(Point1, Node1);                          /* 486 */
        Influence01 = (Length12 - Length1N1) / Length12;                /* 487 */
        Influence23 = Length1N1 / Length12;                             /* 488 */
    }
    else
    {
        Influence01 = 1.0f;                                             /* 491 */
        Influence23 = 0.0f;                                             /* 492 */
    }

    Influence0 = Influence01 * Influence03;                             /* 495 */
    Influence1 = Influence01 * Influence12;                             /* 496 */
    Influence2 = Influence23 * Influence12;                             /* 497 */
    Influence3 = Influence23 * Influence03;                             /* 498 */

    CamPosition[0] = pCamSp->st[0].Cam_Pos_X * Influence0 + pCamSp->st[1].Cam_Pos_X * Influence1 +
                     pCamSp->st[2].Cam_Pos_X * Influence2 + pCamSp->st[3].Cam_Pos_X * Influence3;   /* 502 */
    CamPosition[1] = pCamSp->st[0].Cam_Pos_Y * Influence0 + pCamSp->st[1].Cam_Pos_Y * Influence1 +
                     pCamSp->st[2].Cam_Pos_Y * Influence2 + pCamSp->st[3].Cam_Pos_Y * Influence3;   /* 504 */
    CamPosition[2] = pCamSp->st[0].Cam_Pos_Z * Influence0 + pCamSp->st[1].Cam_Pos_Z * Influence1 +
                     pCamSp->st[2].Cam_Pos_Z * Influence2 + pCamSp->st[3].Cam_Pos_Z * Influence3;   /* 506 */
    MapCamGra3dcamSetPositionAddOffset(CamPosition, pMcCtrl->RoomNo);   /* 507 */

    CamTarget[0] = pCamSp->st[0].View_Pos_X * Influence0 + pCamSp->st[1].View_Pos_X * Influence1 +
                   pCamSp->st[2].View_Pos_X * Influence2 + pCamSp->st[3].View_Pos_X * Influence3;   /* 511 */
    CamTarget[1] = pCamSp->st[0].View_Pos_Y * Influence0 + pCamSp->st[1].View_Pos_Y * Influence1 +
                   pCamSp->st[2].View_Pos_Y * Influence2 + pCamSp->st[3].View_Pos_Y * Influence3;   /* 513 */
    CamTarget[2] = pCamSp->st[0].View_Pos_Z * Influence0 + pCamSp->st[1].View_Pos_Z * Influence1 +
                   pCamSp->st[2].View_Pos_Z * Influence2 + pCamSp->st[3].View_Pos_Z * Influence3;   /* 515 */
    MapCamGra3dcamSetTargetAddOffset(CamTarget, pMcCtrl->RoomNo);       /* 516 */

    gra3dcamSetRoll(pCamSp->st[0].RotZ * Influence0 + pCamSp->st[1].RotZ * Influence1 +   /* 520 */
                    pCamSp->st[2].RotZ * Influence2 + pCamSp->st[3].RotZ * Influence3);   /* 521 */
    gra3dcamSetFov(pCamSp->st[0].Proj * Influence0 + pCamSp->st[1].Proj * Influence1 +    /* 525 */
                   pCamSp->st[2].Proj * Influence2 + pCamSp->st[3].Proj * Influence3);    /* 526 */

    gra3dApplyCamera(0, 0);                                             /* 528 */
}

/* Foot of the perpendicular from Point onto the line Line0-Line1, in the XZ
 * plane.  A degenerate line collapses to Point itself. */
static void MapCamGetPerpendicularNode(float *Node, float *Line0,       /* 543 */
                                       float *Line1, float *Point)
{
    float LineX10 = Line1[0] - Line0[0];                                /* 545 */
    float LineY10 = Line1[2] - Line0[2];                                /* 546 */

    if (LineX10 != 0.0f || LineY10 != 0.0f)                             /* 547 */
    {
        Node[0] = (LineX10 * ((Point[0] - Line0[0]) * LineX10 + (Point[2] - Line0[2]) * LineY10)) /
                  (LineX10 * LineX10 + LineY10 * LineY10) + Line0[0];   /* 548 */
        Node[1] = 0.0f;                                                 /* 549 */
        Node[2] = (LineY10 * ((Point[0] - Line0[0]) * LineX10 + (Point[2] - Line0[2]) * LineY10)) /
                  (LineX10 * LineX10 + LineY10 * LineY10) + Line0[2];   /* 550 */
        Node[3] = 1.0f;                                                 /* 551 */
    }
    else
    {
        Node[0] = Point[0];                                             /* 556 */
        Node[1] = 0.0f;                                                 /* 557 */
        Node[2] = Point[2];                                             /* 558 */
        Node[3] = 1.0f;                                                 /* 559 */
    }
}

/* Ordinary (single MDAT_CAM) rectangle camera.  MrecSetCameraInfo() latches
 * the record for the rectangle the unit is standing in; its return says
 * whether that changed this frame, which together with RectCamFlg decides
 * whether the follow modes snap or ease. */
static void MapCamRectangleCameraCtrl(PLCMN_WRK *pPlayerCmnWrk,         /* 566 */
                                      int DataType, int RectCamFlg)
{
    MAP_CAMERA_CTRL *pMcCtrl = &map_camera_ctrl;                        /* 568 */
    int type;
    int ret;
    int NoCamera = 0;                                                   /* 572 */
    int InitFlg = 0;                                                    /* 573 */

    pMcCtrl->RectCamLastTime = 1;                                       /* 575 */

    ret = MrecSetCameraInfo(pPlayerCmnWrk->floor,                       /* 577 */
                            pPlayerCmnWrk->mbox.pos, DataType);
    if (ret == 1)                                                       /* 580 */
    {
        MapCamUpdateRoomNo(pMcCtrl);                                    /* 581 */
    }

    if (ret != 0 || RectCamFlg == 0)                                    /* 584 */
    {
        InitFlg = 1;                                                    /* 585 */
    }

    type = MrecGetCameraType();                                         /* 588 */
    switch (type)
    {
    case 0:
    case 4:
        MapCamTypeFix();                                                /* 593 */
        break;
    case 1:
    case 5:
        MapCamTypePositionFollow(InitFlg);                              /* 594 */
        break;
    case 2:
    case 6:
        MapCamTypeTargetFollow(InitFlg);                                /* 595 */
        break;
    case 3:
    case 7:
        MapCamTypePositionTargetFollow(InitFlg);                        /* 596 */
        break;
    case 8:
        MapCamTypePositionFollowYFix(InitFlg);                          /* 597 */
        break;
    case 9:
        MapCamTypeTargetFollowYFix(InitFlg);                            /* 598 */
        break;
    default:
        NoCamera = 1;                                                   /* 601 */
        break;
    }

    if (NoCamera == 0)                                                  /* 603 */
    {
        MapCamSetFovRoll();                                             /* 604 */
    }

    gra3dApplyCamera(0, 0);                                             /* 606 */
}

static void MapCamSetFovRoll(void)                                      /* 612 */
{
    float Roll;
    float Fov;

    if (MrecGetCameraRotZ(&Roll) != 0)                                  /* 617 */
    {
        gra3dcamSetRoll(Roll);                                          /* 618 */
    }

    if ((Fov = MrecGetCameraPrj()) > 0.0f)                              /* 620 */
    {
        gra3dcamSetFov(Fov);                                            /* 621 */
    }
}

static void MapCamTypeFix(void)                                         /* 629 */
{
    MAP_CAMERA_CTRL *pMcCtrl = &map_camera_ctrl;                        /* 631 */
    float Position[4];
    float Target[4];

    if (MrecGetCameraPos(Position) == 1)                                /* 635 */
    {
        MapCamGra3dcamSetPositionAddOffset(Position, pMcCtrl->RoomNo);  /* 639 */
    }

    if (MrecGetCameraInterest(Target) == 1)                             /* 641 */
    {
        MapCamGra3dcamSetTargetAddOffset(Target, pMcCtrl->RoomNo);      /* 645 */
    }
}

/* Camera position rides the followed unit (plus the record's offset); the
 * target is whatever the record says. */
static void MapCamTypePositionFollow(int init)                          /* 657 */
{
    MAP_CAMERA_CTRL *pMcCtrl = &map_camera_ctrl;                        /* 659 */
    float Position[4];
    float Target[4];
    float Offset[4];
    float margin;

    memset(Offset, 0, sizeof(Offset));                                  /* 662 */

    if (MapCamGetObjPosition(Position, 0) == 0)                         /* 664 */
    {
        float (&NowPosition)[4] = gra3dcamGetPosition();                /* 665 */

        if (init == 0)                                                  /* 668 */
        {
            if ((margin = MrecGetCameraAsobi()) >= 0.0f)                /* 669 */
            {
                MrecGetCameraPos(Offset);                               /* 670 */
                sceVu0AddVector(Position, Position, Offset);            /* 671 */
                MapCamGra3dcamSetPositionMargin(Position, NowPosition, margin);  /* 672 */
            }
        }
        else
        {
            MrecGetCameraPos(Offset);                                   /* 676 */
            sceVu0AddVector(Position, Position, Offset);                /* 677 */
            Position[3] = 1.0f;                                         /* 678 */
            gra3dcamSetPosition(Position);                              /* 679 */
        }
    }

    if (MrecGetCameraInterest(Target) == 1)                             /* 682 */
    {
        MapCamGra3dcamSetTargetAddOffset(Target, pMcCtrl->RoomNo);      /* 683 */
    }
}

/* Mirror of MapCamTypePositionFollow: fixed position, target follows. */
static void MapCamTypeTargetFollow(int init)                            /* 695 */
{
    MAP_CAMERA_CTRL *pMcCtrl = &map_camera_ctrl;                        /* 697 */
    float Position[4];
    float Target[4];
    float Offset[4];
    float margin;

    memset(Offset, 0, sizeof(Offset));                                  /* 700 */

    if (MrecGetCameraPos(Position) == 1)                                /* 702 */
    {
        MapCamGra3dcamSetPositionAddOffset(Position, pMcCtrl->RoomNo);  /* 703 */
    }

    if (MapCamGetObjPosition(Target, 0) == 0)                           /* 705 */
    {
        float (&NowTarget)[4] = gra3dcamGetTarget();                    /* 706 */

        if (init == 0)                                                  /* 709 */
        {
            if ((margin = MrecGetCameraAsobi()) >= 0.0f)                /* 710 */
            {
                MrecGetCameraInterest(Offset);                          /* 711 */
                sceVu0AddVector(Target, Target, Offset);                /* 712 */
                MapCamGra3dcamSetTargetMargin(Target, NowTarget, margin);        /* 713 */
            }
        }
        else
        {
            MrecGetCameraInterest(Offset);                              /* 718 */
            sceVu0AddVector(Target, Target, Offset);                    /* 719 */
            Target[3] = 1.0f;                                           /* 720 */
            gra3dcamSetTarget(Target, 1);                               /* 721 */
        }
    }
}

/* Both ends ride the followed unit -- a pure trailing camera. */
static void MapCamTypePositionTargetFollow(int init)                    /* 734 */
{
    float Position[4];
    float Target[4];
    float Offset[4];
    float margin;

    memset(Offset, 0, sizeof(Offset));                                  /* 738 */

    if (MapCamGetObjPosition(Position, 0) == 0)                         /* 740 */
    {
        float (&NowPosition)[4] = gra3dcamGetPosition();                /* 741 */

        if (init == 0)                                                  /* 744 */
        {
            if ((margin = MrecGetCameraAsobi()) >= 0.0f)                /* 745 */
            {
                MrecGetCameraPos(Offset);                               /* 746 */
                sceVu0AddVector(Position, Position, Offset);            /* 747 */
                MapCamGra3dcamSetPositionMargin(Position, NowPosition, margin);  /* 748 */
            }
        }
        else
        {
            MrecGetCameraPos(Offset);                                   /* 752 */
            sceVu0AddVector(Position, Position, Offset);                /* 753 */
            Position[3] = 1.0f;                                         /* 754 */
            gra3dcamSetPosition(Position);                              /* 755 */
        }
    }

    if (MapCamGetObjPosition(Target, 0) == 0)                           /* 758 */
    {
        float (&NowTarget)[4] = gra3dcamGetTarget();                    /* 759 */

        if (init == 0)                                                  /* 762 */
        {
            if ((margin = MrecGetCameraAsobi()) >= 0.0f)                /* 763 */
            {
                MrecGetCameraInterest(Offset);                          /* 764 */
                sceVu0AddVector(Target, Target, Offset);                /* 765 */
                MapCamGra3dcamSetTargetMargin(Target, NowTarget, margin);        /* 766 */
            }
        }
        else
        {
            MrecGetCameraInterest(Offset);                              /* 770 */
            sceVu0AddVector(Target, Target, Offset);                    /* 771 */
            Target[3] = 1.0f;                                           /* 772 */
            gra3dcamSetTarget(Target, 1);                               /* 773 */
        }
    }
}

/* As MapCamTypeTargetFollow, but the target keeps the height it already has,
 * so walking up or down stairs does not tilt the shot. */
static void MapCamTypeTargetFollowYFix(int init)                        /* 786 */
{
    MAP_CAMERA_CTRL *pMcCtrl = &map_camera_ctrl;                        /* 788 */
    float Position[4];
    float Target[4];
    float Offset[4];
    float margin;

    memset(Offset, 0, sizeof(Offset));                                  /* 791 */

    if (MrecGetCameraPos(Position) == 1)                                /* 793 */
    {
        MapCamGra3dcamSetPositionAddOffset(Position, pMcCtrl->RoomNo);  /* 794 */
    }

    if (MapCamGetObjPosition(Target, 0) == 0)                           /* 796 */
    {
        float (&NowTarget)[4] = gra3dcamGetTarget();                    /* 797 */

        if (init == 0)                                                  /* 800 */
        {
            if ((margin = MrecGetCameraAsobi()) >= 0.0f)                /* 801 */
            {
                MrecGetCameraInterest(Offset);                          /* 802 */
                sceVu0AddVector(Target, Target, Offset);                /* 803 */
                Target[1] = NowTarget[1];                               /* 804 */
                MapCamGra3dcamSetTargetMargin(Target, NowTarget, margin);        /* 805 */
            }
        }
        else
        {
            MrecGetCameraInterest(Offset);                              /* 810 */
            sceVu0AddVector(Target, Target, Offset);                    /* 811 */
            Target[3] = 1.0f;                                           /* 812 */
            gra3dcamSetTarget(Target, 1);                               /* 813 */
        }
    }
}

/* As MapCamTypePositionFollow, with the camera height pinned. */
static void MapCamTypePositionFollowYFix(int init)                      /* 826 */
{
    MAP_CAMERA_CTRL *pMcCtrl = &map_camera_ctrl;                        /* 828 */
    float Position[4];
    float Target[4];
    float Offset[4];
    float margin;

    memset(Offset, 0, sizeof(Offset));                                  /* 831 */

    if (MapCamGetObjPosition(Position, 0) == 0)                         /* 833 */
    {
        float (&NowPosition)[4] = gra3dcamGetPosition();                /* 834 */

        if (init == 0)                                                  /* 837 */
        {
            if ((margin = MrecGetCameraAsobi()) >= 0.0f)                /* 838 */
            {
                MrecGetCameraPos(Offset);                               /* 839 */
                sceVu0AddVector(Position, Position, Offset);            /* 840 */
                Position[1] = NowPosition[1];                           /* 841 */
                MapCamGra3dcamSetPositionMargin(Position, NowPosition, margin);  /* 842 */
            }
        }
        else
        {
            MrecGetCameraPos(Offset);                                   /* 846 */
            sceVu0AddVector(Position, Position, Offset);                /* 847 */
            Position[3] = 1.0f;                                         /* 848 */
            gra3dcamSetPosition(Position);                              /* 849 */
        }
    }

    if (MrecGetCameraInterest(Target) == 1)                             /* 852 */
    {
        MapCamGra3dcamSetTargetAddOffset(Target, pMcCtrl->RoomNo);      /* 853 */
    }
}

int MapCamGetObjPosition(float *pos, int obj_id)                        /* 869 */
{
    MAP_CAMERA_CTRL *pMcCtrl = &map_camera_ctrl;                        /* 870 */
    MOVE_BOX *pMoveBox;

    /* obj_id is accepted for symmetry with the event camera's object lookup;
     * the ROM ignores it too. */
    (void)obj_id;

    pMoveBox = (pMcCtrl->CamTarget == 0) ? &plyr_wrk.cmn_wrk.mbox        /* 874 */
                                         : &sis_wrk.cmn_wrk.mbox;

    g3dxVu0CopyVector(pos, pMoveBox->pos);                              /* 876 */

    return 0;                                                           /* 878 */
}

void ReqFinderInOverRap(u_short time)                                   /* 887 */
{
    ori_fior_tm = time;                                                 /* 888 */
    fior_tm     = time;                                                 /* 889 */
}

/* Bleeds the previous frame over the new one while the finder is being
 * raised; the strength argument is the original request length, so the
 * effect handler can ramp it. */
static void FinderInOverRapCtrl(void)                                   /* 893 */
{
    if (fior_tm != 0)                                                   /* 894 */
    {
        fior_tm--;                                                      /* 898 */
        SetEffects_OVERLAP(1, ori_fior_tm);                             /* 899 */
    }
}

/* NOTE: RoomNo is accepted but not used -- the ROM re-reads the player's
 * current area instead, so a camera placed while the room number is stale
 * still lands against the right origin. */
void MapCamGra3dcamSetPositionAddOffset(const float *Vector, int RoomNo)         /* 915 */
{
    float CamPos[4];
    float RoomCenter[4];

    (void)RoomNo;

    MapLoadGetOffsetVector(RoomCenter, GetPlyrAreaNo());                /* 916 */
    sceVu0AddVector(CamPos, RoomCenter, (float *)Vector);               /* 917 */

    CamPos[3] = 1.0f;                                                   /* 923 */
    gra3dcamSetPosition(CamPos);                                        /* 924 */
}

void MapCamGra3dcamSetTargetAddOffset(const float *Vector, int RoomNo)           /* 940 */
{
    float CamTarget[4];
    float RoomCenter[4];

    (void)RoomNo;

    MapLoadGetOffsetVector(RoomCenter, GetPlyrAreaNo());                /* 941 */
    sceVu0AddVector(CamTarget, RoomCenter, (float *)Vector);            /* 942 */

    CamTarget[3] = 1.0f;                                                /* 947 */
    gra3dcamSetTarget(CamTarget, 1);                                    /* 948 */
}

void MapCamGra3dcamSetPositionMargin(const float *Position,             /* 962 */
                                     const float *NowPosition, float margin)
{
    float DirVec[4];

    if (MapCamCalcFollowPointWithMargin(DirVec, Position, NowPosition, margin) != 0)  /* 965 */
    {
        gra3dcamSetPosition(DirVec);                                    /* 966 */
    }
    else
    {
        /* Inside the margin: re-set the position we already have, so the
         * gra3dApplyCamera() that follows still sees a fresh request. */
        gra3dcamSetPosition(gra3dcamGetPosition());                     /* 969 */
    }
}

void MapCamGra3dcamSetTargetMargin(const float *Target,                 /* 984 */
                                   const float *NowTarget, float margin)
{
    float DirVec[4];

    if (MapCamCalcFollowPointWithMargin(DirVec, Target, NowTarget, margin) != 0)      /* 987 */
    {
        gra3dcamSetTarget(DirVec, 1);                                   /* 988 */
    }
    else
    {
        gra3dcamSetTarget(gra3dcamGetTarget(), 1);                      /* 991 */
    }
}

/* Moves NowPos towards TargetPos, stopping `margin` short of it.  Returns 0
 * (and leaves NextPos untouched) when the two are already close enough. */
static int MapCamCalcFollowPointWithMargin(float *NextPos, const float *TargetPos,   /* 1013 */
                                           const float *NowPos, float margin)
{
    int ret = 0;                                                        /* 1014 */
    float length;

    length = Get2PLength(NowPos, TargetPos);                            /* 1018 */
    if (margin < length && length != 0.0f)                              /* 1019 */
    {
        sceVu0SubVector(NextPos, (float *)TargetPos, (float *)NowPos);  /* 1020 */
        sceVu0ScaleVector(NextPos, NextPos, (length - margin) / length);/* 1021 */
        sceVu0AddVector(NextPos, NextPos, (float *)NowPos);             /* 1022 */
        NextPos[3] = 1.0f;                                              /* 1023 */
        ret = 1;                                                        /* 1024 */
    }

    return ret;                                                         /* 1027 */
}

static void MapCamUpdateRoomNo(MAP_CAMERA_CTRL *pMcCtrl)                /* 1036 */
{
    pMcCtrl->RoomNo = GetPlyrAreaNo();                                  /* 1037 */
}

void MapCamTargetChange(int no)                                         /* 1044 */
{
    MAP_CAMERA_CTRL *pMcCtrl = &map_camera_ctrl;                        /* 1045 */

    pMcCtrl->CamTarget = no;                                            /* 1047 */
}

void MapCamSetFinCamera(const float (&rvPosition)[3],                   /* 1057 */
                        const float (*pvTarget)[3])
{
    MAP_CAMERA_CTRL *pMcCtrl = &map_camera_ctrl;                        /* 1058 */

    pMcCtrl->pFinCamTarget = pvTarget;                                  /* 1060 */

    gra3dcamSetPosition((float *)rvPosition);                           /* 1061 */
    gra3dcamSetTarget((float *)*pvTarget, 1);                           /* 1062 */
    gra3dApplyCamera(0, 1);                                             /* 1063 */
}

void MapCamCutFinCamera(void)                                           /* 1070 */
{
    MAP_CAMERA_CTRL *pMcCtrl = &map_camera_ctrl;                        /* 1071 */

    pMcCtrl->pFinCamTarget = 0;                                         /* 1073 */
}

void ReqPlyrApproachCameraCtrl(float *ipos, float offy, float dist)     /* 1093 */
{
    app_camera.mode = MAP_CAM_MODE_APPROACH;                            /* 1094 */
    app_camera.flow = APPROACH_CAMERA_FLOW_INIT;                        /* 1095 */
    app_camera.offy = offy;                                             /* 1096 */
    app_camera.dist = dist;                                             /* 1097 */
    g3dxVu0CopyVector(app_camera.ipos, ipos);                           /* 1098 */
    app_camera.pEneWrk = 0;                                             /* 1099 */
}

void ReqPlyrDamageCameraCtrl(float *ipos, float offy, float dist,       /* 1103 */
                             ENE_WRK *pEneWrk)
{
    app_camera.mode = MAP_CAM_MODE_APPROACH;                            /* 1104 */
    app_camera.flow = APPROACH_CAMERA_FLOW_INIT;                        /* 1105 */
    app_camera.offy = offy;                                             /* 1106 */
    app_camera.dist = dist;                                             /* 1107 */
    g3dxVu0CopyVector(app_camera.ipos, ipos);                           /* 1108 */
    app_camera.cnt = 0.0f;                                              /* 1109 */
    app_camera.pEneWrk = pEneWrk;                                       /* 1110 */
    g3dxVu0CopyVector(app_camera.NeckInitVector, pEneWrk->mpos.p0);     /* 1111 */
    PlyrShoulderCameraPositionGet(app_camera.PosInitVector,             /* 1112 */
                                  PlyrShoulderCameraTypeGet(pEneWrk->dat->cmn.mdl_no));
}

void ReqPlyrDeadCameraCtrl(float *ipos, float offy, float offy2,        /* 1116 */
                           float dist, float *trot)
{
    app_camera.mode  = MAP_CAM_MODE_APPROACH;                           /* 1117 */
    app_camera.flow  = APPROACH_CAMERA_FLOW_DEAD_INIT;                  /* 1118 */
    app_camera.offy  = offy;                                            /* 1119 */
    app_camera.offy2 = offy2;                                           /* 1120 */
    app_camera.dist  = dist;                                            /* 1121 */
    g3dxVu0CopyVector(app_camera.ipos, ipos);                           /* 1122 */
    g3dxVu0CopyVector(app_camera.trot, trot);                           /* 1123 */
}

void ReqPlyrTalkCameraCtrl(float offy, float dist)                      /* 1127 */
{
    float ipos[4];

    /* Frame the midpoint between the two heads. */
    GetCenterPoint(ipos, plyr_wrk.cmn_wrk.headpos, sis_wrk.cmn_wrk.headpos);    /* 1130 */

    app_camera.mode = MAP_CAM_MODE_APPROACH;                            /* 1132 */
    app_camera.flow = APPROACH_CAMERA_FLOW_TALK_INIT;                   /* 1133 */
    app_camera.offy = offy;                                             /* 1134 */
    app_camera.dist = dist;                                             /* 1135 */
    g3dxVu0CopyVector(app_camera.ipos, ipos);                           /* 1136 */
}

void EndPlyrApproachCameraCtrl(void)                                    /* 1142 */
{
    app_camera.mode = MAP_CAM_MODE_NORMAL;                              /* 1143 */
}

static void PlyrApproachCameraCtrl(int RectCamFlg)                      /* 1148 */
{
    APPROACH_CAMERA *apcm = &app_camera;                                /* 1159 */
    float ptv[4];
    float itv[4];
    float tw[4];
    float tr[4];

    switch (apcm->flow)                                                 /* 1161 */
    {
    case APPROACH_CAMERA_FLOW_INIT:
        /* Let the map camera place itself first, then interpolate away from
         * wherever it ended up towards the framing we want. */
        MapCamNormalCameraCtrl(RectCamFlg);                             /* 1165 */

        g3dxVu0CopyVector(apcm->npos, gra3dGetCamera()->matCoord[3]);   /* 1168 */
        g3dxVu0CopyVector(apcm->inpos, gra3dGetCamera()->vTarget);      /* 1169 */

        g3dxVu0CopyVector(itv, apcm->ipos);                             /* 1172 */
        itv[1] = apcm->ipos[1] - apcm->offy;                            /* 1173 */

        GetTrgtRot(apcm->ipos, apcm->npos, tr, 3);                      /* 1176 */
        _SetVector(tw, 0.0f, 0.0f, apcm->dist, 0.0f);                   /* 1178 */
        RotFvector(tr, tw);                                             /* 1179 */
        sceVu0AddVector(ptv, itv, tw);                                  /* 1180 */

        apcm->cnt = (GetPALMode() != 0) ? APPROACH_MOVE_FRAME_PAL : APPROACH_MOVE_FRAME_NTSC;   /* 1188 */

        sceVu0SubVector(apcm->imv, itv, apcm->inpos);                   /* 1192 */
        sceVu0DivVector(apcm->imv, apcm->imv, apcm->cnt);               /* 1193 */

        sceVu0SubVector(apcm->pmv, ptv, apcm->npos);                    /* 1196 */
        sceVu0DivVector(apcm->pmv, apcm->pmv, apcm->cnt);               /* 1197 */

        ApproachCameraCrossFadeSW(1);                                   /* 1200 */
        apcm->flow = APPROACH_CAMERA_FLOW_MOVE;                         /* 1203 */
        break;

    case APPROACH_CAMERA_FLOW_MOVE:
        if (apcm->cnt > 0.0f)                                           /* 1206 */
        {
            sceVu0AddVector(apcm->npos, apcm->npos, apcm->pmv);         /* 1207 */
            sceVu0AddVector(apcm->inpos, apcm->inpos, apcm->imv);       /* 1208 */
            apcm->cnt -= 1.0f;                                          /* 1209 */
        }
        else if (apcm->pEneWrk != 0)                                    /* 1211 */
        {
            apcm->flow = APPROACH_CAMERA_FLOW_SHOULDER;                 /* 1212 */
            apcm->cnt  = 0.0f;                                          /* 1213 */
            ApproachCameraCrossFadeSW(1);                               /* 1214 */
        }
        else
        {
            apcm->flow = APPROACH_CAMERA_FLOW_KEEP;                     /* 1217 */
        }

        SetEffects_OVERLAP(1, 20);                                      /* 1220 */
        gra3dcamSetPosition(apcm->npos);                                /* 1221 */
        gra3dcamSetTarget(apcm->inpos, 1);                              /* 1222 */
        gra3dApplyCamera(0, 1);                                         /* 1223 */
        break;                                                          /* 1224 */

    case APPROACH_CAMERA_FLOW_KEEP:
        SetEffects_OVERLAP(1, 20);                                      /* 1227 */
        gra3dcamSetPosition(apcm->npos);                                /* 1228 */
        gra3dcamSetTarget(apcm->inpos, 1);                              /* 1229 */
        gra3dApplyCamera(0, 1);                                         /* 1230 */
        break;                                                          /* 1231 */

    case APPROACH_CAMERA_FLOW_SHOULDER:
        /* Re-latch on the first frame only: the ghost is still moving, and
         * the shot should be composed from where it was when it landed. */
        if (apcm->cnt < 1.0f)                                           /* 1238 */
        {
            g3dxVu0CopyVector(apcm->NeckInitVector, apcm->pEneWrk->mpos.p0);     /* 1239 */
            PlyrShoulderCameraPositionGet(apcm->PosInitVector,          /* 1240 */
                                          PlyrShoulderCameraTypeGet(apcm->pEneWrk->dat->cmn.mdl_no));
        }

        if (apcm->cnt < 10.0f)                                          /* 1243 */
        {
            SetEffects_OVERLAP(1, 10);                                  /* 1244 */
        }

        PlyrShoulderCameraSet(apcm->PosInitVector, apcm->NeckInitVector);        /* 1246 */

        apcm->cnt += 1.0f;                                              /* 1247 */
        if (apcm->cnt > 40.0f)                                          /* 1248 */
        {
            /* NOTE: the ROM writes 20.0f here, not 20 -- every other overlap
             * request on this page passes an int.  The variadic handler read
             * the low byte of the argument slot, so the float literal made the
             * overlap 0.  The typed entry point takes an int, so the ROM's own
             * result is spelled out rather than reproduced by accident. */
            SetEffects_OVERLAP(1, 0);                                   /* 1249 */
            apcm->flow = APPROACH_CAMERA_FLOW_KEEP;                     /* 1250 */
            apcm->cnt  = 0.0f;                                          /* 1251 */
        }
        break;                                                          /* 1253 */

    case APPROACH_CAMERA_FLOW_TALK_INIT:
        g3dxVu0CopyVector(apcm->npos, gra3dGetCamera()->matCoord[3]);   /* 1260 */
        g3dxVu0CopyVector(apcm->inpos, gra3dGetCamera()->vTarget);      /* 1261 */

        g3dxVu0CopyVector(itv, apcm->ipos);                             /* 1264 */
        itv[1] = apcm->ipos[1] - apcm->offy;                            /* 1265 */

        GetTrgtRotFromPlyr(apcm->npos, tr, 3);                          /* 1268 */
        _SetVector(tw, 0.0f, 0.0f, apcm->dist, 0.0f);                   /* 1269 */
        RotFvector(tr, tw);                                             /* 1270 */
        sceVu0AddVector(ptv, itv, tw);                                  /* 1271 */

        /* No interpolation for the talk camera -- it cuts straight in. */
        g3dxVu0CopyVector(apcm->npos, ptv);                             /* 1274 */
        g3dxVu0CopyVector(apcm->inpos, itv);                            /* 1275 */

        apcm->flow = APPROACH_CAMERA_FLOW_TALK_KEEP;                    /* 1278 */
        break;

    case APPROACH_CAMERA_FLOW_TALK_KEEP:
        SetEffects_OVERLAP(1, 16);                                      /* 1281 */
        gra3dcamSetPosition(apcm->npos);                                /* 1282 */
        gra3dcamSetTarget(apcm->inpos, 1);                              /* 1283 */
        gra3dApplyCamera(0, 1);                                         /* 1284 */
        break;                                                          /* 1285 */

    case APPROACH_CAMERA_FLOW_DEAD_INIT:
    {
        float hdist = 425.0f;                                           /* 1291 */

        g3dxVu0CopyVector(apcm->npos, gra3dGetCamera()->matCoord[3]);   /* 1294 */
        g3dxVu0CopyVector(apcm->inpos, gra3dGetCamera()->vTarget);      /* 1295 */

        /* Look at a point hdist in front of the player's facing, dropped by
         * offy -- i.e. the ground she is falling towards. */
        _SetVector(tw, 0.0f, -apcm->offy, hdist, 0.0f);                 /* 1298 */
        RotFvector(apcm->trot, tw);                                     /* 1299 */
        sceVu0AddVector(itv, apcm->ipos, tw);                           /* 1300 */

        _SetVector(tw, 0.0f, 0.0f, apcm->dist, 0.0f);                   /* 1303 */
        GetTrgtRot(apcm->inpos, apcm->npos, tr, 2);                     /* 1304 */
        RotFvector(tr, tw);                                             /* 1305 */
        sceVu0AddVector(ptv, itv, tw);                                  /* 1306 */
        ptv[1] = apcm->ipos[1] - apcm->offy2;                           /* 1307 */

        gra3dcamSetPosition(ptv);                                       /* 1310 */
        gra3dcamSetTarget(itv, 1);                                      /* 1311 */
        gra3dApplyCamera(0, 1);                                         /* 1312 */

        apcm->flow = APPROACH_CAMERA_FLOW_DEAD_KEEP;                    /* 1314 */
        break;
    }

    default:
        break;
    }
}                                                                       /* 1318 */

void ApproachCameraCrossFadeSW(int sw)                                  /* 1325 */
{
    app_camera.crossfade = sw;                                          /* 1326 */
}

int GetApproachCameraCrossFade(void)                                    /* 1329 */
{
    return app_camera.crossfade;                                        /* 1330 */
}

static int PlyrShoulderCameraTypeGet(int ModelNo)                       /* 1339 */
{
    int Type;

    if (ModelNo >= 21 && ModelNo < 23)                                  /* 1341 */
    {
        Type = PLYR_SHOULDER_CAMERA_TYPE_TWINS;                         /* 1343 */
    }
    else if (ModelNo >= 27 && ModelNo < 30)                             /* 1345 */
    {
        Type = PLYR_SHOULDER_CAMERA_TYPE_ONIKODOMO;                     /* 1350 */
    }
    else
    {
        Type = PLYR_SHOULDER_CAMERA_TYPE_DEFAULT;                       /* 1353 */
    }

    return Type;                                                        /* 1357 */
}

/* Over-the-shoulder camera position, expressed in the player's own frame.
 * The literals are what the ROM has bit-for-bit; the round numbers the
 * designer typed (-20 / 57.5) survived the tool chain slightly rounded. */
static void PlyrShoulderCameraPositionGet(float *Position, int Type)    /* 1363 */
{
    static const float Offset[3][4] =                                   /* rodata 3bb200 */
    {
        { -19.999998f,  -675.000000f,   57.499996f, 1.0f },     /* default   */
        { -14.139647f,  -754.186707f,   92.515381f, 1.0f },     /* twins     */
        { -56.993893f,  -807.208252f,  110.294434f, 1.0f }      /* onikodomo */
    };

    PLCMN_WRK *pPlayerCmnWrk = &plyr_wrk.cmn_wrk;                       /* 1371 */
    float TmpMat[4][4];

    sceVu0UnitMatrix(TmpMat);                                           /* 1373 */
    sceVu0RotMatrix(TmpMat, TmpMat, pPlayerCmnWrk->mbox.rot);           /* 1374 */
    sceVu0TransMatrix(TmpMat, TmpMat, pPlayerCmnWrk->mbox.pos);         /* 1375 */
    sceVu0ApplyMatrix(Position, TmpMat, (float *)Offset[Type]);         /* 1376 */
}

static void PlyrShoulderCameraSet(float *Position, float *Target)       /* 1383 */
{
    gra3dcamSetPosition(Position);                                      /* 1384 */
    gra3dcamSetTarget(Target, 1);                                       /* 1385 */
    gra3dcamSetRoll(0.0f);                                              /* 1386 */
    gra3dcamSetFov(0.78539807f);                       /* PI/4 */       /* 1387 */
    gra3dApplyCamera(0, 1);                                             /* 1388 */
}

static void QuakeCameraInit(void)                                       /* 1427 */
{
    QUAKE_CAMERA_CTRL *pQuakeCamera = &QuakeCameraCtrl;                 /* 1428 */

    pQuakeCamera->RequestFlg = 0;                                       /* 1430 */
    pQuakeCamera->Counter    = 0;                                       /* 1431 */
    pQuakeCamera->AllTime    = 0;                                       /* 1432 */
    pQuakeCamera->LoopNum    = 0;                                       /* 1433 */
}

void QuakeCameraStop(void)                                              /* 1440 */
{
    QUAKE_CAMERA_CTRL *pQuakeCamera = &QuakeCameraCtrl;                 /* 1441 */

    pQuakeCamera->RequestFlg = 0;                                       /* 1443 */
}

void QuakeCameraReq(float Power, u_int Time, u_int LoopNum)             /* 1451 */
{
    QUAKE_CAMERA_CTRL *pQuakeCamera = &QuakeCameraCtrl;                 /* 1453 */

    pQuakeCamera->Power[0] = Power;                                     /* 1455 */
    /* The target shakes harder than the eye, which is what makes the shot
     * look like the world is moving rather than the operator. */
    pQuakeCamera->Power[1] = Power * 1.62f;                             /* 1456 */
    pQuakeCamera->Counter    = Time;                                    /* 1457 */
    pQuakeCamera->AllTime    = Time;                                    /* 1458 */
    pQuakeCamera->LoopNum    = (short)LoopNum;                          /* 1459 */
    pQuakeCamera->RequestFlg = 1;                                       /* 1460 */

    snd_utilAutoBDPlay(JISIN_BD, JISIN_HXD, 0, 0, 0x3200, 0x1000, 0, 0);        /* 1461 */
}

int QuakeCameraGetReq(void)                                             /* 1478 */
{
    QUAKE_CAMERA_CTRL *pQuakeCamera = &QuakeCameraCtrl;                 /* 1479 */

    return pQuakeCamera->RequestFlg;                                    /* 1481 */
}

/* Per-frame shake.  Val[0] displaces the eye and Val[1] the target, each a
 * random vector in the camera's own basis; the last 15 frames ramp it out. */
static int QuakeCameraMain(void)                                        /* 1487 */
{
    QUAKE_CAMERA_CTRL *pQuakeCamera = &QuakeCameraCtrl;                 /* 1496 */
    GRA3DCAMERA *pCam = gra3dGetCamera();                               /* 1497 */
    float (&rCamPos)[4] = gra3dcamGetPosition();                        /* 1498 */
    float TmpMat[4][4];
    float Val[2][4];
    float CameraXd[4];
    float CameraYd[4];
    float CameraZd[4];
    float CameraPosition[4];
    float CameraTarget[4];
    float V0;
    float V1;
    float V2;
    int i;

    if (pQuakeCamera->RequestFlg == 0)                                  /* 1500 */
    {
        return 0;
    }

    /* One frame of lead-in: the request is raised mid-frame, so the first
     * pass only burns a tick. */
    if (pQuakeCamera->Counter >= pQuakeCamera->AllTime)                 /* 1503 */
    {
        pQuakeCamera->Counter--;                                        /* 1504 */
        return 0;                                                       /* 1505 */
    }

    if (--pQuakeCamera->Counter <= 0)                                   /* 1508 */
    {
        if (pQuakeCamera->LoopNum == 0)                                 /* 1510 */
        {
            pQuakeCamera->RequestFlg = 0;                               /* 1511 */
            QuakeCameraStop();                                          /* 1512 */
            return 0;                                                   /* 1513 */
        }

        /* Aftershock: wait 300..600 frames, then shake again. */
        pQuakeCamera->LoopNum--;                                        /* 1516 */
        pQuakeCamera->Counter =
            (int)(600.0f + (300.0f - 600.0f) * ((float)MioPan_Rand() / MIOPAN_RAND_MAXF));
        return 0;                                                       /* 1517 */
    }

    if ((pQuakeCamera->Counter & 1) != 0)                               /* 1520 */
    {
        VibrateRequest2(0, 0xa0);                                       /* 1521 */
    }

    /* Camera basis: Z along the view vector, Y straight down rolled by the
     * current camera roll, X their cross product. */
    sceVu0SubVector(CameraZd, pCam->vTarget, rCamPos);                  /* 1523 */

    CameraYd[0] = 0.0f; CameraYd[1] = -1.0f; CameraYd[2] = 0.0f; CameraYd[3] = 0.0f;     /* 1525 */
    sceVu0UnitMatrix(TmpMat);                                           /* 1526 */
    sceVu0RotMatrixZ(TmpMat, TmpMat, -gra3dcamGetRoll());               /* 1527 */
    sceVu0ApplyMatrix(CameraYd, TmpMat, CameraYd);                      /* 1528 */

    sceVu0OuterProduct(CameraXd, CameraZd, CameraYd);                   /* 1530 */

    for (i = 0; i < 2; i++)                                             /* 1533 */
    {
        float dat = pQuakeCamera->Power[i];                             /* 1534 */

        V0 = -dat + (dat - -dat) * ((float)MioPan_Rand() / MIOPAN_RAND_MAXF); /* 1536 */
        V1 = -dat + (dat - -dat) * ((float)MioPan_Rand() / MIOPAN_RAND_MAXF); /* 1537 */
        V2 = -dat + (dat - -dat) * ((float)MioPan_Rand() / MIOPAN_RAND_MAXF); /* 1538 */

        if (pQuakeCamera->Counter < 15)                                 /* 1540 */
        {
            V0 *= (float)pQuakeCamera->Counter / 15.0f;                 /* 1541 */
            V1 *= (float)pQuakeCamera->Counter / 15.0f;                 /* 1542 */
            V2 *= (float)pQuakeCamera->Counter / 15.0f;                 /* 1543 */
        }

        Val[i][0] = CameraXd[0] * V0 + CameraYd[0] * V1 + CameraZd[0] * V2;      /* 1546 */
        Val[i][1] = CameraXd[1] * V0 + CameraYd[1] * V1 + CameraZd[1] * V2;      /* 1547 */
        Val[i][2] = CameraXd[2] * V0 + CameraYd[2] * V1 + CameraZd[2] * V2;      /* 1548 */
        Val[i][3] = 0.0f;                                               /* 1549 */
    }                                                                   /* 1550 */

    sceVu0AddVector(CameraPosition, rCamPos, Val[0]);                   /* 1553 */
    sceVu0AddVector(CameraTarget, pCam->vTarget, Val[1]);               /* 1554 */

    gra3dcamSetPosition(CameraPosition);                                /* 1555 */
    gra3dcamSetTarget(CameraTarget, 1);                                 /* 1556 */
    gra3dApplyCamera(0, 1);                                             /* 1557 */

    return 1;                                                           /* 1559 */
}
