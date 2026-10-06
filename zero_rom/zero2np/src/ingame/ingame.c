// FILE: /home/zero_rom/zero2np/src/ingame/ingame.c
//
// Reconstructed from the Feb 6 2004 prototype (SLES_523.84).

#include "../main/phasefunc.h"
#include "../main/gphase.h"
#include "../main/main.h"
#include "../common/variable.h"
#include "../system/eeiop/cddat.h"
#include "../system/eeiop/snd3d.h"
#include "../system/eeiop/snd_buffer.h"
#include "../system/eeiop/stream_auto.h"

#include <stdio.h>
#include <string.h>

#include "camera_menu.h"
#include "enemy/enemy.h"
#include "enemy/fene_entry.h"
#include "fade.h"
#include "map/foot_se.h"
#include "gra3d.h"
#include "ingame.h"
#include "camera/map_camera.h"
#include "map/map_rectangle.h"
#include "loading.h"
#include "logo.h"
#include "main_decls.h"
#include "mem_util.h"
#include "m_plyr_camera.h"
#include "ol_load.h"
#include "system.h"
#include "title.h"
#include "utility2.h"
#include "eecdvd.h"
#include "debug/debug_menu.h"
#include "graphics/effect/effect.h"
#include "graphics/effect/effect_butterfly.h"
#include "ingame_effect.h"                    /* IgEffectInit / IgEffectMain */
#include "graphics/graph2d/g2d_draw.h"
#include "graphics/graph2d/message.h"
#include "graphics/motion/mdlwork.h"
#include "graphics/obj_draw_ctrl.h"
#include "graphics/scene/scene.h"
#include "movie_room_menu/prg/movie_projecter.h"
#include "../debug/item_debug.h"
#include "map/map_bgm.h"
#include "map/map_reverb.h"
#include "door/prg/door.h"                       /* DoorCtrlInit */
#include "map/MapDoor.h"
#include "map/MapDraw.h"
#include "map/MapFog.h"
#include "map/MapLoad.h"
#include "map/MapObj.h"
#include "map/MapPut.h"
#include "map/MapSave.h"
#include "map/MapView.h"
#include "map/MhCtl.h"
#include "map/map_height.h"
#include "menu/ghost_seal_door.h"                /* GhostSealDoorInit */
#include "menu/menu.h"
#include "mission.h"
#include "movie_room_menu/prg/movie_room_menu.h"
#include "pause/prg/pause.h"
#include "photo.h"
#include "menu/play_data.h"
#include "item/prg/item.h"          // AllPlyrItemInit / AllPlyrEventItemLost
#include "item/prg/soul_list.h"     // PlyrSoulListInit
#include "item/prg/file.h"          // AllPlyrFileInit
#include "item/prg/crystal.h"       // PlyrCrystalInit
#include "item/prg/level_gem.h"     // PlyrLevelGemInit
#include "item/prg/memo.h"          // PlyrMemoInit
#include "plyr/player.h"
#include "plyr/sis_mdl.h"
#include "plyr/sister.h"
#include "puzzle/puzzle.h"
#include "chr_sort.h"
#include "enemy/enemy_dat.h"
#include "menu/plyr_room_info.h"
#include "savepoint/savepoint.h"
#include "../graphics/effect/effect_scr.h"
#include "subtitle/subtitle.h"
#include "event/prg/event.h"
#include "graphics/motion/accessory.h"
#include "graphics/motion/Morph.h"
#include "system/pad/pad.h"
#include "system/pad/vib_manage.h"
#include "../common/save_data.h"
#include "movie_room.h"

/* --------------------------------------------------------------------------
 *  ingame.o's own storage.
 *
 *  .data 3186b0, 0x80 total: the three objects below sit back to back.  All
 *  three used to be defined elsewhere in the port (ingame_wrk in main/glob.c,
 *  fene_entry in enemy/fene_entry.c, movie_room nowhere at all); ZERO2.MAP puts
 *  every one of them in ingame.o, and it is fene_entry's BIT_FLAGS<66> that
 *  pulls this file's entry in .ctors.
 *
 *  The initialisers really are all zero -- the ELF image for 3186b0..318730 is
 *  blank, and the constructors run at boot.
 * ------------------------------------------------------------------------ */
INGAME_WRK ingame_wrk;                                      /* data 3186b0 */
CMovieRoom movie_room;                                      /* data 3186c0 */
CFEneEntry fene_entry;                                      /* data 318710 */

/* .sdata 3f1550.  Global, not static: ZERO2.MAP carries a symbol for it, which
 * the file-statics below deliberately lack.  It lands in .sdata rather than
 * .sbss because the ROM spells the zero initialiser out. */
int iPauseLockTimer = 0;                                    /* sdata 3f1550 */

/* .sbss 3f4d10, 0x18 total -- exactly these five, in this order.  None carries
 * a global symbol, so all five are file-static.  phase_change_reqs and
 * OutPhaseChangeFlg in particular are reached only through the SendIngame /
 * SetIngame / CheckIngame accessors below. */
static int                     before_game_load_wait;       /* sbss 3f4d10 */
static int                     load_game_step;              /* sbss 3f4d14 */
static PHASE_CHANGE_REQS       phase_change_reqs;           /* sbss 3f4d18 */
static PHASE_CANGE_REQ_OUTGAME OutPhaseChangeFlg;           /* sbss 3f4d20 */
static int                     story_effect_time;           /* sbss 3f4d24 */

/* Local-to-local VRAM blit used to park the finished frame before a menu,
 * pause or map screen takes the display over, so the overlay can be rebuilt
 * from it every frame.  Same frame-buffer stride pause.c derives; the ROM
 * reaches it as ((count + 1) & 1) * 0x1180, which GCC strength-reduced into a
 * 64-bit shift pair (* 0x23 << 0x27 >> 0x20) that means nothing on its own.
 * The destination differs from pause.c's own capture area. */
#define INGAME_FRAME_BUF_ADRS 0x1180
#define INGAME_CAPTURE_ADRS   0x3aa0

static void IngameWrkInitNotPlayData(void);
static void ClearBeforeGameInit(void);
static int  InitBeforeGame(void);

/* --------------------------------------------------------------------------
 *  INGAME_WRK lock counters.
 *
 *  The unlock pair decrements unconditionally and asserts afterwards if the
 *  counter went negative -- it does not clamp.  The "is locked" pair hands
 *  back the raw counter rather than a boolean, so an unbalanced unlock leaves
 *  a negative value that still reads as locked at every call site.  Both
 *  details are load-bearing: one_Story_Normal() tests these against zero.
 * ------------------------------------------------------------------------ */

void INGAME_WRK::Init()
{
    this->mMenuLockCnt = 0;                                              /* 244 */
    this->mPauseLockCnt = 0;                                             /* 245 */
}

void INGAME_WRK::MenuLock()
{
    this->mMenuLockCnt++;                                                /* 250 */
}

void INGAME_WRK::MenuUnlock()
{
    this->mMenuLockCnt--;                                                /* 255 */

    if (this->mMenuLockCnt < 0) {                                        /* 258 */
        PRINT_ASSERT("Error! %s", __FUNCTION__);                        /* 259 */
    }
}

int INGAME_WRK::MenuIsLocked()
{
    return this->mMenuLockCnt;                                           /* 265 */
}


