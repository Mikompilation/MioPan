/* ==========================================================================
 *  ingame/photo/finder_dpad.h
 *
 *  PORT ADDITION -- no ROM counterpart.  The d-pad in the viewfinder loads
 *  film and uses healing items, so neither takes a trip to the menu while a
 *  ghost is in front of the lens.  Options > Special switches it ([game]
 *  finder_dpad in miopan.ini).
 *
 *    Up / Down   the next / the previous film Mio carries, Type-07 to Zero
 *    Left        the next healing item she carries
 *    Right       use it
 *
 *  Both go through ItemUse(), so the game sees exactly what the menu would
 *  have done: a film is loaded without being spent and the shutter's charge
 *  is left as it was (the next one charges at the new film's speed), and a
 *  healing item is spent only when it heals.  The cues are the viewfinder's
 *  own rather than the menu's: its selector's click, its power chime and the
 *  camera's refusal.
 *
 *  In the ROM the d-pad aims the finder slowly (FinderModePadChk()).  While
 *  this is on it does not; the sticks, the mouse and a gyro still aim.
 *
 *  The healing item is shown on the viewfinder's right-hand flat, between the
 *  film readout and the health bar: the item menu's own picture of it
 *  (data/tim/item/item_NNN.tm2, loaded once with the finder's paks) with how
 *  many are left.
 * ======================================================================== */

#ifndef _INGAME_PHOTO_FINDER_DPAD_H
#define _INGAME_PHOTO_FINDER_DPAD_H

/* Queue the healing items' pictures.  From IngameLoadOnce(), beside the
 * finder's own paks. */
void FinderDpadLoadOnce(void);

/* 1 while the d-pad is this file's rather than the finder aim's: switched on,
 * and the finder is up (player mode 6). */
int  FinderDpadOwnsDpad(void);

/* One finder frame's presses, from PlyrFinderCtrl(). */
void FinderDpadCtrl(void);

/* The healing item's picture and count, from CNPlyrCamera::Draw() with the
 * finder pak in VRAM -- the count is drawn in the film readout's digits. */
void FinderDpadDraw(int fndr_mx, int fndr_my, int iAlpha);

#endif /* _INGAME_PHOTO_FINDER_DPAD_H */
