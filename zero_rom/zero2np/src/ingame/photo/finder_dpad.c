// FILE: ingame/photo/finder_dpad.c
//
// PORT ADDITION -- no ROM counterpart.  The finder's d-pad: film and healing
// items.  The header's banner says what it does for the player.
//
// Two pieces of state, both this file's own:
//
//   * fdp_choice, which healing item the player picked, as an index into
//     fdp_heal[].  It is only ever where to start looking: the item shown and
//     used is the first one carried from it on (FdpShown()), so running out of
//     one, or loading a save that carries something else, needs no
//     bookkeeping.
//   * fdp_picture[], the healing items' pictures, read once into this file's
//     own memory rather than the game's heap, whose layout the ROM counts on.
//     The item menu loads the same files on demand into its cross-fade slots;
//     a finder frame cannot wait for a read.
//
// The picture is sent to VRAM page 0x2bc0 just before it is drawn.  That page
// is the in-game scratch every 2D and 3D draw that needs room sends to right
// before it draws -- a ghost's textures, the room's TRI2 banks, the item
// menu's picture -- so nothing drawn later relies on what was there.  It is
// laid out at its own width (TBW 4, 256x256) with its CLUT right after it, so
// all of it ends below the finder pak (0x2d3d on), whose readouts are still
// being drawn after it, and clear of the font (0x0000..0x1460, CLUTs at
// 0x3f08).

#include "finder_dpad.h"

#include <SDL3/SDL_log.h>

#include "eetypes.h"

#include "../../common/variable.h"              /* plyr_wrk / pad              */
#include "../../graphics/graph2d/g2d_draw.h"    /* DISP_SPRT / DispSprD        */
#include "../../graphics/graph2d/tim2.h"        /* MakeTim2Direct              */
#include "../../miopan/miopan_config.h"         /* [game] finder_dpad          */
#include "../../sdk/libgraph.h"                 /* SCE_GS_SET_TEX0             */
#include "../../system/eeiop/cddat.h"           /* ITEM_000_TM2 / GetFileSize  */
#include "../../system/eeiop/fileload.h"        /* FileLoadIsEnd2              */
#include "../../system/eeiop/snd3d.h"           /* SND_3D_SET                  */
#include "../../system/os/eecdvd.h"             /* LoadReq                     */
#include "../../system/os/system.h"             /* SystemBankPlay              */
#include "../item/prg/item.h"                   /* ItemUse / item_dat          */
#include "../plyr/player.h"                     /* PlayerModeIsFinder          */
#include "finder.h"                             /* FinderBankPlay              */
#include "n_finder_dat.h"                       /* n_finder_dat                */

/* pad[0].one bits, in the remapped layout finder_camera.c reads pad[0].now
 * in: 0x1000 is UP, not TRIANGLE. */
#define FDP_PAD_UP          0x1000
#define FDP_PAD_RIGHT       0x2000
#define FDP_PAD_DOWN        0x4000
#define FDP_PAD_LEFT        0x8000

/* The viewfinder's own cues, not the item menu's: the click of the equip
 * tray's selector sliding to the next function (CNEquipTrayWrk::Rotate()) for
 * a film or an item picked, the chime of power banked as a stock item
 * (FinderConvertGage2StockNum()) for one taken, both from the finder bank;
 * and from the system bank the camera's refusal, as when the shutter is not
 * charged or there is no film (PlyrPhotoChk2()). */
#define FDP_FSE_SELECT      16
#define FDP_FSE_TAKE        14
#define FDP_SE_REFUSE       4

/* Films are item ids 0..4 -- CCameraFilm::mFilmType is the id itself. */
#define FDP_FILM_NUM        5

/* Room for every ITEM_TYPE_HP row item_dat[] could have.  It has two, the
 * herbal medicine and the sacred water. */
#define FDP_HEAL_MAX        4

/* An item picture is a 256x256 8-bit TIM2 with a 256-colour CLUT, 66624 bytes
 * on the disc; this leaves a little over. */
#define FDP_PICTURE_SIZE    0x10600
#define FDP_PICTURE_W       256
#define FDP_PICTURE_H       256

