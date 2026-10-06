/* ==========================================================================
 *  debug/scn_test.c
 *
 *  Scene-test debug mode: the scene-select front end, the scene / movie
 *  playback drivers, and the in-scene tuning menus (free camera, per-light
 *  colour and cone, ambient banks, fog, the post-process effect bank, the two
 *  parts-deform slots, the per-enemy aura / p-deform parameters and the pad
 *  vibration test), plus the host0: light-data writers used to dump the tuned
 *  values back to the artist's PC.
 *
 *  Reconstructed from the Feb 6 2004 prototype (SLES_523.84).  All 5 ZERO2.MAP
 *  .text exports plus the 43 statics are here; scene_name[72],
 *  SceneTestDebugMenu[16] and scn_eff_ctrl[17] are read straight out of the ELF
 *  and every string literal in the object is accounted for.
 *
 *  The trailing line annotations are measured, not guessed: each one comes from
 *  the $LM label the ROM emits at that statement's call site.  About fifty
 *  statements carry none, and deliberately so -- they sit where GCC cross-jumped
 *  two identical call tails into one jal (SceneTestEffectData's two dither
 *  ScnValueCtrl arms are the clearest case, lines 1455 and 1457 sharing the call
 *  at 0x252b9c) or where a switch's blocks were laid out in an order the line
 *  numbers do not follow.  A wrong annotation is worse than none.
 *
 *  Note the file reaches the scene's FOD_LIGHT two different ways.  Most
 *  functions call SceneFodLightPtrGet(); SceneTestLoopManage, SceneTestLightSelect,
 *  SceneTestLightPosDispOnOff and SceneFileSaveBin instead call SceneCtrlGet()
 *  and take &->fod_ctrl.fod_light themselves.  That is the ROM's own split --
 *  those four emit no jal to SceneFodLightPtrGet at all -- so it is preserved.
 * ======================================================================== */

#include "scn_test.h"
#include "debug_menu.h"
#include "../common/variable.h"
#include "../common/utility2.h"         /* PRINT_ASSERT */
#include "../common/mem_util.h"
#include "../common/ol_load.h"
#include "../common/heapctrl.h"
#include "../system/os/system.h"
#include "../system/os/eecdvd.h"
#include "../system/eeiop/cddat.h"
#include "../system/pad/pad.h"
#include "../graphics/graph3d/gra3d.h"
#include "../graphics/graph3d/g3dLight.h"
#include "../graphics/graph3d/g3dxVu0.h"         /* g3dxVu0Sqrt / g3dxVu0Sqrt2 */
#include "../graphics/graph3d/ctl/fixed_array.h" /* per-TU _fixed_array_* helpers */
#include "../graphics/graph2d/message.h"
#include "../graphics/graphics.h"       /* DrawLine */
#include "../graphics/scene/scene.h"
#include "../graphics/scene/scene_effect.h"
#include "../graphics/scene/fod.h"
#include "../graphics/effect/effect.h"
#include "../graphics/movie/movie.h"
#include "../graphics/motion/accessory.h"
#include "../ingame/map/MapAnim.h"
#include "../ingame/map/MapDoor.h"
#include "../ingame/map/MapLoad.h"
#include "../ingame/map/MapDraw.h"
#include "../ingame/map/MapObjReg.h"
#include "../ingame/map/foot_se.h"
#include "../ingame/plyr/player.h"
#include "../ingame/event/prg/event.h" /* ev_seInit / ev_sisInit / ev_eneInit */
#include "../sdk/fileio.h"
#include "../sdk/libvu0.h"
#include <stdio.h>
#include <string.h>
#include <math.h>

/* --------------------------------------------------------------------------
 *  File-local types
 * ------------------------------------------------------------------------ */

typedef struct                          /* 0x1120 bytes */
{
    G3DLIGHT  light_tmp[FOD_LIGHT_MAX];         /* 0x0000 */
    SCENE_FOG fog_tmp;                          /* 0x0fc0 */
    float     amb_tmp[FOD_AMBIENT_MAX][4];      /* 0x0fe0 */
    u_int     mode;                             /* 0x1040 */
    int       select_stat;                      /* 0x1044 */
    int       scene_no;                         /* 0x1048 */
    u_int     init;                             /* 0x104c */
    u_int     load_status;                      /* 0x1050 */
    int       debug_flg;                        /* 0x1054 */
    int       main_menu_csr;                    /* 0x1058 */
    int       menu_csr;                         /* 0x105c */
    int       now_menu;                         /* 0x1060 */
    int       loop_flg;                         /* 0x1064 */
    int       light_no;                         /* 0x1068 */
    int       l_menu_csr;                       /* 0x106c */
    int       effect_id;                        /* 0x1070 */
    int       effect_csr;                       /* 0x1074 */
    int       ene_mdl_no;                       /* 0x1078 */
    u_int    *load_adrs;                        /* 0x107c */
    int       LightAllDispFlg;                  /* 0x1080 */
    int       LightDispFlg[FOD_LIGHT_MAX];      /* 0x1084 */
    int       LightDispNum;                     /* 0x1114 */
    u_int    *pMovieEffect;                     /* 0x1118 */
} SCN_TEST_WRK;
/* Offsets are the ROM's and are confirmed member-by-member up to ene_mdl_no by
 * an offsetof harness.  From load_adrs on they drift, because the two pointer
 * members are 4 bytes on target and 8 here; nothing outside this file reads the
 * block, so the drift is documentation only. */

typedef enum
{
    SCN_TEST_SLCT       = 0,
    SCN_TEST_PLAY       = 1,
    SCN_TEST_PLAY_MOVIE = 2,
    SCN_TEST_MENU       = 3
} SCN_TEST_MODE;

typedef struct                          /* 0x2c bytes */
{
    FOD_EFF_DATA  eff_data;             /* 0x00 */
    u_int         eff_flg;              /* 0x1c */
    int           eff_no;               /* 0x20 */
    int           param_num;            /* 0x24 */
    char         *eff_name;             /* 0x28 */
} SCN_EFF_CTRL;

typedef struct                          /* 0x40 bytes */
{
    SCN_ENE_EF_PDF  Param;              /* 0x00 */
    u_char          pad0[8];            /* 0x18  see below */
    float           Position[4];        /* 0x20 */
    float           Distance;           /* 0x30 */
    float           WaveSpeed;          /* 0x34 */
    int             ModelId;            /* 0x38 */
    void           *pEffRet;            /* 0x3c  8 bytes on the host */
} SCN_TEST_PDEFORM;
/* pad0 is not in the ROM's declaration: SCN_ENE_EF_PDF ends at 0x18 and the EE
 * gives a float[4] member quadword alignment, which puts Position at 0x20.  The
 * host aligns it to 4 and would collapse the gap, shifting every later member
 * by 8 -- so the padding is spelled out.  See [[float4-members-lose-quadword-alignment]].
 * pEffRet is the one member whose host offset legitimately drifts (pointers are
 * 4 bytes on target, 8 here); the offset comments are the ROM's. */

/* --------------------------------------------------------------------------
 *  Internal helpers (forward declarations)
 * ------------------------------------------------------------------------ */
static void SceneTestScnTestPdeformInit(SCN_TEST_PDEFORM *pPdeform);
static int  SceneTestSelect(void);
static void SceneTestPlay(void);
static void SceneTestPlayMovie(void);
static void SceneTestMenu(void);
static void SceneTestLoopManage(char flg);
static int  ScnLRCtrl(int *csr, int max, int min);
static int  SceneTestLoad(void);
static int  SceneTestMovLoad(void);
static int  SceneTestDraw(SCENE_CTRL *pSceneCtrl, int cnt_flg);
static int  SceneTestMenuCsrToFuncNo(int csr_no);
static void SceneTestMainMenu(void);
static void SceneTestCameraMode(void);
static void RotVectorY(float *normal, float *vector, float delta);
static void SceneTestPadCamera(void);
static void ScnDispPrintVector(char *ttl, float *v, float x, float y);
static void SceneTestLightPosDispInit(void);
static void SceneTestAllLightPosDisp(void);
static void SceneTestLightPosDispEnd(void);
static void SceneTestLightSelect(void);
static void SceneTestLightData(void);
static void SceneTestLightPosDispOnOff(void);
static void SceneTestAmbient(void);
static void SceneTestFogData(void);
static void SceneTestEffect(void);
static void SceneTestEffectData(void);
static void SceneTestPartsDeformCommon(int PdeformNo);
static void SceneTestPartsDeform0(void);
static void SceneTestPartsDeform1(void);
static void SceneTestEneEffFire(void);
static void SceneTestEneEffPDeform(void);
static void SceneTestEneEffOthers(void);
static void SceneTestEffectTest(void);
static void SceneTestPdeformCtrl(void);
static void SceneTestSetDefEffect(void);
static void SceneTestVibrate(void);
static int  ScnUDCtrl(int *csr, int max, int min);
static int  ScnValueCtrl(float *val, float max, float min, float inc, float mul);
static int  ScnValueCtrlI(int *val, int max, int min, int inc, int mul);
static int  ScnValueCtrlC(u_char *val, u_char max, u_char min, int inc, int mul);
static int  SceneFileSaveBin(void);
static int  SceneFileSaveText(void);
static void SceneTestRopeReleaseWorkAll(void);

/* --------------------------------------------------------------------------
 *  File-scope globals
 * ------------------------------------------------------------------------ */

static int mono_flg;                            /* sdata 0x3f3f00 */

static char *scene_name[72] =                   /* data  0x343380 */
{
    "0010 movie", "0020 movie", "0110", "0120", "0121 movie", "0122",
    "0130", "0132", "0133", "0140", "0141 movie", "0150", "0160", "0170",
    "0180 movie", "0190", "0200 movie", "0210", "0220 movie", "0230 movie",
    "0231", "0240", "0310 movie", "0330 movie", "0340", "0350",
    "0351 movie", "0352", "0410", "0510", "0520", "0610", "0611 movie",
    "0620", "0710 movie", "0711 movie", "0712 movie", "0713 movie", "0720",
    "0721", "0730", "0731 movie", "0740 movie", "0810 movie", "0820",
    "0910 movie", "0920 movie", "0930 movie", "0940 movie", "0960 movie",
    "1010", "1020 movie", "1030 movie", "1040 movie", "9001", "9002",
    "9005 movie", "9101", "9203", "9204", "9205", "9206", "9302", "9303",
    "9501", "promo 48 movie", "\x81\x9apromo 01 movie" /* [star]promo 01 movie */,
    "promo 02 movie", "promo 03 movie", "GameOver movie", "UBI rogo movie",
    "SCENE_NO_MAX"
};

static void (*SceneTestDebugMenu[16])(void) =   /* data  0x3434a0 */
{
    SceneTestMainMenu,          /*  0 */
    SceneTestCameraMode,        /*  1 */
    SceneTestLightSelect,       /*  2 */
    SceneTestLightData,         /*  3 */
    SceneTestLightPosDispOnOff, /*  4 */
    SceneTestAmbient,           /*  5 */
    SceneTestFogData,           /*  6 */
    SceneTestEffect,            /*  7 */
    SceneTestEffectData,        /*  8 */
    SceneTestPartsDeform0,      /*  9 */
    SceneTestPartsDeform1,      /* 10 */
    SceneTestEneEffFire,        /* 11 */
    SceneTestEneEffPDeform,     /* 12 */
    SceneTestEneEffPDeform,     /* 13 */
    SceneTestEneEffOthers,      /* 14 */
    SceneTestVibrate            /* 15 */
};

static SCN_EFF_CTRL scn_eff_ctrl[SCN_DB_EFF_MAX] =      /* data 0x3434e0 */
{
    { { 0 }, 0, SCN_DB_EFF_Z_DEP,      0, "Z DEPTH"     },
    { { 0 }, 0, SCN_DB_EFF_MONO,       0, "MONO"        },
    { { 0 }, 0, SCN_DB_EFF_SEPIA,      0, "SEPIA"       },
    { { 0 }, 0, SCN_DB_EFF_DITHER,     5, "DITHER"      },
    { { 0 }, 0, SCN_DB_EFF_BLUR_N,     3, "BLUR NORMAL" },
    { { 0 }, 0, SCN_DB_EFF_BLUR_B,     3, "BLUR BLACK"  },
    { { 0 }, 0, SCN_DB_EFF_BLUR_W,     3, "BLUR WHITE"  },
    { { 0 }, 0, SCN_DB_EFF_DEFORM,     2, "DEFORM"      },
    { { 0 }, 0, SCN_DB_EFF_FOCUS,      1, "FOCUS"       },
    { { 0 }, 0, SCN_DB_EFF_CONTRAST1,  2, "CONTRAST1"   },
    { { 0 }, 0, SCN_DB_EFF_CONTRAST2,  2, "CONTRAST2"   },
    { { 0 }, 0, SCN_DB_EFF_CONTRAST3,  2, "CONTRAST3"   },
    { { 0 }, 0, SCN_DB_EFF_NEGA,       3, "NEGA"        },
    { { 0 }, 0, SCN_DB_EFF_FADE_FRAME, 1, "FADE FRAME"  },
    { { 0 }, 0, SCN_DB_EFF_CROSS_FADE, 1, "CROSS FADE"  },
    { { 0 }, 0, SCN_DB_EFF_FADE_SCR,   2, "FADE SCREEN" },
    { { 0 }, 1, SCN_DB_EFF_SHIBATA,    0, "SHIBATA SET" }
};

SCN_TEST_WRK     scn_test_wrk;                  /* data 0x3437d0 */
SCN_TEST_PDEFORM ScnTestPdeform[2];             /* data 0x3448f0 */

/* --------------------------------------------------------------------------
 *  SceneTestInit
 * ------------------------------------------------------------------------ */
void SceneTestInit(void)
{
    int i;

    memset(&scn_test_wrk, 0, sizeof(SCN_TEST_WRK));

    for (i = 0; i < 2; i++)
    {
        SceneTestScnTestPdeformInit(&ScnTestPdeform[i]);                /* 282 */
    }

    scn_test_wrk.debug_flg = 1;
    scn_test_wrk.mode      = SCN_TEST_SLCT;

    InitEffects();                                                      /* 287 */
    foot_seInit();                                                      /* 289 */
    ev_seInit();                                                        /* 290 */
    ev_sisInit();                                                       /* 291 */
    ev_eneInit();                                                       /* 292 */
    ol_loadHeapReset((void *)0x5a6c00, 0x7a8000);                       /* 293 */
    ol_load.Init();                                                     /* 294 */
    SetDebugMenuSwitch(0);                                              /* 298 */
    SceneEffectInit();                                                  /* 301 */
    SetPlyrAcsNo(-1);                                                   /* 304 */
    SetSisterAcsNo(-1);                                                 /* 305 */
}

