// FILE: /home/zero_rom/zero2np/src/ingame/map/map_bgm.c
//
// Per-room background music.  map_bgm_tbl[] is the room -> default stream
// file map baked into the disc; map_bgmSave.aMapWrkBGMTbl is the live,
// per-save copy map_bgmInit() seeds from it and the only copy map_bgmMain()
// ever reads on the way to StreamAutoPlayNonReset().  Keeping the two
// separate is what lets map_bgmChangeTbl() override one room's music at
// runtime (an event macro) without touching the disc table other rooms still
// read out of, and what lets that override outlive a save/load.
//
// Reconstructed from the Feb 6 2004 prototype (SLES_523.84), map_bgm.o
// 0x001d8ce0..0x001d9260.

#include "map_bgm.h"

#include "../../graphics/graph3d/ctl/fixed_array.h"
#include "../../system/eeiop/cddat.h"            /* CD_FILE_DAT range checked below */
#include "../../system/eeiop/stream_auto.h"       /* StreamAutoPlayNonReset / Fade* */
#include "../plyr/player.h"                       /* GetPlyrRoomID */

#include <stdio.h>

#define MAP_BGM_ROOM_NUM 240

/* struct _MAP_BGM_DAT (types.txt), 0x4 bytes: the per-room disc default. */
typedef struct
{
    int str_file;
} MAP_BGM_DAT;

/* struct _MAP_BGM_SAVE (types.txt), 0x3c4 bytes: the live per-save copy. */
typedef struct
{
    fixed_array<int, MAP_BGM_ROOM_NUM> aMapWrkBGMTbl;
    int                                disable_cnt;
} MAP_BGM_SAVE;

/* One stream file per room, straight off the disc image.  -1 rooms have no
 * scripted music and stay silent until an event macro sets one. */