void INGAME_WRK::PauseLock()
{
    this->mPauseLockCnt++;                                               /* 269 */
}

void INGAME_WRK::PauseUnlock()
{
    this->mPauseLockCnt--;                                               /* 274 */

    if (this->mPauseLockCnt < 0) {                                       /* 277 */
        PRINT_ASSERT("Error! %s", __FUNCTION__);                        /* 278 */
    }
}

int INGAME_WRK::PauseIsLocked()
{
    return this->mPauseLockCnt;                                          /* 284 */
}

void IngameSceneReq(int scene_no)
{
    phase_change_reqs.scene_no = (short)scene_no;                        /* 294 */
}

/* IngameEventMsgDispReq and IngameEventFileDispReq really are byte-for-byte
 * twins in the ROM, down to both bumping event_stop_cnt -- the second is a
 * copy-paste of the first that was never specialised.  Kept as two functions
 * because ZERO2.MAP carries two symbols. */
void IngameEventMsgDispReq(int flg)
{
    if (flg) {                                                     /* 298 */
        phase_change_reqs.event_stop_cnt++;   /* 299 */
    } else {
        phase_change_reqs.event_stop_cnt--;   /* 301 */
    }

    if (300 < (short)phase_change_reqs.event_stop_cnt) {                /* 305 */
        PRINT_WARNING("Event Stop Cnt Is Over 300");                    /* 306 */
    }
    else if ((short)phase_change_reqs.event_stop_cnt < 0) {                  /* 308 */
        phase_change_reqs.event_stop_cnt = 0;
        PRINT_WARNING("Event Stop Cnt Is Under 0");                     /* 309 */
    }
}

void IngameEventFileDispReq(int flg)
{
    if (flg) {                                                     /* 320 */
        phase_change_reqs.event_stop_cnt++;   /* 321 */
    } else {
        phase_change_reqs.event_stop_cnt--;   /* 323 */
    }

    if (300 < (short)phase_change_reqs.event_stop_cnt) {                /* 327 */
        PRINT_WARNING("Event Stop Cnt Is Over 300");                    /* 328 */
    }
    else if ((short)phase_change_reqs.event_stop_cnt < 0) {                  /* 330 */
        phase_change_reqs.event_stop_cnt = 0;
        PRINT_WARNING("Event Stop Cnt Is Under 0");                     /* 331 */
    }
}

void SendIngameGameOver(int flg)
{
    phase_change_reqs.game_over = flg;                                  /* 343 */
}

void SendIngameGameOverPre(int flg)
{
    phase_change_reqs.game_over_pre = flg;                              /* 348 */
}

void SendIngameEndingNormal(int flg)
{
    phase_change_reqs.ending_normal = flg;                              /* 352 */
}

void SendIngameEndingHard(int flg)
{
    phase_change_reqs.ending_hard = flg;                                /* 356 */
}

void SetIngameDamageMode(int flg)
{
    phase_change_reqs.plyr_damage = flg;                                /* 360 */
}

void SetIngameDoorMode(int flg)
{
    phase_change_reqs.plyr_door = flg;                                  /* 364 */
}

void SendIngameEventLoadEndFlg(int flg)
{
    phase_change_reqs.event_load = flg;                                 /* 368 */
}

/* Empty in the ROM too -- eight bytes, jr ra plus an idle delay slot.  The
 * callers survive from an earlier design where an event mode flag existed. */
void SetIngameEventModeFlg(int flg)
{                                                                       /* 373 */
}

void SetIngameEffectModeTime(int time)
{
    phase_change_reqs.effect_mode_time = (short)time;                   /* 376 */
}

void SetIngameMenuMode(int flg)
{
    phase_change_reqs.menu = flg;                                       /* 380 */
}

void SetIngameMapMode(int flg)
{
    phase_change_reqs.map = flg;                                        /* 384 */
}

void SetIngamePauseMode(int flg)
{
    phase_change_reqs.pause = flg;                                      /* 388 */
}

void SetIngameDbgMenu(int flg)
{
    phase_change_reqs.dbg_menu = flg;                                   /* 393 */
}

void SetIngameEneDead(int flg)
{
    phase_change_reqs.ene_dead = flg;                                   /* 398 */
}

void SetIngamePhoto(int flg)
{
    phase_change_reqs.photo = flg;                                      /* 402 */
}

void SetIngameMovieRoomMenu(int flg)
{
    phase_change_reqs.movie_room_menu = flg;                            /* 406 */
}

/* These three carry no line-number stabs at all, unlike every accessor above
 * them, so no trailing line annotation can be trusted here.  They sit between
 * 406 and 419 in the file. */
void SetIngameMission(int flg)
{
    OutPhaseChangeFlg.mission = flg;
}

int CheckIngameMission(void)
{
    return OutPhaseChangeFlg.mission;
}

void ResetOutReqFlg(void)
{
    /* One `sh zero` in the ROM: the whole 2-byte flag word, not a
     * read-modify-write of the single bit. */
    memset(&OutPhaseChangeFlg, 0, sizeof(OutPhaseChangeFlg));
}

GPHASE_ID_ENUM IngameDecideNextPhase(void)
{                                                                       /* 419 */
    if (phase_change_reqs.game_over_pre) {                              /* 421 */
        if (OutPhaseChangeFlg.mission) {                                /* 423 */
            MisDispDeleteFlg(3);                                        /* 424 */
        }

        /* Both paths land on the same phase -- only the mission variant has
         * the timer panel to tear down first. */
        return GID_STORY_GAME_OVER_PRE;                                 /* 426 */
    }

    if (phase_change_reqs.game_over) {                                  /* 428 */
        if (OutPhaseChangeFlg.mission) {                                /* 430 */
            MisSetClearType(0);                                         /* 432 */
            return GID_STORY_MISSION_RESULT;                            /* 433 */
        }

        return GID_STORY_GAME_OVER;
    }

    if (phase_change_reqs.ending_normal) {                              /* 439 */
        if (OutPhaseChangeFlg.mission) {                                /* 441 */
            SendIngameEndingNormal(0);                                  /* 442 */
            return GID_STORY_MISSION_RESULT;                            /* 443 */
        }

        return GID_ENDING_NORMAL1;
    }

    if (phase_change_reqs.ending_hard) {                                /* 449 */
        return GID_ENDING_HARD;
    }

    if (phase_change_reqs.scene_no != -1) {                             /* 453 */
        return IngameSceneInit(phase_change_reqs.scene_no);             /* 454 */
    }

    if (phase_change_reqs.event_stop_cnt) {                             /* 458 */
        return GID_EVENTMSG_DISP;
    }

    if (phase_change_reqs.ene_dead) {                                   /* 461 */
        return GID_STORY_ENE_DEAD;
    }

    if (phase_change_reqs.menu) {                                       /* 465 */
        return GID_STORY_MENU;
    }

    if (phase_change_reqs.map) {                                        /* 469 */
        return GID_STORY_MAP;
    }

    if (phase_change_reqs.pause) {                                      /* 473 */
        if (OutPhaseChangeFlg.mission) {                                /* 475 */
            return GID_STORY_PAUSE_MISSION;
        } else {
            return GID_STORY_PAUSE;                                     /* 478 */
        }
    }

    if (phase_change_reqs.plyr_damage) {                                /* 481 */
        return GID_STORY_DAMAGE;
    }

    if (phase_change_reqs.plyr_door) {                                  /* 485 */
        return GID_STORY_DOOR_OPEN;
    }

    if (phase_change_reqs.effect_mode_time >= 0) {                      /* 489 */
        return GID_STORY_EFFECT;
    }

    if (phase_change_reqs.photo) {                                      /* 493 */
        return GID_STORY_PHOTO;
    }

    if (phase_change_reqs.movie_room_menu) {                            /* 497 */
        return GID_STORY_MOVIE_ROOM_SEL;
    }

    if (phase_change_reqs.dbg_menu) {                                   /* 501 */
        return GID_STORY_DEBUG;
    }

    return GID_STORY_NORMAL;                                            /* 507 */
}

