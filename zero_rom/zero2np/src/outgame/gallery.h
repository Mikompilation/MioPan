/* ==========================================================================
 *  outgame/gallery.h
 *
 *  The gallery ("omake") screen: three picture sets, two ending movies and
 *  the mission/soul-list unlocks that gate them.  gallery.c owns the state
 *  machine and the loads; gallery_disp.c draws it.
 *
 *  Reconstructed from the Feb 6 2004 prototype (SLES_523.84).
 * ======================================================================== */

#ifndef _OUTGAME_GALLERY_H
#define _OUTGAME_GALLERY_H

/* GAL_CTRL::now_place / next_place -- indices into GalleryCtrlModule[] and
 * GalleryDispModule[].  3 is not a module: it is the exit. */
#define GAL_PLACE_TOP       0
#define GAL_PLACE_VIEW      1
#define GAL_PLACE_MOVIE     2
#define GAL_PLACE_END       3

/* GAL_CTRL::anm_step */
#define GAL_ANM_FADE_IN     1
#define GAL_ANM_FADE_OUT    2

/* GAL_CTRL::pic_step */
#define GAL_PIC_LOAD_REQ    0
#define GAL_PIC_LOAD_WAIT   1
#define GAL_PIC_SHOWN       2
#define GAL_PIC_FADE_IN     3
#define GAL_PIC_FADE_OUT    4

/* Number of menu rows on the top page. */
#define GAL_CSR_NUM         8

/* Which texture GalPK2SendVram() last pushed; 5 is "none of them". */
#define GAL_TEX_NONE        5
#define GAL_TEX_OG          0
#define GAL_TEX_TOP         1
#define GAL_TEX_CMN         2
#define GAL_TEX_VIEW        3
#define GAL_TEX_PIC         4

typedef struct                      /* 0x78 */
{
    /* 0x00 */ int setup_pic_flg;   /* soul list complete -> setup pictures  */
    /* 0x04 */ int game_clear_flg;
    /* 0x08 */ int ending1_mov_flg;
    /* 0x0c */ int ending2_mov_flg;
    /* 0x10 */ int movie_no;
    /* 0x14 */ int end1_mov_cnt;    /* ending 1 is two movies back to back   */
    /* 0x18 */ int main_step;
    /* 0x1c */ int now_place;
    /* 0x20 */ int next_place;
    /* 0x24 */ int now_tex;
    /* 0x28 */ int csr_map[GAL_CSR_NUM];  /* per-row "unlocked" flags        */
    /* 0x48 */ int cursor;
    /* 0x4c */ int next_csr;
    /* 0x50 */ int old_csr;
    /* 0x54 */ int anm_step;
    /* 0x58 */ int anm_alpha;
    /* 0x5c */ int pic_mode;        /* which picture set, 0..2               */
    /* 0x60 */ int pic_step;
    /* 0x64 */ int pic_no;
    /* 0x68 */ int next_pic_no;
    /* 0x6c */ int pic_max;
    /* 0x70 */ int file_no;         /* cd label of the picture being loaded  */
    /* 0x74 */ int pic_anm_alpha;
} GAL_CTRL;

extern GAL_CTRL  gal_ctrl;          /* data  315690 */
extern GAL_CTRL *gc;                /* sdata 3f0df0 */

extern void *gal_og_tex_addr;       /* sdata 3f0dd0 -- shared outgame plates */
extern void *gal_cmn_tex_addr;      /* sdata 3f0dd4 -- gallery common        */
extern void *gal_top_tex_addr;      /* sdata 3f0dd8 -- the menu page         */
extern void *gal_view_tex_addr;     /* sdata 3f0ddc -- the viewer frame      */
extern void *gal_pic_tex_addr;      /* sdata 3f0de0 -- the picture on screen */

void GalleryInit(void);                         /* 0x1a5e98 */
void GalleryMain(void);                         /* 0x1a5ff0 */
void GalleryEnd(void);                          /* 0x1a60f0 */
void GalPictureManage(void);                    /* 0x1a6510 */

/* Push a texture to VRAM only if it is not the one already there. */
void GalPK2SendVram(int tex_id, void *tex_addr);/* 0x1a66c0 */

int  GalPictureLoadReq(int id, int no);         /* 0x1a6728 */
int  GalPictureLoadWait(int file_no);           /* 0x1a67c0 */
void GalPictureMemFree(void);                   /* 0x1a67e8 */
void GalleryBackGroundLoadReq(void);            /* 0x1a6818 */
void GalleryMemFree(void);                      /* 0x1a6a58 */

/* gallery_disp.c */
int  GalAnimation(void);                        /* 0x1a6c58 */
void GalleryDispTop(void);                      /* 0x1a6d78 */
void GalleryDispView(void);                     /* 0x1a70f8 */
void GalleryDispMovie(void);                    /* 0x1a7510 */

#endif /* _OUTGAME_GALLERY_H */
