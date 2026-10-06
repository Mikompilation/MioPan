/* ==========================================================================
 *  ingame/photo/photo_dat.h
 *
 *  The photographable-object registry (photo_dat.o).
 *
 *  photo_dat[] is the static description of every one of the 72 things the
 *  Camera Obscura can be pointed at -- score, list image, hint message and the
 *  six behaviour flags.  photo_dat_save is the matching 72-bit "already shot
 *  this" set that goes into the memory card block.
 *
 *  The runtime half is pd_obj_wrk[]: up to four placed MDAT_OBJ records are
 *  registered at a time (EvSetObjPhotoAble does that through
 *  photo_datObjStart / photo_datObjEnd) and photo_datObjMain() re-evaluates
 *  them every frame -- nearest one for the hint SE, most centred one for the
 *  filament and the finder ring, plus the per-object deform effects.
 *
 *  Reconstructed from the Feb 6 2004 prototype (SLES_523.84), photo_dat.o.
 * ======================================================================== */

#ifndef _INGAME_PHOTO_PHOTO_DAT_H
#define _INGAME_PHOTO_PHOTO_DAT_H

#include "eetypes.h"

#include "../../common/save_data.h"             /* MC_SAVE_DATA               */
#include "../../common/variable.h"              /* CWrkVariable               */
#include "../../graphics/graph2d/g2d_draw.h"    /* SPRT_DAT                   */
#include "../../system/os/system.h"             /* CSYSTEM_SND_BUF_PLAY       */
#include "../map/RegDat.h"                      /* MDAT_OBJ                   */

/* photo_dat[]'s length.  GetPhotoDatNum() hands this out to the event macro
 * interpreter, which range-checks EvSetObjPhotoAble's id against it. */
#define PHOTO_DAT_NUM      72

/* How many placed objects can be photographable at once. */
#define PHOTO_DAT_OBJ_NUM  4

/* --------------------------------------------------------------------------
 *  One photographable subject.
 *
 *  Top / Bottom are the vertical extents used when the shot is scored; the
 *  prototype leaves both zero for every entry.  Dist is the maximum range the
 *  subject stays in frame at, and Point the score it is worth.
 *
 *  The six flags are what actually drive photo_datObjMain():
 *    f_finder       - the object answers the filament / hint at all
 *    f_filament     - it deflects the needle
 *    f_deform       - it gets a heat-haze style parts-deform effect
 *    f_sound        - it plays the proximity hint SE
 *    f_seal_ghost   - it is a sealed ghost: fades in through mGhostAlpha and
 *                     takes the second, tinted deform pass
 *    f_unlock_ghost - shooting it unlocks the ghost list entry
 * ------------------------------------------------------------------------ */
struct PhotoData                    /* 0x18 */
{
    /* 0x00 */ float   Top;
    /* 0x04 */ float   Bottom;
    /* 0x08 */ float   Dist;
    /* 0x0c */ u_short Point;
    /* 0x0e */ short   image;
    /* 0x10 */ short   mestype;
    /* 0x12 */ short   mesnuma;
    /* 0x14:0 */ u_short f_finder       : 1;
    /* 0x14:1 */ u_short f_filament     : 1;
    /* 0x14:2 */ u_short f_deform       : 1;
    /* 0x14:3 */ u_short f_sound        : 1;
    /* 0x14:4 */ u_short f_seal_ghost   : 1;
    /* 0x14:5 */ u_short f_unlock_ghost : 1;
    /* Ghost-list slot this subject fills in, or -1.  CheckHintSE() compares it
     * *unsigned* (the ROM emits lhu + sltiu), so -1 reads as 65535 and takes
     * the "not a ghost" branch -- see the note at that call. */
    /* 0x16 */ short   ghost_list_rel_no;
};

/* --------------------------------------------------------------------------
 *  One live registration.  `pos` is refreshed from the MDAT_OBJ every frame so
 *  the distance tests do not have to chase the record; mGhostAlpha is the
 *  sealed-ghost fade, walked +/-4 a frame by whether the object is inside the
 *  finder ring.
 * ------------------------------------------------------------------------ */
struct PHOTO_DAT_OBJ_WRK            /* 0x20 */
{
    /* 0x00 */ MDAT_OBJ *p_obj;
    /* 0x04 */ void     *p_deform;
    /* 0x08 */ CWrkVariable<short, 0, 128> mGhostAlpha;
    /* 0x10 */ float     pos[4];
};

/* --------------------------------------------------------------------------
 *  The module's exported tables.  Nothing inside photo_dat.o reads the four
 *  ratio tables or hint_dat -- they are the shot-scoring and hint-texture data
 *  the (unreconstructed) photo_make.c / n_plyr_camera.c side consumes.
 * ------------------------------------------------------------------------ */

/* Distance and centring score multipliers, indexed by grade. */
extern float   photo_dist_ratio[10];                /* data 33c430 */
extern float   photo_center_ratio[10];              /* data 33c458 */
extern float   photo_charge_ratio[4];               /* data 33c480 */
extern PhotoData photo_dat[PHOTO_DAT_NUM];          /* data 33c490 */
extern SPRT_DAT  hint_dat[4];                       /* data 33cb50 */

/* Maximum range a subject can be picked up at. */
extern float   photo_rng_tbl[1];                    /* sdata 3f3848 */

/* Viewfinder frame size in screen pixels, { width, height }.  Indexed by
 * camera upgrade level in principle -- the prototype ships one row. */
extern u_short photo_frame_tbl[1][2];               /* sdata 3f3850 */

/* The two proximity-hint SE voices.  Exported, but only CheckHintSE() and the
 * two stop helpers in this file touch them. */
extern CSYSTEM_SND_BUF_PLAY furn_sound_player[2];   /* sdata 3f3880 */

/* --------------------------------------------------------------------------
 *  API
 * ------------------------------------------------------------------------ */
int  GetPhotoDatNum(void);

/* ---- the "already photographed" bit set --------------------------------- */
void photo_datSetSave(MC_SAVE_DATA *save);
void photo_datInit(void);
void photo_datFlgUp(int photo_dat_no);
/* Has photo_dat_no been photographed?  Backs the PHOTO_OBJ / NOT_PHOTO_OBJ
 * event conditions. */
int  photo_datIsUp(int photo_dat_no);
void photo_datFlgDown(int photo_dat_no);
void photo_datRelease(void);

/* Furniture "power" the shot scoring adds; the prototype's body is gone and it
 * returns a constant zero. */
float photo_datGetFurnPowerDegree(void);

/* ---- the live registrations --------------------------------------------- */
void photo_datObjStart(MDAT_OBJ *p_obj);
void photo_datObjEnd(MDAT_OBJ *p_obj);
void photo_datObjInit(void);
void photo_datObjSetSave(MC_SAVE_DATA *save);
void photo_datObjMain(void);
/* The most centred registered object this frame, or NULL. */
MDAT_OBJ *photo_datObjIsPhotoAble(void);
int  photo_datObjIsRespondFilament(void);
void photo_datObjRelease(void);
void photo_datObjFadeOutSE(int iFrame);
/* Suppresses the sealed-ghost fade for one frame; photo_datObjMain() clears it
 * again as soon as finder mode ends. */
void photo_datObjSealGhostDrawLock(void);

#endif /* _INGAME_PHOTO_PHOTO_DAT_H */
