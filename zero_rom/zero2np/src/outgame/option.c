// FILE: /home/zero_rom/zero2np/src/outgame/option.c
//
// The option screen.  Four pages driven by a pair of dispatch tables --
// the main list, the operate sub-page, the button-config sub-page and the
// brightness slider -- plus two modal windows (initialise-confirm and the
// system-file save) that suspend the page machinery entirely.
//
// Edits go into `optm`, a working copy of `opt_wrk`; OptionEnd() commits it
// only if something actually changed.  The exceptions are the three settings
// that have to take effect while you are looking at them -- sound output,
// volume and vibration -- which are pushed at the driver from the pad handler
// as well.
//
// Reconstructed from the Feb 6 2004 prototype (SLES_523.84).
// Trailing /* NNN */ are ROM source line numbers, measured from
// disassemble_function.

#include "option.h"

#include "../common/ol_load.h"              // ol_loadGetHeap / ol_loadFreeHeap
#include "../common/variable.h"             // pad[] / opt_wrk
#include "../graphics/graph2d/tim2.h"       // PK2SendVram
#include "../main/gphase.h"                 // SetNextGPhase / GID_TITLE_MENU
#include "../save_load/prg/system_data_save.h"  // SystemDataSave*
#include "../system/eeiop/cddat.h"          // OUTGAME_PK2 / GetFileSize
#include "../system/eeiop/fileload.h"       // FileLoadReqEE / IsEnd2 / Cancel2
#include "../system/eeiop/snd.h"            // SndSetMono / Stereo / GroupVolume
#include "../system/os/system.h"            // GetLanguage / SystemBankPlay
#include "../system/pad/pad.h"              // paddat / GetPadAnalogRpt

#include <stdint.h>                         // uintptr_t
#include <stdio.h>                          // printf

/* Per-language option paks. */
#define OPT_TOP_PK2     0x1110              /* + language */
#define OPT_BRN_PK2     0x1115
#define OPT_KEY_PK2     0x1116              /* + language */

/* Frames the pad buzzes for when vibration is switched on. */
#define OPT_VIB_TIME    20

static void OptionMainPad(void);
static void OptionOperatePad(void);
static void OptionBrightnessSetup(void);
static void OptionButtonSetup(void);
static void OptVerify(void);
static int  GetOptWrkChgValue(void);
static int  ChangeValue(int val, int ofs, int max, int min);
static u_char ReplaceONOFF(u_char src);
static void GetOptionTexMem(void **tex_addr, int data_label);
static void OptionTexLoadReq(void *tex_addr, int data_label);
static int  OptionTexLoadWait(void);
static void LiberateOptionTexMem(void **tex_addr);
static void OptionTexLoadCancel(void *tex_addr, int data_label);

/* Note the order: index 2 is the button page and 3 the brightness slider,
 * which is the reverse of the order the functions are defined in. */
static void (*OptionCtrlModule[4])(void) =                              /* data 33a090 */
{
    OptionMainPad,
    OptionOperatePad,
    OptionButtonSetup,
    OptionBrightnessSetup,
};

static void (*OptionDispModule[4])(void) =                              /* data 33a0a0 */
{
    OptionMainDisp,
    OptionOperateDisp,
    OptionButtonSetupDisp,
    OptionBrightnessDisp,
};

OPT_CTRL *oc;                                                           /* sdata 3f3638 */
void *opt_og_tex_addr;                                                  /* sdata 3f363c */
void *opt_top_tex_addr;                                                 /* sdata 3f3640 */
void *opt_brn_tex_addr;                                                 /* sdata 3f3644 */
void *opt_key_tex_addr;                                                 /* sdata 3f3648 */

OPT_CTRL opt_ctrl;                                                      /* data 33a0b0 */
OPTION_WRK optm;                                                        /* data 33a0f0 */

