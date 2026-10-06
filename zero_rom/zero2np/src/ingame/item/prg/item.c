// FILE: /home/zero_rom/zero2np/src/ingame/item/prg/item.c
//
// Player inventory.  plyr_item[] is indexed by item id and parallels the
// static item_dat[] table; acquisition clamps to item_dat[].get_max, and use
// dispatches on item_dat[].type to the film / HP / SP handlers below.
//
// Every entry point range-checks item_id against 58 and asserts, so the ROM
// bodies are mostly guard.  The bound is written out as a literal in the ROM
// (sltiu against 0x3a), not derived from the array.
//
// Reconstructed from the Feb 6 2004 prototype (SLES_523.84).

#include "item.h"

#include "../../../common/utility2.h"   /* PRINT_ASSERT / PRINT_WARNING */
#include "../../../common/variable.h"   /* plyr_wrk / ingame_wrk */
#include "../../photo/m_plyr_camera.h"  /* m_plyr_camera */
#include "../../photo/photo.h"          /* SyncHpBar */

#include <stdio.h>

fixed_array<PLYR_ITEM, PLYR_ITEM_MAX> plyr_item;                        /* data 318760 */

/* --------------------------------------------------------------------------
 *  AllPlyrItemInit
 *
 *  item.o's .rodata also carries an unreferenced "ERROR!!! ITEM_ID MAX
 *  OVER!!!!!" string and an __FUNCTION__ for this function (3b9428 / 3b9468)
 *  with no code reaching either -- get_xrefs_to finds nothing.  The ROM
 *  presumably guarded the table size with a condition GCC folded away, so the
 *  check is not reproduced here rather than invented.
 * ------------------------------------------------------------------------ */
void AllPlyrItemInit(void)
{                                                                       /* 60 */
    int i;

    for (i = 0; i < PLYR_ITEM_MAX; i++)                                 /* 66 */
    {
        PlyrItemInit(&plyr_item[i]);
    }
}                                                                       /* 72 */

/* 0xff, not 0 -- 0 is a real item id (the first film), so the empty marker
 * has to sit outside the range. */
void PlyrItemInit(PLYR_ITEM *plyr_item_addr)
{
    plyr_item_addr->item_id  = 0xff;                                    /* 95 */
    plyr_item_addr->have_num = 0;                                       /* 96 */
}

/* --------------------------------------------------------------------------
 *  AllPlyrEventItemLost
 *
 *  Only have_num is cleared; item_id is left alone, so the row still reads as
 *  "known" to anything walking the table.
 * ------------------------------------------------------------------------ */
void AllPlyrEventItemLost(void)
{                                                                       /* 105 */
    int i;

    for (i = 0; i < PLYR_ITEM_MAX; i++)                                 /* 110 */
    {
        if (item_dat[i].type == ITEM_TYPE_EVENT)                        /* 112 */
        {
            plyr_item[i].have_num = 0;
        }
    }                                                                   /* 115 */
}

/* --------------------------------------------------------------------------
 *  ItemGet / ItemGetPossible
 * ------------------------------------------------------------------------ */
void ItemGet(int item_id, u_char get_num)
{                                                                       /* 129 */
    ITEM_DAT *dat_addr;

    if ((u_int)item_id >= PLYR_ITEM_MAX)                                /* 135 */
    {
        PRINT_ASSERT("Error! ItemGet item_id %d", item_id);             /* 136 */
    }

    dat_addr = GetItemDatAddr(item_id);                                 /* 140 */

    if (ItemGetPossible(item_id) != 0)                                  /* 144 */
    {
        plyr_item[item_id].item_id   = item_id;
        plyr_item[item_id].have_num += get_num;

        if (plyr_item[item_id].have_num > (int)dat_addr->get_max)
        {
            plyr_item[item_id].have_num = dat_addr->get_max;
        }
    }
}

/* The assert here is the only one in the file that formats __FUNCTION__ with
 * %s instead of spelling the name out in the literal. */
u_char ItemGetPossible(int item_id)
{                                                                       /* 165 */
    int       have_num;
    ITEM_DAT *dat_addr;

    if ((u_int)item_id >= PLYR_ITEM_MAX)                                /* 173 */
    {
        PRINT_ASSERT("Error! %s item_id %d", __FUNCTION__, item_id);    /* 174 */
    }

    have_num = plyr_item[item_id].have_num;

    dat_addr = GetItemDatAddr(item_id);                                 /* 181 */

    return have_num < (int)dat_addr->get_max;                           /* 185 */
}                                                                       /* 193 */

/* --------------------------------------------------------------------------
 *  ItemUse
 *
 *  Answers non-zero when the item was actually used, and only then is it
 *  taken off the player.  Note ItemFilmEquip() always answers 0, so loading
 *  film equips it without spending it.
 *
 *  Case order follows the ROM's line numbers rather than the numeric order of
 *  the type values; NONE and EVENT share a body at the end.
 * ------------------------------------------------------------------------ */
