/* ==========================================================================
 *  ingame/photo/filament.h
 *
 *  The camera's viewfinder filament -- the glowing needle that deflects when
 *  something photographable is in shot.
 *
 *  Three deflection terms are kept apart on purpose: SetHint(), SetAuto() and
 *  SetBattle() each own one, so a photographable object cannot mask a passive
 *  ghost and neither can mask a fight.  Draw() picks between them in that
 *  priority order (battle, then auto, then hint) and swaps the sprite pair for
 *  the two lower ones, so the needle changes colour as well as angle.
 *
 *  Reconstructed from the Feb 6 2004 prototype (SLES_523.84), filament.o.
 * ======================================================================== */

#ifndef _INGAME_PHOTO_FILAMENT_H
#define _INGAME_PHOTO_FILAMENT_H

#include "../../common/save_data.h"     /* MC_SAVE_DATA */
#include "../../common/variable.h"      /* CWrkVariable / CBlinkSwitchVariable */

/* The needle's scripted-override request block.  The `sp*` pair is the
 * spirit-drain override player.c raises through RTSpiritsDownModeOn(); the
 * plain `type`/`time` pair is the ordinary RT (real-time) filament mode.
 *
 * Dead weight in this build: CFilament::Init() clears it and nothing ever
 * writes it, because RTModeOn() / RTModeOff() are both empty bodies. */
struct RT_EV_WRK                    /* 0x14 */
{
    /* 0x00 */ int  time;
    /* 0x04 */ int  cnt;
    /* 0x08 */ char type;
    /* 0x0c */ int  sptime;
    /* 0x10 */ char sptype;
};

/* The needle's own animation state (filament.c).  Only `bright` is ever read,
 * and only on the RT path, which nothing in this build can reach. */
typedef struct                       /* 0x14 */
{
    /* 0x00 */ int   mode;
    /* 0x04 */ int   cnt;
    /* 0x08 */ int   flow;
    /* 0x0c */ float bright;
    /* 0x10 */ int   flg;
} FILLAMENT_WRK;

/* Everything below rt_ev_wrk is private in the ROM; left public here because
 * nothing has accessors yet. */
struct CFilament                    /* 0x4c */
{
    /* 0x00 */ RT_EV_WRK     rt_ev_wrk;
    /* Master fade, 0..128, applied to every piece.  Init() parks it at the
     * maximum and only FadeIn() / FadeOut() -- neither of which the ROM ever
     * calls -- would move it, so in practice it stays at 128 for good. */
    /* 0x14 */ CWrkVariable<short, 0, 128> mMasterAlp;
    /* 0x18 */ float         mRate;         /* deflection actually drawn, 0..1 */
    /* 0x1c */ char          mMode;         /* unused in this build            */
    /* 0x20 */ int           mLockCnt;      /* non-zero suppresses the draw    */
    /* 0x24 */ int           mRTTime;       /* unused in this build            */
    /* 0x28 */ float         mHintRate;
    /* 0x2c */ float         mAutoRate;
    /* 0x30 */ float         mBattleRate;
    /* The debug info types this `unsigned int`, which on the host would put
     * the storage unit at 0x34 and leave the struct the right size anyway --
     * spelled u_char to keep the flag in byte 0x34 as the ROM has it. */
    /* 0x34:0 */ u_char      mRTFlg : 1;
    /* 0x35 */ u_char        _pad35[3];
    /* 0x38 */ FILLAMENT_WRK fillament_wrk;

    /* A counted lock, so nested holders are safe.  The event macro interpreter
     * takes one for the whole of synchro mode. */
    void DrawLock(void);
    void DrawUnlock(void);

    /* Lifecycle and per-frame.  FrameReset() drops the three deflection rates
     * that the ghost sweeps and photo_dat.c re-raise every frame; `bFlip` on
     * Draw() picks the out-of-viewfinder layout -- the same three sprites
     * mirrored and turned a quarter turn about the base plate's position, which
     * is what n_plyr_camera.c draws at (0x130, 0x1a2) with the finder closed. */
    void Init(void);
    void FrameReset(void);
    void Work(void);
    void Draw(int off_x, int off_y, int iAlpha, int bFlip);

    /* Master-alpha ramps, +/-16 a tick.  Both are dead code in the ROM -- zero
     * call sites anywhere in the loadable segments -- and are kept because
     * filament.o exports them. */
    void FadeIn(void);
    void FadeOut(void);

    /* Needle deflection from the nearest ghost, 0..1.  enemy.c feeds the two
     * separately -- passive ghosts move the needle through SetAuto(), hostile
     * ones through SetBattle() -- so a passive ghost cannot mask a fight. */
    void SetAuto(float fRate);
    void SetBattle(float fRate);

    /* The hint deflection: photo_dat.c re-issues it every frame, zero first and
     * then the aim score of the nearest photographable object. */
    void SetHint(float fRate);

    /* Hands the draw lock -- and only that -- to the memory-card block; the
     * only caller is m_plyr_cameraSetSaveFilament().  Four bytes, not the whole
     * object: everything else is rebuilt by Init() on load. */
    void SetSave(MC_SAVE_DATA *save);

    /* The scripted override: RTModeOn() would park the needle on `type` for
     * `time` frames and RTModeOff() release it.  finder.c forwards the event
     * macro interpreter onto these, but both bodies are empty in this
     * prototype, so mRTFlg never goes up and Draw()'s RT branch is unreachable. */
    void RTModeOn(int type, int time);
    void RTModeOff(void);

    static void DrawLock(CFilament *self)   { if (self) self->DrawLock(); }
    static void DrawUnlock(CFilament *self) { if (self) self->DrawUnlock(); }
    static void SetAuto(CFilament *self, float fRate)   { if (self) self->SetAuto(fRate); }
    static void SetBattle(CFilament *self, float fRate) { if (self) self->SetBattle(fRate); }
};

#endif /* _INGAME_PHOTO_FILAMENT_H */
