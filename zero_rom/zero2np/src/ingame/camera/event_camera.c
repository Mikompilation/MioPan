/* ==========================================================================
 *  ingame/camera/event_camera.c
 *
 *  Scripted event camera.
 *
 *  Reconstructed from the Feb 6 2004 prototype (SLES_523.84), event_camera.o
 *  0x0018c660..0x0018e5ab.  Trailing /-* NNN *-/ comments are the original
 *  source line numbers recovered from the ROM's line table.
 *
 *  Manual mode is a straight apply-every-frame of whatever the event macro
 *  last set.  VCI mode is the interesting half:
 *
 *    EventCameraVCIPlayFrame()  converts the path's total arc length into a
 *                               peak speed, given the frame budget
 *      EventCameraHokan()       turns the frame number into a distance along
 *                               the path via the speed profile ...
 *        EventCameraGetProgress()   ... which is a trapezoid (accelerate,
 *                                   hold, brake) integrated to an area
 *        EventCameraSetFromPointDist() / SetBezier() / SetHermite()
 *                               place the eye and the look-at at that
 *                               distance, per HokanType
 *        EventCameraSetRollFromPointDist() / SetProjFromPointDist()
 *
 *  Eye and look-at are advanced independently: each has its own accumulated
 *  arc length, so a path whose look-at barely moves while the eye sweeps a
 *  long arc still reaches both ends together.
 * ======================================================================== */

#include "event_camera.h"

#include "map_camera.h"

#include "../../common/utility.h"                   /* _SetVector           */
#include "../../common/utility2.h"                  /* PRINT_ASSERT         */
#include "../../common/variable.h"                  /* pad                  */
#include "../../common/zero2_util.h"                /* GetObjectPos         */
#include "../../graphics/effect/effect_sub.h"       /* Get2PLength          */
#include "../../graphics/graph3d/ctl/fixed_array.h"
#include "../../graphics/graph3d/g3dxVu0.h"         /* g3dxVu0CopyVector    */
#include "../../graphics/graph3d/gra3d.h"
#include "../../ingame/plyr/player.h"               /* GetPlyrAreaNo        */
#include "../../miopan/miopan_memory.h"             /* MioPan_GetHostPointer*/
#include "../../system/os/system.h"                 /* CAMERA_VCI_ADDR      */

#include <libvu0.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

/* --------------------------------------------------------------------------
 *  Local constants.
 * ------------------------------------------------------------------------ */

/* Default eye offset and field of view installed by EventCameraInitCtrl().
 * Both come out of the object's .lit4 block (0x3ee278 / 0x3ee27c). */
#define EVECAM_INIT_Z    (-5000.0f)
#define EVECAM_INIT_FOV  0.768371f          /* ~44 degrees                   */

/* HokanType values in VCI_CAMERA_HEADER. */
#define EVECAM_HOKAN_LINEAR   0
#define EVECAM_HOKAN_BEZIER   1
#define EVECAM_HOKAN_HERMITE  2

/* Bezier control-point budget.  CalcBezierPoint() and EventCameraSetBezier()
 * both carry a fixed_array of this many float[4] on the stack. */
#define EVECAM_BEZIER_POINT_MAX 50

/* VCI file magic, "EVEC", compared as two halfwords (which is what the ROM
 * emits -- little-endian, so the pairs read back reversed). */
#define EVECAM_VCI_ID0  0x5645              /* 'E','V'                       */
#define EVECAM_VCI_ID1  0x4345              /* 'E','C'                       */

/* PORT: the ROM reads the camera VCI pak straight off its fixed EE address.
 * On the host the loader writes it into the emulated RAM block instead, so
 * the base has to be translated.  MioPan_GetHostPointer() is idempotent by
 * range check, so evaluating this per use costs only a range test. */
#define EVECAM_VCI_TOP  ((u_int *)MioPan_GetHostPointer(CAMERA_VCI_ADDR))


/* --------------------------------------------------------------------------
 *  Controller state.
 *
 *  The offset comments are the ROM's.  pEventCamData is a pointer, so from
 *  there on the host layout drifts (8-byte alignment moves it to 0x60 and
 *  WorldFlg to 0x68); sizeof still lands on 0x70.  Nothing outside this file
 *  reads the block and it is never overlaid on ROM data, so the drift is
 *  documentation-only.
 * ------------------------------------------------------------------------ */
typedef struct                      /* 0x70 */
{
    /* 0x00 */ float  EventPosition[4];
    /* 0x10 */ float  EventTarget[4];
    /* 0x20 */ float  EventPositionOffset[4];
    /* 0x30 */ float  EventTargetOffset[4];
    /* 0x40 */ int    EventPositonObjType;   /* ROM's spelling                */
    /* 0x44 */ int    EventPositonObjId;
    /* 0x48 */ int    EventTargetObjType;
    /* 0x4c */ int    EventTargetObjId;
    /* 0x50 */ float  EventFov;
    /* 0x54 */ float  EventRoll;
    /* 0x58 */ float  EventMargin;
    /* 0x5c */ u_int *pEventCamData;         /* non-NULL selects VCI mode     */
    /* 0x60 */ u_int  WorldFlg;
} EVENT_CAMERA_CTRL;

typedef struct                      /* 0x8 */
{
    /* 0x0 */ u_int *pDataTop;
    /* 0x4 */ int    NowFrame;
} VCI_CAMERA_CTRL;

/* bss 47bf70 */ static EVENT_CAMERA_CTRL event_camera_ctrl;
/* sbss 3f4c58 */ static VCI_CAMERA_CTRL  vci_cam_ctrl;


/* --------------------------------------------------------------------------
 *  File-local helpers (static in the ROM: absent from ZERO2.MAP's symbol
 *  list, present in the debug line table).
 * ------------------------------------------------------------------------ */
static void  EventCameraInitCtrl(EVENT_CAMERA_CTRL *pEveCamCtrl);
static void  EventCameraVCIPlayFrame(VCI_CAMERA_HEADER *pHead, u_int frame, int RoomNo);
static float EventCameraGetProgress(VCI_CAMERA_HEADER *pHead, u_int frame);
static void  EventCameraHokan(VCI_CAMERA_HEADER *pHead, u_int frame,
                              float PosSpdMax, float TargetSpdMax,
                              float RollSpdMax, float ProjSpdMax, int RoomNo);
static int   EventCameraGetPosDistToPointNo(VCI_CAMERA_HEADER *pHead, float PosDist);
static int   EventCameraGetTargetDistToPointNo(VCI_CAMERA_HEADER *pHead, float TargetDist);
static void  EventCameraGetMoveDistToPoint(float *pPosDist, float *pTargetDist,
                                           VCI_CAMERA_HEADER *pHead, int PointNo);