static MAP_BGM_DAT map_bgm_tbl[MAP_BGM_ROOM_NUM] =                      /* data 31c2b8 */
{
    { KAN025_MISONO_STR }, { KAN035_MENNIWA_STR }, { KAN028_MURA1_STR },
    { KAN028_MURA1_STR }, { KAN028_MURA1_STR }, { KAN028_MURA1_STR },
    { KAN028_MURA1_STR }, { KAN028_MURA1_STR }, { KAN028_MURA1_STR },
    { KAN028_MURA1_STR }, { KAN028_MURA1_STR }, { KAN028_MURA1_STR },
    { KAN028_MURA1_STR }, { KAN028_MURA1_STR }, { KAN028_MURA1_STR },
    { KAN010_HASINO_UE_STR }, { KAN010_HASINO_UE_STR }, { KAN010_HASINO_UE_STR },
    { KAN010_HASINO_UE_STR }, { KAN020_TYOU_STR }, { KAN021_HIROI_STR },
    { KAN021_HIROI_STR }, { KAN021_HIROI_STR }, { KAN021_HIROI_STR },
    { KAN033_NAZO_STR }, { KAN035_MENNIWA_STR }, { KAN035_MENNIWA_STR },
    { KAN035_MENNIWA_STR }, { KAN029_SANDOU_STR }, { KAN028_MURA1_STR },
    { KAN028_MURA1_STR }, { KAN028_MURA1_STR }, { KAN029_SANDOU_STR },
    { KAN029_SANDOU_STR }, { KAN029_SANDOU_STR }, { KAN028_MURA1_STR },
    { KAN028_MURA1_STR }, { KAN028_MURA1_STR }, { KAN028_MURA1_STR },
    { KAN033_NAZO_STR }, { KAN033_NAZO_STR }, { KAN028_MURA1_STR },
    { KAN028_MURA1_STR }, { KAN028_MURA1_STR }, { KAN028_MURA1_STR },
    { ESE_WASAN_01_STR }, { KAN023_OKUNAI1_STR }, { KAN023_OKUNAI1_STR },
    { KAN023_OKUNAI1_STR }, { KAN004_OUSAKA_STR }, { KAN022_HOSOI_STR },
    { KAN022_HOSOI_STR }, { KAN022_HOSOI_STR }, { KAN022_HOSOI_STR },
    { KAN006_TACHIBANA_STR }, { KAN006_TACHIBANA_STR }, { KAN006_TACHIBANA_STR },
    { KAN044_CHIKA_STR }, { KAN035_MENNIWA_STR }, { KAN004_OUSAKA_STR },
    { KAN004_OUSAKA_STR }, { KAN022_HOSOI_STR }, { KAN021_HIROI_STR },
    { KAN021_HIROI_STR }, { KAN021_HIROI_STR }, { KAN021_HIROI_STR },
    { KAN033_NAZO_STR }, { KAN018_SAE_STR }, { KAN018_SAE_STR },
    { KAN018_SAE_STR }, { KAN018_SAE_STR }, { KAN018_SAE_STR },
    { KAN018_SAE_STR }, { KAN018_SAE_STR }, { KAN036_NANIKA_STR },
    { KAN003_KUREHA_STR }, { KAN036_NANIKA_STR }, { KAN_AME_00_STR },
    { KAN018_SAE_STR }, { KAN018_SAE_STR }, { KAN018_SAE_STR },
    { KAN018_SAE_STR }, { KAN018_SAE_STR }, { (CD_FILE_DAT)-1 },
    { KAN018_SAE_STR }, { KAN018_SAE_STR }, { KAN032_OKYOU_STR },
    { KAN003_KUREHA_STR }, { KAN003_KUREHA_STR }, { KAN003_KUREHA_STR },
    { KAN003_KUREHA_STR }, { KAN003_KUREHA_STR }, { (CD_FILE_DAT)-1 },
    { KAN032_OKYOU_STR }, { KAN021_HIROI_STR }, { KAN021_HIROI_STR },
    { KAN021_HIROI_STR }, { KAN005_KIRYUU_STR }, { KAN_AME_00_STR },
    { KAN_AME_00_STR }, { KAN_AME_00_STR }, { KAN018_SAE_STR },
    { KAN032_OKYOU_STR }, { KAN022_HOSOI_STR }, { KAN006_TACHIBANA_STR },
    { KAN006_TACHIBANA_STR }, { KAN033_NAZO_STR }, { KAN033_NAZO_STR },
    { KAN033_NAZO_STR }, { (CD_FILE_DAT)-1 }, { KAN018_SAE_STR },
    { KAN022_HOSOI_STR }, { KAN022_HOSOI_STR }, { KAN006_TACHIBANA_STR },
    { KAN006_TACHIBANA_STR }, { KAN021_HIROI_STR }, { KAN005_KIRYUU_STR },
    { KAN005_KIRYUU_STR }, { KAN005_KIRYUU_STR }, { KAN023_OKUNAI1_STR },
    { KAN023_OKUNAI1_STR }, { (CD_FILE_DAT)-1 }, { KAN023_OKUNAI1_STR },
    { KAN036_NANIKA_STR }, { KAN005_KIRYUU_STR }, { KAN006_TACHIBANA_STR },
    { KAN006_TACHIBANA_STR }, { KAN006_TACHIBANA_STR }, { KAN006_TACHIBANA_STR },
    { KAN006_TACHIBANA_STR }, { KAN006_TACHIBANA_STR }, { KAN006_TACHIBANA_STR },
    { KAN033_NAZO_STR }, { KAN005_KIRYUU_STR }, { KAN005_KIRYUU_STR },
    { KAN005_KIRYUU_STR }, { KAN035_MENNIWA_STR }, { KAN005_KIRYUU_STR },
    { KAN005_KIRYUU_STR }, { KAN035_MENNIWA_STR }, { KAN033_NAZO_STR },
    { KAN005_KIRYUU_STR }, { KAN005_KIRYUU_STR }, { KAN023_OKUNAI1_STR },
    { KAN023_OKUNAI1_STR }, { KAN023_OKUNAI1_STR }, { (CD_FILE_DAT)-1 },
    { KAN018_SAE_STR }, { KAN028_MURA1_STR }, { KAN005_KIRYUU_STR },
    { KAN005_KIRYUU_STR }, { KAN005_KIRYUU_STR }, { KAN005_KIRYUU_STR },
    { KAN005_KIRYUU_STR }, { KAN005_KIRYUU_STR }, { KAN023_OKUNAI1_STR },
    { KAN023_OKUNAI1_STR }, { KAN004_OUSAKA_STR }, { KAN004_OUSAKA_STR },
    { KAN023_OKUNAI1_STR }, { KAN004_OUSAKA_STR }, { (CD_FILE_DAT)-1 },
    { KAN018_SAE_STR }, { KAN018_SAE_STR }, { KAN018_SAE_STR },
    { KAN018_SAE_STR }, { KAN018_SAE_STR }, { KAN023_OKUNAI1_STR },
    { KAN023_OKUNAI1_STR }, { (CD_FILE_DAT)-1 }, { KAN023_OKUNAI1_STR },
    { KAN006_TACHIBANA_STR }, { KAN006_TACHIBANA_STR }, { KAN006_TACHIBANA_STR },
    { KAN006_TACHIBANA_STR }, { KAN006_TACHIBANA_STR }, { KAN023_OKUNAI1_STR },
    { KAN006_TACHIBANA_STR }, { KAN033_NAZO_STR }, { KAN005_KIRYUU_STR },
    { KAN005_KIRYUU_STR }, { KAN005_KIRYUU_STR }, { KAN035_MENNIWA_STR },
    { KAN005_KIRYUU_STR }, { KAN005_KIRYUU_STR }, { KAN035_MENNIWA_STR },
    { KAN006_TACHIBANA_STR }, { KAN005_KIRYUU_STR }, { KAN023_OKUNAI1_STR },
    { KAN023_OKUNAI1_STR }, { KAN023_OKUNAI1_STR }, { (CD_FILE_DAT)-1 },
    { KAN018_SAE_STR }, { KAN005_KIRYUU_STR }, { KAN005_KIRYUU_STR },
    { KAN005_KIRYUU_STR }, { KAN005_KIRYUU_STR }, { KAN005_KIRYUU_STR },
    { KAN005_KIRYUU_STR }, { KAN005_KIRYUU_STR }, { KAN005_KIRYUU_STR },
    { KAN004_OUSAKA_STR }, { KAN004_OUSAKA_STR }, { KAN023_OKUNAI1_STR },
    { KAN004_OUSAKA_STR }, { (CD_FILE_DAT)-1 }, { KAN018_SAE_STR },
    { KAN018_SAE_STR }, { KAN018_SAE_STR }, { KAN018_SAE_STR },
    { KAN018_SAE_STR }, { KAN006_TACHIBANA_STR }, { KAN006_TACHIBANA_STR },
    { KAN006_TACHIBANA_STR }, { KAN006_TACHIBANA_STR }, { KAN006_TACHIBANA_STR },
    { KAN006_TACHIBANA_STR }, { (CD_FILE_DAT)-1 }, { KAN044_CHIKA_STR },
    { KAN_FUKAMITI_WASAN_STR }, { KAN044_CHIKA_STR }, { KAN044_CHIKA_STR },
    { KAN_FUKAMITI_WASAN_STR }, { KAN044_CHIKA_STR }, { KAN044_CHIKA_STR },
    { KAN_FUKAMITI_WASAN_STR }, { KAN_FUKAMITI_WASAN_STR }, { KAN_FUKAMITI_WASAN_STR },
    { KAN_FUKAMITI_WASAN_STR }, { KAN_FUKAMITI_WASAN_STR }, { KAN_FUKAMITI_WASAN_STR },
    { KAN_FUKAMITI_WASAN_STR }, { KAN021_HIROI_STR }, { KAN_NIEZA_STR },
    { KAN_UTURO_STR }, { KAN_UTURO_STR }, { KAN_UTURO_STR },
    { KAN003_KUREHA_STR }, { KAN002_KUTIKI_STR }, { KAN021_HIROI_STR }
};

