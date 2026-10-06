/* ==========================================================================
 *  graphics/motion/motion.c
 *
 *  Skeletal animation: decoding "MOTN" motion files, evaluating a frame onto a
 *  model's bone hierarchy, and interpolating between poses.
 *
 *  A MOTN file is laid out as
 *
 *      +0x00  u_char file_id[4]     'MOTN'
 *      +0x04  u_int  map_flg        1 once frame offsets are self-relative
 *      +0x08  u_int  bone_num
 *      +0x0c  u_int  trans_num
 *      +0x10  u_int  frame_num
 *      +0x14  u_int  interp_frame
 *      +0x18  u_int  flg            bit 2: an RST packet precedes the frames
 *      +0x1c  u_int  si_frame
 *      +0x20  MOT_ID_TABLE[]        { parent_id, trans_id } per bone
 *      +0xa0  u_int  frame_addr[]   one offset per frame (see motAddressMapping)
 *
 *  Frame 0 stores full RST triples (rot/scale/trans per bone); later frames
 *  store rotations for every bone followed by translations for only those
 *  bones whose trans_id is not 0xff.
 *
 *  Reconstructed: the MOTN accessors and frame decode, hierarchy wiring,
 *  ANI_CODE playback, pose interpolation, named-bone accessors, inverse
 *  kinematics, the ANI_CTRL pool and the model/animation init paths.
 *
 *  Reconstructed from the Feb 6 2004 prototype (SLES_523.84).
 * ======================================================================== */

#include "motion.h"

#include "../graph3d/g3dxVu0.h"        /* g3dxVu0CopyVector (lq/sq) */
#include "../graph3d/g3ddbg.h"          /* G3DASSERT */
#include "../graph3d/g3dGeom.h"         /* g3dMatrixInverseTransform */
#include "../graph3d/gra3dConst.h"      /* g_matConvertSI2PS */
#include "../graph3d/gra3dSGDData.h"    /* sgdCalcBoneCoordinate */
#include "anicode.h"                    /* motAniCodeRead, motGetNextMotion */
#include "mim.h"                        /* mimInitMimeCtrl, mimInitAcsCtrl */
#include "accessory.h"                  /* acsInitCloth */
#include "mdldat.h"                     /* manmdl_dat, anm_tbl */
#include "../../sdk/libvu0.h"
#include "../../common/ol_load.h"       /* ol_loadGetHeap / ol_loadFreeHeap */
#include "../../common/packfile.h"      /* GetFileInPak */
#include "../../common/utility2.h"      /* PRINT_ASSERT */
#include "../../main/glob.h"            /* plyr_wrk */
#include "../../system/os/system.h"     /* GetPALMode */
#include "../../ingame/map/RegDat.h"    /* RegDatGetBuffID */
#include "../../ingame/map/map_height.h" /* MhGetMapHeight */

#include <math.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>

/* Largest skeleton the frame buffers are sized for. */
#define BONE_MAX        0x3c

/* MOTN magic, little-endian 'M','O','T','N'. */
#define MOT_FILE_MAGIC  0x4e544f4d

/* The frame-address table starts here (in words) unless an RST packet is
 * present, in which case bone_num * 0x18 words of it come first. */
#define MOT_ADDR_TABLE_TOP   0x28
#define MOT_RST_PACKET_WORDS 0x18

/* Array-overrun assert text, Shift-JIS in the original:
 * "配列オーバーアクセス"
 * = "array over-access". */
#define MOT_STR_ARRAY_OVER  "配列オーバーアクセス"

MIME_CTRL mim_chodo[20];
u_char mim_chodo_se[20];
MIME_DAT mim_cdat[2][20];
/* Entry types found in a model or animation pack.  GetFileInPak returns a
 * pointer to the payload; the word three back holds the type. */
#define PAK_DAT_TYPE(p)   ((p)[-3])

#define ANM_DAT_MOT       0   /* motion pack   */
#define ANM_DAT_MIME      1   /* face morph    */
#define ANM_DAT_BGMIME    2   /* background morph */
#define ANM_DAT_MTOP      3   /* morph table   */
#define MDL_DAT_TANM      4   /* texture animation */
#define MDL_DAT_MPK       5   /* the model itself  */
#define MDL_DAT_PK2       6
#define MDL_DAT_BWC       7   /* bone weight cache */

/* Where a character model's textures are uploaded in GS VRAM. */
#define ENE_VRAM_TOP 0x2bc0

/* Size of the ANI_CTRL pool. */
#define ANI_CTRL_MAX 50

ANI_CTRL ani_mdl[ANI_CTRL_MAX];
ANI_MDL_CTRL ani_mdl_ctrl[50];
ACS_ALPHA plyracs_ctrl[2];
ROPE_CTRL rope_ctrl[20];
CMOVE_CTRL cmove_ctrl[10];
ENE_VRAM_CTRL ene_vram_ctrl[4];
ENE_VRAM_CTRL ene_vram_bak[4];
MSN_PLYR_INIT plyr_init_ctrl;
char plyr_mdl_no;
u_char mim_mepati_id;
u_char mim_nigiri_l_id;
u_char mim_nigiri_r_id;
float now_frot_x;

/* Shared pose scratch.  The original keeps these as one file-scope pair plus
 * per-function statics rather than stack buffers -- BONE_MAX 4x4 matrices is
 * far too much to put on the EE stack. */
fixed_array<float[4][4], BONE_MAX> m_start;
fixed_array<float[4][4], BONE_MAX> m_end;

static void motSetInterpMatrix(ANI_CTRL *ani_ctrl, float (*start)[4][4], float (*end)[4][4]);
static void motInterpAnm(ANI_CTRL *ani_ctrl, float (*start)[4][4], float (*end)[4][4]);
static void motSceneInterpAnm(ANI_CTRL *ani_ctrl, float (*start)[4][4], float (*end)[4][4],
                              RST_DATA *rst0, RST_DATA *rst1, float rate);
static void motInitNewMotion(ANI_CTRL *ani_ctrl);
static u_char motAddFrame(MOT_CTRL *m_ctrl);

/* --------------------------------------------------------------------------
 *  MOTN header accessors
 * ------------------------------------------------------------------------ */
static u_int motGetBoneNum(u_int *mot_p)
{
    return mot_p[2];
}

static u_int motGetFrameNum(u_int *mot_p)
{
    return mot_p[4];
}

static u_int motGetSIFrameNum(u_int *mot_p)
{
    return mot_p[7];
}

static u_int motGetInterpFrameNum(u_int *mot_p)
{
    /* Catches a small integer passed where a file pointer was expected -- the
     * ROM tests the pointer against 0x1000 as a signed int, not against NULL,
     * because a stale play_id landing in the low scratch range is the failure
     * this is really guarding against. */
    if (!mot_p)                                                           /* 1919 */
    {
        PRINT_ASSERT("motGetInterpFrameNum Odd mot_p %p", mot_p); /* 1920 */
    }

    return mot_p[5];                                                      /* 1923 */
}

/* Bit 2 of flg: the file carries an RST packet ahead of the frame table. */
static u_int motCheckIncludeRstPacket(u_int *mot_p)
{
    return (mot_p[6] >> 2) & 1;
}

static u_int motGetParentId(u_int *top_addr, u_int id)
{
    return (u_int)*((u_char *)top_addr + id * 2 + 0x20);
}

static u_int motGetTransId(u_int *top_addr, u_int id)
{
    return (u_int)*((u_char *)top_addr + id * 2 + 0x21);
}

/* Start of the per-frame address table. */
static MOT_ADDR_TABLE *motGetAddrTable(u_int *top_addr)
{
    u_int *p = top_addr;

    if (motCheckIncludeRstPacket(top_addr) != 0)
    {
        p += motGetBoneNum(top_addr) * MOT_RST_PACKET_WORDS;
    }

    return (MOT_ADDR_TABLE *)(p + MOT_ADDR_TABLE_TOP);
}

/* --------------------------------------------------------------------------
 *  motAddressMapping
 *
 *  The original 32-bit EE code replaced every file-relative frame offset with
 *  an absolute pointer in place.  A 64-bit pointer cannot fit in that
 *  four-byte slot, so the host port instead changes it to a signed 32-bit
 *  offset relative to the slot itself.  MOTSELF32::get() then reconstructs
 *  the full host pointer without changing the file layout.
 * ------------------------------------------------------------------------ */
static void motAddressMapping(u_int *top_addr)
{
    u_int           frame_num;
    MOT_ADDR_TABLE *table;
    u_int           i;

    frame_num = motGetFrameNum(top_addr);
    table = motGetAddrTable(top_addr);

    if (*top_addr != MOT_FILE_MAGIC)
    {
        printf("This file isn't mot format\n");
        return;
    }

    if (top_addr[1] == 1)
    {
        return;
    }

    for (i = 0; i < frame_num; i++)
    {
        const u_int file_offset = (u_int)table[i].frame_addr.raw();
        u_int      *frame_addr = (u_int *)((uintptr_t)top_addr +
                                           (uintptr_t)file_offset);
        intptr_t    relative   = (intptr_t)frame_addr -
                                  (intptr_t)&table[i].frame_addr;

        /*
         * The four-byte table entry is retained, but it now contains a
         * displacement from its own address rather than a truncated host
         * pointer.  Both objects belong to this MOT file, so a value outside
         * the signed 32-bit range means the file/table calculation is wrong.
         */
        G3DASSERT(relative >= INT32_MIN && relative <= INT32_MAX,
                  "MOT frame offset is outside self-relative range "
                  "(frame:%u, offset:0x%08x)", i, file_offset);
        if (relative < INT32_MIN || relative > INT32_MAX)
        {
            table[i].frame_addr.setRaw(0);
            continue;
        }
        table[i].frame_addr.setRaw((int)relative);
    }

    top_addr[1] = 1;
}

static u_int *motGetFrameDataAddr(u_int *top_addr, u_int frame)
{
    return motGetAddrTable(top_addr)[frame].frame_addr.get();
}

/* --------------------------------------------------------------------------
 *  motGetFrameDataRST
 *
 *  Frame 0 form: nine floats per bone -- rotation, scale, translation.
 * ------------------------------------------------------------------------ */
static void motGetFrameDataRST(RST_DATA *rst, u_int *top_addr, u_int frame)
{
    float *p;
    u_int  bone_num;
    u_int  i;

    if (frame >= motGetFrameNum(top_addr))
    {
        frame = motGetFrameNum(top_addr) - 1;
    }

    p = (float *)motGetFrameDataAddr(top_addr, frame);
    bone_num = motGetBoneNum(top_addr);

    G3DASSERT(bone_num <= BONE_MAX, MOT_STR_ARRAY_OVER);

    for (i = 0; i < bone_num; i++)
    {
        rst[i].rot[0]   = p[0];
        rst[i].rot[1]   = p[1];
        rst[i].rot[2]   = p[2];
        rst[i].scale[0] = p[3];
        rst[i].scale[1] = p[4];
        rst[i].scale[2] = p[5];
        rst[i].trans[0] = p[6];
        rst[i].trans[1] = p[7];
        rst[i].trans[2] = p[8];
        p += 9;
    }
}

static void motGetFrameData(RST_DATA *rst, u_int *top_addr, u_int frame)
{
    if (frame == 0)
    {
        motGetFrameDataRST(rst, top_addr, 0);
        return;
    }

    motGetFrameDataRT(rst, top_addr, frame, 0);
}

/* --------------------------------------------------------------------------
 *  ANI_CTRL pool
 *
 *  ani_mdl[] is a fixed pool of animation-control blocks.  A slot is claimed by
 *  setting use, and identified afterwards by the model number it was bound to.
 * ------------------------------------------------------------------------ */
