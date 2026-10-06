// FILE: /home/zero_rom/zero2np/src/graphics/motion/mdlwork.c
//
// Model-work module.  Owns the four enemy VRAM slots (ene_vram_ctrl), the
// sub-object draw loops for girls/enemies, and the TIM2 upload helpers that
// re-base a model's textures onto whichever slot it was given.
//
// Reconstructed from the Feb 6 2004 prototype (SLES_523.84).
// Trailing /* NNN */ comments are the ROM's source line numbers.

#include "mdlwork.h"

#include <stdio.h>

#include "gra3dConst.h"
#include "gra3dSGD.h"
#include "Morph.h"
#include "motion.h"                             /* motInitANI_CTRL */
#include "accessory.h"                       /* InitPlyrAcsAlpha */
#include "../../common/packfile.h"           /* GetFileInPak / Pk2GetAddr */
#include "../../common/variable.h"           /* sys_wrk */
#include "../graph2d/tim2.h"                 /* MakeTim2SendPacket* */
#include "../graph3d/gra3d.h"                /* gra3dIsMonotoneDrawEnable */
#include "../graph3d/gra3dSGDData.h"         /* sgdRemap */
#include "../../sdk/libgraph.h"              /* SCE_GS_TEX0_1 */
#include "../graph3d/g3dxVu0.h"         /* g3dxVu0CopyVector */
#include "../../miopan/rendering/miopan_graph3d.h" /* host texture binding */

extern ENE_VRAM_CTRL ene_vram_ctrl[4];
extern ENE_VRAM_CTRL ene_vram_bak[4];

u_int *plyr_clut_addr;                       /* sdata 3f2ab4 */
u_int *plyr_bwc_addr;                        /* sdata 3f2ab8 */

/* --------------------------------------------------------------------------
 *  ManmdlSetAlpha
 *
 *  Force the diffuse alpha of every material in an SGD.  The material array
 *  runs from pMaterial up to the next section (pVectorInfo, "phead" in the
 *  untyped view), so the loop bound is the following section pointer rather
 *  than usNumMaterial.
 * ------------------------------------------------------------------------ */
void ManmdlSetAlpha(void *sgd_top, u_char alpha)
{
    SGDMATERIAL *matp;
    u_int       *phead;

    matp  = ((HeaderSection *)sgd_top)->matp.get();                       /* 67 */
    phead = ((HeaderSection *)sgd_top)->phead.get();                      /* 68 */

    while (matp < (SGDMATERIAL *)phead)                             /* 70 */
    {
        matp->vDiffuse[3] = (float)alpha;                           /* 71 */
        matp++;                                                     /* 72 */
    }
}

char motCheckTrRateMdl(u_int mdl_no)
{
    (void)mdl_no;

    return 0;                                                       /* 83 */
}

/* --------------------------------------------------------------------------
 *  MpkMapUnit
 *
 *  A model pack holds its units back to back, each behind a 0x10-byte header
 *  whose first word is the payload size.  Every unit is an SGD, so walk the
 *  chain and fix each one's internal offsets up into real pointers.
 *
 *  The walk stops on a size of 0 or one with the top bit set -- the terminator.
 *  Returns the address of that terminator.
 * ------------------------------------------------------------------------ */
u_int *MpkMapUnit(u_int *mpk_p)
{
    if (!mpk_p)                                                     /* 90 */
    {
        return nullptr;
    }

    mpk_p += 4;                                                     /* 93 */

    while (mpk_p[0] - 1 < 0x7fffffff)                               /* 94 */
    {
        sgdRemap((SGDFILEHEADER *)&mpk_p[4]);                       /* 97 */

        mpk_p += mpk_p[0] / 4 + 4;                                  /* 98 */
    }

    return mpk_p;                                                   /* 101 */
}

/* --------------------------------------------------------------------------
 *  DrawGirlSubObj / DrawEneSubObj
 *
 *  A character model file is a pak: file 0 is the body, files 1..N are the
 *  sub-objects (hair, clothing, accessories).  Each sub-object is a complete
 *  SGD drawn against the parent's coordinate array, so the whole character
 *  only reaches the renderer if these loops run.
 *
 *  Morph ids 1/2 are the cross-fade slots used while a model is being swapped;
 *  a sub-object taking part in one gets its alpha driven by the morph instead
 *  of the caller's.
 * ------------------------------------------------------------------------ */
