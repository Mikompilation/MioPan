/* ==========================================================================
 *  pcport_stubs.h  (PC-port stubs for genuinely-absent EE symbols)
 *
 *  Force-included via pc_prefix.h.  Provides host stand-ins for EE facilities
 *  that are cross-cutting enough not to have a narrower SDK owner: the VU1/EE
 *  scratchpad aliases, trap no-op, and small type aliases.  SDK functions/data
 *  live beside their matching SDK headers, not here.
 *
 *  Reconstructed from the Feb 6 2004 prototype (SLES_523.84).
 * ======================================================================== */

#ifndef _PCPORT_STUBS_H
#define _PCPORT_STUBS_H

#include "scetypes.h"

/* ---- small type aliases the decompiler emitted -------------------------- */
#ifndef _PCPORT_UINT_DEFINED
#define _PCPORT_UINT_DEFINED
typedef unsigned int uint;
#endif

#ifndef _PCPORT_BYTE_DEFINED
#define _PCPORT_BYTE_DEFINED
typedef unsigned char byte;
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* ---- EE VU1/EE scratchpad (0x70000000..0x70003FFF, 16 KiB) --------------
 *  The reconstruction refers to fixed scratchpad addresses through Ghidra
 *  `DAT_7000xxxx` names and walks between them with pointer arithmetic
 *  (e.g. &DAT_70003900 + i*4 reaching DAT_70003910), so they must be one
 *  contiguous buffer.  Map every name onto a single host array at its byte
 *  offset. */
extern float g_sceScratchpad[0x4000 / 4];

#ifdef __cplusplus
}
#endif

#define _SPR(byteoff) (g_sceScratchpad[(byteoff) / 4])
#define DAT_70003900  _SPR(0x3900)
#define DAT_70003910  _SPR(0x3910)
#define DAT_70003920  _SPR(0x3920)
#define DAT_70003930  _SPR(0x3930)
#define DAT_70003940  _SPR(0x3940)
#define DAT_70003950  _SPR(0x3950)
#define DAT_70003960  _SPR(0x3960)
#define DAT_70003970  _SPR(0x3970)
#define DAT_70003980  _SPR(0x3980)

/* The PS2 linker map puts _heap_size at 0xffffffff and the ROM uses its ADDRESS
 * as an "invalid pointer" sentinel.  On the EE a pointer is 4 bytes, so
 * `&_heap_size` and `(T *)-1` are the same bit pattern, and the ROM's sources
 * spell the sentinel both ways interchangeably.
 *
 * On a 64-bit host they are NOT the same: 0xffffffffu is 0x00000000ffffffff
 * while `(T *)-1` is 0xffffffffffffffff.  A sentinel armed with one spelling and
 * tested with the other therefore never compares equal, and both spellings are
 * live in this tree -- gra3dSGD.c arms save_tri2_pointer / save_bw_pointer with
 * `&_heap_size` and tests them against `(SGDPROCUNITHEADER *)-1`, and
 * sgdRemap() arms a root coordinate's pParent with `(SGDCOORDINATE *)-1` while
 * sgdClearCoordCalcFlgParents() tests it against `&_heap_size`.  That second
 * pair is not cosmetic: the failed guard recurses into address -1.
 *
 * Defining it as all-ones is the faithful translation (on a 32-bit target
 * 0xffffffff IS all-ones) and makes every existing spelling agree again.  Only
 * the address is ever taken -- nothing reads the value -- so the dereference in
 * the macro is never evaluated. */
#ifndef _heap_size
#define _heap_size (*(char *)~(uintptr_t)0)
#endif

/* ---- EE kernel no-ops (no host equivalent) ------------------------------ */
/* `trap` raises an EE exception; on the host treat it as an assertion-style
   no-op so the surrounding control flow still compiles. */
#ifndef trap
#define trap(n) ((void)0)
#endif

#endif /* _PCPORT_STUBS_H */
