/* ==========================================================================
 *  ingame/enemy/fene_entry.c
 *
 *  The drifting-ghost entry controller.  Work() is a five-mode state machine
 *  run once a frame: it rolls a ghost set for the player's region and chapter,
 *  loads it, waits out a randomised timer, and then places the ghosts.
 *
 *  The ghosts themselves are ordinary jene_dat entries -- specifically the
 *  battle-reserved tail, [BATTLE_ENE_DAT_TOP, JENE_DAT_MAX).  That is why
 *  FuyuActReq() writes their spawn transform through SetBattleEneDatPos() /
 *  SetBattleEneDatRot() before asking enemy.c to act on them.
 *
 *  Data tables (aResionAreaTbl, aFuyuGhostTbl, aFuyuAppearTbl and the
 *  fene_dat* sets) were read out of SLES_523.84's .rodata / .sdata -- Ghidra
 *  does not show initialiser contents.  They are emitted at the end of the
 *  file; the ROM's first function sits at line 39, so they cannot have been
 *  written above it.
 *
 *  Reconstructed from the Feb 6 2004 prototype (SLES_523.84), fene_entry.o.
 * ======================================================================== */

#include "fene_entry.h"
#include "enemy.h"                              /* ENE_STATUS, Ene*Req      */
#include "enemy_dat.h"                          /* SetBattleEneDat*, bounds */
#include "../../common/utility.h"               /* GetRandValI              */
#include "../../common/utility2.h"              /* PRINT_ASSERT             */
#include "../../common/variable.h"              /* ingame_wrk               */
#include "../../common/zero2_util.h"            /* GetRandomPositionXZ      */
#include "../plyr/player.h"                     /* IsPlayerInBattle, plyr_wrk */
#include "../plyr/plyr_mdl.h"                   /* plyr_mdlGetMATRIX        */
#include "../ingame.h"                          /* ingame_wrk               */
#include "../../graphics/graph3d/g3ddbg.h"      /* G3DASSERT                */
#include "../../sdk/libvu0.h"

#include <cfloat>                               /* FLT_MAX                  */

/* Timer bases, in frames (60fps): the entry wait is 3600 + a 0..3600 draw, the
 * step-out-ahead wait a flat 9000. */
#define FENE_WAIT_BASE  3600
#define FENE_WAIT_FRONT 9000

/* How far ahead of the player a FENE_APPEAR_FRONT ghost is placed, and the
 * ring GetRandomPositionXZ() scatters the rest of a set into. */
#define FENE_FRONT_DIST 500.0f
#define FENE_SCATTER_MAX 2000.0f
#define FENE_SCATTER_MIN 1000.0f

/* Player mode that permits a step-out-ahead appearance -- the ghost may only
 * cut in front while the player is walking. */
#define PLYR_MODE_WALK 6

int dbg_random_ghost = 1;

int FUYU_GHOST_ONE_DATA::mAppearNum;

/* --------------------------------------------------------------------------
 *  FUYU_GHOST_ONE_DATA -- one set of up to ONE_SET_MAX ghosts
 *
 *  Every method below walks the set the same way: labels outside the battle
 *  range are skipped, and the walk stops as soon as a label repeats one
 *  already seen.  The tables pad a short set by repeating its last label, so
 *  that repeat is the terminator.
 * ----------------------------------------------------------------------- */

void FUYU_GHOST_ONE_DATA::Init(void)
{
    mAppearNum = ONE_SET_MAX;                                           /* 39 */
}

void FUYU_GHOST_ONE_DATA::SetAppearNum(int iAppearNum)
{
    G3DASSERT((iAppearNum >= 0 && iAppearNum <= ONE_SET_MAX), "");      /* 43 */
    mAppearNum = iAppearNum;                                            /* 44 */
}

int FUYU_GHOST_ONE_DATA::FuyuIsReady(void) const
{
    int ret = 1;                                                        /* 47 */

    for (int i = 0; i < mAppearNum; i++) {                              /* 48 */
        if ((BATTLE_ENE_DAT_TOP <= mGhostLabel[i]) &&
            (mGhostLabel[i] < JENE_DAT_MAX)) {                          /* 49 */
            if (GetEneDatStatus(0, mGhostLabel[i]) != ENE_STATUS_READY) {
                ret = 0;                                                /* 50 */
            }
        }
        for (int j = i + 1; j < mAppearNum; j++) {                      /* 60 */
            if (mGhostLabel[i] == mGhostLabel[j]) {                     /* 61 */
                return ret;                                             /* 63 */
            }
        }
    }

    return ret;                                                         /* 66 */
}

int FUYU_GHOST_ONE_DATA::FuyuIsDead(void) const
{
    int ret = 1;                                                        /* 68 */

    for (int i = 0; i < mAppearNum; i++) {                              /* 69 */
        if ((BATTLE_ENE_DAT_TOP <= mGhostLabel[i]) &&
            (mGhostLabel[i] < JENE_DAT_MAX)) {                          /* 70 */
            if (GetEneDatStatus(0, mGhostLabel[i]) != ENE_STATUS_NO_USE) {
                ret = 0;                                                /* 71 */
            }
        }
        for (int j = i + 1; j < mAppearNum; j++) {                      /* 82 */
            if (mGhostLabel[i] == mGhostLabel[j]) {                     /* 84 */
                return ret;                                             /* 85 */
            }
        }
    }

    return ret;                                                         /* 87 */
}

