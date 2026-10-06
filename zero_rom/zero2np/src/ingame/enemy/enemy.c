// FILE: /home/zero_rom/zero2np/src/ingame/enemy/enemy.c
//
// The enemy module.  Ten ENE_WRK slots, each one a ghost: its load and release
// requests, its per-frame rule, the nearest-ghost sweeps that drive the finder
// HUD, and the draw path.
//
// The rule itself is thin -- the ghost's behaviour lives in the action script
// enemy_act.o interprets, and this file only sets the cursor up (EneActSet) and
// steps it.  What is here is everything around that: which unit the ghost is
// attacking, how transparent it is, whether it is on screen and unoccluded,
// and the scripted slow / highlight / paralysis / seal states.
//
// Reconstructed from the Feb 6 2004 prototype (SLES_523.84), enemy.o.
// Trailing /* NNN */ comments are the original source line numbers.

#include "enemy.h"

#include <stdio.h>                              /* printf                      */
#include <string.h>                             /* memset                      */
#include <math.h>                               /* tanf                        */
#include <float.h>                              /* FLT_MAX                     */

#include "eetypes.h"
#include "libvu0.h"
#include "enemy_dat.h"                          /* jene_dat / aene_dat         */
#include "enemy_act.h"                          /* EneAlgCtrl / EneBlinkCtrl   */
#include "ene_mot_ctrl.h"                       /* EneMotAlgCtrl               */
#include "alg_manage.h"
#include "fly_ctrl.h"

#include "../../common/utility.h"               /* GetDistV / LineSide / ...   */
#include "../../common/utility2.h"              /* PRINT_ASSERT / PRINT_WARNING */
#include "../../common/variable.h"              /* plyr_wrk / sis_wrk / debug_var */
#include "../../common/zero2_util.h"            /* CalcAngle / PLANE3D         */
#include "../../graphics/mmanage.h"
#include "../../graphics/graph3d/g3ddbg.h"      /* G3DASSERT                   */
#include "../../graphics/graph3d/gra3d.h"
#include "../../graphics/graph3d/gra3dConst.h"  /* g_v0100 / g_matUnit         */
#include "../../graphics/graph3d/gra3dMisc.h"
#include "../../graphics/graph3d/gra3dSGD.h"
#include "../../graphics/graph3d/gra3dSGDData.h"
#include "../../graphics/graph3d/g3dMath.h"     /* g3dAcosf / g3dAtanf         */
#include "../../graphics/motion/mdlwork.h"      /* ANI_CTRL                    */
#include "../../graphics/motion/mdlact.h"       /* motGetEneNeckRot            */
#include "../../graphics/motion/mdldat.h"       /* manmdl_dat                  */
#include "../../graphics/motion/motion.h"
#include "../../graphics/motion/accessory.h"
#include "../../graphics/motion/mim.h"
#include "../../graphics/motion/Morph.h"
#include "../../graphics/effect/effect.h"       /* efcnt / SetEffects          */
#include "../../graphics/effect/effect_sub.h"
#include "../../graphics/effect/effect_obj.h"
#include "../../graphics/effect/effect_oth.h"
#include "../../graphics/effect/effect_torch.h"
#include "../../system/os/system.h"             /* GetPALMode                  */
#include "../../system/eeiop/cddat.h"           /* BGM_START_DMY / BGM_END_DMY */
#include "../../system/eeiop/sndbank.h"
#include "../../system/eeiop/stream_auto.h"
#include "../camera/map_camera.h"               /* QuakeCamera*                */
#include "../ingame_effect.h"                   /* IgEffectEffectEndParticleReq */
#include "../map/MapDraw.h"                     /* MapDrawGetLightPtr          */
#include "../map/MhCtl.h"
#include "../map/map_bgm.h"                     /* map_bgmFadeIn / Out         */
#include "../movie_room_menu/prg/movie_projecter.h"
#include "../photo/m_plyr_camera.h"
#include "../photo/finder.h"
#include "../plyr/ChrSort.h"
#include "../plyr/charBB.h"
#include "../plyr/player.h"
#include "../plyr/plyr_mdl.h"
#include "../plyr/sis_mdl.h"
#include "../plyr/sister.h"
#include "../plyr/unit_ctl.h"
#include "../subtitle/subtitle.h"
#include "../../graphics/obj_draw_ctrl.h"       /* GetEneDrawFLG               */
#include "../../graphics/graph3d/g3dxVu0.h" /* g3dxVu0CopyVector */

/* --------------------------------------------------------------------------
 *  Module state
 * ------------------------------------------------------------------------ */

/* The ROM's .lit4 pi, which GCC 2.96-ee rounded from a shorter decimal than
 * libm's -- 0x40490fda, not 0x40490fdb.  The same spelling is already used in
 * utility2.c and zero2_util.c. */
static const float ENE_PI  = 3.1415925f;                                /* lit4 3ee10c */
static const float ENE_PI2 = 6.283185f;                                 /* lit4 3ee110 */


/* The two per-ghost effect parameter records.  Neither type is reachable from
 * anywhere else in the reconstructed tree yet -- the effect handlers that
 * consume them (SetEffects id 2 and CallPartsDeform5_2) take the fields, not
 * the struct -- so they live here rather than in an effect header. */
typedef struct                          /* 0x10 */
{
    /* 0x0 */ u_char  type;
    /* 0x2 */ u_short alp;
    /* 0x4 */ u_short spd;
    /* 0x6 */ u_short amax;
    /* 0x8 */ u_short cmax;
    /* 0xa */ u_short in;
    /* 0xc */ u_short keep;
    /* 0xe */ u_short out;
} DITHER_TYPE;

typedef struct                          /* 0xc */
{
    /* 0x0 */ u_char no;
    /* 0x1 */ u_char alp;
    /* 0x4 */ float  spd;
    /* 0x8 */ float  wave;
} DEFORM_TYPE;

/* ROM: fixed_array<ENE_WRK,10> at .data 0x2fe030.
 *
 * The ROM has a global constructor keyed to this array (0x171a60), which walks
 * the ten slots running the implicit default constructors of the two members
 * that have one -- CSpiritGage (zero + Init()) and CWaitVariable<short>
 * adpcm_tm (zero).  Both are aggregates here, so static zero-initialisation
 * reaches the same state and no constructor is reproduced. */
fixed_array<ENE_WRK, ENE_WRK_MAX> ene_wrk;

/* The dither ramps a ghost's ENE_DAT_COMMON::dih_type selects.  Only entry 0
 * is all-zero (dih_type 0 means "no dither"), so the table is indexed from 1.
 * .data 0x300dd0; not read from this file yet -- SetCommonDat() only tests
 * dih_type and posts the effect, which reads the ramp itself. */
static DITHER_TYPE dit_type[9] =                                        /* 141 */
{
    {  0,   0,   0,   0,   0,   0,   0,   0 },
    {  8,  40,  80,  63,  58,  50,   0,  80 },
    {  8,  47,  80,  68,  48,  50,   0,  80 },
    {  8,  55,  80,  68,  87,  50,   0,  80 },
    {  2,  40,  10,  68,  88,  50,   0,  80 },
    {  2,  55,  16,  68,  88,  50,   0,  80 },
    {  2,  55,  31,  68,  88,  50,   0,  80 },
    {  8,  65,  16,  68,  88,  50,  60,  80 },
    {  8,  65,  16,  68,  88,  50,  30,  80 }
};

/* The two parts-deform variants a ghost's def_type[] pair selects.  Slot 0 is
 * the "none" entry -- SetCommonDat() tests for a zero index before using it.
 * Deform 1 is a plain alpha dissolve (no speed or wave); deform 2 is the
 * rippling one. .data 0x300e60 / 0x300ed8. */
static DEFORM_TYPE def_type1[10] =                                      /* 155 */
{
    {  0,   0, 0.0f, 0.0f },
    { 19,  77, 0.0f, 0.0f },
    { 19,  60, 0.0f, 0.0f },
    { 19,  85, 0.0f, 0.0f },
    { 19,  83, 0.0f, 0.0f },
    { 19,  90, 0.0f, 0.0f },
    { 19,  90, 0.0f, 0.0f },
    { 19,  73, 0.0f, 0.0f },
    { 19,  75, 0.0f, 0.0f },
    { 19, 100, 0.0f, 0.0f }
};

static DEFORM_TYPE def_type2[10] =                                      /* 168 */
{
    {  0,   0, 0.0f,       0.0f       },
    { 24,  97, 2.5699999f, 0.89999998f },
    { 24,  97, 2.5699999f, 0.79999995f },
    { 24,  97, 2.5999999f, 0.86999995f },
    { 24,  97, 2.5699999f, 0.89999998f },
    { 24,  97, 2.5699999f, 0.69999999f },
    { 24,  97, 0.58999997f, 1.3699999f },
    { 24,  97, 2.5099999f, 2.4199998f },
    { 24,  97, 0.58999997f, 0.66999996f },
    { 24,  97, 2.9099998f, 2.9199998f }
};

/* Combo multiplier by shot index.  ROM: reference_fixed_array<int,3>, i.e. a
 * bare pointer in .sdata patched to the .rodata table at 0x3a8dc0 by the
 * file's global constructor.  Modelled as the table itself -- nothing rebinds
 * the reference, and a fixed_array is the same layout. */
static int ew_combo_tbl_data[3] = { 4, 3, 2 };                          /* rodata 3a8dc0 */
static reference_fixed_array<int, 3> ew_combo_tbl(ew_combo_tbl_data);   /* 181 */

static int earth_quake_cnt;                                             /* sbss 3f4bf8 */
static int enemy_act_lock_cnt;                                          /* sbss 3f4bfc */
static int enemy_draw_lock_cnt;                                         /* sbss 3f4c00 */
static int enemy_anim_lock_cnt;                                         /* sbss 3f4c04 */
/* Which slot the HP readout / spirit gauge tracked last frame, so a change of
 * ghost can snap rather than ramp. */
static int iPreNearestNoHP;                                             /* sbss 3f4c08 */
static int iPreNearestNo;                                               /* sbss 3f4c0c */

/* The nearest-ghost sweeps all fill one of these. */
typedef struct                          /* 0xc */
{
    /* 0x0 */ int   wrk_no;
    /* 0x4 */ int   in_circle;
    /* 0x8 */ float dist;
} NEAREST_ENE_MANAGE;

/* --------------------------------------------------------------------------
 *  File-scope prototypes.  The ROM needs these: enemyInitAnmSub() calls
 *  EnemyStart() 1300 lines before it is defined, and EneRelease() and
 *  enemyInitAnmSub() call each other.
 * ------------------------------------------------------------------------ */
static void  enemyInitAnmSub(int i);
static void  enemyReleaseDataSub(ENE_WRK *ew);
static MMANAGE_ERR enemyReqDataSub(ENE_DAT_COMMON *cmn);
static void  enemyReqDataClearSub(ENE_DAT_COMMON *cmn);
static void  enemyReqDataClear(ENE_WRK *ew);
static MMANAGE_ERR enemyReqData(ENE_WRK *ew);
static int   enemyIsReadyData(ENE_WRK *ew);
static void  _EnemyAnimationProc(ENE_WRK *ew);
static void  EneStreamCtrl(ENE_WRK *ew);
static void  EnemyWrkOne(ENE_WRK *ew, int i);
static void  NearestAutoEneDoJob_In_Circle(void);
static void  NearestAutoEneDoJob(void);
static void  NearestBattleEneDoJob_In_Circle(void);
static void  NearestBattleEneDoJob(void);
static void  EnemyHPSetJob(void);
static void  NearestBattleEneDoJob_WithView(void);
static void  SetCommonDat(ENE_WRK *ew, ENE_DAT_COMMON *dat, int hp);
static void  AutoEnemyStart(ENE_WRK *ew);
static void  JibakuEnemyStart(ENE_WRK *ew);
inline void  EnemyStart(int wrk_no);
static void *GetEneDatP(int ene_type, int dat_no);
static int   GetEneWrkBuffer(void);
static int   EneRelease(int wrk_no);
static void  EneComboCtrl(ENE_WRK *ew);
static void  EnemyMotionWorkOne(ENE_WRK *ew);
static void  _enemySetLight(ENE_WRK *pEW);
static void  OkuraSanEnemyDebug(void);
static void  EneRule(ENE_WRK *ew);
static void  EneMoveCtrl(ENE_WRK *ew);
static u_char EnePRecogChkChk(ENE_WRK *ew);
static u_char EnePRecogChk(ENE_WRK *ew, u_char *act_no);
static void  EneActIniChk(ENE_WRK *ew, u_char view_chk, u_char act_no);
static int   EneActPreferChk(ENE_WRK *ew, u_char *act_no);
static void  EneActRule(ENE_WRK *ew);
static u_char PlyrOutAreaChk(ENE_WRK *ew);
static u_char EneTrtryChk(ENE_WRK *ew, u_char room_no);
static void  EneAtkCtrl(ENE_WRK *ew);
static u_char EneDmgChk(ENE_WRK *ew);
static void  ClearEneStaDmg(ENE_WRK *ew);
static void  EneAniResolutionCtrl(ENE_WRK *ew);
static void  EneCondCtrl(ENE_WRK *ew);
static void  SetAtkTarget(ENE_WRK *ew, int pl);
static void  ChangeAtkTarget(ENE_WRK *ew);
static void  EneBlinkPosSet(ENE_WRK *ew);
static void  EneLightCtrl(ENE_WRK *ew);
static void  EneAuraCtrl(ENE_WRK *ew);
static void  EneSlowHitBackCtrl(ENE_WRK *ew);
static void  EneMahiCtrl(ENE_WRK *ew);
static void  EneSealCtrl(ENE_WRK *ew);
static void  EneHPRecv(ENE_WRK *ew);
static void  CtrlEarthquake(void);
static int   EneSpeMimeCtrl(ANI_CTRL *ani_ctrl);
static int   EneItukiMepatiCtrl(ANI_CTRL *ani_ctrl);

/* ==========================================================================
 *  Boot
 * ======================================================================== */

void InitEnemy(void)                                                    /* 298 */
{
    iPreNearestNo       = -1;                                           /* 299 */
    iPreNearestNoHP     = -1;                                           /* 300 */
    enemy_draw_lock_cnt = 0;                                            /* 301 */
    enemy_act_lock_cnt  = 0;
    enemy_anim_lock_cnt = 0;
    earth_quake_cnt     = GetRndSP(300, 300);                           /* 302 */
}                                                                       /* 304 */

/* ==========================================================================
 *  Resource requests
 * ======================================================================== */

/* Hand the loaded model and animation to the motion system and decide what the
 * slot becomes: still waiting for an ANI_CTRL, ready, or straight into acting
 * if the event system already asked for it.
 *
 * Model 1 is the companion's own model, whose number is chosen at runtime --
 * every other ghost uses the table's mdl_no directly. */
static void enemyInitAnmSub(int i)                                      /* 309 */
{
    ENE_WRK *ew = &ene_wrk[i];
    void *anm_p;
    void *mdl_p;
    u_int mdl_no;
    u_int anm_no;

    mmanageIsReadyAnm(ew->cmn_dat->anm_no, &anm_p, 0);                  /* 314 */

    if (ew->cmn_dat->mdl_no == 1)                                       /* 315 */
    {
        mmanageIsReadyMdl(GetSisterMdlNo(), &mdl_p, 0);                 /* 316 */
        mdl_no = (u_int)GetSisterMdlNo();
        anm_no = ew->cmn_dat->anm_no;
    }
    else
    {
        mmanageIsReadyMdl(ew->cmn_dat->mdl_no, &mdl_p, 0);              /* 319 */
        mdl_no = ew->cmn_dat->mdl_no;
        anm_no = ew->cmn_dat->anm_no;
    }

    ew->ani_ctrl_p = (ANI_CTRL *)motInitOneEnemyAnm((u_int *)anm_p, (u_int *)mdl_p,
                                                    mdl_no, anm_no);    /* 322 */

    if (ew->ani_ctrl_p == nullptr)                                      /* 325 */
    {
        ew->status = ENE_STATUS_WAIT_ANI_CTRL;
    }
    else if (ew->act_flg == 0)                                          /* 330 */
    {
        ew->status = ENE_STATUS_READY;                                  /* 331 */
    }
    else
    {
        EnemyStart(i);                                                  /* 332 */
        ew->status = ENE_STATUS_ACT;                                    /* 333 */
    }
}                                                                       /* 338 */

/* Drop everything the ghost's animation side holds.  The model itself is
 * released by the caller, because a passive ghost that turns hostile keeps its
 * slot and only swaps the animation. */
static void enemyReleaseDataSub(ENE_WRK *ew)                            /* 344 */
{
    if (ew->ani_ctrl_p != nullptr)                                      /* 346 */
    {
        acsDelEneCollision(ew->ani_ctrl_p);                             /* 348 */
        MorphDell(ew->ani_ctrl_p);                                      /* 350 */
        motReleaseOneAnm(ew->ani_ctrl_p);                               /* 354 */
    }

    mmanageClearAnm(ew->cmn_dat->anm_no);                               /* 357 */

    if (ew->cmn_dat->se_no > 0)                                         /* 358 */
    {
        SndBankRelease(ew->se_bank_no);                                 /* 362 */
    }

    alg_manageClearAlg(ew->cmn_dat->alg_no);                            /* 365 */

    /* The three models that carry a swarm. */
    if (ew->cmn_dat->mdl_no == 3 ||                                     /* 370 */
        ew->cmn_dat->mdl_no == 0x1f ||
        ew->cmn_dat->mdl_no == 0x29)
    {
        EneFlyAnmctrlRelease(ew);
    }
}

/* Animation + algorithm for one data head.  Non-zero when either failed. */
static MMANAGE_ERR enemyReqDataSub(ENE_DAT_COMMON *cmn)                 /* 375 */
{
    MMANAGE_ERR ret = OL_LOAD_ERR_OK;

    if (mmanageReqAnm(cmn->anm_no) != OL_LOAD_ERR_OK)
    {
        ret = OL_LOAD_ERR_MEMORY_LACK;
    }
    if (alg_manageReqAlg(cmn->alg_no) != OL_LOAD_ERR_OK)
    {
        ret = OL_LOAD_ERR_MEMORY_LACK;
    }
    return ret;                                                         /* 385 */
}

static void enemyReqDataClearSub(ENE_DAT_COMMON *cmn)
{
    mmanageClearAnm(cmn->anm_no);                                       /* 390 */
    alg_manageClearAlg(cmn->alg_no);                                    /* 391 */
}

