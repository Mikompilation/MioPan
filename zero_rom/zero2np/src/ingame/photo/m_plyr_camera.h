/* ==========================================================================
 *  ingame/photo/m_plyr_camera.h
 *
 *  The player photo-camera.  m_plyr_camera.c owns the object and the four save
 *  hooks; everything the camera does lives in other translation units.  The
 *  CNPlyrCamera layout below is the ROM's: member names, order and offsets all
 *  come from the prototype's debug info, and the offsets in the comments are
 *  the PS2 ones (the class is 0x30c bytes there).
 *
 *  Every sub-object is its real ROM size and every offset up to mZoomRate
 *  (0x160) matches, checked with an offsetof harness.  From finder_buf (0x164)
 *  onwards the host drifts by 12 bytes because the three pointer members --
 *  finder_buf, pl_life_buf, mpSpiritGage -- are 8 bytes here and 4 on target,
 *  which also makes the class 0x318 rather than 0x30c.  Nothing indexes it by
 *  offset, so the comments past that point are documentation.
 *
 *  Sub-objects that are still not reconstructed (CCenterCross) are declared
 *  with their real name and real ROM size but an opaque body -- deliberately,
 *  so the layout stays honest instead of carrying invented field names.
 *
 *  Most of the sub-object *classes* belong to headers of their own in the ROM
 *  (camera_film.h, hp_bar.h, center_cross.h, bonus_shot.h, ...) and are parked
 *  here until those object files are split out; each says so where it stands.
 *  Their method lists are the ROM's own, read out of the stabs in symbols.txt
 *  rather than inferred from call sites -- which is how, for example,
 *  CFinderBase::DrawKakiwari came out private and CHpBar::FadeIn/FadeOut were
 *  identified at all.
 *
 *  Reconstructed from the Feb 6 2004 prototype (SLES_523.84).
 * ======================================================================== */

#ifndef _INGAME_PHOTO_M_PLYR_CAMERA_H
#define _INGAME_PHOTO_M_PLYR_CAMERA_H

#include <string.h>                 /* memset (CCenterCross::Init) */

#include "eetypes.h"
#include "../../common/save_data.h"  /* MC_SAVE_DATA */
#include "../../system/os/system.h"  /* GetPALMode (CCameraFilm::GetFilmChargeSpd) */
#include "../../common/variable.h"  /* BIT_FLAGS / INGAME_CHAR_VAR */
#include "../../graphics/graph3d/ctl/fixed_array.h"
#include "../../system/eeiop/snd_buffer.h" /* CSND_BUF_PLAY */
#include "camera_power_up.h"        /* CCameraPowerUp */
#include "filament.h"               /* CFilament */
#include "finder.h"                 /* CFINDER_SND_BUF_PLAY */
#include "n_equip_tray.h"           /* CNEquipTraySave / CNEquipTrayWrk */
#include "photo.h"                  /* SyncHpBar (CHpBar::Init) */
#include "spirit_gage.h"            /* CSpiritGage */

/* ---- the ROM's widget-value templates ----------------------------------
 * Every animated UI value in the photo camera is one of these.  They are
 * uniform enough to write as templates, which is how the ROM has them; the
 * bounds are template parameters and live only in the (unreconstructed)
 * method bodies, so they do not affect layout.
 *
 * These belong to no single module -- move them to a shared header the moment
 * something outside ingame/photo needs them. */

/* CWaitVariable<T> moved to common/variable.h -- fene_entry.c needs one too. */

/* CWrkVariable<T,Min,Max> moved to common/variable.h -- spirit_gage.h needs
 * one, and enemy.h pulls that in. */

/* CFadeVariable<T> moved to common/variable.h -- n_plyr_camera.o places it
 * there (every expansion carries a variable.h line number) and its Fade() and
 * Work() are linkonce, so it has to be in a header, not here. */

/* CBlinkVariable<T,Min,Max> moved to common/variable.h -- n_equip_tray.o
 * places it there (Init and IsOn expand with variable.h line numbers, and
 * Blink()'s assert banner names that header), and Blink() / Work() are
 * linkonce, so it has to be in a header the neighbours already see. */