int FUYU_GHOST_ONE_DATA::FuyuLoadReq(void) const
{
    int ret = 1;                                                        /* 89 */

    for (int i = 0; i < mAppearNum; i++) {                              /* 90 */
        if ((BATTLE_ENE_DAT_TOP <= mGhostLabel[i]) &&
            (mGhostLabel[i] < JENE_DAT_MAX)) {                          /* 92 */
            if (EneLoadReq(0, mGhostLabel[i], nullptr) != 0) {          /* 93 */
                ret = 0;
            }
            printf("%d ", mGhostLabel[i]);                              /* 98 */
        }
        for (int j = i + 1; j < mAppearNum; j++) {                      /* 103 */
            if (mGhostLabel[i] == mGhostLabel[j]) {                     /* 105 */
                return ret;                                             /* 106 */
            }
        }
    }

    return ret;                                                         /* 108 */
}

void FUYU_GHOST_ONE_DATA::FuyuActReq(sceVu0FVECTOR *pFirstEnePos) const
{
    float Pos[4];
    float PlyrPos[4];

    for (int i = 0; i < mAppearNum; i++) {                              /* 111 */
        if ((BATTLE_ENE_DAT_TOP <= mGhostLabel[i]) &&
            (mGhostLabel[i] < JENE_DAT_MAX)) {                          /* 112 */
            /* Only the first ghost of a set gets the caller's position; the
             * rest are scattered into a ring around the player so a group
             * does not arrive stacked on one spot. */
            if ((i == 0) && (pFirstEnePos != nullptr)) {                /* 119 */
                SetBattleEneDatPos(mGhostLabel[0], *pFirstEnePos);      /* 120 */
            }
            else {
                GetPlayerPos(PlyrPos);                                  /* 123 */
                GetRandomPositionXZ(Pos, PlyrPos,
                                    FENE_SCATTER_MAX, FENE_SCATTER_MIN); /* 124 */
                SetBattleEneDatPos(mGhostLabel[i], Pos);                /* 126 */
            }
            SetBattleEneDatRot(mGhostLabel[i], 1000);                   /* 130 */
            EneActReq(0, mGhostLabel[i]);                               /* 133 */
            printf("%d ", mGhostLabel[i]);                              /* 134 */
        }
        for (int j = i + 1; j < mAppearNum; j++) {                      /* 143 */
            if (mGhostLabel[i] == mGhostLabel[j]) {                     /* 144 */
                return;                                                 /* 148 */
            }
        }
    }
}

void FUYU_GHOST_ONE_DATA::FuyuReleaseReq(void) const
{
    for (int i = 0; i < mAppearNum; i++) {                              /* 150 */
        if ((BATTLE_ENE_DAT_TOP <= mGhostLabel[i]) &&
            (mGhostLabel[i] < JENE_DAT_MAX)) {                          /* 151 */
            EneReleaseReq(0, mGhostLabel[i]);                           /* 153 */
        }
        for (int j = i + 1; j < mAppearNum; j++) {                      /* 157 */
            if (mGhostLabel[i] == mGhostLabel[j]) {                     /* 159 */
                return;                                                 /* 160 */
            }
        }
    }
}

/* --------------------------------------------------------------------------
 *  CFEneEntry
 * ----------------------------------------------------------------------- */

