// FILE: /home/zero_rom/zero2np/src/ingame/photo/photo.c
//
// The photo phase, and the photo album.
//
// Two halves that share nothing but the file.
//
// The phase is a twelve-step state machine on photo_wrk.mode, dispatched once
// a frame by PhotoMain().  Firing the shutter leaves PlayerTakePictJob() with
// a PHOTO_WRK_DEF; PhotoWrkInit() commits it and the sequence takes over the
// screen: load the hint texture (0/4), freeze the 3D world and white-flash
// (1/2/3), hold the developed frame (5), cross-fade the hint in, hold it and
// cross-fade it out (6/7/8), optionally play the door-seal dissolve (9/10),
// then compress the captured frame into the album slot (11) and give the world
// back (12).  one_Story_Photo() is the GPhase wrapper around it, and the whole
// thing is drawn by photo_make.o -- this file only decides what and when.
//
// The album is pfile_wrk: sixteen PICTURE_WRK records, each pinned to a photo
// data slot by adr_no.  Sorting only ever permutes the records; the image
// bytes never move, which is why every sort carries adr_no along and why the
// "delete" path parks the freed adr_no on the last slot rather than losing it.
//
// Three ROM quirks are reproduced as found and called out at their sites:
// GetFilePhotoAdrNo() and GetPhotoData() print the wrong function name in
// their range errors, DelFilePhotoProtect() stores status twice, and
// SortPhotoData_NonProtect() copies back only set_count records where its
// sibling copies all sixteen.
//
// Reconstructed from the Feb 6 2004 prototype (SLES_523.84), photo.o.
//
// Trailing /* NNN */ comments are the original source line numbers, measured
// from the $LM stabs.  A statement whose only memory access goes through
// fixed_array's inlined operator[] leaves no $LM of its own -- its line is
// swallowed by fixed_array.h's 124/125 -- so the album code carries a fair
// number of numbers interpolated into a measured gap rather than read out of
// it.  Every line that appears on a control-flow statement, a call or a plain
// global store is measured.

#include "photo.h"

#include <stddef.h>                                 /* NULL                   */
#include <stdio.h>                                  /* printf                 */
#include <string.h>                                 /* memset                 */

#include "libvu0.h"                                 /* sceVu0CopyVector       */

#include "../../common/mem_util.h"                  /* mem_utilGetMem         */
#include "../../common/utility.h"                   /* GetRandValI            */
#include "../../common/utility2.h"                  /* PRINT_ASSERT           */
#include "../../common/variable.h"                  /* sys_wrk / plyr_wrk     */
#include "../../graphics/effect/effect.h"           /* EffectPhotoPhase       */
#include "../../graphics/effect/effect_oth.h"       /* DoorSealDisappearReq   */
#include "../../graphics/effect/effect_scr.h"       /* SetWhiteIn2            */
#include "../../graphics/graph2d/fade.h"            /* FadeMain               */
#include "../../graphics/graph2d/g2d_draw.h"        /* SPRT_DAT / LocalCopy*  */
#include "../../graphics/graph2d/message.h"         /* PrintMsg_Arrange       */
#include "../../graphics/graph3d/gra3d.h"           /* gra3dDraw              */
#include "../../main/gphase.h"                      /* SetNextGPhase          */
#include "../../system/eeiop/cddat.h"               /* GetFileSize / file ids */
#include "../../system/eeiop/fileload.h"            /* FileLoadIsEnd2         */
#include "../../system/os/eecdvd.h"                 /* LoadReq                */
#include "../../system/os/system.h"                 /* PHOTO_DATA_ADDR        */
#include "../../system/pad/pad.h"                   /* paddat                 */
#include "../../system/pad/vib_manage.h"            /* CallVibrate            */
#include "../enemy/enemy.h"                         /* EnemyPhotoMain         */
#include "../ingame.h"                              /* SetIngamePhoto         */
#include "../map/MapFog.h"                          /* MapFogProc             */
#include "../map/MapObj.h"                          /* MapObjProc             */
#include "../map/MhCtl.h"                           /* MhCtlDrawLock          */
#include "../menu/play_data.h"                      /* SetDateInfoType        */
#include "../movie_room_menu/prg/movie_projecter.h" /* movie_projecterWork    */
#include "../plyr/player.h"                         /* GetPlyrAreaNo          */
#include "../plyr/plyr_mdl.h"                       /* PlayerDrawLock         */
#include "../plyr/sis_mdl.h"                        /* SisterDrawLock         */
#include "../subtitle/subtitle.h"                   /* SubTitleMain           */
#include "finder.h"                                 /* FinderDrawLock         */
#include "m_plyr_camera.h"                          /* m_plyr_camera          */
#include "photo_make.h"                             /* DispPhotoFrame1        */

/* --------------------------------------------------------------------------
 *  Where the pixels go.
 *
 *  PHOTO_FRAME_BUF_ADRS is the stride between the two GS frame buffers, in
 *  4KB pages -- the same 0x1180 ingame.c and pause.c use.  PHOTO_CAPTURE_ADRS
 *  is the VRAM scratch the finished frame is parked in before it is pulled
 *  down to EE memory; several modules borrow that same block.
 *
 *  PHOTO_WORK_ADRS is EFFECT_WRK1_ADDR / PACKET2D_ADDR from system.h.  The
 *  photo pipeline borrows the 2D-packet region as its uncompressed capture
 *  buffer, which is safe because the whole 2D path is frozen for the duration
 *  of the phase.  The compressed result lands in PHOTO_DATA_ADDR, one
 *  SPHOTO_ONE_SIZE slot per album entry.
 * ------------------------------------------------------------------------ */
#define PHOTO_FRAME_BUF_ADRS    0x1180
#define PHOTO_CAPTURE_ADRS      0x2bc0
#define PHOTO_WORK_ADRS         0x01e79b00
#define SPHOTO_ONE_SIZE         0x1000

/* --------------------------------------------------------------------------
 *  The three full-screen hint sprites.
 *
 *  mayu_pk2_dat is the Mayu-curse plate and kusabi_pk2_dat the Kusabi one --
 *  both are picked in PhotoWrkInit() and both come with the texture loaded
 *  into photo_tmp_adrs.  hint_dat_one is the ordinary photo-hint plate that
 *  every photo_dat[] subject shares.
 *
 *  Only tex0, w/h and alpha carry: DrawPhotoFilterPK2() supplies the position
 *  and re-drives the alpha off photo_wrk.cnt, so the alpha here is just the
 *  ceiling the cross-fade reaches.
 * ------------------------------------------------------------------------ */
                                                            /* rodata 3c2eb0 */
static const SPRT_DAT mayu_pk2_dat =
    { 0x2006d00625323480ULL, 0, 0, 388, 256, 0, 0, 0,  50, 0, 0 };
                                                            /* rodata 3c2ed0 */
static const SPRT_DAT kusabi_pk2_dat =
    { 0x200694059d30b480ULL, 0, 0, 128,  64, 0, 0, 0,  80, 0, 0 };
                                                            /* rodata 3c2ef0 */
static const SPRT_DAT hint_dat_one =
    { 0x2006d0062531b480ULL, 0, 0, 384, 256, 0, 0, 0, 150, 0, 0 };

/* --------------------------------------------------------------------------
 *  Module state.
 * ------------------------------------------------------------------------ */

PFILE_WRK pfile_wrk;                                        /* data 33c218   */
PHOTO_WRK photo_wrk;                                        /* data 33c420   */

/* The hint subjects this shot captured, and the cursor PicturePre1() walks
 * through them with.  One pass of the 0/../8 sub-sequence runs per subject. */
static fixed_array<HINT_PHOTO_REQ, HINT_PHOTO_REQ_MAX> hint_photo_req;
                                                            /* bss 4bbb10    */
static int hint_reqs_cnt;                                   /* sbss 3f4eb8   */
static int hint_req_no;                                     /* sbss 3f4ebc   */

/* The subject's name, as a message group / index pair; -1 for "unnamed", which
 * is what suppresses DispPhotoName(). */
static int name_msg_type;                                   /* sbss 3f4eb0   */
static int name_msg_name;                                   /* sbss 3f4eb4   */

/* The hint plate currently being shown, its message, and the texture behind
 * it.  photo_special_tex_file_no is -1 when there is nothing to load. */
static SPRT_DAT *p_hint_dat;                                /* sbss 3f4ed0   */
static int   hint_mes_type;                                 /* sbss 3f4ec8   */
static int   hint_mes_no;                                   /* sbss 3f4ecc   */
static void *photo_tmp_adrs;                                /* sbss 3f4ec0   */
static int   photo_special_tex_file_no;                     /* sbss 3f4ec4   */

