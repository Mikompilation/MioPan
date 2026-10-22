#ifndef MIOPAN_GS_C_H
#define MIOPAN_GS_C_H

#include "sce_gs.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void MioPan_GsClear(void);
void MioPan_GsUpload(sceGsLoadImage *image_load, unsigned char *image);
/* While non-zero, MioPan_GsUpload() drops what it is handed -- nothing is
 * written, invalidated or counted.  Set only around a model's own texture
 * sends once the host has resolved that model's textures (see
 * MioPan_Graph3dBeginTim2Upload()); every other upload is untouched. */
void MioPan_GsSetUploadSuppressed(int suppressed);
/* While set, called with every transfer MioPan_GsUpload() goes on to perform
 * -- the host texture binding uses it to learn exactly what a character's or
 * item's send writes.  NULL clears it. */
void MioPan_GsSetUploadObserver(void (*observer)(const sceGsLoadImage *image,
                                                 void *user),
                                void *user);
/* GS memory by reference (the default): a transfer is kept as the bytes it
 * sent and rearranged into GS memory only when something reads it there; a
 * transfer of what GS memory already holds is dropped; textures decode from
 * what was sent.  0 is the A/B -- every transfer swizzled in as it arrives.
 * See the block above UploadByReference() in miopan_gs.cpp. */
void MioPan_GsSetByReference(int enable);
int MioPan_GsGetByReference(void);
void MioPan_GsStore(sceGsStoreImage *sp, unsigned char *out);
unsigned char *MioPan_GsDownloadTexture(sceGsTex0 *tex0, uint64_t *hash);
uint64_t MioPan_GetTextureHash(sceGsTex0 *tex0);

void MioPan_GsResetFrameMetrics(void);
int MioPan_GsGetUploadCount(void);
uint64_t MioPan_GsGetUploadCountLive(void);
uint64_t MioPan_GsGetUploadBytesLive(void);
/* This frame's transfers that found GS memory already holding what they sent,
 * and the bytes actually rearranged into GS memory. */
uint64_t MioPan_GsGetUnchangedCountLive(void);
uint64_t MioPan_GsGetWrittenBytesLive(void);
int MioPan_GsGetUploadBytes(void);
float MioPan_GsGetUploadMs(void);
int MioPan_GsGetDownloadCount(void);
int MioPan_GsGetDownloadBytes(void);
float MioPan_GsGetDownloadMs(void);

int MioPan_GsHasPendingUploads(void);
void MioPan_GsConsumePendingUploads(
    void (*cb)(int addr, int size, void *ud), void *ud);
void MioPan_GetTextureGsRegion(sceGsTex0 *tex0, int *out_addr, int *out_size);

#ifdef __cplusplus
}
#endif

#endif
