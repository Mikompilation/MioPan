// FILE: /home/zero_rom/zero2np/src/ingame/menu/zero2_anim2d.c
//
// The five canned 2D animation curves every menu screen shares.
//
// Each one owns its ALPHA_ANIM_TBL / RGB_ANIM_TBL, hands it to anim_2d.c to
// interpolate, and advances the caller's counter in place -- so a screen
// stores a counter (and a step, for the in/out pair) and nothing else.
//
// The two fade helpers build their table on the stack from a .rodata blob and
// then patch end_time from the argument, which is why the object has no named
// statics for them: a run of ld/sd from .rodata into consecutive stack slots
// at the head of a scope is a local aggregate initialiser, not a table.
// InOutAnimCtrl does the same thing twice, once per direction.
//
// Reconstructed from the Feb 6 2004 prototype (SLES_523.84), zero2_anim2d.o.
//
// Trailing /* NNN */ comments are the original source line numbers, measured
// from the $LM stabs.

#include "zero2_anim2d.h"

#include "anim_2d.h"                                /* Anim2D_CalcNowAlpha    */
#include "../../common/utility2.h"                  /* PRINT_ASSERT / WARNING */

/* ==========================================================================
 *  Two-phase open/close fade
 * ======================================================================== */

/* The workhorse.  Both phases interpolate 0 <-> 128 over a caller-supplied
 * frame count, and the step advances itself at the end of each ramp:
 * 0 -> 1 (seed), 1 -> 2 (open), 3 -> 4 (closed).  Getting from 2 to 3 is the
 * caller's job -- that is the "close this screen" request.
 *
 * The two clamps at 73..83 are pure defence: a caller that reduces
 * in_anim_time / out_anim_time between frames can leave the timer past the
 * end of its ramp, and Anim2D_CalcNowAlpha() would then walk off the table.
 * Each warns with its own message and pins the timer to the last frame. */
u_char Zero2Anim2D_InOutAnimCtrl(char *anim_step, char *anim_timer,
                                 short in_anim_time, short out_anim_time)    /* 40 */
{
    u_char alpha;

    ALPHA_ANIM_TBL in_alpha_tbl[2] =                        /* rdata 3e6db8 */
    {                                                                   /* 44 */
        {   0, 128, 0, 0 },
        {  -1,  -1, -1, -1 },
    };

    ALPHA_ANIM_TBL out_alpha_tbl[2] =                       /* rdata 3e6dc8 */
    {                                                                   /* 50 */
        { 128,   0, 0, 0 },
        {  -1,  -1, -1, -1 },
    };

    alpha = 0;                                                          /* 56 */

    in_alpha_tbl[0].end_time  = in_anim_time;                           /* 58 */
    out_alpha_tbl[0].end_time = out_anim_time;                          /* 59 */

    if (*anim_step == ZERO2_ANIM2D_STEP_START) {                        /* 62 */
        *anim_timer = 0;                                                /* 63 */
        *anim_step  = ZERO2_ANIM2D_STEP_IN;                             /* 64 */
    }

    if (*anim_timer < 0) {                                              /* 68 */
        PRINT_WARNING("Warning!! %s", __FUNCTION__);                    /* 69 */
        *anim_timer = 0;                                                /* 70 */
    }

    /* Clamp a timer that has outrun its ramp.  Steps 2 and 4 are held
     * states and cannot, so only IN and OUT are checked. */
    switch (*anim_step) {                                               /* 73 */
    case ZERO2_ANIM2D_STEP_IN:
        if (*anim_timer >= in_anim_time) {                              /* 75 */
            PRINT_WARNING("IN ANIM Warning!! %s", __FUNCTION__);        /* 76 */
            *anim_timer = (char)in_anim_time;                           /* 79 */
        }
        break;

    case ZERO2_ANIM2D_STEP_OUT:
        if (*anim_timer >= out_anim_time) {                             /* 81 */
            PRINT_WARNING("OUT ANIM Warning!! %s", __FUNCTION__);       /* 82 */
            *anim_timer = (char)out_anim_time;                          /* 83 */
        }
        break;
    }

    switch (*anim_step) {                                               /* 90 */
    case ZERO2_ANIM2D_STEP_IN:
        alpha = Anim2D_CalcNowAlpha(in_alpha_tbl, (int)*anim_timer);    /* 93 */

        (*anim_timer)++;                                                /* 95 */
        if (*anim_timer >= in_anim_time) {                              /* 96 */
            *anim_step = ZERO2_ANIM2D_STEP_SHOW;                        /* 99 */
        }
        break;

    case ZERO2_ANIM2D_STEP_SHOW:
        alpha = 0x80;                                                   /* 102 */
        break;

    case ZERO2_ANIM2D_STEP_OUT:
        alpha = Anim2D_CalcNowAlpha(out_alpha_tbl, (int)*anim_timer);   /* 105 */

        (*anim_timer)++;                                                /* 107 */
        if (*anim_timer >= out_anim_time) {                             /* 109 */
            *anim_step = ZERO2_ANIM2D_STEP_END;                         /* 110 */
        }
        break;

    case ZERO2_ANIM2D_STEP_END:
        alpha = 0;                                                      /* 115 */
        break;

    default:
        PRINT_ASSERT("Error! %s", __FUNCTION__);                        /* 117 */
        break;
    }

    return alpha;                                                       /* 121 */
}

