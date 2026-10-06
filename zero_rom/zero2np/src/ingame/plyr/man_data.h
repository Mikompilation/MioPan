/* ==========================================================================
 *  ingame/plyr/man_data.h
 *
 *  MAN_DATA -- the shared model-resource owner behind Mio (PLYR_PLYR_DATA,
 *  plyr_mdl.o) and Mayu (SIS_DATA, sis_mdl.o).  It holds one character model,
 *  one shadow model, one animation pak, one sound bank and one accessory item,
 *  and it drives them through the mmanage.o loader as a set:
 *
 *      Setup()   -> SetupIn()   request every piece that changed
 *      IsReady() -> ReadyIn()   poll them all, then InitIn() once they land
 *      Release() -> ReleaseIn() give them all back
 *
 *  Setup() and IsReady() are virtual and defined inline in the class, which is
 *  why the ROM emits them into .gnu.linkonce.t sections rather than into
 *  man_data.o's .text; every other member here is inline and fully expanded at
 *  its call sites, so none of them has a symbol of its own anywhere in the
 *  build.  The trailing marks are this header's own ROM line numbers, measured
 *  from the $LM/SOL pairs in symbols.txt -- the two linkonce bodies give
 *  18/19 and 22..31 directly, and plyr_mdl.o and sis_mdl.o between them pin
 *  every accessor (32 x75, 35 x2, 39 x2, 43 x2, 46/48/49/54, 58 x20).
 *  GetAnmNo() is declared by types.txt but nothing in the build expands it, so
 *  it has no line of its own.
 *
 *  PORT: three deviations, all forced and none observable.
 *    - The ROM lays the object out as man_data_draw_lock_cnt at 0x00 ... vtable
 *      pointer at 0x2c (GNU v2 puts the vptr last).  The host toolchain puts it
 *      first, so no offset here matches the ROM's.  Anything that memset()s or
 *      block-copies a live MAN_DATA would wipe the vptr; see the note in
 *      sis_mdl.c.
 *    - mpAcsMdl is `int` in the ROM and `void *` here: mmanageIsReadyItemMdl()
 *      stores a pointer through it, which does not fit a 32-bit int on a 64-bit
 *      host.  InitIn()/ReadyIn()'s parameters widen for the same reason.
 *    - MAN_DATA is a runtime C++ class; none of this layout is written into an
 *      SGD/ANM file, so nothing on disc depends on it.
 *
 *  Reconstructed from the Feb 6 2004 prototype (SLES_523.84).
 * ======================================================================== */

#ifndef _INGAME_PLYR_MAN_DATA_H
#define _INGAME_PLYR_MAN_DATA_H

#include "../../common/utility2.h"              /* PRINT_WARNING */
#include "../../graphics/motion/mdlwork.h"      /* ANI_CTRL, HeaderSection */

class MAN_DATA
{
public:
    int man_data_draw_lock_cnt;                 /* 0x00 in the ROM */

    MAN_DATA();

    virtual int Setup(int mdl_no, int anm_no, int bd_no, int smdl_no,
                      int iAcsNo)                                       /* 18 */
    {
        return SetupIn(mdl_no, anm_no, bd_no, smdl_no, iAcsNo);         /* 19 */
    }                                                                   /* 20 */

    /* Poll every outstanding request, and the frame they have all landed,
     * build the ANI_CTRLs.  Both subclasses override this to fold in a
     * resource of their own, and both repeat the InitIn() call. */
    virtual int IsReady()                                               /* 22 */
    {
        void *mdl_p;                                                    /* 23 */
        void *anm_p;                                                    /* 24 */
        void *smdl_p;                                                   /* 25 */

        int ret = ReadyIn(&mdl_p, &anm_p, &smdl_p);                     /* 26 */
        if (ret != 0)                                                   /* 27 */
        {
            InitIn(mdl_p, anm_p, smdl_p);                               /* 28 */
        }                                                               /* 29 */
        return ret;                                                     /* 30 */
    }                                                                   /* 31 */

    ANI_CTRL *GetAniCtrl() const { return mpAniCtrl; }                  /* 32 */

    ANI_CTRL *GetShadowAniCtrl() const { return mpShadowAniCtrl; }      /* 35 */

    void DrawLock() { man_data_draw_lock_cnt++; }                       /* 39 */

    void DrawUnlock() { man_data_draw_lock_cnt--; }                     /* 43 */

    int IsLocked() const                                                /* 46 */
    {
        if (man_data_draw_lock_cnt < 0)                                 /* 48 */
        {
            PRINT_WARNING("man_data_draw_lock_cnt < 0");                /* 49 */
        }

        return man_data_draw_lock_cnt != 0;                             /* 54 */
    }                                                                   /* 55 */

    int GetSndBankNo() const { return man_data_bank_no; }               /* 58 */

    void AccessoryDraw(int iAlpha);

protected:
    int GetAnmNo() const { return mAnmNo; }

    int  SetupIn(int mdl_no, int anm_no, int bd_no, int smdl_no, int iAcsNo);
    void InitIn(void *mdl_p, void *anm_p, void *smdl_p);
    int  ReadyIn(void **mdl_pp, void **anm_pp, void **smdl_pp);
    void ReleaseAnmIn();
    void ReleaseIn();
    void InitializeIn();

private:
    ANI_CTRL *mpAniCtrl;                        /* 0x04 */
    ANI_CTRL *mpShadowAniCtrl;                  /* 0x08 */
    void     *mpAcsMdl;                         /* 0x0c -- `int` in the ROM */
    int       mMdlNo;                           /* 0x10 */
    int       mSMdlNo;                          /* 0x14 */
    int       mAnmNo;                           /* 0x18 */
    int       mBDNo;                            /* 0x1c */
    int       mAcsNo;                           /* 0x20 */
    int       man_data_bank_no;                 /* 0x24 */

    /* 0x28, one storage word.  ReleaseAnmIn() reads bit 5 and then bit 0, and
     * InitializeIn() clears both with a single load/and/and/store. */
    unsigned int man_ready_anm_init : 1;        /* 0x28:0 */
    unsigned int man_ready_req_mdl  : 1;        /* 0x28:1 -- never read */
    unsigned int man_ready_req_smdl : 1;        /* 0x28:2 -- never read */
    unsigned int man_ready_req_anm  : 1;        /* 0x28:3 -- never read */
    unsigned int man_ready_req_bd   : 1;        /* 0x28:4 -- never read */
    unsigned int man_ready_collision : 1;       /* 0x28:5 */
};

/* Bone the accessory item rides on, indexed by the accessory's model number.
 * data 319de8, verified against the ROM. */
extern int aBoneLabelTbl[16];

/* Park one item SGD on a bone of pAniCtrl's skeleton and draw it. */
void ManItemSGDDraw(HeaderSection *l_hs, ANI_CTRL *pAniCtrl, int iItemLabel);

#endif /* _INGAME_PLYR_MAN_DATA_H */