void motInitANI_CTRL(void)
{
    int i;

    printf("sizeof(ani_mdl) = %zu\n", sizeof(ani_mdl));

    for (i = 0; i < ANI_CTRL_MAX; i++)
    {
        motClearANI_CTRL(&ani_mdl[i]);
        ani_mdl[i].use = 0;
    }
}

void motFreeANI_CTRL(ANI_CTRL *ani_ctrl)
{
    motClearANI_CTRL(ani_ctrl);
    ani_ctrl->use = 0;
    ani_ctrl->pkt_p = (void *)nullptr;
}

/* Reset every pointer a freshly-bound ANI_CTRL owns, leaving the pool
 * bookkeeping (use / mdl_no / anm_no) alone. */
void motInitClearAniCtrl(ANI_CTRL *ani_ctrl)
{
    ani_ctrl->mdl_p      = (u_int *)0;
    ani_ctrl->pk2_p      = (u_int *)0;
    ani_ctrl->mpk_p      = (u_int *)0;
    ani_ctrl->base_p     = (HeaderSection *)0;
    ani_ctrl->mtop       = (u_int *)0;
    ani_ctrl->mdat       = (u_int *)0;
    ani_ctrl->tanm_p     = (u_int *)0;
    ani_ctrl->bwc_p      = (u_int *)0;
    ani_ctrl->mim        = (MIME_CTRL *)0;
    ani_ctrl->bgmim      = (MIME_CTRL *)0;
    ani_ctrl->wmim       = (WMIM_CTRL *)0;
    ani_ctrl->mot_num    = 0;
    ani_ctrl->mim_num    = 0;
    ani_ctrl->bg_num     = 0;
    ani_ctrl->wmim_num   = 0;
    ani_ctrl->ftype      = 0;
    ani_ctrl->interp_flg = 1;
}

ANI_CTRL *motGetANI_CTRL(void)
{
    for (int i = 0; i < ANI_CTRL_MAX; i++)
    {
        if (ani_mdl[i].use == 0)
        {
            printf("motGetANI_CTRL i = %d\n", i);
            ani_mdl[i].use = 1;
            return &ani_mdl[i];
        }
    }

    PRINT_ASSERT("There is No Vacant ANI_CTRL");
    return (ANI_CTRL *)nullptr;
}

/* Tear a bound slot right back down: the morph and cloth state has to be
 * dropped before the work area under it is handed back to the load heap. */
void motReleaseOneAnm(void *ani_hndl)
{
    if (ani_hndl != nullptr)                                        /* 124 */
    {
        /* The ROM logs pointers with %x; the host ones do not fit in 32 bits,
         * so these prints use %p throughout -- as motClearANI_CTRL does. */
        printf("motReleaseOneAnm ani_ctrl = %p\n", ani_hndl);       /* 128 */

        acsResetCloth((ANI_CTRL *)ani_hndl);                        /* 130 */
        mimClearAllVertex((ANI_CTRL *)ani_hndl);                    /* 131 */
        mimInitWeight((ANI_CTRL *)ani_hndl);                        /* 132 */

        motInitAniCtrlFree((ANI_CTRL *)ani_hndl);                   /* 134 */
        motFreeANI_CTRL((ANI_CTRL *)ani_hndl);                      /* 136 */
    }
}

/* Put a still-bound model back into its rest state.  Unlike motReleaseOneAnm
 * the slot keeps its work area and stays claimed -- only the accumulated
 * morph/cloth deformation is dropped. */
void motResetMdl(int mdl_no)
{
    ANI_CTRL *ani_ctrl;

    ani_ctrl = motSearchANI_CTRL(mdl_no);                           /* 145 */
    if (ani_ctrl != nullptr)                                        /* 146 */
    {
        acsResetCloth(ani_ctrl);                                    /* 148 */
        mimClearAllVertex(ani_ctrl);                                /* 149 */
        mimInitWeight(ani_ctrl);                                    /* 150 */
    }
}

/* --------------------------------------------------------------------------
 *  motInitOneEnemyAnm
 *
 *  Claim a pool slot for a character and bind the model/animation pair to it.
 *  motResetMdl runs first because the same model number may still be holding a
 *  slot from a previous room; the slot is released again if the bind fails, so
 *  a failed load cannot leak an entry out of a pool of only 50.
 * ------------------------------------------------------------------------ */
void *motInitOneEnemyAnm(u_int *anm_p, u_int *mdl_p, u_int mdl_no, u_int anm_no)
{
    ANI_CTRL *ani_ctrl;

    motResetMdl((int)mdl_no);                                       /* 157 */

    ani_ctrl = motGetANI_CTRL();                                    /* 162 */

    if (motInitAniCtrlMalloc(ani_ctrl, anm_p, mdl_p,                /* 163 */
                             mdl_no, anm_no) == (u_int *)0)
    {
        motFreeANI_CTRL(ani_ctrl);                                  /* 164 */
        return (void *)0;                                           /* 165 */
    }

    return ani_ctrl;                                                /* 168 */
}

/* --------------------------------------------------------------------------
 *  motInitAniCtrlMalloc
 *
 *  The heap-owning form of motInitAniCtrl: motGetAniWorkArea measures the work
 *  area first, one ol_loadGetHeap block covers the lot, and motInitAniCtrl then
 *  carves the sub-systems out of it in the same order.
 *
 *  The two must agree exactly, so the amount actually consumed is compared
 *  against the amount reserved -- an over-run means the carve-up walked past
 *  the end of the block and has already corrupted whatever followed it.
 * ------------------------------------------------------------------------ */
u_int *motInitAniCtrlMalloc(ANI_CTRL *pAniCtrl, u_int *pAnim, u_int *pModel, u_int ModelNo, u_int AnimNo)
{
    pAniCtrl->mdl_no = (int)ModelNo;                                /* 187 */
    pAniCtrl->anm_no = (int)AnimNo;                                 /* 188 */

    u_int file_size = motGetAniWorkArea(pAnim, pModel, ModelNo);          /* 191 */
    pAniCtrl->pkt_p = ol_loadGetHeap((int)file_size);               /* 194 */

    printf("pAniCtrl = %p pModel = %p, pAnim = %p pkt_p = %p mdl_no = %d anm_no = %d\n", pAniCtrl, pModel, pAnim, pAniCtrl->pkt_p, pAniCtrl->mdl_no, pAniCtrl->anm_no); /* 195 */

    if (pAniCtrl->pkt_p == nullptr)                               /* 196 */
    {
        return nullptr;
    }

    u_int *pkt_end = motInitAniCtrl(pAniCtrl, pAnim, pModel, (u_int *) pAniCtrl->pkt_p, ModelNo, AnimNo); /* 199 */

    pAniCtrl->mot.reso = motGetMotReso();                           /* 201 */

    ptrdiff_t used = (intptr_t) pkt_end - (intptr_t) pAniCtrl->pkt_p;/* 204 */
    /* Had to change to bigger instead of != otherwise it would trigger the assert */
    if (used > file_size)                                          /* 205 */
    {
        printf("//==================================================\n");   /* 206 */
        printf("// Warning : anime pakcet Size is OVER!!! %x\n", used);      /* 207 */
        printf("//==================================================\n");   /* 208 */
        PRINT_ASSERT("");                                           /* 209 */
    }

    return pkt_end;                                                 /* 213 */
}

void motInitAniCtrlFree(ANI_CTRL *pAniCtrl)
{
    ol_loadFreeHeap(pAniCtrl->pkt_p);                               /* 220 */
}

/* --------------------------------------------------------------------------
 *  motInitOneEnemyMdl
 *
 *  Called once, the first time a character model pack finishes loading.  The
 *  pack's SGD units are still carrying file-relative offsets at this point, so
 *  MpkMapUnit has to fix them up before anything tries to draw the model --
 *  this is the character-model counterpart of the sgdRemap the item path does
 *  in mmanageIsReadyItemMdl.
 *
 *  mdl_no is unused; the original keeps it for the debug build.
 * ------------------------------------------------------------------------ */
void motInitOneEnemyMdl(u_int *mdl_p, u_int mdl_no)
{
    u_int *mpk_p;
    u_int *top;
    u_int  type;
    u_int  num;
    u_int  i;

    (void)mdl_no;

    mpk_p = (u_int *)0;
    num   = *mdl_p;

    for (i = 0; i < num; i++)
    {
        top = (u_int*)GetFileInPak(mdl_p, i);
        if (top == (u_int *)0)
        {
            /* The ROM asserts and then reads top[-3] anyway, which on the EE
             * merely samples the top of the address space.  Here that is a
             * fault, so the entry is skipped instead. */
            G3DASSERT(top, "");                                     /* 236 */
            continue;
        }

        type = PAK_DAT_TYPE(top);

        if (type == MDL_DAT_MPK)
        {
            mpk_p = top;
        }
        else if (type != MDL_DAT_TANM && type != MDL_DAT_PK2 && type != MDL_DAT_BWC)
        {
            printf("Warning : Found illegal datatype %d\n", type);
        }
    }

    MpkMapUnit(mpk_p);
    SetEneVram(mdl_p, ENE_VRAM_TOP);
}

ANI_CTRL *motSearchANI_CTRL(int ModelNo)
{
    int i;

    for (i = 0; i < ANI_CTRL_MAX; i++)
    {
        if (ani_mdl[i].use == 1 && ani_mdl[i].mdl_no == ModelNo)
        {
            return &ani_mdl[i];
        }
    }

    return (ANI_CTRL *)0;
}

/* Wipe a slot back to "no model bound" -- mdl_no -1 rather than 0, so that
 * model 0 is not matched by a stale search. */
void motClearANI_CTRL(ANI_CTRL *pAniCtrl)
{
    if (pAniCtrl != 0)
    {
        printf("ClearAniCtrl[%p] mdl_no %d, anm_no %d\n",
               (void *)pAniCtrl, pAniCtrl->mdl_no, pAniCtrl->anm_no);
        memset(pAniCtrl, 0, sizeof(ANI_CTRL));
        pAniCtrl->mdl_no = -1;
    }
}

/* --------------------------------------------------------------------------
 *  motGetAniWorkArea
 *
 *  How many bytes motInitAniCtrl will claim for this model/animation pair.
 *  The arithmetic runs on a null pointer purely as an offset accumulator, so
 *  the running total lands on the same alignment boundaries the real carve-up
 *  will hit -- the two must be kept in step.  motInitAniCtrlMalloc allocates
 *  exactly this much and then checks that motInitAniCtrl consumed it, so a
 *  disagreement here is a heap overrun, not a rounding curiosity.
 *
 *  PORT DEVIATION: the original advances by literal word counts (0x15e, 0xfa,
 *  200, 400, 0x50) because on the EE those equal the struct sizes it is
 *  standing in for.  Several of those structs hold pointers, so on a 64-bit
 *  host they are a different size -- MIME_CTRL and MIME_DAT are both larger,
 *  which would under-reserve the block by hundreds of bytes.  The steps below
 *  are therefore written as the same pointer arithmetic motInitAniCtrl uses,
 *  which reproduces the ROM's numbers on the EE and stays correct here.
 * ------------------------------------------------------------------------ */
