/* ==========================================================================
 *  system/eeiop/hxd.c
 *
 *  HXD sound-header helpers.
 *
 *  Reconstructed from the Feb 6 2004 prototype (SLES_523.84).
 * ======================================================================== */

#include <stdio.h>

#include "hxd.h"

#include "../../common/utility2.h"  /* PRINT_ASSERT */

/* The file's magic, stored as GCC evaluates the multi-character constant
 * 'HXD\0': ('H' << 24) | ('X' << 16) | ('D' << 8).  The ROM's .sdata word is
 * 0x48584400, which is why the bytes read "\0DXH" in a little-endian dump. */
static int hxd_file_id = ('H' << 24) | ('X' << 16) | ('D' << 8);             /* sdata 3f49b0 */

/* 21 */
void CheckHXDData(HXD_HEADER *header, int requested_file_type)
{
    if (header->name != hxd_file_id)                                         /* 27 */
        PRINT_ASSERT("This File Is Not HXD!!");                              /* 28 */

    if (header->type != requested_file_type)                                 /* 34 */
        PRINT_ASSERT("This File Is Not Requested Type!");                    /* 35 */
}

void PrintSOUND_INFO(SOUND_INFO info)
{
    if (info.attr.effect)                                                    /* 41 */
        printf("<EFFECT = ON>\n");                                           /* 42 */
    else
        printf("<EFFECT = OFF>\n");                                          /* 44 */

    printf("<TYPE   = %d>\n", info.attr.type);                               /* 45 */

    /* ROM BUG, reproduced: the 3D line tests `loop`, not `s3d` -- both
     * branches below load the same attr bit 5.  A debug dump only, so it
     * never mattered. */
    if (info.attr.loop)                                                      /* 46 */
        printf("<3D     = ON>\n");                                           /* 47 */
    else
        printf("<3D     = OFF>\n");                                          /* 49 */

    if (info.attr.loop)                                                      /* 50 */
        printf("<LOOP   = ON>\n");                                           /* 51 */
    else
        printf("<LOOP   = OFF>\n");                                          /* 53 */

    if (info.attr.male)                                                      /* 54 */
        printf("<MALE   = MALE>\n");                                         /* 55 */
    else
        printf("<MALE   = FEMALE>\n");                                       /* 57 */

    printf("\n");                                                            /* 58 */
    printf("<SAMPLING_RATE = %10d>\n", info.smpl_rate);                      /* 59 */
    printf("<OFFSET        = 0x%x>\n", info.offset);                         /* 60 */
    printf("<PITCH         = %10d>\n", info.pitch);                          /* 61 */
    printf("<VOL           = %10d>\n", info.vol);                            /* 62 */
    printf("<ENVELOPE1     = 0x%x>\n", info.adsr1);                          /* 63 */
    printf("<ENVELOPE2     = 0x%x>\n", info.adsr2);                          /* 64 */
    printf("<PAN           = %10d>\n", info.pan);                            /* 65 */
    printf("\n");                                                            /* 66 */
    printf("\n");                                                            /* 67 */
}

void PrintSOUND_INFOArray(SOUND_INFO *info, int num)
{
    while (num-- > 0)                                                        /* 73 */
        PrintSOUND_INFO(*info++);                                            /* 74 */
}                                                                            /* 76 */
