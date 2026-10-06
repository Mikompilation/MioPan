/* ==========================================================================
 *  ingame/event/prg/ev_macro.h
 *
 *  Event macro interpreter (ev_macro.c): the opcode set every event program
 *  is written in, and the interpreter that walks it.
 *
 *  An event program is a flat byte stream of commands.  The leading byte of a
 *  command is its EV_MACRO_LABEL; the operand width is not in the stream but
 *  in ev_exe_wrk[], so the interpreter advances by table lookup rather than by
 *  parsing.  EV_END (255) is not a table entry -- it ends the program, and
 *  which of the event's three programs just ended is inferred from the event's
 *  state.
 *
 *  The labels below are the ROM's own: ev_exe_wrk[] carries each one as a
 *  string for its debug prints, so the spelling here is observed rather than
 *  invented -- including EV_MAP_STREAM_CHAHGE, which is misspelt in the ROM.
 *
 *  Reconstructed from the Feb 6 2004 prototype (SLES_523.84).
 * ======================================================================== */

#ifndef _INGAME_EVENT_PRG_EV_MACRO_H
#define _INGAME_EVENT_PRG_EV_MACRO_H

#include "eetypes.h"

#include "ev_exe.h"                 /* EV_EXE_CTRL */
#include "../../../common/save_data.h"

/* --------------------------------------------------------------------------
 *  Opcodes
 * ------------------------------------------------------------------------ */

