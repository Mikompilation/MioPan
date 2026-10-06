// FILE: /home/zero_rom/zero2np/src/ingame/photo/photo_dat.c
//
// The photographable-object registry.
//
// Two halves that barely talk to each other.  The first is the save-side bit
// set: 72 entries in photo_dat[], one flag each in photo_dat_save, and the
// FlgUp / IsUp / FlgDown trio the event conditions read.  The second is the
// live registry: up to four placed MDAT_OBJ records at a time, registered by
// EvSetObjPhotoAble through photo_datObjStart(), and re-scored every frame by
// photo_datObjMain().
//
// photo_datObjMain() is the whole module.  Per registered object it refreshes
// the cached position, tracks the nearest one (for the proximity hint SE and
// the filament) and, while the finder is up, the most centred one (for the
// finder ring and the shot itself).  Sealed ghosts additionally fade in
// through a CWrkVariable that walks +/-4 a frame by whether the object is
// inside the ring, and objects flagged f_deform get one or two parts-deform
// effects re-issued every frame.
//
// The four ratio tables and hint_dat are exported but never read here -- they
// belong to the shot-scoring and hint-texture code in photo_make.c /
// n_plyr_camera.c, which is not reconstructed.  Neither is PHOTO_DAT_OBJ_WRK's
// p_deform: photo_datObjMain() throws away every SetEffects_* handle.
//
// Reconstructed from the Feb 6 2004 prototype (SLES_523.84), photo_dat.o.
// Trailing /* NNN */ comments are the original source line numbers.  They
// start at GetPhotoDatNum(): the ROM reaches line 71 with photo_dat[] already
// behind it, so the 72-entry initialiser cannot have occupied one line per
// entry there, and no honest line number can be put on the table below.

#include "photo_dat.h"

#include <stddef.h>                             /* NULL                        */

#include "libvu0.h"                             /* sceVu0CopyVector            */

#include "../../common/utility.h"               /* GetDistV                    */
#include "../../common/utility2.h"              /* PRINT_ASSERT                */
#include "../../common/variable.h"              /* plyr_wrk                    */
#include "../../graphics/effect/effect.h"       /* SetEffects_PDEFORM          */
#include "../../graphics/graph3d/g3ddbg.h"      /* G3DASSERT                   */
#include "../../system/eeiop/snd_buffer.h"      /* CSND_BUF_PLAY_NO_ID         */
#include "../../system/os/system.h"             /* CSYSTEM_SND_BUF_PLAY        */
#include "../plyr/player.h"                     /* PlayerModeIsFinder / CulcEP3 */
#include "m_plyr_camera.h"                      /* m_plyr_camera               */
#include "../../graphics/graph3d/g3dxVu0.h" /* g3dxVu0CopyVector */

/* --------------------------------------------------------------------------
 *  Shot-scoring tables.  Read by photo_make.c / the camera HUD, not here.
 * ------------------------------------------------------------------------ */

/* Score multiplier by how far the subject was, and by how well it was centred;
 * photo_charge_ratio[] is the charge-shot bonus by charge level.
 *
 * Ten of the 24 entries come out one ulp above the ROM's stored bits, from six
 * distinct decimals (1.2 1.1 0.85 0.8 1.6 0.6): EE GCC truncates a float
 * literal toward zero where the host rounds to nearest.  The nice decimal is
 * what the source carried, so it is what is written here. */
float photo_dist_ratio[10] =                                /* data 33c430 */
{
    1.2f, 1.15f, 1.1f, 1.0f, 0.95f, 0.9f, 0.85f, 0.8f, 0.75f, 0.7f
};

float photo_center_ratio[10] =                              /* data 33c458 */
{
    1.6f, 1.4f, 1.2f, 1.1f, 1.0f, 0.9f, 0.8f, 0.7f, 0.6f, 0.5f
};

float photo_charge_ratio[4] =                               /* data 33c480 */
{
    1.4f, 1.6f, 1.8f, 2.0f
};

/* Furthest a subject can be picked up at all. */
float photo_rng_tbl[1] = { 5000.0f };                       /* sdata 3f3848 */

/* 436x300 out of the 640x448 frame, so the viewfinder covers a little over two
 * thirds of the screen. */
