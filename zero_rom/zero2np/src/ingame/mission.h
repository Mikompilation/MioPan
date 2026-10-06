/* ==========================================================================
 *  ingame/mission.h
 *
 *  Mission-mode interface, kept as a convenience header for the ingame side.
 *
 *  There is no mission.o in ZERO2.MAP: everything that used to be declared
 *  and stubbed here belongs to outgame/mission_ctl.o, outgame/mission_disp.o
 *  and outgame/mission_pause.o.  This header now just forwards to those, and
 *  ingame/mission.c is empty.
 *
 *  Reconstructed from the Feb 6 2004 prototype (SLES_523.84).
 * ======================================================================== */

#ifndef _INGAME_MISSION_H
#define _INGAME_MISSION_H

#include "../outgame/mission_ctl.h"     /* MisProc / MisSetScore / MisSt* / MisEn* */
#include "../outgame/mission_disp.h"    /* MisDispSetFlg / MisDispDeleteFlg / ... */
#include "../outgame/mission_pause.h"   /* MisPauseInit / MisPauseMain / DispMain */

#endif /* _INGAME_MISSION_H */
