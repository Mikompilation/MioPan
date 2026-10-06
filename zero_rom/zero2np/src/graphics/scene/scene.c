/* /home/zero_rom/zero2np/src/graphics/scene/scene.c
   Reconstructed from the Feb 6 2004 prototype (SLES_523.84). */

#include "scene.h"
#include "fod.h"
#include "../graph3d/ctl/fixed_array.h"
#include "../../common/heapctrl.h"
#include "../../common/mem_util.h"
#include "../../common/packfile.h"
#include "../graph3d/gra3d.h"
#include "../graph3d/gra3dSGD.h"
#include "../graph3d/gra3dTypes.h"
#include "../graph3d/g3dLightEx.h"
#include "../graph3d/gra3dMisc.h"
#include "../../ingame/map/MapDraw.h"
#include "../../ingame/map/MapLoad.h"
#include "../../ingame/map/MapObj.h"
#include "../../ingame/map/MapSp.h"
#include "../../ingame/map/MapDoor.h"
#include "../../ingame/map/MhCtl.h"
#include "../../ingame/plyr/player.h"
#include "../../ingame/plyr/man_data.h"
#include "../../ingame/ingame.h"
#include "../../ingame/plyr/ChrSort.h"
#include "../../ingame/plyr/unit_ctl.h"          /* GetTrgtRot */
#include "../mmanage.h"
#include "../../graphics/motion/motion.h"
#include "../../graphics/motion/mim.h"
#include "../../graphics/motion/Morph.h"
#include "../../graphics/motion/accessory.h"
#include "../../graphics/graphics.h"
#include "../../graphics/movie/movie_title.h"
#include "../../graphics/effect/effect.h"
#include "../../ingame/ingame_effect.h"           /* IgEffectRenzFlareDispFlgSet */
#include "../../system/eeiop/cddat.h"
#include "../../system/os/eecdvd.h"
#include "../../system/os/system.h"
#include "../../system/eeiop/stream_auto.h"
#include "../../common/utility.h"
#include "../../common/utility2.h"
#include "../graph3d/gra3dSGDData.h"
#include "sdk/libvu0.h"

#include <stdio.h>
#include <string.h>
#include <math.h>

#include "graphics/motion/accessory.h"
#include "../graph3d/g3dxVu0.h"         /* g3dxVu0CopyVector */

/* types.txt: 0x8 -- the prefix is a POINTER to an .rodata string, not an
 * inline char[8]. */
typedef struct                          /* 0x8 */
{
    char *prefix;                       /* 0x0 */
    int   top_no;                       /* 0x4 */
} AREA_PREFIX_TO_NO;

static fixed_array<SCENE_CTRL, 2>          scene_ctrl;
static SCENE_FILE                          scene_file;
static SCENE_LOAD                          scene_load;
static int                                 scn_now_play_id;
static int                                 scn_vib_time0;
static int                                 scn_vib_time1;

static G3DLIGHT                      PlayerLightBackup;

/* Area prefix -> first room number of that area.  Read out of .rodata at
 * 0x3c56b8; the original holds it as a reference_fixed_array in sbss
 * (0x3f4f60) that __static_initialization_and_destruction_0 points at this
 * table, which is what the constructor below reproduces.  The final
 * empty-prefix row is the terminator AreaPrefixToTopNo() scans for -- note it
 * is the EMPTY STRING that ends the scan, not top_no, because row [0] is
 * legitimately top_no 0 and so is the terminator. */
static AREA_PREFIX_TO_NO area_prefix_to_no_tbl[10] =     /* rdata 0x3c56b8 */
{
    { (char *)"ry",   0 },
    { (char *)"ros", 13 },
    { (char *)"rtb", 19 },
    { (char *)"rry", 30 },
    { (char *)"rks", 40 },
    { (char *)"rkh", 55 },
    { (char *)"rkc", 56 },
    { (char *)"rch", 58 },
    { (char *)"rkr", 57 },
    { (char *)"",     0 }
};
static reference_fixed_array<AREA_PREFIX_TO_NO, 10> area_prefix_to_no(area_prefix_to_no_tbl);

static const float SCN_PI         = 3.1415925f;

extern float fod_cmn_mtx[4][4];

/* --------------------------------------------------------------------------
 * Static helpers
 * -------------------------------------------------------------------------- */

static u_int *GetADRTBL(u_int *top, u_int no)                           /* 1994 */
{
    /* PORT DEVIATION: the ROM has no null check -- it is six instructions,
     * a load and an add.  The guard is kept because several callers pass an
     * optional pak address that is legitimately absent here; be aware it turns
     * what would be a fault into a silent NULL. */
    if (top == NULL)
    {
        return NULL;
    }
    return (u_int *)((char *)top + top[no + 4]);                        /* 1997 */
}

/* --------------------------------------------------------------------------
 *  SceneSetHandSpotLightToPlyrWrk
 *
 *  Copy the scene's "hand spot" light (the torch) into the player work light so
 *  the ordinary in-game lighting path picks it up.
 *
 *  The FOD light stores vDirection as the spot's TARGET POINT, not a direction
 *  (fod.c case 2 copies FOD_LIT_SPOT::interest into it), so it is converted in
 *  place to a unit vector.
 *
 *  PORT DEVIATION -- the ROM subtracts the other way round, `vPosition -
 *  vDirection`, giving a vector from the target back TOWARD the light.  That
 *  is the anti-beam sense the VU1's CalcIntens wants, and it is why this site
 *  needs no explicit negation where MapLightSetPlayerReal() has one: the ROM
 *  is consistent, every realtime spot install hands over an anti-beam vector.
 *
 *  The port is consistent the other way.  vDirection is the BEAM engine-wide
 *  -- the authored room spots, the prelight's cone ramp,
 *  _IsBBLightingupSpot()'s bounding-box gate and player.c's own
 *  normalize(forward*fl_z + down*fl_y) all read it that way -- and the four
 *  VU1 kernel transcriptions negate it themselves.  So the operands are
 *  swapped here, making this the third site in the same family as
 *  MapLightSetPlayerReal() and MapDrawRoomOne()'s projector.
 *
 *  Leaving it in the ROM's sense pointed the torch backwards in every
 *  cutscene, because plyr_wrk.fl is the shared carrier: gameplay fills it from
 *  player.c (beam) and scenes overwrite it here (anti-beam), and
 *  MapLightSetPlayerReal() then forwards whichever one is current.
 *  See vu1/LIGHTING.md section 3.3.
 * ------------------------------------------------------------------------ */
static void SceneSetHandSpotLightToPlyrWrk(FOD_LIGHT *pFodLight)        /* 1666 */
{
    if (pFodLight->hand_spot_no != -1)                                  /* 1667 */
    {
        g3dutilCopyLight(&plyr_wrk.fl, &pFodLight->all_lit[pFodLight->hand_spot_no]);

        sceVu0SubVector(plyr_wrk.fl.vDirection,
                        plyr_wrk.fl.vDirection, plyr_wrk.fl.vPosition);  /* 1669 */
        sceVu0Normalize(plyr_wrk.fl.vDirection, plyr_wrk.fl.vDirection); /* 1670 */
        plyr_wrk.fl.Type = G3DLIGHT_SPOT;                                /* 1671 */
        gra3dSetLight(LID_SPOT_FLASHLIGHT, &plyr_wrk.fl);                               /* 1672 */
    }
}

/* --------------------------------------------------------------------------
 *  SceneDoorDoubleBufferProc
 *
 *  Doors are double buffered (SceneInitOtherMdl clones the model into
 *  mdl_addr_db) because a door can be mid-animation in two rooms at once.
 *  Pick this frame's copy and re-point the ANI_CTRL at it -- the pak the
 *  animation walks has to follow the buffer, not just the returned header.
 * ------------------------------------------------------------------------ */
static u_int *SceneDoorDoubleBufferProc(SCN_ANM_MDL *sam, int DoubleBufferId)  /* 1680 */
{
    HeaderSection *pak_head;

    if (DoubleBufferId == 0)                                            /* 1683 */
    {
        pak_head = (HeaderSection *)sam->mdl_addr;                      /* 1684 */
    }
    else
    {
        pak_head = (HeaderSection *)sam->mdl_addr_db;                   /* 1687 */
    }

    sam->pMdlAnm->mdl_p  = &pak_head->VersionID;                        /* 1690 */
    sam->pMdlAnm->mpk_p  = (u_int *)GetFileInPak(pak_head, 0);          /* 1691 */
    sam->pMdlAnm->pk2_p  = (u_int *)GetFileInPak(pak_head, 1);          /* 1692 */
    sam->pMdlAnm->base_p = pak_head;                                    /* 1693 */

    return &pak_head->VersionID;                                        /* 1695 */
}

/* --------------------------------------------------------------------------
 *  SceneGetMotAddr
 *
 *  Entry 0 of a scene motion pack is a table of 8-byte model prefixes, one per
 *  model; entry no + 1 is that model's motion pack.  The prefix is copied out
 *  so SceneGetMimAddr can match the morph pack against it.
 * ------------------------------------------------------------------------ */
static u_int *SceneGetMotAddr(u_int *pak_top, u_int no, char *pfx)
{
    u_int *p;

    p = GetADRTBL(pak_top, 0);
    strncpy(pfx, (char *)(p + no * 2), 8);
    pfx[8] = '\0';

    return GetADRTBL(pak_top, no + 1);
}

/* --------------------------------------------------------------------------
 *  SceneGetMimAddr
 *
 *  Same shape as SceneGetMotAddr, but the morph packs are not in model order --
 *  the prefix table is searched instead, on the first 5 characters.
 * ------------------------------------------------------------------------ */
