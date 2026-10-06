// FILE: /home/zero_rom/zero2np/src/album/prg/album.c
//
// The photo album's parent module.  All 23 ZERO2.MAP exports plus the five
// statics (AlbumCtrlInit, AlbumInfoInit, AlbumTexLoadWait, AlbumModeMain,
// AlbumDispCtrlInit) are reconstructed.  It owns three things and nothing
// else: the two resident albums, the memory the pages draw out of, and a
// two-level step machine that hands every frame to one of the two pages.
//
// The machine, top down:
//
//   * album_ctrl.step is the module's own state -- 0 seed the fade,
//     1 wait on the three shared paks, 2 running, 3 closing.  AlbumMain()
//     returns non-zero only in step 3 and only once the closing fade has
//     reached ZERO2_ANIM2D_STEP_END, which is what every caller tests to
//     leave its phase.
//   * album_ctrl.mode picks the page: 0 the two-album edit view
//     (album_edit.o), 1 the photo viewer (album_view.o).  The three
//     album_mode_*_func[] tables are indexed by it, and AlbumModeMain() runs
//     each page as its own init / main / end triple.  A page's main returning
//     non-zero *toggles* the mode -- the two pages hand back and forth, and
//     nothing else ever writes album_ctrl.mode.
//   * album_ctrl.album_photo_addr[] is the two albums' picture pages.  Album A
//     is the ingame photo area at PHOTO_DATA_ADDR, album B the 0xe8000 block
//     claimed out of the caller's heap.  That is why an album opened from a
//     save point can copy the camera's photos without loading anything.
//
// The memory layer:
//
//   * AlbumMemGet / AlbumMemFree are the outgame heap's get/free, handed over
//     once by AlbumBackGroundLoadReq() and used by every album allocation.
//     Handing them over twice is an assert, which is why the pair is tested
//     rather than just overwritten.  AlbumEnd() gives them back.
//   * GetAlbumTexMem() sizes its block from the pak's own CD file size;
//     GetAlbumDataMem() takes an explicit size.  Both free whatever the slot
//     held first, so a re-request is safe.
//   * The five paks the album keeps resident are claimed and requested in two
//     passes -- every GetAlbumTexMem() first, then every FileLoadReqEE() --
//     so the allocations are contiguous before any load starts.  Only three
//     of the five are waited on; the save/load and slot-select paks are not
//     needed until a page asks for them.
//
// Reconstructed from the Feb 6 2004 prototype (SLES_523.84).
//
// Trailing /* NNN */ comments are the original source line numbers, measured
// from symbols.txt's $LM/SOL records rather than guessed.  Statements whose
// only memory access goes through fixed_array<>::operator[] carry no $LM of
// their own (the subscript's own header line swallows them), so the two
// AlbumInfoInit() loop bodies and both SetSave_* addresses are interpolated
// into their measured gaps.
//
// The two `return res;` lines (350 and 397) are the single note on each int
// function's shared epilogue.  album_view.o's AlbumViewTexLoadWait carries two
// there -- the return's own line and the closing brace's -- so one of these two
// numbers may be the brace rather than the return; the code at the address is
// the return either way, and there is no second note to tell them apart.

#include "album.h"

#include "album_disp.h"                             // AlbumInOutAnimCtrl / AlbumBlackBgDisp
#include "album_edit.h"                             // AlbumEditCtrlInit / Main / DispMain
#include "album_view.h"                             // AlbumViewCtrlInit / Main / DispMain
#include "../../common/utility2.h"                  // PRINT_ASSERT
#include "../../graphics/graph2d/tim2.h"            // PK2SendVram
#include "../../graphics/graph3d/ctl/fixed_array.h"
#include "../../ingame/menu/zero2_anim2d.h"         // ZERO2_ANIM2D_STEP_*
#include "../../ingame/photo/photo.h"               // CopyPFileWrk
#include "../../system/eeiop/cddat.h"               // *_PK2 file numbers, GetFileSize
#include "../../system/eeiop/fileload.h"            // FileLoadReqEE / FileLoadIsEnd2 / FileLoadCancel2
#include "../../system/os/system.h"                 // GetLanguage / PHOTO_DATA_ADDR

#include <string.h>                                 // memset