/* Undo a partly-satisfied enemyReqData(). */
static void enemyReqDataClear(ENE_WRK *ew)                              /* 396 */
{
    if (ew->type == 2)                                                  /* 399 */
    {
        /* A passive ghost also holds the hostile entry it can turn into. */
        if (ew->aie->next >= 0)                                         /* 400 */
        {
            enemyReqDataClearSub(&jene_dat[ew->aie->next].cmn);          /* 402 */
        }
    }

    enemyReqDataClearSub(ew->cmn_dat);                                  /* 406 */

    if (ew->cmn_dat->mdl_no == 1)                                       /* 407 */
    {
        mmanageClearMdl(GetSisterMdlNo());                              /* 408 */
    }
    else
    {
        mmanageClearMdl(ew->cmn_dat->mdl_no);                           /* 411 */
    }
}

/* Post every load this ghost needs.  A passive ghost with a `next` entry loads
 * that entry's animation and sound too, so the transformation is instant --
 * which is why the two must share a model, and why the mismatch is an assert
 * rather than a silent reload. */
static MMANAGE_ERR enemyReqData(ENE_WRK *ew)                            /* 417 */
{
    ENE_DAT_COMMON *cmn;
    MMANAGE_ERR Err = OL_LOAD_ERR_OK;                                   /* 420 */
    int auto_ene_se_no = -1;

    if (ew->type == 2)                                                  /* 423 */
    {
        if (ew->aie->next >= 0)                                         /* 424 */
        {
            if (enemyReqDataSub(&jene_dat[ew->aie->next].cmn) != OL_LOAD_ERR_OK) /* 425 */
            {
                Err = OL_LOAD_ERR_MEMORY_LACK;
            }
            auto_ene_se_no = jene_dat[ew->aie->next].cmn.se_no;         /* 427 */

            G3DASSERT(jene_dat[ew->aie->next].cmn.mdl_no == ew->cmn_dat->mdl_no,
                      "AUTO_ENE MDL_NO Is Different From JIBAKU_ENE MDL_NO!!"); /* 431/432 */
        }
    }

    cmn = ew->cmn_dat;

    if (enemyReqDataSub(cmn) != OL_LOAD_ERR_OK)                         /* 441 */
    {
        Err = OL_LOAD_ERR_MEMORY_LACK;
    }

    if (ew->cmn_dat->mdl_no == 1)                                       /* 445 */
    {
        if (mmanageReqMdl(GetSisterMdlNo()) != OL_LOAD_ERR_OK)
        {
            Err = OL_LOAD_ERR_MEMORY_LACK;
        }
    }
    else
    {
        if (mmanageReqMdl(ew->cmn_dat->mdl_no) != OL_LOAD_ERR_OK)       /* 447 */
        {
            Err = OL_LOAD_ERR_MEMORY_LACK;
        }
    }

    if (Err == OL_LOAD_ERR_OK)                                          /* 458 */
    {
        if (cmn->mdl_no == 3 || cmn->mdl_no == 0x1f || cmn->mdl_no == 0x29) /* 459 */
        {
            EneFlyInit(ew);                                             /* 463 */
        }

        /* The bank's own header file is the one below it. */
        if (cmn->se_no >= 0)                                            /* 468 */
        {
            ew->se_bank_no = SndBankNew(cmn->se_no, cmn->se_no - 1, -1); /* 472 */
        }
        if (auto_ene_se_no >= 0)                                        /* 474 */
        {
            ew->se_bank_jibaku_no = SndBankNew(auto_ene_se_no,
                                               auto_ene_se_no - 1, -1);  /* 478 */
        }
    }
    else
    {
        enemyReqDataClear(ew);                                          /* 481 */
    }

    return Err;                                                         /* 482 */
}

/* ==========================================================================
 *  Queries
 * ======================================================================== */

/* Any slot loaded and asked to act. */
int IsEnemyOn(void)                                                     /* 486 */
{
    int i;

    for (i = 0; i < ENE_WRK_MAX; i++)                                   /* 488 */
    {
        if (ene_wrk[i].status != ENE_STATUS_NO_USE &&                   /* 490 */
            ene_wrk[i].act_flg != 0)
        {
            return 1;                                                   /* 494 */
        }
    }
    return 0;                                                           /* 495 */
}                                                                       /* 496 */

/* The same question of one table entry. */
int IsEnemyOn(int ene_type, int dat_no)                                 /* 502 */
{
    int n = SearchEneWrkNo(ene_type, dat_no);                           /* 503 */

    if (n >= 0 &&                                                       /* 508 */
        ene_wrk[n].status != ENE_STATUS_NO_USE &&
        ene_wrk[n].act_flg != 0)
    {
        return 1;                                                       /* 512 */
    }
    return 0;                                                           /* 514 */
}

/* Non-zero once every request this ghost posted has landed.  Everything is
 * ANDed together so one outstanding piece holds the whole slot back. */
static int enemyIsReadyData(ENE_WRK *ew)                                /* 519 */
{
    int LoadOK = 1;                                                     /* 520 */
    void *dummy_p;
    void *temp;

    G3DASSERT(ew != nullptr, "ew : 0x%08x", ew);                        /* 524 */
    G3DASSERT(ew->cmn_dat != nullptr, "ew->cmn_dat : 0x%08x", ew->cmn_dat); /* 525 */

    if (ew->type == 2)                                                  /* 527 */
    {
        G3DASSERT(ew->aie != nullptr, "ew->aie : 0x%08x", ew->aie);     /* 528 */

        if (ew->aie->next >= 0)                                         /* 529 */
        {
            ENE_DAT_COMMON *cmn;

            G3DASSERT(ew->aie->next < g_iNumJeneDat,                    /* 531 */
                      "ew->aie->next : %d, g_iNumJeneDat : %d",
                      ew->aie->next, g_iNumJeneDat);

            cmn = &jene_dat[ew->aie->next].cmn;
            G3DASSERT(cmn != nullptr, "cmn : 0x%08x", cmn);             /* 534 */

            LoadOK &= mmanageIsReadyAnm(cmn->anm_no, &dummy_p, 0);      /* 536 */
            LoadOK &= alg_manageIsReadyAlg(cmn->alg_no, &temp);         /* 538 */

            if (cmn->se_no >= 0)                                        /* 540 */
            {
                LoadOK &= SndBankIsReady(ew->se_bank_jibaku_no);        /* 541 */
            }
        }
    }

    if (ew->cmn_dat->mdl_no == 1)                                       /* 547 */
    {
        LoadOK &= mmanageIsReadyMdl(GetSisterMdlNo(), &dummy_p, 0);     /* 548 */
    }
    else
    {
        LoadOK &= mmanageIsReadyMdl(ew->cmn_dat->mdl_no, &dummy_p, 0);  /* 551 */
    }

    LoadOK &= mmanageIsReadyAnm(ew->cmn_dat->anm_no, &dummy_p, 0);      /* 553 */

    if (ew->cmn_dat->se_no >= 0)                                        /* 554 */
    {
        LoadOK &= SndBankIsReady(ew->se_bank_no);                       /* 555 */
    }

    LoadOK &= alg_manageIsReadyAlg(ew->cmn_dat->alg_no, &temp);         /* 557 */

    if (ew->cmn_dat->mdl_no == 3 ||                                     /* 560 */
        ew->cmn_dat->mdl_no == 0x1f ||
        ew->cmn_dat->mdl_no == 0x29)
    {
        LoadOK &= EneFlyModelInitWait(ew);                              /* 565 */
    }

    return LoadOK;                                                      /* 567 */
}

/* ==========================================================================
 *  Animation
 * ======================================================================== */

/* Advance one ghost's skeleton and place it in the world.
 *
 * The root bone is built here rather than by the motion system: identity,
 * scaled by 25 (the character-model unit), spun 180 degrees about X to flip the
 * model's axis, then yawed by the ghost's own facing, then translated.  Only
 * after that does the neck get its look-at overlay and the rest of the bones
 * their world coordinates. */
static void _EnemyAnimationProc(ENE_WRK *ew)                            /* 573 */
{
    ANI_CTRL      *ani_ctrl;
    HeaderSection *hs2;
    SGDCOORDINATE *cp;
    SGDCOORDINATE *cp2;
    u_int          mdl_no;
    u_int          i;
    float          grot;
    float          trot_m[4][4];
    u_char         ret;

    if (enemy_draw_lock_cnt != 0) { return; }                           /* 587 */
    if ((ew->st.sta & 0x2000000) != 0) { return; }                      /* 590 */
    if (ew->cmn_dat == nullptr) { return; }                             /* 592 */

    ani_ctrl = ew->ani_ctrl_p;
    if (ani_ctrl == nullptr) { return; }                                /* 598 */

    hs2    = ani_ctrl->base_p;                                          /* 602 */
    mdl_no = ani_ctrl->mdl_no;                                          /* 603 */

    /* One animation rate for the skeleton and every morph channel. */
    ani_ctrl->mot.reso = ew->ani_reso;                                  /* 604 */
    for (i = 0; i < ani_ctrl->mim_num; i++)                             /* 605 */
    {
        ani_ctrl->mim[i].reso = ew->ani_reso;                           /* 606 */
    }
    for (i = 0; i < ani_ctrl->bg_num; i++)                              /* 607 */
    {
        ani_ctrl->bgmim[i].reso = ew->ani_reso;                         /* 608 */
    }

    if (ani_ctrl->tanm_p != nullptr)                                    /* 610 */
    {
        motEneTexAnm(ani_ctrl, mdl_no);                                 /* 611 */
    }

    /* motSetCoord reports the clip end: 1 is "reached the end", 2 "looped". */
    ret = motSetCoord(ani_ctrl, 0, (u_char)(enemy_anim_lock_cnt != 0));  /* 613 */
    if (ret == 1)                                                       /* 615 */
    {
        ew->st.sta |= 0x200000000L;                                     /* 616 */
    }
    else if (ret == 2)                                                  /* 618 */
    {
        ew->st.sta |= 0x400000000L;
    }

    mimSetVertex(ani_ctrl);                                             /* 624 */
    EneSpeMimeCtrl(ani_ctrl);                                           /* 626 */

    if (hs2 != nullptr)                                                 /* 628 */
    {
        cp = hs2->coordp;                                               /* 632 */

        sceVu0UnitMatrix(cp->matCoord);                                 /* 668 */
        cp->matCoord[0][0] = 25.0f;                                     /* 669 */
        cp->matCoord[1][1] = 25.0f;
        cp->matCoord[2][2] = 25.0f;

        sceVu0RotMatrixX(cp->matCoord, cp->matCoord, ENE_PI);               /* 671 */

        grot = ew->mbox.rot[1] + ENE_PI;                                    /* 672 */
        if (grot > ENE_PI) { grot -= ENE_PI2; }                                 /* 674 */
        sceVu0RotMatrixY(cp->matCoord, cp->matCoord, grot);             /* 675 */

        g3dxVu0CopyVector(cp->matCoord[3], ew->mbox.pos);               /* 676 */
        cp->matCoord[3][3] = 1.0f;                                      /* 677 */

        if (ani_ctrl->neck_work.flg != 0)                               /* 679 */
        {
            ANI_CTRL *ani_ctrl2;

            if (ew->target_n == 1)                                      /* 682 */
            {
                ani_ctrl2 = sis_mdlGetANI_CTRL();
            }
            else if (ew->target_n == 0)
            {
                ani_ctrl2 = plyr_mdlGetANI_CTRL();
            }
            else
            {
                printf("EnemyDrawOne() Illegal Target\n");              /* 689 */
                ani_ctrl2 = plyr_mdlGetANI_CTRL();
            }

            if (ani_ctrl2 != nullptr)                                   /* 690 */
            {
                cp2 = ani_ctrl2->base_p->coordp;                        /* 691 */
                sceVu0UnitMatrix(trot_m);                               /* 692 */
                motGetEneNeckRot(trot_m, ani_ctrl, cp2, ew->neck_rot);  /* 694 */
                sceVu0MulMatrix(cp[manmdl_dat[mdl_no].neck_id].matCoord, /* 695 */
                                cp[manmdl_dat[mdl_no].neck_id].matCoord,
                                trot_m);
            }
        }

        sgdCalcBoneCoordinate(cp, hs2->blocks - 1);                     /* 699 */
        motSetNeckWork(ani_ctrl);                                       /* 700 */
    }
}                                                                       /* 716 */

/* A passive ghost's voice line.  While the line is playing the stream just
 * follows the ghost; when the countdown reaches zero a new one is started.
 * The velocity handed to the 3D mixer is per second, hence the x32. */
static void EneStreamCtrl(ENE_WRK *ew)                                  /* 720 */
{
    SND_3D_SET set;
    float      spd_per_sec[4];

    memset(&set, 0, sizeof(SND_3D_SET));                                /* 721 */
    sceVu0ScaleVector(spd_per_sec, ew->mbox.spd, 30.0f);                /* 723 */
    set.pos = (sceVu0FVECTOR *)ew->mpos.p0;                             /* 724 */
    set.vel = (sceVu0FVECTOR *)spd_per_sec;

    if (ew->adpcm_tm.Work() != 0)                                       /* 727 */
    {
        int adpcm_no = SubTitleStreamFileNoGet(ew->cmn_dat->adpcm_no);   /* 729 */
        SND_3D_SET set2;

        memset(&set2, 0, sizeof(SND_3D_SET));                           /* 730 */
        set2.pos = (sceVu0FVECTOR *)ew->mbox.pos;                       /* 731 */

        G3DASSERT(adpcm_no < BGM_END_DMY && adpcm_no > BGM_START_DMY,   /* 733 */
                  "AutoEne %d SubTitle Adpcm Is Illegal %d",
                  ew->cmn_dat->adpcm_no, adpcm_no);

        ew->stream_id = StreamAutoPlay(adpcm_no, adpcm_no - 1, 0x10, 0, 0,
                                       0x3200, 0, &set2);               /* 741 */
        SubTitleReqAutoEnemy(ew->cmn_dat->adpcm_no, ew->stream_id);     /* 743 */
    }
    else
    {
        StreamAutoSet3D(ew->stream_id, &set);
    }
}                                                                       /* 746 */

/* Keep the camera-facing effect anchors current while the rule is frozen. */
void EnemyEffectPosUpdate(void)                                         /* 751 */
{
    int i;

    for (i = 0; i < ENE_WRK_MAX; i++)                                   /* 755 */
    {
        ENE_WRK *ew = &ene_wrk[i];

        if (ew->status == ENE_STATUS_ACT && (ew->st.sta & 1) == 0)      /* 756 */
        {
            EneBlinkPosSet(ew);                                         /* 757 */
        }
    }
}                                                                       /* 761 */

/* One slot, one frame.  The status is a small state machine: LOADING polls,
 * WAIT_ANI_CTRL retries the motion hand-off, ACT runs the rule, RELEASE frees
 * the slot.  READY does nothing -- the event system has to ask. */
static void EnemyWrkOne(ENE_WRK *ew, int i)                             /* 767 */
{
    switch (ew->status)                                                 /* 769 */
    {
    case ENE_STATUS_LOADING:                                            /* 771 */
        if (enemyIsReadyData(ew) == 0) { return; }                      /* 777 */
        /* fall through -- everything landed, so set the animation up now */

    case ENE_STATUS_WAIT_ANI_CTRL:                                      /* 783 */
        enemyInitAnmSub(i);                                             /* 787 */
        break;                                                          /* 788 */

    case ENE_STATUS_ACT:                                                /* 791 */
        if (enemy_act_lock_cnt != 0) { break; }                         /* 792 */

        ew->dist_p_e_o[0] = ew->dist_p_e[0];                            /* 796 */
        ew->dist_p_e[0]   = GetDistV(plyr_wrk.cmn_wrk.mbox.pos, ew->mbox.pos); /* 797 */

        ew->st.sta_old = ew->st.sta;                                    /* 798 */
        ew->st.sta    &= ~0x200000L;                                    /* 802 */

        if ((ew->st.sta & 1) != 0)                                      /* 805 */
        {
            /* Sealed: no rule at all, just keep the effect anchor current. */
            EneSealCtrl(ew);                                            /* 807 */
            EneBlinkPosSet(ew);
        }
        else
        {
            EneMahiCtrl(ew);                                            /* 811 */

            if ((ew->st.sta & 2) != 0)                                  /* 815 */
            {
                /* Paralysed: hold the pose. */
                ew->ani_reso = 0;                                       /* 820 */
                EneBlinkPosSet(ew);
            }
            else
            {
                EneRule(ew);                                            /* 823 */
                EneStreamCtrl(ew);                                      /* 824 */
            }
        }

        /* A parent ghost drags its children along, each at its own offset. */
        if ((ew->attr & 0x1000) != 0)                                   /* 825 */
        {
            int n;

            for (n = 0; n < 3; n++)                                     /* 826 */
            {
                int datno = ew->dat->child_ene[n];
                int c;

                if (datno < 0) { continue; }
                c = SearchEneWrkNo(ew->type, datno);
                if (c < 0)     { continue; }

                sceVu0CopyVector(ene_wrk[c].mbox.pos, ew->mbox.pos);
                sceVu0CopyVector(ene_wrk[c].mbox.rot, ew->mbox.rot);
                sceVu0AddVector(ene_wrk[c].mbox.pos, ene_wrk[c].mbox.pos,
                                ene_wrk[c].adjp);                       /* 830 */
                ene_wrk[c].tr_rate_alg = ew->tr_rate_alg;               /* 831 */
            }
        }

        /* ... and, separately, shares its HP with them. */
        if ((ew->attr & 0x10000) != 0)                                  /* 834 */
        {
            int n;

            for (n = 0; n < 3; n++)                                     /* 837 */
            {
                int datno = ew->dat->child_ene[n];
                int c;

                if (datno < 0) { continue; }                            /* 840 */
                c = SearchEneWrkNo(ew->type, datno);                    /* 841 */
                if (c < 0)     { continue; }                            /* 842 */

                ene_wrk[c].st.hp = ew->st.hp;                           /* 843 */
            }
        }

        /* Offer the ghost's neck to the companion's look-at, at no distance
         * limit -- she looks at an attacking ghost over any other target. */
        {
            LOOK_AT_PARAM param;

            param.pos[0]    = 0.0f;                                     /* 845 */
            param.pos[1]    = 0.0f;
            param.pos[2]    = 0.0f;
            param.pos[3]    = 0.0f;
            param.eye_spd   = 0.04f;
            param.head_spd  = 0.01f;
            param.chest_spd = 0.005f;
            param.enable    = 0;

            sceVu0CopyVector(param.pos, ew->mpos.p0);                   /* 853 */

            if ((ew->st.sta & 0x8000) != 0)                             /* 855 */
            {
                SisNeckRegisterTarget(&param, LTP_MAYU_ATTACK_ENEMY, FLT_MAX); /* 859 */
            }
            else
            {
                SisNeckRegisterTarget(&param, LTP_MAYU_ENEMY, FLT_MAX); /* 861 */
            }
        }
        break;                                                          /* 864 */

    case ENE_STATUS_RELEASE:                                            /* 868 */
        EneRelease(i);
        break;

    default:
        break;
    }
}                                                                       /* 878 */

