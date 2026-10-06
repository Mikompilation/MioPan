/* ==========================================================================
 *  ingame/enemy/fene_entry.h
 *
 *  The drifting-ghost ("fuyu ene") entry controller.  A background state
 *  machine that picks a ghost set for the player's current region and chapter,
 *  loads it, waits out a randomised timer and then makes it appear -- either
 *  in front of the player, at a hand-authored spawn point on a camera cut, or
 *  anywhere.  These are the ghosts that drift past while you are simply
 *  walking around, as opposed to the scripted encounters.
 *
 *  Reconstructed from the Feb 6 2004 prototype (SLES_523.84), fene_entry.o.
 * ======================================================================== */

#ifndef _INGAME_ENEMY_FENE_ENTRY_H
#define _INGAME_ENEMY_FENE_ENTRY_H

#include "eetypes.h"
#include "libvu0.h"                          /* sceVu0FVECTOR            */
#include "../../common/variable.h"           /* BIT_FLAGS, CWaitVariable */

/* Bounds.  AREA_MAX_NUM and ONE_SET_MAX are the ROM's own names -- they
 * survive inside the G3DASSERT condition strings in fene_entry.o's .rodata. */
#define AREA_MAX_NUM     66      /* one bit per area in mAreaLockFlg        */
#define RESION_MAX_NUM    6      /* the ROM's spelling of "region"          */
#define CHAPTER_MAX_NUM  11
#define ONE_SET_MAX       3      /* ghosts that can appear together         */
#define APPEAR_POS_MAX    3      /* authored spawn points per area          */

/* A ghost label below this is an empty slot.  The drifting ghosts live in the
 * battle-reserved tail of jene_dat (see enemy_dat.h), so a label is only real
 * when it falls inside [BATTLE_ENE_DAT_TOP, JENE_DAT_MAX); the tables pad
 * unused slots with 249 and repeat a label to mark the end of a set. */

/* One set of ghosts that appear together. */
struct FUYU_GHOST_ONE_DATA                  /* 0x6 */
{
    /* 0x0 */ short mGhostLabel[ONE_SET_MAX];

private:
    /* How many of the three slots are actually used this time round.  Static:
     * only ever one drifting set is live, so the ROM keeps it per-class. */
    static int mAppearNum;

public:
    /* Every one of these walks mGhostLabel[0 .. mAppearNum) and stops early at
     * the first label that repeats an earlier one -- that is how a short set
     * is terminated. */
    int  FuyuIsReady(void) const;
    int  FuyuIsDead(void) const;
    int  FuyuLoadReq(void) const;
    /* pFirstEnePos places the first ghost of the set; the rest are scattered
     * around the player.  NULL scatters all of them. */
    void FuyuActReq(sceVu0FVECTOR *pFirstEnePos) const;
    void FuyuReleaseReq(void) const;

    static void Init(void);
    static void SetAppearNum(int iAppearNum);
};

/* The set list for one (region, chapter) cell. */
struct FUYU_GHOST_DATA                      /* 0x8 */
{
    /* 0x0 */ FUYU_GHOST_ONE_DATA *pOne;
    /* 0x4 */ int                  iNum;
};

/* mMode.  The ROM stores a bare char and switches on it; these names are the
 * port's. */
enum FENE_MODE
{
    FENE_MODE_ENTRY   = 0,      /* pick a set and load it                   */
    FENE_MODE_WAIT    = 1,      /* idle until the appear timer runs out     */
    FENE_MODE_FRONT   = 2,      /* waiting to appear ahead of the player    */
    FENE_MODE_CAMCHG  = 3,      /* waiting for a camera cut to appear on    */
    FENE_MODE_ACT     = 4       /* on the field; watch for it dying         */
};

/* mApparType, rolled in FENE_MODE_ENTRY.  Names are the port's. */
enum FENE_APPEAR_TYPE
{
    FENE_APPEAR_FRONT  = 0,     /* 30% -- step out ahead of the player      */
    FENE_APPEAR_ANY    = 1,     /* 30% -- appear immediately, anywhere      */
    FENE_APPEAR_CAMCHG = 2      /* 40% -- wait for a camera cut             */
};

