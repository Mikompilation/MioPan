// FILE: /home/zero_rom/zero2np/src/outgame/title.c
//
// Title mode: the asset load/free path for the title screen's three textures,
// the title BGM stream and its sound bank, and the init/one/end callbacks for
// every phase that lives underneath GID_TITLE_MODE.
//
// The file itself owns almost no logic: `title_step` is a four-state loader
// (0 = re-request, 1 = waiting on the loads, 2 = running, 3 = ending) and the
// phase callbacks are thin brackets around the screens in title_top.o,
// title_menu.o, title_album.o, newgame.o, loadgame.o, setup.o, gallery.o,
// option.o, framerate.o and chapter_sel.o.  Everything drawn on the title
// belongs to title_disp.o / title_top.o / title_menu.o.
//
// title.o has no static data of its own -- its .rodata and .sdata hold nothing
// but the fixed_array<> assert boilerplate a header drags in.
//
// Reconstructed from the Feb 6 2004 prototype (SLES_523.84).
// Trailing /* NNN */ are ROM source line numbers, measured from
// disassemble_function.

#include "title.h"

#include "chapter_sel.h"                    // ChapterSelCtrlInit / Main / DispMain
#include "framerate.h"                      // FrameRateSelMain / DispMain
#include "gallery.h"                        // GalleryInit / Main / End
#include "loadgame.h"                       // LoadGame* / GetLoadGameTexMem / ...
#include "newgame.h"                        // NewGameCtrlInit / Main / DispMain
#include "option.h"                         // OptionInit / Main / End
#include "outgame.h"                        // BackGroundLoadReq
#include "setup.h"                          // SetupInit / Main / DispMain
#include "title_album.h"                    // TitleAlbumInit / Main / DispMain
#include "title_disp.h"                     // DispTitleBack
#include "title_menu.h"                     // TitleMenuCtrlInit / Main / DispMain
#include "title_movie.h"                    // Lock/Unlock/CheckMoveTitleMovie
#include "title_top.h"                      // TitleTopMain / TitleTopDispMain
#include "../album/prg/album.h"             // AlbumEnd / AlbumBackGroundLoadReq
#include "../common/ol_load.h"              // ol_loadGetHeap / ol_loadFreeHeap
#include "../common/variable.h"             // pad[]
#include "../ingame/menu/menu_cam_main.h"   // MenuCamMain*
#include "../ingame/menu/zero2_anim2d.h"    // Zero2Anim2D_CsrAnimCtrl
#include "../main/gphase.h"                 // SetNextGPhase / GID_*
#include "../main/phasefunc.h"              // GPHASE_ENUM + phase-callback prototypes
#include "../save_load/prg/game_data_save.h"// GameDataSave*
#include "../system/eeiop/cddat.h"          // TITLE_* / OUTGAME_PK2 / BGM*
#include "../system/eeiop/fileload.h"       // FileLoadReqEE / FileLoadIsEnd2 / FileLoadCancel2
#include "../system/eeiop/sndbank.h"        // SndBankNew / IsReady / Release / *LoadPriority
#include "../system/eeiop/stream_auto.h"    // StreamAutoPlay / FadeOut / IsPlaying
#include "../system/os/system.h"            // GetLanguage

#include <stdint.h>                         // uintptr_t

/* title_wrk.title_step */
#define TITLE_STEP_REQ      0   /* free what is resident, re-request the loads */
#define TITLE_STEP_LOAD     1   /* waiting on the file loads and the bank      */
#define TITLE_STEP_MAIN     2   /* running: input live, everything drawn       */
#define TITLE_STEP_END      3   /* ending: still drawn, input dead             */

static void *title_bg_addr;                                             /* sdata 3f4770 */
static void *title_logo_addr;                                           /* sdata 3f4774 */
static void *outgame_cmn_tex_addr;                                      /* sdata 3f4778 */
static u_char title_bg_send_lock;                                       /* sdata 3f477c */
static u_char title_move_movie_timer;                                   /* sdata 3f477d */
static TITLE_WRK title_wrk;                                             /* bss   4bc640 */
static TITLE_DISP_CTRL title_disp_ctrl;                                 /* sbss  3f4fd0 */
static char title_load_end;                                             /* sbss  3f4fd8 */

