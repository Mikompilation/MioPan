// FILE: /home/zero_rom/zero2np/src/album/prg/album_disp.c
//
// The photo album's drawing layer: thirty-five exported primitives plus eight
// file-local helpers that between them compose every album page -- the
// two-album edit view, the enlarged-photo frame, the info window, the confirm
// window, the slot-select window and the memory-card save screen.
//
// There is no state here at all.  album_disp.o has no .data and no .bss, and
// globals.txt lists nothing for the file; its .rodata is the per-function
// index tables, four local aggregate initialisers' worth of nothing, the
// __FUNCTION__ strings and the reporting-macro literals, and its .sdata is
// the fixed_array boilerplate plus the six eight-byte tables that were too
// small for .rodata under -G.  Every routine is "copy an album_tex[] record
// into a DISP_SPRT, offset it, scale its alpha by the caller's, draw it", and
// the whole file is that shape forty times over.
//
// Things worth knowing before touching it:
//
//  * A sprite's x and y update is ONE source line, and so is a
//    crx/cry/rot or csx/csy/scw/sch group.  Ghidra prints the members in the
//    wrong order; read the store offsets (0x14/0x18 crx/cry, 0x1c/0x20
//    csx/csy, 0x24/0x28 x/y, 0x30/0x34 scw/sch, 0x38 rot) instead.
//
//  * off_x / off_y are not honoured everywhere.  AlbumThumbnailBaseNumberDisp
//    reads neither, AlbumEditFrameDisp passes 0,0 to all seven of its helpers
//    rather than forwarding its own, and AlbumEditCaptionDisp,
//    AlbumSlotSelCaptionDisp, AlbumEditInfoPhotoNoDisp and AlbumSaveMsgWinDisp
//    take them and ignore them.  Do not invent uses for them.
//
//  * The seven per-album-type tables all have -1 in rows 5 and 6.  Those two
//    album types never reach the save screen, and every AlbumSave* routine
//    rejects an album_type >= 5 with a PRINT_WARNING before indexing.
//
//  * Rotated sprites turn about their own already-offset position, and the
//    270-degree ones take the rotation centre at y + (float)ds.w -- which is
//    also what ds.y is set to, so the pair looks redundant and is not.
//
//  * DISP_SPRT::w and ::h are u_int, so every `(float)ds.w` compiles to the
//    unsigned int-to-float sequence (test the sign bit, else halve / convert /
//    double).  That sequence is not a decompiler artifact.
//
// Reconstructed from the Feb 6 2004 prototype (SLES_523.84), album_disp.o
// (.text 0x11cee0..0x120174 = 0x3294, byte-for-byte accounted for: the four
// fixed_array.h helpers plus these forty-three bodies, with eighteen 4-byte
// alignment fills between them and no gap large enough to hide another).
//
// Trailing /* NNN */ comments are the original source line numbers, measured
// from the $LM stabs.  A call whose arguments span several source lines is
// tagged with its closing line, which is where GCC 2.96 put the note.  Five of
// the 497 are interpolated rather than measured, and each says so at the site:
// two that a fixed_array<> subscript swallowed (500, 534) and three that GCC
// eliminated when it cross-jumped an identical tail (135, 828, 835).

#include "album_disp.h"

#include "album.h"                                  // album_info[]
#include "../dat/album_dat.h"                       // album_tex[] and the x tables
#include "../../common/utility2.h"                  // PRINT_ASSERT / PRINT_WARNING
#include "../../graphics/graph2d/draw_cmn.h"        // DrawCmn*
#include "../../graphics/graph2d/g2d_draw.h"        // DISP_SPRT / DISP_SQAR / CopySprDToSpr
#include "../../graphics/graph2d/message.h"         // PrintMsg / PrintMsg_Arrange
#include "../../graphics/graph2d/tim2.h"            // PK2SendVram
#include "../../ingame/menu/anim_2d.h"              // Anim2D_CalcNow*
#include "../../ingame/menu/zero2_anim2d.h"         // Zero2Anim2D_InOutAnimCtrl
#include "../../system/os/system.h"                 // GetLanguage

/* album_tex[] indices, by part.  See album_dat.c for the whole map. */
#define ALBUM_TEX_TITLE_FRAME           0       /* 2 pieces, mirrored       */
#define ALBUM_TEX_TITLE_FRAME_NUM       2
#define ALBUM_TEX_TITLE                 2
#define ALBUM_TEX_A_BASE                3       /* 6 pieces                 */
#define ALBUM_TEX_A_FRAME               9       /* 4 pieces                 */
#define ALBUM_TEX_A_THUMB_FRAME         13      /* 8 pieces                 */
#define ALBUM_TEX_B_BASE                21      /* 6 pieces                 */
#define ALBUM_TEX_B_THUMB_FRAME         27      /* 8 pieces                 */
#define ALBUM_TEX_B_FRAME               35      /* 4 pieces                 */
#define ALBUM_TEX_BASE_NUM              6
#define ALBUM_TEX_FRAME_NUM             4
#define ALBUM_TEX_THUMB_FRAME_NUM       8
#define ALBUM_TEX_PROTECT_FRAME         130     /* 4 pieces                 */
#define ALBUM_TEX_PROTECT_FRAME_NUM     4
#define ALBUM_TEX_THUMB_NUM             138     /* + digit                  */
#define ALBUM_TEX_THUMB_BASE            148
#define ALBUM_TEX_PHOTO_NUM             149     /* DrawCmnNumberTex zero_dat*/
#define ALBUM_TEX_A_PHOTO_NUM_PLATE     159
#define ALBUM_TEX_B_PHOTO_NUM_PLATE     160
#define ALBUM_TEX_A_PHOTO_MAX_PLATE     161
#define ALBUM_TEX_B_PHOTO_MAX_PLATE     162
#define ALBUM_TEX_A_CSR                 163
#define ALBUM_TEX_A_CSR_FLARE           164
#define ALBUM_TEX_B_CSR                 165
#define ALBUM_TEX_B_CSR_FLARE           166
#define ALBUM_TEX_A_CUR_FLARE           181     /* 5 pieces                 */
#define ALBUM_TEX_B_CUR_FLARE           186     /* 5 pieces                 */
#define ALBUM_TEX_INFO_NAME_5           215     /* album types 5 and 6 have */
#define ALBUM_TEX_INFO_NAME_6           216     /* a name plate each        */
#define ALBUM_TEX_CONF_FLARE            244     /* 2 pieces                 */
#define ALBUM_TEX_CONF_CSR              246     /* 2 pieces                 */
#define ALBUM_TEX_PHOTO_FRAME           248     /* 4 corners                */
#define ALBUM_TEX_PHOTO_EDGE_L          252     /* tiled 6 times            */
#define ALBUM_TEX_PHOTO_EDGE_L_CAP      253
#define ALBUM_TEX_PHOTO_EDGE_R          254     /* tiled 6 times            */
#define ALBUM_TEX_PHOTO_EDGE_R_CAP      255
#define ALBUM_TEX_PHOTO_EDGE_T          256     /* tiled 19 times           */
#define ALBUM_TEX_PHOTO_EDGE_T_CAP      257
#define ALBUM_TEX_PHOTO_EDGE_B          258     /* tiled 19 times           */
#define ALBUM_TEX_PHOTO_EDGE_B_CAP      259
#define ALBUM_TEX_MC_SLOT               430     /* 2 pieces                 */
#define ALBUM_TEX_SLOT_SEL              432     /* 2 pieces                 */

/* Photo-frame tile counts. */
#define ALBUM_PHOTO_EDGE_V_NUM          6
#define ALBUM_PHOTO_EDGE_H_NUM          19

/* Thumbnail grid geometry, shared by the plates, the numbers and the cursor. */
#define ALBUM_THUMB_X_PITCH             50
#define ALBUM_THUMB_Y_PITCH             35

/* Both flare outlines are a corner, a tiled top edge, a tiled rotated side
 * and two more corners. */
#define ALBUM_CUR_FLARE_TOP_NUM         8
#define ALBUM_CUR_FLARE_SIDE_NUM        7

/* The slot-select window: two rows of text over a common window. */
#define ALBUM_SLOT_SEL_MSG_MAX          2

/* The two edit-menu rows and the two yes / no answers. */
#define ALBUM_MENU_FRAME_MAX            2
#define ALBUM_CONF_CSR_MAX              2

/* ---- file-local helpers ------------------------------------------------- */

static void AlbumBaseFrameDisp(int off_x, int off_y, u_char alpha);
static void AlbumA_FrameDisp(int off_x, int off_y, u_char alpha);
static void AlbumA_ThumbnailFrameDisp(int off_x, int off_y, u_char alpha);
static void AlbumB_FrameDisp(int off_x, int off_y, u_char alpha);
static void AlbumB_ThumbnailFrameDisp(int off_x, int off_y, u_char alpha);
static void AlbumAPhotoNumDisp(int off_x, int off_y, u_char alpha);
static void AlbumBPhotoNumDisp(int off_x, int off_y, u_char alpha);
static void AlbumSlotSelectDisp(int off_x, int off_y, u_char alpha);

/* ==========================================================================
 *  Animation
 * ======================================================================== */

/* The page open/close fade -- ten frames in, five out.  Every album screen
 * drives its own ALBUM_DISP_CTRL pair through this. */
void AlbumInOutAnimCtrl(char *anim_step, char *anim_timer, u_char *alpha) /* 79 */
{
    *alpha = Zero2Anim2D_InOutAnimCtrl(anim_step, anim_timer, 10, 5);   /* 83 */
}

/* The edit menu's own fade.  Same five-step vocabulary as
 * Zero2Anim2D_InOutAnimCtrl(), written out here because it additionally
 * produces the two row scales AlbumMenu{,Non}SelFrameDisp open their frames
 * with -- the selected row overshoots to 1.3, the rest settle at 1.0. */
