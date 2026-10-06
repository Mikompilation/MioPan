// FILE: /home/zero_rom/zero2np/src/outgame/chapter_sel.c
//
// Debug chapter-select screen.  Its own translation unit in the ROM
// (chapter_sel.o); title.c only owns the three phase callbacks that call in
// here.  The whole screen is drawn with the ASCII debug text writer, so there
// are no textures to load and no assets to free -- picking GAMESTART flushes
// the chosen settings into ingame_wrk and hands off to GID_STORY_LOAD_MISSION.
//
// Reconstructed from the Feb 6 2004 prototype (SLES_523.84).

#include "chapter_sel.h"

#include "title.h"                          // GetTitleSoundID / SetTitleLoadFlg
#include "../common/variable.h"             // pad / paddat / ingame_wrk
#include "../common/utility2.h"             // PRINT_ASSERT
#include "../graphics/graph2d/message.h"    // SetASCIIString2 / PrintNumber_N
#include "../ingame/ingame.h"               // IngameWrkInit / InitCostume
#include "../ingame/loading/loading.h"      // LoadingTexLoadWait
#include "../ingame/plyr/plyr_mdl.h"        // SetPlyrMdlNo / SetSisterAcsNo
#include "../main/gphase.h"                 // SetNextGPhase / GID_*
#include "../main/main_decls.h"             // EventDataLoadReq / costume setters
#include "../system/eeiop/snd_buffer.h"     // SndBufIsPlaying
#include "../system/os/system.h"            // SystemBankPlay
#include "../system/pad/pad.h"              // paddat / pad

#define CHAPTER_SEL_MENU_COUNT 7

/* Field order is the ROM's: the debug menu walks the cursor down this struct
 * in declaration order, and the two settings that are not cursor rows
 * (difficulty, clear count) sit at the end.  Signed char throughout -- every
 * ROM access to these fields is an `lb`. */
typedef struct                      /* 0x8 */
{
    /* 0x0 */ char step;
    /* 0x1 */ char menu_csr;
    /* 0x2 */ char chapter_csr;
    /* 0x3 */ char costume_csr;
    /* 0x4 */ char mio_csr;
    /* 0x5 */ char mayu_csr;
    /* 0x6 */ char diff_csr;
    /* 0x7 */ char clear_csr;
} CHAPTER_SEL_CTRL;

static CHAPTER_SEL_CTRL chapter_sel_ctrl;   /* sbss 3f4af0 */

/* Mio / Mayu model numbers per costume row.  rdata 3a2350. */
static const short costume_tbl[9][2] =
{
    {  0,  1 },
    { 64, 65 },
    { 66, 67 },
    { 68, 69 },
    { 70, 71 },
    { 72, 73 },
    { 74, 75 },
    { 62, 63 },
    { 76, 77 },
};

static void ChapterSelPad(void);

void ChapterSelCtrlInit(void)
{
    chapter_sel_ctrl.step = 0;                                          /* 118 */
    chapter_sel_ctrl.menu_csr = 0;                                      /* 119 */
    chapter_sel_ctrl.chapter_csr = 0;                                   /* 120 */
    chapter_sel_ctrl.costume_csr = 0;                                   /* 121 */
    chapter_sel_ctrl.mio_csr = 0;                                       /* 122 */
    chapter_sel_ctrl.mayu_csr = 0;                                      /* 123 */
    chapter_sel_ctrl.diff_csr = 1;                                      /* 124 */
    chapter_sel_ctrl.clear_csr = 0;                                     /* 125 */
}

void ChapterSelMain(void)
{
    if (chapter_sel_ctrl.step == 0) {                                   /* 140 */
        ChapterSelPad();                                                /* 142 */
        return;
    }

    /* Wait for the loading texture and for the title jingle to finish before
     * tearing the title down -- SetTitleLoadFlg(0) below frees its bank. */
    if ((chapter_sel_ctrl.step == 1) && (LoadingTexLoadWait() != 0)) {   /* 144 */
        if (SndBufIsPlaying(GetTitleSoundID()) == 0) {                   /* 148 */
            InitCostume();                                               /* 150 */
            SetPlyrMdlNo((int)costume_tbl[chapter_sel_ctrl.costume_csr][0]);   /* 153 */
            SetSisterMdlNo((int)costume_tbl[chapter_sel_ctrl.costume_csr][1]); /* 154 */

            if (chapter_sel_ctrl.mio_csr == 1) {                         /* 156 */
                SetPlyrAcsNo(6);                                         /* 157 */
            } else {
                SetPlyrAcsNo(-1);                                        /* 160 */
            }

            if (chapter_sel_ctrl.mayu_csr == 1) {                        /* 162 */
                SetSisterAcsNo(7);                                       /* 163 */
            } else {
                SetSisterAcsNo(-1);                                      /* 166 */
            }

            IngameWrkInit((int)chapter_sel_ctrl.chapter_csr,             /* 169 */
                          (int)chapter_sel_ctrl.diff_csr);

            /* CVariable<char,0,99>::SetValue, inlined -- hence the range
             * check and the assert banner naming common/variable.h. */
            if (chapter_sel_ctrl.clear_csr < 'd') {                      /* 171 */
                if (chapter_sel_ctrl.clear_csr < 0) {
                    PRINT_ASSERT("Set Value is Illegal");
                }
            } else {
                PRINT_ASSERT("Set Value is Illegal");
            }
            ingame_wrk.mClearCnt = chapter_sel_ctrl.clear_csr;

            SetTitleLoadFlg(0);                                       /* 174 */
            EventDataLoadReq();                                          /* 177 */
            SetNextGPhase(GID_STORY_LOAD_MISSION);                       /* 179 */
        }
    }
}

