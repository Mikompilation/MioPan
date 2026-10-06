// FILE: /home/zero_rom/zero2np/src/graphics/graph2d/tim2.c
//
// TIM2 (.tm2) texture-container support.
//
//   * Header walking - Tim2CheckFileHeaer validates the "TIM2"/"CLT2" magic;
//     Tim2GetPictureHeader steps the per-picture chain (TotalSize stride);
//     Tim2GetMipMapHeader / Tim2GetUserSpace / Tim2GetUserData / Tim2GetComment
//     and Tim2GetImage / Tim2GetClut resolve the sub-blocks inside a picture.
//   * Pixel accessors - Tim2Get/SetClutColor and Tim2Get/SetTexel/TextureColor
//     read or write a single clut entry / texel honouring the ImageType /
//     ClutType pixel storage (RGB16/24/32, 4/8-bit indexed) and the GS 32-entry
//     CSM1 clut swizzle.
//   * GS upload - Tim2LoadImage2 / Tim2LoadClut2 (and the *Picture / no-offset
//     wrappers) patch the stored GS TEX0 / MIPTBP registers with the resolved
//     VRAM addresses and DMA each mip level up via Tim2LoadTexture's
//     sceGsSetDefLoadImage / sceGsExecLoadImage loop.
//   * Packet builders - MakeTim2Direct / MakeClutDirect[_ChrMono] emit a GIF
//     packet (BITBLTBUF/TRXPOS/TRXREG + image trans) through the PK2D ring; the
//     PK2SendVram / SetSprFile family walk a sprite-file's picture-offset table
//     and emit one pair per texture.
//
// The packed hex literals written into the GIF qwords and folded into TEX0 /
// MIPTBP are GS register payloads; they are preserved verbatim from the build.
//
// Reconstructed from the Feb 6 2004 prototype (SLES_523.84).

#include "tim2.h"                   // this TU's API + the TIM2 format cluster

#include <stdio.h>                  // printf
#include <stdint.h>                 // uintptr_t
#include <sifdev.h>                 // FlushCache
#include <libgraph.h>               // sceGsSetDefLoadImage / sceGsExecLoadImage / sceGsSyncPath

#include "../../sdk/libgraph.h"     // SCE GS register / GIFtag value builders (SCE_GS_SET_*)
#include "../../miopan/miopan_memory.h" // MioPan_GetHostPointer
#include "../../miopan/gs/miopan_gs_c.h"

// ──────────────────────────────────────────────────────────────────────
// Engine assert shim: see common/utility2.h.
#include "../../common/utility2.h" // PRINT_ASSERT + SetAssertPreMessage / PrintAssertReal

// ──────────────────────────────────────────────────────────────────────
// Forward declaration for the file-static buffer-size helper.

static int Tim2CalcBufSize(int psm, int w, int h);

static void Tim2HostUpload(int dbp, int dbw, int dpsm, int w, int h, void *src)
{
    sceGsLoadImage li;

    if (src == (void *)0 || w <= 0 || h <= 0)
    {
        return;
    }

    sceGsSetDefLoadImage(&li, (short)dbp, (short)dbw, (short)dpsm,
                         0, 0, (short)w, (short)h);
    MioPan_GsUpload(&li, (unsigned char *)src);
}

static unsigned char *Tim2GetHostAddress(uintptr_t addr)
{
    return (unsigned char *)MioPan_GetHostPointer(addr);
}

// ──────────────────────────────────────────────────────────────────────
// Debug: dump a clut as index/value pairs.

void printClut(void *pClut, int ClutColors)
{
    int    i;
    u_int *pointer;

    pointer = (u_int *)pClut;
    for (i = 0; i < ClutColors; i++)
    {
        printf("%d %x\n", i, *pointer);
        pointer++;
    }
}

// ──────────────────────────────────────────────────────────────────────
// Clamp the per-entry alpha byte of every clut colour back into 0..0x80 range
// (GS treats 0x80 as fully opaque; values above are wrapped).

void ResetClutAlpha(void *pClut, int ClutColors)
{
    int     i;
    u_char *pointer;

    pointer = (u_char *)pClut + 3;
    for (i = ClutColors; i > 0; i--)
    {
        if (*pointer > 0x80)
        {
            *pointer = *pointer + 0x81;
        }
        pointer += 4;
    }
}

// ──────────────────────────────────────────────────────────────────────
// Per-pixel alpha fix-up pass over a 16-bit image (empty in this build - the
// loop body was compiled away).

void ResetPIXELAlpha(u_char *ip, int size)
{
    int i;

    for (i = size / 2; i > 0; i--)
    {
    }
}

// ──────────────────────────────────────────────────────────────────────
// Validate a TIM2 file header.  Returns 1 for a plain "TIM2" file, 2 for a
// "CLT2" clut-only file, 0 on a broken header.

int Tim2CheckFileHeaer(void *pTim2)
{
    TIM2_FILEHEADER *pFileHdr;
    int              i;

    pFileHdr = (TIM2_FILEHEADER *)pTim2;
    if ((*(short *)&pFileHdr->FileId[0] == 0x4954) && (*(short *)&pFileHdr->FileId[2] == 0x324d))
    {
        i = 1;
    }
    else if (*(short *)&pFileHdr->FileId[0] == 0x4c43 && *(short *)&pFileHdr->FileId[2] == 0x3254)
    {
        i = 2;
    }
    else
    {
        printf("Tim2CheckFileHeaer: TIM2 is broken %02X,%02X,%02X,%02X\n",
               pFileHdr->FileId[0], pFileHdr->FileId[1], pFileHdr->FileId[2], pFileHdr->FileId[3]);
        return 0;
    }

    if ((pFileHdr->FormatVersion != 3) &&
        ((pFileHdr->FormatVersion != 4) || (pFileHdr->FormatId > 1)))
    {
        printf("Tim2CheckFileHeaer: TIM2 is broken (2)\n");
        i = 0;
    }
    return i;
}

// ──────────────────────────────────────────────────────────────────────
// Resolve the picture header for image `imgno` by walking the TotalSize chain.
// The first picture follows the 0x10 file header, or sits at 0x80 when the
// FormatId is non-zero (aligned layout).

