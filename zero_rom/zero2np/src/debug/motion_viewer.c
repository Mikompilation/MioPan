// FILE: /home/zero_rom/zero2np/src/debug/motion_viewer.c
//
// Character model / animation viewer -- the tool behind
// DEBUG_WRK::init_motionviewer.
//
// PORT ADDITION.  The third of the seven sub-tools DEBUG_WRK names and the
// Feb 6 2004 prototype ships no code for: ZERO2.MAP has no motion_viewer
// object, dbg_menu_data[] is three entries wide, and the flag plus its
// InitDebug() clear are all that survived.  Everything below is ours.  Same
// standing as debug/camera_menu.c.
//
// Unlike the two 2D tools beside it this one drives real subsystems, so it is
// built out of an existing working path rather than invented.  The template is
// effect_butterfly.c, which is the only place in the tree that loads a
// character model, binds an ANI_CTRL to it and draws it without also being a
// player, a ghost or a room:
//
//     load     mmanageReqMdl / mmanageReqAnm, then poll mmanageIsReady*
//     bind     motClearANI_CTRL + motInitAniCtrlMalloc      (butterfly 206-208)
//     pose     mimSetVertex + SetCoordinate                 (butterfly 244-247)
//     draw     SendEneVram + _gra3dDrawSGD + DrawEneSubObj
//              + _SetPREVIOUSTRI2PRIM                       (butterfly 264-271)
//     release  motInitAniCtrlFree, mmanageClearMdl / ClearAnm
//
// ONE DELIBERATE DEPARTURE FROM THAT PATH, and it is the point of the tool.
// The butterfly advances its animation with motSetCoord(), which runs the
// ANI_CODE script: at the end of a clip motGetNextMotion() consults the
// model's own animation-code table and moves to whatever it nominates.  That
// is right for a butterfly and wrong for a viewer, which has to hold the clip
// the operator asked for.  This drives motSetCoordFrame(ac, frame) instead --
// the absolute-position primitive underneath, which poses the skeleton at a
// given sub-frame and consults nothing.  So the clip is whatever play_id says
// and the frame is whatever the scrubber says, both of them ours.
//
// Clip selection is motInitMotCtrlEx(&mot, mot.top, mot.rst0, play_id).  Its
// own range check is `play_id >= *mot_addr`, so *mot.top is the number of
// clips in the pak -- that is where CLIP n/N comes from, and there is no other
// count of it anywhere.  Re-seating the same rst0 it already holds is
// deliberate: rst1 is derived as rst0 + motGetBoneNum(dat), the skeleton does
// not change between clips of one pak, so the pose buffers stay put.
//
// PLACEMENT.  SetCoordinate() -> SetRT2BaseMtx() seeds the root from
// g_matConvertSI2PS = diag(25, -25, -25, 1), so world units are 25x the SI
// model's and BOTH Y AND Z ARE NEGATED -- Y grows downwards.  Every camera
// constant below reads inverted because of it: MV_CAM_HEIGHT is negative to
// put the eye *above* the model, and the pitch limit leans the same way.  A
// standing character is roughly a thousand units tall.
//
// LIGHTING is the tool's own two-point rig, not the game's.  playerSetLight()
// builds its GRA3DLIGHTDATA from MapDrawGetLightPtr(GetPlyrAreaNo()), and no
// room is loaded here, so there is nothing to inherit; without a rig of its
// own the model draws black (an object with no light is not a texture fault --
// it is the commonest way a model "does not render").
//
// HEAP.  motInitAniCtrlMalloc() carves the animation work area out of
// ol_loadGetHeap(), and mmanage's slots come off the same loader, so the tool
// resets the out-game heap exactly as scn_test.c does (293-294) and pumps
// ol_load.Main() every frame.  Nothing else is resident at this point: the
// title released its assets on the way into the debug menu.

#include "motion_viewer.h"

#include "zero2_debug.h"