/* CBlinkSwitchVariable<T,Min,Max,Time,InitVal> moved to common/variable.h --
 * filament.o places it there: its four out-of-line Work() bodies are tagged
 * with that file, and so are the Init() / BlinkOn() / Get() expansions inside
 * CFilament.  The parameter list was guessed wrong here (Lo/Hi/Up/Dn); the
 * fourth is a sweep *time*, not a step, and the fifth the value the switch
 * decays to when it is off. */

/* CFVariable moved to common/variable.h -- n_equip_tray.o places it there,
 * the same way as the templates above.  Its constructor stays out of line in
 * m_plyr_camera.c, which is the object file that owns the body. */

/* Which upgrades the camera has.  The three flag sets and the lens set are
 * what the CAM_*_GET macro opcodes raise; the grade/gem members above them are
 * the real layout's, carried so the flag offsets are not invented. */
/* CCameraPowerUp moved to camera_power_up.h -- that is where the ROM declares
 * it: player.o's PhotoDmgChkSub2() and SpiritGageCalc() tag its three inline
 * accessors with `SOL ../photo/camera_power_up.h`.  The include sits at the
 * top with the other sub-object headers. */

/* Which film is loaded.  ITEM_GET sets this the first time film is received.
 *
 * Declared in camera_film.h in the ROM; parked here with the rest of the
 * camera's sub-objects until camera_film.o is split out.  The three tables are
 * that object file's .sdata and are reproduced in camera_film.c. */
struct CCameraFilm                  /* 0x1 */
{
    /* 0x0 */ CVariable<char, 0, 4> mFilmType;

    static unsigned char aFilmDamageTbl[5];         /* sdata 3ef598 */
    static unsigned char aFilmMinPercentTbl[5];     /* sdata 3ef5a0 */
    static unsigned char aFilmChargeSpdTbl[5];      /* sdata 3ef5a8 */

    int GetFilmDamage(void);
    int GetFilmMinPercent(void);

    /* How many frames the shutter takes to recharge with this film loaded --
     * 170 for the weakest, 60 for Type-90.  The PAL branch is the ROM's own:
     * the table is in NTSC frames, so a 50 Hz build has to scale it down or
     * the wait comes out a fifth longer in wall time.
     *
     * Inline in the ROM (camera_film.h line 20); CNPlyrCamera::Init() and
     * ::ResetCharge() are the only two expansions, and both carry the whole
     * branch, which is what puts the GetPALMode() test inside here rather than
     * at the call site. */
    int GetFilmChargeSpd(void)
    {
        if (GetPALMode() != 0)
        {
            return aFilmChargeSpdTbl[mFilmType] * 50 / 60;                       /* 20 */
        }

        return aFilmChargeSpdTbl[mFilmType];
    }
};

/* CNEquipTraySave / CNEquipTrayWrk moved to n_equip_tray.h -- that is where
 * the ROM declares them (its inline GetNowSubFuncNo() / EquipTrayFuncCostNum()
 * / GetNowSubFuncLv() expand with n_equip_tray.h line numbers inside
 * n_equip_tray.o), and CNPlyrCamera embeds one, so this header pulls it in.
 * The include sits at the top with the other sub-object headers. */

/* ---- finder widgets ----------------------------------------------------- *
 * The pieces of the viewfinder HUD.                                          */

/* An x-only layout point.  Every table of these in the finder is two entries
 * long and indexed by "is the language German", whose longer words push the
 * readout left; the y never changes, which is why the struct has one field. */
typedef struct                      /* 0x2 */
{
    /* 0x0 */ short x;
} MyPoint;

/* CFINDER_SND_BUF_PLAY moved to finder.h -- the ROM's assert banner for its
 * Play() names that header, and Play() needs FinderBankPlay(). */

