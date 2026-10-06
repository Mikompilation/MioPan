/* ==========================================================================
 *  ingame/pause/prg/pause.h
 *
 *  Ingame pause interface.
 *
 *  PAUSE_CTRL lives here rather than in pause.c because outgame/mission_pause.c
 *  declares its own file-static instance of the same type (bss 4b6448 against
 *  pause.c's 4bbaf8) -- the two screens share the layout, not the state.
 *
 *  Reconstructed from the Feb 6 2004 prototype (SLES_523.84).
 * ======================================================================== */

#ifndef _INGAME_PAUSE_H
#define _INGAME_PAUSE_H

typedef struct                      /* 0xc */
{
    /* 0x0 */ char step;            /* 0 menu, 1 return-title confirm, 2 pad error */
    /* 0x1 */ char before_step;     /* step to restore when the pad comes back    */
    /* 0x2 */ char csr;             /* menu row 0..2                              */
    /* 0x3 */ char title_csr;       /* confirm cursor: 0 YES, 1 NO                */
    /* 0x4 */ int  vib_csr;         /* live copy of the vibration option          */
    /* 0x8 */ int  vib_time;        /* frames of feedback rumble left             */
} PAUSE_CTRL;

void PauseInit(void);
void PauseDispMain(void);
int  PauseMain(void);

#endif /* _INGAME_PAUSE_H */
