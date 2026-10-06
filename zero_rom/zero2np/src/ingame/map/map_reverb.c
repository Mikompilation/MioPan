// FILE: /home/zero_rom/zero2np/src/ingame/map/map_reverb.c
//
// Per-room reverb depth.  map_reverb_tbl[] is a straight area-id -> GS reverb
// depth map baked into the disc; unlike map_bgm.c there is no per-save
// working copy to override, so map_reverbMain() reads the table directly and
// only re-applies SndSetEffect() when the area actually changes.
//
// Reconstructed from the Feb 6 2004 prototype (SLES_523.84), map_reverb.o
// 0x001dee78..0x001df010.

#include "map_reverb.h"

#include "../plyr/player.h"              /* GetPlyrAreaNo */
#include "../../system/eeiop/snd.h"       /* SndSetEffect */

#include <stdio.h>

#define MAP_REVERB_AREA_NUM 66

/* struct _MAP_REVERB_DAT (types.txt), 0x2 bytes. */
typedef struct
{
    short depth;
} MAP_REVERB_DAT;

static MAP_REVERB_DAT map_reverb_tbl[MAP_REVERB_AREA_NUM] =            /* data 31c7d8 */
{
    { 10376 }, { 12287 }, {  8191 }, {  8191 }, {  8191 }, { 10376 }, { 12287 }, { 12287 }, { 10376 }, { 10376 },
    { 12287 }, { 10376 }, { 12287 }, { 12287 }, { 16383 }, { 10376 }, { 10376 }, { 10376 }, { 14472 }, { 12287 },
    { 14472 }, { 12287 }, { 12287 }, {  8191 }, { 16383 }, {  8191 }, { 20479 }, { 12287 }, { 16383 }, { 12287 },
    { 12287 }, { 14472 }, { 12287 }, { 10376 }, {  8191 }, { 16383 }, { 12287 }, { 20479 }, { 12287 }, { 16383 },
    { 12287 }, { 16383 }, { 18568 }, { 16383 }, { 12287 }, { 14472 }, { 12287 }, { 22664 }, { 12287 }, { 22664 },
    {  4095 }, { 12287 }, { 12287 }, { 20479 }, { 12287 }, { 16383 }, { 24575 }, { 12287 }, { 32767 }, { 28671 },
    { 28671 }, { 24575 }, { 28671 }, { 32767 }, { 32767 }, { 12287 }
};

static int rev_room_id_save;                                            /* sbss 3f4d98 */

void map_reverbInit(void)
{
    rev_room_id_save = -1;
}

void map_reverbAfterMCLoadInit(void)
{
    SndSetEffect(0, map_reverb_tbl[rev_room_id_save].depth, 3);
}

void map_reverbMain(void)
{
    u_int area_no = GetPlyrAreaNo();

    if (MAP_REVERB_AREA_NUM <= area_no)
    {
        printf("ROOM_ID_OVER[%d] : map_reverb.c\n", area_no);
        return;
    }

    if (area_no != rev_room_id_save)
    {
        SndSetEffect(0, map_reverb_tbl[area_no].depth, 3);
        rev_room_id_save = area_no;
    }
}
