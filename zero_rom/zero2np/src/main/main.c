// FILE: /home/zero_rom/zero2np/src/main/main.c
//
// Program entry point and the top two GPhase layers.
//
//   * main()        - bring up libc (__main), init the GPhase system,
//                     then run GPhaseSysMain() once per frame forever.
//   * "super" layer - the root phase (GID_SUPER): one-time engine boot in
//                     init_super(), per-frame draw-env / pad servicing in
//                     pre_super(), per-frame back-end work + soft-reset poll
//                     in after_super().
//   * "Boot_Init"   - GID_BOOT_INIT: kick off the language / subtitle file
//                     loads, advance to GID_BOOT_PADCHECK once they finish.
//   * "SoftResetMain" - GID_SOFTRESETMAIN: a short timer, then drop back to
//                     GID_UBI_MODE and release the in-progress save.
//
// Also holds the soft-reset detector (L1+L2+R1+R2+SELECT+START held for one
// second) and its lock counter, plus DebugMemoryCheck() which dumps the fixed
// memory map and asserts the per-file load budgets are not exceeded.
//
// Reconstructed from the Feb 6 2004 prototype (SLES_523.84).

#include "main.h"                   // entry/phase API + soft-reset + GetSubTitleAddr
#include "gphase.h"                 // GPHASE_ENUM / GPHASE_ID_ENUM + SetNextGPhase
#include "main_decls.h"             // boot/game entry points not yet in owner headers
#include "../common/variable.h"     // sys_wrk / opt_wrk / key_now
#include "../graphics/dmaVif1.h"    // dmaVif1* packet ring
#include "../graphics/draw_env.h"   // draw-env register accessors
#include "../graphics/graph2d/graph2d.h" // InitGraph2dBoot
#include "../graphics/graph2d/message.h" // DebugMsgDataCheck
#include "../graphics/graph2d/tim2.h"    // PK2SendVram
#include "../graphics/graph3d/g3ddbg.h"  // g3ddbgAssert
#include "../graphics/graph3d/gra3d.h"   // gra3dInit
#include "../ingame/loading/loading.h"    // LoadingInit
#include "../miopan/io/miopan_log.h"     // MioPan_LogInit
#include "../miopan/rendering/miopan_renderer.h"
#include "../outgame/logo.h"        // InitLogo
#include "../outgame/option.h"      // InitOptionSetup
#include "../outgame/title.h"       // TitleInit
#include "../system/playpss/playpss.h" // playPssInit
#include "../system/eeiop/ee_iop.h" // ee_iopMain
#include "../system/eeiop/fileload.h" // FileLoadCancelAll
#include "../system/os/eecdvd.h"    // LoadReq / IsLoadEndAll
#include "../common/heapctrl.h"  // HEAP_WRK / SAFE_MALLOC
#include "../system/os/system.h"    // system API + the fixed EE memory map
                                    // (HEAP_AREA_ADDR ... RELEASE_END_ADDR)
#include "../system/eeiop/cddat.h"  // enum CD_FILE_DAT (file_no ids) + GetFileSize

// ──────────────────────────────────────────────────────────────────────
// Assertion-message idiom used throughout the non-graphics code.  Unlike the
// graph3d G3DASSERT/g3ddbgAssert path, this subsystem stamps the location with
// SetAssertPreMessage(__FILE__/__LINE__/__FUNCTION__) and then prints the
// message with PrintAssertReal().  The macro and both reporters live in the
// shared common/utility2.h; (the compiler's per-function __FUNCTION__[] arrays
// in the debug info confirm the macro expands __FUNCTION__.)
#include "../common/utility2.h"     // PRINT_ASSERT + SetAssertPreMessage / PrintAssertReal
#include <miopan/miopan_memory.h>
#include "graphics/scene/scene_effect.h"

// ──────────────────────────────────────────────────────────────────────
// Statics / file-scope globals

static int  softreset_step_timer;       // sdata 3f1988 : SoftResetMain frame counter
static int *SubTitleAddr;                // sbss  3f4d78 : malloc'd subtitle file buffer
static int  soft_reset_disable;          // sbss  3f4d7c : >0 while soft reset is locked out

