// FILE: /home/zero_rom/zero2np/src/album/prg/album_view.c
//
// The album's photo viewer: one filed picture blown up to 346x230, its date,
// score, room and subject lines printed beside it, and the previous and next
// photos shown as thumbnails either side.  LEFT / RIGHT walk the list, TRIANGLE
// goes back to the edit view.
//
// Six exports and seventeen file-local helpers.  Two work blocks and one list:
//
//   * ALBUM_VIEW_CTRL is the machine -- a four-step `step`, the album type, the
//     cursor, how many photos the list holds, and photo_flg.
//   * ALBUM_VIEW_DISP is the fade pair plus the cursor pulse counter.
//   * disp_photo_no[16] is the list itself: the slots of album_info[]'s picture
//     file that are actually in use, in wrap-around order starting from the
//     photo the edit view left on, with the unused tail left at -1.
//
// Things worth knowing before touching it:
//
//  * The cursor indexes disp_photo_no[], not the picture file.  Every draw goes
//    through disp_photo_no[photo_no_csr], and the number under the picture is
//    that slot plus one -- so it is the file slot the player sees, not the
//    position in the list.
//
//  * photo_flg is a one-shot "the decompressor has been asked for this
//    picture".  AlbumViewPhotoDisp() files the request the first frame and then
//    draws from the work area every frame after; the two pad handlers clear it
//    so moving the cursor re-files.  DrawPhotoFromWorkAreaAddr() is what
//    actually keeps feeding the decompressor until the picture appears.
//
//  * Only album type 5 shows protection badges -- both the one over the big
//    picture and the four-piece one over a thumbnail are inside an
//    `album_type == 5` test.  The other six types file photos that cannot be
//    protected.
//
//  * A rotated sprite turns about its own already-offset position, and the
//    90-degree ones take the rotation centre at x + (float)ds.h -- which is
//    also what ds.x is set to, so the pair looks redundant and is not.  Same
//    idiom as album_disp.c's flare outlines, one quarter turn the other way.
//
//  * A statement whose only memory access goes through a fixed_array<>
//    subscript is attributed to fixed_array.h 124/125 and leaves no $LM of its
//    own.  That covers most of the album_info[] reads here, so several lines
//    below are interpolated into a measured gap rather than measured; they say
//    so at the site.
//
// Reconstructed from the Feb 6 2004 prototype (SLES_523.84), album_view.o
// (.text 0x128388..0x12a410 = 0x2088).
//
// Trailing /* NNN */ comments are the original source line numbers, measured
// from the $LM stabs.  27 of the 273 are interpolated rather than measured:
// most are statements a fixed_array<> subscript swallowed (the album_info[]
// tests and the two UncompressPhoto / DrawSPhoto calls), and three -- 261, 266
// and 280 in AlbumViewMain() -- are stores GCC folded into the `break` branch
// that follows them.

#include "album_view.h"

#include "album.h"                                  // album_info[] and the accessors
#include "album_disp.h"                             // AlbumInOutAnimCtrl
#include "../dat/album_dat.h"                       // album_tex[]
#include "../../common/utility2.h"                  // PRINT_ASSERT
#include "../../common/variable.h"                  // pad[] / paddat[] / DATE_INFO
#include "../../graphics/graph2d/draw_cmn.h"        // DrawCmn*
#include "../../graphics/graph2d/g2d_draw.h"        // DISP_SPRT / CopySprDToSpr
#include "../../graphics/graph2d/message.h"         // PrintMsg / PrintNumber_N / GetMsgIDNumMax
#include "../../graphics/graph2d/tim2.h"            // PK2SendVram
#include "../../graphics/graph3d/ctl/fixed_array.h"
#include "../../ingame/menu/play_data.h"            // SetDateInfoType
#include "../../ingame/menu/zero2_anim2d.h"         // Zero2Anim2D_CsrAnimCtrl / STEP_*
#include "../../ingame/photo/photo_make.h"          // UncompressPhoto / DrawPhoto*
#include "../../system/eeiop/cddat.h"               // ALBM_KKD_PAT*_PK2
#include "../../system/eeiop/fileload.h"            // FileLoadReqEE / FileLoadIsEnd2
#include "../../system/os/system.h"                 // GetLanguage / SystemBankPlay
#include "../../system/pad/pad.h"                   // GetPadAnalogRpt

/* ALBUM_VIEW_CTRL::step. */
#define ALBUM_VIEW_STEP_INIT        0   /* seed the fade, request the pak  */
#define ALBUM_VIEW_STEP_LOAD_WAIT   1   /* waiting on the background pak   */
#define ALBUM_VIEW_STEP_TOP         2   /* running                         */
#define ALBUM_VIEW_STEP_END         3   /* closing fade, then hand back    */

/* album_tex[] indices, by part.  See album_dat.c for the whole map. */
#define ALBUM_TEX_VIEW_FRAME            266     /* 27 pieces                */
#define ALBUM_TEX_VIEW_FRAME_NUM        27
#define ALBUM_TEX_THUMB_PROTECT         126     /* 4 pieces                 */
#define ALBUM_TEX_PHOTO_PROTECT         134     /* 4 pieces                 */
#define ALBUM_TEX_VIEW_PHOTO_FRAME      341     /* 6 corners                */
#define ALBUM_TEX_VIEW_PHOTO_FRAME_NUM  6
#define ALBUM_TEX_VIEW_COL_L            347     /* tiled 3 times            */
#define ALBUM_TEX_VIEW_COL_R            348     /* tiled 3 times            */
#define ALBUM_TEX_VIEW_ROW_T            349     /* tiled 8 times, rotated   */
#define ALBUM_TEX_VIEW_ROW_B            350     /* tiled 8 times, rotated   */
#define ALBUM_TEX_VIEW_ROW_T_CAP        351     /* rotated                  */
#define ALBUM_TEX_VIEW_ROW_B_CAP        352     /* rotated                  */
#define ALBUM_TEX_VIEW_CSR              353     /* 2 pieces                 */
#define ALBUM_TEX_VIEW_CSR_NUM          2
#define ALBUM_TEX_VIEW_THUMB_L          355     /* 4 pieces                 */
#define ALBUM_TEX_VIEW_THUMB_R          359     /* 4 pieces                 */
#define ALBUM_TEX_VIEW_THUMB_NUM        4