/* --------------------------------------------------------------------------
 *  SceneTestScnTestPdeformInit
 * ------------------------------------------------------------------------ */
static void SceneTestScnTestPdeformInit(SCN_TEST_PDEFORM *pPdeform)
{
    pPdeform->Param.type  = 0;
    pPdeform->Param.sclx  = 1.0f;
    pPdeform->Param.scly  = 1.0f;
    pPdeform->Param.alpha = 0x40;
    pPdeform->Param.rate  = 1.0f;
    pPdeform->Param.trate = 1.0f;
    pPdeform->Position[0] = 0.0f;
    pPdeform->Position[1] = 0.0f;
    pPdeform->Position[2] = 0.0f;
    pPdeform->Position[3] = 0.0f;
    pPdeform->Distance    = 0.0f;
    pPdeform->WaveSpeed   = 0.299999982f;
    pPdeform->ModelId     = -1;
    pPdeform->pEffRet     = NULL;
}

/* --------------------------------------------------------------------------
 *  SceneTestMain
 * ------------------------------------------------------------------------ */
int SceneTestMain(void)
{
    int ret_val;

    ret_val = 0;

    switch (scn_test_wrk.mode)
    {
        case SCN_TEST_SLCT:
        {
            if (SceneTestSelect() != 0)                                 /* 332 */
            {
                ret_val = 1;
            }
            break;
        }
        case SCN_TEST_PLAY:
        {
            SceneTestPlay();                                            /* 336 */
            break;
        }
        case SCN_TEST_PLAY_MOVIE:
        {
            SceneTestPlayMovie();                                       /* 337 */
            break;
        }
        case SCN_TEST_MENU:
        {
            SceneTestMenu();                                            /* 338 */
            break;
        }
        default:
        {
            break;
        }
    }

    ol_load.Main();                                                     /* 342 */

    if (ret_val != 0)
    {
        SetDebugMenuSwitch(1);                                          /* 346 */
    }

    if (scn_test_wrk.LightAllDispFlg != 0)
    {
        SceneTestAllLightPosDisp();                                     /* 350 */
    }

    return ret_val;
}

/* --------------------------------------------------------------------------
 *  SceneTestSelect  (SCN_TEST_SLCT)
 * ------------------------------------------------------------------------ */
static int SceneTestSelect(void)
{
    char str_title[11]      = "SCENE TEST";
    char str_press[21]      = "Press START to play.";
    char str_scene_no[256]  = "";                                       /* 363 */
    int  func_ret;
    int  ret;

    ret = 0;

    func_ret = ScnLRCtrl(&scn_test_wrk.scene_no, 0x47, 0);              /* 368 */

    if (scn_test_wrk.select_stat == 0)
    {
        if (func_ret == 1)
        {
            scn_test_wrk.init = 1;
            if (SceneDecisionMovie(scn_test_wrk.scene_no) == 0)         /* 372 */
            {
                scn_test_wrk.loop_flg    = 0;
                scn_test_wrk.load_status = 0;
                scn_test_wrk.mode        = SCN_TEST_PLAY;
                SceneTestSetDefEffect();                                /* 380 */
                InitSceneWork();                                        /* 381 */
            }
            else
            {
                scn_test_wrk.mode        = SCN_TEST_PLAY_MOVIE;
                scn_test_wrk.load_status = 0;
            }
        }
        else if (func_ret == -1)
        {
            ret = -1;
        }
    }
    else if (scn_test_wrk.select_stat == 1)
    {
        scn_test_wrk.select_stat = 2;
    }
    else
    {
        scn_test_wrk.select_stat = 0;
    }

    SetASCIIString2(0, 70.0f, 70.0f, 1, 0x80, 0x80, 0x80, str_title);   /* 407 */
    SetASCIIString2(0, 118.0f, 340.0f, 1, 0x80, 0x80, 0x80, str_press); /* 408 */
    sprintf(str_scene_no, "SCENE No. %s", scene_name[scn_test_wrk.scene_no]); /* 409 */
    SetASCIIString2(0, 94.0f, 150.0f, 1, 0x80, 0x80, 0x80, str_scene_no); /* 410 */

    return ret;
}

/* --------------------------------------------------------------------------
 *  SceneTestPlay  (SCN_TEST_PLAY)
 * ------------------------------------------------------------------------ */
static void SceneTestPlay(void)
{
    static int pause_flg;               /* sdata 0x3f4030 */
    int        end_flg;
    int        i;

    if (scn_test_wrk.init == 1)
    {
        if (SceneTestLoad() == 0)                                       /* 426 */
        {
            return;
        }
        scn_test_wrk.loop_flg = 1;
        scn_test_wrk.init     = 0;
        SceneTestLightPosDispInit();                                    /* 431 */
    }

    end_flg = SceneTestDraw(SceneCtrlGet(0), (pause_flg == 0));         /* 436 */

    if (scn_test_wrk.mode != SCN_TEST_MENU)
    {
        if ((pad[0].one & 0x40U) != 0)
        {
            scn_test_wrk.mode = SCN_TEST_MENU;
            scn_test_wrk.init = 1;
        }
        if ((pad[0].one & 0x800U) != 0)
        {
            scn_test_wrk.init     = 1;
            scn_test_wrk.loop_flg = 0;
            scn_test_wrk.mode     = SCN_TEST_SLCT;
            end_flg               = 1;
        }
    }

    if (end_flg == 1)
    {
        SceneEndProc();                                                 /* 456 */
        SceneTestRopeReleaseWorkAll();                                  /* 457 */

        for (i = 0; i < 2; i++)
        {
            MapDrawDeleteRoom(MapLoadGetHeadPtr(i));                    /* 460 */
        }

        scn_test_wrk.init = 1;

        if (scn_test_wrk.loop_flg == 0)
        {
            scn_test_wrk.mode = SCN_TEST_SLCT;
            SceneTestLightPosDispEnd();                                 /* 465 */
        }
        else
        {
            SceneTestLoopManage(1);                                     /* 468 */
            scn_test_wrk.LightAllDispFlg = 0;
        }

        mem_utilFreeMem(scn_test_wrk.load_adrs);                        /* 471 */
    }
}

/* --------------------------------------------------------------------------
 *  SceneTestPlayMovie  (SCN_TEST_PLAY_MOVIE)
 * ------------------------------------------------------------------------ */
static void SceneTestPlayMovie(void)
{
    int LoadStat;

    if (scn_test_wrk.init == 1)
    {
        LoadStat = SceneTestMovLoad();                                  /* 483 */
        if (LoadStat == 0)
        {
            return;
        }
        if (LoadStat == -1)
        {
            scn_test_wrk.mode = SCN_TEST_SLCT;
            scn_test_wrk.init = 1;
            return;
        }
        InitMovieWithTitle(scn_test_wrk.scene_no, 1);                   /* 492 */
        scn_test_wrk.init = 0;
    }

    if (PlayMovieWithTitle() == 0)                                      /* 497 */
    {
        SceneMovieEffectMain(MovieCountGet(), scn_test_wrk.pMovieEffect); /* 506 */
    }
    else
    {
        EndMovieWithTitle();
        scn_test_wrk.init = 1;
        scn_test_wrk.mode = SCN_TEST_SLCT;
        if (scn_test_wrk.pMovieEffect != NULL)
        {
            heapCtrlFree(GetSystemHeapWrkP(), scn_test_wrk.pMovieEffect);
        }
        scn_test_wrk.pMovieEffect = NULL;
        SceneEffectEnd();                                               /* 503 */
    }
}

/* --------------------------------------------------------------------------
 *  SceneTestMenu  (SCN_TEST_MENU)
 * ------------------------------------------------------------------------ */
static void SceneTestMenu(void)
{
    int end_flg;
    int i;

    if (scn_test_wrk.init == 1)
    {
        scn_test_wrk.init          = 0;
        scn_test_wrk.main_menu_csr = 0;
        scn_test_wrk.now_menu      = 0;
    }

    end_flg = SceneTestDraw(SceneCtrlGet(0), 0);                        /* 523 */

    (*SceneTestDebugMenu[scn_test_wrk.now_menu])();

    if ((pad[0].one & 0x800U) != 0)
    {
        scn_test_wrk.loop_flg = 0;
        scn_test_wrk.init     = 1;
        scn_test_wrk.mode     = SCN_TEST_SLCT;
        end_flg               = 1;
    }

    if (end_flg == 1)
    {
        SceneEndProc();                                                 /* 538 */
        SceneTestRopeReleaseWorkAll();                                  /* 539 */

        for (i = 0; i < 2; i++)
        {
            MapDrawDeleteRoom(MapLoadGetHeadPtr(i));                    /* 542 */
        }

        scn_test_wrk.init = 1;

        if (scn_test_wrk.loop_flg == 0)
        {
            scn_test_wrk.mode = SCN_TEST_SLCT;
            SceneTestLightPosDispEnd();                                 /* 550 */
        }
        else
        {
            scn_test_wrk.mode = SCN_TEST_PLAY;
            SceneTestLoopManage(1);                                     /* 553 */
            scn_test_wrk.LightAllDispFlg = 0;
        }
    }
}

/* --------------------------------------------------------------------------
 *  SceneTestLoopManage
 *
 *  flg == 0 : restore the saved light / ambient / fog block into the scene.
 *  flg != 0 : save the scene's light / ambient / fog block, so that looping
 *             the same scene keeps whatever the artist just tuned.
 * ------------------------------------------------------------------------ */
static void SceneTestLoopManage(char flg)
{
    FOD_LIGHT  *scn_fl;
    SCENE_CTRL *scn_p;

    /* The ROM reaches the light block through SceneCtrlGet() + the fod_ctrl
     * offset here rather than through SceneFodLightPtrGet(); the two calls at
     * 566/567 are both to SceneCtrlGet.  Other functions in this file really
     * do call SceneFodLightPtrGet, so the difference is preserved. */
    scn_fl = &SceneCtrlGet(0)->fod_ctrl.fod_light;                      /* 566 */
    scn_p  = SceneCtrlGet(0);                                           /* 567 */

    if (flg == 0)
    {
        memcpy(&scn_fl->all_lit[0], &scn_test_wrk.light_tmp[0], sizeof(scn_test_wrk.light_tmp));
        memcpy(&scn_fl->amb[0], &scn_test_wrk.amb_tmp[0], sizeof(scn_test_wrk.amb_tmp));
        scn_p->fog = scn_test_wrk.fog_tmp;
    }
    else
    {
        memcpy(&scn_test_wrk.light_tmp[0], &scn_fl->all_lit[0], sizeof(scn_test_wrk.light_tmp));
        memcpy(&scn_test_wrk.amb_tmp[0], &scn_fl->amb[0], sizeof(scn_test_wrk.amb_tmp));
        scn_test_wrk.fog_tmp = scn_p->fog;
    }
}

/* --------------------------------------------------------------------------
 *  ScnLRCtrl
 *
 *  L1 / R1 step the cursor, circle decides, cross cancels.
 * ------------------------------------------------------------------------ */
static int ScnLRCtrl(int *csr, int max, int min)
{
    int ret;

    ret = 0;

    if ((pad[0].rpt & 0x2000U) != 0)
    {
        (*csr)++;
        if (*csr >= max)
        {
            *csr = 0;
        }
    }
    else if ((pad[0].rpt & 0x8000U) != 0)
    {
        (*csr)--;
        if (*csr < 0)
        {
            *csr = max - 1;
        }
    }
    else if (**paddat == 1)
    {
        ret = 1;
    }
    else if (*paddat[1] == 1)
    {
        ret = -1;
    }

    return ret;
}

/* --------------------------------------------------------------------------
 *  SceneTestLoad
 * ------------------------------------------------------------------------ */
static int SceneTestLoad(void)
{
    int ret_val = 0;

    MapObjRegSetSceneLoad(1);                                           /* 621 */

    switch (scn_test_wrk.load_status)
    {
        case 0:
        {
            InitSceneWork();                                            /* 625 */
            scn_test_wrk.load_adrs   = (u_int*)mem_utilGetMem(0x200000); /* 626 */
            scn_test_wrk.load_status = 2;
            break;
        }
        case 2:
        {
            if (SceneAllLoad(scn_test_wrk.scene_no, scn_test_wrk.load_adrs) != 0) /* 631 */
            {
                if (SceneSubRoomNoGet() == -1)                          /* 634 */
                {
                    if (SceneRoomNoGet() == -1)                         /* 638 */
                    {
                        PRINT_ASSERT("Room No is not set up.");
                    }
                    MapLoadInit(SceneRoomNoGet());                      /* 641 */
                }
                else
                {
                    MapLoadInit(SceneSubRoomNoGet());
                }
                scn_test_wrk.load_status = 3;
            }
            break;
        }
        case 3:
        {
            if (MapLoadMain() == 0)                                     /* 652 */
            {
                if (SceneSubRoomNoGet() == -1)                          /* 654 */
                {
                    ret_val = 1;
                }
                else
                {
                    if (SceneRoomNoGet() == -1)                         /* 655 */
                    {
                        PRINT_ASSERT("Room No is not set up.");
                    }
                    MapLoadMoveRoom(SceneRoomNoGet());                  /* 658 */
                    scn_test_wrk.load_status = 4;
                }
            }
            break;
        }
        case 4:
        {
            MapLoadMain();                                              /* 668 */
            if (MapLoadCheckLoadNow() == 0)                             /* 669 */
            {
                if (SceneSubRoomNoGet() != -1)                          /* 670 */
                {
                    MapDoorAllPreRender(SceneRoomNoGet());              /* 671 */
                }
                ret_val = 1;
            }
            break;
        }
        default:
        {
            break;
        }
    }

    if (ret_val == 1)
    {
        scn_test_wrk.load_status = 0;
        SceneInitializeIngame();                                        /* 684 */
        if (scn_test_wrk.loop_flg != 0)
        {
            SceneTestLoopManage(0);                                     /* 686 */
        }
        MapAnimProc();                                                  /* 688 */
    }

    MapObjRegSetSceneLoad(0);                                           /* 692 */

    return ret_val;
}

/* --------------------------------------------------------------------------
 *  SceneTestMovLoad
 *
 *  Pulls the scene's movie-effect script onto the system heap before the
 *  movie itself starts.  Returns 1 when ready, 0 while loading, -1 on a
 *  failed allocation.
 * ------------------------------------------------------------------------ */
