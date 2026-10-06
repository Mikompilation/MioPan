/* ==========================================================================
 *  sdk/libgraph.h  (SCE PS2 EE <libgraph.h> -- GS register / GIFtag builders)
 *
 *  This is NOT game code.  It reproduces the value-building macros the PlayStation
 *  2 EE SDK ships in <libgraph.h>: each GS general register has a SCE_GS_SET_<reg>
 *  macro that packs its bitfields into the 64-bit payload, and the GIF layer has
 *  SCE_GIF_SET_TAG / SCE_GS_SET_PRIM plus the SCE_GIF_PACKED_* register-descriptor
 *  selectors used in a PACKED-mode GIFtag's REGS word.
 *
 *  The prototype's 2D/3D draw code fed these macros literal arguments, so the
 *  build embedded only the resulting hex constants -- the macro names did not
 *  survive into the debug artifacts.  They are reproduced here (kept in an sdk/
 *  folder so the SDK surface does not pollute the game's own headers) and the
 *  call sites are rebuilt against them; every macro below has been verified to
 *  re-encode to the exact constant the build emitted.
 *
 *  ABI note: on the EE (ee-gcc) `long`/`u_long` is 64-bit, matching the width of
 *  a GS general register, so every SET macro yields a u_long.
 *
 *  Reconstructed from the Feb 6 2004 prototype (SLES_523.84).
 * ======================================================================== */

#ifndef _SDK_LIBGRAPH_H
#define _SDK_LIBGRAPH_H

/* ---- GS general-register addresses (the subset the draw layer writes) ---- */
#define SCE_GS_PRIM       0x00
#define SCE_GS_RGBAQ      0x01
#define SCE_GS_ST         0x02
#define SCE_GS_UV         0x03
#define SCE_GS_XYZF2      0x04
#define SCE_GS_XYZ2       0x05
#define SCE_GS_TEX0_1     0x06
#define SCE_GS_TEX0_2     0x07
#define SCE_GS_CLAMP_1    0x08
#define SCE_GS_CLAMP_2    0x09
#define SCE_GS_TEX1_1     0x14
#define SCE_GS_TEX1_2     0x15
#define SCE_GS_TEXCLUT    0x1c
#define SCE_GS_XYOFFSET_1 0x18
#define SCE_GS_PRMODE     0x1b
#define SCE_GS_TEXA       0x3b
#define SCE_GS_FOGCOL     0x3d
#define SCE_GS_TEXFLUSH   0x3f
#define SCE_GS_SCISSOR_1  0x40
#define SCE_GS_SCISSOR_2  0x41
#define SCE_GS_ALPHA_1    0x42
#define SCE_GS_ALPHA_2    0x43
#define SCE_GS_TEST_1     0x47
#define SCE_GS_TEST_2     0x48
#define SCE_GS_FRAME_1    0x4c
#define SCE_GS_FRAME_2    0x4d
#define SCE_GS_ZBUF_1     0x4e
#define SCE_GS_ZBUF_2     0x4f
#define SCE_GS_BITBLTBUF  0x50
#define SCE_GS_TRXPOS     0x51
#define SCE_GS_TRXREG     0x52
#define SCE_GS_TRXDIR     0x53
#define SCE_GS_FINISH     0x61

/* ---- GIFtag PACKED-mode register descriptors (REGS-word nibbles) -------- */
#define SCE_GIF_PACKED_PRIM    0x00
#define SCE_GIF_PACKED_RGBAQ   0x01
#define SCE_GIF_PACKED_ST      0x02
#define SCE_GIF_PACKED_UV      0x03
#define SCE_GIF_PACKED_XYZF2   0x04
#define SCE_GIF_PACKED_XYZ2    0x05
#define SCE_GIF_PACKED_FOG     0x0a
#define SCE_GIF_PACKED_AD      0x0e
#define SCE_GIF_PACKED_NOP     0x0f

/* ---- GIFtag (low 64 bits) ------------------------------------------------ *
 * NLOOP b0:15, EOP b15, PRE b46, PRIM b47:11, FLG b58:2, NREG b60:4.        */