static u_int *SceneGetMimAddr(u_int *pak_top, char *pfx)
{
    u_int *p;
    int    no;
    int    i;

    p  = GetADRTBL(pak_top, 0);
    no = -1;

    for (i = 0; i < 8; i++)
    {
        if (strncmp((char *)p, pfx, 5) == 0)
        {
            no = i;
            break;
        }
        p += 2;
    }

    if (no < 0)
    {
        return (u_int *)0;
    }

    return GetADRTBL(pak_top, no + 1);
}

void SceneCheckModelEntry(SCENE_CTRL *sc)
{
    (void)sc;
}

/* --------------------------------------------------------------------------
 *  PrefixToNo
 *
 *  Numeric tail of a model/room prefix: skip to the first digit inside the
 *  first `num` characters, then atoi() everything from there to `num`.  Zero
 *  when the prefix holds no digit at all (e.g. "NULL").
 * ------------------------------------------------------------------------ */
/* --------------------------------------------------------------------------
 *  GetPrefixNo
 *
 *  Model number out of a 4-character prefix: skip the one-letter kind ('c',
 *  'f', 'd', 'i', ...) and read the rest as a number.  Numbers above 500 are
 *  folded modulo 100 -- the high blocks are variants of a base model.
 *
 *  NOTE the terminator written into pfx_tmp[4] is '\n' (0x0a), not '\0'.  That
 *  is what the ROM stores; atoi() stops on it either way, so the quirk is
 *  harmless, but it is reproduced rather than "corrected".
 * ------------------------------------------------------------------------ */
u_int GetPrefixNo(char *pfx)                                            /* 1892 */
{
    u_int mdl_no;
    char  pfx_tmp[5];

    strncpy(pfx_tmp, pfx, 4);                                           /* 1895 */
    pfx_tmp[4] = '\n';                                                  /* 1896 */
    mdl_no = (u_int)atoi(&pfx_tmp[1]);                                  /* 1897 */

    if (mdl_no > 500)                                                   /* 1898 */
    {
        mdl_no = mdl_no % 100;                                          /* 1899 */
    }

    return mdl_no;                                                      /* 1901 */
}

int PrefixToNo(char *pPrefix, int num)                                  /* 1915 */
{
#define MAX_PREFIX_SIZE 33
    fixed_array<char, MAX_PREFIX_SIZE> tmp_prifix;

    /// Need to take into account the '\0' at the end
    if (num > MAX_PREFIX_SIZE-1)
    {
        return 0;
    }

#undef MAX_PREFIX_SIZE

    int i = 0;
    int j = 0;

    if (num > 0)
    {
        while (i < num && (u_char)(pPrefix[i] - '0') >= 10)
        {
            i++;
        }
        while (i < num)
        {
            tmp_prifix[j] = pPrefix[i];
            i++;
            j++;
        }
    }

    tmp_prifix[j] = '\0';

    return atoi(tmp_prifix.data());
}

/* --------------------------------------------------------------------------
 *  AreaPrefixToTopNo
 *
 *  First room number of the area a prefix belongs to, or -1 when it matches
 *  none.  NOTE the table terminates on an EMPTY PREFIX STRING, not on a
 *  top_no of -1 -- entry [0] is legitimately top_no 0, and so is the
 *  terminator, so testing top_no would stop on the very first row.
 * ------------------------------------------------------------------------ */
static int AreaPrefixToTopNo(const char *pPrefix)                       /* 1970 */
{
    u_int i   = 0;                                                      /* 1971 */
    int   ret = -1;                                                     /* 1972 */

    while (area_prefix_to_no[i].prefix[0] != '\0')                      /* 1974 */
    {
        if (strncmp(pPrefix, area_prefix_to_no[i].prefix,
                    strlen(area_prefix_to_no[i].prefix)) == 0)
        {
            ret = area_prefix_to_no[i].top_no;                          /* 1979 */
            break;
        }
        i++;
    }

    return ret;                                                         /* 1984 */
}

/* --------------------------------------------------------------------------
 *  PrefixToRoomNo
 *
 *  "ry02" -> 0 + 2, "ros01" -> 13 + 1, "NULL" -> -1.  The digit scan spans
 *  six characters, not the full eight-byte prefix field: the two trailing
 *  bytes belong to the next field in the scene header and would otherwise be
 *  read as part of the number.
 * ------------------------------------------------------------------------ */
static int PrefixToRoomNo(const char *pPrefix)
{
    int top_no = AreaPrefixToTopNo(pPrefix);

    if (top_no == -1)
    {
        return -1;
    }

    return top_no + PrefixToNo((char *)pPrefix, 6);
}

/* --------------------------------------------------------------------------
 *  GetHeaderMdlNo
 *
 *  Pull `num` 8-byte model prefixes out of the scene header into sam[], and
 *  return the cursor for the next group.
 *
 *  The groups (man / door / furn / item) are each padded to a QUADWORD, so a
 *  group with an odd number of prefixes leaves the cursor 8 bytes into a
 *  quadword and has to be rounded up.  Miss this and every group after the
 *  first odd one reads its prefixes 8 bytes early -- the models still "load",
 *  they are just the wrong ones.  Note the padding is only applied when the
 *  group was non-empty.
 * ------------------------------------------------------------------------ */
static char *GetHeaderMdlNo(SCN_ANM_MDL *sam, int num, char *hdr_pfx)   /* 1869 */
{
    int i;

    if (num > 0)                                                        /* 1871 */
    {
        for (i = 0; i < num; i++)
        {
            strncpy(sam[i].prefix, hdr_pfx, 8);                         /* 1873 */
            sam[i].prefix[8] = '\0';                                    /* 1874 */
            sam[i].mdl_no = PrefixToNo(sam[i].prefix, 8);               /* 1875 */
            hdr_pfx += 8;                                               /* 1877 */
        }

        if (((uintptr_t)hdr_pfx & 0xf) != 0)                            /* 1879 */
        {
            /* ROM: GetAlignUp((u_int)hdr_pfx, 4).  That helper is 32-bit and
             * would truncate a host pointer, so the same round-up-to-1<<4 is
             * done in pointer arithmetic here. */
            hdr_pfx = (char *)(((uintptr_t)hdr_pfx + 0xf) & ~(uintptr_t)0xf); /* 1880 */
        }
    }

    return hdr_pfx;                                                     /* 1882 */
}

/* --------------------------------------------------------------------------
 * File-number / utility functions
 * -------------------------------------------------------------------------- */

int GetFileNoFromSceneNo(int scene_no)
{
    return scene_no * 3 + S0010_PSS;
}

int SceneDecisionMovie(int scene_no)
{
    int file_no = GetFileNoFromSceneNo(scene_no);
    char ext = GetFileExtLabel(file_no);
    return ext == '\x0f';
}

int SceneEffectDataFileNoGet(int scene_no)
{
    return GetFileNoFromSceneNo(scene_no) + 2;
}

void InitSceneWork(void)                                                /* 2074 */
{
    int i;

    memset(&scene_ctrl, 0, sizeof(scene_ctrl));

    /* Redundant after the memset, but it is what the ROM does. */
    for (i = 0; i < 2; i++)                                             /* 2079 */
    {
        scene_ctrl[i].init_flg = 0;
    }

    memset(&scene_load, 0, sizeof(scene_load));                         /* 2081 */
    scene_load.status = 0;                                              /* 2082 */
    scn_vib_time0 = 0;                                                  /* 2083 */
    scn_vib_time1 = 0;                                                  /* 2084 */
}

/* --------------------------------------------------------------------------
 * ADPCM lookup
 * -------------------------------------------------------------------------- */

/* Scene number -> streamed-ADPCM file number.  Read out of .rodata at
 * 0x3c5590; the original is a function-local `static fixed_array<int,65>`
 * copy-constructed from this table behind a GCC 2.96 guard variable, which is
 * the guard reproduced below.  2279 is the "no dedicated track" filler. */
static const int SceneNoToAdpcmNoTbl[65] =              /* rdata 0x3c5590 */
{
    2279, 2279, 2498, 2279, 2281, 2283, 2083, 2081,
    2079, 2285, 2279, 2077, 2075, 2073, 2279, 2071,
    2279, 2069, 2279, 2291, 2293, 2067, 2279, 2329,
    2331, 2295, 2279, 2297, 2325, 2500, 2502, 2554,
    2279, 2556, 2279, 2279, 2279, 2279, 2558, 2620,
    2560, 2279, 2279, 2279, 2572, 2279, 2279, 2279,
    2279, 2279, 3050, 2279, 2279, 2279, 2299, 2327,
    2279, 2562, 2564, 2566, 2568, 3048, 2333, 2570,
    2335
};

static int SceneSceneNoToAdpcmNo(int scene_no)                          /* 2169 */
{
    static fixed_array<int, 65> SceneNoToAdpcmNo;       /* bss 0x4bbe60 */
    static int                  init_flg = 0;           /* the ROM's guard */

    if (init_flg == 0)
    {
        init_flg = 1;
        memcpy(SceneNoToAdpcmNo.data(), SceneNoToAdpcmNoTbl,
               sizeof(SceneNoToAdpcmNoTbl));
    }

    return SceneNoToAdpcmNo[scene_no];                                  /* 2239 */
}

/* --------------------------------------------------------------------------
 * Load request / wait
 * -------------------------------------------------------------------------- */

