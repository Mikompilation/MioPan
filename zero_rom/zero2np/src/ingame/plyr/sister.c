/* ==========================================================================
 *  ingame/plyr/sister.c
 *
 *  The companion ("sister", Mayu) game module: her state machine, damage
 *  handling, path following, and the trace-point search that recovers her
 *  when she loses sight of the player.
 *
 *  She does not path-find towards the player directly.  SisterTracePlayer()
 *  records his position into sis_trace, a 64-entry ring of waypoints;
 *  SisterTraceMove() walks her along that trail at her current speed,
 *  short-cutting wherever the recorded route doubles back on itself.  When
 *  the trail overflows or the geometry blocks it, SearchMain() falls back on
 *  the per-room routing graph in sis_trpoint.c.
 *
 *  ---- SIS_WRK::cmn_wrk.st.mvsta bits -------------------------------------
 *  Recovered from SetSisterStatus(), SisterNAnimeCtrl() and MoveSisStairs():
 *
 *    0x0000001 walk               0x0000002 run
 *    0x0000008 hurry (post-stumble recovery run)
 *    0x0000010 / 0x0000020        stairs up / down, running
 *    0x0000040 / 0x0000080        stairs up / down, walking
 *    0x0000100..0x0000800        pushed aside, direction 0..3
 *    0x0001000                    one-shot: clear the movement bits
 *    0x0002000                    following the recorded trail
 *    0x0004000                    arrived -- stop
 *    0x0010000                    waiting (stood still long enough)
 *    0x0020000 damaged            0x0080000 search failed
 *    0x0100000                    lost the player, cowering
 *    0x0200000 stumbling          0x0400000 fallen
 *    0x0800000 turning in place   0x2000000 caught up
 *    0x4000000 dead
 *
 *  0x61a8000 recurs as "she is in a state that blocks ordinary movement";
 *  0x801fff is "any movement bit at all, plus turning".
 *
 *  Reconstructed from the Feb 6 2004 prototype (SLES_523.84), sister.o
 *  0x0025bc98..0x0026282c.
 * ======================================================================== */

#include "sister.h"

#include <float.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "player.h"
#include "plyr_mdl.h"
#include "sis_algo.h"
#include "sis_mdl.h"
#include "sis_trpoint.h"
#include "unit_ctl.h"
#include "../../common/utility.h"
#include "../../common/variable.h"
#include "../../graphics/draw_env.h"
#include "../../graphics/graphics.h"
#include "../../graphics/graph2d/fade.h"
#include "../../graphics/graph2d/g2d_draw.h"
#include "../../graphics/graph3d/gra3d.h"
#include "../../graphics/motion/motion.h"
#include "../../graphics/obj_draw_ctrl.h"
#include "../camera/map_camera.h"
#include "../enemy/enemy.h"
#include "../event/prg/ev_open.h"
#include "../ingame.h"
#include "../map/MhCtl.h"
#include "../map/map_hit_check.h"
#include "../map/map_rectangle.h"
#include "../subtitle/subtitle.h"
#include "../../system/os/system.h"
#include "../../miopan/miopan_memory.h"                 // MioPan_GetPs2Address
#include "../../graphics/graph3d/g3dxVu0.h"   /* g3dxVu0CopyVector */

/* Animation scripts.  Each entry is {animation, interpolation frames, loop
 * count}; loop -1 means "until interrupted" and animation 0xff terminates. */
static SIS_ANI_TBL sis_ani001_tbl[3] = { { 31, 10,  1 }, { 32, 10, -1 }, { 255, 0, 0 } };
static SIS_ANI_TBL sis_ani002_tbl[3] = { { 33, 10,  1 }, {  0, 10,  1 }, { 255, 0, 0 } };
static SIS_ANI_TBL sis_ani003_tbl[2] = { {  0, 10, -1 }, { 255, 0, 0 } };
static SIS_ANI_TBL sis_ani004_tbl[2] = { {  0, 10,  1 }, { 255, 0, 0 } };
static SIS_ANI_TBL sis_ani005_tbl[4] = { { 45, 30,  1 }, {  4, 10,  1 },
                                         {  0, 10, -1 }, { 255, 0, 0 } };
static SIS_ANI_TBL sis_ani006_tbl[2] = { {  0, 10,  1 }, { 255, 0, 0 } };
static SIS_ANI_TBL sis_ani007_tbl[2] = { {  3, 10, -1 }, { 255, 0, 0 } };
static SIS_ANI_TBL sis_ani008_tbl[2] = { {  4, 10,  1 }, { 255, 0, 0 } };
static SIS_ANI_TBL sis_ani998_tbl[2] = { { 254, 10, 1 }, { 255, 0, 0 } };
static SIS_ANI_TBL sis_ani999_tbl[2] = { {  0, 10,  1 }, { 255, 0, 0 } };

#define SIS_ANI_TBL_MAX 10

static SIS_ANI_TBL *sis_ani_tbl_data[SIS_ANI_TBL_MAX] =
{
    sis_ani999_tbl, sis_ani001_tbl, sis_ani002_tbl, sis_ani003_tbl,
    sis_ani004_tbl, sis_ani005_tbl, sis_ani006_tbl, sis_ani007_tbl,
    sis_ani008_tbl, sis_ani998_tbl
};

/* The ROM binds this in __static_initialization_and_destruction_0, which is
 * why it is a reference_fixed_array rather than a plain pointer array. */
static reference_fixed_array<SIS_ANI_TBL *, SIS_ANI_TBL_MAX> sis_ani_tbl(sis_ani_tbl_data);

/* Forced repositioning across room boundaries: when the player crosses
 * between two listed areas far enough ahead of her, she is teleported rather
 * than made to walk the whole way. */
static SIS_AREA_CHG_SUB sis_area_chg_ry00[3] =
{
    {  2, { 16625.0f,  441.0f,  9864.0f, 1.0f } },
    {  9, {  2011.0f,   12.0f, 11546.0f, 1.0f } },
    { -1, { 0.0f, 0.0f, 0.0f, 0.0f } }
};
static SIS_AREA_CHG_SUB sis_area_chg_ry02[3] =
{
    {  0, { 23138.0f, 1863.0f, 13656.0f, 1.0f } },
    {  4, { 20739.0f, 3000.0f, 24049.0f, 1.0f } },
    { -1, { 0.0f, 0.0f, 0.0f, 0.0f } }
};
static SIS_AREA_CHG_SUB sis_area_chg_ry04[3] =
{
    {  2, { 11654.0f, 3000.0f, 24033.0f, 1.0f } },
    {  6, {  3758.0f, 3000.0f, 25430.0f, 1.0f } },
    { -1, { 0.0f, 0.0f, 0.0f, 0.0f } }
};
static SIS_AREA_CHG_SUB sis_area_chg_ry06[3] =
{
    {  4, {   -87.0f, 3035.0f, 29995.0f, 1.0f } },
    {  8, { -7346.0f, 1304.0f, 30913.0f, 1.0f } },
    { -1, { 0.0f, 0.0f, 0.0f, 0.0f } }
};
static SIS_AREA_CHG_SUB sis_area_chg_ry08[2] =
{
    {  6, { -11545.0f, -3250.0f, 38587.0f, 1.0f } },
    { -1, { 0.0f, 0.0f, 0.0f, 0.0f } }
};
static SIS_AREA_CHG_SUB sis_area_chg_ry09[2] =
{
    {  0, { -4859.0f, 1720.0f, 17344.0f, 1.0f } },
    { -1, { 0.0f, 0.0f, 0.0f, 0.0f } }
};

static SIS_AREA_CHG sis_area_chg[7] =
{
    {  0, sis_area_chg_ry00 },
    {  2, sis_area_chg_ry02 },
    {  4, sis_area_chg_ry04 },
    {  6, sis_area_chg_ry06 },
    {  8, sis_area_chg_ry08 },
    {  9, sis_area_chg_ry09 },
    { -1, nullptr }
};

/* Push-aside headings, quarter turns, indexed by sis_trace.push_dir. */
static float rrot[4] = { 0.0f, 1.5707963705062866f, 3.1415927410125732f,
                         -1.5707963705062866f };

SIS_TRACE    sis_trace;                 /* data  34f3c0 */
SIS_ALG_WORK sis_algo;                  /* sdata 3f4468 */
SIS_SEARCH   sis_search;                /* sdata 3f4470 */
SIS_MOTION   sis_motion;                /* sdata 3f4478 */
float        sistv[4];                  /* data  34fe50 */

static char look_at_pre_kaidan_flg;     /* sbss  3f4fb4 */

static void SisKaidanLookAt(void);
static int  SearchMain(void);

/* Percentage roll shared by SetSisterStatus() and SisterDamageCtrl().
 *
 * The ROM divides by lit4 2147483520.0f, GCC 2.96-ee's truncation of the EE's
 * RAND_MAX; MIOPAN_RAND_MAXF is that same constant and MioPan_Rand() supplies
 * the 31-bit numerator it expects.  Two of the ROM's comparisons on the result
 * are degenerate regardless (">= 0.0" always taken, "< 0.0" never); both are
 * flagged at the call site. */
static float SisRandPercent(void)
{
    return ((float)MioPan_Rand() / MIOPAN_RAND_MAXF) * 100.0f;
}

/* ==========================================================================
 *  Setup
 * ======================================================================== */

void InitSister(void)                                                   /* 167 */
{
    memset(&sis_wrk, 0, sizeof(sis_wrk));
    sis_algo.amode = 0;
    memset(&sis_trace, 0, sizeof(sis_trace));
    memset(&sis_search, 0, sizeof(sis_search));
    memset(&sis_motion, 0, sizeof(sis_motion));

    sis_wrk.se_deadly  = -1;
    sis_wrk.se_matte   = 0;
    sis_wrk.se_konaide = 0;

    /* Her HP pool is the difficulty knob: the damage per hit never changes,
     * she just survives more of them. */
    switch (ingame_wrk.mDifficulty)
    {
    case 0:  sis_wrk.cmn_wrk.st.hpmax = 15000; break;
    case 1:  sis_wrk.cmn_wrk.st.hpmax = 10000; break;
    case 2:  sis_wrk.cmn_wrk.st.hpmax =  7000; break;
    case 3:  sis_wrk.cmn_wrk.st.hpmax =  5000; break;
    default: sis_wrk.cmn_wrk.st.hpmax = 10000; break;
    }

    sis_wrk.cmn_wrk.floor      = 0xb;
    sis_wrk.cmn_wrk.st.hp      = sis_wrk.cmn_wrk.st.hpmax;
    sis_wrk.spd[2]             = 8.0f;
    sis_wrk.cmn_wrk.st.spmax   = 0;
    sis_wrk.cmn_wrk.st.sp      = 0;
    sis_wrk.cmn_wrk.st.rhspdmg = 0;
    sis_wrk.cmn_wrk.st.rhpdmg  = 0;
    sis_wrk.cmn_wrk.st.rspdmg  = 0;
    sis_wrk.cmn_wrk.near_ene_dist_old = 0.0f;
    sis_wrk.cmn_wrk.near_ene_dist     = 0.0f;
    sis_wrk.lock_cnt = 0;

    InitSisterPos();
    InitSisterTrace();

    sis_wrk.pl_dist = GetDistV(sis_wrk.cmn_wrk.mbox.pos, plyr_wrk.cmn_wrk.mbox.pos);

    /* Starts locked and undrawn -- SisterDisp(1) is what brings her in. */
    SisterLock();
    SisterDrawLock();

    look_at_pre_kaidan_flg = 0;
    sis_search.flg = 0;
    sis_algo.amode = 0;                                                 /* 383 */
}

void InitSisterPos(void)
{
    g3dxVu0CopyVector(sis_wrk.cmn_wrk.mbox.pos,  plyr_wrk.cmn_wrk.mbox.pos);
    g3dxVu0CopyVector(sis_wrk.cmn_wrk.mbox.bpos, plyr_wrk.cmn_wrk.mbox.pos);

    /* Placed 175 units behind the player, facing +X. */
    sis_wrk.cmn_wrk.mbox.pos[2] = plyr_wrk.cmn_wrk.mbox.pos[2] - 175.0f;
    sis_wrk.cmn_wrk.mbox.rot[1] = 1.5707963705062866f;
    sis_wrk.cmn_wrk.mbox.pos[3] = 1.0f;                                 /* 392 */
}

/* Collapses the trail to its two-point minimum: [now] is where she is,
 * [top] where the player is. */
void InitSisterTracePos(void)
{
    sis_trace.num  = 2;
    sis_trace.top  = 1;
    sis_trace.now  = 0;
    sis_trace.dist = GetDistV(sis_wrk.cmn_wrk.mbox.pos, plyr_wrk.cmn_wrk.mbox.pos);

    g3dxVu0CopyVector(sis_trace.p[sis_trace.top], plyr_wrk.cmn_wrk.mbox.pos);
    g3dxVu0CopyVector(sis_trace.p[sis_trace.now], sis_wrk.cmn_wrk.mbox.pos);

    sis_trace.l[sis_trace.top] = GetDistV(sis_trace.p[sis_trace.top],
                                          sis_trace.p[sis_trace.now]);  /* 405 */
}

void InitSisterTrace(void)                                              /* 408 */
{
    memset(&sis_trace, 0, sizeof(sis_trace));
    ChangeSisTraceDist(0);
    InitSisterTracePos();                                               /* 414 */
}

void ChangeForceTraceMode(void)                                         /* 418 */
{
    sis_wrk.cmn_wrk.st.dwalk_tm = 0;
    sis_wrk.cmn_wrk.st.mvsta = 0;
    sis_search.flg = 0;
    SetSisterAnime(0, 1);
    sis_algo.amode = 0;
    sis_trace.push_tm = 0;
    sis_trace.push = 0;
    InitSisterTracePos();                                               /* 426 */
}

/* type 0 is the ordinary following distance, type 1 the tighter one used
 * while an active ghost is being tracked (SetModeSisAeneFind). */
void ChangeSisTraceDist(u_char type)                                    /* 432 */
{
    sis_wrk.trace_dist = type;

    if (type == 0)
    {
        sis_trace.redist = 500.0f;      /* stop running at            */
        sis_trace.wsdist = 1000.0f;     /* start walking at           */
        sis_trace.rsdist = 1500.0f;     /* start running at           */
    }
    else
    {
        sis_trace.redist = 400.0f;
        sis_trace.wsdist = 500.0f;
        sis_trace.rsdist = 800.0f;
    }

    sis_trace.wedist = sis_trace.redist;                                /* 447 */
}

void SisterDisp(int sw)
{
    if (sw == 0)
    {
        if (sis_wrk.on != 0)
        {
            sis_wrk.on = 0;
            SisterLock();
            SisterDrawLock();
        }
        return;
    }

    if (sis_wrk.on != 0)
    {
        return;
    }

    sis_wrk.on = 1;
    SisterUnlock();
    SisterDrawUnlock();

    /* Reappears behind the player facing the way he does. */
    g3dxVu0CopyVector(sis_wrk.cmn_wrk.mbox.pos, plyr_wrk.cmn_wrk.mbox.pos);
    sis_wrk.cmn_wrk.mbox.pos[2] = plyr_wrk.cmn_wrk.mbox.pos[2] - 175.0f;
    sis_wrk.cmn_wrk.mbox.rot[1] = plyr_wrk.cmn_wrk.mbox.rot[1];

    InitSisterTracePos();                                               /* 471 */
}

/* Look-at target for the stair animations.  The offset comes off her own bone
 * matrix rather than a world point, so she keeps watching her feet as she
 * turns.  look_at_pre_kaidan_flg holds the target for one extra frame after
 * the stair animation ends so the head eases back instead of snapping. */
