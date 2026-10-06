// FILE: /home/zero_rom/zero2np/src/ingame/enemy/ene_mot_ctrl.c
//
// The ghost animation event track.
//
// Four functions and about a page of tables.  Each animated ghost model gets
// a list of clips, each clip a list of (frame, event) pairs; once per frame
// EneMotAlgCtrl() works out which pairs the clip stepped over since last time
// and hands them to SetEneMotAttr(), which raises or drops a status bit or
// fires a sound.  This is where a ghost's shutter-chance and fatal-frame
// windows come from -- they are authored into the animation, not the script.
//
// Only four models carry a track: 19, 21, 25 and 31.  Every other ghost has a
// null entry in ene_mot_char_tbl and leaves this module immediately.
//
// InitEneMotAlgCtrl() and ClearEneMotAttr() are exported but have no call site
// anywhere in the ROM (checked by scanning every jal in the loadable segments,
// not by Ghidra xrefs).  They are reconstructed because the object file
// defines them.
//
// Reconstructed from the Feb 6 2004 prototype (SLES_523.84), ene_mot_ctrl.o.
// Trailing /* NNN */ comments are the original source line numbers.

#include "ene_mot_ctrl.h"

#include <string.h>                             /* memset                      */

#include "eetypes.h"
#include "enemy.h"                              /* ENE_WRK                     */
#include "../../graphics/graph3d/ctl/fixed_array.h"
#include "../../graphics/motion/mdlwork.h"      /* ANI_CTRL                    */
#include "../../system/eeiop/snd3d.h"           /* SND_3D_SET                  */
#include "../../system/eeiop/sndbank.h"         /* SndBankPlay                 */

/* --------------------------------------------------------------------------
 *  Tracks
 *
 *  Two index conventions, and neither is the model number:
 *
 *  - ene_mot_char_tbl[] is indexed by ENE_DAT_COMMON::anm_no, the animation
 *    pak, which runs two ahead of the model for these four ghosts.  jene_dat
 *    entry 0 is mdl_no 19 / anm_no 21, which is why enemot_ch019 sits at 21.
 *
 *  - enemot_chNNN[] is indexed by ANI_CTRL::mot.play_id, the clip's file index
 *    inside that pak.  The anmNNN in the ROM's own symbol names is the source
 *    clip number and does NOT equal the slot: clip 002 is slot 1 on both
 *    model 19 and model 31, and enemot_ch031anm014 is slot 9.  The placement
 *    below is read straight off the .data pointers (0x2fde40..0x2fe024), so
 *    do not "correct" it to match the names.
 *
 *  Every track ends with a frm == -1 sentinel; attr and sub are ignored there.
 * ----------------------------------------------------------------------- */

/* Model 19 -- one sound on the opening frame of the clip in slot 1. */
static ENE_MOT_WRK enemot_ch019anm002[2] =                  /* data 2fde40 */
{
    { 6,  0, 8 },
    { 0, -1, 0 }
};

static ENE_MOT_WRK *enemot_ch019[17] =                      /* data 2fde50 */
{
    nullptr,  enemot_ch019anm002,  nullptr,  nullptr,  nullptr,  nullptr,
    nullptr,  nullptr,  nullptr,  nullptr,  nullptr,  nullptr,  nullptr,
    nullptr,  nullptr,  nullptr,  nullptr
};

/* Model 21 -- three clips, each firing sounds 7 and 8 together on one frame. */
static ENE_MOT_WRK enemot_ch021anm010[3] =                  /* data 2fde98 */
{
    { 6, 86, 7 },
    { 6, 86, 8 },
    { 0, -1, 0 }
};

static ENE_MOT_WRK enemot_ch021anm011[3] =                  /* data 2fdeb0 */
{
    { 6, 62, 7 },
    { 6, 62, 8 },
    { 0, -1, 0 }
};

static ENE_MOT_WRK enemot_ch021anm012[3] =                  /* data 2fdec8 */
{
    { 6, 51, 7 },
    { 6, 51, 8 },
    { 0, -1, 0 }
};

static ENE_MOT_WRK *enemot_ch021[13] =                      /* data 2fdee0 */
{
    nullptr,  nullptr,  nullptr,  nullptr,  nullptr,  nullptr,
    enemot_ch021anm010,  enemot_ch021anm011,  enemot_ch021anm012,
    nullptr,  nullptr,  nullptr,  nullptr
};

/* Model 25 -- the only track in the game that opens a shutter-chance window
 * rather than playing a sound: frames 35 through 55 of clip 12. */