/* The ring at the centre of the finder.  Its colour fades between states,
 * which is what the four flags select.  enemy.c raises them: the auto flag for
 * a passive ghost within reach, the battle flag for a hostile one, and the
 * enemy-catch flag for one already inside the ring. */
struct CCenterCircle                /* 0x24 */
{
    /* 0x00 */ CWrkVariable<char, 0, 100>  maScale[4];
    /* 0x08 */ CWrkVariable<short, 0, 128> mRippleAlpha;
    /* 0x0c */ CFadeVariable<short>        mR;
    /* 0x12 */ CFadeVariable<short>        mG;
    /* 0x18 */ CFadeVariable<short>        mB;
    /* 0x1e */ CFadeVariable<char>         mMainAlpha;
    /* The debug info types these as `unsigned int` bitfields, but that puts
     * the storage unit at 0x24 here and inflates the struct to 0x28.  A u_char
     * unit reproduces both ROM facts -- flags in byte 0x21, sizeof 0x24. */
    /* 0x21:0 */ u_char mHintFlg   : 1;
    /* 0x21:1 */ u_char mAutoFlg   : 1;
    /* 0x21:2 */ u_char mBattleFlg : 1;
    /* 0x21:3 */ u_char mEnemyFlg  : 1;
    /* The `unsigned int` unit also gave the ROM struct 4-byte alignment and so
     * two bytes of tail padding.  Spelled out, since a u_char unit would end
     * the struct at 0x22. */
    /* 0x22 */ u_char _pad[2];

    /* The four states the ring fades between, indexed by the mode Work()
     * derives from the flags: 0 idle, 1 hostile, 2 hint/auto, 3 unused (a
     * copy of 2).  aMainAlpha is a percentage, not a sprite alpha. */
    static u_char aRgb[4][3];                               /* data 2d8d10 */
    static char   aMainAlpha[4];                            /* sdata 3ef638 */

    void Init(void);
    /* Drops the per-frame flags again.  CNPlyrCamera::FrameReset() is the only
     * caller, and it runs before the frame's ghost sweeps re-raise them. */
    void FrameReset(void);
    void Work(void);
    void Draw(int off_x, int off_y, int iAlpha, float fScale);
    void SetMode(int mode);

    void SetAutoFlg(int flg);
    void SetBattleFlg(int flg);
    void SetEneCatchFlg(int flg);
    /* Raised for the frame by photo_dat.c when a photographable object is
     * inside the ring; cleared at the top of every photo_datObjMain(). */
    void SetHintFlg(int flg);

    static void SetAutoFlg(CCenterCircle *self, int flg)     { if (self) self->SetAutoFlg(flg); }
    static void SetBattleFlg(CCenterCircle *self, int flg)   { if (self) self->SetBattleFlg(flg); }
    static void SetEneCatchFlg(CCenterCircle *self, int flg) { if (self) self->SetEneCatchFlg(flg); }
};

/* Stateless -- a class purely to hang the finder-frame draw off.  Both bodies
 * live in n_plyr_camera.o, which is why they are here rather than in a module
 * of their own. */
struct CFinderBase                  /* 0x1 */
{
    char _empty;

    void Draw(int off_x, int off_y, int iAlpha);

private:
    /* 描き割り -- the painted flat behind the finder: the six border pieces
     * that mask everything outside the viewfinder aperture. */
    void DrawKakiwari(int off_x, int off_y, int iAlpha);
};

/* Enemy health readout while a ghost is in frame. */
struct CEneLife                     /* 0x1c */
{
    /* 0x00 */ int   red_bar_wait;
    /* 0x04 */ float now_hp_percent;
    /* 0x08 */ float disp_hp_percent;
    /* 0x0c */ float old_hp_percent;
    /* 0x10 */ float ene_hp_len;
    /* 0x14 */ CWrkVariable<short, 0, 127> mDamageAlpha;
    /* 0x18 */ short mDamage;

