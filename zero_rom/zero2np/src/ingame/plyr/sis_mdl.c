/* ==========================================================================
 *  ingame/plyr/sis_mdl.c
 *
 *  Sister ("Mayu") model resource management, animation transitions,
 *  coordinate generation, lighting, shadowing and neck control -- the
 *  companion of plyr_mdl.c, built on the same MAN_DATA base.
 *
 *  SIS_DATA adds two things on top of MAN_DATA: the ChrSort registration
 *  latch (plyr_ready_sort, named after the player copy it was cloned from)
 *  and a reference on SIS_ALG_OBJ, the sister behaviour script the algorithm
 *  interpreter in sis_algo.c runs out of sis_mdlGetAlgAdrs().
 *
 *  The original source had a .c suffix but was compiled as C++.  Every
 *  SIS_DATA member is defined inside the class body, which is why the ROM
 *  emits them into .gnu.linkonce.t sections *and* inlines them into every
 *  caller; that is reproduced here so the call graph matches.
 *
 *  Reconstructed from the Feb 6 2004 prototype (SLES_523.84).
 * ======================================================================== */

#include "sis_mdl.h"

#include <float.h>
#include <stdint.h>
#include <stdio.h>

#include "ChrSort.h"
#include "charBB.h"
#include "man_data.h"
#include "player.h"
#include "plyr_mdl.h"
#include "sister.h"
#include "../../common/ol_load.h"
#include "../../common/utility2.h"
#include "../../common/variable.h"
#include "../../graphics/graph3d/g3ddbg.h"
#include "../../graphics/graph3d/g3dxVu0.h"
#include "../../graphics/graph3d/gra3d.h"
#include "../../graphics/graph3d/gra3dMisc.h"
#include "../../graphics/graph3d/gra3dSGD.h"
#include "../../graphics/graph3d/gra3dSGDData.h"
#include "../../graphics/motion/accessory.h"
#include "../../graphics/motion/mdlact.h"
#include "../../graphics/motion/mdldat.h"
#include "../../graphics/motion/mim.h"
#include "../../graphics/motion/motion.h"
#include "../../graphics/obj_draw_ctrl.h"
#include "../../system/eeiop/cddat.h"
#include "../../system/eeiop/sndbank.h"
#include "../event/prg/ev_macro.h"

/* Same two-state "she got bored of looking at that" machine plyr_mdl.c has.
 * globals.txt types the variable itself as a plain int here, not as the
 * enum, so the storage below stays int. */
enum LTD_MODE
{
    LTD_MODE_NORMAL = 0,
    LTD_MODE_TIRED  = 1
};

/* Frames (counted two at a time) a target has to stay top priority before
 * Mayu gives up on it. */
#define SIS_NECK_TIRED_COUNT 0x709

class SIS_DATA : public MAN_DATA                                        /* 55 */
{
public:
    SIS_DATA()
    {
        Initialize();                                                   /*  60 */
    }

    int Setup(int mdl_no, int anm_no, int bd_no, int smdl_no, int iAcsNo) override
    {
        int ret = 0;                                                    /*  65 */

        if (ane_alg_req == 0)                                           /*  69 */
        {
            ol_load.Req(SIS_ALG_OBJ);                                   /*  70 */
            ret = 1;
            ane_alg_req = 1;                                            /*  74 */
        }

        if (SetupIn(mdl_no, anm_no, bd_no, smdl_no, iAcsNo) != 0)       /*  77 */
        {
            ret = 1;
        }
        return ret;
    }

    int IsReady() override
    {
        void *mdl_p;
        void *anm_p;
        void *smdl_p;

        int ret = ReadyIn(&mdl_p, &anm_p, &smdl_p);                     /*  92 */
        ret &= (int)ol_load.IsReady(SIS_ALG_OBJ, &ane_alg_adrs);        /*  93 */

        if (ret != 0)                                                   /*  98 */
        {
            Init(mdl_p, anm_p, smdl_p);
        }
        return ret;                                                     /*  85 */
    }

