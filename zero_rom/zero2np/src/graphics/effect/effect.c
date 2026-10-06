// FILE: /home/zero_rom/zero2np/src/graphics/effect/effect.c
//
// The effect system's core: the four EFFECT_CONT control tables, the private
// heap, the 25 per-effect SetEffects_* request front doors, the per-frame
// EffectControl() pass, and the Z-sorted draw of every modeled effect.
//
// PORT DEVIATION: the ROM's front door is one variadic SetEffects(id, fl, ...)
// that dispatches to those 25 as va_list unpackers.  See effect.h.
//
// All 38 ZERO2.MAP exports plus the 37 statics.  Statements whose only memory
// access goes through a fixed_array subscript lose their line marker to the
// inlined bounds check; interpolated annotations carry a trailing '?'.
//
// Reconstructed from the Feb 6 2004 prototype (SLES_523.84), 0x0013d198.

#include "effect.h"

#include <stdio.h>
#include <string.h>

#include "effect_butterfly.h"
#include "effect_ene.h"
#include "effect_obj.h"
#include "effect_oth.h"
#include "effect_rain.h"
#include "effect_rdr.h"
#include "effect_scr.h"
#include "effect_sub.h"
#include "effect_torch.h"

#include "../obj_draw_ctrl.h"            /* GetEffDrawFLG */
#include "../graph2d/g2d_draw.h"         /* LocalCopyLtoL */
#include "../graph3d/g3dxVu0.h"          /* g3dxVu0CopyVector */
#include "../graph3d/gra3d.h"            /* gra3dGetCamera */
#include "../../common/my_malloc.h"
#include "../../common/heapctrl.h"
#include "../../ingame/ingame_effect.h"  /* IgEffectStoryMainScreenEffectReq */
#include "../../ingame/plyr/player.h"    /* PlayerModeIsFinder / nearest dist */
#include "../../sdk/libvu0.h"
#include "../../system/os/system.h"      /* GetSystemHeapWrkP / sys_wrk */

/* The six canned screen-effect parameter sets the story script picks from
 * with EffectSetScreenEffectNo(); 0 is the ordinary walking-around look. */
SCREEN_EFFECT_PARAMETER ScreenEffectParam00 =               /* data 2dbc60 */
{
    0, 3,  8, 32, 54, 18, 0, 32, 1000, 1800, 0,  0,  0, 0, 0,
    3,  64,  64, 128, 128, 128, 0, 26, 0
};
SCREEN_EFFECT_PARAMETER ScreenEffectParam01 =               /* data 2dbcc0 */
{
    0, 8,  8, 43, 54, 18, 0, 32, 1000, 1800, 2,  5, 58, 0, 0,
    3, 255, 128, 128, 128, 128, 1, 45, 0
};
SCREEN_EFFECT_PARAMETER ScreenEffectParam02 =               /* data 2dbd20 */
{
    0, 3, 26, 67, 83, 10, 0,  0, 1000, 1800, 2, 13,  0, 1, 0,
    3, 100, 100, 128, 128, 128, 0, 26, 0
};
SCREEN_EFFECT_PARAMETER ScreenEffectParam03 =               /* data 2dbd80 */
{
    0, 0,  8, 65, 54, 18, 0, 32, 1000, 1800, 0,  0,  0, 0, 0,
    3,  64,  64, 128, 128, 128, 0, 26, 0
};
SCREEN_EFFECT_PARAMETER ScreenEffectParam04 =               /* data 2dbde0 */
{
    0, 0,  8, 65, 54, 18, 0, 32, 1000, 1800, 0,  0,  0, 0, 0,
    3,  64,  64, 128, 128, 128, 0, 26, 0
};
SCREEN_EFFECT_PARAMETER ScreenEffectParam05 =               /* data 2dbe40 */
{
    0, 8,  8, 40, 64, 50, 0, 32, 1000, 1800, 0,  0,  0, 0, 0,
    3,  64,  64, 128, 128, 128, 0, 26, 0
};

/* The four control tables.  efcnt/efcnt_cnt hold the fixed-slot screen
 * effects (id == slot, 0..15); efcntm/efcntm_cnt the modeled ones, allocated
 * by SearchEmptyEffectBuf().  Which of each pair is live is EFF_WRK's
 * change_bank -- the *_cnt twins are bank 0. */
fixed_array<EFFECT_CONT, EFCNT_MAX>  efcnt;                 /* data 2dbea0 */
fixed_array<EFFECT_CONT, 48>         efcntm;                /* data 2ddaa0 */
fixed_array<EFFECT_CONT, EFCNT_MAX>  efcnt_cnt;             /* data 2defa0 */
fixed_array<EFFECT_CONT, 48>         efcntm_cnt;            /* data 2e0ba0 */

/* PORT DEVIATION, same as every reference_fixed_array so far: the ROM parks
 * the backing pointer table in .rodata and this build never writes it. */
static SCREEN_EFFECT_PARAMETER *aScreenEffectParamPtr[6] =  /* rdata 3a5c78 */
{
    &ScreenEffectParam00, &ScreenEffectParam01, &ScreenEffectParam02,
    &ScreenEffectParam03, &ScreenEffectParam04, &ScreenEffectParam05
};
reference_fixed_array<SCREEN_EFFECT_PARAMETER *, 6>
    pScreenEffectParamPtr(aScreenEffectParamPtr);           /* sdata 3efca8 */

/* The effect system's private heap: one 0x90000 block taken off the system
 * heap once, then sub-allocated through EFFECT_MALLOC/EFFECT_FREE. */
static void *pEffectHeapAdrs;                               /* sdata 3efcac */

/* Screen-effect state the SetBlur/SetDither/... machines in effect_scr.c
 * share; zero-filled at boot and never re-initialised. */
SBTSET msbtset;                                             /* data 2e20a0 */

u_char g_bInterlace;                                        /* sdata 3efcc8 */

/* disp / monochrome / stop / bank / blur-off / dither-off / filament-off. */
typedef struct                          /* 0x1c, types.txt EFF_WRK */
{
    /* 0x00 */ int disp_flg;
    /* 0x04 */ int monochro_mode;
    /* 0x08 */ int stop_flg;
    /* 0x0c */ int change_bank;
    /* 0x10 */ int blur_off;
    /* 0x14 */ int dith_off;
    /* 0x18 */ int filament_off;
} EFF_WRK;

static EFF_WRK eff_wrk;                                     /* bss 423430 */

int look_debugmenu;                                         /* sdata 3efccc */

static int ScreenEffectStatus;                              /* sbss 3f4b68 */
static int ScreenEffectNo;                                  /* sbss 3f4b6c */

static MY_MALLOC EffectMallocWrk;                           /* bss 423450 */

/* Forward declarations, as in the ROM source. */
static void EffectScreenFinder(void);
static void EffectScreenEnemyDead(void);
static int  CheckSetEffectsOK(void);
static EFFECT_CONT *GetEffectContBuf(int id);
static EFFECT_CONT *SetEffectsBegin(int id);
static void EffectContClear(EFFECT_CONT *pEffCont);
static int  SearchEmptyEffectBuf(void);
static void EffectZSort(int PDeformBlurFlg);
static void EffectZSort2(void);
static void EffectInPhoto(EFFECT_CONT *ecm, int PDeformBlurFlg);

void InitEffects(void)
{
    look_debugmenu = 1;                                             /* 178 */

    /* Boot state of the switch block: drawing on, colour, running, bank 1,
     * blur suppressed (SetBlurOff() is the boot value, EffectEndSet() the
     * "let it run" one), dither and filament allowed. */
    eff_wrk.disp_flg = 1;                                           /* 179 */
    eff_wrk.monochro_mode = 0;                                      /* 180 */
    eff_wrk.stop_flg = 0;                                           /* 181 */
    eff_wrk.change_bank = 1;                                        /* 182 */
    eff_wrk.blur_off = 1;                                           /* 184 */
    eff_wrk.dith_off = 0;
    eff_wrk.filament_off = 0;                                       /* 186 */

    InitEffectSub();                                                /* 188 */
    InitEffectScr();                                                /* 189 */
    InitEffectObj();                                                /* 190 */
    InitEffectOth();                                                /* 191 */
    InitEffectRdr(0);                                               /* 192 */
    InitEffectEne();                                                /* 193 */

    memset(&efcnt, 0, sizeof(efcnt));                               /* 196 */
    memset(&efcnt_cnt, 0, sizeof(efcnt_cnt));                       /* 197 */
    memset(&efcntm, 0, sizeof(efcntm));                             /* 198 */
    memset(&efcntm_cnt, 0, sizeof(efcntm_cnt));                     /* 199 */

    EffectButterflyInit();                                          /* 201 */
    EffectButterflyParticleInit();                                  /* 202 */
    EffectRainInit();                                               /* 203 */
    EffectSprayInit();                                              /* 204 */
    EffectDropOfWaterInit();                                        /* 205 */

    ScreenEffectStatus = 0;                                         /* 206 */
    ScreenEffectNo = 0;

    EffectSndInit();                                                /* 209 */
    EffectTorch2BigFreaInit();                                      /* 210 */

    if (pEffectHeapAdrs == nullptr)                                 /* 212 */
    {
        pEffectHeapAdrs = SAFE_MALLOC(GetSystemHeapWrkP(), nullptr, 0x90000); /* 213 */
    }

    /* PORT DEVIATION: the ROM calls my_mallocInit() unconditionally, because
     * on the PS2 the system heap always had this 0x90000 to give.  Here a
     * failed reservation would leave the allocator based at NULL and hand out
     * pointers near zero, so the region is left uninitialised instead --
     * EFFECT_MALLOC() then fails cleanly and every effect that allocates just
     * stays inert. */
    if (pEffectHeapAdrs == nullptr)
    {
        printf("InitEffects() cannot reserve the effect heap\n");
        return;
    }

    my_mallocInit(&EffectMallocWrk, pEffectHeapAdrs, 0x90000);      /* 215 */
}