u_char ItemUse(int item_id, u_char use_num)
{                                                                       /* 207 */
    u_char use_res;

    if ((u_int)item_id >= PLYR_ITEM_MAX)                                /* 213 */
    {
        PRINT_ASSERT("Error! ItemUse item_id %d", item_id);             /* 214 */
    }

    use_res = 0;

    if (ItemUsePossible(item_id, use_num) != 0)                         /* 222 */
    {
        switch (item_dat[item_id].type)                                 /* 224 */
        {
        case ITEM_TYPE_FILM:
            use_res = (u_char)ItemFilmEquip(item_id);                   /* 229 */
            break;                                                      /* 230 */

        case ITEM_TYPE_HP:
            use_res = (u_char)ItemPlyrHPRecover(item_id);               /* 232 */
            break;                                                      /* 233 */

        case ITEM_TYPE_SP:
            use_res = (u_char)ItemPlyrSPRecover(item_id);               /* 235 */
            break;                                                      /* 236 */

        case ITEM_TYPE_CONSUME:
            use_res = 1;                                                /* 239 */
            break;

        case ITEM_TYPE_NONE:
        case ITEM_TYPE_EVENT:
            use_res = 0;                                                /* 242 */
            break;

        default:
            PRINT_ASSERT("Error! ItemUse item_type %d",
                         item_dat[item_id].type);                       /* 244 */
            break;
        }

        if (use_res == 1)                                               /* 247 */
        {
            ItemLost(item_id, use_num);                                 /* 248 */
        }
    }

    return use_res;                                                     /* 253 */
}

/* Clamps a silly use_num to 99 and warns, then tests the sign bit of
 * (have_num - use_num) -- the ROM extracts bit 31 and inverts it rather than
 * comparing. */
u_char ItemUsePossible(int item_id, u_char use_num)
{                                                                       /* 263 */
    int total_num;

    if ((u_int)item_id >= PLYR_ITEM_MAX)                                /* 270 */
    {
        PRINT_ASSERT("Error! ItemUsePossible item_id %d, use_num %d",
                     item_id, use_num);                                 /* 271 */
    }

    total_num = use_num;

    if (total_num > 99)                                                 /* 273 */
    {
        PRINT_WARNING("Warning! ItemUsePossible item_id %d use_num %d",
                      item_id, use_num);                                /* 274 */
        total_num = 99;                                                 /* 275 */
    }

    return (plyr_item[item_id].have_num - total_num) >= 0;              /* 283 */
}                                                                       /* 291 */

/* Dropping to zero resets the row completely, so item_id goes back to 0xff. */
void ItemLost(int item_id, u_char use_num)
{                                                                       /* 299 */
    if ((u_int)item_id >= PLYR_ITEM_MAX)                                /* 304 */
    {
        PRINT_ASSERT("Error! ItemLost item_id %d", item_id);            /* 305 */
    }

    plyr_item[item_id].have_num -= use_num;

    if (plyr_item[item_id].have_num < 1)
    {
        PlyrItemInit(&plyr_item[item_id]);
    }
}

/* --------------------------------------------------------------------------
 *  Queries
 * ------------------------------------------------------------------------ */
int GetPlyrItemHaveNum(int item_id)
{
    if ((u_int)item_id >= PLYR_ITEM_MAX)                                /* 336 */
    {
        /* The message says GetItemHaveNum; the function is
         * GetPlyrItemHaveNum.  That is the ROM's text. */
        PRINT_ASSERT("Error! GetItemHaveNum item_id %d", item_id);      /* 337 */
    }

    return plyr_item[item_id].have_num;
}

int GetHaveItemTypeNum(void)
{                                                                       /* 350 */
    int i;
    int type_num;

    type_num = 0;                                                       /* 355 */

    for (i = 0; i < PLYR_ITEM_MAX; i++)                                 /* 358 */
    {
        if (GetPlyrItemHaveNum(i) > 0)                                  /* 360 */
        {
            type_num++;
        }
    }                                                                   /* 363 */

    return type_num;                                                    /* 366 */
}

int GetItemType(int item_id)
{
    if ((u_int)item_id >= PLYR_ITEM_MAX)                                /* 382 */
    {
        PRINT_ASSERT("Error! GetItemType item_id %d", item_id);         /* 383 */
    }

    return (int)item_dat[item_id].type;                                 /* 389 */
}

/* The range test is done twice: once to assert, once to decide the answer.
 * That is the ROM's shape -- an out-of-range id asserts and then returns
 * NULL rather than indexing. */
ITEM_DAT *GetItemDatAddr(int item_id)
{
    ITEM_DAT *dat_addr;

    if ((u_int)item_id >= PLYR_ITEM_MAX)                                /* 404 */
    {
        PRINT_ASSERT("Error! GetItemDatAddr item_id %d", item_id);      /* 405 */
    }

    dat_addr = (ITEM_DAT *)0;                                           /* 411 */

    if (item_id < PLYR_ITEM_MAX)                                        /* 412 */
    {
        dat_addr = &item_dat[item_id];
    }

    return dat_addr;                                                    /* 416 */
}

/* --------------------------------------------------------------------------
 *  ItemUse handlers
 * ------------------------------------------------------------------------ */

