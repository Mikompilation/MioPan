/* ==========================================================================
 *  outgame/lang_sel.h
 *
 *  The language-select screen (lang_sel.c) and the language setting it
 *  commits.  lang_check.c drives the setting half directly.
 *
 *  Reconstructed from the Feb 6 2004 prototype (SLES_523.84).
 * ======================================================================== */

#ifndef _OUTGAME_LANG_SEL_H
#define _OUTGAME_LANG_SEL_H

#include <sys/types.h>              /* u_char */

#include "../common/save_data.h"    /* MC_SAVE_DATA */

/* lang_sel.c's work block (sbss 3f4d50). */
typedef struct                      /* 0x8 */
{
    /* 0x0 */ int  anim_timer;      /* background scroll clock */
    /* 0x4 */ char step;
    /* 0x5 */ char csr;             /* 0..4, seeded from GetLanguage() */
} LANG_SEL_CTRL;

/* Commit `set_language` to the system.  Called by lang_check.c once it has a
 * language from the card, and by this screen once the player picks one. */
void LoadLangSetUp(void);                   /* 0x1d4c28 */

/* Pull the per-language message and font paks to their fixed addresses. */
void LangData_LoadReq(void);                /* 0x1d4c48 */
int  LangData_LoadWait(void);               /* 0x1d4c90 */

/* The byte that ends up in the memory-card system file. */
void Set_McSaveLanguage(u_char language);   /* 0x1d4cd8 */
void SetSave_Language(MC_SAVE_DATA *data);  /* 0x1d4ce0 */

#endif /* _OUTGAME_LANG_SEL_H */
