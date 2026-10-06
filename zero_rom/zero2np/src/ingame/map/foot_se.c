// FILE: /home/zero_rom/zero2np/src/ingame/map/foot_se.c
//
// Footstep sound banks.  Twelve slots, each holding one SndBank; a room says
// which surfaces it uses and the slots are re-cut on every room change so that
// both the room being left and the room being entered stay playable across the
// door transition.
//
// Reconstructed from the Feb 6 2004 prototype (SLES_523.84), foot_se.o
// 0x001969c0..0x0019738b.  .text is fully accounted for: 0x9b8 of code (the
// four fixed_array template bodies at the head of the object plus the seven
// functions ZERO2.MAP and functions.txt name) and five 4-byte alignment fills.
//
// Four of the bodies here are static helpers GCC inlined at every call site,
// so they carry no symbol and ZERO2.MAP does not list them.  Their existence
// and extent are certain -- symbols.txt brackets each expansion with a
// $LBB/$LBE pair and the out-of-order `; Line` runs (74..77, 245..254,
// 259..265, 269..274) are their bodies -- but their *names* are not
// recoverable.  The ones used below follow the file's own `4' convention;
// their parameter and local names (`no', `file_no', `i') are the ROM's, out of
// the RSYM stabs that the inliner hoisted into each caller's local list.
//
// The line annotations start at foot_seGetSeStat4RegID (line 82).  Nothing
// above it is annotated: se_footDatList's 280 rows have to have been packed
// several per line in the original to fit under line 74, and guessing at that
// layout would be worse than leaving it alone.

#include "foot_se.h"

#include "map_rectangle.h"                      /* MrecSetSEInfo / MrecGetSeNo */

#include "../../common/utility2.h"              /* PRINT_ASSERT */
#include "../../system/eeiop/cddat.h"           /* FOOT0nn_*_BD */
#include "../../system/eeiop/sndbank.h"         /* SndBank* / SND_3D_SET */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Resident bank slots.  The assert text in foot_seSetNewFiles() names this
 * constant. */
#define WRK_MAX             12

/* se_footDatList[] is exactly full: 279 rows of data plus the terminator.  The
 * ROM compares against the literal 280. */
#define SE_FOOT_DAT_MAX     280

/* Surface ids run 0..17 and index foot_se_label_tbl[]. */
#define SE_LABEL_MAX        18

/* --------------------------------------------------------------------------
 *  Per-room surface lists.
 *
 *  One flat array of int pairs rather than a table of records, walked
 *  linearly.  A pair whose second element is -1 is a *header*: its first
 *  element is the room id.  Every pair after it, up to the next header, is a
 *  surface the room uses -- first element the surface id (0..17, an index into
 *  foot_se_label_tbl[]), second element an authoring tag that no code in the
 *  ROM ever reads; it only has to be something other than -1 so the walk can
 *  tell data from a header.  { -1, -1 } ends the table.
 *
 *  Room ids run 0..65 with no gaps.  Rooms 1, 3, 5, 7, 10, 19, 21 and 63 have
 *  a header and no surfaces at all, which is how a room says "no footstep
 *  sounds"; they are not holes in the table.
 * ------------------------------------------------------------------------ */
