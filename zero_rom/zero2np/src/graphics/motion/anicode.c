/* ==========================================================================
 *  graphics/motion/anicode.c
 *
 *  ANI_CODE command reader.  Commands are 16-bit words: the high nibble is
 *  the command class and bits 10-11 select one of three argument layouts.
 *
 *  Reconstructed from the Feb 6 2004 prototype (SLES_523.84).
 * ======================================================================== */

#include "anicode.h"

#include "motion.h"                          /* GetPlyrFootPos / motGetNowFrame */
#include "mim.h"

#include "../../common/packfile.h"
#include "../../ingame/map/foot_se.h"
#include "../../ingame/map/map_rectangle.h"  /* MrecSetSEInfo / MrecGetSeNo   */
#include "../../ingame/ingame_effect.h"      /* IgEffectPlayerDustReq -- was
                                              * stubbed here, belongs to
                                              * ingame_effect.o              */

static int motAniIsSeNoDustEffect(int se_no)
{
    return se_no == 3 || se_no == 0x0d;
}

void motAniCodeClearBuf(ANI_CTRL *ani_ctrl)
{
    u_int i;

    for (i = 0; i < 10; i++)
    {
        ani_ctrl->anm.buf[i].stat = 0;
        ani_ctrl->anm.buf[i].cnt = 0;
    }
}

void motAniCodeSetBuf(ANI_CTRL *ani_ctrl, ANI_CODE code)
{
    u_int i;

    ani_ctrl->anm.code_now++;

    for (i = 0; i < 10; i++)
    {
        if (ani_ctrl->anm.buf[i].stat == 0)
        {
            ani_ctrl->anm.buf[i].code = code;
            ani_ctrl->anm.buf[i].stat = 1;
            ani_ctrl->anm.buf[i].cnt = (u_short)ani_ctrl->anm.timer;
            return;
        }
    }

    printf("Warning : anicode buffer over !!!\n");
}

u_char motAniCodeRead(ANI_CTRL *ani_ctrl)
{
    ANI_CODE code;
    int args[4];

    ani_ctrl->anm.stat = 0;

    do
    {
        code = *ani_ctrl->anm.code_now;
        GetAniCodeArgs(code, args);
        motAniCodeExec(ani_ctrl, code, args);
    }
    while (ani_ctrl->anm.stat == 0);

    return ani_ctrl->anm.stat == 1;
}

void motAniCodeExec(ANI_CTRL *ani_ctrl, ANI_CODE code, int *args)
{
    switch (code >> 12)
    {
    case 0:
        motAniCodeReadCTRL(ani_ctrl, args);
        break;
    case 1:
        motAniCodeReadTIMER(ani_ctrl, args);
        break;
    case 2:
        motAniCodeReadMOT(ani_ctrl, args);
        break;
    default:
        motAniCodeSetBuf(ani_ctrl, code);
        break;
    }
}

void motAniTimerCodeExec(ANI_CTRL *ani_ctrl)
{
    u_int i;
    ANI_CODE code;
    int args[3];

    for (i = 0; i < 10; i++)
    {
        if (ani_ctrl->anm.buf[i].stat == 0)
        {
            continue;
        }

        if (ani_ctrl->anm.buf[i].cnt <= motGetNowFrame(&ani_ctrl->mot))
        {
            code = ani_ctrl->anm.buf[i].code;
            GetAniCodeArgs(code, args);

            switch (code >> 12)
            {
            case 3:
                motAniCodeReadMIM(ani_ctrl, args);
                break;
            case 4:
                motAniCodeReadSE(ani_ctrl, args);
                break;
            case 6:
                motAniCodeReadEFCT(ani_ctrl, args);
                break;
            }

            ani_ctrl->anm.buf[i].stat = 0;
            ani_ctrl->anm.code_now--;
        }
    }
}

int motAniCodeIsEnd(ANI_CODE code)
{
    int args[3];

    GetAniCodeArgs(code, args);
    return (code >> 12) == 0 && args[0] == 0;
}

void GetAniCodeArgs(ANI_CODE code, int *args)
{
    switch ((code >> 10) & 3)
    {
    case 0:
        args[0] = code & 0x3ff;
        args[1] = 0;
        args[2] = 0;
        break;
    case 1:
        args[0] = (code >> 3) & 0x7f;
        args[1] = code & 7;
        args[2] = 0;
        break;
    case 2:
        args[0] = (code >> 6) & 0x0f;
        args[1] = (code >> 3) & 7;
        args[2] = code & 7;
        break;
    }
}

