// FILE: /home/zero_rom/zero2np/src/ingame/event/prg/ev_sis.c
//
// Per-area sister presence.  One bit per area says whether the sister model
// should be resident there; a room change loads or drops her accordingly.
//
// The mdl_no / anm_no arguments to ev_sisRegister are vestigial in this build:
// only the presence bit is stored, and the model numbers come from the costume
// selection instead.
//
// Reconstructed from the Feb 6 2004 prototype (SLES_523.84).

#include "ev_sis.h"

#include "../../../common/variable.h"       // BIT_FLAGS
#include "../../plyr/sis_mdl.h"             // SetupSisMdl / ReleaseSisMdl

#define EV_SIS_AREA_MAX 66

static BIT_FLAGS<EV_SIS_AREA_MAX> area_sis_flg;     /* bss 47b920 */

static int ev_sisIsRegistered(int area_no);

void ev_sisSetSave(MC_SAVE_DATA *save)
{
    save->size = sizeof(area_sis_flg);
    save->addr = (u_char *)&area_sis_flg;
}

void ev_sisInit(void)
{
    area_sis_flg.AllDown();
}

void ev_sisRegister(int area_no, int mdl_no, int anm_no)
{
    area_sis_flg.FlgUp(area_no);
}

static int ev_sisIsRegistered(int area_no)
{
    return area_sis_flg.IsUp(area_no);
}

void ev_sisDelete(int area_no)
{
    area_sis_flg.FlgDown(area_no);
}

void ev_sisRelease(void)
{
    ReleaseSisMdl();
}

void ev_sisChangeRoom(int room_id)
{
    if (ev_sisIsRegistered(room_id) != 0) {
        SetupSisMdl();
        return;
    }

    ReleaseSisMdl();
}
