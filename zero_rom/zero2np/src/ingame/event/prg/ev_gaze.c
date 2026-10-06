// FILE: /home/zero_rom/zero2np/src/ingame/event/prg/ev_gaze.c
//
// Event gaze targets.  Two ways to aim a character's head: at a fixed world
// point (SetPoint) or at a tracked object (SetObjType), the latter re-reading
// the object's position every frame.  mActive gates the whole thing;
// mObjAppoint selects which of the two modes is live.
//
// Reconstructed from the Feb 6 2004 prototype (SLES_523.84).

#include "ev_gaze.h"

#include "../../../common/zero2_util.h"     // GetObjectPos
#include "../../plyr/sis_mdl.h"             // SisNeckRegisterTarget / LTP_*
#include "../../../graphics/motion/mdlwork.h"   // LOOK_AT_PARAM
#include <cfloat>

/* Neck-tracking rates the event gaze always registers with. */
#define EV_GAZE_EYE_SPD   0.2f
#define EV_GAZE_HEAD_SPD  0.1f
#define EV_GAZE_CHEST_SPD 0.05f

void CEventGazeWrk::Init()
{
    this->mActive = 0;                                                   /* 8 */
}

void CEventGazeWrk::SetObjType(int iObjType, int iObjId)
{
    this->mActive = 1;
    this->mObjAppoint = 1;
    this->mObjId = iObjId;
    this->mObjType = (char)iObjType;
}

void CEventGazeWrk::SetPoint(float *Pos)
{
    this->mActive = 1;
    this->mObjAppoint = 0;
    sceVu0CopyVector(this->mPos, Pos);
}

void CEventSisterGazeWrk::Work()
{
    LOOK_AT_PARAM param;

    if (this->mActive != 0) {                                            /* 24 */
        if (this->mObjAppoint != 0) {                                    /* 28 */
            GetObjectPos(this->mPos, (u_char)this->mObjType, this->mObjId); /* 29 */
        }

        /* param.enable is deliberately left uninitialised -- the ROM writes
         * only pos and the three speeds. */
        param.eye_spd = EV_GAZE_EYE_SPD;                                /* 33 */
        param.head_spd = EV_GAZE_HEAD_SPD;                              /* 34 */
        param.chest_spd = EV_GAZE_CHEST_SPD;                            /* 35 */
        sceVu0CopyVector(param.pos, this->mPos);                         /* 36 */
        SisNeckRegisterTarget(&param, LTP_MAYU_EVENT_OBJ, FLT_MAX);  /* 37 */
    }
}