static u_int *SceneDataLoadReq(int scene_no, u_int *load_addr)
{
    int    i;

    int file_no = GetFileNoFromSceneNo(scene_no);

    /* Already loaded? */
    for (i = 0; i < 2; i++)
    {
        if (scene_ctrl[i].init_flg == 1 && scene_ctrl[i].scene_no == scene_no)
        {
            return nullptr;
        }
    }

    /* Find a free buffer slot */
    int buf_id = -1;
    for (i = 0; i < 2; i++)
    {
        if (scene_ctrl[i].init_flg == 0)
        {
            buf_id = i;
            break;
        }
    }
    if (buf_id < 0)
    {
        printf("Warning!! There is no buffer which can be loaded.\n");
        buf_id = 0;
    }

    scene_load.buf_id = buf_id;
    printf("scene load buf no = %d\n", buf_id);

    scene_ctrl[buf_id].scene_no = scene_no;

    /* Load main scene data */
    u_int *addr = (u_int *) LoadReqGetAddr(file_no, (uintptr_t) load_addr, &scene_load.id[scene_load.file_num]);
    scene_ctrl[buf_id].scn_data_addr  = load_addr;
    scene_ctrl[buf_id].light_rev_addr = addr;
    scene_load.file_num++;

    /* Load light revision data (optional) */
    u_int *next = (u_int *) LoadReqGetAddr(file_no + 1, (uintptr_t) addr, &scene_load.id[scene_load.file_num]);
    if (next == nullptr)
    {
        scene_ctrl[buf_id].light_rev_addr = nullptr;
    }
    else
    {
        scene_load.file_num++;
        addr = next;
    }

    /* Load effect data (optional) */
    scene_ctrl[buf_id].effect_addr = addr;
    next = (u_int *)LoadReqGetAddr(file_no + 2, (uintptr_t)addr, &scene_load.id[scene_load.file_num]);

    if (next == nullptr)
    {
        scene_ctrl[buf_id].effect_addr = nullptr;
    }
    else
    {
        scene_load.file_num++;
        addr = next;
    }

    return addr;
}

int SceneDataLoadWait(void)
{
    int i;

    while (scene_load.file_num > 0)
    {
        if (!IsLoadEnd(scene_load.id[scene_load.file_num - 1]))
        {
            return 0;
        }
        scene_load.file_num--;
    }
    return 1;
}

/* --------------------------------------------------------------------------
 * Scene data parsing / environment setup
 * -------------------------------------------------------------------------- */

static void SceneDataAnalyze(SCENE_FILE *sf, u_int *scn_addr)
{
    sf->ofs_top_addr = scn_addr + 4;
    sf->scn_file_addr = scn_addr;
    sf->file_num = *scn_addr;
    sf->hdr_addr      = GetADRTBL(scn_addr, 0);
    sf->cam_fod_addr  = GetADRTBL(scn_addr, 1);
    sf->lit_fod_addr  = GetADRTBL(scn_addr, 2);
    sf->eff_fod_addr  = GetADRTBL(scn_addr, 3);
    sf->man_mot_addr  = GetADRTBL(scn_addr, 4);
    sf->man_mim_addr  = GetADRTBL(scn_addr, 5);
    sf->furn_mot_addr = GetADRTBL(scn_addr, 6);
    sf->door_mot_addr = GetADRTBL(scn_addr, 7);
    sf->item_mot_addr = GetADRTBL(scn_addr, 8);
    printf("sf->scn_file_addr = 0x%p\n", sf->scn_file_addr);
    printf("sf->file_num = %d\n",         sf->file_num);
    printf("sf->ofs_top_addr = 0x%p\n",   sf->ofs_top_addr);
    printf("sf->hdr_addr = 0x%p\n",       sf->hdr_addr);
    printf("sf->cam_fod_addr = 0x%p\n",   sf->cam_fod_addr);
    printf("sf->lit_fod_addr = 0x%p\n",   sf->lit_fod_addr);
    printf("sf->eff_fod_addr = 0x%p\n",   sf->eff_fod_addr);
    printf("sf->man_mot_addr = 0x%p\n",   sf->man_mot_addr);
    printf("sf->man_mim_addr = 0x%p\n",   sf->man_mim_addr);
    printf("sf->furn_mot_addr = 0x%p\n",  sf->furn_mot_addr);
    printf("sf->door_mot_addr = 0x%p\n",  sf->door_mot_addr);
    printf("sf->item_mot_addr = 0x%p\n",  sf->item_mot_addr);
}

static void SceneGetHeaderData(SCENE_CTRL *sc, u_int *hdr_top)
{
    char *pPrefix;

    printf("(unsigned int)hdr_top = 0x%p\n", hdr_top);
    sc->room_no     = PrefixToRoomNo((char *)(hdr_top + 1));
    sc->sub_room_no = PrefixToRoomNo((char *)((uintptr_t)hdr_top + 10));
    sc->mirror_flg  = (int)*(short *)((uintptr_t)hdr_top + 0x10);
    sc->man_mdl_num = (int)*(short *)((uintptr_t)hdr_top + 0x12);
    sc->door_num    = (int)*(short *)((uintptr_t)hdr_top + 0x14);
    sc->furn_num    = (int)*(short *)((uintptr_t)hdr_top + 0x16);
    sc->item_num    = (int)*(short *)((uintptr_t)hdr_top + 0x18);

    if (sc->man_mdl_num > 8)
    {
        printf("ManMdl Num Over!!\n");
    }
    if (sc->door_num >= 9)
    {
        printf("DoorMdl Num Over!!\n");
    }
    if (sc->furn_num >= 0xf)
    {
        printf("FurnMdl Num Over!!\n");
    }
    if (sc->item_num > 8)
    {
        printf("ItemMdl Num Over!!\n");
    }

    pPrefix = GetHeaderMdlNo(&sc->man_mdl[0],  sc->man_mdl_num, (char *)(hdr_top + 8));
    pPrefix = GetHeaderMdlNo(&sc->door_mdl[0], sc->door_num, pPrefix);
    pPrefix = GetHeaderMdlNo(&sc->furn_mdl[0], sc->furn_num, pPrefix);
    GetHeaderMdlNo(&sc->item_mdl[0], sc->item_num, pPrefix);
}

static void SceneInitEnvironment(SCENE_FILE *sf, SCENE_CTRL *sc)        /* 825 */
{
    SceneGetHeaderData(sc, sf->hdr_addr);                               /* 827 */
    sc->fog.min  = 0.0f;                                                /* 829 */
    sc->fog.max  = 255.0f;                                              /* 830 */
    sc->fog.near = 1000.0f;                                             /* 831 */
    sc->fog.far  = 10000.0f;             /* lit4 0x3ee828 */            /* 832 */
    sc->fog.r    = 0x10;                                                /* 833 */
    sc->fog.g    = 0x10;
    sc->fog.b    = 0x10;
    scn_vib_time1 = 0;                                                  /* 836 */
    scn_vib_time0 = 0;
}

static void SceneCtrlInit(void)
{
    int          i;

    GRA3DCAMERA *cam = gra3dGetCamera();
    int buf_id = scene_load.buf_id;
    SCENE_CTRL *sc = &scene_ctrl[buf_id];

    for (i = 0; i < 8; i++)
    {
        memset(&sc->man_mdl[i], 0, sizeof(SCN_ANM_MDL));
        sc->man_mdl[i].disp_flg = 1;
    }

    for (i = 0; i < 14; i++)
    {
        memset(&sc->furn_mdl[i], 0, sizeof(SCN_ANM_MDL));
        sc->furn_mdl[i].disp_flg = 1;
    }

    for (i = 0; i < 8; i++)
    {
        memset(&sc->door_mdl[i], 0, sizeof(SCN_ANM_MDL));
        sc->door_mdl[i].disp_flg = 1;
    }

    for (i = 0; i < 8; i++)
    {
        memset(&sc->item_mdl[i], 0, sizeof(SCN_ANM_MDL));
        sc->item_mdl[i].disp_flg = 1;
    }

    SceneDataAnalyze(&scene_file, sc->scn_data_addr);
    SceneInitEnvironment(&scene_file, sc);

    sc->init_flg             = 1;
    sc->count_flg            = 1;
    sc->fNearZBak            = cam->fNearZ;
    sc->fFarZBak             = cam->fFarZ;
    sc->pMimBuf              = NULL;
    sc->MonotoneEnableBak    = gra3dIsMonotoneDrawEnable();
    sc->pSisterAccessoryPk2  = NULL;
    sc->pPlayerAccessoryPk2  = NULL;
    scene_load.file_num      = 0;
}

/* --------------------------------------------------------------------------
 *  SceneLightRevision
 *
 *  Overlay the room's ".slt" light-revision file (light_rev_addr) onto the
 *  scene's FOD light bank.  Layout, matching SceneFileSaveBin():
 *
 *      +0x00  u_int  light count (+0x04..0x0f padding to a quadword)
 *      +0x10  per light: FOD_LIT_SERIAL (0x20) then G3DLIGHT (0x70)  = 0x90
 *             ... repeated `light count` times ...
 *             SCENE_FOG (0x20)
 *             6 x float[4] ambient, the 4th word carrying the tag "AMB"
 *
 *  The file is only applied when its light count AND every light name match
 *  what the scene already holds; otherwise it is rejected wholesale.
 * ------------------------------------------------------------------------ */
