/* ==========================================================================
 *  system/mc/dat/save_data.c
 *
 *  The save manifests, read straight out of the Feb 6 2004 prototype
 *  (SLES_523.84): .data 0x33e3b0 / 0x33e3c0 / 0x33e488 and .sdata 0x3f3c10.
 *  Every entry was resolved from its address through ZERO2.MAP, so the order
 *  below is the ROM's -- and the order matters: it is the byte order of the
 *  card file, so shuffling two entries silently invalidates every save.
 *
 *  Each callback fills in one MC_SAVE_DATA {addr, size}.  A NULL terminates
 *  the list; SetMemoryCardSaveDataInfo() also stops on {NULL, -1}, which is
 *  what it writes into the slot past the last real block.
 *
 *  Reconstructed from the Feb 6 2004 prototype (SLES_523.84).
 * ======================================================================== */

#include "save_data.h"

#include "../../../album/prg/album.h"                        /* SetSave_Album*        */
#include "../../../ingame/clear/prg/clear_flg.h"             /* SetSave_ClearFlg      */
#include "../../../ingame/door/prg/door.h"                   /* SetSave_DoorCtrl      */
#include "../../../ingame/enemy/enemy_dat.h"                 /* release_typeSetSave*  */
#include "../../../ingame/event/prg/ev_change.h"             /* SetSave_EvChangeCtrl  */
#include "../../../ingame/event/prg/ev_ene.h"                /* ev_eneSetSave         */
#include "../../../ingame/event/prg/ev_exe.h"                /* SetSave_EvExeCtrl     */
#include "../../../ingame/event/prg/ev_macro.h"              /* SetSave_EvSave*       */
#include "../../../ingame/event/prg/ev_main.h"               /* SetSave_EvWrk         */
#include "../../../ingame/event/prg/ev_open.h"               /* SetSave_EvCtrlCenter  */
#include "../../../ingame/event/prg/ev_se.h"                 /* ev_seSetSave          */
#include "../../../ingame/event/prg/ev_sis.h"                /* ev_sisSetSave         */
#include "../../../ingame/event/prg/ev_talk.h"               /* SetSave_EvTalkTbl     */
#include "../../../ingame/event/prg/ev_timer.h"              /* SetSave_EvTimerCtrl   */
#include "../../../ingame/ingame.h"                          /* SetSave_IngameWrk     */
#include "../../../ingame/item/prg/crystal.h"                /* SetSave_PlyrCrystal   */
#include "../../../ingame/item/prg/file.h"                   /* SetSave_PlyrFile      */
#include "../../../ingame/item/prg/item.h"                   /* SetSave_PlyrItem      */
#include "../../../ingame/item/prg/level_gem.h"              /* SetSave_PlyrLevelGem  */
#include "../../../ingame/item/prg/memo.h"                   /* SetSave_PlyrMemo      */
#include "../../../ingame/item/prg/soul_list.h"              /* SetSave_PlyrSoulList  */
#include "../../../ingame/map/MapSave.h"                     /* MapSaveCallback       */
#include "../../../ingame/map/map_bgm.h"                     /* map_bgmSetSave        */
#include "../../../ingame/menu/ghost_seal_door.h"            /* SetSave_GhostSealDoor */
#include "../../../ingame/menu/menu_soul.h"                  /* SetSave_ListCompDispFlg */
#include "../../../ingame/menu/play_data.h"                  /* SetSave_PlayData      */
#include "../../../ingame/menu/plyr_room_info.h"             /* SetSave_RoomInInfo    */
#include "../../../ingame/movie_room_menu/prg/movie_projecter.h" /* movie_projecterSetSave */
#include "../../../ingame/photo/m_plyr_camera.h"             /* m_plyr_cameraSetSave* */
#include "../../../ingame/photo/photo_dat.h"                 /* photo_datSetSave      */
#include "../../../ingame/plyr/player.h"                     /* SetSave_PlyrWrk       */
#include "../../../ingame/plyr/plyr_mdl.h"                   /* plyr_mdlSetSave       */
#include "../../../ingame/plyr/sis_mdl.h"                    /* sis_mdlSetSave        */
#include "../../../ingame/plyr/sister.h"                     /* SetSave_Sis*          */
#include "../../../ingame/puzzle/puzzle.h"                   /* SetSave_ClearPuzzle   */
#include "../../../outgame/mission_sel.h"                    /* MissionSelSave        */
#include "../../../outgame/option.h"                         /* SetSave_Option        */
#include "../prg/mc_set_data.h"                              /* SetSave_PlayDataHead  */

/* --------------------------------------------------------------------------
 *  System file.  Written once per save and re-read at boot by autoload.c, so
 *  it holds only what must outlive a slot: the controller/sound options and
 *  the clear flags that unlock costumes and the gallery.
 * ------------------------------------------------------------------------ */
