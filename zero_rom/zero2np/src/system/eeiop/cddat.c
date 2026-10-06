// FILE: /home/akira_koide/zero2np/src/system/eeiop/cddat.c
//
// On-disc file table.  cddatInit() hands the IOP the name of the file-table
// image (IMG_BD.BIN) and latches the four parallel arrays that describe every
// file on the disc:
//
//   * p_cd_dat[]       - CD_DAT_TBL: compression / exist flags, start sector,
//                        uncompressed size and on-disc (compressed) size.
//   * p_fname_dat[]    - FNAME_DAT: per-file compression flag, path index and
//                        file-name string.
//   * p_filename_path[]- the directory-path strings the path index selects.
//   * p_ext_lbl[]      - a per-file extension label byte.
//
// The Get* accessors map a file index (enum CD_FILE_DAT) onto one of those
// fields; sizes are rounded up with GetAlignUp() (16-byte for byte sizes,
// 2048-byte for the sector count).  cddatCompressFileNoUse[No]() clear the
// compression flag once every outstanding load has drained, so a file that was
// stored compressed can be re-loaded raw.
//
// Reconstructed from the Feb 6 2004 prototype (SLES_523.84).

#include "cddat.h"                  // this file's public API + CD_DAT_TBL / FNAME_DAT / enum CD_FILE_DAT

#include <stdio.h>                  // printf (out-of-range diagnostic)
#include <string.h>                 // strcpy / strcat

#include "ee_iop.h"                 // iopCommandRegister / ee_iopMain + IOP_COMMAND_ENUM (REQ_SET_CD_DAT)
#include "fileload.h"               // AllFileLoadIsEnd
#include "../../common/utility2.h"  // PRINT_ASSERT + GetAlignUp

// ──────────────────────────────────────────────────────────────────────
// The IOP "set file-table image" command payload: just the image file name.

typedef struct                      /* 0x10 */
{
    char file_name[16];
} SET_CD_DAT;

// ──────────────────────────────────────────────────────────────────────
// Statics.  cddatInit() binds the four parallel tables and the file count; the
// path prefix and the command payload are the only storage this module owns.

static char       title_root_path[200];     // bss  4c07e0 : path prefix ("host0:" / disc root)
static FNAME_DAT *p_fname_dat;               // sbss 3f5060 : per-file name records
static char     **p_filename_path;           // sbss 3f5064 : directory-path strings
static char      *p_ext_lbl;                 // sbss 3f5068 : per-file extension labels
static int        project_file_num;          // sbss 3f506c : total file count
static CD_DAT_TBL *p_cd_dat;                 // sbss 3f5070 : per-file sector/size table
static SET_CD_DAT set_cd_dat;                // bss  4c08a8 : IOP set-file-table command payload

// ──────────────────────────────────────────────────────────────────────
// Bind the file tables and tell the IOP which on-disc image to parse, then
// pump the command queue twice so the request is picked up before we return.

void cddatInit(char *cd_dat_name, char *pc_path, FNAME_DAT *p_file_name,
               char **p_path_name, CD_DAT_TBL *cd_tbl, char *ext_lbl,
               int total_file_num)
{
    strcpy(set_cd_dat.file_name, cd_dat_name);
    iopCommandRegister(REQ_SET_CD_DAT, set_cd_dat.file_name, sizeof(set_cd_dat.file_name));

    strcpy(title_root_path, pc_path);
    p_fname_dat      = p_file_name;
    p_filename_path  = p_path_name;
    p_ext_lbl        = ext_lbl;
    project_file_num = total_file_num;
    p_cd_dat         = cd_tbl;

    ee_iopMain();
    ee_iopMain();
}

// ──────────────────────────────────────────────────────────────────────
// Extension label byte for a file.

char GetFileExtLabel(int file_no)
{
    return p_ext_lbl[file_no];
}

// ──────────────────────────────────────────────────────────────────────
// Total number of files in the table.

int GetFileNum(void)
{
    return project_file_num;
}

// ──────────────────────────────────────────────────────────────────────
// Start sector (LBA) of a file; asserts if the file is not present on disc.

int GetFileStartSector(int file_no)
{
    if (p_cd_dat[file_no].exist_flg == 0)
    {
        PRINT_ASSERT("file_no %d is not On CD", file_no);
    }

    return p_cd_dat[file_no].start_sector;
}

// ──────────────────────────────────────────────────────────────────────
// File size expressed as a 2048-byte sector count (rounded up).

int GetFileSectorSize(int file_no)
{
    unsigned int size = GetFileSize(file_no);
    return (int)(size + 0x7ff) >> 0xb;
}

// ──────────────────────────────────────────────────────────────────────
// Uncompressed byte size, rounded up to a multiple of 16.

unsigned int GetFileSize(int file_no)
{
    return GetAlignUp(p_cd_dat[file_no].size, 4);
}

// ──────────────────────────────────────────────────────────────────────
// On-disc (compressed) byte size, rounded up to a multiple of 16.

unsigned int GetFileCmpSize(int file_no)
{
    return GetAlignUp(p_cd_dat[file_no].cmp_size, 4);
}

// ──────────────────────────────────────────────────────────────────────
// Non-zero if the file is stored compressed.

int cddatIsCmpFile(int file_no)
{
    return p_cd_dat[file_no].cmp_flg;
}

// ──────────────────────────────────────────────────────────────────────
// Once every load has drained, clear the compression flag on every file so
// subsequent loads read the raw data.  Returns 1 when it ran, 0 if busy.

int cddatCompressFileNoUse(void)
{
    if (AllFileLoadIsEnd() == 0)
    {
        return 0;
    }

    FNAME_DAT *p_fname_dat_not_const = p_fname_dat;
    for (int i = 0; i < project_file_num; i++)
    {
        p_fname_dat_not_const->cmp_flg = 0;
        p_fname_dat_not_const++;
    }

    return 1;
}

// ──────────────────────────────────────────────────────────────────────
// As cddatCompressFileNoUse(), but for a single file.

int cddatCompressFileNoUseNo(int file_no)
{
    if (AllFileLoadIsEnd() == 0)
    {
        return 0;
    }

    p_fname_dat[file_no].cmp_flg = 0;
    return 1;
}

// ──────────────────────────────────────────────────────────────────────
// Build a file's full path (root prefix + directory + name) into a private
// static buffer and return it.  Returns NULL if the index is out of range.

char *GetFileName(int file_no)
{
    static char    file_full_name[256];     // bss 4c06e0

    unsigned char path_no = p_fname_dat[file_no].path_no;
    if (file_no < project_file_num)
    {
        strcpy(file_full_name, title_root_path);
        strcat(file_full_name, p_filename_path[path_no]);
        strcat(file_full_name, p_fname_dat[file_no].name);
        return file_full_name;
    }

    printf("***** (PC FILE) FILE NO. OVER %d *****\n", file_no);
    return (char *)nullptr;
}

// ──────────────────────────────────────────────────────────────────────
// As GetFileName(), but assemble the path into a caller-supplied buffer.

void GetFileNameBuffer(int file_no, char *buf)
{
    unsigned char path_no = p_fname_dat[file_no].path_no;
    strcpy(buf, title_root_path);
    strcat(buf, p_filename_path[path_no]);
    strcat(buf, p_fname_dat[file_no].name);
}
