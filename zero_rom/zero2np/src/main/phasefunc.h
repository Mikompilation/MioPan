/* ==========================================================================
 *  phasefunc.h
 *
 *  Forward declarations for every GPhase per-phase callback referenced by
 *  the dispatch tables in gphase.c (ini_func / end_func / pre_func /
 *  after_func).  Each game state module defines its own
 *  init_<Phase> / end_<Phase> / pre_<Phase> / (after|one)_<Phase> handlers;
 *  they are collected here so the tables can be initialised in one place.
 *
 *  The GID_SUPER / GID_BOOT_INIT / GID_SOFTRESETMAIN handlers live in main.c
 *  and are declared by main.h (included below); the rest are grouped by their
 *  owning source file.
 *
 *  Reconstructed from the Feb 6 2004 prototype (SLES_523.84).
 * ======================================================================== */

#ifndef _PHASEFUNC_H
#define _PHASEFUNC_H

#include "gphase.h"             /* GPHASE_ENUM                              */
#include "main.h"               /* GID_SUPER / GID_BOOT_INIT / GID_SOFTRESET */

/* -- /home/zero_rom/zero2np/src/outgame/pad_check.c -- */
void        init_Boot_PadCheck(void);
GPHASE_ENUM one_Boot_PadCheck(GPHASE_ENUM dummy);
void        end_Boot_PadCheck(void);

/* -- /home/zero_rom/zero2np/src/outgame/lang_check.c -- */
void        init_LangData_Check(void);
GPHASE_ENUM one_LangData_Check(GPHASE_ENUM dummy);
void        end_LangData_Check(void);

/* -- /home/zero_rom/zero2np/src/outgame/lang_sel.c -- */
void        init_LangSel_Main(void);
GPHASE_ENUM one_LangSel_Main(GPHASE_ENUM dummy);
void        end_LangSel_Main(void);

/* -- /home/zero_rom/zero2np/src/outgame/autoload.c -- */
void        init_AutoLoad_Main(void);
GPHASE_ENUM one_AutoLoad_Main(GPHASE_ENUM dummy);
void        end_AutoLoad_Main(void);

/* -- /home/zero_rom/zero2np/src/outgame/outgame.c -- */
void        init_OutGame_Main(void);
GPHASE_ENUM pre_OutGame_Main(GPHASE_ENUM dummy);
GPHASE_ENUM after_OutGame_Main(GPHASE_ENUM result);
void        end_OutGame_Main(void);
void        init_UBI_Mode(void);
GPHASE_ENUM one_UBI_Mode(GPHASE_ENUM dummy);
void        end_UBI_Mode(void);
void        init_Tecmo_Mode(void);
GPHASE_ENUM one_Tecmo_Mode(GPHASE_ENUM dummy);
void        end_Tecmo_Mode(void);
void        init_Project_Mode(void);
GPHASE_ENUM one_Project_Mode(GPHASE_ENUM dummy);
void        end_Project_Mode(void);