/* ==========================================================================
 *  Locks
 * ======================================================================== */

void EnemyDrawLock(void)   { enemy_draw_lock_cnt++; }                   /* 882 */
void EnemyDrawUnlock(void) { enemy_draw_lock_cnt--; }                   /* 886 */
void EnemyLock(void)       { enemy_act_lock_cnt++;  }                   /* 890 */
void EnemyUnlock(void)     { enemy_act_lock_cnt--;  }                   /* 894 */
void EnemyAnimLock(void)   { enemy_anim_lock_cnt++; }                   /* 898 */
void EnemyAnimUnlock(void) { enemy_anim_lock_cnt--; }                   /* 902 */

/* Step one ghost by data number, with the animation lock lifted for the
 * duration -- an event that wants a single ghost to move while everything else
 * is held uses this.  The lock is restored, not cleared, and the animation
 * advance is only run if it had been held. */
void EnemyAnimOne(int iEneID)                                           /* 908 */
{
    int i;
    int iLockWork = enemy_anim_lock_cnt;                                /* 914 */

    enemy_anim_lock_cnt = 0;                                            /* 915 */

    for (i = 0; i < ENE_WRK_MAX; i++)                                   /* 918 */
    {
        if (ene_wrk[i].dat_no == iEneID)                                /* 920 */
        {
            EnemyWrkOne(&ene_wrk[i], i);                                /* 921 */

            if (iLockWork != 0)                                         /* 924 */
            {
                _EnemyAnimationProc(&ene_wrk[i]);                       /* 925 */
            }
        }
    }

    enemy_anim_lock_cnt = iLockWork;                                    /* 927 */
}                                                                       /* 929 */

/* ==========================================================================
 *  Per-frame entry points
 * ======================================================================== */

void EnemyMain(void)                                                    /* 938 */
{
    int i;

    OkuraSanEnemyDebug();

    if (enemy_anim_lock_cnt != 0)                                       /* 940 */
    {
        EnemyEffectPosUpdate();                                         /* 941 */
        return;
    }

    if (enemy_draw_lock_cnt != 0) { return; }                           /* 945 */

    /* Mode 9 is death: nothing may still be mid-attack. */
    if (plyr_wrk.cmn_wrk.mode == 9 || sis_wrk.cmn_wrk.mode == 9)        /* 949 */
    {
        for (i = 0; i < ENE_WRK_MAX; i++)                               /* 951 */
        {
            ene_wrk[i].atk_tm = 0;                                      /* 953 */
        }
        return;
    }

    for (i = 0; i < ENE_WRK_MAX; i++)                                   /* 961 */
    {
        EnemyWrkOne(&ene_wrk[i], i);                                    /* 962 */
    }                                                                   /* 965 */

    CtrlEarthquake();                                                   /* 968 */
    NearestAutoEneDoJob();                                              /* 971 */
    NearestAutoEneDoJob_In_Circle();                                    /* 974 */
    NearestBattleEneDoJob();                                            /* 977 */
    EnemyHPSetJob();                                                    /* 980 */
    NearestBattleEneDoJob_In_Circle();                                  /* 982 */
    NearestBattleEneDoJob_WithView();                                   /* 984 */
    FlyRule();
}                                                                       /* 985 */

/* Through a door transition the rule is off, but the ghosts still have to fade
 * and their auras still have to breathe. */
void EnemyDoorMain(void)                                                /* 987 */
{
    int i;

    EnemyEffectPosUpdate();                                             /* 991 */

    for (i = 0; i < ENE_WRK_MAX; i++)                                   /* 992 */
    {
        ENE_WRK *ew = &ene_wrk[i];

        if (ew->status == ENE_STATUS_ACT)                               /* 993 */
        {
            EneAlphaCtrl(ew);                                           /* 994 */
            EneAuraCtrl(ew);                                            /* 995 */
        }
    }
}                                                                       /* 997 */

/* ==========================================================================
 *  Nearest-ghost sweeps
 *
 *  Five near-identical passes, each picking one ghost out of the ten and
 *  handing it to a different piece of the finder HUD.  They differ only in
 *  which ghosts qualify -- passive or hostile, inside the ring or not, and
 *  which attribute bit excludes them.
 *
 *  All of them key the result off ENEALG_WRK::idx rather than the loop index.
 *  The two are equal in practice (EneLoadReq stamps idx with the slot number),
 *  but the ROM reads it back out, so this does too.
 * ======================================================================== */

/* A passive ghost close enough to be caught raises the ring's auto flag --
 * but only once it is actually inside the ring (status bit 0x80). */
static void NearestAutoEneDoJob_In_Circle(void)                         /* 1009 */
{
    NEAREST_ENE_MANAGE nearest_ene;
    int i;

    nearest_ene.wrk_no = -1;                                            /* 1014 */
    nearest_ene.dist   = FLT_MAX;                                       /* 1015 */

    for (i = 0; i < ENE_WRK_MAX; i++)                                   /* 1016 */
    {
        if (ene_wrk[i].status == ENE_STATUS_ACT &&
            ene_wrk[i].type == 2 &&
            (ene_wrk[i].st.sta & 0x400) != 0 &&
            (ene_wrk[i].attr & 0x80) == 0 &&
            ene_wrk[i].dist_p_e[0] < nearest_ene.dist)
        {
            nearest_ene.dist   = ene_wrk[i].dist_p_e[0];
            nearest_ene.wrk_no = ene_wrk[i].alg.idx;
        }
    }                                                                   /* 1032 */

    if (nearest_ene.wrk_no >= 0 &&                                      /* 1035 */
        (ene_wrk[nearest_ene.wrk_no].st.sta & 0x80) != 0)
    {
        m_plyr_camera.center_circle.SetAutoFlg(1);                      /* 1037 */
    }
    else
    {
        m_plyr_camera.center_circle.SetAutoFlg(0);                      /* 1040 */
    }
}

/* The needle deflection a passive ghost causes.  attr 0x180 is the pair of
 * "never the nearest pick" bits. */
static void NearestAutoEneDoJob(void)                                   /* 1045 */
{
    NEAREST_ENE_MANAGE nearest_ene;
    int i;

    nearest_ene.wrk_no = -1;                                            /* 1049 */
    nearest_ene.dist   = FLT_MAX;                                       /* 1050 */

    for (i = 0; i < ENE_WRK_MAX; i++)                                   /* 1051 */
    {
        if (ene_wrk[i].status == ENE_STATUS_ACT &&
            ene_wrk[i].type == 2 &&
            (ene_wrk[i].attr & 0x180) == 0 &&
            ene_wrk[i].dist_p_e[0] < nearest_ene.dist)
        {
            nearest_ene.dist   = ene_wrk[i].dist_p_e[0];
            nearest_ene.wrk_no = ene_wrk[i].alg.idx;
        }
    }                                                                   /* 1065 */

    if (nearest_ene.wrk_no < 0)                                         /* 1069 */
    {
        m_plyr_camera.filament.SetAuto(0.0f);
    }
    else
    {
        float fRate = CulcEP3(ene_wrk[nearest_ene.wrk_no].mpos.p0);     /* 1072 */
        m_plyr_camera.filament.SetAuto(fRate);                          /* 1074 */
    }
}

/* The same for a hostile ghost, driving the ring's enemy-catch flag. */
static void NearestBattleEneDoJob_In_Circle(void)                       /* 1080 */
{
    NEAREST_ENE_MANAGE nearest_ene;
    int i;

    nearest_ene.wrk_no = -1;                                            /* 1085 */
    nearest_ene.dist   = FLT_MAX;                                       /* 1086 */

    for (i = 0; i < ENE_WRK_MAX; i++)                                   /* 1087 */
    {
        if (ene_wrk[i].status == ENE_STATUS_ACT &&
            ene_wrk[i].type != 2 &&
            (ene_wrk[i].st.sta & 0x400) != 0 &&
            (ene_wrk[i].attr & 0x80) == 0 &&
            ene_wrk[i].dist_p_e[0] < nearest_ene.dist)
        {
            nearest_ene.dist   = ene_wrk[i].dist_p_e[0];
            nearest_ene.wrk_no = ene_wrk[i].alg.idx;
        }
    }                                                                   /* 1103 */

    if (nearest_ene.wrk_no < 0)                                         /* 1105 */
    {
        m_plyr_camera.center_circle.SetEneCatchFlg(0);                  /* 1106 */
    }
    else
    {
        m_plyr_camera.center_circle.SetEneCatchFlg(1);                  /* 1108 */
    }
}

/* The hostile-ghost pick that owns the needle and the spirit gauge.  Unlike
 * the passive sweeps this one prefers a ghost inside the ring over a nearer
 * one outside it -- once something is framed, the HUD stays on it. */
static void NearestBattleEneDoJob(void)                                 /* 1121 */
{
    NEAREST_ENE_MANAGE nearest_ene;
    int i;

    nearest_ene.wrk_no    = -1;                                         /* 1126 */
    nearest_ene.dist      = FLT_MAX;                                    /* 1127 */
    nearest_ene.in_circle = 0;                                          /* 1128 */

    for (i = 0; i < ENE_WRK_MAX; i++)                                   /* 1129 */
    {
        ENE_WRK *ew = &ene_wrk[i];
        int bInCircle;
        int bUpdate = 0;

        if (ew->status != ENE_STATUS_ACT) { continue; }
        if (ew->type == 2)                { continue; }
        if ((ew->attr & 0x80) != 0)       { continue; }

        bInCircle = (int)(ew->st.sta & 0x400);

        if (bInCircle != 0 && nearest_ene.in_circle == 0)
        {
            bUpdate = 1;                                                /* 1142 */
        }
        else if (bInCircle == 0 && nearest_ene.in_circle != 0)
        {
            bUpdate = 0;                                                /* 1144 */
        }
        else if (ew->dist_p_e[0] < nearest_ene.dist)
        {
            bUpdate = 1;                                                /* 1145 */
        }

        if (bUpdate)
        {
            nearest_ene.dist      = ew->dist_p_e[0];
            nearest_ene.wrk_no    = ew->alg.idx;
            nearest_ene.in_circle = bInCircle;                          /* 1155 */
        }
    }

    if (nearest_ene.wrk_no < 0)                                         /* 1164 */
    {
        if (iPreNearestNo >= 0)                                         /* 1167 */
        {
            ene_wrk[iPreNearestNo].spirit_gage.FadeOut();               /* 1169 */
        }
        m_plyr_camera.mpSpiritGage = nullptr;                           /* 1171 */
        m_plyr_camera.filament.SetBattle(0.0f);                         /* 1175 */
        m_plyr_camera.center_circle.SetBattleFlg(0);                    /* 1176 */
    }
    else
    {
        ENE_WRK     *ew = &ene_wrk[nearest_ene.wrk_no];
        CSpiritGage *pSpiritGage = &ew->spirit_gage;
        float        fRate;

        /* 0x80000 is "gauge suppressed"; bit 0 is the seal. */
        if ((ew->st.sta & 0x80000) == 0 && (ew->st.sta & 1) == 0)       /* 1184 */
        {
            pSpiritGage->FadeIn();                                      /* 1187 */
        }
        else
        {
            pSpiritGage->FadeOut();                                     /* 1190 */
        }

        m_plyr_camera.mpSpiritGage = pSpiritGage;                       /* 1196 */

        fRate = CulcEP3(ew->mpos.p0);                                   /* 1198 */
        m_plyr_camera.filament.SetBattle(fRate);
        m_plyr_camera.center_circle.SetBattleFlg(1);                    /* 1200 */
    }

    iPreNearestNo = nearest_ene.wrk_no;
}

/* The health readout.  Same in-circle preference as the gauge, but excluded
 * by a different attribute bit (0x100) so a boss can show a gauge without
 * showing a health bar. */
static void EnemyHPSetJob(void)                                         /* 1206 */
{
    NEAREST_ENE_MANAGE nearest_ene;
    int i;

    nearest_ene.wrk_no    = -1;                                         /* 1211 */
    nearest_ene.dist      = FLT_MAX;                                    /* 1212 */
    nearest_ene.in_circle = 0;                                          /* 1213 */

    for (i = 0; i < ENE_WRK_MAX; i++)                                   /* 1214 */
    {
        ENE_WRK *ew = &ene_wrk[i];
        int bInCircle;
        int bUpdate = 0;

        if (ew->status != ENE_STATUS_ACT) { continue; }
        if (ew->type == 2)                { continue; }
        if ((ew->attr & 0x100) != 0)      { continue; }

        bInCircle = (int)(ew->st.sta & 0x400);

        if (bInCircle != 0 && nearest_ene.in_circle == 0)
        {
            bUpdate = 1;                                                /* 1226 */
        }
        else if (bInCircle == 0 && nearest_ene.in_circle != 0)
        {
            bUpdate = 0;                                                /* 1228 */
        }
        else if (ew->dist_p_e[0] < nearest_ene.dist)
        {
            bUpdate = 1;                                                /* 1229 */
        }

        if (bUpdate)
        {
            nearest_ene.dist      = ew->dist_p_e[0];
            nearest_ene.wrk_no    = ew->alg.idx;
            nearest_ene.in_circle = bInCircle;                          /* 1239 */
        }
    }

    if (nearest_ene.wrk_no < 0)                                         /* 1248 */
    {
        iPreNearestNoHP = -1;                                           /* 1251 */
        finderEneLifeLen(0.0f);                                         /* 1253 */
    }
    else
    {
        float percentage = (float)ene_wrk[nearest_ene.wrk_no].st.hp /   /* 1257 */
                           (float)ene_wrk[nearest_ene.wrk_no].dat->hp;

        /* A change of ghost snaps the bar; otherwise it is only nudged, so
         * the red damage tail can lag behind. */
        if (iPreNearestNoHP != nearest_ene.wrk_no)                      /* 1260 */
        {
            finderSetEneLifePercentage(percentage);                     /* 1261 */
            iPreNearestNoHP = nearest_ene.wrk_no;                       /* 1262 */
        }

        finderTriggerEneLifeDecrease(percentage);                       /* 1265 */
        finderEneLifeLen(1.0f);                                         /* 1269 */
    }
}                                                                       /* 1276 */

/* The off-screen arrow.  Only a highlighted (stm_view) ghost that is *not*
 * inside the ring qualifies -- once it is framed there is nothing to point at.
 *
 * The bearing is built in two stages: CalcAngle gives the yaw between the
 * camera's forward and the ghost, then the ghost direction is rotated by that
 * yaw so the remaining angle is pure elevation.  atan of the pair, pushed a
 * quarter turn to whichever side the yaw was on, is the screen bearing. */
static void NearestBattleEneDoJob_WithView(void)                        /* 1283 */
{
    NEAREST_ENE_MANAGE nearest_ene;
    float mMat[4][4];
    float vDir[4];
    float vAdjustXDir[4];
    float fRot;
    float fRotX;
    float fRotY;
    float fRad;
    int   i;

    nearest_ene.wrk_no = -1;                                            /* 1287 */
    nearest_ene.dist   = FLT_MAX;                                       /* 1288 */

    for (i = 0; i < ENE_WRK_MAX; i++)                                   /* 1289 */
    {
        if (ene_wrk[i].status == ENE_STATUS_ACT &&
            ene_wrk[i].type != 2 &&
            (ene_wrk[i].attr & 0x80) == 0 &&
            ene_wrk[i].stm_view != 0 &&
            (ene_wrk[i].st.sta & 0x400) == 0 &&
            ene_wrk[i].dist_p_e[0] < nearest_ene.dist)
        {
            nearest_ene.dist   = ene_wrk[i].dist_p_e[0];
            nearest_ene.wrk_no = ene_wrk[i].alg.idx;
        }
    }                                                                   /* 1307 */

    if (nearest_ene.wrk_no < 0)                                         /* 1310 */
    {
        m_plyr_camera.search_mark.FadeOut();
        return;
    }

    sceVu0SubVector(vDir, ene_wrk[nearest_ene.wrk_no].mpos.p0,          /* 1316 */
                    gra3dcamGetPosition());
    sceVu0Normalize(vDir, vDir);                                        /* 1319 */

    fRotY = CalcAngle(gra3dcamGetDirection(), vDir, g_v0100, ZX);       /* 1322 */
    fRot  = -fRotY;                                                     /* 1323 */

    sceVu0RotMatrixY(mMat, g_matUnit, fRotY);                           /* 1324 */
    sceVu0ApplyMatrix(vAdjustXDir, mMat, vDir);                         /* 1325 */
    sceVu0Normalize(vAdjustXDir, vAdjustXDir);                          /* 1326 */

    /* Signed elevation: acos of the reversed dot is pi when the ghost is dead
     * ahead, so subtracting pi centres it on zero. */
    fRad  = sceVu0InnerProduct(gra3dcamGetDirection(), vAdjustXDir);    /* 1327 */
    fRotX = g3dAcosf(-fRad) - ENE_PI;                                       /* 1332 */

    if (vAdjustXDir[1] > gra3dcamGetDirection()[1])                     /* 1333 */
    {
        fRotX = -fRotX;                                                 /* 1334 */
    }

    fRot = g3dAtanf(fRotX / fRot);                                      /* 1336 */
    if (fRot > 0.0f)                                                    /* 1338 */
    {
        fRot += ENE_PI / 2.0f;
    }
    else
    {
        fRot -= ENE_PI / 2.0f;
    }
    fRot = fRot * 180.0f / ENE_PI;                                          /* 1342 */

    if (ene_wrk[nearest_ene.wrk_no].bWithSearcher == 0)                 /* 1343 */
    {
        m_plyr_camera.search_mark.FadeOut();                            /* 1345 */
    }
    else
    {
        m_plyr_camera.search_mark.SetRot(fRot);                         /* 1347 */
        m_plyr_camera.search_mark.FadeIn();                             /* 1348 */
    }
}

/* Runs only the ghosts holding the photographed pose (action 4). */
void EnemyPhotoMain(void)                                               /* 1356 */
{
    int i;

    for (i = 0; i < ENE_WRK_MAX; i++)                                   /* 1360 */
    {
        ENE_WRK *ew = &ene_wrk[i];

        if (ew->act_no == 4)                                            /* 1362 */
        {
            EnemyWrkOne(ew, i);                                         /* 1363 */
        }
    }
}                                                                       /* 1365 */

/* The event-phase counterpart of EnemyMain.  A hostile ghost runs under the
 * act lock -- it animates but does not decide anything -- while a passive one
 * runs its whole rule, because a cutscene ghost is often the point of the
 * scene. */
