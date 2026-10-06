// FILE: /home/zero_rom/zero2np/src/ingame/item/prg/file.c
//
// Collected files/notes inventory.  A (file_type, file_id) pair addresses one
// state byte in plyr_file and one FILE_DAT row in the file_dat.o tables; two
// parallel five-entry pointer tables turn file_type into the right array so
// nothing here needs a switch.
//
// Everything funnels through GetPlyrFileData() / GetFileData(), and every
// public entry point re-checks both indices before calling them -- so a
// single FileGet() range-checks file_type four times over and calls
// GetFileTypeMaxNum() three times.  That redundancy is the ROM's.
//
// The guards are signed comparisons, so a negative index slips past them and
// indexes backwards off the table.  Left as-is; item.o casts to unsigned for
// the same job, so this was a per-file habit rather than a house rule.
//
// Reconstructed from the Feb 6 2004 prototype (SLES_523.84).

#include "file.h"

#include "../../../common/utility2.h"   /* PRINT_ASSERT / PRINT_WARNING */

#include <stdio.h>

static void      PlyrFileInit(int file_type, int file_id);
static FILE_DAT *GetFileData(int file_type, int file_id);
static char     *GetPlyrFileData(int file_type, int file_id);

/* Statically initialised, so this one lands in .data. */
static FILE_DAT *file_dat_tbl[FILE_TYPE_MAX] =                      /* data 312230 */
{
    file_pocketbook, file_scrap, file_oldbook, file_photograph, file_map,
};

static PLYR_FILE plyr_file;                                         /* bss 47bfe0 */

/* Row 0 of each PLYR_FILE member, so file_type indexes straight into the
 * player's flags.  Each subscript runs fixed_array::operator[], which makes
 * this a *dynamic* initialisation -- that is why the ROM keeps it in .bss
 * with a static constructor instead of in .data beside file_dat_tbl. */
static char *plyr_file_tbl[FILE_TYPE_MAX] =                         /* bss 47c080 */
{                                                                   /* 29 */
    &plyr_file.pocketbook[0], &plyr_file.scrap[0], &plyr_file.oldbook[0],
    &plyr_file.photograph[0], &plyr_file.map[0],                    /* 32 */
};

/* --------------------------------------------------------------------------
 *  Init
 * ------------------------------------------------------------------------ */
void AllPlyrFileInit(void)
{                                                                       /* 71 */
    int i;
    int j;

    for (i = 0; i < FILE_TYPE_MAX; i++)                                 /* 76 */
    {
        for (j = 0; j < GetFileTypeMaxNum(i); j++)                      /* 77 */
        {
            PlyrFileInit(i, j);                                         /* 78 */
        }                                                               /* 79 */
    }                                                                   /* 80 */
}

static void PlyrFileInit(int file_type, int file_id)
{                                                                       /* 95 */
    if (FILE_TYPE_MAX <= file_type)                                     /* 99 */
    {
        PRINT_ASSERT("Error! PlyrFileInit file type %d", file_type);    /* 100 */
    }

    if (GetFileTypeMaxNum(file_type) <= file_id)                        /* 102 */
    {
        PRINT_ASSERT("Error! PlyrFileInit file id %d", file_id);        /* 103 */
    }

    SetPlyrFileState(file_type, file_id, FILE_STATE_NONE);              /* 108 */
}

/* --------------------------------------------------------------------------
 *  Get / Lost / Read
 * ------------------------------------------------------------------------ */
void FileGet(int file_type, int file_id)
{                                                                       /* 122 */
    if (FILE_TYPE_MAX <= file_type)                                     /* 126 */
    {
        PRINT_ASSERT("Error! FileGet file type %d", file_type);         /* 127 */
    }

    if (GetFileTypeMaxNum(file_type) <= file_id)                        /* 129 */
    {
        PRINT_ASSERT("Error! FileGet file id %d", file_id);             /* 130 */
    }

    if (GetPlyrFileState(file_type, file_id) == FILE_STATE_NONE)        /* 136 */
    {
        SetPlyrFileState(file_type, file_id, FILE_STATE_HAVE);          /* 138 */
    }
    else
    {
        PRINT_WARNING("Warning!! File Type [%d] File ID [%d] It has already obtained it!!",
                      file_type, file_id);                              /* 142 */
    }
}

