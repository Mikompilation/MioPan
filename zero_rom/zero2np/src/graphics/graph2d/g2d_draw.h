/* ==========================================================================
 *  graphics/graph2d/g2d_draw.h
 *
 *  The 2D primitive layer: sprite / square primitive descriptors, the source
 *  data records that feed them (Copy*DTo* converters), the VRAM<->VRAM /
 *  VRAM<->EE local-image copy helpers, and the PK2D packet ring used to push
 *  2D GIF packets down the VIF1 DMA path (g2d_draw.c).
 *
 *  This TU owns the 2D primitive type cluster (DISP_SPRT / SPRT_DAT /
 *  DISP_SPRT2 / SPRT_DAT2 / DISP_SQAR and the SQAR/GSQR/SQR4/GSQ4 source
 *  records), the DISP_STR / STR_DAT text records (referenced here and by
 *  message.c), the Q_WORDDATA quadword union (no other reconstructed header
 *  declares it yet) and the PK2D / VIF1 packet-ring work blocks.
 *
 *  Reconstructed from the Feb 6 2004 prototype (SLES_523.84).
 * ======================================================================== */

#ifndef _GRAPHICS_GRAPH2D_G2D_DRAW_H
#define _GRAPHICS_GRAPH2D_G2D_DRAW_H

#include <stdint.h>                 /* uintptr_t */
#include <sys/types.h>              /* u_char / u_short / u_int / u_long */
#include <libvu0.h>                 /* sceVu0FVECTOR / sceVu0IVECTOR / u_long128 */

/* --------------------------------------------------------------------------
 *  Quadword scratch union.
 *
 *  A single 128-bit GIF/VIF qword viewed as every width the packet builders
 *  need.  Used pervasively across the graphics layer; declared here because no
 *  earlier-reconstructed header owns it (system.h only forward-declares
 *  struct _Q_WORDDATA for SendDMASub()).
 * ------------------------------------------------------------------------ */
typedef union                       /* 0x10 */
{
    /* 0x0 */ u_long128       ul128;
    /* 0x0 */ u_long          ul64[2];
    /* 0x0 */ u_int           ui32[4];
    /* 0x0 */ float           fl32[4];
    /* 0x0 */ u_short         us16[8];
    /* 0x0 */ u_char          uc8[16];
    /* 0x0 */ sceVu0FVECTOR   fv;
    /* 0x0 */ sceVu0IVECTOR   iv;
} Q_WORDDATA;

/* One 32-bit packet word viewed either way.  The GS takes ST/Q as raw IEEE
 * bit patterns, so the packet builders compute a float and hand the same
 * storage over as a u_int rather than converting. */
typedef union                       /* 0x4 */
{
    /* 0x0 */ int   ui32;
    /* 0x0 */ float fl32;
} U32DATA;

/* --------------------------------------------------------------------------
 *  Text records.  DISP_STR is the full per-string draw descriptor; STR_DAT is
 *  the caller-supplied subset that gets copied into it.  Hosted here for now;
 *  message.c also references them.
 * ------------------------------------------------------------------------ */
typedef struct                      /* 0x38 */
{
    /* 0x00 */ u_char *str;
    /* 0x04 */ int     pos_x;
    /* 0x08 */ int     pos_y;
    /* 0x0c */ int     type;
    /* 0x10 */ u_int   r;
    /* 0x14 */ u_int   g;
    /* 0x18 */ u_int   b;
    /* 0x1c */ int     alpha;
    /* 0x20 */ int     pri;
    /* 0x24 */ int     x_wide;
    /* 0x28 */ int     y_wide;
    /* 0x2c */ int     brnch_num;
    /* 0x30 */ int     csr;
    /* 0x34 */ int     st;
} DISP_STR;

typedef struct                      /* 0x24 */
{
    /* 0x00 */ u_char *str;
    /* 0x04 */ int     pos_x;
    /* 0x08 */ int     pos_y;
    /* 0x0c */ int     type;
    /* 0x10 */ u_int   r;
    /* 0x14 */ u_int   g;
    /* 0x18 */ u_int   b;
    /* 0x1c */ int     alpha;
    /* 0x20 */ int     pri;
} STR_DAT;

/* --------------------------------------------------------------------------
 *  Sprite primitive (single-texture).  DISP_SPRT is the resolved draw record
 *  consumed by DispSprD(); SPRT_DAT is the compact caller record copied into
 *  it by CopySprDToSpr().
 * ------------------------------------------------------------------------ */