static float EventCameraGetRollDistToPoint(VCI_CAMERA_HEADER *pHead, int PointNo);
static float EventCameraGetProjDistToPoint(VCI_CAMERA_HEADER *pHead, int PointNo);
static void  EventCameraGetMoveDist(float *pPosDist, float *pTargetDist,
                                    VCI_CAMERA_HEADER *pHead);
static float EventCameraGetRollDist(VCI_CAMERA_HEADER *pHead);
static float EventCameraGetProjDist(VCI_CAMERA_HEADER *pHead);
static void  EventCameraSetFromPointDist(VCI_CAMERA_HEADER *pHead, int PointNo,
                                         float Dist, int RoomNo, int PointType);
static void  EventCameraSetBezier(VCI_CAMERA_HEADER *pHead, float Dist,
                                  int RoomNo, int PointType);
static void  EventCameraSetRollFromPointDist(VCI_CAMERA_HEADER *pHead, int PointNo,
                                             float Dist);
static void  EventCameraSetProjFromPointDist(VCI_CAMERA_HEADER *pHead, int PointNo,
                                             float Dist);
static u_int *GetAddrPK2(u_int *top, u_int no);
static void  EventCameraSetHermite(VCI_CAMERA_HEADER *pHead, int PointNo,
                                   float Dist, int RoomNo, int PointType);
static void  EventCameraGetHermiteDirVector(float *DirVector, VCI_CAMERA_POINT *pEveCam,
                                            int PointNo, int DataNum, int PointType);


/* ==========================================================================
 *  Manual mode
 * ======================================================================== */

void EventCameraReq(void)
{                                                                    /* 127 */
    EventCameraInitCtrl(&event_camera_ctrl);                         /* 130 */
    MapCamSetEventCameraFlg(1);                                      /* 131 */
}

static void EventCameraInitCtrl(EVENT_CAMERA_CTRL *pEveCamCtrl)
{                                                                    /* 140 */
    _SetVector(pEveCamCtrl->EventPosition, 0.0f, 0.0f, EVECAM_INIT_Z, 0.0f);
    _SetVector(pEveCamCtrl->EventTarget, 0.0f, 0.0f, 0.0f, 0.0f);
    _SetVector(pEveCamCtrl->EventPositionOffset, 0.0f, 0.0f, 0.0f, 0.0f);
    _SetVector(pEveCamCtrl->EventTargetOffset, 0.0f, 0.0f, 0.0f, 0.0f);

    pEveCamCtrl->EventPositonObjType = -1;
    pEveCamCtrl->EventPositonObjId   = -1;
    pEveCamCtrl->EventTargetObjType  = -1;
    pEveCamCtrl->EventTargetObjId    = -1;

    pEveCamCtrl->EventFov    = EVECAM_INIT_FOV;
    pEveCamCtrl->EventRoll   = 0.0f;
    pEveCamCtrl->EventMargin = 0.0f;

    pEveCamCtrl->pEventCamData = NULL;
    pEveCamCtrl->WorldFlg      = 0;                                  /* 153 */
}

void EventCameraInitCtrlReq(void)
{                                                                    /* 159 */
    EventCameraInitCtrl(&event_camera_ctrl);                         /* 160 */
}

void EventCameraSetVCIAddress(u_int *pData)
{                                                                    /* 170 */
    EVENT_CAMERA_CTRL *pEveCamCtrl = &event_camera_ctrl;

    pEveCamCtrl->pEventCamData = pData;                              /* 172 */
}

void EventCameraSetPosition(const float *Position)
{
    EVENT_CAMERA_CTRL *pEveCamCtrl = &event_camera_ctrl;

    g3dxVu0CopyVector(pEveCamCtrl->EventPosition, Position);

    /* A fixed point and an object binding are mutually exclusive, and a
     * manual set also drops out of VCI mode. */
    pEveCamCtrl->EventPositonObjType = -1;
    pEveCamCtrl->EventPositonObjId   = -1;
    pEveCamCtrl->pEventCamData       = NULL;                         /* 190 */
}

void EventCameraSetTarget(const float *Target)
{
    EVENT_CAMERA_CTRL *pEveCamCtrl = &event_camera_ctrl;

    g3dxVu0CopyVector(pEveCamCtrl->EventTarget, Target);

    pEveCamCtrl->EventTargetObjType = -1;
    pEveCamCtrl->EventTargetObjId   = -1;
    pEveCamCtrl->pEventCamData      = NULL;                          /* 208 */
}

void EventCameraSetPositionObjId(int Type, int Id)
{                                                                    /* 221 */
    EVENT_CAMERA_CTRL *pEveCamCtrl = &event_camera_ctrl;

    pEveCamCtrl->EventPositonObjType = Type;                         /* 223 */
    pEveCamCtrl->EventPositonObjId   = Id;                           /* 224 */
}

void EventCameraSetTargetObjId(int Type, int Id)
{                                                                    /* 237 */
    EVENT_CAMERA_CTRL *pEveCamCtrl = &event_camera_ctrl;

    pEveCamCtrl->EventTargetObjType = Type;                          /* 239 */
    pEveCamCtrl->EventTargetObjId   = Id;                            /* 240 */
}

void EventCameraSetPositionOffset(float *Offset)
{
    EVENT_CAMERA_CTRL *pEveCamCtrl = &event_camera_ctrl;

    g3dxVu0CopyVector(pEveCamCtrl->EventPositionOffset, Offset);     /* 251 */
}

void EventCameraSetTargetOffset(float *Offset)
{
    EVENT_CAMERA_CTRL *pEveCamCtrl = &event_camera_ctrl;

    g3dxVu0CopyVector(pEveCamCtrl->EventTargetOffset, Offset);       /* 264 */
}

void EventCameraSetFov(float Fov)
{                                                                    /* 276 */
    EVENT_CAMERA_CTRL *pEveCamCtrl = &event_camera_ctrl;

    pEveCamCtrl->EventFov = Fov;                                     /* 278 */
}

void EventCameraSetRoll(float Roll)
{                                                                    /* 288 */
    EVENT_CAMERA_CTRL *pEveCamCtrl = &event_camera_ctrl;

    pEveCamCtrl->EventRoll = Roll;                                   /* 290 */
}

void EventCameraSetMargin(float Margin)
{                                                                    /* 300 */
    EVENT_CAMERA_CTRL *pEveCamCtrl = &event_camera_ctrl;

    pEveCamCtrl->EventMargin = Margin;                               /* 302 */
}

void EventCameraCut(void)
{                                                                    /* 309 */
    /* MapCamSetEventCameraFlg(0) calls EventCameraInitCtrlReq() for us. */
    MapCamSetEventCameraFlg(0);                                      /* 310 */
}

