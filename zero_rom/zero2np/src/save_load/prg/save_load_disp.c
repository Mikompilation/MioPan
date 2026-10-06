// FILE: /home/zero_rom/zero2np/src/save_load/prg/save_load_disp.c
//
// The save / load screen's drawing layer: 31 primitives that between them
// compose every frame outgame/loadgame.c and game_data_save.c put up.  There
// is no state here at all -- no statics, no work block.  Each routine is
// "copy one save_load_tex[] record into a DISP_SPRT, offset it, scale its
// alpha by the caller's, draw it", and the whole file is that shape twenty
// times over.
//
// The five save slots are one set of sprite records drawn five times, stepped
// SAVE_LOAD_DATA_PITCH (118) pixels apart by the caller's disp_label.  That is
// why every per-slot routine takes the same three arguments and adds the same
// disp_label * 118 to x.
//
// Three things worth knowing before touching it:
//
//  * off_x / off_y are not honoured everywhere.  SaveLoadCaptionDisp ignores
//    both outright; SaveLoadClearNumberDisp reads neither; and the two
//    PrintMsg lines in SaveLoadMcPlayDataInfoDisp are placed at fixed screen
//    coordinates while the play-time readout beside them does add the offset.
//    Both callers pass 0, 0 everywhere, so none of it shows.
//
//  * A sprite's x and y update is ONE source line, not two.  Every draw here
//    has a single $LM covering both stores; they are written out as two C
//    statements sharing one /* NNN */, the same convention savepoint_disp.c
//    already uses.  Ghidra prints the pair in the wrong order -- read the
//    store offsets (0x24 is x, 0x28 is y) rather than the decompiler.
//
//  * The two SQAR_DAT tables in .rodata are LOCAL array initialisers, not
//    file statics.  GCC emits `SQAR_DAT win_bg[4] = {...};` as a .rodata blob
//    copied into the stack frame, which reads exactly like a static table;
//    globals.txt is right to list none.  save_load_disp.o has no data of its
//    own beyond those two blobs and its assert strings.
//
// Reconstructed from the Feb 6 2004 prototype (SLES_523.84), save_load_disp.o.
//
// Trailing /* NNN */ comments are the original source line numbers, measured
// from the $LM stabs.

#include "save_load_disp.h"

#include "../dat/save_load_dat.h"                    // save_load_tex[]
#include "../../common/utility2.h"                   // PRINT_ASSERT
#include "../../graphics/graph2d/draw_cmn.h"         // DrawCmn*
#include "../../graphics/graph2d/g2d_draw.h"         // DISP_SPRT / DISP_SQAR / CopySprDToSpr
#include "../../graphics/graph2d/message.h"          // PrintMsg / PrintNumber_N
#include "../../graphics/graph2d/tim2.h"             // PK2SendVram
#include "../../ingame/menu/plyr_room_info.h"        // GetMapLabelToRoomLabel
#include "../../outgame/tim_dat/outgame_dat.h"       // out_game_tex[]

/* Memory-card save slots. */
#define SAVE_LOAD_DATA_MAX 5

/* save_load_tex[] indices, by part.  See save_load_dat.c for the whole map. */
#define SAVE_LOAD_TEX_FRAME             0       /* 13 pieces                */
#define SAVE_LOAD_TEX_FRAME_NUM         13
#define SAVE_LOAD_TEX_STRIP_L           13
#define SAVE_LOAD_TEX_STRIP_M           14      /* tiled 8 times            */
#define SAVE_LOAD_TEX_STRIP_M_NUM       8
#define SAVE_LOAD_TEX_STRIP_R           15
#define SAVE_LOAD_TEX_CURSOR            16      /* 2 pieces                 */
#define SAVE_LOAD_TEX_CURSOR_NUM        2
#define SAVE_LOAD_TEX_SEL_FLARE         18      /* 2 pieces                 */
#define SAVE_LOAD_TEX_SNAP_SHADOW       20
#define SAVE_LOAD_TEX_NON_SEL_NO        21
#define SAVE_LOAD_TEX_NON_SEL_DATA_NUM  22      /* + disp_label             */
#define SAVE_LOAD_TEX_NON_SEL_LINE      27
#define SAVE_LOAD_TEX_SEL_NO            28
#define SAVE_LOAD_TEX_SEL_DATA_NUM      29      /* + disp_label             */
#define SAVE_LOAD_TEX_SEL_LINE          34
#define SAVE_LOAD_TEX_CLEAR_FLARE       35      /* 4 quadrants              */
#define SAVE_LOAD_TEX_CLEAR_FLARE_NUM   4
#define SAVE_LOAD_TEX_CLEAR_FRAME       39
#define SAVE_LOAD_TEX_NON_CLEAR_MASK    40
#define SAVE_LOAD_TEX_NUMBER            41      /* + digit                  */
#define SAVE_LOAD_TEX_MC_CAPTION        51      /* 2 pieces                 */
#define SAVE_LOAD_TEX_MC_SLOT           53      /* + slot_label             */
#define SAVE_LOAD_TEX_TITLE_SAVE        55      /* 2 pieces                 */
#define SAVE_LOAD_TEX_TITLE_LOAD        57      /* 2 pieces                 */
#define SAVE_LOAD_TEX_TITLE_NUM         2
#define SAVE_LOAD_TEX_SNAP_SHOT         59