u_short photo_frame_tbl[1][2] = { { 436, 300 } };           /* sdata 3f3850 */

/* --------------------------------------------------------------------------
 *  The subjects.
 *
 *  Entries 1..35 are the story objects: an image number for the photo list, a
 *  message type/number pair for the hint text, and a hint range of 1000 (1500
 *  for the two 12/34 duplicates, 630 for entry 26).  Entries 36..71 are the
 *  ghost-list photographs -- a flat run, image 4230 + n against ghost list
 *  slot 116 + n -- and they differ in kind: 10000 range, no deform, no hint
 *  message.  Entry 0 is the "not photographable" slot; PhotoAble == 0 is
 *  rejected before the table is ever indexed.
 *
 *  Top / Bottom are the vertical framing bounds the shot scoring would use;
 *  every entry in the prototype leaves both at zero.
 *
 *  Verified byte-for-byte against .data 0x33c490 -- all 1728 bytes.
 * ------------------------------------------------------------------------ */
PhotoData photo_dat[PHOTO_DAT_NUM] =                        /* data 33c490 */
{
    /*        Top    Bottom      Dist  Point  image  mty  mnu   fi ff fd fs fg fu  ghost */
    /*  0 */ { 0.0f, 0.0f,  1000.0f,    0,    -1,  -1,  -1,  0, 0, 0, 0, 0, 0,    -1 },
    /*  1 */ { 0.0f, 0.0f,  1000.0f, 1000,  3901,  42,   1,  1, 1, 1, 1, 0, 0,    -1 },
    /*  2 */ { 0.0f, 0.0f,  1000.0f, 1000,  3901,  10,   2,  1, 1, 1, 1, 0, 0,    -1 },
    /*  3 */ { 0.0f, 0.0f,  1000.0f, 1000,  3901,  70,   3,  1, 1, 1, 1, 0, 0,    -1 },
    /*  4 */ { 0.0f, 0.0f,  1000.0f, 1000,  3910,  42,   4,  1, 1, 1, 1, 0, 1,    -1 },
    /*  5 */ { 0.0f, 0.0f,  1000.0f,    0,  3901,  42,   5,  1, 1, 1, 1, 0, 0,    -1 },
    /*  6 */ { 0.0f, 0.0f,  1000.0f,    0,  3905,  42,   6,  1, 1, 1, 1, 1, 0,    -1 },
    /*  7 */ { 0.0f, 0.0f,  1000.0f,    0,  3901,  42,   7,  1, 1, 1, 1, 0, 0,    -1 },
    /*  8 */ { 0.0f, 0.0f,  1000.0f, 1000,  3919,  42,   8,  1, 1, 1, 1, 0, 0,    -1 },
    /*  9 */ { 0.0f, 0.0f,  1000.0f, 1000,    -1,  -1,  -1,  1, 1, 1, 1, 0, 0,    -1 },
    /* 10 */ { 0.0f, 0.0f,  1000.0f, 1000,  3918,  42,  10,  1, 1, 1, 1, 0, 0,    -1 },
    /* 11 */ { 0.0f, 0.0f,  1000.0f, 1000,  3917,  42,  11,  1, 1, 1, 1, 0, 0,    -1 },
    /* 12 */ { 0.0f, 0.0f,  1500.0f,    0,    -1,  42,  12,  1, 1, 1, 1, 0, 0,    -1 },
    /* 13 */ { 0.0f, 0.0f,  1000.0f, 1000,  3916,  42,  13,  1, 1, 1, 1, 0, 0,    -1 },
    /* 14 */ { 0.0f, 0.0f,  1000.0f, 1000,  3914,  42,  14,  1, 1, 1, 1, 0, 0,    -1 },
    /* 15 */ { 0.0f, 0.0f,  1000.0f, 1000,  3915,  42,  15,  1, 1, 1, 1, 0, 0,    -1 },
    /* 16 */ { 0.0f, 0.0f,  1000.0f,    0,    -1,  42,  16,  1, 1, 1, 1, 0, 0,    -1 },
    /* 17 */ { 0.0f, 0.0f,  1000.0f,    0,  3925,  42,  42,  1, 1, 1, 1, 0, 0,    -1 },
    /* 18 */ { 0.0f, 0.0f,  1000.0f,    0,  3906,  42,  25,  1, 1, 1, 1, 1, 0,    -1 },
    /* 19 */ { 0.0f, 0.0f,  1000.0f, 1000,  3913,  42,   2,  1, 1, 1, 1, 0, 1,    -1 },
    /* 20 */ { 0.0f, 0.0f,  1000.0f, 1000,  3913,  42,  41,  1, 1, 1, 1, 0, 0,    -1 },
    /* 21 */ { 0.0f, 0.0f,  1000.0f,    0,  3911,  42,  34,  1, 1, 1, 1, 0, 0,    -1 },
    /* 22 */ { 0.0f, 0.0f,  1000.0f,    0,  3912,  42,  36,  1, 1, 1, 1, 0, 0,    -1 },
    /* 23 */ { 0.0f, 0.0f,  1000.0f, 1000,    -1,  -1,  -1,  1, 1, 1, 1, 1, 1,    -1 },
    /* 24 */ { 0.0f, 0.0f,  1000.0f, 1000,    -1,  -1,  -1,  1, 1, 1, 1, 1, 1,    -1 },
    /* 25 */ { 0.0f, 0.0f,  1000.0f, 1000,    -1,  -1,  -1,  1, 1, 1, 1, 1, 1,    -1 },
    /* 26 */ { 0.0f, 0.0f,   630.0f, 1000,    -1,  -1,  -1,  1, 1, 1, 1, 1, 1,    -1 },
    /* 27 */ { 0.0f, 0.0f,  1000.0f, 1000,    -1,  -1,  -1,  1, 1, 1, 1, 1, 1,    -1 },
    /* 28 */ { 0.0f, 0.0f,  1000.0f, 1000,  3912,  42,  17,  1, 1, 1, 1, 0, 0,    -1 },
    /* 29 */ { 0.0f, 0.0f,  1000.0f, 1000,  3907,  42,  37,  1, 1, 1, 1, 0, 0,    -1 },
    /* 30 */ { 0.0f, 0.0f,  1000.0f,    0,  3908,  42,  32,  1, 1, 1, 1, 0, 0,    -1 },
    /* 31 */ { 0.0f, 0.0f,  1000.0f, 1000,  3921,  42,  27,  1, 1, 1, 1, 0, 0,    -1 },
    /* 32 */ { 0.0f, 0.0f,  1000.0f,    0,  3923,  42,  29,  1, 1, 1, 1, 1, 0,    -1 },
    /* 33 */ { 0.0f, 0.0f,  1000.0f, 1000,  3924,  42,  31,  1, 1, 1, 1, 0, 1,    -1 },
    /* 34 */ { 0.0f, 0.0f,  1500.0f,    0,    -1,  42,  12,  1, 1, 1, 1, 0, 0,    -1 },
    /* 35 */ { 0.0f, 0.0f,  1000.0f,    0,    -1,  -1,  12,  1, 1, 1, 1, 0, 0,    -1 },
    /* 36 */ { 0.0f, 0.0f, 10000.0f, 1000,  4230,  -1,  -1,  1, 1, 0, 1, 0, 0,   116 },
    /* 37 */ { 0.0f, 0.0f, 10000.0f, 1000,  4231,  -1,  -1,  1, 1, 0, 1, 0, 0,   117 },
    /* 38 */ { 0.0f, 0.0f, 10000.0f, 1000,  4232,  -1,  -1,  1, 1, 0, 1, 0, 0,   118 },
    /* 39 */ { 0.0f, 0.0f, 10000.0f, 1000,  4233,  -1,  -1,  1, 1, 0, 1, 0, 0,   119 },
    /* 40 */ { 0.0f, 0.0f, 10000.0f, 1000,  4234,  -1,  -1,  1, 1, 0, 1, 0, 0,   120 },
    /* 41 */ { 0.0f, 0.0f, 10000.0f, 1000,  4235,  -1,  -1,  1, 1, 0, 1, 0, 0,   121 },
    /* 42 */ { 0.0f, 0.0f, 10000.0f, 1000,  4236,  -1,  -1,  1, 1, 0, 1, 0, 0,   122 },
    /* 43 */ { 0.0f, 0.0f, 10000.0f, 1000,  4237,  -1,  -1,  1, 1, 0, 1, 0, 0,   123 },
    /* 44 */ { 0.0f, 0.0f, 10000.0f, 1000,  4238,  -1,  -1,  1, 1, 0, 1, 0, 0,   124 },
    /* 45 */ { 0.0f, 0.0f, 10000.0f, 1000,  4239,  -1,  -1,  1, 1, 0, 1, 0, 0,   125 },
    /* 46 */ { 0.0f, 0.0f, 10000.0f, 1000,  4240,  -1,  -1,  1, 1, 0, 1, 0, 0,   126 },
    /* 47 */ { 0.0f, 0.0f, 10000.0f, 1000,  4241,  -1,  -1,  1, 1, 0, 1, 0, 0,   127 },
    /* 48 */ { 0.0f, 0.0f, 10000.0f, 1000,  4242,  -1,  -1,  1, 1, 0, 1, 0, 0,   128 },
    /* 49 */ { 0.0f, 0.0f, 10000.0f, 1000,  4243,  -1,  -1,  1, 1, 0, 1, 0, 0,   129 },
    /* 50 */ { 0.0f, 0.0f, 10000.0f, 1000,  4244,  -1,  -1,  1, 1, 0, 1, 0, 0,   130 },
    /* 51 */ { 0.0f, 0.0f, 10000.0f, 1000,  4245,  -1,  -1,  1, 1, 0, 1, 0, 0,   131 },
    /* 52 */ { 0.0f, 0.0f, 10000.0f, 1000,  4246,  -1,  -1,  1, 1, 0, 1, 0, 0,   132 },
    /* 53 */ { 0.0f, 0.0f, 10000.0f, 1000,  4247,  -1,  -1,  1, 1, 0, 1, 0, 0,   133 },
    /* 54 */ { 0.0f, 0.0f, 10000.0f, 1000,  4248,  -1,  -1,  1, 1, 0, 1, 0, 0,   134 },
    /* 55 */ { 0.0f, 0.0f, 10000.0f, 1000,  4249,  -1,  -1,  1, 1, 0, 1, 0, 0,   135 },
    /* 56 */ { 0.0f, 0.0f, 10000.0f, 1000,  4250,  -1,  -1,  1, 1, 0, 1, 0, 0,   136 },
    /* 57 */ { 0.0f, 0.0f, 10000.0f, 1000,  4251,  -1,  -1,  1, 1, 0, 1, 0, 0,   137 },
    /* 58 */ { 0.0f, 0.0f, 10000.0f, 1000,  4252,  -1,  -1,  1, 1, 0, 1, 0, 0,   138 },
    /* 59 */ { 0.0f, 0.0f, 10000.0f, 1000,  4253,  -1,  -1,  1, 1, 0, 1, 0, 0,   139 },
    /* 60 */ { 0.0f, 0.0f, 10000.0f, 1000,  4254,  -1,  -1,  1, 1, 0, 1, 0, 0,   140 },
    /* 61 */ { 0.0f, 0.0f, 10000.0f, 1000,  4255,  -1,  -1,  1, 1, 0, 1, 0, 0,   141 },
    /* 62 */ { 0.0f, 0.0f, 10000.0f, 1000,  4256,  -1,  -1,  1, 1, 0, 1, 0, 0,   142 },
    /* 63 */ { 0.0f, 0.0f, 10000.0f, 1000,  4257,  -1,  -1,  1, 1, 0, 1, 0, 0,   143 },
    /* 64 */ { 0.0f, 0.0f, 10000.0f, 1000,  4258,  -1,  -1,  1, 1, 0, 1, 0, 0,   144 },
    /* 65 */ { 0.0f, 0.0f, 10000.0f, 1000,  4259,  -1,  -1,  1, 1, 0, 1, 0, 0,   145 },
    /* 66 */ { 0.0f, 0.0f, 10000.0f, 1000,  4260,  -1,  -1,  1, 1, 0, 1, 0, 0,   146 },
    /* 67 */ { 0.0f, 0.0f, 10000.0f, 1000,  4261,  -1,  -1,  1, 1, 0, 1, 0, 0,   147 },
    /* 68 */ { 0.0f, 0.0f, 10000.0f, 1000,  4262,  -1,  -1,  1, 1, 0, 1, 0, 0,   148 },
    /* 69 */ { 0.0f, 0.0f, 10000.0f, 1000,  4263,  -1,  -1,  1, 1, 0, 1, 0, 0,   149 },
    /* 70 */ { 0.0f, 0.0f, 10000.0f, 1000,  4264,  -1,  -1,  1, 1, 0, 1, 0, 0,   150 },
    /* 71 */ { 0.0f, 0.0f, 10000.0f, 1000,  4265,  -1,  -1,  1, 1, 0, 1, 0, 0,   151 },
};