static int SceneTestMovLoad(void)
{
    int ret_val;

    if (scn_test_wrk.load_status == 0)
    {
        int FileNo = SceneEffectDataFileNoGet(scn_test_wrk.scene_no);   /* 707 */
        scn_test_wrk.pMovieEffect =
            (u_int *)SAFE_MALLOC(GetSystemHeapWrkP(), NULL, GetFileSize(FileNo)); /* 709 */

        if (scn_test_wrk.pMovieEffect == NULL)
        {
            ret_val = -1;
        }
        else
        {
            LoadReq(FileNo, (uintptr_t)scn_test_wrk.pMovieEffect);      /* 711 */
            scn_test_wrk.load_status = 1;
            ret_val = 0;
        }
    }
    else
    {
        ret_val = 0;
        if (scn_test_wrk.load_status == 1)
        {
            ret_val = (IsLoadEndAll() != 0);                            /* 720 */
        }
    }

    return ret_val;
}

/* --------------------------------------------------------------------------
 *  SceneTestDraw
 *
 *  One scene frame plus the frame / resolution read-out.  L2 single-steps
 *  while paused; R2 nudges the FOD frame counter forward.
 * ------------------------------------------------------------------------ */
static int SceneTestDraw(SCENE_CTRL *pSceneCtrl, int cnt_flg)
{
    char frame[256];

    if (cnt_flg == 0)
    {
        cnt_flg = ((pad[0].rpt & 0x10U) != 0);
        if ((pad[0].rpt & 0x80U) != 0)
        {
            SceneFodSetFrame(0, pSceneCtrl->fod_ctrl.now_frame - 1);    /* 747 */
        }
    }
    else if ((pad[0].rpt & 0x10U) != 0)
    {
        SceneFodSetNowFrame(0, pSceneCtrl->fod_ctrl.now_frame + 6);     /* 762 */
        SceneFodSetNowRezo(0, 0);                                       /* 763 */
        SceneFodSetFrame(0, pSceneCtrl->fod_ctrl.now_frame - 1);        /* 764 */
    }

    if ((pad[0].one & 0x100U) != 0)
    {
        scn_test_wrk.debug_flg ^= 1;
    }

    int now_frame = pSceneCtrl->fod_ctrl.now_frame;
    int now_reso = pSceneCtrl->fod_ctrl.now_reso;

    SceneCountFlgSet(cnt_flg);                                          /* 775 */
    SceneDraw(pSceneCtrl->scene_no);                                    /* 777 */
    SceneTestEffectTest();                                              /* 779 */

    if (scn_test_wrk.debug_flg != 0)
    {
        sprintf(frame, "NOW FRAME : %04d", now_frame);                  /* 783 */
        SceneSetSquare(2, 418.0f, 26.0f, 204.0f, 16.0f, 0x28, 0x28, 0x28, 0x64); /* 784 */
        SetASCIIString2(1, 424.0f, 28.0f, 0, 0x80, 0x80, 0x80, frame);  /* 785 */
        sprintf(frame, "NOW RESO  : %04d", now_reso);                   /* 786 */
        SceneSetSquare(2, 418.0f, 42.0f, 204.0f, 16.0f, 0x28, 0x28, 0x28, 0x64); /* 787 */
        SetASCIIString2(1, 424.0f, 44.0f, 0, 0x80, 0x80, 0x80, frame);  /* 788 */
    }

    return SceneIsEnd();                                                /* 791 */
}

/* --------------------------------------------------------------------------
 *  SceneTestMenuCsrToFuncNo
 *
 *  Maps a main-menu cursor row onto its SceneTestDebugMenu[] slot.
 * ------------------------------------------------------------------------ */
static int SceneTestMenuCsrToFuncNo(int csr_no)
{
    short int ChangeTable[9] = { 1, 2, 4, 5, 6, 7, 9, 10, 15 };

    int ret = 0;

    if ((u_int)csr_no < 9)
    {
        ret = (int)ChangeTable[csr_no];
    }

    return ret;
}

/* --------------------------------------------------------------------------
 *  SceneTestMainMenu  (debug menu slot 0)
 * ------------------------------------------------------------------------ */
static void SceneTestMainMenu(void)
{
    int   i;
    char *menu_str[12] =
    {
        "CAMERA", "LIGHT", "LIGHT POS DISP", "AMBIENT", "FOG", "EFFECT",
        "PARTS DEFORM 0", "PARTS DEFORM 1", "VIBRATE", "FILE SAVE",
        "TEXT FILE SAVE", "EXIT"
    };

    ScnUDCtrl(&scn_test_wrk.main_menu_csr, 0xc, 0);                     /* 832 */

    if ((pad[0].one & 0x20U) != 0)
    {
        switch (scn_test_wrk.main_menu_csr)
        {
            case 0:
            case 1:
            case 2:
            case 3:
            case 4:
            case 5:
            case 8:
            {
                scn_test_wrk.now_menu = SceneTestMenuCsrToFuncNo(scn_test_wrk.main_menu_csr); /* 842 */
                scn_test_wrk.menu_csr = 0;
                break;
            }
            case 6:
            {
                scn_test_wrk.now_menu = SceneTestMenuCsrToFuncNo(scn_test_wrk.main_menu_csr); /* 846 */
                if (ScnTestPdeform[0].ModelId != -1)
                {
                    scn_test_wrk.menu_csr = ScnTestPdeform[0].ModelId + 1;
                }
                break;
            }
            case 7:
            {
                scn_test_wrk.now_menu = SceneTestMenuCsrToFuncNo(scn_test_wrk.main_menu_csr); /* 852 */
                if (ScnTestPdeform[1].ModelId != -1)
                {
                    scn_test_wrk.menu_csr = ScnTestPdeform[1].ModelId + 1;
                }
                break;
            }
            case 9:
            {
                SceneFileSaveBin();                                     /* 861 */
                break;
            }
            case 10:
            {
                SceneFileSaveText();                                    /* 865 */
                break;
            }
            case 0xb:
            {
                scn_test_wrk.mode = SCN_TEST_PLAY;
                break;
            }
            default:
            {
                break;
            }
        }
    }
    else if ((pad[0].one & 0x40U) != 0)
    {
        scn_test_wrk.mode = SCN_TEST_PLAY;
    }

    /* L3 hides the menu overlay so the artist can see the raw frame. */
    if ((pad[0].now & 1U) == 0)
    {
        for (i = 0; i < 12; i++)
        {
            SetASCIIString2(1, 48.0f, (float)(i * 0xe + 0x30), 0, 0x80, 0x80, 0x80, menu_str[i]); /* 881 */
        }
        SceneSetSquare(2, 48.0f, (float)(scn_test_wrk.main_menu_csr * 0xe + 0x30),
                       120.0f, 12.0f, 0x50, 0x50, 0x64, 0x50);          /* 884 */
    }
}

/* --------------------------------------------------------------------------
 *  SceneTestCameraMode  (debug menu slot 1)
 * ------------------------------------------------------------------------ */
static void SceneTestCameraMode(void)
{
    float  pos[2][4];
    int    i;
    char  *menu_str[2] = { "POSITION", "INTEREST" };

    ScnUDCtrl(&scn_test_wrk.menu_csr, 2, 0);                            /* 901 */

    if ((pad[0].one & 0x40U) != 0)
    {
        scn_test_wrk.menu_csr = 0;
        scn_test_wrk.now_menu = 0;
        return;
    }

    SceneTestPadCamera();                                               /* 907 */

    g3dxVu0CopyVector(pos[0], gra3dcamGetPosition());                   /* 907 */
    g3dxVu0CopyVector(pos[1], gra3dcamGetTarget());                     /* 907 */

    for (i = 0; i < 2; i++)
    {
        ScnDispPrintVector(menu_str[i], pos[i], 32.0f, (float)(i * 0x18 + 0x40)); /* 912 */
    }
}

/* --------------------------------------------------------------------------
 *  RotVectorY
 *
 *  Rotate `vector` about Y by `delta` radians, wrapped into (-PI, PI).
 * ------------------------------------------------------------------------ */
static void RotVectorY(float *normal, float *vector, float delta)
{
    sceVu0FMATRIX m0;
    float         fVar1;

    fVar1 = fmodf(delta, 6.283185f);                                    /* 921 */
    if (fVar1 >= 3.1415925f)
    {
        fVar1 = fmodf(fVar1, 3.1415925f) + (-3.1415925f);               /* 923 */
    }
    else if (fVar1 <= -3.1415925f)
    {
        fVar1 = fmodf(fVar1, 3.1415925f) + 3.1415925f;                  /* 925 */
    }

    sceVu0UnitMatrix(m0);                                               /* 927 */
    sceVu0RotMatrixY(m0, m0, fVar1);                                    /* 928 */
    sceVu0ApplyMatrix(normal, m0, vector);                              /* 929 */
}

/* --------------------------------------------------------------------------
 *  SceneTestPadCamera
 *
 *  Free-camera driver.  Without R1 the d-pad dollies / strafes the eye and
 *  target together and the face buttons raise / lower them; holding R1 turns
 *  it into a look-around, pivoting the target about the eye.
 * ------------------------------------------------------------------------ */
static void SceneTestPadCamera(void)
{
    float  vec[4];
    float  dir[4];
    float  xz_dir[4];
    float  CamPosition[4];
    float  CamTarget[4];
    float  accel;
    float (&rCamPosition)[4] = gra3dcamGetPosition();                   /* 942 */
    float (&rCamTarget)[4]   = gra3dcamGetTarget();                     /* 943 */

    sceVu0SubVector(dir, rCamTarget, rCamPosition);                     /* 961 */

    accel  = g3dxVu0Sqrt2(dir[0], dir[2]);
    dir[1] = 0.0f;
    dir[3] = 1.0f;
    sceVu0Normalize(dir, dir);                                          /* 953 */

    sceVu0CopyVector(xz_dir, dir);
    xz_dir[1] = 0.0f;

    if ((pad[0].now & 2U) == 0)
    {
        sceVu0ScaleVectorXYZ(xz_dir, xz_dir, 10.0f);                    /* 948 */

        if ((pad[0].now & 0x1000U) != 0)
        {
            sceVu0AddVector(CamTarget, rCamTarget, xz_dir);             /* 952 */
            gra3dcamSetTarget(CamTarget, 1);                            /* 962 */
            sceVu0AddVector(CamPosition, rCamPosition, xz_dir);
            gra3dcamSetPosition(CamPosition);                           /* 964 */
        }
        if ((pad[0].now & 0x4000U) != 0)
        {
            sceVu0SubVector(CamTarget, rCamTarget, xz_dir);             /* 963 */
            gra3dcamSetTarget(CamTarget, 1);                            /* 968 */
            sceVu0SubVector(CamPosition, rCamPosition, xz_dir);         /* 981 */
            gra3dcamSetPosition(CamPosition);                           /* 977 */
        }
        if ((pad[0].now & 0x8000U) != 0)
        {
            RotVectorY(xz_dir, xz_dir, 1.57079625f);                    /* 980 */
            sceVu0SubVector(CamTarget, rCamTarget, xz_dir);             /* 983 */
            gra3dcamSetTarget(CamTarget, 1);                            /* 982 */
            sceVu0SubVector(CamPosition, rCamPosition, xz_dir);
            gra3dcamSetPosition(CamPosition);                           /* 984 */
        }
        if ((pad[0].now & 0x2000U) != 0)
        {
            RotVectorY(xz_dir, xz_dir, 1.57079625f);                    /* 996 */
            sceVu0AddVector(CamTarget, rCamTarget, xz_dir);
            gra3dcamSetTarget(CamTarget, 1);                            /* 991 */
            sceVu0AddVector(CamPosition, rCamPosition, xz_dir);
            gra3dcamSetPosition(CamPosition);                           /* 988 */
        }
        if ((pad[0].now & 1U) != 0)
        {
            gra3dcamSetTarget(rCamTarget[0], rCamTarget[1] - 1.5f, rCamTarget[2], 1); /* 987 */
            gra3dcamSetPosition(rCamPosition[0], rCamPosition[1] - 1.5f, rCamPosition[2]); /* 992 */
        }
        if ((pad[0].now & 4U) != 0)
        {
            gra3dcamSetTarget(rCamTarget[0], rCamTarget[1] + 1.5f, rCamTarget[2], 1); /* 997 */
            gra3dcamSetPosition(rCamPosition[0], rCamPosition[1] + 1.5f, rCamPosition[2]);
        }
    }
    else
    {
        if ((pad[0].now & 0x2000U) != 0)
        {
            RotVectorY(vec, xz_dir, 0.03f);                             /* 1000 */
            gra3dcamSetTarget(rCamPosition[0] + vec[0] * accel, rCamTarget[1],
                              rCamPosition[2] + vec[2] * accel, 1);     /* 1001 */
        }
        else if ((pad[0].now & 0x8000U) != 0)
        {
            RotVectorY(vec, xz_dir, -0.03f);
            gra3dcamSetTarget(rCamPosition[0] + vec[0] * accel, rCamTarget[1],
                              rCamPosition[2] + vec[2] * accel, 1);     /* 1004 */
        }
        if ((pad[0].now & 0x4000U) != 0)
        {
            gra3dcamSetTarget(rCamTarget[0], rCamTarget[1] - (accel + accel) / 60.0f,
                              rCamTarget[2], 1);                        /* 1007 */
        }
        if ((pad[0].now & 0x1000U) != 0)
        {
            gra3dcamSetTarget(rCamTarget[0], rCamTarget[1] + (accel + accel) / 60.0f,
                              rCamTarget[2], 1);
        }
    }

    gra3dApplyCamera(NULL, 1);                                          /* 1010 */
}

/* --------------------------------------------------------------------------
 *  ScnDispPrintVector
 * ------------------------------------------------------------------------ */
static void ScnDispPrintVector(char *ttl, float *v, float x, float y)
{
    char tmp_str[256];

    sprintf(tmp_str, "%s = %f : %f : %f", ttl, v[0], v[1], v[2]);       /* 1021 */
    SetASCIIString2(1, x, y, 0, 0x80, 0x80, 0x80, tmp_str);             /* 1022 */
}

/* --------------------------------------------------------------------------
 *  SceneTestLightPosDispInit
 * ------------------------------------------------------------------------ */