/* The factory settings.  Also what the initialise-confirm window restores. */
void InitOptionSetup(OPTION_WRK *ow)
{
    ow->brightness = 0x80;                                              /* 113 */
    ow->snd_volume = 0x100;                                             /* 114 */
    ow->pad_vib = '\x01';                                               /* 115 */
    ow->pad_type = '\0';                                                /* 116 */
    ow->view_vertical = '\0';                                           /* 117 */
    ow->ana_replace = '\0';                                             /* 118 */
    ow->credits = '\x01';                                               /* 119 */
    ow->snd_output = '\0';                                              /* 120 */
    ow->move_operate = '\0';                                            /* 122 */

    OptSoundSetupRef(ow);                                               /* 123 */
}

/* `mode` is 0 from the title and non-zero from the pause menu.  It only
 * matters at the very end, and there both branches do the same thing -- see
 * OptionMain(). */
void OptionInit(int mode)
{
    optm = opt_wrk;                                                     /* 130 */

    oc = &opt_ctrl;

    opt_ctrl.mode = mode;                                               /* 131 */
    opt_ctrl.now_place = 0;                                             /* 132 */
    opt_ctrl.next_place = 0;                                            /* 133 */
    opt_ctrl.now_tex = 4;                                               /* 134 */
    opt_ctrl.main_step = 0;                                             /* 135 */
    opt_ctrl.anm_step = 1;                                              /* 136 */
    opt_ctrl.anm_alpha = 0;                                             /* 137 */
    opt_ctrl.cursor = 0;                                                /* 138 */
    opt_ctrl.next_csr = 0;                                              /* 139 */
    opt_ctrl.old_csr = 0;                                               /* 140 */
    opt_ctrl.old_csr2 = 0;                                              /* 141 */
    opt_ctrl.window = 0;                                                /* 142 */
    opt_ctrl.yn_csr = 1;                                                /* 143 */
    opt_ctrl.save_step = 0;                                             /* 145 */
    opt_ctrl.vib_time = 0;                                              /* 146 */
}

/* main_step 4 is the modal state: the page's own pad handler does not run and
 * either the confirm window or the save screen owns the frame.
 *
 * The brightness page (place 3) is the one page drawn without the background
 * mask, so the setting can be judged against the real screen. */
void OptionMain(void)
{
    int end_flg = 0;                                                    /* 168 */

    if (oc->main_step == 0)                                             /* 170 */
    {
        if (OptionTexLoadWait() != 0)                                   /* 172 */
        {
            oc->main_step = 1;                                          /* 174 */
            oc->anm_step = 1;
        }
    }
    else if (oc->main_step == 1)                                        /* 177 */
    {
        if (oc->anm_step == 0)                                          /* 178 */
        {
            oc->anm_alpha = 0x80;                                       /* 179 */
            (*OptionCtrlModule[oc->now_place])();                       /* 181 */
        }
    }
    else if (oc->main_step == 4)                                        /* 183 */
    {
        if (oc->window == 1)                                            /* 184 */
        {
            OptVerify();                                                /* 185 */
        }
        else if (oc->window == 2)                                       /* 186 */
        {
            if (oc->save_step == 0)                                     /* 187 */
            {
                SystemDataSaveInit(ol_loadGetHeap, ol_loadFreeHeap);    /* 188 */
                oc->save_step = 1;                                      /* 189 */
            }
            else if (oc->save_step == 1)                                /* 190 */
            {
                int save_mode = SystemDataSaveMain();                   /* 191 */

                if (save_mode > 0)                                      /* 192 */
                {
                    SystemDataSaveEnd();                                /* 193 */
                    oc->main_step = 1;                                  /* 194 */
                    oc->anm_step = 2;                                   /* 195 */
                    oc->next_place = 4;                                 /* 196 */
                    oc->save_step = 0;                                  /* 197 */
                }
                else if (save_mode < 0)                                 /* 198 */
                {
                    SystemDataSaveEnd();                                /* 199 */
                    oc->main_step = 1;                                  /* 200 */
                    oc->save_step = 0;                                  /* 201 */
                    oc->anm_step = 0;                                   /* 202 */
                    oc->window = 0;
                }
            }
        }
    }

    if (oc->main_step != 0)                                             /* 210 */
    {
        end_flg = OptAnimation();                                       /* 211 */

        oc->now_tex = 4;                                                /* 212 */

        if (oc->now_place != 3)                                         /* 213 */
        {
            OptDispBgMask();                                            /* 214 */
        }

        (*OptionDispModule[oc->now_place])();                           /* 216 */

        if (oc->window == 1)                                            /* 218 */
        {
            OptionInitialyzeDisp();                                     /* 219 */
        }
        else if (oc->window == 2)                                       /* 220 */
        {
            SystemDataSaveDispMain();                                   /* 221 */
        }
    }

    if (end_flg != 0)                                                   /* 224 */
    {
        /* Both arms are the same phase.  The pause-menu path presumably went
         * somewhere else in an earlier build; kept as found. */
        if (oc->mode == 0)                                              /* 225 */
        {
            SetNextGPhase(GID_TITLE_MENU);                              /* 227 */
        }
        else
        {
            SetNextGPhase(GID_TITLE_MENU);                              /* 230 */
        }
    }
}