static MAP_BGM_SAVE map_bgmSave;                                        /* bss 4b4248 */

static int bgm_act_flg;                                                 /* sdata 3f1a00 */
static int bgm_room_id_save;                                            /* sbss 3f4d80 */
static int bgm_play_id;                                                 /* sbss 3f4d84 */
static int now_str_file;                                                /* sbss 3f4d88 */
static int bgm_now_sector;                                              /* sbss 3f4d8c */

/* Fresh save/room: fold the disc table into the live copy, no override
 * survives a reset. */
void map_bgmInit(void)                                                  /* 50 */
{
    bgm_play_id      = -1;                                              /* 51 */
    bgm_act_flg      = 1;                                                /* 55 */
    bgm_room_id_save = -1;                                              /* 52 */
    now_str_file     = -1;                                              /* 53 */
    map_bgmSave.disable_cnt = 0;                                        /* 56 */
    bgm_now_sector   = 0;                                                /* 59 */

    for (int i = 0; i < MAP_BGM_ROOM_NUM; i++)                          /* 61 */
    {
        map_bgmSave.aMapWrkBGMTbl[i] = map_bgm_tbl[i].str_file;        /* 61 */
    }
}

/* Event-macro entry (EvMapStreamChange).  iStrFileNo outside the BGM range of
 * CD_FILE_DAT [DANMATU_MALE_HXD, BGM_END_DMY) means "clear the override, use
 * the disc default again" rather than a real stream file -- both the
 * too-low and too-high branches reset to map_bgm_tbl[iRoomID]. */