u_int motGetAniWorkArea(u_int *anm_p, u_int *mdl_p, u_int mdl_no)
{
    u_int *work;
    u_int *mdl_top;
    u_int *p;
    u_int  num;
    u_int  i;

    work    = (u_int *)0;
    mdl_top = (u_int *)0;

    /* The MIME and cloth sizes are measured against the model entry. */
    num = *mdl_p;
    for (i = 0; i < num; i++)
    {
        p = (u_int *)GetFileInPak(mdl_p, i);
        if (PAK_DAT_TYPE(p) == MDL_DAT_MPK)
        {
            mdl_top = p;
        }
    }

    num = *anm_p;
    for (i = 0; i < num; i++)
    {
        p = (u_int *)GetFileInPak(anm_p, i);

        switch (PAK_DAT_TYPE(p))
        {
        case ANM_DAT_MOT:
            /* motInitMotCtrlEx decodes two poses, one RST triple per bone. */
            work = (u_int *)((RST_DATA *)work +
                             motGetBoneNum((u_int *)GetFileInPak(p, 0)) * 2);
            break;

        case ANM_DAT_MIME:
            /* mim / wmim / mim_dat / work -- see motInitAniCtrl. */
            work = motAlign128(work);
            work = motAlign128((u_int *)((MIME_CTRL *)work + 0x32));
            work = motAlign128((u_int *)((WMIM_CTRL *)work + 10));
            work = motAlign128((u_int *)((MIME_DAT *)work + 0x32));
            work = (u_int *)((char *)work + mimGetBufSize(p, mdl_top));
            break;

        case ANM_DAT_BGMIME:
            /* bgmim has no wmim slice and only 10 control blocks. */
            work = motAlign128(work);
            work = motAlign128((u_int *)((MIME_CTRL *)work + 10));
            work = motAlign128((u_int *)((MIME_DAT *)work + 0x32));
            work = (u_int *)((char *)work + mimGetBufSize(p, mdl_top));
            break;
        }
    }

    /* Note the order: the original measures cloth before collision but carves
     * collision before cloth.  Both are 16-byte aligned runs, so the running
     * total is the same either way -- kept as the ROM has it. */
    if (manmdl_dat[mdl_no].cdat != (CLOTH_DAT *)0)
    {
        work = motAlign128(work);
        work = motAlign128((u_int *)((CLOTH_CTRL *)work + 10));
        work = (u_int *)((char *)work + acsGetClothBufSize(mdl_top, mdl_no));
    }

    if (manmdl_dat[mdl_no].collision != (COLLISION_DAT *)0)
    {
        work = motAlign128((u_int *)((COLLISION_CTRL *)work + 1));
    }

    return (u_int)(uintptr_t)work;
}

/* --------------------------------------------------------------------------
 *  motInitAniCtrl
 *
 *  Bind a model pack and an animation pack to an ANI_CTRL and carve the work
 *  area out of pkt_p.  Both packs are walked entry by entry and dispatched on
 *  the entry type; pkt_p advances as each sub-system claims its slice, so the
 *  order below has to match motGetAniWorkArea.
 *
 *  Returns the first free word past everything claimed, or NULL if the model
 *  had no usable base section.
 * ------------------------------------------------------------------------ */
u_int *motInitAniCtrl(ANI_CTRL *ani_ctrl, u_int *anm_p, u_int *mdl_p, u_int *pkt_p,
                      u_int mdl_no, u_int anm_no)
{
    MIME_DAT *mim_dat;
    u_int    *work;
    u_int    *p;
    u_int     anm_num;
    u_int     mdl_num;
    u_int     bone_num;
    u_int     i;

    ani_ctrl->anm_p = anm_p;
    anm_num = *anm_p;

    motSetAnime(ani_ctrl, anm_tbl[anm_no].ani, 0);
    motInitClearAniCtrl(ani_ctrl);

    ani_ctrl->mdl_p = mdl_p;
    mdl_num = *mdl_p;

    for (i = 0; i < mdl_num; i++)
    {
        p = (u_int *)GetFileInPak(mdl_p, i);

        switch (PAK_DAT_TYPE(p))
        {
        case MDL_DAT_TANM: ani_ctrl->tanm_p = p; break;
        case MDL_DAT_MPK:  ani_ctrl->mpk_p  = p; break;
        case MDL_DAT_PK2:  ani_ctrl->pk2_p  = p; break;
        case MDL_DAT_BWC:  ani_ctrl->bwc_p  = p; break;
        }
    }

    /* The model's SGD header sits one 0x20-byte pack header in. */
    ani_ctrl->base_p = (HeaderSection *)(ani_ctrl->mpk_p + 8);

    for (i = 0; i < anm_num; i++)
    {
        p = (u_int *)GetFileInPak(anm_p, i);

        switch (PAK_DAT_TYPE(p))
        {
        case ANM_DAT_MOT:
            ani_ctrl->mot_num = *p;
            pkt_p = motInitMotCtrl(&ani_ctrl->mot, p, pkt_p);

            /* blocks counts the root and a terminator on top of the bones. */
            bone_num = motGetBoneNum(ani_ctrl->mot.dat);
            if (bone_num != ani_ctrl->base_p->blocks - 2)
            {
                printf("error in motInitAniCtrl mdl_no %d anm_no %d\n", mdl_no, anm_no);
                PRINT_ASSERT("bone_num is not match mdl_no %d anm_no %d", mdl_no, anm_no);
            }
            break;

        case ANM_DAT_MIME:
            ani_ctrl->mim_num = *p;
            ani_ctrl->mim  = (MIME_CTRL *)motAlign128(pkt_p);
            ani_ctrl->wmim = (WMIM_CTRL *)motAlign128((u_int *)(ani_ctrl->mim + 0x32));
            mim_dat        = (MIME_DAT *)motAlign128((u_int *)(ani_ctrl->wmim + 10));
            work           = motAlign128((u_int *)(mim_dat + 0x32));
            pkt_p = mimInitMimeCtrl(ani_ctrl->mim, mim_dat, p, ani_ctrl->mpk_p, work,
                                    &ani_ctrl->mim_num);
            mimInitAcsCtrl(ani_ctrl, (u_short)mdl_no);
            break;

        case ANM_DAT_BGMIME:
            ani_ctrl->bg_num = *p;
            ani_ctrl->bgmim = (MIME_CTRL *)motAlign128(pkt_p);
            work            = motAlign128((u_int *)(ani_ctrl->bgmim + 10));
            mim_dat         = (MIME_DAT *)motAlign128(work);
            work            = motAlign128((u_int *)(mim_dat + 0x32));
            pkt_p = mimInitMimeCtrl(ani_ctrl->bgmim, mim_dat, p, ani_ctrl->mpk_p, work,
                                    &ani_ctrl->bg_num);
            break;

        case ANM_DAT_MTOP:
            ani_ctrl->mtop = p;
            ani_ctrl->mdat = (u_int *)GetFileInPak(p, 0);
            break;

        default:
            printf("Warning : Found illegal datatype %d\n", PAK_DAT_TYPE(p));
            break;
        }
    }

    if (manmdl_dat[mdl_no].collision == (COLLISION_DAT *)0)
    {
        ani_ctrl->collision_ctrl = (COLLISION_CTRL *)0;
    }
    else
    {
        ani_ctrl->collision_ctrl = (COLLISION_CTRL *)motAlign128(pkt_p);
        pkt_p = motAlign128((u_int *)(ani_ctrl->collision_ctrl + 1));
    }

    if (manmdl_dat[mdl_no].cdat != (CLOTH_DAT *)0)
    {
        ani_ctrl->cloth_ctrl = (CLOTH_CTRL *)motAlign128(pkt_p);
        work  = motAlign128((u_int *)(ani_ctrl->cloth_ctrl + 10));
        pkt_p = acsInitCloth(ani_ctrl->cloth_ctrl, ani_ctrl->collision_ctrl,
                             ani_ctrl->mpk_p, work, mdl_no, 0);
    }

    /* A model with no base section never got its mpk entry -- bail rather than
     * walk a hierarchy off a near-null pointer. */
    if ((uintptr_t)ani_ctrl->base_p <= 0xffffe)
    {
        return (u_int *)0;
    }

    motSetHierarchy(ani_ctrl->base_p->coordp, ani_ctrl->mot.dat);
    return pkt_p;
}

/* --------------------------------------------------------------------------
 *  motInitMotCtrlEx
 *
 *  Bind a MOT_CTRL to one motion out of a motion pack and rewind it.  Every
 *  motion in the pack gets its frame table relocated, not just the selected
 *  one, so a later motSetAnime can switch without re-mapping.
 *
 *  rst_addr, when given, is carved into the two decoded-pose buffers; the
 *  return value is the first free word past them.
 * ------------------------------------------------------------------------ */
u_int *motInitMotCtrlEx(MOT_CTRL *m_ctrl, u_int *mot_addr, u_int *rst_addr, int play_id)
{
    u_int num;

    if (play_id < 0 || play_id >= (int)*mot_addr)
    {
        return (u_int *)0;
    }

    m_ctrl->play_id = play_id;
    m_ctrl->top     = mot_addr;
    m_ctrl->dat     = (u_int *)GetFileInPak(mot_addr, play_id);

    for (num = 0; num < *mot_addr; num = (num + 1) & 0xffff)
    {
        motAddressMapping((u_int *)GetFileInPak(m_ctrl->top, num));
    }

    m_ctrl->cnt         = 0;
    m_ctrl->inp_cnt     = 0.0f;
    m_ctrl->all_cnt     = motGetFrameNum(m_ctrl->dat);
    m_ctrl->old_mot_cnt = -1.0f;
    m_ctrl->reso        = 0;
    m_ctrl->reso_cnt    = 0;
    m_ctrl->next_flg    = 0;
    m_ctrl->inp_allcnt  = motGetInterpFrameNum(m_ctrl->dat) * 2 + 2;
    m_ctrl->end_flg     = 0;

    if (rst_addr == (u_int *)0)
    {
        return (u_int *)0;
    }

    m_ctrl->rst0 = (RST_DATA *)rst_addr;
    m_ctrl->rst1 = m_ctrl->rst0 + motGetBoneNum(m_ctrl->dat);

    motGetFrameData(m_ctrl->rst0, m_ctrl->dat, 0);
    motGetFrameData(m_ctrl->rst1, m_ctrl->dat, 0);

    return (u_int *)(m_ctrl->rst1 + motGetBoneNum(m_ctrl->dat));
}

u_int *motInitMotCtrl(MOT_CTRL *m_ctrl, u_int *mot_addr, u_int *rst_addr)
{
    return motInitMotCtrlEx(m_ctrl, mot_addr, rst_addr, 0);
}

void motSetCoordCamera(ANI_CTRL *ani_ctrl)
{
    float f;
    u_int frame;

    if (ani_ctrl->interp_flg == 1)
    {
        motSetInterpMatrix(ani_ctrl, m_start.data(), m_end.data());
        motInterpAnm(ani_ctrl, m_start.data(), m_end.data());

        if (motAddFrame(&ani_ctrl->mot) != 0)
        {
            ani_ctrl->interp_flg = 0;
        }
    }
    else
    {
        f = plyr_wrk.frot_x - now_frot_x;
        if (fabsf(f) > 0.02f)
        {
            f = (f > 0.0f) ? 0.02f : -0.02f;
        }

        now_frot_x += f;
        frame = (u_int)((-now_frot_x / 30.0f + 1.0f) * 40.0f * 0.5f);
        motSetCoordFrame(ani_ctrl, frame);
    }
}

static void motInitNewMotion(ANI_CTRL *ani_ctrl)
{
    MOT_CTRL *mot;

    mot = &ani_ctrl->mot;
    motAniCodeRead(ani_ctrl);

    if (mot->cnt == 0)
    {
        mot->cnt = 0;
        mot->inp_cnt = 0.0f;
    }
    else
    {
        mot->cnt %= mot->all_cnt;
    }

    mot->old_mot_cnt = -1.0f;
    mot->all_cnt = (int)motGetFrameNum(mot->dat);
    mot->next_flg = 0;
    mot->inp_allcnt = (int)motGetInterpFrameNum(mot->dat) * 2 + 2;
}