static void SisKaidanLookAt(void)                                       /* 478 */
{
    float mtx[4][4];
    float offset[4];
    LOOK_AT_PARAM param;

    param.eye_spd   = 0.4f;
    param.head_spd  = 0.05f;
    param.chest_spd = 0.005f;

    sis_mdlGetMATRIX(mtx, 0);

    u_char a = sis_wrk.anime_no;

    if ((u_char)(a - 0x10) < 2 || a == 0x18 || a == 0x19 ||
        a == 0x12 || a == 0x13 || a == 0x16 || a == 0x17)
    {
        /* Going up: 30 up and 20 forward off the root bone. */
        sceVu0ScaleVector(offset, mtx[1], 30.0f);
        sceVu0AddVector(param.pos, mtx[3], offset);
        sceVu0ScaleVector(offset, mtx[2], 20.0f);
        sceVu0AddVector(param.pos, param.pos, offset);
        SisNeckRegisterTarget(&param, LTP_MAYU_KAIDAN, FLT_MAX);
    }
    else if ((u_char)(a - 0x12) < 2 || a == 0x1a || a == 0x1b ||
             a == 0x14 || a == 0x15 || a == 0x18 || a == 0x19)
    {
        /* Going down: the steps just in front. */
        sceVu0ScaleVector(offset, mtx[2], 10.0f);
        sceVu0AddVector(param.pos, mtx[3], offset);
        SisNeckRegisterTarget(&param, LTP_MAYU_KAIDAN, FLT_MAX);
    }
    else
    {
        if (look_at_pre_kaidan_flg == 0)
        {
            return;
        }

        /* One release frame, anchored on the chest bone instead. */
        sceVu0ScaleVector(offset, mtx[2], 40.0f);
        sis_mdlGetMATRIX(mtx, 2);
        sceVu0AddVector(param.pos, mtx[3], offset);
        SisNeckRegisterTarget(&param, LTP_MAYU_KAIDAN, FLT_MAX);
        look_at_pre_kaidan_flg = 0;
        return;
    }

    look_at_pre_kaidan_flg = 1;                                         /* 542 */
}

/* Blink timer.  MIME part 5 is the eyelid morph; it is only re-armed once the
 * previous blink has finished playing. */
void SisMepachiCtrl(void)                                               /* 550 */
{
    static int no = GetRandValI(200);

    if (IsSisterMimParts(5) == 0 && --no == -1)
    {
        ReqSisterMim(5, 0);
        no = GetRandValI(200);
    }                                                                   /* 556 */
}

float GetDist_Sister2Player(void)
{
    return sis_wrk.pl_dist;                                             /* 564 */
}

/* ==========================================================================
 *  Per-frame entry point
 * ======================================================================== */

void SisterMain(void)                                                   /* 569 */
{
    LOOK_AT_PARAM param;

    /* TEMP PROBE -- strip. Why SisterDebug() may never be reached. */
    {
        static int dbg_main;
        if (debug_var.sis_tr_point != 0 && dbg_main < 10)
        {
            dbg_main++;
            printf("[PROBE %s] on=%d lock_cnt=%d\n", __func__,
                   (int)sis_wrk.on, (int)sis_wrk.lock_cnt);
        }
    }

    if (sis_wrk.on == 0)
    {
        return;
    }

    sis_wrk.pl_dist = GetDistV(sis_wrk.cmn_wrk.mbox.pos, plyr_wrk.cmn_wrk.mbox.pos);
    sis_wrk.s3d.pos = &sis_wrk.cmn_wrk.mbox.pos;

    if (sis_wrk.lock_cnt != 0)
    {
        /* Locked: an event owns her position, but she still has to go through
         * the floor / collision pass. */
        SisterPosUpdate();
        return;
    }

    SisterDebug();

    if (IsReadySisMdl() == 0)
    {
        printf("SIS_MDL IS NOT SETUP\n");
        return;
    }

    SisMepachiCtrl();
    NearEneInfo(&sis_wrk.cmn_wrk);

    if (sis_wrk.cmn_wrk.mode == 9)      /* dead -- SisterGameOver drives it */
    {
        return;
    }

    /* amode 6 (scripted motion) and 7 skip the common status pass. */
    if ((u_char)(sis_algo.amode - 6) > 1)
    {
        SisDoorAct();
        PlyrSPdownCtrl(pl_sta[1]);
        PlyrHPdownCtrl(pl_sta[1]);
        SisterDamageCtrl();
        CalcSisDist();
        MoveSisStairs();
        CheckSisGhost();
        SetSisterStatus();
        SisCondCheck();
    }

    switch (sis_algo.amode)
    {
    case 0: ModeSisTrace();    break;
    case 1: ModeSisFind();     break;
    case 2: SisAlgoMain();     break;
    case 3: ModeSisAeneFind(); break;
    case 4: ModeSisTalk();     break;
    case 5: ModeSisSearch();   break;
    case 6: ModeSisMotion();   break;
    case 7: SisAlgoMain();     break;
    }

    SisterPosUpdate();

    /* mode 1..4 are the damage reactions -- they own the animation. */
    if ((u_char)(sis_wrk.cmn_wrk.mode - 1) > 3 && sis_algo.amode != 6)
    {
        if (sis_algo.amode != 2)
        {
            SisterNAnimeCtrl();
        }

        if (sis_wrk.cmn_wrk.st.invisible_timer != 0)
        {
            sis_wrk.cmn_wrk.st.invisible_timer--;
        }
        if (sis_wrk.btl_recv_tm != 0)
        {
            sis_wrk.btl_recv_tm--;
        }
    }

    SisKaidanLookAt();

    /* Within 2000 units she keeps her head turned towards the player.  The
     * ROM builds the parameter block and then never registers it -- the
     * SisNeckRegisterTarget() call that would consume it is absent, so this
     * is dead in the prototype.  Kept because removing it would hide that. */
    if (GetDist_Sister2Player() <= 2000.0f)
    {
        param.eye_spd   = 0.24f;
        param.head_spd  = 0.06f;
        param.chest_spd = 0.03f;
        sceVu0CopyVector(param.pos, sis_wrk.cmn_wrk.headpos);
    }                                                                   /* 686 */
}

int SisterGameOver(void)                                                /* 690 */
{
    if (sis_wrk.on != 0 && sis_wrk.cmn_wrk.mode == 9)
    {
        return SisDead();
    }
    return 0;                                                           /* 699 */
}

void ReqSisDead(int mode)                                               /* 702 */
{
    SendIngameGameOverPre(1);
    sis_wrk.modedead = (u_char)mode;
    sis_wrk.cmn_wrk.mode = 9;                                           /* 705 */
}

/* ==========================================================================
 *  Death sequence
 *
 *  modedead is a script counter, not a state enum: most arms fall through to
 *  the shared "++modedead" tail, so the sequence advances one step per
 *  satisfied wait.  2 and 4 are the two entry points (caught / drained) and
 *  both jump the counter to 0x1e.
 * ======================================================================== */

int SisDead(void)                                                       /* 709 */
{
    static int cnt;

    DISP_SQAR dsq;
    SQAR_DAT  fade_bg;
    float     center[4];

    u_char eneno = sis_wrk.cmn_wrk.atk_eneno;

    fade_bg.w     = 0x280;
    fade_bg.h     = 0x1c0;
    fade_bg.x     = 0;
    fade_bg.y     = 0;
    fade_bg.pri   = 0;
    fade_bg.r     = 0xff;
    fade_bg.g     = 0xff;
    fade_bg.b     = 0xff;
    fade_bg.alpha = 0;

    switch (sis_wrk.modedead)
    {
    case 2:
    case 4:
        sis_wrk.modedead = 0x1e;
        cnt = 0;
        return 0;

    case 0x1e:
        MapCamTargetChange(1);
        break;

    case 0x15:
    case 0x1f:
        /* Swing the camera onto the midpoint between her and her killer. */
        GetCenterPoint(center, sis_wrk.cmn_wrk.mbox.pos, ene_wrk[eneno].mbox.pos);
        ReqPlyrApproachCameraCtrl(center, 600.0f, 700.0f);
        ReqSisBankPlay(2, 1, 1, 0, &sis_wrk.s3d);
        sis_wrk.modedead++;
        cnt = 0;
        return 0;

    case 0x20:
        if (++cnt < 0x14)
        {
            return 0;
        }
        cnt = 0;
        break;

    case 0x21:
        CopySqrDToSqr(&dsq, &fade_bg);
        dsq.alpha = (u_char)((cnt << 7) / 0x1e);
        DispSqrD(&dsq);
        if (++cnt < 0x1e)
        {
            return 0;
        }
        cnt = 0;
        SetDrawFLG_SI_GameOver();
        EndPlyrApproachCameraCtrl();
        ReqPlyrDeadCameraCtrl(sis_wrk.cmn_wrk.mbox.pos, 200.0f, 500.0f, 1000.0f,
                              sis_wrk.cmn_wrk.mbox.rot);
        sis_wrk.modedead++;
        return 0;

    case 0x22:
        CopySqrDToSqr(&dsq, &fade_bg);
        dsq.alpha = 0x80;
        DispSqrD(&dsq);
        if (++cnt < 0x1e)
        {
            return 0;
        }
        sis_wrk.cmn_wrk.st.hp = 0;
        sis_wrk.cmn_wrk.st.sp = 0;
        SetSisterAnime(0x28, 2);
        cnt = 0;
        sis_wrk.modedead++;
        return 0;

    case 0x23:
        CopySqrDToSqr(&dsq, &fade_bg);
        dsq.alpha = (u_char)(((0x1e - cnt) * 0x80) / 0x1e);
        DispSqrD(&dsq);
        if (cnt == 0xe)
        {
            ReqSisBankPlay(4, 1, 1, 0, &sis_wrk.s3d);
        }
        if (++cnt < 0x1e)
        {
            return 0;
        }
        cnt = 0;
        break;

    case 0x24:
        if (++cnt < 0x14)
        {
            return 0;
        }
        FadeOutReq(0, 0, 0, 0x1e);
        cnt = 0;
        break;

    case 0x25:
        if (++cnt < 0x32)
        {
            return 0;
        }
        cnt = 0;
        EndPlyrApproachCameraCtrl();
        SetOpenCondSwitch(1);
        return 1;

    case 0x0a:
        SetOpenCondSwitch(1);
        return 1;

    case 0x14:
        break;

    case 0x16:
        if (++cnt < 0xf)
        {
            return 0;
        }
        FadeOutReq(0xff, 0xff, 0xff, 0x1e);
        cnt = 0;
        break;

    case 0x17:
        if (++cnt < 0x1e)
        {
            return 0;
        }
        cnt = 0;
        break;

    case 0x18:
        if (++cnt < 0x1e)
        {
            return 0;
        }
        sis_wrk.cmn_wrk.st.hp = 0;
        sis_wrk.cmn_wrk.st.sp = 0;
        EndPlyrApproachCameraCtrl();
        FadeInReq(0xff, 0xff, 0xff, 0x1e);
        SetSisterAnime(0x28, 2);
        PlayerChangeMode(0);
        cnt = 0;
        sis_wrk.modedead++;
        return 0;

    case 0x19:
        if (++cnt < 0x1e)
        {
            return 0;
        }
        SetOpenCondSwitch(1);
        return 1;

    default:
        return 0;
    }

    sis_wrk.modedead++;
    return 0;                                                           /* 875 */
}

/* ==========================================================================
 *  Presence
 * ======================================================================== */

int IsSisWrk(void)
{
    return sis_wrk.on;                                                  /* 883 */
}

void SetSisWrk(int flg)                                                 /* 888 */
{
    if (flg != 0)
    {
        sis_algo.amode = 0;
        sis_wrk.alg.data_addr = 0;
    }
    sis_wrk.on = (u_char)flg;                                           /* 894 */
}

void SisterUnlock(void)
{
    sis_wrk.lock_cnt--;                                                 /* 3746 */
}

void SisterLock(void)
{
    sis_wrk.lock_cnt++;                                                 /* 3751 */
}

void SetSisAreaNo(int area_no)
{
    sis_wrk.cmn_wrk.pr_info.area_no = (u_char)area_no;                  /* 3756 */
}

int GetSisAreaNo(void)
{
    return sis_wrk.cmn_wrk.pr_info.area_no;                             /* 3437 */
}

void SetSisJoinFlg(u_char join_flg)
{
    sis_wrk.join_flg = join_flg;                                        /* 3761 */
}

int GetSisJoinFlg(void)
{
    return sis_wrk.join_flg;                                            /* 3764 */
}

/* ==========================================================================
 *  Behaviour modes
 * ======================================================================== */

void ModeSisTrace(void)                                                 /* 900 */
{
    g3dxVu0CopyVector(sis_trace.trgt, plyr_wrk.cmn_wrk.mbox.pos);
    sis_trace.trgt_floor = (short)plyr_wrk.cmn_wrk.floor;

    if ((sis_wrk.cmn_wrk.st.mvsta & 0x61a8000) != 0 ||
        (u_char)(sis_wrk.cmn_wrk.mode - 1) < 4)
    {
        SisterNoMove();
        return;
    }

    SisterTracePlayer();

    if ((sis_wrk.cmn_wrk.st.mvsta & 0x2000) != 0)
    {
        SisterTraceMove();
        return;
    }

    SisterPushCheck();                                                  /* 916 */
}

void ModeSisFind(void)
{                                                                       /* 939 */
}

/* As ModeSisTrace() but without the damage-mode guard: in a battle she keeps
 * following even while reacting. */
void ModeSisBattle(void)                                                /* 943 */
{
    g3dxVu0CopyVector(sis_trace.trgt, plyr_wrk.cmn_wrk.mbox.pos);
    sis_trace.trgt_floor = (short)plyr_wrk.cmn_wrk.floor;

    if ((sis_wrk.cmn_wrk.st.mvsta & 0x61a8000) != 0)
    {
        SisterNoMove();
        return;
    }

    SisterTracePlayer();

    if ((sis_wrk.cmn_wrk.st.mvsta & 0x2000) != 0)
    {
        SisterTraceMove();
        return;
    }

    SisterPushCheck();                                                  /* 954 */
}

void ModeSisTalk(void)
{                                                                       /* 961 */
}

/* Search failed: she stands still until the player is close enough that a
 * fresh search is worth trying, then retries every half second. */
void ModeSisSearch(void)                                                /* 965 */
{
    static int cnt;

    float dist = GetDistV(sis_wrk.cmn_wrk.mbox.pos, plyr_wrk.cmn_wrk.mbox.pos);

    g3dxVu0CopyVector(sis_trace.trgt, plyr_wrk.cmn_wrk.mbox.pos);
    sis_trace.trgt_floor = (short)plyr_wrk.cmn_wrk.floor;

    if (dist < 2500.0f && ++cnt > 0x1e)
    {
        SetSearchMode();
        cnt = 0;
    }                                                                   /* 976 */
}

/* Unlike ModeSisTrace(), the push check runs even while she is following the
 * trail -- an active ghost makes the player crowd her far more. */
void ModeSisAeneFind(void)                                              /* 3143 */
{
    g3dxVu0CopyVector(sis_trace.trgt, plyr_wrk.cmn_wrk.mbox.pos);
    sis_trace.trgt_floor = (short)plyr_wrk.cmn_wrk.floor;

    if ((sis_wrk.cmn_wrk.st.mvsta & 0x61a8000) != 0)
    {
        SisterNoMove();
        return;
    }

    SisterTracePlayer();

    if ((sis_wrk.cmn_wrk.st.mvsta & 0x2000) != 0)
    {
        SisterTraceMove();
    }
    SisterPushCheck();                                                  /* 3162 */
}

void SetModeSisAeneFind(void)                                           /* 3130 */
{
    sis_algo.amode = 3;
    sis_wrk.trace_dist_bak = sis_wrk.trace_dist;
    ChangeSisTraceDist(1);                                              /* 3133 */
}

void EndModeSisAeneFind(void)                                           /* 3136 */
{
    sis_algo.amode = 0;
    ChangeSisTraceDist(sis_wrk.trace_dist_bak);                         /* 3138 */
}

