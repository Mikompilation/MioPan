/* ==========================================================================
 *  ingame/map/map_rectangle.c
 *
 *  Registration-rectangle lookup: the layer between RegDat's raw PZB tables
 *  and the game systems that ask "what is at this position".
 *
 *  Camera and hit rectangles are the same mechanism -- RegDat resolves the
 *  position to a registration record of a given rectangle type, and the
 *  accessors below read fields off whichever record is latched.  Types used
 *  here:  0 hit (wall collision), 6 room outline, 8 battle camera,
 *  9 four-corner "special" camera.
 *
 *  Reconstructed from the Feb 6 2004 prototype (SLES_523.84), map_rectangle.o
 *  0x001dd0a8..0x001dee73.
 * ======================================================================== */

#include "map_rectangle.h"

#include "RegDat.h"
#include "hit_check_base.h"     /* HcBaseIsInTriXZ / HcBasePointRectangle */

#include "../../common/variable.h"       /* debug_var.hit_disp */
#include "../../graphics/graphics.h"     /* Draw3DSquare */

#include <libvu0.h>
#include <stdio.h>

struct MapRecBufCtrl                    /* 0x28 */
{
    int buf_id;
    int id_num;
    int id_list[8];
};

/* Cached type-0 rectangle lists for one frame, so MapHitCheck() can walk
 * them without re-resolving the position per rectangle. */
struct MapRecInfo                       /* 0x64 */
{
    /* 0x00 */ int          id_num;
    /* 0x04 */ int          rec_num_list[8];
    /* 0x24 */ int          buf_ids[8];
    /* 0x44 */ MB_OUT_RECT *rec_list[8];
};

/* data 31c6a0 */
static MapRecBufCtrl map_buf_ctrl[2] =
{
    { -1, 0, { 0 } },
    { -1, 0, { 0 } }
};

/* data 31c6f0 */
static MapRecInfo map_rec_info;

/* Door rectangles (type 7) get their own copy of the same cache; MapHitCheck()
 * walks the two independently so a shut door can be collided against without
 * disturbing the wall list. */
static MapRecInfo map_door_rec_info;

/* sdata 3f1ab8 / 3f1abc / 3f1ac0 */
static MDAT_CAM *cam_info;
static int cam_change_flg;
static int cam_hit_flg;

/* The footstep-sound region MrecSetSEInfo() latched. */
static MDAT_SE *se_info;

/* The ROM defines this down beside MrecGetCameraInfo(); it is hoisted here
 * because MrecGetSeNo() below needs it and it is file-local. */
static MDAT_SE *MrecGetSeInfo(void)                                     /* 1175 */
{
    return se_info;
}

static MapRecBufCtrl *MrecGetRegBuffID(int buf_no)
{
    if ((unsigned int)buf_no > 1)
    {
        return map_buf_ctrl;
    }
    return &map_buf_ctrl[buf_no];
}

int MrecSetRegBuffID(int floor_id, const float *pos, int buf_no)       /* 66 */
{
    MapRecBufCtrl *mb_ctrl = MrecGetRegBuffID(buf_no);
    int buff_id = RegDatGetBuffIDG(floor_id, (float *)pos);

    if (buff_id == -1)
    {
        mb_ctrl->buf_id = -1;
        return 0;
    }

    if ((unsigned int)(buff_id + 3) < 2)
    {
        int id_num = RegDatGetHitNum();
        int *ids = RegDatGetHitList();

        for (int i = 0; i < id_num; i++)
        {
            mb_ctrl->id_list[i] = ids[i];
        }
        mb_ctrl->id_num = id_num;
    }
    else
    {
        mb_ctrl->id_list[0] = buff_id;
        mb_ctrl->id_num = 1;
    }

    mb_ctrl->buf_id = buff_id;
    return 1;
}

/* A rectangle is a quad; test it as two triangles in the XZ plane. */
int MrecIsInRectangle(const float *pos, float (*rec)[4])                /* 1137 */
{
    if (HcBaseIsInTriXZ(pos, rec[0], rec[1], rec[2]) != 0)
    {
        return 1;
    }
    return (HcBaseIsInTriXZ(pos, rec[0], rec[2], rec[3]) != 0);
}

/* ---- stair rectangles --------------------------------------------------- */

/* data 31c7c0.  The six headings a flight of stairs can run along: the four
 * cardinals then the two diagonals the mansion actually uses.  Stair records
 * store an index into this rather than an angle. */
static float sta_rot_tbl[6] =
{
    0.0f, 1.5707962f, 3.1415925f, 4.7123885f, 0.78539813f, 3.9269905f
};

