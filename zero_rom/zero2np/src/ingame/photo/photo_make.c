// FILE: /home/zero_rom/zero2np/src/ingame/photo/photo_make.c
//
// Photo image production -- everything between the shutter going and a
// picture existing, plus everything that later draws one back.
//
// Three separate jobs live here:
//
//   * Capture.  DrawSpecialFurnPhoto() re-draws the previous frame buffer
//     into a private VRAM page, squashing a 384x256 window of the screen 2:1
//     into a 384x128 picture; MakeSmallPhotoV() does the same thing again at
//     45x15 for the album thumbnail; CopyScreenToBuffer2() then pulls either
//     one down into EE memory a scanline at a time.
//
//   * Storage.  CompressData() runs the picture through the lossless LZSS
//     encoder first and keeps the result if it came out under 27.4% of the
//     input; if not it falls back to the lossy DCT codec at four descending
//     quality steps, and if even that fails it stores the picture as "blank".
//     UncompressData() is the mirror, driven a slice at a time by the album.
//
//   * Presentation.  DispPhotoFrame1() is the photo phase's per-frame draw:
//     the black vignette, the darkened live world behind it, the shutter
//     flash, the captured picture, and the two overlay passes that tint the
//     whole screen.  DrawPhotoFrame() puts the fourteen-sprite border round
//     it and DrawPhotoBuffer() is the primitive every album view goes
//     through.
//
// Reconstructed from the Feb 6 2004 prototype (SLES_523.84), photo_make.o.
// All 20 ZERO2.MAP .text symbols plus photo_frame[14] (.data 33cbd0) and the
// four .rodata local aggregate initialisers.
//
// Three notes on reading the annotations:
//
//   * Where two field writes share one /* NNN */ the ROM really did put them
//     on one source line -- the pri/z pairs especially, since overriding pri
//     after CopySprDToSpr() means recomputing z by hand.  Where a statement
//     landed in a branch delay slot its own line note was lost and it
//     inherited the neighbouring one; those are marked where it matters.
//
//   * DrawPhotoBuffer() converts x/y into screen-centred coordinates at the
//     top of the function and converts them straight back before use.  The
//     round trip is dead in the ROM (DispSprD2 does its own centring) and is
//     kept as found.
//
//   * DrawPhotoHinttex(), DrawSPhotoFromSmallPhotoArea() and
//     DrawSPhotoFromSmallPhotoAreaAddr() are exported and never called -- a
//     jal scan over the loadable segments finds no site for any of them.

#include "photo_make.h"

#include "photo.h"                              /* FurnPhotoFlgIsUp          */
#include "photo_dat.h"                          /* hint_dat                  */

#include "../../common/utility.h"               /* log_2                     */
#include "../../common/variable.h"              /* sys_wrk / plyr_wrk        */
#include "../../graphics/draw_env.h"            /* DRAW_ENV_5 / SetDrawEnv   */
#include "../../graphics/effect/effect_scr.h"   /* SubBlur / SubContrast2    */
#include "../../graphics/graph2d/graph2d.h"     /* effdat                    */
#include "../../graphics/graph2d/tim2.h"        /* PK2SendVram               */
#include "../../miopan/miopan_memory.h"         /* MioPan_GetHostPointer     */
#include "../../sdk/libgraph.h"                 /* SCE_GS_SET_* / SCE_GIF_*  */
#include "../../sdk/sce_gs.h"                   /* sceGsDrawEnv1             */
#include "../../system/compress/compress.h"     /* photo_expand + codec      */
#include "../../system/encodes/encodes.h"       /* SlideEncode / SlideDecode */
#include "../../system/os/system.h"             /* pdrawenv / GetDrawEnv     */

#include <stdio.h>                              /* printf                    */
#include <string.h>                             /* memcpy / memset           */
#include <math.h>                               /* sinf                      */

/* --------------------------------------------------------------------------
 *  GS register words.
 *
 *  Two ZBUFs (the mask bit is what separates "read depth" from "read and
 *  write it"), four TESTs and three ALPHAs cover the whole file.  The photo
 *  passes are the one place in the finder HUD that writes depth at all: the
 *  vignette quad lays a wedge with PHM_TEST_Z_GEQUAL and the two tint passes
 *  above it read it back with PHM_TEST_MASK_READ, which is also what keeps
 *  the tint off the transparent parts of the plate.
 * ------------------------------------------------------------------------ */
#define PHM_ZBUF_NO_WRITE   0x000000010a000118ULL
#define PHM_ZBUF_WRITE      0x000000000a000118ULL

#define PHM_TEST_ALWAYS     0x0000000000030003ULL   /* ATST ALWAYS, ZTST ALWAYS */
#define PHM_TEST_Z_GEQUAL   0x0000000000050003ULL   /* ATST ALWAYS, ZTST GEQUAL */
#define PHM_TEST_MASK_READ  0x000000000005000dULL   /* ATST GREATER, ZTST GEQUAL */
#define PHM_TEST_AFAIL_FB   0x0000000000031003ULL   /* AFAIL = FB_ONLY          */

#define PHM_ALPHA_BLEND     0x44                    /* (Cs - Cd) * As + Cd      */
#define PHM_ALPHA_ADD       0x48                    /* (Cs - 0)  * As + Cd      */
#define PHM_ALPHA_DEST      0x84                    /* (Cs - Cd) * As + 0       */
#define PHM_ALPHA_FIX80     0x0000008000000064ULL   /* (Cs - Cd) * 0x80 + Cd    */
#define PHM_ALPHA_ENV       0x0000008000000044ULL   /* draw-env source-over     */

#define PHM_TEX1            0x0000000000000161ULL
#define PHM_TEX1_ENV        0x0000000100000161ULL

/* Priorities, and the z each one turns into.  CopySprDToSpr() derives z from
 * pri, so anything that overrides pri afterwards has to redo the arithmetic
 * -- which is why these come in pairs throughout. */
#define PHM_Z(pri)          (0xfffff - ((pri) & 0xfffff))

/* --------------------------------------------------------------------------
 *  VRAM.
 *
 *  PHM_CAPTURE_TBP is the scratch texture page every stored picture is
 *  uploaded into before it is drawn -- photo.c calls the same number
 *  PHOTO_CAPTURE_ADRS.  PHM_PHOTO_FBP is the frame-buffer form of the page
 *  DrawSpecialFurnPhoto() renders the live world into, and PHM_PHOTO_TBP the
 *  texture form of the same address (FRAME pages are 32x bigger than TEX0
 *  pages, so 0x1d5 * 32 == 0x3aa0).
 *
 *  PHM_FRAME_STRIDE is the gap between the two display buffers; adding it to
 *  a TEX0 samples the frame that was being shown rather than the one being
 *  drawn.
 * ------------------------------------------------------------------------ */
#define PHM_CAPTURE_TBP     0x2bc0
#define PHM_PHOTO_FBP       0x1d5
#define PHM_PHOTO_TBP       0x3aa0
#define PHM_SPHOTO_FBP      0x15e
#define PHM_FRAME_STRIDE    0x1180

/* The screen, as the 2D path measures it. */
#define PHM_SCREEN_W        640
#define PHM_SCREEN_H        448
#define PHM_SCREEN_CX       320.0f
#define PHM_SCREEN_CY       224.0f

/* plyr_wrk.cmn_wrk.st.sta -- set while the player is in battle.  The shutter
 * flash is a third as long in a fight so the ghost is back on screen sooner. */
#define PLST_BATTLE         0x20
#define PHM_FLASH_BATTLE    19
#define PHM_FLASH_NORMAL    59

/* The lossless encoder is kept when it gets under this fraction of the input,
 * and each lossy pass has to clear the same bar.  Both live in .lit4 as
 * separate words holding the same value. */
#define PHM_COMPRESS_RATE   0.273999959f
#define PHM_QUALITY_MAX     4