void AlbumEditMenuAnimCtrl(char *anim_step, char *anim_timer, u_char *alpha,
                           float *sel_scl, float *non_sel_scl)          /* 96 */
{
    static const SCL_ANIM_TBL sel_scl_tbl[2] =
    {
        /* start_scl, end_scl, start_time, end_time */
        {       0.0f,    1.3f,           0,        5 },
        {      -1.0f,   -1.0f,          -1,       -1 },
    };
    static const SCL_ANIM_TBL non_sel_scl_tbl[2] =
    {
        {       0.0f,    1.0f,           0,        5 },
        {      -1.0f,   -1.0f,          -1,       -1 },
    };
    static const ALPHA_ANIM_TBL in_alpha_tbl[2] =
    {
        /* start_alpha, end_alpha, start_time, end_time */
        {            0,       128,           0,        5 },
        {           -1,        -1,          -1,       -1 },
    };
    static const ALPHA_ANIM_TBL out_alpha_tbl[2] =
    {
        {          128,         0,           0,        5 },
        {           -1,        -1,          -1,       -1 },
    };

    *alpha       = 128;                                                 /* 115 */
    *sel_scl     = 1.3f;                                                /* 116 */
    *non_sel_scl = 1.0f;                                                /* 117 */

    if (*anim_step == ZERO2_ANIM2D_STEP_START) {                        /* 120 */
        *anim_timer = 0;                                                /* 121 */
        *anim_step  = ZERO2_ANIM2D_STEP_IN;                             /* 122 */
    }

    if (*anim_step == ZERO2_ANIM2D_STEP_IN) {                           /* 125 */
        *sel_scl     = Anim2D_CalcNowScale(sel_scl_tbl, *anim_timer);     /* 126 */
        *non_sel_scl = Anim2D_CalcNowScale(non_sel_scl_tbl, *anim_timer); /* 127 */
        *alpha       = Anim2D_CalcNowAlpha(in_alpha_tbl, *anim_timer);    /* 128 */

        (*anim_timer)++;                                                /* 130 */

        if (*anim_timer >= 5) {                                         /* 132 */

            *anim_step  = ZERO2_ANIM2D_STEP_SHOW;                       /* 134 */
            /* 135 interpolated: GCC cross-jumped this pair of stores onto the
             * step-3 arm's copy, so only that one's 143/144 survive. */
            *anim_timer = 0;                                            /* 135 */
        }
    } else if (*anim_step == ZERO2_ANIM2D_STEP_OUT) {                   /* 137 */
        *alpha = Anim2D_CalcNowAlpha(out_alpha_tbl, *anim_timer);       /* 138 */

        (*anim_timer)++;                                                /* 140 */

        if (*anim_timer >= 5) {                                         /* 142 */
            *anim_step  = ZERO2_ANIM2D_STEP_END;                        /* 143 */
            *anim_timer = 0;                                            /* 144 */
        }
    } else if (*anim_step == ZERO2_ANIM2D_STEP_END) {                   /* 147 */
        *alpha = 0;
    }
}

/* ==========================================================================
 *  The edit page's static frame
 * ======================================================================== */

/* Both albums' furniture in one call.  Note that every helper is handed 0, 0
 * rather than the caller's own offsets -- the edit page never slides. */
void AlbumEditFrameDisp(int off_x, int off_y, u_char alpha)             /* 164 */
{
    AlbumBaseFrameDisp(0, 0, alpha);                                    /* 168 */

    AlbumA_FrameDisp(0, 0, alpha);                                      /* 171 */

    AlbumA_ThumbnailFrameDisp(0, 0, alpha);                             /* 174 */

    AlbumB_FrameDisp(0, 0, alpha);                                      /* 177 */

    AlbumB_ThumbnailFrameDisp(0, 0, alpha);                             /* 180 */

    AlbumAPhotoNumDisp(0, 0, alpha);                                    /* 183 */

    AlbumBPhotoNumDisp(0, 0, alpha);                                    /* 186 */
}

/* A full-screen black quad.  max_alpha is the dim depth the caller wants and
 * alpha its own fade -- note the product is of those two, not of the record's
 * own alpha, so the 0x80 in the initialiser never reaches the GS. */
void AlbumBlackBgDisp(int off_x, int off_y, u_char alpha, u_char max_alpha) /* 201 */
{
    DISP_SQAR dsq;

    /* A local initialiser, not a static table -- GCC copies the .rodata image
     * at 3a0488 straight into the stack frame. */
    SQAR_DAT cursor_base = { 640, 448, 0, 0, 0, 0, 0, 0, 128 };         /* 204 */

    CopySqrDToSqr(&dsq, &cursor_base);                                  /* 208 */

    dsq.alpha = (u_char)(((int)max_alpha * (int)alpha) >> 7);           /* 209 */

    DispSqrD(&dsq);                                                     /* 210 */
}

void AlbumTitleFrameDisp(int off_x, int off_y, u_char alpha)            /* 221 */
{
    DISP_SPRT title_ds;
    int       i;

    for (i = 0; i < ALBUM_TEX_TITLE_FRAME_NUM; i++) {                   /* 227 */
        CopySprDToSpr(&title_ds, &album_tex[ALBUM_TEX_TITLE_FRAME + i]); /* 228 */

        title_ds.x = title_ds.x + (float)off_x;                         /* 229 */
        title_ds.y = title_ds.y + (float)off_y;                         /* 229 */

        title_ds.alpha = (u_char)(((int)title_ds.alpha * (int)alpha) >> 7); /* 230 */

        DispSprD(&title_ds);                                            /* 231 */
    }                                                                   /* 232 */
}

void AlbumTitleDisp(int off_x, int off_y, u_char alpha)                 /* 243 */
{
    DISP_SPRT title_ds;

    CopySprDToSpr(&title_ds, &album_tex[ALBUM_TEX_TITLE]);              /* 248 */

    title_ds.x = title_ds.x + (float)off_x;                             /* 249 */
    title_ds.y = title_ds.y + (float)off_y;                             /* 249 */

    title_ds.alpha = (u_char)(((int)title_ds.alpha * (int)alpha) >> 7); /* 250 */

    DispSprD(&title_ds);                                                /* 251 */
}

/* The two albums' base rows: the shelf each album sits on, six pieces each. */
static void AlbumBaseFrameDisp(int off_x, int off_y, u_char alpha)      /* 262 */
{
    DISP_SPRT base_ds;
    int       i;

    for (i = 0; i < ALBUM_TEX_BASE_NUM; i++) {                          /* 268 */
        CopySprDToSpr(&base_ds, &album_tex[ALBUM_TEX_A_BASE + i]);      /* 269 */

        base_ds.x = base_ds.x + (float)off_x;                           /* 270 */
        base_ds.y = base_ds.y + (float)off_y;                           /* 270 */

        base_ds.alpha = (u_char)(((int)base_ds.alpha * (int)alpha) >> 7); /* 271 */

        DispSprD(&base_ds);                                             /* 272 */
    }                                                                   /* 273 */

    for (i = 0; i < ALBUM_TEX_BASE_NUM; i++) {                          /* 275 */
        CopySprDToSpr(&base_ds, &album_tex[ALBUM_TEX_B_BASE + i]);      /* 276 */

        base_ds.x = base_ds.x + (float)off_x;                           /* 277 */
        base_ds.y = base_ds.y + (float)off_y;                           /* 277 */

        base_ds.alpha = (u_char)(((int)base_ds.alpha * (int)alpha) >> 7); /* 278 */

        DispSprD(&base_ds);                                             /* 279 */
    }                                                                   /* 280 */
}

static void AlbumA_FrameDisp(int off_x, int off_y, u_char alpha)        /* 291 */
{
    DISP_SPRT frame_ds;
    int       i;

    for (i = 0; i < ALBUM_TEX_FRAME_NUM; i++) {                         /* 297 */
        CopySprDToSpr(&frame_ds, &album_tex[ALBUM_TEX_A_FRAME + i]);    /* 298 */

        frame_ds.x = frame_ds.x + (float)off_x;                         /* 299 */
        frame_ds.y = frame_ds.y + (float)off_y;                         /* 299 */

        frame_ds.alpha = (u_char)(((int)frame_ds.alpha * (int)alpha) >> 7); /* 300 */

        DispSprD(&frame_ds);                                            /* 301 */
    }                                                                   /* 302 */
}

static void AlbumA_ThumbnailFrameDisp(int off_x, int off_y, u_char alpha) /* 313 */
{
    DISP_SPRT frame_ds;
    int       i;

    for (i = 0; i < ALBUM_TEX_THUMB_FRAME_NUM; i++) {                   /* 319 */
        CopySprDToSpr(&frame_ds, &album_tex[ALBUM_TEX_A_THUMB_FRAME + i]); /* 320 */

        frame_ds.x = frame_ds.x + (float)off_x;                         /* 321 */
        frame_ds.y = frame_ds.y + (float)off_y;                         /* 321 */

        frame_ds.alpha = (u_char)(((int)frame_ds.alpha * (int)alpha) >> 7); /* 322 */

        DispSprD(&frame_ds);                                            /* 323 */
    }                                                                   /* 324 */
}

/* The glow round album A while it is the current one.  Five pieces: a corner,
 * a rotated corner, the top edge tiled eight times, the side tiled seven
 * times (also rotated), and a last corner.  Everything is additive
 * (alphar 0x48) and tinted by the caller's pulse. */
