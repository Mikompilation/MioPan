// FILE: /home/zero_rom/zero2np/src/outgame/logo.c
//
// Boot-logo sequence: the Tecmo and "Project Zero" (ZERO) start-up logos.
//
//   * InitLogo / InitLogoCtrl - clear the texture-buffer pointers; reset the
//                     per-logo playback state (step / mode / cnt).
//   * Get*LogoTexMem  - size the logo's .pk2 file (file 2 = Tecmo, file 3 =
//                     Project Zero) and allocate a buffer from the outgame
//                     load heap; assert if already held.
//   * *LogoTexLoadReq / *LogoTexLoadWait - queue the async file load into that
//                     buffer, and poll for completion.
//   * LogoMain        - per-frame driver: once the texture is resident, DMA it
//                     to VRAM and run a fade-in / hold / fade-out envelope
//                     (mode 0/1/2), where hold can be cut short by any face /
//                     shoulder button; returns 1 when the fade-out completes.
//   * LogoDispMain    - place the logo sprite(s) for the frame at the running
//                     alpha (the Tecmo logo is one sprite; Project Zero is a
//                     logo plus a sub-caption sprite).
//   * Release*LogoTexMem - free the buffer and clear the pointer.
//   * LangSelMain     - language-select driver; a stub in this prototype that
//                     immediately reports "done".
//
// The tex0 fields of logodat[] are packed GS register payloads baked by the
// asset pipeline; they are preserved verbatim from the build.
//
// Reconstructed from the Feb 6 2004 prototype (SLES_523.84).

#include "logo.h"                   // this file's public API

#include "../graphics/graph2d/g2d_draw.h"   // DISP_SPRT / SPRT_DAT / CopySprDToSpr / DispSprD
#include "../graphics/graph2d/tim2.h"       // PK2SendVram
#include "../system/eeiop/cddat.h"          // enum CD_FILE_DAT (LOGO_PK2 / ZERO_LOGO_PK2) + GetFileSize
#include "../system/eeiop/fileload.h"       // FileLoadReqEE / FileLoadIsEnd2
#include "../common/ol_load.h"              // ol_loadGetHeap / ol_loadFreeHeap
#include "../common/variable.h"            // key_now[] - per-button current state

#include "../graphics/graph3d/ctl/fixed_array.h"   // fixed_array<> template (inlined per TU)

#include "../common/utility2.h"            // PRINT_ASSERT + SetAssertPreMessage / PrintAssertReal

// ──────────────────────────────────────────────────────────────────────
// Per-logo playback state.

typedef struct                      /* 0xc */
{
    /* 0x0 */ char step;            // 0 = loading, 1 = playing
    /* 0x4 */ int  cnt;             // frames elapsed in the current mode
    /* 0x8 */ int  mode;            // 0 = fade in, 1 = hold, 2 = fade out
} LOGO_WRK;

// ──────────────────────────────────────────────────────────────────────
// Statics.

static SPRT_DAT logodat[3] =        // data 319a78 : [0] Tecmo, [1]/[2] Project Zero
{
    { 0x20058805e1312bc0, 1,   1, 226,  45, 197, 201, 0, 128, 0, 0 },
    { 0x2005980621312bc0, 1,   1, 195, 138, 229, 151, 0, 128, 0, 0 },
    { 0x2005980621312bc0, 1, 141, 103,  17, 280, 374, 0, 128, 0, 0 },
};

static LOGO_WRK logo_wrk;           // bss  4b4238
static void    *tecmo_tex_addr;     // sbss 3f4d70
static void    *project_tex_addr;   // sbss 3f4d74

// ──────────────────────────────────────────────────────────────────────
// Forward declaration for the file-static per-frame placement helper.

static void LogoDispMain(int logo_label, u_char alpha);

// ──────────────────────────────────────────────────────────────────────
// Module init: drop both texture-buffer pointers.

void InitLogo(void)
{
    tecmo_tex_addr = (void *)0;
    project_tex_addr = (void *)0;
}

// ──────────────────────────────────────────────────────────────────────
// Reset per-logo playback state.

void InitLogoCtrl(void)
{
    logo_wrk.mode = 0;
    logo_wrk.step = 0;
    logo_wrk.cnt = 0;
}

// ──────────────────────────────────────────────────────────────────────
// Allocate the Tecmo logo texture buffer from the outgame load heap.

void GetTecmoLogoTexMem(void)
{
    unsigned int file_size;

    if (tecmo_tex_addr == (void *)0)
    {
        file_size = GetFileSize(LOGO_PK2);
        tecmo_tex_addr = ol_loadGetHeap(file_size);
    }
    else
    {
        PRINT_ASSERT("Error! GetTecmoLogoTexMem", "");
    }
}

// ──────────────────────────────────────────────────────────────────────
// Allocate the Project Zero logo texture buffer from the outgame load heap.

void GetProjectLogoTexMem(void)
{
    unsigned int file_size;

    if (project_tex_addr == (void *)0)
    {
        file_size = GetFileSize(ZERO_LOGO_PK2);
        project_tex_addr = ol_loadGetHeap(file_size);
    }
    else
    {
        PRINT_ASSERT("Error! GetProjectLogoTexMem", "");
    }
}

// ──────────────────────────────────────────────────────────────────────
// Queue the async load of the Tecmo logo texture into its buffer.

