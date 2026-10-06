/* ==========================================================================
 *  graphics/scene/scene_dat.h
 *
 *  Static scene tables (scene_dat.c).
 *
 *  Reconstructed from the Feb 6 2004 prototype (SLES_523.84).
 * ======================================================================== */

#ifndef _GRAPHICS_SCENE_SCENE_DAT_H
#define _GRAPHICS_SCENE_SCENE_DAT_H

#include "eetypes.h"

enum
{
    SCENE_CUT_TIMING_MAX = 71,
    SCENE_DATA_CMN_MAX   = 72,
};

/* One byte per scene -- the whole struct.  It is a struct rather than a plain
 * u_char[] because that is what the ROM's debug info declares. */
typedef struct _SCENE_DATA_CMN       /* 0x1 */
{
    /* 0x0 */ u_char vol;
} SCENE_DATA_CMN;

#ifdef __cplusplus
extern "C" {
#endif

/* Per-scene camera-cut frame lists, each terminated by -1.  Read only in PAL;
 * see the note in scene_dat.c. */
extern int *scene_cut_timing[SCENE_CUT_TIMING_MAX];

/* Per-scene movie volume, as a percentage of the BGM group volume. */
extern SCENE_DATA_CMN scene_data_cmn[SCENE_DATA_CMN_MAX];

#ifdef __cplusplus
}
#endif

#endif /* _GRAPHICS_SCENE_SCENE_DAT_H */
