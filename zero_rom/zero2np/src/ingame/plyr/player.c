// FILE: /home/zero_rom/zero2np/src/ingame/plyr/player.c
//
// PARTIAL.  player.c is the player state / movement / animation module: 152
// functions and roughly 5600 source lines in the ROM (player.o .text is
// 0x8740 bytes).  Reconstructed:
//
//   init/frame spine : InitPlayer / ReleasePlayer / ClrEneSta / PlyrDebug
//                      PlayerMainCmn / PlyrNormalCtrl / PlyrMotionMovement
//                      PlayerGameOver / PlayerChangeMode / ReqPlyrDead
//                      PlyrDead / PlyrMepachiCtrl
//   movement/anim    : PlyrNModeCtrl / PlyrFModeMoveCtrl / PlyrMovePad
//                      PlyrMoveChk / PlyrMoveChkV / PlyrMovePadFind
//                      GetMovePad / PlyrMoveStaChk / PlyrLeverInputChk
//                      GetMoveSpeed / PlyrPosSet / PlyrHeightCtrl
//                      PadInfoTmpSave / CngPlyrRotRapid / PlyrNAnimeCtrl
//   rooms/doors      : PlyrRoomCheck / PlyrCondCheck / MovePlyrStairs
//                      ReqPlyrDoorMotion / PlyrOpenDoor / DoorMotionIsEnd
//                      SetPlyrRoomID / SetPlyrAreaNo / CheckPlyrAnimeEnd
//   stamina          : ReqPlyr{HP,SP,HPSP}down[P] / PlyrHPdownCtrl
//                      PlyrSPdownCtrl / PlyrAvoidCheck / LeverGachaChk
//                      RTSpiritsDownMode{On,Off} / PlyrSEStop
//   finder entry/exit: SetPlyrFinderIn / SetPlyrFinder / SetPlyrFinderEnd{,1,2}
//                      SetPlyrFinderQEnd / PlyrFinderIn / PlyrFinderEnd
//                      ShutterChanceChangeJob / FinderModeEndJob
//   flashlight       : PlyrFlashlight / _SetFlashlightHand
//                      _SetFlashlightStep / PlyrSpotMoveCtrl
//   vibration        : PlyrVibCheck / PlyrVibCtrl / PlyrVibCtrlBig
//   enemy tracking   : PlyrBattleCheck / NearEneInfo / NearAllEneInfo
//                      ReqCamTraceNearEne / playerSetSearchEne
//                      CEneTracer::{Init,Req,Work}
//   aim scoring      : CulcEP / CulcEP2 / CulcEP3 / GetEnePowerDegree
//   accessors/locks  : the Get/Set* family, every Player*Lock/Unlock,
//                      SetSave_PlyrWrk
//   finder frame     : PlyrFinderCtrl / SpiritGageCalc / EneFrameHitChk
//                      PlyrPhotoChk2 / PlyrPhotoChk2Sub / ChkPhotoAble
//   shot scoring     : PhotoDmgChk2 / PhotoDmgChkSub2 / PlayerTakePictJob
//                      PhotoPointCulcEne2 / GetSoulListNo
//   damage reactions : PlyrDamageCtrl
//
// The frame spine matters because PlayerMainCmn() is the ROM's ONLY caller of
// CalcGirlCoord(), which builds the bone matrices _gra3dDrawSGD() draws the
// player from -- with it stubbed, DrawGirl() had nothing to draw.
//
// The finder and scoring chain links against stubs in the photo module
// (CSpiritGage::Work / CalcDamageRate, most of CNEquipTrayWrk, the photo
// album in photo.c) and in effect_ene.c.  Everything routes correctly and the
// bookkeeping is right, but with CalcDamageRate() returning 0 every shot
// currently scores as sub-function-only: no ghost loses HP yet.  Each stub
// says so where it stands.
//
// STILL STUBBED:
//
//   PlyrAttractSisMain 0x00236530  waits on sister.c's neck look-at
//
// Reconstructed from the Feb 6 2004 prototype (SLES_523.84).

#include "player.h"

#include "plyr_mdl.h"                  /* CalcGirlCoord / plyr_mdlGetANI_CTRL */
#include "../enemy/enemy.h"            /* ene_wrk                             */
#include "../enemy/fene_entry.h"       /* CFEneEntry::AreaChange              */
#include "../../common/utility.h"      /* _ClearVector                        */
#include "../../common/utility2.h"     /* PRINT_WARNING                       */
#include "../../common/variable.h"     /* plyr_wrk / ingame_wrk / GameCostume */
#include "../../graphics/graph3d/g3ddbg.h"
#include "../../graphics/graphics.h"             /* DrawLine / DrawCrossLine  */
#include "../../graphics/graph2d/g2d_draw.h"     /* DISP_SQAR / DispSqrD      */
#include "../../graphics/graph2d/fade.h"         /* FadeInReq / FadeOutReq    */
#include "../../graphics/obj_draw_ctrl.h"        /* SetDrawFLG_PL_GameOver    */
#include "../../ingame/event/prg/ev_open.h"      /* SetOpenCondSwitch         */
#include "../../graphics/graph3d/gra3dConst.h"   /* g_NullLight               */
#include "../../graphics/effect/effect_sub.h"    /* GetCamI2DPos              */
#include "../../graphics/effect/effect_ene.h"    /* ENE_DMG_PARTICLE_REQ      */
#include "../../graphics/effect/effect_scr.h"    /* CallNega2 / CallDeform2   */
#include "../../graphics/effect/effect_obj.h"    /* EffectCameraFlashReq      */
#include "../photo/photo_dat.h"                  /* photo_frame_tbl           */
#include "../photo/photo.h"                      /* PHOTO_WRK_DEF / album     */
#include "../enemy/fly_ctrl.h"                   /* PhotoFlyChk               */
#include "../enemy/enemy_dat.h"                  /* jene_dat / aene_dat       */
#include "../mission.h"                          /* MisSetScore               */
#include "../menu/play_data.h"                   /* PlayData_ScoreCount       */
#include "../item/prg/soul_list.h"               /* GetSoulList               */
#include "../item/dat/item_dat.h"                /* ItemLost                  */
#include "../../graphics/motion/accessory.h"     /* PlyrAcsAlphaCtrl          */
#include "../../graphics/motion/motion.h"        /* motCheckInterp            */
#include "../../ingame/item/prg/item.h"          /* GetPlyrItemHaveNum        */
#include "../../ingame/map/MapFog.h"             /* MapFogStartFogEv          */
#include "../../ingame/map/MapHit.h"             /* MapHitDeleteDoorFlg       */
#include "../../ingame/map/MhCtl.h"              /* MhCtlGetRoomNo / MapHeight*/
#include "../../ingame/map/map_hit_check.h"      /* MapHitCheck               */
#include "../../ingame/map/map_rectangle.h"      /* MrecIsCameraChange / Sta  */
#include "../../ingame/menu/plyr_room_info.h"    /* GetRoomLabel              */
#include "../../ingame/movie_room_menu/prg/movie_projecter.h"
#include "../../ingame/event/prg/ev_macro.h"     /* GetSynchroModeFlg         */
#include "../../ingame/camera/map_camera.h"      /* ReqFinderInOverRap        */
#include "../../ingame/camera/finder_camera.h"   /* ReqFinderCamera           */
#include "../../ingame/photo/m_plyr_camera.h"    /* m_plyr_camera             */
#include "../../ingame/photo/finder.h"           /* ReqFinderFadeIn / Out     */
#include "../../ingame/ingame.h"                 /* SendIngameGameOverPre     */
#include "../../ingame/ingame_effect.h"          /* IgEffectEffectEndParticleReq */
#include "sister.h"                              /* sis_wrk / ReqSisDead      */
#include "../../system/pad/pad.h"                /* paddat / pushdat / pad[]  */
#include "unit_ctl.h"                            /* RotLimitChk / GetTrgtRotY */
#include "../../graphics/graph3d/gra3d.h"        /* gra3dGetCamera            */
#include "../../system/os/system.h"              /* GetPALMode                */

#include <libvu0.h>
#include <math.h>
#include <stdio.h>

#include <string.h>
#include "../../graphics/graph3d/g3dxVu0.h" /* g3dxVu0CopyVector */

/* Defined further down; PlayerChangeMode() has to run the finder teardown and
 * sits ahead of it in the ROM's layout. */
static void FinderModeEndJob(void);

/* Defined with the shot-scoring chain; PlyrPhotoChk2Sub() drives it. */
static void PlayerTakePictJob(int iDmg, int bWithLenz);

static PLAYERFLASHLIGHTTYPE s_FlashlightType;

/* sbss 3f4ef4 / 3f4efc */
static char look_at_pre_kaidan_flg;
static int  finder_off_lock_timer_cnt;

/* sbss 3f4ef0.  Raised by PhotoDmgChkSub2() when a shot did something other
 * than damage -- a fitted sub-function fired.  PlyrPhotoChk2Sub() reads it
 * back to pick which cue to play and whether to spend a tray charge. */
static int gbLenzShot;

/* sbss 3f4f00.  Ghosts finished off by this one shot; drives the double- and
 * triple-kill bonuses.  Cleared per shot, not per frame. */
static int iBusterCountThisTime;

/* rdata 3c3df8.  Damage multiplier by difficulty -- easier settings hit
 * harder.  Indexed by ingame_wrk.mDifficulty. */
static const float aDmgRateByDifficulty[4] = { 1.5f, 1.0f, 0.8f, 0.5f };

/* rdata 3c3e08 / 3c3e18.  Score multipliers for the second and third shot of
 * a combo.  The sub-function ("sb") combo does not multiply at all in the
 * prototype -- all three entries are 1.0 -- but the lookup is there. */
static const float combo_point_mag[3]    = { 1.3f, 3.5f, 4.5f };
static const float combo_sb_point_mag[3] = { 1.0f, 1.0f, 1.0f };

/* sdata 3f3968.  Frames left of the post-camera-cut heading hold. */
static u_short cam_cng_tm;

/* sdata 3f3960.  Status blocks the HP/SP drain runs over: [0] is the player,
 * [1] the sister.  Statically initialised in the ROM -- the two words at
 * 3f3960 are 0x33cd90 and 0x34fc00, i.e. &plyr_wrk and &sis_wrk.  Both work
 * blocks carry cmn_wrk at offset 0, which is why the ROM's initialiser is the
 * bare object address.  Nothing ever assigns to this array at runtime, so it
 * must be initialised here or the first PlyrSPdownCtrl(pl_sta[0]) from
 * PlayerMainCmn() dereferences null. */
PLCMN_WRK *pl_sta[2] = { &plyr_wrk.cmn_wrk, &sis_wrk.cmn_wrk };

/* sdata 3f3970.  Mio's death cry -- a sound-buffer handle rather than a
 * one-shot, so ReleasePlayer() can fade it out. */
CPLYR_SND_BUF_PLAY mio_deadly_player;

/* PORT NOTE: these live in plyr_wrk.cmn_wrk.pr_info, not in a private static.
 * The height lookup (MhCtlGetMapHeight), the outdoor test (PlyrOutsideCheck)
 * and ev_macro all read pr_info.area_no directly, so a separate static would
 * silently disagree with them. */
int GetPlyrAreaNo(void)                                                 /* 3846 */
{
    return plyr_wrk.cmn_wrk.pr_info.area_no;
}

int GetPlyrOldAreaNo(void)                                              /* 3840 */
{
    return plyr_wrk.cmn_wrk.pr_info.area_old;
}

int GetPlyrRoomID(void)                                                 /* 3852 */
{
    return plyr_wrk.cmn_wrk.pr_info.room_id;
}

/* Changing area re-seeds the floating ghosts and re-aims the flashlight (the
 * two rooms can have different light setups). */
void SetPlyrAreaNo(int area_no)                                         /* 3858 */
{
    CFEneEntry::AreaChange(&fene_entry, area_no,                        /* 3860 */
                           plyr_wrk.cmn_wrk.pr_info.area_no);

    plyr_wrk.cmn_wrk.pr_info.area_no = (u_char)area_no;                 /* 3861 */

    PlyrFlashlight(1);                                                  /* 3862 */
}

int GetPlyrFloor(void)                                                  /* 4186 */
{
    return plyr_wrk.cmn_wrk.floor;
}

int GetPlyrEquipmentFilmType(void)                                      /* 4190 */
{
    return m_plyr_camera.camera_film.mFilmType;
}

float PlayerGetNowHPPercentage(void)                                    /* 4194 */
{
    return (float)plyr_wrk.cmn_wrk.st.hp / (float)plyr_wrk.cmn_wrk.st.hpmax;
}

float PlayerGetNowMPPercentage(void)                                    /* 4198 */
{
    return (float)plyr_wrk.cmn_wrk.st.sp / (float)plyr_wrk.cmn_wrk.st.spmax;
}

/* Modes 5..7 are finder-in / finder / finder-out; all three count as "in the
 * viewfinder" for anything that has to suppress ordinary control. */
int InFinderMode(void)                                                  /* 4233 */
{
    return ((u_char)(plyr_wrk.cmn_wrk.mode - 5) < 3);                   /* 4234 */
}

/* Player modes 1..4 are the damage reactions; the ROM tests the whole range
 * with one unsigned compare on (mode - 1). */
int InDamageState(void)
{
    return ((u_char)(plyr_wrk.cmn_wrk.mode - 1) < 4);
}

int IsPlayerInBattle(void)
{
    return ((plyr_wrk.cmn_wrk.st.sta & 0x20) != 0);
}

int PlayerModeIsFinder(void)
{
    return plyr_wrk.cmn_wrk.mode == 6;
}

int PlyrOutsideCheck(void)
{
    int ret = 1;

    if (plyr_wrk.cmn_wrk.pr_info.area_no > 12)
    {
        ret = 0;
        if (plyr_wrk.cmn_wrk.pr_info.area_no == 65)
        {
            ret = 1;
        }
    }
    return ret;
}

void playerSetFlashlightType(PLAYERFLASHLIGHTTYPE pft)
{
    G3DASSERT(s_FlashlightType < NUM_PLAYERFLASHLIGHTTYPE, "");
    s_FlashlightType = pft;
}

PLAYERFLASHLIGHTTYPE playerGetFlashlightType(void)
{
    G3DASSERT(s_FlashlightType < NUM_PLAYERFLASHLIGHTTYPE, "");
    return s_FlashlightType;
}

/* Requests an animation change.  Clip 0 -> 0 is a no-op so an idle player
 * does not restart her idle every frame. */
void SetPlyrAnime(u_char anime_no, u_char frame)                        /* 4045 */
{
    if (anime_no == 0 && plyr_wrk.anime_no == 0)                        /* 4047 */
    {
        return;
    }

    plyr_wrk.cmn_wrk.st.sta &= ~0x2000;                                 /* 4049 */

    SetPlyrNeckFlg(CheckPlayerNeckSW(anime_no));                        /* 4051 */

    plyr_wrk.anime_no = anime_no;                                       /* 4053 */
    ReqPlayerAnime(frame);                                              /* 4055 */
}

void SetPlayerFloor(int floor)
{
    plyr_wrk.cmn_wrk.floor = (short)floor;
}

/* Entering a room is what arms the in-room projector, but never mid-battle. */
void SetPlyrRoomID(int room_id)                                         /* 3866 */
{
    plyr_wrk.cmn_wrk.pr_info.room_id = (u_char)room_id;                 /* 3868 */

    if (IsPlayerInBattle() == 0 && movie_projecterIsReq() != 0)         /* 3870 */
    {
        movie_projecterPlay();                                          /* 3872 */
    }
    else
    {
        movie_projecterStop();                                          /* 3876 */
    }
}

/* ==========================================================================
 *  Player mode changes, doors, stairs and room bookkeeping
 *  (player.o 0x00236050, 0x0023d270..0x0023d820).
 * ======================================================================== */

/* The player mode is traced on every change; the ROM ships the printf pair
 * because a wrong mode transition is otherwise silent and very hard to spot.
 * FinderModeEndJob() is the teardown that has to run whichever way finder
 * mode was left -- including the abrupt event-driven exits. */
void PlayerChangeMode(int iMode)                                        /* 5204 */
{
    int bBack = InFinderMode();                                         /* 5205 */

    printf("Plyr Mode Transfer From %d To ", plyr_wrk.cmn_wrk.mode);    /* 5208 */
    plyr_wrk.cmn_wrk.mode = (u_char)iMode;                              /* 5209 */
    printf("%d\n", iMode & 0xff);                                       /* 5210 */

    if (InFinderMode() == 0 && bBack != 0)                              /* 5212 */
    {
        FinderModeEndJob();                                             /* 5217 */
    }
}

void ReqPlyrDead(int mode)                                              /* 621 */
{
    SendIngameGameOverPre(1);                                           /* 622 */
    plyr_wrk.cmn_wrk.mode = 9;                                          /* 623 */
    plyr_wrk.modedead     = (u_char)mode;                               /* 624 */
}

/* Blink control.  `no` counts frames to the next blink.  The ROM's static has
 * a non-constant initialiser, so GCC 2.96 pairs it with a guard flag at
 * sbss 3f4ee8 -- these files compile as C++, so the same construct works
 * here and the guard is again the compiler's. */
void PlyrMepachiCtrl(void)                                              /* 800 */
{
    static int no = GetRandValI(200);           /* sbss 3f4ee4 */       /* 801 */

    /* Never restart the blink morph while one is already playing. */
    if (IsPlayerMimParts(5) == 0 && --no == -1)                         /* 803 */
    {
        ReqPlayerMim(5, 0);                                             /* 805 */
        no = GetRandValI(200);                                          /* 806 */
    }
}

/* Is `pos` a valid finder target this frame, and how far off centre is it?
 *
 * Two gates: the point has to be inside the sight cone (and within limit_dist)
 * of the camera, and its projection has to land inside the viewfinder frame.
 * `p_dist` comes back as the screen-space distance from the finder centre
 * whichever way the test went -- photo_dat.c's centring sweep reads it only on
 * a hit, which is what makes the ROM's quirk here harmless: on the out-of-
 * sight path FrameInsideChk() never runs, so tx / ty are read back
 * uninitialised and *p_dist is garbage.  They are pinned to zero rather than
 * left indeterminate, because reading an uninitialised local is undefined on
 * the host and no caller consumes the value. */
int InFinderFrameSub(float *pos, float *p_dist, float sight_range,      /* 5036 */
                     float limit_dist)
{
    float tx = 0.0f;
    float ty = 0.0f;
    int   ret;

    if (OutSightChk(pos, gra3dGetCamera()->matCoord[3],                 /* 5041 */
                    plyr_wrk.cmn_wrk.mbox.rot[1], sight_range, limit_dist) == 0 &&
        FrameInsideChk(pos, &tx, &ty) != 0)
    {
        ret = 1;
    }
    else
    {
        ret = 0;                                                        /* 5045 */
    }

    *p_dist = GetDist((float)plyr_wrk.fp[0] - tx,                       /* 5048 */
                      (float)plyr_wrk.fp[1] - ty);

    return ret;                                                         /* 5050 */
}

/* A camera cut arms the heading hold PlyrMoveChk() honours; crossing an area
 * boundary at the same time also raises the 0x200 latch, so the stick has to
 * swing more than 90 degrees before the new camera can turn her. */
void PlyrRoomCheck(void)                                                /* 5055 */
{
    if (plyr_wrk.cmn_wrk.pr_info.camera_no_old !=                       /* 5057 */
        plyr_wrk.cmn_wrk.pr_info.camera_no)
    {
        cam_cng_tm = (GetPALMode() != 0) ? 16 : 20;                     /* 5058 */

        if (plyr_wrk.cmn_wrk.pr_info.area_no !=                         /* 5064 */
            plyr_wrk.cmn_wrk.pr_info.area_old)
        {
            plyr_wrk.cmn_wrk.st.mvsta |= 0x200;                         /* 5065 */
        }
    }

    plyr_wrk.cmn_wrk.pr_info.camera_no_old = plyr_wrk.cmn_wrk.pr_info.camera_no;  /* 5078 */
    plyr_wrk.cmn_wrk.pr_info.camera_no     = (u_char)MrecIsCameraChange();        /* 5079 */
    plyr_wrk.cmn_wrk.pr_info.area_old      = plyr_wrk.cmn_wrk.pr_info.area_no;    /* 5080 */
}

/* 99999.0 (lit4 3ee7e0) stands in for "no ghost in the room" -- callers treat
 * anything that large as out of range rather than testing a flag. */
float GetNearestDistFromPlyrToEnemy(void)                               /* 5086 */
{
    float ll = 99999.0f;                                                /* 5087 */
    int   i;

    for (i = 0; i < ENE_WRK_MAX; i++)                                   /* 5090 */
    {
        if (IsAliveEnemy(i) != 0)                                       /* 5091 */
        {
            float l = GetDistV(plyr_wrk.cmn_wrk.mbox.pos, ene_wrk[i].mbox.pos);

            if (l < ll)                                                 /* 5093 */
            {
                ll = l;
            }
        }
    }

    return ll;                                                          /* 5097 */
}

/* rodata 3c3c98.  Two clips per door type: [0] is the open, [1] the walk
 * through.  Several types deliberately share a pair. */
static const int door_ani_tbl[16][2] =                                  /* 5111 */
{
    { 30, 31 }, { 26, 27 }, { 28, 29 }, { 24, 25 },
    { 38, 39 }, { 36, 37 }, { 38, 39 }, { 36, 37 },
    { 32, 33 }, { 34, 35 }, { 40, 41 }, { 40, 41 },
    { 42, 43 }, { 42, 43 }, { 44, 45 }, { 46, 47 }
};

/* sbss.  Which door clip is playing, so DoorMotionIsEnd() knows what to wait
 * on -- SetPlyrAnime() has already overwritten plyr_wrk.anime_no by then. */
static int door_anime_no;

int DoorMotionIsEnd(void)                                               /* 5199 */
{
    return CheckPlyrAnimeEnd(door_anime_no);
}

void ReqPlyrDoorMotion(int door_type, int phase)                        /* 5110 */
{
    int anime_no = -1;

    if (door_type < 16)                                                 /* 5132 */
    {
        anime_no = door_ani_tbl[door_type][phase];                      /* 5133 */
    }

    if (anime_no < 1)                                                   /* 5135 */
    {
        printf("ERR! NO PLAYER_ANIM[%d]\n", door_type);                 /* 5150 */
        return;
    }

    SetPlyrAnime((u_char)anime_no, 10);                                 /* 5136 */
    door_anime_no = anime_no;
    PlayerChangeMode(8);                                                /* 5139 */

    if (phase == 0)                                                     /* 5141 */
    {
        /* Opening: freeze the ghosts so nothing walks into the doorway, and
         * flag 0x8 so GetPlyrDoorMoveFlg() reports the transit. */
        EnemyAnimLock();                                                /* 5142 */
        plyr_wrk.cmn_wrk.st.sta |= 0x8;                                 /* 5143 */
        ClearPlyrMoveStatus();                                          /* 5144 */
    }
    else if (phase == 1)                                                /* 5146 */
    {
        plyr_wrk.cmn_wrk.st.sta |= 0x10;                                /* 5147 */
    }
}

int PlyrOpenDoor(void)                                                  /* 5154 */
{
    int ret = 0;                                                        /* 5155 */

    if (plyr_wrk.cmn_wrk.mode == 8 &&                                   /* 5157 */
        DoorMotionIsEnd() != 0 &&                                       /* 5160 */
        (plyr_wrk.cmn_wrk.st.sta & 0x10) != 0)
    {
        /* 0x8 transit, 0x10 walk-through, 0x2000 anime-end all clear at once. */
        plyr_wrk.cmn_wrk.st.sta &= ~0x2018;                             /* 5161 */

        PlayerChangeMode(0);                                            /* 5164 */
        plyr_wrk.door_flg = 1;                                          /* 5165 */
        EnemyAnimUnlock();                                              /* 5167 */
        MapHitDeleteDoorFlg(1);                                         /* 5170 */

        /* The door motion shrinks her collision radius; put it back. */
        SetPlayerHitRadius(150.0f);                                     /* 5172 */
        ret = 1;                                                        /* 5174 */
    }

    return ret;                                                         /* 5178 */
}

void ClearPlyrDoorFlg(void)                                             /* 5182 */
{
    plyr_wrk.door_flg = 0;                                              /* 5183 */
}

u_char GetPlyrDoorFlg(void)                                             /* 5186 */
{
    return plyr_wrk.door_flg;                                           /* 5187 */
}

int GetPlyrDoorMoveFlg(void)                                            /* 5190 */
{
    if (DoorMotionIsEnd() == 0 || (plyr_wrk.cmn_wrk.st.sta & 8) == 0)   /* 5191 */
    {
        return 1;                                                       /* 5194 */
    }
    return 0;                                                           /* 5195 */
}

/* Picks the stair clip from the angle between the flight's heading and the
 * player's facing: within +/-65 degrees she is climbing, beyond +/-115 she is
 * descending, and the band between the two is a traverse that gets no stair
 * clip at all.  The step-in bits are mutually exclusive, so each branch sets
 * one and clears its partner. */
