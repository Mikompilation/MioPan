/* ==========================================================================
 *  ingame/enemy/enemy_dat.h
 *
 *  The two static enemy tables and the release-type state that rides along
 *  with them.
 *
 *  Reconstructed from the Feb 6 2004 prototype (SLES_523.84), enemy_dat.o.
 * ======================================================================== */

#ifndef _INGAME_ENEMY_DAT_H
#define _INGAME_ENEMY_DAT_H

#include "eetypes.h"
#include "enemy.h"                              /* ENE_DAT / AENE_DAT   */
#include "../../common/save_data.h"             /* MC_SAVE_DATA         */

/* Entry counts.  Both are fixed by the ROM's .data block sizes: jene_dat is
 * 0x8d60 bytes of 0x74-byte records, aene_dat 0x3cdc of 0x4c. */
#define JENE_DAT_MAX 312
#define AENE_DAT_MAX 205

/* The tail of jene_dat is reserved for ghosts placed by a battle event rather
 * than by the room's own data -- SetBattleEneDatPos() / SetBattleEneDatRot()
 * write their position and facing in before the fight starts, and reject
 * anything outside [BATTLE_ENE_DAT_TOP, JENE_DAT_MAX).  The ROM inlined both
 * bounds as literals; the names here are the port's. */
#define BATTLE_ENE_DAT_TOP 250

/* ENE_RELASE_TYPE moved to enemy.h -- ENE_WRK holds one by value, and this
 * header includes that one rather than the other way round. */

/* One byte per table entry, and the whole array is what goes to the memory
 * card -- hence the struct rather than a bare char array. */
struct ENE_DAT_SAVE
{
    /* 0x0 */ char release_type;
};

/* ene_type selects which table a (ene_type, dat_no) pair addresses.  The ROM
 * compares against bare 0 and 2 and no enum survives in the debug info, so the
 * literals are kept as-is at the two call sites in enemy_dat.c. */

extern ENE_DAT  jene_dat[JENE_DAT_MAX];
extern int      g_iNumJeneDat;
extern AENE_DAT aene_dat[AENE_DAT_MAX];

/* Overwrite a battle ghost's spawn position / facing before it is loaded. */
void SetBattleEneDatPos(int dat_no, float *vec);
void SetBattleEneDatRot(int dat_no, int iRot);

void release_typeInit(void);
void release_typeClear(void);
void release_typeRegister(int ene_type, int dat_no, ENE_RELASE_TYPE type);
ENE_RELASE_TYPE release_typeGetReleaseType(int ene_type, int dat_no);

/* Hand the save system each release-type array in turn. */
void release_typeSetSaveJ(MC_SAVE_DATA *save);
void release_typeSetSaveA(MC_SAVE_DATA *save);

#endif /* _INGAME_ENEMY_DAT_H */
