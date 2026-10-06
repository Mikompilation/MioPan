// FILE: /home/zero_rom/zero2np/src/ingame/photo/finder.c
//
// The Camera Obscura's viewfinder overlay.
//
// Three kinds of thing live here, and they barely interact:
//
//   * The fades.  info_wrk carries a little state machine per element -- the
//     overlay itself, the health bar, the battle tint, the shutter-chance
//     flash, the post-shot score line -- each of the same shape: 0 out,
//     1 fading in, 2 held, 3 fading out, with a counter inside it.  The Main()
//     functions step them and nothing else.
//   * The charge gauge.  Twelve pips, each with its own copy of that state
//     machine, armed one per charge level as the shot builds.
//   * The sprite helpers.  DispChara / DispCharaRGB / PutSpriteYW /
//     SetNumerousDisp are how the whole photo module puts anything on screen:
//     they copy a record out of n_finder_dat[] into a DISP_SPRT, override the
//     position and blend, and hand it to DispSprD().
//
// All 40 functions are exported -- the module has no statics beyond five
// scalars -- so finder.h is the whole of it rather than a subset.
//
// The file is about 2230 source lines but only these 40 functions compile;
// the gaps between them (408..647, 904..1251, 1614..1705, 1714..1866) produce
// no code at all and are commented-out blocks in the ROM.
//
// Reconstructed from the Feb 6 2004 prototype (SLES_523.84), finder.o.
// Trailing /* NNN */ comments are the original source line numbers.

#include "finder.h"

#include <string.h>                             /* memset                      */

#include "../../common/utility.h"               /* log_10                      */
#include "../../common/variable.h"              /* plyr_wrk                    */
#include "../../system/eeiop/sndbank.h"         /* SndBank*                    */
#include "../enemy/enemy.h"                     /* ShutterChanceChk            */
#include "../plyr/player.h"                     /* IsPlayerInBattle            */
#include "freq_camera.h"                        /* FreqCameraInit              */
#include "m_plyr_camera.h"                      /* m_plyr_camera               */
#include "n_finder_dat.h"                       /* n_finder_dat                */

/* The GS register words the overlay draws with.  ZBUF with the mask bit set
 * (0x1_0a000118) writes no depth, which is what every HUD piece wants; the
 * plain 0x0a000118 is the same register with the mask clear, and DispChara()
 * picks between them off its `z` argument. */
#define FD_ZBUF_NOMASK      0x000000000a000118ULL
#define FD_ZBUF_MASK        0x000000010a000118ULL

/* ALPHA register: 0x44 is the ordinary source-over blend, 0x48 additive. */
#define FD_ALPHA_BLEND      0x44
#define FD_ALPHA_ADD        0x48

/* The finder is centred on the 640x448 screen. */
#define FD_CENTER_X         320.0f
#define FD_CENTER_Y         224.0f

/* --------------------------------------------------------------------------
 *  Module state
 * ------------------------------------------------------------------------ */
ENEDMGLINE_WRK enedmgline_wrk;                              /* data 3124c8 */
INFO_WRK       info_wrk;                                    /* data 312898 */

static int   sp_chance_mode;                                /* sdata 3f0850 */
static int   sp_chance_alpha;                               /* sdata 3f0854 */
static float sp_rot1;                                       /* sdata 3f0858 */
/* The two rings counter-rotate, and the second starts a fifth of a turn ahead
 * so they never line up. */
static float sp_rot2 = 20.0f;                               /* sdata 3f085c */
static int   finder_sound_bank_id = -1;                     /* sdata 3f0860 */

static int   finder_draw_lock;                              /* sbss 3f4c60 */

/* --------------------------------------------------------------------------
 *  Battle tint.  Rises four times faster than it falls, so a fight that keeps
 *  flickering in and out holds the tint up.
 * ------------------------------------------------------------------------ */
void FinderBattleAlphaMain(void)
{                                                                       /* 238 */
    if (IsPlayerInBattle() != 0)
    {
        info_wrk.alp_battle += 30;
        if (0x80 < info_wrk.alp_battle)
        {
            info_wrk.alp_battle = 0x80;
        }
    }
    else
    {
        info_wrk.alp_battle -= 15;
        if (info_wrk.alp_battle < 0)
        {
            info_wrk.alp_battle = 0;
        }
    }
}                                                                       /* 246 */

/* --------------------------------------------------------------------------
 *  Health bar
 * ------------------------------------------------------------------------ */