/* GCC 2.96-ee truncated its float literals rather than rounding them, so this
 * is one ulp below (float)M_PI -- written out so the bits match .lit4. */
#define PHM_PI              3.1415925f      /* lit4 3ee6b0 */

/* --------------------------------------------------------------------------
 *  The photo frame -- fourteen sprites round a 384x256 picture.
 *
 *  Four 14x14 corners, then ten edge pieces cut from three source strips.
 *  Only the corners are drawn as authored: DrawPhotoFrame() rotates entries
 *  6, 7 and 9 by 180 degrees, 10 and 11 by 270 and 12 and 13 by 90, which is
 *  why their x/y look like they sit outside the frame -- they are the far
 *  end of a bar that is about to be spun round its own origin.
 *
 *  tex0 is filled in at draw time from effdat[60]; the zero here is never
 *  used.
 * ------------------------------------------------------------------------ */
                                                            /* data 33cbd0 */
static SPRT_DAT photo_frame[14] =
{
    /*  tex0    u   v    w    h     x     y  pri  alp fl bln */
    {   0,      1, 49,  14,  14,   -2,   -4,   0, 128, 0, 1 },   /* corners  */
    {   0,     17, 49,  14,  14,   -2,  246,   0, 128, 0, 1 },
    {   0,     33, 49,  14,  14,  372,   -4,   0, 128, 0, 1 },
    {   0,     49, 49,  14,  14,  372,  246,   0, 128, 0, 1 },

    {   0,      1,  1, 124,  14,   12,   -4,   0, 128, 0, 1 },   /* top      */
    {   0,      1, 17, 112,  14,  136,   -4,   0, 128, 0, 1 },
    {   0,      1,  1, 124,  14,  372,  260,   0, 128, 0, 1 },   /* bottom   */
    {   0,      1, 17, 112,  14,  248,  260,   0, 128, 0, 1 },
    {   0,      1, 33, 124,  14,  248,   -4,   0, 128, 0, 1 },   /* top      */
    {   0,      1, 33, 124,  14,  136,  260,   0, 128, 0, 1 },   /* bottom   */

    {   0,      1,  1, 124,  14,   -2,  246,   0, 128, 0, 1 },   /* left     */
    {   0,      1, 17, 112,  14,   -2,  122,   0, 128, 0, 1 },
    {   0,      1,  1, 124,  14,  386,   10,   0, 128, 0, 1 },   /* right    */
    {   0,      1, 17, 112,  14,  386,  134,   0, 128, 0, 1 },
};

/* ==========================================================================
 *  DrawPhotoBuffer (38..160)
 *
 *  Upload `mszw` x `mszh` texels from EE memory at `addr` into the VRAM
 *  scratch page, then draw them as one sprite `szw` x `szh` big at (x, y).
 *
 *  The upload is split into 100-scanline strips because a single VIF1 image
 *  transfer is limited to what one DMA tag can reference; each strip gets its
 *  own BITBLTBUF/TRXPOS/TRXREG/TRXDIR block with DSAY walking down the
 *  destination, followed by the image data itself.
 * ======================================================================== */
void DrawPhotoBuffer(u_int pri, uintptr_t addr, int szfl, int x, int y,
                     int szw, int szh, int mszw, int mszh,
                     int ftype, u_char alp, int ztype)              /* 38 */
{
    int         dbw;
    int         bw;
    int         bline;
    int         rline;
    int         oline;
    int         nloop;
    float       fh;
    float       fw;
    float       xx;
    float       yy;
    u_long      zbuf = PHM_ZBUF_NO_WRITE;
    u_long      test = PHM_TEST_ALWAYS;
    Q_WORDDATA *pbuf;
    SPRT_DAT2   sd;
    DISP_SPRT2  ds;

    (void)szfl;                 /* dead in this build */
    (void)ztype;

    /* Centred screen coordinates.  Nothing between here and line 114 reads
     * them; the ROM converts and converts straight back. */
    xx = (float)x - PHM_SCREEN_CX - 1.0f;                           /* 57 */
    yy = (float)y - PHM_SCREEN_CY - 1.0f;                           /* 58 */

    /* Destination buffer width in GS pages, rounded up. */
    dbw = (mszw - 1) / 64 + 1;                                      /* 63 */

    if (dbw * mszh * 128 > 0x80000)                                 /* 68 */
    {
        printf("warning!\n");                                       /* 69 */
    }

    bw    = dbw * 64;                                               /* 80 */
    bline = 100;                                                    /* 82 */
    rline = mszh;

    while (rline > 0)                                               /* 87 */
    {
        oline = bline < rline ? bline : rline;                      /* 90 */
        nloop = bw * oline / 4;                                     /* 91 */

        pbuf = GetPK2Dbuf();                                        /* 93 */
        pbuf[0].ul64[0] = SCE_GIF_SET_TAG(4, 1, 0, 0, SCE_GIF_PACKED, 1); /* 94 */
        pbuf[0].ul64[1] = SCE_GIF_PACKED_AD;                        /* 95 */
        pbuf[1].ul64[0] = SCE_GS_SET_BITBLTBUF(0, 0, 0,
                                               PHM_CAPTURE_TBP, dbw, 0); /* 96 */
        pbuf[1].ul64[1] = SCE_GS_BITBLTBUF;                         /* 97 */
        pbuf[2].ul64[0] = SCE_GS_SET_TRXPOS(0, 0, 0, mszh - rline, 0); /* 98 */
        pbuf[2].ul64[1] = SCE_GS_TRXPOS;                            /* 99 */
        pbuf[3].ul64[0] = SCE_GS_SET_TRXREG(bw, oline);             /* 100 */
        pbuf[3].ul64[1] = SCE_GS_TRXREG;                            /* 101 */
        pbuf[4].ul64[0] = SCE_GS_SET_TRXDIR(0);                     /* 102 */
        pbuf[4].ul64[1] = SCE_GS_TRXDIR;                            /* 103 */
        EndPK2Dbuf(&pbuf[5]);                                       /* 104 */

        /* Four texels to a quadword, sixteen bytes to a quadword. */
        SetPK2DImageTrans(addr + (uintptr_t)(bw * (mszh - rline) / 4 * 16),
                          nloop);                                   /* 107 */

        /* Host: the packet above is dropped by the DMA shim, so re-issue the
         * upload -- this is the one that puts a stored photograph where the
         * sprite below can sample it. */
        G2dHostUpload(PHM_CAPTURE_TBP, dbw, SCE_GS_PSMCT32, 0, mszh - rline,
                      bw, oline,
                      MioPan_GetHostPointer(addr +
                          (uintptr_t)(bw * (mszh - rline) * 4)));

        rline -= oline;                                             /* 109 */
    }

    xx += PHM_SCREEN_CX;                                            /* 114 */
    yy += PHM_SCREEN_CY;                                            /* 115 */

    /* sd is deliberately left uninitialised, exactly as the ROM leaves it:
     * every field CopySprDToSpr2() reads out of it is overwritten below
     * except pri, and DispSprD2() never looks at pri. */
    CopySprDToSpr2(&ds, &sd);                                       /* 121 */
    ds.tex0 = SCE_GS_SET_TEX0(PHM_CAPTURE_TBP, dbw, 1,
                              log_2((u_int)bw), log_2((u_int)mszh),
                              0, 0, 0, 0, 0, 0, 1);                 /* 122 */

    if (ftype != 1)                                                 /* 123 */
    {
        ds.u1 = 0;              ds.v1 = 0;                          /* 124 */
        ds.u2 = mszw * 16.0f;   ds.v2 = mszh * 16.0f;               /* 125 */
    }
    else
    {
        /* Inset half a texel on every side so the border art can overlap
         * without dragging in the edge row. */
        ds.u1 = 0x80;                       ds.v1 = 0x80;           /* 127 */
        ds.u2 = (mszw - 8.0f) * 16.0f;      ds.v2 = (mszh - 8.0f) * 16.0f; /* 128 */
    }

    ds.w      = (float)szw;   ds.h = (float)szh;                    /* 130 */
    ds.x      = xx;           ds.y = yy;                            /* 131 */
    ds.alpreg = PHM_ALPHA_FIX80;                                    /* 132 */
    ds.alp    = alp;                                                /* 134 */
    ds.zbuf   = zbuf;                                               /* 135 */
    ds.test   = test;                                               /* 136 */
    ds.z      = 0xfff00;                                            /* 137 */
    ds.r      = 0x80;  ds.g = 0x80;  ds.b = 0x80;                   /* 138 */
    DispSprD2(&ds);                                                 /* 139 */

    switch (ftype)                                                  /* 143 */
    {
    case 2:
        /* A sepia border whose thickness grows with the picture: two pixels
         * for a thumbnail, six for a full-size photo.  The ROM computes the
         * same clamp twice, once for each axis. */
        fh = (szw - 48.0f) / 84.0f + 2.0f;                          /* 148 */

        fw = fh < 2.0f ? 2.0f : fh;                                 /* 150 */
        fh = fh < 2.0f ? 2.0f : fh;                                 /* 151 */

        SetPanel(pri, (xx - fw) + 2.0f, (yy - fh) + 1.0f,
                 (xx + szw + fw) - 2.0f, yy + 1.0f,
                 0xcf, 0xbd, 0xa1, alp);                            /* 153 */
        SetPanel(pri, (xx - fw) + 2.0f, (yy + szh) - 1.0f,
                 (xx + szw + fw) - 2.0f, (yy + szh + fh) - 1.0f,
                 0xcf, 0xbd, 0xa1, alp);                            /* 154 */
        SetPanel(pri, (xx - fw) + 2.0f, yy + 1.0f,
                 xx + 2.0f, (yy + szh) - 1.0f,
                 0xcf, 0xbd, 0xa1, alp);                            /* 155 */
        SetPanel(pri, (xx + szw) - 2.0f, yy + 1.0f,
                 (xx + szw + fw) - 2.0f, (yy + szh) - 1.0f,
                 0xcf, 0xbd, 0xa1, alp);                            /* 156 */
        break;                                                      /* 157 */

    case 3:
        DrawPhotoFrame(xx, yy);                                     /* 159 */
        break;
    }
}                                                                   /* 160 */