void DrawGirlSubObj(u_int *mpk_p, u_char alpha)
{
    u_int          i;
    u_int          obj_num;
    float          tmpvec[4];
    HeaderSection *hs;
    SGDCOORDINATE *cp;
    u_char         alp;

    obj_num = mpk_p[0];                                             /* 116 */
    cp      = ((SGDFILEHEADER *)&mpk_p[8])->pCoord;                 /* 118 */

    /*
     * Dead in this build: the ROM still clears tmpvec here (the compiler
     * hoisted the copy out of the loop below) but nothing reads it back.
     */
    g3dxVu0CopyVector(tmpvec, g_v0000);

    for (i = 1; i < obj_num; i++)                                   /* 131 */
    {
        hs = (HeaderSection *)GetFileInPak(mpk_p, i);                /* 132 */

        alp = alpha;                                                /* 150 */

        if (MorphCheckId1(i))                                       /* 151 */
        {
            alp = (u_char)(MorphGetAlpha1((float)alp / 128.0f)       /* 152 */
                           * 128.0f);                               /* 154 */
        }
        else if (MorphCheckId2(i))                                  /* 156 */
        {
            alp = (u_char)(MorphGetAlpha2((float)alp / 128.0f)       /* 157 */
                           * 128.0f);                               /* 159 */
        }

        ManmdlSetAlpha(hs, alp);                                    /* 161 */
        _gra3dDrawSGD((SGDFILEHEADER *)hs, SRT_REALTIME, cp, -1);    /* 163 */
    }
}

void DrawEneSubObj(u_int *mpk_p, u_char alpha1, u_char alpha2)
{
    u_int          i;
    HeaderSection *hs;
    SGDCOORDINATE *cp;
    u_int          obj_num;
    u_char         alpha;

    obj_num = mpk_p[0];                                             /* 182 */
    cp      = ((SGDFILEHEADER *)&mpk_p[8])->pCoord;                 /* 184 */

    for (i = 1; i < obj_num; i++)                                   /* 186 */
    {
        /* sub-object 1 is the one the caller alpha-fades separately */
        alpha = (i == 1) ? alpha2 : alpha1;                          /* 187 */

        hs = (HeaderSection *)GetFileInPak(mpk_p, i);                /* 189 */

        if (MorphCheckId1(i))                                       /* 190 */
        {
            alpha = (u_char)(MorphGetAlpha1((float)alpha / 128.0f)   /* 191 */
                             * 128.0f);                             /* 193 */
        }
        else if (MorphCheckId2(i))                                  /* 195 */
        {
            alpha = (u_char)(MorphGetAlpha2((float)alpha / 128.0f)   /* 196 */
                             * 128.0f);                             /* 198 */
        }

        ManmdlSetAlpha(hs, alpha);                                  /* 200 */
        _gra3dDrawSGD((SGDFILEHEADER *)hs, SRT_REALTIME, cp, -1);    /* 202 */
    }
}

void InitEneVramCtrl(void)
{
    u_int i;

    for (i = 0; i < 4; i++)                                         /* 219 */
    {
        InitEneVramCtrlSub(&ene_vram_ctrl[i]);                      /* 220 */
    }
}

void InitEneVramCtrlSub(ENE_VRAM_CTRL *ev_ctrl)
{
    ev_ctrl->mdl_p  = 0;                                            /* 225 */
    ev_ctrl->flg    = 0;                                            /* 226 */
    ev_ctrl->mdl_no = 0xffff;                                       /* 227 */
    ev_ctrl->offset = 0;                                            /* 228 */
}

/* --------------------------------------------------------------------------
 *  SetEneVram
 *
 *  Shift every TEX0 in the model's pack by "offset" 64-word VRAM blocks, and
 *  flag the pack as mapped.  A negative offset undoes a previous mapping, so
 *  the map flag doubles as a guard against double-mapping either way.
 * ------------------------------------------------------------------------ */
void SetEneVram(u_int *mdl_p, int offset)
{
    u_int *mpk_p;

    mpk_p = (u_int *)GetFileInPak(mdl_p, 0);                        /* 240 */
    (void)GetFileInPak(mdl_p, 1);                                   /* 241 */

    if (offset >= 0)                                                /* 244 */
    {
        if (mpk_p[1] == 1)                                          /* 245 */
        {
            printf("Warning : SetEneVram Already Mapped\n");        /* 247 */
            return;
        }

        mpk_p[1] = 1;
        MpkAddTexOffset(mpk_p, offset);                             /* 251 */
    }
    else
    {
        if (mpk_p[1] == 0)                                          /* 254 */
        {
            printf("Warning : SetEneVram Already UnMapped\n");      /* 256 */
            return;
        }

        mpk_p[1] = 0;
        MpkAddTexOffset(mpk_p, offset);                             /* 260 */
    }
}