enum EV_MACRO_LABEL
{
    PLYR_POS_SET               =   0,
    PLYR_HEIGHT_SET            =   1,
    PLYR_ROT_SET               =   2,
    PLYR_POS_MOVE              =   3,
    PLYR_DISP                  =   4,
    PLYR_PAD                   =   5,
    PLYR_FLOOR_CHANGE          =   6,
    PLYR_FINDER_MODE           =   7,
    PLYR_MOTION_CALL           =   8,
    PLYR_FACIAL_CALL           =   9,
    PLYR_DAMAGE_REQUEST        =  10,
    PLYR_MOTION_CHANGE         =  11,
    PLYR_MODEL_CHANGE          =  12,
    PLYR_FLASHLIGHT_SET        =  13,
    PLYR_GAZE_POINT_OBJ_SET    =  14,
    PLYR_GAZE_POINT_POS_SET    =  15,
    PLYR_GAZE_POINT_DEF_SET    =  16,
    SIS_POS_SET                =  17,
    SIS_HEIGHT_SET             =  18,
    SIS_ROT_SET                =  19,
    SIS_POS_MOVE               =  20,
    SIS_DISP                   =  21,
    SIS_JOIN                   =  22,
    SIS_LEAVE                  =  23,
    SIS_FLOOR_CHANGE           =  24,
    SIS_REGIST                 =  25,
    SIS_DELETE                 =  26,
    SIS_MOTION_CALL            =  27,
    SIS_FACIAL_CALL            =  28,
    SIS_DAMAGE_REQUEST         =  29,
    SIS_GAZE_POINT_OBJ_SET     =  30,
    SIS_GAZE_POINT_POS_SET     =  31,
    SIS_GAZE_POINT_DEF_SET     =  32,
    GHOST_APPEAR               =  33,
    GHOST_DISAPPEAR            =  34,
    GHOST_REGIST               =  35,
    GHOST_DELETE               =  36,
    FLOATAGE_GHOST             =  37,
    LOCK_AREA_F_GHOST          =  38,
    UNLOCK_AREA_F_GHOST        =  39,
    NPC_POS_SET                =  40,
    NPC_HEIGHT_SET             =  41,
    NPC_ROT_SET                =  42,
    NPC_POS_MOVE               =  43,
    NPC_DISP                   =  44,
    NPC_FLOOR_CHANGE           =  45,
    SET_OBJ_HITCHECK           =  46,
    SET_OBJ_PHOTOABLE          =  47,
    SET_OBJ_EFFECT             =  48,
    SET_OBJ_VISIBLE            =  49,
    SET_OBJ_REQ_ACTION         =  50,
    SET_OBJ_ACTIONTYPE         =  51,
    PHOTO_LOCK                 =  52,
    PHOTO_UNLOCK               =  53,
    ITEM_GET                   =  54,
    ITEM_USE                   =  55,
    ITEM_LOST                  =  56,
    FILE_GET                   =  57,
    FILE_LOST                  =  58,
    FILE_READ                  =  59,
    CRYSTAL_GET                =  60,
    CRYSTAL_LOST               =  61,
    LEVELGEM_GET               =  62,
    LEVELGEM_LOST              =  63,
    CAM_SPECIALSHOT_GET        =  64,
    CAM_ADD_FUNCTION_GET       =  65,
    CAM_EQUIP_FUNCTION_GET     =  66,
    MEMO_UPDATE                =  67,
    PUZZLE_START               =  68,
    EV_CAM                     =  69,
    SET_EV_CAM_VCI             =  70,
    SET_EV_CAM_VP              =  71,
    SET_EV_CAM_VR              =  72,
    SET_EV_CAM_ROT             =  73,
    SET_EV_CAM_PROJ            =  74,
    EV_CAM_VP_SET_OBJ          =  75,
    EV_CAM_VR_SET_OBJ          =  76,
    EV_CAM_SET_WORLD_SWITCH    =  77,
    MONO_DISP                  =  78,
    FADE_IN                    =  79,
    FADE_OUT                   =  80,
    EV_SOUND_LOAD              =  81,
    EV_SOUND_RELEASE           =  82,
    EV_SOUND_PLAY              =  83,
    EV_SOUND_STOP              =  84,
    EV_SOUND3D_OBJ_PLAY        =  85,
    EV_SOUND3D_OBJ_STOP        =  86,
    EV_SOUND3D_POS_PLAY        =  87,
    EV_SOUND3D_POS_STOP        =  88,
    EV_STREAM_PLAY             =  89,
    EV_STREAM_STOP             =  90,
    EV_MAP_STREAM_PLAY         =  91,
    EV_MAP_STREAM_STOP         =  92,
    EV_MAP_STREAM_CHAHGE       =  93,
    EV_STREAM3D_OBJ_PLAY       =  94,
    EV_STREAM3D_OBJ_STOP       =  95,
    EV_STREAM3D_POS_PLAY       =  96,
    EV_STREAM3D_POS_STOP       =  97,
    EV_STREAM_ALL_STOP         =  98,
    EV_DOOR_OPEN               =  99,
    EV_DOOR_CLOSE              = 100,
    EV_DOOR_LOCK               = 101,
    EV_DOOR_UNLOCK             = 102,
    LOAD_REQUEST               = 103,
    RELEASE_REQUEST            = 104,
    AREA_CHANGE                = 105,
    BACK_GROUND_LOAD           = 106,
    AREA_SWITCH                = 107,
    MSG_DISP                   = 108,
    MSG_CHOICE                 = 109,
    TALK_TBL_INIT              = 110,
    TALK_DATA_ADD              = 111,
    TALK_SUBTITLE_ADD          = 112,
    TALK_TYPE_CHANGE           = 113,
    TALK_EXE                   = 114,
    TALK_CAM                   = 115,
    MOVIE_PLAY                 = 116,
    DISP2D_START               = 117,
    DISP2D_END                 = 118,
    CHAPTER_DISP_START         = 119,
    EV_FOG                     = 120,
    SET_EV_FOG_COLOR           = 121,
    SET_EV_FOG_DIST_NEAR       = 122,
    SET_EV_FOG_DIST_FAR        = 123,
    SET_EV_FOG_DIST            = 124,
    EV_OVER_LAP_START          = 125,
    EV_OVER_LAP_END            = 126,
    FILAMENT_TIMER_CALL        = 127,
    FILAMENT_CALL              = 128,
    FILAMENT_RELEASE           = 129,
    SET_OBJ_ALGORITHM          = 130,
    SET_PLYR_SIS_DISTANCE      = 131,
    EV_SET_STATE               = 132,
    EV_SET_TIMER               = 133,
    EV_STOP                    = 134,
    CHAPTER_LOAD_REQUEST       = 135,
    CHANGE_CHAPTER             = 136,
    GAME_DATA_SAVE             = 137,
    GAME_CLEAR                 = 138,
    SET_CONDITION_CHECK        = 139,
    SET_BUTTERFLY              = 140,
    MOVE_BUTTERFLY             = 141,
    RELEASE_BUTTERFLY          = 142,
    EV_EFF_DITHER_START        = 143,
    EV_EFF_DITHER_END          = 144,
    EV_EFF_THUNDER_REQ         = 145,
    EV_SET_SCREEN_EFFECT       = 146,
    SYNCHRO_MODE_START         = 147,
    SYNCHRO_MODE_END           = 148,
    ITEM_NAME_DISP_START       = 149,
    ITEM_NAME_DISP_END         = 150,
    MENU_LOCK                  = 151,
    MENU_UNLOCK                = 152,
    PAUSE_LOCK                 = 153,
    PAUSE_UNLOCK               = 154,
    SET_PHOTO_CURSE            = 155,
    EV_PAD_WAIT                = 156,
    EV_MOVIE_ROOM_REQUEST      = 157,
    GAMEOVER_REQUEST           = 158,
    SUBTITLE_DISP_REQ          = 159,
    SUBTITLE3D_OBJ_DISP_REQ    = 160,
    SUBTITLE3D_POS_DISP_REQ    = 161,
    SUBTITLE_STOP              = 162,
    SET_GHOST_SEAL_DOOR        = 163,
    RELEASE_GHOST_SEAL_DOOR    = 164,
    EV_MISSION_START           = 165,
    EV_MISSION_CLEAR           = 166,
    EV_MISSION_FAILED          = 167,
    CHAPTER_SEL_INIT           = 168,
    EV_IF                      = 169,
    EV_ELSE                    = 170,
    EV_ELSEIF                  = 171,
    EV_ENDIF                   = 172,