void init_Story_NowLoading(void)
{
    StreamAutoAllStop();                                                /* 519 */
    LoadingCtrlInit();                                                  /* 522 */
}

GPHASE_ENUM pre_Story_NowLoading(GPHASE_ENUM dummy)
{
    return GPHASE_CONTINUE;                                             /* 526 */
}

GPHASE_ENUM after_Story_NowLoading(GPHASE_ENUM result)
{
    LoadingCtrlMain();                                                  /* 531 */
    LoadingDispMain();                                                  /* 533 */
    return GPHASE_CONTINUE;                                             /* 535 */
}

void end_Story_NowLoading(void)
{
    TitleMemFree();                                                     /* 541 */
    ReleaseTecmoLogoTexMem();                                           /* 543 */
    ReleaseProjectLogoTexMem();                                         /* 545 */
    ReleaseLoadingTexMem();                                             /* 548 */
}

void init_Story_Load_Mission(void)
{
    before_game_load_wait = 0;                                          /* 556 */
}

void end_Story_Load_Mission(void)
{                                                                       /* 561 */
}

GPHASE_ENUM one_Story_Load_Mission(GPHASE_ENUM dummy)
{
    if (InitBeforeGame() != 0)                                          /* 564 */
    {
        SetNextGPhase(GID_STORY_LOAD_MISSION_EVENT);                    /* 565 */
    }
    return GPHASE_CONTINUE;                                             /* 567 */
}

void init_Story_Load_Mission_Save(void)
{
    load_game_step = 0;                                                 /* 577 */
    before_game_load_wait = 0;                                          /* 578 */
}

void end_Story_Load_Mission_Save(void)
{                                                                       /* 583 */
}

/* Note the annotation runs backwards across the two arms: the ROM writes the
 * clear_save_flg != 0 case first (588-595) and the load-from-card case second
 * (600-626).  The branch order below is the compiled order, which is inverted
 * against the source; the line numbers are the honest record of which arm is
 * which. */
GPHASE_ENUM one_Story_Load_Mission_Save(GPHASE_ENUM dummy)
{
    if (ingame_wrk.clear_save_flg) {
        if (load_game_step == 0x0) {
            ClearBeforeGameInit();
            load_game_step = 0x1;
        }
        else if (load_game_step == 0x1) {
            if (InitBeforeGame() != 0x0) {
                SetNextGPhase(GID_STORY_LOAD_MISSION_EVENT);
                ingame_wrk.clear_save_flg = 0;
            }
        }
    }
    else {
        if (load_game_step == 0x0) {
            if (InitBeforeGame() != 0x0)
            {
                ev_seChangeRoom(GetPlyrAreaNo());
                ev_sisChangeRoom(GetPlyrAreaNo());
                ev_eneChangeRoom(GetPlyrAreaNo());
                MapLoadInit(GetPlyrAreaNo());
                map_reverbAfterMCLoadInit();

                load_game_step = 0x1;
            }
        }
        else if (load_game_step == 0x1) {


            if (MapLoadMain() == 0x0) {
                printf("**************Load End!!***************\n");






                EventMacroLoadInit();

                FadeInReq(0, 0, 0, 0);
                SetNextGPhase(GID_STORY_NORMAL);
            }
        }
    }

    return GPHASE_CONTINUE;
}

void init_Story_Load_Mission_Event(void)
{
    EventRootStart();                                                   /* 641 */
    SendIngameEventLoadEndFlg(0);                                       /* 642 */
}

void end_Story_Load_Mission_Event(void)
{                                                                       /* 646 */
}

GPHASE_ENUM one_Story_Load_Mission_Event(GPHASE_ENUM dummy)
{
    EventMain();                                                        /* 649 */
    if (phase_change_reqs.event_load != 0)                              /* 651 */
    {
        SetNextGPhase(GID_STORY_NORMAL);                                /* 659 */
    }

    return GPHASE_CONTINUE;                                             /* 661 */
}

void init_Story_Main(void)
{
    IngameSceneReq(-1);                                                 /* 672 */
    phase_change_reqs.event_stop_cnt = 0;                               /* 674 */
    SetIngameDamageMode(0);                                             /* 675 */
    SetIngameDoorMode(0);                                               /* 677 */
    SendIngameGameOver(0);                                              /* 678 */
    SendIngameGameOverPre(0);                                           /* 679 */
    SetIngameEffectModeTime(-1);                                        /* 680 */
    SetIngameMenuMode(0);                                               /* 681 */
    SetIngameMapMode(0);                                                /* 682 */
    SetIngamePauseMode(0);                                              /* 683 */
    SetIngameEventModeFlg(0);                                           /* 684 */
    SetIngameEneDead(0);                                                /* 685 */
    SetIngamePhoto(0);                                                  /* 686 */
    SetIngameMovieRoomMenu(0);                                          /* 687 */
    SetIngameDbgMenu(0);                                                /* 689 */
}

void end_Story_Main(void)
{                                                                       /* 694 */
    printf("ingame_end Release\n");                               /* 695 */
    map_bgmRelease(1);                                       /* 696 */
    foot_seRelease();                                                   /* 697 */
    ev_seRelease();                                                     /* 698 */
    ev_sisRelease();                                                    /* 699 */
    ev_eneRelease();                                                    /* 700 */
    ReleasePlayer();                                                    /* 701 */
    photo_datObjRelease();                                              /* 702 */
    m_plyr_camera.Release();                                            /* 703 */
    ol_load.Clear(ENE_ACT01_OBJ);                                 /* 706 */
    EvDisp2DEndRelease();                                               /* 709 */
    EvChapterDispEndRelease();                                          /* 710 */
    ReleasePlyrMdl();                                                   /* 713 */
    EffectReleaseButterflyModel();                                      /* 716 */
    EffectSndEnd();                                                     /* 719 */
    EneAllRelease();                                                    /* 722 */
    MapDoorRelease();                                                   /* 724 */
    MapDrawDeleteRoomAll();                                             /* 727 */
    EventEnd();                                                         /* 730 */
    movie_projecterStop();                                              /* 733 */
}

GPHASE_ENUM pre_Story_Main(GPHASE_ENUM dummy)
{
    release_typeClear();                                                /* 741 */
    ol_load.Main();                                                     /* 743 */
    return GPHASE_CONTINUE;                                             /* 745 */
}

GPHASE_ENUM after_Story_Main(GPHASE_ENUM result)
{
    return GPHASE_CONTINUE;                                             /* 750 */
}