void EventCameraSetWorldFlg(int Flg)
{                                                                    /* 321 */
    EVENT_CAMERA_CTRL *pEveCamCtrl = &event_camera_ctrl;

    if (Flg != 0)                                                    /* 323 */
    {
        pEveCamCtrl->WorldFlg = 1;                                   /* 324 */
    }
    else
    {
        pEveCamCtrl->WorldFlg = 0;                                   /* 327 */
    }
}

int EventCameraMain(void)
{                                                                    /* 338 */
    EVENT_CAMERA_CTRL *pEveCamCtrl = &event_camera_ctrl;             /* 339 */
    float *NowTarget;
    float  CamPosition[4];
    float  CamTarget[4];

    if (pEveCamCtrl->pEventCamData != NULL)                          /* 342 */
    {
        EventCameraVCIPlay();                                        /* 344 */
    }
    else
    {
        if (pEveCamCtrl->EventPositonObjType != -1 &&                /* 350 */
            pEveCamCtrl->EventPositonObjId != -1)
        {
            /* GetObjectPos() returning 0 means the object is not resident;
             * the camera then simply keeps last frame's eye. */
            if (GetObjectPos(CamPosition,                            /* 353 */
                             (u_char)pEveCamCtrl->EventPositonObjType,
                             pEveCamCtrl->EventPositonObjId) != 0)
            {
                sceVu0AddVector(CamPosition, CamPosition,
                                pEveCamCtrl->EventPositionOffset);   /* 354 */
                gra3dcamSetPosition(CamPosition);                    /* 355 */
            }
        }
        else if (pEveCamCtrl->WorldFlg == 0)                         /* 360 */
        {
            MapCamGra3dcamSetPositionAddOffset(pEveCamCtrl->EventPosition,
                                               GetPlyrAreaNo());     /* 361 */
        }
        else
        {
            gra3dcamSetPosition(pEveCamCtrl->EventPosition);         /* 364 */
        }

        if (pEveCamCtrl->EventTargetObjType != -1 &&                 /* 368 */
            pEveCamCtrl->EventTargetObjId != -1)
        {
            if (GetObjectPos(CamTarget,                              /* 371 */
                             (u_char)pEveCamCtrl->EventTargetObjType,
                             pEveCamCtrl->EventTargetObjId) != 0)
            {
                NowTarget = gra3dcamGetTarget();                     /* 372 */
                sceVu0AddVector(CamTarget, CamTarget,
                                pEveCamCtrl->EventTargetOffset);     /* 374 */
                MapCamGra3dcamSetTargetMargin(CamTarget, NowTarget,
                                              pEveCamCtrl->EventMargin); /* 375 */
            }
        }
        else if (pEveCamCtrl->WorldFlg == 0)                         /* 380 */
        {
            MapCamGra3dcamSetTargetAddOffset(pEveCamCtrl->EventTarget,
                                             GetPlyrAreaNo());       /* 381 */
        }
        else
        {
            gra3dcamSetTarget(pEveCamCtrl->EventTarget, 1);          /* 384 */
        }

        gra3dcamSetFov(pEveCamCtrl->EventFov);                       /* 388 */
        gra3dcamSetRoll(pEveCamCtrl->EventRoll);                     /* 390 */
    }

    gra3dApplyCamera(NULL, 1);                                       /* 397 */

    /* Always 0: the scripted move is torn down by EventCameraCut(), never by
     * the driver reporting itself finished. */
    return 0;                                                        /* 399 */
}


/* ==========================================================================
 *  VCI playback
 * ======================================================================== */

int EventCameraVCIPlay(void)
{
    if (vci_cam_ctrl.pDataTop == NULL)                               /* 414 */
    {
        printf("Error!! : pVciCam->pDataTop is NULL : in EventCameraVCIPlay()\n"); /* 415 */
        return 1;                                                    /* 416 */
    }

    /* Clamp rather than stop, so overrunning the path holds the last frame
     * instead of snapping the camera somewhere undefined. */
    if (vci_cam_ctrl.NowFrame >=                                     /* 421 */
        ((VCI_CAMERA_HEADER *)vci_cam_ctrl.pDataTop)->Frame)
    {
        vci_cam_ctrl.NowFrame =
            ((VCI_CAMERA_HEADER *)vci_cam_ctrl.pDataTop)->Frame - 1; /* 422 */
    }

    EventCameraVCIPlayFrame((VCI_CAMERA_HEADER *)vci_cam_ctrl.pDataTop,
                            vci_cam_ctrl.NowFrame, GetPlyrAreaNo()); /* 425 */

    vci_cam_ctrl.NowFrame++;                                         /* 426 */

    return 0;                                                        /* 428 */
}

int EventCameraVCIPlayIsEnd(void)
{                                                                    /* 443 */
    int Ret = 1;

    if (vci_cam_ctrl.pDataTop != NULL)
    {
        Ret = (vci_cam_ctrl.NowFrame >=
               ((VCI_CAMERA_HEADER *)vci_cam_ctrl.pDataTop)->Frame);
    }

    return Ret;                                                      /* 450 */
}

/* Turn the path's total arc length into the peak speed of the trapezoid.
 *
 * TmpFrame is the "equivalent constant-speed frame count": the trapezoid's
 * area equals a rectangle of height SpdMax over TmpFrame frames, so
 * SpdMax = 2 * distance / TmpFrame. */
static void EventCameraVCIPlayFrame(VCI_CAMERA_HEADER *pHead, u_int frame, int RoomNo)
{                                                                    /* 461 */
    float PosDist;
    float TargetDist;
    float RollDist;
    float ProjDist;
    int   AllTime;
    int   TmpFrame;

    if (pHead == NULL)                                               /* 470 */
    {
        return;
    }

    EventCameraGetMoveDist(&PosDist, &TargetDist, pHead);             /* 477 */
    RollDist = EventCameraGetRollDist(pHead);                         /* 478 */
    ProjDist = EventCameraGetProjDist(pHead);                         /* 479 */

    AllTime = pHead->AccelTime + pHead->EqualTime + pHead->BrakeTime; /* 482 */

    if (AllTime == 0)                                                 /* 483 */
    {
        printf("Error!! AllTime is zero. : in EventCameraVCIPlayFrame()\n"); /* 484 */
        return;                                                       /* 485 */
    }

    TmpFrame = pHead->Frame + pHead->Frame * pHead->EqualTime / AllTime; /* 487 */

    if (TmpFrame == 0)                                                /* 488 */
    {
        printf("Error!! TmpFrame is zero. : in EventCameraVCIPlayFrame()\n"); /* 489 */
        return;                                                       /* 490 */
    }

    EventCameraHokan(pHead, frame,
                     (PosDist + PosDist) / (float)TmpFrame,           /* 492 */
                     (TargetDist + TargetDist) / (float)TmpFrame,     /* 493 */
                     (RollDist + RollDist) / (float)TmpFrame,         /* 494 */
                     (ProjDist + ProjDist) / (float)TmpFrame,         /* 495 */
                     RoomNo);                                         /* 497 */
}