/* out_game_tex[] -- the shared title frame, out of the OUTGAME pak. */
#define OUT_GAME_TEX_TITLE_FRAME        0
#define OUT_GAME_TEX_TITLE_FRAME_NUM    2

/* The clear count is two digits at x = 105 + 13 per digit, y = 123, all
 * relative to the slot column. */
#define SAVE_LOAD_CLEAR_NUM_DIGIT   2
#define SAVE_LOAD_CLEAR_NUM_X       105
#define SAVE_LOAD_CLEAR_NUM_Y       123
#define SAVE_LOAD_CLEAR_NUM_PITCH   13

/* ==========================================================================
 *  Frame furniture
 * ======================================================================== */

/* The whole static frame in one call: four dimming quads, the border, the
 * memory-card caption, the screen caption, and the five clear-count frames. */
void SaveLoadCmnBaseDisp(int off_x, int off_y, u_char alpha, void *pk2_addr,
                         int disp_slot)                                 /* 58 */
{
    DISP_SQAR dsq;
    int       i;

    /* A local initialiser, not a static table -- GCC copies the .rodata image
     * at 3c4a40 straight into the stack frame. */
    SQAR_DAT win_bg[4] =                                                /* 61 */
    {
        /* w,    h,   x,   y, pri, r, g, b, alpha */
        {  60, 238,  24,  99,   0, 0, 0, 0, 51 },
        {  60, 238, 551,  99,   0, 0, 0, 0, 51 },
        { 467, 132,  84,  99,   0, 0, 0, 0, 51 },
        { 467,  36,  84, 301,   0, 0, 0, 0, 51 },
    };

    for (i = 0; i < 4; i++) {                                           /* 81 */
        CopySqrDToSqr(&dsq, &win_bg[i]);                                /* 82 */

        dsq.alpha = (u_char)(((int)dsq.alpha * (int)alpha) >> 7);       /* 83 */

        DispSqrD(&dsq);                                                 /* 84 */
    }                                                                   /* 85 */

    SaveLoadFrameDisp(off_x, off_y, alpha, pk2_addr);                   /* 88 */
    SaveLoadMemoryCardSlotDisp(off_x, off_y, alpha, pk2_addr, disp_slot); /* 90 */
    SaveLoadCaptionDisp(off_x, off_y, alpha);                           /* 92 */

    for (i = 0; i < SAVE_LOAD_DATA_MAX; i++) {                          /* 95 */
        SaveLoadClearFrameDisp(off_x, off_y, alpha, pk2_addr, i);       /* 96 */
    }                                                                   /* 97 */
}

/* Five empty rows.  What the screen shows while the card is being checked and
 * nothing is known about any slot yet. */
void SaveLoadMcCheckDisp(int off_x, int off_y, u_char alpha, void *pk2_addr) /* 109 */
{
    int i;

    for (i = 0; i < SAVE_LOAD_DATA_MAX; i++) {                          /* 114 */
        SaveLoadSnapShadowDisp(off_x, off_y, alpha, pk2_addr, i);       /* 116 */
        SaveLoadNonClearMaskDisp(off_x, off_y, alpha, pk2_addr, i);     /* 118 */
        SaveLoadNonSelLineDisp(off_x, off_y, alpha, pk2_addr, i);       /* 120 */
    }                                                                   /* 121 */
}

/* The one part that comes out of the OUTGAME pak rather than this screen's --
 * which is why loadgame.c passes GetOutGameCmnTexAddr() here and its own
 * buffer everywhere else. */
void SaveLoadTitleFrameDisp(int off_x, int off_y, u_char alpha, void *pk2_addr) /* 137 */
{
    DISP_SPRT title_ds;
    int       i;

    PK2SendVram((uintptr_t)pk2_addr, -1, -1, 0);                        /* 142 */

    for (i = 0; i < OUT_GAME_TEX_TITLE_FRAME_NUM; i++) {                /* 144 */
        CopySprDToSpr(&title_ds, &out_game_tex[OUT_GAME_TEX_TITLE_FRAME + i]); /* 145 */

        title_ds.x = title_ds.x + (float)off_x;                         /* 146 */
        title_ds.y = title_ds.y + (float)off_y;                         /* 146 */

        title_ds.alpha = (u_char)(((int)title_ds.alpha * (int)alpha) >> 7); /* 147 */

        DispSprD(&title_ds);                                            /* 148 */
    }                                                                   /* 149 */
}