#include "../common/ol_load.h"                   // ol_load / ol_loadHeapReset
#include "../common/variable.h"                  // pad / key_now
#include "../graphics/graph2d/message.h"         // SetASCIIString2 / SetString2
#include "../graphics/graph3d/g3dLight.h"        // g3dutilSetLightDefault
#include "../graphics/graph3d/gra3d.h"           // camera / light / gra3dDraw
#include "../graphics/graph3d/gra3dSGD.h"        // _gra3dDrawSGD / _SetPREVIOUSTRI2PRIM
#include "../graphics/graph3d/sgd_types.h"       // SRT_REALTIME / HeaderSection
#include "../graphics/mmanage.h"                 // mmanageReq* / mmanageIsReady* / mmanageClear*
#include "../graphics/motion/mdlwork.h"          // ANI_CTRL / SendEneVram / DrawEneSubObj
#include "../graphics/motion/mim.h"              // mimSetVertex
#include "../graphics/motion/motion.h"           // motInitAniCtrlMalloc / motSetCoordFrame
#include "../system/os/system.h"                 // GetPALMode
#include "../system/pad/pad.h"                   // paddat / GetPadAnalogRpt

#include <math.h>
#include <string.h>

#define MV_SCREEN_W         640
#define MV_SCREEN_H         448
#define MV_LINE_H           16

/* Character model / animation numbers are the mmanage family indices, i.e.
 * offsets from CH000_MIO_MDL (303) and CH000_MIO_ANM (382).  The cursors are
 * clamped rather than bounded by a table: no list of which numbers exist is
 * recoverable, and a number with no file simply never becomes ready -- which
 * the LOAD page reports rather than hanging on. */
#define MV_MDL_MAX          63
#define MV_ANM_MAX          63

/* Frames to wait for a load before calling it absent.  The loader retries a
 * WAIT_MEMORY slot every Main(), so this has to be long enough to outlast a
 * real disc read and short enough not to look hung. */
#define MV_LOAD_TIMEOUT     600

/* Camera.  World units are 25x the SI model and Y grows downwards, so the
 * height is negative to put the eye above the model's root. */
#define MV_CAM_DIST_MIN     600.0f
#define MV_CAM_DIST_MAX     6000.0f
#define MV_CAM_DIST_INIT    2600.0f
#define MV_CAM_DIST_STEP    60.0f
#define MV_CAM_HEIGHT      -900.0f
#define MV_CAM_TARGET_Y    -700.0f
#define MV_CAM_YAW_STEP     0.045f
#define MV_CAM_PITCH_STEP   0.030f
#define MV_CAM_PITCH_LIMIT  1.30f
#define MV_CAM_FOV          0.55f

#define MV_PI               3.1415927f

#define MV_SEL_ROWS         2

#define MV_STATE_SELECT     0
#define MV_STATE_LOAD       1
#define MV_STATE_VIEW       2

typedef struct
{
    int   state;
    int   csr;              /* select page row: 0 model, 1 anim            */
    int   mdl_no;
    int   anm_no;
    int   req_mdl;          /* what was actually requested, -1 if nothing  */
    int   req_anm;
    int   bound;            /* an ANI_CTRL is live and must be freed       */
    int   load_timer;
    int   failed;

    int   clip;
    int   frame;            /* scrub position, in mot.cnt units            */
    int   playing;
    int   speed;            /* frames advanced per tick when playing       */
    int   turntable;

    float yaw;
    float pitch;
    float dist;
    float model_rot;
} MOTION_VIEWER_WRK;

static MOTION_VIEWER_WRK mv_wrk;

/* The viewer's own ANI_CTRL, not one of motGetANI_CTRL()'s pool slots: the
 * pool is the game's and motSearchANI_CTRL() walks it, so a viewer entry
 * sitting in it would be visible to code that has no idea this tool exists.
 * effect_butterfly.c owns its ANI_CTRLs the same way. */
static ANI_CTRL mv_ani_ctrl;