/* ==========================================================================
 *  CopyScreenToBuffer2 (178..205)
 *
 *  GS local memory -> EE memory.  LocalCopyLtoB() does the transfer itself,
 *  into the shared effect scratch buffer; the loop below then lifts the
 *  requested rectangle out of that buffer and repacks it at the destination's
 *  page-aligned stride.
 *
 *  The source row index is halved because the read-back lands as a single
 *  field: the 448-line frame arrives as 224 lines.
 * ======================================================================== */
void CopyScreenToBuffer2(int addr_i, int addr_o, int szfl,
                         int mx, int my, int mw, int mh)            /* 178 */
{
    int x;
    int y;
    int oneli = PHM_SCREEN_W;
    int onelo;
    int myy;
    u_char *src;
    u_char *dst;

    (void)szfl;                 /* dead in this build */

    myy   = my / 2;                                                 /* 183 */
    onelo = (mw - 1) / 64 * 64 + 64;                                /* 188 */

    PK2DKick();                                                     /* 195 */

    LocalCopyLtoB(3, 1, addr_i);                                    /* 198 */

    /* Both ends of the repack are EE memory-map constants -- the shared effect
     * scratch LocalCopyLtoB() just filled, and the caller's work area.  Resolve
     * them to the emulated RAM before dereferencing either. */
    src = (u_char *)MioPan_GetHostPointer(EFFECT_WRK2_ADDR);
    dst = (u_char *)MioPan_GetHostPointer((uintptr_t)addr_o);

    for (y = 0; y < mh; y++)                                        /* 201 */
    {
        for (x = 0; x < mw; x++)                                    /* 202 */
        {
            *(u_int *)(dst + (y * onelo + x) * 4) =
                *(u_int *)(src + ((myy + y) * oneli + mx + x) * 4);
                                                                    /* 203 */
        }                                                           /* 204 */
    }                                                               /* 205 */
}

/* ==========================================================================
 *  MakeSmallPhotoV (230..292)
 *
 *  Build the album thumbnail straight in VRAM.  A one-sprite render target at
 *  PHM_SPHOTO_FBP takes the 384x256 window of the previous frame buffer that
 *  the photo covers and scales it down to 45x15 -- the thumbnail is stored at
 *  half its display height, the same 2:1 squash the full picture uses.
 *
 *  The frame-buffer, offset and texture registers are pushed by hand and then
 *  restored from the live draw env by the three-register block at the end.
 * ======================================================================== */
void MakeSmallPhotoV(float x, float y)                              /* 230 */
{
    Q_WORDDATA *ppbuf;

    /* Depth off (mask set), keep the live ZBP/PSM, and AFAIL = FB_ONLY so the
     * copy cannot disturb the depth buffer it is drawing over. */
    DRAW_ENV_5 de =                                                 /* 235 */
    {
        PHM_ALPHA_BLEND,
        PHM_TEX1_ENV,
        0,
        PHM_TEST_AFAIL_FB,
        (*(u_long *)&((sceGsDrawEnv1 *)pdrawenv)->zbuf1 & 0x000000000f0001ffULL)
            | 0x0000000100000000ULL
    };

    SetDrawEnv(0, &de);                                             /* 242 */

    ppbuf = GetPK2Dbuf();                                           /* 245 */

    ppbuf[0].ul64[0] = SCE_GIF_SET_TAG(4, 1, 0, 0, SCE_GIF_PACKED, 1); /* 248 */
    ppbuf[0].ul64[1] = SCE_GIF_PACKED_AD;                           /* 249 */

    ppbuf[1].ul64[0] = 0;                                           /* 251 */
    ppbuf[1].ul64[1] = SCE_GS_TEXFLUSH;                             /* 252 */

    ppbuf[2].ul64[0] = SCE_GS_SET_FRAME(PHM_SPHOTO_FBP, 6, 1, 0);   /* 255 */
    ppbuf[2].ul64[1] = SCE_GS_FRAME_1;                              /* 256 */

    ppbuf[3].ul64[0] = SCE_GS_SET_XYOFFSET(0x6c08, 0x7908);         /* 258 */
    ppbuf[3].ul64[1] = SCE_GS_XYOFFSET_1;                           /* 259 */

    /* Sample the frame that was on screen, not the one being drawn. */
    ppbuf[4].ul64[0] = SCE_GS_SET_TEX0(0, 10, 1, 10, 9, 1, 1, 0, 0, 0, 0, 0)
                     | ((u_long)(((u_int)sys_wrk.count + 1) & 1) * PHM_FRAME_STRIDE);
                                                                    /* 261 */
    ppbuf[4].ul64[1] = SCE_GS_TEX0_1;                               /* 262 */

    ppbuf[5].ul64[0] = SCE_GIF_SET_TAG(1, 1, 1,
                                       SCE_GS_SET_PRIM(SCE_GS_PRIM_SPRITE,
                                                       0, 1, 0, 0, 0, 1, 0, 0),
                                       SCE_GIF_PACKED, 4);          /* 265 */
    ppbuf[5].ul64[1] = (u_long)SCE_GIF_PACKED_UV
                     | ((u_long)SCE_GIF_PACKED_XYZF2 << 4)
                     | ((u_long)SCE_GIF_PACKED_UV    << 8)
                     | ((u_long)SCE_GIF_PACKED_XYZF2 << 12);        /* 268 */

    ppbuf[6].ui32[0] = (int)x * 16 + 8;                             /* 270 */
    ppbuf[6].ui32[1] = (int)y * 16 + 8;                             /* 271 */

    ppbuf[7].ui32[0] = 0x6c00;                                      /* 273 */
    ppbuf[7].ui32[1] = 0x7900;                                      /* 274 */
    ppbuf[7].ul64[1] = 0;                                           /* 275 */

    ppbuf[8].ui32[0] = (int)x * 16 + 0x17f8;                        /* 278 */
    ppbuf[8].ui32[1] = (int)y * 16 + 0xff8;                         /* 279 */

    ppbuf[9].ui32[0] = 0x6ed0;                                      /* 281 */
    ppbuf[9].ui32[1] = 0x79f0;                                      /* 282 */
    ppbuf[9].ul64[1] = 0;                                           /* 283 */

    /* Put FRAME, ZBUF and XYOFFSET back the way the frame had them. */
    ppbuf[10].ul64[0] = SCE_GIF_SET_TAG(3, 1, 0, 0, SCE_GIF_PACKED, 1); /* 286 */
    ppbuf[10].ul64[1] = SCE_GIF_PACKED_AD;                          /* 287 */

    ppbuf[11].ul128 = ((Q_WORDDATA *)pdrawenv)[0].ul128;            /* g3dxVu0.h 134/135 */
    ppbuf[12].ul128 = ((Q_WORDDATA *)pdrawenv)[1].ul128;
    ppbuf[13].ul128 = ((Q_WORDDATA *)pdrawenv)[2].ul128;

    EndPK2Dbuf(&ppbuf[14]);                                         /* 292 */

    /* Host: the packet above is dropped by the DMA shim, so record what it
     * would have put in the page.  PHM_SPHOTO_FBP is PHM_CAPTURE_TBP in frame
     * units (0x15e * 32), and CopyScreenToBuffer2() reads the 45x15 corner
     * straight back out of it on the next line of PictureCapture(). */
    G2dRegisterScreenPage(PHM_CAPTURE_TBP, (int)x, (int)y, PHOTO_W, PHOTO_H * 2,
                          SPHOTO_W, SPHOTO_H);
}