int GetSisTraceStatus(void)
{
    return sis_algo.amode == 0;                                         /* 3454 */
}

int GetSisStandAnm(void)
{
    return sis_algo.amode != 2;                                         /* 3461 */
}

/* ==========================================================================
 *  Geometry helpers
 * ======================================================================== */

/* Horizontal proximity with a floor-height sanity check: 775 units of Y
 * separation means different storeys, not "next to each other". */
int DistHitCheck(float *v1, float *v2, float dist)                      /* 981 */
{
    if (GetDistV(v1, v2) >= dist)
    {
        return 0;
    }
    if (fabsf(v1[1] - v2[1]) >= 775.0f)
    {
        return 0;
    }
    return 1;                                                           /* 987 */
}

void SetSisterHeight(void)                                              /* 993 */
{
    float tv[4];

    int room = MhCtlGetRoomNo((short)sis_wrk.cmn_wrk.floor, sis_wrk.cmn_wrk.mbox.pos);
    if (room >= 0)
    {
        sis_wrk.cmn_wrk.pr_info.area_no = (u_char)room;
    }

    MhCtlGetMapHeight(tv, sis_wrk.cmn_wrk.mbox.pos,
                      sis_wrk.cmn_wrk.pr_info.area_no, 1);
    sis_wrk.cmn_wrk.mbox.pos[1] = tv[1];                                /* 1000 */
}

/* sis_trace.dist is how far she still has to walk: with line of sight that is
 * just the straight-line distance, without it the summed trail length. */
void CalcSisDist(void)                                                  /* 1004 */
{
    sis_trace.dist = 0.0f;

    if ((sis_wrk.cmn_wrk.st.mvsta & 0x2000) == 0 && sis_trace.view_hit == 0)
    {
        sis_trace.dist = GetDistV(sis_wrk.cmn_wrk.mbox.pos, sis_trace.trgt);
        sis_trace.l[sis_trace.top] = GetDistV(sis_trace.p[sis_trace.top],
                                              sis_trace.p[sis_trace.now]);
        return;
    }

    int i;
    for (i = 0; i < sis_trace.num - 1; i++)
    {
        int n = sis_trace.top - i;
        if (n < 0)
        {
            n += 64;
        }
        sis_trace.dist += sis_trace.l[n];
    }                                                                   /* 1019 */
}

void ClearSisWait(void)                                                 /* 1351 */
{
    sis_wrk.stop_tm = 0;
    sis_wrk.cmn_wrk.st.mvsta &= ~0x10000ULL;                            /* 1354 */
}

/* Line of sight: walls and closed doors both.  Outdoors the MhCtl mesh has no
 * usable occluders, so PlyrOutsideCheck() short-circuits that half. */
int SisterHitCheck(float *v1, int floor1, float *v2, int floor2)        /* 1360 */
{
    int ret = MapHitLineCheck(v1, floor1, v2, floor2, 150.0f) != 0;

    if (ret == 0 && PlyrOutsideCheck() == 0 &&
        MhCtlHitLineCheck(v1, v2, sis_wrk.cmn_wrk.pr_info.area_no) != 0)
    {
        ret = 1;
    }

    return ret;                                                         /* 1381 */
}

int IsSisterTurn(void)
{
    return sis_wrk.cmn_wrk.st.mvsta == 0x800000;                        /* 2937 */
}

void SisCondCheck(void)
{
    sis_wrk.cmn_wrk.st.sta_old = sis_wrk.cmn_wrk.st.sta;                /* 3419 */
}

int CheckSisAnimeEnd(int anime_no)                                      /* 3444 */
{
    if ((sis_wrk.cmn_wrk.st.sta & 0x2000) != 0)
    {
        return 1;
    }
    return anime_no != (int)sis_wrk.anime_no;                           /* 3449 */
}

/* ==========================================================================
 *  Save blocks
 * ======================================================================== */

void SetSave_SisWrk(MC_SAVE_DATA *data)                                 /* 3778 */
{
    data->addr = (u_char *)&sis_wrk;
    data->size = sizeof(sis_wrk);
}

void SetSave_SisTrace(MC_SAVE_DATA *data)                               /* 3785 */
{
    data->addr = (u_char *)&sis_trace;
    data->size = sizeof(sis_trace);
}

void SetSave_SisAlgoWrk(MC_SAVE_DATA *data)                             /* 3792 */
{
    data->addr = &sis_algo.amode;
    data->size = 1;
}

void SetSave_SisMotion(MC_SAVE_DATA *data)                              /* 3799 */
{
    data->addr = (u_char *)&sis_motion;
    data->size = sizeof(sis_motion);
}

/* PORT DEVIATION -- no ROM counterpart.
 *
 * SIS_MOTION::sat is a live pointer to one of the ten static tables in
 * sis_ani_tbl[], and this block is written to the save file verbatim.  On the
 * PS2 that address is fixed by the link map, so reading it back works.  On the
 * host it is an address inside this executable's image, which is not stable
 * across runs (ASLR) or rebuilds -- so a restored sat sends Mayu's motion
 * driver into unmapped memory.
 *
 * The pointer is therefore stored as its sis_ani_tbl index plus one, with 0
 * meaning "none"; the same one-of-ten table is picked up again on load.  Same
 * family as the event-cursor fixups in ev_exe.c / ev_open.c, but an index
 * rather than an EE address, because these tables live in the executable and
 * not in the emulated EE RAM.  to_host == 0 converts for writing, 1 after
 * reading. */
void SisMotionSavePtrFixup(int to_host)
{
    int i;

    if (to_host != 0) {
        uintptr_t slot = (uintptr_t)sis_motion.sat;

        if (slot >= 1 && slot <= SIS_ANI_TBL_MAX) {
            sis_motion.sat = sis_ani_tbl[(int)slot - 1];
        } else {
            sis_motion.sat = nullptr;
        }
        return;
    }

    for (i = 0; i < SIS_ANI_TBL_MAX; i++) {
        if (sis_motion.sat == sis_ani_tbl[i]) {
            sis_motion.sat = (SIS_ANI_TBL *)(uintptr_t)(i + 1);
            return;
        }
    }

    sis_motion.sat = nullptr;
}

/* ==========================================================================
 *  Sound
 * ======================================================================== */

/* Her voice lines never talk over a subtitle. */
void ReqSisBankPlay(int no, int effect, int loop, int fade_time, SND_3D_SET *s3d)
{                                                                       /* 3914 */
    if (SubTitleIsEnd() != 0)
    {
        sis_mdlBankPlay(no, effect, loop, fade_time, s3d, 0x3200, 0x1000);
    }                                                                   /* 3917 */
}

void SisDoorAct(void)                                                   /* 3423 */
{
    if (plyr_wrk.cmn_wrk.mode != 8 || sis_wrk.pl_dist < 2500.0f)
    {
        sis_wrk.se_door_fl = -1;
        return;
    }

    if (sis_wrk.se_door_fl == -1 && SubTitleIsEnd() != 0)
    {
        sis_wrk.se_door_fl =
            (short)sis_mdlBankPlay(9, 1, 1, 0, &sis_wrk.s3d, 0x3200, 0x1000);
    }                                                                   /* 3430 */
}

/* Raises the "ghost nearby" status bits: 0x40 for a type 2 (scripted) ghost,
 * 0x20 for any ordinary one that is not flagged non-hostile. */
void CheckSisGhost(void)                                                /* 3345 */
{
    int i;

    sis_wrk.cmn_wrk.st.sta &= ~0x60ULL;

    for (i = 0; i < 10; i++)
    {
        if (IsActEnemy(i) == 0)
        {
            continue;
        }

        if (ene_wrk[i].type == 2)
        {
            sis_wrk.cmn_wrk.st.sta |= 0x40;
        }
        else if ((ene_wrk[i].attr & 0x8000) == 0)
        {
            sis_wrk.cmn_wrk.st.sta |= 0x20;
        }
    }                                                                   /* 3362 */
}

/* ==========================================================================
 *  Status machine
 *
 *  One long ladder of mvsta tests, ordered by priority: dead first, then the
 *  blocking states, then the movement states.  Each arm decides what she
 *  should be doing next frame and leaves the answer in mvsta for
 *  SisterNAnimeCtrl() to turn into an animation.
 * ======================================================================== */

void SetSisterStatus(void)                                              /* 1023 */
{
    static u_int turn_chk_time;

    float dist = GetDistV(sis_wrk.cmn_wrk.mbox.pos, plyr_wrk.cmn_wrk.mbox.pos);

    if (sis_wrk.cmn_wrk.st.hp == 0)
    {
        sis_wrk.cmn_wrk.st.mvsta =
            (sis_wrk.cmn_wrk.st.mvsta & ~0x069a9fffULL) | 0x4000000;
        return;
    }

    if ((sis_wrk.cmn_wrk.st.mvsta & 0x2000000) == 0)
    {
        /* Lost and cowering: after 90 frames she calls out, and once the
         * player is back inside 2500 units she counts as caught up. */
        if ((sis_wrk.cmn_wrk.st.mvsta & 0x1a0000) != 0 &&
            (sis_wrk.cmn_wrk.st.mvsta & 0x100000) != 0)
        {
            if (sis_wrk.se_cower_cnt == 0 && ++sis_wrk.cower_tm > 0x59)
            {
                sis_wrk.se_cower_cnt = 1;
                ReqSisBankPlay(0x15, 1, 1, 0, &sis_wrk.s3d);
                printf("MIO-\n");
            }
            if (dist < 2500.0f)
            {
                sis_wrk.cmn_wrk.st.mvsta |= 0x2000000;
            }
        }
    }
    else if ((sis_wrk.cmn_wrk.st.sta & 0x6000) != 0)
    {
        sis_wrk.cmn_wrk.st.mvsta &= ~0x061a800fULL;
        SetSearchMode();
    }

    /* A close enough ghost, outside the post-battle grace period, hands her
     * to the behaviour-script interpreter. */
    if ((sis_wrk.cmn_wrk.st.sta & 0x20) != 0 &&
        sis_wrk.cmn_wrk.near_ene_dist < 2200.0f &&
        sis_wrk.btl_recv_tm == 0 &&
        (u_char)(sis_wrk.cmn_wrk.mode - 1) > 3 &&
        sis_wrk.cmn_wrk.st.invisible_timer == 0 &&
        sis_algo.amode != 2)
    {
        ReqSisAlgo(0);
        sis_algo.amode = 2;
    }

    if ((sis_wrk.cmn_wrk.st.mvsta & 0x61a8000) != 0 ||
        (u_char)(sis_wrk.cmn_wrk.mode - 1) < 4)
    {
        goto tail;
    }

    /* A full ring means the trail has grown longer than she could ever walk;
     * treat it as having lost him. */
    if ((sis_wrk.cmn_wrk.st.mvsta & 0x100000) == 0 && sis_trace.num > 0x3f)
    {
        sis_wrk.cower_tm = 0;
        sis_wrk.cmn_wrk.st.mvsta =
            (sis_wrk.cmn_wrk.st.mvsta & ~0x00801fffULL) | 0x100000;
        sis_wrk.se_cower_cnt = 0;
        SetSisterAnime(0, 10);
    }

    if ((sis_wrk.cmn_wrk.st.mvsta & 0x4000) != 0)
    {
        sis_wrk.cmn_wrk.st.mvsta &= ~0x00807fffULL;
    }

    if ((sis_wrk.cmn_wrk.st.mvsta & 0x1000) != 0)
    {
        sis_wrk.cmn_wrk.st.mvsta &= ~0x00801fffULL;
        goto tail;
    }

    if ((sis_wrk.cmn_wrk.st.mvsta & 0xf00) != 0)
    {
        goto tail;                      /* being pushed aside -- leave it */
    }

    if ((sis_wrk.cmn_wrk.st.mvsta & 0x800000) != 0)
    {
        /* Turning in place.  Once she faces the right way she either sets off
         * walking or, if already close, simply stops. */
        if (SetSisTurn() != 0)
        {
            if (sis_trace.dist >= sis_trace.wsdist)
            {
                sis_wrk.cmn_wrk.st.mvsta =
                    (sis_wrk.cmn_wrk.st.mvsta & ~0x00800000ULL) | 0x2001;
            }
            else
            {
                sis_wrk.cmn_wrk.st.mvsta &= ~0x00803fffULL;
            }
            turn_chk_time = 0;
        }
        goto tail;
    }

    if ((sis_wrk.cmn_wrk.st.mvsta & 0x400000) != 0)
    {
        sis_wrk.cmn_wrk.st.mvsta &= ~0x065a8000ULL;     /* fallen -- get up */
        goto tail;
    }

    if ((sis_wrk.cmn_wrk.st.mvsta & 0x200000) != 0)
    {
        /* Stumbling: on the animation-end flag she either recovers into a
         * walk/run or goes all the way down.  The ROM's roll is
         * ">= 0.0f", i.e. always taken, so the fall arm is dead in this
         * build -- kept because dropping it would lose the printf pair. */
        if ((sis_wrk.cmn_wrk.st.sta & 0x4000) != 0)
        {
            sis_wrk.cmn_wrk.st.sta   &= ~0x4000ULL;
            sis_wrk.cmn_wrk.st.mvsta &= ~0x00200000ULL;

            if (SisRandPercent() >= 0.0f)
            {
                printf("fukki!!\n");
                if ((u_short)sis_wrk.run_tm < 10)
                {
                    sis_wrk.cmn_wrk.st.mvsta |= 2;
                }
                else
                {
                    sis_wrk.cmn_wrk.st.mvsta |= 8;
                }
            }
            else
            {
                printf("korobi!!\n");
                sis_wrk.cmn_wrk.st.mvsta |= 0x400000;
            }
        }
        goto tail;
    }

    if ((sis_wrk.cmn_wrk.st.mvsta & 8) != 0)
    {
        /* Hurrying.  Back inside the run-start distance she settles to a walk. */
        if (sis_wrk.pl_dist > 2000.0f && sis_wrk.se_matte == 0)
        {
            if (SisRandPercent() < 10.0f)
            {
                ReqSisBankPlay(9, 1, 1, 0, &sis_wrk.s3d);
            }
            sis_wrk.se_matte++;
        }

        if (sis_trace.dist >= sis_trace.rsdist)
        {
            /* Roll is "<= 0.0f", i.e. never taken in this build. */
            if ((sis_wrk.cmn_wrk.st.sta & 0x4000) != 0 && SisRandPercent() <= 0.0f)
            {
                sis_wrk.cmn_wrk.st.sta   &= ~0x4000ULL;
                sis_wrk.cmn_wrk.st.mvsta =
                    (sis_wrk.cmn_wrk.st.mvsta & ~8ULL) | 0x200000;
                printf("tumaduki!!\n");
            }
        }
        else
        {
            sis_wrk.cmn_wrk.st.mvsta |= 1;
        }
        goto tail;
    }

    if ((sis_wrk.cmn_wrk.st.mvsta & 0x30) != 0)
    {
        /* Running stairs -- drop to the walking stair pair once she is close
         * enough.  run_tm ticks here, which is what identifies 0x10/0x20 as
         * the running pair. */
        if ((sis_wrk.cmn_wrk.st.sta & 0x4000) == 0)
        {
            goto tail;
        }
        sis_wrk.cmn_wrk.st.sta &= ~0x4000ULL;

        if (sis_trace.dist < sis_trace.rsdist && sis_wrk.run_tm >= 0)
        {
            if ((sis_wrk.cmn_wrk.st.mvsta & 0x10) != 0)
            {
                sis_wrk.cmn_wrk.st.mvsta |= 0x40;
            }
            if ((sis_wrk.cmn_wrk.st.mvsta & 0x20) != 0)
            {
                sis_wrk.cmn_wrk.st.mvsta |= 0x80;
            }
        }
        sis_wrk.run_tm++;
    }
    else if ((sis_wrk.cmn_wrk.st.mvsta & 0xc0) != 0)
    {
        /* Walking stairs -- promote to the running pair once she falls far
         * enough behind. */
        if ((sis_wrk.cmn_wrk.st.sta & 0x4000) == 0)
        {
            goto tail;
        }
        if (sis_trace.dist >= sis_trace.rsdist && sis_wrk.walk_tm >= 0)
        {
            if ((sis_wrk.cmn_wrk.st.mvsta & 0x40) != 0)
            {
                sis_wrk.cmn_wrk.st.mvsta |= 0x10;
            }
            if ((sis_wrk.cmn_wrk.st.mvsta & 0x80) != 0)
            {
                sis_wrk.cmn_wrk.st.mvsta |= 0x20;
            }
        }
        sis_wrk.cmn_wrk.st.sta &= ~0x4000ULL;
        sis_wrk.walk_tm++;
    }
    else if ((sis_wrk.cmn_wrk.st.mvsta & 2) != 0)
    {
        /* Running. */
        if (sis_wrk.pl_dist > 2000.0f && sis_wrk.se_matte == 0)
        {
            if (SisRandPercent() < 10.0f)
            {
                ReqSisBankPlay(9, 1, 1, 0, &sis_wrk.s3d);
            }
            sis_wrk.se_matte++;
        }

        if ((sis_wrk.cmn_wrk.st.sta & 0x4000) == 0)
        {
            goto tail;
        }
        sis_wrk.cmn_wrk.st.sta &= ~0x4000ULL;

        if (sis_trace.dist < sis_trace.redist && sis_wrk.run_tm >= 0)
        {
            sis_wrk.cmn_wrk.st.mvsta = (sis_wrk.cmn_wrk.st.mvsta & ~0x00801fffULL) | 1;
        }
        else if ((u_short)sis_wrk.run_tm >= 10)
        {
            sis_wrk.cmn_wrk.st.mvsta = (sis_wrk.cmn_wrk.st.mvsta & ~0x00801fffULL) | 8;
        }
        else if (SisRandPercent() < 0.0f)       /* never taken in this build */
        {
            sis_wrk.cmn_wrk.st.mvsta =
                (sis_wrk.cmn_wrk.st.mvsta & ~0x00801fffULL) | 0x200000;
            printf("tumaduki!!\n");
        }
        sis_wrk.run_tm++;
    }
    else if ((sis_wrk.cmn_wrk.st.mvsta & 1) != 0)
    {
        /* Walking -- promote to a run once she falls too far behind. */
        if ((sis_wrk.cmn_wrk.st.sta & 0x4000) == 0)
        {
            goto tail;
        }
        if (sis_trace.dist >= sis_trace.rsdist && sis_wrk.walk_tm >= 0)
        {
            sis_wrk.cmn_wrk.st.mvsta = (sis_wrk.cmn_wrk.st.mvsta & ~0x00801fffULL) | 2;
        }
        sis_wrk.cmn_wrk.st.sta &= ~0x4000ULL;
        sis_wrk.walk_tm++;
    }
    else if ((sis_wrk.cmn_wrk.st.mvsta & 0x801fff) == 0)
    {
        /* Standing still.  Two ways out: the player walks far enough away
         * that she sets off after him, or he circles behind her long enough
         * that she turns to keep him in view. */
        if ((sis_wrk.cmn_wrk.st.mvsta & 0x10000) == 0)
        {
            float vw[4];
            float psrot;

            GetTrgtRot(sis_wrk.cmn_wrk.mbox.pos, plyr_wrk.cmn_wrk.mbox.pos, vw, 2);
            psrot = vw[1] - sis_wrk.cmn_wrk.mbox.rot[1];
            RotLimitChk(&psrot);

            if (fabsf(psrot) > 2.0943951606750488f)     /* 120 degrees */
            {
                turn_chk_time++;
            }

            if (turn_chk_time >= 0x5b)
            {
                sis_wrk.cmn_wrk.st.mvsta |= 0x800000;
            }
            else if ((u_short)sis_wrk.stop_tm++ > 0x1c1)
            {
                sis_wrk.cmn_wrk.st.mvsta |= 0x10000;
            }
        }

        if (dist >= sis_trace.wsdist)
        {
            sis_wrk.cmn_wrk.st.mvsta |= 0x2001;
            if (sis_trace.view_hit == 0)
            {
                InitSisterTracePos();
            }
        }
    }
    else
    {
        sis_wrk.cmn_wrk.st.mvsta &= ~0x00801fffULL;
    }

tail:
    if ((sis_wrk.cmn_wrk.st.mvsta & 0x801fff) == 0)
    {
        sis_wrk.se_matte = 0;

        if (sis_algo.amode != 2)
        {
            turn_chk_time = 0;
            if ((sis_wrk.cmn_wrk.st.mvsta & 0x10000) != 0)
            {
                SetSisterAnime(0, 0x14);
            }
            ClearSisWait();
        }
    }

    if ((sis_wrk.cmn_wrk.st.mvsta & 1) == 0)
    {
        sis_wrk.walk_tm = 0;
    }
    if ((sis_wrk.cmn_wrk.st.mvsta & 0x200002) == 0)
    {
        sis_wrk.run_tm = 0;
    }                                                                   /* 1347 */
}

