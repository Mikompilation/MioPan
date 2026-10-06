/* ==========================================================================
 *  ingame/enemy/enemy.h
 *
 *  The enemy module (enemy.o): ten ENE_WRK slots, their per-frame rule, the
 *  load / act / release requests the event system drives them with, and the
 *  draw entry points.
 *
 *  ENE_WRK is the ROM's own 0x490 layout (types.txt), not a runtime subset:
 *  enemy.c indexes almost every field, and enemy_act.o's algorithm interpreter
 *  is handed the embedded ENEALG_WRK by address.  Host offsets drift from
 *  0x340 onwards because every pointer member is 8 bytes here and 4 on target;
 *  nothing indexes the struct by offset, so the comments are documentation.
 *
 *  Reconstructed from the Feb 6 2004 prototype (SLES_523.84), enemy.o.
 * ======================================================================== */

#ifndef _INGAME_ENEMY_ENEMY_H
#define _INGAME_ENEMY_ENEMY_H

#include "eetypes.h"
#include "../../common/variable.h"           /* MOVE_BOX / STATUS_DAT / PLCMN_WRK
                                              * / CWaitVariable / SHUTTER_CHANCE_STATE */
#include "../../graphics/mmanage.h"           /* MMANAGE_ERR                   */
#include "../../graphics/graph3d/ctl/fixed_array.h"
#include "../../graphics/effect/effect_sub.h" /* PP_JUDGE                     */
#include "../photo/spirit_gage.h"            /* CSpiritGage                   */

struct ANI_CTRL;
struct FLY_WRK;

/* ENE_WRK::status.  The ROM stores it as a plain int and compares against bare
 * literals; the names are the port's, but the values are the ROM's. */
enum ENE_STATUS
{
    ENE_STATUS_NO_USE        = 0,
    ENE_STATUS_LOADING       = 1,
    ENE_STATUS_WAIT_ANI_CTRL = 2,
    ENE_STATUS_READY         = 3,
    ENE_STATUS_ACT           = 4,
    ENE_STATUS_RELEASE       = 5
};

/* Model-space reference points maintained per enemy (ROM MPOS, 0xa0).  p0 is
 * the neck -- the damage camera aims at it, and it is what the finder and the
 * spirit gauge sample; p1 is the waist, p2..p5 the four weapon anchors. */
typedef struct                          /* 0xa0 */
{
    /* 0x00 */ sceVu0FVECTOR p0;
    /* 0x10 */ sceVu0FVECTOR p1;
    /* 0x20 */ sceVu0FVECTOR p2;
    /* 0x30 */ sceVu0FVECTOR p3;
    /* 0x40 */ sceVu0FVECTOR p4;
    /* 0x50 */ sceVu0FVECTOR p5;
    /* 0x60 */ sceVu0FVECTOR p6;
    /* 0x70 */ sceVu0FVECTOR p7;
    /* 0x80 */ sceVu0FVECTOR p8;
    /* 0x90 */ sceVu0FVECTOR p9;
} MPOS;

/* The enemy algorithm interpreter's per-ghost cursor.  enemy.c only sets it up
 * (EneActSet / EneBlinkSet) and reads `idx`; enemy_act.o's EJob* handlers do
 * the stepping.  `branch` is what ChangeEneAlgorithm() writes.
 *
 * comm_add / bcomm_add are P_INT, the ROM's 64-bit pointer-or-integer union:
 * the script addresses are computed as sums off the pack base.  The two `top`
 * members are `long` in the ROM -- 8 bytes there, wide enough for a target
 * pointer -- so they take intptr_t here for the same reason SISALG_WRK's
 * comm_add_top does: a MinGW `long` is 4 bytes and would truncate the pack
 * base to its low half.  Both slots are 8-byte aligned either way, so the
 * offsets below are unaffected. */
