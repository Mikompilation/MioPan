// FILE: /home/zero_rom/zero2np/src/ingame/menu/anim_2d.c
//
// Shared 2D animation-table evaluators.  Each Anim2D_CalcNow* scans up to 100
// entries of its *_ANIM_TBL for the one whose [start_time,end_time) contains
// `timer` (a start_time == -1 entry is a hole/terminator, skipped), then
// interpolates the start/end value across that span.  Anim2D_CalcPosAnim and
// Anim2D_CalcAlphaAnim are the interpolation kernels shared by callers that
// already know which segment they are in.
//
// Reconstructed from the Feb 6 2004 prototype (SLES_523.84).

#include "anim_2d.h"

#include "../../common/utility2.h"  // PRINT_WARNING / PRINT_ASSERT

// ──────────────────────────────────────────────────────────────────────
// Position: search tbl for the entry owning `timer`, then hand off to
// Anim2D_CalcPosAnim for the actual curve.

float Anim2D_CalcNowPos(const POS_ANIM_TBL *tbl, int timer)
{
    int i;
    int tbl_pos;
    int change_time;
    float pos;

    tbl_pos = -1;
    for (i = 0; i < 100; i++)
    {
        if (tbl[i].start_time == -1)
        {
            continue;
        }
        if ((timer < tbl[i].start_time) || (tbl[i].end_time <= timer))
        {
            continue;
        }
        tbl_pos = i;
        break;
    }

    if (tbl_pos == -1)
    {
        return 0.0f;
    }

    change_time = tbl[tbl_pos].end_time - tbl[tbl_pos].start_time;
    if (change_time == 0)
    {
        PRINT_WARNING("POS TBL Warning!! %s", __FUNCTION__);                  /* 68 */
    }

    pos = Anim2D_CalcPosAnim(tbl[tbl_pos].start_pos, tbl[tbl_pos].end_pos,
                             tbl[tbl_pos].anim_label, change_time,
                             timer - tbl[tbl_pos].start_time);                 /* 72 */
    return pos;
}

// ──────────────────────────────────────────────────────────────────────
// Scale: same search, linear interpolation inline (no shared curve kernel).

float Anim2D_CalcNowScale(const SCL_ANIM_TBL *tbl, int timer)
{
    int i;
    int tbl_pos;
    float change_scl;
    float change_time;
    float scale;

    tbl_pos = -1;
    for (i = 0; i < 100; i++)
    {
        if (tbl[i].start_time == -1)
        {
            continue;
        }
        if ((timer < tbl[i].start_time) || (tbl[i].end_time <= timer))
        {
            continue;
        }
        tbl_pos = i;
        break;
    }

    if (tbl_pos == -1)
    {
        return 1.0f;
    }

    change_time = (float)(tbl[tbl_pos].end_time - tbl[tbl_pos].start_time);
    change_scl = tbl[tbl_pos].end_scl - tbl[tbl_pos].start_scl;
    if (change_time == 0.0f)
    {
        PRINT_WARNING("SCALE TBL Warning!! %s", __FUNCTION__);                /* 117 */
    }

    scale = tbl[tbl_pos].start_scl +
            (change_scl * (float)(timer - tbl[tbl_pos].start_time)) / change_time; /* 120 */
    return scale;
}

// ──────────────────────────────────────────────────────────────────────
// Alpha: same search, hands off to Anim2D_CalcAlphaAnim for the curve.

u_char Anim2D_CalcNowAlpha(const ALPHA_ANIM_TBL *tbl, int timer)
{
    int i;
    int tbl_pos;
    int change_time;
    u_char alpha;

    tbl_pos = -1;
    for (i = 0; i < 100; i++)
    {
        if (tbl[i].start_time == -1)
        {
            continue;
        }
        if ((timer < tbl[i].start_time) || (tbl[i].end_time <= timer))
        {
            continue;
        }
        tbl_pos = i;
        break;
    }

    if (tbl_pos == -1)
    {
        return 0x80;
    }

    change_time = tbl[tbl_pos].end_time - tbl[tbl_pos].start_time;
    if (change_time == 0)
    {
        PRINT_WARNING("ALPHA TBL Warning!! %s", __FUNCTION__);                /* 163 */
    }

    alpha = Anim2D_CalcAlphaAnim((u_char)tbl[tbl_pos].start_alpha,
                                 (u_char)tbl[tbl_pos].end_alpha, change_time,
                                 timer - tbl[tbl_pos].start_time);             /* 167 */
    return alpha;
}