    void Release()
    {
        if (plyr_ready_sort != 0)                                       /* 105 */
        {
            ChrSortDelete(0);                                           /* 106 */
            plyr_ready_sort = 0;                                        /* 107 */
        }
        if (ane_alg_req != 0)                                           /* 109 */
        {
            ol_load.Clear(SIS_ALG_OBJ);                                 /* 110 */
            ane_alg_req = 0;                                            /* 113 */
        }
        ReleaseIn();                                                    /* 116 */
    }

    void *GetAlgAdrs() { return ane_alg_adrs; }                         /* 119 */

    void Initialize()
    {
        InitializeIn();                                                 /* 125 */
        ane_alg_adrs = nullptr;                                         /* 126 */
        Init();
    }

private:
    /* 0x30:0 */ unsigned int plyr_ready_sort : 1;   /* registered with ChrSort */
    /* 0x30:1 */ unsigned int ane_alg_req     : 1;   /* holds a SIS_ALG_OBJ ref */
    /* 0x34   */ void        *ane_alg_adrs;          /* int in the ROM -- a
                                                      * 32-bit pointer there  */

    void Init()
    {
        ane_alg_req     = 0;                                            /* 129 */
        plyr_ready_sort = 0;
    }

    void Init(void *mdl_p, void *anm_p, void *smdl_p)
    {
        InitIn(mdl_p, anm_p, smdl_p);                                   /* 140 */

        if (plyr_ready_sort == 0)                                       /* 143 */
        {
            ChrSortRegistSis();                                         /* 145 */
            sis_wrk.anime_no = (u_char)GetAniCtrl()->anm.playnum;       /* 146 */
            plyr_ready_sort = 1;                                        /* 147 */
        }
    }
};

static SIS_DATA sis_data;

static int                     ltd_mode;
static int                     same_priority_count;
static LOOK_TARGET_PRIORITY_MAYU sis_neck_now_priority;
static LOOK_TARGET_PRIORITY_MAYU pre_priority;
static LOOK_AT_PARAM           sis_neck_now_param;
static float                   sis_neck_now_dist;
static int                     sis_neck_flg;
static int                     sis_neck_no_registered_cnt;

static GRA3DEMULATIONLIGHTDATACREATIONDATA *_GetEmulationLightdataCreationDataRef(void);
static void SisNeckInit(void);
static void SisNeckFrameInit(void);
static void SisNeckMain(void);
static LOOK_AT_PARAM *SisNeckGetParam(void);

void sis_mdlInit(void)                                                  /* 156 */
{
    SisNeckInit();                                                      /* 158 */
    sis_data.Initialize();
}

void SisterDrawLock(void)
{
    sis_data.DrawLock();
}

void SisterDrawUnlock(void)
{
    sis_data.DrawUnlock();
}

int SisterIsLocked(void)
{
    return sis_data.IsLocked();
}

void *sis_mdlGetAlgAdrs(void)
{
    return sis_data.GetAlgAdrs();
}

/* The ROM really does hand the save system four bytes taken from the top of
 * sis_data -- that is MAN_DATA::man_data_draw_lock_cnt, not a request block.
 * plyr_mdlSetSave() saves a proper MDL_REQ_SAVE; this one looks like it was
 * cloned from it and never finished.  Kept as-is: the size and the address
 * both come straight out of the disassembly. */
/* PORT DEVIATION, and it matters.
 *
 * The ROM writes `data->addr = &sis_data; data->size = 4;` -- and on the EE
 * that is the *first member*, man_data_draw_lock_cnt, because GCC 2.96 places
 * a class's vtable pointer LAST (types.txt has it at 0x2c of MAN_DATA's 0x30).
 *
 * The host compiler places the vptr FIRST, so &sis_data + 4 bytes is half of
 * a vtable pointer: saving it writes garbage into the file, and loading it
 * corrupts the live object -- after which sis_data.IsReady(), a virtual call
 * DrawSister() makes every frame, dispatches through a damaged pointer.
 *
 * The member the ROM meant is named explicitly.  Same four bytes, same
 * position in the file, so the save layout is unchanged. */