static int se_footDatList[SE_FOOT_DAT_MAX][2] =                     /* data 313ee0 */
{
    {  0, -1 }, { 15,  0 }, { 11,  1 },
    {  1, -1 },
    {  2, -1 }, { 15,  0 }, { 11,  1 }, {  1,  3 }, {  0,  4 }, {  2,  4 },
    {  3, -1 },
    {  4, -1 }, { 15,  0 }, {  1,  1 }, {  0,  4 },
    {  5, -1 },
    {  6, -1 }, { 15,  0 }, { 12,  1 }, {  0,  1 },
    {  7, -1 },
    {  8, -1 }, {  0,  0 }, {  1,  1 },
    {  9, -1 }, { 15,  0 }, {  8,  2 },
    { 10, -1 },
    { 11, -1 }, { 15,  2 }, { 13,  0 }, { 14,  1 }, {  1,  0 }, {  0,  2 },
    { 12, -1 }, { 15,  0 }, { 11,  1 }, { 16,  2 }, {  8,  3 },
    { 13, -1 }, { 13,  0 }, {  2,  2 }, {  4,  3 }, {  5,  4 }, {  3,  1 }, {  7,  0 }, {  0,  0 },
    { 14, -1 }, { 13,  0 }, {  7,  2 }, {  6,  4 }, {  0,  3 }, {  4,  3 }, {  5,  3 },
    { 15, -1 }, {  5,  0 }, {  7,  1 }, { 17,  4 },
    { 16, -1 }, {  5,  0 }, {  4,  1 }, {  7,  2 }, { 17,  4 }, {  6,  1 },
    { 17, -1 }, { 17,  9 }, {  5,  0 }, {  3,  1 }, {  7,  2 }, {  4,  2 },
    { 18, -1 }, {  7,  0 }, {  5,  1 }, {  4,  2 }, { 17,  4 },
    { 19, -1 },
    { 20, -1 }, {  0,  2 }, {  7,  0 }, {  4,  2 }, {  5,  1 }, { 17,  2 }, {  6,  2 },
    { 21, -1 },
    { 22, -1 }, {  5,  2 }, {  7,  0 }, {  4,  2 }, { 17,  3 },
    { 23, -1 }, { 17, 10 }, {  5,  0 }, {  7,  1 }, {  4,  2 },
    { 24, -1 }, {  7,  0 }, {  4,  2 },
    { 25, -1 }, { 14,  0 }, {  4,  2 },
    { 26, -1 }, {  7,  0 }, {  4,  2 }, {  6,  2 }, { 17, 10 }, {  5,  1 },
    { 27, -1 }, {  7,  0 }, {  4,  2 }, {  6,  2 }, {  5,  1 }, { 17,  2 },
    { 28, -1 }, {  7,  0 }, {  4,  2 }, {  6,  2 }, { 17, 10 }, {  5,  1 },
    { 29, -1 }, {  5,  0 }, {  4,  2 }, { 17,  2 }, {  7, 10 },
    { 30, -1 }, {  8,  0 }, {  4,  2 },
    { 31, -1 }, { 14,  0 }, { 13,  2 }, {  4,  2 }, {  6,  3 },
    { 32, -1 }, { 17, 10 }, {  8,  0 }, {  5,  0 }, {  4,  2 },
    { 33, -1 }, {  5,  1 }, {  7,  0 }, {  4,  2 }, { 17,  3 },
    { 34, -1 }, {  5,  0 }, {  7,  1 }, {  4,  2 }, { 17,  2 },
    { 35, -1 }, {  7,  0 }, {  8,  2 }, {  4,  2 },
    { 36, -1 }, {  7, 10 }, {  5,  0 }, {  4,  2 },
    { 37, -1 }, {  7,  0 }, {  6,  2 }, {  4,  2 }, {  9,  2 }, {  2,  2 },
    { 38, -1 }, {  7,  0 }, {  6,  2 }, {  4,  2 }, {  0, 10 }, { 17, 10 }, {  5,  2 },
    { 39, -1 }, {  7,  0 }, {  6,  2 }, {  4,  2 }, {  5,  1 }, { 10,  2 }, {  2,  3 },
    { 40, -1 }, {  1,  1 }, {  0,  2 }, { 10,  0 }, { 11,  2 },
    { 41, -1 }, {  1,  0 }, {  0,  3 }, {  7,  1 }, {  5,  0 }, {  8,  1 }, {  4,  3 }, {  2,  2 },
    { 42, -1 }, {  7,  1 }, {  5,  0 }, {  4,  2 }, { 14,  1 },
    { 43, -1 }, { 14,  1 }, {  5,  0 }, {  4,  2 },
    { 44, -1 }, { 14,  0 }, {  4,  2 }, {  6,  1 },
    { 45, -1 }, {  7,  1 }, {  5,  0 }, {  6,  2 }, {  4,  3 }, {  2,  2 }, {  9,  2 },
    { 46, -1 }, {  6,  2 }, {  1,  0 }, {  4,  3 }, {  5,  1 }, {  7,  0 },
    { 47, -1 }, {  6,  2 }, {  8,  0 }, {  4,  3 }, {  5,  1 }, { 14,  3 },
    { 48, -1 }, {  8,  0 },
    { 49, -1 }, {  8,  2 }, {  6,  3 }, {  3,  0 }, { 14,  2 },
    { 50, -1 }, {  5,  0 }, {  4,  2 },
    { 51, -1 }, {  7,  0 }, {  5,  1 }, {  8,  0 }, {  4,  3 }, { 17,  5 },
    { 52, -1 }, {  5,  0 }, {  4,  2 },
    { 53, -1 }, {  1,  0 }, {  0,  2 }, {  4,  3 }, {  5,  2 },
    { 54, -1 }, {  7,  0 }, {  4,  1 }, {  5,  2 },
    { 55, -1 }, { 14,  0 },
    { 56, -1 }, { 15,  0 },
    { 57, -1 }, {  5,  0 }, {  7,  1 }, {  6,  2 }, {  3,  2 }, {  2,  2 }, { 17,  2 },
    { 58, -1 }, {  1,  0 }, {  0,  2 },
    { 59, -1 }, {  0,  2 }, {  1,  0 },
    { 60, -1 }, {  1,  0 },
    { 61, -1 }, {  1,  0 },
    { 62, -1 }, {  3,  5 }, {  1,  0 },
    { 63, -1 },
    { 64, -1 }, {  0,  2 }, {  1,  0 }, { 15,  0 },
    { 65, -1 }, { 11,  0 }, { 16,  1 },
    { -1, -1 },
};