static int  CheckSoftReset(void);
static void CallSoftReset(void);

// ──────────────────────────────────────────────────────────────────────
// Program entry.  __main() runs the C++ global constructors, then we never
// return: GPhaseSysMain() drives the whole game one frame per iteration.

int main(void)
{
#if !defined(__EE__)
    /* Open the log before anything can report into it, so the banner is the
     * first thing in the file even on a boot that says nothing else.
     *
     * This replaces a pair of setvbuf(_IONBF) calls on stdout and stderr.  They
     * were here for exactly the reason a log file solves -- buffered output is
     * lost when the process dies, which is when the tail matters most -- and
     * they paid for it with an unbuffered write per call at a thousand-odd
     * printf sites.  The log has its own flush policy now (every record, see
     * miopan/io/miopan_log.h), so the tail survives a hard kill without making
     * the console the slowest part of a frame. */
    MioPan_LogInit();
#endif

    InitGPhaseSys();
    for (;;)
    {
        GPhaseSysMain();
    }
}

// ──────────────────────────────────────────────────────────────────────
// Dump the (fixed) memory map and verify the largest files still fit their
// reserved regions.  Pure debug output; called once from init_super().

void DebugMemoryCheck(void)
{
    printf("\n");
    printf("\n");
    printf("\n");
    printf("<<<<<<<<<<<<<<<MEMORY CHECK>>>>>>>>>>>>>>>>\n");
    printf("DEBUG_PROGRAM_ADDR = 0x%x\n", DEBUG_PROGRAM_ADDR);
    printf("HEAP_AREA_ADDR = 0x%x\n", HEAP_AREA_ADDR);
    printf("PRE_PACKET3D_ADDR = 0x%x\n", PRE_PACKET3D_ADDR);
    printf("PACKET3D_ADDR = 0x%x\n", PACKET3D_ADDR);
    printf("PACKET2D_ADDR = 0x%x\n", PACKET2D_ADDR);
    printf("CAMERA_VCI_ADDR = 0x%x\n", CAMERA_VCI_ADDR);
    printf("MODEL_HEAP_ADDR = 0x%x\n", MODEL_HEAP_ADDR);
    printf("EVENT_DATA_ADDR = 0x%x\n", EVENT_DATA_ADDR);
    printf("MSG_DATA_ADDR = 0x%x\n", MSG_DATA_ADDR);
    printf("ROOM1_DATA_ADDR = 0x%x\n", ROOM1_DATA_ADDR);
    printf("ROOM2_DATA_ADDR = 0x%x\n", ROOM2_DATA_ADDR);
    printf("MEM_UTIL_HEAP_ADDR = 0x%x\n", MEM_UTIL_HEAP_ADDR);
    printf("TEX_2D_ADDR = 0x%x\n", TEX_2D_ADDR);
    printf("PHOTO_DATA_ADDR = 0x%x\n", PHOTO_DATA_ADDR);
    printf("EFFECT_WRK1_ADDR = 0x%x\n", EFFECT_WRK1_ADDR);
    printf("EFFECT_WRK2_ADDR = 0x%x\n", EFFECT_WRK2_ADDR);
    printf("RELEASE_END_ADDR = 0x%x\n", RELEASE_END_ADDR);
    

    if (!TEX_2D_ADDR) 
    {
        PRINT_ASSERT("Memory Over!! TEX_2D_ADDR", "");
    }

    // EventObj must fit PACKET2D..EFFECT_WRK1 (0x50000 bytes).
    if (GetFileSize(EVENT_OBJ) > 0x50000)
    {
        PRINT_ASSERT("EventObj Size Is Over", "");
    }

    if (GetFileSize(EVENT_50_OBJ) > 0x50000)
    {
        PRINT_ASSERT("Event 50 Obj Size Is Over", "");
    }

    if (GetFileSize(MSG_OBJ) > 0x3d000)
    {
        PRINT_ASSERT("MsgObj Size Is Over", "");
    }

    if (GetFileSize(VCITEST_PK2) > 0x8000)
    {
        PRINT_ASSERT("VCI Size Is Over", "");
    }

    if (0) 
    {
        PRINT_ASSERT("PHOTO_SIZE Is Over", "");
    }

    DebugMsgDataCheck();

    if (0) 
    {
        PRINT_ASSERT("RELEASE_END_ADDR[%x] OVER STACK_ADDR[%x]", 0, 0);
    }

    printf("STACK_ADDR = 0x%x\n", STACK_ADDR);

    if (0) 
    {
        PRINT_ASSERT("ROOM_MAX_NUM is Over %d", 0);
    }

    if (0) 
    {
        PRINT_ASSERT("AREA_MAX_NUM is Over %d", 0);
    }
    if (0) 
    {
        PRINT_ASSERT("M_MAP_MAX_NUM is Over %d", 0);
    }
    
    if (0) 
    {
        PRINT_ASSERT("(ENA_MAX <= 255)", "");
    }

    printf("MENU_END_ADDR = 0x%x\n\n\n", MENU_END_ADDR);
}