int motAniCodeReadCTRL(ANI_CTRL *ani_ctrl, int *args)
{
    ANI_CODE *next;

    switch (args[0])
    {
    case 0:
        ani_ctrl->anm.stat = 1;
        return 0;

    case 1:
        if (ani_ctrl->anm.loop_rest == 0)
        {
            next = ani_ctrl->anm.code_now + 1;
        }
        else if (ani_ctrl->anm.loop_rest == -1)
        {
            next = ani_ctrl->anm.loop_start;
        }
        else
        {
            ani_ctrl->anm.loop_rest--;
            next = ani_ctrl->anm.loop_start;
        }
        ani_ctrl->anm.code_now = next;
        return 1;

    case 2:
        ani_ctrl->anm.loop_rest = (args[1] == 0) ? -1 : args[1];
        ani_ctrl->anm.timer = 0;
        ani_ctrl->anm.loop_start = ani_ctrl->anm.code_now + 1;
        ani_ctrl->anm.code_now = ani_ctrl->anm.loop_start;
        return 0;

    default:
        printf("Warning : Wrong ANI_LOOP\n");
        break;
    }

    return 0;
}

void motAniCodeReadTIMER(ANI_CTRL *ani_ctrl, int *args)
{
    ani_ctrl->anm.timer = args[0];
    ani_ctrl->anm.code_now++;
}

void motAniCodeReadMOT(ANI_CTRL *ani_ctrl, int *args)
{
    ani_ctrl->mot.play_id = (u_int)args[0];
    ani_ctrl->mot.dat =
        (u_int *)GetFileInPak(ani_ctrl->mot.top, ani_ctrl->mot.play_id);
    if (ani_ctrl->mtop != (u_int *)0)
    {
        ani_ctrl->mdat =
            (u_int *)GetFileInPak(ani_ctrl->mtop, ani_ctrl->mot.play_id);
    }

    ani_ctrl->interp_flg = 0;
    ani_ctrl->ftype = (u_short)args[1];
    ani_ctrl->anm.code_now++;
    ani_ctrl->anm.stat = 2;
}

void motAniCodeReadMIM(ANI_CTRL *ani_ctrl, int *args)
{
    if (ani_ctrl->mim != (MIME_CTRL *)0)
    {
        switch (args[1])
        {
        case 0:
            mimRequestNum(ani_ctrl, args[0], 0);
            break;
        case 1:
            mimRequestNum(ani_ctrl, args[0], 1);
            break;
        case 2:
            mimLoopRequestNum(ani_ctrl, args[0], 0);
            break;
        case 3:
            mimLoopRequestNum(ani_ctrl, args[0], 1);
            break;
        }
    }

    ani_ctrl->anm.code_now++;
}

void motAniCodeReadSE(ANI_CTRL *ani_ctrl, int *args)
{
    float pos[4];
    u_char lr;

    switch (args[0])
    {
    case 0:
    case 2:
        lr = 0;
        break;
    case 1:
    case 3:
        lr = 1;
        break;
    default:
        return;
    }

    GetPlyrFootPos(pos, ani_ctrl, lr);
    ani_ctrl->anm.code_now++;

    if (args[0] < 2)
    {
        foot_sePlay(pos, 0x2300, 0x0f3c);
    }
    else
    {
        foot_sePlay(pos, 0x3200, 0x1000);
    }
}

void motAniCodeReadEFCT(ANI_CTRL *ani_ctrl, int *args)
{
    float pos[4];
    int se_no;

    if (args[0] == 0)
    {
        GetPlyrFootPos(pos, ani_ctrl, 0);
        MrecSetSEInfo(pos);
        se_no = MrecGetSeNo();
        if (motAniIsSeNoDustEffect(se_no))
        {
            IgEffectPlayerDustReq(ani_ctrl, 5);
        }
    }
    else if (args[0] == 1)
    {
        GetPlyrFootPos(pos, ani_ctrl, 1);
        MrecSetSEInfo(pos);
        se_no = MrecGetSeNo();
        if (motAniIsSeNoDustEffect(se_no))
        {
            IgEffectPlayerDustReq(ani_ctrl, 0x10);
        }
    }

    ani_ctrl->anm.code_now++;
}

u_char motGetNextMotion(ANI_CTRL *ani_ctrl)
{
    ANI_CODE code;
    int args[3];
    int loop_end;

    loop_end = 0;
    ani_ctrl->anm.stat = 0;

    for (;;)
    {
        code = *ani_ctrl->anm.code_now;
        GetAniCodeArgs(code, args);

        if ((code >> 12) == 0)
        {
            loop_end = motAniCodeReadCTRL(ani_ctrl, args);
        }
        else if ((code >> 12) == 2)
        {
            motAniCodeReadMOT(ani_ctrl, args);
        }
        else
        {
            ani_ctrl->anm.code_now++;
        }

        if (ani_ctrl->anm.stat != 0)
        {
            if (ani_ctrl->anm.stat == 1)
            {
                return 1;
            }
            if (loop_end != 0)
            {
                return 2;
            }
            return 0;
        }
    }
}