TIM2_PICTUREHEADER *Tim2GetPictureHeader(void *pTim2, int imgno)
{
    TIM2_PICTUREHEADER *pPictHdr;
    int                 i;

    if (imgno < (int)(u_int)((TIM2_FILEHEADER *)pTim2)->Pictures)
    {
        pPictHdr = (TIM2_PICTUREHEADER *)((uintptr_t)pTim2 + 0x10);
        if (((TIM2_FILEHEADER *)pTim2)->FormatId != 0)
        {
            pPictHdr = (TIM2_PICTUREHEADER *)((uintptr_t)pTim2 + 0x80);
        }
        for (i = imgno; i > 0; i--)
        {
            pPictHdr = (TIM2_PICTUREHEADER *)((uintptr_t)pPictHdr + pPictHdr->TotalSize);
        }
    }
    else
    {
        printf("Tim2GetPictureHeader: Illegal image no.(%d)\n");
        pPictHdr = (TIM2_PICTUREHEADER *)0;
    }
    return pPictHdr;
}

// ──────────────────────────────────────────────────────────────────────
// True when the picture has no mipmap chain (MipMapTextures == 0).

int Tim2IsClut2(TIM2_PICTUREHEADER *ph)
{
    return (int)(ph->MipMapTextures == 0);
}

// ──────────────────────────────────────────────────────────────────────
// Compute the size (and optionally width/height) of mip level `mipmap`, rounded
// up to a 16-byte boundary.

int Tim2GetMipMapPictureSize(TIM2_PICTUREHEADER *ph, int mipmap, int *pWidth, int *pHeight)
{
    int w;
    int h;
    int n;

    w = (int)(u_int)(u_short)ph->ImageWidth >> (mipmap & 0x1f);
    h = (int)(u_int)(u_short)ph->ImageHeight >> (mipmap & 0x1f);
    if (pWidth != (int *)0)
    {
        *pWidth = w;
    }
    if (pHeight != (int *)0)
    {
        *pHeight = h;
    }
    n = w * h;
    switch (ph->ImageType)
    {
    case TIM2_RGB16:
        n = n * 2;
        break;
    case TIM2_RGB24:
        n = n * 3;
        break;
    case TIM2_RGB32:
        n = n * 4;
        break;
    case TIM2_IDTEX4:
        n = n / 2;
        break;
    case TIM2_IDTEX8:
        break;
    default:
        break;
    }
    return (n + 0xf) & 0xfffffff0;
}

// ──────────────────────────────────────────────────────────────────────
// The mipmap header (GsMiptbp1/2 + per-level sizes) follows the picture header
// only when there are 2+ mip levels.  `mmsize[]` maps MipMapTextures count to
// the picture-header size that precedes it.

TIM2_MIPMAPHEADER *Tim2GetMipMapHeader(TIM2_PICTUREHEADER *ph, int *pSize)
{
    TIM2_MIPMAPHEADER *pMmHdr;
    static char        mmsize[8] = { 0, 0, 32, 32, 32, 48, 48, 48 };

    pMmHdr = (TIM2_MIPMAPHEADER *)0;
    if (ph->MipMapTextures > 1)
    {
        pMmHdr = (TIM2_MIPMAPHEADER *)(ph + 1);
    }
    if (pSize != (int *)0)
    {
        *pSize = (int)mmsize[ph->MipMapTextures];
    }
    return pMmHdr;
}

// ──────────────────────────────────────────────────────────────────────
// Locate the extended-header user space that trails the fixed picture header.
// `mmsize[]` maps MipMapTextures count to the fixed picture-header size.

void *Tim2GetUserSpace(TIM2_PICTUREHEADER *ph, int *pSize)
{
    void       *pUserSpace;
    static char mmsize[8] = { 48, 48, 80, 80, 80, 96, 96, 96 };

    if ((u_long)(u_short)ph->HeaderSize == (long)mmsize[ph->MipMapTextures])
    {
        if (pSize != (int *)0)
        {
            *pSize = 0;
        }
        return (void *)0;
    }
    pUserSpace = (void *)((uintptr_t)ph + (int)mmsize[ph->MipMapTextures]);
    if (pSize != (int *)0)
    {
        if (*(u_int *)pUserSpace == 0x745865)   /* 'eXt' */
        {
            *pSize = ((TIM2_EXHEADER *)pUserSpace)->UserSpaceSize;
        }
        else
        {
            *pSize = (u_int)(u_short)ph->HeaderSize - (int)mmsize[ph->MipMapTextures];
        }
    }
    return pUserSpace;
}

// ──────────────────────────────────────────────────────────────────────
// Skip past the extended header (if present) to the raw user data block.

void *Tim2GetUserData(TIM2_PICTUREHEADER *ph, int *pSize)
{
    void *pUserSpace;

    pUserSpace = Tim2GetUserSpace(ph, pSize);
    if ((pUserSpace != (void *)0) && (*(u_int *)pUserSpace == 0x745865))
    {
        pUserSpace = (void *)((uintptr_t)pUserSpace + 0x10);
        if (pSize != (int *)0)
        {
            *pSize = ((TIM2_EXHEADER *)((uintptr_t)pUserSpace - 0x10))->UserDataSize;
        }
    }
    return pUserSpace;
}

// ──────────────────────────────────────────────────────────────────────
// Return the NUL-terminated comment that follows the user data, or NULL.

char *Tim2GetComment(TIM2_PICTUREHEADER *ph)
{
    void *pUserSpace;

    pUserSpace = Tim2GetUserSpace(ph, (int *)0);
    if (pUserSpace == (void *)0)
    {
        return (char *)pUserSpace;
    }
    if (*(u_int *)pUserSpace != 0x745865)
    {
        return (char *)0;
    }
    if (((TIM2_EXHEADER *)pUserSpace)->UserSpaceSize ==
        ((TIM2_EXHEADER *)pUserSpace)->UserDataSize + 0x10)
    {
        return (char *)0;
    }
    return (char *)((uintptr_t)pUserSpace + ((TIM2_EXHEADER *)pUserSpace)->UserDataSize + 0x10);
}

// ──────────────────────────────────────────────────────────────────────
// Pointer to the image data for mip level `mipmap` (level 0 follows the
// header; deeper levels are reached by adding the per-level MMImageSize).