/* ==========================================================================
 *  Trail recording and following
 * ======================================================================== */

/* Lays down waypoints behind the player.  Whenever line of sight is clear and
 * he has stopped moving the whole trail collapses back to its two-point
 * minimum; otherwise a point is appended each time he gets more than 100
 * units from the last one. */
void SisterTracePlayer(void)                                            /* 1386 */
{
    float vws[4];
    float vwp[4];

    float dist = GetDistV(sis_wrk.cmn_wrk.mbox.pos, plyr_wrk.cmn_wrk.mbox.pos);

    /* Sight is tested eye-to-eye, not foot-to-foot. */
    _SetVector(vws, sis_wrk.cmn_wrk.mbox.pos[0], sis_wrk.cmn_wrk.headpos[1],
               sis_wrk.cmn_wrk.mbox.pos[2], sis_wrk.cmn_wrk.mbox.pos[3]);
    _SetVector(vwp, plyr_wrk.cmn_wrk.mbox.pos[0], plyr_wrk.cmn_wrk.headpos[1],
               plyr_wrk.cmn_wrk.mbox.pos[2], plyr_wrk.cmn_wrk.mbox.pos[3]);

    sis_trace.view_hit = SisterHitCheck(vws, (short)sis_wrk.cmn_wrk.floor,
                                        vwp, (short)plyr_wrk.cmn_wrk.floor) != 0;

    if (sis_trace.cnt >= 10)
    {
        int prev = sis_trace.top;

        if (CompVector(plyr_wrk.cmn_wrk.mbox.bpos, plyr_wrk.cmn_wrk.mbox.pos) == 0 &&
            sis_trace.view_hit == 0)
        {
            /* He is standing still and she can see him: forget the trail. */
            sis_trace.top = 1;
            sis_trace.num = 2;
            sis_trace.now = 0;

            g3dxVu0CopyVector(sis_trace.p[1], plyr_wrk.cmn_wrk.mbox.pos);
            g3dxVu0CopyVector(sis_trace.p[sis_trace.now], sis_wrk.cmn_wrk.mbox.pos);
            sis_trace.fl[sis_trace.top] = 0;

            sis_trace.l[sis_trace.top] = GetDistV(sis_trace.p[sis_trace.top],
                                                  sis_trace.p[sis_trace.now]);
            sis_trace.dist = sis_trace.l[sis_trace.top];
            sis_trace.cnt = 0;
        }
        else if (dist <= sis_trace.wsdist && sis_trace.view_hit == 0)
        {
            /* Close and visible -- nothing worth recording. */
            sis_trace.cnt++;
            return;
        }
        else if (GetDistV(sis_trace.p[prev], plyr_wrk.cmn_wrk.mbox.pos) > 100.0f)
        {
            if ((sis_wrk.cmn_wrk.st.mvsta & 0x61a8000) == 0)
            {
                sis_trace.top = (sis_trace.top + 1) % 64;
                if (sis_trace.num < 64)
                {
                    sis_trace.num++;
                }

                g3dxVu0CopyVector(sis_trace.p[sis_trace.top],
                                 plyr_wrk.cmn_wrk.mbox.pos);
                sis_trace.fl[sis_trace.top] = 0;
                sis_trace.l[sis_trace.top] =
                    GetDistV(sis_trace.p[sis_trace.top], sis_trace.p[prev]);
                CalcSisDist();
            }
            sis_trace.cnt = 0;
        }
    }

    sis_trace.cnt++;                                                    /* 1456 */
}

/* Walks her along the recorded trail by her current speed, consuming
 * waypoints as she passes them.  Each time one is consumed the rest of the
 * trail is checked for a self-crossing and short-cut across it, which stops
 * her retracing a loop the player walked. */
void SisterTraceMove(void)                                              /* 1461 */
{
    LINE_T l1;
    LINE_T l2;
    float  vr[4];

    float pdist = GetDistV2(sis_wrk.cmn_wrk.mbox.pos, sis_trace.trgt);

    /* Close enough on the same floor: stop outright. */
    if (DistHitCheck(sis_wrk.cmn_wrk.mbox.pos, sis_trace.trgt, 140.0f) != 0 &&
        (short)sis_wrk.cmn_wrk.floor == sis_trace.trgt_floor)
    {
        sis_wrk.cmn_wrk.st.mvsta &= ~0x00803fffULL;
        return;
    }

    if (sis_trace.push != 0 ||
        (pdist <= sis_trace.wedist && sis_wrk.walk_tm >= 0))
    {
        sis_wrk.cmn_wrk.st.mvsta |= 0x4000;         /* arrived */
        return;
    }

    float spd = sis_wrk.spd[2];
    if (debug_var.hi_spd != 0)
    {
        spd = sis_wrk.spd[2] * 5.0f;
    }

    int   n1 = (sis_trace.now + 1) % 64;
    int   n2 = sis_trace.now % 64;
    float rdist = sis_trace.l[n1];

    while (spd != 0.0f)
    {
        if (spd < rdist)
        {
            /* Partial step: slide the current waypoint towards the next. */
            sis_trace.p[n2][0] +=
                (spd * (sis_trace.p[n1][0] - sis_trace.p[n2][0])) / rdist;
            sis_trace.p[n2][2] +=
                (spd * (sis_trace.p[n1][2] - sis_trace.p[n2][2])) / rdist;
            spd = 0.0f;
            continue;
        }

        if (sis_trace.top == sis_trace.now)
        {
            spd = 0.0f;                 /* nothing left to walk */
            continue;
        }

        sis_trace.now = (sis_trace.now + 1) % 64;
        if (sis_trace.num > 1)
        {
            sis_trace.num--;
        }

        if (sis_trace.num > 3)
        {
            int i;
            int n = -1;
            int a = sis_trace.now % 64;
            int b = (sis_trace.now + 1) % 64;

            l1.a.x = sis_trace.p[a][0];
            l1.a.y = sis_trace.p[a][2];
            l1.b.x = sis_trace.p[b][0];
            l1.b.y = sis_trace.p[b][2];

            for (i = 0; i < sis_trace.num - 3 && n == -1; i++)
            {
                int c = sis_trace.top - i;
                int d = c - 1;
                if (d < 0) d += 64;
                if (c < 0) c += 64;

                l2.a.x = sis_trace.p[d][0];
                l2.a.y = sis_trace.p[d][2];
                l2.b.x = sis_trace.p[c][0];
                l2.b.y = sis_trace.p[c][2];

                if (LineIntersect(&l1, &l2) == 0)
                {
                    continue;
                }

                /* Only short-cut across a crossing on the same storey. */
                if (fabs((double)(sis_trace.p[a][1] - sis_trace.p[d][1])) >= 775.0)
                {
                    continue;
                }

                g3dxVu0CopyVector(sis_trace.p[d], sis_trace.p[sis_trace.now]);
                n = 0;
                sis_trace.num = i + 2;
                sis_trace.now = d;
                sis_trace.l[c] = GetDistV(sis_trace.p[d], sis_trace.p[c]);
                CalcSisDist();
            }
        }

        spd -= rdist;
        n2 = sis_trace.now % 64;
        n1 = (sis_trace.now + 1) % 64;
        rdist = GetDistV(sis_trace.p[n2], sis_trace.p[n1]);
    }

    /* Face along the segment she is on. */
    n2 = sis_trace.now % 64;
    n1 = (sis_trace.now + 1) % 64;
    sis_wrk.cmn_wrk.mbox.brot[1] = sis_wrk.cmn_wrk.mbox.rot[1];
    GetTrgtRot(sis_trace.p[n2], sis_trace.p[n1], vr, 2);

    float f = vr[1] - sis_wrk.cmn_wrk.mbox.brot[1];
    RotLimitChk(&f);

    if (fabsf(f) <= 0.0872664950788021f)        /* 5 degrees -- close enough */
    {
        return;
    }

    if (fabsf(f) <= 1.5707963705062866f)        /* up to 90 degrees: ease in */
    {
        float div = GetPALMode() == 0 ? 8.0f : 6.6666665077209473f;

        sis_wrk.cmn_wrk.mbox.rot[1] = vr[1];
        if (fabsf(f) > 0.0436332486569881f)     /* 2.5 degrees */
        {
            sis_wrk.cmn_wrk.mbox.rot[1] = sis_wrk.cmn_wrk.mbox.brot[1] + f / div;
        }
        RotLimitChk(&sis_wrk.cmn_wrk.mbox.rot[1]);
        return;
    }

    sis_wrk.cmn_wrk.st.mvsta |= 0x800000;       /* too far -- turn in place */
}                                                                       /* 1632 */

/* Rewrites the waypoint she is standing on with her actual position, which is
 * where the collision pass may have pushed her. */
void SetNowTracePos(void)                                               /* 1636 */
{
    int n2 = sis_trace.now % 64;
    int n1 = (sis_trace.now + 1) % 64;

    g3dxVu0CopyVector(sis_trace.p[n2], sis_wrk.cmn_wrk.mbox.pos);
    sis_trace.l[n1] = GetDistV(sis_trace.p[n2], sis_trace.p[n1]);
    CalcSisDist();                                                      /* 1643 */
}

/* Turns her towards GetSisterRot() a step at a time; returns non-zero once
 * she is within 5 degrees of it. */
int SetSisTurn(void)                                                    /* 3367 */
{
    float vw[4];
    float psrot;

    float rw   = GetSisterRot();
    float step = GetPALMode() == 0 ? 0.1396263986825943f    /*  8 degrees */
                                   : 0.1675516068935394f;   /* 9.6 degrees */

    psrot = rw - sis_wrk.cmn_wrk.mbox.rot[1];
    RotLimitChk(&psrot);

    if (fabs((double)psrot) <= (double)step)
    {
        /* Within one step of the target.  Both arms of the ROM's test add the
         * *magnitude*, so a target to the left is turned away from rather
         * than towards -- she comes back round on the next frame because the
         * residual is recomputed below.  Reproduced as written; "fixing" it
         * would change how she settles out of a turn. */
        if (psrot < 0.0f)
        {
            sis_wrk.cmn_wrk.mbox.rot[1] -= psrot;
        }
        else
        {
            sis_wrk.cmn_wrk.mbox.rot[1] += psrot;
        }
    }
    else if (psrot < 0.0f)
    {
        sis_wrk.cmn_wrk.mbox.rot[1] -= step;
    }
    else
    {
        sis_wrk.cmn_wrk.mbox.rot[1] += step;
    }

    RotLimitChk(&sis_wrk.cmn_wrk.mbox.rot[1]);

    psrot = rw - sis_wrk.cmn_wrk.mbox.rot[1];
    RotLimitChk(&psrot);

    sis_wrk.cmn_wrk.mbox.brot[1] = sis_wrk.cmn_wrk.mbox.rot[1];
    g3dxVu0CopyVector(sis_wrk.wpos, sis_wrk.cmn_wrk.mbox.pos);

    return fabsf(psrot) <= 0.0872664950788021f;                         /* 3403 */
}