static void SceneTestLightPosDispInit(void)
{
    FOD_LIGHT *pFodLight = SceneFodLightPtrGet(0);                      /* 1031 */

    if (scn_test_wrk.LightDispNum == 0)
    {
        scn_test_wrk.LightDispNum = pFodLight->all_lit_num;

        for (int i = 0; i < scn_test_wrk.LightDispNum; i++)
        {
            scn_test_wrk.LightDispFlg[i] = 0;
            scn_test_wrk.LightDispNum = pFodLight->all_lit_num;
        }
    }

    scn_test_wrk.LightAllDispFlg = 1;
}

/* --------------------------------------------------------------------------
 *  SceneTestAllLightPosDisp
 * ------------------------------------------------------------------------ */
static void SceneTestAllLightPosDisp(void)
{
    FOD_LIGHT *pFodLight = SceneFodLightPtrGet(0);                      /* 1048 */

    for (int i = 0; i < scn_test_wrk.LightDispNum; i++)
    {
        if (scn_test_wrk.LightDispFlg[i] != 0)
        {
            SceneTestDrawCrossLine(pFodLight->all_lit[i].vPosition, 60.0f,
                                   0x80, 0x80, 0x80, 0x80);             /* 1055 */
        }
    }
}

/* --------------------------------------------------------------------------
 *  SceneTestLightPosDispEnd
 * ------------------------------------------------------------------------ */
static void SceneTestLightPosDispEnd(void)
{
    for (int i = FOD_LIGHT_MAX - 1; i >= 0; i--)
    {
        scn_test_wrk.LightDispFlg[i] = 0;
    }

    scn_test_wrk.LightAllDispFlg = 0;
    scn_test_wrk.LightDispNum    = 0;
}

/* --------------------------------------------------------------------------
 *  SceneTestLightSelect  (debug menu slot 2)
 * ------------------------------------------------------------------------ */
static void SceneTestLightSelect(void)
{
    char      *ltype_name[4] = { "AMB  ", "INF  ", "SPOT ", "POINT" };
    char       tmp_str[256]  = "";                                      /* 1088 */

    FOD_LIGHT *scn_fl = &SceneCtrlGet(0)->fod_ctrl.fod_light;           /* 1089 */

    int ret = ScnUDCtrl(&scn_test_wrk.menu_csr, scn_fl->all_lit_num, 0); /* 1092 */

    if (ret == 1)
    {
        scn_test_wrk.now_menu   = 3;
        scn_test_wrk.light_no   = scn_test_wrk.menu_csr;
        scn_test_wrk.l_menu_csr = 0;
    }
    if (ret == -1)
    {
        scn_test_wrk.menu_csr = 0;
        scn_test_wrk.now_menu = 0;
    }

    for (int i = 0; i < (int)scn_fl->all_lit_num; i++)
    {
        sprintf(tmp_str, "No.%02d : %s : %s",
                scn_fl->lit_serial[i].light_no,
                ltype_name[scn_fl->lit_serial[i].light_type],
                scn_fl->lit_serial[i].light_name);                      /* 1106 */
        SetASCIIString2(1, 48.0f, (float)(i * 0xe + 0x30), 0, 0x80, 0x80, 0x80, tmp_str); /* 1107 */
    }

    SceneSetSquare(2, 48.0f, (float)(scn_test_wrk.menu_csr * 0xe + 0x30), 156.0f, 12.0f,
                   0x50, 0x50, 0x64, 0x50);                             /* 1110 */
}

/* --------------------------------------------------------------------------
 *  SceneTestLightData  (debug menu slot 3)
 *
 *  Per-light RGB, plus range for spot/point lights and the cone half-angle
 *  for spots.  The cone is stored as cos^2 in afPad0[0], so it is converted
 *  to degrees for display and back on edit.
 * ------------------------------------------------------------------------ */
static void SceneTestLightData(void)
{
    float           color[4];
    float           cone_deg;
    int             menu_num;
    char           *ltype_name[4] = { "AMB  ", "INF  ", "SPOT ", "POINT" };
    char           *col_str[3]    = { "R", "G", "B" };
    char            tmp_str[256]  = "";                                 /* 1128 */

    FOD_LIGHT *scn_fl = SceneFodLightPtrGet(0);                         /* 1129 */
    G3DLIGHT *light = &scn_fl->all_lit[scn_test_wrk.light_no];
    FOD_LIT_SERIAL *fls = &scn_fl->lit_serial[scn_test_wrk.light_no];

    if (fls->light_type == 2)
    {
        cone_deg = acosf(g3dxVu0Sqrt(light->afPad0[0])) * 180.0f / 3.1415925f; /* 1137 */
        menu_num = 5;
    }
    else if (fls->light_type == 3)
    {
        menu_num = 4;
    }
    else
    {
        menu_num = 3;
    }

    g3dxVu0CopyVector(color, light->vDiffuse);

    if (ScnUDCtrl(&scn_test_wrk.l_menu_csr, menu_num, 0) == -1)         /* 1145 */
    {
        scn_test_wrk.now_menu   = 2;
        scn_test_wrk.l_menu_csr = 0;
    }

    if (scn_test_wrk.l_menu_csr < 3)
    {
        ScnValueCtrl(&color[scn_test_wrk.l_menu_csr], 100.0f, 0.0f, 0.01f, 20.0f); /* 1153 */
    }
    else if (scn_test_wrk.l_menu_csr == 3)
    {
        ScnValueCtrl(&light->fMaxRange, 10000.0f, 0.0f, 1.0f, 10.0f);   /* 1156 */
        light->fMinRange = light->fMaxRange * 0.5f;
    }
    else
    {
        if (ScnValueCtrl(&cone_deg, 90.0f, 1.0f, 0.099999994f, 10.0f) == 1) /* 1159 */
        {
            float fCos = cosf(cone_deg * 3.1415925f / 180.0f);          /* 1162 */
            gra3dSetLightIntens(light, fCos * fCos);                    /* 1163 */
        }
    }

    sprintf(tmp_str, "No.%02d : %s : %s",
            fls->light_no, ltype_name[fls->light_type], fls->light_name); /* 1169 */
    SetASCIIString2(1, 48.0f, 48.0f, 0, 0x80, 0x80, 0x80, tmp_str);     /* 1170 */

    int yofs = 0x1c;
    for (int i = 0; i < 3; i++)
    {
        sprintf(tmp_str, "  %s : %.3f", col_str[i], color[i]);          /* 1174 */
        SetASCIIString2(1, 48.0f, (float)(yofs + 0x30), 0, 0x80, 0x80, 0x80, tmp_str); /* 1175 */
        yofs += 0xe;
    }

    yofs   += 0xe;
    int csr_pos = scn_test_wrk.l_menu_csr * 0xe + 0x4c;

    if (fls->light_type == 2 || fls->light_type == 3)
    {
        csr_pos = yofs + 0x30;
        sprintf(tmp_str, "POWER = %.2f", light->fMaxRange);             /* 1183 */
        SetASCIIString2(1, 48.0f, (float)csr_pos, 0, 0x80, 0x80, 0x80, tmp_str); /* 1184 */

        yofs += 0x1c;

        if (fls->light_type == 2)
        {
            sprintf(tmp_str, "CONE = %.1f", cone_deg);                  /* 1190 */
            SetASCIIString2(1, 48.0f, (float)(yofs + 0x30), 0, 0x80, 0x80, 0x80, tmp_str); /* 1191 */
            if (scn_test_wrk.l_menu_csr == 4)
            {
                csr_pos = yofs + 0x30;
            }

            yofs += 0x1c;

            ScnDispPrintVector("POS ", light->vPosition, 48.0f, (float)(yofs + 0x30)); /* 1197 */
            ScnDispPrintVector("INT ", light->vDirection, 48.0f, (float)(yofs + 0x4c)); /* 1199 */
        }
        else
        {
            ScnDispPrintVector("POS ", light->vPosition, 48.0f, (float)(yofs + 0x30)); /* 1202 */
        }

        if (scn_test_wrk.l_menu_csr < 3)
        {
            csr_pos = scn_test_wrk.l_menu_csr * 0xe + 0x4c;
        }
    }

    SceneSetSquare(2, 36.0f, (float)csr_pos, 180.0f, 12.0f, 0x50, 0x50, 0x64, 0x50); /* 1211 */

    g3dxVu0CopyVector(light->vDiffuse, color);
}

/* --------------------------------------------------------------------------
 *  SceneTestLightPosDispOnOff  (debug menu slot 4)
 * ------------------------------------------------------------------------ */
static void SceneTestLightPosDispOnOff(void)
{
    SCN_TEST_WRK *stw;
    int           i;
    char         *ltype_name[4] = { "AMB  ", "INF  ", "SPOT ", "POINT" };
    char         *on_off[2]     = { "OFF", "ON " };
    char          tmp_str[256]  = "";                                   /* 1225 */
    FOD_LIGHT    *scn_fl;

    stw    = &scn_test_wrk;
    scn_fl = &SceneCtrlGet(0)->fod_ctrl.fod_light;                      /* 1226 */

    if (ScnUDCtrl(&stw->menu_csr, scn_fl->all_lit_num, 0) == -1)        /* 1229 */
    {
        stw->now_menu = 0;
        stw->menu_csr = 0;
    }

    ScnValueCtrlI(&stw->LightDispFlg[stw->menu_csr], 1, 0, 1, 1);       /* 1234 */

    for (i = 0; i < (int)scn_fl->all_lit_num; i++)
    {
        sprintf(tmp_str, "%s : %s : %s",
                on_off[stw->LightDispFlg[i]],
                ltype_name[scn_fl->lit_serial[i].light_type],
                scn_fl->lit_serial[i].light_name);                      /* 1239 */
        SetASCIIString2(1, 48.0f, (float)(i * 0xe + 0x30), 0, 0x80, 0x80, 0x80, tmp_str); /* 1240 */
    }

    SceneSetSquare(2, 48.0f, (float)(stw->menu_csr * 0xe + 0x30), 156.0f, 12.0f,
                   0x50, 0x50, 0x64, 0x50);                             /* 1243 */
}

/* --------------------------------------------------------------------------
 *  SceneTestAmbient  (debug menu slot 5)
 *
 *  Six ambient banks (common / chara / room / furn / door / item) x RGB.
 * ------------------------------------------------------------------------ */
static void SceneTestAmbient(void)
{
    float     *amb_p;
    char      *amb_str[6] = { "COMMON", "CHARA ", "ROOM  ", "FURN  ", "DOOR  ", "ITEM  " };
    char      *col_str[3] = { "R", "G", "B" };
    char       tmp_str[256] = "";                                       /* 1259 */
    int        i;
    int        j;
    int        yofs;
    int        csr_pos;
    FOD_LIGHT *scn_fl;

    scn_fl = SceneFodLightPtrGet(0);                                    /* 1261 */

    if (ScnUDCtrl(&scn_test_wrk.menu_csr, 0x12, 0) == -1)               /* 1263 */
    {
        scn_test_wrk.now_menu = 0;
        scn_test_wrk.menu_csr = 0;
        return;
    }

    ScnValueCtrl(&scn_fl->amb[scn_test_wrk.menu_csr / 3][scn_test_wrk.menu_csr % 3],
                 10.0f, 0.0f, 0.000999999931f, 20.0f);                  /* 1271 */

    SetASCIIString2(1, 48.0f, 48.0f, 0, 0x80, 0x80, 0x80, "AMBIENT");   /* 1275 */

    yofs = 0x1c;
    for (i = 0; i < 6; i++)
    {
        amb_p = scn_fl->amb[i];
        sprintf(tmp_str, " %s %s : %.3f", amb_str[i], col_str[0], amb_p[0]); /* 1279 */
        SetASCIIString2(1, 48.0f, (float)(yofs + 0x30), 0, 0x80, 0x80, 0x80, tmp_str); /* 1280 */

        for (j = 1; j < 3; j++)
        {
            sprintf(tmp_str, "        %s : %.3f", col_str[j], amb_p[j]); /* 1284 */
            SetASCIIString2(1, 48.0f, (float)(yofs + 0x3e), 0, 0x80, 0x80, 0x80, tmp_str); /* 1285 */
            yofs += 0xe;
        }

        yofs += 0x1c;
    }

    csr_pos = (scn_test_wrk.menu_csr % 3) * 0xe + (scn_test_wrk.menu_csr / 3) * 0x38 + 0x4c;
    SceneSetSquare(2, 48.0f, (float)csr_pos, 240.0f, 12.0f, 0x50, 0x50, 0x64, 0x50); /* 1292 */
}

/* --------------------------------------------------------------------------
 *  SceneTestFogData  (debug menu slot 6)
 * ------------------------------------------------------------------------ */
static void SceneTestFogData(void)
{
    SCENE_CTRL *scn_p;
    int         i;
    char       *menu_str[7] = { "R   ", "G   ", "B   ", "NEAR", "FAR ", "MIN ", "MAX " };
    char        tmp_str[256];
    float       fog_data[7];

    scn_p = SceneCtrlGet(0);                                            /* 1306 */

    fog_data[0] = (float)scn_p->fog.r;
    fog_data[1] = (float)scn_p->fog.g;
    fog_data[2] = (float)scn_p->fog.b;
    fog_data[3] = scn_p->fog.near;
    fog_data[4] = scn_p->fog.far;
    fog_data[5] = scn_p->fog.min;
    fog_data[6] = scn_p->fog.max;

    if (ScnUDCtrl(&scn_test_wrk.menu_csr, 7, 0) == -1)                  /* 1320 */
    {
        scn_test_wrk.now_menu = 0;
        scn_test_wrk.menu_csr = 0;
    }

    if (scn_test_wrk.menu_csr < 3)
    {
        ScnValueCtrl(&fog_data[scn_test_wrk.menu_csr], 255.0f, 0.0f, 1.0f, 16.0f); /* 1326 */
    }
    else if (scn_test_wrk.menu_csr < 5)
    {
        ScnValueCtrl(&fog_data[scn_test_wrk.menu_csr], 10000.0f, -10000.0f, 10.0f, 10.0f); /* 1328 */
    }
    else
    {
        ScnValueCtrl(&fog_data[scn_test_wrk.menu_csr], 255.0f, 0.0f, 1.0f, 16.0f); /* 1330 */
    }

    SetASCIIString2(1, 32.0f, 40.0f, 0, 0x80, 0x80, 0x80, "FOG DATA");  /* 1333 */

    for (i = 0; i < 7; i++)
    {
        if (i < 3)
        {
            sprintf(tmp_str, "%s : %d", menu_str[i], (int)fog_data[i]); /* 1336 */
        }
        else if (i < 5)
        {
            sprintf(tmp_str, "%s : %.01f", menu_str[i], fog_data[i]);   /* 1338 */
        }
        else
        {
            sprintf(tmp_str, "%s : %.00f", menu_str[i], fog_data[i]);   /* 1340 */
        }
        SetASCIIString2(1, 40.0f, (float)(i * 0xe + 0x40), 0, 0x80, 0x80, 0x80, tmp_str); /* 1342 */
    }

    SceneSetSquare(2, 36.0f, (float)(scn_test_wrk.menu_csr * 0xe + 0x40), 192.0f, 12.0f,
                   0x50, 0x50, 0x64, 0x50);                             /* 1346 */

    scn_p->fog.r    = (int)fog_data[0];
    scn_p->fog.g    = (int)fog_data[1];
    scn_p->fog.b    = (int)fog_data[2];
    scn_p->fog.near = fog_data[3];
    scn_p->fog.far  = fog_data[4];
    scn_p->fog.min  = fog_data[5];
    scn_p->fog.max  = fog_data[6];
}

