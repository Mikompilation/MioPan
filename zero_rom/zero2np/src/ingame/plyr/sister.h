/* ==========================================================================
 *  ingame/plyr/sister.h
 *
 *  Companion ("sister", Mayu) game module (sister.o) -- her state machine,
 *  damage handling, path following, and the trace-point search that recovers
 *  her when she loses sight of the player.
 *
 *  sis_mdl.o owns her model and animation playback; this module owns where she
 *  is and what she is doing.  The two meet at SetSisterAnime(), the only place
 *  sister.o pushes an animation request into sis_mdl.o.
 *
 *  Reconstructed from the Feb 6 2004 prototype (SLES_523.84).
 * ======================================================================== */

#ifndef _INGAME_PLYR_SISTER_H
#define _INGAME_PLYR_SISTER_H

#include "eetypes.h"

#include "../../common/save_data.h"          /* MC_SAVE_DATA */
#include "../../system/eeiop/snd3d.h"        /* SND_3D_SET   */
#include "../../graphics/graph3d/ctl/fixed_array.h"
#include "../../sdk/libvu0.h"                /* sceVu0FVECTOR */

/* --------------------------------------------------------------------------
 *  Recorded trail.
 *
 *  p/l/fl form a 64-entry ring.  `now` is the entry she is standing on and
 *  `top` the newest one the player laid down, so the live route runs
 *  now..top forwards with wraparound and holds `num` entries.  l[i] is the
 *  length of the segment ending at i; fl[i] marks a segment DrawSisDummy()
 *  found crossing another one.
 * ------------------------------------------------------------------------ */
struct SIS_TRACE                    /* 0x5c0 */
{
    /* 0x000 */ int   top;
    /* 0x004 */ int   now;
    /* 0x008 */ int   num;
    /* 0x00c */ int   cnt;              /* frames since the last point       */
    /* 0x010 */ float trgt[4];
    /* 0x020 */ int   trgt_floor;
    /* 0x030 */ float vwpos[4];
    /* 0x040 */ int   push_tm;          /* cooldown after a push-aside       */
    /* 0x044 */ int   push_cnt;         /* frame counter for the turn ease   */
    /* 0x048 */ int   push;             /* 0 idle, 1 decide, 2/3 turn, 10 move */
    /* 0x04c */ int   push_dir;         /* index into rrot[]                 */
    /* 0x050 */ int   push_pldir;
    /* 0x054 */ float push_dist;        /* step budget left                  */
    /* 0x058 */ float push_rot;
    /* 0x05c */ float push_orot;        /* heading when the push started     */
    /* 0x060 */ u_char view_hit;        /* line of sight to trgt is blocked  */
    /* 0x064 */ float dist;             /* distance left to walk             */
    /* 0x068 */ float wsdist;           /* start walking / stop walking      */
    /* 0x06c */ float wedist;
    /* 0x070 */ float rsdist;           /* start running / stop running      */
    /* 0x074 */ float redist;
    /* 0x080 */ fixed_array<float[4], 64> p;
    /* 0x480 */ fixed_array<float, 64>    l;
    /* 0x580 */ fixed_array<u_char, 64>   fl;
};

/* One step of an animation script.  ani_no 0xff terminates a table and 0xfe
 * means "turn to face the target first"; loop -1 loops until interrupted. */
struct SIS_ANI_TBL                  /* 0x4 */
{
    /* 0x0 */ u_char ani_no;
    /* 0x1 */ u_char frm;               /* interpolation frames              */
    /* 0x2 */ short  loop;
};

struct SIS_AREA_CHG_SUB             /* 0x20 */
{
    /* 0x00 */ int   now;               /* destination area, -1 terminates   */
    /* 0x10 */ float pos[4];
};

struct SIS_AREA_CHG                 /* 0x8 */
{
    /* 0x0 */ int old;                  /* source area, -1 terminates        */
    /* 0x4 */ SIS_AREA_CHG_SUB *target;
};

typedef struct                      /* 0x1 */
{
    /* 0x0 */ u_char amode;             /* 0 trace 1 find 2 battle 3 aene
                                         * 4 talk 5 search 6 motion 7 battle */
} SIS_ALG_WORK;

struct SIS_SEARCH                   /* 0x3 */
{
    /* 0x0 */ char start;               /* graph node nearest her            */
    /* 0x1 */ char end;                 /* graph node nearest the player     */
    /* 0x2 */ char flg;
};

struct SIS_MOTION                   /* 0x8 */
{
    /* 0x0 */ char mot_loop;
    /* 0x1 */ char mot_end;
    /* 0x2 */ char now_tbl;
    /* 0x4 */ SIS_ANI_TBL *sat;
};

extern SIS_TRACE    sis_trace;      /* data  34f3c0 */
extern SIS_ALG_WORK sis_algo;       /* sdata 3f4468 */
extern SIS_SEARCH   sis_search;     /* sdata 3f4470 */
extern SIS_MOTION   sis_motion;     /* sdata 3f4478 */
extern float        sistv[4];       /* data  34fe50 */

