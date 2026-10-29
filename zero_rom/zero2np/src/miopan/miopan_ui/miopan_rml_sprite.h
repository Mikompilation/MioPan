#ifndef MIOPAN_RML_SPRITE_H
#define MIOPAN_RML_SPRITE_H

#include "../../graphics/graph2d/g2d_draw.h"   /* SPRT_DAT */

#include <cstdint>
#include <string>

/*
 * The game's own 2D art -- an icon, a portrait, a plate -- as an image an
 * RmlUi screen can show.  A SPRT_DAT names its picture by the VRAM address
 * the pak uploads it to (TEX0's TBP0 and CBP) and its part of it by u, v, w
 * and h; the pak resident at `pk2` (an EE address, as the ROM hands it to
 * PK2SendVram()) is searched for the picture with that address and the part
 * is decoded as DispSprD() would draw it: mirrored or turned over by its
 * flip bits, faded by its alpha, tinted by r, g, b (0x80 is untinted).
 *
 * Images are named by their pixels, so the same art is one texture however
 * often it is asked for.  The answer is a src for <img>, or "" when the
 * picture is not in that pak.  Each answer is remembered, so asking every
 * frame is cheap; Forget() drops what is remembered, for when a pak at an
 * address it has answered for may have been loaded over -- a menu visit's
 * paks, say.
 */
namespace MioPanRmlSprite
{
/* How DispSprD() is asked to draw it, beyond the sprite's own record. */
struct Look
{
    int r = 0x80;
    int g = 0x80;
    int b = 0x80;
    /* Quarter turns clockwise about its corner, as `rot` 90 turns it: the
     * image comes out h wide and w tall. */
    int turns = 0;
    /* alphar 0x48, Cs * As + Cd: it adds to what is under it. */
    bool additive = false;
    /* Only the pak's picture at this index, as PK2SendVramOne() sends one --
     * a thumbnail out of a pak of them all at the same address; -1 for any. */
    int picture = -1;
};

std::string Image(uintptr_t pk2, const SPRT_DAT &sprite, const Look &look);
std::string Image(uintptr_t pk2, const SPRT_DAT &sprite, int r = 0x80,
                  int g = 0x80, int b = 0x80);

/* A whole TIM2 the game loaded on its own rather than in a pak -- an item's
 * picture.  Named by its contents, so a buffer loaded over with another
 * picture gives another image; decoded only the first time those contents
 * are seen. */
std::string Picture(const void *tim2);
/* Its part at u, v, w, h, as a sprite samples a picture streamed into VRAM --
 * a spirit's photograph. */
std::string Picture(const void *tim2, int u, int v, int w, int h);

/* A picture the game keeps as bare 32-bit texels rather than a TIM2 -- a
 * photograph, at `addr` in EE memory, w x h of rows `stride` texels long --
 * as DrawPhotoBuffer() uploads it.  Named by its contents. */
std::string Texels(uintptr_t addr, int w, int h, int stride);

void Forget();
}

#endif