/* Heading she should be facing: along the segment she is walking, or straight
 * at the target once the trail is down to its minimum. */
float GetSisterRot(void)                                                /* 2146 */
{
    float vw[4];

    if (sis_trace.num < 3)
    {
        GetTrgtRot(sis_wrk.cmn_wrk.mbox.pos, sis_trace.trgt, vw, 2);
    }
    else
    {
        int n1 = (sis_trace.now + 1) % 64;
        GetTrgtRot(sis_trace.p[sis_trace.now], sis_trace.p[n1], vw, 2);
    }

    return vw[1];                                                       /* 2161 */
}

/* Stair animation select: compares her heading against the heading of the
 * flight she is standing on and raises the matching up/down bit. */
void MoveSisStairs(void)                                                /* 3280 */
{
    float fw;
    float rotw;

    if (MrecGetStaInfo(&rotw, (short)sis_wrk.cmn_wrk.floor,
                       sis_wrk.cmn_wrk.mbox.pos) < 1)
    {
        if ((sis_wrk.cmn_wrk.st.mvsta & 0x30) != 0)
        {
            sis_wrk.cmn_wrk.st.mvsta &= ~0xf0ULL;
        }
        if ((sis_wrk.cmn_wrk.st.mvsta & 0xc0) != 0)
        {
            sis_wrk.cmn_wrk.st.mvsta &= ~0xf0ULL;
        }
        return;
    }

    fw = rotw - sis_wrk.cmn_wrk.mbox.rot[1];
    RotLimitChk(&fw);

    float deg = (fw * 180.0f) / 3.1415927410125732f;

    if (deg > -65.0f && deg < 65.0f)
    {
        /* Facing up the flight. */
        if ((sis_wrk.cmn_wrk.st.mvsta & 2) != 0)
        {
            sis_wrk.cmn_wrk.st.mvsta |= 0x10;
        }
        else if ((sis_wrk.cmn_wrk.st.mvsta & 1) != 0 ||
                 (sis_wrk.cmn_wrk.st.mvsta & 0xc) != 0)
        {
            sis_wrk.cmn_wrk.st.mvsta |= 0x40;
        }
    }
    else if ((deg > -180.0f && deg < -115.0f) || (deg > 115.0f && deg < 180.0f))
    {
        /* Facing down it. */
        u_long bit;

        if ((sis_wrk.cmn_wrk.st.mvsta & 2) != 0)
        {
            bit = 0x20;
        }
        else if ((sis_wrk.cmn_wrk.st.mvsta & 1) != 0 ||
                 (sis_wrk.cmn_wrk.st.mvsta & 0xc) != 0)
        {
            bit = 0x80;
        }
        else
        {
            return;
        }
        sis_wrk.cmn_wrk.st.mvsta |= bit;
    }
    else
    {
        /* Crossing it sideways -- no stair animation. */
        if ((sis_wrk.cmn_wrk.st.mvsta & 0x30) != 0)
        {
            sis_wrk.cmn_wrk.st.mvsta &= ~0xf0ULL;
        }
        if ((sis_wrk.cmn_wrk.st.mvsta & 0xc0) != 0)
        {
            sis_wrk.cmn_wrk.st.mvsta &= ~0xf0ULL;
        }
    }                                                                   /* 3338 */
}

/* ==========================================================================
 *  Push-out
 *
 *  sis_trace.push is a small state machine:
 *    0  idle
 *    1  a collision was just detected -- pick the direction and the animation
 *    2  ease the heading round over 14 frames, then go to 10
 *    3  as 2, but snap to push_rot and reset push_dir when it lands
 *    10 slide along push_dir until push_dist runs out
 * ======================================================================== */

void ClearSisterPushMove(void)                                          /* 2141 */
{
    sis_trace.push = 0;
    sis_wrk.cmn_wrk.st.mvsta &= ~0x1f00ULL;                             /* 2142 */
}

/* Player walked into her: pick a quarter-turn direction to step aside in.
 *
 * a[] holds the four candidate player headings and b[] the four candidate
 * companion headings; c[] pairs each player heading with the companion
 * heading inside a 45-degree window of it, and d[] orders the surviving
 * candidates by how far each is from the direction he is pushing from.  The
 * first candidate whose 200-unit trial step is not blocked wins. */
void SisterPushCheck(void)                                              /* 2166 */
{
    float mpos[4]  = { 0.0f, 0.0f, 200.0f, 1.0f };
    float mopos[4] = { 0.0f, 0.0f,  25.0f, 1.0f };
    float wlm[4][4];
    float vw[4][4];
    float a[4];
    float b[4];
    int   c[4];
    int   d[4][2];
    int   flg[4];
    float psrot;
    float rot;
    float trgt[4];
    int   n = 0;
    int   i;
    int   j;
    int   dir;
    float step;

    const float qpai    = 1.5707963705062866f;  /*  90 degrees */
    const float pai     = 3.1415927410125732f;  /* 180 degrees */
    const float quarter = 0.7853981852531433f;  /*  45 degrees */
    const float twopai  = 6.2831854820251465f;

    if (sis_trace.push_tm > 0)
    {
        sis_trace.push_tm--;
    }

    if (fabsf(plyr_wrk.cmn_wrk.mbox.pos[1] - sis_wrk.cmn_wrk.mbox.pos[1]) < 775.0f &&
        DistHitCheck(sis_wrk.cmn_wrk.mbox.pos, plyr_wrk.cmn_wrk.mbox.pos, 140.0f) != 0 &&
        sis_trace.push == 0 &&
        MapHitLineCheck(sis_wrk.cmn_wrk.mbox.pos, (short)sis_wrk.cmn_wrk.floor,
                        plyr_wrk.cmn_wrk.mbox.pos, (short)plyr_wrk.cmn_wrk.floor,
                        140.0f) == 0)
    {
        float srot = sis_wrk.cmn_wrk.mbox.rot[1];
        float prot = plyr_wrk.cmn_wrk.mbox.rot[1];

        c[0] = c[1] = c[2] = c[3] = -1;

        GetTrgtRot(plyr_wrk.cmn_wrk.mbox.pos, sis_wrk.cmn_wrk.mbox.pos, trgt, 2);
        float from = trgt[1];

        a[0] = prot - qpai;
        a[1] = prot + qpai;
        a[2] = prot;
        a[3] = prot + pai;

        b[0] = srot;
        b[1] = srot + qpai;
        b[2] = srot + pai;
        b[3] = srot + 4.7123889923095703f;      /* 270 degrees */

        for (i = 0; i < 4; i++)
        {
            RotLimitChk(&a[i]);
            RotLimitChk(&b[i]);
        }

        for (i = 0; i < 4; i++)
        {
            for (j = 0; j < 4; j++)
            {
                if (a[i] >= 2.3561944961547852f)        /*  135 degrees */
                {
                    /* Window runs off the top of the range -- wrap it. */
                    if (b[j] > a[i] - quarter && b[j] <= pai)
                    {
                        c[i] = j;
                    }
                    else if (b[j] > -pai && b[j] <= (a[i] - twopai) + quarter)
                    {
                        c[i] = j;
                    }
                }
                else if (a[i] < -2.3561944961547852f)   /* -135 degrees */
                {
                    if (b[j] > -pai && b[j] <= a[i] + quarter)
                    {
                        c[i] = j;
                    }
                    else if (b[j] > (a[i] + twopai) - quarter && b[j] <= pai)
                    {
                        c[i] = j;
                    }
                }
                else if (b[j] > a[i] - quarter && b[j] <= a[i] + quarter)
                {
                    c[i] = j;
                }
            }
        }

        psrot = b[c[0]] - from;
        rot   = b[c[1]] - from;
        RotLimitChk(&psrot);
        RotLimitChk(&rot);
        psrot = fabsf(psrot);
        rot   = fabsf(rot);

        if (rot < psrot)
        {
            d[0][0] = c[1]; d[0][1] = 0;
            d[1][0] = c[2]; d[1][1] = 2;
            d[2][0] = -1;
            n = 1;
        }
        else if (psrot < rot)
        {
            d[0][0] = c[0]; d[0][1] = 1;
            d[1][0] = c[2]; d[1][1] = 2;
            d[2][0] = -1;
            n = 1;
        }
        else
        {
            d[0][0] = c[0]; d[0][1] = 1;
            d[1][0] = c[1]; d[1][1] = 0;
            d[2][0] = c[2]; d[2][1] = 2;
            d[3][0] = -1;
            n = 2;
        }

        /* Trial-step 200 units along each candidate and record whether the
         * geometry allows it. */
        for (i = 0; d[i][0] != -1; i++)
        {
            float step = b[d[i][0]];
            RotLimitChk(&step);

            sceVu0UnitMatrix(wlm);
            sceVu0RotMatrixY(wlm, wlm, step);
            sceVu0TransMatrix(wlm, wlm, sis_wrk.cmn_wrk.mbox.pos);
            sceVu0ApplyMatrix(vw[i], wlm, mpos);

            flg[d[i][0]] = MapHitLineCheck(sis_wrk.cmn_wrk.mbox.pos,
                                           (short)sis_wrk.cmn_wrk.floor, vw[i],
                                           (short)sis_wrk.cmn_wrk.floor, 150.0f);
            flg[d[i][0]] += MapHitCheck(vw[i], vw[i], sis_wrk.cmn_wrk.mbox.pos,
                                        150.0f, (short)sis_wrk.cmn_wrk.floor);
        }

        int found = 0;
        for (i = 0; d[i][0] != -1; i++)
        {
            if (flg[d[i][0]] == 0)
            {
                found = 1;
                sis_trace.push_pldir = d[i][1];
                sis_trace.push_rot   = a[sis_trace.push_pldir];
                sis_trace.push_dir   = d[i][0];
                break;
            }
        }

        if (found == 0)
        {
            /* Everything is blocked -- take the last candidate anyway. */
            sis_trace.push_pldir = d[n][1];
            sis_trace.push_dir   = d[n][0];
            sis_trace.push_rot   = a[sis_trace.push_pldir];
        }

        PlyrVibCtrlBig(0x5a, 7);

        if (sis_wrk.push_se_tm == 0)
        {
            if (GetRndSP(1, 99) > 9)
            {
                ReqSisBankPlay(GetRndSP(0, 3) + 0x12, 1, 1, 0, &sis_wrk.s3d);
            }
            sis_wrk.push_se_tm =
                (short)(int)((float)MioPan_Rand() / MIOPAN_RAND_MAXF) * 0x96 + 0x96;
        }

        sis_wrk.cmn_wrk.st.mvsta |= (u_long)(0x100 << (sis_trace.push_dir & 0x1f));
        sis_trace.push_dist = 200.0f;
        sis_trace.push = 1;
        g3dxVu0CopyVector(sis_wrk.wpos, sis_wrk.cmn_wrk.mbox.pos);
    }

    if (sis_trace.push == 0)
    {
        goto tail;
    }

    if (sis_trace.push == 1)
    {
        /* Decide how she gets out of the way: sideways with a turn, or a
         * straight back-step (animation 0x1b). */
        if (sis_trace.push_pldir == 2 &&
            (sis_trace.push_dir == 1 || sis_trace.push_dir == 3))
        {
            sis_trace.push = sis_trace.push_pldir;      /* 2 -- turn first */
            sis_trace.push_dir = 0;
            SetSisterAnime(10, 4);
        }
        else if (sis_trace.push_pldir == 2 && sis_trace.push_dir == 2 &&
                 sis_trace.push_tm != 0)
        {
            sis_trace.push = 3;
            SetSisterAnime(10, 4);
        }
        else
        {
            sis_trace.push = 10;
            if (sis_trace.push_pldir == 2 && sis_trace.push_dir == 0)
            {
                SetSisterAnime(10, 4);
            }
            else
            {
                SetSisterAnime(10, 4);
                SetSisterAnime(0x1b, 4);        /* back-step */
            }
        }
        sis_trace.push_cnt  = 0;
        sis_trace.push_orot = sis_wrk.cmn_wrk.mbox.rot[1];
        goto tail;
    }

    if (sis_trace.push == 2)
    {
        if ((float)sis_trace.push_cnt > 14.0f)
        {
            sis_trace.push = 10;
        }
        else
        {
            psrot = sis_trace.push_rot - sis_trace.push_orot;
            RotLimitChk(&psrot);
            sis_wrk.cmn_wrk.mbox.rot[1] =
                sis_trace.push_orot + ((float)sis_trace.push_cnt * psrot) / 14.0f;
            RotLimitChk(&sis_wrk.cmn_wrk.mbox.rot[1]);
            sis_trace.push_cnt++;
        }
        goto tail;
    }

    if (sis_trace.push == 3)
    {
        /* Same ease, but this arm does not wrap the delta first and snaps to
         * push_rot when it lands. */
        sis_wrk.cmn_wrk.mbox.rot[1] =
            sis_trace.push_orot +
            ((float)sis_trace.push_cnt * (sis_trace.push_rot - sis_trace.push_orot)) / 14.0f;
        RotLimitChk(&sis_wrk.cmn_wrk.mbox.rot[1]);
        sis_trace.push_cnt++;

        if ((float)sis_trace.push_cnt >= 14.0f)
        {
            SetSisterAnime(10, 4);
            sis_trace.push_dir = 0;
            sis_wrk.cmn_wrk.mbox.rot[1] = sis_trace.push_rot;
            sis_trace.push = 10;
            sis_trace.push_cnt = 0;
        }
        goto tail;
    }

    if (sis_trace.push != 10)
    {
        goto tail;
    }

    /* Sliding.  A back-step moves at her own speed scaled by 1.2 and ends on
     * the animation-end flag; anything else spends the 200-unit budget 25 at
     * a time. */
    if (sis_wrk.anime_no == 0x1b)
    {
        step = -sis_wrk.spd[2] * 1.2000000476837158f;
    }
    else
    {
        step = sis_trace.push_dist <= 25.0f ? sis_trace.push_dist : 25.0f;
    }

    sis_trace.push_dist -= step;
    mopos[2] = step;

    dir = sis_trace.push_dir;
    if (sis_trace.push_pldir < 2 &&
        (sis_trace.push_dir == 1 || sis_trace.push_dir == 3) &&
        (float)sis_trace.push_cnt <= 14.0f)
    {
        /* Still easing the heading round while she slides. */
        psrot = sis_trace.push_rot - sis_trace.push_orot;
        RotLimitChk(&psrot);
        sis_wrk.cmn_wrk.mbox.rot[1] =
            sis_trace.push_orot + ((float)sis_trace.push_cnt * psrot) / 14.0f;
        RotLimitChk(&sis_wrk.cmn_wrk.mbox.rot[1]);
        sis_trace.push_cnt++;
    }

    rot = rrot[dir] + sis_trace.push_orot;
    RotLimitChk(&rot);
    sceVu0UnitMatrix(wlm);
    sceVu0RotMatrixY(wlm, wlm, rot);
    sceVu0TransMatrix(wlm, wlm, sis_wrk.cmn_wrk.mbox.pos);
    sceVu0ApplyMatrix(sis_wrk.wpos, wlm, mopos);

    if (sis_wrk.anime_no == 0x1b)
    {
        if ((sis_wrk.cmn_wrk.st.sta & 0x2000) == 0)
        {
            goto tail;
        }
    }
    else if (sis_trace.push_dist > 0.0f)
    {
        goto tail;
    }

    sis_wrk.cmn_wrk.st.mvsta |= 0x1000;
    sis_trace.push_tm = 0x14;
    sis_trace.push = 0;
    if (sis_trace.push_pldir == 2 && sis_trace.push_dir == 0)
    {
        sis_trace.push_tm = 8;
    }

tail:
    if (sis_wrk.push_se_tm != 0)
    {
        sis_wrk.push_se_tm--;
    }                                                                   /* 2137 */
}