/* Photo-frame tile counts. */
#define ALBUM_VIEW_COL_NUM              3
#define ALBUM_VIEW_ROW_NUM              8

/* Protection badge pieces. */
#define ALBUM_VIEW_PROTECT_NUM          4

/* The album type whose photos can be protected. */
#define ALBUM_TYPE_PROTECTABLE          5

/* The big picture, and the two thumbnails. */
#define ALBUM_VIEW_PHOTO_X          145
#define ALBUM_VIEW_PHOTO_Y          111
#define ALBUM_VIEW_PHOTO_W          346
#define ALBUM_VIEW_PHOTO_H          230
#define ALBUM_VIEW_THUMB_L_X         67
#define ALBUM_VIEW_THUMB_R_X        525
#define ALBUM_VIEW_THUMB_Y          212
#define ALBUM_VIEW_THUMB_W           45
#define ALBUM_VIEW_THUMB_H           30

/* The three info readouts' rows, and how far apart the subject lines sit. */
#define ALBUM_VIEW_ROOM_Y           347
#define ALBUM_VIEW_DATE_Y           373
#define ALBUM_VIEW_SCORE_Y          397
#define ALBUM_VIEW_SUBJECT_X        360
#define ALBUM_VIEW_SUBJECT_PITCH     24

/* Room ids at or above this have no name plate. */
#define ALBUM_VIEW_ROOM_MSG_MAX     240

/* ---- state -------------------------------------------------------------- */

/* sdata 3ef4a8.  The per-album-type background pak. */
static void *album_view_tex_addr;

/* The six ALBM_KKD_PAT*_PK2 backgrounds, by album type.  Types 5 and 6 share
 * PAT0, which is why the last two entries repeat; GetLanguage() is added to
 * whichever one is picked.
 *
 * PORT DEVIATION: the ROM's copy is in .rodata (3a14e0) and the
 * reference_fixed_array below points at it.  Nothing writes through it here,
 * but reference_fixed_array<> takes a plain T*, so the backing array is
 * non-const -- the same shape effect_ene.c and game_data_save.c already use. */
static int album_view_tex[7] =                          /* rdata 3a14e0 */
{
    ALBM_KKD_PAT1_PK2, ALBM_KKD_PAT2_PK2, ALBM_KKD_PAT3_PK2,
    ALBM_KKD_PAT4_PK2, ALBM_KKD_PAT5_PK2,
    ALBM_KKD_PAT0_PK2, ALBM_KKD_PAT0_PK2,
};

static reference_fixed_array<int, 7> album_view_tex_tbl(album_view_tex); /* sbss 3f4ad0 */

typedef struct                      /* 0x5 */
{
    /* 0x0 */ char step;
    /* 0x1 */ char album_type;
    /* 0x2 */ char photo_no_csr;    /* indexes disp_photo_no[], not the file */
    /* 0x3 */ char disp_photo_num;
    /* 0x4 */ char photo_flg;       /* the decompressor has been asked       */
} ALBUM_VIEW_CTRL;

typedef struct                      /* 0x3 */
{
    /* 0x0 */ char anim_step;
    /* 0x1 */ char anim_timer;
    /* 0x2 */ char csr_anim_timer;
} ALBUM_VIEW_DISP;

static ALBUM_VIEW_CTRL album_view_ctrl;                 /* sbss 3f4ad8 */
static ALBUM_VIEW_DISP album_view_disp;                 /* sbss 3f4ae0 */

/* bss 422140.  The picture-file slots worth showing, in wrap-around order from
 * the photo the edit view left on; the unused tail stays -1. */
static fixed_array<char, PHOTO_FILE_MAX> disp_photo_no;

/* ---- file-local helpers ------------------------------------------------- */

static int  AlbumViewTexLoadWait(void);
static void SetAlbumViewDispPhotoNo(void);
static void AlbumViewTopPad(void);
static void AlbumViewMoveEditReq(void);
static void AlbumViewDispInit(void);
static void AlbumViewBaseDisp(int album_type, int off_x, int off_y, u_char alpha);
static void AlbumViewFrameDisp(int off_x, int off_y, u_char alpha);
static void AlbumViewNoDisp(int album_type, int off_x, int off_y, u_char alpha);
static void AlbumViewAlbumTypeDisp(int album_type, int off_x, int off_y, u_char alpha);
static void AlbumViewPhotoDisp(int album_data, int photo_no, int off_x, int off_y, u_char alpha);
static void AlbumViewPhotoFrameDisp(int off_x, int off_y, u_char alpha);
static void AlbumViewLeftThumbnailDisp(int album_data, int photo_no, int off_x, int off_y, u_char alpha);
static void AlbumViewRightThumbnailDisp(int album_data, int photo_no, int off_x, int off_y, u_char alpha);
static void AlbumViewPhotoInfoDisp(int album_data, int photo_no, int off_x, int off_y, u_char alpha);
static void AlbumViewPhotoProtectionFrameDisp(int off_x, int off_y, u_char alpha);
static void AlbumViewCsrDisp(int off_x, int off_y, u_char alpha);
static void AlbumViewCaptionDisp(int off_x, int off_y, u_char alpha);

/* ==========================================================================
 *  Setup
 * ======================================================================== */

void AlbumViewCtrlInit(void)                                            /* 156 */
{
    album_view_ctrl.step           = ALBUM_VIEW_STEP_INIT;              /* 159 */
    album_view_ctrl.album_type     = (char)album_info[GetCurrentAlbum()].album_type; /* 160 */
    album_view_ctrl.photo_no_csr   = 0;                                 /* 161 */
    album_view_ctrl.disp_photo_num = 0;                                 /* 162 */
    album_view_ctrl.photo_flg      = 0;                                 /* 163 */

    SetAlbumViewDispPhotoNo();                                          /* 166 */
}

/* The background is one pak per album type, plus the language offset.  Claimed
 * and posted in two calls so the allocation is in place before the load. */