/* The commit.  Nothing is written unless a value actually moved, which is why
 * cancelling out of the screen costs nothing. */
void OptionEnd(void)
{
    if (GetOptWrkChgValue() != 0)                                       /* 250 */
    {
        OptSetOptWrk();                                                 /* 251 */
    }

    OptSoundSetupRef(&opt_wrk);                                         /* 253 */
}

/* Row 0 opens the operate page, 2 the brightness slider, 6 the initialise
 * window; 1, 3, 4 and 5 are edited in place.  paddat[0x19] is the save
 * shortcut. */
static void OptionMainPad(void)
{
    int old_snd_volume;
    char end_flg = 0;                                                   /* 275 */

    if (*paddat[1] == 1)                                                /* 276 */
    {
        end_flg = 1;                                                    /* 280 */
        SystemBankPlay(1, 1, 0, 0, (SND_3D_SET *)0, 0x3200, 0x1000);    /* 281 */
        oc->next_place = 4;                                             /* 291 */
        oc->anm_step = 2;                                               /* 292 */
    }
    else if (*paddat[0x19] == 1)                                        /* 295 */
    {
        end_flg = 1;                                                    /* 297 */
        SystemBankPlay(3, 1, 0, 0, (SND_3D_SET *)0, 0x3200, 0x1000);    /* 298 */
        oc->window = 2;                                                 /* 300 */
        oc->main_step = 4;                                              /* 301 */
    }
    else if (((pad[0].rpt & 0x1000U) != 0) || (GetPadAnalogRpt(0) != 0))/* 303 */
    {
        if (oc->cursor > 0)                                             /* 305 */
        {
            oc->cursor--;                                               /* 308 */
            SystemBankPlay(0, 1, 0, 0, (SND_3D_SET *)0, 0x3200, 0x1000);/* 310 */
        }
    }
    else if (((pad[0].rpt & 0x4000U) != 0) || (GetPadAnalogRpt(1) != 0))/* 312 */
    {
        if (oc->cursor < 5)                                             /* 315 */
        {
            oc->cursor++;                                               /* 317 */
            SystemBankPlay(0, 1, 0, 0, (SND_3D_SET *)0, 0x3200, 0x1000);/* 318 */
        }
    }
    else if (*paddat[0] == 1)                                           /* 320 */
    {
        oc->old_csr = oc->cursor;                                       /* 323 */

        if (oc->cursor == 0)                                            /* 324 */
        {
            oc->anm_step = 2;                                           /* 326 */
            oc->next_place = 1;                                         /* 327 */
            oc->next_csr = 0;                                           /* 328 */
            SystemBankPlay(3, 1, 0, 0, (SND_3D_SET *)0, 0x3200, 0x1000);/* 329 */
        }
        else if (oc->cursor == 2)                                       /* 330 */
        {
            oc->anm_step = 2;                                           /* 332 */
            oc->next_place = 3;                                         /* 333 */
            oc->next_csr = 0;                                           /* 334 */
            SystemBankPlay(3, 1, 0, 0, (SND_3D_SET *)0, 0x3200, 0x1000);/* 335 */
        }
        else if (oc->cursor == 6)                                       /* 336 */
        {
            oc->yn_csr = 1;                                             /* 342 */
            oc->window = 1;                                             /* 344 */
            oc->main_step = 4;                                          /* 345 */
            SystemBankPlay(3, 1, 0, 0, (SND_3D_SET *)0, 0x3200, 0x1000);/* 346 */
        }
    }
    else
    {
        if (oc->cursor == 1)                                            /* 347 */
        {
            u_char old_vib = optm.pad_vib;

            optm.pad_vib = ReplaceONOFF(optm.pad_vib);                  /* 348 */

            /* Switching it on buzzes the pad so the setting is audible. */
            if ((old_vib == '\0') && (optm.pad_vib == '\x01'))          /* 350 */
            {
                oc->vib_time = OPT_VIB_TIME;                            /* 352 */
                printf("Vib Start!!\n");                                /* 353 */
            }
        }
        else if (oc->cursor == 3)                                       /* 355 */
        {
            optm.credits = ReplaceONOFF(optm.credits);                  /* 356 */
        }
        else if (oc->cursor == 4)                                       /* 357 */
        {
            u_char old_output = optm.snd_output;

            optm.snd_output = ReplaceONOFF(optm.snd_output);            /* 358 */

            if (optm.snd_output != old_output)                          /* 359 */
            {
                if (optm.snd_output == 1)                               /* 361 */
                {
                    SndSetMono();                                       /* 363 */
                }
                else
                {
                    SndSetStereo();                                     /* 365 */
                }
            }
        }
        else if (oc->cursor == 5)                                       /* 366 */
        {
            old_snd_volume = optm.snd_volume;
            optm.snd_volume = ChangeValue(optm.snd_volume, 2, 0x100, 0);/* 367 */

            if (optm.snd_volume != old_snd_volume)                      /* 368 */
            {
                SndSetGroupVolume(1, optm.snd_volume);
                SndSetGroupVolume(0, optm.snd_volume);
            }
        }
    }

    /* Leaving the page cancels any buzz still running. */
    if (end_flg != 0)                                                   /* 382 */
    {
        oc->vib_time = 0;                                               /* 383 */
    }
}