float *MrecGetStaRotTbl(void)                                           /* 1182 */
{
    return sta_rot_tbl;                                                 /* 1183 */
}

float MrecGetStaRot(int rot)                                            /* 1125 */
{
    float *tbl = MrecGetStaRotTbl();                                    /* 1125 */

    return tbl[rot];                                                    /* 1126 */
}

/* The stair section record.  Only the rot index at 0xc is read here, but the
 * head/type ahead of it are the standard MDAT_* preamble. */
typedef struct MDAT_STA
{
    /* 0x00 */ MB_OUT_SECTION head;
    /* 0x08 */ int type;
    /* 0x0c */ int rot;
} MDAT_STA;

/* Writes the heading of the flight of stairs `pos` stands on, if any.  Note
 * the loop does NOT break on a hit -- the ROM keeps scanning and the last
 * overlapping rectangle wins, which is how overlapping landings resolve. */
int MrecGetStaInfo(float *rot, int floor, const float *pos)             /* 1089 */
{
    MDAT_STA    *sta = (MDAT_STA *)0;
    MB_OUT_RECT *rec;
    int          num;
    int          buff_id;

    buff_id = RegDatGetBuffIDG(floor, (float *)pos);                    /* 1096 */
    if (buff_id < 0)                                                    /* 1098 */
    {
        return 0;
    }

    rec = RegDatGetVecPtr(buff_id, RECORD_TYPE_STAIRS);     /* type 10 = stairs */     /* 1102 */
    num = RegDatGetVecNum(buff_id, RECORD_TYPE_STAIRS);                                 /* 1104 */
    if (num < 1)                                                        /* 1106 */
    {
        return 0;
    }

    do
    {
        if (MrecIsInRectangle(pos, rec->vec) != 0)                      /* 1107 */
        {
            sta = (MDAT_STA *)RegDatGetStPtr(buff_id, rec->reg_id);     /* 1109 */
            *rot = MrecGetStaRot(sta->rot);                             /* 1110 */
        }
        num--;                                                          /* 1112 */
        rec++;
    } while (num != 0);

    return (sta != (MDAT_STA *)0);                                      /* 1114 */
}

/* ---- camera rectangles ------------------------------------------------- */

MDAT_CAM *MrecGetCameraInfo(void)                                       /* 1164 */
{
    return cam_info;
}

void MrecInitCameraInfo(void)                                           /* 111 */
{
    cam_info       = (MDAT_CAM *)0;
    cam_change_flg = 0;
    cam_hit_flg    = 0;
}

/* Latches the camera record covering `pos`.  Returns 1 when that is a
 * different record from last frame -- the caller treats that as a cut -- and
 * 0 when it is the same one or there is no camera rectangle here.
 *
 * Note the record is only replaced when something was actually hit: walking
 * off the end of the camera rectangles keeps the last camera rather than
 * dropping to none. */
int MrecSetCameraInfo(int floor, const float *pos, int DataType)        /* 131 */
{
    MDAT_CAM *first = (MDAT_CAM *)0;
    MDAT_CAM *cam   = (MDAT_CAM *)0;
    MB_OUT_RECT *region_p;
    int reg_id = -1;
    int change = 0;
    int buff_id;
    int num;
    int i;

    buff_id = RegDatGetBuffIDG(floor, (float *)pos);
    if (buff_id < 0)
    {
        return 0;
    }

    region_p = RegDatGetVecPtr(buff_id, DataType);
    num      = RegDatGetVecNum(buff_id, DataType);

    for (i = 0; i < num; i++)
    {
        cam = first;

        if (MrecIsInRectangle(pos, region_p->vec) != 0)
        {
            reg_id = region_p->reg_id;
            cam    = (MDAT_CAM *)RegDatGetStPtr(buff_id, reg_id);

            if (first == (MDAT_CAM *)0)
            {
                first = cam;
            }
            if (cam == MrecGetCameraInfo())
            {
                change = 0;             /* already on this camera */
                break;
            }
            change = 1;
        }
        region_p++;
    }

    if (reg_id == -1)
    {
        cam = cam_info;
    }

    cam_info       = cam;
    cam_hit_flg    = (reg_id != -1);
    cam_change_flg = change;

    return change;
}

int MrecIsCameraChange(void)                                            /* 187 */
{
    return cam_change_flg;
}

int MrecIsCameraHit(void)                                               /* 196 */
{
    return cam_hit_flg;
}

int MrecGetCameraID(void)                                               /* 207 */
{
    MDAT_CAM *cam = MrecGetCameraInfo();

    return (cam != nullptr) ? (int)cam->head.SecStID : -1;
}

