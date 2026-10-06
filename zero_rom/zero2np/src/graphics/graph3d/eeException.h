/* ==========================================================================
 *  eeException.h
 *
 *  Public API for the EE/IOP CPU exception reporting subsystem.
 *
 *  Reconstructed from the Feb 6 2004 prototype (SLES_523.84).
 * ======================================================================== */
#ifndef _EEEXCEPTION_H
#define _EEEXCEPTION_H

#ifdef __cplusplus
extern "C" {
#endif

/* MIPS R5900 exception codes (Cause.ExcCode). */
typedef enum
{
    PS2E_INTERRUPT                          = 0,
    PS2E_TLB_MOD                            = 1,
    PS2E_TLB_LOAD_OR_IFETCH                 = 2,
    PS2E_TLB_STORE                          = 3,
    PS2E_ADDRESS_ERROR_LOAD_OR_IFETCH       = 4,
    PS2E_ADDRESS_ERROR_STORE                = 5,
    PS2E_BUS_ERROR_IFETCH                   = 6,
    PS2E_BUS_ERROR_DATAREF_LOAD_OR_STORE    = 7,
    PS2E_SYSCALL                            = 8,
    PS2E_BREAKPOINT                         = 9,
    PS2E_RESERVED_INSTRUCTION               = 10,
    PS2E_COP_UNUSABLE                       = 11,
    PS2E_ARITHMETIC_OVERFLOW                = 12,
    PS2E_TRAP                               = 13,
    NUM_PS2EXCEPTION                        = 14
} PS2EXCEPTION;

/* Parameters for iopexceptionInitialize. */
typedef struct                          /* 0x4 */
{
    char *pModuleName;                  /* 0x0 */
} IOPEXCEPTIONCREATIONDATA;

void eeexceptionInitialize(void);
void eeexceptionEnableExcCode(PS2EXCEPTION eee, int bEnable);
void eeexceptionEnableExcCodeAll(int bEnable);
void iopexceptionInitialize(IOPEXCEPTIONCREATIONDATA *pCD);

#ifdef __cplusplus
}
#endif

#endif /* _EEEXCEPTION_H */