void sis_mdlSetSave(MC_SAVE_DATA *data)
{
    data->addr = (u_char *)&sis_data.man_data_draw_lock_cnt;            /* 182 */
    data->size = 4;                                                     /* 183 */
}

void SetupSisMdl(void)
{
    int mdl_no = GetSisterMdlNo();
    int iAcsNo = GetSisterAcsNo();

    sis_data.Setup(mdl_no, 4, ANE_SISUTEMU_BD, 0x10, iAcsNo);
}

int IsReadySisMdl(void)                                                 /* 191 */
{
    return sis_data.IsReady();
}

void ReleaseSisMdl(void)                                                /* 195 */
{
    sis_data.Release();
}

void sis_mdlMotionWork(void)
{
    if (IsReadySisMdl() != 0 && IsSisWrk() != 0)                        /* 204, 205 */
    {
        SisNeckMain();                                                  /* 207 */
        sisterAnimationProc();                                          /* 208 */
    }
}

int sis_mdlBankPlay(int no, int effect, int loop, int fade_time,
                    SND_3D_SET *s3d, int vol, int pitch)                /* 213 */
{
    int bank_no = sis_data.GetSndBankNo();

    if (bank_no != -1)
    {
        return SndBankPlay(bank_no, no, effect, loop, vol, pitch, fade_time, s3d); /* 214 */
    }

    PRINT_ASSERT("sis_bank_id is Illegal");                             /* 220 */

    return 0x300000;                                                    /* 221 */
}

int sis_mdlBankIsLoopSnd(int no)                                        /* 224 */
{
    int bank_no = sis_data.GetSndBankNo();

    if (bank_no != -1)
    {
        return SndBankIsLoopSnd(bank_no, no);                           /* 225 */
    }

    PRINT_ASSERT("sis_bank_id is Illegal");                             /* 229 */

    return 0;                                                           /* 230 */
}

/* Unlike GetPlyrFtype() this one does not null-check the ANI_CTRL. */
u_short GetSisterFtype(void)
{
    return sis_data.GetAniCtrl()->ftype;
}

void ReqSisterMim(int no, int rev)                                      /* 247 */
{
    ANI_CTRL *ani_ctrl = sis_data.GetAniCtrl();

    if (ani_ctrl != nullptr)                                            /* 250 */
    {
        mimRequestNum(ani_ctrl, no, (u_char)rev);                       /* 251 */
    }
}

void ReqSisterMimContinue(int no, int rev)                              /* 256 */
{
    ANI_CTRL *ani_ctrl = sis_data.GetAniCtrl();

    if (ani_ctrl != nullptr)                                            /* 259 */
    {
        mimRequestNumContinue(ani_ctrl, no, (u_char)rev);               /* 260 */
    }
}

void StopSisterMim(int no)                                              /* 265 */
{
    ANI_CTRL *ani_ctrl = sis_data.GetAniCtrl();

    if (ani_ctrl != nullptr)                                            /* 268 */
    {
        mimStopNum(ani_ctrl, no);                                       /* 269 */
    }
}

int IsSisterMimParts(int no)                                            /* 275 */
{
    return mimIsUseParts(sis_data.GetAniCtrl(), no) != 0;               /* 277 */
}