struct ENEALG_WRK                       /* 0xd0 */
{
    /* 0x00 */ intptr_t comm_add_top;
    /* 0x08 */ u_long  stack_b[16];
    /* 0x88 */ u_long *stack_p;
    /* 0x90 */ intptr_t bcomm_add_top;
    /* 0x98 */ P_INT   bcomm_add;
    /* 0xa0 */ P_INT   comm_add;
    /* 0xa8 */ float   wait_time;
    /* 0xac */ float   loop[2];
    /* 0xb4 */ float   loop_tr[2];
    /* 0xbc */ float   bwait_time;
    /* 0xc0 */ u_char  pos_no_tr[2];
    /* 0xc2 */ u_char  cnt[2];
    /* 0xc4 */ u_char  idx;
    /* 0xc5 */ u_char  job_no;
    /* 0xc6 */ u_char  pos_no;
    /* 0xc7 */ u_char  flag;
    /* 0xc8 */ u_char  branch;
    /* 0xc9 */ u_char  bjob_no;
    /* 0xca */ u_char  bpos_no;
    /* The ROM's tail padding to 0xd0 -- ENEALG_WRK is 8-byte aligned because
     * of comm_add_top, so the last six bytes are implicit there. */
    /* 0xcb */ u_char  _padcb[5];
};

/* Head shared by both enemy tables (ROM 0x38).  Complete layout: enemy_dat.c
 * emits jene_dat[] / aene_dat[] as real initialisers, so every field has to be
 * present and correctly placed.  The two implicit holes (0x1f before attr,
 * 0x37 tail) fall out of the natural 4-byte alignment and match the target. */
struct ENE_DAT_COMMON
{
    /* 0x00 */ int      adpcm_no;
    /* 0x04 */ float    px;
    /* 0x08 */ float    py;
    /* 0x0c */ float    pz;
    /* 0x10 */ int      se_no;
    /* 0x14 */ u_short  mdl_no;
    /* 0x16 */ u_short  anm_no;
    /* 0x18 */ u_short  alg_no;
    /* 0x1a */ u_short  point_base;
    /* 0x1c */ u_short  dir;
    /* 0x1e */ u_char   neck_ctl;
    /* 0x20 */ u_int    attr;
    /* 0x24 */ float    near;
    /* 0x28 */ float    far;
    /* 0x2c */ u_char   blg_r;
    /* 0x2d */ u_char   blg_g;
    /* 0x2e */ u_char   blg_b;
    /* 0x2f */ u_char   balp;
    /* 0x30 */ u_char   ghost_list_no;
    /* 0x31 */ u_char   ghost_list_no_sp;
    /* 0x32 */ u_char   def_type[2];
    /* 0x34 */ u_char   def_size[2];
    /* 0x36 */ u_char   dih_type;
};

/* A hostile ghost's stat block (ROM 0x74).  jene_dat[] in enemy_dat.c. */
struct ENE_DAT
{
    /* 0x00 */ ENE_DAT_COMMON cmn;
    /* 0x38 */ u_short  dst_gthr;
    /* 0x3a */ u_char   way_gthr;
    /* 0x3b */ u_char   atk_ptn;
    /* 0x3c */ float    atk_rng;
    /* 0x40 */ float    hit_rng;
    /* 0x44 */ float    chance_rng;
    /* 0x48 */ int      dead_adpcm;
    /* 0x4c */ short    hit_adjx;
    /* 0x4e */ u_char   hint_pic;
    /* 0x4f */ u_char   aura_alp;
    /* 0x50 */ u_short  trgt_chg;
    /* 0x52 */ u_short  hp;
    /* 0x54 */ u_short  atk_p;
    /* 0x56 */ u_short  atk_h;
    /* ROM 0x58 -- attack power.  sister.c drains her HP by this when a ghost
     * connects with the companion rather than the player. */
    /* 0x58 */ u_char   atk;
    /* 0x59 */ u_char   atk_tm;
    /* 0x5a */ u_char   wspd;
    /* 0x5b */ u_char   rspd;
    /* 0x5c */ u_char   rotsp;
    /* 0x5e */ u_short  hitbk;              /* 1 byte of padding at 0x5d */
    /* 0x60 */ u_int    hp_recv_wait;
    /* 0x64 */ float    hp_recv_vol;
    /* 0x68 */ short    fly_type[3];
    /* 0x6e */ short    child_ene[3];
};

/* A passive / scenery ghost (ROM 0x4c).  aene_dat[] in enemy_dat.c.  These
 * carry no combat stats: they exist to be photographed and to walk a route.
 * `next` is the jene_dat entry the ghost turns into when it stops being
 * passive -- EneRelease() re-seats the slot on it rather than freeing it. */