void *Tim2GetImage(TIM2_PICTUREHEADER *ph, int mipmap)
{
    void  *pImage;
    int    i;
    u_int *pMMImageSize;

    pImage = (void *)0;
    if (mipmap < (int)(u_int)ph->MipMapTextures)
    {
        pImage = (void *)((uintptr_t)ph + (u_int)(u_short)ph->HeaderSize);
        if (ph->MipMapTextures != 1)
        {
            pMMImageSize = ((TIM2_MIPMAPHEADER *)(ph + 1))->MMImageSize;
            for (i = mipmap; i > 0; i--)
            {
                pImage = (void *)((uintptr_t)pImage + (int)*pMMImageSize);
                pMMImageSize++;
            }
        }
    }
    return pImage;
}

// ──────────────────────────────────────────────────────────────────────
// Pointer to the clut data (trails the level-0 image), or NULL when paletteless.

void *Tim2GetClut(TIM2_PICTUREHEADER *ph)
{
    void *pClut;

    pClut = (void *)0;
    if (ph->ClutColors != 0)
    {
        pClut = (void *)((uintptr_t)ph + ph->ImageSize + (u_int)(u_short)ph->HeaderSize);
    }
    return pClut;
}

// ──────────────────────────────────────────────────────────────────────
// Read clut entry `no` of palette bank `clut` as a packed 0xAABBGGRR colour.
// The 0x305 (RGB32 / 32-colour CSM1) cases unswizzle the GS 8/8/8 block order.

u_int Tim2GetClutColor(TIM2_PICTUREHEADER *ph, int clut, int no)
{
    u_char  *pClut;
    int      n;
    u_char   r;
    u_char   g;
    u_char   b;
    u_char   a;
    u_short  *pClut16;
    u_short  type;

    pClut = (u_char *)Tim2GetClut(ph);
    if (pClut == (u_char *)0)
    {
        return 0;
    }
    if (ph->ImageType == TIM2_IDTEX4)
    {
        n = (clut << 4) + no;
    }
    else if (ph->ImageType == TIM2_IDTEX8)
    {
        n = (clut << 8) + no;
    }
    else
    {
        return 0;
    }
    if ((int)(u_int)(u_short)ph->ClutColors < n)
    {
        return 0;
    }

    type = (u_short)((ph->ClutType << 8) | ph->ImageType);
    switch (type)
    {
    case 0x105:
    case 0x205:
    case 0x305:
    case 0x4104:
    case 0x4204:
    case 0x4304:
        /* 32-entry CSM1 clut: swap the 2nd/3rd 8-entry sub-blocks. */
        if ((n & 0x1f) >= 8)
        {
            if ((n & 0x1f) < 0x10)
            {
                n = n + 8;
            }
            else if ((n & 0x1f) < 0x18)
            {
                n = n - 8;
            }
        }
        break;
    default:
        break;
    }

    switch (ph->ClutType & 0x3f)
    {
    case TIM2_RGB16:
        pClut16 = (u_short *)(pClut + n * 2);
        r = (u_char)(((u_char)*pClut16 & 0x1f) << 3);
        g = (u_char)((*pClut16 >> 2) & 0xf8);
        b = (u_char)((*pClut16 >> 7) & 0xf8);
        a = (u_char)((*pClut16 >> 8) & 0x80);
        break;
    case TIM2_RGB24:
        pClut = pClut + n * 3;
        r = pClut[0];
        g = pClut[1];
        b = pClut[2];
        a = 0x80;
        break;
    case TIM2_RGB32:
        pClut = pClut + n * 4;
        r = pClut[0];
        g = pClut[1];
        b = pClut[2];
        a = pClut[3];
        break;
    default:
        r = 0;
        g = 0;
        b = 0;
        a = 0;
        break;
    }
    return ((u_int)a << 0x18) | (u_int)r | ((u_int)b << 0x10) | ((u_int)g << 8);
}

// ──────────────────────────────────────────────────────────────────────
// Overwrite clut entry `no` of bank `clut` with `newcolor`; returns the old
// colour.  Same CSM1 unswizzle as the getter.

u_int Tim2SetClutColor(TIM2_PICTUREHEADER *ph, int clut, int no, u_int newcolor)
{
    u_char  *pClut;
    u_char   r;
    u_char   g;
    u_char   b;
    u_char   a;
    int      n;
    u_short  *pClut16;
    u_short   type;
    u_short   old16;

    pClut = (u_char *)Tim2GetClut(ph);
    if (pClut == (u_char *)0)
    {
        return 0;
    }
    if (ph->ImageType == TIM2_IDTEX4)
    {
        n = (clut << 4) + no;
    }
    else if (ph->ImageType == TIM2_IDTEX8)
    {
        n = (clut << 8) + no;
    }
    else
    {
        return 0;
    }
    if ((int)(u_int)(u_short)ph->ClutColors < n)
    {
        return 0;
    }

    type = (u_short)((ph->ClutType << 8) | ph->ImageType);
    switch (type)
    {
    case 0x105:
    case 0x205:
    case 0x305:
    case 0x4104:
    case 0x4204:
    case 0x4304:
        if ((n & 0x1f) >= 8)
        {
            if ((n & 0x1f) < 0x10)
            {
                n = n + 8;
            }
            else if ((n & 0x1f) < 0x18)
            {
                n = n - 8;
            }
        }
        break;
    default:
        break;
    }

    g = (u_char)(newcolor >> 8);
    b = (u_char)(newcolor >> 0x10);
    switch (ph->ClutType & 0x3f)
    {
    case TIM2_RGB16:
        pClut16 = (u_short *)(pClut + n * 2);
        old16 = *pClut16;
        *(u_char *)pClut16 =
            ((u_char)(newcolor >> 3) & 0x1f) | (u_char)((newcolor >> 6) & 0x3e0);
        *((u_char *)pClut16 + 1) =
            (u_char)((((u_int)((int)newcolor < 0) << 0xf) >> 8)) |
            (u_char)(((newcolor >> 6) & 0x3e0) >> 8) |
            ((u_char)((newcolor >> 9) >> 8) & 0x7c);
        r = (u_char)((old16 & 0x1f) << 3);
        g = (u_char)((old16 >> 2) & 0xf8);
        b = (u_char)((old16 >> 7) & 0xf8);
        a = (u_char)((old16 >> 8) & 0x80);
        break;
    case TIM2_RGB24:
        pClut = pClut + n * 3;
        r = pClut[0];
        g = pClut[1];
        b = pClut[2];
        a = 0x80;
        pClut[0] = (u_char)newcolor;
        pClut[1] = (u_char)(newcolor >> 8);
        pClut[2] = (u_char)(newcolor >> 0x10);
        break;
    case TIM2_RGB32:
        pClut = pClut + n * 4;
        r = pClut[0];
        g = pClut[1];
        b = pClut[2];
        a = pClut[3];
        pClut[0] = (u_char)newcolor;
        pClut[1] = (u_char)(newcolor >> 8);
        pClut[2] = (u_char)(newcolor >> 0x10);
        pClut[3] = (u_char)(newcolor >> 0x18);
        break;
    default:
        r = 0;
        g = 0;
        b = 0;
        a = 0;
        break;
    }
    return ((u_int)a << 0x18) | (u_int)r | ((u_int)b << 8) | ((u_int)g << 0x10);
}