/* The hint texture the finder overlays on a photographable object.  Slot 0 is
 * left blank and the other three are the same 384x256 sprite -- the hint code
 * that picks between them is in the unreconstructed camera HUD. */
SPRT_DAT hint_dat[4] =                                      /* data 33cb50 */
{
    /*     tex0                    u   v    w    h   x  y  pri  alp flip bln */
    { 0x0000000000000000ULL,       0,  0,   0,   0,  0, 0,   0,   0,  0,  0 },
    { 0x2007df822531bcfcULL,       0,  0, 384, 256,  0, 0,   0, 128,  0,  1 },
    { 0x2007df822531bcfcULL,       0,  0, 384, 256,  0, 0,   0, 128,  0,  1 },
    { 0x2007df822531bcfcULL,       0,  0, 384, 256,  0, 0,   0, 128,  0,  1 },
};

int GetPhotoDatNum(void)
{
    return PHOTO_DAT_NUM;                                                        /* 71 */
}

/* --------------------------------------------------------------------------
 *  The "already photographed" bit set.
 *
 *  Three words, and the whole thing goes into the memory-card block verbatim.
 * ------------------------------------------------------------------------ */
static BIT_FLAGS<PHOTO_DAT_NUM> photo_dat_save;             /* bss 4bbb40 */    /* 76 */

