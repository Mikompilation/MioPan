/* ==========================================================================
 *  system/os/system.h
 *
 *  Core OS-layer header for the zero2np engine: the fixed EE main-memory map
 *  and the system bring-up / file-service API implemented in system.c.
 *
 *  This file is referenced by name (../../system/os/system.h) in the
 *  prototype's debug strings.  Only the part that could be recovered with
 *  certainty is reconstructed here: the EE memory map.  Those region base
 *  addresses are #define constants (they inline to lui/ori immediates and so
 *  leave no symbol in the debug info), but their names are preserved verbatim
 *  by DebugMemoryCheck()'s printf labels in main.c, and the same constants are
 *  consumed by system.c's InitSystemON() (heapCtrlInit @ HEAP_AREA_ADDR,
 *  mem_utilInit @ MEM_UTIL_HEAP_ADDR, ol_loadHeapInit @ MODEL_HEAP_ADDR ...),
 *  which is what places them in this shared header.
 *
 *  The remaining system.c prototypes (InitSystemON, GetSystemHeapWrkP, the
 *  file/language services, ...) also belong in this header but are left out
 *  until those translation units are reconstructed.
 *
 *  Reconstructed from the Feb 6 2004 prototype (SLES_523.84).
 * ======================================================================== */

#ifndef _SYSTEM_OS_SYSTEM_H
#define _SYSTEM_OS_SYSTEM_H

#include "../../sdk/libcdvd.h"      /* sceCdCLOCK / u_char */
#include "../../sdk/sce_gs.h"       /* sceGsDrawEnv1 */
#include "../../common/utility2.h"  /* PRINT_ASSERT (CSYSTEM_SND_BUF_PLAY::Play) */
#include "../eeiop/snd_buffer.h"    /* CSND_BUF_PLAY                            */
#include "heapctrl.h"               /* HEAP_WRK */

/* --------------------------------------------------------------------------
 *  EE main-memory map (physical / cached addresses).
 *
 *  One contiguous layout from the debug program area up to the stack.  The
 *  trailing comment on each line is the size of the region (gap to the next
 *  base); several of those sizes are exactly the per-file load budgets that
 *  DebugMemoryCheck() asserts against (e.g. EVENT_DATA..MSG_DATA == 0x50000,
 *  MSG_DATA..ROOM1_DATA == 0x3d000).
 * ------------------------------------------------------------------------ */
#define DEBUG_PROGRAM_ADDR   0x004d6c00      /* debug build program/heap base   */
#define HEAP_AREA_ADDR       0x004d6c00      /* system heap   (0xc8000)         */
#define CAMERA_VCI_ADDR      0x0059ec00      /* camera VCI    (0x8000)          */
#define MODEL_HEAP_ADDR      0x005a6c00      /* model heap    (0x7a8000)        */
#define EVENT_DATA_ADDR      0x00d4ec00      /* EventObj      (0x50000)         */
#define MSG_DATA_ADDR        0x00d9ec00      /* MsgObj        (0x3d000)         */
#define ROOM1_DATA_ADDR      0x00ddbc00      /* room 1 data   (0x410000)        */
#define ROOM2_DATA_ADDR      0x011ebc00      /* room 2 data   (0x410000)        */
#define MEM_UTIL_HEAP_ADDR   0x015fbc00      /* mem_util heap (0x2b1400)        */
#define TEX_2D_ADDR          0x018ad000      /* 2D textures   (0xfc1c0)         */
#define MENU_END_ADDR        0x019a91c0      /* end of menu region              */
#define PHOTO_DATA_ADDR      0x019a9b00      /* photo data    (0xe8000)         */
#define PRE_PACKET3D_ADDR    0x01a91b00      /* pre-built 3D packet (0xfa00)    */
#define PACKET3D_ADDR        0x01aa1500      /* 3D DMA packet ring (0x3d8600)   */
#define PACKET2D_ADDR        0x01e79b00      /* 2D DMA packet / EFFECT_WRK1     */
#define EFFECT_WRK1_ADDR     0x01e79b00      /* effect work 1 (0x8c000)         */
#define EFFECT_WRK2_ADDR     0x01f05b00      /* effect work 2 (0x8c000)         */
#define RELEASE_END_ADDR     0x01f91b00      /* end of release allocation       */
#define STACK_ADDR           0x01fa0000      /* EE stack top                    */

 /* --------------------------------------------------------------------------
  *  VIDEO MODE
  * ------------------------------------------------------------------------ */

