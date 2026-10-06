/* ==========================================================================
 *  ingame/photo/finder.h
 *
 *  The Camera Obscura's viewfinder overlay (finder.o).
 *
 *  Everything the finder puts on screen that is not one of the CNPlyrCamera
 *  widget classes lives here: the fade of the overlay itself, the health bar,
 *  the charge gauge, the capture circle, the score readout and the shutter-
 *  chance flash, plus the low-level sprite helpers the rest of the photo code
 *  draws through.  info_wrk is the module's state; enedmgline_wrk belongs to
 *  the damage-line effect and is only stored here.
 *
 *  finder.o is unusual in having no file statics beyond five scalars -- all 40
 *  of its functions are exported, which is why this header is the whole
 *  module's interface rather than a subset.
 *
 *  Reconstructed from the Feb 6 2004 prototype (SLES_523.84), finder.o.
 * ======================================================================== */

#ifndef _INGAME_PHOTO_FINDER_H
#define _INGAME_PHOTO_FINDER_H

#include "eetypes.h"

#include "../../common/utility2.h"              /* PRINT_ASSERT                */
#include "../../graphics/graph2d/g2d_draw.h"    /* SPRT_DAT / SPRT_DAT2        */
#include "../../system/eeiop/snd3d.h"           /* SND_3D_SET                  */
#include "../../system/eeiop/snd_buffer.h"      /* CSND_BUF_PLAY               */

/* --------------------------------------------------------------------------
 *  Overlay state
 * ------------------------------------------------------------------------ */

/* One pip of the charge gauge ring.  `flow` is the pip's own little state
 * machine -- 0 dark, 1 armed, 2 filling, 3 draining, 4 held lit -- and `cnt`
 * the frame counter within it, so the twelve pips animate independently. */
typedef struct                      /* 0x8 */
{
    /* 0x0 */ int cnt;
    /* 0x4 */ int flow;
} CHARGE_GUAGE_ONE;

typedef struct                      /* 0x6c */
{
    /* 0x00 */ int    flow;
    /* 0x04 */ int    bg_flow;
    /* 0x08 */ u_char old_ch_num;
    /* 0x0c */ CHARGE_GUAGE_ONE cgo[12];
} CHARGE_GUAGE;

typedef struct                      /* 0x8 */
{
    /* 0x0 */ int cnt;
    /* 0x4 */ int flow;
} SHOT_ENABLE;

/* The post-shot score line: `_num` drives the number, `_sp` the message above
 * it, each with its own state and fade counter. */
typedef struct                      /* 0x1c */
{
    /* 0x00 */ int flow_num;
    /* 0x04 */ int cnt_num;
    /* 0x08 */ int flow_sp;
    /* 0x0c */ int cnt_sp;
    /* 0x10 */ int score;
    /* 0x14 */ int mes_alp;
    /* 0x18 */ int num_alp;
} DISPFMES;

/* The overlay's own work block.  The `fade_*` members are the state machines
 * (0 out, 1 fading in, 2 held, 3 fading out) and the `alp_*` the alphas they
 * drive; `time_hpbar` is how long the health bar has been held up, which is
 * what ReqHPDispOut() checks before it will let it fade. */
typedef struct                      /* 0xac */
{
    /* 0x00 */ u_char  phot_shot;
    /* 0x01 */ u_char  disp_pause;
    /* 0x02 */ short   alp_battle;
    /* 0x04 */ u_char  alp_finder;
    /* 0x05 */ u_char  alp_hpbar;
    /* 0x06 */ char    fade_finder;
    /* 0x07 */ char    fade_hpbar;
    /* 0x08 */ u_char  sw_finder;
    /* 0x09 */ u_char  sw_hpbar;
    /* 0x0a */ u_char  sw_filament;
    /* 0x0c */ u_int   cnt_finder;
    /* 0x10 */ u_int   cnt_hpbar;
    /* 0x14 */ u_short time_hpbar;
    /* 0x16 */ u_char  film_num;
    /* 0x18 */ SHOT_ENABLE  sena;
    /* 0x20 */ CHARGE_GUAGE cg;
    /* 0x8c */ DISPFMES     dispfmes;
    /* 0xa8 */ u_char  alp;
} INFO_WRK;

/* One radial streak of the damage line effect. */
typedef struct                      /* 0x10 */
{
    /* 0x0 */ float sclw;
    /* 0x4 */ float sclh;
    /* 0x8 */ float rot;
    /* 0xc */ float dist;
} ENEDMGLINE_SUB;

typedef struct                      /* 0x3d0 */
{
    /* 0x000 */ ENEDMGLINE_SUB edl_sub[60];
    /* 0x3c0 */ int    flow;
    /* 0x3c4 */ int    cnt;
    /* 0x3c8 */ u_char alp;
    /* 0x3c9 */ u_char dummy08[3];
    /* 0x3cc */ int    col;
} ENEDMGLINE_WRK;

enum SP_CHANCE_MODE
{
    SP_CHANCE_NONE = 0,
    SP_CHANCE_IN   = 1,
    SP_CHANCE_OUT  = 2,
    SP_CHANCE_END  = 3
};

extern ENEDMGLINE_WRK enedmgline_wrk;                       /* data 3124c8 */
extern INFO_WRK       info_wrk;                             /* data 312898 */

/* --------------------------------------------------------------------------
 *  Overlay fades
 * ------------------------------------------------------------------------ */