void AutoEnemyMain(void)                                                /* 1370 */
{
    int i;

    if (enemy_draw_lock_cnt != 0) { return; }                           /* 1374 */

    for (i = 0; i < ENE_WRK_MAX; i++)                                   /* 1376 */
    {
        if (ene_wrk[i].type == 2)                                       /* 1378 */
        {
            EnemyWrkOne(&ene_wrk[i], i);                                /* 1379 */
        }
        else
        {
            EnemyLock();                                                /* 1382 */
            EnemyWrkOne(&ene_wrk[i], i);                                /* 1383 */
            EnemyUnlock();                                              /* 1384 */
        }
    }                                                                   /* 1386 */
}                                                                       /* 1387 */

/* ==========================================================================
 *  Slot queries
 * ======================================================================== */

ENE_STATUS GetEneDatStatus(int ene_type, int dat_no)
{
    int wrk_no = SearchEneWrkNo(ene_type, dat_no);                      /* 1395 */

    if (wrk_no >= 0)                                                    /* 1396 */
    {
        return (ENE_STATUS)ene_wrk[wrk_no].status;
    }
    return ENE_STATUS_NO_USE;                                           /* 1400 */
}

int IsActEnemy(int wrk_no)                                              /* 1403 */
{
    return (ene_wrk[wrk_no].status == ENE_STATUS_ACT);
}

/* A ghost counts as alive only once it is acting -- the loading / ready states
 * have HP but are not in the world yet. */
int IsAliveEnemy(int wrk_no)                                            /* 1409 */
{
    if (ene_wrk[wrk_no].status == ENE_STATUS_ACT &&
        ene_wrk[wrk_no].st.hp != 0)
    {
        return 1;                                                       /* 1413 */
    }
    return 0;                                                           /* 1414 */
}

/* Non-zero when nothing is mid-load.  The room load waits on this. */
int IsReadyAllEnemy(void)                                               /* 1417 */
{
    int i;

    for (i = 0; i < ENE_WRK_MAX; i++)                                   /* 1419 */
    {
        if (ene_wrk[i].status == ENE_STATUS_LOADING ||
            ene_wrk[i].status == ENE_STATUS_WAIT_ANI_CTRL)
        {
            return 0;                                                   /* 1423 */
        }
    }
    return 1;                                                           /* 1426 */
}                                                                       /* 1427 */

/* ==========================================================================
 *  Spawn
 * ======================================================================== */

/* Everything both ghost kinds set up when they start acting: position, facing,
 * light, the transparency terms, and the per-model effects.
 *
 * dir 1000 is the sentinel for "face the player"; anything else is degrees. */
static void SetCommonDat(ENE_WRK *ew, ENE_DAT_COMMON *dat, int hp)      /* 1431 */
{
    ANI_CTRL *ani_ctrl;
    float     rot;
    int       n1;
    int       n2;
    int       sclx;
    int       scly;

    /* attr 0x20000 is the one-hit shell: HP is forced to 1 whatever the table
     * says, so the fight ends on the first connected shot. */
    if ((ew->attr & 0x20000) != 0)                                      /* 1435 */
    {
        ew->st.hp = 1;
    }
    else
    {
        ew->st.hp = (u_short)hp;                                        /* 1436 */
    }

    _SetVector(ew->mbox.pos, dat->px, dat->py, dat->pz, 0.0f);          /* 1440 */

    if (dat->dir == 1000)                                               /* 1443 */
    {
        rot = GetTrgtRotY(ew->mbox.pos, plyr_wrk.cmn_wrk.mbox.pos);     /* 1444 */
    }
    else
    {
        rot = (float)dat->dir * ENE_PI / 180.0f;                            /* 1448 */
        RotLimitChk(&rot);                                              /* 1449 */
    }
    _SetVector(ew->mbox.rot, 0.0f, rot, 0.0f, 0.0f);                    /* 1451 */

    EneBlinkSet(ew);                                                    /* 1454 */
    SetEnemyParallelLight(ew, (float)dat->blg_r / 10.0f,                /* 1457 */
                              (float)dat->blg_g / 10.0f,
                              (float)dat->blg_b / 10.0f, 1.0f);
    _ClearVector(ew->neck_rot);                                         /* 1460 */

    ew->alg.stack_p     = ew->alg.stack_b;                              /* 1461 */
    ew->alg.branch      = 0;                                            /* 1462 */
    ew->tr_rate_alg     = 0;                                            /* 1466 */
    ew->tr_rate_alg_sp  = 0;                                            /* 1467 */
    ew->tr2_rate_alg    = 0x80;                                         /* 1469 */

    /* attr 0x2: the ghost fades in from nothing rather than appearing. */
    ew->tr_rate_out = ((ew->attr & 2) != 0) ? 0 : 0x80;                 /* 1470/1471 */
    ew->tr_rate_in  = 0x80;                                             /* 1472 */

    ew->effw          = 1.0f;                                           /* 1474 */
    ew->tr_frate      = 1.0f;                                           /* 1475 */
    ew->tr_common     = 1.0f;                                           /* 1476 */
    ew->stm_view      = 0;                                              /* 1477 */
    ew->stm_slow      = 0;                                              /* 1478 */
    ew->reso          = 1.0f;                                           /* 1479 */
    ew->wlk_reso      = 1.0f;                                           /* 1480 */
    ew->wlk_reso_chg  = 0.0f;                                           /* 1481 */
    ew->wlk_reso_frm  = 0;
    ew->wlk_reso_wait = 0;

    ani_ctrl = ew->ani_ctrl_p;

    /* attr 0x4: this model's cloth collides with the ghost's own body. */
    if ((ew->attr & 4) != 0)                                            /* 1485 */
    {
        acsSetEneCollision(ani_ctrl, (u_short)ani_ctrl->mdl_no);        /* 1487 */
    }
    MorphSetCtrl(ani_ctrl, ani_ctrl->mdl_no);                           /* 1491 */

    /* Model 0x23 carries a lantern, anchored to the weapon point. */
    if (ew->cmn_dat->mdl_no == 0x23)                                    /* 1494 */
    {
        ew->efpw = EffectSetTorch2(ew->mpos.p2, 5);                     /* 1495 */
    }

    /* Kusabi's haze -- hostile only; the passive version of the same model
     * appears in cutscenes without it.  The passive case jumps past the Sae
     * check too, which is the ROM's control flow (and is mirrored exactly by
     * the matching pair of cuts in EneRelease). */
    if (ew->cmn_dat->mdl_no == 3 || ew->cmn_dat->mdl_no == 0x12)        /* 1498 */
    {
        if (ew->type == 2) { goto skip_haze; }                          /* 1499 */

        ew->efpw = EffectKusabiHazeReq(ew->mbox.pos, ew->mbox.rot,      /* 1500 */
                                       &ew->tr_frate);
        ew->effw = 0.69999999f;                                         /* 1501 */
    }

    if (ew->type != 2 && (ew->dat_no == 0x45 || ew->dat_no == 0xf9))    /* 1505 */
    {
        ew->efpw = EffectSaeHazeReq(ew->mbox.pos, ew->mbox.rot,         /* 1506 */
                                    &ew->tr_frate);
    }

skip_haze:
    ChrSortRegistEnem(ew);                                              /* 1510 */

    /* The two parts-deform channels.  d_pda / d_pdc are the drive and the
     * aura gate; their product times tr_common is what the effect reads. */
    n1   = ew->cmn_dat->def_type[0];                                    /* 1517 */
    n2   = ew->cmn_dat->def_type[1];                                    /* 1518 */
    sclx = ew->cmn_dat->def_size[0];                                    /* 1519 */
    scly = ew->cmn_dat->def_size[1];                                    /* 1520 */

    ew->d_pd    = 0.0f;                                                 /* 1521 */
    ew->d_pd2   = 0.0f;                                                 /* 1522 */
    ew->d_pda   = 0.0f;                                                 /* 1523 */
    ew->d_pda2  = 0.0f;                                                 /* 1524 */
    ew->d_pdc   = 1.0f;                                                 /* 1526 */
    ew->d_pdc2  = 1.0f;

    if (n1 != 0)                                                        /* 1528 */
    {
        DEFORM_TYPE *d1 = &def_type1[n1];

        printf("SET PARTS-DEFORM1:%d\n", n1);                           /* 1529 */
        ew->d_mpd = (float)d1->alp;                                     /* 1530 */
        ew->pdf   = CallPartsDeform5_2(d1->no,                          /* 1544 */
                                       (float)sclx * 0.89999998f / 100.0f,
                                       (float)scly * 0.89999998f / 100.0f,
                                       ew, &ew->d_pd, &d1->spd, &d1->wave); /* 1547 */
    }

    if (n2 != 0)                                                        /* 1550 */
    {
        DEFORM_TYPE *d2 = &def_type2[n2];

        printf("SET PARTS-DEFORM2:%d\n", n1);                           /* 1551 */
        ew->d_mpd2 = (float)d2->alp;                                    /* 1552 */
        ew->pdf2   = CallPartsDeform5_2(d2->no,                         /* 1566 */
                                        (float)sclx / 100.0f,
                                        (float)scly / 100.0f,
                                        ew, &ew->d_pd2, &d2->spd, &d2->wave); /* 1569 */
    }

    /* dih_type selects a dit_type[] ramp; the effect reads the table itself. */
    {
        DITHER_TYPE *d = &dit_type[ew->cmn_dat->dih_type];

        if (ew->cmn_dat->dih_type != 0)                                 /* 1576 */
        {
            SetEffects_DITHER(4, (int)d->type, d->alp, d->spd,      /* 1578 */
                              (int)d->amax, (int)d->cmax, (u_int)d->in,
                              (u_int)d->keep, (u_int)d->out);          /* 1579 */
        }
    }
}                                                                       /* 1581 */

/* A passive ghost starts talking as it appears: the BGM ducks, and either the
 * line plays now or a countdown is armed for it. */
static void AutoEnemyStart(ENE_WRK *ew)                                 /* 1594 */
{
    SND_3D_SET set;
    int        adpcm_no;

    SetAtkTarget(ew, 0);                                                /* 1595 */

    if (ew->cmn_dat->se_no >= 0)                                        /* 1598 */
    {
        SndBankPlay(ew->se_bank_no, 0, 0, 0, 0x3200, 0x1000, 0, nullptr); /* 1600 */
    }

    if (ew->cmn_dat->adpcm_no >= 0)                                     /* 1604 */
    {
        map_bgmFadeOut(10, 0xa00);                                      /* 1605 */

        if (aene_dat[ew->dat_no].adpcm_tm == 0)                         /* 1608 */
        {
            adpcm_no = SubTitleStreamFileNoGet(ew->cmn_dat->adpcm_no);  /* 1612 */

            memset(&set, 0, sizeof(SND_3D_SET));                        /* 1614 */
            set.pos = (sceVu0FVECTOR *)ew->mbox.pos;                    /* 1615 */

            G3DASSERT(adpcm_no < BGM_END_DMY && adpcm_no > BGM_START_DMY,  "AutoEne %d SubTitle Adpcm Is Illegal %d", ew->cmn_dat->adpcm_no, adpcm_no);

            ew->stream_id = StreamAutoPlay(adpcm_no, adpcm_no - 1, 0x10, 0, 0, 0x3200, 0, &set);            /* 1625 */
            SubTitleReqAutoEnemy(ew->cmn_dat->adpcm_no, ew->stream_id); /* 1627 */
        }
        else
        {
            ew->adpcm_tm.Wait(aene_dat[ew->dat_no].adpcm_tm);
        }
    }

    SetCommonDat(ew, ew->cmn_dat, 100);                                 /* 1633 */
    EneActSet(ew, 1);                                                   /* 1636 */

    printf("\n\n### AUTO ENE Entry.  < ENE_DAT No. %d > < ENE_WRK No. %d > <ANI_CTRL %x>###\n\n",
           ew->dat_no, ew->alg.idx, ew->ani_ctrl_p);                    /* 1638 */
}

/* A hostile ghost ("jibaku" -- earthbound) starts with its cry, the BGM cut to
 * nothing, and its aura raised. */
static void JibakuEnemyStart(ENE_WRK *ew)                               /* 1647 */
{
    SND_3D_SET set;

    ChangeAtkTargetRnd(ew);                                             /* 1653 */

    if (ew->cmn_dat->adpcm_no >= 0)                                     /* 1654 */
    {
        memset(&set, 0, sizeof(SND_3D_SET));                            /* 1655 */
        set.pos = (sceVu0FVECTOR *)ew->mbox.pos;

        ew->stream_id = StreamAutoPlay(ew->cmn_dat->adpcm_no,           /* 1660 */
                                       ew->cmn_dat->adpcm_no - 1,
                                       0x13, 0, 1, 0x3200, 0x1e, &set);
    }

    map_bgmFadeOut(10, 0);                                              /* 1662 */

    if (ew->nee == nullptr)                                             /* 1665 */
    {
        ew->nee_size = 10000.0f;                                        /* 1666 */
        ew->nee_rate = 1.0f;                                            /* 1667 */
        ew->nee_col  = 0x40404500 | ew->dat->aura_alp;                  /* 1668 */
        ew->nee      = SetEffects_ENEFIRE(2, 1, ew->mbox.pos, &ew->mpos, /* 1669 */
                                         &ew->nee_col, &ew->nee_size, 0xa0,
                                         &ew->nee_rate);
    }

    SetCommonDat(ew, ew->cmn_dat, ew->dat->hp);                         /* 1673 */
    EneActSet(ew, 7);                                                   /* 1676 */

    printf("\n\n### JIBAKU ENE Entry.  < ENE_DAT No. %d > < ENE_WRK No. %d > <ANI_CTRL %x>###\n\n",
           ew->dat_no, ew->alg.idx, ew->ani_ctrl_p);                    /* 1678 */
}

/* Inline in the ROM -- it lands in its own .gnu.linkonce.t section, which is
 * how GCC 2.9x emits an out-of-line copy of an `inline` function. */
inline void EnemyStart(int wrk_no)                                      /* 1683 */
{
    ENE_WRK *ew = &ene_wrk[wrk_no];                                     /* 1685 */

    /* Any ghost appearing kills the projector cutscene. */
    movie_projecterStop();                                              /* 1688 */

    if (ew->type < 2)                                                   /* 1691 */
    {
        JibakuEnemyStart(ew);                                           /* 1694 */
    }
    else if (ew->type == 2)                                             /* 1695 */
    {
        AutoEnemyStart(ew);                                             /* 1697 */
    }
}

/* ==========================================================================
 *  Slot allocation
 * ======================================================================== */

/* ene_type 0 and 1 both address jene_dat; 2 addresses aene_dat. */
static void *GetEneDatP(int ene_type, int dat_no)                       /* 1706 */
{
    if (ene_type >= 0)                                                  /* 1707 */
    {
        if (ene_type < 2)                                               /* 1712 */
        {
            return &jene_dat[dat_no];
        }
        if (ene_type == 2)                                              /* 1715 */
        {
            return &aene_dat[dat_no];
        }
    }

    PRINT_ASSERT("GetEneDatP type is Illegal");                         /* 1717 */
    return nullptr;                                                     /* 1718 */
}                                                                       /* 1720 */

int SearchEneWrkNo(int ene_type, int dat_no)                            /* 1724 */
{
    int i;

    for (i = 0; i < ENE_WRK_MAX; i++)                                   /* 1727 */
    {
        if (ene_wrk[i].status != ENE_STATUS_NO_USE &&
            ene_wrk[i].dat_no == dat_no &&
            ene_wrk[i].type == ene_type)
        {
            return i;                                                   /* 1733 */
        }
    }
    return -1;                                                          /* 1735 */
}                                                                       /* 1736 */

static int GetEneWrkBuffer(void)                                        /* 1740 */
{
    int i;

    for (i = 0; i < ENE_WRK_MAX; i++)                                   /* 1743 */
    {
        if (ene_wrk[i].status == ENE_STATUS_NO_USE)
        {
            return i;                                                   /* 1747 */
        }
    }
    return -1;                                                          /* 1748 */
}                                                                       /* 1749 */

/* Free every ghost the event system has not asked to act, except wrk_no.
 * mmanage's ModelMemoryFree() escalates through this when the heap is short. */
int PreloadedEneAllRelease(int wrk_no)                                  /* 1753 */
{
    int i;
    int rel_flg = 0;                                                    /* 1755 */

    for (i = 0; i < ENE_WRK_MAX; i++)                                   /* 1756 */
    {
        if (ene_wrk[i].status != ENE_STATUS_NO_USE &&
            ene_wrk[i].act_flg == 0 &&
            i != wrk_no)
        {
            rel_flg = 1;                                                /* 1760 */
            EneRelease(i);
            printf("EneWrk %d Release\n", i);                           /* 1761 */
        }
    }
    return rel_flg;                                                     /* 1764 */
}                                                                       /* 1766 */

void EneAllRelease(void)                                                /* 1770 */
{
    int i;

    for (i = 0; i < ENE_WRK_MAX; i++)                                   /* 1772 */
    {
        if (ene_wrk[i].status != ENE_STATUS_NO_USE)
        {
            EneRelease(i);                                              /* 1774 */
        }
    }
}                                                                       /* 1776 */

/* Free one slot -- or, for a passive ghost with a `next` entry, re-seat it on
 * the hostile entry instead of freeing it.  That is the transformation: the
 * slot keeps its position and facing, swaps table pointer, type and sound bank,
 * and re-runs the animation hand-off.  Returning 0 says the slot is still in
 * use, so the caller must not treat it as reclaimed. */