#define SCE_GIF_SET_TAG(nloop, eop, pre, prim, flg, nreg)                     \
    ((u_long)(nloop)         | ((u_long)(eop)  << 15) | ((u_long)(pre)  << 46)\
   | ((u_long)(prim) << 47)  | ((u_long)(flg)  << 58) | ((u_long)(nreg) << 60))

/* GIF transfer mode (FLG field of the tag). */
#define SCE_GIF_PACKED    0
#define SCE_GIF_REGLIST   1
#define SCE_GIF_IMAGE     2

/* ---- PRIM register value (also the GIFtag PRIM field) ------------------- *
 * PRIM b0:3, IIP b3, TME b4, FGE b5, ABE b6, AA1 b7, FST b8, CTXT b9, FIX b10. */
#define SCE_GS_SET_PRIM(prim, iip, tme, fge, abe, aa1, fst, ctxt, fix)        \
    ((u_long)(prim)          | ((u_long)(iip)  << 3)  | ((u_long)(tme)  << 4) \
   | ((u_long)(fge)  << 5)   | ((u_long)(abe)  << 6)  | ((u_long)(aa1)  << 7) \
   | ((u_long)(fst)  << 8)   | ((u_long)(ctxt) << 9)  | ((u_long)(fix)  << 10))

/* PRIM primitive-type field. */
#define SCE_GS_PRIM_POINT    0
#define SCE_GS_PRIM_LINE     1
#define SCE_GS_PRIM_LINESTRIP 2
#define SCE_GS_PRIM_TRI      3
#define SCE_GS_PRIM_TRISTRIP 4
#define SCE_GS_PRIM_TRIFAN   5
#define SCE_GS_PRIM_SPRITE   6

#define SCE_GS_PSMT8            0x13
#define SCE_GS_PSMT8H           0x1B
#define SCE_GS_PSMT4            0x14
#define SCE_GS_PSMT4HL          0x24
#define SCE_GS_PSMT4HH          0x2C
#define SCE_GS_PSMCT32          0x00
#define SCE_GS_PSMCT24          0x01
#define SCE_GS_PSMCT16          0x02
#define SCE_GS_PSMCT16S         0x0A
#define SCE_GS_PSMZ32           0x30
#define SCE_GS_PSMZ24           0x31
#define SCE_GS_PSMZ16           0x32
#define SCE_GS_PSMZ16S          0x3A
#define SCE_GS_CLD_NO_LOAD      0x0
#define SCE_GS_CLD_LOAD         0x1

#define SCE_GS_INTERLACE        0
#define SCE_GS_NOINTERLACE      1
#define SCE_GS_NTSC             2
#define SCE_GS_PAL              3
#define SCE_GS_DTV480P          0x50
#define SCE_GS_FIELD            0
#define SCE_GS_FRAME            1

/* ---- TEX0  (CSM/CLUT-bearing texture base) ------------------------------ *
 * TBP0 b0:14, TBW b14:6, PSM b20:6, TW b26:4, TH b30:4, TCC b34, TFX b35:2,
 * CBP b37:14, CPSM b51:4, CSM b55, CSA b56:5, CLD b61:3.                    */
#define SCE_GS_SET_TEX0(tbp, tbw, psm, tw, th, tcc, tfx, cbp, cpsm, csm, csa, cld) \
    ((u_long)(tbp)           | ((u_long)(tbw)  << 14) | ((u_long)(psm)  << 20) \
   | ((u_long)(tw)   << 26)  | ((u_long)(th)   << 30) | ((u_long)(tcc)  << 34) \
   | ((u_long)(tfx)  << 35)  | ((u_long)(cbp)  << 37) | ((u_long)(cpsm) << 51) \
   | ((u_long)(csm)  << 55)  | ((u_long)(csa)  << 56) | ((u_long)(cld)  << 61))

/* ---- TEX1  (LOD / filter) ----------------------------------------------- *
 * LCM b0, MXL b2:3, MMAG b5, MMIN b6:3, MTBA b9, L b19:2, K b32:12.         */
