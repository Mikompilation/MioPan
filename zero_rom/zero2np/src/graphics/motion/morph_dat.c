// FILE: /home/zero_rom/zero2np/src/graphics/motion/morph_dat.c
//
// Morph code-table data: one script per animation, per morphing model.
//
// A MORPH_CODE is a 64-bit word; only the low 32 bits are used, laid out as
//
//     bits 31..28  kind    (only 0 and 1 are handled)
//     bits 27..24  arg0    (selects the meaning of the rest)
//     bits 23..16  arg1
//     bits 15.. 8  arg2
//     bits  7.. 0  arg3
//
// MorphSetNewCode() decodes it.  For kind 1 there are two forms:
//
//     arg0 == 0   target = arg1 (percent), hold = arg2, speed = arg3
//     arg0 == 1   hold   = arg1, target/speed untouched
//
// A hold of 0 means "stay here" -- cnt is forced to 1.0 and the countdown is
// switched off; anything else counts down by reso/200 per call.  A zero word
// terminates the script, and MorphDevCode() steps one word on whenever the
// hold reaches zero.
//
// Values extracted from the prototype's .data at 0x32eb80.
//
// Reconstructed from the Feb 6 2004 prototype (SLES_523.84).

#include "morph_dat.h"

/* the default script every unremarkable animation shares: fade straight out */
MORPH_CODE morph_down[2] = {
    0x10000003,                     /* target   0%, hold  0, speed  3 */
    0x00000000
};

MORPH_CODE ch017morph006[3] = {
    0x11230000,                     /* hold 35                        */
    0x10460006,                     /* target  70%, hold  0, speed  6 */
    0x00000000
};

MORPH_CODE ch017morph008[2] = {
    0x11000000,                     /* hold  0 -- stay put            */
    0x00000000
};

MORPH_CODE ch017morph016[3] = {
    0x11140000,                     /* hold 20                        */
    0x10460005,                     /* target  70%, hold  0, speed  5 */
    0x00000000
};

MORPH_CODE ch020morph006[3] = {
    0x110f0000,                     /* hold 15                        */
    0x1064000a,                     /* target 100%, hold  0, speed 10 */
    0x00000000
};

MORPH_CODE ch020morph008[6] = {
    0x10001e03,                     /* target   0%, hold 30, speed  3 */
    0x10640f07,                     /* target 100%, hold 15, speed  7 */
    0x10000f07,                     /* target   0%, hold 15, speed  7 */
    0x10640f07,                     /* target 100%, hold 15, speed  7 */
    0x11000000,                     /* hold  0 -- stay put            */
    0x00000000
};

MORPH_CODE ch030morph006[3] = {
    0x11640000,                     /* hold 100                       */
    0x10640007,                     /* target 100%, hold  0, speed  7 */
    0x00000000
};

MORPH_CODE ch030morph008[3] = {
    0x10001405,                     /* target   0%, hold 20, speed  5 */
    0x11000000,                     /* hold  0 -- stay put            */
    0x00000000
};

MORPH_CODE ch041morph000[2] = {
    0x10000005,                     /* target   0%, hold  0, speed  5 */
    0x00000000
};

MORPH_CODE ch041morph002[2] = {
    0x10000014,                     /* target   0%, hold  0, speed 20 */
    0x00000000
};

MORPH_CODE ch041morph004[2] = {
    0x10000014,
    0x00000000
};

MORPH_CODE ch041morph005[2] = {
    0x10000014,
    0x00000000
};

MORPH_CODE ch041morph006[3] = {
    0x11230000,                     /* hold 35                        */
    0x10640007,                     /* target 100%, hold  0, speed  7 */
    0x00000000
};

MORPH_CODE ch041morph007[2] = {
    0x1064000a,                     /* target 100%, hold  0, speed 10 */
    0x00000000
};

MORPH_CODE ch041morph008[2] = {
    0x1064000a,
    0x00000000
};

MORPH_CODE ch041morph009[2] = {
    0x1064000a,
    0x00000000
};

MORPH_CODE ch041morph010[2] = {
    0x1064000a,
    0x00000000
};

MORPH_CODE ch041morph011[2] = {
    0x1064000a,
    0x00000000
};

MORPH_CODE ch041morph012[2] = {
    0x1064000a,
    0x00000000
};

MORPH_CODE ch041morph013[3] = {
    0x111e0000,                     /* hold 30                        */
    0x10000003,                     /* target   0%, hold  0, speed  3 */
    0x00000000
};

MORPH_CODE ch041morph014[3] = {
    0x111e0000,                     /* hold 30                        */
    0x10640002,                     /* target 100%, hold  0, speed  2 */
    0x00000000
};

MORPH_CODE ch041morph016[3] = {
    0x111e0000,
    0x10640002,
    0x00000000
};

MORPH_CODE ch041morph017[3] = {
    0x111e0000,                     /* hold 30                        */
    0x10640003,                     /* target 100%, hold  0, speed  3 */
    0x00000000
};

MORPH_CODE ch041morph018[2] = {
    0x1064000a,
    0x00000000
};

/* --------------------------------------------------------------------------
 *  Per-model script tables, indexed by the animation's playnum.  Entry 22 is
 *  the NULL terminator; every animation with nothing special to do gets
 *  morph_down.
 * ------------------------------------------------------------------------ */
MORPH_CODE *ch017morph_tbl[23] = {
    morph_down,     morph_down,     morph_down,     morph_down,
    morph_down,     morph_down,     ch017morph006,  morph_down,
    ch017morph008,  morph_down,     morph_down,     morph_down,
    morph_down,     morph_down,     morph_down,     morph_down,
    ch017morph016,  morph_down,     morph_down,     morph_down,
    morph_down,     morph_down,     nullptr
};

MORPH_CODE *ch020morph_tbl[23] = {
    morph_down,     morph_down,     morph_down,     morph_down,
    morph_down,     morph_down,     ch020morph006,  morph_down,
    ch020morph008,  morph_down,     morph_down,     morph_down,
    morph_down,     morph_down,     morph_down,     morph_down,
    morph_down,     morph_down,     morph_down,     morph_down,
    morph_down,     morph_down,     nullptr
};

MORPH_CODE *ch030morph_tbl[23] = {
    morph_down,     morph_down,     morph_down,     morph_down,
    morph_down,     morph_down,     ch030morph006,  morph_down,
    ch030morph008,  morph_down,     morph_down,     morph_down,
    morph_down,     morph_down,     morph_down,     morph_down,
    morph_down,     morph_down,     morph_down,     morph_down,
    morph_down,     morph_down,     nullptr
};

MORPH_CODE *ch041morph_tbl[23] = {
    ch041morph000,  morph_down,     ch041morph002,  morph_down,
    ch041morph004,  ch041morph005,  ch041morph006,  ch041morph007,
    ch041morph008,  ch041morph009,  ch041morph010,  ch041morph011,
    ch041morph012,  ch041morph013,  ch041morph014,  morph_down,
    ch041morph016,  ch041morph017,  ch041morph018,  morph_down,
    morph_down,     morph_down,     nullptr
};