void EventCameraVCICtrlInit(u_int *pDataTop)
{                                                                    /* 511 */
    vci_cam_ctrl.pDataTop  = pDataTop;
    vci_cam_ctrl.NowFrame  = 0;                                      /* 512 */
}

/* Integrate the trapezoidal speed profile up to `frame`, in percent-of-peak
 * units (Hokan divides the 100 back out).  The three phase lengths are the
 * header's ratios scaled by the total frame count. */
static float EventCameraGetProgress(VCI_CAMERA_HEADER *pHead, u_int frame)
{
    u_int AllTime;
    u_int AccelFrame;
    u_int EqualFrame;
    u_int BrakeFrame;
    u_int TmpFrame;
    float ret = 0.0f;                                                /* 528 */

    AllTime = pHead->AccelTime + pHead->EqualTime + pHead->BrakeTime; /* 530 */

    if (AllTime == 0)                                                /* 531 */
    {
        printf("Error!! : AllTime is zero\n");                       /* 532 */
        return ret;                                                  /* 533 */
    }

    AccelFrame = pHead->AccelTime * pHead->Frame / AllTime;          /* 536 */
    BrakeFrame = pHead->BrakeTime * pHead->Frame / AllTime;          /* 537 */
    EqualFrame = pHead->Frame - AccelFrame - BrakeFrame;             /* 538 */

    if (frame <= AccelFrame)                                         /* 540 */
    {
        /* Ramp-up: area of the triangle under the speed line. */
        if (AccelFrame != 0)                                         /* 542 */
        {
            ret = (float)frame * 100.0f * (float)frame
                  / (float)AccelFrame * 0.5f;                        /* 543 */
        }
    }
    else if (frame <= AccelFrame + EqualFrame)                       /* 549 */
    {
        if (AccelFrame + EqualFrame != 0)                            /* 551 */
        {
            ret = (float)AccelFrame * 100.0f * 0.5f
                  + (float)(frame - AccelFrame) * 100.0f;            /* 552 */
        }
    }
    else
    {
        /* Ramp-down: the whole trapezoid less the triangle still to come. */
        TmpFrame = BrakeFrame - ((frame - AccelFrame) - EqualFrame); /* 560 */

        if (BrakeFrame != 0)                                         /* 562 */
        {
            ret = ((float)AccelFrame * 100.0f * 0.5f
                   + (float)EqualFrame * 100.0f
                   + (float)BrakeFrame * 100.0f * 0.5f)
                  - (float)TmpFrame * 100.0f * (float)TmpFrame
                    / (float)BrakeFrame * 0.5f;                      /* 564 */
        }
    }

    return ret;                                                      /* 571 */
}

/* Place the camera for one frame.  The four *SpdMax arguments are peak speeds
 * per axis; scaling them by the shared progress keeps position, target, roll
 * and fov in lockstep even though each covers a different total distance. */
static void EventCameraHokan(VCI_CAMERA_HEADER *pHead, u_int frame,
                             float PosSpdMax, float TargetSpdMax,
                             float RollSpdMax, float ProjSpdMax, int RoomNo)
{
    float PosDist;
    float TargetDist;
    float RollDist;
    float ProjDist;
    int   PointNo;
    float Progress;

    Progress = EventCameraGetProgress(pHead, frame);                 /* 593 */

    PosDist    = PosSpdMax * Progress / 100.0f;                      /* 595 */
    TargetDist = TargetSpdMax * Progress / 100.0f;                   /* 596 */
    RollDist   = RollSpdMax * Progress / 100.0f;                     /* 597 */
    ProjDist   = ProjSpdMax * Progress / 100.0f;                     /* 598 */

    /* Roll and fov key off a single segment index; take it from whichever of
     * the two streams has travelled further. */
    if (PosDist < TargetDist)                                        /* 602 */
    {
        PointNo = EventCameraGetTargetDistToPointNo(pHead, TargetDist); /* 603 */
    }
    else
    {
        PointNo = EventCameraGetPosDistToPointNo(pHead, PosDist);    /* 606 */
    }

    if (pHead->HokanType == EVECAM_HOKAN_BEZIER)                     /* 609 */
    {
        EventCameraSetBezier(pHead, PosDist, RoomNo, EVECAM_POINT_POSITION);  /* 610 */
        EventCameraSetBezier(pHead, TargetDist, RoomNo, EVECAM_POINT_TARGET); /* 611 */
    }
    else if (pHead->HokanType == EVECAM_HOKAN_HERMITE)               /* 614 */
    {
        EventCameraSetHermite(pHead, PointNo, PosDist, RoomNo, EVECAM_POINT_POSITION);  /* 615 */
        EventCameraSetHermite(pHead, PointNo, TargetDist, RoomNo, EVECAM_POINT_TARGET); /* 616 */
    }
    else
    {
        EventCameraSetFromPointDist(pHead, PointNo, PosDist, RoomNo, EVECAM_POINT_POSITION);  /* 620 */
        EventCameraSetFromPointDist(pHead, PointNo, TargetDist, RoomNo, EVECAM_POINT_TARGET); /* 621 */
    }

    EventCameraSetRollFromPointDist(pHead, PointNo, RollDist);       /* 625 */
    EventCameraSetProjFromPointDist(pHead, PointNo, ProjDist);       /* 626 */

    gra3dApplyCamera(NULL, 1);                                       /* 628 */
}


/* ==========================================================================
 *  Arc-length queries
 * ======================================================================== */

/* Segment index the eye has reached after travelling PosDist along the path. */
static int EventCameraGetPosDistToPointNo(VCI_CAMERA_HEADER *pHead, float PosDist)
{                                                                    /* 640 */
    VCI_CAMERA_POINT *pEveCam;
    int   i;
    float dist    = 0.0f;                                            /* 643 */
    int   PointNo = 0;                                               /* 644 */

    if (pHead == NULL)                                               /* 646 */
    {
        printf("Error!! : pHead is NULL\n");                         /* 647 */
        return PointNo;                                              /* 648 */
    }

    pEveCam = (VCI_CAMERA_POINT *)&pHead[1];

    for (i = 0; i < pHead->DataNum - 1; i++, pEveCam++)              /* 653 */
    {
        dist += Get2PLength(pEveCam[0].LocalPosition,
                            pEveCam[1].LocalPosition);               /* 654 */

        if (dist >= PosDist)                                         /* 655 */
        {
            PointNo = i;                                             /* 656 */
            break;
        }
    }

    /* Ran off the end -- park on the last segment. */
    if (dist < PosDist)                                              /* 661 */
    {
        PointNo = pHead->DataNum - 2;                                /* 662 */
    }

    return PointNo;                                                  /* 665 */
}