/* --------------------------------------------------------------------------
 *  SceneTestEffect  (debug menu slot 7)
 *
 *  The post-process bank: one on/off row per effect.  Rows past SEPIA also
 *  have tunable parameters, so circle drops into SceneTestEffectData.
 * ------------------------------------------------------------------------ */
static void SceneTestEffect(void)
{
    SCN_TEST_WRK *stw;
    SCN_EFF_CTRL *sec;
    int           i;
    int           ret;
    int           yofs;
    char          tmp_str[256] = "";                                    /* 1390 */
    char         *on_off[2]    = { "OFF", "ON" };

    stw = &scn_test_wrk;

    ret = ScnUDCtrl(&stw->menu_csr, SCN_DB_EFF_MAX, 0);                 /* 1392 */

    if (ret == 1 && stw->menu_csr > SCN_DB_EFF_FLG_ONLY)
    {
        stw->effect_id  = stw->menu_csr;
        stw->now_menu   = 8;
        stw->effect_csr = 0;
    }
    if (ret == -1)
    {
        stw->menu_csr = 0;
        stw->now_menu = 0;
        return;
    }

    ScnValueCtrlI((int *)&scn_eff_ctrl[stw->menu_csr].eff_flg, 1, 0, 1, 1); /* 1406 */

    SetASCIIString2(1, 48.0f, 48.0f, 0, 0x80, 0x80, 0x80, "EFFECT");    /* 1409 */

    yofs = 0x1c;
    for (i = 0; i < SCN_DB_EFF_MAX; i++)
    {
        sec = &scn_eff_ctrl[i];
        sprintf(tmp_str, "  %s", sec->eff_name);                        /* 1414 */
        SetASCIIString2(1, 48.0f, (float)(yofs + 0x30), 0, 0x80, 0x80, 0x80, tmp_str); /* 1415 */
        sprintf(tmp_str, ":  %s", on_off[sec->eff_flg]);                /* 1416 */
        SetASCIIString2(1, 216.0f, (float)(yofs + 0x30), 0, 0x80, 0x80, 0x80, tmp_str); /* 1417 */
        yofs += 0xe;
    }

    SceneSetSquare(2, 48.0f, (float)(stw->menu_csr * 0xe + 0x4c), 252.0f, 12.0f,
                   0x50, 0x50, 0x64, 0x50);                             /* 1422 */
}

/* --------------------------------------------------------------------------
 *  SceneTestEffectData  (debug menu slot 8)
 *
 *  Parameter editor for the effect selected in SceneTestEffect.  Each effect
 *  interprets scn_eff_ctrl[].eff_data through its own union member.
 * ------------------------------------------------------------------------ */
static void SceneTestEffectData(void)
{
    SCN_TEST_WRK *stw;
    SCN_EFF_CTRL *sec;
    char          tmp_str[256] = "";                                    /* 1434 */

    stw = &scn_test_wrk;
    sec = &scn_eff_ctrl[stw->effect_id];

    if (ScnUDCtrl(&stw->effect_csr, sec->param_num, 0) == -1)           /* 1437 */
    {
        stw->now_menu   = 7;
        stw->effect_id  = 0;
        stw->effect_csr = 0;
        return;
    }

    SetASCIIString2(1, 48.0f, 48.0f, 0, 0x80, 0x80, 0x80, sec->eff_name); /* 1446 */

    switch (stw->effect_id)
    {
        case SCN_DB_EFF_DITHER:
        {
            if (stw->effect_csr == 0)
            {
                ScnValueCtrlC(&sec->eff_data.dither.type, 8, 1, 1, 1);  /* 1453 */
            }
            else if (stw->effect_csr == 1)
            {
                ScnValueCtrl(&sec->eff_data.dither.speed, 90.0f, 0.0f, 0.099999994f, 10.0f); /* 1457 */
            }
            else if (stw->effect_csr == 2)
            {
                ScnValueCtrl(&sec->eff_data.dither.alpha, 127.0f, 0.0f, 0.099999994f, 10.0f);
            }
            else if (stw->effect_csr == 3)
            {
                ScnValueCtrlC(&sec->eff_data.dither.alpmax, 0x80, 0, 1, 8); /* 1459 */
            }
            else if (stw->effect_csr == 4)
            {
                ScnValueCtrlC(&sec->eff_data.dither.colmax, 0x80, 0, 1, 8); /* 1461 */
            }

            sprintf(tmp_str, "TYPE        %d", sec->eff_data.dither.type); /* 1463 */
            SetASCIIString2(1, 48.0f, 76.0f, 0, 0x80, 0x80, 0x80, tmp_str); /* 1464 */
            sprintf(tmp_str, "SPEED       %.1f", sec->eff_data.dither.speed); /* 1466 */
            SetASCIIString2(1, 48.0f, 90.0f, 0, 0x80, 0x80, 0x80, tmp_str); /* 1467 */
            sprintf(tmp_str, "ALPHA       %.1f", sec->eff_data.dither.alpha); /* 1469 */
            SetASCIIString2(1, 48.0f, 104.0f, 0, 0x80, 0x80, 0x80, tmp_str); /* 1470 */
            sprintf(tmp_str, "ALPHA MAX   %d", sec->eff_data.dither.alpmax); /* 1472 */
            SetASCIIString2(1, 48.0f, 118.0f, 0, 0x80, 0x80, 0x80, tmp_str); /* 1473 */
            sprintf(tmp_str, "COLOR MAX   %d", sec->eff_data.dither.colmax); /* 1475 */
            SetASCIIString2(1, 48.0f, 132.0f, 0, 0x80, 0x80, 0x80, tmp_str); /* 1492 */
            break;
        }
        case SCN_DB_EFF_BLUR_N:
        case SCN_DB_EFF_BLUR_B:
        case SCN_DB_EFF_BLUR_W:
        {
            if (stw->effect_csr == 0)
            {
                ScnValueCtrlC(&sec->eff_data.blur.alpha, 0x7f, 0, 1, 8); /* 1504 */
            }
            else if (stw->effect_csr == 1)
            {
                ScnValueCtrlI((int *)&sec->eff_data.blur.scale, 0x44c, 900, 1, 10);
            }
            else if (stw->effect_csr == 2)
            {
                ScnValueCtrlI((int *)&sec->eff_data.blur.rot, 0xe10, 0, 10, 10);
            }

            sprintf(tmp_str, "ALPHA       %d", sec->eff_data.blur.alpha);
            SetASCIIString2(1, 48.0f, 76.0f, 0, 0x80, 0x80, 0x80, tmp_str);
            sprintf(tmp_str, "SCALE       %d", sec->eff_data.blur.scale);
            SetASCIIString2(1, 48.0f, 90.0f, 0, 0x80, 0x80, 0x80, tmp_str);
            sprintf(tmp_str, "ROT         %d", sec->eff_data.blur.rot);
            SetASCIIString2(1, 48.0f, 104.0f, 0, 0x80, 0x80, 0x80, tmp_str);
            break;
        }
        case SCN_DB_EFF_DEFORM:
        {
            if (stw->effect_csr == 0)
            {
                ScnValueCtrlC(&sec->eff_data.deform.type, 7, 1, 1, 1);  /* 1506 */
            }
            else if (stw->effect_csr == 1)
            {
                ScnValueCtrlC(&sec->eff_data.deform.volume, 0x80, 0, 1, 8); /* 1532 */
            }

            sprintf(tmp_str, "TYPE        %d", sec->eff_data.deform.type);
            SetASCIIString2(1, 48.0f, 76.0f, 0, 0x80, 0x80, 0x80, tmp_str);
            sprintf(tmp_str, "VOLUME      %d", sec->eff_data.deform.volume);
            SetASCIIString2(1, 48.0f, 90.0f, 0, 0x80, 0x80, 0x80, tmp_str);
            break;
        }
        case SCN_DB_EFF_FOCUS:
        {
            if (stw->effect_csr == 0)
            {
                ScnValueCtrlC(&sec->eff_data.focus.volume, 0x80, 0, 1, 8); /* 1534 */
            }

            sprintf(tmp_str, "VOLUME      %d", sec->eff_data.focus.volume);
            SetASCIIString2(1, 48.0f, 76.0f, 0, 0x80, 0x80, 0x80, tmp_str);
            break;
        }
        case SCN_DB_EFF_CONTRAST1:
        case SCN_DB_EFF_CONTRAST2:
        case SCN_DB_EFF_CONTRAST3:
        {
            if (stw->effect_csr == 0)
            {
                ScnValueCtrlC(&sec->eff_data.contrast.color, 0xff, 0, 1, 8); /* 1547 */
            }
            else if (stw->effect_csr == 1)
            {
                ScnValueCtrlC(&sec->eff_data.contrast.alpha, 0xff, 0, 1, 8); /* 1549 */
            }

            sprintf(tmp_str, "COLOR       %d", sec->eff_data.contrast.color);
            SetASCIIString2(1, 48.0f, 76.0f, 0, 0x80, 0x80, 0x80, tmp_str);
            sprintf(tmp_str, "ALPHA       %d", sec->eff_data.contrast.alpha);
            SetASCIIString2(1, 48.0f, 90.0f, 0, 0x80, 0x80, 0x80, tmp_str);
            break;
        }
        case SCN_DB_EFF_NEGA:
        {
            if (stw->effect_csr == 0)
            {
                ScnValueCtrlC(&sec->eff_data.nega.color, 0xff, 0, 1, 8); /* 1562 */
            }
            else if (stw->effect_csr == 1)
            {
                ScnValueCtrlC(&sec->eff_data.nega.alpha, 0xff, 0, 1, 8); /* 1564 */
            }
            else if (stw->effect_csr == 2)
            {
                ScnValueCtrlC(&sec->eff_data.nega.alpha2, 0xff, 0, 1, 8); /* 1579 */
            }

            sprintf(tmp_str, "COLOR       %d", sec->eff_data.nega.color);
            SetASCIIString2(1, 48.0f, 76.0f, 0, 0x80, 0x80, 0x80, tmp_str);
            sprintf(tmp_str, "ALPHA       %d", sec->eff_data.nega.alpha);
            SetASCIIString2(1, 48.0f, 90.0f, 0, 0x80, 0x80, 0x80, tmp_str);
            sprintf(tmp_str, "ALPHA2      %d", sec->eff_data.nega.alpha2);
            SetASCIIString2(1, 48.0f, 104.0f, 0, 0x80, 0x80, 0x80, tmp_str);
            break;
        }
        case SCN_DB_EFF_FADE_FRAME:
        case SCN_DB_EFF_CROSS_FADE:
        {
            if (stw->effect_csr == 0)
            {
                ScnValueCtrlC(&sec->eff_data.f_frame.volume, 0xff, 0, 1, 8); /* 1581 */
            }

            sprintf(tmp_str, "VOLUME      %d", sec->eff_data.f_frame.volume);
            SetASCIIString2(1, 48.0f, 76.0f, 0, 0x80, 0x80, 0x80, tmp_str);
            break;
        }
        case SCN_DB_EFF_FADE_SCR:
        {
            if (stw->effect_csr == 0)
            {
                ScnValueCtrlC(&sec->eff_data.fade_scr.r, 0xff, 0, 1, 8); /* 1607 */
                sec->eff_data.fade_scr.g = sec->eff_data.fade_scr.r;
                sec->eff_data.fade_scr.b = sec->eff_data.fade_scr.r;
            }
            else if (stw->effect_csr == 1)
            {
                ScnValueCtrlC(&sec->eff_data.fade_scr.a, 0x80, 0, 1, 8); /* 1617 */
            }

            sprintf(tmp_str, "COLOR      %d", sec->eff_data.fade_scr.r); /* 1609 */
            SetASCIIString2(1, 48.0f, 76.0f, 0, 0x80, 0x80, 0x80, tmp_str);
            sprintf(tmp_str, "ALPHA      %d", sec->eff_data.fade_scr.a);
            SetASCIIString2(1, 48.0f, 90.0f, 0, 0x80, 0x80, 0x80, tmp_str);
            break;
        }
        default:
        {
            break;
        }
    }

    SceneSetSquare(2, 48.0f, (float)(stw->effect_csr * 0xe + 0x4c), 252.0f, 12.0f,
                   0x50, 0x50, 0x64, 0x50);                             /* 1634 */
}

/* --------------------------------------------------------------------------
 *  SceneTestPartsDeformCommon
 *
 *  Character picker shared by the two parts-deform slots.  Row 0 clears the
 *  slot; any other row binds that scene model and drops into its parameter
 *  page (SceneTestEneEffPDeform).
 * ------------------------------------------------------------------------ */