int MrecGetCameraType(void)                                             /* 229 */
{
    MDAT_CAM *cam = MrecGetCameraInfo();

    return (cam != nullptr) ? cam->type : -1;
}

int MrecGetCameraPos(float *pos)                                        /* 247 */
{
    MDAT_CAM *cam = MrecGetCameraInfo();

    if (cam != nullptr)
    {
        pos[0] = cam->Pos[0];
        pos[1] = cam->Pos[1];
        pos[2] = cam->Pos[2];
        pos[3] = 1.0f;
    }
    return (cam != nullptr);
}

int MrecGetCameraInterest(float *pos)                                   /* 269 */
{
    MDAT_CAM *cam = MrecGetCameraInfo();

    if (cam != nullptr)
    {
        pos[0] = cam->View[0];
        pos[1] = cam->View[1];
        pos[2] = cam->View[2];
        pos[3] = 1.0f;
    }
    return (cam != nullptr);
}

int MrecGetCameraRotZ(float *rot_z)                                     /* 291 */
{
    MDAT_CAM *cam = MrecGetCameraInfo();

    if (cam != nullptr)
    {
        *rot_z = cam->RotZ;
    }
    return (cam != nullptr);
}

/* -1 means "no record", which map_camera.c reads as "leave the FOV alone". */
float MrecGetCameraPrj(void)                                            /* 310 */
{
    MDAT_CAM *cam = MrecGetCameraInfo();

    return (cam != nullptr) ? cam->Rrg : -1.0f;
}

/* Follow slack.  Negative means "do not follow". */
float MrecGetCameraAsobi(void)                                          /* 328 */
{
    MDAT_CAM *cam = MrecGetCameraInfo();

    return (cam != nullptr) ? cam->Asobi : -1.0f;
}

MDAT_CAM_SP *MrecGetCameraSpInfo(MB_OUT_RECT **ppRect, void *pRectStat, /* 344 */
                                 int floor, float *Pos)
{
    return (MDAT_CAM_SP *)RegDatGetRectAndStat(ppRect, pRectStat, floor, Pos, RECORD_TYPE_FOUR_CORNER_CAMERA);
}

int MrecCheckOnSameCamRect(void *pRectStat, int floor, float *Pos, int DataType)  /* 349 */
{
    return (RegDatCheckSameRectStat(pRectStat, floor, Pos, DataType) != 0);
}

int MrecCheckHitRect(int floor, float *Pos, int DataType)               /* 378 */
{
    return (RegDatGetStat(floor, Pos, DataType) != nullptr);
}

/* ---- hit rectangles (wall collision) ----------------------------------- */

/* Total number of type-0 rectangles across every buffer the position
 * resolved to. */
int MrecIsHitRectangleNum(int buf_no)                                   /* 462 */
{
    MapRecBufCtrl *buf_ctrl = MrecGetRegBuffID(buf_no);
    int buf_id = buf_ctrl->buf_id;
    int total = 0;
    int i;

    if (buf_id == -1)
    {
        return 0;
    }

    /* -2 / -3 mean the position was ambiguous, so walk the whole list. */
    if ((unsigned int)(buf_id + 3) < 2)
    {
        for (i = 0; i < buf_ctrl->id_num; i++)
        {
            total += RegDatGetVecNum(buf_ctrl->id_list[i], RECORD_TYPE_HIT);
        }
        return total;
    }

    return RegDatGetVecNum(buf_id, RECORD_TYPE_HIT);
}

/* Flattens the per-buffer type-0 rectangle lists into one index space. */
float (*MrecGetRectPtr(int num, int buf_no))[4]                         /* 495 */
{
    MapRecBufCtrl *buf_ctrl = MrecGetRegBuffID(buf_no != 0);
    int buf_id = buf_ctrl->buf_id;
    MB_OUT_RECT *reg_p;

    if (buf_id == -1)
    {
        return (float (*)[4])0;
    }

    if ((unsigned int)(buf_id + 3) < 2)
    {
        int base = 0;
        int i;

        for (i = 0; i < buf_ctrl->id_num; i++)
        {
            int n = RegDatGetVecNum(buf_ctrl->id_list[i], RECORD_TYPE_HIT);
            if (num < base + n)
            {
                buf_id = buf_ctrl->id_list[i];
                num   -= base;
                break;
            }
            base += n;
        }
    }

    reg_p = RegDatGetVecPtr(buf_id, RECORD_TYPE_HIT);
    if (reg_p == (MB_OUT_RECT *)0)
    {
        return (float (*)[4])0;
    }
    return reg_p[num].vec;
}