void AlbumViewBackGroundLoadReq(int album_type)                         /* 175 */
{
    GetAlbumTexMem(&album_view_tex_addr,
                   album_view_tex_tbl[album_type] + GetLanguage());     /* 179 */

    FileLoadReqEE(album_view_tex_tbl[album_type] + GetLanguage(),
                  album_view_tex_addr, 6, NULL, NULL);                  /* 182 */
}

static int AlbumViewTexLoadWait(void)                                   /* 192 */
{
    return (FileLoadIsEnd2(album_view_tex_tbl[album_view_ctrl.album_type] + GetLanguage(),
                           album_view_tex_addr) != 0);                  /* 200 */
}                                                                       /* 205 */

/* Build the list of slots worth showing.  It starts at the photo the edit view
 * left on and wraps, so disp_photo_no[0] is always that photo and the cursor
 * can start at 0; the tail keeps the -1 fill, which is what
 * AlbumViewDispMain() tests before drawing anything. */
static void SetAlbumViewDispPhotoNo(void)                               /* 211 */
{
    int  i;
    int  current_album;
    char photo_no;

    for (i = 0; i < PHOTO_FILE_MAX; i++) {                              /* 218 */
        disp_photo_no[i] = -1;                                          /* 219 */
    }                                                                   /* 220 */

    current_album = GetCurrentAlbum();                                  /* 222 */
    photo_no      = (char)GetAlbumPhotoNo();                            /* 223 */

    for (i = 0; i < PHOTO_FILE_MAX; i++) {                              /* 226 */

        if ((album_info[current_album].album_info.pic[photo_no].status & 1) != 0) { /* 228 */
            disp_photo_no[album_view_ctrl.disp_photo_num] = photo_no;   /* 231 */
            album_view_ctrl.disp_photo_num++;                           /* 232 */
        }

        photo_no = (char)((photo_no + 1) % PHOTO_FILE_MAX);             /* 234 */
    }                                                                   /* 235 */
}

/* ==========================================================================
 *  The machine
 * ======================================================================== */

int AlbumViewMain(void)                                                 /* 249 */
{
    int res;

    res = 0;                                                            /* 253 */

    switch (album_view_ctrl.step) {                                     /* 256 */

    case ALBUM_VIEW_STEP_INIT:
        AlbumViewDispInit();                                            /* 259 */

        album_view_ctrl.step = ALBUM_VIEW_STEP_LOAD_WAIT;               /* 261 */
        break;                                                          /* 262 */

    case ALBUM_VIEW_STEP_LOAD_WAIT:
        if (AlbumViewTexLoadWait() != 0) {                              /* 264 */
            album_view_ctrl.step = ALBUM_VIEW_STEP_TOP;                 /* 266 */
        }
        break;                                                          /* 267 */

    case ALBUM_VIEW_STEP_TOP:
        AlbumViewTopPad();                                              /* 270 */
        break;                                                          /* 271 */

    case ALBUM_VIEW_STEP_END:
        if (album_view_disp.anim_step == ZERO2_ANIM2D_STEP_END) {       /* 273 */
            LiberateAlbumViewTex();                                     /* 275 */

            /* Hand the cursor's photo back to album.c, so the edit view comes
             * up on the picture the player was looking at. */
            SetAlbumPhotoNo(disp_photo_no[album_view_ctrl.photo_no_csr]); /* 278 */

            res = 1;                                                    /* 280 */
        }
        break;                                                          /* 282 */

    default:
        PRINT_ASSERT("Error! %s", __FUNCTION__);                        /* 284 */
        break;
    }

    return res;                                                         /* 288 */
}

/* LEFT / RIGHT walk the list, TRIANGLE leaves.  Both walks are gated on the
 * album holding more than one photo -- not on disp_photo_num, which is what
 * the modulo below divides by. */
static void AlbumViewTopPad(void)                                       /* 294 */
{
    int current_album;

    current_album = GetCurrentAlbum();                                  /* 299 */

    if ((pad[0].rpt & 0x8000) || GetPadAnalogRpt(2)) {                  /* 303 */

        if (album_info[current_album].album_info.pic_num > 1) {         /* 305 */
            SystemBankPlay(0, 1, 0, 0, NULL, 0x3200, 0x1000);           /* 306 */

            album_view_ctrl.photo_no_csr =
                (char)((album_view_ctrl.photo_no_csr
                        + album_view_ctrl.disp_photo_num - 1)
                       % album_view_ctrl.disp_photo_num);               /* 307 */

            album_view_ctrl.photo_flg = 0;                              /* 309 */
        }
    } else if ((pad[0].rpt & 0x2000) || GetPadAnalogRpt(3)) {           /* 313 */

        if (album_info[current_album].album_info.pic_num > 1) {         /* 315 */
            SystemBankPlay(0, 1, 0, 0, NULL, 0x3200, 0x1000);           /* 316 */

            album_view_ctrl.photo_no_csr =
                (char)((album_view_ctrl.photo_no_csr + 1)
                       % album_view_ctrl.disp_photo_num);               /* 317 */

            album_view_ctrl.photo_flg = 0;                              /* 319 */
        }
    } else if (*paddat[1] == 1) {                                       /* 323 */
        SystemBankPlay(1, 1, 0, 0, NULL, 0x3200, 0x1000);               /* 324 */

        AlbumViewMoveEditReq();                                         /* 326 */
    }
}

/* Start the closing fade.  AlbumViewMain()'s step 3 is what waits for it. */
static void AlbumViewMoveEditReq(void)                                  /* 335 */
{
    album_view_ctrl.step = ALBUM_VIEW_STEP_END;                         /* 338 */

    album_view_disp.anim_step  = ZERO2_ANIM2D_STEP_OUT;                 /* 340 */
    album_view_disp.anim_timer = 0;                                     /* 341 */
}

void LiberateAlbumViewTex(void)                                         /* 353 */
{
    LiberateAlbumTexMem(&album_view_tex_addr);                          /* 356 */
}

