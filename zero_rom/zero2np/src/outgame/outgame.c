// FILE: /home/zero_rom/zero2np/src/outgame/outgame.c
//
// "Outgame" GPhase layer: the boot / attract-mode flow.
//
//   * OutGame_Main (GID_OUTGAME_MAIN) - the parent phase.  init_ stages every
//                     outgame screen's texture/data background loads (title,
//                     load-game, setup, option, gallery, save, album, camera,
//                     and both logos), resets the model load heap, and starts
//                     the title movie subsystem; end_ frees them all again.
//                     pre_/after_ are no-ops.
//   * UBI_Mode (GID_UBI_MODE)   - play the publisher (UBI) movie, then advance
//                     to the Tecmo logo.
//   * Tecmo_Mode (GID_TECMO_MODE) / Project_Mode (GID_PROJECT_MODE) - run the
//                     Tecmo and Project Zero logos (LogoMain), advancing on
//                     completion or a Start press; soft reset is locked for
//                     the duration of each.
//   * BackGroundLoadReq - stage the loading-screen texture and, once, the
//                     one-time ingame resource loads (IngameLoadOnce).
//
// This is a C++ translation unit; the phase callbacks have C++ linkage and are
// wired into the GPhase registry table by symbol.
//
// Reconstructed from the Feb 6 2004 prototype (SLES_523.84).

#include "outgame.h"                // BackGroundLoadReq
#include "logo.h"                   // InitLogoCtrl / LogoMain / Get*LogoTexMem / *LogoTexLoadReq / Release*
#include "title.h"                  // TitleTexBackGroundLoadReq / TitleMemFree / SetTitleLoadFlg
#include "title_movie.h"            // TitleMovieInit
#include "loadgame.h"               // GetLoadGameTexMem / LoadGameDataLoadReq / ReleaseLoadGameTexMem
#include "setup.h"                  // SetupBackGroundLoadReq / SetupMemFree
#include "option.h"                 // OptionBackGroundLoadReq / OptionMemFree
#include "gallery.h"                // GalleryBackGroundLoadReq / GalleryMemFree

#include "../main/gphase.h"         // GPHASE_ENUM / GID_* + SetNextGPhase
#include "../main/main.h"           // SoftResetLock / SoftResetUnlock
#include "../system/os/system.h"    // GetLanguage + the fixed EE memory map (MODEL_HEAP_ADDR / CAMERA_VCI_ADDR)
#include "../system/os/eecdvd.h"    // LoadReq
#include "../system/eeiop/cddat.h"  // enum CD_FILE_DAT (MENU_* / VCITEST_PK2 / ... file ids)
#include "../system/eeiop/stream_auto.h"    // StreamAutoAllStop
#include "../system/eeiop/snd.h"            // SndSetEffect
#include "../system/eeiop/snd_buffer.h"     // SndBufAllStopLoopSnd
#include "../system/eeiop/fileload.h"       // FileLoadCancelAll
#include "../common/ol_load.h"      // ol_loadHeapReset / ol_loadGetHeap / ol_loadFreeHeap
#include "../graphics/movie/movie.h"        // InitMovieWithTitle / PlayMovieWithTitle / EndMovieWithTitle
#include "../ingame/loading/loading.h"      // GetLoadingTexMem / LoadingTexLoadReq / ReleaseLoadingTexMem
#include "../ingame/menu/menu_cam_main.h"   // MenuCamMainBackGroundLoadReq / MenuCamMainMemFree
#include "../ingame/photo/m_plyr_camera.h"  // CNPlyrCamera / m_plyr_camera
#include "../save_load/prg/game_data_save.h"    // GameDataSaveBackGroundLoadReq / GameDataSaveTexMemFree
#include "../album/prg/album.h"     // AlbumBackGroundLoadReq / AlbumEnd

#include "../graphics/graph3d/ctl/fixed_array.h"   // fixed_array<> template (inlined per TU)

#include <stdio.h>                  // printf

// ──────────────────────────────────────────────────────────────────────
// Per-pad button-state table (system/pad/pad.c).  paddat[0] is player 1;
// [n][0] is the "start / decide" state polled by the logo phases.

#include "../system/pad/pad.h"      // paddat pad-data table

// ──────────────────────────────────────────────────────────────────────
// Statics.

static int mFlgOnce;                // sdata 3f36d0 : ingame one-time-load guard

// ──────────────────────────────────────────────────────────────────────
// Forward declaration for the file-static one-time ingame loader.

static void IngameLoadOnce(void);

// ──────────────────────────────────────────────────────────────────────
// GID_OUTGAME_MAIN
// ──────────────────────────────────────────────────────────────────────

