/* ==========================================================================
 *  system/mc/prg/mc_check_card.c
 *
 *  Card presence and capacity (mc_check_card.o, .text 0x1df418, 0x35c bytes).
 *
 *  Two things worth knowing before touching MemoryCardGetCardInfoMain():
 *
 *  - `unformat_flg` makes an unformatted card take *two* passes to report.
 *    The first time the query comes back "no format" (or with the format flag
 *    down) the flag goes up and the step machine is reset to 0, so the query
 *    runs again; only the second time does it answer -2.  A card being
 *    inserted reads as unformatted for a moment, and this is what keeps that
 *    from raising the format prompt.
 *
 *  - The busy path here does not spend a retry, unlike mc_open / mc_make_dir /
 *    mc_format.  mc_check_dir.c and mc_del_file.c share that asymmetry.
 *
 *  Reconstructed from the Feb 6 2004 prototype (SLES_523.84).
 * ======================================================================== */

#include "mc_check_card.h"

#include "libmc.h"
#include "mc_cmn.h"                             /* MemoryCardExeEndSync     */
#include "mc_set_data.h"                        /* MemoryCardAssert         */

#include <string.h>

typedef struct                                  /* 0x14 */
{
    /* 0x00 */ int port;
    /* 0x04 */ int slot;
    /* 0x08 */ int type;
    /* 0x0c */ int free;
    /* 0x10 */ int format;
} MC_INFO;

typedef struct                                  /* 0xc */
{
    /* 0x0 */ char step;
    /* 0x1 */ char retry_cnt;
    /* 0x2 */ char unformat_flg;
    /* 0x4 */ int  port;
    /* 0x8 */ int  slot;
} MC_CHECK_CARD_CTRL;

typedef struct                                  /* 0x8 */
{
    /* 0x0 */ int port;
    /* 0x4 */ int slot;
} MC_CHECK_CARD_EVERY_FRAME;

static MC_INFO                   mc_info;                   /* bss 4b4750  */
static MC_CHECK_CARD_CTRL        mc_check_card_ctrl;         /* bss 4b4768  */
static MC_CHECK_CARD_EVERY_FRAME mc_check_card_every_frame;  /* sbss 3f4da0 */

void MemoryCardInfoCtrlInit(void)                                       /* 84 */
{
    memset(&mc_info, 0, sizeof(mc_info));                               /* 87 */

    mc_info.port = -1;                                                  /* 89 */
    mc_info.slot = -1;                                                  /* 90 */
}

void MemoryCardGetCardInfoInit(int port, int slot)
{
    mc_check_card_ctrl.step         = 0;                                /* 103 */
    mc_check_card_ctrl.retry_cnt    = 4;                                /* 104 */
    mc_check_card_ctrl.unformat_flg = 0;                                /* 105 */
    mc_check_card_ctrl.port         = port;                             /* 106 */
    mc_check_card_ctrl.slot         = slot;                             /* 107 */
}

void MemoryCardCheckEveryFrameInit(int port, int slot)                  /* 117 */
{
    mc_check_card_every_frame.port = port;                              /* 120 */
    mc_check_card_every_frame.slot = slot;                              /* 121 */

    MemoryCardGetCardInfoInit(port, slot);                              /* 123 */
}

