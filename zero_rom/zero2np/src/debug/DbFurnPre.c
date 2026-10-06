/* ==========================================================================
 *  debug/DbFurnPre.c
 *
 *  Furniture pre-light debug editor.  UP/DOWN walk the room's registered
 *  model names, LEFT/RIGHT walk that model's placements, and R2 re-bakes the
 *  selected placement's vertex lighting against the room's live light set.
 *  Three lines of text along the top left report what is selected.
 *
 *  Reconstructed from the Feb 6 2004 prototype (SLES_523.84).  All 5 ZERO2.MAP
 *  .text exports plus the two file-local helpers are here, and .text is
 *  accounted for byte-for-byte: 0x1013d8..0x101a34 = 0x644 of code plus six
 *  4-byte alignment fills, once the fixed_array.h boilerplate at the head of
 *  the object (_fixed_array_assert and the three verifyrange instantiations)
 *  is set aside.  The object has no .data and no .bss at all; its .rodata
 *  (0x75) and .sdata (0x5e) decode with no slack, and are nothing but the six
 *  statics below, the compiler's own fixed_array assert literal and type
 *  names, ten _$tmp_N temporary slots, and the three format strings.
 *
 *  All six statics start at -1, not 0.  DbFurnPreProc() opens with a
 *  DbFurnPreBuffID >= 0 test, so a zeroed DbFurnPreBuffID would let the editor
 *  drive room buffer 0 before MapDrawInitFurn() had ever pointed it anywhere.
 *
 *  The trailing line annotations are measured from the $LM labels in
 *  symbols.txt, not guessed.  Two statements carry a range rather than a
 *  single line -- DbFurnPreGetDat's two `->Pos` returns, lines 111-113 and
 *  124-126.  GCC cross-jumped them into the one jal at 0x1016d0 (both records
 *  keep Pos at 0x30, which is what made the tails identical), so the door
 *  arm's own call and add no longer exist as instructions and only the
 *  argument setup carries its lines.
 * ======================================================================== */

#include "DbFurnPre.h"

#include "../common/variable.h"                      /* pad[]              */
#include "../ingame/map/FurnCtl.h"
#include "../ingame/map/MapDraw.h"                   /* MapDrawGetLightPtr */
#include "../ingame/map/MapLoad.h"                   /* MapLoadGetHeadPtr  */
#include "../ingame/map/RegDat.h"                    /* MDAT_* records     */
#include "../ingame/plyr/player.h"                   /* GetPlyrAreaNo      */
#include "../graphics/graph2d/message.h"             /* SetString2         */
#include "../graphics/graph3d/gra3d.h"
#include "../graphics/graph3d/gra3dConst.h"          /* g_v0000            */
#include "../graphics/graph3d/gra3dSGD.h"            /* _gra3dDrawSGD      */
#include "../graphics/graph3d/sgd_types.h"

#include <string.h>

/* ---- editor state -------------------------------------------------------
 * Every one of these is -1 in the ROM's .sdata (0x3eeb60..0x3eeb77). */

/* Which of the room's distinct model names is selected. */
static int    DbFurnPreNameID  = -1;                 /* sdata 3eeb60 */
/* How many distinct names the room has, from DbFurnPreGetObjNum(buff_id). */
static int    DbFurnPreMdoelMax = -1;                /* sdata 3eeb64 */
/* Room buffer the editor is pointed at; < 0 disables the whole module. */
static int    DbFurnPreBuffID  = -1;                 /* sdata 3eeb68 */
/* Which placement of the selected model is selected. */
static int    DbFurnPreObjID   = -1;                 /* sdata 3eeb6c */
/* How many placements the selected model has. */
static int    DbFurnPreObjMax  = -1;                 /* sdata 3eeb70 */
/* The selected placement's Pos[3], or NULL before one has been resolved.
 * Doubles as the "is there anything to report" test for the third readout. */
static float *DbFurnPrePos     = (float *)-1;        /* sdata 3eeb74 */


/* Number of distinct model names registered to `buff_id`.  FurnCtl's cursor
 * is module-wide, so this cannot be nested inside another FurnCtl walk. */
static int DbFurnPreGetObjNum(int buff_id)                              /* 40 */
{                                                                       /* 41 */
    int cnt = 0;

    FurnCtlFindInit(buff_id);                                           /* 45 */

    while (FurnCtlGetNextName() != NULL) cnt++;                         /* 47 */
    return cnt;                                                         /* 48 */
}