// Enter the outgame parent phase: silence audio, cancel any pending loads,
// reset the model load heap, then background-load every outgame screen's
// textures/data and both boot logos.
void init_OutGame_Main(void)
{
    StreamAutoAllStop();
    SndBufAllStopLoopSnd();
    SndSetEffect(0, 0x2fff, 3);
    FileLoadCancelAll();
    ReleaseLoadingTexMem();
    ol_loadHeapReset((void *)MODEL_HEAP_ADDR, 0x7a8000);
    TitleMovieInit();

    GetTecmoLogoTexMem();
    GetProjectLogoTexMem();
    TecmoLogoTexLoadReq();
    ProjectLogoTexLoadReq();

    TitleTexBackGroundLoadReq();
    GetLoadGameTexMem();
    LoadGameDataLoadReq();
    SetupBackGroundLoadReq();
    OptionBackGroundLoadReq();
    GalleryBackGroundLoadReq();
    GameDataSaveBackGroundLoadReq(ol_loadGetHeap, ol_loadFreeHeap);
    AlbumBackGroundLoadReq(ol_loadGetHeap, ol_loadFreeHeap);
    MenuCamMainBackGroundLoadReq(ol_loadGetHeap, ol_loadFreeHeap);

    SetTitleLoadFlg(1);
    BackGroundLoadReq();
}

GPHASE_ENUM pre_OutGame_Main(GPHASE_ENUM dummy)
{
    return GPHASE_CONTINUE;
}

GPHASE_ENUM after_OutGame_Main(GPHASE_ENUM result)
{
    return GPHASE_CONTINUE;
}

// Leave the outgame parent phase: free everything init_OutGame_Main staged.
void end_OutGame_Main(void)
{
    TitleMemFree();
    ReleaseLoadGameTexMem();
    SetupMemFree();
    OptionMemFree();
    GalleryMemFree();
    GameDataSaveTexMemFree();
    AlbumEnd();
    MenuCamMainMemFree();
    ReleaseTecmoLogoTexMem();
    ReleaseProjectLogoTexMem();
}

// ──────────────────────────────────────────────────────────────────────
// GID_UBI_MODE  (publisher movie)
// ──────────────────────────────────────────────────────────────────────

void init_UBI_Mode(void)
{
    StreamAutoAllStop();
    SndBufAllStopLoopSnd();
    SndSetEffect(0, 0x2fff, 3);
    FileLoadCancelAll();
    ReleaseLoadingTexMem();
    ol_loadHeapReset((void *)MODEL_HEAP_ADDR, 0x7a8000);
    SoftResetLock();
    InitMovieWithTitle(0x46, 1);
}

GPHASE_ENUM one_UBI_Mode(GPHASE_ENUM dummy)
{
    if (PlayMovieWithTitle() != 0)
    {
        SetNextGPhase(GID_TECMO_MODE);
    }

    return GPHASE_CONTINUE;
}

void end_UBI_Mode(void)
{
    EndMovieWithTitle();
    SoftResetUnlock();
    printf("UBI End\n");
}

// ──────────────────────────────────────────────────────────────────────
// GID_TECMO_MODE  (Tecmo logo)
// ──────────────────────────────────────────────────────────────────────

void init_Tecmo_Mode(void)
{
    InitLogoCtrl();
    SoftResetLock();
}

// Run the Tecmo logo (30/90/30-frame fade envelope); advance to Project Zero
// on completion or a Start press.
GPHASE_ENUM one_Tecmo_Mode(GPHASE_ENUM dummy)
{
    if (LogoMain(0, 0x1e, 0x5a, 0x1e) != 0)
    {
        SetNextGPhase(GID_PROJECT_MODE);
    }

    if (**paddat == 1)
    {
        SetNextGPhase(GID_PROJECT_MODE);
    }

    return GPHASE_CONTINUE;
}

void end_Tecmo_Mode(void)
{
    SoftResetUnlock();
    printf("tecmo_end\n");
}

// ──────────────────────────────────────────────────────────────────────
// GID_PROJECT_MODE  (Project Zero logo)
// ──────────────────────────────────────────────────────────────────────

void init_Project_Mode(void)
{
    InitLogoCtrl();
    SoftResetLock();
}

// Run the Project Zero logo; advance to the title top on completion or Start.
GPHASE_ENUM one_Project_Mode(GPHASE_ENUM dummy)
{
    if (LogoMain(1, 0x1e, 0x5a, 0x1e) != 0)
    {
        SetNextGPhase(GID_TITLE_TOP);
    }

    if (**paddat == 1)
    {
        SetNextGPhase(GID_TITLE_TOP);
    }

    return GPHASE_CONTINUE;
}

void end_Project_Mode(void)
{
    SoftResetUnlock();
}

// ──────────────────────────────────────────────────────────────────────
// Loading-screen background prep
// ──────────────────────────────────────────────────────────────────────

// Stage the loading-screen texture; on the first call, also fire the one-time
// ingame resource loads.
void BackGroundLoadReq(void)
{
    GetLoadingTexMem();
    LoadingTexLoadReq();

    if (mFlgOnce == 0)
    {
        IngameLoadOnce();
        mFlgOnce = 1;
    }
}

// One-time ingame resource loads: the language-indexed common banks, the fixed
// bank at 0x2a, the player camera setup, and the ingame program at the camera
// VCI region.  Addresses are the fixed ingame load layout.
static void IngameLoadOnce(void)
{
    LoadReq(GetLanguage() + MENU_BG_PK2, 0x19368c0);
    LoadReq(MENU_TOUROU_PK2, 0x1950ec0);
    LoadReq(GetLanguage() + MENU_PLAYDATA_PK2, 0x1973cc0);
    LoadReq(GetLanguage() + MENU_STATUS_PK2, 0x19981c0);
    m_plyr_camera.SetUp();
    LoadReq(VCITEST_PK2, CAMERA_VCI_ADDR);
}