void SaveLoadTitleSaveDisp(int off_x, int off_y, u_char alpha, void *pk2_addr) /* 161 */
{
    DISP_SPRT title_ds;
    int       i;

    PK2SendVram((uintptr_t)pk2_addr, -1, -1, 0);                        /* 166 */

    for (i = 0; i < SAVE_LOAD_TEX_TITLE_NUM; i++) {                     /* 168 */
        CopySprDToSpr(&title_ds, &save_load_tex[SAVE_LOAD_TEX_TITLE_SAVE + i]); /* 169 */

        title_ds.x = title_ds.x + (float)off_x;                         /* 170 */
        title_ds.y = title_ds.y + (float)off_y;                         /* 170 */

        title_ds.alpha = (u_char)(((int)title_ds.alpha * (int)alpha) >> 7); /* 171 */

        DispSprD(&title_ds);                                            /* 172 */
    }                                                                   /* 173 */
}

void SaveLoadTitleLoadDisp(int off_x, int off_y, u_char alpha, void *pk2_addr) /* 185 */
{
    DISP_SPRT title_ds;
    int       i;

    PK2SendVram((uintptr_t)pk2_addr, -1, -1, 0);                        /* 190 */

    for (i = 0; i < SAVE_LOAD_TEX_TITLE_NUM; i++) {                     /* 192 */
        CopySprDToSpr(&title_ds, &save_load_tex[SAVE_LOAD_TEX_TITLE_LOAD + i]); /* 193 */

        title_ds.x = title_ds.x + (float)off_x;                         /* 194 */
        title_ds.y = title_ds.y + (float)off_y;                         /* 194 */

        title_ds.alpha = (u_char)(((int)title_ds.alpha * (int)alpha) >> 7); /* 195 */

        DispSprD(&title_ds);                                            /* 196 */
    }                                                                   /* 197 */
}

/* The border: thirteen fixed pieces, then the slot strip -- a left cap, one
 * 50-pixel tile repeated eight times, and a right cap.  The tile's own width
 * is read back out of the DISP_SPRT rather than written as a constant, which
 * is why the copy sits inside the loop. */
void SaveLoadFrameDisp(int off_x, int off_y, u_char alpha, void *pk2_addr) /* 209 */
{
    DISP_SPRT frame_ds;
    int       i;

    PK2SendVram((uintptr_t)pk2_addr, -1, -1, 0);                        /* 214 */

    for (i = 0; i < SAVE_LOAD_TEX_FRAME_NUM; i++) {                     /* 216 */
        CopySprDToSpr(&frame_ds, &save_load_tex[SAVE_LOAD_TEX_FRAME + i]); /* 217 */

        frame_ds.x = frame_ds.x + (float)off_x;                         /* 218 */
        frame_ds.y = frame_ds.y + (float)off_y;                         /* 218 */

        frame_ds.alpha = (u_char)(((int)frame_ds.alpha * (int)alpha) >> 7); /* 219 */

        DispSprD(&frame_ds);                                            /* 220 */
    }                                                                   /* 221 */

    CopySprDToSpr(&frame_ds, &save_load_tex[SAVE_LOAD_TEX_STRIP_L]);    /* 223 */

    frame_ds.x = frame_ds.x + (float)off_x;                             /* 224 */
    frame_ds.y = frame_ds.y + (float)off_y;                             /* 224 */

    frame_ds.alpha = (u_char)(((int)frame_ds.alpha * (int)alpha) >> 7); /* 225 */

    DispSprD(&frame_ds);                                                /* 226 */

    for (i = 0; i < SAVE_LOAD_TEX_STRIP_M_NUM; i++) {                   /* 228 */
        CopySprDToSpr(&frame_ds, &save_load_tex[SAVE_LOAD_TEX_STRIP_M]); /* 229 */

        /* i * w is unsigned here -- frame_ds.w is u_int, and the ROM emits
         * the unsigned int-to-float sequence rather than a plain cvt.s.w. */
        frame_ds.x = frame_ds.x + (float)(i * frame_ds.w) + (float)off_x; /* 230 */
        frame_ds.y = frame_ds.y + (float)off_y;                         /* 230 */

        frame_ds.alpha = (u_char)(((int)frame_ds.alpha * (int)alpha) >> 7); /* 231 */

        DispSprD(&frame_ds);                                            /* 232 */
    }                                                                   /* 233 */

    CopySprDToSpr(&frame_ds, &save_load_tex[SAVE_LOAD_TEX_STRIP_R]);    /* 235 */

    frame_ds.x = frame_ds.x + (float)off_x;                             /* 236 */
    frame_ds.y = frame_ds.y + (float)off_y;                             /* 236 */

    frame_ds.alpha = (u_char)(((int)frame_ds.alpha * (int)alpha) >> 7); /* 237 */

    DispSprD(&frame_ds);                                                /* 238 */
}