static void TitleWrkInit(void);
static int  TitleLoadWait(void);
static int  TitleMain(void);
static void TitleTexLoadCancel(void *tex_addr, int data_label);
static void TitleDispCtrlInit(void);

/* One-time reset of the three texture pointers.  outgame.c calls this before
 * the title is first entered; TitleWrkInit() does the per-entry work. */
void TitleInit(void)
{
    title_bg_addr = (void *)0;                                          /* 141 */
    title_logo_addr = (void *)0;                                        /* 142 */
    outgame_cmn_tex_addr = (void *)0;                                   /* 143 */

    title_load_end = '\0';                                              /* 145 */
    title_bg_send_lock = '\0';                                          /* 146 */
}

/* Per-entry work reset.  `title_load_end` is the "assets are already resident"
 * flag: when it is up the loader step is skipped, which is how coming back
 * from the debug menu does not re-read the title textures off the disc. */
static void TitleWrkInit(void)
{
    if (title_load_end != '\0')                                         /* 157 */
    {
        title_wrk.title_step = TITLE_STEP_LOAD;                         /* 158 */
    }
    else
    {
        title_wrk.title_step = TITLE_STEP_REQ;                          /* 161 */
    }

    title_wrk.stream_id = -1;                                           /* 163 */
    title_wrk.snd_id = 0;                                               /* 164 */
    title_wrk.snd_bank_id = 0;                                          /* 165 */
    title_wrk.wait_timer = '\0';                                        /* 166 */

    /* Kept so TitleLoadWait() can put the loader back the way it found it --
     * init_Title_Mode() raises the bank priority to 5 for the title's own
     * bank and nothing else would ever lower it again. */
    title_wrk.iOriginSndBankLoadPriority = SndBankGetLoadPriority();    /* 168 */
}

/* Claim heap for the three title textures and post their loads.  The logo pak
 * is per-language: TITLE_LOGO_PK2 + GetLanguage(), read fresh at every use
 * rather than cached (the ROM calls GetLanguage() five times across this
 * file). */
void TitleTexBackGroundLoadReq(void)
{
    if (title_bg_addr != (void *)0)                                     /* 179 */
    {
        LiberateTitleTexMem(&title_bg_addr);                            /* 180 */
    }
    if (title_logo_addr != (void *)0)                                   /* 182 */
    {
        LiberateTitleTexMem(&title_logo_addr);                          /* 183 */
    }
    if (outgame_cmn_tex_addr != (void *)0)                              /* 185 */
    {
        LiberateTitleTexMem(&outgame_cmn_tex_addr);                     /* 186 */
    }

    GetTitleTexMem(&title_bg_addr, TITLE_BG_PK2);                       /* 190 */
    GetTitleTexMem(&title_logo_addr, TITLE_LOGO_PK2 + GetLanguage());   /* 191 */
    GetTitleTexMem(&outgame_cmn_tex_addr, OUTGAME_PK2);                 /* 192 */

    TitleTexLoadReq(title_bg_addr, TITLE_BG_PK2);                       /* 195 */
    TitleTexLoadReq(title_logo_addr, TITLE_LOGO_PK2 + GetLanguage());   /* 196 */
    TitleTexLoadReq(outgame_cmn_tex_addr, OUTGAME_PK2);                 /* 197 */

    SetTitleLoadFlg('\x01');                                            /* 199 */
}

/* Reserve exactly the file's size out of the outgame heap. */
void GetTitleTexMem(void **tex_addr, int data_label)
{
    if (*tex_addr != (void *)0)                                         /* 212 */
    {
        LiberateTitleTexMem(tex_addr);                                  /* 213 */
    }

    *tex_addr = ol_loadGetHeap(GetFileSize(data_label));                /* 217 */
}

