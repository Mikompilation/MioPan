/* ==========================================================================
 *  ingame/puzzle/kaza2/kaza2_pzl.h
 *
 *  The second kazaguruma (pinwheel) board (kaza2_pzl.o).  Same machine as
 *  kaza_pzl.o with a different panel layout and one extra mode, and it shares
 *  that module's types and puzzle_kaza_dat.o's sprite table.
 *
 *  Reconstructed from the Feb 6 2004 prototype (SLES_523.84).
 * ======================================================================== */

#ifndef _INGAME_PUZZLE_KAZA2_KAZA2_PZL_H
#define _INGAME_PUZZLE_KAZA2_KAZA2_PZL_H

#include "eetypes.h"

#include "../kaza/kaza_pzl.h"                   /* KAZA_PZL_CTRL / _DISP */

void KazaPuzzle2ExeInit(void);
int  KazaPuzzle2Main(void);
void KazaPuzzle2DispMain(void);
void KazaPuzzle2CrossScreenDisp(int off_x, int off_y, u_char alpha);

#endif /* _INGAME_PUZZLE_KAZA2_KAZA2_PZL_H */
