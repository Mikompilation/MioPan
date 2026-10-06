// FILE: /home/zero_rom/zero2np/src/graphics/obj_draw_ctrl.c
//
// Object draw-control flags.
//
// Reconstructed from the Feb 6 2004 prototype (SLES_523.84).

#include "obj_draw_ctrl.h"

OBJ_DRAW_FLG obj_draw_ctrl = { 1, 1, 1, 1, 1, 1, 1, 1, 1 };

static int DrawFlagValue(int sw)
{
    return sw % 2;
}

void InitDrawFLG(void)
{
    SetDrawFLGAllON();
}

void SetDrawFLGAllON(void)
{
    SetPlyrDrawFLG(1);
    SetSisDrawFLG(1);
    SetEneDrawFLG(1);
    SetEffDrawFLG(1);
    SetRoomDrawFLG(1);
    SetObjDrawFLG(1);
    SetSkyDrawFLG(1);
    SetSdwDrawFLG(1);
    SetSdwSrcDrawFLG(1);
}

void SetDrawFLGAllOFF(void)
{
    SetPlyrDrawFLG(0);
    SetSisDrawFLG(0);
    SetEneDrawFLG(0);
    SetEffDrawFLG(0);
    SetRoomDrawFLG(0);
    SetObjDrawFLG(0);
    SetSkyDrawFLG(0);
    SetSdwDrawFLG(0);
    SetSdwSrcDrawFLG(0);
}

int GetPlyrDrawFLG(void)
{
    return obj_draw_ctrl.player;
}

void SetPlyrDrawFLG(int sw)
{
    obj_draw_ctrl.player = DrawFlagValue(sw);
}

int GetSisDrawFLG(void)
{
    return obj_draw_ctrl.sister;
}

void SetSisDrawFLG(int sw)
{
    obj_draw_ctrl.sister = DrawFlagValue(sw);
}

int GetEneDrawFLG(void)
{
    return obj_draw_ctrl.enemy;
}

void SetEneDrawFLG(int sw)
{
    obj_draw_ctrl.enemy = DrawFlagValue(sw);
}

int GetEffDrawFLG(void)
{
    return obj_draw_ctrl.effect;
}

void SetEffDrawFLG(int sw)
{
    obj_draw_ctrl.effect = DrawFlagValue(sw);
}

int GetRoomDrawFLG(void)
{
    return obj_draw_ctrl.room;
}

void SetRoomDrawFLG(int sw)
{
    obj_draw_ctrl.room = DrawFlagValue(sw);
}

int GetObjDrawFLG(void)
{
    return obj_draw_ctrl.obj;
}

void SetObjDrawFLG(int sw)
{
    obj_draw_ctrl.obj = DrawFlagValue(sw);
}

int GetSkyDrawFLG(void)
{
    return obj_draw_ctrl.sky;
}

void SetSkyDrawFLG(int sw)
{
    obj_draw_ctrl.sky = DrawFlagValue(sw);
}

int GetSdwDrawFLG(void)
{
    return obj_draw_ctrl.shadow;
}

void SetSdwDrawFLG(int sw)
{
    obj_draw_ctrl.shadow = DrawFlagValue(sw);
}

int GetSdwSrcDrawFLG(void)
{
    return obj_draw_ctrl.shadow_src;
}

void SetSdwSrcDrawFLG(int sw)
{
    obj_draw_ctrl.shadow_src = DrawFlagValue(sw);
}

void SetDrawFLG_PL_GameOver(void)
{
    SetPlyrDrawFLG(1);
    SetSisDrawFLG(0);
    SetEneDrawFLG(0);
    SetEffDrawFLG(0);
    SetRoomDrawFLG(0);
    SetObjDrawFLG(0);
    SetSkyDrawFLG(0);
    SetSdwDrawFLG(0);
    SetSdwSrcDrawFLG(0);
}

/* Companion game-over presentation: only the sister is drawn.  The room, the
 * enemies, the effects, the sky and both shadow passes are all turned off, so
 * the death camera frames her against the fade rectangle SisDead() draws. */
void SetDrawFLG_SI_GameOver(void)                                       /* 160 */
{
    SetPlyrDrawFLG(0);
    SetSisDrawFLG(1);
    SetEneDrawFLG(0);
    SetEffDrawFLG(0);
    SetRoomDrawFLG(0);
    SetObjDrawFLG(0);
    SetSkyDrawFLG(0);
    SetSdwDrawFLG(0);
    SetSdwSrcDrawFLG(0);                                                /* 169 */
}
