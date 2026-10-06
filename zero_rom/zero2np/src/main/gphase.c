// FILE: /home/zero_rom/zero2np/src/main/gphase.c
//
// Global Phase System (GPhase) – hierarchical game-state machine.
// Six layers (0 = root "GID_SUPER", deeper = children), each holding
// a current phase ID.  Every frame GPhaseSysMain() calls SetInitFlag(),
// advances now[] <- next[], runs DoJobPhase(), then fires end_func for
// any layer whose phase just changed during the frame.

#include "gphase.h"             // GPHASE_ENUM / GPHASE_ID_ENUM / GPHASE_DAT / GPHASE_SYS
#include "phasefunc.h"          // extern prototypes for every per-phase callback
#include "gphase_trace.h"       // PORT-ONLY phase trace (no ROM equivalent)

// ──────────────────────────────────────────────────────────────────────
// Statics

// The four per-phase callback tables below are parallel arrays indexed by
// GPHASE_ID_ENUM (0 .. 93).  Their function-pointer element type is written
// with unspecified parameters `()` because the callbacks have heterogeneous
// prototypes (init/end take no args; pre/after receive a GPHASE_ENUM at the
// call site, which some handlers ignore) -- matching the "parameters
// unknown" element type recorded for these arrays in the debug symbols.
// The prototypes themselves come from phasefunc.h.

// Called once when a phase becomes active.
static void (*ini_func[94])() =
{
    /* GID_SUPER                    */ init_super,
    /* GID_BOOT_INIT                */ init_Boot_Init,
    /* GID_BOOT_PADCHECK            */ init_Boot_PadCheck,
    /* GID_LANGDATA_CHECK           */ init_LangData_Check,
    /* GID_LANGSEL_MAIN             */ init_LangSel_Main,
    /* GID_AUTOLOAD_MAIN            */ init_AutoLoad_Main,
    /* GID_UBI_MODE                 */ init_UBI_Mode,
    /* GID_OUTGAME_MAIN             */ init_OutGame_Main,
    /* GID_STORY_MAIN               */ init_Story_Main,
    /* GID_GAMEOVER_MENU            */ init_GameOver_Menu,
    /* GID_ENDING_MOVIE             */ init_Ending_Movie,
    /* GID_GAMERESULT               */ init_GameResult,
    /* GID_CLEARMENU                */ init_ClearMenu,
    /* GID_SOFTRESETMAIN            */ init_SoftResetMain,
    /* GID_DEBUG_MENU               */ init_Debug_Menu,
    /* GID_TECMO_MODE               */ init_Tecmo_Mode,
    /* GID_PROJECT_MODE             */ init_Project_Mode,
    /* GID_TITLE_MODE               */ init_Title_Mode,
    /* GID_TITLE_MOVIE_MODE         */ init_Title_Movie_Mode,
    /* GID_STORY_NOWLOADING         */ init_Story_NowLoading,
    /* GID_STORY_NORMAL             */ init_Story_Normal,
    /* GID_STORY_DAMAGE             */ init_Story_Damage,
    /* GID_STORY_DOOR_OPEN          */ init_Story_Door_Open,
    /* GID_STORY_DEBUG              */ init_Story_Debug,
    /* GID_STORY_DEBUG_CAM          */ init_Story_Debug_Cam,
    /* GID_STORY_PAUSE              */ init_Story_Pause,
    /* GID_STORY_PAUSE_MISSION      */ init_Story_Pause_Mission,
    /* GID_STORY_MENU               */ init_Story_Menu,
    /* GID_STORY_MAP                */ init_Story_Map,
    /* GID_STORY_MISSION_ST         */ init_Story_Mission_St,
    /* GID_STORY_MISSION_RESULT     */ init_Story_Mission_Result,
    /* GID_STORY_GAME_OVER_PRE      */ init_Story_Game_Over_Pre,
    /* GID_STORY_GAME_OVER          */ init_Story_Game_Over,
    /* GID_STORY_SCENE              */ init_Story_Scene,
    /* GID_STORY_MOVIE              */ init_Story_Movie,
    /* GID_STORY_EFFECT             */ init_Story_Effect,
    /* GID_EVENTMSG_DISP            */ init_EventMsg_Disp,
    /* GID_EVENTFILE_DISP           */ init_EventFile_Disp,
    /* GID_STORY_PHOTO              */ init_Story_Photo,
    /* GID_STORY_ENE_DEAD           */ init_Story_Ene_Dead,
    /* GID_STORY_PUZZLE             */ init_Story_Puzzle,
    /* GID_STORY_SAVEPOINT          */ init_Story_SavePoint,
    /* GID_STORY_MOVIE_ROOM_SEL     */ init_Story_Movie_Room_Sel,
    /* GID_GAMEOVER_MENU_TOP        */ init_GameOver_Menu_Top,
    /* GID_GAMEOVER_MENU_LOAD       */ init_GameOver_Menu_Load,
    /* GID_GAMEOVER_MENU_ALBUM      */ init_GameOver_Menu_Album,
    /* GID_ENDING_NORMAL1           */ init_Ending_Normal1,
    /* GID_ENDING_NORMAL2           */ init_Ending_Normal2,
    /* GID_ENDING_HARD              */ init_Ending_Hard,
    /* GID_GAMERESULT_TOP           */ init_GameResult_Top,
    /* GID_CLEARMENU_TOP            */ init_ClearMenu_Top,
    /* GID_CLEARMENU_SAVE           */ init_ClearMenu_Save,
    /* GID_CLEARMENU_ALBUM          */ init_ClearMenu_Album,
    /* GID_TITLE_TOP                */ init_Title_Top,
    /* GID_TITLE_MENU               */ init_Title_Menu,
    /* GID_TITLE_NEWGAME            */ init_Title_NewGame,
    /* GID_TITLE_LOADGAME           */ init_Title_LoadGame,
    /* GID_TITLE_SETUP              */ init_Title_Setup,
    /* GID_TITLE_ALBUM              */ init_Title_Album,
    /* GID_TITLE_GALLERY            */ init_Title_Gallery,
    /* GID_TITLE_OPTION             */ init_Title_Option,
    /* GID_TITLE_FRAMERATE_SEL      */ init_Title_FrameRate_Sel,
    /* GID_TITLE_CHAPTER_SEL        */ init_Title_Chapter_Sel,
    /* GID_TITLE_MOVE_MOVIE         */ init_Title_Move_Movie,
    /* GID_STORY_LOAD_MISSION       */ init_Story_Load_Mission,
    /* GID_STORY_LOAD_MISSION_EVENT */ init_Story_Load_Mission_Event,
    /* GID_STORY_LOAD_MISSION_SAVE  */ init_Story_Load_Mission_Save,
    /* GID_STORY_GAMEOVER_EFF       */ init_Story_GameOver_Eff,
    /* GID_STORY_GAMEOVER_FADE      */ init_Story_GameOver_Fade,
    /* GID_STORY_GAMEOVER_MOVIE     */ init_Story_GameOver_Movie,
    /* GID_STORY_SCENE_PRELOAD      */ init_Story_Scene_PreLoad,
    /* GID_STORY_SCENE_MAIN         */ init_Story_Scene_Main,
    /* GID_STORY_MOVIE_PRELOAD      */ init_Story_Movie_PreLoad,
    /* GID_STORY_MOVIE_MAIN         */ init_Story_Movie_Main,
    /* GID_PUZZLE_INCONF            */ init_Puzzle_InConf,
    /* GID_PUZZLE_CROSSFADE         */ init_Puzzle_CrossFade,
    /* GID_PUZZLE_HINA              */ init_Puzzle_Hina,
    /* GID_PUZZLE_ROKU              */ init_Puzzle_Roku,
    /* GID_PUZZLE_KAZA              */ init_Puzzle_Kaza,
    /* GID_PUZZLE_KAZA2             */ init_Puzzle_Kaza2,
    /* GID_PUZZLE_KAI1              */ init_Puzzle_Kai1,
    /* GID_PUZZLE_KAI2              */ init_Puzzle_Kai2,
    /* GID_SAVEPOINT_FADEIN         */ init_SavePoint_FadeIn,
    /* GID_SAVEPOINT_MAIN           */ init_SavePoint_Main,
    /* GID_SAVEPOINT_FADEOUT        */ init_SavePoint_FadeOut,
    /* GID_TITLE_SETUPMENU          */ init_Title_SetupMenu,
    /* GID_TITLE_MISSION            */ init_Title_Mission,
    /* GID_SAVEPOINT_TOP            */ init_SavePoint_Top,
    /* GID_SAVEPOINT_SAVE           */ init_SavePoint_Save,
    /* GID_SAVEPOINT_ALBUM          */ init_SavePoint_Album,
    /* GID_MISSION_SEL              */ init_Mission_Sel,
    /* GID_MISSION_CAM              */ init_Mission_Cam,
    /* GID_MISSION_ALBUM            */ init_Mission_Album,
    /* GID_MISSION_SAVE             */ init_Mission_Save,
};

