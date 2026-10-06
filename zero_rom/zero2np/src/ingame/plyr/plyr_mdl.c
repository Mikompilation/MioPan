/* ==========================================================================
 *  ingame/plyr/plyr_mdl.c
 *
 *  Player model resource management, animation transitions, coordinate
 *  generation, lighting, shadowing, accessory drawing, and neck control.
 *
 *  The original source had a .c suffix but was compiled as C++.  Runtime
 *  resource handles below are native pointers.  The 32-bit relative fields in
 *  SGD and MOT file structures remain 32-bit and are resolved by their own
 *  file-layout wrappers.
 *
 *  Reconstructed from the Feb 6 2004 prototype (SLES_523.84).
 * ======================================================================== */

#include "plyr_mdl.h"

#include <stdio.h>
#include <string.h>

#include "ChrSort.h"
#include "charBB.h"
#include "man_data.h"
#include "player.h"
#include "../../common/utility2.h"
#include "../../main/glob.h"
#include "../../graphics/effect/effect.h"
#include "../../graphics/effect/effect_oth.h"
#include "../ingame_effect.h"                      /* IgEffectRenzFlareDispFlgSet */
#include "../../graphics/graph3d/g3dBoundingVolume.h"
#include "../../graphics/graph3d/g3dGeom.h"
#include "../../graphics/graph3d/g3dLight.h"
#include "../../graphics/graph3d/g3ddbg.h"
#include "../../graphics/graph3d/g3dxVu0.h"
#include "../../graphics/graph3d/gra3d.h"
#include "../../graphics/graph3d/gra3dMisc.h"
#include "../../graphics/graph3d/gra3dSGD.h"
#include "../../graphics/graph3d/gra3dSGDData.h"
#include "../../graphics/mmanage.h"
#include "../../graphics/motion/mdlact.h"
#include "../../graphics/motion/mdldat.h"
#include "../../graphics/motion/mim.h"
#include "../../graphics/motion/motion.h"
#include "../../graphics/obj_draw_ctrl.h"
#include "../../system/eeiop/sndbank.h"
#include "../map/MapDoor.h"
#include "../map/MapDraw.h"
#include "../map/map_rectangle.h"
#include "../event/prg/ev_macro.h"

struct MDL_REQ_SAVE
{
    int mMdlNo;
    int mAnmNo;
    int mBdNo;
    int mSmdlNo;
};

enum LTD_MODE
{
    LTD_MODE_NORMAL = 0,
    LTD_MODE_TIRED = 1
};

class PLYR_PLYR_DATA : public MAN_DATA
{
public:
    void *plyr_cam_mdl_p;
    void *plyr_light_mdl_p;
    u_char plyr_cam_alpha;
    u_char plyr_light_alpha;

    int Setup(int mdl_no, int anm_no, int bd_no, int smdl_no, int acs_no) override;
    int IsReady() override;
    void Release();
    void Initialize();

private:
    unsigned int plyr_req_other_mdl : 1;
    unsigned int plyr_init_ok : 1;

    void Init();
};

int g_iMaxPlayerAlpha = 128;
int g_iMinPlayerAlpha = 0;

static int s_bUseDoorLight;
static PLYR_PLYR_DATA plyr_data;
static MDL_REQ_SAVE plyr_mdl_req_save;

static LTD_MODE ltd_mode;
static int same_priority_count;
static LOOK_TARGET_PRIORITY_MIO plyr_neck_now_priority;
static LOOK_TARGET_PRIORITY_MIO pre_priority;
static LOOK_AT_PARAM plyr_neck_now_param;
static int plyr_neck_flg;
static int plyr_neck_no_registered_cnt;

static int PlayerInterpFlame(ANI_CTRL *ani_ctrl, int now_anm, int next_anm);
static void LeftHandCtrl(SGDCOORDINATE *cp);
static void PlyrNeckFrameInit(void);
static void PlyrNeckInit(void);
static void PlyrNeckMain(void);
static LOOK_AT_PARAM *PlyrNeckGetParam(void);

void PLYR_PLYR_DATA::Init()
{
    plyr_cam_alpha = 0;
    plyr_light_alpha = 0;
    plyr_req_other_mdl = 0;
    plyr_init_ok = 0;
}

void PLYR_PLYR_DATA::Initialize()
{
    InitializeIn();
    Init();
}

int PLYR_PLYR_DATA::Setup(int mdl_no, int anm_no, int bd_no, int smdl_no, int acs_no)
{
    if (plyr_req_other_mdl == 0)
    {
        mmanageReqItemMdl(0);
        mmanageReqItemMdl(1);
        plyr_req_other_mdl = 1;
    }

    int ret = SetupIn(mdl_no, anm_no, bd_no, smdl_no, acs_no);
    if (ret != 0 && plyr_init_ok != 0)
    {
        ChrSortDelete(1);
        plyr_init_ok = 0;
    }
    return ret;
}