/* ==========================================================================
 *  The album thumbnail wrappers (329..368)
 *
 *  A thumbnail is stored 45x15 and drawn 45x30 unless the caller says
 *  otherwise, which is the 2:1 squash the capture path applied.
 * ======================================================================== */
void DrawSPhotoFromSmallPhotoArea(int n, int pri, int ftype, int x, int y,
                                  u_char alp)                       /* 329 */
{
    DrawPhotoBuffer(pri, SPHOTO_AREA + n * SPHOTO_ONE_SIZE, 0, x, y, /* 330 */
                    SPHOTO_W, SPHOTO_H * 2, SPHOTO_W, SPHOTO_H,
                    ftype, alp, 0);                                 /* 332 */
}

void DrawSPhotoFromSmallPhotoAreaAddr(uintptr_t addr, int n, int pri, int ftype,
                                      int x, int y, u_char alp)     /* 336 */
{
    uintptr_t addr2 = addr + n * SPHOTO_ONE_SIZE;                   /* 337 */

    DrawPhotoBuffer(pri, addr2, 0, x, y,
                    SPHOTO_W, SPHOTO_H * 2, SPHOTO_W, SPHOTO_H,
                    ftype, alp, 0);                                 /* 339 */
}

void DrawSPhotoFromSmallPhotoArea2(int n, int pri, int ftype, int x, int y,
                                   int szw, int szh, u_char alp)    /* 358 */
{
    DrawPhotoBuffer(pri, SPHOTO_AREA + n * SPHOTO_ONE_SIZE, 0, x, y, /* 359 */
                    szw, szh, SPHOTO_W, SPHOTO_H,
                    ftype, alp, 0);                                 /* 361 */
}

void DrawSPhotoFromSmallPhotoAreaAddr2(uintptr_t addr, int n, int pri, int ftype,
                                       int x, int y, int szw, int szh,
                                       int alp)                     /* 365 */
{
    uintptr_t addr2 = addr + n * SPHOTO_ONE_SIZE;                   /* 366 */

    DrawPhotoBuffer(pri, addr2, 0, x, y, szw, szh,
                    SPHOTO_W, SPHOTO_H,
                    ftype, (u_char)alp, 0);                         /* 368 */
}

/* ==========================================================================
 *  CompressData (385..423)
 *
 *  Encode the picture sitting at `addri` and file it in slot `n`.
 *
 *  The lossless (kagyaku) LZSS pass runs first and is kept when it gets under
 *  PHM_COMPRESS_RATE of the input.  If it does not, the lossy (hikagyaku) DCT
 *  codec is run at quality 1 and then at successively worse settings until it
 *  clears the same bar; if quality 4 still cannot, the header is marked type
 *  2 -- "no image" -- and the slot decodes to black.
 *
 *  Both codecs write into the scratch area immediately after the picture, so
 *  a failed pass costs nothing but time.
 * ======================================================================== */
void CompressData(int addri, int addro, int n)                      /* 385 */
{
    /* Both addresses are EE memory-map constants; everything below writes
     * through them, so they have to be resolved to the emulated RAM first. */
    u_char *in  = (u_char *)MioPan_GetHostPointer((uintptr_t)addri);
    u_char *out = (u_char *)MioPan_GetHostPointer((uintptr_t)addro);

    SLIDE_ENCODE_HEADER *sheader = (SLIDE_ENCODE_HEADER *)(in + PHOTO_RAW_SIZE);
    int    one_size = PHOTO_ONE_SIZE;
    u_int  quality;
    float  rate;

    printf("kagyaku compress\n");                                   /* 390 */

    rate = SlideEncodeHeader(in, (u_char *)sheader,
                             PHOTO_RAW_SIZE);                       /* 393 */
    printf("kagayku_rate == %f\n", (double)rate);                   /* 394 */

    if (rate > PHM_COMPRESS_RATE)                                   /* 397 */
    {
        quality = 1;                                                /* 398 */

        while (1)                                                   /* 401 */
        {
            printf("hikagyaku compress\n");                         /* 402 */
            memset((char *)sheader, 0xff, PHOTO_ENCODE_SIZE);       /* 403 */

            rate = CompressFile((u_int *)in, (char *)sheader,
                                PHOTO_ENCODE_SIZE, (char)quality++); /* 405 */
            if (rate < PHM_COMPRESS_RATE)                           /* 406 */
            {
                break;
            }

            if (quality > PHM_QUALITY_MAX)                          /* 407 */
            {
                printf("Warning : 写真圧縮に失敗しました\n");
                                        /* 409  "photo compression failed" */
                printf("Out/In : %f\n", (double)rate);              /* 410 */
                sheader->type = 2;                                  /* 411 */
                break;                                              /* 412 */
            }

            printf("Out/In  : %f\n", (double)rate);                 /* 414 */
            printf("Warning : quality down !!!\n");                 /* 415 */
        }
    }

    memcpy((void *)(out + n * one_size), (void *)sheader, one_size); /* 422 */
    printf("compress end\n");                                       /* 423 */
}

/* ==========================================================================
 *  UncompressData (438..471)
 *
 *  The mirror.  The LZSS and blank cases finish in one call and post
 *  photo_expand.sta = 2 themselves; the DCT case runs thirty macroblock rows
 *  per call and lets ExpandFile() post the completion when it reaches the end
 *  of the picture, which is what makes it a multi-frame job.
 * ======================================================================== */