static ENE_MOT_WRK enemot_ch025anm016[3] =                  /* data 2fdf18 */
{
    { 1, 35, 0 },
    { 0, 55, 0 },
    { 0, -1, 0 }
};

static ENE_MOT_WRK *enemot_ch025[13] =                      /* data 2fdf30 */
{
    nullptr,  nullptr,  nullptr,  nullptr,  nullptr,  nullptr,  nullptr,
    nullptr,  nullptr,  nullptr,  nullptr,  nullptr,  enemot_ch025anm016
};

/* Model 31 -- eight clips, one sound each. */
static ENE_MOT_WRK enemot_ch031anm000[2] =                  /* data 2fdf68 */
{
    { 6, 80, 7 },
    { 0, -1, 0 }
};

static ENE_MOT_WRK enemot_ch031anm002[2] =                  /* data 2fdf78 */
{
    { 6, 56, 7 },
    { 0, -1, 0 }
};

static ENE_MOT_WRK enemot_ch031anm014[2] =                  /* data 2fdf88 */
{
    { 6, 135, 7 },
    { 0,  -1, 0 }
};

static ENE_MOT_WRK enemot_ch031anm015[2] =                  /* data 2fdf98 */
{
    { 6, 81, 7 },
    { 0, -1, 0 }
};

static ENE_MOT_WRK enemot_ch031anm016[2] =                  /* data 2fdfa8 */
{
    { 6, 100, 7 },
    { 0,  -1, 0 }
};

static ENE_MOT_WRK enemot_ch031anm017[2] =                  /* data 2fdfb8 */
{
    { 6, 150, 7 },
    { 0,  -1, 0 }
};

static ENE_MOT_WRK enemot_ch031anm018[2] =                  /* data 2fdfc8 */
{
    { 6, 82, 7 },
    { 0, -1, 0 }
};

static ENE_MOT_WRK enemot_ch031anm019[2] =                  /* data 2fdfd8 */
{
    { 6, 90, 7 },
    { 0, -1, 0 }
};

static ENE_MOT_WRK *enemot_ch031[15] =                      /* data 2fdfe8 */
{
    enemot_ch031anm000,  enemot_ch031anm002,
    nullptr,  nullptr,  nullptr,  nullptr,  nullptr,  nullptr,  nullptr,
    enemot_ch031anm014,  enemot_ch031anm015,  enemot_ch031anm016,
    enemot_ch031anm017,  enemot_ch031anm018,  enemot_ch031anm019
};

/* rodata 3a86b0.  const because that is the section the ROM put it in; the
 * cast below is only needed because reference_fixed_array<ENE_MOT_WRK**,63>
 * stores a plain ENE_MOT_WRK *** -- GCC 2.96-ee took the conversion silently. */
static ENE_MOT_WRK **const ene_mot_char_tbl_dat[ENE_MOT_CHAR_MAX] =
{
    nullptr,  nullptr,  nullptr,  nullptr,  nullptr,  nullptr,  nullptr,
    nullptr,  nullptr,  nullptr,  nullptr,  nullptr,  nullptr,  nullptr,
    nullptr,  nullptr,  nullptr,  nullptr,  nullptr,  nullptr,  nullptr,
    enemot_ch019,                                           /* anm_no 21 */
    nullptr,
    enemot_ch021,                                           /* anm_no 23 */
    nullptr,  nullptr,  nullptr,
    enemot_ch025,                                           /* anm_no 27 */
    nullptr,  nullptr,  nullptr,  nullptr,  nullptr,
    enemot_ch031,                                           /* anm_no 33 */
    nullptr,  nullptr,  nullptr,  nullptr,  nullptr,  nullptr,  nullptr,
    nullptr,  nullptr,  nullptr,  nullptr,  nullptr,  nullptr,  nullptr,
    nullptr,  nullptr,  nullptr,  nullptr,  nullptr,  nullptr,  nullptr,
    nullptr,  nullptr,  nullptr,  nullptr,  nullptr,  nullptr,  nullptr,
    nullptr
};

/* Bound at static-init time, which is the single .ctors entry (0x2c3b74) this
 * object contributes: __static_initialization_and_destruction_0 stores
 * &ene_mot_char_tbl_dat into ene_mot_char_tbl.m_aData. */
static reference_fixed_array<ENE_MOT_WRK **, ENE_MOT_CHAR_MAX>
    ene_mot_char_tbl(const_cast<ENE_MOT_WRK ***>(ene_mot_char_tbl_dat));