void MovePlyrStairs(void)                                               /* 5223 */
{
    float fw;
    float rotw;

    if (MrecGetStaInfo(&rotw, plyr_wrk.cmn_wrk.floor,                   /* 5229 */
                       plyr_wrk.cmn_wrk.mbox.pos) < 1)
    {
        if ((plyr_wrk.cmn_wrk.st.mvsta & 0xf0) != 0)                    /* 5269 */
        {
            plyr_wrk.cmn_wrk.st.mvsta &= ~0xf0;                         /* 5270 */
        }
        return;
    }

    fw = rotw - plyr_wrk.cmn_wrk.mbox.rot[1];                           /* 5230 */
    RotLimitChk(&fw);                                                   /* 5231 */
    fw = (fw * 180.0f) / 3.1415925f;                                    /* 5232 */

    if (fw > -65.0f && fw < 65.0f)                                      /* 5235 */
    {
        /* Facing up the flight. */
        if ((plyr_wrk.cmn_wrk.st.mvsta & 1) != 0)                       /* 5236 */
        {
            plyr_wrk.cmn_wrk.st.mvsta = (plyr_wrk.cmn_wrk.st.mvsta | 0x40) & ~0x10;   /* 5238 */
        }
        else if ((plyr_wrk.cmn_wrk.st.mvsta & 2) != 0)                  /* 5240 */
        {
            plyr_wrk.cmn_wrk.st.mvsta = (plyr_wrk.cmn_wrk.st.mvsta | 0x10) & ~0x40;   /* 5242 */
        }
        else
        {
            plyr_wrk.cmn_wrk.st.mvsta &= ~0xf0;                         /* 5244 */
        }
        return;
    }

    if ((fw >= -180.0f && fw < -115.0f) || (fw > 115.0f && fw <= 180.0f))         /* 5249 */
    {
        /* Facing down the flight. */
        if ((plyr_wrk.cmn_wrk.st.mvsta & 1) != 0)                       /* 5251 */
        {
            plyr_wrk.cmn_wrk.st.mvsta = (plyr_wrk.cmn_wrk.st.mvsta | 0x80) & ~0x20;   /* 5253 */
        }
        else if ((plyr_wrk.cmn_wrk.st.mvsta & 2) != 0)                  /* 5255 */
        {
            plyr_wrk.cmn_wrk.st.mvsta = (plyr_wrk.cmn_wrk.st.mvsta | 0x20) & ~0x80;   /* 5257 */
        }
        else
        {
            plyr_wrk.cmn_wrk.st.mvsta &= ~0xf0;                         /* 5259 */
        }
        return;
    }

    if ((plyr_wrk.cmn_wrk.st.mvsta & 0xf0) != 0)                        /* 5264 */
    {
        plyr_wrk.cmn_wrk.st.mvsta &= ~0xf0;                             /* 5265 */
    }
}

/* 0x2000 is the anime-end flag; a clip that has finished satisfies any wait. */
int CheckPlyrAnimeEnd(int anime_no)                                     /* 5278 */
{
    if ((plyr_wrk.cmn_wrk.st.sta & 0x2000) != 0)                        /* 5279 */
    {
        return 1;                                                       /* 5281 */
    }
    if (anime_no != (int)plyr_wrk.anime_no)                             /* 5283 */
    {
        return 1;
    }
    return 0;                                                           /* 5286 */
}

/* Per-frame condition upkeep: the "about to die" heartbeat loop, the poison
 * fog, and the timers that expire both. */
void PlyrCondCheck(void)                                                /* 5290 */
{
    /* Under 1200 stamina Mio gasps continuously -- unless an event has taken
     * the character over (synchro mode), where the cry would talk over it. */
    if ((u_short)(plyr_wrk.cmn_wrk.st.hp - 1) < 1200 &&                 /* 5293 */
        GetSynchroModeFlg() != 1)                                       /* 5295 */
    {
        if (mio_deadly_player.play_id == CSND_BUF_PLAY_NO_ID)
        {
            mio_deadly_player.play_id = plyr_mdlBankPlay(3, 1, 1, 0, nullptr, 0x3200, 0x1000);
        }
    }
    else if (mio_deadly_player.play_id != CSND_BUF_PLAY_NO_ID)
    {
        SndBufFadeStop(mio_deadly_player.play_id, 10);
        mio_deadly_player.play_id = CSND_BUF_PLAY_NO_ID;
    }

    plyr_wrk.cmn_wrk.st.sta_old = plyr_wrk.cmn_wrk.st.sta;              /* 5312 */

    if (plyr_wrk.cmn_wrk.st.cond_tm != 0)                               /* 5315 */
    {
        plyr_wrk.cmn_wrk.st.cond_tm--;                                  /* 5316 */
    }

    if (plyr_wrk.cmn_wrk.st.cond == 1)                                  /* 5319 */
    {
        /* Held: rumble flat out, and let the player shake free by waggling
         * the stick -- each direction change burns an extra frame. */
        VibrateRequest2(0, 0xff);                                       /* 5324 */

        if (plyr_wrk.cmn_wrk.st.cond_tm != 0 && LeverGachaChk() != 0)   /* 5327 */
        {
            plyr_wrk.cmn_wrk.st.cond_tm--;                              /* 5329 */
        }
    }
    else if (plyr_wrk.cmn_wrk.st.cond == 2)                             /* 5332 */
    {
        if (plyr_wrk.cmn_wrk.st.cond_old != 2)                          /* 5335 */
        {
            MapFogStartFogEv(4, 4, 4, 500, 0x226, 10, 0xff);            /* 5336 */
        }

        /* Being hit cancels the poison outright. */
        if ((u_char)(plyr_wrk.cmn_wrk.mode - 1) < 4)                    /* 5338 */
        {
            plyr_wrk.cmn_wrk.st.cond_tm = 0;
        }

        if (plyr_wrk.cmn_wrk.st.cond_tm == 0)                           /* 5340 */
        {
            MapFogEndFogEv(30);                                         /* 5341 */
        }
    }

    plyr_wrk.cmn_wrk.st.cond_old = plyr_wrk.cmn_wrk.st.cond;            /* 5349 */

    if (plyr_wrk.cmn_wrk.st.cond_tm == 0)                               /* 5352 */
    {
        plyr_wrk.cmn_wrk.st.cond = 0;                                   /* 5353 */
    }

    if (plyr_wrk.shutter_tm != 0)                                       /* 5357 */
    {
        plyr_wrk.shutter_tm--;                                          /* 5358 */
    }
}

/* Called when the player is healed past the danger line, so the looping death
 * cry has to be faded out even though PlyrCondCheck() is not running. */
void PlyrSEStop(void)                                                   /* 5362 */
{
    if ((u_short)(plyr_wrk.cmn_wrk.st.hp - 1) < 1200 &&                 /* 5363 */
        mio_deadly_player.play_id != CSND_BUF_PLAY_NO_ID)
    {
        SndBufFadeStop(mio_deadly_player.play_id, 10);
        mio_deadly_player.play_id = CSND_BUF_PLAY_NO_ID;
    }
}


/* ==========================================================================
 *  HP / SP damage requests (player.o 0x00237130..0x002372a8).
 *
 *  Nothing subtracts stamina directly: callers queue the amount on the
 *  status block and PlyrHPdownCtrl() / PlyrSPdownCtrl() drain it over the
 *  following frames.  The *P variants take a percentage of the maximum.
 *  debug_var.muteki (invincible) suppresses the lot.
 * ======================================================================== */

/* Spirit drain override for scripted set pieces -- the filament pins itself
 * to a fixed rate for `time` frames rather than tracking the ghosts. */
void RTSpiritsDownModeOn(int time)                                      /* 1164 */
{
    m_plyr_camera.filament.rt_ev_wrk.sptime = time;                     /* 1165 */
    m_plyr_camera.filament.rt_ev_wrk.sptype = 1;                        /* 1166 */
}

void RTSpiritsDownModeOff(void)                                         /* 1172 */
{
    m_plyr_camera.filament.rt_ev_wrk.sptype = 0;                        /* 1173 */
}

void ReqPlyrHPSPdownP(PLCMN_WRK *cmn, u_short per)                      /* 1180 */
{
    if (debug_var.muteki == 0)                                          /* 1182 */
    {
        cmn->st.rhspdmg += (u_short)((per * cmn->st.spmax) / 100);      /* 1184 */
    }
}

void ReqPlyrHPdownP(PLCMN_WRK *cmn, u_short per)                        /* 1188 */
{
    if (debug_var.muteki == 0)                                          /* 1190 */
    {
        cmn->st.rhpdmg += (u_short)((per * cmn->st.hpmax) / 100);       /* 1192 */
    }
}

void ReqPlyrSPdownP(PLCMN_WRK *cmn, u_short per)                        /* 1196 */
{
    if (debug_var.muteki == 0)                                          /* 1198 */
    {
        cmn->st.rspdmg += (u_short)((per * cmn->st.spmax) / 100);       /* 1200 */
    }
}

void ReqPlyrHPSPdown(PLCMN_WRK *cmn, u_short deg)                       /* 1205 */
{
    if (debug_var.muteki == 0)                                          /* 1207 */
    {
        cmn->st.rhspdmg += deg;                                         /* 1209 */
    }
}

void ReqPlyrHPdown(PLCMN_WRK *cmn, u_short deg)                         /* 1213 */
{
    if (debug_var.muteki == 0)                                          /* 1215 */
    {
        cmn->st.rhpdmg += deg;                                          /* 1217 */
    }
}

void ReqPlyrSPdown(PLCMN_WRK *cmn, u_short deg)                         /* 1221 */
{
    if (debug_var.muteki == 0)                                          /* 1223 */
    {
        cmn->st.rspdmg += deg;                                          /* 1225 */
    }
}

/* Counts out the dodge window during a grab: avoid_st frames in the button
 * becomes live, and it stays live for avoid_sp frames.  Returns non-zero once
 * the whole window has gone by, whether or not the player made it. */
u_char PlyrAvoidCheck(void)                                             /* 1126 */
{
    plyr_wrk.avoid_tm++;

    if (plyr_wrk.avoid_tm >= plyr_wrk.avoid_st &&                       /* 1128 */
        plyr_wrk.avoid_tm < (u_short)(plyr_wrk.avoid_st + plyr_wrk.avoid_sp) &&
        (*paddat[13] == 1 || *paddat[14] == 1))                         /* 1136 */
    {
        plyr_wrk.avoid_flg++;                                           /* 1138 */
    }

    return (plyr_wrk.avoid_tm >=                                        /* 1141 */
            (u_short)(plyr_wrk.avoid_st + plyr_wrk.avoid_sp));
}

/* "Gacha" is the Japanese onomatopoeia for waggling a stick.  Only a full
 * deflection counts, and only when the octant changed from last frame, so
 * holding the stick over does nothing. */
u_char LeverGachaChk(void)                                              /* 1148 */
{
    static u_char lever_dir_old;            /* sdata 3f39ad */
    u_char result = 0;

    if (PlyrLeverInputChk() == 2 &&                                     /* 1151 */
        lever_dir_old != pad[0].an_dir[0])                              /* 1152 */
    {
        lever_dir_old = pad[0].an_dir[0];                               /* 1153 */
        result = 1;                                                     /* 1154 */
    }

    return result;                                                      /* 1157 */
}


/* ==========================================================================
 *  Finder (viewfinder) mode entry and exit (player.o 0x00237bc0..0x00237fb0,
 *  0x00239298, 0x00239610).
 *
 *  Mode flow: 0 -> 5 (raising, SetPlyrFinderIn) -> 6 (in finder, SetPlyrFinder)
 *  -> 7 (lowering, SetPlyrFinderEnd2) -> 0.  SetPlyrFinderQEnd() is the abrupt
 *  exit events use, which skips 7 entirely.
 * ======================================================================== */

/* Raising the camera auto-aims at the one ghost in the room, but only when
 * there is exactly one candidate -- with two or more the ROM leaves the aim
 * to the player rather than guessing wrong. */
void SetPlyrFinderIn(void)                                              /* 1534 */
{
    float  tv[4];
    float  dist[2];
    u_char trgt = 0xff;
    u_char n    = 0;
    u_char i;

    if (InFinderMode() != 0)                                            /* 1535 */
    {
        return;
    }

    PlayerChangeMode(5);                                                /* 1540 */
    SetPlyrNeckFlg(0);                                                  /* 1543 */

    plyr_wrk.frot_x    = 0.0f;                                          /* 1547 */
    plyr_wrk.fp[0]     = 320;                                           /* 1548 */
    plyr_wrk.fp[1]     = 224;                                           /* 1549 */
    plyr_wrk.finder_tm = 0;                                             /* 1550 */

    SetPlyrAnime(0x30, 0);                                              /* 1553 */

    plyr_wrk.no_photo_tm = 8;                                           /* 1559 */

    dist[1] = 0.0f;                                                     /* 1562 */

    for (i = 0; i < ENE_WRK_MAX; i++)
    {
        /* A candidate has to be acting, alive, aware of the player (0x100 up
         * and 0x1000000 down), visible at all, and not flagged un-targetable. */
        if (ene_wrk[i].status != ENE_STATUS_ACT)          { continue; }  /* 1563 */
        if (ene_wrk[i].st.hp == 0)                        { continue; }
        if ((ene_wrk[i].st.sta & 0x1000100) != 0x100)     { continue; }
        if (ene_wrk[i].tr_rate == 0)                      { continue; }
        if ((ene_wrk[i].attr & 0x40) != 0)                { continue; }

        n++;                                                            /* 1568 */

        float d = GetDistV(plyr_wrk.cmn_wrk.mbox.pos, ene_wrk[i].mbox.pos);  /* 1569 */

        /* Out past the auto-aim range (lit4 3ee710) it is not a candidate for
         * the aim, though it still counts towards n. */
        if (d <= 2500.0f)                                               /* 1570 */
        {
            dist[0] = d;
            if (dist[1] == 0.0f || d < dist[1])
            {
                dist[1] = d;                                            /* 1574 */
                trgt    = i;                                            /* 1573 */
            }
        }
    }

    if (trgt != 0xff && n == 1)                                         /* 1578 */
    {
        /* Aim at the ghost's neck from the player's eye line.  lit4 3ee714 is
         * the ROM's eye offset off the move box; rspd holds the aim relative
         * to her current facing, and the pitch is moved out to frot_x because
         * the body itself does not pitch. */
        g3dxVu0CopyVector(tv, plyr_wrk.cmn_wrk.mbox.pos);
        tv[1] += -709.0f;                                               /* 1581 */

        GetTrgtRot(tv, ene_wrk[trgt].mpos.p0,                           /* 1583 */
                   plyr_wrk.cmn_wrk.mbox.rspd, 3);

        plyr_wrk.cmn_wrk.mbox.rspd[1] -= plyr_wrk.cmn_wrk.mbox.rot[1];  /* 1584 */
        RotLimitChk(&plyr_wrk.cmn_wrk.mbox.rspd[1]);                    /* 1585 */

        plyr_wrk.frot_x = plyr_wrk.cmn_wrk.mbox.rspd[0];                /* 1586 */
        plyr_wrk.cmn_wrk.mbox.rspd[0] = 0.0f;                           /* 1587 */
    }
    else
    {
        _ClearVector(plyr_wrk.cmn_wrk.mbox.rspd);                       /* 1590 */
    }

    /* Poll every slot: IsActEnemy() has the side effect the ROM wants here,
     * which is why the return value is discarded. */
    for (i = 0; i < ENE_WRK_MAX; i++)                                   /* 1593 */
    {
        IsActEnemy(i);                                                  /* 1594 */
    }

    ReqFinderCamera();                                                  /* 1600 */
}

void SetPlyrFinder(void)                                                /* 1611 */
{
    PlayerChangeMode(6);                                                /* 1612 */
    PlayerDrawLock();                                                   /* 1613 */
    ReqFinderFadeIn();                                                  /* 1614 */
}

/* finder_tm doubles as the exit countdown; 0 means "not leaving". */
void SetPlyrFinderEnd(void)                                             /* 1620 */
{
    if (plyr_wrk.finder_tm == 0)                                        /* 1621 */
    {
        plyr_wrk.finder_tm = 2;                                         /* 1624 */
    }
}

void SetPlyrFinderEnd1(void)                                            /* 1627 */
{
    SetPlyrAnime(0x31, 0);                                              /* 1634 */
}

void SetPlyrFinderEnd2(void)                                            /* 1637 */
{
    u_char i;

    PlayerChangeMode(7);                                                /* 1640 */
    PlayerDrawUnlock();                                                 /* 1641 */
    ReqFinderFadeOut();                                                 /* 1642 */
    SetPlyrNeckFlg(1);                                                  /* 1643 */

    for (i = 0; i < ENE_WRK_MAX; i++)                                   /* 1646 */
    {
        IsActEnemy(i);
    }
}

/* Abrupt exit: events use this so the lowering animation never plays. */
void SetPlyrFinderQEnd(void)                                            /* 1656 */
{
    if ((u_char)(plyr_wrk.cmn_wrk.mode - 5) < 2)                        /* 1658 */
    {
        if (plyr_wrk.cmn_wrk.mode == 6)                                 /* 1661 */
        {
            PlayerDrawUnlock();                                         /* 1662 */
        }
        ReqFinderFadeOut();                                             /* 1664 */
        FinderDispInit();                                               /* 1666 */
        PlayerChangeMode(0);                                            /* 1668 */
    }
}

/* Mode 5: the raise takes 10 frames, then the overlap wipe hands over to the
 * finder proper.  Movement still runs off the animation the whole time. */
void PlyrFinderIn(void)                                                 /* 1708 */
{
    plyr_wrk.preShutterChanceState = SHUTTER_CHANCE_NONE;               /* 1709 */
    plyr_wrk.nowShutterChanceState = SHUTTER_CHANCE_NONE;               /* 1710 */
    finder_off_lock_timer_cnt      = 0;                                 /* 1711 */

    plyr_wrk.finder_tm++;                                               /* 1714 */
    if (plyr_wrk.finder_tm > 10)
    {
        ReqFinderInOverRap(8);                                          /* 1715 */
        SetPlyrFinder();                                                /* 1716 */
        plyr_wrk.finder_tm = 0;                                         /* 1717 */
    }

    PlyrMotionMovement();                                               /* 1720 */
}

/* Mode 7: hold until the lowering clip (0x31) finishes. */
void PlyrFinderEnd(void)                                                /* 1723 */
{
    if (CheckPlyrAnimeEnd(0x31) != 0)                                   /* 1724 */
    {
        PlayerChangeMode(0);                                            /* 1727 */
    }

    PlyrMotionMovement();                                               /* 1729 */
}

/* Lens zoom telegraphs the shutter chance, and the SP gauge lights when the
 * chance is a special one -- or a normal one taken mid-combo. */
static void ShutterChanceChangeJob(void)                                /* 2526 */
{
    int bZoomIn = 0;                                                    /* 2528 */

    if (plyr_wrk.nowShutterChanceState != SHUTTER_CHANCE_NONE)
    {
        bZoomIn = (CPhotoCharger::IsReady(&m_plyr_camera.charger) != 0);
    }

    if (bZoomIn)
    {
        CNPlyrCamera::ReqZoomIn(&m_plyr_camera);                        /* 2530 */
    }
    else if (plyr_wrk.preShutterChanceState != SHUTTER_CHANCE_NONE &&
             plyr_wrk.nowShutterChanceState == SHUTTER_CHANCE_NONE)
    {
        CNPlyrCamera::ReqZoomOut(&m_plyr_camera);                       /* 2535 */
    }

    if (IsInCombo() != 0)                                               /* 2538 */
    {
        if (plyr_wrk.nowShutterChanceState == SHUTTER_CHANCE_NONE)      /* 2540 */
        {
            CSPChance::Set(&m_plyr_camera.sp, 0);                       /* 2541 */
        }
    }
    else if (plyr_wrk.nowShutterChanceState != SHUTTER_CHANCE_SP)
    {
        CSPChance::Set(&m_plyr_camera.sp, 0);                           /* 2546 */
    }

    if (plyr_wrk.preShutterChanceState != plyr_wrk.nowShutterChanceState &&      /* 2552 */
        (plyr_wrk.nowShutterChanceState == SHUTTER_CHANCE_SP ||
         (plyr_wrk.nowShutterChanceState == SHUTTER_CHANCE_NORMAL &&
          IsInCombo() != 0)))
    {
        CSPChance::Set(&m_plyr_camera.sp, 1);                           /* 2554 */
    }
}

/* Teardown shared by every way out of finder mode.  0x40000 is the
 * finder-aim-in-progress flag CEneTracer::Work() also owns. */
static void FinderModeEndJob(void)                                      /* 2628 */
{
    CNPlyrCamera::ReqZoomOut(&m_plyr_camera);                           /* 2629 */
    CSPChance::Set(&m_plyr_camera.sp, 0);                               /* 2630 */
    CNEquipTrayWrk::End(&m_plyr_camera.eq_tray);                        /* 2631 */

    _ClearVector(plyr_wrk.cmn_wrk.mbox.rspd);                           /* 2633 */
    plyr_wrk.cmn_wrk.st.sta &= ~0x40000;                                /* 2634 */
}


/* ==========================================================================
 *  Position / rotation accessors and the lock counters
 *  (player.o 0x0023bb98..0x0023bc70, 0x0023dbb0..0x0023ddf0).
 * ======================================================================== */

void GetPlayerPos(float *pos)                                           /* 4003 */
{
    g3dxVu0CopyVector(pos, plyr_wrk.cmn_wrk.mbox.pos);
}

void GetPlayerFinderPos(float *fx, float *fy)                           /* 4007 */
{
    *fx = (float)plyr_wrk.fp[0];                                        /* 4007 */
    *fy = (float)plyr_wrk.fp[1];                                        /* 4008 */
}

/* Teleporting the player has to carry the cached head position with her, or
 * the look-at system aims at where she used to be for a frame. */
void SetPlayerPos(float *pos)                                           /* 4013 */
{
    float vw[4];

    sceVu0SubVector(vw, pos, plyr_wrk.cmn_wrk.mbox.pos);                /* 4015 */
    sceVu0AddVector(plyr_wrk.cmn_wrk.headpos,                           /* 4016 */
                    plyr_wrk.cmn_wrk.headpos, vw);

    g3dxVu0CopyVector(plyr_wrk.cmn_wrk.mbox.pos, pos);
}

void SetPlayerRot(float *rot)                                           /* 4021 */
{
    g3dxVu0CopyVector(plyr_wrk.cmn_wrk.mbox.rot, rot);
}

void SetPlayerHitRadius(float fLen)                                     /* 4030 */
{
    plyr_wrk.hit_rad = fLen;
}

/* Stops the turn dead and drops both rapid-turn bits, so a scripted move does
 * not inherit a spin that was in flight. */
void ClearPlyrMoveStatus(void)                                          /* 5396 */
{
    plyr_wrk.cmn_wrk.mbox.rspd[1] = 0.0f;                               /* 5397 */
    plyr_wrk.cmn_wrk.st.mvsta &= ~0x1800;                               /* 5398 */
}

int IsFinderLocked(void)                                                /* 5410 */
{
    return (plyr_wrk.finder_lock_cnt != 0);                             /* 5411 */
}

/* Returns -1 when the lock was taken while the finder was already up -- the
 * caller then knows it has to force the finder down itself. */
int PlayerFinderLock(void)                                              /* 5418 */
{
    m_plyr_camera.filament.DrawLock();                       /* 5419 */
    plyr_wrk.finder_lock_cnt++;                                         /* 5420 */

    return (InFinderMode() != 0) ? -1 : 0;                              /* 5424 */
}

void PlayerFinderUnlock(void)                                           /* 5432 */
{
    plyr_wrk.finder_lock_cnt--;                                         /* 5433 */
    m_plyr_camera.filament.DrawUnlock();                     /* 5434 */

    if (plyr_wrk.finder_lock_cnt < 0)                                   /* 5438 */
    {
        PRINT_ASSERT("finder_lock_cnt is under 0");                     /* 5439 */
    }
}

void PlayerShutterLock(void)    { plyr_wrk.shutter_lock_cnt++; }        /* 5446 */
void PlayerShutterUnlock(void)  { plyr_wrk.shutter_lock_cnt--; }        /* 5452 */
void PlayerMoveLock(void)       { plyr_wrk.move_lock_cnt++; }           /* 5459 */
void PlayerMoveUnlock(void)     { plyr_wrk.move_lock_cnt--; }           /* 5465 */
void PlayerActionLock(void)     { plyr_wrk.action_lock_cnt++; }         /* 5471 */
void PlayerActionUnlock(void)   { plyr_wrk.action_lock_cnt--; }         /* 5477 */
void PlayerRunLock(void)        { plyr_wrk.run_lock_cnt++; }            /* 5484 */
void PlayerRunUnlock(void)      { plyr_wrk.run_lock_cnt--; }            /* 5488 */
void PlayerCurseLock(void)      { plyr_wrk.ane_curse_lock++; }          /* 5493 */
void PlayerCurseUnlock(void)    { plyr_wrk.ane_curse_lock--; }          /* 5497 */

void PlayerLock(void)                                                   /* 5502 */
{
    PlayerMoveLock();                                                   /* 5503 */
    PlayerActionLock();                                                 /* 5504 */
    PlayerFinderLock();                                                 /* 5505 */
}

void PlayerUnlock(void)                                                 /* 5509 */
{
    PlayerMoveUnlock();                                                 /* 5510 */
    PlayerActionUnlock();                                               /* 5511 */
    PlayerFinderUnlock();                                               /* 5512 */
}

/* The whole PLYR_WRK goes into the save block verbatim. */
void SetSave_PlyrWrk(MC_SAVE_DATA *data)                                /* 5526 */
{
    data->addr = (u_char *)&plyr_wrk;
    data->size = sizeof(plyr_wrk);                                                 /* 5527 */
}


/* ==========================================================================
 *  Flashlight (player.o 0x0023c1c8..0x0023cb80).
 *
 *  Two rigs share the PLYR_WRK light slots: PFT_HAND is the hand-held torch
 *  (a spot light plus a soft "bounce" point light for the ground splash), and
 *  PFT_STEP is the flat point light used where the torch is not in hand.
 *  playerSetFlashlightType() picks between them; PlyrFlashlight() dispatches.
 * ======================================================================== */

/* Aims the torch with the right stick.  Both axes ease towards the stick at a
 * fixed rate (2 degrees a frame moving out, 6 coming back) and clamp to the
 * shoulder's range, so the beam never snaps. */
