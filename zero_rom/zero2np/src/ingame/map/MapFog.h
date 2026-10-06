/* ==========================================================================
 *  ingame/map/MapFog.h
 *
 *  Map fog interface.
 *
 *  Reconstructed from the Feb 6 2004 prototype (SLES_523.84).
 * ======================================================================== */

#ifndef _INGAME_MAP_MAPFOG_H
#define _INGAME_MAP_MAPFOG_H

/* One fog setting: the colour plus the distance ramp fed to gra3dSetFog().
 * `near`/`far` are the ramp in world units, `min`/`max` the 0..255 density at
 * each end of it.  MapFogAnim() walks all seven ints as a block, so the order
 * of the members is load-bearing. */
typedef struct                      /* 0x1c */
{
    /* 0x00 */ int r;
    /* 0x04 */ int g;
    /* 0x08 */ int b;
    /* 0x0c */ int near;
    /* 0x10 */ int far;
    /* 0x14 */ int min;
    /* 0x18 */ int max;
} MAP_FOG_HEAD;

/* &MapFogDat[2].r -- the three ints in force this frame, read as rc[0..2].
 * MapSky.c tints the horizon strip with these. */
int *MapFogGetColor(void);

/* Arm an interpolation: MapFogDat[0] takes a copy of the setting in force,
 * and the next `frame` calls to MapFogAnim() walk it into MapFogDat[1]. */
void MapFogAnimStart(int frame);

/* One step of that walk.  Global in the ROM's link map even though MapFog.c
 * is its only caller; it is always called as
 * MapFogAnim(&MapFogDat[2], &MapFogDat[0], &MapFogDat[1]). */
void MapFogAnim(MAP_FOG_HEAD *now, MAP_FOG_HEAD *st, MAP_FOG_HEAD *en);

/* Per-frame.  `pos` is accepted and ignored -- the fog region is sampled at
 * the *camera*, not at the player.  MapFogDbProc() is the debug-mode entry
 * and does nothing extra in this build. */
void MapFogProc(int room_no, int floor, float *pos);
void MapFogDbProc(int room_no, int floor, float *pos);

void MapFogReset(void);

/* Scripted fog override -- PlyrCondCheck() raises it while the player is
 * poisoned and drops it again when the condition times out.  While the event
 * flag is up MapFogProc() stops sampling the map's fog regions entirely, so
 * the scripted setting is what stays on screen. */
void MapFogStartFogEv(int r, int g, int b, int st, int en, int min, int max);
void MapFogEndFogEv(int frame);

#endif /* _INGAME_MAP_MAPFOG_H */