/* ==========================================================================
 *  Position update
 * ======================================================================== */

/* Applies the frame's movement, resolves it against the map, then keeps the
 * player out of her: if the two overlap, whichever of them moved is rewound. */
void SisterPosUpdate(void)                                              /* 2199 */
{
    float tv[4];

    g3dxVu0CopyVector(sis_wrk.cmn_wrk.mbox.bpos, sis_wrk.cmn_wrk.mbox.pos);

    if ((sis_wrk.cmn_wrk.st.mvsta & 0x2000) != 0 &&
        sis_algo.amode != 2 && sis_algo.amode != 6)
    {
        /* Following the trail: her position is simply the waypoint. */
        g3dxVu0CopyVector(sis_wrk.cmn_wrk.mbox.pos, sis_trace.p[sis_trace.now]);
    }
    else if ((sis_wrk.cmn_wrk.st.mvsta & 0x801f00) != 0)
    {
        /* Being pushed aside or turning: wpos already holds the answer. */
        g3dxVu0CopyVector(sis_wrk.cmn_wrk.mbox.pos, sis_wrk.wpos);
    }
    else if (sis_wrk.cmn_wrk.mode == 2)
    {
        g3dxVu0CopyVector(sis_wrk.cmn_wrk.mbox.bpos, sis_wrk.cmn_wrk.mbox.pos);
        return;
    }
    else
    {
        g3dxVu0CopyVector(tv, sis_wrk.spd);
        RotLimitChk(&sis_wrk.cmn_wrk.mbox.rot[1]);
        RotFvector(sis_wrk.cmn_wrk.mbox.rot, tv);
        sceVu0AddVector(sis_wrk.cmn_wrk.mbox.pos, sis_wrk.cmn_wrk.mbox.pos, tv);
    }

    sis_lalg.hit = (u_char)MapHitCheck(sis_wrk.cmn_wrk.mbox.pos,
                                       sis_wrk.cmn_wrk.mbox.pos,
                                       sis_wrk.cmn_wrk.mbox.bpos, 150.0f,
                                       (short)sis_wrk.cmn_wrk.floor);

    if (DistHitCheck(sis_wrk.cmn_wrk.mbox.pos, plyr_wrk.cmn_wrk.mbox.pos, 140.0f) != 0)
    {
        if ((u_char)(sis_wrk.cmn_wrk.mode - 1) < 4)
        {
            /* She is reacting to damage -- the player gives way. */
            if (DistHitCheck(sis_wrk.cmn_wrk.mbox.pos,
                             plyr_wrk.cmn_wrk.mbox.bpos, 140.0f) == 0 ||
                GetDistV(sis_wrk.cmn_wrk.mbox.pos, plyr_wrk.cmn_wrk.mbox.bpos) >
                GetDistV(sis_wrk.cmn_wrk.mbox.pos, plyr_wrk.cmn_wrk.mbox.pos))
            {
                g3dxVu0CopyVector(plyr_wrk.cmn_wrk.mbox.pos,
                                 plyr_wrk.cmn_wrk.mbox.bpos);
            }
        }
        else
        {
            if (DistHitCheck(sis_wrk.cmn_wrk.mbox.bpos,
                             plyr_wrk.cmn_wrk.mbox.pos, 140.0f) != 0)
            {
                if (DistHitCheck(sis_wrk.cmn_wrk.mbox.pos,
                                 plyr_wrk.cmn_wrk.mbox.bpos, 140.0f) == 0 ||
                    (DistHitCheck(sis_wrk.cmn_wrk.mbox.bpos,
                                  plyr_wrk.cmn_wrk.mbox.bpos, 140.0f) != 0 &&
                     GetDistV(sis_wrk.cmn_wrk.mbox.bpos,
                              plyr_wrk.cmn_wrk.mbox.bpos) >
                     GetDistV(sis_wrk.cmn_wrk.mbox.pos,
                              plyr_wrk.cmn_wrk.mbox.pos)))
                {
                    g3dxVu0CopyVector(plyr_wrk.cmn_wrk.mbox.pos,
                                     plyr_wrk.cmn_wrk.mbox.bpos);
                }
            }
            g3dxVu0CopyVector(sis_wrk.cmn_wrk.mbox.pos, sis_wrk.cmn_wrk.mbox.bpos);
        }
        sis_lalg.hit = 1;
    }

    SetSisterHeight();
    SetNowTracePos();
    g3dxVu0CopyVector(sis_wrk.wpos, sis_wrk.cmn_wrk.mbox.pos);
    PlyrDWalkTmCtrl(&sis_wrk.cmn_wrk);                                  /* 2279 */
}

/* She is standing still and the player has walked into her: slide *him*
 * around her rather than letting him stop dead.  The arc is proportional to
 * how far he moved this frame, in whichever direction he is already passing. */
void SisterNoMove(void)                                                 /* 2283 */
{
    POINT_T pt;
    LINE_T  lt;
    float   vt[4];
    float   wpos[4];
    float   wlm[4][4];
    float   rot;

    if (plyr_wrk.cmn_wrk.floor != sis_wrk.cmn_wrk.floor)
    {
        return;
    }
    if (DistHitCheck(sis_wrk.cmn_wrk.mbox.pos, plyr_wrk.cmn_wrk.mbox.pos, 140.0f) == 0)
    {
        return;
    }

    /* Her position offset by his movement gives the line he is crossing. */
    sceVu0SubVector(vt, plyr_wrk.cmn_wrk.mbox.pos, plyr_wrk.cmn_wrk.mbox.bpos);
    sceVu0AddVector(vt, sis_wrk.cmn_wrk.mbox.pos, vt);

    pt.x = plyr_wrk.cmn_wrk.mbox.bpos[0];
    pt.y = plyr_wrk.cmn_wrk.mbox.bpos[2];
    lt.a.x = sis_wrk.cmn_wrk.mbox.pos[0];
    lt.a.y = sis_wrk.cmn_wrk.mbox.pos[2];
    lt.b.x = vt[0];
    lt.b.y = vt[2];

    int dir = LineSide(&pt, &lt);
    if (dir != 0)
    {
        wpos[0] = plyr_wrk.cmn_wrk.mbox.bpos[0] - sis_wrk.cmn_wrk.mbox.pos[0];
        wpos[2] = plyr_wrk.cmn_wrk.mbox.bpos[2] - sis_wrk.cmn_wrk.mbox.pos[2];
        wpos[1] = 0.0f;
        wpos[3] = 1.0f;

        float radius = GetDist(wpos[0], wpos[2]);
        float moved  = GetDistV(plyr_wrk.cmn_wrk.mbox.pos, plyr_wrk.cmn_wrk.mbox.bpos);

        rot = (moved * 0.5f) / radius;
        RotLimitChk(&rot);
        if (dir < 1)
        {
            rot = -rot;
        }

        sceVu0UnitMatrix(wlm);
        sceVu0RotMatrixY(wlm, wlm, rot);
        sceVu0TransMatrix(wlm, wlm, sis_wrk.cmn_wrk.mbox.pos);
        sceVu0ApplyMatrix(wpos, wlm, wpos);

        if (MapHitCheck(wpos, wpos, plyr_wrk.cmn_wrk.mbox.bpos, plyr_wrk.hit_rad,
                        (short)plyr_wrk.cmn_wrk.floor) == 0)
        {
            g3dxVu0CopyVector(plyr_wrk.cmn_wrk.mbox.pos, wpos);
            return;
        }
    }

    /* No room to slide -- rewind him instead. */
    g3dxVu0CopyVector(plyr_wrk.cmn_wrk.mbox.pos, plyr_wrk.cmn_wrk.mbox.bpos);
}                                                                       /* 2341 */

void SetSisterPos(float *pos)                                           /* 2345 */
{
    float vw[4];

    sis_wrk.stop_tm = 0;
    sis_wrk.run_tm  = 0;
    sis_wrk.walk_tm = 0;
    sis_wrk.cmn_wrk.st.mvsta &= ~0x00810000ULL;

    /* Carry her head target along with the body so the neck does not snap. */
    sceVu0SubVector(vw, pos, sis_wrk.cmn_wrk.mbox.pos);
    sceVu0AddVector(sis_wrk.cmn_wrk.headpos, sis_wrk.cmn_wrk.headpos, vw);

    g3dxVu0CopyVector(sis_wrk.cmn_wrk.mbox.pos, pos);
    g3dxVu0CopyVector(sis_wrk.cmn_wrk.mbox.bpos, pos);

    SetSisterHeight();
    InitSisterTracePos();                                               /* 2368 */
}

void SetSisterRot(float *rot)                                           /* 2372 */
{
    g3dxVu0CopyVector(sis_wrk.cmn_wrk.mbox.rot, rot);
    RotLimitChk(&sis_wrk.cmn_wrk.mbox.rot[1]);                          /* 2374 */
}

void SetSisterRotY(float rot)                                           /* 2377 */
{
    sis_wrk.cmn_wrk.mbox.rot[1] = rot;
    RotLimitChk(&sis_wrk.cmn_wrk.mbox.rot[1]);                          /* 2379 */
}

void SetSisterFloorPL(void)
{
    sis_wrk.cmn_wrk.floor = plyr_wrk.cmn_wrk.floor;                     /* 2383 */
}

/* ==========================================================================
 *  Damage
 *
 *  cmn_wrk.mode is the reaction state: 0 free, 1 grabbed, 2 held, 3 draining,
 *  4 released, 9 dead.  atk_pos selects front/back and atk_rot the approach
 *  quadrant; the three little tables map those onto animation numbers.
 * ======================================================================== */

void SisterDamageCtrl(void)                                             /* 2388 */
{
    /* {grab-in} by approach quadrant and side. */
    u_char sani_dmgin_tbl[3][2] = { { 0x1c, 0x1f }, { 0x24, 0x26 }, { 0x25, 0x27 } };
    u_char sani_dmg_tbl[2]      = { 0x1d, 0x20 };   /* being drained  */
    u_char sani_out_tbl[2]      = { 0x1e, 0x21 };   /* breaking free  */

    u_char   eneno = sis_wrk.cmn_wrk.atk_eneno;
    ENE_WRK *ew    = &ene_wrk[eneno];

    if (sis_wrk.cmn_wrk.st.hp == 0)
    {
        sis_wrk.cmn_wrk.st.dwalk_tm = 0;
        if (sis_wrk.cmn_wrk.st.dmg_type != 4)
        {
            ew->atk_tm = 0;
        }
        EndPlyrApproachCameraCtrl();
        if (sis_wrk.cmn_wrk.mode != 9)
        {
            sis_wrk.cmn_wrk.mode = 0;
        }
        return;
    }

    if ((u_char)(sis_wrk.cmn_wrk.mode - 1) > 3)
    {
        sis_wrk.cmn_wrk.st.dmg = 0;
    }

    /* Grabbed, but the ghost is gone or has been told to let go. */
    if (sis_wrk.cmn_wrk.st.dmg_type != 4 && sis_wrk.cmn_wrk.mode == 1 &&
        (ew->st.hp == 0 || (ew->st.sta & 0x20000000000ULL) != 0))
    {
        sis_wrk.cmn_wrk.mode = 0;
        SetSisEscape();
        return;
    }

    switch (sis_wrk.cmn_wrk.mode)
    {
    case 1:
        sis_wrk.cmn_wrk.mode = 2;
        /* fall through */
    case 2:
        if (sis_wrk.cmn_wrk.st.dmg_type != 4)
        {
            if ((ew->attr & 0x2000) != 0)
            {
                /* This ghost's grab is an instant kill. */
                SetOpenCondSwitch(0);
                ReqSisDead(4);
                ew->atk_tm = 0;
                if (sis_wrk.cmn_wrk.st.dmg_type == 3)
                {
                    ReqAnm(ew->ani_ctrl_p, 10, ew->dat->cmn.anm_no, 9);
                }
                return;
            }
            if (ew->status != ENE_STATUS_ACT)
            {
                sis_wrk.cmn_wrk.mode = 4;
                return;
            }
        }

        if (sis_wrk.cmn_wrk.st.dmg_type == 3)
        {
            u_char want = sani_dmgin_tbl[sis_wrk.cmn_wrk.atk_rot]
                                        [sis_wrk.cmn_wrk.atk_pos];
            if (sis_wrk.anime_no != want)
            {
                ew->atk_tm = 0;
                break;                  /* -> escape */
            }
            if (CheckSisAnimeEnd(want) == 0)
            {
                return;
            }

            sis_wrk.cmn_wrk.st.sta |= 0x20000;
            ReqPlyrHPdown(&sis_wrk.cmn_wrk, sis_wrk.cmn_wrk.st.dmg);
            ReqPlyrSPdown(&plyr_wrk.cmn_wrk, sis_wrk.cmn_wrk.st.dmg);
            sis_wrk.cmn_wrk.mode = 3;
            SetSisterAnime(sani_dmg_tbl[sis_wrk.cmn_wrk.atk_pos], 1);

            ReqSisBankPlay(SisRandPercent() < 20.0f ? 7 : 2, 1, 1, 0, &sis_wrk.s3d);
            sis_wrk.cmn_wrk.st.dmg = 0;
            sis_wrk.dmg_se_cnt = 0;
            return;
        }

        if (sis_wrk.cmn_wrk.st.dmg_type == 0 ||
            (sis_wrk.cmn_wrk.st.dmg_type > 4))
        {
            return;
        }
        ReqPlyrHPdown(&sis_wrk.cmn_wrk, sis_wrk.cmn_wrk.st.dmg);
        ReqPlyrSPdown(&plyr_wrk.cmn_wrk, sis_wrk.cmn_wrk.st.dmg);
        sis_wrk.cmn_wrk.st.dmg = 0;
        sis_wrk.cmn_wrk.mode = 4;
        ReqSisBankPlay(1, 1, 1, 0, &sis_wrk.s3d);
        return;

    case 3:
        if (sis_wrk.cmn_wrk.st.dmg_type != 4 && ew->status != ENE_STATUS_ACT)
        {
            sis_wrk.cmn_wrk.mode = 4;
            return;
        }

        if (sis_wrk.cmn_wrk.st.dmg_type != 3)
        {
            if (sis_wrk.cmn_wrk.st.dmg_type == 0)
            {
                return;
            }
            if (sis_wrk.cmn_wrk.st.dmg_type > 4)
            {
                return;
            }
            sis_wrk.cmn_wrk.mode = 4;
            return;
        }

        if (sis_wrk.anime_no != sani_dmg_tbl[sis_wrk.cmn_wrk.atk_pos])
        {
            ew->atk_tm = 0;
            break;                      /* -> escape */
        }

        /* Still being drained: one tick of HP per attack frame, plus a cry
         * every fifth animation loop. */
        ReqPlyrHPdown(&sis_wrk.cmn_wrk, ew->dat->atk);
        if ((ew->st.sta & 0x8000ULL) != 0 && ew->atk_tm != 0)
        {
            if ((sis_wrk.cmn_wrk.st.sta & 0x4000) != 0 && ++sis_wrk.dmg_se_cnt == 5)
            {
                ReqSisBankPlay(6, 1, 1, 0, &sis_wrk.s3d);
            }
        }
        if (ew->atk_tm != 0)
        {
            return;
        }

        SetSisterAnime(sani_out_tbl[sis_wrk.cmn_wrk.atk_pos], 1);
        sis_wrk.cmn_wrk.mode = 4;
        if (SisRandPercent() < 50.0f)
        {
            ReqSisBankPlay(10, 1, 1, 0, &sis_wrk.s3d);
        }
        return;

    case 4:
        if (sis_wrk.cmn_wrk.st.dmg_type == 2)
        {
            break;                      /* -> escape */
        }
        if (sis_wrk.cmn_wrk.st.dmg_type == 3)
        {
            u_char out = sani_out_tbl[sis_wrk.cmn_wrk.atk_pos];
            if (sis_wrk.anime_no == out && CheckSisAnimeEnd(out) == 0)
            {
                return;
            }
            break;                      /* -> escape */
        }
        if (sis_wrk.cmn_wrk.st.dmg_type == 1)
        {
            /* falls into the shared knock-out test below */
        }
        else if (sis_wrk.cmn_wrk.st.dmg_type != 4)
        {
            return;
        }
        if ((u_char)(sis_wrk.anime_no - 0x22) < 2 &&
            (sis_wrk.cmn_wrk.st.sta & 0x2000) == 0)
        {
            return;                     /* knock-out still playing */
        }
        break;                          /* -> escape */

    default:
        return;
    }

    /* Shared exit: back on her feet and looking for a way back to him. */
    sis_wrk.cmn_wrk.mode = 0;
    SetSisterAnime(0, 10);
    SetSisEscape();                                                     /* 2590 */
}