void AlbumA_CurrentFrameFlareDisp(int off_x, int off_y, u_char alpha, u_char rgb) /* 336 */
{
    DISP_SPRT frame_ds;
    int       i;

    CopySprDToSpr(&frame_ds, &album_tex[ALBUM_TEX_A_CUR_FLARE]);        /* 342 */

    frame_ds.x = frame_ds.x + (float)off_x;                             /* 343 */
    frame_ds.y = frame_ds.y + (float)off_y;                             /* 343 */

    frame_ds.alpha  = (u_char)(((int)frame_ds.alpha * (int)alpha) >> 7); /* 344 */
    frame_ds.alphar = 0x48;                                             /* 345 */
    frame_ds.r = rgb;  frame_ds.g = rgb;  frame_ds.b = rgb;             /* 346 */

    DispSprD(&frame_ds);                                                /* 347 */

    CopySprDToSpr(&frame_ds, &album_tex[ALBUM_TEX_A_CUR_FLARE + 1]);    /* 349 */

    frame_ds.x = frame_ds.x + (float)off_x;                             /* 350 */
    frame_ds.y = frame_ds.y + (float)frame_ds.w + (float)off_y;         /* 350 */

    frame_ds.crx = frame_ds.x;  frame_ds.cry = frame_ds.y;  frame_ds.rot = 270.0f; /* 351 */

    frame_ds.alpha  = (u_char)(((int)frame_ds.alpha * (int)alpha) >> 7); /* 352 */
    frame_ds.alphar = 0x48;                                             /* 353 */
    frame_ds.r = rgb;  frame_ds.g = rgb;  frame_ds.b = rgb;             /* 354 */

    DispSprD(&frame_ds);                                                /* 355 */

    for (i = 0; i < ALBUM_CUR_FLARE_TOP_NUM; i++) {                     /* 357 */
        CopySprDToSpr(&frame_ds, &album_tex[ALBUM_TEX_A_CUR_FLARE + 2]); /* 358 */

        frame_ds.x = frame_ds.x + (float)off_x + (float)(i * ALBUM_THUMB_X_PITCH); /* 359 */
        frame_ds.y = frame_ds.y + (float)off_y;                         /* 359 */

        frame_ds.alpha  = (u_char)(((int)frame_ds.alpha * (int)alpha) >> 7); /* 360 */
        frame_ds.alphar = 0x48;                                         /* 361 */
        frame_ds.r = rgb;  frame_ds.g = rgb;  frame_ds.b = rgb;         /* 362 */

        DispSprD(&frame_ds);                                            /* 363 */
    }                                                                   /* 364 */

    for (i = 0; i < ALBUM_CUR_FLARE_SIDE_NUM; i++) {                    /* 366 */
        CopySprDToSpr(&frame_ds, &album_tex[ALBUM_TEX_A_CUR_FLARE + 3]); /* 367 */

        frame_ds.x = frame_ds.x + (float)off_x + (float)(i * ALBUM_THUMB_X_PITCH); /* 368 */
        frame_ds.y = frame_ds.y + (float)frame_ds.w + (float)off_y;     /* 368 */

        frame_ds.crx = frame_ds.x;  frame_ds.cry = frame_ds.y;  frame_ds.rot = 270.0f; /* 369 */

        frame_ds.alpha  = (u_char)(((int)frame_ds.alpha * (int)alpha) >> 7); /* 370 */
        frame_ds.alphar = 0x48;                                         /* 371 */
        frame_ds.r = rgb;  frame_ds.g = rgb;  frame_ds.b = rgb;         /* 372 */

        DispSprD(&frame_ds);                                            /* 373 */
    }                                                                   /* 374 */

    CopySprDToSpr(&frame_ds, &album_tex[ALBUM_TEX_A_CUR_FLARE + 4]);    /* 376 */

    frame_ds.x = frame_ds.x + (float)off_x;                             /* 377 */
    frame_ds.y = frame_ds.y + (float)off_y;                             /* 377 */

    frame_ds.alpha  = (u_char)(((int)frame_ds.alpha * (int)alpha) >> 7); /* 378 */
    frame_ds.alphar = 0x48;                                             /* 379 */
    frame_ds.r = rgb;  frame_ds.g = rgb;  frame_ds.b = rgb;             /* 380 */

    DispSprD(&frame_ds);                                                /* 381 */
}

static void AlbumB_FrameDisp(int off_x, int off_y, u_char alpha)        /* 392 */
{
    DISP_SPRT frame_ds;
    int       i;

    for (i = 0; i < ALBUM_TEX_FRAME_NUM; i++) {                         /* 398 */
        CopySprDToSpr(&frame_ds, &album_tex[ALBUM_TEX_B_FRAME + i]);    /* 399 */

        frame_ds.x = frame_ds.x + (float)off_x;                         /* 400 */
        frame_ds.y = frame_ds.y + (float)off_y;                         /* 400 */

        frame_ds.alpha = (u_char)(((int)frame_ds.alpha * (int)alpha) >> 7); /* 401 */

        DispSprD(&frame_ds);                                            /* 402 */
    }                                                                   /* 403 */
}

static void AlbumB_ThumbnailFrameDisp(int off_x, int off_y, u_char alpha) /* 414 */
{
    DISP_SPRT frame_ds;
    int       i;

    for (i = 0; i < ALBUM_TEX_THUMB_FRAME_NUM; i++) {                   /* 420 */
        CopySprDToSpr(&frame_ds, &album_tex[ALBUM_TEX_B_THUMB_FRAME + i]); /* 421 */

        frame_ds.x = frame_ds.x + (float)off_x;                         /* 422 */
        frame_ds.y = frame_ds.y + (float)off_y;                         /* 422 */

        frame_ds.alpha = (u_char)(((int)frame_ds.alpha * (int)alpha) >> 7); /* 423 */

        DispSprD(&frame_ds);                                            /* 424 */
    }                                                                   /* 425 */
}

/* Album B's glow.  Same five pieces as album A's, but in a different order --
 * the rotated corner comes fourth here rather than second. */
void AlbumB_CurrentFrameFlareDisp(int off_x, int off_y, u_char alpha, u_char rgb) /* 437 */
{
    DISP_SPRT frame_ds;
    int       i;

    CopySprDToSpr(&frame_ds, &album_tex[ALBUM_TEX_B_CUR_FLARE]);        /* 443 */

    frame_ds.x = frame_ds.x + (float)off_x;                             /* 444 */
    frame_ds.y = frame_ds.y + (float)off_y;                             /* 444 */

    frame_ds.alpha  = (u_char)(((int)frame_ds.alpha * (int)alpha) >> 7); /* 445 */
    frame_ds.alphar = 0x48;                                             /* 446 */
    frame_ds.r = rgb;  frame_ds.g = rgb;  frame_ds.b = rgb;             /* 447 */

    DispSprD(&frame_ds);                                                /* 448 */

    for (i = 0; i < ALBUM_CUR_FLARE_TOP_NUM; i++) {                     /* 450 */
        CopySprDToSpr(&frame_ds, &album_tex[ALBUM_TEX_B_CUR_FLARE + 1]); /* 451 */

        frame_ds.x = frame_ds.x + (float)off_x + (float)(i * ALBUM_THUMB_X_PITCH); /* 452 */
        frame_ds.y = frame_ds.y + (float)off_y;                         /* 452 */

        frame_ds.alpha  = (u_char)(((int)frame_ds.alpha * (int)alpha) >> 7); /* 453 */
        frame_ds.alphar = 0x48;                                         /* 454 */
        frame_ds.r = rgb;  frame_ds.g = rgb;  frame_ds.b = rgb;         /* 455 */

        DispSprD(&frame_ds);                                            /* 456 */
    }                                                                   /* 457 */

    for (i = 0; i < ALBUM_CUR_FLARE_SIDE_NUM; i++) {                    /* 459 */
        CopySprDToSpr(&frame_ds, &album_tex[ALBUM_TEX_B_CUR_FLARE + 2]); /* 460 */

        frame_ds.x = frame_ds.x + (float)off_x + (float)(i * ALBUM_THUMB_X_PITCH); /* 461 */
        frame_ds.y = frame_ds.y + (float)frame_ds.w + (float)off_y;     /* 461 */

        frame_ds.crx = frame_ds.x;  frame_ds.cry = frame_ds.y;  frame_ds.rot = 270.0f; /* 462 */

        frame_ds.alpha  = (u_char)(((int)frame_ds.alpha * (int)alpha) >> 7); /* 463 */
        frame_ds.alphar = 0x48;                                         /* 464 */
        frame_ds.r = rgb;  frame_ds.g = rgb;  frame_ds.b = rgb;         /* 465 */

        DispSprD(&frame_ds);                                            /* 466 */
    }                                                                   /* 467 */

    CopySprDToSpr(&frame_ds, &album_tex[ALBUM_TEX_B_CUR_FLARE + 3]);    /* 469 */

    frame_ds.x = frame_ds.x + (float)off_x;                             /* 470 */
    frame_ds.y = frame_ds.y + (float)frame_ds.w + (float)off_y;         /* 470 */

    frame_ds.crx = frame_ds.x;  frame_ds.cry = frame_ds.y;  frame_ds.rot = 270.0f; /* 471 */

    frame_ds.alpha  = (u_char)(((int)frame_ds.alpha * (int)alpha) >> 7); /* 472 */
    frame_ds.alphar = 0x48;                                             /* 473 */
    frame_ds.r = rgb;  frame_ds.g = rgb;  frame_ds.b = rgb;             /* 474 */

    DispSprD(&frame_ds);                                                /* 475 */

    CopySprDToSpr(&frame_ds, &album_tex[ALBUM_TEX_B_CUR_FLARE + 4]);    /* 477 */

    frame_ds.x = frame_ds.x + (float)off_x;                             /* 478 */
    frame_ds.y = frame_ds.y + (float)off_y;                             /* 478 */

    frame_ds.alpha  = (u_char)(((int)frame_ds.alpha * (int)alpha) >> 7); /* 479 */
    frame_ds.alphar = 0x48;                                             /* 480 */
    frame_ds.r = rgb;  frame_ds.g = rgb;  frame_ds.b = rgb;             /* 481 */

    DispSprD(&frame_ds);                                                /* 482 */
}

/* "<n> / 16" under album A.  The count and the total are drawn as two
 * DrawCmnNumberTex() runs with a plate after each -- the plate carries the
 * slash and the label, the digits come out of album_tex[149..158]. */