/* ALBUM_CTRL::step. */
#define ALBUM_STEP_INIT             0   /* seed the opening fade           */
#define ALBUM_STEP_LOAD_WAIT        1   /* waiting on the three shared paks*/
#define ALBUM_STEP_MAIN             2   /* running a page                  */
#define ALBUM_STEP_OUT              3   /* closing fade, then hand back    */

/* ALBUM_CTRL::mode -- which page owns the frame, and the width of the three
 * dispatch tables. */
#define ALBUM_MODE_EDIT             0
#define ALBUM_MODE_VIEW             1
#define ALBUM_MODE_MAX              2

/* ALBUM_CTRL::mode_step, the per-page init / main / end triple. */
#define ALBUM_MODE_STEP_INIT        0
#define ALBUM_MODE_STEP_MAIN        1
#define ALBUM_MODE_STEP_END         2

/* AlbumInit()'s argument: 0 is the ingame album (album A is the camera's own
 * photo file, copied in), 1 the outgame one reached from the title. */
#define ALBUM_INIT_MODE_INGAME      0
#define ALBUM_INIT_MODE_OUTGAME     1
#define ALBUM_INIT_MODE_MAX         2

/* ALBUM_INFO::album_type for the two albums that never reach a memory card:
 * 5 is the camera's own file (the only one whose photos can be protected),
 * 6 an empty card album. */
#define ALBUM_TYPE_PHOTO_FILE       5
#define ALBUM_TYPE_EMPTY            6

/* How dark AlbumDispMain() lays the backdrop under whichever page is up. */
#define ALBUM_BG_MAX_ALPHA          30

/* ---- state -------------------------------------------------------------- */

fixed_array<ALBUM_INFO, 2> album_info;              /* data 2d4370 */

/* sdata 3ef2a8 / 3ef2ac.  The outgame heap, borrowed for as long as the album
 * is open.  Both are NULL until AlbumBackGroundLoadReq() hands them over. */
static void *(*AlbumMemGet)(int);
static void  (*AlbumMemFree)(void *);

/* sdata 3ef2b0..3ef2c4.  The five paks the album keeps resident plus the
 * picture buffer that backs album_photo_addr[ALBUM_DATA_B]. */
static void *outgame_tex_addr;                      /* sdata 3ef2b0 */
static void *album_cmn_tex_addr;                    /* sdata 3ef2b4 */
static void *album_hensyu_tex_addr;                 /* sdata 3ef2b8 */
static void *album_sl_addr;                         /* sdata 3ef2bc */
static void *album_slot_sl_addr;                    /* sdata 3ef2c0 */
static void *album_buff_addr;                       /* sdata 3ef2c4 */

/* sdata 3ef2c8.  Where the picture pages of the album being edited live. */
static void *album_save_data_addr;

/* sdata 3ef2cc.  Whether the parent page draws the "ALBUM" title plate.  It
 * initialises to 1 in the ROM's .sdata image, not 0 -- AlbumInit() writes the
 * same 1 back, but a caller that reaches AlbumDispMain() before AlbumInit()
 * would otherwise lose the plate.  See [[zeroed-statics-lose-rom-initialisers]]. */
static char album_title_disp_flg = 1;

/* sdata 3ef2d0 / 3ef2d8 / 3ef2e0.  The two pages, as three parallel tables
 * indexed by ALBUM_CTRL::mode.  No slot is NULL in this build; AlbumModeMain()
 * tests anyway. */
static void (*album_mode_init_func[ALBUM_MODE_MAX])(void) =
{
    AlbumEditCtrlInit, AlbumViewCtrlInit,
};

static int (*album_mode_main_func[ALBUM_MODE_MAX])(void) =
{
    AlbumEditMain, AlbumViewMain,
};

static void (*album_mode_disp_func[ALBUM_MODE_MAX])(void) =
{
    AlbumEditDispMain, AlbumViewDispMain,
};

/* rdata 3a0198 / sbss 3f4aa8.  A second, unused copy of album_view.c's
 * per-album-type background table.  It is genuinely dead here -- nothing in
 * album.o reads it, and the only thing that touches it is the file's own
 * static constructor -- but the ROM emits both the .rodata image and the
 * reference_fixed_array, and album.o's ctor runs it *after* album_info's,
 * which is what proves it is a file static of album.c rather than something
 * arriving from a shared header.  Kept so the object accounts.
 *
 * The host compiler is smarter than GCC 2.96 here and drops both, so
 * objdiff.py reports a DIFF for it and `nm` finds no symbol -- that is the
 * elimination, not a bad transcription.  album_view.c's identical copy is
 * live and matches the ROM's bytes verbatim. */