int MrecSetHitRectInfo(int buf_no)                                      /* 542 */
{
    MapRecBufCtrl *buf_ctrl = MrecGetRegBuffID(buf_no != 0);
    int buf_id = buf_ctrl->buf_id;
    int i;

    if (buf_id == -1)
    {
        map_rec_info.id_num = 0;
        return 0;
    }

    if ((unsigned int)(buf_id + 3) < 2)
    {
        map_rec_info.id_num = buf_ctrl->id_num;
        for (i = 0; i < buf_ctrl->id_num; i++)
        {
            map_rec_info.rec_num_list[i] = RegDatGetVecNum(buf_ctrl->id_list[i], RECORD_TYPE_HIT);
            map_rec_info.rec_list[i]     = RegDatGetVecPtr(buf_ctrl->id_list[i], RECORD_TYPE_HIT);
            map_rec_info.buf_ids[i]      = buf_ctrl->id_list[i];
        }
    }
    else
    {
        map_rec_info.id_num          = 1;
        map_rec_info.rec_num_list[0] = RegDatGetVecNum(buf_id, RECORD_TYPE_HIT);
        map_rec_info.rec_list[0]     = RegDatGetVecPtr(buf_id, RECORD_TYPE_HIT);
        map_rec_info.buf_ids[0]      = buf_id;
    }

    return 1;
}

int MrecGetHitInfoIdNum(void)                                           /* 584 */
{
    return map_rec_info.id_num;
}

int *MrecGetHitInfoRecNumList(void)                                     /* 593 */
{
    return map_rec_info.rec_num_list;
}

float (*MrecGetHitInfoRecVecter(int list_no, int rec_no))[4]            /* 602 */
{
    if (map_rec_info.rec_list[list_no] == nullptr)
    {
        return (float (*)[4])0;
    }
    return map_rec_info.rec_list[list_no][rec_no].vec;
}

/* Non-zero when the segment pos1..pos2 crosses hit rectangle `num` of list
 * `list_no`.  Each of the quad's four edges is tested in turn; the rectangle
 * is closed, so edge 3 wraps back to corner 0. */
int MrecLineCross(const float *pos1, const float *pos2,
                  int list_no, int num)                                 /* 617 */
{
    float (*vec)[4];
    float vw[4][4];
    int i;

    vec = MrecGetHitInfoRecVecter(list_no, num);                        /* 623 */
    if (vec == (float (*)[4])0)
    {
        return 0;
    }

    for (i = 0; i < 4; i++)                                             /* 627 */
    {
        if (HcBaseLineIntersect2(pos1, pos2, vec[i], vec[(i + 1) % 4]) != 0)
        {                                                               /* 628 */
            break;
        }
    }

    if (i == 4)                                                         /* 632 */
    {
        return 0;
    }

    /* The rectangles are authored flat at y == 0, so the debug quad is lifted
     * to the caller's height to be visible against the floor. */
    if (debug_var.hit_disp != 0)                                        /* 637 */
    {
        for (i = 0; i < 4; i++)                                         /* 639 */
        {
            sceVu0CopyVector(vw[i], vec[i]);                            /* 640 */
            vw[i][1] = pos1[1];                                         /* 641 */
        }
        Draw3DSquare(vw[0], vw[1], vw[2], vw[3], 0x40, 0xff, 0x40, 0x40);
    }                                                                   /* 643 */

    return 1;                                                           /* 646 */
}

/* Non-zero when hit rectangle `num` lies wholly inside the quad pos1..pos4 --
 * all four of its corners must be in one of the quad's two triangles.  This is
 * the containment half of the sweep test; MrecLineCross() is the crossing
 * half, and a rectangle fully swallowed by the sweep crosses no edge. */
int MrecIsInHitRectangle(const float *pos1, const float *pos2,
                         const float *pos3, const float *pos4,
                         int list_no, int num)                          /* 659 */
{
    float (*target)[4];
    int i;

    target = MrecGetHitInfoRecVecter(list_no, num);                     /* 665 */
    if (target == (float (*)[4])0)
    {
        return 0;
    }

    for (i = 0; i < 4; i++)                                             /* 669 */
    {
        if ((HcBaseIsInTriXZ(target[i], pos1, pos2, pos3) == 0) &&      /* 670 */
            (HcBaseIsInTriXZ(target[i], pos3, pos4, pos1) == 0))
        {
            break;
        }
    }

    return (i == 4);                                                    /* 679 */
}

/* Cheap AABB overlap against hit rectangle `num`, in the floor plane only.
 * The ROM builds the rectangle's bounds with VU0 vmax/vmini across all four
 * lanes at once; only X and Z are ever compared, because the rectangles are
 * flattened to y == 0 and a Y test would reject everything. */