/* -- /home/zero_rom/zero2np/src/ingame/ingame.c -- */
void        init_Story_NowLoading(void);
GPHASE_ENUM pre_Story_NowLoading(GPHASE_ENUM dummy);
GPHASE_ENUM after_Story_NowLoading(GPHASE_ENUM result);
void        end_Story_NowLoading(void);
void        init_Story_Load_Mission(void);
void        end_Story_Load_Mission(void);
GPHASE_ENUM one_Story_Load_Mission(GPHASE_ENUM dummy);
void        init_Story_Load_Mission_Save(void);
void        end_Story_Load_Mission_Save(void);
GPHASE_ENUM one_Story_Load_Mission_Save(GPHASE_ENUM dummy);
void        init_Story_Load_Mission_Event(void);
void        end_Story_Load_Mission_Event(void);
GPHASE_ENUM one_Story_Load_Mission_Event(GPHASE_ENUM dummy);
void        init_Story_Main(void);
void        end_Story_Main(void);
GPHASE_ENUM pre_Story_Main(GPHASE_ENUM dummy);
GPHASE_ENUM after_Story_Main(GPHASE_ENUM result);
void        init_Story_Normal(void);
void        end_Story_Normal(void);
GPHASE_ENUM one_Story_Normal(GPHASE_ENUM dummy);
void        init_Story_Game_Over_Pre(void);
void        end_Story_Game_Over_Pre(void);
GPHASE_ENUM one_Story_Game_Over_Pre(GPHASE_ENUM dummy);
void        init_Story_Damage(void);
void        end_Story_Damage(void);
GPHASE_ENUM one_Story_Damage(GPHASE_ENUM dummy);
void        init_Story_Door_Open(void);
void        end_Story_Door_Open(void);
GPHASE_ENUM one_Story_Door_Open(GPHASE_ENUM dummy);
void        init_Story_Ene_Dead(void);
void        end_Story_Ene_Dead(void);
GPHASE_ENUM one_Story_Ene_Dead(GPHASE_ENUM dummy);
void        init_Story_Debug(void);
void        end_Story_Debug(void);
GPHASE_ENUM one_Story_Debug(GPHASE_ENUM dummy);
void        init_Story_Debug_Cam(void);
void        end_Story_Debug_Cam(void);
GPHASE_ENUM one_Story_Debug_Cam(GPHASE_ENUM dummy);
void        init_Story_Pause(void);
void        end_Story_Pause(void);
GPHASE_ENUM one_Story_Pause(GPHASE_ENUM dummy);
void        init_Story_Menu(void);
void        end_Story_Menu(void);
GPHASE_ENUM one_Story_Menu(GPHASE_ENUM dummy);
void        init_Story_Map(void);
void        end_Story_Map(void);
GPHASE_ENUM one_Story_Map(GPHASE_ENUM dummy);
void        init_Story_Mission_St(void);
void        end_Story_Mission_St(void);
GPHASE_ENUM one_Story_Mission_St(GPHASE_ENUM dummy);
void        init_Story_Mission_Result(void);
void        end_Story_Mission_Result(void);
GPHASE_ENUM one_Story_Mission_Result(GPHASE_ENUM dummy);
void        init_Story_Pause_Mission(void);
void        end_Story_Pause_Mission(void);
GPHASE_ENUM one_Story_Pause_Mission(GPHASE_ENUM dummy);
void        init_Story_Game_Over(void);
GPHASE_ENUM pre_Story_Game_Over(GPHASE_ENUM dummy);
GPHASE_ENUM after_Story_Game_Over(GPHASE_ENUM result);
void        end_Story_Game_Over(void);
void        init_Story_Effect(void);
void        end_Story_Effect(void);
GPHASE_ENUM one_Story_Effect(GPHASE_ENUM dummy);
void        init_Story_Puzzle(void);
GPHASE_ENUM pre_Story_Puzzle(GPHASE_ENUM dummy);
GPHASE_ENUM after_Story_Puzzle(GPHASE_ENUM result);
void        end_Story_Puzzle(void);
void        init_Story_SavePoint(void);
GPHASE_ENUM pre_Story_SavePoint(GPHASE_ENUM dummy);
GPHASE_ENUM after_Story_SavePoint(GPHASE_ENUM result);
void        end_Story_SavePoint(void);
void        init_Story_Movie_Room_Sel(void);
void        end_Story_Movie_Room_Sel(void);
GPHASE_ENUM one_Story_Movie_Room_Sel(GPHASE_ENUM dummy);

/* -- /home/zero_rom/zero2np/src/ingame/gameover/prg/gameover_menu.c -- */
void        init_GameOver_Menu(void);
GPHASE_ENUM pre_GameOver_Menu(GPHASE_ENUM dummy);
GPHASE_ENUM after_GameOver_Menu(GPHASE_ENUM result);
void        end_GameOver_Menu(void);
void        init_GameOver_Menu_Top(void);
GPHASE_ENUM one_GameOver_Menu_Top(GPHASE_ENUM dummy);
void        end_GameOver_Menu_Top(void);
void        init_GameOver_Menu_Load(void);
GPHASE_ENUM one_GameOver_Menu_Load(GPHASE_ENUM dummy);
void        end_GameOver_Menu_Load(void);
void        init_GameOver_Menu_Album(void);
GPHASE_ENUM one_GameOver_Menu_Album(GPHASE_ENUM dummy);
void        end_GameOver_Menu_Album(void);

/* -- /home/zero_rom/zero2np/src/ingame/clear/prg/ending.c -- */
void        init_Ending_Movie(void);
GPHASE_ENUM pre_Ending_Movie(GPHASE_ENUM dummy);
GPHASE_ENUM after_Ending_Movie(GPHASE_ENUM result);
void        end_Ending_Movie(void);
void        init_Ending_Normal1(void);
GPHASE_ENUM one_Ending_Normal1(GPHASE_ENUM dummy);
void        end_Ending_Normal1(void);
void        init_Ending_Normal2(void);
GPHASE_ENUM one_Ending_Normal2(GPHASE_ENUM dummy);
void        end_Ending_Normal2(void);
void        init_Ending_Hard(void);
GPHASE_ENUM one_Ending_Hard(GPHASE_ENUM dummy);
void        end_Ending_Hard(void);