void TecmoLogoTexLoadReq(void)
{
    if (tecmo_tex_addr != (void *)0)
    {
        FileLoadReqEE(LOGO_PK2, tecmo_tex_addr, 4, (FILE_LOAD_CALLBACK)0, (void *)0);
        return;
    }

    PRINT_ASSERT("Error! TecmoLogoTexLoadReq", "");
}

// ──────────────────────────────────────────────────────────────────────
// Queue the async load of the Project Zero logo texture into its buffer.

void ProjectLogoTexLoadReq(void)
{
    if (project_tex_addr != (void *)0)
    {
        FileLoadReqEE(ZERO_LOGO_PK2, project_tex_addr, 4, (FILE_LOAD_CALLBACK)0, (void *)0);
        return;
    }

    PRINT_ASSERT("Error! ProjectLogoTexLoadReq", "");
}

// ──────────────────────────────────────────────────────────────────────
// Poll the Tecmo / Project Zero logo texture loads (1 = complete).

int TecmoLogoTexLoadWait(void)
{
    return FileLoadIsEnd2(LOGO_PK2, tecmo_tex_addr) != 0;
}

int ProjectLogoTexLoadWait(void)
{
    return FileLoadIsEnd2(ZERO_LOGO_PK2, project_tex_addr) != 0;
}

// ──────────────────────────────────────────────────────────────────────
// Per-frame logo driver.  logo_label: 0 = Tecmo, 1 = Project Zero.
// step 0 waits for the texture load; step 1 sends the texture to VRAM and
// runs the fade-in (mode 0) / hold (mode 1) / fade-out (mode 2) envelope.
// Hold ends early on any face / shoulder button.  Returns 1 when finished.

int LogoMain(int logo_label, int in_time, int wait_time, int out_time)
{
    u_char alp;

    alp = 0;

    if (logo_wrk.step == 0)
    {
        if (logo_label == 0)
        {
            if (TecmoLogoTexLoadWait() != 0)
            {
                logo_wrk.step = 1;
            }
        }
        else if (logo_label == 1)
        {
            if (ProjectLogoTexLoadWait() == 0)
            {
                return 0;
            }

            logo_wrk.step = 1;
        }
        else
        {
            return 0;
        }
    }
    else if (logo_wrk.step == 1)
    {
        if (logo_label == 0)
        {
            PK2SendVram((uintptr_t)tecmo_tex_addr, -1, -1, 0);
        }
        else if (logo_label == 1)
        {
            PK2SendVram((uintptr_t)project_tex_addr, -1, -1, 0);
        }

        if (logo_wrk.mode == 1)
        {
            logo_wrk.cnt = logo_wrk.cnt + 1;
            alp = 0x80;
            if ((wait_time < logo_wrk.cnt) ||
                (*key_now[9]  != 0) || (*key_now[8]  != 0) ||
                (*key_now[0xb] != 0) || (*key_now[10] != 0) ||
                (*key_now[7]  != 0) || (*key_now[5]  != 0) ||
                (*key_now[6]  != 0) || (*key_now[4]  != 0))
            {
                logo_wrk.cnt = 0;
                logo_wrk.mode = 2;
            }
        }
        else if (logo_wrk.mode == 0)
        {
            alp = (u_char)((logo_wrk.cnt << 7) / in_time);
            logo_wrk.cnt = logo_wrk.cnt + 1;
            if (in_time < logo_wrk.cnt)
            {
                logo_wrk.mode = 1;
                logo_wrk.cnt = 0;
            }
        }
        else if (logo_wrk.mode == 2)
        {
            alp = (u_char)(((out_time - logo_wrk.cnt) * 0x80) / out_time);
            logo_wrk.cnt = logo_wrk.cnt + 1;
            if (out_time < logo_wrk.cnt)
            {
                return 1;
            }
        }

        if ((u_int)logo_wrk.mode > 2)
        {
            return 0;
        }

        LogoDispMain(logo_label, alp);
    }

    return 0;
}

// ──────────────────────────────────────────────────────────────────────
// Free the Tecmo / Project Zero logo texture buffers.

void ReleaseTecmoLogoTexMem(void)
{
    if (tecmo_tex_addr != (void *)0)
    {
        ol_loadFreeHeap(tecmo_tex_addr);
        tecmo_tex_addr = (void *)0;
    }
}

void ReleaseProjectLogoTexMem(void)
{
    if (project_tex_addr != (void *)0)
    {
        ol_loadFreeHeap(project_tex_addr);
        project_tex_addr = (void *)0;
    }
}

// ──────────────────────────────────────────────────────────────────────
// Place the logo sprite(s) for this frame at the running alpha.  The Tecmo
// logo is a single sprite (logodat[0]); Project Zero is the logo (logodat[1])
// plus a sub-caption sprite (logodat[2]).

static void LogoDispMain(int logo_label, u_char alpha)
{
    DISP_SPRT ds;

    if (logo_label == 0)
    {
        CopySprDToSpr(&ds, logodat);
        ds.alpha = alpha;
        DispSprD(&ds);
    }
    else if (logo_label == 1)
    {
        CopySprDToSpr(&ds, logodat + 1);
        ds.alpha = alpha;
        DispSprD(&ds);
        CopySprDToSpr(&ds, logodat + 2);
        ds.alpha = alpha;
        DispSprD(&ds);
    }
}

// ──────────────────────────────────────────────────────────────────────
// Language-select driver.  Stubbed in this prototype build: reports "done".

int LangSelMain(void)
{
    return 1;
}
