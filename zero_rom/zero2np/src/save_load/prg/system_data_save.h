/* ==========================================================================
 *  save_load/prg/system_data_save.h
 *
 *  The system-file save screen (system_data_save.o, .text 0x265348): writes
 *  file 0 of the game-data directory, which holds the option settings, the
 *  language and the clear record.  There is no slot list -- the whole screen
 *  is one confirm prompt plus whatever the card makes it say.
 *
 *  option.c drives it: SystemDataSaveInit() once, then SystemDataSaveMain()
 *  and SystemDataSaveDispMain() every frame until Main() answers non-zero,
 *  then SystemDataSaveEnd().
 *
 *  Reconstructed from the Feb 6 2004 prototype (SLES_523.84).
 * ======================================================================== */

#ifndef _SAVE_LOAD_PRG_SYSTEM_DATA_SAVE_H
#define _SAVE_LOAD_PRG_SYSTEM_DATA_SAVE_H

void SystemDataSaveInit(void *(*mem_get)(int), void (*mem_free)(void *)); /* 0x265420 */

/* 1 saved, -1 backed out, 0 still working. */
int  SystemDataSaveMain(void);      /* 0x265528 */

void SystemDataSaveEnd(void);       /* 0x266878 */
void SystemDataSaveDispMain(void);  /* 0x266900 */

#endif /* _SAVE_LOAD_PRG_SYSTEM_DATA_SAVE_H */