static int EneRelease(int wrk_no)                                       /* 1781 */
{
    ENE_WRK *ew = &ene_wrk[wrk_no];                                     /* 1785 */
    float    pos[4];
    float    rot[4];

    ChrSortDeleteEnem(ew);                                              /* 1787 */

    if (ew->type < 2)                                                   /* 1790 */
    {
        StreamAutoFadeOut(ew->stream_id, 0x14);                         /* 1793 */

        if (ew->status == ENE_STATUS_ACT || ew->status == ENE_STATUS_RELEASE) /* 1795 */
        {
            map_bgmFadeIn(0x32);
        }
        release_typeRegister(ew->type, ew->dat_no, ew->rel_type);       /* 1798 */
    }
    else if (ew->type == 2)                                             /* 1799 */
    {
        release_typeRegister(2, ew->dat_no, ew->rel_type);              /* 1807 */

        if (ew->cmn_dat->adpcm_no >= 0 &&                               /* 1810 */
            (ew->status == ENE_STATUS_ACT || ew->status == ENE_STATUS_RELEASE))
        {
            map_bgmFadeIn(0x32);                                        /* 1813 */
        }

        if (ew->aie->next >= 0)                                         /* 1817 */
        {
            g3dxVu0CopyVector(pos, ew->mbox.pos);                       /* 1821 */
            g3dxVu0CopyVector(rot, ew->mbox.rot);

            enemyReleaseDataSub(ew);                                    /* 1824 */

            ew->se_bank_no = ew->se_bank_jibaku_no;                     /* 1825 */
            ew->dat_no     = ew->aie->next;                             /* 1826 */
            ew->type       = 0;                                         /* 1827 */
            ew->cmn_dat    = &jene_dat[ew->aie->next].cmn;              /* 1829 */
            ew->dat        = &jene_dat[ew->aie->next];

            enemyInitAnmSub(wrk_no);                                    /* 1836 */

            ew->aie    = nullptr;                                       /* 1838 */
            ew->st.sta = 0;                                             /* 1840 */

            g3dxVu0CopyVector(ew->mbox.pos, pos);                       /* 1845 */
            g3dxVu0CopyVector(ew->mbox.rot, rot);
            return 0;
        }
    }

    if (ew->nee != nullptr)                                             /* 1856 */
    {
        ResetEffects(ew->nee);                                          /* 1858 */
    }
    if (ew->pdf != nullptr)                                             /* 1860 */
    {
        ResetEffects(ew->pdf);                                          /* 1862 */
    }
    if (ew->pdf2 != nullptr)                                            /* 1864 */
    {
        ResetEffects(ew->pdf2);                                         /* 1866 */
    }

    if (ew->cmn_dat->mdl_no == 0x23)                                    /* 1870 */
    {
        EffectResetTorch2(ew->efpw);                                    /* 1871 */
    }

    if (ew->cmn_dat->mdl_no == 3 || ew->cmn_dat->mdl_no == 0x12)        /* 1874 */
    {
        if (ew->type != 2)                                              /* 1875 */
        {
            EffectKusabiHazeCut(ew->efpw);                              /* 1876 */
        }
        else
        {
            goto skip_effect_cut;
        }
    }

    if (ew->type != 2 && (ew->dat_no == 0x45 || ew->dat_no == 0xf9))    /* 1880 */
    {
        EffectSaeHazeCut(ew->efpw);                                     /* 1881 */
    }

skip_effect_cut:
    enemyReleaseDataSub(ew);                                            /* 1898 */

    if (ew->cmn_dat->mdl_no == 1)                                       /* 1900 */
    {
        mmanageClearMdl(GetSisterMdlNo());                              /* 1901 */
    }
    else
    {
        mmanageClearMdl(ew->cmn_dat->mdl_no);                           /* 1904 */
    }

    ew->status = ENE_STATUS_NO_USE;                                     /* 1908 */
    printf("\n### ENE_WRK Release / WorkNo = %d / DataNo = %d ###\n",
           ew->alg.idx, ew->dat_no);

    memset(ew, 0, sizeof(ENE_WRK));                                     /* 1911 */
    return 1;                                                           /* 1916 */
}                                                                       /* 1917 */

/* Reserve a slot for (ene_type, ene_dat_no) and post its loads.  The slot goes
 * straight to LOADING; EnemyWrkOne() polls it from there.  A slot whose data
 * has already landed skips LOADING entirely. */
MMANAGE_ERR EneLoadReq(int ene_type, int ene_dat_no, int *pWrkNo)       /* 1923 */
{
    ENE_WRK    *ew;
    void       *dat;
    MMANAGE_ERR Err;
    int         wrk_no;
    int         i;

    dat = GetEneDatP(ene_type, ene_dat_no);                             /* 1930 */

    /* Two live copies of the same table entry would fight over its state. */
    for (i = 0; i < ENE_WRK_MAX; i++)                                   /* 1931 */
    {
        if (ene_wrk[i].status != ENE_STATUS_NO_USE &&
            ene_wrk[i].dat_no == ene_dat_no &&
            ene_wrk[i].type == ene_type)
        {
            PRINT_ASSERT("ILLEGAL!!! SAME ENE DAT NO type %d no %d",    /* 1936 */
                         ene_type, ene_dat_no);
            break;
        }
    }

    wrk_no = GetEneWrkBuffer();                                         /* 1944 */
    if (pWrkNo != nullptr)                                              /* 1947 */
    {
        *pWrkNo = wrk_no;
    }

    if (wrk_no < 0)                                                     /* 1950 */
    {
        PRINT_ASSERT("EneWrkIsNotFree");                                /* 1957 */
        return OL_LOAD_ERR_WORK_LACK;                                   /* 1958 */
    }

    ew = &ene_wrk[wrk_no];                                              /* 1960 */
    memset(ew, 0, sizeof(ENE_WRK));                                     /* 1963 */

    ew->slow_hb_wait_frame = -1;                                        /* 1964 */
    ew->se_bank_no         = -1;                                        /* 1965 */
    ew->combo_counter      = -1;                                        /* 1967 */
    ew->combo_sb_counter   = -1;                                        /* 1968 */
    ew->stream_id          = -1;                                        /* 1969 */
    ew->adpcm_tm.Reset();
    ew->se_bank_jibaku_no  = -1;

    if (ene_type >= 0)                                                  /* 1979 */
    {
        if (ene_type < 2)                                               /* 1982 */
        {
            ew->cmn_dat = &((ENE_DAT *)dat)->cmn;                       /* 1985 */
            ew->dat     = (ENE_DAT *)dat;                               /* 1987 */
            ew->aie     = nullptr;                                      /* 1988 */
        }
        else if (ene_type == 2)                                         /* 1992 */
        {
            ew->cmn_dat = &((AENE_DAT *)dat)->cmn;                      /* 1993 */
            ew->dat     = nullptr;                                      /* 1994 */
            ew->aie     = (AENE_DAT *)dat;                              /* 1996 */
        }
    }

    ew->dat_no   = (short)ene_dat_no;                                   /* 1999 */
    ew->type     = (u_char)ene_type;                                    /* 2000 */
    ew->attr     = ew->cmn_dat->attr;                                   /* 2001 */
    ew->alg.idx  = (u_char)wrk_no;                                      /* 2004 */
    ew->rel_type = release_typeGetReleaseType(ene_type, ene_dat_no);    /* 2007 */

    Err = enemyReqData(ew);                                             /* 2011 */
    if (Err == OL_LOAD_ERR_OK)                                          /* 2012 */
    {
        if (enemyIsReadyData(ew) == 0)                                  /* 2014 */
        {
            ew->status = ENE_STATUS_LOADING;
        }
        else
        {
            enemyInitAnmSub(wrk_no);
        }
    }
    else if (pWrkNo != nullptr)
    {
        *pWrkNo = -1;                                                   /* 2018 */
    }

    return Err;                                                         /* 2019 */
}

/* Ask a slot to start acting, loading it first if it is not resident.  Returns
 * 0 while the data is still on its way, so the caller retries. */
int EneActReq(int ene_type, int dat_no)                                 /* 2026 */
{
    ENE_WRK *ew;
    int      wrk_no;

    wrk_no = SearchEneWrkNo(ene_type, dat_no);                          /* 2029 */

    if (wrk_no < 0)                                                     /* 2032 */
    {
        EneLoadReq(ene_type, dat_no, &wrk_no);                          /* 2033 */
        if (wrk_no < 0)                                                 /* 2034 */
        {
            ModelMemoryFree(wrk_no);                                    /* 2035 */
            return 0;
        }
    }

    ew = &ene_wrk[wrk_no];
    ew->act_flg = 1;                                                    /* 2040 */

    if (ew->status == ENE_STATUS_READY)                                 /* 2043 */
    {
        EnemyStart(wrk_no);                                             /* 2046 */
        ew->status = ENE_STATUS_ACT;                                    /* 2047 */
    }
    else if (ew->status == ENE_STATUS_LOADING)                          /* 2050 */
    {
        return 0;
    }
    return 1;                                                           /* 2051 */
}

/* ==========================================================================
 *  Combo
 * ======================================================================== */

/* Advance the combo counter and hand back the multiplier for this shot.  Past
 * the third hit the chain restarts rather than continuing. */
int EneComboReq(ENE_WRK *ew, int sw_flg)                                /* 2064 */
{
    if (ew->combo_counter > 1)                                          /* 2065 */
    {
        ew->combo_counter    = -1;                                      /* 2066 */
        ew->combo_sb_counter = -1;
    }
    if (sw_flg != 0)                                                    /* 2069 */
    {
        ew->combo_sb_counter++;                                         /* 2070 */
    }
    ew->combo_time = 0;                                                 /* 2073 */
    ew->combo_counter++;

    return ew_combo_tbl[(u_char)ew->combo_counter];                     /* 2078 */
}

/* Non-zero while any live ghost is still inside its combo window.  The counter
 * is read unsigned, so the idle -1 reads as 255 and does not qualify. */
int IsInCombo(void)                                                     /* 2085 */
{
    int ret = 0;                                                        /* 2086 */
    int i;

    for (i = 0; i < ENE_WRK_MAX; i++)                                   /* 2089 */
    {
        if (ene_wrk[i].status == ENE_STATUS_ACT &&                      /* 2091 */
            ene_wrk[i].st.hp != 0 &&
            (u_char)ene_wrk[i].combo_counter < 3)
        {
            ret = 1;                                                    /* 2092 */
        }
    }
    return ret;                                                         /* 2096 */
}                                                                       /* 2097 */

/* Three seconds without another hit ends the chain. */
static void EneComboCtrl(ENE_WRK *ew)
{
    if (ew->combo_time > 179)                                           /* 2125 */
    {
        ew->combo_counter    = -1;                                      /* 2127 */
        ew->combo_sb_counter = -1;
    }
    else
    {
        ew->combo_time++;                                               /* 2128 */
    }
}                                                                       /* 2131 */

void EneReleaseReq(int ene_type, int dat_no)
{
    int wrk_no = SearchEneWrkNo(ene_type, dat_no);                      /* 2141 */

    if (wrk_no < 0)                                                     /* 2144 */
    {
        printf("Enemy type[%d] [%d] Is Already Released\n", ene_type, dat_no); /* 2145 */
        return;
    }

    printf("Enemy type[%d] [%d] Is Forcely Released\n", ene_type, dat_no); /* 2149 */
    EneRelease(wrk_no);
}                                                                       /* 2151 */

void EnemyMotionWork(void)                                              /* 2160 */
{
    int i;

    for (i = 0; i < ENE_WRK_MAX; i++)                                   /* 2163 */
    {
        if (ene_wrk[i].status == ENE_STATUS_ACT)
        {
            EnemyMotionWorkOne(&ene_wrk[i]);
        }
    }
}                                                                       /* 2167 */

/* Empty in the prototype. */
static void EnemyMotionWorkOne(ENE_WRK *ew)
{
    (void)ew;
}                                                                       /* 2186 */

/* ==========================================================================
 *  Draw
 * ======================================================================== */

/* Build the ghost's own light set: the room's, emulated at its waist, with
 * directional light 2 overridden to point straight back down the camera's
 * forward axis in the ghost's own colour.  That is what makes a ghost read as
 * lit from the camera rather than by the room. */
static void _enemySetLight(ENE_WRK *pEW)
{
    GRA3DLIGHTDATA                     LD;
    GRA3DEMULATIONLIGHTDATACREATIONDATA eldcd;

    LD = *MapDrawGetLightPtr(GetPlyrAreaNo());                          /* 2203 */

    eldcd.vStaticDirLightColor[0] = 0.0f;                               /* 2209 */
    eldcd.vStaticDirLightColor[1] = 0.0f;
    eldcd.vStaticDirLightColor[2] = 0.0f;
    eldcd.vStaticDirLightColor[3] = 0.0f;
    eldcd.fAngleScale             = 1.0f;
    eldcd.fDiffuseScale           = 1.0f;
    eldcd.fMaplightScale          = 1.0f;
    eldcd.bEnableSelfreflection   = 0;
    eldcd.bEmulateSelfreflection  = 0;
    eldcd.bEnableFlashlight       = 1;
    eldcd.bEmulateFlashlight      = 1;
    eldcd.bEnableFlashlight2      = 1;
    eldcd.bEmulateFlashlight2     = 1;
    eldcd.bEnableStaticDirLight   = 0;

    gra3dGenerateLightDataToChar(&LD, MapDrawGetLightPtr(GetPlyrAreaNo()), &eldcd); /* 2224 */
    gra3dEmulateLightData(&LD, &LD, pEW->mpos.p1, eldcd.fMaplightScale); /* 2226 */

    sceVu0ScaleVector(LD.aLight[LID_DIRECTIONAL_2].vDirection,
                      gra3dGetCamera()->matCoord[2], -1.0f);
    g3dxVu0CopyVector(LD.aLight[LID_DIRECTIONAL_2].vDiffuse,
                     pEW->directionaldiffuse);
    LD.aStatus[LID_DIRECTIONAL_2].bEnable = 1;

    gra3dSetLightData(&LD, nullptr);                                    /* 2235 */
}

float (*EnemyGetMatrix(ENE_WRK *ew))[4][4]                              /* 2247 */
{
    ANI_CTRL *ani_ctrl = ew->ani_ctrl_p;                                /* 2248 */

    if (ani_ctrl->base_p != nullptr)                                    /* 2250 */
    {
        return &ani_ctrl->base_p->coordp->matCoord;                     /* 2255 */
    }
    return nullptr;                                                     /* 2257 */
}

float (*FlyGetMatrix(FLY_WRK *fw))[4][4]                                /* 2267 */
{
    ANI_CTRL *ani_ctrl = fw->ani_ctrl;                                  /* 2268 */

    if (ani_ctrl->base_p != nullptr)                                    /* 2270 */
    {
        return &ani_ctrl->base_p->coordp->matCoord;                     /* 2275 */
    }
    return nullptr;                                                     /* 2276 */
}

/* Draw one ghost.  The bounding-box test is a cull, not a correctness check:
 * charbbGet() failing (no box at all) draws anyway.  Two passive ghosts --
 * data numbers 2 and 0x22 -- are drawn unconditionally, because they are
 * scripted appearances that must not be culled out of their own scene. */
void EnemyDrawOne(ENE_WRK *ew)                                          /* 2292 */
{
    ANI_CTRL      *ani_ctrl;
    SGDFILEHEADER *pSGDTop;
    u_int          mdl_no;
    float          avWorldBB[8][4];
    int            flg;

    if (GetEneDrawFLG() == 0)              { return; }                  /* 2300 */
    if (enemy_draw_lock_cnt != 0)          { return; }                  /* 2304 */
    if ((ew->st.sta & 0x2000000) != 0)     { return; }                  /* 2308 */

    ani_ctrl = ew->ani_ctrl_p;
    mdl_no   = ani_ctrl->mdl_no;

    _EnemyAnimationProc(ew);

    GetMdlNeckPos(ew->mpos.p0, ani_ctrl, (u_short)mdl_no);              /* 2320 */
    GetMdlWaistPos(ew->mpos.p1, ani_ctrl, (u_short)mdl_no);             /* 2321 */
    motGetBukiUpPos(ew->mpos.p2, ani_ctrl);                             /* 2322 */
    motGetBukiDownPos(ew->mpos.p3, ani_ctrl);                           /* 2323 */
    motGetBukiSpeAPos(ew->mpos.p4, ani_ctrl);                           /* 2324 */
    motGetBukiSpeBPos(ew->mpos.p5, ani_ctrl);                           /* 2325 */

    if (ew->tr_rate == 0) { return; }                                   /* 2328 */

    flg      = 0;
    ani_ctrl = ew->ani_ctrl_p;                                          /* 2332 */
    pSGDTop  = (SGDFILEHEADER *)ani_ctrl->base_p;                       /* 2334 */

    MorphRun(ani_ctrl, ani_ctrl->mpk_p);                                /* 2338 */
    acsClothCtrl(ani_ctrl, ani_ctrl->mpk_p, mdl_no, 0);                 /* 2340 */

    if (charbbGet(avWorldBB, ani_ctrl,                                  /* 2361 */
                  ani_ctrl->base_p->coordp.get()->matCoord) == 0 ||
        CheckModelBoundingBox(avWorldBB) != 0)                          /* 2364 */
    {
        flg = 1;                                                        /* 2365 */
    }

    if (ew->type == 2 && (ew->dat_no == 2 || ew->dat_no == 0x22))       /* 2367 */
    {
        flg = 1;                                                        /* 2369 */
    }

    if (flg)                                                            /* 2371 */
    {
        gra3dLightEnablePush();                                         /* 2377 */
        _enemySetLight(ew);                                             /* 2379 */

        if (gra3dIsMonotoneDrawEnable())                                /* 2383 */
        {
            SendEneVramMono(ani_ctrl->mdl_p, 0x2bc0, ani_ctrl->bwc_p);  /* 2384 */
        }
        else
        {
            SendEneVram(ani_ctrl->mdl_p, 0x2bc0);                       /* 2387 */
        }

        ManmdlSetAlpha(pSGDTop, ew->tr_rate);                           /* 2390 */
        _gra3dDrawSGD(pSGDTop, SRT_REALTIME, nullptr, -1);              /* 2391 */

        /* Models flagged in motCheckTrRateMdl() carry a second alpha for the
         * sub-objects, so a ghost's clothing can fade at its own rate. */
        if (motCheckTrRateMdl(ew->cmn_dat->mdl_no) == 0)                /* 2392 */
        {
            DrawEneSubObj(ani_ctrl->mpk_p, ew->tr_rate, ew->tr_rate);   /* 2393 */
        }
        else
        {
            DrawEneSubObj(ani_ctrl->mpk_p, ew->tr_rate, ew->tr_rate_alg_sp); /* 2395 */
        }

        gra3dLightEnablePop();                                          /* 2398 */
    }

    MorphReset(ani_ctrl, ani_ctrl->mpk_p);                              /* 2402 */
    _SetPREVIOUSTRI2PRIM(nullptr);                                      /* 2405 */
}                                                                       /* 2406 */

/* The flying creatures' own animation step.  Same root-bone construction as
 * the ghosts', except a creature also pitches, and it is placed at its own
 * adjusted position rather than a MOVE_BOX. */