/* Surface id -> CD file number of the sample body.  The header file is always
 * the body minus one, which is what foot_seSetNewFiles() passes to
 * SndBankNew().  Entry 17 is the odd one out: FOOT016_SOFT lives a long way
 * from the rest of the FOOT set on the disc, and shares its `016' name with
 * entry 16 (FOOT016_KUSA). */
static int foot_se_label_tbl[SE_LABEL_MAX] =                        /* data 3147a0 */
{
    FOOT000_ISIKAIDAN_BD,       /*  0  stone stairs        */
    FOOT001_ISIDATAMI_BD,       /*  1  stone paving        */
    FOOT002_MOKUHEN_BD,         /*  2  wood chips          */
    FOOT003_DOMA2_BD,           /*  3  earth floor 2       */
    FOOT004_KAMOI_BD,           /*  4  lintel              */
    FOOT005_TATAMI_BD,          /*  5  tatami              */
    FOOT006_ITAKAIDAN_BD,       /*  6  wooden stairs       */
    FOOT007_ITANOMA_BD,         /*  7  boarded room        */
    FOOT008_ITANOMA2_BD,        /*  8  boarded room 2      */
    FOOT009_WARETOUKI_BD,       /*  9  broken pottery      */
    FOOT010_JYARIMAJIRI_BD,     /* 10  gravel-strewn       */
    FOOT011_OTIBAMAJIRI_BD,     /* 11  leaf-strewn         */
    FOOT012_EXTKISIMI_BD,       /* 12  exterior creak      */
    FOOT013_DOMA1_BD,           /* 13  earth floor 1       */
    FOOT014_ITANOMA3_BD,        /* 14  boarded room 3      */
    FOOT015_TUTI_BD,            /* 15  soil                */
    FOOT016_KUSA_BD,            /* 16  grass               */
    FOOT016_SOFT_BD,            /* 17  soft                */
};

/* One resident bank each.  -1 means the slot is free. */
typedef struct _FOOT_SE_MANAGE      /* 0x4 */
{
    /* 0x0 */ int bank_id;
} FOOT_SE_MANAGE;

static FOOT_SE_MANAGE foot_se_manage[WRK_MAX];                      /* bss 4af390 */

/* --------------------------------------------------------------------------
 *  foot_seGetFileNo4Label            (inlined; ROM lines 74..78)
 *
 *  Surface id -> CD file number.  The bounds test is a single unsigned
 *  compare in the ROM, which is what makes MrecGetSeNo()'s -1 ("no footstep
 *  rectangle under the player") fall out here as -1 rather than reading off
 *  the front of the table.
 * ------------------------------------------------------------------------ */
static int foot_seGetFileNo4Label(int no)                               /* 74 */
{
    if ((unsigned int)no >= SE_LABEL_MAX)                               /* 76 */
    {
        return -1;
    }

    return foot_se_label_tbl[no];                                       /* 77 */
}

/* --------------------------------------------------------------------------
 *  foot_seGetSeStat4RegID
 *
 *  Finds `reg_id`'s header row and returns the first row past it -- i.e. the
 *  start of that room's surface list, which foot_seGetSeStat() then walks.
 *  NULL when the room is not in the table.
 * ------------------------------------------------------------------------ */