void IngameCameraMain(void)
{
    CameraMain();                                                       /* 780 */

    /* Port addition -- the live free camera.  It runs after CameraMain() so
     * it overrides whatever the map / event / finder camera just placed, and
     * before SetIngameListnerInfo() so 3D sound follows the eye rather than
     * the camera the game thinks it has.  It is inert unless the debug menu's
     * FREE CAMERA (or CAMERA DEBUG) switch is up.
     *
     * The pad is only handed over when the debug menu is closed: DrawDbgMenu()
     * uses L2/R1/R2 as its own edit modifiers, which are the free camera's
     * speed and lift keys. */
    DebugCameraMenuMain(phase_change_reqs.dbg_menu == 0);

    SetIngameListnerInfo();                                             /* 784 */
}

void IngameDrawSub(void)
{
    int area_no;
    int floor;

    if (phase_change_reqs.dbg_menu == 0)                                /* 789 */
    {
        area_no = GetPlyrAreaNo();
        MapFogProc(area_no, (int)(short)plyr_wrk.cmn_wrk.floor,
                   plyr_wrk.cmn_wrk.mbox.pos);                          /* 795 */
    }
    else
    {
        area_no = GetPlyrAreaNo();
        floor = GetPlyrFloor();
        MapFogDbProc(area_no, floor, plyr_wrk.cmn_wrk.mbox.pos);        /* 797 */
    }

    gra3dDraw();                                                        /* 800 */
    movie_projecterDraw();                                              /* 803 */
    InitEffectsEF();                                                    /* 808 */
    EffectControl(5);                                                   /* 810 */
    EffectControl(7);                                                   /* 811 */
    BrightnessAdjustmentFilterDraw();                                   /* 814 */
    EffectControl(8);                                                   /* 816 */
    m_plyr_camera.Draw();                                               /* 819 */
    FadeMain();                                                         /* 821 */
    CallVibrate();                                                      /* 823 */
    EvDispMain();                                                       /* 825 */
    MisDispTimeProc();                                                  /* 827 */
    ScreenSaverDraw();                                                  /* 830 */
}                                                                       /* 832 */

void init_Story_Normal(void)
{
    StreamAutoAllRestart();                                             /* 841 */
    SndBufAllRestart();                                                 /* 842 */
}

void end_Story_Normal(void)
{                                                                       /* 846 */
}

GPHASE_ENUM one_Story_Normal(GPHASE_ENUM dummy)
{
    movie_projecterWork();                                              /* 849 */

    if (DebugCameraMenu.CameraDebugON == 0)                             /* 853 */
    {
        PlayerMainCmn(1);                                               /* 854 */
        PlyrNormalCtrl();                                               /* 859 */
    }

    photo_datObjMain();                                                 /* 866 */
    fene_entry.Work();                                                  /* 869 */
    SisterMain();                                                       /* 873 */

    if (phase_change_reqs.photo == 0)                                   /* 876 */
    {
        EnemyMain();                                                    /* 877 */
    }

    map_bgmMain();                                                      /* 879 */
    map_reverbMain();                                                   /* 880 */

    int ret = GetPlyrAreaNo();
    MhCtlMain(ret);                                                     /* 885 */
    IngameCameraMain();                                                 /* 889 */
    ret = IsPlayerInBattle();                                           /* 893 */

    /* Expands the n_plyr_camera.h inline (lines 66/67) at both arms. */
    m_plyr_camera.SetBattleFlg(ret != 0);

    m_plyr_camera.Main();                                               /* 898 */
    PlayData_PlayTimeCount();                                           /* 902 */
    RoomInCheckMain();                                                  /* 905 */
    EventMain();                                                        /* 908 */

    /* The test reads the pre-increment value, so the timer saturates one frame
     * after it would otherwise pass 10. */
    bool over_pause_lock = 10 < iPauseLockTimer;                        /* 912 */
    iPauseLockTimer++;

    if (over_pause_lock)
    {
        iPauseLockTimer = 10;                                           /* 913 */
    }

    ret = InFinderMode();                                               /* 918 */
    if (u_int free_size; (((ret == 0) && (ret = ingame_wrk.MenuIsLocked(), ret == 0)) &&
                          (*paddat[5] == 1)) &&
                         ((7 < *pushdat[5] && (free_size = mem_utilQueryTotalFreeSize(), 0x2a23ff < free_size))))
    {
        SystemBankPlay(3, 1, 0, 0, (SND_3D_SET *)nullptr, 0x3200, 0x1000);   /* 923 */
        SetIngameMenuMode(1);                                           /* 924 */
    }
    else
    {
        ret = ingame_wrk.PauseIsLocked();                   /* 927 */

        if (((ret == 0) && (9 < iPauseLockTimer)) &&
           ((*paddat[7] == 1 || (ret = padIsConnected(0), ret == 0))))
        {
            SetIngamePauseMode(1);                                      /* 929 */
        }
    }

    if (*key_now[0xd] == 1)                                             /* 933 */
    {
        SetIngameDbgMenu(1);                                            /* 934 */
    }

    GPHASE_ID_ENUM id = IngameDecideNextPhase();                        /* 947 */
    if (id != GID_STORY_NORMAL)
    {
        SetNextGPhase(id);                                              /* 950 */
    }

    ItemDbg_PlyrItemLink();                                             /* 953 */
    EnemyMotionWork();                                                  /* 955 */
    sis_mdlMotionWork();                                                /* 956 */
    plyr_mdlMotionWork();                                               /* 957 */
    IgEffectMain();                                                     /* 960 */
    IngameDrawSub();                                                    /* 975 */
    SubTitleMain(1);                                                    /* 981 */
    CheckEneDepth();                                                    /* 984 */

    if (((pad[1].now & 0x100U) != 0) && ((pad[1].one & 0x800U) != 0))   /* 1005 */
    {
        SetNextGPhase(GID_ENDING_NORMAL1);                              /* 1007 */
    }

    return GPHASE_CONTINUE;                                             /* 1025 */
}

void init_Story_Game_Over_Pre(void)
{
    SetPlyrFinderQEnd();                                                /* 1031 */
}

void end_Story_Game_Over_Pre(void)
{                                                                       /* 1035 */
}

GPHASE_ENUM one_Story_Game_Over_Pre(GPHASE_ENUM dummy)
{
    int ret;
    GPHASE_ID_ENUM id;

    movie_projecterWork();                                              /* 1038 */
    photo_datObjMain();                                                 /* 1039 */
    PlayerMainCmn(1);                                                   /* 1041 */
    PlyrNormalCtrl();                                                   /* 1042 */
    SisterMain();                                                       /* 1043 */
    EnemyMain();                                                        /* 1044 */
    map_bgmMain();                                                      /* 1046 */
    map_reverbMain();                                                   /* 1047 */
    ret = GetPlyrAreaNo();
    MhCtlMain(ret);                                                     /* 1049 */
    IngameCameraMain();                                                 /* 1051 */
    ret = IsPlayerInBattle();                                           /* 1052 */
    m_plyr_camera.SetBattleFlg(ret != 0);
    m_plyr_camera.Main();                                               /* 1057 */
    PlayData_PlayTimeCount();                                           /* 1059 */
    RoomInCheckMain();                                                  /* 1061 */
    EnemyMotionWork();                                                  /* 1064 */
    sis_mdlMotionWork();                                                /* 1065 */
    plyr_mdlMotionWork();                                               /* 1066 */
    IgEffectMain();                                                     /* 1067 */
    IngameDrawSub();                                                    /* 1069 */

    ret = PlayerGameOver();                                             /* 1072 */
    if (ret != 0) {
        SendIngameGameOverPre(0);                                       /* 1073 */
        SendIngameGameOver(1);                                          /* 1074 */
        id = IngameDecideNextPhase();                                   /* 1075 */
        SetNextGPhase(id);
    }

    ret = SisterGameOver();                                             /* 1077 */
    if (ret != 0) {
        SendIngameGameOverPre(0);                                       /* 1078 */
        SendIngameGameOver(1);                                          /* 1079 */
        id = IngameDecideNextPhase();                                   /* 1080 */
        SetNextGPhase(id);
    }

    return GPHASE_CONTINUE;                                             /* 1083 */
}