    /* enemy.c drives all three every frame through finder.c: FrameLenSet()
     * says how much of the bar is on screen at all (0 when no ghost
     * qualifies), Set() snaps it when the tracked ghost changes, and
     * Decrease() feeds the running value so the red tail can lag behind. */
    void FrameLenSet(float len);
    void Set(float new_hp_per);
    void Decrease(float new_hp_per);

    void Work(void);
    /* The damage number that flashes over the bar; PhotoInfoDispNew() feeds it
     * the shot's total. */
    void SetDamage(int iDamage);
    void Draw(int off_x, int off_y, int iAlpha);
};

/* The off-screen "a ghost is over there" arrow.  enemy.c aims it at whichever
 * highlighted ghost is nearest and off screen, in degrees clockwise from the
 * top of the finder. */
struct CSearchMark                  /* 0x8 */
{
    /* 0x0 */ float mRot;
    /* 0x4 */ CWrkVariable<short, 0, 128> mAlpha;

    void Init(void);
    void Work(void);
    void Draw(int off_x, int off_y, int iAlpha, float fScale);
    void Release(void);

    void SetRot(float fRot);
    void FadeIn(void);
    void FadeOut(void);

    static void SetRot(CSearchMark *self, float fRot) { if (self) self->SetRot(fRot); }
    static void FadeIn(CSearchMark *self)  { if (self) self->FadeIn(); }
    static void FadeOut(CSearchMark *self) { if (self) self->FadeOut(); }
};

/* One tick mark.  `cnt` is how far along its travel it is, 0..CC_CNT_MAX, and
 * (tx, ty) the finder-space point it is travelling towards -- the ghost's own
 * screen position while that ghost qualifies, the player's crosshair once it
 * stops.  `alp` ramps separately so a mark fades rather than vanishing. */
typedef struct                      /* 0xc */
{
    /* 0x0 */ int    cnt;
    /* 0x4 */ short  tx;
    /* 0x6 */ short  ty;
    /* 0x8 */ u_char alp;
} CENTER_CROSS;

/* The ten little marks that ring the capture circle -- one per ENE_WRK slot,
 * each creeping out towards its ghost and back again. */
struct CCenterCross                 /* 0x78 */
{
    /* 0x00 */ CENTER_CROSS center_cercle[10];

    /* Inline in the ROM (center_cross.h line 24): CNPlyrCamera::Init() expands
     * it to a bare memset of the whole object rather than to ten element
     * resets, so it is written as one here too. */
    void Init(void) { memset(center_cercle, 0, sizeof(center_cercle)); }         /* 24 */

    void Work(void);
    void Draw(int off_x, int off_y, int iAlpha);
};

/* The film-type readout in the corner of the finder.  It blinks when the
 * shutter is charged, which is the only state it has. */
struct CFilmNo                      /* 0x2 */
{
    /* 0x0 */ CBlinkSwitchVariable<char, 70, 90, 15, 40> mBlinkAlpha;

    void Init(void);
    void Work(void);
    void BlinkOn(void);
    void BlinkOff(void);
    void Draw(int iNum, int iTypeNo, int off_x, int off_y, int iAlpha);
};

/* CSpiritGage moved to spirit_gage.h -- ENE_WRK embeds one, and enemy.h must
 * not pull the whole camera in to get it. */

/* The damage number that flies off a hit ghost. */
struct CDamageDisp                  /* 0xc */
{
    /* 0x0 */ CWaitVariable<short>         mWaiter;
    /* 0x2 */ CBlinkVariable<char, 0, 127> mOneBlink;
    /* 0x4 */ short mDamage;
    /* 0x8 */ int   mScore;

    void Init(void);
    void Work(void);
    /* iWait is how long the number holds before it fades; PhotoInfoDisp()
     * always passes 60, one NTSC second. */
    void Req(int iDamage, int iScore, int iWait);
    void Draw(int off_x, int off_y, int iAlpha);
};

/* Post-shot score breakdown.  Nine lines can appear -- one per bonus the shot
 * earned -- and each animates in independently, which is what the per-line
 * work block below is for.
 *
 * Declared in bonus_shot.h in the ROM; parked here with the rest of the finder
 * widgets until bonus_shot.o is reconstructed. */
