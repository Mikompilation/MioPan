/* ==========================================================================
 *  ingame/map/MapAnim.h
 *
 *  Map animation controller.  Every MapAnim.o symbol in ZERO2.MAP is
 *  implemented in MapAnim.c; the statics (MapAnimGetCtlPtr,
 *  MapAnimGetFreeCtlEx, MapAnimGetFreeCtl, MapAnimGetCtlArea,
 *  MapAnimGetUseHeader, MapAnimOne, MapManimGetFreeMatrix,
 *  MapManimDeleteMatrix, MapManimGetMatrixNumSgd, MapManimGetHeadPtr,
 *  MapManimGetFreeSpace, MapManimAddMatrix, MapManimSetAnimSub,
 *  MapManimSetMatrixSgdOneSub, MapManimSetMatrixSgdOne) are not exported.
 *
 *  Two systems live here:
 *
 *    MapAnim*  -- one animation per registration, 32 slots, each owning an
 *                 ANI_CTRL and writing its result into a caller-supplied
 *                 matrix.  Animated furniture and swinging doors run on this.
 *
 *    MapManim* -- multi-instance: one head per registration, a linked list of
 *                 MAPMANIM_MATRIX nodes underneath it, and one MapAnim slot
 *                 per node.  MapManimProc() copies the nodes' matrices into
 *                 the model's SGD coordinate array each frame, which is how a
 *                 whole stand of trees animates off a single model.
 *
 *  Reconstructed from the Feb 6 2004 prototype (SLES_523.84), MapAnim.o
 *  0x001035c0..0x001052e0.
 * ======================================================================== */

#ifndef _INGAME_MAP_MAPANIM_H
#define _INGAME_MAP_MAPANIM_H

#include "eetypes.h"
#include "../../graphics/graph3d/sgd_types.h"   /* SGDFILEHEADER */
#include "../../graphics/motion/mdlwork.h"      /* ANI_CTRL / MOT_CTRL / RST_DATA */

/* Slot counts, straight off the fixed_array declarations in globals.txt. */
#define MAP_ANIM_CTL_MAX        32      /* bss 3f65a0, 0x1000 bytes  */
#define MAP_ANIM_MOT_CTL_MAX    2       /* bss 3f75a0, 0x480 bytes   */
#define MAPMANIM_MATRIX_MAX     255     /* bss 3f7a20, 0x4fb0 bytes  */
#define MAPMANIM_HEAD_MAX       64      /* bss 3fc9d0, 0x500 bytes   */

/* MAP_ANIM_CTL::flg.
 *
 * 0x04 / 0x08 are the room-buffer tags MapAnimDeleteBuffID() sweeps on, and
 * 0x400 / 0x800 pick one of the two shared ANI_CTRLs instead of allocating
 * from the motion pool -- the door path uses them so a transition can never
 * exhaust motGetANI_CTRL(). */
#define MAPANIM_FLG_USE         0x0001  /* slot occupied                    */
#define MAPANIM_FLG_STOP        0x0002  /* held: MapAnimProc() skips it     */
#define MAPANIM_FLG_BUFF0       0x0004  /* owned by room buffer 0           */
#define MAPANIM_FLG_BUFF1       0x0008  /* owned by room buffer 1           */
#define MAPANIM_FLG_LOOP        0x0010  /* restart instead of finishing     */
#define MAPANIM_FLG_REVERSE     0x0020  /* add pi to the Y rotation         */
#define MAPANIM_FLG_PLAYING     0x0040  /* MapObjUpdateAnim() waits on this */
#define MAPANIM_FLG_KEEP        0x0080  /* stop at the end, do not delete   */
#define MAPANIM_FLG_NOLOCAL     0x0100  /* skip the local matrix on `mat`   */
#define MAPANIM_FLG_CALLBACK    0x0200  /* func is installed                */
#define MAPANIM_FLG_MOTCTL0     0x0400  /* borrow MapAnimMotCtl[0]          */
#define MAPANIM_FLG_MOTCTL1     0x0800  /* borrow MapAnimMotCtl[1]          */
#define MAPANIM_FLG_MOTCTL_MASK 0x0c00

