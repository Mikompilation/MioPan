// FILE: /home/zero_rom/zero2np/src/ingame/movie_room_menu/prg/movie_room_menu.c
//
// movie_room_menu.o -- the reel-selection menu in front of the movie room's
// projectors.  Four exported functions and twenty statics: the strip of film
// reels the player can scroll through, the four confirm windows around it,
// and the hand-off to movie_projecter.o that actually starts a film.
//
// THE SHAPE IS THE FOLDER'S USUAL ONE.  A MOVIE_ROOM_MENU_CTRL work block, a
// `mode` that indexes movie_room_menu_pad_func[6] and
// movie_room_menu_disp_func[6] in step (the part `now_place` plays in every
// outgame screen), and a Get/Liberate/LoadReq/LoadWait texture quartet.  The
// outer `step` is the 0 load / 1 run / 2 leave sequence MovieRoomMenuMain()
// walks; ingame.c leaves the sub-phase when Main() answers 0.
//
// SEVEN REELS, COMPACTED.  ITM_REEL1..ITM_REEL7 are inventory items 34..40 and
// the films they play are MOVIE_ROOM_000_PSS..006, two CD files apart because
// every film has a PAL twin one file up -- which is the whole of
// GetItemNameFromPlayFilmNo()/GetPlayFilmNoFromItemNo().  SetDispFilmData()
// walks all seven, keeps the ones the player actually carries, and packs them
// into the first `have_num` slots of disp_film_reel[].  So `cursor` indexes
// the compacted strip, never the item id.
//
// WHICH MENU THE PLAYER GETS depends on two things, both decided by
// MovieRoomMenuInit():  a projector that already has a reel in it opens
// straight on MODE_FILM_SET (take it out / swap it / leave), an empty one with
// reels in the inventory opens on MODE_START_MSG, and an empty one with no
// reels opens on MODE_NO_HAVE_FILM with step already 1.  MODE_FILM_SET itself
// then has three rows or two depending on have_num, which is why
// MovieRoomMenuFilmSetPad() and MovieRoomMenuFilmSetDisp() are each two whole
// copies of the same logic.
//
// TWO FIELDS ARE DEAD IN THIS BUILD.  A gp-relative scan over the object's
// whole .text finds no access at all to MOVIE_ROOM_MENU_CTRL::play_flg or to
// MOVIE_ROOM_MENU_DISP::anim_timer, and MOVIE_ROOM_MENU_DISP::anim_step is
// written only by MovieRoomMenuDispInit() (to 0) and read only by
// MovieRoomMenuDisp() (`!= 4`) -- so that test is always true and the window
// fade every sibling screen has never runs here.  Kept as found.
//
// A NOTE ON THE /* NNN */ ANNOTATIONS.  Function opening lines are the $LM
// that precedes each PROC record in symbols.txt; statement lines come from the
// same table rather than from Ghidra's `; Line` comments.  A repeated number
// separated by an instruction of another line is the -O2 scheduler
// interleaving the prologue with the first statement, not two statements.
//
// Several arms have NO recoverable line numbers because GCC cross-jumped them
// into an identical tail elsewhere in the function -- see
// [[gcc-cross-jumps-identical-call-tails]].  MovieRoomMenuFilmSetPad() is the
// worst case: its have_num != 0 half shares the LEFT arm, the RIGHT arm, the
// "take the reel out" arm and the whole TRIANGLE arm with its have_num == 0
// half, so lines 712-717, 722-724, 728-739 and 741-744 left nothing behind but
// the four collapsed notes 714, 740, 745 and 746.  Those statements are
// reconstructed from the shared code they branch into and are annotated with
// the line of the copy that survived.
//
// VERIFIED.  4/4 ZERO2.MAP .text exports and 30/30 functions.txt entries.
// .text is accounted for byte-for-byte: 0x1ae4 of code plus eighteen 4-byte
// alignment fills = the section's 0x1b2c, ending exactly at 0x22183c, so there
// is no unlisted body.  msg_lbl[2], msg_lbl[3], move_alpha_tbl1,
// move_alpha_tbl2 and move_pos_tbl are byte-identical to the ROM, diffed out
// of the compiled .obj.
//
// Two things do NOT round-trip through that diff, both for host reasons and
// both checked another way.  `film_reel_base` is a local aggregate whose
// image the EE compiler kept as a .rodata blob and MinGW materialises inline
// ([[rodata-blob-into-stack-is-a-local-initialiser]]); an offsetof harness
// confirms the initialiser below reproduces the ROM's 32 bytes exactly.  And
// DISP_FILM_REEL is 0x10 on the host against the ROM's 0xc, because
// `data_addr` is a pointer -- so disp_film_reel[] diverges at +0xc by
// construction.
//
// Reconstructed from the Feb 6 2004 prototype (SLES_523.84).

#include "movie_room_menu.h"

#include "movie_projecter.h"

#include "../../../common/ol_load.h"            /* ol_loadGetHeap/FreeHeap   */
#include "../../../common/utility2.h"           /* PRINT_WARNING             */
#include "../../../common/variable.h"           /* pad[] / paddat[]          */
#include "../../../graphics/draw_env.h"         /* GET_SCISSOR_REGISTER      */
#include "../../../graphics/graph2d/draw_cmn.h" /* DrawCmn*                  */
#include "../../../graphics/graph2d/g2d_draw.h" /* CopySprDToSpr / DispSprD  */
#include "../../../graphics/graph2d/message.h"  /* SetMsgDefData / PrintMsg  */
#include "../../../system/eeiop/cddat.h"        /* ITEM_0NN_TM2 / *_PSS      */
#include "../../../system/eeiop/fileload.h"     /* FileLoadReqEE             */
#include "../../../system/os/system.h"          /* SystemBankPlay            */
#include "../../../system/pad/pad.h"            /* GetPadAnalogRpt           */

#include "../../item/prg/item.h"                /* ItemGet / ItemLost        */
#include "../../menu/anim_2d.h"                 /* ALPHA_ANIM_TBL / POS_*    */
#include "../../menu/menu_cmn.h"                /* MenuTim2SendVram          */
#include "../../menu/zero2_anim2d.h"            /* Zero2Anim2D_CsrAnimCtrl   */

/* The first film reel in the inventory: item_dat[] 34..40 = ITM_REEL1..7. */
#define ITEM_LABEL_REEL_TOP     34

/* Message bank 12 is this screen's; bank 44 is the shared item-name bank. */
#define MOVIE_ROOM_MENU_MSG     12
#define ITEM_NAME_MSG           44

/* msg_id inside bank 12. */
#define MSG_NO_HAVE_FILM        0
#define MSG_START               1
#define MSG_FILM_PLAY_CONF      2
#define MSG_EXIT_CONF           3
#define MSG_FILM_SEL            4
#define MSG_SEL_CHANGE          6       /* "put a different reel in"      */
#define MSG_SEL_TAKE_OUT        7
#define MSG_SEL_LEAVE           8
#define MSG_FILM_SET            12

