// FILE: /home/zero_rom/zero2np/src/ingame/subtitle/subtitle.c
//
// Ingame spoken-line captions: the subtitles the event script puts up during
// a conversation, and the ones a passive ghost's voice line carries.
//
// One caption at a time.  SUBTITLE_CTRL is a single global, so a new request
// stops whatever was running, and a request does three things at once:
//
//   * queues the ADPCM stream that carries the voice (StreamAutoPreload, at
//     STREAM_PRIORITY_EVENT -- not the SUBTITLE priority, which nothing uses)
//   * starts the speaker's mouth moving (SubTitleMimReq)
//   * walks a list of (message id, frame count) pairs in step with it
//
// The four entry points differ only in where the voice is heard from:
//
//   SubTitleReq          no position -- the voice is not spatialised
//   SubTitleReq3D        a fixed world point
//   SubTitleReq3DObj     an object, re-positioned every frame while it plays
//   SubTitleReqAutoEnemy a stream someone else already started (enemy.c)
//
// Playback is a three-state machine on SUBTITLE_CTRL::Status.  PRELOAD waits
// for the CD, EXEC advances MsgDataNo one line at a time, and a -1 message id
// parks the caption -- from there the stream ending is what tears everything
// down, not the table running out.  That is deliberate: it lets a caption go
// blank before the voice has finished.
//
// Reconstructed from the Feb 6 2004 prototype (SLES_523.84), subtitle.o.
// All 9 `ZERO2.MAP` `.text` exports plus 7 statics; `.text` is accounted for
// byte-for-byte (0x263fb8..0x264690) once the fixed_array.h boilerplate at the
// head of the object file is set aside.  The file has no static data at all:
// its `.data` is SubTitleCtrl (all zeroes), and its `.rodata`/`.sdata` hold
// nothing but the compiler's own fixed_array assert literals.
//
// Line annotations are measured from the $LM stabs, so they are exact.  The
// gaps are real -- this file has a lot of commented-out code (180-184, 234-238
// and 255-269 are the big ones).

#include "subtitle.h"

#include "../../common/variable.h"              /* plyr_wrk                  */
#include "../../common/zero2_util.h"            /* GetObjectPos              */
#include "../../graphics/graph2d/message.h"     /* MesStatusCheck            */
#include "../../graphics/graph3d/g3dxVu0.h"     /* g3dxVu0CopyVector         */
#include "../../graphics/motion/mim.h"          /* mimRequestNumContinue     */
#include "../../graphics/motion/motion.h"       /* motSearchANI_CTRL         */
#include "../../graphics/movie/movie_title.h"   /* MovieTitleDispMain        */
#include "../../main/main.h"                    /* GetSubTitleAddr           */
#include "../../system/eeiop/stream_auto.h"     /* StreamAuto*               */
#include "../../system/os/system.h"             /* GetPALMode                */
#include "../event/prg/ev_disp.h"               /* EvChapterIsDisp           */
#include "../plyr/sis_mdl.h"                    /* ReqSisterMimContinue      */

/* Size of the offset table at the head of the subtitle file. */
#define SUBTITLE_DATA_MAX       250

/* MovieTitleDispMain() arguments.  Type 6 is the subtitle message table; the
 * caption sits near the bottom of the screen in colour 1. */
#define SUBTITLE_MSG_TYPE       6
#define SUBTITLE_DISP_Y         380
#define SUBTITLE_MSG_COL        1

/* The one message that is drawn without its backing plate. */
#define SUBTITLE_NO_BASE_MSG    180

/* The speakers that have a mouth to move.  CharId 1 goes through the sister
 * model helper (which finds the model for itself); CharId 6 is a model number
 * and is looked up directly. */
#define SUBTITLE_CHAR_SISTER    1
#define SUBTITLE_CHAR_MDL6      6

#define SUBTITLE_SISTER_MIM_NO  6
#define SUBTITLE_MDL6_MIM_NO    0