/* The per-frame half of the init pair: re-arm the every-frame sub-systems
 * and issue this frame's whole-screen effect. */
void InitEffectsEF(void)
{
    InitEffectScrEF();                                              /* 225 */
    InitEffectObjEF();                                              /* 226 */
    InitEffectOthEF();                                              /* 227 */
    InitEffectRdrEF();                                              /* 228 */
    InitEffectEneEF();                                              /* 229 */

    if (ScreenEffectStatus == 0)                                    /* 231 */
    {
        if (PlayerModeIsFinder() != 0 && ScreenEffectNo == 0)       /* 232 */
        {
            EffectScreenFinder();                                   /* 244 */
        }
        else
        {
            IgEffectStoryMainScreenEffectReq(
                EffectGetScreenEffectParamPtr(ScreenEffectNo));     /* 247 */
        }
    }
    else if (ScreenEffectStatus == 1)                               /* 253 */
    {
        EffectScreenEnemyDead();                                    /* 255 */
    }

    EffectSndCtrl();                                                /* 261 */
}

/* The live modeled-effect bank's base -- what enemy.c reads efcnt[12] tricks
 * through. */
EFFECT_CONT *EffectGetBufferTopAdrs(void)
{
    if (EffWrkEffectBankGet() == 0)                                 /* 272 */
    {
        return &efcntm_cnt[0];
    }

    return &efcntm[0];
}

/* --------------------------------------------------------------------------
 *  The finder's own whole-screen look: dither that speeds up as the nearest
 *  ghost closes in, plus a fixed contrast lift.  The Blur/Contrast selectors
 *  and their parameter statics are a debug tuning board -- Blur 0 and
 *  Contrast 3 ship, so only those arms ever run.
 * ------------------------------------------------------------------------ */
static void EffectScreenFinder(void)
{
    float lng_max = 1800.0f;
    float lng_min = 300.0f;                                         /* 281 */
    float speed_max = 85.0f;
    float speed_change;
    float alpha_max = 40.0f;
    float alpha_change;
    int   Blur = 0;                                                 /* 288 */
    static int BlurAlpha = 32;                              /* sdata 3efcb0 */
    int   Contrast = 3;                                             /* 293 */
    static int NegaAlpha2 = 128;                            /* sdata 3efcb4 */
    float ll;

    if (look_debugmenu != 0)                                        /* 297 */
    {
        ll = GetNearestDistFromPlyrToEnemy();                       /* 298 */

        if (ll != 0.0f && ll < lng_max)                             /* 306 */
        {
            if (ll < lng_min)                                       /* 307 */
            {
                ll = 0.0f;                                          /* 308 */
            }
            else
            {
                ll -= lng_min;
            }

            speed_change = speed_max - (49.0f * ll) / 1500.0f;
            alpha_change = alpha_max - (32.0f * ll) / 1500.0f;
            SetEffects_DITHER(1, 3, speed_change,
                              alpha_change, 54, 18, 0, 0, 0);       /* 314 */
        }
        else
        {
            SetEffects_DITHER(1, 3, 36.0f, 8.0f, 54, 18, 0, 0, 0);  /* 318 */
        }

        if (Blur == 1)                                              /* 322 */
        {
            SetEffects_BLUR(0, 1, &BlurAlpha, 1000, 1800, 320.0f, 224.0f); /* 323 */
        }
        if (Blur == 2)                                              /* 325 */
        {
            SetEffects_BLUR(1, 1, &BlurAlpha, 1000, 1800, 320.0f, 224.0f); /* 326 */
        }
        if (Blur == 3)                                              /* 328 */
        {
            SetEffects_BLUR(2, 1, &BlurAlpha, 1000, 1800, 320.0f, 240.0f); /* 329 */
        }

        if (Contrast == 2)                                          /* 344 */
        {
            SetEffects_NCONTRAST(0xd, 1, 0x40, 0x40);               /* 345 */
        }
        if (Contrast == 3)                                          /* 347 */
        {
            SetEffects_NCONTRAST(0xe, 1, 0x40, 0x40);               /* 348 */
        }
        if (Contrast == 4)                                          /* 350 */
        {
            SetEffects_NCONTRAST(0xf, 1, 0x40, 0x40);               /* 351 */
        }
        if (Contrast == 5)                                          /* 353 */
        {
            SetEffects_NEGA(1, 0x80, 0x80, 0, 0, 0, &NegaAlpha2);   /* 354 */
        }
    }
}                                                                   /* 360 */

/* The "a ghost just died" screen: heavy dither, a deform wobble, a fade
 * frame and the contrast lift. */
static void EffectScreenEnemyDead(void)
{
    static int BlurAlpha = 64;                              /* sdata 3efcb8 */
    static int NegaAlpha2 = 128;                            /* sdata 3efcbc */

    (void)BlurAlpha; (void)NegaAlpha2;  /* parked debug-board parameters */

    if (look_debugmenu != 0)                                        /* 378 */
    {
        SetEffects_DITHER(1, 3, 68.0f, 32.0f, 54, 8, 0, 0, 0);      /* 386 */
        SetEffects_DEFORM(1, 2, 24, 0, 0, 0);                       /* 400 */
        SetEffects_FADEFRAME(1, 0x3d, 0);                           /* 408 */
        SetEffects_NCONTRAST(0xe, 1, 0x40, 0x40);                   /* 415 */
    }
}                                                                   /* 427 */

/* Lets the blur machinery run again -- the "end of the intro" call. */
void EffectEndSet(void)
{
    EffWrkBlurOffSet(0);                                            /* 433 */
}

static int CheckSetEffectsOK(void)
{
    if (GetEffDrawFLG() != 0 && EffWrkDispFlgGet() != 0)            /* 449 */
    {
        return 1;                                                   /* 462 */
    }

    return 0;
}                                                                   /* 465 */

/* --------------------------------------------------------------------------
 *  id -> control record.  Screen effects (0..15) own their slot outright;
 *  modeled effects (0x12..0x22) take the first free slot in the modeled
 *  bank.  0x23 (ENEIN) also arrives here but only through SetEffects_ENEIN()
 *  -- the ROM's range list simply omits it, so it falls into the error arm
 *  and ENEIN can only ever reuse a caller-supplied slot.
 * ------------------------------------------------------------------------ */
static EFFECT_CONT *GetEffectContBuf(int id)
{
    EFFECT_CONT *pRetEc;
    int          ret;

    switch (id)                                                     /* 482 */
    {
    case 0: case 1: case 2: case 3: case 4: case 5: case 6: case 7:
    case 8: case 9: case 10: case 0xb: case 0xc: case 0xd: case 0xe:
    case 0xf:
        if (EffWrkEffectBankGet() == 0)                             /* 500 */
        {
            pRetEc = &efcnt_cnt[id];                                /* 501 */
        }
        else
        {
            pRetEc = &efcnt[id];
        }
        break;

    case 0x12: case 0x15: case 0x16: case 0x18: case 0x19: case 0x1a:
    case 0x1b: case 0x1c: case 0x1d: case 0x1e: case 0x1f: case 0x20:
    case 0x21: case 0x22:
        ret = SearchEmptyEffectBuf();                               /* 518 */
        if (ret == -1)                                              /* 519 */
        {
            printf("EffectBuffer is Full!!\n");
            pRetEc = nullptr;
        }
        else if (EffWrkEffectBankGet() == 0)                        /* 522 */
        {
            pRetEc = &efcntm_cnt[ret];                              /* 524 */
        }
        else
        {
            pRetEc = &efcntm[ret];
        }
        break;

    default:
        printf("Unknown Effect ID!!\n");                            /* 526 */
        pRetEc = nullptr;
        break;
    }

    return pRetEc;                                                  /* 530 */
}

/* --------------------------------------------------------------------------
 *  The effect request front door.
 *
 *  PORT DEVIATION -- see the block comment in effect.h.  In the ROM this was
 *  one variadic SetEffects(id, fl, ...) whose body is the four statements
 *  below, followed by a switch that handed the argument tail to one of the
 *  per-effect unpackers.  Those unpackers are the entry points now, so the
 *  head of SetEffects() lives here and every one of them opens with it: the
 *  gate at 546, the lookup at 556 and the null test at 557 are unchanged, and
 *  each entry point still returns the record (null included) the ROM returned
 *  at 665.
 * ------------------------------------------------------------------------ */
static EFFECT_CONT *SetEffectsBegin(int id)
{
    if (CheckSetEffectsOK() == 0)                                   /* 546 */
    {
        return nullptr;                                             /* 551?*/
    }

    return GetEffectContBuf(id);                                    /* 556 */
}