static int *foot_seGetSeStat4RegID(int reg_id)                          /* 82 */
{
    int i;

    for (i = 0; i < SE_FOOT_DAT_MAX; i++)                               /* 85 */
    {
        if (se_footDatList[i][1] != -1)                                 /* 87 */
        {
            continue;
        }

        if (se_footDatList[i][0] == -1)                                 /* 89 */
        {
            return (int *)0;
        }
        if (se_footDatList[i][0] == reg_id)                             /* 90 */
        {
            return se_footDatList[i + 1];
        }
    }                                                                   /* 91 */

    return (int *)0;                                                    /* 92 */
}

/* Copies `reg_id`'s surface ids into `dat` and returns how many there were.
 * `dat` is a WRK_MAX-entry array in every caller; nothing bounds-checks it,
 * so the table is the only thing keeping a room under twelve surfaces. */
static int foot_seGetSeStat(int reg_id, int *dat)                       /* 97 */
{
    int *ip;
    int  cnt = 0;

    if ((ip = foot_seGetSeStat4RegID(reg_id)) != (int *)0)              /* 102 */
    {
        while (ip[1] != -1)                                             /* 103 */
        {
            *dat = ip[0];                                               /* 104 */
            dat++;                                                      /* 105 */
            ip += 2;                                                    /* 106 */
            cnt++;                                                      /* 107 */
        }
    }

    return cnt;                                                         /* 109 */
}

/* --------------------------------------------------------------------------
 *  foot_seSetRoom
 *
 *  Called from MapLoadRegistReq() with the room that is being dropped and the
 *  one being registered.  Builds the union of the two surface lists -- new
 *  room first, then whatever the outgoing room uses that the new one does not
 *  -- maps it through foot_se_label_tbl[] and hands it to
 *  foot_seSetNewFiles().  Both rooms stay audible, which is what a door
 *  transition needs.
 *
 *  The four empty loops below (152/154, 155/157, 184/186, 193/195) are in the
 *  ROM exactly as written here: the `for' headers survive but their bodies --
 *  debug prints of the three arrays -- are commented out.  GCC 2.96 does not
 *  delete an empty loop, so all four are still in the object, six `nop's
 *  apiece.  Kept, so the shape of the original is visible.
 * ------------------------------------------------------------------------ */
void foot_seSetRoom(int room_id_new, int room_id_now)                   /* 139 */
{
    int i, j;
    int count, count2, new_count;

    /* Both initialisers really are eight -1s into a twelve-entry array, so
     * the last four slots start at 0, not -1.  Read straight out of the
     * object's .rodata (0x3ae770 and 0x3ae7a0), where GCC parked the two
     * 48-byte blobs it copies onto the stack.  It makes no difference: every
     * slot up to `count' / `count2' is overwritten below, and nothing reads
     * past that. */
    int array[WRK_MAX]  = { -1, -1, -1, -1, -1, -1, -1, -1 };           /* 144 */
    int array2[WRK_MAX] = { -1, -1, -1, -1, -1, -1, -1, -1 };           /* 145 */
    int no_overlap_array[WRK_MAX];

    count     = foot_seGetSeStat(room_id_new, array);                   /* 149 */
    count2    = foot_seGetSeStat(room_id_now, array2);                  /* 150 */
    new_count = 0;

    for (i = 0; i < count; i++)                                         /* 152 */
    {
        /* printf of array[i] -- commented out in the ROM */
    }                                                                   /* 154 */
    for (i = 0; i < count2; i++)                                        /* 155 */
    {
        /* printf of array2[i] -- commented out in the ROM */
    }                                                                   /* 157 */

    /* Everything the new room wants, and a -1 struck through each matching
     * entry of the outgoing room's list so the second pass cannot add it
     * twice. */
    for (i = 0; i < count; i++)                                         /* 163 */
    {
        no_overlap_array[new_count] = array[i];                         /* 164 */
        new_count++;                                                    /* 165 */

        for (j = 0; j < count2; j++)                                    /* 166 */
        {
            if (array[i] == array2[j])                                  /* 168 */
            {
                array2[j] = -1;                                         /* 169 */
                break;                                                  /* 170 */
            }
        }                                                               /* 172 */
    }                                                                   /* 173 */

    /* Whatever the outgoing room still needs on its own. */
    for (i = 0; i < count2; i++)                                        /* 176 */
    {
        if (array2[i] != -1)                                            /* 177 */
        {
            no_overlap_array[new_count] = array2[i];                    /* 178 */
            new_count++;                                                /* 179 */
        }
    }                                                                   /* 181 */

    for (i = 0; i < new_count; i++)                                     /* 184 */
    {
        /* printf of no_overlap_array[i] -- commented out in the ROM */
    }                                                                   /* 186 */

    /* Surface ids in, CD file numbers out -- in place. */
    for (i = 0; i < new_count; i++)                                     /* 189 */
    {
        no_overlap_array[i] = foot_seGetFileNo4Label(no_overlap_array[i]);  /* 190 */
    }                                                                   /* 191 */

    for (i = 0; i < new_count; i++)                                     /* 193 */
    {
        /* printf of no_overlap_array[i] -- commented out in the ROM */
    }                                                                   /* 195 */

    foot_seSetNewFiles(no_overlap_array, new_count);                    /* 200 */
}

