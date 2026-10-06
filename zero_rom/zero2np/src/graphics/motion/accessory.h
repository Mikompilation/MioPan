/* ==========================================================================
 *  graphics/motion/accessory.h
 *
 *  Accessory / rope subsystem interface (accessory.c).
 *
 *  Reconstructed from the Feb 6 2004 prototype (SLES_523.84).
 * ======================================================================== */

#ifndef _GRAPHICS_MOTION_ACCESSORY_H
#define _GRAPHICS_MOTION_ACCESSORY_H

#include "mdlwork.h"

#ifdef __cplusplus
extern "C" {
#endif

void InitPlyrAcsAlpha(void);
void DispPlyrAcs(u_int *base_p, u_int *mdl_p, ACS_ALPHA *acs_ctrl, u_int bone_id);
void PlyrAcsAlphaCtrl(void);
void acsInitRopeWork(void);
void acsRopeSetWork(u_int furn_id, u_char acs_no);
void acsRopeReleaseWork(u_int furn_id);
int acsRopeGetFurnID(u_int id);
float (*acsGetRopePos(u_int furn_id))[4];
void acsInitRopeSub(u_int work_id, u_int furn_id, u_int type);
void acsRopeMoveRequest(u_int furn_id, u_char move_mode, float pow);
void acsRopeMoveStop(u_int furn_id);
u_char acsCheckRopeMoveExec(u_int furn_id);
ENE_COLLISION *acsGetRopeCollisionBuf(void);
int acsSetRopeCollision(ANI_CTRL *ani_ctrl, u_short mdl_no);
int acsDelRopeCollision(ANI_CTRL *ani_ctrl);
int acsRopeMakeCollision(COLLISION_DAT *collision, float (*mtx)[4], TUBE *tube, float scale);
void acsCalcCoordinate(SGDCOORDINATE *cp, ROPE_CTRL *rope);
void acsSetMoveDir(float *dir);
void acsRopeMoveCtrl(ROPE_CTRL *rope);
void acsRopeMoveWind(ROPE_CTRL *rope, char dir_cng);
void acsRopeMoveVib(ROPE_CTRL *rope);
void acsMoveRope(u_int furn_id, SGDCOORDINATE *furn_cp);
void acsMoveRopeEx(u_int furn_id, HeaderSection *sgd_p, float (*mat)[4]);
u_int *acsInitCloth(CLOTH_CTRL *cloth_top, COLLISION_CTRL *collision_ctrl, u_int *mpk_p, u_int *top_addr, int mdl_no, int chodo_flg);
u_int acsGetClothBufSize(u_int *mpk_p, int mdl_no);
void acsClothCtrl(ANI_CTRL *ani_ctrl, u_int *mpk_p, u_int mdl_no, u_char scene_flg);
void acsMoveCloth(float (*vtx)[4], CLOTH_CTRL *cloth, SGDCOORDINATE *cp, COLLISION_CTRL *collision_ctrl, COLLISION_DAT *collision, float scale, float collision_scale, u_char hip_id);
int acsResetCloth(ANI_CTRL *ani_ctrl);
int acsChodoInitCloth(void);
u_int *acsChodoSetCloth(u_int *mpk_p, int mdl_no, int id, u_int *top_addr, int key);
int acsChodoClothCtrl(void);
int acsChodoSetWind(int key, float rot, float pow, int cycle);
int acsChodoResetWind(int key);
int acsChodoDel(int id);
ENE_COLLISION *acsGetEneCollisionBuf(void);
int acsSetEneCollision(ANI_CTRL *ani_ctrl, u_short mdl_no);
int acsDelEneCollision(ANI_CTRL *ani_ctrl);
int acsChodoMakeCollision(COLLISION_DAT *collision, SGDCOORDINATE *cp, TUBE *tube, float scale);
u_char acsCheckCollisionSphere(SPHERE *sphere, float *current, float *relative_v, float Ke);
u_char acsCheckCollisionTube(TUBE *tube, float *current, float *relative_v, float *old_c, float Ke);
u_char acsCheckCollisionPlane(CPLANE *plane, float *current, float *relative_v, float Ke, int ofs);
void SetLWS2(SGDCOORDINATE *cp);

#ifdef __cplusplus
}
#endif

#endif /* _GRAPHICS_MOTION_ACCESSORY_H */