/* Where the rare ghost was standing.  Written by PhotoWrkInit() and never read
 * in this module -- the camera HUD is the consumer. */
static float rare_ene_pos[4];                               /* bss 4bbb30    */

/* Non-zero when the shot broke a door seal, which is what inserts the
 * dissolve steps (modes 9 and 10) into the sequence. */
static int unlock_ghost;                                    /* sbss 3f4ed4   */

static void photoLock3D(void);
static int  PictureEnd(void);
static void DispPhotoName(void);
static void SetSpecialFurn(int sw);

/* ==========================================================================
 *  Committing a shot
 * ======================================================================== */

/* Called before the shot is scored, to clear what the last one left behind. */
void PhotoWrkPreInit(void)                                                   /* 151 */
{
    photo_wrk.sta      = 0;                                                  /* 153 */
    photo_wrk.bRareEne = 0;                                                  /* 154 */
    photo_wrk.furn_flg = 0;                                                  /* 155 */
}

/* Commits the finished PHOTO_WRK_DEF and arms the phase.
 *
 * The Mayu-curse and invalid shots take the first branch: they have no
 * photo_dat[] subjects at all, just one fixed plate and its texture, and they
 * clear bGradual so the plate is shown flat instead of dissolved in.  Every
 * other shot takes the second, which copies the subject list over and defers
 * the texture choice to PicturePre1(). */
void PhotoWrkInit(const PHOTO_WRK_DEF *pDef)                                 /* 160 */
{
    int i;

    if (pDef->type == PHOTO_TYPE_MAYU_CURSE ||                               /* 161 */
        pDef->type == PHOTO_TYPE_INVALID)
    {
        photo_wrk.bGradual = 0;                                              /* 164 */
        hint_reqs_cnt      = 0;                                              /* 166 */

        if (pDef->type == PHOTO_TYPE_INVALID)                                /* 169 */
        {
            /* One of the four Kusabi plates, at random. */
            photo_special_tex_file_no = GetRandValI(4) + EF_KUSABI_PHT1_PK2; /* 171 */
            hint_mes_no   = 3;                                               /* 172 */
            hint_mes_type = 0x2a;                                            /* 173 */
            p_hint_dat    = (SPRT_DAT *)&kusabi_pk2_dat;                     /* 174 */
        }
        else
        {
            photo_special_tex_file_no = PHT_ETC_GET_000_PK2;                 /* 178 */
            hint_mes_no   = 0;                                               /* 179 */
            hint_mes_type = -1;                                              /* 180 */
            p_hint_dat    = (SPRT_DAT *)&mayu_pk2_dat;                       /* 181 */
        }
    }
    else
    {
        if (pDef->hint_cnt > HINT_PHOTO_REQ_MAX)                             /* 187 */
        {
            PRINT_ASSERT("hint_req_cnt > HINT_PHOTO_REQ_MAX");               /* 188 */
        }

        photo_wrk.bGradual        = 1;                                       /* 191 */
        photo_special_tex_file_no = -1;                                      /* 192 */
        p_hint_dat                = NULL;                                    /* 193 */
        hint_reqs_cnt             = pDef->hint_cnt;                          /* 194 */

        for (i = 0; i < pDef->hint_cnt; i++)                                 /* 196 */
        {
            hint_photo_req[i] = pDef->hint_pict[i];                          /* 197 */
        }                                                                    /* 198 */

        if (pDef->type == PHOTO_TYPE_RARE)                                   /* 201 */
        {
            photo_wrk.bRareEne = 1;                                          /* 202 */
            sceVu0CopyVector(rare_ene_pos, (float *)pDef->pos);              /* 203 */
        }
        else if (pDef->type == PHOTO_TYPE_HINT3D)                            /* 206 */
        {
            photo_wrk.sta |= PHOTO_STA_FURN;                                 /* 207 */
        }
    }

    photo_wrk.b3DDraw = 1;                                                   /* 213 */
    photo_wrk.mode    = 0;                                                   /* 214 */
    photo_wrk.cnt     = 1;                                                   /* 215 */
    SetDebugMenuSwitch(0);                                                   /* 216 */
    name_msg_type     = pDef->msg_type;                                      /* 217 */
    name_msg_name     = pDef->msg_name;                                      /* 218 */
    photo_wrk.adr_no  = (u_char)pDef->adr_no;                                /* 219 */
    unlock_ghost      = pDef->unlock_ghost;                                  /* 220 */
    photo_tmp_adrs    = NULL;                                                /* 221 */
    hint_req_no       = 0;                                                   /* 222 */
}

/* ==========================================================================
 *  The phase
 * ======================================================================== */

/* One step of the sequence.  Returns non-zero only on the frame mode 12
 * finishes, which is what tells one_Story_Photo() to hand the phase back. */
int PhotoMain(void)                                                          /* 228 */
{
    int ret;

    ret = 0;                                                                 /* 229 */

    switch (photo_wrk.mode)                                                  /* 235 */
    {
    case 0:
        PicturePre1();                                                       /* 237 */
        break;                                                               /* 238 */

    case 1:
        PictureInitSub();                                                    /* 241 */
        break;                                                               /* 242 */

    case 2:
        PicturePre3();                                                       /* 245 */
        photo_wrk.mode = 3;                                                  /* 246 */
        break;                                                               /* 247 */

    case 4:
        /* Waiting on the hint texture the previous step requested. */
        EffectPhotoPhase();                                                  /* 250 */
        if (FileLoadIsEnd2(photo_special_tex_file_no, photo_tmp_adrs))       /* 251 */
        {
            photo_wrk.mode = 2;                                              /* 254 */
        }
        break;

    case 3:
        PicturePre4();                                                       /* 257 */
        break;                                                               /* 258 */

    case 5:
        PictureDisp();                                                       /* 261 */
        break;                                                               /* 262 */

    case 6:
        PictureHint1();                                                      /* 265 */
        break;                                                               /* 266 */

    case 7:
        PictureHint2();                                                      /* 269 */
        break;                                                               /* 270 */

    case 8:
        PictureHint3();                                                      /* 273 */
        break;                                                               /* 274 */

    case 9:
        PictureToUnlockGhost();                                              /* 277 */
        break;                                                               /* 278 */

    case 10:
        PictureUnlockGhost();                                                /* 281 */
        break;                                                               /* 282 */

    case 11:
        PictureCapture();                                                    /* 285 */
        break;                                                               /* 286 */

    case 12:
        ret = PictureEnd();                                                  /* 289 */
        break;
    }

    return ret;                                                              /* 293 */
}

/* Wipes the album and re-pins each slot to its own photo-data address.  Called
 * once per new game, from ingame.c. */
void InitPhotoWrk(void)                                                      /* 298 */
{
    int i;

    memset(&photo_wrk, 0, sizeof(photo_wrk));                                /* 301 */
    memset(&pfile_wrk, 0, sizeof(pfile_wrk));                                /* 302 */

    for (i = 0; i < PHOTO_FILE_MAX; i++)                                     /* 304 */
    {
        pfile_wrk.pic[i].adr_no = (u_char)i;                                 /* 305 */
    }                                                                        /* 306 */
}

/* Mode 0.  Takes the next hint subject off the list, picks its plate and its
 * texture, and either issues the load (mode 4) or skips straight to the freeze
 * (mode 1).  A scenery shot has no texture to load -- its "hint" is the 3D
 * world -- so PHOTO_STA_FURN suppresses the file lookup entirely.
 *
 * Re-entered once per subject: modes 6/7/8 come back here when the list is not
 * exhausted, which is what lets one shot show several hints in turn. */
void PicturePre1(void)                                                       /* 309 */
{
    int spno;

    if (hint_req_no < hint_reqs_cnt)                                         /* 311 */
    {
        spno = hint_photo_req[hint_req_no].no;                               /* 313 */
        hint_req_no++;                                                       /* 314 */

        if ((photo_wrk.sta & PHOTO_STA_FURN) == 0)                           /* 317 */
        {
            if (photo_tmp_adrs != NULL)                                      /* 319 */
            {
                mem_utilFreeMem(photo_tmp_adrs);                             /* 320 */
            }
            photo_special_tex_file_no = photo_dat[spno].image;               /* 321 */
        }

        hint_mes_no   = photo_dat[spno].mesnuma;                             /* 325 */
        hint_mes_type = photo_dat[spno].mestype;                             /* 326 */
        p_hint_dat    = (SPRT_DAT *)&hint_dat_one;                           /* 327 */
    }

    if (photo_special_tex_file_no >= 0)                                      /* 331 */
    {
        photo_tmp_adrs = mem_utilGetMem(GetFileSize(photo_special_tex_file_no));  /* 332 */
        LoadReq(photo_special_tex_file_no, (uintptr_t)photo_tmp_adrs);       /* 333 */

        photo_wrk.sta |= PHOTO_STA_HINT_LOAD;                                /* 335 */
        photo_wrk.mode = 4;                                                  /* 336 */
    }
    else
    {
        photo_wrk.mode = 1;                                                  /* 339 */
    }

    EffectPhotoPhase();                                                      /* 342 */
}

