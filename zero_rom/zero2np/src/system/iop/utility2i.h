/* ==========================================================================
 *  system/iop/utility2i.h
 *
 *  The IOP's copy of common/utility2.c -- same helpers, compiled into
 *  iopsys.irx because the IOP cannot call the EE's.  Most bodies are
 *  identical to the EE side; where they differ it is noted at the definition.
 *
 *  Reconstructed from iopsys.irx (Feb 6 2004 prototype, SLES_523.84).
 * ======================================================================== */

#ifndef _SYSTEM_IOP_UTILITY2I_H
#define _SYSTEM_IOP_UTILITY2I_H

#include <stdint.h>

/* --------------------------------------------------------------------------
 *  PORT NOTE: one process, two copies
 *
 *  The PS2 had this file compiled twice -- once into the EE binary as
 *  common/utility2.c, once into iopsys.irx as this -- because the two CPUs
 *  could not call each other.  On the host they are one program, so ten of
 *  these names would be defined twice and the link fails.
 *
 *  The ten are renamed here rather than in utility2i.c, so both
 *  reconstructions stay byte-for-byte faithful and the split is reversible:
 *  drop this block and the ROM's own spelling comes back.  Every caller
 *  includes this header, so the renaming reaches all of them.
 *
 *  The bodies were reconstructed independently and differ only in style --
 *  `tolower()` against `isupper() + ' '`, named constants against literals,
 *  a defensive `& 0x1f` on a shift count.  They are the same functions, so
 *  collapsing the two copies onto common/utility2.c later would be safe; that
 *  needs the eleven helpers only this copy has (RingBufAdd, GetStrLen, ...)
 *  reconstructed on the EE side first.
 * ------------------------------------------------------------------------ */
#define Get2Power           IopGet2Power
#define GetAlignUp          IopGetAlignUp
#define GetClampValF        IopGetClampValF
#define PrintAssertReal     IopPrintAssertReal
#define PrintWarningReal    IopPrintWarningReal
#define RotLimitChk2        IopRotLimitChk2
#define SetAssertPreMessage IopSetAssertPreMessage
#define SetPrintAssert      IopSetPrintAssert
#define SetPrintWarning     IopSetPrintWarning
#define StrToLower          IopStrToLower

/* ---- alignment --------------------------------------------------------- */
int          Get2Power(int num);
/* PORT: pointer-width, not `unsigned int` -- see the definition. */
uintptr_t    GetAlignUp(uintptr_t a, int power);

/* ---- strings ----------------------------------------------------------- */
/* Offset of the first `chr` in `string`, or -1. */
int   GetOffsetChar(char *string, char chr);
int   GetStrLen(char *str);
/* Offset of the last `chr` in the first `len` bytes, or -1. */
int   GetOffsetLastChar(char *string, int len, char chr);
/* 1 if `filename`'s extension matches `c_extent` (which excludes the dot). */
int   FileExtensionMatch(char *filename, char *c_extent);
/* The four bytes after the first '.', read as an int, or 0 if there is none. */
int   GetExtension(char *filename);
char *StrToLower(char *str);
/* Points past the last '/' -- see the note on the definition. */
char *GetNameWithoutPath(char *path_file);

/* ---- ring-buffer index arithmetic -------------------------------------- */
int   RingBufAdd(int idx, int buf_num);
/* Distance from `later` back to `former` around a `num`-slot ring. */
int   RingBufCalcDiff(int former, int later, int num);

/* ---- clamps ------------------------------------------------------------ */
/* Upper bound before the lower one, matching the ROM's argument order. */
float GetClampValF(float val, float upper, float lower);
int   GetClampVal(int val, int upper, int lower);

/* Fold a rotation back into (-PI, PI].  One step only. */
float RotLimitChk2(float rot);

/* ---- reporting --------------------------------------------------------- */
void  SetPrintWarning(void (*func)(char *));
void  SetPrintAssert(void (*func)(char *));
void  PrintWarningReal(char *str, ...);
/* Stamps the location banner into the module-static scratch buffer that the
 * next PrintAssertReal() prefixes its message with. */
void  SetAssertPreMessage(char *cComment, ...);
/* Never returns unless an assert hook is installed. */
void  PrintAssertReal(char *str, ...);

/* ---- debug dumps ------------------------------------------------------- */
void  DumpMemReal(unsigned int adrs, int size, char *name);
void  PrintBit(int iInt);

#endif /* _SYSTEM_IOP_UTILITY2I_H */