int PLYR_PLYR_DATA::IsReady()
{
    void *mdl_p;
    void *anm_p;
    void *smdl_p;

    unsigned int ret = (unsigned int)ReadyIn(&mdl_p, &anm_p, &smdl_p);
    ret &= (unsigned int)mmanageIsReadyItemMdl(0, &plyr_cam_mdl_p, 1);
    ret &= (unsigned int)mmanageIsReadyItemMdl(1, &plyr_light_mdl_p, 1);

    if (ret != 0)
    {
        InitIn(mdl_p, anm_p, smdl_p);
        if (plyr_init_ok == 0)
        {
            ChrSortRegistPlayr();
            plyr_wrk.anime_no = (u_char)GetAniCtrl()->anm.playnum;
            plyr_cam_alpha = 0;
            plyr_light_alpha = 0;
            plyr_init_ok = 1;

            if ((plyr_wrk.cmn_wrk.st.sta & 0x8000) != 0)
            {
                ReqPlayerMim(0x19, 0);
                if (GetAniCtrl()->mdl_no == GetPlyrMdlNo())
                {
                    IgEffectRenzFlareDispFlgSet(1);
                }
            }
        }
    }
    return (int)ret;
}

void PLYR_PLYR_DATA::Release()
{
    if (plyr_init_ok != 0)
    {
        ChrSortDelete(1);
        plyr_init_ok = 0;
    }
    if (plyr_req_other_mdl != 0)
    {
        mmanageClearItemMdl(0);
        mmanageClearItemMdl(1);
        plyr_req_other_mdl = 0;
    }
    ReleaseIn();
}

void plyr_mdlInit(void)
{
    plyr_mdl_req_save.mMdlNo = GetPlyrMdlNo();
    plyr_mdl_req_save.mSmdlNo = 0x10;
    plyr_mdl_req_save.mBdNo = 0xcfd;
    plyr_mdl_req_save.mAnmNo = 0;
    PlyrNeckInit();
    plyr_data.Initialize();
}

void plyr_mdlResetReq(void)
{
    plyr_data.Setup(GetPlyrMdlNo(),
                    plyr_mdl_req_save.mAnmNo,
                    plyr_mdl_req_save.mBdNo,
                    plyr_mdl_req_save.mSmdlNo,
                    GetPlyrAcsNo());
}

void SetupPlyrMdl(int mdl_no, int anm_no, int smdl_no, int acs_no)
{
    plyr_mdl_req_save.mBdNo = 0xcfd;
    plyr_mdl_req_save.mMdlNo = mdl_no;
    plyr_mdl_req_save.mAnmNo = anm_no;
    plyr_mdl_req_save.mSmdlNo = smdl_no;
    plyr_data.Setup(mdl_no, anm_no, 0xcfd, smdl_no, acs_no);
}

int IsReadyPlyrMdl(void)
{
    return plyr_data.IsReady();
}

void ReleasePlyrMdl(void)
{
    plyr_data.Release();
}

void ReqPlayerMim(int no, int rev)
{
    ANI_CTRL *ani_ctrl = plyr_data.GetAniCtrl();
    if (ani_ctrl != nullptr)
    {
        mimRequestNum(ani_ctrl, no, (u_char)rev);
    }
}

void ReqPlayerMimContinue(int no, int rev)
{
    ANI_CTRL *ani_ctrl = plyr_data.GetAniCtrl();
    if (ani_ctrl != nullptr)
    {
        mimRequestNumContinue(ani_ctrl, no, (u_char)rev);
    }
}

void StopPlayerMim(int no)
{
    ANI_CTRL *ani_ctrl = plyr_data.GetAniCtrl();
    if (ani_ctrl != nullptr)
    {
        mimStopNum(ani_ctrl, no);
    }
}

int IsPlayerMimParts(int no)
{
    return mimIsUseParts(plyr_data.GetAniCtrl(), no) != 0;
}

void plyr_mdlSetSave(MC_SAVE_DATA *data)
{
    data->size = sizeof(plyr_mdl_req_save);
    data->addr = (u_char *)&plyr_mdl_req_save;
}

u_short GetPlyrFtype(void)
{
    ANI_CTRL *ani_ctrl = plyr_data.GetAniCtrl();
    return ani_ctrl != nullptr ? ani_ctrl->ftype : 0;
}

static int IsPlayerWalkAnime(int anime_no)
{
    return (anime_no >= 6 && anime_no <= 9) ||
           (anime_no >= 12 && anime_no <= 15);
}