/* MapAnimOne()'s a_flg, which MapAnimRegistEx() forwards as iFflg and
 * MapAnimProc() rebuilds from MAPANIM_FLG_NOLOCAL each frame. */
#define MAPANIM_ONE_NOROT       0x0001  /* drop the key's own rotation      */
#define MAPANIM_ONE_NOADVANCE   0x0002  /* evaluate without stepping frame  */
#define MAPANIM_ONE_NOLOCAL     0x0100  /* do not fold in the local matrix  */

/* MapManimSetAnimSub()'s e_flg: hand alternate nodes the two shared
 * ANI_CTRLs.  Only the door sets it (MapDoorAnim passes 0x1100). */
#define MAPMANIM_EFLG_SPLITCTL  0x1000

/* Animations tick in hundredths of a frame; PAL steps 6/5 as far per field so
 * a 50 Hz field rate covers the same wall-clock span as 60 Hz NTSC. */
#define MAPANIM_FRAME_SCALE     100
#define MAPANIM_FRAME_STEP_NTSC 100
#define MAPANIM_FRAME_STEP_PAL  120

/* Terminator in a MapManim anim_id[] list.  A negative entry that is not the
 * terminator is a hole: that matrix slot is skipped, the walk continues. */
#define MAPMANIM_ID_END         (-99)

/* Per-frame callback: `mat` is the animation's world matrix, `dat`/`dat2` are
 * whatever MapAnimSetCallback() was handed.  `id` arrives as -1 on the frame
 * the motion runs past its last key. */
typedef int (*MAPANIM_FUNC)(int id, float (*mat)[4], void *dat, void *dat2);

/* One animation slot.
 *
 * Offsets are the ROM's.  `mot_p`, `mat`, `ctl`, `func_dat` and `func` are all
 * pointers, so everything from 0x08 on sits further along on a 64-bit host --
 * the comments document the original layout, not this build's. */
typedef struct                          /* 0x80 */
{
    /* 0x00 */ short          id;
    /* 0x02 */ short          flg;
    /* 0x04 */ u_int          frame;       /* play cursor, x100            */
    /* 0x08 */ u_int         *mot_p;       /* motion data top              */
    /* 0x0c */ float        (*mat)[4][4];  /* where the result is written  */
    /* 0x10 */ ANI_CTRL      *ctl;
    /* 0x20 */ float          offset[4];   /* local translation            */
    /* 0x30 */ float          rot[4];      /* local rotation, radians      */
    /* 0x40 */ RST_DATA       rst;         /* this frame's decoded key     */
    /* 0x70 */ void          *func_dat[2];
    /* 0x78 */ MAPANIM_FUNC   func;
} MAP_ANIM_CTL;

/* One instance inside a multi-instance registration.  `mat_id` is the SGD
 * coordinate block this instance drives; nodes stay in ascending mat_id order
 * because MapManimSetMatrixSgdOneSub() walks blocks and nodes together in a
 * single pass. */
struct MAPMANIM_MATRIX                  /* 0x50 */
{
    /* 0x00 */ short              flg;       /* bit 0: in use */
    /* 0x02 */ short              anim_id;
    /* 0x04 */ int                mat_id;
    /* 0x08 */ char              *mot_addr;
    /* 0x0c */ MAPMANIM_MATRIX   *next;
    /* 0x10 */ float              mat[4][4];
};

/* A multi-instance registration.  buff_id == -1 marks the head free. */
typedef struct                          /* 0x14 */
{
    /* 0x00 */ int              id;
    /* 0x04 */ int              buff_id;
    /* 0x08 */ char            *mdl_addr;
    /* 0x0c */ MAPMANIM_MATRIX *mat_addr;   /* list head */
    /* 0x10 */ int              mat_num;
} MAPMANIM_HEAD;