static void SceneLightRevision(SCENE_CTRL *sc)
{
    FOD_LIGHT *scn_fl;
    u_char    *pRev;
    float      amb[4];
    int        ng_flg;
    int        i;

    if (sc->light_rev_addr == NULL)
    {
        printf("Warning!! Light Revision Data Not Found.\n");
        return;
    }

    scn_fl = &sc->fod_ctrl.fod_light;

    /* Both the count and every name have to agree.  Note the count test is
     * evaluated first and is NOT short-circuited by the name loop. */
    ng_flg = (scn_fl->all_lit_num != sc->light_rev_addr[0]);

    pRev = (u_char *)sc->light_rev_addr + 0x10;
    for (i = 0; i < (int)scn_fl->all_lit_num; i++)
    {
        if (strcmp((char *)pRev + 4, scn_fl->lit_serial[i].light_name) != 0)
        {
            ng_flg = 1;
            break;
        }
        pRev += sizeof(FOD_LIT_SERIAL) + sizeof(G3DLIGHT);
    }

    if (ng_flg != 0)
    {
        printf("Waning!! Light Revision Data is Wrong.\n");
        return;
    }

    pRev = (u_char *)sc->light_rev_addr + 0x10;
    for (i = 0; i < (int)scn_fl->all_lit_num; i++)
    {
        memcpy(&scn_fl->lit_serial[i], pRev, sizeof(FOD_LIT_SERIAL));
        memcpy(&scn_fl->all_lit[i], pRev + sizeof(FOD_LIT_SERIAL), sizeof(G3DLIGHT));
        pRev += sizeof(FOD_LIT_SERIAL) + sizeof(G3DLIGHT);
    }

    memcpy(&sc->fog, pRev, sizeof(SCENE_FOG));
    pRev += sizeof(SCENE_FOG);

    for (i = 0; i < FOD_AMBIENT_MAX; i++)
    {
        memcpy(amb, pRev, sizeof(amb));

        /* The writer stamps "AMB" over the unused w component; a record that
         * does not carry the tag is treated as absent. */
        if (strcmp((char *)&amb[3], "AMB") == 0)
        {
            amb[3] = 0.0f;
            g3dxVu0CopyVector(scn_fl->amb[i], amb);
        }
        else
        {
            scn_fl->amb[i][0] = 0.0f;
            scn_fl->amb[i][1] = 0.0f;
            scn_fl->amb[i][2] = 0.0f;
            scn_fl->amb[i][3] = 1.0f;
        }

        pRev += sizeof(amb);
    }

}

static u_int *SceneInitManMdl(SCN_ANM_MDL *sam, u_int *mot_addr, u_int *mim_addr,
                        u_int *mim_buf, u_int mdl_id)
{
    u_int  *next_mim_buf;
    u_int   mdl_no;
    char    pfx[9];
    int     j;

    sam->pMdlAnm     = motGetANI_CTRL();
    sam->mot_addr    = SceneGetMotAddr(mot_addr, mdl_id, pfx);
    sam->mim_addr    = SceneGetMimAddr(mim_addr, pfx);
    sam->mim_buf_addr = mim_buf;
    sam->ene_efct    = NULL;
    for (j = 3; j >= 0; j--)
    {
        sam->efct_addr[j] = NULL;
    }
    sam->scn_mdl_no  = mdl_id;
    sam->mdl_alpha   = 0x7f;
    sam->mdl_addr_db = NULL;

    mdl_no      = SceneManModelNoChange(sam->mdl_no);
    next_mim_buf = SceneInitAnime(sam->pMdlAnm, sam->mdl_addr, sam->mot_addr,
                                  sam->mim_addr, sam->mim_buf_addr, mdl_no);
    mimInitWeight(sam->pMdlAnm);
    printf("Scene One ManMdl MimBuf %d : 0x%x\n", mdl_id, (uintptr_t)next_mim_buf - (uintptr_t)mim_buf);
    MorphSetCtrl(sam->pMdlAnm, SceneManModelNoChange(sam->mdl_no));
    return next_mim_buf;
}

static void SceneInitOtherMdl(SCN_ANM_MDL *sam, u_int *pk2_mot_addr, u_int mdl_id)
{
    HEAP_WRK *heap;
    u_int    *mdl_addr;
    int       mdl_no;
    int       door_size;
    char      pfx[9];

    if (sam->disp_flg == 0)
    {
        return;
    }

    sam->pMdlAnm     = motGetANI_CTRL();
    sam->mdl_addr_db = NULL;
    sam->mot_addr    = SceneGetMotAddr(pk2_mot_addr, mdl_id, pfx);

    if (pfx[0] == 'f')
    {
        mdl_no           = PrefixToNo(pfx, 8);
        mdl_addr         = (u_int *)MapObjGetModelAddr(3, mdl_no);
        sam->mdl_addr    = mdl_addr;
        if (mdl_addr == NULL)
        {
            motFreeANI_CTRL(sam->pMdlAnm);
            sam->pMdlAnm  = NULL;
            sam->disp_flg = 0;
            PRINT_WARNING("Furn model %d does not exist",
                          PrefixToNo(pfx, 8));                          /* 1124 */
            return;
        }
    }
    else if (pfx[0] == 'd')
    {
        mdl_no        = PrefixToNo(pfx, 8);
        sam->mdl_addr = (u_int *)MapObjGetModelAddr(7, mdl_no);
        door_size     = MapDoorGetModelSize(pfx);
        heap          = GetSystemHeapWrkP();
        sam->mdl_addr_db = (u_int *)SAFE_MALLOC(heap, NULL, door_size);
        if (sam->mdl_addr == NULL || sam->mdl_addr_db == NULL)
        {
            motFreeANI_CTRL(sam->pMdlAnm);
            if (sam->mdl_addr_db != NULL)
            {
                heapCtrlFree(GetSystemHeapWrkP(), sam->mdl_addr_db);
                sam->mdl_addr_db = NULL;
            }
            sam->disp_flg = 0;
            sam->pMdlAnm  = NULL;
            PRINT_WARNING("Door model %d does not exist",
                          PrefixToNo(pfx, 8));                          /* 1141 */
            return;
        }
        sgdRemapInverse((SGDFILEHEADER *)sam->mdl_addr);
        memcpy(sam->mdl_addr_db, sam->mdl_addr, door_size);
        sgdRemap((SGDFILEHEADER *)sam->mdl_addr);
        sgdRemap((SGDFILEHEADER *)sam->mdl_addr_db);
        mdl_addr = sam->mdl_addr;
    }
    else
    {
        if (pfx[0] != 'i')
        {
            printf("Unknown Model Prefix.:%s -> call T.Yokota\n", pfx);
        }
        mdl_addr = sam->mdl_addr;
    }

    if (mdl_addr == NULL)
    {
        PRINT_WARNING("Warning!! Model Addr %s [%d]\n", pfx, sam->mdl_no); /* 1155 */
    }

    sam->scn_mdl_no = mdl_id;
    sam->mdl_alpha  = 0x7f;
    sam->mim_addr   = NULL;
    SceneInitOtherAnime(sam->pMdlAnm, mdl_addr, sam->mot_addr, NULL, NULL);
}

static void SceneAllMdlInit(SCENE_CTRL *sc, SCENE_FILE *pSceneFile)
{
    int          i;

    u_int *mim_buf = (u_int *) mem_utilGetMem(0x40000);
    sc->pMimBuf = mim_buf;

    for (i = 0; i < sc->man_mdl_num; i++)
    {
        mim_buf = SceneInitManMdl(&sc->man_mdl[i], pSceneFile->man_mot_addr,
                                  pSceneFile->man_mim_addr, mim_buf, i);
    }

    for (i = 0; i < sc->furn_num; i++)
    {
        SceneInitOtherMdl(&sc->furn_mdl[i], pSceneFile->furn_mot_addr, i);
    }

    for (i = 0; i < sc->door_num; i++)
    {
        SceneInitOtherMdl(&sc->door_mdl[i], pSceneFile->door_mot_addr, i);
    }

    for (i = 0; i < sc->item_num; i++)
    {
        SceneInitOtherMdl(&sc->item_mdl[i], pSceneFile->item_mot_addr, i);
    }
}

void SceneInitAfterModelLoad(int MimInit)
{
    SCENE_CTRL     *sc;
    FOD_CTRL       *fc;
    GRA3DLIGHTDATA *lp;
    ANI_CTRL       *ani_ctrl;
    float           offset[4];
    int             buf_id;
    int             i;
    int             mdl_no;

    buf_id = scene_load.buf_id;
    sc     = &scene_ctrl[buf_id];
    fc     = &sc->fod_ctrl;

    MapLoadGetOffsetVector(offset, sc->room_no);

    sc->AreaNoBak = GetPlyrAreaNo();
    SetPlyrAreaNo(sc->room_no);

    FodInit(fc, scene_file.cam_fod_addr, scene_file.lit_fod_addr, scene_file.eff_fod_addr, offset);
    SceneLightRevision(sc);
    FodGetFirstCam(&sc->CameraData, fc, offset);

    if (MimInit != 0)
    {
        for (i = 0; i < sc->man_mdl_num; i++)
        {
            mdl_no   = SceneManModelNoChange(sc->man_mdl[i].mdl_no);
            ani_ctrl = motSearchANI_CTRL(mdl_no);
            if (ani_ctrl != NULL)
            {
                acsResetCloth(ani_ctrl);
                mimClearAllVertex(ani_ctrl);
                mimInitWeight(ani_ctrl);
                if ((plyr_wrk.cmn_wrk.st.sta & 0x8000) != 0)
                {
                    if (ani_ctrl->mdl_no == GetPlyrMdlNo())
                    {
                        mimRequestNum(ani_ctrl, 0x19, 0);
                        IgEffectRenzFlareDispFlgSet(1);
                    }
                }
            }
        }
    }

    SceneCheckModelEntry(sc);
    SceneAllMdlInit(sc, &scene_file);
    MapObjGetModelAddr(3, 0x18);
    lp = FodGetGra3DLight();
    MapDrawSetSpRoomLight(lp);
    SceneEffectInit();
}

/* --------------------------------------------------------------------------
 * Model load management
 * -------------------------------------------------------------------------- */