static int album_view_tex[ALBUM_TYPE_MAX] =         /* rdata 3a0198 */
{
    ALBM_KKD_PAT1_PK2, ALBM_KKD_PAT2_PK2, ALBM_KKD_PAT3_PK2,
    ALBM_KKD_PAT4_PK2, ALBM_KKD_PAT5_PK2,
    ALBM_KKD_PAT0_PK2, ALBM_KKD_PAT0_PK2,
};

static reference_fixed_array<int, ALBUM_TYPE_MAX> album_view_tex_tbl(album_view_tex);

/* bss 4220e0.  A file static of album.c, so the type belongs here rather than
 * in the header. */
typedef struct                                      /* 0x10 */
{
    /* 0x0 */ char  step;
    /* 0x1 */ char  mode_step;
    /* 0x2 */ char  mode;
    /* 0x3 */ char  album_init_mode;
    /* 0x4 */ char  current_album;
    /* 0x5 */ char  photo_no;
    /* 0x8 */ void *album_photo_addr[ALBUM_DATA_MAX];
} ALBUM_CTRL;

static ALBUM_CTRL album_ctrl;

/* sbss 3f4ab0.  The parent page's own fade pair. */
typedef struct                                      /* 0x2 */
{
    /* 0x0 */ char anim_step;
    /* 0x1 */ char anim_timer;
} ALBUM_DISP_CTRL;

static ALBUM_DISP_CTRL album_disp_ctrl;

/* ---- file-local helpers ------------------------------------------------- */

static void AlbumCtrlInit(int init_mode);                       /* 0x11c398 */
static void AlbumInfoInit(int init_mode);                       /* 0x11c470 */
static void GetAlbumDataMem(void **tex_addr, int size);         /* 0x11c778 */
static int  AlbumTexLoadWait(void);                             /* 0x11c7c8 */
static void AlbumModeMain(void);                                /* 0x11c8f0 */
static void AlbumDispCtrlInit(void);                            /* 0x11cbe0 */

/* ==========================================================================
 *  Setup
 * ======================================================================== */

/* Open the album.  init_mode 0 is the ingame album, 1 the outgame one; the
 * heap has to have been handed over by AlbumBackGroundLoadReq() first, which
 * is what the album_buff_addr assert inside AlbumCtrlInit() checks. */
void AlbumInit(int init_mode)                                           /* 176 */
{
    if ((u_int)init_mode >= ALBUM_INIT_MODE_MAX) {                      /* 178 */
        PRINT_ASSERT("Error! %s", __FUNCTION__);                        /* 179 */
    }

    AlbumCtrlInit(init_mode);                                           /* 184 */

    AlbumInfoInit(init_mode);                                           /* 187 */

    album_title_disp_flg = 1;                                           /* 191 */
}

static void AlbumCtrlInit(int init_mode)                                /* 199 */
{
    if ((u_int)init_mode >= ALBUM_INIT_MODE_MAX) {                      /* 201 */
        PRINT_ASSERT("Error! %s", __FUNCTION__);                        /* 202 */
    }

    if (album_buff_addr == NULL) {                                      /* 204 */
        PRINT_ASSERT("Error! %s album_buff_addr NULL", __FUNCTION__);   /* 205 */
    }

    album_ctrl.step            = ALBUM_STEP_INIT;                       /* 209 */
    album_ctrl.mode_step       = ALBUM_MODE_STEP_INIT;                  /* 210 */
    album_ctrl.mode            = ALBUM_MODE_EDIT;                       /* 211 */
    album_ctrl.album_init_mode = (char)init_mode;                       /* 212 */
    album_ctrl.current_album   = ALBUM_DATA_A;                          /* 213 */
    album_ctrl.photo_no        = 0;                                     /* 214 */

    /* Album A is the ingame photo area, album B the block claimed out of the
     * caller's heap.  PORT: the first is a raw EE address and the second a
     * host pointer, which is legal only because every consumer of
     * GetAlbumDataAddr() runs it through MioPan_GetHostPointer() -- that is
     * idempotent by range check, so a host pointer passes straight through.
     * The photo_make.c entry points they reach take uintptr_t for the same
     * reason; an `int` there truncates album B. */
    album_ctrl.album_photo_addr[ALBUM_DATA_A] = (void *)PHOTO_DATA_ADDR; /* 215 */
    album_ctrl.album_photo_addr[ALBUM_DATA_B] = album_buff_addr;        /* 216 */
}