void ReqSisterAnime(u_char flame)                                       /* 283 */
{
    ANI_CTRL *ani_ctrl = sis_data.GetAniCtrl();

    if (ani_ctrl == nullptr)
    {
        PRINT_ASSERT("SisAniCtrl Is NULL");                             /* 291 */
        return;
    }

    if ((u_int)sis_wrk.anime_no == ani_ctrl->anm.playnum)               /* 297 */
    {
        printf("The same anime is requested. In ReqSisterAnime!!!!!!!!\n"); /* 298 */
        return;
    }

    mimInitLoop(ani_ctrl);                                              /* 303 */

    /* Mayu always plays out of animation set 4 -- SetupSisMdl() requests that
     * set and nothing ever swaps it, so the index is hardcoded here where the
     * player reads it back out of the ANI_CTRL. */
    motSetAnime(ani_ctrl, anm_tbl[4].ani, (int)sis_wrk.anime_no);       /* 306 */
    motInitInterpAnime(ani_ctrl, (int)flame);                           /* 309 */
}

void sisterAnimationProc(void)                                          /* 314 */
{
    ANI_CTRL *ani_ctrl = sis_data.GetAniCtrl();

    u_char coord_state = motSetCoord(ani_ctrl, 0xff, 0);                /* 316 */
    if (coord_state == 1)
    {
        sis_wrk.cmn_wrk.st.sta |= 0x2000;                               /* 319 */
    }
    else if (coord_state == 2)
    {
        sis_wrk.cmn_wrk.st.sta |= 0x4000;                               /* 321 */
    }

    movGetMoveval(sis_wrk.spd, sis_wrk.old_spd, ani_ctrl,
                  motGetNowFrame(&sis_data.GetAniCtrl()->mot) >> 1, 0.0f); /* 328 */
    mimSetVertex(sis_data.GetAniCtrl());                                /* 331 */

    if (sis_data.GetAniCtrl()->base_p == nullptr)
    {
        printf("(int)(sis_data.GetAniCtrl()->mdl_no) = 0x%x\n",
               sis_data.GetAniCtrl()->mdl_no);                          /* 335 */
        printf("(int)(sis_data.GetAniCtrl()->anm_no) = 0x%x\n",
               sis_data.GetAniCtrl()->anm_no);                          /* 336 */
        printf("(int)(sis_data.GetAniCtrl()->mdl_p) = 0x%x\n",
               (u_int)(uintptr_t)sis_data.GetAniCtrl()->mdl_p);         /* 337 */
        printf("(int)(sis_data.GetAniCtrl()->anm_p) = 0x%x\n",
               (u_int)(uintptr_t)sis_data.GetAniCtrl()->anm_p);         /* 338 */

        printf("(int)(sis_data.GetAniCtrl()->pk2_p) = 0x%x\n",
               (u_int)(uintptr_t)sis_data.GetAniCtrl()->pk2_p);         /* 341 */
        printf("(int)(sis_data.GetAniCtrl()->mpk_p) = 0x%x\n",
               (u_int)(uintptr_t)sis_data.GetAniCtrl()->mpk_p);         /* 342 */
        printf("(int)(sis_data.GetAniCtrl()->mtop) = 0x%x\n",
               (u_int)(uintptr_t)sis_data.GetAniCtrl()->mtop);          /* 343 */
        printf("(int)(sis_data.GetAniCtrl()->mdat) = 0x%x\n",
               (u_int)(uintptr_t)sis_data.GetAniCtrl()->mdat);          /* 344 */
        printf("(int)(sis_data.GetAniCtrl()->mim) = 0x%x\n",
               (u_int)(uintptr_t)sis_data.GetAniCtrl()->mim);           /* 345 */
        printf("(int)(sis_data.GetAniCtrl()->wmim) = 0x%x\n",
               (u_int)(uintptr_t)sis_data.GetAniCtrl()->wmim);          /* 346 */
        printf("(int)(sis_data.GetAniCtrl()->cloth_ctrl) = 0x%x\n",
               (u_int)(uintptr_t)sis_data.GetAniCtrl()->cloth_ctrl);    /* 347 */
        printf("(int)(sis_data.GetAniCtrl()->collision_ctrl) = 0x%x\n",
               (u_int)(uintptr_t)sis_data.GetAniCtrl()->collision_ctrl); /* 348 */
        printf("(int)(sis_data.GetAniCtrl()->pkt_p) = 0x%x\n",
               (u_int)(uintptr_t)sis_data.GetAniCtrl()->pkt_p);         /* 349 */

        PRINT_ASSERT("sisAniCtrl->base_p is NULL");                     /* 350 */
    }

    SetRT2BaseMtx(sis_data.GetAniCtrl(), sis_wrk.cmn_wrk.mbox.pos,
                  sis_wrk.cmn_wrk.mbox.rot);                            /* 355 */
    motLookAtCtrl(sis_data.GetAniCtrl(), SisNeckGetParam());            /* 358 */
    SetCoordinate(sis_data.GetAniCtrl(), sis_wrk.cmn_wrk.mbox.pos,
                  sis_wrk.cmn_wrk.mbox.rot);                            /* 362 */

    GetMdlWaistPos(sis_wrk.bwp, sis_data.GetAniCtrl(),
                   (u_short)GetSisterMdlNo());                          /* 365 */
    GetMdlNeckPos(sis_wrk.cmn_wrk.headpos, sis_data.GetAniCtrl(),
                  (u_short)GetSisterMdlNo());                           /* 366 */

    acsClothCtrl(sis_data.GetAniCtrl(), sis_data.GetAniCtrl()->mpk_p,
                 (u_int)GetSisterMdlNo(), 0);                           /* 369 */
}