static char s_mv_title[]     = "MOTION VIEWER";
static char s_mv_sel_hdr[]   = "SELECT MODEL AND ANIMATION";
static char s_mv_row_mdl[]   = "MODEL NO";
static char s_mv_row_anm[]   = "ANIM  NO";
static char s_mv_sel_help1[] = "UP/DOWN ROW   LEFT/RIGHT VALUE   L1/R1 x10";
static char s_mv_sel_help2[] = "CROSS LOAD   TRIANGLE EXIT";
static char s_mv_loading[]   = "LOADING  mdl %d  anm %d   %d";
static char s_mv_failed[]    = "LOAD FAILED -- no such model or animation";
static char s_mv_failed2[]   = "CROSS or TRIANGLE to go back";
static char s_mv_bindfail[]  = "BIND FAILED -- no room on the load heap";
static char s_mv_cursor[]    = "o";

static char s_mv_info1[]     = "MDL %3d   ANM %3d   CLIP %3d / %3d";
static char s_mv_info2[]     = "SUBFRAME %4d / %4d   %s   SPEED %d";
static char s_mv_info3[]     = "YAW %4d   PITCH %4d   DIST %5d   TURN %s";
static char s_mv_playing[]   = "PLAY  ";
static char s_mv_paused[]    = "PAUSE ";
static char s_mv_on[]        = "ON";
static char s_mv_off[]       = "OFF";
static char s_mv_help1[]     = "L1/R1 CLIP   CROSS PLAY   SQUARE STEP";
static char s_mv_help2[]     = "DPAD ORBIT   L2/R2 ZOOM   CIRCLE TURNTABLE";
static char s_mv_help3[]     = "START SPEED   TRIANGLE BACK";

/* --------------------------------------------------------------------------
 *  Small helpers
 * ------------------------------------------------------------------------ */

/* pad[0].rpt is the remapped d-pad word: 0x1000 UP, 0x4000 DOWN, 0x8000 LEFT,
 * 0x2000 RIGHT, with GetPadAnalogRpt() indices 0 up, 1 down, 2 left, 3 right.
 * The folder's own idiom. */
static int MvRpt(u_int mask, int analog)
{
    return ((pad[0].rpt & mask) != 0) || (GetPadAnalogRpt(analog) != 0);
}

static int MvClampi(int v, int lo, int hi)
{
    if (v < lo)
    {
        return lo;
    }
    if (v > hi)
    {
        return hi;
    }
    return v;
}

static float MvClampf(float v, float lo, float hi)
{
    if (v < lo)
    {
        return lo;
    }
    if (v > hi)
    {
        return hi;
    }
    return v;
}

/* Number of clips in the bound animation pak.  motInitMotCtrlEx() range-checks
 * play_id against *mot_addr, so the pak's first word is the count; there is no
 * other record of it. */
static int MvClipCount(void)
{
    if (mv_wrk.bound == 0 || mv_ani_ctrl.mot.top == (u_int *)0)
    {
        return 0;
    }
    return (int)*mv_ani_ctrl.mot.top;
}

/* Length of the current clip in the units motSetCoordFrame() takes.
 *
 * THESE ARE NOT mot.cnt's UNITS, and the difference is the whole reason this
 * helper exists.  mot.cnt and mot.all_cnt count *keys*: motSetCoord() indexes
 * the frame-data table with `mot->cnt % mot->all_cnt` directly.
 * motSetCoordFrame() instead takes an interpolated sub-frame and divides it
 * down -- `key = frame / step`, `inp_cnt = frame % step` -- which is exactly
 * the inverse of motGetNowFrame()'s `cnt * step + inp_cnt`.  So the scrub
 * range is all_cnt keys times step sub-frames each, and using all_cnt on its
 * own would play only the first 1/step of every clip.
 *
 * step is motGetInterpFrameNum(dat) * 2 + 2, which is static to motion.c --
 * but motInitMotCtrlEx() and motSetCoordFrame() both park it in inp_allcnt,
 * so it is readable here.  It is at least 2 by construction; the clamp is for
 * the frame between a bind and the first motSetCoordFrame(). */