/* Wipe both albums and seed every picture slot with its own page number --
 * PICTURE_WRK::adr_no travels with the record through every sort, so slot i
 * starting on page i is what makes the pages permanently assigned.
 *
 * The ingame album then takes a copy of the camera's own photo file; the
 * outgame one starts empty on both sides. */
static void AlbumInfoInit(int init_mode)                                /* 225 */
{
    int i;
    int j;

    for (i = 0; i < ALBUM_DATA_MAX; i++) {                              /* 229 */
        memset(&album_info[i], 0, sizeof(ALBUM_INFO));                  /* 230 */
        for (j = 0; j < PHOTO_FILE_MAX; j++) {                          /* 231 */
            album_info[i].album_info.pic[j].adr_no = (u_char)j;         /* 232 */
        }                                                              /* 233 */
    }                                                                  /* 234 */

    if (init_mode == ALBUM_INIT_MODE_INGAME) {                          /* 238 */
        album_info[ALBUM_DATA_A].album_type = ALBUM_TYPE_PHOTO_FILE;    /* 239 */
        CopyPFileWrk(&album_info[ALBUM_DATA_A].album_info);             /* 240 */
        album_info[ALBUM_DATA_B].album_type = ALBUM_TYPE_EMPTY;         /* 241 */
    } else {
        album_info[ALBUM_DATA_A].album_type = ALBUM_TYPE_EMPTY;         /* 243 */
        album_info[ALBUM_DATA_B].album_type = ALBUM_TYPE_EMPTY;         /* 244 */
    }
}

/* ==========================================================================
 *  Memory and texture layer
 * ======================================================================== */

/* Claim the album's five paks and its picture buffer out of the caller's heap,
 * then post every load.  mem_get / mem_free may only be handed over once --
 * AlbumEnd() is what gives them back. */
void AlbumBackGroundLoadReq(void *(*mem_get)(int), void (*mem_free)(void *)) /* 262 */
{
    if (AlbumMemGet == NULL && AlbumMemFree == NULL) {                  /* 265 */
        AlbumMemGet  = mem_get;                                         /* 266 */
        AlbumMemFree = mem_free;                                        /* 267 */
    } else {

        PRINT_ASSERT("Error! %s", __FUNCTION__);                        /* 270 */
    }

    GetAlbumDataMem(&album_buff_addr, ALBUM_PHOTO_DATA_SIZE);           /* 275 */

    GetAlbumTexMem(&outgame_tex_addr, OUTGAME_PK2);                     /* 278 */
    GetAlbumTexMem(&album_cmn_tex_addr, ALBM_CMN_PK2 + GetLanguage());  /* 279 */
    GetAlbumTexMem(&album_hensyu_tex_addr, ALBM_HENSYU_PK2 + GetLanguage()); /* 280 */
    GetAlbumTexMem(&album_sl_addr, ALBM_SL_PK2);                        /* 281 */
    GetAlbumTexMem(&album_slot_sl_addr, ALBM_SLOT_SL_PK2 + GetLanguage()); /* 282 */

    FileLoadReqEE(OUTGAME_PK2, outgame_tex_addr, 6, NULL, NULL);        /* 285 */
    FileLoadReqEE(ALBM_CMN_PK2 + GetLanguage(), album_cmn_tex_addr, 6, NULL, NULL); /* 286 */
    FileLoadReqEE(ALBM_HENSYU_PK2 + GetLanguage(), album_hensyu_tex_addr, 6, NULL, NULL); /* 287 */
    FileLoadReqEE(ALBM_SL_PK2, album_sl_addr, 6, NULL, NULL);           /* 288 */
    FileLoadReqEE(ALBM_SLOT_SL_PK2 + GetLanguage(), album_slot_sl_addr, 6, NULL, NULL); /* 289 */
}

/* Claim a block big enough for pak `data_label`, freeing whatever the slot
 * held first.  There is no null check on the result. */