void CFEneEntry::Work(void)
{
    float PlyrPos[4];

    /* Suppressed for this area, by a scripted lock, or by the debug switch:
     * bank the remaining wait so the timer resumes where it left off. */
    int bLockFlg = (mAreaLockFlg.IsUp(mNowAreaNo) || (mLockCnt != 0) ||
                    (dbg_random_ghost == 0));                          /* 179 */

    if (bLockFlg) {                                                    /* 186 */
        mWaitSave = mWaitCnt.Get();                                    /* 187 */
        Release();                                                     /* 188 */
        return;                                                        /* 189 */
    }

    switch (mMode) {                                                   /* 194 */
    case FENE_MODE_ENTRY:
        if (mMultiEnemyDisable != 0) {                                 /* 198 */
            FUYU_GHOST_ONE_DATA::SetAppearNum(1);                      /* 199 */
        }
        else {
            FUYU_GHOST_ONE_DATA::Init();                               /* 201 */
        }

        mpFD = GetPbyRand(mNowAreaNo, ingame_wrk.mChapterNo);          /* 205 */

        if (mpFD->FuyuLoadReq() == 0) {                                /* 208 */
            printf("FuyuEne Memory Fail\n");                           /* 209 */
            Release();                                                 /* 211 */
            return;                                                    /* 212 */
        }

        /* Reset() first: Wait() will not restart a counter that is still
         * running, and the banked mWaitSave has to win here. */
        mWaitCnt.Reset();
        mWaitCnt.Wait(mWaitSave + 1);

        {
            int iRandVal = GetRandValI(100);                           /* 227 */

            if (iRandVal < 40) {                                       /* 228 */
                mApparType = FENE_APPEAR_CAMCHG;                       /* 229 */
            }
            else if (iRandVal < 70) {                                  /* 231 */
                mApparType = FENE_APPEAR_FRONT;                        /* 232 */
            }
            else {
                mApparType = FENE_APPEAR_ANY;                          /* 236 */
            }
        }

        mMode = FENE_MODE_WAIT;                                        /* 239 */
        break;                                                         /* 241 */

    case FENE_MODE_WAIT:
        if (IsPlayerInBattle()) {                                      /* 245 */
            break;
        }
        if (mWaitCnt.Work()) {
            if (mApparType == FENE_APPEAR_FRONT) {                     /* 248 */
                mMode = FENE_MODE_FRONT;                               /* 249 */
                mWaitCnt.Wait(FENE_WAIT_FRONT);
            }
            else if (mApparType == FENE_APPEAR_CAMCHG) {               /* 252 */
                mMode = FENE_MODE_CAMCHG;                              /* 253 */
                CamChangeFlg(0);                                       /* 254 */
                mWaitCnt.Wait(FENE_WAIT_BASE);
            }
            else {
                mpFD->FuyuActReq(nullptr);                             /* 265 */
                mWaitSave = GetRandValI(FENE_WAIT_BASE) + FENE_WAIT_BASE;
                mMode = FENE_MODE_ACT;
            }
        }
        break;

    case FENE_MODE_FRONT:
        if (IsPlayerInBattle()) {                                      /* 269 */
            break;
        }
        if (mWaitCnt.Work()) {                                         /* 271 */
            /* Waited long enough -- give up on the staged entrance. */
            mpFD->FuyuActReq(nullptr);                                 /* 276 */
        }
        else {
            float Mtx[4][4];
            sceVu0FVECTOR Pos;

            if (plyr_wrk.cmn_wrk.mode != PLYR_MODE_WALK) {             /* 279 */
                break;
            }

            /* FENE_FRONT_DIST along the player's facing axis. */
            plyr_mdlGetMATRIX(Mtx, 0);                                 /* 284 */
            sceVu0Normalize(Pos, Mtx[2]);                              /* 285 */
            sceVu0ScaleVector(Pos, Pos, FENE_FRONT_DIST);              /* 286 */
            sceVu0AddVector(Pos, Pos, Mtx[3]);                         /* 287 */

            mpFD->FuyuActReq(&Pos);                                    /* 290 */
        }
        mWaitSave = GetRandValI(FENE_WAIT_BASE) + FENE_WAIT_BASE;      /* 291 */
        mMode = FENE_MODE_ACT;                                         /* 293 */
        break;                                                         /* 296 */

    case FENE_MODE_CAMCHG:
        /* No break: whichever way this mode goes it falls into the
         * FENE_MODE_ACT dead check in the same frame.  That is the ROM's
         * structure -- both early-outs below branch straight into case 4. */
        if (!IsPlayerInBattle()) {                                     /* 300 */
            if (mWaitCnt.Work()) {                                     /* 302 */
                /* Ran out of patience waiting for a cut. */
                mpFD->FuyuActReq(nullptr);                             /* 307 */
                mWaitSave = GetRandValI(FENE_WAIT_BASE) + FENE_WAIT_BASE;
                mMode = FENE_MODE_ACT;
            }
            else if (mCamChangeFlg != 0) {                          /* 310 */
                GetPlayerPos(PlyrPos);                                 /* 314 */
                mpFD->FuyuActReq(
                    GetNearestAppearPos(mNowAreaNo, PlyrPos));         /* 315 318 */
                mWaitSave = GetRandValI(FENE_WAIT_BASE) + FENE_WAIT_BASE; /* 319 */
                mMode = FENE_MODE_ACT;                                 /* 321 */
            }
        }
        /* FALLTHROUGH */

    case FENE_MODE_ACT:
        if (mpFD->FuyuIsDead()) {                                      /* 325 */
            Release();                                                 /* 327 */
        }
        break;

    default:
        break;
    }
}                                                                      /* 334 */

void CFEneEntry::Init(void)
{
    FUYU_GHOST_ONE_DATA::Init();                                       /* 338 */
    mMultiEnemyDisable = 0;                                            /* 339 */
    mpFD = nullptr;                                                    /* 340 */
    mLockCnt = 0;                                                      /* 341 */
    mWaitSave = GetRandValI(FENE_WAIT_BASE) + FENE_WAIT_BASE;          /* 342 */
    mWaitCnt.Wait(mWaitSave);
    mAreaLockFlg.AllDown();
    mMode = FENE_MODE_ENTRY;                                           /* 345 */
    mLockCnt = 0;                                                      /* 346 */
}

void CFEneEntry::CamChangeFlg(int bSwitch)
{
    mCamChangeFlg = bSwitch;                                        /* 351 */
}

void CFEneEntry::MultiAppearDisable(void)
{
    /* Changing the group setting invalidates whatever is already out. */
    if (mMultiEnemyDisable == 0) {                                     /* 354 */
        Release();                                                     /* 355 */
    }
    mMultiEnemyDisable = 1;                                            /* 357 */
}

void CFEneEntry::MultiAppearEnable(void)
{
    if (mMultiEnemyDisable != 0) {                                     /* 360 */
        Release();                                                     /* 361 */
    }
    mMultiEnemyDisable = 0;                                            /* 363 */
}

void CFEneEntry::AreaChange(int iNewAreaNo, int iOldAreaNo)
{
    int iNewResionId = GetResionId(iNewAreaNo);                        /* 368 */
    int iOldResionId = GetResionId(iOldAreaNo);                        /* 369 */

    G3DASSERT((iNewAreaNo >= 0 && iNewAreaNo < AREA_MAX_NUM), "");     /* 371 */

    mNowAreaNo = (char)iNewAreaNo;                                     /* 375 */

    /* Same region -- the set stays resident across the door. */
    if (iNewResionId != iOldResionId) {                                /* 376 */
        Release();
    }
}

