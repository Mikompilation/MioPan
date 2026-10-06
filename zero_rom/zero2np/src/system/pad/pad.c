// FILE: /home/zero_rom/zero2np/src/system/pad/pad.c
//
// Controller pad subsystem.  The host build feeds the same button counters,
// action tables, and key_now/key_bak mapping that the reconstructed game code
// expects, while the raw controller report comes from the SDK scePad shim.
//
// Reconstructed from the Feb 6 2004 prototype (SLES_523.84).

#include "pad.h"

#include "../../common/variable.h"
#include "../../sdk/libpad.h"

#include <mathf.h>

u_short sce_pad[16] = {
    0x1000, 0x4000, 0x8000, 0x2000,
    0x0010, 0x0040, 0x0080, 0x0020,
    0x0004, 0x0001, 0x0008, 0x0002,
    0x0800, 0x0100, 0x0200, 0x0400,
};

typedef struct
{
    u_char rpt_flg[4];
    int    analog_timer[4];
} ANALOG_PAD_CTRL;

ANALOG_PAD_CTRL analog_pad_ctrl;

u_short **paddat = paddat_m[0];
u_char  **pushdat = pushdat_m[0];

#define PAD_ANALOG_CENTER    0x80
#define PAD_ANALOG_THRESHOLD 60

static int PadAbs(int x)
{
    return x < 0 ? -x : x;
}

int InitPad(void)
{
    PAD_STRUCT *psp;
    int i;
    int j;
    int loop;
    int port_slot[2][2] = {
        { 0, 0 },
        { 1, 0 },
    };

    scePadInit(0);

    for (i = 0, psp = pad; i < 2; i++, psp++)
    {
        psp->port = port_slot[i][0];
        psp->slot = port_slot[i][1];
        psp->now = 0;
        psp->old = 0;
        psp->one = 0;
        psp->rpt = 0;
        psp->rpt_time = 0;
        psp->flags = 2;
        psp->step = 99;
        psp->id = 0x79;

        for (loop = 0; loop < 6; loop++)
        {
            psp->pad_direct[loop] = 0;
        }

        scePadPortOpen(psp->port, psp->slot, psp->pad_dma_buf);
    }

    for (i = 0; i < 2; i++)
    {
        PadClearCount(i);

        for (j = 0; j < 16; j++)
        {
            pad[i].cnt[j] = 0;
            pad[i].cnt_bak[j] = 0;
        }
    }

    return 0;
}

int padIsConnected(int iId)
{
    if (iId < 0 || iId >= 2)
    {
        return 0;
    }

    return (pad[iId].flags & 1) != 0;
}

int GetPadDUALSHOCK2(int iId)
{
    if (iId < 0 || iId >= 2)
    {
        return 0;
    }

    return pad[iId].id == 0x79 ? 1 : 0;
}

int GetPadStateStable(int iId)
{
    int state;

    if (iId < 0 || iId >= 2)
    {
        return 0;
    }

    state = scePadGetState(pad[iId].port, pad[iId].slot);
    return state == scePadStateStable || state == scePadStateFindCTP1;
}

int PadSyncCallback(void)
{
    int state;
    int p_id;
    int pad_type;
    PAD_STRUCT *psp;

    pad_type = opt_wrk.pad_type;
    if (pad_type < 0 || pad_type >= 3)
    {
        pad_type = 0;
    }
    paddat = paddat_m[pad_type];
    pushdat = pushdat_m[pad_type];

    for (psp = pad, p_id = 0; p_id < 2; psp++, p_id++)
    {
        state = scePadGetState(psp->port, psp->slot);

        if (state == scePadStateDiscon)
        {
            psp->step = 0;
            psp->flags &= (char)0xfe;
            PadClearCount(p_id);
            continue;
        }

        psp->flags |= 1;

        if (state == scePadStateStable || state == scePadStateFindCTP1)
        {
            psp->step = 99;
            PadReadFunc(psp, p_id);

            if ((psp->pad_direct[0] & 0x80) != 0)
            {
                psp->pad_direct[0] &= 1;
                scePadSetActDirect(psp->port, psp->slot, psp->pad_direct);
            }
        }
    }

    return 0;
}

