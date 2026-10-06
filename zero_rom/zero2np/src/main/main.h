/* ==========================================================================
 *  main.h
 *
 *  Public interface for src/main/main.c: the program entry point, the top two
 *  GPhase layers (GID_SUPER / GID_BOOT_INIT / GID_SOFTRESETMAIN callbacks),
 *  the soft-reset lock API, and the subtitle-buffer accessor.
 *
 *  The per-phase callbacks below are not called directly; they are registered
 *  in the GPhase callback tables (ini_func/pre_func/after_func/end_func) and
 *  so need external linkage.  SoftResetLock/SoftResetUnlock are called from
 *  many state modules (memory card, language select, UBI/Tecmo/Project modes,
 *  story puzzle, ...); GetSubTitleAddr backs SubTitleDataPtrGet().
 *
 *  Reconstructed from the Feb 6 2004 prototype (SLES_523.84).
 * ======================================================================== */

#ifndef _MAIN_H
#define _MAIN_H

#include "gphase.h"             /* GPHASE_ENUM */

/* Dump the fixed EE memory map and assert the per-file load budgets. */
void DebugMemoryCheck(void);

/* Assert hook installed via SetPrintAssert(). */
void newAssert(char *pStr);

/* GID_SUPER (root phase) callbacks. */
void       init_super(void);
void       end_super(void);
GPHASE_ENUM pre_super(GPHASE_ENUM super);
GPHASE_ENUM after_super(GPHASE_ENUM result);

/* GID_BOOT_INIT callbacks. */
void       init_Boot_Init(void);
void       end_Boot_Init(void);
GPHASE_ENUM one_Boot_Init(GPHASE_ENUM dummy);

/* GID_SOFTRESETMAIN callbacks. */
void       init_SoftResetMain(void);
GPHASE_ENUM one_SoftResetMain(GPHASE_ENUM dummy);
void       end_SoftResetMain(void);

/* Soft-reset lock-out (nestable): while locked, the L1+L2+R1+R2+SELECT+START
 * combo will not trigger a reset. */
void SoftResetLock(void);
void SoftResetUnlock(void);

/* Pointer to the loaded subtitle file buffer (NULL until GID_BOOT_INIT). */
int *GetSubTitleAddr(void);

#endif /* _MAIN_H */
