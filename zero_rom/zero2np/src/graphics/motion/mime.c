// FILE: /home/zero_rom/zero2np/src/graphics/motion/mime.c
//
// PARTIAL.  Vertex application, MIME pack setup, playback control and weighted
// accessory control are reconstructed.  mimClearToScene remains a no-op, as in
// the prototype.
//
// Reconstructed from the Feb 6 2004 prototype (SLES_523.84).

#include "mim.h"
#include "mim_dat.h"
#include "../graph3d/g3ddbg.h"      /* G3DASSERT */
#include "../graph3d/ctl/fixed_array.h"  /* fixed_array<> template (inlined per TU) */
#include "mdldat.h"                      /* manmdl_dat */
#include "motion.h"                         /* motGetMotReso */
#include "../../common/packfile.h"       /* GetFileInPak */
#include "../../miopan/miopan_memory.h"  /* MioPan_IsPs2Address */

/* MIME magic, little-endian 'M','I','M','E'. */
#define MIM_FILE_MAGIC 0x454d494d

/* Accessory slots probed by mimCalcVertex when MIME_FLG_ACS is set.  The flag
 * word only has bits 1..6 available for them, hence six. */
#define MIM_ACS_MAX 6

/* Per-parts "already cleared this pass" table.  Parts ids index it directly. */
#define MIM_PARTS_MAX 30

/* Key models whose weight falls below this contribute nothing and are skipped. */
#define MIM_WEIGHT_MIN 1e-5f

/* The animation workspace reserves ten spring controls after its MIME control
 * array.  Weight names in the MIME header are fixed-width eight-byte fields. */
#define MIM_WMIM_MAX 10
#define MIM_WEIGHT_NAME_LEN 8

/* Original .sdata values at 0x3ee5a8 and 0x3ee5ac. */
#define MIM_CHARACTER_VELOCITY_SCALE 0.9f
#define MIM_FORCE_RUNAWAY_LIMIT 0.599999964f

/* MIME_CTRL::flg bit 0 -- this control drives an accessory off a WMIM spring
 * rather than a baked weight curve.  Bits 1..6 then select the accessory. */
#define MIME_FLG_ACS 0x01

/* MIME_CTRL::stat -- cued but not running / live and applied every pass. */
#define MIME_STAT_IDLE 1
#define MIME_STAT_PLAY 2

/* A MIME pak is a count word followed by records, each a 0x10 byte header
 * whose first word is the payload size, then the MIME file itself. */
#define MIM_REC_HDR_WORDS 4

/* Capacity of the per-pak part dedup tables. */
#define MIM_PARTS_BUF_MAX 50

/* MIME file header: the key-model table at +0x20 holds two words per entry,
 * [weight-curve address, key-block address].  In u_int units that is index
 * 8 + i*2 for the curve and 9 + i*2 for the key blocks. */
#define MIM_KEYMDL_TBL_IDX 8

#include <string.h>
#include "../graph3d/g3dxVu0.h"         /* g3dxVu0CopyVector */

/* --------------------------------------------------------------------------
 *  mimInitMimeCtrl
 *
 *  Bind every MIME file in a pak to a control and a data record, carving the
 *  per-part backup buffers out of tmp_p as it goes and returning the new high
 *  water mark.
 *
 *  Several morphs may target the same part; only the first allocates a backup
 *  buffer and the rest are pointed at it, which is what parts_buf/parts_p
 *  track.  Note *m_num is only ever cleared, on the failure path -- the caller
 *  sets the real count from mim_p[0] before calling, so a normal return leaves
 *  its value standing.
 * ------------------------------------------------------------------------ */
u_int *mimInitMimeCtrl(MIME_CTRL *m_ctrl, MIME_DAT *mdat, u_int *mim_p, u_int *mdl_p, u_int *tmp_p, u_int *m_num)
{
    fixed_array<u_int, MIM_PARTS_BUF_MAX>   parts_buf;
    fixed_array<u_int *, MIM_PARTS_BUF_MAX> parts_p;
    u_int *parts;
    u_int *mim_top;
    u_int *mdl_sub;
    u_int  mim_num;
    u_int  fsize;
    u_int  parts_no;
    u_int  flg;
    u_int  i, j;

    mim_num = mim_p[0];
    parts   = mim_p + MIM_REC_HDR_WORDS;

    for (i = 0; i < mim_num; i++)
    {
        fsize        = parts[0];
        mim_top      = parts + MIM_REC_HDR_WORDS;
        m_ctrl->stat = 0;
        flg          = 0;

        parts_no = mimGetPartsNo(mim_top);
        mdl_sub  = (u_int *)GetFileInPak(mdl_p, parts_no);

        if (mdl_sub == NULL)
        {
            *m_num = 0;
            return tmp_p;
        }

        parts_buf[i] = parts_no;
        parts_p[i]   = tmp_p;

        for (j = 0; j < i; j++)
        {
            if (parts_buf[j] == parts_no)
            {
                flg = 1;
                break;
            }
        }

        if (flg != 0)
        {
            /* This part already owns a backup buffer -- share it, and leave
             * tmp_p where it is so nothing new is carved off. */
            mimSetMimeDat(mdat, mim_top, parts_p[j], mdl_sub);
        }
        else
        {
            tmp_p = mimSetMimeDat(mdat, mim_top, tmp_p, mdl_sub);
        }

        mimSetMimeCtrl(m_ctrl, mdat, 0, parts_no);
        mimAddressMapping(m_ctrl->mdat->dat);
        mimSetOriVertex(m_ctrl->mdat);

        mdat++;
        m_ctrl++;
        parts = (u_int *)((u_char *)mim_top + (fsize & ~3u));
    }

    return tmp_p;
}