int MrecIsNearRectangle(float *boxmin, float *boxmax, int list_no, int num)
{                                                                       /* 698 */
    float (*vec)[4];
    float vecmin[4];
    float vecmax[4];
    int i;
    int j;

    vec = MrecGetHitInfoRecVecter(list_no, num);                        /* 703 */
    if (vec == (float (*)[4])0)
    {
        return 0;
    }

    sceVu0CopyVector(vecmax, vec[0]);
    sceVu0CopyVector(vecmin, vec[0]);
    for (i = 1; i < 4; i++)
    {
        for (j = 0; j < 4; j++)
        {
            if (vec[i][j] > vecmax[j])
            {
                vecmax[j] = vec[i][j];
            }
            if (vec[i][j] < vecmin[j])
            {
                vecmin[j] = vec[i][j];
            }
        }
    }

    if (vecmin[0] > boxmax[0])                                          /* 711 */
    {
        return 0;
    }
    if (boxmin[0] > vecmax[0])
    {
        return 0;
    }
    if (vecmin[2] > boxmax[2])                                          /* 712 */
    {
        return 0;
    }
    if (boxmin[2] > vecmax[2])
    {
        return 0;
    }

    return 1;                                                           /* 716 */
}

/* ---- footstep-sound rectangles (type 4) -------------------------------- */

/* Latches the footstep-sound region under `pos`.  Every type-4 rectangle in
 * every buffer the position resolved to is tested, and the winner is the one
 * with the highest Pre -- regions overlap deliberately (a rug over floorboards)
 * and Pre is how the author says which surface wins.  Records already seen are
 * skipped by reg_id, so a region split across several rectangles does not beat
 * itself.  On a miss the ROM prints and leaves the previous region latched. */
int MrecSetSEInfo(float *pos)                                           /* 729 */
{
    MapRecBufCtrl *mb_ctrl;
    MB_OUT_RECT   *rec;
    MDAT_SE       *best =nullptr;
    MDAT_SE       *se;
    int best_id = -1;
    int reg_id;
    int buff_id;
    int num;
    int i;

    mb_ctrl = MrecGetRegBuffID(0);                                      /* 730 */
    if (mb_ctrl->buf_id == -1)                                          /* 736 */
    {
        return 0;
    }

    for (i = 0; i < mb_ctrl->id_num; i++)                               /* 739 */
    {
        buff_id = mb_ctrl->id_list[i];
        rec = RegDatGetVecPtr(buff_id, RECORD_TYPE_FOOTSTEP_SE);        /* 742 */
        num = RegDatGetVecNum(buff_id, RECORD_TYPE_FOOTSTEP_SE);

        for (; num > 0; num--, rec++)                                   /* 746 */
        {
            if (MrecIsInRectangle(pos, rec->vec) == 0)                  /* 747 */
            {
                continue;
            }

            reg_id = rec->reg_id;                                       /* 749 */
            se     = (MDAT_SE *)RegDatGetStPtr(buff_id, reg_id);        /* 751 */

            if (best_id == -1)                                          /* 753 */
            {
                best_id = reg_id;
                best    = se;
            }
            else if ((best_id != reg_id) && (se->Pre > best->Pre))      /* 754 */
            {
                best_id = reg_id;
                best    = se;
            }
        }
    }

    if (best_id == -1)                                                  /* 785 */
    {
        printf(" foot se miss \n");                                     /* 786 */
        return 0;
    }

    se_info = best;                                                     /* 791 */
    return 1;                                                           /* 793 */
}

int MrecGetSeNo(void)                                                   /* 804 */
{
    MDAT_SE *se = MrecGetSeInfo();                                      /* 806 */

    return (se != (MDAT_SE *)0) ? se->No : -1;                          /* 809 */
}

/* ---- door rectangles (type 7) ------------------------------------------
 * Doors get their own rectangle type so they can be collided against only
 * while shut -- the registration record carries the open/closed state. */

int MrecIsDoorRectangleNum(void)                                        /* 821 */
{
    MapRecBufCtrl *buf_ctrl = MrecGetRegBuffID(0);
    int buf_id = buf_ctrl->buf_id;
    int total = 0;

    if (buf_id == -1)
    {
        return 0;
    }

    if ((unsigned int)(buf_id + 3) < 2)
    {
        int num = RegDatGetHitNum();
        int *list = RegDatGetHitList();

        for (int i = 0; i < num; i++)
        {
            total += RegDatGetVecNum(list[i], RECORD_TYPE_DOOR);
        }
        return total;
    }

    return RegDatGetVecNum(buf_id, RECORD_TYPE_DOOR);
}

/* As HcBasePointRectangle against door rectangle `num`, but only when that
 * door's record says it is solid. */