/* The plate the clear count is printed into. */
void SaveLoadClearFrameDisp(int off_x, int off_y, u_char alpha, void *pk2_addr,
                            int disp_label)                             /* 251 */
{
    DISP_SPRT frame_ds;

    PK2SendVram((uintptr_t)pk2_addr, -1, -1, 0);                        /* 255 */

    CopySprDToSpr(&frame_ds, &save_load_tex[SAVE_LOAD_TEX_CLEAR_FRAME]); /* 258 */

    frame_ds.x = frame_ds.x + (float)(disp_label * SAVE_LOAD_DATA_PITCH) + (float)off_x; /* 259 */
    frame_ds.y = frame_ds.y + (float)off_y;                             /* 259 */

    frame_ds.alpha = (u_char)(((int)frame_ds.alpha * (int)alpha) >> 7); /* 260 */

    DispSprD(&frame_ds);                                                /* 261 */
}

/* Two pieces, both tinted by the caller's pulse value.  The loop was written
 * out rather than rolled -- the second half is byte-for-byte the first. */
void SaveLoadCursorDisp(int off_x, int off_y, u_char alpha, u_char rgb,
                        void *pk2_addr, int disp_label)                 /* 275 */
{
    DISP_SPRT csr_ds;

    PK2SendVram((uintptr_t)pk2_addr, -1, -1, 0);                        /* 279 */

    CopySprDToSpr(&csr_ds, &save_load_tex[SAVE_LOAD_TEX_CURSOR]);       /* 283 */

    csr_ds.x = csr_ds.x + (float)(disp_label * SAVE_LOAD_DATA_PITCH) + (float)off_x; /* 284 */
    csr_ds.y = csr_ds.y + (float)off_y;                                 /* 284 */

    csr_ds.alpha = (u_char)(((int)csr_ds.alpha * (int)alpha) >> 7);     /* 285 */

    csr_ds.r = rgb;     csr_ds.g = rgb;     csr_ds.b = rgb;             /* 286 */

    DispSprD(&csr_ds);                                                  /* 287 */

    CopySprDToSpr(&csr_ds, &save_load_tex[SAVE_LOAD_TEX_CURSOR + 1]);   /* 290 */

    csr_ds.x = csr_ds.x + (float)(disp_label * SAVE_LOAD_DATA_PITCH) + (float)off_x; /* 291 */
    csr_ds.y = csr_ds.y + (float)off_y;                                 /* 291 */

    csr_ds.alpha = (u_char)(((int)csr_ds.alpha * (int)alpha) >> 7);     /* 292 */

    csr_ds.r = rgb;     csr_ds.g = rgb;     csr_ds.b = rgb;             /* 293 */

    DispSprD(&csr_ds);                                                  /* 294 */
}

/* The glow behind the selected slot.  alphar 0x48 is the additive blend --
 * the only place in this file that leaves the default. */
void SaveLoadSelFlareDisp(int off_x, int off_y, u_char alpha, void *pk2_addr,
                          int disp_label)                               /* 307 */
{
    DISP_SPRT flare_ds;

    PK2SendVram((uintptr_t)pk2_addr, -1, -1, 0);                        /* 311 */

    CopySprDToSpr(&flare_ds, &save_load_tex[SAVE_LOAD_TEX_SEL_FLARE]);  /* 315 */

    flare_ds.x = flare_ds.x + (float)(disp_label * SAVE_LOAD_DATA_PITCH) + (float)off_x; /* 316 */
    flare_ds.y = flare_ds.y + (float)off_y;                             /* 316 */

    flare_ds.alpha  = (u_char)(((int)flare_ds.alpha * (int)alpha) >> 7); /* 317 */
    flare_ds.alphar = 0x48;                                             /* 318 */

    DispSprD(&flare_ds);                                                /* 319 */

    CopySprDToSpr(&flare_ds, &save_load_tex[SAVE_LOAD_TEX_SEL_FLARE + 1]); /* 322 */

    flare_ds.x = flare_ds.x + (float)(disp_label * SAVE_LOAD_DATA_PITCH) + (float)off_x; /* 323 */
    flare_ds.y = flare_ds.y + (float)off_y;                             /* 323 */

    flare_ds.alpha  = (u_char)(((int)flare_ds.alpha * (int)alpha) >> 7); /* 324 */
    flare_ds.alphar = 0x48;                                             /* 325 */

    DispSprD(&flare_ds);                                                /* 326 */
}

/* ==========================================================================
 *  Per-slot rows
 *
 *  All of these are the same six statements with a different table index; the
 *  ROM writes each one out in full rather than sharing a helper.
 * ======================================================================== */

void SaveLoadSnapShadowDisp(int off_x, int off_y, u_char alpha, void *pk2_addr,
                            int disp_label)                             /* 339 */
{
    DISP_SPRT shadow_ds;

    PK2SendVram((uintptr_t)pk2_addr, -1, -1, 0);                        /* 343 */

    CopySprDToSpr(&shadow_ds, &save_load_tex[SAVE_LOAD_TEX_SNAP_SHADOW]); /* 346 */

    shadow_ds.x = shadow_ds.x + (float)(disp_label * SAVE_LOAD_DATA_PITCH) + (float)off_x; /* 347 */
    shadow_ds.y = shadow_ds.y + (float)off_y;                           /* 347 */

    shadow_ds.alpha = (u_char)(((int)shadow_ds.alpha * (int)alpha) >> 7); /* 348 */

    DispSprD(&shadow_ds);                                               /* 349 */
}

