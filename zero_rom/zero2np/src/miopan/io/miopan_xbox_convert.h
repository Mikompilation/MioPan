#ifndef MIOPAN_XBOX_CONVERT_H
#define MIOPAN_XBOX_CONVERT_H

/*
 * Fatal Frame II Xbox (Director's Cut) models -> the PS2 formats the game loads.
 *
 * Pure byte conversion: no file system, no SDL, no game state, so it can run on
 * a worker thread and be built on its own.  miopan_xbox_assets.cpp finds the
 * files and writes the results; the format notes are in the .cpp.
 */

#include <cstdint>
#include <string>
#include <vector>

namespace MioPanXboxConvert
{

using Bytes = std::vector<uint8_t>;

struct Result
{
    Bytes       data;           /* the PS2 file, empty on failure */
    std::string error;          /* empty on success */

    int units = 0;              /* SGD units (a costume's body and sub-objects) */
    int textures = 0;
    int vram_blocks = 0;        /* of the 0x2bc0 character window */
    int strips = 0;
    int batches = 0;            /* VUVN + MESH pairs written */
};

/* A costume: the Xbox .mdl and the .ppd beside it -> a PS2 .mdl. */
Result Character(const Bytes &mdl, const Bytes &ppd);

/* An item: the Xbox .pk2 and its .ppd -> a PS2 .pk2. */
Result Item(const Bytes &pk2, const Bytes &ppd);

} // namespace MioPanXboxConvert

#endif /* MIOPAN_XBOX_CONVERT_H */