static void SceneManModelLoadReq(void)
{
    SCENE_CTRL *sc = &scene_ctrl[scene_load.buf_id];

    for (int i = 0; i < sc->man_mdl_num; i++)
    {
        int mdl_no = SceneManModelNoChange(sc->man_mdl[i].mdl_no);
        mmanageReqMdl(mdl_no);
    }
}

static void SceneItemModelLoadReq(void)
{
    SCENE_CTRL *sc = &scene_ctrl[scene_load.buf_id];
    for (int i = 0; i < sc->item_num; i++)
    {
        mmanageReqItemMdl(sc->item_mdl[i].mdl_no);
    }

    if (GetPlyrAcsNo() > -1)
    {
        mmanageReqItemMdl(GetPlyrAcsNo());
    }

    if (GetSisterAcsNo() > -1)
    {
        mmanageReqItemMdl(GetSisterAcsNo());
    }
}

static int SceneManModelLoadWait(void)
{
    SCENE_CTRL *sc;
    int         i;
    int         mdl_no;

    sc = &scene_ctrl[scene_load.buf_id];
    for (i = 0; i < sc->man_mdl_num; i++)
    {
        mdl_no = SceneManModelNoChange(sc->man_mdl[i].mdl_no);
        if (!mmanageIsReadyMdl(mdl_no, (void **)&sc->man_mdl[i].mdl_addr, 1))
        {
            return 0;
        }
    }
    return 1;
}

static int SceneItemModelLoadIsEnd(void)                                /* 391 */
{
    SCENE_CTRL *sc;
    int         i;
    int         RetVal = 1;                                             /* 397 */

    sc = &scene_ctrl[scene_load.buf_id];
    for (i = 0; i < sc->item_num; i++)                                  /* 399 */
    {
        if (!mmanageIsReadyItemMdl(sc->item_mdl[i].mdl_no,
                                   (void **)&sc->item_mdl[i].pk2_addr, 1))  /* 403 */
        {
            RetVal = 0;                                                 /* 405 */
        }
        else
        {
            sc->item_mdl[i].mdl_addr =
                (u_int *)GetItemSgdAddr((int *)sc->item_mdl[i].pk2_addr); /* 408 */
        }
    }

    /* The two worn accessories are requested by SceneItemModelLoadReq() but do
     * not live in item_mdl[]; they land straight in the SCENE_CTRL pointers
     * SceneDrawManMdl() reads.  Without these the pointers stay NULL and the
     * characters draw with no accessory. */
    if (GetPlyrAcsNo() > -1)                                            /* 412 */
    {
        if (!mmanageIsReadyItemMdl(GetPlyrAcsNo(),
                                   (void **)&sc->pPlayerAccessoryPk2, 1))
        {
            RetVal = 0;
        }
    }

    if (GetSisterAcsNo() > -1)                                          /* 413 */
    {
        if (!mmanageIsReadyItemMdl(GetSisterAcsNo(),
                                   (void **)&sc->pSisterAccessoryPk2, 1))
        {
            RetVal = 0;
        }
    }

    return RetVal;                                                      /* 418 */
}

static void SceneManModelClear(void)
{
    SCENE_CTRL *sc;
    int         i;

    sc = &scene_ctrl[scene_load.buf_id];
    for (i = 0; i < sc->man_mdl_num; i++)
    {
        mmanageClearMdl(SceneManModelNoChange(sc->man_mdl[i].mdl_no));
    }
}

static void SceneItemModelClear(void)
{
    SCENE_CTRL *sc;
    int         i;

    sc = &scene_ctrl[scene_load.buf_id];
    for (i = 0; i < sc->item_num; i++)
    {
        mmanageClearItemMdl(sc->item_mdl[i].mdl_no);
    }
    mmanageClearItemMdl(GetPlyrAcsNo());
    mmanageClearItemMdl(GetSisterAcsNo());
}

static void SceneDoorModelDBFree(void)
{
    SCENE_CTRL *sc;
    HEAP_WRK   *heap;
    int         i;

    sc = &scene_ctrl[scene_load.buf_id];
    for (i = 0; i < 8; i++)
    {
        if (sc->door_mdl[i].mdl_addr_db != NULL)
        {
            heap = GetSystemHeapWrkP();
            heapCtrlFree(heap, sc->door_mdl[i].mdl_addr_db);
            sc->door_mdl[i].mdl_addr_db = NULL;
        }
    }
}

/* --------------------------------------------------------------------------
 * Main load state machine
 * -------------------------------------------------------------------------- */

int SceneAllLoad(int scene_no, u_int *load_addr)
{
    u_int *end_addr;
    int    adpcm_no;
    int    done = 0;

    switch (scene_load.status)
    {
    case 0:
        end_addr = SceneDataLoadReq(scene_no, load_addr);
        if (0x200000 < (uint)((uintptr_t)end_addr - (uintptr_t)load_addr))
        {
            PRINT_ASSERT("Scene data size over 0x%x",
                         (uintptr_t)end_addr - (uintptr_t)load_addr);   /* 295 */
        }
        scene_load.status = 1;
        printf("scene data load req\n");
        break;
    case 1:
        if (SceneDataLoadWait())
        {
            SceneCtrlInit();
            SceneManModelLoadReq();
            scene_load.status = 2;
        }
        break;
    case 2:
        if (SceneManModelLoadWait())
        {
            SceneItemModelLoadReq();
            scene_load.status = 3;
        }
        break;
    case 3:
        if (SceneItemModelLoadIsEnd())
        {
            scene_load.status = 4;
        }
        break;
    case 4:
        adpcm_no = SceneSceneNoToAdpcmNo(scene_no);
        scene_load.adpcm_id = StreamAutoPreload(adpcm_no + 1, adpcm_no, 0xd, 0, 0, 0x3200, 0, NULL);
        scene_load.status = 5;
        printf("scene adpcm load req : %d\n", scene_load.adpcm_id);
        break;
    case 5:
        if (StreamAutoIsPreload(scene_load.adpcm_id))
        {
            scene_load.status = 0;
            done = 1;
            printf("scene adpcm load finish\n");
        }
        break;
    }
    return done;
}

/* --------------------------------------------------------------------------
 * Ingame scene initialisation / shutdown
 * -------------------------------------------------------------------------- */

void SceneInitializeIngame(void)
{
    SCENE_CTRL *sc;
    int         flg;

    sc = &scene_ctrl[scene_load.buf_id];
    mimClearToScene();
    MovieTitleInit(sc->scene_no);
    MapDrawEnableFlashlightOnly(0);

    g3dutilCopyLight(&PlayerLightBackup, &plyr_wrk.fl);

    sc->DrawAneFlg    = ChrSortDelete(0);
    sc->DrawImoutoFlg = ChrSortDelete(1);
    ChrSortSetFlg(1);
    MapObjItemOff();
    EffScreenEffectStatusSet(3);
    SceneInitAfterModelLoad(1);
}

static void SceneCameraSet(FOD_CAMERA_DATA *pFodCam)
{
    gra3dcamSetPosition(pFodCam->vPosition);
    gra3dcamSetTarget(pFodCam->vTarget, 1);
    gra3dcamSetRoll(pFodCam->fRoll);
    gra3dcamSetFov(pFodCam->fFov);
    gra3dcamSetClip(0.6f, pFodCam->fFarZ);
    gra3dApplyCamera(NULL, 1);
    SetIngameListnerInfo();
}

/* --------------------------------------------------------------------------
 * Per-frame environment renewal
 * -------------------------------------------------------------------------- */

