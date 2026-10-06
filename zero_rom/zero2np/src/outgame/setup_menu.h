/* ==========================================================================
 *  outgame/setup_menu.h
 *
 *  The setup menu (setup_menu.o) -- the screen GID_TITLE_SETUPMENU shows over
 *  setup.c's background.  Three rows on the left (Story / Mission / Exit) and
 *  a five-row settings column on the right (costume, Mio's accessory, Mayu's
 *  accessory, difficulty, and the Game Start / Mission Select button); the
 *  Exit row opens a yes/no window instead.
 *
 *  Only the three entry points setup.c calls are exported; everything else in
 *  the object is static, reached through the two dispatch tables the mode
 *  index selects.
 *
 *  Reconstructed from the Feb 6 2004 prototype (SLES_523.84).
 * ======================================================================== */

#ifndef _OUTGAME_SETUP_MENU_H
#define _OUTGAME_SETUP_MENU_H

/* setup_menu.o's two work blocks (bss 4bbfe0 / 4bbff0).  Declared here rather
 * than in the .c because types.txt names them, but nothing outside the file
 * touches either. */
typedef struct                      /* 0x10 */
{
    /* 0x0 */ char step;            /* SETUP_MENU_STEP_*                      */
    /* 0x1 */ char mode;            /* SETUP_MENU_MODE_*, indexes both tables */
    /* 0x2 */ char conf_csr;        /* 0 yes / 1 no, in the exit window       */
    /* 0x3 */ char menu_csr;        /* SETUP_MENU_CSR_*                       */
    /* 0x4 */ char setup_csr;       /* SETUP_MENU_SETUP_CSR_*                 */
    /* 0x5 */ char costume_csr;     /* index into costume_tbl[9]              */
    /* 0x6 */ char mio_csr;         /* 0 off / 1 on                           */
    /* 0x7 */ char mayu_csr;        /* 0 off / 1 on                           */
    /* 0x8 */ char difficulty_csr;  /* seeded from ingame_wrk.mDifficulty     */
    /* 0x9 */ char next_place;      /* SETUP_MENU_NEXT_*                      */
    /* 0xc */ int  stream_id;       /* the menu BGM, -1 when none             */
} SETUP_MENU_CTRL;

typedef struct                      /* 0xc */
{
    /* 0x0 */ char anim_step;       /* Zero2Anim2D_InOutAnimCtrl()'s state    */
    /* 0x1 */ char anim_timer;
    /* 0x2 */ char csr_timer;       /* settings-column cursor pulse           */
    /* 0x3 */ char menu_csr_timer;  /* left-hand cursor pulse                 */
    /* 0x4 */ char sel_anim_timer;  /* the selected row's own blink           */
    /* 0x8 */ int  fade_anim_timer; /* the black fade into the game           */
} SETUP_MENU_DISP;

void SetupMenuInit(void);           /* 0x2565a8 */
void SetupMenuMain(void);           /* 0x2565f0 */
void SetupMenuDispMain(void);       /* 0x257950 */

#endif /* _OUTGAME_SETUP_MENU_H */
