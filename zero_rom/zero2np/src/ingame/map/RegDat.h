/* ==========================================================================
 *  ingame/map/RegDat.h
 *
 *  Map registration-data interface.  A room's registration file ("pzb") is a
 *  flat block: an MB_OUT_HEAD, a run of variable-length MB_OUT_SECTION
 *  records (the placements -- doors, objects, cameras, fog, ...) and a run of
 *  variable-length MB_OUT_RECT rectangles that bind positions to those
 *  records.  Up to eight files are resident at once, one slot each in
 *  RegDatBuff.
 *
 *  Reconstructed from the Feb 6 2004 prototype (SLES_523.84).
 * ======================================================================== */

#ifndef _INGAME_MAP_REGDAT_H
#define _INGAME_MAP_REGDAT_H

#include <stdint.h>

#include "eetypes.h"
#include "../../graphics/graph3d/ctl/fixed_array.h"

#define REG_DAT_BUFF_NUM    8

typedef struct MB_OUT_RECT
{
    /* 0x00 */ u_short RectID;
    /* 0x02 */ u_short size;
    /* 0x04 */ int     reg_id;
    /* 0x08 */ int     pad[2];
    /* 0x10 */ float   vec[4][4];
} MB_OUT_RECT;

/* The file header, which sits at offset 0 of the block -- so `hp` doubles as
 * the file base that reg_vecp / reg_stp are measured from.
 *
 * PORT: the ROM types those two fields as MB_OUT_RECT * / MB_OUT_SECTION *
 * and RegDatRegist() patches them in place from file-relative offset to
 * absolute pointer.  A host pointer does not fit in the four bytes the file
 * reserves, so the port leaves them as the on-disc offsets and resolves them
 * on every read (RegDatVecTop / RegDatStTop in RegDat.c).  The observable
 * difference is that registering the same block into two buffers is harmless
 * here, where the ROM would add the base twice. */
typedef struct MB_OUT_HEAD
{
    /* 0x00 */ char     magic[4];       /* "pzb"                            */
    /* 0x04 */ int      area_id;
    /* 0x08 */ int      kai;            /* floor ("kai") this area sits on  */
    /* 0x0c */ int      pad;
    /* 0x10 */ int      reg_vec_num;
    /* 0x14 */ uint32_t reg_vecp;       /* MB_OUT_RECT *    on target       */
    /* 0x18 */ int      reg_st_num;
    /* 0x1c */ uint32_t reg_stp;        /* MB_OUT_SECTION * on target       */
    /* 0x20 */ float    Pos[4];
} MB_OUT_HEAD;

/* Common preamble of every placement record.  SecStID is the record type --
 * the same numbering the rectangles are grouped by (0 hit, 1/2 camera and
 * event debug rects, 3 object, 4 footstep SE, 5 fog, 6 room outline, 7 door,
 * 8 battle camera, 9 four-corner camera, 10 stairs, 11 put-item, 13 fog
 * switch).  `size` covers the whole record, so it is also the stride to the
 * next one. */
typedef struct MB_OUT_SECTION
{
    /* 0x00 */ u_short  SecStID;
    /* 0x02 */ u_short  size;
    /* 0x04 */ u_int    labelID;
} MB_OUT_SECTION;

#define RECORD_TYPE_HIT                 0
#define RECORD_TYPE_CAMERA_EVENT0       1
#define RECORD_TYPE_CAMERA_EVENT1       2
#define RECORD_TYPE_OBJECT              3
#define RECORD_TYPE_FOOTSTEP_SE         4
#define RECORD_TYPE_FOG                 5
#define RECORD_TYPE_ROOM_OUTLINE        6
#define RECORD_TYPE_DOOR                7
#define RECORD_TYPE_BATTLE_CAMERA       8
#define RECORD_TYPE_FOUR_CORNER_CAMERA  9
#define RECORD_TYPE_STAIRS              10
#define RECORD_TYPE_PUT_ITEM            11
#define RECORD_TYPE_FOG_SWITCH          13
#define REG_DAT_ST_TYPE_NUM             14

/* One placed map object.  The five state fields from HitCheck onwards are what
 * the SET_OBJ_* macro opcodes write; the same values are mirrored into the map
 * save block, because this record only exists while the room is resident. */
typedef struct MDAT_OBJ
{
    /* 0x00 */ MB_OUT_SECTION head;
    /* 0x08 */ int   type;
    /* 0x0c */ char  ModelName[36];
    /* 0x30 */ float Pos[3];
    /* 0x3c */ float Rot[3];
    /* 0x48 */ int   HitCheck;
    /* 0x4c */ int   PhotoAble;
    /* 0x50 */ int   Visible;
    /* 0x54 */ int   Action;
    /* 0x58 */ int   ActionType;
    /* 0x5c */ int   Weight;
    /* 0x60 */ int   Attribute;
} MDAT_OBJ;

/* Ordinary map camera record: one placement plus the follow parameters the
 * MapCamType* modes read.  Asobi ("play"/slack) is the follow margin. */