void ReqHPDispIn(void)
{                                                                       /* 310 */
    if (info_wrk.fade_hpbar == 0 || info_wrk.fade_hpbar == 3)
    {
        info_wrk.time_hpbar  = 0;
        info_wrk.fade_hpbar  = 1;
    }
}                                                                       /* 313 */

/* Refuses to start the fade until the bar has been up for three seconds --
 * 0xb4 frames -- so a burst of damage cannot flicker it. */
void ReqHPDispOut(void)
{                                                                       /* 318 */
    if (180 < info_wrk.time_hpbar && (u_char)(info_wrk.fade_hpbar - 1) < 2)
    {
        info_wrk.fade_hpbar = 3;
    }
}                                                                       /* 321 */

void HPDispInit(void)
{                                                                       /* 326 */
    info_wrk.time_hpbar = 0;
    info_wrk.alp_hpbar  = 0;
    info_wrk.fade_hpbar = 0;
}                                                                       /* 328 */

void HPDispMain(void)
{                                                                       /* 333 */
    switch (info_wrk.fade_hpbar)
    {
    case 1:
        if (0x7f < info_wrk.alp_hpbar + 0x20)
        {
            info_wrk.fade_hpbar = 2;
            info_wrk.alp_hpbar  = 0x80;
        }
        else
        {
            info_wrk.alp_hpbar = (u_char)(info_wrk.alp_hpbar + 0x20);
        }
        break;

    case 2:
        info_wrk.time_hpbar++;
        break;

    case 3:
        if (info_wrk.alp_hpbar - 0x20 < 1)
        {
            info_wrk.fade_hpbar = 0;
            info_wrk.alp_hpbar  = 0;
        }
        else
        {
            info_wrk.alp_hpbar = (u_char)(info_wrk.alp_hpbar - 0x20);
        }
        break;
    }
}                                                                       /* 354 */

/* --------------------------------------------------------------------------
 *  Forwarders onto the camera's widget classes.  These exist so the rest of
 *  the game can reach the filament and the finder without pulling in
 *  m_plyr_camera.h.
 * ------------------------------------------------------------------------ */
void RTFillamentModeOn(int type, int time)
{
    m_plyr_camera.filament.RTModeOn(type, time);                        /* 385 */
}                                                                       /* 386 */

void RTFillamentModeOff(void)
{
    m_plyr_camera.filament.RTModeOff();                                 /* 397 */
}                                                                       /* 398 */

void FilamentDrawLock(void)
{
    m_plyr_camera.filament.DrawLock();                                  /* 403 */
}                                                                       /* 404 */

void FilamentDrawUnlock(void)
{
    m_plyr_camera.filament.DrawUnlock();                                /* 407 */
}                                                                       /* 408 */

/* --------------------------------------------------------------------------
 *  Overlay fade
 * ------------------------------------------------------------------------ */
void ReqFinderDispIn(void)
{                                                                       /* 647 */
    if (info_wrk.fade_finder == 0 || info_wrk.fade_finder == 3)
    {
        info_wrk.fade_finder = 1;
    }
}                                                                       /* 649 */

void ReqFinderDispOut(void)
{                                                                       /* 655 */
    if ((u_char)(info_wrk.fade_finder - 1) < 2)
    {
        info_wrk.fade_finder = 3;
    }
}                                                                       /* 657 */

void FinderDispInit(void)
{                                                                       /* 662 */
    info_wrk.alp_battle  = 0;
    info_wrk.alp_finder  = 0;
    info_wrk.fade_finder = 0;
    info_wrk.sena.flow   = 0;
    memset(&info_wrk.cg, 0, sizeof(CHARGE_GUAGE));
}                                                                       /* 673 */

void FinderDispMain(void)
{                                                                       /* 677 */
    switch (info_wrk.fade_finder)
    {
    case 1:
        if (info_wrk.alp_finder + 0x10 < 0x80)
        {
            info_wrk.alp_finder = (u_char)(info_wrk.alp_finder + 0x10);
        }
        else
        {
            info_wrk.fade_finder = 2;
            info_wrk.alp_finder  = 0x80;
        }
        break;

    case 3:
        if (info_wrk.alp_finder - 0x10 < 1)
        {
            /* The overlay tears the charge gauge down with it, so a shot that
             * was building when the finder came down does not come back. */
            info_wrk.alp_finder  = 0;
            info_wrk.fade_finder = 0;
            info_wrk.sena.flow   = 0;
            memset(&info_wrk.cg, 0, sizeof(CHARGE_GUAGE));
        }
        else
        {
            info_wrk.alp_finder = (u_char)(info_wrk.alp_finder - 0x10);
        }
        break;
    }
}                                                                       /* 703 */