    EV_MACRO_LABEL_MAX          = 173,

    /* Not a table entry -- Event_EvEnd() handles it directly. */
    EV_END                      = 255
};

enum EV_IF_LABEL
{
    IF_ITEM_MAX_CHECK          = 0,
    IF_SELECT_CHOICE           = 1,
    IF_RANDOM_CHECK            = 2,
    IF_WITH_SISTER             = 3,
    IF_PLYR_FLASH_LIGHT_HAVE   = 4,
    IF_PUZZLE_CLEAR            = 5,
    IF_GAME_DIFFICULTY         = 6,
    IF_GAME_CLEAR_NUM          = 7,

    IF_COND_MAX                 = 8
};

/* --------------------------------------------------------------------------
 *  Interpreter
 * ------------------------------------------------------------------------ */

/* Reset every macro-owned work area.  EventExeInit() calls this. */
void EventMacroInit(void);

/* Restart the streams and screen effects recorded in the save block.  Called
 * once the memory-card data has been read back in -- the opcodes that started
 * them are long past and will not run again. */
void EventMacroLoadInit(void);

/* Step one entry's macro program: read the opcode at ctrl->event_addr, run
 * it, and advance the cursor.  Keeps going until a handler asks to be called
 * again next frame, or the program ends.  Driven per frame by EventExe(), and
 * once directly by SetEventInitStatus() for the init program. */
void EventExeFuncCall(EV_EXE_CTRL *ctrl);

/* Non-zero while SYNCHRO_MODE_START has run and SYNCHRO_MODE_END has not. */
char GetSynchroModeFlg(void);

/* --------------------------------------------------------------------------
 *  Event ghost registry
 *
 *  Which ghosts an event brought into the world, so the condition evaluator
 *  can ask whether one is still there.
 * ------------------------------------------------------------------------ */

void Set_EvGhostID(u_char ghost_type, int ghost_label, int wrk_id);
void Del_EvGhostID(u_char ghost_type, int ghost_label);
int  GetEvGhostExist(u_char ghost_type, int ghost_label);

void EvDbg_EventGhostPrint(void);

/* --------------------------------------------------------------------------
 *  Sound / stream control
 * ------------------------------------------------------------------------ */

/* Pause and resume the sounds this file started -- ingame.c drives these when
 * the menu opens and closes. */
void EvSoundPause(void);
void EvSoundRestart(void);

/* Fade out every stream the interpreter started.  Called from EventEnd(). */
void EventEnd_StreamRelease(void);

/* --------------------------------------------------------------------------
 *  Save blocks
 * ------------------------------------------------------------------------ */

void SetSave_EvSaveStream(MC_SAVE_DATA *data);
void SetSave_EvSaveObjStream(MC_SAVE_DATA *data);
void SetSave_EvSavePosStream(MC_SAVE_DATA *data);
void SetSave_EvSaveEffDither(MC_SAVE_DATA *data);
void SetSave_EvSaveScreenEffect(MC_SAVE_DATA *data);

#endif /* _INGAME_EVENT_PRG_EV_MACRO_H */
