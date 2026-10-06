/* ==========================================================================
 *  ingame/photo/photo.h
 *
 *  The photo phase and the photo album.
 *
 *  Two halves.  The phase side is the twelve-step sequence that runs after the
 *  shutter fires -- PhotoMain() dispatches photo_wrk.mode into the Picture*
 *  handlers, and one_Story_Photo() is the GPhase entry that drives it.  The
 *  album side is pfile_wrk: sixteen PICTURE_WRK slots plus the six sort orders
 *  the album menu offers.
 *
 *  Reconstructed from the Feb 6 2004 prototype (SLES_523.84), photo.o.
 * ======================================================================== */

#ifndef _INGAME_PHOTO_PHOTO_H
#define _INGAME_PHOTO_PHOTO_H

#include "../../main/phasefunc.h"
#include "../../sdk/scetypes.h"
#include "../../sdk/libcdvd.h"          /* sceCdCLOCK (PICTURE_WRK::time)     */
#include "../../graphics/graph3d/ctl/fixed_array.h"

/* The photo_dat* / GetPhotoDatNum entry points used to be stubbed here.  They
 * belong to photo_dat.o and now live in photo_dat.c; the include keeps the
 * existing users (ev_open.c, ev_phase.c, ingame.c) compiling unchanged. */
#include "photo_dat.h"

/* Album capacity, and the per-shot hint-subject cap.  The ROM names the second
 * one itself, in PhotoWrkInit()'s assert text. */
#define PHOTO_FILE_MAX          16
#define HINT_PHOTO_REQ_MAX      5

/* How many named subjects a filed picture carries -- the bound on
 * PICTURE_WRK::maSubject, and the loop bound album_view.c walks it with. */
#define PICTURE_SUBJECT_MAX     3

/* photo_wrk.sta bits.  0x2 is up while a hint texture load is in flight (it is
 * cleared once the hint has been shown); 0x10 says the shot was of a scenery
 * object, whose "hint" is the 3D world itself rather than a loaded texture --
 * FurnPhotoFlgUp() raises it from the player code and every Picture* handler
 * branches on it. */
#define PHOTO_STA_HINT_LOAD     0x2
#define PHOTO_STA_FURN          0x10

/* What kind of picture the shot turned out to be.  PhotoWrkInit() stores it on
 * PHOTO_WRK and the photo phase branches on it for the "you got something"
 * beat -- INVALID is the shot that hit nothing but an invincible ghost. */
enum PHOTO_TYPE
{
    PHOTO_TYPE_HINT       = 0,
    PHOTO_TYPE_HINT3D     = 1,
    PHOTO_TYPE_RARE       = 2,
    PHOTO_TYPE_MAYU_CURSE = 3,
    PHOTO_TYPE_INVALID    = 4
};

/* One hint subject the shot captured, by photo_dat[] index; -1 for "none".
 * PlayerTakePictJob() fills up to five per shot. */
struct HINT_PHOTO_REQ               /* 0x4 */
{
    /* 0x0 */ int no;
};

/* One thing in frame, as the album records it.  `type` 0x3b is a ghost (the
 * ghost-list message group), 0xffff "nothing nameable"; `no` and `sp_no` are
 * ghost-list slots, already multiplied by three. */
struct SUBJECT_WRK                  /* 0x6 */
{
    /* 0x0 */ u_short sp_no;
    /* 0x2 */ u_short no;
    /* 0x4 */ short   type;
};

/* Everything the photo phase needs about the shot just taken.  Built on the
 * stack by PlayerTakePictJob() and handed to PhotoWrkInit() once. */
struct PHOTO_WRK_DEF                /* 0x40 */
{
    /* 0x00 */ float          pos[4];
    /* 0x10 */ HINT_PHOTO_REQ hint_pict[HINT_PHOTO_REQ_MAX];
    /* 0x24 */ int            hint_cnt;
    /* 0x28 */ int            adr_no;
    /* 0x2c */ PHOTO_TYPE     type;
    /* 0x30 */ int            msg_name;
    /* 0x34 */ int            msg_type;
    /* 0x38 */ int            unlock_ghost;
    /* On the EE the leading float[4] gives the whole struct quadword
     * alignment, which rounds it up to 0x40; the host aligns it to 4 and would
     * stop at 0x3c.  Spelled out so sizeof matches the ROM's. */
    /* 0x3c */ int            pad;
};

struct PHOTO_WRK                    /* 0xc */
{
    /* 0x0 */ int   mode;
    /* 0x4 */ u_int sta;
    /* 0x8 */ u_char adr_no;
    /* 0x9 */ u_char cnt;
    /* 0xa:0 */ unsigned char furn_flg : 1;
    /* 0xa:1 */ unsigned char bGradual : 1;
    /* 0xa:2 */ unsigned char b3DDraw  : 1;
    /* 0xa:3 */ unsigned char bRareEne : 1;
};

/* --------------------------------------------------------------------------
 *  The album.
 * ------------------------------------------------------------------------ */

