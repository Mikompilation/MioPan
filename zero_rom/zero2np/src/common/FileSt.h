/* ==========================================================================
 *  common/FileSt.h
 *
 *  File-label classification.  The whole module is one predicate that says
 *  which of two classes a load label belongs to, decided by a label-ID range
 *  held in a table.  MapLBuff.c is its only consumer.
 *
 *  Reconstructed from the Feb 6 2004 prototype (SLES_523.84).
 * ======================================================================== */

#ifndef _COMMON_FILEST_H
#define _COMMON_FILEST_H

/* Classifies `label_id`: 0 when it falls strictly inside the first table row's
 * range, 1 otherwise.  Only those two values are ever produced -- the callers
 * in MapLBuff.c switch over three, so their third case is dead. */
int FileStGetType(int label_id);

#endif /* _COMMON_FILEST_H */
