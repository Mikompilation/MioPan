/* ==========================================================================
 *  ingame/plyr/man_data.c
 *
 *  MAN_DATA's out-of-line half: the five loader steps (SetupIn / ReadyIn /
 *  InitIn / ReleaseAnmIn / ReleaseIn), the constructor's InitializeIn(), and
 *  the accessory draw.  Everything else the class declares is inline in
 *  man_data.h and has no symbol anywhere in the build.
 *
 *  The object has no static data beyond aBoneLabelTbl: its .rodata is the
 *  fixed_array assert literal, AccessoryDraw's G3DASSERT strings and the
 *  "unsigned int*" type name, and its .sdata is the fixed_array boilerplate.
 *  globals.txt is right to list nothing else.
 *
 *  .text is accounted for byte-for-byte: 0x1d85d0..0x1d8cdc = 0x70c, being
 *  0x6e8 of bodies (four fixed_array boilerplate functions plus the nine
 *  listed here) and nine 4-byte alignment fills.  There is no unlisted body.
 *
 *  The trailing marks are the ROM's own line numbers, measured from the
 *  $LM/PROC pairs in symbols.txt rather than guessed.  Two things about them
 *  are worth knowing before trusting a gap:
 *
 *    - An insn GCC moves into a branch delay slot loses its line note, so
 *      every `ret = 1;` in SetupIn(), `int ret = 0;` itself, and
 *      `mAcsNo = iAcsNo;` are unmarked.  Their positions come from the gaps
 *      they exactly fill.
 *    - The ROM's brace style is K&R, so a function's opening line is its
 *      signature line.  The port follows this folder's Allman style, which
 *      means an annotation marks the statement, not the physical line.
 *
 *  Reconstructed from the Feb 6 2004 prototype (SLES_523.84).
 * ======================================================================== */

#include "man_data.h"

#include "../../graphics/mmanage.h"
#include "../../graphics/graph3d/g3ddbg.h"
#include "../../graphics/graph3d/gra3dSGD.h"
#include "../../graphics/graph3d/gra3dSGDData.h"
#include "../../graphics/motion/accessory.h"
#include "../../graphics/motion/mim.h"
#include "../../graphics/motion/motion.h"
#include "../../system/eeiop/sndbank.h"

/* Accessory model number -> bone index in the wearer's skeleton.  Entry 0
 * (the camera) rides bone 17, 1..5 ride bone 6, and everything from 6 up
 * rides bone 2.  data 319de8, byte-identical to the ROM. */
int aBoneLabelTbl[16] =
{
    17, 6, 6, 6, 6, 6,
    2, 2, 2, 2, 2, 2, 2, 2, 2, 2
};

/* --------------------------------------------------------------------------
 *  Construction
 * ----------------------------------------------------------------------- */

void MAN_DATA::InitializeIn()                                           /* 25 */
{
    man_data_bank_no = -1;                                              /* 26 */

    mpAniCtrl = mpShadowAniCtrl = NULL;                                 /* 29 */

    man_data_draw_lock_cnt = 0;                                         /* 32 */

    /* One chained store: the ROM materialises -1 once and spends it on all
     * five fields, which is what puts them on a single line. */
    mMdlNo = mSMdlNo = mAnmNo = mBDNo = mAcsNo = -1;                    /* 37 */

    man_ready_anm_init = man_ready_collision = 0;                       /* 40 */
}                                                                       /* 41 */

/* The ROM clears mpShadowAniCtrl here as well as in InitializeIn(); the
 * duplicate store is emitted, so it is the source's own. */
MAN_DATA::MAN_DATA()                                                    /* 43 */
{
    mpShadowAniCtrl = NULL;                                             /* 44 */
    InitializeIn();                                                     /* 45 */
}                                                                       /* 46 */

/* --------------------------------------------------------------------------
 *  Accessory drawing
 * ----------------------------------------------------------------------- */

/* Copy the bone's world matrix onto the item's root coordinate, re-derive the
 * rest of the item's skeleton from it, and draw. */
void ManItemSGDDraw(HeaderSection *l_hs, ANI_CTRL *pAniCtrl, int iItemLabel) /* 60 */
{
    SGDCOORDINATE *l_cp = l_hs->coordp;                                 /* 61 */
    HeaderSection *hs = pAniCtrl->base_p;                               /* 62 */
    SGDCOORDINATE *cp = hs->coordp;                                     /* 63 */

    sceVu0CopyMatrix(l_cp->matCoord,
                     cp[aBoneLabelTbl[iItemLabel]].matLocalWorld);      /* 65 */
    sgdCalcBoneCoordinate(l_cp, (int)l_hs->blocks - 1);                 /* 66 */
    _gra3dDrawSGD((SGDFILEHEADER *)l_hs, SRT_REALTIME, NULL, -1);       /* 67 */
}                                                                       /* 68 */

