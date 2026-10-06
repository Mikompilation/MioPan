// FILE: /home/zero_rom/zero2np/src/ingame/menu/tim_dat/file_tex_dat.c
//
// The collected-documents page's thumbnail paks (file_tex_dat.o, sdata
// 0x3f07b8).  Eight bytes and no code at all -- ZERO2.MAP gives the object a
// .sdata section and nothing else.
//
// Both are "DTS" paks: one small picture per file id, which is why menu_file.c
// reaches them through PK2SendVramOne(addr, file_id, ...) rather than
// PK2SendVram().  The matching full-size pictures are not in a pak -- they are
// one TM2 per file, PHT_DTL_000_TM2 + file_id and PIC_DTL_000_TM2 + file_id,
// streamed through the menu cross-fade slots.
//
// Reconstructed from the Feb 6 2004 prototype (SLES_523.84).

#include "file_tex_dat.h"

#include "../../../system/eeiop/cddat.h"         /* PHT_DTS_PK2 / PIC_DTS_PK2 */

int file_tex_pack[2] =                                      /* sdata 3f07b8 */
{
    PHT_DTS_PK2,        /* 3873 -- the photograph thumbnails */
    PIC_DTS_PK2,        /* 3952 -- the map thumbnails        */
};
