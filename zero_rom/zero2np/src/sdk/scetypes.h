/* ==========================================================================
 *  scetypes.h  (PS2 / EE fundamental scalar types — PC-port shim)
 *
 *  Host-portable definitions of the EE/SCE base types the reconstruction uses.
 *  Sizes follow the EE ABI (char=1, short=2, int=4, long=8, 128-bit qword) so
 *  the reconstructed GS/VIF/DMA register + tag bitfield structs keep their
 *  layout.  The 128-bit type is emulated where the host compiler has no native
 *  __int128 (MSVC); it is never used in a bitfield, so a 16-byte aggregate is
 *  sufficient.
 *
 *  This header is force-included into every translation unit (see
 *  CMakeLists.txt) so these names — and NULL — are ambient exactly the way the
 *  ubiquitous PS2 SDK includes made them on the EE.
 * ======================================================================== */

#ifndef _SCETYPES_H
#define _SCETYPES_H

#include <stdint.h>
#include <stddef.h>              /* NULL, size_t */

typedef uint8_t   u_char;
typedef uint16_t  u_short;
typedef uint32_t  u_int;
typedef uint64_t  u_long;       /* EE ABI: `long` / `u_long` is 64-bit        */
typedef int8_t    s_char;

typedef volatile uint8_t  vu_char;
typedef volatile uint16_t vu_short;
typedef volatile uint32_t vu_int;
typedef volatile uint64_t vu_long;

/* 128-bit quadword (lq/sq loads, DMA tags). */
#if defined(__SIZEOF_INT128__)
typedef          __int128 long128;
typedef unsigned __int128 u_long128;
#elif defined(_MSC_VER)
typedef struct __declspec(align(16)) { uint64_t _lo, _hi; } long128;
typedef struct __declspec(align(16)) { uint64_t _lo, _hi; } u_long128;
#else
typedef struct { uint64_t _lo, _hi; } long128;
typedef struct { uint64_t _lo, _hi; } u_long128;
#endif

typedef int qword[4];           /* 128-bit as 4×int (matches types.txt)       */

#endif /* _SCETYPES_H */