// ──────────────────────────────────────────────────────────────────────
// Read the raw texel (palette index for indexed formats, packed colour for
// RGB) at (x,y) of mip level `mipmap`.

u_int Tim2GetTexel(TIM2_PICTUREHEADER *ph, int mipmap, int x, int y)
{
    u_char *pImage;
    int     t;
    int     w;
    int     h;

    pImage = (u_char *)Tim2GetImage(ph, mipmap);
    if (pImage == (u_char *)0)
    {
        return 0;
    }
    Tim2GetMipMapPictureSize(ph, mipmap, &w, &h);
    if ((x > w) || (y > h))
    {
        return 0;
    }
    t = y * w + x;
    switch (ph->ImageType)
    {
    case TIM2_RGB16:
        return (u_int)*(u_short *)(pImage + t * 2);
    case TIM2_RGB24:
        pImage = pImage + t * 3;
        return ((u_int)pImage[2] << 0x10) | ((u_int)pImage[1] << 8) | (u_int)pImage[0];
    case TIM2_RGB32:
        pImage = pImage + t * 4;
        return ((u_int)pImage[3] << 0x18) | (u_int)pImage[0] |
               ((u_int)pImage[1] << 8) | ((u_int)pImage[2] << 0x10);
    case TIM2_IDTEX4:
        if ((x & 1) == 0)
        {
            return (u_int)(pImage[t / 2] & 0xf);
        }
        else
        {
            return (u_int)(pImage[t / 2] >> 4);
        }
    case TIM2_IDTEX8:
        return (u_int)pImage[t];
    default:
        break;
    }
    return 0;
}

// ──────────────────────────────────────────────────────────────────────
// Overwrite the texel at (x,y) of mip level `mipmap`; returns the old texel.

u_int Tim2SetTexel(TIM2_PICTUREHEADER *ph, int mipmap, int x, int y, u_int newtexel)
{
    u_char *pImage;
    int     t;
    int     w;
    int     h;
    u_int   oldtexel;
    u_short old16;
    u_char  b0;
    u_char  b1;

    pImage = (u_char *)Tim2GetImage(ph, mipmap);
    if (pImage == (u_char *)0)
    {
        return 0;
    }
    Tim2GetMipMapPictureSize(ph, mipmap, &w, &h);
    if ((x > w) || (y > h))
    {
        return 0;
    }
    t = y * w + x;
    switch (ph->ImageType)
    {
    case TIM2_RGB16:
        old16 = *(u_short *)(pImage + t * 2);
        *(pImage + t * 2) = (u_char)newtexel;
        *(pImage + t * 2 + 1) = (u_char)(newtexel >> 8);
        oldtexel = (u_int)old16;
        break;
    case TIM2_RGB24:
        pImage = pImage + t * 3;
        b1 = pImage[1];
        b0 = pImage[0];
        pImage[0] = (u_char)newtexel;
        pImage[1] = (u_char)(newtexel >> 8);
        oldtexel = ((u_int)pImage[2] << 0x10) | ((u_int)b1 << 8) | (u_int)b0;
        pImage[2] = (u_char)(newtexel >> 0x10);
        break;
    case TIM2_RGB32:
        pImage = pImage + t * 4;
        b1 = pImage[1];
        b0 = pImage[0];
        pImage[0] = (u_char)newtexel;
        pImage[1] = (u_char)(newtexel >> 8);
        oldtexel = ((u_int)pImage[3] << 0x18) | (u_int)b0 |
                   ((u_int)b1 << 8) | ((u_int)pImage[2] << 0x10);
        pImage[2] = (u_char)(newtexel >> 0x10);
        pImage[3] = (u_char)(newtexel >> 0x18);
        break;
    case TIM2_IDTEX4:
        if ((x & 1) == 0)
        {
            pImage = pImage + t / 2;
            oldtexel = *pImage & 0xf;
            *pImage = (u_char)(oldtexel << 4) | (u_char)newtexel;
        }
        else
        {
            pImage = pImage + t / 2;
            oldtexel = (u_int)(*pImage >> 4);
            *pImage = (u_char)(newtexel << 4) | (u_char)oldtexel;
        }
        break;
    case TIM2_IDTEX8:
        oldtexel = (u_int)*(pImage + t);
        *(pImage + t) = (u_char)newtexel;
        break;
    default:
        oldtexel = 0;
        break;
    }
    return oldtexel;
}

// ──────────────────────────────────────────────────────────────────────
// Read the texel at (x,y) and resolve it to a packed 0xAABBGGRR colour,
// resolving through the clut for indexed formats.

u_int Tim2GetTextureColor(TIM2_PICTUREHEADER *ph, int mipmap, int clut, int x, int y)
{
    u_int t;

    if (Tim2GetImage(ph, mipmap) == (void *)0)
    {
        return 0;
    }
    t = Tim2GetTexel(ph, mipmap, x >> (mipmap & 0x1f), y >> (mipmap & 0x1f));
    switch (ph->ImageType)
    {
    case TIM2_RGB16:
        return ((t >> 8 & 0x80) << 0x18) | ((t & 0x1f) << 3) |
               ((t & 0x7c00) << 1) | ((t >> 2 & 0xf8) << 0x10);
    case TIM2_RGB24:
        return (t & 0xffffff) | 0x80000000;
    case TIM2_RGB32:
        return t;
    case TIM2_IDTEX4:
    case TIM2_IDTEX8:
        return Tim2GetClutColor(ph, clut, (int)t);
    default:
        break;
    }
    return 0;
}

