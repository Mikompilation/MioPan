// FILE: /home/zero_rom/zero2np/src/ingame/event/prg/ev_ene.c
//
// Per-room enemy residency.  Each of the 66 rooms lists up to four
// (ene_type, dat_no) pairs; ene_dats holds the set currently loaded.
//
// A room change diffs the outgoing set against the incoming one so enemies
// present in both are left alone: only the ones that dropped out are released
// and only the genuinely new ones are requested.
//
// Reconstructed from the Feb 6 2004 prototype (SLES_523.84).

#include "ev_ene.h"

#include <string.h>                         // memset

#include "../../../common/utility2.h"       // PRINT_ASSERT / PRINT_WARNING
#include "../../enemy/enemy.h"              // EneLoadReq / EneReleaseReq / SearchEneWrkNo

#include <stdio.h>                          // printf

#define EV_ENE_ROOM_MAX 66
#define EV_ENE_DAT_MAX  4

static ENE_LOAD_DATS ev_ene_dats[EV_ENE_ROOM_MAX][EV_ENE_DAT_MAX]; /* bss 478ff0 */
static ENE_DATS      ene_dats[EV_ENE_DAT_MAX];                     /* bss 479830 */

void ev_eneInit(void)
{
    int i;
    int j;

    for (i = 0; i < EV_ENE_ROOM_MAX; i++) {
        for (j = 0; j < EV_ENE_DAT_MAX; j++) {
            ev_ene_dats[i][j].dats.dat_no = -1;
        }
    }

    for (i = 0; i < EV_ENE_DAT_MAX; i++) {
        ene_dats[i].dat_no = -1;
    }
}

void ev_eneSetSave(MC_SAVE_DATA *save)
{
    save->size = sizeof(ev_ene_dats);
    save->addr = (u_char *)ev_ene_dats;
}

void ev_eneRelease(void)
{
    int i;

    for (i = 0; i < EV_ENE_DAT_MAX; i++) {
        if (ene_dats[i].dat_no == -1) {
            break;
        }
        EneReleaseReq(ene_dats[i].ene_type, ene_dats[i].dat_no);
    }
}

void ev_eneRegisterFile(int room_id, int ene_type, int dat_no)
{
    int i;

    for (i = 0; i < EV_ENE_DAT_MAX; i++) {
        if ((ev_ene_dats[room_id][i].dats.dat_no == dat_no) &&
            (ev_ene_dats[room_id][i].dats.ene_type == ene_type)) {
            printf("room_id = %d\n", room_id);
            printf("ene_type = %d\n", ene_type);
            printf("dat_no = %d\n", dat_no);
            PRINT_ASSERT("ev_eneRegisterFile() \n Same ENEDAT[%d], type[%d]!!",
                         dat_no, ene_type);
            return;
        }
    }

    for (i = 0; i < EV_ENE_DAT_MAX; i++) {
        if (ev_ene_dats[room_id][i].dats.dat_no == -1) {
            ev_ene_dats[room_id][i].dats.dat_no = dat_no;
            ev_ene_dats[room_id][i].dats.ene_type = ene_type;
            return;
        }
    }

    PRINT_ASSERT("ENE_LOAD REGISTER IS OVER!! ROOM[%d] ene_dat_no = %d",
                 room_id, dat_no);
}

void ev_eneDeleteFile(int room_id, int ene_type, int dat_no)
{
    int i;
    int j;

    for (i = 0; i < EV_ENE_DAT_MAX; i++) {
        if ((ev_ene_dats[room_id][i].dats.dat_no == dat_no) &&
            (ev_ene_dats[room_id][i].dats.ene_type == ene_type)) {
            /* Keep the used slots contiguous -- the register path stops at
             * the first dat_no == -1. */
            for (j = i; j < EV_ENE_DAT_MAX - 1; j++) {
                ev_ene_dats[room_id][j].dats = ev_ene_dats[room_id][j + 1].dats;
            }
            ev_ene_dats[room_id][EV_ENE_DAT_MAX - 1].dats.dat_no = -1;
            return;
        }
    }

    PRINT_WARNING("ENE_LOAD DELETE NOT FOUND FILE!! ROOM[%d] ene_dat_no = %d",
                  room_id, dat_no);
}

void ev_eneChangeRoom(int room_id)
{
    ENE_DATS old_ene_dats[EV_ENE_DAT_MAX];
    int      no_need_flg[EV_ENE_DAT_MAX];
    int      i;
    int      j;

    memset(no_need_flg, 0, sizeof(no_need_flg));

    for (i = 0; i < EV_ENE_DAT_MAX; i++) {
        old_ene_dats[i] = ene_dats[i];
    }

    for (i = 0; i < EV_ENE_DAT_MAX; i++) {
        ene_dats[i] = ev_ene_dats[room_id][i].dats;
    }

    /* Release everything the outgoing room had that the incoming one does not;
     * mark the survivors so the load pass below skips them. */
    for (i = 0; i < EV_ENE_DAT_MAX; i++) {
        if (old_ene_dats[i].dat_no == -1) {
            break;
        }

        for (j = 0; j < EV_ENE_DAT_MAX; j++) {
            if ((ene_dats[j].dat_no == old_ene_dats[i].dat_no) &&
                (ene_dats[j].ene_type == old_ene_dats[i].ene_type)) {
                no_need_flg[j] = 1;
                break;
            }
        }

        if (j == EV_ENE_DAT_MAX) {
            EneReleaseReq(old_ene_dats[i].ene_type, old_ene_dats[i].dat_no);
        }
    }

    for (i = 0; i < EV_ENE_DAT_MAX; i++) {
        if (ene_dats[i].dat_no == -1) {
            break;
        }

        if ((no_need_flg[i] != 1) &&
            (SearchEneWrkNo(ene_dats[i].ene_type, ene_dats[i].dat_no) < 0)) {
            EneLoadReq(ene_dats[i].ene_type, ene_dats[i].dat_no, (int *)nullptr);
        }
    }
}

int ev_eneIsReady(void)
{
    return 1;
}