/* No null check in the ROM -- a failed GetTitleTexMem() would post a load to
 * address 0.  Kept as found; ol_loadGetHeap() asserts on exhaustion. */
void TitleTexLoadReq(void *tex_addr, int data_label)
{
    FileLoadReqEE(data_label, tex_addr, 5, (FILE_LOAD_CALLBACK)0, (void *)0); /* 230 */
}

/* True once all three textures and the title sound bank are resident.  The
 * bank priority is restored here rather than in init_Title_Mode() because the
 * raise has to stay in force for the whole of SndBankNew()'s load. */
static int TitleLoadWait(void)
{
    int res = 0;

    if ((FileLoadIsEnd2(TITLE_BG_PK2, title_bg_addr) != 0) &&           /* 248 */
        (FileLoadIsEnd2(TITLE_LOGO_PK2 + GetLanguage(),
                        title_logo_addr) != 0) &&                       /* 249 */
        (FileLoadIsEnd2(OUTGAME_PK2, outgame_cmn_tex_addr) != 0))       /* 250 */
    {
        if (SndBankIsReady(title_wrk.snd_bank_id) != 0)                 /* 252 */
        {
            /* Both stores carry line 254 -- GCC filled the jal's delay slot
             * with the `res = 1`, so their relative source order is not
             * recoverable.  They are independent. */
            SndBankSetLoadPriority(title_wrk.iOriginSndBankLoadPriority); /* 254 */
            res = 1;                                                    /* 254 */
        }
    }

    return res;                                                         /* 262 */
}

/* The loader.  Step 0 tears down and re-requests every outgame screen's
 * assets in one go -- the title is the only place where all of them are
 * guaranteed to be off the heap, so each sub-screen's background is claimed
 * here and stays resident for as long as the title mode does.
 *
 * The return value is a constant 1 and no caller looks at it. */
static int TitleMain(void)
{
    if (title_wrk.title_step == TITLE_STEP_REQ)                         /* 273 */
    {
        TitleMemFree();                                                 /* 275 */

        TitleTexBackGroundLoadReq();                                    /* 278 */

        ReleaseLoadGameTexMem();                                        /* 281 */
        GetLoadGameTexMem();                                            /* 283 */
        LoadGameDataLoadReq();                                          /* 285 */

        SetupBackGroundLoadReq();                                       /* 288 */

        OptionBackGroundLoadReq();                                      /* 291 */

        GalleryBackGroundLoadReq();                                     /* 294 */

        GameDataSaveTexMemFree();                                       /* 297 */

        AlbumEnd();                                                     /* 300 */

        MenuCamMainMemFree();                                           /* 303 */

        GameDataSaveBackGroundLoadReq(ol_loadGetHeap, ol_loadFreeHeap); /* 306 */

        AlbumBackGroundLoadReq(ol_loadGetHeap, ol_loadFreeHeap);        /* 309 */

        MenuCamMainBackGroundLoadReq(ol_loadGetHeap, ol_loadFreeHeap);  /* 312 */

        BackGroundLoadReq();                                            /* 315 */

        title_wrk.title_step = TITLE_STEP_LOAD;                         /* 317 */
    }

    if ((title_wrk.title_step == TITLE_STEP_LOAD) &&                    /* 320 */
        (TitleLoadWait() != 0) &&                                       /* 322 */
        (LoadGameDataLoadWait() != 0))                                  /* 324 */
    {
        title_wrk.title_step = TITLE_STEP_MAIN;                         /* 325 */
    }

    return 1;                                                           /* 331 */
}

/* The title BGM's StreamAuto handle.  Every child phase re-plays the stream
 * when it finds it stopped, so this is written from a dozen places. */
void SetTitleStreamID(int stream_id)
{
    title_wrk.stream_id = stream_id;                                    /* 345 */
}

void SetTitleSoundID(int sound_id)
{
    title_wrk.snd_id = sound_id;                                        /* 357 */
}

/* "Title assets are resident."  Cleared by whoever takes the heap away. */
void SetTitleLoadFlg(char flg)
{
    title_load_end = flg;                                               /* 369 */
}