/* Where the reel picture is staged in GS memory, and the TEX0 that samples
 * it.  The reel icons are the ordinary 64x64 inventory TIM2s. */
#define REEL_TBP                0x2bc0
#define REEL_CBP                0x2ee0
#define REEL_TEX0               0x2005dc066932abc0ULL

/* The strip is clipped to the window so a reel scrolling in or out is cut off
 * at the frame rather than drawn over it.  SCAX0 192, SCAX1 453, SCAY0 58,
 * SCAY1 294. */
#define REEL_STRIP_SCISSOR      0x0126003a01c500c0ULL

/* How far a reel travels when the strip scrolls one place. */
#define REEL_MOVE_WIDTH         269.0f

/* SE bank ids -- the same three every menu in the tree uses. */
#define SE_CURSOR               0
#define SE_CANCEL               1
#define SE_DECIDE               3

/* --------------------------------------------------------------------------
 *  The compacted strip of reels the player is carrying.  Only the first four
 *  entries carry an initialiser in the ROM's .data and the other three are
 *  zero-filled, even though the array is seven long -- reproduced as found.
 *  Nothing reads item_label or tex_label past have_num, so the difference is
 *  inert; it reads like a leftover from when there were four reels.
 * ------------------------------------------------------------------------ */
static DISP_FILM_REEL disp_film_reel[MOVIE_ROOM_MENU_FILM_MAX] =
{                                                           /* data 336e40 */
    { -1, -1, NULL },
    { -1, -1, NULL },
    { -1, -1, NULL },
    { -1, -1, NULL },
};

static void MovieRoomMenuMsgDispPad(void);
static void MovieRoomMenuFilmSelPad(void);
static void MovieRoomMenuFilmPlayConfPad(void);
static void MovieRoomMenuExitConfPad(void);
static void MovieRoomMenuNoHaveFilmPad(void);
static void MovieRoomMenuFilmSetPad(void);

static void MovieRoomMenuStartMsgDisp(int off_x, int off_y, u_char alpha);
static void MovieRoomMenuFilmSelDisp(int off_x, int off_y, u_char alpha);
static void MovieRoomMenuFilmPlayConfDisp(int off_x, int off_y, u_char alpha);
static void MovieRoomMenuExitConfDisp(int off_x, int off_y, u_char alpha);
static void MovieRoomMenuNoHaveFilmDisp(int off_x, int off_y, u_char alpha);
static void MovieRoomMenuFilmSetDisp(int off_x, int off_y, u_char alpha);

/* Both tables are indexed by MOVIE_ROOM_MENU_CTRL::mode and stay in step. */
static void (*movie_room_menu_pad_func[6])(void) =          /* data 336e98 */
{
    MovieRoomMenuMsgDispPad,
    MovieRoomMenuFilmSelPad,
    MovieRoomMenuFilmPlayConfPad,
    MovieRoomMenuExitConfPad,
    MovieRoomMenuNoHaveFilmPad,
    MovieRoomMenuFilmSetPad,
};

static void (*movie_room_menu_disp_func[6])(int, int, u_char) =
{                                                           /* data 336eb0 */
    MovieRoomMenuStartMsgDisp,
    MovieRoomMenuFilmSelDisp,
    MovieRoomMenuFilmPlayConfDisp,
    MovieRoomMenuExitConfDisp,
    MovieRoomMenuNoHaveFilmDisp,
    MovieRoomMenuFilmSetDisp,
};

static MOVIE_ROOM_MENU_CTRL movie_room_menu_ctrl;           /* sbss 3f4e88 */
static MOVIE_ROOM_MENU_DISP movie_room_menu_disp;           /* sbss 3f4e90 */

static void MovieRoomMenuCtrlInit(void);
static void SetDispFilmData(void);
static void GetMovieRoomMenuTexMem(void **data_addr, int data_label);
static void MovieRoomMenuTexLoadReq(void *tex_addr, int data_label);
static int  MovieRoomMenuTexLoadWait(void);
static void MovieRoomMenuFilmSelAnimCtrl(char *anim_step, short *anim_timer);
static void LiberateMovieRoomMenuTexMem(void **tex_addr);
static void MovieRoomMenuDispInit(void);
static void MovieRoomMenuFilmReelDisp(int off_x, int off_y, u_char alpha);
static void MovieRoomMenuFilmMoveAnim(char *anim_step, short *anim_timer,
                                      float *pos);
static void MovieRoomMenuCursorDisp(int off_x, int off_y, u_char alpha);
static void MovieRoomMenuFilmReelNameDisp(int off_x, int off_y, int alpha,
                                          int item_label);


/* ------------------------------------------------------------------------ *
 *  The two film-number <-> item-id converters.  Every film has a PAL twin one
 *  CD file up, which is where the /2 and *2 come from.
 * ------------------------------------------------------------------------ */
static int GetItemNameFromPlayFilmNo(int iPlayFilm)                    /* 273 */
{
    return (iPlayFilm - MOVIE_ROOM_000_PSS) / 2 + ITEM_LABEL_REEL_TOP; /* 274 */
}

static int GetPlayFilmNoFromItemNo(int iItem)                          /* 277 */
{
    /* GCC folded the two constants into one `iItem * 2 + 3327`, so the two
     * halves are not separately recoverable; this is the form its sibling
     * above inverts. */
    return (iItem - ITEM_LABEL_REEL_TOP) * 2 + MOVIE_ROOM_000_PSS;     /* 278 */
}

/* ------------------------------------------------------------------------ *
 *  Init -- entered from ingame.c when the player walks up to a projector.
 * ------------------------------------------------------------------------ */
void MovieRoomMenuInit(void)                                           /* 287 */
{
    MovieRoomMenuCtrlInit();                                           /* 291 */

    SetDispFilmData();                                                 /* 293 */

    MovieRoomMenuDispInit();                                           /* 296 */

    /* iFilmNo has no stab, like movie_projecterPlay()'s iIndex: the value
     * never has to survive a call, so the allocator left it in v0 and dbxout
     * skipped the pseudo.  The line map is what recovers it -- the call is at
     * 300 and the test at 303, and a single `if (f() < 0)` would put both on
     * one line.  The name is the port's. */
    int iFilmNo = movie_projecterGetFilmNo();                          /* 300 */

    if (iFilmNo >= 0) {                                                /* 303 */

        /* A reel is already loaded, so open on the take-out/swap menu. */
        movie_room_menu_ctrl.mode = MOVIE_ROOM_MENU_MODE_FILM_SET;     /* 305 */
    } else {

        if (movie_room_menu_ctrl.have_num == 0) {                      /* 309 */
            movie_room_menu_ctrl.step = 1;                             /* 310 */
            movie_room_menu_ctrl.mode = MOVIE_ROOM_MENU_MODE_NO_HAVE_FILM;
                                                                       /* 311 */
        } else {
            movie_room_menu_ctrl.mode = MOVIE_ROOM_MENU_MODE_START_MSG;
        }
    }
}                                                                      /* 315 */