// Called once when a phase is torn down.
static void (*end_func[94])() =
{
    /* GID_SUPER                    */ end_super,
    /* GID_BOOT_INIT                */ end_Boot_Init,
    /* GID_BOOT_PADCHECK            */ end_Boot_PadCheck,
    /* GID_LANGDATA_CHECK           */ end_LangData_Check,
    /* GID_LANGSEL_MAIN             */ end_LangSel_Main,
    /* GID_AUTOLOAD_MAIN            */ end_AutoLoad_Main,
    /* GID_UBI_MODE                 */ end_UBI_Mode,
    /* GID_OUTGAME_MAIN             */ end_OutGame_Main,
    /* GID_STORY_MAIN               */ end_Story_Main,
    /* GID_GAMEOVER_MENU            */ end_GameOver_Menu,
    /* GID_ENDING_MOVIE             */ end_Ending_Movie,
    /* GID_GAMERESULT               */ end_GameResult,
    /* GID_CLEARMENU                */ end_ClearMenu,
    /* GID_SOFTRESETMAIN            */ end_SoftResetMain,
    /* GID_DEBUG_MENU               */ end_Debug_Menu,
    /* GID_TECMO_MODE               */ end_Tecmo_Mode,
    /* GID_PROJECT_MODE             */ end_Project_Mode,
    /* GID_TITLE_MODE               */ end_Title_Mode,
    /* GID_TITLE_MOVIE_MODE         */ end_Title_Movie_Mode,
    /* GID_STORY_NOWLOADING         */ end_Story_NowLoading,
    /* GID_STORY_NORMAL             */ end_Story_Normal,
    /* GID_STORY_DAMAGE             */ end_Story_Damage,
    /* GID_STORY_DOOR_OPEN          */ end_Story_Door_Open,
    /* GID_STORY_DEBUG              */ end_Story_Debug,
    /* GID_STORY_DEBUG_CAM          */ end_Story_Debug_Cam,
    /* GID_STORY_PAUSE              */ end_Story_Pause,
    /* GID_STORY_PAUSE_MISSION      */ end_Story_Pause_Mission,
    /* GID_STORY_MENU               */ end_Story_Menu,
    /* GID_STORY_MAP                */ end_Story_Map,
    /* GID_STORY_MISSION_ST         */ end_Story_Mission_St,
    /* GID_STORY_MISSION_RESULT     */ end_Story_Mission_Result,
    /* GID_STORY_GAME_OVER_PRE      */ end_Story_Game_Over_Pre,
    /* GID_STORY_GAME_OVER          */ end_Story_Game_Over,
    /* GID_STORY_SCENE              */ end_Story_Scene,
    /* GID_STORY_MOVIE              */ end_Story_Movie,
    /* GID_STORY_EFFECT             */ end_Story_Effect,
    /* GID_EVENTMSG_DISP            */ end_EventMsg_Disp,
    /* GID_EVENTFILE_DISP           */ end_EventFile_Disp,
    /* GID_STORY_PHOTO              */ end_Story_Photo,
    /* GID_STORY_ENE_DEAD           */ end_Story_Ene_Dead,
    /* GID_STORY_PUZZLE             */ end_Story_Puzzle,
    /* GID_STORY_SAVEPOINT          */ end_Story_SavePoint,
    /* GID_STORY_MOVIE_ROOM_SEL     */ end_Story_Movie_Room_Sel,
    /* GID_GAMEOVER_MENU_TOP        */ end_GameOver_Menu_Top,
    /* GID_GAMEOVER_MENU_LOAD       */ end_GameOver_Menu_Load,
    /* GID_GAMEOVER_MENU_ALBUM      */ end_GameOver_Menu_Album,
    /* GID_ENDING_NORMAL1           */ end_Ending_Normal1,
    /* GID_ENDING_NORMAL2           */ end_Ending_Normal2,
    /* GID_ENDING_HARD              */ end_Ending_Hard,
    /* GID_GAMERESULT_TOP           */ end_GameResult_Top,
    /* GID_CLEARMENU_TOP            */ end_ClearMenu_Top,
    /* GID_CLEARMENU_SAVE           */ end_ClearMenu_Save,
    /* GID_CLEARMENU_ALBUM          */ end_ClearMenu_Album,
    /* GID_TITLE_TOP                */ end_Title_Top,
    /* GID_TITLE_MENU               */ end_Title_Menu,
    /* GID_TITLE_NEWGAME            */ end_Title_NewGame,
    /* GID_TITLE_LOADGAME           */ end_Title_LoadGame,
    /* GID_TITLE_SETUP              */ end_Title_Setup,
    /* GID_TITLE_ALBUM              */ end_Title_Album,
    /* GID_TITLE_GALLERY            */ end_Title_Gallery,
    /* GID_TITLE_OPTION             */ end_Title_Option,
    /* GID_TITLE_FRAMERATE_SEL      */ end_Title_FrameRate_Sel,
    /* GID_TITLE_CHAPTER_SEL        */ end_Title_Chapter_Sel,
    /* GID_TITLE_MOVE_MOVIE         */ end_Title_Move_Movie,
    /* GID_STORY_LOAD_MISSION       */ end_Story_Load_Mission,
    /* GID_STORY_LOAD_MISSION_EVENT */ end_Story_Load_Mission_Event,
    /* GID_STORY_LOAD_MISSION_SAVE  */ end_Story_Load_Mission_Save,
    /* GID_STORY_GAMEOVER_EFF       */ end_Story_GameOver_Eff,
    /* GID_STORY_GAMEOVER_FADE      */ end_Story_GameOver_Fade,
    /* GID_STORY_GAMEOVER_MOVIE     */ end_Story_GameOver_Movie,
    /* GID_STORY_SCENE_PRELOAD      */ end_Story_Scene_PreLoad,
    /* GID_STORY_SCENE_MAIN         */ end_Story_Scene_Main,
    /* GID_STORY_MOVIE_PRELOAD      */ end_Story_Movie_PreLoad,
    /* GID_STORY_MOVIE_MAIN         */ end_Story_Movie_Main,
    /* GID_PUZZLE_INCONF            */ end_Puzzle_InConf,
    /* GID_PUZZLE_CROSSFADE         */ end_Puzzle_CrossFade,
    /* GID_PUZZLE_HINA              */ end_Puzzle_Hina,
    /* GID_PUZZLE_ROKU              */ end_Puzzle_Roku,
    /* GID_PUZZLE_KAZA              */ end_Puzzle_Kaza,
    /* GID_PUZZLE_KAZA2             */ end_Puzzle_Kaza2,
    /* GID_PUZZLE_KAI1              */ end_Puzzle_Kai1,
    /* GID_PUZZLE_KAI2              */ end_Puzzle_Kai2,
    /* GID_SAVEPOINT_FADEIN         */ end_SavePoint_FadeIn,
    /* GID_SAVEPOINT_MAIN           */ end_SavePoint_Main,
    /* GID_SAVEPOINT_FADEOUT        */ end_SavePoint_FadeOut,
    /* GID_TITLE_SETUPMENU          */ end_Title_SetupMenu,
    /* GID_TITLE_MISSION            */ end_Title_Mission,
    /* GID_SAVEPOINT_TOP            */ end_SavePoint_Top,
    /* GID_SAVEPOINT_SAVE           */ end_SavePoint_Save,
    /* GID_SAVEPOINT_ALBUM          */ end_SavePoint_Album,
    /* GID_MISSION_SEL              */ end_Mission_Sel,
    /* GID_MISSION_CAM              */ end_Mission_Cam,
    /* GID_MISSION_ALBUM            */ end_Mission_Album,
    /* GID_MISSION_SAVE             */ end_Mission_Save,
};