/* Both bodies are entirely inlines out of common/variable.h and hp_bar.h, so
 * the only line numbers the ROM records here (342, 343, 421, 426, 427) belong
 * to those headers, not to ingame.c.  No trailing annotation is therefore
 * possible for either function.
 *
 * These used to be written as direct stores into CHpBar; they are the same two
 * expansions CNPlyrCamera::FinderIn() and ::FinderOut() produce, which is what
 * identified them as the class's own FadeIn() / FadeOut() -- CHpBar's members
 * are private in the ROM's debug info, so no caller can reach them. */
void init_Story_Damage(void)
{
    m_plyr_camera.hp.FadeIn();
}

void end_Story_Damage(void)
{
    m_plyr_camera.hp.FadeOut();
}

GPHASE_ENUM one_Story_Damage(GPHASE_ENUM dummy)
{
    int ret;
    GPHASE_ID_ENUM id;

    if (DebugCameraMenu.CameraDebugON == 0) {                           /* 1101 */
        PlayerMainCmn(0);                                               /* 1103 */
        PlyrMotionMovement();                                           /* 1104 */
    }
    ret = PlyrDamageCtrl();                                             /* 1105 */
    if (ret != 0) {
        SetIngameDamageMode(0);                                         /* 1114 */
    }
    SisterMain();                                                       /* 1116 */
    EnemyMain();                                                        /* 1119 */
    map_bgmMain();                                                      /* 1121 */
    map_reverbMain();                                                   /* 1123 */
    ret = GetPlyrAreaNo();
    MhCtlMain(ret);                                                     /* 1124 */
    IngameCameraMain();                                                 /* 1127 */
    ret = IsPlayerInBattle();                                           /* 1130 */
    m_plyr_camera.SetBattleFlg(ret != 0);                               /* 1133 */
    m_plyr_camera.Main();                                               /* 1138 */
    PlayData_PlayTimeCount();                                           /* 1141 */
    ItemDbg_PlyrItemLink();                                             /* 1143 */
    EnemyMotionWork();                                                  /* 1145 */
    sis_mdlMotionWork();                                                /* 1146 */
    plyr_mdlMotionWork();                                               /* 1147 */
    IgEffectMain();                                                     /* 1149 */
    IngameDrawSub();                                                    /* 1153 */

    if (phase_change_reqs.plyr_damage == 0) {                           /* 1156 */
        id = IngameDecideNextPhase();                                   /* 1157 */
        SetNextGPhase(id);
    }

    return GPHASE_CONTINUE;                                             /* 1161 */
}

void init_Story_Door_Open(void)
{
    float tv[4];

    memset(tv, 0, sizeof(tv));                                          /* 1166 */
    tv[3] = 1.0f;                                                       /* 1167 */
    SetPlyrNeckFlg(0);                                                  /* 1170 */
    plyr_wrk.spd[0] = 0.0f;                                             /* 1171 */
    plyr_wrk.spd[2] = 0.0f;                                             /* 1172 */
    plyr_wrk.spd[1] = 0.0f;
    PlyrPosSet((MOVE_BOX *)&plyr_wrk, tv);                              /* 1174 */
    movie_projecterStop();                                              /* 1175 */
    playerUseDoorLight(1);
}

void end_Story_Door_Open(void)
{
    SetPlyrNeckFlg(1);                                                  /* 1178 */
}                                                                       /* 1179 */

GPHASE_ENUM one_Story_Door_Open(GPHASE_ENUM dummy)
{
    int ret;
    GPHASE_ID_ENUM id;

    if (DebugCameraMenu.CameraDebugON == 0) {                           /* 1183 */
        PlayerMainCmn(1);                                               /* 1185 */
        ret = GetPlyrDoorMoveFlg();                                     /* 1186 */
        if (ret != 0) {
            PlyrMotionMovement();                                       /* 1187 */
        }
    }
    ret = PlyrOpenDoor();                                               /* 1188 */
    if (ret != 0) {
        SetIngameDoorMode(0);                                           /* 1198 */
        id = IngameDecideNextPhase();                                   /* 1199 */
        SetNextGPhase(id);
    }
    SisterMain();                                                       /* 1201 */
    EnemyDoorMain();                                                    /* 1204 */
    map_bgmMain();                                                      /* 1207 */
    map_reverbMain();                                                   /* 1209 */
    ret = GetPlyrAreaNo();
    MhCtlMain(ret);                                                     /* 1210 */
    IngameCameraMain();                                                 /* 1213 */
    PlayData_PlayTimeCount();                                           /* 1217 */
    EventMain();                                                        /* 1220 */
    ItemDbg_PlyrItemLink();                                             /* 1222 */
    EnemyMotionWork();                                                  /* 1224 */
    sis_mdlMotionWork();                                                /* 1226 */
    plyr_mdlMotionWork();                                               /* 1227 */
    IgEffectMain();                                                     /* 1228 */
    IngameDrawSub();                                                    /* 1230 */
    SubTitleMain(1);                                                    /* 1233 */
    return GPHASE_CONTINUE;                                             /* 1235 */
}                                                                       /* 1238 */

void init_Story_Ene_Dead(void)
{                                                                       /* 1245 */
}

void end_Story_Ene_Dead(void)
{                                                                       /* 1248 */
}

/* Draws its own tail rather than calling IngameDrawSub(): no fog-debug branch,
 * no movie projecter, no camera/HUD pass. */
GPHASE_ENUM one_Story_Ene_Dead(GPHASE_ENUM dummy)
{
    GPHASE_ID_ENUM id;
    int ret;

    if (DebugCameraMenu.CameraDebugON == 0) {                           /* 1250 */
        PlayerMainCmn(1);                                               /* 1252 */
    }
    EnemyMain();                                                        /* 1253 */
    if (phase_change_reqs.ene_dead == 0) {                              /* 1258 */
        id = IngameDecideNextPhase();                                   /* 1259 */
        SetNextGPhase(id);
    }
    ret = GetPlyrAreaNo();
    MhCtlMain(ret);                                                     /* 1261 */
    IngameCameraMain();                                                 /* 1265 */
    PlayData_PlayTimeCount();                                           /* 1268 */
    ItemDbg_PlyrItemLink();                                             /* 1271 */
    EnemyMotionWork();                                                  /* 1273 */
    sis_mdlMotionWork();                                                /* 1275 */
    plyr_mdlMotionWork();                                               /* 1276 */
    IgEffectMain();                                                     /* 1277 */
    ret = GetPlyrAreaNo();
    MapFogProc(ret, (int)(short)plyr_wrk.cmn_wrk.floor,
               plyr_wrk.cmn_wrk.mbox.pos);                              /* 1279 */
    gra3dDraw();                                                        /* 1283 */
    InitEffectsEF();                                                    /* 1285 */
    EffectControl(5);                                                   /* 1287 */
    EffectControl(7);                                                   /* 1288 */
    BrightnessAdjustmentFilterDraw();                                   /* 1291 */
    EffectControl(8);                                                   /* 1293 */
    FadeMain();                                                         /* 1296 */
    CallVibrate();                                                      /* 1298 */
    return GPHASE_CONTINUE;                                             /* 1300 */
}                                                                       /* 1303 */