static int IsPlayerIdleAnime(int anime_no)
{
    return anime_no == 1 || anime_no == 10 || anime_no == 11;
}

static int IsPlayerFinderAnime(int anime_no)
{
    return anime_no >= 0x18 && anime_no <= 0x39;
}

void ReqPlayerAnime(u_char flame)
{
    if (IsReadyPlyrMdl() == 0)
    {
        return;
    }

    ANI_CTRL *ani_ctrl = plyr_data.GetAniCtrl();
    int now_anm = ani_ctrl->anm.playnum;
    int next_anm = plyr_wrk.anime_no;

    if (next_anm == now_anm)
    {
        printf("The same anime is requested. In ReqPlayerAnime!!!!!!!!\n");
        return;
    }

    int interp = PlayerInterpFlame(ani_ctrl, now_anm, next_anm);
    int interp_flame = interp == -1 ? (int)flame : (interp & 0xff);

    mimInitLoop(ani_ctrl);

    if (IsPlayerWalkAnime(next_anm) && !IsPlayerWalkAnime(now_anm))
    {
        mimRequestNum(ani_ctrl, 0x16, 0);
        if ((plyr_wrk.cmn_wrk.st.sta & 0x8000) == 0)
        {
            mimRequestNum(ani_ctrl, 0x15, 0);
        }
    }
    else if (!IsPlayerWalkAnime(next_anm) && IsPlayerWalkAnime(now_anm))
    {
        mimRequestNum(ani_ctrl, 0x16, 1);
        if ((plyr_wrk.cmn_wrk.st.sta & 0x8000) == 0)
        {
            mimRequestNum(ani_ctrl, 0x15, 1);
        }
    }

    if (IsPlayerIdleAnime(next_anm) && !IsPlayerIdleAnime(now_anm))
    {
        mimRequestNum(ani_ctrl, 0x18, 0);
        if ((plyr_wrk.cmn_wrk.st.sta & 0x8000) == 0)
        {
            mimRequestNum(ani_ctrl, 0x17, 0);
        }
    }
    else if (!IsPlayerIdleAnime(next_anm) && IsPlayerIdleAnime(now_anm))
    {
        mimRequestNum(ani_ctrl, 0x18, 1);
        if ((plyr_wrk.cmn_wrk.st.sta & 0x8000) == 0)
        {
            mimRequestNum(ani_ctrl, 0x17, 1);
        }
    }

    if ((plyr_wrk.cmn_wrk.st.sta & 0x8000) != 0)
    {
        if (!IsPlayerFinderAnime(next_anm) && IsPlayerFinderAnime(now_anm))
        {
            mimRequestNum(ani_ctrl, 0x19, 0);
            plyr_data.plyr_light_alpha = 0;
            IgEffectRenzFlareDispFlgSet(1);
        }
        else if (IsPlayerFinderAnime(next_anm) && !IsPlayerFinderAnime(now_anm))
        {
            mimRequestNum(ani_ctrl, 0x19, 1);
            IgEffectRenzFlareDispFlgSet(0);
        }
    }

    if (next_anm == 0x30 && now_anm != 0x30)
    {
        plyr_data.plyr_cam_alpha = 0;
    }

    motSetAnime(ani_ctrl, anm_tbl[ani_ctrl->anm_no].ani, next_anm);
    motInitInterpAnime(ani_ctrl, interp_flame);
}

