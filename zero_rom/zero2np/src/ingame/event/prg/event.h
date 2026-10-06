/* ==========================================================================
 *  ingame/event/prg/event.h
 *
 *  Umbrella header for the event runtime.  There is no event.c in the ROM --
 *  the entry points ingame.c uses are spread across ev_main / ev_macro /
 *  ev_disp / ev_se / ev_sis / ev_ene, one translation unit each per
 *  ZERO2.MAP.  This header just pulls them together so ingame.c keeps a
 *  single include.
 *
 *  Reconstructed from the Feb 6 2004 prototype (SLES_523.84).
 * ======================================================================== */

#ifndef _INGAME_EVENT_PRG_EVENT_H
#define _INGAME_EVENT_PRG_EVENT_H

#include "ev_main.h"
#include "ev_macro.h"
#include "ev_disp.h"
#include "ev_se.h"
#include "ev_sis.h"
#include "ev_ene.h"

#endif /* _INGAME_EVENT_PRG_EVENT_H */