typedef struct MDAT_CAM
{
    /* 0x00 */ MB_OUT_SECTION head;
    /* 0x08 */ int   type;
    /* 0x0c */ float Pos[3];
    /* 0x18 */ float View[3];
    /* 0x24 */ float Rrg;
    /* 0x28 */ float RotZ;
    /* 0x2c */ float Asobi;
} MDAT_CAM;

/* One corner of a four-corner "special" camera. */
typedef struct MDAT_CAM_SP_ONE
{
    /* 0x00 */ float Cam_Pos_X;
    /* 0x04 */ float Cam_Pos_Y;
    /* 0x08 */ float Cam_Pos_Z;
    /* 0x0c */ float View_Pos_X;
    /* 0x10 */ float View_Pos_Y;
    /* 0x14 */ float View_Pos_Z;
    /* 0x18 */ float Proj;
    /* 0x1c */ float RotZ;
} MDAT_CAM_SP_ONE;

/* Type-9 rectangle camera: the four corner records are blended across the
 * rectangle by map_camera.c.  `type` picks the blend -- 0 = parallel
 * (bilinear across two adjacent edges), otherwise diagonal. */
typedef struct MDAT_CAM_SP
{
    /* 0x00 */ MB_OUT_SECTION  head;
    /* 0x08 */ int             type;
    /* 0x0c */ MDAT_CAM_SP_ONE st[4];
} MDAT_CAM_SP;

/* A placed door.  HitCheck is what map_rectangle.c's door-rectangle test
 * reads: zero means the door is open/passable and its rectangle is ignored. */
typedef struct MDAT_DOOR                /* 0x54 */
{
    /* 0x00 */ MB_OUT_SECTION head;
    /* 0x08 */ char  ModelName[36];
    /* 0x2c */ int   DatID;
    /* 0x30 */ float Pos[3];
    /* 0x3c */ float Rot[3];
    /* 0x48 */ int   HitCheck;
    /* 0x4c */ int   ActionType;
    /* 0x50 */ int   Attribute;
} MDAT_DOOR;

/* A "put" item -- scenery placed into the room independently of the furniture
 * set.  Same preamble as MDAT_OBJ but with a scale, and no HitCheck/PhotoAble:
 * put items are decoration, not interactable. */
typedef struct MDAT_PUT                 /* 0x64 */
{
    /* 0x00 */ MB_OUT_SECTION head;
    /* 0x08 */ char  ModelName[36];
    /* 0x2c */ float Pos[3];
    /* 0x38 */ float Rot[3];
    /* 0x44 */ float Scale[3];
    /* 0x50 */ int   Visible;
    /* 0x54 */ int   Action;
    /* 0x58 */ int   ActionType;
    /* 0x5c */ int   Weight;
    /* 0x60 */ int   Attribute;
} MDAT_PUT;

/* A footstep-sound region.  `Pre` is the priority used to break the tie when
 * several regions overlap the same spot -- the highest wins. */
typedef struct MDAT_SE                  /* 0x10 */
{
    /* 0x00 */ MB_OUT_SECTION head;
    /* 0x08 */ int No;
    /* 0x0c */ int Pre;
} MDAT_SE;

/* One fog setting.  Start/End are the distance ramp and Min/Max the 0..255
 * density at each end of it; MapFog.c truncates Start/End to int on the way
 * into MAP_FOG_HEAD, so the fractional part on disc never survives. */
typedef struct MDAT_FOG_ST_ONE          /* 0x1c */
{
    /* 0x00 */ float Start;
    /* 0x04 */ float End;
    /* 0x08 */ int   Min;
    /* 0x0c */ int   Max;
    /* 0x10 */ int   Color[3];
} MDAT_FOG_ST_ONE;

/* A fog region (rectangle type 5).  Two settings per region: dat[0] is the
 * ordinary one and dat[1] the one MapFog.c picks while the player is looking
 * through the camera obscura (plyr_wrk.cmn_wrk.mode == 6). */
typedef struct MDAT_FOG                 /* 0x40 */
{
    /* 0x00 */ MB_OUT_SECTION  head;
    /* 0x08 */ MDAT_FOG_ST_ONE dat[2];
} MDAT_FOG;

/* Rectangle type 13, same layout.  It overrides a type-5 region where the two
 * overlap, and it hides the sky while the camera is inside it -- MapFog.c
 * casts both to MDAT_FOG and only the rectangle type tells them apart. */
typedef struct MDAT_FOG_SW              /* 0x40 */
{
    /* 0x00 */ MB_OUT_SECTION  head;
    /* 0x08 */ MDAT_FOG_ST_ONE dat[2];
} MDAT_FOG_SW;

/* Per-type index built once at registration time: how many rectangles of that
 * type the file holds, and where the run starts. */
typedef struct RD_REG_ST_DAT            /* 0x8 */
{
    /* 0x0 */ int          st_num;
    /* 0x4 */ MB_OUT_RECT *dat;
} RD_REG_ST_DAT;