static void AlbumAPhotoNumDisp(int off_x, int off_y, u_char alpha)      /* 493 */
{
    DISP_SPRT num_ds;

    /* The album_info[] subscript swallows this statement's own line note --
     * fixed_array<>::operator[] is tagged fixed_array.h 124 and GCC emits no
     * fresh note for the call.  500 is interpolated from the second pair's
     * 510 / 513 spacing. */
    DrawCmnNumberTex((int)album_info[ALBUM_DATA_A].album_info.pic_num, 2,
                     &album_tex[ALBUM_TEX_PHOTO_NUM], 36, 59, alpha, 0, 0); /* 500 */

    CopySprDToSpr(&num_ds, &album_tex[ALBUM_TEX_A_PHOTO_NUM_PLATE]);    /* 503 */

    num_ds.x = num_ds.x + (float)off_x;                                 /* 504 */
    num_ds.y = num_ds.y + (float)off_y;                                 /* 504 */

    num_ds.alpha = (u_char)(((int)num_ds.alpha * (int)alpha) >> 7);     /* 505 */

    DispSprD(&num_ds);                                                  /* 506 */

    DrawCmnNumberTex(PHOTO_FILE_MAX, 2,
                     &album_tex[ALBUM_TEX_PHOTO_NUM], 82, 59, alpha, 0, 0); /* 510 */

    CopySprDToSpr(&num_ds, &album_tex[ALBUM_TEX_A_PHOTO_MAX_PLATE]);    /* 513 */

    num_ds.x = num_ds.x + (float)off_x;                                 /* 514 */
    num_ds.y = num_ds.y + (float)off_y;                                 /* 514 */

    num_ds.alpha = (u_char)(((int)num_ds.alpha * (int)alpha) >> 7);     /* 515 */

    DispSprD(&num_ds);                                                  /* 516 */
}

static void AlbumBPhotoNumDisp(int off_x, int off_y, u_char alpha)      /* 527 */
{
    DISP_SPRT num_ds;

    /* 534 interpolated, as in AlbumAPhotoNumDisp above. */
    DrawCmnNumberTex((int)album_info[ALBUM_DATA_B].album_info.pic_num, 2,
                     &album_tex[ALBUM_TEX_PHOTO_NUM], 503, 406, alpha, 0, 0); /* 534 */

    CopySprDToSpr(&num_ds, &album_tex[ALBUM_TEX_B_PHOTO_NUM_PLATE]);    /* 537 */

    num_ds.x = num_ds.x + (float)off_x;                                 /* 538 */
    num_ds.y = num_ds.y + (float)off_y;                                 /* 538 */

    num_ds.alpha = (u_char)(((int)num_ds.alpha * (int)alpha) >> 7);     /* 539 */

    DispSprD(&num_ds);                                                  /* 540 */

    DrawCmnNumberTex(PHOTO_FILE_MAX, 2,
                     &album_tex[ALBUM_TEX_PHOTO_NUM], 549, 406, alpha, 0, 0); /* 544 */

    CopySprDToSpr(&num_ds, &album_tex[ALBUM_TEX_B_PHOTO_MAX_PLATE]);    /* 547 */

    num_ds.x = num_ds.x + (float)off_x;                                 /* 548 */
    num_ds.y = num_ds.y + (float)off_y;                                 /* 548 */

    num_ds.alpha = (u_char)(((int)num_ds.alpha * (int)alpha) >> 7);     /* 549 */

    DispSprD(&num_ds);                                                  /* 550 */
}

/* ==========================================================================
 *  The thumbnail grid
 * ======================================================================== */

/* Sixteen empty slot plates, two rows of eight, at the album's own origin. */
void AlbumThumbnailBaseDisp(int data_label, int off_x, int off_y, u_char alpha) /* 562 */
{
    DISP_SPRT frame_ds;
    int       i;
    int       j;

    static const int thumbnail_base_x[ALBUM_DATA_MAX] = { 207,  36 };
    static const int thumbnail_base_y[ALBUM_DATA_MAX] = {  80, 338 };

    for (i = 0; i < ALBUM_THUMB_Y_NUM; i++) {                           /* 576 */
        for (j = 0; j < ALBUM_THUMB_X_NUM; j++) {                       /* 577 */
            CopySprDToSpr(&frame_ds, &album_tex[ALBUM_TEX_THUMB_BASE]); /* 578 */

            frame_ds.x = (float)(thumbnail_base_x[data_label] + off_x + j * ALBUM_THUMB_X_PITCH); /* 579 */
            frame_ds.y = (float)(thumbnail_base_y[data_label] + off_y + i * ALBUM_THUMB_Y_PITCH); /* 579 */

            frame_ds.alpha = (u_char)(((int)frame_ds.alpha * (int)alpha) >> 7); /* 581 */

            DispSprD(&frame_ds);                                        /* 582 */
        }                                                               /* 583 */
    }                                                                   /* 584 */
}

/* The slot numbers 1..16 under those plates, each centred on its own plate.
 * Two digits are drawn as a hardcoded leading album_tex[THUMB_NUM + 1] -- the
 * glyph for 1 -- plus the units digit, which is correct only because the grid
 * never goes past 16.  Neither off_x nor off_y is read. */
void AlbumThumbnailBaseNumberDisp(int data_label, int off_x, int off_y, u_char alpha) /* 596 */
{
    DISP_SPRT frame_ds;
    int       i;
    int       j;
    int       disp_number;
    float     num_x;
    float     num_w;
    float     thumbnail_base_x;
    float     thumbnail_base_half;

    static const int thumbnail_base_x_tbl[ALBUM_DATA_MAX] = { 207,  36 };
    static const int thumbnail_num_y_tbl[ALBUM_DATA_MAX]  = {  84, 343 };

    thumbnail_base_half = (float)album_tex[ALBUM_TEX_THUMB_BASE].w * 0.5f; /* 616 */

    disp_number = 1;                                                    /* 619 */

    for (i = 0; i < ALBUM_THUMB_Y_NUM; i++) {                           /* 622 */
        for (j = 0; j < ALBUM_THUMB_X_NUM; j++) {                       /* 623 */

            thumbnail_base_x = (float)(thumbnail_base_x_tbl[data_label]
                                       + j * ALBUM_THUMB_X_PITCH);      /* 625 */

            num_w = (float)album_tex[ALBUM_TEX_THUMB_NUM + disp_number % 10].w; /* 628 */

            if (disp_number >= 10) {                                    /* 629 */
                num_w = num_w + (float)album_tex[ALBUM_TEX_THUMB_NUM + 1].w; /* 630 */
            }

            num_x = thumbnail_base_x + thumbnail_base_half - num_w * 0.5f; /* 635 */

            if (disp_number >= 10) {                                    /* 637 */
                CopySprDToSpr(&frame_ds, &album_tex[ALBUM_TEX_THUMB_NUM + 1]); /* 638 */

                frame_ds.x = num_x;                                     /* 639 */
                frame_ds.y = (float)(thumbnail_num_y_tbl[data_label] + i * ALBUM_THUMB_Y_PITCH); /* 639 */

                frame_ds.alpha = (u_char)(((int)frame_ds.alpha * (int)alpha) >> 7); /* 640 */

                DispSprD(&frame_ds);                                    /* 641 */

                num_x = num_x + (float)album_tex[ALBUM_TEX_THUMB_NUM + 1].w; /* 643 */

                CopySprDToSpr(&frame_ds, &album_tex[ALBUM_TEX_THUMB_NUM + disp_number % 10]); /* 645 */

                frame_ds.x = num_x;                                     /* 646 */
                frame_ds.y = (float)(thumbnail_num_y_tbl[data_label] + i * ALBUM_THUMB_Y_PITCH); /* 646 */

                frame_ds.alpha = (u_char)(((int)frame_ds.alpha * (int)alpha) >> 7); /* 647 */

                DispSprD(&frame_ds);                                    /* 648 */
            } else {
                CopySprDToSpr(&frame_ds, &album_tex[ALBUM_TEX_THUMB_NUM + disp_number % 10]); /* 651 */

                frame_ds.x = num_x;                                     /* 652 */
                frame_ds.y = (float)(thumbnail_num_y_tbl[data_label] + i * ALBUM_THUMB_Y_PITCH); /* 652 */

                frame_ds.alpha = (u_char)(((int)frame_ds.alpha * (int)alpha) >> 7); /* 653 */

                DispSprD(&frame_ds);                                    /* 654 */
            }

            disp_number++;                                              /* 657 */
        }                                                               /* 658 */
    }                                                                   /* 659 */
}

/* The thumbnail cursor over album A: an additive flare, then the frame on top.
 * photo_no is the flat 0..15 slot index, so the grid position falls out of a
 * real signed divide and remainder by eight. */
void AlbumEditAlbumACursorDisp(int photo_no, int off_x, int off_y,
                               u_char alpha, u_char rgb)                /* 672 */
{
    DISP_SPRT cursor_ds;
    int       csr_tate;
    int       csr_yoko;

    csr_tate = photo_no / ALBUM_THUMB_X_NUM;                            /* 678 */
    csr_yoko = photo_no % ALBUM_THUMB_X_NUM;                            /* 679 */

    CopySprDToSpr(&cursor_ds, &album_tex[ALBUM_TEX_A_CSR_FLARE]);       /* 683 */

    cursor_ds.x = (float)(csr_yoko * ALBUM_THUMB_X_PITCH + off_x + 207); /* 684 */
    cursor_ds.y = (float)(csr_tate * ALBUM_THUMB_Y_PITCH + off_y +  80); /* 684 */

    cursor_ds.alphar = 0x48;                                            /* 686 */
    cursor_ds.alpha  = (u_char)(((int)cursor_ds.alpha * (int)alpha) >> 7); /* 687 */
    cursor_ds.r = rgb;  cursor_ds.g = rgb;  cursor_ds.b = rgb;          /* 688 */

    DispSprD(&cursor_ds);                                               /* 689 */

    CopySprDToSpr(&cursor_ds, &album_tex[ALBUM_TEX_A_CSR]);             /* 691 */

    cursor_ds.x = (float)(csr_yoko * ALBUM_THUMB_X_PITCH + off_x + 202); /* 692 */
    cursor_ds.y = (float)(csr_tate * ALBUM_THUMB_Y_PITCH + off_y +  74); /* 692 */

    cursor_ds.alpha = (u_char)(((int)cursor_ds.alpha * (int)alpha) >> 7); /* 694 */
    cursor_ds.r = rgb;  cursor_ds.g = rgb;  cursor_ds.b = rgb;          /* 695 */

    DispSprD(&cursor_ds);                                               /* 696 */
}