struct AENE_DAT
{
    /* 0x00 */ ENE_DAT_COMMON cmn;
    /* 0x38 */ u_char   dat_no;
    /* 0x39 */ u_char   soul_no;
    /* 0x3a */ u_short  adpcm_tm;
    /* 0x3c */ short    next;
    /* 0x3e */ u_short  chgattr;
    /* 0x40 */ float    rng;
    /* 0x44 */ short    time;                /* 2 bytes of padding at 0x46 */
    /* 0x48 */ int      se_foot;
};

/* Why a ghost left the field.  Event data compares against these through the
 * GHOST_RELEASE_TYPE open condition; enemy_dat.o owns the per-entry array, but
 * the enum lives here because ENE_WRK holds one by value. */
enum ENE_RELASE_TYPE
{
    ENE_RELEASE_NO_RELEASE = 0,
    ENE_RELEASE_TAKE_PICT  = 1,
    ENE_RELEASE_TIME_OUT   = 2,
    ENE_RELEASE_DIST       = 3,
    ENE_RELEASE_DEAD       = 4,
    ENE_RELEASE_REQ        = 5,
    ENE_RELEASE_MAX_NO     = 6
};

struct ENE_WRK                          /* 0x490 */
{
    /* 0x000 -- the camera-facing billboard anchor EneBlinkPosSet() writes:
     * mpos.p1 pulled 400 units towards the camera. */
    /* 0x000 */ sceVu0FVECTOR bep;
    /* 0x010 */ sceVu0FVECTOR neck_rot;
    /* 0x020 -- per-ghost spawn offset, added to a child ghost's position when
     * the parent drags it along (attr 0x1000). */
    /* 0x020 */ sceVu0FVECTOR adjp;
    /* 0x030 */ MPOS          mpos;
    /* 0x0d0 */ ENEALG_WRK    alg;
    /* 0x1a0 -- the point-depth query CheckEneDepth() posts each frame; result
     * [0] is "the neck is not behind geometry". */
    /* 0x1a0 */ PP_JUDGE      ppj;
    /* 0x250 */ MOVE_BOX      mbox;
    /* 0x2f0 */ CSpiritGage   spirit_gage;
    /* 0x300 */ STATUS_DAT    st;

    /* 0x340 -- the table entry this ghost was spawned from.  `dat` is set for
     * hostile ghosts (type 0/1), `aie` for passive ones (type 2); `cmn_dat`
     * always points at whichever head is live. */
    /* 0x340 */ ENE_DAT  *dat;
    /* 0x344 */ AENE_DAT *aie;

    /* 0x348 / 0x350 -- player-to-ghost distance, this frame and last, indexed
     * [0] player and [1] companion.  PlyrVibCheck() compares the pair to tell
     * approach from retreat, and ChangeAtkTarget() to tell who closed in. */
    /* 0x348 */ float dist_p_e[2];
    /* 0x350 */ float dist_p_e_o[2];
    /* 0x358 -- camera-to-ghost distance. */
    /* 0x358 */ float dist_c_e;

    /* 0x35c */ u_int hp_recv_tm;
    /* 0x360 */ float hp_recv_pt;

    /* 0x364 -- animation rate.  `reso` is the scripted slow-motion factor,
     * `wlk_reso` the idle walk-speed jitter; ani_reso is their product with
     * the global motion rate. */
    /* 0x364 */ float reso;
    /* 0x368 */ float wlk_reso_chg;
    /* 0x36c */ float wlk_reso;

    /* 0x370 -- which unit this ghost is attacking, by pointer. */
    /* 0x370 */ PLCMN_WRK *target;

    /* 0x374 -- transparency, as a 0..1 float for the effects that ride along
     * with the ghost.  tr_common is the global fade the finder drives. */
    /* 0x374 */ float tr_frate;
    /* 0x378 */ float tr_common;

    /* 0x37c -- the two parts-deform effect handles and their drive values.
     * d_pd* feed effect_obj.c's CallPartsDeform5_2 by address, so the effect
     * reads them every frame without a callback. */
    /* 0x37c */ void *pdf;
    /* 0x380 */ void *pdf2;
    /* 0x384 */ float d_pd;
    /* 0x388 */ float d_pd2;
    /* 0x38c */ float d_pda;
    /* 0x390 */ float d_pda2;
    /* 0x394 */ float d_pdc;
    /* 0x398 */ float d_pdc2;
    /* 0x39c */ float d_mpd;
    /* 0x3a0 */ float d_mpd2;

