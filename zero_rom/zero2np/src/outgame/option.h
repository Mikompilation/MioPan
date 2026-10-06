/* ==========================================================================
 *  outgame/option.h
 *
 *  The option screen (option.c) and its drawing half (option_disp.c).
 *
 *  Reconstructed from the Feb 6 2004 prototype (SLES_523.84).
 * ======================================================================== */

#ifndef _OUTGAME_OPTION_H
#define _OUTGAME_OPTION_H

#include <sys/types.h>              /* u_char */

#include "../common/save_data.h"    /* MC_SAVE_DATA */
#include "../common/variable.h"     /* OPTION_WRK */

/* OPT_CTRL::now_place -- index into OptionCtrlModule[] / OptionDispModule[].
 * 4 is not a module: it is the exit. */
#define OPT_PLACE_MAIN      0
#define OPT_PLACE_OPERATE   1
#define OPT_PLACE_BUTTON    2
#define OPT_PLACE_BRIGHT    3
#define OPT_PLACE_END       4

/* OPT_CTRL::window -- the two modal states, both entered via main_step 4. */
#define OPT_WINDOW_NONE     0
#define OPT_WINDOW_INIT     1
#define OPT_WINDOW_SAVE     2

typedef struct                      /* 0x40 */
{
    /* 0x00 */ int mode;            /* 0 from the title, non-zero from pause */
    /* 0x04 */ int now_place;
    /* 0x08 */ int next_place;
    /* 0x0c */ int anm_alpha;
    /* 0x10 */ int now_tex;
    /* 0x14 */ int main_step;
    /* 0x18 */ int anm_step;
    /* 0x1c */ int save_step;
    /* 0x20 */ int vib_time;        /* frames left on the test buzz          */
    /* 0x24 */ int test_vib_flg;
    /* 0x28 */ int window;
    /* 0x2c */ int yn_csr;
    /* 0x30 */ int cursor;
    /* 0x34 */ int next_csr;
    /* 0x38 */ int old_csr;         /* breadcrumb back to the main list      */
    /* 0x3c */ int old_csr2;        /* breadcrumb back to the operate page   */
} OPT_CTRL;

/* The option screen's editable copy of the settings; opt_wrk is the live one.
 * The three that have to be audible while being edited are written to both --
 * see OptionVibChange() and OptionMainPad(). */
extern OPTION_WRK optm;             /* data 33a0f0 */
extern OPT_CTRL   opt_ctrl;         /* data 33a0b0 */
extern OPT_CTRL  *oc;               /* sdata 3f3638 */

extern void *opt_og_tex_addr;       /* sdata 3f363c */
extern void *opt_top_tex_addr;      /* sdata 3f3640 */
extern void *opt_brn_tex_addr;      /* sdata 3f3644 */
extern void *opt_key_tex_addr;      /* sdata 3f3648 */

void InitOptionSetup(OPTION_WRK *ow);           /* 0x228200 */
void OptionInit(int mode);                      /* 0x228250 */
void OptionMain(void);                          /* 0x2282d8 */
void OptionEnd(void);                           /* 0x228540 */
void OptPK2SendVram(int tex_id, void *tex_addr);/* 0x228d80 */
void OptSetOptWrk(void);                        /* 0x229010 */
void OptSoundSetupRef(OPTION_WRK *ow);          /* 0x229048 */
void SetSave_Option(MC_SAVE_DATA *data);        /* 0x2290a8 */
void OptionBackGroundLoadReq(void);             /* 0x2290c0 */
void OptionMemFree(void);                       /* 0x229300 */

/* Vibration setting.  The pause menu toggles it in place, which is why these
 * two are separate from the rest of the screen. */
void OptionVibChange(int sw);                   /* 0x229420 */
int  GetOptionVib(void);                        /* 0x229438 */

/* option_disp.c */
int  OptAnimation(void);                        /* 0x229598 */
void OptionMainDisp(void);                      /* 0x2295e8 */
void OptionOperateDisp(void);                   /* 0x2298c0 */
void OptionBrightnessDisp(void);                /* 0x229b00 */
void OptionButtonSetupDisp(void);               /* 0x229d08 */
void OptionInitialyzeDisp(void);                /* 0x22a668 */
void OptDispBgMask(void);                       /* 0x22a838 */

#endif /* _OUTGAME_OPTION_H */