/* Voice streams are queued at event priority and full volume. */
#define SUBTITLE_STREAM_PRIORITY    0x11    /* STREAM_PRIORITY_EVENT         */
#define SUBTITLE_STREAM_VOL         0x3200

/* SubTitleCheckDispAccept() is the last function in the object file but is
 * called from SubTitleMain(), so the ROM had it declared up here. */
static int SubTitleCheckDispAccept(void);

SUBTITLE_CTRL SubTitleCtrl;                 /* data 34fe80                   */

/* --------------------------------------------------------------------------
 *  SubTitleCtrlStructInit
 *
 *  Note what it does *not* do: ObjType and ObjId are left alone.  Nothing
 *  reads them while StreamId is -1, so the stale values are harmless -- but
 *  they are stale, and SubTitleReqSub() rewrites both on every path rather
 *  than relying on this.
 * ------------------------------------------------------------------------ */
static void SubTitleCtrlStructInit(SUBTITLE_CTRL *pCtrl)                /* 98 */
{                                                                       /* 99 */
    pCtrl->pSubTitleData = NULL;                                        /* 100 */
    pCtrl->StreamId      = -1;                                          /* 101 */
    pCtrl->Status        = SUBTITLE_STATUS_IDLE;                        /* 102 */
    pCtrl->Counter       = 0;                                           /* 103 */
    pCtrl->MsgDataNo     = 0;                                           /* 104 */
}

void SubTitleInit(void)                                                 /* 110 */
{                                                                       /* 111 */
    SUBTITLE_CTRL *pCtrl = &SubTitleCtrl;                               /* 112 */

    SubTitleCtrlStructInit(pCtrl);                                      /* 114 */
}

/* --------------------------------------------------------------------------
 *  SubTitleDataPtrGet
 *
 *  Resolve a subtitle number against the loaded file.  The table is
 *  self-relative: the entry is a byte offset from the table's own base, which
 *  is what lets the file be loaded anywhere.
 *
 *  The range test is unsigned, so a negative SubTitleNo fails it too.
 *
 *  PORT: the ROM adds the offset to the base as a 32-bit integer.  Pointers
 *  are 8 bytes here, so the add has to stay in pointer arithmetic or the
 *  result is truncated.
 * ------------------------------------------------------------------------ */
static SUBTITLE_DATA *SubTitleDataPtrGet(int SubTitleNo)                /* 120 */
{                                                                       /* 121 */
    SUBTITLE_DATA *pRet = NULL;                                         /* 122 */

    if ((u_int)SubTitleNo < SUBTITLE_DATA_MAX)                          /* 124 */
    {
        u_int *pDataTop = (u_int *)GetSubTitleAddr();                   /* 125 */
        u_int *pDataTbl = pDataTop + SubTitleNo;                        /* 126 */

        pRet = (SUBTITLE_DATA *)((u_char *)pDataTop + *pDataTbl);       /* 128 */
    }

    return pRet;                                                        /* 131 */
}

/* --------------------------------------------------------------------------
 *  SubTitleMimReq / SubTitleMimStop
 *
 *  Start and stop the speaker's mouth.  Only two CharIds are handled -- the
 *  rest of the cast talks without lip-sync.  Called every frame while the
 *  caption runs, which is why the "continue" form is used: it leaves an
 *  already-running mim alone rather than restarting it each frame.
 * ------------------------------------------------------------------------ */
static void SubTitleMimReq(int CharId)                                  /* 137 */
{                                                                       /* 138 */
    if (CharId == SUBTITLE_CHAR_SISTER)                                 /* 139 */
    {
        ReqSisterMimContinue(SUBTITLE_SISTER_MIM_NO, 0);                /* 140 */
    }

    if (CharId == SUBTITLE_CHAR_MDL6)                                   /* 143 */
    {
        ANI_CTRL *pAniCtrl = motSearchANI_CTRL(CharId);                 /* 146 */

        if (pAniCtrl != NULL)                                           /* 147 */
        {
            mimRequestNumContinue(pAniCtrl, SUBTITLE_MDL6_MIM_NO, 0);   /* 148 */
        }
    }
}