static void MovieRoomMenuCtrlInit(void)                                /* 323 */
{
    movie_room_menu_ctrl.step        = 0;                              /* 326 */
    movie_room_menu_ctrl.mode        = 0;                              /* 327 */
    movie_room_menu_ctrl.cursor      = 0;                              /* 328 */
    movie_room_menu_ctrl.next_cursor = 0;                              /* 329 */
    movie_room_menu_ctrl.conf_csr    = 0;                              /* 330 */
    movie_room_menu_ctrl.play_flg    = 0;                              /* 331 */
    movie_room_menu_ctrl.have_num    = 0;                              /* 333 */
}

/* An inline the ROM expands once, inside SetDispFilmData(), and that has no
 * symbol of its own -- only its line survives (339, below this file's own
 * 346).  See [[out-of-order-line-numbers-mean-inlined-static]].  Its name and
 * its parameter are the port's; GCC folded the add into the loop's induction
 * variable, so whether the ROM passed the item id or the loop index is not
 * recoverable. */
static inline int GetFilmTexLabel(int iItem)                           /* 338 */
{
    return ITEM_000_TM2 + iItem;                                       /* 339 */
}

/* ------------------------------------------------------------------------ *
 *  SetDispFilmData -- walk the seven reels, keep the ones the player has, and
 *  pack them into the head of disp_film_reel[] with their icons loading.
 *  have_num is both the output count and the write cursor.
 * ------------------------------------------------------------------------ */
static void SetDispFilmData(void)                                      /* 346 */
{
    /* Two separate counters in the ROM -- functions.txt lists `int i` twice,
     * in s2 and v1, one per loop. */
    for (int i = 0; i < MOVIE_ROOM_MENU_FILM_MAX; i++) {               /* 348 */

        if (GetPlyrItemHaveNum(ITEM_LABEL_REEL_TOP + i) > 0) {         /* 350 */

            disp_film_reel[movie_room_menu_ctrl.have_num].item_label =
                ITEM_LABEL_REEL_TOP + i;                               /* 352 */
            disp_film_reel[movie_room_menu_ctrl.have_num].tex_label =
                GetFilmTexLabel(ITEM_LABEL_REEL_TOP + i);              /* 353 */

            GetMovieRoomMenuTexMem(
                &disp_film_reel[movie_room_menu_ctrl.have_num].data_addr,
                GetFilmTexLabel(ITEM_LABEL_REEL_TOP + i));             /* 356 */

            MovieRoomMenuTexLoadReq(
                disp_film_reel[movie_room_menu_ctrl.have_num].data_addr,
                GetFilmTexLabel(ITEM_LABEL_REEL_TOP + i));             /* 358 */

            movie_room_menu_ctrl.have_num++;                           /* 369 */
        }
    }                                                                  /* 371 */

    /* Anything past the last reel must read as "nothing loaded" -- End()
     * walks only have_num entries, but the draw side does not. */
    for (int i = movie_room_menu_ctrl.have_num;
         i < MOVIE_ROOM_MENU_FILM_MAX; i++) {                          /* 373 */
        disp_film_reel[i].data_addr = NULL;                            /* 374 */
    }                                                                  /* 375 */
}

/* ------------------------------------------------------------------------ *
 *  The texture quartet.  These claim out of the ingame load heap, so a
 *  re-entered Init() has to give the old block back first.
 * ------------------------------------------------------------------------ */
static void GetMovieRoomMenuTexMem(void **data_addr, int data_label)   /* 383 */
{
    if (*data_addr != NULL) {                                          /* 386 */
        LiberateMovieRoomMenuTexMem(data_addr);                        /* 387 */
    }

    *data_addr = ol_loadGetHeap(GetFileSize(data_label));              /* 393 */
}

static void MovieRoomMenuTexLoadReq(void *tex_addr, int data_label)    /* 402 */
{
    if (tex_addr != NULL) {                                            /* 406 */
        FileLoadReqEE(data_label, tex_addr, 6, NULL, NULL);            /* 407 */
    }
}

/* 1 once every icon has arrived.  A slot whose data_addr is NULL is skipped
 * rather than waited on, so a failed claim does not stall the menu. */
static int MovieRoomMenuTexLoadWait(void)                              /* 417 */
{
    int i;

    for (i = 0; i < movie_room_menu_ctrl.have_num; i++) {              /* 425 */

        if (disp_film_reel[i].data_addr != NULL) {                     /* 426 */

            if (FileLoadIsEnd2(disp_film_reel[i].tex_label,
                               disp_film_reel[i].data_addr) == 0) {    /* 430 */
                break;
            }
        }
    }                                                                  /* 433 */

    return (i < movie_room_menu_ctrl.have_num) ^ 1;                    /* 436 */
}                                                                      /* 444 */

/* ------------------------------------------------------------------------ *
 *  Main -- one frame.  0 = the menu is finished and ingame.c should drop the
 *  sub-phase; 1 = keep going.
 * ------------------------------------------------------------------------ */
int MovieRoomMenuMain(void)                                            /* 455 */
{
    if (movie_room_menu_ctrl.step == 0) {                              /* 458 */

        if (MovieRoomMenuTexLoadWait() != 0) {                         /* 459 */
            movie_room_menu_ctrl.step = 1;                             /* 460 */
        }
    }
    else if (movie_room_menu_ctrl.step == 1) {                         /* 463 */

        /* The pad is read every frame, but the reel strip only settles when
         * its scroll animation has finished. */
        if (movie_room_menu_disp.move_anim_step != 2) {                /* 464 */
            MovieRoomMenuFilmSelAnimCtrl(&movie_room_menu_disp.move_anim_step,
                                         &movie_room_menu_disp.move_anim_timer);
                                                                       /* 465 */
        }

        movie_room_menu_pad_func[movie_room_menu_ctrl.mode]();         /* 469 */
    }
    else if (movie_room_menu_ctrl.step == 2) {                         /* 471 */
        MovieRoomMenuEnd();                                            /* 473 */
        return 0;                                                      /* 474 */
    }

    return 1;                                                          /* 477 */
}                                                                      /* 479 */

/* ------------------------------------------------------------------------ *
 *  The reel-strip scroll: eight frames, and the cursor only commits to the
 *  new reel when it lands.
 * ------------------------------------------------------------------------ */
static void MovieRoomMenuFilmSelAnimCtrl(char *anim_step,
                                         short *anim_timer)            /* 484 */
{
    if (*anim_step == 0) {                                             /* 488 */
        *anim_timer = 0;                                               /* 489 */
        *anim_step  = 1;                                               /* 490 */
    }

    if (*anim_step == 1) {                                             /* 493 */

        (*anim_timer)++;                                               /* 495 */

        if (*anim_timer >= 8) {                                        /* 496 */
            *anim_step = 2;                                            /* 497 */

            movie_room_menu_ctrl.cursor =
                movie_room_menu_ctrl.next_cursor;                      /* 499 */
        }
    }
}                                                                      /* 506 */

