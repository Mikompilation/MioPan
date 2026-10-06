/* ==========================================================================
 *  system/mc/prg/mc_icon_sys.h
 *
 *  Write icon.sys onto the card (mc_icon_sys.o, .text 0x1e0678).
 *
 *  Init() builds the whole 964-byte structure into this module's .bss (which is
 *  why its .bss is 0x3d4 rather than the 0x10 its siblings need), and Main()
 *  is a thin wrapper over mc_make_file.c.
 *
 *  Reconstructed from the Feb 6 2004 prototype (SLES_523.84).
 * ======================================================================== */

#ifndef _SYSTEM_MC_PRG_MC_ICON_SYS_H
#define _SYSTEM_MC_PRG_MC_ICON_SYS_H

void MemoryCardIconSysInit(int port, int slot, int dir_label);   /* 0x1e0678 */
int  MemoryCardIconSysMain(void);                               /* 0x1e07b8 */

#endif /* _SYSTEM_MC_PRG_MC_ICON_SYS_H */
