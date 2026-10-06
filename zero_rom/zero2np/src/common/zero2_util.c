// FILE: /home/zero_rom/zero2np/src/common/zero2_util.c
//
// Shared Zero2 utilities: the helpers that were too game-aware for the
// generic utility.c / utility2.c split.  Three unrelated groups live here:
//
//   * GetObjectPos      - the one place that maps an (obj_type, obj_id) pair
//                         to a world position, whatever kind of thing it is.
//                         The event gaze and event camera both target through
//                         it, which is why it has to know about ghosts, both
//                         girls and the room's registration records at once.
//   * Zero2Print*Func   - the game's implementations of utility2.c's warning
//                         and assert hooks.  The assert one is the reason a
//                         failed assert shows on screen and halts.
//   * utilTim2SendVram /
//     GetRandomPositionXZ /
//     CalcAngle         - small maths and TIM2 helpers with no better home.
//
// Reconstructed from the Feb 6 2004 prototype (SLES_523.84).

#include "zero2_util.h"

#include <math.h>                                   // sinf / cosf
#include <stdio.h>                                  // printf

#include "utility.h"                                // GetRandValF
#include "utility2.h"                               // PRINT_ASSERT / RotLimitChk2
#include "variable.h"                               // plyr_wrk / sis_wrk
#include "libvu0.h"                                 // sceVu0CopyVector / Normalize / Inner / Outer
#include "../graphics/graph2d/message.h"            // SetASCIIString2
#include "../graphics/graph2d/tim2.h"               // Tim2GetPictureHeader / MakeTim2Direct / MakeClutDirect
#include "../graphics/graph3d/g3dMath.h"            // g3dAcosf
#include "../graphics/graph3d/g3ddbg.h"             // G3DASSERT
#include "../ingame/enemy/enemy.h"                  // GetEnePos
#include "../ingame/map/MapLoad.h"                  // MapLoadGetRegBuffID
#include "../ingame/map/RegDat.h"                   // RegDatGetStPtr4Label / MDAT_OBJ
#include "../ingame/plyr/player.h"                  // GetPlyrAreaNo
#include "../main/main_decls.h"                     // PadSyncCallback
#include "../system/os/system.h"                    // SendDMAMain
#include "../system/pad/pad.h"                      // paddat
#include "../graphics/graph3d/g3dxVu0.h" /* g3dxVu0CopyVector */

/* This object's own PI copies (.lit4 0x3eea00).  Same truncated value as
 * utility2.c's -- one entry per inline expansion, not distinct constants. */
static const float Z2U_PI  = 3.1415925f;
static const float Z2U_PI2 = 6.283185f;

/* Where Zero2PrintAssertFunc() puts its two lines, and the pad it waits on. */
#define ASSERT_TEXT_X       30.0f
#define ASSERT_TITLE_Y      30.0f
#define ASSERT_BODY_Y       60.0f
#define ASSERT_PAD_DECIDE   0

/* ---------------------------------------------------------------------------
 *  Object position lookup
 * ------------------------------------------------------------------------ */

int GetObjectPos(float *obj_pos, u_char obj_type, int obj_id)
{                                                                       /* 34 */
    int       i;
    int       res = 1;                                                  /* 36 */
    MDAT_OBJ *obj_data;

    /* Cleared up front so a failing lookup still leaves a defined vector. */
    for (i = 3; i >= 0; i--)                                            /* 41 */
    {
        obj_pos[i] = 0.0f;                                              /* 42 */
    }

    switch (obj_type)                                                   /* 46 */
    {
    case 0:
    case 1:
    case 2:
    case 5:
        /* Ghost kinds -- enemy.c owns the work-slot search. */
        res = (GetEnePos(obj_pos, obj_type, obj_id) != 0);              /* 57 */
        break;                                                          /* 63 */

    case OBJ_TYPE_PLAYER:
        g3dxVu0CopyVector(obj_pos, plyr_wrk.cmn_wrk.mbox.pos);
        break;

    case OBJ_TYPE_SISTER:
        g3dxVu0CopyVector(obj_pos, sis_wrk.cmn_wrk.mbox.pos);
        break;

    case 8:
    case 9:
        /* A placed map object, found by label in whichever registration
         * buffer holds the player's current room and floor. */
        obj_data = (MDAT_OBJ *)RegDatGetStPtr4Label(                    /* 68 */
            MapLoadGetRegBuffID(GetPlyrAreaNo(),                        /* 67 */
                                (int)(short)plyr_wrk.cmn_wrk.floor),
            obj_id);

        obj_pos[0] = obj_data->Pos[0];                                  /* 70 */
        obj_pos[1] = obj_data->Pos[1];                                  /* 71 */
        obj_pos[2] = obj_data->Pos[2];                                  /* 72 */
        obj_pos[3] = 0.0f;                                              /* 73 */
        break;                                                          /* 74 */

    default:
        /* 3, 4 and anything past the table.  The banner is four printfs in
         * the ROM, not a PRINT_ERROR. */
        printf("*******************************************************\n");    /* 79 */
        printf("*     Error!! The value of the data is illegal!!!     *\n");     /* 80 */
        printf("*                   GetObjectPos()                    *\n");     /* 81 */
        printf("*******************************************************\n");    /* 82 */
        PRINT_ASSERT("");                                               /* 83 */
        res = 0;                                                        /* 85 */
        break;
    }

    return res;                                                         /* 89 */
}

