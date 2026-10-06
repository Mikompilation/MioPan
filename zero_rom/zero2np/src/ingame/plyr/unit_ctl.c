// FILE: /home/zero_rom/zero2np/src/ingame/plyr/unit_ctl.c
//
// unit_ctl.o -- the shared "unit" (player / sister / enemy) geometry helpers.
// Nine exported functions, no statics and no data of its own: the object's
// .rodata (0x61) and .sdata (0x3e) hold nothing but the fixed_array<%s,%d>
// assert literal and the "void*" / "char*" / "unsigned int*" / "7ENE_WRK"
// type names, and .lit4 (0x34) is thirteen words of PI-derived constants plus
// (float)RAND_MAX.  The whole file is leaf math over MOVE_BOX positions --
// which is why almost every ingame module includes its header.
//
// The .lit4 order is the source order and is worth keeping in mind when
// reading the disassembly: [0..3] RotLimitChk (PI, PI*2, -PI, PI*2),
// [4] GetRndSP, [5..12] ConvertRot2Dir's three quantiser blocks.  GCC 2.96-ee
// does not dedupe within a file, so PI has four separate slots -- one per
// expansion, not four different values.  See the reversing skill's note on
// repeated lit4 constants.
//
// Reconstructed from the Feb 6 2004 prototype (SLES_523.84), .text
// 0x0026b448..0x0026bc08.

#include "unit_ctl.h"

#include <math.h>

#include "../../common/utility.h"                 // GetDistV / _ClearVector
#include "../../common/variable.h"                // plyr_wrk
#include "../../graphics/graph3d/ctl/fixed_array.h"
#include "../../graphics/graph3d/g3dMath.h"       // g3dAtan2f
#include "../enemy/enemy.h"                       // ene_wrk / ENE_WRK

#include <libvu0.h>

/* The ROM's PI, one ulp below the true float value -- GCC 2.96-ee truncated
 * float literals rather than rounding, so 3.1415926 became 0x40490fda.  Every
 * other constant in the file is an exact binary scaling of it, so they all
 * come out of the same word: PI*2 0x40c90fda, -PI 0xc0490fda, PI/2 0x3fc90fda,
 * PI/4 0x3f490fda, PI/8 0x3ec90fda. */
#define UNIT_CTL_PI    3.1415925f

/* Wrap rot into (-PI, PI].  The ROM loads *rot once and reuses it across both
 * arms, which is only valid because the first arm returns -- hence the
 * else-if rather than two independent tests. */
void RotLimitChk(float *rot)                                            /* 11 */
{
    if (*rot > UNIT_CTL_PI)       { *rot = *rot - UNIT_CTL_PI * 2.0f; } /* 12 */
    else if (*rot < -UNIT_CTL_PI) { *rot = *rot + UNIT_CTL_PI * 2.0f; } /* 13 */
}

/* Rotation from the player towards p.  -600 is the eye offset: Y grows
 * downwards here, so subtracting it and negating turns the drop from the eye
 * to p into a rise, which is what the pitch wants.
 *
 * Unlike GetTrgtRot() below, neither angle is wrapped -- the callers feed the
 * result straight into a look-at, not into an interpolated camera rotation. */
void GetTrgtRotFromPlyr(float *p, float *rot, int id)                   /* 26 */
{
    float dist[4];

    _ClearVector(rot);                                                  /* 31 */

    sceVu0SubVector(dist, p, plyr_wrk.cmn_wrk.mbox.pos);                /* 33 */
    dist[1] = -(dist[1] - -600.0f);                                     /* 34 */

    if ((id & 1) != 0) {                                                /* 37 */
        /* The pitch is taken against the floor-plane run, not the 3D
         * separation: GetDistV() is the XZ distance. */
        dist[3] = GetDistV(p, plyr_wrk.cmn_wrk.mbox.pos);               /* 38 */
        rot[0]  = g3dAtan2f(dist[1], dist[3]);                          /* 39 */
    }

    if ((id & 2) != 0) {                                                /* 42 */
        rot[1] = g3dAtan2f(dist[0], dist[2]);                           /* 43 */
    }
}

/* The general form of GetTrgtRotFromPlyr(): the rotation that turns p0
 * towards p1, with the same id bit mask (1 = pitch into rot[0], 2 = yaw into
 * rot[1]).
 *
 * Two differences from GetTrgtRotFromPlyr() are real, not transcription slips:
 * there is no eye-height bias on Y -- p0 is already the viewpoint, so the
 * negation moves onto the atan2 argument instead -- and each angle is folded
 * back into (-PI, PI] by RotLimitChk().  The callers here feed the result into
 * a camera rotation that is interpolated towards, so an unwrapped angle would
 * send it the long way round. */