static int EventCameraGetTargetDistToPointNo(VCI_CAMERA_HEADER *pHead, float TargetDist)
{                                                                    /* 677 */
    VCI_CAMERA_POINT *pEveCam;
    int   i;
    float dist    = 0.0f;                                            /* 680 */
    int   PointNo = 0;                                               /* 681 */

    if (pHead == NULL)                                               /* 683 */
    {
        printf("Error!! : pHead is NULL\n");                         /* 684 */
        return PointNo;                                              /* 685 */
    }

    pEveCam = (VCI_CAMERA_POINT *)&pHead[1];

    for (i = 0; i < pHead->DataNum - 1; i++, pEveCam++)              /* 690 */
    {
        dist += Get2PLength(pEveCam[0].LocalTarget,
                            pEveCam[1].LocalTarget);                 /* 691 */

        if (dist >= TargetDist)                                      /* 692 */
        {
            PointNo = i;                                             /* 693 */
            break;
        }
    }

    if (dist < TargetDist)                                           /* 698 */
    {
        PointNo = pHead->DataNum - 2;                                /* 699 */
    }

    return PointNo;                                                  /* 702 */
}

/* Arc length from the start of the path up to point PointNo.  Either output
 * may be NULL; the eye and the look-at are measured independently. */
static void EventCameraGetMoveDistToPoint(float *pPosDist, float *pTargetDist,
                                          VCI_CAMERA_HEADER *pHead, int PointNo)
{
    VCI_CAMERA_POINT *pEveCam;
    int i;

    if (pHead == NULL)                                               /* 719 */
    {
        printf("Error!! pHead is NULL : in EventCameraGetMoveDistToPoint()\n"); /* 720 */
        return;
    }

    pEveCam = (VCI_CAMERA_POINT *)&pHead[1];

    if (pPosDist != NULL)                                            /* 726 */
    {
        *pPosDist = 0.0f;                                            /* 727 */
    }

    if (pTargetDist != NULL)                                         /* 729 */
    {
        *pTargetDist = 0.0f;
    }

    /* Reported, not fatal: the ROM walks past the end anyway. */
    if (PointNo >= pHead->DataNum)                                   /* 733 */
    {
        printf("Error!! : PointNo is beyond DataNum\n");             /* 734 */
    }

    for (i = 0; i < PointNo; i++)                                    /* 737 */
    {
        if (pPosDist != NULL)                                        /* 738 */
        {
            *pPosDist += Get2PLength(pEveCam[0].LocalPosition,
                                     pEveCam[1].LocalPosition);      /* 739 */
        }

        if (pTargetDist != NULL)                                     /* 742 */
        {
            *pTargetDist += Get2PLength(pEveCam[0].LocalTarget,
                                        pEveCam[1].LocalTarget);     /* 743 */
        }

        pEveCam++;                                                   /* 746 */
    }
}

/* Roll and fov are scalars, so their "distance" is the summed absolute
 * change rather than an arc length. */
static float EventCameraGetRollDistToPoint(VCI_CAMERA_HEADER *pHead, int PointNo)
{
    VCI_CAMERA_POINT *pEveCam;
    int   i;
    float dist = 0.0f;                                               /* 761 */

    if (pHead == NULL)                                               /* 763 */
    {
        printf("Error!! pHead is NULL : in EventCameraGetRollDistToPoint()\n"); /* 764 */
        return dist;                                                 /* 765 */
    }

    pEveCam = (VCI_CAMERA_POINT *)&pHead[1];

    if (PointNo >= pHead->DataNum)                                   /* 770 */
    {
        printf("Error!! : PointNo is beyond DataNum\n");             /* 771 */
    }

    for (i = 0; i < PointNo; i++)                                    /* 774 */
    {
        dist += fabs(pEveCam[1].Roll - pEveCam[0].Roll);             /* 777 */

        pEveCam++;                                                   /* 779 */
    }

    return dist;                                                     /* 781 */
}

static float EventCameraGetProjDistToPoint(VCI_CAMERA_HEADER *pHead, int PointNo)
{
    VCI_CAMERA_POINT *pEveCam;
    int   i;
    float dist = 0.0f;                                               /* 796 */

    if (pHead == NULL)                                               /* 798 */
    {
        printf("Error!! pHead is NULL : in EventCameraGetProjDistToPoint()\n"); /* 799 */
        return dist;                                                 /* 800 */
    }

    pEveCam = (VCI_CAMERA_POINT *)&pHead[1];

    if (PointNo >= pHead->DataNum)                                   /* 805 */
    {
        printf("Error!! : PointNo is beyond DataNum\n");             /* 806 */
    }

    for (i = 0; i < PointNo; i++)                                    /* 809 */
    {
        dist += fabs(pEveCam[1].Proj - pEveCam[0].Proj);             /* 812 */

        pEveCam++;                                                   /* 814 */
    }

    return dist;                                                     /* 816 */
}

static void EventCameraGetMoveDist(float *pPosDist, float *pTargetDist,
                                   VCI_CAMERA_HEADER *pHead)
{
    if (pHead == NULL)                                               /* 828 */
    {
        printf("Error!! pHead is NULL : in EventCameraGetMoveDist()\n"); /* 829 */
        return;
    }

    EventCameraGetMoveDistToPoint(pPosDist, pTargetDist, pHead,
                                  pHead->DataNum - 1);               /* 833 */
}

static float EventCameraGetRollDist(VCI_CAMERA_HEADER *pHead)
{
    if (pHead == NULL)                                               /* 845 */
    {
        printf("Error!! pHead is NULL : in EventCameraGetRollDist()\n"); /* 846 */
        return 0.0f;                                                 /* 847 */
    }

    return EventCameraGetRollDistToPoint(pHead, pHead->DataNum - 1); /* 850 */
}

static float EventCameraGetProjDist(VCI_CAMERA_HEADER *pHead)
{
    if (pHead == NULL)                                               /* 864 */
    {
        printf("Error!! pHead is NULL : in EventCameraGetProjDist()\n"); /* 865 */
        return 0.0f;                                                 /* 866 */
    }

    return EventCameraGetProjDistToPoint(pHead, pHead->DataNum - 1); /* 869 */
}


/* ==========================================================================
 *  Interpolators
 * ======================================================================== */

/* HokanType 0: straight line between the two points bracketing Dist.  Every
 * error message in this family names EventCameraSetPosFromPointDist(), the
 * name this function must have carried before the target/position split. */