int PadReadFunc(PAD_STRUCT *psp, int p_id)
{
    char r_data[32];
    int i;
    int j;

    if (psp == 0 || p_id < 0 || p_id >= 2)
    {
        return 0;
    }

    psp->old = psp->now;

    if (scePadRead(psp->port, psp->slot, (u_char *)r_data) == 0)
    {
        psp->now = 0;
        PadClearCount(p_id);
        return 0;
    }

    if (r_data[0] != 0)
    {
        psp->now = 0;
        PadClearCount(p_id);
        return 0;
    }

    if (psp->id != 0 && psp->id != r_data[1])
    {
        psp->step = 0;
        return 0;
    }

    psp->now = 0xffff ^ ((u_short)(((u_char)r_data[2] << 8) | (u_char)r_data[3]));
    psp->one = psp->now & (psp->now ^ psp->old);
    psp->rpt = psp->one;
    psp->id = r_data[1];

    if (psp->now == psp->old)
    {
        psp->rpt_time++;
        if (psp->rpt_time > 9)
        {
            psp->rpt = psp->now;
            psp->rpt_time = 5;
        }
    }
    else
    {
        psp->rpt_time = 0;
    }

    for (i = 0; i < 16; i++)
    {
        pad[p_id].cnt_bak[i] = pad[p_id].cnt[i];

        if ((sce_pad[i] & psp->now) != 0)
        {
            if (pad[p_id].cnt[i] != 0xffff)
            {
                pad[p_id].cnt[i]++;
            }
        }
        else
        {
            pad[p_id].cnt[i] = 0;
        }
    }

    if ((psp->id & 0xf0) == 0x70)
    {
        for (j = 0; j < 4; j++)
        {
            psp->analog[j] = (u_char)r_data[j + 4];
        }
    }

    if (psp->id == 0x79)
    {
        for (j = 0; j < 12; j++)
        {
            psp->push[j] = (u_char)r_data[j + 8];
        }

        SetAnlgInfo(psp, p_id);
    }
    return 0;
}

void SetAnlgInfo(PAD_STRUCT *psp, int p_id)
{
    float rot;
    short int pad_x;
    short int pad_y;
    u_char i;
    u_char dir_old;

    if (psp == 0 || p_id < 0 || p_id >= 2)
    {
        return;
    }

    for (i = 0; i < 2; i++)
    {
        dir_old = psp->an_dir[i];
        psp->an_cnt_bak[i] = psp->an_cnt[i];
        psp->an_dir_bak[i] = psp->an_dir[i];
        psp->an_rot_bak[i] = psp->an_rot[i];

        if (i == 0)
        {
            pad_y = (short int)(pad[p_id].analog[2] - PAD_ANALOG_CENTER);
            pad_x = (short int)(PAD_ANALOG_CENTER - pad[p_id].analog[3]);
        }
        else
        {
            pad_y = (short int)(pad[p_id].analog[0] - PAD_ANALOG_CENTER);
            pad_x = (short int)(PAD_ANALOG_CENTER - pad[p_id].analog[1]);
        }

        if (PadAbs(pad_y) >= PAD_ANALOG_THRESHOLD ||
            PadAbs(pad_x) >= PAD_ANALOG_THRESHOLD)
        {
            rot = atan2f((float)pad_y, (float)pad_x);
            psp->an_dir[i] = (u_char)(((int)((rot + 3.1415927f + 0.39269909f) /
                                             0.78539819f) % 8) + 4);

            if (psp->an_dir[i] > 7)
            {
                psp->an_dir[i] -= 8;
            }

            psp->an_rot[i] = rot;

            if (psp->an_dir[i] == dir_old)
            {
                psp->an_cnt[i]++;
            }
            else
            {
                psp->an_cnt[i] = 0;
            }
        }
        else
        {
            psp->an_dir[i] = 0xff;
            psp->an_cnt[i] = 0;
        }
    }
}

u_short VibrateRequest(u_short p_id, u_short act1, u_short act2)
{
    PAD_STRUCT *psp;

    if (p_id >= 2)
    {
        return 0;
    }

    psp = &pad[p_id];
    psp->pad_direct[0] = (u_char)(act1 | 0x80);
    psp->pad_direct[1] = (u_char)act2;
    return 0;
}

