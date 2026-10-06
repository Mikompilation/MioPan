/* ==========================================================================
 *  debug/scn_test.h
 *
 *  Scene-test debug mode public interface.  Declares the entry points called
 *  from debug.c, the effect-flag query used by the scene renderer, and the
 *  cross-line draw helper called by the light-display subsystem.
 *
 *  Reconstructed from the Feb 6 2004 prototype (SLES_523.84).
 * ======================================================================== */

#ifndef _DEBUG_SCN_TEST_H
#define _DEBUG_SCN_TEST_H

#include "eetypes.h"

/* --------------------------------------------------------------------------
 *  Effect slot indices (also used by the scene renderer to test which
 *  post-processing passes should run this frame).
 * ------------------------------------------------------------------------ */
typedef enum
{
    SCN_DB_EFF_Z_DEP      = 0,
    SCN_DB_EFF_MONO       = 1,
    SCN_DB_EFF_SEPIA      = 2,
    SCN_DB_EFF_FLG_ONLY   = 2,
    SCN_DB_EFF_DITHER     = 3,
    SCN_DB_EFF_BLUR_N     = 4,
    SCN_DB_EFF_BLUR_B     = 5,
    SCN_DB_EFF_BLUR_W     = 6,
    SCN_DB_EFF_DEFORM     = 7,
    SCN_DB_EFF_FOCUS      = 8,
    SCN_DB_EFF_CONTRAST1  = 9,
    SCN_DB_EFF_CONTRAST2  = 10,
    SCN_DB_EFF_CONTRAST3  = 11,
    SCN_DB_EFF_NEGA       = 12,
    SCN_DB_EFF_FADE_FRAME = 13,
    SCN_DB_EFF_CROSS_FADE = 14,
    SCN_DB_EFF_FADE_SCR   = 15,
    SCN_DB_EFF_SHIBATA    = 16,
    SCN_DB_EFF_MAX        = 17
} SCN_DB_EFF_TYPE;

void SceneTestInit(void);
int  SceneTestMain(void);
void SceneTestDrawCrossLine(float *CenterPos, float LineLength, int r, int g, int b, int a);
int  SceneTestIsMenuMode(void);
int  SceneTestEffectFlgGet(SCN_DB_EFF_TYPE type);

#endif /* _DEBUG_SCN_TEST_H */