// ──────────────────────────────────────────────────────────────────────
// Assert hook installed via SetPrintAssert(): the lib calls this with the
// pre-formatted message string when an assertion trips.

void newAssert(char *pStr)
{
    g3ddbgAssert(false, pStr);
}

// ──────────────────────────────────────────────────────────────────────
// GID_SUPER (root phase) callbacks
// ──────────────────────────────────────────────────────────────────────

// One-time boot: stand up every engine subsystem in dependency order.
void init_super(void)
{
    InitCostume();
    soft_reset_disable = 0;
    InitSystemON();
    DebugMemoryCheck();
    InitGraph2dBoot();
    playPssInit();
    InitLogo();
    LoadingInit();
    TitleInit();
    InitOptionSetup(&opt_wrk);
    ClearFlgCtrlInit();
    MemoryCardInit();
    MemoryCardDebugReqSizeDisp();

    // PACKET3D ring buffer (uncached alias of PACKET3D_ADDR, 0x1ec30 bytes).
    dmaVif1Init((void *)0x0, 0, (void *)(PACKET3D_ADDR | 0x30000000), 0x1ec30);
    InitDrawEnv(dmaVif1GetPacketFLUSH_DIRECT,
                dmaVif1GetPacketFLUSH_DIRECT,
                dmaVif1SetPacketFLUSH_DIRECT,
                dmaVif1SetPacketFLUSH_DIRECT);

    SetPrintWarning(Zero2PrintWarningFunc);
    SetPrintAssert(newAssert);

    gra3dInit((void *)MioPan_GetHostPointer(PRE_PACKET3D_ADDR), 64000);
    IngameWrkInit(0, 1);
    sceSifAllocSysMemory(1, 0x600000, (void *)0x0);
    FinderBankSetup();
    SceneEffectInit();
}

void end_super(void)
{
}

// Per-frame, before the child layers run: reset the draw env and reload the
// scissor registers for both contexts, then service the pad.
GPHASE_ENUM pre_super(GPHASE_ENUM super)
{
    ClearDrawEnv();
    MioPan_RendererBeginFrame();
    SetScissorRegister(0, *(u_long *)&(GetDrawEnv(0)->scissor1));
    SetScissorRegister(1, *(u_long *)&(GetDrawEnv(1)->scissor1));
    GET_SCISSOR_REGISTER(0);

    PadSyncCallback();
    PadAnalogMain();

    return GPHASE_CONTINUE;
}

// Per-frame, after the child layers run: back-end work, then poll for the
// soft-reset button combo.  Returns GPHASE_END to tear the tree down when a
// reset is being requested.
GPHASE_ENUM after_super(GPHASE_ENUM result)
{
    (void)result;
    EachDebugMain();
    ee_iopMain();
    SendDMAMain();

    return (GPHASE_ENUM)(CheckSoftReset() != GPHASE_CONTINUE);
}

// ──────────────────────────────────────────────────────────────────────
// GID_BOOT_INIT callbacks
// ──────────────────────────────────────────────────────────────────────

