/* ==========================================================================
 *  system/eeiop/hxd.h
 *
 *  HXD sound-header helpers (hxd.c).  An HXD file is a HXD_HEADER followed by
 *  `num` SOUND_INFO records; the matching BD file holds the ADPCM bodies they
 *  index.  snd_bank.c and snd_stream.c validate a freshly loaded header with
 *  CheckHXDData(); the two Print* entries are debug dumps.
 *
 *  Reconstructed from the Feb 6 2004 prototype (SLES_523.84).
 * ======================================================================== */

#ifndef _SYSTEM_EEIOP_HXD_H
#define _SYSTEM_EEIOP_HXD_H

#include "snd_def.h"                /* HXD_HEADER, SOUND_INFO */

/* HXD `type` values, from the two call sites: snd_bank.c asks for 0 (a bank
 * of one-shot samples), snd_stream.c for 1 (a stream description). */
#define HXD_TYPE_BANK    0
#define HXD_TYPE_STREAM  1

void CheckHXDData(HXD_HEADER *header, int requested_file_type);

/* Takes its record by value -- the ROM copies all 0x1c bytes into the callee's
 * own frame, which is the EE ABI's large-struct-by-value convention. */
void PrintSOUND_INFO(SOUND_INFO info);
void PrintSOUND_INFOArray(SOUND_INFO *info, int num);

#endif /* _SYSTEM_EEIOP_HXD_H */