typedef struct                      /* 0x88 */
{
    /* 0x00 */ u_int  att;
    /* 0x04 */ u_int  u;
    /* 0x08 */ u_int  v;
    /* 0x0c */ u_int  w;
    /* 0x10 */ u_int  h;
    /* 0x14 */ float  crx;
    /* 0x18 */ float  cry;
    /* 0x1c */ float  csx;
    /* 0x20 */ float  csy;
    /* 0x24 */ float  x;
    /* 0x28 */ float  y;
    /* 0x2c */ u_int  z;
    /* 0x30 */ float  scw;
    /* 0x34 */ float  sch;
    /* 0x38 */ float  rot;
    /* 0x40 */ u_long gftg;
    /* 0x48 */ u_long tex0;
    /* 0x50 */ u_long tex1;
    /* 0x58 */ u_long texa;
    /* 0x60 */ u_long alphar;
    /* 0x68 */ u_long zbuf;
    /* 0x70 */ u_long test;
    /* 0x78 */ u_int  pri;
    /* 0x7c */ u_char r;
    /* 0x7d */ u_char g;
    /* 0x7e */ u_char b;
    /* 0x7f */ u_char alpha;
    /* 0x80 */ u_char col;
} DISP_SPRT;

typedef struct                      /* 0x20 */
{
    /* 0x00 */ u_long  tex0;
    /* 0x08 */ u_short u;
    /* 0x0a */ u_short v;
    /* 0x0c */ u_short w;
    /* 0x0e */ u_short h;
    /* 0x10 */ int     x;
    /* 0x14 */ int     y;
    /* 0x18 */ int     pri;
    /* 0x1c */ u_char  alpha;
    /* 0x1d */ u_char  flip;
    /* 0x1e */ u_char  bln;
} SPRT_DAT;

/* --------------------------------------------------------------------------
 *  Sprite primitive (explicit UV corners).  DISP_SPRT2 / SPRT_DAT2 are the
 *  DispSprD2() pair; the UVs are float texel coordinates that get scaled to
 *  the GS 4-bit fractional u/v on copy.
 * ------------------------------------------------------------------------ */
typedef struct                      /* 0x90 */
{
    /* 0x00 */ u_int   att;
    /* 0x04 */ u_short u1;
    /* 0x06 */ u_short v1;
    /* 0x08 */ u_short u2;
    /* 0x0a */ u_short v2;
    /* 0x0c */ float   w;
    /* 0x10 */ float   h;
    /* 0x14 */ float   crx;
    /* 0x18 */ float   cry;
    /* 0x1c */ float   csx;
    /* 0x20 */ float   csy;
    /* 0x24 */ float   x;
    /* 0x28 */ float   y;
    /* 0x2c */ u_int   z;
    /* 0x30 */ float   scw;
    /* 0x34 */ float   sch;
    /* 0x38 */ float   rot;
    /* 0x40 */ u_long  gftg;
    /* 0x48 */ u_long  tex0;
    /* 0x50 */ u_long  tex1;
    /* 0x58 */ u_long  texa;
    /* 0x60 */ u_long  alpreg;
    /* 0x68 */ u_long  zbuf;
    /* 0x70 */ u_long  test;
    /* 0x78 */ u_long  clmp;
    /* 0x80 */ u_int   pri;
    /* 0x84 */ u_char  r;
    /* 0x85 */ u_char  g;
    /* 0x86 */ u_char  b;
    /* 0x87 */ u_char  alp;
    /* 0x88 */ u_char  col;
} DISP_SPRT2;

typedef struct                      /* 0x30 */
{
    /* 0x00 */ u_long tex0;
    /* 0x08 */ float  u1;
    /* 0x0c */ float  v1;
    /* 0x10 */ float  u2;
    /* 0x14 */ float  v2;
    /* 0x18 */ float  w;
    /* 0x1c */ float  h;
    /* 0x20 */ float  x;
    /* 0x24 */ float  y;
    /* 0x28 */ int    pri;
    /* 0x2c */ u_char alpha;
} SPRT_DAT2;

/* --------------------------------------------------------------------------
 *  Square / quad primitive (4 corners, per-corner colour) consumed by
 *  DispSqrD(), plus the four caller-record flavours that feed it:
 *    SQAR_DAT - axis-aligned rect, single colour
 *    GSQR_DAT - axis-aligned rect, per-corner (gouraud) colour
 *    SQR4_DAT - free 4 corners, single colour
 *    GSQ4_DAT - free 4 corners, per-corner colour
 * ------------------------------------------------------------------------ */