static void EventCameraSetFromPointDist(VCI_CAMERA_HEADER *pHead, int PointNo,
                                        float Dist, int RoomNo, int PointType)
{
    VCI_CAMERA_POINT *pEveCam;
    float TmpVec[4];
    float StartPos[4];
    float EndPos[4];
    float PointDist;
    float Point2PointDist;

    if (pHead == NULL)                                               /* 891 */
    {
        printf("Error!! pHead is NULL : in EventCameraSetPosFromPointDist()\n"); /* 892 */
        return;                                                      /* 893 */
    }

    pEveCam = (VCI_CAMERA_POINT *)&pHead[1];

    if (PointType == EVECAM_POINT_POSITION)                          /* 898 */
    {
        g3dxVu0CopyVector(StartPos, pEveCam[PointNo].LocalPosition);
        g3dxVu0CopyVector(EndPos, pEveCam[PointNo + 1].LocalPosition);

        EventCameraGetMoveDistToPoint(&PointDist, NULL, pHead, PointNo); /* 901 */
    }
    else
    {
        g3dxVu0CopyVector(StartPos, pEveCam[PointNo].LocalTarget);
        g3dxVu0CopyVector(EndPos, pEveCam[PointNo + 1].LocalTarget);

        EventCameraGetMoveDistToPoint(NULL, &PointDist, pHead, PointNo); /* 906 */
    }

    /* Distance still to travel inside this one segment. */
    Dist -= PointDist;                                               /* 908 */

    Point2PointDist = Get2PLength(EndPos, StartPos);                 /* 910 */

    sceVu0SubVector(TmpVec, EndPos, StartPos);                       /* 912 */

    if (Point2PointDist != 0.0f)                                     /* 914 */
    {
        sceVu0ScaleVector(TmpVec, TmpVec, Dist / Point2PointDist);   /* 915 */
    }
    else
    {
        sceVu0ScaleVector(TmpVec, TmpVec, 0.0f);                     /* 918 */
    }

    sceVu0AddVector(TmpVec, TmpVec, StartPos);                       /* 920 */

    if (PointType == EVECAM_POINT_POSITION)                          /* 927 */
    {
        MapCamGra3dcamSetPositionAddOffset(TmpVec, RoomNo);           /* 928 */
    }
    else
    {
        MapCamGra3dcamSetTargetAddOffset(TmpVec, RoomNo);             /* 931 */
    }
}

/* HokanType 1: one Bezier over every point in the path.  Unlike the linear
 * and Hermite cases this is not segment-local, so it takes the fraction of
 * the *total* arc length rather than a segment index. */
static void EventCameraSetBezier(VCI_CAMERA_HEADER *pHead, float Dist,
                                 int RoomNo, int PointType)
{
    fixed_array<float[4], EVECAM_BEZIER_POINT_MAX> ControlPoint;
    float TmpVec[4];
    VCI_CAMERA_POINT *pEveCam;
    int   i;
    float AllDist;
    float DistRate;

    if (pHead == NULL)                                               /* 954 */
    {
        printf("Error!! pHead is NULL : in EventCameraSetPosFromPointDist()\n"); /* 955 */
        return;                                                      /* 956 */
    }

    if (pHead->DataNum > EVECAM_BEZIER_POINT_MAX)                    /* 959 */
    {
        printf("Error!! Bezier Control Point Over : in EventCameraSetPosFromPointDist() : pHead->DataNum = %d\n",
               pHead->DataNum);                                      /* 960 */
        return;                                                      /* 961 */
    }

    pEveCam = (VCI_CAMERA_POINT *)&pHead[1];

    if (PointType == EVECAM_POINT_POSITION)                          /* 966 */
    {
        for (i = 0; i < pHead->DataNum; i++)                         /* 967 */
        {
            g3dxVu0CopyVector(ControlPoint[i], pEveCam[i].LocalPosition);
        }

        EventCameraGetMoveDist(&AllDist, NULL, pHead);               /* 970 */
    }
    else
    {
        for (i = 0; i < pHead->DataNum; i++)                         /* 973 */
        {
            g3dxVu0CopyVector(ControlPoint[i], pEveCam[i].LocalTarget);
        }

        EventCameraGetMoveDist(NULL, &AllDist, pHead);               /* 976 */
    }

    if (AllDist != 0.0f)                                             /* 979 */
    {
        DistRate = Dist / AllDist;                                   /* 980 */
    }
    else
    {
        DistRate = 0.0f;                                             /* 983 */
    }

    CalcBezierPoint(TmpVec, pHead->DataNum - 1, DistRate, &ControlPoint[0]);

    if (PointType == EVECAM_POINT_POSITION)                          /* 989 */
    {
        MapCamGra3dcamSetPositionAddOffset(TmpVec, RoomNo);           /* 990 */
    }
    else
    {
        MapCamGra3dcamSetTargetAddOffset(TmpVec, RoomNo);             /* 993 */
    }
}

/* Roll is interpolated on the same accumulated-change scale the distance
 * queries produce, so dividing by |ChgRoll| turns Dist back into a signed
 * step along this segment. */
static void EventCameraSetRollFromPointDist(VCI_CAMERA_HEADER *pHead, int PointNo,
                                            float Dist)
{
    VCI_CAMERA_POINT *pEveCam;
    float ChgRoll;
    float Roll;

    if (pHead == NULL)                                               /* 1012 */
    {
        printf("Error!! pHead is NULL : in EventCameraSetRollFromPointDist()\n"); /* 1013 */
        return;
    }

    pEveCam = (VCI_CAMERA_POINT *)&pHead[1];                         /* 1017 */

    Dist -= EventCameraGetRollDistToPoint(pHead, PointNo);      /* 1019, 1020 */

    ChgRoll = pEveCam[PointNo + 1].Roll - pEveCam[PointNo].Roll;     /* 1022 */
    Roll    = pEveCam[PointNo].Roll;                                 /* 1023 */

    if (fabs(ChgRoll) != 0.0f)                                       /* 1025 */
    {
        Roll += ChgRoll * Dist / (float)fabs(ChgRoll);               /* 1026 */
    }

    gra3dcamSetRoll(Roll);                                           /* 1032 */
}

static void EventCameraSetProjFromPointDist(VCI_CAMERA_HEADER *pHead, int PointNo,
                                            float Dist)
{
    VCI_CAMERA_POINT *pEveCam;
    float ChgProj;
    float Proj;

    if (pHead == NULL)                                               /* 1050 */
    {
        printf("Error!! pHead is NULL : in EventCameraSetProjFromPointDist()\n"); /* 1051 */
        return;
    }

    pEveCam = (VCI_CAMERA_POINT *)&pHead[1];                         /* 1055 */

    Dist -= EventCameraGetProjDistToPoint(pHead, PointNo);      /* 1057, 1058 */

    ChgProj = pEveCam[PointNo + 1].Proj - pEveCam[PointNo].Proj;     /* 1060 */
    Proj    = pEveCam[PointNo].Proj;                                 /* 1061 */

    if (fabs(ChgProj) != 0.0f)                                       /* 1063 */
    {
        Proj += ChgProj * Dist / (float)fabs(ChgProj);               /* 1064 */
    }

    gra3dcamSetFov(Proj);                                            /* 1070 */
}


