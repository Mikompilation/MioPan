// FILE: /home/akira_koide/zero2np/src/system/playpss/ldimage.c
//
// The GS side of the PSS movie player: it turns the IPU's decoded output into
// a PATH3 DMA chain and kicks it.
//
// The IPU writes a picture as a run of 16x16 RGBA32 macroblocks, one after
// another in memory, ordered Y-first -- which is what the "YX" in
// setLoadImageTagsYX() means: the inner loop walks down a column and the outer
// walks across.  Each macroblock therefore needs its own host-to-local
// transfer, and the chain is one CNT tag per block (GIFtag, TRXPOS, TRXDIR and
// the IMAGE GIFtag inline) followed by a REF tag pointing at the block's 1024
// bytes.  BITBLTBUF and TRXREG are set once at the head, because the
// destination page and the 16x16 transfer size never change.
//
// EOP goes on the last macroblock's IMAGE tag and an END tag closes the chain,
// so loadImage() is three register writes and the DMAC does the rest.
//
// PORT: the chain is built exactly as the ROM builds it, but it cannot be run
// by hardware here, and a DMA tag's ADDR field is 31 bits of physical address
// while the macroblocks live behind 64-bit host pointers.  Two deviations cover
// that, both local to this file and marked at the site:
//
//   - setDMAscTag() additionally stores the real pointer in the tag qword's
//     upper half, which the EE only transfers when CHCR.TTE is set (it never
//     is here) and which the ROM therefore leaves untouched.
//   - loadImage() walks the chain in software after writing the three DMAC
//     registers, and hands each macroblock to the host GS.
//
// Reconstructed from the Feb 6 2004 prototype (SLES_523.84).
// playpss.a(ldimage.o), .text 0x27a530..0x27a994; both exports plus the
// 8 statics.  The object has no data sections at all.

#include "ldimage.h"

#include "../../sdk/eeregs.h"       /* REG_DMAC_2_GIF_*                       */
#include "../../sdk/libgraph.h"     /* SCE_GS_* / SCE_GIF_*                   */
#include "../../miopan/gs/miopan_gs_c.h"  /* MioPan_GsUpload -- see loadImage */
#include "../../miopan/rendering/miopan_video.h"  /* the direct picture path  */

#include <stdint.h>
#include <string.h>

/* A macroblock is 16x16 and the IPU writes it as RGBA32. */
#define LDIMAGE_MB_SIZE         16
#define LDIMAGE_MB_QWC          0x40    /* 16 * 16 * 4 / 16                   */
#define LDIMAGE_MB_BYTES        0x400

/* The destination page is 640 pixels wide: BITBLTBUF counts that in units of
 * 64, and PSMCT32 is format 0. */
#define LDIMAGE_DBW             10
#define LDIMAGE_DPSM            0

/* Source-chain DMA tag ids. */
#define LDIMAGE_DMATAG_CNT      1
#define LDIMAGE_DMATAG_REF      3
#define LDIMAGE_DMATAG_END      7

/* The EE's uncached-accelerated window.  The ROM writes the chain through it so
 * the DMAC sees the tags without a cache flush.
 *
 * PORT: identity here -- masking a host pointer to 28 bits and relocating it to
 * 0x20000000 gives a wild pointer, not another view of the same memory, and
 * there is no cache between this store and the reader. */
#define LDIMAGE_UNCACHED(p)     (p)

static u_int *setLoadImageTagsYX(u_int *tags, void *image, int skip,
                                 int x, int y, int width, int height,
                                 int vram_adrs);
static void   setDMAscTag(u_int *p, u_int spr, void *addr, u_int irq,
                          u_int id, u_int pce, u_int qwc);
static void   setGIFtag(u_int *p, u_long regs, u_int nreg, u_int flg,
                        u_int prim, u_int pre, u_int eop, u_int nloop);
static void   setGIFad(u_int *p, u_int reg, u_long value);
static void   setBITBLTBUF(u_int *p, u_int dbp, u_int dbw, u_int dpsm);
static void   setTRXPOS(u_int *p, u_int dir, u_int dsax, u_int dsay);
static void   setTRXREG(u_int *p, u_int rrw, u_int rrh);
static void   setTRXDIR(u_int *p, u_int xdir);

static void   ldImageRunChain(const u_int *tags);   /* PORT, see loadImage */

/* --------------------------------------------------------------------------
 *  setLoadImageTags  (0x27a530)
 *
 *  The macroblocks are contiguous, so the stride between them is just their
 *  size.  The parameter exists because setLoadImageTagsYX() is written to take
 *  one; nothing in the ROM passes anything else.
 * ------------------------------------------------------------------------ */