struct CFEneEntry                           /* 0x20 */
{
    /* 0x00 */ BIT_FLAGS<AREA_MAX_NUM> mAreaLockFlg;

private:
    /* 0x0c */ char mMode;
    /* 0x0d */ char mApparType;
    /* 0x0e */ char mNowAreaNo;
    /* 0x0f */ char mLockCnt;
    /* The ROM packs both flags into the byte at 0x10; u_char rather than the
     * declared type keeps the storage unit where the target put it. */
    /* 0x10:0 */ u_char mCamChangeFlg     : 1;
    /* 0x10:1 */ u_char mMultiEnemyDisable   : 1;
    /* 0x14 */ FUYU_GHOST_ONE_DATA *mpFD;
    /* 0x18 */ CWaitVariable<int> mWaitCnt;
    /* 0x1c */ int mWaitSave;

    static short           aResionAreaTbl[RESION_MAX_NUM];
    static FUYU_GHOST_DATA aFuyuGhostTbl[RESION_MAX_NUM][CHAPTER_MAX_NUM];
    static float           aFuyuAppearTbl[AREA_MAX_NUM][APPEAR_POS_MAX][4];

    /* An authored spawn point is live when its w component is non-zero.  Fully
     * inlined in the ROM -- it has no symbol of its own, but the debug info
     * still lists it as a member. */
    int IsValidPosData(const float *pPos) const { return pPos[3] != 0.0f; }

    sceVu0FVECTOR       *GetNearestAppearPos(int iNowAreaNo, float *PlyrPos);
    FUYU_GHOST_ONE_DATA *GetPbyRand(int iAreaNo, int iChapterNo);
    int                  GetResionId(int iAreaNo);

    /* PORT: mpFD is a live pointer and this whole object is a save block, so
     * it has to be encoded on the way out.  See the body in fene_entry.c. */
    friend void FeneEntrySavePtrFixup(int to_host);

public:
    void Init(void);
    void Work(void);

    /* Called from SetPlyrAreaNo() on every area change.  The drifting ghosts
     * are only released when the move crosses a *region* boundary -- moving
     * between areas of the same region keeps them resident. */
    void AreaChange(int iNewAreaNo, int iOldAreaNo);

    /* Raised by the map camera whenever it cuts to a different rectangle --
     * a cut is the only moment a drifting ghost can be placed without the
     * player seeing it pop in. */
    void CamChangeFlg(int bSwitch);

    /* Frees the drifting-ghost entry; non-zero when something was actually
     * released.  mmanage.c calls this first under memory pressure
     * (ModelMemoryFree). */
    int  Release(void);

    /* Counted, so overlapping events cannot unlock each other's suppression. */
    void Lock(void);
    void Unlock(void);

    /* Whether the drifting ghosts may appear in groups.  Disabled while the
     * sister is on screen -- EvSisDisp() drives both. */
    void MultiAppearEnable(void);
    void MultiAppearDisable(void);

    /* Null-guarded forwarders.  The decompiler renders every method call as
     * CFEneEntry::Method(&fene_entry, ...) and ev_macro.c was reconstructed
     * with that spelling; these keep those call sites compiling.  They are the
     * port's, not the ROM's -- the ROM has no null check here. */
    static void CamChangeFlg(CFEneEntry *self, int bSwitch) { if (self) self->CamChangeFlg(bSwitch); }
    static void AreaChange(CFEneEntry *self, int iNew, int iOld) { if (self) self->AreaChange(iNew, iOld); }
    static void Init(CFEneEntry *self) { if (self) self->Init(); }
    static void Work(CFEneEntry *self) { if (self) self->Work(); }
    static int  Release(CFEneEntry *self) { return self ? self->Release() : 0; }
    static void Lock(CFEneEntry *self) { if (self) self->Lock(); }
    static void Unlock(CFEneEntry *self) { if (self) self->Unlock(); }
    static void MultiAppearEnable(CFEneEntry *self) { if (self) self->MultiAppearEnable(); }
    static void MultiAppearDisable(CFEneEntry *self) { if (self) self->MultiAppearDisable(); }
};

/* ZERO2.MAP puts the instance itself in ingame.o's .data (0x318710), not in
 * fene_entry.o -- it is defined alongside ingame_wrk. */
extern CFEneEntry fene_entry;

/* PORT: converts CFEneEntry::mpFD between a live pointer into the static ghost
 * set tables and a process-independent index.  Called from mc_set_data.c
 * around the save-block marshalling; see the body in fene_entry.c. */
void FeneEntrySavePtrFixup(int to_host);

/* Debug-menu "RANDOM ENEMY" switch (dbg_menu_main).  Zero suppresses the
 * drifting ghosts entirely; the ROM ships it enabled. */
extern int dbg_random_ghost;        /* sdata 3f06d8 */

#endif /* _INGAME_ENEMY_FENE_ENTRY_H */