void AlbumEditAlbumBCursorDisp(int photo_no, int off_x, int off_y,
                               u_char alpha, u_char rgb)                /* 709 */
{
    DISP_SPRT cursor_ds;
    int       csr_tate;
    int       csr_yoko;

    csr_tate = photo_no / ALBUM_THUMB_X_NUM;                            /* 715 */
    csr_yoko = photo_no % ALBUM_THUMB_X_NUM;                            /* 716 */

    CopySprDToSpr(&cursor_ds, &album_tex[ALBUM_TEX_B_CSR_FLARE]);       /* 720 */

    cursor_ds.x = (float)(csr_yoko * ALBUM_THUMB_X_PITCH + off_x +  36); /* 721 */
    cursor_ds.y = (float)(csr_tate * ALBUM_THUMB_Y_PITCH + off_y + 338); /* 721 */

    cursor_ds.alphar = 0x48;                                            /* 723 */
    cursor_ds.alpha  = (u_char)(((int)cursor_ds.alpha * (int)alpha) >> 7); /* 724 */
    cursor_ds.r = rgb;  cursor_ds.g = rgb;  cursor_ds.b = rgb;          /* 725 */

    DispSprD(&cursor_ds);                                               /* 726 */

    CopySprDToSpr(&cursor_ds, &album_tex[ALBUM_TEX_B_CSR]);             /* 728 */

    cursor_ds.x = (float)(csr_yoko * ALBUM_THUMB_X_PITCH + off_x +  31); /* 729 */
    cursor_ds.y = (float)(csr_tate * ALBUM_THUMB_Y_PITCH + off_y + 332); /* 729 */

    cursor_ds.alpha = (u_char)(((int)cursor_ds.alpha * (int)alpha) >> 7); /* 731 */
    cursor_ds.r = rgb;  cursor_ds.g = rgb;  cursor_ds.b = rgb;          /* 732 */

    DispSprD(&cursor_ds);                                               /* 733 */
}

/* ==========================================================================
 *  The album spine and its info window
 * ======================================================================== */

/* The album's own art: two pieces per type, placed by which half of the page
 * it belongs to. */
void AlbumEditAlbumDisp(int data_label, int album_type, int off_x, int off_y,
                        u_char alpha)                                   /* 746 */
{
    DISP_SPRT album_ds;
    int       i;

    static const int album_x_tbl[ALBUM_DATA_MAX][2] =
    {
        {  45, 171 },
        { 444, 570 },
    };
    static const int album_y_tbl[ALBUM_DATA_MAX][2] =
    {
        {  80,  83 },
        { 338, 341 },
    };
    static const int album_disp_tbl[ALBUM_TYPE_MAX][2] =
    {
        { 169, 170 },
        { 171, 172 },
        { 173, 174 },
        { 175, 176 },
        { 177, 178 },
        { 167, 168 },
        { 179, 180 },
    };

    for (i = 0; i < 2; i++) {                                           /* 772 */
        CopySprDToSpr(&album_ds, &album_tex[album_disp_tbl[album_type][i]]); /* 773 */

        album_ds.x = (float)(album_x_tbl[data_label][i] + off_x);       /* 774 */
        album_ds.y = (float)(album_y_tbl[data_label][i] + off_y);       /* 774 */

        album_ds.alpha = (u_char)(((int)album_ds.alpha * (int)alpha) >> 7); /* 775 */

        DispSprD(&album_ds);                                            /* 776 */
    }                                                                   /* 777 */
}

/* The info window's body -- a run of album_tex[] records whose first and last
 * index the type selects -- followed by the album's name plate.  Types 5 and 6
 * have a name plate of their own; every other type takes it from
 * album_info_name_tbl[], and a -1 there is an assert.
 *
 * The three name-plate arms each wrote their own copy of the four-statement
 * draw block; GCC cross-jumped them into one tail, which is why the two
 * branches carry a pair of eliminated line numbers each. */
void AlbumEditAlbumInfoWinDisp(int album_type, int off_x, int off_y, u_char alpha) /* 789 */
{
    DISP_SPRT info_ds;
    int       i;

    static const int album_info_tex_tbl[ALBUM_TYPE_MAX][2] =
    {
        { 195, 198 },
        { 199, 202 },
        { 203, 206 },
        { 207, 210 },
        { 211, 214 },
        { 191, 194 },
        { 191, 194 },
    };
    static const int album_info_name_tbl[ALBUM_TYPE_MAX] =
    {
        217, 218, 219, 220, 221, -1, -1,
    };

    for (i = album_info_tex_tbl[album_type][0];
         i <= album_info_tex_tbl[album_type][1]; i++) {                 /* 815 */
        CopySprDToSpr(&info_ds, &album_tex[i]);                         /* 816 */

        info_ds.x = info_ds.x + (float)off_x;                           /* 817 */
        info_ds.y = info_ds.y + (float)off_y;                           /* 817 */

        info_ds.alpha = (u_char)(((int)info_ds.alpha * (int)alpha) >> 7); /* 818 */

        DispSprD(&info_ds);                                             /* 819 */
    }                                                                   /* 820 */

    if (album_type == 5) {                                              /* 823 */
        CopySprDToSpr(&info_ds, &album_tex[ALBUM_TEX_INFO_NAME_5]);     /* 824 */

        info_ds.x = info_ds.x + (float)off_x;                           /* 825 */
        info_ds.y = info_ds.y + (float)off_y;                           /* 825 */

        info_ds.alpha = (u_char)(((int)info_ds.alpha * (int)alpha) >> 7); /* 827 */

        DispSprD(&info_ds);                                             /* 828 interpolated */
    } else if (album_type == 6) {                                       /* 830 */
        CopySprDToSpr(&info_ds, &album_tex[ALBUM_TEX_INFO_NAME_6]);     /* 831 */

        info_ds.x = info_ds.x + (float)off_x;                           /* 832 */
        info_ds.y = info_ds.y + (float)off_y;                           /* 832 */

        info_ds.alpha = (u_char)(((int)info_ds.alpha * (int)alpha) >> 7); /* 834 */

        DispSprD(&info_ds);                                             /* 835 interpolated */
    } else if (album_info_name_tbl[album_type] != -1) {                 /* 838 */
        CopySprDToSpr(&info_ds, &album_tex[album_info_name_tbl[album_type]]); /* 839 */

        info_ds.x = info_ds.x + (float)off_x;                           /* 840 */
        info_ds.y = info_ds.y + (float)off_y;                           /* 840 */

        info_ds.alpha = (u_char)(((int)info_ds.alpha * (int)alpha) >> 7); /* 841 */

        DispSprD(&info_ds);                                             /* 842 */
    } else {

        PRINT_ASSERT("Error! %s", __FUNCTION__);                        /* 845 */
    }
}

/* The album's number plate.  Types 5 and 6 share one, which is why the last
 * two entries repeat. */
void AlbumEditInfoNoDisp(int album_type, int off_x, int off_y, u_char alpha) /* 860 */
{
    DISP_SPRT no_ds;

    static const int no_tex_tbl[ALBUM_TYPE_MAX] =
    {
        261, 262, 263, 264, 265, 260, 260,
    };

    CopySprDToSpr(&no_ds, &album_tex[no_tex_tbl[album_type]]);          /* 876 */

    no_ds.x = no_ds.x + (float)off_x;                                   /* 877 */
    no_ds.y = no_ds.y + (float)off_y;                                   /* 877 */

    no_ds.alpha = (u_char)(((int)no_ds.alpha * (int)alpha) >> 7);       /* 878 */

    DispSprD(&no_ds);                                                   /* 879 */
}

/* The selected photo's 1-based number in the info window, in the digit set
 * that matches the album's own colour.  Both offsets are ignored. */
void AlbumEditInfoPhotoNoDisp(int album_type, int csr_num, int off_x, int off_y,
                              u_char alpha)                             /* 893 */
{
    static const int num_tex_tbl[ALBUM_TYPE_MAX] =
    {
        49, 59, 69, 79, 89, 39, 99,
    };

    DrawCmnNumberTex(csr_num + 1, 2, &album_tex[num_tex_tbl[album_type]],
                     219, 156, alpha, 0, 0);                            /* 908 */
}

/* The info window's item rows -- a second index range per type, drawn over
 * the body. */
void AlbumEditAlbumInfoWinItemDisp(int album_type, int off_x, int off_y,
                                   u_char alpha)                        /* 920 */
{
    DISP_SPRT info_ds;
    int       i;

    static const int item_tex_tbl[ALBUM_TYPE_MAX][2] =
    {
        { 225, 227 },
        { 228, 230 },
        { 231, 233 },
        { 234, 236 },
        { 237, 239 },
        { 222, 224 },
        { 222, 224 },
    };

    for (i = item_tex_tbl[album_type][0];
         i <= item_tex_tbl[album_type][1]; i++) {                       /* 936 */
        CopySprDToSpr(&info_ds, &album_tex[i]);                         /* 937 */

        info_ds.x = info_ds.x + (float)off_x;                           /* 938 */
        info_ds.y = info_ds.y + (float)off_y;                           /* 938 */

        info_ds.alpha = (u_char)(((int)info_ds.alpha * (int)alpha) >> 7); /* 939 */

        DispSprD(&info_ds);                                             /* 940 */
    }                                                                   /* 941 */
}

/* ==========================================================================
 *  The enlarged-photo view
 * ======================================================================== */

/* The frame round a photo blown up to full size: four corners, then two
 * vertical edges tiled six times and two horizontal ones tiled nineteen, each
 * with its own end cap.  The tile step is read back out of the DISP_SPRT
 * rather than written as a constant, which is why every copy sits inside its
 * loop. */