/* ------------------------------------------------------------------------ *
 *  MODE_START_MSG -- "use the projector?".  Both buttons go on to the reel
 *  strip; only the cue differs.
 * ------------------------------------------------------------------------ */
static void MovieRoomMenuMsgDispPad(void)                              /* 517 */
{
    if (*paddat[0] == 1) {                                             /* 521 */
        SystemBankPlay(SE_DECIDE, 1, 0, 0, NULL, 0x3200, 0x1000);      /* 522 */

        movie_room_menu_ctrl.mode = MOVIE_ROOM_MENU_MODE_FILM_SEL;     /* 525 */
    }
    else if (*paddat[1] == 1) {                                        /* 528 */
        SystemBankPlay(SE_CANCEL, 1, 0, 0, NULL, 0x3200, 0x1000);      /* 529 */

        movie_room_menu_ctrl.mode = MOVIE_ROOM_MENU_MODE_FILM_SEL;     /* 532 */
    }
}

/* ------------------------------------------------------------------------ *
 *  MODE_FILM_SEL -- scroll the strip, pick a reel, or back out.
 *
 *  The two scroll arms are deliberately asymmetric about the cue: the SE is
 *  played whether or not the strip can actually move, so a single reel still
 *  clicks when the player presses left or right.
 * ------------------------------------------------------------------------ */
static void MovieRoomMenuFilmSelPad(void)                              /* 541 */
{
    if ((pad[0].rpt & 0x8000) || GetPadAnalogRpt(2)) {                 /* 545 */

        if (movie_room_menu_disp.move_anim_step != 2) return;          /* 546 */

        SystemBankPlay(SE_CURSOR, 1, 0, 0, NULL, 0x3200, 0x1000);      /* 547 */

        if (movie_room_menu_ctrl.have_num < 2) return;                 /* 549 */

        movie_room_menu_ctrl.next_cursor =
            (movie_room_menu_ctrl.cursor + movie_room_menu_ctrl.have_num - 1)
            % movie_room_menu_ctrl.have_num;                           /* 550 */

        movie_room_menu_disp.move_anim_step  = 0;                      /* 553 */
        movie_room_menu_disp.move_anim_timer = 0;                      /* 554 */
        movie_room_menu_disp.move_rot        = 0;                      /* 555 */
    }
    else if ((pad[0].rpt & 0x2000) || GetPadAnalogRpt(3)) {            /* 560 */

        if (movie_room_menu_disp.move_anim_step != 2) return;          /* 561 */

        SystemBankPlay(SE_CURSOR, 1, 0, 0, NULL, 0x3200, 0x1000);      /* 562 */

        if (movie_room_menu_ctrl.have_num < 2) return;                 /* 564 */

        movie_room_menu_ctrl.next_cursor =
            (movie_room_menu_ctrl.cursor + 1)
            % movie_room_menu_ctrl.have_num;                           /* 565 */

        movie_room_menu_disp.move_anim_step  = 0;                      /* 568 */
        movie_room_menu_disp.move_anim_timer = 0;                      /* 569 */
        movie_room_menu_disp.move_rot        = 1;                      /* 570 */
    }
    else if (*paddat[0] == 1) {                                        /* 575 */
        SystemBankPlay(SE_DECIDE, 1, 0, 0, NULL, 0x3200, 0x1000);      /* 576 */

        /* Snapping the scroll to "settled" is what stops the confirm window
         * from opening over a half-scrolled strip. */
        movie_room_menu_disp.move_anim_step = 2;                       /* 579 */

        movie_room_menu_ctrl.conf_csr = 1;                             /* 581 */
        movie_room_menu_ctrl.mode = MOVIE_ROOM_MENU_MODE_FILM_PLAY_CONF;
                                                                       /* 582 */
    }
    else if (*paddat[1] == 1) {                                        /* 585 */
        SystemBankPlay(SE_CANCEL, 1, 0, 0, NULL, 0x3200, 0x1000);      /* 586 */

        movie_room_menu_disp.move_anim_step = 2;                       /* 589 */

        movie_room_menu_ctrl.conf_csr = 1;                             /* 591 */
        movie_room_menu_ctrl.mode = MOVIE_ROOM_MENU_MODE_EXIT_CONF;    /* 592 */
    }
}

/* ------------------------------------------------------------------------ *
 *  MODE_FILM_PLAY_CONF -- "play this reel?".
 *
 *  Answering yes is the one place a film is actually loaded: whatever was in
 *  the projector comes back to the inventory, the chosen reel leaves it, and
 *  movie_projecter.o is armed.  step 2 then tears the menu down so the film
 *  plays with nothing drawn over it.
 * ------------------------------------------------------------------------ */
static void MovieRoomMenuFilmPlayConfPad(void)                         /* 601 */
{
    if ((pad[0].rpt & 0x8000) || GetPadAnalogRpt(2) ||                 /* 605 */
        (pad[0].rpt & 0x2000) || GetPadAnalogRpt(3)) {                 /* 610 */

        SystemBankPlay(SE_CURSOR, 1, 0, 0, NULL, 0x3200, 0x1000);      /* 611 */
        movie_room_menu_ctrl.conf_csr ^= 1;                            /* 612 */
    }
    else if (*paddat[0] == 1) {                                        /* 615 */
        SystemBankPlay(SE_DECIDE, 1, 0, 0, NULL, 0x3200, 0x1000);      /* 616 */

        if (movie_room_menu_ctrl.conf_csr != 0) {                      /* 619 */
            movie_room_menu_ctrl.mode = MOVIE_ROOM_MENU_MODE_FILM_SEL;
        } else {
            int iPlayFilm = movie_projecterTakeFilm();                 /* 621 */

            if (iPlayFilm >= 0) {                                      /* 622 */
                ItemGet(GetItemNameFromPlayFilmNo(iPlayFilm), 1);      /* 623 */
            }

            ItemLost(
                disp_film_reel[movie_room_menu_ctrl.cursor].item_label, 1);
                                                                       /* 627 */
            movie_projecterSetFilmNo(GetPlayFilmNoFromItemNo(
                disp_film_reel[movie_room_menu_ctrl.cursor].item_label));
                                                                       /* 628 */

            movie_projecterPlay();                                     /* 630 */
            movie_room_menu_ctrl.step = 2;                             /* 631 */
        }
    }
    else if (*paddat[1] == 1) {                                        /* 639 */
        SystemBankPlay(SE_CANCEL, 1, 0, 0, NULL, 0x3200, 0x1000);      /* 640 */

        movie_room_menu_ctrl.mode = MOVIE_ROOM_MENU_MODE_FILM_SEL;     /* 641 */
    }
}

/* ------------------------------------------------------------------------ *
 *  MODE_EXIT_CONF -- "stop watching?".  Structurally the twin of the one
 *  above with the loading taken out.
 * ------------------------------------------------------------------------ */
