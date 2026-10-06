// FILE: /home/zero_rom/zero2np/src/main/glob.c
//
// Definition site for the engine-wide shared globals.  This translation unit
// holds nothing but the storage for the top-level work blocks declared in
// common/variable.h; every other TU reaches them through that header's extern
// declarations.  All of them are zero-initialised (they land in .data but are
// filled in at runtime by the boot / per-frame code, e.g. InitSysWrk(),
// SetLanguage(), the pad reader), so they are plain tentative definitions here.
//
// Reconstructed from the Feb 6 2004 prototype (SLES_523.84).

#include "../common/variable.h"     // the shared types + matching extern declarations

// ──────────────────────────────────────────────────────────────────────
// Controller pad state (port 0 / port 1).

PAD_STRUCT      pad[2];             // data 316c80

// ──────────────────────────────────────────────────────────────────────
// Per-logical-button pointers into this / last frame's pad data.

u_short        *key_now[32];        // data 317000
u_short        *key_bak[32];        // data 317080

// ──────────────────────────────────────────────────────────────────────
// System / option / camera / debug work blocks.

SYS_WRK         sys_wrk;            // data 317100
OPTION_WRK      opt_wrk;            // data 317120
CAM_CUSTOM_WRK  cam_custom_wrk;     // data 317130
DEBUG_WRK       debug_wrk;          // data 317140

// Larger shared game work blocks whose owning modules are not reconstructed yet.
DEBUG_VAR       debug_var;          // data 2db2b0
PLYR_WRK        plyr_wrk;           // data 33cd90
SIS_WRK         sis_wrk;            // data 34fc00 (owner: sister.o)
GAME_COSTUME    GameCostume;        // bss 4bbce0

// ingame_wrk (data 3186b0), phase_change_reqs (sbss 3f4d18) and
// OutPhaseChangeFlg (sbss 3f4d20) used to be defined here.  ZERO2.MAP puts all
// three in ingame.o, and the latter two carry no global symbol at all -- they
// are file-statics of ingame.c -- so they moved to ingame/ingame.c.
