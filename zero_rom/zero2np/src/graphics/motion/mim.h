/* ==========================================================================
 *  graphics/motion/mim.h
 *
 *  MIME vertex-deformation interface.
 *
 *  Reconstructed from the Feb 6 2004 prototype (SLES_523.84).
 * ======================================================================== */

#ifndef _GRAPHICS_MOTION_MIM_H
#define _GRAPHICS_MOTION_MIM_H

#include "mdlwork.h"

u_int *mimInitMimeCtrl(MIME_CTRL *m_ctrl, MIME_DAT *mdat, u_int *mim_p, u_int *mdl_p, u_int *tmp_p, u_int *m_num);
u_int mimGetBufSize(u_int *mim_p, u_int *mdl_p);
void mimInitAcsCtrl(ANI_CTRL *ani_ctrl, u_short mdl_no);
void mimInitLoop(ANI_CTRL *ani_ctrl);
void mimRequest(MIME_CTRL *m_ctrl, u_char rev);
void mimRequestLastFrame(MIME_CTRL *m_ctrl, u_char rev);
int mimRequestNum(ANI_CTRL *ani_ctrl, int num, u_char rev);
int mimLoopRequestNum(ANI_CTRL *ani_ctrl, int num, u_char rev);
int mimRequestNumContinue(ANI_CTRL *ani_ctrl, int num, u_char rev);
int mimIsUseParts(ANI_CTRL *ani_ctrl, int no);
int mimIsReqMimNum(ANI_CTRL *ani_ctrl, int no);
int mimStopNum(ANI_CTRL *ani_ctrl, int num);
int mimEndStopNum(ANI_CTRL *ani_ctrl, int num);
void mimStop(MIME_CTRL *m_ctrl);
void mimSetReso(MIME_CTRL *m_ctrl, int reso);
void mimClearVertex(MIME_CTRL *m_ctrl);
void mimCalcVertex(MIME_CTRL *m_ctrl, WMIM_CTRL *wmim, u_char clear_vtx_flg);
void mimSetMimeCtrl(MIME_CTRL *m_ctrl, MIME_DAT *mdat, u_int furn_id, u_int parts_id);
u_int *mimSetMimeDat(MIME_DAT *mdat, u_int *mim_p, u_int *tmp_buf, u_int *mdl_p);
void mimSetOriVertex(MIME_DAT *mdat);
void mimSetVertex(ANI_CTRL *ani_ctrl);
void mimClearToScene(void);
void mimClearAllVertex(ANI_CTRL *ani_ctrl);
u_char mimAddFrame(MIME_CTRL *m_ctrl, u_int *dat);
void SceneMimSetVertex(ANI_CTRL *ani_ctrl, u_int frame);
u_int mimGetFrameNum(u_int *mim_top);
u_int mimGetKeymdlNum(u_int *mim_top);
u_int mimGetPartsNo(u_int *mim_top);
u_int mimGetFlg(u_int *mim_top);
u_char *mimGetWeightName(u_int *mim_top);
void mimAddressMapping(u_int *top_addr);
void mimInitWeight(ANI_CTRL *ani_ctrl);
void mimWeightCtrl(ANI_CTRL *ani_ctrl, u_int weight_id, float scale);

#endif /* _GRAPHICS_MOTION_MIM_H */
