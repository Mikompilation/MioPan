/* ==========================================================================
 *  sdk/libpad.cpp  (SCE PS2 EE <libpad> -- PC-port shim)
 *
 *  Translates the DualShock2 report format expected by the game into queries
 *  against the miopan input wrapper (miopan/io/miopan_input), which owns the
 *  host devices (SDL gamepad + keyboard).  This file keeps only the PS2 report
 *  packing (button bit layout, rdata byte offsets); all SDL/device handling
 *  lives in the wrapper.  system/pad/pad.c owns the game-side counters/key maps.
 * ======================================================================== */

#include "libpad.h"

#include "../miopan/io/miopan_input.h"

#define MIOPAN_PAD_LOGICAL_COUNT MIOPAN_PAD_BUTTON_COUNT
#define MIOPAN_STICK_COUNT       MIOPAN_PAD_STICK_COUNT

// FF2-internal pad bit per logical index (matches miopan_input's index order
// after PadReadFunc byte-swaps rdata[2]/rdata[3]).
static const u_short s_button_bits[MIOPAN_PAD_LOGICAL_COUNT] = {
    0x1000, 0x4000, 0x8000, 0x2000,
    0x0010, 0x0040, 0x0080, 0x0020,
    0x0004, 0x0001, 0x0008, 0x0002,
    0x0800, 0x0100, 0x0200, 0x0400,
};

// rdata byte each stick axis is written to (left X/Y, right X/Y).
static const int s_stick_rdata_byte[MIOPAN_STICK_COUNT] = { 6, 7, 4, 5 };

extern "C" {

int scePadInit(int mode)
{
    (void)mode;
    return MioPan_InputInit();
}

int scePadEnd(void)
{
    MioPan_InputShutdown();
    return 1;
}

int scePadPortOpen(int port, int slot, void *addr)
{
    (void)slot;
    (void)addr;

    if (port != 0)
    {
        return 0;
    }

    MioPan_InputUpdate();
    return 1;
}

int scePadPortClose(int port, int slot)
{
    (void)slot;

    if (port == 0)
    {
        MioPan_InputShutdown();
    }

    return 1;
}

int scePadGetState(int port, int slot)
{
    (void)slot;

    if (port != 0)
    {
        return scePadStateDiscon;
    }

    MioPan_InputInit();
    return scePadStateStable;
}

int scePadRead(int port, int slot, unsigned char *rdata)
{
    u_short raw_buttons;
    int i;

    (void)slot;

    if (port != 0 || rdata == 0 || MioPan_InputInit() == 0)
    {
        return 0;
    }

    MioPan_InputUpdate();

    for (i = 1; i < 32; i++)
    {
        rdata[i] = 0xff;
    }

    rdata[0] = 0;
    rdata[1] = 0x79;
    raw_buttons = 0xffff;

    for (i = 0; i < MIOPAN_PAD_LOGICAL_COUNT; i++)
    {
        if (MioPan_InputButtonHeld(i))
        {
            raw_buttons ^= s_button_bits[i];
        }
    }

    rdata[2] = (u_char)((raw_buttons >> 8) & 0x00ff);
    rdata[3] = (u_char)(raw_buttons & 0x00ff);

    for (i = 0; i < MIOPAN_STICK_COUNT; i++)
    {
        rdata[s_stick_rdata_byte[i]] = MioPan_InputStickValue(i);
    }

    return 1;
}

int scePadInfoMode(int port, int slot, int term, int offs)
{
    (void)slot;
    (void)offs;

    if (port != 0)
    {
        return 0;
    }

    if (term == InfoModeCurID || term == InfoModeCurExID)
    {
        return 7;
    }

    return 0;
}

int scePadGetReqState(int port, int slot)
{
    (void)port;
    (void)slot;
    return scePadReqStateComplete;
}

int scePadInfoAct(int port, int slot, int actno, int term)
{
    (void)slot;
    (void)term;

    if (port != 0)
    {
        return 0;
    }

    if (actno < 0)
    {
        return 2;
    }

    return actno < 2 ? 1 : 0;
}

int scePadInfoComb(int port, int slot, int listno, int offs)
{
    (void)port;
    (void)slot;
    (void)listno;
    (void)offs;
    return 0;
}

int scePadSetMainMode(int port, int slot, int offs, int lock)
{
    (void)slot;
    (void)offs;
    (void)lock;
    return port == 0 ? 1 : 0;
}

int scePadSetActDirect(int port, int slot, const unsigned char *data)
{
    (void)slot;

    if (port != 0 || data == 0)
    {
        return 0;
    }

    // data[0] = large (on/off) motor, data[1] = small motor strength.
    return MioPan_InputRumble(data[0], data[1]);
}

int scePadSetActAlign(int port, int slot, const unsigned char *data)
{
    (void)slot;
    (void)data;
    return port == 0 ? 1 : 0;
}

int scePadInfoPressMode(int port, int slot)
{
    return scePadReqStateComplete;
}

int scePadEnterPressMode(int port, int slot)
{
    (void)slot;
    return port == 0 ? 1 : 0;
}

int scePadExitPressMode(int port, int slot)
{
    (void)slot;
    return port == 0 ? 1 : 0;
}

int scePadGetSlotMax(int port)
{
    return port == 0 ? 1 : 0;
}

int scePadSetWarningLevel(int level)
{
    (void)level;
    return 1;
}

}