// Optional per-frame hook run before the child layers (NULL when unused).
static GPHASE_ENUM (*pre_func[94])(GPHASE_ENUM) =
{
    /* GID_SUPER                    */ pre_super,
    /* GID_BOOT_INIT                */ NULL,
    /* GID_BOOT_PADCHECK            */ NULL,
    /* GID_LANGDATA_CHECK           */ NULL,
    /* GID_LANGSEL_MAIN             */ NULL,
    /* GID_AUTOLOAD_MAIN            */ NULL,
    /* GID_UBI_MODE                 */ NULL,
    /* GID_OUTGAME_MAIN             */ pre_OutGame_Main,
    /* GID_STORY_MAIN               */ pre_Story_Main,
    /* GID_GAMEOVER_MENU            */ pre_GameOver_Menu,
    /* GID_ENDING_MOVIE             */ pre_Ending_Movie,
    /* GID_GAMERESULT               */ pre_GameResult,
    /* GID_CLEARMENU                */ pre_ClearMenu,
    /* GID_SOFTRESETMAIN            */ NULL,
    /* GID_DEBUG_MENU               */ NULL,
    /* GID_TECMO_MODE               */ NULL,
    /* GID_PROJECT_MODE             */ NULL,
    /* GID_TITLE_MODE               */ pre_Title_Mode,
    /* GID_TITLE_MOVIE_MODE         */ NULL,
    /* GID_STORY_NOWLOADING         */ pre_Story_NowLoading,
    /* GID_STORY_NORMAL             */ NULL,
    /* GID_STORY_DAMAGE             */ NULL,
    /* GID_STORY_DOOR_OPEN          */ NULL,
    /* GID_STORY_DEBUG              */ NULL,
    /* GID_STORY_DEBUG_CAM          */ NULL,
    /* GID_STORY_PAUSE              */ NULL,
    /* GID_STORY_PAUSE_MISSION      */ NULL,
    /* GID_STORY_MENU               */ NULL,
    /* GID_STORY_MAP                */ NULL,
    /* GID_STORY_MISSION_ST         */ NULL,
    /* GID_STORY_MISSION_RESULT     */ NULL,
    /* GID_STORY_GAME_OVER_PRE      */ NULL,
    /* GID_STORY_GAME_OVER          */ pre_Story_Game_Over,
    /* GID_STORY_SCENE              */ pre_Story_Scene,
    /* GID_STORY_MOVIE              */ pre_Story_Movie,
    /* GID_STORY_EFFECT             */ NULL,
    /* GID_EVENTMSG_DISP            */ NULL,
    /* GID_EVENTFILE_DISP           */ NULL,
    /* GID_STORY_PHOTO              */ NULL,
    /* GID_STORY_ENE_DEAD           */ NULL,
    /* GID_STORY_PUZZLE             */ pre_Story_Puzzle,
    /* GID_STORY_SAVEPOINT          */ pre_Story_SavePoint,
    /* GID_STORY_MOVIE_ROOM_SEL     */ NULL,
    /* GID_GAMEOVER_MENU_TOP        */ NULL,
    /* GID_GAMEOVER_MENU_LOAD       */ NULL,
    /* GID_GAMEOVER_MENU_ALBUM      */ NULL,
    /* GID_ENDING_NORMAL1           */ NULL,
    /* GID_ENDING_NORMAL2           */ NULL,
    /* GID_ENDING_HARD              */ NULL,
    /* GID_GAMERESULT_TOP           */ NULL,
    /* GID_CLEARMENU_TOP            */ NULL,
    /* GID_CLEARMENU_SAVE           */ NULL,
    /* GID_CLEARMENU_ALBUM          */ NULL,
    /* GID_TITLE_TOP                */ NULL,
    /* GID_TITLE_MENU               */ NULL,
    /* GID_TITLE_NEWGAME            */ NULL,
    /* GID_TITLE_LOADGAME           */ NULL,
    /* GID_TITLE_SETUP              */ pre_Title_Setup,
    /* GID_TITLE_ALBUM              */ NULL,
    /* GID_TITLE_GALLERY            */ NULL,
    /* GID_TITLE_OPTION             */ NULL,
    /* GID_TITLE_FRAMERATE_SEL      */ NULL,
    /* GID_TITLE_CHAPTER_SEL        */ NULL,
    /* GID_TITLE_MOVE_MOVIE         */ NULL,
    /* GID_STORY_LOAD_MISSION       */ NULL,
    /* GID_STORY_LOAD_MISSION_EVENT */ NULL,
    /* GID_STORY_LOAD_MISSION_SAVE  */ NULL,
    /* GID_STORY_GAMEOVER_EFF       */ NULL,
    /* GID_STORY_GAMEOVER_FADE      */ NULL,
    /* GID_STORY_GAMEOVER_MOVIE     */ NULL,
    /* GID_STORY_SCENE_PRELOAD      */ NULL,
    /* GID_STORY_SCENE_MAIN         */ NULL,
    /* GID_STORY_MOVIE_PRELOAD      */ NULL,
    /* GID_STORY_MOVIE_MAIN         */ NULL,
    /* GID_PUZZLE_INCONF            */ NULL,
    /* GID_PUZZLE_CROSSFADE         */ NULL,
    /* GID_PUZZLE_HINA              */ NULL,
    /* GID_PUZZLE_ROKU              */ NULL,
    /* GID_PUZZLE_KAZA              */ NULL,
    /* GID_PUZZLE_KAZA2             */ NULL,
    /* GID_PUZZLE_KAI1              */ NULL,
    /* GID_PUZZLE_KAI2              */ NULL,
    /* GID_SAVEPOINT_FADEIN         */ NULL,
    /* GID_SAVEPOINT_MAIN           */ pre_SavePoint_Main,
    /* GID_SAVEPOINT_FADEOUT        */ NULL,
    /* GID_TITLE_SETUPMENU          */ NULL,
    /* GID_TITLE_MISSION            */ pre_Title_Mission,
    /* GID_SAVEPOINT_TOP            */ NULL,
    /* GID_SAVEPOINT_SAVE           */ NULL,
    /* GID_SAVEPOINT_ALBUM          */ NULL,
    /* GID_MISSION_SEL              */ NULL,
    /* GID_MISSION_CAM              */ NULL,
    /* GID_MISSION_ALBUM            */ NULL,
    /* GID_MISSION_SAVE             */ NULL
};

