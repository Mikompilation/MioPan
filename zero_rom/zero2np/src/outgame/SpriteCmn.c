// FILE: /home/zero_rom/zero2np/src/outgame/SpriteCmn.c
//
// Common sprite / UI helpers shared across the out-game (menu, title, logo,
// album, ...) screens.  A caller registers one SPRT_DAT source table with
// SpCmnStart(); every draw call then indexes that table by "label" and copies
// the record into a scratch DISP_SPRT before shipping it to DispSprD().
//
//   * SpCmnStart          - point the module at the active SPRT_DAT table.
//   * SpCmnSetSprite      - resolve label -> DISP_SPRT, apply alpha / offset;
//                           iFlg selects the additive (0x48) alpha register.
//   * SpCmnDrawSprite     - set + draw one sprite.
//   * SpCmnDrawSpriteScale- as above, with an explicit scale about its origin.
//   * SpCmnDrawRange      - draw a contiguous run of labels [st..en].
//   * SpCmnBlackOut       - full-screen black quad at the given alpha.
//   * SpCmnPrintNumber_NK - drop-shadowed number (shadow pass in colour 9).
//   * SpCmnPrintMsg_K     - drop-shadowed message (shadow pass in colour 9).
//   * SpCmnGetCenterX     - left edge that centres a message about iCenX.
//   * SpCmnTexMemLoad     - size a file, grab a mem_util buffer, queue the load.
//   * SpCmnTexMemReleaseSub- free such a buffer (NULL-safe); returns NULL.
//   * SpCmnGetScreen      - snapshot the current back buffer into an EE buffer.
//   * SpCmnDrawScreen     - upload an EE snapshot to VRAM and draw it full-screen.
//
// The GS register payloads in SpCmnDrawScreen are rebuilt from the SCE
// <libgraph.h> SCE_GS_SET_* value builders (see sdk/libgraph.h); the black-out
// quad and the screen-blit sprite records are static data baked by the build
// and are reproduced verbatim.
//
// Reconstructed from the Feb 6 2004 prototype (SLES_523.84).

#include "SpriteCmn.h"              // this file's public API

#include "../graphics/graph2d/g2d_draw.h"   // DISP_SPRT / SPRT_DAT / DISP_SQAR / SQAR_DAT / DISP_STR
                                            // CopySprDToSpr / CopySqrDToSqr / DispSprD / DispSqrD
                                            // LocalCopyLtoB* / LocalCopyBtoL* (screen snapshot/upload)
#include "../graphics/graph2d/message.h"    // PrintNumber_N / PrintMsg / GetMsgDataAddr / GetMsgLineLength
#include "../../sdk/libgraph.h"             // SCE GS register value builders (SCE_GS_SET_*)
#include "../common/variable.h"            // sys_wrk (SYS_WRK) - the frame counter
#include "../system/eeiop/cddat.h"         // GetFileSize (+ enum CD_FILE_DAT file ids)
#include "../system/os/eecdvd.h"           // LoadReq
#include "../common/mem_util.h"            // mem_utilGetMem / mem_utilFreeMem
#include <string.h>                 // memset

// ──────────────────────────────────────────────────────────────────────
// Statics.

static SPRT_DAT *SpCmnCtlList;      // sdata 3ef1d8 : active sprite source table

// ──────────────────────────────────────────────────────────────────────
// Register the SPRT_DAT source table subsequent draws index by label.

void SpCmnStart(SPRT_DAT *pSprDat)
{
    SpCmnCtlList = pSprDat;
}

// ──────────────────────────────────────────────────────────────────────
// Resolve a sprite label into ds: copy the source record, scale its baked-in
// alpha by the caller alpha, then shift it by (off_x, off_y).  iFlg != 0
// swaps in the additive-blend alpha register (0x48).

void SpCmnSetSprite(DISP_SPRT *ds, int iLabel, int off_x, int off_y, u_char alpha, int iFlg)
{
    CopySprDToSpr(ds, SpCmnCtlList + iLabel);
    
    ds->alpha = (u_char)((int)((uint)ds->alpha * (uint)alpha) >> 7);
    ds->x = ds->x + (float)off_x;
    ds->y = ds->y + (float)off_y;

    if (iFlg != 0)
    {
        ds->alphar = 0x48;
    }
}

// ──────────────────────────────────────────────────────────────────────
// Set one sprite and draw it.

void SpCmnDrawSprite(int iLabel, int off_x, int off_y, u_char alpha, int iFlg)
{
    DISP_SPRT ds;

    SpCmnSetSprite(&ds, iLabel, off_x, off_y, alpha, iFlg);
    DispSprD(&ds);
}

// ──────────────────────────────────────────────────────────────────────
// Set one sprite and draw it scaled about its own origin.

void SpCmnDrawSpriteScale(int iLabel, int iOffX, int iOffY, float fScaleW, float fScaleH,
                          u_char ucAlpha, int iFlg)
{
    DISP_SPRT ds;

    SpCmnSetSprite(&ds, iLabel, iOffX, iOffY, ucAlpha, iFlg);
    ds.csx = ds.x;
    ds.csy = ds.y;
    ds.scw = fScaleW;
    ds.sch = fScaleH;
    DispSprD(&ds);
}

// ──────────────────────────────────────────────────────────────────────
// Draw a contiguous run of sprite labels [st..en], all with the same
// offset / alpha / flag.