/* Where the picture goes in VRAM: see the banner.  TBW is in 64-pixel units;
 * TW/TH are log2 of the picture's sides. */
#define FDP_TBP             0x2bc0
#define FDP_TBW             4
#define FDP_TW              8
#define FDP_TH              8
#define FDP_CBP             0x2cc0
#define FDP_TEX0            SCE_GS_SET_TEX0(FDP_TBP, FDP_TBW, SCE_GS_PSMT8,      \
                                            FDP_TW, FDP_TH, 1, 0, FDP_CBP,     \
                                            0, 0, 0, 1)

/* The icon, on the 640x448 frame: centred over the health bar (x 575..598),
 * in the dark strip between the film readout (y 29..49) and the top of the
 * bar (y 200).  The count is centred under it in the film readout's digits,
 * shrunk. */
#define FDP_ICON_CX         587.0f
#define FDP_ICON_CY         140.0f
#define FDP_ICON_SIZE       48.0f
#define FDP_NUM_Y           158
#define FDP_NUM_PITCH       8
#define FDP_NUM_SCALE       0.7f

/* The film readout's ten digit records (film_no.c's FD_FILM_DIGIT), each
 * this wide. */
#define FDP_FILM_DIGIT      0x29
#define FDP_DIGIT_W         11.0f

/* The finder HUD's draw state: pri 16 like every n_finder_dat[] record, depth
 * read but never written, source-over. */
#define FDP_PRI             16
#define FDP_ZBUF_NO_WRITE   0x000000010a000118ULL
#define FDP_ALPHA_BLEND     0x44

/* A pick or a use swells the icon by a quarter and lets it settle back over
 * this many frames. */
#define FDP_FLASH_FRAMES    8
#define FDP_FLASH_GROW      0.25f

static int    fdp_heal[FDP_HEAL_MAX];       /* item ids, item_dat[] order    */
static int    fdp_heal_num;
static int    fdp_choice;
static int    fdp_flash;

alignas(64) static u_char fdp_picture[FDP_HEAL_MAX][FDP_PICTURE_SIZE];
static int    fdp_picture_state[FDP_HEAL_MAX];  /* FDP_PIC_*                  */
static int    fdp_picture_tries[FDP_HEAL_MAX];

#define FDP_PIC_NONE        0               /* not asked for, or no file     */
#define FDP_PIC_LOADING     1
#define FDP_PIC_READY       2
#define FDP_PIC_BAD         3               /* read, but not a picture       */

/* A load that ends with no picture in the buffer was most likely cancelled --
 * init_OutGame_Main() and a soft reset drop every queued load -- so it is
 * asked for again, this many times in all. */
#define FDP_PIC_TRIES       3

static int FdpCarried(int k)
{
    return GetPlyrItemHaveNum(fdp_heal[k]) > 0;
}

/* The healing item shown and used: the chosen one, or the first carried after
 * it.  -1 when she carries none. */
static int FdpShown(void)
{
    int i;
    int k;

    for (i = 0; i < fdp_heal_num; i++)
    {
        k = (fdp_choice + i) % fdp_heal_num;
        if (FdpCarried(k) != 0)
        {
            return k;
        }
    }

    return -1;
}

/* Each played as its own call site in the ROM plays it. */
static void FdpSelectCue(void)
{
    FinderBankPlay(FDP_FSE_SELECT, 1, 0, 0, (SND_3D_SET *)nullptr, 0x3200, 0x1000);
}

static void FdpTakeCue(void)
{
    FinderBankPlay(FDP_FSE_TAKE, 1, 0, 0, (SND_3D_SET *)nullptr, 0x3200, 0x1000);
}

static void FdpRefuseCue(void)
{
    SystemBankPlay(FDP_SE_REFUSE, 1, 1, 0, (SND_3D_SET *)nullptr, 0x3200, 0x1000);
}

/* Up and down: the next film carried in that direction, wrapping round. */
static void FdpFilmStep(int step)
{
    const int film = GetPlyrEquipmentFilmType();
    int i;
    int next;

    for (i = 1; i < FDP_FILM_NUM; i++)
    {
        next = (film + step * i + FDP_FILM_NUM * FDP_FILM_NUM) % FDP_FILM_NUM;
        if (GetPlyrItemHaveNum(next) > 0)
        {
            /* ItemFilmEquip(): loaded, not spent. */
            FdpSelectCue();
            ItemUse(next, 1);
            return;
        }
    }

    /* Only the one film. */
    FdpRefuseCue();
}