/* -- /home/zero_rom/zero2np/src/ingame/clear/prg/game_result.c -- */
void        init_GameResult(void);
GPHASE_ENUM pre_GameResult(GPHASE_ENUM dummy);
GPHASE_ENUM after_GameResult(GPHASE_ENUM result);
void        end_GameResult(void);
void        init_GameResult_Top(void);
GPHASE_ENUM one_GameResult_Top(GPHASE_ENUM dummy);
void        end_GameResult_Top(void);

/* -- /home/zero_rom/zero2np/src/ingame/clear/prg/clearmenu.c -- */
void        init_ClearMenu(void);
GPHASE_ENUM pre_ClearMenu(GPHASE_ENUM dummy);
GPHASE_ENUM after_ClearMenu(GPHASE_ENUM result);
void        end_ClearMenu(void);
void        init_ClearMenu_Top(void);
GPHASE_ENUM one_ClearMenu_Top(GPHASE_ENUM dummy);
void        end_ClearMenu_Top(void);
void        init_ClearMenu_Save(void);
GPHASE_ENUM one_ClearMenu_Save(GPHASE_ENUM dummy);
void        end_ClearMenu_Save(void);
void        init_ClearMenu_Album(void);
GPHASE_ENUM one_ClearMenu_Album(GPHASE_ENUM dummy);
void        end_ClearMenu_Album(void);

/* -- /home/zero_rom/zero2np/src/debug/debug.c -- */
void        init_Debug_Menu(void);
void        end_Debug_Menu(void);
GPHASE_ENUM one_Debug_Menu(GPHASE_ENUM dummy);

/* -- /home/zero_rom/zero2np/src/outgame/title.c -- */
void        init_Title_Mode(void);
GPHASE_ENUM pre_Title_Mode(GPHASE_ENUM dummy);
GPHASE_ENUM after_Title_Mode(GPHASE_ENUM result);
void        end_Title_Mode(void);
void        init_Title_Top(void);
GPHASE_ENUM one_Title_Top(GPHASE_ENUM dummy);
void        end_Title_Top(void);
void        init_Title_Menu(void);
GPHASE_ENUM one_Title_Menu(GPHASE_ENUM dummy);
void        end_Title_Menu(void);
void        init_Title_NewGame(void);
GPHASE_ENUM one_Title_NewGame(GPHASE_ENUM dummy);
void        end_Title_NewGame(void);
void        init_Title_LoadGame(void);
GPHASE_ENUM one_Title_LoadGame(GPHASE_ENUM dummy);
void        end_Title_LoadGame(void);
void        init_Title_Setup(void);
GPHASE_ENUM pre_Title_Setup(GPHASE_ENUM dummy);
GPHASE_ENUM after_Title_Setup(GPHASE_ENUM result);
void        end_Title_Setup(void);
void        init_Title_Album(void);
GPHASE_ENUM one_Title_Album(GPHASE_ENUM dummy);
void        end_Title_Album(void);
void        init_Title_Gallery(void);
GPHASE_ENUM one_Title_Gallery(GPHASE_ENUM dummy);
void        end_Title_Gallery(void);
void        init_Title_Option(void);
GPHASE_ENUM one_Title_Option(GPHASE_ENUM dummy);
void        end_Title_Option(void);
void        init_Title_FrameRate_Sel(void);
GPHASE_ENUM one_Title_FrameRate_Sel(GPHASE_ENUM dummy);
void        end_Title_FrameRate_Sel(void);
void        init_Title_Chapter_Sel(void);
GPHASE_ENUM one_Title_Chapter_Sel(GPHASE_ENUM dummy);
void        end_Title_Chapter_Sel(void);
void        init_Title_Move_Movie(void);
GPHASE_ENUM one_Title_Move_Movie(GPHASE_ENUM dummy);
void        end_Title_Move_Movie(void);

/* -- /home/zero_rom/zero2np/src/outgame/title_movie.c -- */
void        init_Title_Movie_Mode(void);
GPHASE_ENUM one_Title_Movie_Mode(GPHASE_ENUM dummy);
void        end_Title_Movie_Mode(void);

/* -- /home/zero_rom/zero2np/src/graphics/scene/IngameScene.c -- */
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

/* -- /home/zero_rom/zero2np/src/ingame/event/prg/ev_phase.c -- */
void        init_EventMsg_Disp(void);
void        end_EventMsg_Disp(void);
GPHASE_ENUM one_EventMsg_Disp(GPHASE_ENUM dummy);
void        init_EventFile_Disp(void);
void        end_EventFile_Disp(void);
GPHASE_ENUM one_EventFile_Disp(GPHASE_ENUM dummy);

/* -- /home/zero_rom/zero2np/src/ingame/photo/photo.c -- */
void        init_Story_Photo(void);
void        end_Story_Photo(void);
GPHASE_ENUM one_Story_Photo(GPHASE_ENUM dummy);

