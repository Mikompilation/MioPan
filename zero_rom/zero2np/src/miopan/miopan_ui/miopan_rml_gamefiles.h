#ifndef MIOPAN_RML_GAMEFILES_H
#define MIOPAN_RML_GAMEFILES_H

/*
 * The screen for when there is no game to start.
 *
 * MioPan ships without the game.  When the data folder, the setting and the
 * search around the executable all come up empty, this asks the player for
 * their own copy -- a disc image, or IMG_BD.BIN off the disc -- checks what
 * they chose, saves it in [paths] data_folder and lets the game start.  A choice that will not work is explained on the spot, and they can
 * choose again.
 *
 * It runs ahead of everything else, while nothing of the game has started:
 * SDL_AppIterate() ticks this until it answers 0, and only then the game.  That
 * is what lets the data root stay decided once (miopan_paths.h): nothing has
 * asked for it yet, so it is decided after the player has answered.  Unlike the
 * other screens this one is loaded when it is needed and closed for good when
 * it is done; a boot that finds the game never touches it.
 *
 * With the game's sound bank unread there is nothing for MioPanRmlCommon's cues
 * to play, so this screen is silent.
 */

#ifdef __cplusplus
extern "C" {
#endif

/* One tick of the screen, drawing it if it is needed.  1 while the player is
 * being asked, 0 once the game can start -- from the first tick, when the files
 * are where they should be, or when RmlUi could not come up, which leaves the
 * game to report what it cannot find as it always has. */
int MioPan_RmlGameFilesTick(void);

#ifdef __cplusplus
}
#endif

#endif /* MIOPAN_RML_GAMEFILES_H */