/* The `id`'th registered model name, counting from the head of the walk.  An
 * `id` past the end yields the last name the cursor produced -- the ROM does
 * not re-test the result, and DbFurnPreProc() keeps id in range instead. */
static char *DbFurnPreGetObjName(int buff_id, int id)                   /* 52 */
{                                                                       /* 53 */
    int   i;
    char *name;

    FurnCtlFindInit(buff_id);                                           /* 58 */
    name = FurnCtlGetNextName();                                        /* 59 */

    for (i = 0; i < id; i++)                                            /* 61 */
    {
        name = FurnCtlGetNextName();                                    /* 62 */
    }                                                                   /* 63 */
    return name;                                                        /* 64 */
}


uintptr_t DbFurnPreGetNumOneType(int reg_id, int type,                  /* 68 */
                                 char *name, int flg)
{                                                                       /* 69 */
    int             cnt = 0;                                            /* 70 */
    void           *vp;
    char           *d_name;

    RegDatGetStPtrStart(reg_id, type);                                  /* 74 */

    while ((vp = RegDatGetNextStPtr(reg_id)) != NULL)                   /* 75 */
    {
        d_name = NULL;                                                  /* 76 */

        switch (type)                                                   /* 78 */
        {
        case 7:                     /* door     */
        case 11:                    /* put-item */
            d_name = ((MDAT_DOOR *)vp)->ModelName;                      /* 86 */
            break;
        case 3:                     /* object   */
            d_name = ((MDAT_OBJ *)vp)->ModelName;                       /* 89 */
            break;                                                      /* 90 */
        }

        /* No default arm, so an unhandled `type` reaches strncmp() with a NULL
         * second argument.  Unreachable -- the only three call sites pass 7,
         * 11 and 3 -- and left as the ROM has it. */
        if (strncmp(name, d_name, 4) == 0) cnt++;                       /* 93 */

        if (flg >= 0)                                                   /* 96 */
        {
            if (cnt == flg + 1) return (uintptr_t)vp;                   /* 97 */
        }
    }
    return (uintptr_t)cnt;                                              /* 100 */
}                                                                       /* 101 */


float *DbFurnPreGetDat(int buff_id, char *name, int id)                 /* 104 */
{                                                                       /* 105 */
    int reg_id = MapLoadGetHeadPtr(buff_id)->reg_id[0];                 /* 106 */

    if (name[0] == 'd')                                                 /* 110 */
    {
        return ((MDAT_DOOR *)DbFurnPreGetNumOneType(reg_id, 7,
                                                    name, id))->Pos;    /* 111-113 */
    }
    else
    {
        int put_num = (int)DbFurnPreGetNumOneType(reg_id, 11, name, -1);/* 115 */

        if (id < put_num)                                               /* 118 */
        {
            return ((MDAT_PUT *)DbFurnPreGetNumOneType(reg_id, 11,      /* 119 */
                                                       name, id))->Pos; /* 121 */
        }
        id -= put_num;                                                  /* 124 */

        return ((MDAT_OBJ *)DbFurnPreGetNumOneType(reg_id, 3,
                                                   name, id))->Pos;     /* 124-126 */
    }
}                                                                       /* 131 */


int DbFurnPreGetObjNum(int buff_id, char *name)                         /* 135 */
{                                                                       /* 136 */
    int cnt;
    int reg_id = MapLoadGetHeadPtr(buff_id)->reg_id[0];                 /* 138 */

    if (name[0] == 'd')                                                 /* 142 */
    {
        cnt = (int)DbFurnPreGetNumOneType(reg_id, 7, name, -1);         /* 144 */
    }
    else
    {
        cnt  = (int)DbFurnPreGetNumOneType(reg_id, 11, name, -1);       /* 148 */
        cnt += (int)DbFurnPreGetNumOneType(reg_id, 3, name, -1);        /* 150 */
    }
    return cnt;                                                         /* 152 */
}


void DbFurnPreSetBuffID(int buff_id)                                    /* 156 */
{                                                                       /* 157 */
    DbFurnPrePos = NULL;                                                /* 158 */
    DbFurnPreBuffID = buff_id & 1;                                      /* 159 */

    DbFurnPreMdoelMax = DbFurnPreGetObjNum(DbFurnPreBuffID);            /* 161 */
}