static void SceneRenewEnvironment(SCENE_CTRL *sc)
{
    FOD_LIGHT      FirstLight;
    FOD_LIGHT      SecondLight;
    FOD_CAMERA_DATA FirstCamera;
    FOD_CAMERA_DATA SecondCamera;
    float           offset[4];
    float           frac;
    float           ifrac;
    uint            i;

    memcpy(&FirstCamera,  &sc->CameraData,       sizeof(FOD_CAMERA_DATA));
    memcpy(&FirstLight,   &sc->fod_ctrl.fod_light, sizeof(FOD_LIGHT));
    memcpy(&SecondLight,  &sc->fod_ctrl.fod_light, sizeof(FOD_LIGHT));
    memcpy(&SecondCamera, &FirstCamera,           sizeof(FOD_CAMERA_DATA));

    if (sc->fod_ctrl.now_reso == 0)
    {
        MapLoadGetOffsetVector(offset, sc->room_no);
        FodGetToSgLight(&FirstLight,  sc->fod_ctrl.lit_frame,      offset);
        FodGetToSgLight(&SecondLight, sc->fod_ctrl.lit_frame_next, offset);

        frac  = sc->fod_ctrl.float_now_frame - (float)(int)sc->fod_ctrl.float_now_frame;
        ifrac = 1.0f - frac;

        for (i = 0; (int)i < (int)sc->fod_ctrl.fod_light.all_lit_num; i++)
        {
            /* The type comes from the LIVE light record, not from the
             * lit_serial table: FodGetLightType() looks a light up by its
             * light_no, and `i` here is an array index, so going through it
             * would blend the wrong lights.  The ROM reads all_lit[i].Type. */
            int type = sc->fod_ctrl.fod_light.all_lit[i].Type;
            if (type == 0)
            {
                sceVu0InterVectorXYZ(sc->fod_ctrl.fod_light.all_lit[i].vDiffuse, SecondLight.all_lit[i].vDiffuse, FirstLight.all_lit[i].vDiffuse, frac);
                sceVu0InterVectorXYZ(sc->fod_ctrl.fod_light.all_lit[i].vSpecular, SecondLight.all_lit[i].vSpecular, FirstLight.all_lit[i].vSpecular, frac);
                sceVu0InterVectorXYZ(sc->fod_ctrl.fod_light.all_lit[i].vDirection, SecondLight.all_lit[i].vDirection, FirstLight.all_lit[i].vDirection, frac);
            }
            else if (type == 1)
            {
                sceVu0InterVectorXYZ(sc->fod_ctrl.fod_light.all_lit[i].vDiffuse, SecondLight.all_lit[i].vDiffuse, FirstLight.all_lit[i].vDiffuse, frac);
                sceVu0InterVectorXYZ(sc->fod_ctrl.fod_light.all_lit[i].vSpecular, SecondLight.all_lit[i].vSpecular, FirstLight.all_lit[i].vSpecular, frac);
                sceVu0InterVectorXYZ(sc->fod_ctrl.fod_light.all_lit[i].vPosition, SecondLight.all_lit[i].vPosition, FirstLight.all_lit[i].vPosition, frac);
            }
            else if (type == 2)
            {
                sceVu0InterVectorXYZ(sc->fod_ctrl.fod_light.all_lit[i].vDiffuse, SecondLight.all_lit[i].vDiffuse, FirstLight.all_lit[i].vDiffuse, frac);
                sceVu0InterVectorXYZ(sc->fod_ctrl.fod_light.all_lit[i].vSpecular, SecondLight.all_lit[i].vSpecular, FirstLight.all_lit[i].vSpecular, frac);
                sceVu0InterVectorXYZ(sc->fod_ctrl.fod_light.all_lit[i].vPosition, SecondLight.all_lit[i].vPosition, FirstLight.all_lit[i].vPosition, frac);
                sceVu0InterVectorXYZ(sc->fod_ctrl.fod_light.all_lit[i].vDirection, SecondLight.all_lit[i].vDirection, FirstLight.all_lit[i].vDirection, frac);
                sc->fod_ctrl.fod_light.all_lit[i].fAngleOutside =
                    SecondLight.all_lit[i].fAngleOutside * frac +
                    FirstLight.all_lit[i].fAngleOutside * ifrac;
                sc->fod_ctrl.fod_light.all_lit[i].fAngleInside =
                    SecondLight.all_lit[i].fAngleInside * frac +
                    FirstLight.all_lit[i].fAngleInside * ifrac;
            }
        }

        FodGetCamData(&FirstCamera,  sc->fod_ctrl.cam_frame,      offset);
        FodGetCamData(&SecondCamera, sc->fod_ctrl.cam_frame_next, offset);

        frac  = sc->fod_ctrl.float_now_frame - (float)(int)sc->fod_ctrl.float_now_frame;
        ifrac = 1.0f - frac;

        sceVu0InterVectorXYZ(sc->CameraData.vPosition, SecondCamera.vPosition, FirstCamera.vPosition, frac);
        sceVu0InterVectorXYZ(sc->CameraData.vTarget, SecondCamera.vTarget, FirstCamera.vTarget, frac);
        sc->CameraData.fRoll  = SecondCamera.fRoll  * frac + FirstCamera.fRoll  * ifrac;
        sc->CameraData.fFov   = SecondCamera.fFov   * frac + FirstCamera.fFov   * ifrac;
        sc->CameraData.fFarZ  = SecondCamera.fFarZ  * frac + FirstCamera.fFarZ  * ifrac;
        sc->CameraData.fNearZ = SecondCamera.fNearZ * frac + FirstCamera.fNearZ * ifrac;
    }

    SceneEffectMain(sc, sc->effect_addr);

    if (sc->count_flg == 1)
    {
        SceneCameraSet(&sc->CameraData);
    }
}

static void SceneLightClear(SCENE_CTRL *sc)
{
    FodSetSpotLights(&sc->fod_ctrl.fod_light.all_lit[0], 0);
}

void SceneReleaseEffect(SCENE_CTRL *sc)
{
    int  i;
    int  j;

    for (i = 0; i < 8; i++)
    {
        for (j = 0; j < 4; j++)
        {
            if (sc->man_mdl[i].efct_addr[j] != NULL)
            {
                ResetEffects(sc->man_mdl[i].efct_addr[j]);
                sc->man_mdl[i].efct_addr[j] = NULL;
            }
        }
    }
    if (eff_param.pdf_p != NULL)
    {
        ResetEffects(eff_param.pdf_p);
        eff_param.pdf_p = NULL;
    }
}

static void SceneDrawRoom(SCENE_CTRL *sc)
{
    GRA3DCAMERA *pCam;

    pCam = gra3dGetCamera();

    FodSetMyLight(&sc->fod_ctrl.fod_light, "room", pCam->matCoord[2]);
    MhCtlDraw();
    MapSpProc();
}

static void SceneDrawManMdl(SCENE_CTRL *sc, u_int mdl_id, float *offset)
{
    SGDFILEHEADER  *pSGDTop;
    SGDCOORDINATE  *pCoord;
    GRA3DCAMERA    *pCam;
    u_int           mdl_no;
    HeaderSection  *pHdr;
    int             acs_no;
    SCN_ANM_MDL    *sam;

    sam = &sc->man_mdl[mdl_id];
    pSGDTop = (SGDFILEHEADER *)sam->pMdlAnm->base_p;

    if (pSGDTop == NULL)
    {
        return;
    }

    SceneSetCoordFrameF(sam->pMdlAnm, sc->fod_ctrl.float_now_frame - 1.0f, 0);
    SceneMimSetVertex(sam->pMdlAnm, sc->fod_ctrl.now_frame - 1);

    pCoord = pSGDTop->pCoord;
    sceVu0UnitMatrix(pCoord->matCoord);
    pCoord->matCoord[0][0] = 25.0f;
    pCoord->matCoord[1][1] = 25.0f;
    pCoord->matCoord[2][2] = 25.0f;
    pCoord->matCoord[3][3] = 1.0f;
    sceVu0RotMatrixX(pCoord->matCoord, pCoord->matCoord, SCN_PI);
    sceVu0TransMatrix(pCoord->matCoord, pCoord->matCoord, offset);
    sgdCalcBoneCoordinate(pCoord, pSGDTop->uiNumBlock - 1);


    pCam = gra3dGetCamera();
    FodSetMyLight(&sc->fod_ctrl.fod_light, sam->prefix, pCam->matCoord[2]);

    mdl_no = SceneManModelNoChange(sam->mdl_no);
    acsClothCtrl(sam->pMdlAnm, sam->pMdlAnm->mpk_p, mdl_no, 0);
    MorphRun(sam->pMdlAnm, sam->pMdlAnm->mpk_p);

    sam->mdl_alpha = sam->mdl_alpha & 0x7fffffff;
    ManmdlSetAlpha(pSGDTop, (u_char)sam->mdl_alpha);

    if ((sam->mdl_no == 0) && (sc->pPlayerAccessoryPk2 != NULL))
    {
        pHdr = (HeaderSection *)GetItemSgdAddr(sc->pPlayerAccessoryPk2);
        SendItemVram((u_int *)sc->pPlayerAccessoryPk2, 0);
        acs_no = GetPlyrAcsNo();
        ManItemSGDDraw(pHdr, sam->pMdlAnm, acs_no);
    }
    else if ((sam->mdl_no == 1) && (sc->pSisterAccessoryPk2 != NULL))
    {
        pHdr = (HeaderSection *)GetItemSgdAddr(sc->pSisterAccessoryPk2);
        SendItemVram((u_int *)sc->pSisterAccessoryPk2, 0);
        acs_no = GetSisterAcsNo();
        ManItemSGDDraw(pHdr, sam->pMdlAnm, acs_no);
    }

    if (gra3dIsMonotoneDrawEnable())
    {
        SendEneVramMono(sam->pMdlAnm->mdl_p, 0x2bc0, sam->pMdlAnm->bwc_p);
    }
    else
    {
        SendEneVram(sam->pMdlAnm->mdl_p, 0x2bc0);
    }

    MioPan_ProbeDumpCharLights("scene", plyr_wrk.fl.vPosition);   /* MIOPAN_PROBE */

    _gra3dDrawSGD(pSGDTop, SRT_REALTIME, NULL, -1);
    DrawGirlSubObj(sam->pMdlAnm->mpk_p, (u_char)sam->mdl_alpha);
    MorphReset(sam->pMdlAnm, sam->pMdlAnm->mpk_p);
}

static void SceneDrawOtherMdl(SCENE_CTRL *sc, SCN_ANM_MDL *sam, float *offset)
{
    SGDFILEHEADER  *pSGDHead;
    SGDCOORDINATE  *pCoord;
    GRA3DCAMERA    *pCam;
    GRA3DLIGHTDATA *pLightData;
    int             no;
    int             cmp;
    float           tmp_mat[4][4];

    if (sam->disp_flg == 0)
    {
        return;
    }

    if (sam->prefix[0] == 'd')
    {
        pSGDHead = (SGDFILEHEADER *)SceneDoorDoubleBufferProc(sam, sc->DoubleBufferId);
    }
    else
    {
        pSGDHead = (SGDFILEHEADER *)sam->mdl_addr;
    }

    SceneSetCoordFrameF(sam->pMdlAnm, sc->fod_ctrl.float_now_frame - 1.0f, 1);
    SceneMimSetVertex(sam->pMdlAnm, sc->fod_ctrl.now_frame - 1);

    if (pSGDHead == NULL)
    {
        return;
    }

    pCoord = pSGDHead->pCoord;
    sceVu0MulMatrix(pCoord->matCoord, fod_cmn_mtx, pCoord->matCoord);
    sceVu0UnitMatrix(tmp_mat);
    sceVu0TransMatrix(tmp_mat, tmp_mat, offset);
    sceVu0MulMatrix(pCoord->matCoord, tmp_mat, pCoord->matCoord);
    sgdCalcBoneCoordinate(pCoord, pSGDHead->uiNumBlock - 1);

    SceneSetHandSpotLightToPlyrWrk(&sc->fod_ctrl.fod_light);

    if (sam->prefix[0] == 'd')
    {
        no         = PrefixToNo(sam->prefix, 8);
        pLightData = (GRA3DLIGHTDATA *)MapObjGetLight(7, no);
        gra3dSetLightData(pLightData, NULL);
        gra3dExecPrelight(pSGDHead, pCoord->matCoord);
        pCam = gra3dGetCamera();
        FodSetMyLight(&sc->fod_ctrl.fod_light, sam->prefix, pCam->matCoord[2]);
    }
    else
    {
        cmp  = strncmp(sam->prefix, "i001", 4);
        pCam = gra3dGetCamera();
        if (cmp == 0)
        {
            FodSetMyLight(&sc->fod_ctrl.fod_light, "ch000", pCam->matCoord[2]);
        }
        else
        {
            FodSetMyLight(&sc->fod_ctrl.fod_light, sam->prefix, pCam->matCoord[2]);
        }
    }

    sam->mdl_alpha = sam->mdl_alpha & 0x7fffffff;
    ManmdlSetAlpha(sam->pMdlAnm->base_p, (u_char)sam->mdl_alpha);

    if (sam->prefix[0] == 'i')
    {
        SendItemVram(sam->pk2_addr, 0);
    }

    _gra3dDrawSGD(pSGDHead, SRT_REALTIME, NULL, -1);
}