u_short VibrateRequest1(u_short p_id, u_short act_1)
{
    PAD_STRUCT *psp;

    if (p_id >= 2)
    {
        return 0;
    }

    psp = &pad[p_id];
    psp->pad_direct[0] = (u_char)(act_1 | 0x80);
    return 0;
}

u_short VibrateRequest2(u_short p_id, u_short act_2)
{
    PAD_STRUCT *psp;

    if (p_id >= 2)
    {
        return 0;
    }

    psp = &pad[p_id];
    psp->pad_direct[1] = (u_char)act_2;
    psp->pad_direct[0] = 0x80;
    return 0;
}

void PadClearCount(int p_id)
{
    u_int i;

    if (p_id < 0 || p_id >= 2)
    {
        return;
    }

    pad[p_id].now = 0;
    pad[p_id].old = 0;
    pad[p_id].one = 0;
    pad[p_id].rpt = 0;
    pad[p_id].rpt_time = 0;

    for (i = 0; i < 16; i++)
    {
        pad[p_id].cnt_bak[i] = 0;
        pad[p_id].cnt[i] = 0;
    }

    for (i = 0; i < 2; i++)
    {
        pad[p_id].an_cnt_bak[i] = 0;
        pad[p_id].an_cnt[i] = 0;
        pad[p_id].an_dir_bak[i] = 0xff;
        pad[p_id].an_dir[i] = 0xff;
        pad[p_id].an_rot_bak[i] = 0.0f;
        pad[p_id].an_rot[i] = 0.0f;
    }

    for (i = 0; i < 4; i++)
    {
        pad[p_id].analog[i] = PAD_ANALOG_CENTER;
    }
}

void PadAnalogInit(void)
{
    int i;

    for (i = 0; i < 4; i++)
    {
        analog_pad_ctrl.rpt_flg[i] = 0;
        analog_pad_ctrl.analog_timer[i] = 0;
    }
}

static void PadAnalogRptCtrl(int pad_label)
{
    int timer;

    if (pad_label < 0 || pad_label >= 4)
    {
        return;
    }

    timer = analog_pad_ctrl.analog_timer[pad_label];
    analog_pad_ctrl.rpt_flg[pad_label] =
        (timer == 0 || timer == 10 || (timer >= 11 && (timer % 5) == 0)) ? 1 : 0;
}

void PadAnalogMain(void)
{
    if ((pad[0].now & 0xf000) != 0)
    {
        PadAnalogInit();
        return;
    }

    if (pad[0].id == 0x79 && pad[0].analog[3] < 0x3b)
    {
        PadAnalogRptCtrl(0);
        analog_pad_ctrl.analog_timer[0]++;
    }
    else
    {
        analog_pad_ctrl.rpt_flg[0] = 0;
        analog_pad_ctrl.analog_timer[0] = 0;
    }

    if (pad[0].id == 0x79 && pad[0].analog[3] > 0xbb)
    {
        PadAnalogRptCtrl(1);
        analog_pad_ctrl.analog_timer[1]++;
    }
    else
    {
        analog_pad_ctrl.rpt_flg[1] = 0;
        analog_pad_ctrl.analog_timer[1] = 0;
    }

    if (pad[0].id == 0x79 && pad[0].analog[2] < 0x3b)
    {
        PadAnalogRptCtrl(2);
        analog_pad_ctrl.analog_timer[2]++;
    }
    else
    {
        analog_pad_ctrl.rpt_flg[2] = 0;
        analog_pad_ctrl.analog_timer[2] = 0;
    }

    if (pad[0].id == 0x79 && pad[0].analog[2] > 0xbb)
    {
        PadAnalogRptCtrl(3);
        analog_pad_ctrl.analog_timer[3]++;
    }
    else
    {
        analog_pad_ctrl.rpt_flg[3] = 0;
        analog_pad_ctrl.analog_timer[3] = 0;
    }
}

int GetPadAnalogRpt(int pad_label)
{
    if (pad_label < 0 || pad_label >= 4)
    {
        return 0;
    }

    return analog_pad_ctrl.rpt_flg[pad_label];
}
