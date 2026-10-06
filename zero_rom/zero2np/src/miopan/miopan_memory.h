#ifndef MIOPAN_MEMORY_H
#define MIOPAN_MEMORY_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void  MioPan_InitPs2Memory(void);
int   MioPan_TryGetHostPointer(uintptr_t address, void **host_ptr);
void *MioPan_GetHostPointer(uintptr_t address);
int   MioPan_IsPs2Address(uintptr_t address);

/* Inverse of MioPan_GetHostPointer: host pointer -> EE address, 0 if the
 * pointer is not inside the emulated RAM.  Needed by anything that has to
 * persist a pointer outside the process -- see the save-block fixups in
 * system/mc/prg/mc_set_data.c. */
uintptr_t MioPan_GetPs2Address(const void *host_ptr);

#ifdef __cplusplus
}
#endif

#endif /* MIOPAN_MEMORY_H */
