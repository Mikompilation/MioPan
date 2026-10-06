// FILE: /home/zero_rom/zero2np/src/graphics/motion/mim_dat.c
//
// Player-facing MIME ids are translated through the two model-specific lookup
// tables stored in the original executable.  Other character models use their
// MIME id directly.
//
// Reconstructed from the Feb 6 2004 prototype (SLES_523.84).

#include "mim_dat.h"

/* Original .data images at 0x32d140 and 0x32d1e8. */
static int ch000_mim_no_tbl[41] = {
     0,  1,  2,  3,  4,  5, -1,  6,  7,  6,  7,  8,  8,  9,
     9, -1, -1, -1, -1, 10, 10, 11, 18, 12, 19, 17, -1, -1,
    -1, -1, 13, 20, 14, 21, 15, 22, 16, 23, -1, -1, -1
};

static int ch001_mim_no_tbl[41] = {
     0,  1,  2,  3, -1,  4,  5,  6,  7,  6,  7,  8,  8,  9,
     9, 10, 11, 12, 13, -1, -1, -1, -1, -1, -1, -1, 16, 14,
    17, 15, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1
};

int mimdatGetMimeNo(ANI_CTRL *ani_ctrl, int num)
{
    switch (ani_ctrl->mdl_no)
    {
    case 0:
    case 0x3e:
    case 0x40:
    case 0x42:
    case 0x44:
    case 0x46:
    case 0x48:
    case 0x4a:
    case 0x4c:
        if (num < 0x29)
        {
            return ch000_mim_no_tbl[num];
        }
        break;

    case 1:
    case 0x3f:
    case 0x41:
    case 0x43:
    case 0x45:
    case 0x47:
    case 0x49:
    case 0x4b:
    case 0x4d:
        if (num < 0x29)
        {
            return ch001_mim_no_tbl[num];
        }
        break;

    default:
        return num;
    }

    return -1;
}
