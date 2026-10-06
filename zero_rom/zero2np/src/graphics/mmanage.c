/* ==========================================================================
 *  graphics/mmanage.c
 *
 *  Model manager.  Every entry point here is a thin wrapper over the OL_LOAD
 *  slot table in common/ol_load.c; all this file adds is the per-family file
 *  number base and the one-off fixup each family needs the first time its data
 *  becomes resident:
 *
 *      item model      -> sgdRemap() over the pack's SGD
 *      character model -> motInitOneEnemyMdl()
 *      animation       -> none
 *
 *  ROM object: mmanage.o, .text 0x218a00..0x218ee8 (0x4e8).  Eleven bodies
 *  plus the four fixed_array template instantiations at the head of the object
 *  (_fixed_array_assert and _fixed_array_verifyrange<void *>/<char *>/
 *  <unsigned int *>) and five 4-byte alignment fills account for the section
 *  byte-for-byte, so there is no unlisted body.  The object owns no data of
 *  its own: .rodata (0x187) is the fixed_array assert literal, ModelMemoryFree's
 *  __FUNCTION__ and its seven message strings, and .sdata (0x42) is the assert
 *  literal's `str` pointer, the "void*"/"char*" type names, the ten unused
 *  _$tmp_N slots and the two-byte "\n" the printf runs share.
 *
 *  Two things about the shape below are measured rather than assumed, and both
 *  differ from what a first reading suggests:
 *
 *  * Each family's Req and Clear go through a one-line file-number helper --
 *    ROM lines 45 (item), 87 (model) and 117 (animation), each sitting in the
 *    gap just before its family's block.  They are fully inlined and carry no
 *    symbol, so the names here are the port's.  IsReady does *not* use them: it
 *    spells the addition out on its own call line (64 / 97 / 126).  That
 *    asymmetry is the ROM's -- line 45 appears in mmanageReqItemMdl and
 *    mmanageClearItemMdl and nowhere else.
 *
 *  * Every IsReady is a `switch` on the call's result with no local to hold it
 *    (functions.txt lists only the three parameters, and the int stab list is
 *    exhaustive).  The `li 3 / beq`, `slti 4`, `li 1 / beq` ladder is GCC's
 *    case decision tree, which expand_end_case reorders to sit *before* the
 *    case bodies -- which is why it carries the switch head's own line number
 *    and not one of its own.  WAIT_MEMORY falls through into `default`, which
 *    is what gives both of them the single shared `return 0`.
 *
 *  Reconstructed from the Feb 6 2004 prototype (SLES_523.84).
 * ======================================================================== */

#include "mmanage.h"

#include "../common/ol_load.h"
#include "../common/packfile.h"                  /* Pk2GetAddr */
#include "../common/utility2.h"                  /* PRINT_ASSERT / PRINT_WARNING */
#include "graph3d/gra3dSGDData.h"                /* sgdRemap */
#include "graph3d/sgd_types.h"                   /* SGDFILEHEADER */
#include "motion/motion.h"                       /* motInitOneEnemyMdl */
#include "../ingame/enemy/enemy.h"               /* EneAllRelease / PreloadedEneAllRelease */
#include "../ingame/enemy/fene_entry.h"          /* fene_entry */
#include "../system/eeiop/cddat.h"               /* CH000_MIO_MDL / _ANM / I000_... */

#include <stdio.h>

/* --------------------------------------------------------------------------
 *  ModelMemoryFree
 *
 *  Called when the load heap cannot satisfy a request.  Escalates in three
 *  steps, stopping as soon as one of them reports it freed something: drop the
 *  float ghost, then the preloaded enemies, and failing that dump the loader
 *  state and assert before releasing every enemy.
 *
 *  StringBuf is a block-scope local of the *inner* if, not of the function --
 *  the stabs put it inside $LBB9, the innermost of the three nested blocks.
 * ------------------------------------------------------------------------ */
void ModelMemoryFree(int wrk_no)                                        /* 13 */
{
    printf("\n");                                                       /* 14 */
    printf("\n");                                                       /* 15 */
    printf("\n");                                                       /* 16 */
    printf("\n");                                                       /* 17 */
    printf("\n");                                                       /* 18 */

    printf("====================================================\n");   /* 19 */
    printf("==========Memory Lack, So Free Float Ghost==========\n");   /* 20 */
    printf("====================================================\n");   /* 21 */

    if (fene_entry.Release() == 0)                                      /* 23 */
    {
        PRINT_WARNING("Still Memory Lack, So Free Preloaded Enemy");    /* 24 */

        if (PreloadedEneAllRelease(wrk_no) == 0)                        /* 27 */
        {
            char StringBuf[2000];

            ol_load.Print(StringBuf);                                   /* 30 */
            PRINT_ASSERT("Memory Lack %s", StringBuf);                  /* 31 */

            EneAllRelease();                                            /* 34 */
        }
    }

    printf("\n");                                                       /* 37 */
    printf("\n");                                                       /* 38 */
    printf("\n");                                                       /* 39 */
    printf("\n");                                                       /* 40 */
    printf("\n");                                                       /* 41 */
}                                                                       /* 42 */

