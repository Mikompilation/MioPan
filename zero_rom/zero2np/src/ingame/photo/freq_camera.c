// FILE: /home/zero_rom/zero2np/src/ingame/photo/freq_camera.c
//
// The finder camera's kick.
//
// Two independent one-shot animations played over whatever camera the finder
// has already set up.  Both are table walks rather than formulas: freqcam[]
// jolts the eye and the look-at point vertically, fovcam[] pulses the field of
// view, and each entry is held for a fixed number of frames before the cursor
// moves on.  A zero entry ends the run, which is why both tables are
// zero-terminated rather than counted.
//
// The two halves are armed together by ReqFreqCamera() but run apart -- each
// keeps its own state word and its own cursor -- so FreqCameraInit() can reset
// the vertical half on its own and leave a FOV pulse to finish and restore
// itself.  That matters because ReqFinderFadeIn() calls Init() every time the
// finder comes up.
//
// Reconstructed from the Feb 6 2004 prototype (SLES_523.84), freq_camera.o.
// Trailing /* NNN */ comments are the original source line numbers.

#include "freq_camera.h"

#include "../../graphics/graph3d/gra3d.h"        /* gra3dGetCamera */

/* --------------------------------------------------------------------------
 *  The two curves.
 *
 *  freqcam[] is a damped oscillation in world units, three frames an entry --
 *  a 39-frame recoil that overshoots hard on the first reversal (14 up, then
 *  48 down) and rings down from there.  fovcam[] is five frames an entry: the
 *  lens snaps 0.3 radians wide, comes back 0.15, and lands.
 *
 *  Verified against .data 0x3147e8 / 0x314820: every byte matches except the
 *  low byte of the five non-integral entries (3.4, -1.6, 0.1, -0.3, 0.15),
 *  which sit one ulp lower in the ROM -- EE GCC truncating float literals
 *  toward zero where the host rounds to nearest.
 * ------------------------------------------------------------------------ */
static float freqcam[14] =                                  /* data 3147e8 */
{
    14.0f, -48.0f, 42.0f, -34.0f, 27.0f, -21.0f, 15.0f, -10.0f, -6.0f,
    3.4f, -1.6f, -0.5f, 0.1f, 0.0f
};

static float fovcam[3] =                                    /* data 314820 */
{
    -0.3f, 0.15f, 0.0f
};

static FREQ_CAM freq_cam_wrk;                               /* bss 4af3c0 */

void FreqCameraInit(void)
{                                                                       /* 34 */
    freq_cam_wrk.frq_flow = 0;
}                                                                       /* 36 */

void ReqFreqCamera(void)
{                                                                       /* 41 */
    FREQ_CAM *fcp = &freq_cam_wrk;                                      /* 44 */

    /* Both halves have to be idle: a kick already in flight is not restarted,
     * and neither is it extended. */
    if (fcp->frq_flow == 0 && fcp->fov_flow == 0)                       /* 46 */
    {
        fcp->frq_flow = 1;                                              /* 47 */
        fcp->fov_flow = 1;                                              /* 48 */
        fcp->fov_bak  = gra3dGetCamera()->fFov;                         /* 49 */
    }
}

void FreqCamera(void)
{                                                                       /* 54 */
    FREQ_CAM *fcp = &freq_cam_wrk;                                      /* 60 */

    /* Vertical.  State 1 seeds the cursor and falls straight through into the
     * running state, so the first frame after the request already moves. */
    switch (fcp->frq_flow)                                              /* 62 */
    {
    case 1:
        fcp->frq_cnt  = 0;                                              /* 66 */
        fcp->frq_flow = 2;                                              /* 67 */
        /* fallthrough */

    /* Lines 68..88 produce no code -- a commented-out block in the ROM. */

    case 2:
        gra3dGetCamera()->matCoord[3][1] += freqcam[fcp->frq_cnt / 3];  /* 89 */
        gra3dGetCamera()->vTarget[1]     += freqcam[fcp->frq_cnt / 3];  /* 90 */

        /* The terminator is consumed rather than skipped: the last frame of a
         * kick applies a zero offset and only then clears the state, so
         * GetFreqCamera() reads 0 on the frame the run ends. */
        if (freqcam[fcp->frq_cnt / 3] != 0.0f)                          /* 92 */
        {
            fcp->frq_cnt++;                                             /* 93 */
        }
        else
        {
            fcp->frq_flow = 0;                                          /* 95 */
        }
        break;
    }

    /* Field of view.  Same shape, five frames an entry, and it puts the
     * captured FOV back rather than leaving the last table entry applied. */
    switch (fcp->fov_flow)                                              /* 99 */
    {
    case 1:
        fcp->fov_cnt  = 0;                                              /* 103 */
        fcp->fov_flow = 2;                                              /* 104 */
        /* fallthrough */

    case 2:
        gra3dGetCamera()->fFov = fcp->fov_bak + fovcam[fcp->fov_cnt / 5]; /* 106 */

        if (fovcam[fcp->fov_cnt / 5] != 0.0f)                           /* 108 */
        {
            fcp->fov_cnt++;                                             /* 109 */
        }
        else
        {
            gra3dGetCamera()->fFov = fcp->fov_bak;                      /* 111 */
            fcp->fov_flow = 0;                                          /* 112 */
        }
        break;
    }
}                                                                       /* 114 */

/* The offset the vertical half applied *this* frame.  FreqCamera() has already
 * moved the cursor on by the time anything reads this back, so the live entry
 * is the one before it -- hence the -1.  frq_cnt is only zero on the very
 * first frame of a run, which is the other branch. */
float GetFreqCamera(void)
{                                                                       /* 122 */
    FREQ_CAM *fcp = &freq_cam_wrk;

    if (fcp->frq_flow != 0)                                             /* 124 */
    {
        if (0 < fcp->frq_cnt)                                           /* 125 */
        {
            return freqcam[(fcp->frq_cnt - 1) / 3];
        }

        return freqcam[fcp->frq_cnt / 3];
    }

    return 0.0f;                                                        /* 127 */
}                                                                       /* 129 */