/* The older, flag-per-bonus form of the same thing.  PhotoInfoDisp() still
 * takes one and drops it on the floor -- the ROM's body reads only the damage
 * and the score -- so it survives as a parameter type and nothing else.
 * Declared in photo.h in the ROM; parked here with its one user. */
struct _PHOTO_BONUS_SHOT            /* 0x4 */
{
    /* 0x0:0 */ u_int triple : 1;
    /* 0x0:1 */ u_int dbl    : 1;
    /* 0x0:2 */ u_int core   : 1;
    /* 0x0:3 */ u_int close  : 1;
    /* 0x0:4 */ u_int SP     : 1;
    /* 0x0:5 */ u_int chance : 1;
};
typedef _PHOTO_BONUS_SHOT PHOTO_BONUS_SHOT;

/* Which two n_finder_dat[] records spell one bonus's name.  Every line of the
 * breakdown is two texture pieces, placed end to end by CBonusShotOne::Draw();
 * aShotTexTbl in bonus_shot.c has one of these per language per bonus. */
typedef struct                      /* 0x2 */
{
    /* 0x0 */ u_char mPreTexNo;
    /* 0x1 */ u_char mAfterTexNo;
} SHOT_NAME_TEX;

struct BONUS_SHOT_SCORE             /* 0x16 */
{
    /* 0x00 */ fixed_array<short, 9> mScore;
    /* 0x12 */ short mSP;
    /* 0x14 */ short mComboNum;
};

/* One line of the breakdown.  mIndex is which bonus it is (an index into
 * BONUS_SHOT_SCORE::mScore and into aShotTexTbl's rows); everything else is
 * how it animates.  mYPosSave keeps the line's home row so the underline
 * stays put while the text slides away from it. */
struct CBonusShotOne                /* 0x14 */
{
    /* 0x00 */ CVariable<char, 0, 9> mIndex;
    /* 0x02 */ short mYPosSave;
    /* 0x04 */ CFadeVariable<short> mYPos;
    /* 0x0a */ CFadeVariable<char>  mXOffset;
    /* 0x0d */ CWrkVariable<char, 0, 127> mAlpha;
    /* 0x0f */ CWrkVariable<char, 0, 127> mUnderLineAlpha;
    /* 0x11 */ CWrkVariable<char, 0, 100> mUnderLineScale;

    void Init(void);
    /* Places the line at row iYPos with its text invisible, and starts the
     * underline growing out from under it. */
    void InReqUnderLine(int iYPos);
    /* Brings the text in: the x offset slides back to zero as the alpha
     * ramps up. */
    void InReq(void);
    void OutReq(int iTargetYPos);
    void Work(void);
    void Draw(int iOffX, int iOffY, int iPreSprtDat, int iAfterSprtDat);
};

struct CBonusShot                   /* 0xe4 */
{
    /* 0x00 */ CWaitVariable<char> mFadeOutWaiter;
    /* 0x01 */ CWrkVariable<char, 0, 127> mNewScoreAlpha;
    /* 0x03 */ CWrkVariable<char, 0, 127> mOldScoreAlpha;
    /* 0x05 */ CWrkVariable<char, 0, 127> mScorePtsAlpha;
    /* 0x07 */ CWrkVariable<char, 0, 127> mComboAlpha;
    /* 0x09 */ char mDispNum;
    /* 0x0a */ char mReqUnderLineCnt;
    /* 0x0b */ char mReqUnderLineTimer;
    /* 0x0c */ char mInReqCnt;
    /* 0x0d */ char mInReqTimer;
    /* 0x0e */ char mOutReqCnt;
    /* 0x0f */ char mOutReqTimer;
    /* 0x10 */ CWaitVariable<short> mTimer;
    /* 0x12 */ BONUS_SHOT_SCORE mBonus;
    /* 0x28 */ fixed_array<CBonusShotOne, 9> mAnim;
    /* 0xdc */ int mOldScore;
    /* 0xe0 */ int mNewScore;