static void MovieRoomMenuExitConfPad(void)                             /* 650 */
{
    if ((pad[0].rpt & 0x8000) || GetPadAnalogRpt(2) ||                 /* 654 */
        (pad[0].rpt & 0x2000) || GetPadAnalogRpt(3)) {                 /* 659 */

        SystemBankPlay(SE_CURSOR, 1, 0, 0, NULL, 0x3200, 0x1000);      /* 660 */
        movie_room_menu_ctrl.conf_csr ^= 1;                            /* 661 */
    }
    else if (*paddat[0] == 1) {                                        /* 664 */
        SystemBankPlay(SE_DECIDE, 1, 0, 0, NULL, 0x3200, 0x1000);      /* 665 */

        if (movie_room_menu_ctrl.conf_csr != 0) {                      /* 668 */
            movie_room_menu_ctrl.mode = MOVIE_ROOM_MENU_MODE_FILM_SEL;
        } else {
            movie_room_menu_ctrl.step = 2;                             /* 669 */
        }
    }
    else if (*paddat[1] == 1) {                                        /* 677 */
        SystemBankPlay(SE_CANCEL, 1, 0, 0, NULL, 0x3200, 0x1000);      /* 678 */

        movie_room_menu_ctrl.mode = MOVIE_ROOM_MENU_MODE_FILM_SEL;     /* 679 */
    }
}

/* ------------------------------------------------------------------------ *
 *  MODE_NO_HAVE_FILM -- there is nothing to put in.  Either button leaves,
 *  and both arms are byte-identical (GCC cross-jumped them into one tail).
 * ------------------------------------------------------------------------ */
static void MovieRoomMenuNoHaveFilmPad(void)                           /* 689 */
{
    if (*paddat[0] == 1) {                                             /* 693 */
        SystemBankPlay(SE_CANCEL, 1, 0, 0, NULL, 0x3200, 0x1000);      /* 696 */
        movie_room_menu_ctrl.step = 2;
    }
    else if (*paddat[1] == 1) {                                        /* 699 */
        SystemBankPlay(SE_CANCEL, 1, 0, 0, NULL, 0x3200, 0x1000);      /* 700 */
        movie_room_menu_ctrl.step = 2;                                 /* 701 */
    }
}

/* ------------------------------------------------------------------------ *
 *  MODE_FILM_SET -- a reel is already in the projector.
 *
 *  Two whole copies of the same menu: three rows (change / take out / leave)
 *  when the player is carrying other reels, two (take out / leave) when they
 *  are not.  Only the row count, the wrap limit and the row->action mapping
 *  differ, and GCC cross-jumped every arm the two halves share -- which is
 *  why the have_num != 0 half has almost no line numbers of its own.
 * ------------------------------------------------------------------------ */
static void MovieRoomMenuFilmSetPad(void)                              /* 707 */
{
    if (movie_room_menu_ctrl.have_num != 0) {                          /* 709 */

        if ((pad[0].rpt & 0x8000) || GetPadAnalogRpt(2)) {             /* 711 */

            if (movie_room_menu_ctrl.conf_csr != 0) {
                SystemBankPlay(SE_CURSOR, 1, 0, 0, NULL, 0x3200, 0x1000);
                movie_room_menu_ctrl.conf_csr--;                       /* 714 */
            }
        }
        else if ((pad[0].rpt & 0x2000) || GetPadAnalogRpt(3)) {        /* 718 */

            if (movie_room_menu_ctrl.conf_csr < 2) {                   /* 719 */
                SystemBankPlay(SE_CURSOR, 1, 0, 0, NULL, 0x3200, 0x1000);
                                                                       /* 721 */
                movie_room_menu_ctrl.conf_csr++;
            }
        }
        else if (*paddat[0] == 1) {                                    /* 725 */
            SystemBankPlay(SE_DECIDE, 1, 0, 0, NULL, 0x3200, 0x1000);  /* 726 */

            switch (movie_room_menu_ctrl.conf_csr) {                   /* 727 */
            case 0:
                movie_room_menu_ctrl.mode = MOVIE_ROOM_MENU_MODE_FILM_SEL;
                break;

            case 1:
                ItemGet(GetItemNameFromPlayFilmNo(movie_projecterTakeFilm()),
                        1);
                movie_room_menu_ctrl.step = 2;
                break;

            case 2:
                movie_room_menu_ctrl.step = 2;
                break;
            }                                                          /* 740 */
        }
        else if (*paddat[1] == 1) {                                    /* 745 */
            SystemBankPlay(SE_CANCEL, 1, 0, 0, NULL, 0x3200, 0x1000);  /* 746 */
            movie_room_menu_ctrl.step = 2;
        }
    } else {

        if ((pad[0].rpt & 0x8000) || GetPadAnalogRpt(2)) {             /* 750 */

            if (movie_room_menu_ctrl.conf_csr != 0) {                  /* 751 */
                SystemBankPlay(SE_CURSOR, 1, 0, 0, NULL, 0x3200, 0x1000);
                                                                       /* 752 */
                movie_room_menu_ctrl.conf_csr--;                       /* 753 */
            }
        }
        else if ((pad[0].rpt & 0x2000) || GetPadAnalogRpt(3)) {        /* 757 */

            if (movie_room_menu_ctrl.conf_csr == 0) {                  /* 758 */
                SystemBankPlay(SE_CURSOR, 1, 0, 0, NULL, 0x3200, 0x1000);
                                                                       /* 759 */
                movie_room_menu_ctrl.conf_csr++;                       /* 760 */
            }
        }
        else if (*paddat[0] == 1) {                                    /* 764 */
            SystemBankPlay(SE_DECIDE, 1, 0, 0, NULL, 0x3200, 0x1000);  /* 765 */

            switch (movie_room_menu_ctrl.conf_csr) {                   /* 766 */
            case 0:
                /* No `>= 0` guard, unlike MovieRoomMenuFilmPlayConfPad's --
                 * MODE_FILM_SET is only ever entered with a reel already in
                 * the projector, so TakeFilm() cannot answer -1 here.  Both
                 * copies of this arm are one shared body in the ROM. */
                ItemGet(GetItemNameFromPlayFilmNo(movie_projecterTakeFilm()),
                        1);                                            /* 768 */
                                                                       /* 769 */
                movie_room_menu_ctrl.step = 2;                         /* 774 */
                break;                                                 /* 775 */

            case 1:
                movie_room_menu_ctrl.step = 2;
                break;
            }
        }
        else if (*paddat[1] == 1) {                                    /* 779 */
            SystemBankPlay(SE_CANCEL, 1, 0, 0, NULL, 0x3200, 0x1000);  /* 780 */
            movie_room_menu_ctrl.step = 2;                             /* 781 */
        }
    }
}

/* ------------------------------------------------------------------------ *
 *  End -- give the reel icons' heap blocks back.
 * ------------------------------------------------------------------------ */