    /* 0x3a4 -- the aura effect around a hostile ghost. */
    /* 0x3a4 */ void  *nee;
    /* 0x3a8 */ float  nee_rate;
    /* 0x3ac */ float  nee_size;
    /* 0x3b0 */ u_int  nee_col;

    /* 0x3b4 -- the per-model extra effect (torch, haze).  effw scales the
     * ghost's alpha into it. */
    /* 0x3b4 */ void  *efpw;
    /* 0x3b8 */ float  effw;

    /* 0x3bc */ float slow_hb_reso;
    /* 0x3c0 */ int   stream_id;
    /* 0x3c4 */ int   status;               /* ENE_STATUS */
    /* 0x3c8 */ ANI_CTRL *ani_ctrl_p;

    /* 0x3cc -- ghost attribute bits, copied out of ENE_DAT_COMMON::attr.
     * 0x00001 distance fade, 0x00002 spawns fully transparent, 0x00004 cloth
     * collision, 0x00008 snap to floor, 0x00010 regenerates HP, 0x00080 never
     * the nearest-ghost pick, 0x00100 excluded from the HP readout, 0x00200
     * walk-speed jitter, 0x01000 drags its children, 0x10000 shares HP with
     * them, 0x20000 one-hit-point boss shell, 0x80000 survives the fade-out. */
    /* 0x3cc */ u_int attr;

    /* 0x3d0 -- the ghost's own directional light colour, pushed into the
     * emulated light set by _enemySetLight(). */
    /* 0x3d0 */ sceVu0FVECTOR directionaldiffuse;

    /* 0x3e0 */ ENE_DAT_COMMON *cmn_dat;
    /* 0x3e4 */ int anm_jibaku_p;
    /* 0x3e8 */ int se_bank_jibaku_no;
    /* 0x3ec */ int se_bank_no;

    /* 0x3f0 -- the flying sub-creatures (fly_ctrl.o), three types of five. */
    /* 0x3f0 */ FLY_WRK *fw[3][5];
    /* 0x42c */ void    *wrkp[3];

    /* 0x438 */ ENE_RELASE_TYPE rel_type;
    /* 0x43c */ CWaitVariable<short> adpcm_tm;
    /* 0x43e */ short  reso_tm;
    /* 0x440 -- frames left in the current attack.  sister.c clears it to
     * break a grab off the companion. */
    /* 0x440 */ u_short atk_tm;
    /* 0x442 */ u_short atk_type;
    /* 0x444 */ short   combo_time;
    /* 0x446 */ u_short tr_time;
    /* 0x448 */ u_short tr2_cnt;
    /* 0x44a -- frames this ghost has been inside the viewfinder frame. */
    /* 0x44a */ u_short in_finder_tm;
    /* 0x44c */ short   dat_no;
    /* 0x44e */ short   fp[2];
    /* 0x452 */ short   dist_in_tm[2];
    /* 0x456 -- scripted slow / highlight countdowns.  Non-zero stm_view also
     * puts the off-screen search mark on this ghost. */
    /* 0x456 */ short   stm_slow;
    /* 0x458 */ short   stm_view;
    /* 0x45a -- paralysis ("mahi"): total frames, and the alternating
     * stop / act windows inside it. */
    /* 0x45a */ short   mahi_total_time;
    /* 0x45c */ short   mahi_cnt;
    /* 0x45e */ short   mahi_one_stop_time;
    /* 0x460 */ short   mahi_one_act_time;
    /* 0x462:0 -- set by SetEneView(); the ROM read-modify-writes the whole
     * doubleword at 0x460, which is why the decompiler shows the neighbouring
     * bytes moving with it. */
    /* 0x462:0 */ u_char bWithSearcher : 1;
    /* 0x463 */ u_char anime_no;
    /* 0x464 -- which unit this ghost is going for; 1 is the companion. */
    /* 0x464 */ u_char target_n;
    /* 0x465 */ u_char tr_max;
    /* 0x466 -- the four transparency terms EneAlphaCtrl() multiplies:
     * tr_rate_alg is the script's, tr2_rate_alg the aura's, and
     * tr_rate_in / tr_rate_out select by finder mode. */
    /* 0x466 */ u_char tr2_rate_alg;
    /* 0x467 */ u_char tr2_base;
    /* 0x468 */ u_char tr2_freq;
    /* 0x469 */ u_char tr2_add;
    /* 0x46a -- final transparency, 0..255.  A fully transparent ghost is not a
     * legal finder target. */
    /* 0x46a */ u_char tr_rate;
    /* 0x46b */ u_char tr_rate_in;
    /* 0x46c */ u_char tr_rate_out;
    /* 0x46d */ u_char tr_rate_alg;
    /* 0x46e */ u_char tr_rate_alg_sp;
    /* 0x46f */ u_char ppj_check;
    /* 0x470 -- ghost class: 0/1 hostile, 2 passive.  Type 2 halves the
     * proximity rumble and takes the AutoEnemy* path throughout. */
    /* 0x470 */ u_char type;
    /* 0x471 */ u_char act_no;
    /* 0x472 */ u_char recog_tm;
    /* 0x473 */ u_char room_no;
    /* 0x474 */ u_char wlk_reso_frm;
    /* 0x475 */ u_char wlk_reso_wait;
    /* 0x476 -- raised once the event system has asked this ghost to act;
     * PreloadedEneAllRelease() frees every slot that has not. */
    /* 0x476 */ char act_flg;
    /* 0x477 */ char slow_hb_wait_frame;
    /* 0x478 */ char slow_hb_frame;
    /* 0x479 */ char _pad479[3];
    /* 0x47c */ int  ani_reso;
    /* 0x480 -- how many shots into a combo this ghost is. */
    /* 0x480 */ char combo_counter;
    /* 0x481 */ char combo_sb_counter;
    /* The ROM pads to 0x490 from here, out of the struct's 16-byte alignment.
     * Not reproduced: 26 members above are pointers, so the host struct is
     * already longer than 0x490 and no padding would make it match.  Every
     * offset up to `dat` at 0x340 does match -- see the header comment. */
};