/* One playback cursor per ghost slot, indexed by ENEALG_WRK::idx. */
static fixed_array<ENE_MOT_CTRL, ENE_WRK_MAX> ene_mot_ctrl;  /* bss 478720 */

/* --------------------------------------------------------------------------
 *  Playback
 * ----------------------------------------------------------------------- */

/* Force the next EneMotAlgCtrl() on this slot to replay the whole track from
 * the top.  Dead code in this build -- EneMotAlgCtrl's own restart test
 * (frm < old_frm) already covers a clip change in practice. */
void InitEneMotAlgCtrl(ENE_WRK *ew)                                     /* 257 */
{
    ene_mot_ctrl[ew->alg.idx].old_mot = -1;
    ene_mot_ctrl[ew->alg.idx].old_frm = -1;
}

void EneMotAlgCtrl(ENE_WRK *ew)                                         /* 263 */
{
    ANI_CTRL     *anc = ew->ani_ctrl_p;
    ENE_MOT_WRK  *emw;
    ENE_MOT_CTRL *emc;
    int           anm;
    int           mot;
    int           frm;
    int           old_frm;

    /* Tracks belong to ene_type 0 only.  Type 1 shares jene_dat and type 2 is
     * the passive table, and neither is given one. */
    if (ew->type != 0) { return; }                                      /* 272 */

    anm = ew->cmn_dat->anm_no;                                          /* 274 */

    if (anm >= ENE_MOT_CHAR_MAX)          { return; }                   /* 276 */
    if (ene_mot_char_tbl[anm] == nullptr) { return; }

    emc = &ene_mot_ctrl[ew->alg.idx];

    frm     = anc->mot.cnt;                                             /* 283 */
    old_frm = emc->old_frm;                                             /* 285 */
    mot     = anc->mot.play_id;

    emw = ene_mot_char_tbl[anm][mot];

    /* A clip with no track still falls through to the cursor update below, so
     * old_frm follows the untracked clip too. */
    if (emw != nullptr)                                                 /* 292 */
    {
        /* The clip looped or a new one started: treat every event as unseen.
         * Note this keys off the frame going backwards rather than off
         * old_mot, which is written but never read. */
        if (frm < old_frm) { old_frm = -1; }                            /* 295 */

        while (emw->frm != -1)                                          /* 298 */
        {
            /* Fire everything the clip stepped over -- at or before this
             * frame, and past the one handled last time.  The whole track is
             * walked; it is short and not required to be sorted. */
            if (emw->frm <= frm && old_frm < emw->frm)                  /* 301 */
            {
                SetEneMotAttr(ew, emw);                                 /* 304 */
            }

            emw++;                                                      /* 307 */
        }
    }

    emc->old_mot = mot;                                                 /* 310 */
    emc->old_frm = frm;                                                 /* 311 */
}                                                                       /* 314 */

void ClearEneMotAttr(ENE_WRK *ew)                                       /* 317 */
{
    ew->st.sta &= ~0x1000L;                                             /* 318 */
    ew->st.sta &= ~0x2000L;                                             /* 319 */
    ew->st.sta &= ~0x80000L;                                            /* 320 */
}

void SetEneMotAttr(ENE_WRK *ew, ENE_MOT_WRK *emw)                       /* 323 */
{
    SND_3D_SET s3d;

    switch (emw->attr)                                                  /* 324 */
    {
    case 0:
        ew->st.sta &= ~0x1000L;                                         /* 326 */
        break;
    case 1:
        ew->st.sta |= 0x1000L;                                          /* 329 */
        break;
    case 2:
        ew->st.sta &= ~0x2000L;                                         /* 332 */
        break;
    case 3:
        ew->st.sta |= 0x2000L;                                          /* 335 */
        break;
    case 4:
        ew->st.sta &= ~0x80000L;                                        /* 338 */
        break;
    case 5:
        ew->st.sta |= 0x80000L;                                         /* 341 */
        break;
    case 6:
        /* The ROM passes a literal 0xc here, which is sizeof(SND_3D_SET) with
         * 4-byte pointers; on the host the struct is twice that, so the size
         * has to be taken rather than copied. */
        memset(&s3d, 0, sizeof(s3d));                                   /* 344 */
        s3d.pos = &ew->mbox.pos;                                        /* 345 */
        SndBankPlay(ew->se_bank_no, emw->sub, 0, 0, 0x3200, 0x1000, 0, &s3d);
                                                                        /* 346 */
        break;
    }
}                                                                       /* 347 */