/* Suppress the scrolling background for one frame -- used by screens that
 * draw their own full-screen art over the title. */
void SetTitleBgSendLock(u_char flg)
{
    title_bg_send_lock = flg;                                           /* 381 */
}

void *GetTitleBgTexAddr(void)
{
    return title_bg_addr;                                               /* 398 */
}

void *GetTitleLogoTexAddr(void)
{
    return title_logo_addr;                                             /* 409 */
}

int GetTitleStreamID(void)
{
    return title_wrk.stream_id;                                         /* 420 */
}

int GetTitleSoundBankID(void)
{
    return title_wrk.snd_bank_id;                                       /* 431 */
}

int GetTitleSoundID(void)
{
    return title_wrk.snd_id;                                            /* 442 */
}

/* The shared cursor pulse.  Driven by Zero2Anim2D_CsrAnimCtrl() once a frame
 * out of pre_Title_Mode(); every cursor and the PRESS START plate read it. */
u_char GetTitleAnimRGB(void)
{
    return title_disp_ctrl.rgb;                                         /* 453 */
}

void *GetOutGameCmnTexAddr(void)
{
    return outgame_cmn_tex_addr;                                        /* 463 */
}

/* Stop accepting input but keep drawing.  Used on the way out of the title. */
void TitleEndReq(void)
{
    title_wrk.title_step = TITLE_STEP_END;                              /* 476 */
    title_wrk.wait_timer = '\0';                                        /* 478 */
}

/* Cancel any load still in flight, then give the heap back. */
void TitleMemFree(void)
{
    TitleTexLoadCancel(title_bg_addr, TITLE_BG_PK2);                    /* 489 */
    TitleTexLoadCancel(title_logo_addr, TITLE_LOGO_PK2 + GetLanguage());/* 490 */
    TitleTexLoadCancel(outgame_cmn_tex_addr, OUTGAME_PK2);              /* 491 */

    LiberateTitleTexMem(&title_bg_addr);                                /* 495 */
    LiberateTitleTexMem(&title_logo_addr);                              /* 496 */
    LiberateTitleTexMem(&outgame_cmn_tex_addr);                         /* 497 */
}

void LiberateTitleTexMem(void **tex_addr)
{
    if (*tex_addr != (void *)0)                                         /* 508 */
    {
        ol_loadFreeHeap(*tex_addr);                                     /* 509 */
        *tex_addr = (void *)0;                                          /* 510 */
    }
}

static void TitleTexLoadCancel(void *tex_addr, int data_label)
{
    if ((tex_addr != (void *)0) &&
        (FileLoadIsEnd2(data_label, tex_addr) == 0))                    /* 524 */
    {
        FileLoadCancel2(data_label, tex_addr,
                        (FILE_LOAD_CALLBACK)0, (void *)0);              /* 526 */
    }
}

static void TitleDispCtrlInit(void)
{
    title_disp_ctrl.rgb = '\x40';                                       /* 544 */
    title_disp_ctrl.start_timer = '\0';                                 /* 545 */
    title_disp_ctrl.timer = 0;                                          /* 546 */
}

// ──────────────────────────────────────────────────────────────────────
// GPhase per-phase callbacks, invoked via the main/gphase.c dispatch tables.
// GID_TITLE_MODE is the enclosing mode; every other phase below it runs with
// pre_Title_Mode() ahead of it and after_Title_Mode() behind it.

void init_Title_Mode(void)
{
    TitleWrkInit();                                                     /* 560 */

    TitleDispCtrlInit();                                                /* 563 */
    TitleMenuCtrlInit();                                                /* 565 */

    title_wrk.stream_id = StreamAutoPlay(BGM000_TITLE_STR, BGM000_TITLE_HXD,
                                         0xc, 0, 0, 0x3200, 0,
                                         (SND_3D_SET *)0);              /* 569 */

    /* Raised for the title's own bank load and put back by TitleLoadWait(). */
    SndBankSetLoadPriority(5);                                          /* 572 */
    title_wrk.snd_bank_id = SndBankNew(AJ_002_32_BD, AJ_002_32_HXD, -1);/* 573 */
}