int SendEneVram(u_int *mdl_p, int offset)
{
    u_int *mpk_p;
    u_int *pk2_p;

    mpk_p = (u_int *)GetFileInPak(mdl_p, 0);                        /* 274 */
    pk2_p = (u_int *)GetFileInPak(mdl_p, 1);                        /* 275 */

    if (offset >= 0)                                                /* 278 */
    {
        if (mpk_p[1] == 1)                                          /* 279 */
        {
            /* PORT: once the host has resolved this pack's textures it drops
             * the GS uploads inside the bracket; see miopan_graph3d.h. */
            MioPan_Graph3dBeginTim2Upload(mpk_p,
                                          MIOPAN_TIM2_SOURCE_CHARACTER, 0);
            SetManmdlTm2(pk2_p, offset, 0);                         /* 280 */
            MioPan_Graph3dEndTim2Upload();
        }
    }
    else
    {
        printf("Warning : SendEneVram Illegal Vram Adrs\n");        /* 285 */

        mpk_p[1] = 0;                                               /* 287 */
        return 0;
    }

    return 1;                                                       /* 291 */
}

int SendEneVramMono(u_int *mdl_p, int offset, u_int *bwc_p)
{
    u_int *mpk_p;
    u_int *pk2_p;

    mpk_p = (u_int *)GetFileInPak(mdl_p, 0);                        /* 298 */
    pk2_p = (u_int *)GetFileInPak(mdl_p, 1);                        /* 299 */

    if (offset >= 0)                                                /* 302 */
    {
        if (mpk_p[1] == 1)                                          /* 303 */
        {
            /* PORT: the same bracket as SendEneVram(); the monotone CLUT set
             * is a mode of its own. */
            MioPan_Graph3dBeginTim2Upload(mpk_p,
                                          MIOPAN_TIM2_SOURCE_CHARACTER,
                                          bwc_p != (u_int *)0);
            if (bwc_p == (u_int *)0)                                /* 304 */
            {
                SetManmdlTm2(pk2_p, offset, 0);                     /* 305 */
            }
            else
            {
                SetManmdlTm2_Mono(pk2_p, offset, 0, bwc_p);         /* 307 */
            }
            MioPan_Graph3dEndTim2Upload();
        }
    }
    else
    {
        printf("Warning : SendEneVram Illegal Vram Adrs\n");        /* 313 */

        mpk_p[1] = 0;                                               /* 315 */
        return 0;
    }

    return 1;                                                       /* 319 */
}

void SendManMdlTex(void)
{
    u_int i;

    for (i = 0; i < 4; i++)                                         /* 329 */
    {
        if (ene_vram_ctrl[i].flg != 0)                              /* 330 */
        {
            SetManmdlTm2((u_int *)GetFileInPak(ene_vram_ctrl[i].mdl_p, 1),  /* 332 */
                         ene_vram_ctrl[i].offset, 0);               /* 333 */
        }
    }
}

/* --------------------------------------------------------------------------
 *  SetTextureToScene / SetTextureAfterScene
 *
 *  An event scene takes over the whole texture area, so the slots in use are
 *  saved off, un-mapped (each model's TEX0s shifted back to zero) and the
 *  table cleared.  Afterwards the saved table is restored and every slot
 *  re-mapped to the offset it had.
 * ------------------------------------------------------------------------ */
void SetTextureToScene(void)
{
    u_int i;
    int   offset;

    BackupEneVramCtrl();                                            /* 349 */

    for (i = 0; i < 4; i++)                                         /* 352 */
    {
        if (ene_vram_ctrl[i].flg == 1)                              /* 353 */
        {
            offset = -ene_vram_ctrl[i].offset;                      /* 354 */

            SetEneVram(ene_vram_ctrl[i].mdl_p, offset);             /* 356 */
        }
    }

    InitEneVramCtrl();                                              /* 361 */
}

void SetTextureAfterScene(void)
{
    u_int i;

    SetupEneVramCtrl();                                             /* 371 */

    for (i = 0; i < 4; i++)                                         /* 374 */
    {
        if (ene_vram_ctrl[i].flg != 0)                              /* 375 */
        {
            SetEneVram(ene_vram_ctrl[i].mdl_p,                      /* 379 */
                       ene_vram_ctrl[i].offset);
        }
    }
}