void map_bgmChangeTbl(int iRoomID, int iStrFileNo)                     /* 67 */
{
    if (iStrFileNo < DANMATU_MALE_HXD)                                  /* 69 */
    {
        map_bgmSave.aMapWrkBGMTbl[iRoomID] = -1;
    }
    else if (iStrFileNo < BGM_END_DMY)                                   /* 73 */
    {
        map_bgmSave.aMapWrkBGMTbl[iRoomID] = iStrFileNo;
    }
    else
    {
        map_bgmSave.aMapWrkBGMTbl[iRoomID] = map_bgm_tbl[iRoomID].str_file;
    }
}

/* Starts the room's stream from bgm_now_sector (0 on a fresh switch, the
 * saved position on a fade-in resume).  A working entry outside the BGM
 * range [DANMATU_MALE_HXD, BGM_END_DMY) is "no music for this room" and
 * returns -1 without touching the stream engine. */
static int map_bgmPlayIn(int iNowRoomId, int iInTime)                   /* 84 */
{
    int str_file = map_bgmSave.aMapWrkBGMTbl[iNowRoomId];

    if (str_file < DANMATU_MALE_HXD || BGM_END_DMY <= str_file)          /* 95 */
    {
        return -1;
    }

    return StreamAutoPlayNonReset(str_file, str_file - 1, 0x15, 0, 1,   /* 96 */
                                   0x3200, iInTime, 0x14, (SND_3D_SET *)0,
                                   bgm_now_sector);
}

/* Per-frame poll: if the player's room's working stream file differs from
 * what is currently playing, fade the old one out and start the new one from
 * sector 0.  Returns 1 when a switch happened (or the room ID was invalid),
 * 0 when the room's music was already correct -- map_bgmFadeIn() uses that to
 * tell "still on the right track" apart from "just restarted it". */
static int map_bgmPlaySub(void)                                         /* 102 */
{
    u_int now_room_id = GetPlyrRoomID();

    if (MAP_BGM_ROOM_NUM <= now_room_id)                                 /* 108 */
    {
        printf("ROOM_ID_OVER[%d] : map_bgm.c\n", now_room_id);          /* 109 */
        return 1;                                                        /* 110 */
    }

    if (now_str_file == map_bgmSave.aMapWrkBGMTbl[now_room_id])         /* 117 */
    {
        return 0;
    }

    StreamAutoFadeOut(bgm_play_id, 0x3c);                                /* 118 */
    bgm_now_sector   = 0;                                                 /* 119 */
    bgm_play_id      = map_bgmPlayIn(now_room_id, 0x14);                 /* 120 */
    bgm_room_id_save = now_room_id;                                     /* 122 */
    now_str_file     = map_bgmSave.aMapWrkBGMTbl[now_room_id];           /* 124 */
    return 1;                                                             /* 125 */
}