void photo_datSetSave(MC_SAVE_DATA *save)
{
    save->addr = (u_char *)&photo_dat_save;                                      /* 79 */
    save->size = sizeof(photo_dat_save);                                         /* 80 */
}

void photo_datInit(void)
{
    photo_dat_save.AllDown();
}

void photo_datFlgUp(int photo_dat_no)
{
    photo_dat_save.FlgUp(photo_dat_no);                                          /* 93 */
}

int photo_datIsUp(int photo_dat_no)
{
    return photo_dat_save.IsUp(photo_dat_no);                                    /* 97 */
}

void photo_datFlgDown(int photo_dat_no)
{
    photo_dat_save.FlgDown(photo_dat_no);                                        /* 101 */
}

/* --------------------------------------------------------------------------
 *  Parts-deform parameters.
 *
 *  SetEffects_PDEFORM() takes these by address, not by value, so the effect keeps
 *  reading them for as long as it lives -- which is why they are file statics
 *  rewritten just before each call rather than locals.  The `pdb_` pair is the
 *  sealed ghost's first (undistorted) pass.
 * ------------------------------------------------------------------------ */
static float pd_default_spd   = 1.0f;                       /* sdata 3f3870 */
static float pd_default_rate  = 1.0f;                       /* sdata 3f3874 */
static float pdb_default_spd  = 1.0f;                       /* sdata 3f3878 */
static float pdb_default_rate = 1.0f;                       /* sdata 3f387c */