/* Note that pk2_addr here is the *slot's own* snapshot pak, not the screen's
 * -- the record supplies only the geometry. */
void SaveLoadSnapShotDisp(int off_x, int off_y, u_char alpha, void *pk2_addr,
                          int disp_label)                               /* 362 */
{
    DISP_SPRT snap_ds;

    PK2SendVram((uintptr_t)pk2_addr, -1, -1, 0);                        /* 366 */

    CopySprDToSpr(&snap_ds, &save_load_tex[SAVE_LOAD_TEX_SNAP_SHOT]);   /* 370 */

    snap_ds.x = snap_ds.x + (float)(disp_label * SAVE_LOAD_DATA_PITCH) + (float)off_x; /* 371 */
    snap_ds.y = snap_ds.y + (float)off_y;                               /* 371 */

    snap_ds.alpha = (u_char)(((int)snap_ds.alpha * (int)alpha) >> 7);   /* 372 */

    DispSprD(&snap_ds);                                                 /* 373 */
}

void SaveLoadNonSelNoDisp(int off_x, int off_y, u_char alpha, void *pk2_addr,
                          int disp_label)                               /* 386 */
{
    DISP_SPRT no_ds;

    PK2SendVram((uintptr_t)pk2_addr, -1, -1, 0);                        /* 390 */

    CopySprDToSpr(&no_ds, &save_load_tex[SAVE_LOAD_TEX_NON_SEL_NO]);    /* 393 */

    no_ds.x = no_ds.x + (float)(disp_label * SAVE_LOAD_DATA_PITCH) + (float)off_x; /* 394 */
    no_ds.y = no_ds.y + (float)off_y;                                   /* 394 */

    no_ds.alpha = (u_char)(((int)no_ds.alpha * (int)alpha) >> 7);       /* 395 */

    DispSprD(&no_ds);                                                   /* 396 */
}

/* The digit plates carry their own x, one record per slot, so this is the one
 * row that does not step by SAVE_LOAD_DATA_PITCH. */
void SaveLoadNonSelDataNumDisp(int off_x, int off_y, u_char alpha, void *pk2_addr,
                               int disp_label)                          /* 409 */
{
    DISP_SPRT num_ds;

    PK2SendVram((uintptr_t)pk2_addr, -1, -1, 0);                        /* 413 */

    CopySprDToSpr(&num_ds,
                  &save_load_tex[SAVE_LOAD_TEX_NON_SEL_DATA_NUM + disp_label]); /* 416 */

    num_ds.x = num_ds.x + (float)off_x;                                 /* 417 */
    num_ds.y = num_ds.y + (float)off_y;                                 /* 417 */

    num_ds.alpha = (u_char)(((int)num_ds.alpha * (int)alpha) >> 7);     /* 418 */

    DispSprD(&num_ds);                                                  /* 419 */
}

void SaveLoadNonSelLineDisp(int off_x, int off_y, u_char alpha, void *pk2_addr,
                            int disp_label)                             /* 432 */
{
    DISP_SPRT line_ds;

    PK2SendVram((uintptr_t)pk2_addr, -1, -1, 0);                        /* 436 */

    CopySprDToSpr(&line_ds, &save_load_tex[SAVE_LOAD_TEX_NON_SEL_LINE]); /* 439 */

    line_ds.x = line_ds.x + (float)(disp_label * SAVE_LOAD_DATA_PITCH) + (float)off_x; /* 440 */
    line_ds.y = line_ds.y + (float)off_y;                               /* 440 */

    line_ds.alpha = (u_char)(((int)line_ds.alpha * (int)alpha) >> 7);   /* 441 */

    DispSprD(&line_ds);                                                 /* 442 */
}

void SaveLoadSelNoDisp(int off_x, int off_y, u_char alpha, void *pk2_addr,
                       int disp_label)                                  /* 455 */
{
    DISP_SPRT no_ds;

    PK2SendVram((uintptr_t)pk2_addr, -1, -1, 0);                        /* 459 */

    CopySprDToSpr(&no_ds, &save_load_tex[SAVE_LOAD_TEX_SEL_NO]);        /* 462 */

    no_ds.x = no_ds.x + (float)(disp_label * SAVE_LOAD_DATA_PITCH) + (float)off_x; /* 463 */
    no_ds.y = no_ds.y + (float)off_y;                                   /* 463 */

    no_ds.alpha = (u_char)(((int)no_ds.alpha * (int)alpha) >> 7);       /* 464 */

    DispSprD(&no_ds);                                                   /* 465 */
}

void SaveLoadSelDataNumDisp(int off_x, int off_y, u_char alpha, void *pk2_addr,
                            int disp_label)                             /* 478 */
{
    DISP_SPRT num_ds;

    PK2SendVram((uintptr_t)pk2_addr, -1, -1, 0);                        /* 482 */

    CopySprDToSpr(&num_ds,
                  &save_load_tex[SAVE_LOAD_TEX_SEL_DATA_NUM + disp_label]); /* 485 */

    num_ds.x = num_ds.x + (float)off_x;                                 /* 486 */
    num_ds.y = num_ds.y + (float)off_y;                                 /* 486 */

    num_ds.alpha = (u_char)(((int)num_ds.alpha * (int)alpha) >> 7);     /* 487 */

    DispSprD(&num_ds);                                                  /* 488 */
}