/* Left: the next healing item carried. */
static void FdpItemStep(void)
{
    const int shown = FdpShown();
    int i;
    int k;

    if (shown >= 0)
    {
        for (i = 1; i < fdp_heal_num; i++)
        {
            k = (shown + i) % fdp_heal_num;
            if (FdpCarried(k) != 0)
            {
                fdp_choice = k;
                fdp_flash = FDP_FLASH_FRAMES;
                FdpSelectCue();
                return;
            }
        }
    }

    /* None, or only the one. */
    FdpRefuseCue();
}

/* Right: take it.  Refused at full health, as the menu refuses it. */
static void FdpItemUse(void)
{
    const int shown = FdpShown();

    if (shown < 0 || plyr_wrk.cmn_wrk.st.hp == plyr_wrk.cmn_wrk.st.hpmax)
    {
        FdpRefuseCue();
        return;
    }

    /* ItemPlyrHPRecover(), then the item is spent. */
    FdpTakeCue();
    ItemUse(fdp_heal[shown], 1);
    fdp_choice = shown;
    fdp_flash = FDP_FLASH_FRAMES;
}

static void FdpPictureLoadReq(int k)
{
    const int file_no = ITEM_000_TM2 + fdp_heal[k];

    fdp_picture_state[k] = FDP_PIC_NONE;
    if (GetFileSize(file_no) == 0 || GetFileSize(file_no) > FDP_PICTURE_SIZE)
    {
        SDL_Log("[port] finder d-pad: item %d's picture (file %d, %u bytes) "
                "does not fit; no icon for it",
                fdp_heal[k], file_no, GetFileSize(file_no));
        return;
    }

    if (LoadReq(file_no, (uintptr_t)fdp_picture[k]) >= 0)
    {
        fdp_picture_state[k] = FDP_PIC_LOADING;
        fdp_picture_tries[k]++;
    }
}

void FinderDpadLoadOnce(void)
{
    int i;

    fdp_heal_num = 0;
    for (i = 0; i < ITEM_DAT_MAX && fdp_heal_num < FDP_HEAL_MAX; i++)
    {
        if (item_dat[i].type == ITEM_TYPE_HP)
        {
            fdp_heal[fdp_heal_num++] = i;
        }
    }

    for (i = 0; i < fdp_heal_num; i++)
    {
        FdpPictureLoadReq(i);
    }
}

/* The picture of fdp_heal[k], once it has arrived and proved to be one. */
static u_int *FdpPicture(int k)
{
    TIM2_PICTUREHEADER *ph;

    if (fdp_picture_state[k] == FDP_PIC_LOADING &&
        FileLoadIsEnd2(ITEM_000_TM2 + fdp_heal[k], fdp_picture[k]) != 0)
    {
        ph = Tim2GetPictureHeader(fdp_picture[k], 0);
        if (fdp_picture[k][0] == 'T' && fdp_picture[k][1] == 'I' &&
            fdp_picture[k][2] == 'M' && fdp_picture[k][3] == '2' &&
            ph != nullptr && ph->ImageType == 5 &&      /* 8-bit indexed */
            ph->ImageWidth == FDP_PICTURE_W && ph->ImageHeight == FDP_PICTURE_H)
        {
            fdp_picture_state[k] = FDP_PIC_READY;
        }
        else if (fdp_picture_tries[k] < FDP_PIC_TRIES)
        {
            FdpPictureLoadReq(k);
        }
        else
        {
            fdp_picture_state[k] = FDP_PIC_BAD;
            SDL_Log("[port] finder d-pad: item %d's picture is not a 256x256 "
                    "8-bit TIM2; no icon for it", fdp_heal[k]);
        }
    }

    return fdp_picture_state[k] == FDP_PIC_READY ? (u_int *)fdp_picture[k]
                                                 : (u_int *)nullptr;
}