/* --------------------------------------------------------------------------
 *  The live registrations.
 * ------------------------------------------------------------------------ */
static PHOTO_DAT_OBJ_WRK pd_obj_wrk[PHOTO_DAT_OBJ_NUM];     /* bss 4bbb50 */    /* 132 */

/* The most centred registered object this frame, and the one-frame suppression
 * of the sealed-ghost fade. */
static MDAT_OBJ *p_centerest_obj;                           /* sbss 3f4ed8 */
static int       seal_ghost_draw_lock;                      /* sbss 3f4edc */

void photo_datObjStart(MDAT_OBJ *p_obj)
{                                                                                /* 137 */
    for (int i = 0; i < PHOTO_DAT_OBJ_NUM; i++)                                  /* 138 */
    {
        if (pd_obj_wrk[i].p_obj == NULL)                                         /* 139 */
        {
            pd_obj_wrk[i].p_obj = p_obj;                                         /* 143 */
            pd_obj_wrk[i].mGhostAlpha.Init();

            return;                                                              /* 146 */
        }
    }                                                                            /* 148 */

    PRINT_ASSERT("photo_datObjStart Cannot Get Wrk");                            /* 150 */
}

/* The two proximity-hint SE voices.  Both are started and stopped together, so
 * the pair really behaves as one two-layer cue. */