// ──────────────────────────────────────────────────────────────────────
// Upload an entire picture (image + clut) to VRAM at tbp/cbp.

u_int Tim2LoadPicture2(TIM2_PICTUREHEADER *ph, u_int tbp, u_int cbp, u_int offset)
{
    u_int ret;

    ret = Tim2LoadImage2(ph, tbp, offset);
    Tim2LoadClut2(ph, cbp, offset);
    return ret;
}

u_int Tim2LoadPicture(TIM2_PICTUREHEADER *ph, u_int tbp, u_int cbp)
{
    return Tim2LoadPicture2(ph, tbp, cbp, 0);
}

// ──────────────────────────────────────────────────────────────────────
// Upload the image (all mip levels) to VRAM.  When tbp == -1 the textures are
// loaded to the addresses already stored in TEX0 / MIPTBP (plus `offset`);
// otherwise they are packed contiguously from tbp and the registers are
// rewritten.  Returns the next free VRAM word address (or -1 in the in-place
// case).

u_int Tim2LoadImage2(TIM2_PICTUREHEADER *ph, u_int tbp, u_int offset)
{
    int                i;
    int                psm;
    u_long128         *pImage;
    int                w;
    int                h;
    int                tbw;
    TIM2_MIPMAPHEADER *pm;
    int                miptbp;
    u_long             reg;
    int                shift;

    if (ph->MipMapTextures == 0)
    {
        return (u_int)tbp;
    }

    w   = (int)(u_int)(u_short)ph->ImageWidth;
    h   = (int)(u_int)(u_short)ph->ImageHeight;
    psm = ((sceGsTex0 *)&ph->GsTex0)->PSM;
    pImage = (u_long128 *)((uintptr_t)ph + (u_int)(u_short)ph->HeaderSize);

    if (tbp == 0xffffffff)
    {
        tbp = ((sceGsTex0 *)&ph->GsTex0)->TBP0 + offset;
        if (ph->ImageSize != 0)
        {
            Tim2LoadTexture(psm, tbp, ((sceGsTex0 *)&ph->GsTex0)->TBW, w, h, pImage);
        }
    }
    else
    {
        tbw = Tim2CalcBufWidth(psm, w);
        ph->GsTex0 = (ph->GsTex0 & 0xfffffffffff00000) | ((long)(int)(tbp + offset) & 0x3fff) | (((long)tbw & 0x3f) << 0xe);
        Tim2LoadTexture(psm, tbp, tbw, w, h, pImage);
        tbp = tbp + Tim2CalcBufSize(psm, w, h);
    }

    if (ph->MipMapTextures > 1)
    {
        pm = (TIM2_MIPMAPHEADER *)(ph + 1);
        if (tbp != 0xffffffff)
        {
            pm->GsMiptbp1 = 0;
            pm->GsMiptbp2 = 0;
        }
        pImage = (u_long128 *)((uintptr_t)ph + (u_int)(u_short)ph->HeaderSize);
        for (i = 1; i < (int)(u_int)ph->MipMapTextures; i++)
        {
            pImage = (u_long128 *)((uintptr_t)pImage + (int)pm->MMImageSize[i - 1]);
            w = w / 2;
            h = h / 2;
            if (tbp == 0xffffffff)
            {
                if (i < 4)
                {
                    reg    = pm->GsMiptbp1;
                    shift  = i * 0x14;
                    miptbp = (int)((u_int)(reg >> (shift - 0x14)) & 0x3fff);
                    tbw    = (int)((u_int)(reg >> (shift - 6)) & 0x3f);
                }
                else
                {
                    reg    = pm->GsMiptbp2;
                    shift  = i * 0x14;
                    miptbp = (int)((u_int)(reg >> (shift - 0x50)) & 0x3fff);
                    tbw    = (int)((u_int)(reg >> (shift - 0x42)) & 0x3f);
                }
                Tim2LoadTexture(psm, miptbp, tbw, w, h, pImage);
            }
            else
            {
                tbw   = Tim2CalcBufWidth(psm, w);
                shift = i * 0x14;
                if (i < 4)
                {
                    pm->GsMiptbp1 = pm->GsMiptbp1 |
                                    (((u_long)tbp & 0xffffffff) << (shift - 0x14)) |
                                    (((long)tbw) << (shift - 6));
                }
                else
                {
                    pm->GsMiptbp2 = pm->GsMiptbp2 |
                                    (((u_long)tbp & 0xffffffff) << (shift - 0x50)) |
                                    (((long)tbw) << (shift - 0x42));
                }
                Tim2LoadTexture(psm, tbp, tbw, w, h, pImage);
                tbp = tbp + Tim2CalcBufSize(psm, w, h);
            }
        }
    }
    return (u_int)tbp;
}

u_int Tim2LoadImage(TIM2_PICTUREHEADER *ph, u_int tbp)
{
    return Tim2LoadImage2(ph, tbp, 0);
}

// ──────────────────────────────────────────────────────────────────────
// Upload the clut to VRAM at cbp.  The CSM1 cluts are stored as 16-wide blocks
// and DMA'd up one 8x2 / 8x3 / 8x4 group at a time.

