/* ==========================================================================
 *  system/mc/prg/pc_save.h
 *
 *  Development save transfer (pc_save.o, .text 0x22dcd8).
 *
 *  Writes and reads save slot 1 as a plain file on the *development host* --
 *  "host0:" is the dev-kit's link to the PC, not a memory card and not the
 *  PocketStation.  It exists so a save state could be captured and restored
 *  without a card, and it goes through the same marshalling as the real save.
 *
 *  Reconstructed from the Feb 6 2004 prototype (SLES_523.84).
 * ======================================================================== */

#ifndef _SYSTEM_MC_PRG_PC_SAVE_H
#define _SYSTEM_MC_PRG_PC_SAVE_H

void SavePCFile(void);          /* 0x22dcd8 */
void LoadPCFile(void);          /* 0x22ddc0 */

#endif /* _SYSTEM_MC_PRG_PC_SAVE_H */