void flyAnimationProc(FLY_WRK *fw)                                      /* 2421 */
{
    ANI_CTRL      *ani_ctrl = fw->ani_ctrl;
    HeaderSection *hs;
    SGDCOORDINATE *cp;
    u_int          mdl_no;
    float          grot;

    if (ani_ctrl == nullptr) { return; }                                /* 2428 */

    hs     = ani_ctrl->base_p;                                          /* 2430 */
    mdl_no = fw->mdl_no;

    motSetCoord(ani_ctrl, 0, (u_char)(enemy_anim_lock_cnt != 0));       /* 2440 */
    mimSetVertex(ani_ctrl);                                             /* 2442 */

    if (hs != nullptr)                                                  /* 2446 */
    {
        cp = hs->coordp;                                                /* 2449 */

        sceVu0UnitMatrix(cp->matCoord);                                 /* 2450 */
        cp->matCoord[0][0] = 25.0f;                                     /* 2451 */
        cp->matCoord[1][1] = 25.0f;
        cp->matCoord[2][2] = 25.0f;

        grot = ENE_PI - fw->nrot[0];                                        /* 2452 */
        if (grot > ENE_PI) { grot -= ENE_PI2; }                                 /* 2453 */
        sceVu0RotMatrixX(cp->matCoord, cp->matCoord, grot);             /* 2454 */

        grot = fw->nrot[1] + ENE_PI;                                        /* 2455 */
        if (grot > ENE_PI) { grot -= ENE_PI2; }                                 /* 2456 */
        sceVu0RotMatrixY(cp->matCoord, cp->matCoord, grot);             /* 2457 */

        g3dxVu0CopyVector(cp->matCoord[3], fw->adjv);                   /* 2458 */
        cp->matCoord[3][3] = 1.0f;                                      /* 2459 */

        sgdCalcBoneCoordinate(cp, hs->blocks - 1);                      /* 2460 */
        acsClothCtrl(ani_ctrl, ani_ctrl->mpk_p, mdl_no, 0);             /* 2463 */
    }
}                                                                       /* 2468 */

int enemyIsFlyDraw(FLY_WRK *fw)                                         /* 2473 */
{
    if (enemy_draw_lock_cnt != 0) { return 0; }                         /* 2474 */
    if ((fw->sta & 1) == 0)       { return 0; }                         /* 2476 */
    return 1;                                                           /* 2477 */
}

int FlyDrawOne(FLY_WRK *fw)                                             /* 2489 */
{
    ANI_CTRL      *ani_ctrl;
    SGDFILEHEADER *pSGDTop;

    if (enemyIsFlyDraw(fw) == 0) { return 0; }                          /* 2492 */

    ani_ctrl = fw->ani_ctrl;                                            /* 2497 */
    pSGDTop  = (SGDFILEHEADER *)ani_ctrl->base_p;

    flyAnimationProc(fw);                                               /* 2509 */

    gra3dLightEnablePush();                                             /* 2511 */
    SetFlyLight(fw);                                                    /* 2514 */
    SendEneVram(ani_ctrl->mdl_p, 0x2bc0);                               /* 2515 */
    ManmdlSetAlpha(pSGDTop, fw->alp);                                   /* 2516 */
    _gra3dDrawSGD(pSGDTop, SRT_REALTIME, nullptr, -1);                  /* 2517 */
    DrawEneSubObj(ani_ctrl->mpk_p, fw->alp, fw->alp);                   /* 2519 */
    gra3dLightEnablePop();                                              /* 2521 */

    return 1;                                                           /* 2522 */
}

/* Empty in the prototype -- a developer's debug hook that survived into the
 * build with its body compiled out. */
static void OkuraSanEnemyDebug(void)
{
}                                                                       /* 2561 */

/* ==========================================================================
 *  The rule
 * ======================================================================== */

/* One acting ghost, one frame.  Passive ghosts get the short path: they move,
 * fade and animate but make no decisions.  Hostile ghosts additionally run
 * the damage, target and action-selection chain before the script steps.
 *
 * The 0x1c0000000 test is the three "script suspended" status bits. */
static void EneRule(ENE_WRK *ew)                                        /* 2669 */
{
    u_char act_no;
    u_char view_chk;

    if (IsSisWrk() != 0)                                                /* 2670 */
    {
        ew->dist_p_e_o[1] = ew->dist_p_e[1];                            /* 2671 */
        ew->dist_p_e[1]   = GetDistV(sis_wrk.cmn_wrk.mbox.pos, ew->mbox.pos);
    }

    EneInDispChk(ew);                                                   /* 2674 */
    EneLightCtrl(ew);                                                   /* 2675 */
    EneBlinkPosSet(ew);                                                 /* 2676 */

    if (ew->type == 2)                                                  /* 2678 */
    {
        EneMoveCtrl(ew);                                                /* 2680 */
        EneAlphaCtrl(ew);                                               /* 2681 */
        EneAuraCtrl(ew);                                                /* 2682 */
        EneAniResolutionCtrl(ew);                                       /* 2683 */
    }
    else
    {
        EneSlowHitBackCtrl(ew);                                         /* 2687 */
        EneComboCtrl(ew);                                               /* 2688 */
        EneCondCtrl(ew);                                                /* 2689 */

        if (debug_var.ene_stop == 0)                                    /* 2694 */
        {
            EneMoveCtrl(ew);                                            /* 2695 */
        }

        view_chk = EnePRecogChkChk(ew);                                 /* 2701 */
        if (view_chk == 0)                                              /* 2702 */
        {
            act_no = ew->act_no;                                        /* 2704 */
        }
        else
        {
            view_chk = EnePRecogChk(ew, &act_no);                       /* 2705 */
        }

        EneActIniChk(ew, view_chk, act_no);                             /* 2707 */
        EneActRule(ew);                                                 /* 2708 */
        EneHPRecv(ew);                                                  /* 2709 */
        EneAlphaCtrl(ew);                                               /* 2710 */
        EneAuraCtrl(ew);                                                /* 2711 */
        EneAniResolutionCtrl(ew);                                       /* 2712 */
    }

    if ((ew->st.sta & 0x1c0000000L) == 0 && debug_var.ene_stop == 0)    /* 2715 */
    {
        EneAlgCtrl(ew);                                                 /* 2720 */
    }

    EneMotAlgCtrl(ew);                                                  /* 2721 */
    EneBlinkCtrl(ew);                                                   /* 2729 */
}                                                                       /* 2732 */

/* Whether the camera has a shot lined up, and of what quality.  Tested against
 * sta_old, not sta: the answer has to describe the frame the player is looking
 * at, which the rule has already moved past. */
SHUTTER_CHANCE_STATE ShutterChanceChk(void)                             /* 2741 */
{
    SHUTTER_CHANCE_STATE result = SHUTTER_CHANCE_NONE;
    int i;

    for (i = 0; i < ENE_WRK_MAX; i++)                                   /* 2745 */
    {
        ENE_WRK    *ew  = &ene_wrk[i];
        STATUS_DAT *ews = &ew->st;

        if (ew->status != ENE_STATUS_ACT)     { continue; }             /* 2747 */
        if (ews->hp == 0)                     { continue; }             /* 2750 */
        if ((ews->sta_old & 0x1000000) != 0)  { continue; }

        /* 0x2000 is the ghost's own "vulnerable now" window; 0x80 is inside
         * the ring.  Both together is the fatal-frame shot. */
        if ((ews->sta_old & 0x2080) == 0x2080)                          /* 2756 */
        {
            result = SHUTTER_CHANCE_SP;                                 /* 2757 */
            break;
        }
        if ((ews->sta_old & 0x80) != 0 && (ews->sta_old & 0x11000) != 0) /* 2760 */
        {
            result = SHUTTER_CHANCE_NORMAL;                             /* 2766 */
            break;
        }
    }
    return result;                                                      /* 2767 */
}

/* ==========================================================================
 *  Action scripts
 * ======================================================================== */

/* Point the interpreter at action act_no.  The script pack is a three-level
 * offset table -- ghost type, then algorithm number, then action -- and each
 * level is read as a little-endian 16-bit offset from the pack base, which is
 * why it is walked a byte at a time.
 *
 * Every clear at the end drops a per-action status bit, so a new action never
 * inherits the last one's attack, hit or recovery flags. */
void EneActSet(ENE_WRK *ew, u_char act_no)                              /* 2780 */
{
    MOVE_BOX   *mb  = &ew->mbox;                                        /* 2781 */
    ENEALG_WRK *alg = &ew->alg;                                         /* 2782 */
    void       *temp;
    intptr_t    eact_data_addr;
    u_char     *p;

    alg_manageIsReadyAlg(ew->cmn_dat->alg_no, &temp);                   /* 2789 */
    eact_data_addr = (intptr_t)temp;                                    /* 2790 */

    ew->act_no = act_no;                                                /* 2792 */

    alg->comm_add_top = eact_data_addr;                                 /* 2793 */

    p = (u_char *)(eact_data_addr + ew->type * 2);            /* 2794 */
    p = (u_char *)(eact_data_addr + (p[0] + p[1] * 0x100));   /* 2795/2796 */
    p += ew->cmn_dat->alg_no * 2;                                       /* 2797 */
    p = (u_char *)(eact_data_addr + (p[0] + p[1] * 0x100));   /* 2798/2799 */
    p += act_no * 2;                                                    /* 2800 */

    alg->comm_add.wrk = eact_data_addr + (p[0] + p[1] * 0x100);         /* 2801/2802 */
    alg->pos_no    = 0;                                                 /* 2803 */
    alg->wait_time = 1.0f;                                              /* 2804 */

    _ClearVector(mb->spd);                                              /* 2806 */
    _ClearVector(mb->rspd);                                             /* 2807 */
    mb->rot[2] = 0.0f;                                                  /* 2808 */

    ew->st.sta &= ~0x00080000L;                                         /* 2809 */
    ew->st.sta &= ~0x00020000L;                                         /* 2810 */
    ew->st.sta &= ~0x00040000L;                                         /* 2811 */
    ew->st.sta &= ~0x04000000L;                                         /* 2812 */
    ew->st.sta &= ~0x01000000L;                                         /* 2813 */
    ew->st.sta &= ~0x00010000L;                                         /* 2814 */

    alg->stack_p = alg->stack_b;                                        /* 2819 */
}

/* The blink script's cursor.  Same table walk, one level shallower -- there is
 * one blink script per algorithm, not one per action. */
void EneBlinkSet(ENE_WRK *ew)                                           /* 2827 */
{
    ENEALG_WRK *alg = &ew->alg;
    void       *temp;
    intptr_t    eact_data_addr;
    u_char     *p;

    alg_manageIsReadyAlg(ew->cmn_dat->alg_no, &temp);                   /* 2828 */
    eact_data_addr = (intptr_t)temp;                                    /* 2834 */

    alg->bcomm_add_top = eact_data_addr;                                /* 2835 */

    p = (u_char *)(eact_data_addr + ew->type * 2);            /* 2837 */
    p = (u_char *)(eact_data_addr + (p[0] + p[1] * 0x100));   /* 2838/2839 */
    p += ew->cmn_dat->alg_no * 2;                                       /* 2840 */
    p = (u_char *)(eact_data_addr + (p[0] + p[1] * 0x100));   /* 2841/2842 */

    alg->bpos_no    = 0;                                                /* 2844 */
    alg->bwait_time = 1.0f;                                             /* 2845 */
    alg->bcomm_add.wrk = eact_data_addr + (p[0] + p[1] * 0x100);        /* 2846/2847 */
}

/* ==========================================================================
 *  Visibility
 * ======================================================================== */

/* Raise status bit 0x100 when the ghost is on screen, unoccluded and inside
 * the camera's sight cone.  0x1000000 is the "hidden" override, which skips
 * the test entirely.  The screen bounds are the PAL/NTSC frame, tested against
 * the interlaced projection of the neck. */
void EneInDispChk(ENE_WRK *ew)                                          /* 2857 */
{
    MPOS *pos = &ew->mpos;
    float rot;
    float tx;
    float ty;

    rot = GetTrgtRotY(gra3dGetCamera()->matCoord[3],                    /* 2862 */
                      gra3dGetCamera()->vTarget);
    GetCamI2DPos(pos->p0, &tx, &ty);                                    /* 2863 */

    ew->st.sta &= ~0x100L;                                              /* 2864 */

    if ((ew->st.sta & 0x1000000) == 0)                                  /* 2865 */
    {
        if (OutSightChk(pos->p0, gra3dGetCamera()->matCoord[3], rot,
                        ENE_PI, 5000.0f) == 0 &&
            tx >= 0.0f && tx <= 640.0f &&
            ty >= 0.0f && ty <= 448.0f &&
            ew->ppj.result[0] != 0)
        {
            ew->st.sta |= 0x100L;                                       /* 2870 */
        }
    }

    SetEneDepth(ew, pos->p0);                                           /* 2873 */
}

/* Post one occlusion query.  The sampled point is pushed 200 units towards the
 * camera, so a ghost standing flush against a wall still reads as visible. */
void SetEneDepth(ENE_WRK *ew, float *pos)                               /* 2880 */
{
    float tv[4];
    float rv[4];

    ew->ppj.num      = 1;
    ew->ppj_check    = 1;
    ew->ppj.result[0] = 0;
    g3dxVu0CopyVector(ew->ppj.p[0], pos);

    _SetVector(tv, 0.0f, 0.0f, 200.0f, 0.0f);                           /* 2887 */
    GetTrgtRot(ew->ppj.p[0], gra3dGetCamera()->matCoord[3], rv, 3);     /* 2888 */
    RotFvector(rv, tv);
    sceVu0AddVector(ew->ppj.p[0], ew->ppj.p[0], tv);                    /* 2891 */
}                                                                       /* 2893 */

/* Resolve every query posted this frame.  Runs once, after the draw pass. */
void CheckEneDepth(void)                                                /* 2898 */
{
    int i;

    for (i = 0; i < ENE_WRK_MAX; i++)                                   /* 2903 */
    {
        ENE_WRK *ew = &ene_wrk[i];

        if (ew->ppj_check != 0)                                         /* 2904 */
        {
            CheckPointDepth(&ew->ppj);                                  /* 2905 */
            ew->ppj_check = 0;
        }
    }
}                                                                       /* 2907 */

/* ==========================================================================
 *  Movement
 * ======================================================================== */

/* Integrate the script's velocities.  The speed is per 200 animation ticks, so
 * a slowed ghost covers proportionally less ground -- movement and animation
 * cannot drift apart.
 *
 * 0x60000 is "move in world space" (the two axes are already absolute), and
 * a backwards-moving hostile ghost never gains height, which is what keeps a
 * knocked-back ghost from riding up a slope. */
static void EneMoveCtrl(ENE_WRK *ew)                                    /* 2914 */
{
    MOVE_BOX *mb = &ew->mbox;
    float     tv[4];
    u_char    i;
    u_char    back_flg;

    if ((ew->st.sta & 0x1c0000000L) != 0) { return; }                   /* 2918 */

    g3dxVu0CopyVector(tv, mb->spd);                                     /* 2921 */
    sceVu0ScaleVector(tv, tv, (float)ew->ani_reso / 200.0f);            /* 2926 */
    GetPALMode();                                                       /* 2928 */

    back_flg = (u_char)(tv[2] < 0.0f);                                  /* 2932 */

    if ((ew->st.sta & 0x60000) == 0)                                    /* 2934 */
    {
        RotFvector(mb->rot, tv);                                        /* 2935 */
    }

    if ((ew->st.sta & 0x20000000000L) != 0)                             /* 2939 */
    {
        tv[1] = 0.0f;
    }
    if (ew->type == 0 && back_flg)                                      /* 2944 */
    {
        tv[1] = 0.0f;                                                   /* 2945 */
    }

    sceVu0AddVector(mb->pos, mb->pos, tv);                              /* 2954 */
    EnePosInfoSet(ew);                                                  /* 2955 */

    sceVu0AddVector(mb->rot, mb->rot, mb->rspd);                        /* 2957 */
    for (i = 0; i < 3; i++)                                             /* 2958 */
    {
        RotLimitChk(&mb->rot[i]);
    }

    /* trot[3] non-zero means "turn towards trot[1] at this rate per frame". */
    if (mb->trot[3] != 0.0f)                                            /* 2961 */
    {
        tv[1] = mb->trot[1] - mb->rot[1];                               /* 2962 */
        RotLimitChk(&tv[1]);                                            /* 2963 */

        if (tv[1] > 0.0f)                                               /* 2964 */
        {
            if (tv[1] >= mb->trot[3])                                   /* 2965 */
            {
                mb->rot[1] += mb->trot[3];                              /* 2970 */
                RotLimitChk(&mb->rot[1]);                               /* 2971 */
                return;
            }
        }
        else
        {
            if (tv[1] <= -mb->trot[3])                                  /* 2975 */
            {
                mb->rot[1] -= mb->trot[3];                              /* 2976 */
                RotLimitChk(&mb->rot[1]);                               /* 2977 */
                return;
            }
        }

        /* Close enough -- snap to the target and stop turning. */
        mb->rot[1]  = mb->trot[1];                                      /* 2980 */
        mb->trot[3] = 0.0f;                                             /* 2981 */
    }
}                                                                       /* 2985 */

/* ==========================================================================
 *  Action selection
 * ======================================================================== */

/* Is the ghost allowed to react to the player right now?  Status bit 8 is the
 * script's own "notice the player" request, which fires immediately; otherwise
 * only the idle and walk actions may notice, and only after recog_tm frames. */
static u_char EnePRecogChkChk(ENE_WRK *ew)                              /* 2997 */
{
    u_char req = 0;                                                     /* 2998 */

    if ((ew->st.sta & 8) != 0)                                          /* 2999 */
    {
        ew->st.sta &= ~8L;                                              /* 3001 */
        req = 1;
    }
    else if (ew->act_no == 0xff || ew->act_no == 1 || ew->act_no == 2)  /* 3004 */
    {
        if (ew->recog_tm == 0)                                          /* 3005 */
        {
            ew->recog_tm = 0;                                           /* 3006 */
            req = 1;
        }
        else
        {
            ew->recog_tm--;                                             /* 3008 */
        }
    }
    return req;                                                         /* 3011 */
}

/* Action 2 is the approach.  The out-parameter is loaded twice -- once with
 * the current action, then with the chosen one -- which is the ROM's shape,
 * not a slip: the first store is what a wider version of this function would
 * have left in place had it decided not to change anything. */
static u_char EnePRecogChk(ENE_WRK *ew, u_char *act_no)                 /* 3027 */
{
    u_char new_act;

    *act_no = ew->act_no;                                               /* 3029 */
    ew->st.sta |= 0x20L;                                                /* 3032 */

    new_act = 2;                                                        /* 3035 */
    *act_no = new_act;                                                  /* 3037 */

    return (u_char)(ew->act_no != new_act);                             /* 3040 */
}                                                                       /* 3041 */

/* Commit the chosen action.  0x800000 is the script's own "restart me" flag,
 * so an action can re-enter itself without the recognition check firing. */
static void EneActIniChk(ENE_WRK *ew, u_char view_chk, u_char act_no)   /* 3053 */
{
    if (view_chk != 0 || (ew->st.sta & 0x800000) != 0)                  /* 3054 */
    {
        ew->st.sta &= ~0x800000L;
        EneActSet(ew, act_no);                                          /* 3055 */
    }
}

/* Actions that override whatever the ghost was doing, in priority order:
 * 1 the player left the ghost's territory, 2 damage taken, 3 killed.
 * Index 0 is 0xff -- "nothing to force". */