void CutEffects(int id)
{
    ResetEffects(GetEffectContBuf(id));                             /* 672 */
}

/* (fl) -- id 1.  The ROM's dispatch passed a constant type 0 that the handler
 * never read, so it is gone. */
void *SetEffects_Z_DEP(int fl)
{
    EFFECT_CONT *ec = SetEffectsBegin(0x01);                        /* 565 */

    if (ec == nullptr)                                              /* 557 */
    {
        return nullptr;
    }

    ec->dat.uc8[0] = 1;                                             /* 686 */
    ec->dat.uc8[1] = (u_char)fl;                                    /* 687 */

    return ec;                                                      /* 665 */
}

/* (type, alp, spd, amax, cmax [, in, keep, out]) -- id 2.  effect_scr.c's
 * SubDither3(type, alp, spd, ...) names the slots.  in/keep/out are read only
 * when fl bit 2 is set; the ROM's callers that leave it clear pushed no such
 * arguments at all, and pass 0 here. */
void *SetEffects_DITHER(int fl, int type, float alp, float spd,
                        int amax, int cmax,
                        u_int in, u_int keep, u_int out)
{
    EFFECT_CONT *ec = SetEffectsBegin(0x02);                        /* 568 */

    if (ec == nullptr)                                              /* 557 */
    {
        return nullptr;
    }

    ec->dat.uc8[0] = 2;                                             /* 701 */
    ec->dat.uc8[1] = (u_char)fl;                                    /* 702 */
    ec->dat.uc8[2] = (u_char)type;                                  /* 703 */
    ec->dat.fl32[2] = alp;                                          /* 704 */
    ec->dat.fl32[3] = spd;                                          /* 705 */
    ec->dat.uc8[3] = (u_char)amax;                                  /* 706 */
    ec->dat.uc8[4] = (u_char)cmax;                                  /* 707 */

    if ((fl & 4) != 0)                                              /* 708 */
    {
        ec->cnt = 0;                                                /* 709 */
        ec->in = in;
        ec->keep = keep;                                            /* 710 */
        ec->out = out;                                              /* 711 */
        if (ec->in != 0)                                            /* 712 */
        {
            ec->flow = 0;
        }
        else if (ec->keep != 0)
        {
            ec->flow = 1;
        }
        else if (ec->out != 0)
        {
            ec->flow = 2;
        }
        else
        {
            ec->flow = 3;
        }
    }

    return ec;                                                      /* 665 */
}                                                                   /* 713 */

int EffectDitherIsSet(void)
{
    EFFECT_CONT *p = GetEffectContBuf(2);                           /* 722 */

    return p->dat.uc8[0] != 0;                                      /* 725 */
}                                                                   /* 729 */

/* (pAlpha, ui32[2], ui32[3], centre x, centre y) -- ids 3/4/5 map one to one
 * onto types 0/1/2, which is how the ROM's dispatch reached this handler.
 * pAlpha is read back as a u_char through the pointer, so the int the screen
 * effects hand over works on a little-endian host for the 0..255 they use. */
void *SetEffects_BLUR(int type, int fl, void *pAlpha,
                      u_int scale, u_int rot, float cx, float cy)
{
    EFFECT_CONT *ec = SetEffectsBegin(type + 3);                    /* 571..577 */

    if (ec == nullptr)                                              /* 557 */
    {
        return nullptr;
    }

    if (type == 0)                                                  /* 743 */
    {
        ec->dat.uc8[0] = 3;                                         /* 744 */
        ec->dat.uc8[2] = 0;                                         /* 745 */
    }
    else if (type == 1)                                             /* 746 */
    {
        ec->dat.uc8[0] = 4;                                         /* 749 */
        ec->dat.uc8[2] = 1;                                         /* 750 */
    }
    else
    {
        ec->dat.uc8[0] = 5;                                         /* 754 */
        ec->dat.uc8[2] = 2;                                         /* 755 */
    }
    ec->dat.uc8[1] = (u_char)fl;                                    /* 757 */

    ec->pnt[0] = pAlpha;                                            /* 759 */
    ec->dat.ui32[2] = scale;                                        /* 760 */
    ec->dat.ui32[3] = rot;                                          /* 761 */
    ec->fw[0] = cx;
    ec->fw[1] = cy;

    return ec;                                                      /* 665 */
}

/* (type, rate [, in, keep, out]) -- id 6.  Note the envelope decision reads
 * the PREVIOUS request's windows -- the new ones are stored after it.  DITHER
 * decides from the new values; this one does not.  ROM order, kept. */
void *SetEffects_DEFORM(int fl, int type, int rate,
                        u_int in, u_int keep, u_int out)
{
    EFFECT_CONT *ec = SetEffectsBegin(0x06);                        /* 580 */

    if (ec == nullptr)                                              /* 557 */
    {
        return nullptr;
    }

    ec->dat.uc8[0] = 6;                                             /* 777 */
    ec->dat.uc8[1] = (u_char)fl;                                    /* 778 */
    ec->dat.uc8[2] = (u_char)type;                                  /* 779 */
    ec->dat.uc8[3] = (u_char)rate;                                  /* 780 */
    ec->dat.uc8[4] = 0;                                             /* 781 */

    if ((fl & 4) != 0)                                              /* 782 */
    {
        if (ec->in != 0)                                            /* 783 */
        {
            ec->flow = 0;
        }
        else if (ec->keep != 0)
        {
            ec->flow = 1;
        }
        else if (ec->out != 0)
        {
            ec->flow = 2;
        }
        else
        {
            ec->flow = 3;
        }
        ec->cnt = 0;                                                /* 784 */
        ec->in = in;                                                /* 785 */
        ec->keep = keep;                                            /* 786 */
        ec->out = out;                                              /* 787 */
    }

    return ec;                                                      /* 665 */
}

/* (power) -- id 7 */
void *SetEffects_FOCUS(int fl, int power)
{
    EFFECT_CONT *ec = SetEffectsBegin(0x07);                        /* 583 */

    if (ec == nullptr)                                              /* 557 */
    {
        return nullptr;
    }

    ec->dat.uc8[0] = 7;                                             /* 802 */
    ec->dat.uc8[1] = (u_char)fl;                                    /* 803 */
    ec->dat.uc8[2] = (u_char)power;                                 /* 804 */

    return ec;                                                      /* 665 */
}

/* (alpha) -- id 8 */
void *SetEffects_OVERLAP(int fl, int alpha)
{
    EFFECT_CONT *ec = SetEffectsBegin(0x08);                        /* 586 */

    if (ec == nullptr)                                              /* 557 */
    {
        return nullptr;
    }

    ec->dat.uc8[0] = 8;                                             /* 818 */
    ec->dat.uc8[1] = (u_char)fl;                                    /* 819 */
    ec->dat.uc8[2] = (u_char)alpha;                                 /* 820 */

    return ec;                                                      /* 665 */
}

/* (alpha, pri) -- id 9 */
void *SetEffects_FADEFRAME(int fl, int alpha, u_int pri)
{
    EFFECT_CONT *ec = SetEffectsBegin(0x09);                        /* 589 */

    if (ec == nullptr)                                              /* 557 */
    {
        return nullptr;
    }

    ec->dat.uc8[0] = 9;                                             /* 834 */
    ec->dat.uc8[1] = (u_char)fl;                                    /* 835 */
    ec->dat.uc8[2] = (u_char)alpha;                                 /* 836 */
    ec->dat.ui32[1] = pri;                                          /* 837 */

    return ec;                                                      /* 665 */
}

/* (type, pPos, pAlpha) -- id 0xa */
void *SetEffects_RENZFLARE(int fl, int type, void *pPos, void *pRot)
{
    EFFECT_CONT *ec = SetEffectsBegin(0x0a);                        /* 592 */

    if (ec == nullptr)                                              /* 557 */
    {
        return nullptr;
    }

    ec->dat.uc8[0] = 10;                                            /* 851 */
    ec->dat.uc8[1] = (u_char)fl;                                    /* 852 */
    ec->dat.uc8[2] = (u_char)type;                                  /* 853 */
    ec->pnt[0] = pPos;                                              /* 854 */
    ec->pnt[1] = pRot;                                              /* 855 */

    return ec;                                                      /* 665 */
}

/* (alpha) -- id 0xb */
void *SetEffects_BLACKFILTER(int fl, int alpha)
{
    EFFECT_CONT *ec = SetEffectsBegin(0x0b);                        /* 595 */

    if (ec == nullptr)                                              /* 557 */
    {
        return nullptr;
    }

    ec->dat.uc8[0] = 0xb;                                           /* 869 */
    ec->dat.uc8[1] = (u_char)fl;                                    /* 870 */
    ec->dat.uc8[2] = (u_char)alpha;                                 /* 871 */

    return ec;                                                      /* 665 */
}

/* (col, alpha [, in, keep, out] / pAlpha2) -- id 0xc.  The ROM's argument tail
 * forks on fl bit 2: with the envelope armed it is three window counts, and
 * without it a pointer to the second alpha, which EffectNega() dereferences
 * unconditionally.  Both tails are parameters here; a caller supplies the one
 * its `fl` selects and passes 0 / nullptr for the other. */