typedef struct                      /* 0x80 */
{
    /* 0x00 */ u_int  att;
    /* 0x04 */ float  crx;
    /* 0x08 */ float  cry;
    /* 0x0c */ float  csx;
    /* 0x10 */ float  csy;
    /* 0x14 */ int    x[4];
    /* 0x24 */ int    y[4];
    /* 0x34 */ u_int  z;
    /* 0x38 */ float  scw;
    /* 0x3c */ float  sch;
    /* 0x40 */ float  rot;
    /* 0x48 */ u_long texa;
    /* 0x50 */ u_long alphar;
    /* 0x58 */ u_long zbuf;
    /* 0x60 */ u_long test;
    /* 0x68 */ u_int  pri;
    /* 0x6c */ u_char r[4];
    /* 0x70 */ u_char g[4];
    /* 0x74 */ u_char b[4];
    /* 0x78 */ u_char alpha;
} DISP_SQAR;

typedef struct                      /* 0x18 */
{
    /* 0x00 */ u_int  w;
    /* 0x04 */ u_int  h;
    /* 0x08 */ int    x;
    /* 0x0c */ int    y;
    /* 0x10 */ u_int  pri;
    /* 0x14 */ u_char r;
    /* 0x15 */ u_char g;
    /* 0x16 */ u_char b;
    /* 0x17 */ u_char alpha;
} SQAR_DAT;

typedef struct                      /* 0x24 */
{
    /* 0x00 */ u_int  w;
    /* 0x04 */ u_int  h;
    /* 0x08 */ int    x;
    /* 0x0c */ int    y;
    /* 0x10 */ u_int  pri;
    /* 0x14 */ u_char r[4];
    /* 0x18 */ u_char g[4];
    /* 0x1c */ u_char b[4];
    /* 0x20 */ u_char alpha;
} GSQR_DAT;

typedef struct                      /* 0x28 */
{
    /* 0x00 */ int    x[4];
    /* 0x10 */ int    y[4];
    /* 0x20 */ u_int  pri;
    /* 0x24 */ u_char r;
    /* 0x25 */ u_char g;
    /* 0x26 */ u_char b;
    /* 0x27 */ u_char alpha;
} SQR4_DAT;

typedef struct                      /* 0x34 */
{
    /* 0x00 */ int    x[4];
    /* 0x10 */ int    y[4];
    /* 0x20 */ u_int  pri;
    /* 0x24 */ u_char r[4];
    /* 0x28 */ u_char g[4];
    /* 0x2c */ u_char b[4];
    /* 0x30 */ u_char alpha;
} GSQ4_DAT;

/* --------------------------------------------------------------------------
 *  PK2D packet ring work block + VIF1->GS packet control.  The ring lives at
 *  fixed EE addresses (PACKET2D region); InitPK2Dbuf() / SwapPK2Dbuf() pin the
 *  pointers each frame.
 * ------------------------------------------------------------------------ */
typedef struct                      /* 0x10 */
{
    /* 0x00 */ Q_WORDDATA *idx_top;
    /* 0x04 */ Q_WORDDATA *idx_now;
    /* 0x08 */ Q_WORDDATA *buf_top;
    /* 0x0c */ Q_WORDDATA *buf_now;
} PK2D_WRK;

typedef struct                      /* 0x8 */
{
    /* 0x0 */ Q_WORDDATA *pp0;
    /* 0x4 */ Q_WORDDATA *pp1;
} VIF1_GS_PACKET_CTRL;

/* Local-image copy descriptor (one src/dst rect of a VRAM->VRAM blit);
 * LocalCopyLtoL() keeps a static scpw[6] indexed by copy "type". */
typedef struct                      /* 0x28 */
{
    /* 0x00 */ int stbp;
    /* 0x04 */ int sfbw;
    /* 0x08 */ int stw;
    /* 0x0c */ int sth;
    /* 0x10 */ int dtbp;
    /* 0x14 */ int dfbw;
    /* 0x18 */ int dw;
    /* 0x1c */ int dh;
    /* 0x20 */ int du;
    /* 0x24 */ int dv;
} SCREEN_COPY_WRK;

/* --------------------------------------------------------------------------
 *  Public entry points (g2d_draw.c).
 * ------------------------------------------------------------------------ */
void        InitG2DDraw(void);

