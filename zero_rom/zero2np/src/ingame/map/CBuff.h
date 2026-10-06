/* ==========================================================================
 *  ingame/map/CBuff.h
 *
 *  Deduplicating string table.  FurnLoad.c feeds it the model name of every
 *  registered door / object / put-item as the room is walked, and is left with
 *  the set of distinct names to load, each addressable by index.
 *
 *  Reconstructed from the Feb 6 2004 prototype (SLES_523.84).
 * ======================================================================== */

#ifndef _INGAME_MAP_CBUFF_H
#define _INGAME_MAP_CBUFF_H

/* Allocates max_num slots of max_len bytes from the system heap and empties
 * the table.  There is no matching guard in CBuffSetStr(), so max_num has to
 * be an upper bound the caller can actually vouch for -- see CBuff.c. */
void CBuffInit(int max_len, int max_num);
void CBuffTerm(void);

/* Empties the table without freeing it. */
void CBuffReset(void);

/* Adds `str` if it is not already present.  Returns 1 when it was already
 * there (nothing was added) and 0 when it was appended -- note this is the
 * opposite of the usual "success" sense. */
int CBuffSetStr(char *str);

/* Number of distinct strings currently held. */
int CBuffGetRegistNum(void);

/* String `id`, or NULL when id is outside [0, CBuffGetRegistNum()). */
char *CBuffGetStr(int id);

#endif /* _INGAME_MAP_CBUFF_H */