void AlbumEditPhotoFrameDisp(int off_x, int off_y, u_char alpha, void *pk2_addr) /* 953 */
{
    DISP_SPRT photo_ds;
    int       i;

    PK2SendVram((uintptr_t)pk2_addr, -1, -1, 0);                        /* 958 */

    for (i = 0; i < ALBUM_TEX_FRAME_NUM; i++) {                         /* 960 */
        CopySprDToSpr(&photo_ds, &album_tex[ALBUM_TEX_PHOTO_FRAME + i]); /* 961 */

        photo_ds.x = photo_ds.x + (float)off_x;                         /* 962 */
        photo_ds.y = photo_ds.y + (float)off_y;                         /* 962 */

        photo_ds.alpha = (u_char)(((int)photo_ds.alpha * (int)alpha) >> 7); /* 963 */

        DispSprD(&photo_ds);                                            /* 964 */
    }                                                                   /* 965 */

    for (i = 0; i < ALBUM_PHOTO_EDGE_V_NUM; i++) {                      /* 967 */
        CopySprDToSpr(&photo_ds, &album_tex[ALBUM_TEX_PHOTO_EDGE_L]);   /* 968 */

        photo_ds.x = photo_ds.x + (float)off_x;                         /* 969 */
        photo_ds.y = photo_ds.y + (float)(photo_ds.h * i) + (float)off_y; /* 969 */

        photo_ds.alpha = (u_char)(((int)photo_ds.alpha * (int)alpha) >> 7); /* 970 */

        DispSprD(&photo_ds);                                            /* 971 */
    }                                                                   /* 972 */

    CopySprDToSpr(&photo_ds, &album_tex[ALBUM_TEX_PHOTO_EDGE_L_CAP]);   /* 974 */

    photo_ds.x = photo_ds.x + (float)off_x;                             /* 975 */
    photo_ds.y = photo_ds.y + (float)off_y;                             /* 975 */

    photo_ds.alpha = (u_char)(((int)photo_ds.alpha * (int)alpha) >> 7); /* 976 */

    DispSprD(&photo_ds);                                                /* 977 */

    for (i = 0; i < ALBUM_PHOTO_EDGE_V_NUM; i++) {                      /* 979 */
        CopySprDToSpr(&photo_ds, &album_tex[ALBUM_TEX_PHOTO_EDGE_R]);   /* 980 */

        photo_ds.x = photo_ds.x + (float)off_x;                         /* 981 */
        photo_ds.y = photo_ds.y + (float)(photo_ds.h * i) + (float)off_y; /* 981 */

        photo_ds.alpha = (u_char)(((int)photo_ds.alpha * (int)alpha) >> 7); /* 982 */

        DispSprD(&photo_ds);                                            /* 983 */
    }                                                                   /* 984 */

    CopySprDToSpr(&photo_ds, &album_tex[ALBUM_TEX_PHOTO_EDGE_R_CAP]);   /* 986 */

    photo_ds.x = photo_ds.x + (float)off_x;                             /* 987 */
    photo_ds.y = photo_ds.y + (float)off_y;                             /* 987 */

    photo_ds.alpha = (u_char)(((int)photo_ds.alpha * (int)alpha) >> 7); /* 988 */

    DispSprD(&photo_ds);                                                /* 989 */

    for (i = 0; i < ALBUM_PHOTO_EDGE_H_NUM; i++) {                      /* 991 */
        CopySprDToSpr(&photo_ds, &album_tex[ALBUM_TEX_PHOTO_EDGE_T]);   /* 992 */

        photo_ds.x = photo_ds.x + (float)(photo_ds.w * i) + (float)off_x; /* 993 */
        photo_ds.y = photo_ds.y + (float)off_y;                         /* 993 */

        photo_ds.alpha = (u_char)(((int)photo_ds.alpha * (int)alpha) >> 7); /* 994 */

        DispSprD(&photo_ds);                                            /* 995 */
    }                                                                   /* 996 */

    CopySprDToSpr(&photo_ds, &album_tex[ALBUM_TEX_PHOTO_EDGE_T_CAP]);   /* 998 */

    photo_ds.x = photo_ds.x + (float)off_x;                             /* 999 */
    photo_ds.y = photo_ds.y + (float)off_y;                             /* 999 */

    photo_ds.alpha = (u_char)(((int)photo_ds.alpha * (int)alpha) >> 7); /* 1000 */

    DispSprD(&photo_ds);                                                /* 1001 */

    for (i = 0; i < ALBUM_PHOTO_EDGE_H_NUM; i++) {                      /* 1003 */
        CopySprDToSpr(&photo_ds, &album_tex[ALBUM_TEX_PHOTO_EDGE_B]);   /* 1004 */

        photo_ds.x = photo_ds.x + (float)(photo_ds.w * i) + (float)off_x; /* 1005 */
        photo_ds.y = photo_ds.y + (float)off_y;                         /* 1005 */

        photo_ds.alpha = (u_char)(((int)photo_ds.alpha * (int)alpha) >> 7); /* 1006 */

        DispSprD(&photo_ds);                                            /* 1007 */
    }                                                                   /* 1008 */

    CopySprDToSpr(&photo_ds, &album_tex[ALBUM_TEX_PHOTO_EDGE_B_CAP]);   /* 1010 */

    photo_ds.x = photo_ds.x + (float)off_x;                             /* 1011 */
    photo_ds.y = photo_ds.y + (float)off_y;                             /* 1011 */

    photo_ds.alpha = (u_char)(((int)photo_ds.alpha * (int)alpha) >> 7); /* 1012 */

    DispSprD(&photo_ds);                                                /* 1013 */
}

/* The badge over a photo that has been marked protected. */
void AlbumEditPhotoProtectionFrameDisp(int off_x, int off_y, u_char alpha) /* 1024 */
{
    DISP_SPRT photo_ds;
    int       i;

    for (i = 0; i < ALBUM_TEX_PROTECT_FRAME_NUM; i++) {                 /* 1030 */
        CopySprDToSpr(&photo_ds, &album_tex[ALBUM_TEX_PROTECT_FRAME + i]); /* 1031 */

        photo_ds.x = photo_ds.x + (float)off_x;                         /* 1032 */
        photo_ds.y = photo_ds.y + (float)off_y;                         /* 1032 */

        photo_ds.alpha = (u_char)(((int)photo_ds.alpha * (int)alpha) >> 7); /* 1033 */

        DispSprD(&photo_ds);                                            /* 1034 */
    }                                                                   /* 1035 */
}

/* ==========================================================================
 *  The edit menu
 * ======================================================================== */

/* One menu row's frame, lit.  It is one plate in two halves that open away
 * from a common seam at x + (first half's width): the left half scales about
 * its right edge, the right half about its left, so `scl` widens the row
 * symmetrically. */
void AlbumMenuSelFrameDisp(int data_label, int x, int y, u_char alpha,
                           float scl, u_char rgb)                       /* 1050 */
{
    DISP_SPRT sel_frame_ds;

    static const int album_frame_tbl[ALBUM_MENU_FRAME_MAX][2] =
    {
        { 117, 118 },
        { 121, 122 },
    };

    CopySprDToSpr(&sel_frame_ds, &album_tex[album_frame_tbl[data_label][0]]); /* 1061 */

    sel_frame_ds.x = (float)x;                                          /* 1062 */
    sel_frame_ds.y = (float)y;                                          /* 1062 */

    sel_frame_ds.alpha = (u_char)(((int)sel_frame_ds.alpha * (int)alpha) >> 7); /* 1063 */
    sel_frame_ds.r = rgb;  sel_frame_ds.g = rgb;  sel_frame_ds.b = rgb; /* 1064 */

    sel_frame_ds.csx = sel_frame_ds.x + (float)sel_frame_ds.w;          /* 1065 */
    sel_frame_ds.csy = sel_frame_ds.y;                                  /* 1065 */
    sel_frame_ds.scw = scl;                                             /* 1065 */
    sel_frame_ds.sch = 1.0f;                                            /* 1065 */

    DispSprD(&sel_frame_ds);                                            /* 1066 */

    CopySprDToSpr(&sel_frame_ds, &album_tex[album_frame_tbl[data_label][1]]); /* 1068 */

    sel_frame_ds.x = (float)(x + (int)album_tex[album_frame_tbl[data_label][0]].w); /* 1069 */
    sel_frame_ds.y = (float)y;                                          /* 1069 */

    sel_frame_ds.alpha = (u_char)(((int)sel_frame_ds.alpha * (int)alpha) >> 7); /* 1070 */
    sel_frame_ds.r = rgb;  sel_frame_ds.g = rgb;  sel_frame_ds.b = rgb; /* 1071 */

    sel_frame_ds.csx = sel_frame_ds.x;                                  /* 1072 */
    sel_frame_ds.csy = sel_frame_ds.y;                                  /* 1072 */
    sel_frame_ds.scw = scl;                                             /* 1072 */
    sel_frame_ds.sch = 1.0f;                                            /* 1072 */

    DispSprD(&sel_frame_ds);                                            /* 1073 */
}

/* The same row, dim.  The only difference from the lit one is that no rgb
 * tint is written. */
void AlbumMenuNonSelFrameDisp(int data_label, int x, int y, u_char alpha,
                              float scl)                                /* 1087 */
{
    DISP_SPRT sel_frame_ds;

    static const int album_frame_tbl[ALBUM_MENU_FRAME_MAX][2] =
    {
        { 119, 120 },
        { 123, 124 },
    };

    CopySprDToSpr(&sel_frame_ds, &album_tex[album_frame_tbl[data_label][0]]); /* 1098 */

    sel_frame_ds.x = (float)x;                                          /* 1099 */
    sel_frame_ds.y = (float)y;                                          /* 1099 */

    sel_frame_ds.alpha = (u_char)(((int)sel_frame_ds.alpha * (int)alpha) >> 7); /* 1100 */

    sel_frame_ds.csx = sel_frame_ds.x + (float)sel_frame_ds.w;          /* 1101 */
    sel_frame_ds.csy = sel_frame_ds.y;                                  /* 1101 */
    sel_frame_ds.scw = scl;                                             /* 1101 */
    sel_frame_ds.sch = 1.0f;                                            /* 1101 */

    DispSprD(&sel_frame_ds);                                            /* 1102 */

    CopySprDToSpr(&sel_frame_ds, &album_tex[album_frame_tbl[data_label][1]]); /* 1104 */

    sel_frame_ds.x = (float)(x + (int)album_tex[album_frame_tbl[data_label][0]].w); /* 1105 */
    sel_frame_ds.y = (float)y;                                          /* 1105 */

    sel_frame_ds.alpha = (u_char)(((int)sel_frame_ds.alpha * (int)alpha) >> 7); /* 1106 */

    sel_frame_ds.csx = sel_frame_ds.x;                                  /* 1107 */
    sel_frame_ds.csy = sel_frame_ds.y;                                  /* 1107 */
    sel_frame_ds.scw = scl;                                             /* 1107 */
    sel_frame_ds.sch = 1.0f;                                            /* 1107 */

    DispSprD(&sel_frame_ds);                                            /* 1108 */
}

