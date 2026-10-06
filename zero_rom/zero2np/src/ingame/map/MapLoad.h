/* ==========================================================================
 *  ingame/map/MapLoad.h
 *
 *  Room loader interface.  Two room buffers are held in MapLoadBuff; the
 *  player occupies one and the neighbouring room streams into the other, so
 *  a door transition is a buffer swap rather than a reload.
 *
 *  Reconstructed from the Feb 6 2004 prototype (SLES_523.84).
 * ======================================================================== */

#ifndef _INGAME_MAP_MAPLOAD_H
#define _INGAME_MAP_MAPLOAD_H

#include <stdint.h>

#include "eetypes.h"
#include "graphics/graph3d/gra3dTypes.h"
#include "graphics/graph3d/ctl/fixed_array.h"

/* --------------------------------------------------------------------------
 *  Per-room-buffer header.
 *
 *  This is a RUNTIME structure (MapLoadBuff lives in bss at 0x407ab0), not an
 *  on-disc layout, so it is safe to widen for the 64-bit host -- nothing is
 *  ever read into it straight from a file.  The address fields hold whatever
 *  the loader and the pak helpers hand back, and on this port those are real
 *  host pointers (MapDrawRegistModel stores GetFileInPak() results into
 *  model_addr / shadow_addr / shadow_s_addr), so they must be pointer-width or
 *  they silently truncate.  The offsets below are the original PS2 ones and
 *  are kept for reference only; the host build's layout differs.
 *
 *  Contrast the on-disc structures (MB_OUT_HEAD and friends in RegDat.h),
 *  whose 32-bit fields must NOT be widened -- doing so would shift every
 *  following field and mis-parse the file.
 *
 *  reg_id[] is SIGNED: empty slots hold -1 and every test in MapLoad.c is
 *  `!= -1` / `-1 < x`, emitted as `lb` in the original.
 * ------------------------------------------------------------------------ */
typedef struct                          /* 0x13f0 on PS2 */
{
    /* 0x0000 */ int                  labelID;
    /* 0x0004 */ int                  buff_id;
    /* 0x0008 */ int                  stat;
    /* 0x000c */ char                *addr;
    /* 0x0010 */ float                pos[4];
    /* 0x0020 */ uintptr_t            model_addr;
    /* 0x0024 */ uintptr_t            reg_dat_top;
    /* 0x0028 */ uintptr_t            free_mem_top;
    /* 0x002c */ uintptr_t            high_addr;
    /* 0x0030 */ uintptr_t            model_pak_addr;
    /* 0x0034 */ uintptr_t            lit_addr;
    /* 0x0038 */ uintptr_t            shadow_addr;
    /* 0x003c */ uintptr_t            shadow_s_addr;
    /* 0x0040 */ GRA3DLIGHTDATA       lit_dat;
    /* 0x13e0 */ fixed_array<char, 4> reg_id;
} MLOAD_HEAD;

/* --------------------------------------------------------------------------
 *  Door record (MapDoor.c owns the table; MapLoadUpdatRoomDat reads it).
 *
 *  The three-word hole after `se` is the ROM's own: the vectors start on a
 *  quadword boundary.  It is spelled out here so the offsets below are true on
 *  this host too -- without it the compiler would pack player_pos at 0x14 and
 *  every documented offset from there down would be a lie.  The bytes are zero
 *  in the shipped table.
 * ------------------------------------------------------------------------ */
typedef struct                          /* 0x60 */
{
    /* 0x00 */ int      doorID;
    /* 0x04 */ int      room_no[2];
    /* 0x0c */ int      attribute;
    /* 0x10 */ u_short  type;
    /* 0x12 */ u_short  se;
    /* 0x14 */ int      pad[3];
    /* 0x20 */ float    player_pos[4];
    /* 0x30 */ float    sister_bpos[4];
    /* 0x40 */ float    sister_pos[4];
    /* 0x50 */ float    player_rot;
    /* 0x54 */ float    sister_brot;
    /* 0x58 */ float    sister_rot;
    /* 0x5c */ int      pad2[1];     /* PS2 quadword tail padding */
} MLOAD_DOOR_DAT;

int         MapLoadGetLabel(int _room_no);
int         MapLoadCheckDrawFlg(int buff_id);
int         MapLoadCheckLoadNow(void);
char       *MapLoadInitFreeMem(int buff_id);
char       *MapLoadGetFreeMemAddr(int buff_id);
void        MapLoadSetFreeMemAddr(int buff_id, char *addr);
void       *MapLoadGetDoorBuffPtr(void);
int         MapLoadGetFileID(int stat);
MLOAD_HEAD *MapLoadGetHeadPtr(int id);
int         MapLoadGetBuffID4Label(int label);
void        MapLoadSetNoRegList(void);
MLOAD_HEAD *MapLoadGetHeader(void);
int         MapLoadGetRegBuffID(int room_no, int kai);
void        MapLoadSetNowRoom(int room_no);
void        MapLoadSetDrawFlgSub(int buff_id, int sw);
void        MapLoadSetDrawFlg(int flg, int sw);
void        MapLoadSetDrawFlg2(int buff_id, int sw);
void        MapLoadSetDrawFlg3(int room_no, int sw);
void        MapLoadSetDrawOnly(int room_no);
int        *MapLoadGetFreeArea(void);
void        MapLoadSetOffSet(int room_no, float x, float y, float z);
float      *MapLoadGetOffset(int room_no);
void        MapLoadGetOffsetVector(float *Center, int RoomNo);
int         MapLoadGetBuffID4Pos(int kai, float *vPos);
int         MapLoadGetBuffID(int room_no);
int         MapLoadGetRoomNo4BuffID(int buff_id);
int         MapLoadGetRoomNoNow(void);
int         MapLoadSwitch(void);
void        MapLoadBg(int room_no);
int         MapLoadDrawRoomOne(int room_no);
int         MapLoadInitRoom(void);
void        MapLoadDeleteRoom(int b_id);
void        MapLoadDeleteRoomAll(void);
int         MapLoadMoveRoom(int room_no);
int         MapLoadReload(int room_no);
int         MapLoadUpdatRoomDat(int door_id);
int         MapLoadMain(void);
uintptr_t   MapLoadSetMemSpace(uintptr_t st_addr, uintptr_t en_addr);
int         MapLoadInit(int room_no);

#endif /* _INGAME_MAP_MAPLOAD_H */