static int PlayerInterpFlame(ANI_CTRL *ani_ctrl, int now_anm, int next_anm)
{
    int ani_run = 0;
    int mot_run = 0;
    float tmp;
    float base;

    if (PlyrOutsideCheck() != 0)
    {
        ani_run = 6;
        mot_run = 6;
    }

    if (now_anm == 0 && next_anm >= 2 && next_anm <= 4)
    {
        return 10;
    }

    if ((unsigned int)(now_anm - 2) < 3 && next_anm == 0)
    {
        if (ani_ctrl->mot.play_id == 3 || ani_ctrl->mot.play_id == 5)
        {
            tmp = (float)ani_ctrl->mot.cnt / (float)ani_ctrl->mot.all_cnt;
        }
        else if (ani_ctrl->mot.play_id == 2 || ani_ctrl->mot.play_id == 4)
        {
            tmp = (float)(ani_ctrl->mot.all_cnt - ani_ctrl->mot.cnt) /
                  (float)ani_ctrl->mot.all_cnt;
        }
        else
        {
            tmp = 0.0f;
        }
        tmp = tmp * 5.0f + 10.0f;
    }
    else
    {
        int run_start = ani_run + 6;

        if (now_anm == 0 && next_anm >= run_start && next_anm <= ani_run + 8)
        {
            return 10;
        }

        if (now_anm >= run_start && now_anm <= ani_run + 8 && next_anm == 0)
        {
            if (ani_ctrl->mot.play_id == (unsigned int)(mot_run + 7) ||
                ani_ctrl->mot.play_id == (unsigned int)(mot_run + 9))
            {
                tmp = (float)ani_ctrl->mot.cnt / (float)ani_ctrl->mot.all_cnt;
            }
            else if (ani_ctrl->mot.play_id == (unsigned int)(mot_run + 6) ||
                     ani_ctrl->mot.play_id == (unsigned int)(mot_run + 8))
            {
                tmp = (float)(ani_ctrl->mot.all_cnt - ani_ctrl->mot.cnt) /
                      (float)ani_ctrl->mot.all_cnt;
            }
            else
            {
                tmp = 0.0f;
            }
            tmp = tmp * 5.0f + 5.0f;
        }
        else
        {
            if ((unsigned int)(now_anm - 2) < 4 &&
                next_anm >= run_start && next_anm <= ani_run + 9)
            {
                tmp = ((float)(ani_ctrl->mot.all_cnt - ani_ctrl->mot.cnt) /
                       (float)ani_ctrl->mot.all_cnt) * 5.0f + 5.0f;
                goto finish;
            }

            if (now_anm >= run_start && now_anm <= ani_run + 9 &&
                next_anm >= 2 && next_anm <= 5)
            {
                tmp = ((float)(ani_ctrl->mot.all_cnt - ani_ctrl->mot.cnt) /
                       (float)ani_ctrl->mot.all_cnt) * 5.0f + 5.0f;
                goto finish;
            }

            if (now_anm >= run_start && now_anm <= ani_run + 9 &&
                next_anm >= 0x41 && next_anm <= 0x42)
            {
                if (ani_ctrl->mot.play_id == (unsigned int)(mot_run + 7) ||
                    ani_ctrl->mot.play_id == (unsigned int)(mot_run + 9))
                {
                    tmp = (float)(ani_ctrl->mot.all_cnt - ani_ctrl->mot.cnt) /
                          (float)(ani_ctrl->mot.all_cnt * 2);
                }
                else if (ani_ctrl->mot.play_id == (unsigned int)(mot_run + 6) ||
                         ani_ctrl->mot.play_id == (unsigned int)(mot_run + 8))
                {
                    int twice = ani_ctrl->mot.all_cnt * 2;
                    tmp = (float)(twice - ani_ctrl->mot.cnt) / (float)twice;
                }
                else
                {
                    tmp = 0.0f;
                }
                tmp = tmp * 6.0f + 2.0f;
            }
            else
            {
                if ((unsigned int)(now_anm - 0x41) < 2 &&
                    next_anm >= run_start && next_anm <= ani_run + 9)
                {
                    return 10;
                }
                if ((unsigned int)(next_anm - 0x18) > 0x17)
                {
                    return -1;
                }
                return 2;
            }
        }
    }

finish:
    base = tmp;
    int flame = (int)base;
    return flame < 0 ? 0 : flame;
}

void plyr_mdlMotionWork(void)
{
    PlyrNeckMain();
}

void plyr_mdlGetMATRIX(float (*mtx)[4], int bone_no)
{
    if (IsReadyPlyrMdl() != 0)
    {
        SGDCOORDINATE *cp = plyr_data.GetAniCtrl()->base_p->coordp;
        sceVu0CopyMatrix(mtx, cp[bone_no].matLocalWorld);
    }
}