    void Init(void);
    void Work(void);
    /* The breakdown arrives whole -- one score per bonus the shot earned --
     * and iWaitTime is how long the whole sequence holds before it leaves. */
    void Req(int iBaseScore, BONUS_SHOT_SCORE BonusScore, int iWaitTime);
    void Draw(int fndr_mx, int fndr_my);

private:
    /* The three staggered passes: underlines out, text in, everything out.
     * Each advances one line every fourth frame and parks its counter at -1
     * when it has run through mDispNum of them. */
    void ReqUnderLineWrk(void);
    void InReqWrk(void);
    void OutReqWrk(void);
};

/* Pulls the drawn bar back onto the player's real HP.  It belongs to hp_bar.o
 * alongside CHpBar, not to photo.o -- it was declared in photo.h by an earlier
 * pass, which is where item.c and ingame.c still reach it from. */
void SyncHpBar(void);

/* The player's health bar.  ingame.c drives the fade directly on the Damage
 * phase: mAlpha.mAdd pushes it in, mFadeWaitCnt holds it up before it fades. */
struct CHpBar                       /* 0x6 */
{
private:
    /* 0x0 */ CWrkVariable<short, 0, 128> mAlpha;
    /* 0x4 */ CWaitVariable<short>        mFadeWaitCnt;

public:
    /* Inline in the ROM (hp_bar.h lines 13-17).  The SyncHpBar() at the end is
     * what makes this more than a pair of resets: it pulls the drawn value
     * back onto the player's real HP so the bar does not animate up from zero
     * the first time it is shown. */
    void Init(void)                                                              /* 13 */
    {
        mAlpha.Init();                                                           /* 14 */
        mFadeWaitCnt.Reset();
        SyncHpBar();                                                             /* 17 */
    }

    void Work(void);
    void Draw(int off_x, int off_y, int iAlpha);

    /* Also inline in the ROM -- hp_bar.o exports only Work() and Draw(), and
     * CNPlyrCamera cannot reach the members, which are private there.  The
     * bodies are what CNPlyrCamera::FinderIn() and ::FinderOut() expand to:
     * coming up, the bar ramps in at 32 a frame with the hold cancelled;
     * going down, only the 140-frame hold is armed, and Work() is what turns
     * its expiry into the fade. */
    void FadeIn(void)
    {
        mAlpha.SetAddVal(0x20);
        mFadeWaitCnt.Reset();
    }

    void FadeOut(void) { mFadeWaitCnt.Wait(0x8c); }
};

/* The spirit-power ("shutter chance") gauge.  player.c raises and clears it as
 * the shutter chance comes and goes; Set() is still a stub in n_plyr_camera.c. */
struct CSPChance                    /* 0x10 */
{
    /* 0x0:0 */ u_int mbSeFlg : 1;
    /* 0x0:1 */ u_int mSPFlg  : 1;
    /* 0x4 */ CFINDER_SND_BUF_PLAY                   mSe;
    /* 0x8 */ CWrkVariable<short, 0, 128>            mLampAlpha;
    /* 0xc */ CBlinkSwitchVariable<short, 0, 128, 1, 0> mAlpha;

    void Set(int bOn);
    static void Set(CSPChance *self, int bOn) { if (self) self->Set(bOn); }

    void Init(void);
    /* bEnable is the fitted-part test -- the lamp only works once the camera
     * has the shutter-chance part, so Main() hands it that flag every frame
     * rather than gating the call. */
    void Work(int bEnable);
    void Draw(int off_x, int off_y, int iAlpha);
    void Release(void);
    void SEEnable(void);
    void SEDisable(void);
};

/* Charge-shot accumulator.  IsReady() gates the zoom-in that telegraphs a
 * charged shot; the body is still a stub in n_plyr_camera.c. */
struct CPhotoCharger                /* 0x8 */
{
    /* 0x0 */ CWaitVariable<short>          mNowWaitCnt;
    /* 0x2 */ CBlinkVariable<char, 50, 127> mFlare;
    /* 0x4 */ short  mWaitCnt;
    /* 0x6 */ u_short mReady;