GPHASE_ENUM pre_Title_Mode(GPHASE_ENUM dummy)
{
    (void)dummy;

    TitleMain();                                                        /* 580 */

    /* (u_char)(title_step - 2) < 2, i.e. step 2 or step 3. */
    if ((title_wrk.title_step == TITLE_STEP_MAIN) ||
        (title_wrk.title_step == TITLE_STEP_END))                       /* 582 */
    {
        Zero2Anim2D_CsrAnimCtrl(&title_disp_ctrl.start_timer,
                                &title_disp_ctrl.rgb);                  /* 584 */

        if (title_bg_send_lock == '\0')                                 /* 586 */
        {
            DispTitleBack(&title_disp_ctrl.timer, GetTitleBgTexAddr()); /* 588 */
        }
    }

    return GPHASE_CONTINUE;                                             /* 593 */
}

GPHASE_ENUM after_Title_Mode(GPHASE_ENUM result)
{
    (void)result;
    return GPHASE_CONTINUE;                                             /* 597 */
}

void end_Title_Mode(void)
{
    StreamAutoFadeOut(title_wrk.stream_id, 3);                          /* 602 */
    SndBankRelease(title_wrk.snd_bank_id);                              /* 605 */
}

void init_Title_Top(void)
{
    UnlockMoveTitleMovie();                                             /* 614 */
}

GPHASE_ENUM one_Title_Top(GPHASE_ENUM dummy)
{
    (void)dummy;

    switch (title_wrk.title_step)                                       /* 620 */
    {
    case TITLE_STEP_MAIN:
        TitleTopMain();                                                 /* 623 */
        /* fall through */

    case TITLE_STEP_END:
        TitleTopDispMain(0, 0, 0x80);                                   /* 627 */
        break;
    }

    if (CheckMoveTitleMovie() != 0)                                     /* 632 */
    {
        SetNextGPhase(GID_TITLE_MOVE_MOVIE);                            /* 634 */
    }

    /* R3 (0x400 in the remapped pad layout) forces the attract movie. */
    if ((pad[0].one & 0x400U) != 0)                                     /* 638 */
    {
        StreamAutoFadeOut(title_wrk.stream_id, 0);                      /* 640 */
        SetNextGPhase(GID_TITLE_MOVE_MOVIE);                            /* 642 */
    }

    return GPHASE_CONTINUE;                                             /* 647 */
}

void end_Title_Top(void)
{
}

void init_Title_Menu(void)
{
    LockMoveTitleMovie();                                               /* 660 */
}

GPHASE_ENUM one_Title_Menu(GPHASE_ENUM dummy)
{
    (void)dummy;

    switch (title_wrk.title_step)                                       /* 670 */
    {
    case TITLE_STEP_MAIN:
        TitleMenuMain();                                                /* 673 */
        /* fall through */

    case TITLE_STEP_END:
        TitleMenuDispMain(0, 0, 0x80);                                  /* 677 */
        break;
    }

    if (StreamAutoIsPlaying(GetTitleStreamID()) == 0)                   /* 684 */
    {
        SetTitleStreamID(StreamAutoPlay(BGM000_TITLE_STR, BGM000_TITLE_HXD,
                                        0xc, 0, 0, 0x3200, 0,
                                        (SND_3D_SET *)0));              /* 687 */
    }

    return GPHASE_CONTINUE;                                             /* 708 */
}

void end_Title_Menu(void)
{
    UnlockMoveTitleMovie();                                             /* 714 */
}

void init_Title_NewGame(void)
{
    LockMoveTitleMovie();                                               /* 722 */
    NewGameCtrlInit();                                                  /* 724 */
}