void CalcGirlCoord(int pause_flg)
{
    (void)pause_flg;

    if (IsReadyPlyrMdl() == 0)
    {
        return;
    }

    ANI_CTRL *ani_ctrl = plyr_data.GetAniCtrl();
    HeaderSection *hs = ani_ctrl->base_p;
    SGDCOORDINATE *cp = hs->coordp;

    MrecSetRegBuffID((int)(short)plyr_wrk.cmn_wrk.floor,
                     plyr_wrk.cmn_wrk.mbox.pos, 0);

    u_char coord_state = motSetCoord(ani_ctrl, 0xff, 0);
    if (coord_state == 1)
    {
        plyr_wrk.cmn_wrk.st.sta |= 0x2000;
    }
    else if (coord_state == 2)
    {
        plyr_wrk.cmn_wrk.st.sta |= 0x4000;
    }

    plyr_data.plyr_light_alpha += 10;
    if (plyr_data.plyr_light_alpha > 0x80)
    {
        plyr_data.plyr_light_alpha = 0x80;
    }
    plyr_data.plyr_cam_alpha += 10;
    if (plyr_data.plyr_cam_alpha > 0x80)
    {
        plyr_data.plyr_cam_alpha = 0x80;
    }

    u_int frame = motGetNowFrame(&ani_ctrl->mot);
    movGetMoveval(plyr_wrk.spd, plyr_wrk.old_spd, ani_ctrl, frame >> 1, 0.0f);
    mimSetVertex(ani_ctrl);

    sceVu0UnitMatrix(cp->matCoord);
    cp->matCoord[0][0] = 25.0f;
    cp->matCoord[1][1] = 25.0f;
    cp->matCoord[2][2] = 25.0f;

    const float pi = 3.1415927410125732f;
    const float two_pi = 6.2831854820251465f;
    float grot = plyr_wrk.cmn_wrk.mbox.rot[1] + pi;
    if (grot > pi)
    {
        grot -= two_pi;
    }

    sceVu0RotMatrixX(cp->matCoord, cp->matCoord, pi);
    sceVu0RotMatrixY(cp->matCoord, cp->matCoord, grot);
    g3dxVu0CopyVector(cp->matCoord[3], plyr_wrk.cmn_wrk.mbox.pos);
    cp->matCoord[3][3] = 1.0f;

    sgdCalcBoneCoordinate(cp, (int)hs->blocks - 1);
    motLookAtCtrl(ani_ctrl, PlyrNeckGetParam());
    LeftHandCtrl(cp);
    sgdCalcBoneCoordinate(cp, (int)hs->blocks - 1);

    GetMdlWaistPos(plyr_wrk.bwp, ani_ctrl, (u_short)ani_ctrl->mdl_no);
    GetMdlNeckPos(plyr_wrk.cmn_wrk.headpos, ani_ctrl, (u_short)ani_ctrl->mdl_no);
    GetPlyrAcsLightPos(plyr_wrk.spot_pos, ani_ctrl);
}

void playerUseDoorLight(int b)
{
    s_bUseDoorLight = b;
}

void playerSetLight(const float *vPosition,
                    const GRA3DEMULATIONLIGHTDATACREATIONDATA *pData)
{
    if (pData == nullptr)
    {
        G3DASSERT(pData != nullptr, "");
        return;
    }

    GRA3DLIGHTDATA LD;
    GRA3DLIGHTDATA *pLDSrc;

    if (s_bUseDoorLight == 0)
    {
        pLDSrc = MapDrawGetLightPtr(GetPlyrAreaNo());
    }
    else
    {
        pLDSrc = MapDoorGetLight();
    }

    gra3dGenerateLightDataToChar(&LD, pLDSrc, (GRA3DEMULATIONLIGHTDATACREATIONDATA *)pData);

    if (gra3dIsSpecialLightActive() != 0)
    {
        GRA3DLIGHTID lightId = gra3dGetProjectorSpotId();
        LD.aLight[(int)lightId] = gra3dGetProjectorSpot();
        LD.aStatus[(int)lightId].bEnable = 1;
    }

    gra3dEmulateLightData(&LD, &LD, (float *)vPosition, pData->fMaplightScale);

    LD.vAmbient[0] = debug_var.pl_amb;
    LD.vAmbient[1] = debug_var.pl_amb;
    LD.vAmbient[2] = debug_var.pl_amb;
    LD.vAmbient[3] = 0.0f;

    if (EffectThunderLightGetLightningFlg() != 0)
    {
        EffectThunderLightGetG3dLight(&LD.aLight[LID_DIRECTIONAL_1]);
        LD.aStatus[LID_DIRECTIONAL_1].bEnable = 1;
        LD.aStatus[LID_DIRECTIONAL_1].bEnableToChar = 1;
        LD.aStatus[LID_DIRECTIONAL_1].bEnableToShadow = 1;
    }

    if (pData->bEnableStaticDirLight != 0)
    {
        G3DLIGHT &light = LD.aLight[LID_DIRECTIONAL_2];
        g3dutilSetLightDefault(&light, G3DLIGHT_DIRECTIONAL);

        GRA3DCAMERA *camera = gra3dGetCamera();
        light.vDirection[0] = -camera->matCoord[2][0];
        light.vDirection[1] = -camera->matCoord[2][1];
        light.vDirection[2] = -camera->matCoord[2][2];
        light.vDirection[3] = -camera->matCoord[2][3];
        g3dxVu0CopyVector(light.vDiffuse, (float*)pData->vStaticDirLightColor);
        LD.aStatus[LID_DIRECTIONAL_2].bEnable = 1;
    }

    gra3dSetLightData(&LD, nullptr);
}