static void SceneTestPartsDeformCommon(int PdeformNo)
{
    SCN_TEST_WRK *stw;
    SCENE_CTRL   *scn_p;
    char          tmp_str[256] = "";                                    /* 1646 */
    int           i;
    int           ret;
    int           yofs;

    stw   = &scn_test_wrk;
    scn_p = SceneCtrlGet(0);                                            /* 1645 */

    ret = ScnUDCtrl(&stw->menu_csr, scn_p->man_mdl_num + 1, 0);         /* 1651 */

    if (ret == -1)
    {
        stw->now_menu = 0;
        stw->menu_csr = 0;
        return;
    }

    if (ret == 1)
    {
        if (stw->menu_csr == 0)
        {
            stw->menu_csr = 0;
            stw->now_menu = 0;
            ScnTestPdeform[PdeformNo].ModelId = -1;
            return;
        }

        if (PdeformNo == 0)
        {
            stw->now_menu = 0xc;
        }
        else
        {
            stw->now_menu = 0xd;
        }
        stw->effect_id  = (PdeformNo != 0);
        stw->effect_csr = 0;
        stw->ene_mdl_no = stw->menu_csr - 1;
    }

    SetASCIIString2(1, 24.0f, 48.0f, 0, 0x80, 0x80, 0x80, "CHARACTER SELECT"); /* 1686 */
    SetASCIIString2(1, 48.0f, 62.0f, 0, 0x80, 0x80, 0x80, "OFF");       /* 1688 */

    yofs = 0x1c;
    for (i = 0; i < scn_p->man_mdl_num; i++)
    {
        sprintf(tmp_str, "%s", scn_p->man_mdl[i].prefix);               /* 1694 */
        SetASCIIString2(1, 48.0f, (float)(yofs + 0x30), 0, 0x80, 0x80, 0x80, tmp_str); /* 1692 */
        yofs += 0xe;
    }

    SceneSetSquare(2, 48.0f, (float)(stw->menu_csr * 0xe + 0x3e), 180.0f, 12.0f,
                   0x50, 0x50, 0x64, 0x50);                             /* 1696 */
}

/* --------------------------------------------------------------------------
 *  SceneTestPartsDeform0  (debug menu slot 9)
 * ------------------------------------------------------------------------ */
static void SceneTestPartsDeform0(void)
{
    SceneTestPartsDeformCommon(0);                                      /* 1704 */
}

/* --------------------------------------------------------------------------
 *  SceneTestPartsDeform1  (debug menu slot 10)
 * ------------------------------------------------------------------------ */
static void SceneTestPartsDeform1(void)
{
    SceneTestPartsDeformCommon(1);                                      /* 1712 */
}

/* --------------------------------------------------------------------------
 *  SceneTestEneEffFire  (debug menu slot 11)
 * ------------------------------------------------------------------------ */
static void SceneTestEneEffFire(void)
{
    SCN_TEST_WRK *stw;
    SCN_ENE_EFCT *see;
    int           i;
    int           ret;
    int           yofs;
    char          tmp_str[256] = "";                                    /* 1726 */
    char         *rgba_str[4]  = { "R     ", "G     ", "B     ", "ALPHA " };
    float         size;
    float         rate;
    u_char        rgba[4];
    SCENE_CTRL   *scn_p;

    stw   = &scn_test_wrk;
    scn_p = SceneCtrlGet(0);                                            /* 1730 */
    see   = scn_p->man_mdl[stw->ene_mdl_no].ene_efct;

    rgba[0] = (u_char)(see->aura_rgba >> 24);
    size    = see->aura_size;
    rate    = see->aura_rate;
    rgba[1] = (u_char)(see->aura_rgba >> 16);
    rgba[2] = (u_char)(see->aura_rgba >> 8);
    rgba[3] = (u_char)see->aura_rgba;

    ret = ScnUDCtrl(&stw->effect_csr, 7, 0);                            /* 1741 */

    if (ret == -1)
    {
        stw->now_menu  = 9;
        stw->effect_id = 0;
        return;
    }

    switch (stw->effect_csr)
    {
        case 0:
        {
            ScnValueCtrl(&size, 20000.0f, 1.0f, 10.0f, 10.0f);          /* 1749 */
            break;
        }
        case 1:
        {
            ScnValueCtrl(&rate, 2.0f, 0.0f, 0.000999999931f, 10.0f);
            break;
        }
        case 2:
        case 3:
        case 4:
        {
            ScnValueCtrlC(&rgba[stw->effect_csr - 2], 0xff, 0, 1, 0x10); /* 1755 */
            break;
        }
        case 5:
        {
            ScnValueCtrlC(&rgba[3], 0x80, 0, 1, 0x10);                  /* 1757 */
            break;
        }
        case 6:
        {
            if (ret == 1)
            {
                SceneReleaseEffect(scn_p);                              /* 1760 */
            }
            break;
        }
        default:
        {
            break;
        }
    }

    sprintf(tmp_str, "ENE EFFECT  %s  FIRE", scn_p->man_mdl[stw->ene_mdl_no].prefix); /* 1781 */
    SetASCIIString2(1, 48.0f, 48.0f, 0, 0x80, 0x80, 0x80, tmp_str);
    sprintf(tmp_str, "  SIZE    : %.1f", size);                         /* 1773 */
    SetASCIIString2(1, 48.0f, 76.0f, 0, 0x80, 0x80, 0x80, tmp_str);     /* 1769 */
    sprintf(tmp_str, "  RATE    : %.6f", rate);                         /* 1777 */
    SetASCIIString2(1, 48.0f, 90.0f, 0, 0x80, 0x80, 0x80, tmp_str);     /* 1774 */

    yofs = 0x38;
    for (i = 0; i < 4; i++)
    {
        sprintf(tmp_str, "  %s   : %d", rgba_str[i], rgba[i]);          /* 1782 */
        SetASCIIString2(1, 48.0f, (float)(yofs + 0x30), 0, 0x80, 0x80, 0x80, tmp_str); /* 1778 */
        yofs += 0xe;
    }
    SetASCIIString2(1, 48.0f, (float)(yofs + 0x30), 0, 0x80, 0x80, 0x80, "  APPLY"); /* 1783 */

    SceneSetSquare(2, 48.0f, (float)(stw->effect_csr * 0xe + 0x4c), 264.0f, 12.0f,
                   0x50, 0x50, 0x64, 0x50);                             /* 1791 */

    see->aura_size = size;
    see->aura_rate = rate;
    see->aura_rgba = ((u_int)rgba[0] << 24) | ((u_int)rgba[1] << 16)
                   | ((u_int)rgba[2] << 8)  | (u_int)rgba[3];
}

/* --------------------------------------------------------------------------
 *  SceneTestEneEffPDeform  (debug menu slots 12 and 13)
 * ------------------------------------------------------------------------ */
static void SceneTestEneEffPDeform(void)
{
    SCN_TEST_WRK     *stw;
    SCENE_CTRL       *scn_p;
    SCN_TEST_PDEFORM *pStp;
    char              tmp_str[256] = "";                                /* 1808 */
    int               ret;

    stw   = &scn_test_wrk;
    scn_p = SceneCtrlGet(0);                                            /* 1806 */
    pStp  = &ScnTestPdeform[stw->effect_id];

    pStp->ModelId = scn_p->man_mdl[stw->ene_mdl_no].mdl_no;

    ret = ScnUDCtrl(&stw->effect_csr, 9, 0);                            /* 1821 */

    if (ret == -1)
    {
        stw->now_menu = 9;
        return;
    }

    switch (stw->effect_csr)
    {
        case 0:
        {
            ScnValueCtrlI((int *)&pStp->Param.type, 0x24, 0, 1, 10);    /* 1828 */
            break;
        }
        case 1:
        {
            ScnValueCtrl(&pStp->Param.sclx, 2.0f, -2.0f, 0.01f, 10.0f); /* 1842 */
            break;
        }
        case 2:
        {
            ScnValueCtrl(&pStp->Param.scly, 2.0f, -2.0f, 0.01f, 10.0f);
            break;
        }
        case 3:
        {
            ScnValueCtrlI((int *)&pStp->Param.alpha, 0x80, 0, 1, 0x10); /* 1834 */
            break;
        }
        case 4:
        {
            ScnValueCtrl(&pStp->Param.rate, 2.0f, 0.0f, 0.01f, 10.0f);
            break;
        }
        case 5:
        {
            ScnValueCtrl(&pStp->Param.trate, 2.0f, 0.0f, 0.01f, 10.0f);
            break;
        }
        case 6:
        {
            ScnValueCtrl(&pStp->WaveSpeed, 2.0f, -2.0f, 0.01f, 10.0f);
            break;
        }
        case 7:
        {
            ScnValueCtrl(&pStp->Distance, 1000.0f, -1000.0f, 1.0f, 10.0f);
            break;
        }
        case 8:
        {
            if (ret == 1)
            {
                ResetEffects(pStp->pEffRet);                            /* 1845 */
                pStp->pEffRet = NULL;
            }
            break;
        }
        default:
        {
            break;
        }
    }

    sprintf(tmp_str, "%s PDEFORM  %d",
            scn_p->man_mdl[stw->ene_mdl_no].prefix, stw->effect_id);    /* 1862 */
    SetASCIIString2(1, 48.0f, 48.0f, 0, 0x80, 0x80, 0x80, tmp_str);     /* 1856 */
    sprintf(tmp_str, "  TYPE    : %d", pStp->Param.type);               /* 1859 */
    SetASCIIString2(1, 48.0f, 76.0f, 0, 0x80, 0x80, 0x80, tmp_str);     /* 1860 */
    sprintf(tmp_str, "  SCALE X : %f", pStp->Param.sclx);               /* 1862 */
    SetASCIIString2(1, 48.0f, 90.0f, 0, 0x80, 0x80, 0x80, tmp_str);     /* 1863 */
    sprintf(tmp_str, "  SCALE Y : %f", pStp->Param.scly);               /* 1865 */
    SetASCIIString2(1, 48.0f, 104.0f, 0, 0x80, 0x80, 0x80, tmp_str);    /* 1866 */
    sprintf(tmp_str, "  ALPHA   : %d", pStp->Param.alpha);              /* 1868 */
    SetASCIIString2(1, 48.0f, 118.0f, 0, 0x80, 0x80, 0x80, tmp_str);    /* 1869 */
    sprintf(tmp_str, "  RATE    : %f", pStp->Param.rate);               /* 1871 */
    SetASCIIString2(1, 48.0f, 132.0f, 0, 0x80, 0x80, 0x80, tmp_str);    /* 1872 */
    sprintf(tmp_str, "  TRATE   : %f", pStp->Param.trate);              /* 1874 */
    SetASCIIString2(1, 48.0f, 146.0f, 0, 0x80, 0x80, 0x80, tmp_str);    /* 1875 */
    sprintf(tmp_str, "  SPEED   : %f", pStp->WaveSpeed);                /* 1877 */
    SetASCIIString2(1, 48.0f, 160.0f, 0, 0x80, 0x80, 0x80, tmp_str);    /* 1878 */
    sprintf(tmp_str, "  DISTANCE: %f", pStp->Distance);                 /* 1880 */
    SetASCIIString2(1, 48.0f, 174.0f, 0, 0x80, 0x80, 0x80, tmp_str);    /* 1881 */
    SetASCIIString2(1, 48.0f, 188.0f, 0, 0x80, 0x80, 0x80, "  APPLY");  /* 1883 */

    SceneSetSquare(2, 48.0f, (float)(stw->effect_csr * 0xe + 0x4c), 264.0f, 12.0f,
                   0x50, 0x50, 0x64, 0x50);                             /* 1887 */
}

/* --------------------------------------------------------------------------
 *  SceneTestEneEffOthers  (debug menu slot 14)
 * ------------------------------------------------------------------------ */
static void SceneTestEneEffOthers(void)
{
    SCN_TEST_WRK *stw;
    SCN_ENE_EFCT *see;
    int           ret;
    char          tmp_str[256] = "";                                    /* 1900 */
    float         dist;
    float         pos_ajst;
    int           alpha;
    SCENE_CTRL   *scn_p;

    stw   = &scn_test_wrk;
    scn_p = SceneCtrlGet(0);                                            /* 1903 */
    see   = scn_p->man_mdl[stw->ene_mdl_no].ene_efct;

    dist     = see->pdf_dist;
    pos_ajst = see->aura_pos_ajst;
    alpha    = see->mdl_alpha;

    ret = ScnUDCtrl(&stw->effect_csr, 4, 0);                            /* 1911 */

    if (ret == -1)
    {
        stw->now_menu  = 9;
        stw->effect_id = 0;
        return;
    }

    sprintf(tmp_str, "ENE EFFECT   %s OTHERS", scn_p->man_mdl[stw->ene_mdl_no].prefix); /* 1915 */
    SetASCIIString2(1, 48.0f, 48.0f, 0, 0x80, 0x80, 0x80, tmp_str);     /* 1922 */

    /* A switch: the ROM pivots on effect_csr == 1 before splitting the low
     * and high sides, GCC 2.96's decision tree for a small case set. */
    switch (stw->effect_csr)
    {
        case 0:
        {
            ScnValueCtrl(&dist, 800.0f, -800.0f, 1.0f, 10.0f);          /* 1928 */
            break;
        }
        case 1:
        {
            ScnValueCtrl(&pos_ajst, 800.0f, -800.0f, 1.0f, 10.0f);
            break;
        }
        case 2:
        {
            ScnValueCtrlI(&alpha, 0x80, 0, 1, 0x10);                    /* 1930 */
            break;
        }
        case 3:
        {
            if (ret == 1)
            {
                SceneReleaseEffect(scn_p);                              /* 1933 */
            }
            break;
        }
        default:
        {
            break;
        }
    }

    sprintf(tmp_str, "  PDEFORM DIST  : %f", dist);                     /* 1938 */
    SetASCIIString2(1, 48.0f, 76.0f, 0, 0x80, 0x80, 0x80, tmp_str);     /* 1939 */
    sprintf(tmp_str, "  AURA POS AJST : %f", pos_ajst);                 /* 1941 */
    SetASCIIString2(1, 48.0f, 90.0f, 0, 0x80, 0x80, 0x80, tmp_str);     /* 1942 */
    sprintf(tmp_str, "  MODEL ALPHA   : %d", alpha);                    /* 1944 */
    SetASCIIString2(1, 48.0f, 104.0f, 0, 0x80, 0x80, 0x80, tmp_str);    /* 1945 */
    SetASCIIString2(1, 48.0f, 118.0f, 0, 0x80, 0x80, 0x80, "  APPLY");  /* 1947 */

    SceneSetSquare(2, 48.0f, (float)(stw->effect_csr * 0xe + 0x4c), 264.0f, 12.0f,
                   0x50, 0x50, 0x64, 0x50);                             /* 1951 */

    see->pdf_dist      = dist;
    see->mdl_alpha     = alpha;
    see->aura_pos_ajst = pos_ajst;
}

/* --------------------------------------------------------------------------
 *  SceneTestEffectTest
 *
 *  Re-issues every enabled post-process effect for this frame.  One typed
 *  SetEffects_* front door per effect id.
 * ------------------------------------------------------------------------ */