/* --------------------------------------------------------------------------
 *  Shutter chance flash.
 *
 *  The two rings spin in opposite directions the whole time, whether or not a
 *  chance is up; only the alpha is gated.  IN fades up in five steps and hands
 *  over to OUT, which fades back down and hands *back* to IN -- so a held
 *  shutter chance pulses rather than sitting lit.  END is the one-way exit,
 *  and it is entered from either pulsing state the moment the chance lapses.
 * ------------------------------------------------------------------------ */
void SPChanceMain(void)
{
    SHUTTER_CHANCE_STATE n = ShutterChanceChk();                        /* 797 */

    sp_rot1 += 10.0f;                                                   /* 800 */
    if (180.0f < sp_rot1)                                               /* 801 */
    {
        sp_rot1 -= 360.0f;                                              /* 802 */
    }

    sp_rot2 -= 12.0f;                                                   /* 803 */
    if (sp_rot2 < -180.0f)                                              /* 804 */
    {
        sp_rot2 += 360.0f;                                              /* 805 */
    }

    switch (sp_chance_mode)                                             /* 807 */
    {
    case SP_CHANCE_NONE:
        if (n != SHUTTER_CHANCE_SP)
        {
            sp_chance_alpha = 0;
            break;
        }
        sp_chance_mode = SP_CHANCE_IN;                                  /* 810 */
        /* fallthrough */

    case SP_CHANCE_IN:
        if (n == SHUTTER_CHANCE_SP)                                     /* 818 */
        {
            sp_chance_alpha += 0x46;                                    /* 819 */
            if (0x7f < sp_chance_alpha)
            {
                sp_chance_alpha = 0x80;                                 /* 821 */
                sp_chance_mode  = SP_CHANCE_OUT;
            }
        }
        else
        {
            sp_chance_mode = SP_CHANCE_END;
        }
        break;

    case SP_CHANCE_OUT:
        if (n != SHUTTER_CHANCE_SP)                                     /* 829 */
        {
            sp_chance_mode = SP_CHANCE_END;
            break;
        }

        if (0 < sp_chance_alpha - 0x46)                                 /* 830 */
        {
            sp_chance_alpha -= 0x46;                                    /* 831 */
        }
        else
        {
            sp_chance_mode  = SP_CHANCE_IN;                             /* 832 */
            sp_chance_alpha = 0;                                        /* 833 */
        }
        break;

    case SP_CHANCE_END:
        if (n == SHUTTER_CHANCE_SP)                                     /* 840 */
        {
            sp_chance_mode = SP_CHANCE_IN;                              /* 841 */
            break;
        }

        if (0 < sp_chance_alpha - 5)                                    /* 843 */
        {
            sp_chance_alpha -= 5;                                       /* 844 */
        }
        else
        {
            sp_chance_mode  = SP_CHANCE_NONE;                           /* 845 */
            sp_chance_alpha = 0;                                        /* 846 */
        }
        break;                                                          /* 849 */
    }
}

void FinderDrawLock(void)
{
    m_plyr_camera.DrawLock();                                           /* 885 */
}                                                                       /* 892 */

void FinderDrawUnlock(void)
{
    m_plyr_camera.DrawUnlock();                                         /* 896 */
}                                                                       /* 904 */

void ReqFinderFadeIn(void)
{                                                                       /* 1251 */
    m_plyr_camera.FinderIn();
    /* Clears the vertical half of any camera kick still in flight; a FOV pulse
     * is left to finish, see freq_camera.c. */
    FreqCameraInit();
}                                                                       /* 1253 */

void ReqFinderFadeOut(void)
{
    m_plyr_camera.FinderOut();                                          /* 1256 */
}                                                                       /* 1257 */

void InformationDispInit(void)
{                                                                       /* 1270 */
    memset(&info_wrk, 0, sizeof(INFO_WRK));
    HPDispInit();
    FinderDispInit();
    finderEneLifeLen(0.0f);
    finder_draw_lock = 0;
}                                                                       /* 1280 */

void InfoDispPause(void)
{
    info_wrk.disp_pause = 1;                                            /* 1291 */
}

void InfoDispRestart(void)
{
    info_wrk.disp_pause = 0;                                            /* 1296 */
}