void UncompressData(uintptr_t addri, int n, int addro)              /* 438 */
{
    int      one_size = PHOTO_ONE_SIZE;
    u_int    i;
    u_char  *base;
    /* EE memory-map constants on both sides, as in CompressData(). */
    u_char  *in  = (u_char *)MioPan_GetHostPointer(addri);
    u_char  *out = (u_char *)MioPan_GetHostPointer((uintptr_t)addro);

    if (n == 0xff)                                                  /* 447 */
    {
        n = (char)GetPhotoExpandNo();                               /* 448 */
    }

    base = in + n * one_size;                                       /* 452 */

    switch (((SLIDE_ENCODE_HEADER *)base)->type)                    /* 454 */
    {
    case 0:
        SlideDecodeHeader(base, out);                               /* 457 */
        photo_expand.sta = 2;                                       /* 458 */
        photo_expand.cnt = 0;                                       /* 459 */
        break;                                                      /* 460 */

    case 1:
        for (i = 0; i < 30; i++)                                    /* 463 */
        {
            ExpandFile((char *)(in + n * one_size), (u_int *)out);  /* 464 */
        }                                                           /* 465 */
        break;

    case 2:
        memset((void *)out, 0, PHOTO_ENCODE_SIZE);                  /* 468 */
        photo_expand.sta = 2;                                       /* 469 */
        photo_expand.cnt = 0;                                       /* 470 */
        break;
    }
}                                                                   /* 471 */

/* ==========================================================================
 *  The fixed-address wrappers (488..551)
 * ======================================================================== */
void CompPhotoFromWorkArea(int n)                                   /* 488 */
{
    CompressData(PACKET2D_ADDR, PHOTO_AREA, n);                     /* 489 */
}

void UncompressPhoto(int n)                                         /* 502 */
{
    ReqPhotoExpand((u_char)n);                                      /* 503 */
}

void DrawPhotoFromWorkArea(int pri, int ftype, int x, int y,
                           int szw, int szh, u_char alp)            /* 520 */
{
    if (CheckPhotoExpandEnd())                                      /* 521 */
    {
        DrawPhotoBuffer(pri, PACKET2D_ADDR, 0, x, y, szw, szh,
                        PHOTO_W, PHOTO_H, ftype, alp, 0);           /* 522 */
    }
    else if (GetPhotoExpand() == 1)                                 /* 531 */
    {
        UncompressData(PHOTO_AREA, 0xff, PACKET2D_ADDR);            /* 532 */
    }
}

void DrawPhotoFromWorkAreaAddr(uintptr_t addr, int pri, int ftype, int x, int y,
                               int szw, int szh, u_char alp)        /* 537 */
{
    uintptr_t addr2 = addr + 0x10000;                               /* 538 */

    if (CheckPhotoExpandEnd())                                      /* 540 */
    {
        DrawPhotoBuffer(pri, PACKET2D_ADDR, 0, x, y, szw, szh,
                        PHOTO_W, PHOTO_H, ftype, alp, 0);           /* 541 */
    }
    else if (GetPhotoExpand() == 1)                                 /* 550 */
    {
        UncompressData(addr2, 0xff, PACKET2D_ADDR);                 /* 551 */
    }
}

/* ==========================================================================
 *  The SLIDE_ENCODE_HEADER wrappers (566..579)
 * ======================================================================== */
float SlideEncodeHeader(u_char *base, u_char *addrs, int max_size)  /* 566 */
{
    SLIDE_ENCODE_HEADER *sheader = (SLIDE_ENCODE_HEADER *)addrs;

    sheader->type = 0;
    sheader->size = SlideEncode(base, addrs + sizeof(SLIDE_ENCODE_HEADER),
                                max_size);                          /* 570 */

    return (float)sheader->size / (float)max_size;                  /* 572 */
}

void SlideDecodeHeader(u_char *base, u_char *addrs)                 /* 576 */
{
    SlideDecode(base + sizeof(SLIDE_ENCODE_HEADER), addrs,
                ((SLIDE_ENCODE_HEADER *)base)->size);               /* 579 */
}

/* ==========================================================================
 *  DispPhotoFrame1 (585..734)
 *
 *  One frame of the photo presentation, in five layers:
 *
 *    fl == 3 is the tail: a single full-screen blend of the previous frame
 *    over itself, ramping down over 32 frames, which is what makes the
 *    picture bleed away rather than cut.
 *
 *    Everything else builds the presentation proper -- a black quad the size
 *    of the picture laid *with* depth writes, the darkened live world behind
 *    it, the shutter flash (a sine-driven blur and contrast pass, shorter in
 *    battle), the captured picture if this shot had a subject, and finally
 *    two full-screen passes that read that depth wedge back so the tint stops
 *    at the edge of the picture.
 * ======================================================================== */