void GetAlbumTexMem(void **tex_addr, int data_label)                    /* 299 */
{
    if (*tex_addr != NULL) {                                            /* 302 */
        LiberateAlbumTexMem(tex_addr);                                  /* 303 */
    }

    *tex_addr = AlbumMemGet((int)GetFileSize(data_label));              /* 307 */
}

/* The same, with the size given rather than looked up -- what the 0xe8000
 * picture buffer goes through. */
static void GetAlbumDataMem(void **tex_addr, int size)                  /* 316 */
{
    if (*tex_addr != NULL) {                                            /* 319 */
        LiberateAlbumTexMem(tex_addr);                                  /* 320 */
    }

    *tex_addr = AlbumMemGet(size);                                      /* 324 */
}

/* Only the three paks every page needs are waited on.  The save/load and
 * slot-select paks are requested alongside them but nothing gates on them --
 * their screens are reached from a menu, several frames later.
 *
 * The whole test is one store-flag expression, not an `if` with a `res = 1`
 * inside it: the last term compiles to `sltu s0,zero,v0` and no line note of
 * its own, which is what a boolean assignment looks like and an `if` does
 * not. */
static int AlbumTexLoadWait(void)                                       /* 333 */
{
    int res;

    res = (FileLoadIsEnd2(ALBM_CMN_PK2 + GetLanguage(), album_cmn_tex_addr) != 0 &&      /* 341 */
           FileLoadIsEnd2(ALBM_HENSYU_PK2 + GetLanguage(), album_hensyu_tex_addr) != 0 && /* 342 */
           FileLoadIsEnd2(OUTGAME_PK2, outgame_tex_addr) != 0);         /* 343 */

    return res;                                                         /* 350 */
}

/* ==========================================================================
 *  The phase machine
 * ======================================================================== */

/* One frame of the album.  Returns non-zero on the frame the closing fade
 * finishes, which is every caller's cue to leave its phase.
 *
 * The four steps are consecutive `if`s rather than a switch, so a step that
 * advances is re-tested in the same frame -- the load wait can start and the
 * first page can be entered without giving up a frame to each. */
int AlbumMain(void)                                                     /* 362 */
{
    int res;

    res = 0;                                                            /* 366 */

    if (album_ctrl.step == ALBUM_STEP_INIT) {                           /* 370 */

        AlbumDispCtrlInit();                                            /* 372 */

        album_ctrl.step = ALBUM_STEP_LOAD_WAIT;                         /* 374 */
    }

    if (album_ctrl.step == ALBUM_STEP_LOAD_WAIT) {                      /* 378 */
        if (AlbumTexLoadWait() != 0) {                                  /* 379 */
            album_ctrl.step = ALBUM_STEP_MAIN;                          /* 380 */
        }
    }

    if (album_ctrl.step == ALBUM_STEP_MAIN) {                           /* 385 */
        AlbumModeMain();                                                /* 386 */
    }

    if (album_ctrl.step == ALBUM_STEP_OUT) {                            /* 390 */
        res = (album_disp_ctrl.anim_step == ZERO2_ANIM2D_STEP_END);     /* 391 */
    }

    return res;                                                         /* 397 */
}

/* One frame of whichever page owns the album.  A page's main returning
 * non-zero does not select a successor -- it flips `mode` between the two, so
 * the edit view and the viewer simply hand back and forth. */
static void AlbumModeMain(void)                                         /* 403 */
{
    if (album_ctrl.mode_step == ALBUM_MODE_STEP_INIT) {                 /* 407 */
        if (album_mode_init_func[album_ctrl.mode] != NULL) {            /* 408 */
            album_mode_init_func[album_ctrl.mode]();                    /* 409 */
        }

        album_ctrl.mode_step = ALBUM_MODE_STEP_MAIN;                    /* 412 */
    }

    if (album_ctrl.mode_step == ALBUM_MODE_STEP_MAIN) {                 /* 416 */
        if (album_mode_main_func[album_ctrl.mode] != NULL) {            /* 417 */
            if (album_mode_main_func[album_ctrl.mode]() != 0) {         /* 418 */
                album_ctrl.mode_step = ALBUM_MODE_STEP_END;             /* 419 */
            }
        }
    }

    if (album_ctrl.mode_step == ALBUM_MODE_STEP_END) {                  /* 425 */
        if (album_ctrl.mode == ALBUM_MODE_EDIT) {                       /* 426 */
            album_ctrl.mode = ALBUM_MODE_VIEW;                          /* 427 */
        } else {
            album_ctrl.mode = ALBUM_MODE_EDIT;                          /* 429 */
        }

        album_ctrl.mode_step = ALBUM_MODE_STEP_INIT;                    /* 433 */
    }
}

