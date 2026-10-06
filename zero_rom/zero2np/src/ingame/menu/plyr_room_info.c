// FILE: /home/zero_rom/zero2np/src/ingame/menu/plyr_room_info.c
//
// Room-in tracker.  One byte per room label says whether the player has ever
// stood in that room; the in-game map draws only the rooms whose byte is set,
// and the array is handed to the save system verbatim.
//
// The room test does not happen in the world.  Each room's footprint lives in
// map_area_dat[] as an axis-aligned quad in *map-sheet* units, so the player's
// world position is first folded down by ChangeWorldPosToWinPos() -- divide by
// the map's world divisor, offset by the sheet origin, scale by the sheet zoom
// -- and only then tested against the quads.  That is why every corner goes
// through RoomInfoSetPosition() first: the quads are stored unscaled.
//
// Reconstructed from the Feb 6 2004 prototype (SLES_523.84), plyr_room_info.o
// 0x002400e8..0x00240ab7.

#include "plyr_room_info.h"

#include <stdio.h>

#include "menu_map.h"                   /* MenuMapHitCheck / GetMapLabelFromAreaLabel */
#include "ghost_seal_door.h"            /* GhostSealDoorMain                 */
#include "tim_dat/map_area_dat.h"       /* map_area_dat[]                    */
#include "tim_dat/map_room_dat.h"       /* room_info_dat[] / house_info_dat[] */
#include "tim_dat/map_size_dat.h"       /* map_world_point[] / map_scall_dat[] */
#include "../plyr/player.h"             /* GetPlyrAreaNo / GetPlyrFloor      */
#include "../../common/variable.h"      /* plyr_wrk                          */
#include "../../common/utility2.h"      /* PRINT_ASSERT                      */
#include "../../graphics/graph3d/ctl/fixed_array.h"

/* The ROM's bounds come from #defines, which leave no trace in the debug
 * info; the names are this port's, the values are the ROM's compares. */
#define ROOM_LABEL_MAX      240         /* 0xf0 -- also sizeof(room_in_info) */
#define MAP_LABEL_MAX       18          /* 0x12                              */
#define AREA_LABEL_MAX      66          /* 0x42                              */
#define FLOOR_LABEL_MIN     10
#define FLOOR_LABEL_MAX     14

/* Maps 0 and 13 are laid out at half the density of the rest, so world units
 * divide down by 100 instead of 50 before the sheet offset is applied. */
#define MAP_LABEL_WIDE_A    0
#define MAP_LABEL_WIDE_B    13

typedef struct                                          /* 0x1 */
{
    /* 0x0 */ char info;
} ROOM_IN_INFO;

static fixed_array<ROOM_IN_INFO, ROOM_LABEL_MAX> room_in_info;          /* bss 4bbcf0 */

static void HouseInCheck(int room_label);
static int  RoomInfoRoomHitCheck(int map_label, int area_label, float x, float y);
static void RoomInfoSetPosition(float *pos, float x, float y, int map_label);

void RoomInInfoInit(void)                                               /* 61 */
{
    int i;

    for (i = 0; i < ROOM_LABEL_MAX; i++)                                /* 65 */
    {
        room_in_info[i].info = 0;                                       /* 71 */
    }
}

/* Note that GhostSealDoorMain() is called with the raw result, -1 included --
 * it is outside the "player is somewhere" guard on purpose. */
void RoomInCheckMain(void)
{
    int room_label;

    room_label = GetPlyrRoomLabel();                                    /* 105 */

    if (room_label != -1)                                               /* 116 */
    {
        room_in_info[room_label].info = 1;
        HouseInCheck(room_label);                                       /* 120 */
    }

    GhostSealDoorMain(room_label);                                      /* 125 */
}

/* Some rooms stand in for a whole building on the map: walking into any room
 * of the range also lights the building's own label.  Both tables terminate
 * on a -1 record instead of carrying a count. */
static void HouseInCheck(int room_label)                                /* 135 */
{
    int i;
    int j;

    for (i = 0; house_info_dat[i].room_label != -1; i++)                /* 142 */
    {
        for (j = house_info_dat[i].start_room_label;                    /* 147 */
             j <= house_info_dat[i].end_room_label; j++)
        {
            if (room_label == j)                                        /* 149 */
            {
                room_in_info[house_info_dat[i].room_label].info = 1;
                break;
            }
        }                                                               /* 154 */
    }                                                                   /* 156 */
}