u_int Tim2LoadClut2(TIM2_PICTUREHEADER *ph, u_int cbp, u_int offset)
{
    int          i;
    sceGsLoadImage li;
    u_long128   *pClut;
    int          cpsm;
    u_long       tex0;
    u_short      type;

    if (ph->ClutType == 0)
    {
        return 1;
    }

    cpsm = 2;
    if ((ph->ClutType & 0x3f) != 1)
    {
        cpsm = (int)((ph->ClutType & 0x3f) == 2);
    }

    tex0 = ph->GsTex0;
    ph->GsTex0 = (tex0 & 0x7ffffffffffff) | ((u_long)cpsm << 0x33) | 0x2000000000000000;
    if (cbp == 0xffffffff)
    {
        cbp = ((sceGsTex0 *)&ph->GsTex0)->CBP + offset;
    }
    else
    {
        ph->GsTex0 = (tex0 & 0x1fffffffff) | ((u_long)cpsm << 0x33) | 0x2000000000000000 |
                     (((long)(int)(cbp + offset) & 0x3fff) << 0x25);
    }

    pClut = (u_long128 *)((uintptr_t)ph + ph->ImageSize + (u_int)(u_short)ph->HeaderSize);

    type = (u_short)((ph->ClutType << 8) | ph->ImageType);
    switch (type)
    {
    case 0x104:
    case 0x105:
    case 0x204:
    case 0x205:
    case 0x305:
    case 0x4104:
    case 0x4204:
    case 0x4304:
        /* 256-colour CSM1 / linear cluts: single full-width upload. */
        Tim2LoadTexture(cpsm, cbp, 1, 0x10, (u_short)ph->ClutColors >> 4, pClut);
        return 1;
    case 0x304:
    case 0x8104:
    case 0x8105:
    case 0x8204:
    case 0x8205:
    case 0x8304:
    case 0x8305:
        /* fall through to the per-group loop below */
        break;
    default:
        printf("Illegal clut and texture combination. ($%02X,$%02X)\n", ph->ClutType);
        return 0;
    }

    for (i = 0; i < (int)(u_int)((u_short)ph->ClutColors >> 4); i++)
    {
        sceGsSetDefLoadImage(&li, (short)cbp, 1, (short)cpsm,
                             (i & 1) << 3, ((i >> 1) << 0x11) >> 0x10, 8, 2);
        FlushCache(0);
        sceGsExecLoadImage(&li, (u_long128 *)pClut);
        sceGsSyncPath(0, 0);
        if ((ph->ClutType & 0x3f) == 1)
        {
            pClut = (u_long128 *)((u_short *)pClut + 2);
        }
        else if ((ph->ClutType & 0x3f) == 2)
        {
            pClut = (u_long128 *)((u_short *)pClut + 3);
        }
        else
        {
            pClut = (u_long128 *)((u_short *)pClut + 4);
        }
    }
    return 1;
}

u_int Tim2LoadClut(TIM2_PICTUREHEADER *ph, u_int cbp)
{
    return Tim2LoadClut2(ph, cbp, 0);
}

// ──────────────────────────────────────────────────────────────────────
// DMA `w`x`h` of image data up to GS local memory at tbp/tbw/psm, split into
// horizontal strips small enough to fit the 0x7ffc0-byte transfer window.

void Tim2LoadTexture(int psm, u_int tbp, int tbw, int w, int h, u_long128 *pImage)
{
    sceGsLoadImage li;
    int            i;
    int            l;
    int            n;
    u_long128     *p;
    int            bpl;

    switch (psm)
    {
    case 0:
    case 0x30:
        ResetPIXELAlpha((u_char *)pImage, w * h);
        bpl = w << 2;
        break;
    case 1:
    case 0x31:
        bpl = w * 3;
        break;
    case 2:
    case 10:
    case 0x32:
    case 0x3a:
        bpl = w << 1;
        break;
    case 0x13:
    case 0x1b:
        bpl = w;
        break;
    case 0x14:
    case 0x24:
    case 0x2c:
        bpl = w / 2;
        break;
    default:
        return;
    }

    if (bpl == 0)
    {
        /* division-by-zero trap mirrors the build (assert on empty buffer). */
        *(volatile int *)0 = 0;
    }

    n = 0x7ffc0 / bpl;
    p = pImage;
    for (i = 0; i < h; i += l)
    {
        l = n;
        if (h < n)
        {
            l = h - i;
        }
        sceGsSetDefLoadImage(&li, tbp, (short)tbw, (short)psm, 0,
                             (short)i, (short)w, (short)l);
        FlushCache(0);
        sceGsExecLoadImage(&li, (u_long128 *)((uintptr_t)pImage + bpl * i));
        sceGsSyncPath(0, 0);
        n = i + l + l;
    }
}

// ──────────────────────────────────────────────────────────────────────
// GS buffer-width (in 64-pixel units) for a PSM/width, via a per-PSM table.

int Tim2CalcBufWidth(int psm, int w)
{
    int bw;

    bw = (w + 0x3f) >> 6;
    switch (psm)
    {
    case 0:
    case 1:
    case 2:
    case 10:
    case 0x1b:
    case 0x24:
    case 0x2c:
    case 0x30:
    case 0x31:
    case 0x32:
    case 0x3a:
        return bw;
    case 0x13:
    case 0x14:
        return bw + (bw & 1);
    default:
        return 0;
    }
}

// ──────────────────────────────────────────────────────────────────────
// VRAM word footprint of a w*h block (in 64-word pages).

static int Tim2CalcBufSize(int psm, int w, int h)
{
    int n;

    n = w * h;
    if (n < 0)
    {
        n = n + 0x3f;
    }
    return n >> 6;
}

// ──────────────────────────────────────────────────────────────────────
// Build the TEX0 register for sprite-file texture `no` at `addr`, applying the
// VRAM `offset`.

u_long GetTex0RegPK(uintptr_t addr, int no, u_int offset)
{
    unsigned char *base;
    int           *offtop;

    base   = Tim2GetHostAddress(addr);
    offtop = (int *)(base + 0x10);
    return GetTex0RegTM((uintptr_t)(base + offtop[no]), offset);
}

// ──────────────────────────────────────────────────────────────────────
// Build the TEX0 register for the lone picture of TIM2 file at `addr`, biasing
// the TBP0 / CBP fields by `offset`.

u_long GetTex0RegTM(uintptr_t addr, u_int offset)
{
    u_long              ret;
    sceGsTex0           tex0;
    TIM2_PICTUREHEADER *ph;
    unsigned char      *base;

    base = Tim2GetHostAddress(addr);
    ph  = Tim2GetPictureHeader((void *)base, 0);
    ret = 0;
    if (ph != (TIM2_PICTUREHEADER *)0)
    {
        // bias the TBP0 / CBP fields by offset, leaving every other field intact.
        tex0 = *(sceGsTex0 *)&ph->GsTex0;
        tex0.TBP0 = tex0.TBP0 + offset;
        tex0.CBP = tex0.CBP + offset;
        ret = *(u_long *)&tex0;
    }
    return ret;
}

// ──────────────────────────────────────────────────────────────────────
// Emit a GIF packet that programs BITBLTBUF/TRXPOS/TRXREG for the image of a
// TIM2 picture and queues the image transfer through the PK2D ring.

