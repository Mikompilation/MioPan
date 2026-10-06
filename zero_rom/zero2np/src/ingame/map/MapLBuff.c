// FILE: /home/zero_rom/zero2np/src/ingame/map/MapLBuff.c
//
// Map load buffer: a reference-counted list of the files an event has asked to
// be kept resident for a given map.  66 maps (matching the map_id <= 0x41
// bound ev_macro.c asserts) times 16 slots, each slot a label ID and a count.
//
// Only half the module works in this build.  The reference counting -- taking
// a slot in MapLBuffSetLoadFile() and giving it back in MapLBuffDeleteFile()
// -- is complete and is what LOAD_REQUEST / RELEASE_REQUEST drive.  The half
// that would do something with the resulting list, MapLBuffRegist() and
// MapLBuffLoad(), classifies every listed label through FileStGetType() and
// then does nothing with the answer; both compile to a validation walk over an
// empty switch.  Neither has a caller anywhere in the ROM, and neither does
// MapLBuffInit() -- verified by scanning every jal in the image.
//
// That last fact has a real consequence, so it is spelled out rather than
// quietly fixed: MapLBuffList lives in .bss and MapLBuffInit() is the only
// thing that would stamp the free marker (-1) into it.  Uninitialised, every
// slot reads labelID == 0, which is neither "free" nor any real label, so
// MapLBuffGetSpace() finds no match and no free slot and returns NULL for any
// label but 0.  LOAD_REQUEST therefore always takes the ERR!_NO_FREE_SPACE
// path in the shipped prototype.  Reproduced as-is; calling MapLBuffInit()
// from the room load path would deviate from the ROM.
//
// The third field, `stat`, is zeroed by MapLBuffGetSpace() and MapLBuffInit()
// and never read -- it is the load-state the unfinished Regist/Load pair would
// have driven.
//
// Reconstructed from the Feb 6 2004 prototype (SLES_523.84), MapLBuff.o
// 0x00109508..0x0010981b.

#include "MapLBuff.h"
#include "../../common/utility2.h"            /* PRINT_ERROR */

#include "../../common/FileSt.h"       /* FileStGetType */

#include <stdio.h>

/* Declared in the .c rather than the header: MAP_LBUFF_ST is the last type in
 * MapLBuff.o's stab block, after every included header's types, and it appears
 * in no other object -- ev_macro.c, which includes MapLBuff.h for the two
 * entry points it calls, does not carry it. */
typedef struct                                                          /* 0xc */
{
    /* 0x0 */ int labelID;
    /* 0x4 */ int cnt;
    /* 0x8 */ int stat;
} MAP_LBUFF_ST;

/* One row per map, one column per resident file.  labelID == -1 marks a free
 * slot; `cnt` is the number of outstanding LOAD_REQUESTs against it. */
static MAP_LBUFF_ST MapLBuffList[66][16];                               /* bss 400e30 */


/* Regist and Load are the same walk with a different error string -- the ROM
 * emits byte-identical code for both bodies.  The switch has arms for types
 * 0, 1 and 2 (GCC collapsed them into the single range test 0 <= type <= 2,
 * which is how we know there were three), but every arm is empty, so the only
 * reachable effect is the default arm's complaint.  FileStGetType() only ever
 * answers 0 or 1, so even that is unreachable.
 *
 * The two switches occupy a different number of source lines -- 46..55 here
 * against 79..91 in MapLBuffLoad() -- so the arms did hold something in the
 * original file, commented out or preprocessed away.  Left empty rather than
 * invented. */
int MapLBuffRegist(int map_id)
{                                                                       /* 35 */
    int j;

    for (j = 0; j < 16; j++)                                            /* 39 */
    {
        if (MapLBuffList[map_id][j].labelID == -1)                      /* 42 */
        {
            continue;
        }

        switch (FileStGetType(MapLBuffList[map_id][j].labelID))         /* 45 */
        {
        case 0:
            break;

        case 1:
            break;

        case 2:
            break;

        default:
            PRINT_ERROR("ERR! LOAD_FILE_NO_TYPE\n");                    /* 56 */
            return -1;                                                  /* 57 */
        }
    }                                                                   /* 59 */

    return 0;                                                           /* 61 */
}                                                                       /* 62 */

/* See MapLBuffRegist().  The error here is a bare printf pair rather than
 * PRINT_ERROR, so it carries no ***ERR!! banner -- the author's own signature
 * string stands in for the file/line. */