void FinderBattleAlphaMain(void);
void ReqHPDispIn(void);
void ReqHPDispOut(void);
void HPDispInit(void);
void HPDispMain(void);
/* Viewfinder overlay fade, driven by the player's finder mode changes in
 * player.c.  FinderDispInit() resets the overlay outright -- the quick-exit
 * path (SetPlyrFinderQEnd) uses it when there is no time to fade. */
void ReqFinderDispIn(void);
void ReqFinderDispOut(void);
void FinderDispInit(void);
void FinderDispMain(void);
void InformationDispInit(void);
void InfoDispPause(void);
void InfoDispRestart(void);
int  isDispLamp(void);

/* --------------------------------------------------------------------------
 *  Forwarders onto the camera's widget classes
 * ------------------------------------------------------------------------ */
void RTFillamentModeOn(int type, int time);
void RTFillamentModeOff(void);
void FilamentDrawLock(void);
void FilamentDrawUnlock(void);
void FinderDrawLock(void);
void FinderDrawUnlock(void);
void ReqFinderFadeIn(void);
void ReqFinderFadeOut(void);

/* The nearest hostile ghost's health readout inside the finder.  enemy.c
 * drives all three every frame from EnemyHPSetJob(): Len() sets how much of
 * the bar is on screen at all (0 when no ghost qualifies), SetEnePercentage()
 * snaps it when the tracked ghost changes, and TriggerEneLifeDecrease() feeds
 * the running value so the red damage tail can lag behind it. */
void finderEneLifeLen(float len);
void finderSetEneLifePercentage(float new_hp_per);
void finderTriggerEneLifeDecrease(float new_hp_per);

/* --------------------------------------------------------------------------
 *  Shutter chance
 * ------------------------------------------------------------------------ */
void SPChanceMain(void);

/* --------------------------------------------------------------------------
 *  Sprite helpers.  `label` indexes n_finder_dat[]; ptyp 2 adds the table
 *  entry's own x/y to the position, anything else treats it as absolute.
 * ------------------------------------------------------------------------ */
void DispChara(int label, u_char ptyp, float x, float y, u_char z,
               u_char alp, u_char atyp, u_char bln);
void DispCharaRGB(int label, u_char ptyp, float x, float y, u_char z,
                  u_char r, u_char g, u_char b, u_char alp, u_char atyp,
                  int bln);
/* Lays a decimal number out right-to-left from `sprt`, which must point at ten
 * consecutive digit records.  iZeroDispFigure forces leading zeros out to that
 * many digits. */
void SetNumerousDisp(SPRT_DAT *sprt, int n, int alpha, int chara_width,
                     int pos_x, int pos_y, float scale, int iZeroDispFigure,
                     int bAddAlphaFlg);
void DispPointNumberNew(int number, short adj_x, short adj_y, u_char malp);
void DispCameraCharge(short pos_x, short pos_y, int battle_master_alp);
void ChargeDispReset(void);
void DispCaptureCircleNew(short pos_x, short pos_y);
void DispFinderMessageMain(void);
/* Draws every n_finder_dat[] record from top_label to end_label inclusive with
 * one shared transform.  scl_mode 0 scales about each piece's own centre,
 * anything else about the first record's anchor. */
void PutSpriteYW(u_short top_label, u_short end_label, float pos_x, float pos_y,
                 float rot, int rgb, float alp, float scl_x, float scl_y,
                 u_char scl_mode, int pri, u_char by, u_char blnd, u_char z_sw);
void SD1toSD2(SPRT_DAT *sd, SPRT_DAT2 *sd2);

/* --------------------------------------------------------------------------
 *  The camera's own sound bank: shutter, hit and vanish cues.  The ghost
 *  scripts play them through here rather than through the ghost's bank so the
 *  volume follows the viewfinder, not the ghost's distance.
 * ------------------------------------------------------------------------ */
void FinderBankSetup(void);
int  FinderBankIsReady(void);
int  FinderBankPlay(int no, int effect, int loop, int fade_time,
                    SND_3D_SET *s3d, int vol, int pitch);
void FinderBankRelease(void);
int  FinderBankIsLoopSnd(int no);

/* A held handle on one of those voices.  Adds nothing to the base but the pool
 * it plays from; the inherited constructor is what seeds play_id with
 * CSND_BUF_PLAY_NO_ID rather than 0.
 *
 * It lives here rather than beside its owners in m_plyr_camera.h because the
 * ROM says so: the assert banner Play() expands to in n_plyr_camera.o names
 * finder.h, line 222.  The seven-argument signature is the stabs'. */
struct CFINDER_SND_BUF_PLAY : CSND_BUF_PLAY /* 0x4 */
{
    void Play(int no, int effect, int loop, int fade_time, SND_3D_SET *s3d,
              int vol, int pitch)
    {
        /* Reports the leak and plays anyway -- the old handle is dropped on
         * the floor.  Every ROM call site tests IsPlaying() first, so the
         * branch is dead at all of them. */
        if (play_id != CSND_BUF_PLAY_NO_ID)                                      /* 221 */
        {
            PRINT_ASSERT("Overlap Snd Play");                                    /* 222 */
        }

        play_id = FinderBankPlay(no, effect, loop, fade_time, s3d, vol, pitch);  /* 225 */
    }
};

#endif /* _INGAME_PHOTO_FINDER_H */