static int MvFrameCount(void)
{
    int step;

    if (mv_wrk.bound == 0 || mv_ani_ctrl.mot.all_cnt <= 0)
    {
        return 1;
    }

    step = mv_ani_ctrl.mot.inp_allcnt;
    if (step < 1)
    {
        step = 2;
    }

    return mv_ani_ctrl.mot.all_cnt * step;
}

/* --------------------------------------------------------------------------
 *  Resource handling
 * ------------------------------------------------------------------------ */

/* Give back everything claimed, in the reverse of the order it was taken:
 * the animation work area first (it was carved out of the load heap after the
 * files landed), then the two loader slots. */
static void MvRelease(void)
{
    if (mv_wrk.bound != 0)
    {
        motInitAniCtrlFree(&mv_ani_ctrl);
        motClearANI_CTRL(&mv_ani_ctrl);
        mv_wrk.bound = 0;
    }

    if (mv_wrk.req_mdl >= 0)
    {
        mmanageClearMdl(mv_wrk.req_mdl);
        mv_wrk.req_mdl = -1;
    }

    if (mv_wrk.req_anm >= 0)
    {
        mmanageClearAnm(mv_wrk.req_anm);
        mv_wrk.req_anm = -1;
    }
}

static void MvBeginLoad(void)
{
    MvRelease();

    mv_wrk.req_mdl    = mv_wrk.mdl_no;
    mv_wrk.req_anm    = mv_wrk.anm_no;
    mv_wrk.load_timer = 0;
    mv_wrk.failed     = 0;
    mv_wrk.state      = MV_STATE_LOAD;

    mmanageReqMdl(mv_wrk.req_mdl);
    mmanageReqAnm(mv_wrk.req_anm);
}

/* Select a clip.  Re-seating mot.rst0 as the base is identity -- rst1 is
 * derived from it and the bone count does not change within one pak. */
static void MvSetClip(int clip)
{
    int clips = MvClipCount();

    if (clips <= 0)
    {
        return;
    }

    /* motInitMotCtrlEx() returns null two ways -- an out-of-range play_id,
     * which the modulo has just excluded, and a null rst_addr, which is the
     * real precondition and is checked here instead.  Its return is the end of
     * the work area, not a status. */
    if (mv_ani_ctrl.mot.rst0 == (RST_DATA *)0)
    {
        return;
    }

    clip = (clip % clips + clips) % clips;

    motInitMotCtrlEx(&mv_ani_ctrl.mot, mv_ani_ctrl.mot.top,
                     (u_int *)mv_ani_ctrl.mot.rst0, clip);

    mv_wrk.clip  = clip;
    mv_wrk.frame = 0;
}

/* Poll the two loads and, once both have landed, bind an ANI_CTRL over them.
 * The IsReady() out-parameters are the only way to get the addresses. */
static void MvPollLoad(void)
{
    void *mdl_p = (void *)0;
    void *anm_p = (void *)0;

    mv_wrk.load_timer++;

    if (mmanageIsReadyMdl(mv_wrk.req_mdl, &mdl_p, 0) == 0 ||
        mmanageIsReadyAnm(mv_wrk.req_anm, &anm_p, 0) == 0)
    {
        if (mv_wrk.load_timer > MV_LOAD_TIMEOUT)
        {
            mv_wrk.failed = 1;
        }
        return;
    }

    motClearANI_CTRL(&mv_ani_ctrl);

    if (motInitAniCtrlMalloc(&mv_ani_ctrl, (u_int *)anm_p, (u_int *)mdl_p,
                             (u_int)mv_wrk.req_mdl, (u_int)mv_wrk.req_anm)
        == (u_int *)0)
    {
        /* The work area would not fit.  Nothing was allocated, so there is
         * nothing to free -- but the loader slots still have to go back. */
        mv_wrk.failed = 2;
        return;
    }

    mv_wrk.bound     = 1;
    mv_wrk.clip      = 0;
    mv_wrk.frame     = 0;
    mv_wrk.playing   = 1;
    mv_wrk.speed     = 1;
    mv_wrk.model_rot = 0.0f;
    mv_wrk.state     = MV_STATE_VIEW;

    MvSetClip(0);
}