/* --------------------------------------------------------------------------
 *  MpkAddTexOffset
 *
 *  Same unit walk as MpkMapUnit, but shifting the TEX0 of every mesh in every
 *  unit instead of remapping it.
 * ------------------------------------------------------------------------ */
void MpkAddTexOffset(u_int *mpk_p, int offset)
{
    /* PORT: the TEX0s are about to move, so whatever the host resolved for
     * them no longer names anything. */
    MioPan_Graph3dNotifyTex0Rebase(mpk_p);

    mpk_p += 4;                                                     /* 388 */

    while (mpk_p[0] - 1 < 0x7fffffff)                               /* 389 */
    {
        SgdAddTexOffset(&mpk_p[4], offset);                         /* 392 */

        mpk_p += mpk_p[0] / 4 + 4;                                  /* 393 */
    }
}

void SetManmdlTm2(u_int *pak_addr, int offset, char mode)
{
    u_int  tm2_num;
    u_int  i;
    u_int *tm2_addr;

    tm2_num = *pak_addr;                                            /* 405 */

    for (i = 0; i < tm2_num; i++)                                   /* 406 */
    {
        tm2_addr = (u_int *)GetFileInPak(pak_addr, (int)i);          /* 407 */

        /*
         * The original passed a 32-bit EE address here.  Keep the pointer
         * intact on the 64-bit host; tim2.c resolves uintptr_t addresses and
         * performs the actual MioPan GS upload in addition to building the
         * original PS2 DMA packet.
         */
        if (mode == 0)                                              /* 408 */
        {
            MakeTim2SendPacket_3Dpkt((uintptr_t)tm2_addr, offset);  /* 409 */
        }
        else
        {
            MakeTim2SendPacket((uintptr_t)tm2_addr, offset);        /* 411 */
        }
    }
}

void SetManmdlTm2_Mono(u_int *pak_addr, int offset, char mode, u_int *mono_addr)
{
    u_int  tm2_num;
    u_int  i;
    u_int *tm2_addr;

    (void)mode;                                                     /* unused in the ROM too */

    tm2_num = *pak_addr;                                            /* 421 */

    for (i = 0; i < tm2_num; i++)                                   /* 422 */
    {
        tm2_addr = (u_int *)GetFileInPak(pak_addr, (int)i);          /* 423 */

        MakeTim2SendPacket_Mono((uintptr_t)tm2_addr, offset,         /* 424 */
                                (uintptr_t)GetFileInPak(mono_addr, (int)i)); /* 425 */
    }
}

void BackupEneVramCtrl(void)
{
    u_int i;

    for (i = 0; i < 4; i++)                                         /* 436 */
    {
        ene_vram_bak[i] = ene_vram_ctrl[i];                         /* 437 */
    }
}

void SetupEneVramCtrl(void)
{
    u_int i;

    for (i = 0; i < 4; i++)                                         /* 443 */
    {
        ene_vram_ctrl[i] = ene_vram_bak[i];                         /* 444 */
    }
}

/* --------------------------------------------------------------------------
 *  SgdAddTexOffset
 *
 *  Walk every process-unit chain in one SGD and re-base the textures of its
 *  meshes.  A textured mesh carries a GIF A+D pair in its packet: word 12 is
 *  the GS register address and words 10/11 the 64-bit value.  Only TEX0_1
 *  writes are touched -- TBP0 sits at bit 0 of the low word and CBP at bit 37,
 *  i.e. bit 5 of the high word, so the CLUT shift is offset * 0x20.
 *
 *  Note the loop reloads uiNumBlock every iteration, exactly as the ROM does.
 * ------------------------------------------------------------------------ */
void SgdAddTexOffset(void *sgd_top, int offset)
{
    SGDFILEHEADER *sgd = (SGDFILEHEADER *)sgd_top;
    u_int          i;

    /*
     * The PS2 build remapped apProcUnitHead/pNext into absolute 32-bit
     * pointers and walked them as raw words.  The port deliberately keeps
     * those on-disc fields at four bytes, so resolve the self-relative links
     * instead of reading them as host pointers.
     */
    for (i = 0; i < sgd->uiNumBlock; i++)                           /* 461 */
    {
        SGDPROCUNITHEADER *prim = sgd->apProcUnitHead[i];            /* 462 */

        if (prim == (SGDPROCUNITHEADER *)0)
        {
            continue;
        }

        /* the terminating unit (pNext == 0) is skipped, as in the ROM */
        while (prim->pNext.get() != (SGDPROCUNITHEADER *)0)          /* 466 */
        {
            if (prim->iCategory == SPC_MESH)                        /* 471 */
            {
                u_int *packet = (u_int *)prim;

                if (packet[12] == SCE_GS_TEX0_1)                    /* 473 */
                {
                    packet[10] += (u_int)offset;                    /* 474 */
                    packet[11] += (u_int)(offset * 0x20);           /* 475 */
                }
            }

            prim = prim->pNext;                                     /* 484 */
        }
    }
}