/* ---- MapAnim: single-instance ------------------------------------------ */

void    MapAnimInit(void);
void    MapAnimProc(void);

/* Registers `mot_p` playing on `mdl_p`, drawn through `mat`.  Returns the
 * animation id, or -1 when no slot is free.  A model/matrix pair that is
 * already registered is reused rather than duplicated.
 *
 * `iFlg` is OR'd into MAP_ANIM_CTL::flg (so 0x400/0x800 pick a shared
 * ANI_CTRL); `iFflg` goes straight through to the first frame's evaluation. */
int     MapAnimRegistEx(int play_id, u_int *mdl_p, u_int *mot_p,
                        float (*mat)[4][4], float *offset, float *rot,
                        int iFlg, int iFflg);
int     MapAnimRegist(int play_id, u_int *mdl_p, u_int *mot_p,
                      float (*mat)[4][4], float *offset, float *rot);

/* Restarts the animation on sub-sequence `play_id`, keeping the slot and its
 * ANI_CTRL.  Returns the animation id, or -1 if `anim_id` is out of range. */
int     MapAnimCall(int anim_id, int play_id);

void    MapAnimDelete(int id);
void    MapAnimDeleteBuffID(int buff_id);
void    MapAnimDeleteAll(void);

/* Non-zero while animation `id` is still occupied; -1 for a bad id. */
int     MapAnimCheckPlay(int id);

void    MapAnimSetFlg(int id, int flg);
void    MapAnimDeleteFlg(int id, int flg);
short   MapAnimGetFlg(int id);

/* `func` NULL clears MAPANIM_FLG_CALLBACK again. */
void    MapAnimSetCallback(int anim_id, MAPANIM_FUNC func,
                           void *dat, void *dat2);

/* Attaches the model's morph (MIME) set to the animation's ANI_CTRL. */
u_int  *MapAnimRegstMim(int id, u_int *mim_p, u_int *pkt_p);

/* Frame-key helpers. */
void    MapAnimGetRstMix(MAP_ANIM_CTL *pMapAniCtl, MOT_CTRL *pMotCtl);
void    MapAnimMixRst(float (*aRetMatrix)[4], RST_DATA *pStartRst,
                      RST_DATA *pEndRst, float fRate);


/* ---- MapManim: multi-instance ------------------------------------------ */

void    MapManimInit(void);
void    MapManimProc(void);

/* `anim_id` is both input and output: going in it is the per-instance
 * sequence list terminated by MAPMANIM_ID_END, coming back each entry holds
 * that instance's MapAnim id.  Returns the head id. */
int     MapManimSetAnim(int buff_id, char *mdl_addr, char *mot_addr,
                        int *anim_id, float *offset, float *rot, int st_frame);
int     MapManimSetAnimEx(int buff_id, char *mdl_addr, char *mot_addr,
                          int *anim_id, float *offset, float *rot,
                          int st_frame, int e_flg);
/* As MapManimSetAnimEx, but with no model of its own: the instance count is
 * given rather than read out of an SGD. */
int     MapManimSetAnimEx2(int buff_id, int mat_num, char *mot_addr,
                           int *anim_id, float *offset, float *rot,
                           int st_frame, int e_flg);

void    MapManimDeleteOne(int id);
void    MapManimDelete(int buff_id);

/* Writes head `id`'s instance matrices into `pSGDHead`'s coordinate array. */
void    MapManimSetMatrixSgdOne2(SGDFILEHEADER *pSGDHead, int id);
/* Resets every instance matrix of head `id`, returning the model to its bind
 * pose, and pushes that through to the SGD immediately. */
void    MapManimUnitMatrix(int id);

#endif /* _INGAME_MAP_MAPANIM_H */
