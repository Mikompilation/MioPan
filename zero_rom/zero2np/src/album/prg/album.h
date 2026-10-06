/* ==========================================================================
 *  album/prg/album.h
 *
 *  The photo album's parent module (album.o).  It owns the two resident
 *  albums, the memory every page draws out of, and the two-level step machine
 *  that hands each frame to one of the two pages -- album_edit.o's two-album
 *  edit view or album_view.o's photo viewer.
 *
 *  Every screen that offers an album drives the same four calls:
 *  AlbumBackGroundLoadReq() once when the screen's assets are claimed,
 *  AlbumInit() on entry, AlbumMain() / AlbumDispMain() every frame, and
 *  AlbumEnd() when the screen's assets go back.
 *
 *  Reconstructed from the Feb 6 2004 prototype (SLES_523.84).
 * ======================================================================== */

#ifndef _ALBUM_PRG_ALBUM_H
#define _ALBUM_PRG_ALBUM_H

#include "../../common/save_data.h"          /* MC_SAVE_DATA */
#include "../../graphics/graph3d/ctl/fixed_array.h"
#include "../../ingame/photo/photo.h"        /* PFILE_WRK    */

/* One album's picture pages: sixteen slots of compressed image data, which is
 * why an album gets a memory-card directory of its own rather than sharing the
 * game-data one. */
#define ALBUM_PHOTO_DATA_SIZE   0xe8000

/* One album's contents: which kind of album it is, then the sixteen-slot
 * picture file that backs it. */
typedef struct                              /* 0x20c */
{
    /* 0x000 */ int       album_type;
    /* 0x004 */ PFILE_WRK album_info;
} ALBUM_INFO;

/* Two of them resident at a time -- the one being viewed and the one being
 * copied to. */
extern fixed_array<ALBUM_INFO, 2> album_info;    /* data 2d4370 */

/* ---- the phase machine -------------------------------------------------- *
 * AlbumInit()'s mode is 0 for the ingame album (album A is seeded with a copy
 * of the camera's own photo file) and 1 for the outgame one reached from the
 * title screen.  AlbumMain() returns non-zero exactly once, on the frame the
 * closing fade started by AlbumOutReq() reaches ZERO2_ANIM2D_STEP_END. */

void AlbumInit(int init_mode);                          /* 0x11c318 */
int  AlbumMain(void);                                   /* 0x11c830 */
void AlbumDispMain(void);                               /* 0x11cbf0 */
void AlbumEnd(void);                                    /* 0x11ca78 */

/* ---- memory and textures ------------------------------------------------ *
 * mem_get / mem_free are the outgame heap's, handed over once; every album
 * allocation runs through them.  Both Get* routines free whatever the slot
 * already held, so a re-request is safe. */

void AlbumBackGroundLoadReq(void *(*mem_get)(int), void (*mem_free)(void *));
void GetAlbumTexMem(void **tex_addr, int data_label);   /* 0x11c720 */
void LiberateAlbumTexMem(void **tex_addr);              /* 0x11cb40 */
void AlbumTexLoadCancel(void *tex_addr, int data_label);/* 0x11cb80 */

void *GetAlbumOutGameTexAddr(void);                     /* 0x11ca50 */
void *GetAlbumCmnTexAddr(void);                         /* 0x11ca58 */
void *GetAlbumEditTexAddr(void);                        /* 0x11ca60 */
void *GetAlbumSaveLoadTexAddr(void);                    /* 0x11ca68 */
void *GetAlbumSlotSelTexAddr(void);                     /* 0x11ca70 */

/* ---- the album's current selection -------------------------------------- */

int   GetCurrentAlbum(void);                            /* 0x11ca18 */
void  ChengeCurrentAlbum(void);                         /* 0x11c9e0 -- ROM spelling */
int   GetAlbumPhotoNo(void);                            /* 0x11ca28 */
void  SetAlbumPhotoNo(char photo_no);                   /* 0x11c9f8 */

/* Album A's picture pages are the ingame photo area at PHOTO_DATA_ADDR (a raw
 * EE address); album B's are the block claimed out of the caller's heap (a
 * host pointer).  Both are only ever handed to photo_make.c entry points that
 * resolve them with MioPan_GetHostPointer(), which is idempotent -- so the
 * mixture is safe, but the parameter carrying one must be uintptr_t and not
 * int. */
void *GetAlbumDataAddr(int album_data_label);           /* 0x11ca38 */

/* Ask the album to close, and the two flags its parent screens set. */
void  AlbumOutReq(void);                                /* 0x11c9c0 */
void  SetAlbumSaveDataAddr(void *data_addr);            /* 0x11ca08 */
void  SetAlbumTitleFlg(char flg);                       /* 0x11ca10 */

/* ---- save blocks (system/mc/dat/save_data.c's save_album_data[]) -------- */

/* The currently-selected album's info block. */
void SetSave_AlbumInfoData(MC_SAVE_DATA *data);  /* 0x11cd00 */

/* Its picture pages: ALBUM_PHOTO_DATA_SIZE bytes. */
void SetSave_AlbumData(MC_SAVE_DATA *data);      /* 0x11cce8 */

#endif /* _ALBUM_PRG_ALBUM_H */