static int EneActPreferChk(ENE_WRK *ew, u_char *act_no)                 /* 3063 */
{
    u_char prefer_act[4] = { 0xff, 5, 4, 6 };                           /* 3065 */
    u_char n;
    int    ret = 0;

    if (PlyrOutAreaChk(ew) != 0)                                        /* 3068 */
    {
        ret = 1;                                                        /* 3069 */
    }
    else
    {
        n = EneDmgChk(ew);                                              /* 3073 */
        if (n == 1)                                                     /* 3074 */
        {
            ret = 2;
        }
        else if (n == 2)                                                /* 3075 */
        {
            ret = 3;
        }
    }

    *act_no = prefer_act[ret];                                          /* 3078 */
    return ret;                                                         /* 3080 */
}

/* Pick this frame's action.  0x40000000 is "the script owns the action"; while
 * it is set nothing here may interfere.
 *
 * The two transitions are symmetric with a 200-unit hysteresis band: approach
 * (2) becomes attack (3) at attack range, and attack falls back to idle (1)
 * only once the target is 200 units past that range. */
static void EneActRule(ENE_WRK *ew)                                     /* 3088 */
{
    PLCMN_WRK *pcmw = ew->target;
    u_char     act_no = 0xff;                                           /* 3090 */
    float      dist;

    if ((ew->st.sta & 0x40000000L) != 0) { return; }                    /* 3094 */

    if (EneActPreferChk(ew, &act_no) == 0)                              /* 3097 */
    {
        if ((ew->st.sta & 0x8000) != 0)                                 /* 3100 */
        {
            EneAtkCtrl(ew);                                             /* 3101 */
        }
        if ((ew->st.sta & 0x80000000L) != 0) { return; }                /* 3104 */

        dist = GetDistV2(pcmw->mbox.pos, ew->mbox.pos);                 /* 3106 */

        if (ew->act_no == 2)                                            /* 3109 */
        {
            if (dist <= ew->dat->atk_rng &&                             /* 3115 */
                (ew->st.sta & 0x801020000L) == 0)
            {
                act_no = 3;                                             /* 3116 */
            }
            ChangeAtkTarget(ew);                                        /* 3119 */
        }
        else if (ew->act_no == 3)                                       /* 3121 */
        {
            if (dist < ew->dat->atk_rng + 200.0f ||                     /* 3123 */
                (ew->st.sta & 0x800000000L) != 0)
            {
                /* Still in reach: arm the shutter chance once the target is
                 * inside the ghost's own chance range. */
                if ((ew->st.sta & 0x10000) != 0 &&                      /* 3129 */
                    dist <= ew->dat->chance_rng)
                {
                    ew->st.sta |= 0x1000L;                              /* 3133 */
                }
            }
            else
            {
                act_no = 1;                                             /* 3137 */
            }
        }
    }

    if (act_no != 0xff)                                                 /* 3143 */
    {
        EneActSet(ew, act_no);                                          /* 3144 */
    }
}                                                                       /* 3146 */

/* Has the player left this ghost's territory?  Only checked from the idle and
 * approach actions, and never while the ghost is the one the player is being
 * grabbed by (player modes 1..4 with this ghost as the attacker). */
static u_char PlyrOutAreaChk(ENE_WRK *ew)                               /* 3152 */
{
    PLCMN_WRK *pcw = ew->target;
    u_char     result = 0;                                              /* 3153 */

    if ((ew->act_no == 0xff || ew->act_no == 7 ||                       /* 3156 */
         ew->act_no == 1 || ew->act_no == 2) &&
        ((u_char)(plyr_wrk.cmn_wrk.mode - 1) > 3 ||
         pcw->atk_eneno != ew->alg.idx))
    {
        result = (u_char)(EneTrtryChk(ew, (u_char)GetPlyrAreaNo()) == 0); /* 3161/3162 */
    }
    return result;                                                      /* 3167 */
}

/* Territory check.  Compiled to an unconditional "yes" in the prototype -- the
 * per-ghost room list it would read is not in this build's data, so
 * PlyrOutAreaChk() never fires. */
static u_char EneTrtryChk(ENE_WRK *ew, u_char room_no)
{
    (void)ew; (void)room_no;
    return 1;                                                           /* 3174 */
}

/* Count the attack down.  A grab on the companion (attack type 3) does not
 * tick -- it ends when sister.c breaks it, not on a timer. */
static void EneAtkCtrl(ENE_WRK *ew)                                     /* 3185 */
{
    if (ew->atk_tm == 0)                                                /* 3186 */
    {
        ew->atk_type = 0;                                               /* 3188 */
        ew->st.sta &= ~0x40000008000L;                                  /* 3189 */
    }
    else if (ew->target_n == 1 && ew->atk_type == 3)                    /* 3193 */
    {
        ew->dist_in_tm[0] = 0;                                          /* 3194 */
        ew->dist_in_tm[1] = 0;
        return;
    }
    else
    {
        ew->atk_tm--;                                                   /* 3197 */
    }

    ew->dist_in_tm[0] = 0;
    ew->dist_in_tm[1] = 0;
}                                                                       /* 3198 */

/* Apply pending damage.  Returns 1 for "hurt", 2 for "killed".  The kill path
 * also clears the target's condition timer and zeroes the blink script's wait,
 * so the death animation starts on the same frame. */
static u_char EneDmgChk(ENE_WRK *ew)                                    /* 3209 */
{
    STATUS_DAT *ews  = &ew->st;                                         /* 3210 */
    PLCMN_WRK  *pcw  = ew->target;                                      /* 3211 */
    STATUS_DAT *pcsw = &pcw->st;
    u_char      result = 0;                                             /* 3212 */

    if ((ews->sta & 0x100000) != 0)                                     /* 3215 */
    {
        ClearEneStaDmg(ew);                                             /* 3218 */

        if (ews->dmg < ews->hp)                                         /* 3221 */
        {
            ews->hp -= ews->dmg;                                        /* 3222 */
            result = 1;                                                 /* 3223 */
        }
        else
        {
            ews->hp = 0;                                                /* 3226 */
            pcsw->cond_tm = 0;                                          /* 3230 */
            ew->rel_type = ENE_RELEASE_DEAD;                            /* 3231 */
            ew->alg.bwait_time = 0.0f;                                  /* 3234 */
            result = 2;                                                 /* 3235 */
        }

        ews->dmg_old = ews->dmg;                                        /* 3237 */
        ews->dmg     = 0;                                               /* 3238 */
        ews->sta = (ews->sta & ~0x100000L) | 0x200000L;
    }
    return result;                                                      /* 3240 */
}

/* Reset the interpreter stack and the attack bookkeeping around a hit. */
static void ClearEneStaDmg(ENE_WRK *ew)                                 /* 3250 */
{
    if ((ew->st.sta & 0x100000) != 0)                                   /* 3251 */
    {
        ew->st.sta &= ~0x20000L;                                        /* 3253 */
    }

    ew->alg.stack_p   = ew->alg.stack_b;                                /* 3255 */
    ew->atk_tm        = 0;                                              /* 3256 */
    ew->dist_in_tm[0] = 0;                                              /* 3257 */
    ew->dist_in_tm[1] = 0;
    ew->st.sta &= ~0x180000000L;
}                                                                       /* 3262 */

/* The ghost's animation rate.  Two independent factors: `reso`, the scripted
 * slow-motion, and `wlk_reso`, a random walk-speed jitter that keeps a group
 * of the same ghost from moving in lockstep.  attr 0x200 opts a ghost in, and
 * only while it is walking (anime_no 2). */
static void EneAniResolutionCtrl(ENE_WRK *ew)                           /* 3287 */
{
    if ((ew->attr & 0x200) == 0 || ew->anime_no != 2)                   /* 3288 */
    {
        ew->wlk_reso = 1.0f;                                            /* 3289 */
    }
    else if (ew->wlk_reso_wait != 0)                                    /* 3290 */
    {
        ew->wlk_reso_wait--;
    }
    else if (ew->wlk_reso_frm == 0)                                     /* 3292 */
    {
        /* Pick a new target rate and how many frames to reach it over. */
        ew->wlk_reso_frm = (u_char)GetRndSP(5, 10);                     /* 3293 */
        ew->wlk_reso_chg = ((float)GetRndSP(0, 120) / 100.0f - ew->wlk_reso) /
                           (float)ew->wlk_reso_frm;                     /* 3294/3295 */
    }
    else
    {
        float f = ew->wlk_reso + ew->wlk_reso_chg;                      /* 3297 */

        if (f > 1.0f)              { f = 1.0f; }                        /* 3298 */
        else if (f < 0.19999999f)  { f = 0.19999999f; }                 /* 3299 */
        ew->wlk_reso = f;

        ew->wlk_reso_frm--;                                             /* 3300 */
        if (ew->wlk_reso_frm == 0)                                      /* 3301 */
        {
            ew->wlk_reso_wait = (u_char)GetRndSP(20, 40);
        }
    }

    if (ew->reso_tm > 0)                                                /* 3309 */
    {
        ew->reso_tm--;
    }
    else if (ew->reso_tm == 0)                                          /* 3313 */
    {
        ew->reso = 1.0f;                                                /* 3314 */
    }

    ew->ani_reso = (int)((float)motGetMotReso() * ew->reso * ew->wlk_reso); /* 3316 */
}                                                                       /* 3318 */

/* Tint the ghost by its condition, and post the "condition ended" particle as
 * each countdown reaches zero.  A sealed ghost (status bit 1) keeps whatever
 * EneSealCtrl() set. */
static void EneCondCtrl(ENE_WRK *ew)                                    /* 3333 */
{
    if (ew->stm_slow != 0)                                              /* 3334 */
    {
        ew->stm_slow--;                                                 /* 3335 */
        if (ew->stm_slow == 0)
        {
            IgEffectEffectEndParticleReq(ew->mpos.p1, 0);               /* 3337 */
        }
    }
    if (ew->stm_view != 0)                                              /* 3341 */
    {
        ew->stm_view--;                                                 /* 3342 */
        if (ew->stm_view == 0)
        {
            IgEffectEffectEndParticleReq(ew->mpos.p1, 2);               /* 3343 */
        }
    }

    if ((ew->st.sta & 2) != 0) { return; }                              /* 3345 */

    if (ew->stm_slow != 0)                                              /* 3350 */
    {
        SetEnemyParallelLight(ew, 1.0f, 0.59999996f, 1.0f, 1.0f);       /* 3351 */
    }
    else if (ew->stm_view != 0)                                         /* 3352 */
    {
        SetEnemyParallelLight(ew, 0.59999996f, 1.0f, 1.0f, 1.0f);       /* 3353 */
    }
    else if (ew->st.hp != 0)                                            /* 3354 */
    {
        SetEnemyParallelLight(ew, (float)ew->cmn_dat->blg_r / 10.0f,    /* 3357 */
                                  (float)ew->cmn_dat->blg_g / 10.0f,
                                  (float)ew->cmn_dat->blg_b / 10.0f, 1.0f);
    }
}                                                                       /* 3358 */

/* ==========================================================================
 *  Targeting
 * ======================================================================== */

/* pl 1 selects the companion, anything else the player.  With no companion in
 * the party there is only one answer. */
static void SetAtkTarget(ENE_WRK *ew, int pl)                           /* 3373 */
{
    if (IsSisWrk() != 0 && pl == 1)                                     /* 3374/3375 */
    {
        ew->target   = &sis_wrk.cmn_wrk;
        ew->target_n = 1;
    }
    else
    {
        ew->target   = &plyr_wrk.cmn_wrk;                               /* 3377 */
        ew->target_n = 0;
    }
}                                                                       /* 3378 */

/* Re-roll the target, weighted by the ghost's own trgt_chg percentage. */
void ChangeAtkTargetRnd(ENE_WRK *ew)                                    /* 3383 */
{
    int per = ew->dat->trgt_chg;                                        /* 3384 */

    if (IsSisWrk() != 0 && GetRndSP(1, 99) <= 100 - per)                /* 3386/3387 */
    {
        ew->target   = &sis_wrk.cmn_wrk;                                /* 3388 */
        ew->target_n = 1;
    }
    else
    {
        ew->target   = &plyr_wrk.cmn_wrk;                               /* 3390 */
        ew->target_n = 0;
    }
}                                                                       /* 3391 */

/* Switch targets when the other unit has been in reach for 90 frames, or when
 * the current one is walking away while the other is closing in.
 * 0x40000000000 is the "target locked" bit an attack script raises. */
static void ChangeAtkTarget(ENE_WRK *ew)                                /* 3398 */
{
    if (IsSisWrk() == 0) { return; }                                    /* 3403 */

    if ((ew->st.sta & 0x40000000000L) != 0)                             /* 3405 */
    {
        ew->dist_in_tm[0] = 0;                                          /* 3406 */
        ew->dist_in_tm[1] = 0;
        return;
    }

    if (ew->dist_p_e[1] <= ew->dat->atk_rng)                            /* 3410 */
    {
        if (ew->dist_in_tm[1]++ > 90)                                   /* 3411 */
        {
            ew->dist_in_tm[0] = 0;                                      /* 3412 */
            ew->dist_in_tm[1] = 0;
            if (ew->target == pl_sta[0])                                /* 3415 */
            {
                SetAtkTarget(ew, 1);                                    /* 3416 */
            }
        }
    }
    else
    {
        ew->dist_in_tm[1] = 0;
    }

    if (ew->dist_p_e[0] <= ew->dat->atk_rng)                            /* 3422 */
    {
        if (ew->dist_in_tm[0]++ > 90)                                   /* 3423 */
        {
            ew->dist_in_tm[0] = 0;                                      /* 3424 */
            ew->dist_in_tm[1] = 0;
            if (ew->target == pl_sta[1])                                /* 3427 */
            {
                SetAtkTarget(ew, 0);                                    /* 3428 */
            }
        }
    }
    else
    {
        ew->dist_in_tm[0] = 0;
    }

    /* Chasing the player while the companion just came into range, or the
     * other way round: re-roll rather than switch outright. */
    if (ew->target_n == 0 &&                                            /* 3436 */
        ew->dist_p_e[1] <= ew->dat->atk_rng &&
        ew->dist_p_e_o[1] > ew->dat->atk_rng)
    {
        ChangeAtkTargetRnd(ew);                                         /* 3437 */
    }
    else if (ew->target_n == 1 &&                                       /* 3439 */
             ew->dist_p_e[0] <= ew->dat->atk_rng &&
             ew->dist_p_e_o[0] > ew->dat->atk_rng)
    {
        ChangeAtkTargetRnd(ew);                                         /* 3440 */
    }
}                                                                       /* 3442 */

/* The camera-facing anchor the aura and haze effects hang off: the waist,
 * pulled 400 units towards the camera so the billboard clears the model. */
static void EneBlinkPosSet(ENE_WRK *ew)                                 /* 3449 */
{
    float tv[4];
    float tr[4];

    memset(tv, 0, sizeof(tv));                                          /* 3450 */
    tv[2] = 400.0f;

    GetTrgtRot(ew->mpos.p1, gra3dGetCamera()->matCoord[3], tr, 3);      /* 3454 */
    RotFvector(tr, tv);                                                 /* 3455 */
    sceVu0AddVector(ew->bep, ew->mpos.p1, tv);                          /* 3456 */
}

/* Empty in the prototype. */
static void EneLightCtrl(ENE_WRK *ew)
{
    (void)ew;
}                                                                       /* 3475 */

/* Swap the running algorithm branch.  0xff instead forces the ghost onto
 * action 8, the scripted-stop pose. */
int ChangeEneAlgorithm(int ene_type, int dat_no, int algo_no)           /* 3486 */
{
    int i = SearchEneWrkNo(ene_type, dat_no);                           /* 3489 */

    if (i < 0)
    {
        PRINT_WARNING("EneChangeAlg type %d no %d Is Not Worked",
                      ene_type, dat_no);
        return 1;
    }

    if (ene_wrk[i].status != ENE_STATUS_ACT)                            /* 3494 */
    {
        return 0;
    }

    if (algo_no == 0xff)                                                /* 3497 */
    {
        EneActSet(&ene_wrk[i], 8);                                      /* 3498 */
    }
    else
    {
        ene_wrk[i].alg.branch = (u_char)algo_no;                        /* 3501 */
    }
    return 1;                                                           /* 3502 */
}

/* ==========================================================================
 *  Transparency
 * ======================================================================== */

/* The ghost's final alpha, as the product of four independent terms:
 *
 *   fw           distance fade (attr 0x1) -- in over the first tenth of the
 *                near..far span, out over the last tenth
 *   tr_rate_alg  what the action script asked for
 *   tr2_rate_alg what the aura asked for
 *   tr_rate_in / tr_rate_out   the finder mode's own term
 *   tr_common    the global fade-in after a scene change
 *
 * A highlighted ghost (stm_view) is floored at half opacity so it can always
 * be found. */
void EneAlphaCtrl(ENE_WRK *ew)                                          /* 3508 */
{
    float l = GetDistV(plyr_wrk.cmn_wrk.mbox.pos, ew->mbox.pos);        /* 3510 */
    float fw = 1.0f;
    float now;
    float tr;

    if ((ew->attr & 1) != 0)                                            /* 3513 */
    {
        float near1 = ew->cmn_dat->near;
        float far1  = ew->cmn_dat->far;
        float band  = (far1 - near1 < 1000.0f) ? (far1 - near1) * 0.099999994f
                                               : 100.0f;
        float near2 = near1 + band;                                     /* 3529 */
        float far2  = far1 - band;                                      /* 3530 */

        if (l < near1)                                                  /* 3531 */
        {
            fw = 0.0f;
        }
        else if (l < near2)                                             /* 3533 */
        {
            float d = near2 - near1;                                    /* 3534 */
            fw = (d > 0.0f) ? (l - near1) / d : 1.0f;                   /* 3535/3536 */
        }
        else if (l < far2)                                              /* 3538 */
        {
            fw = 1.0f;                                                  /* 3539 */
        }
        else if (l < far1)                                              /* 3542 */
        {
            float d = far1 - far2;                                      /* 3544 */
            fw = (d > 0.0f) ? (far1 - l) / d : 1.0f;                    /* 3545/3546 */
        }
        else
        {
            fw = 0.0f;                                                  /* 3547 */
        }
    }

    /* A ghost mid-grab on its target is always fully opaque. */
    if (ew->target->mode == 3 && ew->target->atk_eneno == ew->alg.idx)  /* 3548/3549 */
    {
        ew->tr_rate_alg    = 0x80;                                      /* 3551 */
        ew->tr_rate_alg_sp = 0x80;
    }

    /* Player mode 8 is the scene fade-out; attr 0x80000 survives it. */
    if (plyr_wrk.cmn_wrk.mode == 8 && (ew->attr & 0x80000) == 0)        /* 3557 */
    {
        ew->tr_common = 0.0f;                                           /* 3558 */
    }
    else if (ew->tr_common < 1.0f)                                      /* 3559 */
    {
        ew->tr_common += 0.079999998f;                                  /* 3563 */
    }
    else
    {
        ew->tr_common = 1.0f;                                           /* 3564 */
    }

    /* Player mode 6 is finder mode. */
    now = (float)((u_int)ew->tr_rate_alg *                              /* 3567 */
                  (u_int)ew->tr2_rate_alg *
                  (u_int)((plyr_wrk.cmn_wrk.mode == 6) ? ew->tr_rate_in
                                                       : ew->tr_rate_out)) /
          (128.0f * 128.0f * 128.0f) * ew->tr_common * fw * 128.0f;     /* 3574 */

    ew->tr_rate = (u_char)(u_int)now;                                   /* 3575 */
    tr = (float)ew->tr_rate;

    ew->tr_frate = tr * (1.0f / 128.0f) * ew->effw;                     /* 3577 */

    /* attr 0x400 keeps the highlight floor even at zero alpha. */
    if (ew->stm_view != 0 && ((ew->attr & 0x400) != 0 || ew->tr_rate != 0)) /* 3579 */
    {
        ew->tr_rate = (u_char)(ew->tr_rate / 2 + 64);                   /* 3582 */
    }
}                                                                       /* 3587 */