/* One named thing in a filed picture.  This is SUBJECT_WRK after the album has
 * taken it: sp_no is dropped and `no` is renamed. */
struct PICTURE_SUBJECT              /* 0x4 */
{
    /* 0x0 */ short type;
    /* 0x2 */ short obj_no;
};

/* One filed picture.  `adr_no` is the photo-data slot the image bytes live in
 * and travels with the record through every sort, so the album can reorder the
 * list without moving a single texture.  `status` bit 0 is "in use", bit 1 is
 * "protected"; a zeroed status is a free slot. */
typedef struct                      /* 0x20 */
{
    /* 0x00 */ u_char adr_no;
    /* 0x01 */ u_char chp_no;
    /* 0x02 */ u_char status;
    /* 0x03 */ u_char pad;
    /* 0x04 */ fixed_array<PICTURE_SUBJECT, PICTURE_SUBJECT_MAX> maSubject;
    /* 0x10 */ u_int  score;
    /* 0x14 */ sceCdCLOCK time;
    /* 0x1c */ short  room;
    /* 0x1e */ u_char pad2[2];
} PICTURE_WRK;

/* The album itself.  `sort_key` / `skey_bak` are the menu's current and
 * previous sort selection; the module never reads them -- menu_photo.o does. */
typedef struct                      /* 0x208 */
{
    /* 0x000 */ u_char pic_num;
    /* 0x001 */ u_char protect_num;
    /* 0x002 */ u_char sort_key;
    /* 0x003 */ u_char skey_bak;
    /* 0x004 */ fixed_array<PICTURE_WRK, PHOTO_FILE_MAX> pic;
    /* 0x204 */ u_int  padding;
} PFILE_WRK;

extern PHOTO_WRK photo_wrk;         /* data 33c420 */
extern PFILE_WRK pfile_wrk;         /* data 33c218 */

/* ---- the GPhase entry points ------------------------------------------- */
void       init_Story_Photo(void);
void       end_Story_Photo(void);
GPHASE_ENUM one_Story_Photo(GPHASE_ENUM dummy);

/* ---- the phase itself --------------------------------------------------- *
 * PhotoMain() returns non-zero on the frame the sequence finishes.  The
 * Picture* handlers are exported but are only ever reached through it. */
int  PhotoMain(void);
void PicturePre1(void);
void PictureInitSub(void);
void PicturePre3(void);
void PicturePre4(void);
void PictureDisp(void);
void PictureHint1(void);
void PictureHint2(void);
void PictureHint3(void);
void PictureToUnlockGhost(void);
void PictureUnlockGhost(void);
void PictureCapture(void);
void PhotoDebug(void);

/* ---- the album side ----------------------------------------------------- *
 * PlayerTakePictJob() runs PreInit / GetSavePhotoNo / AddPhotoData / Init once
 * per shot: PreInit clears the last shot's working state, GetSavePhotoNo hands
 * out the slot the image will be written to, AddPhotoData files the score and
 * the subject list against it, and Init commits the finished PHOTO_WRK_DEF for
 * the photo phase to show. */
void InitPhotoWrk(void);
void PhotoWrkPreInit(void);
void PhotoWrkInit(const PHOTO_WRK_DEF *pDef);
int  GetSavePhotoNo(void);
int  AddPhotoData(int adr_no, int score, int room_no, int chapter_no,
                  sceCdCLOCK rtc, SUBJECT_WRK *obj, int obj_num);
void DeletePhotoData(u_char no);

void         CopyPFileWrk(PFILE_WRK *copy_wrk);
PFILE_WRK   *GetCamPhotoFile(void);
PICTURE_WRK *GetPhotoData(u_char no);
int          GetFilePhotoState(u_char no);
int          GetFilePhotoAdrNo(u_char no);
int          GetFilePhotoNum(void);
int          SetFilePhotoProtect(u_char no);
void         DelFilePhotoProtect(u_char no);

/* The album menu's six orderings.  Every one begins with a _Before() pass, so
 * they all start from a list with the free slots packed at the end. */
void SortPhotoData_Before(PFILE_WRK *photo_file);
void SortPhotoData_Protect(PFILE_WRK *photo_file);
void SortPhotoData_NonProtect(PFILE_WRK *photo_file);
void SortPhotoData_NewTime(PFILE_WRK *photo_file);
void SortPhotoData_OldTime(PFILE_WRK *photo_file);
void SortPhotoData_BigScore(PFILE_WRK *photo_file);
void SortPhotoData_SmallScore(PFILE_WRK *photo_file);

/* ---- the scenery-photo flag -------------------------------------------- *
 * PhotoFlgIsUp() is the draw-side test: MapObj / MapPut swap in the objects
 * that only exist inside a photograph while it is up.  FurnPhotoFlg* is the
 * separate "this shot was of scenery" bit on photo_wrk.sta. */
int  PhotoFlgIsUp(void);
void FurnPhotoFlgUp(void);
int  FurnPhotoFlgIsUp(void);

#endif /* _INGAME_PHOTO_PHOTO_H */