/* --------------------------------------------------------------------------
 *  mimGetBufSize
 *
 *  Dry run of mimInitMimeCtrl's allocation: total the backup-buffer bytes the
 *  pak will need, counting each distinct part once.  Bails out early on the
 *  same missing-part condition, returning the total accumulated so far.
 * ------------------------------------------------------------------------ */
u_int mimGetBufSize(u_int *mim_p, u_int *mdl_p)
{
    fixed_array<u_int, MIM_PARTS_BUF_MAX> parts_buf;
    PHEAD *ph;
    u_int *parts;
    void  *pak;
    u_int  mim_num;
    u_int  work_buf;
    u_int  fsize;
    u_int  parts_no;
    u_int  flg;
    u_int  i, j;

    mim_num  = mim_p[0];
    parts    = mim_p + MIM_REC_HDR_WORDS;
    work_buf = 0;

    for (i = 0; i < mim_num; i++)
    {
        fsize = parts[0];
        flg   = 0;

        parts_no = mimGetPartsNo(parts + MIM_REC_HDR_WORDS);
        pak      = GetFileInPak(mdl_p, parts_no);

        if (pak == NULL)
        {
            return work_buf;
        }

        parts_buf[i] = parts_no;

        for (j = 0; j < i; j++)
        {
            if (parts_buf[j] == parts_no)
            {
                flg = 1;
                break;
            }
        }

        if (flg == 0)
        {
            ph = (PHEAD *)((HeaderSection *)pak)->phead.get();
            work_buf += (u_int)((u_char *)ph->pUniqNormal.get() -
                                (u_char *)ph->pUniqVertex.get());
        }

        parts = (u_int *)((u_char *)(parts + MIM_REC_HDR_WORDS) + (fsize & ~3u));
    }

    return work_buf;
}

void mimInitAcsCtrl(ANI_CTRL *ani_ctrl, u_short mdl_no)
{
    u_int      i;
    u_int      j;
    u_int      k;
    WMIM_DAT  *wmim_tbl;
    char       name[100];

    if (ani_ctrl->wmim == NULL)
    {
        return;
    }

    wmim_tbl = manmdl_dat[mdl_no].wdat;
    if (wmim_tbl == NULL)
    {
        return;
    }

    u_int wmim_num = 0;
    while (wmim_tbl[wmim_num].dat != NULL)
    {
        WMIM_CTRL *w_ctrl = &ani_ctrl->wmim[wmim_num];

        w_ctrl->wdat = &wmim_tbl[wmim_num];
        for (j = 0; j < 4; j++)
        {
            w_ctrl->v[j]       = 0.0f;
            w_ctrl->w[j]       = 0.0f;
            w_ctrl->pbak[j]    = 0.0f;
            w_ctrl->old_pos[j] = 0.0f;
        }
        wmim_num++;
    }

    for (i = wmim_num; i < MIM_WMIM_MAX; i++)
    {
        ani_ctrl->wmim[i].wdat = NULL;
    }

    ani_ctrl->wmim_num = wmim_num;
    if (ani_ctrl->mim_num < ani_ctrl->wmim_num)
    {
        printf("//=================================================\n");
        printf("// Warning : ani_ctrl->mim_num < ani_ctrl->wmim_num\n");
        printf("//=================================================\n");
        printf("mim_num = %d wmim_num = %d\n",
               ani_ctrl->mim_num, ani_ctrl->wmim_num);
        ani_ctrl->mim_num  = 0;
        ani_ctrl->wmim_num = 0;
        return;
    }

    for (i = 0; i < ani_ctrl->mim_num; i++)
    {
        u_char flg = 0;
        if (ani_ctrl->mim == NULL)
        {
            printf("Warning : mim addr is NULL \n");
            return;
        }

        MIME_CTRL *m_ctrl = &ani_ctrl->mim[i];
        if (m_ctrl->flg == 0)
        {
            m_ctrl->weight_id = 0;
            continue;
        }

        strncpy(name, (char *)mimGetWeightName(m_ctrl->mdat->dat),
                MIM_WEIGHT_NAME_LEN);

        for (j = 0; j < ani_ctrl->wmim_num; j++)
        {
            WMIM_DAT *wdat = ani_ctrl->wmim[j].wdat;
            for (k = 0; wdat->dat->name[k] != '\0'; k++)
            {
                if (name[k] != (char)wdat->dat->name[k])
                {
                    break;
                }
            }

            if (wdat->dat->name[k] == '\0')
            {
                m_ctrl->weight_id = (u_char)j;
                flg = 1;
                mimRequest(m_ctrl, 0);
                break;
            }
        }

        if (flg != 0)
        {
            continue;
        }
    }
}