#ifdef __cplusplus
extern "C" {
#endif

/* ---- lifecycle -------------------------------------------------------- */
void InitSister(void);
void InitSisterPos(void);
void InitSisterTrace(void);
void InitSisterTracePos(void);
void SisterMain(void);
int  SisterGameOver(void);

/* ---- presence / locking ----------------------------------------------- */
int  IsSisWrk(void);
void SetSisWrk(int flg);
void SisterDisp(int sw);
void SisterLock(void);
void SisterUnlock(void);

/* ---- death ------------------------------------------------------------ */
void ReqSisDead(int mode);
int  SisDead(void);

/* ---- behaviour modes ---------------------------------------------------- */
void ModeSisTrace(void);
void ModeSisFind(void);
void ModeSisBattle(void);
void ModeSisTalk(void);
void ModeSisSearch(void);
void ModeSisMotion(void);
void ModeSisAeneFind(void);
void SetModeSisAeneFind(void);
void EndModeSisAeneFind(void);
void ReqModeSisMotion(u_int tbl_no);
void SetSearchMode(void);
void SetSisEscape(void);
void SetFindMode(float *tgt, int floor);
int  GetSisTraceStatus(void);

/* ---- movement ---------------------------------------------------------- */
void  SisterTracePlayer(void);
void  SisterTraceMove(void);
void  SisterNoMove(void);
void  SisterPushCheck(void);
void  ClearSisterPushMove(void);
void  SisterPosUpdate(void);
void  SetNowTracePos(void);
void  CalcSisDist(void);
void  SetSisterHeight(void);
void  MoveSisStairs(void);
float GetSisterRot(void);
int   SetSisTurn(void);
int   IsSisterTurn(void);
void  ChangeSisTraceDist(u_char type);
void  ChangeForceTraceMode(void);
void  ChangeSisterExPos(int old, int now);

/* ---- placement --------------------------------------------------------- */
void SetSisterPos(float *pos);
void SetSisterRot(float *rot);
void SetSisterRotY(float rot);
void SetSisterFloorPL(void);
void SetSisAreaNo(int area_no);
int  GetSisAreaNo(void);

/* ---- collision helpers -------------------------------------------------- */
int DistHitCheck(float *v1, float *v2, float dist);
int SisterHitCheck(float *v1, int floor1, float *v2, int floor2);

/* ---- status ------------------------------------------------------------ */
void  SetSisterStatus(void);
void  ClearSisWait(void);
void  SisCondCheck(void);
void  SisDoorAct(void);
void  CheckSisGhost(void);
void  SisterDamageCtrl(void);
float GetDist_Sister2Player(void);
void  SetSisJoinFlg(u_char join_flg);
int   GetSisJoinFlg(void);

/* ---- animation --------------------------------------------------------- */
void SetSisterAnime(u_char anime_no, u_char frame);
void SisterNAnimeCtrl(void);
int  CheckSisterNeckSW(u_char anime_no);
int  CheckSisAnimeEnd(int anime_no);
int  GetSisStandAnm(void);
void SisMepachiCtrl(void);

/* ---- trace-point search -------------------------------------------------
 * SearchSearchPoint() picks the graph nodes nearest `pos` and `tgt` that are
 * actually visible from them; SearchTraceRoute() runs Dijkstra between those
 * two nodes and writes a -1 terminated node list into `route`;
 * SetSearchPoint() string-pulls that list and turns it back into sis_trace's
 * ring of positions. */
int  SearchSearchPoint(float *pos, int floor1, float *tgt, int floor2);
int  SearchTraceRoute(char *route);
void SetSearchPoint(char *route, int floor1, int floor2);

/* ---- sound ------------------------------------------------------------- */
void ReqSisBankPlay(int no, int effect, int loop, int fade_time, SND_3D_SET *s3d);

/* ---- debug draw --------------------------------------------------------- */
void DrawHitLCircle(float *mpos, u_char r, u_char g, u_char b, float rd);
void DrawSisDummy(void);
void SisterDebug(void);

/* ---- save -------------------------------------------------------------- */
void SetSave_SisWrk(MC_SAVE_DATA *data);
void SetSave_SisTrace(MC_SAVE_DATA *data);
void SetSave_SisAlgoWrk(MC_SAVE_DATA *data);
void SetSave_SisMotion(MC_SAVE_DATA *data);

/* PORT: converts SIS_MOTION::sat between a live table pointer and its
 * sis_ani_tbl index.  See the body in sister.c. */
void SisMotionSavePtrFixup(int to_host);

/* sis_mdlInit() / sis_mdlMotionWork() moved to sis_mdl.h -- they are
 * sis_mdl.o symbols, not sister.o ones. */

#ifdef __cplusplus
}
#endif

#endif /* _INGAME_PLYR_SISTER_H */