/* Walks every room quad for `map_label` and returns the first one that
 * contains (x, y).  An entry with area_label == -1 applies on every area;
 * otherwise the entry only counts while the player is in that area, which is
 * how two areas can overlay different room layouts on one sheet. */
static int RoomInfoRoomHitCheck(int map_label, int area_label,          /* 169 */
                                float x, float y)
{
    int   res;
    int   i;
    float target[4];
    float tri0[4];
    float tri1[4];
    float tri2[4];
    float tri3[4];

    if (map_label >= MAP_LABEL_MAX)                                     /* 180 */
    {
        PRINT_ASSERT("Error! RoomInfoRoomHitCheck map_label %d", map_label); /* 181 */
    }

    if (map_label < 0)                                                  /* 183 */
    {
        /* Bare printf, no banner -- matching the ROM, which only wraps the
         * upper-bound check in the assert macro. */
        printf("Error! RoomInfoRoomHitCheck map_label %d", map_label);   /* 184 */
    }

    res = -1;                                                           /* 188 */

    target[0] = x;                                                      /* 190 */
    target[1] = 0.0f;                                                   /* 191 */
    target[2] = y;                                                      /* 192 */
    target[3] = 1.0f;                                                   /* 193 */

    for (i = 0; map_area_dat[i].map_label != -1; i++)                   /* 198 */
    {
        if (map_area_dat[i].map_label != map_label)                     /* 202 */
        {
            continue;
        }

        if ((map_area_dat[i].area_label != -1) &&                       /* 203 */
            (map_area_dat[i].area_label != area_label))
        {
            continue;
        }

        RoomInfoSetPosition(tri0, map_area_dat[i].pos[0][0],            /* 206 */
                            map_area_dat[i].pos[0][1], map_label);
        RoomInfoSetPosition(tri1, map_area_dat[i].pos[1][0],            /* 207 */
                            map_area_dat[i].pos[1][1], map_label);
        RoomInfoSetPosition(tri2, map_area_dat[i].pos[2][0],            /* 208 */
                            map_area_dat[i].pos[2][1], map_label);
        RoomInfoSetPosition(tri3, map_area_dat[i].pos[3][0],            /* 209 */
                            map_area_dat[i].pos[3][1], map_label);

        if (MenuMapHitCheck(target, tri0, tri1, tri2, tri3) != 0)       /* 212 */
        {
            res = map_area_dat[i].room_label;                           /* 214 */
            break;
        }
    }

    return res;                                                         /* 221 */
}

/* Corner -> hit-test point.  The quads are stored at unit scale, so each
 * corner picks up the sheet zoom here; y goes into [2] because the test runs
 * in the XZ plane. */
static void RoomInfoSetPosition(float *pos, float x, float y, int map_label)
{
    pos[0] = x * map_scall_dat[map_label].normal;                       /* 234 */
    pos[1] = 0.0f;                                                      /* 235 */
    pos[2] = y * map_scall_dat[map_label].normal;                       /* 236 */
    pos[3] = 1.0f;                                                      /* 237 */
}

int GetPlyrRoomLabel(void)
{
    return GetRoomLabel(GetPlyrAreaNo(), GetPlyrFloor(),                /* 274 */
                        plyr_wrk.cmn_wrk.mbox.pos);
}

int GetRoomLabel(int area_label, int floor_label, float *pos)           /* 285 */
{
    int   map_label;
    int   room_label;
    float pos_x;
    float pos_y;

    if (area_label >= AREA_LABEL_MAX)                                   /* 293 */
    {
        PRINT_ASSERT("Error! GetRoomLabel area_label %d", area_label);   /* 294 */
    }

    if ((floor_label < FLOOR_LABEL_MIN) || (floor_label > FLOOR_LABEL_MAX)) /* 296 */
    {
        PRINT_ASSERT("Error! GetRoomLabel floor_label %d", floor_label); /* 297 */
    }

    room_label = -1;                                                    /* 300 */
    pos_x = 0.0f;                                                       /* 301 */
    pos_y = 0.0f;

    map_label = GetMapLabelFromAreaLabel(area_label, floor_label);      /* 305 */

    if (map_label != -1)                                                /* 308 */
    {
        ChangeWorldPosToWinPos(&pos_x, &pos_y, map_label, pos, 0);      /* 310 */
        room_label = RoomInfoRoomHitCheck(map_label, area_label,        /* 313 */
                                          pos_x, pos_y);
    }

    return room_label;                                                  /* 334 */
}