void mimInitLoop(ANI_CTRL *ani_ctrl)
{
    MIME_CTRL *m_ctrl;
    u_int      i;

    m_ctrl = ani_ctrl->mim;
    if (m_ctrl != NULL && ani_ctrl->mim_num != 0)
    {
        for (i = 0; i < ani_ctrl->mim_num; i++)
        {
            m_ctrl[i].loop = 0;
        }
    }
}

void mimRequest(MIME_CTRL *m_ctrl, u_char rev)
{
    if (rev == 0)
    {
        m_ctrl->frame = 0;
    }
    else
    {
        m_ctrl->frame = (int)mimGetFrameNum(m_ctrl->mdat->dat) - 1;
    }

    m_ctrl->stat = MIME_STAT_PLAY;
    m_ctrl->rev  = rev;
}

void mimRequestLastFrame(MIME_CTRL *m_ctrl, u_char rev)
{
    if (rev == 0)
    {
        m_ctrl->frame = (int)mimGetFrameNum(m_ctrl->mdat->dat) - 1;
    }
    else
    {
        m_ctrl->frame = 0;
    }

    m_ctrl->stat = MIME_STAT_PLAY;
    m_ctrl->rev  = rev;
}

int mimRequestNum(ANI_CTRL *ani_ctrl, int num, u_char rev)
{
    int mim_no;
    int i;

    if (ani_ctrl == NULL)
    {
        return 0;
    }

    mim_no = mimdatGetMimeNo(ani_ctrl, num);
    if (mim_no == -1)
    {
        return 0;
    }

    if (mim_no < (int)ani_ctrl->mim_num)
    {
        MIME_CTRL *target = &ani_ctrl->mim[mim_no];

        for (i = 0; i < (int)ani_ctrl->mim_num; i++)
        {
            MIME_CTRL *m_ctrl = &ani_ctrl->mim[i];
            if (target->parts_id == m_ctrl->parts_id && i != mim_no)
            {
                mimStop(m_ctrl);
            }
        }

        target->loop = 0;
        mimRequest(target, rev);
        return 1;
    }

    printf("mime num is over!!!!!!!!!!!\n");
    return 1;
}

int mimLoopRequestNum(ANI_CTRL *ani_ctrl, int num, u_char rev)
{
    int mim_no;
    int i;

    if (ani_ctrl == NULL)
    {
        return 0;
    }

    mim_no = mimdatGetMimeNo(ani_ctrl, num);
    if (mim_no == -1)
    {
        return 0;
    }

    if (mim_no < (int)ani_ctrl->mim_num)
    {
        MIME_CTRL *target = &ani_ctrl->mim[mim_no];

        for (i = 0; i < (int)ani_ctrl->mim_num; i++)
        {
            MIME_CTRL *m_ctrl = &ani_ctrl->mim[i];
            if (target->parts_id == m_ctrl->parts_id && i != mim_no)
            {
                mimStop(m_ctrl);
            }
        }

        target->loop = 1;
        if (target->stat == MIME_STAT_IDLE)
        {
            mimRequest(target, rev);
        }
        return 1;
    }

    printf("mime num is over!!!!!!!!!!\n");
    return 1;
}

int mimRequestNumContinue(ANI_CTRL *ani_ctrl, int num, u_char rev)
{
    int mim_no;
    int i;

    if (ani_ctrl == NULL)
    {
        return 0;
    }

    mim_no = mimdatGetMimeNo(ani_ctrl, num);
    if (mim_no == -1)
    {
        return 0;
    }

    if (mim_no < (int)ani_ctrl->mim_num)
    {
        MIME_CTRL *target = &ani_ctrl->mim[mim_no];

        for (i = 0; i < (int)ani_ctrl->mim_num; i++)
        {
            MIME_CTRL *m_ctrl = &ani_ctrl->mim[i];
            if (target->parts_id == m_ctrl->parts_id && i != mim_no)
            {
                mimStop(m_ctrl);
            }
        }

        if (target->stat == MIME_STAT_IDLE)
        {
            mimRequest(target, rev);
        }
        return 1;
    }

    printf("mime num is over!!!!!!!!!!!\n");
    return 1;
}

int mimIsUseParts(ANI_CTRL *ani_ctrl, int no)
{
    int mim_no;
    int i;

    if (ani_ctrl == NULL)
    {
        return 0;
    }

    mim_no = mimdatGetMimeNo(ani_ctrl, no);
    if (mim_no == -1)
    {
        return 0;
    }

    for (i = 0; i < (int)ani_ctrl->mim_num; i++)
    {
        MIME_CTRL *m_ctrl = &ani_ctrl->mim[i];
        if (ani_ctrl->mim[mim_no].parts_id == m_ctrl->parts_id &&
            m_ctrl->stat != MIME_STAT_IDLE)
        {
            return 1;
        }
    }

    return 0;
}