void SaveLoadSelLineDisp(int off_x, int off_y, u_char alpha, void *pk2_addr,
                         int disp_label)                                /* 501 */
{
    DISP_SPRT line_ds;

    PK2SendVram((uintptr_t)pk2_addr, -1, -1, 0);                        /* 505 */

    CopySprDToSpr(&line_ds, &save_load_tex[SAVE_LOAD_TEX_SEL_LINE]);    /* 508 */

    line_ds.x = line_ds.x + (float)(disp_label * SAVE_LOAD_DATA_PITCH) + (float)off_x; /* 509 */
    line_ds.y = line_ds.y + (float)off_y;                               /* 509 */

    line_ds.alpha = (u_char)(((int)line_ds.alpha * (int)alpha) >> 7);   /* 510 */

    DispSprD(&line_ds);                                                 /* 511 */
}

/* Four quadrants of one image, flips 0/2/1/3 -- the halo around a slot that
 * has been cleared at least once. */
void SaveLoadClearFlareDisp(int off_x, int off_y, u_char alpha, void *pk2_addr,
                            int disp_label)                             /* 524 */
{
    DISP_SPRT flare_ds;
    int       i;

    PK2SendVram((uintptr_t)pk2_addr, -1, -1, 0);                        /* 529 */

    for (i = 0; i < SAVE_LOAD_TEX_CLEAR_FLARE_NUM; i++) {               /* 532 */
        CopySprDToSpr(&flare_ds, &save_load_tex[SAVE_LOAD_TEX_CLEAR_FLARE + i]); /* 533 */

        flare_ds.x = flare_ds.x + (float)(disp_label * SAVE_LOAD_DATA_PITCH) + (float)off_x; /* 534 */
        flare_ds.y = flare_ds.y + (float)off_y;                         /* 534 */

        flare_ds.alpha = (u_char)(((int)flare_ds.alpha * (int)alpha) >> 7); /* 535 */

        DispSprD(&flare_ds);                                            /* 536 */
    }                                                                   /* 537 */
}

/* Drawn over the clear frame when there is no clear count to show. */
void SaveLoadNonClearMaskDisp(int off_x, int off_y, u_char alpha, void *pk2_addr,
                              int disp_label)                           /* 550 */
{
    DISP_SPRT mask_ds;

    PK2SendVram((uintptr_t)pk2_addr, -1, -1, 0);                        /* 554 */

    CopySprDToSpr(&mask_ds, &save_load_tex[SAVE_LOAD_TEX_NON_CLEAR_MASK]); /* 557 */

    mask_ds.x = mask_ds.x + (float)(disp_label * SAVE_LOAD_DATA_PITCH) + (float)off_x; /* 558 */
    mask_ds.y = mask_ds.y + (float)off_y;                               /* 558 */

    mask_ds.alpha = (u_char)(((int)mask_ds.alpha * (int)alpha) >> 7);   /* 559 */

    DispSprD(&mask_ds);                                                 /* 560 */
}

/* "MEMORY CARD (8MB)" plus the glyph for the port being used.  Both callers
 * pass 0, so the port-2 glyph at index 54 is never reached in this build. */
void SaveLoadMemoryCardSlotDisp(int off_x, int off_y, u_char alpha, void *pk2_addr,
                                int slot_label)                         /* 573 */
{
    DISP_SPRT slot_ds;

    PK2SendVram((uintptr_t)pk2_addr, -1, -1, 0);                        /* 577 */

    CopySprDToSpr(&slot_ds, &save_load_tex[SAVE_LOAD_TEX_MC_CAPTION]);  /* 580 */

    slot_ds.x = slot_ds.x + (float)off_x;                               /* 581 */
    slot_ds.y = slot_ds.y + (float)off_y;                               /* 581 */

    slot_ds.alpha = (u_char)(((int)slot_ds.alpha * (int)alpha) >> 7);   /* 582 */

    DispSprD(&slot_ds);                                                 /* 583 */

    CopySprDToSpr(&slot_ds, &save_load_tex[SAVE_LOAD_TEX_MC_CAPTION + 1]); /* 584 */

    slot_ds.x = slot_ds.x + (float)off_x;                               /* 585 */
    slot_ds.y = slot_ds.y + (float)off_y;                               /* 585 */

    slot_ds.alpha = (u_char)(((int)slot_ds.alpha * (int)alpha) >> 7);   /* 586 */

    DispSprD(&slot_ds);                                                 /* 587 */

    CopySprDToSpr(&slot_ds, &save_load_tex[SAVE_LOAD_TEX_MC_SLOT + slot_label]); /* 590 */

    slot_ds.x = slot_ds.x + (float)off_x;                               /* 591 */
    slot_ds.y = slot_ds.y + (float)off_y;                               /* 591 */

    slot_ds.alpha = (u_char)(((int)slot_ds.alpha * (int)alpha) >> 7);   /* 592 */

    DispSprD(&slot_ds);                                                 /* 593 */
}