void *SetEffects_NEGA(int fl, int col, int alpha,
                      u_int in, u_int keep, u_int out, void *pAlpha2)
{
    EFFECT_CONT *ec = SetEffectsBegin(0x0c);                        /* 598 */

    if (ec == nullptr)                                              /* 557 */
    {
        return nullptr;
    }

    ec->dat.uc8[0] = 0xc;                                           /* 885 */
    ec->dat.uc8[1] = (u_char)fl;                                    /* 886 */
    ec->dat.uc8[2] = (u_char)col;                                   /* 887 */
    ec->dat.uc8[3] = (u_char)alpha;                                 /* 888 */

    if ((fl & 4) != 0)                                              /* 890 */
    {
        ec->cnt = 0;                                                /* 891 */
        ec->in = in;
        ec->keep = keep;                                            /* 892 */
        ec->out = out;                                              /* 893 */
        if (ec->in != 0)                                            /* 894 */
        {
            ec->flow = 0;
        }
        else if (ec->keep != 0)
        {
            ec->flow = 1;
        }
        else if (ec->out != 0)
        {
            ec->flow = 2;
        }
        else
        {
            ec->flow = 3;
        }
    }
    else
    {
        ec->pnt[0] = pAlpha2;                                       /* 895 */
    }

    return ec;                                                      /* 665 */
}                                                                   /* 897 */

/* (col, alpha) -- ids 0xd/0xe/0xf, and the id is what the handler stores. */
void *SetEffects_NCONTRAST(int id, int fl, int col, int alpha)
{
    EFFECT_CONT *ec = SetEffectsBegin(id);                          /* 601..607 */

    if (ec == nullptr)                                              /* 557 */
    {
        return nullptr;
    }

    ec->dat.uc8[0] = (u_char)id;                                    /* 913 */
    ec->dat.uc8[1] = (u_char)fl;                                    /* 914 */
    ec->dat.uc8[2] = (u_char)col;                                   /* 915 */
    ec->dat.uc8[3] = (u_char)alpha;                                 /* 916 */

    return ec;                                                      /* 665 */
}

/* --------------------------------------------------------------------------
 *  The story contrast filter, whose effect id is only known at run time.
 *
 *  PORT NOTE.  IgEffectStoryMainContrastTypeGet() maps the screen-effect
 *  parameter block's Contrast 2..5 onto ids 0xd/0xe/0xf/0xc and answers 0 for
 *  everything else, and puzzle.c feeds the answer straight to the ROM's
 *  variadic front door -- so one call site reached the contrast handler, the
 *  nega handler, or neither.  With typed entry points that has to be spelled
 *  out, and this is the only place in the build where it happens.
 *
 *  The nega arm needs an alpha pointer that puzzle.c never pushed: with fl bit
 *  2 clear the ROM read a fourth argument that was not there, and EffectNega()
 *  dereferences it every frame with no null test.  EffectScreenFinder() parks
 *  a `static int NegaAlpha2 = 128` for exactly this slot, so this does the
 *  same rather than reproducing a wild read.
 *
 *  Falling through with no store is the ROM's own behaviour for any other id:
 *  its switch had no arm for it, so the record was fetched and returned
 *  untouched.
 * ------------------------------------------------------------------------ */
void *SetEffectsStoryContrast(int id, int fl, int col, int alpha)
{
    static int StoryNegaAlpha2 = 128;

    switch (id)
    {
    case 0x0c:
        return SetEffects_NEGA(fl, col, alpha, 0, 0, 0, &StoryNegaAlpha2);

    case 0x0d:
    case 0x0e:
    case 0x0f:
        return SetEffects_NCONTRAST(id, fl, col, alpha);

    default:
        return SetEffectsBegin(id);                                 /* 556 */
    }
}

/* (type2) -- ids 0x10 and 0x11 select the stored kind. */
void *SetEffects_ENEDMG(int type, int fl, int type2)
{
    EFFECT_CONT *ec = SetEffectsBegin(0x10 + type);                 /* 610/615?*/

    if (ec == nullptr)                                              /* 557 */
    {
        return nullptr;
    }

    if (type == 0)                                                  /* 931 */
    {
        ec->dat.uc8[0] = 0x10;                                      /* 932 */
    }
    else
    {
        ec->dat.uc8[0] = 0x11;                                      /* 935 */
    }
    ec->dat.uc8[1] = (u_char)fl;                                    /* 937 */
    ec->dat.uc8[2] = (u_char)type2;                                 /* 939 */

    return ec;                                                      /* 665 */
}

/* (flow, pPos, size, r, g, scale, b, alpha, type, rate) -- id 0x15.  The
 * argument order really does interleave the two colours the way the two-colour
 * flame authored them; effect_oth.c's CallFire2() is the readable form. */
void *SetEffects_FIRE(int fl, int flow, void *pPos, int size,
                      int r, int g, float scale, int b, int alpha,
                      int type, float rate)
{
    EFFECT_CONT *ec = SetEffectsBegin(0x15);                        /* 621 */

    if (ec == nullptr)                                              /* 557 */
    {
        return nullptr;
    }

    ec->dat.uc8[0] = 0x15;                                          /* 952 */
    ec->dat.uc8[1] = (u_char)(fl | 0x80);                           /* 953 */
    ec->flow = (u_int)(char)flow;                                   /* 954 */
    ec->pnt[0] = pPos;                                              /* 955 */
    ec->dat.uc8[2] = (u_char)size;                                  /* 956 */
    ec->dat.uc8[3] = (u_char)r;                                     /* 957 */
    ec->dat.uc8[4] = (u_char)g;                                     /* 958 */
    ec->dat.fl32[2] = scale;                                        /* 959 */
    ec->dat.uc8[5] = (u_char)b;                                     /* 960 */
    ec->dat.uc8[6] = (u_char)alpha;                                 /* 962 */
    ec->dat.uc8[7] = (u_char)type;                                  /* 963 */
    ec->dat.fl32[3] = rate;                                         /* 964 */
    ec->cnt = 0;                                                    /* 965 */

    return ec;                                                      /* 665 */
}                                                                   /* 966 */

/* (type, pPos, r, g, b, scale, alpha) -- id 0x12 */
void *SetEffects_HALO(int fl, int type, void *pPos,
                      int r, int g, int b, float scale, int alpha)
{
    EFFECT_CONT *ec = SetEffectsBegin(0x12);                        /* 618 */

    if (ec == nullptr)                                              /* 557 */
    {
        return nullptr;
    }

    ec->dat.uc8[0] = 0x12;                                          /* 979 */
    ec->dat.uc8[1] = (u_char)(fl | 0x80);                           /* 980 */
    ec->dat.uc8[2] = (u_char)type;                                  /* 981 */
    ec->pnt[0] = pPos;                                              /* 982 */
    ec->dat.uc8[3] = (u_char)r;                                     /* 983 */
    ec->dat.uc8[4] = (u_char)g;                                     /* 984 */
    ec->dat.uc8[5] = (u_char)b;                                     /* 985 */
    ec->dat.fl32[2] = scale;                                        /* 986 */
    ec->dat.uc8[6] = (u_char)alpha;                                 /* 987 */

    return ec;                                                      /* 665 */
}                                                                   /* 988 */

/* (type, max, sclx, scly, pWrk, in, keep, out, pDrive, pSpeed, pWave, pXYZ,
 * r, g, b) -- id 0x18.  effect_obj.c's seven CallPartsDeform* front doors are
 * this call with different slots defaulted. */
void *SetEffects_PDEFORM(int fl, int type, u_int max, float sclx, float scly,
                         void *pWrk, u_int in, u_int keep, u_int out,
                         void *pDrive, void *pSpeed, void *pWave, void *pXYZ,
                         int r, int g, int b)
{
    EFFECT_CONT *ec = SetEffectsBegin(0x18);                        /* 627 */

    if (ec == nullptr)                                              /* 557 */
    {
        return nullptr;
    }

    ec->dat.uc8[0] = 0x18;                                          /* 1001 */
    ec->dat.uc8[1] = (u_char)(fl | 0x80);                           /* 1002 */
    ec->dat.uc8[2] = (u_char)type;                                  /* 1003 */
    ec->max = max;                                                  /* 1004 */
    ec->dat.uc8[4] = 0xff;                                          /* 1005 */
    ec->dat.fl32[2] = sclx;                                         /* 1007 */
    ec->dat.fl32[3] = scly;                                         /* 1008 */
    ec->pnt[0] = pWrk;                                              /* 1009 */
    ec->fw[0] = 0.0f;                                               /* 1010 */
    ec->fw[1] = 1.0f;
    ec->in = in;                                                    /* 1014?*/
    ec->keep = keep;                                                /* 1015 */
    ec->out = out;                                                  /* 1016 */
    ec->pnt[1] = pDrive;                                            /* 1017 */
    ec->pnt[2] = pSpeed;                                            /* 1019 */
    ec->pnt[4] = pWave;                                             /* 1020 */
    ec->pnt[5] = pXYZ;                                              /* 1021 */
    ec->r = (u_char)r;                                              /* 1023 */
    ec->g = (u_char)g;                                              /* 1024 */
    ec->b = (u_char)b;                                              /* 1025 */
    ec->cnt = 0;                                                    /* 1026 */
    ec->dat.uc8[5] = 0;

    if ((fl & 4) != 0)                                              /* 1029?*/
    {
        if (ec->in != 0)                                            /* 1030?*/
        {
            ec->flow = 0;
        }
        else if (ec->keep != 0)
        {
            ec->flow = 1;
        }
        else if (ec->out != 0)
        {
            ec->flow = 2;
        }
        else
        {
            ec->flow = 3;
        }
    }

    return ec;                                                      /* 665 */
}