/* File-static here, unlike the identically named global in plyr_mdl.c -- the
 * ROM emits this one as a STATICPROC, so the two really are separate
 * functions with separate static state (plyr_mdl.h therefore must not
 * declare its copy). */
static GRA3DEMULATIONLIGHTDATACREATIONDATA *_GetEmulationLightdataCreationDataRef(void)
{
    if (sis_mdlGetANI_CTRL() == nullptr)                                /* 381 */
    {
        return nullptr;                                                 /* 383 */
    }

    float rf_rate = debug_var.flrf_si_rate;                             /* 390 */

    static GRA3DEMULATIONLIGHTDATACREATIONDATA eldcd_MAYU =             /* 394 */
    {
        { 0.06f, 0.06f, 0.06f },        /* vStaticDirLightColor         */
        rf_rate,                        /* fAngleScale                  */
        0.5f,                           /* fDiffuseScale                */
        plyr_wrk.maplight_scale,        /* fMaplightScale               */
        0,                              /* bEnableSelfreflection        */
        0,                              /* bEmulateSelfreflection       */
        1,                              /* bEnableFlashlight            */
        1,                              /* bEmulateFlashlight           */
        1,                              /* bEnableFlashlight2           */
        1,                              /* bEmulateFlashlight2          */
        1                               /* bEnableStaticDirLight        */
    };                                                                  /* 410 */

    eldcd_MAYU.vStaticDirLightColor[0] = debug_var.sis_para_r;          /* 414 */
    eldcd_MAYU.vStaticDirLightColor[1] = debug_var.sis_para_g;          /* 415 */
    eldcd_MAYU.vStaticDirLightColor[2] = debug_var.sis_para_b;          /* 416 */

    return &eldcd_MAYU;                                                 /* 419 */
}