static void SubTitleMimStop(int CharId)                                 /* 156 */
{                                                                       /* 157 */
    if (CharId == SUBTITLE_CHAR_SISTER)                                 /* 158 */
    {
        StopSisterMim(SUBTITLE_SISTER_MIM_NO);                          /* 159 */
    }

    if (CharId == SUBTITLE_CHAR_MDL6)                                   /* 162 */
    {
        ANI_CTRL *pAniCtrl = motSearchANI_CTRL(CharId);                 /* 165 */

        if (pAniCtrl != NULL)                                           /* 166 */
        {
            /* Ends the mim at the end of its loop rather than cutting it. */
            mimEndStopNum(pAniCtrl, SUBTITLE_MDL6_MIM_NO);              /* 167 */
        }
    }
}

/* --------------------------------------------------------------------------
 *  SubTitleUpdateStreamPosition
 *
 *  Keep a caption bound to a moving object sounding like it comes from that
 *  object.  Only SubTitleReq3DObj() leaves ObjType/ObjId set, so the other
 *  three request forms fall out at the first test.
 * ------------------------------------------------------------------------ */
static void SubTitleUpdateStreamPosition(int StreamId, int ObjType, int ObjId) /* 175 */
{                                                                       /* 176 */
    sceVu0FVECTOR TmpPos;

    if (StreamId == -1 || ObjType == -1 || ObjId == -1)                 /* 179 */
    {
        return;
    }

    if (GetObjectPos(TmpPos, (u_char)ObjType, ObjId) != 0)              /* 185 */
    {
        StreamAutoSetPosition(StreamId, TmpPos);                        /* 186 */
    }
}                                                                       /* 194 */

/* --------------------------------------------------------------------------
 *  SubTitleMain
 *
 *  The playback machine.  DrawFlg suppresses only the drawing -- the stream,
 *  the mouth and the line counter all keep running, so a caption hidden for a
 *  few frames comes back where it should be rather than where it was.
 * ------------------------------------------------------------------------ */
void SubTitleMain(int DrawFlg)                                          /* 202 */
{                                                                       /* 203 */
    SUBTITLE_CTRL *pCtrl = &SubTitleCtrl;

    if (pCtrl->Status == SUBTITLE_STATUS_PRELOAD)                       /* 206 */
    {
        if (StreamAutoIsPreload(pCtrl->StreamId) != 0)                  /* 207 */
        {
            StreamAutoPreloadPlay(pCtrl->StreamId);                     /* 208 */
            pCtrl->Status = SUBTITLE_STATUS_EXEC;                       /* 209 */
        }
    }
    else if (pCtrl->Status == SUBTITLE_STATUS_EXEC)                     /* 214 */
    {
        SUBTITLE_DATA     *pSubTitle = pCtrl->pSubTitleData;            /* 215 */
        SUBTITLE_MSG_DATA *pMsgData  = &pSubTitle->MsgData[pCtrl->MsgDataNo]; /* 216 */

        if (StreamAutoIsPlaying(pCtrl->StreamId) != 0)                  /* 218 */
        {
            SubTitleUpdateStreamPosition(pCtrl->StreamId,               /* 220 */
                                         pCtrl->ObjType, pCtrl->ObjId);
            SubTitleMimReq(pSubTitle->CharId);                          /* 223 */

            /* A -1 message id is the end of the caption, not the end of the
             * line: the counter stops here and the stream is left to finish
             * on its own. */
            if (pMsgData->MsgId != -1)                                  /* 226 */
            {
                if (SubTitleCheckDispAccept() && DrawFlg)     /* 227 */
                {
                    /* One message draws without its backing plate. */
                    MovieTitleDispMain(SUBTITLE_MSG_TYPE, pMsgData->MsgId, /* 231 */
                                       SUBTITLE_DISP_Y, SUBTITLE_MSG_COL,
                                       (char)(pMsgData->MsgId != SUBTITLE_NO_BASE_MSG)); /* 233 */
                }

                if (GetPALMode())                                  /* 239 */
                {
                    /* The frame counts are authored at 60Hz, so 50Hz has to
                     * hold each line for five sixths as many frames. */
                    if (pCtrl->Counter >= pMsgData->Frame * 5 / 6)      /* 240 */
                    {
                        pCtrl->Counter = 0;                             /* 241 */
                        pCtrl->MsgDataNo++;                             /* 242 */
                    }
                    else
                    {
                        pCtrl->Counter++;                               /* 245 */
                    }
                }
                else
                {
                    /* GCC cross-jumped these two stores with the PAL copy
                     * above, which is why only this one carries markers. */
                    if (pCtrl->Counter >= pMsgData->Frame)              /* 249 */
                    {
                        pCtrl->Counter = 0;                             /* 250 */
                        pCtrl->MsgDataNo++;                             /* 251 */
                    }
                    else
                    {
                        pCtrl->Counter++;                               /* 254 */
                    }
                }
            }
        }
        else
        {
            SubTitleMimStop(pSubTitle->CharId);                         /* 270 */
            SubTitleCtrlStructInit(pCtrl);                              /* 271 */
        }
    }
}