/* Resident ghost slots.  ROM: fixed_array<ENE_WRK,10> at .data 0x2fe030. */
#define ENE_WRK_MAX 10
extern fixed_array<ENE_WRK, ENE_WRK_MAX> ene_wrk;

/* ---- enemy.c ----------------------------------------------------------- */

void InitEnemy(void);

/* Per-frame rule.  Skipped wholesale while the animation lock is held (the
 * effect positions are still refreshed), and reduced to the ENE_WRK sweep
 * while the draw lock is. */
void EnemyMain(void);
/* The event-phase counterpart: runs every slot without the battle gating, so
 * ghosts keep animating through a cutscene.  Hostile ghosts run under the act
 * lock, passive ones do not. */
void AutoEnemyMain(void);
/* Runs during a door transition -- alpha and aura only, no rule. */
void EnemyDoorMain(void);
/* Runs the slots whose current action is the photographed pose (act_no 4). */
void EnemyPhotoMain(void);
/* Runs one ghost by its data number, restoring the animation lock around it so
 * a scripted single-ghost step still advances the animation. */
void EnemyAnimOne(int iEneID);

void EnemyMotionWork(void);
void EnemyEffectPosUpdate(void);

/* Counted locks.  Draw suppresses animation and drawing entirely, act freezes
 * the rule but keeps the animation running, anim freezes the animation
 * advance so a ghost holds its pose through scripted message playback. */
void EnemyDrawLock(void);
void EnemyDrawUnlock(void);
void EnemyLock(void);
void EnemyUnlock(void);
void EnemyAnimLock(void);
void EnemyAnimUnlock(void);

/* Is any enemy currently loaded and asked to act?  Drives the BATTLE_MODE /
 * BATTLE_END event conditions.  The (ene_type, dat_no) overload asks the same
 * question of one table entry. */
int IsEnemyOn(void);
int IsEnemyOn(int ene_type, int dat_no);

/* Non-zero when no slot is still loading. */
int IsReadyAllEnemy(void);
/* Residency state of one entry in the per-room enemy data table. */
ENE_STATUS GetEneDatStatus(int ene_type, int dat_no);
/* Non-zero when slot wrk_no holds a ghost that is acting and still has HP. */
int IsAliveEnemy(int wrk_no);
/* Non-zero when slot wrk_no is merely acting -- HP is not considered. */
int IsActEnemy(int wrk_no);
/* Non-zero while any live ghost is still inside its combo window. */
int IsInCombo(void);
/* Advances this ghost's combo counter and returns the multiplier for the
 * shot; sw_flg also counts it towards the bonus-shot tally. */