/* Freezes everything the photo is a picture of.  b3DDraw goes down with it, so
 * MoviePhaseJobPre() stops re-scoring the photographable objects too. */
static void photoLock3D(void)                                                /* 345 */
{
    FinderDrawLock();                                                        /* 346 */
    EffWrkStopFlgSet(1);                                                     /* 347 */
    PlayerDrawLock();                                                        /* 348 */
    EnemyDrawLock();                                                         /* 349 */
    SisterDrawLock();                                                        /* 350 */
    MhCtlDrawLock();                                                         /* 351 */
    photo_wrk.b3DDraw = 0;                                                   /* 352 */
}

/* Mode 1.  The white flash, and the hold behind it: 20 frames in battle, 60
 * out of it, with SetWhiteIn2() given one frame less so the flash has cleared
 * by the time the count expires.
 *
 * A scenery shot takes the other branch -- there is nothing to freeze, because
 * what the picture shows is still the live world; DrawSpecialFurnPhoto() draws
 * it into the photo frame and SetSpecialFurn(1) swaps in the objects that only
 * exist inside a photograph. */
void PictureInitSub(void)                                                    /* 355 */
{
    if ((photo_wrk.sta & PHOTO_STA_FURN) == 0)                               /* 356 */
    {
        photoLock3D();                                                       /* 357 */
        photo_wrk.cnt = (plyr_wrk.cmn_wrk.st.sta & 0x20) ? 20 : 60;          /* 359 */
        EffectPhotoPhase();                                                  /* 360 */
        LocalCopyLtoL(3, (int)(((u_int)sys_wrk.count & 1) * PHOTO_FRAME_BUF_ADRS),
                      PHOTO_CAPTURE_ADRS);                                   /* 361 */
        SetWhiteIn2((plyr_wrk.cmn_wrk.st.sta & 0x20) ? 19 : 59);             /* 362 */
        photo_wrk.mode = 3;                                                  /* 363 */
    }
    else
    {
        EffectPhotoPhase();                                                  /* 365 */
        DrawSpecialFurnPhoto(128.0f, 96.0f);                                 /* 366 */
        SetSpecialFurn(1);                                                   /* 367 */
        photo_wrk.mode = 2;                                                  /* 368 */
    }
}

/* Mode 2.  The scenery-shot twin of PictureInitSub()'s first branch: the world
 * is frozen only after the photograph-only objects have had a frame to draw. */
void PicturePre3(void)                                                       /* 374 */
{
    photoLock3D();                                                           /* 375 */
    photo_wrk.cnt = (plyr_wrk.cmn_wrk.st.sta & 0x20) ? 20 : 60;              /* 377 */
    EffectPhotoPhase();                                                      /* 379 */
    LocalCopyLtoL(3, (int)(((u_int)sys_wrk.count & 1) * PHOTO_FRAME_BUF_ADRS),
                  PHOTO_CAPTURE_ADRS);                                       /* 380 */
    LocalCopyLtoL(0, (int)((((u_int)sys_wrk.count + 1) & 1) * PHOTO_FRAME_BUF_ADRS),
                  (int)(((u_int)sys_wrk.count & 1) * PHOTO_FRAME_BUF_ADRS)); /* 381 */
    SetWhiteIn2((plyr_wrk.cmn_wrk.st.sta & 0x20) ? 19 : 59);                 /* 382 */
}

/* Mode 3.  First frame of the developed picture. */
void PicturePre4(void)                                                       /* 386 */
{
    if (photo_wrk.bRareEne)                                                  /* 387 */
    {
        DispPhotoFrame1(0, 128.0f, 96.0f, 3);                                /* 388 */
    }
    else
    {
        DispPhotoFrame1(0, 128.0f, 128.0f, 100);                             /* 390 */
    }

    photo_wrk.mode = 5;                                                      /* 392 */
    DrawPhotoFrame(128.0f, 96.0f);                                           /* 393 */
}

/* Mode 5.  Holds the picture while the subject's name is shown, then decides
 * what follows: the two fixed plates and the scenery/hint-load shots go on to
 * the hint cross-fade (mode 6), everything else skips straight to the capture
 * (mode 11) because there is no hint to show. */
void PictureDisp(void)                                                       /* 397 */
{
    if (photo_wrk.bRareEne)                                                  /* 398 */
    {
        DispPhotoFrame1(1, 128.0f, 96.0f, 3);                                /* 399 */
    }
    else
    {
        DispPhotoFrame1(1, 128.0f, 96.0f, 100);                              /* 401 */
    }

    if ((photo_wrk.sta & (PHOTO_STA_HINT_LOAD | PHOTO_STA_FURN)) == 0)       /* 405 */
    {
        DispPhotoName();                                                     /* 406 */
    }

    if (photo_wrk.cnt-- == 0)                                                /* 409 */
    {
        if (p_hint_dat == (SPRT_DAT *)&mayu_pk2_dat ||                       /* 410 */
            p_hint_dat == (SPRT_DAT *)&kusabi_pk2_dat)
        {
            photo_wrk.cnt  = 50;                                             /* 412 */
            photo_wrk.mode = 6;                                              /* 413 */
            photo_wrk.sta &= ~PHOTO_STA_HINT_LOAD;                           /* 414 */
        }
        else if (photo_wrk.sta & (PHOTO_STA_HINT_LOAD | PHOTO_STA_FURN))     /* 415 */
        {
            photo_wrk.cnt  = 50;                                             /* 416 */
            photo_wrk.mode = 6;                                              /* 417 */
            FinderBankPlay(8, 1, 1, 0, NULL, 0x3200, 0x1000);                /* 418 */
            photo_wrk.sta &= ~PHOTO_STA_HINT_LOAD;                           /* 419 */
        }
        else
        {
            photo_wrk.mode = 11;                                             /* 421 */
        }
    }

    DrawPhotoFrame(128.0f, 96.0f);                                           /* 424 */
}

/* Mode 6.  Cross-fades the hint plate in over 50 frames.  cnt counts down, so
 * the frame's own alpha rises as cnt*2 while the filter drives the plate. */
void PictureHint1(void)                                                      /* 429 */
{
    if ((photo_wrk.sta & PHOTO_STA_FURN) == 0)                               /* 432 */
    {
        if (photo_wrk.bRareEne)                                              /* 433 */
        {
            DispPhotoFrame1(2, 128.0f, 96.0f, 3);                            /* 434 */
        }
        else
        {
            DispPhotoFrame1(2, 128.0f, 96.0f, photo_wrk.cnt * 2);            /* 437 */
        }

        if (p_hint_dat != NULL)                                              /* 439 */
        {
            DrawPhotoFilterPK2(0, 128.0f, 96.0f, p_hint_dat,
                               photo_wrk.cnt, photo_tmp_adrs,
                               photo_wrk.bGradual);                          /* 440 */
        }
    }
    else
    {
        DispPhotoFrame1(2, 128.0f, 96.0f, 100);                              /* 443 */
    }

    if (photo_wrk.cnt-- == 0)                                                /* 446 */
    {
        photo_wrk.cnt  = 50;                                                 /* 448 */
        photo_wrk.mode = 7;                                                  /* 449 */
    }

    DrawPhotoFrame(128.0f, 96.0f);                                           /* 451 */
}

/* Mode 7.  Holds the hint and runs its message.  An unnamed hint (msg type -1)
 * just waits out the 50 frames and shows the subject's name instead; a named
 * one runs the message window until MesStatusCheck() says it is finished, with
 * circle advancing the page.
 *
 * The exit splits: an ordinary hint goes back for the fade-out (mode 8), but a
 * scenery or rare-ghost shot has nothing left to dissolve and drops straight to
 * the capture (mode 11). */