// Per-frame hook run after the child layers; receives (and may escalate)
// the child result.
static GPHASE_ENUM (*after_func[94])(GPHASE_ENUM) =
{
    /* GID_SUPER                    */ after_super,
    /* GID_BOOT_INIT                */ one_Boot_Init,
    /* GID_BOOT_PADCHECK            */ one_Boot_PadCheck,
    /* GID_LANGDATA_CHECK           */ one_LangData_Check,
    /* GID_LANGSEL_MAIN             */ one_LangSel_Main,
    /* GID_AUTOLOAD_MAIN            */ one_AutoLoad_Main,
    /* GID_UBI_MODE                 */ one_UBI_Mode,
    /* GID_OUTGAME_MAIN             */ after_OutGame_Main,
    /* GID_STORY_MAIN               */ after_Story_Main,
    /* GID_GAMEOVER_MENU            */ after_GameOver_Menu,
    /* GID_ENDING_MOVIE             */ after_Ending_Movie,
    /* GID_GAMERESULT               */ after_GameResult,
    /* GID_CLEARMENU                */ after_ClearMenu,
    /* GID_SOFTRESETMAIN            */ one_SoftResetMain,
    /* GID_DEBUG_MENU               */ one_Debug_Menu,
    /* GID_TECMO_MODE               */ one_Tecmo_Mode,
    /* GID_PROJECT_MODE             */ one_Project_Mode,
    /* GID_TITLE_MODE               */ after_Title_Mode,
    /* GID_TITLE_MOVIE_MODE         */ one_Title_Movie_Mode,
    /* GID_STORY_NOWLOADING         */ after_Story_NowLoading,
    /* GID_STORY_NORMAL             */ one_Story_Normal,
    /* GID_STORY_DAMAGE             */ one_Story_Damage,
    /* GID_STORY_DOOR_OPEN          */ one_Story_Door_Open,
    /* GID_STORY_DEBUG              */ one_Story_Debug,
    /* GID_STORY_DEBUG_CAM          */ one_Story_Debug_Cam,
    /* GID_STORY_PAUSE              */ one_Story_Pause,
    /* GID_STORY_PAUSE_MISSION      */ one_Story_Pause_Mission,
    /* GID_STORY_MENU               */ one_Story_Menu,
    /* GID_STORY_MAP                */ one_Story_Map,
    /* GID_STORY_MISSION_ST         */ one_Story_Mission_St,
    /* GID_STORY_MISSION_RESULT     */ one_Story_Mission_Result,
    /* GID_STORY_GAME_OVER_PRE      */ one_Story_Game_Over_Pre,
    /* GID_STORY_GAME_OVER          */ after_Story_Game_Over,
    /* GID_STORY_SCENE              */ after_Story_Scene,
    /* GID_STORY_MOVIE              */ after_Story_Movie,
    /* GID_STORY_EFFECT             */ one_Story_Effect,
    /* GID_EVENTMSG_DISP            */ one_EventMsg_Disp,
    /* GID_EVENTFILE_DISP           */ one_EventFile_Disp,
    /* GID_STORY_PHOTO              */ one_Story_Photo,
    /* GID_STORY_ENE_DEAD           */ one_Story_Ene_Dead,
    /* GID_STORY_PUZZLE             */ after_Story_Puzzle,
    /* GID_STORY_SAVEPOINT          */ after_Story_SavePoint,
    /* GID_STORY_MOVIE_ROOM_SEL     */ one_Story_Movie_Room_Sel,
    /* GID_GAMEOVER_MENU_TOP        */ one_GameOver_Menu_Top,
    /* GID_GAMEOVER_MENU_LOAD       */ one_GameOver_Menu_Load,
    /* GID_GAMEOVER_MENU_ALBUM      */ one_GameOver_Menu_Album,
    /* GID_ENDING_NORMAL1           */ one_Ending_Normal1,
    /* GID_ENDING_NORMAL2           */ one_Ending_Normal2,
    /* GID_ENDING_HARD              */ one_Ending_Hard,
    /* GID_GAMERESULT_TOP           */ one_GameResult_Top,
    /* GID_CLEARMENU_TOP            */ one_ClearMenu_Top,
    /* GID_CLEARMENU_SAVE           */ one_ClearMenu_Save,
    /* GID_CLEARMENU_ALBUM          */ one_ClearMenu_Album,
    /* GID_TITLE_TOP                */ one_Title_Top,
    /* GID_TITLE_MENU               */ one_Title_Menu,
    /* GID_TITLE_NEWGAME            */ one_Title_NewGame,
    /* GID_TITLE_LOADGAME           */ one_Title_LoadGame,
    /* GID_TITLE_SETUP              */ after_Title_Setup,
    /* GID_TITLE_ALBUM              */ one_Title_Album,
    /* GID_TITLE_GALLERY            */ one_Title_Gallery,
    /* GID_TITLE_OPTION             */ one_Title_Option,
    /* GID_TITLE_FRAMERATE_SEL      */ one_Title_FrameRate_Sel,
    /* GID_TITLE_CHAPTER_SEL        */ one_Title_Chapter_Sel,
    /* GID_TITLE_MOVE_MOVIE         */ one_Title_Move_Movie,
    /* GID_STORY_LOAD_MISSION       */ one_Story_Load_Mission,
    /* GID_STORY_LOAD_MISSION_EVENT */ one_Story_Load_Mission_Event,
    /* GID_STORY_LOAD_MISSION_SAVE  */ one_Story_Load_Mission_Save,
    /* GID_STORY_GAMEOVER_EFF       */ one_Story_GameOver_Eff,
    /* GID_STORY_GAMEOVER_FADE      */ one_Story_GameOver_Fade,
    /* GID_STORY_GAMEOVER_MOVIE     */ one_Story_GameOver_Movie,
    /* GID_STORY_SCENE_PRELOAD      */ one_Story_Scene_PreLoad,
    /* GID_STORY_SCENE_MAIN         */ one_Story_Scene_Main,
    /* GID_STORY_MOVIE_PRELOAD      */ one_Story_Movie_PreLoad,
    /* GID_STORY_MOVIE_MAIN         */ one_Story_Movie_Main,
    /* GID_PUZZLE_INCONF            */ one_Puzzle_InConf,
    /* GID_PUZZLE_CROSSFADE         */ one_Puzzle_CrossFade,
    /* GID_PUZZLE_HINA              */ one_Puzzle_Hina,
    /* GID_PUZZLE_ROKU              */ one_Puzzle_Roku,
    /* GID_PUZZLE_KAZA              */ one_Puzzle_Kaza,
    /* GID_PUZZLE_KAZA2             */ one_Puzzle_Kaza2,
    /* GID_PUZZLE_KAI1              */ one_Puzzle_Kai1,
    /* GID_PUZZLE_KAI2              */ one_Puzzle_Kai2,
    /* GID_SAVEPOINT_FADEIN         */ one_SavePoint_FadeIn,
    /* GID_SAVEPOINT_MAIN           */ after_SavePoint_Main,
    /* GID_SAVEPOINT_FADEOUT        */ one_SavePoint_FadeOut,
    /* GID_TITLE_SETUPMENU          */ one_Title_SetupMenu,
    /* GID_TITLE_MISSION            */ after_Title_Mission,
    /* GID_SAVEPOINT_TOP            */ one_SavePoint_Top,
    /* GID_SAVEPOINT_SAVE           */ one_SavePoint_Save,
    /* GID_SAVEPOINT_ALBUM          */ one_SavePoint_Album,
    /* GID_MISSION_SEL              */ one_Mission_Sel,
    /* GID_MISSION_CAM              */ one_Mission_Cam,
    /* GID_MISSION_ALBUM            */ one_Mission_Album,
    /* GID_MISSION_SAVE             */ one_Mission_Save
};

