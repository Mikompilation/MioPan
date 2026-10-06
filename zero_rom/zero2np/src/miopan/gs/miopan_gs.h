#ifndef MIOPAN_GS_H
#define MIOPAN_GS_H

#include "sce_gs.h"
#include <array>
#include <cstdint>
#include <unordered_map>
#include <vector>

namespace MioPan::GS
{
enum PixelStorageFormat
{
    PSMCT32  = 0x00,
    PSMCT24  = 0x01,
    PSMCT16  = 0x02,
    PSMCT16S = 0x0a,
    PSMT8    = 0x13,
    PSMT4    = 0x14,
    PSMT8H   = 0x1b,
    PSMT4HL  = 0x24,
    PSMT4HH  = 0x2c,
    PSMZ32   = 0x30,
    PSMZ24   = 0x31,
    PSMZ16   = 0x32,
    PSMZ16S  = 0x3a,
};

class GSHelper
{
public:
    GSHelper();

    void UploadPSMCT32(int dbp, int dbw, int dsax, int dsay, int rrw, int rrh,
                       const uint8_t *inbuf);
    void UploadPSMCT24(int dbp, int dbw, int dsax, int dsay, int rrw, int rrh,
                       const uint8_t *inbuf);
    void UploadPSMCT16(int dbp, int dbw, int dsax, int dsay, int rrw, int rrh,
                       const uint8_t *inbuf);
    void UploadPSMT8(int dbp, int dbw, int dsax, int dsay, int rrw, int rrh,
                     const uint8_t *inbuf, int psm = PSMT8);
    void UploadPSMT4(int dbp, int dbw, int dsax, int dsay, int rrw, int rrh,
                     const uint8_t *inbuf, int psm = PSMT4);

    void DownloadPSMCT32(unsigned char *outbuf, int dbp, int dbw, int dsax,
                         int dsay, int rrw, int rrh);
    void StorePSMCT24(unsigned char *outbuf, int dbp, int dbw, int dsax,
                      int dsay, int rrw, int rrh);
    void StorePSMCT16(unsigned char *outbuf, int dbp, int dbw, int dsax,
                      int dsay, int rrw, int rrh);
    void StorePSMT8(unsigned char *outbuf, int dbp, int dbw, int dsax,
                    int dsay, int rrw, int rrh);
    void StorePSMT4(unsigned char *outbuf, int dbp, int dbw, int dsax,
                    int dsay, int rrw, int rrh);
    void DownloadImagePSMT8(unsigned char *outbuf, int dbp, int dbw,
                            int dsax, int dsay, int rrw, int rrh, int cbp,
                            int cbw, int alpha_reg, int psm = PSMT8);
    void DownloadImagePSMT4(unsigned char *outbuf, int dbp, int dbw,
                            int dsax, int dsay, int rrw, int rrh, int cbp,
                            int cbw, int csa, int alpha_reg, int psm = PSMT4);
    void Clear();

    std::vector<unsigned char> mem_;
};

extern GSHelper gsHelper;

/* What a set of GS image transfers writes, at the nibble, and whether decoding
 * a texture reads nothing else -- texels and CLUT exactly as the decoders
 * below address them.  When it does not, the texture depends on whatever GS
 * memory held before those transfers, and decoding it straight after them is
 * no guarantee of decoding it the same way later.  Nothing is uploaded; this
 * only does the address arithmetic. */
class UploadCoverage
{
public:
    UploadCoverage();
    void Clear();
    void Add(const sceGsLoadImage &image);
    bool Covers(const sceGsTex0 &tex0) const;

private:
    void Mark(int psm, int bp, int bw, int x0, int y0, int w, int h);
    bool Marked(int psm, int bp, int bw, int x0, int y0, int w, int h) const;

    /* One bit per 256-byte block every nibble of which was written, and the
     * nibbles written in any block that was only partly written. */
    std::vector<uint64_t> full_;
    std::unordered_map<int, std::array<uint64_t, 8>> partial_;
};

/* Transfers ownership of the most recently decoded RGBA texture.  The C
 * download entry point keeps its legacy pointer return for reconstructed game
 * code; the host renderer calls this immediately afterwards so deferred GPU
 * uploads do not need to copy the complete image into a second CPU vector. */
std::vector<unsigned char> TakeDownloadedTexture();
}

#endif
