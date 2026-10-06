// FILE: /home/zero_rom/zero2np/src/common/utility2.c
//
// Shared low-level utilities.  This portion is the engine's assert / warning
// reporting shim, used throughout the non-graphics code via the PRINT_ASSERT
// macro (see utility2.h):
//
//   * SetAssertPreMessage - vsprintf a location banner (FILE/LINE/FUNC) into
//                           the module-static scratch buffer g_cComment.
//   * PrintAssertReal      - append the caller's message (with its own printf
//                            arguments) after that banner and dispatch the line
//                            to the installed assert hook; with no hook set it
//                            prints the line and spins (a hard assert).
//   * PrintWarningReal     - the non-fatal sibling: dispatch to the warning
//                            hook, or print and continue.
//   * SetPrintAssert / SetPrintWarning - install those hooks.
//
// Both reporters are variadic: the caller's format arguments are consumed by
// PrintAssertReal / PrintWarningReal, not by SetAssertPreMessage.
//
// Reconstructed from the Feb 6 2004 prototype (SLES_523.84).

#include "utility2.h"               // this file's public API + PRINT_ASSERT

#include <ctype.h>                  // isupper
#include <stdarg.h>                 // va_list / va_start / va_end
#include <stdio.h>                  // printf / vsprintf
#include <string.h>                 // strcpy / strlen

// The ROM's PI, as it appears in this object's .lit4 (0x3ee9ec).  It is a
// truncated 3.1415925f, not the libm constant -- keep it for bit-fidelity.
static const float UTIL2_PI  = 3.1415925f;
static const float UTIL2_PI2 = 6.283185f;

// ──────────────────────────────────────────────────────────────────────
// Alignment helpers.

// Smallest `power` such that (1 << power) >= num.
int Get2Power(int num)
{
    int power;
    int cmp;

    power = 0;
    cmp = 1;
    if (num < 1)
    {
        do
        {
            cmp <<= 1;
            power++;
        } while (num < cmp);
    }

    return power;
}

// Round `a` up to the next multiple of (1 << power).
unsigned int GetAlignUp(unsigned int a, int power)
{
    unsigned int mask;

    mask = ~(-1 << (power & 0x1f));
    return ((a + mask) >> (power & 0x1f)) << (power & 0x1f);
}

// ──────────────────────────────────────────────────────────────────────
// Clamp helpers.  Note the argument order: upper bound before lower.

float GetClampValF(float val, float upper, float lower)
{
    if (upper < val)                        /* 173 */
    {
        return upper;
    }

    if (val < lower)                        /* 175 */
    {
        return lower;
    }

    return val;
}

// ──────────────────────────────────────────────────────────────────────
// Statics.

static void (*print_warning_func)(char *);  // sdata 3f48d0 : non-fatal report hook
static void (*print_assert_func)(char *);    // sdata 3f48d4 : assert report hook
static char g_cComment[300];                 // bss   4bc658 : assembled banner

// ──────────────────────────────────────────────────────────────────────
// Install the warning / assert report hooks.

void SetPrintWarning(void (*func)(char *))
{
    print_warning_func = func;
}

void SetPrintAssert(void (*func)(char *))
{
    print_assert_func = func;
}

// ──────────────────────────────────────────────────────────────────────
// Format the non-fatal warning message and dispatch it (or print it and carry
// on when no hook is installed).

void PrintWarningReal(const char *str, ...)
{
    va_list ap;
    char    buf[1000];

    va_start(ap, str);
    vsprintf(buf, str, ap);
    va_end(ap);

    if (print_warning_func == (void (*)(char *))nullptr)
    {
        printf("<<warning>>\n");
        printf("%s\n", buf);
    }
    else
    {
        (*print_warning_func)(buf);
    }
}

// ──────────────────────────────────────────────────────────────────────
// Stamp the location banner into g_cComment.  PRINT_ASSERT calls this first,
// then PrintAssertReal appends the message body.

void SetAssertPreMessage(const char *cComment, ...)
{
    va_list ap;

    va_start(ap, cComment);
    vsprintf(g_cComment, cComment, ap);
    va_end(ap);
}

// ──────────────────────────────────────────────────────────────────────
// Append the assert body (with its own printf arguments) to the banner and
// dispatch the whole line.  With no assert hook installed this is a hard stop:
// print the line and spin forever.

void PrintAssertReal(const char *str, ...)
{
    va_list ap;
    char    buf[2000];

    strcpy(buf, g_cComment);

    va_start(ap, str);
    vsprintf(buf + (int)strlen(buf), str, ap);
    va_end(ap);

    if (print_assert_func == (void (*)(char *))nullptr)
    {
        printf("<<assert>>\n");
        printf("%s\n", buf);
        for (;;)
        {
        }
    }

    (*print_assert_func)(buf);
}

// ──────────────────────────────────────────────────────────────────────
// Lower-case a string in place, returning it.  Used by fod.c to normalise the
// FOD light names before prefix matching.
//
// The ROM tests each byte against newlib's _ctype_ table (the _U bit) and adds
// ' ' branchlessly with movn -- i.e. isupper().  It indexes the table with a
// *sign-extended* char, so a byte >= 0x80 reads just below the table; this port
// masks to unsigned char instead, which is identical for the ASCII names the
// function is ever handed and avoids the out-of-range isupper() argument.

char *StrToLower(char *str)                                             /* 280 */
{
    char *p;

    p = str;                                                            /* 282 */
    while (*p != '\0')                                                  /* 283 */
    {
        if (isupper((u_char)*p))                                        /* 284 */
        {
            *p = (char)(*p + ' ');
        }
        p++;                                                            /* 285 */
    }

    return str;                                                         /* 287 */
}

// ──────────────────────────────────────────────────────────────────────
// Fold a rotation back into (-PI, PI].  Single-step, not a modulo: a value
// more than one turn out of range comes back still out of range.  The ROM's
// PI is 3.1415925f, not the libm constant.

float RotLimitChk2(float rot)
{
    if (rot > UTIL2_PI)                                                 /* 304 */
    {
        return rot - UTIL2_PI2;                                         /* 305 */
    }

    if (rot < -UTIL2_PI)                                                /* 306 */
    {
        return rot + UTIL2_PI2;                                         /* 307 */
    }

    return rot;                                                         /* 310 */
}
