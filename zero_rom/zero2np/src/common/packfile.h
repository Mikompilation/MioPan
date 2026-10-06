/* ==========================================================================
 *  common/packfile.h
 *
 *  Small FF2 packed-file container helpers.
 *
 *  Reconstructed from the Feb 6 2004 prototype (SLES_523.84).
 * ======================================================================== */

#ifndef _COMMON_PACKFILE_H
#define _COMMON_PACKFILE_H

#ifdef __cplusplus
extern "C" {
#endif

int   GetNumInPak(void *pak_head);

/* The "pk2" variant is a different container from the record-walking
 * GetFileInPak above: word 0 is the entry count and words 4.. are byte
 * offsets from the pack base, so lookup is O(1). */
int    Pk2GetNum(u_int *top_addr);
u_int *Pk2GetAddr(u_int *top_addr, int index);
void *GetFileInPak(void *pak_head, int num);
u_int *PakAlign128(u_int *addr);
void *GetPakTaleAddr(void *pak_head);

#ifdef __cplusplus
}
#endif

#endif /* _COMMON_PACKFILE_H */