int mimIsReqMimNum(ANI_CTRL *ani_ctrl, int no)
{
    int mim_no;

    if (ani_ctrl == NULL)
    {
        return 0;
    }

    mim_no = mimdatGetMimeNo(ani_ctrl, no);
    if (mim_no == -1)
    {
        return 0;
    }

    if (mim_no < (int)ani_ctrl->mim_num)
    {
        if (ani_ctrl->mim[mim_no].stat != MIME_STAT_IDLE)
        {
            return 1;
        }
    }
    else
    {
        printf("mime num is over!!!!!!!!!!!\n");
    }

    return 0;
}

int mimStopNum(ANI_CTRL *ani_ctrl, int num)
{
    int mim_no;

    if (ani_ctrl == NULL)
    {
        return 0;
    }

    mim_no = mimdatGetMimeNo(ani_ctrl, num);
    if (mim_no == -1)
    {
        return 0;
    }

    if (mim_no < (int)ani_ctrl->mim_num)
    {
        mimStop(&ani_ctrl->mim[mim_no]);
        return 1;
    }

    printf("mime num is over!!!!!!!!!!!\n");
    return 0;
}

int mimEndStopNum(ANI_CTRL *ani_ctrl, int num)
{
    int mim_no;

    if (ani_ctrl == NULL)
    {
        return 0;
    }

    mim_no = mimdatGetMimeNo(ani_ctrl, num);
    if (mim_no == -1)
    {
        return 0;
    }

    if (mim_no < (int)ani_ctrl->mim_num)
    {
        MIME_CTRL *m_ctrl = &ani_ctrl->mim[mim_no];
        m_ctrl->frame = (int)mimGetFrameNum(m_ctrl->mdat->dat) - 1;
        m_ctrl->stat  = MIME_STAT_PLAY;
        return 1;
    }

    printf("mime num is over!!!!!!!!!!!\n");
    return 0;
}

void mimStop(MIME_CTRL *m_ctrl)
{
    m_ctrl->frame = 0;
    m_ctrl->stat  = MIME_STAT_IDLE;
    m_ctrl->rev   = 0;
}

void mimSetReso(MIME_CTRL *m_ctrl, int reso)
{
    if (m_ctrl != 0)
    {
        m_ctrl->reso = reso;
    }
}

/* --------------------------------------------------------------------------
 *  mimClearVertex
 *
 *  Restore the live packet vertices from the untouched copy taken by
 *  mimSetOriVertex, discarding whatever morphs were accumulated into it last
 *  pass.  Note mdat->vtx is the backup and mdat->pkt is what the GS reads.
 * ------------------------------------------------------------------------ */
void mimClearVertex(MIME_CTRL *m_ctrl)
{
    MIME_DAT      *mdat;
    sceVu0FVECTOR *vtx;
    sceVu0FVECTOR *pkt;
    u_int          vtx_num;
    u_int          i;

    mdat = m_ctrl->mdat;
    if (mdat == NULL)
    {
        return;
    }

    vtx_num = mdat->vtx_num;
    pkt     = mdat->pkt;
    vtx     = mdat->vtx;

    for (i = 0; i < vtx_num; i++)
    {
        g3dxVu0CopyVector(pkt[i], vtx[i]);
    }
}

/* Resolve one key-model table slot to a live pointer.  See the PORT DEVIATION
 * on mimAddressMapping: the table keeps the file-relative offsets it shipped
 * with, so every read goes through here rather than dereferencing the slot. */
static u_int *mimResolve(u_int *mim_top, u_int idx)
{
    return (u_int *)((u_char *)mim_top + mim_top[idx]);
}

/* --------------------------------------------------------------------------
 *  mimCalcVertex
 *
 *  Accumulate one MIME control's key models into the packet vertices.  Each
 *  key model carries a weight -- either sampled from its baked curve at the
 *  control's current frame, or, for accessories, taken live from the WMIM
 *  spring -- and contributes weight * delta to every vertex it touches.
 *
 *  Deltas are stored as a list of blocks, each a header of {vertex count,
 *  first vertex index} followed by that many quadword deltas, so a key model
 *  only pays for the vertices it actually moves.  Only XYZ is accumulated;
 *  the packet's W is left alone (the ROM uses vmulx.xyz / vadd.xyz).
 * ------------------------------------------------------------------------ */