static u_char motAddFrame(MOT_CTRL *m_ctrl)
{
    u_char cnt;
    u_char ret;
    float inp_allcnt;

    cnt = 0;
    ret = 0;

    if (m_ctrl->reso != 0 && m_ctrl->end_flg != 1)
    {
        m_ctrl->old_mot_cnt = motGetNowFramef(m_ctrl);
        m_ctrl->inp_cnt += (float)m_ctrl->reso / 100.0f;
        ret = 1;

        if (m_ctrl->inp_allcnt != 0)
        {
            inp_allcnt = (float)m_ctrl->inp_allcnt;
            ret = 0;

            while (inp_allcnt <= m_ctrl->inp_cnt)
            {
                cnt++;
                m_ctrl->inp_cnt -= inp_allcnt;
                ret = cnt;
            }
        }
    }

    return ret;
}

u_char motSetCoord(ANI_CTRL *ani_ctrl, u_char work_id, u_char stop_fl)
{
    MOT_CTRL *mot;
    ANI_CTRL tmp;
    u_int *old_dat;
    u_int interp0;
    u_int interp1;
    u_int frame_num;
    u_int si_frame;
    u_char add_frame;
    u_char next_status;
    u_char loop_end;
    bool ani_end;

    (void)work_id;

    mot = &ani_ctrl->mot;
    G3DASSERT(mot != 0, "mot_ctrl is NULL mdl_no %d anm_no %d",
              ani_ctrl->mdl_no, ani_ctrl->anm_no);

    motSetInterpMatrix(ani_ctrl, m_start.data(), m_end.data());
    motInterpAnm(ani_ctrl, m_start.data(), m_end.data());

    if (stop_fl != 0)
    {
        return 0;
    }

    loop_end = 0;
    old_dat = (u_int *)0;
    ani_end = false;

    add_frame = motAddFrame(mot);
    if (add_frame == 0)
    {
        motAniTimerCodeExec(ani_ctrl);
        return 0;
    }

    mot->cnt += (char)add_frame;

    if (ani_ctrl->interp_flg == 1)
    {
        mot->cnt = 0;
        motInitNewMotion(ani_ctrl);
    }
    else if ((mot->cnt < mot->all_cnt - 1) && (mot->all_cnt > 1))
    {
        if (mot->cnt < mot->all_cnt)
        {
            mot->inp_allcnt = (int)motGetInterpFrameNum(mot->dat) * 2 + 2;
        }
        else
        {
            motInitNewMotion(ani_ctrl);
            mot->next_flg = 0;
        }
    }
    else if (mot->next_flg != 0)
    {
        if (mot->cnt < mot->all_cnt)
        {
            mot->inp_allcnt = (int)motGetInterpFrameNum(mot->dat) * 2 + 2;
        }
        else
        {
            motInitNewMotion(ani_ctrl);
            mot->next_flg = 0;
        }
    }
    else
    {
        tmp = *ani_ctrl;
        next_status = motGetNextMotion(&tmp);

        if (next_status == 1)
        {
            mot->inp_cnt = 0.0f;
            mot->end_flg = 1;
            mot->cnt = (mot->cnt < 1) ? 0 : mot->all_cnt - 1;
            ani_end = true;
        }
        else if (next_status == 0 || next_status == 2)
        {
            if (next_status == 2)
            {
                loop_end = 1;
            }

            /*
             * Both calls deliberately use the outgoing motion.  The original
             * assembly computes the transition duration before installing
             * tmp.mot.dat.
             */
            interp0 = motGetInterpFrameNum(mot->dat);
            interp1 = motGetInterpFrameNum(mot->dat);
            frame_num = motGetFrameNum(mot->dat);
            si_frame = motGetSIFrameNum(mot->dat);

            old_dat = mot->dat;
            mot->old_mot_cnt = -1.0f;
            mot->dat = tmp.mot.dat;
            mot->next_flg = 1;
            mot->inp_allcnt =
                (int)(interp1 * 2 + 2) -
                (int)(((interp0 + 1) * frame_num - si_frame) * 2);
        }
    }

    if (mot->next_flg == 0 || mot->all_cnt <= mot->cnt)
    {
        motGetFrameData(mot->rst0, mot->dat, (u_int)(mot->cnt % mot->all_cnt));
    }
    else
    {
        motGetFrameData(mot->rst0, old_dat, (u_int)(mot->cnt % mot->all_cnt));
    }

    motGetFrameData(mot->rst1, mot->dat,
                    (u_int)((mot->cnt + 1) % mot->all_cnt));

    if (!ani_end)
    {
        motAniTimerCodeExec(ani_ctrl);
        return (u_char)(loop_end << 1);
    }

    return 1;
}

u_int motGetNowFrame(MOT_CTRL *m_ctrl)
{
    u_int interp;

    interp = motGetInterpFrameNum(m_ctrl->dat) * 2 + 2;
    return (u_int)((float)(m_ctrl->cnt * interp) + m_ctrl->inp_cnt);
}

float motGetNowFramef(MOT_CTRL *m_ctrl)
{
    u_int interp;

    interp = motGetInterpFrameNum(m_ctrl->dat) * 2 + 2;
    return (float)(m_ctrl->cnt * interp) + m_ctrl->inp_cnt;
}

void ReqAnm(void *ani_hndl, int flame, int anm_no, int anime_no)
{
    ANI_CTRL *ani_ctrl = (ANI_CTRL *)ani_hndl;

    mimInitLoop(ani_ctrl);
    motSetAnime(ani_ctrl, anm_tbl[anm_no].ani, anime_no);
    motSetHierarchy(ani_ctrl->base_p->coordp, ani_ctrl->mot.dat);
    motInitInterpAnime(ani_ctrl, flame);
}

void motSetAnime(ANI_CTRL *ani_ctrl, ANI_CODE **tbl, int req_no)
{
    int iNumAnim;

    iNumAnim = 0;                                                   /* 989 */
    while (tbl[iNumAnim] != (ANI_CODE *)0)                          /* 990 */
    {
        iNumAnim++;
    }

    if (req_no >= iNumAnim)                                         /* 991 */
    {
        /* "motSetAnime() : req_no might be off (%d)" */
        G3DWARNING(req_no < iNumAnim,
                   "motSetAnime() : req_no がおかしいかもね(%d)", req_no);
        req_no = 0;                                                 /* 992 */
    }

    ani_ctrl->anm.timer = 0;
    ani_ctrl->anm.playnum = req_no;
    ani_ctrl->anm.stat = 0;
    ani_ctrl->anm.loop_rest = 0;
    ani_ctrl->anm.code_now = tbl[req_no];
    ani_ctrl->anm.code_head = tbl[req_no];
    motAniCodeClearBuf(ani_ctrl);
    ani_ctrl->interp_flg = 1;
    ani_ctrl->mot.end_flg = 0;
}

int motCheckInterp(ANI_CTRL *ani_ctrl)
{
    return (int)(u_short)ani_ctrl->interp_flg;
}

int motGetMotReso(void)
{
    return (GetPALMode() != 0) ? 0xef : 200;
}

/* --------------------------------------------------------------------------
 *  Bone-position accessors
 *
 *  A bone's world position is row 3 of its matLocalWorld, so most of these are
 *  a straight copy of that row.  The ones that need a point offset inside the
 *  bone's own space run it through sceVu0ApplyMatrix instead.
 *
 *  Bone slots come from manmdl_dat[mdl_no]; note motSetHierarchy places bone i
 *  at coord[i + 1], so those ids are already 1-based against coordp.
 * ------------------------------------------------------------------------ */
/* The head bone with a small forward nudge, divided through by the model's
 * scale so the offset stays in world units.  Most models take the default. */
void GetMdlNeckPos(float *pos, ANI_CTRL *ani_ctrl, u_short mdl_no)
{
    float  p[4];
    u_char head_id;

    head_id = manmdl_dat[mdl_no].head_id;

    memset(p, 0, sizeof(p));
    p[3] = 1.0f;

    switch (mdl_no)
    {
    case 0x02:
        p[2] = 4.0f;
        break;

    case 0x0c:
    case 0x0e:
    case 0x17:
    case 0x28:
    case 0x38:
    case 0x39:
        p[0] = 0.0f;
        break;

    case 0x25:
        p[0] = 2.0f;
        break;

    default:
        p[0] = 1.0f;
        break;
    }

    p[0] = p[0] / manmdl_dat[mdl_no].scale;

    sceVu0ApplyMatrix(pos, ani_ctrl->base_p->coordp[head_id].matLocalWorld, p);
}

/* Fill pos[] with every bone's world position; returns how many were written. */
u_int GetMdlBonePos(float (*pos)[4], void *ani_hndl)
{
    ANI_CTRL      *ani_ctrl;
    HeaderSection *pHead;
    SGDCOORDINATE *coord;
    u_int          i;

    ani_ctrl = (ANI_CTRL *)ani_hndl;
    pHead    = ani_ctrl->base_p;

    if (pHead->blocks == 1)
    {
        return 0;
    }

    coord = pHead->coordp;

    for (i = 0; i < pHead->blocks - 1; i++)
    {
        g3dxVu0CopyVector(pos[i], coord[i].matLocalWorld[3]);
    }

    return pHead->blocks - 1;
}

void GetMdlWaistPos(float *pos, ANI_CTRL *ani_ctrl, u_short mdl_no)
{
    float ofs[4];

    memset(ofs, 0, sizeof(ofs));
    ofs[3] = 1.0f;

    if (mdl_no == 2)
    {
        ofs[2] = 7.0f;
    }

    sceVu0ApplyMatrix(pos, ani_ctrl->base_p->coordp[manmdl_dat[mdl_no].waist_id].matLocalWorld, ofs);
}

void GetMdlHipPos(float *pos, ANI_CTRL *ani_ctrl, u_short mdl_no)
{
    g3dxVu0CopyVector(pos, ani_ctrl->base_p->coordp[manmdl_dat[mdl_no].hip_id].matLocalWorld[3]);
}

void GetMdlLegPos(float *pos, ANI_CTRL *ani_ctrl, u_short mdl_no)
{
    g3dxVu0CopyVector(pos, ani_ctrl->base_p->coordp[manmdl_dat[mdl_no].leg_id].matLocalWorld[3]);
}

/* Shoulders are coord[1] and coord[2]; lr picks between them. */
void GetMdlShldPos(float *pos, ANI_CTRL *ani_ctrl, u_char lr)
{
    g3dxVu0CopyVector(pos, ani_ctrl->base_p->coordp[lr + 1].matLocalWorld[3]);
}

/* Fixed slots on the player skeleton: left foot 5, right foot 0x10. */
void GetPlyrFootPos(float *pos, ANI_CTRL *ani_ctrl, u_char lr)
{
    g3dxVu0CopyVector(pos, ani_ctrl->base_p->coordp[(lr != 0) ? 0x10 : 5].matLocalWorld[3]);
}

void GetPlyrAcsLightPos(float *pos, ANI_CTRL *ani_ctrl)
{
    static float acs_light_ofs[4] = { 2.562164f, -1.225883f, 0.259496f, 1.0f };
    sceVu0ApplyMatrix(pos, ani_ctrl->base_p->coordp[6].matLocalWorld, acs_light_ofs);
}

/* Both ends of the blade, taken from the same hand bone. */
void GetToushuKatanaPos(float *p0, float *p1, ANI_CTRL *ani_ctrl)
{
    static float katana_ofs0[4]   = { 1.4f, 1.4f, 0.3f, 1.0f };
    static float katana_ofs1[4]   = { 1.81f, 18.0f, -6.0f, 1.0f };

    sceVu0ApplyMatrix(p0, ani_ctrl->base_p->coordp[6].matLocalWorld, katana_ofs0);
    sceVu0ApplyMatrix(p1, ani_ctrl->base_p->coordp[6].matLocalWorld, katana_ofs1);
}