void MakeTim2Direct(u_int *tim2_addr, int tbp, int offset)
{
    TIM2_PICTUREHEADER *tph;
    u_int              *img_addr;
    u_int               psm;
    u_int               tbp0;
    u_int               tbw;
    u_int               nloop;
    sceGsTex0           sgtx0;
    Q_WORDDATA         *pbuf;

    img_addr = tim2_addr + 4;

    if (*((char *)tim2_addr + 5) != 0)
    {
        img_addr = (u_int *)0;
        if (*((char *)tim2_addr + 5) == 1)
        {
            img_addr = tim2_addr + 0x20;
        }
    }

    if (img_addr == (u_int *)0)
    {
        PRINT_ASSERT("MakeTim2Direct Illegal");
        return;
    }

    tph   = (TIM2_PICTUREHEADER *)img_addr;
    nloop = tph->ImageSize;

    if (nloop == 0)
    {
        printf("MakeTim2Direct Size Is 0\n");
        return;
    }

    sgtx0 = *(sceGsTex0 *)&tph->GsTex0;
    tbp0  = tbp;
    if (tbp < 0)
    {
        tbp0 = sgtx0.TBP0 + offset;
    }

    // A+D GIFtag: BITBLTBUF, TRXPOS, TRXREG, TRXDIR for the image upload.
    Tim2HostUpload((int)tbp0, (int)sgtx0.TBW, (int)sgtx0.PSM,
                   (int)(u_short)tph->ImageWidth,
                   (int)(u_short)tph->ImageHeight,
                   (void *)((uintptr_t)img_addr + (u_int)(u_short)tph->HeaderSize));

    pbuf = GetPK2Dbuf();
    pbuf[0].ul64[0] = SCE_GIF_SET_TAG(5, 1, 0, 0, SCE_GIF_PACKED, 1);
    pbuf[0].ul64[1] = SCE_GIF_PACKED_AD;
    pbuf[1].ul64[1] = SCE_GS_TEXFLUSH;
    pbuf[1].ul64[0] = 0;
    pbuf[2].ul64[0] = SCE_GS_SET_BITBLTBUF(0, 0, 0, tbp0, sgtx0.TBW, sgtx0.PSM);
    pbuf[2].ul64[1] = SCE_GS_BITBLTBUF;
    pbuf[3].ul64[1] = SCE_GS_TRXPOS;
    pbuf[3].ul64[0] = 0;
    pbuf[4].ul64[1] = SCE_GS_TRXREG;
    pbuf[4].ul64[0] = SCE_GS_SET_TRXREG((u_short)tph->ImageHeight, (u_short)tph->ImageWidth);
    pbuf[5].ul64[1] = SCE_GS_TRXDIR;
    pbuf[5].ul64[0] = SCE_GS_SET_TRXDIR(0);
    EndPK2Dbuf(&pbuf[6]);
    SetPK2DImageTrans((uintptr_t)img_addr + (u_int)(u_short)tph->HeaderSize, nloop >> 4);
}

// ──────────────────────────────────────────────────────────────────────
// Emit the clut-transfer GIF packet for a TIM2 picture's clut.

void MakeClutDirect(u_int *tim2_addr, int cbp, int offset)
{
    TIM2_PICTUREHEADER *tph;
    u_int              *img_addr;
    u_int               nloop;
    u_int               cbp0;
    sceGsTex0           sgtx0;
    Q_WORDDATA         *pbuf;
    int                 n;

    img_addr = tim2_addr + 4;
    if (*((char *)tim2_addr + 5) != 0)
    {
        img_addr = (u_int *)0;
        if (*((char *)tim2_addr + 5) == 1)
        {
            img_addr = tim2_addr + 0x20;
        }
    }
    if (img_addr == (u_int *)0)
    {
        PRINT_ASSERT("MakeClutDirect Illegal");
        return;
    }

    tph   = (TIM2_PICTUREHEADER *)img_addr;
    nloop = tph->ClutSize;
    if (nloop == 0)
    {
        return;
    }

    sgtx0 = *(sceGsTex0 *)&tph->GsTex0;
    cbp0  = cbp;
    if (cbp < 0)
    {
        cbp0 = sgtx0.CBP + offset;
    }

    // A+D GIFtag: BITBLTBUF, TRXPOS, TRXREG, TRXDIR for the clut upload.
    Tim2HostUpload((int)cbp0, 1, 0,
                   tph->ClutColors == 0x10 ? 8 : 0x10,
                   tph->ClutColors == 0x10 ? 2 : (int)(tph->ClutColors >> 4),
                   (void *)((uintptr_t)img_addr + (u_short)tph->HeaderSize + tph->ImageSize));

    pbuf = GetPK2Dbuf();
    pbuf[0].ul64[0] = SCE_GIF_SET_TAG(5, 1, 0, 0, SCE_GIF_PACKED, 1);
    pbuf[0].ul64[1] = SCE_GIF_PACKED_AD;
    pbuf[1].ul64[1] = SCE_GS_TEXFLUSH;
    pbuf[1].ul64[0] = 0;
    pbuf[2].ul64[1] = SCE_GS_BITBLTBUF;
    // BITBLTBUF: DBP from cbp0, DBW=1.
    pbuf[2].ul64[0] = SCE_GS_SET_BITBLTBUF(0, 0, 0, cbp0, 1, 0);
    pbuf[3].ul64[1] = SCE_GS_TRXPOS;
    pbuf[3].ul64[0] = 0;
    if (tph->ClutColors == 0x10)
    {
        pbuf[4].ul64[0] = SCE_GS_SET_TRXREG(8, 2);
    }
    else
    {
        pbuf[4].ul64[0] = SCE_GS_SET_TRXREG(0x10, tph->ClutColors >> 4);
    }
    pbuf[4].ul64[1] = SCE_GS_TRXREG;
    pbuf[5].ul64[1] = SCE_GS_TRXDIR;
    pbuf[5].ul64[0] = SCE_GS_SET_TRXDIR(0);
    EndPK2Dbuf(pbuf + 6);
    SetPK2DImageTrans((uintptr_t)img_addr + (u_short)tph->HeaderSize + tph->ImageSize, nloop >> 4);
}

// ──────────────────────────────────────────────────────────────────────
// Clut-transfer packet for a "mono" character glyph: the clut comes from
// `tim2_addr`'s picture but the image source is taken from `mono_addr`.