void CFEneEntry::Lock(void)
{
    mLockCnt++;                                                        /* 382 */
}

void CFEneEntry::Unlock(void)
{
    mLockCnt--;                                                        /* 386 */
}

int CFEneEntry::Release(void)
{
    int ret = 0;                                                       /* 392 */

    if (mpFD != nullptr) {                                             /* 393 */
        mpFD->FuyuReleaseReq();                                        /* 398 */
        mpFD = nullptr;                                                /* 400 */
        ret = 1;
    }
    mMode = FENE_MODE_ENTRY;                                           /* 402 */

    return ret;                                                        /* 403 */
}

FUYU_GHOST_ONE_DATA *CFEneEntry::GetPbyRand(int iAreaNo, int iChapterNo)
{
    int iResionId = GetResionId(iAreaNo);                              /* 409 */
    FUYU_GHOST_DATA *pFD = &aFuyuGhostTbl[iResionId][iChapterNo];      /* 411 */

    return pFD->pOne + GetRandValI(pFD->iNum);                         /* 413 */
}

int CFEneEntry::GetResionId(int iAreaNo)
{
    G3DASSERT((iAreaNo >= 0 && iAreaNo < AREA_MAX_NUM), "");           /* 417 */

    /* aResionAreaTbl holds each region's last area number, ascending. */
    for (int i = 0; i < RESION_MAX_NUM; i++) {                         /* 418 */
        if (iAreaNo <= aResionAreaTbl[i]) {                            /* 419 */
            return i;                                                  /* 421 */
        }
    }

    PRINT_ASSERT("AreaNo Is Illegal");                                 /* 423 */

    return 0;                                                          /* 425 */
}

sceVu0FVECTOR *CFEneEntry::GetNearestAppearPos(int iNowAreaNo, float *PlyrPos)
{
    sceVu0FVECTOR *ret = nullptr;                                      /* 429 */
    float fSquare = FLT_MAX;                                           /* 430 */
    float Vec[4];

    for (int i = 0; i < APPEAR_POS_MAX; i++) {                         /* 437 */
        if (IsValidPosData(aFuyuAppearTbl[iNowAreaNo][i])) {           /* 438 */
            sceVu0SubVector(Vec, PlyrPos, aFuyuAppearTbl[iNowAreaNo][i]); /* 443 */
            float fTemp = sceVu0InnerProduct(Vec, Vec);                /* 447 */

            if (fTemp < fSquare) {                                     /* 448 */
                fSquare = fTemp;                                       /* 449 */
                ret = &aFuyuAppearTbl[iNowAreaNo][i];                  /* 450 */
            }
        }
    }

    return ret;                                                        /* 468 */
}

/* --------------------------------------------------------------------------
 *  Tables
 *
 *  A ghost set is three labels; unused slots hold 249 (below the battle range,
 *  so skipped) and a repeated label ends the walk.  fene_datNN_* is named for
 *  the 1-based chapter it serves, so fene_dat02_* sits at chapter index 1.
 * ----------------------------------------------------------------------- */

static FUYU_GHOST_ONE_DATA fene_dat0[1] =
{
    { { 249, 249, 249 } }
};

static FUYU_GHOST_ONE_DATA fene_dat02_osaka[4] =
{
    { { 250, 249, 249 } },
    { { 252, 249, 249 } },
    { { 250, 251, 249 } },
    { { 250, 252, 249 } }
};

static FUYU_GHOST_ONE_DATA fene_dat02_outdoor[6] =
{
    { { 250, 251, 249 } },
    { { 250, 252, 249 } },
    { { 253, 254, 249 } },
    { { 253, 255, 249 } },
    { { 254, 255, 249 } },
    { { 253, 254, 255 } }
};

static FUYU_GHOST_ONE_DATA fene_dat02_kiryu[1] =
{
    { { 259, 249, 249 } }
};

static FUYU_GHOST_ONE_DATA fene_dat02_kureha[1] =
{
    { { 258, 249, 249 } }
};

static FUYU_GHOST_ONE_DATA fene_dat05_kurosawa[6] =
{
    { { 262, 249, 249 } },
    { { 260, 261, 249 } },
    { { 260, 262, 249 } },
    { { 263, 264, 249 } },
    { { 275, 276, 249 } },
    { { 276, 277, 249 } }
};

static FUYU_GHOST_ONE_DATA fene_dat05_outdoor[5] =
{
    { { 269, 249, 249 } },
    { { 271, 249, 249 } },
    { { 266, 267, 249 } },
    { { 267, 268, 249 } },
    { { 266, 267, 268 } }
};

static FUYU_GHOST_ONE_DATA fene_dat05_osaka[4] =
{
    { { 269, 249, 249 } },
    { { 271, 249, 249 } },
    { { 269, 270, 249 } },
    { { 269, 271, 249 } }
};

static FUYU_GHOST_ONE_DATA fene_dat05_kureha[3] =
{
    { { 265, 249, 249 } },
    { { 263, 264, 249 } },
    { { 264, 265, 249 } }
};

static FUYU_GHOST_ONE_DATA fene_dat07_kiryu[3] =
{
    { { 278, 249, 249 } },
    { { 279, 249, 249 } },
    { { 280, 281, 249 } }
};