// ──────────────────────────────────────────────────────────────────────
// RGB: same search, interpolation inline; asserts via trap(7) rather than
// delegating, unlike the alpha twin above -- matches the ROM exactly.

u_char Anim2D_CalcNowRGB(const RGB_ANIM_TBL *tbl, int timer)
{
    int i;
    int tbl_pos;
    short int change_rgb;
    short int change_time;
    u_char rgb;

    tbl_pos = -1;
    for (i = 0; i < 100; i++)
    {
        if (tbl[i].start_time == -1)
        {
            continue;
        }
        if ((timer < tbl[i].start_time) || (tbl[i].end_time <= timer))
        {
            continue;
        }
        tbl_pos = i;
        break;
    }

    if (tbl_pos == -1)
    {
        return 0x80;
    }

    change_time = tbl[tbl_pos].end_time - tbl[tbl_pos].start_time;
    change_rgb = tbl[tbl_pos].end_rgb - tbl[tbl_pos].start_rgb;
    if (change_time == 0)
    {
        PRINT_WARNING("RGB TBL Warning!! %s", __FUNCTION__);                  /* 213 */
        trap(7);                                                              /* 216 */
    }

    rgb = (u_char)(tbl[tbl_pos].start_rgb +
                   (change_rgb * (timer - tbl[tbl_pos].start_time)) / change_time); /* 219 */
    return rgb;
}

// ──────────────────────────────────────────────────────────────────────
// Rotation: same search (PRINT_ASSERT, not WARNING, on the zero-span path).

float Anim2D_CalcNowRot(const ROT_ANIM_TBL *tbl, int timer)
{
    int i;
    int tbl_pos;
    float change_rot;
    short int change_time;
    float rot;

    tbl_pos = -1;
    for (i = 0; i < 100; i++)
    {
        if (tbl[i].start_time == -1)
        {
            continue;
        }
        if ((timer < tbl[i].start_time) || (tbl[i].end_time <= timer))
        {
            continue;
        }
        tbl_pos = i;
        break;
    }

    if (tbl_pos == -1)
    {
        return 0.0f;
    }

    change_time = tbl[tbl_pos].end_time - tbl[tbl_pos].start_time;
    change_rot = tbl[tbl_pos].end_rot - tbl[tbl_pos].start_rot;
    if (change_time == 0)
    {
        PRINT_ASSERT("ROT TBL Error!! %s", __FUNCTION__);                     /* 262 */
    }

    rot = tbl[tbl_pos].start_rot +
          (change_rot * (float)(timer - tbl[tbl_pos].start_time)) / (float)change_time; /* 265 */
    return rot;
}

// ──────────────────────────────────────────────────────────────────────
// Position curve kernel.  calc_label 0/1 are the same linear ease (1
// pre-squares both anim_time and timer before the divide); 2 is ease-out.

float Anim2D_CalcPosAnim(float start_pos, float end_pos, int calc_label,
                         int anim_time, int timer)
{
    float pos;
    float distance;

    distance = end_pos - start_pos;
    pos = 0.0f;

    if (calc_label == 1)
    {
        anim_time = anim_time * anim_time;
        timer = timer * timer;
        pos = start_pos + (distance / (float)anim_time) * (float)timer;       /* 296 */
    }
    else if (calc_label == 0)
    {
        pos = start_pos + (distance / (float)anim_time) * (float)timer;       /* 296 */
    }
    else if (calc_label == 2)
    {
        return end_pos - (distance / (float)(anim_time * anim_time)) *
                          (float)((timer - anim_time) * (timer - anim_time));  /* 302 */
    }
    else
    {
        PRINT_ASSERT("Error! %s", __FUNCTION__);                              /* 305 */
    }

    return pos;
}

// ──────────────────────────────────────────────────────────────────────
// Alpha curve kernel.  anim_time == 0 returns end_alpha untouched.

u_char Anim2D_CalcAlphaAnim(u_char start_alpha, u_char end_alpha, int anim_time,
                            int timer)
{
    int change_alpha;

    if (anim_time != 0)
    {
        change_alpha = end_alpha - start_alpha;
        end_alpha = (u_char)(start_alpha + (change_alpha * timer) / anim_time); /* 331 */
    }

    return end_alpha;
}