/* -- /home/zero_rom/zero2np/src/ingame/gameover/prg/gameover.c -- */
void        init_Story_GameOver_Eff(void);
GPHASE_ENUM one_Story_GameOver_Eff(GPHASE_ENUM dummy);
void        end_Story_GameOver_Eff(void);
void        init_Story_GameOver_Fade(void);
GPHASE_ENUM one_Story_GameOver_Fade(GPHASE_ENUM dummy);
void        end_Story_GameOver_Fade(void);
void        init_Story_GameOver_Movie(void);
GPHASE_ENUM one_Story_GameOver_Movie(GPHASE_ENUM dummy);
void        end_Story_GameOver_Movie(void);

/* -- /home/zero_rom/zero2np/src/ingame/puzzle/puzzle.c -- */
void        init_Puzzle_InConf(void);
GPHASE_ENUM one_Puzzle_InConf(GPHASE_ENUM dummy);
void        end_Puzzle_InConf(void);
void        init_Puzzle_CrossFade(void);
GPHASE_ENUM one_Puzzle_CrossFade(GPHASE_ENUM dummy);
void        end_Puzzle_CrossFade(void);
void        init_Puzzle_Hina(void);
GPHASE_ENUM one_Puzzle_Hina(GPHASE_ENUM dummy);
void        end_Puzzle_Hina(void);
void        init_Puzzle_Roku(void);
GPHASE_ENUM one_Puzzle_Roku(GPHASE_ENUM dummy);
void        end_Puzzle_Roku(void);
void        init_Puzzle_Kaza(void);
GPHASE_ENUM one_Puzzle_Kaza(GPHASE_ENUM dummy);
void        end_Puzzle_Kaza(void);
void        init_Puzzle_Kaza2(void);
GPHASE_ENUM one_Puzzle_Kaza2(GPHASE_ENUM dummy);
void        end_Puzzle_Kaza2(void);
void        init_Puzzle_Kai1(void);
GPHASE_ENUM one_Puzzle_Kai1(GPHASE_ENUM dummy);
void        end_Puzzle_Kai1(void);
void        init_Puzzle_Kai2(void);
GPHASE_ENUM one_Puzzle_Kai2(GPHASE_ENUM dummy);
void        end_Puzzle_Kai2(void);

/* -- /home/zero_rom/zero2np/src/ingame/savepoint/savepoint.c -- */
void        init_SavePoint_FadeIn(void);
GPHASE_ENUM one_SavePoint_FadeIn(GPHASE_ENUM dummy);
void        end_SavePoint_FadeIn(void);
void        init_SavePoint_FadeOut(void);
GPHASE_ENUM one_SavePoint_FadeOut(GPHASE_ENUM dummy);
void        end_SavePoint_FadeOut(void);

/* -- /home/zero_rom/zero2np/src/ingame/savepoint/savepoint_main.c -- */
void        init_SavePoint_Main(void);
GPHASE_ENUM pre_SavePoint_Main(GPHASE_ENUM dummy);
GPHASE_ENUM after_SavePoint_Main(GPHASE_ENUM result);
void        end_SavePoint_Main(void);
void        init_SavePoint_Top(void);
GPHASE_ENUM one_SavePoint_Top(GPHASE_ENUM dummy);
void        end_SavePoint_Top(void);
void        init_SavePoint_Save(void);
GPHASE_ENUM one_SavePoint_Save(GPHASE_ENUM dummy);
void        end_SavePoint_Save(void);
void        init_SavePoint_Album(void);
GPHASE_ENUM one_SavePoint_Album(GPHASE_ENUM dummy);
void        end_SavePoint_Album(void);

/* -- /home/zero_rom/zero2np/src/outgame/setup.c -- */
void        init_Title_SetupMenu(void);
GPHASE_ENUM one_Title_SetupMenu(GPHASE_ENUM dummy);
void        end_Title_SetupMenu(void);
void        init_Title_Mission(void);
GPHASE_ENUM pre_Title_Mission(GPHASE_ENUM dummy);
GPHASE_ENUM after_Title_Mission(GPHASE_ENUM result);
void        end_Title_Mission(void);
void        init_Mission_Sel(void);
GPHASE_ENUM one_Mission_Sel(GPHASE_ENUM dummy);
void        end_Mission_Sel(void);
void        init_Mission_Cam(void);
GPHASE_ENUM one_Mission_Cam(GPHASE_ENUM dummy);
void        end_Mission_Cam(void);
void        init_Mission_Album(void);
GPHASE_ENUM one_Mission_Album(GPHASE_ENUM dummy);
void        end_Mission_Album(void);
void        init_Mission_Save(void);
GPHASE_ENUM one_Mission_Save(GPHASE_ENUM dummy);
void        end_Mission_Save(void);

#endif /* _PHASEFUNC_H */
