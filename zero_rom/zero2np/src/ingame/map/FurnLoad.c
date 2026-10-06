// FILE: /home/zero_rom/zero2np/src/ingame/map/FurnLoad.c
//
// Furniture loader: works out which furniture/door models a room needs and
// pulls them off the disc into one contiguous run of the map heap.
//
// The room's registration data names a model per placed object, but the same
// model is placed many times and the same file backs several named variants
// ("f012", "f012_p", "f012_ev" all resolve to f012.sgd).  So the pass runs in
// two halves: walk every door / object / put-item registration pushing
// normalised model names into CBuff, which discards the duplicates, then load
// the distinct set that survives.
//
// A model name is <key><3 digits>, where the key selects a base file number
// from FurnTbl and the digits are the variant within that block.
//
// Reconstructed from the Feb 6 2004 prototype (SLES_523.84), FurnLoad.o
// 0x00102910..0x00102daf.

#include "FurnLoad.h"
#include "../../common/utility2.h"            /* PRINT_ERROR */

#include "CBuff.h"
#include "FurnCtl.h"
#include "RegDat.h"

#include "../../system/os/eecdvd.h"     /* LoadReqGetAddr */

#include <stdio.h>
#include <string.h>

/* Model-name prefix -> base file number and attribute.  The f-keys are spaced
 * 100 apart, one block of variants each; the d-keys are packed tighter because
 * there are far fewer door models per key. */
static FURN_LOAD_TBL FurnTbl[FURN_TBL_NUM] =                            /* data 2c3d10 */
{
    { "f0",  887, 0 },
    { "f1",  987, 4 },
    { "f2", 1087, 0 },
    { "f3", 1187, 0 },
    { "f4", 1287, 0 },
    { "f5", 1387, 5 },
    { "f6", 1487, 4 },
    { "f7", 1587, 0 },
    { "f8", 1687, 2 },
    { "f9", 1787, 1 },
    { "d0", 1887, 3 },
    { "d1", 1898, 3 },
    { "d2", 1929, 3 },
    { "d3", 1962, 3 },
    { "d4", 2003, 3 },
    { "d8", 2004, 3 }
};

/* Prefix match, not equality -- the table holds the two-character key and the
 * name carries the variant digits after it.  strlen() per entry rather than a
 * constant, because the keys are NUL-terminated inside an 8-byte field. */
static FURN_LOAD_TBL *FurnLoadGetKeyPtr(const char *name)               /* 63 */
{
    for (int i = 0; i < FURN_TBL_NUM; i++)                                  /* 67 */
    {
        if (strncmp(name, FurnTbl[i].key, strlen(FurnTbl[i].key)) == 0) /* 68 */
        {
            return &FurnTbl[i];                                         /* 70 */
        }
    }

    PRINT_ERROR("NO_FURN_KEY_NAME[%s]\n", name);                /* 73 */

    return (FURN_LOAD_TBL *)0;                                          /* 74 */
}

static int FurnLoadGetKeyLabel(const char *name)                        /* 79 */
{
    FURN_LOAD_TBL *fup = FurnLoadGetKeyPtr(name);

    return (fup != (FURN_LOAD_TBL *)0) ? fup->label : -1;               /* 80 */
}

int FurnLoadGetAttr(char *name)                                         /* 86 */
{
    FURN_LOAD_TBL *fup = FurnLoadGetKeyPtr(name);

    return (fup != (FURN_LOAD_TBL *)0) ? fup->attr : -1;                /* 87 */
}

/* Loads one model at `addr` and returns the new end of the run, or `addr`
 * unchanged when the name does not resolve to a file.  The caller uses that
 * "did the cursor move" test rather than a return code. */
char *FurnLoadOne(const char *furn_name, char *addr, int *file_id)      /* 98 */
{
    int top = FurnLoadGetKeyLabel(furn_name);                               /* 100 */
    if (top >= 0)
    {
        int id = FurnCtlGetID((char*)furn_name);                                   /* 102 */
        if (id >= 0)
        {
            addr = (char *)LoadReqGetAddr(top + (id % 100),             /* 105 */
                                          (uintptr_t)addr, file_id);
        }
    }

    return addr;
}

/* The three registration walkers.  Each reaches the model name at a different
 * offset because the three record types put it in a different place; the rest
 * is identical.  FurnCtlGetType() rejects the "eff_" placeholders, which name
 * an effect rather than a model and have no file behind them. */
static void FurnLoadRegistDoor(void *op)                                /* 110 */
{
    char name[36];

    if (FurnCtlGetType(((MDAT_DOOR *)op)->ModelName) != 0)              /* 114 */
    {
        FurnCtlGetMdoelName(name, ((MDAT_DOOR *)op)->ModelName);        /* 115 */
        CBuffSetStr(name);                                              /* 116 */
    }
}

static void FurnLoadRegistObj(void *op)                                 /* 121 */
{
    char name[36];

    if (FurnCtlGetType(((MDAT_OBJ *)op)->ModelName) != 0)               /* 126 */
    {
        FurnCtlGetMdoelName(name, ((MDAT_OBJ *)op)->ModelName);         /* 127 */
        CBuffSetStr(name);                                              /* 128 */
    }
}

static void FurnLoadRegistPut(void *op)                                 /* 133 */
{
    char name[36];

    if (FurnCtlGetType(((MDAT_PUT *)op)->ModelName) != 0)               /* 138 */
    {
        FurnCtlGetMdoelName(name, ((MDAT_PUT *)op)->ModelName);         /* 139 */
        CBuffSetStr(name);                                              /* 140 */
    }
}

/* Runs `func` over every registration section of `type` in the buffer.  The
 * cursor is RegDat's own (RegDatGetStPtrStart / RegDatGetNextStPtr), so this
 * is not reentrant -- fine here, the three walks below run in sequence. */
static void FurnLoadCallFunc(int reg_id, int type, FURN_LOAD_REG_FUNC func)
{                                                                       /* 149 */
    MB_OUT_SECTION *stp;

    RegDatGetStPtrStart(reg_id, type);                                  /* 151 */

    while ((stp = RegDatGetNextStPtr(reg_id)) != (MB_OUT_SECTION *)0)
    {
        func(stp);                                                      /* 153 */
    }
}

/* `addr` is a bump cursor into the map heap: each model is loaded at the
 * current top and the cursor advances past it, so the loaded set ends up
 * contiguous and the caller gets back the new watermark. */
char *FurnLoadRegID(int buff_id, int reg_id, char *addr)                /* 159 */
{
    /* 36 is the ModelName field width; 512 is the per-room cap on distinct
     * models, which CBuffSetStr() does not itself enforce. */
    CBuffInit(36, 512);                                                 /* 163 */

    FurnLoadCallFunc(reg_id, 7,  FurnLoadRegistDoor);                   /* 165 */
    FurnLoadCallFunc(reg_id, 3,  FurnLoadRegistObj);                    /* 166 */
    FurnLoadCallFunc(reg_id, 11, FurnLoadRegistPut);                    /* 167 */

    for (int i = 0; i < CBuffGetRegistNum(); i++)                           /* 169 */
    {
        char *work = addr;                                                    /* 173 */
        char *name = CBuffGetStr(i);                                          /* 175 */
        addr = FurnLoadOne(name, work, (int *)nullptr);                       /* 177 */

        if (addr != work)                                               /* 180 */
        {
            FurnCtlRegist(buff_id, name, work,                          /* 182 */
                          FurnLoadGetAttr(name), (u_int)(addr - work));
        }
    }

    CBuffTerm();                                                        /* 186 */

    return addr;                                                        /* 187 */
}
