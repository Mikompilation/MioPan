/* ==========================================================================
 *  ingame/map/map_rectangle.h
 *
 *  Current map-rectangle registration buffer selection.
 * ======================================================================== */

#ifndef _INGAME_MAP_MAP_RECTANGLE_H
#define _INGAME_MAP_MAP_RECTANGLE_H

#include "RegDat.h"

int MrecSetRegBuffID(int floor_id, const float *pos, int buf_no);

/* ---- camera rectangles (map_camera.c) --------------------------------- */

void MrecInitCameraInfo(void);
int  MrecIsCameraChange(void);
int  MrecIsCameraHit(void);
int  MrecGetCameraID(void);
/* A rectangle quad, tested as two XZ triangles. */
int  MrecIsInRectangle(const float *pos, float (*rec)[4]);

/* Stairs.  MrecGetStaInfo() writes the heading of the flight `pos` stands on
 * and returns non-zero when it found one; MovePlyrStairs() picks the up/down
 * stair animation from the angle between that and the player's facing. */
int    MrecGetStaInfo(float *rot, int floor, const float *pos);
float  MrecGetStaRot(int rot);
float *MrecGetStaRotTbl(void);

/* ---- hit rectangles (wall collision) ---------------------------------- */

/* Caches this frame's type-0 rectangle lists for MapHitCheck(). */
int  MrecSetHitRectInfo(int buf_no);
int  MrecGetHitInfoIdNum(void);
int *MrecGetHitInfoRecNumList(void);
float (*MrecGetHitInfoRecVecter(int list_no, int rec_no))[4];
int  MrecIsHitRectangleNum(int buf_no);
float (*MrecGetRectPtr(int num, int buf_no))[4];

/* Segment/rectangle crossing and quad containment against the cached hit list;
 * MapHitCheck() uses the pair to sweep a moving character against the walls.
 * MrecIsNearRectangle() is the cheap floor-plane AABB reject in front of them,
 * with boxmin/boxmax the character's swept bounds. */
int  MrecLineCross(const float *pos1, const float *pos2, int list_no, int num);
int  MrecIsInHitRectangle(const float *pos1, const float *pos2,
                          const float *pos3, const float *pos4,
                          int list_no, int num);
int  MrecIsNearRectangle(float *boxmin, float *boxmax, int list_no, int num);

/* Door rectangles (type 7).  Only collided against while the door's record
 * says it is solid. */
int  MrecIsDoorRectangleNum(void);
int  MrecPointDoorRectangle(float *len, float *pos, float *a, float *b,
                            int num, float r);

/* The door equivalents of the hit-rectangle cache and its walk helpers.
 * MrecCheckDoorHitSta() is the "is this door still shut" test the collision
 * loop needs, since an open door's rectangle must be ignored. */
int  MrecSetDoorHitRectInfo(int buf_no);
int  MrecGetDoorHitInfoIdNum(void);
int *MrecGetDoorHitInfoRecNumList(void);
float (*MrecGetDoorHitInfoRecVecter(int list_no, int rec_no))[4];
int  MrecCheckDoorHitSta(int list_no, int rec_no);
int  MrecDoorLineCross(const float *pos1, const float *pos2,
                       int list_no, int num);
int  MrecIsNearDoorRectangle(float *boxmin, float *boxmax,
                             int list_no, int num);

/* ---- footstep-sound rectangles (type 4) -------------------------------- */

/* Latches the highest-priority footstep-sound region covering `pos`; returns 0
 * and keeps the previous region when there is none.  MrecGetSeNo() is the
 * surface id foot_sePlay() maps to a sample, or -1 when nothing is latched. */
int MrecSetSEInfo(float *pos);
int MrecGetSeNo(void);

/* Latches the MDAT_CAM record for the rectangle `pos` falls in.  Returns 1
 * when that record changed this frame (a cut), 0 when it is the same one or
 * there is no camera rectangle here.  Losing the rectangle keeps the last
 * camera rather than dropping to none. */
int  MrecSetCameraInfo(int floor, const float *pos, int DataType);
/* The record MrecSetCameraInfo() latched. */
MDAT_CAM *MrecGetCameraInfo(void);

/* Non-zero when `Pos` on `floor` is inside a rectangle of type DataType. */
int  MrecCheckHitRect(int floor, float *Pos, int DataType);
/* Non-zero while `Pos` is still inside the same camera rectangle pRectStat
 * came from -- this is what stops the camera cutting every frame. */
int  MrecCheckOnSameCamRect(void *pRectStat, int floor, float *Pos, int DataType);
/* The four-corner camera for the type-9 rectangle at Pos.  Also hands back
 * the rectangle itself through ppRect; both are NULL when there is none. */
MDAT_CAM_SP *MrecGetCameraSpInfo(MB_OUT_RECT **ppRect, void *pRectStat, int floor, float *Pos);

/* Accessors on the latched record.  The int-returning ones give 1 when the
 * field is present. */
int   MrecGetCameraType(void);
int   MrecGetCameraPos(float *pos);
int   MrecGetCameraInterest(float *pos);
int   MrecGetCameraRotZ(float *rot_z);
float MrecGetCameraPrj(void);
/* Follow slack in world units; negative means "do not follow". */
float MrecGetCameraAsobi(void);

/* Non-zero when pos (on the given floor) is inside the event rectangle
 * registered under `label`.  Drives the PLYR_/SIS_ AREAIN/AREAOUT event
 * conditions.  MrecIsInEventSub() is the single-buffer case; MrecIsInEvent()
 * fans it out over the hit list when the position sits on a room seam. */
int MrecIsInEvent(const float *pos, int label, int floor);
int MrecIsInEventSub(const float *pos, int buf_id, int label);

/* ---- debug rectangle display ------------------------------------------
 * All gated on debug_var.hit_disp; each draws the registration rectangles of
 * one type as wire quads at the caller's height.  The `*One` variants take a
 * rectangle index and draw just that one, brighter. */
int DrawCameraRect(float *pos);
int DrawCameraRectOne(int i, float *pos);
int DrawEventRect(float *pos);
int DrawEventRectOne(int i, float *pos);
int DrawMapHitRect(float *pos);
int DrawMapHitRectOne(int i, float *pos);
int DrawSeRect(float *pos);
int DrawSeRectOne(int i, float *pos);

#endif /* _INGAME_MAP_MAP_RECTANGLE_H */