static void OptionOperatePad(void)
{
    if (*paddat[1] == 1)                                                /* 395 */
    {
        oc->anm_step = 2;                                               /* 397 */
        oc->next_place = 0;                                             /* 398 */
        oc->next_csr = oc->old_csr;                                     /* 399 */
        SystemBankPlay(1, 1, 0, 0, (SND_3D_SET *)0, 0x3200, 0x1000);    /* 400 */
    }
    else if (((pad[0].rpt & 0x1000U) != 0) || (GetPadAnalogRpt(0) != 0))/* 401 */
    {
        if (oc->cursor > 0)                                             /* 403 */
        {
            oc->cursor--;                                               /* 404 */
            SystemBankPlay(0, 1, 0, 0, (SND_3D_SET *)0, 0x3200, 0x1000);/* 405 */
        }
    }
    else if (((pad[0].rpt & 0x4000U) != 0) || (GetPadAnalogRpt(1) != 0))/* 408 */
    {
        if (oc->cursor < 3)                                             /* 410 */
        {
            oc->cursor++;                                               /* 411 */
            SystemBankPlay(0, 1, 0, 0, (SND_3D_SET *)0, 0x3200, 0x1000);/* 412 */
        }
    }
    else if (*paddat[0] == 1)                                           /* 415 */
    {
        if (oc->cursor == 3)                                            /* 417 */
        {
            oc->old_csr2 = 3;                                           /* 418 */
            oc->anm_step = 2;                                           /* 419 */
            oc->next_place = 2;                                         /* 420 */
            oc->next_csr = 0;                                           /* 421 */
            SystemBankPlay(3, 1, 0, 0, (SND_3D_SET *)0, 0x3200, 0x1000);/* 422 */
        }
    }
    else
    {
        if (oc->cursor == 0)                                            /* 427 */
        {
            optm.move_operate = ReplaceONOFF(optm.move_operate);        /* 429 */
        }
        else if (oc->cursor == 1)                                       /* 430 */
        {
            optm.view_vertical = ReplaceONOFF(optm.view_vertical);      /* 432 */
        }
        else if (oc->cursor == 2)                                       /* 433 */
        {
            optm.ana_replace = ReplaceONOFF(optm.ana_replace);          /* 435 */
        }
    }
}

