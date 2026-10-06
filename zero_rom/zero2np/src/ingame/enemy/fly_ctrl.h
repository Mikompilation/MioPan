/* ==========================================================================
 *  ingame/enemy/fly_ctrl.h
 *
 *  The flying sub-creatures -- the swarms three ghost models release at the
 *  player.  Forty resident FLY_WRK slots shared by everything on the field;
 *  a hostile ghost claims 5 of them per fly type it declares, and the action
 *  script sends them off one at a time with EneFlyAct().
 *
 *  FLY_WRK's layout is the ROM's (types.txt), because enemy.c indexes it.
 *  Host offsets drift past 0x4 -- the pointer members are 8 bytes here.
 *
 *  Reconstructed from the Feb 6 2004 prototype (SLES_523.84), fly_ctrl.o.
 * ======================================================================== */

#ifndef _INGAME_ENEMY_FLY_CTRL_H
#define _INGAME_ENEMY_FLY_CTRL_H

#include "eetypes.h"
#include "../../common/variable.h"                  /* PLCMN_WRK       */
#include "../../graphics/graph3d/gra3dTypes.h"      /* GRA3DLIGHTDATA  */

struct ANI_CTRL;
struct ENE_WRK;

/* One flying creature's static parameters -- fly_dat[] below.
 *
 * `mdl_no` with bit 0x8000 set means the creature has no model at all and is
 * drawn purely as a torch effect; CheckEffectFly() is that test, and the low
 * bits then select the EffectSetTorch2 type rather than a model number.
 *
 * The three distance bands are far / mid / near: outside fdist the creature
 * flies at fmove and turns at frot, inside ndist at nmove / nrot, and between
 * them both are interpolated.  fstmove / fstrot are the launch values, blended
 * out over the first chg_cnt frames.  All the rot fields are degrees. */
typedef struct                          /* 0x34 */
{
    /* 0x00 -- bit 0 excludes the creature from the photo check, bit 1
     * suppresses its X rotation so it stays level. */
    /* 0x00 */ u_char  attr;
    /* 0x01 */ u_char  alp;
    /* 0x02 */ u_short mdl_no;
    /* 0x04 */ u_short anm_no;
    /* 0x06 */ u_short dmg;
    /* 0x08 */ u_short hit_rng;
    /* 0x0a */ u_short cond;
    /* 0x0c */ u_int   blifetime;
    /* 0x10 */ u_short chg_cnt;     /* 2 bytes of padding at 0x12 */
    /* 0x14 */ float   fstmove;
    /* 0x18 */ float   fstrot;
    /* 0x1c */ float   fdist;
    /* 0x20 */ float   fmove;
    /* 0x24 */ float   frot;
    /* 0x28 */ float   ndist;
    /* 0x2c */ float   nmove;
    /* 0x30 */ float   nrot;
} FLY_DATA;

/* FLY_WRK::sta bits.  0x10 is the allocation flag GetFlyWork() scans for, so
 * everything below it describes a slot that is already owned. */
#define FLY_STA_MOVE    0x0001      /* launched: FlyRule() steps it each frame */
#define FLY_STA_KILL    0x0002      /* faded out; ClearCheckFlyWork() tears it down */
#define FLY_STA_HOLD    0x0004      /* frozen: no lifetime countdown, no movement */
#define FLY_STA_FADE    0x0008      /* fading: alpha drops 8 a frame, then KILL */
#define FLY_STA_USE     0x0010      /* slot in use */

/* One flying creature.  It carries its own light set, which is why the struct
 * is 0x1460 bytes for so few fields. */