/* The yes / no window: the two answer plates (lit pair or dim pair, chosen by
 * the cursor), then the flare under the selected one and the two cursor
 * halves round it. */
void AlbumConfYesNoDisp(int conf_csr, int off_x, int off_y, u_char alpha,
                        u_char rgb)                                     /* 1121 */
{
    DISP_SPRT conf_ds;
    int       i;

    static const int conf_tex_tbl[ALBUM_CONF_CSR_MAX][2] =
    {
        { 240, 243 },
        { 241, 242 },
    };
    static const int csr_x_tbl[ALBUM_CONF_CSR_MAX][2] =
    {
        { 304, 396 },
        { 436, 529 },
    };
    static const int flare_x_tbl[ALBUM_CONF_CSR_MAX][2] =
    {
        { 301, 393 },
        { 433, 526 },
    };

    for (i = 0; i < 2; i++) {                                           /* 1142 */
        CopySprDToSpr(&conf_ds, &album_tex[conf_tex_tbl[conf_csr][i]]); /* 1143 */

        conf_ds.x = conf_ds.x + (float)off_x;                           /* 1144 */
        conf_ds.y = conf_ds.y + (float)off_y;                           /* 1144 */

        conf_ds.alpha = (u_char)(((int)conf_ds.alpha * (int)alpha) >> 7); /* 1145 */

        DispSprD(&conf_ds);                                             /* 1146 */
    }                                                                   /* 1147 */

    CopySprDToSpr(&conf_ds, &album_tex[ALBUM_TEX_CONF_FLARE]);          /* 1150 */

    conf_ds.x = (float)(flare_x_tbl[conf_csr][0] + off_x);              /* 1151 */
    conf_ds.y = conf_ds.y + (float)off_y;                               /* 1151 */

    conf_ds.alpha = (u_char)(((int)conf_ds.alpha * (int)alpha) >> 7);   /* 1152 */
    conf_ds.r = rgb;  conf_ds.g = rgb;  conf_ds.b = rgb;                /* 1153 */

    DispSprD(&conf_ds);                                                 /* 1154 */

    CopySprDToSpr(&conf_ds, &album_tex[ALBUM_TEX_CONF_FLARE + 1]);      /* 1155 */

    conf_ds.x = (float)(flare_x_tbl[conf_csr][1] + off_x);              /* 1156 */
    conf_ds.y = conf_ds.y + (float)off_y;                               /* 1156 */

    conf_ds.alpha = (u_char)(((int)conf_ds.alpha * (int)alpha) >> 7);   /* 1157 */
    conf_ds.r = rgb;  conf_ds.g = rgb;  conf_ds.b = rgb;                /* 1158 */

    DispSprD(&conf_ds);                                                 /* 1159 */

    CopySprDToSpr(&conf_ds, &album_tex[ALBUM_TEX_CONF_CSR]);            /* 1162 */

    conf_ds.x = (float)(csr_x_tbl[conf_csr][0] + off_x);                /* 1163 */
    conf_ds.y = conf_ds.y + (float)off_y;                               /* 1163 */

    conf_ds.alpha = (u_char)(((int)conf_ds.alpha * (int)alpha) >> 7);   /* 1164 */

    DispSprD(&conf_ds);                                                 /* 1165 */

    CopySprDToSpr(&conf_ds, &album_tex[ALBUM_TEX_CONF_CSR + 1]);        /* 1166 */

    conf_ds.x = (float)(csr_x_tbl[conf_csr][1] + off_x);                /* 1167 */
    conf_ds.y = conf_ds.y + (float)off_y;                               /* 1167 */

    conf_ds.alpha = (u_char)(((int)conf_ds.alpha * (int)alpha) >> 7);   /* 1168 */

    DispSprD(&conf_ds);                                                 /* 1169 */
}

/* One menu label.  Note that entry 0 is not adjacent to the other four --
 * the plate for the first row lives past them in the bank. */
void AlbumMenuItemDisp(int menu_label, int x, int y, u_char alpha)      /* 1182 */
{
    DISP_SPRT menu_ds;

    static const int menu_item_tbl[5] =
    {
        116, 110, 111, 112, 113,
    };

    CopySprDToSpr(&menu_ds, &album_tex[menu_item_tbl[menu_label]]);     /* 1195 */

    menu_ds.x = (float)x;                                               /* 1196 */
    menu_ds.y = (float)y;                                               /* 1196 */

    menu_ds.alpha = (u_char)(((int)menu_ds.alpha * (int)alpha) >> 7);   /* 1197 */

    DispSprD(&menu_ds);                                                 /* 1198 */
}

/* The button caption strip.  Both offsets are ignored -- the group's own
 * placement comes from draw_cmn.c. */
void AlbumEditCaptionDisp(int off_x, int off_y, u_char alpha)           /* 1209 */
{
    DrawCmnCapGroup_W(10, 10, alpha, 0);                                /* 1212 */
}

/* ==========================================================================
 *  The memory-card slot-select window
 * ======================================================================== */

/* "Which slot?": a two-line window, a rule under the title, the slot art and
 * the two rows, with the selected one lit. */
void AlbumSlotSelWinDisp(int cursor, int off_x, int off_y, u_char alpha) /* 1247 */
{
    int i;
    int col_label;

    static const int msg_id_tbl[ALBUM_SLOT_SEL_MSG_MAX] = {   0,   1 };
    static const int msg_y_tbl[ALBUM_SLOT_SEL_MSG_MAX]  = { 186, 224 };

    DrawCmnWindow(0, 141.0f, 119.0f, 356.0f, 160.0f, alpha, 89);        /* 1264 */

    DrawCmnLine(141.0f, 160.0f, 356.0f, 1, alpha, 0);                   /* 1268 */

    AlbumSlotSelectDisp(off_x, off_y, alpha);                           /* 1271 */

    for (i = 0; i < ALBUM_SLOT_SEL_MSG_MAX; i++) {                      /* 1273 */
        col_label = (cursor == i) ? 7 : 1;                              /* 1274 */

        PrintMsg_Arrange(0, msg_id_tbl[i], off_x + 320, msg_y_tbl[i] + off_y,
                         col_label, alpha, 0, 0, 0, 2);                 /* 1282 */
    }                                                                   /* 1283 */

    DrawCmnSelCsr(0, 160.0f, (float)(cursor * 38 + 182), alpha, 320.0f, 1); /* 1287 */
}

/* Both offsets are ignored, as in AlbumEditCaptionDisp. */
void AlbumSlotSelCaptionDisp(int off_x, int off_y, u_char alpha)        /* 1297 */
{
    DrawCmnCapGroup_W(0, 0, alpha, 0);                                  /* 1300 */
}

static void AlbumSlotSelectDisp(int off_x, int off_y, u_char alpha)     /* 1326 */
{
    DISP_SPRT slot_ds;
    int       i;

    for (i = 0; i < 2; i++) {                                           /* 1332 */
        CopySprDToSpr(&slot_ds, &album_tex[ALBUM_TEX_SLOT_SEL + i]);    /* 1333 */

        slot_ds.x = slot_ds.x + (float)off_x;                           /* 1334 */
        slot_ds.y = slot_ds.y + (float)off_y;                           /* 1334 */

        slot_ds.alpha = (u_char)(((int)slot_ds.alpha * (int)alpha) >> 7); /* 1335 */

        DispSprD(&slot_ds);                                             /* 1336 */
    }                                                                   /* 1337 */
}

/* ==========================================================================
 *  The memory-card save / load screen
 *
 *  Every routine below rejects an album_type of 5 or 6 -- those two never
 *  reach a card, so their rows in the tables are -1 rather than sprite
 *  indices, and reaching one is a PRINT_WARNING and nothing drawn.
 * ======================================================================== */

void AlbumSaveSelAlbumDisp(int album_type, int off_x, int off_y, u_char alpha) /* 1350 */
{
    DISP_SPRT album_ds;

    static const int album_tex_tbl[ALBUM_TYPE_MAX] =
    {
        390, 391, 392, 393, 394, -1, -1,
    };

    if (album_type < ALBUM_SAVE_TYPE_MAX) {                             /* 1365 */
        CopySprDToSpr(&album_ds, &album_tex[album_tex_tbl[album_type]]); /* 1366 */

        album_ds.x = album_ds.x + (float)off_x;                         /* 1367 */
        album_ds.y = album_ds.y + (float)off_y;                         /* 1367 */

        album_ds.alpha = (u_char)(((int)album_ds.alpha * (int)alpha) >> 7); /* 1368 */

        DispSprD(&album_ds);                                            /* 1369 */
    } else {

        PRINT_WARNING("Warning! %s", __FUNCTION__);                     /* 1372 */
    }
}

/* The lit left / right arrows either side of the album name.  Their x comes
 * from album_dat.c's per-language tables, and GetLanguage() is called once per
 * arrow rather than hoisted. */
