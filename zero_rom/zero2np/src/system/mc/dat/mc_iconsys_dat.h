/* ==========================================================================
 *  system/mc/dat/mc_iconsys_dat.h
 *
 *  The lighting rig icon.sys carries for the save icon in the PS2 browser.
 *  MemoryCardSetIconSysData() memcpy's all four tables straight into the
 *  sceMcIconSys it writes out, so the shapes have to match that struct's
 *  BgColor[4] / LightDir[3] / LightColor[3] / Ambient fields exactly.
 *
 *  Data-only: mc_iconsys_dat.o has no .text.
 *
 *  Reconstructed from the Feb 6 2004 prototype (SLES_523.84).
 * ======================================================================== */

#ifndef _SYSTEM_MC_DAT_MC_ICONSYS_DAT_H
#define _SYSTEM_MC_DAT_MC_ICONSYS_DAT_H

/* Background gradient, one RGBA per screen corner, 0..255.  The first three
 * corners are black and only the last carries colour, which gives the icon a
 * single dark-red wash out of one corner. */
extern int mc_bgcolor[4][4];                    /* rdata 3bb570 */

/* Three directional lights and their colours, then the ambient term.  The w
 * component of every vector is unused by the browser and left at 0. */
extern float mc_lightdir[3][4];                 /* rdata 3bb5b0 */
extern float mc_lightcol[3][4];                 /* rdata 3bb5e0 */
extern float mc_ambient[4];                     /* rdata 3bb610 */

#endif /* _SYSTEM_MC_DAT_MC_ICONSYS_DAT_H */