void DrawSister(void)                                                   /* 425 */
{
    if (IsSisWrk() == 0)                                                /* 426 */
    {
        return;
    }
    if (sis_data.IsLocked() != 0)
    {
        return;
    }
    if (sis_data.IsReady() == 0 || GetSisDrawFLG() == 0)                /* 433 */
    {
        return;
    }

    HeaderSection *hs = sis_data.GetAniCtrl()->base_p;                  /* 459 */
    gra3dLightEnablePush();

    float work_amb[4];
    g3dxVu0CopyVector(work_amb, gra3dGetAmbientRef());

    float avWorldBB[8][4];
    charbbGet(avWorldBB, sis_data.GetAniCtrl(), hs->coordp->matCoord);  /* 466 */

    if (CheckModelBoundingBox(avWorldBB) != 0)                          /* 470 */
    {
        playerSetLight(sis_wrk.bwp, _GetEmulationLightdataCreationDataRef()); /* 474 */

        int iAlpha;
        if (PlayerModeIsFinder() != 0)                                  /* 490 */
        {
            iAlpha = g_iMaxPlayerAlpha;                                 /* 491 */
        }
        else
        {
            iAlpha = playerCalcAlpha(sis_data.GetAniCtrl());            /* 493 */
        }

        sis_data.AccessoryDraw(iAlpha);                                 /* 502 */

        if (gra3dIsMonotoneDrawEnable() != 0)                           /* 505 */
        {
            SendEneVramMono(sis_data.GetAniCtrl()->mdl_p, 0x2bc0,
                            sis_data.GetAniCtrl()->bwc_p);              /* 506 */
        }
        else
        {
            SendEneVram(sis_data.GetAniCtrl()->mdl_p, 0x2bc0);          /* 509 */
        }

        /* The ROM issues the plain upload a second time unconditionally, so
         * the monotone branch above is immediately overwritten by the normal
         * one.  Kept: removing it would change which texture set reaches the
         * GS when monotone drawing is on. */
        SendEneVram(sis_data.GetAniCtrl()->mdl_p, 0x2bc0);              /* 512 */

        ManmdlSetAlpha(hs, (u_char)iAlpha);                             /* 514 */
        _gra3dDrawSGD((SGDFILEHEADER *)hs, SRT_REALTIME, nullptr, -1);  /* 516 */
        DrawGirlSubObj(sis_data.GetAniCtrl()->mpk_p, (u_char)iAlpha);   /* 523 */

        _SetPREVIOUSTRI2PRIM(nullptr);                                  /* 527 */
    }

    gra3dSetAmbient(work_amb);                                          /* 541 */
    gra3dLightEnablePop();                                              /* 543 */
}

static void SisNeckInit(void)                                           /* 561 */
{
    SetSisNeckFlg(1);                                                   /* 562 */
    ltd_mode = LTD_MODE_NORMAL;                                         /* 563 */
    same_priority_count = 0;
    SisNeckFrameInit();                                                 /* 565 */
    sis_neck_no_registered_cnt = 0;                                     /* 566 */
}

static void SisNeckFrameInit(void)
{
    sis_neck_now_priority = LTP_MAYU_LEAST;                             /* 572 */

    /* The ROM seeds this with 0x7fffffff, which on the EE FPU is simply the
     * largest representable float (that unit has no inf/NaN encodings).  The
     * same bit pattern is a NaN on an IEEE host, and every
     * "closer than the current target" test below would then fail, so use
     * FLT_MAX -- the value the constant was meant to be. */
    sis_neck_now_dist = FLT_MAX;                                        /* 573 */
}

int SisNeckRegisterTarget(LOOK_AT_PARAM *param, LOOK_TARGET_PRIORITY_MAYU priority, float fLimitDist)                             /* 580 */
{
    if (sis_data.IsReady() == 0)
    {
        return 0;                                                       /* 604 */
    }

    float vSub[4];
    sceVu0SubVector(vSub, sis_wrk.cmn_wrk.headpos, param->pos);         /* 589 */

    /* Squared distance -- fLimitDist is squared too. */
    float dist = sceVu0InnerProduct(vSub, vSub);                        /* 590 */
    if (dist >= fLimitDist)                                             /* 591 */
    {
        return 0;
    }

    if (IsTargetInSight(sis_data.GetAniCtrl(), param->pos) == 0)
    {
        return 0;
    }

    if (priority >= sis_neck_now_priority)  /* 593 */
    {
        /* Same priority as the target already held: only take over when we
         * are the closer of the two. */
        if (priority != sis_neck_now_priority || dist >= sis_neck_now_dist)
        {
            return 0;
        }
    }

    sis_neck_now_priority = priority;                                   /* 595 */
    sis_neck_now_param    = *param;                                     /* 596 */
    sis_neck_now_dist     = dist;                                       /* 597 */
    return 1;                                                           /* 599 */
}