void PlyrSpotMoveCtrl(void)                                             /* 4560 */
{
    float rv[4];
    float r;
    float rcng_adj;
    int   over;

    /* Held or stunned: the arm is not hers to move. */
    if (plyr_wrk.cmn_wrk.st.cond == 1 || plyr_wrk.cmn_wrk.st.cond == 3)
    {
        return;
    }

    /* ---- pitch (analog[1]) ---- */
    r        = 0.0f;
    rcng_adj = 0.034906581f;                /* 2 deg, the move-out rate */

    /* +0xd0 rotates the dead zone so a centred stick folds to <= 0xa0. */
    if ((u_char)(pad[0].analog[1] + 0xd0) > 0xa0)
    {
        float t = -(((float)(int)(pad[0].analog[1] - 0x80) * 1.5707962f) / 127.0f);

        rcng_adj = 0.104719743f;            /* 6 deg, the return rate */
        r = 0.69813162f;                    /* +40 deg */
        if (t <= 0.69813162f)
        {
            r = t;
            if (t < -0.34906581f)           /* -20 deg */
            {
                r = -0.34906581f;
            }
        }
    }

    if (plyr_wrk.spot_rot[0] < r)
    {
        float n = plyr_wrk.spot_rot[0] + rcng_adj;
        over = (n < r);
        plyr_wrk.spot_rot[0] = over ? n : r;
    }
    else
    {
        float n = plyr_wrk.spot_rot[0] - rcng_adj;
        over = (r < n);
        plyr_wrk.spot_rot[0] = over ? n : r;
    }

    /* ---- yaw (analog[0]) ---- */
    r        = 0.0f;
    rcng_adj = 0.034906581f;

    if ((u_char)(pad[0].analog[0] + 0xd0) > 0xa0)
    {
        float t = ((float)(int)(pad[0].analog[0] - 0x80) * 1.5707962f) / 127.0f;

        r = 0.52359873f;                    /* +30 deg */
        if (t <= 0.52359873f)
        {
            r = t;
            if (t < -0.52359873f)
            {
                r = -0.52359873f;
            }
        }
        rcng_adj = 0.104719743f;

        /* Recomputed but unused by the ROM past this point -- the aim is
         * still taken from the stick, not the camera.  Kept because the call
         * is what leaves rv holding the camera-relative heading. */
        GetTrgtRot(plyr_wrk.cmn_wrk.mbox.pos, gra3dGetCamera()->matCoord[3], rv, 2);
        rv[1] -= plyr_wrk.cmn_wrk.mbox.rot[1];
        RotLimitChk(&rv[1]);
    }

    if (plyr_wrk.spot_rot[1] < r)
    {
        float n = plyr_wrk.spot_rot[1] + rcng_adj;
        over = (n < r);
        plyr_wrk.spot_rot[1] = over ? n : r;
    }
    else
    {
        float n = plyr_wrk.spot_rot[1] - rcng_adj;
        over = (r < n);
        plyr_wrk.spot_rot[1] = over ? n : r;
    }
}

/* Is the world point tv inside the viewfinder frame?  Projects it to screen
 * space and tests the result against the frame rectangle, which is centred on
 * plyr_wrk.fp -- the finder's own screen centre, which drifts as the camera is
 * swung -- and sized by photo_frame_tbl.
 *
 * tx / ty are out-params: the caller gets the projected position whether or not
 * it landed inside, because GetCamI2DPos() runs unconditionally. */
int FrameInsideChk(float *tv, float *tx, float *ty)                     /* 4271 */
{
    float minx;
    float maxx;
    float miny;
    float maxy;
    int   result = 0;                                                   /* 4273 */

    GetCamI2DPos(tv, tx, ty);                                           /* 4275 */

    minx = (float)plyr_wrk.fp[0] - (float)photo_frame_tbl[0][0] * 0.5f; /* 4277 */
    maxx = (float)plyr_wrk.fp[0] + (float)photo_frame_tbl[0][0] * 0.5f; /* 4278 */
    miny = (float)plyr_wrk.fp[1] - (float)photo_frame_tbl[0][1] * 0.5f; /* 4279 */
    maxy = (float)plyr_wrk.fp[1] + (float)photo_frame_tbl[0][1] * 0.5f; /* 4280 */

    if (minx <= *tx && *tx <= maxx && miny <= *ty && *ty <= maxy)       /* 4282 */
    {
        result = 1;
    }

    return result;                                                      /* 4286 */
}

/* Hand-held torch.  The beam is built as a matrix chain rather than by hand:
 * an offset point rotated by pitch/yaw and translated by the mount point,
 * which gives both the lamp position (mpos2) and a point along the beam
 * (mpos1) the direction is normalised from. */
static void _SetFlashlightHand(int move_sw)                             /* 4290 */
{
    float vref[4];
    float mpos1[4], mpos2[4];
    float wpos11[4], wpos21[4];
    float wpos12[4], wpos22[4];
    float wlm[4][4];
    float xrot, yrot;
    float y, z, py, pz, rfy, rfz;
    float range, intens, rfrange;
    int   out_info;

    /* sdata 3f39e8 is 0xffffffff in the ROM.  The sentinel forces the indoor
     * rig to be seeded on the first call; zero-initialising it leaves a player
     * who starts indoors with a zero-range, zero-intensity flashlight. */
    static int old_out_info = -1;

    out_info = PlyrOutsideCheck();                                      /* 4335 */

    if (old_out_info != out_info)                                       /* 4341 */
    {
        if (out_info != 0)
        {
            /* Outdoors: mounted lower and further back, shorter and dimmer
             * beam -- the exteriors are already lit by the sky. */
            debug_var.fl_py     = -39.0f;                               /* 4344 */
            debug_var.fl_pz     = -204.0f;                              /* 4345 */
            debug_var.fl_y      = 90.0f;                                /* 4346 */
            debug_var.fl_z      = 200.0f;                               /* 4347 */
            debug_var.fl_range  = 1440.0f;                              /* 4348 */
            debug_var.fl_intens = 0.53f;                                /* 4349 */
            debug_var.fl2_range = 1520.0f;                              /* 4350 */
        }
        else
        {
            debug_var.fl_py     = -23.0f;                               /* 4355 */
            debug_var.fl_pz     = -130.0f;                              /* 4356 */
            debug_var.fl_y      = 65.0f;                                /* 4357 */
            debug_var.fl_z      = 200.0f;                               /* 4358 */
            debug_var.fl_range  = 3420.0f;                              /* 4359 */
            debug_var.fl_intens = 0.73f;                                /* 4360 */
            debug_var.fl2_range = 1000.0f;                              /* 4361 */
        }

        debug_var.flrf_y     = 82.0f;                                   /* 4362 */
        debug_var.flrf_z     = 145.0f;                                  /* 4363 */
        debug_var.flrf_range = 240.0f;                                  /* 4364 */
    }
    old_out_info = out_info;                                            /* 4367 */

    py      = debug_var.fl_py;                                          /* 4370 */
    pz      = debug_var.fl_pz;                                          /* 4371 */
    y       = debug_var.fl_y;                                           /* 4372 */
    z       = debug_var.fl_z;                                           /* 4373 */
    range   = debug_var.fl_range * plyr_wrk.fl_pow;                     /* 4374 */
    intens  = debug_var.fl_intens;                                      /* 4375 */
    rfy     = debug_var.flrf_y;                                         /* 4376 */
    rfz     = debug_var.flrf_z;                                         /* 4377 */
    rfrange = debug_var.flrf_range * plyr_wrk.fl_pow;                   /* 4379 */

    /* 0x8000 is "torch forced on"; without it and without the debug override
     * the lights are parked rather than updated. */
    if (debug_var.fl_sw == 0 && (plyr_wrk.cmn_wrk.st.sta & 0x8000) == 0)         /* 4381 */
    {
        plyr_wrk.fl.Type  = G3DLIGHTTYPE_FORCE_DWORD;                   /* 4382 */
        plyr_wrk.fl2.Type = G3DLIGHTTYPE_FORCE_DWORD;                   /* 4383 */
        plyr_wrk.spot_rot[0] = 0.0f;                                    /* 4386 */
        plyr_wrk.spot_rot[1] = 0.0f;                                    /* 4387 */
        return;                                                         /* 4389 */
    }

    /* The arm only aims while she is upright and in control. */
    if (move_sw != 0 &&                                                 /* 4436 */
        (u_char)(plyr_wrk.cmn_wrk.mode - 1) >= 4 &&                     /* 4437 */
        plyr_wrk.cmn_wrk.mode != 8 &&
        plyr_wrk.move_lock_cnt == 0)
    {
        PlyrSpotMoveCtrl();                                             /* 4438 */
    }

    if (plyr_wrk.cmn_wrk.mode == 6)                                     /* 4451 */
    {
        /* In the finder the torch tracks the viewfinder, not the shoulder. */
        y    = 42.0f;                                                   /* 4452 */
        yrot = plyr_wrk.cmn_wrk.mbox.rot[1];                            /* 4454 */
        xrot = plyr_wrk.frot_x;
    }
    else
    {
        xrot = plyr_wrk.spot_rot[0];                                    /* 4456 */
        yrot = plyr_wrk.spot_rot[1] + plyr_wrk.cmn_wrk.mbox.rot[1];     /* 4457 */
        RotLimitChk(&yrot);                                             /* 4458 */
    }

    /* Dead store in the ROM: light 0x26 is already a spot and the copy on the
     * next line overwrites Type.  Kept for fidelity. */
    plyr_wrk.fl.Type = G3DLIGHT_SPOT;                                   /* 4464 */
    plyr_wrk.fl = gra3dGetLightRef(0x26);                               /* 4467 */

    /* Beam vector: a point z ahead, raised to the mount height y. */
    _SetVector(wpos11, 0.0f, 0.0f, z, 1.0f);                            /* 4470 */
    _SetVector(wpos12, 0.0f, y, 0.0f, 1.0f);                            /* 4471 */
    sceVu0UnitMatrix(wlm);                                              /* 4472 */
    sceVu0RotMatrixX(wlm, wlm, xrot);                                   /* 4473 */
    sceVu0RotMatrixY(wlm, wlm, yrot);                                   /* 4474 */
    sceVu0TransMatrix(wlm, wlm, wpos12);
    sceVu0ApplyMatrix(mpos1, wlm, wpos11);

    /* Lamp position: the same construction off the *body* heading, so the
     * lamp stays on her hand while the beam swings. */
    _SetVector(wpos21, 0.0f, 0.0f, pz, 1.0f);
    _SetVector(wpos22, 0.0f, py, 0.0f, 1.0f);
    sceVu0UnitMatrix(wlm);
    sceVu0RotMatrixX(wlm, wlm, xrot);
    sceVu0RotMatrixY(wlm, wlm, plyr_wrk.cmn_wrk.mbox.rot[1]);
    sceVu0TransMatrix(wlm, wlm, wpos22);
    sceVu0ApplyMatrix(mpos2, wlm, wpos21);

    /* bwp is the smoothed body position the lights hang off -- using mbox.pos
     * directly makes the beam jitter on every collision push-out. */
    sceVu0AddVector(mpos2, plyr_wrk.bwp, mpos2);                        /* 4486 */
    mpos1[3] = 1.0f;
    mpos2[3] = 1.0f;

    if (debug_var.fl_line != 0)
    {
        sceVu0AddVector(wpos21, mpos2, mpos1);
    }

    g3dxVu0CopyVector(plyr_wrk.fl.vPosition, mpos2);
    sceVu0Normalize(plyr_wrk.fl.vDirection, mpos1);

    if (debug_var.fl_line != 0)
    {
        DrawLine(mpos2, 0xff, 0x80, 0x80, 0x80, wpos21, 0xff, 0x80, 0x80, 0x80);
    }

    plyr_wrk.fl2.Type = G3DLIGHTTYPE_FORCE_DWORD;

    plyr_wrk.fl.vDiffuse[0]  = 1.0f;
    plyr_wrk.fl.vDiffuse[1]  = 1.0f;
    plyr_wrk.fl.vDiffuse[2]  = 1.0f;
    plyr_wrk.fl.vDiffuse[3]  = 1.0f;
    plyr_wrk.fl.vSpecular[0] = 1.0f;
    plyr_wrk.fl.vSpecular[1] = 1.0f;
    plyr_wrk.fl.vSpecular[2] = 1.0f;
    plyr_wrk.fl.vSpecular[3] = 1.0f;

    gra3dSetLightIntens(&plyr_wrk.fl, intens);
    plyr_wrk.fl.fMinRange = range * 0.125f;
    plyr_wrk.fl.fMaxRange = range;
    plyr_wrk.fl.fFalloff  = 1.0f;

    /* Bounce light: a dim point light on the floor in front of her, so the
     * spot's hard edge is not the only thing lighting the near ground. */
    _SetVector(vref, 0.0f, rfy, rfz, 1.0f);
    sceVu0UnitMatrix(wlm);
    sceVu0RotMatrixX(wlm, wlm, xrot);
    sceVu0RotMatrixY(wlm, wlm, yrot);
    sceVu0TransMatrix(wlm, wlm, plyr_wrk.bwp);
    sceVu0ApplyMatrix(vref, wlm, vref);

    if (debug_var.fl_line != 0)
    {
        DrawCrossLine(vref);
    }

    g3dxVu0CopyVector(plyr_wrk.reflectionlight.vPosition, vref);
    g3dxVu0CopyVector(plyr_wrk.reflectionlight.vDiffuse, plyr_wrk.fl.vDiffuse);
    plyr_wrk.reflectionlight.Type      = G3DLIGHT_POINT;
    plyr_wrk.reflectionlight.fMinRange = rfrange * 0.5f;
    plyr_wrk.reflectionlight.fMaxRange = rfrange;
}

/* The "step" rig: a plain point light sitting on the player, with no beam and
 * no aiming.  Used where the torch is stowed but she still has to be lit. */
static void _SetFlashlightStep(int move_sw)                             /* 4600 */
{
    float fRange;

    (void)move_sw;

    plyr_wrk.fl.Type  = G3DLIGHT_POINT;
    plyr_wrk.fl2.Type = G3DLIGHTTYPE_FORCE_DWORD;
    plyr_wrk.reflectionlight.Type = G3DLIGHT_POINT;

    _ClearVector(plyr_wrk.fl.vDirection);
    _ClearVector(plyr_wrk.reflectionlight.vDirection);

    plyr_wrk.fl.vPosition[0] = plyr_wrk.cmn_wrk.mbox.pos[0];
    plyr_wrk.fl.vPosition[1] = plyr_wrk.cmn_wrk.mbox.pos[1] +
                               debug_var.fYFlashlightStep;
    plyr_wrk.fl.vPosition[2] = plyr_wrk.cmn_wrk.mbox.pos[2];
    plyr_wrk.fl.vPosition[3] = 1.0f;

    plyr_wrk.fl.vDiffuse[0]  = 1.0f;
    plyr_wrk.fl.vDiffuse[1]  = 1.0f;
    plyr_wrk.fl.vDiffuse[2]  = 1.0f;
    plyr_wrk.fl.vDiffuse[3]  = 1.0f;
    plyr_wrk.fl.vSpecular[0] = 1.0f;
    plyr_wrk.fl.vSpecular[1] = 1.0f;
    plyr_wrk.fl.vSpecular[2] = 1.0f;
    plyr_wrk.fl.vSpecular[3] = 1.0f;

    fRange = debug_var.fRangeFlashlightStep;
    gra3dSetLightIntens(&plyr_wrk.fl, debug_var.fl_intens);
    plyr_wrk.fl.fFalloff  = 1.0f;
    plyr_wrk.fl.fMaxRange = fRange;
    plyr_wrk.fl.fMinRange = fRange * 0.5f;

    g3dxVu0CopyVector(plyr_wrk.reflectionlight.vPosition, plyr_wrk.fl.vPosition);
}

/* data 3f39f0.  Indexed by PLAYERFLASHLIGHTTYPE. */
typedef void (*LPFUNC_SETFLASHLIGHT)(int move_sw);

void PlyrFlashlight(int move_sw)                                        /* 4646 */
{
    LPFUNC_SETFLASHLIGHT apFunc[2] =                                    /* 4648 */
    {
        _SetFlashlightHand,
        _SetFlashlightStep
    };

    G3DASSERT((int)s_FlashlightType < NUM_PLAYERFLASHLIGHTTYPE,         /* 4655 */
              "s_FlashlightType < NUM_PLAYERFLASHLIGHTTYPE");

    apFunc[s_FlashlightType](move_sw);                                  /* 4658 */
}


/* ==========================================================================
 *  Controller vibration (player.o 0x00239ff8..0x0023a3d0).
 *
 *  Two independent channels: the small motor (PlyrVibCtrl) and the big one
 *  (PlyrVibCtrlBig), each holding a countdown so a one-shot request survives
 *  the frames after it was made.  PlyrVibCheck() drives the proximity pulse
 *  that warns of an approaching ghost.
 * ======================================================================== */

/* The heartbeat: the closer the nearest ghost, the shorter the gap between
 * pulses and the harder each one is.  Both are linear in distance over the
 * 300..3000 band, and the pulse itself lasts 18 frames. */
void PlyrVibCheck(void)                                                 /* 3055 */
{
    static u_short mvib_time0;              /* sdata 3f39e0 */
    static u_short mvib_time1;              /* sdata 3f39e2 */
    static u_char  mvib_degree;             /* sbss  3f4eec */

    ENE_WRK *ewp  = (ENE_WRK *)0;
    float    dist = 0.0f;                                               /* 3075 */
    u_char   chk  = 0;
    u_char   no   = 0;
    u_char   i;

    for (i = 0; i < ENE_WRK_MAX; i++)
    {
        /* attr 0x80 marks a ghost that never rumbles (scripted set pieces). */
        if ((ene_wrk[i].attr & 0x80) != 0)                              /* 3076 */
        {
            continue;
        }
        if (ene_wrk[i].status != ENE_STATUS_ACT) { continue; }          /* 3077 */
        if (ene_wrk[i].st.hp == 0)               { continue; }
        if ((ene_wrk[i].st.sta & 0x1000000) != 0) { continue; }

        /* 0x8000 is "engaged with someone"; only rumble if that someone is
         * the player (target_n == 1). */
        if ((ene_wrk[i].st.sta & 0x8000) != 0 && ene_wrk[i].target_n != 1)
        {
            continue;
        }

        {
            float d = GetDistV2(plyr_wrk.cmn_wrk.mbox.pos,              /* 3081 */
                                ene_wrk[i].mbox.pos);

            if (dist == 0.0f || d < dist)                               /* 3082 */
            {
                dist = d;                                               /* 3083 */
                no   = i;                                               /* 3084 */
                chk  = 1;                                               /* 3085 */
                ewp  = &ene_wrk[i];                                     /* 3086 */
            }
        }
    }

    if (chk != 0 && dist >= 1.0f && dist <= 4000.0f)                    /* 3092 */
    {
        if (mvib_time1 != 0)                                            /* 3094 */
        {
            /* Mid-pulse.  A ghost that turned un-rumbleable mid-pulse just
             * drops it rather than finishing. */
            if ((ewp->attr & 0x80) != 0)                                /* 3095 */
            {
                goto out;
            }

            mvib_time1--;                                               /* 3096 */
            if (mvib_time1 == 0x11)
            {
                SystemBankPlay(13, 1, 1, 0, (SND_3D_SET *)0, 0x3200, 0x1000);    /* 3097 */
            }
            VibrateRequest2(0, mvib_degree);                            /* 3099 */
        }
        else if (mvib_time0 != 0)                                       /* 3102 */
        {
            mvib_time0--;                                               /* 3114 */
        }
        else
        {
            /* Start a new pulse.  Clamp into 300..3000 first, then map that
             * band onto 0..90 frames of gap and 0..128 of strength; the
             * strength is inverted, so nearer is stronger. */
            float d = dist;

            if (d > 3000.0f)     { d = 3000.0f; }                       /* 3103 */
            else if (d < 300.0f) { d = 300.0f; }

            d -= 300.0f;                                                /* 3104 */

            mvib_time0  = (u_short)(int)((d * 90.0f) / 2700.0f);        /* 3105 */
            mvib_time1  = 0x12;                                         /* 3106 */
            mvib_degree = (u_char)~(u_char)(int)((d * 128.0f) / 2700.0f);        /* 3107 */

            plyr_wrk.cmn_wrk.st.sp_down_fl = 1;                         /* 3108 */

            /* Type 2 ghosts announce themselves at half strength. */
            if (ene_wrk[no].type == 2)                                  /* 3110 */
            {
                mvib_degree >>= 1;                                      /* 3111 */
            }
        }
    }

out:
    PlyrVibCtrl(0);                                                     /* 3165 */
    PlyrVibCtrlBig(0, 0);                                               /* 3166 */
}

/* time == 0 just advances an in-flight request; a non-zero time raises one,
 * but never shortens a longer request already running. */
void PlyrVibCtrl(u_char time)                                           /* 3180 */
{
    if (time != 0 && plyr_wrk.vib_time_sm < time)                       /* 3182 */
    {
        plyr_wrk.vib_time_sm = time;                                    /* 3183 */
    }

    if (plyr_wrk.vib_time_sm != 0)                                      /* 3185 */
    {
        VibrateRequest1(0, 1);                                          /* 3186 */
        plyr_wrk.vib_time_sm--;                                         /* 3187 */
    }
}

void PlyrVibCtrlBig(u_char pow, u_char time)                            /* 3191 */
{
    static u_char mpow;                     /* sdata 3f39e4 */

    if (time != 0 && plyr_wrk.vib_time_bg < time)                       /* 3194 */
    {
        plyr_wrk.vib_time_bg = time;                                    /* 3195 */
        mpow = pow;                                                     /* 3196 */
    }

    if (plyr_wrk.vib_time_bg != 0)                                      /* 3198 */
    {
        VibrateRequest2(0, mpow);                                       /* 3199 */
        plyr_wrk.vib_time_bg--;                                         /* 3200 */
    }
}


/* ==========================================================================
 *  Enemy proximity and camera tracing
 *  (player.o 0x002366a0, 0x002378a8..0x002379e0, 0x00239a28, 0x0023dea0).
 * ======================================================================== */

/* Raises the two battle bits from what is actually in the room this frame:
 * 0x20 for an ordinary ghost, 0x40 for a type-2 one.  Both are recomputed
 * from scratch every frame, so nothing has to clear them on death. */
void PlyrBattleCheck(void)                                              /* 843 */
{
    int i;

    plyr_wrk.cmn_wrk.st.sta &= ~0x60;

    for (i = 0; i < ENE_WRK_MAX; i++)
    {
        if (IsActEnemy(i) != 0 && ene_wrk[i].type != 2)
        {
            plyr_wrk.cmn_wrk.st.sta |= 0x20;
        }
        else if (IsActEnemy(i) != 0 && ene_wrk[i].type == 2)
        {
            plyr_wrk.cmn_wrk.st.sta |= 0x40;
        }
    }
}

/* Closest ordinary ghost (type < 2).  Unlike NearAllEneInfo() this skips the
 * special classes, so the "a ghost is near" music and the HP drain do not
 * react to set-piece apparitions. */
void NearEneInfo(PLCMN_WRK *cmn)                                        /* 2845 */
{
    float  dist[2] = { 0.0f, 99999.0f };     /* sdata 3f39d0 */
    u_char no = 0xff;
    u_char i;

    for (i = 0; i < ENE_WRK_MAX; i++)
    {
        if (ene_wrk[i].type < 2 &&                                      /* 2851 */
            ene_wrk[i].status == ENE_STATUS_ACT &&
            ene_wrk[i].st.hp != 0 &&
            (ene_wrk[i].st.sta & 0x1000000) == 0)
        {
            float d = GetDistV(cmn->mbox.pos, ene_wrk[i].mbox.pos);

            if (d < dist[1])
            {
                dist[1] = d;
                no      = i;
            }
            dist[0] = d;
        }
    }

    cmn->near_ene_dist_old = cmn->near_ene_dist;
    cmn->near_ene_no       = no;
    cmn->near_ene_dist     = dist[1];
}

/* Points the player at one ghost and hands the turn to the mbox: rspd is the
 * per-frame delta (the remaining angle spread over 50 frames) and trot the
 * target.  0x40000 tells the rest of the frame a trace is in progress. */
static void playerSetSearchEne(int iEneWrkNo)                           /* 1466 */
{
    float tv[4];

    plyr_wrk.cmn_wrk.st.sta |= 0x40000;

    g3dxVu0CopyVector(tv, plyr_wrk.cmn_wrk.mbox.pos);
    tv[1] += -709.0f;                       /* lit4 3ee704, the eye offset */

    GetTrgtRot(tv, ene_wrk[iEneWrkNo].mpos.p0, plyr_wrk.cmn_wrk.mbox.rspd, 3);

    /* Pitch is measured against the finder's own pitch, not the body's. */
    plyr_wrk.cmn_wrk.mbox.rspd[0] -= plyr_wrk.frot_x;
    RotLimitChk(&plyr_wrk.cmn_wrk.mbox.rspd[0]);
    plyr_wrk.cmn_wrk.mbox.rspd[1] -= plyr_wrk.cmn_wrk.mbox.rot[1];
    RotLimitChk(&plyr_wrk.cmn_wrk.mbox.rspd[1]);

    /* (x + x) / 100 == x / 50: the turn is spread over 50 frames. */
    plyr_wrk.cmn_wrk.mbox.rspd[0] =
        (plyr_wrk.cmn_wrk.mbox.rspd[0] + plyr_wrk.cmn_wrk.mbox.rspd[0]) / 100.0f;
    plyr_wrk.cmn_wrk.mbox.rspd[1] =
        (plyr_wrk.cmn_wrk.mbox.rspd[1] + plyr_wrk.cmn_wrk.mbox.rspd[1]) / 100.0f;

    g3dxVu0CopyVector(plyr_wrk.cmn_wrk.mbox.trot, plyr_wrk.cmn_wrk.mbox.rot);
    plyr_wrk.cmn_wrk.mbox.trot[0] = plyr_wrk.frot_x;
    plyr_wrk.cmn_wrk.mbox.mloop   = 0.0f;
}

