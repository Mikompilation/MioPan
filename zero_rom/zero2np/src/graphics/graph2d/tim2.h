/* ==========================================================================
 *  graphics/graph2d/tim2.h
 *
 *  TIM2 (.tm2) texture-container format support: header walking, per-picture
 *  clut / image / texel accessors, GS VRAM upload (Tim2Load*), and the GIF
 *  packet builders (MakeTim2Direct / MakeClutDirect / PK2SendVram ...) that push
 *  a sprite-file's textures + cluts down the PK2D ring (tim2.c).
 *
 *  This TU owns the TIM2 on-disk format type cluster (the gattr_type enum plus
 *  the FILE / PICTURE / MIPMAP / EX header structs).
 *
 *  Reconstructed from the Feb 6 2004 prototype (SLES_523.84).
 * ======================================================================== */

#ifndef _GRAPHICS_GRAPH2D_TIM2_H
#define _GRAPHICS_GRAPH2D_TIM2_H

#include <stdint.h>                 /* uintptr_t */
#include <sys/types.h>              /* u_char / u_short / u_int / u_long */
#include <libgraph.h>               /* sceGsTex0 */
#include <libvu0.h>                 /* u_long128 */

#include "g2d_draw.h"               /* Q_WORDDATA + PK2D ring */

/* --------------------------------------------------------------------------
 *  TIM2 graphics-attribute (image / clut pixel storage) types.  These index the
 *  PictFormat / ImageType / ClutType fields and select the texel size in the
 *  Get/Set accessors and the GS PSM in the upload path.
 * ------------------------------------------------------------------------ */
enum TIM2_gattr_type
{
    TIM2_NONE   = 0,
    TIM2_RGB16  = 1,
    TIM2_RGB24  = 2,
    TIM2_RGB32  = 3,
    TIM2_IDTEX4 = 4,
    TIM2_IDTEX8 = 5
};

/* --------------------------------------------------------------------------
 *  On-disk header layout.
 * ------------------------------------------------------------------------ */
typedef struct                      /* 0x10 */
{
    /* 0x0 */ u_char  FileId[4];
    /* 0x4 */ u_char  FormatVersion;
    /* 0x5 */ u_char  FormatId;
    /* 0x6 */ u_short Pictures;
    /* 0x8 */ u_char  pad[8];
} TIM2_FILEHEADER;

typedef struct                      /* 0x30 */
{
    /* 0x00 */ u_int   TotalSize;
    /* 0x04 */ u_int   ClutSize;
    /* 0x08 */ u_int   ImageSize;
    /* 0x0c */ u_short HeaderSize;
    /* 0x0e */ u_short ClutColors;
    /* 0x10 */ u_char  PictFormat;
    /* 0x11 */ u_char  MipMapTextures;
    /* 0x12 */ u_char  ClutType;
    /* 0x13 */ u_char  ImageType;
    /* 0x14 */ u_short ImageWidth;
    /* 0x16 */ u_short ImageHeight;
    /* 0x18 */ u_long  GsTex0;
    /* 0x20 */ u_long  GsTex1;
    /* 0x28 */ u_int   GsTexaFbaPabe;
    /* 0x2c */ u_int   GsTexClut;
} TIM2_PICTUREHEADER;

typedef struct                      /* 0x10 */
{
    /* 0x00 */ u_long GsMiptbp1;
    /* 0x08 */ u_long GsMiptbp2;
    /* 0x10 */ u_int  MMImageSize[1];   /* flexible / 0-length array: indexed MMImageSize[mipmap] */
} TIM2_MIPMAPHEADER;

typedef struct                      /* 0x10 */
{
    /* 0x0 */ u_char ExHeaderId[4];
    /* 0x4 */ u_int  UserSpaceSize;
    /* 0x8 */ u_int  UserDataSize;
    /* 0xc */ u_int  Reserved;
} TIM2_EXHEADER;

