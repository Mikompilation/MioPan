/* ==========================================================================
 *  ingame/item/dat/file_dat.c
 *
 *  Texture id per collected file/note.  Pure data file: file_dat.o contributes
 *  no code of its own -- its whole .text is the fixed_array template
 *  boilerplate (_fixed_array_assert plus _fixed_array_verifyrange<void*>,
 *  <char*> and <unsigned int*>) that the include chain drags in.
 *
 *  Read straight out of SLES_523.84 at data 312248, five plain int arrays
 *  totalling 0x280 bytes.  Every table is one unbroken ascending run, so the
 *  art was authored as a contiguous block per file type; the only breaks are
 *  between tables -- 3873 is skipped between oldbook and photograph, and map
 *  starts far away at 3953.
 *
 *  Reconstructed from the Feb 6 2004 prototype (SLES_523.84).
 * ======================================================================== */

#include "file_dat.h"

FILE_DAT file_pocketbook[FILE_POCKETBOOK_MAX] =                     /* data 312248 */
{
    3749, 3750, 3751, 3752, 3753, 3754, 3755,   /*  0 */
    3756, 3757, 3758, 3759, 3760, 3761, 3762,   /*  7 */
    3763, 3764, 3765, 3766, 3767, 3768, 3769,   /* 14 */
    3770, 3771, 3772, 3773, 3774, 3775, 3776,   /* 21 */
    3777, 3778, 3779, 3780, 3781, 3782, 3783,   /* 28 */
    3784, 3785, 3786, 3787, 3788, 3789, 3790,   /* 35 */
};

FILE_DAT file_scrap[FILE_SCRAP_MAX] =                               /* data 3122f0 */
{
    3791, 3792, 3793, 3794, 3795, 3796, 3797,   /*  0 */
    3798, 3799, 3800, 3801, 3802, 3803, 3804,   /*  7 */
    3805, 3806, 3807, 3808, 3809, 3810, 3811,   /* 14 */
    3812, 3813, 3814, 3815, 3816, 3817, 3818,   /* 21 */
    3819, 3820, 3821, 3822, 3823, 3824, 3825,   /* 28 */
    3826, 3827, 3828, 3829, 3830, 3831, 3832,   /* 35 */
};

FILE_DAT file_oldbook[FILE_OLDBOOK_MAX] =                           /* data 312398 */
{
    3833, 3834, 3835, 3836, 3837, 3838, 3839,   /*  0 */
    3840, 3841, 3842, 3843, 3844, 3845, 3846,   /*  7 */
    3847, 3848, 3849, 3850, 3851, 3852, 3853,   /* 14 */
    3854, 3855, 3856, 3857, 3858, 3859, 3860,   /* 21 */
    3861, 3862, 3863, 3864, 3865, 3866, 3867,   /* 28 */
    3868, 3869, 3870, 3871, 3872,               /* 35 */
};

FILE_DAT file_photograph[FILE_PHOTOGRAPH_MAX] =                     /* data 312438 */
{
    3874, 3875, 3876, 3877, 3878, 3879, 3880,   /*  0 */
    3881, 3882, 3883, 3884, 3885, 3886, 3887,   /*  7 */
    3888, 3889, 3890, 3891, 3892, 3893, 3894,   /* 14 */
    3895, 3896, 3897, 3898, 3899,               /* 21 */
};

FILE_DAT file_map[FILE_MAP_MAX] =                                   /* data 3124a0 */
{
    3953, 3954, 3955, 3956, 3957, 3958, 3959,   /*  0 */
    3960, 3961, 3962,                           /*  7 */
};