/* ==========================================================================
 *  VCI data readers
 * ======================================================================== */

int EventCameraGetDataNum(u_int *pData)
{
    if (pData == NULL)                                               /* 1085 */
    {
        printf("Error!! pData is NULL : in EventCameraGetDataNum()\n"); /* 1086 */
        return 0;                                                    /* 1087 */
    }

    return ((VCI_CAMERA_HEADER *)pData)->DataNum;                    /* 1094 */
}

void EventCameraGetPosition(float *Position, u_int *pData, int PointNo)
{                                                                    /* 1106 */
    VCI_CAMERA_POINT *pPoint;

    if (pData == NULL)                                               /* 1110 */
    {
        printf("Error!! pData is NULL : in EventCameraGetPosition()\n"); /* 1111 */
        return;
    }

    pPoint = (VCI_CAMERA_POINT *)&((VCI_CAMERA_HEADER *)pData)[1];
    g3dxVu0CopyVector(Position, pPoint[PointNo].LocalPosition);      /* 1116 */
}

void EventCameraGetTarget(float *Target, u_int *pData, int PointNo)
{                                                                    /* 1130 */
    VCI_CAMERA_POINT *pPoint;

    if (pData == NULL)                                               /* 1134 */
    {
        printf("Error!! pData is NULL : in EventCameraGetTarget()\n"); /* 1135 */
        return;
    }

    pPoint = (VCI_CAMERA_POINT *)&((VCI_CAMERA_HEADER *)pData)[1];
    g3dxVu0CopyVector(Target, pPoint[PointNo].LocalTarget);
}

void EventCameraGetOneData(VCI_CAMERA_POINT *pOut, const u_int *pData, int PointNo)
{
    const VCI_CAMERA_POINT *pPoint;

    /* Copied verbatim from EventCameraGetTarget() in the ROM, name included. */
    if (pData == NULL)                                               /* 1157 */
    {
        printf("Error!! pData is NULL : in EventCameraGetTarget()\n"); /* 1158 */
        return;
    }

    pPoint = (const VCI_CAMERA_POINT *)&((const VCI_CAMERA_HEADER *)pData)[1]; /* 1163 */
    *pOut  = pPoint[PointNo];                                        /* 1165 */
}

int EventCameraCheckFileId(const u_int *pData)
{
    int ret = 0;                                                     /* 1182 */

    if (pData == NULL)                                               /* 1184 */
    {
        return ret;
    }

    /* The ROM tests the 4-byte FileId as two halfwords, not four bytes. */
    if (*(const u_short *)pData != EVECAM_VCI_ID0)                   /* 1190 */
    {
        return ret;
    }

    ret = (*(const u_short *)((const u_char *)pData + 2) == EVECAM_VCI_ID1);

    return ret;                                                      /* 1198 */
}

void EventCameraVCIReq(int CamNo)
{                                                                    /* 1207 */
    u_int *pVciData;

    pVciData = GetAddrPK2(EVECAM_VCI_TOP, CamNo);                    /* 1210 */

    if (EventCameraCheckFileId(pVciData) == 0)                       /* 1213 */
    {
        PRINT_ASSERT("Error!! Not VCI data 0x%x : EventCameraVCIReq(%d)",
                     pVciData, CamNo);                               /* 1214 */
    }

    EventCameraSetVCIAddress(pVciData);                              /* 1218 */
    EventCameraVCICtrlInit(pVciData);                                /* 1219 */
    MapCamSetEventCameraFlg(1);                                      /* 1220 */
}

/* PK2 pak accessor: word 0 is the file count, words 4.. are per-file byte
 * offsets from the pak base. */
static u_int *GetAddrPK2(u_int *top, u_int no)
{                                                                    /* 1227 */
    if (top == NULL)                                                 /* 1230 */
    {
        return NULL;
    }

    if (no >= *top)                                                  /* 1232 */
    {
        printf("Error!! *top = %d, no = %d : GetAddrPK2()\n", *top, no); /* 1233 */
        PRINT_ASSERT("");                                            /* 1234 */
    }

    return (u_int *)((u_char *)top + top[no + 4]);                   /* 1239 */
}


/* ==========================================================================
 *  Curve primitives
 * ======================================================================== */

/* De Casteljau: repeatedly lerp neighbouring control points in place until
 * one is left.  Note the lerp is sceVu0ScaleVectorXYZ, so w is carried
 * unchanged from the copied control points and overwritten at the end. */
void CalcBezierPoint(float *Point, int n, float t, float (*pControlPoint)[4])
{                                                                    /* 1253 */
    fixed_array<float[4], EVECAM_BEZIER_POINT_MAX> WorkVector;
    float TmpVector[4];
    int   m;
    int   i;
    int   j;

    /* Reported, not fatal -- the fixed_array bounds check below is what
     * actually stops an overrun. */
    if (n > EVECAM_BEZIER_POINT_MAX)                                 /* 1260 */
    {
        printf("Error!! Bezier Control Point Over : in CalcBezierPoint() : n = %d\n",
               n);                                                   /* 1261 */
    }

    for (i = 0; i <= n; i++)                                         /* 1264 */
    {
        g3dxVu0CopyVector(WorkVector[i], pControlPoint[i]);
    }                                                                /* 1266 */

    for (m = 1; m <= n; m++)                                         /* 1268 */
    {
        for (i = 0, j = 1; i <= n - m; i++, j++)                     /* 1269 */
        {
            sceVu0SubVector(TmpVector, WorkVector[j], WorkVector[i]);
            sceVu0ScaleVectorXYZ(TmpVector, TmpVector, t);           /* 1271 */
            sceVu0AddVector(WorkVector[i], WorkVector[i], TmpVector);
        }                                                            /* 1273 */
    }                                                                /* 1274 */

    g3dxVu0CopyVector(Point, WorkVector[0]);
    Point[3] = 1.0f;                                                 /* 1276 */
}

/* HokanType 2: cubic Hermite through the segment, with tangents estimated
 * from the neighbouring points. */