/* Only model 0x1f carries the staff; flg picks which end. */
int motGetGuujiTuePos(float *p0, ANI_CTRL *ani_ctrl, int flg)
{
    float ofs0[4];
    float ofs1[4];

    ofs0[0] =  2.0f; ofs0[1] = -15.0f; ofs0[2] = 0.0f; ofs0[3] = 1.0f;
    ofs1[0] =  2.0f; ofs1[1] =  20.0f; ofs1[2] = 0.0f; ofs1[3] = 1.0f;

    if (ani_ctrl->mdl_no != 0x1f)
    {
        return 0;
    }

    sceVu0ApplyMatrix(p0, ani_ctrl->base_p->coordp[0xc].matLocalWorld,
                      (flg == 0) ? ofs0 : ofs1);
    return 1;
}

int motGetKusabiPos(float *p0, ANI_CTRL *ani_ctrl, int flg)
{
    float ofs0[4] = { -25.0f, 45.0f, 0.0f, 1.0f };
    float ofs1[4] = {  23.0f, 40.0f, 0.0f, 1.0f };
    float ofs2[4] = { -15.0f, 27.0f, 0.0f, 1.0f };
    float ofs3[4] = {  20.0f, 27.0f, 0.0f, 1.0f };
    float *ofs;

    if (ani_ctrl->mdl_no != 3)
    {
        return 0;
    }

    switch (flg)
    {
    case 0: ofs = ofs0; break;
    case 1: ofs = ofs1; break;
    case 2: ofs = ofs2; break;
    case 3: ofs = ofs3; break;
    default:
        return 1;
    }

    sceVu0ApplyMatrix(p0, ani_ctrl->base_p->coordp[0].matLocalWorld, ofs);
    return 1;
}

int motGetTaimatuPos(float *p0, ANI_CTRL *ani_ctrl)
{
    float ofs0[4] = { 2.0f, -4.8f, 0.8f, 1.0f };

    switch (ani_ctrl->mdl_no)
    {
    case 0x23:
    case 0x32:
    case 0x33:
    case 0x34:
        sceVu0ApplyMatrix(
            p0, ani_ctrl->base_p->coordp[0x0c].matLocalWorld, ofs0);
        return 1;
    default:
        return 0;
    }
}

int motGetKuroreiPos(float *p0, ANI_CTRL *ani_ctrl)
{
    float ofs0[4] = { 2.0f, 0.0f, 1.0f, 1.0f };

    if (ani_ctrl->mdl_no == 0x29)
    {
        sceVu0ApplyMatrix(
            p0, ani_ctrl->base_p->coordp[0x0c].matLocalWorld, ofs0);
        return 1;
    }

    return 0;
}

int motGetBukiUpPos(float *p0, ANI_CTRL *ani_ctrl)
{
    switch (ani_ctrl->mdl_no)
    {
    case 3:
        motGetKusabiPos(p0, ani_ctrl, 0);
        break;
    case 0x1f:
        motGetGuujiTuePos(p0, ani_ctrl, 0);
        break;
    case 0x23:
    case 0x32:
    case 0x33:
    case 0x34:
        motGetTaimatuPos(p0, ani_ctrl);
        break;
    case 0x29:
        motGetKuroreiPos(p0, ani_ctrl);
        break;
    }

    return 1;
}

int motGetBukiDownPos(float *p0, ANI_CTRL *ani_ctrl)
{
    if (ani_ctrl->mdl_no == 3)
    {
        motGetKusabiPos(p0, ani_ctrl, 1);
    }
    else if (ani_ctrl->mdl_no == 0x1f)
    {
        motGetGuujiTuePos(p0, ani_ctrl, 1);
    }

    return 1;
}

int motGetBukiSpeAPos(float *p0, ANI_CTRL *ani_ctrl)
{
    if (ani_ctrl->mdl_no == 3)
    {
        motGetKusabiPos(p0, ani_ctrl, 2);
    }

    return 1;
}

int motGetBukiSpeBPos(float *p0, ANI_CTRL *ani_ctrl)
{
    if (ani_ctrl->mdl_no == 3)
    {
        motGetKusabiPos(p0, ani_ctrl, 3);
    }

    return 1;
}

/* --------------------------------------------------------------------------
 *  GetMdlHeightPos
 *
 *  Where the model is standing and which way it faces: the waist position
 *  dropped onto the map floor, plus a yaw taken from the hip bone's X axis
 *  flattened into the XZ plane.  Returns non-zero when a floor was found.
 * ------------------------------------------------------------------------ */
int GetMdlHeightPos(void *ani_hndl, float *pos, float *rot, int mdl_no)
{
    ANI_CTRL      *ani_ctrl;
    SGDCOORDINATE *coord;
    float          hip_pos[4];
    float          tmp[4];
    float          x_vec[4];
    float          ry;
    int            buff_id;

    ani_ctrl = (ANI_CTRL *)ani_hndl;
    coord    = ani_ctrl->base_p->coordp;

    memset(x_vec, 0, sizeof(x_vec));
    x_vec[0] = 1.0f;

    GetMdlWaistPos(hip_pos, ani_ctrl, (u_short)mdl_no);

    /* Row 0 of the hip's matLocalWorld is its X axis. */
    sceVu0CopyVector(tmp, coord[manmdl_dat[mdl_no].hip_id].matLocalWorld[0]);
    tmp[1] = 0.0f;
    tmp[3] = 0.0f;
    sceVu0Normalize(tmp, tmp);

    ry = g3dAcosf(sceVu0InnerProduct(tmp, x_vec)) + 3.1415925f;
    if (ry > 3.1415925f)
    {
        ry -= 6.283185f;
    }

    rot[0] = 0.0f;
    rot[1] = ry;
    rot[2] = 0.0f;

    buff_id = RegDatGetBuffID(hip_pos);
    if (buff_id >= 0)
    {
        MhGetMapHeight(pos, hip_pos, buff_id, 0);
    }
    else
    {
        sceVu0CopyVector(pos, hip_pos);
    }

    return (buff_id >= 0);
}

void motInitInterpAnime(ANI_CTRL *ani_ctrl, int flame)
{
    ANI_CODE *code;
    int args[3];
    bool found;

    G3DRETURN(ani_ctrl->anm.code_head, "ani_ctrl->anm.code_head is NULL");

    found = false;
    ani_ctrl->mot.old_mot_cnt = -1.0f;
    code = ani_ctrl->anm.code_head;
    ani_ctrl->mot.inp_allcnt = flame;
    ani_ctrl->mot.inp_cnt = 0.0f;
    ani_ctrl->mot.reso_cnt = 0;
    ani_ctrl->mot.cnt = 0;

    while (!motAniCodeIsEnd(*code))
    {
        if ((*code >> 12) == 2)
        {
            GetAniCodeArgs(*code, args);
            ani_ctrl->mot.dat =
                (u_int *)GetFileInPak(ani_ctrl->mot.top, (u_int)args[0]);
            ani_ctrl->mot.play_id = (u_int)args[0];
            if (ani_ctrl->mtop != (u_int *)0)
            {
                ani_ctrl->mdat =
                    (u_int *)GetFileInPak(ani_ctrl->mtop, (u_int)args[0]);
            }

            found = true;
            ani_ctrl->mot.all_cnt = (int)motGetFrameNum(ani_ctrl->mot.dat);
            break;
        }

        code++;
    }

    if (!found)
    {
        printf("Warning : Not Found InterpAnime\n");
    }

    motGetFrameData(ani_ctrl->mot.rst1, ani_ctrl->mot.dat, 0);
}

/* --------------------------------------------------------------------------
 *  motInterpMatrix
 *
 *  Blend two orientations without quaternions: first swing the X axes together
 *  about their common perpendicular, then swing the resulting Y axes together
 *  about theirs.  Two passes are enough to pin down the full orientation.
 *
 *  Note the first pass builds both the forward rotation (by rate) and the
 *  reverse one (by 1 - rate); only the forward result feeds the second pass.
 *
 *  Rodrigues' rotation is written out longhand three times rather than through
 *  a helper -- the ROM line numbers below are three distinct copies in the
 *  original source, not three calls to one inline, so it is kept that way.
 *
 *  On composition order: sceVu0MulMatrix broadcasts m2's components against
 *  m1's rows (vmulax/vmadday/vmaddaz/vmaddw on the EE), so it yields
 *  m0 = m2 * m1 over the raw indices.  cglm is column-major, which reverses
 *  glm_mat4_mul the same way, so the port agrees with the EE and the original
 *  argument order is kept -- see sgdCalcCoordinateMatrix, same reasoning.
 *  Passing the swing rotation on the right instead re-mixes m0's rows rather
 *  than swinging them, and the blend then fails to land on m1 at rate == 1.
 * ------------------------------------------------------------------------ */
void motInterpMatrix(float (*interp)[4], float (*m0)[4], float (*m1)[4], float rate)
{
    float m[4][4];
    float v[4];
    float r;
    float cos;
    float sin;
    float val;
    float r2;
    float v2[4];
    float m2[4][4];
    float dm0[4][4];
    float dm1[4][4];

    sceVu0Normalize(m0[0], m0[0]);                                  /* 1616 */
    sceVu0Normalize(m1[0], m1[0]);                                  /* 1617 */
    sceVu0OuterProduct(v, m0[0], m1[0]);                            /* 1618 */
    sceVu0Normalize(v, v);                                          /* 1619 */

    sceVu0OuterProduct(v2, m1[0], m0[0]);                           /* 1621 */
    sceVu0Normalize(v2, v2);                                        /* 1622 */

    r = sceVu0InnerProduct(m0[0], m1[0]);                           /* 1625 */
    r = g3dAcosf(r) * rate;                                         /* 1626 */

    r2 = sceVu0InnerProduct(m1[0], m0[0]);                          /* 1628 */
    r2 = g3dAcosf(r2) * (1.0f - rate);                              /* 1629 */

    cos = cosf(r);                                                  /* 1633 */
    sin = sinf(r);                                                  /* 1634 */
    val = 1.0f - cos;                                               /* 1635 */
    sceVu0UnitMatrix(m);                                            /* 1636 */
    m[0][0] = v[0] * v[0] * val + cos;                              /* 1637 */
    m[1][0] = v[0] * v[1] * val - v[2] * sin;                       /* 1638 */
    m[2][0] = v[0] * v[2] * val + v[1] * sin;                       /* 1639 */
    m[0][1] = v[0] * v[1] * val + v[2] * sin;                       /* 1640 */
    m[1][1] = v[1] * v[1] * val + cos;                              /* 1641 */
    m[2][1] = v[1] * v[2] * val - v[0] * sin;                       /* 1642 */
    m[0][2] = v[0] * v[2] * val - v[1] * sin;                       /* 1643 */
    m[1][2] = v[1] * v[2] * val + v[0] * sin;                       /* 1644 */
    m[2][2] = v[2] * v[2] * val + cos;                              /* 1645 */

    cos = cosf(r2);                                                 /* 1647 */
    sin = sinf(r2);                                                 /* 1648 */
    val = 1.0f - cos;                                               /* 1649 */
    sceVu0UnitMatrix(m2);                                           /* 1650 */
    m2[0][0] = v2[0] * v2[0] * val + cos;                           /* 1651 */
    m2[1][0] = v2[0] * v2[1] * val - v2[2] * sin;                   /* 1652 */
    m2[2][0] = v2[0] * v2[2] * val + v2[1] * sin;                   /* 1653 */
    m2[0][1] = v2[0] * v2[1] * val + v2[2] * sin;                   /* 1654 */
    m2[1][1] = v2[1] * v2[1] * val + cos;                           /* 1655 */
    m2[2][1] = v2[1] * v2[2] * val - v2[0] * sin;                   /* 1656 */
    m2[0][2] = v2[0] * v2[2] * val - v2[1] * sin;                   /* 1657 */
    m2[1][2] = v2[1] * v2[2] * val + v2[0] * sin;                   /* 1658 */
    m2[2][2] = v2[2] * v2[2] * val + cos;                           /* 1659 */

    sceVu0MulMatrix(dm0, m, m0);                                    /* 1662 */
    sceVu0MulMatrix(dm1, m2, m1);                                   /* 1663 */

    sceVu0Normalize(dm0[1], dm0[1]);                                /* 1666 */
    sceVu0Normalize(dm1[1], dm1[1]);                                /* 1667 */
    sceVu0OuterProduct(v, dm0[1], dm1[1]);                          /* 1668 */
    sceVu0Normalize(v, v);                                          /* 1669 */

    r = sceVu0InnerProduct(dm0[1], dm1[1]);                         /* 1672 */
    r = g3dAcosf(r) * rate;                                         /* 1673 */

    cos = cosf(r);                                                  /* 1676 */
    sin = sinf(r);                                                  /* 1677 */
    val = 1.0f - cos;                                               /* 1678 */
    sceVu0UnitMatrix(m);                                            /* 1679 */
    m[0][0] = v[0] * v[0] * val + cos;                              /* 1680 */
    m[1][0] = v[0] * v[1] * val - v[2] * sin;                       /* 1681 */
    m[2][0] = v[0] * v[2] * val + v[1] * sin;                       /* 1682 */
    m[0][1] = v[0] * v[1] * val + v[2] * sin;                       /* 1683 */
    m[1][1] = v[1] * v[1] * val + cos;                              /* 1684 */
    m[2][1] = v[1] * v[2] * val - v[0] * sin;                       /* 1685 */
    m[0][2] = v[0] * v[2] * val - v[1] * sin;                       /* 1686 */
    m[1][2] = v[1] * v[2] * val + v[0] * sin;                       /* 1687 */
    m[2][2] = v[2] * v[2] * val + cos;                              /* 1688 */

    sceVu0MulMatrix(interp, m, dm0);                                /* 1690 */
}

