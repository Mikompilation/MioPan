/* ==========================================================================
 *  ingame/enemy/alg_manage.c
 *
 *  Enemy algorithm manager.  The sibling of graphics/mmanage.c: three thin
 *  wrappers over the OL_LOAD slot table in common/ol_load.c that keep the
 *  enemy action-script pack resident while any enemy needs it.  enemy.c pairs
 *  each call with the matching mmanage*Anm one --
 *
 *      enemyReqDataSub()       mmanageReqAnm     + alg_manageReqAlg
 *      enemyIsReadyData()      mmanageIsReadyAnm + alg_manageIsReadyAlg
 *      enemyReqDataClearSub()  mmanageClearAnm   + alg_manageClearAlg
 *      enemyReleaseDataSub()   mmanageClearAnm   + alg_manageClearAlg
 *
 *  -- always passing ENE_DAT_COMMON::alg_no.
 *
 *  Where mmanage.c maps its index onto a block of CD files (mdl_no +
 *  CH000_MIO_MDL and friends), there is no block to map onto here: jene_dat[]
 *  uses alg_no 0..66 but every one of those scripts lives inside the single
 *  ENE_ACT01_OBJ pack, so alg_no picks a script *within* the file.  All three
 *  entry points consequently take alg_no and ignore it, and the refcount
 *  OL_LOAD keeps for the pack is what actually matters.
 *
 *  IsReady() collapses OL_LOAD_READY into a plain 0/1.  There is no one-off
 *  fixup for algorithm data -- READY and READY_FIRST are equivalent -- and no
 *  bForceFree escalation, so the ladder mmanage.c needs degenerates to a
 *  single test.  Every ROM caller passes a throwaway for mdl_pp and never
 *  reads it back; the pack is reached through OL_LOAD elsewhere.
 *
 *  Reconstructed from alg_manage.o (.text 0x12a410, 0x158).  The object has no
 *  statics of its own: its .rodata and .sdata hold nothing but the
 *  fixed_array.h assert string and the three typeid names emitted by the
 *  fixed_array<OL_LOAD_ONE,30> instantiation ol_load.h drags in.
 *
 *  Reconstructed from the Feb 6 2004 prototype (SLES_523.84).
 * ======================================================================== */

#include "alg_manage.h"

#include "../../common/ol_load.h"
#include "../../system/eeiop/cddat.h"            /* ENE_ACT01_OBJ */

/* --------------------------------------------------------------------------
 *  alg_manageGetFileNo
 *
 *  Reconstructed from the line stabs rather than from a symbol: the `li
 *  a1,0x12d` that supplies the file number in alg_manageReqAlg (0x12a4f4) and
 *  alg_manageClearAlg (0x12a554) is attributed to alg_manage.c line 10, which
 *  is above both function bodies (line 14/15 and 29/30) -- an inlined static
 *  living near the top of the file.  Only its shape survives; the name here is
 *  ours.  alg_manageIsReadyAlg's copy of the same constant was scheduled into
 *  the middle of its prologue and carries the surrounding line 19 instead,
 *  which is why the attribution shows up in two of the three callers.
 *
 *  alg_no is accepted and dropped: one pack holds every script.  Kept as a
 *  parameter so the call sites read the way the ROM's do, and so a later build
 *  that splits the pack has the seam already in place.
 * ------------------------------------------------------------------------ */
static int alg_manageGetFileNo(int alg_no)
{
    return ENE_ACT01_OBJ;
}

/* --------------------------------------------------------------------------
 *  Enemy algorithm pack
 * ------------------------------------------------------------------------ */
OL_LOAD_ERR alg_manageReqAlg(int alg_no)
{
    return ol_load.Req(alg_manageGetFileNo(alg_no));
}

int alg_manageIsReadyAlg(int alg_no, void **mdl_pp)
{
    OL_LOAD_READY ready;

    ready = ol_load.IsReady(alg_manageGetFileNo(alg_no), mdl_pp);

    if (ready != OL_LOAD_READY_READY && ready != OL_LOAD_READY_READY_FIRST)
    {
        return 0;
    }

    return 1;
}

void alg_manageClearAlg(int alg_no)
{
    ol_load.Clear(alg_manageGetFileNo(alg_no));
}