void MakeClutDirect_ChrMono(u_int *tim2_addr, int cbp, int offset, u_int *mono_addr)
{
    TIM2_PICTUREHEADER *tph;
    TIM2_PICTUREHEADER *tph_mono;
    u_int              *img_addr;
    u_int               nloop;
    u_int               cbp0;
    sceGsTex0           sgtx0;
    Q_WORDDATA         *pbuf;
    int                 n;
    u_int              *clut_addr;

    clut_addr = tim2_addr + 4;
    if (*((char *)tim2_addr + 5) != 0)
    {
        clut_addr = (u_int *)0;
        if (*((char *)tim2_addr + 5) == 1)
        {
            clut_addr = tim2_addr + 0x20;
        }
    }
    img_addr = mono_addr + 4;
    if (*((char *)mono_addr + 5) != 0)
    {
        img_addr = (u_int *)0;
        if (*((char *)mono_addr + 5) == 1)
        {
            img_addr = mono_addr + 0x20;
        }
    }

    tph      = (TIM2_PICTUREHEADER *)clut_addr;
    tph_mono = (TIM2_PICTUREHEADER *)img_addr;
    nloop    = tph_mono->ClutSize;
    if (nloop == 0)
    {
        return;
    }

    sgtx0 = *(sceGsTex0 *)&tph->GsTex0;
    cbp0  = cbp;
    if (cbp < 0)
    {
        cbp0 = sgtx0.CBP + offset;
    }

    // A+D GIFtag: BITBLTBUF, TRXPOS, TRXREG, TRXDIR for the clut upload.
    Tim2HostUpload((int)cbp0, 1, 0,
                   tph->ClutColors == 0x10 ? 8 : 0x10,
                   tph->ClutColors == 0x10 ? 2 : (int)(tph->ClutColors >> 4),
                   (void *)((uintptr_t)img_addr + (u_short)tph_mono->HeaderSize + tph_mono->ImageSize));

    pbuf = GetPK2Dbuf();
    pbuf[0].ul64[0] = SCE_GIF_SET_TAG(5, 1, 0, 0, SCE_GIF_PACKED, 1);
    pbuf[0].ul64[1] = SCE_GIF_PACKED_AD;
    pbuf[1].ul64[1] = SCE_GS_TEXFLUSH;
    pbuf[1].ul64[0] = 0;
    pbuf[2].ul64[1] = SCE_GS_BITBLTBUF;
    pbuf[2].ul64[0] = SCE_GS_SET_BITBLTBUF(0, 0, 0, cbp0, 1, 0);
    pbuf[3].ul64[1] = SCE_GS_TRXPOS;
    pbuf[3].ul64[0] = 0;
    if (tph->ClutColors == 0x10)
    {
        pbuf[4].ul64[0] = SCE_GS_SET_TRXREG(8, 2);
    }
    else
    {
        pbuf[4].ul64[0] = SCE_GS_SET_TRXREG(0x10, tph->ClutColors >> 4);
    }
    pbuf[4].ul64[1] = SCE_GS_TRXREG;
    pbuf[5].ul64[1] = SCE_GS_TRXDIR;
    pbuf[5].ul64[0] = SCE_GS_SET_TRXDIR(0);
    EndPK2Dbuf(pbuf + 6);
    SetPK2DImageTrans((uintptr_t)img_addr + (u_short)tph_mono->HeaderSize + tph_mono->ImageSize, nloop >> 4);
}

// ──────────────────────────────────────────────────────────────────────
// Emit image + clut packets for every texture in a sprite file.  The sprite
// file begins with a texture count, followed by a per-texture offset table at
// +0x10.

void PK2SendVram(uintptr_t tm2_addr, int tbp, int cbp, int offset)
{
    int            texnum;
    int           *offtop;
    unsigned char *base;

    base   = Tim2GetHostAddress(tm2_addr);
    texnum = *(int *)base;
    offtop = (int *)(base + 0x10);
    for (int i = 0; i < texnum; i++)
    {
        MakeTim2Direct((u_int *)(base + *offtop), tbp, offset);
        MakeClutDirect((u_int *)(base + *offtop), cbp, offset);
        offtop++;
    }
}

// ──────────────────────────────────────────────────────────────────────
// Emit image + clut packets for a single texture `num` of a sprite file.

void PK2SendVramOne(uintptr_t tm2_addr, int num, int tbp, int cbp, int offset)
{
    int            texnum;
    int           *offtop;
    unsigned char *base;

    base   = Tim2GetHostAddress(tm2_addr);
    texnum = *(int *)base;
    if ((num >= 0) && (num < texnum))
    {
        offtop = (int *)(base + num * 4 + 0x10);
        MakeTim2Direct((u_int *)(base + *offtop), tbp, offset);
        MakeClutDirect((u_int *)(base + *offtop), cbp, offset);
    }
}

// ──────────────────────────────────────────────────────────────────────
// Image + clut transfer to the addresses baked into the TIM2's TEX0 (offset
// only).

void MakeTim2SendPacket(uintptr_t tm2_addr, int offset)
{
    u_int *base;

    base = (u_int *)Tim2GetHostAddress(tm2_addr);
    MakeTim2Direct(base, -1, offset);
    MakeClutDirect(base, -1, offset);
}

void MakeTim2SendPacket_3Dpkt(uintptr_t tm2_addr, int offset)
{
    u_int *base;

    base = (u_int *)Tim2GetHostAddress(tm2_addr);
    MakeTim2Direct(base, -1, offset);
    MakeClutDirect(base, -1, offset);
}

void MakeTim2SendPacket_Mono(uintptr_t tm2_addr, int offset, uintptr_t mono_addr)
{
    u_int *base;
    u_int *mono_base;

    base      = (u_int *)Tim2GetHostAddress(tm2_addr);
    mono_base = (u_int *)Tim2GetHostAddress(mono_addr);
    MakeTim2Direct(base, -1, offset);
    MakeClutDirect_ChrMono(base, -1, offset, mono_base);
}

// ──────────────────────────────────────────────────────────────────────
// Send a whole sprite file to its baked-in VRAM addresses.

void SetSprFile(uintptr_t addr)
{
    SetSprFile2(addr, 0);
}

void SetSprFile2(uintptr_t addr, u_int offset)
{
    PK2SendVram(addr, -1, -1, offset);
}