#define SCE_GS_SET_TEX1(lcm, mxl, mmag, mmin, mtba, l, k)                     \
    ((u_long)(lcm)           | ((u_long)(mxl)  << 2)  | ((u_long)(mmag) << 5) \
   | ((u_long)(mmin) << 6)   | ((u_long)(mtba) << 9)  | ((u_long)(l)    << 19)\
   | ((u_long)(k)    << 32))

/* ---- TEXA  (alpha for non-alpha texel formats) -------------------------- *
 * TA0 b0:8, AEM b15, TA1 b32:8.                                             */
#define SCE_GS_SET_TEXA(ta0, aem, ta1)                                        \
    ((u_long)(ta0) | ((u_long)(aem) << 15) | ((u_long)(ta1) << 32))

/* ---- TEXCLUT  (CLUT buffer position) ------------------------------------ *
 * CBW b0:6, COU b6:6, COV b12:11.                                           */
#define SCE_GS_SET_TEXCLUT(cbw, cou, cov)                                     \
    ((u_long)(cbw) | ((u_long)(cou) << 6) | ((u_long)(cov) << 12))

/* ---- CLAMP  (wrap modes) ------------------------------------------------ *
 * WMS b0:2, WMT b2:2, MINU b4:10, MAXU b14:10, MINV b24:10, MAXV b34:10.    */
#define SCE_GS_SET_CLAMP(wms, wmt, minu, maxu, minv, maxv)                    \
    ((u_long)(wms)           | ((u_long)(wmt)  << 2)  | ((u_long)(minu) << 4) \
   | ((u_long)(maxu) << 14)  | ((u_long)(minv) << 24) | ((u_long)(maxv) << 34))

#define SCE_GS_CLAMP_REPEAT  0
#define SCE_GS_CLAMP_CLAMP   1

/* ---- ALPHA  (alpha-blend equation (A-B)*C>>7 + D) ----------------------- *
 * A b0:2, B b2:2, C b4:2, D b6:2, FIX b32:8.                                */
#define SCE_GS_SET_ALPHA(a, b, c, d, fix)                                     \
    ((u_long)(a) | ((u_long)(b) << 2) | ((u_long)(c) << 4) | ((u_long)(d) << 6)\
   | ((u_long)(fix) << 32))

/* ---- TEST  (alpha / dest-alpha / depth test) ---------------------------- *
 * ATE b0, ATST b1:3, AREF b4:8, AFAIL b12:2, DATE b14, DATM b15,
 * ZTE b16, ZTST b17:2.                                                      */
#define SCE_GS_SET_TEST(ate, atst, aref, afail, date, datm, zte, ztst)        \
    ((u_long)(ate)           | ((u_long)(atst) << 1)  | ((u_long)(aref) << 4) \
   | ((u_long)(afail) << 12) | ((u_long)(date) << 14) | ((u_long)(datm) << 15)\
   | ((u_long)(zte)  << 16)  | ((u_long)(ztst) << 17))

#define SCE_GS_ALPHA_GREATER  1
#define SCE_GS_DEPTH_GREATER  1

/* ---- ZBUF  (depth buffer) ----------------------------------------------- *
 * ZBP b0:9, PSM b24:4, ZMSK b32.                                            */
#define SCE_GS_SET_ZBUF(zbp, psm, zmsk)                                       \
    ((u_long)(zbp) | ((u_long)(psm) << 24) | ((u_long)(zmsk) << 32))

/* ---- FRAME  (frame buffer) ---------------------------------------------- *
 * FBP b0:9, FBW b16:6, PSM b24:6, FBMSK b32:32.                             */
#define SCE_GS_SET_FRAME(fbp, fbw, psm, fbmsk)                                \
    ((u_long)(fbp) | ((u_long)(fbw) << 16) | ((u_long)(psm) << 24)           \
   | ((u_long)(fbmsk) << 32))

/* ---- XYOFFSET  (screen-space origin, 12.4 fixed) ------------------------ *
 * OFX b0:16, OFY b32:16.                                                    */