/* 0x10..0x90 in steps of 2.  There is no cursor -- the whole page is the
 * slider. */
static void OptionBrightnessSetup(void)
{
    if (*paddat[1] == 1)                                                /* 451 */
    {
        oc->anm_step = 2;                                               /* 453 */
        oc->next_place = 0;                                             /* 454 */
        oc->next_csr = oc->old_csr;                                     /* 455 */
        SystemBankPlay(1, 1, 0, 0, (SND_3D_SET *)0, 0x3200, 0x1000);    /* 456 */
    }
    else
    {
        optm.brightness = ChangeValue(optm.brightness, 2, 0x90, 0x10);  /* 459 */
    }
}

/* Three pad layouts, 0..2.  Backing out returns to the operate page's row 3,
 * not to the main list -- old_csr2 is the second-level breadcrumb. */
static void OptionButtonSetup(void)
{
    if (*paddat[1] == 1)                                                /* 471 */
    {
        oc->next_place = 1;                                             /* 473 */
        oc->next_csr = oc->old_csr2;                                    /* 474 */
        oc->anm_step = 2;                                               /* 475 */
        SystemBankPlay(1, 1, 0, 0, (SND_3D_SET *)0, 0x3200, 0x1000);    /* 476 */
    }
    else
    {
        optm.pad_type = (u_char)ChangeValue((int)optm.pad_type, 1, 2, 0); /* 479 */
    }
}

/* The initialise-confirm window.  Yes (yn_csr 0) resets the *working* copy,
 * not opt_wrk -- backing out of the screen afterwards still discards it. */
static void OptVerify(void)
{
    if (oc->window == 1)                                                /* 490 */
    {
        if (*paddat[1] == 1)                                            /* 492 */
        {
            SystemBankPlay(1, 1, 0, 0, (SND_3D_SET *)0, 0x3200, 0x1000);/* 493 */
            oc->main_step = 1;                                          /* 494 */
            oc->window = 0;                                             /* 495 */
        }
        else if (*paddat[0] == 1)                                       /* 496 */
        {
            SystemBankPlay(3, 1, 0, 0, (SND_3D_SET *)0, 0x3200, 0x1000);/* 497 */

            if (oc->yn_csr == 0)                                        /* 498 */
            {
                InitOptionSetup(&optm);                                 /* 499 */
                oc->main_step = 1;                                      /* 501 */
            }
            else
            {
                oc->main_step = 1;                                      /* 502 */
            }

            oc->window = 0;                                             /* 503 */
        }
        else if (((pad[0].one & 0x2000U) != 0) || (GetPadAnalogRpt(3) != 0) ||
                 ((pad[0].one & 0x8000U) != 0) || (GetPadAnalogRpt(2) != 0)) /* 507 */
        {
            oc->yn_csr = (oc->yn_csr == 0);                             /* 508 */
            SystemBankPlay(0, 1, 0, 0, (SND_3D_SET *)0, 0x3200, 0x1000);
        }
    }
}

void OptPK2SendVram(int tex_id, void *tex_addr)
{
    if (tex_id != oc->now_tex)                                          /* 529 */
    {
        PK2SendVram((uintptr_t)tex_addr, -1, -1, 0);                    /* 532 */
        oc->now_tex = tex_id;                                           /* 533 */
    }
}