CSYSTEM_SND_BUF_PLAY furn_sound_player[2];                  /* sdata 3f3880 */  /* 157 */

void photo_datRelease(void)
{
}                                                                                /* 162 */

/* Drops both voices over a single frame. */
static void furn_soundStop(void)
{
    for (int i = 0; i < 2; i++)                                                  /* 167 */
    {
        furn_sound_player[i].Stop(1);
    }                                                                            /* 169 */
}

/* --------------------------------------------------------------------------
 *  Proximity hint SE.
 *
 *  The nearest registered object gets a two-voice cue whose volume ramps from
 *  silence at 1000 units to full at zero.  Already-playing voices are faded to
 *  the new volume; silent ones are started, at SE 14 for a ghost-list subject
 *  and SE 12 for anything else.
 * ------------------------------------------------------------------------ */
void CheckHintSE(MDAT_OBJ *plyr_wrk_nearest_furn, float *plyr_wrk_nearest_furn_pos)
{
    float dist = 1000.0f;                                                        /* 174 */
    float f;
    int   pow;
    int   i;

    if (plyr_wrk_nearest_furn != NULL &&                                         /* 179 */
        photo_dat[plyr_wrk_nearest_furn->PhotoAble].f_sound != 0)
    {
        f = GetDistV(plyr_wrk.cmn_wrk.mbox.pos, plyr_wrk_nearest_furn_pos);      /* 180 */

        if (dist < f)                                                            /* 181 */
        {
            furn_soundStop();                                                    /* 182 */
            return;
        }

        pow = (int)((dist - f) * 16383.0f / dist);                               /* 184 */

        for (i = 0; i < 2; i++)                                                  /* 186 */
        {
            if (furn_sound_player[i].IsPlaying())
            {
                furn_sound_player[i].Fade(pow, 1);
            }
            /* The ROM loads ghost_list_rel_no with lhu and compares it with
             * sltiu, so the -1 that every non-ghost entry carries reads as
             * 65535 and takes the else branch.  types.txt types the member
             * `short int`, hence the cast -- without it -1 would compare below
             * 177 and every piece of furniture would get the ghost cue. */
            else if ((u_short)photo_dat[plyr_wrk_nearest_furn->PhotoAble]         /* 192 */
                         .ghost_list_rel_no < 177)
            {
                furn_sound_player[i].Play(14, 1, 1, 0, NULL, pow, 0x1000);
            }
            else
            {
                furn_sound_player[i].Play(12, 1, 1, 0, NULL, pow, 0x1000);
            }
        }                                                                        /* 199 */
    }
    else
    {
        furn_soundStop();                                                        /* 202 */
    }
}

/* The furniture half of the shot's power score.  The ROM's body is gone -- the
 * function is three instructions and returns a hard zero, so furniture
 * contributes nothing in the prototype. */
float photo_datGetFurnPowerDegree(void)
{
    return 0.0f;                                                                 /* 210 */
}

void photo_datObjInit(void)
{
    for (int i = 0; i < PHOTO_DAT_OBJ_NUM; i++)                                  /* 215 */
    {
        pd_obj_wrk[i].p_obj = NULL;                                              /* 216 */
    }                                                                            /* 217 */

    p_centerest_obj      = NULL;                                                 /* 218 */
    seal_ghost_draw_lock = 0;                                                    /* 219 */
}

/* The ROM hands the whole registration array to the memory card, live MDAT_OBJ
 * pointers and all -- 0x80 bytes there.  PHOTO_DAT_OBJ_WRK is 0x28 on the host
 * because those two pointers widen, so the block reported here is 0xa0.  The
 * saved pointers are meaningless either way; nothing reads this block back
 * yet, and no caller of photo_datObjSetSave() exists in the port. */