void GetTrgtRot(const float *p0, const float *p1, float *rot, int id)   /* 59 */
{
    float dist[4];

    _ClearVector(rot);                                                  /* 62 */

    /* The EE SDK prototype is non-const, so the const-correct signature the
     * ROM mangles (float const *, float const *) has to be cast back off. */
    sceVu0SubVector(dist, (float *)p1, (float *)p0);                    /* 66 */

    if ((id & 1) != 0) {                                                /* 69 */
        dist[3] = GetDistV(p0, p1);                                     /* 70 */
        rot[0]  = g3dAtan2f(-dist[1], dist[3]);                         /* 71 */
        RotLimitChk(&rot[0]);                                           /* 72 */
    }

    if ((id & 2) != 0) {                                                /* 75 */
        rot[1] = g3dAtan2f(dist[0], dist[2]);                           /* 76 */
        RotLimitChk(&rot[1]);                                           /* 77 */
    }
}

/* Y rotation that turns p0 towards p1. */
float GetTrgtRotY(const float *p0, const float *p1)                     /* 88 */
{
    return g3dAtan2f(p1[0] - p0[0], p1[2] - p0[2]);                     /* 89 */
}

/* Non-zero when tp lies within +/- rng radians of the heading rot at vp.
 *
 * `rot` is the ROM's own scratch: it is a parameter, it lives in a stack slot
 * (functions.txt puts it at 0x0(sp) rather than in a register) because
 * RotLimitChk() takes its address, and it is overwritten twice. */
int RotRngChk(float *vp, float *tp, float rot, float rng)               /* 96 */
{
    rot = GetTrgtRotY(vp, tp) - rot;                                    /* 97 */
    RotLimitChk(&rot);                                                  /* 98 */
    rot = fabsf(rot);                                                   /* 99 */

    return (rot <= rng);                                                /* 100 */
}

/* Non-zero when tp is OUTSIDE the viewer's cone: further than dist (0 = no
 * distance limit) or outside the full-width sight angle centred on rot.
 *
 * Lines 122..124 hold no code -- comment, or a disabled older test. */
u_char OutSightChk(float *tp, float *vp, float rot, float sight, float dist)
{                                                                       /* 116 */
    u_char chk = 1;                                                     /* 117 */

    if ((dist != 0.0f) && (dist < GetDistV(tp, vp))) { return chk; }    /* 119 */

    /* sight is the full cone width, so the half-angle is what the rotation
     * range test wants. */
    chk = (u_char)(RotRngChk(vp, tp, rot, sight * 0.5f) == 0);          /* 121 */

    return chk;                                                         /* 125 */
}

/* Uniform random integer in [min, min + lng).  A zero span returns min
 * untouched rather than dividing by it.
 *
 * The ROM divides by lit4 0x3ee9b4 == 2147483520.0f, GCC 2.96-ee's truncation
 * of (float)RAND_MAX for the EE's RAND_MAX of 0x7fffffff; MIOPAN_RAND_MAXF is
 * that same constant and MioPan_Rand() supplies the 31-bit numerator.
 *
 * PORT DEVIATION: `result` is `long` in the ROM and EE `long` is 64 bits --
 * the product needs every one of them (0xffffffff * 0x7fffffff).  Host `long`
 * is 32 bits under MinGW and MSVC, so it is spelled int64_t here; the ROM's
 * own __muldi3 / __floatdisf / __fixsfdi calls are that 64-bit arithmetic. */
int GetRndSP(u_int min, u_int lng)                                      /* 137 */
{
    int64_t result;

    if (lng != 0) {                                                     /* 140 */
        result = MioPan_Rand();                                         /* 141 */

        return min + (int64_t)((float)(lng * result) / MIOPAN_RAND_MAXF);
    }                                                                   /* 143 */

    return min;                                                         /* 146 */
}                                                                       /* 148 */

/* Quantise a heading to a direction index, measured from straight ahead and
 * running clockwise.  `id` picks the granularity:
 *
 *   0   four ways   0 front, 1 right, 2 back, 3 left
 *   1   eight ways  0 front, 1..3 right, 4 back, 5..7 left
 *   2   two ways    0 front, 1 back
 *   any other       0 when rot >= 0, 1 when rot < 0 -- a bare left/right
 *
 * Each block is the same expression with the sector size changed: shift the
 * heading up by PI so it is never negative, add the half-sector so a sector is
 * centred on its own direction, divide by the sector, take it modulo the count
 * and then rotate by half a turn (the +2 / +4 / +1) so index 0 lands on
 * straight ahead rather than straight behind.
 *
 * The two-way block reuses one register for the offset and the divisor because
 * for N == 2 the sector *is* PI -- that is a CSE, not a different formula. */
