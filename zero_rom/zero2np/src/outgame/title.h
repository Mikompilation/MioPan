/* ==========================================================================
 *  outgame/title.h
 *
 *  Public title-screen API: title texture load/free, state flags, and the
 *  getters shared by title_disp.o / title_top.o / title_menu.o.
 *
 *  Reconstructed from the Feb 6 2004 prototype (SLES_523.84).
 * ======================================================================== */

#ifndef _OUTGAME_TITLE_H
#define _OUTGAME_TITLE_H

#include <sys/types.h>              /* u_char */

/* --------------------------------------------------------------------------
 *  Title mode work blocks.  Both are title.c statics; the types live here
 *  because types.txt carries them alongside TITLE_MENU_CTRL and
 *  TITLE_MOVIE_WRK, which belong to sibling translation units.
 * ------------------------------------------------------------------------ */
typedef struct                      /* 0x14 */
{
    /* 0x00 */ u_char title_step;   /* 0 request, 1 loading, 2 running, 3 end */
    /* 0x01 */ u_char wait_timer;
    /* 0x04 */ int    stream_id;    /* StreamAuto handle for the title BGM    */
    /* 0x08 */ int    snd_id;
    /* 0x0c */ int    snd_bank_id;
    /* 0x10 */ int    iOriginSndBankLoadPriority;
} TITLE_WRK;

typedef struct                      /* 0x8 */
{
    /* 0x0 */ u_char rgb;           /* shared cursor pulse, 0x40..0x80        */
    /* 0x1 */ char   start_timer;   /* Zero2Anim2D_CsrAnimCtrl's own counter  */
    /* 0x4 */ int    timer;         /* background scroll clock                */
} TITLE_DISP_CTRL;

void  TitleInit(void);
void  TitleTexBackGroundLoadReq(void);
void  GetTitleTexMem(void **tex_addr, int data_label);
void  TitleTexLoadReq(void *tex_addr, int data_label);
void  LiberateTitleTexMem(void **tex_addr);
void  TitleMemFree(void);
void  TitleEndReq(void);

void  SetTitleLoadFlg(char flg);
void  SetTitleBgSendLock(u_char flg);
void  SetTitleStreamID(int stream_id);
void  SetTitleSoundID(int sound_id);

void *GetTitleBgTexAddr(void);
void *GetTitleLogoTexAddr(void);
void *GetOutGameCmnTexAddr(void);
int   GetTitleStreamID(void);
int   GetTitleSoundBankID(void);
int   GetTitleSoundID(void);
u_char GetTitleAnimRGB(void);

#endif /* _OUTGAME_TITLE_H */
