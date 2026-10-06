// FILE: /home/zero_rom/zero2np/src/ingame/photo/m_plyr_camera.c
//
// The player's photo camera -- the object itself, and the four hooks that hand
// its saveable parts to the memory card.
//
// The whole translation unit is 27 lines: `m_plyr_camera` and four accessors.
// Everything the camera *does* lives in n_plyr_camera.o (the per-frame work and
// the draw), n_equip_tray.o, filament.o, camera_power_up.o and bonus_shot.o;
// this file owns only the storage.
//
// That storage is not inert, though.  The object sits in .data zero-filled and
// its constructor chain -- 0x2e4 bytes of it, all inlined member constructors
// -- runs before main().  Almost every one of those writes a zero the .data
// already holds, but four do not, and they are the reason the chain has to be
// reproduced rather than skipped:
//
//   * CNEquipTrayWrk::mRenzMarkBlink comes out holding 20, its CWrkVariable's
//     low bound.  That is what proved Reset() seeds mValue with Min and not
//     zero -- see the note in common/variable.h.
//   * CNEquipTrayWrk::mAccumulateBollRot opens its range to the whole float
//     format (CFVariable's constructor, below).
//   * mSpiritNoise and sp.mSe come out holding CSND_BUF_PLAY_NO_ID, not 0 --
//     and 0 is a valid sound-buffer handle.
//   * the chain ends by calling CNPlyrCamera::Init(), so the camera is already
//     reset before the first frame.
//
// Reconstructed from the Feb 6 2004 prototype (SLES_523.84), m_plyr_camera.o.
// Trailing /* NNN */ comments are the original source line numbers.

#include "m_plyr_camera.h"

#include <float.h>                                  /* FLT_MAX */

CNPlyrCamera m_plyr_camera;                         /* data 319ad8 */   /* 4 */

void m_plyr_cameraSetSaveEQ(MC_SAVE_DATA *save)
{                                                                       /* 8 */
    m_plyr_camera.eq_tray.SetSave(save);                                /* 9 */
}

void m_plyr_cameraSetSavePowrUp(MC_SAVE_DATA *save)
{
    save->addr = (u_char *)&m_plyr_camera.camera_power_up;              /* 14 */
    save->size = sizeof(CCameraPowerUp);                                /* 15 */
}

void m_plyr_cameraSetSaveFilament(MC_SAVE_DATA *save)
{                                                                       /* 19 */
    m_plyr_camera.filament.SetSave(save);                               /* 20 */
}

void m_plyr_cameraSetSaveFilmType(MC_SAVE_DATA *save)
{
    save->addr = (u_char *)&m_plyr_camera.camera_film;                  /* 25 */
    save->size = sizeof(CCameraFilm);                                   /* 26 */
}

/* --------------------------------------------------------------------------
 *  Below this point are bodies that belong to other object files and are
 *  parked here until those are split out.  The constructor chain above is what
 *  pins CFVariable's; the rest are stubs.
 * ------------------------------------------------------------------------ */

/* Opens the range as wide as the float format goes and parks the value at the
 * bottom of it.
 *
 * The upper sentinel is .sdata 3f1940 = 0x7fffffff, which is the EE's largest
 * float but a NaN on the host -- every comparison against it would come out
 * false, so FLT_MAX is written instead, per the standing rule for that
 * constant.  The lower one is .lit4 3ee498 = 0x00800001, one ulp above the
 * smallest normal float; that decimal round-trips exactly. */
CFVariable::CFVariable(void)
{
    m_fMin = 1.1754945e-38f;                        /* lit4  3ee498 = 0x00800001 */
    m_fMax = FLT_MAX;                               /* sdata 3f1940 = 0x7fffffff */

    mValue = m_fMin;
}

/* CCameraPowerUp::Init, ::GetRadiusRate and ::AllRelease were here; all three
 * belong to camera_power_up.o and are reconstructed in camera_power_up.c.
 *
 * CNEquipTraySave::Init and CNEquipTrayWrk::SetBattleFlg were stubbed here;
 * they belong to n_equip_tray.o and are reconstructed in n_equip_tray.c.
 *
 * CNPlyrCamera::SetUp / Init / Release / Main / Draw were stubbed here too;
 * they belong to n_plyr_camera.o and are reconstructed in n_plyr_camera.c. */