    int IsReady(void);
    static int IsReady(CPhotoCharger *self) { return self ? self->IsReady() : 0; }

    /* iWaitCnt is the film's charge time in frames -- CCameraFilm supplies it,
     * already scaled for PAL. */
    void Reset(int iWaitCnt);
    /* bSeFlg gates the "nearly charged" cue and nothing else: n_plyr_camera
     * passes 1 in finder mode and 0 outside it, and the countdown advances
     * either way. */
    void Work(int bSeFlg);
    void Draw(int fndr_mx, int fndr_my, int iAlpha);

    /* types.txt also lists Init(), but ZERO2.MAP has no out-of-line copy and
     * nothing in the build expands it -- CNPlyrCamera::Init() calls Reset()
     * instead -- so there is no body to recover and none is invented. */
};

class CNPlyrCamera                  /* 0x30c */
{
public:
    /* 0x000 */ CCameraPowerUp camera_power_up;
    /* 0x020 */ CCameraFilm    camera_film;
    /* 0x024 */ CNEquipTrayWrk eq_tray;
    /* 0x0a4 */ CFilament      filament;
    /* 0x0f0 */ CCenterCircle  center_circle;
    /* 0x114 */ CHpBar         hp;
    /* 0x11a */ CFinderBase    finder_base;
    /* 0x11c */ CSPChance      sp;
    /* 0x12c */ CEneLife       ene_life;
    /* 0x148 */ CSearchMark    search_mark;

    /* Private in the ROM; left public here because nothing has accessors yet. */
    /* 0x150 */ CWrkVariable<short, 0, 128> mFcs;
    /* 0x154 */ CFadeVariable<float>        mFOV;
    /* 0x160 */ float  mZoomRate;
    /* 0x164 */ void  *finder_buf;
    /* 0x168 */ void  *pl_life_buf;

    /* One byte of flags.  The ROM read-modify-writes the whole word at 0x16c
     * to set bit 0, which is why a decompiler shows a 4-byte field here. */
    /* 0x16c:0 */ u_char mBattleFlg : 1;
    /* 0x16c:1 */ u_char mHintFlg   : 1;

    /* 0x16d */ char mNoSpiritGageTimer;
    /* 0x16e */ char mDrawLockCnt;
    /* 0x16f */ CWrkVariable<char, 0, 64>   mSpiritGageScale;
    /* 0x171 */ CWaitVariable<char>         mFOVTimer;
    /* 0x172 */ CWaitVariable<short>        mInWaiter;
    /* 0x174 */ CWrkVariable<int, 0, 128>   mMasterAlpha;
    /* 0x17c */ CWrkVariable<short, 0, 128> mInAlpha;
    /* 0x180 */ short mCntFinder;
    /* 0x184 */ CCenterCross  center_cross;
    /* 0x1fc */ CFilmNo       film_no;
    /* 0x200 */ CSpiritGage  *mpSpiritGage;
    /* 0x204 */ CFadeVariable<int>   mSpiritGageAlpha;
    /* 0x210 */ CPhotoCharger        charger;
    /* 0x218 */ CFINDER_SND_BUF_PLAY mSpiritNoise;
    /* 0x21c */ CDamageDisp          mDmgDisp;
    /* 0x228 */ CBonusShot           mBonusShot;

    /* The ROM's constructor is almost entirely the members' own -- the object
     * lives in .data and is zero-filled -- but it ends by calling Init(), so
     * the camera is already in its reset state before main() runs. */
    CNPlyrCamera(void) { Init(); }

    /* Loads the two finder texture paks into the 2D texture region.  Runs once
     * for the life of the process -- mIsSetup is never cleared. */
    void SetUp(void);
    void Init(void);
    void Release(void);
    void Main(void);
    void Draw(void);