/* --------------------------------------------------------------------------
 *  Camera and lights
 *
 *  Both are re-applied every frame rather than set once: gra3dDraw() consumes
 *  the live state, and nothing else here is putting it back.
 * ------------------------------------------------------------------------ */

static void MvApplyCamera(void)
{
    float eye[4];
    float target[4];
    float horiz;

    target[0] = 0.0f;
    target[1] = MV_CAM_TARGET_Y;
    target[2] = 0.0f;
    target[3] = 1.0f;

    /* Pitch leans the eye along -Y because that is up in this space. */
    horiz = cosf(mv_wrk.pitch) * mv_wrk.dist;

    eye[0] = target[0] + sinf(mv_wrk.yaw) * horiz;
    eye[1] = MV_CAM_HEIGHT - sinf(mv_wrk.pitch) * mv_wrk.dist;
    eye[2] = target[2] + cosf(mv_wrk.yaw) * horiz;
    eye[3] = 1.0f;

    gra3dcamSetFov(MV_CAM_FOV);
    gra3dcamSetAspect(1.0f, GetPALMode() != 0 ? 0.875f : 1.0f);
    gra3dcamSetPosition(eye);
    gra3dcamSetTarget(target, 1);
    gra3dApplyCamera((GRA3DCAMERA *)0, 1);
}

/* Two directionals and a lifted ambient: a key from over the camera's left
 * shoulder and a cooler fill opposite it, so a turntable never leaves a face
 * fully unlit.  Directions are unit vectors in world space, and -Y is up. */
static void MvApplyLights(void)
{
    G3DLIGHT light;
    float    ambient[4];
    float    key_yaw  = mv_wrk.yaw + 0.6f;
    float    fill_yaw = mv_wrk.yaw - 2.1f;

    gra3dLightEnableAll(0);

    g3dutilSetLightDefault(&light, G3DLIGHT_DIRECTIONAL);
    light.vDirection[0] = -sinf(key_yaw) * 0.82f;
    light.vDirection[1] =  0.57f;
    light.vDirection[2] = -cosf(key_yaw) * 0.82f;
    light.vDirection[3] =  0.0f;
    light.vDiffuse[0]   =  1.00f;
    light.vDiffuse[1]   =  0.97f;
    light.vDiffuse[2]   =  0.90f;
    light.vDiffuse[3]   =  1.0f;
    gra3dSetLight(LID_DIRECTIONAL_0, &light);
    gra3dLightEnable(LID_DIRECTIONAL_0, 1);

    g3dutilSetLightDefault(&light, G3DLIGHT_DIRECTIONAL);
    light.vDirection[0] = -sinf(fill_yaw) * 0.90f;
    light.vDirection[1] = -0.20f;
    light.vDirection[2] = -cosf(fill_yaw) * 0.90f;
    light.vDirection[3] =  0.0f;
    light.vDiffuse[0]   =  0.34f;
    light.vDiffuse[1]   =  0.38f;
    light.vDiffuse[2]   =  0.50f;
    light.vDiffuse[3]   =  1.0f;
    gra3dSetLight(LID_DIRECTIONAL_1, &light);
    gra3dLightEnable(LID_DIRECTIONAL_1, 1);

    ambient[0] = 0.34f;
    ambient[1] = 0.34f;
    ambient[2] = 0.38f;
    ambient[3] = 0.0f;
    gra3dSetAmbient(ambient);

    gra3dApplyLight();
}

/* --------------------------------------------------------------------------
 *  Pose and draw
 * ------------------------------------------------------------------------ */