void map_bgmMain(void)                                                   /* 129 */
{
    if (bgm_act_flg != 0 && map_bgmSave.disable_cnt == 0)                /* 131, 135 */
    {
        map_bgmPlaySub();                                                /* 138 */
    }
}

/* target_vol == 0 stops the room track outright, remembering the sector it
 * was faded out at so a matching map_bgmFadeIn() can resume from there;
 * anything else is a duck, not a stop, and leaves bgm_play_id alone.
 * disable_cnt nests: each call adds one, and map_bgmMain() stays quiet until
 * every matching map_bgmFadeIn() has paid it back down to zero. */
void map_bgmFadeOut(int fade_time, int target_vol)                       /* 142 */
{
    if (bgm_act_flg == 0)                                                 /* 143 */
    {
        return;
    }

    map_bgmSave.disable_cnt++;                                            /* 146 */

    if (target_vol == 0)                                                  /* 149 */
    {
        if (bgm_play_id != -1)                                             /* 151 */
        {
            bgm_now_sector = StreamAutoGetNowSector(bgm_play_id);         /* 152 */
            StreamAutoFadeOut(bgm_play_id, fade_time);                    /* 153 */
            bgm_play_id = -1;                                              /* 155 */
        }
    }
    else
    {
        StreamAutoFade(bgm_play_id, target_vol, fade_time);               /* 156 */
    }
}

/* Fully shuts the room BGM system down (a scene teardown, not a duck) --
 * unlike map_bgmFadeOut() this does not touch disable_cnt and there is no
 * matching re-enable; map_bgmInit() is what turns bgm_act_flg back on. */
void map_bgmRelease(int fade_out_time)                                    /* 161 */
{
    StreamAutoFadeOut(bgm_play_id, fade_out_time);                        /* 162 */
    bgm_act_flg = 0;                                                       /* 163 */
}

/* Undoes one map_bgmFadeOut(): if disable_cnt is still positive after paying
 * it back, something else is still holding the music down and this call is a
 * no-op.  Once it reaches zero, either resume the still-playing stream at
 * full volume (the duck case) or restart the room track from its saved
 * sector (the stopped case) -- map_bgmPlaySub() is tried first because the
 * player may have changed rooms while the music was down, which needs a
 * plain switch rather than a resume. */
void map_bgmFadeIn(int fade_time)                                         /* 167 */
{
    if (bgm_act_flg == 0)                                                  /* 168 */
    {
        return;
    }

    if (map_bgmSave.disable_cnt >= 1)                                     /* 171 */
    {
        map_bgmSave.disable_cnt--;                                        /* 174 */
        if (map_bgmSave.disable_cnt >= 1)                                 /* 175 */
        {
            return;
        }
    }

    if (bgm_play_id == -1)                                                 /* 176 */
    {
        int switched = map_bgmPlaySub();                                   /* 177 */
        if (switched == 0 && (u_int)bgm_room_id_save < MAP_BGM_ROOM_NUM)   /* 178 */
        {
            bgm_play_id = map_bgmPlayIn(bgm_room_id_save, fade_time);      /* 184 */
        }
    }
    else
    {
        StreamAutoFade(bgm_play_id, 0x3200, fade_time);                    /* 187 */
    }
}

/* Whole aMapWrkBGMTbl/disable_cnt block goes into the save verbatim, the same
 * pattern SetSave_DoorCtrl() uses for the door lock table. */
void map_bgmSetSave(MC_SAVE_DATA *data)                                    /* 190 */
{
    data->size = sizeof(map_bgmSave);                                      /* 190 */
    data->addr = (u_char *)&map_bgmSave;                                   /* 191 */
}