/* Turns the player towards a ghost.  With ew given, that ghost; with NULL,
 * the nearest visible one inside 5000 units -- and if none of those qualify,
 * the nearest one at all, ignoring visibility. */
void ReqCamTraceNearEne(void *ew)                                       /* 1478 */
{
    float  dist[2];
    u_char trgt = 0xff;
    u_char i;

    if (ew != (void *)0)
    {
        trgt = ((ENE_WRK *)ew)->alg.idx;                                /* 1512 */
    }
    else
    {
        dist[1] = 0.0f;

        for (i = 0; i < ENE_WRK_MAX; i++)
        {
            if (ene_wrk[i].status != ENE_STATUS_ACT)   { continue; }
            if (ene_wrk[i].st.hp == 0)                 { continue; }
            if ((ene_wrk[i].st.sta & 0x1000000) != 0)  { continue; }
            if (ene_wrk[i].tr_rate == 0)               { continue; }

            {
                float d = GetDistV(plyr_wrk.cmn_wrk.mbox.pos, ene_wrk[i].mbox.pos);

                if (d <= 5000.0f && (dist[1] == 0.0f || d < dist[1]))
                {
                    dist[1] = d;
                    trgt    = i;
                }
            }
        }

        if (trgt == 0xff)
        {
            /* Second pass: transparency and the hidden flag no longer matter,
             * because something has to be looked at. */
            dist[1] = 0.0f;

            for (i = 0; i < ENE_WRK_MAX; i++)
            {
                if (ene_wrk[i].status != ENE_STATUS_ACT) { continue; }
                if (ene_wrk[i].st.hp == 0)               { continue; }

                {
                    float d = GetDistV(plyr_wrk.cmn_wrk.mbox.pos,
                                       ene_wrk[i].mbox.pos);

                    if (d <= 5000.0f && (dist[1] == 0.0f || d < dist[1]))
                    {
                        dist[1] = d;
                        trgt    = i;
                    }
                }
            }
        }
    }

    if (trgt == 0xff)
    {
        _ClearVector(plyr_wrk.cmn_wrk.mbox.rspd);
        return;
    }

    playerSetSearchEne(trgt);
}

/* Empty in the shipped prototype. */
void PlyrCamTurnChk(void)                                               /* 2896 */
{
}


/* ==========================================================================
 *  HP / SP drain (player.o 0x002372a8, 0x002376b0).
 * ======================================================================== */

/* Spirit power bleeds off while a ghost is close.  The rate steps up in three
 * bands, and the whole thing only fires on the frames PlyrVibCheck() marked
 * with sp_down_fl, so the drain is tied to the heartbeat pulse.
 *
 * sta bits: 0x400 draining, 0x800 out of range (recovering), 0x100 empty. */
void PlyrSPdownCtrl(PLCMN_WRK *cmn)                                     /* 1232 */
{
    float dist = 10000.0f;
    int   i;

    /* Scripted override: force the "draining" flag on regardless of what the
     * ghosts are doing.
     *
     * The ROM's rspdmg update here is dead: 0x002372f4 emits only the
     * divide-by-zero guard for sptime and then loads rspdmg and stores it
     * straight back, with no div or mult in between -- the quotient that was
     * meant to be added folded to zero and GCC 2.96 kept the trap (it has a
     * side effect) while dropping the arithmetic.  Reproduced as the ROM
     * behaves, not as it reads: adding spmax/sptime here would drain spirit
     * power through scripted sequences that in fact drain none. */
    if (m_plyr_camera.filament.rt_ev_wrk.sptype == 1 && cmn == pl_sta[0])
    {
        cmn->st.sta |= 0x400;
    }

    for (i = 0; i < ENE_WRK_MAX; i++)
    {
        /* attr 0x20 marks a ghost that does not drain. */
        if (IsActEnemy(i) != 0 && (ene_wrk[i].attr & 0x20) == 0)
        {
            float d = GetDistV(cmn->mbox.pos, ene_wrk[i].mbox.pos);

            if (d <= dist)
            {
                dist = d;
            }
        }
    }

    if (debug_var.muteki != 0)
    {
        dist = 100000.0f;
    }

    if (dist > 1450.0f)
    {
        cmn->st.sta = (cmn->st.sta | 0x800) & ~0x400;
    }
    else
    {
        if (cmn->st.sp_down_fl != 0)
        {
            if (dist <= 900.0f)
            {
                cmn->st.rspdmg += 500;
                cmn->st.sp_down_fl = 0;
            }
            else if (dist <= 1250.0f)
            {
                cmn->st.rspdmg += 200;
                cmn->st.sp_down_fl = 0;
            }
            else if (dist <= 1450.0f)
            {
                cmn->st.rspdmg += 20;
                cmn->st.sp_down_fl = 0;
            }
            else
            {
                cmn->st.sp_down_fl = 0;
            }
        }
        cmn->st.sta = (cmn->st.sta | 0x400) & ~0x800;
    }

    /* Queued pure-SP damage.  The 1.1 scale is applied twice: once to test
     * whether the hit would overdraw, once to take it. */
    if (cmn->st.rspdmg != 0)
    {
        if (cmn->st.sp == 0)
        {
            cmn->st.rspdmg = 0;
            cmn->st.sta &= ~0x400;
        }
        else
        {
            u_short dmg = cmn->st.rspdmg;

            cmn->st.rspdmg = 0;
            if ((u_short)(int)((float)dmg * 1.0999999f) <= cmn->st.sp)
            {
                cmn->st.sp -= (u_short)(int)((float)dmg * 1.0999999f);
            }
            else
            {
                cmn->st.sp = 0;
            }
        }
        cmn->st.sp_recover_time = 0;
    }

    /* Queued combined HP+SP damage takes the SP half here; PlyrHPdownCtrl()
     * takes the HP half only once SP has run out.  0x100 says it has. */
    if (cmn->st.rhspdmg != 0)
    {
        u_short sp = cmn->st.sp;

        if (sp == 0)
        {
            /* Nothing left to absorb it: leave rhspdmg queued for
             * PlyrHPdownCtrl() to take out of health instead. */
            cmn->st.sta |= 0x100;
        }
        else
        {
            u_short dmg = cmn->st.rhspdmg;

            cmn->st.rhspdmg = 0;
            if ((u_short)(int)((float)dmg * 1.0999999f) <= sp)
            {
                sp -= (u_short)(int)((float)dmg * 1.0999999f);
            }
            else
            {
                sp = 0;
            }
            cmn->st.sp = sp;
        }

        /* Reached zero with the request still queued: stop draining and say
         * so, which is what lets the HP pass pick it up. */
        if (sp == 0 && cmn->st.rhspdmg != 0)
        {
            cmn->st.sta = (cmn->st.sta & ~0x400) | 0x100;
        }
        cmn->st.sp_recover_time = 0;
    }

    if (cmn->st.rhspdmg == 0 && cmn->st.rspdmg == 0)
    {
        cmn->st.sta &= ~0x400;
    }

    if (cmn->st.rhpdmg != 0)
    {
        cmn->st.sp_recover_time = 0;
    }

    /* Recover: 90 quiet frames, then 45 points a frame back to full.  0xd00
     * masks in 0x800 (out of range), 0x400 (draining) and 0x100 (empty), so
     * recovery only runs when out of range and neither of the others is up. */
    if ((cmn->st.sta & 0xd00) == 0x800)
    {
        if (cmn->st.sp < cmn->st.spmax)
        {
            if (cmn->st.sp_recover_time < 90)
            {
                cmn->st.sp_recover_time++;
            }
            else
            {
                u_int n = cmn->st.sp + 45;

                if (n > cmn->st.spmax)
                {
                    n = cmn->st.spmax;
                }
                cmn->st.sp = (u_short)n;
            }
        }
        else
        {
            cmn->st.sta &= ~0x800;
        }
    }
}

/* Applies the queued stamina loss.  Combined HP+SP damage only reaches HP
 * once SP is empty, so the spirit gauge is a buffer in front of health. */
void PlyrHPdownCtrl(PLCMN_WRK *cmn)                                     /* 1344 */
{
    u_short down1 = 0;
    u_short down2 = 0;

    /* The sister only regenerates while she is not mid-damage; 0x200 is her
     * regen flag and nothing raises it for the player. */
    if (cmn == pl_sta[1] && (u_char)(sis_wrk.cmn_wrk.mode - 1) > 3)
    {
        cmn->st.sta |= 0x200;
    }

    if (cmn->st.rhspdmg == 0 && cmn->st.rhpdmg == 0)
    {
        if ((cmn->st.sta & 0x200) == 0)
        {
            return;
        }

        cmn->st.hp += 100;
        if (cmn->st.hp >= cmn->st.hpmax)
        {
            cmn->st.hp   = cmn->st.hpmax;
            cmn->st.sta &= ~0x200;
        }
        return;
    }

    /* Combined damage only spills into HP once SP is gone and no plain SP
     * damage is still queued ahead of it. */
    if (cmn->st.rhspdmg != 0 && cmn->st.sp == 0 && cmn->st.rspdmg == 0)
    {
        down1 = cmn->st.rhspdmg;
        cmn->st.rhspdmg = 0;
    }

    down2 = cmn->st.rhpdmg;
    if (down2 != 0)
    {
        cmn->st.rhpdmg = 0;
    }

    if (cmn->st.rhspdmg == 0 && cmn->st.rhpdmg == 0)
    {
        cmn->st.sta &= ~0x100;
    }

    if (cmn->st.hp < (u_short)(down1 + down2))
    {
        cmn->st.hp = 0;
    }
    else
    {
        cmn->st.hp -= (u_short)(down1 + down2);
    }

    if (cmn->st.hp != 0)
    {
        return;
    }

    if (cmn == pl_sta[1])
    {
        ReqSisDead(2);
        cmn->st.sta &= ~0x100;
        return;
    }

    /* Item 7 is the stone mirror -- it revives her once and is consumed. */
    if (ItemUsePossible(7, 1) != 0)
    {
        SystemBankPlay(8, 1, 1, 0, (SND_3D_SET *)0, 0x3200, 0x1000);
        SystemBankPlay(10, 1, 1, 0, (SND_3D_SET *)0, 0x3200, 0x1000);

        cmn->st.hp = cmn->st.hpmax;
        plyr_wrk.cmn_wrk.st.sta &= ~0x100;
        ItemLost(7, 1);
        plyr_wrk.cmn_wrk.st.rhspdmg = 0;
        plyr_wrk.cmn_wrk.st.rspdmg  = 0;
        return;
    }

    ReqPlyrDead(1);
    cmn->st.sta &= ~0x100;
}


/* ==========================================================================
 *  Aim quality ("EP", enemy power) helpers (player.o 0x0023cb80..0x0023d058).
 *
 *  Each returns 0..100 for how well the given point is lined up.  CulcEP()
 *  scores both axes, CulcEP2() the yaw alone, CulcEP3() the yaw with a
 *  distance falloff.  The music and the filament needle ride on these.
 * ======================================================================== */

/* Full aim score: yaw against a 135-degree half-cone, pitch against a fixed
 * 60 (which is in *degrees* here, not radians -- the ROM never converts
 * rv[0], so the pitch term is effectively always saturated).  Reproduced as
 * written; "fixing" it would change the score the ROM actually computes. */
float CulcEP(float *v0, float *v1)                                      /* 4809 */
{
    float rv[4];
    float dx;
    float dy = 0.0f;

    GetTrgtRot(v0, v1, rv, 3);

    rv[1] -= plyr_wrk.cmn_wrk.mbox.rot[1];
    RotLimitChk(&rv[1]);
    rv[1] = fabsf(rv[1]);
    if (rv[1] <= 2.356194f)                 /* 135 deg */
    {
        dy = (2.356194f - rv[1]) / 2.356194f;
    }

    rv[0] -= plyr_wrk.frot_x;
    RotLimitChk(&rv[0]);
    dx = 0.0f;
    if (fabsf(rv[0]) <= 60.0f)
    {
        dx = (60.0f - fabsf(rv[0])) / 60.0f;
    }

    return dx * dy * 100.0f;
}

/* Yaw only. */
float CulcEP2(float *v0, float *v1)                                     /* 4838 */
{
    float rv[4];
    float degree = 0.0f;

    GetTrgtRot(v0, v1, rv, 2);

    rv[1] -= plyr_wrk.cmn_wrk.mbox.rot[1];
    RotLimitChk(&rv[1]);

    if (fabsf(rv[1]) <= 2.356194f)          /* 135 deg */
    {
        degree = ((2.356194f - fabsf(rv[1])) * 100.0f) / 2.356194f;
    }

    return degree;
}

/* Yaw with distance falloff, measured from the camera in finder mode and from
 * the player otherwise.  Full score inside 600 units, fading to nothing by
 * 5000 (the 4400 divisor is that span). */
float CulcEP3(float *v1)                                                /* 4858 */
{
    float rv[4];
    float v0[4];
    float degree = 0.0f;
    float dist;

    if (plyr_wrk.cmn_wrk.mode == 6)
    {
        sceVu0CopyVector(v0, gra3dGetCamera()->matCoord[3]);
    }
    else
    {
        sceVu0CopyVector(v0, plyr_wrk.cmn_wrk.mbox.pos);
    }

    GetTrgtRot(v0, v1, rv, 2);
    rv[1] -= plyr_wrk.cmn_wrk.mbox.rot[1];
    RotLimitChk(&rv[1]);
    rv[1] = fabsf(rv[1]);

    if (rv[1] <= 2.356194f)                 /* 135 deg */
    {
        degree = (2.356194f - rv[1]) / 2.356194f;
    }

    dist = GetDistV(plyr_wrk.cmn_wrk.mbox.pos, v1);
    if (dist > 600.0f)
    {
        if (dist < 5000.0f)
        {
            degree *= 1.0f - ((dist - 600.0f) / 4400.0f);
        }
        else
        {
            degree *= 0.0f;
        }
    }
    else
    {
        degree *= 1.0f;
    }

    return degree;
}

/* Aim score for the nearest visible ghost, or 0 when there is none in range.
 * In finder mode both the search and the score are done from the camera --
 * the viewfinder *is* the camera, so the player's own position is irrelevant.
 * attr 0x80 ghosts are excluded, same as for the rumble. */
float GetEnePowerDegree(void)                                           /* 4700 */
{
    float  sv[4];
    float  dist[2];
    float  degree = 0.0f;
    u_char i;

    dist[0] = 0.0f;

    if (plyr_wrk.cmn_wrk.mode == 6)
    {
        for (i = 0; i < ENE_WRK_MAX; i++)
        {
            if (ene_wrk[i].status == ENE_STATUS_ACT &&
                ene_wrk[i].st.hp != 0 &&
                (ene_wrk[i].st.sta & 0x1000000) == 0 &&
                (ene_wrk[i].attr & 0x80) == 0)
            {
                float d = GetDistV(gra3dGetCamera()->matCoord[3],
                                   ene_wrk[i].mpos.p0);

                if (dist[0] == 0.0f || d < dist[0])
                {
                    g3dxVu0CopyVector(sv, ene_wrk[i].mpos.p0);
                    dist[0] = d;
                }
            }
        }

        if (dist[0] >= 1.0f && dist[0] <= 4000.0f)
        {
            degree = CulcEP(gra3dGetCamera()->matCoord[3], sv);
        }
    }
    else
    {
        for (i = 0; i < ENE_WRK_MAX; i++)
        {
            if (ene_wrk[i].status == ENE_STATUS_ACT &&
                ene_wrk[i].st.hp != 0 &&
                (ene_wrk[i].st.sta & 0x1000000) == 0 &&
                (ene_wrk[i].attr & 0x80) == 0)
            {
                float d = GetDistV(plyr_wrk.cmn_wrk.mbox.pos, ene_wrk[i].mpos.p0);

                if (dist[0] == 0.0f || d < dist[0])
                {
                    g3dxVu0CopyVector(sv, ene_wrk[i].mpos.p0);
                    dist[0] = d;
                }
            }
        }

        if (dist[0] >= 1.0f && dist[0] <= 4000.0f)
        {
            degree = CulcEP2(plyr_wrk.cmn_wrk.mbox.pos, sv);
        }
    }

    return degree;
}

/* Empty in the shipped prototype. */
void FModeScreenEffect(void)                                            /* 2836 */
{
}


/* ==========================================================================
 *  Death sequence (player.o 0x00236090).
 *
 *  modedead is a little state machine, not a mode: ReqPlyrDead() seeds it and
 *  each frame advances one step.  Two routes exist -- 0x1e.. is the plain
 *  fade-to-white death, 0x1f.. the "caught by a ghost" one that pulls the
 *  camera in on the pair first.  Returns non-zero on the frame the sequence
 *  finishes and the game-over screen should take over.
 * ======================================================================== */

int PlyrDead(void)                                                      /* 630 */
{
    static int cnt;                         /* sdata 3f3974 */

    DISP_SQAR dsq;
    SQAR_DAT  fade_bg;
    float     center[4];
    u_char    eneno = plyr_wrk.cmn_wrk.atk_eneno;
    int       ret = 0;

    /* Full-screen white quad the later steps cross-fade through. */
    fade_bg.w     = 640;
    fade_bg.h     = 448;
    fade_bg.x     = 0;
    fade_bg.y     = 0;
    fade_bg.pri   = 0;
    fade_bg.r     = 0xff;
    fade_bg.g     = 0xff;
    fade_bg.b     = 0xff;
    fade_bg.alpha = 0;

    switch (plyr_wrk.modedead)
    {
    case 1:
    case 3:
        /* Ordinary death: hand over to the 0x1e chain. */
        plyr_wrk.modedead = 0x1e;
        cnt = 0;
        return 0;

    case 0x15:
    case 0x1f:
        /* Caught: frame the point between her and the ghost holding her. */
        GetCenterPoint(center, plyr_wrk.cmn_wrk.mbox.pos, ene_wrk[eneno].mbox.pos);
        ReqPlyrApproachCameraCtrl(center, 600.0f, 700.0f);
        plyr_mdlBankPlay(2, 1, 1, 0, &plyr_wrk.s3d, 0x3200, 0x1000);
        plyr_wrk.modedead++;
        cnt = 0;
        return 0;

    case 0x14:
    case 0x1e:
        break;

    case 0x16:
        if (++cnt < 15)
        {
            return 0;
        }
        FadeOutReq(0xff, 0xff, 0xff, 30);
        cnt = 0;
        break;

    case 0x17:
        if (cnt + 1 < 30)
        {
            cnt++;
            return 0;
        }
        cnt = 0;
        break;

    case 0x18:
        if (++cnt < 30)
        {
            return 0;
        }
        plyr_wrk.cmn_wrk.st.hp = 0;
        plyr_wrk.cmn_wrk.st.sp = 0;
        EndPlyrApproachCameraCtrl();
        FadeInReq(0xff, 0xff, 0xff, 30);
        SetPlyrAnime(0x40, 2);
        cnt = 0;
        plyr_wrk.modedead++;
        return 0;

    case 0x19:
        if (++cnt < 30)
        {
            return 0;
        }
        ret = 1;
        SetOpenCondSwitch(1);
        break;

    case 0x20:
        if (cnt + 1 < 20)
        {
            cnt++;
            return 0;
        }
        cnt = 0;
        break;

    case 0x21:
        /* Fade the white quad up to half over 30 frames, then swap to the
         * dead camera. */
        CopySqrDToSqr(&dsq, &fade_bg);
        dsq.alpha = (u_char)((cnt << 7) / 30);
        DispSqrD(&dsq);

        if (cnt + 1 < 30)
        {
            cnt++;
            return 0;
        }
        cnt = 0;
        SetDrawFLG_PL_GameOver();
        EndPlyrApproachCameraCtrl();
        ReqPlyrDeadCameraCtrl(plyr_wrk.cmn_wrk.mbox.pos, 200.0f, 500.0f, 1000.0f,
                              plyr_wrk.cmn_wrk.mbox.rot);
        plyr_wrk.modedead++;
        return 0;

    case 0x22:
        CopySqrDToSqr(&dsq, &fade_bg);
        dsq.alpha = 0x80;
        DispSqrD(&dsq);

        if (++cnt < 30)
        {
            return 0;
        }
        plyr_wrk.cmn_wrk.st.hp = 0;
        plyr_wrk.cmn_wrk.st.sp = 0;
        SetPlyrAnime(0x40, 2);
        cnt = 0;
        plyr_wrk.modedead++;
        return 0;

    case 0x23:
        /* Fade the quad back down; her last breath lands halfway through. */
        CopySqrDToSqr(&dsq, &fade_bg);
        dsq.alpha = (u_char)(((30 - cnt) * 0x80) / 30);
        DispSqrD(&dsq);

        if (cnt == 14)
        {
            plyr_mdlBankPlay(4, 1, 1, 0, &plyr_wrk.s3d, 0x3200, 0x1000);
        }
        if (cnt + 1 < 30)
        {
            cnt++;
            return 0;
        }
        cnt = 0;
        break;

    case 0x24:
        if (++cnt < 20)
        {
            return 0;
        }
        FadeOutReq(0, 0, 0, 30);
        cnt = 0;
        break;

    case 0x25:
        if (cnt + 1 < 50)
        {
            cnt++;
            return 0;
        }
        cnt = 0;
        EndPlyrApproachCameraCtrl();
        /* fall through */
    case 10:
        ret = 1;
        SetOpenCondSwitch(1);
        break;

    default:
        return 0;
    }

    plyr_wrk.modedead++;
    return ret;
}


/* ==========================================================================
 *  AWAITING RECONSTRUCTION.  Real functions in player.o, stubbed here so the
 *  reconstructed spine links and runs.  Addresses are the ROM's.
 * ======================================================================== */

/* static in the ROM.  Drives the sister's head towards Mio; waits on
 * sister.c's SisNeckRegisterTarget / GetDist_Sister2Player / IsSisterTurn. */
static void PlyrAttractSisMain(void) { }                /* 0x00236530 */


/* ==========================================================================
 *  Shot scoring and damage (player.o 0x0023a450..0x0023bb98).
 *
 *  One shutter press runs the whole chain, in this order:
 *
 *    PlyrPhotoChk2Sub  spends a frame of film, then
 *      PhotoFlyChk       fades out every fly in frame
 *      PhotoDmgChk2      per ghost: how much damage, and the particles
 *        PhotoDmgChkSub2   the damage itself, and what kind of hit it was
 *      PlayerTakePictJob the score, the bonuses and the album entry
 *        PhotoPointCulcEne2  per ghost: base points and the five bonuses
 *
 *  Two things are worth knowing before reading any of it.  Damage lands on
 *  ENE_WRK::st.dmg and is applied by enemy.c later in the frame, not here --
 *  so "hp <= dmg" throughout means "this shot will finish it", in the future
 *  tense.  And the two counts the multi-shot bonuses key off are different:
 *  `cnt` is subjects in frame, `iBusterCountThisTime` ghosts this shot kills.
 * ======================================================================== */

/* A ghost's ghost-list slot as the album records it: three message ids per
 * ghost, and 0xb5 is the end of the list.  Inlined at every call site in the
 * ROM -- it leaves no symbol -- but functions.txt names its local in each
 * caller, and the line numbers (3515/3516/3519) are its own. */
static int GetSoulListNo(int no)                                        /* 3513 */
{
    int ghost_list_no = -1;                                             /* 3515 */

    if (no < 0xb5)                                                      /* 3516 */
    {
        ghost_list_no = no * 3;                                         /* 3519 */
    }

    return ghost_list_no;
}

/* Scores one ghost in frame and fills in its album subject entry.  Returns the
 * base points; the bonuses are accumulated into *bonus, which is shared across
 * every ghost in the shot.
 *
 * Hostile ghosts (type < 2) score off the damage done; passive ones (type 2)
 * off their own point_base scaled by how far away and how well centred they
 * were.  Anything else scores nothing. */