/* --------------------------------------------------------------------------
 * Main draw entry
 * -------------------------------------------------------------------------- */

void SceneDraw(int scene_no)
{
    SCENE_CTRL *sc;
    float       offset[4];
    int         i;

    sc = NULL;
    for (i = 0; i < 2; i++)
    {
        if (scene_ctrl[i].scene_no == scene_no)
        {
            scn_now_play_id = i;
            sc = &scene_ctrl[i];
            break;
        }
    }
    if (sc == NULL)
    {
        printf("Warning!! Unknown Play Scene No.<%d>\n", scene_no);
        scn_now_play_id = 0;
        sc = &scene_ctrl[0];
    }

    if ((sc->fod_ctrl.now_frame == 1) && (sc->fod_ctrl.now_reso == 0))
    {
        StreamAutoPreloadPlay(scene_load.adpcm_id);
        printf("scene now play no = %d\n", scn_now_play_id);
    }

    SceneRenewEnvironment(sc);
    gra3dSetGsRegisterDefault();
    gra3dSetFog(sc->fog.min, sc->fog.max, sc->fog.near, sc->fog.far);
    gra3dSetFogColor(sc->fog.r, sc->fog.g, sc->fog.b);
    gra3dApplyFog();
    SceneLightClear(sc);

    MapLoadGetOffsetVector(offset, sc->room_no);
    SceneDrawRoom(sc);

    for (i = 0; i < sc->door_num; i++)
    {
        SceneDrawOtherMdl(sc, &sc->door_mdl[i], offset);
    }
    for (i = 0; i < sc->furn_num; i++)
    {
        SceneDrawOtherMdl(sc, &sc->furn_mdl[i], offset);
    }
    for (i = 0; i < sc->item_num; i++)
    {
        SceneDrawOtherMdl(sc, &sc->item_mdl[i], offset);
    }
    for (i = 0; i < sc->man_mdl_num; i++)
    {
        SceneDrawManMdl(sc, i, offset);
    }

    InitEffectsEF();
    EffectControl(5);
    EffectControl(7);
    BrightnessAdjustmentFilterDraw();
    EffectControl(8);

    if (GetPALMode() == 0)
    {
        MovieTitleMain(sc->fod_ctrl.now_frame);
    }
    else
    {
        MovieTitleMain((int)(sc->fod_ctrl.float_now_frame / 1.2f));
    }

    sc->DoubleBufferId ^= 1;

    if (sc->count_flg == 1)
    {
        FodNextFrame(&sc->fod_ctrl, sc->scene_no);
    }
}

/* --------------------------------------------------------------------------
 * Scene end
 * -------------------------------------------------------------------------- */

int SceneIsEnd(void)                                                    /* 1372 */
{
    SCENE_CTRL *sc;
    int         ret = 0;                                                /* 1374 */
    int         skip_ok;

    sc = &scene_ctrl[scn_now_play_id];

    /* Skip only becomes available a moment into the scene.  The ROM tests
     * (now_frame * 2 >= 31); 2*now_frame is always even so that is the same
     * threshold as now_frame >= 16, but the doubled form is what the compare
     * emits and is kept to kill any doubt about the boundary. */
    skip_ok = (sc->fod_ctrl.now_frame * 2 >= 31);                       /* 1377 */

    if (skip_ok != 0)                                                   /* 1381 */
    {
        if ((pad[0].one & 0x800U) != 0)                                 /* 1382 */
        {
            sc->fod_ctrl.end_flg = 1;                                   /* 1383 */
            StreamAutoFadeOut(scene_load.adpcm_id, 1);                  /* 1384 */
        }
    }

    if (sc->fod_ctrl.end_flg != 0)                                      /* 1389 */
    {
        sc->init_flg = 0;                                               /* 1391 */
        ret          = 1;                                               /* 1392 */
    }

    return ret;                                                         /* 1394 */
}

void SceneCountFlgSet(int flg)
{
    scene_ctrl[scn_now_play_id].count_flg = (flg != 0) ? 1 : 0;
}

int SceneIsPlay(void)
{
    return scene_ctrl[scn_now_play_id].init_flg;
}

static void SceneMonotoneReturn(SCENE_CTRL *sc)
{
    int RoomNo;
    int SubRoomNo;

    if (sc->MonotoneEnableBak != gra3dIsMonotoneDrawEnable())
    {
        RoomNo    = SceneRoomNoGet();
        SubRoomNo = SceneSubRoomNoGet();
        if (sc->MonotoneEnableBak == 0)
        {
            EffWrkMonochroModeSet(0);
            gra3dMonotoneDrawEnable(0);
        }
        else
        {
            EffWrkMonochroModeSet(1);
            gra3dMonotoneDrawEnable(1);
        }
        if (RoomNo != -1)
        {
            gra3dPrelightScene(RoomNo);
        }
        if (SubRoomNo != -1)
        {
            gra3dPrelightScene(SubRoomNo);
        }
    }
}

static void SceneAniCtrlFree(SCENE_CTRL *pSceneCtrl)
{
    int i;

    for (i = 0; i < 8; i++)
    {
        if (pSceneCtrl->man_mdl[i].pMdlAnm != NULL)
        {
            MorphDell(pSceneCtrl->man_mdl[i].pMdlAnm);
            motFreeANI_CTRL(pSceneCtrl->man_mdl[i].pMdlAnm);
        }
    }
    for (i = 0; i < 0xe; i++)
    {
        if (pSceneCtrl->furn_mdl[i].pMdlAnm != NULL)
        {
            motFreeANI_CTRL(pSceneCtrl->furn_mdl[i].pMdlAnm);
        }
    }
    for (i = 0; i < 8; i++)
    {
        if (pSceneCtrl->item_mdl[i].pMdlAnm != NULL)
        {
            motFreeANI_CTRL(pSceneCtrl->item_mdl[i].pMdlAnm);
        }
    }
    for (i = 0; i < 8; i++)
    {
        if (pSceneCtrl->door_mdl[i].pMdlAnm != NULL)
        {
            motFreeANI_CTRL(pSceneCtrl->door_mdl[i].pMdlAnm);
        }
    }
}

void SceneEndProc(void)
{
    SCENE_CTRL *sc;
    int         i;

    sc = &scene_ctrl[scn_now_play_id];

    for (i = 0; i < sc->man_mdl_num; i++)
    {
        acsResetCloth(sc->man_mdl[i].pMdlAnm);
        mimClearAllVertex(sc->man_mdl[i].pMdlAnm);
        mimInitWeight(sc->man_mdl[i].pMdlAnm);
    }

    SceneReleaseEffect(sc);
    if (eff_param.mono_flg != 0)
    {
        eff_param.mono_flg = 0;
    }

    SetPlyrAreaNo(sc->AreaNoBak);
    MapObjDrawON();
    MapDrawSetSpRoomLight(NULL);
    MapDrawEnableFlashlightOnly(1);
    SceneAniCtrlFree(sc);
    gra3dcamSetClip(sc->fNearZBak, sc->fFarZBak);
    gra3dApplyCamera(NULL, 1);

    g3dutilCopyLight(&plyr_wrk.fl, &PlayerLightBackup);

    if (sc->DrawAneFlg != 0)
    {
        ChrSortRegistSis();
    }
    if (sc->DrawImoutoFlg != 0)
    {
        ChrSortRegistPlayr();
    }

    ChrSortDelFlg(1);
    MapObjItemOn();
    SceneEffectEnd();
    EffScreenEffectStatusSet(0);
    mem_utilFreeMem(sc->pMimBuf);
    SceneItemModelClear();
    SceneManModelClear();
    SceneDoorModelDBFree();
    MovieTitleEnd();
    SceneMonotoneReturn(sc);
}

/* --------------------------------------------------------------------------
 * Accessors
 * -------------------------------------------------------------------------- */

int SceneRoomNoGet(void)
{
    return scene_ctrl[scn_now_play_id].room_no;
}

int SceneSubRoomNoGet(void)
{
    return scene_ctrl[scn_now_play_id].sub_room_no;
}

SCENE_CTRL *SceneCtrlGet(int buf_no)
{
    if ((uint)buf_no < 2)
    {
        return &scene_ctrl[buf_no];
    }
    return NULL;
}