/* One resident registration file.  LabVec* is the cursor for the
 * RegDatVecFind4Label / RegDatVecNextFind walk and type_* the cursor for the
 * RegDatGetStPtrStart / RegDatGetNextStPtr walk; both live here rather than
 * in the caller, so only one walk of each kind per buffer can be in flight.
 *
 * Offsets are the target's (0x8c total there); pointer members are wider on
 * the host, so treat them as documentation. */
typedef struct RD_REG_HEAD              /* 0x8c */
{
    /* 0x00 */ fixed_array<RD_REG_ST_DAT, REG_DAT_ST_TYPE_NUM> StPtrList;
    /* 0x70 */ char           *RegDatPtr;
    /* 0x74 */ int             LabVecID;
    /* 0x78 */ int             LabVecNum;
    /* 0x7c */ MB_OUT_RECT    *LabVecPtr;
    /* 0x80 */ int             type_id;
    /* 0x84 */ int             type_search_num;
    /* 0x88 */ MB_OUT_SECTION *type_search_p;
} RD_REG_HEAD;

void         RegDatInit(void);
int          RegDatRegist(char *mst);
int          RegDatSetTopAddr(int buff_id, void *addr);
void         RegDatDeleteBuff(int buff_id);
int          RegDatCheckBuff(int buff_id);
void         RegDatDeleteBuffList(int id);
void         RegDatDeleteAllBuffList(void);
MB_OUT_HEAD *RegDatGetHead(int buff_id);

/* Section (placement-record) lookup.  RegDatGetStPtr() returns u_short * so
 * the first halfword -- the record type -- can be read without a cast; every
 * caller casts the result to the MDAT_* the type implies. */
u_short     *RegDatGetStPtr(int buff_id, int reg_id);
u_int        RegDatGetStLabel(int buff_id, int reg_id);

MB_OUT_RECT *RegDatGetVecPtr(int buff_id, int type);
int          RegDatGetVecNum(int buff_id, int type);
MB_OUT_RECT *RegDatGetVecPtrStart(int buff_id);
int          RegDatGetVecNumAll(int buff_id);
MB_OUT_RECT *RegDatGetNextVecPtr(MB_OUT_RECT *mst);
void         RegDatSetOffset(int buff_id, float x, float y, float z);

/* Walk every section of one type.  Start() arms the cursor, NextStPtr()
 * yields one record per call and NULL at the end. */
void            RegDatGetStPtrStart(int buff_id, int type);
MB_OUT_SECTION *RegDatGetNextStPtr(int buff_id);

/* Labels pack area and record: label / 1000 is the area id, label % 1000 the
 * record index within it.  Negative returns are diagnostic; callers test < 0. */
int          RegDatGetStID4Label(int buff_id, int label);
u_short     *RegDatGetStPtr4Label(int buff_id, int label);

/* The same lookups for callers that only hold a label: the buffer is resolved
 * from the label's area.  Both report -1 / NULL when no loaded buffer covers
 * that area.  ...4Label3() additionally requires the record to be of `type`,
 * and asserts when it is not. */
int          RegDatBuffID4Label(int labelID);
u_short     *RegDatGetStPtr4Label2(int label);
u_short     *RegDatGetStPtr4Label3(int label, int type);

/* Walk every rectangle registered against `label`.  Find4Label() arms the
 * cursor (0 = ok, negative = nothing to walk); NextFind() yields one rectangle
 * per call and NULL at the end.  The cursor is per-buffer state, so only one
 * walk per buffer can be in flight at a time. */
int          RegDatVecFind4Label(int buff_id, int label);
MB_OUT_RECT *RegDatVecNextFind(int buff_id);

/* Buffers parked on the no-register list are skipped by the position lookups
 * -- that is how a door transition keeps the room being left from answering
 * "which room is the player in?". */
void         RegDatResetNoRegistList(void);
void         RegDatAddNoRegistList(int id);

/* Which resident buffer covers vPos (kai == -1 matches any floor).  The
 * return encodes ambiguity as well as the answer: -1 nothing, -2 every buffer
 * hit, -3 more than one -- and for -3 the caller re-reads the full set through
 * RegDatGetHitList() / RegDatGetHitNum(). */
int          RegDatGetBuffIDG(int kai, const float *vPos);
int          RegDatGetBuffID(float *vPos);
int         *RegDatGetHitList(void);
int          RegDatGetHitNum(void);

/* Registration-record lookup by position.  `type` is the rectangle type
 * (0 hit, 6 room outline, 8 battle camera, 9 special camera, ...).
 * pStat/pRectStat let a caller say "prefer the record I already hold". */
void *RegDatGetStat(int kai, float *vPos, int type);
void *RegDatGetRectAndStat(MB_OUT_RECT **ppRect, void *pRectStat, int kai,
                           float *vPos, int type);
void *RegDatGetRectAndStat2(MB_OUT_RECT **ppRect, void *pStat, int buff_id,
                            float *vPos, int type);
int   RegDatCheckSameRectStat(void *pStat, int kai, float *vPos, int type);

#endif /* _INGAME_MAP_REGDAT_H */
