/* ==========================================================================
 *  system/pad/key_cnf.h
 *
 *  Controller logical-key/action mapping interface.
 *
 *  Reconstructed from the Feb 6 2004 prototype (SLES_523.84).
 * ======================================================================== */

#ifndef _SYSTEM_PAD_KEY_CNF_H
#define _SYSTEM_PAD_KEY_CNF_H

#include "eetypes.h"

extern u_short *default_key[32];
extern u_short *default_key_bak[32];
extern u_char   key_type[32];
extern u_short *paddat_m[3][26];
extern u_char  *pushdat_m[3][26];

void SetDefaultKeyType(void);
void SetKeyType(void);

#endif /* _SYSTEM_PAD_KEY_CNF_H */