#define VIDEO_MODE_NTSCU    2
#define VIDEO_MODE_PAL      3
#define VIDEO_MODE_MAX      4

/* --------------------------------------------------------------------------
 *  system.c public API.
 *
 *  Bring-up, the system heap, video-mode / display control, the per-frame DMA
 *  back end, the RTC, the system sound bank and the language services.  The
 *  internal helpers (InitSysWrk / InitGraphics / InitVBlank / vfunc /
 *  v_callback / _InitException) are static to system.c and are not declared.
 * ------------------------------------------------------------------------ */
struct _Q_WORDDATA;
struct _SND_3D_SET;

/* The double display/draw buffer set.  DrawDbgMenu() reaches into the two
 * clear packets to force the framebuffer clear colour. */
extern sceGsDBuff gdb;              /* data 35e3b0 */

/* system heap */
HEAP_WRK *GetSystemHeapWrkP(void);
void  SetVBlankWaitNum(int iWaitNum);
void *systemGetMem(int size);
void  systemFreeMem(void *adrs);

/* VIF1 DMA-end timer handler (DMAC handler ABI) */
int   GetVifEndTimer(int ch);

/* boot */
void  InitSystemON(void);

/* video / display */
void  ChangeVideoMode(u_char mode);
int   GetPALMode(void);
void  SystemSetVCallback(void);
void  SetFrameBuffer(void);
void  SetDisplayPos(void);
void  SetGsResetGraph(int n);
void  SetPCRTC(int frame);

/* RTC */
void  GetNowClock(sceCdCLOCK *nc);

/* per-frame DMA back end */
void  SendDMASub(struct _Q_WORDDATA *packet_buf);
void  SendDMAMain(void);
sceGsDrawEnv1 *GetDrawEnv(int id);

/* The draw env the frame currently being built is using -- &gdb.draw0 or
 * &gdb.draw1, flipped by SetPCRTC().  MakeSmallPhotoV() reads the live ZBUF
 * out of it and copies three of its register quadwords back into its own
 * packet, so it needs the pointer itself and not GetDrawEnv()'s by-index
 * form.  Declared void * because that is how system.c defines it. */
extern void *pdrawenv;                      /* sdata 3f4628 */

/* system sound bank */
void  SystemBankSetup(void);
int   SystemBankIsReady(void);
int   SystemBankPlay(int no, int effect, int loop, int fade_time,
                     struct _SND_3D_SET *s3d, int vol, int pitch);
void  SystemBankRelease(void);
int   SystemBankIsLoopSnd(int no);

/* A held handle on a system-bank sound.  The base class (the id plus the
 * Fade / PitchFade / IsPlaying / Stop verbs) is shared with the player and
 * sister pools; only Play() differs, and it lives here because it is the
 * system bank it allocates from.  The assert banner photo_dat.o emits for the
 * overlap check names this header at line 57, which is what places the body. */
struct CSYSTEM_SND_BUF_PLAY : CSND_BUF_PLAY /* 0x4 */
{
    /* Note the overlap check reports and then plays anyway, overwriting the
     * live handle -- the assert does not guard the assignment. */
    void Play(int no, int effect, int loop, int fade_time,
              struct _SND_3D_SET *s3d, int vol, int pitch)
    {
        if (play_id != CSND_BUF_PLAY_NO_ID) {                                    /* 56 */
            PRINT_ASSERT("Overlap Snd Play");                                    /* 57 */
        }

        play_id = SystemBankPlay(no, effect, loop, fade_time, s3d, vol, pitch);   /* 60 */
    }
};

/* language */
void   SetLanguage(u_char language);
u_char GetLanguage(void);
u_char GetSystemLanguage(void);

#endif /* _SYSTEM_OS_SYSTEM_H */