void SetSisNeckFlg(int flg)
{
    sis_neck_flg = flg;                                                 /* 610 */
}

/* Debug look-point override.  Compiled out of the release build -- the ROM
 * body is a bare `jr ra`. */
void DbgSisLookPointCtrl(void)                                          /* 674 */
{
}

static void SisNeckMain(void)
{
    DbgSisLookPointCtrl();                                              /* 680 */

    sis_neck_now_param.enable = sis_neck_flg;                           /* 684 */

    if (ltd_mode == LTD_MODE_NORMAL)                                    /* 686 */
    {
        if (pre_priority == sis_neck_now_priority)                      /* 690 */
        {
            same_priority_count += 2;                                   /* 691 */
        }
        else
        {
            same_priority_count = 0;                                    /* 695 */
        }

        if (sis_neck_now_priority == LTP_MAYU_LEAST)                    /* 707 */
        {
            sis_neck_no_registered_cnt += 2;                            /* 708 */
            sis_neck_now_param.enable = 0;                              /* 712 */
        }
        else
        {
            sis_neck_no_registered_cnt = 0;
        }
    }
    else if (ltd_mode == LTD_MODE_TIRED)
    {
        same_priority_count += 2;                                       /* 721 */
        ltd_mode = (same_priority_count < SIS_NECK_TIRED_COUNT);        /* 728 */

        if (pre_priority != sis_neck_now_priority)
        {
            ltd_mode = LTD_MODE_NORMAL;
        }
        else
        {
            printf("MAYU IS TIRED\n");                                  /* 738 */
        }
    }

    pre_priority = sis_neck_now_priority;
    SisNeckFrameInit();                                                 /* 745 */
}

static LOOK_AT_PARAM *SisNeckGetParam(void)
{
    return &sis_neck_now_param;                                         /* 750 */
}

ANI_CTRL *sis_mdlGetANI_CTRL(void)
{
    return sis_data.GetAniCtrl();
}

ANI_CTRL *sis_mdlGetShadowANI_CTRL(void)
{
    return sis_data.GetShadowAniCtrl();
}

void sis_mdlGetMATRIX(float (*mtx)[4], int bone_no)                     /* 765 */
{
    if (sis_data.IsReady() != 0)
    {
        HeaderSection *hs = sis_data.GetAniCtrl()->base_p;
        SGDCOORDINATE *cp = hs->coordp;                                 /* 775 */
        sceVu0CopyMatrix(mtx, cp[bone_no].matLocalWorld);
    }
}                                                                       /* 777 */

void sisterDrawShadow(void)                                             /* 783 */
{
    ANI_CTRL *pAC = sis_mdlGetANI_CTRL();                               /* 784 */
    if (pAC == nullptr)                                                 /* 786 */
    {
        return;
    }

    /* Both asserts are recoverable in the ROM: it re-reads the field and
     * carries on rather than bailing out. */
    G3DASSERT(pAC->base_p, "");                              /* 788 */
    G3DASSERT(pAC->base_p->coordp, "");                      /* 789 */

    SGDCOORDINATE *pCoord = pAC->base_p->coordp;                        /* 793 */
    float avBBWorld[8][4];
    charbbGet(avBBWorld, pAC, pCoord->matCoord);                        /* 794 */

    if (SisterIsLocked() == 0 || GetSynchroModeFlg() != 0)              /* 798 */
    {
        /* The ROM passes the *player's* shadow ANI_CTRL, not
         * sis_mdlGetShadowANI_CTRL().  Left as the ROM has it. */
        gra3dDrawSGDShadowCharacter(plyr_mdlGetShadowANI_CTRL(), pCoord,
                                    avBBWorld,
                                    _GetEmulationLightdataCreationDataRef()); /* 802 */
    }
}