    /* Raised and cleared by finder.c as the finder comes up and down; the two
     * lock counters are held while an event owns the screen. */
    void FinderIn(void);
    void FinderOut(void);
    void DrawLock(void);
    void DrawUnlock(void);

    /* Hides the capture ring for iCnt frames and then fades it back in --
     * the "you just took a shot" beat. */
    void InCircleDrawLock(int iCnt);

    /* Scales the finder's FOV: 1.0 is the lens's own field, and the zoom that
     * telegraphs a shutter chance drives it down from there.  finder_camera.c
     * multiplies the finder's 44.374-degree base by it every frame. */
    float GetFOVRate(void);

    /* Lens zoom telegraphs the shutter chance: in when one is available, out
     * when it lapses or finder mode ends. */
    void ReqZoomIn(void);
    void ReqZoomOut(void);

    /* Re-arms the shutter with the loaded film's charge time. */
    void ResetCharge(void);

    /* Kills the spirit-drain noise and holds the gauge off for 70 frames --
     * what a fired shot does to the sound the gauge was making. */
    void ReqNoiseUp(void);
    void ReqNoiseReset(void);

    /* Clears the per-frame flags on the two widgets that collect them. */
    void FrameReset(void);

    /* Post-shot readouts.  The old one is the plain damage/score number; the
     * new one adds the bonus breakdown and is what the shipped game shows. */
    void PhotoInfoDisp(int iDamage, int iScore, PHOTO_BONUS_SHOT bonus);
    void PhotoInfoDispNew(int iDamage, int iScore, BONUS_SHOT_SCORE bonus);

    /* Inline in the ROM: every call site in ingame.c expands to the tray store
     * followed by the read-modify-write of the flag word at 0x16c, with no call
     * to a CNPlyrCamera symbol.  types.txt lists the method, so the expansion is
     * this one function rather than two open-coded statements. */
    void SetBattleFlg(int flg)
    {
        eq_tray.SetBattleFlg(flg);
        mBattleFlg = (u_char)(flg != 0);
    }

    static void Init(CNPlyrCamera *self) { if (self) self->Init(); }
    static void ReqZoomIn(CNPlyrCamera *self) { if (self) self->ReqZoomIn(); }
    static void ReqZoomOut(CNPlyrCamera *self) { if (self) self->ReqZoomOut(); }
    static void Release(CNPlyrCamera *self) { if (self) self->Release(); }
    static void Main(CNPlyrCamera *self) { if (self) self->Main(); }
    static void Draw(CNPlyrCamera *self) { if (self) self->Draw(); }

private:
    /* Steps the lens zoom off the pad.  Called only from Main(). */
    void ZoomWork(void);

    /* The four pieces the spirit gauge is drawn on top of. */
    void DrawSpiritGageBase(int off_x, int off_y, int iAlpha, float fMasterScale);

    /* The ring the shot is framed in.  Drawn under CCenterCircle, which is the
     * coloured ring that reacts to what is inside it. */
    void CaptureCircleDraw(int fndr_mx, int fndr_my, int iAlpha);

    /* Where the finder has drifted to, as an offset from screen centre.  The
     * round-trip through short is the ROM's: it quantises the sub-pixel sway
     * so the whole HUD lands on the same integer offset. */
    void GetFinderMovePos(float *fx, float *fy);
};

extern CNPlyrCamera m_plyr_camera;  /* data 319ad8 */

/* The four saveable parts of the camera.  The save system registers these as
 * separate memory-card blocks, which is why they are four hooks rather than
 * one -- the upgrades and the film type are raw sub-objects, the equip tray
 * and the filament know which part of themselves to hand over. */
void m_plyr_cameraSetSaveEQ(MC_SAVE_DATA *save);
void m_plyr_cameraSetSavePowrUp(MC_SAVE_DATA *save);
void m_plyr_cameraSetSaveFilament(MC_SAVE_DATA *save);
void m_plyr_cameraSetSaveFilmType(MC_SAVE_DATA *save);

#endif /* _INGAME_PHOTO_M_PLYR_CAMERA_H */
