// FILE: /home/zero_rom/zero2np/src/common/FileSt.c
//
// File-label classification.  The object is a single predicate plus the table
// it reads; FileSt.o is 0x34 bytes of .text and 0x10 of .data, and nothing
// else in the ROM links against it beyond MapLBuff.c.
//
// Reconstructed from the Feb 6 2004 prototype (SLES_523.84), FileSt.o
// 0x00101a38..0x00101a6b.

#include "FileSt.h"

/* Two [lo, hi] label-ID ranges, read out of .data at 0x2c3d00.  Only row 0 is
 * ever consulted -- FileStGetType() is the object's only function and it does
 * not take a row index -- so row 1 is carried purely to keep the initialiser
 * faithful to the ROM. */
static int FileStLabelList[2][2] =                                      /* data 2c3d00 */
{
    {  555,  886 },
    { 3087, 3380 }
};

int FileStGetType(int label_id)
{
    /* Both bounds are exclusive: a label sitting exactly on 555 or 886 is
     * classified as type 1, not 0. */
    if ((label_id > FileStLabelList[0][0]) &&
        (label_id < FileStLabelList[0][1]))                             /* 31 */
    {
        return 0;
    }

    return 1;                                                           /* 35 */
}                                                                       /* 36 */