#define SCE_GS_SET_XYOFFSET(ofx, ofy)                                         \
    ((u_long)(ofx) | ((u_long)(ofy) << 32))

/* ---- BITBLTBUF  (image-transfer src/dst buffers) ------------------------ *
 * SBP b0:14, SBW b16:6, SPSM b24:6, DBP b32:14, DBW b48:6, DPSM b56:6.      */
#define SCE_GS_SET_BITBLTBUF(sbp, sbw, spsm, dbp, dbw, dpsm)                  \
    ((u_long)(sbp)           | ((u_long)(sbw)  << 16) | ((u_long)(spsm) << 24)\
   | ((u_long)(dbp)  << 32)  | ((u_long)(dbw)  << 48) | ((u_long)(dpsm) << 56))

/* ---- TRXPOS  (image-transfer rectangle origin) -------------------------- *
 * SSAX b0:11, SSAY b16:11, DSAX b32:11, DSAY b48:11, DIR b59:2.             */
#define SCE_GS_SET_TRXPOS(ssax, ssay, dsax, dsay, dir)                        \
    ((u_long)(ssax)          | ((u_long)(ssay) << 16) | ((u_long)(dsax) << 32)\
   | ((u_long)(dsay) << 48)  | ((u_long)(dir)  << 59))

/* ---- TRXREG  (image-transfer rectangle size) ---------------------------- *
 * RRW b0:12, RRH b32:12.                                                    */
#define SCE_GS_SET_TRXREG(rrw, rrh)                                           \
    ((u_long)(rrw) | ((u_long)(rrh) << 32))

/* ---- TRXDIR  (image-transfer direction) --------------------------------- *
 * XDIR b0:2 (0 = EE->GS, 1 = GS->EE, 2 = GS->GS).                           */
#define SCE_GS_SET_TRXDIR(xdir)   ((u_long)(xdir))

/* ==========================================================================
 *  VIF1 codes (the <libvif.h> / <eekernel.h> VIFcode builders)
 *
 *  A VIFcode is the 32-bit command word at the head of a VIF packet, NOT a
 *  64-bit GS register -- so every macro below yields a u_int, and the field it
 *  is stored into is 32 bits wide (the upper word of a DMAtag qword, a
 *  sceVif1Code, etc.).  Layout:
 *      IMMEDIATE b0:16, NUM b16:8, CMD b24:7, IRQ(i-bit) b31.
 *  The DIRECT / DIRECTHL / UNPACK IMMEDIATE field carries a qword count.
 * ======================================================================== */

/* ---- VIF1 command codes (CMD field, bits 24:30) ------------------------- */
#define SCE_VIF1_NOP      0x00
#define SCE_VIF1_STCYCL   0x01
#define SCE_VIF1_OFFSET   0x02
#define SCE_VIF1_BASE     0x03
#define SCE_VIF1_ITOP     0x04
#define SCE_VIF1_STMOD    0x05
#define SCE_VIF1_MSKPATH3 0x06
#define SCE_VIF1_MARK     0x07
#define SCE_VIF1_FLUSHE   0x10
#define SCE_VIF1_FLUSH    0x11
#define SCE_VIF1_FLUSHA   0x13
#define SCE_VIF1_MSCAL    0x14
#define SCE_VIF1_MSCALF   0x15
#define SCE_VIF1_MSCNT    0x17
#define SCE_VIF1_STMASK   0x20
#define SCE_VIF1_STROW    0x30
#define SCE_VIF1_STCOL    0x31
#define SCE_VIF1_MPG      0x4a
#define SCE_VIF1_DIRECT   0x50
#define SCE_VIF1_DIRECTHL 0x51
#define SCE_VIF1_UNPACK   0x60      /* base of the UNPACK opcode range 0x60:1f */

/* ---- Generic VIFcode: pack (immediate, num, cmd) with the i-bit clear ---- */
#define SCE_VIF1_SET_CODE(immediate, num, cmd)                                \
    ((u_int)(immediate) | ((u_int)(num) << 16) | ((u_int)(cmd) << 24))