void mimCalcVertex(MIME_CTRL *m_ctrl, WMIM_CTRL *wmim, u_char clear_vtx_flg)
{
    MIME_DAT      *mdat;
    u_int         *mim_top;
    u_int         *wav_addr;
    u_int         *ko_top;
    u_int         *pBlock;
    sceVu0FVECTOR *key;
    sceVu0FVECTOR *pkt;
    fixed_array<u_char, MIM_ACS_MAX> acs_flg;
    u_int          key_num;
    u_int          koblock_num;
    u_int          vtx_num;
    u_int          vtx_ofs;
    u_int          i, j, k;
    float          weight;

    mdat    = m_ctrl->mdat;
    key_num = mimGetKeymdlNum(mdat->dat);

    if (clear_vtx_flg != 0)
    {
        mimClearVertex(m_ctrl);
    }

    /* Accessory mode: collapse the set bits of flg[1..6] into a dense list so
     * key model i can look up which accessory drives it. */
    if ((m_ctrl->flg & MIME_FLG_ACS) != 0)
    {
        j = 0;
        for (i = 0; i < MIM_ACS_MAX; i++)
        {
            acs_flg[j] = 0;
            if (((int)(u_int)m_ctrl->flg >> ((i + 1) & 0x1f)) & 1)
            {
                acs_flg[j] = (u_char)i;
                j++;
            }
        }

        if (j != key_num)
        {
            printf("Warning : acsnum is Wrong!!!!\n");
        }
    }

    for (i = 0; i < key_num; i++)
    {
        if ((m_ctrl->flg & MIME_FLG_ACS) != 0)
        {
            if (wmim == NULL)
            {
                printf("Warning : wmim addr is NULL \n");
                return;
            }

            if (wmim->wdat == NULL)
            {
                continue;
            }

            /* Bit 0 of the slot picks the sign, the rest indexes the spring's
             * displacement vector -- one accessory can be driven either way
             * along the same axis. */
            if ((acs_flg[i] & 1) != 0)
            {
                weight = wmim->w[acs_flg[i] >> 1];
            }
            else
            {
                weight = -wmim->w[acs_flg[i] >> 1];
            }

            if (weight < 0.0f)
            {
                continue;
            }
        }
        else
        {
            mim_top  = mdat->dat;
            wav_addr = mimResolve(mim_top, MIM_KEYMDL_TBL_IDX + i * 2);

            weight = ((float *)wav_addr)[m_ctrl->frame];
        }

        if (weight < MIM_WEIGHT_MIN)
        {
            continue;
        }
        if (weight > 1.0f)
        {
            weight = 1.0f;
        }

        mim_top = mdat->dat;
        ko_top  = mimResolve(mim_top, MIM_KEYMDL_TBL_IDX + i * 2 + 1);

        koblock_num = ko_top[0];
        pBlock      = ko_top + 4;               /* past the 0x10 byte header */

        for (j = 0; j < koblock_num; j++)
        {
            pkt     = mdat->pkt;
            vtx_num = pBlock[0];
            vtx_ofs = pBlock[1];
            key     = (sceVu0FVECTOR *)(pBlock + 4);

            for (k = 0; k < vtx_num; k++)
            {
                pkt[vtx_ofs + k][0] += key[k][0] * weight;
                pkt[vtx_ofs + k][1] += key[k][1] * weight;
                pkt[vtx_ofs + k][2] += key[k][2] * weight;
            }

            pBlock = (u_int *)(key + vtx_num);
        }
    }
}

/* --------------------------------------------------------------------------
 *  mimSetMimeCtrl
 *
 *  Point a control at its data record and seed it from the MIME file's own
 *  header flags.  A control whose flag byte is exactly MIME_FLG_ACS is purely
 *  spring-driven, so it starts live rather than waiting to be cued.
 *
 *  Note this deliberately does not clear the whole struct -- weight_id is left
 *  as mimInitAcsCtrl set it.
 * ------------------------------------------------------------------------ */
void mimSetMimeCtrl(MIME_CTRL *m_ctrl, MIME_DAT *mdat, u_int furn_id, u_int parts_id)
{
    u_int *mim_top;
    u_int  flg;

    m_ctrl->furn_id  = (u_short)furn_id;
    m_ctrl->parts_id = (u_char)parts_id;
    m_ctrl->mdat     = mdat;
    m_ctrl->frame    = 0;
    m_ctrl->reso     = motGetMotReso();
    m_ctrl->cnt      = 0.0f;

    mim_top     = mdat->dat;
    flg         = mimGetFlg(mim_top);
    m_ctrl->flg = (u_char)flg;

    if ((flg & 0xff) == MIME_FLG_ACS)
    {
        m_ctrl->stat = MIME_STAT_PLAY;
    }
    else
    {
        m_ctrl->stat = MIME_STAT_IDLE;
    }

    m_ctrl->rev  = 0;
    m_ctrl->loop = 0;
}

/* --------------------------------------------------------------------------
 *  mimSetMimeDat
 *
 *  Fill in one data record: the MIME file, the live packet vertices taken from
 *  the model's PHEAD, and a slice of tmp_buf to hold their untouched copy.
 *
 *  The vertex count falls out of the layout -- the normal array immediately
 *  follows the vertex array, so the gap between them is the vertex count in
 *  quadwords.  Returns tmp_buf advanced past the slice just claimed.
 * ------------------------------------------------------------------------ */
u_int *mimSetMimeDat(MIME_DAT *mdat, u_int *mim_p, u_int *tmp_buf, u_int *mdl_p)
{
    PHEAD         *ph;
    sceVu0FVECTOR *pkt;
    u_int          vtx_num;

    ph = (PHEAD *)((HeaderSection *)mdl_p)->phead.get();

    mdat->dat = mim_p;

    pkt     = (sceVu0FVECTOR *)ph->pUniqVertex.get();
    vtx_num = (u_int)(((u_char *)ph->pUniqNormal.get() - (u_char *)pkt) >> 4);

    mdat->pkt     = pkt;
    mdat->vtx     = (sceVu0FVECTOR *)tmp_buf;
    mdat->vtx_num = vtx_num;

    return tmp_buf + vtx_num * 4;
}