void photo_datObjSetSave(MC_SAVE_DATA *save)
{
    save->addr = (u_char *)pd_obj_wrk;                                           /* 224 */
    save->size = sizeof(pd_obj_wrk);                                             /* 225 */
}

/* --------------------------------------------------------------------------
 *  Per-frame re-scoring of every registered object.
 *
 *  One pass produces three answers: p_nearest_obj (nearest in the horizontal
 *  plane -- the hint SE and the filament), p_centerest_obj (closest to the
 *  finder centre among those actually inside the frame and the ring -- the
 *  shot target), and the per-object effect state.
 *
 *  Note the ring test only runs while the finder is up, so the sealed-ghost
 *  fade freezes wherever it was the moment the camera comes down.
 * ------------------------------------------------------------------------ */
void photo_datObjMain(void)
{                                                                                /* 230 */
    MDAT_OBJ  *p_nearest_obj = NULL;                                             /* 231 */
    float      nearest_pos[4];
    float      min2d = 9999.0f;                                                  /* 234 */
    float      min3d = 100000.0f;                                                /* 235 */
    float      dist;
    float      dist2d;
    PhotoData *ppd;
    short      iValue;

    p_centerest_obj = NULL;
    m_plyr_camera.filament.SetHint(0.0f);                                        /* 238 */
    m_plyr_camera.center_circle.SetHintFlg(0);                                   /* 239 */

    for (int i = 0; i < PHOTO_DAT_OBJ_NUM; i++)                                  /* 241 */
    {
        /* The ROM's G3DASSERT below stringifies its condition as
         * "p_obj->PhotoAble ...", so the record is held in a named local even
         * though functions.txt does not list one -- GCC coalesced it away. */
        MDAT_OBJ *p_obj = pd_obj_wrk[i].p_obj;

        if (p_obj == NULL)                                                       /* 246 */
        {
            continue;
        }

        if (p_obj->PhotoAble == 0)                                               /* 249 */
        {
            continue;
        }

        G3DASSERT((p_obj->PhotoAble < PHOTO_DAT_NUM && p_obj->PhotoAble >= 0),    /* 251 */
                  "Illegal Val");

        ppd = &photo_dat[p_obj->PhotoAble];                                      /* 252 */

        pd_obj_wrk[i].pos[0] = p_obj->Pos[0];                                    /* 255 */
        pd_obj_wrk[i].pos[1] = p_obj->Pos[1];                                    /* 256 */
        pd_obj_wrk[i].pos[2] = p_obj->Pos[2];                                    /* 257 */
        pd_obj_wrk[i].pos[3] = 1.0f;                                             /* 258 */

        /* GetDistV() is the horizontal distance despite the local's name. */
        dist = GetDistV(plyr_wrk.cmn_wrk.mbox.pos, pd_obj_wrk[i].pos);           /* 263 */

        if (dist < min3d)                                                        /* 264 */
        {
            min3d = dist;                                                        /* 267 */
            g3dxVu0CopyVector(nearest_pos, pd_obj_wrk[i].pos);
            p_nearest_obj = p_obj;                                               /* 269 */
        }

        if (PlayerModeIsFinder())                                                /* 272 */
        {
            /* PI is the sight cone's full width, and OutSightChk() halves it,
             * so the angular gate is +/-90 degrees off the player's facing and
             * the frame test does the real work.  ppd->Dist is the range. */
            if (InFinderFrameSub(pd_obj_wrk[i].pos, &dist2d,                     /* 274 */
                                 3.1415925f, ppd->Dist) != 0 &&
                dist2d <= m_plyr_camera.camera_power_up.GetRadius()) /* 275 */
            {
                if (dist2d < min2d)                                              /* 276 */
                {
                    min2d           = dist2d;                                    /* 277 */
                    p_centerest_obj = p_obj;                                     /* 279 */
                }

                m_plyr_camera.center_circle.SetHintFlg(1);                       /* 282 */
                iValue = 4;
            }
            else
            {
                iValue = -4;
            }

            pd_obj_wrk[i].mGhostAlpha.SetAddVal(iValue);

            if (ppd->f_seal_ghost != 0 && seal_ghost_draw_lock == 0)             /* 289 */
            {
                pd_obj_wrk[i].mGhostAlpha.Work();                                /* 292 */
                SetEffects_DOOR_SEAL(1, pd_obj_wrk[i].pos,                       /* 294 */
                                     pd_obj_wrk[i].mGhostAlpha.Get() / 128.0f);
            }
        }

        if (ppd->f_deform != 0)                                                  /* 299 */
        {
            if (ppd->f_seal_ghost != 0)                                          /* 300 */
            {
                /* Two passes for a sealed ghost: a still one that just tints
                 * the silhouette, then the moving distortion over it. */
                pdb_default_spd  = 0.0f;                                         /* 301 */
                pdb_default_rate = 0.0f;                                         /* 302 */
                SetEffects_PDEFORM(1, 0x13, 0x32, 0.4f, 0.8f,
                                   pd_obj_wrk[i].pos, 0, 0, 0, nullptr,
                                   &pdb_default_spd, &pdb_default_rate, nullptr,
                                   0x80, 0x80, 0x80);                            /* 314 */

                pd_default_spd  = 0.97f;                                         /* 316 */
                pd_default_rate = 0.9733333f;                                    /* 317 */
                SetEffects_PDEFORM(1, 0x18, 0x61, 0.4f, 0.8f,
                                   pd_obj_wrk[i].pos, 0, 0, 0, nullptr,
                                   &pd_default_spd, &pd_default_rate, nullptr,
                                   0x82, 0x91, 0x9b);                            /* 329 */
            }
            else
            {
                pd_default_spd  = 1.0f;                                          /* 331 */
                pd_default_rate = 1.0f;                                          /* 332 */
                SetEffects_PDEFORM(1, 0x17, 0x37, 0.5f, 1.0f,
                                   pd_obj_wrk[i].pos, 0, 0, 0, nullptr,
                                   &pd_default_spd, &pd_default_rate, nullptr,
                                   0x60, 0x90, 0xa0);                            /* 344 */
            }
        }
    }                                                                            /* 351 */

    /* nearest_pos is only meaningful when p_nearest_obj is set; CheckHintSE()
     * tests the pointer before it reads the position, which is what makes
     * passing it unconditionally safe. */
    CheckHintSE(p_nearest_obj, nearest_pos);                                     /* 357 */

    if (p_nearest_obj != NULL)                                                   /* 359 */
    {
        m_plyr_camera.filament.SetHint(CulcEP3(nearest_pos));                    /* 361 */
    }

    /* The lock is a single frame's suppression, released as soon as the finder
     * comes down rather than by whoever took it. */
    if (PlayerModeIsFinder() == 0)                                               /* 364 */
    {
        seal_ghost_draw_lock = 0;
    }
}                                                                                /* 367 */

MDAT_OBJ *photo_datObjIsPhotoAble(void)
{
    return p_centerest_obj;                                                      /* 374 */
}

int photo_datObjIsRespondFilament(void)
{
    if (p_centerest_obj != NULL &&                                               /* 380 */
        photo_dat[p_centerest_obj->PhotoAble].f_finder != 0)
    {
        return 1;
    }

    return 0;                                                                    /* 384 */
}

void photo_datObjRelease(void)
{
    furn_soundStop();                                                            /* 390 */
}

void photo_datObjFadeOutSE(int iFrame)
{
    for (int i = 0; i < 2; i++)                                                  /* 396 */
    {
        furn_sound_player[i].Stop(iFrame);
    }                                                                            /* 398 */
}

void photo_datObjEnd(MDAT_OBJ *p_obj)
{
    for (int i = 0; i < PHOTO_DAT_OBJ_NUM; i++)                                  /* 404 */
    {
        if (pd_obj_wrk[i].p_obj == p_obj)                                        /* 405 */
        {
            pd_obj_wrk[i].p_obj = NULL;
            return;
        }
    }
}                                                                                /* 413 */

void photo_datObjSealGhostDrawLock(void)
{
    seal_ghost_draw_lock = 1;                                                    /* 426 */
}