/* ---- DIRECT / DIRECTHL: route `immediate` qwords to the GIF over PATH2 --- *
 * NUM is unused; `irq` sets the interrupt (i) bit.                          */
#define SCE_VIF1_SET_DIRECT(immediate, irq)                                   \
    ((u_int)(immediate) | ((u_int)(SCE_VIF1_DIRECT) << 24) | ((u_int)(irq) << 31))
#define SCE_VIF1_SET_DIRECTHL(immediate, irq)                                 \
    ((u_int)(immediate) | ((u_int)(SCE_VIF1_DIRECTHL) << 24) | ((u_int)(irq) << 31))

/* ---- UNPACK: CMD byte = 0x60 | (m<<4) | vnvl ---------------------------- *
 * vnvl b0:4 select the element layout (V4-32 = 0xc, V2-16 = 0x4, ...), the   *
 * mask flag `m` is CMD bit 4; NUM b16:8 is the element count.  The IMMEDIATE  *
 * word holds addr b0:10, the unsigned flag usn b14, and the double-buffer /  *
 * add-to-tops flag flg b15.                                                  */
#define SCE_VIF1_SET_UNPACK(addr, num, vnvl, m, flg, usn, irq)                \
    ((u_int)(addr)           | ((u_int)(usn) << 14) | ((u_int)(flg) << 15)    \
   | ((u_int)(num)   << 16)  | ((u_int)(SCE_VIF1_UNPACK | (vnvl) | ((m) << 4)) << 24) \
   | ((u_int)(irq)   << 31))

   
#define SCE_GS_SET_DISPLAY(dx, dy, magh, magv, dw, dh) \
    ((u_long)(dx)          | ((u_long)(dy)   << 12) \
   | ((u_long)(magh) << 23) | ((u_long)(magv) << 27) \
   | ((u_long)(dw)   << 32) | ((u_long)(dh)   << 44))

/* ==========================================================================
 *  GS library functions (<libgraph.h>) — PC-port declarations.
 *
 *  Signatures adapted from the Fatal Frame 1 PC port (MikuPan sdk/sce/libgraph.h);
 *  the higher-level packet types they take (sceGsDBuff / sceGsLoadImage /
 *  sceGsStoreImage / sceGifTag) already live in sce_gs.h.  Implementations are a
 *  later (link-time) concern — a PC GS backend must service them.
 * ======================================================================== */

#include "sce_gs.h"             /* sceGsDBuff / sceGsLoadImage / sceGsStoreImage */

/* VSync interrupt callback installed with sceGsSyncVCallback. */
typedef int (*sceGsVCallbackFunc)(int);

#ifdef __cplusplus
extern "C" {
#endif

void sceGsResetGraph(short mode, short inter, short omode, short ffmode);
void sceGsResetPath(void);
int  sceGsSyncV(int mode);
int  sceGsSyncPath(int mode, u_short timeout);
sceGsVCallbackFunc sceGsSyncVCallback(sceGsVCallbackFunc func);
void sceGsSetDefDBuff(sceGsDBuff *dp, short psm, short w, short h,
                      short ztest, short zpsm, short clear);
int  sceGsSwapDBuff(sceGsDBuff *db, int id);
int  sceGsSetDefLoadImage(sceGsLoadImage *lp, short dbp, short dbw, short dpsm,
                          short x, short y, short w, short h);
int  sceGsExecLoadImage(sceGsLoadImage *lp, u_long128 *srcaddr);
int  sceGsSetDefStoreImage(sceGsStoreImage *sp, short sbp, short sbw, short spsm,
                           short x, short y, short w, short h);
int  sceGsExecStoreImage(sceGsStoreImage *sp, u_long128 *dstaddr);
int  sceGsSetDefAlphaEnv(u_long128 *addr, int mode);
void sceGsSetHalfOffset(sceGsDrawEnv1 *draw, short centerx, short centery, short halfoff);

#ifdef __cplusplus
}
#endif

#endif /* _SDK_LIBGRAPH_H */