int EneComboReq(ENE_WRK *ew, int sw_flg);

/* Which slot holds (ene_type, dat_no), or -1. */
int SearchEneWrkNo(int ene_type, int dat_no);

/* Load / act / release.  EneLoadReq reserves a slot and starts the model,
 * animation, algorithm and sound-bank requests; EneActReq turns a ready slot
 * into an acting one; EneReleaseReq forces one out. */
MMANAGE_ERR EneLoadReq(int ene_type, int ene_dat_no, int *pWrkNo);
int  EneActReq(int ene_type, int dat_no);
void EneReleaseReq(int ene_type, int dat_no);
void EneAllRelease(void);
/* Releases every loaded-but-not-yet-acting ghost except wrk_no (-1 = all).
 * Non-zero when something was freed; mmanage.c's ModelMemoryFree escalates
 * through it. */
int  PreloadedEneAllRelease(int wrk_no);

/* Swaps the running algorithm branch.  algo_no 0xff instead forces the ghost
 * onto action 8 (the scripted-stop pose). */
int ChangeEneAlgorithm(int ene_type, int dat_no, int algo_no);

/* Points the ghost's script at action act_no and resets the interpreter; the
 * blink (secondary) script is set up separately. */
void EneActSet(ENE_WRK *ew, u_char act_no);
void EneBlinkSet(ENE_WRK *ew);

/* Re-rolls which unit this ghost attacks, weighted by ENE_DAT::trgt_chg. */
void ChangeAtkTargetRnd(ENE_WRK *ew);

/* Raises the "on screen" status bit and posts the depth query. */
void EneInDispChk(ENE_WRK *ew);
/* Posts one point-depth query for `pos`, offset 200 units towards the camera
 * so a ghost standing against a wall still reads as visible. */
void SetEneDepth(ENE_WRK *ew, float *pos);
/* Resolves every posted query.  Called once, after the draw pass. */
void CheckEneDepth(void);

/* Per-frame transparency.  EneAlphaClear() drops tr_common so the next fade
 * starts from nothing. */
void EneAlphaCtrl(ENE_WRK *ew);
void EneAlphaClear(void);

/* Scripted highlight / slow / paralysis / seal states.  The Clear() calls
 * return non-zero when they actually cancelled something. */
void SetEneView(ENE_WRK *ew, int time, int bWithSearcher);
int  SetEneViewClear(ENE_WRK *ew);
void SetEneSlow(ENE_WRK *ew, int time, float reso);
void SetEneSlowMode(ENE_WRK *ew, int time, float reso);
int  SetEneSlowClear(ENE_WRK *ew);
void SetEneSlowHitBack(ENE_WRK *ew, int wait_frame, int hit_back_frame, float reso);
void SetEneMahiMode(ENE_WRK *ew, int all_time, int one_stop_time, int one_act_time);
int  SetEneMahiClear(ENE_WRK *ew);
void SetEneSealMode(ENE_WRK *ew, int iEffFrame);
int  SetEneSealClear(ENE_WRK *ew);

/* Snaps the ghost to the room floor and refreshes its room number. */
void EnePosInfoSet(ENE_WRK *ew);

/* Is the player inside the ghost's `rot`-degree, `dist`-deep view cone? */
int CheckEneView(ENE_WRK *ew, float dist, float rot);

/* World position of the ghost matching (obj_type, obj_id).  Non-zero on
 * success; on failure pos is left alone and a banner is printed.  This is the
 * enemy arm of zero2_util.c's GetObjectPos(). */
int GetEnePos(float *pos, u_char obj_type, int obj_id);

/* Whether the camera has a shot lined up on any ghost, and which kind. */
SHUTTER_CHANCE_STATE ShutterChanceChk(void);

/* Draw.  The matrix accessors hand out the model's root bone coordinate. */
float (*EnemyGetMatrix(ENE_WRK *ew))[4][4];
float (*FlyGetMatrix(FLY_WRK *fw))[4][4];
void  EnemyDrawOne(ENE_WRK *ew);
void  flyAnimationProc(FLY_WRK *fw);
int   enemyIsFlyDraw(FLY_WRK *fw);
int   FlyDrawOne(FLY_WRK *fw);

#endif /* _INGAME_ENEMY_ENEMY_H */