// Per-phase tree configuration: { layer, superID, son_ID, son_num }, where
// superID is the parent phase, son_ID the first child, son_num the child
// count (0 for a leaf).
static GPHASE_DAT gphase_tbl[94] =
{
    /* GID_SUPER                    */ { 0, GPHASE_ID_NONE,          GID_BOOT_INIT,           14 },
    /* GID_BOOT_INIT                */ { 1, GID_SUPER,               GPHASE_ID_NONE,           0 },
    /* GID_BOOT_PADCHECK            */ { 1, GID_SUPER,               GPHASE_ID_NONE,           0 },
    /* GID_LANGDATA_CHECK           */ { 1, GID_SUPER,               GPHASE_ID_NONE,           0 },
    /* GID_LANGSEL_MAIN             */ { 1, GID_SUPER,               GPHASE_ID_NONE,           0 },
    /* GID_AUTOLOAD_MAIN            */ { 1, GID_SUPER,               GPHASE_ID_NONE,           0 },
    /* GID_UBI_MODE                 */ { 1, GID_SUPER,               GPHASE_ID_NONE,           0 },
    /* GID_OUTGAME_MAIN             */ { 1, GID_SUPER,               GID_TECMO_MODE,           4 },
    /* GID_STORY_MAIN               */ { 1, GID_SUPER,               GID_STORY_NOWLOADING,    24 },
    /* GID_GAMEOVER_MENU            */ { 1, GID_SUPER,               GID_GAMEOVER_MENU_TOP,    3 },
    /* GID_ENDING_MOVIE             */ { 1, GID_SUPER,               GID_ENDING_NORMAL1,       3 },
    /* GID_GAMERESULT               */ { 1, GID_SUPER,               GID_GAMERESULT_TOP,       1 },
    /* GID_CLEARMENU                */ { 1, GID_SUPER,               GID_CLEARMENU_TOP,        3 },
    /* GID_SOFTRESETMAIN            */ { 1, GID_SUPER,               GPHASE_ID_NONE,           0 },
    /* GID_DEBUG_MENU               */ { 1, GID_SUPER,               GPHASE_ID_NONE,           0 },
    /* GID_TECMO_MODE               */ { 2, GID_OUTGAME_MAIN,        GPHASE_ID_NONE,           0 },
    /* GID_PROJECT_MODE             */ { 2, GID_OUTGAME_MAIN,        GPHASE_ID_NONE,           0 },
    /* GID_TITLE_MODE               */ { 2, GID_OUTGAME_MAIN,        GID_TITLE_TOP,           11 },
    /* GID_TITLE_MOVIE_MODE         */ { 2, GID_OUTGAME_MAIN,        GPHASE_ID_NONE,           0 },
    /* GID_STORY_NOWLOADING         */ { 2, GID_STORY_MAIN,          GID_STORY_LOAD_MISSION,   3 },
    /* GID_STORY_NORMAL             */ { 2, GID_STORY_MAIN,          GPHASE_ID_NONE,           0 },
    /* GID_STORY_DAMAGE             */ { 2, GID_STORY_MAIN,          GPHASE_ID_NONE,           0 },
    /* GID_STORY_DOOR_OPEN          */ { 2, GID_STORY_MAIN,          GPHASE_ID_NONE,           0 },
    /* GID_STORY_DEBUG              */ { 2, GID_STORY_MAIN,          GPHASE_ID_NONE,           0 },
    /* GID_STORY_DEBUG_CAM          */ { 2, GID_STORY_MAIN,          GPHASE_ID_NONE,           0 },
    /* GID_STORY_PAUSE              */ { 2, GID_STORY_MAIN,          GPHASE_ID_NONE,           0 },
    /* GID_STORY_PAUSE_MISSION      */ { 2, GID_STORY_MAIN,          GPHASE_ID_NONE,           0 },
    /* GID_STORY_MENU               */ { 2, GID_STORY_MAIN,          GPHASE_ID_NONE,           0 },
    /* GID_STORY_MAP                */ { 2, GID_STORY_MAIN,          GPHASE_ID_NONE,           0 },
    /* GID_STORY_MISSION_ST         */ { 2, GID_STORY_MAIN,          GPHASE_ID_NONE,           0 },
    /* GID_STORY_MISSION_RESULT     */ { 2, GID_STORY_MAIN,          GPHASE_ID_NONE,           0 },
    /* GID_STORY_GAME_OVER_PRE      */ { 2, GID_STORY_MAIN,          GPHASE_ID_NONE,           0 },
    /* GID_STORY_GAME_OVER          */ { 2, GID_STORY_MAIN,          GID_STORY_GAMEOVER_EFF,   3 },
    /* GID_STORY_SCENE              */ { 2, GID_STORY_MAIN,          GID_STORY_SCENE_PRELOAD,  2 },
    /* GID_STORY_MOVIE              */ { 2, GID_STORY_MAIN,          GID_STORY_MOVIE_PRELOAD,  2 },
    /* GID_STORY_EFFECT             */ { 2, GID_STORY_MAIN,          GPHASE_ID_NONE,           0 },
    /* GID_EVENTMSG_DISP            */ { 2, GID_STORY_MAIN,          GPHASE_ID_NONE,           0 },
    /* GID_EVENTFILE_DISP           */ { 2, GID_STORY_MAIN,          GPHASE_ID_NONE,           0 },
    /* GID_STORY_PHOTO              */ { 2, GID_STORY_MAIN,          GPHASE_ID_NONE,           0 },
    /* GID_STORY_ENE_DEAD           */ { 2, GID_STORY_MAIN,          GPHASE_ID_NONE,           0 },
    /* GID_STORY_PUZZLE             */ { 2, GID_STORY_MAIN,          GID_PUZZLE_INCONF,        8 },
    /* GID_STORY_SAVEPOINT          */ { 2, GID_STORY_MAIN,          GID_SAVEPOINT_FADEIN,     3 },
    /* GID_STORY_MOVIE_ROOM_SEL     */ { 2, GID_STORY_MAIN,          GPHASE_ID_NONE,           0 },
    /* GID_GAMEOVER_MENU_TOP        */ { 2, GID_GAMEOVER_MENU,       GPHASE_ID_NONE,           0 },
    /* GID_GAMEOVER_MENU_LOAD       */ { 2, GID_GAMEOVER_MENU,       GPHASE_ID_NONE,           0 },
    /* GID_GAMEOVER_MENU_ALBUM      */ { 2, GID_GAMEOVER_MENU,       GPHASE_ID_NONE,           0 },
    /* GID_ENDING_NORMAL1           */ { 2, GID_ENDING_MOVIE,        GPHASE_ID_NONE,           0 },
    /* GID_ENDING_NORMAL2           */ { 2, GID_ENDING_MOVIE,        GPHASE_ID_NONE,           0 },
    /* GID_ENDING_HARD              */ { 2, GID_ENDING_MOVIE,        GPHASE_ID_NONE,           0 },
    /* GID_GAMERESULT_TOP           */ { 2, GID_GAMERESULT,          GPHASE_ID_NONE,           0 },
    /* GID_CLEARMENU_TOP            */ { 2, GID_CLEARMENU,           GPHASE_ID_NONE,           0 },
    /* GID_CLEARMENU_SAVE           */ { 2, GID_CLEARMENU,           GPHASE_ID_NONE,           0 },
    /* GID_CLEARMENU_ALBUM          */ { 2, GID_CLEARMENU,           GPHASE_ID_NONE,           0 },
    /* GID_TITLE_TOP                */ { 3, GID_TITLE_MODE,          GPHASE_ID_NONE,           0 },
    /* GID_TITLE_MENU               */ { 3, GID_TITLE_MODE,          GPHASE_ID_NONE,           0 },
    /* GID_TITLE_NEWGAME            */ { 3, GID_TITLE_MODE,          GPHASE_ID_NONE,           0 },
    /* GID_TITLE_LOADGAME           */ { 3, GID_TITLE_MODE,          GPHASE_ID_NONE,           0 },
    /* GID_TITLE_SETUP              */ { 3, GID_TITLE_MODE,          GID_TITLE_SETUPMENU,      2 },
    /* GID_TITLE_ALBUM              */ { 3, GID_TITLE_MODE,          GPHASE_ID_NONE,           0 },
    /* GID_TITLE_GALLERY            */ { 3, GID_TITLE_MODE,          GPHASE_ID_NONE,           0 },
    /* GID_TITLE_OPTION             */ { 3, GID_TITLE_MODE,          GPHASE_ID_NONE,           0 },
    /* GID_TITLE_FRAMERATE_SEL      */ { 3, GID_TITLE_MODE,          GPHASE_ID_NONE,           0 },
    /* GID_TITLE_CHAPTER_SEL        */ { 3, GID_TITLE_MODE,          GPHASE_ID_NONE,           0 },
    /* GID_TITLE_MOVE_MOVIE         */ { 3, GID_TITLE_MODE,          GPHASE_ID_NONE,           0 },
    /* GID_STORY_LOAD_MISSION       */ { 3, GID_STORY_NOWLOADING,    GPHASE_ID_NONE,           0 },
    /* GID_STORY_LOAD_MISSION_EVENT */ { 3, GID_STORY_NOWLOADING,    GPHASE_ID_NONE,           0 },
    /* GID_STORY_LOAD_MISSION_SAVE  */ { 3, GID_STORY_NOWLOADING,    GPHASE_ID_NONE,           0 },
    /* GID_STORY_GAMEOVER_EFF       */ { 3, GID_STORY_GAME_OVER,     GPHASE_ID_NONE,           0 },
    /* GID_STORY_GAMEOVER_FADE      */ { 3, GID_STORY_GAME_OVER,     GPHASE_ID_NONE,           0 },
    /* GID_STORY_GAMEOVER_MOVIE     */ { 3, GID_STORY_GAME_OVER,     GPHASE_ID_NONE,           0 },
    /* GID_STORY_SCENE_PRELOAD      */ { 3, GID_STORY_SCENE,         GPHASE_ID_NONE,           0 },
    /* GID_STORY_SCENE_MAIN         */ { 3, GID_STORY_SCENE,         GPHASE_ID_NONE,           0 },
    /* GID_STORY_MOVIE_PRELOAD      */ { 3, GID_STORY_MOVIE,         GPHASE_ID_NONE,           0 },
    /* GID_STORY_MOVIE_MAIN         */ { 3, GID_STORY_MOVIE,         GPHASE_ID_NONE,           0 },
    /* GID_PUZZLE_INCONF            */ { 3, GID_STORY_PUZZLE,        GPHASE_ID_NONE,           0 },
    /* GID_PUZZLE_CROSSFADE         */ { 3, GID_STORY_PUZZLE,        GPHASE_ID_NONE,           0 },
    /* GID_PUZZLE_HINA              */ { 3, GID_STORY_PUZZLE,        GPHASE_ID_NONE,           0 },
    /* GID_PUZZLE_ROKU              */ { 3, GID_STORY_PUZZLE,        GPHASE_ID_NONE,           0 },
    /* GID_PUZZLE_KAZA              */ { 3, GID_STORY_PUZZLE,        GPHASE_ID_NONE,           0 },
    /* GID_PUZZLE_KAZA2             */ { 3, GID_STORY_PUZZLE,        GPHASE_ID_NONE,           0 },
    /* GID_PUZZLE_KAI1              */ { 3, GID_STORY_PUZZLE,        GPHASE_ID_NONE,           0 },
    /* GID_PUZZLE_KAI2              */ { 3, GID_STORY_PUZZLE,        GPHASE_ID_NONE,           0 },
    /* GID_SAVEPOINT_FADEIN         */ { 3, GID_STORY_SAVEPOINT,     GPHASE_ID_NONE,           0 },
    /* GID_SAVEPOINT_MAIN           */ { 3, GID_STORY_SAVEPOINT,     GID_SAVEPOINT_TOP,        3 },
    /* GID_SAVEPOINT_FADEOUT        */ { 3, GID_STORY_SAVEPOINT,     GPHASE_ID_NONE,           0 },
    /* GID_TITLE_SETUPMENU          */ { 4, GID_TITLE_SETUP,         GPHASE_ID_NONE,           0 },
    /* GID_TITLE_MISSION            */ { 4, GID_TITLE_SETUP,         GID_MISSION_SEL,          4 },
    /* GID_SAVEPOINT_TOP            */ { 4, GID_SAVEPOINT_MAIN,      GPHASE_ID_NONE,           0 },
    /* GID_SAVEPOINT_SAVE           */ { 4, GID_SAVEPOINT_MAIN,      GPHASE_ID_NONE,           0 },
    /* GID_SAVEPOINT_ALBUM          */ { 4, GID_SAVEPOINT_MAIN,      GPHASE_ID_NONE,           0 },
    /* GID_MISSION_SEL              */ { 5, GID_TITLE_MISSION,       GPHASE_ID_NONE,           0 },
    /* GID_MISSION_CAM              */ { 5, GID_TITLE_MISSION,       GPHASE_ID_NONE,           0 },
    /* GID_MISSION_ALBUM            */ { 5, GID_TITLE_MISSION,       GPHASE_ID_NONE,           0 },
    /* GID_MISSION_SAVE             */ { 5, GID_TITLE_MISSION,       GPHASE_ID_NONE,           0 }
};