static void SceneTestEffectTest(void)
{
    FOD_EFF_DATA *fed;

    if (scn_eff_ctrl[SCN_DB_EFF_Z_DEP].eff_flg != 0)
    {
        SetEffects_Z_DEP(1);                                            /* 1971 */
    }

    if (scn_eff_ctrl[SCN_DB_EFF_MONO].eff_flg == 0)
    {
        mono_flg = 0;
    }
    else if (mono_flg == 0)
    {
        mono_flg = 1;
    }

    if (scn_eff_ctrl[SCN_DB_EFF_DITHER].eff_flg != 0)
    {
        fed = &scn_eff_ctrl[SCN_DB_EFF_DITHER].eff_data;
        SetEffects_DITHER(1, fed->dither.type, fed->dither.alpha,
                          fed->dither.speed, fed->dither.alpmax,
                          fed->dither.colmax, 0, 0, 0);                 /* 1990 */
    }

    if (scn_eff_ctrl[SCN_DB_EFF_BLUR_N].eff_flg != 0)
    {
        fed = &scn_eff_ctrl[SCN_DB_EFF_BLUR_N].eff_data;
        SetEffects_BLUR(0, 1, &fed->blur.alpha, fed->blur.scale,
                        fed->blur.rot, 320.0f, 112.0f);                 /* 1996 */
    }

    if (scn_eff_ctrl[SCN_DB_EFF_BLUR_B].eff_flg != 0)
    {
        fed = &scn_eff_ctrl[SCN_DB_EFF_BLUR_B].eff_data;
        SetEffects_BLUR(0, 1, &fed->blur.alpha, fed->blur.scale,
                        fed->blur.rot, 320.0f, 112.0f);                 /* 2002 */
    }

    if (scn_eff_ctrl[SCN_DB_EFF_BLUR_W].eff_flg != 0)
    {
        fed = &scn_eff_ctrl[SCN_DB_EFF_BLUR_W].eff_data;
        SetEffects_BLUR(0, 1, &fed->blur.alpha, fed->blur.scale,
                        fed->blur.rot, 320.0f, 112.0f);                 /* 2008 */
    }

    if (scn_eff_ctrl[SCN_DB_EFF_DEFORM].eff_flg != 0)
    {
        fed = &scn_eff_ctrl[SCN_DB_EFF_DEFORM].eff_data;
        SetEffects_DEFORM(1, fed->deform.type, fed->deform.volume,
                          0, 0, 0);                                     /* 2013 */
    }

    if (scn_eff_ctrl[SCN_DB_EFF_FOCUS].eff_flg != 0)
    {
        fed = &scn_eff_ctrl[SCN_DB_EFF_FOCUS].eff_data;
        SetEffects_FOCUS(1, fed->focus.volume);                         /* 2018 */
    }

    if (scn_eff_ctrl[SCN_DB_EFF_CONTRAST1].eff_flg != 0)
    {
        fed = &scn_eff_ctrl[SCN_DB_EFF_CONTRAST1].eff_data;
        SetEffects_NCONTRAST(0xd, 1, fed->contrast.color,
                             fed->contrast.alpha);                      /* 2024 */
    }

    if (scn_eff_ctrl[SCN_DB_EFF_CONTRAST2].eff_flg != 0)
    {
        fed = &scn_eff_ctrl[SCN_DB_EFF_CONTRAST2].eff_data;
        SetEffects_NCONTRAST(0xe, 1, fed->contrast.color,
                             fed->contrast.alpha);                      /* 2030 */
    }

    if (scn_eff_ctrl[SCN_DB_EFF_CONTRAST3].eff_flg != 0)
    {
        fed = &scn_eff_ctrl[SCN_DB_EFF_CONTRAST3].eff_data;
        SetEffects_NCONTRAST(0xf, 1, fed->contrast.color,
                             fed->contrast.alpha);                      /* 2036 */
    }

    if (scn_eff_ctrl[SCN_DB_EFF_NEGA].eff_flg != 0)
    {
        fed = &scn_eff_ctrl[SCN_DB_EFF_NEGA].eff_data;
        SetEffects_NEGA(1, fed->nega.color, fed->nega.alpha,
                        0, 0, 0, &fed->nega.alpha2);                    /* 2041 */
    }

    if (scn_eff_ctrl[SCN_DB_EFF_FADE_FRAME].eff_flg != 0)
    {
        fed = &scn_eff_ctrl[SCN_DB_EFF_FADE_FRAME].eff_data;
        SetEffects_FADEFRAME(1, fed->f_frame.volume, 0x10);             /* 2046 */
    }

    if (scn_eff_ctrl[SCN_DB_EFF_CROSS_FADE].eff_flg != 0)
    {
        fed = &scn_eff_ctrl[SCN_DB_EFF_CROSS_FADE].eff_data;
        SetEffects_OVERLAP(1, fed->cross_f.volume);                     /* 2051 */
    }

    if (scn_eff_ctrl[SCN_DB_EFF_FADE_SCR].eff_flg != 0)
    {
        fed = &scn_eff_ctrl[SCN_DB_EFF_FADE_SCR].eff_data;
        SceneSetSquare(1, 0.0f, 0.0f, 640.0f, 448.0f,
                       fed->fade_scr.r, fed->fade_scr.g,
                       fed->fade_scr.b, fed->fade_scr.a);               /* 2059 */
    }

    SceneTestPdeformCtrl();                                             /* 2062 */
}

/* --------------------------------------------------------------------------
 *  SceneTestPdeformCtrl
 *
 *  Keeps the two parts-deform slots in sync with their bound scene model:
 *  drops the effect when the slot is cleared, otherwise refreshes the deform
 *  origin and (re)creates the effect.
 * ------------------------------------------------------------------------ */
static void SceneTestPdeformCtrl(void)
{
    SCN_TEST_PDEFORM *pPdeform;
    int               i;

    for (i = 0; i < 2; i++)
    {
        pPdeform = &ScnTestPdeform[i];

        if (pPdeform->pEffRet != NULL)
        {
            if (pPdeform->ModelId == -1 || pPdeform->Param.type == 0)
            {
                ResetEffects(pPdeform->pEffRet);                        /* 2077 */
                pPdeform->pEffRet = NULL;
            }
        }

        if (pPdeform->ModelId != -1 && pPdeform->Param.type != 0)
        {
            SceneGetModelPDeformPos(pPdeform->Position,
                                    SceneManModelNoChange(pPdeform->ModelId),
                                    pPdeform->Distance);                /* 2083 */

            if (pPdeform->pEffRet == NULL)
            {
                pPdeform->pEffRet =
                    SetEffects_PDEFORM(2,
                                       (u_char)pPdeform->Param.type,
                                       (u_char)pPdeform->Param.alpha,
                                       pPdeform->Param.sclx,
                                       pPdeform->Param.scly,
                                       pPdeform->Position,
                                       0, 0, 0, nullptr,
                                       &pPdeform->WaveSpeed,
                                       &pPdeform->Param.rate,
                                       &pPdeform->Param.trate,
                                       0x80, 0x80, 0x80);               /* 2100 */
            }
        }
    }
}

/* --------------------------------------------------------------------------
 *  SceneTestSetDefEffect
 *
 *  Reloads the factory defaults into the effect bank; run whenever a fresh
 *  scene is selected or the menu is (re)entered.
 * ------------------------------------------------------------------------ */
static void SceneTestSetDefEffect(void)
{
    int i;

    for (i = SCN_DB_EFF_MAX - 1; i >= 0; i--)
    {
        scn_eff_ctrl[i].eff_flg = 0;
    }

    mono_flg = 0;

    memset(&scn_eff_ctrl[SCN_DB_EFF_Z_DEP].eff_data, 0, sizeof(FOD_EFF_DATA));
    memset(&scn_eff_ctrl[SCN_DB_EFF_MONO].eff_data, 0, sizeof(FOD_EFF_DATA));
    memset(&scn_eff_ctrl[SCN_DB_EFF_SEPIA].eff_data, 0, sizeof(FOD_EFF_DATA));

    scn_eff_ctrl[SCN_DB_EFF_DITHER].eff_data.dither.type   = 1;
    scn_eff_ctrl[SCN_DB_EFF_DITHER].eff_data.dither.speed  = 6.0f;
    scn_eff_ctrl[SCN_DB_EFF_DITHER].eff_data.dither.alpha  = 32.0f;
    scn_eff_ctrl[SCN_DB_EFF_DITHER].eff_data.dither.alpmax = 0x40;
    scn_eff_ctrl[SCN_DB_EFF_DITHER].eff_data.dither.colmax = 0x78;

    for (i = SCN_DB_EFF_BLUR_N; i <= SCN_DB_EFF_BLUR_W; i++)
    {
        scn_eff_ctrl[i].eff_data.blur.alpha = 0x40;
        scn_eff_ctrl[i].eff_data.blur.scale = 1010;
        scn_eff_ctrl[i].eff_data.blur.rot   = 1810;
    }

    scn_eff_ctrl[SCN_DB_EFF_DEFORM].eff_data.deform.type      = 1;
    scn_eff_ctrl[SCN_DB_EFF_DEFORM].eff_data.deform.volume    = 0x1e;
    scn_eff_ctrl[SCN_DB_EFF_FOCUS].eff_data.focus.volume      = 0x40;
    scn_eff_ctrl[SCN_DB_EFF_CONTRAST1].eff_data.contrast.color = 0x40;
    scn_eff_ctrl[SCN_DB_EFF_CONTRAST1].eff_data.contrast.alpha = 0x40;
    scn_eff_ctrl[SCN_DB_EFF_CONTRAST2].eff_data.contrast.color = 0x40;
    scn_eff_ctrl[SCN_DB_EFF_CONTRAST2].eff_data.contrast.alpha = 0x40;
    scn_eff_ctrl[SCN_DB_EFF_CONTRAST3].eff_data.contrast.color = 0x40;
    scn_eff_ctrl[SCN_DB_EFF_CONTRAST3].eff_data.contrast.alpha = 0x40;
    scn_eff_ctrl[SCN_DB_EFF_NEGA].eff_data.nega.color         = 0x40;
    scn_eff_ctrl[SCN_DB_EFF_NEGA].eff_data.nega.alpha         = 0x40;
    scn_eff_ctrl[SCN_DB_EFF_NEGA].eff_data.nega.alpha2        = 0x40;
    scn_eff_ctrl[SCN_DB_EFF_FADE_FRAME].eff_data.f_frame.volume = 0x80;
    scn_eff_ctrl[SCN_DB_EFF_CROSS_FADE].eff_data.cross_f.volume = 0x1e;
    scn_eff_ctrl[SCN_DB_EFF_FADE_SCR].eff_data.fade_scr.r     = 0;
    scn_eff_ctrl[SCN_DB_EFF_FADE_SCR].eff_data.fade_scr.g     = 0;
    scn_eff_ctrl[SCN_DB_EFF_FADE_SCR].eff_data.fade_scr.b     = 0;
    scn_eff_ctrl[SCN_DB_EFF_FADE_SCR].eff_data.fade_scr.a     = 0x80;
    scn_eff_ctrl[SCN_DB_EFF_SHIBATA].eff_flg                  = 1;
}

/* --------------------------------------------------------------------------
 *  SceneTestVibrate  (debug menu slot 15)
 * ------------------------------------------------------------------------ */
static void SceneTestVibrate(void)
{
    static int    vib2_val;             /* sdata 0x3f41d0 */
    SCN_TEST_WRK *stw;
    int           i;
    int           yofs;
    char         *vib_menu[3]  = { "ACT1", "ACT2", "BOTH" };
    char          tmp_str[256] = "";                                    /* 2181 */

    stw = &scn_test_wrk;

    if (ScnUDCtrl(&stw->menu_csr, 3, 0) == -1)                          /* 2183 */
    {
        stw->now_menu = 0;
        stw->menu_csr = 0;
        return;
    }

    if (stw->menu_csr == 1)
    {
        ScnValueCtrlI(&vib2_val, 0xff, 0, 1, 0x10);                     /* 2190 */
    }

    if ((pad[0].now & 0x20U) != 0)
    {
        /* A switch, not an if chain: the ROM tests menu_csr == 1 first and
         * only then splits the low and high sides, which is GCC 2.96's
         * decision tree for a three-case switch. */
        switch (stw->menu_csr)
        {
            case 0:
            {
                VibrateRequest1(0, 1);                                  /* 2194 */
                break;
            }
            case 1:
            {
                VibrateRequest2(0, vib2_val);                           /* 2196 */
                break;
            }
            case 2:
            {
                VibrateRequest(0, 1, vib2_val);                         /* 2198 */
                break;
            }
            default:
            {
                break;
            }
        }
    }

    SetASCIIString2(1, 48.0f, 48.0f, 0, 0x80, 0x80, 0x80, "VIBRATE TEST"); /* 2205 */

    yofs = 0x1c;
    for (i = 0; i < 3; i++)
    {
        if (i == 1)
        {
            sprintf(tmp_str, "  %s  : %d", vib_menu[i], vib2_val);      /* 2209 */
        }
        else
        {
            sprintf(tmp_str, "  %s", vib_menu[i]);                      /* 2211 */
        }
        SetASCIIString2(1, 48.0f, (float)(yofs + 0x30), 0, 0x80, 0x80, 0x80, tmp_str); /* 2213 */
        yofs += 0xe;
    }

    SceneSetSquare(2, 48.0f, (float)(stw->menu_csr * 0xe + 0x4c), 216.0f, 12.0f,
                   0x50, 0x50, 0x64, 0x50);                             /* 2218 */
}

/* --------------------------------------------------------------------------
 *  ScnUDCtrl
 *
 *  Up / down step the cursor, circle decides, cross cancels.
 * ------------------------------------------------------------------------ */
static int ScnUDCtrl(int *csr, int max, int min)
{
    int ret;

    ret = 0;

    if ((pad[0].rpt & 0x4000U) != 0)
    {
        (*csr)++;
        if (*csr >= max)
        {
            *csr = 0;
        }
    }
    else if ((pad[0].rpt & 0x1000U) != 0)
    {
        (*csr)--;
        if (*csr < 0)
        {
            *csr = max - 1;
        }
    }
    else if ((pad[0].one & 0x20U) != 0)
    {
        ret = 1;
    }
    else if ((pad[0].one & 0x40U) != 0)
    {
        ret = -1;
    }

    return ret;
}

/* --------------------------------------------------------------------------
 *  ScnValueCtrl
 *
 *  Left / right nudge a float by `inc`, square multiplies the step by `mul`
 *  and triangle by a further 10x; circle / cross snap to max / min.  The
 *  value clamps at both ends.  Returns 1 when it changed.
 * ------------------------------------------------------------------------ */