void AlbumViewTexLoadCancel(void)                                       /* 364 */
{
    AlbumTexLoadCancel(album_view_tex_addr,
                       album_view_tex_tbl[album_view_ctrl.album_type] + GetLanguage()); /* 367 */
}

static void AlbumViewDispInit(void)                                     /* 379 */
{
    album_view_disp.anim_step      = ZERO2_ANIM2D_STEP_START;           /* 382 */
    album_view_disp.anim_timer     = 0;                                 /* 383 */
    album_view_disp.csr_anim_timer = 0;                                 /* 384 */
}

/* ==========================================================================
 *  Drawing
 * ======================================================================== */

/* The whole page.  Nothing is drawn outside steps 2 and 3, and nothing once
 * the closing fade has reached its end -- which is the frame AlbumViewMain()
 * hands control back on. */
void AlbumViewDispMain(void)                                            /* 392 */
{
    u_char alpha;
    int    current_album;
    char   before_photo_no;
    char   after_photo_no;
    void  *album_cmn_tex_addr;

    static const int num_tex_tbl[ALBUM_TYPE_MAX] =                      /* rdata 3a1340 */
    {
        49, 59, 69, 79, 89, 39, 99,
    };

    alpha = 128;                                                        /* 410 */

    current_album = GetCurrentAlbum();                                  /* 412 */

    /* 414 / 416 interpolated: the disp_photo_no[] subscript swallows both
     * statements' line notes into fixed_array.h 124/125. */
    before_photo_no = disp_photo_no[(album_view_ctrl.photo_no_csr
                                     + album_view_ctrl.disp_photo_num - 1)
                                    % album_view_ctrl.disp_photo_num];  /* 414 */
    after_photo_no  = disp_photo_no[(album_view_ctrl.photo_no_csr + 1)
                                    % album_view_ctrl.disp_photo_num];  /* 416 */

    album_cmn_tex_addr = GetAlbumCmnTexAddr();                          /* 418 */

    /* Steps 2 and 3 -- the ROM writes the pair as one unsigned range test. */
    if ((u_char)(album_view_ctrl.step - ALBUM_VIEW_STEP_TOP) < 2) {     /* 422 */
        AlbumInOutAnimCtrl(&album_view_disp.anim_step,
                           &album_view_disp.anim_timer, &alpha);        /* 423 */

        if (album_view_disp.anim_step != ZERO2_ANIM2D_STEP_END) {       /* 425 */
            PK2SendVram((uintptr_t)album_cmn_tex_addr, -1, -1, 0);      /* 426 */

            AlbumViewFrameDisp(0, 0, alpha);                            /* 429 */

            PK2SendVram((uintptr_t)album_view_tex_addr, -1, -1, 0);     /* 431 */

            AlbumViewBaseDisp(album_view_ctrl.album_type, 0, 0, alpha); /* 434 */

            if (disp_photo_no[album_view_ctrl.photo_no_csr] != -1) {    /* 436 */
                AlbumViewPhotoDisp(current_album,
                                   disp_photo_no[album_view_ctrl.photo_no_csr],
                                   0, 0, alpha);                        /* 438 */

                AlbumViewPhotoInfoDisp(current_album,
                                       disp_photo_no[album_view_ctrl.photo_no_csr],
                                       0, 0, alpha);                    /* 441 */
            }

            if (album_view_ctrl.disp_photo_num > 1) {                   /* 444 */
                AlbumViewLeftThumbnailDisp(current_album, before_photo_no, 0, 0, alpha); /* 446 */

                AlbumViewRightThumbnailDisp(current_album, after_photo_no, 0, 0, alpha); /* 449 */
            }

            AlbumViewNoDisp(album_view_ctrl.album_type, 0, 0, alpha);   /* 453 */

            AlbumViewAlbumTypeDisp(album_view_ctrl.album_type, 0, 0, alpha); /* 456 */

            PK2SendVram((uintptr_t)album_cmn_tex_addr, -1, -1, 0);      /* 458 */

            if (disp_photo_no[album_view_ctrl.photo_no_csr] != -1) {    /* 461 */
                DrawCmnNumberTex(disp_photo_no[album_view_ctrl.photo_no_csr] + 1, 2,
                                 &album_tex[num_tex_tbl[album_view_ctrl.album_type]],
                                 323, 81, alpha, 0, 0);                 /* 463 */
            }

            if (album_view_ctrl.disp_photo_num > 1) {                   /* 466 */
                AlbumViewCsrDisp(0, 0, alpha);                          /* 468 */

                DrawCmnNumberTex(before_photo_no + 1, 2,
                                 &album_tex[num_tex_tbl[album_view_ctrl.album_type]],
                                 94, 255, alpha, 0, 0);                 /* 472 */

                DrawCmnNumberTex(after_photo_no + 1, 2,
                                 &album_tex[num_tex_tbl[album_view_ctrl.album_type]],
                                 553, 255, alpha, 0, 0);                /* 476 */
            }

            AlbumViewCaptionDisp(0, 0, alpha);                          /* 480 */
        }
    }
}

/* The album's own backdrop -- a run of album_tex[] records whose first and last
 * index the type selects, out of the per-type pak. */
static void AlbumViewBaseDisp(int album_type, int off_x, int off_y, u_char alpha) /* 494 */
{
    DISP_SPRT base_ds;
    int       i;

    static const int frame_tex_tbl[ALBUM_TYPE_MAX][2] =                 /* rdata 3a1360 */
    {
        { 301, 308 },
        { 309, 316 },
        { 317, 324 },
        { 325, 332 },
        { 333, 340 },
        { 293, 300 },
        { 293, 300 },
    };

    for (i = frame_tex_tbl[album_type][0];
         i <= frame_tex_tbl[album_type][1]; i++) {                      /* 510 */
        CopySprDToSpr(&base_ds, &album_tex[i]);                         /* 511 */

        base_ds.x = base_ds.x + (float)off_x;                           /* 512 */
        base_ds.y = base_ds.y + (float)off_y;                           /* 512 */

        base_ds.alpha = (u_char)(((int)base_ds.alpha * (int)alpha) >> 7); /* 513 */

        DispSprD(&base_ds);                                             /* 514 */
    }                                                                   /* 515 */
}