/* Nine fields compared one by one; there is no memcmp. */
static int GetOptWrkChgValue(void)
{
    int res = 1;                                                        /* 545 */

    if ((optm.brightness == opt_wrk.brightness) &&                      /* 546 */
        (optm.snd_volume == opt_wrk.snd_volume) &&                      /* 547 */
        (optm.pad_vib == opt_wrk.pad_vib) &&                            /* 548 */
        (optm.pad_type == opt_wrk.pad_type) &&                          /* 549 */
        (optm.view_vertical == opt_wrk.view_vertical) &&                /* 550 */
        (optm.ana_replace == opt_wrk.ana_replace) &&
        (optm.credits == opt_wrk.credits) &&
        (optm.snd_output == opt_wrk.snd_output))                        /* 551 */
    {
        res = (optm.move_operate != opt_wrk.move_operate);              /* 552 */
    }

    return res;                                                         /* 554 */
}

/* One step per auto-repeat tick, clamped.  The SE only plays when the value
 * actually moved, so a slider at its end is silent. */
static int ChangeValue(int val, int ofs, int max, int min)
{
    if (((pad[0].rpt & 0x2000U) != 0) || (GetPadAnalogRpt(3) != 0))     /* 570 */
    {
        if (val < max)                                                  /* 571 */
        {
            val += ofs;                                                 /* 572 */
            if (val > max)                                              /* 573 */
            {
                val = max;
            }
            SystemBankPlay(0, 1, 0, 0, (SND_3D_SET *)0, 0x3200, 0x1000);/* 574 */
        }
    }
    else if (((pad[0].rpt & 0x8000U) != 0) || (GetPadAnalogRpt(2) != 0))/* 577 */
    {
        if (val > min)                                                  /* 578 */
        {
            val -= ofs;                                                 /* 579 */
            if (val < min)                                              /* 580 */
            {
                val = min;
            }
            SystemBankPlay(0, 1, 0, 0, (SND_3D_SET *)0, 0x3200, 0x1000);/* 581 */
        }
    }

    return val;                                                         /* 584 */
}

/* Either direction flips a boolean setting; GCC merged the two `one` tests
 * into one 8-byte load and mask. */
static u_char ReplaceONOFF(u_char src)
{
    u_char ret = src;

    if (((pad[0].one & 0x2000U) != 0) || (GetPadAnalogRpt(3) != 0) ||
        ((pad[0].one & 0x8000U) != 0) || (GetPadAnalogRpt(2) != 0))     /* 595 */
    {
        ret = (src == '\0');                                            /* 599 */
        SystemBankPlay(0, 1, 0, 0, (SND_3D_SET *)0, 0x3200, 0x1000);
    }

    return ret;                                                         /* 601 */
}

void OptSetOptWrk(void)
{
    opt_wrk = optm;                                                     /* 611 */
}

/* Group 1 is the effects bus and group 0 the music bus; both follow the one
 * volume setting. */
void OptSoundSetupRef(OPTION_WRK *ow)
{
    if (ow->snd_output == '\x01')                                       /* 620 */
    {
        SndSetMono();                                                   /* 621 */
    }
    else
    {
        SndSetStereo();                                                 /* 622 */
    }

    SndSetGroupVolume(1, ow->snd_volume);                               /* 623 */
    SndSetGroupVolume(0, ow->snd_volume);                               /* 624 */
}

void SetSave_Option(MC_SAVE_DATA *data)
{
    data->addr = (u_char *)&opt_wrk;                                    /* 635 */
    data->size = 0x10;                                                  /* 636 */
}