/* Animation 0x2b is the only one that must not have the neck controller
 * fighting it. */
int CheckSisterNeckSW(u_char anime_no)
{
    return anime_no != 0x2b;                                            /* 2609 */
}

void SetSisterAnime(u_char anime_no, u_char frame)                      /* 2615 */
{
    /* Requesting the idle animation while already idle is a no-op -- it would
     * otherwise restart the loop every frame. */
    if (anime_no == 0 && sis_wrk.anime_no == 0)
    {
        return;
    }

    if (anime_no != 1)
    {
        ClearSisWait();
    }

    sis_wrk.cmn_wrk.st.sta &= ~0x6000ULL;       /* clear anim loop/end flags */
    SetSisNeckFlg(CheckSisterNeckSW(anime_no));
    sis_wrk.anime_no = anime_no;
    ReqSisterAnime(frame);                                              /* 2628 */
}

/* Picks the walk/run/stairs animation from mvsta.  psta_chk_tbl[] is scanned
 * in priority order and its index selects a pair from pmani_no_tbl[]; the
 * second of the pair is used when the model's ftype says she is holding the
 * camera. */
void SisterNAnimeCtrl(void)                                             /* 2632 */
{
    u_int psta_chk_tbl[14] =
    {
        0x10, 0x20, 0x40, 0x80, 1, 2, 2, 8, 4, 0x800000,
        0x100, 0x200, 0x400, 0x800
    };
    u_char pmani_no_tbl[14][2] =
    {
        { 0x16, 0x17 }, { 0x18, 0x19 }, { 0x12, 0x13 }, { 0x14, 0x15 },
        { 0x08, 0x09 }, { 0x0a, 0x0b }, { 0x10, 0x11 }, { 0x0e, 0x0f },
        { 0x0c, 0x0d }, { 0x0a, 0x0b }, { 0x0a, 0x0b }, { 0x1b, 0x1b },
        { 0x1b, 0x1b }, { 0x1b, 0x1b }
    };

    u_char frame = 10;
    u_char anime_no;
    u_int  i = 7;

    if (sis_wrk.cmn_wrk.mode == 9)
    {
        return;
    }

    anime_no = sis_wrk.anime_no;

    if ((sis_wrk.cmn_wrk.st.mvsta & 0x1f00) == 0)
    {
        if ((sis_wrk.cmn_wrk.st.mvsta & 0x801fff) == 0 ||
            (sis_wrk.cmn_wrk.st.mvsta & 0x61a8000) != 0)
        {
            /* Not moving: idle, or the wait pose. */
            anime_no = (sis_wrk.cmn_wrk.st.mvsta & 0x10000) != 0;
            if ((sis_wrk.cmn_wrk.st.mvsta & 0x2000000) != 0)
            {
                anime_no = sis_wrk.anime_no;
            }
        }
        else
        {
            if (sis_wrk.cmn_wrk.st.dwalk_tm == 0 &&
                (sis_wrk.cmn_wrk.st.mvsta & 0x10) == 0)
            {
                for (i = 0; ; )
                {
                    i = (i + 1) & 0xff;
                    if (i > 0xd)
                    {
                        break;
                    }
                    if ((sis_wrk.cmn_wrk.st.mvsta & psta_chk_tbl[i]) != 0)
                    {
                        if ((psta_chk_tbl[i] & 0x1f00) != 0)
                        {
                            frame = 2;  /* push-aside snaps in faster */
                        }
                        break;
                    }
                }
            }

            /* Outdoors there is no walk clip for entry 5, so slide to 6. */
            if (PlyrOutsideCheck() != 0 && psta_chk_tbl[i] == 2)
            {
                i = (i + 1) & 0xff;
            }

            if ((pmani_no_tbl[i][0] != sis_wrk.anime_no &&
                 pmani_no_tbl[i][1] != sis_wrk.anime_no) ||
                (sis_wrk.cmn_wrk.st.sta & 0x2000) != 0)
            {
                int col = 0;
                if (i < 0xe && GetSisterFtype() == 1)
                {
                    col = 1;
                }
                anime_no = i < 0xe ? pmani_no_tbl[i][col] : sis_wrk.anime_no;
            }
            else
            {
                anime_no = sis_wrk.anime_no;
            }
        }
    }

    /* Limping replaces the idle. */
    if (anime_no == 0 && sis_wrk.cmn_wrk.st.dwalk_tm != 0)
    {
        anime_no = 5;
    }

    if (sis_wrk.anime_no != anime_no)
    {
        SetSisterAnime(anime_no, frame);
    }                                                                   /* 2775 */
}

/* ==========================================================================
 *  Trace-point search
 *
 *  The routing graph lives in sis_trpoint.c, one node list plus one
 *  connection matrix per room.  Node indices are 1-based inside the matrix
 *  (row/column 0 is unused padding), which is why everything below works in
 *  "+1" space and subtracts one again to index the position list.
 * ======================================================================== */

/* Picks the node nearest her that she can actually see, and the node nearest
 * the target that the target can see.  Returns non-zero only when both were
 * found. */
int SearchSearchPoint(float *pos, int floor1, float *tgt, int floor2)   /* 2781 */
{
    if (sis_wrk.cmn_wrk.pr_info.area_no >= 0x42)
    {
        return 0;
    }

    int area = sis_wrk.cmn_wrk.pr_info.area_no;
    if (room_point_num[area] == 0)
    {
        return 0;
    }

    sceVu0FVECTOR *p1 = room_point_pos[area];
    float l1 = 9999.0f;
    float l2 = 9999.0f;
    int   i;

    sis_search.start = -1;
    sis_search.end   = -1;

    for (i = 0; i < room_point_num[area]; i++, p1++)
    {
        float lw = GetDistV2(pos, *p1);
        if (lw < l1 && SisterHitCheck(pos, floor1, *p1, (int)(*p1)[3]) == 0)
        {
            sis_search.start = (char)i;
            l1 = lw;
        }

        lw = GetDistV2(tgt, *p1);
        if (lw < l2 && SisterHitCheck(tgt, floor2, *p1, (int)(*p1)[3]) == 0)
        {
            sis_search.end = (char)i;
            l2 = lw;
        }
    }

    if (sis_search.start == -1)
    {
        printf("nearest-point not found[p1]\n");
    }
    if (sis_search.end == -1)
    {
        printf("nearest-point not found[p2]\n");
        return 0;
    }

    return sis_search.start != -1;                                      /* 2821 */
}

/* Dijkstra from sis_search.end back to sis_search.start over the room's
 * connection matrix, writing the node chain into `route` terminated by -1. */
int SearchTraceRoute(char *route)                                       /* 2866 */
{
    if (sis_wrk.cmn_wrk.pr_info.area_no >= 0x42)
    {
        return 0;
    }

    int area = sis_wrk.cmn_wrk.pr_info.area_no;
    int leng[32];
    int v[32];
    int index[32];
    int i;
    int j;

    int n = room_point_num[area];
    if (n > 0x20)
    {
        n = 0x20;
        printf("TracePoint Over!!\n");
    }

    int start = sis_search.start + 1;
    int end   = sis_search.end + 1;
    printf("start-p:%d  end-p:%d\n", start, end);

    for (i = 1; i <= n; i++)
    {
        leng[i] = 9999;
        v[i] = 0;
    }
    leng[end]  = 0;
    index[end] = 0;

    for (j = 1; j <= n; j++)
    {
        int min = 9999;
        int p   = 0;

        for (i = 1; i <= n; i++)
        {
            if (v[i] == 0 && leng[i] < min)
            {
                p   = i;
                min = leng[i];
            }
        }
        v[p] = 1;

        if (min == 9999)
        {
            printf("connection matrix error!!\n");
            return 0;
        }

        /* The matrix is (n+1) square with an unused row/column 0. */
        int *row = room_connect[area] + p * (n + 1);
        for (i = 1; i <= n; i++)
        {
            if (leng[p] + row[i] < leng[i])
            {
                leng[i]  = leng[p] + row[i];
                index[i] = p;
            }
        }
    }

    /* Walk the predecessor chain back out. */
    route[0] = (char)start;
    i = 1;
    j = start;
    while (index[j] != 0)
    {
        j = index[j];
        route[i++] = (char)j;
    }
    route[i] = -1;

    printf("[base route]------");
    for (i = 0; route[i] != -1; i++)
    {
        printf("->%d", route[i]);
    }
    printf("\n");

    return 1;                                                           /* 2933 */
}

/* String-pulls `route` -- drops every node she can see past -- and rewrites
 * sis_trace's ring with the surviving positions. */
void SetSearchPoint(char *route, int floor1, int floor2)                /* 2944 */
{
    float vw[4];
    int   mroute[64];
    int   i;
    int   num = 0;
    int   nw  = 0;

    int area = sis_wrk.cmn_wrk.pr_info.area_no;
    sceVu0FVECTOR *pointpos = room_point_pos[area];

    sis_trace.num = 0;
    sis_trace.top = 0;
    sis_trace.now = 0;

    for (i = 0; route[i] != -1; i++)
    {
        num++;
    }

    /* Entry 0 is where she is standing. */
    g3dxVu0CopyVector(sis_trace.p[0], sis_wrk.cmn_wrk.mbox.pos);
    MhCtlGetMapHeight(vw, sis_trace.p[0], area, 0);
    sis_trace.p[0][1] = vw[1];
    sis_trace.num++;

    mroute[0] = 0;
    int j = sis_trace.now;

    /* Walk in from the far end of the route to the furthest node she can
     * already see; that becomes the first waypoint. */
    int ne = num - 1;
    for (i = num - 1; i >= 0; i--)
    {
        if (SisterHitCheck(sis_wrk.cmn_wrk.headpos, floor1,
                           pointpos[route[i] - 1],
                           (int)pointpos[route[i] - 1][3]) == 0)
        {
            nw = 1;
            mroute[0] = i;
            ne = i;
            break;
        }
    }

    /* Walk out from the near end to the first node the target can see; that
     * is where the route can stop. */
    int nend = num - 1;
    for (i = ne; i < num; i++)
    {
        if (SisterHitCheck(sis_trace.trgt, floor2, pointpos[route[i] - 1],
                           (int)pointpos[route[i] - 1][3]) == 0)
        {
            nend = i;
            break;
        }
    }

    /* Between those two, keep only the nodes a straight line cannot skip. */
    i = ne + 1;
    while (i <= nend)
    {
        int found = 0;
        int k;

        for (k = nend; k > ne; k--)
        {
            if (SisterHitCheck(pointpos[route[ne] - 1],
                               (int)pointpos[route[ne] - 1][3],
                               pointpos[route[k] - 1],
                               (int)pointpos[route[k] - 1][3]) == 0)
            {
                if (k != nend)
                {
                    mroute[nw++] = k;
                }
                found = 1;
                ne = k;
                i  = k + 1;
                break;
            }
        }

        if (found == 0)
        {
            if (i != nend)
            {
                mroute[nw++] = i;
            }
            ne = i;
            i++;
        }
    }
    mroute[nw++] = nend;

    printf("[short cut route]-");
    for (i = 0; i < nw; i++)
    {
        printf("->%d", route[mroute[i]]);

        int prev = sis_trace.top;
        sis_trace.top = prev + 1;

        g3dxVu0CopyVector(sis_trace.p[sis_trace.top],
                         pointpos[route[mroute[i]] - 1]);
        MhCtlGetMapHeight(vw, sis_trace.p[sis_trace.top], area, 0);
        sis_trace.p[sis_trace.top][1] = vw[1];
        sis_trace.l[sis_trace.top] = GetDistV(sis_trace.p[sis_trace.top],
                                              sis_trace.p[j]);
        sis_trace.num++;
        j = sis_trace.top;
    }
    printf("\n");

    /* Finally the target itself. */
    {
        int prev = sis_trace.top;
        sis_trace.top = prev + 1;

        g3dxVu0CopyVector(sis_trace.p[sis_trace.top], sis_trace.trgt);
        MhCtlGetMapHeight(vw, sis_trace.p[sis_trace.top], area, 0);
        sis_trace.p[sis_trace.top][1] = vw[1];
        sis_trace.l[sis_trace.top] = GetDistV(sis_trace.p[sis_trace.top],
                                              sis_trace.p[j]);
        sis_trace.num++;
    }

    sis_trace.dist = 0.0f;
    for (i = 1; i < sis_trace.num; i++)
    {
        sis_trace.dist += sis_trace.l[i];
    }                                                                   /* 3040 */
}

/* Recovery: try a straight line first, then the routing graph, and give up
 * into the cowering state if neither works. */
static int SearchMain(void)                                             /* 3167 */
{
    char route[255];

    printf("recovery search\n");

    g3dxVu0CopyVector(sis_trace.trgt, plyr_wrk.cmn_wrk.headpos);
    sis_trace.trgt_floor = (short)plyr_wrk.cmn_wrk.floor;
    sis_wrk.cmn_wrk.st.mvsta &= ~0x100000ULL;

    if (SisterHitCheck(sis_wrk.cmn_wrk.headpos, (short)sis_wrk.cmn_wrk.floor,
                       plyr_wrk.cmn_wrk.headpos, (short)plyr_wrk.cmn_wrk.floor) == 0)
    {
        sis_trace.num = 2;
        sis_trace.top = (sis_trace.now + 1) % 64;
        CalcSisDist();
        SisterTracePlayer();
        sis_algo.amode = 0;
        printf(">direct trace\n");
        return 1;
    }

    if (SearchSearchPoint(sis_wrk.cmn_wrk.headpos, (short)sis_wrk.cmn_wrk.floor,
                          plyr_wrk.cmn_wrk.headpos,
                          (short)plyr_wrk.cmn_wrk.floor) == 0)
    {
        sis_algo.amode = 5;
        sis_wrk.cmn_wrk.st.mvsta |= 0x100000;
        printf(">search failure\n");
        return 0;
    }

    printf(">trace-point using search\n");
    SearchTraceRoute(route);
    SetSearchPoint(route, (short)sis_wrk.cmn_wrk.floor,
                   (short)plyr_wrk.cmn_wrk.floor);
    SisterTracePlayer();
    sis_algo.amode = 0;
    return 1;                                                           /* 3202 */
}

void SetSearchMode(void)                                                /* 3207 */
{
    if (SearchMain() == 0)
    {
        sis_wrk.cmn_wrk.st.mvsta |= 0x80000;
    }
    else
    {
        sis_wrk.cmn_wrk.st.mvsta &= ~0x80000ULL;
    }                                                                   /* 3210 */
}

/* Breaking off a battle.  Chapters 9 and 19 keep her in place during their
 * scripted missions rather than letting her run. */
