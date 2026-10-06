// FILE: /home/zero_rom/zero2np/src/ingame/puzzle/puzzle_dat.c
//
// The puzzle confirmation message table.
//
// A data-only translation unit -- 0x30 bytes of .rodata and nothing else.
// Each row is the (msg_type, msg_id) pair handed to SetMsgWinDefData() /
// SetMsgDefData() / PrintMsg() for the "do you want to solve this?" prompt
// that PzlExeConfWinDisp() puts up before a puzzle starts.
//
// Only the two puzzles that ask (Hina and Roku, ids 0 and 1) ever reach the
// prompt -- PzlExeCtrlInit() starts ids 2..5 at step 2, past it -- so rows 2
// through 5 are the same filler value.
//
// Extracted from .rodata 0x3c4528 and verified byte-for-byte against the ROM.
//
// Reconstructed from the Feb 6 2004 prototype (SLES_523.84), puzzle_dat.o.

#include "puzzle_dat.h"

const int pzl_conf_msg[6][2] =                              /* rdata 3c4528 */
{
    /*             msg_type  msg_id */
    /* 0 hina  */ {      68,      5 },
    /* 1 roku  */ {      71,      8 },
    /* 2 kaza  */ {      69,      5 },
    /* 3 kaza2 */ {      69,      5 },
    /* 4 kai1  */ {      69,      5 },
    /* 5 kai2  */ {      69,      5 },
};