/* --------------------------------------------------------------------------
 *  SubTitleReqSub
 *
 *  The one request path.  StreamId >= 0 means the caller owns the stream and
 *  this is only riding on it; otherwise the stream is started here, with a
 *  SND_3D_SET if a position was given.
 *
 *  The header file always sits one CD file below the stream itself.
 * ------------------------------------------------------------------------ */
static void SubTitleReqSub(int SubTitleNo, int StreamId, int ObjType, int ObjId,
                           sceVu0FVECTOR *pPosition)                    /* 280 */
{
    SUBTITLE_CTRL *pCtrl = &SubTitleCtrl;                               /* 281 */
    SUBTITLE_DATA *pSubTitle = SubTitleDataPtrGet(SubTitleNo);          /* 282 */

    if (pSubTitle == NULL)                                              /* 284 */
    {
        return;
    }

    /* Anything already on screen is stopped -- including its stream, which is
     * what separates this from a plain re-init. */
    if (pCtrl->StreamId != -1)                                          /* 286 */
    {
        SubTitleStop();                                                 /* 287 */
    }
    else
    {
        SubTitleCtrlStructInit(pCtrl);                                  /* 290 */
    }

    pCtrl->pSubTitleData = pSubTitle;                                   /* 293 */

    if (StreamId >= 0)                                                  /* 294 */
    {
        pCtrl->StreamId = StreamId;                                     /* 295 */
        pCtrl->ObjType  = -1;                                           /* 296 */
        pCtrl->ObjId    = -1;                                           /* 297 */
    }
    else if (pPosition != NULL)                                         /* 300 */
    {
        SND_3D_SET set = { pPosition, NULL, NULL };                     /* 301 */

        pCtrl->StreamId = StreamAutoPreload(pSubTitle->StreamFile,      /* 305 */
                                            pSubTitle->StreamFile - 1,
                                            SUBTITLE_STREAM_PRIORITY, 0, 0,
                                            SUBTITLE_STREAM_VOL, 0, &set);
        pCtrl->ObjType  = ObjType;                                      /* 310 */
        pCtrl->ObjId    = ObjId;                                        /* 311 */
    }
    else
    {
        pCtrl->StreamId = StreamAutoPreload(pSubTitle->StreamFile,      /* 316 */
                                            pSubTitle->StreamFile - 1,
                                            SUBTITLE_STREAM_PRIORITY, 0, 0,
                                            SUBTITLE_STREAM_VOL, 0, NULL);
        pCtrl->ObjType  = -1;                                           /* 321 */
        pCtrl->ObjId    = -1;                                           /* 322 */
    }

    pCtrl->Status    = SUBTITLE_STATUS_PRELOAD;                         /* 326 */
    pCtrl->Counter   = 0;                                               /* 330 */
    pCtrl->MsgDataNo = 0;                                               /* 331 */
}                                                                       /* 332 */