/* --------------------------------------------------------------------------
 *  mimSetOriVertex
 *
 *  Inverse of mimClearVertex: snapshot the packet as loaded into the backup
 *  buffer, so every later pass has an unmorphed base to restore from.
 * ------------------------------------------------------------------------ */
void mimSetOriVertex(MIME_DAT *mdat)
{
    sceVu0FVECTOR *vtx;
    sceVu0FVECTOR *pkt;
    u_int          vtx_num;
    u_int          i;

    vtx_num = mdat->vtx_num;
    pkt     = mdat->pkt;
    vtx     = mdat->vtx;

    for (i = 0; i < vtx_num; i++)
    {
        g3dxVu0CopyVector(vtx[i], pkt[i]);
    }
}

/* --------------------------------------------------------------------------
 *  mimSetVertex
 *
 *  Ingame per-frame morph pass.  Walks the character's own MIME controls and
 *  then the background ones, applying each live morph and advancing its frame.
 *
 *  flg[] tracks, per parts id, whether the packet has yet to be cleared this
 *  pass: the first control to touch a part passes a non-zero clear flag into
 *  mimCalcVertex and the rest accumulate on top of it.  That is also what
 *  gates the background pass -- a background morph only runs on a part no
 *  character morph already claimed.
 * ------------------------------------------------------------------------ */
void mimSetVertex(ANI_CTRL *ani_ctrl)
{
    MIME_CTRL *m_ctrl;
    MIME_DAT  *mdat;
    u_int     *pBakDat;
    fixed_array<u_char, MIM_PARTS_MAX> flg;
    u_int      mim_num;
    u_int      bg_num;
    u_int      i;

    for (i = 0; i < MIM_PARTS_MAX; i++)
    {
        flg[i] = 1;
    }

    bg_num  = ani_ctrl->bg_num;
    mim_num = ani_ctrl->mim_num;

    for (i = 0; i < ani_ctrl->wmim_num; i++)
    {
        mimWeightCtrl(ani_ctrl, i, 1.0f);
    }

    for (i = 0; i < mim_num; i++)
    {
        m_ctrl = &ani_ctrl->mim[i];

        if (ani_ctrl->mim == NULL)
        {
            printf("Warning : mim addr is NULL \n");
            return;
        }

        mdat    = m_ctrl->mdat;
        pBakDat = mdat->dat;

        if (m_ctrl->stat == MIME_STAT_PLAY)
        {
            if (ani_ctrl->wmim == NULL)
            {
                mimCalcVertex(m_ctrl, NULL, flg[m_ctrl->parts_id]);
            }
            else
            {
                mimCalcVertex(m_ctrl, &ani_ctrl->wmim[m_ctrl->weight_id],
                              flg[m_ctrl->parts_id]);
            }

            G3DASSERT(pBakDat == mdat->dat, "");

            /* Accessory morphs are driven by the spring, not by a frame
             * counter, so only the curve-driven ones advance. */
            if ((m_ctrl->flg & MIME_FLG_ACS) == 0)
            {
                G3DASSERT(pBakDat == mdat->dat, "");
                mimAddFrame(m_ctrl, mdat->dat);
            }

            flg[m_ctrl->parts_id] = 0;
        }
    }

    for (i = 0; i < bg_num; i++)
    {
        m_ctrl = &ani_ctrl->bgmim[i];

        if (ani_ctrl->bgmim == NULL)
        {
            printf("Warning : bgmim addr is NULL \n");
            return;
        }

        mdat    = m_ctrl->mdat;
        pBakDat = mdat->dat;

        if ((flg[m_ctrl->parts_id] == 1) && (ani_ctrl->interp_flg == 0))
        {
            mimCalcVertex(m_ctrl, NULL, flg[m_ctrl->parts_id]);

            G3DASSERT(pBakDat == mdat->dat, "");
            mimAddFrame(m_ctrl, mdat->dat);

            flg[m_ctrl->parts_id] = 0;
        }
    }
}

void mimClearToScene(void)
{
}

/* --------------------------------------------------------------------------
 *  mimClearAllVertex
 *
 *  Drop every accumulated morph, character and background alike, back to the
 *  unmorphed base.  A NULL character list is only a warning -- the background
 *  list is still cleared afterwards.
 * ------------------------------------------------------------------------ */