static void MvPoseAndDraw(void)
{
    HeaderSection *hs;
    float          pos[4];
    float          rot[4];

    if (mv_wrk.bound == 0)
    {
        return;
    }

    /* Absolute-position pose: no ANI_CODE, so the clip cannot walk away. */
    motSetCoordFrame(&mv_ani_ctrl, (u_int)mv_wrk.frame);
    mimSetVertex(&mv_ani_ctrl);

    pos[0] = 0.0f;
    pos[1] = 0.0f;
    pos[2] = 0.0f;
    pos[3] = 1.0f;
    rot[0] = 0.0f;
    rot[1] = mv_wrk.model_rot;
    rot[2] = 0.0f;
    rot[3] = 0.0f;

    SetCoordinate(&mv_ani_ctrl, pos, rot);

    hs = mv_ani_ctrl.base_p;
    if (hs == (HeaderSection *)0)
    {
        return;
    }

    SendEneVram(mv_ani_ctrl.mdl_p, 0x2bc0);
    ManmdlSetAlpha(hs, 0x80);
    _gra3dDrawSGD((SGDFILEHEADER *)hs, SRT_REALTIME, (SGDCOORDINATE *)0, -1);
    DrawEneSubObj(mv_ani_ctrl.mpk_p, 0x80, 0x80);
    _SetPREVIOUSTRI2PRIM(nullptr);
}

/* --------------------------------------------------------------------------
 *  Pages
 * ------------------------------------------------------------------------ */

static int MvSelectPage(void)
{
    int step = ((*key_now[8] != 0) || (*key_now[10] != 0)) ? 10 : 1;
    int y;

    if (MvRpt(0x1000U, 0))
    {
        mv_wrk.csr = (mv_wrk.csr + MV_SEL_ROWS - 1) % MV_SEL_ROWS;
    }
    else if (MvRpt(0x4000U, 1))
    {
        mv_wrk.csr = (mv_wrk.csr + 1) % MV_SEL_ROWS;
    }

    if (MvRpt(0x8000U, 2))
    {
        if (mv_wrk.csr == 0)
        {
            mv_wrk.mdl_no = MvClampi(mv_wrk.mdl_no - step, 0, MV_MDL_MAX);
        }
        else
        {
            mv_wrk.anm_no = MvClampi(mv_wrk.anm_no - step, 0, MV_ANM_MAX);
        }
    }
    else if (MvRpt(0x2000U, 3))
    {
        if (mv_wrk.csr == 0)
        {
            mv_wrk.mdl_no = MvClampi(mv_wrk.mdl_no + step, 0, MV_MDL_MAX);
        }
        else
        {
            mv_wrk.anm_no = MvClampi(mv_wrk.anm_no + step, 0, MV_ANM_MAX);
        }
    }

    if (*paddat[0] == 1)
    {
        MvBeginLoad();
    }

    y = 96;
    SetASCIIString2(0, 40.0f, 64.0f, 1, 0xff, 0xff, 0x00, s_mv_sel_hdr);
    SetString2(0, 72.0f, (float)y, 1, 0xff, 0xff, 0xff, "%s   %3d",
               s_mv_row_mdl, mv_wrk.mdl_no);
    SetString2(0, 72.0f, (float)(y + MV_LINE_H * 2), 1, 0xff, 0xff, 0xff,
               "%s   %3d", s_mv_row_anm, mv_wrk.anm_no);
    SetASCIIString2(0, 48.0f, (float)(y + mv_wrk.csr * MV_LINE_H * 2),
                    1, 0xff, 0xff, 0x00, s_mv_cursor);

    SetASCIIString2(0, 40.0f, (float)(MV_SCREEN_H - 52), 1, 0x80, 0x80, 0x80,
                    s_mv_sel_help1);
    SetASCIIString2(0, 40.0f, (float)(MV_SCREEN_H - 36), 1, 0x80, 0x80, 0x80,
                    s_mv_sel_help2);

    return (*paddat[1] == 1);
}

