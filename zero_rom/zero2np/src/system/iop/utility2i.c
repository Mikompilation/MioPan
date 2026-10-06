/* ==========================================================================
 *  system/iop/utility2i.c
 *
 *  The IOP's copy of common/utility2.c.  The IOP cannot call into EE code, so
 *  the handful of helpers iopsys.irx needs are compiled in again here; the
 *  bodies match the EE's, including their quirks.
 *
 *  The reporting pair at the bottom is the IOP end of the same hook design the
 *  EE uses: SetAssertPreMessage() leaves the location banner in g_cComment and
 *  the next PrintAssertReal() prefixes its own message with it.  With no hook
 *  installed an assert prints and then spins for ever, which on the IOP means
 *  the sound server stops answering rather than the machine resetting.
 *
 *  Reconstructed from iopsys.irx (Feb 6 2004 prototype, SLES_523.84).
 *  No source line numbers are available for this module, so there are no
 *  trailing ROM-line annotations.
 * ======================================================================== */

#include <stdarg.h>

#include "utility2i.h"

#include <stdio.h>                  /* printf                              */
#include <sysclib.h>                /* tolower / strcpy / strlen / vsprintf */

static void (*print_warning_func)(char *) = 0;                               /* data 320 */
static void (*print_assert_func)(char *)  = 0;                               /* data 324 */
static char   g_cComment[300];                                               /* bss 1fc0 */

/* Both hooks sit in .data rather than .bss, so the ROM initialised them
 * explicitly rather than letting the loader zero them. */

/* --------------------------------------------------------------------------
 *  Alignment
 * ------------------------------------------------------------------------ */

/* ROM ODDITY, reproduced: the guard is `num < 1`, so any positive argument
 * returns 0 rather than ceil(log2(num)).  The EE's copy in common/utility2.c
 * has exactly the same shape -- two independent builds agree, so this is the
 * source and not a misread. */