static void ChapterSelPad(void)
{
    if ((pad[0].rpt & 0x1000U) != 0) {                                  /* 194 */
        SystemBankPlay(0, 1, 0, 0, (SND_3D_SET *)nullptr, 0x3200, 0x1000);   /* 195 */
        chapter_sel_ctrl.menu_csr =                                     /* 197 */
            (char)((chapter_sel_ctrl.menu_csr + (CHAPTER_SEL_MENU_COUNT - 1)) %
                     CHAPTER_SEL_MENU_COUNT);
    } else if ((pad[0].rpt & 0x4000U) != 0) {                           /* 200 */
        SystemBankPlay(0, 1, 0, 0, (SND_3D_SET *)nullptr, 0x3200, 0x1000);   /* 201 */
        chapter_sel_ctrl.menu_csr =                                     /* 203 */
            (char)((chapter_sel_ctrl.menu_csr + 1) % CHAPTER_SEL_MENU_COUNT);
    } else if ((pad[0].rpt & 0x8000U) != 0) {                           /* 206 */
        SystemBankPlay(0, 1, 0, 0, (SND_3D_SET *)nullptr, 0x3200, 0x1000);   /* 207 */

        /* Decrement wraps by adding count-1 rather than going negative. */
        switch (chapter_sel_ctrl.menu_csr) {                            /* 208 */
        case 0:
            break;
        case 1:
            chapter_sel_ctrl.chapter_csr =                              /* 213 */
                (char)((chapter_sel_ctrl.chapter_csr + 10) % 11);
            break;                                                      /* 214 */
        case 2:
            chapter_sel_ctrl.costume_csr =                              /* 216 */
                (char)((chapter_sel_ctrl.costume_csr + 8) % 9);
            break;                                                      /* 217 */
        case 3:
            chapter_sel_ctrl.mio_csr ^= 1;                              /* 219 */
            break;                                                      /* 220 */
        case 4:
            chapter_sel_ctrl.mayu_csr ^= 1;                             /* 222 */
            break;
        case 5:
            chapter_sel_ctrl.diff_csr =                                 /* 225 */
                (char)((chapter_sel_ctrl.diff_csr + 3) % 4);
            break;                                                      /* 226 */
        case 6:
            chapter_sel_ctrl.clear_csr =                                /* 228 */
                (char)((chapter_sel_ctrl.clear_csr + 99) % 100);
            break;                                                      /* 229 */
        default:
            PRINT_ASSERT("Error! %s", __FUNCTION__);                    /* 231 */
            break;
        }
    } else if ((pad[0].rpt & 0x2000U) != 0) {                           /* 235 */
        SystemBankPlay(0, 1, 0, 0, (SND_3D_SET *)nullptr, 0x3200, 0x1000);   /* 236 */

        switch (chapter_sel_ctrl.menu_csr) {                            /* 237 */
        case 0:
            break;
        case 1:
            chapter_sel_ctrl.chapter_csr =                              /* 242 */
                (char)((chapter_sel_ctrl.chapter_csr + 1) % 11);
            break;                                                      /* 243 */
        case 2:
            chapter_sel_ctrl.costume_csr =                              /* 245 */
                (char)((chapter_sel_ctrl.costume_csr + 1) % 9);
            break;                                                      /* 246 */
        case 3:
            chapter_sel_ctrl.mio_csr ^= 1;                              /* 248 */
            break;                                                      /* 249 */
        case 4:
            chapter_sel_ctrl.mayu_csr ^= 1;                             /* 251 */
            break;                                                      /* 252 */
        case 5:
            chapter_sel_ctrl.diff_csr =                                 /* 254 */
                (char)((chapter_sel_ctrl.diff_csr + 1) % 4);
            break;                                                      /* 255 */
        case 6:
            chapter_sel_ctrl.clear_csr =                                /* 257 */
                (char)((chapter_sel_ctrl.clear_csr + 1) % 100);
            break;                                                      /* 258 */
        default:
            PRINT_ASSERT("Error! %s", __FUNCTION__);                    /* 260 */
            break;
        }
    } else if (*paddat[1] == 1) {                                       /* 264 */
        SystemBankPlay(1, 1, 0, 0, (SND_3D_SET *)nullptr, 0x3200, 0x1000);   /* 265 */
        SetNextGPhase(GID_TITLE_MENU);                                  /* 268 */
    } else if (*paddat[0] == 1) {                                       /* 271 */
        SystemBankPlay(3, 1, 0, 0, (SND_3D_SET *)nullptr, 0x3200, 0x1000);   /* 272 */

        /* Confirm on any row but GAMESTART just snaps the cursor back to it. */
        if (chapter_sel_ctrl.menu_csr != 0) {                           /* 274 */
            chapter_sel_ctrl.menu_csr = 0;
        } else {
            chapter_sel_ctrl.step = 1;                                  /* 275 */
        }
    }
}

