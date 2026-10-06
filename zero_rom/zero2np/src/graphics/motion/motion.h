/* ==========================================================================
 *  graphics/motion/motion.h
 *
 *  Animation-control and motion helper interface -- the contents of motion.o.
 *
 *  Only functions the link map attributes to motion.o belong here.  The
 *  ANI_CODE reader lives in anicode.h/anicode.c and the look-at / neck-aim
 *  solvers live in mdlact.h/mdlact.c; include those headers directly.
 *
 *  Reconstructed from the Feb 6 2004 prototype (SLES_523.84).
 * ======================================================================== */

#ifndef _GRAPHICS_MOTION_MOTION_H
#define _GRAPHICS_MOTION_MOTION_H

#include "mdlwork.h"

void motInitANI_CTRL(void);
void motFreeANI_CTRL(ANI_CTRL *ani_ctrl);
ANI_CTRL *motGetANI_CTRL(void);
void motReleaseOneAnm(void *ani_hndl);
void motResetMdl(int mdl_no);
void *motInitOneEnemyAnm(u_int *anm_p, u_int *mdl_p, u_int mdl_no, u_int anm_no);
u_int *motInitAniCtrlMalloc(ANI_CTRL *pAniCtrl, u_int *pAnim, u_int *pModel, u_int ModelNo, u_int AnimNo);
void motInitAniCtrlFree(ANI_CTRL *pAniCtrl);
void motInitOneEnemyMdl(u_int *mdl_p, u_int mdl_no);
ANI_CTRL *motSearchANI_CTRL(int ModelNo);
void motClearANI_CTRL(ANI_CTRL *pAniCtrl);
u_int motGetAniWorkArea(u_int *anm_p, u_int *mdl_p, u_int mdl_no);
u_int *motInitAniCtrl(ANI_CTRL *ani_ctrl, u_int *anm_p, u_int *mdl_p, u_int *pkt_p, u_int mdl_no, u_int anm_no);
u_int *motInitMotCtrlEx(MOT_CTRL *m_ctrl, u_int *mot_addr, u_int *rst_addr, int play_id);
u_int *motInitMotCtrl(MOT_CTRL *m_ctrl, u_int *mot_addr, u_int *rst_addr);
void motSetCoordCamera(ANI_CTRL *ani_ctrl);
u_char motSetCoord(ANI_CTRL *ani_ctrl, u_char work_id, u_char stop_fl);
u_int motGetNowFrame(MOT_CTRL *m_ctrl);
float motGetNowFramef(MOT_CTRL *m_ctrl);
void ReqAnm(void *ani_hndl, int flame, int anm_no, int anime_no);
void motSetAnime(ANI_CTRL *ani_ctrl, ANI_CODE **tbl, int req_no);
int motCheckInterp(ANI_CTRL *ani_ctrl);
int motGetMotReso(void);
void GetMdlNeckPos(float *pos, ANI_CTRL *ani_ctrl, u_short mdl_no);
u_int GetMdlBonePos(float (*pos)[4], void *ani_hndl);
void GetMdlWaistPos(float *pos, ANI_CTRL *ani_ctrl, u_short mdl_no);
void GetMdlHipPos(float *pos, ANI_CTRL *ani_ctrl, u_short mdl_no);
void GetMdlLegPos(float *pos, ANI_CTRL *ani_ctrl, u_short mdl_no);
void GetMdlShldPos(float *pos, ANI_CTRL *ani_ctrl, u_char lr);
void GetPlyrFootPos(float *pos, ANI_CTRL *ani_ctrl, u_char lr);
void GetPlyrAcsLightPos(float *pos, ANI_CTRL *ani_ctrl);
void GetToushuKatanaPos(float *p0, float *p1, ANI_CTRL *ani_ctrl);
int motGetGuujiTuePos(float *p0, ANI_CTRL *ani_ctrl, int flg);
int motGetKusabiPos(float *p0, ANI_CTRL *ani_ctrl, int flg);
int motGetTaimatuPos(float *p0, ANI_CTRL *ani_ctrl);
int motGetKuroreiPos(float *p0, ANI_CTRL *ani_ctrl);
int motGetBukiUpPos(float *p0, ANI_CTRL *ani_ctrl);
int motGetBukiDownPos(float *p0, ANI_CTRL *ani_ctrl);
int motGetBukiSpeAPos(float *p0, ANI_CTRL *ani_ctrl);
int motGetBukiSpeBPos(float *p0, ANI_CTRL *ani_ctrl);
int GetMdlHeightPos(void *ani_hndl, float *pos, float *rot, int mdl_no);
void motInitInterpAnime(ANI_CTRL *ani_ctrl, int flame);
void motInterpMatrix(float (*interp)[4], float (*m0)[4], float (*m1)[4], float rate);
void motMatrix2Quaternion(float *q, float (*m)[4]);
void motQuaternion2Matrix(float (*m)[4], float *q);
void motQuaternionSlerp(float *q, float *q1, float *q2, float rate);
void LocalRotMatrixX(float (*m0)[4], float (*m1)[4], float rx);
void LocalRotMatrixY(float (*m0)[4], float (*m1)[4], float ry);
void LocalRotMatrixZ(float (*m0)[4], float (*m1)[4], float rz);
void motInversKinematics(SGDCOORDINATE *cp, float *target, u_int *top_addr, u_char bone_id);
void motGetFrameDataRT(RST_DATA *rst, u_int *top_addr, u_int frame, u_int init_flg);
void motSetHierarchy(SGDCOORDINATE *coord, u_int *top_addr);
u_int *SceneInitAnime(ANI_CTRL *ani_ctrl, u_int *mdl_p, u_int *mot_p, u_int *mim_p, u_int *pkt_p, u_int mdl_no);
u_int *SceneInitOtherAnime(ANI_CTRL *ani_ctrl, u_int *mdl_p, u_int *mot_p, u_int *mim_p, u_int *pkt_p);
void motSetCoordFrame(ANI_CTRL *ani_ctrl, u_int frame);
void SceneSetCoordFrame(ANI_CTRL *ani_ctrl, u_int frame, u_int type);
void SceneSetCoordFrameF(ANI_CTRL *ani_ctrl, float frame, u_int type);
void motSetInvMatrix(float (*m1)[4], float (*m0)[4]);
u_int *motAlign128(u_int *addr);
void motPrintVector(char *str, float *vec);
void sceRotMatrixXYZ(float (*m0)[4], float (*m1)[4], float *rot);
void SetCoordinate(ANI_CTRL *ani_ctrl, float *mdl_pos, float *mdl_rot);
void SetRT2BaseMtx(ANI_CTRL *ani_ctrl, float *mdl_pos, float *mdl_rot);
void motGetLocalWorldMatrix(float (*LocalWorld)[4], u_int *mpk_p, int BoneId);

/* Defined in motion.c, driven entirely by accessory.c.  Entry 0 is the player's
 * own accessory fade, entry 1 the sister's. */
extern ACS_ALPHA plyracs_ctrl[2];       /* sdata 3f32f0 */

/* Twenty rope instances, keyed by furn_id; 0xffff marks a free slot. */
extern ROPE_CTRL rope_ctrl[20];         /* data 336770 */

#endif /* _GRAPHICS_MOTION_MOTION_H */