GRA3DEMULATIONLIGHTDATACREATIONDATA *_GetEmulationLightdataCreationDataRef(void)
{
    static GRA3DEMULATIONLIGHTDATACREATIONDATA aELDCD_MIO[2];
    static int initialized;

    if (plyr_mdlGetANI_CTRL() == nullptr)
    {
        return nullptr;
    }

    if (initialized == 0)
    {
        memset(aELDCD_MIO, 0, sizeof(aELDCD_MIO));

        aELDCD_MIO[PFT_HAND].vStaticDirLightColor[0] = 0.06f;
        aELDCD_MIO[PFT_HAND].vStaticDirLightColor[1] = 0.06f;
        aELDCD_MIO[PFT_HAND].vStaticDirLightColor[2] = 0.06f;
        aELDCD_MIO[PFT_HAND].fAngleScale = 1.0f;
        aELDCD_MIO[PFT_HAND].fDiffuseScale = 1.0f;
        aELDCD_MIO[PFT_HAND].fMaplightScale = plyr_wrk.maplight_scale;
        aELDCD_MIO[PFT_HAND].bEnableSelfreflection = 1;
        aELDCD_MIO[PFT_HAND].bEmulateSelfreflection = 1;

        /* .rodata 3ee800 / 3ee7fc: 0.3, 0.1, 0.1 -- the earlier pass had 0.22
         * for the green and blue.  Dead either way, because the tail below
         * rewrites all three from debug_var.fStaticDirLightColStep* on every
         * call (DebugInit seeds those with the same 0.3 / 0.1 / 0.1). */
        aELDCD_MIO[PFT_STEP].vStaticDirLightColor[0] = 0.3f;
        aELDCD_MIO[PFT_STEP].vStaticDirLightColor[1] = 0.1f;
        aELDCD_MIO[PFT_STEP].vStaticDirLightColor[2] = 0.1f;
        aELDCD_MIO[PFT_STEP].fMaplightScale = plyr_wrk.maplight_scale;
        aELDCD_MIO[PFT_STEP].bEnableSelfreflection = 1;
        aELDCD_MIO[PFT_STEP].bEmulateSelfreflection = 1;
        aELDCD_MIO[PFT_STEP].bEnableStaticDirLight = 1;

        initialized = 1;
    }

    aELDCD_MIO[PFT_HAND].vStaticDirLightColor[0] = debug_var.sis_para_r;
    aELDCD_MIO[PFT_HAND].vStaticDirLightColor[1] = debug_var.sis_para_g;
    aELDCD_MIO[PFT_HAND].vStaticDirLightColor[2] = debug_var.sis_para_b;
    aELDCD_MIO[PFT_HAND].bEnableStaticDirLight =
        (plyr_wrk.cmn_wrk.st.sta & 0x8000) == 0;

    aELDCD_MIO[PFT_STEP].vStaticDirLightColor[0] = debug_var.fStaticDirLightColStepR;
    aELDCD_MIO[PFT_STEP].vStaticDirLightColor[1] = debug_var.fStaticDirLightColStepG;
    aELDCD_MIO[PFT_STEP].vStaticDirLightColor[2] = debug_var.fStaticDirLightColStepB;

    return &aELDCD_MIO[(int)playerGetFlashlightType()];
}

int playerCalcAlpha(const ANI_CTRL *pAC)
{
    if (pAC == nullptr || pAC->base_p == nullptr || pAC->base_p->coordp == nullptr)
    {
        G3DASSERT(pAC != nullptr && pAC->base_p != nullptr &&
                  pAC->base_p->coordp != nullptr, "");
        return g_iMinPlayerAlpha;
    }

    float avBBWorld[8][4];
    float ellipse[4][4];
    float avRet[2][4];
    float vDir[4];

    charbbGet(avBBWorld, pAC, pAC->base_p->coordp.get()->matCoord);
    g3dbvInnerEllipseFromVertices(ellipse, avBBWorld);

    GRA3DCAMERA *camera = gra3dGetCamera();
    sceVu0SubVector(vDir, ellipse[3], camera->matCoord[3]);
    sceVu0Normalize(vDir, vDir);
    g3dCalcIntersectionEllipseAndLine(avRet, ellipse, ellipse[3], vDir);

    float boundary_distance = g3dxVu0Length3(ellipse[3], avRet[0]);
    float camera_distance = g3dxVu0Length3(ellipse[3], camera->matCoord[3]);
    int alpha = g_iMinPlayerAlpha;

    if (boundary_distance <= camera_distance)
    {
        alpha = g_iMaxPlayerAlpha;
        if (camera_distance <= boundary_distance + 50.0f)
        {
            alpha = (int)(((camera_distance - boundary_distance) *
                           (float)(g_iMaxPlayerAlpha - g_iMinPlayerAlpha)) /
                          50.0f + (float)g_iMinPlayerAlpha);
        }
    }
    return alpha;
}