void DispPhotoFrame1(int fl, float x, float y, int iPercent)        /* 585 */
{
    static int    cnt;                                      /* sdata 3f38d0 */
    static u_char alp1;                                     /* sdata 3f38d4 */
    static u_char alp2;                                     /* sdata 3f38d5 */
    static int    flash_fr;                                 /* sdata 3f38d8 */
    static int    fl_cnt;                                   /* sdata 3f38dc */

    int rgb;

    if (fl == 0)                                                    /* 596 */
    {
        cnt      = 0;                                               /* 597 */
        alp1     = 128;                                             /* 598 */
        alp2     = 0;                                               /* 599 */
        flash_fr = (plyr_wrk.cmn_wrk.st.sta & PLST_BATTLE)
                       ? PHM_FLASH_BATTLE : PHM_FLASH_NORMAL;       /* 600 */
        fl_cnt   = flash_fr;                                        /* 601 */
    }

    rgb = iPercent * 191 / 100 + 64;                                /* 603 */

    if (fl == 3)                                                    /* 606 */
    {
        SPRT_DAT  sd = { 0, 0, 0, PHM_SCREEN_W, PHM_SCREEN_H,
                         0, 0, 0, 128, 0, 1 };                      /* 610 */
        DISP_SPRT ds;
        int       i = 128 - alp2 * 4;                               /* 612 */

        CopySprDToSpr(&ds, &sd);                                    /* 616 */
        ds.tex0   = SCE_GS_SET_TEX0(0, 10, 1, 10, 9, 0, 0, 0, 0, 0, 0, 1)
                  | ((u_long)(((u_int)sys_wrk.count + 1) & 1) * PHM_FRAME_STRIDE);
                                                                    /* 617 */
        ds.zbuf   = PHM_ZBUF_WRITE;                                 /* 619 */
        ds.test   = PHM_TEST_Z_GEQUAL;                              /* 620 */
        ds.alphar = PHM_ALPHA_BLEND;                                /* 621 */
        ds.tex1   = PHM_TEX1;                                       /* 622 */
        ds.x      = -0.5f;   ds.y = -1.0f;                          /* 623 */
        ds.alpha  = (u_char)i;                                      /* 624 */
        ds.r      = 0x80;    ds.g = 0x80;   ds.b = 0x80;            /* 625 */
        ds.z      = 0xffff0;                                        /* 626 */

        DispSprD(&ds);                                              /* 628 */

        if (++alp2 > 32)                                            /* 629 */
        {
            alp2 = 32;
        }
    }
    else                                                            /* 631 */
    {
        {
            SQAR_DAT  sq = { PHM_SCREEN_W, PHM_SCREEN_H, 0, 0, 0, 0, 0, 0, 128 };
                                                                    /* 637 */
            DISP_SQAR dq;
            int       i;

            /* A black rectangle exactly the size of the picture, drawn with
             * depth writes on: it is the stencil the two tint passes below
             * read back. */
            CopySqrDToSqr(&dq, &sq);                                /* 640 */
            dq.pri  = 0xb0;   dq.z = PHM_Z(0xb0);                   /* 641 */
            dq.zbuf = PHM_ZBUF_WRITE;                               /* 642 */
            dq.test = PHM_TEST_ALWAYS;                              /* 643 */
            dq.x[0] = (int)x;   dq.y[0] = (int)y;                   /* 644 */
            dq.x[1] = dq.x[0] + PHOTO_W;                            /* 645 */
            dq.y[2] = dq.y[0] + PHOTO_H * 2;                        /* 646 */

            for (i = 0; i < 4; i++)                                 /* 647 */
            {
                dq.r[i] = 0;   dq.g[i] = 0;   dq.b[i] = 0;
            }

            dq.alpha = 128;                                         /* 648 */
            dq.x[2] = dq.x[0];   dq.x[3] = dq.x[1];
            dq.y[1] = dq.y[0];   dq.y[3] = dq.y[2];
            DispSqrD(&dq);                                          /* 649 */
        }

        {
            /* The live world, at half width and stretched back out, dimmed to
             * `rgb`.  This is the "world still visible behind the photo"
             * layer -- iPercent is what fades it. */
            SPRT_DAT  sd = { 0, 0, 0, PHM_SCREEN_W / 2, PHM_SCREEN_H,
                             0, 0, 0, 128, 0, 1 };                  /* 655 */
            DISP_SPRT ds;

            CopySprDToSpr(&ds, &sd);                                /* 657 */
            ds.tex0   = SCE_GS_SET_TEX0(PHM_CAPTURE_TBP, 5, 1, 9, 9,
                                        0, 0, 0, 0, 0, 0, 1);       /* 658 */
            ds.zbuf   = PHM_ZBUF_NO_WRITE;                          /* 659 */
            ds.test   = PHM_TEST_ALWAYS;                            /* 660 */
            ds.alphar = PHM_ALPHA_BLEND;                            /* 661 */
            ds.z      = PHM_Z(0x10);   ds.pri = 0x10;               /* 662 */
            ds.csx    = -0.5f;   ds.csy = -1.0f;
            ds.x      = -0.5f;   ds.y   = -1.0f;                    /* 663 */
            ds.scw    = 2.0f;    ds.sch = 1.0f;                     /* 664 */
            ds.r      = (u_char)rgb;
            ds.g      = (u_char)rgb;
            ds.b      = (u_char)rgb;                                /* 665 */
            DispSprD(&ds);                                          /* 666 */
        }

        /* The shutter flash: one half-cycle of a sine over flash_fr frames,
         * driving a radial blur and a contrast lift at different depths. */
        if (fl == 1 && fl_cnt > 0)                                  /* 669 */
        {
            int i = (int)(sinf((float)(fl_cnt * 90 / flash_fr) * PHM_PI / 180.0f)
                          * 160.0f);                                /* 670 */
            int j = (int)(sinf((float)(fl_cnt * 90 / flash_fr) * PHM_PI / 180.0f)
                          * 80.0f);                                 /* 671 */

            SubBlur(1, (u_char)j, 1.0f, 180.0f, 320.0f, 112.0f, 1); /* 672 */
            SubContrast2((u_char)i, (u_char)i);                     /* 673 */

            fl_cnt--;                                               /* 674 */
        }

        if (FurnPhotoFlgIsUp())                                     /* 678 */
        {
            /* The captured picture: 384x128 in VRAM, drawn at 2x height so it
             * fills the 384x256 hole the black quad left. */
            SPRT_DAT  sd = { 0, 0, 0, PHOTO_W, PHOTO_H,
                             128, 128, 128, 128, 0, 1 };            /* 681 */
            DISP_SPRT ds;

            CopySprDToSpr(&ds, &sd);                                /* 683 */
            ds.tex0   = SCE_GS_SET_TEX0(PHM_PHOTO_TBP, 6, 0, 9, 7,
                                        0, 0, 0, 0, 0, 0, 1);       /* 684 */
            ds.zbuf   = PHM_ZBUF_NO_WRITE;                          /* 685 */
            ds.test   = PHM_TEST_ALWAYS;                            /* 686 */
            ds.alphar = PHM_ALPHA_BLEND;                            /* 687 */
            ds.z      = PHM_Z(0x60);   ds.pri = 0x60;               /* 688 */
            ds.csx    = x - 0.5f;      ds.csy = y - 1.0f;           /* 691 */
            ds.scw    = 1.0f;          ds.sch = 2.0f;               /* 692 */
            ds.x      = ds.csx;        ds.y   = ds.csy;
            ds.alpha  = alp1;                                       /* 693 */
            DispSprD(&ds);                                          /* 694 */

            if (fl == 2)                                            /* 695 */
            {
                alp1 = (u_char)(alp1 - 4 < 0 ? 0 : alp1 - 4);       /* 696 */
            }
        }

        {
            /* First tint pass: the live world again at quarter brightness,
             * masked to where the black quad wrote depth. */
            SPRT_DAT  sd = { 0, 0, 0, PHM_SCREEN_W, PHM_SCREEN_H,
                             0, 0, 0, 128, 0, 1 };                  /* 703 */
            DISP_SPRT ds;

            CopySprDToSpr(&ds, &sd);                                /* 705 */
            ds.tex0   = SCE_GS_SET_TEX0(PHM_CAPTURE_TBP, 5, 1, 9, 9,
                                        0, 0, 0, 0, 0, 0, 1);       /* 706 */
            ds.zbuf   = PHM_ZBUF_WRITE;                             /* 707 */
            ds.test   = PHM_TEST_MASK_READ;                         /* 708 */
            ds.alphar = PHM_ALPHA_BLEND;                            /* 709 */
            ds.z      = PHM_Z(0xd0);   ds.pri = 0xd0;               /* 710 */
            ds.csx    = -0.5f;   ds.csy = -1.0f;
            ds.x      = -0.5f;   ds.y   = -1.0f;                    /* 711 */
            ds.scw    = 2.0f;    ds.sch = 1.0f;                     /* 712 */
            ds.r      = 0x40;    ds.g = 0x40;   ds.b = 0x40;        /* 713 */
            DispSprD(&ds);                                          /* 714 */
        }

        {
            /* Second tint pass: a flat grey wash over the same mask, blended
             * against the destination alpha rather than the source. */
            SQAR_DAT  sq = { PHM_SCREEN_W, PHM_SCREEN_H, 0, 0, 0, 0, 0, 0, 128 };
                                                                    /* 719 */
            DISP_SQAR dq;
            int       i;

            CopySqrDToSqr(&dq, &sq);                                /* 722 */
            dq.zbuf   = PHM_ZBUF_WRITE;                             /* 723 */
            dq.test   = PHM_TEST_MASK_READ;                         /* 724 */
            dq.alphar = PHM_ALPHA_DEST;                             /* 725 */
            dq.z      = PHM_Z(0xc0);   dq.pri = 0xc0;               /* 726 */

            for (i = 0; i < 4; i++)                                 /* 727 */
            {
                dq.r[i] = 0x30;   dq.g[i] = 0x30;   dq.b[i] = 0x30;
            }

            dq.alpha = 200;                                         /* 728 */
            DispSqrD(&dq);                                          /* 729 */
        }

        SubFadeFrame(0x60, 0x90);                                   /* 733 */
    }
}                                                                   /* 734 */

/* ==========================================================================
 *  DrawPhotoFilterPK2 (738..826)
 *
 *  Cross-fade a hint plate over the picture.  `sw` is the direction -- 0 in,
 *  1 hold, 2 out -- and photo_cnt the phase's own 50-frame countdown, so the
 *  same call site drives the whole fade by changing sw as the phase advances.
 *
 *  bGradual replaces the straight fade with a four-way blur: the same plate
 *  drawn at quarter alpha in four corners, spread by up to three pixels while
 *  it is faint and converging as it comes up to full.
 * ======================================================================== */