/* Both offsets are dead here -- the shared caption group places itself. */
void SaveLoadCaptionDisp(int off_x, int off_y, u_char alpha)            /* 604 */
{
    DrawCmnCapGroup_W(0, 0, alpha, 0);                                  /* 607 */
}

/* ==========================================================================
 *  Slot summary lines
 * ======================================================================== */

/* Chapter, room and elapsed time for a game in progress.  The first two are
 * drawn at fixed screen coordinates and ignore off_x / off_y; only the time
 * readout honours them.  Both bounds tests are unsigned, so a negative
 * chapter or room simply draws nothing. */
void SaveLoadMcPlayDataInfoDisp(int off_x, int off_y, u_char alpha, int chapter,
                                int room, TIME_INFO play_time)          /* 635 */
{
    if ((u_int)chapter < 11) {                                          /* 638 */
        PrintMsg(3, chapter, 110, 240, 13, (int)alpha, 0);              /* 640 */
    }

    if ((u_int)room < 240) {                                            /* 643 */
        PrintMsg(0x51, GetMapLabelToRoomLabel(room), 110, 267, 14,
                 (int)alpha, 0);                                        /* 645 */
    }

    PrintNumber_N(play_time.hour, 3, off_x + 410, off_y + 303, 15, alpha, 0, 1, 1); /* 654 */
    PrintMsg(8, 0, off_x + 452, off_y + 303, 15, (int)alpha, 0xa0);     /* 657 */
    PrintNumber_N(play_time.min, 2, off_x + 467, off_y + 303, 15, alpha, 0, 1, 1);  /* 660 */
    PrintMsg(8, 0, off_x + 499, off_y + 303, 15, (int)alpha, 0xa0);     /* 663 */
    PrintNumber_N(play_time.sec, 2, off_x + 514, off_y + 303, 15, alpha, 0, 1, 1);  /* 666 */
}

/* The same readout for a finished game: message 0x32 ("CLEAR") replaces the
 * chapter and room lines, and the time stays. */
void SaveLoadMcClearPlayDataInfoDisp(int off_x, int off_y, u_char alpha,
                                     TIME_INFO play_time)               /* 678 */
{
    PrintMsg(0x50, 0x32, 110, 240, 13, (int)alpha, 0);                  /* 682 */

    PrintNumber_N(play_time.hour, 3, off_x + 410, off_y + 303, 15, alpha, 0, 1, 1); /* 687 */
    PrintMsg(8, 0, off_x + 452, off_y + 303, 15, (int)alpha, 0xa0);     /* 690 */
    PrintNumber_N(play_time.min, 2, off_x + 467, off_y + 303, 15, alpha, 0, 1, 1);  /* 693 */
    PrintMsg(8, 0, off_x + 499, off_y + 303, 15, (int)alpha, 0xa0);     /* 696 */
    PrintNumber_N(play_time.sec, 2, off_x + 514, off_y + 303, 15, alpha, 0, 1, 1);  /* 699 */
}

/* ==========================================================================
 *  Message windows
 * ======================================================================== */

/* Half-black over the whole screen, then the card message window on top. */
void SaveLoadMcStateMsgWinDisp(int off_x, int off_y, u_char alpha)      /* 710 */
{
    DISP_SQAR dsq;

    /* A local initialiser again -- .rodata 3c4aa0, copied into the frame. */
    SQAR_DAT win_bg = { 640, 448, 0, 0, 0, 0, 0, 0, 64 };               /* 712 */

    CopySqrDToSqr(&dsq, &win_bg);                                       /* 718 */

    dsq.alpha = (u_char)(((int)dsq.alpha * (int)alpha) >> 7);           /* 719 */

    DispSqrD(&dsq);                                                     /* 720 */

    DrawCmnWindow(0, (float)(off_x + 45), (float)(off_y + 126),
                  550.0f, 216.0f, alpha, 0x59);                         /* 724 */
}

void SaveLoadMcSelYesNoWinDisp(int off_x, int off_y, u_char alpha, int csr) /* 736 */
{
    SaveLoadMcStateMsgWinDisp(off_x, off_y, alpha);                     /* 740 */

    DrawCmnYesNoSel(csr, (float)(off_y + 295), alpha, 0);               /* 742 */
}

/* The slot-list prompt: no dimming quad, so the slots stay readable behind it. */
void SaveLoadFileSelMsgWinDisp(int off_x, int off_y, u_char alpha)      /* 753 */
{
    DrawCmnTwoLineWindow(0, (float)(off_x + 30), (float)(off_y + 340),
                         580.0f, 98.0f, alpha, 0x59);                   /* 758 */
}