void AlbumSaveSelAlbumCsrDisp(int album_type, int off_x, int off_y,
                              u_char alpha, u_char rgb)                 /* 1387 */
{
    DISP_SPRT album_ds;

    static const int album_tex_tbl[ALBUM_TYPE_MAX][2] =
    {
        { 420, 421 },
        { 422, 423 },
        { 424, 425 },
        { 426, 427 },
        { 428, 429 },
        {  -1,  -1 },
        {  -1,  -1 },
    };

    if (album_type < ALBUM_SAVE_TYPE_MAX) {                             /* 1403 */
        CopySprDToSpr(&album_ds, &album_tex[album_tex_tbl[album_type][0]]); /* 1417 */

        album_ds.x = (float)(album_left_csr_x[album_type][GetLanguage()] + off_x); /* 1418 */
        album_ds.y = album_ds.y + (float)off_y;                         /* 1418 */

        album_ds.alpha = (u_char)(((int)album_ds.alpha * (int)alpha) >> 7); /* 1419 */
        album_ds.r = rgb;  album_ds.g = rgb;  album_ds.b = rgb;         /* 1420 */
        album_ds.alphar = 0x48;                                         /* 1421 */

        DispSprD(&album_ds);                                            /* 1422 */

        CopySprDToSpr(&album_ds, &album_tex[album_tex_tbl[album_type][1]]); /* 1424 */

        album_ds.x = (float)(album_right_csr_x[album_type][GetLanguage()] + off_x); /* 1425 */
        album_ds.y = album_ds.y + (float)off_y;                         /* 1425 */

        album_ds.alpha = (u_char)(((int)album_ds.alpha * (int)alpha) >> 7); /* 1426 */
        album_ds.r = rgb;  album_ds.g = rgb;  album_ds.b = rgb;         /* 1427 */
        album_ds.alphar = 0x48;                                         /* 1428 */

        DispSprD(&album_ds);                                            /* 1429 */
    } else {

        PRINT_WARNING("Warning! %s", __FUNCTION__);                     /* 1433 */
    }
}

/* The same two arrows, dim: no additive blend, and a different plate pair. */
void AlbumSaveNonSelAlbumCsrDisp(int album_type, int off_x, int off_y,
                                 u_char alpha, u_char rgb)              /* 1448 */
{
    DISP_SPRT album_ds;

    static const int album_tex_tbl[ALBUM_TYPE_MAX][2] =
    {
        { 410, 411 },
        { 412, 413 },
        { 414, 415 },
        { 416, 417 },
        { 418, 419 },
        {  -1,  -1 },
        {  -1,  -1 },
    };

    if (album_type < ALBUM_SAVE_TYPE_MAX) {                             /* 1464 */
        CopySprDToSpr(&album_ds, &album_tex[album_tex_tbl[album_type][0]]); /* 1475 */

        album_ds.x = (float)(album_left_csr_x[album_type][GetLanguage()] + off_x); /* 1476 */
        album_ds.y = album_ds.y + (float)off_y;                         /* 1476 */

        album_ds.alpha = (u_char)(((int)album_ds.alpha * (int)alpha) >> 7); /* 1477 */
        album_ds.r = rgb;  album_ds.g = rgb;  album_ds.b = rgb;         /* 1478 */

        DispSprD(&album_ds);                                            /* 1479 */

        CopySprDToSpr(&album_ds, &album_tex[album_tex_tbl[album_type][1]]); /* 1481 */

        album_ds.x = (float)(album_right_csr_x[album_type][GetLanguage()] + off_x); /* 1482 */
        album_ds.y = album_ds.y + (float)off_y;                         /* 1482 */

        album_ds.alpha = (u_char)(((int)album_ds.alpha * (int)alpha) >> 7); /* 1483 */
        album_ds.r = rgb;  album_ds.g = rgb;  album_ds.b = rgb;         /* 1484 */

        DispSprD(&album_ds);                                            /* 1485 */
    } else {

        PRINT_WARNING("Warning! %s", __FUNCTION__);                     /* 1489 */
    }
}

/* The album's name, as a message rather than a plate -- which is why it needs
 * a per-language x.  The two y values put types 0..2 on the upper row and 3..4
 * on the lower one. */
void AlbumSaveSelAlbumNameDisp(int album_type, int off_x, int off_y,
                               u_char alpha, int col_label)             /* 1504 */
{
    static const int album_msg_tbl[ALBUM_TYPE_MAX] =
    {
        2, 3, 4, 5, 6, -1, -1,
    };
    static const int msg_y_tbl[ALBUM_TYPE_MAX] =
    {
        79, 79, 79, 310, 310, -1, -1,
    };

    if (album_type < ALBUM_SAVE_TYPE_MAX) {                             /* 1541 */

        PrintMsg(0, album_msg_tbl[album_type],
                 album_name_x_tbl[album_type][GetLanguage()],
                 msg_y_tbl[album_type], col_label, alpha, 0);           /* 1543 */
    } else {

        PRINT_WARNING("Warning! %s", __FUNCTION__);                     /* 1546 */
    }
}

/* The "MEMORY CARD (8MB)" plate over the slot the screen is working on, plus
 * the slot's own name. */
void AlbumSaveSelSlotDisp(int sel_slot, int off_x, int off_y, u_char alpha,
                          u_char rgb)                                   /* 1560 */
{
    DISP_SPRT album_ds;
    int       i;

    static const int msg_id_tbl[2] = { 11, 12 };

    for (i = 0; i < 2; i++) {                                           /* 1571 */
        CopySprDToSpr(&album_ds, &album_tex[ALBUM_TEX_MC_SLOT + i]);    /* 1572 */

        album_ds.x = album_ds.x + (float)off_x;                         /* 1573 */
        album_ds.y = album_ds.y + (float)off_y;                         /* 1573 */

        album_ds.alpha = (u_char)(((int)album_ds.alpha * (int)alpha) >> 7); /* 1574 */
        album_ds.r = rgb;  album_ds.g = rgb;  album_ds.b = rgb;         /* 1575 */
        album_ds.alphar = 0x48;                                         /* 1576 */

        DispSprD(&album_ds);                                            /* 1577 */
    }                                                                   /* 1578 */

    PrintMsg_Arrange(0, msg_id_tbl[sel_slot], 313, 53, 22, alpha, 0, 0, 0, 2); /* 1581 */
}

/* The glow under the selected album plate. */
void AlbumSaveSelAlbumCsrFlareDisp(int album_type, int off_x, int off_y,
                                   u_char alpha, u_char rgb)            /* 1595 */
{
    DISP_SPRT album_ds;

    static const int album_tex_tbl[ALBUM_TYPE_MAX] =
    {
        395, 396, 397, 398, 399, -1, -1,
    };

    if (album_type < ALBUM_SAVE_TYPE_MAX) {                             /* 1610 */
        CopySprDToSpr(&album_ds, &album_tex[album_tex_tbl[album_type]]); /* 1611 */

        album_ds.x = album_ds.x + (float)off_x;                         /* 1612 */
        album_ds.y = album_ds.y + (float)off_y;                         /* 1612 */

        album_ds.alpha = (u_char)(((int)album_ds.alpha * (int)alpha) >> 7); /* 1613 */
        album_ds.r = rgb;  album_ds.g = rgb;  album_ds.b = rgb;         /* 1614 */
        album_ds.alphar = 0x48;                                         /* 1615 */

        DispSprD(&album_ds);                                            /* 1616 */
    } else {

        PRINT_WARNING("Warning! %s", __FUNCTION__);                     /* 1619 */
    }
}

/* The two mask plates that grey an album out when its slot cannot be used. */
void AlbumSaveAlbumMaskDisp(int album_type, int off_x, int off_y, u_char alpha,
                            u_char rgb)                                 /* 1634 */
{
    DISP_SPRT album_ds;
    int       i;

    static const int album_tex_tbl[ALBUM_TYPE_MAX][2] =
    {
        { 400, 401 },
        { 402, 403 },
        { 404, 405 },
        { 406, 407 },
        { 408, 409 },
        {  -1,  -1 },
        {  -1,  -1 },
    };

    if (album_type < ALBUM_SAVE_TYPE_MAX) {                             /* 1650 */

        for (i = 0; i < 2; i++) {                                       /* 1651 */
            /* ROM BUG, reproduced in effect but not in form.  The guard is
             * written album_tex_tbl[ALBUM_TYPE_MAX][i] -- one row past the end
             * of the table, which lands on the "10ALBUM_INFO" type_info string
             * the linker parked immediately after it at rodata 3a08b8.  Neither
             * word is -1, so the ROM always draws both masks.  Rows 0..4 hold
             * no -1 either, and album_type is already known to be < 5, so the
             * in-range spelling used here agrees with the ROM for every
             * reachable input; the ROM's own index is undefined behaviour on
             * the host and is not written out. */
            if (album_tex_tbl[album_type][i] != -1) {                   /* 1652 */
                CopySprDToSpr(&album_ds, &album_tex[album_tex_tbl[album_type][i]]); /* 1653 */

                album_ds.x = album_ds.x + (float)off_x;                 /* 1654 */
                album_ds.y = album_ds.y + (float)off_y;                 /* 1654 */

                album_ds.alpha = (u_char)(((int)album_ds.alpha * (int)alpha) >> 7); /* 1655 */
                album_ds.r = rgb;  album_ds.g = rgb;  album_ds.b = rgb; /* 1656 */

                DispSprD(&album_ds);                                    /* 1657 */
            }
        }                                                               /* 1659 */
    } else {

        PRINT_WARNING("Warning! %s", __FUNCTION__);                     /* 1662 */
    }
}

/* The card-message window.  Both offsets are ignored. */
void AlbumSaveMsgWinDisp(int off_x, int off_y, u_char alpha)            /* 1674 */
{
    DrawCmnTwoLineWindow(0, 45.0f, 126.0f, 550.0f, 216.0f, alpha, 102); /* 1679 */
}

/* The same window over a dimmed screen -- what the album puts up while the
 * card is being read or written. */
void AlbumMcMsgWinDisp(int off_x, int off_y, u_char alpha)              /* 1691 */
{
    AlbumBlackBgDisp(0, 0, alpha, 81);                                  /* 1695 */

    AlbumSaveMsgWinDisp(off_x, off_y, alpha);                           /* 1698 */
}
