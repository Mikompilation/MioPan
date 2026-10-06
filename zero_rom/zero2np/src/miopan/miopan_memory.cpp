#include "miopan_memory.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MIOPAN_PS2_RAM_BASE       0x00400000u
/* One extra megabyte above the 80MB the game maps: effect_sub.c's
 * InitEffectSub() parks the Z-buffer stash at devkit address 0x5000000
 * (0x46000 bytes, LocalCopyZtoBZ/BZtoZ), which the prototype could only have
 * run with TOOL memory present. */
#define MIOPAN_PS2_RAM_END        0x05100000u
#define MIOPAN_PS2_RAM_SIZE       (MIOPAN_PS2_RAM_END - MIOPAN_PS2_RAM_BASE)
#define MIOPAN_SCRATCHPAD_BASE    0x70000000u
#define MIOPAN_SCRATCHPAD_SIZE    0x00004000u

static unsigned char *ps2_fallback_ram;
static unsigned char  ps2_scratchpad[MIOPAN_SCRATCHPAD_SIZE];
static int            ps2_memory_initialized;

static uintptr_t MioPan_SanitizePs2Address(uintptr_t address)
{
    return address & ~(uintptr_t)0x30000000u;
}

/* True when [p, p+size) overlaps the PS2 address window.  A block that
 * overlaps would make translated host pointers indistinguishable from raw PS2
 * addresses, and MioPan_TryGetHostPointer would translate them a second time. */
static int MioPan_BlockOverlapsPs2Window(const unsigned char *p)
{
    uintptr_t lo = (uintptr_t)p;
    uintptr_t hi = lo + MIOPAN_PS2_RAM_SIZE;

    return lo < MIOPAN_PS2_RAM_END && hi > MIOPAN_PS2_RAM_BASE;
}

void MioPan_InitPs2Memory(void)
{
    // Back the EE address space with an ordinary allocation rather than trying
    // to reserve the literal 0x00400000..0x05000000 window -- reserving a
    // hardcoded range is unreliable, because the CRT heap takes pages inside it
    // before we ever run.
    //
    // The one hard requirement is that the block must NOT overlap the PS2
    // address window.  Addresses reaching MioPan_TryGetHostPointer are a mix of
    // raw memory-map constants (which need translating) and already-translated
    // heap pointers (which do not), and the only way to tell them apart is the
    // range check.  If the block overlapped, translated pointers would look
    // like PS2 addresses and get translated twice.
    if (ps2_memory_initialized == 0)
    {
        unsigned char *reject[8];
        int            rejected = 0;
        int            i;

        for (;;)
        {
            ps2_fallback_ram = (unsigned char *)calloc(1, MIOPAN_PS2_RAM_SIZE);

            if (ps2_fallback_ram == 0 ||
                MioPan_BlockOverlapsPs2Window(ps2_fallback_ram) == 0)
            {
                break;
            }

            // Landed on top of the PS2 window; hold it aside and retry so the
            // allocator hands us a different region, then release the rejects.
            if (rejected >= (int)(sizeof(reject) / sizeof(reject[0])))
            {
                printf("MioPan PS2 RAM: could not place block outside "
                       "0x%08x..0x%08x -- address translation is unreliable\n",
                       MIOPAN_PS2_RAM_BASE, MIOPAN_PS2_RAM_END);
                break;
            }
            reject[rejected++] = ps2_fallback_ram;
        }

        for (i = 0; i < rejected; i++)
        {
            free(reject[i]);
        }

        if (ps2_fallback_ram == 0)
        {
            printf("MioPan PS2 RAM allocation FAILED (%u bytes)\n",
                   (unsigned int)MIOPAN_PS2_RAM_SIZE);
        }
        else
        {
            printf("MioPan PS2 RAM: %u bytes at %p (EE 0x%08x..0x%08x)\n",
                   (unsigned int)MIOPAN_PS2_RAM_SIZE, (void *)ps2_fallback_ram,
                   MIOPAN_PS2_RAM_BASE, MIOPAN_PS2_RAM_END);
        }

        ps2_memory_initialized = 1;
    }
    else if (ps2_fallback_ram != 0)
    {
        // Re-entry (InitSystemON runs after boot): wipe rather than realloc, so
        // existing translated pointers stay valid.
        memset(ps2_fallback_ram, 0, MIOPAN_PS2_RAM_SIZE);
    }

    memset(ps2_scratchpad, 0, sizeof(ps2_scratchpad));
}