static GPHASE_SYS       gphase_sys;             // runtime state

// ──────────────────────────────────────────────────────────────────────

void InitGPhaseSys(void)
{
    for (int i = layer_num-1; i >= 0; i--)
        gphase_sys.now[i] = GPHASE_ID_NONE;

    SetNextGPhase(GID_SUPER);   // queue the root phase as the first transition
}

// ──────────────────────────────────────────────────────────────────────
// Mark ini_flg[i] = 1 for every layer whose next phase differs from now.
// Called at the top of GPhaseSysMain(), before now[] is overwritten.

static void SetInitFlag(void)
{
    for (int i = 0; i < layer_num; i++)
    {
        gphase_sys.ini_flg[i] = (gphase_sys.next[i] != gphase_sys.now[i]) ? 1 : 0;
    } 
}

// ──────────────────────────────────────────────────────────────────────
// Execute one frame for the given layer and all its children (recursive).
// Returns the combined GPHASE_ENUM result that after_func reports.

static GPHASE_ENUM DoJobPhase(int layer)
{
    GPHASE_ENUM result;

    // Sanity guard – should never be reached in a correct build.
    if (layer >= layer_num) 
    {
        printf("layer_num over %d\n", layer_num);
        for (;;) {}
    }

    result = GPHASE_CONTINUE;

    // If the phase just changed this frame, run its one-shot init function.
    if (gphase_sys.ini_flg[layer]) 
    {
        ini_func[gphase_sys.now[layer]]();
    }

    GPHASE_ID_ENUM now = gphase_sys.now[layer];

    // Optional pre-frame hook (not all phases have one).
    if (pre_func[now] != NULL) 
    {
        result = pre_func[now](result);
    }

    // If this phase has child sub-phases, recurse into the next layer.
    if (gphase_tbl[now].son_num != 0) 
    {
        result = DoJobPhase(layer + 1);
    }

    // Post-frame hook receives the child result and may escalate it.
    return after_func[now](result);
}

