#include "snd3d.h"
#include "../main/phasefunc.h"       // GPHASE_ENUM + this module's phase-callback prototypes
#include "../main/gphase.h"
#include "../system/pad/pad.h"
#include "../system/os/system.h"
#include "../graphics/graph2d/draw_cmn.h"
#include "../graphics/graph2d/message.h"
#include "../common/utility2.h"

static char pad_check_step;

static void PadCheckInit(void)
{
    pad_check_step = 0;
}

static void PadCheckMainPad(void)
{
    if (**paddat == 1)
    {
        SystemBankPlay(3, 1, 0, 0, (SND_3D_SET *)0, 0x3200, 0x1000);
        SetNextGPhase(GID_LANGDATA_CHECK);
    }
}

static void PadCheckMain(void)
{
    if (pad_check_step == 0)
    {
        if (padIsConnected(0) == 0)
        {
            pad_check_step = 1;
        }
        else if (GetPadStateStable(0) != 0)
        {
            if (GetPadDUALSHOCK2(0) != 0)
            {
                SetNextGPhase(GID_LANGDATA_CHECK);
                printf("pass1\n");
                return;
            }
            pad_check_step = 1;
            printf("pass2\n");
        }
    }
    else if (pad_check_step == 1)
    {
        if (padIsConnected(0) != 0 && GetPadDUALSHOCK2(0) != 0)
        {
            PadCheckMainPad();
        }
    }
    else
    {
        PRINT_ASSERT("Error! %s", __FUNCTION__);
    }
}

static void PadCheckDispMain(void)
{
    if (pad_check_step == 1)
    {
        DrawCmnTwoLineWindow(0, 45.0f, 126.0f, 550.0f, 216.0f, 0x80, 0x80);
        PrintMsg(0x41, 0x35, 0x5c, 0x8e, 1, 0x80, 0);
    }
}

void init_Boot_PadCheck(void)
{
    PadCheckInit();
}

GPHASE_ENUM one_Boot_PadCheck(GPHASE_ENUM dummy)
{
    (void)dummy;
    PadCheckMain();
    PadCheckDispMain();
    return GPHASE_CONTINUE;
}

void end_Boot_PadCheck(void)
{
}