void PictureHint2(void)                                                      /* 455 */
{
    int n;
    int ret;

    ret = 0;

    if ((photo_wrk.sta & PHOTO_STA_FURN) == 0)                               /* 460 */
    {
        if (photo_wrk.bRareEne)                                              /* 461 */
        {
            DispPhotoFrame1(2, 128.0f, 96.0f, 3);                            /* 462 */
        }
        else
        {
            DispPhotoFrame1(2, 128.0f, 96.0f, 0);                            /* 465 */
        }

        if (p_hint_dat != NULL)                                              /* 468 */
        {
            DrawPhotoFilterPK2(1, 128.0f, 96.0f, p_hint_dat,
                               photo_wrk.cnt, photo_tmp_adrs,
                               photo_wrk.bGradual);                          /* 469 */
        }
    }
    else
    {
        DispPhotoFrame1(2, 128.0f, 96.0f, 100);                              /* 472 */
    }

    DrawPhotoFrame(128.0f, 96.0f);                                           /* 476 */

    if (hint_mes_type < 0)                                                   /* 479 */
    {
        if (photo_wrk.cnt-- == 0)                                            /* 480 */
        {
            photo_wrk.cnt = 50;                                              /* 481 */
            ret = 1;                                                         /* 482 */
        }
        DispPhotoName();                                                     /* 484 */
    }
    else
    {
        PrintMsgDef_W(hint_mes_type, hint_mes_no);                           /* 486 */
        n = MesStatusCheck();                                                /* 487 */
        if (n == 0)                                                          /* 488 */
        {
            photo_wrk.cnt = 50;                                              /* 489 */
            ret = 1;                                                         /* 490 */
        }
        else if (n == 1)                                                     /* 491 */
        {
            /* Action 3 is the page-advance button; a hold count of exactly 1
             * is "pressed on this frame".  The ROM uses the bare index here,
             * as the rest of the tree does. */
            if (*paddat[3] == 1)                                             /* 492 */
            {
                MesSetNextPage();                                            /* 493 */
            }
        }
    }

    if (ret)                                                                 /* 498 */
    {
        if ((photo_wrk.sta & PHOTO_STA_FURN) != 0 ||                         /* 499 */
            photo_wrk.bRareEne)
        {
            photo_wrk.mode = 11;                                             /* 501 */
        }
        else
        {
            photo_wrk.mode = 8;                                              /* 504 */
        }
    }
}

/* Mode 9.  Ten frames of the plain photo frame, then the door-seal dissolve is
 * requested and the sealed ghost is unlocked for drawing. */
void PictureToUnlockGhost(void)                                              /* 511 */
{
    DispPhotoFrame1(3, 128.0f, 96.0f, 100);                                  /* 512 */

    if (photo_wrk.cnt-- == 0)                                                /* 514 */
    {
        photo_wrk.cnt  = 50;                                                 /* 515 */
        photo_wrk.mode = 10;                                                 /* 516 */
        DoorSealDisappearReq();                                              /* 517 */
        photo_datObjSealGhostDrawLock();                                     /* 518 */
    }
}

/* Mode 10.  Runs the dissolve to its end, then rejoins the ordinary tail. */
void PictureUnlockGhost(void)                                                /* 525 */
{
    DoorSealDisappearDraw();                                                 /* 527 */

    if (DoorSealDisappearIsEnd())                                            /* 530 */
    {
        DoorSealDisappearEndProc();                                          /* 531 */
        photo_wrk.cnt  = 10;                                                 /* 532 */
        photo_wrk.mode = 12;                                                 /* 533 */
        MhCtlDrawLock();                                                     /* 534 */
        SetDebugMenuSwitch(1);                                               /* 535 */
    }
}

/* Mode 8.  The mirror of PictureHint1(): fades the hint back out, 100 - cnt*2,
 * and drops into the capture when the count runs out. */
void PictureHint3(void)                                                      /* 546 */
{
    if ((photo_wrk.sta & PHOTO_STA_FURN) == 0)                               /* 548 */
    {
        if (photo_wrk.bRareEne)                                              /* 549 */
        {
            DispPhotoFrame1(2, 128.0f, 96.0f, 3);                            /* 550 */
        }
        else
        {
            DispPhotoFrame1(2, 128.0f, 96.0f, 100 - photo_wrk.cnt * 2);      /* 553 */
        }

        if (p_hint_dat != NULL)                                              /* 555 */
        {
            DrawPhotoFilterPK2(2, 128.0f, 96.0f, p_hint_dat,
                               photo_wrk.cnt, photo_tmp_adrs,
                               photo_wrk.bGradual);                          /* 556 */
        }
    }
    else
    {
        DispPhotoFrame1(2, 128.0f, 96.0f, 100);                              /* 559 */
    }

    DrawPhotoFrame(128.0f, 96.0f);                                           /* 562 */

    if (photo_wrk.cnt-- == 0)                                                /* 564 */
    {
        photo_wrk.mode = 11;                                                 /* 566 */
    }
}

/* Mode 11.  Pulls the frozen frame out of VRAM and files it.
 *
 * Two copies are taken from the same scratch block: the full 384x128 picture
 * goes to the work area and is compressed into the album slot, and a 45x15
 * thumbnail (built into VRAM by MakeSmallPhotoV) goes to the slot's own
 * SPHOTO_ONE_SIZE page uncompressed. */
void PictureCapture(void)                                                    /* 571 */
{
    LocalCopyLtoL(0, (int)((((u_int)sys_wrk.count + 1) & 1) * PHOTO_FRAME_BUF_ADRS),
                  (int)(((u_int)sys_wrk.count & 1) * PHOTO_FRAME_BUF_ADRS)); /* 572 */
    LocalCopyLtoL(5, (int)((((u_int)sys_wrk.count + 1) & 1) * PHOTO_FRAME_BUF_ADRS),
                  PHOTO_CAPTURE_ADRS);                                       /* 574 */

    CopyScreenToBuffer2(PHOTO_CAPTURE_ADRS, PHOTO_WORK_ADRS, 0,
                        128, 96, 384, 128);                                  /* 576 */
    CompPhotoFromWorkArea(photo_wrk.adr_no);                                 /* 577 */

    /* The ROM reads photo_wrk.adr_no one statement early (line 580) and keeps
     * the address in a callee-saved register across MakeSmallPhotoV(); folded
     * back into the call it belongs to, since nothing between the two can
     * change it. */
    MakeSmallPhotoV(128.0f, 96.0f);                                          /* 581 */
    CopyScreenToBuffer2(PHOTO_CAPTURE_ADRS,
                        PHOTO_DATA_ADDR + photo_wrk.adr_no * SPHOTO_ONE_SIZE,
                        0, 0, 0, 45, 15);                                    /* 582 */

    LocalCopyLtoL(0, (int)((((u_int)sys_wrk.count + 1) & 1) * PHOTO_FRAME_BUF_ADRS),
                  (int)(((u_int)sys_wrk.count & 1) * PHOTO_FRAME_BUF_ADRS)); /* 585 */
    LocalCopyLtoB(0, 0, (int)((((u_int)sys_wrk.count + 1) & 1) * PHOTO_FRAME_BUF_ADRS));  /* 587 */

    if (unlock_ghost)                                                        /* 589 */
    {
        photo_wrk.cnt  = 10;                                                 /* 590 */
        photo_wrk.mode = 9;                                                  /* 591 */
        MhCtlDrawUnlock();                                                   /* 592 */
    }
    else
    {
        photo_wrk.cnt  = 10;                                                 /* 595 */
        photo_wrk.mode = 12;                                                 /* 596 */
        SetDebugMenuSwitch(1);                                               /* 598 */
    }
}

/* Mode 12.  Ten frames of tail, the first of which gives the world back: every
 * draw lock taken by photoLock3D() is released, the hint subjects this shot
 * captured are marked as seen, and the camera's noise is reset.
 *
 * EnemyAnimLock() goes *on* here rather than off -- the ghosts stay posed for
 * the remaining frames and are released only at the very end, so nothing moves
 * between the picture leaving the screen and the phase ending. */
static int PictureEnd(void)                                                  /* 604 */
{
    int ret;
    int i;

    if (photo_wrk.cnt == 10)                                                 /* 607 */
    {
        LocalCopyLtoL(0, (int)((((u_int)sys_wrk.count + 1) & 1) * PHOTO_FRAME_BUF_ADRS),
                      (int)(((u_int)sys_wrk.count & 1) * PHOTO_FRAME_BUF_ADRS));  /* 608 */
        PlayerDrawUnlock();                                                  /* 609 */
        SisterDrawUnlock();                                                  /* 610 */
        EnemyDrawUnlock();                                                   /* 611 */
        EnemyAnimLock();                                                     /* 612 */
        FinderDrawUnlock();                                                  /* 613 */
        MhCtlDrawUnlock();                                                   /* 614 */
        photo_wrk.b3DDraw = 1;                                               /* 615 */
        SetSpecialFurn(0);                                                   /* 616 */
        m_plyr_camera.ReqNoiseReset();                                       /* 617 */

        for (i = 0; i < hint_reqs_cnt; i++)                                  /* 620 */
        {
            photo_datFlgUp(hint_photo_req[i].no);                            /* 621 */
        }                                                                    /* 622 */

        EffWrkStopFlgSet(0);                                                 /* 624 */
    }
    else
    {
        DispPhotoFrame1(3, 128.0f, 96.0f, 100);                              /* 626 */
    }

    m_plyr_camera.Main();                                                    /* 630 */
    EnemyPhotoMain();                                                        /* 632 */

    if (photo_wrk.cnt-- == 0)                                                /* 634 */
    {
        EnemyAnimUnlock();                                                   /* 636 */
        ret = 1;                                                             /* 637 */
    }
    else
    {
        ret = 0;                                                             /* 638 */
    }

    return ret;                                                              /* 641 */
}