/* (type, alpha, rate, pWrk, in, keep, out) -- id 0x23, which GetEffectContBuf()
 * has no arm for: the request always reports the unknown id and answers null,
 * so an ENEIN can only ever be armed on a record its caller already owns. */
void *SetEffects_ENEIN(int fl, int type, int alpha, float rate, void *pWrk,
                       u_int in, u_int keep, u_int out)
{
    EFFECT_CONT *ec = SetEffectsBegin(0x23);                        /* 649?*/

    if (ec == nullptr)                                              /* 557 */
    {
        return nullptr;
    }

    ec->dat.uc8[0] = 0x23;                                          /* 1044 */
    ec->dat.uc8[1] = (u_char)(fl | 0x80);                           /* 1045 */
    ec->dat.uc8[2] = (u_char)type;                                  /* 1046 */
    ec->dat.uc8[3] = (u_char)alpha;                                 /* 1047 */
    ec->dat.uc8[4] = 0xff;                                          /* 1048 */
    ec->dat.fl32[3] = rate;                                         /* 1049 */
    ec->pnt[0] = pWrk;                                              /* 1050 */
    ec->max = 100;                                                  /* 1051 */
    ec->cnt = 0;                                                    /* 1053?*/
    ec->in = in;                                                    /* 1054 */
    ec->keep = keep;                                                /* 1055 */
    ec->out = out;                                                  /* 1056 */

    if ((fl & 4) != 0)                                              /* 1057 */
    {
        if (ec->in != 0)                                            /* 1058 */
        {
            ec->flow = 0;
        }
        else if (ec->keep != 0)
        {
            ec->flow = 1;
        }
        else if (ec->out != 0)
        {
            ec->flow = 2;
        }
        else
        {
            ec->flow = 3;
        }
    }

    return ec;                                                      /* 665 */
}                                                                   /* 1059 */

/* (pPos) -- id 0x1a.  The position is copied, so the caller's vector does not
 * have to outlive the request. */
void *SetEffects_DUST(int fl, void *pPos)
{
    EFFECT_CONT *ec = SetEffectsBegin(0x1a);                        /* 634 */

    if (ec == nullptr)                                              /* 557 */
    {
        return nullptr;
    }

    ec->dat.uc8[0] = 0x1a;                                          /* 1076 */
    ec->dat.uc8[1] = (u_char)(fl | 0x80);                           /* 1077 */
    ec->dat.uc8[2] = 1;                                             /* 1078 */
    g3dxVu0CopyVector(ec->Pos, (const float *)pPos);
    ec->pnt[0] = ec->Pos;                                           /* 1081 */

    return ec;                                                      /* 665 */
}

/* (pTexture, type, scale, count, uc8[2], uc8[3], uc8[4]) -- id 0x1b */
void *SetEffects_WATERDROP(int fl, void *pTexture, int type, float scale,
                           u_int count, int a, int b, int c)
{
    EFFECT_CONT *ec = SetEffectsBegin(0x1b);                        /* 637 */

    if (ec == nullptr)                                              /* 557 */
    {
        return nullptr;
    }

    ec->dat.uc8[0] = 0x1b;                                          /* 1094 */
    ec->dat.uc8[1] = (u_char)(fl | 0x80);                           /* 1095 */
    ec->pnt[0] = pTexture;                                          /* 1096 */
    ec->dat.uc8[5] = (u_char)type;                                  /* 1097 */
    ec->dat.ui32[3] = 0;                                            /* 1098 */
    ec->dat.fl32[2] = scale;                                        /* 1099 */
    ec->cnt = count;                                                /* 1100 */
    ec->max = ec->cnt;                                              /* 1101 */
    ec->dat.uc8[2] = (u_char)a;                                     /* 1102?*/
    ec->dat.uc8[3] = (u_char)b;                                     /* 1103 */
    ec->dat.uc8[4] = (u_char)c;                                     /* 1104 */
    ec->dat.uc8[6] = 0;                                             /* 1105 */

    return ec;                                                      /* 665 */
}                                                                   /* 1106 */

/* (type, pPos, pMtx, pCol, pSize, alpha, pRate) -- id 0x19.  enemy.c is the
 * only caller: the ghost's model box position, its matrix, and the three
 * per-frame values the aura reads back through pointers. */
void *SetEffects_ENEFIRE(int fl, int type, void *pPos, void *pMtx,
                         void *pCol, void *pSize, u_int alpha, void *pRate)
{
    EFFECT_CONT *ec = SetEffectsBegin(0x19);                        /* 630 */

    if (ec == nullptr)                                              /* 557 */
    {
        return nullptr;
    }

    ec->dat.uc8[0] = 0x19;                                          /* 1120 */
    ec->dat.uc8[1] = (u_char)(fl | 0x80);                           /* 1121 */
    ec->dat.uc8[2] = (u_char)type;                                  /* 1122 */
    ec->pnt[3] = nullptr;                                           /* 1123 */
    ec->pnt[0] = pPos;                                              /* 1124 */
    ec->pnt[4] = pMtx;                                              /* 1125 */
    ec->pnt[1] = pCol;                                              /* 1126 */
    ec->pnt[2] = pSize;                                             /* 1127 */
    ec->dat.ui32[3] = alpha;                                        /* 1128 */
    ec->pnt[5] = pRate;                                             /* 1129 */

    return ec;                                                      /* 665 */
}

/* (type, pPos, pAlpha, pScale) -- id 0x16 */
void *SetEffects_TORCH(int fl, int type, void *pPos,
                       void *pAlpha, void *pScale)
{
    EFFECT_CONT *ec = SetEffectsBegin(0x16);                        /* 624 */

    if (ec == nullptr)                                              /* 557 */
    {
        return nullptr;
    }

    ec->dat.uc8[0] = 0x16;                                          /* 1143 */
    ec->dat.uc8[1] = (u_char)(fl | 0x80);                           /* 1144 */
    ec->dat.uc8[2] = (u_char)type;                                  /* 1145 */
    ec->dat.uc8[3] = 0;                                             /* 1146 */
    ec->pnt[1] = nullptr;                                           /* 1147 */
    ec->pnt[0] = pPos;                                              /* 1148 */
    ec->pnt[2] = pAlpha;                                            /* 1149 */
    ec->pnt[3] = pScale;                                            /* 1150 */

    return ec;                                                      /* 665 */
}

/* (type, pPos, depth) -- id 0x1c */
void *SetEffects_TORCH2(int fl, int type, void *pPos, int depth)
{
    EFFECT_CONT *ec = SetEffectsBegin(0x1c);                        /* 640 */

    if (ec == nullptr)                                              /* 557 */
    {
        return nullptr;
    }

    ec->dat.uc8[0] = 0x1c;                                          /* 1164 */
    ec->dat.uc8[1] = (u_char)(fl | 0x80);                           /* 1165 */
    ec->dat.uc8[2] = (u_char)type;                                  /* 1166 */
    ec->dat.uc8[3] = 0;                                             /* 1167 */
    ec->pnt[1] = nullptr;                                           /* 1168 */
    ec->pnt[0] = pPos;                                              /* 1169 */
    ec->dat.uc8[4] = (u_char)depth;                                 /* 1170 */

    return ec;                                                      /* 665 */
}

/* (pPos, type, fw[0], fw[1]) -- id 0x1d */
void *SetEffects_SPARK(int fl, void *pPos, int type, float fw0, float fw1)
{
    EFFECT_CONT *ec = SetEffectsBegin(0x1d);                        /* 643 */

    if (ec == nullptr)                                              /* 557 */
    {
        return nullptr;
    }

    ec->dat.uc8[0] = 0x1d;                                          /* 1183 */
    ec->dat.uc8[1] = (u_char)(fl | 0x80);                           /* 1186 */
    ec->dat.uc8[3] = 0;                                             /* 1187 */
    g3dxVu0CopyVector(ec->Pos, (const float *)pPos);                /* 1188 */
    ec->pnt[0] = ec->Pos;                                           /* 1189 */
    ec->dat.uc8[2] = (u_char)type;                                  /* 1191 */
    ec->pnt[1] = nullptr;                                           /* 1192 */
    ec->fw[0] = fw0;
    ec->fw[1] = fw1;

    return ec;                                                      /* 665 */
}                                                                   /* 1193 */

/* (pPos, r, g, b, fw[0], fw[1], alpha) -- id 0x1e */
void *SetEffects_TORCH_FREA(int fl, void *pPos, int r, int g, int b,
                            float fw0, float fw1, int alpha)
{
    EFFECT_CONT *ec = SetEffectsBegin(0x1e);                        /* 646 */

    if (ec == nullptr)                                              /* 557 */
    {
        return nullptr;
    }

    ec->dat.uc8[0] = 0x1e;                                          /* 1208 */
    ec->dat.uc8[1] = (u_char)(fl | 0x80);                           /* 1211 */
    g3dxVu0CopyVector(ec->Pos, (const float *)pPos);                /* 1212 */
    ec->pnt[0] = ec->Pos;                                           /* 1213 */
    ec->r = (u_char)r;                                              /* 1215 */
    ec->g = (u_char)g;                                              /* 1216 */
    ec->b = (u_char)b;                                              /* 1217 */
    ec->fw[0] = fw0;                                                /* 1218 */
    ec->fw[1] = fw1;
    ec->a = (u_char)alpha;                                          /* 1221 */

    return ec;                                                      /* 665 */
}

