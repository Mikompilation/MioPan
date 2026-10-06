/* ==========================================================================
 *  graphics/obj_draw_ctrl.h
 *
 *  Object draw-control interface.
 *
 *  Reconstructed from the Feb 6 2004 prototype (SLES_523.84).
 * ======================================================================== */

#ifndef _GRAPHICS_OBJ_DRAW_CTRL_H
#define _GRAPHICS_OBJ_DRAW_CTRL_H

typedef struct
{
    int player;
    int sister;
    int enemy;
    int effect;
    int room;
    int obj;
    int sky;
    int shadow;
    int shadow_src;
} OBJ_DRAW_FLG;

extern OBJ_DRAW_FLG obj_draw_ctrl;

#ifdef __cplusplus
extern "C" {
#endif

void InitDrawFLG(void);
void SetDrawFLGAllON(void);
void SetDrawFLGAllOFF(void);

/* STUB.  Game-over draw set: only the player and the fade quad stay visible.
 * player.c's PlyrDead() switches to it before the death camera runs. */
void SetDrawFLG_PL_GameOver(void);
void SetDrawFLG_SI_GameOver(void);
int GetPlyrDrawFLG(void);
void SetPlyrDrawFLG(int sw);
int GetSisDrawFLG(void);
void SetSisDrawFLG(int sw);
int GetEneDrawFLG(void);
void SetEneDrawFLG(int sw);
int GetEffDrawFLG(void);
void SetEffDrawFLG(int sw);
int GetRoomDrawFLG(void);
void SetRoomDrawFLG(int sw);
int GetObjDrawFLG(void);
void SetObjDrawFLG(int sw);
int GetSkyDrawFLG(void);
void SetSkyDrawFLG(int sw);
int GetSdwDrawFLG(void);
void SetSdwDrawFLG(int sw);
int GetSdwSrcDrawFLG(void);
void SetSdwSrcDrawFLG(int sw);

#ifdef __cplusplus
}
#endif

#endif /* _GRAPHICS_OBJ_DRAW_CTRL_H */