// ──────────────────────────────────────────────────────────────────────
// Main entry point – call once per game frame.


void GPhaseSysMain(void)
{
    // Snapshot which layers are transitioning this frame.
    SetInitFlag();

    // Apply queued transitions: now[] <- next[].
    for (int i = 0; i < layer_num; i++)
    {
        gphase_sys.now[i] = gphase_sys.next[i];
    }

    // PORT-ONLY: report phase changes / stalls.  No ROM equivalent.
    GPhaseTraceMain(gphase_sys.now);

    // Run the phase hierarchy starting from layer 0 (GID_SUPER root).
    DoJobPhase(0);

    // Fire end callbacks for every layer where a *new* transition was queued
    // during DoJobPhase (i.e. now[i] != next[i] again after the frame ran).
    for (int i = layer_num-1; i >= 0; i--) 
    {
        if (gphase_sys.now[i] == GPHASE_ID_NONE)
            continue;
        if (gphase_sys.now[i] != gphase_sys.next[i])
            end_func[gphase_sys.now[i]]();
    }
}

// ──────────────────────────────────────────────────────────────────────
// Queue a transition to phase `id`.  Lays the whole root-to-leaf path for
// `id` into next[]: `id` sits at its own layer, its first-child (son_ID)
// chain fills the deeper layers — with any layer that falls away blanked to
// GPHASE_ID_NONE — and its parent (superID) chain fills the shallower layers.
// GPhaseSysMain() promotes next[] into now[] on the following frame.

void SetNextGPhase(GPHASE_ID_ENUM id)
{
    GPHASE_DAT *gp;
    GPHASE_DAT *gpbak;
    int layer;
    int i;

    gp = &gphase_tbl[id];
    gpbak = gp;
    layer = gp->layer;
    gphase_sys.next[layer] = id;

    if (layer < layer_num)
    {
        for (i = layer; i < layer_num; i++)
        {
            if (gp->son_num == 0)
            {
                for (; i < layer_num - 1; i++)
                {
                    gphase_sys.next[i + 1] = GPHASE_ID_NONE;
                }

                break;
            }

            id = (GPHASE_ID_ENUM)gp->son_ID;
            gphase_sys.next[i + 1] = id;
            gp = &gphase_tbl[id];
        }
    }

    if ((0 < layer) && (GPHASE_ID_NONE < gpbak->superID))
    {
        id = (GPHASE_ID_ENUM)gpbak->superID;
        gphase_sys.next[layer - 1] = id;

        for (i = layer - 1; i > 0; i--)
        {
            id = (GPHASE_ID_ENUM)gphase_tbl[id].superID;

            if (id < GID_SUPER)
            {
                break;
            }

            gphase_sys.next[i - 1] = id;
        }
    }
}