u_char ConvertRot2Dir(float rot, u_char id)                             /* 163 */
{
    u_char dir = 0;

    switch (id) {                                                       /* 166 */
    case 0:
        dir = (u_char)((int)((rot + UNIT_CTL_PI + UNIT_CTL_PI / 4.0f) /
                             (UNIT_CTL_PI / 2.0f)) % 4);                /* 168 */
        dir += 2;                                                       /* 169 */
        if (dir >= 4) {                                                 /* 170 */
            dir -= 4;                                                   /* 171 */
        }
        break;

    case 1:
        dir = (u_char)((int)((rot + UNIT_CTL_PI + UNIT_CTL_PI / 8.0f) /
                             (UNIT_CTL_PI / 4.0f)) % 8);                /* 174 */
        dir += 4;                                                       /* 175 */
        if (dir >= 8) {                                                 /* 176 */
            dir -= 8;                                                   /* 177 */
        }
        break;

    case 2:
        dir = (u_char)((int)((rot + UNIT_CTL_PI + UNIT_CTL_PI / 2.0f) /
                             UNIT_CTL_PI) % 2);                         /* 180 */
        dir += 1;                                                       /* 181 */
        if (dir >= 2) {                                                 /* 182 */
            dir -= 2;                                                   /* 183 */
        }
        break;

    default:
        if (rot < 0.0f) { dir = 1; }                                    /* 186 */
        break;
    }

    return dir;                                                         /* 192 */
}

/* Freeze / hide the resident ghosts around a scripted death.  `req` is the
 * phase and `except` a bitmask of ENE_WRK slots that keep running -- the
 * dying ghost passes (1 << its own index), so it plays its death while
 * everything else holds still.
 *
 *   0  release: clear both bits on every slot that was frozen
 *   1  freeze the action script (0x40000000 -- EneActRule() bails on it)
 *   2  stop drawing (0x2000000 -- EnemyDrawOne() and the animation bail)
 *   3  both
 *
 * Only slots in ENE_STATUS_ACT are touched, and only by 1..3; the release
 * sweep does not test the status, so a ghost that changed state while frozen
 * is still let go.  enemy_act.c's EJobC05 is the only caller. */
void ReqEneStop(u_char req, u_char except)                              /* 207 */
{
    ENE_WRK *ew;
    u_char   i;

    switch (req) {                                                      /* 212 */
    case 0:
        /* The release arm ignores `except` -- everything frozen is let go. */
        for (ew = &ene_wrk[0], i = 0; i < ENE_WRK_MAX; i++, ew++) {     /* 215 */
            if ((ew->st.sta & 0x40000000L) != 0) {                      /* 216 */
                ew->st.sta &= ~(0x40000000L | 0x2000000L);              /* 217 */
            }
        }                                                               /* 219 */
        break;

    case 1:
        /* A slot named in `except` keeps its script running. */
        for (ew = &ene_wrk[0], i = 0; i < ENE_WRK_MAX; i++, ew++) {     /* 223 */
            if ((ew->status == ENE_STATUS_ACT) &&
                (((except >> i) & 1) == 0)) {                           /* 225 */
                ew->st.sta |= 0x40000000L;                              /* 226 */
            }
        }                                                               /* 228 */
        break;

    case 2:
        /* Same filter, but this one takes them out of the draw list. */
        for (ew = &ene_wrk[0], i = 0; i < ENE_WRK_MAX; i++, ew++) {     /* 232 */
            if ((ew->status == ENE_STATUS_ACT) &&
                (((except >> i) & 1) == 0)) {                           /* 234 */
                ew->st.sta |= 0x2000000L;                               /* 235 */
            }
        }                                                               /* 237 */
        break;

    case 3:
        /* Frozen and hidden.  Two statements, one load/store pair. */
        for (ew = &ene_wrk[0], i = 0; i < ENE_WRK_MAX; i++, ew++) {     /* 241 */
            if ((ew->status == ENE_STATUS_ACT) &&
                (((except >> i) & 1) == 0)) {                           /* 243 */
                ew->st.sta |= 0x40000000L;                              /* 244 */
                ew->st.sta |= 0x2000000L;                               /* 245 */
            }
        }                                                               /* 247 */
        break;
    }                                                                   /* 248 */
}
