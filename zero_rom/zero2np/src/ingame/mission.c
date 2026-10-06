// FILE: /home/zero_rom/zero2np/src/ingame/mission.c
//
// There is no mission.o in ZERO2.MAP.  This file used to aggregate stubs for
// three real translation units, all of which now own their own bodies:
//
//   MisProc / MisSetScore / MisSetClearType / MisStInit / MisStTerm /
//   MisEnInit / MisEnTerm              -> outgame/mission_ctl.c   (reconstructed)
//   MisDispSetFlg / MisDispDeleteFlg /
//   MisDispTimeInit / MisDispTimeProc  -> outgame/mission_disp.c  (stub)
//   MisPauseInit / MisPauseMain /
//   MisPauseDispMain                   -> outgame/mission_pause.c (stub)
//
// mission.h is now a forwarding header over those three, so ingame code that
// included it keeps working.
//
// Reconstructed from the Feb 6 2004 prototype (SLES_523.84).

#include "mission.h"