/* The page furniture that is the same for every album type. */
static void AlbumViewFrameDisp(int off_x, int off_y, u_char alpha)      /* 526 */
{
    DISP_SPRT frame_ds;
    int       i;

    for (i = 0; i < ALBUM_TEX_VIEW_FRAME_NUM; i++) {                    /* 532 */
        CopySprDToSpr(&frame_ds, &album_tex[ALBUM_TEX_VIEW_FRAME + i]); /* 533 */

        frame_ds.x = frame_ds.x + (float)off_x;                         /* 534 */
        frame_ds.y = frame_ds.y + (float)off_y;                         /* 534 */

        frame_ds.alpha = (u_char)(((int)frame_ds.alpha * (int)alpha) >> 7); /* 535 */

        DispSprD(&frame_ds);                                            /* 536 */
    }                                                                   /* 537 */
}

/* The album's number plate: three pieces per type, out of the edit pak rather
 * than this page's own. */
static void AlbumViewNoDisp(int album_type, int off_x, int off_y, u_char alpha) /* 550 */
{
    DISP_SPRT type_ds;
    int       i;
    void     *tm2_addr;

    static const int type_tex_tbl[ALBUM_TYPE_MAX][3] =                  /* rdata 3a13a8 */
    {
        { 373, 379, 385 },
        { 374, 380, 386 },
        { 375, 381, 387 },
        { 376, 382, 388 },
        { 377, 383, 389 },
        { 372, 378, 384 },
        { 372, 378, 384 },
    };

    if ((u_int)album_type >= ALBUM_TYPE_MAX) {                          /* 567 */
        PRINT_ASSERT("Error! %s", __FUNCTION__);                        /* 568 */
    }

    tm2_addr = GetAlbumEditTexAddr();                                   /* 573 */

    PK2SendVram((uintptr_t)tm2_addr, -1, -1, 0);                        /* 575 */

    for (i = 0; i < 3; i++) {                                           /* 578 */
        CopySprDToSpr(&type_ds, &album_tex[type_tex_tbl[album_type][i]]); /* 579 */

        type_ds.x = type_ds.x + (float)off_x;                           /* 580 */
        type_ds.y = type_ds.y + (float)off_y;                           /* 580 */

        type_ds.alpha = (u_char)(((int)type_ds.alpha * (int)alpha) >> 7); /* 581 */

        DispSprD(&type_ds);                                             /* 582 */
    }                                                                   /* 583 */
}

/* The album's name plate.  Types 0..4 share the first two pieces and differ
 * only in the third; types 5 and 6 have one piece each, which is why the tails
 * of those rows are -1 and the loop tests before drawing. */
static void AlbumViewAlbumTypeDisp(int album_type, int off_x, int off_y, u_char alpha) /* 595 */
{
    DISP_SPRT type_ds;
    int       i;
    void     *tm2_addr;

    static const int type_tex_tbl[ALBUM_TYPE_MAX][3] =                  /* rdata 3a1418 */
    {
        { 364, 365, 366 },
        { 364, 365, 367 },
        { 364, 365, 368 },
        { 364, 365, 369 },
        { 364, 365, 370 },
        { 363,  -1,  -1 },
        { 371,  -1,  -1 },
    };

    if ((u_int)album_type >= ALBUM_TYPE_MAX) {                          /* 612 */
        PRINT_ASSERT("Error! %s", __FUNCTION__);                        /* 613 */
    }

    tm2_addr = GetAlbumEditTexAddr();                                   /* 618 */

    PK2SendVram((uintptr_t)tm2_addr, -1, -1, 0);                        /* 620 */

    for (i = 0; i < 3; i++) {                                           /* 623 */

        if (type_tex_tbl[album_type][i] != -1) {                        /* 624 */
            CopySprDToSpr(&type_ds, &album_tex[type_tex_tbl[album_type][i]]); /* 625 */

            type_ds.x = type_ds.x + (float)off_x;                       /* 626 */
            type_ds.y = type_ds.y + (float)off_y;                       /* 626 */

            type_ds.alpha = (u_char)(((int)type_ds.alpha * (int)alpha) >> 7); /* 627 */

            DispSprD(&type_ds);                                         /* 628 */
        }
    }                                                                   /* 630 */
}

/* The picture itself.  The first frame files a decompression request and sets
 * photo_flg; from then on DrawPhotoFromWorkAreaAddr() keeps feeding the
 * decompressor and draws the picture once it is whole. */
static void AlbumViewPhotoDisp(int album_data, int photo_no, int off_x, int off_y,
                               u_char alpha)                            /* 643 */
{
    void *album_cmn_tex_addr;

    album_cmn_tex_addr = GetAlbumCmnTexAddr();                          /* 647 */

    if (album_view_ctrl.photo_flg == 0) {                               /* 650 */

        if ((album_info[album_data].album_info.pic[photo_no].status & 1) != 0) { /* 652 */
            UncompressPhoto(album_info[album_data].album_info.pic[photo_no].adr_no); /* 654 */

            album_view_ctrl.photo_flg = 1;                              /* 656 */
        }
    }

    if (album_view_ctrl.photo_flg == 1) {                               /* 660 */
        DrawPhotoFromWorkAreaAddr((uintptr_t)GetAlbumDataAddr(album_data), 0, 1,
                                  ALBUM_VIEW_PHOTO_X, ALBUM_VIEW_PHOTO_Y,
                                  ALBUM_VIEW_PHOTO_W, ALBUM_VIEW_PHOTO_H,
                                  alpha);                               /* 664 */

        PK2SendVram((uintptr_t)album_cmn_tex_addr, -1, -1, 0);          /* 666 */

        AlbumViewPhotoFrameDisp(0, 0, alpha);                           /* 669 */

        if (album_info[album_data].album_type == ALBUM_TYPE_PROTECTABLE) { /* 671 */

            if ((album_info[album_data].album_info.pic[photo_no].status & 1) != 0) { /* 673 */

                if ((album_info[album_data].album_info.pic[photo_no].status & 2) != 0) { /* 675 */
                    AlbumViewPhotoProtectionFrameDisp(0, 0, alpha);     /* 678 */
                }
            }
        }
    }
}