void FileLost(int file_type, int file_id)
{                                                                       /* 157 */
    if (FILE_TYPE_MAX <= file_type)                                     /* 161 */
    {
        PRINT_ASSERT("Error! FileLost file type %d", file_type);        /* 162 */
    }

    if (GetFileTypeMaxNum(file_type) <= file_id)                        /* 164 */
    {
        PRINT_ASSERT("Error! FileLost file id %d", file_id);            /* 165 */
    }

    if (GetPlyrFileState(file_type, file_id) != FILE_STATE_NONE)        /* 171 */
    {
        SetPlyrFileState(file_type, file_id, FILE_STATE_NONE);          /* 173 */
    }
    else
    {
        PRINT_WARNING("Warning! File Type[%d] File ID[%d] FileLost!!\n",
                      file_type, file_id);                              /* 177 */
    }
}

/* `res` is set once and never reassigned, so every path returns 1 -- including
 * the "you do not have this file" warning and the impossible-state assert.
 * GCC folded the store into the switch's case-1 constant, which is why line
 * 208 lands on a `li v0,1` that does double duty. */
int FileRead(int file_type, int file_id)
{                                                                       /* 194 */
    int res;

    if (FILE_TYPE_MAX <= file_type)                                     /* 200 */
    {
        PRINT_ASSERT("Error! FileRead file type %d", file_type);        /* 201 */
    }

    if (GetFileTypeMaxNum(file_type) <= file_id)                        /* 203 */
    {
        PRINT_ASSERT("Error! FileRead file id %d", file_id);            /* 204 */
    }

    res = 1;                                                            /* 208 */

    switch (GetPlyrFileState(file_type, file_id))                       /* 210 */
    {
    case FILE_STATE_NONE:
        PRINT_WARNING("Warning! FileRead FileType [%d], FileID [%d]\n",
                      file_type, file_id);                              /* 212 */
        break;                                                          /* 213 */

    case FILE_STATE_HAVE:
        SetPlyrFileState(file_type, file_id, FILE_STATE_READ);          /* 215 */
        break;                                                          /* 216 */

    case FILE_STATE_READ:
        break;

    default:
        PRINT_ASSERT("Error! FileRead");                                /* 221 */
        break;
    }

    return res;                                                         /* 225 */
}

/* --------------------------------------------------------------------------
 *  Table lookups
 * ------------------------------------------------------------------------ */
static FILE_DAT *GetFileData(int file_type, int file_id)
{                                                                       /* 238 */
    if (FILE_TYPE_MAX <= file_type)                                     /* 242 */
    {
        PRINT_ASSERT("Error! GetFileData file type %d", file_type);     /* 243 */
    }

    if (GetFileTypeMaxNum(file_type) <= file_id)                        /* 245 */
    {
        PRINT_ASSERT("Error! GetFileData file id %d", file_id);         /* 246 */
    }

    return &file_dat_tbl[file_type][file_id];                           /* 252 */
}

int GetFileTexId(int file_type, int file_id)
{                                                                       /* 261 */
    if (FILE_TYPE_MAX <= file_type)                                     /* 267 */
    {
        PRINT_ASSERT("Error! GetFileTexId file type %d", file_type);    /* 268 */
    }

    if (GetFileTypeMaxNum(file_type) <= file_id)                        /* 270 */
    {
        PRINT_ASSERT("Error! GetFileTexId file id %d", file_id);        /* 271 */
    }

    return GetFileData(file_type, file_id)->tex_id;                     /* 277 */
}                                                                       /* 280 */

static char *GetPlyrFileData(int file_type, int file_id)
{                                                                       /* 289 */
    if (FILE_TYPE_MAX <= file_type)                                     /* 293 */
    {
        PRINT_ASSERT("Error! GetPlyrFileData file type %d", file_type); /* 294 */
    }

    if (GetFileTypeMaxNum(file_type) <= file_id)                        /* 296 */
    {
        PRINT_ASSERT("Error! GetPlyrFileData file id %d", file_id);     /* 297 */
    }

    return plyr_file_tbl[file_type] + file_id;                          /* 303 */
}