/* primitive-record converters */
void        CopySprDToSpr(DISP_SPRT *s, SPRT_DAT *d);
void        CopySqrDToSqr(DISP_SQAR *s, SQAR_DAT *d);
void        CopyGSqDToSqr(DISP_SQAR *s, GSQR_DAT *d);
void        CopySq4DToSqr(DISP_SQAR *s, SQR4_DAT *d);
void        CopyGS4DToSqr(DISP_SQAR *s, GSQ4_DAT *d);
void        CopySprDToSpr2(DISP_SPRT2 *s, SPRT_DAT2 *d);

/* primitive draw */
void        DispSprD(DISP_SPRT *s);
void        DispSqrD(DISP_SQAR *s);
void        DispSprD2(DISP_SPRT2 *s);
void        SetPanel(u_int pri, float x1, float y1, float x2, float y2,
                     u_char r, u_char g, u_char b, u_char a);

/* local image copy (VRAM<->EE, VRAM<->VRAM) */
int         LocalCopyLtoBGetSize(int type);
void        LocalCopyLtoBAdrs(int type, uintptr_t ee_adrs, int v_adrs);
void        LocalCopyLtoB(int type, int no, int addr);
void        LocalCopyBtoLAdrs(int type, uintptr_t ee_adrs, int v_adrs);
void        LocalCopyBtoL(int type, int no, int addr);
void        LocalCopyLtoL(int type, int addr1, int addr2);

/* PORT-ONLY.  The screen-page registry behind the host read-back.
 *
 * Nothing draws into emulated GS memory, so a LOCAL->HOST store has no pixels
 * to hand back; MioPan_RendererReadbackScreen() supplies them and these say
 * which part of the screen the page in question is holding.  LocalCopyLtoL()
 * registers its own destination automatically -- these are for the pages built
 * by a hand-written GIF packet instead, which is photo_make.c's two.
 *
 *   G2dRegisterScreenPage  the page holds screen rect (src_*), in 640x448
 *                          frame coordinates, rendered into its top-left
 *                          corner at dst_w x dst_h.
 *   G2dForgetScreenPage    something else has been written over the page.
 *   G2dScreenToGsPage      materialise that rectangle into GS memory as
 *                          PSMCT32, for a page that is sampled as a texture
 *                          rather than read back into EE memory.  `dbw_pages`
 *                          is the page's BITBLTBUF DBW (width / 64). */
void        G2dRegisterScreenPage(int block, int src_x, int src_y, int src_w,
                                  int src_h, int dst_w, int dst_h);
void        G2dForgetScreenPage(int block);
int         G2dScreenToGsPage(int block, int dbw_pages, int src_x, int src_y,
                              int src_w, int src_h, int dst_w, int dst_h);

/* PORT-ONLY.  Re-issue a HOST->LOCAL image transfer whose GIF packet the PK2D
 * ring dropped, so the upload actually reaches emulated GS memory.  The twin of
 * tim2.c's Tim2HostUpload(), for the two builders that had no bridge of their
 * own: LocalCopyBtoLAdrs() and photo_make.c's DrawPhotoBuffer(). */
void        G2dHostUpload(int dbp, int dbw, int dpsm, int dsax, int dsay,
                          int w, int h, void *src);

/* PK2D packet ring */
void        InitPK2Dbuf(void);
void        SwapPK2Dbuf(void);
void        PK2DKick(void);
Q_WORDDATA *GetPK2Dbuf(void);
void        EndPK2Dbuf(Q_WORDDATA *addr);
Q_WORDDATA *GetPK2DbufWait(void);
void        EndPK2DbufWait(Q_WORDDATA *addr);
Q_WORDDATA *SetPK2DRefTag(int nloop, uintptr_t addr);
Q_WORDDATA *TermPK2Dbuf(void);
void        AddCNTtag(Q_WORDDATA *addr, int n);
void        SetPK2DImageTrans(uintptr_t img_addr, int nloop);

/* PK3D stub helpers + direct DMA transfer */
Q_WORDDATA *GetPK3Dbuf(void);
void        TermPK3Dbuf(void);
Q_WORDDATA *StartDmaDirectTrans(void);
Q_WORDDATA *EndDmaDirectTrans(Q_WORDDATA *tail);

/* --------------------------------------------------------------------------
 *  File-scope globals owned by g2d_draw.c.
 * ------------------------------------------------------------------------ */
extern u_char gInterlace;           /* sdata 3f0a3e : 1 = interlaced (halve Y) */

#endif /* _GRAPHICS_GRAPH2D_G2D_DRAW_H */