static void EventCameraSetHermite(VCI_CAMERA_HEADER *pHead, int PointNo,
                                  float Dist, int RoomNo, int PointType)
{
    VCI_CAMERA_POINT *pEveCam;
    float TmpVec[4];
    float StartPos[4];
    float EndPos[4];
    float DirVector0[4];
    float DirVector1[4];
    float PointDist;
    float Point2PointDist;
    float Progress;

    if (pHead == NULL)                                               /* 1320 */
    {
        printf("Error!! pHead is NULL : in EventCameraSetPosFromPointDist()\n"); /* 1321 */
        return;                                                      /* 1322 */
    }

    pEveCam = (VCI_CAMERA_POINT *)&pHead[1];

    if (PointType == EVECAM_POINT_POSITION)                          /* 1328 */
    {
        g3dxVu0CopyVector(StartPos, pEveCam[PointNo].LocalPosition);
        g3dxVu0CopyVector(EndPos, pEveCam[PointNo + 1].LocalPosition);

        EventCameraGetMoveDistToPoint(&PointDist, NULL, pHead, PointNo); /* 1331 */
    }
    else
    {
        g3dxVu0CopyVector(StartPos, pEveCam[PointNo].LocalTarget);
        g3dxVu0CopyVector(EndPos, pEveCam[PointNo + 1].LocalTarget);

        EventCameraGetMoveDistToPoint(NULL, &PointDist, pHead, PointNo); /* 1336 */
    }

    Dist -= PointDist;                                               /* 1339 */

    Point2PointDist = Get2PLength(EndPos, StartPos);                 /* 1341 */

    Progress = 0.0f;                                                 /* 1342 */

    if (Point2PointDist != Progress)                                 /* 1343 */
    {
        Progress = Dist / Point2PointDist;                           /* 1344 */
    }

    EventCameraGetHermiteDirVector(DirVector0, pEveCam, PointNo,
                                   pHead->DataNum, PointType);       /* 1348 */
    EventCameraGetHermiteDirVector(DirVector1, pEveCam, PointNo + 1,
                                   pHead->DataNum, PointType);       /* 1349 */

    CalcHermitePoint(TmpVec, StartPos, EndPos, DirVector0, DirVector1, Progress); /* 1351 */

    if (PointType == EVECAM_POINT_POSITION)                          /* 1353 */
    {
        MapCamGra3dcamSetPositionAddOffset(TmpVec, RoomNo);           /* 1354 */
    }
    else
    {
        MapCamGra3dcamSetTargetAddOffset(TmpVec, RoomNo);             /* 1356 */
    }
}

/* Central difference for interior points, one-sided (and unhalved) at both
 * ends so the curve leaves and enters the path along the first/last segment. */
static void EventCameraGetHermiteDirVector(float *DirVector, VCI_CAMERA_POINT *pEveCam,
                                           int PointNo, int DataNum, int PointType)
{
    int   StartNo;
    int   EndNo;
    float ScaleVal = 1.0f;                                           /* 1378 */

    if (PointNo == 0)                                                /* 1380 */
    {
        StartNo = 0;                                                 /* 1381 */
        EndNo   = 1;
    }
    else if (PointNo == DataNum - 1)                                 /* 1384 */
    {
        StartNo = DataNum - 2;                                       /* 1385 */
        EndNo   = PointNo;
    }
    else
    {
        StartNo  = PointNo - 1;                                      /* 1389 */
        EndNo    = PointNo + 1;
        ScaleVal = 0.5f;                                             /* 1392 */
    }

    if (PointType == EVECAM_POINT_POSITION)                          /* 1395 */
    {
        sceVu0SubVector(DirVector, pEveCam[EndNo].LocalPosition,
                        pEveCam[StartNo].LocalPosition);             /* 1396 */
    }
    else
    {
        sceVu0SubVector(DirVector, pEveCam[EndNo].LocalTarget,
                        pEveCam[StartNo].LocalTarget);               /* 1399 */
    }

    sceVu0ScaleVector(DirVector, DirVector, ScaleVal);               /* 1401 */
}

void CalcHermitePoint(float *HermiteVector, float *Point0, float *Point1,
                      float *Direction0, float *Direction1, float ParamU)
{                                                                    /* 1424 */
    float Hermite0[4];
    float Hermite1[4];
    float Hermite2[4];
    float Hermite3[4];
    float ParamU2;
    float ParamU3;
    float Basis0;
    float Basis1;
    float Basis2;
    float Basis3;

    ParamU2 = ParamU * ParamU;                                       /* 1429 */
    ParamU3 = ParamU2 * ParamU;                                      /* 1430 */

    /* The four cubic Hermite basis functions.  Basis0 is single-use and the
     * ROM's debug info gives it no register of its own; the other three are
     * named there. */
    Basis0 = ParamU3 * 2.0f - ParamU2 * 3.0f + 1.0f;                 /* 1432 */
    Basis1 = ParamU3 * -2.0f + ParamU2 * 3.0f;                       /* 1433 */
    Basis2 = ParamU3 - ParamU2 * 2.0f + ParamU;                      /* 1434 */
    Basis3 = ParamU3 - ParamU2;                                      /* 1435 */

    sceVu0ScaleVector(Hermite0, Point0, Basis0);                     /* 1437 */
    sceVu0ScaleVector(Hermite1, Point1, Basis1);                     /* 1438 */
    sceVu0ScaleVector(Hermite2, Direction0, Basis2);                 /* 1439 */
    sceVu0ScaleVector(Hermite3, Direction1, Basis3);                 /* 1440 */

    sceVu0AddVector(HermiteVector, Hermite0, Hermite1);              /* 1442 */
    sceVu0AddVector(HermiteVector, HermiteVector, Hermite2);         /* 1443 */
    sceVu0AddVector(HermiteVector, HermiteVector, Hermite3);         /* 1444 */
}


/* ==========================================================================
 *  Debug
 * ======================================================================== */

void EventCameraTest(void)
{                                                                    /* 1449 */
    if ((pad[0].one & 0x20U) != 0)                                   /* 1450 */
    {
        float CamPos[4] = { 1534.99f, -1000.0f, 1732.64f, 0.0f };    /* 1451 */
        float CamTargetOffset[4];

        memset(CamTargetOffset, 0, sizeof(CamTargetOffset));         /* 1453 */
        CamTargetOffset[1] = -500.0f;

        EventCameraReq();                                            /* 1454 */
        EventCameraSetPosition(CamPos);                              /* 1455 */
        EventCameraSetMargin(500.0f);                                /* 1457 */
        EventCameraSetTargetOffset(CamTargetOffset);                 /* 1458 */
        EventCameraSetTargetObjId(0, 0);                             /* 1459 */
        EventCameraSetRoll(0.26f);                                   /* 1461 */
    }

    if ((pad[0].one & 0x40U) != 0)                                   /* 1463 */
    {
        EventCameraCut();                                            /* 1464 */
    }
}