// Kick off the language-dependent loads and the subtitle file.
void init_Boot_Init(void)
{
    HEAP_WRK     *wrk;
    unsigned int  size;

    SetLanguage(GetSystemLanguage());

    // Language font/data file ids start at VRAM_TEX_PK2 (+ language offset) ...
    LoadReq(GetLanguage() + VRAM_TEX_PK2, PACKET2D_ADDR);

    // ... and the language message files at MSG_OBJ.
    LoadReq(GetLanguage() + MSG_OBJ, MSG_DATA_ADDR);

    // Subtitle table: size it from the file system, malloc, then load.
    wrk = GetSystemHeapWrkP();
    size = GetFileSize(SUBTITLE_OBJ);
    SubTitleAddr = (int *)SAFE_MALLOC(wrk, (void *)0x0, size);
    LoadReq(SUBTITLE_OBJ, (uintptr_t)SubTitleAddr);
}

void end_Boot_Init(void)
{
}

// Wait for every queued load to finish, blit the loaded image to VRAM, then
// advance to the pad-check phase.
GPHASE_ENUM one_Boot_Init(GPHASE_ENUM dummy)
{
    if (IsLoadEndAll() != 0)
    {
        PK2SendVram(PACKET2D_ADDR, -1, -1, 0);
        SetNextGPhase(GID_BOOT_PADCHECK);
    }

    return GPHASE_CONTINUE;
}

// ──────────────────────────────────────────────────────────────────────
// Soft reset
// ──────────────────────────────────────────────────────────────────────

// Returns non-zero (and triggers the reset) once L1+L2+R1+R2+SELECT+START
// have been held continuously for 60 frames.  key_now[8..0xd] are the six
// button states.  Disabled while soft_reset_disable > 0 or sreset_ng is set.
static int CheckSoftReset(void)
{
    if (sys_wrk.sreset_ng == 0)
    {
        if ((soft_reset_disable == 0) &&
            (*key_now[8]  != 0) && (*key_now[9]  != 0) &&
            (*key_now[10] != 0) && (*key_now[0xb] != 0) &&
            (*key_now[0xc] != 0) && (*key_now[0xd] != 0))
        {
            printf("SOFT RESET COUNT DOWN = %d\n", 60 - (short)sys_wrk.sreset_count);
            sys_wrk.sreset_count++;

            if ((short)sys_wrk.sreset_count < 60)
            {
                return 0;
            }
            else
            {
                sys_wrk.sreset_count = 0;
                CallSoftReset();
                return 1;
            }
        }
        else
        {
            sys_wrk.sreset_count = 0;
        }
    }
    else
    {
        sys_wrk.sreset_count = 0;
    }

    return 0;
}

// Inhibit soft reset (nestable).
void SoftResetLock(void)
{
    sys_wrk.sreset_count = 0;
    soft_reset_disable++;
}

// Re-enable soft reset; warn (and clamp) on underflow.
void SoftResetUnlock(void)
{
    soft_reset_disable--;
    if (soft_reset_disable < 0)
    {
        PRINT_ASSERT("SoftResetUnlock Cnt under 0", "");
        soft_reset_disable = 0;
    }
}

// Queue the soft-reset phase.
static void CallSoftReset(void)
{
    SetNextGPhase(GID_SOFTRESETMAIN);
    printf("SOFT RESET!!\n");
}

int *GetSubTitleAddr(void)
{
    return SubTitleAddr;
}

// ──────────────────────────────────────────────────────────────────────
// GID_SOFTRESETMAIN callbacks
// ──────────────────────────────────────────────────────────────────────

void init_SoftResetMain(void)
{
    FileLoadCancelAll();
    softreset_step_timer = 0;
}

// Hold for a few frames so cancelled loads settle, then drop to UBI mode and
// release the save data.
GPHASE_ENUM one_SoftResetMain(GPHASE_ENUM dummy)
{
    softreset_step_timer++;
    if (softreset_step_timer > 2)
    {
        SetNextGPhase(GID_UBI_MODE);
        ResetOutReqFlg();
        MissionReleaseSaveData();
    }

    return GPHASE_CONTINUE;
}

void end_SoftResetMain(void)
{
}