int PhotoPointCulcEne2(ENE_WRK *ew, BONUS_SHOT_SCORE *bonus,            /* 3832 */
                       SUBJECT_WRK *subjects, HINT_PHOTO_REQ *hint_picture,
                       int bWithLenz)
{
    STATUS_DAT *ews = &ew->st;                                          /* 3841 */
    int   total = 0;
    int   ghost_list_no;
    float rate;
    float sb_rate;
    float base_point;
    float point;

    hint_picture->no = -1;                                              /* 3846 */

    if (ew->type < 2)                                                   /* 3847 */
    {
        printf("***************************************\n");            /* 3855 */
        printf("Enemy%d --------------------------------\n",            /* 3856 */
               ew->alg.idx);

        rate = 1.0f;                                                    /* 3859 */

        /* Ten points per point of damage, before the combo multipliers. */
        base_point = (float)ews->dmg * 10.0f;                           /* 3862 */

        if ((u_char)ew->combo_counter < 3)                              /* 3206 */
        {
            /* The shot's combo depth is the deepest any one ghost reached. */
            if (bonus->mComboNum < (short)(ew->combo_counter + 1))       /* 3867 */
            {
                bonus->mComboNum = (short)(ew->combo_counter + 1);       /* 3868 */
            }

            rate = combo_point_mag[ew->combo_counter];                  /* 3871 */
        }

        if ((u_char)ew->combo_sb_counter < 3)                           /* 3877 */
        {
            sb_rate = combo_sb_point_mag[ew->combo_sb_counter];         /* 3878 */
        }
        else
        {
            sb_rate = 1.0f;                                             /* 3880 */
        }

        point = base_point * rate * sb_rate;
        total = (int)point;

        printf("Base Point : %d = %d * [combo ratio = %f] "              /* 3885 */
               "* [combo sb ratio = %f]\n",
               total, (int)base_point, (double)rate, (double)sb_rate);

        /* Core shot -- dead centre of the capture ring. */
        if (ew->dist_c_e <= 20.0f)                                      /* 3890 */
        {
            bonus->mScore[2] = (short)(bonus->mScore[2] + (int)(point * 0.2f));
            printf("CoreShot     : %d\n", bonus->mScore[2]);
        }

        /* Close shot -- taken from inside the ghost's own strike range. */
        if (ew->dist_p_e[0] <= ew->dat->hit_rng)                        /* 3895 */
        {
            bonus->mScore[1] = (short)(bonus->mScore[1] + (int)(point * 0.2f));
            printf("CloseShot    : %d\n", bonus->mScore[1]);
        }

        /* Just kill -- the shot finishes it with 10 or less to spare.  The
         * subtraction is signed, so a shot that overkills badly misses out. */
        if ((int)((u_int)ews->dmg - (u_int)ews->hp) < 11 &&             /* 3901 */
            ews->hp < ews->dmg)
        {
            bonus->mScore[4] = (short)(bonus->mScore[4] + (int)(point * 0.1f));
            printf("JustKill    : %d\n", bonus->mScore[4]);
        }

        if ((ews->sta & 0x3000L) != 0)                                  /* 3907 */
        {
            bonus->mScore[0] = (short)(bonus->mScore[0] + (int)(point * 0.5f));
            printf("ShutterChanceShot    : %d\n", bonus->mScore[0]);

            if (bWithLenz != 0)                                         /* 3912 */
            {
                bonus->mScore[3] = (short)(bonus->mScore[3] + (int)(point * 0.3f));
                printf("SpecialShot  : %d\n", bonus->mScore[3]);
            }
        }

        printf("---------------------------------------\n");            /* 3919 */
        printf("Enemy%d       Score : %d\n", ew->alg.idx, total);        /* 3920 */

        ghost_list_no =                                                 /* 3922 */
            GetSoulListNo(jene_dat[ew->dat_no].cmn.ghost_list_no);

        if (ghost_list_no < 0)                                          /* 3928 */
        {
            subjects->type = -1;                                        /* 3933 */
        }
        else
        {
            subjects->type  = 0x3b;                                     /* 3929 */
            subjects->no    = (u_short)GetSoulListNo(
                                  jene_dat[ew->dat_no].cmn.ghost_list_no);
            subjects->sp_no = (u_short)GetSoulListNo(
                                  jene_dat[ew->dat_no].cmn.ghost_list_no_sp);
        }

        /* A killing blow on a ghost that carries a hint picture files it. */
        if (ew->type == 0 && ew->dat->hint_pic != 0xff &&               /* 3938 */
            ews->hp <= ews->dmg)
        {
            hint_picture->no = ew->dat->hint_pic;                       /* 3943 */
        }
    }
    else if (ew->type == 2)                                             /* 3847 */
    {
        int iDist;
        int iCenter;
        float fCenter;

        iDist = (int)(ew->dist_p_e[0] / 500.0f);                        /* 3955 */
        if (iDist > 9)                                                  /* 3956 */
        {
            iDist = 9;
        }

        iCenter = (int)(ew->dist_c_e / 10.0f);                          /* 3960 */
        if (iCenter > 10)                                               /* 3961 */
        {
            iCenter = 10;
        }

        /* PORT NOTE: the ROM clamps to 10 but photo_center_ratio only holds
         * ten entries, so index 10 falls off the end into photo_charge_ratio,
         * which follows it in .data -- reading 1.4 where the table's trend
         * would give something below 0.5.  A badly centred passive subject is
         * therefore scored better than a well centred one.  Reproduced
         * explicitly rather than left as an out-of-bounds read. */
        fCenter = (iCenter < 10) ? photo_center_ratio[iCenter]           /* 3962 */
                                 : photo_charge_ratio[0];

        total = (int)((float)ew->cmn_dat->point_base *                  /* 3963 */
                      photo_dist_ratio[iDist] * fCenter);

        ghost_list_no =
            GetSoulListNo(aene_dat[ew->dat_no].cmn.ghost_list_no);

        if (ghost_list_no < 0)                                          /* 3969 */
        {
            subjects->type = -1;                                        /* 3974 */
        }
        else
        {
            subjects->type  = 0x3b;                                     /* 3970 */
            subjects->no    = (u_short)GetSoulListNo(
                                  aene_dat[ew->dat_no].cmn.ghost_list_no);
            subjects->sp_no = (u_short)GetSoulListNo(
                                  aene_dat[ew->dat_no].cmn.ghost_list_no_sp);
        }

        /* Photographing either of these two holds the finder shut for five
         * seconds -- the scripted "she is gone" beat. */
        if (ew->dat_no == 0 || ew->dat_no == 0x2f)                      /* 3978 */
        {
            finder_off_lock_timer_cnt = 300;                            /* 3981 */
        }
    }

    return total;                                                       /* 3987 */
}

/* Everything that happens after the damage has been decided: the score, the
 * multi-shot bonuses, the album entry and the on-screen readout.
 *
 * `iDmg` is the shot's total damage, only used for the readout.  Every ghost
 * whose 0x80 bit survived PhotoDmgChk2() is a subject, plus -- when no ghost
 * was in frame at all -- whatever scenery photo_dat.c had lined up, plus the
 * companion if she was in the ring and roughly in front. */
static void PlayerTakePictJob(int iDmg, int bWithLenz)                  /* 3525 */
{
    PHOTO_WRK_DEF    def;
    BONUS_SHOT_SCORE bonus;
    SUBJECT_WRK      subjects[15];
    sceCdCLOCK       rtc;
    int   i;
    int   cnt               = 0;            /* 3530 */
    int   iBaseScore        = 0;            /* 3531 */
    int   iTotalScore       = 0;            /* 3532 */
    int   iAutoEneCnt       = 0;            /* 3533 */
    int   iRareEneNo        = -1;           /* 3534 */
    int   no_effect_mes_flg = 0;            /* 3535 */
    int   ane_curse_flg     = 0;            /* 3537 */
    int   bHint3D           = 0;            /* 3539 */
    float base;

    for (i = 0; i < 9; i++)                                             /* 34 */
    {
        bonus.mScore[i] = 0;
    }
    bonus.mSP       = 0;                                                /* 35 */
    bonus.mComboNum = 0;                                                /* 36 */

    def.hint_cnt     = 0;                                               /* 3540 */
    def.msg_name     = -1;                                              /* 3541 */
    def.msg_type     = -1;                                              /* 3542 */
    def.unlock_ghost = 0;

    printf("========= Score Information ===========\n");                 /* 3548 */

    for (i = 0; i < ENE_WRK_MAX; i++)                                   /* 3551 */
    {
        ENE_WRK *ew = &ene_wrk[i];

        if ((ew->st.sta & 0x80L) == 0 || (ew->attr & 0x40) != 0)        /* 124 */
        {
            continue;
        }

        /* An invincible ghost voids the whole picture: the score, every bonus
         * and every other subject go with it, and the shot files as INVALID. */
        if ((ew->attr & 0x4000) != 0)                                   /* 3558 */
        {
            BONUS_SHOT_SCORE no_bonus;

            for (int j = 0; j < 9; j++)                                 /* 34 */
            {
                no_bonus.mScore[j] = 0;
            }
            no_bonus.mSP       = 0;                                     /* 35 */
            no_bonus.mComboNum = 0;                                     /* 36 */

            no_effect_mes_flg = 1;                                      /* 3558 */
            def.hint_cnt      = 0;                                      /* 3562 */
            bonus             = no_bonus;                               /* 3563 */
            iBaseScore        = 0;                                      /* 3564 */
            cnt               = 0;                                      /* 3565 */
            break;
        }

        iBaseScore += PhotoPointCulcEne2(ew, &bonus, &subjects[cnt],    /* 124 */
                                         &def.hint_pict[def.hint_cnt],
                                         bWithLenz);
        cnt++;                                                          /* 3580 */

        if (def.hint_pict[def.hint_cnt].no >= 0)                        /* 3570 */
        {
            def.hint_cnt++;                                             /* 3571 */
        }

        /* A passive ghost that made it into frame names the picture. */
        if (ew->type == 2 && subjects[cnt - 1].type >= 0)               /* 124 */
        {
            def.msg_name = subjects[cnt - 1].no;                        /* 3576 */
            def.msg_type = 0x3b;                                        /* 3577 */
            iAutoEneCnt++;                                              /* 3578 */
        }
    }

    if (no_effect_mes_flg == 0)                                         /* 3587 */
    {
        int iBattleEneCnt = cnt - iAutoEneCnt;                          /* 3588 */

        base = (float)iBaseScore;                                       /* 3591 */

        printf("=======================================\n");             /* 3589 */

        /* PORT NOTE: the triple multipliers really are lower than the double
         * ones in this build -- 0.2 against 0.3 for shots, 0.3 against 0.4
         * for kills -- so the third ghost in frame is worth less than the
         * second.  Both pairs read that way in .rodata; reproduced as found. */
        if (iBattleEneCnt >= 3)                                         /* 3593 */
        {
            bonus.mScore[6] = (short)(int)(base * 0.2f);
            printf("[Triple Shot] : %d\n", bonus.mScore[6]);
        }
        else if (iBattleEneCnt >= 2)                                    /* 3598 */
        {
            bonus.mScore[5] = (short)(int)(base * 0.3f);
            printf("[Double Shot] : %d\n", bonus.mScore[5]);
        }

        if (iBusterCountThisTime >= 3)                                  /* 3604 */
        {
            bonus.mScore[8] = (short)(int)(base * 0.3f);
            printf("[Triple Kill] : %d\n", bonus.mScore[8]);
        }
        else if (iBusterCountThisTime >= 2)                             /* 3609 */
        {
            bonus.mScore[7] = (short)(int)(base * 0.4f);
            printf("[Double Kill] : %d\n", bonus.mScore[7]);
        }

        iTotalScore = iBaseScore;                                       /* 3615 */
        for (i = 0; i < 9; i++)                                         /* 3619 */
        {
            iTotalScore += bonus.mScore[i];
        }

        if (iTotalScore > 99999)                                        /* 3622 */
        {
            iTotalScore = 99999;
        }

        if (plyr_wrk.nowShutterChanceState == SHUTTER_CHANCE_SP)        /* 3627 */
        {
            bonus.mSP = 1;                                              /* 3628 */
        }

        printf("Total        Score : %d\n", iTotalScore);                /* 3630 */
        printf("=======================================\n");             /* 3631 */

        MisSetScore(iTotalScore);                                       /* 3634 */
        PhotoWrkPreInit();                                              /* 3637 */

        /* Nothing living in frame: fall back on whatever piece of scenery
         * photo_dat.c had lined up. */
        if (cnt == 0)                                                   /* 3640 */
        {
            MDAT_OBJ *sp = photo_datObjIsPhotoAble();                   /* 3641 */

            if (sp != NULL)                                             /* 3643 */
            {
                if ((u_int)(sp->Visible - 4) < 2)                       /* 3645 */
                {
                    bHint3D = 1;                                        /* 3646 */
                    FurnPhotoFlgUp();                                   /* 3647 */
                }

                if (sp->PhotoAble != 0)                                 /* 3649 */
                {
                    def.hint_pict[def.hint_cnt].no = sp->PhotoAble;     /* 3652 */
                    def.hint_cnt++;

                    if (photo_dat[sp->PhotoAble].f_unlock_ghost != 0)   /* 3656 */
                    {
                        def.unlock_ghost = 1;                           /* 3657 */
                    }

                    iTotalScore      = photo_dat[sp->PhotoAble].Point;  /* 3660 */
                    subjects[0].type = -1;                              /* 3662 */
                    subjects[0].no   = (u_short)sp->type;               /* 3663 */

                    /* ghost_list_rel_no is compared unsigned here, so the -1
                     * "not a ghost" entries read as 65535 and fall out. */
                    if ((u_short)photo_dat[sp->PhotoAble].ghost_list_rel_no  /* 3664 */
                            < 0xb0)
                    {
                        iRareEneNo =                                    /* 3665 */
                            photo_dat[sp->PhotoAble].ghost_list_rel_no;

                        if (GetSoulListNo(iRareEneNo) >= 0)             /* 3670 */
                        {
                            subjects[0].no = (u_short)GetSoulListNo(     /* 3515 */
                                photo_dat[sp->PhotoAble].ghost_list_rel_no);
                            def.msg_name   = subjects[0].no;
                            subjects[0].type = 0x3b;                    /* 3677 */
                            def.msg_type     = 0x3b;
                        }
                        else
                        {
                            def.msg_type = -1;                          /* 3679 */
                        }

                        def.pos[0] = sp->Pos[0];                        /* 3684 */
                        def.pos[1] = sp->Pos[1];                        /* 3685 */
                        def.pos[2] = sp->Pos[2];                        /* 3686 */
                        def.pos[3] = 1.0f;                              /* 3687 */
                    }

                    cnt++;                                              /* 3690 */
                }
            }
        }

        /* The companion counts as a subject when she is inside the capture
         * ring and within 90 degrees of where the player is facing. */
        if (IsSisWrk() != 0)                                            /* 3695 */
        {
            float tx;
            float ty;
            float dist[2];
            float vw[4];
            float psrot;

            dist[1] = 100000.0f;                                        /* 3698 */

            if (FrameInsideChk(sis_wrk.cmn_wrk.headpos, &tx, &ty) != 0) /* 3699 */
            {
                dist[0] = GetDist((float)plyr_wrk.fp[0] - tx,           /* 3700 */
                                  (float)plyr_wrk.fp[1] - ty);

                if (dist[0] <=
                    m_plyr_camera.camera_power_up.GetRadius())
                {
                    GetTrgtRot(plyr_wrk.cmn_wrk.mbox.pos,               /* 3704 */
                               sis_wrk.cmn_wrk.mbox.pos, vw, 2);
                    psrot = vw[1] - plyr_wrk.cmn_wrk.mbox.rot[1];       /* 3705 */
                    RotLimitChk(&psrot);                                /* 3706 */

                    /* PI/2 as a float, widened -- the ROM compares in
                     * double because fabs() is the double one. */
                    if (fabs((double)psrot) < 1.5707964f &&             /* 3707 */
                        dist[0] < dist[1])
                    {
                        /* The Mayu-curse picture: she is the only subject and
                         * the curse lock is not already held. */
                        if (plyr_wrk.ane_curse_lock == 0)               /* 3710 */
                        {
                            ane_curse_flg = (cnt == 0);
                        }

                        subjects[cnt].type = 5;                         /* 3714 */
                        subjects[cnt].no   = 0;                         /* 3715 */
                        cnt++;                                          /* 3716 */
                        dist[1] = dist[0];                              /* 3719 */
                    }
                }
            }
        }

        if (cnt >= 16)                                                  /* 3728 */
        {
            PRINT_ASSERT("PICTURE_SUBJECT IS TOO LARGE");                /* 3729 */
        }
    }

    sceCdReadClock(&rtc);                                               /* 3744 */
    def.adr_no = GetSavePhotoNo();                                      /* 3745 */

    if (AddPhotoData(def.adr_no, iTotalScore, GetPlyrRoomID(),          /* 3746 */
                     ingame_wrk.mChapterNo, rtc, subjects, cnt) != 0)
    {
        PlayData_MaxScoreUpdate(iTotalScore);                           /* 3750 */
    }

    PlayData_ScoreCount(iTotalScore);                                   /* 3754 */
    PlayData_PhotoNumCount();                                           /* 3755 */

    /* Every ghost in frame unlocks its ghost-list entry; a special shutter
     * chance unlocks the second one too. */
    for (i = 0; i < cnt; i++)                                           /* 3758 */
    {
        if (subjects[i].type == 0x3b)                                   /* 3759 */
        {
            GetSoulList(subjects[i].no / 3, iTotalScore);                /* 3761 */

            if (plyr_wrk.nowShutterChanceState == SHUTTER_CHANCE_SP)    /* 3762 */
            {
                GetSoulList(subjects[i].sp_no / 3, iTotalScore);         /* 3765 */
            }
        }
    }

    if (iBaseScore != 0)                                                /* 3777 */
    {
        m_plyr_camera.PhotoInfoDispNew(iDmg, iBaseScore, bonus);        /* 3778 */
    }

    m_plyr_camera.ReqNoiseUp();                           /* 3782 */

    if (no_effect_mes_flg != 0)                                         /* 3786 */
    {
        def.type = PHOTO_TYPE_INVALID;                                  /* 3787 */
    }
    else if (ane_curse_flg != 0)                                        /* 3789 */
    {
        def.type = PHOTO_TYPE_MAYU_CURSE;                               /* 3790 */
    }
    else if (iRareEneNo >= 0)                                           /* 3794 */
    {
        def.type = PHOTO_TYPE_RARE;                                     /* 3795 */
        GetSoulList(iRareEneNo, iTotalScore);                            /* 3796 */
    }
    else if (bHint3D != 0)                                              /* 3799 */
    {
        def.type = PHOTO_TYPE_HINT3D;                                   /* 3800 */
    }
    else
    {
        def.type = PHOTO_TYPE_HINT;                                     /* 3804 */
    }

    PhotoWrkInit(&def);                                                 /* 3807 */
    SetIngamePhoto(1);                                                  /* 3810 */
}

/* Works out what one shot does to one ghost, and writes it to ENE_WRK::st.dmg
 * for enemy.c to apply.  Returns the damage; *pbParticleFlg comes back
 * non-zero when the spirit particles should fly home to the tray rather than
 * disperse.
 *
 * The whole body is bracketed by the ROM's own damage trace -- five printfs
 * that are the fastest way to read this function's intent. */
int PhotoDmgChkSub2(ENE_WRK *ew, int bWithLenz, int *pbParticleFlg)     /* 3331 */
{
    STATUS_DAT *ews = &ew->st;                                          /* 3332 */
    float sub_func_dmg_rate;
    int   bClear;
    SHUTTER_CHANCE_STATE SState;

    printf("========= Damage Information ==========\n");                 /* 3336 */

    if ((ew->attr & 0x4000) != 0)                                       /* 3339 */
    {
        ews->dmg      = 0;                                              /* 3340 */
        ews->dmg_type = 0;                                              /* 3341 */
        ews->sta     |= 0x100000L;                                      /* 3342 */

        printf("MUTEKI!!\n");                     /* "invincible" */     /* 3343 */
        printf("=======================================\n");             /* 3344 */
    }
    else
    {
        if (bWithLenz != 0)                                             /* 3352 */
        {
            *pbParticleFlg = 0;                                         /* 3353 */

            sub_func_dmg_rate =                                         /* 3354 */
                m_plyr_camera.eq_tray.GetDmgRate(
                    plyr_wrk.nowShutterChanceState);

            printf("### SUB FUNC DAMAGE RATE = %f\n",                    /* 3355 */
                   (double)sub_func_dmg_rate);
        }
        else
        {
            *pbParticleFlg    = 1;                                      /* 3357 */
            sub_func_dmg_rate = 1.0f;                                   /* 3358 */
        }

        /* A shot breaks every scripted hold on the ghost.  The five results
         * are OR-ed because any one of them means the ghost was doing
         * something the particles should not interrupt. */
        bClear  = SetEneMahiClear(ew);                                  /* 3362 */
        bClear |= SetEneSlowClear(ew);                                  /* 3363 */
        bClear |= SetEneSealClear(ew);                                  /* 3364 */
        bClear |= plyr_wrk.ene_tracer.Init();               /* 3365 */
        bClear |= SetEneViewClear(ew);                                  /* 3366 */
        IgEffectSubFuncPDeformClear(ew->alg.idx);                       /* 3367 */

        if (sub_func_dmg_rate != 0.0f)                                  /* 3371 */
        {
            u_long sta = ews->sta;

            ews->sta &= ~0x100000L;                                     /* 3375 */

            if ((sta & 0x2000L) != 0)                                   /* 3378 */
            {
                /* Fatal frame: always a combo hit. */
                ews->dmg_type = (u_short)EneComboReq(ew, bWithLenz);     /* 3381 */
                ews->sta     |= 0x100000L;                              /* 3382 */
                SState        = SHUTTER_CHANCE_SP;
                printf("Hit back Type : Combo Big\n");                   /* 3383 */
            }
            else if ((sta & 0x1000L) != 0)                              /* 3386 */
            {
                SState = SHUTTER_CHANCE_NORMAL;

                if ((u_char)ew->combo_counter < 3)                      /* 3206 */
                {
                    ews->dmg_type = (u_short)EneComboReq(ew, bWithLenz); /* 3390 */
                }
                else
                {
                    ews->dmg_type = 2;                                  /* 3392 */
                }

                ews->sta |= 0x100000L;                                  /* 3394 */
                printf("Hit back Type : Big\n");                         /* 3395 */
            }
            else
            {
                SState = SHUTTER_CHANCE_NONE;

                if (bWithLenz != 0 &&                                   /* 3401 */
                    m_plyr_camera.eq_tray.IsHitBackON() != 0)
                {
                    ews->dmg_type = 2;                                  /* 3402 */
                }
                else
                {
                    ews->dmg_type = 0;                                  /* 3405 */
                }

                ews->sta |= 0x100000L;                                  /* 3407 */
                printf("Hit back Type : No Hit back\n");                 /* 3408 */
            }

            /* Mid-combo hits score their damage as if there were no shutter
             * chance at all -- the chance was already paid for on the first
             * shot of the combo. */
            if ((u_char)ew->combo_counter < 3 && ew->combo_counter > 0)  /* 3206 */
            {
                ews->dmg = (u_short)(int)(                              /* 3422 */
                    m_plyr_camera.camera_power_up.GetDmgRate() *
                    ew->spirit_gage.CalcDamageRate(SHUTTER_CHANCE_NONE) *
                    aDmgRateByDifficulty[ingame_wrk.mDifficulty] *
                    (float)CCameraFilm::aFilmDamageTbl[
                        m_plyr_camera.camera_film.mFilmType]);
            }
            else
            {
                ews->dmg = (u_short)(int)(                              /* 3432 */
                    m_plyr_camera.camera_power_up.GetDmgRate() *
                    ew->spirit_gage.CalcDamageRate(SState) *
                    aDmgRateByDifficulty[ingame_wrk.mDifficulty] *
                    (float)CCameraFilm::aFilmDamageTbl[
                        m_plyr_camera.camera_film.mFilmType]);
            }

            /* Debug one-shot kill: L2 held with R2 tapped. */
            if (*key_now[8] != 0 && *key_now[10] == 1 && ews->hp != 0)  /* 3437 */
            {
                ews->dmg = ews->hp;                                     /* 3438 */
            }

            /* A shot that broke a hold does not also suck. */
            if (bClear != 0)                                            /* 3445 */
            {
                *pbParticleFlg = 0;
            }

            if (ews->dmg != 0 && bWithLenz != 0)                        /* 3450 */
            {
                gbLenzShot = 1;                                         /* 3451 */
                m_plyr_camera.eq_tray.SetEffect(ew, SState);            /* 3452 */
            }

            ews->dmg = (u_short)(int)((float)ews->dmg * sub_func_dmg_rate);  /* 3456 */
        }
        else
        {
            /* Sub-function only: no damage, but the tray still fires its
             * effect at whatever shutter chance was up. */
            if ((ews->sta & 0x2000L) != 0)                              /* 3460 */
            {
                SState = SHUTTER_CHANCE_SP;
            }
            else                                                        /* 3464 */
            {
                SState = (SHUTTER_CHANCE_STATE)((ews->sta & 0x1000L) != 0);
            }

            ews->dmg   = 0;                                             /* 3473 */
            gbLenzShot = 1;                                             /* 3474 */
            m_plyr_camera.eq_tray.SetEffect(ew, SState);                /* 3475 */
        }

        if (ews->dmg >= ews->hp)                                        /* 3479 */
        {
            plyr_wrk.cmn_wrk.st.sta |= 0x80L;                           /* 3480 */
            PlayData_BusterNumCount();                                  /* 3482 */
            iBusterCountThisTime++;                                     /* 3483 */

            if (ew->dat_no == 0x28)                                     /* 3485 */
            {
                finder_off_lock_timer_cnt = 300;                        /* 3486 */
            }
        }

        /* attr 0x800 -- a ghost that takes no damage from the camera at all,
         * only the reaction. */
        if ((ew->attr & 0x800) != 0)                                    /* 3491 */
        {
            ews->dmg = 0;
        }
    }

    printf("### Damage type = %d ###\n", ews->dmg_type);                 /* 3496 */
    printf("### Enemy Damage = %d ###\n", ews->dmg);                     /* 3497 */
    printf("### HP before damage culc = %d ###\n", ews->hp);             /* 3498 */

    return ews->dmg;                                                    /* 3504 */
}

/* Runs the damage pass over every ghost the shot caught and returns the total.
 *
 * The 0x80 bit is EneFrameHitChk()'s "this ghost is a legal target"; 0x80000
 * excludes it and bit 0 is the dead flag.  A passive ghost (type 2) is not
 * damaged at all -- it is marked photographed and hidden, which is what makes
 * it vanish from the room. */