int MrecPointDoorRectangle(float *len, float *pos, float *a, float *b,  /* 853 */
                           int num, float r)
{
    MapRecBufCtrl *buf_ctrl = MrecGetRegBuffID(0);
    int buf_id = buf_ctrl->buf_id;
    MB_OUT_RECT *reg_p;
    MDAT_DOOR *door;

    if (buf_id == -1)
    {
        return 0;
    }

    if ((unsigned int)(buf_id + 3) < 2)
    {
        int hit_num = RegDatGetHitNum();
        int *list = RegDatGetHitList();
        int base = 0;
        int i;

        for (i = 0; i < hit_num; i++)
        {
            int n = RegDatGetVecNum(list[i], RECORD_TYPE_DOOR);
            if (num < base + n)
            {
                buf_id = list[i];
                num   -= base;
                break;
            }
            base += n;
        }
    }

    reg_p = RegDatGetVecPtr(buf_id, RECORD_TYPE_DOOR);
    if (reg_p == (MB_OUT_RECT *)0)
    {
        return 0;
    }

    door = (MDAT_DOOR *)RegDatGetStPtr(buf_id, reg_p[num].reg_id);
    if (door == (MDAT_DOOR *)0 || door->HitCheck == 0)
    {
        return 0;               /* open / passable */
    }

    return HcBasePointRectangle(len, pos, a, b, reg_p[num].vec, r);
}

/* The door equivalent of MrecSetHitRectInfo(): caches this frame's type-7
 * rectangle lists so the collision walk does not re-resolve per rectangle. */
int MrecSetDoorHitRectInfo(int buf_no)                                  /* 916 */
{
    MapRecBufCtrl *buf_ctrl = MrecGetRegBuffID(buf_no != 0);            /* 917 */
    int buf_id = buf_ctrl->buf_id;
    int i;

    if (buf_id == -1)                                                   /* 923 */
    {
        map_door_rec_info.id_num = 0;
        return 0;
    }

    if ((unsigned int)(buf_id + 3) < 2)                                 /* 926 */
    {
        map_door_rec_info.id_num = buf_ctrl->id_num;                    /* 929 */
        for (i = 0; i < buf_ctrl->id_num; i++)                          /* 930 */
        {
            map_door_rec_info.rec_num_list[i] = RegDatGetVecNum(buf_ctrl->id_list[i], RECORD_TYPE_DOOR);               /* 931 */
            map_door_rec_info.rec_list[i] = RegDatGetVecPtr(buf_ctrl->id_list[i], RECORD_TYPE_DOOR);               /* 933 */
            map_door_rec_info.buf_ids[i] = buf_ctrl->id_list[i];        /* 934 */
        }
    }
    else
    {
        map_door_rec_info.id_num          = 1;                          /* 942 */
        map_door_rec_info.rec_num_list[0] = RegDatGetVecNum(buf_id, RECORD_TYPE_DOOR); /* 943 */
        map_door_rec_info.rec_list[0]     = RegDatGetVecPtr(buf_id, RECORD_TYPE_DOOR); /* 944 */
        map_door_rec_info.buf_ids[0]      = buf_id;                     /* 945 */
    }

    return 1;                                                           /* 948 */
}

int MrecGetDoorHitInfoIdNum(void)                                       /* 958 */
{
    return map_door_rec_info.id_num;
}

int *MrecGetDoorHitInfoRecNumList(void)                                 /* 967 */
{
    return map_door_rec_info.rec_num_list;
}

float (*MrecGetDoorHitInfoRecVecter(int list_no, int rec_no))[4]        /* 976 */
{
    if (map_door_rec_info.rec_list[list_no] == (MB_OUT_RECT *)0)        /* 979 */
    {
        return (float (*)[4])0;
    }
    return map_door_rec_info.rec_list[list_no][rec_no].vec;             /* 980 */
}

/* Non-zero while door rectangle `rec_no` is solid.  This is the cached-list
 * form of the HitCheck test MrecPointDoorRectangle() does inline. */
int MrecCheckDoorHitSta(int list_no, int rec_no)                        /* 989 */
{
    MDAT_DOOR *door;

    if (map_door_rec_info.rec_list[list_no] == (MB_OUT_RECT *)0)        /* 993 */
    {
        return 0;
    }

    door = (MDAT_DOOR *)RegDatGetStPtr(                                 /* 996 */
        map_door_rec_info.buf_ids[list_no],
        map_door_rec_info.rec_list[list_no][rec_no].reg_id);
    if (door == (MDAT_DOOR *)0)                                         /* 999 */
    {
        return 0;
    }

    return (door->HitCheck != 0);                                       /* 1003 */
}