void SaveLoadFileSelYesNoWinDisp(int off_x, int off_y, u_char alpha, int csr) /* 770 */
{
    SaveLoadFileSelMsgWinDisp(off_x, off_y, alpha);                     /* 774 */

    DrawCmnYesNoSel(csr, (float)(off_y + 388), alpha, 0);               /* 776 */
}

/* Both message routines ignore their offsets. */
void SaveLoadMcStateMsgDisp(int off_x, int off_y, u_char alpha, int msg_id) /* 788 */
{
    PrintMsg(0x50, msg_id, 92, 142, 1, (int)alpha, 0);                  /* 792 */
}

void SaveLoadFileSelMsgDisp(int off_x, int off_y, u_char alpha, int msg_id) /* 803 */
{
    PrintMsg(0x50, msg_id, 65, 356, 1, (int)alpha, 0);                  /* 807 */
}

/* ==========================================================================
 *  Clear count
 * ======================================================================== */

/* Two digits, most significant first.  set_flg is the leading-zero gate: it
 * starts raised when zero_flg says pad, and is raised on the first non-zero
 * digit otherwise, so 7 draws as " 7" and 07 with zero_flg.  The extra test
 * at 862 is what stops a count of exactly zero printing nothing at all.
 *
 * off_x and off_y are dead -- the ROM never reads either.
 *
 * Two notes on the shape.  The running x lives in a callee-saved register
 * with no stab of its own, which normally means a CSE temp; here it is an
 * accumulator across iterations and so cannot be one, and the two lines it is
 * built from (833 for the column, 879 for the per-digit step) place it
 * exactly.  And ten_tmp really is reset twice: line 840's store feeds the
 * first iteration and the one at the bottom of the body feeds the rest --
 * which is why GCC has a `li 1` in the preheader tagged 840 and another in
 * the back edge's delay slot.  A reset written at the *top* of the body would
 * have carried its own line into the preheader instead. */
void SaveLoadClearNumberDisp(int data, int off_x, int off_y, u_char alpha, int pri,
                             u_char zero_flg, int disp_label, void *pk2_addr) /* 823 */
{
    int    i;
    int    j;
    int    tmp;
    int    ten_tmp;
    u_char set_flg;
    int    x;

    x = disp_label * SAVE_LOAD_DATA_PITCH + SAVE_LOAD_CLEAR_NUM_X;      /* 833 */

    ten_tmp = 1;                                                        /* 840 */

    set_flg = (u_char)(zero_flg == 1);                                  /* 842 */

    if (data > 99) {                                                    /* 847 */
        data = 99;
    }

    for (i = SAVE_LOAD_CLEAR_NUM_DIGIT; i > 0; i--) {                   /* 852 */
        for (j = i - 1; j > 0; j--) {                                   /* 853 */
            ten_tmp = ten_tmp * 10;                                     /* 854 */
        }                                                               /* 855 */

        if (data / ten_tmp != 0) {                                      /* 856 */
            set_flg = 1;                                                /* 858 */
        }

        if ((zero_flg == 0) && (data == 0) && (i == 1)) {               /* 862 */
            set_flg = 1;
        }

        if (i == 1) {                                                   /* 867 */
            tmp = data % 10;                                            /* 868 */
        }
        else {
            tmp = (data / ten_tmp) % 10;                                /* 871 */
        }

        if (set_flg == 1) {                                             /* 874 */
            SaveLoadClearNumberDisp_One(tmp, x, SAVE_LOAD_CLEAR_NUM_Y,
                                        alpha, pri, pk2_addr);          /* 876 */
        }

        x = x + SAVE_LOAD_CLEAR_NUM_PITCH;                              /* 879 */

        ten_tmp = 1;
    }                                                                   /* 881 */
}

/* One 12x14 digit at an absolute position -- the only routine here that sets
 * x and y outright rather than offsetting the record's own.  The z it writes
 * is the usual pri-derived depth, so the digits sort with the frame.
 *
 * The assert names SaveLoadNumberDisp_One, which is not this function; the
 * literal is hardcoded rather than taken from __FUNCTION__, so the banner
 * above it says SaveLoadClearNumberDisp_One and the message does not. */
void SaveLoadClearNumberDisp_One(int data, int x, int y, u_char alpha, int pri,
                                 void *pk2_addr)                        /* 896 */
{
    DISP_SPRT num_ds;

    if (data > 9) {                                                     /* 900 */
        PRINT_ASSERT("Error!! SaveLoadNumberDisp_One");                 /* 901 */
    }

    PK2SendVram((uintptr_t)pk2_addr, -1, -1, 0);                        /* 904 */

    CopySprDToSpr(&num_ds, &save_load_tex[SAVE_LOAD_TEX_NUMBER + data]); /* 907 */

    num_ds.x = (float)x;                                                /* 908 */
    num_ds.y = (float)y;                                                /* 908 */

    num_ds.alpha = alpha;                                               /* 909 */

    num_ds.z = 0xfffff - (pri & 0xfffff);   num_ds.pri = pri;           /* 910 */

    DispSprD(&num_ds);                                                  /* 911 */
}