/* ---------------------------------------------------------------------------
 *  Report hooks
 * ------------------------------------------------------------------------ */

void Zero2PrintWarningFunc(char *str)
{                                                                       /* 95 */
    printf("<<<Warning\n");                                             /* 97 */
    printf("%s\n", str);                                                /* 98 */

    /* Lines 99-108 of the ROM emit no code.  This object's .rodata carries an
     * unreferenced "Warning!!" string right beside the "Assert!!" one below,
     * which is what is left of the on-screen half of this function -- it was
     * disabled, leaving only the console output. */
}                                                                       /* 109 */

/* Unlike the warning hook this never returns until the player acknowledges:
 * it drives the display itself, one frame at a time, so an assert freezes the
 * game with its message visible rather than scrolling past in the log. */
void Zero2PrintAssertFunc(char *str)
{                                                                       /* 113 */
    do                                                                  /* 129 */
    {
        SetASCIIString2(0, ASSERT_TEXT_X, ASSERT_TITLE_Y, 1,            /* 130 */
                        0x80, 0, 0, "Assert!!");
        SetASCIIString2(0, ASSERT_TEXT_X, ASSERT_BODY_Y, 1,             /* 131 */
                        0x80, 0x80, 0x80, str);
        SendDMAMain();                                                  /* 132 */

        PadSyncCallback();                                              /* 134 */
    } while (*paddat[ASSERT_PAD_DECIDE] != 1);                          /* 135 */
}

/* ---------------------------------------------------------------------------
 *  TIM2 upload
 * ------------------------------------------------------------------------ */

void utilTim2SendVram(u_int *tim2_addr, int tbp, int cbp, int tw_2, int th_2)
{                                                                       /* 141 */
    TIM2_PICTUREHEADER *ph;

    ph = Tim2GetPictureHeader(tim2_addr, 0);                            /* 145 */

    /* TEX0: TBP0 | TBW | PSM=PSMT8 | TW | TH | TCC=1 | CBP | CLD=1.
     * Note TBW is fed the *log2* width as well -- the ROM passes tw_2 to both
     * the TBW and TW fields rather than the buffer width in 64-texel units. */
    ph->GsTex0 = ((u_long)tbp)                                          /* 147 */
               | ((u_long)tw_2 << 14)       /* TBW  */
               | ((u_long)0x13  << 20)      /* PSM  = PSMT8            */
               | ((u_long)tw_2 << 26)       /* TW   */
               | ((u_long)th_2 << 30)       /* TH   */
               | ((u_long)1    << 34)       /* TCC  = RGBA             */
               | ((u_long)cbp  << 37)       /* CBP  */
               | ((u_long)1    << 61);      /* CLD  = load the CLUT    */

    MakeTim2Direct(tim2_addr, tbp, 0);                                  /* 151 */
    MakeClutDirect(tim2_addr, cbp, 0);                                  /* 153 */
}

/* ---------------------------------------------------------------------------
 *  Small maths helpers
 * ------------------------------------------------------------------------ */

void GetRandomPositionXZ(float *pos, float *o_pos, float max, float min)
{                                                                       /* 161 */
    float radius;
    float radian;

    radius  = GetRandValF(max - min) + min;                             /* 162 */
    radian  = GetRandValF(Z2U_PI2);                                     /* 163 */
    radian -= Z2U_PI;                                                   /* 164 */

    pos[0] = o_pos[0] + radius * sinf(radian);                          /* 166 */
    pos[1] = o_pos[1];                                                  /* 167 */
    pos[2] = o_pos[2] + radius * cosf(radian);                          /* 168 */
    pos[3] = o_pos[3];                                                  /* 169 */
}

float CalcAngle(const float *vDirectionAxis, const float *vDirection,
                const float *vTop, PLANE3D plane_3d)
{                                                                       /* 181 */
    float vDirectionAxisIn[4];
    float vDirectionIn[4];
    float vRightVec[4];
    float fRot;

    G3DASSERT((plane_3d == YZ || plane_3d == ZX || plane_3d == XY), "");  /* 185 */

    /* Work on copies: flattening the plane's axis to zero is what makes this
     * a 2D measurement, and the caller's vectors must survive it. */
    g3dxVu0CopyVector(vDirectionAxisIn, (float *)vDirectionAxis);
    g3dxVu0CopyVector(vDirectionIn, (float *)vDirection);

    vDirectionAxisIn[plane_3d] = 0.0f;                                  /* 190 */
    vDirectionIn[plane_3d]     = 0.0f;                                  /* 191 */

    sceVu0Normalize(vDirectionAxisIn, vDirectionAxisIn);                /* 193 */
    sceVu0Normalize(vDirectionIn, vDirectionIn);                        /* 194 */

    /* Right-hand reference so the unsigned acos can be given a sign. */
    sceVu0OuterProduct(vRightVec, (float *)vTop, vDirectionAxisIn);     /* 198 */

    fRot = g3dAcosf(-sceVu0InnerProduct(vDirectionAxisIn, vDirectionIn));

    if (sceVu0InnerProduct(vRightVec, vDirectionIn) < 0.0f)
    {
        fRot = -fRot;
    }

    return RotLimitChk2(fRot + Z2U_PI);                                 /* 206 */
}