/* ==========================================================================
 *  The album
 * ======================================================================== */

/* Picks the photo-data slot the next shot will be written to.
 *
 * A free record first; failing that the first used-but-unprotected one, whose
 * picture is about to be overwritten.  That second scan deliberately stops one
 * short of the end, and slot 15 is the fallback when it finds nothing -- so a
 * completely full and completely protected album still gives up slot 15. */
int GetSavePhotoNo(void)                                                     /* 649 */
{
    int    i;
    u_char adr_no;
    u_char get_save_pos;

    adr_no       = 0;                                                        /* 651 */
    get_save_pos = 0;                                                        /* 655 */

    for (i = 0; i < PHOTO_FILE_MAX; i++)                                     /* 659 */
    {
        if (pfile_wrk.pic[i].status == 0)                                    /* 661 */
        {
            adr_no       = pfile_wrk.pic[i].adr_no;                          /* 663 */
            get_save_pos = 1;                                                /* 664 */
            break;
        }
    }                                                                        /* 666 */

    if (!get_save_pos)                                                       /* 669 */
    {
        for (i = 0; i < PHOTO_FILE_MAX - 1; i++)                             /* 671 */
        {
            if ((pfile_wrk.pic[i].status & 2) == 0 &&
                (pfile_wrk.pic[i].status & 1) != 0)                          /* 673 */
            {
                adr_no       = pfile_wrk.pic[i].adr_no;                      /* 675 */
                get_save_pos = 1;                                            /* 676 */
                break;
            }
        }

        if (!get_save_pos)                                                   /* 681 */
        {
            adr_no = pfile_wrk.pic[PHOTO_FILE_MAX - 1].adr_no;               /* 683 */
        }
    }

    return adr_no;                                                           /* 687 */
}

/* Files a shot against the slot GetSavePhotoNo() handed out.
 *
 * With room in the album the record simply goes on the end.  Without it the
 * old record holding this adr_no is wiped and re-sorted to the back, then
 * found again -- the sort moved it, which is why the second scan exists.
 *
 * `number` stays 0 if that second scan somehow fails, so a lost record
 * overwrites slot 0 rather than running off the array. */
int AddPhotoData(int adr_no, int score, int room_no, int chapter_no,
                 sceCdCLOCK rtc, SUBJECT_WRK *obj, int obj_num)              /* 691 */
{
    int          i;
    int          number;
    PICTURE_WRK *pic;

    number = 0;                                                              /* 698 */

    if (pfile_wrk.pic_num + 1 > PHOTO_FILE_MAX)                              /* 703 */
    {
        for (i = 0; i < PHOTO_FILE_MAX; i++)                                 /* 705 */
        {
            if (pfile_wrk.pic[i].adr_no == adr_no)                           /* 707 */
            {
                memset(&pfile_wrk.pic[i], 0, sizeof(PICTURE_WRK));           /* 709 */
                pfile_wrk.pic[i].adr_no = (u_char)adr_no;                    /* 710 */
                SortPhotoData_Before(&pfile_wrk);                            /* 712 */
                break;                                                       /* 713 */
            }
        }                                                                    /* 715 */

        if (i >= PHOTO_FILE_MAX)                                             /* 719 */
        {
            PRINT_ASSERT("Error! %s", __FUNCTION__);                         /* 721 */
        }

        for (i = 0; i < PHOTO_FILE_MAX; i++)                                 /* 726 */
        {
            if (pfile_wrk.pic[i].adr_no == adr_no)
            {
                number = i;                                                  /* 729 */
                break;                                                       /* 730 */
            }
        }
    }
    else
    {
        number = pfile_wrk.pic_num++;                                        /* 753 */

        /* Should not happen -- the slot past pic_num is free by construction
         * -- but if it is somehow in use, fall back to the first free one. */
        if (pfile_wrk.pic[number].status & 1)                                /* 755 */
        {
            for (i = 0; i < PHOTO_FILE_MAX; i++)                             /* 758 */
            {
                if (pfile_wrk.pic[i].status == 0)                            /* 760 */
                {
                    number = i;                                              /* 761 */
                    break;                                                   /* 762 */
                }
            }                                                                /* 764 */
        }
    }

    pic = &pfile_wrk.pic[number];                                            /* 769 */

    pic->adr_no = (u_char)adr_no;                                            /* 771 */
    pic->score  = score;                                                     /* 772 */
    pic->room   = (short)room_no;                                            /* 773 */
    pic->chp_no = (u_char)chapter_no;                                        /* 774 */
    pic->status = 1;                                                         /* 775 */
    pic->time   = rtc;                                                       /* 776 */

    /* SUBJECT_WRK::sp_no is deliberately dropped; the album only keeps the
     * type and the ghost-list slot. */
    if (obj_num < 3)                                                         /* 778 */
    {
        for (i = 0; i < obj_num; i++)                                        /* 779 */
        {
            pic->maSubject[i].type   = obj[i].type;                          /* 780 */
            pic->maSubject[i].obj_no = obj[i].no;                            /* 781 */
        }                                                                    /* 782 */

        for (; i < 3; i++)                                                   /* 784 */
        {
            pic->maSubject[i].type   = -1;                                   /* 785 */
            pic->maSubject[i].obj_no = 0;                                    /* 786 */
        }                                                                    /* 787 */
    }
    else
    {
        for (i = 0; i < 3; i++)                                              /* 791 */
        {
            pic->maSubject[i].type   = obj[i].type;                          /* 792 */
            pic->maSubject[i].obj_no = obj[i].no;                            /* 793 */
        }                                                                    /* 794 */
    }

    return 1;                                                                /* 798 */
}

/* Removes a filed picture, closing the gap behind it.  The freed adr_no is
 * parked on the now-empty last slot rather than discarded, so the photo-data
 * page it names comes back into circulation. */
void DeletePhotoData(u_char no)                                              /* 805 */
{
    int i;
    int dust;

    if (pfile_wrk.pic[no].status == 0)                                       /* 813 */
    {
        PRINT_WARNING("Warning! %s\n", __FUNCTION__);                        /* 815 */
        return;
    }

    dust = pfile_wrk.pic[no].adr_no;                                         /* 820 */

    for (i = no; i < PHOTO_FILE_MAX - 1; i++)                                /* 823 */
    {
        pfile_wrk.pic[i] = pfile_wrk.pic[i + 1];                             /* 824 */
    }                                                                        /* 825 */

    pfile_wrk.pic[PHOTO_FILE_MAX - 1].status = 0;                            /* 827 */
    pfile_wrk.pic[PHOTO_FILE_MAX - 1].adr_no = (u_char)dust;                 /* 828 */
    pfile_wrk.pic_num--;                                                     /* 830 */
}

/* Hands the album menu its own copy to sort, so a cancelled sort costs
 * nothing. */
void CopyPFileWrk(PFILE_WRK *copy_wrk)                                       /* 842 */
{
    *copy_wrk = pfile_wrk;                                                   /* 845 */
}

PFILE_WRK *GetCamPhotoFile(void)                                             /* 854 */
{
    return &pfile_wrk;                                                       /* 858 */
}

int GetFilePhotoState(u_char no)                                             /* 866 */
{
    if (no >= PHOTO_FILE_MAX)                                                /* 870 */
    {
        printf("ERROR!! GetFilePhotoState()\n");                             /* 871 */
        no = 0;                                                              /* 872 */
    }
    return pfile_wrk.pic[no].status;
}

/* The range error names SetFilePhotoProtect(), not this function -- a
 * copy-paste in the ROM, kept. */
int GetFilePhotoAdrNo(u_char no)                                             /* 883 */
{
    if (no >= PHOTO_FILE_MAX)                                                /* 886 */
    {
        printf("ERROR!! SetFilePhotoProtect()\n");                           /* 887 */
        no = 0;                                                              /* 888 */
    }
    return pfile_wrk.pic[no].adr_no;
}