/* MrecLineCross() against the door list. */
int MrecDoorLineCross(const float *pos1, const float *pos2,
                      int list_no, int num)                             /* 1019 */
{
    float (*vec)[4];
    float vw[4][4];
    int i;

    vec = MrecGetDoorHitInfoRecVecter(list_no, num);                    /* 1024 */
    if (vec == (float (*)[4])0)
    {
        return 0;
    }

    for (i = 0; i < 4; i++)                                             /* 1028 */
    {
        if (HcBaseLineIntersect2(pos1, pos2, vec[i], vec[(i + 1) % 4]) != 0)
        {                                                               /* 1029 */
            break;
        }
    }

    if (i == 4)                                                         /* 1033 */
    {
        return 0;
    }

    if (debug_var.hit_disp != 0)                                        /* 1038 */
    {
        for (i = 0; i < 4; i++)                                         /* 1040 */
        {
            sceVu0CopyVector(vw[i], vec[i]);                            /* 1041 */
            vw[i][1] = pos1[1];                                         /* 1042 */
        }
        Draw3DSquare(vw[0], vw[1], vw[2], vw[3], 0x40, 0xff, 0x40, 0x40);
    }                                                                   /* 1044 */

    return 1;                                                           /* 1047 */
}

/* MrecIsNearRectangle() against the door list. */
int MrecIsNearDoorRectangle(float *boxmin, float *boxmax, int list_no, int num)
{                                                                       /* 1059 */
    float (*vec)[4];
    float vecmin[4];
    float vecmax[4];
    int i;
    int j;

    vec = MrecGetDoorHitInfoRecVecter(list_no, num);                    /* 1063 */
    if (vec == (float (*)[4])0)
    {
        return 0;
    }

    sceVu0CopyVector(vecmax, vec[0]);
    sceVu0CopyVector(vecmin, vec[0]);
    for (i = 1; i < 4; i++)
    {
        for (j = 0; j < 4; j++)
        {
            if (vec[i][j] > vecmax[j])
            {
                vecmax[j] = vec[i][j];
            }
            if (vec[i][j] < vecmin[j])
            {
                vecmin[j] = vec[i][j];
            }
        }
    }

    if (vecmin[0] > boxmax[0])                                          /* 1071 */
    {
        return 0;
    }
    if (boxmin[0] > vecmax[0])
    {
        return 0;
    }
    if (vecmin[2] > boxmax[2])                                          /* 1072 */
    {
        return 0;
    }
    if (boxmin[2] > vecmax[2])
    {
        return 0;
    }

    return 1;                                                           /* 1076 */
}

/* ---- event rectangles --------------------------------------------------
 * Event rectangles are addressed by label rather than by type: the label
 * names one registration record, and RegDatVecFind4Label() walks every
 * rectangle registered against it. */

int MrecIsInEventSub(const float *pos, int buf_id, int label)           /* 436 */
{
    MB_OUT_RECT *rec;

    if (RegDatVecFind4Label(buf_id, label) < 0)                         /* 440 */
    {
        return 0;
    }

    rec = RegDatVecNextFind(buf_id);                                    /* 443 */
    while (rec != (MB_OUT_RECT *)0)                                     /* 444 */
    {
        if (MrecIsInRectangle(pos, rec->vec) != 0)                      /* 445 */
        {
            return 1;                                                   /* 446 */
        }
        rec = RegDatVecNextFind(buf_id);                                /* 448 */
    }

    return 0;                                                           /* 451 */
}

/* A negative buffer id means the position was ambiguous -- it sits on a seam
 * between rooms -- so every buffer in the hit list is tried and any one of
 * them containing the label counts. */
int MrecIsInEvent(const float *pos, int label, int floor)               /* 395 */
{
    int buff_id;
    int i;

    buff_id = RegDatGetBuffIDG(floor, (float *)pos);                    /* 399 */

    if (buff_id < 0)                                                    /* 401 */
    {
        int *list = RegDatGetHitList();                                 /* 404 */

        for (i = 0; i < RegDatGetHitNum(); i++)                         /* 407 */
        {
            if (MrecIsInEventSub(pos, list[i], label) != 0)             /* 408 */
            {
                return 1;
            }
        }
        return 0;
    }

    return (MrecIsInEventSub(pos, buff_id, label) != 0);                /* 421 */
}