/* World -> map sheet.  The sheet's y axis runs opposite to world Z, hence the
 * subtraction on pos_y.  A `scall` other than 0 or 1 leaves both outputs
 * alone rather than reporting anything. */
void ChangeWorldPosToWinPos(float *pos_x, float *pos_y, int map_label,
                            float *pos, int scall)
{
    if (scall == 0)                                                     /* 349 */
    {
        if ((map_label == MAP_LABEL_WIDE_A) ||                          /* 350 */
            (map_label == MAP_LABEL_WIDE_B))
        {
            *pos_x = (pos[0] / 100.0f) * map_scall_dat[map_label].normal    /* 351 */
                     + map_world_point[map_label].x * map_scall_dat[map_label].normal;
            *pos_y = map_world_point[map_label].y * map_scall_dat[map_label].normal  /* 353 */
                     - (pos[2] / 100.0f) * map_scall_dat[map_label].normal;
        }
        else
        {
            *pos_x = (pos[0] / 50.0f) * map_scall_dat[map_label].normal     /* 356 */
                     + map_world_point[map_label].x * map_scall_dat[map_label].normal;
            *pos_y = map_world_point[map_label].y * map_scall_dat[map_label].normal  /* 358 */
                     - (pos[2] / 50.0f) * map_scall_dat[map_label].normal;
        }
    }
    else if (scall == 1)                                                /* 361 */
    {
        float div;

        if ((map_label == MAP_LABEL_WIDE_A) ||                          /* 362 */
            (map_label == MAP_LABEL_WIDE_B))
        {
            div = 100.0f;                                               /* 363 */
        }
        else
        {
            div = 50.0f;                                                /* 365 */
        }

        *pos_x = (pos[0] / div) * map_scall_dat[map_label].big          /* 368 */
                 + map_world_point[map_label].x * map_scall_dat[map_label].big;
        *pos_y = map_world_point[map_label].y * map_scall_dat[map_label].big /* 370 */
                 - (pos[2] / div) * map_scall_dat[map_label].big;
    }
}

/* Despite the name it goes the other way: room label -> map label.  The scan
 * stops on the -1 terminator, and the terminator's own map_label (-1) is what
 * an unregistered room gets back -- there is no separate not-found path. */
int GetMapLabelToRoomLabel(int room_label)
{
    int i;

    if (room_label >= ROOM_LABEL_MAX)                                   /* 387 */
    {
        PRINT_ASSERT("Error! GetMapLabelToRoomLabel room_label %d", room_label); /* 388 */
    }

    if (room_label < 0)                                                 /* 390 */
    {
        printf("Error! GetMapLabelToRoomLabel room_label %d", room_label); /* 391 */
    }

    i = 0;                                                              /* 395 */

    while (room_info_dat[i].map_label != -1)                            /* 397 */
    {
        if (room_info_dat[i].room_label == room_label)                  /* 401 */
        {
            break;                                                      /* 402 */
        }

        i++;                                                            /* 404 */
    }

    return room_info_dat[i].map_label;                                  /* 407 */
}

/* Both range checks are advisory: the subscript happens either way, which is
 * the ROM's behaviour -- the fixed_array bounds check is what actually stops
 * an out-of-range read. */
int GetRoomInfo(int room_label)
{
    if (room_label >= ROOM_LABEL_MAX)                                   /* 420 */
    {
        PRINT_ASSERT("Error! GetRoomInfo room_label %d", room_label);    /* 421 */
    }

    if (room_label < 0)                                                 /* 423 */
    {
        printf("Error! GetRoomInfo room_label %d", room_label);          /* 424 */
    }

    return room_in_info[room_label].info;
}

void SetSave_RoomInInfo(MC_SAVE_DATA *data)                             /* 463 */
{
    data->size = ROOM_LABEL_MAX * sizeof(ROOM_IN_INFO);                 /* 467 */
    data->addr = (u_char *)&room_in_info[0];
}

void DebugAllMapDisp(void)                                              /* 479 */
{
    int i;

    for (i = 0; i < ROOM_LABEL_MAX; i++)                                /* 484 */
    {
        room_in_info[i].info = 1;                                       /* 486 */
    }
}