/* Protects a filed picture from being overwritten.  Fails -- returning 0 --
 * once fifteen of the sixteen are protected, so there is always one slot
 * GetSavePhotoNo() can take. */
int SetFilePhotoProtect(u_char no)                                           /* 901 */
{
    int res;

    res = 0;                                                                 /* 904 */

    if (no >= PHOTO_FILE_MAX)                                                /* 907 */
    {
        printf("ERROR!! SetFilePhotoProtect()\n");                           /* 908 */
        no = 0;                                                              /* 909 */
    }

    if (pfile_wrk.pic[no].status & 1)                                        /* 913 */
    {
        if (pfile_wrk.protect_num < PHOTO_FILE_MAX - 1)                      /* 915 */
        {
            pfile_wrk.pic[no].status |= 2;                                   /* 916 */
            pfile_wrk.protect_num++;                                         /* 917 */
            res = 1;                                                         /* 918 */
        }
    }

    return res;                                                              /* 923 */
}

/* The range error names DelFilePhotoProtect() correctly here, but the two
 * stores to status are the ROM's own: it clears the whole byte and then writes
 * the in-use bit back, rather than masking bit 1 off.  The dead store survives
 * because the inlined fixed_array bounds check sits between the two. */
void DelFilePhotoProtect(u_char no)                                          /* 930 */
{
    if (no >= PHOTO_FILE_MAX)                                                /* 933 */
    {
        printf("ERROR!! DelFilePhotoProtect()\n");                           /* 934 */
        no = 0;                                                              /* 935 */
    }

    pfile_wrk.pic[no].status = 0;                                            /* 939 */
    pfile_wrk.pic[no].status = 1;                                            /* 940 */
    pfile_wrk.protect_num--;                                                 /* 943 */
}

/* Same copy-paste as GetFilePhotoAdrNo(): the error names
 * DelFilePhotoProtect(). */
PICTURE_WRK *GetPhotoData(u_char no)                                         /* 953 */
{
    if (no >= PHOTO_FILE_MAX)                                                /* 956 */
    {
        printf("ERROR!! DelFilePhotoProtect()\n");                           /* 957 */
        no = 0;                                                              /* 958 */
    }
    return &pfile_wrk.pic[no];
}

int GetFilePhotoNum(void)                                                    /* 969 */
{
    return pfile_wrk.pic_num;                                                /* 973 */
}

/* --------------------------------------------------------------------------
 *  The six album orderings.
 *
 *  Every one of them starts from _Before(), which packs the used records at
 *  the front, so each sort only has to deal with a contiguous run.
 * ------------------------------------------------------------------------ */

/* Used records first, free ones after, both in their existing order. */
void SortPhotoData_Before(PFILE_WRK *photo_file)                             /* 980 */
{
    PICTURE_WRK sort_buff[PHOTO_FILE_MAX];
    int         photo_num;
    int         i;

    memset(sort_buff, 0, sizeof(sort_buff));                                 /* 987 */

    photo_num = 0;                                                           /* 989 */

    for (i = 0; i < PHOTO_FILE_MAX; i++)                                     /* 992 */
    {
        if (photo_file->pic[i].status & 1)                                   /* 994 */
        {
            sort_buff[photo_num] = photo_file->pic[i];                       /* 996 */
            photo_num++;                                                     /* 998 */
        }
    }                                                                        /* 1000 */

    for (i = 0; i < PHOTO_FILE_MAX; i++)                                     /* 1003 */
    {
        if (photo_file->pic[i].status == 0)                                  /* 1005 */
        {
            sort_buff[photo_num] = photo_file->pic[i];                       /* 1007 */
            photo_num++;                                                     /* 1008 */
        }
    }                                                                        /* 1010 */

    if (photo_num > PHOTO_FILE_MAX)                                          /* 1013 */
    {
        PRINT_ASSERT("Error! %s", __FUNCTION__);                             /* 1014 */
    }

    for (i = 0; i < PHOTO_FILE_MAX; i++)                                     /* 1019 */
    {
        photo_file->pic[i] = sort_buff[i];                                   /* 1020 */
    }                                                                        /* 1021 */
}

/* Protected first, then the rest of the used ones, then the free slots. */
void SortPhotoData_Protect(PFILE_WRK *photo_file)                            /* 1030 */
{
    PICTURE_WRK sort_buff[PHOTO_FILE_MAX];
    int         set_count;
    int         i;

    set_count = 0;                                                           /* 1036 */

    SortPhotoData_Before(photo_file);                                        /* 1039 */

    for (i = 0; i < PHOTO_FILE_MAX; i++)                                     /* 1044 */
    {
        if (photo_file->pic[i].status & 2)                                   /* 1046 */
        {
            sort_buff[set_count] = photo_file->pic[i];
            set_count++;                                                     /* 1047 */
        }
    }                                                                        /* 1049 */

    for (i = 0; i < PHOTO_FILE_MAX; i++)                                     /* 1051 */
    {
        if ((photo_file->pic[i].status & 2) == 0 &&
            (photo_file->pic[i].status & 1) != 0)                            /* 1053 */
        {
            sort_buff[set_count] = photo_file->pic[i];
            set_count++;                                                     /* 1055 */
        }
    }                                                                        /* 1057 */

    for (i = 0; i < PHOTO_FILE_MAX; i++)                                     /* 1059 */
    {
        if (photo_file->pic[i].status == 0)                                  /* 1061 */
        {
            sort_buff[set_count] = photo_file->pic[i];
            set_count++;                                                     /* 1063 */
        }
    }                                                                        /* 1065 */

    if (set_count != PHOTO_FILE_MAX)                                         /* 1068 */
    {
        PRINT_ASSERT("Error! %s", __FUNCTION__);                             /* 1069 */
    }

    for (i = 0; i < PHOTO_FILE_MAX; i++)                                     /* 1074 */
    {
        photo_file->pic[i] = sort_buff[i];                                   /* 1075 */
    }                                                                        /* 1076 */
}

/* The inverse: unprotected used records first, then the protected ones, then
 * the free slots.
 *
 * The copy-back runs to set_count, not to PHOTO_FILE_MAX as its sibling above
 * does.  Since the three passes between them cover every slot exactly once,
 * set_count is 16 whenever the assert holds, so the two behave identically --
 * but the asymmetry is the ROM's and is kept. */
void SortPhotoData_NonProtect(PFILE_WRK *photo_file)                         /* 1082 */
{
    PICTURE_WRK sort_buff[PHOTO_FILE_MAX];
    int         set_count;
    int         i;

    set_count = 0;                                                           /* 1088 */

    SortPhotoData_Before(photo_file);                                        /* 1091 */

    for (i = 0; i < PHOTO_FILE_MAX; i++)                                     /* 1096 */
    {
        if ((photo_file->pic[i].status & 2) == 0 &&
            (photo_file->pic[i].status & 1) != 0)                            /* 1098 */
        {
            sort_buff[set_count] = photo_file->pic[i];
            set_count++;                                                     /* 1100 */
        }
    }                                                                        /* 1102 */

    for (i = 0; i < PHOTO_FILE_MAX; i++)                                     /* 1104 */
    {
        if ((photo_file->pic[i].status & 2) != 0 &&
            (photo_file->pic[i].status & 1) != 0)                            /* 1106 */
        {
            sort_buff[set_count] = photo_file->pic[i];
            set_count++;                                                     /* 1108 */
        }
    }                                                                        /* 1110 */

    for (i = 0; i < PHOTO_FILE_MAX; i++)                                     /* 1112 */
    {
        if (photo_file->pic[i].status == 0)                                  /* 1114 */
        {
            sort_buff[set_count] = photo_file->pic[i];
            set_count++;                                                     /* 1116 */
        }
    }                                                                        /* 1118 */

    if (set_count != PHOTO_FILE_MAX)                                         /* 1121 */
    {
        PRINT_ASSERT("Error! %s", __FUNCTION__);                             /* 1122 */
    }

    for (i = 0; i < set_count; i++)                                          /* 1127 */
    {
        photo_file->pic[i] = sort_buff[i];                                   /* 1128 */
    }                                                                        /* 1129 */
}

/* Newest first.
 *
 * Six full bubble passes rather than one comparison chain: pass n orders by
 * the n-th date field among records whose earlier fields are all equal.  The
 * RTC field is BCD, so SetDateInfoType() has to unpack both records every
 * comparison -- which is why the ROM pays for six passes rather than one. */
