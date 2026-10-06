// FILE: /home/zero_rom/zero2np/src/ingame/photo/camera_film.c
//
// Which film is loaded, and what that film is worth.
//
// The whole translation unit is three tables -- camera_film.o has no .text at
// all, because every accessor is inline in camera_film.h.  They are indexed by
// CCameraFilm::mFilmType (0..4) and read, in order, as the damage a shot does,
// the minimum drain percentage it guarantees, and how many frames the shutter
// takes to recharge.
//
// Extracted from .sdata 0x3ef598 and verified against the ROM.  The class
// itself is declared in m_plyr_camera.h until camera_film.h is split out.
//
// Reconstructed from the Feb 6 2004 prototype (SLES_523.84), camera_film.o.

#include "m_plyr_camera.h"

/* Type-07, Type-14, Type-61, Type-90 and the fifth (unnumbered) film. */
unsigned char CCameraFilm::aFilmDamageTbl[5] =                          /* sdata 3ef598 */
{
    8, 20, 25, 30, 40
};

unsigned char CCameraFilm::aFilmMinPercentTbl[5] =                      /* sdata 3ef5a0 */
{
    40, 60, 80, 0, 0
};

/* NTSC frames.  Stronger film recharges faster, except for the fifth entry,
 * which is slow again. */
unsigned char CCameraFilm::aFilmChargeSpdTbl[5] =                       /* sdata 3ef5a8 */
{
    170, 100, 80, 60, 150
};

/* ---- STUB: the two accessors nothing reconstructed calls yet ----
 * Both are inline in camera_film.h in the ROM; they are out of line here only
 * because no expansion of either survives to copy. */
int CCameraFilm::GetFilmDamage(void)
{
    return aFilmDamageTbl[mFilmType];
}

int CCameraFilm::GetFilmMinPercent(void)
{
    return aFilmMinPercentTbl[mFilmType];
}