GPHASE_ENUM one_Title_NewGame(GPHASE_ENUM dummy)
{
    (void)dummy;

    if (StreamAutoIsPlaying(GetTitleStreamID()) == 0)                   /* 729 */
    {
        SetTitleStreamID(StreamAutoPlay(BGM000_TITLE_STR, BGM000_TITLE_HXD,
                                        0xc, 0, 0, 0x3200, 0,
                                        (SND_3D_SET *)0));              /* 732 */
    }

    NewGameMain();                                                      /* 736 */
    NewGameDispMain();                                                  /* 738 */

    return GPHASE_CONTINUE;                                             /* 740 */
}

void end_Title_NewGame(void)
{
    UnlockMoveTitleMovie();                                             /* 745 */
}

void init_Title_LoadGame(void)
{
    LockMoveTitleMovie();                                               /* 753 */
    LoadGameInit();                                                     /* 756 */
}

GPHASE_ENUM one_Title_LoadGame(GPHASE_ENUM dummy)
{
    (void)dummy;

    if (StreamAutoIsPlaying(GetTitleStreamID()) == 0)                   /* 761 */
    {
        SetTitleStreamID(StreamAutoPlay(BGM000_TITLE_STR, BGM000_TITLE_HXD,
                                        0xc, 0, 0, 0x3200, 0,
                                        (SND_3D_SET *)0));              /* 764 */
    }

    LoadGameMain();                                                     /* 768 */
    LoadGameDispMain();                                                 /* 770 */

    return GPHASE_CONTINUE;                                             /* 772 */
}

void end_Title_LoadGame(void)
{
    UnlockMoveTitleMovie();                                             /* 777 */
    LoadGameEnd();                                                      /* 780 */
}

void init_Title_Setup(void)
{
    StreamAutoFadeOut(GetTitleStreamID(), 2);                           /* 788 */
    LockMoveTitleMovie();                                               /* 791 */
    SetupInit();                                                        /* 794 */
}

/* Setup runs as a `pre` phase, so it is the only child screen gated on the
 * loader step -- it can be entered while the title is still loading. */
GPHASE_ENUM pre_Title_Setup(GPHASE_ENUM dummy)
{
    (void)dummy;

    if (title_wrk.title_step == TITLE_STEP_MAIN)                        /* 798 */
    {
        SetupMain();                                                    /* 800 */
        SetupDispMain();                                                /* 803 */
    }

    return GPHASE_CONTINUE;                                             /* 806 */
}

GPHASE_ENUM after_Title_Setup(GPHASE_ENUM result)
{
    (void)result;
    return GPHASE_CONTINUE;                                             /* 810 */
}

void end_Title_Setup(void)
{
    UnlockMoveTitleMovie();                                             /* 815 */
}

void init_Title_Album(void)
{
    StreamAutoFadeOut(GetTitleStreamID(), 2);                           /* 823 */
    SetTitleStreamID(StreamAutoPlay(BGM004_MENU1_STR, BGM004_MENU1_HXD,
                                    0xc, 0, 1, 0x3200, 0,
                                    (SND_3D_SET *)0));                  /* 827 */

    LockMoveTitleMovie();                                               /* 830 */
    TitleAlbumInit();                                                   /* 833 */
}

GPHASE_ENUM one_Title_Album(GPHASE_ENUM dummy)
{
    (void)dummy;

    TitleAlbumMain();                                                   /* 838 */
    TitleAlbumDispMain();                                               /* 841 */

    return GPHASE_CONTINUE;                                             /* 843 */
}

void end_Title_Album(void)
{
    StreamAutoFadeOut(GetTitleStreamID(), 2);                           /* 848 */
    SetTitleStreamID(StreamAutoPlay(BGM000_TITLE_STR, BGM000_TITLE_HXD,
                                    0xc, 0, 0, 0x3200, 0,
                                    (SND_3D_SET *)0));                  /* 852 */

    UnlockMoveTitleMovie();                                             /* 855 */
}