void SortPhotoData_NewTime(PFILE_WRK *photo_file)                            /* 1137 */
{
    int         i;
    int         j;
    PICTURE_WRK buff;
    DATE_INFO   date1;
    DATE_INFO   date2;

    SortPhotoData_Before(photo_file);                                        /* 1144 */

    for (i = 0; i < photo_file->pic_num - 1; i++)                            /* 1148 */
    {
        for (j = i + 1; j < photo_file->pic_num; j++)                        /* 1149 */
        {
            SetDateInfoType(&date1, &photo_file->pic[i].time);               /* 1151 */
            SetDateInfoType(&date2, &photo_file->pic[j].time);               /* 1152 */

            if (date1.day.year < date2.day.year)                             /* 1155 */
            {
                buff               = photo_file->pic[j];
                photo_file->pic[j] = photo_file->pic[i];
                photo_file->pic[i] = buff;
            }
        }                                                                    /* 1160 */
    }                                                                        /* 1161 */

    for (i = 0; i < photo_file->pic_num - 1; i++)                            /* 1163 */
    {
        for (j = i + 1; j < photo_file->pic_num; j++)                        /* 1164 */
        {
            SetDateInfoType(&date1, &photo_file->pic[i].time);               /* 1166 */
            SetDateInfoType(&date2, &photo_file->pic[j].time);               /* 1167 */

            if (date1.day.year == date2.day.year)                            /* 1168 */
            {
                if (date1.day.month < date2.day.month)                       /* 1170 */
                {
                    buff               = photo_file->pic[j];
                    photo_file->pic[j] = photo_file->pic[i];
                    photo_file->pic[i] = buff;
                }
            }
        }                                                                    /* 1176 */
    }                                                                        /* 1177 */

    for (i = 0; i < photo_file->pic_num - 1; i++)                            /* 1179 */
    {
        for (j = i + 1; j < photo_file->pic_num; j++)                        /* 1180 */
        {
            SetDateInfoType(&date1, &photo_file->pic[i].time);               /* 1182 */
            SetDateInfoType(&date2, &photo_file->pic[j].time);               /* 1183 */

            if (date1.day.year  == date2.day.year &&                         /* 1185 */
                date1.day.month == date2.day.month)
            {
                if (date1.day.day < date2.day.day)                           /* 1188 */
                {
                    buff               = photo_file->pic[j];
                    photo_file->pic[j] = photo_file->pic[i];
                    photo_file->pic[i] = buff;
                }
            }
        }                                                                    /* 1194 */
    }                                                                        /* 1195 */

    for (i = 0; i < photo_file->pic_num - 1; i++)                            /* 1197 */
    {
        for (j = i + 1; j < photo_file->pic_num; j++)                        /* 1198 */
        {
            SetDateInfoType(&date1, &photo_file->pic[i].time);               /* 1200 */
            SetDateInfoType(&date2, &photo_file->pic[j].time);               /* 1201 */

            if (date1.day.year  == date2.day.year  &&                        /* 1203 */
                date1.day.month == date2.day.month &&
                date1.day.day   == date2.day.day)
            {
                if (date1.time.hour < date2.time.hour)                       /* 1207 */
                {
                    buff               = photo_file->pic[j];
                    photo_file->pic[j] = photo_file->pic[i];
                    photo_file->pic[i] = buff;
                }
            }
        }                                                                    /* 1213 */
    }                                                                        /* 1214 */

    for (i = 0; i < photo_file->pic_num - 1; i++)                            /* 1216 */
    {
        for (j = i + 1; j < photo_file->pic_num; j++)                        /* 1217 */
        {
            SetDateInfoType(&date1, &photo_file->pic[i].time);               /* 1219 */
            SetDateInfoType(&date2, &photo_file->pic[j].time);               /* 1220 */

            if (date1.day.year  == date2.day.year  &&                        /* 1222 */
                date1.day.month == date2.day.month &&
                date1.day.day   == date2.day.day   &&
                date1.time.hour == date2.time.hour)
            {
                if (date1.time.min < date2.time.min)                         /* 1227 */
                {
                    buff               = photo_file->pic[j];
                    photo_file->pic[j] = photo_file->pic[i];
                    photo_file->pic[i] = buff;
                }
            }
        }                                                                    /* 1233 */
    }                                                                        /* 1234 */

    for (i = 0; i < photo_file->pic_num - 1; i++)                            /* 1236 */
    {
        for (j = i + 1; j < photo_file->pic_num; j++)                        /* 1237 */
        {
            SetDateInfoType(&date1, &photo_file->pic[i].time);               /* 1239 */
            SetDateInfoType(&date2, &photo_file->pic[j].time);               /* 1240 */

            if (date1.day.year  == date2.day.year  &&                        /* 1242 */
                date1.day.month == date2.day.month &&
                date1.day.day   == date2.day.day   &&
                date1.time.hour == date2.time.hour &&
                date1.time.min  == date2.time.min)
            {
                if (date1.time.sec < date2.time.sec)                         /* 1248 */
                {
                    buff               = photo_file->pic[j];
                    photo_file->pic[j] = photo_file->pic[i];
                    photo_file->pic[i] = buff;
                }
            }
        }                                                                    /* 1254 */
    }                                                                        /* 1255 */
}

/* Oldest first.  The exact mirror of SortPhotoData_NewTime(): same six passes,
 * every comparison reversed. */
void SortPhotoData_OldTime(PFILE_WRK *photo_file)                            /* 1262 */
{
    int         i;
    int         j;
    PICTURE_WRK buff;
    DATE_INFO   date1;
    DATE_INFO   date2;

    SortPhotoData_Before(photo_file);                                        /* 1269 */

    for (i = 0; i < photo_file->pic_num - 1; i++)                            /* 1273 */
    {
        for (j = i + 1; j < photo_file->pic_num; j++)                        /* 1274 */
        {
            SetDateInfoType(&date1, &photo_file->pic[i].time);               /* 1276 */
            SetDateInfoType(&date2, &photo_file->pic[j].time);               /* 1277 */

            if (date1.day.year > date2.day.year)                             /* 1280 */
            {
                buff               = photo_file->pic[j];
                photo_file->pic[j] = photo_file->pic[i];
                photo_file->pic[i] = buff;
            }
        }                                                                    /* 1285 */
    }                                                                        /* 1286 */

    for (i = 0; i < photo_file->pic_num - 1; i++)                            /* 1288 */
    {
        for (j = i + 1; j < photo_file->pic_num; j++)                        /* 1289 */
        {
            SetDateInfoType(&date1, &photo_file->pic[i].time);               /* 1291 */
            SetDateInfoType(&date2, &photo_file->pic[j].time);               /* 1292 */

            if (date1.day.year == date2.day.year)                            /* 1293 */
            {
                if (date1.day.month > date2.day.month)                       /* 1295 */
                {
                    buff               = photo_file->pic[j];
                    photo_file->pic[j] = photo_file->pic[i];
                    photo_file->pic[i] = buff;
                }
            }
        }                                                                    /* 1301 */
    }                                                                        /* 1302 */

    for (i = 0; i < photo_file->pic_num - 1; i++)                            /* 1304 */
    {
        for (j = i + 1; j < photo_file->pic_num; j++)                        /* 1305 */
        {
            SetDateInfoType(&date1, &photo_file->pic[i].time);               /* 1307 */
            SetDateInfoType(&date2, &photo_file->pic[j].time);               /* 1308 */

            if (date1.day.year  == date2.day.year &&                         /* 1310 */
                date1.day.month == date2.day.month)
            {
                if (date1.day.day > date2.day.day)                           /* 1313 */
                {
                    buff               = photo_file->pic[j];
                    photo_file->pic[j] = photo_file->pic[i];
                    photo_file->pic[i] = buff;
                }
            }
        }                                                                    /* 1319 */
    }                                                                        /* 1320 */

    for (i = 0; i < photo_file->pic_num - 1; i++)                            /* 1322 */
    {
        for (j = i + 1; j < photo_file->pic_num; j++)                        /* 1323 */
        {
            SetDateInfoType(&date1, &photo_file->pic[i].time);               /* 1325 */
            SetDateInfoType(&date2, &photo_file->pic[j].time);               /* 1326 */

            if (date1.day.year  == date2.day.year  &&                        /* 1328 */
                date1.day.month == date2.day.month &&
                date1.day.day   == date2.day.day)
            {
                if (date1.time.hour > date2.time.hour)                       /* 1332 */
                {
                    buff               = photo_file->pic[j];
                    photo_file->pic[j] = photo_file->pic[i];
                    photo_file->pic[i] = buff;
                }
            }
        }                                                                    /* 1338 */
    }                                                                        /* 1339 */

    for (i = 0; i < photo_file->pic_num - 1; i++)                            /* 1341 */
    {
        for (j = i + 1; j < photo_file->pic_num; j++)                        /* 1342 */
        {
            SetDateInfoType(&date1, &photo_file->pic[i].time);               /* 1344 */
            SetDateInfoType(&date2, &photo_file->pic[j].time);               /* 1345 */

            if (date1.day.year  == date2.day.year  &&                        /* 1347 */
                date1.day.month == date2.day.month &&
                date1.day.day   == date2.day.day   &&
                date1.time.hour == date2.time.hour)
            {
                if (date1.time.min > date2.time.min)                         /* 1352 */
                {
                    buff               = photo_file->pic[j];
                    photo_file->pic[j] = photo_file->pic[i];
                    photo_file->pic[i] = buff;
                }
            }
        }                                                                    /* 1358 */
    }                                                                        /* 1359 */

    for (i = 0; i < photo_file->pic_num - 1; i++)                            /* 1361 */
    {
        for (j = i + 1; j < photo_file->pic_num; j++)                        /* 1362 */
        {
            SetDateInfoType(&date1, &photo_file->pic[i].time);               /* 1364 */
            SetDateInfoType(&date2, &photo_file->pic[j].time);               /* 1365 */

            if (date1.day.year  == date2.day.year  &&                        /* 1367 */
                date1.day.month == date2.day.month &&
                date1.day.day   == date2.day.day   &&
                date1.time.hour == date2.time.hour &&
                date1.time.min  == date2.time.min)
            {
                if (date1.time.sec > date2.time.sec)                         /* 1373 */
                {
                    buff               = photo_file->pic[j];
                    photo_file->pic[j] = photo_file->pic[i];
                    photo_file->pic[i] = buff;
                }
            }
        }                                                                    /* 1379 */
    }                                                                        /* 1380 */
}

