/* ==========================================================================
 *  ingame/photo/n_equip_tray.h
 *
 *  The camera's equip tray: the three sub-function slots the viewfinder shows
 *  down its left edge, the spirit power they are charged from, and the stock
 *  items that charge buys.
 *
 *  CNEquipTraySave is the half that goes to the memory card (which functions
 *  are fitted, at what level, and how much power is banked); CNEquipTrayWrk
 *  wraps it with the per-frame animation state and is what CNPlyrCamera holds.
 *  Both were parked in m_plyr_camera.h until this module was reconstructed;
 *  the ROM declares them here, which is what the inline accessors below prove.
 *
 *  Those three inlines are the only n_equip_tray.h line numbers the stabs
 *  carry -- 171, 175 and 185 -- and they appear exactly where a
 *  "which function is selected", a "what does it cost" and a "what level is
 *  it" would.  The mapping of number to inline is inferred from that; the
 *  bodies themselves are read off the expansions and are not in doubt.
 *
 *  Reconstructed from the Feb 6 2004 prototype (SLES_523.84), n_equip_tray.o.
 * ======================================================================== */

#ifndef _INGAME_PHOTO_N_EQUIP_TRAY_H
#define _INGAME_PHOTO_N_EQUIP_TRAY_H

#include "eetypes.h"
#include "../../common/save_data.h" /* MC_SAVE_DATA */
#include "../../common/variable.h"  /* CVariable / CFVariable / SHUTTER_CHANCE_STATE */
#include "../../graphics/graph3d/ctl/fixed_array.h"

struct ENE_WRK;

/* The camera's sub-functions.  This is the index for equip_func_tbl[], for
 * CNEquipTraySave::mSubFuncLv[] and for CCameraPowerUp::mSubFuncGem[], and the
 * value CNEquipTraySave::mEquipFunc[] holds per slot.  NONE is a fitted-but-
 * empty slot, so 0 doubles as "nothing here". */
enum CAMERA_SUB_FUNC_ENUM
{
    CAMERA_SUB_FUNC_NONE  = 0,
    CAMERA_SUB_FUNC_SLOW  = 1,
    CAMERA_SUB_FUNC_STOP  = 2,
    CAMERA_SUB_FUNC_VIEW  = 3,
    CAMERA_SUB_FUNC_TRACE = 4,
    CAMERA_SUB_FUNC_SEAL  = 5,
    CAMERA_SUB_FUNC_KOKU  = 6,
    CAMERA_SUB_FUNC_ZERO  = 7,
    CAMERA_SUB_FUNC_METSU = 8,
    CAMERA_SUB_FUNC_REN   = 9,
    SUB_FUNC_NUM          = 10
};

/* One row of CNEquipTrayWrk::equip_func_tbl, indexed by CAMERA_SUB_FUNC_ENUM.
 * `pk2_no` is the n_finder_dat[] sprite for the slot icon -- 24 is the "empty
 * slot" art, which is why every draw tests for it rather than for a zero
 * function number. */
typedef struct                      /* 0xc */
{
    /* 0x0 */ int    need_stock_num;
    /* 0x4 */ int    pk2_no;
    /* 0x8 */ u_char r;
    /* 0x9 */ u_char g;
    /* 0xa */ u_char b;
    /* 0xb */ char   bHitBack;
} NEQUIP_FUNC_DAT;

/* The per-level parameter rows, one table per sub-function.  All four levels
 * are always present; index them with CNEquipTrayWrk::GetNowSubFuncLv(). */
typedef struct                      /* 0x2 */
{
    /* 0x0 */ char EffTime;
    /* 0x1 */ char SearchFlg;
} SUB_FUNC_SI_PARAM;

typedef struct                      /* 0x1 */
{
    /* 0x0 */ char EffTime;
} SUB_FUNC_SUB_PARAM;

typedef struct                      /* 0x3 */
{
    /* 0x0 */ char EffTime;
    /* 0x1 */ char StopTime;
    /* 0x2 */ char ActTime;
} SUB_FUNC_MAHI_PARAM;

/* The damage functions.  NDmgMag is the multiplier for an ordinary shot and
 * SDmgMag for one taken in a shutter chance; SlowHTTime / SlowHTRate are the
 * knock-back window SetEneSlowHitBack() is given. */
typedef struct                      /* 0x10 */
{
    /* 0x0 */ float NDmgMag;
    /* 0x4 */ float SDmgMag;
    /* 0x8 */ int   SlowHTTime;
    /* 0xc */ float SlowHTRate;
} SUB_FUNC_KOUGEKI_PARAM;

/* SUB_FUNC_SEAL_PARAM is declared in the ROM's debug info and is the same one
 * byte as SUB_FUNC_SUB_PARAM; sub_func_seal_param[] is typed as the latter, so
 * only that one is carried here. */