/* Drop the global fade so the next one starts from nothing. */
void EneAlphaClear(void)                                                /* 3593 */
{
    int i;

    for (i = 0; i < ENE_WRK_MAX; i++)                                   /* 3597 */
    {
        ENE_WRK *ew = &ene_wrk[i];

        if (ew->status == ENE_STATUS_ACT)                               /* 3598 */
        {
            ew->tr_common = 0.0f;                                       /* 3601 */
        }
    }
}

/* The aura's own drive.  efcnt[12] is the all-screen effect slot: while it is
 * live the aura is forced off so it cannot fight with it.  A ghost mid-attack
 * on the player -- but not on the companion -- also suppresses it. */
static void EneAuraCtrl(ENE_WRK *ew)                                    /* 3607 */
{
    if (efcnt[12].dat.uc8[0] != 0)                                      /* 3612 */
    {
        ew->d_pdc  = 0.0f;                                              /* 3613 */
        ew->d_pdc2 = 0.0f;
    }
    else if ((ew->st.sta & 0x8000) != 0 && ew->target_n != 1)           /* 3615 */
    {
        ew->d_pdc  = 0.0f;                                              /* 3616 */
        ew->d_pdc2 = 0.0f;
    }
    else
    {
        ew->d_pdc  = 1.0f;                                              /* 3618 */
        ew->d_pdc2 = 1.0f;                                              /* 3619 */
    }

    ew->d_pd  = ew->d_pda  * ew->d_pdc  * ew->tr_common;                /* 3627 */
    ew->d_pd2 = ew->d_pda2 * ew->d_pdc2 * ew->tr_common;

    if (efcnt[12].dat.uc8[0] == 0 && ew->st.hp != 0)                    /* 3632 */
    {
        float f = ew->d_pd * (1.0f / 128.0f);

        ew->nee_rate = f;

        if (ew->tr_rate_alg != 0)                                       /* 3634 */
        {
            ew->nee_rate = (f + 0.079999998f >= 1.0f) ? 1.0f
                                                      : f + 0.079999998f; /* 3635 */
        }
        else
        {
            ew->nee_rate = (f > 0.024999999f) ? f - 0.024999999f : 0.0f; /* 3638 */
        }
    }
}

/* ==========================================================================
 *  Scripted states
 * ======================================================================== */

/* Highlight the ghost for `time` frames.  bWithSearcher also puts the
 * off-screen arrow on it. */
void SetEneView(ENE_WRK *ew, int time, int bWithSearcher)               /* 3646 */
{
    ew->stm_view      = (short)time;
    ew->bWithSearcher = (u_char)(bWithSearcher != 0);                   /* 3647 */
}

int SetEneViewClear(ENE_WRK *ew)                                        /* 3651 */
{
    if (ew->stm_view != 0)                                              /* 3652 */
    {
        ew->stm_view = 0;
        return 1;
    }
    return 0;                                                           /* 3658 */
}

/* Slow the ghost's animation to `reso` for `time` frames. */
void SetEneSlow(ENE_WRK *ew, int time, float reso)                      /* 3663 */
{
    ew->reso_tm = (short)time;
    ew->reso    = reso;
}                                                                       /* 3664 */

/* The same, but as a visible condition -- it tints the ghost and posts the
 * end-of-condition particle. */
void SetEneSlowMode(ENE_WRK *ew, int time, float reso)                  /* 3670 */
{
    SetEneSlow(ew, time, reso);
    ew->stm_slow = (short)time;
}                                                                       /* 3671 */

int SetEneSlowClear(ENE_WRK *ew)                                        /* 3677 */
{
    int ret;

    ew->slow_hb_wait_frame = -1;                                        /* 3680 */
    ret = (ew->stm_slow != 0);                                          /* 3687 */
    ew->stm_slow = 0;                                                   /* 3688 */
    SetEneSlow(ew, 0, 1.0f);                                            /* 3689 */
    return ret;                                                         /* 3690 */
}

/* Arm a delayed slow: `wait_frame` frames from now, slow for
 * `hit_back_frame`.  That is the hit-stop on a connected shot -- the ghost
 * keeps moving for a moment and then stalls. */
void SetEneSlowHitBack(ENE_WRK *ew, int wait_frame, int hit_back_frame, float reso)
{                                                                       /* 3699 */
    ew->slow_hb_reso       = reso;                                      /* 3700 */
    ew->slow_hb_wait_frame = (char)wait_frame;
    ew->slow_hb_frame      = (char)hit_back_frame;
}                                                                       /* 3701 */

static void EneSlowHitBackCtrl(ENE_WRK *ew)                             /* 3706 */
{
    if (ew->slow_hb_wait_frame >= 0)                                    /* 3707 */
    {
        if (ew->slow_hb_wait_frame == 0)                                /* 3708 */
        {
            SetEneSlow(ew, ew->slow_hb_frame, ew->slow_hb_reso);
        }
        ew->slow_hb_wait_frame--;                                       /* 3710 */
    }
}

/* Paralysis: `all_time` frames of alternating `one_stop_time` frozen and
 * `one_act_time` moving. */
void SetEneMahiMode(ENE_WRK *ew, int all_time, int one_stop_time, int one_act_time)
{                                                                       /* 3720 */
    ew->mahi_total_time    = (short)(all_time + 1);                     /* 3721 */
    ew->mahi_one_stop_time = (short)one_stop_time;                      /* 3722 */
    ew->mahi_one_act_time  = (short)one_act_time;
    ew->mahi_cnt           = 0;                                         /* 3723 */
}

int SetEneMahiClear(ENE_WRK *ew)                                        /* 3729 */
{
    int ret = (ew->mahi_total_time != 0);                               /* 3736 */

    ew->mahi_total_time = 0;                                            /* 3737 */
    ew->st.sta &= ~2L;
    return ret;                                                         /* 3739 */
}

/* Runs the paralysis alternation and holds the magenta tint. */
static void EneMahiCtrl(ENE_WRK *ew)                                    /* 3743 */
{
    if (ew->mahi_total_time == 0)                                       /* 3744 */
    {
        ew->st.sta &= ~2L;                                              /* 3765 */
        return;
    }

    ew->mahi_total_time--;                                              /* 3745 */
    if (ew->mahi_total_time == 0)
    {
        IgEffectEffectEndParticleReq(ew->mpos.p1, 1);                   /* 3747 */
    }

    if (ew->mahi_cnt == 0)                                              /* 3750 */
    {
        if ((ew->st.sta & 2) == 0)                                      /* 3752 */
        {
            ew->st.sta  |= 2L;                                          /* 3753 */
            ew->mahi_cnt = ew->mahi_one_stop_time;                      /* 3754 */
        }
        else
        {
            ew->st.sta  &= ~2L;                                         /* 3758 */
            ew->mahi_cnt = ew->mahi_one_act_time;                       /* 3759 */
        }
    }
    ew->mahi_cnt--;                                                     /* 3762 */

    SetEnemyParallelLight(ew, 0.59999996f, 1.0f, 0.59999996f, 1.0f);    /* 3763 */
}                                                                       /* 3767 */

/* The seal is paralysis with no act window at all. */
void SetEneSealMode(ENE_WRK *ew, int iEffFrame)                         /* 3775 */
{
    SetEneMahiMode(ew, iEffFrame, iEffFrame, 0);
}                                                                       /* 3777 */

int SetEneSealClear(ENE_WRK *ew)
{
    return SetEneMahiClear(ew);                                         /* 3787 */
}

/* Held on status bit 0, outside the ordinary rule: the ghost is frozen solid
 * with its animation rate at zero until the counter underflows. */
static void EneSealCtrl(ENE_WRK *ew)                                    /* 3804 */
{
    ew->mahi_cnt--;                                                     /* 3805 */
    if (ew->mahi_cnt == -1)                                             /* 3806 */
    {
        ew->st.sta &= ~1L;
        IgEffectEffectEndParticleReq(ew->mpos.p1, 4);                   /* 3808 */
    }

    ew->ani_reso = 0;                                                   /* 3810 */
    SetEnemyParallelLight(ew, 0.59999996f, 0.69999999f, 0.59999996f, 1.0f); /* 3811 */
}

/* ==========================================================================
 *  Misc queries
 * ======================================================================== */

/* World position of the ghost matching (obj_type, obj_id).  The banner is
 * printed but not asserted, so a stale pair is survivable -- the caller just
 * gets 0 back and leaves pos alone. */
int GetEnePos(float *pos, u_char obj_type, int obj_id)                  /* 3824 */
{
    int res = 1;
    int ene_wrk_id;

    ene_wrk_id = SearchEneWrkNo(obj_type, obj_id);                      /* 3830 */

    if (ene_wrk_id == -1)                                               /* 3833 */
    {
        printf("*******************************************************\n"); /* 3838 */
        printf("*     Error!! The value of the data is illegal!!!     *\n");  /* 3839 */
        printf("*                    GetEnePos()                      *\n");  /* 3840 */
        printf("*******************************************************\n"); /* 3841 */
        res = 0;
    }
    else
    {
        g3dxVu0CopyVector(pos, ene_wrk[ene_wrk_id].mbox.pos);
    }

    return res;                                                         /* 3848 */
}

/* Refresh the ghost's room number, and drop it onto the floor if attr 0x8 says
 * it walks rather than floats. */
void EnePosInfoSet(ENE_WRK *ew)                                         /* 3861 */
{
    ew->room_no = (u_char)MhCtlGetRoomNo(plyr_wrk.cmn_wrk.floor,        /* 3862 */
                                         ew->mbox.pos);

    if (ew->room_no != 0xff && (ew->attr & 8) != 0)                     /* 3864 */
    {
        MhCtlGetMapHeight(ew->mbox.pos, ew->mbox.pos, ew->room_no, 1);  /* 3866 */
    }
}                                                                       /* 3867 */

/* HP regeneration (attr 0x10).  The recovery only starts after hp_recv_wait
 * frames without damage, and never restores more than 5 points a frame. */
static void EneHPRecv(ENE_WRK *ew)                                      /* 3882 */
{
    float recv;

    if ((ew->attr & 0x10) == 0) { return; }                             /* 3883 */
    if (ew->st.hp == 0)         { return; }                             /* 3888 */

    /* 0x20000000000 is the "no regeneration" script bit. */
    if ((ew->st.sta & 0x20000000000L) != 0 ||                           /* 3890 */
        ew->st.hp >= ew->dat->hp)
    {
        ew->hp_recv_tm = 0;                                             /* 3893 */
        ew->hp_recv_pt = 0.0f;                                          /* 3894 */
    }
    else
    {
        ew->hp_recv_tm++;
    }

    if (ew->hp_recv_tm >= ew->dat->hp_recv_wait)                        /* 3898 */
    {
        ew->hp_recv_pt += ew->dat->hp_recv_vol;                         /* 3899 */
    }

    recv = ew->hp_recv_pt;
    if (recv > 0.0f)                                                    /* 3903 */
    {
        float step = (recv > 5.0f) ? 5.0f : (float)(int)recv;           /* 3904 */
        int   hp   = ew->st.hp + (int)step;                             /* 3906 */

        ew->hp_recv_pt = recv - step;                                   /* 3905 */
        if (hp > ew->dat->hp) { hp = ew->dat->hp; }
        ew->st.hp = (u_short)hp;
    }
}                                                                       /* 3913 */

/* Is the player inside the ghost's view cone?  Built as a triangle in world
 * space -- apex at the ghost, base `dist` ahead and `rot` degrees wide -- and
 * tested by which side of each edge the player falls on.  A cone of 90 degrees
 * or more has no finite base, so it answers "no". */
int CheckEneView(ENE_WRK *ew, float dist, float rot)                    /* 3918 */
{
    float   wlm[4][4];
    float   wp[3][4];
    float   mp[3][4];
    POINT_T p;
    LINE_T  l;
    float   f;
    int     a1;
    int     a2;
    int     a3;
    int     i;

    memset(mp, 0, sizeof(mp));                                          /* 3923 */
    mp[0][3] = 1.0f;
    mp[1][3] = 1.0f;
    mp[2][3] = 1.0f;

    if (rot >= 90.0f) { return 0; }                                     /* 3931 */

    f = tanf(rot * ENE_PI / 180.0f);                                        /* 3932 */

    mp[2][0] = f * dist;                                                /* 3935 */
    mp[1][0] = -mp[2][0];                                               /* 3936 */
    mp[1][2] = dist;                                                    /* 3937 */
    mp[2][2] = dist;                                                    /* 3938 */

    sceVu0UnitMatrix(wlm);                                              /* 3939 */
    sceVu0RotMatrixY(wlm, wlm, ew->mbox.rot[1]);                        /* 3941 */
    sceVu0TransMatrix(wlm, wlm, ew->mbox.pos);                          /* 3942 */

    for (i = 0; i < 3; i++)                                             /* 3943 */
    {
        sceVu0ApplyMatrix(wp[i], wlm, mp[i]);                           /* 3945 */
    }

    /* Flattened to XZ -- the cone has no vertical extent. */
    p.x = plyr_wrk.cmn_wrk.mbox.pos[0];                                 /* 3946 */
    p.y = plyr_wrk.cmn_wrk.mbox.pos[2];                                 /* 3947 */

    l.a.x = wp[0][0];                                                   /* 3949 */
    l.a.y = wp[0][2];                                                   /* 3950 */
    l.b.x = wp[1][0];                                                   /* 3951 */
    l.b.y = wp[1][2];                                                   /* 3952 */
    a1 = LineSide(&p, &l);                                              /* 3953 */

    l.a.x = wp[1][0];                                                   /* 3954 */
    l.a.y = wp[1][2];                                                   /* 3955 */
    l.b.x = wp[2][0];                                                   /* 3956 */
    l.b.y = wp[2][2];                                                   /* 3957 */
    a2 = LineSide(&p, &l);                                              /* 3958 */

    l.a.x = wp[2][0];                                                   /* 3959 */
    l.a.y = wp[2][2];                                                   /* 3960 */
    l.b.x = wp[0][0];                                                   /* 3961 */
    l.b.y = wp[0][2];                                                   /* 3962 */
    a3 = LineSide(&p, &l);                                              /* 3963 */

    if (a1 == -1 && a2 == -1 && a3 == -1)                               /* 3964 */
    {
        return 1;                                                       /* 3965 */
    }
    return 0;                                                           /* 3967 */
}                                                                       /* 3968 */

/* Two ghosts shake the room while they are on the field.  The tremor repeats
 * on a randomised cycle; once neither is present it is stopped, but only if
 * nothing else has a quake request outstanding. */
static void CtrlEarthquake(void)                                        /* 3972 */
{
    int flg = 0;                                                        /* 3973 */
    int i;

    for (i = 0; i < ENE_WRK_MAX; i++)                                   /* 3976 */
    {
        ENE_WRK *ew = &ene_wrk[i];

        if (ew->status == ENE_STATUS_ACT &&                             /* 3977 */
            (ew->dat_no == 0x45 || ew->dat_no == 0x20))
        {
            flg = 1;                                                    /* 3978 */
        }
    }                                                                   /* 3980 */

    if (flg)                                                            /* 3983 */
    {
        if (earth_quake_cnt == 0)                                       /* 3984 */
        {
            QuakeCameraReq(0.0099999998f, 0xb4, 1);                     /* 3985 */
            earth_quake_cnt = GetRndSP(300, 300) + 179;                 /* 3986 */
        }
        else
        {
            earth_quake_cnt--;                                          /* 3987 */
        }
    }
    else if (QuakeCameraGetReq() == 0)                                  /* 3989 */
    {
        QuakeCameraStop();                                              /* 3991 */
    }
}                                                                       /* 3992 */

/* ==========================================================================
 *  Per-model morph quirks
 * ======================================================================== */

/* Animation 0x49 is Itsuki's; he gets his own blink handling. */
static int EneSpeMimeCtrl(ANI_CTRL *ani_ctrl)                           /* 3999 */
{
    if (ani_ctrl->anm_no == 0x49)                                       /* 4000 */
    {
        EneItukiMepatiCtrl(ani_ctrl);                                   /* 4002 */
        return 1;                                                       /* 4009 */
    }
    return 0;                                                           /* 4010 */
}

/* Itsuki's blink.  A countdown of up to 200 frames between blinks; which morph
 * is requested depends on whether the clip is playing (3) or paused (1).
 *
 * The counter is a function-local static in the ROM -- Ghidra shows it as
 * no_1492 with a __tmp_10_1493 guard, which is GCC's lazy initialisation of a
 * local static with a non-constant initialiser. */
static int EneItukiMepatiCtrl(ANI_CTRL *ani_ctrl)                       /* 4015 */
{
    static int no = GetRandValI(200);                                   /* 4016 */
    int        num;

    if (ani_ctrl->anm.playnum == 1)                                     /* 4018 */
    {
        if (no != 0) { no--; return 1; }                                /* 4019/4020 */
        num = 3;                                                        /* 4021 */
    }
    else
    {
        if (mimIsUseParts(ani_ctrl, 1) != 0) { return 1; }              /* 4026 */
        if (no != 0) { no--; return 1; }                                /* 4027/4028 */
        num = 1;                                                        /* 4029 */
    }

    no = -1;
    mimRequestNum(ani_ctrl, num, 0);
    no = GetRandValI(200);
    return 1;                                                           /* 4033 */
}
