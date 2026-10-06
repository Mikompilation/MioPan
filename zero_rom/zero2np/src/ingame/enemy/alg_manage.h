/* ==========================================================================
 *  ingame/enemy/alg_manage.h
 *
 *  Enemy algorithm manager: the Req / IsReady / Clear triple enemy.c uses to
 *  keep the enemy action-script pack resident, exactly mirroring the model
 *  manager's per-family triples in graphics/mmanage.h.
 *
 *  Unlike the model families there is no per-index file block here: every
 *  enemy algorithm lives inside the single ENE_ACT01_OBJ pack, so alg_no
 *  selects a script *within* the file rather than a file.  All three entry
 *  points therefore accept alg_no and drop it -- see alg_manage.c.
 *
 *  Reconstructed from the Feb 6 2004 prototype (SLES_523.84), alg_manage.o.
 * ======================================================================== */

#ifndef _INGAME_ENEMY_ALG_MANAGE_H
#define _INGAME_ENEMY_ALG_MANAGE_H

#include "../../common/ol_load.h"           /* OL_LOAD_ERR */

/* ZERO2.MAP declares the out-parameter as `int *` (mangled
 * alg_manageIsReadyAlg__FiPi), which on the EE is the same width as the
 * pointer OL_LOAD::IsReady() stores through it.  It is not on the host, so the
 * port widens it to `void **` -- the same deviation graphics/mmanage.h already
 * makes for mmanageIsReadyMdl / _Anm / _ItemMdl.  Callers must declare their
 * scratch as `void *`, not `int`, or the store overruns it. */
OL_LOAD_ERR alg_manageReqAlg(int alg_no);
int         alg_manageIsReadyAlg(int alg_no, void **mdl_pp);
void        alg_manageClearAlg(int alg_no);

#endif /* _INGAME_ENEMY_ALG_MANAGE_H */
