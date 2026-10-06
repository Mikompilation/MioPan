/* ==========================================================================
 *  ingame/plyr/sis_algo.h
 *
 *  Companion behaviour-script interpreter (sis_algo.o).
 *
 *  The script is SIS_ALG_OBJ (cddat file 302), loaded by sis_mdl.c and handed
 *  over through sis_mdlGetAlgAdrs().  It opens with a table of u16 entry
 *  offsets -- ReqSisAlgo(n) selects entry n -- and the body is a byte stream
 *  of one-byte opcodes, each followed by its own operands.  SisCtrlTbl[]
 *  dispatches the twenty opcodes; SISALG_WRK (variable.h) is the machine
 *  state, living inside SIS_WRK::alg.
 *
 *  Every jump operand is a u16 offset from the script base, not an absolute
 *  address, which is what lets the whole thing be position independent.
 *
 *  Reconstructed from the Feb 6 2004 prototype (SLES_523.84).
 * ======================================================================== */

#ifndef _INGAME_PLYR_SIS_ALGO_H
#define _INGAME_PLYR_SIS_ALGO_H

#include "eetypes.h"

#include "../../common/variable.h"      /* SISALG_WRK */

/* Per-frame scratch shared with sister.c. */
typedef struct                      /* 0x10 */
{
    /* 0x0 */ int    loop_cnt;
    /* 0x4 */ u_char hit;               /* last SisterPosUpdate() wall hit   */
    /* 0x8 */ u_long ghost_erase_tm;    /* frames with no ghost within 8000  */
} SIS_LOCAL_ALG;

extern SIS_LOCAL_ALG sis_lalg;          /* data 3450c0 */

/* Opcode numbers, in SisCtrlTbl[] order. */
enum SIS_ALG_OP
{
    SA_END_ACT       = 0,
    SA_LOOP_SET      = 1,
    SA_LOOP_END      = 2,
    SA_JUMP          = 3,
    SA_NECK_POS      = 4,
    SA_ANIM_SET      = 5,
    SA_ANIM_END      = 6,
    SA_ENE_DIST      = 7,
    SA_ALGO_CHG      = 8,
    SA_ALGO_TRACE    = 9,
    SA_ROT_ENE_DIR   = 10,
    SA_OBJ_HIT       = 11,
    SA_ENE_OUT_TIME  = 12,
    SA_PLYR_DIST     = 13,
    SA_WAIT          = 14,
    SA_GET_ENE_NUM   = 15,
    SA_SET_DIRECTION = 16,
    SA_REQ_SE        = 17,
    SA_RND_JUMP      = 18,
    SA_SET_MVSTA     = 19
};

/* Points the machine at script entry `alg_no` and restarts it. */
void SisActSet(u_char alg_no);
void ReqSisAlgo(u_char no);

/* Runs opcodes until one asks to wait (SaWait) or the script ends
 * (SaEndAct).  SisAlgoMain() is the per-frame entry sister.c dispatches to
 * for sis_algo.amode 2 and 7. */
void SisAlgCtrl(void);
void SisAlgoMain(void);

/* Ghosts of type < 2 that are alive, active, and within `dist` of her. */
int  EneDistCount(float dist);
void CountAlgTime(void);

/* Opcode handlers.  All are reachable only through SisCtrlTbl[], but the ROM
 * exports them, so they are declared here too. */
void SaEndAct(SISALG_WRK *alg);
void SaLoopSet(SISALG_WRK *alg);
void SaLoopEnd(SISALG_WRK *alg);
void SaJump(SISALG_WRK *alg);
void SaNeckPos(SISALG_WRK *alg);
void SaAnimSet(SISALG_WRK *alg);
void SaAnimEnd(SISALG_WRK *alg);
void SaEneDist(SISALG_WRK *alg);
void SaAlgoChg(SISALG_WRK *alg);
void SaAlgoTrace(SISALG_WRK *alg);
void SaRotEneDir(SISALG_WRK *alg);
void SaObjHit(SISALG_WRK *alg);
void SaEneOutTime(SISALG_WRK *alg);
void SaPlyrDist(SISALG_WRK *alg);
void SaWait(SISALG_WRK *alg);
void SaGetEneNum(SISALG_WRK *alg);
void SaSetDirection(SISALG_WRK *alg);
void SaReqSE(SISALG_WRK *alg);
void SaRndJump(SISALG_WRK *alg);
void SaSetMvsta(SISALG_WRK *alg);

#endif /* _INGAME_PLYR_SIS_ALGO_H */