struct FLY_WRK                          /* 0x1460 */
{
    /* 0x0000 */ u_short sta;
    /* 0x0004 -- the parent ghost, or null for a creature spawned by FlyInit()
     * with no owner.  ClearCheckFlyWork() fades the swarm out with it. */
    /* 0x0004 */ ENE_WRK   *ew;
    /* 0x0008 */ FLY_DATA  *dat;
    /* 0x000c */ PLCMN_WRK *target;
    /* 0x0010 */ GRA3DLIGHTDATA light;
    /* 0x13b0 */ sceVu0FVECTOR npos;
    /* 0x13c0 */ sceVu0FVECTOR opos;
    /* 0x13d0 */ sceVu0FVECTOR nrot;
    /* 0x13e0 */ sceVu0FVECTOR tpos;
    /* 0x13f0 */ sceVu0FVECTOR trot;
    /* 0x1400 */ float   spd;
    /* 0x1404 */ u_int   life_time;
    /* 0x1408 */ u_int   life_cnt;
    /* 0x140c */ float   trace_ang;
    /* 0x1410 -- frames left of homing.  When it runs out the turn rate is
     * forced to zero and the creature flies straight on. */
    /* 0x1410 */ u_int   trace_time;
    /* 0x1414 */ u_short now_cnt;
    /* 0x1416 */ u_char  alp;
    /* 0x1418 */ void     *efpw;
    /* 0x141c */ ANI_CTRL *ani_ctrl;
    /* 0x1420 -- model / animation base addresses.  `int` on target because a
     * pointer is 4 bytes there; they have to be real pointers here, since
     * mmanageIsReadyMdl() stores through them. */
    /* 0x1420 */ void *mdl_p;
    /* 0x1424 */ int   mdl_no;
    /* 0x1428 */ void *anm_p;
    /* 0x142c */ int   anm_no;
    /* 0x1430 */ int   init_flow;
    /* 0x1434 -- the wobble that keeps a swarm from flying in a straight line.
     * adjp_cnt sweeps at adjp_add per frame with amplitude adjp_span, and
     * adjp_ang rotates the offset half a degree a frame. */
    /* 0x1434 */ float adjp_cnt;
    /* 0x1438 */ float adjp_span;
    /* 0x143c */ float adjp_add;
    /* 0x1440 */ float adjp_ang;
    /* 0x1444 */ u_char _pad1444[0x0c];
    /* 0x1450 -- the position flyAnimationProc() drops into the root bone. */
    /* 0x1450 */ sceVu0FVECTOR adjv;
};

/* The six creature types (data 312948).  Not static in the ROM. */
extern FLY_DATA fly_dat[6];

/* ---- The standalone (ownerless) API.  Both are dead code in this build: no
 * jal anywhere in the ROM reaches either. ---- */

/* Claim a slot and start loading type's model.  Null if all 40 are taken. */
FLY_WRK *FlyInit(u_char type);
/* Launch an already-claimed ownerless creature.  Non-zero once it is away. */
int FlyAct(FLY_WRK *fw, u_char type, float *pos, float *rot, PLCMN_WRK *target);

/* ---- The ghost-owned API ---- */

/* Allocate the parent's swarm -- 5 slots per declared fly type.  Called from
 * enemyReqData() once the parent's own model request has gone through. */
void EneFlyInit(ENE_WRK *ew);
/* Non-zero once every creature in the swarm has its model and animation.
 * enemyIsReadyData() ANDs it into the parent's ready state. */
int  EneFlyModelInitWait(ENE_WRK *ew);
/* Launch the next idle creature of the given type from the parent's swarm. */
void EneFlyAct(ENE_WRK *ew, u_char type, float *pos, float *rot,
               PLCMN_WRK *target);
/* Tear the swarm's animation controls down with the parent. */
void EneFlyAnmctrlRelease(ENE_WRK *ew);
/* Cut a released ghost's swarm loose: every creature it owns starts fading. */
void EraseEneFlyWork(ENE_WRK *ew);

/* ---- Per-creature ---- */

/* Place a claimed creature at pos/rot and set it flying at target. */
void SetFlyOne(FLY_WRK *fw, ENE_WRK *ew, u_char type, float *pos, float *rot,
               PLCMN_WRK *target);
void FlyModelRelease(FLY_WRK *fw);
void FlyEffectRelease(FLY_WRK *fw);
void FlyRelease(FLY_WRK *fw);

/* ---- Per-frame ---- */

/* One frame of every live creature, run at the end of EnemyMain(). */
void FlyRule(void);
/* Fade out every creature the player has in shot.  Called from player.o. */
void PhotoFlyChk(void);
/* Push one creature's own light set before it is drawn. */
void SetFlyLight(FLY_WRK *fw);

#endif /* _INGAME_ENEMY_FLY_CTRL_H */