/* The frame round the enlarged picture: six corners, two vertical column tiles
 * repeated three times each, and two horizontal row tiles repeated eight times
 * each -- the horizontals being the same art turned a quarter turn, which is
 * why they step by ds.h rather than ds.w. */
static void AlbumViewPhotoFrameDisp(int off_x, int off_y, u_char alpha) /* 693 */
{
    DISP_SPRT frame_ds;
    int       i;

    for (i = 0; i < ALBUM_VIEW_COL_NUM; i++) {                          /* 699 */
        CopySprDToSpr(&frame_ds, &album_tex[ALBUM_TEX_VIEW_COL_L]);     /* 700 */

        frame_ds.x = frame_ds.x + (float)off_x;                         /* 701 */
        frame_ds.y = frame_ds.y + (float)(frame_ds.h * i) + (float)off_y; /* 701 */

        frame_ds.alpha = (u_char)(((int)frame_ds.alpha * (int)alpha) >> 7); /* 702 */

        DispSprD(&frame_ds);                                            /* 703 */

        CopySprDToSpr(&frame_ds, &album_tex[ALBUM_TEX_VIEW_COL_R]);     /* 705 */

        frame_ds.x = frame_ds.x + (float)off_x;                         /* 706 */
        frame_ds.y = frame_ds.y + (float)(frame_ds.h * i) + (float)off_y; /* 706 */

        frame_ds.alpha = (u_char)(((int)frame_ds.alpha * (int)alpha) >> 7); /* 707 */

        DispSprD(&frame_ds);                                            /* 708 */
    }                                                                   /* 709 */

    for (i = 0; i < ALBUM_VIEW_ROW_NUM; i++) {                          /* 711 */
        CopySprDToSpr(&frame_ds, &album_tex[ALBUM_TEX_VIEW_ROW_T]);     /* 712 */

        frame_ds.x = frame_ds.x + (float)frame_ds.h + (float)(frame_ds.h * i) + (float)off_x; /* 713 */
        frame_ds.y = frame_ds.y + (float)off_y;                         /* 713 */

        frame_ds.crx = frame_ds.x;  frame_ds.cry = frame_ds.y;  frame_ds.rot = 90.0f; /* 714 */

        frame_ds.alpha = (u_char)(((int)frame_ds.alpha * (int)alpha) >> 7); /* 715 */

        DispSprD(&frame_ds);                                            /* 716 */

        CopySprDToSpr(&frame_ds, &album_tex[ALBUM_TEX_VIEW_ROW_B]);     /* 718 */

        frame_ds.x = frame_ds.x + (float)frame_ds.h + (float)(frame_ds.h * i) + (float)off_x; /* 719 */
        frame_ds.y = frame_ds.y + (float)off_y;                         /* 719 */

        frame_ds.crx = frame_ds.x;  frame_ds.cry = frame_ds.y;  frame_ds.rot = 90.0f; /* 720 */

        frame_ds.alpha = (u_char)(((int)frame_ds.alpha * (int)alpha) >> 7); /* 721 */

        DispSprD(&frame_ds);                                            /* 722 */
    }                                                                   /* 723 */

    CopySprDToSpr(&frame_ds, &album_tex[ALBUM_TEX_VIEW_ROW_T_CAP]);     /* 725 */

    frame_ds.x = frame_ds.x + (float)frame_ds.h + (float)off_x;         /* 726 */
    frame_ds.y = frame_ds.y + (float)off_y;                             /* 726 */

    frame_ds.crx = frame_ds.x;  frame_ds.cry = frame_ds.y;  frame_ds.rot = 90.0f; /* 727 */

    frame_ds.alpha = (u_char)(((int)frame_ds.alpha * (int)alpha) >> 7); /* 728 */

    DispSprD(&frame_ds);                                                /* 729 */

    CopySprDToSpr(&frame_ds, &album_tex[ALBUM_TEX_VIEW_ROW_B_CAP]);     /* 731 */

    frame_ds.x = frame_ds.x + (float)frame_ds.h + (float)off_x;         /* 732 */
    frame_ds.y = frame_ds.y + (float)off_y;                             /* 732 */

    frame_ds.crx = frame_ds.x;  frame_ds.cry = frame_ds.y;  frame_ds.rot = 90.0f; /* 733 */

    frame_ds.alpha = (u_char)(((int)frame_ds.alpha * (int)alpha) >> 7); /* 734 */

    DispSprD(&frame_ds);                                                /* 735 */

    for (i = 0; i < ALBUM_TEX_VIEW_PHOTO_FRAME_NUM; i++) {              /* 737 */
        CopySprDToSpr(&frame_ds, &album_tex[ALBUM_TEX_VIEW_PHOTO_FRAME + i]); /* 738 */

        frame_ds.x = frame_ds.x + (float)off_x;                         /* 739 */
        frame_ds.y = frame_ds.y + (float)off_y;                         /* 739 */

        frame_ds.alpha = (u_char)(((int)frame_ds.alpha * (int)alpha) >> 7); /* 740 */

        DispSprD(&frame_ds);                                            /* 741 */
    }                                                                   /* 742 */
}

/* The previous photo, small, on the left.  Drawn straight out of the album's
 * thumbnail area rather than the decompressor, so it costs nothing to keep
 * both neighbours on screen. */