/* Highest score first.  score is u_int, so the comparison is unsigned. */
void SortPhotoData_BigScore(PFILE_WRK *photo_file)                           /* 1386 */
{
    int         i;
    int         j;
    PICTURE_WRK buff;

    SortPhotoData_Before(photo_file);                                        /* 1391 */

    for (i = 0; i < photo_file->pic_num - 1; i++)                            /* 1394 */
    {
        for (j = i + 1; j < photo_file->pic_num; j++)                        /* 1395 */
        {
            if (photo_file->pic[i].score < photo_file->pic[j].score)          /* 1396 */
            {
                buff               = photo_file->pic[j];
                photo_file->pic[j] = photo_file->pic[i];
                photo_file->pic[i] = buff;
            }
        }                                                                    /* 1401 */
    }                                                                        /* 1402 */
}

/* Lowest score first. */
void SortPhotoData_SmallScore(PFILE_WRK *photo_file)                         /* 1410 */
{
    int         i;
    int         j;
    PICTURE_WRK buff;

    SortPhotoData_Before(photo_file);                                        /* 1415 */

    for (i = 0; i < photo_file->pic_num - 1; i++)                            /* 1418 */
    {
        for (j = i + 1; j < photo_file->pic_num; j++)                        /* 1419 */
        {
            if (photo_file->pic[j].score < photo_file->pic[i].score)          /* 1420 */
            {
                buff               = photo_file->pic[j];
                photo_file->pic[j] = photo_file->pic[i];
                photo_file->pic[i] = buff;
            }
        }                                                                    /* 1425 */
    }                                                                        /* 1426 */
}

/* ==========================================================================
 *  The phase's own frame
 * ======================================================================== */

/* Draws the subject's name under the picture.  The draw env is pushed by hand
 * so the caption lands on top of the photo frame regardless of what the
 * message system was last configured for, and taken back down after. */
static void DispPhotoName(void)                                              /* 1433 */
{
    if (name_msg_type >= 0 && name_msg_name >= 0)                            /* 1434 */
    {
        DRAW_ENV_5 env = {                                                   /* 1435 */
            0x0000008000000044ULL,          /* alpha                         */
            0x0000000000000141ULL,          /* tex1                          */
            0x0000000000000000ULL,          /* clamp                         */
            0x0000000000030003ULL,          /* test                          */
            0x000000010a000118ULL           /* zbuf                          */
        };

        MessageChangeDrawEnv(&env);                                          /* 1444 */
        PrintMsg_Arrange(name_msg_type, name_msg_name,
                         0x140, 0x16f, 1, 0x80, 0xa0, 0, 0, 2);              /* 1445 */
        MessageChangeDrawEnv(NULL);                                          /* 1447 */
    }
}

/* The phase's world pass.  b3DDraw is the switch photoLock3D() throws: while
 * it is down, the photographable objects stop being re-scored, which freezes
 * the finder's idea of what is in frame for the whole sequence. */
static void MoviePhaseJobPre(void)                                           /* 1463 */
{
    movie_projecterWork();                                                   /* 1464 */

    if (photo_wrk.b3DDraw)                                                   /* 1465 */
    {
        photo_datObjMain();                                                  /* 1466 */
    }

    MapFogProc(GetPlyrAreaNo(), plyr_wrk.cmn_wrk.floor,
               plyr_wrk.cmn_wrk.mbox.pos);                                   /* 1470 */
    gra3dDraw();                                                             /* 1471 */
    movie_projecterDraw();                                                   /* 1472 */
}

/* The phase's 2D pass.  The extra frame copy is the door-seal case: with two
 * frames left on the tail, the finished picture is pushed to the other buffer
 * so the dissolve that just ended does not flicker back in. */
static int MoviePhaseJobAfter(void)                                          /* 1475 */
{
    int ret;

    InitEffectsEF();                                                         /* 1478 */

    if (photo_wrk.mode == 12)                                                /* 1480 */
    {
        EffectControl(5);                                                    /* 1482 */
    }

    ret = PhotoMain();                                                       /* 1486 */

    EffectControl(8);                                                        /* 1489 */

    m_plyr_camera.Draw();                                                    /* 1493 */

    if (unlock_ghost && photo_wrk.mode == 12 && photo_wrk.cnt == 8)          /* 1498 */
    {
        LocalCopyLtoL(0, (int)((((u_int)sys_wrk.count + 1) & 1) * PHOTO_FRAME_BUF_ADRS),
                      (int)(((u_int)sys_wrk.count & 1) * PHOTO_FRAME_BUF_ADRS));  /* 1499 */
    }

    FadeMain();                                                              /* 1502 */
    CallVibrate();                                                           /* 1503 */

    return ret;                                                              /* 1505 */
}

/* ==========================================================================
 *  GPhase entry points
 * ======================================================================== */

void init_Story_Photo(void)                                                  /* 1509 */
{
    m_plyr_camera.sp.SEDisable();                                            /* 1511 */
}

void end_Story_Photo(void)                                                   /* 1514 */
{
    m_plyr_camera.sp.SEEnable();                                             /* 1516 */

    if (photo_tmp_adrs != NULL)                                              /* 1518 */
    {
        mem_utilFreeMem(photo_tmp_adrs);                                     /* 1519 */
    }
}

GPHASE_ENUM one_Story_Photo(GPHASE_ENUM dummy)                               /* 1522 */
{
    (void)dummy;

    MoviePhaseJobPre();                                                      /* 1523 */

    if (MoviePhaseJobAfter())                                                /* 1524 */
    {
        SetIngamePhoto(0);                                                   /* 1525 */
        SetNextGPhase(IngameDecideNextPhase());                              /* 1526 */
    }

    SubTitleMain(0);                                                         /* 1529 */

    return GPHASE_CONTINUE;                                                  /* 1531 */
}

/* ==========================================================================
 *  The scenery-photo flag
 * ======================================================================== */

/* Swaps the photograph-only scenery in or out.  MapObjProc() has to run right
 * away because the flag changes what is on the draw list, not just how it is
 * drawn. */
static void SetSpecialFurn(int sw)                                           /* 1539 */
{
    photo_wrk.furn_flg = sw;                                                 /* 1541 */
    MapObjProc();                                                            /* 1542 */
}

int PhotoFlgIsUp(void)                                                       /* 1547 */
{
    return photo_wrk.furn_flg;                                               /* 1548 */
}

void FurnPhotoFlgUp(void)                                                    /* 1552 */
{
    photo_wrk.sta |= PHOTO_STA_FURN;                                         /* 1553 */
}

int FurnPhotoFlgIsUp(void)                                                   /* 1556 */
{
    return ((photo_wrk.sta & PHOTO_STA_FURN) != 0);                          /* 1557 */
}

/* Exported, and eight bytes long: `jr ra` plus a nop.  The function opens at
 * 1568 and its only $LM is the return at 1596, so the twenty-eight lines
 * between them were commented out before this build -- nothing of the debug
 * readout survives to be recovered. */
void PhotoDebug(void)                                                        /* 1568 */
{
}                                                                            /* 1596 */