void MovieRoomMenuEnd(void)                                            /* 795 */
{
    int i;

    for (i = 0; i < movie_room_menu_ctrl.have_num; i++) {              /* 801 */
        LiberateMovieRoomMenuTexMem(&disp_film_reel[i].data_addr);     /* 802 */
    }                                                                  /* 803 */
}

static void LiberateMovieRoomMenuTexMem(void **tex_addr)               /* 813 */
{
    if (*tex_addr != NULL) {                                           /* 816 */
        ol_loadFreeHeap(*tex_addr);                                    /* 819 */
        *tex_addr = NULL;                                              /* 820 */
    }
}

/* ------------------------------------------------------------------------ *
 *  DispInit -- move_anim_step opens at 2 ("settled"), so the strip is drawn
 *  in place on the first frame rather than sliding in.
 * ------------------------------------------------------------------------ */
static void MovieRoomMenuDispInit(void)                                /* 833 */
{
    movie_room_menu_disp.anim_step       = 0;                          /* 836 */
    movie_room_menu_disp.anim_timer      = 0;                          /* 837 */
    movie_room_menu_disp.cursor_timer    = 0;                          /* 838 */
    movie_room_menu_disp.move_anim_step  = 2;                          /* 839 */
    movie_room_menu_disp.move_anim_timer = 0;                          /* 840 */
    movie_room_menu_disp.move_rot        = 0;                          /* 841 */
}

/* ------------------------------------------------------------------------ *
 *  Disp -- one frame of drawing, from ingame.c.
 *
 *  anim_step is only ever 0 in this build (nothing but DispInit() writes it),
 *  so the `!= 4` test never fails.  Reproduced as found.
 * ------------------------------------------------------------------------ */
void MovieRoomMenuDisp(void)                                           /* 849 */
{
    if (movie_room_menu_ctrl.step == 1) {                              /* 856 */

        if (movie_room_menu_disp.anim_step != 4) {                     /* 857 */
            movie_room_menu_disp_func[movie_room_menu_ctrl.mode](0, 0, 0x80);
                                                                       /* 858 */
        }
    }
}

/* ------------------------------------------------------------------------ *
 *  The six drawing halves.  Each opens with the bank-12 defaults so the
 *  caption window lands wherever the message data says, and none of them
 *  reads off_x/off_y for that part -- MovieRoomMenuDisp() passes 0, 0 anyway.
 * ------------------------------------------------------------------------ */
static void MovieRoomMenuStartMsgDisp(int off_x, int off_y, u_char alpha)
{                                                                      /* 872 */
    DISP_STR    msg_data;
    MSG_WIN_DAT msg_win;

    SetMsgDefData(&msg_data, MOVIE_ROOM_MENU_MSG);                     /* 878 */
    SetMsgWinDefData(&msg_win, MOVIE_ROOM_MENU_MSG);                   /* 879 */

    DrawCmnWindow(0, msg_win.x, msg_win.y, msg_win.w, msg_win.h,
                  alpha, 102);                                         /* 883 */

    PrintMsg(MOVIE_ROOM_MENU_MSG, MSG_START,
             msg_data.pos_x, msg_data.pos_y, 1, alpha, 0);             /* 887 */
}

static void MovieRoomMenuFilmSelDisp(int off_x, int off_y, u_char alpha)
{                                                                      /* 898 */
    DISP_STR    msg_data;
    MSG_WIN_DAT msg_win;

    SetMsgDefData(&msg_data, MOVIE_ROOM_MENU_MSG);                     /* 904 */
    SetMsgWinDefData(&msg_win, MOVIE_ROOM_MENU_MSG);                   /* 905 */

    DrawCmnWindow(0, 176.0f, 45.0f, 293.0f, 262.0f, alpha, 108);       /* 910 */

    MovieRoomMenuFilmReelDisp(off_x, off_y, alpha);                    /* 913 */

    MovieRoomMenuCursorDisp(off_x, off_y, alpha);                      /* 916 */

    MovieRoomMenuFilmReelNameDisp(
        off_x, off_y, alpha,
        disp_film_reel[movie_room_menu_ctrl.cursor].item_label);       /* 919 */

    DrawCmnWindow(0, msg_win.x, msg_win.y, msg_win.w, msg_win.h,
                  alpha, 102);                                         /* 922 */

    PrintMsg(MOVIE_ROOM_MENU_MSG, MSG_FILM_SEL,
             msg_data.pos_x, msg_data.pos_y, 1, alpha, 0);             /* 926 */
}

static void MovieRoomMenuFilmPlayConfDisp(int off_x, int off_y, u_char alpha)
{                                                                      /* 937 */
    DISP_STR    msg_data;
    MSG_WIN_DAT msg_win;

    SetMsgDefData(&msg_data, MOVIE_ROOM_MENU_MSG);                     /* 943 */
    SetMsgWinDefData(&msg_win, MOVIE_ROOM_MENU_MSG);                   /* 944 */

    DrawCmnWindow(0, 176.0f, 45.0f, 293.0f, 262.0f, alpha, 108);       /* 949 */

    MovieRoomMenuFilmReelDisp(off_x, off_y, alpha);                    /* 952 */

    MovieRoomMenuFilmReelNameDisp(
        off_x, off_y, alpha,
        disp_film_reel[movie_room_menu_ctrl.cursor].item_label);       /* 955 */

    DrawCmnWindow(0, msg_win.x, msg_win.y, msg_win.w, msg_win.h,
                  alpha, 102);                                         /* 958 */

    PrintMsg(MOVIE_ROOM_MENU_MSG, MSG_FILM_PLAY_CONF,
             msg_data.pos_x, msg_data.pos_y, 1, alpha, 0);             /* 962 */

    DrawCmnSelCsr(0,
                  (float)(off_x + movie_room_menu_ctrl.conf_csr * 207 + 155),
                  (float)(off_y + 387), alpha, 0.0f, 0);               /* 966 */

    DrawCmnSelYes(0, (float)(off_x + 153), (float)(off_y + 389), alpha);
                                                                       /* 969 */
    DrawCmnSelNo(0, (float)(off_x + 361), (float)(off_y + 389), alpha);
                                                                       /* 970 */
}