void foot_seInit(void)                                                  /* 222 */
{
    int i;

    for (i = 0; i < WRK_MAX; i++)                                       /* 226 */
    {
        foot_se_manage[i].bank_id = -1;                                 /* 227 */
    }
}

void foot_seRelease(void)                                               /* 231 */
{
    int i;

    for (i = 0; i < WRK_MAX; i++)                                       /* 234 */
    {
        if (foot_se_manage[i].bank_id >= 0)                             /* 235 */
        {
            /* "Releae" is the ROM's own typo. */
            printf("foot_seReleae bank_id = %d\n",
                   foot_se_manage[i].bank_id);                          /* 236 */
            SndBankRelease(foot_se_manage[i].bank_id);                  /* 237 */
            foot_se_manage[i].bank_id = -1;                             /* 238 */
        }
    }
}

/* --------------------------------------------------------------------------
 *  foot_seGetWrkNo4FileNo            (inlined; ROM lines 245..255)
 *
 *  Which slot, if any, already holds `file_no'.  Asking SndBankGetFileNo()
 *  rather than caching the file number in FOOT_SE_MANAGE is what keeps the
 *  slot a single int.
 * ------------------------------------------------------------------------ */
static int foot_seGetWrkNo4FileNo(int file_no)                          /* 245 */
{
    int i;
    int no;

    for (i = 0; i < WRK_MAX; i++)                                       /* 247 */
    {
        if (foot_se_manage[i].bank_id >= 0 &&                           /* 248 */
            SndBankGetFileNo(foot_se_manage[i].bank_id, &no) == SND_BANK_OK &&
            file_no == no)                                              /* 249 */
        {
            return i;
        }
    }                                                                   /* 254 */

    return -1;
}

/* foot_seGetWrkNo4FileNo() one step further on: the bank itself.  (inlined;
 * ROM lines 259..266) */
static int foot_seGetBankNo4FileNo(int file_no)                         /* 259 */
{
    int no = foot_seGetWrkNo4FileNo(file_no);

    if (no == -1)                                                       /* 262 */
    {
        return -1;
    }

    return foot_se_manage[no].bank_id;                                  /* 265 */
}

/* First free slot, or -1.  (inlined; ROM lines 269..275) */
static int foot_seGetFreeWrkNo(void)                                    /* 269 */
{
    int i;

    for (i = 0; i < WRK_MAX; i++)                                       /* 271 */
    {
        if (foot_se_manage[i].bank_id == -1)                            /* 272 */
        {
            return i;
        }
    }                                                                   /* 274 */

    return -1;
}

/* --------------------------------------------------------------------------
 *  foot_seSetNewFiles
 *
 *  Re-cuts the twelve slots to hold exactly `file_array'.  Three passes: mark
 *  what is already loaded, release what nothing wants any more, then claim a
 *  slot for each file that is still missing.  Releasing before claiming is
 *  what lets a full set of twelve be replaced by a different full set.
 * ------------------------------------------------------------------------ */
