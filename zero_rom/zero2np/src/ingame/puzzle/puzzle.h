/* ==========================================================================
 *  ingame/puzzle/puzzle.h
 *
 *  The puzzle driver (puzzle.o): the six puzzles' shared entry point, the
 *  confirmation prompt in front of them, the cross-fade that hands the screen
 *  over, and the six clear flags the event system tests.
 *
 *  Reconstructed from the Feb 6 2004 prototype (SLES_523.84).
 * ======================================================================== */

#ifndef _INGAME_PUZZLE_PUZZLE_H
#define _INGAME_PUZZLE_PUZZLE_H

#include "eetypes.h"

#include "../../common/save_data.h"                 /* MC_SAVE_DATA           */
#include "../../main/phasefunc.h"                   /* GPHASE_ENUM            */

/* --------------------------------------------------------------------------
 *  Puzzle ids.  These index pzl_conf_msg[], tex_data_tbl[], snd_label_tbl[]
 *  and clear_puzzle[], and are what PuzzleStartReq() is called with from the
 *  event stream (ev_macro.c's PUZZLE_START).
 *
 *  The ROM has no enum for them -- every site is a bare integer -- but the
 *  ordering is fixed by PzlCrossMovePuzzlePhase()'s switch, which maps them
 *  one-for-one onto GID_PUZZLE_HINA .. GID_PUZZLE_KAI2.
 * ------------------------------------------------------------------------ */
#define PZL_ID_HINA     0       /* hina-dan, the doll-shelf sliding puzzle    */
#define PZL_ID_ROKU     1       /* rokumen, the six-book shelf                */
#define PZL_ID_KAZA     2       /* kazaguruma, pinwheel panels                */
#define PZL_ID_KAZA2    3       /* kazaguruma, second board                   */
#define PZL_ID_KAI1     4       /* kai, first rotation lock                   */
#define PZL_ID_KAI2     5       /* kai, second rotation lock                  */
#define PZL_ID_MAX      6

/* --------------------------------------------------------------------------
 *  Driver state.  `step` walks the pre-puzzle sequence:
 *      0  the yes/no prompt is up            (PzlExeSelPad)
 *      1  the after-answer message is up     (PzlExeConfMsgDispPad)
 *      2  waiting for the puzzle's pak       (PzlExePuzzleLoadWait)
 *  PzlExeCtrlInit() starts ids 0 and 1 at step 0 and 2..5 at step 2, which is
 *  what decides whether a puzzle asks before it opens.
 *
 *  con_color / con_alpha latch the story contrast filter as it was when the
 *  puzzle was requested, so PuzzleEndCmnExe() can put it back.
 * ------------------------------------------------------------------------ */
typedef struct                      /* 0x18 */
{
    /* 0x00 */ char  step;
    /* 0x01 */ char  conf_csr;      /* 0 = yes, 1 = no                       */
    /* 0x02 */ short fade_timer;
    /* 0x04 */ int   puzzle_id;
    /* 0x08 */ int   con_color;
    /* 0x0c */ int   con_alpha;
    /* 0x10 */ int   snd_id;        /* CSND_BUF play id of the menu cue      */
    /* 0x14 */ int   snd_bank_id;   /* the puzzle's own sound bank           */
} PZL_EXE_CTRL;

void   PuzzleInit(void);
void   PuzzleRelease(void);
void   PuzzleStartReq(int puzzle_id);

/* The loaded texture pak, shared by every puzzle's display code, and the
 * sound bank claimed for it by init_Puzzle_CrossFade(). */
void  *GetPzlTexDataAddr(void);
int    GetPzlSndBankID(void);

void   PuzzleClear(int puzzle_id);
u_char GetPuzzleClearInfo(int puzzle_id);
void   SetSave_ClearPuzzle(MC_SAVE_DATA *data);

void       init_Puzzle_InConf(void);
GPHASE_ENUM one_Puzzle_InConf(GPHASE_ENUM dummy);
void       end_Puzzle_InConf(void);
void       init_Puzzle_CrossFade(void);
GPHASE_ENUM one_Puzzle_CrossFade(GPHASE_ENUM dummy);
void       end_Puzzle_CrossFade(void);
void       init_Puzzle_Hina(void);
GPHASE_ENUM one_Puzzle_Hina(GPHASE_ENUM dummy);
void       end_Puzzle_Hina(void);
void       init_Puzzle_Roku(void);
GPHASE_ENUM one_Puzzle_Roku(GPHASE_ENUM dummy);
void       end_Puzzle_Roku(void);
void       init_Puzzle_Kaza(void);
GPHASE_ENUM one_Puzzle_Kaza(GPHASE_ENUM dummy);
void       end_Puzzle_Kaza(void);
void       init_Puzzle_Kaza2(void);
GPHASE_ENUM one_Puzzle_Kaza2(GPHASE_ENUM dummy);
void       end_Puzzle_Kaza2(void);
void       init_Puzzle_Kai1(void);
GPHASE_ENUM one_Puzzle_Kai1(GPHASE_ENUM dummy);
void       end_Puzzle_Kai1(void);
void       init_Puzzle_Kai2(void);
GPHASE_ENUM one_Puzzle_Kai2(GPHASE_ENUM dummy);
void       end_Puzzle_Kai2(void);

#endif /* _INGAME_PUZZLE_PUZZLE_H */