int MemoryCardGetCardInfoMain(void)                                     /* 143 */
{
    int res;
    int mc_res;
    int result;

    result = 0;                                                        /* 151 */
    res    = 0;

    if (mc_check_card_ctrl.step == 0)                                   /* 155 */
    {
        mc_res = MemoryCardGetCardInfoReq(mc_check_card_ctrl.port,        /* 156 */
                                          mc_check_card_ctrl.slot);

        if (mc_res == 0)                                                /* 159 */
        {
            mc_check_card_ctrl.step = 1;                                /* 160 */
        }
        else if (mc_check_card_ctrl.retry_cnt > 0)                       /* 165 */
        {
            if (mc_res == -100)                                         /* 167 */
            {
                MemoryCardAssert("Error! sceMcInit forgets");            /* 169 */
            }
            else if (mc_res == -210)                                    /* 172 */
            {
                MemoryCardAssert("PATH Error! MemoryCardGetCardInfoMain"); /* 174 */
            }
            else if (mc_res == -200)                                    /* 177 */
            {
                mc_check_card_ctrl.step = 2;                             /* 179 */
            }
            else
            {
                mc_check_card_ctrl.retry_cnt--;                           /* 184 */
            }
        }
        else
        {
            res = -10;                                                   /* 189 */
        }
    }

    if (mc_check_card_ctrl.step == 1)                                    /* 195 */
    {
        mc_res = MemoryCardExeEndSync(&result);                          /* 196 */

        if (mc_res == 1)                                                 /* 199 */
        {
            if (GetAccessMemoryCardType() != 2)                          /* 201 */
            {
                /* A PS1 card or a PDA.  Nothing here can use it. */
                res = -20;
            }
            else if (GetAccessMemoryCardFormat() == 1)                    /* 203 */
            {
                if (result == 0)                                         /* 205 */
                {
                    res = 1;                                             /* 206 */
                }
                else
                {
                    switch (result)                                      /* 210 */
                    {
                    case -2:
                        if (mc_check_card_ctrl.unformat_flg > 0)          /* 213 */
                        {
                            res = -2;
                        }
                        else
                        {
                            /* First look -- try once more before believing it. */
                            mc_check_card_ctrl.unformat_flg = 1;          /* 223 */
                            mc_check_card_ctrl.step         = 0;
                        }
                        break;

                    case -1:
                    case -5:
                        res = result;
                        break;

                    default:
                        res = -20;
                        break;
                    }
                }
            }
            else if (mc_check_card_ctrl.unformat_flg > 0)                 /* 236 */
            {
                res = -2;                                                /* 237 */
            }
            else
            {
                mc_check_card_ctrl.unformat_flg = 1;
                mc_check_card_ctrl.step         = 0;
            }
        }
        else if (mc_res == -1)                                           /* 254 */
        {
            mc_check_card_ctrl.step = 0;                                 /* 256 */
        }
    }

    if (mc_check_card_ctrl.step == 2)                                    /* 261 */
    {
        if (MemoryCardExeEndSync(&result) != 0)                          /* 262 */
        {
            mc_check_card_ctrl.step = 0;                                 /* 265 */
        }
    }

    return res;                                                          /* 272 */
}

int MemoryCardGetCardInfoReq(int port, int slot)                        /* 282 */
{
    mc_info.port = port;                                                /* 289 */
    mc_info.slot = slot;                                                /* 290 */

    return sceMcGetInfo(port, slot, &mc_info.type, &mc_info.free,        /* 295 */
                        &mc_info.format);
}

/* --------------------------------------------------------------------------
 *  MemoryCardCheckEveryFrameMain
 *
 *  Re-arms the query as soon as it completes, so a screen that polls this every
 *  frame notices a card being pulled or swapped.  1 means "the query finished
 *  and has been re-armed"; anything else non-negative is folded to 0 so the
 *  caller's step machine is left alone.
 * ------------------------------------------------------------------------ */
int MemoryCardCheckEveryFrameMain(void)                                 /* 298 */
{
    int res;

    res = MemoryCardGetCardInfoMain();                                  /* 323 */

    if (res == 1)                                                       /* 326 */
    {
        MemoryCardGetCardInfoInit(mc_check_card_every_frame.port,        /* 327 */
                                  mc_check_card_every_frame.slot);
        res = 1;                                                        /* 328 */
    }
    else if (res >= 0)                                                  /* 331 */
    {
        res = 0;
    }

    return res;                                                         /* 336 */
}

int GetAccessMemoryCardPort(void)                                       /* 352 */
{
    int port;

    port = mc_info.port;

    if (port < 0 || port > 1)                                           /* 354 */
    {
        port = -1;
    }

    return port;                                                        /* 362 */
}

int GetAccessMemoryCardSlot(void)                                       /* 373 */
{
    int slot;

    slot = mc_info.slot;

    if (slot < 0 || slot > 1)                                           /* 375 */
    {
        slot = -1;
    }

    return slot;                                                        /* 383 */
}

int GetAccessMemoryCardType(void)
{
    return mc_info.type;                                                /* 394 */
}

int GetAccessMemoryCardFreeCluster(void)
{
    return mc_info.free;                                                /* 405 */
}

int GetAccessMemoryCardFormat(void)
{
    return mc_info.format;                                              /* 416 */
}

void MemoryCardSetAccessPort(int port)
{
    mc_info.port = port;                                                /* 427 */
}