MC_SET_SAVE_FUNC save_system_data[3] =          /* data 33e3b0 */
{
    SetSave_ClearFlg,
    SetSave_Option,
    (MC_SET_SAVE_FUNC)0
};

/* --------------------------------------------------------------------------
 *  Play-data header.  One block: MC_PLAY_DATA_HEAD, the five-slot summary the
 *  load screen lists before any slot is touched.
 * ------------------------------------------------------------------------ */
MC_SET_SAVE_FUNC save_play_data_head[2] =       /* sdata 3f3c10 */
{
    SetSave_PlayDataHead,
    (MC_SET_SAVE_FUNC)0
};

/* --------------------------------------------------------------------------
 *  A save slot: 49 blocks.  Roughly grouped as the ROM has them -- the two
 *  girls, the inventory, the event interpreter's whole state, the door and
 *  room bookkeeping, the camera, and finally the per-module callbacks that
 *  were added late (they sit after the numbered SetSave_* family).
 * ------------------------------------------------------------------------ */
MC_SET_SAVE_FUNC save_game_data[50] =           /* data 33e3c0 */
{
    SetSave_IngameWrk,                          /*  0  chapter / clear count */
    SetSave_PlyrWrk,                            /*  1  Mio                   */
    SetSave_SisWrk,                             /*  2  Mayu                  */
    SetSave_SisTrace,                           /*  3                        */
    SetSave_SisAlgoWrk,                         /*  4                        */
    SetSave_SisMotion,                          /*  5                        */
    SetSave_PlyrItem,                           /*  6  inventory             */
    SetSave_PlyrFile,                           /*  7  notes                 */
    SetSave_PlyrCrystal,                        /*  8                        */
    SetSave_PlyrLevelGem,                       /*  9  camera upgrade gems   */
    SetSave_PlyrMemo,                           /* 10                        */
    SetSave_PlyrSoulList,                       /* 11                        */
    SetSave_ListCompDispFlg,                    /* 12                        */
    SetSave_EvWrk,                              /* 13  event interpreter     */
    SetSave_EvCtrlCenter,                       /* 14                        */
    SetSave_EvExeCtrl,                          /* 15                        */
    SetSave_EvTalkTbl,                          /* 16                        */
    SetSave_EvTimerCtrl,                        /* 17                        */
    SetSave_EvChangeCtrl,                       /* 18                        */
    SetSave_EvSaveStream,                       /* 19                        */
    SetSave_EvSaveObjStream,                    /* 20                        */
    SetSave_EvSavePosStream,                    /* 21                        */
    SetSave_EvSaveEffDither,                    /* 22                        */
    SetSave_EvSaveScreenEffect,                 /* 23                        */
    SetSave_DoorCtrl,                           /* 24  door states           */
    SetSave_PlayData,                           /* 25                        */
    SetSave_PlayTimer,                          /* 26  play time             */
    SetSave_RoomInInfo,                         /* 27                        */
    SetSave_GhostSealDoor,                      /* 28                        */
    ev_seSetSave,                               /* 29                        */
    ev_sisSetSave,                              /* 30                        */
    ev_eneSetSave,                              /* 31                        */
    m_plyr_cameraSetSaveEQ,                     /* 32  camera equip tray     */
    m_plyr_cameraSetSavePowrUp,                 /* 33  camera upgrades       */
    m_plyr_cameraSetSaveFilament,               /* 34  viewfinder needle     */
    m_plyr_cameraSetSaveFilmType,               /* 35  loaded film           */
    sis_mdlSetSave,                             /* 36                        */
    photo_datSetSave,                           /* 37                        */
    release_typeSetSaveJ,                       /* 38  hostile ghost unlocks */
    release_typeSetSaveA,                       /* 39  passive ghost unlocks */
    movie_projecterSetSave,                     /* 40                        */
    SetSave_ClearPuzzle,                        /* 41                        */
    MapSaveCallback,                            /* 42  furniture states      */
    plyr_mdlSetSave,                            /* 43                        */
    map_bgmSetSave,                             /* 44                        */
    ev_gazeSisSetSave,                          /* 45                        */
    MissionSelSave,                             /* 46  mission mode records  */
    fene_entrySetSave,                          /* 47                        */
    CostumeSetSave,                             /* 48  chosen costumes       */
    (MC_SET_SAVE_FUNC)0
};

/* --------------------------------------------------------------------------
 *  A photo album: the album's own info block, then the picture pages.  The
 *  pages are the big one -- 0xe8000 bytes, which is why each album gets its
 *  own card directory rather than sharing the game data one.
 * ------------------------------------------------------------------------ */
MC_SET_SAVE_FUNC save_album_data[3] =           /* data 33e488 */
{
    SetSave_AlbumInfoData,
    SetSave_AlbumData,
    (MC_SET_SAVE_FUNC)0
};