void SpCmnDrawRange(int st, int en, int off_x, int off_y, u_char alpha, int iFlg)
{
    int i;

    for (i = st; i <= en; i++)
    {
        SpCmnDrawSprite(i, off_x, off_y, alpha, iFlg);
    }
}

// ──────────────────────────────────────────────────────────────────────
// Full-screen black quad (640x448) at the given alpha (scaled from 0x80).

void SpCmnBlackOut(u_char ucAlpha)
{
    DISP_SQAR dsq;
    SQAR_DAT  black_bg = { 640, 448, 0, 0, 0, 0, 0, 0, 0x80 };

    CopySqrDToSqr(&dsq, &black_bg);
    dsq.alpha = (u_char)((int)((uint)dsq.alpha * (uint)ucAlpha) >> 7);
    DispSqrD(&dsq);
}

// ──────────────────────────────────────────────────────────────────────
// Drop-shadowed number: a shadow pass in colour 9 at (x+2, y+2), then the
// real number in ucColLabel at (x, y).

void SpCmnPrintNumber_NK(int iData, int iNum, int iX, int iY, u_char ucColLabel, u_char ucAlpha,
                         int iPri, u_char ucMsgType, int ucZeroFlg)
{
    PrintNumber_N(iData, iNum, iX + 2, iY + 2, 9, ucAlpha, iPri, ucMsgType, ucZeroFlg & 0xff);
    PrintNumber_N(iData, iNum, iX, iY, ucColLabel, ucAlpha, iPri, ucMsgType, ucZeroFlg & 0xff);
}

// ──────────────────────────────────────────────────────────────────────
// Drop-shadowed message: a shadow pass in colour 9 at (x+2, y+2), then the
// real message in iColLabel at (x, y).

void SpCmnPrintMsg_K(int iMsgType, int iMsgID, int iX, int iY, int iColLabel, int iAlpha, int iPri)
{
    PrintMsg(iMsgType, iMsgID, iX + 2, iY + 2, 9, iAlpha, iPri);
    PrintMsg(iMsgType, iMsgID, iX, iY, iColLabel, iAlpha, iPri);
}

// ──────────────────────────────────────────────────────────────────────
// Return the left edge at which a message (group iGroupLabel, id iMsgLabel)
// is horizontally centred about iCenX.

int SpCmnGetCenterX(int iGroupLabel, int iMsgLabel, int iCenX)
{
    DISP_STR disp_wrk;
    int      len;

    memset(&disp_wrk, 0, sizeof(DISP_STR));
    disp_wrk.str = GetMsgDataAddr(iGroupLabel, iMsgLabel);
    len = GetMsgLineLength(disp_wrk.str, (u_char **)0);
    return iCenX - len / 2;
}

// ──────────────────────────────────────────────────────────────────────
// Size file iFileNo, allocate a mem_util buffer for it, queue the load, and
// return the buffer address.

void *SpCmnTexMemLoad(int iFileNo)
{
    unsigned int size;
    void        *pLoadAddr;

    size = GetFileSize(iFileNo);
    pLoadAddr = mem_utilGetMem(size);
    LoadReq(iFileNo, (uintptr_t)pLoadAddr);
    return pLoadAddr;
}

// ──────────────────────────────────────────────────────────────────────
// Free a texture buffer allocated by SpCmnTexMemLoad (NULL-safe).  Always
// returns NULL so the caller can clear its pointer in one assignment.

void *SpCmnTexMemReleaseSub(void *pTexAddr)
{
    if (pTexAddr != (void *)0)
    {
        mem_utilFreeMem(pTexAddr);
    }
    return (void *)0;
}

// ──────────────────────────────────────────────────────────────────────
// Snapshot the current back buffer into a freshly allocated EE buffer and
// return it.  The source VRAM page alternates each frame (double buffer),
// selected from sys_wrk.count: page offset = ((count + 1) & 1) * 0x1180.

void *SpCmnGetScreen(void)
{
    int   size;
    void *ee_adrs;

    size = LocalCopyLtoBGetSize(0);
    ee_adrs = mem_utilGetMem(size);
    LocalCopyLtoBAdrs(0, (uintptr_t)ee_adrs, (int)(((sys_wrk.count + 1) & 1) * 0x1180));
    return ee_adrs;
}

// ──────────────────────────────────────────────────────────────────────
// Upload an EE snapshot (from SpCmnGetScreen) to VRAM at iVramAddr and draw it
// full-screen.  tex0/tex1 select the uploaded page as a 1024x512 CT32 texture
// with bilinear filtering.

void SpCmnDrawScreen(void *pScrAddr, int iVramAddr)
{
    // Screen-blit sprite template (tex0 patched into ds.tex0 below at runtime):
    // full 640x448 window, pri 0xe0, alpha 0x80, blend mode 1.
    SPRT_DAT  sd = { 0, 0, 0, 640, 448, 0, 0, 224, 0x80, 0, 1 };
    DISP_SPRT ds;

    LocalCopyBtoLAdrs(0, (uintptr_t)pScrAddr, iVramAddr);

    memset(&ds, 0, sizeof(DISP_SPRT));
    CopySprDToSpr(&ds, &sd);
    ds.tex0 = SCE_GS_SET_TEX0(iVramAddr, 10, 0, 10, 9, 0, 0, iVramAddr, 0, 0, 0, 1);
    ds.tex1 = SCE_GS_SET_TEX1(1, 0, 1, 5, 0, 0, 0);
    DispSprD(&ds);
}