/* Whether the film-remaining lamp is drawn at all.  The prototype's body is
 * gone and it always says yes. */
int isDispLamp(void)
{
    return 1;                                                           /* 1312 */
}

/* --------------------------------------------------------------------------
 *  Sprite helpers.
 *
 *  ptyp 2 places the piece relative to its own anchor in n_finder_dat[];
 *  anything else treats x/y as absolute screen coordinates.  atyp picks the
 *  blend, and z 6 is the one value that writes the depth mask.
 * ------------------------------------------------------------------------ */
void DispChara(int label, u_char ptyp, float x, float y, u_char z,
               u_char alp, u_char atyp, u_char bln)
{                                                                       /* 1317 */
    DISP_SPRT ds;
    float     bx;
    float     by;
    u_long    areg;

    if (ptyp == 2)                                                      /* 1323 */
    {
        bx = (float)n_finder_dat[label].x + x;                          /* 1324 */
        by = (float)n_finder_dat[label].y + y;                          /* 1325 */
    }
    else
    {
        bx = x;                                                         /* 1327 */
        by = y;                                                         /* 1328 */
    }

    areg = (atyp != 0) ? FD_ALPHA_ADD : FD_ALPHA_BLEND;                 /* 1335 */

    CopySprDToSpr(&ds, &n_finder_dat[label]);                           /* 1347 */
    ds.x      = bx;                                                     /* 1348 */
    ds.y      = by;
    ds.zbuf   = (z == 6) ? FD_ZBUF_MASK : FD_ZBUF_NOMASK;               /* 1349 */
    ds.alphar = areg;                                                   /* 1350 */
    ds.alpha  = alp;                                                    /* 1351 */
    DispSprD(&ds);                                                      /* 1353 */

    /* `bln` is genuinely unread -- no instruction in the ROM body touches the
     * register it arrives in.  Callers still pass it, so it stays. */
    (void)bln;
}

/* As DispChara(), with the vertex colour overridden.  `bln` is an int here
 * rather than a u_char and is likewise unread. */
void DispCharaRGB(int label, u_char ptyp, float x, float y, u_char z,
                  u_char r, u_char g, u_char b, u_char alp, u_char atyp,
                  int bln)
{                                                                       /* 1359 */
    DISP_SPRT ds;
    float     bx;
    float     by;
    u_long    areg;

    if (ptyp == 2)
    {
        bx = (float)n_finder_dat[label].x + x;
        by = (float)n_finder_dat[label].y + y;
    }
    else
    {
        bx = x;
        by = y;
    }

    areg = (atyp != 0) ? FD_ALPHA_ADD : FD_ALPHA_BLEND;

    CopySprDToSpr(&ds, &n_finder_dat[label]);
    ds.x      = bx;
    ds.y      = by;
    ds.zbuf   = (z == 6) ? FD_ZBUF_MASK : FD_ZBUF_NOMASK;
    ds.alphar = areg;
    ds.r      = r;
    ds.g      = g;
    ds.b      = b;
    ds.alpha  = alp;
    DispSprD(&ds);                                                      /* 1398 */

    (void)bln;      /* unread, as in DispChara() */
}

/* Lays `n` out right-to-left from pos_x, one digit sprite per decimal place.
 * `sprt` points at ten consecutive records (0 .. 9).  The loop always draws at
 * least one digit and keeps going while there is a digit left or the caller
 * asked for more leading zeros. */
void SetNumerousDisp(SPRT_DAT *sprt, int n, int alpha, int chara_width,
                     int pos_x, int pos_y, float scale, int iZeroDispFigure,
                     int bAddAlphaFlg)
{                                                                       /* 1403 */
    int       i = 0;                                                    /* 1404 */
    int       dsp_num;
    DISP_SPRT ds;

    do
    {
        dsp_num = n / 10;                                               /* 1407 */

        CopySprDToSpr(&ds, &sprt[n % 10]);                              /* 1410 */

        if (bAddAlphaFlg != 0)                                          /* 1411 */
        {
            ds.alphar = FD_ALPHA_ADD;                                   /* 1413 */
        }

        ds.zbuf  = FD_ZBUF_MASK;
        ds.alpha = (u_char)alpha;                                       /* 1412 */
        ds.x     = ds.x + (float)pos_x;                                 /* 1416 */
        ds.y     = ds.y + (float)pos_y;

        /* scale 1.0 means "leave the record's own scale alone", which is not
         * the same as writing 1.0 into it -- the table's csx/csy anchors would
         * be lost. */
        if (scale != 1.0f)                                              /* 1417 */
        {
            ds.csx = ds.x;                                              /* 1418 */
            ds.csy = ds.y;
            ds.scw = scale;
            ds.sch = scale;
        }

        ds.tex1 = 0x161;                                                /* 1419 */
        DispSprD(&ds);                                                  /* 1420 */

        pos_x -= chara_width;                                           /* 1422 */
        i++;
        n = dsp_num;                                                    /* 1423 */
    } while (n != 0 || (n = 0, i < iZeroDispFigure));                   /* 1425 */
}                                                                       /* 1428 */