void init_Story_Debug(void)
{                                                                       /* 1313 */
}

void end_Story_Debug(void)
{                                                                       /* 1316 */
}

GPHASE_ENUM one_Story_Debug(GPHASE_ENUM dummy)
{
    int ret;
    GPHASE_ID_ENUM id;

    ret = GetPlyrAreaNo();
    MhCtlMain(ret);                                                     /* 1321 */
    IngameCameraMain();                                                 /* 1324 */
    ItemDbg_PlyrItemLink();                                             /* 1326 */
    IngameDrawSub();                                                    /* 1333 */
    ret = DrawDbgMenu();                                                /* 1338 */
    if (ret == 0) {
        if (DebugCameraMenu.CameraDebugON != 0) {                       /* 1339 */
            SetNextGPhase(GID_STORY_DEBUG_CAM);                         /* 1341 */
        }
    } else {
        SetIngameDbgMenu(0);                                            /* 1343 */
        id = IngameDecideNextPhase();                                   /* 1344 */
        SetNextGPhase(id);
    }
    return GPHASE_CONTINUE;                                             /* 1346 */
}

void init_Story_Debug_Cam(void)
{                                                                       /* 1357 */
}

void end_Story_Debug_Cam(void)
{                                                                       /* 1360 */
}

GPHASE_ENUM one_Story_Debug_Cam(GPHASE_ENUM dummy)
{
    int area_no;

    area_no = GetPlyrAreaNo();
    MhCtlMain(area_no);                                                 /* 1365 */

    /* Port addition, at the ROM's own hole.  Lines 1366..1373 emit no code in
     * this build -- camera_menu.o was compiled down to its three flags and
     * nothing else -- and this phase runs no CameraMain(), so nothing else
     * would ever move the camera here.  The pad is always ours: the debug menu
     * belongs to GID_STORY_DEBUG, which this phase has already left. */
    DebugCameraMenuMain(1);

    SetIngameListnerInfo();                                             /* 1374 */
    if (*key_now[0xd] == 1) {                                           /* 1377 */
        DebugCameraMenu.CameraDebugON = 0;                              /* 1378 */
        SetNextGPhase(GID_STORY_DEBUG);                                 /* 1379 */
    }
    IngameDrawSub();                                                    /* 1387 */
    return GPHASE_CONTINUE;                                             /* 1395 */
}

void init_Story_Pause(void)
{
    PauseInit();                                                        /* 1405 */
}

void end_Story_Pause(void)
{
    int ret;

    iPauseLockTimer = 0;                                                /* 1408 */
    ret = movie_projecterIsReq();                                       /* 1409 */
    if (ret == 0) {                                                     /* 1411 */
        LocalCopyLtoL(1, (int)(((sys_wrk.count + 1) & 1) * INGAME_FRAME_BUF_ADRS),
                      INGAME_CAPTURE_ADRS);                             /* 1412 */
    }
}

GPHASE_ENUM one_Story_Pause(GPHASE_ENUM dummy)
{
    int ret;

    /* A switch in the ROM: GCC's binary search over the three cases is what a
     * decompiler renders as nested ifs plus a goto past the shared tail. */
    ret = PauseMain();                                                  /* 1417 */
    switch (ret) {                                                      /* 1420 */
    case 1:                                                             /* 1422 */
        SetIngamePauseMode(0);
        SetNextGPhase(IngameDecideNextPhase());                         /* 1424 */
        break;

    case 2:                                                             /* 1425 */
        SetNextGPhase(GID_TITLE_TOP);
        break;

    case 3:                                                             /* 1427 */
        SetNextGPhase(GID_STORY_DEBUG);
        break;
    }

    PauseDispMain();                                                    /* 1431 */
    return GPHASE_CONTINUE;                                             /* 1437 */
}                                                                       /* 1439 */

void IngameLoopSEPause(void)
{
    PlyrSEStop();                                                       /* 1444 */
    photo_datObjFadeOutSE(2);                                           /* 1445 */
    EffectSndAllPause();                                                /* 1446 */
    EvSoundPause();                                                     /* 1447 */
}

void IngameLoopSERestart(void)
{
    EffectSndAllRestart();                                              /* 1451 */
    EvSoundRestart();                                                   /* 1452 */
}

void init_Story_Menu(void)
{
    MenuIn();                                                           /* 1462 */
    IngameLoopSEPause();                                                /* 1464 */
}

void end_Story_Menu(void)
{
    MenuRelease();                                                      /* 1468 */
    IngameLoopSERestart();                                              /* 1469 */
    LocalCopyLtoL(1, (int)(((sys_wrk.count + 1) & 1) * INGAME_FRAME_BUF_ADRS),
                  INGAME_CAPTURE_ADRS);                                 /* 1470 */
}

GPHASE_ENUM one_Story_Menu(GPHASE_ENUM dummy)
{
    int ret;
    GPHASE_ID_ENUM id;

    PlayData_PlayTimeCount();                                           /* 1475 */
    ret = MenuMain();                                                   /* 1478 */
    if (ret != 0) {
        SetIngameMenuMode(0);                                           /* 1479 */
        id = IngameDecideNextPhase();                                   /* 1481 */
        SetNextGPhase(id);
    }
    MenuDispMain();                                                     /* 1484 */
    return GPHASE_CONTINUE;                                             /* 1486 */
}

void init_Story_Map(void)
{
    MapViewInit();                                                      /* 1492 */
}

void end_Story_Map(void)
{
    MenuRelease();                                                      /* 1496 */
    LocalCopyLtoL(1, (int)(((sys_wrk.count + 1) & 1) * INGAME_FRAME_BUF_ADRS),
                  INGAME_CAPTURE_ADRS);                                 /* 1497 */
}

GPHASE_ENUM one_Story_Map(GPHASE_ENUM dummy)
{
    int ret;
    GPHASE_ID_ENUM id;

    PlayData_PlayTimeCount();                                           /* 1502 */
    ret = MenuMain();                                                   /* 1505 */
    if (ret != 0) {
        SetIngameMapMode(0);                                            /* 1506 */
        id = IngameDecideNextPhase();                                   /* 1508 */
        SetNextGPhase(id);
    }
    MenuDispMain();                                                     /* 1511 */
    return GPHASE_CONTINUE;                                             /* 1513 */
}

void init_Story_Mission_St(void)
{
    init_Story_Normal();                                                /* 1522 */
    MisStInit();                                                        /* 1523 */
    MisDispTimeInit();                                                  /* 1524 */
    MisDispDeleteFlg(3);                                                /* 1525 */
    MapObjItemOff();                                                    /* 1526 */
}

void end_Story_Mission_St(void)
{
    MisDispSetFlg(3);                                                   /* 1530 */
    MisStTerm();                                                        /* 1532 */
}                                                                       /* 1534 */

