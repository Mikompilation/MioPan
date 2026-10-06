// FILE: /home/zero_rom/zero2np/src/ingame/photo/center_cross.c
//
// The ten little marks that ring the capture circle -- one per ENE_WRK slot.
//
// Each mark tracks its own ghost with a counter rather than a position: while
// that ghost is on screen and photographable the counter climbs to CC_CNT_MAX
// and the mark's target snaps to the ghost's screen point; when it stops
// qualifying the counter falls back to zero and the target reverts to the
// player's own crosshair.  Draw() then places the mark cnt/CC_CNT_MAX of the
// way along, so a mark slides out to a ghost and drifts back rather than
// jumping, and Work() never has to know which ghost it had last frame.
//
// Init() is inline in center_cross.h (a memset of the whole object) and lives
// with the class in m_plyr_camera.h; the two bodies here are center_cross.o's.
//
// Reconstructed from the Feb 6 2004 prototype (SLES_523.84), center_cross.o.
// Both ZERO2.MAP .text symbols.

#include "m_plyr_camera.h"
#include "finder.h"                             /* DispChara               */
#include "n_finder_dat.h"                       /* n_finder_dat            */
#include "../../common/variable.h"              /* plyr_wrk                */
#include "../enemy/enemy.h"                     /* ene_wrk / IsActEnemy    */

/* How far out a mark can travel, and the denominator Draw() interpolates
 * with.  It is also ENE_WRK_MAX, but the two are separate numbers in the ROM:
 * this one is a distance, not a slot count. */
#define CC_CNT_MAX          10

/* How fast a mark fades in and out once it is placed. */
#define CC_ALPHA_STEP       8

/* n_finder_dat[] index.  ptyp 3 places the sprite at the given point, ptyp 2
 * adds the table entry's own x/y -- which is how the fallback below lands on
 * the crosshair without any arithmetic. */
#define FD_CENTER_CROSS     0x38

/* ENE_WRK bits.  st.sta 0x80 is the "eligible target" flag the nearest-ghost
 * sweeps in enemy.c key off; attr 0x40 marks a ghost the finder never puts a
 * mark on at all. */
#define CC_ENEST_TARGETABLE 0x80
#define CC_ENEATTR_NO_MARK  0x40

/* ==========================================================================
 *  CCenterCross::Work (8..33)
 *
 *  Advance every mark's counter and keep its target point fresh.
 * ======================================================================== */
void CCenterCross::Work(void)
{                                                                       /* 9 */
    int cmx = CC_CNT_MAX;                                               /* 10 */

    for (int i = 0; i < ENE_WRK_MAX; i++)                               /* 12 */
    {
        CENTER_CROSS *ccp = &center_cercle[i];

        if ((ene_wrk[i].st.sta & CC_ENEST_TARGETABLE) != 0 &&
            (ene_wrk[i].attr & CC_ENEATTR_NO_MARK) == 0)                /* 19 */
        {
            if (++ccp->cnt > cmx) { ccp->cnt = cmx; }                   /* 21 */

            ccp->tx = ene_wrk[i].fp[0];                                 /* 23 */
            ccp->ty = ene_wrk[i].fp[1];                                 /* 24 */
        }
        else
        {
            if (--ccp->cnt < 0) { ccp->cnt = 0; }                       /* 26 */

            /* Only once it has come all the way home; while it is still on
             * its way back it keeps aiming at where the ghost was. */
            if (ccp->cnt <= 0)                                          /* 28 */
            {
                ccp->tx = plyr_wrk.fp[0];                               /* 29 */
                ccp->ty = plyr_wrk.fp[1];                               /* 30 */
            }
        }
    }                                                                   /* 33 */
}

/* ==========================================================================
 *  CCenterCross::Draw (38..86)
 *
 *  Place each mark cnt/cmx of the way from the player's crosshair to its
 *  target, ramp its alpha towards or away from iAlpha, and draw it.
 *
 *  The second term is what keeps the sprite registered: n_finder_dat's own
 *  x/y is where the mark sits when it is at rest, so subtracting the offset
 *  from the crosshair to that entry turns the interpolated *world* point into
 *  a finder-local one.
 *
 *  If nothing was drawn at all the plain resting mark goes out instead, with
 *  ptyp 2 so the table entry supplies its own position.
 * ======================================================================== */
void CCenterCross::Draw(int fndr_mx, int fndr_my, int iAlpha)           /* 38 */
{
    int m   = 0;
    int cmx = CC_CNT_MAX;                                               /* 47 */

    for (int i = 0; i < ENE_WRK_MAX; i++)                               /* 52 */
    {
        CENTER_CROSS *ccp = &center_cercle[i];                          /* 54 */
        int           alp;
        float         ex;
        float         ey;

        /* The line this test was on is swallowed by fixed_array.h's
         * operator[]; it sits between 54 and 69. */
        if ((ene_wrk[i].attr & CC_ENEATTR_NO_MARK) != 0)
        {
            continue;
        }

        ex = (float)((ccp->tx - plyr_wrk.fp[0]) * ccp->cnt / cmx + plyr_wrk.fp[0])
           - (float)(plyr_wrk.fp[0] - (int)n_finder_dat[FD_CENTER_CROSS].x)
           + (float)fndr_mx;                                            /* 69 */
        ey = (float)((ccp->ty - plyr_wrk.fp[1]) * ccp->cnt / cmx + plyr_wrk.fp[1])
           - (float)(plyr_wrk.fp[1] - (int)n_finder_dat[FD_CENTER_CROSS].y)
           + (float)fndr_my;                                            /* 71 */

        if (IsActEnemy(i))                                              /* 73 */
        {
            alp = ccp->alp + CC_ALPHA_STEP;                             /* 74 */
            if (alp >= iAlpha) { alp = iAlpha; }
        }
        else
        {
            alp = ccp->alp - CC_ALPHA_STEP;                             /* 76 */
            if (alp < 0) { alp = 0; }
        }

        ccp->alp = (u_char)alp;

        if (ccp->alp != 0)                                              /* 78 */
        {
            m++;
            DispChara(FD_CENTER_CROSS, 3, ex, ey, 6, ccp->alp, 0, 4);   /* 79 */
        }
    }                                                                   /* 83 */

    if (m == 0)                                                         /* 85 */
    {
        DispChara(FD_CENTER_CROSS, 2, (float)fndr_mx, (float)fndr_my,
                  6, (u_char)iAlpha, 0, 4);                             /* 86 */
    }
}