void DbFurnPreProc(void)                                                /* 168 */
{                                                                       /* 169 */
    int   flg = 0;                                                      /* 170 */
    char *name;

    if (DbFurnPreBuffID >= 0)                                           /* 174 */
    {
        /* UP/DOWN change the model, LEFT/RIGHT the placement within it.  The
         * pad word is byte-swapped from the DualShock layout (pad.c builds it
         * as ~((r_data[2] << 8) | r_data[3])), so 0x1000 is UP and 0x8000
         * LEFT -- not TRIANGLE and SQUARE. */
        if (pad[0].one & 0x1000)                                        /* 177 */
        {
            DbFurnPreNameID--; flg = 1;                                 /* 179 */
        }
        if (pad[0].one & 0x4000)
        {                                                               /* 181 */
            DbFurnPreNameID++;                                          /* 182 */
            flg = 1;                                                    /* 183 */
        }

        if (pad[0].one & 0x2000) DbFurnPreObjID++;                      /* 187 */
        if (pad[0].one & 0x8000) DbFurnPreObjID--;                      /* 188 */

        /* Both selections wrap rather than clamp. */
        if (DbFurnPreObjID >= DbFurnPreObjMax) DbFurnPreObjID = 0;      /* 191 */
        if (DbFurnPreObjID < 0) DbFurnPreObjID = DbFurnPreObjMax - 1;   /* 192 */

        if (DbFurnPreNameID >= DbFurnPreMdoelMax) DbFurnPreNameID = 0;  /* 194 */
        if (DbFurnPreNameID < 0) DbFurnPreNameID = DbFurnPreMdoelMax - 1; /* 195 */

        name = DbFurnPreGetObjName(DbFurnPreBuffID, DbFurnPreNameID);   /* 198 */

        /* Only a model change re-counts the placements; walking within one
         * model must not disturb DbFurnPreObjID. */
        if (flg)                                                        /* 201 */
        {
            DbFurnPrePos = NULL;                                        /* 202 */
            DbFurnPreObjID = 0;                                         /* 203 */

            DbFurnPreObjMax = DbFurnPreGetObjNum(DbFurnPreBuffID, name);/* 205 */
        }

        /* R2 bakes.  DbFurnPreBuffID is re-tested here even though the whole
         * body already sits inside that test; kept as found. */
        if ((pad[0].one & 0x2) && DbFurnPreBuffID >= 0 &&
            DbFurnPreObjID >= 0 && name != NULL)                        /* 209 */
        {
            SGDFILEHEADER *mdp;
            float          pos[4];

            mdp = (SGDFILEHEADER *)FurnCtlGetModelAddr(DbFurnPreBuffID,
                                                       name);           /* 215 */
            /* The room's own light set, so the bake matches what the room
             * draws with rather than whatever was last installed. */
            gra3dSetLightData(MapDrawGetLightPtr(GetPlyrAreaNo()),      /* 216 */
                              (float *)0);                              /* 219 */

            DbFurnPrePos = DbFurnPreGetDat(DbFurnPreBuffID, name,
                                           DbFurnPreObjID);             /* 222 */
            pos[0] = DbFurnPrePos[0];                                   /* 223 */
            pos[1] = DbFurnPrePos[1];                                   /* 224 */
            pos[2] = DbFurnPrePos[2];                                   /* 225 */
            pos[3] = 1.0f;                                              /* 226 */

            /* pnum -1 clears every block: SgClearPreRenderPrim() resets the
             * mesh packets' per-vertex colours, so the bake below starts from
             * an unlit model rather than on top of the previous pass. */
            _gra3dDrawSGD(mdp, SRT_CLEARPRELIGHTING, NULL, -1);         /* 229 */

            gra3dExecPrelight(mdp, pos, g_v0000);                       /* 231 */
        }

        SetString2(0, 20.0f, 20.0f, 1, 0x80, 0x80, 0x80,
                   "%s %d", name, DbFurnPreNameID);                     /* 237 */

        SetString2(0, 20.0f, 40.0f, 1, 0x80, 0x80, 0x80,
                   "now%d max%d", DbFurnPreObjID, DbFurnPreObjMax);     /* 239 */
        if (DbFurnPrePos != NULL)                                       /* 240 */
        {
            SetString2(0, 20.0f, 60.0f, 1, 0x80, 0x80, 0x80,
                       "pos %f %f %f",
                       DbFurnPrePos[0], DbFurnPrePos[1], DbFurnPrePos[2]); /* 242 */
        }
    }
}                                                                       /* 245 */
