/* ==========================================================================
 *  ingame/plyr/charBB.h
 *
 *  Character-model bounding boxes.
 * ======================================================================== */

#ifndef _INGAME_PLYR_CHARBB_H
#define _INGAME_PLYR_CHARBB_H

#include "../../graphics/motion/mdlwork.h"

int charbbGet(float (*avBB)[4], const ANI_CTRL *pAC,
              const float (*matLocalWorld)[4]);

#endif /* _INGAME_PLYR_CHARBB_H */