static FUYU_GHOST_ONE_DATA fene_dat08_outdoor[5] =
{
    { { 285, 249, 249 } },
    { { 286, 249, 249 } },
    { { 282, 283, 249 } },
    { { 283, 284, 249 } },
    { { 282, 283, 284 } }
};

static FUYU_GHOST_ONE_DATA fene_dat08_kiryu[3] =
{
    { { 287, 249, 249 } },
    { { 288, 249, 249 } },
    { { 289, 290, 249 } }
};

static FUYU_GHOST_ONE_DATA fene_dat08_tachibana[3] =
{
    { { 287, 249, 249 } },
    { { 288, 249, 249 } },
    { { 293, 249, 249 } }
};

static FUYU_GHOST_ONE_DATA fene_dat08_osaka[5] =
{
    { { 285, 249, 249 } },
    { { 286, 249, 249 } },
    { { 285, 286, 249 } },
    { { 291, 249, 249 } },
    { { 292, 249, 249 } }
};

static FUYU_GHOST_ONE_DATA fene_dat08_kureha[3] =
{
    { { 296, 249, 249 } },
    { { 294, 295, 249 } },
    { { 294, 295, 296 } }
};

static FUYU_GHOST_ONE_DATA fene_dat09_outdoor[5] =
{
    { { 300, 249, 249 } },
    { { 301, 249, 249 } },
    { { 297, 298, 249 } },
    { { 298, 299, 249 } },
    { { 297, 298, 299 } }
};

static FUYU_GHOST_ONE_DATA fene_dat09_kurosawa[5] =
{
    { { 304, 305, 249 } },
    { { 302, 304, 249 } },
    { { 306, 307, 249 } },
    { { 309, 310, 311 } },
    { { 310, 311, 249 } }
};

/* Last area number of each region, ascending: outdoor, osaka, tachibana,
 * kiryu, kurosawa, kureha. */
short CFEneEntry::aResionAreaTbl[RESION_MAX_NUM] =
{
    12, 18, 29, 39, 54, 65
};

/* Which ghost sets each region offers in each chapter.  fene_dat0 is the empty
 * set -- every cell the drifting ghosts do not cover points at it. */
FUYU_GHOST_DATA CFEneEntry::aFuyuGhostTbl[RESION_MAX_NUM][CHAPTER_MAX_NUM] =
{
    /* resion 0 */
    { { fene_dat0, 1 }, { fene_dat02_outdoor, 6 }, { fene_dat0, 1 }, { fene_dat0, 1 }, { fene_dat05_outdoor, 5 }, { fene_dat0, 1 }, { fene_dat0, 1 }, { fene_dat08_outdoor, 5 }, { fene_dat09_outdoor, 5 }, { fene_dat0, 1 }, { fene_dat09_outdoor, 5 } },
    /* resion 1 */
    { { fene_dat0, 1 }, { fene_dat02_osaka, 4 }, { fene_dat0, 1 }, { fene_dat0, 1 }, { fene_dat05_osaka, 4 }, { fene_dat0, 1 }, { fene_dat0, 1 }, { fene_dat08_osaka, 5 }, { fene_dat08_osaka, 5 }, { fene_dat0, 1 }, { fene_dat0, 1 } },
    /* resion 2 */
    { { fene_dat0, 1 }, { fene_dat0, 1 }, { fene_dat0, 1 }, { fene_dat0, 1 }, { fene_dat0, 1 }, { fene_dat0, 1 }, { fene_dat0, 1 }, { fene_dat08_tachibana, 3 }, { fene_dat0, 1 }, { fene_dat0, 1 }, { fene_dat08_tachibana, 3 } },
    /* resion 3 */
    { { fene_dat0, 1 }, { fene_dat02_kiryu, 1 }, { fene_dat0, 1 }, { fene_dat0, 1 }, { fene_dat0, 1 }, { fene_dat0, 1 }, { fene_dat07_kiryu, 3 }, { fene_dat08_kiryu, 3 }, { fene_dat0, 1 }, { fene_dat0, 1 }, { fene_dat08_kiryu, 3 } },
    /* resion 4 */
    { { fene_dat0, 1 }, { fene_dat0, 1 }, { fene_dat0, 1 }, { fene_dat0, 1 }, { fene_dat05_kurosawa, 6 }, { fene_dat0, 1 }, { fene_dat0, 1 }, { fene_dat0, 1 }, { fene_dat09_kurosawa, 5 }, { fene_dat0, 1 }, { fene_dat0, 1 } },
    /* resion 5 */
    { { fene_dat0, 1 }, { fene_dat02_kureha, 1 }, { fene_dat0, 1 }, { fene_dat0, 1 }, { fene_dat05_kureha, 3 }, { fene_dat0, 1 }, { fene_dat0, 1 }, { fene_dat08_kureha, 3 }, { fene_dat0, 1 }, { fene_dat0, 1 }, { fene_dat0, 1 } },
};

/* Authored spawn points, three per area.  w == 0 marks an unused slot; areas
 * with no authored points are all zero and fall back to a scatter. */