int PhotoDmgChk2(int bWithLenz)                                         /* 3215 */
{
    ENE_DMG_PARTICLE_REQ EneDmgParticleReq;
    ENE_WRK *ew;
    int      i;
    u_short  dmg = 0;
    int      bParticleFlg;
    int      dmg_one;

    m_plyr_camera.eq_tray.SetEffectsPre();                              /* 3222 */

    plyr_wrk.cmn_wrk.st.sta &= ~0x80L;                                  /* 3223 */

    for (i = 0, ew = &ene_wrk[0]; i < ENE_WRK_MAX; i++, ew++)           /* 3224 */
    {
        if ((ew->attr & 0x40) != 0)                                     /* 3225 */
        {
            continue;
        }

        if ((ew->st.sta & 0x80080L) != 0x80L ||                         /* 3230 */
            (ew->st.sta & 1L) != 0)
        {
            continue;
        }

        /* Bit 40: "has been photographed this run". */
        ew->st.sta |= 0x10000000000L;                                   /* 3231 */

        if (ew->type == 2)                                              /* 3233 */
        {
            ew->st.sta |= 0x1000000L;                                   /* 3234 */
            continue;
        }

        /* 0x10 is SpiritGageCalc()'s "the gauge was live on this ghost". */
        if ((ew->st.sta & 0x10L) == 0)                                  /* 3237 */
        {
            continue;
        }

        dmg_one = PhotoDmgChkSub2(ew, bWithLenz, &bParticleFlg);        /* 3239 */

        if (dmg_one != 0)                                               /* 3241 */
        {
            int iPercent = ew->spirit_gage.GetPercent();                /* 52 */
            int iSuctionPower;

            if (iPercent >= 91)      { iSuctionPower = 1050; }          /* 3248 */
            else if (iPercent >= 71) { iSuctionPower = 700;  }          /* 3250 */
            else if (iPercent >= 41) { iSuctionPower = 450;  }          /* 3252 */
            else if (iPercent >= 21) { iSuctionPower = 300;  }          /* 3254 */
            else if (iPercent >= 1)  { iSuctionPower = 200;  }          /* 3256 */
            else                     { iSuctionPower = 0;    }

            /* Each particle carries 45 units of spirit power. */
            iSuctionPower /= 45;                                        /* 3263 */

            g3dxVu0CopyVector(EneDmgParticleReq.StartPos, ew->mpos.p0); /* 135 */
            EneDmgParticleReq.pEndPos =                             /* 3267 */
                IgEffectParticleEndPosFinderGet();
            EneDmgParticleReq.DistPE      = ew->dist_p_e[0];            /* 3269 */
            EneDmgParticleReq.ParticleNum = iSuctionPower;

            if (bParticleFlg == 0)                                      /* 3271 */
            {
                EneDmgParticleReq.SuctionFlg = 0;
                EneDmgParticleEffectReq(&EneDmgParticleReq);            /* 3273 */
            }
            else
            {
                int HitEffectLabel;

                EneDmgParticleReq.SuctionFlg = 1;                       /* 3276 */

                /* If the burst could not be started the power would never
                 * arrive, so bank it on the spot instead. */
                if (EneDmgParticleEffectReq(&EneDmgParticleReq) != 0)    /* 3279 */
                {
                    m_plyr_camera.eq_tray.SetRemainParticle(iSuctionPower);  /* 3281 */
                }
                else
                {
                    m_plyr_camera.eq_tray.AbsorbImmediately(iSuctionPower * 45);  /* 3284 */
                }

                if (ew->st.dmg_type < 4)                                /* 3288 */
                {
                    HitEffectLabel = (ew->st.dmg_type == 2);            /* 3290 */
                }
                else
                {
                    HitEffectLabel = 2;
                }

                EneHitEffectReq(ew->alg.idx, ew->mpos.p0, HitEffectLabel);  /* 3296 */
            }

            /* Every shot of a combo banks a flat 340 on top. */
            if ((u_char)ew->combo_counter < 3)                          /* 3206 */
            {
                m_plyr_camera.eq_tray.AbsorbImmediately(340);           /* 3301 */
            }
        }

        /* The capture ring is hidden for 20 frames whether or not the shot
         * did damage -- it is the shutter beat, not a hit reaction. */
        m_plyr_camera.InCircleDrawLock(20);             /* 3306 */

        dmg = (u_short)(dmg + dmg_one);                                 /* 3307 */
    }

    return dmg;                                                         /* 3320 */
}


/* ==========================================================================
 *  The in-finder frame (player.o 0x002393d0..0x00239a20, 0x00239c60..).
 * ======================================================================== */

/* Fills every acting ghost's spirit gauge, and latches this frame's shutter
 * chance onto the player work block.
 *
 * A ghost qualifies when the shutter is charged, it is inside the fitted
 * lens's reach, and EneFrameHitChk() marked it a legal target (0x80).  The
 * gauge's own 0x10 flag records that, so PhotoDmgChk2() can tell which ghosts
 * were actually being drained when the shutter went. */
static void SpiritGageCalc(void)                                        /* 2562 */
{
    int   ret = 0;
    int   iMinPercent = m_plyr_camera.camera_film.GetFilmMinPercent();
    SHUTTER_CHANCE_STATE SState = ShutterChanceChk();                   /* 2565 */
    int   i;

    for (i = 0; i < ENE_WRK_MAX; i++)                                   /* 2567 */
    {
        ENE_WRK *ew = &ene_wrk[i];

        if (IsActEnemy(i) == 0)                                         /* 2569 */
        {
            continue;
        }

        float fMaxDistance = m_plyr_camera.camera_power_up.GetDistance();

        if (CPhotoCharger::IsReady(&m_plyr_camera.charger) != 0 &&      /* 61 */
            ew->dist_p_e[0] < fMaxDistance &&
            (ew->st.sta & 0x80L) != 0)
        {
            float fDistanceRate = 1.0f;                                 /* 2582 */

            /* Full inside 800 units, tapering to nothing at the lens's reach. */
            if (ew->dist_p_e[0] > 800.0f)
            {
                fDistanceRate = 1.0f - (ew->dist_p_e[0] - 800.0f)       /* 2586 */
                                     / (fMaxDistance - 800.0f);
            }

            ret = 1;

            ew->spirit_gage.Work(
                fDistanceRate,
                1.0f - ew->dist_c_e /
                       m_plyr_camera.camera_power_up.GetRadius(),
                (float)ew->tr_rate / 128.0f,
                SState, iMinPercent);

            ew->st.sta |= 0x10L;                                        /* 2597 */
        }
        else                                                            /* 2598 */
        {
            ew->spirit_gage.Work(0.0f, 0.0f, 0.0f, SHUTTER_CHANCE_NONE, 0);  /* 2605 */
            ew->st.sta &= ~0x10L;                                       /* 2606 */
        }
    }

    plyr_wrk.preShutterChanceState = plyr_wrk.nowShutterChanceState;    /* 2613 */

    if (ret)                                                            /* 2614 */
    {
        plyr_wrk.nowShutterChanceState = SState;                        /* 2615 */
        m_plyr_camera.eq_tray.RenzMarkOn();             /* 2616 */
    }
    else
    {
        plyr_wrk.nowShutterChanceState = SHUTTER_CHANCE_NONE;           /* 2618 */
        m_plyr_camera.eq_tray.RenzMarkOff();            /* 2619 */
    }

    ShutterChanceChangeJob();                                           /* 2623 */
}

/* Decides which ghosts are legal camera targets this frame and where they sit
 * in the viewfinder.  Three flags come out of it, all on ENE_WRK::st.sta:
 *
 *   0x200  inside the viewfinder frame at all
 *   0x400  inside the capture ring
 *   0x40   near enough, or already visible, to be worth aiming at
 *   0x80   all of the above -- what SpiritGageCalc() and PhotoDmgChk2() read
 *
 * in_finder_tm counts the frames a ghost has stayed in frame, which is what
 * the shutter-chance windows are timed against; it resets the moment the
 * ghost leaves. */
void EneFrameHitChk(void)                                               /* 2725 */
{
    u_char i;

    for (i = 0; i < ENE_WRK_MAX; i++)                                   /* 2749 */
    {
        ENE_WRK *ew = &ene_wrk[i];
        float    dpe;
        float    dce;
        float    tx;
        float    ty;

        if (ew->status != ENE_STATUS_ACT || ew->st.hp == 0 ||           /* 2750 */
            (ew->st.sta & 0x1000000L) != 0)
        {
            continue;
        }

        dpe = ew->dist_p_e[0];

        /* PI*2/3 is the sight cone's full width and OutSightChk() halves it,
         * so the angular gate is +/-60 degrees off the player's facing. */
        if (OutSightChk(ew->mpos.p0, gra3dGetCamera()->matCoord[3],     /* 2759 */
                        plyr_wrk.cmn_wrk.mbox.rot[1],
                        2.0943949f, photo_rng_tbl[0]) != 0 ||
            FrameInsideChk(ew->mpos.p0, &tx, &ty) == 0)
        {
            ew->in_finder_tm = 0;                                       /* 2824 */
            continue;
        }

        ew->st.sta |= 0x200L;                                           /* 2767 */

        /* -1 is the "hold at maximum" sentinel; everything else counts up. */
        if (ew->in_finder_tm != 0xffff)                                 /* 2770 */
        {
            ew->in_finder_tm++;
        }

        ew->fp[0] = (short)(int)tx;                                     /* 2772 */
        ew->fp[1] = (short)(int)ty;                                     /* 2773 */

        dce = GetDist(tx - (float)plyr_wrk.fp[0],                       /* 2776 */
                      ty - (float)plyr_wrk.fp[1]);

        if (dce <= m_plyr_camera.camera_power_up.GetRadius())  /* 37 */
        {
            ew->dist_c_e = dce;                                         /* 2784 */
            ew->st.sta  |= 0x400L;                                      /* 2783 */
        }

        /* 0x80000 is "excluded from the camera"; bit 0 the dead flag. */
        if ((ew->st.sta & 0x80000L) == 0 && (ew->st.sta & 1L) == 0)     /* 2794 */
        {
            /* Either the neck is not behind geometry, or the ghost is close
             * enough that it does not matter. */
            if (ew->ppj.result[0] != 0 || dpe <= 300.0f)                /* 124 */
            {
                ew->st.sta |= 0x40L;                                    /* 2797 */
            }
        }

        if (((ew->st.sta & 0x400L) != 0 ||                              /* 2807 */
             (dpe <= 510.0f && (ew->st.sta & 0x200L) != 0)) &&
            (ew->st.sta & 0x40L) != 0 &&                                /* 2810 */
            ew->tr_rate != 0)
        {
            ew->st.sta |= 0x80L;                                        /* 2812 */
        }
    }
}

/* One shutter press.  Returns non-zero when a picture was actually taken.
 * bWithLenz is the burst-shot button rather than the plain shutter. */
static int PlyrPhotoChk2Sub(int bWithLenz)                              /* 2922 */
{
    int ret;
    int dmg;
    SHUTTER_CHANCE_STATE SPState;

    /* mFilmType doubles as the film's item id. */
    if (GetPlyrItemHaveNum(m_plyr_camera.camera_film.mFilmType) < 1)    /* 2924 */
    {
        SystemBankPlay(4, 1, 1, 0, NULL, 0x3200, 0x1000);               /* 2982 */
        ret = 0;                                                        /* 2983 */
        return ret;
    }

    SPState              = plyr_wrk.nowShutterChanceState;              /* 2926 */
    gbLenzShot           = 0;                                           /* 2927 */
    iBusterCountThisTime = 0;                                           /* 2928 */

    PhotoFlyChk();                                                      /* 2929 */
    dmg = PhotoDmgChk2(bWithLenz);                                      /* 2930 */
    PlayerTakePictJob(dmg, bWithLenz);                                  /* 2932 */

    printf("dmg = %d\n", dmg);                                          /* 2934 */

    /* Film type 0 is the unlimited starting film. */
    if (m_plyr_camera.camera_film.mFilmType != 0)                       /* 167 */
    {
        ItemLost(m_plyr_camera.camera_film.mFilmType, 1);               /* 2938 */
    }

    /* Which shutter sound: 4 a sub-function firing or a shutter chance taken,
     * 3 an ordinary battle shot, 2 a shot with nothing in front of it.
     * GCC cross-jumped the identical FinderBankPlay() tails into one call. */
    if (bWithLenz != 0 && gbLenzShot != 0)                              /* 2942 */
    {
        m_plyr_camera.eq_tray.Use2();                                   /* 2943 */

        ret = 4;                                                        /* 2946 */
        FinderBankPlay(ret, 1, 1, 0, NULL, 0x3200, 0x1000);             /* 2947 */
    }
    else if ((plyr_wrk.cmn_wrk.st.sta & 0x20L) != 0)                    /* 2950 */
    {
        if (SPState == SHUTTER_CHANCE_NONE)                             /* 2955 */
        {
            ret = 3;                                                    /* 2957 */
            FinderBankPlay(ret, 1, 1, 0, NULL, 0x3200, 0x1000);         /* 2958 */
        }
        else
        {
            FinderBankPlay(4, 1, 1, 0, NULL, 0x3200, 0x1000);           /* 2960 */
        }
    }
    else
    {
        ret = 2;
        FinderBankPlay(ret, 1, 1, 0, NULL, 0x3200, 0x1000);
    }

    plyr_wrk.no_photo_tm = 10;                                          /* 2965 */

    /* A shot taken on a shutter chance keeps its charge; anything else
     * re-arms the shutter and pulls the lens back out. */
    if (SPState != SHUTTER_CHANCE_NORMAL && SPState != SHUTTER_CHANCE_SP)  /* 2968 */
    {
        if (m_plyr_camera.eq_tray.IsChargeResetOK() == 0 && bWithLenz != 0)
        {
            ret = 1;
            return ret;
        }

        m_plyr_camera.ResetCharge();                      /* 2975 */
        CNPlyrCamera::ReqZoomOut(&m_plyr_camera);                       /* 2976 */
    }

    ret = 1;                                                            /* 2979 */
    return ret;                                                         /* 2985 */
}

/* Reads the shutter buttons.  Returns non-zero when a picture was taken --
 * PlyrFinderCtrl() uses that to suppress the finder-down request on the same
 * frame, so a shot never doubles as an exit. */
int PlyrPhotoChk2(void)                                                 /* 2991 */
{
    int ret = 0;

    /* Conditions 1 and 3 are the two that take the camera away. */
    if (plyr_wrk.cmn_wrk.st.cond == 1 || plyr_wrk.cmn_wrk.st.cond == 3) /* 2995 */
    {
        return 0;
    }

    if (plyr_wrk.no_photo_tm != 0)                                      /* 2997 */
    {
        plyr_wrk.no_photo_tm--;
        return 0;
    }

    if (plyr_wrk.shutter_lock_cnt > 0)                                  /* 2998 */
    {
        return 0;
    }

    if (CPhotoCharger::IsReady(&m_plyr_camera.charger) == 0)            /* 61 */
    {
        /* Still charging: the press only plays the refusal cue, and holds the
         * shutter icon lit for a second so the player sees why. */
        if ((*paddat[13] == 1 && *pushdat[13] > 7) ||                   /* 3040 */
            (*paddat[14] == 1 && *pushdat[14] > 7))
        {
            SystemBankPlay(4, 1, 1, 0, NULL, 0x3200, 0x1000);           /* 3042 */
            plyr_wrk.shutter_tm = 60;                                   /* 3043 */
        }

        return ret;
    }

    if ((*paddat[13] == 1 && *pushdat[13] > 7) ||                       /* 3023 */
        (*paddat[14] == 1 && *pushdat[14] > 7))
    {
        ret = PlyrPhotoChk2Sub(0);                                      /* 3025 */
        plyr_wrk.shutter_tm = 60;                                       /* 3026 */
    }
    else if (*paddat[16] == 1 && *pushdat[16] > 7)                      /* 3028 */
    {
        /* The burst shot needs a sub-function actually fitted. */
        if (m_plyr_camera.eq_tray.IsSetUp() != 0)       /* 3029 */
        {
            ret = PlyrPhotoChk2Sub(1);                                  /* 3030 */
        }
        else
        {
            SystemBankPlay(4, 1, 1, 0, NULL, 0x3200, 0x1000);           /* 3034 */
        }
    }

    return ret;                                                         /* 3048 */
}

/* Mode 6.  The whole in-finder frame, in the ROM's order: the aim helpers
 * first, then the shutter, then movement, then the finder-down request.
 *
 * finder_off_lock_timer_cnt is the scripted hold that keeps the camera up
 * after certain shots; while it runs the down button does nothing. */
void PlyrFinderCtrl(void)                                               /* 2642 */
{
    int photo_ok;

    FModeScreenEffect();                                                /* 2646 */
    plyr_wrk.ene_tracer.Work();                             /* 2648 */
    EneFrameHitChk();                                                   /* 2650 */
    SpiritGageCalc();                                                   /* 2652 */
    m_plyr_camera.eq_tray.SetUp();                      /* 2655 */

    photo_ok = PlyrPhotoChk2();                                         /* 2657 */

    PlyrCamTurnChk();                                                   /* 2658 */
    PlyrFModeMoveCtrl();                                                /* 2659 */
    PlyrDWalkTmCtrl(&plyr_wrk.cmn_wrk);                                 /* 2660 */
    NearAllEneInfo(&plyr_wrk.cmn_wrk);                                  /* 2661 */

    if (finder_off_lock_timer_cnt != 0)                                 /* 2664 */
    {
        finder_off_lock_timer_cnt--;                                    /* 2666 */
        return;
    }

    /* Lowering the camera takes a firm press, and is refused outright while
     * the player is being held (cond 1 / 3) or has just fired. */
    if (*paddat[15] == 1 && *pushdat[15] > 7 &&                         /* 2670 */
        plyr_wrk.cmn_wrk.st.cond != 1 &&
        plyr_wrk.cmn_wrk.st.cond != 3 &&
        plyr_wrk.finder_lock_cnt == 0 &&
        photo_ok == 0)
    {
        plyr_wrk.no_photo_tm = 30;                                      /* 2677 */
        plyr_wrk.finder_tm   = 2;                                       /* 2679 */
    }

    if (*paddat[17] == 1)                                               /* 2705 */
    {
        m_plyr_camera.eq_tray.Rotate();                 /* 2706 */
    }

    /* Two frames of teardown: the wipe on the first, the mode change on the
     * second.  finder_tm counts down through both. */
    if (plyr_wrk.finder_tm != 0)                                        /* 2710 */
    {
        if (plyr_wrk.finder_tm == 2)
        {
            SetPlyrFinderEnd1();                                        /* 2712 */
        }
        else if (plyr_wrk.finder_tm == 1)                               /* 2714 */
        {
            SetPlyrFinderEnd2();                                        /* 2715 */
        }

        plyr_wrk.finder_tm--;                                           /* 2717 */
    }
}

/* Is there anything worth photographing right now?  Referenced only from a
 * data table in the ROM (0x395cc4) -- no `jal` site reaches it -- but it is a
 * player.o export, so it is reconstructed rather than left out.
 *
 * fhp[i][3] is the per-subject "in shot" marker the HUD reads back. */
int ChkPhotoAble(void)                                                  /* 4893 */
{
    int    ret = 0;                                                     /* 4894 */
    u_char i;

    for (i = 0; i < 5; i++)                                             /* 4897 */
    {
        plyr_wrk.fhp[i][3] = 0.0f;
    }

    if (plyr_wrk.cmn_wrk.mode == 6)                                     /* 4899 */
    {
        if (plyr_wrk.cmn_wrk.st.cond != 1)                              /* 4901 */
        {
            for (i = 0; i < ENE_WRK_MAX; i++)                           /* 4903 */
            {
                if ((ene_wrk[i].st.sta & 0x80L) != 0)                   /* 4908 */
                {
                    ret = 1;                                            /* 4910 */
                    return ret;
                }
            }

            /* Mid-battle, only ghosts count. */
            if ((plyr_wrk.cmn_wrk.st.sta & 0x20L) != 0)
            {
                return ret;
            }

            ret = (photo_datObjIsRespondFilament() != 0);               /* 4919 */

            if (ret != 0)                                               /* 4925 */
            {
                plyr_wrk.fhp[0][3] = 1.0f;
            }
        }
    }

    return ret;                                                         /* 4930 */
}


/* ==========================================================================
 *  Damage reactions (player.o 0x002367b0).
 *
 *  Modes 1..4 are one state machine: 1 is the frame the hit lands, 2 the
 *  reaction proper, 3 being held, 4 recovering.  PlyrDamageCtrl() runs one
 *  step of it per frame and returns non-zero on the frame the player is back
 *  in the player's hands.
 *
 *  The four animation tables pick a clip by attack position (0 front, 1 back)
 *  and, for the grab-entry table, by attack direction as well.
 * ======================================================================== */