int MapLBuffLoad(int map_id)
{                                                                       /* 68 */
    int j;

    for (j = 0; j < 16; j++)                                            /* 72 */
    {
        if (MapLBuffList[map_id][j].labelID == -1)                      /* 75 */
        {
            continue;
        }

        switch (FileStGetType(MapLBuffList[map_id][j].labelID))         /* 78 */
        {
        case 0:
            break;

        case 1:
            break;

        case 2:
            break;

        default:
            printf("ERR! LOAD_FILE_NOT_TYPE  MapLBuff.c/J.serizawa\n"); /* 92 */
            return -1;                                                  /* 93 */
        }
    }                                                                   /* 95 */

    return 0;                                                           /* 97 */
}                                                                       /* 98 */

/* Resolves `label` to its slot in map `map_id`, preferring an existing entry
 * and falling back to the first free one.  The scan runs to completion even
 * after a free slot is spotted, because a later slot may still be the match.
 *
 * A freshly claimed slot has its count and state cleared but NOT its labelID;
 * the caller decides whether it is actually taking the slot.  That is why
 * MapLBuffDeleteFile() has to re-test labelID after a successful lookup -- a
 * hit on a free slot is a miss as far as releasing goes. */
static MAP_LBUFF_ST *MapLBuffGetSpace(int map_id, int label)
{
    MAP_LBUFF_ST *wp = (MAP_LBUFF_ST *)0;                               /* 107 */
    int           j;

    for (j = 0; j < 16; j++)                                            /* 109 */
    {
        if (MapLBuffList[map_id][j].labelID == label)                   /* 112 */
        {
            return &MapLBuffList[map_id][j];
        }

        if ((wp == (MAP_LBUFF_ST *)0) &&
            (MapLBuffList[map_id][j].labelID == -1))                    /* 114 */
        {
            wp = &MapLBuffList[map_id][j];
        }
    }                                                                   /* 115 */

    if (wp != (MAP_LBUFF_ST *)0)                                        /* 118 */
    {
        wp->cnt  = 0;                                                   /* 119 */
        wp->stat = 0;                                                   /* 120 */
    }

    return wp;                                                          /* 123 */
}

int MapLBuffDeleteFile(int map_id, int label)
{
    MAP_LBUFF_ST *mp = MapLBuffGetSpace(map_id, label);                 /* 129 */

    if (mp != (MAP_LBUFF_ST *)0)                                        /* 130 */
    {
        /* MapLBuffGetSpace() hands back a free slot when the label is not
         * listed, so a non-NULL return is not yet a hit. */
        if (mp->labelID != -1)                                          /* 131 */
        {
            mp->cnt--;                                                  /* 132 */

            if (mp->cnt < 1)                                            /* 135 */
            {
                mp->labelID = -1;
            }

            return 0;                                                   /* 136 */
        }
    }

    printf("ERR!_NO_DEL_SPACE  MapLBuff.c/J.serizawa\n");               /* 139 */

    return -1;                                                          /* 140 */
}                                                                       /* 141 */

int MapLBuffSetLoadFile(int map_id, int label)
{
    MAP_LBUFF_ST *mp = MapLBuffGetSpace(map_id, label);                 /* 149 */

    if (mp == (MAP_LBUFF_ST *)0)                                        /* 150 */
    {
        printf("ERR!_NO_FREE_SPACE  MapLBuff.c/J.serizawa\n");          /* 151 */
        return -1;                                                      /* 152 */
    }

    /* Unconditional: on an existing entry this rewrites the label with itself,
     * on a fresh slot it claims it.  Either way the count goes up by one. */
    mp->labelID = label;                                                /* 154 */
    mp->cnt++;                                                          /* 155 */

    return 0;                                                           /* 156 */
}                                                                       /* 157 */

void MapLBuffInit(void)
{
    int i;
    int j;

    for (i = 0; i < 66; i++)                                            /* 168 */
    {
        for (j = 0; j < 16; j++)                                        /* 169 */
        {
            MapLBuffList[i][j].labelID = -1;                            /* 170 */
            MapLBuffList[i][j].cnt     = 0;                             /* 171 */
            MapLBuffList[i][j].stat    = 0;                             /* 172 */
        }                                                               /* 173 */
    }                                                                   /* 174 */
}