/* --------------------------------------------------------------------------
 *  Item models
 * ------------------------------------------------------------------------ */

/* The whole of this lives on ROM line 45; the name is the port's. */
static int GetItemMdlFileNo(int mdl_no)
{
    return mdl_no + I000_PLAY_CAMERA_PK2;                               /* 45 */
}

/* An item pack's SGD is entry 0. */
int *GetItemSgdAddr(int *pDataTop)                                      /* 52 */
{
    return (int *)Pk2GetAddr((u_int *)pDataTop,
                             ITEM_MODEL_PACK_ORDER_SGD);                /* 53 */
}                                                                       /* 55 */

MMANAGE_ERR mmanageReqItemMdl(int mdl_no)                               /* 59 */
{
    return ol_load.Req(GetItemMdlFileNo(mdl_no));                       /* 60 */
}                                                                       /* 61 */

int mmanageIsReadyItemMdl(int mdl_no, void **mdl_pp, int bForceFree)    /* 63 */
{
    switch (ol_load.IsReady(mdl_no + I000_PLAY_CAMERA_PK2, mdl_pp))     /* 64 */
    {
    case OL_LOAD_READY_READY:
        return 1;

    case OL_LOAD_READY_READY_FIRST:
        /* First sighting: fix the pack's SGD pointers up once. */
        sgdRemap((SGDFILEHEADER *)GetItemSgdAddr((int *)*mdl_pp));      /* 69 */

        return 1;                                                       /* 71 */

    case OL_LOAD_READY_WAIT_MEMORY:
        if (bForceFree != 0)                                            /* 73 */
            ModelMemoryFree(-1);                                        /* 74 */
        /* FALLTHROUGH */

    default:
        return 0;                                                       /* 76 */
    }
}                                                                       /* 78 */

void mmanageClearItemMdl(int mdl_no)                                    /* 80 */
{
    ol_load.Clear(GetItemMdlFileNo(mdl_no));                            /* 81 */
}                                                                       /* 82 */

/* --------------------------------------------------------------------------
 *  Character models
 * ------------------------------------------------------------------------ */

/* The whole of this lives on ROM line 87; the name is the port's. */
static int GetMdlFileNo(int mdl_no)
{
    return mdl_no + CH000_MIO_MDL;                                      /* 87 */
}

MMANAGE_ERR mmanageReqMdl(int mdl_no)                                   /* 91 */
{
    return ol_load.Req(GetMdlFileNo(mdl_no));                           /* 92 */
}                                                                       /* 93 */

int mmanageIsReadyMdl(int mdl_no, void **mdl_pp, int bForceFree)        /* 96 */
{
    switch (ol_load.IsReady(mdl_no + CH000_MIO_MDL, mdl_pp))            /* 97 */
    {
    case OL_LOAD_READY_READY:
        return 1;                                                       /* 99 */

    case OL_LOAD_READY_READY_FIRST:
        motInitOneEnemyMdl((u_int *)*mdl_pp, mdl_no);                   /* 101 */
        return 1;                                                       /* 102 */

    case OL_LOAD_READY_WAIT_MEMORY:
        if (bForceFree != 0)                                            /* 104 */
            ModelMemoryFree(-1);                                        /* 105 */
        /* FALLTHROUGH */

    default:
        return 0;                                                       /* 107 */
    }
}                                                                       /* 109 */

void mmanageClearMdl(int mdl_no)                                        /* 111 */
{
    ol_load.Clear(GetMdlFileNo(mdl_no));                                /* 112 */
}                                                                       /* 113 */

/* --------------------------------------------------------------------------
 *  Character animations
 *
 *  Animation data needs no fixup, so READY_FIRST and READY are two cases with
 *  the same one-line body rather than one shared label -- the ROM has a
 *  separate `return 1` for each (128 and 130).
 * ------------------------------------------------------------------------ */

/* The whole of this lives on ROM line 117; the name is the port's. */
static int GetAnmFileNo(int mdl_no)
{
    return mdl_no + CH000_MIO_ANM;                                      /* 117 */
}

MMANAGE_ERR mmanageReqAnm(int mdl_no)                                   /* 121 */
{
    return ol_load.Req(GetAnmFileNo(mdl_no));                           /* 122 */
}                                                                       /* 123 */

int mmanageIsReadyAnm(int mdl_no, void **mdl_pp, int bForceFree)        /* 125 */
{
    switch (ol_load.IsReady(mdl_no + CH000_MIO_ANM, mdl_pp))            /* 126 */
    {
    case OL_LOAD_READY_READY:
        return 1;                                                       /* 128 */

    case OL_LOAD_READY_READY_FIRST:
        return 1;                                                       /* 130 */

    case OL_LOAD_READY_WAIT_MEMORY:
        if (bForceFree != 0)                                            /* 132 */
            ModelMemoryFree(-1);                                        /* 133 */
        /* FALLTHROUGH */

    default:
        return 0;                                                       /* 135 */
    }
}                                                                       /* 137 */

void mmanageClearAnm(int mdl_no)                                        /* 139 */
{
    ol_load.Clear(GetAnmFileNo(mdl_no));                                /* 140 */
}                                                                       /* 141 */
