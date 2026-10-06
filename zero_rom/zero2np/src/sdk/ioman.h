/* ioman.h  (IOP file I/O — PC-port shim; see iop_host.h) */
#ifndef _IOP_IOMAN_H
#define _IOP_IOMAN_H

#include "iop_host.h"

/* Only iopCommandQuery()'s REQ_FILE_SIZE uses this, and only to lseek() the
 * handle MyOpen() returned.  MyOpen() is an empty stub in this build (the
 * host-PC read path was compiled out of the ROM), so this never sees a real
 * descriptor -- see the note on iopCommandQuery(). */
#define lseek MioPan_IopLseek

#endif /* _IOP_IOMAN_H */