/* ==========================================================================
 *  Cursor and selection pulses
 * ======================================================================== */

/* 45-frame cursor pulse: 64 -> 128 over 15, held 15, 128 -> 64 over 15. */
void Zero2Anim2D_CsrAnimCtrl(char *timer, u_char *rgb)                  /* 129 */
{
    static const RGB_ANIM_TBL rgb_tbl[4] =                  /* rdata 3e6e98 */
    {
        {  64, 128,  0, 15 },
        { 128, 128, 15, 30 },
        { 128,  64, 30, 45 },
        {  -1,  -1, -1, -1 },
    };

    *rgb = Anim2D_CalcNowRGB((RGB_ANIM_TBL *)rgb_tbl, (int)*timer);     /* 139 */

    (*timer)++;                                                         /* 141 */
    if (*timer >= 45) {                                                 /* 144 */
        *timer = 0;                                                     /* 145 */
    }
}

/* 30-frame selected-item pulse: 128 -> 64 over 15 and back. */
u_char Zero2Anim2D_SelAnimCtrl(char *timer)                             /* 155 */
{
    u_char alpha;

    static const ALPHA_ANIM_TBL alpha_tbl[3] =              /* rdata 3e6eb8 */
    {
        { 128,  64,  0, 15 },
        {  64, 128, 15, 30 },
        {  -1,  -1, -1, -1 },
    };

    alpha = Anim2D_CalcNowAlpha((ALPHA_ANIM_TBL *)alpha_tbl, (int)*timer); /* 166 */

    (*timer)++;                                                         /* 168 */
    if (*timer >= 30) {                                                 /* 171 */
        *timer = 0;                                                     /* 172 */
    }

    return alpha;                                                       /* 175 */
}

/* ==========================================================================
 *  Screen-black ramps
 * ======================================================================== */

/* Black closing over the scene: alpha 0 -> 128 across fade_in_time frames.
 * The timer is post-incremented without a ceiling, so a caller that keeps
 * calling past the end simply stays at 128 -- and *timer is pinned to
 * fade_in_time first, which is what stops it running away. */
u_char Zero2Anim2D_FadeInAnimCtrl(int *timer, short fade_in_time)       /* 185 */
{
    u_char alpha;

    ALPHA_ANIM_TBL fade_in_tbl[2] =                         /* rdata 3e6ed0 */
    {                                                                   /* 186 */
        {   0, 128, 0, 0 },
        {  -1,  -1, -1, -1 },
    };

    /* No $LM of its own -- the only instruction it generates is sunk into a
     * branch delay slot -- so its line is somewhere in 187..196. */
    fade_in_tbl[0].end_time = fade_in_time;

    if (*timer >= (int)fade_in_time) {                                  /* 197 */
        *timer = (int)fade_in_time;                                     /* 198 */
        alpha  = 0x80;                                                  /* 199 */
    } else {
        alpha = Anim2D_CalcNowAlpha(fade_in_tbl, *timer);               /* 202 */
    }

    (*timer)++;                                                         /* 205 */

    return alpha;
}

/* Black clearing off the scene: alpha 128 -> 0 across fade_out_time frames.
 *
 * ROM BUG, reproduced: the past-the-end arm returns 0x80, not 0 -- so a
 * fade-out that is polled one frame too long snaps back to fully black
 * instead of staying clear.  It is the fade-in body with only the table
 * swapped.  Every caller in the build gates on the timer and stops calling
 * on the frame the ramp completes, so it never shows; savepoint_main.c's
 * step 2 -> 3 transition is the clearest example, since SavePointMain() runs
 * before after_SavePoint_Main() in the same frame. */
u_char Zero2Anim2D_FadeOutAnimCtrl(int *timer, short fade_out_time)     /* 217 */
{
    u_char alpha;

    ALPHA_ANIM_TBL fade_out_tbl[2] =                        /* rdata 3e6ee0 */
    {                                                                   /* 218 */
        { 128,   0, 0, 0 },
        {  -1,  -1, -1, -1 },
    };

    fade_out_tbl[0].end_time = fade_out_time;   /* no $LM; see FadeIn above */

    if (*timer >= (int)fade_out_time) {                                 /* 229 */
        *timer = (int)fade_out_time;                                    /* 230 */
        alpha  = 0x80;                                                  /* 231 */
    } else {
        alpha = Anim2D_CalcNowAlpha(fade_out_tbl, *timer);              /* 234 */
    }

    (*timer)++;                                                         /* 237 */

    return alpha;
}