/* --------------------------------------------------------------------------
 *  motMatrix2Quaternion
 *
 *  Shoemake's conversion: use the trace when it is positive, otherwise pivot on
 *  whichever diagonal term is largest to keep the square root well away from
 *  zero.  nxt[] walks the axes cyclically.
 * ------------------------------------------------------------------------ */
void motMatrix2Quaternion(float *q, float (*m)[4])
{
    /* Copied out of .rodata as a unit in the original, not stored field by
     * field -- keep it an initialiser so the same thing can happen here. */
    int   nxt[3] = { 1, 2, 0 };                                     /* 1706 */
    float tr;
    float s;
    int   i, j, k;

    sceVu0TransposeMatrix(m, m);                                    /* 1709 */

    tr = m[0][0] + m[1][1] + m[2][2];                               /* 1711 */

    if (tr > 0.0f)
    {
        /* vsqrt / vwaitq / vaddq, not the libm call. */
        s = g3dxVu0Sqrt(tr + 1.0f);
        q[3] = s * 0.5f;
        s = 0.5f / s;
        q[0] = (m[1][2] - m[2][1]) * s;
        q[1] = (m[2][0] - m[0][2]) * s;
        q[2] = (m[0][1] - m[1][0]) * s;
    }
    else
    {
        i = 0;
        if (m[0][0] < m[1][1])
        {
            i = 1;
        }
        if (m[i][i] < m[2][2])
        {
            i = 2;
        }
        j = nxt[i];
        k = nxt[j];

        s = g3dxVu0Sqrt((m[i][i] - (m[j][j] + m[k][k])) + 1.0f);
        q[i] = s * 0.5f;
        if (s != 0.0f)
        {
            s = 0.5f / s;
        }
        q[3] = (m[j][k] - m[k][j]) * s;
        q[j] = (m[i][j] + m[j][i]) * s;
        q[k] = (m[i][k] + m[k][i]) * s;
    }
}

void motQuaternion2Matrix(float (*m)[4], float *q)
{
    float x, y, z, w;
    float x2, y2, z2;
    float xx2, wx2;

    x = q[0];
    y = q[1];
    z = q[2];
    w = q[3];

    x2 = x + x;
    y2 = y + y;
    z2 = z + z;

    xx2 = x * x2;
    wx2 = w * x2;

    m[0][0] = 1.0f - (y * y2 + z * z2);
    m[0][1] = x * y2 - w * z2;
    m[0][2] = x * z2 + w * y2;
    m[0][3] = 0.0f;

    m[1][0] = x * y2 + w * z2;
    m[1][1] = 1.0f - (xx2 + z * z2);
    m[1][2] = y * z2 - wx2;
    m[1][3] = 0.0f;

    m[2][0] = x * z2 - w * y2;
    m[2][1] = y * z2 + wx2;
    m[2][2] = 1.0f - (xx2 + y * y2);
    m[2][3] = 0.0f;

    m[3][0] = 0.0f;
    m[3][1] = 0.0f;
    m[3][2] = 0.0f;
    m[3][3] = 1.0f;
}

/* --------------------------------------------------------------------------
 *  motQuaternionSlerp
 *
 *  Negate q2 when the two quaternions point into opposite hemispheres so the
 *  blend takes the short way round, then interpolate.
 *
 *  Despite the name this is a plain lerp, not a true slerp.  PS2
 *  sceVu0InterVector weights its first source by the supplied value, so passing
 *  1 - rate yields q1 at rate 0 and q2 at rate 1.
 * ------------------------------------------------------------------------ */
void motQuaternionSlerp(float *q, float *q1, float *q2, float rate)
{
    float ret[4];

    sceVu0MulVector(ret, q1, q2);

    if (ret[0] + ret[1] + ret[2] + ret[3] < 0.0f)
    {
        sceVu0ScaleVector(q2, q2, -1.0f);
    }

    sceVu0InterVector(q, q1, q2, 1.0f - rate);
}

void LocalRotMatrixX(float (*m0)[4], float (*m1)[4], float rx)
{
    float rot[4][4];
    float s;

    sceVu0UnitMatrix(rot);
    rot[1][1] = cosf(rx);
    s = sinf(rx);
    rot[2][1] = -s;
    rot[1][2] = sinf(rx);
    rot[2][2] = cosf(rx);
    sceVu0MulMatrix(m0, m1, rot);
}

void LocalRotMatrixY(float (*m0)[4], float (*m1)[4], float ry)
{
    float rot[4][4];
    float s;

    sceVu0UnitMatrix(rot);
    rot[0][0] = cosf(ry);
    rot[0][2] = sinf(ry);
    s = sinf(ry);
    rot[2][0] = -s;
    rot[2][2] = cosf(ry);
    sceVu0MulMatrix(m0, m1, rot);
}

void LocalRotMatrixZ(float (*m0)[4], float (*m1)[4], float rz)
{
    float rot[4][4];
    float s;

    sceVu0UnitMatrix(rot);
    rot[0][0] = cosf(rz);
    s = sinf(rz);
    rot[1][0] = -s;
    rot[0][1] = sinf(rz);
    rot[1][1] = cosf(rz);
    sceVu0MulMatrix(m0, m1, rot);
}

/* --------------------------------------------------------------------------
 *  motInversKinematics
 *
 *  Walk up the bone chain from bone_id, swinging each parent so the end
 *  effector moves toward `target`, and stop once bone 11 (the arm root) has
 *  been reached.  Each step is clamped to 90 degrees, and a negative spot_rot
 *  damps it to a quarter turn in the opposite direction.
 *
 *  The end effector is tracked locally rather than re-read from the hierarchy:
 *  matLocalWorld is only recomputed later, so the loop rotates its own copy by
 *  the same matrix it just applied to the parent.
 * ------------------------------------------------------------------------ */
void motInversKinematics(SGDCOORDINATE *cp, float *target, u_int *top_addr, u_char bone_id)
{
    u_int parent_id;
    float end_eff[4];
    float root[4];
    float end_root[4];
    float target_root[4];
    float inner;
    float m[4][4];
    float r;
    float raxis[4];
    float length;

    parent_id = (u_int)bone_id;
    sceVu0CopyVector(end_eff, cp[bone_id].matLocalWorld[3]);        /* 1836 */

    do                                                              /* 1837 */
    {
        parent_id = motGetParentId(top_addr, parent_id - 1);        /* 1844 */
        sceVu0CopyVector(root, cp[parent_id].matLocalWorld[3]);     /* 1845 */

        sceVu0SubVector(end_root, end_eff, root);                   /* 1847 */
        sceVu0SubVector(target_root, target, root);                 /* 1848 */
        sceVu0Normalize(end_root, end_root);                        /* 1849 */
        sceVu0Normalize(target_root, target_root);                  /* 1850 */

        inner = sceVu0InnerProduct(end_root, target_root);          /* 1853 */
        if (inner >= 0.99999f)                                      /* 1854 */
        {
            return;
        }

        r = g3dAcosf(inner);                                        /* 1856 */
        if (r > 1.57079625f)                                        /* 1859 */
        {
            r = 1.57079625f;
        }
        if (plyr_wrk.spot_rot[0] < 0.0f)                            /* 1860 */
        {
            r *= -0.25f;
        }
        r = -r;                                                     /* 1862 */

        sceVu0OuterProduct(raxis, target_root, end_root);           /* 1863 */

        LocalRotMatrixZ(cp[parent_id].matCoord,                     /* 1866 */
                        cp[parent_id].matCoord, r);
        sceVu0Normalize(raxis, raxis);                              /* 1867 */
        g3dxVu0RotMatrixAxis(m, raxis, r);                          /* 1868 */

        sceVu0SubVector(end_root, end_eff, root);                   /* 1869 */
        length = g3dxVu0Sqrt(sceVu0InnerProduct(end_root,           /* 1870 */
                                                end_root));
        sceVu0Normalize(end_root, end_root);                        /* 1872 */
        sceVu0ApplyMatrix(end_root, m, end_root);                   /* 1873 */
        sceVu0Normalize(end_root, end_root);                        /* 1874 */
        sceVu0ScaleVector(end_root, end_root, length);              /* 1875 */
        sceVu0AddVector(end_eff, end_root, root);                   /* 1876 */
    }
    while (parent_id != 0x0b);                                      /* 1882 */
}

/* --------------------------------------------------------------------------
 *  motGetFrameDataRT
 *
 *  Frames past 0: every bone's rotation, then translations for only those
 *  bones that carry one (trans_id != 0xff).  Scale is inherited from frame 0,
 *  which init_flg asks for first.
 * ------------------------------------------------------------------------ */
void motGetFrameDataRT(RST_DATA *rst, u_int *top_addr, u_int frame, u_int init_flg)
{
    float *p;
    u_int  bone_num;
    u_int  i;

    if (init_flg != 0)
    {
        motGetFrameDataRST(rst, top_addr, 0);
        if (frame == 0)
        {
            return;
        }
    }

    if (frame >= motGetFrameNum(top_addr))
    {
        frame = motGetFrameNum(top_addr) - 1;
        if (frame == 0)
        {
            return;
        }
    }

    p = (float *)motGetFrameDataAddr(top_addr, frame);

    /* Frames that share frame 0's data have nothing to add. */
    if (p == (float *)motGetFrameDataAddr(top_addr, 0))
    {
        return;
    }

    bone_num = motGetBoneNum(top_addr);

    G3DASSERT(bone_num <= BONE_MAX, MOT_STR_ARRAY_OVER);

    for (i = 0; i < bone_num; i++)
    {
        rst[i].rot[0] = p[0];
        rst[i].rot[1] = p[1];
        rst[i].rot[2] = p[2];
        p += 3;
    }

    for (i = 0; i < bone_num; i++)
    {
        if (motGetTransId(top_addr, i) != 0xff)
        {
            rst[i].trans[0] = p[0];
            rst[i].trans[1] = p[1];
            rst[i].trans[2] = p[2];
            p += 3;
        }
    }
}