/* (pPos, pRot, ui32[1], mode, pAlpha) -- id 0x21.  Mode 0 is a free-standing
 * haze at a copied position; non-zero hangs it on the caller's own vectors. */
void *SetEffects_HAZE(int fl, void *pPos, void *pRot, u_int Id,
                      u_int mode, void *pAlpha)
{
    EFFECT_CONT *ec = SetEffectsBegin(0x21);

    if (ec == nullptr)                                              /* 557 */
    {
        return nullptr;
    }

    ec->dat.uc8[0] = 0x21;                                          /* 1238 */
    ec->dat.uc8[1] = (u_char)(fl | 0x80);                           /* 1239 */
    ec->dat.ui32[1] = Id;                                           /* 1242 */
    ec->pnt[1] = nullptr;                                           /* 1243 */
    ec->dat.ui32[2] = mode;                                         /* 1244 */
    ec->pnt[3] = pAlpha;                                            /* 1245 */

    if (ec->dat.ui32[2] == 0)                                       /* 1246 */
    {
        g3dxVu0CopyVector(ec->Pos, (const float *)pPos);            /* 1248 */
        ec->pnt[0] = ec->Pos;
        ec->pnt[2] = nullptr;
    }
    else
    {
        ec->pnt[0] = pPos;
        ec->pnt[2] = pRot;
    }

    return ec;                                                      /* 665 */
}                                                                   /* 1253 */

/* (pPosTex, pParam[3], ReqFlg) -- id 0x1f */
void *SetEffects_CAMERA_FLASH(int fl, void *pPosTex, const float *pParam,
                              int ReqFlg)
{
    EFFECT_CONT *ec = SetEffectsBegin(0x1f);

    if (ec == nullptr)                                              /* 557 */
    {
        return nullptr;
    }

    ec->dat.uc8[0] = 0x1f;                                          /* 1270 */
    ec->dat.uc8[1] = (u_char)(fl | 0x80);                           /* 1275 */
    g3dxVu0CopyVector(ec->Pos, (const float *)pPosTex);             /* 1279 */
    ec->pnt[0] = ec->Pos;                                           /* 1281 */
    ec->fw[0] = pParam[0];                                          /* 1286?*/
    ec->fw[1] = pParam[1];                                          /* 1287 */
    ec->fw[2] = pParam[2];
    ec->flow = (ReqFlg == 0);
    ec->cnt = 0;                                                    /* 1291 */

    return ec;                                                      /* 665 */
}

/* (pPos, pnt[2], ui32[1], ui32[2]) -- id 0x20 */
void *SetEffects_MANY_CANDLE(int fl, void *pPos, void *pData,
                             u_int DataNum, u_int Id)
{
    EFFECT_CONT *ec = SetEffectsBegin(0x20);

    if (ec == nullptr)                                              /* 557 */
    {
        return nullptr;
    }

    ec->dat.uc8[0] = 0x20;                                          /* 1308 */
    ec->dat.uc8[1] = (u_char)(fl | 0x80);                           /* 1309 */
    ec->pnt[2] = pData;                                             /* 1311 */
    ec->dat.ui32[1] = DataNum;                                      /* 1312 */
    ec->dat.ui32[2] = Id;                                           /* 1313 */
    g3dxVu0CopyVector(ec->Pos, (const float *)pPos);
    ec->pnt[0] = ec->Pos;                                           /* 1315 */
    ec->pnt[1] = nullptr;                                           /* 1316 */

    return ec;                                                      /* 665 */
}

/* (pPos, size) -- id 0x22.  fw keeps a private copy of the position on top of
 * Pos. */
void *SetEffects_DOOR_SEAL(int fl, void *pPos, float size)
{
    EFFECT_CONT *ec = SetEffectsBegin(0x22);

    if (ec == nullptr)                                              /* 557 */
    {
        return nullptr;
    }

    ec->dat.uc8[0] = 0x22;                                          /* 1329 */
    ec->dat.uc8[1] = (u_char)(fl | 0x80);                           /* 1332 */
    ec->dat.fl32[1] = size;                                         /* 1333 */
    g3dxVu0CopyVector(ec->Pos, (const float *)pPos);
    ec->pnt[0] = ec->Pos;                                           /* 1335 */
    ec->pnt[1] = nullptr;
    ec->fw[0] = ec->Pos[0];                                         /* 1337 */
    ec->fw[1] = ec->Pos[1];
    ec->fw[2] = ec->Pos[2];

    return ec;                                                      /* 665 */
}                                                                   /* 1338 */

static void EffectContClear(EFFECT_CONT *pEffCont)
{
    int i;

    if (pEffCont != nullptr)                                        /* 1351 */
    {
        /* The memset covers the pointers already; the ROM nulls them again
         * (backwards) anyway. */
        memset(pEffCont, 0, sizeof(EFFECT_CONT));                   /* 1352 */
        for (i = 5; i >= 0; i--)                                    /* 1353 */
        {
            pEffCont->pnt[i] = nullptr;                             /* 1354 */
        }
    }
}                                                                   /* 1355 */

void ResetEffects(void *p)
{
    if (p != nullptr)                                               /* 1366 */
    {
        EffectContClear((EFFECT_CONT *)p);                          /* 1368 */
        ((EFFECT_CONT *)p)->dat.uc8[0] = 0;                         /* 1369 */
    }
}

/* First free slot in the live modeled bank, -1 when full. */
static int SearchEmptyEffectBuf(void)
{
    EFFECT_CONT *ecm;
    int          fl;
    int          i;

    fl = -1;                                                        /* 1377 */

    if (EffWrkEffectBankGet() == 0)                                 /* 1381 */
    {
        ecm = &efcntm_cnt[0];
    }
    else
    {
        ecm = &efcntm[0];
    }

    for (i = 0; i < 48; i++)                                        /* 1383 */
    {
        if (ecm->dat.uc8[0] == 0)                                   /* 1385 */
        {
            fl = i;
        }
        if (fl != -1)
        {
            break;
        }
        ecm++;
    }                                                               /* 1388 */

    return fl;                                                      /* 1389 */
}

/* --------------------------------------------------------------------------
 *  Depth-sort the live modeled bank and draw it back to front.  A record
 *  whose flags lack bit 7 has no world position and sorts to the very front.
 *  Door seals draw first regardless, dust always last; the PDEFORM/ENEIN
 *  pair can be deferred to the blur pass by EffectObjPartsDeformBlurCheck().
 * ------------------------------------------------------------------------ */
static void EffectZSort(int PDeformBlurFlg)
{
    static fixed_array<int, 48> efzsort;                    /* bss 4232b0 */

    sceVu0IVECTOR ivec;
    float         vpos[4];
    float         wpos[4];
    float         wlm[4][4];
    float         slm[4][4];
    EFFECT_CONT  *ecm;
    EFFECT_CONT  *ec;
    GRA3DCAMERA  *pCam;
    int           i;
    int           j;
    int           n;
    int           num;

    memset(wpos, 0, sizeof(wpos));                                  /* 1398 */
    wpos[3] = 1.0f;

    pCam = gra3dGetCamera();                                        /* 1406 */

    if (EffWrkEffectBankGet() == 0)                                 /* 1407 */
    {
        ecm = &efcntm_cnt[0];                                       /* 1409 */
    }
    else
    {
        ecm = &efcntm[0];
    }

    num = 0;                                                        /* 1412 */
    for (i = 0; i < 48; i++)                                        /* 1414 */
    {
        ec = &ecm[i];
        if (ec->dat.uc8[0] != 0)                                    /* 1415 */
        {
            if ((ec->dat.uc8[1] & 0x80) == 0)                       /* 1418 */
            {
                ec->z = 0;                                          /* 1419 */
            }
            else
            {
                g3dxVu0CopyVector(vpos, (const float *)ec->pnt[0]); /* 1420 */
                sceVu0UnitMatrix(wlm);                              /* 1421 */
                wlm[0][0] = wlm[1][1] = wlm[2][2] = 25.0f;          /* 1422 */
                sceVu0TransMatrix(wlm, wlm, vpos);                  /* 1423 */
                sceVu0MulMatrix(slm, pCam->matWorldScreen, wlm);
                sceVu0RotTransPers(ivec, slm, wpos, 0);
                ec->z = ivec[2];                                    /* 1429?*/
            }
            efzsort[num] = i;                                       /* 1432 */
            num++;                                                  /* 1433 */
        }
    }

    for (i = 0; i < num - 1; i++)                                   /* 1439 */
    {
        for (j = i + 1; j < num; j++)                               /* 1440 */
        {
            if (ecm[efzsort[j]].z < ecm[efzsort[i]].z)              /* 1443 */
            {
                n = efzsort[j];                                     /* 1445 */
                efzsort[j] = efzsort[i];
                efzsort[i] = n;                                     /* 1447 */
            }
        }
    }

    for (i = 0; i < num; i++)                                       /* 1450 */
    {
        if (ecm[efzsort[i]].dat.uc8[0] == 0x22)                     /* 1452 */
        {
            SetDoorSeal(&ecm[efzsort[i]]);
        }
    }

    for (i = 0; i < num; i++)                                       /* 1454 */
    {
        ec = &ecm[efzsort[i]];
        switch (ec->dat.uc8[0])                                     /* 1456 */
        {
        case 0x12:
            SetHalo(ec);
            break;
        case 0x15:
            SetFire(ec);
            break;
        case 0x16:
            SetTorch(ec);
            break;
        case 0x18:
        case 0x23:
            if (PDeformBlurFlg != 0 ||
                EffectObjPartsDeformBlurCheck(ec) == 0)
            {
                SetPartsDeform(ec);
            }
            break;
        case 0x1c:
            SetTorch2(ec);
            break;
        case 0x1d:
            SetSpark(ec);
            break;
        case 0x1e:
            SetTorch2BigFrea(ec);
            break;
        case 0x1f:
            SetCameraFlash(ec);
            break;
        case 0x20:
            SetManyCandle(ec);
            break;
        case 0x21:
            SetHaze(ec);
            break;
        default:
            break;
        }
    }                                                               /* 1457?*/

    for (i = 0; i < num; i++)                                       /* 1459 */
    {
        if (ecm[efzsort[i]].dat.uc8[0] == 0x1a)                     /* 1460 */
        {
            SetDust(&ecm[efzsort[i]]);
        }
    }
}