/* The post-shot score.  Digits are labels 0x7f..0x88 and step 18 pixels left
 * each; log_10() decides how many there are. */
void DispPointNumberNew(int number, short adj_x, short adj_y, u_char malp)
{                                                                       /* 1434 */
    u_char alp;
    int    i;
    int    m;
    u_int  n;

    if (9999 < number)                                                  /* 1439 */
    {
        number = 9999;
    }

    n = log_10(number);                                                 /* 1440 */
    alp = info_wrk.alp_finder;                                          /* 1441 */
    m = (int)adj_x;

    if (0 < (int)n)                                                     /* 1444 */
    {
        for (i = (int)n; i != 0; i--)                                   /* 1448 */
        {
            DispChara(number % 10 + 0x7f, 2, (float)m, (float)adj_y,    /* 1446 */
                      6, (u_char)((malp * alp) >> 7), 0, 4);
            number /= 10;                                               /* 1445 */
            m -= 18;                                                    /* 1448 */
        }
    }
}

/* --------------------------------------------------------------------------
 *  Charge gauge.
 *
 *  Twelve pips around the capture circle (labels 0x5c..0x67 for the ring,
 *  0x68..0x73 for the highlight over it).  A pip is armed each time the charge
 *  level rises, and then runs its own fade on its own counter -- filling for
 *  15 frames, draining for 15, then held at a quarter brightness.
 * ------------------------------------------------------------------------ */