/* ---- debug rectangle display -------------------------------------------
 * All of these are gated on debug_var.hit_disp and draw the registration
 * rectangles of one type as wire quads, lifted to the caller's height because
 * the stored rectangles are flat at y == 0.  The `*One` variants draw a single
 * indexed rectangle in a brighter shade so a specific hit can be picked out of
 * the field.  None of them handle the ambiguous -2/-3 buffer ids -- on a seam
 * they simply draw nothing, which is the ROM's behaviour.
 *
 * PORT NOTE: the ROM has eight separate copies of this body, one per entry
 * point, differing only in rectangle type, colour and the height bias.  They
 * are factored into the two helpers below; every difference is carried as an
 * argument, so behaviour is identical, but a future matching pass should
 * expect eight bodies here rather than two. */

static int MrecDrawRectAll(float *pos, int type, float y_bias,
                           u_char r, u_char g, u_char b, u_char a)
{
    MapRecBufCtrl *buf_ctrl = MrecGetRegBuffID(0);
    MB_OUT_RECT   *rec;
    float v[4][4];
    int num;
    int i;
    int j;

    if (buf_ctrl->buf_id < 0)
    {
        return 0;
    }

    num = RegDatGetVecNum(buf_ctrl->buf_id, type);
    rec = RegDatGetVecPtr(buf_ctrl->buf_id, type);

    for (i = 0; i < num; i++)
    {
        for (j = 0; j < 4; j++)
        {
            sceVu0CopyVector(v[j], rec[i].vec[j]);
            v[j][1] = pos[1] - y_bias;
        }
        Draw3DSquare(v[0], v[1], v[2], v[3], r, g, b, a);
    }

    return 1;
}

static int MrecDrawRectOne(int i, float *pos, int type,
                           u_char r, u_char g, u_char b, u_char a)
{
    MapRecBufCtrl *buf_ctrl = MrecGetRegBuffID(0);
    MB_OUT_RECT   *rec;
    float v[4][4];
    int j;

    if (buf_ctrl->buf_id < 0)
    {
        return 0;
    }

    rec = RegDatGetVecPtr(buf_ctrl->buf_id, type);

    for (j = 0; j < 4; j++)
    {
        sceVu0CopyVector(v[j], rec[i].vec[j]);
        v[j][1] = pos[1];
    }
    Draw3DSquare(v[0], v[1], v[2], v[3], r, g, b, a);

    return 1;
}

int DrawCameraRect(float *pos)                                          /* 1195 */
{
    if (debug_var.hit_disp == 0)                                        /* 1202 */
    {
        return 0;
    }
    return MrecDrawRectAll(pos, 1, 0.0f, 0x20, 0x80, 0x80, 0x40);
}

int DrawCameraRectOne(int i, float *pos)                                /* 1227 */
{
    if (debug_var.hit_disp == 0)                                        /* 1234 */
    {
        return 0;
    }
    return MrecDrawRectOne(i, pos, 1, 0x40, 0x80, 0x80, 0x40);
}

int DrawEventRect(float *pos)                                           /* 1258 */
{
    if (debug_var.hit_disp == 0)                                        /* 1265 */
    {
        return 0;
    }
    return MrecDrawRectAll(pos, 2, 0.0f, 0x20, 0x80, 0x80, 0x40);
}

int DrawEventRectOne(int i, float *pos)                                 /* 1291 */
{
    if (debug_var.hit_disp == 0)                                        /* 1298 */
    {
        return 0;
    }
    return MrecDrawRectOne(i, pos, 2, 0x40, 0x80, 0x80, 0x40);
}

/* The only one with a height bias: wall rectangles are dropped 0.1 below the
 * reference height (lit4 3ee4b4) so they do not z-fight with the floor mesh. */
int DrawMapHitRect(float *pos)                                          /* 1322 */
{
    if (debug_var.hit_disp == 0)                                        /* 1329 */
    {
        return 0;
    }
    return MrecDrawRectAll(pos, 0, 0.1f, 0x80, 0x40, 0x20, 0x40);
}

int DrawMapHitRectOne(int i, float *pos)                                /* 1353 */
{
    if (debug_var.hit_disp == 0)                                        /* 1360 */
    {
        return 0;
    }
    return MrecDrawRectOne(i, pos, 0, 0x20, 0x40, 0x80, 0x40);
}

int DrawSeRect(float *pos)                                              /* 1381 */
{
    if (debug_var.hit_disp == 0)                                        /* 1388 */
    {
        return 0;
    }
    return MrecDrawRectAll(pos, 4, 0.0f, 0x20, 0x80, 0x80, 0x40);
}

int DrawSeRectOne(int i, float *pos)                                    /* 1413 */
{
    if (debug_var.hit_disp == 0)                                        /* 1420 */
    {
        return 0;
    }
    return MrecDrawRectOne(i, pos, 4, 0x40, 0x80, 0x80, 0x40);
}