void foot_seSetNewFiles(const int *file_array, int file_num)            /* 280 */
{
    int i, no, need_flg[WRK_MAX], exist_flg[WRK_MAX];

    memset(need_flg, 0, sizeof(need_flg));                              /* 283 */
    memset(exist_flg, 0, sizeof(exist_flg));                            /* 284 */

    /* exist_flg[] is indexed by file, so more files than slots would run off
     * the end of it.  The ROM prints and carries on. */
    if (file_num > WRK_MAX)                                             /* 287 */
    {
        PRINT_ASSERT("foot_seSetNewFiles() file_num is over WRK_MAX");   /* 288 */
    }

    for (i = 0; i < file_num; i++)                                      /* 297 */
    {
        no = foot_seGetWrkNo4FileNo(file_array[i]);

        if (no != -1)                                                   /* 300 */
        {
            need_flg[no]  = 1;                                          /* 301 */
            exist_flg[i]  = 1;                                          /* 302 */
        }
    }                                                                   /* 304 */

    for (i = 0; i < WRK_MAX; i++)                                       /* 307 */
    {
        if (need_flg[i] == 0 && foot_se_manage[i].bank_id >= 0)         /* 309 */
        {
            /* ROM lines 310..313 carry no code. */
            SndBankRelease(foot_se_manage[i].bank_id);                  /* 314 */
            foot_se_manage[i].bank_id = -1;                             /* 315 */
        }
    }                                                                   /* 317 */

    for (i = 0; i < file_num; i++)                                      /* 320 */
    {
        if (file_array[i] >= 0 && exist_flg[i] == 0)                    /* 321 */
        {
            no = foot_seGetFreeWrkNo();

            /* ROM BUG, reproduced: the assert does not stop the store below,
             * so overflowing the twelve slots writes foot_se_manage[-1].
             * Unreachable with the shipped se_footDatList -- driving
             * foot_seSetRoom() over all 66x66 room pairs puts the union at 11
             * surfaces at worst, and foot_seSetNewFiles() releases before it
             * claims -- which is presumably why it survived. */
            if (no == -1)                                               /* 326 */
            {
                PRINT_ASSERT("foot_seSetNewFiles file is too many");     /* 327 */
            }

            /* The sample header is always the body's file number minus one. */
            foot_se_manage[no].bank_id =
                SndBankNew(file_array[i], file_array[i] - 1, -1);        /* 330 */
        }
    }                                                                   /* 335 */
}

/* --------------------------------------------------------------------------
 *  foot_sePlay
 *
 *  One footstep.  motAniCodeReadSE() supplies the foot position and a nominal
 *  volume/pitch; the surface comes from whichever type-4 rectangle the foot
 *  landed in.
 *
 *  Volume is always spread over 0.86..1.15 of nominal; pitch is left alone two
 *  times in three and spread over 0.86..1.10 the third.  The bare draw at
 *  line 362 has its result thrown away -- it only advances the sequence, so
 *  the volume draw does not come straight off the previous frame's state.
 * ------------------------------------------------------------------------ */
void foot_sePlay(float *pos, int vol, int pitch)                        /* 348 */
{
    SND_3D_SET set;
    int        file_no;
    int        rnd_pitch;
    /* The ROM's local table names `rnd_pitch' but not this one, even though
     * GCC gave it a callee-saved register of its own ($s7) and the value is
     * computed at line 363 and not consumed until the call at 406.  It cannot
     * be an expression hoisted out of that call -- the draw in it has to
     * execute where the source put it -- so it is a source variable whose
     * stab was dropped.  The name is the port's. */
    int        rnd_vol;
    int        bank_no;

    memset(&set, 0, sizeof(SND_3D_SET));                                /* 349 */

    MioPan_Rand();                                                      /* 362 */
    rnd_vol = ((115 - MioPan_Rand() % 30) * vol) / 100;                 /* 363 */

    rnd_pitch = pitch;
    if (MioPan_Rand() % 3 == 0)                                         /* 370 */
    {
        rnd_pitch = ((110 - MioPan_Rand() % 25) * pitch) / 100;         /* 371 */
    }

    MrecSetSEInfo(pos);                                                 /* 377 */
    file_no = foot_seGetFileNo4Label(MrecGetSeNo());                    /* 378 */

    if (file_no >= 0)                                                   /* 386 */
    {
        bank_no = foot_seGetBankNo4FileNo(file_no);

        /* file_no is already known to be >= 0 here; the ROM tests it again
         * anyway.  A -1 bank means the room's list and the resident set
         * disagree -- a room whose surface was never claimed. */
        if (file_no == -1 || bank_no == -1)                             /* 397 */
        {
            printf("foot_sePlay Cannot Get Info\n");                    /* 398 */
            return;                                                     /* 399 */
        }

        set.pos = (sceVu0FVECTOR *)pos;
        SndBankPlay(bank_no, 0, 1, 0, rnd_vol, rnd_pitch, 0, &set);     /* 406 */
    }
}
