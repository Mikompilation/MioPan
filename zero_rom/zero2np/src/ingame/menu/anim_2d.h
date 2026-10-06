/* ==========================================================================
 *  ingame/menu/anim_2d.h
 *
 *  Shared 2D animation-table evaluators (anim_2d.o, .text 0x12aeb0).  Each
 *  *_ANIM_TBL is a small array of "if timer falls in [start_time,end_time)
 *  play this segment" entries, terminated by a start_time == -1 sentinel;
 *  the Anim2D_CalcNow* family scans a table for the entry that owns `timer`
 *  and interpolates within it.  loading.c's background/cloud scroll and
 *  fade envelopes are built on this.
 *
 *  Reconstructed from the Feb 6 2004 prototype (SLES_523.84).
 * ======================================================================== */

#ifndef _INGAME_MENU_ANIM_2D_H
#define _INGAME_MENU_ANIM_2D_H

#include <sys/types.h>              /* u_char */

typedef struct                      /* 0x10 */
{
    /* 0x0 */ float     start_pos;
    /* 0x4 */ float     end_pos;
    /* 0x8 */ short int start_time;
    /* 0xa */ short int end_time;
    /* 0xc */ int       anim_label;
} POS_ANIM_TBL;

typedef struct                      /* 0xc */
{
    /* 0x0 */ float     start_scl;
    /* 0x4 */ float     end_scl;
    /* 0x8 */ short int start_time;
    /* 0xa */ short int end_time;
} SCL_ANIM_TBL;

typedef struct                      /* 0x8 */
{
    /* 0x0 */ short int start_alpha;
    /* 0x2 */ short int end_alpha;
    /* 0x4 */ short int start_time;
    /* 0x6 */ short int end_time;
} ALPHA_ANIM_TBL;

typedef struct                      /* 0x8 */
{
    /* 0x0 */ short int start_rgb;
    /* 0x2 */ short int end_rgb;
    /* 0x4 */ short int start_time;
    /* 0x6 */ short int end_time;
} RGB_ANIM_TBL;

typedef struct                      /* 0xc */
{
    /* 0x0 */ float     start_rot;
    /* 0x4 */ float     end_rot;
    /* 0x8 */ short int start_time;
    /* 0xa */ short int end_time;
} ROT_ANIM_TBL;

/* Scan `tbl` (up to 100 entries, start_time == -1 terminates) for the entry
 * whose [start_time,end_time) contains `timer`, and interpolate within it.
 * Each returns its "not found" value untouched if no entry matches. */
float  Anim2D_CalcNowPos(const POS_ANIM_TBL *tbl, int timer);      /* 0x12aeb0 */
float  Anim2D_CalcNowScale(const SCL_ANIM_TBL *tbl, int timer);     /* 0x12afe8 */
u_char Anim2D_CalcNowAlpha(const ALPHA_ANIM_TBL *tbl, int timer);   /* 0x12b130 */
u_char Anim2D_CalcNowRGB(const RGB_ANIM_TBL *tbl, int timer);       /* 0x12b260 */
float  Anim2D_CalcNowRot(const ROT_ANIM_TBL *tbl, int timer);       /* 0x12b3b8 */

/* calc_label: 0/1 linear ease (1 pre-squares both time args), 2 ease-out. */
float  Anim2D_CalcPosAnim(float start_pos, float end_pos, int calc_label,
                          int anim_time, int timer);          /* 0x12b500 */
u_char Anim2D_CalcAlphaAnim(u_char start_alpha, u_char end_alpha,
                            int anim_time, int timer);        /* 0x12b618 */

#endif /* _INGAME_MENU_ANIM_2D_H */