void DispCameraCharge(short pos_x, short pos_y, int battle_master_alp)
{                                                                       /* 1454 */
    u_char    ba  = 0;
    u_char    ba2 = 0;
    int       i;
    int       n;
    CHARGE_GUAGE_ONE *cgop;
    DISP_SPRT ds;
    float     radius_rate;

    radius_rate = m_plyr_camera.camera_power_up.GetRadiusRate();

    if (info_wrk.cg.flow == 1)
    {
        info_wrk.cg.bg_flow = info_wrk.cg.flow;
        info_wrk.cg.flow    = 2;
    }

    /* One pip per charge level, armed only if it is currently dark. */
    if (plyr_wrk.charge_num != info_wrk.cg.old_ch_num && plyr_wrk.charge_num != 0)
    {
        for (i = 0; i < (int)plyr_wrk.charge_num; i++)
        {
            if (info_wrk.cg.cgo[i].flow == 0)
            {
                info_wrk.cg.cgo[i].flow = 1;
            }
        }
    }

    /* The dim backing ring, drawn whole whenever any charge is held. */
    if (plyr_wrk.charge_num != 0)
    {
        for (i = 0; i < 12; i++)
        {
            n = 0x5c + i;
            CopySprDToSpr(&ds, &n_finder_dat[n]);
            ds.csx    = (float)pos_x + FD_CENTER_X;
            ds.csy    = (float)pos_y + FD_CENTER_Y;
            ds.x      = (float)(n_finder_dat[n].x + pos_x);
            ds.y      = (float)(n_finder_dat[n].y + pos_y);
            ds.alpha  = (u_char)(battle_master_alp * 20 / 128);
            ds.zbuf   = FD_ZBUF_MASK;
            ds.tex1   = 0x161;
            ds.alphar = FD_ALPHA_ADD;
            ds.scw    = radius_rate;
            ds.sch    = radius_rate;
            DispSprD(&ds);
        }
    }

    for (i = 0; i < 12; i++)                                            /* 1502 */
    {
        cgop = &info_wrk.cg.cgo[i];                                     /* 1503 */

        switch (cgop->flow)                                             /* 1506 */
        {
        case 0:
            cgop->cnt = 0;                                              /* 1510 */
            ba  = 0;                                                    /* 1509 */
            ba2 = 0;
            break;                                                      /* 1511 */

        case 1:
            cgop->cnt = 0;                                              /* 1515 */
            cgop->flow++;                                               /* 1516 */
            /* fallthrough */

        case 2:
            ba  = (u_char)(cgop->cnt * 17);                             /* 1518 */
            ba2 = (u_char)(cgop->cnt * 36 / 15);                        /* 1519 */
            cgop->cnt++;                                                /* 1520 */
            if (15 <= cgop->cnt)
            {
                cgop->cnt = 0;                                          /* 1529 */
                cgop->flow++;                                           /* 1530 */
            }
            break;                                                      /* 1524 */

        case 3:
            /* ~x is the ROM's own spelling of 255 - x here; the two agree over
             * the range this can produce. */
            ba  = (u_char)~(cgop->cnt * 191 / 15);                      /* 1526 */
            ba2 = (u_char)(36 - cgop->cnt * 36 / 15);                   /* 1527 */
            cgop->cnt++;                                                /* 1528 */
            if (15 <= cgop->cnt)
            {
                cgop->cnt = 0;
                cgop->flow++;
            }
            break;

        case 4:
            ba  = 0x40;
            ba2 = 0;
            break;
        }

        ba  = (u_char)(ba  * battle_master_alp / 128);
        ba2 = (u_char)(ba2 * battle_master_alp / 128);

        if (0 < cgop->flow)
        {
            if (ba != 0)
            {
                n = 0x5c + i;
                CopySprDToSpr(&ds, &n_finder_dat[n]);
                ds.csx    = (float)pos_x + FD_CENTER_X;
                ds.csy    = (float)pos_y + FD_CENTER_Y;
                ds.x      = (float)(n_finder_dat[n].x + pos_x);
                ds.y      = (float)(n_finder_dat[n].y + pos_y);
                ds.zbuf   = FD_ZBUF_MASK;
                ds.alphar = FD_ALPHA_ADD;
                ds.tex1   = 0x161;
                ds.alpha  = ba;
                ds.scw    = radius_rate;
                ds.sch    = radius_rate;
                DispSprD(&ds);
            }

            if (0 < cgop->flow && ba2 != 0)
            {
                n = 0x68 + i;
                CopySprDToSpr(&ds, &n_finder_dat[n]);
                ds.csx    = (float)pos_x + FD_CENTER_X;
                ds.csy    = (float)pos_y + FD_CENTER_Y;
                ds.x      = (float)(n_finder_dat[n].x + pos_x);
                ds.y      = (float)(n_finder_dat[n].y + pos_y);
                ds.zbuf   = FD_ZBUF_MASK;
                ds.alphar = FD_ALPHA_ADD;
                ds.tex1   = 0x161;
                ds.alpha  = ba2;
                ds.scw    = radius_rate;
                ds.sch    = radius_rate;
                DispSprD(&ds);
            }
        }
    }

    info_wrk.cg.old_ch_num = plyr_wrk.charge_num;                       /* 1570 */
}

void ChargeDispReset(void)
{
}                                                                       /* 1578 */

/* The four quadrants of the capture circle, scaled by whatever radius lens is
 * fitted.  TEST 0x30003 turns the alpha test on, which is what cuts the ring
 * out of its texture page. */
void DispCaptureCircleNew(short pos_x, short pos_y)
{                                                                       /* 1583 */
    u_char    a;
    int       i;
    float     radius_rate;
    DISP_SPRT ds;

    radius_rate = m_plyr_camera.camera_power_up.GetRadiusRate();        /* 1587 */
    a = 0xff;                                                           /* 1599 */

    for (i = 0; i < 4; i++)                                             /* 1613 */
    {
        CopySprDToSpr(&ds, &n_finder_dat[0x52 + i]);                    /* 1606 */
        ds.r     = 0x80;                                                /* 1607 */
        ds.g     = 0x80;
        ds.b     = 0x80;
        ds.test  = 0x30003;                                             /* 1608 */
        ds.alpha = a;                                                   /* 1609 */
        ds.x     = ds.x + (float)pos_x;                                 /* 1610 */
        ds.y     = ds.y + (float)pos_y;
        ds.csx   = (float)pos_x + FD_CENTER_X;                          /* 1611 */
        ds.csy   = (float)pos_y + FD_CENTER_Y;
        ds.scw   = radius_rate;
        ds.sch   = radius_rate;
        DispSprD(&ds);                                                  /* 1612 */
    }
}

