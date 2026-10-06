/* ==========================================================================
 *  graphics/scene/IngameScene.h
 *
 *  In-game scene / movie playback phase interface.
 *
 *  Reconstructed from the Feb 6 2004 prototype (SLES_523.84).
 * ======================================================================== */

#ifndef _GRAPHICS_SCENE_INGAME_SCENE_H
#define _GRAPHICS_SCENE_INGAME_SCENE_H

#include "eetypes.h"
#include "../../main/gphase.h"

enum INGAME_SCENE_STATUS {
    INGAME_SCENE_STREAM_BACKUP = 0,
    INGAME_SCENE_DATA_LOAD_REQ = 1,
    INGAME_SCENE_DATA_LOAD_WAIT = 2,
    INGAME_SCENE_ROOM_LOAD = 3,
    INGAME_SCENE_PLAY = 4,
    INGAME_SCENE_PLAY_MOVIE = 5,
    INGAME_SCENE_DEBUG_SKIP = 6
};

enum INGAME_SCENE_ROOM_LOAD_STATUS {
    INGAME_SCENE_ROOM_LOAD_WAIT = 0,
    INGAME_SCENE_ROOM_SUB_LOAD_REQ = 1,
    INGAME_SCENE_ROOM_SUB_LOAD = 2,
    INGAME_SCENE_ROOM_MAIN_LOAD_REQ = 3,
    INGAME_SCENE_ROOM_MAIN_LOAD = 4
};

/* Member order is the ROM's (types.txt): pLoadAdrs sits at 0x4, between
 * SceneNo and Status.  Offsets past pLoadAdrs drift on the host because a
 * pointer is 8 bytes here and 4 on the EE; nothing outside this file depends
 * on the layout, so only the order is preserved. */
typedef struct              // 0x10 on target
{
    /* 0x0 */ int    SceneNo;
    /* 0x4 */ u_int *pLoadAdrs;
    /* 0x8 */ int    Status;
    /* 0xc */ int    RoomLoadStatus;
} INGAME_SCENE_CTRL;

GPHASE_ID_ENUM IngameSceneInit(int scene_no);

void        init_Story_Scene(void);
void        end_Story_Scene(void);
GPHASE_ENUM pre_Story_Scene(GPHASE_ENUM dummy);
GPHASE_ENUM after_Story_Scene(GPHASE_ENUM dummy);
void        init_Story_Scene_PreLoad(void);
void        end_Story_Scene_PreLoad(void);
GPHASE_ENUM one_Story_Scene_PreLoad(GPHASE_ENUM dummy);
void        init_Story_Scene_Main(void);
void        end_Story_Scene_Main(void);
GPHASE_ENUM one_Story_Scene_Main(GPHASE_ENUM dummy);

void        init_Story_Movie(void);
void        end_Story_Movie(void);
GPHASE_ENUM pre_Story_Movie(GPHASE_ENUM dummy);
GPHASE_ENUM after_Story_Movie(GPHASE_ENUM dummy);
void        init_Story_Movie_PreLoad(void);
void        end_Story_Movie_PreLoad(void);
GPHASE_ENUM one_Story_Movie_PreLoad(GPHASE_ENUM dummy);
void        init_Story_Movie_Main(void);
void        end_Story_Movie_Main(void);
GPHASE_ENUM one_Story_Movie_Main(GPHASE_ENUM dummy);

#endif /* _GRAPHICS_SCENE_INGAME_SCENE_H */