void SceneFodSetNowFrame(int scene_id, int frame)
{
    u_int frame_max = scene_ctrl[scene_id].fod_ctrl.frame_max;
    if ((int)frame_max <= frame)
    {
        frame = frame_max;
    }
    scene_ctrl[scene_id].fod_ctrl.now_frame = frame;
}

void SceneFodSetNowRezo(int scene_id, int reso)
{
    scene_ctrl[scene_id].fod_ctrl.now_reso = reso;
}

void SceneFodSetFrame(int scene_id, int frame)
{
    FodSetFrame(&scene_ctrl[scene_id].fod_ctrl, frame);
}

FOD_LIGHT *SceneFodLightPtrGet(int scene_id)
{
    return &scene_ctrl[scene_id].fod_ctrl.fod_light;
}

/* --------------------------------------------------------------------------
 * Model / position queries
 * -------------------------------------------------------------------------- */

static int SceneManModelIdToSceneModelNo(SCENE_CTRL *pCtrl, int ModelId)
{
    if (pCtrl == nullptr)
    {
        return -1;
    }

    for (int i = 0; i < pCtrl->man_mdl_num; i++)
    {
        if ((int)pCtrl->man_mdl[i].mdl_no == ModelId)
        {
            return i;
        }
    }

    return -1;
}

void SceneGetModelPDeformPos(float *Position, int ModelId, float Dist)
{
    SCENE_CTRL  *sc;
    float       (*camPos)[4];
    SCN_ANM_MDL *pSam;
    uint         scn_mdl_no;
    float        TmpPos[4];
    float        tv[4];
    float        tr[4];

    camPos    = (float (*)[4])gra3dcamGetPosition();
    sc        = &scene_ctrl[scn_now_play_id];
    memset(tv, 0, sizeof(tv));

    scn_mdl_no = SceneManModelIdToSceneModelNo(sc, ModelId);
    if (scn_mdl_no != 0xffffffff)
    {
        pSam = &sc->man_mdl[scn_mdl_no];
        GetMdlWaistPos(TmpPos, pSam->pMdlAnm, (u_short)pSam->mdl_no);
        GetTrgtRot(TmpPos, *camPos, tr, 3);
        tv[2] = Dist;
        RotFvector(tr, tv);
        sceVu0AddVector(Position, TmpPos, tv);
    }
}

int SceneManModelNoChange(int ModelNo)
{
    if (ModelNo == 0)
    {
        ModelNo = GetPlyrMdlNo();
    }
    else if (ModelNo == 1)
    {
        ModelNo = GetSisterMdlNo();
    }
    return ModelNo;
}

ANI_CTRL *SceneGetAniCtrl(int ModelId)
{
    SCENE_CTRL *sc;
    int         i;

    sc = &scene_ctrl[scn_now_play_id];
    for (i = 0; i < sc->man_mdl_num; i++)
    {
        if ((int)sc->man_mdl[i].mdl_no == ModelId)
        {
            return sc->man_mdl[i].pMdlAnm;
        }
    }
    return NULL;
}

void SceneManModelLegPositionGet(float *LegPos, int ModelId)
{
    ANI_CTRL *ani_ctrl;

    ani_ctrl = SceneGetAniCtrl(ModelId);
    if (ani_ctrl == NULL)
    {
        LegPos[0] = 0.0f;
        LegPos[1] = 0.0f;
        LegPos[2] = 0.0f;
        LegPos[3] = 1.0f;
    }
    else
    {
        GetMdlLegPos(LegPos, ani_ctrl, (u_short)ModelId);
    }
}

void SceneManModelHipPositionGet(float *HipPos, int ModelId)
{
    ANI_CTRL *ani_ctrl;

    ani_ctrl = SceneGetAniCtrl(ModelId);
    if (ani_ctrl == NULL)
    {
        HipPos[0] = 0.0f;
        HipPos[1] = 0.0f;
        HipPos[2] = 0.0f;
        HipPos[3] = 1.0f;
    }
    else
    {
        GetMdlHipPos(HipPos, ani_ctrl, (u_short)ModelId);
    }
}

/* --------------------------------------------------------------------------
 * Light / model name construction
 * -------------------------------------------------------------------------- */

/* Light-name prefix per FOD light type.  Types 0 and 1 (directional / point)
 * share the one empty string in the ROM, so only spot and point lights ever
 * produce a non-empty name to match against. */
static char *LightTypeToPrefixTbl[4] =                  /* rdata 0x3c5698 */
{
    (char *)"", (char *)"", (char *)"spot", (char *)"point"
};

static void SceneMakeLightName(char *pName, u_int LightType, u_int LightNo)  /* 2379 */
{
    /* reference_fixed_array in sbss 0x3f4f50, pointed at the table above by a
     * guarded local-static initialiser -- the ROM's __tmp_11. */
    static reference_fixed_array<char *, 4> LightTypeToPrefix(LightTypeToPrefixTbl);

    if (pName != NULL)                                                  /* 2386 */
    {
        if (LightType < 4)                                              /* 2388 */
        {
            sprintf(pName, "%s%d", LightTypeToPrefix[LightType], LightNo);  /* 2389 */
        }
        else
        {
            *pName = '\0';                                              /* 2390 */
        }
    }
}

/* Model prefix per model type: 0 character, 1 furniture, 2 door, 3 item. */
static char *ModelTypeToPrefixTbl[4] =                  /* rdata 0x3c56a8 */
{
    (char *)"ch", (char *)"f", (char *)"d", (char *)"i"
};

static void SceneMakeModelPrefix(char *pName, u_int ModelType, u_int ModelId) /* 2402 */
{
    static reference_fixed_array<char *, 4> ModelTypeToPrefix(ModelTypeToPrefixTbl);

    if (pName != NULL)                                                  /* 2410 */
    {
        if (ModelType < 4)                                              /* 2412 */
        {
            sprintf(pName, "%s%03d", ModelTypeToPrefix[ModelType], ModelId); /* 2413 */
        }
        else
        {
            *pName = '\0';                                              /* 2414 */
        }
    }
}

void SceneChangeLightParameter(int LightType, int LightNo, int ModelType, int ModelId,
                                float r, float g, float b, float Power, float Cone)
{
    SCENE_CTRL *sc;
    FOD_LIGHT  *fl;
    G3DLIGHT   *pLight;
    int         i;
    float       intens;
    char        LightNameBuf[28];
    char        ModelPrefixBuf[28];

    sc = &scene_ctrl[scn_now_play_id];
    fl = &sc->fod_ctrl.fod_light;
    SceneMakeLightName(LightNameBuf,    LightType, LightNo);
    SceneMakeModelPrefix(ModelPrefixBuf, ModelType, ModelId);

    if ((LightNameBuf[0] == '\0') || (ModelPrefixBuf[0] == '\0'))
    {
        return;
    }

    for (i = 0; i < (int)fl->all_lit_num; i++)                          /* 2439 */
    {
        if ((uint)fl->lit_serial[i].light_type == (uint)LightType)
        {
            if (strstr(fl->lit_serial[i].light_name, LightNameBuf) != NULL)
            {
                if (strstr(fl->lit_serial[i].light_name, ModelPrefixBuf) != NULL)
                {
                    pLight = &fl->all_lit[i];
                    pLight->fMaxRange   = Power;
                    pLight->fMinRange   = Power * 0.5f;
                    pLight->vDiffuse[0] = r;
                    pLight->vDiffuse[1] = g;
                    pLight->vDiffuse[2] = b;

                    /* Cone only means anything for a spot; the intensity the
                     * g3d core wants is the SQUARED cosine of the half angle,
                     * the angle arriving here in degrees. */
                    if (LightType == G3DLIGHT_SPOT)                     /* 2448 */
                    {
                        intens = cosf(Cone * SCN_PI / 180.0f);
                        gra3dSetLightIntens(pLight, intens * intens);   /* 2449 */
                    }
                }
            }
        }
    }
}

/* --------------------------------------------------------------------------
 *  SceneChangeHandSpotLightParameter
 *
 *  Retune the scene's hand spot (the torch) without going through the name
 *  matching SceneChangeLightParameter does -- the FOD light bank records which
 *  light it is up front, in hand_spot_no.
 *
 *  The cone angle arrives in degrees; gra3dSetLightIntens wants the SQUARED
 *  cosine of the half angle.
 * ------------------------------------------------------------------------ */
void SceneChangeHandSpotLightParameter(float r, float g, float b,
                                       float Power, float Cone)         /* 2467 */
{
    SCENE_CTRL *sc;
    G3DLIGHT   *pLight;
    float       intens;

    sc = &scene_ctrl[scn_now_play_id];

    if (sc->fod_ctrl.fod_light.hand_spot_no != -1)                      /* 2473 */
    {
        pLight = &sc->fod_ctrl.fod_light.all_lit[sc->fod_ctrl.fod_light.hand_spot_no];

        pLight->fMinRange   = Power * 0.5f;                             /* 2477 */
        pLight->vDiffuse[0] = r;                                        /* 2478 */
        pLight->vDiffuse[1] = g;                                        /* 2479 */
        pLight->vDiffuse[2] = b;                                        /* 2480 */
        pLight->fMaxRange   = Power;                                    /* 2481 */

        intens = cosf(Cone * SCN_PI / 180.0f);
        gra3dSetLightIntens(pLight, intens * intens);                   /* 2484 */
    }
}

void SceneSetSquare(int pri, float x, float y, float w, float h,
                    u_char r, u_char g, u_char b, u_char a)             /* 2008 */
{
    float x1 = x - 320.0f;
    float y1 = y - 224.0f;
    SetSquare(pri, x1, y1, x1 + w, y1, x1, y1 + h, x1 + w, y1 + h, r, g, b, a);
}