/* Ask the album to close.  Same shape as album_view.c's own
 * AlbumViewMoveEditReq(): raise the step, then start the closing fade. */
void AlbumOutReq(void)                                                  /* 442 */
{
    album_ctrl.step = ALBUM_STEP_OUT;                                   /* 445 */

    album_disp_ctrl.anim_step  = ZERO2_ANIM2D_STEP_OUT;                 /* 446 */
    album_disp_ctrl.anim_timer = 0;                                     /* 447 */
}

/* ==========================================================================
 *  Accessors
 * ======================================================================== */

/* Swap album A for album B.  The ROM's own spelling of the name. */
void ChengeCurrentAlbum(void)                                           /* 459 */
{
    album_ctrl.current_album = album_ctrl.current_album ^ 1;            /* 462 */
}

void SetAlbumPhotoNo(char photo_no)                                     /* 471 */
{
    album_ctrl.photo_no = photo_no;                                     /* 474 */
}

void SetAlbumSaveDataAddr(void *data_addr)                              /* 482 */
{
    album_save_data_addr = data_addr;                                   /* 485 */
}

void SetAlbumTitleFlg(char flg)                                         /* 493 */
{
    album_title_disp_flg = flg;                                         /* 496 */
}

int GetCurrentAlbum(void)                                               /* 509 */
{
    return (int)album_ctrl.current_album;                               /* 513 */
}

int GetAlbumPhotoNo(void)                                               /* 520 */
{
    return (int)album_ctrl.photo_no;                                    /* 524 */
}

/* The picture pages of album A (the ingame photo area) or album B (the block
 * claimed out of the caller's heap). */
void *GetAlbumDataAddr(int album_data_label)                            /* 532 */
{
    return album_ctrl.album_photo_addr[album_data_label];               /* 536 */
}

void *GetAlbumOutGameTexAddr(void)                                      /* 543 */
{
    return outgame_tex_addr;                                            /* 547 */
}

void *GetAlbumCmnTexAddr(void)                                          /* 554 */
{
    return album_cmn_tex_addr;                                          /* 558 */
}

void *GetAlbumEditTexAddr(void)                                         /* 565 */
{
    return album_hensyu_tex_addr;                                       /* 569 */
}

void *GetAlbumSaveLoadTexAddr(void)                                     /* 576 */
{
    return album_sl_addr;                                               /* 580 */
}

void *GetAlbumSlotSelTexAddr(void)                                      /* 587 */
{
    return album_slot_sl_addr;                                          /* 591 */
}

/* ==========================================================================
 *  Teardown
 * ======================================================================== */

/* Withdraw every load still in flight, hand every block back, and give up the
 * heap.  The viewer's own per-album-type pak is torn down alongside them --
 * album_view.o has no other owner. */
void AlbumEnd(void)                                                     /* 601 */
{
    AlbumTexLoadCancel(outgame_tex_addr, OUTGAME_PK2);                  /* 604 */
    AlbumTexLoadCancel(album_cmn_tex_addr, ALBM_CMN_PK2 + GetLanguage()); /* 605 */
    AlbumTexLoadCancel(album_hensyu_tex_addr, ALBM_HENSYU_PK2 + GetLanguage()); /* 606 */
    AlbumTexLoadCancel(album_sl_addr, ALBM_SL_PK2);                     /* 607 */
    AlbumTexLoadCancel(album_slot_sl_addr, ALBM_SLOT_SL_PK2 + GetLanguage()); /* 608 */
    AlbumViewTexLoadCancel();                                           /* 609 */

    LiberateAlbumTexMem(&album_buff_addr);                              /* 613 */
    LiberateAlbumTexMem(&outgame_tex_addr);                             /* 614 */
    LiberateAlbumTexMem(&album_cmn_tex_addr);                           /* 615 */
    LiberateAlbumTexMem(&album_hensyu_tex_addr);                        /* 616 */
    LiberateAlbumTexMem(&album_sl_addr);                                /* 617 */
    LiberateAlbumTexMem(&album_slot_sl_addr);                           /* 618 */
    LiberateAlbumViewTex();                                             /* 619 */

    AlbumMemGet  = NULL;                                                /* 622 */
    AlbumMemFree = NULL;                                                /* 623 */
}