void DrawPhotoFilterPK2(u_int sw, float x, float y, const SPRT_DAT *dat,
                        int photo_cnt, void *tex_adrs, int bGradual) /* 738 */
{
    DISP_SPRT ds;
    int       alp = 0;                                              /* 741 */
    int       time;

    switch (sw)                                                     /* 744 */
    {
    case 0:
        if (photo_cnt > 50) { alp = dat->alpha; } else              /* 746 */
        {
            time = 50 - photo_cnt;                                  /* 747 */
            alp  = dat->alpha * time / 50;                          /* 748 */
        }
        break;

    case 2:
        if (photo_cnt > 50) { alp = dat->alpha; } else              /* 757 */
        {
            alp = dat->alpha * photo_cnt / 50;                      /* 758 */
        }
        break;

    case 1: alp = dat->alpha; break;                                /* 760 */
    }

    if (tex_adrs != NULL)                                           /* 785 */
    {
        PK2SendVram((uintptr_t)tex_adrs, -1, -1, 0);                /* 786 */
    }

    if (bGradual)                                                   /* 789 */
    {
        /* Three pixels apart at zero alpha, converging to nothing at full. */
        float d = (1.0f - (float)alp / (float)dat->alpha) * 3.0f;   /* 790, 791 */

        alp >>= 2;                                                  /* 793 */

        CopySprDToSpr(&ds, (SPRT_DAT*)dat);                                    /* 794 */
        ds.tex1   = PHM_TEX1;                                       /* 795 */
        ds.zbuf   = PHM_ZBUF_NO_WRITE;                              /* 796 */
        ds.test   = PHM_TEST_ALWAYS;                                /* 797 */
        ds.x      = x;    ds.y   = y;                               /* 798 */
        ds.csx    = x;    ds.csy = y;
        ds.scw    = 384.0f / (float)dat->w;
        ds.sch    = (float)(256 / dat->h);                          /* 799 */
        ds.alphar = PHM_ALPHA_ADD;                                  /* 800 */
        ds.alpha  = (u_char)alp;                                    /* 802 */

        ds.x = x - d;   ds.y = y - d;                               /* 805 */
        DispSprD(&ds);                                              /* 806 */

        ds.x = x + d;   ds.y = y + d;                               /* 808 */
        DispSprD(&ds);                                              /* 809 */

        ds.x = x - d;                                               /* 811 */
        DispSprD(&ds);                                              /* 812 */

        ds.x = x + d;   ds.y = y - d;                               /* 814 */
        DispSprD(&ds);                                              /* 815 */
    }
    else
    {
        CopySprDToSpr(&ds, (SPRT_DAT*)dat);                                    /* 817 */
        ds.tex1   = PHM_TEX1;                                       /* 818 */
        ds.zbuf   = PHM_ZBUF_NO_WRITE;                              /* 819 */
        ds.test   = PHM_TEST_ALWAYS;                                /* 820 */
        ds.x      = x;    ds.y   = y;                               /* 821 */
        ds.csx    = x;    ds.csy = y;
        ds.scw    = 384.0f / (float)dat->w;
        ds.sch    = (float)(256 / dat->h);                          /* 822 */
        ds.alphar = PHM_ALPHA_ADD;                                  /* 823 */
        ds.alpha  = (u_char)alp;                                    /* 825 */
        DispSprD(&ds);                                              /* 826 */
    }
}

/* ==========================================================================
 *  DrawPhotoHinttex (835..929)
 *
 *  The hint-plate cross-fade against hint_dat[].  Same shape as
 *  DrawPhotoFilterPK2()'s gradual path -- a black quad underneath, then the
 *  plate four times -- but the alpha is quartered up front and the spread is
 *  derived from how far the fade still has to go.
 *
 *  Exported and dead: no jal site for it anywhere in the loadable segments.
 * ======================================================================== */
void DrawPhotoHinttex(u_int sw, float x, float y, int photo_cnt,
                      int photo_spno, u_int *tex_adrs)              /* 835 */
{
    DISP_SPRT ds;
    DISP_SQAR dq;
    SPRT_DAT *sd;
    u_char    alp  = 0;                                             /* 838 */
    u_char    alp2 = 0;                                             /* 838 */
    int       time;
    int       max = 0;                                              /* 840 */
    int       i;
    float     pos;
    float     f;

    sd = &hint_dat[photo_spno];                                     /* 857 */

    /* `max` is never assigned, so alp2 -- the quad's alpha below -- is always
     * zero and the plate is drawn over nothing.  Reproduced as found; it is
     * the other half of why this function is dead. */
    switch (sw)                                                     /* 858 */
    {
    case 0:
        if (photo_cnt > 50) { alp = (u_char)(sd->alpha / 4); } else /* 860 */
        {
            time = 50 - photo_cnt;                                  /* 861 */
            alp  = (u_char)(u_int)((float)sd->alpha * 0.25f
                                   * (float)time / 50.0f);          /* 862 */
            alp2 = (u_char)(time * max / 50);                       /* 863 */
        }
        break;

    case 2:
        if (photo_cnt > 50) { alp = (u_char)(sd->alpha / 4); } else /* 874 */
        {
            alp  = (u_char)(u_int)((float)sd->alpha * 0.25f
                                   * (float)photo_cnt / 50.0f);     /* 876 */
            alp2 = (u_char)(photo_cnt * max / 50);                  /* 877 */
        }
        break;

    case 1: alp = (u_char)(sd->alpha / 4); break;                   /* 879 */
    }

    /* A black quad the size of the picture, drawn at alp2 -- i.e. invisible.
     * It exists to lay the depth wedge the plate is then clipped to. */
    {
        SQAR_DAT sq = { PHM_SCREEN_W, PHM_SCREEN_H, 0, 0, 0, 0, 0, 0, 128 };
                                                                    /* 886 */

        CopySqrDToSqr(&dq, &sq);                                    /* 889 */
    }

    dq.pri    = 0x10;   dq.z = PHM_Z(0x10);                         /* 890 */
    dq.zbuf   = PHM_ZBUF_NO_WRITE;                                  /* 892 */
    dq.test   = PHM_TEST_ALWAYS;                                    /* 893 */
    dq.alphar = PHM_ALPHA_BLEND;                                    /* 894 */
    dq.x[0]   = (int)x;   dq.y[0] = (int)y;                         /* 895 */
    dq.x[1]   = dq.x[0] + PHOTO_W;                                  /* 896 */
    dq.y[2]   = dq.y[0] + PHOTO_H * 2;                              /* 897 */

    for (i = 0; i < 4; i++)                                         /* 898 */
    {
        dq.r[i] = 0;   dq.g[i] = 0;   dq.b[i] = 0;
    }

    dq.alpha = alp2;
    dq.x[2] = dq.x[0];   dq.x[3] = dq.x[1];
    dq.y[1] = dq.y[0];   dq.y[3] = dq.y[2];
    DispSqrD(&dq);                                                  /* 900 */

    /* How far the fade still has to run, in pixels of spread. */
    f   = (float)sd->alpha * 0.25f;                                 /* 903 */
    pos = ((f - (float)alp) * 4.0f) / f;                            /* 905 */

    if (tex_adrs != NULL)                                           /* 909 */
    {
        MakeTim2SendPacket((uintptr_t)tex_adrs, 0);                 /* 910 */
    }

    CopySprDToSpr(&ds, sd);                                         /* 912 */
    ds.tex1   = PHM_TEX1;                                           /* 913 */
    ds.zbuf   = PHM_ZBUF_NO_WRITE;                                  /* 914 */
    ds.test   = PHM_TEST_MASK_READ;                                 /* 915 */
    ds.pri    = 0x10;   ds.z = PHM_Z(0x10);                         /* 916 */
    ds.alpha  = alp;                                                /* 917 */

    ds.x = (x + (float)((PHOTO_W - sd->w) / 2)) - pos;
    ds.y = (y + (float)((PHOTO_H * 2 - sd->h) / 2)) - pos;          /* 919 */
    DispSprD(&ds);                                                  /* 920 */

    ds.x = (x + (float)((PHOTO_W - sd->w) / 2)) - pos;
    ds.y = (y + (float)((PHOTO_H * 2 - sd->h) / 2)) + pos;          /* 922 */
    DispSprD(&ds);                                                  /* 923 */

    ds.x = (x + (float)((PHOTO_W - sd->w) / 2)) + pos;
    ds.y = (y + (float)((PHOTO_H * 2 - sd->h) / 2)) - pos;          /* 925 */
    DispSprD(&ds);                                                  /* 926 */

    ds.x = (x + (float)((PHOTO_W - sd->w) / 2)) + pos;
    ds.y = (y + (float)((PHOTO_H * 2 - sd->h) / 2)) + pos;          /* 928 */
    DispSprD(&ds);                                                  /* 929 */
}