/* --------------------------------------------------------------------------
 *  Enemy health readout.  Three forwarders onto the camera's CEneLife.
 * ------------------------------------------------------------------------ */
void finderTriggerEneLifeDecrease(float new_hp_per)
{
    m_plyr_camera.ene_life.Decrease(new_hp_per);                        /* 1705 */
}                                                                       /* 1706 */

void finderSetEneLifePercentage(float new_hp_per)
{
    m_plyr_camera.ene_life.Set(new_hp_per);                             /* 1709 */
}                                                                       /* 1710 */

void finderEneLifeLen(float len)
{
    m_plyr_camera.ene_life.FrameLenSet(len);                            /* 1713 */
}                                                                       /* 1714 */

/* --------------------------------------------------------------------------
 *  Post-shot score line.
 *
 *  Two independent state machines over the same counter shape: `_num` drives
 *  the number (20 frames in, 8 held, 20 out) and `_sp` the message above it,
 *  which additionally has a lead-in state that counts *down* before anything
 *  is shown.  Any state the machine does not know resets it.
 * ------------------------------------------------------------------------ */
void DispFinderMessageMain(void)
{
    DISPFMES *dfp = &info_wrk.dispfmes;                                 /* 1866 */
    int       alp;

    switch (dfp->flow_num)
    {
    case 1:
        alp = dfp->cnt_num << 7;
        dfp->cnt_num++;
        dfp->num_alp = alp / 20;
        if (19 < dfp->cnt_num)
        {
            dfp->flow_num = 2;
            dfp->num_alp  = 0x80;
            dfp->cnt_num  = 0;
        }
        break;

    case 2:
        dfp->num_alp = 0x80;
        dfp->cnt_num++;
        if (7 < dfp->cnt_num)
        {
            dfp->cnt_num  = 0;
            dfp->flow_num = 3;
        }
        break;

    case 3:
        alp = dfp->cnt_num << 7;
        dfp->cnt_num++;
        dfp->num_alp = 0x80 - alp / 20;
        if (19 < dfp->cnt_num)
        {
            dfp->cnt_num  = 0;
            dfp->num_alp  = 0;
            dfp->flow_num = 0;
        }
        break;

    default:
        dfp->cnt_num = 0;
        dfp->num_alp = 0;
        break;
    }

    switch (dfp->flow_sp)
    {
    /* The lead-in: cnt_sp is seeded by whoever raised the line and counts back
     * to zero with the message still hidden. */
    case 1:
        if (0 < dfp->cnt_sp - 1)
        {
            dfp->cnt_sp--;
            dfp->mes_alp = 0;
        }
        else
        {
            dfp->flow_sp = 2;
            dfp->cnt_sp  = 0;
            dfp->mes_alp = 0;
        }
        break;

    case 2:
        alp = dfp->cnt_sp << 7;
        if (dfp->cnt_sp + 1 < 20)
        {
            dfp->cnt_sp++;
            dfp->mes_alp = alp / 20;
        }
        else
        {
            dfp->flow_sp = 3;
            dfp->cnt_sp  = 0;
            dfp->mes_alp = 0x80;
        }
        break;

    case 3:
        if (dfp->cnt_sp + 1 < 8)
        {
            dfp->cnt_sp++;
            dfp->mes_alp = 0x80;
        }
        else
        {
            dfp->flow_sp = 4;
            dfp->cnt_sp  = 0;
            dfp->mes_alp = 0x80;
        }
        break;

    case 4:
        alp = dfp->cnt_sp << 7;
        if (dfp->cnt_sp + 1 < 20)
        {
            dfp->cnt_sp++;
            dfp->mes_alp = 0x80 - alp / 20;
        }
        else
        {
            dfp->flow_sp = 0;
            dfp->cnt_sp  = 0;
            dfp->mes_alp = 0;
        }
        break;

    default:
        dfp->cnt_sp  = 0;
        dfp->mes_alp = 0;
        break;
    }
}                                                                       /* 1938 */

/* --------------------------------------------------------------------------
 *  Draws a run of n_finder_dat[] records with one shared transform.  This is
 *  the general-purpose one -- rotation, per-channel colour, priority override
 *  and two scaling origins.
 * ------------------------------------------------------------------------ */