void init_Title_Gallery(void)
{
    StreamAutoFadeOut(GetTitleStreamID(), 2);                           /* 863 */
    SetTitleStreamID(StreamAutoPlay(BGM010_OMAKE_STR, BGM010_OMAKE_HXD,
                                    0xc, 0, 1, 0x3200, 0,
                                    (SND_3D_SET *)0));                  /* 867 */

    LockMoveTitleMovie();                                               /* 870 */
    GalleryInit();                                                      /* 871 */
}

/* The gallery draws from inside its own Main(); there is no DispMain. */
GPHASE_ENUM one_Title_Gallery(GPHASE_ENUM dummy)
{
    (void)dummy;

    GalleryMain();                                                      /* 875 */

    return GPHASE_CONTINUE;                                             /* 876 */
}

void end_Title_Gallery(void)
{
    GalleryEnd();                                                       /* 880 */

    StreamAutoFadeOut(GetTitleStreamID(), 2);                           /* 883 */
    SetTitleStreamID(StreamAutoPlay(BGM000_TITLE_STR, BGM000_TITLE_HXD,
                                    0xc, 0, 0, 0x3200, 0,
                                    (SND_3D_SET *)0));                  /* 887 */

    UnlockMoveTitleMovie();                                             /* 890 */
}

void init_Title_Option(void)
{
    LockMoveTitleMovie();                                               /* 898 */
    OptionInit(0);                                                      /* 899 */
}

GPHASE_ENUM one_Title_Option(GPHASE_ENUM dummy)
{
    (void)dummy;

    if (StreamAutoIsPlaying(GetTitleStreamID()) == 0)                   /* 904 */
    {
        SetTitleStreamID(StreamAutoPlay(BGM000_TITLE_STR, BGM000_TITLE_HXD,
                                        0xc, 0, 0, 0x3200, 0,
                                        (SND_3D_SET *)0));              /* 907 */
    }

    OptionMain();                                                       /* 911 */

    return GPHASE_CONTINUE;                                             /* 913 */
}

void end_Title_Option(void)
{
    OptionEnd();                                                        /* 918 */
    UnlockMoveTitleMovie();                                             /* 919 */
}

void init_Title_FrameRate_Sel(void)
{
    LockMoveTitleMovie();                                               /* 928 */
}

GPHASE_ENUM one_Title_FrameRate_Sel(GPHASE_ENUM dummy)
{
    (void)dummy;

    FrameRateSelMain();                                                 /* 933 */
    FrameRateSelDispMain();                                             /* 936 */

    return GPHASE_CONTINUE;                                             /* 938 */
}

void end_Title_FrameRate_Sel(void)
{
    UnlockMoveTitleMovie();                                             /* 943 */
}

/* Chapter select is the one child screen that does not touch the movie lock
 * or the BGM -- it is a debug-menu entry, not a shipped title item. */
void init_Title_Chapter_Sel(void)
{
    ChapterSelCtrlInit();                                               /* 950 */
}

GPHASE_ENUM one_Title_Chapter_Sel(GPHASE_ENUM dummy)
{
    (void)dummy;

    ChapterSelMain();                                                   /* 954 */
    ChapterSelDispMain();                                               /* 956 */

    return GPHASE_CONTINUE;                                             /* 958 */
}

void end_Title_Chapter_Sel(void)
{
}

/* The three-frame handover into the attract movie.  The title top keeps being
 * drawn over it so the fade out of the BGM has somewhere to land. */
void init_Title_Move_Movie(void)
{
    StreamAutoFadeOut(title_wrk.stream_id, 3);                          /* 971 */
    title_move_movie_timer = '\0';                                      /* 973 */
}

GPHASE_ENUM one_Title_Move_Movie(GPHASE_ENUM dummy)
{
    (void)dummy;

    title_move_movie_timer++;                                           /* 979 */

    TitleTopDispMain(0, 0, 0x80);                                       /* 982 */

    if (title_move_movie_timer >= 4)                                    /* 985 */
    {
        SetNextGPhase(GID_TITLE_MOVIE_MODE);                            /* 987 */
    }

    return GPHASE_CONTINUE;                                             /* 991 */
}

void end_Title_Move_Movie(void)
{
}
