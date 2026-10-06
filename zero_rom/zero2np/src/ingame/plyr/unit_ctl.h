/* ==========================================================================
 *  ingame/plyr/unit_ctl.h
 *
 *  Shared "unit" (player / sister / enemy) geometry from unit_ctl.o: the
 *  look-at rotations, the line-of-sight cone test, the heading quantiser and
 *  the ranged random.  Nine functions, no state -- which is why nearly every
 *  ingame module includes this header.
 *
 *  Reconstructed from the Feb 6 2004 prototype (SLES_523.84).
 * ======================================================================== */

#ifndef _INGAME_PLYR_UNIT_CTL_H
#define _INGAME_PLYR_UNIT_CTL_H

#include "eetypes.h"

/* Rotation from the player towards p.  id is a bit mask: 1 fills rot[0] (the
 * pitch onto the player's eye line, 600 units above the player's origin),
 * 2 fills rot[1] (the yaw).  Neither angle is wrapped. */
void GetTrgtRotFromPlyr(float *p, float *rot, int id);

/* Rotation that turns p0 towards p1, same id bit mask as above.  Unlike
 * GetTrgtRotFromPlyr() both angles are wrapped into (-PI, PI], which is what
 * the camera callers need.  Lives in unit_ctl.o, not utility.o. */
void GetTrgtRot(const float *p0, const float *p1, float *rot, int id);

/* Quantise a heading to a direction index, 0 = straight ahead and running
 * clockwise.  id selects the granularity: 0 four ways, 1 eight ways (0 front,
 * 4 back), 2 two ways (0 front, 1 back).  Any other id gives a bare
 * left/right: 0 when rot >= 0, 1 when rot < 0. */
u_char ConvertRot2Dir(float rot, u_char id);

/* Wrap rot into (-PI, PI]. */
void RotLimitChk(float *rot);

/* Y rotation that turns p0 towards p1. */
float GetTrgtRotY(const float *p0, const float *p1);

/* Non-zero when tp lies within +/- rng radians of the heading rot at vp. */
int RotRngChk(float *vp, float *tp, float rot, float rng);

/* Non-zero when tp is OUTSIDE the viewer's cone: further than dist (0 = no
 * distance limit) or outside the full-width sight angle centred on rot. */
u_char OutSightChk(float *tp, float *vp, float rot, float sight, float dist);

/* Uniform random integer in [min, min + lng). */
int GetRndSP(u_int min, u_int lng);

/* Freeze / hide every resident ghost except the slots named in `except` (a
 * bitmask by ENE_WRK index).  req: 0 release, 1 freeze the action script,
 * 2 stop drawing, 3 both.  Drives the scripted-death cinematic. */
void ReqEneStop(u_char req, u_char except);

#endif /* _INGAME_PLYR_UNIT_CTL_H */
