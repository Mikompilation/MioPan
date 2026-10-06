/* ==========================================================================
 *  graphics/scene/scene_effect.h
 *
 *  Scene effect controller interface: the interpreter for the FOD file's
 *  effect byte-stream (dither, blur, deform, contrast, fades, lens flare,
 *  lights, torches, haze, model fades, particle deforms, pad vibration).
 *  SceneEffectMain() runs it against scene frames, SceneMovieEffectMain()
 *  against movie frames.
 *
 *  Reconstructed from the Feb 6 2004 prototype (SLES_523.84), 0x0024cfd0.
 * ======================================================================== */

#ifndef _GRAPHICS_SCENE_SCENE_EFFECT_H
#define _GRAPHICS_SCENE_SCENE_EFFECT_H

#include "scene.h"

void SceneEffectInit(void);
void SceneEffectMain(SCENE_CTRL *pSceneCtrl, u_int *pDataAddr);
void SceneEffectEnd(void);
void SceneMovieEffectMain(int NowFrame, u_int *pDataAddr);

#endif /* _GRAPHICS_SCENE_SCENE_EFFECT_H */
