/* ==========================================================================
 *  save_load/prg/save_load_disp.h
 *
 *  Shared save / load screen drawing primitives (save_load_disp.o, .text
 *  0x244ff0).  The load-game screen (outgame/loadgame.c) and the save screen
 *  (game_data_save.c) compose their frames entirely out of these; the caller
 *  supplies the texture pak address and a slot index, and each routine knows
 *  its own placement.
 *
 *  Every routine takes (off_x, off_y, alpha) first.  Both callers pass
 *  0, 0 for the offsets at every site, and several routines ignore them
 *  outright -- see the notes in save_load_disp.c.
 *
 *  Reconstructed from the Feb 6 2004 prototype (SLES_523.84).
 * ======================================================================== */

#ifndef _SAVE_LOAD_PRG_SAVE_LOAD_DISP_H
#define _SAVE_LOAD_PRG_SAVE_LOAD_DISP_H

#include "eetypes.h"
#include "../../common/variable.h"      /* TIME_INFO */

/* Slots are one set of records drawn five times, this far apart. */
#define SAVE_LOAD_DATA_PITCH 118

/* Frame furniture. */
void SaveLoadCmnBaseDisp(int off_x, int off_y, u_char alpha, void *pk2_addr,
                         int disp_slot);                                /* 0x244ff0 */
void SaveLoadMcCheckDisp(int off_x, int off_y, u_char alpha, void *pk2_addr); /* 0x2451b0 */
void SaveLoadTitleFrameDisp(int off_x, int off_y, u_char alpha, void *pk2_addr); /* 0x245260 */
void SaveLoadTitleSaveDisp(int off_x, int off_y, u_char alpha, void *pk2_addr);  /* 0x245328 */
void SaveLoadTitleLoadDisp(int off_x, int off_y, u_char alpha, void *pk2_addr);  /* 0x2453f0 */
void SaveLoadFrameDisp(int off_x, int off_y, u_char alpha, void *pk2_addr);      /* 0x2454b8 */
void SaveLoadClearFrameDisp(int off_x, int off_y, u_char alpha, void *pk2_addr,
                            int disp_label);                            /* 0x245688 */
void SaveLoadCursorDisp(int off_x, int off_y, u_char alpha, u_char rgb,
                        void *pk2_addr, int disp_label);                /* 0x245750 */
void SaveLoadMemoryCardSlotDisp(int off_x, int off_y, u_char alpha, void *pk2_addr,
                                int slot_label);                        /* 0x2461a8 */
void SaveLoadCaptionDisp(int off_x, int off_y, u_char alpha);           /* 0x2462f0 */

/* Per-slot rows.  disp_label is 0..4. */
void SaveLoadSelFlareDisp(int off_x, int off_y, u_char alpha, void *pk2_addr,
                          int disp_label);                              /* 0x2458a0 */
void SaveLoadSnapShadowDisp(int off_x, int off_y, u_char alpha, void *pk2_addr,
                            int disp_label);                            /* 0x2459d8 */
void SaveLoadSnapShotDisp(int off_x, int off_y, u_char alpha, void *pk2_addr,
                          int disp_label);                              /* 0x245aa0 */
void SaveLoadNonSelNoDisp(int off_x, int off_y, u_char alpha, void *pk2_addr,
                          int disp_label);                              /* 0x245b68 */
void SaveLoadNonSelDataNumDisp(int off_x, int off_y, u_char alpha, void *pk2_addr,
                               int disp_label);                         /* 0x245c30 */
void SaveLoadNonSelLineDisp(int off_x, int off_y, u_char alpha, void *pk2_addr,
                            int disp_label);                            /* 0x245ce0 */
void SaveLoadSelNoDisp(int off_x, int off_y, u_char alpha, void *pk2_addr,
                       int disp_label);                                 /* 0x245da8 */
void SaveLoadSelDataNumDisp(int off_x, int off_y, u_char alpha, void *pk2_addr,
                            int disp_label);                            /* 0x245e70 */
void SaveLoadSelLineDisp(int off_x, int off_y, u_char alpha, void *pk2_addr,
                         int disp_label);                               /* 0x245f20 */
void SaveLoadClearFlareDisp(int off_x, int off_y, u_char alpha, void *pk2_addr,
                            int disp_label);                            /* 0x245fe8 */
void SaveLoadNonClearMaskDisp(int off_x, int off_y, u_char alpha, void *pk2_addr,
                              int disp_label);                          /* 0x2460e0 */

/* Slot summary lines. */
void SaveLoadMcPlayDataInfoDisp(int off_x, int off_y, u_char alpha, int chapter,
                                int room, TIME_INFO play_time);         /* 0x246318 */
void SaveLoadMcClearPlayDataInfoDisp(int off_x, int off_y, u_char alpha,
                                     TIME_INFO play_time);              /* 0x2464a0 */
void SaveLoadClearNumberDisp(int data, int off_x, int off_y, u_char alpha, int pri,
                             u_char zero_flg, int disp_label, void *pk2_addr); /* 0x246830 */
void SaveLoadClearNumberDisp_One(int data, int x, int y, u_char alpha, int pri,
                                 void *pk2_addr);                       /* 0x246980 */

/* Message windows and their text. */
void SaveLoadMcStateMsgWinDisp(int off_x, int off_y, u_char alpha);      /* 0x2465e0 */
void SaveLoadMcSelYesNoWinDisp(int off_x, int off_y, u_char alpha, int csr); /* 0x2466b0 */
void SaveLoadFileSelMsgWinDisp(int off_x, int off_y, u_char alpha);      /* 0x246710 */
void SaveLoadFileSelYesNoWinDisp(int off_x, int off_y, u_char alpha, int csr); /* 0x246760 */
void SaveLoadMcStateMsgDisp(int off_x, int off_y, u_char alpha, int msg_id);   /* 0x2467c0 */
void SaveLoadFileSelMsgDisp(int off_x, int off_y, u_char alpha, int msg_id);   /* 0x2467f8 */

#endif /* _SAVE_LOAD_PRG_SAVE_LOAD_DISP_H */