void ChapterSelDispMain(void)
{
    /* Stack-local in the ROM: rebuilt from .sdata every frame rather than
     * held in a static table. */
    char *menu_sel_str[CHAPTER_SEL_MENU_COUNT];                         /* 293 */
    char *chapter_sel_str[11];
    char *costume_str[9];
    char *accessory_str[2];
    char *diff_str[4];
    int   i;
    int   y;

    menu_sel_str[0] = "GAMESTART";                                      /* 293 */
    menu_sel_str[1] = "CHAPTER";
    menu_sel_str[2] = "COSTUME";
    menu_sel_str[3] = "ACCESSORY MIO";
    menu_sel_str[4] = "ACCESSORY MAYU";
    menu_sel_str[5] = "DIFFICULTY";
    menu_sel_str[6] = "CLEAR_NUM";

    chapter_sel_str[0] = "CHAPTER 1";                                   /* 303 */
    chapter_sel_str[1] = "CHAPTER 2";
    chapter_sel_str[2] = "CHAPTER 3";
    chapter_sel_str[3] = "CHAPTER 4";
    chapter_sel_str[4] = "CHAPTER 5";
    chapter_sel_str[5] = "CHAPTER 6";
    chapter_sel_str[6] = "CHAPTER 7";
    chapter_sel_str[7] = "CHAPTER 8";
    chapter_sel_str[8] = "CHAPTER 9";
    chapter_sel_str[9] = "CHAPTER 10 1";
    chapter_sel_str[10] = "CHAPTER 10 2";

    costume_str[0] = "NORMAL";                                          /* 317 */
    costume_str[1] = "TYPE A";
    costume_str[2] = "TYPE B";
    costume_str[3] = "TYPE C";
    costume_str[4] = "TYPE D";
    costume_str[5] = "TYPE E";
    costume_str[6] = "TYPE F";
    costume_str[7] = "TYPE G";
    costume_str[8] = "TYPE H";

    accessory_str[0] = "OFF";                                           /* 329 */
    accessory_str[1] = "ON";

    diff_str[0] = "EASY";                                               /* 334 */
    diff_str[1] = "NORMAL";
    diff_str[2] = "HARD";
    diff_str[3] = "NIGHTMARE";

    SetASCIIString2(0, 170.0f, 60.0f, 1, 0xff, 0xff, 0xff,              /* 346 */
                    "CHAPTER SELECT MENU");

    y = 0x82;                                                           /* 349 */
    for (i = 0; i < CHAPTER_SEL_MENU_COUNT; i++) {
        if (i == chapter_sel_ctrl.menu_csr) {                           /* 350 */
            SetASCIIString2(0, 120.0f, (float)y, 1,                     /* 352 */
                            0xff, 0xff, 0xff, menu_sel_str[i]);
        } else {
            SetASCIIString2(0, 120.0f, (float)y, 1,                     /* 355 */
                            0x1e, 0x1e, 0x1e, menu_sel_str[i]);
        }
        y += 0x23;                                                      /* 357 */
    }

    SetASCIIString2(0, 380.0f, 165.0f, 1, 0xff, 0xff, 0xff,             /* 361 */
                    chapter_sel_str[chapter_sel_ctrl.chapter_csr]);
    SetASCIIString2(0, 380.0f, 200.0f, 1, 0xff, 0xff, 0xff,             /* 365 */
                    costume_str[chapter_sel_ctrl.costume_csr]);
    SetASCIIString2(0, 380.0f, 235.0f, 1, 0xff, 0xff, 0xff,             /* 369 */
                    accessory_str[chapter_sel_ctrl.mio_csr]);
    SetASCIIString2(0, 380.0f, 270.0f, 1, 0xff, 0xff, 0xff,             /* 373 */
                    accessory_str[chapter_sel_ctrl.mayu_csr]);
    SetASCIIString2(0, 380.0f, 305.0f, 1, 0xff, 0xff, 0xff,             /* 377 */
                    diff_str[chapter_sel_ctrl.diff_csr]);
    PrintNumber_N((int)chapter_sel_ctrl.clear_csr, 2, 0x17c, 0x154,     /* 380 */
                  0, 0x80, 0, 0, 1);
    SetASCIIString2(0, 96.0f,                                           /* 384 */
                    (float)(chapter_sel_ctrl.menu_csr * 0x23 + 0x82), 1,
                    0xff, 0xff, 0xff, "o");
}
