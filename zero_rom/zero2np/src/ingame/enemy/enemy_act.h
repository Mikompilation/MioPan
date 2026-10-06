/* ==========================================================================
 *  ingame/enemy/enemy_act.h
 *
 *  The ghost behaviour-script interpreter (enemy_act.o).  enemy.c owns the
 *  ten ENE_WRK slots and points the cursor at an action (EneActSet /
 *  EneBlinkSet); enemy_act.c is the machine that runs it.
 *
 *  The 152 opcode handlers are global symbols in the ROM but are only ever
 *  reached through the five dispatch tables inside enemy_act.c, so they are
 *  declared there rather than here.  Only the three entry points the rest of
 *  the tree calls are exported.
 *
 *  Reconstructed from the Feb 6 2004 prototype (SLES_523.84), enemy_act.o.
 * ======================================================================== */

#ifndef _INGAME_ENEMY_ENEMY_ACT_H
#define _INGAME_ENEMY_ENEMY_ACT_H

#include "enemy.h"

/* One step of the ghost's main action script.  EneRule() runs it unless the
 * ghost is held by one of the three "no algorithm" status bits.  Opcodes fall
 * through to each other inside a single frame until one parks a real wait, so
 * this may execute a whole run of state changes in one call. */
void EneAlgCtrl(ENE_WRK *ew);

/* One step of the blink (secondary) script, which drives the face and the
 * per-part visibility independently of the main action -- it keeps running
 * while the status bits that freeze the main script are up. */
void EneBlinkCtrl(ENE_WRK *ew);

/* Set the ghost's own directional light colour.  enemy.c calls it whenever
 * the ghost's condition changes -- the ENE_DAT colour normally, tinted green
 * while slowed, red while highlighted, magenta while paralysed or sealed --
 * and the death script whitens it out as the ghost dissolves.  `amb` is taken
 * and ignored; the ROM never writes an ambient term. */
void SetEnemyParallelLight(ENE_WRK *ew, float r, float g, float b, float amb);

#endif /* _INGAME_ENEMY_ENEMY_ACT_H */