GPHASE_ENUM one_Story_Mission_St(GPHASE_ENUM dummy)
{
    int ret;

    if (DebugCameraMenu.CameraDebugON == 0) {                           /* 1539 */
        PlayerMainCmn(1);                                               /* 1541 */
    }
    photo_datObjMain();                                                 /* 1542 */
    fene_entry.Work();                                                  /* 1548 */
    SisterMain();                                                       /* 1549 */
    map_bgmMain();                                                      /* 1550 */
    map_reverbMain();                                                   /* 1551 */
    ret = GetPlyrAreaNo();
    MhCtlMain(ret);                                                     /* 1552 */
    IngameCameraMain();                                                 /* 1553 */
    PlayData_PlayTimeCount();                                           /* 1554 */
    RoomInCheckMain();                                                  /* 1555 */
    EventMain();                                                        /* 1556 */
    EnemyMotionWork();                                                  /* 1557 */
    sis_mdlMotionWork();                                                /* 1559 */
    plyr_mdlMotionWork();                                               /* 1560 */
    IgEffectMain();                                                     /* 1561 */
    IngameDrawSub();                                                    /* 1562 */
    ret = MisProc();                                                    /* 1563 */
    if (ret < 0) {
        SetNextGPhase(GID_STORY_NORMAL);                                /* 1566 */
    }
    return GPHASE_CONTINUE;                                             /* 1567 */
}                                                                       /* 1570 */

void init_Story_Mission_Result(void)
{
    MisDispDeleteFlg(3);                                                /* 1575 */
    MisEnInit();                                                        /* 1577 */
}                                                                       /* 1578 */

void end_Story_Mission_Result(void)
{
    MapObjItemOn();                                                     /* 1582 */
    MisEnTerm();                                                        /* 1583 */
}

GPHASE_ENUM one_Story_Mission_Result(GPHASE_ENUM dummy)
{
    int ret;

    ret = MisProc();                                                    /* 1588 */
    if (ret < 0) {
        SetNextGPhase(GID_MISSION_SEL);                                 /* 1589 */
    }
    return GPHASE_CONTINUE;                                             /* 1591 */
}

void init_Story_Pause_Mission(void)
{
    MisPauseInit();                                                     /* 1597 */
}

void end_Story_Pause_Mission(void)
{
    iPauseLockTimer = 0;                                                /* 1600 */


    if (movie_projecterIsReq() == 0) {                                                     /* 1603 */
        LocalCopyLtoL(1, (int)(((sys_wrk.count + 1) & 1) * INGAME_FRAME_BUF_ADRS), INGAME_CAPTURE_ADRS);                             /* 1604 */
    }
}

GPHASE_ENUM one_Story_Pause_Mission(GPHASE_ENUM dummy)
{ /* 1609 */
    if (MisPauseMain() == 0)
    {
        MisPauseDispMain();                                             /* 1611 */
    }
    return GPHASE_CONTINUE;                                             /* 1614 */
}

void init_Story_Game_Over(void)
{
    EvDisp2DEndRelease();                                               /* 1622 */
    EvChapterDispEndRelease();                                          /* 1623 */
    StreamAutoAllStop();                                                /* 1625 */
    SndBufAllStopLoopSnd();                                             /* 1626 */
}

GPHASE_ENUM pre_Story_Game_Over(GPHASE_ENUM dummy)
{
    return GPHASE_CONTINUE;                                             /* 1630 */
}

GPHASE_ENUM after_Story_Game_Over(GPHASE_ENUM result)
{
    return GPHASE_CONTINUE;                                             /* 1634 */
}

void end_Story_Game_Over(void)
{
    SendIngameGameOver(0);                                              /* 1637 */
}                                                                       /* 1638 */

void init_Story_Effect(void)
{
    story_effect_time = (int)(short)phase_change_reqs.effect_mode_time;  /* 1653 */
}

void end_Story_Effect(void)
{                                                                       /* 1657 */
}

GPHASE_ENUM one_Story_Effect(GPHASE_ENUM dummy)
{
    MhCtlMain(GetPlyrAreaNo());                                         /* 1661 */


    PlayData_PlayTimeCount();                                           /* 1664 */
    ItemDbg_PlyrItemLink();                                             /* 1666 */

    IgEffectMain();                                                     /* 1668 */


    IngameDrawSub();                                                    /* 1671 */

    if (--story_effect_time < 1) {                                       /* 1675 */
        SetIngameEffectModeTime(-1);                                    /* 1676 */
        SetNextGPhase(IngameDecideNextPhase());
    }

    return GPHASE_CONTINUE;                                             /* 1679 */
}

void init_Story_Puzzle(void)
{
    SoftResetLock();                                                    /* 1687 */
    EnemyAnimLock();                                                    /* 1689 */
}

GPHASE_ENUM pre_Story_Puzzle(GPHASE_ENUM dummy)
{
    return GPHASE_CONTINUE;                                             /* 1693 */
}

GPHASE_ENUM after_Story_Puzzle(GPHASE_ENUM result)
{
    EnemyEffectPosUpdate();                                             /* 1697 */
    return GPHASE_CONTINUE;                                             /* 1698 */
}

void end_Story_Puzzle(void)
{
    EnemyAnimUnlock();                                                  /* 1702 */
    PuzzleRelease();                                                    /* 1705 */
    SoftResetUnlock();                                                  /* 1707 */
}

void init_Story_SavePoint(void)
{
    SetPlyrAnime(0, 10);                                          /* 1713 */
    SisterLock();                                                       /* 1715 */
    if (IsSisWrk() && GetSisStandAnm()) {                               /* 1720 */
        SetSisterAnime(0, 10);                                    /* 1721 */
    }


    SavePointBackGroundLoadReq();                                       /* 1725 */
}

GPHASE_ENUM pre_Story_SavePoint(GPHASE_ENUM dummy)
{
    return GPHASE_CONTINUE;                                             /* 1729 */
}

GPHASE_ENUM after_Story_SavePoint(GPHASE_ENUM result)
{
    return GPHASE_CONTINUE;                                             /* 1733 */
}

void end_Story_SavePoint(void)
{
    SavePointEnd();                                                     /* 1738 */
}

void init_Story_Movie_Room(void)
{                                                                       /* 1743 */
}

void end_Story_Movie_Room(void)
{
    movie_projecterStop();                                              /* 1746 */
}

void init_Story_Movie_Room_Sel(void)
{
    MovieRoomMenuInit();                                                /* 1753 */
    PlayerLock();                                                       /* 1756 */
}

void end_Story_Movie_Room_Sel(void)
{
    MovieRoomMenuEnd();                                                 /* 1761 */
    PlayerUnlock();                                                     /* 1764 */
}

/* The per-frame work every phase that freezes the player still owes: the world
 * keeps simulating and drawing, only the player control call is missing. */