float CFEneEntry::aFuyuAppearTbl[AREA_MAX_NUM][APPEAR_POS_MAX][4] =
{
    /* area  0 */ { { 7981.0f, 0.0f, 9311.0f, 1.0f }, { 724.0f, 77.0f, 11767.0f, 1.0f }, { 21113.0f, 1333.0f, 11617.0f, 1.0f } },
    /* area  1 */ { { 0.0f, 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f, 0.0f } },
    /* area  2 */ { { 22199.0f, 3000.0f, 23956.0f, 1.0f }, { 28637.0f, 3000.0f, 21012.0f, 1.0f }, { 13534.0f, -773.0f, 291.0f, 1.0f } },
    /* area  3 */ { { 0.0f, 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f, 0.0f } },
    /* area  4 */ { { 3490.0f, 3000.0f, 31011.0f, 1.0f }, { 11188.0f, 3000.0f, 23259.0f, 1.0f }, { 18875.0f, 5100.0f, 31932.0f, 1.0f } },
    /* area  5 */ { { 0.0f, 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f, 0.0f } },
    /* area  6 */ { { -7404.0f, 1827.0f, 27802.0f, 1.0f }, { -7239.0f, 1700.0f, 30365.0f, 1.0f }, { 1048.0f, 3002.0f, 30202.0f, 1.0f } },
    /* area  7 */ { { 0.0f, 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f, 0.0f } },
    /* area  8 */ { { -14485.0f, -3250.0f, 39658.0f, 1.0f }, { -11948.0f, -3250.0f, 40101.0f, 1.0f }, { -10574.0f, -2800.0f, 36942.0f, 1.0f } },
    /* area  9 */ { { -9735.0f, 1200.0f, 17937.0f, 1.0f }, { -13810.0f, 989.0f, 16531.0f, 1.0f }, { -5481.0f, 1753.0f, 17235.0f, 1.0f } },
    /* area 10 */ { { 0.0f, 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f, 0.0f } },
    /* area 11 */ { { 18795.0f, 5100.0f, 54880.0f, 1.0f }, { 18609.0f, 5100.0f, 47542.0f, 1.0f }, { 0.0f, 0.0f, 0.0f, 0.0f } },
    /* area 12 */ { { 29183.0f, 3000.0f, 17341.0f, 1.0f }, { 27216.0f, 3000.0f, 17820.0f, 1.0f }, { 0.0f, 0.0f, 0.0f, 0.0f } },
    /* area 13 */ { { 10226.0f, -250.0f, 4911.0f, 1.0f }, { 9204.0f, -250.0f, 3190.0f, 1.0f }, { 10463.0f, -500.0f, 2341.0f, 1.0f } },
    /* area 14 */ { { 5899.0f, 0.0f, 5901.0f, 1.0f }, { 254.0f, 0.0f, 5990.0f, 1.0f }, { 5563.0f, -1625.0f, 3583.0f, 1.0f } },
    /* area 15 */ { { 4728.0f, -250.0f, 1299.0f, 1.0f }, { 5066.0f, -250.0f, 5222.0f, 1.0f }, { 0.0f, 0.0f, 0.0f, 0.0f } },
    /* area 16 */ { { 3526.0f, -375.0f, 3507.0f, 1.0f }, { 2916.0f, -250.0f, 3985.0f, 1.0f }, { 1975.0f, -250.0f, 5375.0f, 1.0f } },
    /* area 17 */ { { 4434.0f, -52.0f, 755.0f, 1.0f }, { 1670.0f, -250.0f, 1548.0f, 1.0f }, { 0.0f, 0.0f, 0.0f, 0.0f } },
    /* area 18 */ { { 3534.0f, -1625.0f, 4336.0f, 1.0f }, { 1206.0f, -1625.0f, 3483.0f, 1.0f }, { 2121.0f, -1625.0f, 6472.0f, 1.0f } },
    /* area 19 */ { { 0.0f, 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f, 0.0f } },
    /* area 20 */ { { 10135.0f, -350.0f, 6913.0f, 1.0f }, { 8654.0f, -250.0f, 4832.0f, 1.0f }, { 0.0f, 0.0f, 0.0f, 0.0f } },
    /* area 21 */ { { 0.0f, 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f, 0.0f } },
    /* area 22 */ { { 15024.0f, -250.0f, 4034.0f, 1.0f }, { 10693.0f, -250.0f, 5045.0f, 1.0f }, { 11142.0f, -250.0f, 7163.0f, 1.0f } },
    /* area 23 */ { { 19377.0f, -250.0f, 7646.0f, 1.0f }, { 0.0f, 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f, 0.0f } },
    /* area 24 */ { { 22227.0f, -250.0f, 1651.0f, 1.0f }, { 17924.0f, -250.0f, 2713.0f, 1.0f }, { 0.0f, 0.0f, 0.0f, 0.0f } },
    /* area 25 */ { { 21040.0f, -250.0f, 12008.0f, 1.0f }, { 0.0f, 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f, 0.0f } },
    /* area 26 */ { { 19622.0f, -2250.0f, 4868.0f, 1.0f }, { 19266.0f, -250.0f, 5068.0f, 1.0f }, { 18122.0f, -2250.0f, 6243.0f, 1.0f } },
    /* area 27 */ { { 21183.0f, -2000.0f, 7953.0f, 1.0f }, { 19269.0f, -1750.0f, 10539.0f, 1.0f }, { 17704.0f, -250.0f, 12896.0f, 1.0f } },
    /* area 28 */ { { 10799.0f, -1750.0f, 6689.0f, 1.0f }, { 16110.0f, -2262.0f, 6204.0f, 1.0f }, { 15606.0f, -2279.0f, 4508.0f, 1.0f } },
    /* area 29 */ { { 12071.0f, -1750.0f, 3816.0f, 1.0f }, { 13938.0f, -1750.0f, 6022.0f, 1.0f }, { 0.0f, 0.0f, 0.0f, 0.0f } },
    /* area 30 */ { { 8454.0f, -1761.0f, 7521.0f, 1.0f }, { 0.0f, 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f, 0.0f } },
    /* area 31 */ { { 771.0f, -250.0f, 2725.0f, 1.0f }, { 4853.0f, -250.0f, 394.0f, 1.0f }, { 0.0f, 0.0f, 0.0f, 0.0f } },
    /* area 32 */ { { 2175.0f, -250.0f, 5751.0f, 1.0f }, { 226.0f, -250.0f, 5127.0f, 1.0f }, { 2507.0f, -250.0f, 3322.0f, 1.0f } },
    /* area 33 */ { { 3162.0f, -250.0f, 4263.0f, 1.0f }, { 4149.0f, -250.0f, 10943.0f, 1.0f }, { 5168.0f, -250.0f, 7934.0f, 1.0f } },
    /* area 34 */ { { 5240.0f, -250.0f, 13303.0f, 1.0f }, { 5894.0f, -250.0f, 11594.0f, 1.0f }, { 0.0f, 0.0f, 0.0f, 0.0f } },
    /* area 35 */ { { 7325.0f, -250.0f, 15084.0f, 1.0f }, { -150.0f, -250.0f, 14200.0f, 1.0f }, { 0.0f, 0.0f, 0.0f, 0.0f } },
    /* area 36 */ { { 8311.0f, -250.0f, 14672.0f, 1.0f }, { 0.0f, 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f, 0.0f } },
    /* area 37 */ { { 2594.0f, -1500.0f, 14291.0f, 1.0f }, { 3161.0f, -1999.0f, 13563.0f, 1.0f }, { 3120.0f, -2250.0f, 10495.0f, 1.0f } },
    /* area 38 */ { { 5775.0f, -2025.0f, 11597.0f, 1.0f }, { 10267.0f, -557.0f, 12236.0f, 1.0f }, { 0.0f, 0.0f, 0.0f, 0.0f } },
    /* area 39 */ { { 4158.0f, -1750.0f, 8380.0f, 1.0f }, { 5131.0f, -2250.0f, 9174.0f, 1.0f }, { 1427.0f, -250.0f, 10459.0f, 1.0f } },
    /* area 40 */ { { 8202.0f, -750.0f, -1635.0f, 1.0f }, { 13236.0f, -750.0f, 2405.0f, 1.0f }, { 8958.0f, -750.0f, 3194.0f, 1.0f } },
    /* area 41 */ { { 8700.0f, -1000.0f, 3910.0f, 1.0f }, { 8932.0f, -1000.0f, 9645.0f, 1.0f }, { 6963.0f, -1000.0f, 6736.0f, 1.0f } },
    /* area 42 */ { { 1040.0f, -1000.0f, 3458.0f, 1.0f }, { -280.0f, -1000.0f, 7621.0f, 1.0f }, { 3555.0f, -1000.0f, 3262.0f, 1.0f } },
    /* area 43 */ { { 2406.0f, -1000.0f, 9074.0f, 1.0f }, { 4851.0f, -1075.0f, 6877.0f, 1.0f }, { 2072.0f, -1075.0f, 6002.0f, 1.0f } },
    /* area 44 */ { { 6419.0f, -3000.0f, 9280.0f, 1.0f }, { 6433.0f, -2413.0f, 12366.0f, 1.0f }, { 5805.0f, -1000.0f, 10987.0f, 1.0f } },
    /* area 45 */ { { 8933.0f, -3000.0f, 8659.0f, 1.0f }, { 9659.0f, -1000.0f, 5902.0f, 1.0f }, { 17856.0f, -1000.0f, 3219.0f, 1.0f } },
    /* area 46 */ { { 21238.0f, -1025.0f, -576.0f, 1.0f }, { 21535.0f, -1000.0f, 3871.0f, 1.0f }, { 17945.0f, -1000.0f, 7308.0f, 1.0f } },
    /* area 47 */ { { 12237.0f, -2677.0f, 5872.0f, 1.0f }, { 15420.0f, -701.0f, 5826.0f, 1.0f }, { 16200.0f, 1000.0f, 5485.0f, 1.0f } },
    /* area 48 */ { { 9541.0f, 1000.0f, 4845.0f, 1.0f }, { 14183.0f, -1000.0f, 14162.0f, 1.0f }, { 16406.0f, -1000.0f, 18440.0f, 1.0f } },
    /* area 49 */ { { 17485.0f, -1000.0f, 19839.0f, 1.0f }, { 20443.0f, -1000.0f, 22458.0f, 1.0f }, { 27160.0f, 0.0f, 23958.0f, 1.0f } },
    /* area 50 */ { { 5625.0f, -3000.0f, 5304.0f, 1.0f }, { 2472.0f, -3000.0f, 5490.0f, 1.0f }, { 0.0f, 0.0f, 0.0f, 0.0f } },
    /* area 51 */ { { 3575.0f, -3125.0f, 905.0f, 1.0f }, { 3891.0f, -3125.0f, 1346.0f, 1.0f }, { 3976.0f, -3000.0f, 3781.0f, 1.0f } },
    /* area 52 */ { { 3471.0f, -3000.0f, 6699.0f, 1.0f }, { 7106.0f, -3000.0f, 6591.0f, 1.0f }, { 0.0f, 0.0f, 0.0f, 0.0f } },
    /* area 53 */ { { 14232.0f, 1500.0f, 6902.0f, 1.0f }, { 12893.0f, 1000.0f, 5214.0f, 1.0f }, { 11721.0f, -603.0f, 3203.0f, 1.0f } },
    /* area 54 */ { { 9440.0f, -3000.0f, 4316.0f, 1.0f }, { 8579.0f, -3000.0f, 6139.0f, 1.0f }, { 0.0f, 0.0f, 0.0f, 0.0f } },
    /* area 55 */ { { 1795.0f, 0.0f, 4673.0f, 1.0f }, { 543.0f, 0.0f, 7015.0f, 1.0f }, { 3130.0f, 0.0f, 1519.0f, 1.0f } },
    /* area 56 */ { { 0.0f, 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f, 0.0f } },
    /* area 57 */ { { 0.0f, 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f, 0.0f } },
    /* area 58 */ { { 0.0f, 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f, 0.0f } },
    /* area 59 */ { { 0.0f, 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f, 0.0f } },
    /* area 60 */ { { 0.0f, 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f, 0.0f } },
    /* area 61 */ { { 0.0f, 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f, 0.0f } },
    /* area 62 */ { { 0.0f, 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f, 0.0f } },
    /* area 63 */ { { 0.0f, 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f, 0.0f } },
    /* area 64 */ { { 0.0f, 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f, 0.0f } },
    /* area 65 */ { { 0.0f, 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f, 0.0f } },
};