void mimClearAllVertex(ANI_CTRL *ani_ctrl)
{
    MIME_CTRL *m_ctrl;
    u_int      mim_num;
    u_int      bg_num;
    u_int      i;

    mim_num = ani_ctrl->mim_num;
    for (i = 0; i < mim_num; i++)
    {
        m_ctrl = &ani_ctrl->mim[i];

        if (ani_ctrl->mim == NULL)
        {
            printf("Warning : mim addr is NULL\n");
            break;
        }

        mimClearVertex(m_ctrl);
    }

    bg_num = ani_ctrl->bg_num;
    for (i = 0; i < bg_num; i++)
    {
        m_ctrl = &ani_ctrl->bgmim[i];

        if (ani_ctrl->bgmim == NULL)
        {
            printf("Warning : bg_mim addr is NULL\n");
            return;
        }

        mimClearVertex(m_ctrl);
    }
}

u_char mimAddFrame(MIME_CTRL *m_ctrl, u_int *dat)
{
    int       add_frame;
    uintptr_t dat_addr;
    u_char    loop;

    /*
     * The original checks that an EE pointer does not use cache-mode bits
     * 25..27.  Native x64 pointers have unrelated bits there, so retain the
     * check only for an address that is still in the emulated PS2 window.
     */
    dat_addr = (uintptr_t)dat;
    if (MioPan_IsPs2Address(dat_addr))
    {
        G3DASSERT((((u_int)dat_addr) & 0x0e000000u) == 0, "IllegalData");
    }

    if (m_ctrl->reso == 0)
    {
        return 0;
    }

    m_ctrl->cnt += (float)m_ctrl->reso / 200.0f;
    add_frame = (int)m_ctrl->cnt;
    m_ctrl->cnt -= (float)add_frame;

    if (m_ctrl->rev == 0)
    {
        m_ctrl->frame += add_frame;
        if (m_ctrl->frame < (int)mimGetFrameNum(dat))
        {
            return 0;
        }

        loop = m_ctrl->loop;
        m_ctrl->frame = 0;
    }
    else
    {
        m_ctrl->frame -= add_frame;
        if (m_ctrl->frame >= 0)
        {
            return 0;
        }

        loop = m_ctrl->loop;
        m_ctrl->frame = (int)mimGetFrameNum(dat) - 1;
    }

    if (loop == 0)
    {
        mimStop(m_ctrl);
    }

    return 1;
}

/* --------------------------------------------------------------------------
 *  SceneMimSetVertex
 *
 *  Cutscene counterpart of mimSetVertex, driven every frame by
 *  SceneDrawManMdl.  The scene owns the timeline, so instead of advancing each
 *  control with mimAddFrame this seeks them all to the frame it is handed,
 *  clamped to the end of each morph's own curve.
 *
 *  Note frame is clamped in place: once one control runs past the end of its
 *  curve the clamped value carries into the controls after it, which is what
 *  the original does.
 * ------------------------------------------------------------------------ */
void SceneMimSetVertex(ANI_CTRL *ani_ctrl, u_int frame)
{
    MIME_CTRL *m_ctrl;
    MIME_DAT  *mdat;
    fixed_array<u_char, MIM_PARTS_MAX> flg;
    u_int      i;

    for (i = 0; i < MIM_PARTS_MAX; i++)
    {
        flg[i] = 1;
    }

    for (i = 0; i < ani_ctrl->wmim_num; i++)
    {
        mimWeightCtrl(ani_ctrl, i, 1.0f);
    }

    for (i = 0; i < ani_ctrl->mim_num; i++)
    {
        m_ctrl = &ani_ctrl->mim[i];

        if (ani_ctrl->mim == NULL)
        {
            printf("Warning : mim addr is NULL \n");
            return;
        }

        mdat = m_ctrl->mdat;

        /* Accessory morphs take their weight from the spring, so seeking them
         * to a frame would be meaningless. */
        if ((m_ctrl->flg & MIME_FLG_ACS) == 0)
        {
            if (frame < mimGetFrameNum(mdat->dat))
            {
                m_ctrl->frame = frame;
            }
            else
            {
                frame = mimGetFrameNum(mdat->dat) - 1;
                m_ctrl->frame = frame;
            }
        }

        if (ani_ctrl->wmim == NULL)
        {
            mimCalcVertex(m_ctrl, NULL, flg[m_ctrl->parts_id]);
        }
        else
        {
            mimCalcVertex(m_ctrl, &ani_ctrl->wmim[m_ctrl->weight_id],
                          flg[m_ctrl->parts_id]);
        }

        flg[m_ctrl->parts_id] = 0;
    }
}

/* --------------------------------------------------------------------------
 *  MIME file header
 *
 *      +0x00  u_int  file_id      'MIME'
 *      +0x04  u_int  map_flg      1 once the key-model table is relocated
 *      +0x08  u_int  keymdl_num
 *      +0x0c  u_int  frame_num
 *      +0x10  u_int  parts_no
 *      +0x14  u_int  flg
 *      +0x18  char   weight_name[8]
 *      +0x20  u_int  keymdl[keymdl_num][2]   vertex / normal offsets
 * ------------------------------------------------------------------------ */
u_int mimGetFrameNum(u_int *mim_top)
{
    return mim_top[3];
}

u_int mimGetKeymdlNum(u_int *mim_top)
{
    return mim_top[2];
}

u_int mimGetPartsNo(u_int *mim_top)
{
    return mim_top[4];
}