/* The blur pass's own sort: only the enemy fire shells draw here. */
static void EffectZSort2(void)
{
    static fixed_array<int, 48> efzsort;                    /* bss 423370 */

    sceVu0IVECTOR ivec;
    float         vpos[4];
    float         wpos[4];
    float         wlm[4][4];
    float         slm[4][4];
    EFFECT_CONT  *ecm;
    EFFECT_CONT  *ec;
    GRA3DCAMERA  *pCam;
    int           i;
    int           j;
    int           n;
    int           num;

    memset(wpos, 0, sizeof(wpos));                                  /* 1511 */
    wpos[3] = 1.0f;

    pCam = gra3dGetCamera();                                        /* 1519 */

    if (EffWrkEffectBankGet() == 0)                                 /* 1520 */
    {
        ecm = &efcntm_cnt[0];                                       /* 1522 */
    }
    else
    {
        ecm = &efcntm[0];
    }

    num = 0;                                                        /* 1525 */
    for (i = 0; i < 48; i++)                                        /* 1527 */
    {
        ec = &ecm[i];
        if (ec->dat.uc8[0] != 0)                                    /* 1528 */
        {
            if ((ec->dat.uc8[1] & 0x80) == 0)                       /* 1531 */
            {
                ec->z = 0;                                          /* 1532 */
            }
            else
            {
                g3dxVu0CopyVector(vpos, (const float *)ec->pnt[0]); /* 1533 */
                sceVu0UnitMatrix(wlm);                              /* 1534 */
                wlm[0][0] = wlm[1][1] = wlm[2][2] = 25.0f;          /* 1535 */
                sceVu0TransMatrix(wlm, wlm, vpos);                  /* 1536 */
                sceVu0MulMatrix(slm, pCam->matWorldScreen, wlm);
                sceVu0RotTransPers(ivec, slm, wpos, 0);
                ec->z = ivec[2];
            }
            efzsort[num] = i;                                       /* 1542 */
            num++;                                                  /* 1545?*/
        }
    }

    for (i = 0; i < num - 1; i++)                                   /* 1546 */
    {
        for (j = i + 1; j < num; j++)                               /* 1552?*/
        {
            if (ecm[efzsort[j]].z < ecm[efzsort[i]].z)              /* 1553 */
            {
                n = efzsort[j];                                     /* 1556 */
                efzsort[j] = efzsort[i];
                efzsort[i] = n;
            }
        }
    }

    for (i = 0; i < num; i++)                                       /* 1558 */
    {
        if (ecm[efzsort[i]].dat.uc8[0] == 0x19)                     /* 1560 */
        {
            SetEneFire(&ecm[efzsort[i]]);
        }
    }
}                                                                   /* 1563 */

/* The 3D pass shared by the ordinary frame and the photo phase. */
static void EffectInPhoto(EFFECT_CONT *ecm, int PDeformBlurFlg)
{
    RunLeaf();                                                      /* 1573 */
    EffectTorch2BigFreaMain();                                      /* 1574 */
    RDPFireMoveCtrl();                                              /* 1575 */
    EffectZSort(PDeformBlurFlg);                                    /* 1577 */

    if (ecm[10].dat.uc8[0] == 10)                                   /* 1580 */
    {
        SetRenzFlare(&ecm[10]);                                     /* 1582 */
    }

    EffectButterflyMain();                                          /* 1583 */
    EffectButterflyParticleMain();                                  /* 1584 */
    EffectSprayAllDraw();                                           /* 1585 */
    EffectRainDraw();                                               /* 1586 */
    EffectDropOfWaterDraw();                                        /* 1587 */
    EffectLeavesFallExec();                                         /* 1588 */
    EffectWaterFlowExec();                                          /* 1589 */
    EffectThunderLightExec();                                       /* 1590 */
    EffectModelAlphaChangeExec();
}

/* --------------------------------------------------------------------------
 *  The per-frame effect pass, keyed by where in the frame graph the caller
 *  sits: 5 is the main 3D + screen-filter pass, 7 the blur-source pass,
 *  8 the very last 2D overlays.
 * ------------------------------------------------------------------------ */
void EffectControl(int no)
{
    EFFECT_CONT *ecm;

    if (GetEffDrawFLG() == 0)                                       /* 1601 */
    {
        return;
    }

    if (EffWrkEffectBankGet() == 0)                                 /* 1612 */
    {
        ecm = &efcnt_cnt[0];                                        /* 1614 */
    }
    else
    {
        ecm = &efcnt[0];
    }

    switch (no)
    {
    case 5:
        if (EffWrkStopFlgGet() == 0)                                /* 1652 */
        {
            EffectInPhoto(ecm, 1);                                  /* 1654 */
            EneDmgMain();                                           /* 1657 */
            EneHitEffectMain();                                     /* 1658 */
            EffectEndParticleMain();                                /* 1659 */

            if (ecm[1].dat.uc8[0] == 1)                             /* 1664 */
            {
                SetForcusDepth(&ecm[1]);
            }
            if (ecm[3].dat.uc8[0] == 3)                             /* 1667 */
            {
                SetBlur(&ecm[3]);                                   /* 1668 */
            }
            if (ecm[4].dat.uc8[0] == 4)                             /* 1669 */
            {
                SetBlur(&ecm[4]);                                   /* 1670 */
            }
            if (ecm[5].dat.uc8[0] == 5)                             /* 1673 */
            {
                SetBlur(&ecm[5]);                                   /* 1674 */
            }
            RunBlur(&ecm[3]);                                       /* 1678 */

            if (EffWrkStopFlgGet() == 0)                            /* 1681 */
            {
                /* Stash this frame's picture for next frame's overlap. */
                LocalCopyLtoL(1, (sys_wrk.count & 1) * 0x1180, 0x3aa0); /* 1684 */
            }

            if (ecm[6].dat.uc8[0] == 6)                             /* 1687 */
            {
                SetDeform(&ecm[6]);                                 /* 1690 */
            }
            if (ecm[13].dat.uc8[0] == 0xd)                          /* 1691 */
            {
                SetContrast2(&ecm[13]);                             /* 1694 */
            }
            if (ecm[12].dat.uc8[0] == 0xc)                          /* 1697 */
            {
                SetNega(&ecm[12]);                                  /* 1700 */
            }
            if (ecm[15].dat.uc8[0] == 0xf)                          /* 1702 */
            {
                SetContrast3(&ecm[15]);
            }
            if (ecm[7].dat.uc8[0] == 7)                             /* 1707?*/
            {
                SetFocus(&ecm[7]);
            }
            RunFocus(&ecm[7]);                                      /* 1708?*/

            if (ecm[2].dat.uc8[0] == 2)
            {
                SetDither3(&ecm[2]);
            }
            if (ecm[11].dat.uc8[0] == 0xb)
            {
                SetBlackFilter(&ecm[11]);
            }
            if (ecm[14].dat.uc8[0] == 0xe)
            {
                SetContrast2(&ecm[14]);
            }
        }
        break;

    case 7:
        SetEneDmgEffect1_Sub();                                     /* 1707 */
        SetEneDmgEffect2_Sub();                                     /* 1708 */
        EffectZSort2();                                             /* 1710 */
        break;                                                      /* 1712 */

    case 8:
        if (ecm[9].dat.uc8[0] == 9)                                 /* 1718 */
        {
            SetFadeFrame(&ecm[9]);
        }
        if (ecm[8].dat.uc8[0] == 8)                                 /* 1721 */
        {
            SetOverRap(&ecm[8]);                                    /* 1724 */
        }
        ScreenCtrl();                                               /* 1741 */
        CamSave();
        break;

    default:
        break;
    }
}                                                                   /* 1747 */

