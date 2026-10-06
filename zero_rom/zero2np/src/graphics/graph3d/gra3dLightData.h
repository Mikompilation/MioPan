/* ==========================================================================
 *  gra3dLightData.h
 *
 *  The light-data container the gra3d layer keeps in RAM (GRA3DLIGHTDATA) and
 *  the on-disc light file it is loaded from (ZERO2LIGHTDATAFILE), plus the one
 *  free function that operates on a GRA3DLIGHTDATA (gra3dLightDataAddOffset-
 *  Position).  GRA3DLIGHTDATA / GRA3DLIGHTSTATUS themselves live in the master
 *  type cluster (gra3dTypes.h); this header adds the file wrapper and the API.
 *
 *  Reconstructed from the Feb 6 2004 prototype (SLES_523.84).
 * ======================================================================== */

#ifndef _GRA3DLIGHTDATA_H
#define _GRA3DLIGHTDATA_H

#include <cstddef>
#include "gra3dTypes.h"         /* GRA3DLIGHTDATA, GRA3DLIGHTSTATUS, G3DLIGHT */

#define GRA3DSIGNATURE_ZERO2LIGHTDATAFILE 0x646c7a  /* "Z2LD" */

/* ---- on-disc light file (one named slot per light, then the live data) --- *
 * astrLightName holds the 39 editor names (32 chars each); LD is the runtime
 * GRA3DLIGHTDATA payload sgdVerifyLightData validates and copies out. */
struct ZERO2LIGHTDATAFILE                       /* size 0x1890 */
{
    int            iSignature;                  /* 0x0000 */
    int            iSizeOfThisFile;             /* 0x0004 */
    int            aiPad[2];                    /* 0x0008 */
    char           astrLightName[NUM_GRA3DLIGHTID][32];       /* 0x0010 */
    GRA3DLIGHTDATA LD;                          /* 0x04f0 */
};
typedef struct ZERO2LIGHTDATAFILE ZERO2LIGHTDATAFILE;

/* Layout guards.  sgdVerifyLightData() memcpy's pZLD->LD straight out of the
 * loaded file, so any drift here silently shifts every light and the ambient
 * rather than failing -- and a room whose ambient parses as zero then trips
 * MapDrawInitRoom()'s 0.002 emergency floor and looks merely dark, not broken.
 * ROM sizes from types.txt: file 0x1890, LD at 0x04f0, payload 0x13a0. */
static_assert(sizeof(GRA3DLIGHTDATA) == 0x13a0, "GRA3DLIGHTDATA size drift");
static_assert(offsetof(ZERO2LIGHTDATAFILE, LD) == 0x04f0, "LD offset drift");
static_assert(sizeof(ZERO2LIGHTDATAFILE) == 0x1890, "light file size drift");
static_assert(sizeof(G3DLIGHT) == 0x70, "G3DLIGHT size drift");
static_assert(sizeof(GRA3DLIGHTSTATUS) == 0x10, "GRA3DLIGHTSTATUS size drift");

/* Copy pSrc into pDest, translating every light position by vPosition. */
void gra3dLightDataAddOffsetPosition(GRA3DLIGHTDATA *pDest, const GRA3DLIGHTDATA *pSrc,
                                     const float *vPosition);

#endif /* _GRA3DLIGHTDATA_H */