static void AlbumViewLeftThumbnailDisp(int album_data, int photo_no, int off_x,
                                       int off_y, u_char alpha)         /* 755 */
{
    DISP_SPRT frame_ds;
    int       i;

    static const int protect_x_tbl[ALBUM_VIEW_PROTECT_NUM] = {  62,  62, 104, 104 }; /* rdata 3a1470 */
    static const int protect_y_tbl[ALBUM_VIEW_PROTECT_NUM] = { 208, 234, 208, 234 }; /* rdata 3a1480 */

    if ((album_info[album_data].album_info.pic[photo_no].status & 1) != 0) { /* 772 */
        DrawSPhotoFromSmallPhotoAreaAddr2((uintptr_t)GetAlbumDataAddr(album_data),
                                          album_info[album_data].album_info.pic[photo_no].adr_no,
                                          0, 0,
                                          off_x + ALBUM_VIEW_THUMB_L_X,
                                          off_y + ALBUM_VIEW_THUMB_Y,
                                          ALBUM_VIEW_THUMB_W, ALBUM_VIEW_THUMB_H,
                                          alpha);                       /* 775 */

        for (i = 0; i < ALBUM_TEX_VIEW_THUMB_NUM; i++) {                /* 778 */
            CopySprDToSpr(&frame_ds, &album_tex[ALBUM_TEX_VIEW_THUMB_L + i]); /* 779 */

            frame_ds.x = frame_ds.x + (float)off_x;                     /* 780 */
            frame_ds.y = frame_ds.y + (float)off_y;                     /* 780 */

            frame_ds.alpha = (u_char)(((int)frame_ds.alpha * (int)alpha) >> 7); /* 781 */

            DispSprD(&frame_ds);                                        /* 782 */
        }                                                               /* 783 */

        if (album_info[album_data].album_type == ALBUM_TYPE_PROTECTABLE) { /* 786 */

            if ((album_info[album_data].album_info.pic[photo_no].status & 2) != 0) { /* 788 */

                for (i = 0; i < ALBUM_VIEW_PROTECT_NUM; i++) {          /* 790 */
                    CopySprDToSpr(&frame_ds, &album_tex[ALBUM_TEX_THUMB_PROTECT + i]); /* 791 */

                    frame_ds.x = (float)(protect_x_tbl[i] + off_x);     /* 792 */
                    frame_ds.y = (float)(protect_y_tbl[i] + off_y);     /* 792 */

                    frame_ds.alpha = (u_char)(((int)frame_ds.alpha * (int)alpha) >> 7); /* 793 */

                    DispSprD(&frame_ds);                                /* 794 */
                }                                                       /* 795 */
            }
        }
    }
}

/* The next photo, small, on the right.  The same function with the other
 * thumbnail frame and the other protection-badge coordinates. */
static void AlbumViewRightThumbnailDisp(int album_data, int photo_no, int off_x,
                                        int off_y, u_char alpha)        /* 811 */
{
    DISP_SPRT frame_ds;
    int       i;

    static const int protect_x_tbl[ALBUM_VIEW_PROTECT_NUM] = { 520, 520, 562, 562 }; /* rdata 3a1490 */
    static const int protect_y_tbl[ALBUM_VIEW_PROTECT_NUM] = { 208, 234, 208, 234 }; /* rdata 3a14a0 */

    if ((album_info[album_data].album_info.pic[photo_no].status & 1) != 0) { /* 828 */
        DrawSPhotoFromSmallPhotoAreaAddr2((uintptr_t)GetAlbumDataAddr(album_data),
                                          album_info[album_data].album_info.pic[photo_no].adr_no,
                                          0, 0,
                                          off_x + ALBUM_VIEW_THUMB_R_X,
                                          off_y + ALBUM_VIEW_THUMB_Y,
                                          ALBUM_VIEW_THUMB_W, ALBUM_VIEW_THUMB_H,
                                          alpha);                       /* 831 */

        for (i = 0; i < ALBUM_TEX_VIEW_THUMB_NUM; i++) {                /* 833 */
            CopySprDToSpr(&frame_ds, &album_tex[ALBUM_TEX_VIEW_THUMB_R + i]); /* 834 */

            frame_ds.x = frame_ds.x + (float)off_x;                     /* 835 */
            frame_ds.y = frame_ds.y + (float)off_y;                     /* 835 */

            frame_ds.alpha = (u_char)(((int)frame_ds.alpha * (int)alpha) >> 7); /* 836 */

            DispSprD(&frame_ds);                                        /* 837 */
        }                                                               /* 838 */

        if (album_info[album_data].album_type == ALBUM_TYPE_PROTECTABLE) { /* 841 */

            if ((album_info[album_data].album_info.pic[photo_no].status & 2) != 0) { /* 843 */

                for (i = 0; i < ALBUM_VIEW_PROTECT_NUM; i++) {          /* 845 */
                    CopySprDToSpr(&frame_ds, &album_tex[ALBUM_TEX_THUMB_PROTECT + i]); /* 846 */

                    frame_ds.x = (float)(protect_x_tbl[i] + off_x);     /* 847 */
                    frame_ds.y = (float)(protect_y_tbl[i] + off_y);     /* 847 */

                    frame_ds.alpha = (u_char)(((int)frame_ds.alpha * (int)alpha) >> 7); /* 848 */

                    DispSprD(&frame_ds);                                /* 849 */
                }                                                       /* 850 */
            }
        }
    }
}

/* Everything printed beside the picture: the room name, the date and time, the
 * score, and up to three subject lines.  The subject block is centred
 * vertically on the count, which is what the first loop is for. */