void SubTitleReq(int SubTitleNo)                                        /* 337 */
{                                                                       /* 338 */
    SubTitleReqSub(SubTitleNo, -1, -1, -1, NULL);                       /* 339 */
}

void SubTitleReq3D(int SubTitleNo, float *Position)                     /* 345 */
{                                                                       /* 346 */
    sceVu0FVECTOR TmpPos;

    /* Copied rather than passed through: StreamAutoPreload() reads the
     * SND_3D_SET now, but the caller's vector is a script operand. */
    g3dxVu0CopyVector(TmpPos, Position);                                /* 349 (g3dxVu0.h 135) */
    SubTitleReqSub(SubTitleNo, -1, -1, -1, &TmpPos);                    /* 350 */
}

void SubTitleReq3DObj(int SubTitleNo, int ObjType, int ObjId)           /* 356 */
{                                                                       /* 357 */
    sceVu0FVECTOR TmpPos;

    if (GetObjectPos(TmpPos, (u_char)ObjType, ObjId) != 0)              /* 361 */
    {
        SubTitleReqSub(SubTitleNo, -1, ObjType, ObjId, &TmpPos);        /* 362 */
    }
}

void SubTitleReqAutoEnemy(int SubTitleNo, int StreamId)                 /* 369 */
{                                                                       /* 370 */
    SubTitleReqSub(SubTitleNo, StreamId, -1, -1, NULL);                 /* 371 */
}

/* --------------------------------------------------------------------------
 *  SubTitleStop
 *
 *  Fades the voice out with time 0 -- an immediate stop -- and clears the
 *  slot.  It does not stop the speaker's mouth: only SubTitleMain() seeing
 *  the stream end does that, so a caption stopped from the script leaves the
 *  mim to finish its loop.
 * ------------------------------------------------------------------------ */
void SubTitleStop(void)                                                 /* 377 */
{                                                                       /* 378 */
    SUBTITLE_CTRL *pCtrl = &SubTitleCtrl;                               /* 379 */

    StreamAutoFadeOut(pCtrl->StreamId, 0);                              /* 381 */
    SubTitleCtrlStructInit(pCtrl);                                      /* 382 */
}

int SubTitleIsEnd(void)                                                 /* 388 */
{                                                                       /* 389 */
    SUBTITLE_CTRL *pCtrl = &SubTitleCtrl;                               /* 390 */

    if (pCtrl->Status == SUBTITLE_STATUS_IDLE)                          /* 393 */
    {
        return 1;
    }

    return 0;                                                           /* 397 */
}

/* --------------------------------------------------------------------------
 *  SubTitleStreamFileNoGet
 *
 *  Which CD file carries a subtitle's voice.  Note the missing NULL check --
 *  an out-of-range SubTitleNo faults here rather than reporting.  enemy.c is
 *  the only caller and it asserts the *result*, which is after the fact.
 *  Reproduced as found.
 * ------------------------------------------------------------------------ */
int SubTitleStreamFileNoGet(int SubTitleNo)                             /* 403 */
{                                                                       /* 404 */
    SUBTITLE_DATA *pSubTitle = SubTitleDataPtrGet(SubTitleNo);          /* 405 */

    return pSubTitle->StreamFile;                                       /* 407 */
}

/* --------------------------------------------------------------------------
 *  SubTitleCheckDispAccept
 *
 *  Whether the caption may be drawn this frame.  Three things take the screen
 *  away from it: the viewfinder being up, a message window being open, and the
 *  chapter title being on screen.
 * ------------------------------------------------------------------------ */
static int SubTitleCheckDispAccept(void)                                /* 413 */
{                                                                       /* 414 */
    int Accept;

    if (plyr_wrk.cmn_wrk.mode == 6)                                     /* 417 */
    {
        Accept = 0;
    }
    else if (MesStatusCheck() != 0)                                     /* 421 */
    {
        Accept = 0;
    }
    else
    {
        Accept = (EvChapterIsDisp() == 0);                              /* 425 */
    }

    return Accept;                                                      /* 429 */
}
