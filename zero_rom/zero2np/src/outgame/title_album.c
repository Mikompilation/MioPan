// FILE: /home/zero_rom/zero2np/src/outgame/title_album.c
//
// The title-side wrapper around the photo album.  All three functions just
// forward to album.o; the only thing this file adds is the argument that puts
// the album into its outgame mode, and the exit back to the title menu.
//
// Reconstructed from the Feb 6 2004 prototype (SLES_523.84).
// Trailing /* NNN */ are ROM source line numbers, measured from
// disassemble_function.

#include "title_album.h"

#include "../album/prg/album.h"             // AlbumInit / AlbumMain / AlbumDispMain
#include "../main/gphase.h"                 // SetNextGPhase / GID_TITLE_MENU

void TitleAlbumInit(void)
{
    AlbumInit(1);                                                       /* 50 */
}

/* AlbumMain() returns non-zero when the player backs out. */
void TitleAlbumMain(void)
{
    if (AlbumMain() != 0)                                               /* 65 */
    {
        SetNextGPhase(GID_TITLE_MENU);                                  /* 67 */
    }
}

void TitleAlbumDispMain(void)
{
    AlbumDispMain();                                                    /* 83 */
}