static void AlbumViewPhotoInfoDisp(int album_data, int photo_no, int off_x,
                                   int off_y, u_char alpha)             /* 866 */
{
    DATE_INFO date;
    int       i;
    int       subject_num;
    int       msg_y;

    subject_num = 0;                                                    /* 890 */

    if ((album_info[album_data].album_info.pic[photo_no].status & 1) != 0) { /* 896 */
        SetDateInfoType(&date, &album_info[album_data].album_info.pic[photo_no].time); /* 899 */

        if ((u_short)album_info[album_data].album_info.pic[photo_no].room
            < ALBUM_VIEW_ROOM_MSG_MAX) {                                /* 902 */
            PrintMsg(0x4a, album_info[album_data].album_info.pic[photo_no].room,
                     off_x + 80, off_y + ALBUM_VIEW_ROOM_Y, 2, alpha, 0); /* 904 */
        }

        PrintNumber_N(date.day.year, 2, off_x + 166, off_y + ALBUM_VIEW_DATE_Y, 2, alpha, 0, 1, 1); /* 909 */
        PrintMsg(8, 1, off_x + 110, off_y + ALBUM_VIEW_DATE_Y, 2, alpha, 0xa0); /* 911 */
        PrintNumber_N(date.day.month, 2, off_x + 124, off_y + ALBUM_VIEW_DATE_Y, 2, alpha, 0, 1, 1); /* 913 */
        PrintMsg(8, 1, off_x + 152, off_y + ALBUM_VIEW_DATE_Y, 2, alpha, 0xa0); /* 915 */
        PrintNumber_N(date.day.day, 2, off_x + 82, off_y + ALBUM_VIEW_DATE_Y, 2, alpha, 0, 1, 1); /* 917 */

        PrintNumber_N(date.time.hour, 2, off_x + 204, off_y + ALBUM_VIEW_DATE_Y, 2, alpha, 0, 1, 1); /* 920 */
        PrintMsg(8, 0, off_x + 233, off_y + ALBUM_VIEW_DATE_Y, 2, alpha, 0xa0); /* 922 */
        PrintNumber_N(date.time.min, 2, off_x + 244, off_y + ALBUM_VIEW_DATE_Y, 2, alpha, 0, 1, 1); /* 924 */
        PrintMsg(8, 0, off_x + 273, off_y + ALBUM_VIEW_DATE_Y, 2, alpha, 0xa0); /* 926 */
        PrintNumber_N(date.time.sec, 2, off_x + 284, off_y + ALBUM_VIEW_DATE_Y, 2, alpha, 0, 1, 1); /* 928 */

        PrintNumber_N((int)album_info[album_data].album_info.pic[photo_no].score, 5,
                      off_x + 182, off_y + ALBUM_VIEW_SCORE_Y, 2, alpha, 0, 0, 0); /* 932 */
        PrintMsg(8, 2, off_x + 274, off_y + ALBUM_VIEW_SCORE_Y, 2, alpha, 0); /* 935 */

        for (i = 0; i < PICTURE_SUBJECT_MAX; i++) {                     /* 938 */

            if (album_info[album_data].album_info.pic[photo_no].maSubject[i].type < 0) { /* 940 */
                break;
            }
            subject_num++;                                              /* 943 */
        }                                                               /* 945 */

        /* GCC builds a decision tree round the pivot 2, which is why the
         * compare against it appears twice; case 3 and the default share their
         * value.  The arms are attributed from that tree rather than measured
         * one by one, so the line numbers are approximate. */
        switch (subject_num) {                                          /* 947 */
        case 1:
            msg_y = 371;                                                /* 950 */
            break;
        case 2:
            msg_y = 359;                                                /* 953 */
            break;
        case 3:
            msg_y = 347;                                                /* 955 */
            break;
        default:
            msg_y = 347;                                                /* 956 */
            break;
        }

        msg_y = msg_y + off_y;

        for (i = 0; i < PICTURE_SUBJECT_MAX; i++) {                     /* 961 */

            if (album_info[album_data].album_info.pic[photo_no].maSubject[i].type < 0) {
                return;                                                 /* 963 */
            }

            if (album_info[album_data].album_info.pic[photo_no].maSubject[i].obj_no < 0 ||
                album_info[album_data].album_info.pic[photo_no].maSubject[i].obj_no >=
                    GetMsgIDNumMax(album_info[album_data].album_info.pic[photo_no].maSubject[i].type)) {
                PRINT_ASSERT("Error! %s msg_id %d", __FUNCTION__,
                             album_info[album_data].album_info.pic[photo_no].maSubject[i].obj_no); /* 969 */
            }

            PrintMsg(album_info[album_data].album_info.pic[photo_no].maSubject[i].type,
                     album_info[album_data].album_info.pic[photo_no].maSubject[i].obj_no,
                     off_x + ALBUM_VIEW_SUBJECT_X, msg_y, 2, alpha, 0);

            msg_y = msg_y + ALBUM_VIEW_SUBJECT_PITCH;                   /* 975 */
        }                                                               /* 976 */
    }
}

/* The badge over a protected picture. */
static void AlbumViewPhotoProtectionFrameDisp(int off_x, int off_y, u_char alpha) /* 988 */
{
    DISP_SPRT photo_ds;
    int       i;

    for (i = 0; i < ALBUM_VIEW_PROTECT_NUM; i++) {                      /* 994 */
        CopySprDToSpr(&photo_ds, &album_tex[ALBUM_TEX_PHOTO_PROTECT + i]); /* 995 */

        photo_ds.x = photo_ds.x + (float)off_x;                         /* 996 */
        photo_ds.y = photo_ds.y + (float)off_y;                         /* 996 */

        photo_ds.alpha = (u_char)(((int)photo_ds.alpha * (int)alpha) >> 7); /* 997 */

        DispSprD(&photo_ds);                                            /* 998 */
    }                                                                   /* 999 */
}

/* The two left / right arrows, pulsing on the shared 45-frame cursor ramp. */
static void AlbumViewCsrDisp(int off_x, int off_y, u_char alpha)        /* 1010 */
{
    DISP_SPRT csr_ds;
    u_char    rgb;
    int       i;

    Zero2Anim2D_CsrAnimCtrl(&album_view_disp.csr_anim_timer, &rgb);     /* 1017 */

    for (i = 0; i < ALBUM_TEX_VIEW_CSR_NUM; i++) {                      /* 1020 */
        CopySprDToSpr(&csr_ds, &album_tex[ALBUM_TEX_VIEW_CSR + i]);     /* 1021 */

        csr_ds.x = csr_ds.x + (float)off_x;                             /* 1022 */
        csr_ds.y = csr_ds.y + (float)off_y;                             /* 1022 */

        csr_ds.alpha = (u_char)(((int)csr_ds.alpha * (int)alpha) >> 7); /* 1023 */
        csr_ds.r = rgb;  csr_ds.g = rgb;  csr_ds.b = rgb;               /* 1024 */

        DispSprD(&csr_ds);                                              /* 1025 */
    }                                                                   /* 1026 */
}

/* The button caption strip.  Both offsets are ignored. */
static void AlbumViewCaptionDisp(int off_x, int off_y, u_char alpha)    /* 1037 */
{
    DrawCmnCapGroup_W(12, 12, alpha, 0);                                /* 1040 */
}