/* --------------------------------------------------------------------------
 *  motSetHierarchy
 *
 *  Wire each bone's coordinate node to its parent.  coord[0] is the model root
 *  and has no parent, so bone i lives at coord[i + 1].
 * ------------------------------------------------------------------------ */
void motSetHierarchy(SGDCOORDINATE *coord, u_int *top_addr)
{
    u_int bone_num;
    u_int parent_id;
    u_int id;

    bone_num = motGetBoneNum(top_addr);
    coord->pParent = (SGDCOORDINATE *)0;

    for (id = 0; id < bone_num; id++)
    {
        parent_id = motGetParentId(top_addr, id);
        if (parent_id == 0xff)
        {
            printf("Warning : model Hierarchy is wrong\n");
            coord[id + 1].pParent = (SGDCOORDINATE *)0;
        }
        else
        {
            coord[id + 1].pParent = &coord[parent_id];
        }
    }
}

/* --------------------------------------------------------------------------
 *  SceneInitAnime
 *
 *  Bind a scene model to an ANI_CTRL.  Unlike motInitAniCtrl this takes the
 *  motion and morph packs directly rather than looking them up through
 *  anm_tbl, so anm_no is left at -1: a scene model is posed by frame number,
 *  not by the ANI_CODE playback machine.
 *
 *  motInitMotCtrl is called with a null rst_addr, so mot.rst0 / mot.rst1 stay
 *  null -- scene models decode into the shared buffers in SceneSetCoordFrame
 *  and SceneSetCoordFrameF instead of owning a pair.
 * ------------------------------------------------------------------------ */
u_int *SceneInitAnime(ANI_CTRL *ani_ctrl, u_int *mdl_p, u_int *mot_p, u_int *mim_p,
                      u_int *pkt_p, u_int mdl_no)
{
    MIME_DAT *mim_dat;
    u_int    *work;
    u_int    *p;
    u_int     num;
    u_int     i;

    motInitClearAniCtrl(ani_ctrl);

    ani_ctrl->mdl_p = mdl_p;
    num = *mdl_p;

    for (i = 0; i < num; i++)
    {
        p = (u_int*)GetFileInPak(mdl_p, i);

        switch (PAK_DAT_TYPE(p))
        {
        case MDL_DAT_TANM: ani_ctrl->tanm_p = p; break;
        case MDL_DAT_MPK:  ani_ctrl->mpk_p  = p; break;
        case MDL_DAT_PK2:  ani_ctrl->pk2_p  = p; break;
        case MDL_DAT_BWC:  ani_ctrl->bwc_p  = p; break;
        }
    }

    ani_ctrl->anm_no = -1;
    ani_ctrl->mdl_no = mdl_no;
    ani_ctrl->base_p = (HeaderSection *)(ani_ctrl->mpk_p + 8);

    if (mot_p != (u_int *)0)
    {
        motInitMotCtrl(&ani_ctrl->mot, mot_p, (u_int *)0);
        ani_ctrl->mot_num = *mot_p;
    }

    if (mim_p != (u_int *)0)
    {
        ani_ctrl->mim_num = *mim_p;
        ani_ctrl->mim  = (MIME_CTRL *)motAlign128(pkt_p);
        ani_ctrl->wmim = (WMIM_CTRL *)motAlign128((u_int *)(ani_ctrl->mim + 0x32));
        mim_dat        = (MIME_DAT *)motAlign128((u_int *)(ani_ctrl->wmim + 10));
        work           = motAlign128((u_int *)(mim_dat + 0x32));
        pkt_p = mimInitMimeCtrl(ani_ctrl->mim, mim_dat, mim_p, ani_ctrl->mpk_p, work,
                                &ani_ctrl->mim_num);

        /* Scene morphs start already applied rather than waiting to be cued. */
        for (i = 0; i < ani_ctrl->mim_num; i++)
        {
            ani_ctrl->mim[i].stat = 2;
        }

        mimInitAcsCtrl(ani_ctrl, (u_short)mdl_no);
    }

    if (manmdl_dat[mdl_no].collision == (COLLISION_DAT *)0)
    {
        ani_ctrl->collision_ctrl = (COLLISION_CTRL *)0;
    }
    else
    {
        ani_ctrl->collision_ctrl = (COLLISION_CTRL *)motAlign128(pkt_p);
        pkt_p = motAlign128((u_int *)(ani_ctrl->collision_ctrl + 1));
    }

    if (manmdl_dat[mdl_no].cdat != (CLOTH_DAT *)0)
    {
        ani_ctrl->cloth_ctrl = (CLOTH_CTRL *)motAlign128(pkt_p);
        work  = motAlign128((u_int *)(ani_ctrl->cloth_ctrl + 10));
        pkt_p = acsInitCloth(ani_ctrl->cloth_ctrl, ani_ctrl->collision_ctrl,
                             ani_ctrl->mpk_p, work, mdl_no, 0);
    }

    motSetHierarchy(ani_ctrl->base_p->coordp, ani_ctrl->mot.dat);
    return pkt_p;
}

/* --------------------------------------------------------------------------
 *  SceneInitOtherAnime
 *
 *  For scene props that are already an unpacked SGD rather than a model pack:
 *  mdl_p is the base section itself, so there is no pack walk and no +8.  The
 *  morph work area is taken from the end of the morph pack rather than pkt_p.
 * ------------------------------------------------------------------------ */
u_int *SceneInitOtherAnime(ANI_CTRL *ani_ctrl, u_int *mdl_p, u_int *mot_p, u_int *mim_p,
                           u_int *pkt_p)
{
    MIME_DAT *mim_dat;
    u_int    *work;
    u_int     i;

    ani_ctrl->base_p   = (HeaderSection *)mdl_p;
    ani_ctrl->mdl_p    = (u_int *)0;
    ani_ctrl->mpk_p    = (u_int *)0;
    ani_ctrl->pk2_p    = (u_int *)0;
    ani_ctrl->mim      = (MIME_CTRL *)0;
    ani_ctrl->bgmim    = (MIME_CTRL *)0;
    ani_ctrl->wmim     = (WMIM_CTRL *)0;
    ani_ctrl->mot_num  = 0;
    ani_ctrl->mim_num  = 0;
    ani_ctrl->bg_num   = 0;
    ani_ctrl->wmim_num = 0;
    ani_ctrl->ftype    = 0;

    if (mot_p != (u_int *)0)
    {
        motInitMotCtrl(&ani_ctrl->mot, mot_p, (u_int *)0);
        ani_ctrl->mot_num = *mot_p;
    }

    if (mim_p != (u_int *)0)
    {
        work = (u_int *)GetPakTaleAddr(mim_p);
        ani_ctrl->mim_num = *mim_p;
        ani_ctrl->mim = (MIME_CTRL *)motAlign128(work);
        mim_dat       = (MIME_DAT *)motAlign128((u_int *)(ani_ctrl->mim + 0x32));
        work          = motAlign128((u_int *)(mim_dat + 0x32));
        pkt_p = mimInitMimeCtrl(ani_ctrl->mim, mim_dat, mim_p, ani_ctrl->mpk_p, work,
                                &ani_ctrl->mim_num);

        for (i = 0; i < ani_ctrl->mim_num; i++)
        {
            ani_ctrl->mim[i].stat = 2;
        }
    }

    motSetHierarchy(ani_ctrl->base_p->coordp, ani_ctrl->mot.dat);
    return pkt_p;
}

/* --------------------------------------------------------------------------
 *  motSetInterpMatrix
 *
 *  Turn the two decoded key poses held on the ANI_CTRL into per-bone rotation
 *  matrices, normalised so the later blend stays orthonormal.
 * ------------------------------------------------------------------------ */
static void motSetInterpMatrix(ANI_CTRL *ani_ctrl, float (*start)[4][4], float (*end)[4][4])
{
    HeaderSection *pHead;
    float          m0[4][4];
    float          m1[4][4];
    u_int          i;

    pHead = ani_ctrl->base_p;

    for (i = 0; i < pHead->blocks - 2; i++)
    {
        sceRotMatrixXYZ(m0, g_matUnit, ani_ctrl->mot.rst0[i].rot);
        sceVu0Normalize(m0[0], m0[0]);
        sceVu0Normalize(m0[1], m0[1]);
        sceVu0Normalize(m0[2], m0[2]);

        sceRotMatrixXYZ(m1, g_matUnit, ani_ctrl->mot.rst1[i].rot);
        sceVu0Normalize(m1[0], m1[0]);
        sceVu0Normalize(m1[1], m1[1]);
        sceVu0Normalize(m1[2], m1[2]);

        sceVu0CopyMatrix(start[i], m0);
        sceVu0CopyMatrix(end[i], m1);
    }
}

/* --------------------------------------------------------------------------
 *  motInterpAnm
 *
 *  Apply the blended pose to the bone chain, with the sub-frame counter giving
 *  the blend weight.  The original spells the loop out again rather than
 *  sharing it with motSceneInterpAnm; the two bodies are identical apart from
 *  where the RST data and the rate come from.
 * ------------------------------------------------------------------------ */
static void motInterpAnm(ANI_CTRL *ani_ctrl, float (*start)[4][4], float (*end)[4][4])
{
    HeaderSection *pHead;
    SGDCOORDINATE *coord;
    float          interp[4][4];
    float          trans[4];
    float          scale[4];
    float          rate;
    u_int          i;

    pHead = ani_ctrl->base_p;
    coord = pHead->coordp;

    /* inp_allcnt is 0 on a motion with no interpolation frames; the pose is
     * then simply the end key. */
    if (ani_ctrl->mot.inp_allcnt == 0)
    {
        rate = 1.0f;
    }
    else
    {
        rate = ani_ctrl->mot.inp_cnt / (float)ani_ctrl->mot.inp_allcnt;
    }

    for (i = 0; i < pHead->blocks - 1; i++)
    {
        coord[i].bCalc = 0;

        if (i == 0)
        {
            sceVu0UnitMatrix(coord->matCoord);
            continue;
        }

        sceVu0UnitMatrix(interp);
        motInterpMatrix(interp, start[i - 1], end[i - 1], rate);

        sceVu0InterVector(trans, ani_ctrl->mot.rst1[i - 1].trans,
                          ani_ctrl->mot.rst0[i - 1].trans, rate);
        sceVu0InterVector(scale, ani_ctrl->mot.rst1[i - 1].scale,
                          ani_ctrl->mot.rst0[i - 1].scale, rate);

        sceVu0UnitMatrix(coord[i].matCoord);
        coord[i].matCoord[0][0] = scale[0];
        coord[i].matCoord[1][1] = scale[1];
        coord[i].matCoord[2][2] = scale[2];
        coord[i].matCoord[3][3] = 1.0f;
        sceVu0MulMatrix(coord[i].matCoord, coord[i].matCoord, interp);
        sceVu0TransMatrix(coord[i].matCoord, coord[i].matCoord, trans);
    }
}

/* --------------------------------------------------------------------------
 *  motSetCoordFrame
 *
 *  Seek to an absolute frame.  Frames are spaced inp_allcnt ticks apart
 *  (2 * interp_frame + 2), so the tick splits into a key-frame index and the
 *  sub-frame position between that key and the next.
 * ------------------------------------------------------------------------ */