u_int *setLoadImageTags(u_int *tags, void *image, int x, int y,
                        int width, int height, int vram_adrs)           /* 46 */
{
    /* PORT: this is the only point in the movie player that is told where the
     * picture is going, so it is where the direct path is armed.  From here
     * the renderer answers any TEX0 naming `vram_adrs` with the decoder's own
     * frame and loadImage() below stops walking the chain -- see
     * miopan/rendering/miopan_video.h for why the GS round trip cannot stand.
     * The chain is still built: it costs one pass per movie, it keeps the
     * pTagEndAdrs form of playPssSetPacket() honest. */
    MioPan_VideoBegin(vram_adrs);

    return setLoadImageTagsYX(tags, image, LDIMAGE_MB_BYTES,            /* 48 */
                              x, y, width, height, vram_adrs);
}

/* --------------------------------------------------------------------------
 *  setLoadImageTagsYX  (0x27a570)  static
 *
 *  Build the chain.  `p` walks it a quadword at a time and the return is one
 *  past the END tag, which is what lets a caller splice the chain into a longer
 *  display list.
 *
 *  Note the loop nesting: `i` is the macroblock column and `j` the row, so the
 *  source pointer advances down a column before moving right -- the order the
 *  IPU wrote them in.  Only the destination position changes per block; the
 *  transfer size and destination page were set once at the head.
 * ------------------------------------------------------------------------ */
static u_int *setLoadImageTagsYX(u_int *tags, void *image, int skip,
                                 int x, int y, int width, int height,
                                 int vram_adrs)                         /* 89 */
{
    u_int *p;
    int    mbx;
    int    mby;
    int    i;
    int    j;

    mbx = width  >> 4;                                                  /* 96 */
    mby = height >> 4;                                                  /* 97 */

    p = (u_int *)LDIMAGE_UNCACHED(tags);                                /* 121 */

    setDMAscTag(p, 0, NULL, 0, LDIMAGE_DMATAG_CNT, 0, 3);               /* 122 */
    p += 4;                                                             /* 130 */
    setGIFtag(p, SCE_GIF_PACKED_AD, 1, SCE_GIF_PACKED, 0, 0, 0, 2);     /* 131 */
    p += 4;
    setBITBLTBUF(p, (u_int)vram_adrs, LDIMAGE_DBW, LDIMAGE_DPSM);       /* 132 */
    p += 4;
    setTRXREG(p, LDIMAGE_MB_SIZE, LDIMAGE_MB_SIZE);                     /* 133 */
    p += 4;

    for (i = 0; i < mbx; i++)                                           /* 136 */
    {
        for (j = 0; j < mby; j++)                                       /* 137 */
        {
            /* The chain ends on the bottom of the last column. */
            u_int eop = (i == mbx - 1 && j == mby - 1);                  /* 138 */

            setDMAscTag(p, 0, NULL, 0, LDIMAGE_DMATAG_CNT, 0, 4);       /* 140 */
            p += 4;                                                     /* 148 */
            setGIFtag(p, SCE_GIF_PACKED_AD, 1, SCE_GIF_PACKED,          /* 149 */
                      0, 0, 0, 2);
            p += 4;
            setTRXPOS(p, 0, (u_int)(x + i * LDIMAGE_MB_SIZE),           /* 150 */
                            (u_int)(y + j * LDIMAGE_MB_SIZE));
            p += 4;
            setTRXDIR(p, 0);                                            /* 151 */
            p += 4;
            setGIFtag(p, 0, 0, SCE_GIF_IMAGE, 0, 0, eop,                /* 152 */
                      LDIMAGE_MB_QWC);
            p += 4;
            setDMAscTag(p, 0, image, 0, LDIMAGE_DMATAG_REF, 0,          /* 153 */
                        LDIMAGE_MB_QWC);
            p += 4;

            image = (void *)((char *)image + skip);                     /* 162 */
        }
    }

    setDMAscTag(p, 0, NULL, 0, LDIMAGE_DMATAG_END, 0, 0);               /* 166 */

    return p + 4;                                                       /* 176 */
}

/* --------------------------------------------------------------------------
 *  setDMAscTag  (0x27a7e0)  static
 *
 *  One source-chain DMA tag, composed into a single 64-bit word.  The upper
 *  half of the quadword is tag data the EE transfers only under CHCR.TTE, which
 *  is never set here, so the ROM writes nothing to it.
 * ------------------------------------------------------------------------ */