void MAN_DATA::AccessoryDraw(int iAlpha)                                /* 71 */
{
    HeaderSection *item;                                                /* 72 */

    if (mAcsNo >= 0)                                                    /* 73 */
    {
        /* The assert does not guard the draw: the ROM falls straight through
         * into it whether the model is resident or not.  The extra parens are
         * the source's own -- the stringized condition in .rodata reads
         * "(mmanageIsReadyItemMdl(mAcsNo, &mpAcsMdl))", which is also what
         * proves the loader's third parameter is defaulted.
         *
         * It can only fire on a caller bug: ReadyIn() ANDs the same test into
         * the answer IsReady() gives, so an accessory is always resident by
         * the time anything draws it. */
        G3DASSERT((mmanageIsReadyItemMdl(mAcsNo, &mpAcsMdl)),
                  "Not Ready Accessory Mdl");                           /* 74 */

        item = (HeaderSection *)GetItemSgdAddr((int *)mpAcsMdl);        /* 75 */
        ManmdlSetAlpha(item, (u_char)iAlpha);                           /* 76 */
        SendItemVram((u_int *)mpAcsMdl, 0);                             /* 77 */
        ManItemSGDDraw(item, mpAniCtrl, mAcsNo);                        /* 78 */
    }
}                                                                       /* 79 */

/* --------------------------------------------------------------------------
 *  Loader
 * ----------------------------------------------------------------------- */

/* Request whatever changed.  Returns non-zero if anything was newly asked
 * for, which is what tells the caller its old ANI_CTRLs are about to go. */
int MAN_DATA::SetupIn(int mdl_no, int anm_no, int bd_no, int smdl_no,
                      int iAcsNo)                                       /* 85 */
{
    int ret = 0;                                                        /* 86 */

    if (mAcsNo != iAcsNo)                                               /* 89 */
    {
        if (mAcsNo >= 0)                                                /* 90 */
        {
            mmanageClearItemMdl(mAcsNo);                                /* 91 */
        }
        mAcsNo = iAcsNo;                                                /* 93 */
        if (iAcsNo >= 0)                                                /* 94 */
        {
            mmanageReqItemMdl(iAcsNo);                                  /* 95 */
            ret = 1;                                                    /* 96 */
        }
    }

    /* The ROM writes this block without braces on the inner `if` and the next
     * one with them -- lines 100..106 leave no room for a closing brace where
     * 108..115 has one.  Kept as measured. */
    if (mMdlNo != mdl_no && mdl_no >= 0)                                /* 100 */
    {
        if (mMdlNo >= 0)                                                /* 101 */
            mmanageClearMdl(mMdlNo);                                    /* 102 */
        mMdlNo = mdl_no;                                                /* 103 */
        mmanageReqMdl(mdl_no);                                          /* 104 */
        ret = 1;                                                        /* 105 */
    }

    if (mSMdlNo != smdl_no && smdl_no >= 0)                             /* 108 */
    {
        if (mSMdlNo >= 0)                                               /* 109 */
        {
            mmanageClearMdl(mSMdlNo);                                   /* 110 */
        }
        mSMdlNo = smdl_no;                                              /* 112 */
        mmanageReqMdl(smdl_no);                                         /* 113 */
        ret = 1;                                                        /* 114 */
    }

    /* An animation change tears down the ANI_CTRLs built on the old one --
     * the models stay, so only this arm calls ReleaseAnmIn(). */
    if (mAnmNo != anm_no && anm_no >= 0)                                /* 117 */
    {
        ReleaseAnmIn();                                                 /* 118 */
        ret = 1;                                                        /* 119 */
        mAnmNo = anm_no;                                                /* 120 */
        mmanageReqAnm(anm_no);                                          /* 121 */
    }

    /* The bank is the odd one out: it does not touch `ret`, because nothing
     * downstream has to be rebuilt when only the voice bank changes. */
    if (mBDNo != bd_no && bd_no >= 0)                                   /* 127 */
    {
        if (man_data_bank_no != -1)                                     /* 128 */
            SndBankRelease(man_data_bank_no);                           /* 129 */
        mBDNo = bd_no;                                                  /* 130 */

        man_data_bank_no = SndBankNew(bd_no, bd_no - 1, -1);            /* 132 */
    }

    return ret;                                                         /* 142 */
}                                                                       /* 143 */

/* Build the ANI_CTRLs and hook the cloth/rope collision, once each.  Both
 * subclasses call this out of their own IsReady() override. */
