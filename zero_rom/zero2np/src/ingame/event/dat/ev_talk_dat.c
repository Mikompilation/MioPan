// FILE: /home/zero_rom/zero2np/src/ingame/event/dat/ev_talk_dat.c
//
// Pure data file: the message type each of the 8 event talk tables prints
// through.  ev_talk.c's TalkDispMain() reads talk_info[tbl_id] and hands it to
// PrintMsgDef_W() as msg_type, which picks both the text bank the line is
// looked up in and -- via SetMsgWinDefData() -- the window style it is drawn
// in.  Every table is set to type 6, so in this prototype all event dialogue
// shares one bank and one window; the table exists so a table could be
// retargeted without touching ev_talk.c.
//
// The whole object file is this one array plus its binding constructor: the
// rest of ev_talk_dat.o's .text is fixed_array template boilerplate that the
// included headers emit, and there is no .data or .bss at all.  The ROM's
// array sits at line 231 and talk_info at line 232, so ~230 lines of the
// original precede them and generate nothing -- commentary or non-emitting
// declarations that cannot be recovered from the binary.
//
// Reconstructed from the Feb 6 2004 prototype (SLES_523.84).

#include "ev_talk_dat.h"

/* rodata 3ac330.  const because that is the section the ROM put it in; the
 * cast below is only needed because reference_fixed_array<int,8> stores a
 * plain int * -- GCC 2.96-ee took the conversion silently. */
static const int talk_msg_type[8] =                                 /* 231 */
{
    6,      /* table 0 */
    6,      /* table 1 */
    6,      /* table 2 */
    6,      /* table 3 */
    6,      /* table 4 */
    6,      /* table 5 */
    6,      /* table 6 */
    6       /* table 7 */
};

/* Bound at static-init time, which is the single .ctors entry (0x2c3b9c) this
 * object contributes: __static_initialization_and_destruction_0 stores
 * &talk_msg_type into talk_info.m_aData. */
reference_fixed_array<int, 8> talk_info(const_cast<int *>(talk_msg_type));  /* 232 */