int PlyrDamageCtrl(void)                                                /* 867 */
{
    /* sdata 3f3988 / 3f3990 / 3f3998 / 3f39a0 */
    u_char pani_dmgin_tbl[3][2] = { { 0x32, 0x35 },                     /* 881 */
                                    { 0x3c, 0x3e },
                                    { 0x3d, 0x3f } };
    u_char pani_dmg_tbl[2] = { 0x33, 0x36 };                            /* 886 */
    u_char pani_out_tbl[2] = { 0x34, 0x37 };                            /* 887 */
    u_char pani_avo_tbl[2] = { 0x3a, 0x3b };                            /* 888 */

    ENE_WRK *ew  = &ene_wrk[plyr_wrk.cmn_wrk.atk_eneno];                /* 124 */
    int      ret = 0;                                                   /* 879 */
    float    tv[4];

    if (plyr_wrk.cmn_wrk.st.hp == 0)                                    /* 890 */
    {
        plyr_wrk.cmn_wrk.st.dwalk_tm = 0;                               /* 892 */

        if (plyr_wrk.cmn_wrk.st.dmg_type != 4)
        {
            ew->atk_tm = 0;                                             /* 893 */
        }

        if (plyr_wrk.cmn_wrk.mode == 9)                                 /* 895 */
        {
            EndPlyrApproachCameraCtrl();                                /* 910 */
            return 1;                                                   /* 911 */
        }

        if (plyr_wrk.cmn_wrk.mode != 4)
        {
            /* Dying out of a grab still plays the release clip first. */
            if (plyr_wrk.cmn_wrk.st.dmg_type == 3)                      /* 896 */
            {
                SetPlyrAnime(pani_out_tbl[plyr_wrk.cmn_wrk.atk_pos], 1); /* 903 */
                PlayerChangeMode(4);                                    /* 904 */
                return 0;                                               /* 907 */
            }

            if (plyr_wrk.cmn_wrk.st.dmg_type == 0)
            {
                return 0;
            }
            if (plyr_wrk.cmn_wrk.st.dmg_type > 4)
            {
                return 0;
            }

            PlayerChangeMode(4);                                        /* 900 */
            return 0;                                                   /* 901 */
        }
    }

    /* The ghost let go, or died, before the reaction finished. */
    if (plyr_wrk.cmn_wrk.st.dmg_type != 4 &&                            /* 917 */
        ew->status != ENE_STATUS_ACT)                                   /* 918 */
    {
        PlayerChangeMode(0);                                            /* 919 */
        EndPlyrApproachCameraCtrl();                                    /* 920 */
        plyr_wrk.cmn_wrk.st.sta &= ~0x10000L;                           /* 921 */
        return 1;                                                       /* 922 */
    }

    switch (plyr_wrk.cmn_wrk.mode)                                      /* 926 */
    {
    case 1:
        /* The frame the hit lands: roll this hit's dodge window and pull the
         * camera round onto the pair. */
        plyr_wrk.cmn_wrk.st.mvsta &= ~0x1800L;                          /* 929 */
        plyr_wrk.avoid_tm  = 0;                                         /* 930 */
        plyr_wrk.avoid_flg = 0;                                         /* 931 */

        plyr_wrk.avoid_st = (u_short)(GetRandValI(6) + 15);             /* 932 */
        plyr_wrk.avoid_sp = (u_short)(GetRandValI(10) + 5);             /* 933 */

        /* 60/50: the window is authored in NTSC frames. */
        if (GetPALMode() != 0)                                          /* 934 */
        {
            plyr_wrk.avoid_st = (u_short)(int)((float)plyr_wrk.avoid_st / 1.2f);  /* 935 */
            plyr_wrk.avoid_sp = (u_short)(int)((float)plyr_wrk.avoid_sp / 1.2f);  /* 936 */
        }

        printf("Avoid accept time [ %2d ~ %2d (%dfr) ]\n",               /* 938 */
               plyr_wrk.avoid_st,
               plyr_wrk.avoid_st + plyr_wrk.avoid_sp,
               plyr_wrk.avoid_sp);

        if (plyr_wrk.cmn_wrk.st.dmg_type != 4)                          /* 941 */
        {
            GetCenterPoint(tv, plyr_wrk.cmn_wrk.mbox.pos, ew->mbox.pos); /* 943 */

            if (ew->cmn_dat->mdl_no == 0x27)                            /* 944 */
            {
                ReqPlyrDamageCameraCtrl(tv, 450.0f, 1100.0f, ew);       /* 954 */
            }
            else if (plyr_wrk.cmn_wrk.atk_pos == 1)                     /* 945 */
            {
                ReqPlyrDamageCameraCtrl(tv, 320.0f, 800.0f, ew);        /* 947 */
            }
            else
            {
                ReqPlyrDamageCameraCtrl(tv, 450.0f, 700.0f, ew);        /* 950 */
            }
        }

        PlayerChangeMode(2);                                            /* 958 */
        /* fall through -- the ROM enters mode 2's body on the same frame */

    case 2:
        /* A fatal-frame grab (0x2000) is instant death. */
        if (plyr_wrk.cmn_wrk.st.dmg_type != 4 &&                        /* 962 */
            (ew->attr & 0x2000) != 0)                                   /* 963 */
        {
            SetOpenCondSwitch(0);                                       /* 964 */
            ReqPlyrDead(3);                                             /* 965 */
            ew->atk_tm = 0;                                             /* 966 */

            if (plyr_wrk.cmn_wrk.st.dmg_type == 3)                      /* 967 */
            {
                ReqAnm(ew->ani_ctrl_p, 10, ew->cmn_dat->anm_no, 9);     /* 968 */
            }

            return 1;                                                   /* 969 */
        }

        switch (plyr_wrk.cmn_wrk.st.dmg_type)                           /* 973 */
        {
        case 3:
            /* A grab: the dodge window has to have elapsed before the player
             * can be counted as having missed it. */
            if (PlyrAvoidCheck() == 0)                                  /* 986 */
            {
                return 0;
            }

            /* A clean dodge needs the timing, the fitted camera part and a
             * spare item -- and the entry clip has to have finished. */
            if (plyr_wrk.avoid_flg == 1 &&                              /* 989 */
                m_plyr_camera.camera_power_up.mAdditionFlg.IsUp(1) &&   /* 858 */
                GetPlyrItemHaveNum(10) > 0)
            {
                if (CheckPlyrAnimeEnd(                                  /* 993 */
                        pani_dmgin_tbl[plyr_wrk.cmn_wrk.atk_rot]
                                      [plyr_wrk.cmn_wrk.atk_pos]) == 0)
                {
                    return 0;
                }

                ew->atk_tm = 0;                                         /* 1007 */
                plyr_wrk.cmn_wrk.st.sta     |= 0x20000L;                /* 1005 */
                plyr_wrk.cmn_wrk.st.dwalk_tm = 0;

                SetPlyrAnime(pani_avo_tbl[plyr_wrk.cmn_wrk.atk_pos], 1); /* 1009 */
                EndPlyrApproachCameraCtrl();                            /* 1010 */
                EffectCameraFlashReq();                                 /* 1011 */
                PlayerChangeMode(4);                                    /* 1012 */

                /* 0x10000 tells mode 4 to wait on the dodge clip rather than
                 * the ordinary release one. */
                plyr_wrk.cmn_wrk.st.sta |= 0x10000L;                    /* 1013 */

                plyr_mdlBankPlay(1, 1, 1, 0, &plyr_wrk.s3d, 0x3200, 0x1000);  /* 1014 */
                FinderBankPlay(3, 1, 1, 0, NULL, 0x3200, 0x1000);       /* 1015 */

                plyr_wrk.cmn_wrk.st.dmg = 0;                            /* 1020 */
                return 0;
            }

            if (CheckPlyrAnimeEnd(                                      /* 993 */
                    pani_dmgin_tbl[plyr_wrk.cmn_wrk.atk_rot]
                                  [plyr_wrk.cmn_wrk.atk_pos]) == 0)
            {
                return 0;
            }

            plyr_wrk.cmn_wrk.st.sta |= 0x20000L;                        /* 994 */

            CallNega2(0, 0x2d, 0x1e);                                   /* 995 */
            ReqPlyrHPSPdown(&plyr_wrk.cmn_wrk, plyr_wrk.cmn_wrk.st.dmg); /* 996 */
            PlayerChangeMode(3);                                        /* 997 */
            SetPlyrAnime(pani_dmg_tbl[plyr_wrk.cmn_wrk.atk_pos], 1);    /* 998 */
            FinderBankPlay(5, 1, 1, 0, NULL, 0x3200, 0x1000);           /* 999 */
            plyr_mdlBankPlay(2, 1, 1, 0, &plyr_wrk.s3d, 0x3200, 0x1000); /* 1000 */

            plyr_wrk.cmn_wrk.st.dmg = 0;                                /* 1001 */
            return 0;

        case 1:
        case 2:
        case 4:
            break;

        default:
            return 0;
        }

        /* An ordinary hit: the flinch. */
        CallNega2(0, 0xf, 0x1e);                                        /* 1023 */
        ReqPlyrHPSPdown(&plyr_wrk.cmn_wrk, plyr_wrk.cmn_wrk.st.dmg);    /* 1024 */
        PlyrVibCtrl(15);                                                /* 1025 */
        plyr_wrk.cmn_wrk.st.dmg = 0;                                    /* 1026 */
        PlayerChangeMode(4);                                            /* 1027 */
        FinderBankPlay(5, 1, 1, 0, NULL, 0x3200, 0x1000);               /* 1028 */
        plyr_mdlBankPlay(0, 1, 1, 0, &plyr_wrk.s3d, 0x3200, 0x1000);    /* 1029 */
        return ret;                                                     /* 1030 */

    case 3:
        /* Being held.  Damage ticks at the ghost's own attack rate and the
         * player shakes the stick to shorten the hold. */
        if (plyr_wrk.cmn_wrk.st.dmg_type == 3)                          /* 1035 */
        {
            u_char gacha;

            ReqPlyrHPSPdown(&plyr_wrk.cmn_wrk, ew->dat->atk);           /* 1043 */
            gacha = LeverGachaChk();                                    /* 1045 */

            if ((ew->st.sta & 0x8000L) != 0 && ew->atk_tm != 0)         /* 1046 */
            {
                /* Three frames off the hold per full stick sweep, but never
                 * inside the last 22 -- the release always plays out. */
                if (gacha != 0 && ew->atk_tm > 22)                      /* 1048 */
                {
                    ew->atk_tm = (u_short)(ew->atk_tm - 3);             /* 1049 */
                }

                VibrateRequest1(0, 1);                                  /* 1051 */
            }

            if (ew->atk_tm >= 21)                                       /* 1053 */
            {
                CallDeform2(0, 0, ew->atk_tm, 7, 13);                   /* 1054 */
            }

            if (ew->atk_tm != 0)                                        /* 1056 */
            {
                return ret;
            }

            SetPlyrAnime(pani_out_tbl[plyr_wrk.cmn_wrk.atk_pos], 1);    /* 1057 */
        }
        else if (plyr_wrk.cmn_wrk.st.dmg_type == 0 ||
                 plyr_wrk.cmn_wrk.st.dmg_type > 4)
        {
            return ret;
        }

        PlayerChangeMode(4);                                            /* 1058 */
        return ret;                                                     /* 1060 */

    case 4:
        /* Recovering.  dmg_type 2 (the knock-back) ends the moment the mode
         * is entered; everything else waits on its clip. */
        if (plyr_wrk.cmn_wrk.st.dmg_type == 2)                          /* 1065 */
        {
            ret = 1;
            PlayerChangeMode(0);                                        /* 1097 */
            EndPlyrApproachCameraCtrl();                                /* 1099 */
            return ret;
        }

        if (plyr_wrk.cmn_wrk.st.dmg_type == 3)                          /* 1080 */
        {
            u_char *pani_tbl = pani_out_tbl;

            if ((plyr_wrk.cmn_wrk.st.sta & 0x10000L) != 0)              /* 1085 */
            {
                pani_tbl = pani_avo_tbl;
            }

            if (CheckPlyrAnimeEnd(pani_tbl[plyr_wrk.cmn_wrk.atk_pos]) == 0)  /* 1088 */
            {
                return ret;
            }
        }
        else if (plyr_wrk.cmn_wrk.st.dmg_type == 1 ||
                 plyr_wrk.cmn_wrk.st.dmg_type == 4)
        {
            /* Clips 0x38/0x39 are the two flinches; 0x2000 is "the clip
             * already ended". */
            if ((u_char)(plyr_wrk.anime_no - 0x38) < 2 &&               /* 1070 */
                (plyr_wrk.cmn_wrk.st.sta & 0x2000L) == 0)               /* 1077 */
            {
                return ret;
            }
        }
        else
        {
            return ret;
        }

        ret = 1;                                                        /* 1090 */
        PlayerChangeMode(0);
        EndPlyrApproachCameraCtrl();                                    /* 1092 */
        return ret;                                                     /* 1095 */

    default:
        break;
    }

    return ret;                                                         /* 1115 */
}


/* ==========================================================================
 *  Reconstructed fan-out of the movement dispatch (PlyrNModeCtrl).
 * ======================================================================== */

/* Damaged-walk timer.  While non-zero PlyrMoveStaChk() forces the limping
 * pace regardless of the run button. */
void PlyrDWalkTmCtrl(PLCMN_WRK *cmn)                                    /* 2893 */
{
    if (cmn->st.dwalk_tm != 0)                                          /* 2895 */
    {
        cmn->st.dwalk_tm--;                                             /* 2896 */
    }
}

/* Finds the closest live ghost and records it on the player's work block.
 * The distance is horizontal (GetDistV drops Y), and near_ene_dist_old keeps
 * last frame's value so callers can tell approach from retreat. */
void NearAllEneInfo(PLCMN_WRK *cmn)                                     /* 2870 */
{
    float dist[2] = { 0.0f, 99999.0f };     /* sdata 3f39d8 */          /* 2872 */
    u_char near_no = 0xff;
    u_char i;

    for (i = 0; i < ENE_WRK_MAX; i++)                                   /* 2876 */
    {
        /* Only ghosts that are acting, alive, and not hidden. */
        if (ene_wrk[i].status == ENE_STATUS_ACT &&                      /* 2878 */
            ene_wrk[i].st.hp != 0 &&
            (ene_wrk[i].st.sta & 0x1000000) == 0)
        {
            float d = GetDistV(cmn->mbox.pos, ene_wrk[i].mbox.pos);     /* 2880 */

            if (d < dist[1])                                            /* 2882 */
            {
                near_no = i;
                dist[1] = d;
            }
            dist[0] = d;
        }
    }

    cmn->near_ene_dist_old = cmn->near_ene_dist;                        /* 2886 */
    cmn->near_ene_no       = near_no;                                   /* 2887 */
    cmn->near_ene_dist     = dist[1];                                   /* 2888 */
}

/* Turns Mio's head towards whatever the spot light is pointed at -- a point
 * 300 units down the light bone's X axis. */
static void PlyrSpotLightLookAt(void)                                   /* 1735 */
{
    float mtx[4][4];
    float offset[4];
    LOOK_AT_PARAM param;

    if (plyr_wrk.spot_rot[0] == 0.0f && plyr_wrk.spot_rot[1] == 0.0f)   /* 1737 */
    {
        return;
    }

    plyr_mdlGetMATRIX(mtx, 7);                                          /* 1741 */

    sceVu0ScaleVector(offset, mtx[0], 300.0f);                          /* 1743 */
    sceVu0AddVector(param.pos, offset, mtx[3]);                         /* 1744 */

    /* The eye leads, the head follows, the chest barely moves. */
    param.eye_spd   = 0.4f;                                             /* 1758 */
    param.head_spd  = 0.02f;                                            /* 1759 */
    param.chest_spd = 0.005f;                                           /* 1760 */

    PlyrNeckRegisterTarget(&param, LTP_MIO_SPOT_LIGHT);                 /* 1764 */
}

/* Stair look-at: on the stair clips she looks up or down the flight rather
 * than straight ahead.  look_at_pre_kaidan_flg gives one settling frame
 * after leaving the stairs so the head does not snap back. */
static void PlyrKaidanLookAt(void)                                      /* 1766 */
{
    float mtx[4][4];
    float offset[4];
    LOOK_AT_PARAM param;

    param.eye_spd   = 0.4f;                                             /* 1767 */
    param.head_spd  = 0.05f;                                            /* 1768 */
    param.chest_spd = 0.005f;                                           /* 1769 */

    plyr_mdlGetMATRIX(mtx, 0);                                          /* 1771 */

    /* Clips 0x10/0x11 and 0x14/0x15 are stairs-up. */
    if ((u_char)(plyr_wrk.anime_no - 0x10) < 2 ||                       /* 1774 */
        plyr_wrk.anime_no == 0x14 || plyr_wrk.anime_no == 0x15)
    {
        sceVu0ScaleVector(offset, mtx[1], 30.0f);                       /* 1777 */
        sceVu0AddVector(param.pos, mtx[3], offset);
        sceVu0ScaleVector(offset, mtx[2], 20.0f);                       /* 1779 */
        sceVu0AddVector(param.pos, param.pos, offset);

        PlyrNeckRegisterTarget(&param, LTP_MIO_KAIDAN);                 /* 1782 */
        look_at_pre_kaidan_flg = 1;
        return;
    }

    /* Clips 0x12/0x13 and 0x16/0x17 are stairs-down: look level, closer in. */
    if ((u_char)(plyr_wrk.anime_no - 0x12) < 2 ||                       /* 1791 */
        plyr_wrk.anime_no == 0x16 || plyr_wrk.anime_no == 0x17)
    {
        sceVu0ScaleVector(offset, mtx[2], 10.0f);                       /* 1794 */
        sceVu0AddVector(param.pos, mtx[3], offset);

        PlyrNeckRegisterTarget(&param, LTP_MIO_KAIDAN);                 /* 1797 */
        look_at_pre_kaidan_flg = 1;
        return;
    }

    /* Just left the stairs: one last target off the chest bone, then stop. */
    if (look_at_pre_kaidan_flg == 0)                                    /* 1806 */
    {
        return;
    }

    sceVu0ScaleVector(offset, mtx[2], 40.0f);                           /* 1810 */
    plyr_mdlGetMATRIX(mtx, 2);                                          /* 1811 */
    sceVu0AddVector(param.pos, mtx[3], offset);                         /* 1812 */

    PlyrNeckRegisterTarget(&param, LTP_MIO_KAIDAN);                     /* 1815 */
    look_at_pre_kaidan_flg = 0;                                         /* 1820 */
}

/* Clips 0x32..0x3b drive the neck from the animation itself, so the look-at
 * system is switched off across them. */
int CheckPlayerNeckSW(u_char anime_no)                                  /* 5538 */
{
    if (anime_no > 0x3b || anime_no < 0x32)                             /* 5540 */
    {
        return 1;                                                       /* 5556 */
    }
    return 0;                                                           /* 5559 */
}

/* One frame of the camera trace.  While the countdown runs the player turns
 * towards the ghost; when it expires the trace is dropped and a particle
 * burst marks the release.  A ghost that stops acting cancels it outright.
 *
 * Under 0.1 radians of error the remaining angle is written straight to rspd
 * (finish this frame); beyond that rspd is set from the unit direction, so
 * the turn eases in over several frames. */
void CEneTracer::Work(void)                                             /* 5586 */
{
    float tv[4];
    float DiffVec[4];
    float DiffVecUnit[4];

    if (mTraceEne == ENE_TRACER_NONE || IsActEnemy(mTraceEne) == 0)
    {
        Init();
        return;
    }

    /* CWaitVariable<short>::Work() counts down and reports zero on the frame
     * the countdown is still running; non-zero once it has expired. */
    if (mWaitCnt != 0)
    {
        mWaitCnt--;

        plyr_wrk.cmn_wrk.st.sta |= 0x40000;

        sceVu0CopyVector(tv, plyr_wrk.cmn_wrk.mbox.pos);
        tv[1] += -709.0f;                   /* lit4 3ee7e8, the eye offset */

        GetTrgtRot(tv, ene_wrk[(int)mTraceEne].mpos.p0, DiffVec, 3);

        DiffVec[0] -= plyr_wrk.frot_x;
        RotLimitChk(&DiffVec[0]);
        DiffVec[1] -= plyr_wrk.cmn_wrk.mbox.rot[1];
        RotLimitChk(&DiffVec[1]);

        if (GetLenUnitFromVec(DiffVecUnit, DiffVec) < 0.1f)
        {
            sceVu0CopyVector(plyr_wrk.cmn_wrk.mbox.rspd, DiffVec);
        }
        else
        {
            sceVu0ScaleVector(plyr_wrk.cmn_wrk.mbox.rspd, DiffVecUnit, 0.1f);
        }

        sceVu0CopyVector(plyr_wrk.cmn_wrk.mbox.trot, plyr_wrk.cmn_wrk.mbox.rot);
        plyr_wrk.cmn_wrk.mbox.trot[0] = plyr_wrk.frot_x;
        plyr_wrk.cmn_wrk.mbox.mloop   = 0.0f;
        return;
    }

    /* Expired: burst at the ghost's chest (mpos.p1) and let go. */
    IgEffectEffectEndParticleReq(ene_wrk[(int)mTraceEne].mpos.p1, 3);
    _ClearVector(plyr_wrk.cmn_wrk.mbox.rspd);
    mTraceEne    = ENE_TRACER_NONE;
    mTraceEneAdd = 0;
}


/* ==========================================================================
 *  Reconstructed init / per-frame spine (player.c 0x00235a98..0x00236050).
 *
 *  This is the chain that drives CalcGirlCoord(), which is the only thing
 *  that builds the bone matrices _gra3dDrawSGD() draws the player from --
 *  in the ROM PlayerMainCmn() is CalcGirlCoord()'s only caller.
 *
 *  The per-frame helpers PlayerMainCmn() and PlyrNormalCtrl() fan out to
 *  (battle / room / condition / vibration checks, HP+SP drain, stairs,
 *  finder, movement) are still stubbed further down this file.
 * ======================================================================== */

/* Debug hook; empty in the shipped prototype too. */
void PlyrDebug(void)                                                    /* 483 */
{
}

/* Clears the per-frame enemy contact bits.  Mask 0xed4 covers the flags the
 * movement / damage passes re-raise each frame. */
void ClrEneSta(void)                                                    /* 400 */
{
    u_int i;

    for (i = 0; i < ENE_WRK_MAX; i++)                                   /* 404 */
    {
        ene_wrk[i].st.sta &= ~0xed4;                                    /* 406 */
    }
}

void InitPlayer(void)                                                   /* 355 */
{
    memset(&plyr_wrk, 0, sizeof(PLYR_WRK));                             /* 357 */

    finder_off_lock_timer_cnt = 0;                                      /* 359 */

    /* Starting hp by difficulty. */
    switch (ingame_wrk.mDifficulty)                              /* 361 */
    {
    case 0:  plyr_wrk.cmn_wrk.st.hpmax = 20000; break;                  /* 364 */
    case 1:  plyr_wrk.cmn_wrk.st.hpmax = 10000; break;                  /* 366 */
    case 2:  plyr_wrk.cmn_wrk.st.hpmax =  7000; break;                  /* 368 */
    case 3:  plyr_wrk.cmn_wrk.st.hpmax =  4000; break;                  /* 370 */
    default: plyr_wrk.cmn_wrk.st.hpmax = 10000; break;                  /* 372 */
    }
    plyr_wrk.cmn_wrk.st.hp = plyr_wrk.cmn_wrk.st.hpmax;                 /* 374 */

    plyr_wrk.cmn_wrk.st.spmax   = 0;                                    /* 376 */
    plyr_wrk.cmn_wrk.st.sp      = 0;                                    /* 377 */
    plyr_wrk.cmn_wrk.st.rhspdmg = 0;                                    /* 378 */
    plyr_wrk.cmn_wrk.st.rhpdmg  = 0;                                    /* 379 */
    plyr_wrk.cmn_wrk.st.rspdmg  = 0;                                    /* 380 */

    plyr_wrk.hit_rad        = 150.0f;                                   /* 382 */
    plyr_wrk.spd[2]         = 10.5f;                                    /* 383 */
    plyr_wrk.ane_curse_lock = 1;                                        /* 384 */
    plyr_wrk.fl_pow         = 1.0f;                                     /* 385 */
    plyr_wrk.maplight_scale = 0.55f;                                    /* 386 */

    plyr_wrk.finder_lock_cnt  = 0;                                      /* 388 */
    plyr_wrk.move_lock_cnt    = 0;                                      /* 389 */
    plyr_wrk.action_lock_cnt  = 0;                                      /* 390 */
    plyr_wrk.shutter_lock_cnt = 0;                                      /* 391 */
    plyr_wrk.run_lock_cnt     = 0;                                      /* 392 */

    look_at_pre_kaidan_flg = 0;                                         /* 394 */

    /* Flashlight: a spot light, its companion, and the bounce light. */
    plyr_wrk.fl = g_NullLight;                                          /* 396 */
    plyr_wrk.fl2 = g_NullLight;                                         /* 397 */
    plyr_wrk.fl.vDirection[0] = 0.0f;                                   /* 399 */
    plyr_wrk.fl.vDirection[1] = 0.0f;
    plyr_wrk.fl.vDirection[2] = 1.0f;
    plyr_wrk.fl.vDirection[3] = 0.0f;
    plyr_wrk.fl.Type = G3DLIGHT_SPOT;                                   /* 400 */

    plyr_wrk.reflectionlight = g_NullLight;                             /* 402 */
    plyr_wrk.reflectionlight.Type = G3DLIGHT_POINT;                     /* 403 */

    plyr_wrk.ene_tracer.Init();                                         /* 405 */

    s_FlashlightType = PFT_HAND;                                        /* 407 */
}

void ReleasePlayer(void)                                                /* 390 */
{
    SndBufFadeStop(mio_deadly_player.play_id, 1);                       /* 392 */
    mio_deadly_player.play_id = CSND_BUF_PLAY_NO_ID;
}

/* Common per-frame update, run in every player mode.  flight_sw == 1 asks
 * for the flashlight to be re-aimed as part of this pass. */
void PlayerMainCmn(int flight_sw)                                       /* 487 */
{
    PlyrDebug();                                                        /* 489 */

    /* The 3D sound listener rides the player's move box. */
    plyr_wrk.s3d.pos = &plyr_wrk.cmn_wrk.mbox.pos;                      /* 492 */

    if (plyr_wrk.move_lock_cnt == 0)                                    /* 494 */
    {
        PlyrBattleCheck();                                              /* 498 */
        PlyrRoomCheck();                                                /* 499 */
        PlyrCondCheck();                                                /* 500 */
        PlyrVibCheck();                                                 /* 501 */

        ClrEneSta();                                                    /* 503 */

        PlyrSPdownCtrl(pl_sta[0]);                                      /* 505 */
        PlyrHPdownCtrl(pl_sta[0]);                                      /* 506 */

        MovePlyrStairs();                                               /* 508 */

        /* Post-damage invulnerability only counts down outside the finder
         * and damage modes (1..4). */
        if ((u_char)(plyr_wrk.cmn_wrk.mode - 1) > 3)                    /* 511 */
        {
            if (plyr_wrk.cmn_wrk.st.invisible_timer-- == 0)             /* 512 */
            {
                plyr_wrk.cmn_wrk.st.invisible_timer = 0;                /* 514 */
            }
        }

        PlyrAttractSisMain();                                           /* 523 */
        PlyrMepachiCtrl();                                              /* 526 */

        CalcGirlCoord(0);                                               /* 529 */
        PlyrAcsAlphaCtrl();                                             /* 531 */

        ANI_CTRL *ani_ctrl = plyr_mdlGetANI_CTRL();                     /* 534 */
        if (ani_ctrl != 0)                                              /* 536 */
        {
            acsClothCtrl(ani_ctrl, ani_ctrl->mpk_p, ani_ctrl->mdl_no, 0);        /* 537 */
        }

        if (flight_sw == 1)                                             /* 540 */
        {
            PlyrFlashlight(1);                                          /* 541 */
        }
    }
}

/* Mode dispatch for the ordinary (non-event) player update. */
void PlyrNormalCtrl(void)                                               /* 546 */
{
    if (plyr_wrk.move_lock_cnt != 0)                                    /* 548 */
    {
        return;
    }

    switch (plyr_wrk.cmn_wrk.mode)                                      /* 551 */
    {
    case 0:
        PlyrNModeCtrl();                                                /* 554 */
        break;
    case 5:
        PlyrFinderIn();                                                 /* 557 */
        break;
    case 6:
        PlyrFinderCtrl();                                               /* 560 */
        break;
    case 7:
        PlyrFinderEnd();                                                /* 563 */
        break;
    case 9:
        /* Dead: PlayerGameOver() drives this mode, nothing to do here and
         * notably no flashlight update. */
        return;                                                         /* 566 */
    default:
        /* Mode 2 was retired during development; anything still requesting
         * it is a bug, so complain and fall back to the normal mode. */
        if (plyr_wrk.cmn_wrk.mode == 2)                                 /* 569 */
        {
            PRINT_WARNING("PMODE IS ILLEGAL %d", plyr_wrk.cmn_wrk.mode);          /* 571 */
            plyr_wrk.cmn_wrk.mode = 0;                                  /* 572 */
            PlyrNModeCtrl();                                            /* 573 */
        }
        break;
    }

    PlyrFlashlight(1);                                                  /* 575 */
}

/* ==========================================================================
 *  Movement and animation (player.c 0x00238228..0x002392a0, 0x0023bcf8).
 *
 *  mvsta bit meanings recovered from these functions:
 *    0x0001 run          0x0008 damaged-walk     0x000f any move state
 *    0x0010 / 0x0020 / 0x0040 / 0x0080  step-in / step-out variants
 *    0x0100 damaged walk latch          0x0200 camera-change hold
 *    0x0400 turning in place            0x0800 / 0x1000 rapid turn (walk/run)
 * ======================================================================== */

/* Free-look ground movement.  Runs the camera-relative stick vector through
 * the turn logic, then hands the result to PlyrPosSet(). */
void PlyrNModeCtrl(void)                                                /* 1829 */
{
    float tv[4];

    /* Heading of the camera, so stick "up" means "away from the camera". */
    plyr_wrk.prot = GetTrgtRotY(gra3dGetCamera()->matCoord[3],
                                plyr_wrk.cmn_wrk.mbox.pos);             /* 1831 */

    _ClearVector(tv);                                                   /* 1833 */

    PlyrDWalkTmCtrl(&plyr_wrk.cmn_wrk);                                 /* 1835 */

    /* opt_wrk.move_operate: 0 = normal, 1 = "vehicle" (tank) controls. */
    if (opt_wrk.move_operate == 1)                                      /* 1840 */
    {
        PlyrMoveChkV(&plyr_wrk.cmn_wrk.mbox, tv, PlyrMovePad());        /* 1843 */
    }
    else
    {
        PlyrMoveChk(&plyr_wrk.cmn_wrk.mbox, tv, PlyrMovePad());         /* 1847 */
    }

    PlyrPosSet(&plyr_wrk.cmn_wrk.mbox, tv);                             /* 1851 */
    PlyrNAnimeCtrl();                                                   /* 1853 */

    NearAllEneInfo(&plyr_wrk.cmn_wrk);                                  /* 1856 */
    PlyrSpotLightLookAt();                                              /* 1858 */
    PlyrKaidanLookAt();                                                 /* 1859 */

    /* Raising the camera needs the camera in the inventory, an unlocked
     * finder, the normal mode, and a deliberate (>7 frame) hold. */
    if (plyr_wrk.finder_lock_cnt == 0 &&                                /* 1874 */
        plyr_wrk.cmn_wrk.mode == 0 &&
        GetPlyrItemHaveNum(10) != 0 &&
        *paddat[6] == 1 && *pushdat[6] > 7)
    {
        printf("finder in\n");                                          /* 1877 */
        SetPlyrFinderIn();                                              /* 1878 */
    }
}