/* --------------------------------------------------------------------------
 *  The saved half.  This is the block m_plyr_cameraSetSaveEQ() hands to the
 *  memory card, and the reason CNEquipTrayWrk keeps it first and separate.
 * ------------------------------------------------------------------------ */
struct CNEquipTraySave              /* 0x20 */
{
    /* 0x00 */ fixed_array<CVariable<char, 0, 3>, 10> mSubFuncLv;
    /* 0x0a */ CVariable<char, 0, 3> mStockGrade;
    /* 0x0b */ fixed_array<CVariable<char, 0, 9>, 3> mEquipFunc;
    /* 0x0e */ CVariable<char, 0, 2> mSlctNo;
    /* 0x10 */ int   mDispGage;
    /* 0x14 */ int   mBankGage;
    /* 0x18 */ int   mStockNum;
    /* 0x1c */ float mAbsorbMultiRate;

    /* How many stock items the tray can hold, by mStockGrade. */
    static char aStockMaxTbl[4];                    /* sdata 3f34d0 */

    static SUB_FUNC_SI_PARAM      sub_func_si_param[4];     /* sdata 3f34b0 */
    static SUB_FUNC_MAHI_PARAM    sub_func_mahi_param[4];   /* data  338878 */
    static SUB_FUNC_SUB_PARAM     sub_func_oso_param[4];    /* sdata 3f34b8 */
    static SUB_FUNC_SUB_PARAM     sub_func_seal_param[4];   /* sdata 3f34c0 */
    static SUB_FUNC_SUB_PARAM     sub_func_trace_param[4];  /* sdata 3f34c8 */
    static SUB_FUNC_KOUGEKI_PARAM sub_func_rei_param[4];    /* data  338888 */
    static SUB_FUNC_KOUGEKI_PARAM sub_func_koku_param[4];   /* data  3388c8 */
    static SUB_FUNC_KOUGEKI_PARAM sub_func_metsu_param[4];  /* data  338908 */
    static SUB_FUNC_KOUGEKI_PARAM sub_func_ren_param[4];    /* data  338948 */

    void Init(void);
    static void Init(CNEquipTraySave *self) { if (self) self->Init(); }

    /* Banks the shot's remaining suction power as the finder closes. */
    void End(int iRemainParticle);

    /* Adds banked power and converts every whole 1000 of it into a stock item;
     * non-zero when at least one came out. */
    int  ConvertGage2StockNum(int iGage);
    void Absorb(int power);

    int  IsStockMax(void);

    /* Steps mSlctNo on to the next *fitted* slot, wrapping; returns how far it
     * moved, which is what tells the tray which way to slide the icons. */
    int  NextFuncSet(void);

    void ResetGage(void);
    void PushGage(int *iGageDat);
    void PopGage(int *iGageDat);
};

/* --------------------------------------------------------------------------
 *  The runtime half.
 *
 *  The five flags are typed `unsigned int` in the debug info, which on the host
 *  puts the storage unit at 0x7c and overlaps mNowOffset.  u_short reproduces
 *  both ROM facts -- flags in the halfword at 0x7e, sizeof 0x80.
 * ------------------------------------------------------------------------ */
struct CNEquipTrayWrk               /* 0x80 */
{
    /* 0x00 */ CNEquipTraySave mSave;
    /* 0x20 */ int   mRemainParticleNum;
    /* 0x24 */ CFadeVariable<float> mFOV;
    /* 0x30 */ CBlinkVariable<char, 0, 127> mStockAnm;
    /* 0x32 */ CBlinkVariable<char, 0, 100> mSubFuncAnmShot;
    /* 0x34 */ CBlinkSwitchVariable<u_char, 50, 100, 1, 0> mSubFuncAnmBlink;
    /* 0x36 */ CBlinkSwitchVariable<u_char, 0, 60, 20, 0> mSubFuncAnmBlinkNoSetup;
    /* 0x38 */ CFadeVariable<char> mGageAnmAlpha;
    /* 0x3c */ int   mMode;
    /* 0x40 */ float mRot;
    /* 0x44 */ float mRotSpd;
    /* 0x48 */ int   mRemainTime;
    /* 0x4c */ CWrkVariable<short, 0, 128> mFcs;
    /* 0x50 */ CWrkVariable<short, 20, 128> mRenzMarkBlink;
    /* 0x54 */ CBlinkSwitchVariable<short, 20, 60, 15, 0> mAccumulateBollFlare;
    /* 0x58 */ CFVariable mAccumulateBollRot;
    /* 0x64 */ CWrkVariable<short, 0, 76> mRenzMarkAlpha;
    /* 0x68 */ CWrkVariable<char, 0, 100> mSuckMouthScale;
    /* 0x6a */ CWrkVariable<short, 0, 128> mSuckMouthAlpha;
    /* 0x6e */ CFadeVariable<short> mPreSlctAlpha;
    /* 0x74 */ int   mPreSlctYOffset;
    /* 0x78 */ CFadeVariable<short> mNowOffset;
    /* 0x7e:0 */ u_short mSetupFlg  : 1;
    /* 0x7e:1 */ u_short mGageUpFlg : 1;
    /* 0x7e:2 */ u_short mBattleFlg : 1;
    /* 0x7e:3 */ u_short mShotAble  : 1;
    /* 0x7e:4 */ u_short mFirstFlg  : 1;