static void MovieRoomMenuExitConfDisp(int off_x, int off_y, u_char alpha)
{                                                                      /* 981 */
    DISP_STR    msg_data;
    MSG_WIN_DAT msg_win;

    SetMsgDefData(&msg_data, MOVIE_ROOM_MENU_MSG);                     /* 987 */
    SetMsgWinDefData(&msg_win, MOVIE_ROOM_MENU_MSG);                   /* 988 */

    DrawCmnWindow(0, 176.0f, 45.0f, 293.0f, 262.0f, alpha, 108);       /* 993 */

    MovieRoomMenuFilmReelDisp(off_x, off_y, alpha);                    /* 996 */

    MovieRoomMenuFilmReelNameDisp(
        off_x, off_y, alpha,
        disp_film_reel[movie_room_menu_ctrl.cursor].item_label);       /* 999 */

    DrawCmnWindow(0, msg_win.x, msg_win.y, msg_win.w, msg_win.h,
                  alpha, 102);                                         /* 1002 */

    PrintMsg(MOVIE_ROOM_MENU_MSG, MSG_EXIT_CONF,
             msg_data.pos_x, msg_data.pos_y, 1, alpha, 0);             /* 1006 */

    DrawCmnSelCsr(0,
                  (float)(off_x + movie_room_menu_ctrl.conf_csr * 207 + 155),
                  (float)(off_y + 387), alpha, 0.0f, 0);               /* 1010 */

    DrawCmnSelYes(0, (float)(off_x + 153), (float)(off_y + 389), alpha);
                                                                       /* 1013 */
    DrawCmnSelNo(0, (float)(off_x + 361), (float)(off_y + 389), alpha);
                                                                       /* 1014 */
}

static void MovieRoomMenuNoHaveFilmDisp(int off_x, int off_y, u_char alpha)
{                                                                      /* 1025 */
    DISP_STR    msg_data;
    MSG_WIN_DAT msg_win;

    SetMsgDefData(&msg_data, MOVIE_ROOM_MENU_MSG);                     /* 1031 */
    SetMsgWinDefData(&msg_win, MOVIE_ROOM_MENU_MSG);                   /* 1032 */

    DrawCmnWindow(0, msg_win.x, msg_win.y, msg_win.w, msg_win.h,
                  alpha, 102);                                         /* 1035 */

    PrintMsg(MOVIE_ROOM_MENU_MSG, MSG_NO_HAVE_FILM,
             msg_data.pos_x, msg_data.pos_y, 1, alpha, 0);             /* 1039 */
}

/* ------------------------------------------------------------------------ *
 *  MODE_FILM_SET's drawing half: the row list, three wide or two.
 *
 *  Both rows and the caption are drawn at a hardcoded 0x80 rather than at the
 *  caller's `alpha`; only the row text honours it.  The two msg_lbl tables are
 *  function-local statics, which is why the two-entry one lands in .sdata
 *  (small enough for -G8) and the three-entry one in .rodata.
 * ------------------------------------------------------------------------ */
static void MovieRoomMenuFilmSetDisp(int off_x, int off_y, u_char alpha)
{                                                                      /* 1046 */
    DISP_STR    msg_data;
    MSG_WIN_DAT msg_win;

    SetMsgDefData(&msg_data, MOVIE_ROOM_MENU_MSG);                     /* 1061 */
    SetMsgWinDefData(&msg_win, MOVIE_ROOM_MENU_MSG);                   /* 1062 */

    DrawCmnWindow(0, msg_win.x, msg_win.y, msg_win.w, msg_win.h,
                  0x80, 102);                                          /* 1064 */

    PrintMsg(MOVIE_ROOM_MENU_MSG, MSG_FILM_SET,
             msg_data.pos_x, msg_data.pos_y, 1, 0x80, 0);              /* 1068 */

    if (movie_room_menu_ctrl.have_num == 0) {                          /* 1086 */
        int x = off_x + 155;                                           /* 1093 */
        int y = off_y + 389;                                           /* 1094 */
        static const int msg_lbl[2] = { MSG_SEL_TAKE_OUT, MSG_SEL_LEAVE };
                                                            /* sdata 3f3428 */
        int i;

        for (i = 0; i < 2; i++) {                                      /* 1096 */

            if (i == movie_room_menu_ctrl.conf_csr) {                  /* 1097 */
                DrawSelItemMsg(MOVIE_ROOM_MENU_MSG, msg_lbl[i],
                               x, y, alpha, 1, 1, 0);                  /* 1098 */
            } else {
                DrawSelItemMsg(MOVIE_ROOM_MENU_MSG, msg_lbl[i],
                               x, y, alpha, 0, 1, 0);                  /* 1100 */
            }

            x += 207;                                                  /* 1101 */
        }                                                              /* 1102 */
    } else {
        int x = off_x + 140;                                           /* 1112 */
        int y = off_y + 389;                                           /* 1113 */
        static const int msg_lbl[3] = { MSG_SEL_CHANGE, MSG_SEL_TAKE_OUT,
                                        MSG_SEL_LEAVE };
                                                            /* rdata 3c1810 */
        int i;

        for (i = 0; i < 3; i++) {                                      /* 1115 */

            if (i == movie_room_menu_ctrl.conf_csr) {                  /* 1116 */
                DrawSelItemMsg(MOVIE_ROOM_MENU_MSG, msg_lbl[i],
                               x, y, alpha, 1, 1, 0);                  /* 1117 */
            } else {
                DrawSelItemMsg(MOVIE_ROOM_MENU_MSG, msg_lbl[i],
                               x, y, alpha, 0, 1, 0);                  /* 1119 */
            }

            x += 170;                                                  /* 1120 */
        }                                                              /* 1121 */
    }
}

/* ------------------------------------------------------------------------ *
 *  The reel strip itself.
 *
 *  While the strip is scrolling (move_anim_step < 2) two reels are drawn and
 *  cross-faded: the one leaving on move_alpha_tbl2 (128 -> 0) and the one
 *  arriving on move_alpha_tbl1 (0 -> 128), both offset by the same
 *  MovieRoomMenuFilmMoveAnim() position and mirrored about the scroll
 *  direction.  Once it settles, one reel is drawn at the caller's alpha.
 *
 *  All of it runs inside a scissor box the size of the window, which is what
 *  cuts a reel off at the frame instead of letting it draw over the border.
 * ------------------------------------------------------------------------ */