static void setDMAscTag(u_int *p, u_int spr, void *addr, u_int irq,
                        u_int id, u_int pce, u_int qwc)                 /* 195 */
{
    *(u_long *)p = ((u_long)spr << 63)                                  /* 198 */
                 | ((u_long)((u_int)(uintptr_t)addr & ~15u) << 32)
                 | ((u_long)irq << 31)
                 | ((u_long)id  << 28)
                 | ((u_long)pce << 26)
                 | ((u_long)qwc);                                       /* 205 */

    /* PORT: ADDR above is 31 bits and `addr` is a 64-bit host pointer, so the
     * field keeps only its low half.  The eight bytes the ROM leaves alone are
     * free, and ldImageRunChain() reads the real pointer back out of them. */
    memcpy(p + 2, &addr, sizeof(addr));
}

/* --------------------------------------------------------------------------
 *  setGIFtag  (0x27a830)  static
 *
 *  Four words rather than one 128-bit store: NLOOP and EOP, then the PRIM /
 *  FLG / NREG field, then the 64-bit register descriptor.
 * ------------------------------------------------------------------------ */
static void setGIFtag(u_int *p, u_long regs, u_int nreg, u_int flg,
                      u_int prim, u_int pre, u_int eop, u_int nloop)    /* 216 */
{
    p[0] = (eop << 15) | nloop;                                         /* 218 */
    p[1] = (pre << 14) | (prim << 15) | (flg << 26) | (nreg << 28);     /* 219 */
    p[2] = (u_int)regs;                                                 /* 220 */
    p[3] = (u_int)(regs >> 32);                                         /* 221 */
}

/* --------------------------------------------------------------------------
 *  setGIFad  (0x27a880)  static
 *
 *  One PACKED A+D item: the 64-bit value, then the register address.
 * ------------------------------------------------------------------------ */
static void setGIFad(u_int *p, u_int reg, u_long value)                 /* 224 */
{
    p[0] = (u_int)value;                                                /* 226 */
    p[1] = (u_int)(value >> 32);                                        /* 227 */
    p[2] = reg;                                                         /* 228 */
    p[3] = 0;                                                           /* 229 */
}

/* --------------------------------------------------------------------------
 *  setBITBLTBUF / setTRXPOS / setTRXREG / setTRXDIR  (0x27a8b0..0x27a958)
 *
 *  The four GS registers a host-to-local transfer needs, each one A+D item.
 * ------------------------------------------------------------------------ */
static void setBITBLTBUF(u_int *p, u_int dbp, u_int dbw, u_int dpsm)   /* 232 */
{
    setGIFad(p, SCE_GS_BITBLTBUF,                                       /* 234 */
             ((u_long)dpsm << 56) | ((u_long)dbw << 48) |
             ((u_long)dbp << 32));
}

static void setTRXPOS(u_int *p, u_int dir, u_int dsax, u_int dsay)     /* 242 */
{
    setGIFad(p, SCE_GS_TRXPOS,                                          /* 244 */
             ((u_long)dir << 59) | ((u_long)dsay << 48) |
             ((u_long)dsax << 32));
}

static void setTRXREG(u_int *p, u_int rrw, u_int rrh)                  /* 251 */
{
    setGIFad(p, SCE_GS_TRXREG, ((u_long)rrh << 32) | (u_long)rrw);      /* 253 */
}

static void setTRXDIR(u_int *p, u_int xdir)                            /* 259 */
{
    setGIFad(p, SCE_GS_TRXDIR, (u_long)xdir);                           /* 261 */
}

/* --------------------------------------------------------------------------
 *  ldImageRunChain  --  PORT, no ROM counterpart
 *
 *  Walk the chain setLoadImageTagsYX() just built and do what the DMAC would
 *  have done: accumulate the four transfer registers out of the PACKED A+D
 *  items, and when an IMAGE GIFtag arrives, hand the quadwords the next REF tag
 *  points at to the host GS.
 *
 *  The image data is never inline: the IMAGE tag is the last quadword of a CNT
 *  block and its payload is the REF block after it, which is why the pending
 *  count has to survive across one tag.
 * ------------------------------------------------------------------------ */
#define LDIMAGE_MAX_TAGS    0x4000  /* 640x448 is 1120 blocks; this is slack */