void OptionBackGroundLoadReq(void)
{
    if (opt_og_tex_addr != (void *)0)                                   /* 655 */
    {
        LiberateOptionTexMem(&opt_og_tex_addr);
    }
    if (opt_top_tex_addr != (void *)0)                                  /* 656 */
    {
        LiberateOptionTexMem(&opt_top_tex_addr);
    }
    if (opt_brn_tex_addr != (void *)0)                                  /* 657 */
    {
        LiberateOptionTexMem(&opt_brn_tex_addr);
    }
    if (opt_key_tex_addr != (void *)0)                                  /* 658 */
    {
        LiberateOptionTexMem(&opt_key_tex_addr);
    }

    GetOptionTexMem(&opt_og_tex_addr, OUTGAME_PK2);                     /* 661 */
    GetOptionTexMem(&opt_top_tex_addr, OPT_TOP_PK2 + GetLanguage());    /* 662 */
    GetOptionTexMem(&opt_brn_tex_addr, OPT_BRN_PK2);                    /* 663 */
    GetOptionTexMem(&opt_key_tex_addr, OPT_KEY_PK2 + GetLanguage());    /* 664 */

    OptionTexLoadReq(opt_og_tex_addr, OUTGAME_PK2);                     /* 666 */
    OptionTexLoadReq(opt_top_tex_addr, OPT_TOP_PK2 + GetLanguage());    /* 667 */
    OptionTexLoadReq(opt_brn_tex_addr, OPT_BRN_PK2);                    /* 668 */
    OptionTexLoadReq(opt_key_tex_addr, OPT_KEY_PK2 + GetLanguage());    /* 669 */
}

static void GetOptionTexMem(void **tex_addr, int data_label)
{
    if (*tex_addr != (void *)0)                                         /* 679 */
    {
        LiberateOptionTexMem(tex_addr);
    }

    *tex_addr = ol_loadGetHeap(GetFileSize(data_label));                /* 681 */
}

static void OptionTexLoadReq(void *tex_addr, int data_label)
{
    FileLoadReqEE(data_label, tex_addr, 5, (FILE_LOAD_CALLBACK)0, (void *)0); /* 692 */
}

static int OptionTexLoadWait(void)
{
    int res = 0;                                                        /* 701 */

    if ((FileLoadIsEnd2(OUTGAME_PK2, opt_og_tex_addr) != 0) &&
        (FileLoadIsEnd2(OPT_TOP_PK2 + GetLanguage(), opt_top_tex_addr) != 0) &&
        (FileLoadIsEnd2(OPT_BRN_PK2, opt_brn_tex_addr) != 0) &&
        (FileLoadIsEnd2(OPT_KEY_PK2 + GetLanguage(), opt_key_tex_addr) != 0)) /* 705 */
    {
        res = 1;
    }

    return res;                                                         /* 712 */
}

void OptionMemFree(void)
{
    OptionTexLoadCancel(opt_og_tex_addr, OUTGAME_PK2);
    OptionTexLoadCancel(opt_top_tex_addr, OPT_TOP_PK2 + GetLanguage());
    OptionTexLoadCancel(opt_brn_tex_addr, OPT_BRN_PK2);
    OptionTexLoadCancel(opt_key_tex_addr, OPT_KEY_PK2 + GetLanguage());

    LiberateOptionTexMem(&opt_og_tex_addr);
    LiberateOptionTexMem(&opt_top_tex_addr);
    LiberateOptionTexMem(&opt_brn_tex_addr);
    LiberateOptionTexMem(&opt_key_tex_addr);
}

static void LiberateOptionTexMem(void **tex_addr)
{
    if (*tex_addr != (void *)0)                                         /* 743 */
    {
        ol_loadFreeHeap(*tex_addr);                                     /* 744 */
        *tex_addr = (void *)0;                                          /* 745 */
    }
}

static void OptionTexLoadCancel(void *tex_addr, int data_label)
{
    if ((tex_addr != (void *)0) &&
        (FileLoadIsEnd2(data_label, tex_addr) == 0))                    /* 757 */
    {
        FileLoadCancel2(data_label, tex_addr,
                        (FILE_LOAD_CALLBACK)0, (void *)0);              /* 758 */
    }
}

/* The pause menu edits vibration in place, so both copies move together. */
void OptionVibChange(int sw)
{
    optm.pad_vib = (u_char)sw;                                          /* 769 */
    opt_wrk.pad_vib = (u_char)sw;                                       /* 770 */
}

int GetOptionVib(void)
{
    return (int)opt_wrk.pad_vib;                                        /* 777 */
}