void motSetCoordFrame(ANI_CTRL *ani_ctrl, u_int frame)
{
    u_int key;
    int   step;

    step = motGetInterpFrameNum(ani_ctrl->mot.dat) * 2 + 2;
    ani_ctrl->mot.inp_allcnt = step;

    if (step == 0)
    {
        key = 0;
        ani_ctrl->mot.inp_cnt = 0.0f;
        ani_ctrl->mot.old_mot_cnt = -1.0f;
    }
    else
    {
        key = (int)frame / step;
        ani_ctrl->mot.inp_cnt = (float)((int)frame % step);
    }

    motGetFrameDataRT(ani_ctrl->mot.rst0, ani_ctrl->mot.dat, key, 1);
    motGetFrameDataRT(ani_ctrl->mot.rst1, ani_ctrl->mot.dat, key + 1, 1);

    motSetInterpMatrix(ani_ctrl, m_start.data(), m_end.data());
    motInterpAnm(ani_ctrl, m_start.data(), m_end.data());
}

/* --------------------------------------------------------------------------
 *  SceneSetCoordFrame
 *
 *  Snap the skeleton to one whole frame, no interpolation.  type == 0 poses
 *  the whole chain, otherwise only the root.  Bone i is driven by rst[i - 1];
 *  coord[0] is the model root and stays identity.
 * ------------------------------------------------------------------------ */
void SceneSetCoordFrame(ANI_CTRL *ani_ctrl, u_int frame, u_int type)
{
    /* Function-local static, as in the original: BONE_MAX RST triples is far
     * more than the EE stack frame could carry. */
    static fixed_array<RST_DATA, BONE_MAX> rst;
    HeaderSection  *pHead;
    SGDCOORDINATE  *coord;
    u_int           i;

    pHead = ani_ctrl->base_p;
    coord = pHead->coordp;

    motGetFrameDataRT(rst.data(), ani_ctrl->mot.dat, frame, 1);

    if (type != 0)
    {
        sceVu0UnitMatrix(coord->matCoord);
        coord->matCoord[0][0] = rst[0].scale[0];
        coord->matCoord[1][1] = rst[0].scale[1];
        coord->matCoord[2][2] = rst[0].scale[2];
        sceRotMatrixXYZ(coord->matCoord, coord->matCoord, rst[0].rot);
        sceVu0TransMatrix(coord->matCoord, coord->matCoord, rst[0].trans);
        coord->bCalc = 0;
        return;
    }

    sceVu0UnitMatrix(coord->matCoord);
    coord->bCalc = 0;

    for (i = 1; i < pHead->blocks - 1; i++)
    {
        G3DASSERT(i - 1 < BONE_MAX, MOT_STR_ARRAY_OVER);

        sceVu0UnitMatrix(coord[i].matCoord);
        coord[i].matCoord[0][0] = rst[i - 1].scale[0];
        coord[i].matCoord[1][1] = rst[i - 1].scale[1];
        coord[i].matCoord[2][2] = rst[i - 1].scale[2];
        sceRotMatrixXYZ(coord[i].matCoord, coord[i].matCoord, rst[i - 1].rot);
        sceVu0TransMatrix(coord[i].matCoord, coord[i].matCoord, rst[i - 1].trans);
        coord[i].bCalc = 0;
    }
}

/* --------------------------------------------------------------------------
 *  SceneSetCoordFrameF
 *
 *  Fractional-frame pose: decode the bracketing key frames and blend them by
 *  the fractional part.  The next frame wraps on mot.all_cnt so a looping
 *  animation blends across the seam.  This is the entry point the scene
 *  renderer drives every frame.
 * ------------------------------------------------------------------------ */
void SceneSetCoordFrameF(ANI_CTRL *ani_ctrl, float frame, u_int type)
{
    static fixed_array<RST_DATA, BONE_MAX> rst0;
    static fixed_array<RST_DATA, BONE_MAX> rst1;
    HeaderSection  *pHead;
    SGDCOORDINATE  *coord;
    float           m0[4][4];
    float           m1[4][4];
    float           interp[4][4];
    float           trans[4];
    float           scale[4];
    float           rate;
    u_int           key;
    u_int           i;

    key   = (u_int)frame;
    rate  = frame - (float)key;
    pHead = ani_ctrl->base_p;
    coord = pHead->coordp;

    motGetFrameDataRT(rst0.data(), ani_ctrl->mot.dat, key, 1);
    motGetFrameDataRT(rst1.data(), ani_ctrl->mot.dat,
                      (int)(key + 1) % ani_ctrl->mot.all_cnt, 1);


    if (type != 0)
    {
        sceRotMatrixXYZ(m0, g_matUnit, rst0[0].rot);
        sceVu0Normalize(m0[0], m0[0]);
        sceVu0Normalize(m0[1], m0[1]);
        sceVu0Normalize(m0[2], m0[2]);

        sceRotMatrixXYZ(m1, g_matUnit, rst1[0].rot);
        sceVu0Normalize(m1[0], m1[0]);
        sceVu0Normalize(m1[1], m1[1]);
        sceVu0Normalize(m1[2], m1[2]);

        coord->bCalc = 0;
        sceVu0UnitMatrix(interp);
        motInterpMatrix(interp, m0, m1, rate);

        sceVu0InterVector(trans, rst1[0].trans, rst0[0].trans, rate);
        sceVu0InterVector(scale, rst1[0].scale, rst0[0].scale, rate);

        sceVu0UnitMatrix(coord->matCoord);
        coord->matCoord[0][0] = scale[0];
        coord->matCoord[1][1] = scale[1];
        coord->matCoord[2][2] = scale[2];
        coord->matCoord[3][3] = 1.0f;
        sceVu0MulMatrix(coord->matCoord, coord->matCoord, interp);
        sceVu0TransMatrix(coord->matCoord, coord->matCoord, trans);
        return;
    }

    /* Rotation only, per bone, normalised so the blend stays orthonormal. */
    for (i = 0; i < pHead->blocks - 2; i++)
    {
        sceRotMatrixXYZ(m0, g_matUnit, rst0[i].rot);
        sceVu0Normalize(m0[0], m0[0]);
        sceVu0Normalize(m0[1], m0[1]);
        sceVu0Normalize(m0[2], m0[2]);

        sceRotMatrixXYZ(m1, g_matUnit, rst1[i].rot);
        sceVu0Normalize(m1[0], m1[0]);
        sceVu0Normalize(m1[1], m1[1]);
        sceVu0Normalize(m1[2], m1[2]);

        sceVu0CopyMatrix(m_start[i], m0);
        sceVu0CopyMatrix(m_end[i], m1);
    }

    motSceneInterpAnm(ani_ctrl, m_start.data(), m_end.data(),
                      rst0.data(), rst1.data(), rate);
}

/* --------------------------------------------------------------------------
 *  motSceneInterpAnm
 *
 *  Blend the two decoded poses onto the bone chain.  coord[0] is the model
 *  root and is reset to identity; bone i takes start/end[i - 1].
 *
 *  NOTE the interpolation operands: PS2 sceVu0InterVector computes
 *  v0 = rate * v1 + (1 - rate) * v2.  The original therefore passes rst1
 *  first and rst0 second so translation and scale advance from the current key
 *  to the next key in the same direction as the rotation blend.
 * ------------------------------------------------------------------------ */
static void motSceneInterpAnm(ANI_CTRL *ani_ctrl, float (*start)[4][4], float (*end)[4][4],
                              RST_DATA *rst0, RST_DATA *rst1, float rate)
{
    HeaderSection *pHead;
    SGDCOORDINATE *coord;
    float          interp[4][4];
    float          trans[4];
    float          scale[4];
    u_int          i;

    pHead = ani_ctrl->base_p;
    coord = pHead->coordp;

    for (i = 0; i < pHead->blocks - 1; i++)
    {
        coord[i].bCalc = 0;

        if (i == 0)
        {
            sceVu0UnitMatrix(coord->matCoord);
            continue;
        }

        sceVu0UnitMatrix(interp);
        motInterpMatrix(interp, start[i - 1], end[i - 1], rate);

        sceVu0InterVector(trans, rst1[i - 1].trans, rst0[i - 1].trans, rate);
        sceVu0InterVector(scale, rst1[i - 1].scale, rst0[i - 1].scale, rate);

        sceVu0UnitMatrix(coord[i].matCoord);
        coord[i].matCoord[0][0] = scale[0];
        coord[i].matCoord[1][1] = scale[1];
        coord[i].matCoord[2][2] = scale[2];
        coord[i].matCoord[3][3] = 1.0f;
        sceVu0MulMatrix(coord[i].matCoord, coord[i].matCoord, interp);
        sceVu0TransMatrix(coord[i].matCoord, coord[i].matCoord, trans);
    }
}

void motSetInvMatrix(float (*m1)[4], float (*m0)[4])
{
    g3dMatrixInverseTransform(m1, m0);
}

/* Round up to the next quadword boundary. */
u_int *motAlign128(u_int *addr)
{
    if (((uintptr_t)addr & 0xf) != 0)
    {
        addr = (u_int *)((uintptr_t)addr + (0x10 - ((uintptr_t)addr & 0xf)));
    }

    return addr;
}

void motPrintVector(char *str, float *vec)
{
    printf("%s : %f,%f,%f,%f\n", str, vec[0], vec[1], vec[2], vec[3]);
}

/* Concatenate an X, then Y, then Z rotation onto m1, leaving the result in m0. */
void sceRotMatrixXYZ(float (*m0)[4], float (*m1)[4], float *rot)
{
    sceVu0FMATRIX mat;

    sceVu0RotMatrixX(mat, m1, rot[0]);
    sceVu0RotMatrixY(mat, mat, rot[1]);
    sceVu0RotMatrixZ(m0, mat, rot[2]);
}

/* Place the model, then evaluate the whole bone chain from it. */
void SetCoordinate(ANI_CTRL *ani_ctrl, float *mdl_pos, float *mdl_rot)
{
    HeaderSection *pHead;
    SGDCOORDINATE *pCoord;

    pHead  = ani_ctrl->base_p;
    pCoord = pHead->coordp;

    SetRT2BaseMtx(ani_ctrl, mdl_pos, mdl_rot);
    sgdCalcBoneCoordinate(pCoord, pHead->blocks - 1);
}

/* --------------------------------------------------------------------------
 *  SetRT2BaseMtx
 *
 *  Build the model's root matrix: the SI->PS axis conversion, spun about Y by
 *  the model's yaw, then translated to its position.  The yaw is wrapped into
 *  (-PI, PI] first.
 * ------------------------------------------------------------------------ */
void SetRT2BaseMtx(ANI_CTRL *ani_ctrl, float *mdl_pos, float *mdl_rot)
{
    SGDCOORDINATE *pCoord;
    float          ry;

    pCoord = ani_ctrl->base_p->coordp;

    sceVu0CopyMatrix(pCoord->matCoord, (float (*)[4])g_matConvertSI2PS);

    ry = mdl_rot[1] + 3.1415925f;
    if (ry > 3.1415925f)
    {
        ry -= 6.283185f;
    }
    sceVu0RotMatrixY(pCoord->matCoord, pCoord->matCoord, ry);

    pCoord->matCoord[3][0] = mdl_pos[0];
    pCoord->matCoord[3][1] = mdl_pos[1];
    pCoord->matCoord[3][2] = mdl_pos[2];
    pCoord->matCoord[3][3] = 1.0f;
}

/* mpk_p[10] points at the model's SGDCOORDINATE array; +0x40 is matLocalWorld. */
void motGetLocalWorldMatrix(float (*LocalWorld)[4], u_int *mpk_p, int BoneId)
{
    sceVu0CopyMatrix(LocalWorld, ((SGDFILEHEADER*)&mpk_p[8])->pCoord[BoneId].matLocalWorld);
}