void playerDrawShadow(void)
{
    ANI_CTRL *pAC = plyr_mdlGetANI_CTRL();
    if (pAC == nullptr)
    {
        return;
    }

    if (pAC->base_p == nullptr || pAC->base_p->coordp == nullptr)
    {
        G3DASSERT(pAC->base_p != nullptr && pAC->base_p->coordp != nullptr, "");
        return;
    }

    SGDCOORDINATE *pCoord = pAC->base_p->coordp;
    float avBBWorld[8][4];
    charbbGet(avBBWorld, pAC, pCoord->matCoord);

    if (plyr_data.IsLocked() == 0)
    {
        gra3dDrawSGDShadowCharacter(plyr_mdlGetShadowANI_CTRL(), pCoord,
                                     avBBWorld,
                                     _GetEmulationLightdataCreationDataRef());
    }
}

void PlayerDrawLock(void)
{
    plyr_data.DrawLock();
}

void PlayerDrawUnlock(void)
{
    plyr_data.DrawUnlock();
}

void DrawGirl(void)
{
    if (IsReadyPlyrMdl() == 0 || GetPlyrDrawFLG() == 0 || plyr_data.IsLocked() != 0)
    {
        return;
    }

    gra3dLightEnablePush();

    float work_amb[4];
    g3dxVu0CopyVector(work_amb, gra3dGetAmbientRef());

    ANI_CTRL *ani_ctrl = plyr_data.GetAniCtrl();
    HeaderSection *hs = ani_ctrl->base_p;
    float tgirlbox[8][4];
    charbbGet(tgirlbox, ani_ctrl, hs->coordp->matCoord);

    GRA3DEMULATIONLIGHTDATACREATIONDATA *pData =
        _GetEmulationLightdataCreationDataRef();

    if (CheckModelBoundingBox(tgirlbox) != 0)
    {
        int iAlpha = g_iMaxPlayerAlpha;
        playerSetLight(plyr_wrk.bwp, pData);

        MioPan_ProbeDumpCharLights("gameplay", plyr_wrk.fl.vPosition); /* MIOPAN_PROBE */

        if (PlayerModeIsFinder() == 0)
        {
            iAlpha = playerCalcAlpha(ani_ctrl);
        }

        plyr_data.AccessoryDraw(iAlpha);

        if (gra3dIsMonotoneDrawEnable() == 0)
        {
            SendEneVram(ani_ctrl->mdl_p, 0x2bc0);
        }
        else
        {
            SendEneVramMono(ani_ctrl->mdl_p, 0x2bc0, ani_ctrl->bwc_p);
        }

        ManmdlSetAlpha(hs, (u_char)iAlpha);
        _gra3dDrawSGD((SGDFILEHEADER *)hs, SRT_REALTIME, nullptr, -1);
        DrawGirlSubObj(ani_ctrl->mpk_p, (u_char)iAlpha);

        if ((plyr_wrk.cmn_wrk.st.sta & 0x8000) != 0 &&
            GetSynchroModeFlg() == 0 &&
            !IsPlayerFinderAnime(ani_ctrl->anm.playnum))
        {
            HeaderSection *item =
                (HeaderSection *)GetItemSgdAddr((int *)plyr_data.plyr_light_mdl_p);
            SendItemVram((u_int *)plyr_data.plyr_light_mdl_p, 0);
            ManmdlSetAlpha(item, plyr_data.plyr_light_alpha);
            ManItemSGDDraw(item, ani_ctrl, 1);
        }

        if (ani_ctrl->anm.playnum == 0x30 || ani_ctrl->anm.playnum == 0x31)
        {
            HeaderSection *item =
                (HeaderSection *)GetItemSgdAddr((int *)plyr_data.plyr_cam_mdl_p);
            SendItemVram((u_int *)plyr_data.plyr_cam_mdl_p, 0);
            ManmdlSetAlpha(item, plyr_data.plyr_cam_alpha);
            ManItemSGDDraw(item, ani_ctrl, 0);
        }

        _SetPREVIOUSTRI2PRIM(nullptr);
    }

    gra3dSetAmbient(work_amb);
    gra3dLightEnablePop();
    gra3dApplyLight();
}

static void LeftHandCtrl(SGDCOORDINATE *cp)
{
    const float hand_rot_scale = 0.22f;
    const float thirty_degrees = 0.5235987901687622f;
    const float forty_degrees = 0.6981317400932312f;

    float rx = (plyr_wrk.spot_rot[1] * hand_rot_scale) / thirty_degrees;
    float target[4];
    sceVu0CopyVector(target, cp[6].matLocalWorld[3]);
    target[1] -= (plyr_wrk.spot_rot[0] * 60.0f) / forty_degrees;

    if (plyr_wrk.cmn_wrk.mode != 6)
    {
        motInversKinematics(cp, target, plyr_data.GetAniCtrl()->mot.dat, 6);
        LocalRotMatrixX(cp[11].matCoord, cp[11].matCoord, rx);
    }
}

