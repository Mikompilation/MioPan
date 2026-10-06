// FILE: /home/zero_rom/zero2np/src/ingame/event/prg/ev_se.c
//
// Per-room event sound-bank management.  Each of the 66 rooms owns up to five
// registered sound files; on a room change the banks for the outgoing room are
// released and the incoming room's are requested.
//
// sb_ids is double-buffered: a room change loads into the current half and
// then flips ev_se_toggle, so ev_seIsReady() polls the half just loaded
// (ev_se_toggle ^ 1) while the other half is free to be torn down.
//
// Reconstructed from the Feb 6 2004 prototype (SLES_523.84).

#include "ev_se.h"

#include "../../../common/utility2.h"       // PRINT_ASSERT / PRINT_WARNING
#include "../../../system/eeiop/sndbank.h"  // SndBankNew / Release / IsReady / GetFileNo

#define EV_SE_ROOM_MAX 66
#define EV_SE_FILE_MAX 5

static int ev_se_reg_files[EV_SE_ROOM_MAX][EV_SE_FILE_MAX]; /* bss 47b3d0 */
static int sb_ids[2][EV_SE_FILE_MAX];                       /* bss 47b8f8 */
static int ev_se_toggle;                                    /* sbss 3f4c54 */

void ev_seInit(void)
{
    int i;
    int j;

    for (i = 0; i < EV_SE_ROOM_MAX; i++) {
        for (j = 0; j < EV_SE_FILE_MAX; j++) {
            ev_se_reg_files[i][j] = -1;
        }
    }

    for (i = 0; i < 2; i++) {
        for (j = 0; j < EV_SE_FILE_MAX; j++) {
            sb_ids[i][j] = -1;
        }
    }

    ev_se_toggle = 0;
}

void ev_seSetSave(MC_SAVE_DATA *save)
{
    save->size = sizeof(ev_se_reg_files);
    save->addr = (u_char *)ev_se_reg_files;
}

void ev_seRelease(void)
{
    int i;
    int j;

    for (i = 0; i < 2; i++) {
        for (j = 0; j < EV_SE_FILE_MAX; j++) {
            if (sb_ids[i][j] >= 0) {
                SndBankRelease(sb_ids[i][j]);
                sb_ids[i][j] = -1;
            }
        }
    }

    ev_se_toggle = 0;
}

void ev_seRegisterFile(int room_id, int file_no)
{
    int i;

    for (i = 0; i < EV_SE_FILE_MAX; i++) {
        if (ev_se_reg_files[room_id][i] == file_no) {
            PRINT_ASSERT("EVENT_SE REGISTER SAME FILE!! ROOM[%d] file_no = %d",
                         room_id, file_no);
        }

        if (ev_se_reg_files[room_id][i] == -1) {
            ev_se_reg_files[room_id][i] = file_no;
            return;
        }
    }

    PRINT_ASSERT("EVENT_SE REGISTER IS OVER!! ROOM[%d] file_no = %d",
                 room_id, file_no);
}

void ev_seDeleteFile(int room_id, int file_no)
{
    int i;
    int j;

    for (i = 0; i < EV_SE_FILE_MAX; i++) {
        if (ev_se_reg_files[room_id][i] == file_no) {
            /* Close the gap so the used slots stay contiguous -- the register
             * path stops at the first -1. */
            for (j = i; j < EV_SE_FILE_MAX - 1; j++) {
                ev_se_reg_files[room_id][j] = ev_se_reg_files[room_id][j + 1];
            }
            ev_se_reg_files[room_id][EV_SE_FILE_MAX - 1] = -1;
            return;
        }
    }

    PRINT_WARNING("EVENT_SE DELETE NOT FOUND FILE!! ROOM[%d] file_no = %d",
                  room_id, file_no);
}

int ev_seGetBankID(int file_no)
{
    int i;
    int j;
    int bank_file_no;

    for (i = 0; i < EV_SE_FILE_MAX; i++) {
        for (j = 0; j < 2; j++) {
            if ((sb_ids[j][i] >= 0) &&
                (SndBankGetFileNo(sb_ids[j][i], &bank_file_no) == SND_BANK_OK) &&
                (bank_file_no == file_no)) {
                return sb_ids[j][i];
            }
        }
    }

    return -1;
}

void ev_seChangeRoom(int room_id)
{
    int i;
    int file_no;

    for (i = 0; i < EV_SE_FILE_MAX; i++) {
        if (sb_ids[ev_se_toggle][i] >= 0) {
            SndBankRelease(sb_ids[ev_se_toggle][i]);
            sb_ids[ev_se_toggle][i] = -1;
        }
    }

    for (i = 0; i < EV_SE_FILE_MAX; i++) {
        file_no = ev_se_reg_files[room_id][i];
        if (file_no >= 0) {
            /* The bank's header pak is always the file immediately before it. */
            sb_ids[ev_se_toggle][i] = SndBankNew(file_no, file_no - 1, -1);
        }
    }

    ev_se_toggle = ev_se_toggle ^ 1;
}

int ev_seIsReady(void)
{
    int ready;
    int i;

    ready = 1;
    for (i = 0; i < EV_SE_FILE_MAX; i++) {
        if (sb_ids[ev_se_toggle ^ 1][i] >= 0) {
            ready = ready & SndBankIsReady(sb_ids[ev_se_toggle ^ 1][i]);
        }
    }

    return ready;
}