void PutSpriteYW(u_short top_label, u_short end_label, float pos_x, float pos_y,
                 float rot, int rgb, float alp, float scl_x, float scl_y,
                 u_char scl_mode, int pri, u_char by, u_char blnd, u_char z_sw)
{                                                                       /* 2003 */
    int       i;
    float     rot_px;
    float     rot_py;
    float     scl_px;
    float     scl_py;
    DISP_SPRT ds;

    for (i = (int)top_label; i <= (int)end_label; i++)
    {
        CopySprDToSpr(&ds, &n_finder_dat[i]);

        ds.x = ds.x + pos_x;
        ds.y = ds.y + pos_y;

        ds.alphar = (blnd == 0) ? FD_ALPHA_BLEND : FD_ALPHA_ADD;
        ds.alpha  = (u_char)(int)alp;

        /* Rotation is always about the piece's own centre. */
        rot_px = ds.x + (float)(ds.w >> 1);
        rot_py = ds.y + (float)(ds.h >> 1);
        ds.crx = rot_px;
        ds.cry = rot_py;

        /* Scaling is either about that same centre, or -- scl_mode non-zero --
         * about the first record's anchor, so a run scales as one piece. */
        if (scl_mode == 0)
        {
            scl_px = ds.x + (float)(ds.w >> 1);
            scl_py = ds.y + (float)(ds.h >> 1);
        }
        else
        {
            scl_px = (float)n_finder_dat[top_label].x;
            scl_py = (float)n_finder_dat[top_label].y;
        }
        ds.csx = scl_px;
        ds.csy = scl_py;

        /* 0xff means "keep the table's priority". */
        if (pri != 0xff)
        {
            ds.z   = 0xfffff - (pri & 0xfffff);
            ds.pri = pri;
        }

        if (z_sw != 0)
        {
            ds.zbuf = FD_ZBUF_MASK;
        }

        ds.scw  = scl_x;
        ds.sch  = scl_y;
        ds.rot  = rot;
        ds.tex1 = ((u_long)by << 5) | 0x141;
        ds.r    = (u_char)((u_int)rgb >> 16);
        ds.g    = (u_char)((u_int)rgb >> 8);
        ds.b    = (u_char)rgb;
        DispSprD(&ds);
    }
}                                                                       /* 2062 */

/* Widens a SPRT_DAT into the explicit-UV SPRT_DAT2 the DispSprD2() path wants:
 * the compact record's u/v/w/h become two texel corners. */
void SD1toSD2(SPRT_DAT *sd, SPRT_DAT2 *sd2)
{
    sd2->tex0  = sd->tex0;                                              /* 2076 */
    sd2->u1    = (float)sd->u;                                          /* 2077 */
    sd2->v1    = (float)sd->v;                                          /* 2078 */
    sd2->u2    = (float)(sd->u + sd->w);                                /* 2079 */
    sd2->v2    = (float)(sd->v + sd->h);                                /* 2080 */
    sd2->w     = (float)sd->w;                                          /* 2081 */
    sd2->h     = (float)sd->h;                                          /* 2082 */
    sd2->x     = (float)sd->x;                                          /* 2083 */
    sd2->y     = (float)sd->y;                                          /* 2084 */
    sd2->pri   = sd->pri;                                               /* 2085 */
    sd2->alpha = sd->alpha;                                             /* 2086 */
}

/* --------------------------------------------------------------------------
 *  The camera's own sound bank.  Files 0xcfb / 0xcfa are the sample set and
 *  its header; -1 for the size lets the bank take whatever the header says.
 * ------------------------------------------------------------------------ */
void FinderBankSetup(void)
{
    finder_sound_bank_id = SndBankNew(0xcfb, 0xcfa, -1);                /* 2202 */
}                                                                       /* 2205 */

int FinderBankIsReady(void)
{
    return SndBankIsReady(finder_sound_bank_id);                        /* 2210 */
}                                                                       /* 2211 */

/* Note the argument order is shuffled on the way through: the bank call takes
 * vol/pitch before fade_time, this one after. */
int FinderBankPlay(int no, int effect, int loop, int fade_time,
                   SND_3D_SET *s3d, int vol, int pitch)
{
    return SndBankPlay(finder_sound_bank_id, no, effect, loop,          /* 2216 */
                       vol, pitch, fade_time, s3d);
}                                                                       /* 2218 */

void FinderBankRelease(void)
{
    SndBankRelease(finder_sound_bank_id);                               /* 2223 */
    finder_sound_bank_id = -1;                                          /* 2224 */
}                                                                       /* 2225 */

int FinderBankIsLoopSnd(int no)
{
    return SndBankIsLoopSnd(finder_sound_bank_id, no);                  /* 2229 */
}