int MioPan_IsPs2Address(uintptr_t address)
{
    uintptr_t clean;

    clean = MioPan_SanitizePs2Address(address);
    if (clean >= MIOPAN_PS2_RAM_BASE && clean < MIOPAN_PS2_RAM_END)
    {
        return 1;
    }

    if (address >= MIOPAN_SCRATCHPAD_BASE &&
        address < MIOPAN_SCRATCHPAD_BASE + MIOPAN_SCRATCHPAD_SIZE)
    {
        return 1;
    }

    return 0;
}

int MioPan_TryGetHostPointer(uintptr_t address, void **host_ptr)
{
    uintptr_t clean;

    if (host_ptr == 0)
    {
        return 0;
    }

    if (address == 0)
    {
        *host_ptr = 0;
        return 1;
    }

    if (address >= MIOPAN_SCRATCHPAD_BASE &&
        address < MIOPAN_SCRATCHPAD_BASE + MIOPAN_SCRATCHPAD_SIZE)
    {
        *host_ptr = ps2_scratchpad + (address - MIOPAN_SCRATCHPAD_BASE);
        return 1;
    }

    // Already a host pointer into the emulated RAM (the game heaps translate
    // their base once at init, so everything they hand out is host memory).
    // This must be tested BEFORE the PS2-window check: SanitizePs2Address
    // clears the cache-mode bits 0x30000000, so a host pointer such as
    // 0x10800000 would otherwise sanitize to 0x00800000, look like a PS2
    // address, and get translated a second time.
    if (ps2_fallback_ram != 0 &&
        address >= (uintptr_t)ps2_fallback_ram &&
        address < (uintptr_t)ps2_fallback_ram + MIOPAN_PS2_RAM_SIZE)
    {
        *host_ptr = (void *)address;
        return 1;
    }

    clean = MioPan_SanitizePs2Address(address);
    if (clean < MIOPAN_PS2_RAM_BASE || clean >= MIOPAN_PS2_RAM_END)
    {
        return 0;
    }

    if (ps2_memory_initialized == 0)
    {
        MioPan_InitPs2Memory();
    }

    if (ps2_fallback_ram == 0)
    {
        return 0;
    }

    *host_ptr = ps2_fallback_ram + (clean - MIOPAN_PS2_RAM_BASE);
    return 1;
}

void *MioPan_GetHostPointer(uintptr_t address)
{
    void *host_ptr;

    if (MioPan_TryGetHostPointer(address, &host_ptr) != 0)
    {
        return host_ptr;
    }

    return (void *)address;
}

// The inverse of MioPan_TryGetHostPointer, and the reason it exists: the
// emulated EE RAM is an ordinary calloc() block, so its base is different in
// every process.  A host pointer into it is therefore meaningless once it
// leaves the process -- which is exactly what happens when a memory-card save
// block stores one (see mc_set_data.c's pointer fixups).  Converting back to
// the EE address the PS2 would have held makes the value stable across runs
// and makes the save file hold what the ROM's file held.
//
// Returns 0 for a pointer that is not inside the emulated RAM or scratchpad;
// callers treat that as "not a translatable pointer" and leave it alone.
uintptr_t MioPan_GetPs2Address(const void *host_ptr)
{
    uintptr_t p = (uintptr_t)host_ptr;

    if (host_ptr == 0)
    {
        return 0;
    }

    if (ps2_fallback_ram != 0 &&
        p >= (uintptr_t)ps2_fallback_ram &&
        p < (uintptr_t)ps2_fallback_ram + MIOPAN_PS2_RAM_SIZE)
    {
        return MIOPAN_PS2_RAM_BASE + (uintptr_t)(p - (uintptr_t)ps2_fallback_ram);
    }

    if (p >= (uintptr_t)ps2_scratchpad &&
        p < (uintptr_t)ps2_scratchpad + MIOPAN_SCRATCHPAD_SIZE)
    {
        return MIOPAN_SCRATCHPAD_BASE + (uintptr_t)(p - (uintptr_t)ps2_scratchpad);
    }

    return 0;
}