void MAN_DATA::InitIn(void *mdl_p, void *anm_p, void *smdl_p)           /* 145 */
{
    if (man_ready_anm_init == 0)                                        /* 146 */
    {
        mpAniCtrl = (ANI_CTRL *)motInitOneEnemyAnm((u_int *)anm_p,
                                                   (u_int *)mdl_p,
                                                   (u_int)mMdlNo,
                                                   (u_int)mAnmNo);      /* 148 */

        mpShadowAniCtrl = (ANI_CTRL *)motInitOneEnemyAnm((u_int *)anm_p,
                                                         (u_int *)smdl_p,
                                                         (u_int)mSMdlNo,
                                                         (u_int)mAnmNo); /* 150 */
        man_ready_anm_init = 1;                                         /* 151 */
    }

    if (man_ready_collision == 0)                                       /* 154 */
    {
        acsSetEneCollision(mpAniCtrl, (u_short)mMdlNo);                 /* 156 */

        acsSetRopeCollision(mpAniCtrl, (u_short)mMdlNo);                /* 158 */
        man_ready_collision = 1;                                        /* 159 */
    }
}                                                                       /* 161 */

/* Poll every request.  `ret` starts at 1 and every piece ANDs its own answer
 * into it, so one outstanding load holds the whole set back; a piece that was
 * never asked for at all (number < 0) zeroes it outright.  The accessory is
 * the exception -- it is optional, so a negative number simply skips it. */
int MAN_DATA::ReadyIn(void **mdl_pp, void **anm_pp, void **smdl_pp)     /* 163 */
{
    int ret = 1;                                                        /* 164 */

    if (mAcsNo >= 0)                                                    /* 166 */
    {
        ret &= mmanageIsReadyItemMdl(mAcsNo, &mpAcsMdl);                /* 167 */
    }

    if (mMdlNo >= 0)                                                    /* 170 */
    {
        ret &= mmanageIsReadyMdl(mMdlNo, mdl_pp, 1);                    /* 171 */
    }
    else                                                                /* 172 */
    {
        ret = 0;                                                        /* 173 */
    }

    if (mSMdlNo >= 0)                                                   /* 176 */
    {
        ret &= mmanageIsReadyMdl(mSMdlNo, smdl_pp, 1);                  /* 177 */
    }
    else                                                                /* 178 */
    {
        ret = 0;                                                        /* 179 */
    }

    if (mAnmNo >= 0)                                                    /* 182 */
    {
        ret &= mmanageIsReadyAnm(mAnmNo, anm_pp, 1);                    /* 183 */
    }
    else                                                                /* 184 */
    {
        ret = 0;                                                        /* 185 */
    }

    if (mBDNo >= 0 && man_data_bank_no != -1)                           /* 189 */
    {
        ret &= SndBankIsReady(man_data_bank_no);                        /* 191 */
    }

    return ret;                                                         /* 194 */
}                                                                       /* 195 */

/* Give back the animation and everything derived from it.  Collision comes
 * off before the ANI_CTRLs go, since acsDel* dereference them. */
void MAN_DATA::ReleaseAnmIn()                                           /* 197 */
{
    if (man_ready_collision != 0)                                       /* 198 */
    {
        acsDelEneCollision(mpAniCtrl);                                  /* 200 */

        acsDelRopeCollision(mpAniCtrl);                                 /* 202 */
        man_ready_collision = 0;                                        /* 203 */
    }

    if (man_ready_anm_init != 0)                                        /* 206 */
    {
        mimClearAllVertex(mpAniCtrl);                                   /* 208 */

        acsResetCloth(mpAniCtrl);                                       /* 210 */

        motReleaseOneAnm(mpAniCtrl);                                    /* 213 */
        motReleaseOneAnm(mpShadowAniCtrl);                              /* 214 */
        man_ready_anm_init = 0;                                         /* 215 */

        mpAniCtrl = mpShadowAniCtrl = NULL;                             /* 217 */
    }

    if (mAnmNo >= 0)                                                    /* 220 */
    {
        mmanageClearAnm(mAnmNo);                                        /* 221 */
        mAnmNo = -1;                                                    /* 222 */
    }
}                                                                       /* 224 */

void MAN_DATA::ReleaseIn()                                              /* 227 */
{
    if (mAcsNo >= 0)                                                    /* 228 */
    {
        mmanageClearItemMdl(mAcsNo);                                    /* 229 */
        mAcsNo = -1;                                                    /* 230 */
    }

    if (mMdlNo >= 0)                                                    /* 233 */
    {
        mmanageClearMdl(mMdlNo);                                        /* 234 */
        mMdlNo = -1;                                                    /* 235 */
    }
    if (mSMdlNo >= 0)                                                   /* 237 */
    {
        mmanageClearMdl(mSMdlNo);                                       /* 238 */
        mSMdlNo = -1;                                                   /* 239 */
    }

    /* The ROM clears the bank handle before the bank number, sharing the one
     * -1 it materialised; the store order is the source's. */
    if (mBDNo >= 0)                                                     /* 242 */
    {
        SndBankRelease(man_data_bank_no);                               /* 243 */
        man_data_bank_no = -1;                                          /* 244 */
        mBDNo = -1;                                                     /* 245 */
    }

    ReleaseAnmIn();                                                     /* 248 */
}                                                                       /* 249 */