/* --------------------------------------------------------------------------
 *  PORT DEVIATION -- no ROM counterpart.
 *
 *  mpFD is a live pointer into one of the static fene_dat* sets above, and
 *  fene_entrySetSave() hands the whole CFEneEntry to the card marshaller
 *  verbatim (block 47 of save_game_data[]).  On the PS2 that address is fixed
 *  by the link map, so writing it into the file and reading it back is sound.
 *  On the host the sets live in this executable's image, whose base moves with
 *  every run (ASLR) and every rebuild -- so a save written in one run restored
 *  a pointer that was stale by that delta, and the next Release() (an area
 *  change, a script lock, or ModelMemoryFree() under memory pressure) walked
 *  mGhostLabel[] off it inside FuyuReleaseReq().
 *
 *  Every mpFD comes from GetPbyRand(), which returns aFuyuGhostTbl[r][c].pOne
 *  plus a draw k, so the triple (r, c, k) names the set with no address in it
 *  at all.  That triple is what goes into the file, flattened to one index
 *  with 0 meaning "no set"; the block's size does not change.  Anything the
 *  tables do not cover decodes back to "no set" and resets the state machine
 *  to FENE_MODE_ENTRY, which is also what repairs the saves written before
 *  this existed -- they hold a raw 64-bit pointer in the slot.
 *
 *  Same family as SisMotionSavePtrFixup() in sister.c: an index rather than an
 *  EE address, because these tables live in the executable and not in the
 *  emulated EE RAM.  to_host == 0 converts for writing, 1 after reading.
 * ----------------------------------------------------------------------- */