void motInitMsn(void)
{
    motInitANI_CTRL();                                              /* 500 */

    InitEneVramCtrl();                                              /* 503 */

    InitPlyrAcsAlpha();                                             /* 509 */
}

void motInitAniMdlBufSub(ANI_MDL_CTRL *am_ctrl)
{
    am_ctrl->anm_no  = (u_short)-1;                                 /* 522 */
    am_ctrl->anm_p   = 0;                                           /* 523 */
    am_ctrl->map_flg = 0;                                           /* 524 */
    am_ctrl->pkt_no  = 0;                                           /* 525 */
}

/* --------------------------------------------------------------------------
 *  motEneTexAnm
 *
 *  Enemy texture animation: step a 7-frame ping-pong on every other field and
 *  re-upload the selected frame to whichever VRAM slot holds this model.
 *
 *  The ROM's slot search ends with an unconditional "if (i == 3) return;",
 *  which fires on the very first pass through the last slot -- what was surely
 *  meant as the not-found bail-out runs every time, so SetManmdlTm2 below is
 *  never reached in this build.  Reproduced as compiled; do not "fix" it.
 * ------------------------------------------------------------------------ */
void motEneTexAnm(ANI_CTRL *ani_ctrl, u_int work_id)
{
    static u_int cnt;                                               /* sdata 3f2ab0 */

    u_int *pk2_p;
    u_int  i;
    u_int  offset = 0;

    (void)work_id;

    if (sys_wrk.count & 1)                                          /* 538 */
    {
        cnt++;                                                      /* 539 */
    }

    i = cnt % 7;                                                    /* 540 */
    if ((cnt / 7) & 1)                                              /* 541 */
    {
        i = 7 - i;                                                  /* 542 */
    }

    pk2_p = (u_int *)GetFileInPak(ani_ctrl->tanm_p, (int)i);         /* 548 */

    for (i = 0; i < 4; i++)                                         /* 549 */
    {
        if (ene_vram_ctrl[i].mdl_p == ani_ctrl->mdl_p)              /* 550 */
        {
            offset = (u_int)ene_vram_ctrl[i].offset;                /* 551 */
        }

        if (i == 3)                                                 /* 553 */
        {
            return;
        }
    }

    SetManmdlTm2(pk2_p, (int)offset, 0);                            /* 556 */
}

void SetPlyrClut(int bw_flg)
{
    u_int *clut_addr;
    u_int  i;
    u_int  num;

    clut_addr = bw_flg == 0 ? plyr_clut_addr : plyr_bwc_addr;        /* 568 */

    if (clut_addr == (u_int *)0)                                    /* 570 */
    {
        return;
    }

    num = *clut_addr;                                               /* 573 */

    for (i = 0; i < num; i++)                                       /* 574 */
    {
        MakeTim2SendPacket((uintptr_t)GetFileInPak(clut_addr, (int)i),  /* 576 */
                           0);                                      /* 577 */
    }
}

int SendItemVram(u_int *pPk2, int offset)
{
    u_int *pTm2;
    u_int *pBwc;

    if (pPk2 == (u_int *)0)                                         /* 590 */
    {
        return 0;
    }

    pTm2 = Pk2GetAddr(pPk2, 1);                                     /* 592 */
    pBwc = Pk2GetAddr(pPk2, 2);                                     /* 593 */

    if (offset >= 0)                                                /* 595 */
    {
        /* PORT: the same bracket as SendEneVram(), keyed by the item's
         * pack. */
        MioPan_Graph3dBeginTim2Upload(pPk2, MIOPAN_TIM2_SOURCE_ITEM,
                                      gra3dIsMonotoneDrawEnable() != 0);
        if (gra3dIsMonotoneDrawEnable() != 0)                       /* 596 */
        {
            SetManmdlTm2_Mono(pTm2, offset, 0, pBwc);               /* 597 */
        }
        else
        {
            SetManmdlTm2(pTm2, offset, 0);                          /* 600 */
        }
        MioPan_Graph3dEndTim2Upload();

        return 1;
    }

    printf("Warning : SendItemVram Illegal Vram Adrs\n");            /* 605 */

    return 0;                                                       /* 606 */
}