/* A value of 100 is a full heal and skips the difficulty scaling entirely --
 * otherwise 100% would still come out at 165% or 100% depending on the row.
 * SyncHpBar() runs even when nothing was restored. */
int ItemPlyrHPRecover(int item_id)
{                                                                       /* 429 */
    /* Percentage of the healed amount kept, per difficulty: easier settings
     * heal more.  Function-local in the ROM (functions.txt lists it under
     * this function), and indexed by ingame_wrk.mDifficulty. */
    static const int difficulty_value[4] = { 165, 155, 155, 100 };      /* rdata 3b96b8 */

    int res;

    if ((u_int)item_id >= PLYR_ITEM_MAX)                                /* 448 */
    {
        PRINT_ASSERT("Error! ItemPlyrHPRecover item_id %d", item_id);   /* 449 */
    }

    res = 0;

    if (plyr_wrk.cmn_wrk.st.hp != plyr_wrk.cmn_wrk.st.hpmax)            /* 455 */
    {
        if (item_dat[item_id].value != 100)                             /* 457 */
        {
            plyr_wrk.cmn_wrk.st.hp = (u_short)
                (plyr_wrk.cmn_wrk.st.hp +
                 (plyr_wrk.cmn_wrk.st.hpmax * item_dat[item_id].value / 100) *
                 difficulty_value[ingame_wrk.mDifficulty] / 100); /* 458 */

            if (plyr_wrk.cmn_wrk.st.hp >= plyr_wrk.cmn_wrk.st.hpmax)    /* 460 */
            {
                plyr_wrk.cmn_wrk.st.hp = plyr_wrk.cmn_wrk.st.hpmax;
            }
        }                                                               /* 464 */
        else
        {
            plyr_wrk.cmn_wrk.st.hp = plyr_wrk.cmn_wrk.st.hpmax;         /* 467 */
        }

        res = 1;                                                        /* 469 */
    }

    SyncHpBar();                                                        /* 478 */

    return res;                                                         /* 482 */
}

/* No difficulty scaling and no full-heal special case -- the SP side is the
 * plain percentage. */
int ItemPlyrSPRecover(int item_id)
{
    int res;

    if ((u_int)item_id >= PLYR_ITEM_MAX)                                /* 496 */
    {
        PRINT_ASSERT("Error! ItemPlyrSPRecover item_id %d", item_id);   /* 497 */
    }

    res = 0;

    if (plyr_wrk.cmn_wrk.st.sp != plyr_wrk.cmn_wrk.st.spmax)            /* 503 */
    {
        plyr_wrk.cmn_wrk.st.sp = (u_short)
            (plyr_wrk.cmn_wrk.st.sp +
             plyr_wrk.cmn_wrk.st.spmax * item_dat[item_id].value / 100); /* 504 */

        if (plyr_wrk.cmn_wrk.st.sp >= plyr_wrk.cmn_wrk.st.spmax)        /* 506 */
        {
            plyr_wrk.cmn_wrk.st.sp = plyr_wrk.cmn_wrk.st.spmax;         /* 507 */
        }

        res = 1;                                                        /* 510 */
    }

    return res;                                                         /* 517 */
}

/* Always answers 0, so ItemUse() never passes the film to ItemLost() --
 * loading a film equips it without spending it.
 *
 * item_id is stored straight into the film type with no translation, which
 * works because item_dat[] puts the five film rows at ids 0..4.  The range
 * check below is CVariable<char,0,4>::SetValue inlined -- hence the assert
 * banner naming common/variable.h rather than item.c. */
int ItemFilmEquip(int item_id)
{
    if ((u_int)item_id >= PLYR_ITEM_MAX)                                /* 530 */
    {
        PRINT_ASSERT("Error! ItemFilmEquip item_id %d", item_id);       /* 531 */
    }

    char iValue = (char)item_id;

    m_plyr_camera.camera_film.mFilmType = iValue;

    return 0;                                                           /* 540 */
}

/* --------------------------------------------------------------------------
 *  Save / debug
 * ------------------------------------------------------------------------ */
void SetSave_PlyrItem(MC_SAVE_DATA *data)
{
    data->addr = (u_char *)&plyr_item[0];                               /* 551 */
    data->size = sizeof(PLYR_ITEM) * PLYR_ITEM_MAX;                                                 /* 555 */
}

void DebugAllItemGet(void)
{                                                                       /* 575 */
    int i;

    for (i = 0; i < PLYR_ITEM_MAX; i++)                                 /* 577 */
    {
        ItemGet(i, GetItemDatAddr(i)->get_max);                         /* 579 */
    }                                                                   /* 580 */
}

void ItemDbgPlyrItemPrint(void)
{                                                                       /* 588 */
    int i;

    for (i = 0; i < PLYR_ITEM_MAX; i++)                                 /* 593 */
    {
        if (GetPlyrItemHaveNum(i) > 0)                                  /* 594 */
        {
            printf("%s num = %d\n", item_dat[i].item_name,
                   plyr_item[i].have_num);
        }
    }                                                                   /* 597 */
}
