// FILE: /home/zero_rom/zero2np/src/ingame/event/prg/ev_debug.c
//
// Event-system debug console.  One function, and it owns no state of its own:
// the four dumps it triggers are defined next to the tables they print
// (ev_change.c, ev_open.c, ev_macro.c), so this file is purely the key
// polling and the position readout.
//
// The paddat[] subscripts are debug *action labels*, not buttons -- paddat
// points at paddat_m[opt_wrk.pad_type] in key_cnf.c, so the physical key
// moves with the controller config.  On the default type 0 the four labels
// resolve to TRIANGLE (1), CROSS (0), TRIANGLE (5) and CIRCLE (6); note that
// labels 1 and 5 alias onto the same key there, so the event-state and
// compulsion-queue dumps come out together.  That is the ROM's own table.
//
// Reconstructed from the Feb 6 2004 prototype (SLES_523.84).

#include "ev_debug.h"

#include <stdio.h>                                  // printf (this file is all TTY output)

#include "ev_change.h"                              // EvDbg_EventStatePrint / EvDbg_CompulsionSetPrint
#include "ev_macro.h"                               // EvDbg_EventGhostPrint
#include "ev_open.h"                                // EvDbgDispCenter
#include "../../map/MapLoad.h"                      // MapLoadGetOffset
#include "../../map/map_rectangle.h"                // DrawEventRect
#include "../../plyr/player.h"                      // GetPlyrAreaNo
#include "../../../common/variable.h"               // plyr_wrk / sis_wrk / pad
#include "../../../system/pad/pad.h"                // paddat

/* Port addition -- no ROM counterpart.  The prototype exports EvDbgMain() but
 * never calls it, so there is no original gate to be faithful to; this backs
 * the DEBUG MENU's "EVENT DEBUG" row and EventMain() polls it.
 *
 * Defaults to ON.  The debug menu only runs from one_Story_Debug(), so a
 * default of 0 would be unreachable in precisely the case the console is for:
 * a chapter that never finishes coming up.  Being on costs four pointer reads
 * a frame -- every dump needs a key press, and the DrawEventRect() call
 * early-outs unless HIT RECTANGLE is also on. */
int dbg_event_debug = 0;

void EvDbgMain(void)
{                                                                       /* 40 */
    float *room_off;

    /* Room origin for the area the player is standing in.  Positions below
     * are printed relative to it because that is what the map data uses. */
    room_off = MapLoadGetOffset(GetPlyrAreaNo());                       /* 46 */

    DrawEventRect(plyr_wrk.cmn_wrk.mbox.pos);                           /* 51 */

    if (*paddat[1] == 1) {                                              /* 53 */
        EvDbg_EventStatePrint();                                        /* 55 */
    }
    if (*paddat[0] == 1) {                                              /* 57 */
        EvDbgDispCenter();                                              /* 59 */
    }
    if (*paddat[5] == 1) {                                              /* 61 */
        EvDbg_CompulsionSetPrint();                                     /* 63 */
    }
    if (*paddat[6] == 1) {                                              /* 65 */
        EvDbg_EventGhostPrint();                                        /* 67 */
    }

    /* R1 (one = newly pressed this frame).
     *
     * The `room_off != NULL` half of these two tests is a port deviation.
     * MapLoadGetOffset() returns NULL when the area has no resident room --
     * which is the normal state on a chapter that is still loading, or one
     * that never comes up.  The EE read address 0 and printed junk; here it
     * is a segfault, and it would land on exactly the run you were trying to
     * diagnose.  Drop the guards only if the ROM's garbage output matters. */
    if ((pad[0].one & 8U) != 0 && room_off != NULL)                     /* 69 */
    {                   
        printf("*********************************\n");                  /* 71 */
        printf("*     Player Room Local Pos     *\n");                  /* 72 */
        printf("*********************************\n");                  /* 73 */
        printf("  Player Room Local Pos x = %f\n", plyr_wrk.cmn_wrk.mbox.pos[0] - room_off[0]);             /* 74 */
        printf("  Player Room Local Pos y = %f\n", plyr_wrk.cmn_wrk.mbox.pos[1] - room_off[1]);             /* 75 */
        printf("  Player Room Local Pos z = %f\n", plyr_wrk.cmn_wrk.mbox.pos[2] - room_off[2]);             /* 76 */
        printf("  Player Floor %d\n", plyr_wrk.cmn_wrk.floor);          /* 77 */
    }

    /* R2. */
    if ((pad[0].one & 2U) != 0 && room_off != NULL)                     /* 79 */
    {                   
        printf("*********************************\n");                  /* 81 */
        printf("*     Sister Room Local Pos     *\n");                  /* 82 */
        printf("*********************************\n");                  /* 83 */
        printf("  Sister Room Local Pos x = %f\n", sis_wrk.cmn_wrk.mbox.pos[0] - room_off[0]);              /* 84 */
        printf("  Sister Room Local Pos y = %f\n", sis_wrk.cmn_wrk.mbox.pos[1] - room_off[1]);              /* 85 */
        printf("  Sister Room Local Pos z = %f\n", sis_wrk.cmn_wrk.mbox.pos[2] - room_off[2]);              /* 86 */
        printf("  Sister Floor %d\n", sis_wrk.cmn_wrk.floor);           /* 87 */
    }
}
