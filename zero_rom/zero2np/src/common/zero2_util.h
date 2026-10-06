/* ==========================================================================
 *  common/zero2_util.h
 *
 *  Shared Zero2 utilities (zero2_util.c) -- the handful of helpers that did
 *  not fit the generic utility.c / utility2.c split because they reach into
 *  game state (player, sister, ghosts, the room's registration data) or into
 *  the 2D/3D layers.
 *
 *  Reconstructed from the Feb 6 2004 prototype (SLES_523.84).
 * ======================================================================== */

#ifndef _COMMON_ZERO2_UTIL_H
#define _COMMON_ZERO2_UTIL_H

#include "../sdk/scetypes.h"

/* Which axis CalcAngle() flattens before measuring, i.e. the plane the angle
 * is taken in.  The value doubles as the index of the component zeroed. */
enum _PLANE3D
{
    YZ = 0,
    ZX = 1,
    XY = 2
};
typedef _PLANE3D PLANE3D;

/* The object classes GetObjectPos() understands.  0/1/2/5 are ghost kinds
 * resolved through GetEnePos(); 8/9 are placed map objects looked up by
 * label.  3 and 4 are holes in the range and take the error path along with
 * anything >= OBJ_TYPE_MAX. */
#define OBJ_TYPE_PLAYER 6
#define OBJ_TYPE_SISTER 7
#define OBJ_TYPE_MAX    10

/* Resolve a world position for an (obj_type, obj_id) pair -- used by the
 * event gaze and event camera code to follow a moving target.  Non-zero on
 * success; obj_pos is zeroed first either way. */
int GetObjectPos(float *obj_pos, u_char obj_type, int obj_id);

/* The two hooks utility2.c's reporter dispatches through; installed with
 * SetPrintWarning() / SetPrintAssert().  The assert one does not return until
 * the player presses the confirm button. */
void Zero2PrintWarningFunc(char *str);
void Zero2PrintAssertFunc(char *str);

/* Build a TEX0 for a loaded TIM2 and queue its texture and CLUT uploads.
 * tw_2 / th_2 are log2 sizes; tbp / cbp are VRAM word addresses. */
void utilTim2SendVram(u_int *tim2_addr, int tbp, int cbp, int tw_2, int th_2);

/* Pick a point on the XZ plane at a random bearing and a random distance in
 * [min, max) from o_pos.  Y and W are copied through unchanged. */
void GetRandomPositionXZ(float *pos, float *o_pos, float max, float min);

/* Signed angle from vDirectionAxis to vDirection measured within `plane_3d`,
 * biased by PI and folded back into range.  vTop supplies the handedness. */
float CalcAngle(const float *vDirectionAxis, const float *vDirection,
                const float *vTop, PLANE3D plane_3d);

#endif /* _COMMON_ZERO2_UTIL_H */