u_int mimGetFlg(u_int *mim_top)
{
    return mim_top[5];
}

u_char *mimGetWeightName(u_int *mim_top)
{
    return (u_char *)(mim_top + 6);
}

/* --------------------------------------------------------------------------
 *  mimAddressMapping
 *
 *  Mark a MIME file as relocated, once per file.
 *
 *  PORT DEVIATION -- the ROM walks the key-model table here and rewrites each
 *  offset pair in place to an absolute address (top_addr + offset), after
 *  which mimCalcVertex dereferences the slots directly.  That cannot work on a
 *  64-bit host: the table slots are 32 bits wide and the loaded file sits at a
 *  host address well above 4GB, so the rewrite truncates and every later
 *  dereference is a wild pointer.
 *
 *  So the table is left holding the file-relative offsets it shipped with and
 *  readers resolve against the file base through mimResolve instead -- the
 *  same treatment the SGD loader gives SGDSELF32 / SGDFILE32.  map_flg is
 *  still set so the "already mapped" bookkeeping matches the original.
 * ------------------------------------------------------------------------ */
void mimAddressMapping(u_int *top_addr)
{
    if (*top_addr != MIM_FILE_MAGIC)
    {
        printf("This isn't mime file\n");
        return;
    }

    if (top_addr[1] == 1)
    {
        return;
    }

    top_addr[1] = 1;
}

void mimInitWeight(ANI_CTRL *ani_ctrl)
{
    u_int i;
    u_int j;

    for (i = 0; i < ani_ctrl->wmim_num; i++)
    {
        for (j = 0; j < 4; j++)
        {
            ani_ctrl->wmim[i].v[j] = 0.0f;
            ani_ctrl->wmim[i].w[j] = 0.0f;
        }
    }
}

void mimWeightCtrl(ANI_CTRL *ani_ctrl, u_int weight_id, float scale)
{
    u_int        i;
    sceVu0FMATRIX m;
    sceVu0FVECTOR pos;
    sceVu0FVECTOR f;
    sceVu0FVECTOR char_pos;
    sceVu0FVECTOR char_v;
    sceVu0FVECTOR reverse;
    sceVu0FVECTOR force;
    HeaderSection *hs;
    WMIM_CTRL    *w_ctrl;
    WMIM_DAT     *wdat;

    if (ani_ctrl->wmim == NULL)
    {
        printf("Warning : wmim addr is NULL \n");
        return;
    }

    hs     = ani_ctrl->base_p;
    w_ctrl = &ani_ctrl->wmim[weight_id];
    wdat   = w_ctrl->wdat;

    sceVu0CopyMatrix(m, hs->coordp[wdat->bone_id].matLocalWorld);
    sceVu0CopyVector(char_pos, hs->coordp[0].matLocalWorld[3]);
    sceVu0SubVector(char_v, char_pos, w_ctrl->old_pos);
    sceVu0ScaleVector(char_v, char_v, MIM_CHARACTER_VELOCITY_SCALE);

    sceVu0ApplyMatrix(pos, m, wdat->dat->pos);
    sceVu0SubVector(f, pos, w_ctrl->pbak);
    sceVu0SubVector(f, f, char_v);
    sceVu0CopyVector(w_ctrl->pbak, pos);
    sceVu0CopyVector(w_ctrl->old_pos, char_pos);

    sceVu0ScaleVector(f, f, wdat->dat->mass);
    sceVu0ScaleVector(force, wdat->dat->gravity, wdat->dat->mass);
    sceVu0AddVector(f, f, force);
    sceVu0ScaleVector(f, f, scale);

    if (sceVu0InnerProduct(f, f) > MIM_FORCE_RUNAWAY_LIMIT)
    {
        mimInitWeight(ani_ctrl);
        return;
    }

    m[0][3] = 0.0f;
    m[1][3] = 0.0f;
    m[2][3] = 0.0f;
    m[3][0] = 0.0f;
    m[3][1] = 0.0f;
    m[3][2] = 0.0f;
    m[3][3] = 1.0f;
    sceVu0Normalize(m[0], m[0]);
    sceVu0Normalize(m[1], m[1]);
    sceVu0Normalize(m[2], m[2]);
    sceVu0InversMatrix(m, m);

    f[3] = 0.0f;
    sceVu0ApplyMatrix(f, m, f);

    sceVu0ScaleVector(reverse, w_ctrl->w, -wdat->dat->Ks);
    sceVu0AddVector(w_ctrl->v, w_ctrl->v, reverse);
    sceVu0ScaleVector(w_ctrl->v, w_ctrl->v, wdat->dat->dec);
    sceVu0AddVector(w_ctrl->w, w_ctrl->w, w_ctrl->v);
    sceVu0AddVector(w_ctrl->w, w_ctrl->w, f);

    for (i = 0; i < 3; i++)
    {
        if (w_ctrl->w[i] > 1.0f)
        {
            w_ctrl->w[i] = 1.0f;
        }
        if (w_ctrl->w[i] < -1.0f)
        {
            w_ctrl->w[i] = -1.0f;
        }
    }
}