void SetBlurOff(void)
{
    EffWrkBlurOffSet(1);                                            /* 1753 */
}

void SetDebugMenuSwitch(int sw)
{
    look_debugmenu = sw % 2;                                        /* 1760 */
}

int GetDebugMenuSwitch(void)
{
    return look_debugmenu;                                          /* 1767 */
}

int EffWrkDispFlgGet(void)
{
    return eff_wrk.disp_flg;                                        /* 1775 */
}

void EffWrkDispFlgSet(int flg)
{
    if (flg != 0)                                                   /* 1783 */
    {
        eff_wrk.disp_flg = 1;                                       /* 1784 */
    }
    else
    {
        eff_wrk.disp_flg = 0;
    }
}                                                                   /* 1787 */

int EffWrkMonochroModeGet(void)
{
    return eff_wrk.monochro_mode;                                   /* 1796 */
}

void EffWrkMonochroModeSet(int flg)
{
    if (flg != 0)                                                   /* 1804 */
    {
        eff_wrk.monochro_mode = 1;                                  /* 1805 */
    }
    else
    {
        eff_wrk.monochro_mode = 0;
    }
}                                                                   /* 1808 */

int EffWrkStopFlgGet(void)
{
    return eff_wrk.stop_flg;                                        /* 1817 */
}

void EffWrkStopFlgSet(int flg)
{
    if (flg != 0)                                                   /* 1824 */
    {
        eff_wrk.stop_flg = 1;                                       /* 1825 */
    }
    else
    {
        eff_wrk.stop_flg = 0;
    }
}                                                                   /* 1828 */

int EffWrkEffectBankGet(void)
{
    return eff_wrk.change_bank;                                     /* 1837 */
}

void EffWrkEffectBankSet(int flg)
{
    if (flg != 0)                                                   /* 1845 */
    {
        eff_wrk.change_bank = 1;                                    /* 1846 */
    }
    else
    {
        eff_wrk.change_bank = 0;
    }
}                                                                   /* 1849 */

int EffWrkBlurOffGet(void)
{
    return eff_wrk.blur_off;                                        /* 1858 */
}

void EffWrkBlurOffSet(int flg)
{
    if (flg != 0)                                                   /* 1865 */
    {
        eff_wrk.blur_off = 1;                                       /* 1866 */
    }
    else
    {
        eff_wrk.blur_off = 0;
    }
}                                                                   /* 1869 */

int EffWrkDithOffGet(void)
{
    return eff_wrk.dith_off;                                        /* 1878 */
}

void EffWrkDithOffSet(int flg)
{
    if (flg != 0)                                                   /* 1885 */
    {
        eff_wrk.dith_off = 1;                                       /* 1886 */
    }
    else
    {
        eff_wrk.dith_off = 0;
    }
}                                                                   /* 1889 */

int EffWrkFilamentOffGet(void)
{
    return eff_wrk.filament_off;                                    /* 1898 */
}

void EffWrkFilamentOffSet(int flg)
{
    if (flg != 0)                                                   /* 1905 */
    {
        eff_wrk.filament_off = 1;                                   /* 1906 */
    }
    else
    {
        eff_wrk.filament_off = 0;
    }
}                                                                   /* 1909 */

/* Halve a 32-bit image in place: every other pixel of every row, packed
 * to the front.  (Every row, not every other row -- the height stays.) */
void EffImageHalf32(u_int *pImage, u_int Width, u_int Height)
{
    u_int *pSrcImage;
    u_int *pDstImage;
    u_int  i;
    u_int  j;

    pSrcImage = pImage + 1;                                         /* 1930 */
    pDstImage = pImage;                                             /* 1931 */

    for (i = 0; i < Height; i++)                                    /* 1932 */
    {
        for (j = 0; j < Width / 2; j++)                             /* 1933 */
        {
            *pDstImage = *pSrcImage;                                /* 1935 */
            pDstImage += 1;                                         /* 1936 */
            pSrcImage += 2;
        }
    }
}                                                                   /* 1937 */

void EffScreenEffectStatusSet(int Status)
{
    ScreenEffectStatus = Status;                                    /* 1947 */
}

/* Steps one record's in/keep/out envelope; flow 3 is "over", 4 "reaped". */
void EffInKeepOutFlowCtrl(EFFECT_CONT *ec)
{
    if (EffWrkStopFlgGet() == 0 && ec->flow < 3)                    /* 1956 */
    {
        ec->cnt++;                                                  /* 1959 */
    }

    switch (ec->flow)                                               /* 1961 */
    {
    case 0:
        if (ec->in <= ec->cnt)                                      /* 1962 */
        {
            if (ec->keep != 0)                                      /* 1965 */
            {
                ec->flow = 1;
            }
            else if (ec->out != 0)                                  /* 1967 */
            {
                ec->flow = 2;
            }
            else
            {
                ec->flow = 3;                                       /* 1968 */
            }
            ec->cnt = 0;                                            /* 1969 */
        }
        break;
    case 1:
        if (ec->keep <= ec->cnt)                                    /* 1971 */
        {
            if (ec->out != 0)                                       /* 1973 */
            {
                ec->flow = 2;
            }
            else
            {
                ec->flow = 3;
            }
            ec->cnt = 0;                                            /* 1974 */
        }
        break;
    case 2:
        if (ec->out <= ec->cnt)                                     /* 1976 */
        {
            ec->flow = 3;
            ec->cnt = 0;
        }
        break;
    case 3:
        ec->flow = 4;                                               /* 1980 */
        break;
    case 4:
        break;
    default:
        break;
    }
}                                                                   /* 1987 */

/* A player who moved this frame makes every candle flame nearby gutter. */
void EffectCandleFlameYuramekiCtrl(float *PlayerNowPos, float *PlayerOldPos)
{
    EFFECT_CONT *pEffCont;
    float        TmpVector[4];
    float        Speed;
    int          i;

    sceVu0SubVector(TmpVector, PlayerNowPos, PlayerOldPos);         /* 2023?*/
    sceVu0MulVector(TmpVector, TmpVector, TmpVector);               /* 2029 */
    Speed = TmpVector[0] + TmpVector[1] + TmpVector[2];             /* 2030 */

    if (Speed >= 400.0f)                                            /* 2031 */
    {
        if (EffWrkEffectBankGet() == 0)                             /* 2033 */
        {
            pEffCont = &efcntm_cnt[0];                              /* 2035 */
        }
        else
        {
            pEffCont = &efcntm[0];
        }

        for (i = 0; i < 48; i++)                                    /* 2037 */
        {
            if (pEffCont->dat.uc8[0] == 0x15)                       /* 2038 */
            {
                EffOthCandleFlameYuramekiReq(pEffCont, PlayerNowPos); /* 2039 */
            }
            pEffCont++;                                             /* 2041 */
        }
    }
}                                                                   /* 2042 */

/* The photo phase's own effect pass: the 3D set, without the screen work. */
void EffectPhotoPhase(void)
{
    EFFECT_CONT *ecm;

    if (EffWrkEffectBankGet() == 0)                                 /* 2051 */
    {
        ecm = &efcntm_cnt[0];
    }
    else
    {
        ecm = &efcntm[0];
    }

    EffectInPhoto(ecm, 0);                                          /* 2053 */
}

SCREEN_EFFECT_PARAMETER *EffectGetScreenEffectParamPtr(int EffectNo)
{
    if (EffectNo > 5)                                               /* 2060 */
    {
        EffectNo = 0;
    }

    return pScreenEffectParamPtr[EffectNo];                         /* 2061 */
}

SCREEN_EFFECT_PARAMETER *EffectGetNowScreenEffectParamPtr(void)
{
    return pScreenEffectParamPtr[ScreenEffectNo];                   /* 2070 */
}

void EffectSetScreenEffectNo(int EffectNo)
{
    if (EffectNo < 6)                                               /* 2092 */
    {
        ScreenEffectNo = EffectNo;                                  /* 2093 */
    }
}

int EffectGetScreenEffectNo(void)
{
    return ScreenEffectNo;                                          /* 2102 */
}

/* --------------------------------------------------------------------------
 *  Effect heap accessors.  Every effect-owned allocation goes through these;
 *  the 6 is the alignment bit, i.e. 64-byte alignment.
 * ------------------------------------------------------------------------ */
void *EFFECT_MALLOC(int size)
{
    return my_mallocMalloc(&EffectMallocWrk, size, 6);              /* 2110 */
}

void EFFECT_FREE(void *block)
{
    my_mallocFree(&EffectMallocWrk, block);                         /* 2118 */
}

/* Is the handle a SetEffects_* request returned still that same live effect? */
int EffectExecCheck(void *pEffRet, int EffectType)
{
    EFFECT_CONT *ecm;
    int          Ret;
    int          i;

    Ret = 0;                                                        /* 2125 */

    if (EffWrkEffectBankGet() == 0)                                 /* 2128 */
    {
        ecm = &efcntm_cnt[0];                                       /* 2130 */
    }
    else
    {
        ecm = &efcntm[0];
    }

    for (i = 0; i < 48; i++)                                        /* 2132 */
    {
        if (ecm->dat.uc8[0] == EffectType && ecm == pEffRet)        /* 2134 */
        {
            Ret = 1;                                                /* 2137 */
        }
        ecm++;
    }

    return Ret;                                                     /* 2138 */
}