/* Slots reserved per (region, chapter) cell.  The widest cell offers six sets
 * (fene_dat02_outdoor, fene_dat05_kurosawa), so ONE_SET_MAX -- which counts
 * ghosts in a set, not sets in a cell -- is not the stride to use here. */
#define FENE_SAVE_SET_STRIDE 16

void FeneEntrySavePtrFixup(int to_host)
{
    if (to_host == 0) {
        uintptr_t code = 0;

        for (int r = 0; r < RESION_MAX_NUM && code == 0; r++) {
            for (int c = 0; c < CHAPTER_MAX_NUM && code == 0; c++) {
                FUYU_GHOST_DATA *pFD = &CFEneEntry::aFuyuGhostTbl[r][c];

                for (int k = 0; k < pFD->iNum && k < FENE_SAVE_SET_STRIDE; k++) {
                    if (fene_entry.mpFD == pFD->pOne + k) {
                        code = (uintptr_t)((r * CHAPTER_MAX_NUM + c) *
                                           FENE_SAVE_SET_STRIDE + k + 1);
                        break;
                    }
                }
            }
        }

        fene_entry.mpFD = (FUYU_GHOST_ONE_DATA *)code;
        return;
    }

    {
        uintptr_t code = (uintptr_t)fene_entry.mpFD;

        fene_entry.mpFD = nullptr;

        if (code != 0) {
            uintptr_t idx  = code - 1;
            int       k    = (int)(idx % FENE_SAVE_SET_STRIDE);
            uintptr_t cell = idx / FENE_SAVE_SET_STRIDE;
            int       c    = (int)(cell % CHAPTER_MAX_NUM);
            uintptr_t r    = cell / CHAPTER_MAX_NUM;

            if (r < RESION_MAX_NUM && k < CFEneEntry::aFuyuGhostTbl[r][c].iNum) {
                fene_entry.mpFD = CFEneEntry::aFuyuGhostTbl[r][c].pOne + k;
            }
        }

        /* Without a set there is nothing for modes 1..4 to act on, and they all
         * dereference mpFD unguarded.  Park the machine where Release() parks
         * it so the next Work() rolls a fresh set. */
        if (fene_entry.mpFD == nullptr) {
            fene_entry.mMode = FENE_MODE_ENTRY;
        }
    }
}