/* Give a block back and clear the slot.  A NULL slot is a no-op, which is why
 * every caller can run it unconditionally. */
void LiberateAlbumTexMem(void **tex_addr)                               /* 630 */
{
    if (*tex_addr != NULL) {                                            /* 633 */
        AlbumMemFree(*tex_addr);                                        /* 634 */
        *tex_addr = NULL;                                               /* 635 */
    }
}

/* Withdraw a load that has not landed yet.  Cancelling one that already
 * finished would be an error, hence the FileLoadIsEnd2() guard. */
void AlbumTexLoadCancel(void *tex_addr, int data_label)                 /* 646 */
{
    if (tex_addr != NULL) {                                             /* 649 */

        if (FileLoadIsEnd2(data_label, tex_addr) == 0) {                /* 651 */
            FileLoadCancel2(data_label, tex_addr, NULL, NULL);          /* 652 */
        }
    }
}

/* ==========================================================================
 *  Drawing
 * ======================================================================== */

static void AlbumDispCtrlInit(void)                                     /* 666 */
{
    album_disp_ctrl.anim_step  = ZERO2_ANIM2D_STEP_START;               /* 669 */
    album_disp_ctrl.anim_timer = 0;                                     /* 670 */
}

/* The album's own layer: the backdrop, then whichever page is up, then the
 * title plate over the top.  Nothing is drawn before the paks have landed
 * (step 2) or after the closing fade has finished, and `alpha` is the shared
 * open/close ramp every part is scaled by.
 *
 * The two PK2SendVram() calls are the pak switch the parts either side of
 * them come out of -- the page draws against whatever it set last, so the
 * OUTGAME pak has to be put back before the title frame. */
void AlbumDispMain(void)                                                /* 679 */
{
    u_char alpha;

    alpha = 128;                                                        /* 683 */

    if (album_ctrl.step == ALBUM_STEP_MAIN ||
        album_ctrl.step == ALBUM_STEP_OUT) {                            /* 686 */

        AlbumInOutAnimCtrl(&album_disp_ctrl.anim_step,
                           &album_disp_ctrl.anim_timer, &alpha);        /* 688 */

        if (album_disp_ctrl.anim_step != ZERO2_ANIM2D_STEP_END) {       /* 690 */

            AlbumBlackBgDisp(0, 0, alpha, ALBUM_BG_MAX_ALPHA);          /* 692 */

            if (album_ctrl.mode_step == ALBUM_MODE_STEP_MAIN) {         /* 694 */
                if (album_mode_disp_func[album_ctrl.mode] != NULL) {    /* 695 */
                    album_mode_disp_func[album_ctrl.mode]();            /* 696 */
                }
            }

            PK2SendVram((uintptr_t)outgame_tex_addr, -1, -1, 0);        /* 700 */

            if (album_title_disp_flg != 0) {                            /* 703 */

                AlbumTitleFrameDisp(0, 0, alpha);                       /* 705 */

                PK2SendVram((uintptr_t)album_hensyu_tex_addr, -1, -1, 0); /* 707 */

                AlbumTitleDisp(0, 0, alpha);                            /* 711 */
            }
        }
    }
}

/* ==========================================================================
 *  Save blocks (system/mc/dat/save_data.c's save_album_data[])
 * ======================================================================== */

/* The picture pages of the album being saved.  album_save_data_addr is set by
 * album_save.o, which is not reconstructed yet, so this still hands the save
 * system a NULL block. */
void SetSave_AlbumData(MC_SAVE_DATA *data)                              /* 727 */
{
    data->addr = (u_char *)album_save_data_addr;                        /* 730 */
    data->size = ALBUM_PHOTO_DATA_SIZE;                                 /* 731 */
}

void SetSave_AlbumInfoData(MC_SAVE_DATA *data)                          /* 739 */
{
    data->addr = (u_char *)&album_info[album_ctrl.current_album];       /* 740 */
    data->size = sizeof(ALBUM_INFO);                                    /* 743 */
}