    /* One row per CAMERA_SUB_FUNC_ENUM. */
    static NEQUIP_FUNC_DAT equip_func_tbl[10];      /* rdata 3c1c68 */

    void Init(void);
    void End(void);
    void Work(void);
    void Reset(void);

    void Draw(int fndr_mx, int fndr_my, int master_alp);
    void RenzMarkDraw(int fndr_mx, int fndr_my, int master_alp);

    /* Puts a sub-function on the shutter: SetUp() spends nothing but arms the
     * slot (and refuses if the stock is short), Use2() spends the stock and
     * PutOut() takes it back down.  Rotate() steps the selector. */
    int  SetUp(void);
    int  IsSetUp(void);
    int  Use2(void);
    void PutOut(void);
    void Rotate(void);

    /* The burst-shot mark over the capture ring. */
    void RenzMarkOn(void);
    void RenzMarkOff(void);

    /* SetEffectsPre() arms the once-per-shot effects before the damage sweep;
     * SetEffect() commits the fitted function against one ghost. */
    void  SetEffectsPre(void);
    void  SetEffect(ENE_WRK *ew, SHUTTER_CHANCE_STATE SState);
    float GetDmgRate(SHUTTER_CHANCE_STATE SState);
    int   IsHitBackON(void);
    int   IsChargeResetOK(void);
    int   IsMoving(void);

    /* AbsorbImmediately() banks power that never became particles;
     * SetRemainParticle() registers the ones that did, so Work() can bank them
     * as EneDmgParticleSuctionNumGet() reports them landing. */
    void AbsorbImmediately(int power);
    void SetRemainParticle(int iParticleNum);
    void FinderConvertGage2StockNum(int iAddGage);

    float GetTargetFOV(void);

    void SetSave(MC_SAVE_DATA *save);
    void SetBattleFlg(int iFlg);
    void GetSubFuncArray(char *equip_func);
    void SetSubFuncArray(char *equip_func);
    void SetAbsorbMultiRate(float fRate);

    /* ---- the header's own inlines --------------------------------------
     * n_equip_tray.o carries exactly three n_equip_tray.h line numbers and
     * these are the three one-liners they belong to. */

    /* Which sub-function is on the shutter right now. */
    int GetNowSubFuncNo(void)                                                /* 171 */
    {
        return mSave.mEquipFunc[mSave.mSlctNo].Get();
    }

    /* What it costs to fire, in stock items. */
    int EquipTrayFuncCostNum(void)                                           /* 175 */
    {
        return equip_func_tbl[GetNowSubFuncNo()].need_stock_num;
    }

    /* Its upgrade level, which indexes every sub_func_*_param table. */
    int GetNowSubFuncLv(void)                                                /* 185 */
    {
        return mSave.mSubFuncLv[GetNowSubFuncNo()].Get();
    }

private:
    int  IsReady(void);
    void BaseDraw(int fndr_mx, int fndr_my, int master_alp);
    void AccumulaterDraw(int fndr_mx, int fndr_my, int master_alp);
    int  GetNowEquipFuncNum(void);

public:
    /* Null-tolerant wrappers, for the call sites that reach the tray through a
     * possibly-absent camera. */
    static void Init(CNEquipTrayWrk *self) { if (self) self->Init(); }
    static void End(CNEquipTrayWrk *self) { if (self) self->End(); }
    static void SetBattleFlg(CNEquipTrayWrk *self, int flg) { if (self) self->SetBattleFlg(flg); }
    static void SetSubFuncArray(CNEquipTrayWrk *self, char *equip_func)
    { if (self) self->SetSubFuncArray(equip_func); }
    static void SetAbsorbMultiRate(CNEquipTrayWrk *self, float fRate)
    { if (self) self->SetAbsorbMultiRate(fRate); }
};

/* Debug-menu "STOCK_NUM" row (dbg_menu_main): the tray's stock count, mirrored
 * out here so the menu can edit it.  Work() copies it back into mSave every
 * frame, which is what makes the row live. */
extern int dbg_stock_num;           /* sdata 3f3508 */

#endif /* _INGAME_PHOTO_N_EQUIP_TRAY_H */