void SetSisEscape(void)                                                 /* 3215 */
{
    int run = 1;

    if (CheckIngameMission() != 0 && (ingame_wrk.mChapterNo == 9 || ingame_wrk.mChapterNo == 19))
    {
        run = 0;
    }

    if (run)
    {
        if (SearchMain() == 0)
        {
            return;
        }
        sis_wrk.btl_recv_tm = 0x3c;     /* one second before she can re-enter */
    }

    sis_wrk.cmn_wrk.st.mvsta &= ~0x061a8000ULL;                         /* 3234 */
}

/* Stubbed out in the prototype -- the ROM body is a bare `jr ra`. */
void SetFindMode(float *tgt, int floor)                                 /* 3258 */
{
    (void)tgt; (void)floor;
}

/* ==========================================================================
 *  Scripted motion
 * ======================================================================== */

void ReqModeSisMotion(u_int tbl_no)                                     /* 3060 */
{
    if (tbl_no > 9)
    {
        printf("Sis Motion Req Over!!\n");
    }

    sis_motion.sat      = sis_ani_tbl[tbl_no];
    sis_motion.now_tbl  = 0;
    sis_motion.mot_loop = 0;
    sis_motion.mot_end  = 0;

    if (sis_motion.sat->ani_no == 0xfe)
    {
        /* 0xfe means "turn to face the target first" -- collapse the trail so
         * GetSisterRot() aims straight at it. */
        sis_trace.num = 2;
        SetSisterAnime(10, sis_motion.sat->frm);
    }
    else
    {
        SetSisterAnime(sis_motion.sat->ani_no, sis_motion.sat->frm);
    }

    sis_algo.amode = 6;
    ClearSisterPushMove();                                              /* 3066 */
}

void ModeSisMotion(void)                                                /* 3071 */
{
    SisterNoMove();

    if (sis_motion.mot_end != 0)
    {
        return;
    }

    SIS_ANI_TBL *sat = &sis_motion.sat[sis_motion.now_tbl];

    if (sat->ani_no == 0xfe)
    {
        /* Turn step: advance once she is facing the target. */
        if (SetSisTurn() == 0)
        {
            return;
        }

        sis_motion.now_tbl++;
        u_char next = sis_motion.sat[sis_motion.now_tbl].ani_no;
        if (next == 0xff)
        {
            sis_motion.mot_end = 1;
            SetSearchMode();
            return;
        }
        SetSisterAnime(next, sis_motion.sat[sis_motion.now_tbl].frm);
        sis_motion.mot_loop = 0;
        return;
    }

    if ((sis_wrk.cmn_wrk.st.sta & 0x6000) == 0)
    {
        return;                         /* animation still running */
    }

    if (sat->loop != -1)
    {
        sis_motion.mot_loop++;
        if (sat->loop <= (short)sis_motion.mot_loop)
        {
            sis_motion.now_tbl++;
            u_char next = sis_motion.sat[sis_motion.now_tbl].ani_no;
            if (next != 0xff)
            {
                SetSisterAnime(next, sis_motion.sat[sis_motion.now_tbl].frm);
                sis_motion.mot_loop = 0;
                return;
            }
            sis_motion.mot_end = 1;
            SetSearchMode();
            return;
        }
    }

    if ((sis_wrk.cmn_wrk.st.sta & 0x2000) != 0)
    {
        SetSisterAnime(sat->ani_no, sat->frm);      /* replay the loop */
        return;
    }

    if ((sis_wrk.cmn_wrk.st.sta & 0x4000) != 0)
    {
        sis_wrk.cmn_wrk.st.sta &= ~0x4000ULL;
    }                                                                   /* 3122 */
}

/* ==========================================================================
 *  Cross-area teleport
 * ======================================================================== */

/* When the player crosses between two listed areas and she has fallen more
 * than 5800 units behind, put her down at the listed position instead of
 * making her walk the whole way. */
void ChangeSisterExPos(int old, int now)                                /* 3871 */
{
    float pos[4];
    int   flag = 0;

    float dist = GetDistV(plyr_wrk.cmn_wrk.mbox.pos, sis_wrk.cmn_wrk.mbox.pos);

    if (IsSisWrk() == 0 || dist < 5800.0f)
    {
        return;
    }

    _ClearVector(pos);

    SIS_AREA_CHG *sac;
    for (sac = sis_area_chg; sac->old != -1 && !flag; sac++)
    {
        if (sac->old != old)
        {
            continue;
        }

        SIS_AREA_CHG_SUB *sacs;
        for (sacs = sac->target; sacs->now != -1 && !flag; sacs++)
        {
            if (sacs->now == now)
            {
                g3dxVu0CopyVector(pos, sacs->pos);
                flag = 1;
            }
        }
    }

    if (flag)
    {
        SetSisterPos(pos);
        ChangeForceTraceMode();
        SetSearchMode();
    }                                                                   /* 3910 */
}

/* ==========================================================================
 *  Debug draw (all gated on debug_var.sis_tr_point)
 * ======================================================================== */

/* Flat ring of `rd` units around mpos, drawn as a GS triangle fan straight
 * into the 2D packet buffer.  The whole fan is rejected if the centre vertex
 * lands outside the guard band or behind the near plane. */
void DrawHitLCircle(float *mpos, u_char r, u_char g, u_char b, float rd)  /* 1650 */
{
    float ncf[38][4];
    sceVu0IVECTOR nci[38];
    float wpos[4];
    float slm1[4][4];
    float wlm[4][4];
    DRAW_ENV_NOTEX env;

    const float pai = 3.1415927410125732f;
    float f = 0.0f;
    int   i = 1;

    g3dxVu0CopyVector(wpos, mpos);

    sceVu0UnitMatrix(wlm);
    sceVu0RotMatrixX(wlm, wlm, 1.5707963705062866f);    /* lay it flat */
    sceVu0TransMatrix(wlm, wlm, wpos);
    sceVu0MulMatrix(slm1, gra3dGetCamera()->matWorldScreen, wlm);

    ncf[0][0] = 0.0f;
    ncf[0][1] = 0.0f;
    ncf[0][2] = 0.0f;
    ncf[0][3] = 1.0f;

    do
    {
        float rad = (f * pai) / 180.0f;
        ncf[i][0] = rd * cosf(rad);
        ncf[i][1] = rd * sinf(rad);
        ncf[i][2] = 0.0f;
        ncf[i][3] = 1.0f;
        i++;
        f += 10.0f;
    } while (f < 360.0f);

    /* Close the fan back onto the first rim vertex. */
    g3dxVu0CopyVector(ncf[i], ncf[1]);

    u_int n = (u_int)(i + 1);
    sceVu0RotTransPersN(nci, slm1, ncf, n, 0);

    int reject = 0;
    if ((u_int)nci[0][0] < 0x5a80 || (u_int)nci[0][0] > 0xa580) reject = 1;
    if ((u_int)nci[0][1] < 0x6700 || (u_int)nci[0][1] > 0x9900) reject = 1;
    if (nci[0][2] == 0 || (u_int)nci[0][2] > 0xfffff)           reject = 1;

    if (reject)
    {
        return;
    }

    env.alpha = 0x84;
    env.test  = 0x5000d;
    env.zbuf  = 0xa000118;
    SetDrawEnvNoTex(0, &env);

    Q_WORDDATA *pk = GetPK2Dbuf();
    int qwc = 3;

    /* GIF tag: 1 reg (A+D), 1 loop -- the RGBAQ / PRIM pair. */
    pk[0].ul64[0] = 0x1022c00000008001ULL;
    pk[0].ul64[1] = 1;
    pk[1].ui32[0] = r;
    pk[1].ui32[1] = g;
    pk[1].ui32[2] = b;
    pk[1].ui32[3] = 0x80;
    pk[1].ul64[1] = 4;                  /* RGBAQ */

    /* Second tag: n XYZ2 vertices as a triangle fan. */
    pk[2].ul64[0] = (u_long)n | 0x1021400000008000ULL;

    Q_WORDDATA *dst = &pk[3];
    u_int k;
    for (k = 0; k < n; k++, dst++)
    {
        dst->ui32[0] = nci[k][0];
        dst->ui32[1] = nci[k][1];
        dst->ui32[2] = nci[k][2];
        /* The first two vertices are kicked without drawing -- a fan needs
         * three before the first triangle exists. */
        dst->ui32[3] = k < 2 ? 0x8000 : 0;
        qwc++;
    }

    EndPK2Dbuf(pk + qwc);                                               /* 1734 */
}

/* Draws her current state as a coloured disc plus the recorded trail, with
 * self-crossing segments highlighted. */
void DrawSisDummy(void)                                                 /* 3470 */
{
    LINE_T l1;
    LINE_T l2;
    float  wpos[4];
    float  wpos2[4];
    int    i;
    int    j;

    const float qpai = 1.5707963705062866f;
    float rot = sis_wrk.cmn_wrk.mbox.rot[1] - qpai;

    if ((sis_wrk.cmn_wrk.st.mvsta & 0x61a8000) != 0)
    {
        DrawHitCircle(sis_wrk.cmn_wrk.mbox.pos, rot, 0, 9, 0x80, 70.0f);
    }
    else if ((sis_wrk.cmn_wrk.st.mvsta & 0x10000) != 0)
    {
        DrawHitCircle(sis_wrk.cmn_wrk.mbox.pos, rot, 0, 11, 0x80, 70.0f);
    }
    else if (sis_wrk.cmn_wrk.st.mvsta == 0x400000)
    {
        DrawHitCircle(sis_wrk.cmn_wrk.mbox.pos, rot, 0, 2, 0x80, 70.0f);
    }
    else if (sis_wrk.cmn_wrk.st.mvsta == 0x200000)
    {
        DrawHitCircle(sis_wrk.cmn_wrk.mbox.pos, rot, 0, 4, 0x80, 70.0f);
    }
    else if (sis_wrk.cmn_wrk.st.mvsta == 8)
    {
        DrawHitCircle(sis_wrk.cmn_wrk.mbox.pos, rot, 0, 1, 0x80, 70.0f);
    }
    else if (sis_wrk.cmn_wrk.st.mvsta == 1 ||
             sis_wrk.cmn_wrk.st.mvsta == 2 ||
             sis_wrk.cmn_wrk.st.mvsta == 0x800000)
    {
        DrawHitCircle(sis_wrk.cmn_wrk.mbox.pos, rot, 0, 3, 0x80, 70.0f);
    }
    else
    {
        DrawHitCircle(sis_wrk.cmn_wrk.mbox.pos, rot, 0, 0, 0x80, 70.0f);
    }

    /* Mark every trail segment that crosses another one on the same storey. */
    for (i = 0; i < sis_trace.num - 1; i++)
    {
        int a = (sis_trace.now + i) % 64;
        int b = (sis_trace.now + i + 1) % 64;

        l1.a.x = sis_trace.p[a][0];
        l1.a.y = sis_trace.p[a][2];
        l1.b.x = sis_trace.p[b][0];
        l1.b.y = sis_trace.p[b][2];

        for (j = 0; j < i - 1; j++)
        {
            int c = (sis_trace.now + j) % 64;
            int d = (sis_trace.now + j + 1) % 64;

            l2.a.x = sis_trace.p[c][0];
            l2.a.y = sis_trace.p[c][2];
            l2.b.x = sis_trace.p[d][0];
            l2.b.y = sis_trace.p[d][2];

            if (LineIntersect(&l1, &l2) != 0 &&
                fabs((double)(sis_trace.p[c][1] - sis_trace.p[d][1])) < 1000.0)
            {
                sis_trace.fl[a] = 1;
            }
        }

        for (j = i + 2; j < sis_trace.num - 1; j++)
        {
            int c = (sis_trace.now + j) % 64;
            int d = (sis_trace.now + j + 1) % 64;

            l2.a.x = sis_trace.p[c][0];
            l2.a.y = sis_trace.p[c][2];
            l2.b.x = sis_trace.p[d][0];
            l2.b.y = sis_trace.p[d][2];

            if (LineIntersect(&l1, &l2) != 0 &&
                fabs((double)(sis_trace.p[c][1] - sis_trace.p[d][1])) < 1000.0)
            {
                sis_trace.fl[a] = 1;
            }
        }
    }

    /* Then draw it, 210 units down so the line sits on the floor. */
    for (i = 0; i < sis_trace.num - 1; i++)
    {
        int a = (sis_trace.now + i) % 64;
        int b = (sis_trace.now + i + 1) % 64;

        g3dxVu0CopyVector(wpos,  sis_trace.p[a]);
        g3dxVu0CopyVector(wpos2, sis_trace.p[b]);
        wpos[1]  -= 210.0f;
        wpos2[1] -= 210.0f;

        if (sis_trace.fl[a] == 0)
        {
            DrawLine(wpos, 0x80, 0x80, 0x80, 0x40, wpos2, 0x80, 0x80, 0x80, 0x40);
        }
        else
        {
            DrawLine(wpos, 0xff, 0x40, 0x40, 0x80, wpos2, 0xff, 0x40, 0x40, 0x80);
        }
    }                                                                   /* 3617 */
}

/* Trace-point overlay.  R2 (key_now[11]) and L2 (key_now[9]) fire the two
 * scripted motions; holding L1 (key_now[8]) draws the visibility lines from
 * both characters to every node in her room.  The index-to-button mapping is
 * the one debug.c and debug_menu.c document. */
void SisterDebug(void)                                                  /* 3623 */
{
    int i;

    if (debug_var.sis_tr_point == 0)
    {
        return;
    }

    /* TEMP PROBE -- strip.  room_point_num[] is legitimately 0 in most rooms,
     * which would make an empty overlay correct rather than broken. */
    {
        static int dbg_sd;
        if (dbg_sd < 10)
        {
            dbg_sd++;
            printf("[PROBE %s] plyr_area=%d npt=%d sis_area=%d trace_num=%d\n",
                   __func__, plyr_wrk.cmn_wrk.pr_info.area_no,
                   room_point_num[plyr_wrk.cmn_wrk.pr_info.area_no],
                   sis_wrk.cmn_wrk.pr_info.area_no, sis_trace.num);
        }
    }

    for (i = 0; i < room_point_num[plyr_wrk.cmn_wrk.pr_info.area_no]; i++)
    {
        DrawCrossLine(room_point_pos[plyr_wrk.cmn_wrk.pr_info.area_no][i]);
    }

    if (*key_now[0xb] == 1)
    {
        ReqModeSisMotion(2);
    }
    if (*key_now[9] == 1)
    {
        ReqModeSisMotion(9);
    }

    if (*key_now[8] != 0)
    {
        int area = sis_wrk.cmn_wrk.pr_info.area_no;
        sceVu0FVECTOR *v2 = room_point_pos[area];

        for (i = 0; i < room_point_num[area]; i++, v2++)
        {
            if (SisterHitCheck(sis_wrk.cmn_wrk.headpos,
                               (short)sis_wrk.cmn_wrk.floor,
                               *v2, (int)(*v2)[3]) == 0)
            {
                DrawLine(sis_wrk.cmn_wrk.headpos, 0x80, 0x80, 0xff, 0x80,
                         *v2, 0x80, 0x80, 0xff, 0x80);
            }
            else
            {
                DrawLine(sis_wrk.cmn_wrk.headpos, 0xff, 0x80, 0x80, 0x80,
                         *v2, 0xff, 0x80, 0x80, 0x80);
            }

            if (SisterHitCheck(plyr_wrk.cmn_wrk.headpos,
                               (short)plyr_wrk.cmn_wrk.floor,
                               *v2, (int)(*v2)[3]) == 0)
            {
                DrawLine(plyr_wrk.cmn_wrk.headpos, 0x80, 0x80, 0xff, 0x80,
                         *v2, 0x80, 0x80, 0xff, 0x80);
            }
            else
            {
                DrawLine(plyr_wrk.cmn_wrk.headpos, 0xff, 0x80, 0x80, 0x80,
                         *v2, 0xff, 0x80, 0x80, 0x80);
            }
        }
    }

    DrawSisDummy();                                                     /* 3655 */
}