static void ldImageRunChain(const u_int *tags)
{
    sceGsLoadImage load;
    const u_int   *p       = tags;
    int            have    = 0;     /* one bit per register seen             */
    u_int          pending = 0;     /* quadwords an IMAGE tag is waiting for */
    int            guard;

    if (tags == NULL)
    {
        return;
    }

    memset(&load, 0, sizeof(load));

    for (guard = 0; guard < LDIMAGE_MAX_TAGS; guard++)
    {
        const u_int *body;
        u_long       tag;
        void        *ref;
        u_int        qwc;
        u_int        id;
        u_int        nloop;
        u_int        k;

        memcpy(&tag, p, sizeof(tag));
        memcpy(&ref, p + 2, sizeof(ref));

        qwc = (u_int)(tag & 0xffffu);
        id  = (u_int)((tag >> 28) & 7u);

        if (id == LDIMAGE_DMATAG_REF)
        {
            body = (const u_int *)ref;
            p   += 4;
        }
        else
        {
            body = p + 4;
            p   += 4 + qwc * 4;
        }

        if (pending != 0)
        {
            if (have == 0xf && body != NULL && qwc >= pending)
            {
                MioPan_GsUpload(&load, (unsigned char *)body);
            }
            pending = 0;
        }
        else
        {
            nloop = 0;

            for (k = 0; k < qwc; k++)
            {
                const u_int *qw = body + k * 4;
                u_long       lo;
                u_long       hi;

                memcpy(&lo, qw,     sizeof(lo));
                memcpy(&hi, qw + 2, sizeof(hi));

                if (nloop == 0)
                {
                    /* A GIFtag: either the PACKED A+D run that follows, or the
                     * IMAGE tag that closes the block. */
                    if ((u_int)((lo >> 58) & 3u) == SCE_GIF_IMAGE)
                    {
                        pending = (u_int)(lo & 0x7fffu);
                        break;
                    }
                    nloop = (u_int)(lo & 0x7fffu);
                    continue;
                }

                switch ((u_int)(hi & 0x7fu))
                {
                case SCE_GS_BITBLTBUF:
                    memcpy(&load.bitbltbuf, &lo, sizeof(lo));
                    load.bitbltbufaddr = SCE_GS_BITBLTBUF;
                    have |= 1;
                    break;
                case SCE_GS_TRXPOS:
                    memcpy(&load.trxpos, &lo, sizeof(lo));
                    load.trxposaddr = SCE_GS_TRXPOS;
                    have |= 2;
                    break;
                case SCE_GS_TRXREG:
                    memcpy(&load.trxreg, &lo, sizeof(lo));
                    load.trxregaddr = SCE_GS_TRXREG;
                    have |= 4;
                    break;
                case SCE_GS_TRXDIR:
                    memcpy(&load.trxdir, &lo, sizeof(lo));
                    load.trxdiraddr = SCE_GS_TRXDIR;
                    have |= 8;
                    break;
                default:
                    break;
                }

                nloop--;
            }
        }

        if (id == LDIMAGE_DMATAG_END)
        {
            break;
        }
    }
}

/* --------------------------------------------------------------------------
 *  loadImage  (0x27a958)
 *
 *  Kick the chain down PATH3.  QWC is 0 because the tags carry their own
 *  counts, and CHCR 0x105 is "to memory-mapped device, source chain, start".
 * ------------------------------------------------------------------------ */
void loadImage(u_int *tags)                                            /* 264 */
{
    REG_DMAC_2_GIF_TADR = (u_int)((uintptr_t)tags & 0x0fffffff);        /* 266 */
    REG_DMAC_2_GIF_QWC  = 0;                                            /* 267 */
    REG_DMAC_2_GIF_CHCR = 0x105;                                        /* 268 */

    /* PORT: no DMAC, and TADR above has been truncated to the ROM's 28 bits
     * anyway, so the walk takes the pointer it was handed.  Clearing STR
     * afterwards is not decoration: g3dGsPutDrawEnv() spins on that bit before
     * every draw environment, and on the EE the channel cleared it itself when
     * the chain finished.
     *
     * The walk is skipped while the direct picture path is armed.  It is the
     * expensive half of the movie player here -- 1120 swizzled uploads into
     * emulated GS memory for a 640x448 frame, which the renderer then hashes
     * and un-swizzles straight back out -- and the decoder has already handed
     * the same pixels to the renderer.  The registers are still written, and
     * STR still cleared, because g3dGsPutDrawEnv() watches that bit whichever
     * path ran. */
    if (MioPan_VideoIsLive() == 0)
    {
        ldImageRunChain(tags);
    }
    REG_DMAC_2_GIF_CHCR &= ~0x100u;
}