/* Finder-mode strafing: no turning, and only while not stunned. */
void PlyrFModeMoveCtrl(void)                                            /* 1888 */
{
    float tv[4];

    plyr_wrk.cmn_wrk.st.mvsta &= ~0xf;                                  /* 1890 */

    if (plyr_wrk.cmn_wrk.st.cond != 1 && plyr_wrk.cmn_wrk.st.cond != 3) /* 1893 */
    {
        _ClearVector(tv);                                               /* 1896 */
        PlyrMovePadFind(&plyr_wrk.cmn_wrk.mbox, tv);                    /* 1898 */
        PlyrPosSet(&plyr_wrk.cmn_wrk.mbox, tv);                         /* 1899 */
    }
}

float PlyrMovePad(void)                                                 /* 1908 */
{
    return GetMovePad(0);                                               /* 1912 */
}

/* Camera-relative movement.  The bulk of this is turn handling: the player
 * turns towards the stick at a fixed rate, snaps through a rapid-turn
 * animation past 135 degrees, and holds still for a beat after a camera cut
 * so a cut does not fling her in a new direction. */
void PlyrMoveChk(MOVE_BOX *mb, float *tv, float rot)                    /* 1916 */
{
    /* Frame histories, so a camera cut can compare against what the stick
     * was doing before it. */
    static u_char ds[5];                    /* sdata 3f39a8 */
    static float  rs[2];                    /* sdata 3f39b8 */
    static float  keep_rot;                 /* sdata 3f39b0 */
    static u_char no_rot_cng2;              /* sdata 3f39ae */

    u_char no_move;
    float fr[4];
    float rcng;
    float step;

    memset(fr, 0, sizeof(fr));                                          /* 1921 */

    /* Mid rapid-turn: play it out, ignoring the stick entirely. */
    if ((plyr_wrk.cmn_wrk.st.mvsta & 0x1800) != 0)                      /* 1924 */
    {
        ANI_CTRL *ani_ctrl = plyr_mdlGetANI_CTRL();
        if (ani_ctrl == 0 || motCheckInterp(ani_ctrl) != 0)             /* 1927 */
        {
            return;
        }

        mb->rot[1] += mb->rspd[1];                                      /* 1934 */
        RotLimitChk(&mb->rot[1]);
        RotFvector(mb->rot, tv);

        if (mb->mloop > 1.0f)                                           /* 1931 */
        {
            mb->mloop -= 1.0f;
            return;
        }

        mb->rspd[1] = 0.0f;                                             /* 1943 */
        plyr_wrk.cmn_wrk.st.mvsta &= ~0x1800;                           /* 1944 */
        return;
    }

    no_move = PlyrMoveStaChk(rot);                                      /* 1950 */
    GetMoveSpeed(tv);                                                   /* 1951 */

    PadInfoTmpSave(ds, pad[0].an_dir[0], rs, rot);                      /* 1953 */

    rcng = keep_rot - rs[0];                                            /* 1955 */
    RotLimitChk(&rcng);
    rcng = fabsf(rcng);

    if (rot == 10.0f && no_move == 0)                                   /* 1960 */
    {
        /* Stick centred: drop the camera-change hold outright. */
        cam_cng_tm = 0;                                                 /* 1962 */
        plyr_wrk.cmn_wrk.st.mvsta &= ~0x200;
        no_rot_cng2 = 0;
    }
    else
    {
        if (rot == 10.0f)
        {
            cam_cng_tm = 0;
            plyr_wrk.cmn_wrk.st.mvsta &= ~0x200;
            no_rot_cng2 = 0;
        }

        if (cam_cng_tm != 0)                                            /* 1975 */
        {
            /* Just after a cut: hold the heading for a few frames so the new
             * camera does not immediately spin her. */
            if (cam_cng_tm == (GetPALMode() != 0 ? 16 : 20))            /* 1977 */
            {
                keep_rot = rs[0];
            }
            mb->rspd[1] = 0.0f;
            RotFvector(mb->rot, tv);
            cam_cng_tm--;

            if (rcng < 1.5707961f)              /* 90 deg */            /* 1990 */
            {
                if (cam_cng_tm == 0)
                {
                    plyr_wrk.cmn_wrk.st.mvsta |= 0x200;
                }
                return;
            }

            /* Stick swung more than 90 degrees across the cut -- honour it. */
            cam_cng_tm = 0;                                             /* 1997 */
            plyr_wrk.cmn_wrk.st.mvsta &= ~0x200;
            return;
        }

        if ((plyr_wrk.cmn_wrk.st.mvsta & 0x200) != 0 &&
            rcng <= 0.34906581f)               /* 20 deg */             /* 2004 */
        {
            no_rot_cng2 = 1;
            RotFvector(mb->rot, tv);
            return;
        }

        cam_cng_tm = 0;

        if ((rs[1] == rot && (plyr_wrk.cmn_wrk.st.mvsta & 0x400) == 0) ||
            no_move != 0)                                               /* 2012 */
        {
            RotFvector(mb->rot, tv);
            return;
        }
    }

    cam_cng_tm = 0;

    if ((plyr_wrk.cmn_wrk.st.mvsta & 0xf) == 0)                         /* 2024 */
    {
        RotFvector(mb->rot, tv);
    }

    plyr_wrk.cmn_wrk.st.mvsta &= ~0x200;

    if (rot == 10.0f)                                                   /* 2030 */
    {
        return;
    }

    /* Target heading = stick direction rotated into camera space. */
    fr[1] = rot + plyr_wrk.prot;                                        /* 2036 */
    RotLimitChk(&fr[1]);

    rot = fr[1] - mb->rot[1];                                           /* 2039 */
    RotLimitChk(&rot);

    /* Turn rate is per-frame, so PAL turns further each frame. */
    step = (GetPALMode() != 0) ? 0.33510315f : 0.27925265f;             /* 2043 */
    rcng = step;
    if (rot <= 0.0f)
    {
        rot  = fabsf(rot);
        rcng = -step;
    }

    if (rot > 2.356194f && no_rot_cng2 == 0)   /* 135 deg */            /* 2053 */
    {
        CngPlyrRotRapid(mb, fr[1]);                                     /* 2055 */
        plyr_wrk.cmn_wrk.st.mvsta =
            (plyr_wrk.cmn_wrk.st.mvsta & ~0x400) |
            (((plyr_wrk.cmn_wrk.st.mvsta & 1) == 0) ? 0x800 : 0x1000);  /* 2058 */
        RotLimitChk(&mb->rot[1]);
        RotFvector(mb->rot, tv);
        return;
    }

    /* Ordinary turn: walk the heading towards the target one step a frame,
     * and move along the *target* heading meanwhile. */
    RotFvector(fr, tv);                                                 /* 2070 */

    if (rot > step)                                                     /* 2072 */
    {
        mb->rot[1] += rcng;
        RotLimitChk(&mb->rot[1]);
        plyr_wrk.cmn_wrk.st.mvsta |= 0x400;
        return;
    }

    mb->rot[1] = fr[1];                                                 /* 2084 */
    plyr_wrk.cmn_wrk.st.mvsta &= ~0x400;
}

/* "Vehicle" control scheme: left/right turn on the spot, up/down drive. */
void PlyrMoveChkV(MOVE_BOX *mb, float *tv, float mrot)                  /* 2090 */
{
    u_char dir = 0xff;
    float step = 0.069813162f;              /* 4 deg per frame */

    _ClearVector(tv);                                                   /* 2095 */

    if ((plyr_wrk.cmn_wrk.st.mvsta & 0x1800) != 0)                      /* 2098 */
    {
        ANI_CTRL *ani_ctrl = plyr_mdlGetANI_CTRL();
        if (ani_ctrl == 0 || motCheckInterp(ani_ctrl) != 0)
        {
            return;
        }

        mb->rot[1] += mb->rspd[1];
        RotLimitChk(&mb->rot[1]);
        RotFvector(mb->rot, tv);

        if (mb->mloop > 1.0f)
        {
            mb->mloop -= 1.0f;
            return;
        }

        mb->rspd[1] = 0.0f;
        plyr_wrk.cmn_wrk.st.mvsta &= ~0x1800;
        return;
    }

    PlyrMoveStaChk(mrot);                                               /* 2126 */

    if (mrot != 10.0f)                                                  /* 2128 */
    {
        dir = ConvertRot2Dir(mrot, 1);                                  /* 2130 */

        switch (dir)
        {
        case 1: case 2: case 3:
            mb->rot[1] += step;                                         /* 2136 */
            RotLimitChk(&mb->rot[1]);
            break;
        case 5: case 6: case 7:
            mb->rot[1] -= step;                                         /* 2142 */
            RotLimitChk(&mb->rot[1]);
            break;
        case 4:
            /* Straight back: turn about face rather than reverse. */
            CngPlyrRotRapid(mb, mb->rot[1] + 3.1415925f);               /* 2148 */
            plyr_wrk.cmn_wrk.st.mvsta =
                (plyr_wrk.cmn_wrk.st.mvsta & ~0x400) |
                (((plyr_wrk.cmn_wrk.st.mvsta & 1) == 0) ? 0x800 : 0x1000);
            RotLimitChk(&mb->rot[1]);
            RotFvector(mb->rot, tv);
            break;
        default:
            break;
        }
    }

    /* Drive only when pointing roughly forward, or already moving. */
    if (dir < 2 || dir == 7 ||                                          /* 2167 */
        (plyr_wrk.cmn_wrk.st.mvsta & 1) != 0 ||
        (plyr_wrk.cmn_wrk.st.mvsta & 8) != 0)
    {
        GetMoveSpeed(tv);                                               /* 2170 */
        RotFvector(mb->rot, tv);
    }
}

/* Finder-mode movement: strafe in screen space, no turning. */
void PlyrMovePadFind(MOVE_BOX *mb, float *tv)                           /* 2182 */
{
    static const u_char anime_tbl[4] = { 2, 3, 4, 5 };   /* sdata 3f39c8 */

    u_char anime_no = plyr_wrk.anime_no;                                /* 2184 */

    if (plyr_wrk.finder_tm != 0)                                        /* 2186 */
    {
        return;
    }

    float rot = GetMovePad(1);                                          /* 2188 */
    if (rot == 10.0f)
    {
        anime_no = 0;                                                   /* 2191 */
    }
    else
    {
        u_int ftype = GetPlyrFtype() & 0xff;                            /* 2196 */
        if (ftype == 4)
        {
            ftype = 0;
        }

        plyr_wrk.cmn_wrk.st.mvsta |= 4;                                 /* 2199 */
        plyr_wrk.spd[2] = 10.5f;                                        /* 2200 */
        if (GetPALMode() != 0)
        {
            plyr_wrk.spd[2] *= 1.2f;                                    /* 2202 */
        }

        tv[0] = plyr_wrk.spd[2] * sinf(rot);                            /* 2205 */
        tv[2] = plyr_wrk.spd[2] * cosf(rot);
        RotFvector(mb->rot, tv);                                        /* 2207 */

        /* Only re-pick the clip when she is not already strafing. */
        if (anime_tbl[0] != plyr_wrk.anime_no &&                        /* 2211 */
            anime_tbl[1] != plyr_wrk.anime_no &&
            anime_tbl[2] != plyr_wrk.anime_no &&
            anime_tbl[3] != plyr_wrk.anime_no)
        {
            anime_no = anime_tbl[ftype];                                /* 2216 */
        }
    }

    if (plyr_wrk.anime_no != anime_no)                                  /* 2224 */
    {
        SetPlyrAnime(anime_no, 10);                                     /* 2225 */
    }
}

/* Stick/d-pad heading in radians, or 10.0 for "centred".  id 0 is the move
 * stick, id 1 the finder strafe stick. */
float GetMovePad(u_char id)                                             /* 2232 */
{
    float rot = 10.0f;

    if (id == 0)                                                        /* 2236 */
    {
        if (opt_wrk.move_operate == 1)                                  /* 2238 */
        {
            /* Vehicle scheme reads the d-pad as eight octants. */
            u_int dir = 0xff;

            if (*paddat[9] != 0)        { dir = 4; }                    /* 2242 */
            else if (*paddat[8] == 1)   { dir = 0; }

            if (*paddat[11] != 0)                                       /* 2250 */
            {
                dir = (dir == 0xff) ? 2 : (dir + 2) / 2;
            }
            else if (*paddat[10] != 0)
            {
                if (dir == 0xff)
                {
                    dir = 6;
                }
                else
                {
                    if (dir == 0) { dir = 8; }      /* wrap for the average */
                    dir = (dir + 6) / 2;
                }
            }

            if (dir != 0xff)                                            /* 2270 */
            {
                rot = (float)dir * 0.78539813f - 3.1415925f;
            }
        }

        if (pad[0].id == 'y' && pad[0].an_dir[0] != 0xff)               /* 2278 */
        {
            return pad[0].an_rot[0];
        }
        return rot;
    }

    /* opt_wrk.ana_replace swaps which stick drives the finder. */
    if (opt_wrk.ana_replace == 0)                                       /* 2286 */
    {
        if (pad[0].an_dir[1] != 0xff) { return pad[0].an_rot[1]; }
    }
    else if (opt_wrk.ana_replace == 1)                                  /* 2291 */
    {
        if (pad[0].an_dir[0] != 0xff) { return pad[0].an_rot[0]; }
    }
    else if (pad[0].an_dir[1] != 0xff)                                  /* 2296 */
    {
        return pad[0].an_rot[1];
    }

    return rot;                                                         /* 2302 */
}

/* Folds the run button and stick deflection into mvsta's low nibble.
 * Returns non-zero when the stick is centred while the run button is held --
 * PlyrMoveChk() treats that as "no move" but still lets the turn run. */
u_char PlyrMoveStaChk(float pad_chk)                                    /* 2306 */
{
    u_long mv = 0;
    u_char no_move = 0;

    /* paddat[4] is run.  It latches: once running it must be released. */
    if (((plyr_wrk.cmn_wrk.st.mvsta & 0xf) == 0 && *paddat[4] == 1) ||  /* 2310 */
        ((plyr_wrk.cmn_wrk.st.mvsta & 0xf) != 0 && *paddat[4] != 0))
    {
        mv = 1;                                                         /* 2318 */
        if (pad_chk == 10.0f)
        {
            no_move = 1;
        }
    }
    else if (pad_chk != 10.0f)                                          /* 2326 */
    {
        if (PlyrLeverInputChk() != 0)
        {
            mv = 2;                                                     /* 2330 */
        }
    }

    /* Damaged walk overrides the pace. */
    if (plyr_wrk.cmn_wrk.st.dwalk_tm != 0)                              /* 2338 */
    {
        mv = (mv == 0) ? 0x100 : 0x108;
    }

    plyr_wrk.cmn_wrk.st.mvsta = (plyr_wrk.cmn_wrk.st.mvsta & ~0x10f) | mv;       /* 2344 */

    return no_move;                                                     /* 2348 */
}

/* 0 = centred, 1 = walk deflection, 2 = run deflection. */
u_int PlyrLeverInputChk(void)                                           /* 2357 */
{
    u_int ret = 0;

    if (*paddat[9] != 0 || *paddat[8] != 0 ||                           /* 2361 */
        *paddat[10] != 0 || *paddat[11] != 0)
    {
        ret = 1;
    }

    if (pad[0].id == 'y')                       /* DualShock analog */  /* 2368 */
    {
        /* Fold each axis to a 0..127 deflection about centre. */
        u_char dx = ((pad[0].analog[2] & 0x80) == 0)
                        ? (u_char)(0x7f - pad[0].analog[2])
                        : (u_char)(pad[0].analog[2] + 0x80);            /* 2372 */
        u_char dy = ((pad[0].analog[3] & 0x80) == 0)
                        ? (u_char)(0x7f - pad[0].analog[3])
                        : (u_char)(pad[0].analog[3] + 0x80);

        u_int len = (u_int)GetDist((float)dx, (float)dy) & 0xff;        /* 2380 */

        if (len > 0x31) { ret = 1; }                                    /* 2383 */
        if (len > 0x7e) { ret = 2; }                                    /* 2386 */
    }

    return ret;                                                         /* 2389 */
}

void GetMoveSpeed(float *tv)                                            /* 2397 */
{
    if (plyr_wrk.cmn_wrk.mode == 2)                                     /* 2399 */
    {
        return;
    }

    tv[0] = plyr_wrk.spd[0];                                            /* 2403 */
    tv[2] = plyr_wrk.spd[2];

    if (plyr_wrk.cmn_wrk.mode != 8 && debug_var.hi_spd != 0)            /* 2407 */
    {
        tv[2] = plyr_wrk.spd[2] * 4.0f;                                 /* 2409 */
    }
}

/* Commits one frame of movement: rolls the position/velocity history, adds
 * the step, resolves wall collision, then re-grounds. */
void PlyrPosSet(MOVE_BOX *mb, float *tv)                                /* 2415 */
{
    g3dxVu0CopyVector(mb->bpos, mb->pos);                               /* 2419 */
    g3dxVu0CopyVector(mb->bmv, mb->mv);                                 /* 2420 */
    g3dxVu0CopyVector(mb->mv, tv);                                      /* 2421 */

    sceVu0AddVector(mb->pos, mb->pos, tv);                              /* 2423 */

    MapHitCheck(plyr_wrk.cmn_wrk.mbox.pos, plyr_wrk.cmn_wrk.mbox.pos,   /* 2427 */
                plyr_wrk.cmn_wrk.mbox.bpos, plyr_wrk.hit_rad,
                plyr_wrk.cmn_wrk.floor);

    PlyrHeightCtrl(tv);                                                 /* 2437 */
}

/* Snaps the player to the floor and keeps the room / area bookkeeping in
 * step with where she ended up. */
void PlyrHeightCtrl(float *tv)                                          /* 2439 */
{
    int area_no = MhCtlGetRoomNo(plyr_wrk.cmn_wrk.floor,                /* 2441 */
                                 plyr_wrk.cmn_wrk.mbox.pos);
    if (area_no >= 0 && GetPlyrAreaNo() != area_no)                     /* 2443 */
    {
        SetPlyrAreaNo(area_no);                                         /* 2445 */
    }

    MhCtlGetMapHeight(tv, plyr_wrk.cmn_wrk.mbox.pos,                    /* 2451 */
                      plyr_wrk.cmn_wrk.pr_info.area_no, 1);
    plyr_wrk.cmn_wrk.mbox.pos[1] = tv[1];                               /* 2453 */

    u_int room_id = GetRoomLabel(GetPlyrAreaNo(), plyr_wrk.cmn_wrk.floor,        /* 2457 */
                                 plyr_wrk.cmn_wrk.mbox.pos);
    if (room_id != 0xffffffff && room_id != plyr_wrk.cmn_wrk.pr_info.room_id)    /* 2459 */
    {
        plyr_wrk.cmn_wrk.pr_info.room_old = plyr_wrk.cmn_wrk.pr_info.room_id;    /* 2462 */
        SetPlyrRoomID(room_id);                                         /* 2463 */
    }
}

/* Rolls a 5-frame direction history and a 2-frame rotation history. */
void PadInfoTmpSave(u_char *dir_save, u_char dir_now,                   /* 2484 */
                    float *rot_save, float rot_now)
{
    u_int i;

    for (i = 4; i != 0; i--)                                            /* 2486 */
    {
        dir_save[i] = dir_save[i - 1];
    }
    for (i = 1; i != 0; i--)                                            /* 2487 */
    {
        rot_save[i] = rot_save[i - 1];
    }

    dir_save[0] = dir_now;                                              /* 2489 */
    rot_save[0] = rot_now;                                              /* 2490 */
}

/* Sets up a rapid (about-face) turn: picks the short way round based on the
 * costume's turn animation, then spreads it over the clip's frame count. */
void CngPlyrRotRapid(MOVE_BOX *mb, float rot0)                          /* 2496 */
{
    float diff = rot0 - mb->rot[1];                                     /* 2498 */

    switch (GetPlyrFtype())                                             /* 2500 */
    {
    case 0:
    case 3:
    case 4:
        if (diff < 0.0f) { diff += 6.283185f; }                         /* 2504 */
        break;
    case 1:
    case 2:
        if (diff > 0.0f) { diff -= 6.283185f; }                         /* 2509 */
        break;
    default:
        break;
    }

    /* PAL plays the same clip in fewer frames. */
    float frames = (GetPALMode() != 0) ? 10.833334f : 13.0f;            /* 2515 */

    mb->mloop    = frames;                                              /* 2518 */
    mb->rspd[1]  = diff / frames;                                       /* 2519 */
}

/* Picks the idle / walk / run clip from mvsta and the costume type. */
void PlyrNAnimeCtrl(void)                                               /* 4056 */
{
    /* mvsta bit to test, in priority order. */
    static const u_int psta_chk_tbl[10] =                               /* 4058 */
    {
        0x1000, 0x40, 0x80, 0x10, 0x20, 1, 1, 2, 4, 8
    };
    /* Four clips per state: one per costume type. */
    static const u_char pmani_no_tbl[10][4] =                           /* 4066 */
    {
        { 0x41, 0x42, 0x42, 0x41 },
        { 0x14, 0x15, 0x14, 0x15 },
        { 0x16, 0x17, 0x16, 0x17 },
        { 0x10, 0x11, 0x10, 0x11 },
        { 0x12, 0x13, 0x12, 0x13 },
        { 0x06, 0x07, 0x08, 0x09 },
        { 0x0c, 0x0d, 0x0e, 0x0f },
        { 0x02, 0x03, 0x04, 0x05 },
        { 0x02, 0x03, 0x04, 0x05 },
        { 0x0a, 0x0b, 0x0a, 0x0b }
    };

    u_int anime_no = 0;
    u_int idx = 0;

    if (plyr_wrk.cmn_wrk.mode == 9)                                     /* 4098 */
    {
        return;
    }

    if ((plyr_wrk.cmn_wrk.st.mvsta & 0x10ff) != 0)                      /* 4101 */
    {
        /* Rapid turn (0x1000) wins outright; otherwise take the first
         * matching state in table order. */
        if ((plyr_wrk.cmn_wrk.st.mvsta & 0x1000) == 0)                  /* 4104 */
        {
            for (idx = 1; idx < 10; idx++)
            {
                if ((plyr_wrk.cmn_wrk.st.mvsta & psta_chk_tbl[idx]) != 0)
                {
                    break;
                }
            }
        }

        /* Outdoors uses the next entry along -- the run states are doubled
         * up so the outdoor variants sit right after the indoor ones. */
        if (PlyrOutsideCheck() != 0 && idx < 10 && psta_chk_tbl[idx] == 1)        /* 4126 */
        {
            idx++;
        }

        if (idx < 10 &&                                                 /* 4134 */
            (pmani_no_tbl[idx][0] == plyr_wrk.anime_no ||
             pmani_no_tbl[idx][1] == plyr_wrk.anime_no ||
             pmani_no_tbl[idx][2] == plyr_wrk.anime_no ||
             pmani_no_tbl[idx][3] == plyr_wrk.anime_no))
        {
            /* Already in this state's clip set -- leave it alone. */
            anime_no = plyr_wrk.anime_no;                               /* 4141 */
        }
        else
        {
            u_int ftype = GetPlyrFtype() & 0xff;                        /* 4145 */
            if (ftype == 4)
            {
                ftype = 0;
            }
            anime_no = (idx < 10) ? pmani_no_tbl[idx][ftype]            /* 4152 */
                                  : plyr_wrk.anime_no;
        }
    }

    /* Idle: swap to the hurt idle once stamina drops under 1200. */
    if (anime_no == 0 && GetSynchroModeFlg() == 0)                      /* 4166 */
    {
        anime_no = ((u_short)(plyr_wrk.cmn_wrk.st.hp - 1) < 1200);      /* 4168 */
    }

    if (plyr_wrk.anime_no != anime_no)                                  /* 4176 */
    {
        SetPlyrAnime((u_char)anime_no, 10);                             /* 4177 */
    }
}

/* Applies one frame of root motion coming out of the animation. */
void PlyrMotionMovement(void)                                           /* 578 */
{
    float tv[4];

    _ClearVector(tv);                                                   /* 582 */

    PlyrMoveChk(&plyr_wrk.cmn_wrk.mbox, tv, 10.0f);                     /* 584 */
    PlyrPosSet(&plyr_wrk.cmn_wrk.mbox, tv);                             /* 585 */

    PlyrFlashlight(0);                                                  /* 602 */
}

int PlayerGameOver(void)                                                /* 605 */
{
    if (plyr_wrk.move_lock_cnt == 0 && plyr_wrk.cmn_wrk.mode == 9)      /* 607 */
    {
        return PlyrDead();                                              /* 609 */
    }

    return 0;                                                           /* 615 */
}

/* --------------------------------------------------------------------------
 *  CEneTracer -- holds the camera on one ghost for a countdown of frames.
 * ------------------------------------------------------------------------ */

int CEneTracer::Init(void)                                              /* 5567 */
{
    char cPrev = mTraceEne;

    mWaitCnt      = 0;
    mTraceEne     = ENE_TRACER_NONE;
    mTraceEneAdd  = 0;

    _ClearVector(plyr_wrk.cmn_wrk.mbox.rspd);                           /* 5577 */
    plyr_wrk.cmn_wrk.st.sta &= ~0x40000;                                /* 5578 */

    return cPrev != ENE_TRACER_NONE;                                    /* 5580 */
}

void CEneTracer::Req(int iEneNo, int iFrame)
{
    /* First request wins the countdown; later ones only re-point it. */
    if (mWaitCnt == 0)
    {
        mWaitCnt = (short)iFrame;
    }
    mTraceEne    = (char)iEneNo;
    mTraceEneAdd = 0;
}

/* Reconstructed (0x0023db58).  The ROM writes w before z; order is
 * immaterial on the host. */
void GetCenterPoint(float *center, float *t1, float *t2)
{
    center[0] = (t1[0] + t2[0]) * 0.5f;
    center[1] = (t1[1] + t2[1]) * 0.5f;
    center[2] = (t1[2] + t2[2]) * 0.5f;
    center[3] = 1.0f;
}