void IngamePlyrNoActJob(void)
{
    movie_projecterWork();                                              /* 1770 */
    PlayerMainCmn(1);                                              /* 1771 */
    photo_datObjMain();                                                 /* 1772 */
    map_bgmMain();                                                      /* 1774 */
    map_reverbMain();                                                   /* 1775 */
    MhCtlMain(GetPlyrAreaNo());                                         /* 1777 */
    IngameCameraMain();                                                 /* 1779 */
    if (IsPlayerInBattle()) {                                           /* 1780 */
        m_plyr_camera.SetBattleFlg(1);
    } else {
        m_plyr_camera.SetBattleFlg(0);
    }
    m_plyr_camera.Main();                                               /* 1785 */
    PlayData_PlayTimeCount();                                           /* 1787 */
    RoomInCheckMain();                                                  /* 1789 */
    EventMain();                                                        /* 1790 */
    EnemyMotionWork();                                                  /* 1792 */
    sis_mdlMotionWork();                                                /* 1793 */
    plyr_mdlMotionWork();                                               /* 1794 */
    IgEffectMain();                                                     /* 1795 */
    IngameDrawSub();                                                    /* 1797 */
}

GPHASE_ENUM one_Story_Movie_Room_Sel(/* a0 4 */ GPHASE_ENUM dummy) {
    IngamePlyrNoActJob();


    if (MovieRoomMenuMain() == 0) {
        SetIngameMovieRoomMenu(0);
        SetNextGPhase(IngameDecideNextPhase());
    }


    MovieRoomMenuDisp();

    return GPHASE_CONTINUE;
}

/* File-local in the ROM -- no global symbol in ingame.o's link-map entry, and
 * every caller lives in this translation unit. */
static void IngameWrkInitNotPlayData(void)
{
    InitMessage();                                                      /* 1820 */
    ingame_wrk.Init();                                                  /* 1821 */
    snd3DSet1Meter(500.0f);                                         /* 1822 */
    InitDrawFLG();                                                      /* 1823 */
    ol_load.Init();                                                     /* 1824 */
    IgEffectInit();                                                     /* 1825 */
    motInitMsn();                                                       /* 1826 */
    MhCtlInit();                                                        /* 1827 */
    InitPlayer();                                                       /* 1828 */
    SetPlayerFloor(11);                                                 /* 1829 */
    sis_mdlInit();                                                      /* 1830 */
    plyr_mdlInit();                                                     /* 1831 */
    InitSister();                                                       /* 1832 */
    InitEnemy();                                                        /* 1833 */
    InitEffects();                                                      /* 1834 */

    if (CheckIngameMission() == 0)                                      /* 1836 */
    {
        InitPhotoWrk();                                                 /* 1837 */
    }
    foot_seInit();                                                      /* 1839 */
    ev_seInit();                                                        /* 1840 */
    ev_eneInit();                                                       /* 1841 */
    ev_sisInit();                                                       /* 1842 */
    photo_datInit();                                                    /* 1843 */
    photo_datObjInit();                                                 /* 1844 */
    EventInit();                                                        /* 1845 */
    GhostSealDoorInit();                                                /* 1846 */
    DoorCtrlInit();                                                     /* 1847 */
    InitSceneWork();                                                    /* 1848 */
    CameraMainInit();                                                   /* 1849 */
    acsChodoInitCloth();                                                /* 1850 */
    MorphInit();                                                        /* 1851 */
    acsInitRopeWork();                                                  /* 1852 */
    MapFogReset();                                                      /* 1853 */
    ChrSortInit();                                                      /* 1854 */
    MapPutResetAll();                                                   /* 1855 */
    gra3dMonotoneDrawEnable(0);                                         /* 1856 */
    map_bgmInit();                                                      /* 1857 */
    map_reverbInit();                                                   /* 1858 */
    MrecInitCameraInfo();                                               /* 1859 */
    PuzzleInit();                                                       /* 1860 */
    MapSavePopFirstDat();                                               /* 1861 */
    MapSaveRegist();                                                    /* 1862 */
    movie_projecterInit();                                              /* 1863 */
    SubTitleInit();                                                     /* 1864 */
    fene_entry.Init();                                                  /* 1865 */
    MhFirstInit();                                                      /* 1866 */
    m_plyr_camera.Init();                                               /* 1867 */
    release_typeInit();                                                 /* 1868 */
    playerUseDoorLight(0);                                            /* 1869 */
}

void InitCostume(void)
{
    SetPlyrMdlNo(0);                                                    /* 1873 */
    SetSisterMdlNo(1);                                                  /* 1874 */
    SetPlyrAcsNo(-1);                                                   /* 1875 */
    SetSisterAcsNo(-1);                                                 /* 1876 */
}                                                                       /* 1877 */

void IngameWrkInit(int chapter_no, int difficulty_label)
{
    ingame_wrk.mChapterNo = chapter_no;
    ingame_wrk.mDifficulty = difficulty_label;
    ingame_wrk.mClearCnt = 0;
    ingame_wrk.clear_save_flg = 0;
    m_plyr_camera.camera_power_up.Init();
    m_plyr_camera.eq_tray.mSave.Init();
    IngameWrkInitNotPlayData();
    PlayData_Init();
    AllPlyrItemInit();
    AllPlyrFileInit();
    PlyrCrystalInit();
    PlyrLevelGemInit();
    PlyrMemoInit();
    PlyrSoulListInit();
    RoomInInfoInit();
}

static void ClearBeforeGameInit(void)
{
    ingame_wrk.mChapterNo = 0;                                          /* 1903 */
    IngameWrkInitNotPlayData();                                         /* 1908 */
    AllPlyrEventItemLost();                                             /* 1911 */
    PlayData_PlayTimeInit();                                            /* 1913 */
}

static int InitBeforeGame() {
    int ret = 0;

    if (before_game_load_wait == 0) {


        ol_load.Req(ENE_ACT01_OBJ);


        EffectSetupButterflyModel();


        plyr_mdlResetReq();

        before_game_load_wait = 1;
    }


    else if (before_game_load_wait == 0x1) {

        if (IsReadyPlyrMdl() && IsLoadEndAll() && EffectIsReadyButterflyModel()) {
            ret = 1;
            SyncHpBar();
        }

    }
    return ret;
}

void SetIngameListnerInfo(void)
{
    sceVu0FVECTOR *ref;
    SND_3D_SET set = { 0 }; /* 1953 */
    sceVu0FVECTOR top = { 0.0f, -1.0f };
    sceVu0FVECTOR btm;

    set.pos = &gra3dcamGetPosition();                                   /* 1956 */
    set.dir = &gra3dcamGetDirection();                                  /* 1959 */
    ref = &gra3dcamGetPositionOld();                                    /* 1960 */
    /* Listener velocity is last frame's position minus this frame's, i.e. the
     * ROM subtracts in that order -- not the other way round. */
    sceVu0SubVector(btm, *ref, *set.pos);                               /* 1966 */
    set.vel = &btm;
    snd3DSetListner(&set, top);                                         /* 1967 */
}                                                                       /* 1971 */

/* The last two functions in ingame.o.  Both descriptors are collected by the
 * memory-card save table.  The ROM stores the target sizes literally (0xc and
 * 0x20); sizeof is used instead because the save is a straight memcpy of the
 * host object, and CFEneEntry in particular is larger here -- its
 * FUYU_GHOST_ONE_DATA pointer is 8 bytes rather than 4. */
void SetSave_IngameWrk(MC_SAVE_DATA *data)
{
    data->addr = (u_char *)&ingame_wrk;                                 /* 1985 */
    data->size = sizeof(INGAME_WRK);                                    /* 1986 */
}

void fene_entrySetSave(MC_SAVE_DATA *data)
{
    data->addr = (u_char *)&fene_entry;                                 /* 1994 */
    data->size = sizeof(fene_entry);                                    /* 1995 */
}