int FinderDpadOwnsDpad(void)
{
    return miopan_config.game.finder_dpad != 0 && PlayerModeIsFinder() != 0;
}

void FinderDpadCtrl(void)
{
    u_short one;

    if (FinderDpadOwnsDpad() == 0)
    {
        return;
    }

    if (fdp_flash > 0)
    {
        fdp_flash--;
    }

    /* Not while the camera is going down, nor while a ghost has hold of her
     * -- conditions 1 and 3 are the two that take the camera away. */
    if (plyr_wrk.finder_tm != 0 ||
        plyr_wrk.cmn_wrk.st.cond == 1 || plyr_wrk.cmn_wrk.st.cond == 3)
    {
        return;
    }

    one = pad[0].one;

    if ((one & FDP_PAD_UP) != 0)
    {
        FdpFilmStep(1);
    }
    else if ((one & FDP_PAD_DOWN) != 0)
    {
        FdpFilmStep(-1);
    }
    else if ((one & FDP_PAD_LEFT) != 0)
    {
        FdpItemStep();
    }
    else if ((one & FDP_PAD_RIGHT) != 0)
    {
        FdpItemUse();
    }
}

void FinderDpadDraw(int fndr_mx, int fndr_my, int iAlpha)
{
    const int shown = FdpShown();
    u_int    *picture;
    SPRT_DAT  sd;
    DISP_SPRT ds;
    float     scale;
    int       have;
    float     num_x;

    if (miopan_config.game.finder_dpad == 0 || shown < 0)
    {
        return;
    }

    picture = FdpPicture(shown);
    if (picture != nullptr)
    {
        /* The file's own GsTex0 is not where it goes here; the item menu
         * rewrites it the same way (MenuTim2SendVram()). */
        Tim2GetPictureHeader(picture, 0)->GsTex0 = FDP_TEX0;
        MakeTim2Direct(picture, FDP_TBP, 0);
        MakeClutDirect(picture, FDP_CBP, 0);

        sd.tex0  = FDP_TEX0;
        sd.u     = 0;
        sd.v     = 0;
        sd.w     = FDP_PICTURE_W;
        sd.h     = FDP_PICTURE_H;
        sd.x     = 0;
        sd.y     = 0;
        sd.pri   = FDP_PRI;
        sd.alpha = 0x80;
        sd.flip  = 0;
        sd.bln   = 1;                           /* filtered: it is shrunk  */
        CopySprDToSpr(&ds, &sd);

        scale = FDP_ICON_SIZE / (float)FDP_PICTURE_W
                * (1.0f + FDP_FLASH_GROW * (float)fdp_flash / FDP_FLASH_FRAMES);

        /* Shrunk about its own centre, so the swell grows it in place. */
        ds.csx    = FDP_ICON_CX + (float)fndr_mx;
        ds.csy    = FDP_ICON_CY + (float)fndr_my;
        ds.x      = ds.csx - (float)FDP_PICTURE_W * 0.5f;
        ds.y      = ds.csy - (float)FDP_PICTURE_H * 0.5f;
        ds.scw    = scale;
        ds.sch    = scale;
        ds.zbuf   = FDP_ZBUF_NO_WRITE;
        ds.alphar = FDP_ALPHA_BLEND;
        ds.alpha  = (u_char)iAlpha;
        DispSprD(&ds);
    }

    /* Two figures at most, as the film readout keeps it.  SetNumerousDisp()
     * lays a number out leftwards from its last digit; that digit goes where
     * the whole number comes out centred under the icon. */
    have  = GetPlyrItemHaveNum(fdp_heal[shown]);
    have  = have < 100 ? have : 99;
    num_x = FDP_ICON_CX + ((have < 10 ? 0.0f : (float)FDP_NUM_PITCH)
                           - FDP_DIGIT_W * FDP_NUM_SCALE) * 0.5f;
    SetNumerousDisp(&n_finder_dat[FDP_FILM_DIGIT], have, iAlpha, FDP_NUM_PITCH,
                    fndr_mx + (int)(num_x + 0.5f), fndr_my + FDP_NUM_Y,
                    FDP_NUM_SCALE, 0, 0);
}
