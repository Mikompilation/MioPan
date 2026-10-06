/* ==========================================================================
 *  common/utility2.h
 *
 *  Shared low-level utilities (utility2.c).  This header exposes the engine's
 *  assert / warning reporting shim used throughout the non-graphics code:
 *
 *    SetAssertPreMessage(fmt, ...)  - vsprintf the location banner into a
 *                                     module-static scratch buffer.
 *    PrintAssertReal(str, ...)      - append the caller's message (its own
 *                                     printf args) to that banner and hand the
 *                                     whole line to the installed assert hook
 *                                     (or spin, printing it, if none is set).
 *    PrintWarningReal(str, ...)     - the non-fatal sibling.
 *    SetPrintAssert / SetPrintWarning - install those hooks.
 *
 *  Both reporters are variadic (the format arguments flow through
 *  PrintAssertReal, not SetAssertPreMessage), so PRINT_ASSERT forwards a
 *  variable argument list.
 *
 *  Reconstructed from the Feb 6 2004 prototype (SLES_523.84).
 * ======================================================================== */

#ifndef _COMMON_UTILITY2_H
#define _COMMON_UTILITY2_H

/* ---- alignment helpers ------------------------------------------------- */
int          Get2Power(int num);                        /* ceil(log2(num))   */
unsigned int GetAlignUp(unsigned int a, int power);     /* round up to 1<<power */

/* ---- clamp helper ------------------------------------------------------ */
/* Upper bound comes before the lower one, matching the ROM's argument order. */
float        GetClampValF(float val, float upper, float lower);

/* ---- string helpers ---------------------------------------------------- */
/* Lower-case `str` in place and return it. */
char        *StrToLower(char *str);

/* ---- rotation helper --------------------------------------------------- */
/* Fold a rotation back into (-PI, PI].  One step only: a value more than a
 * full turn out of range comes back still out of range. */
float        RotLimitChk2(float rot);

/* ---- assert / warning reporting --------------------------------------- */
void SetAssertPreMessage(const char *fmt, ...);
void PrintAssertReal(const char *str, ...);
void PrintWarningReal(const char *str, ...);
void SetPrintAssert(void (*func)(char *));
void SetPrintWarning(void (*func)(char *));

/* Stamp the location banner, then print `fmt` (with any printf args) as the
 * assert body.  C++20 __VA_OPT__ drops the comma when no extra arguments are
 * supplied. */
#define PRINT_ASSERT(fmt, ...)                                                      \
    do                                                                              \
    {                                                                               \
\
        SetAssertPreMessage("\n\tFILE_NAME : %s \n\tLINE :%d \n\tFUNC_NAME: %s\n",  \
                            __FILE__, __LINE__, __FUNCTION__);                      \
        PrintAssertReal(fmt __VA_OPT__(,) __VA_ARGS__);                             \
    } while (0)

/* The third reporter in the family, and the most common one in the map code.
 * Unlike the two above it goes straight to printf -- no hook, no scratch
 * buffer -- which is why the ROM emits a bare pair of printf calls at every
 * call site rather than a call to a shared routine.  The banner carries the
 * macro's own __FILE__/__LINE__, so a decompiled ROM line number tells you
 * exactly where the invocation sat in the original source. */
#define PRINT_ERROR(fmt, ...)                                                   \
    do                                                                          \
    {                                                                           \
        printf("***ERR!! %s(%d):", __FILE__, __LINE__);                         \
        printf(fmt __VA_OPT__(,) __VA_ARGS__);                                  \
    } while (0)

#define PRINT_WARNING(fmt, ...)                                                 \
    do                                                                          \
    {                                                                           \
        printf("<<<<<<<<<WARNING FILE[%s] LINE[%d]>>>>>>>>>>\n",                \
               __FILE__, __LINE__);                                             \
        PrintWarningReal(fmt __VA_OPT__(,) __VA_ARGS__);                        \
    } while (0)

#endif /* _COMMON_UTILITY2_H */