/* ==========================================================================
 *  DrawPhotoFrame (997..1010)
 *
 *  The fourteen border sprites, all sharing effdat[60]'s texture.  Ten of
 *  them are the same three source strips reused at a rotation: entries 6, 7
 *  and 9 upside down, 10 and 11 turned a quarter left, 12 and 13 a quarter
 *  right.  Each rotates about its own (already offset) position, which is why
 *  the table's coordinates for those are the far end of the bar.
 * ======================================================================== */
void DrawPhotoFrame(float x, float y)                               /* 997 */
{
    DISP_SPRT ds;
    u_long    tex0 = effdat[60].tex0;                               /* 999 */
    int       i;

    for (i = 0; i < 14; i++)                                        /* 1002 */
    {
        CopySprDToSpr(&ds, &photo_frame[i]);                        /* 1003 */
        ds.tex0 = tex0;                                             /* 1004 */
        ds.x += (int)x;   ds.y += (int)y;                           /* 1005 */

        if (i == 6 || i == 7 || i == 9)                             /* 1006 */
        {
            ds.crx = ds.x;   ds.cry = ds.y;   ds.rot = 180.0f;
        }
        if (i == 10 || i == 11)                                     /* 1007 */
        {
            ds.crx = ds.x;   ds.cry = ds.y;   ds.rot = 270.0f;
        }
        if (i == 12 || i == 13)                                     /* 1008 */
        {
            ds.crx = ds.x;   ds.cry = ds.y;   ds.rot = 90.0f;
        }

        DispSprD(&ds);                                              /* 1009 */
    }                                                               /* 1010 */
}

/* ==========================================================================
 *  DrawSpecialFurnPhoto (1014..1082)
 *
 *  Take the picture.  The frame that was on screen is re-drawn into the photo
 *  page as one textured sprite: a 384x256 window starting at (photo_x,
 *  photo_y) squashed 2:1 into a 384x128 destination, which is the stored
 *  picture's own layout.
 *
 *  Unlike MakeSmallPhotoV() this uses the *waiting* packet buffer, because
 *  the caller needs the transfer to have happened before it reads the page
 *  back; and it restores only FRAME and XYOFFSET afterwards, since it never
 *  touched ZBUF.
 * ======================================================================== */
void DrawSpecialFurnPhoto(float photo_x, float photo_y)             /* 1014 */
{
    Q_WORDDATA *pbuf;
    void       *pNDrawEnv;
    int         n;

    pNDrawEnv = GetDrawEnv((int)(((u_int)sys_wrk.count + 1) & 1));  /* 1017 */

    {
        DRAW_ENV_5 de =                                             /* 1021 */
        {
            PHM_ALPHA_ENV,
            PHM_TEX1_ENV,
            SCE_GS_SET_CLAMP(SCE_GS_CLAMP_CLAMP, SCE_GS_CLAMP_CLAMP, 0, 0, 0, 0),
            PHM_TEST_ALWAYS,
            (*(u_long *)&((sceGsDrawEnv1 *)pNDrawEnv)->zbuf1 & 0x000000000f0001ffULL)
                | 0x0000000100000000ULL
        };

        SetDrawEnv(0, &de);                                         /* 1030 */
    }

    pbuf = GetPK2DbufWait();                                        /* 1033 */
    n    = 0;

    pbuf[n].ul64[0] = SCE_GIF_SET_TAG(4, 1, 0, 0, SCE_GIF_PACKED, 1); /* 1037 */
    pbuf[n].ul64[1] = SCE_GIF_PACKED_AD;                            /* 1038 */
    n++;

    pbuf[n].ul64[0] = 0;                                            /* 1040 */
    pbuf[n].ul64[1] = SCE_GS_TEXFLUSH;                              /* 1041 */
    n++;

    pbuf[n].ul64[0] = SCE_GS_SET_FRAME(PHM_PHOTO_FBP, 6, 1, 0);     /* 1045 */
    pbuf[n].ul64[1] = SCE_GS_FRAME_1;                               /* 1048 */
    n++;

    pbuf[n].ul64[0] = SCE_GS_SET_XYOFFSET(0x7400, 0x7c00);          /* 1050 */
    pbuf[n].ul64[1] = SCE_GS_XYOFFSET_1;                            /* 1051 */
    n++;

    pbuf[n].ul64[0] = SCE_GS_SET_TEX0(0, 10, 1, 10, 9, 0, 0, 0, 0, 0, 0, 0)
                    | ((u_long)((u_int)sys_wrk.count & 1) * PHM_FRAME_STRIDE);
                                                                    /* 1053 */
    pbuf[n].ul64[1] = SCE_GS_TEX0_1;                                /* 1054 */
    n++;

    pbuf[n].ul64[0] = SCE_GIF_SET_TAG(1, 1, 1,
                                      SCE_GS_SET_PRIM(SCE_GS_PRIM_SPRITE,
                                                      0, 1, 0, 0, 0, 1, 0, 0),
                                      SCE_GIF_PACKED, 4);           /* 1056 */
    pbuf[n].ul64[1] = (u_long)SCE_GIF_PACKED_UV
                    | ((u_long)SCE_GIF_PACKED_XYZF2 << 4)
                    | ((u_long)SCE_GIF_PACKED_UV    << 8)
                    | ((u_long)SCE_GIF_PACKED_XYZF2 << 12);         /* 1059 */
    n++;

    pbuf[n].ui32[0] = (u_int)(photo_x * 16.0f);                     /* 1062 */
    pbuf[n].ui32[1] = (u_int)(photo_y * 16.0f);                     /* 1063 */
    n++;

    pbuf[n].ui32[0] = 0x7400;                                       /* 1065 */
    pbuf[n].ui32[1] = 0x7c00;                                       /* 1066 */
    pbuf[n].ul64[1] = 0;                                            /* 1067 */
    n++;

    pbuf[n].ui32[0] = (u_int)((photo_x + 384.0f) * 16.0f);          /* 1069 */
    pbuf[n].ui32[1] = (u_int)((photo_y + 256.0f) * 16.0f);          /* 1070 */
    n++;

    pbuf[n].ui32[0] = 0x8c00;                                       /* 1072 */
    pbuf[n].ui32[1] = 0x8400;                                       /* 1073 */
    pbuf[n].ul64[1] = 0;                                            /* 1074 */
    n++;

    /* Put FRAME and XYOFFSET back; ZBUF was never touched. */
    pbuf[n].ul64[0] = SCE_GIF_SET_TAG(2, 1, 0, 0, SCE_GIF_PACKED, 1); /* 1077 */
    pbuf[n].ul64[1] = SCE_GIF_PACKED_AD;                            /* 1078 */
    n++;

    pbuf[n++].ul128 = ((Q_WORDDATA *)pNDrawEnv)[0].ul128;   /* g3dxVu0.h 134/135 */
    pbuf[n++].ul128 = ((Q_WORDDATA *)pNDrawEnv)[2].ul128;

    EndPK2DbufWait(&pbuf[n]);                                       /* 1082 */

    /* Host: same packet-dropping problem, but this page is never read back to
     * EE memory -- DispPhotoFrame1() samples it as a texture -- so the frame
     * has to be put into emulated GS memory rather than merely recorded.  The
     * 2:1 squash is the 384x256 window landing in a 384x128 page, exactly as
     * the sprite above writes it.
     *
     * PORT DEVIATION: the ROM samples the page being drawn into (`count & 1`)
     * and the host mirror holds the frame last presented.  The photo phase has
     * frozen the world by the time this runs, so the two are the same picture;
     * it is the same one-composite-early the renderer's frame-buffer sampling
     * already documents. */
    G2dScreenToGsPage(PHM_PHOTO_TBP, 6, (int)photo_x, (int)photo_y,
                      PHOTO_W, PHOTO_H * 2, PHOTO_W, PHOTO_H);
}