static void MovieRoomMenuFilmReelDisp(int off_x, int off_y, u_char alpha)
{                                                                      /* 1133 */
    DISP_SPRT win_ds;
    u_long    scissor_backup;
    float     anim_off_x = 0.0f;
    u_char    film_alpha;
    u_char    next_alpha;
    SPRT_DAT  film_reel_base = { 0, 0, 0, 256, 256, 194, 49, 0, 128, 0, 1 };
                                                                       /* 1140 */

    /* The cross-fade pair, and each other's mirror: an ease that spends most
     * of its eight frames near the ends. */
    static const ALPHA_ANIM_TBL move_alpha_tbl1[5] =        /* rdata 3c1840 */
    {
        {   0,   6, 0, 2 },
        {   6,  25, 2, 4 },
        {  25,  64, 4, 6 },
        {  64, 128, 6, 8 },
        {  -1,  -1, -1, -1 },
    };
    static const ALPHA_ANIM_TBL move_alpha_tbl2[5] =        /* rdata 3c1868 */
    {
        { 128,  64, 0, 2 },
        {  64,  25, 2, 4 },
        {  25,   6, 4, 6 },
        {   6,   0, 6, 8 },
        {  -1,  -1, -1, -1 },
    };

    scissor_backup = GET_SCISSOR_REGISTER(0);                          /* 1167 */
    SetScissorRegister(0, REEL_STRIP_SCISSOR);                         /* 1171 */

    if (movie_room_menu_disp.move_anim_step < 2) {                     /* 1175 */

        MovieRoomMenuFilmMoveAnim(&movie_room_menu_disp.move_anim_step,
                                  &movie_room_menu_disp.move_anim_timer,
                                  &anim_off_x);                        /* 1176 */

        next_alpha = Anim2D_CalcNowAlpha(
            move_alpha_tbl1, movie_room_menu_disp.move_anim_timer);     /* 1178 */
        film_alpha = Anim2D_CalcNowAlpha(
            move_alpha_tbl2, movie_room_menu_disp.move_anim_timer);     /* 1179 */

        if (movie_room_menu_disp.move_rot == 0) {                      /* 1181 */

            MenuTim2SendVram(
                (u_int *)disp_film_reel[movie_room_menu_ctrl.cursor].data_addr,
                REEL_TBP, REEL_CBP);                                   /* 1182 */
            CopySprDToSpr(&win_ds, &film_reel_base);                   /* 1183 */
            win_ds.tex0 = REEL_TEX0;                                   /* 1184 */

            win_ds.x = win_ds.x + (float)off_x - anim_off_x;
            win_ds.y = win_ds.y + (float)off_y;                        /* 1186 */
            win_ds.alpha = film_alpha;                                 /* 1187 */
            DispSprD(&win_ds);                                         /* 1188 */

            MenuTim2SendVram(
                (u_int *)disp_film_reel[movie_room_menu_ctrl.next_cursor]
                    .data_addr,
                REEL_TBP, REEL_CBP);                                   /* 1190 */
            CopySprDToSpr(&win_ds, &film_reel_base);                   /* 1191 */
            win_ds.tex0 = REEL_TEX0;                                   /* 1192 */

            win_ds.x = win_ds.x + (float)off_x
                     + (REEL_MOVE_WIDTH - anim_off_x);
            win_ds.y = win_ds.y + (float)off_y;                        /* 1194 */
            win_ds.alpha = next_alpha;                                 /* 1195 */
            DispSprD(&win_ds);                                         /* 1196 */
        } else {
            MenuTim2SendVram(
                (u_int *)disp_film_reel[movie_room_menu_ctrl.cursor].data_addr,
                REEL_TBP, REEL_CBP);                                   /* 1199 */
            CopySprDToSpr(&win_ds, &film_reel_base);                   /* 1200 */
            win_ds.tex0 = REEL_TEX0;                                   /* 1201 */

            win_ds.x = win_ds.x + (float)off_x + anim_off_x;
            win_ds.y = win_ds.y + (float)off_y;                        /* 1203 */
            win_ds.alpha = film_alpha;                                 /* 1204 */
            DispSprD(&win_ds);                                         /* 1205 */

            MenuTim2SendVram(
                (u_int *)disp_film_reel[movie_room_menu_ctrl.next_cursor]
                    .data_addr,
                REEL_TBP, REEL_CBP);                                   /* 1207 */
            CopySprDToSpr(&win_ds, &film_reel_base);                   /* 1208 */
            win_ds.tex0 = REEL_TEX0;                                   /* 1209 */

            win_ds.x = win_ds.x + (float)off_x - REEL_MOVE_WIDTH
                     + anim_off_x;
            win_ds.y = win_ds.y + (float)off_y;                        /* 1211 */
            win_ds.alpha = next_alpha;                                 /* 1212 */
            DispSprD(&win_ds);                                         /* 1213 */
        }
    }

    if (movie_room_menu_disp.move_anim_step == 2) {                    /* 1216 */

        MenuTim2SendVram(
            (u_int *)disp_film_reel[movie_room_menu_ctrl.cursor].data_addr,
            REEL_TBP, REEL_CBP);                                       /* 1217 */
        CopySprDToSpr(&win_ds, &film_reel_base);                       /* 1218 */
        win_ds.tex0 = REEL_TEX0;                                       /* 1219 */

        win_ds.x = win_ds.x + (float)off_x;
        win_ds.y = win_ds.y + (float)off_y;                            /* 1221 */
        win_ds.alpha = (u_char)(win_ds.alpha * alpha >> 7);            /* 1222 */
        DispSprD(&win_ds);                                             /* 1223 */
    }

    SetScissorRegister(0, scissor_backup);                             /* 1228 */
}

/* ------------------------------------------------------------------------ *
 *  How far along the scroll the strip is, in pixels.  Step 2 (settled) parks
 *  it at 0; the negative-timer guard is the only PRINT_WARNING in the file.
 * ------------------------------------------------------------------------ */
static void MovieRoomMenuFilmMoveAnim(char *anim_step, short *anim_timer,
                                      float *pos)                      /* 1237 */
{
    /* One segment: the whole 269-pixel slide over eight frames, linear. */
    static const POS_ANIM_TBL move_pos_tbl[2] =            /* rdata 3c18b0 */
    {
        {  0.0f, REEL_MOVE_WIDTH,  0,  8, 0 },
        { -1.0f, -1.0f,           -1, -1, 0 },
    };

    if (*anim_timer < 0) {                                             /* 1247 */
        PRINT_WARNING("Warning!! %s", __FUNCTION__);                   /* 1248 */
        *anim_timer = 0;                                               /* 1249 */
    }

    switch (*anim_step) {                                              /* 1253 */
    case 0:
    case 1:
        *pos = Anim2D_CalcNowPos(move_pos_tbl, *anim_timer);           /* 1256 */
        break;                                                         /* 1257 */

    case 2:
        *pos = 0.0f;                                                   /* 1259 */
        break;
    }
}                                                                      /* 1262 */

/* The left/right arrows either side of the strip; the pulse is the shared
 * cursor animation every menu screen uses. */
static void MovieRoomMenuCursorDisp(int off_x, int off_y, u_char alpha)
{                                                                      /* 1274 */
    u_char rgb = 0;

    Zero2Anim2D_CsrAnimCtrl(&movie_room_menu_disp.cursor_timer, &rgb); /* 1280 */

    DrawCmnTriCsrL(0, (float)off_x + 209.0f, (float)off_y + 172.0f,
                   alpha, rgb);                                        /* 1283 */
    DrawCmnTriCsrR(0, (float)off_x + 422.0f, (float)off_y + 172.0f,
                   alpha, rgb);                                        /* 1284 */
}

/* The reel's name, under the strip.  Neither offset is read -- the plate and
 * the text are both at fixed screen coordinates. */
static void MovieRoomMenuFilmReelNameDisp(int off_x, int off_y, int alpha,
                                          int item_label)              /* 1295 */
{
    DrawCmnTwoLineWindow(0, 174.0f, 289.0f, 293.0f, 60.0f, alpha, 0x80);
                                                                       /* 1300 */

    PrintMsg_Arrange(ITEM_NAME_MSG, item_label, 320, 304, 1, alpha,
                     0, 0, 0, 2);                                      /* 1303 */
}