static int MvLoadPage(void)
{
    if (mv_wrk.failed == 0)
    {
        MvPollLoad();
    }

    if (mv_wrk.failed != 0)
    {
        SetASCIIString2(0, 40.0f, 96.0f, 1, 0xff, 0x60, 0x60,
                        (mv_wrk.failed == 2) ? s_mv_bindfail : s_mv_failed);
        SetASCIIString2(0, 40.0f, 128.0f, 1, 0x80, 0x80, 0x80, s_mv_failed2);

        if (*paddat[0] == 1 || *paddat[1] == 1)
        {
            MvRelease();
            mv_wrk.failed = 0;
            mv_wrk.state  = MV_STATE_SELECT;
        }
        return 0;
    }

    SetString2(0, 40.0f, 96.0f, 1, 0xff, 0xff, 0xff, s_mv_loading,
               mv_wrk.req_mdl, mv_wrk.req_anm, mv_wrk.load_timer);

    /* TRIANGLE abandons a load in progress.  The requests are cancelled, not
     * left running -- a slot still loading into a heap this tool is about to
     * reset would write into whatever claims it next. */
    if (*paddat[1] == 1)
    {
        MvRelease();
        mv_wrk.state = MV_STATE_SELECT;
    }

    return 0;
}

static int MvViewPage(void)
{
    int clips  = MvClipCount();
    int frames = MvFrameCount();

    /* L1 / R1 change clip.  key_now[8] is L1 and [10] R1; they are read as a
     * fresh press rather than a repeat so a held shoulder does not race
     * through a 60-clip pak. */
    if (*key_now[8] == 1)
    {
        MvSetClip(mv_wrk.clip - 1);
        clips  = MvClipCount();
        frames = MvFrameCount();
    }
    else if (*key_now[10] == 1)
    {
        MvSetClip(mv_wrk.clip + 1);
        clips  = MvClipCount();
        frames = MvFrameCount();
    }

    /* L2 / R2 zoom. */
    if (*key_now[9] != 0)
    {
        mv_wrk.dist = MvClampf(mv_wrk.dist + MV_CAM_DIST_STEP,
                               MV_CAM_DIST_MIN, MV_CAM_DIST_MAX);
    }
    if (*key_now[11] != 0)
    {
        mv_wrk.dist = MvClampf(mv_wrk.dist - MV_CAM_DIST_STEP,
                               MV_CAM_DIST_MIN, MV_CAM_DIST_MAX);
    }

    if (*paddat[0] == 1)
    {
        mv_wrk.playing ^= 1;
    }

    if (*key_now[7] == 1)
    {
        mv_wrk.turntable ^= 1;
    }

    /* START cycles the playback rate.  key_now[12] is START; nothing else in
     * this phase reads it. */
    if (*key_now[12] == 1)
    {
        mv_wrk.speed = (mv_wrk.speed >= 4) ? 1 : mv_wrk.speed * 2;
    }

    /* SQUARE steps one frame and implies pause -- the usual scrubber
     * behaviour, and it is what makes a single frame readable. */
    if (*key_now[6] == 1)
    {
        mv_wrk.playing = 0;
        mv_wrk.frame   = (mv_wrk.frame + 1) % frames;
    }

    /* The d-pad orbits, read as a hold rather than a repeat so the camera
     * moves smoothly. */
    if ((pad[0].now & 0x8000U) != 0)
    {
        mv_wrk.yaw -= MV_CAM_YAW_STEP;
    }
    if ((pad[0].now & 0x2000U) != 0)
    {
        mv_wrk.yaw += MV_CAM_YAW_STEP;
    }
    if ((pad[0].now & 0x1000U) != 0)
    {
        mv_wrk.pitch = MvClampf(mv_wrk.pitch + MV_CAM_PITCH_STEP,
                                -MV_CAM_PITCH_LIMIT, MV_CAM_PITCH_LIMIT);
    }
    if ((pad[0].now & 0x4000U) != 0)
    {
        mv_wrk.pitch = MvClampf(mv_wrk.pitch - MV_CAM_PITCH_STEP,
                                -MV_CAM_PITCH_LIMIT, MV_CAM_PITCH_LIMIT);
    }

    if (mv_wrk.turntable != 0)
    {
        mv_wrk.model_rot += 0.012f;
        if (mv_wrk.model_rot > MV_PI)
        {
            mv_wrk.model_rot -= MV_PI * 2.0f;
        }
    }

    if (mv_wrk.playing != 0)
    {
        mv_wrk.frame = (mv_wrk.frame + mv_wrk.speed) % frames;
    }

    MvApplyCamera();
    MvApplyLights();
    MvPoseAndDraw();

    SetString2(0, 8.0f, 40.0f, 1, 0xff, 0xff, 0xff, s_mv_info1,
               mv_wrk.req_mdl, mv_wrk.req_anm, mv_wrk.clip, clips);
    SetString2(0, 8.0f, 56.0f, 1, 0xff, 0xff, 0xff, s_mv_info2,
               mv_wrk.frame, frames,
               mv_wrk.playing ? s_mv_playing : s_mv_paused, mv_wrk.speed);
    SetString2(0, 8.0f, 72.0f, 1, 0x90, 0x90, 0x90, s_mv_info3,
               (int)(mv_wrk.yaw * 180.0f / MV_PI),
               (int)(mv_wrk.pitch * 180.0f / MV_PI),
               (int)mv_wrk.dist,
               mv_wrk.turntable ? s_mv_on : s_mv_off);

    SetASCIIString2(0, 8.0f, (float)(MV_SCREEN_H - 56), 1, 0x80, 0x80, 0x80, s_mv_help1);
    SetASCIIString2(0, 8.0f, (float)(MV_SCREEN_H - 40), 1, 0x80, 0x80, 0x80, s_mv_help2);
    SetASCIIString2(0, 8.0f, (float)(MV_SCREEN_H - 24), 1, 0x80, 0x80, 0x80, s_mv_help3);

    /* TRIANGLE goes back to the select page, releasing as it goes: staying
     * bound would keep the model resident behind a page that cannot show it. */
    if (*paddat[1] == 1)
    {
        MvRelease();
        mv_wrk.state = MV_STATE_SELECT;
    }

    return 0;
}