ANI_CTRL *plyr_mdlGetANI_CTRL(void)
{
    return plyr_data.GetAniCtrl();
}

ANI_CTRL *plyr_mdlGetShadowANI_CTRL(void)
{
    return plyr_data.GetShadowAniCtrl();
}

static void PlyrNeckFrameInit(void)
{
    plyr_neck_now_priority = LTP_MIO_LEAST;
}

static void PlyrNeckInit(void)
{
    SetPlyrNeckFlg(1);
    ltd_mode = LTD_MODE_NORMAL;
    pre_priority = LTP_MIO_LEAST;
    same_priority_count = 0;
    plyr_neck_no_registered_cnt = 0;
    PlyrNeckFrameInit();
}

int PlyrNeckRegisterTarget(LOOK_AT_PARAM *param, LOOK_TARGET_PRIORITY_MIO priority)
{
    if (IsReadyPlyrMdl() == 0)
    {
        return 0;
    }

    if ((unsigned int)priority < (unsigned int)plyr_neck_now_priority)
    {
        if (IsTargetInSight(plyr_data.GetAniCtrl(), param->pos) == 0)
        {
            return 0;
        }

        plyr_neck_now_param = *param;
        plyr_neck_now_priority = priority;
        return 1;
    }
    return 0;
}

void SetPlyrNeckFlg(int flg)
{
    plyr_neck_flg = flg;
}

static void PlyrNeckMain(void)
{
    plyr_neck_now_param.enable = plyr_neck_flg;

    if (ltd_mode == LTD_MODE_NORMAL)
    {
        if (pre_priority == plyr_neck_now_priority)
        {
            same_priority_count += 2;
        }
        else
        {
            same_priority_count = 0;
        }

        if (plyr_neck_now_priority == LTP_MIO_LEAST)
        {
            plyr_neck_now_param.enable = 0;
            plyr_neck_no_registered_cnt += 2;
        }
        else
        {
            plyr_neck_no_registered_cnt = 0;
        }
    }
    else if (ltd_mode == LTD_MODE_TIRED)
    {
        same_priority_count += 2;
        ltd_mode = (LTD_MODE)(same_priority_count < 0x709);
        if (pre_priority != plyr_neck_now_priority)
        {
            ltd_mode = LTD_MODE_NORMAL;
        }
    }

    pre_priority = plyr_neck_now_priority;
    PlyrNeckFrameInit();
}

static LOOK_AT_PARAM *PlyrNeckGetParam(void)
{
    return &plyr_neck_now_param;
}

int plyr_mdlBankPlay(int no, int effect, int loop, int fade_time,
                     SND_3D_SET *s3d, int vol, int pitch)
{
    int bank_no = plyr_data.GetSndBankNo();
    if (bank_no != -1)
    {
        return SndBankPlay(bank_no, no, effect, loop, vol, pitch, fade_time, s3d);
    }

    PRINT_ASSERT("plyr_bank_id is Illegal");
    return 0x300000;
}

int plyr_mdlBankIsLoopSnd(int no)
{
    int bank_no = plyr_data.GetSndBankNo();

    if (bank_no != -1)
    {
        return SndBankIsLoopSnd(bank_no, no);
    }

    PRINT_ASSERT("plyr_bank_id is Illegal");

    return 0;
}

int GetPlyrMdlNo(void)
{
    return GameCostume.mPlyrMdlNo;
}

void SetPlyrMdlNo(int iMdlNo)
{
    GameCostume.mPlyrMdlNo = iMdlNo;
}

int GetPlyrAcsNo(void)
{
    return GameCostume.mPlyrAcsNo;
}

void SetPlyrAcsNo(int iMdlNo)
{
    GameCostume.mPlyrAcsNo = iMdlNo;
}

int GetSisterMdlNo(void)
{
    return GameCostume.mSisterMdlNo;
}

void SetSisterMdlNo(int iMdlNo)
{
    GameCostume.mSisterMdlNo = iMdlNo;
}

int GetSisterAcsNo(void)
{
    return GameCostume.mSisterAcsNo;
}

void SetSisterAcsNo(int iAcsNo)
{
    GameCostume.mSisterAcsNo = iAcsNo;
}

void CostumeSetSave(MC_SAVE_DATA *data)
{
    data->size = sizeof(GameCostume);
    data->addr = (u_char *)&GameCostume;
}