/* --------------------------------------------------------------------------
 *  State accessors
 * ------------------------------------------------------------------------ */
char GetPlyrFileState(int file_type, int file_id)
{                                                                       /* 312 */
    if (FILE_TYPE_MAX <= file_type)                                     /* 317 */
    {
        PRINT_ASSERT("Error! GetPlyrFileState file type %d", file_type);/* 318 */
    }

    if (GetFileTypeMaxNum(file_type) <= file_id)                        /* 320 */
    {
        PRINT_ASSERT("Error! GetPlyrFileState file id %d", file_id);    /* 321 */
    }

    return *GetPlyrFileData(file_type, file_id);                        /* 327 */
}                                                                       /* 330 */

int GetPlyrFileTotalNum(int file_type)
{                                                                       /* 338 */
    int i;
    int total;

    total = 0;                                                          /* 343 */

    if (FILE_TYPE_MAX <= file_type)                                     /* 347 */
    {
        PRINT_ASSERT("Error! GetPlyrFileTotalNum file type %d",
                     file_type);                                        /* 348 */
    }

    for (i = 0; i < GetFileTypeMaxNum(file_type); i++)                  /* 353 */
    {
        if (GetPlyrFileState(file_type, i) != FILE_STATE_NONE)          /* 355 */
        {
            total++;
        }
    }                                                                   /* 358 */

    return total;                                                       /* 361 */
}

int GetFileTypeMaxNum(int type)
{                                                                       /* 369 */
    /* rdata 3ae150 -- const, which is what puts it in .rodata rather than
     * .data.  These are the row counts of the five file_dat.o tables. */
    static const int file_id_max[FILE_TYPE_MAX] =
    {
        FILE_POCKETBOOK_MAX, FILE_SCRAP_MAX, FILE_OLDBOOK_MAX,
        FILE_PHOTOGRAPH_MAX, FILE_MAP_MAX,
    };

    if (FILE_TYPE_MAX <= type)                                          /* 380 */
    {
        PRINT_ASSERT("Error! GetFileTypeMaxNum file type %d", type);    /* 381 */
    }

    return file_id_max[type];                                           /* 387 */
}

/* Signed compare against FILE_STATE_READ, so a negative state is accepted
 * here -- soul_list.o's equivalent guard casts to unsigned instead. */
void SetPlyrFileState(int file_type, int file_id, char state)
{                                                                       /* 400 */
    if (FILE_TYPE_MAX <= file_type)                                     /* 405 */
    {
        PRINT_ASSERT("Error! SetPlyrFileState file type %d", file_type);/* 406 */
    }

    if (GetFileTypeMaxNum(file_type) <= file_id)                        /* 408 */
    {
        PRINT_ASSERT("Error! SetPlyrFileState file id %d", file_id);    /* 409 */
    }

    if (FILE_STATE_READ < state)                                        /* 411 */
    {
        PRINT_ASSERT("Error! SetPlyrFileState state %d", state);        /* 412 */
    }

    *GetPlyrFileData(file_type, file_id) = state;                       /* 418 */
}                                                                       /* 420 */

/* --------------------------------------------------------------------------
 *  Save / debug
 * ------------------------------------------------------------------------ */
void SetSave_PlyrFile(MC_SAVE_DATA *data)
{                                                                       /* 434 */
    data->addr = (u_char *)&plyr_file;                                  /* 437 */
    data->size = sizeof(PLYR_FILE);                                     /* 438 */
}

void DebugAllFileGet(void)
{                                                                       /* 449 */
    int i;
    int j;

    for (i = 0; i < FILE_TYPE_MAX; i++)                                 /* 455 */
    {
        for (j = 0; j < GetFileTypeMaxNum(i); j++)                      /* 456 */
        {
            if (GetPlyrFileState(i, j) == FILE_STATE_NONE)              /* 457 */
            {
                FileGet(i, j);                                          /* 458 */
            }
        }                                                               /* 460 */
    }                                                                   /* 461 */
}