/* --------------------------------------------------------------------------
 *  Entry points
 * ------------------------------------------------------------------------ */

void MotionViewerInit(void)
{
    memset(&mv_wrk, 0, sizeof(mv_wrk));
    memset(&mv_ani_ctrl, 0, sizeof(mv_ani_ctrl));

    mv_wrk.state   = MV_STATE_SELECT;
    mv_wrk.req_mdl = -1;
    mv_wrk.req_anm = -1;
    mv_wrk.speed   = 1;
    mv_wrk.dist    = MV_CAM_DIST_INIT;
    mv_wrk.yaw     = 0.0f;
    mv_wrk.pitch   = 0.10f;

    /* The out-game heap, the same block and size scn_test.c claims (293-294).
     * Everything below -- the two loader slots and the animation work area --
     * comes out of this. */
    ol_loadHeapReset((void *)0x5a6c00, 0x7a8000);
    ol_load.Init();
}

int MotionViewerMain(void)
{
    int leave = 0;

    switch (mv_wrk.state)
    {
    case MV_STATE_SELECT:
        leave = MvSelectPage();
        break;

    case MV_STATE_LOAD:
        leave = MvLoadPage();
        break;

    case MV_STATE_VIEW:
        leave = MvViewPage();
        break;

    default:
        mv_wrk.state = MV_STATE_SELECT;
        break;
    }

    SetASCIIString2(0, 8.0f, 8.0f, 1, 0xff, 0xff, 0x00, s_mv_title);

    /* The loader only advances when it is pumped, and this is the only thing
     * pumping it -- scn_test.c does the same, at the bottom of its Main(). */
    ol_load.Main();

    /* Flush what MvPoseAndDraw() queued.  Only the view page queues anything,
     * and only it has applied a camera, so the other two do not call in. */
    if (mv_wrk.state == MV_STATE_VIEW)
    {
        gra3dDraw();
    }

    if (leave != 0)
    {
        MvRelease();
    }

    return leave;
}