static int ScnValueCtrl(float *val, float max, float min, float inc, float mul)
{
    int ret;

    ret = 0;

    if ((pad[0].one & 2U) != 0)
    {
        *val = max;
        ret  = 1;
    }
    if ((pad[0].one & 1U) != 0)
    {
        *val = min;
        ret  = 1;
    }
    if ((pad[0].now & 8U) != 0)
    {
        inc = inc * mul;
    }
    if ((pad[0].now & 4U) != 0)
    {
        inc = inc * mul * 10.0f;
    }

    if ((pad[0].rpt & 0x2000U) != 0)
    {
        *val += inc;
        if (*val > max)
        {
            *val = max;
        }
        ret = 1;
    }
    else if ((pad[0].rpt & 0x8000U) != 0)
    {
        *val -= inc;
        if (*val < min)
        {
            *val = min;
        }
        ret = 1;
    }

    return ret;
}

/* --------------------------------------------------------------------------
 *  ScnValueCtrlI
 *
 *  Integer form of ScnValueCtrl.  Unlike the float version this one wraps
 *  around instead of clamping.
 * ------------------------------------------------------------------------ */
static int ScnValueCtrlI(int *val, int max, int min, int inc, int mul)
{
    int ret;

    ret = 0;

    if ((pad[0].one & 2U) != 0)
    {
        *val = max;
        ret  = 1;
    }
    if ((pad[0].one & 1U) != 0)
    {
        *val = min;
        ret  = 1;
    }
    if ((pad[0].now & 8U) != 0)
    {
        inc = inc * mul;
    }

    if ((pad[0].rpt & 0x2000U) != 0)
    {
        *val += inc;
        if (*val > max)
        {
            *val = min;
        }
        ret = 1;
    }
    else if ((pad[0].rpt & 0x8000U) != 0)
    {
        *val -= inc;
        if (*val < min)
        {
            *val = max;
        }
        ret = 1;
    }

    return ret;
}

/* --------------------------------------------------------------------------
 *  ScnValueCtrlC
 *
 *  u_char form of ScnValueCtrlI, with a final clamp into [min, max] to catch
 *  the 8-bit wrap.
 * ------------------------------------------------------------------------ */
static int ScnValueCtrlC(u_char *val, u_char max, u_char min, int inc, int mul)
{
    int ret;

    ret = 0;

    if ((pad[0].one & 2U) != 0)
    {
        *val = max;
        ret  = 1;
    }
    if ((pad[0].one & 1U) != 0)
    {
        *val = min;
        ret  = 1;
    }
    if ((pad[0].now & 8U) != 0)
    {
        inc = inc * mul;
    }

    if ((pad[0].rpt & 0x2000U) != 0)
    {
        *val = (u_char)(*val + inc);
        if (*val > max)
        {
            *val = min;
        }
        ret = 1;
    }
    else if ((pad[0].rpt & 0x8000U) != 0)
    {
        *val = (u_char)(*val - inc);
        if (*val < min)
        {
            *val = max;
        }
        ret = 1;
    }

    if (*val > max)
    {
        *val = max;
    }
    if (*val < min)
    {
        *val = min;
    }

    return ret;
}

/* --------------------------------------------------------------------------
 *  SceneFileSaveBin
 *
 *  Dumps the tuned light bank to host0: as scene<name>.slt -- light count,
 *  then one FOD_LIT_SERIAL + G3DLIGHT pair per light, the fog block, and the
 *  six ambient banks (each padded out to a quadword with an "AMB" tag).
 * ------------------------------------------------------------------------ */
static int SceneFileSaveBin(void)
{
    float       amb[4];
    int         i;
    int         fd;
    int         align128[4];
    char        fname[256] = "";                                        /* 2354 */
    SCENE_CTRL *scn_p;
    FOD_LIGHT  *scn_fl;

    memset(align128, 0, sizeof(align128));

    scn_p  = SceneCtrlGet(0);                                           /* 2357 */
    scn_fl = &scn_p->fod_ctrl.fod_light;    /* one SceneCtrlGet in the ROM, not two */

    sprintf(fname, "host0:../bin/data/scene/scene%s.slt", scene_name[scn_p->scene_no]); /* 2362 */

    fd = sceOpen(fname, SCE_CREAT | SCE_TRUNC | SCE_WRONLY);            /* 2363 */

    if (fd != 0)
    {
        printf("Warning!! Light File Save Fail !!\n");                  /* 2365 */
        printf("File Name <%s>\n", fname);                              /* 2366 */
        return 1;
    }

    sceLseek(fd, 0, 0);                                                 /* 2369 */

    align128[0] = scn_fl->all_lit_num;
    sceWrite(fd, align128, sizeof(align128));                           /* 2372 */

    for (i = 0; i < (int)scn_fl->all_lit_num; i++)
    {
        sceWrite(fd, &scn_fl->lit_serial[i], sizeof(FOD_LIT_SERIAL));   /* 2377 */
        sceWrite(fd, &scn_fl->all_lit[i], sizeof(G3DLIGHT));            /* 2378 */
    }

    sceWrite(fd, &scn_p->fog, sizeof(SCENE_FOG));                       /* 2381 */

    for (i = 0; i < FOD_AMBIENT_MAX; i++)
    {
        g3dxVu0CopyVector(amb, scn_fl->amb[i]);
        ((char *)&amb[3])[0] = 'A';
        ((char *)&amb[3])[1] = 'M';
        ((char *)&amb[3])[2] = 'B';
        ((char *)&amb[3])[3] = '\0';
        sceWrite(fd, amb, sizeof(amb));                                 /* 2390 */
    }

    sceClose(fd);                                                       /* 2393 */
    /* &fname[6] skips the "host0:" prefix -- the ROM prints the bare path. */
    printf("Light File [%s] Save Complete !!\n", &fname[6]);            /* 2394 */

    return 0;
}

/* --------------------------------------------------------------------------
 *  SceneFileSaveText
 *
 *  Human-readable companion to SceneFileSaveBin, written as
 *  scene<name>-lit.txt.
 * ------------------------------------------------------------------------ */
static int SceneFileSaveText(void)
{
    float       ambient[4];
    int         i;
    int         fd;
    char       *ltype_name[4] = { "AMB  ", "INF  ", "SPOT ", "POINT" };
    char       *amb_str[6]    = { "COMMON", "CHARA ", "ROOM  ", "FURN  ", "DOOR  ", "ITEM  " };
    char        fname[256]    = "";                                     /* 2414 */
    char        line[256]     = "";                                     /* 2415 */
    SCENE_CTRL *scn_p;
    FOD_LIGHT  *scn_fl;

    scn_p  = SceneCtrlGet(0);                                           /* 2416 */
    scn_fl = SceneFodLightPtrGet(0);                                    /* 2417 */

    sprintf(line, "host0:../bin/data/scene/scene%s-lit.txt", scene_name[scn_p->scene_no]); /* 2421 */
    line[strlen(line)] = '\0';                                          /* 2422 */
    strcpy(fname, line);                                                /* 2425 */

    fd = sceOpen(fname, SCE_CREAT | SCE_WRONLY);                        /* 2426 */

    if (fd != 0)
    {
        printf("Warning!! Light File Save Fail !!\n");                  /* 2428 */
        printf("File Name <%s>\n", fname);                              /* 2429 */
        return 1;
    }

    sceLseek(fd, 0, 0);                                                 /* 2432 */

    sprintf(line, "=============================================\n");   /* 2434 */
    sceWrite(fd, line, strlen(line));                                   /* 2435 */
    sprintf(line, "AMBIENT\n");                                         /* 2436 */
    sceWrite(fd, line, strlen(line));                                   /* 2437 */

    for (i = 0; i < FOD_AMBIENT_MAX; i++)
    {
        g3dxVu0CopyVector(ambient, scn_fl->amb[i]);
        sprintf(line, "  %s = [R : %.3f] [G : %.3f] [B : %.3f]\n",
                amb_str[i], ambient[0], ambient[1], ambient[2]);        /* 2441 */
        sceWrite(fd, line, strlen(line));                               /* 2442 */
    }

    sprintf(line, "=============================================\n");   /* 2444 */
    sceWrite(fd, line, strlen(line));                                   /* 2445 */

    for (i = 0; i < (int)scn_fl->all_lit_num; i++)
    {
        sprintf(line, "=============================================\n"); /* 2452 */
        sceWrite(fd, line, strlen(line));                               /* 2453 */
        sprintf(line, "No.%02d : %s : %s\n",
                scn_fl->lit_serial[i].light_no,
                ltype_name[scn_fl->lit_serial[i].light_type],
                scn_fl->lit_serial[i].light_name);                      /* 2456 */
        sceWrite(fd, line, strlen(line));                               /* 2457 */
        sprintf(line, "  [R : %.3f] [G : %.3f] [B : %.3f]\n",
                scn_fl->all_lit[i].vDiffuse[0],
                scn_fl->all_lit[i].vDiffuse[1],
                scn_fl->all_lit[i].vDiffuse[2]);                        /* 2461 */
        sceWrite(fd, line, strlen(line));                               /* 2462 */

        if (scn_fl->lit_serial[i].light_type == 2 || scn_fl->lit_serial[i].light_type == 3)
        {
            sprintf(line, "  [POWER : %.2f]\n", scn_fl->all_lit[i].fMaxRange); /* 2466 */
            sceWrite(fd, line, strlen(line));                           /* 2467 */

            if (scn_fl->lit_serial[i].light_type == 2)
            {
                sprintf(line, "  [CONE : %.1f]\n",
                        acosf(g3dxVu0Sqrt(scn_fl->all_lit[i].afPad0[0])) * 180.0f / 3.1415925f); /* 2472 */
                sceWrite(fd, line, strlen(line));                       /* 2475 */
            }
        }

        sceWrite(fd, "\n", 1);                                          /* 2478 */
    }

    sprintf(line, "\n=============================================\n"); /* 2481 */
    sceWrite(fd, line, strlen(line));                                   /* 2482 */
    sprintf(line, "                fog data\n");                        /* 2483 */
    sceWrite(fd, line, strlen(line));                                   /* 2484 */
    sprintf(line, "=============================================\n");   /* 2485 */
    sceWrite(fd, line, strlen(line));                                   /* 2486 */
    sprintf(line, " r : g : b \n");                                     /* 2487 */
    sceWrite(fd, line, strlen(line));                                   /* 2488 */
    sprintf(line, "%d : %d : %d \n\n", scn_p->fog.r, scn_p->fog.g, scn_p->fog.b); /* 2490 */
    sceWrite(fd, line, strlen(line));                                   /* 2491 */
    sprintf(line, "near : far : min : max \n");                         /* 2492 */
    sceWrite(fd, line, strlen(line));                                   /* 2493 */
    sprintf(line, "%.1f : %.1f : %.1f : %.1f \n\n",
            scn_p->fog.near, scn_p->fog.far, scn_p->fog.min, scn_p->fog.max); /* 2495 */
    sceWrite(fd, line, strlen(line));                                   /* 2496 */

    sceClose(fd);                                                       /* 2498 */
    /* &fname[6] skips the "host0:" prefix -- the ROM prints the bare path. */
    printf("Light File [%s] Save Complete !!\n", &fname[6]);            /* 2499 */

    return 0;
}

/* --------------------------------------------------------------------------
 *  SceneTestDrawCrossLine
 *
 *  Three axis-aligned segments of `LineLength` centred on CenterPos, used to
 *  mark light positions in the world.
 * ------------------------------------------------------------------------ */
void SceneTestDrawCrossLine(float *CenterPos, float LineLength, int r, int g, int b, int a)
{
    float LinePos1[4];
    float LinePos2[4];
    float fHalf;

    fHalf = LineLength * 0.5f;

    g3dxVu0CopyVector(LinePos1, CenterPos);
    g3dxVu0CopyVector(LinePos2, CenterPos);
    LinePos1[0] = CenterPos[0] - fHalf;
    LinePos2[0] = CenterPos[0] + fHalf;
    DrawLine(LinePos1, (u_char)r, (u_char)g, (u_char)b, (u_char)a,
             LinePos2, (u_char)r, (u_char)g, b, a);                     /* 2518 */

    g3dxVu0CopyVector(LinePos1, CenterPos);
    g3dxVu0CopyVector(LinePos2, CenterPos);
    LinePos1[1] = CenterPos[1] - fHalf;
    LinePos2[1] = CenterPos[1] + fHalf;
    DrawLine(LinePos1, (u_char)r, (u_char)g, (u_char)b, (u_char)a,
             LinePos2, (u_char)r, (u_char)g, b, a);                     /* 2524 */

    g3dxVu0CopyVector(LinePos1, CenterPos);
    g3dxVu0CopyVector(LinePos2, CenterPos);
    LinePos1[2] = CenterPos[2] - fHalf;
    LinePos2[2] = CenterPos[2] + fHalf;
    DrawLine(LinePos1, (u_char)r, (u_char)g, (u_char)b, (u_char)a,
             LinePos2, (u_char)r, (u_char)g, b, a);                     /* 2530 */
}

/* --------------------------------------------------------------------------
 *  SceneTestIsMenuMode
 *
 *  Dead code: exported, but a scan of every jal/j in the loadable segments
 *  finds no call site anywhere in the ROM.  SceneTestEffectFlgGet beside it has
 *  twelve, all in scene_effect.o.
 * ------------------------------------------------------------------------ */
int SceneTestIsMenuMode(void)
{
    return (scn_test_wrk.mode == SCN_TEST_MENU);
}

/* --------------------------------------------------------------------------
 *  SceneTestEffectFlgGet
 * ------------------------------------------------------------------------ */
int SceneTestEffectFlgGet(SCN_DB_EFF_TYPE type)
{
    return scn_eff_ctrl[type].eff_flg;
}

/* --------------------------------------------------------------------------
 *  SceneTestRopeReleaseWorkAll
 *
 *  Drops every rope accessory work slot when a scene ends, so the next scene
 *  does not inherit them.
 * ------------------------------------------------------------------------ */
static void SceneTestRopeReleaseWorkAll(void)
{
    int i;
    int FurnId;

    for (i = 0; i < 20; i++)
    {
        FurnId = acsRopeGetFurnID(i);                                   /* 2556 */
        if (FurnId >= 0)
        {
            acsRopeReleaseWork(FurnId);                                 /* 2559 */
        }
    }
}