/* --------------------------------------------------------------------------
 *  Public entry points (tim2.c).
 * ------------------------------------------------------------------------ */
void                printClut(void *pClut, int ClutColors);
void                ResetClutAlpha(void *pClut, int ClutColors);
void                ResetPIXELAlpha(u_char *ip, int size);

int                 Tim2CheckFileHeaer(void *pTim2);
TIM2_PICTUREHEADER *Tim2GetPictureHeader(void *pTim2, int imgno);
int                 Tim2IsClut2(TIM2_PICTUREHEADER *ph);
int                 Tim2GetMipMapPictureSize(TIM2_PICTUREHEADER *ph, int mipmap,
                                             int *pWidth, int *pHeight);
TIM2_MIPMAPHEADER  *Tim2GetMipMapHeader(TIM2_PICTUREHEADER *ph, int *pSize);
void               *Tim2GetUserSpace(TIM2_PICTUREHEADER *ph, int *pSize);
void               *Tim2GetUserData(TIM2_PICTUREHEADER *ph, int *pSize);
char               *Tim2GetComment(TIM2_PICTUREHEADER *ph);
void               *Tim2GetImage(TIM2_PICTUREHEADER *ph, int mipmap);
void               *Tim2GetClut(TIM2_PICTUREHEADER *ph);

u_int               Tim2GetClutColor(TIM2_PICTUREHEADER *ph, int clut, int no);
u_int               Tim2SetClutColor(TIM2_PICTUREHEADER *ph, int clut, int no,
                                     u_int newcolor);
u_int               Tim2GetTexel(TIM2_PICTUREHEADER *ph, int mipmap, int x, int y);
u_int               Tim2SetTexel(TIM2_PICTUREHEADER *ph, int mipmap, int x, int y,
                                 u_int newtexel);
u_int               Tim2GetTextureColor(TIM2_PICTUREHEADER *ph, int mipmap,
                                        int clut, int x, int y);

u_int               Tim2LoadPicture2(TIM2_PICTUREHEADER *ph, u_int tbp, u_int cbp,
                                     u_int offset);
u_int               Tim2LoadPicture(TIM2_PICTUREHEADER *ph, u_int tbp, u_int cbp);
u_int               Tim2LoadImage2(TIM2_PICTUREHEADER *ph, u_int tbp, u_int offset);
u_int               Tim2LoadImage(TIM2_PICTUREHEADER *ph, u_int tbp);
u_int               Tim2LoadClut2(TIM2_PICTUREHEADER *ph, u_int cbp, u_int offset);
u_int               Tim2LoadClut(TIM2_PICTUREHEADER *ph, u_int cbp);
void                Tim2LoadTexture(int psm, u_int tbp, int tbw, int w, int h,
                                    u_long128 *pImage);
int                 Tim2CalcBufWidth(int psm, int w);

u_long              GetTex0RegPK(uintptr_t addr, int no, u_int offset);
u_long              GetTex0RegTM(uintptr_t addr, u_int offset);

void                MakeTim2Direct(u_int *tim2_addr, int tbp, int offset);
void                MakeClutDirect(u_int *tim2_addr, int cbp, int offset);
void                MakeClutDirect_ChrMono(u_int *tim2_addr, int cbp, int offset,
                                           u_int *mono_addr);
void                PK2SendVram(uintptr_t tm2_addr, int tbp, int cbp, int offset);
void                PK2SendVramOne(uintptr_t tm2_addr, int num, int tbp, int cbp,
                                   int offset);
void                MakeTim2SendPacket(uintptr_t tm2_addr, int offset);
void                MakeTim2SendPacket_3Dpkt(uintptr_t tm2_addr, int offset);
void                MakeTim2SendPacket_Mono(uintptr_t tm2_addr, int offset,
                                            uintptr_t mono_addr);
void                SetSprFile(uintptr_t addr);
void                SetSprFile2(uintptr_t addr, u_int offset);

#endif /* _GRAPHICS_GRAPH2D_TIM2_H */