int Get2Power(int num)
{
    int power;
    int cmp;

    power = 0;
    cmp   = 1;

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

/* PORT: widened from `unsigned int` to pointer width.  Callers align host
 * addresses with this, and on a 64-bit host truncating one to 32 bits produces
 * a pointer that is not merely wrong but unmapped. */
uintptr_t GetAlignUp(uintptr_t a, int power)
{
    uintptr_t mask = ~((uintptr_t)-1 << power);

    return ((a + mask) >> power) << power;
}

/* --------------------------------------------------------------------------
 *  Strings
 * ------------------------------------------------------------------------ */

int GetOffsetChar(char *string, char chr)
{
    char *ptr  = string;
    int   ofst = 0;

    while (ptr[ofst] != '\0')
    {
        if (ptr[ofst] == chr)
            return ofst;

        ofst++;
    }

    return -1;
}

int GetStrLen(char *str)
{
    int i = 0;

    while (str[i] != '\0')
        i++;

    return i;
}

/* Walks back from `len - 1`.  Note it also stops on a NUL, so a `len` past the
 * terminator finds nothing rather than scanning whatever follows. */
int GetOffsetLastChar(char *string, int len, char chr)
{
    int i = len - 1;

    while (string[i] != chr)
    {
        if (string[i] == '\0')
            return -1;

        i--;
        if (i < 0)
            return -1;
    }

    return i;
}

int FileExtensionMatch(char *filename, char *c_extent)
{
    char *ptr;
    char *ex;
    int   ofst;

    ofst = GetOffsetLastChar(filename, GetStrLen(filename), '.');
    if (ofst < 0)
        return 0;

    /* +1 steps past the dot; c_extent is given without one. */
    ptr = filename + ofst + 1;
    ex  = c_extent;

    while (*ptr == *ex)
    {
        if (*ptr == '\0')
            return 1;

        ptr++;
        ex++;
    }

    return 0;
}

/* The extension as a packed int rather than a string, so callers can compare
 * it against a four-character literal in one instruction. */
int GetExtension(char *filename)
{
    int   ofst;
    char *ptr;

    ofst = GetOffsetChar(filename, '.');
    if (ofst < 0)
        return 0;

    ptr = filename + ofst + 1;

    return *(int *)ptr;
}

char *StrToLower(char *str)
{
    char *strp = str;

    while (*strp != '\0')
    {
        *strp = (char)tolower((int)*strp);
        strp++;
    }

    return str;
}

/* NOTE: there is no lower bound on the walk, so a path with no '/' in it runs
 * off the front of the buffer.  Every caller passes a "cdrom0:\\..." style
 * path, which always has one. */
char *GetNameWithoutPath(char *path_file)
{
    int len = strlen(path_file);

    while (path_file[len] != '/')
        len--;

    return &path_file[len + 1];
}

/* --------------------------------------------------------------------------
 *  Ring-buffer index arithmetic
 * ------------------------------------------------------------------------ */

int RingBufAdd(int idx, int buf_num)
{
    if (idx + 1 < buf_num)
        return idx + 1;

    return 0;
}

int RingBufCalcDiff(int former, int later, int num)
{
    if (former < later)
        return num - (later - former);

    return former - later;
}

/* --------------------------------------------------------------------------
 *  Clamps
 * ------------------------------------------------------------------------ */

float GetClampValF(float val, float upper, float lower)
{
    if (val > upper)
        return upper;

    if (val < lower)
        return lower;

    return val;
}

int GetClampVal(int val, int upper, int lower)
{
    if (val > upper)
        return upper;

    if (val < lower)
        return lower;

    return val;
}

/* 0x40490fdb / 0x40c90fdb -- the correctly rounded floats.  The EE's copy uses
 * 3.1415925f / 6.283185f, which are one ulp lower; the two builds' compilers
 * disagreed, so these are spelled from this module's own words. */
float RotLimitChk2(float rot)
{
    if (rot > 3.1415927f)
        return rot - 6.2831855f;

    if (rot < -3.1415927f)
        return rot + 6.2831855f;

    return rot;
}

/* --------------------------------------------------------------------------
 *  Reporting
 * ------------------------------------------------------------------------ */

void SetPrintWarning(void (*func)(char *))
{
    print_warning_func = func;
}

void SetPrintAssert(void (*func)(char *))
{
    print_assert_func = func;
}

void PrintWarningReal(char *str, ...)
{
    char    buf[1000];
    va_list ap;

    va_start(ap, str);
    vsprintf(buf, str, ap);
    va_end(ap);

    if (print_warning_func == 0)
    {
        printf("<<warning>>\n");
        printf("%s\n", buf);
    }
    else
    {
        print_warning_func(buf);
    }
}

void SetAssertPreMessage(char *cComment, ...)
{
    va_list ap;

    va_start(ap, cComment);
    vsprintf(g_cComment, cComment, ap);
    va_end(ap);
}

/* The banner left by SetAssertPreMessage() is copied in first and the caller's
 * message appended, so the two arrive as one line. */
void PrintAssertReal(char *str, ...)
{
    char    buf[2000];
    va_list ap;

    strcpy(buf, g_cComment);

    va_start(ap, str);
    vsprintf(buf + strlen(buf), str, ap);
    va_end(ap);

    if (print_assert_func != 0)
    {
        print_assert_func(buf);
        return;
    }

    printf("<<assert>>\n");
    printf("%s\n", buf);

    for (;;)
        ;
}

/* --------------------------------------------------------------------------
 *  Debug dumps
 * ------------------------------------------------------------------------ */

void DumpMemReal(unsigned int adrs, int size, char *name)
{
    unsigned char *ptr;
    int            i;
    int            count;

    printf("DUMP<%s>  ADRS<%x>\n", name, adrs);

    ptr   = (unsigned char *)adrs;
    count = 0;

    for (i = 0; i < size; i++)
    {
        count++;
        printf("%02x ", ptr[i]);

        /* The address printed is the start of the line just finished. */
        if (count > 15)
        {
            printf("\t0x%x\n", &ptr[i] - 15);
            count = 0;
        }
    }
}

/* Least significant bit first, so the output reads right-to-left against the
 * usual hex notation. */
void PrintBit(int iInt)
{
    int i;

    printf("\t");

    for (i = 0; i < 32; i++)
        printf((iInt >> i) & 1 ? "1" : "0");

    printf("\n");
}
