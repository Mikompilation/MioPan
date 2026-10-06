---
name: miopan-reversing
description: Everything needed to reverse-engineer Fatal Frame II (Crimson Butterfly) PS2 prototype functions into the MioPan PC port — Ghidra HTTP API access, build commands, PS2/EE conventions, renderer architecture, and the hazards that waste hours. Use this whenever work touches the MioPan repo: reconstructing or fixing a function from the ROM, diagnosing rendering or animation problems, building the project, querying Ghidra for decompilation, or interpreting SGD/MOTN/MIME data. Also use when the user mentions Ghidra, gra3dSGD, motion.c, SGDSELF32, VU0, a stubbed function, or asks why something does not render — even if they do not name this skill.
---

# Reversing Fatal Frame II into MioPan

MioPan is a PC port of **Fatal Frame II (Crimson Butterfly), Feb 6 2004 PS2 prototype
(`SLES_523.84`)**, reconstructed function-by-function from Ghidra output. A prior AI
("Codex") produced hallucinated reconstructions, so **treat any existing code as suspect
until checked against the ROM**. Accuracy to the original matters more than elegance:
this is a matching decompilation, not a rewrite.

Reference port of Fatal Frame 1:
[MikuPan](https://github.com/Mikompilation/MikuPan) (useful for renderer-splitting ideas, not authority).

## Ghidra HTTP API

Ghidra runs an HTTP server (GhidraMCP) on **port 8089** here. The `mcp__ghidra__*` tools
have never loaded reliably, so drive it over HTTP instead.

**The parameter is `address`, not `name`.** Passing `name=` returns
`{"error":"Address or function name is required"}` for every endpoint, which reads like
the server is broken when it is only rejecting the key. This costs a lot of time if you
assume the server is down.

```python
import urllib.request, urllib.parse
def ghidra(ep, **kw):
    u = "http://127.0.0.1:8089/" + ep + "?" + urllib.parse.urlencode(kw)
    return urllib.request.urlopen(u, timeout=90).read().decode("utf-8", "replace")

print(ghidra("decompile_function", address="0x001e4410"))
```

Endpoints: `decompile_function`, `disassemble_function`, `get_xrefs_to`. Some other
endpoint names 404 — do not conclude the server is dead from one 404.

Get addresses from the repo-root artifacts (see below) rather than guessing.

### Two things about Ghidra output

**Variadic arguments are silently dropped.** `printf`, `sprintf`, `PRINT_ASSERT`,
`G3DASSERT`, and `SetEffects` will appear with too few arguments. The format string and
its arguments are still in the disassembly — recover them from there, and never trust a
decompiled call's arity for these.

**`disassemble_function` carries original source line numbers** as `; Line NNN`
comments. These are gold for ordering statements and for confirming that an inline
really was inlined at a given point. Prefer it over the decompiler when reconstructing
statement order.

## Repo-root artifacts

Generated dumps at the repo root, all searchable with Grep:

| File | Contents |
|---|---|
| `functions.txt` | Every function's signature **and local variable names/registers** — but **no bodies**. Use it for names, types, addresses, and locals; go to Ghidra for logic. |
| `globals.txt` | Global variables with addresses |
| `types.txt` | Struct/enum definitions |
| `symbols.txt` | Symbol table incl. mangled names and sizes |
| `ZERO2.MAP` | Link map |
| `SLES_523.84`, `IMG_BD.BIN` | The game's executable and data archive — **not distributed**; dump them from your own disc |

`functions.txt` local-variable lists are genuinely useful: they tell you how many
locals the original had and their types, which constrains your reconstruction. If your
version needs a variable the original did not have, you probably inlined something the
original called, or vice versa.

## Reconstructing a whole module

The repeatable recipe, in order. Every step is cheap and each one has caught a real
error that the next step would have propagated.

**1. Get the symbol list from `ZERO2.MAP`, not from guessing.** Find the module's
`.text` block and read the symbols under it; the same grep gives you `.data` / `.bss` /
`.sdata` / `.rodata` addresses and sizes, which tell you how much static state to expect.

```bash
L=$(grep -n "^ .text.*MapHit.o" ZERO2.MAP | cut -d: -f1); sed -n "${L},$((L+30))p" ZERO2.MAP
grep -n "MapHit.o" ZERO2.MAP | grep -E "\.data|\.bss|\.sdata|\.sbss|rodata"
```

**2. `functions.txt` for locals and statics.** Its per-file section lists every function
including the `static` ones that `ZERO2.MAP` omits, with local variable names and types.
That constrains your reconstruction: if you need a variable the original did not have,
you inlined something it called, or vice versa.

**3. `globals.txt` for the file's statics** — names, types and addresses, sectioned by
source file. **Read the declared type, not just the name.** A recurring mistake is
flattening `fixed_array<T,N>` into a plain `T[N]`:

```
/* bss 400b00 */ static fixed_array<MAPHIT_HEAD,32> MapHitRecList;   <- container
/* bss 400d90 */ static float MapHitDoorVec[2][4][4];                <- plain array
```

`fixed_array<T,N>` (`graphics/graph3d/ctl/fixed_array.h`) is same-size and same-layout
as `T[N]`, so flattening it builds and runs — but it drops the bounds check, and the
declaration no longer matches the ROM. The tell in the decompiler is unmistakable:
`_fixed_array_verifyrange<T>(i, N)` inlined at every subscript, and a `static`
`_fixed_array_assert` copy at the top of the object file. If you see those, it is a
`fixed_array`.

It is a near drop-in — `operator[]` returns a reference, so `&List[i]` and `List[i].x`
are unchanged — but `m_aData` is protected, so anything reaching for the raw storage
needs `.data()`. Include the header explicitly rather than relying on it arriving
transitively.

The same applies to struct *members*: `MLOAD_HEAD::reg_id` is `fixed_array<char,4>`, not
`char[4]`. Check `types.txt` for the member's declared type. Beware of grepping a window
around a struct — the `fixed_array<T,N>` template instantiations sit next to the
typedefs and read like false positives.

**4. Read `.data` initialisers straight out of the ELF.** Ghidra will not show you the
table contents. Parse the program headers and translate VA→file offset. This is how
`FurnTbl`, `MapHitPoint` and the rotation `lit4` constants were recovered.

**5. Survey dependencies before committing.** Extract every `jal` target, map the
addresses to names via `ZERO2.MAP`, then check which already exist in the repo. This is
what sized MapObjReg honestly (93 calls, 32 absent across 7 modules) instead of
discovering it halfway through.

**6. Decompile in batches, pulling line ranges alongside.** `sorted(set(re.findall(r'; Line (\d+)', disasm)))`
gives the function's source-line span, which orders the file and feeds the
`/* NNN */` annotations.

**7. Verify completeness against `ZERO2.MAP`** before claiming the module is done:

```python
start = [i for i,l in enumerate(lines) if l.startswith(' .text') and 'FurnCtl.o' in l][0]
rom = {m.group(1) for line in lines[start+1:]
       if (m := re.match(r'\s+0x[0-9a-f]{16}\s+(\w+)\(', line))}
missing = [f for f in rom if not re.search(r'\b'+f+r'\s*\(', src)]
```

Slice the map by locating the `.text` line — a hardcoded line range silently grabs the
neighbouring object's symbols and reports nonsense.

### Verify struct layouts, do not eyeball them

For any reconstructed struct, compile a throwaway `offsetof`/`sizeof` harness against the
header and compare with the ROM sizes from `types.txt`. This caught `CCenterCircle`
coming out `0x28` against the ROM's `0x24`.

**Bitfield storage units are the usual culprit.** `types.txt` reports the declared type,
but GCC 2.96-ee and the host compiler allocate differently. `CCenterCircle`'s flags are
typed `unsigned int` there, which on the host pushes the unit past the byte the ROM used
and inflates the struct; `u_char` reproduces both facts (flags in byte `0x21`, `sizeof`
`0x24`). Match the *offsets and size*, and say in a comment why the declared type was
not used. Where the ROM's tail padding came from an alignment you no longer have, spell
the padding out explicitly.

Host offsets legitimately drift from the ROM's wherever a struct holds pointers (4 bytes
on target, 8 here). Say so once in the header rather than per-member, and treat the
offset comments as documentation.

## Building

```bash
# MinGW (CLion's bundled one works) and ninja; read the real paths out of CMakeCache.txt
export PATH="/path/to/mingw/bin:/path/to/ninja:$PATH"
cd <repo root>
ninja -C cmake-build-relwithdebinfo MioPan > /tmp/b.log 2>&1
echo "exit=$?"; grep -oE "error: .*|undefined reference to .*" /tmp/b.log | sort -u | head
```

**Without the PATH export, `cc1plus` fails with exit 1 and zero diagnostics.** If a
build fails with no error text at all, this is why. `ninja: command not found`
(exit 127) is the same problem one step earlier.

**Do not guess the toolchain path — read it out of the build directory.** `grep -E "CMAKE_MAKE_PROGRAM|CMAKE_CXX_COMPILER:" cmake-build-relwithdebinfo/CMakeCache.txt`
names both the compiler and ninja in one command, and takes less time than
discovering the path is stale.

Three build dirs exist:
- `cmake-build-debug` — `-Og` with `_FORTIFY_SOURCE=3` (see below), or `-O0` when
  configured with `-DMIOPAN_DEBUG_FORTIFY=OFF`. Do not use it for performance work: cglm
  is header-only inline math, and at `-O0` the software vertex path runs roughly 5–10×
  slower.
- `cmake-build-relwithdebinfo` — `-O2`, keeps symbols. Prefer this.
- `cmake-build-release` — `-O3 -DNDEBUG`, no symbols.

`-fno-strict-aliasing` is not optional, and **it is now set in `CMakeLists.txt`** so
every build type and every fresh configure gets it. The codebase reinterprets `u_int *`
as struct pointers constantly (`SGDSELF32` resolution, DMA packet walks,
`GetFileInPak` casts). That is a strict-aliasing violation which GCC only exploits at
`-O2`+. If a release crash persists, try `-fsanitize=address` on a debug build.

**It was not always in `CMakeLists.txt`, and the way it went missing is worth
recognising.** It lived only as a hand-set `CMAKE_CXX_FLAGS` entry in
`cmake-build-relwithdebinfo/CMakeCache.txt`, so that one directory was correct and
`cmake-build-release`, configured separately, silently built without it. Nothing in the
tree said so — `git log -S` over `CMakeLists.txt` finds the flag was never there. The
symptom was a **release-only SIGSEGV in `gra3dsgdDrawPresetDataObject`**, which is
exactly where it should show: that walker re-reads `pPUHead->iCategory` and
`pPUHead->pNext` on every iteration of a chain `sgdRemap()` has just written through
`u_int *` (`vuvnprim = (u_int *)pPUHead`, `MappingCoordinateData((u_int *)pPUHead, …)`).

So: **when a bug is release-only, diff the build directories' caches before reading any
code.** `grep -E "CMAKE_(C|CXX)_FLAGS" */CMakeCache.txt` takes one command and names
every codegen difference between the build that works and the build that does not. Here
there were exactly two — the missing flag, and `-O3` against `-O2`.

**A Linux release build that dies with `*** buffer overflow detected ***: terminated` is
glibc's `_FORTIFY_SOURCE`, not a segfault.** Ubuntu's GCC turns it on by default at -O1
and up, and the nightly's `ubuntu-latest` runner uses that GCC. MinGW never fortifies, so
an overrun that Windows absorbs aborts there. The first was `_ClearMaterialData()`'s
0x150-byte memset into a 0x80-byte light cache, on the first room drawn. glibc checks
`mem*` calls against the whole object and `str*` calls against the member: the ROM's
`strcpy(icon.Head, "PS2D")` writes five bytes into four and aborted every save.

- **The GCC Debug build is fortified now** (`MIOPAN_DEBUG_FORTIFY`, on by default). That
  is why Debug builds at `-Og` rather than `-O0`: fortify does nothing unoptimised. It
  catches overruns visible at the call. An overrun routed through an out-of-line helper
  needs `-O2`'s inlining: `_ClearMaterialData()` only ever sees a `void *`. For the wider
  net, configure a RelWithDebInfo build with `-DCMAKE_CXX_FLAGS=-D_FORTIFY_SOURCE=3`.
- **Under gdb**, MinGW's `__chk_fail` raises exception 0xc0000409, reported as "Unknown
  signal" with the overrunning call on the stack. Do not link `libssp`: its `__chk_fail`
  replaces the CRT's with a silent trap.
- **To find candidates without running anything**, compile with
  `-D_FORTIFY_SOURCE=3 -O3` and grep the warnings for `__builtin___*_chk`.
- **An overrun into "the next static" lands somewhere different per build type.**
  MinGW's -O2/-O3 emit a file's statics in reverse declaration order, while -O0 keeps
  it. Pin the ROM's `.bss` order with a struct, as `gra3d.c`'s `s_Vu1MaterialBss` does.

**`ld.exe: cannot open output file MioPan\MioPan.exe: Permission denied` means the game
is still running.** It is not a code error. Ask the user to close it.

## Target conventions

- PS2 EE MIPS, GCC 2.96-ee. On target: `long`/`u_long` are **8 bytes**, pointers are
  **4 bytes**. Host is 64-bit, so anything storing a pointer in a 32-bit field needs
  care — a truncated pointer once showed up as `base_p == 0x20`.
- **Every `.c` file is compiled as C++.** You can use references, `nullptr`, and inline
  functions in `.c` files, and you will see them in existing code.
- `SGDSELF32<T>` — self-relative 32-bit offset, resolves as `(uintptr_t)this + value`.
  Reads resolve on every access, so no in-place fixup is needed even where the ROM
  overwrote the field with an absolute pointer. `SGDFILE32<T,off>` is file-base-relative.
- EE→host pointer translation: `MioPan_GetHostPointer` / `TryGetHostPointer`, idempotent
  by range check. EE window is `0x00400000..0x05000000`; the emulated RAM block is
  deliberately allocated outside it. `my_mallocInit` already translates every heap base —
  do not add translation there.

### Reporting macros — recognise them, do not transcribe them

Three macros in `common/utility2.h`, plus `G3DASSERT` in `g3ddbg.h`. Each expands to
*several* calls at the call site, so the decompiler shows you the expansion, not the
macro. Transcribing the expansion literally is wrong.

| ROM pattern at the call site | Macro |
|---|---|
| `printf("***ERR!! %s(%d):", file, line)` then `printf(fmt, ...)` | `PRINT_ERROR(fmt, ...)` |
| `SetAssertPreMessage(...)` then `PrintAssertReal(fmt, ...)` | `PRINT_ASSERT(fmt, ...)` |
| `printf("<<<<<<<<<WARNING ...")` then `PrintWarningReal(...)` | `PRINT_WARNING(fmt, ...)` |

`PRINT_ERROR` is by far the most common in the map code (40 sites across 9 files). The
banner takes the macro's own `__FILE__`/`__LINE__`, so a ROM line number recovered from
the disassembly belongs in a trailing `/* NNN */` comment, not hardcoded into the call.

`PRINT_ASSERT` and `G3DASSERT` carry no breakpoint: like the ROM's own pairs they report
and fall through, so a recoverable path keeps going. (A `__debugbreak()` there once broke
the Linux build and was removed; don't add one back.)

### VU0 macro-mode idioms

Inline COP2 asm in `g3dxVu0.h` follows recognisable patterns. When the original has
inlined VU0 that Ghidra shows as `/* inlined from ../graph3d/g3dxVu0.h */`, match the
idiom rather than inventing a helper:

| Pattern | Meaning |
|---|---|
| `vsqrt` / `vwaitq` / `vaddq(vf0, Q)` | scalar square root |
| `vrsqrt` + `vmulq` | normalize |
| `vaddabc` / `vmaddbc` | xyz dot product |
| `lq` / `sq` | quadword copy |
| `vmulbc` | broadcast scalar multiply |

Functions with no implementation in `libvu0` were often **inlined asm in `g3dxVu0.h`**
that got misfiled. The fix is to write the inline in `g3dxVu0.h`, not to add a body to
`vu0_lib.cpp`. `libvu0.h` signatures should match `vu0_lib.cpp` (destination first,
scalars last) — that file came from a matching decomp and is the authority.

**cglm is column-major.** `glm_mat4_mul(a, b, dest)` is equivalent to row-major
`dest = b * a`. So `sceVu0MulMatrix(m0, m1, m2)` yields `m0 = m2 * m1`. Do not "fix"
matrix multiply order without checking this — it has caused a regression already.

## Renderer architecture

The PS2 path builds DMA packets for the VU1; the host has no VU, so the reconstructed
code both walks the packets *and* calls host draw bridges.

```
_gra3dDrawSGD(pSGDTop, type, pCoord, pnum)      gra3dSGD.c
  └─ per block: apProcUnitHead[i]  (SGDSELF32 chain)
       ├─ SgSortUnitPrim(pk[i])          blocks 1 .. numblock-2
       └─ SgSortUnitPrimPost(pk[n-1])    last block
            └─ switch (pPUHead->iCategory)   0 VUVN, 1 mesh, 2 material,
                                             3 nop, 4 coord, 5 image
                 └─ SetVUMeshData / SetVUMeshDataPost / SetVUMeshDataP
                      └─ MioPan_Graph3dDraw{Preset,Runtime}Mesh[Post]   miopan_graph3d.cpp
                           ├─ preset:  FindResidentPresetUnit
                           │    └─ MioPan_RendererDrawResidentMesh      (geometry on the GPU)
                           └─ runtime, or a preset unit that declines:
                                MeshStreamScope → MioPan_RendererBeginMeshStream
                                                  (decoded and streamed every frame)
```

**Preset models are resident.** A room, furniture or door SGD is decoded once, on
its first draw, into one GPU mesh holding every unit in walk order; each later
draw of a unit references its slice, and consecutive units with identical state
are folded into one draw. Everything per-draw still happens per draw — the walker's
culling, TEX0/TEST scan, light image, colours. The model is retired through
`MioPan_Graph3dInvalidateSkinCache()`, which sgdRemap() and sgdRemapInverse()
call; gra3dChangeST() calls `MioPan_Graph3dNotifyUVChange()`, which drops only the
mesh. Nothing else writes preset positions or UVs. **If a room looks different,
toggle Renderer → Static meshes → "Keep geometry on the GPU" first** — off is the
old streamed path, the A/B.

**Model textures stop going through the GS.** The PS2 streamed every model's
textures into the shared 0x2bc0 window before every draw, and the port replayed
that. Now each model's textures are resolved once and its sends dropped:

- *Rooms, furniture, doors* (TRI2 units in block 0): `gra3dLoadTRI2FileToVRAM()`
  asks `MioPan_Graph3dTextureUploadBound()` before mirroring a TRI2, and reports
  `MioPan_Graph3dNoteTextureUpload()` after one that went through. The model's
  first draw after such an upload, with nothing else uploaded since, resolves
  every TEX0 its units carry (`ResolveResidentTexture()`); later sends are skipped
  and the draws get the textures directly. A skip unlatches `previous_tri2_prim`,
  because the guard assumes skipped texels are still in VRAM.
- *Characters, ghosts, items* (TIM2 packs): `SendEneVram()`, `SendEneVramMono()`
  and `SendItemVram()` bracket their sends with `MioPan_Graph3dBeginTim2Upload()` /
  `End`; a resolved pack's uploads are dropped inside the GS layer
  (`MioPan_GsSetUploadSuppressed`). `MpkAddTexOffset()` re-bases TEX0s around event
  scenes and retires the pack (`MioPan_Graph3dNotifyTex0Rebase`).

Both bind per colour mode (monotone uploads another CLUT set), key by TEX0 value
so the GS's TEX0 inheritance stays exact, and refuse — uploads resume — on any
TEX0 outside the model's own. Measured on the disc: every block opens on a unit
with its own TEX0, and no character or item mesh inherits one. The A/B is
Renderer → Static meshes → "Stop re-sending their textures".
`tools/renderer/resident_test.cpp` drives both halves against every model.

Key facts that are easy to get wrong:

- **`uiNumBlock` is the coordinate/bone count, not the part count.** For Mio it is 27
  (25 bones + root + terminator). Only some blocks carry geometry; the rest are
  legitimately `raw[0]` in the file. Do not treat null chain heads as a bug.
- **A model file is a pak.** File 0 is the body; files 1..N are sub-objects (hair,
  clothing, accessories), each a complete SGD drawn against the parent's coordinate
  array. `DrawGirlSubObj` / `DrawEneSubObj` iterate them. If most of a character is
  missing, check these first — a stub there means only file 0 ever reaches the renderer,
  and every measurement of file 0 will look perfectly correct.
- **TEX0 is GS register state, not per-primitive data.** A textured mesh whose packet
  carries no TEX0 inherits whatever was last set. `miopan_graph3d.cpp` keeps
  `g_last_tex0` / `RememberTex0` for this. Missing TEX0 does not mean untextured.
- Face culling is back-face, counter-clockwise front, on every depth-tested mesh
  pipeline (`CreatePipelineVariant()`). The PS2 culled in the VU1 microprograms with
  an alternating per-vertex sign; `AppendTriangleStripIndices()` reproduces that by
  swapping the first two corners of every odd strip triangle, so change the two
  together or not at all.

## Room load chain

Reconstructed this session end to end. Several modules interlock, and a break anywhere
leaves the rooms looking empty rather than erroring, so know the whole chain before
diagnosing a missing-furniture problem.

```
MapLoadInit                 → FurnCtlInit / FurnWorkInit / MapObjRegInit
MapLoad*                    → FurnLoadRegID(buff_id, reg_id, addr)
  ├─ CBuffInit(36, 512)
  ├─ walk regs types 7/3/11 → FurnCtlGetMdoelName → CBuffSetStr   (dedupe)
  └─ per distinct name      → FurnLoadOne  → LoadReqGetAddr       (bump cursor)
                            → FurnCtlRegist(buff_id, name, addr, attr, size)
MapObjRegistPhf(buff_id, lit_addr)
  └─ FurnCtlModelInit       → resolves each FURN_CTL's model / mot out of its file
MapObjRegistRegDatOne(buff_id, reg_id)
  └─ MapObjRegistDoor / MapObjRegistFurn / MapObjRegistPutFurn
       └─ MapObjAddDrawList → FurnCtlGetModelAddr → MapObjRegistModel → MapPutSetObj
```

Facts that are easy to get wrong:

- **A model name is `<2-char key><3 digits>`.** The key selects a base file number from
  `FurnTbl` in `FurnLoad.c`; the file is `base + (digits % 100)`. `f`-keys are spaced 100
  apart, `d`-keys packed tighter.
- **`_p` and `_ev` suffixes collapse onto the same file.** `FurnCtlGetMdoelName()` strips
  them and appends `.sgd`, which is what lets `CBuff` dedupe several placements into one
  load. `FurnCtl` lookups then compare only the **first four characters**.
- **An `eff_` prefix means an effect placeholder with no model file.**
  `FurnCtlGetType()` returns 0 for those — note the inverted sense.
- **Furniture attribute** (from `FurnTbl`): 1 cloth, 2 foliage, 4 water/light effect,
  5 bone/rope. It drives lighting (1 and 5 use the baked light), animation dispatch in
  `MapObjRegistMot()`, and whether `FurnCtlGetModelAddr()` hands out a private copy.
- **Registration record types**: 0 hit, 1/2 camera & event debug rects, 3 object,
  4 footstep SE, 6 room outline, 7 door, 8 battle camera, 9 four-corner camera,
  10 stairs, 11 put-item.
- **Two room buffers** exist so a door transition has both rooms resident. That is why
  `MapObjDeletDraw()` hands a door being walked through to the *other* buffer instead of
  dropping it.

### Debug view toggles

Already built into `miopan_renderer.cpp`, in `PumpEvents`:

- **F1** — wireframe (`SDL_GPU_FILLMODE_LINE`). Four pipeline variants are created up
  front (sprite/mesh × fill/line) and selected per draw, so nothing is recreated
  mid-frame while a command buffer may still reference it.
- **F2** — disable depth test/write. Routes mesh draws to the existing depth-off pipeline.
- **F3** — flat colour: swaps in the 1×1 white texture and replaces vertex colour with an
  opaque per-block palette indexed by `g_dbgBlockId`. Alpha 1.0 also defeats blending.

These three separate the remaining failure classes cheaply: F1 says whether geometry
exists, F2 whether it is occluded, F3 whether the texture is at fault.

## Diagnosing "it does not render"

Hard-won process lesson, worth following literally: **measure before theorising.** In one
session, five successive diagnoses of an invisible-character bug were each disproved by
the next measurement, because each was a plausible mechanism reasoned from code rather
than a fact read off the running program. The actual cause was a stubbed function
(`DrawGirlSubObj`) that meant most of the model never entered the pipeline at all.

Two specific traps that produced false "everything is correct" conclusions:

1. **Filtered probes.** A probe gated on `mag > 1.0e6f` silently excluded every
   model-local vertex population (magnitude ~50), so only the one working path was ever
   sampled. If a probe filters, state what it excludes.
2. **Global counters attributed to one object.** A tally of 60,000 meshes drawn was
   assumed to be the character's; it was mostly room geometry. Attribute per block or per
   model, never globally.

The probe that actually resolved it: per-block submission tally — triangle count plus
vertex bounding box plus transform, keyed by a block index threaded from the
`apProcUnitHead[]` dispatch loop into the renderer bridge. That one measurement
distinguishes parse failure (block absent), transform failure (bbox in the wrong place),
and shading failure (bbox correct but invisible) in a single run.

When adding probes: cap them (`static int dbg_x; if (dbg_x < N)`), include `__func__`
(there are several similarly-named draw functions and probes land in the wrong one), and
**strip them when done** — the user will notice if you do not.

## Hazards

**Heredoc backslash mangling — this has bitten repeatedly.** Inside a bash heredoc, `\n`
loses its backslash, so `printf("...\n")` becomes a literal newline and the compiler
reports `missing terminating " character`. Line continuations in macros (`\` at
end-of-line) are destroyed the same way, which silently collapses a multi-line `#define`
into one broken line.

For any content containing backslash escapes, **use the Write or Edit tool, not a shell
heredoc.** If you must script it, build the escape as `chr(92) + 'n'`.

**A stub for the function you are reconstructing may already exist in the wrong file.**
This has happened four times. `GetTrgtRot` was stubbed in `utility.c` but belongs to
`unit_ctl.o`; `DrawMapHitRect`/`One` sat in `map_hit_check.c` but belong to
`map_rectangle.o`; `MrecSetSEInfo`/`MrecGetSeNo` sat in `anicode.c`. You find out via a
**multiple-definition link error**, after the work is done. Grep for each symbol across
the repo *before* writing it, and when you find one, move it (leaving a breadcrumb
comment in the old file) rather than deleting the new work.

**Existing `/* NNN */` line annotations are frequently wrong.** They were guessed by the
earlier pass. In `map_rectangle.c` 26 of them were wrong — `MrecSetCameraInfo` was
marked 162 but is line 131, `MrecIsInRectangle` marked 1698 but is 1137. They exist to
make future matching easier, so a wrong one is worse than none. Measure them from
`disassemble_function` and fix them when you touch a file.

**Unpack the decompiler's comma-and-`&&` chains slowly.** Ghidra renders nested
early-outs as `(iVar2 = -3, cond) && (iVar2 = x, cond2)`, where each assignment is the
value that survives if the *next* test fails. Read them as a ladder of `if`s. A first
reading of `FurnCtlCheckFileType` inverted its final condition.

**A `MB_OUT_SECTION *` parameter indexed as `reg_p + N` is really an `MDAT_*` record.**
The header is 8 bytes, so `reg_p + 6` is byte `0x30` and `&reg_p[7].labelID` is `0x3c`.
Cast to the real record type and use named fields; the offsets all line up with
`MDAT_OBJ` / `MDAT_DOOR` / `MDAT_PUT` and the result is readable.

**Repeated `lit4` constants are one-per-inline-expansion, not distinct values.**
`MapObjReg` referenced five different `.lit4` addresses that all held the same
degrees→radians constant, one per expansion of the same inlined helper. Read them out of
the ELF rather than assuming they differ — or that they match.

**Verify before claiming.** Several confident claims turned out wrong: that a function
had no callers (grepped the wrong symbol), that a table was still zeroed (it had been
populated), that a family of accessors was unimplemented (they were done — the `(void)`
cast heuristic was measuring the wrong thing). Check function *bodies*, not proxies.

**Bulk edit scripts need assertions.** A script that reorders or replaces call sites
should `assert` its anchors and report a count. One unasserted `str.replace` silently
did nothing and the same bug was "fixed" twice; another over-matched and deleted an
entire anonymous namespace of helper functions. When a scripted edit truncates a file,
`git show HEAD:<path>` to recover, then redo with tighter anchors.

**Before scripted deletion, know what is inside the span.** Removing "from marker A to
the end of function B" can swallow unrelated code that happens to sit between them.

## Style

Match the surrounding code: same comment density, naming, and idiom. Comments should
explain *why* — especially where the port necessarily deviates from the ROM, or where a
PS2 behaviour (GS state, DMA ordering, VU semantics) is being emulated. Where the ROM's
line numbers are known, keep them as `/* NNN */` trailing comments; that convention is
already used and makes future matching much easier. Japanese assert strings from the ROM
are preserved with an English gloss.

Preserved-original blocks appear as `#if 0` sections with ROM line numbers — these are
authoritative for statement order and are worth checking before asking Ghidra.

## Known open items


- `utility2.h` uses `__VA_OPT__`, which MSVC only honours under `/Zc:preprocessor` in
  C++20 mode. `CMAKE_CXX_STANDARD_REQUIRED` is `OFF`, so `/std:c++20` may be dropped
  silently. A per-compiler `#if` (MSVC traditional preprocessor drops the comma itself
  as a legacy extension) is more robust than relying on flags. **Write this with the
  Write/Edit tool — a heredoc will destroy the line continuations.**
- MIME/morph: foundation only (header accessors, `mimAddressMapping`). Sizing/init,
  vertex application, playback control, and weights/accessories remain.
- Four ingame aggregation stubs still need splitting: `event.c`, `mission.c`,
  `MapBgm.c`, `MapView.c`. (`ingame_camera.c` is done — it became
  `ingame/camera/map_camera.c`, a full reconstruction of `map_camera.o`.)
- Story camera: the whole chain is reconstructed — `map_camera.c`, the `Mrec*`
  camera-rectangle side of `map_rectangle.c`, `RotFvector` (`utility.o`) and
  `GetTrgtRot` (`unit_ctl.o`). **Not yet verified in-game.** If it still falls through
  to the `NoCamera` branch, measure where the chain breaks rather than assuming any one
  link.

### Map modules — done, and what is left

Complete (every `ZERO2.MAP` symbol implemented and verified against the map):
`map_rectangle.o`, `MapHit.o`, `CBuff.o`, `FurnLoad.o`, `FurnCtl.o`, `MapObjReg.o`,
`hit_check_base.o` (see its own section below), plus RegDat's label-lookup family
(`RegDatGetStID4Label`, `RegDatGetStPtr4Label`/`2`, `RegDatBuffID4Label`,
`RegDatVecFind4Label`, `RegDatVecNextFind`).

**None of it has been run in-game.** These are the first modules that add real work to
the per-room load path, so a launch is worth the time before building further on them.

`MapObjReg.c` links against **32 stubs** that were added to their real owning modules
(marked `STUB: needed by MapObjReg.c`, so they are greppable for deletion):

| Module | Stubs | Effect while stubbed |
|---|---:|---|
| `effect_obj.c/h` (new file) | 11 | effect models still draw, never animate |
| `MapObj.c/h` | 7 | `MapObjCheckEffect` → -1 = "ordinary model", the path that draws |
| `MapAnim.c/h` | 4 | no furniture animation at all |
| `MapSp.c/h` | 3 | **done — see below** |
| `MapLight.c/h` | 3 | no per-object lighting |
| `ingame_effect.c/h` | 3 | effect draw callbacks do nothing |
| `MapDoor.c/h` | 2 | doors placed but not state-bound |
| `MapSave.c/h` | 1 | furniture state not persisted |

`MapObj.o` is the natural next target: it owns 7 of the 32 and is the heaviest consumer.

### hit_check_base.o — done

All 9 `ZERO2.MAP` `.text` exports plus the one static, verified 9/9 and 10/10
against `functions.txt`. `.text` is accounted for byte-for-byte
(0x1c7498..0x1c8218 = 0xd80: ten bodies plus four 4-byte alignment fills), the
object has no data sections at all, and every function is driven against a
float32 transcription of its disassembly over 50 000 randomized cases — return
codes, output vectors and distances all match. The shared geometric
primitives under the height mesh and the hit rectangles.

- **`HcBaseLineIntersect` and its two helpers are dead code.** A jal scan
  finds no caller of it anywhere; `HcBaseLineStraddle` is called only from it
  and `HcBaseLineSide` only from `HcBaseLineStraddle`. The shipped
  segment-crossing test is `HcBaseLineIntersect2` (cross products); the trio
  is its scalar sign-of-determinant prototype.
- **The GNU v2 demangler prints array bounds as the max index.**
  `HcBasePointRectangle(..., float (*)[3], float)` in `ZERO2.MAP` is really
  `float (*)[4]` — `PA3_f` stores 3 for a 4-element row. `MapHitSetDoorHit`'s
  `float (*)[3]` = the `float (*mat)[4]` the done MapHit.c already uses.
  Do not "fix" a reconstruction to match the map's printed bound.
- **The ROM mixes libvu0 calls and g3dxVu0.h inlines for the same math, and
  the split is per call site.** `sceVu0SubVector` is a jal in
  `HcBasePointLineXZ`/`HcBasePointRectangle` but the g3dxVu0.h 184-185 inline
  in `IsInTriXZ`/`Intersect2`/`IsLineHitFace`; outer products and the loop
  copies in `PointRectangle` are always calls, the quadword copies in
  `IsNearSegTri` always the 134-135 inline. The port preserves the split:
  `sceVu0*` = ROM call, `g3dxVu0*` = ROM inline. New inlines this settled,
  with ROM lines in their comments: `g3dxVu0AddVector` (161),
  `g3dxVu0SubVector` (184-185), `g3dxVu0InnerProductXZ` (433-436),
  `g3dxVu0PlaneDot` (2095) — dot3 plus the *first* operand's w, the
  plane-times-homogeneous-point the vmaddw.x idiom encodes.
- **`g3dxVu0Sqrt` now takes `fabsf` first.** VU0's SQRT computes sqrt(|x|)
  and only flags a negative input; the host's bare `sqrtf` returned NaN.
  That mattered twice here: `HcBasePointLineXZ`'s `d2 - dv*dv/vv` rounds to a
  tiny negative when the point sits exactly on the line (a NaN distance made
  a touching wall unresolvable), and `HcBasePointRectangle` roots its -1.0
  no-corner sentinel before testing the flag that guards it.
- **`HcBasePointRectangle` keeps corner distances squared all through the
  loop** (`p_len`, both candidates and the running best) and delinearises
  once after it, into the stab-listed scratch `tmp_len` — not into `p_len`.
  The edge arm's `(i + 1) % 4` is a real signed modulo (the movn bias
  sequence), not `& 3`; `IsInTriXZ`'s `% 3` is a real `div` with the zero
  trap — EE GCC has no magic-number division, so `%` spellings survive.
- **Two identical sqrt inlines exist in the ROM's g3dxVu0.h** — 2247
  (`PointLineXZ`) and 2252 (`PointRectangle`). Same body, different lines:
  proof of two distinct source helpers, like the port's `g3dxVu0Sqrt`/`Sqrt2`
  pair.
- **GCC 2.96 CSEs a repeated pure inline only within an extended basic
  block.** `PointLineXZ` writes `XZ(v, v)` twice (condition + body) and the
  object has one expansion; `IsLineHitFace` writes its denominator dot twice
  (condition + after the join) and the object has two. Both repeats are the
  source's own — do not invent locals for them.
- **`IsLineHitFace` negates through doubles**: `* -1.0` runs
  fptodp/dpmul/dptofp, so the plane constant statement is
  `d = g3dxVu0InnerProduct(n, v0) * -1.0;` with a double literal, and `d`
  itself is a float local the stabs omit (it lives in $f20 across the copy
  call — [[float-locals-leave-no-stab]]).
- The earlier pass's annotations here were badly wrong where they existed:
  `PointRectangle` was marked 496-574 (really 304-395), the static
  470 (really 404-439, and it is defined *after* its caller — the object
  emits it last, so the source did too, behind a forward declaration).
  `Intersect2`'s second half is `if (... <= 0.0f) { return 1; } return 0;`
  — the measured 154/158 tags — not a mirrored early-out.

### MapSp.o — done

All 16 `ZERO2.MAP` `.text` exports plus 5 statics and all 13 globals, verified
16/16 against the map and 21/21 against `functions.txt`; `.text` is accounted for
byte-for-byte (0x1133c8..0x114398 = 0xfd0, code plus seven 4-byte alignment fills
and the fixed_array boilerplate). `MAPSP_KAZ_HEAD` (0x30), `MAPSP_KAZ_SPEED`
(0x14) and `MAPSP_KAZ_DB` (0x18) are confirmed by an `offsetof` harness, every
`.lit4` and `.data` float matches the ROM's word bit-for-bit, and `MapSpDbDat`
plus all seven debug-menu rows are diffed out of the compiled `.obj`. It is the
**special-object jobs**: four placed models that need per-frame work instead of
an animation clip, armed by model number as the room registers.

- **`MapSpObjReg()` is a switch on `FurnCtlGetID()`, i.e. on the three digits in
  the model name** — 9 mosquito net, 20 / 705 / 706 draw-first, 24 / 302 / 362
  Z-offset, 28 / 330 sliding screen, 218 / 236 projector reel, 262 shadow,
  329 room-lit, 405 wind charm, 527 / 528 flag 0x800. Case 236 **falls through**
  into the 527/528 flag case; that is the ROM's own order, recovered from the
  line numbers rather than the branch tree.
- **The kaza speed generator alternates ease and hold.** Both arms of the test at
  259/264 store `fMstSpeed = fNextSpeed`; only the equal arm draws a new target.
  So a cycle that eased leaves the pair equal, and the *next* expiry re-randomises.
  The two identical stores are not a decompiler artifact.
- **Only five speed generators exist for all 64 charms**, keyed by
  `MAPSP_KAZ_HEAD::iType` — which is why a group gusts together.
- **`MapSpKazSetMatrix`'s deg → rad → deg round-trip is the wrap, not waste.**
  `MapGeomDegToRad()` folds [180, 360) onto [-pi, 0), so the pair lands `fRot` in
  [-180, 180) — verified over the whole domain by the harness.
- **`MapSpKageProc` and `MapSpFusumaProc` both null-test, clear their job slot,
  and then dereference the pointer anyway.** Kept as found (the same judgement as
  `MapObjRegistFurn`'s early `MapObjUpdateFlg`); unreachable in practice because
  the pointer and the slot are always cleared together.
- **`MapSpKageBuffID` and `MapSpFusumaBuffID` are -1 in `.sdata`, not 0**, and
  nothing ever re-seeds the fusuma one — a zero there would let buffer 0 release
  a screen it never registered. Another [[zeroed-statics-lose-rom-initialisers]].
- **`MapSpMoviProc` has no job slot** — nothing installs it in `MapSpFuncList[]`,
  so the reel only turns while something else calls it.
- **`MapSpAraCheck()` is `ingame_wrk.mChapterNo.Get() >= 7`**, not the constant 0
  the stub returned. Its whole body is attributed to variable.h 167 because the
  inlined accessor moved the line — [[inlined-accessor-line-leaks-onto-caller]].
- A `static inline` at ROM lines 449-452 with no symbol of its own folds the
  screen's `ActionType` (`if (x < 0) x = ~x;`), and `MapSpFusumaProc` calls it
  twice; GCC CSE'd the pair, so the step is not a source local.

`MapGeom.h` gained two inlines this module proves are separate functions rather
than lines inside `MapObjSetPutMatrix`: `MapGeomLoopValue` (23, 26-28, MapSp.o
only) and `MapGeomSetScaleMatrix` (49-52, expanded by six objects). The header
now carries the whole file's line map, tallied from every `SOL`/`$LM` pair in
`symbols.txt` that names it — that tally is what places each helper and says who
uses it, and it is cheap to redo for any header.

**Not yet run in-game.** `MapSpProc()` and `MapSpInit()` were empty and the other
three were `(void)`-cast stubs, so no special object has ever moved; `MapSpProc()`
now runs every frame from `scene.c` and `MapObj.c`, and `MapSpAraCheck()` changes
what `MapSky.c` and `MapObjRegistTreeAnim()` pick from chapter 7 on.

### enemy.o — done

Full reconstruction: all 57 exported symbols plus ~50 statics, verified against
`ZERO2.MAP` and `functions.txt`. `ENE_WRK` is now the ROM's real 0x490 layout, not
the old native-width subset — every offset up to `dat` at 0x340 matches; past that it
drifts because 26 members are pointers.

Facts worth knowing before touching it:

- **A passive ghost with `aie->next >= 0` is not released, it transforms.** `EneRelease()`
  re-seats the slot on the hostile `jene_dat` entry, keeping position and facing, and
  returns 0 — "still in use". That is why the two entries must share a model, and why
  the mismatch is a `G3DASSERT`.
- **`ene_type` 0 and 1 both index `jene_dat`; 2 indexes `aene_dat`.** `ENE_WRK::type`
  carries the same value, so `type == 2` is the passive/hostile test throughout.
- **The five nearest-ghost sweeps differ only in their filter.** They key the result off
  `ENEALG_WRK::idx`, not the loop index — equal in practice, but the ROM reads it back.
- **`EneActSet` walks a three-level u16 offset table** (ghost type → algorithm → action)
  a byte at a time. That is the ROM's own code, not a decompiler artifact.
- **`SetCommonDat`'s passive-Kusabi branch jumps *to* `ChrSortRegistEnem`, not past it.**
  A first reading turned it into an early `return`, which silently dropped every model-3
  ghost out of the draw list.

New stubs it links against, all greppable as `STUB: needed by enemy.c`:

| Module | Stubs | Effect while stubbed |
|---|---:|---|
| `enemy_act.c/h` (new) | 3 | **done — see below** |
| `spirit_gage.c/h` (new) | 3 | **done — see below** |
| `effect_oth`/`_torch`/`_obj` | 7 | no haze, lantern or parts-deform |
| `finder.c/h` | 3 | no enemy health readout |
| `n_plyr_camera.c` | 6 | ring flags and search mark inert |
| `filament.c/h` | 2 | **done — see below** |
| `subtitle`, `stream_auto` | 3 | no ghost voice lines |

`effect_sub.o`'s two — `CheckPointDepth` and `GetCamI2DPos` — are **done**, and were
the reason no ghost could become a finder target: the stub left `result[]` at 0, which
reads as "occluded". The reconstruction is faithful, but its answer on the host is not:
the ROM read one 8×1 strip of the PS2 Z buffer back over the GS→EE bus, and the port
resolves depth in an SDL_GPU depth texture that is never mirrored into emulated GS
memory. The read-back therefore returns zeros — "far plane, nothing drawn" — and the
test degrades to **on screen ⇒ visible**, losing wall occlusion only. It starts working
unchanged the day the renderer writes depth into GS memory at `0x2300`.

**Not yet run in-game.** `EnemyMain()` was an empty stub before this and now does real
per-frame work.

One faithfulness note carried forward: `MapObjRegistFurn` calls
`MapObjUpdateFlg(dp->obj_hdl, ...)` *before* its `dp != NULL` check. That is the ROM's
order and a null `dp` there would fault — it only happens if the 300-entry draw list
overflows. Do not "fix" it without deciding that deviation deliberately.

### ene_mot_ctrl.o — done

All four `.text` symbols plus every `.data` table, verified byte-for-byte against the
ROM by diffing the compiled `.obj`. It is the ghost **animation event track**: per model
a list of clips, per clip a list of `(frame, event)` pairs, applied once a frame for
whatever the clip stepped over. This is where shutter-chance and fatal-frame windows
come from — they are authored into the animation, not the script.

- **Two index conventions, neither is the model number.** `ene_mot_char_tbl[63]` is keyed
  by `ENE_DAT_COMMON::anm_no` (the anm pak, two ahead of the model for these four
  ghosts: `enemot_ch019` sits at 21). `enemot_chNNN[]` is keyed by `ANI_CTRL::mot.play_id`,
  the clip's `GetFileInPak` index — and the `anmNNN` in the ROM's own symbol names is the
  *source* clip number, which does not match: clip 002 is slot 1 on both model 19 and 31.
- **Only models 19, 21, 25 and 31 have a track**, and only model 25 uses it for anything
  but a sound cue.
- **`InitEneMotAlgCtrl` and `ClearEneMotAttr` are dead code** — exported, zero `jal` sites
  in the loadable segments. Ghidra's xrefs showed `.eh_frame` hits for them; the jal scan
  is what settles it.
- **`emc->old_mot` is written but never read.** The restart test is `frm < old_frm`, not a
  clip-change compare — so a clip swap that lands on a higher frame replays nothing.

Read pointer tables out of the ELF **with a script, not by eye**: a hand-read of the
`elf.py` hexdump put `enemot_ch019anm002` in slot 2 instead of slot 1 (a phantom byte
group; the ASCII gutter had it right). Unpacking with `struct` and diffing the compiled
`.obj`'s relocation addends caught it.

### fly_ctrl.o — done

All 14 `ZERO2.MAP` exports plus 8 statics, with `fly_dat[6]` verified byte-for-byte
against the ROM and `FLY_DATA` verified by `offsetof` harness (0x34, every member). The
flying sub-creatures: 40 shared `FLY_WRK` slots, 5 claimed per fly type a hostile ghost
declares, launched one at a time by the action script.

- **The old header's `FLY_DATA` was wrong** — `u_short type` where the ROM has
  `u_char attr; u_char alp;`. Everything downstream of it (the whole table) decoded to
  nonsense until `types.txt` was checked. Another instance of the standing rule: existing
  declarations from the earlier pass are suspect.
- **`mdl_no` bit 0x8000 means "no model, draw a torch effect"** — `CheckEffectFly()` is
  that one-line test and it forks nearly every function in the file. Note the inverted
  sense: non-zero means *nothing to load*. Types 1 and 4 in `fly_dat` are those.
- **`FLY_WRK::mdl_p` / `anm_p` are `int` in `types.txt` but hold pointers** —
  `mmanageIsReadyMdl(..., void **, ...)` stores through them, so they must widen on the
  host. See [[int-pointer-out-params-must-widen]].
- **`FlyInit` and `FlyAct` are dead code** — the ownerless API, zero `jal` sites anywhere.
  Same pattern as `ene_mot_ctrl.o`'s pair; the jal scan is what settles it, not xrefs.
- **`SetFlyLight` writes `vDirection` twice.** The camera-relative direction from
  `_SetVector` is immediately overwritten by a `vmulbc` of the camera's Z axis against a
  broadcast -1.0. The first write is dead in the ROM — keep it, do not "clean it up".
- **Rotation data is in tenths of a degree**, hence the `* PI / 1800.0f` and
  `* PI * 0.1f / 180.0f` forms. `fly_dat`'s own rot fields are whole degrees.
- PAL scaling is `1.1999999f` (60/50, one ulp low) — frame counts divide by it,
  speeds multiply.

One new stub, greppable as `STUB: needed by fly_ctrl.c`:

| Module | Stub | Effect while stubbed |
|---|---|---|
| `effect_torch.c/h` | `EffectTorch2SetAlphaRate` | effect-only creatures never fade visually; faithful anyway while `EffectSetTorch2` returns null |

`player.o`'s `FrameInsideChk` is **done** — it needed only `photo_frame_tbl`, a 4-byte
`.sdata` global (`{436, 300}`, verified against the ROM) that belongs to `photo_dat.o`
and now lives in the otherwise-stubbed `photo_dat.c`. Its four bound locals
(`minx`/`maxx`/`miny`/`maxy`) are real, not decompiler temporaries — `functions.txt`
lists them, which is what makes the 4277–4280 line span read as four statements rather
than one multi-line `if`.

`PhotoFlyChk`'s own ROM caller is inside `PlyrCamTurnChk`, which is an empty stub in
`player.c`, so nothing reaches it yet. **Not yet run in-game.**

### enemy_act.o — done

All 155 `ZERO2.MAP` exports plus 2 statics and all five dispatch tables, verified against
`ZERO2.MAP` (155/155) and against `functions.txt` for every one of the 32 function-local
statics (owning function, type and `.bss`/`.sbss` address all match). At ~4800 lines it is
the largest single module in the tree: the ghost **behaviour-script interpreter**, and
the place every visible ghost decision actually comes from.

**The machine.** `ENEALG_WRK` is a cursor into a flat byte stream (the ALG pak).
`EneAlgCtrl()` decrements `wait_time` by `ew->reso` once a frame, then runs opcodes in a
loop until one parks a positive wait — so a run of state changes executes inside a single
frame. `pos_no != 0` means "re-enter the same handler without consuming another opcode
byte", which is how the multi-frame jobs (fades, turns, wander) are written.

- **Opcode space is split four ways by numeric range**, each with its own table:
  `0x00..0x6f` `CommJmpContTbl` (74), `0x70..0x9f` `CommJmpMoveTbl` (22),
  `0xa0..0xdf` `CommJmpBrnchTbl` (43), `0xe0..0xfe` `CommJmpEffTbl` (6). The range checks
  are coarser than the tables — the ROM does not bounds-check within a space.
- **`BCommJmpTbl` really has a trailing NULL** (8 slots, 7 handlers) and `bjob_no` is not
  range-checked at all.
- **`wait_time == 255.0f` is the end-of-run sentinel**, not a wait.
- **Operand fetches leave no symbol** — they were a macro or a header inline this build
  does not carry. The port uses `EaGetU8` / `EaGetU16` / `EaJumpTo`, the same choice
  `sis_algo.c` already made; `functions.txt`'s local lists are what pin down how many
  operands each opcode really eats.
- **Several opcodes read an operand byte and throw it away.** `EJobC10` skips one,
  `EJobC33` two, `EJobM0E` one. GCC merges the pointer bumps into one store and drops the
  dead load, so the *only* evidence is the `pu8 = base + N` stride — count it, do not
  count the loads.
- **Whole bodies are commented out in the ROM and still eat their operands.** `EJobC14`
  (50 source lines), `EJobM09` (40), `EJobE00`/`E01`/`E02`, `BJobL02`, and `EJobC10`'s
  entire `case 1` (~95 lines) compile to nothing but a cursor bump. A big gap in the
  `; Line` sequence is the tell.

Facts worth knowing before touching it:

- **`ENE_WRK` member decoding is mechanical.** Ghidra renders `&ene_wrk[alg->idx]` as
  `ene_wrk.m_aData[0].SOMEMEMBER + iVarN * S + K`; the byte offset is
  `base(SOMEMEMBER) + K * (16/S)`. Worth scripting — a rewriter over the decompile text
  paid for itself many times over here.
- **The four transparency terms have one opcode family each**: `0x18`–`0x21` drive
  `tr_rate_alg` (with `loop[0]`), `0x2b`–`0x2e` `tr_rate_in` / `tr_rate_out` (with
  `loop_tr[0]` / `loop_tr[1]`). That is what the second counter pair exists for.
- **`EJobM06` / `EJobM07` / `EJobM11` share a vertical-speed block that is a ROM bug.**
  `tv[1]` is zeroed, the speed stored, then the ghost's own position added back — so the
  height difference it then computes collapses to `adjp[1]`. Reproduced as found.
- **`EJobM08`'s PAL branch computes exactly what its NTSC branch does.** Two different
  `.lit4` slots, both `1.57079625`. The 60/50 factor was never applied. Do not "fix" it.
- **`EJobC33` increments `pos_no` on a path whose shared tail immediately zeroes it.**
  Dead in the ROM; kept.
- **`EJobM12` copies child → parent**, not the other way round: the parent snaps onto its
  last live child. Reads like a bug, is what ships.
- **`EJobB13` never clears `pos_no` / `wait_time`.** Harmless (both are already zero when
  a branch opcode runs) but it is the ROM's own asymmetry.
- **Branch senses are not consistent.** Roughly half the conditional opcodes jump when the
  test passes and half when it fails, and the `sw` operand flips the comparison rather
  than the jump. Every one is spelled out rather than factored.

Four new stubs, greppable as `STUB: needed by enemy_act.c`:

| Module | Stub | Effect while stubbed |
|---|---|---|
| `finder.c/h` | `FinderBankPlay` | no camera-side hit / vanish cues |
| `ingame_effect.c/h` | `IgEffectIsEndParticleSuck` | **returns 1, not 0** — `EJobB15` loops for ever otherwise |
| `unit_ctl.c/h` | `ReqEneStop` | scripted deaths do not freeze the other units |
| `effect_obj.c/h` | `CallPartsDeform5` | ghosts do not dissolve visually |

`finder.h` had to pull in `system/eeiop/snd3d.h` for `SND_3D_SET`.

**Not yet run in-game.** `EneAlgCtrl()` was an empty stub before this, so every ghost held
its pose; it now drives the whole ghost.

### filament.o — done

All 14 `ZERO2.MAP` `.text` symbols, the four global blink switches in `.sdata`, and the
four linkonce `CBlinkSwitchVariable<...>::Work()` bodies. `.text` is fully accounted for
byte-for-byte (0x18f648..0x18fbfc), and every offset, size and clamp constant is verified
by an `offsetof`/behaviour harness against the ROM. It is the viewfinder needle: a dial
plate plus a two-piece needle whose **alpha**, not angle, carries the deflection.

- **`CBlinkSwitchVariable<T,Min,Max,Time,InitVal>` belongs in `common/variable.h`**, not
  `m_plyr_camera.h` where the earlier pass declared it, and the earlier parameter names
  (`Lo/Hi/Up/Dn`) were wrong. The per-tick step is `(Max - Min) / Time` truncated, and the
  blink-**off** branch decays to `InitVal`, not to `Min`. filament.o's four instantiations
  all have `InitVal == Min` and cannot tell those apart; film_no.o's `<char,70,90,15,40>`
  is what settles it.
- **`Init()` leaves the switch off** (`mOn = 0; mUp = 1`) and every caller follows with
  `BlinkOn()`. GCC folds the pair into one `ori 3`, so a lone `ori 3` is *two* calls.
- **`SetSave()` hands over `&mLockCnt` and 4 bytes**, not the whole object — the stub that
  was there guessed `sizeof(CFilament)`.
- **`RTModeOn` / `RTModeOff` are genuinely empty** (8 bytes, `jr ra` + a bare `nop`), so
  `mRTFlg` never rises and Draw's whole RT branch, plus `rt_ev_wrk`, `fillament_wrk`,
  `mMode` and `mRTTime`, are unreachable in this prototype.
- **`FadeIn` / `FadeOut` are dead code** — zero `jal` sites in the loadable segments, the
  same pattern as `ene_mot_ctrl.o`'s and `fly_ctrl.o`'s exported-but-unused pairs.
- **All three sprites rotate about the *plate's* position.** The ROM holds it in
  `$f20`/`$f21` across the two later `CopySprDToSpr()` calls, so blocks 2 and 3 write a
  centre that is not their own `ds.x`/`ds.y`. Do not "fix" it to `ds.crx = ds.x`.
- **filament.c lines 120–210 hold no code.** `.text` is complete without them, so that
  90-line span is comment or a disabled older draw; nothing of it is recoverable.

Two facts about the debug info that this module is the clearest example of:

- **`symbols.txt`'s `SOL` records say which *file* each `$LM` belongs to.** They are
  interleaved with the `$LM` labels (`<addr> LABEL TEXT SOL <path>`), so a header line
  number is unambiguous — this is what placed `CBlinkSwitchVariable` in `variable.h` and
  pinned `Get()` to line 657. Prefer it to guessing from `; Line NNN` alone.
- **Float locals leave no stab.** `CFilament::Draw`'s `fcx`/`fcy` and
  `CNPlyrCamera::DrawSpiritGageBase`'s are absent from both `functions.txt` and
  `symbols.txt`, while every `int` register local is listed. A value kept in `$f20`–`$f23`
  across a call *is* a source local even though the local list denies it.

**Not yet run in-game.** Every body was an empty stub before this.

### n_equip_tray.o — done

All 41 `ZERO2.MAP` `.text` symbols plus the nine `sub_func_*_param` tables,
`aStockMaxTbl` and `equip_func_tbl`, verified 41/41 against the map and byte-for-byte
against the ROM by diffing the compiled `.obj`. `CNEquipTraySave` (0x20) and
`CNEquipTrayWrk` (0x80) are confirmed by an `offsetof` harness, every member and the
bitfield word at 0x7c. It is the camera's **equip tray**: three sub-function slots, the
spirit-power accumulator that charges them, and the stock items that firing spends.

- **The classes belong in `n_equip_tray.h`, not `m_plyr_camera.h`.** The object file
  carries exactly three `n_equip_tray.h` line numbers — 171, 175, 185 — and they are
  `GetNowSubFuncNo()`, `EquipTrayFuncCostNum()` and `GetNowSubFuncLv()`. What settles
  171 is the contrast between `CNEquipTraySave::Absorb` (reads `mEquipFunc[mSlctNo]`
  with only a variable.h stab, because `Save` has no such inline) and
  `CNEquipTrayWrk::AbsorbImmediately` (same read, tagged 171).
- **`CVariable`'s real method list is in `types.txt`** — Init/SetMax/SetMin/GetMax/
  GetMin/GetWidth/Set/Increment/Decrement/LoopIncrement/LoopDecrement/Offset/Add/Sub/
  LoopAdd/LoopSub/Get. There is **no `operator T()` and no `operator=(const T&)`**;
  those are the port's, and `Get()` is variable.h line 167. The tray's selector walks
  with `LoopIncrement()` / `LoopDecrement()`, which do *not* assert — only `Set()` does.
- **`CBlinkVariable` and `CFVariable` belong in `common/variable.h`.** Same evidence as
  `CFadeVariable`/`CBlinkSwitchVariable` before them: `CBlinkVariable::Init` expands at
  variable.h 554/555, `IsOn` at 559, `Blink`'s assert banner names line 603; `CFVariable`
  at 187/188/190 (SetMin/SetMax/Set), 243–248 (`Add`), 272/273 (`LoopAdd`), 287 (`Get`).
- **`CBlinkVariable::IsOn()` is "still pulsing", not "lit".** It tests `mSpeed`, and
  `Work()` zeroes `mSpeed` the moment the pulse lands back on Min. `Blink()` ramps to
  Max, negates the speed, and stops dead at Min — one up-down pulse, not a loop.
- **A `SetAddVal()` argument computed from `GetWidth()`/`GetMax()` still emits the
  divide-by-zero trap check.** `mAdd = -108/5` shows up as a runtime `div` with both
  operands literal; `76/8` folds away because it is a power of two. That trap sequence
  is how the *divisor* is recovered when GCC folded the quotient.
- **Ren reads the *metsu* damage table in `SetEffect()`**, not `sub_func_ren_param`.
  Its own table is only ever used by `GetDmgRate()`. Reproduced as found.
- **`ConvertGage2StockNum()` printfs every frame the accumulator moves.** Plain
  `printf`, not `PRINT_ERROR` — one `jal`, no banner.
- **The accumulator dial is a depth-stencil trick.** Two black `DISP_SQAR` quads are
  drawn *with* ZMSK clear and a pass-everything TEST to lay a depth wedge, then the
  needle is drawn at `z = 0xfffdf` with ATST GREATER and ZTST GEQUAL to clip it. It is
  the only place in the file that leaves the usual `zbuf 0x10a000118` / `alphar 0x44`
  pattern — and it depends on GS TEST, which is the one draw_env register the port
  emulates.
- **`equip_func_tbl[NONE].pk2_no == 24` is a sentinel, not art.** Every draw tests for
  it and skips, so an empty slot is never drawn.

One new stub, greppable as `STUB: needed by n_equip_tray.c`:

| Module | Stub | Effect while stubbed |
|---|---|---|
| `effect_ene.c/h` | `EneDmgParticleSuctionNumGet` | returns 0 — consistent with `EneDmgParticleEffectReq()` never launching a burst, so no spirit power is lost (it all arrives via `AbsorbImmediately()`) |

`CNEquipTraySave::Init` and `CNEquipTrayWrk::SetBattleFlg` were stubbed in
`m_plyr_camera.c` and belong here — the fifth instance of that hazard.

**Not yet run in-game.** Every body except `SetSave` was an empty stub before this, and
`CNPlyrCamera::Draw()` calls `Draw()` and `RenzMarkDraw()` every finder frame.

### photo.o — done

All 40 `ZERO2.MAP` `.text` exports plus 6 statics and all 17 globals, verified 40/40
against the map. `PICTURE_WRK` (0x20), `PFILE_WRK` (0x208), `PHOTO_WRK` (0xc) and
`PHOTO_WRK_DEF` (0x40) are confirmed by an `offsetof` harness, and the three `SPRT_DAT`
hint plates plus `DispPhotoName`'s `DRAW_ENV_5` initialiser are byte-for-byte identical
to the ROM's `.rodata`. It is the **photo phase** (a twelve-step machine on
`photo_wrk.mode`) and the **photo album** (sixteen `PICTURE_WRK` slots).

- **`; Line NNN` markers interleave between adjacent statements at -O2.** A marker for
  line N appearing *after* one for N+1 is GCC's scheduler, not a source-order clue —
  `final` emits a fresh `$LM` every time the current line changes in the already-scheduled
  insn stream. `PictureCapture` has `$LM346`(580) `$LM347`(581) `$LM348`(580)
  `$LM349`(581) in four consecutive instructions. Read statement order from the
  *ascending* sequence and treat interleaving as noise.
- **`fixed_array::operator[]` swallows the statement's own line.** Any statement whose
  only memory access goes through a subscript is attributed to fixed_array.h 124/125, so
  it leaves no `$LM` at all. In photo.c that is 73 of 575 annotations — they have to be
  interpolated into a measured gap, and the file says so in its header.
- **`STATICPROC`/`PROC` stabs give the function's *opening* line, which is not the first
  `; Line` Ghidra shows.** `one_Story_Photo` opens at 1522 and its first statement is
  1523; `PhotoDebug` opens at 1568 and its only code is the return at 1596 — twenty-eight
  lines commented out. Ghidra's LINES list cannot distinguish those.
- **The album never moves image data.** `PICTURE_WRK::adr_no` names the photo-data page
  and travels with the record through every sort, which is why `DeletePhotoData()` parks
  the freed `adr_no` on the now-empty last slot instead of dropping it.
- **`GetSavePhotoNo`'s second scan stops at 15, not 16**, and slot 15 is the fallback —
  so a completely full, completely protected album still gives up slot 15.
- **`SortPhotoData_NewTime`/`_OldTime` are six full bubble passes**, one per date field,
  each gated on all earlier fields being equal. 0xb0c bytes each and the largest
  functions in the file; the RTC is BCD, so `SetDateInfoType()` unpacks both records on
  every comparison.
- **Three ROM bugs, reproduced:** `GetFilePhotoAdrNo()` and `GetPhotoData()` print the
  wrong function name in their range errors; `DelFilePhotoProtect()` stores `status = 0`
  then `status = 1` (the dead store survives because the inlined bounds check sits
  between them); and `SortPhotoData_NonProtect()` copies back `set_count` records where
  its sibling copies all sixteen.
- **`PHOTO_WRK_DEF` needs explicit tail padding** — the leading `float[4]` gives it
  quadword alignment on the EE (0x40) and only 4-byte alignment on the host (0x3c). Same
  hazard as [[float4-members-lose-quadword-alignment]].

New stubs, all greppable by `STUB: needed by photo.c`:

| Module | Stubs | Effect while stubbed |
|---|---:|---|
| `photo_make.c/h` (new file) | 7 | **done — see below** |
| `effect_oth.c/h` | 4 | no door-seal dissolve; `DoorSealDisappearIsEnd()` **returns 1, not 0** — a 0 parks the phase in mode 10 for ever |
| `effect.c/h` | 2 | effects are not frozen for the phase, and get no per-step pass |
| `effect_scr.c/h` | 1 | no white flash |

`SyncHpBar` was stubbed in `photo.c` and belongs to `hp_bar.o` — the sixth instance of
that hazard; its declaration moved to `m_plyr_camera.h` beside `CHpBar`.

**Not yet run in-game.** `one_Story_Photo()` was an empty stub returning
`GPHASE_CONTINUE`, so the photo phase never advanced; it now drives the whole sequence
and `PlayerTakePictJob()`'s album writes land for the first time.

### spirit_gage.o — done

All 6 `ZERO2.MAP` `.text` exports, both `.data` colour vectors and the `.rodata`
damage table, verified 6/6 against the map and byte-for-byte against the ROM by
diffing the compiled `.obj`. `CSpiritGage` (0xc) and `CMIN_MAX<float>` (0x8) are
confirmed by an `offsetof` harness, and the arithmetic is checked against a
transcription of the disassembly over the whole `mPercent` domain. It is the
**per-ghost spirit gauge**: the ring of blips that fills while the camera drains a
ghost, plus the multiplier the shot is finally scored with.

- **The fill saturates at exactly 90, not 100.** `Work()` weights the three rates
  70/15/5, and a shutter chance *replaces* that scale — `iPercent / 10 + 90`, so any
  shot taken in a chance lands in 90..99. That single fact explains the other two
  constants in the file: `Draw()` switches to the shutter-chance colour at 90 and
  `CalcDamageRate()` divides by 90.
- **`GetByProportion()` is deliberately unclamped**, which is how a normal chance
  reaches 2.02 against a table maximum of 2.0.
- **`iMinPercent` does not lift an empty gauge.** The test is
  `iPercent != 0 && iPercent < iMinPercent` — a ghost that scored nothing stays at 0.
- **The ring is 13 whole blips plus one partial**, and the partial one's *alpha*
  carries the fraction in six steps: `iDivNum` is
  `p*13*6/100 - (p*13/100)*6`, i.e. six times the fractional part, divided back by
  six at line 122.
- **`n_finder_dat[12]` is a degenerate record** — all zeroes but `pri`/`alpha`, so it
  covers no pixels. Every blip you see is entry 13; the ROM draws both anyway, and so
  does the port. Same flavour as `n_equip_tray`'s `pk2_no == 24` sentinel.
- **`mFlg` is written once by `Init()` and never read**, anywhere in the object.
- **`CMIN_MAX<T>` belongs in `common/variable.h`**, between `CFadeVariable` (…523)
  and `CBlinkVariable` (553…). `CalcDamageRate` is its only expansion in the build;
  `GetWidth` is line 533 and `GetByProportion` 536. `GetAverage` is in `types.txt`'s
  member list but nothing expands it, so it is left out rather than invented.

Two debug-info lessons this module is the clearest example of, both worth carrying:

- **An inlined accessor's line number leaks onto the rest of the caller's statement.**
  GCC emits line notes per *statement*, so once `Get()` (variable.h 352) has moved the
  line, the caller's own `* iMasterAlpha / 128` gets no fresh note and is reported at
  352 too. `CWrkVariable::Get()` really is the bare `return mValue;` — filament.c's
  identical `mMasterAlp.Get() * iAlpha / 128` at 0x18f914 is what settles it. Do not
  conclude an accessor takes an argument from the arithmetic attributed to it.
- **A value held in a callee-saved register with no stab is a CSE temp, not a local.**
  The stab list is exhaustive for `int`s (`fndr_mx` and `i` even share `s0`, and both
  are listed), so `mPercent * 13 / 100` living in `s3` across a call proves it is a
  subexpression written twice — the loop bound and `iDivNum`'s initialiser — rather
  than a variable Ghidra failed to name.

**Not yet run in-game.** Every body except `Init()` was an empty stub before this, and
`CNPlyrCamera::Draw()` calls `mpSpiritGage->Draw()` on every finder frame.

### photo_charger.o — done

All 4 `ZERO2.MAP` `.text` exports plus both linkonce `CBlinkVariable<char,50,127>`
bodies, verified 4/4 against the map. `CPhotoCharger` (0x8) is confirmed by an
`offsetof` harness, and the whole object is checked frame-by-frame against a
transcription of the disassembly for every charge time 1..200. It is the **shutter's
charge accumulator**: the tick row along the top right of the viewfinder, and the
`mReady` latch that gates the charged shot, the film-number blink and the spirit gauge.

- **`Work(int bSeFlg)` gates the *cue*, not the counting.** The earlier stub's header
  said the argument held the charge where it was; it does not — the countdown, the
  flare and `mReady` all advance whether the finder is up or not. `n_plyr_camera`
  passes 1 in finder mode and 0 outside it purely to silence the sound.
- **The cue fires three frames early** (`mNowWaitCnt.Get() == mWaitCnt - 3`), so it
  lands with the flare rather than after it.
- **`Reset()` does not restart a countdown already running** — `CWaitVariable::Wait()`
  only seeds a counter that has reached zero, so a film change mid-wind does not hand
  the player a free charge.
- **The tick row is the *elapsed* fraction**: `(mWaitCnt - now) * 16 / mWaitCnt`, so it
  fills as the counter runs down. Sixteen copies of one 6x4 sprite
  (`n_finder_dat[39]`), five pixels apart, additive.
- **`mWaitCnt` is an unguarded divisor** in `Draw()` — a film with a zero charge time
  traps. Reset() is the only writer.
- **`CPhotoCharger::Init()` is declared by the ROM and never emitted or expanded.**
  `CNPlyrCamera::Init()` calls `Reset()` instead. The empty out-of-line stub that was
  there is removed rather than kept, since ZERO2.MAP has no such symbol.

The `<char,50,127>` instantiation is the useful thing this module contributes to
`common/variable.h`: **`CBlinkVariable::Init()` really does seed from `Min`**, which
n_equip_tray.o could not settle because both of its instantiations have `Min == 0`.
`Reset()` stores `0x32` and the linkonce `Work()` clamps its low end to the same
`0x32`. Newly measured header lines, all added: `CWaitVariable` Wait 425/426/427,
Get 433, Work 441–449; `CBlinkVariable` Init 553, Get 567, Blink 598/599/600,
Work 610–627. Note Blink's ROM order is `if (tTime != 0) … else assert` — line 600 is
the assignment and 603 the assert.

**Not yet run in-game.** All four bodies were empty stubs, so the charge never
completed: `IsReady()` returned 0 for ever and nothing downstream of it could fire.

### camera_power_up.o — done

All 3 `ZERO2.MAP` `.text` exports and all 3 `.rdata` tables, verified 3/3 against the
map and byte-for-byte against the ROM by diffing the compiled `.obj`.
`CCameraPowerUp` (0x20) is confirmed by an `offsetof` harness, together with the exact
byte range each of `Init()` and `AllRelease()` touches. It is the camera's **upgrade
state**: three gems plus ten sub-function gems, two derived grades, and the three
tables that say what a grade is worth.

- **`camera_power_up.h` is a real ROM header**, not part of `m_plyr_camera.h`.
  player.o's `PhotoDmgChkSub2()` and `SpiritGageCalc()` carry
  `SOL ../photo/camera_power_up.h` stabs for the three inline accessors, which places
  them and pins their lines: `GetRadius()` 36/37, `GetDistance()` 41/42,
  `GetDmgRate()` 44/45. The class moved there and `m_plyr_camera.h` now includes it.
- **`GetRadius()` is `GetRadiusRate() * 108.0f`** — 108 is the stock capture ring, so
  the four call sites that had the multiply written out (three in player.c, one in
  photo_dat.c, one of them already annotated `/* 37 */`) are the accessor. The rings
  are 91.8 / 97.2 / 108 / 118.8 by grade.
- **The tables are private in the ROM.** player.c was reaching into `aDmgTbl` and
  `aDistanceTbl` directly; those three sites are `GetDmgRate()` and `GetDistance()`.
- **`AllRelease()` deliberately leaves both grades alone** — only the gems and the four
  flag sets are raised, so the upgrade menu still has to be visited to spend them.
- **`Init()` writes four of the five scalars twice**, once via `CVariable::Init()`
  (variable.h 19/20) and once via `CVariable::Set(0)` (49), with `mAccumGem` only via
  the second. Nine stores are emitted and this object's `.rodata` carries `Set()`'s
  "Set Value is Illegal" strings, which only appear if `Set()` really was expanded
  here. Byte 0x0f — padding after `mSubFuncGem[10]` — is written by neither.
- **`aRadiusTbl`'s 0.85 and 1.1 are one ulp low** (EE GCC literal truncation) and are
  spelled `0.84999996f` / `1.0999999f`. `aDmgTbl`'s 1.3 and 1.8 are **not** — `1.3f`
  and `1.8f` round to the ROM's words exactly. The old file's comment claimed they were
  truncated; check the bits rather than assuming the pattern repeats.

New `common/variable.h` lines this settles: `CVariable::Init()` 19/20, `SetMax()`
23/24, `GetMax()` 29; `BIT_FLAGS::AllUp()` 808/809. `SetMin()` and `GetWidth()` are in
`types.txt`'s member list but nothing expands them, so they are left out. One
correction carried back: the loops in `CNEquipTraySave::Init()` are
`CVariable::Init()` (19/20), not `Set(0)` (49) — behaviourally identical for every
`<char,0,N>` instantiation in the build, which is all of them.

`CCameraPowerUp::Init`, `::GetRadiusRate` and `::AllRelease` were all in
`m_plyr_camera.c` — the seventh instance of the stub-in-the-wrong-file hazard.

**Not yet run in-game.** `GetRadiusRate()` returned a hardcoded 1.0 before this, so
every upgraded lens behaved as stock.

### sp_chance.o — done

All 7 `ZERO2.MAP` `.text` exports, verified 7/7 against the map; `.text` is accounted
for byte-for-byte (0x263288..0x263718). `CSPChance` (0x10) is confirmed by an
`offsetof` harness, and every one of the eight `(mSPFlg, bSeFlg, mbSeFlg)` combinations
is driven for 40 frames against a transcription of the disassembly — both alpha ramps
and every play/stop call match. It is the **shutter-chance lamp**: three sprites, one
looping cue, and two mutes.

- **`Work()`'s argument gates the cue, not the work** — the same shape
  `photo_charger.o`'s does. `bSeFlg` is the fitted-part flag (`mCamPartsSetFlg` bit 2);
  the lamp still ramps and the strobe still stops when it is 0. Together with `mbSeFlg`
  (the photo phase's mute, via `SEEnable`/`SEDisable`) the voice has two independent
  mutes and the lamp has none.
- **`mAlpha`'s sweep time is 1**, so `CBlinkSwitchVariable<short,0,128,1,0>` steps the
  whole 0..128 range every tick: the halo strobes on/off at 30 Hz rather than pulsing.
  `mLampAlpha` is the slow one — `SetAddVal(128)` on, `SetAddVal(-8)` off, so the core
  snaps on and bleeds out over exactly 16 frames.
- **Three sprites, drawn plate → core → halo** (`n_finder_dat[31]`, `[33]`, `[32]`).
  The plate is unconditional and blended at plain master alpha; the other two are
  additive and carry the two ramps. 32 (33x34) encloses 31 (16x16), and 33 is a 6x6
  core inside it.
- **`Init()` does not touch `mSe`.** `CSND_BUF_PLAY_NO_ID` is `0x300000`, not 0, so an
  object that arrived zero-filled rather than constructed reads as "already playing"
  and the cue never starts. The harness hit exactly that before it seeded `play_id`.
  Same family as [[zeroed-statics-lose-rom-initialisers]].
- **`Set()` stops the cue on the way down only.** Raising the chance leaves the voice
  to `Work()`, which will not start one unless the part is fitted and the phase has
  not muted it.

**`CSND_BUF_PLAY` belongs in `common/zero2_util.h`, not `system/eeiop/snd_buffer.h`.**
Every expansion in the build — n_plyr_camera.o, photo_dat.o, player.o, sp_chance.o —
carries `SOL common/zero2_util.h`, and `snd_buffer.h` never appears as a SOL anywhere.
The line numbers already on it are that file's and are right (ctor 43/44, Fade 45/47,
PitchFade 49/50, IsPlaying 52, Stop 56/57/58); only the home is wrong. **Not moved** —
zero2_util.h would need to see the `SndBuf*` prototypes that snd_buffer.h declares, so
it is a two-file untangle rather than a cut-and-paste, and nothing depends on it. A
note to that effect sits on the struct.

**Not yet run in-game.** All seven bodies were empty stubs, so the lamp never lit and
the cue never played.

### photo_make.o — done

All 20 `ZERO2.MAP` `.text` exports plus `photo_frame[14]`, verified 20/20 against the
map and byte-for-byte against the ROM by diffing the compiled `.obj`'s `.data`. Every
GS register word the file writes is checked against the ROM's literal by a harness
(36 constants: TEX0, GIFtag, FRAME, XYOFFSET, BITBLTBUF/TRXPOS/TRXREG/TRXDIR, CLAMP),
and the strip loop, the alpha ladder and the two fade ramps are driven over their whole
input domains. It is everything between the shutter going and a picture existing:
capture, compression, and the photo phase's own per-frame draw.

- **A picture is 384x128 PSMCT24 — exactly 0x30000 bytes — and the screen window it
  comes from is 384x256.** The 2:1 squash happens once, in `DrawSpecialFurnPhoto()`'s
  sprite, and is undone at draw time by `ds.sch = 2.0f`. Every size in the file follows
  from that: 0x30020 with the header, 0xd360 per compressed album slot, 45x15 stored /
  45x30 drawn for the thumbnail.
- **The photo passes are the only place in the finder HUD that writes GS depth.** The
  black quad under the picture is drawn *with* ZMSK clear to lay a wedge, and the two
  full-screen tint passes above it read it back with ATST GREATER / ZTST GEQUAL. Same
  trick `n_equip_tray.o`'s accumulator dial uses, and it depends on the one draw_env
  register the port emulates.
- **`CompressData()` is a fallback ladder, not a codec.** Lossless LZSS first; if the
  result is over 27.4% of the input, the lossy DCT codec at quality 1..4; if even that
  fails, `header->type = 2` and the slot decodes to black. `quality` is post-incremented
  *inside* the `CompressFile()` call, which is why the ROM tests the already-bumped
  value.
- **`photo_expand` is a three-byte handshake, not a flag.** `GetPhotoExpand()` returns
  the old state and moves 0 -> 1, so the caller that sees the 0 owns this frame's slice;
  `ExpandFile()` posts sta = 2 when it reaches the end of the picture. That is what makes
  `DrawPhotoFromWorkArea()` a multi-frame job rather than a stall.
- **`DrawPhotoBuffer()` converts x/y to screen-centred coordinates and straight back**
  (`- 320 - 1` at line 57, `+ 320` at 114). Dead in the ROM — `DispSprD2()` does its own
  centring — and kept as found. Its `SPRT_DAT2 sd` is also genuinely uninitialised;
  every field `CopySprDToSpr2()` reads out of it is overwritten except `pri`, which
  `DispSprD2()` never looks at.
- **`DrawPhotoFrame()` draws ten of its fourteen sprites rotated** (180 for 6/7/9, 270
  for 10/11, 90 for 12/13), each about its own already-offset position. That is why the
  table's coordinates for those entries look like they sit outside the frame.
- **`DrawPhotoHinttex`, `DrawSPhotoFromSmallPhotoArea` and
  `DrawSPhotoFromSmallPhotoAreaAddr` are dead code** — exported, zero `jal` sites in the
  loadable segments. Same pattern as `ene_mot_ctrl.o`'s and `fly_ctrl.o`'s pairs.

The debug-info lesson this module is the clearest example of: **a run of `ld`/`sd` (or
`ldl`/`ldr`) from a `.rodata` address into a stack slot at the head of a scope is a
local aggregate initialiser, not a static table.** `DispPhotoFrame1` has four of them
and they account for the whole tail of the object's `.rodata`; reading them as file-scope
statics would have invented four globals that `globals.txt` correctly does not list.
See [[rodata-blob-into-stack-is-a-local-initialiser]].

New files and stubs, all greppable as `STUB: needed by photo_make.c`:

| Module | Stubs | Effect while stubbed |
|---|---:|---|
| `system/compress/compress.c/h` (new) | 2 of 7 | the five handshake accessors are real; `CompressFile`/`ExpandFile` (the DCT codec) are not |
| `effect_scr.c/h` | 3 | no shutter blur, contrast lift or frame darkening |
| `encodes.c/h` | 1 | **`SlideEncode` — the one thing left before saved photos carry real pixels** |

`ExpandFile()`'s stub deliberately posts `photo_expand.sta = 2`, the completion the real
one posts at the end of a picture; leaving it at 1 parks `DrawPhotoFromWorkArea()` in a
loop that hands out a slice every frame and never finishes. Same judgement as
`IgEffectIsEndParticleSuck` returning 1.

`log_2` (utility.o) is done — `ceil(log2(n))`, which is what a TEX0's TW/TH want.
`pdrawenv` needed an `extern` in `system.h`; it was only ever defined in `system.c`.

**Not yet run in-game.** All seven bodies photo.c calls were empty stubs, so the photo
phase drew nothing and filed nothing.

### system/eeiop sound — all 11 TUs done

Every sound translation unit in `system/eeiop` is reconstructed: `snd.o`, `snd_3d.o`,
`snd_buffer.o`, `snd_bank.o`, `snd_util.o`, `snd_pcmstream.o`, `snd_stream.o`,
`spu_mem.o`, `spu_voice.o`, `hxd.o` and `stream_auto.o`. **148/148 `ZERO2.MAP` `.text`
symbols implemented**, plus ~30 statics and every `.data`/`.sdata` initialiser. All
non-pointer layouts are confirmed by an `offsetof`/`sizeof` harness, and `snd_3d_env`,
`eff_use_size_tbl` and the `.lit4` constants are verified bit-for-bit against the ROM.

The shared EE/IOP types went into a new **`system/eeiop/snd_def.h`** — the payload
structs for every `IOP_COMMAND_ENUM`, plus `VOLSET`, `VOICE_ATTR`, `SOUND_INFO`,
`HXD_HEADER` and `sceSdEffectAttr`. The ROM's own header name is not recoverable: none
of the sound objects carries a `SOL` stab, because none of them inlines anything.

The stack, top down: `snd.o` brings the others up and pumps them once a frame;
`stream_auto.o` schedules streams onto `snd_stream.o`'s two slots by priority;
`snd_util.o` does fire-and-forget bank playback on `snd_bank.o`; `snd_bank.o` and
`snd_stream.o` both claim voices from `spu_voice.o` and SPU RAM from `spu_mem.o`;
`snd_buffer.o` owns the 48 one-shot voices; `snd_3d.o` turns positions into
volume/pitch for both.

Facts worth knowing before touching any of it:

- **Everything ends at the SIF transport, which is a no-op shim.** `iopCommandRegister()`
  is real and the queue is built correctly; `sceSifCallRpc()` then drops it. Two
  consequences are worth naming because they look like bugs. `CheckEndPointThrough()`
  reads `iop_ret`, which `ee_iopMain()` copies out of a reply buffer nothing fills, so it
  answers "not finished" for ever — `SndBufPlayMain()` never retires a voice by itself and
  `StreamWrkRelease()` always parks the slot back in `ST_STREAM_END`. And every stream's
  status reads `ST_STREAM_NO_USE`, so `StreamPlayQueueWrk()` sees its slots as permanently
  idle. Both start working the day the SIF shims carry real traffic.
- **The EE's `status` and the IOP's are separate everywhere.** `SndStreamMain()` and
  `SndPCMStreamMain()` are the only places they meet; every transition out of
  `PRE_LOAD`, `PLAYING` and `WAIT_END` is gated on `GetStreamWrkRet()`.
- **`SndCalcValue()` is the whole mixer.** `vol * bvol >> 7`, then `* group >> 8`, then
  pan into a `VOLSET`; a non-NULL 3D handle *replaces* the pan (`>> 14`) and multiplies
  the pitch (`>> 12`). Mono folds the pair at the very end. Every voice owner calls it.
- **Effect samples go to SPU core 0, everything else to core 1.** `SndBankPlay()` derives
  the core from `info[no].attr.effect` being *zero*, and retries on the other core with
  the reverb send forced off if the first is full.
- **`SND_3D_WRK::status` bit 1 is a dirty flag, not a state.** `Snd3DMain()` recalculates
  only dirty emitters — unless `snd_3d_listner.defer` is up, in which case every live one
  is stale.
- **A stream that loops off a block boundary needs a third SPU buffer.**
  `SetStreamStartSub()` allocates four interleave units instead of two and starts the
  play buffer halfway in; `StreamWrkRelease()` frees the loop packet, which *is* the base
  of that allocation.
- **`stream_auto`'s eviction is a resume, not a stop.** `PlayQueueReturn()` puts the
  loser back on the wait queue with `SndStreamGetNowOffset()` latched into `offset`,
  unless `reset_flg` says start over.

Six ROM bugs found and reproduced, all commented at the site:

| Where | What |
|---|---|
| `SndBankIsLoopSnd` | bounds-checks `no` then never indexes with it — always answers for sample 0 |
| `SndStreamFadePitch` | passes `(wrk->pitch, pitch)` to `SndGetFrameAddVol` instead of `(pitch, wrk->pitch)`; the step is negated, so a timed pitch fade walks away from its target |
| `SndPCMStreamIsPreload` | assert condition inverted — fires for every slot that *is* in use |
| `GetSPUMemory` | the "no free SPU_BUF" path returns without `SignalSema()` |
| `PrintSOUND_INFO` | the `<3D>` line tests `attr.loop`, not `attr.s3d` |
| `SndSetEffect` | neither branch writes `r_attr.delay`/`feedback`, and the same-mode branch leaves `end_adrs` uninitialised |

Layout notes this batch adds to [[float4-members-lose-quadword-alignment]]: `AUTO_BD_WRK`
needs **both** an interior 8-byte gap before `pos` and an 8-byte tail;
`SND_3D_LISTENER` needs 12 bytes of tail; `STREAM_QUEUE` needs 4. All three come from a
leading or embedded `sceVu0FVECTOR`, and all three were caught by the harness, not by
reading.

### ee_iop.o — done

All 14 `ZERO2.MAP` `.text` symbols plus the static `ee_iopInitSub` and all seven globals.
This is the module the sound stack hangs off: **nothing in snd.c has a caller anywhere
else**, so until `ee_iopGetNeedSize`/`ee_iopInit`/`ee_iopMain` were written the whole
sound layer linked as dead code.

- **The three sound hooks are the point of the file.** `sndGetNeedSize()` at
  `ee_iopGetNeedSize+0x10`, `SndInit()` at `ee_iopInit+0x364` (line 241), `SndMain()` at
  the top of `ee_iopMain()` (line 258). Both halves of the size query matter: one work
  buffer serves `ee_iopInitSub()` (which calls `FileLoadInit`) and then `SndInit()`, which
  carves the sound tables out of the cursor the first returns.
- **`ee_iopMain()`'s status copy is one struct assignment**, not a loop. GCC unrolled
  `iop_ret = *(IOP_RET_STATUS *)iop_ret_buffer` into thirteen 0x20-byte chunks plus an
  8-byte tail (0x1a8 total), which decompiles as a walk over `stream_ret`/`pcm_stream_ret`.
- **PORT DEVIATION, deliberate and flagged:** `ee_iopInit`'s IOP bring-up — reboot from
  IOPRP, HIL index read, DIL image push, per-IRX `sceSifLoadModuleBuffer` — is
  reconstructed and compiled but gated behind a `static const int ee_iop_boot_iop = 0`.
  The ROM treats any failure there as fatal (`return 0`), and on the host every step must
  fail (the images are at `cdrom0:\ZERO2IRX.HIL;1`), which would take the EE-side init
  down with it. A runtime `if` rather than `#if 0` so the block stays type-checked.
- New SDK shims for it, all succeed-and-do-nothing: `sceSifInitRpc`, `sceSifCheckStatRpc`
  (sifrpc), and `sceSifRebootIop`, `sceSifSyncIop`, `sceSifLoadFileReset`, `sceFsReset`,
  `sceSifLoadModuleBuffer`, `sceSifInitIopHeap`, `sceSifFreeIopHeap`, `sceSifLoadIopHeap`,
  `sceSifQuery{Max,Total}FreeMemSize` (sifdev). `HIL_FORMAT` / `HIL_ONE_FORMAT` moved into
  `ee_iop.h`.
- One new stub, greppable as `STUB: needed by ee_iop.c`: `file_stream.c/h`'s
  `FileStreamInit`. `file_stream.o`'s other four exports have no reconstructed callers.

`iop_com_buffer` and `iop_ret_buffer` are 64-byte aligned on the host as they are in the
ROM — the latter is read back through an `IOP_RET_STATUS *`, so that is not decorative.

**Not yet run in-game.** Every one of these bodies was an empty stub or absent before
this, so nothing in the game has ever made a sound; `SndMain()` now does real per-frame
work on six subsystems.

### subtitle.o — done

All 9 `ZERO2.MAP` `.text` exports plus 7 statics, verified 9/9 and 7/7. `.text` is
accounted for byte-for-byte (0x263fb8..0x264690 = 0x6c0 of code plus six 4-byte
alignment fills) once the fixed_array.h boilerplate at the head of the object is set
aside. `SUBTITLE_MSG_DATA` (0x4) and `SUBTITLE_DATA` are confirmed by an `offsetof`
harness, `SUBTITLE_CTRL` too modulo the one pointer, and the PAL/NTSC counter ladder is
driven against a transcription of the disassembly over its whole domain (79M cases, zero
mismatches). It is the **spoken-line caption**: one at a time, a voice stream, a mouth,
and a list of (message, frame) pairs walked in step.

- **The file has no static data at all.** Its `.data` is `SubTitleCtrl`, all zeroes; its
  `.rodata` (0x56) and `.sdata` (0x40) hold nothing but the compiler's own
  `fixed_array<%s,%d>` assert literal and the `"void*"` / `"char*"` / `"unsigned int*"`
  type names. Reading them out of the ELF is what settles that — the sizes look like
  there ought to be a table.
- **The subtitle file is a self-relative table**, the same shape as `SGDSELF32`:
  `GetSubTitleAddr()` gives the base, the first 250 words are byte offsets from that same
  base. The ROM's add is 32-bit; on the host it has to stay pointer arithmetic.
- **`SubTitleCtrlStructInit()` leaves `ObjType`/`ObjId` alone** — five stores, not seven.
  Harmless, because `SubTitleReqSub()` rewrites both on all three of its paths.
- **A `-1` message id parks the caption, it does not end it.** The counter stops and the
  stream ending is what tears the slot down, which is how a caption goes blank before the
  voice has finished.
- **`SubTitleMain`'s `DrawFlg` suppresses only the drawing.** The stream, the mouth and
  the line counter all keep running.
- **Voice streams go out at `STREAM_PRIORITY_EVENT` (17), not `_SUBTITLE` (15)**, which
  nothing in the build uses. The header file is always the stream's CD file minus one —
  same convention `enemy.c` uses for a ghost's own line.
- **PAL scales the authored frame counts by `Frame * 5 / 6`**, and GCC cross-jumped the
  two identical reset tails, so only the NTSC copy carries `$LM` markers (250/251); the
  PAL copy's own 241/242 were eliminated. The +9 line offset between the two branches is
  what proves they were written out in full.
- **`SubTitleStreamFileNoGet()` does not null-check `SubTitleDataPtrGet()`.** An
  out-of-range number faults; `enemy.c`, its only caller, asserts the *result*.
- **Only two `CharId`s lip-sync** — 1 through the sister model helper, 6 as a model
  number looked up directly. Everyone else talks with a closed mouth.

Two debug-info lessons this module is a clean example of:

- **The `$LM` that appears *before* a `PROC`/`STATICPROC` record is the function's
  opening brace line**, and `symbols.txt` interleaves them in address order, so it gives
  the opening line and the first statement separately — which Ghidra's `; Line` list
  cannot. That is what pinned every function here to the line, including the ones whose
  first marker is four lines into the body.
- **GCC 2.96 does not propagate a constant out of a branch condition into another
  register.** `SubTitleMimReq`'s `CharId == 6` arm calls `motSearchANI_CTRL` with *no*
  `li a0` at all, while the `CharId == 1` arm materialises its literal — so the argument
  is the parameter, not a `6` the compiler happened to know. Ghidra prints `(6)` for both.
  See [[branch-condition-does-not-constant-propagate]].

One new stub, greppable as `STUB: needed by subtitle.c`:

| Module | Stub | Effect while stubbed |
|---|---|---|
| `movie_title.c/h` | `MovieTitleDispMain` | **no caption is ever drawn** — everything else (stream, mouth, timing) runs |

`movie_title.o` is the natural next target: it is six exports, and `MovieTitleDispMain`
is the only thing between this module and a visible subtitle. It needs
`MovieTitleBaseDisp`, `movie_title_base_tex[3]` and `every_disp_subtitles[33]`
(`movie_title_dat.o`) alongside it.

**Not yet run in-game.** Every body was an empty stub before this, so `SubTitleMain()`
never advanced and `ev_macro.c`'s four `SUBTITLE_*` opcodes did nothing.

### The eight finder widgets — done

`bonus_shot.o`, `center_circle.o`, `center_cross.o`, `damage_disp.o`, `ene_life.o`,
`film_no.o`, `hp_bar.o` and `search_mark.o` — 49/49 `ZERO2.MAP` `.text` symbols, all
seven data tables byte-identical to the ROM (diffed out of the compiled `.obj`s), all
thirteen struct sizes and every member offset confirmed by an `offsetof` harness, and
all seven `.lit4` floats matching bit-for-bit. With these, everything
`CNPlyrCamera::Draw()` calls per finder frame is real code.

- **Every widget's alpha expression is repeated at each use site, not held in a local.**
  The stabs are exhaustive for `int`s, and `CDamageDisp::Draw` names only `ds` while
  using `iAlpha * mOneBlink.Get() / mOneBlink.GetMax()` twice; `CFilmNo::Draw` emits
  variable.h line 657 three separate times in one prologue. GCC CSEs them into one
  callee-saved register, which reads like a local but is not.
- **`/ 128` and `/ GetMax()` are distinguishable even when GetMax() is 128.** A literal
  power-of-two divisor strength-reduces to a shift (`CHpBar::Draw`); the same value
  through the inline accessor keeps a real `div` and its divide-by-zero trap
  (`CSearchMark::Draw`). Same tell as the `SetAddVal` note above, and it settles which
  form the source used.
- **`CCenterCross` interpolates by counter, not by position.** Each mark's `cnt` climbs
  to 10 while its ghost qualifies and falls back when it does not; `Draw()` places the
  mark `cnt/cmx` of the way from the crosshair to the stored target. That is why `Work()`
  never needs to know which ghost it had last frame.
- **`CCenterCircle::FrameReset()` and `::SetMode()` are genuinely empty** — eight bytes
  each. The mode is derived from the four flags inside `Work()`, and every flag is
  re-driven every frame by its owner, so there is nothing for either to do. `Work()`'s
  ripple test is `(u)(mMode - 2) < 2`, i.e. mode 3 as well as 2 — and mode 3 is
  unreachable.
- **`CEneLife` is three values, not one**: the bright bar chases at 0.04/frame, the red
  tail at 0.005 after a 35-frame hold. `Set()` snaps all three (a different ghost),
  `Decrease()` snaps only the bright one (the same ghost, hit).
- **Two ROM bugs, reproduced.** `CBonusShotOne::Draw` computes `ds.scw = fXScl / fXScl`
  — the underline scale divided by itself, so the rule is always full length and NaN-wide
  on any frame the counter is still zero (verified against the `div.s $f20,$f20,$f20`
  encoding, not just the decompiler). And `CBonusShot` sets `mOldScoreAlpha` to 128 on a
  `<char,0,127>`, which stores as -128; the linkonce `Work()` reads `mValue` with `lb`,
  so the next tick clamps it to 0 and the old total flashes for one frame instead of
  fading.
- **`CFilmNo` draws the count as two dashes for Type-07** (the unlimited film) and a word
  plate for the unnumbered fifth. Both readouts shift left in German
  (`GetLanguage() == 2`); `bonus_shot`'s two combo plates shift in Italian (`== 4`).

New in `common/variable.h`, both measured rather than inferred: `CWrkVariable::LoopWork()`
(transcribed from the linkonce `<char,0,100>` body at 0x2b0d60 and driven over 256×37
input pairs) and `CWrkVariable::GetState()` (2 rising / 0 at Max / 3 falling / 1 at Min —
*not* `CFadeVariable::GetState()`'s numbering). `CBlinkVariable::GetMax()` is variable.h
line 581, pinned by damage_disp.o.

`CENTER_CROSS`, `MyPoint` and `SHOT_NAME_TEX` are now real types in `m_plyr_camera.h`;
`CCenterCross` no longer carries opaque storage.

**Not yet run in-game.** Every body was an empty stub.

### title.o — done

All 56 `ZERO2.MAP` `.text` exports plus the 5 statics, verified 56/56 against the map,
and `.text` is accounted for byte-for-byte: 0xe38 of code plus 0x90 of 8-byte alignment
fill = the section's 0xec8, with no unlisted body. `TITLE_WRK` (0x14) and
`TITLE_DISP_CTRL` (0x8) are confirmed member-by-member by an `offsetof` harness. It is
the title mode: a four-state asset loader, the title BGM stream, and the init/one/end
brackets for every phase underneath `GID_TITLE_MODE`.

- **The file has no static data at all.** Its `.rodata` (0x56) and `.sdata` (0x4e) hold
  nothing but the `fixed_array<%s,%d>` assert literal and the `void*`/`char*`/
  `unsigned int*` type names — the same signature `subtitle.o` has. Read them out of the
  ELF before assuming a table exists; the sizes look like there ought to be one.
- **The logo pak is `TITLE_LOGO_PK2 + GetLanguage()`, computed fresh at all five use
  sites and never clamped.** There is no `TitleLogoFileNo()` helper; the earlier pass
  invented one, along with a `language > 4` clamp that the ROM does not have.
- **`TitleMain()`'s step 0 re-requests every outgame screen's assets**, not just the
  title's — 15 calls across loadgame, setup, option, gallery, game_data_save, album and
  menu_cam_main. The title is the only point where all of them are guaranteed off the
  heap, so each sub-screen's background is claimed here and stays resident.
- **`SndBankSetLoadPriority(5)` in `init_Title_Mode` is undone by `TitleLoadWait()`**,
  from `TITLE_WRK::iOriginSndBankLoadPriority` that `TitleWrkInit()` latched. Nothing
  else would ever lower it again.
- **`TitleTexLoadReq()` has no null check** — a failed `GetTitleTexMem()` posts a load to
  address 0. Kept as found.
- **`one_Title_Top` and `one_Title_Menu` are switches, not if/else chains.** Both
  comparisons sit back-to-back under a *single* `; Line` marker and `case 2` falls
  through into `case 3`; an `if (step == 2) … if (step == 2 || step == 3) …` would give
  the second test its own line note. `pre_Title_Setup`'s plain single `if` is the
  contrast that settles it.
- **`pad[0].one & 0x400` on the title top is R3**, not L1 — the remapped layout. It
  forces the attract movie, alongside the idle timer.
- **The BGM re-play idiom appears five times** (`menu`, `newgame`, `loadgame`, `option`,
  and `title_mode`'s own start): `StreamAutoPlay(BGM000_TITLE_STR, …, loop = 0)` whenever
  `StreamAutoIsPlaying()` says it stopped. Album and gallery swap in their own stream
  with `loop = 1` and put the title's back on the way out.

Reconstructed alongside it, because title.o drives them every frame and the earlier pass
had them inside title.c:

- **`title_top.o`** — 2/2 exports plus 2 statics. `TitleTopPad()` is START (`paddat[7]`)
  or CROSS (`paddat[0]`); `TitleTopPressStartDisp()` tints `title_top[14]` with the
  shared pulse and blends it additively.
- **`title_menu.o`** — 3/3 exports plus 4 statics and `title_tbl[8][2]` (byte-identical
  to the ROM). The menu is **horizontal**: LEFT/RIGHT move, and `title_left_x_tbl` /
  `title_right_x_tbl` place the arrow pair per item per language. **`DispTitleMenuItem`'s
  three parameters are all dead** — neither the sprite path nor the ASCII path reads
  `off_x`, `off_y` or `alpha`. Its two `SetASCIIString2` calls are cross-jumped, which is
  why one shared tail carries two line numbers; `functions.txt` listing no `char *`
  local is what proves they are two separate calls rather than one call with a hoisted
  string.
- **`title_album.o`** — 3/3, all forwarding to album.o.
- **`title_disp.o`** — 6/6 exports reconstructed (logos, both cursors, caption). Its
  background half is **not** done: `DispTitleBack` plus six statics
  (`DispTitleBgPattern1/2`, `DispTitleBgCloud1/2`, `DispTitleBgShadeingOffFlea1/2`) and
  nine `.rodata` animation tables at 0x3e6920..0x3e6aa0. The earlier pass's approximation
  is kept there under a `NOT RECONSTRUCTED` banner.

New stubs, all greppable as `STUB: needed by title.c`:

| Module | Stubs | Effect while stubbed |
|---|---:|---|
| `newgame.c/h` (new file) | 3 | new-game phase draws nothing and cannot be left |
| `framerate.c/h` (new file) | 2 | frame-rate phase likewise; the menu's UP/DOWN shortcut reaches the same setting |
| `setup.c/h` | 3 | setup phase inert |
| `option.c/h` | 3 | option phase inert |
| `gallery.c/h` | 3 | gallery phase inert |
| `album.c/h` | 3 | `AlbumMain()` **returns 0, not 1** — a 1 would make the album phase un-enterable |
| `title_movie.c/h` | 2 | `CheckMoveTitleMovie()` **returns 0** so the title never falls into the unreconstructed attract movie |

**Not yet run in-game**, but this is the first change that makes the title screen do
real work: `init_Title_Mode()` now starts the title BGM stream and the sound bank, the
loader actually gates on the file loads, and the top/menu screens are driven every frame.

### outgame/ — all 28 TUs done

The whole folder was surveyed and worked end to end. **Every one of its 28 ROM
translation units is reconstructed and verified against `ZERO2.MAP`** — 28/28 on
exports, with no stubs left anywhere in the folder.

| Done this pass | exports | Notes |
|---|---:|---|
| `title.o` | 56 | see its own section above |
| `title_top.o` / `title_menu.o` / `title_album.o` / `title_disp.o` | 2/3/3/6 | ditto |
| `title_movie.o` | 8 | attract-movie idle timer + BGM keep-alive |
| `framerate.o` | 2 | 50/60 Hz toggle |
| `newgame.o` | 3 | difficulty select + black-out hand-off |
| `autoload.o` | 3 | boot-time system-file probe, nine states |
| `lang_check.o` | 3 | the same probe trimmed to the language byte |
| `lang_sel.o` | 8 | five-flag select **plus** the language setting itself |
| `gallery.o` / `gallery_disp.o` | 10 / 4 | eight-row unlock menu, picture viewer, ending movies |
| `option.o` / `option_disp.o` | 12 / 7 | four pages, two modal windows |
| `setup.o` | 29 | the mission mode's loads and all six phase callbacks |
| `mission_ctl.o` | 24 | mission scoring and the four result screens |

Already complete before this pass and re-verified: `SpriteCmn.o` (13),
`chapter_sel.o` (3), `loadgame.o` (8), `logo.o` (12), `outgame.o` (14),
`pad_check.o` (3).

**Nothing left stubbed.** `setup_menu.o`, `mission_sel.o`, `mission_disp.o`,
`mis_sel_disp.o` and `mission_pause.o` each have their own section below; the
last of them closes the folder.

Facts worth carrying into the rest of the folder:

- **Every outgame screen is built from the same five pieces**: a `*_CTRL` work
  block reached through a `*c` pointer, a `now_place`/`next_place` pair indexing
  two parallel dispatch tables (`XxxCtrlModule[]` / `XxxDispModule[]`), an
  `anm_step`/`anm_alpha` cross-fade whose *animation* function performs the place
  change at the bottom of the fade-out, a `now_tex` cache in front of
  `PK2SendVram()`, and a `Get/Liberate/LoadReq/LoadWait/LoadCancel` texture
  quintet. `GalAnimation()` and `OptAnimation()` are the same function twice.
  Recognising the shape makes each new screen mostly transcription.
- **A `place` value one past the last module means "exit".** Both gallery and
  option have a 4-slot table whose last entry is NULL and is never called.
- **Display helpers routinely ignore their `off_x` / `off_y` / `alpha`
  parameters.** `DispTitleMenuItem`, `FrameRateSelTitleDisp`,
  `FrameRateSelCursorDisp`, `NewGameItemDisp`, `NewGameCursorDisp`,
  `GalDispTitle` and `LangSelBlackBgDisp` all take three and read none. Do not
  invent uses for them.
- **The pad idiom is fixed**: `(pad[0].rpt & mask) || GetPadAnalogRpt(n)` with
  0x1000/0 up, 0x4000/1 down, 0x8000/2 left, 0x2000/3 right, and `*paddat[0]`
  CROSS / `*paddat[1]` TRIANGLE for confirm and cancel. A screen that reads
  `pad[0].one` instead of `.rpt` wants a single press, not auto-repeat.
- **`ld` of `pad[0]` at 0x180 masked with `0xa00000000000` is
  `(one & 0x2000) | (one & 0x8000)`** — GCC merged two 16-bit tests into one
  64-bit load. It appears in `ReplaceONOFF()` and `OptVerify()`.
- **A run of `ld`/`sd` from `.rodata` into consecutive stack slots is a local
  array initialiser**, and several of these files have three or four in one
  function (`GalleryTopPad`, `GalDispMenuCsr`, `OptDispAHeadText`,
  `OptDispButtonSetupText`). GCC merges adjacent ones, so Ghidra will render
  `mov_no[cursor-3]` as `pic_mode[cursor+1]`; recover the split from the stack
  offsets. See [[rodata-blob-into-stack-is-a-local-initialiser]].
- **`ingame/mission.c` was an aggregation stub for three real objects** —
  `mission_ctl.o`, `mission_disp.o` and `mission_pause.o`. It is now empty and
  `ingame/mission.h` forwards to the three real headers. That is the eighth
  instance of the stub-in-the-wrong-file hazard.
- **`lang_sel.o` owns `LangData_LoadReq`/`LangData_LoadWait`/`LoadLangSetUp`**,
  which `lang_check.o` calls directly. The earlier pass had them as statics in
  `lang_sel.c`, so `lang_check.c` could never have linked against them.
- **`init_LangSel_Main()` is where the outgame heap is created**
  (`ol_loadHeapReset(0x5a6c00, 0x7a8000)`). Everything after it claims out of
  that block, which is why `TitleMain()` can free and re-request every outgame
  screen's assets in one go.

New data files under `outgame/tim_dat/`, all read straight out of `.data` and
byte-checked against the ROM: `lang_sl_dat.c` (17 sprites), `gallery_dat.c`
(118), `option_dat.c` (231 sprites + 21 quads), `setup_dat.c` (55), plus
`newgame_*_x_tbl` added to `title_dat.c`.

New stub modules created along the way, all greppable by `STUB:`:
`system_data_save.c/h` (4), `mission_disp.c/h` (17), `mission_pause.c/h` (3),
`mis_sel_disp.c/h` (6), plus additions to
`mc_check_card`, `movie`, `album`, `clear_flg`, `effect_oth`, `zero2_anim2d`,
`menu_cam_main` and `game_data_save`. Two return deliberately non-default
values and say so at the site: `AlbumMain()` returns 0 ("still open", so the
album phase is enterable) and `MenuCamMain()`/`GameDataSaveMain()` return 1
("backed out", so their phases are not dead ends).

**Not yet run in-game.** The build is clean, but nothing here has been launched.

### mission_pause.o — done, and the folder with it

All 3 `ZERO2.MAP` `.text` exports plus 13 statics, verified 3/3 against the map
and 16/16 against `functions.txt`. `.text` is accounted for byte-for-byte
(0x215100..0x215e00 = 0xccc of code plus thirteen 4-byte alignment fills), both
`SQAR_DAT` local initialisers and the message-id table decode exactly to the
ROM's `.rodata` bytes, and every float — the `.lit4` 550.0 plus ten `lui at`
immediates — round-trips bit-for-bit.

It is the mission-mode pause menu: three rows over a still of the frame the game
was on, a "return to the mission list?" window, and the pad-disconnected notice.

- **`MisPauseMain()` always returns 0, and `ingame.c` draws the menu only on 0.**
  The stub returned 1 with a comment guessing that meant "leave the pause menu";
  it actually meant the pause screen has never drawn a pixel.
- **The dimmed game behind the menu is a still, not a paused render.**
  `MisPauseInDispCaptuer()` blits the front buffer into a VRAM scratch at
  0x2bc0 once, and `MisPauseCaptureDataDisp()` blits it back every frame —
  which is why the menu can run with the whole ingame draw path stopped.
  Note the copy *type* is 0 here against `ingame.c`'s 1, and the scratch address
  differs too (0x2bc0 against 0x3aa0): the two pause screens have separate
  capture buffers.
- **The vibration row is live, not a setting to confirm.** Toggling it calls
  `OptionVibChange()` straight away and parks 20 frames of `VibrateRequest()`,
  which is what the counter at the bottom of `MisPauseMain()` is for.
- **Losing the pad forces step 2 from the top of `MisPauseMain()`**, whatever
  else was open, and `before_step` is how the notice knows whether to put the
  return-title window back. `MisPauseDispMain()` draws both when it has to.
- **START resumes and SELECT drops into `GID_STORY_DEBUG`** — `key_now[12]` and
  `key_now[13]`, the remapped layout.
- **The "no" and "cancel" answers share a tail.** Both land on the same pair of
  stores at lines 332/333, which is why they sit after the if/else rather than
  inside each arm.
- **`PAUSE_CTRL` was already in `ingame/pause/prg/pause.h`**, put there by an
  earlier pass precisely because this file declares its own instance of the same
  layout (bss 4b6448 against pause.c's 4bbaf8). Declaring it again here is a
  conflicting-typedef error; include that header instead.

**`outgame/` is now 28 of 28.** A sweep of the whole folder reports every TU
complete on its `ZERO2.MAP` exports. Two residuals are known and neither is an
export: `mission_sel.o` is 43/44 on `functions.txt` (the entry is the
`PLYR_ITEM` type_info node, a compiler artifact), and `title_disp.o` is 7/13 —
all six exports done, the six missing entries being the file-local title
background helpers still marked `NOT RECONSTRUCTED`.

**Tool fix carried with it:** `verify.py` sliced `ZERO2.MAP` with a bare
`obj in line`, so `title.o` matched `movie_title.o` and reported *that* object's
six exports as missing from title.c — a clean 56/56 file reading as 0/6. It now
anchors on `endswith('/' + obj)`. Any object whose name is a suffix of another's
was mis-sliced before this.

**Not yet run in-game.**

### mis_sel_disp.o — done

All 6 `ZERO2.MAP` `.text` exports plus 10 statics, verified 6/6 against the map
and 16/16 against `functions.txt`. `.text` is accounted for byte-for-byte
(0x212640..0x2136b0 = 0x1054 of code plus seven 4-byte alignment fills), all
eight `.data` tables plus both `.sdata` ones and the mini menu's `.rodata` blob
are byte-identical to the ROM (diffed out of the compiled `.obj`), every one of
the eight `.lit4` floats round-trips bit-for-bit, and the achievement-counter
digit walk and the caption cross-fade are driven against transcriptions of their
disassembly over their whole input domains.

It is the mission-select screen's drawing half — six list rows out of 25 with
their ranks and best times, the three achievement counters, the scrollbar, the
cursor, the slide-out Album/Save menu, and the caption window.

- **The object has no `_fixed_array_*` boilerplate at all**, which is why its
  16 `functions.txt` entries are 16 real functions rather than 12.
- **`MisFadeNew` and `MisFadeOld` are -1 in `.sdata`, not 0**, and it matters:
  `MissionSelInit()` opens with `MisFadeSetMsg(-1)`, which on a zeroed pair
  would report "changed" and reset the fade counter. Another
  [[zeroed-statics-lose-rom-initialisers]] — the earlier stub had them zeroed.
- **A missing number prints as dashes, not zeros.** `PrintNull_K()` draws `n`
  copies of message 3 from bank 8, and `SpCmnPrintNumber_NK2()` is the wrapper
  that picks between it and `SpCmnPrintNumber_NK()` on the sign of the value.
  That is how a never-cleared mission shows `--:--:--`.
- **Zero is the *last* glyph in the achievement counter's run**, not the first:
  1..9 are 0x41..0x49 and 0 is 0x4a. One unsigned compare — `(u)(digit - 1) >=
  9` — covers both "digit is zero" and "digit is out of range".
- **`MissionDrawTassei()`'s `iRank` is a grade, not a row**: 0, 1 and 5 pick the
  three column triples in `iPos[3][3]` and the three suffix plates, and any
  other value draws nothing at all.
- **The mini menu is one plate drawn twice, mirrored.** A negative `scw` opens
  the left half and a positive one the right, both about the same centre, so
  `fMove` scales them apart symmetrically. When the slide is over (`iFlg == 0`)
  the ROM stops scaling and drives the alpha instead — which is what the two
  writes to `fMove` and `ucMstAlpha` at lines 404/405 are.
- **`MissionDrawSelect()`'s loop has a redundant second bound.** It is
  `(i < 6) && (i < 25)`, testing `i` both times rather than `iTopID + i`;
  reproduced as found.
- **`MissionCaptionDisp()` ignores both its offsets** — another of the folder's
  display helpers that takes them and reads neither — and lines 235..275 hold no
  code, a forty-line span of comment or disabled draw between it and
  `MissionDrawCsr()`.
- **`MissionDrawTime()` walks its own `iOffX` parameter** between the three
  fields rather than recomputing, the same shape `MisDispNum()` has in
  `mission_disp.o`.

The float literals are a clean run of [[ee-gcc-truncates-float-literals]]: every
one of 21.45, 20.35, ±(9/13), 1.3 and 2.4 sits one or more ulp under the decimal
the source wrote, so they are spelled `21.4499989f`, `20.3499985f`,
`0.692307651f`, `1.29999995f` and `2.39999986f` here. 471.0 and 361.0 are exact.

One `objdiff` DIFF is expected and not a fault: `MissionDrawScrollbar()`'s
`int iMovY[2] = { 129, 261 }` is a stack array whose 8-byte initialiser the ROM
kept as a `.sdata` blob (small read-only data lands there under `-G`, not in
`.rodata`) while MinGW materialises two immediates. The values are the ROM's.

**With this the mission-select screen draws.** `mission_sel.o` has been driving
a blank page since it was reconstructed; every one of its draw calls now lands
on real code. `mission_disp.o`'s in-mission timer readout also starts working,
because `MissionDrawTime()` was the one thing it was still missing.

**Not yet run in-game.**

### mission_disp.o — done

All 17 `ZERO2.MAP` `.text` exports, verified 17/17 against the map and 17/17
against `functions.txt`; the object has **no statics at all** beyond its three
timer variables and no work block, so the export list is the whole file. `.text`
is accounted for byte-for-byte (0x2141f0..0x215100 = 0xef4 of code plus seven
4-byte alignment fills), all six tables are byte-identical to the ROM (diffed out
of the compiled `.obj`), and `MisDispGetAnimAlpha()` is driven against a
transcription of its disassembly over every table in the build and the whole
timer domain.

It is the mission-mode HUD and the four result screens — clear, all-clear,
all-clear-at-S and failed — plus the start banner.

- **Everything animates off one counter.** `MisDispTimer` is zeroed by each
  `*Init()` and advanced by each draw (capped at 1000), so the banner and all
  four result screens share it and none of them needs state of its own. The
  in-mission readout is the exception: `MisDispTimerCnt` is a real frame count,
  and `MisDispTimeProc()` is the only thing that advances it — it counts frames
  the readout was *enabled* for, not frames the mission was live for.
- **`MisDispGetAnimAlpha()` is `Anim2D_CalcNowAlpha()` with both ends held**,
  and its terminator test is on `start_alpha == -1`, not the `start_time == -1`
  the rest of `anim_2d.h` documents. Before the first segment it returns the
  opening alpha, after the last one the closing alpha; only in between does it
  call the interpolator. That is why mission_ctl.c's result-screen cross-fade
  parks rather than extrapolating.
- **`iDatList[5][3]` is the clear screen's schedule**: `{first sprite, last
  sprite, start offset}` with offsets 0, -10, -20, -40, so the four rows arrive
  ten frames apart on one shared 15-frame fade. It is the file's only `.data`
  table — the five `ALPHA_ANIM_TBL`/`SCL_ANIM_TBL` tables are all `.rodata` — so
  it is the one that was declared without `const`.
- **A mission cleared before shows no prize row.** `MissionGetStat(id, 0) >= 2`
  drops both the fourth `SpCmnDrawRange` and the prize/rank block, which is what
  the two extra tests around row 3 are for.
- **`MisDispClear()` keeps the row alphas in `ucAAlpha[8]`** because the second
  pass — the "new record" markers — has to fade in step with the row each one
  annotates, and the first pass has already advanced past them.
- **Two signatures the earlier pass had wrong**, both now from `functions.txt`:
  `MisDispNum(iNum, iKeta, iOffX, iOffY, ucAlpha, iFlg)` takes the digit count
  *before* the offsets, and `MisDispTime(iHour, iMin, iSec, iOffX, iOffY,
  ucAlpha)` takes h/m/s rather than a packed frame count — the same shape
  `mis_sel_disp.o`'s `MissionDrawTime()` has.
- **`MisDispNum()` has no working locals**: it walks `iNum` and `iOffX`, its own
  parameters, which is why `functions.txt` lists only `int i` for a function
  with two obvious accumulators. `functions.txt`'s *register* column is what
  settles it — `iOffX` is s2, the decrementing cursor.
- **`MisDispClear()`'s h/m/s are block-scope**, declared inside the switch's
  first case; `functions.txt` lists no such locals but the stack slots are
  there. See [[stabs-lbrac-reveals-block-scope-locals]].

`ingame.c` calls `MisDispTimeProc()` every frame and `MisDispSetFlg(3)` on
mission start, so this is live code the moment a mission runs.

**Not yet run in-game.** The timer readout was blocked on `MissionDrawTime()`,
which belongs to `mis_sel_disp.o`; that is now reconstructed too, so all three
of the in-mission readouts draw.

### mission_sel.o — done

All 26 `ZERO2.MAP` `.text` exports plus 17 statics, verified 26/26 against the map
and 43/44 against `functions.txt` (the one absent entry is the `PLYR_ITEM`
type_info node, a compiler artifact). `.text` is accounted for byte-for-byte
(0x215e00..0x218a00 = 0x2b94 of code plus twenty-seven 4-byte alignment fills),
`MISSION_TBL` (0x24), `MISSION_SEL_CTRL` (0x8) and `MISSION_SEL_DISP` (0x2) are
confirmed by an `offsetof` harness, and the frame<->h/m/s pair is driven over its
whole range in both video modes.

It is Mission Mode's spine: the 25-mission table, the record each mission keeps,
the list screen, and the save/restore that lets a mission run on top of a story
game without touching it.

- **`MissionTblList[25]` is in `.bss` with a 0xe04-byte dynamic initialiser**,
  not in `.data`, because ten of its entries build `iRank[]` out of
  `MissionSetTimePal()` calls. Fifteen entries are constant 0x24 blobs in
  `.rodata` at 0x3c0710 (stride 0x28, 8-byte aligned) that the helper block-copies
  straight in; the other ten are assembled on the stack and copied. The rank
  thresholds therefore depend on what `GetPALMode()` answers *at construction
  time*.
- **`sType` is which `MissionList[id][]` slot scores the mission**, and the two
  halves of the table agree with it exactly: every `sType == 1` entry has an
  *ascending* `iRank[]` (a time, lower is better) and every 2 or 3 a descending
  one. That is the split `MissionGetRankPoint()` keys on, and it is the check that
  proved the recovered table right.
- **Two file-local `inline`s have no symbol and no out-of-line body**, so they
  show up only as their own source lines appearing inside every caller: the
  scratch-buffer push/pop at lines 523/524 and 530-533 (four Push functions plus
  both Reset functions), and the item-slot setter at 636-638. Names in the port
  are the port's; the ROM's are not recoverable.
- **The Push/Pop pair is what survives a mission.** `MissionKeepSaveData()` parks
  the *whole* game state in one heap buffer, and `IngameWrkInit()` then wipes the
  live state -- so the camera upgrades, the equip-tray gauge, the mission records
  and the level gems are marshalled out and back around it by hand.
- **`MissionCheckEnd()` is "is the cursor at an end of the list"** (1 top, 2
  bottom), not "is the mission over". Six visible rows out of 25 puts the last
  window at `MissionListTop == 19`.
- **`MissionSetStat()` seeds `ret` at 1 before the bounds check**, and the store
  that raises the state sits in a branch delay slot, so both of the tests that
  pick 2 over 1 read the *old* value. Written any other way the first clear
  reports 1.
- **`MissionSelMain()`'s switch writes `case 3` before `case 2`** -- the jump
  table at 0x3c06e0 settles which is which, and the line numbers (1012-1023 then
  1028-1030) settle the source order. `SetMissionSelNextPhase()` likewise has no
  `case 1` at all, with a twelve-line gap where one would sit.
- **`MissionSelInit()` only parks the story game on the way *in*.** Coming back
  from the album or the save screen `CheckIngameMission()` is already 1, so the
  cursor keeps its place and nothing is marshalled twice.
- **`MissionGetTassei()` returns a percentage**, 4 per cleared mission, and its
  `iCnt != 0` guard is redundant -- `iCnt * 4` is already 0. Reproduced as found
  (one line, one `movz`).

Recovering a `.bss` table with a dynamic initialiser is worth the tooling: the
static-init helper is straight-line (only the two `__initialize_p` guards branch),
so a ~120-line emulator over `lui/addiu/li/move/sb/sh/sw/lw/ldl-ldr/sdl-sdr/jal`
recovers all 25 entries with the `MissionSetTimePal()` calls kept symbolic. Three
traps in writing one: EE is **little-endian** on both the load and the store side;
a `jal`'s **delay slot runs before the callee**, so the pair has to be swapped or
every argument set there is lost; and the helper's own `a0`/`a1` must be seeded
(1, 65535) because GCC reuses `a0` wherever a 1 is wanted. All 15 constant blobs
round-trip byte-for-byte through the compiled `.obj`.

New alongside it: **`outgame/tim_dat/mission_dat.c/h`** -- `mission_tex[174]`
(data 32d3a8, 5568 bytes byte-identical to the ROM), the sprite bank the whole
mission mode draws from.

`mis_sel_disp.o`'s **`MisFadeSetMsg()`** is reconstructed too, with its three
statics: it returns 1 only when the caption message actually changed, and
`MissionSubSelect()` gates the cursor cue on that -- a `void` stub silences the
whole list. Its header also had three wrong signatures from the earlier pass
(`MissionDrawTime()` takes h/m/s plus an offset, not a packed time;
`MissionDrawSelect()`'s second argument is the list top, not the cursor).

**Not yet run in-game.** Every body was a stub, so the mission list drew nothing
and `MissionGetType()` returning -1 made `mission_ctl.o` fall back on "cleared, no
rank" for every mission. The screen now runs, but `mis_sel_disp.o` still draws
nothing, so it is a working machine behind a blank page.

### setup_menu.o — done

All 3 `ZERO2.MAP` `.text` exports plus 33 statics, verified 3/3 against the map
and 36/36 against `functions.txt`. `.text` is accounted for byte-for-byte
(0x2564d0..0x259a80 = 0x3574 of code plus fifteen 4-byte alignment fills, so
there is no unlisted body), all twelve tables are byte-identical to the ROM
(diffed out of the compiled `.obj`), and `SETUP_MENU_CTRL` (0x10),
`SETUP_MENU_DISP` (0xc) and `COSTUME_TWIN_TBL` (0x8) are confirmed by an
`offsetof` harness. At 0x35b0 it was the largest TU in `outgame/`.

It is the screen `GID_TITLE_SETUPMENU` draws over setup.c's background: three
rows on the left (Story / Mission / Exit) and, for the first two, a five-row
settings column on the right — costume, Mio's accessory, Mayu's accessory,
difficulty, and the Game Start / Mission Select button that commits.

- **`mode` plays the part `now_place` does in every other outgame screen**, and
  it indexes `setup_menu_pad_func[3]` and `setup_menu_disp_func[3]` in step:
  0 top menu, 1 settings column, 2 exit window. There is no `next_place`
  animation hand-off — `next_place` here means "which phase to leave to".
- **The clear flags *skip* locked entries rather than blocking on them.** Both
  the costume and difficulty cursors walk in a bounded `for` loop
  (`% 9` / `% 4`, up to N tries) until `BIT_FLAGS::IsUp()` says the entry is
  unlocked; the cue at the bottom is the move SE if the cursor actually moved
  and the error SE if it came back to where it started. That comparison against
  a `csr_back_up` latched before the loop is the whole point of the local.
- **`accessory_flg` is indexed by the row, not by a literal.** Bit 1 is Mio's
  and bit 2 is Mayu's, which is exactly `setup_csr` for those two rows, and the
  ROM passes `setup_csr` — the bit position is computed at run time
  (`(setup_csr << 24) >> 29` for the word index), so it cannot be a constant
  GCC folded.
- **The difficulty row is skipped on a first playthrough and in Mission Mode.**
  `SetupMenuSetupSelPad`'s UP/DOWN arms re-test after the wrap and step the
  cursor past row 3 whenever `menu_csr == 1 || ingame_wrk.clear_save_flg == 0`.
- **This is the only place in `outgame/` that writes the ingame model
  numbers.** The commit calls `SetPlyrMdlNo` / `SetSisterMdlNo` out of
  `costume_tbl[9]` and the two accessory setters (-1 when the toggle is off),
  then `ingame_wrk.mDifficulty.Set()`. All nine costumes share accessory models
  6 and 7, which is why the toggle is a plain yes/no.
- **`flg` on every part-drawing helper is "this row is selected" and its only
  effect is the additive blend** (`alphar = 0x48`). The brightening comes from
  the caller handing over a bigger alpha, not from the flag.
- **`SetupMenuTopCursorDisp` draws a flare and an arrow per side, both rotated
  270 degrees**, and only the flare carries `rgb` and the additive blend — the
  arrow keeps whatever `CopySprDToSpr()` left. The rotation centre is
  `y + (float)ds.w`, and `DISP_SPRT::w` is `u_int`, so the ROM emits the
  halve/convert/double sequence for an unsigned int-to-float.
- **`SetupMenuCursorDisp`'s difficulty branch uses a literal `off_y + 280`**
  where its three siblings write `off_y + 190 + setup_csr * 30`. The flare on
  the very next line *does* recompute from `setup_csr`, so this is the source's
  own asymmetry, not a fold.

Two GCC facts this module is the cleanest example of, both worth carrying:

- **`>> 7` and `/ 128` are distinguishable in the output, and both appear in
  this file.** A shift of a value GCC can prove non-negative (built from
  `andi`/`sll`/`addu` off a `u_char`) is simplified from `sra` to `srl` by
  combine; a *division* keeps `expand_divmod`'s bias-and-`movn` sequence, which
  combine does not undo. So `alpha * 51 >> 7` and `alpha * 38 / 128` are
  visibly different, while the same expression through `mult`/`mflo` (every
  `ds.alpha * alpha >> 7`) stays `sra` because `nonzero_bits` cannot see
  through the multiplier.
- **The ROM's own brace style here is K&R** — `{` on the `if`/`else`/`for`
  line, with a blank line often following it. Every measured `$LM` in the file
  lands on a statement under that assumption and several do not under Allman;
  it is what resolves "is line N the `{` or the first statement". The
  reconstruction still follows the folder's Allman style for `if`, so its
  annotations are per statement rather than per source line, and the file says
  so.

Newly measured `common/variable.h` lines, both corrections: `CVariable::Set()`
is 39 (open), 43 / 45 (the two tests), 44 / 46 (the two asserts) and 49 (the
store) — the file had 44 on the first test and 46 on its assert;
`BIT_FLAGS::IsUp()` opens at 852. `<char,0,3>` is the first instantiation in
the build with both bounds non-trivial, which is what pins `Set()`.

Three range checks in this file compile to a single `sltiu`
(`SetupMenuSelMenuItemDisp`, `SetupMenuCostumeTypeDisp`,
`SetupMenuDifficultyDisp`), unlike `BIT_FLAGS::IsUp()`'s signed `slti` — so the
ROM's own test there is unsigned, or a two-sided one GCC folded.

**Not yet run in-game.** All three bodies were empty stubs, so the setup menu
drew nothing and had no way out; `SetupMenuMain()` now starts the menu BGM
(`BGM010_OMAKE`), drives all three pad handlers and reaches both hand-offs —
`GID_STORY_LOAD_MISSION_SAVE` for a story game and `GID_MISSION_SEL` for
Mission Mode, the latter of which is still a stub screen.

### system/mc — the whole folder is done

All 26 translation units under `system/mc`: 24 code TUs in `prg/` plus the two
data-only ones in `dat/`, and `dat/save_data.c` alongside them. **111/111
`ZERO2.MAP` `.text` symbols implemented** plus both file statics, and `.text` is
accounted for byte-for-byte — 0x41dc across the folder, with no gap of 8 bytes or
more anywhere, so there is no unlisted body. Every non-pointer struct is confirmed
by an `offsetof` harness (23 of them, plus `sceMcIconSys`, `sceMcTblGetDir` and
`ALBUM_INFO`), and all eight non-pointer tables are byte-identical to the ROM.

It is the save system: eleven `sceMc*` calls at the bottom, a step machine per
call, four composite jobs over those, and the marshalling layer that turns the
live game state into a card file.

**The shape is uniform and worth learning once.** Every primitive
(`mc_open`/`mc_close`/`mc_read`/`mc_write`/`mc_del_file`/`mc_make_dir`/
`mc_format`/`mc_check_dir`/`mc_check_card`) is an `Init` that fills a file-static
control block, a `Main` with steps 0 (issue) / 1 (poll) / 2 (drain a busy
request), and a `Req` that is nothing but the libmc call. **Steps 0 and 1 are
consecutive `if`s, not an if/else**, so a request issued this frame is polled in
the same frame — that is the whole reason the save screens are not glacial.

- **Every `Main` returns 1 / 0 / negative and the codes are the folder's own
  vocabulary**: -1..-8 are libmc's result codes passed through, -10 is "rejected
  four times, give up", -0x14 is the catch-all. The table is documented once in
  `mc.h`.
- **A pass-through set spelled as sparse compares is a `switch`.** GCC 2.96 builds
  a binary decision tree for a handful of cases: an equality test against a pivot,
  then a less-than to reject the low side, then a range check for the contiguous
  cluster. `MemoryCardFileOpenMain`'s apparently redundant `< -7` is that pivot's
  low-side branch, and the case-label count falls straight out of the line numbers
  (switch line, `{`, N cases, then the body). Do not write it as an `if` chain.
- **The busy path is asymmetric across the folder.** `mc_open` / `mc_make_dir` /
  `mc_format` spend a retry on `-200`; `mc_check_card` / `mc_check_dir` /
  `mc_del_file` do not. The tell is where GCC put the `retry_cnt` store — inside
  the branches, or once at a merge point after the whole chain.
- **`mc_read` insists on the full byte count and `mc_write` does not.** A short
  read is corruption (`error = -3`, close, report); any non-negative write result
  is success. Both need steps 2 and 3 for that: an open descriptor has to be closed
  before the real error can be reported, which is what `error` exists to carry.
- **`char fname[55] = "/";` is a declaration initialiser, not a statement.** GCC
  emits a two-byte copy from the `"/"` literal plus one `memset` of the tail, all
  on the declaration's line — which is why the path builders show three line
  markers before their first real statement. `char x[N] = "";` is the same thing
  with a whole-array memset. Writing it as `strcpy` + `memset(&x[2], ...)`
  reproduces the bytes but not the source.

Facts about the save layout that are easy to get wrong:

- **Two index spaces run through everything.** `dir_label` 0 is the game-data
  directory (7 files) and 1..5 the photo albums (1 file each); `file_label` inside
  dir 0 is 0 system / 1 play-data header / 2..6 the five slots. Several existing
  headers had these named `port` and `file_no`, which is wrong — every caller
  happens to pass 0, so nothing broke.
- **A save file is a manifest walk.** `system/mc/dat/save_data.c` holds four
  NULL-terminated tables of `void (*)(MC_SAVE_DATA *)` callbacks; a slot is 49 of
  them, resolved from the ROM's own pointers through `ZERO2.MAP`. **The order is
  the byte order of the file**, so swapping two entries silently invalidates every
  save. Only five of the 54 were missing from the tree and all five are two-line
  functions, now in their real owning modules (`clear_flg.o`, `album.o` x2,
  `menu_soul.o`, `movie_projecter.o`).
- **`MemoryCardCheckDirBroken()` is an exact arithmetic identity, not a
  heuristic.** `GetMemoryCardDirSizeCluster()` predicts the cluster cost from the
  save layout and `GetMemoryCardCheckDirSize()` derives it from the card's own
  listing; the latter's `+ (entries - 1) / 2 + 2` reproduces the former's literal
  `+ 5 + 2` (game dir) or `+ 2 + 2` (album) exactly. If a change makes those two
  disagree, every existing save reads as corrupt.
- **The listing always starts with "." and ".."** — `MemoryCardAllFileDelInit()`
  starts its walk at entry 2 and `GetMemoryCardCheckDirSize()` skips two entries
  before summing. A host-side card has to synthesise them.
- **A freshly-made file is all zeroes with `0xffffffff` in its checksum slot.**
  `MemoryCardAllFileMakeMain()` writes exactly that, `MemoryCardCheckFileBroken()`
  special-cases it as intact, and `MemoryCardCheckNewFileLoad()` recognises it as
  "never saved to" — checksum -1 *and* not one non-zero byte. Despite its name it
  has nothing to do with newer builds; the old header comment guessed wrong.
- **`unformat_flg` makes an unformatted card take two passes to report.** The
  first "no format" raises the flag and resets the step to 0 so the query runs
  again; only the second answers -2. A card mid-insertion reads as unformatted for
  a moment, and this is what stops that raising the format prompt.
- **`MemoryCardCheckMain()` treats card-result -1 (swapped) as success** and goes
  straight on to the listing. Only the every-frame watch reports the swap.
- **`sceMcTblGetDir` needs 64-byte alignment** and that is load-bearing, not
  decoration: it is what makes `MC_DIR_INFO` come out at the ROM's 0x4c0 rather
  than 0x484, and ZERO2.MAP shows the matching 0x3c gap in front of `mc_dir_info`
  in `mc_check_dir.o`'s `.bss`. Without it the harness fails on that one struct
  and nothing else.
- **`MemoryCardAssert` / `MemoryCardWarning` / `MemoryCardPrint` are real varargs
  functions, not macros.** All three vsprintf into a 1000-byte stack buffer and
  only then hand the result to `PRINT_ASSERT` / `PRINT_WARNING` — so the banner
  always names `mc_set_data.c` lines 1010 / 1034, and a ROM line recovered for one
  of the 36 `MemoryCardAssert()` sites belongs to the *caller*.

New in `src/sdk`: **`libmc.h` / `libmc.cpp`**, the eleven entry points the folder
actually reaches. A `jal`-only scan finds *none* of the sceMc calls and reads as
"the game never calls libmc" — the `Req` wrappers are tail jumps, so the scan has
to cover `j` as well. The other twelve libmc exports ZERO2.MAP links in
(`sceMcSeek`, `sceMcFlush`, `sceMcRename`, `sceMcSetFileInfo`, ...) genuinely have
no call site.

**PORT DEVIATION, deliberate and flagged in the file:** the shim backs the card
with a directory on the host (`<user dir>/memcard/mc<port>-<slot>/`, the user
directory being the one `miopan/io/miopan_paths.h` names) through
`std::filesystem`, rather than a "no card inserted" stub that would leave the
whole folder unreachable. The async handshake is preserved exactly — a request
does its work, parks its result, and the next `sceMcSync()` hands it back once,
answering `sceMcExecIdle` when nothing is parked — because that idle answer is the
path every step machine uses to re-issue a lost request.

Three ROM behaviours reproduced and commented at the site:

| Where | What |
|---|---|
| `DevelopMemoryCardLoadData` | the NULL-address warning does not skip the block; control falls into the copy with a NULL destination |
| `SavePCFile` / `LoadPCFile` | the staging buffer is leaked whenever the open fails — allocated before the open, freed only inside the success branch |
| `MemoryCardMakeNewDirMain` | no per-code error mapping at all; every non-zero result becomes -0x14, and the ten-line gap where its siblings have their switch holds no recoverable code |

**Run in-game.** `MemoryCardDebugReqSizeDisp()` runs on frame 0 of `init_super()`
and now drives the whole manifest walk: it prints 255 clusters for the game-data
directory and 970 for an album, with no assert and no bounds failure. That
exercises `GetMemoryCardDirSizeCluster` -> `GetMemoryCardDataSize` ->
`SetMemoryCardSaveDataInfo` and all 54 callbacks. The card traffic itself
(`mc_check` onward) still has no in-game trigger, because `loadgame.c` is only
reachable from the title screen's load option.

### ingame/savepoint — the whole folder is done

All seven translation units: the six code TUs plus the data-only
`savepoint_dat.o`. **49/49 `ZERO2.MAP` `.text` symbols implemented** (44 in the
folder plus 5 in `zero2_anim2d.o`, see below) and 72/72 `functions.txt` entries
including every static. `.text` is accounted for byte-for-byte across all seven
— every gap is exactly one 4-byte alignment fill and every span matches the
map's `.text` size, so there is no unlisted body. All five structs are confirmed
by an `offsetof` harness, `savepoint_tex[16]` is byte-identical to the ROM's
`.data` (diffed out of the compiled `.obj`), and all eight `.rodata` tables plus
all five local aggregate initialisers are verified against the ROM's bytes.

It is the save point: a prompt, a three-row menu, and the two fades that
bracket them.

- **`GID_SAVEPOINT_MAIN` is a *parent* phase.** Its `pre_`/`after_` callbacks
  run every frame while one of three children — `_TOP`, `_SAVE`, `_ALBUM` — is
  the active phase, which is why the background, both black fades and the BGM
  live in `savepoint_main.o` and the children only draw on top. `pre_` runs
  before `after_` in the same frame, and that ordering is load-bearing (below).
- **The room never stops during a save.** `one_SavePoint_FadeIn` and
  `one_SavePoint_FadeOut` are the same seventeen calls — player, sister,
  ghosts, BGM, map hit, camera, play timer, motion, fog, `gra3dDraw`, effects,
  brightness, event display, 2D — differing only in the fade module's own
  tail. Only the black quad takes the room away.
- **The two fades are named for what the *black* does, not the picture.**
  Step 2 runs `Zero2Anim2D_FadeOutAnimCtrl` (black clearing off, menu
  appearing); step 4 runs `FadeInAnimCtrl` (black closing over it). Both share
  one counter.
- **`savepoint_main_ctrl.step == 3` is a real empty `case`.** The jump table in
  `.rodata` has a slot for it pointing at the break label, and the default arm
  asserts — so it is intentional, not a hole. Nothing happens while the menu is
  open; `SavePointEndReq()` is what moves it on.
- **Leaving skips the confirm window.** The `csr` switch appears three times in
  `savepoint_top.c` with the same shape, once per place a decision can be
  taken, and `SAVEPOINT_TOP_CSR_EXIT` goes straight to `SavePointEndReq()` in
  all three. `SavePointTopExeDecision`'s copy of that arm is unreachable.
- **`SavePointTopFirstInit()` deliberately skips the open animation** — it
  parks step at 1 and `anim_step` at 2 (already open), because the menu is
  meant to be there the moment the parent's fade clears. `SavePointTopInit()`'s
  `savepoint_top_init_flg` is what makes only *later* entries (back from the
  save screen or the album) animate in.
- **`SavePointTopPad`'s TRIANGLE only *selects* the exit row**; the step-3 arm
  of `SavePointTopMain()` is what acts on it. CROSS on save/album opens the
  confirm window and re-stores `conf_csr = 1` — a store GCC eliminated as
  redundant, which is why line 307's `$LM` sits on top of line 308's.
- **`SavePointStartReq()` has two gates and only one warns.** Under
  `0x2b1400` bytes free is a silent no-op the event script retries; a live
  ghost gets a `PRINT_WARNING`. Lines 91–103 compiled to nothing.
- **The background is five layers on a half-black fill**, three of them taking
  alpha from one 900-frame counter while the two pattern sheets scroll on
  counters of their own (900 and 600) — which is what stops them beating in
  step. Every group of four sprites is one quadrant image drawn with
  flip 0/1/2/3.

One debug-info pattern this folder is a clean example of: **a draw loop's
`ds.x`/`ds.y` update and its `scw`/`sch`/`csx`/`csy` update are one source line
each, not four.** `SavePoint_BgPattern1Disp` has five `$LM`s on 219 and six on
220, and the stores confirm the split. Ghidra renders the pair in the wrong
order (it prints `csx` being computed first and `x = csx` last); read the store
offsets — `0x1c`/`0x20` are `csx`/`csy` and `0x24`/`0x28` are `x`/`y` — rather
than the decompiler's ordering.

### zero2_anim2d.o — done, and it is the folder's prerequisite

Reconstructed alongside savepoint because **`Zero2Anim2D_FadeInAnimCtrl` and
`_FadeOutAnimCtrl` did not exist at all** — the folder could not have linked.
All 5 exports, both named `.rodata` tables and all four local-array
initialisers verified against the ROM. It is the five canned 2D curves every
menu screen shares.

- **The earlier header had `InOutAnimCtrl`'s last two parameters as `int`**;
  `ZERO2.MAP` says `short`. Its counters are `char`, so neither duration may
  exceed 127.
- **ROM BUG, reproduced: `Zero2Anim2D_FadeOutAnimCtrl` returns `0x80`, not 0,
  once the timer passes `fade_out_time`** — a fade-out polled one frame too
  long snaps back to fully black. It is the fade-in body with the table
  swapped. Every caller in the build gates on the timer, so it never shows;
  `savepoint_main.c`'s step 2 → 3 flip is the clearest case, because
  `SavePointMain()` runs in `pre_` and the fade in `after_`.
- **`InOutAnimCtrl` has three clamps, each with its own message** — a negative
  timer, and a timer past the end of either ramp — all `PRINT_WARNING`, all
  defence against a caller that shortens a duration mid-animation.
- **The two `sh` stores of `end_time` are separate statements (58/59) in
  `InOutAnimCtrl` but part of the declaration in the two fade helpers.** GCC
  emits a partially-constant aggregate initialiser as a `.rodata` blob copy
  plus a store of the variable member, and the store then carries no `$LM` of
  its own — which is how you tell the two forms apart.

**Not yet run in-game**, though every entry point is wired: `ingame.c` calls
`SavePointBackGroundLoadReq()`/`SavePointEnd()` on room load/unload and
`ev_macro.c` calls `SavePointStartReq()`, all three of which were no-ops
before.

**One reachable hang, flagged at the call site and out of scope to fix here:**
`AlbumMain()` is still a stub returning 0 ("still open"), so
`GID_SAVEPOINT_ALBUM` never exits — the player picks Album and is stuck on a
blank screen. That return was the right choice for `title_album.c`, where
either value is equally inert; here it is a dead end. `album.o` is the natural
next target. `game_data_save.o`'s stub is benign by comparison —
`GameDataSaveMain()` returns 1, so the save screen bounces straight back to the
menu.

### game_data_save.o — done

All 6 `ZERO2.MAP` `.text` exports plus 47 statics, verified 6/6 against the map
and 53/53 against `functions.txt`. `.text` is accounted for byte-for-byte
(0x2904 of code plus 37 four-byte alignment fills = the section's 0x2998, no
gap of 8 bytes or more anywhere). `GAME_DATA_SAVE_CTRL` (0xc) and
`GAME_DATA_SAVE_DISP` (0x8) are confirmed by an `offsetof` harness, and all
three of its writable tables are byte-identical to the ROM, diffed out of the
compiled `.obj`. It is the save screen: pick one of five slots and write it.

**Read outgame/loadgame.c first.** It is the same screen with the writes taken
out, it is already reconstructed, and it establishes every convention this file
follows — raw hex `msg_id`s with no invented names, a `*_MC_STEP` enum named
after the ROM's own handler symbols, the same include set, and the same
writable-backing deviation. Most of the work here was recognising the twin.

- **Every `Init` case falls through into its `Wait` case**, so a request is
  issued and polled on the same frame. That fall-through is the ROM's own —
  the jump table has separate entries for both halves and case N's code runs
  straight into case N+1's. Same idea as the `system/mc` step machines.
- **31 states versus loadgame's 15**, and the extra sixteen are all "the card
  is not ready": remake, dir-delete, new-make, new-make-save, format, plus a
  format-end hold. Each is an Init/Wait pair with its own yes/no prompt.
- **A save writes three files in one pass**: system (0), the chosen slot
  (csr + 2), then the play-data header (1). `save_file_label` is that list and
  `save_file_cnt` walks it; `GameDataSaveMcSaveInit` patches index 1 to the
  slot before the first write. `GameDataSaveMcNewMakeSaveInit` is the twin for
  a brand-new directory and never patches, so it always writes slot 0.
- **The system file is read back before it is overwritten.** That is the whole
  point of steps 8/9: `ClearFlgMerging()` folds the card's clear record into
  the running one so a save never loses a costume or an ending the other
  playthrough unlocked. The merge takes the larger clear count per difficulty
  and the OR of every flag — and tests `== 1`, so a `clear_flg` holding any
  other non-zero value does not survive.
- **The cursor's range is "used slots + 1", clamped to five.** So the player
  can overwrite any existing save or start exactly one new one, but cannot
  skip past the first empty slot.
- **`GameDataSaveMcFormatConfWait` deliberately does not use
  `GameDataSaveMcEveryFrameCheck()`.** An unformatted card reports -2 every
  frame and the common check would bounce the player into the error screen
  before they could answer the format prompt, so -2 is swallowed there and
  nowhere else.
- **`GameDataSaveMcEndConf` and `GameDataSaveMcErrorConfPad` are the same
  function twice** — both buttons do the same thing in each, and they are still
  two separate bodies in the ROM.
- **Two ROM bugs, reproduced:** both `save_file_cnt` over-range asserts pass
  one argument to a two-argument format (`"Error! %s save_file_cnt %d\n"` gets
  only the count), so `%s` prints whatever is in a1.

**PORT DEVIATION, flagged in the file:** all three of the file's tables are the
backing store of a `reference_fixed_array` and **the ROM writes through every
one of them** — `GameDataSaveInit()` stores -1 into the snap labels, and the two
save steps patch their file list. GCC put the initialiser images in
`.rodata` (3b3730 / 3b3798 / 3b38a8) and the EE has no write protection on data
pages, so nothing complained; the host does, so they are plain writable arrays
here. `loadgame.c` already carries the same deviation for its own snap table —
**assume it whenever a `reference_fixed_array` points into `.rodata`.**

Two of `clear_flg.o`'s sixteen exports came with it, reconstructed rather
than stubbed because the merge is what makes a save correct:
`ClearFlgMerging` (0x12ef00) and `SetClearFlgCtrl` (0x12f548). Both take and
return `CLEAR_FLG_CTRL` **by value**, as the ROM does. (`clear_flg.o` is now
complete — see its own section below.)

**Not yet run in-game.** Every body was an empty stub and `GameDataSaveMain()`
returned 1 unconditionally, so both callers — `setup.c`'s Mission Mode save and
`GID_SAVEPOINT_SAVE` — fell straight through without touching the card. They
now drive the whole machine, and because `system/mc`'s host shim is real, a save
actually writes files under `<user dir>/memcard/mc0-0/` — on Windows
`%APPDATA%\Mikompilation\MioPan\memcard\mc0-0\`.

One faithfulness note carried forward: `GameDataSaveMcCheckWait` and
`GameDataSaveMcFormatConfWait` each carry an `int msg_id` local that the ROM
does not have — `functions.txt` lists only `mc_res` for both, and the
disassembly shows the display-field stores cross-jumped into one tail (lines
629/630 and their FormatConfWait equivalents) rather than a variable being
assigned and then stored. Behaviourally identical; the fix is to write each
case's `msg_id`/`mc_step` pair out in full, the way `system_data_save.c` does.

### save_load_disp.o + save_load_dat.o — done

All 31 `ZERO2.MAP` `.text` exports, verified 31/31 against the map and 31/31
against `functions.txt`, and `.text` is accounted for byte-for-byte
(0x244ff0..0x246a78 = 6748 bytes of code plus eleven 4-byte alignment fills,
no gap of 8 bytes or more anywhere). `save_load_tex[60]` (data 33e498) and
`out_game_tex[8]` (data 33bfd8, a new `outgame/tim_dat/outgame_dat.c`) are
byte-identical to the ROM, diffed out of the compiled `.obj`s; both local
`SQAR_DAT` initialisers reproduce the ROM's `.rodata` images exactly; and
`SaveLoadClearNumberDisp` is driven against a transcription of its
disassembly over its whole input domain (3900 cases, zero mismatches).

It is the drawing layer both save screens share, and it holds **no state at
all** — no statics, no work block, 31 variations on "copy a `save_load_tex[]`
record into a `DISP_SPRT`, offset it, scale its alpha, draw it".

- **The five slots are one set of records drawn five times**, stepped 118
  pixels apart by the caller's `disp_label`. The two digit-plate routines
  (`SelDataNumDisp` / `NonSelDataNumDisp`) are the exception: they index
  `save_load_tex[base + disp_label]` and so carry their own x.
- **A sprite's x and y update is ONE source line.** Every draw here has a
  single `$LM` covering both stores; Ghidra prints the pair backwards, so read
  the store offsets (0x24 is x, 0x28 is y). Same tell as
  [[sprite-draw-loop-line-grouping]].
- **The two `SQAR_DAT` tables in `.rodata` are LOCAL array initialisers.**
  `SaveLoadCmnBaseDisp`'s `win_bg[4]` (3c4a40) and `SaveLoadMcStateMsgWinDisp`'s
  `win_bg` (3c4aa0) are blobs copied into the stack frame, and they plus the
  assert strings are the whole 0xff of the object's `.rodata`. `globals.txt` is
  right to list none. See [[rodata-blob-into-stack-is-a-local-initialiser]].
- **off_x / off_y are not honoured everywhere.** `SaveLoadCaptionDisp` ignores
  both, `SaveLoadClearNumberDisp` reads neither, and
  `SaveLoadMcPlayDataInfoDisp` places its chapter and room lines at fixed
  screen coordinates while the play-time readout beside them does add the
  offset. Both callers pass 0, 0, so none of it shows.
- **`SaveLoadClearNumberDisp`'s running x has no stab**, but it is an
  accumulator across loop iterations and so cannot be a CSE temp — the two
  lines it is built from (833 for the column, 879 for the per-digit step) place
  it exactly. That is the counter-example to
  [[unstabbed-register-is-a-cse-temp]]: the rule holds for repeated
  subexpressions, not for values that carry across an iteration.
- **`ten_tmp` is reset twice**: line 840's store feeds the first iteration and
  a second store at the bottom of the body feeds the rest, which is why GCC has
  a `li 1` in the preheader tagged 840 and another in the back edge's delay
  slot. A reset written at the *top* of the body would have carried its own
  line into the preheader instead.
- **`SaveLoadClearNumberDisp_One`'s assert names the wrong function.** The
  banner takes `__FUNCTION__` (`SaveLoadClearNumberDisp_One`) but the message
  is the hardcoded literal `"Error!! SaveLoadNumberDisp_One"`.
- **`SaveLoadTitleFrameDisp` is the one part out of the OUTGAME pak**, which is
  why `loadgame.c` passes `GetOutGameCmnTexAddr()` there and its own buffer
  everywhere else. That is what pulled `outgame_dat.o` in.

### system_data_save.o — done

All 4 `ZERO2.MAP` `.text` exports plus 37 statics, verified 4/4 against the map
and 41/41 against `functions.txt`. `.text` is accounted for byte-for-byte
(0x265348..0x2669a4 = 5456 bytes of code plus 268 bytes of alignment fill and
the fixed_array boilerplate). `SYSTEM_DATA_SAVE_CTRL` (0x5) and
`SYSTEM_DATA_SAVE_DISP` (0x8) are confirmed by an `offsetof` harness. It is the
system-file save screen: option.c's "Save the settings?" prompt.

**It is `game_data_save.c` with the slot list taken out** — same two-level
step/mc_step pair, same Init-falls-into-Wait, same five recovery branches. No
cursor, no snapshots, no play-data header, one file written. That takes 31 card
states down to 25 and four screen steps down to three; there is no load-wait
step either, because option.c already has both paks resident.

- **The file has no static data at all.** Its `.rodata` (0x2ec) is the
  `fixed_array` assert literal, four `__FUNCTION__` strings, the banner, and
  five jump tables — nothing else. Same signature as `subtitle.o` and `title.o`.
- **`exit_state` is not "did it save".** 0 means "done with the option screen"
  — a completed save, an acknowledged error, *or* CROSS on "no" — and makes
  `Main()` return 1, which takes option.c to place 4. 1 means "backed out"
  (TRIANGLE) and returns -1, which leaves the player on the option page. So
  answering "no" still leaves the menu; only cancelling keeps it.
- **The save is a read-modify-write.** mc_step 4/5 loads the card's copy of
  file 0 first so `ClearFlgMerging()` can fold its clear record into the
  running one before the write goes out — same reason `game_data_save.c` reads
  file 0 back before overwriting it.
- **`SystemDataSaveMcSaveInit` calls `OptSetOptWrk()` first**, which is what
  makes the write meaningful: it flushes the option screen's working copy into
  the live option block, so the marshalled bytes are the settings the player
  just chose.
- **`SystemDataSaveMcCheckWait`'s case -1 repeats the whole triage.** A swapped
  card is not an error here — the DirBroken/EmptyBroken tests simply run again
  for the new card. `game_data_save.c`'s own -1 case short-circuits instead.
- **`SystemDataSaveInit`'s guard is written the other way round** from
  `GameDataSaveBackGroundLoadReq`'s: the assignment is the `if` arm (lines
  204/205) and the assert the `else` (208).
- **`SystemDataSaveOutReq` stores `exit_state` last**, at line 329, after the
  three step/anim fields at 325-327.
- **There is no `msg_id` local anywhere in this file.** Each case writes
  `system_data_save_disp.msg_id` and `mc_step` directly and GCC cross-jumped
  the stores into one tail; the tail carries the *last* contributor's line
  numbers, which is why 540/541 (the default arm) sit on code every other case
  branches into.
- **Field order in the three-store groups is conf_csr, msg_id, mc_step.**
  Settled by the block at 0x2659f0, where the conf_csr store itself is tagged
  524 while the msg_id and mc_step value loads are 525 and 526.

**Not yet run in-game.** `SystemDataSaveMain()` returned -1 unconditionally and
every drawing body was empty, so option.c's save window bounced straight back
and neither screen has ever drawn a pixel. Both now do real work: the option
save writes file 0 through `system/mc`'s host shim, and `game_data_save.o`'s
screen draws for the first time.

### movie_room.o — done

All 6 `ZERO2.MAP` `.text` exports, verified 6/6 against the map and 6/6 against
`functions.txt`. `.text` is accounted for byte-for-byte (0x21f680..0x21fd10 =
0x680 of code plus four 4-byte alignment fills, so there is no unlisted body),
`MovieDrawEnv` is byte-identical to the ROM's `.rodata` (diffed out of the
compiled `.obj`), and `CMovieRoom`, the composed TEX0 and both `.lit4` floats
are confirmed by an `offsetof`/arithmetic harness. It is `CMovieRoom`: the film
player behind the movie room's two projectors, driven by `movie_projecter.o`
as PreLoad → PlayFilm until 1 → Work every frame until 1 → Release.

- **`movie_room.h` and `movie_room.c` live in `src/ingame/`**, not in
  `movie_room_menu/prg/` where an earlier pass put the header.
  `functions.txt` gives the source path outright, and symbols.txt carries
  exactly one `SOL movie_room.h` — at 1ca9b8 inside ingame.o, whose enclosing
  `SO` directory is `src/ingame/`. `movie_projecter.o` calls the six methods
  out of line and inlines nothing, which is why it has no SOL of its own.
- **That same record is the whole constructor.** `$LM851` = header line 12 and
  the instruction it labels is `jal CMovieRoom::Init`; the expansion ends two
  instructions later, so the ctor body really is `{ Init(); }`. The instance
  belongs to ingame.o (.data 3186c0, defined at ingame.c 146).
- **The sound bank is released every time, the buffers only under
  `mPreloadFlg`.** A `PreLoad()` that failed its admission test has already
  claimed the bank and nothing else, and `Release()` is what reclaims it.
- **`PreLoad()`'s EE threshold is 0x2b1400** — the same figure savepoint.o
  refuses to open under. Both are "is there a spare 2.7 MB on the ingame
  heap"; the five buffers here total 0x2a3d00. The IOP test beside it calls
  `sceSifQueryMaxFreeMemSize()` a **second** time to print the number, which
  is the source's own and not a CSE the port lost, and it compiles to `sltu`
  because that SDK entry point returns `unsigned int`.
- **Draw() writes all twelve TEX0 fields into an uninitialised stack local**,
  the four CLUT ones included, even though PSM is PSMCT32. The and/or chain at
  21fb18..21fc58 composes 0x200754026802BAA0 — reproduced exactly by the
  harness, which is also what proves the host's `sceGsTex0` bitfield layout
  matches the EE's.
- **MakePacket3D's fUX/fUY/fUW/fUH are texels, not fractions** — it divides by
  `1 << TW` / `1 << TH` itself. So `0.02 .. 0.02 + w*0.96` crops 4% off a
  160x128 frame, which is what hides the decoder's macroblock edge under
  `tex1`'s bilinear magnify. The films really are 160x128: read the MPEG
  sequence header (`00 00 01 B3`, then 12 bits width and 12 bits height) out
  of `movie/movie_room_0NN.pss`.
- **PlayFilm() lines 114-155 hold no code.** It opens at 113 and its first
  statement is 156; `.text` is complete without that 42-line span.
- **PlayFilm's `SND_3D_SET` has no name in the stabs.** Its `$LBB13` block
  holds no `LSYM` at all, unlike `Draw()`'s `Info`/`Tex0`, so the local exists
  but is unnamed — the name in the port is the port's.

Two debug-info rules this module is a clean joint example of, both worth
carrying:

- **`final` emits a `$LM` only when the line number *changes*, so two
  statements on one source line give one label — and two *different* lines
  landing at the same address give two labels at that address.** Both happen
  here: `Draw()`'s line 198 carries the TBP0 *and* TBW assignments (four
  insns, against line 199's one), and `Release()`'s 37 carries the `if` and
  its `PRINT_WARNING`; meanwhile `$LM37`/`$LM38` sit together at 21f804 and
  `$LM148`/`$LM149` at 21fd00. That pair of facts is what rules out "line 197
  was collapsed" and settles the count.
- **An `int` function gets a closing-brace `$LM` on its shared epilogue; a
  `void` one does not.** Init, Release and Draw stop at their last statement;
  PreLoad, PlayFilm and Work each carry one more. So **one** trailing note
  means early-`return` guards (PlayFilm: 178 `return 1;`, 180 `}`) and **two**
  means a real trailing statement (Work: 238 `return 1;`, 240 `}`). That is
  what distinguishes `if(a) if(b) if(c){...} return 0;` from three
  `if(!x) return 0;` guards, which compile identically.

**Tool fix carried with it:** `verify.py`'s `map_exports()` matched export
names with `(\w+)\(`, which cannot match `CMovieRoom::Draw(void)` — so **every
class-based object reported "0 exports, 0 implemented"** rather than failing.
filament.o, sp_chance.o, the eight finder widgets and all the `system/mc`
classes were all silently unchecked by it. The pattern is now `([\w:]+)\(`;
filament.o reads 14/14 and sp_chance.o 7/7.

Three playpss.a entry points were added alongside, all shims like the rest of
that file: `playPssStartNoWait`, `playPssIsReady` and `playPssGetMpegInfo`
(plus `PLAY_PSS_MPEG_INFO`). `playPssIsReady()` **returns 1, not 0** — a 0
parks `movie_projecterWork()` in `MOVIE_PROJECTER_STATE_PRELOAD` for ever,
the same judgement `IgEffectIsEndParticleSuck()` and `ExpandFile()` needed.
`playPssGetMpegInfo()` reports 0x0, which degenerates Draw()'s quad to a point
rather than texturing it with whatever is at 0x3aa0.

**Not yet run in-game**, and it cannot be until `movie_projecter.o` and
`movie_room_menu.o` are reconstructed — both are still stubs, so nothing calls
any of these six bodies yet.

### clear_flg.o — done

All 16 `ZERO2.MAP` `.text` exports, verified 16/16 against the map and against
`functions.txt` (the one extra entry there is the `global constructors keyed to
clear_flg_ctrl` node, which the port reproduces as `_GLOBAL__sub_I_clear_flg_ctrl`
via `BIT_FLAGS`'s default constructor). `.text` is accounted for byte-for-byte
(0x12eda8..0x12f990 = 0xbc0 of code plus ten 4-byte alignment fills = the
section's 0xbe8, so there is no unlisted body). Every flag write, bit index and
`FileGet` argument is re-derived straight from the ROM words by a small
constant-propagating decoder rather than read off the decompiler, and the
compiled `.obj`'s own constant pool carries the ROM's four `ClearFlgCtrlInit`
words (`1, 0, 1, 3`) verbatim.

It is the record of everything the player has ever finished — clear counts,
costumes, accessories, endings watched, difficulties unlocked, mission mode's
two all-clear awards — and it lives in the **system** file, not a save slot,
which is what carries it across playthroughs.

- **The file has no static data at all.** `.data` is `clear_flg_ctrl` (all
  zeroes, in `.data` rather than `.bss` because it has a constructor); `.rodata`
  (0xf6) and `.sdata` (0x86) hold nothing but the `fixed_array` assert literal,
  the `void*`/`char*`/`unsigned int*` type names, and nine `__FUNCTION__`
  strings. Same signature as `subtitle.o` and `title.o`.
- **Those nine strings are the module's cheapest map.** They are one per
  `BIT_FLAGS<N>::FlgUp`/`IsUp` *instantiation*, emitted in order of first use:
  FlgUp ×3 (accessory `<3>`, costume `<9>`, difficulty `<4>` — i.e.
  `ClearFlgCtrlInit`), then IsUp, IsUp, FlgUp, IsUp, IsUp (`ClearFlgMerging`
  walking `<3>`, `<2>`, `<9>`, `<4>`), then a fifth FlgUp — which is the tell
  that a **fifth** `BIT_FLAGS` is written somewhere: `CCameraPowerUp`'s
  `BIT_FLAGS<10> mTemperedRenzFlg`. Nothing references them, because every call
  in the file passes a constant and the range check folds away.
- **Clearing the game hands out camera upgrades.** Three of the four
  `*GameClearExe` reach outside the record into
  `m_plyr_camera.camera_power_up`: easy raises `mAdditionFlg` 3, normal
  `mTemperedRenzFlg` 4 and `mCamPartsFlg` 3, hard `mTemperedRenzFlg` 8 and 5.
  Those five bits are exactly the ones `game_result_top.o`'s
  `GameResultTopClearFlgSet()` tests — an independent confirmation of the whole
  decode, since that object was reconstructed separately.
- **`ClearFlgCtrlInit` leaves costume 0 and difficulties 0/1 up**, so
  `difficulty_flg`'s word comes out **3**, not 1. The four `AllDown()` calls are
  *inferred*: the `FlgUp()`s that follow emit no load from `clear_flg_ctrl`, and
  GCC can only know a global's value because a store it then deleted as dead had
  just put a 0 there. Only `ending_movie_flg`'s AllDown store survives (nothing
  overwrites it) and it is the one the disassembly shows outright, at
  variable.h 801. **This is also proof that GCC 2.96's `flow.c` does delete dead
  memory stores within a basic block** — worth remembering the next time a
  missing load looks impossible.
- **`ClearFlgMerging`'s clear-count arm is a real `if`/`else`, not a ternary.**
  The measured 6-line body (95..100) and the two-arm layout with a `b` to a
  shared `sb` both say so, and the branch sense makes the condition
  `buff1.clear_cnt[i] <= buff2.clear_cnt[i]` — the *else* is the block at the
  higher address, so the emitted `slt buff2, buff1` is the inverted test.
  The earlier pass's loop annotations here were two lines low throughout (109
  for what is 111, and so on); the increment carries the **closing brace**, so
  each flag loop is `for` / `if` / `FlgUp` / `}` / `}` over five lines.
- **The two `Check` functions answer "already awarded", not "still to award".**
  Each is one `&&` of two `IsUp`s, and `mission_ctl.o` writes
  `Check...() == 0` to get the sense it wants.
- **`ClearFlgEndingNormalExe` / `ClearFlgEndingHardExe` belong to `ending.o`'s
  flow**: a jal scan finds their only callers in `init_Ending_Normal1()` and
  `init_Ending_Hard()`. `SetSave_ClearFlg` has **no** `jal` anywhere — it is
  reached only through `save_data.c`'s manifest pointer.
- **`ClearFlg_AddClearCnt` saturates at 99** and spends three separate
  `operator[]` calls, one per subscript; GCC CSEd the address but not the bounds
  check. The increment loads `lbu` and the clamp compares `lb`.

New in `common/variable.h`: `BIT_FLAGS<N>::FlgUp()` opens at **825**, measured
from the `this`-setup marker in `ClearFlgMerging`'s four flag loops — the same
shape that already pins `IsUp()` to 852.

**Not yet run in-game.** Twelve of the sixteen bodies were empty stubs, so no
clear had ever unlocked anything: `ClearFlgCtrlInit()` runs on frame 0 of
`init_super()` and now seeds the difficulty and costume flags, the
`*GameClearExe` chain is what `game_result_top.o` calls on a finished
playthrough, and `DebugAllClearFlgUp()` (the ingame debug page) now raises the
lot in one press.

### clearmenu.o — done

All 18 `ZERO2.MAP` `.text` exports plus 9 statics, verified 18/18 against the
map and 27/27 against `functions.txt`. `.text` is accounted for byte-for-byte
(0x12f990..0x130088 = 0x6b8 of code plus sixteen 4-byte alignment fills = the
section's 0x6f8, no gap larger than 4 bytes, so there is no unlisted body).
`CLEAR_MENU_CTRL` (0x8) and `CLEAR_MENU_DISP` (0x10) are confirmed by an
`offsetof` harness, the `SQAR_DAT` local initialiser reproduces the ROM's
`.rodata` image at 0x3a34a8 byte-for-byte, and the alpha scale is driven
against a transcription of its disassembly over all 65 536 input pairs.

It is the clear menu's spine: the parent phase behind `clearmenu_top.o`'s
three-row screen, and the `init`/`one`/`end` brackets for it and its three
children.

- **It is `savepoint_main.o`'s twin, the same source with the names changed**,
  and the line numbers run about +5 against savepoint's through the whole
  switch. It does not merely resemble that screen, it *borrows* it: the
  background pak is `SAVEPOINT_BG_PK2` (file 4465, the same id savepoint
  loads) and the per-frame draw is `savepoint_disp.o`'s own
  `SavePoint_BgDisp()`. There is no clear-menu background art.
- **Three deliberate deviations from the twin.** The heap is
  `ol_loadGetHeap`/`ol_loadFreeHeap` where savepoint's is `mem_util*`, because
  this menu hands over to the title screen and its buffers must come out of
  the out-game heap; the load priority is **6**, the lowest anything in the
  tree asks for, against savepoint's 2; and the black quad goes through this
  file's own `ClearMenuFadeBlackBgDisp()` rather than `SavePoint_BlackBgDisp()`
  even though the two compose exactly the same 640x448 half-black `SQAR_DAT`.
  The fade time is 20 frames, not 30.
- **`clear_menu_ctrl` puts `step` at 0x0 and `stream_id` at 0x4** — the twin
  struct `SAVEPOINT_MAIN_CTRL` has them the other way round. Read the `gp`
  offsets rather than copying savepoint's header.
- **Step 3 is a real, empty `case`.** The jump table at `.rodata` 0x3a3490 has
  five slots and slot 3 points straight at the break label; the default arm
  asserts. Same shape savepoint has, and the same reading.
- **Step 4 leaves for `GID_TITLE_TOP`**, not a fade-out phase of its own — this
  is the end of the playthrough.
- **`init_ClearMenu()` claims every screen the menu can reach**, handing
  `ol_loadGetHeap`/`ol_loadFreeHeap` to `GameDataSaveBackGroundLoadReq()` and
  `AlbumBackGroundLoadReq()` as function pointers: the save screen and the
  album are children of this phase and have nowhere else to load from.
- **`init_ClearMenu_Save()` passes `GameDataSaveInit(1)`**, where savepoint
  passes 0 — the post-clear save, which raises `ingame_wrk.clear_save_flg` and
  is what makes the difficulty row appear in the setup menu next time.
- **`SetClearMenuStreamID()`'s only caller is `init_GameResult()`**
  (`game_result.o`, at 0x1aafbc, still a stub). The clear BGM is started by the
  result screen and plays on under this menu; `ClearMenuFadeOutReq()` needs its
  id to take it down. So the fade-out is silent until `game_result.o` is done.
- **`ClearMenuFadeBlackBgDisp()`'s `off_x`/`off_y` are dead** — both call sites
  pass 0 and neither is read. Another of the tree's display helpers that takes
  offsets and ignores them.

Two small things worth carrying:

- **The `$LM` on a `b` is the `break;`, not the store in its delay slot.** Case
  0 settles it outright: `$LM215` sits on the `li` and `$LM216` on the `b`
  whose delay slot holds the `sb`. Applying that reading to the whole switch
  gives a completely self-consistent statement layout (213 `switch`, 214 `case`,
  215 store, 216 `break`, 217 `case`, 218 `if`, 219 store, 220 `}`, 221
  `break`, ...) with exactly one blank line unaccounted for, inside the step-4
  `if`. Reading the delay-slot store as the higher line instead leaves the
  layout one short and cannot be made to close.
- **40 bytes of zero in an object's `.sdata`, next to the
  `_fixed_array_assert` `str` pointer, is template boilerplate, not data.**
  `clearmenu.o` and `clearmenu_top.o` carry theirs *before* `str`, `ending.o`
  *after* the type-name literals -- but all three are exactly 0x28 once the
  file's own statics are subtracted (0x44-0x1c, 0x45-0x1d, 0x40-0x18).
  `globals.txt` and `functions.txt` are right to list nothing there; do not go
  looking for a table.

The earlier pass's annotations here were one to two lines low where they
existed (`GetClearMenuTexMem` marked 155 is 156, `LiberateClearMenuTexMem` 287
is 288, `init_ClearMenu_Top` 433 is 432) and its step names were wrong —
`step == 2` is the fade-in, not "menu opening".

**Not yet run in-game.** Twelve of the eighteen exports were placeholder
bodies, so the clear menu never loaded its background, never faded, and never
advanced past step 0 — `one_ClearMenu_Top()` was gated on a step nothing moved,
which meant `clearmenu_top.o`'s whole reconstruction sat behind a screen that
could not open. `pre_`/`after_ClearMenu()` now run the machine every frame.

### ending.o — done

All 13 `ZERO2.MAP` `.text` exports, verified 13/13 against the map and 13/13
against `functions.txt`. `.text` is accounted for byte-for-byte
(0x16b170..0x16b3d4 = 0x250 of code plus five 4-byte alignment fills = the
section's 0x264, so there is no unlisted body). **The object has no statics and
no data at all** -- its `.rodata` (0x56) and `.sdata` (0x40) hold nothing but
the `fixed_array<%s,%d>` assert literal, the `void*` / `char*` /
`unsigned int*` type names and the boilerplate zero block; `globals.txt` has no
section for the file. Same signature as `subtitle.o` and `title.o`.

It is the three ending phases: play a movie, unlock it in the gallery, hand on
to the result screen. Four GPhase callback sets over the same three calls, and
nothing else.

```
ev_macro.c SendIngameEndingNormal(1) -> GID_ENDING_NORMAL1  scene 51 (S1020)
                                     -> GID_ENDING_NORMAL2  scene 53 (S1040)
                                     -> GID_GAMERESULT_TOP
ev_macro.c SendIngameEndingHard(1)   -> GID_ENDING_HARD     scene 52 (S1030)
                                     -> GID_GAMERESULT_TOP
```

- **The normal ending is two movies with a phase change between them**; the
  hard ending is one. That is the whole reason `GID_ENDING_NORMAL2` exists.
- **The literals are scene numbers, not file ids.** `GetFileNoFromSceneNo()` is
  `n * 3 + S0010_PSS`, so 51/52/53 resolve to CD files 3562/3565/3568 =
  **S1020 / S1030 / S1040** -- and the hard ending's movie sits *between* the
  two halves of the normal one in file order, which is why the numbers look
  scrambled.
- **The gallery unlock happens on the way in, not when the movie ends.**
  `init_Ending_Normal1()` calls `ClearFlgEndingNormalExe()` (ending_movie_flg
  bit 0) and `init_Ending_Hard()` calls `ClearFlgEndingHardExe()` (bit 1)
  before the movie starts, so an ending skipped with START still counts as
  watched. `init_Ending_Normal2()` calls neither -- the two movies are one
  ending.
- **`SendIngameEndingNormal(0)` in `end_Ending_Normal1()` is what stops the
  ending looping.** `IngameGetNextPhase()` keeps answering `GID_ENDING_NORMAL1`
  while the request flag is up, so the clear sits at the end of the *first*
  half, where the request is finally spent -- not in `end_Ending_Normal2()`,
  which nothing set a flag for. `end_Ending_Hard()` clears its own the same
  way.
- **All four `GID_ENDING_MOVIE` callbacks are genuinely empty** -- 8 bytes
  each, `jr ra` plus a delay slot. Unlike `GID_CLEARMENU` or
  `GID_SAVEPOINT_MAIN`, this parent owns no background, no fade and no BGM: the
  child's movie fills the screen, so there is nothing to draw under or over it.
  It exists only to hold the three children in the phase tree.
- `one_Ending_Hard()` is laid out differently from its two siblings -- 118/119
  where they have 70/73 and 92/95, and 121 for the `SetNextGPhase` where they
  have the line straight after the `if`. Source formatting, nothing more, but
  it is why its annotations do not mirror theirs.

**Not yet run in-game.** Every body was an empty stub, so both endings played
no movie, unlocked nothing in the gallery, and -- because the request flag was
never cleared -- `GID_ENDING_NORMAL1` would have re-entered itself every frame
rather than reaching the result screen.

### game_result.o — done, and ingame/clear/ with it

All 9 `ZERO2.MAP` `.text` exports plus 12 statics, verified 9/9 against the map
and 21/21 against `functions.txt`. `.text` is accounted for byte-for-byte
(0x1aa6f0..0x1ab0f8 = 0x9e0 of code plus ten 4-byte alignment fills = the
section's 0xa08, so there is no unlisted body). `GAME_RESULT_CTRL` (0x8) is
confirmed by an `offsetof` harness, the `SQAR_DAT` local initialiser reproduces
the ROM's `.rodata` image at 0x3b39e0 byte-for-byte, and the whole `.rodata`
(0x138) is accounted for: the `fixed_array` literal, four `__FUNCTION__`
strings, the assert banner, the 5-slot jump table and that blob.

It is the game-clear phase: the parent of `game_result_top.o`'s result page,
and the third member of the `savepoint_main.o` / `clearmenu.o` family.

- **Everything on screen is chosen by `ingame_wrk.mDifficulty`**, and all three
  dispatches assert on anything outside 0..3: difficulty 0/1 take
  `GAMECLEAR_BG_A_PK2` and `gameclear_tex[0..5]`, 2/3 take `GAMECLEAR_BG_B_PK2`
  and the second copy at `[6..11]`. The text pak is
  `GAMECLEAR_CHARA_PK2 + GetLanguage()`.
- **The background is six sprites, four upright and two rotated 270 degrees**
  about their own already-offset position — the same
  `rot`/`crx`/`cry` = `(x, y + (float)ds.w)` idiom `setup_menu.o`'s cursor uses,
  `DISP_SPRT::w` being `u_int` and so emitting the halve/convert/double
  unsigned-to-float sequence. It is drawn at a fixed alpha 0x80; the fade is the
  black quad over the top, not this.
- **This is where the clear BGM starts, and `init_GameResult()` hands the
  stream id straight to `clearmenu.o`** via `SetClearMenuStreamID()` — one
  nested statement, since the `jal` carries no marker and `functions.txt` lists
  no local. That is why `GameResultFadeOutReq()` does *not* fade the music
  where `ClearMenuFadeOutReq()` does: the same stream plays on under the clear
  menu, and step 4 hands over to `GID_CLEARMENU_TOP` as a sibling phase.
- **`one_GameResult_Top()` tests `step == 3`, not `< 4`.** clearmenu.o's twin
  steps its menu on anything below FADE_OUT; here the page takes input only in
  the open state, so it is inert during *both* fades.
- **Field order in this file is step-then-timer**, in `GameResultCtrlInit()`
  (117/118), the step-1 arm of `GameResultMain()` (250/251) and
  `GameResultFadeOutReq()` (280/281) alike — even though `GAME_RESULT_CTRL`
  puts `anim_timer` at 0x0 and `step` at 0x4, the reverse of
  `CLEAR_MENU_CTRL`.
- **Step 3 is a real, empty case** — jump table at 0x3b39b0, slot 3 pointing at
  the break label, default asserting. Same reading as the other two twins.
- `GameResultBgDisp`'s first loop is a *countdown* in the ROM (`li 3` … `bgez`)
  while its difficulty-2/3 sibling keeps a real induction variable — GCC
  strength-reducing one and not the other. Both are four iterations of the same
  `for`; do not read the countdown as a backwards walk, the table pointer
  increments.

**One place is not fully recoverable and says so at the site.**
`GameResultTexLoadWait()`'s difficulty dispatch is emitted as a single call
sequence with only the pak literal differing between the arms, so the second
arm's line notes were eliminated and the switch cannot be laid out from the
stabs. What is measured: `res` seeded at 205, the surviving literal at 217, the
two `FileLoadIsEnd2` calls at 220/221, a `return res` at 225, the assert at 227
and a second `return res` at 231 that the assert path falls into. The pak
number is not in the stabs (only `res` is), so its spelling is the port's.
The evidence for a *selection* rather than two duplicated bodies is
`GameResultBackGroundLoadReq()` in the same object: its structurally identical
two-call arms were **not** merged, so GCC 2.96's cross-jumping is not
aggressive enough to have collapsed two full bodies here.

Two general readings this object confirms, both worth carrying:

- **Two `$LM`s at one address are two statements, one of which produced no code
  of its own.** `GameResultMain` has 250 and 253 both at the `b` that ends its
  step-1 arm (250 is the `sb` in its delay slot, 253 the `break`), and
  `GameResultBgDisp` has 383 and 391 both at the head of its second arm's loop
  — 383 being the *first* arm's `break`. That second pair is what pins the
  switch's shape: `}` 382, `break` 383, two `case` labels 384/385, `for` 386.
- **A switch on `mDifficulty` leaves no marker of its own**, because the inlined
  `CVariable::Get()` moves the line to variable.h 167 and the statement gets no
  fresh note. All three in this file behave that way.

**`ingame/clear/` is now complete** — six code TUs (`clear_flg.o`,
`clearmenu.o`, `clearmenu_top.o`, `ending.o`, `game_result.o`,
`game_result_top.o`) plus the two data TUs, every one verified against its
`ZERO2.MAP` exports, with no stub left in the folder. The one residual is
`clear_flg.o`'s `global constructors keyed to clear_flg_ctrl` node, a compiler
artifact rather than a body.

**Not yet run in-game.** Twelve of this object's twenty-one bodies were
placeholders, so the clear screen loaded no pak, drew no background, never left
step 0 — which meant `game_result_top.o`'s finished reconstruction sat behind a
page that could not open — and never started the clear BGM, so
`ClearMenuFadeOutReq()` was fading a stream id of 0. The whole
ending → result → clear-menu chain is now real code end to end.

### audiodec.o — done

All 10 `ZERO2.MAP` `.text` exports plus 4 statics, verified 10/10 against the
map and 14/14 against `functions.txt`. `.text` is accounted for byte-for-byte
(0x279c88..0x27a52c = 0x88c of code plus six 4-byte alignment fills = the
section's 0x8a4, no gap of 8 bytes or more, so there is no unlisted body), the
whole `.rodata` — 0x142 bytes, three printf format strings and the alignment
padding between them — is byte-identical to the ROM (diffed out of the compiled
`.obj`), and `AudioDec` / `SpuStreamHeader` / `SpuStreamBody` are confirmed by
an `offsetof` harness. Every function is driven against a transcription of its
disassembly over 400 000 randomized cases each — output spans, DMA sequences,
return values and the exact `sceSdRemote()` argument lists all match.

It is the audio half of the PSS movie player: two rings and the pump between
them. The demuxer writes the EE ring through `audioDecBeginPut()` /
`audioDecEndPut()`; the SPU2's auto-DMA reads continuously out of an IOP-side
ring it was pointed at once; `audioDecSendToIOP()` copies whole 1024-byte blocks
from the first into the second. **There is no interrupt and no callback anywhere
in the file** — the whole thing is polled off the auto-DMA's play address.

- **The header is read through the same producer API as the samples.** While
  `state` is 0, the free span `audioDecBeginPut()` reports is the 40-byte
  SShd/SSbd pair *inside `AudioDec` itself* (`(u_char *)&ad->sshd + hdrCount`),
  not the ring — so the demuxer's copy loop never knows which it is filling.
  `audioDecEndPut()` takes the header's share off the front of `size` before the
  rest advances the ring, which is why one call can carry both.
- **Nothing below 1024 bytes ever moves.** Every length is rounded down to a
  whole block, both ends are refused if either has less than one, and
  `iopGetArea()` measures the play/write gap a block short. The stream runs up
  to a block behind for ever, deliberately.
- **`audioDecPause()` reads the play position by stopping the DMA.**
  `SD_BLOCK_TRANS_STAT` answers with the address it had reached; `& 0xffffff`
  because the IOP's map is 2 MB and the top byte is status. `audioDecStart()` is
  `audioDecResume()` with `iopPausePos` still 0.
- **`iopGetArea()` tests its gap unsigned** (`sltiu`, on an `int` local) so a
  play position that has wrapped past the write cursor — negative — reads as
  "plenty of room" rather than "less than a block".
- **`sendToIOP2area()` writes all three seam cases out in full**, and trims an
  over-long source from the tail: out of the second span first, then out of the
  first.

**The `fno` numbering of `sceSdRemote()` is recoverable and worth writing down:**
it is libsd's export index times 0x10 plus 0x8000. `sceSdRemote()` itself
(0x2972a0) special-cases 0x8160 and 0x8170 to stash a transfer / SPU2 interrupt
handler, which pins indices 22 and 23 — `sceSdSetTransIntrHandler` and
`sceSdSetSpu2IntrHandler` — and the rest of the table falls out. So 0x8010 is
`sceSdSetParam`, 0x80d0 `sceSdVoiceTrans`, 0x80e0 `sceSdBlockTrans`, 0x8100
`sceSdBlockTransStatus`; each call site's argument shape then matches `libsd.h`
exactly, which is the independent check.

Two places the ROM's own spelling is not recoverable, both flagged at the site:
`changeInputVolume()`'s scaled volume (line 460) and `audioDecSendToIOP()`'s
`pos` / `len` (321 / 335) are each a statement with a line note of its own whose
destination has no RSYM. The `int` local list is otherwise exhaustive here, so
these are the `save_load_disp.o` case again — a real source local whose pseudo
was coalesced away — not CSE temps. (The EE ring's read cursor at 331/337 *is* a
CSE temp: written twice, one expansion.)

New in the port's SDK, all additive:

| File | What |
|---|---|
| `libsdr.h` / `libsdr.cpp` (new) | `sceSdRemote()` / `sceSdRemoteInit()`; the RPC dispatches straight into the libsd shim |
| `sifman.h` / `sif.cpp` | `sceSifSetDma()`, the EE→IOP half of the transport |
| `libsd.h` / `iop_libsd.cpp` | `sceSdBlockTransStatus()` plus the per-core latch it answers from |

**PORT DEVIATION, flagged in `libsdr.cpp`:** the ROM's `sceSdRemote()` marshals
six argument words whether the callee wants them or not, which is free on the EE
and undefined through `va_arg` here — `audioDecPause()` stops the auto-DMA with
four arguments where `audioDecResume()` restarts it with five, and
`audioDecSendToIOP()` passes `sceSdBlockTransStatus()` only the core. The shim
reads exactly what each call carries.

**Nothing calls any of this yet, and that is expected.** `audioDecCreate()` has
exactly one caller in the ROM — `playpss.o` — and `playpss.o` is the port's one
deliberate shim, because it is a hardware pipeline (IPU, GS PATH3, SPU2).
`audiodec.o` is not, so it is reconstructed rather than shimmed. Note also that
`sceSifAllocIopHeap()` hands out addresses from the IOP's own map that nothing
here has mapped, so `sceSifSetDma()` drops the transfer and names it once rather
than faulting; that is the one thing between this module and audible movie sound.

### playpss.o — done, and the folder with it

All 16 `ZERO2.MAP` `.text` exports plus 7 statics, verified 16/16 against the
map and 22/22 against `functions.txt`. `.text` is accounted for byte-for-byte
(0x278eb0..0x279c84 = 0xdb4 of code plus eight 4-byte alignment fills = the
section's 0xdd4, so there is no unlisted body). `PLAY_PSS_FLAGS` (0x4) and
`sceMpeg` (0x48) are confirmed by an `offsetof` harness — including that each
of the five bits lands where the ROM's `ori 1/2/4/8/0x10` and `andi 0xffe4`
masks say — and `vblankHandler`, `videoCallback`, `nodataCallback`, `fillBuff`
and the whole state machine are driven against transcriptions of their
disassembly (200 000 randomized cases each for the three ring bodies).

It is the movie player's **driver**: it owns no codec. Four rings and a flag
word turn libmpeg, ldimage and audiodec.c into a movie.

- **Two decode paths, chosen once by `playPssSetNtsc2Pal()`.** NTSC decodes
  inline in `playPssSetPacket()`, once per game frame. PAL cannot — the console
  runs at 50 Hz and the film at 29.97 — so `vblankHandler` counts 11988/20000
  of a film frame per field and posts a semaphore, and a thread
  (`videoDecMain`) does the same work off it. `playPssSetPacket()` then does
  nothing but report whether that thread has finished. **This is the whole of
  the "NTSC2PAL" conversion**: no scaling, no field doubling, just a divider.
  The harness confirms 119 880 posts in 200 000 fields, exactly 29.97 Hz.
- **`demuxBuff` is a three-pointer ring, not two.** Past is where the demuxer
  writes; Present..Future is the slice currently being DMA'd into the IPU.
  Equal pointers are ambiguous, and `videoCallback`'s `Present == Future` test
  is the only thing that separates "empty" from "full".
- **The player refills in the gaps, on a scanline budget.** Timer 0 is
  programmed with `CLKS = 3` so it counts H-BLANKs, and both
  `playPssSetPacket()` and `backgroundCallback()` spend a measured number of
  lines in `fillBuff()` before giving the frame back — 500 lines for a full
  640x448 picture, scaled by area, and `bgDuration` (width * height * 0.000151)
  for the background pass. That constant is one ulp low, so it is spelled
  `0.000150999986f`; the ROM has three identical `.lit4` slots for it, one per
  expansion.
- **A callback returning 0 is back-pressure, not an error.** `videoCallback`
  refuses a payload that will not fit and libmpeg hands the same one back next
  pass, so a full ring stalls rather than drops.
- **`fillBuff()` demuxes the mux block from its end.** `muxBuffFullness` counts
  down as the demuxer eats it and the cursor is derived from that
  (`muxBuff + 0x4000 - muxBuffFullness`) rather than kept.
- **`audioCallback` takes 4 bytes off the front of every payload** — the PSS
  sub-stream header — and *writes them back* into `cbstr->len`/`cbstr->data`
  before handing the rest to audiodec.c.
- **`playPssPause()` is gated on the audio having started and
  `playPssRestart()` is not.** That asymmetry is the ROM's.
- **`playPssStart()`'s `SemaParam` has no stab**, and the ROM fills only two of
  its six fields — currentCount, numWaitThreads, attr and option go to the
  kernel uninitialised. `fillBuff`'s demux result is the same: a real local the
  debug info omits. Third instance of that pattern in this archive.

New in the port's SDK, all additive:

| File | What |
|---|---|
| `libmpeg.h` / `libmpeg.cpp` (new) | the nine `sceMpeg*` entry points; **this is now the one lie in the movie chain** |
| `system/playpss/ldimage.h` / `.c` (new) | `setLoadImageTags` / `loadImage`, stubbed and marked NOT RECONSTRUCTED |
| `eekernel.h` / `.cpp` | `TerminateThread`, `DeleteThread`, and a real INTC handler registry (`AddIntcHandler` / `RemoveIntcHandler` / `EnableIntc` / `MioPan_IntcRaise`) |
| `eeregs.h` / `.cpp` | EE Timer 0 (a proxy object, because the count must be readable, assignable *and* free-running or the budget loops never end) and the DMAC channel 4 registers |

**The lie moved down a level, which is where it belongs.** playpss.c used to be
a shim; it is now a full reconstruction and `sdk/libmpeg.cpp` reports an empty
stream instead. `sceMpegIsEnd()` answering 1 is what ends a cutscene on its
first frame, and `sceMpegDemuxPss()` answering 0 is what keeps the two
`while (fillBuff(mp, 1) != 0)` loops from spinning. A real decoder goes behind
those nine functions and nothing above them changes.

**One behaviour change to know about:** `playPssAlreadySendImage()` is faithful
now, so in NTSC it answers 1 and `PlayMovie()` blits one frame from
MOVIE_VRAM_ADRS at the end of a cutscene. The old shim answered 0 to avoid
exactly that. It is one frame of stale VRAM per movie and it is what the ROM
does; suppressing it would mean putting a second lie inside the reconstruction.

**Trap this cost a crash to find: `MioPan_IopMemIsBareOffset()` answered 0
until the IOP arena existed**, and the movie path never creates one —
`sceSifAllocIopHeap()` (sifdev.cpp) is a separate bump cursor over the IOP's
own 2 MB map, and it is where `movie.c`'s `iopalloc()` gets audiodec.c's rings.
So a 0x16000 went straight into `sceSifSetDma()`'s memcpy. The predicate now
rejects anything below 4 MB whether an arena exists or not; that closes the
same hole in `sceSdVoiceTrans()`, which had it too.

**`system/playpss/` is 3 of 4.** `playpss.o`, `audiodec.o` and `my_strfile.o`
are reconstructed; `ldimage.o` (2 exports, 7 statics, 0x464) is the one left
and is the natural companion to a video decoder.

### file_stream.o — done, and the streaming path is live

All 5 `ZERO2.MAP` `.text` exports plus 4 statics, verified 5/5 against the map
and 5/5 against `functions.txt`. `.text` is accounted for byte-for-byte
(0x276670..0x27699c = 0x328 of code plus one 4-byte alignment fill = the
section's 0x32c). The RPC payloads and every `sceSifCallRpc()` argument are
driven by a harness — payload field offsets, the values written, the fno, the
mode, both window sizes and the lock flag.

It is the **other** way the game gets bytes off the disc. An ordinary load
(`fileload.c`, RPC 3) asks for a whole file and waits; this asks the IOP to keep
a ring of sector buffers topped up from one file and then hands over slices on
demand. `playpss.a`'s `my_strfile.c` is the only caller, which makes a movie the
only thing in the game that streams.

- **There is no state on this side beyond a lock flag.** The ring, the reader
  thread and the file position all live on the IOP; the file is three RPC
  payloads and the marshalling for them.
- **`FileStreamStart()` resolves the file on the EE and sends the answer.** The
  IOP has the CD table too, but the ROM calls `GetFileNameBuffer()` /
  `GetFileStartSector()` / `GetFileSize()` here and puts all three in the
  payload. Field order in the source is start_sector, ring_buf_num,
  one_buf_size, size — *not* the struct's order, which is what the line notes
  settle (47 stores offset 4, 48 stores offset 0).
- **`block_read` picks the RPC mode, and the sense is not what it looks like.**
  Non-zero waits (`mode` 0); zero posts with `SIF_RPC_M_NOWAIT` and leaves
  `FileStreamIsAct()` to report. `my_strfile.c` uses the second and polls.
- **Both size checks are asserts, not recoveries** — a non-sector-aligned size
  and an overlapping call each mean the caller has lost track of its own
  request. Their ROM line numbers are recoverable from the `PRINT_ASSERT`
  banners' own arguments (0x45 = 69, 0x48 = 72).
- **`FileStreamInit()`'s retry loop is load-bearing.** `CreateRPCLoadStmThread()`
  runs on the EE's REQ_IOP_REBOOT round trip and `StartThread()` is
  asynchronous, so service 4 may not be registered when the bind is first tried.
  A bind that *fails* is a different matter and hangs deliberately. Same shape
  as `FileLoadInit()`, which has been doing this successfully all along — that
  is the evidence the spin terminates on the host. The delay between retries is
  an empty `for (i = 0; i < 10000; i++)`, which GCC 2.96 unrolled ×4 (`fileload.c`
  transcribed the *unrolled* form; the stab lists `i`, so the source is the
  rolled one).

**PORT: the truncation this was blocked on is fixed.** `strSTM_READ::ee_buf` is
a pointer — 4 bytes on the PS2, 8 here — and the IOP read it as word 1 of the
payload (`sect[1]`), then carried it through `CdvdStmRead()` and
`RingBufTransEE()` as an `int`, so a real host pointer lost its top half and the
SIF DMA landed on a wild address. `iopCommandLoadStm()` now reads the payload as
`strSTM_READ *` and both signatures take `uintptr_t`; `MyTransEEWait()` was
already widened. Only the `zero_rom/` copy of the IOP tree is changed — the
standalone `iop/src/` reconstruction targets the real IOP, where `int` is right.

One SDK addition: `SIF_RPC_M_NOWAIT` / `SIF_RPC_M_NOWBDC` in `sifrpc.h`.

**Not yet run in-game, and it is the first time this path has ever executed.**
Every body was a stub, so a movie's stream was opened and never read. What to
watch for: a boot hang would be `FileStreamInit()`'s bind spin (service 4 never
registering); a teardown hang would be `ReleaseRingBufSubWait()` printing
"=========Wait Read End========" every 100 ms. Note that a `FileStreamStop()`
with no preceding `FileStreamStart()` *would* hang there for ever — nothing
would ever set `load_end_flg` to 2 — but `movie.c` cannot produce one:
`InitMovie()` sets `iMovieCnt` to 0 only after `MyStrStart()`, and `EndMovie()`
returns early while it is negative.

**Bytes now arrive, but nothing decodes them yet** — `sdk/libmpeg.cpp` still
reports an empty stream. The remaining two, in order: `ldimage.o` (the GS
transfer chain) and then libmpeg itself.

### ldimage.o — done, and system/playpss/ with it

Both `ZERO2.MAP` `.text` exports plus 8 statics, verified 2/2 against the map
and 10/10 against `functions.txt`. `.text` is accounted for byte-for-byte
(0x27a530..0x27a994 = 0x450 of code plus five 4-byte alignment fills = the
section's 0x464). **The object has no data sections at all** — no `.rodata`,
`.data`, `.bss`, `.sdata` or `.sbss`, which is why `globals.txt` has no section
for the file. The emitted DMA chain is checked quadword-for-quadword against a
transcription of the ROM's packet across seven picture sizes, and the port's
chain walker against the transfers that packet describes.

It is the GS side of the movie player: the IPU's decoded output turned into a
PATH3 DMA chain.

- **"YX" in `setLoadImageTagsYX` is the macroblock order.** The IPU writes a
  picture as 16x16 RGBA32 blocks stored Y-first, so the inner loop walks *down*
  a column and the outer walks across — `i` is the column and `j` the row, which
  is the opposite of how the nesting reads.
- **One transfer per macroblock, and only the position changes.** BITBLTBUF and
  TRXREG are set once at the head (destination page and 16x16 size never vary);
  each block is a CNT tag carrying GIFtag + TRXPOS + TRXDIR + the IMAGE GIFtag
  inline, then a REF tag pointing at its 1024 bytes.
- **EOP goes on the last block's IMAGE tag and an END tag closes the chain**, so
  `loadImage()` is three DMAC register writes and nothing else.
- **`p` is a running quadword pointer with `p += 4` after every helper**, which
  is what the line notes say (122 call, 130 advance, then 131/132/133 with their
  advances scheduled into those windows). Ghidra renders it as `p+4`, `p+8`,
  `p+0xc` off a base with one `p += 16` at the end — the same packet either way.
- **`setDMAscTag` writes exactly one 64-bit word** and leaves the quadword's
  upper half alone; `setGIFtag` and `setGIFad` write four `u_int`s each. That
  asymmetry is what makes the port deviation below possible.
- **The packet size confirms the reconstruction against `InitMovie`.** A
  640x448 picture needs 4 + 1120*6 + 1 = 6725 quadwords = 107600 bytes, and
  `movie.c` allocates `path3tag` at 0x1a500 = 107776. An off-by-one in the
  per-block quadword count would not fit.

**Two PORT deviations, both local to this file and marked at the site:**

- A DMA tag's ADDR field is 31 bits of physical address and the macroblocks live
  behind 64-bit host pointers, so `setDMAscTag()` additionally stores the real
  pointer in the tag quadword's upper half — the eight bytes the EE transfers
  only under `CHCR.TTE`, which is never set here, so the ROM never writes them.
- `loadImage()` walks the chain in software after writing the three registers,
  accumulating BITBLTBUF/TRXPOS/TRXREG/TRXDIR out of the PACKED A+D items and
  handing each macroblock to `MioPan_GsUpload()`. Note the image data is never
  inline — the IMAGE tag is the last quadword of a CNT block and its payload is
  the REF block *after* it, so the pending count has to survive across one tag.

**`loadImage()` must clear `CHCR.STR` when the walk finishes**, and that is not
decoration: `g3dGsPutDrawEnv()` spins on that bit before every draw environment
and on the EE the channel cleared it itself. Leaving it set costs a
`G3DGS_SPIN_LIMIT` stall and an error print per frame.

**`system/playpss/` is now 4 of 4** — `playpss.o`, `audiodec.o`, `my_strfile.o`
and `ldimage.o`, with no stub left in the folder.

**Not yet run in-game.** The path is complete from the disc to GS memory now:
`file_stream.o` delivers the bytes, `playpss.o` drives the machine, this puts
the picture in VRAM. The one thing still missing is the decoder — `libmpeg.cpp`
reports an empty stream, so nothing ever reaches `setLoadImageTags()`.

### Movie audio — the demuxer is real, and the streams are not what they look like

Two facts read off the disc rather than assumed, both of which change the plan
for the movie player:

- **The video is MPEG-2, not MPEG-1.** Every sequence header in IMG_BD.BIN is
  followed by a `sequence_extension` (`00 00 01 B5`, ext id 1), and every
  picture carries a `picture_coding_extension` with `alternate_scan=1`,
  `intra_vlc_format=1`, `q_scale_type=1`, `intra_dc_precision=1`. The GOPs are
  full IPB — one 160x128 film measures 56 I, 222 P, 552 B. **pl_mpeg and any
  other MPEG-1 decoder cannot be bent into this.**
- **The audio is not a custom codec.** Private stream 1 (0xBD), a 4-byte
  sub-stream header (exactly the `+4` `audioCallback` steps over), then SCE's
  SPU stream container: `SShd` size=24 **type=1 (little-endian PCM)** rate=48000
  ch=2 interSize=512, then `SSbd`. So it is plain 48 kHz 16-bit stereo PCM.
  `audiodec.c` already parses all of it.

Stream layout, for anyone touching the demuxer: 16 KB packs (MPEG-2 pack header,
14 bytes, `pack_stuffing_length` 0), then PES packets — 0xE0 video, 0xBD audio,
0xBB system header, 0xBE padding out to the pack boundary, and `00 00 01 B9`
after the last pack. `MyStrStart(no, 10, 8)` reads 8 sectors = 16 KB, so **one
`fillBuff()` read is exactly one pack**.

Content note worth knowing before chasing a silence bug: the movie-room films
are near-silent *by design* (33.4 s of video against 0.7 s of audio). The
cutscenes are not — 52.5 s of video against 52.2 s of audio.

**`sceMpegDemuxPss()` is now real**, verified against a 2.5 MB PSS pulled off
the disc: 2 399 933 bytes of video and 135 208 of audio, byte-identical to a
reference extraction, in three passes — clean, and with 1-in-3 and 1-in-7
synthetic callback refusals. That last part is the contract that matters:

- **A callback returning 0 is back-pressure and nothing may be consumed.**
  `fillBuff()` hands the demuxer a cursor, not a length, and subtracts the
  returned count from `muxBuffFullness`, so one call takes exactly one packet
  and a refused one is re-offered next frame. Under refusal the demux-call count
  rises (784 -> 1095) and the output is bit-identical.
- **`audioCallback` mutates `cbstr` *before* it refuses** (the `-4` / `+4`), so
  the record has to be rebuilt from scratch on every offer.

Three consequences that are easy to get wrong:

- **`sceMpegGetPicture()` must still dispatch the Nodata callback** even though
  it decodes nothing. That callback is what retires the finished slice of
  `demuxBuff`; without it the ring never drains, `videoCallback()` refuses for
  ever, and because the demuxer cannot step past a refused packet **the audio
  behind it never arrives either**. A missing decoder would deadlock the whole
  pipeline rather than just losing the picture.
- **`sceSifAllocIopHeap()` now serves real memory** out of the IOP arena
  (`MioPan_IopAllocSysMemory`), not the old fictional 2 MB map. It has to: the
  audio ring is written by `sendToIOP()` and read by the SPU2 auto-DMA, and both
  have to see the same bytes. The arena sits above 16 MB and under 4 GB
  precisely so the address survives the round trip through `int` the ROM puts it
  through (`iopalloc()` -> `playPssRsrcs::iopBuff` -> `AudioDec::iopBuff`).
- **Movies now run their real length.** `sceMpegIsEnd()` only answers yes at the
  0x1B9 terminator, so a cutscene plays to the end over a blank screen rather
  than ending on frame one. START/CROSS still skips it.

**The SPU2 auto-DMA plays for real.** `iop_voice.cpp` gained an external-input
stream per core (`MioPan_VoiceAutoDma*`), bound to the same SDL device as the
voices; `sceSdBlockTrans()` arms it, `sceSdBlockTransStatus()` reports its play
position, and BVOLL/BVOLR drive its level — which is what
`playpss.c`'s `changeInputVolume()` writes, and why a movie can fade without
touching game audio. Stereo is alternating 512-byte blocks, left first: the
SPU2's own transfer granularity, and what `SShd.interSize` reports.

The read position is deliberately **what has been heard, not what has been
pushed** (pushed bytes minus what SDL still has queued). `audioDecSendToIOP()`
subtracts it from its write cursor to decide how far ahead it may fill, so
reporting the write position would let the writer lap the reader.

**Correction, and a trap worth naming: `sceMpegGetPicture()` must return 0, not
a negative, when there is no decoder.** Both call sites read `< 0` as a decoder
*failure* and raise `Flags.bErrorCallback`, which line 425 turns into
end-of-movie on the very next frame -- so a negative return kills the movie
before the audio has started, and the symptom is "sceMpegGetPicture failed" and
silence. It reads like the video codec being a prerequisite for audio; it is
not. "No picture this frame" is 0, and the negative return stays reserved for a
decoder that really did fail.

Two things go with it, both needed once the movie actually runs:

- **The demuxer reads the picture size out of the sequence header.** That is
  what lets `playPssGetMpegInfo()` answer correctly with nothing decoding --
  `CMovieRoom::Draw()` sizes its screen quad from it -- and it is 12 bits each
  straight after `00 00 01 B3`.
- **The picture buffer is blanked once per movie.** With the geometry known the
  transfer chain is a real one, and without this it would walk uninitialised
  heap into GS memory every frame instead of black.

And drain **several** Nodata slices per `sceMpegGetPicture()`, not one: a
640x448 picture in these streams averages ~9.5 KB (14.9 MB over 1573 pictures),
and retiring less than a frame's worth leaves the ring full, which turns
`playPssSetPacket()`'s scanline refill budget into a busy wait on a demuxer that
can no longer make progress.

### g2d_debug.o — done

All 4 `ZERO2.MAP` `.text` exports plus the one function-local static, verified
4/4 against the map and 4/4 against `functions.txt`. `.text` is accounted for
byte-for-byte (0x197c08..0x198288 = 0x680: 8 + 8 + 8 + 0x668, no alignment fill
and no unlisted body), all four string literals are byte-identical to the ROM
(diffed out of the compiled `.obj`, the 0xa5 decimal point included), and the
whole meter is driven against a transcription of its disassembly over 3.6M
`(percount, perf_max, draw_counter)` triples — colours, bar ends, both read-out
splits and the branch taken all match.

It is the frame performance meter `SendDMAMain()` draws over the finished
frame: a two-bar gauge with a peak hold, and three numeric read-outs.

- **Almost the whole file is gone, and that is the headline.** `.text` is 0x680
  and the four bodies fill it exactly, yet the `$LM` stabs put the first
  opening brace at 1716 and the last closing brace at 2187. `SetShibataSet()`
  opens at 1720 and its `jr ra` carries 1855; `CheckHintTex()` opens at 1977
  and closes at 2107 — 134 and 129 lines each compiling to a bare return.
  Nothing of either survives in the object.
- **All three of those and all four globals are dead.** A jal/j scan finds no
  caller of `InitShibataSet`, `SetShibataSet` or `CheckHintTex`; a gp-relative
  scan finds no reference to `dither_alp`, `dither_col`, `hint_test_sw` or
  `hint_test_posx` anywhere, and `sbtset_old` is written by `InitShibataSet`
  and never read. `InitShibataSet` is 8 bytes with its one statement in the
  `jr ra` delay slot — [[empty-looking-functions-may-have-delay-slot-bodies]].
- **Everything in `.sdata` had an explicit initialiser.** Four of the five are
  zero and still land in `.sdata` rather than `.sbss`, which is the tell: GCC
  only sends a small object to `.sbss` when it has no initialiser at all. The
  two non-zero ones are 64 and 128.
- **The colour ramp is blue, not white.** r and g climb together while b falls,
  so under 100% the bar runs (0,0,255) → grey at half a frame → (254,254,1);
  100..200% drops g only, yellow to red; past 200% it holds flat red. The `~`
  in `b = ~(u_char)(percount * 255.0f)` is a real `nor`, not `255 - x` (which
  would be `subu`).
- **The bar's zero is at x = -152 and the bright grid line at x = 48 is 100%.**
  400 pixels span 0..200%, which is what makes `* 200.0f` and the four fixed
  abscissae line up; the read-out beside it is `* 448.0f`, i.e. scanlines. The
  bar coordinates are the drawing layer's screen-centred space and the text
  coordinates `SetString2()`'s top-left one, so 190..220 and 401..431 describe
  the same band — [[two-2d-coordinate-spaces]].
- **Two ROM bugs in the draw-perf line, both reproduced.** The `> 80` test is
  written before the `> 100` test, so the red arm is unreachable; and the two
  thresholds are in unscaled units while the number printed beside them is
  `draw_percount * 100`, so neither would fire at the load it names even in the
  right order. Every frame prints white — the harness takes the `>100` arm 0 of
  3.6M times.
- `percount < 1.0f` compiles to `c.lt.s` and `percount < 2.0` goes through
  `__fptodp`/`__dpcmp`, so the second literal really is a double in the source.
  The three `percount * 255.0f` expansions are the source's own as well: each
  float→unsigned conversion carries its own fixup branch, so the three sit in
  different basic blocks and 2.96's local CSE cannot reach across them.

Two general debug-info readings this module is the clearest example of:

- **`li` + `mtc1` + `cvt.s.w` for a *constant* float argument means the source
  passed an `int`.** GCC folds `(float)-152` at tree level, so a float literal
  would have come out as `lui at,0xc318`; what produces the int path is cprop
  substituting a propagated constant into the use without re-folding the
  conversion. Here it names five locals `functions.txt` lists but the
  arithmetic never explains — `x1..x5`, the bar's fixed abscissae, numbered in
  first-use order — and settles that they are those rather than the five modulo
  numerators that share `v0`. See [[unstabbed-register-is-a-cse-temp]] for the
  other half: the numerators have no stab, so they are subexpressions.
- **If-conversion deletes a statement's line note along with its branch.** Both
  clamps are `slti`+`movz` and neither has an `$LM`, so a clamp written on its
  own source line is indistinguishable here from one sharing the assignment's.
  Do not read a missing marker as "same line" wherever GCC produced a
  conditional move.

**Not yet run in-game**, though the call site was already there: `SendDMAMain()`
has been calling `DrawPerformanceCounter2(draw_perf_count)` into an empty stub,
and it now draws the moment `debug_var.perf_count_sw` is raised. **The gauge
carries real numbers as of `zero2_perf.o` + `perf_measure.o` below** — it read
flat zero while `C_ZERO2_PERF_CNT::GetPercent()` was a stub returning 0.0f.

**The IOP arena has to live below 16 MB, and the reason is the ROM's own 24-bit
address mask.** `audioDecSendToIOP()` and `audioDecPause()` both read the
auto-DMA play position back as `(status & 0xffffff) - ad->iopBuff`. On hardware
the mask is a no-op -- the IOP has 2 MB -- but with the arena reserved at 16 MB
it strips the top bits, the subtraction goes hugely negative, `iopGetArea()`
reports a negative writable span, `d0 + d1 < 1024` and **nothing is ever sent
after the preload**. The symptom is unmistakable once you know it: the movie
plays its first ring-full of audio on a loop and never advances.

`IopArenaInit()` now walks 0x00200000 upward and takes the first reservation
whose whole 4 MB fits under 0x01000000 (measured: it lands at
0x00200000..0x005fffff on Win64), falling back to the old 16 MB walk with a
printed warning that names this consequence. Keeping the arena where the ROM's
assumption already holds beats deleting the mask from a verified
reconstruction -- and note the mask is in *two* places, so patching it would
mean touching audiodec.c twice.

Measured both ways over 399 simulated frames: arena low, 398 frames ship data
(434 176 bytes); arena at 16 MB, 0 frames ship anything after the 26 624-byte
preload.

### zero2_perf.o + perf_measure.o — done, and the frame meter now reads

Both objects: 5/5 and 4/4 `ZERO2.MAP` `.text` exports, verified against the map
and against `functions.txt`, with `.text` accounted for byte-for-byte
(0x26d4a0..0x26d6e0 = 0x34 + 0x14c + 0x1c + 0x38 + 0x38 of bodies, four 4-byte
alignment fills and the 8 + 0x20 of static-init machinery; 0x22dea8..0x22df18 =
0x28 + 0x14 + 0x14 + 0x18 plus two fills). `C_PERFORMANCE_MEASURE` (0x4) and
`C_ZERO2_PERF_CNT` (0x8) are confirmed by an `offsetof` harness, all six float
literals round-trip bit-for-bit, both string literals are byte-identical to the
ROM (checked against the ELF and then in the compiled `.obj`, the 0xa5 decimal
point included), and the row ordinate and the percent/two-decimals pair are
driven against a transcription of the disassembly over their whole domains.

Together they are the engine's stopwatch and the thing `g2d_debug.o`'s meter
reads. **They are also the last link in that chain** — the meter was drawing a
frame, a grid and a flat-zero bar until these landed.

- **EE timer 1 is the clock and `0xc82` is the whole specification.**
  `C_PERFORMANCE_MEASURE::FrameStart()` writes it to `T1_MODE` (0x10000810):
  CLKS = 2 (BUSCLK/256 = 147.456 MHz / 256 = **576 kHz**), CUE = 1, and
  write-one-to-clear on EQUF and OVFF. The count is 16 bits and wraps every
  113.8 ms.
- **The 20480 divisor is a frame budget, and it settles that this is a 30 fps
  game.** 20480 ticks at 576 kHz is 35.6 ms; a 29.97 fps frame is 19219 ticks,
  which reads 93.8% — just under the meter's 100% grid line. At 60 fps it would
  read 47%, which is the check that rules out the other reading of BUSCLK.
- **`AddDraw` and `::SetMark` are dead code**, and `::GetPercentFromMark` with
  them: a jal/j scan finds no caller of the first two anywhere, and the third
  has exactly one call site, inside `AddDraw`. The live path is
  `FrameInit -> FrameStart/SetMark` (twice, from `system.c`) and
  `GetPercent -> Get` (once, from `DrawPerformanceCounter2`). Same pattern as
  `ene_mot_ctrl.o`'s and `fly_ctrl.o`'s exported-but-unused pairs.
- **`AddDraw` needs a cast the ROM's own signature forces.** It takes
  `const char *` (`PCc` in the mangled name) and `ZERO2.MAP` gives
  `SetASCIIString2`'s last parameter as a plain `char *`.
- **16 of `AddDraw`'s 24 body lines compile to nothing** (13..17, 19..22,
  24..25, 27..30, 33) — the rest of the row list, commented out around what is
  left. Its three row ordinates are three separate reloads of `m_NowCnt`, not a
  hoisted local: `functions.txt` lists `Cnt` as the only local.
- **`C_PERFORMANCE_MEASURE` belongs in its own header.** Of the 17 objects whose
  stabs carry it, `perf_measure.c` is the *only* one that does not also carry
  `C_ZERO2_PERF_CNT` and `CZero2PerfDisplay` — so `zero2_perf.h` includes
  `perf_measure.h`, and the four bodies that were stubbed inside `zero2_perf.c`
  belong to `perf_measure.o`. Ninth instance of the stub-in-the-wrong-file
  hazard.
- **`CZero2PerfDisplay` is left out rather than invented.** `types.txt` has it
  (0x8, `char *m_pFuncName` + `int m_iLine`, an `(int, const char *)` ctor and a
  destructor — a scoped block timer), but `ZERO2.MAP` carries no body for any
  member and no object expands one.

**This file is the cleanest proof in the tree that the ROM's brace style is
K&R for function definitions as well as for control statements.**
`::GetPercent`'s brace is line 40 and its one statement 41; `::GetPercentFromMark`'s
brace is 43 and its statement 44. Allman would need a signature line at 42 —
which is `::GetPercent`'s closing brace. Same in `perf_measure.c`: `Get` closes
at 39 and `SetMark`'s brace is 42. See [[rom-source-brace-style-is-knr]].

**PORT: `REG_RCNT1_COUNT` is now a live proxy** (`sdk/eeregs.{h,cpp}`), the same
shape `REG_RCNT0_COUNT` already had for playpss.c's scanline budget — a host
steady-clock reading scaled to 576 kHz, wrapping at 16 bits, with a write
moving the origin. It was a plain `volatile unsigned int` that never advanced,
which made **both** consumers report zero: this module and `system.c`'s
`draw_perf_count` snapshot in `GetVifEndTimer()`. Measured on Win64:
575 976 Hz, and a 33.3 ms frame reads 93.65% of the budget. `REG_RCNT1_MODE` is
new alongside it, a write-only latch like RCNT0's.

**Not yet run in-game.** Every body was a stub before this;
`c_zero2_perf_cnt.FrameInit()` runs every frame from `system.c` and now really
restarts the timer, so the meter reads the moment `debug_var.perf_count_sw` is
raised.

### FFmpeg — the MPEG-2 decoder behind sceMpegGetPicture()

Imported the way MikuPan does it: a prebuilt tree under `extern/ffmpeg`
(`include/`, `lib/`, `bin/`), imported CMake targets, and a POST_BUILD copy of
the DLLs next to the exe. `cmake/ffmpeg.cmake` is MioPan's, and differs from
MikuPan's in three deliberate ways:

- **Only avcodec, avutil and swscale.** `sceMpegDemuxPss()` is a full PSS
  demultiplexer already, so avcodec is fed raw elementary-stream bytes and
  there is no container left for avformat; the audio is plain PCM and needs no
  swresample. That is 111 MB of tree instead of 143.
- **It does not hard-fail when the tree is absent.** It reports
  `MIOPAN_HAVE_FFMPEG OFF`, the decoder compiles out, and movies play their
  audio over a black screen. MikuPan's `message(FATAL_ERROR)`s.
- **It prefers the toolchain's own import library** — `lib*.dll.a` under MinGW,
  `*.lib` under MSVC. MioPan builds with MinGW and links `-static`, which
  governs libgcc/libstdc++ and not the import libs, so DLL linking is fine.

**`avcodec-62.dll` imports `swresample-6.dll` at runtime even though nothing
links against it.** Ship it or the exe will not start, with a loader error that
names no library. Checked with `objdump -p`; `avutil` and `swscale` need
nothing beyond each other.

The decoder is `sdk/libmpeg_video.cpp`, kept apart from `libmpeg.cpp` so the
demultiplexer carries no FFmpeg headers and stays testable on its own. Three
things about it are not what a normal player would do:

- **The output is the IPU's layout, not a frame.** 16x16 RGBA32 macroblocks in
  Y-first (column-major) order, because that is what `ldimage.c`'s transfer
  chain indexes — block `i * mby + j` is column `i`, row `j`.
- **Alpha is forced to 0x80**, the PS2's opaque, not 0xff.
- **Pacing is a port deviation.** `sceMpegGetPicture()` is called once per game
  frame (60 Hz in NTSC) while the films are 25 or 29.97 fps; on hardware the IPU
  and the PTS machinery held the rate. Here the `frame_rate_code` out of the
  sequence header does it, and between due times the previous picture is left in
  the buffer — which is exactly what a held frame looks like to the chain.

`av_parser_parse2()` is needed, not just `avcodec_send_packet()`: the demuxer
hands over PES payloads, which are not picture-aligned.

Verified against the real elementary stream demuxed out of IMG_BD.BIN:
50 pictures decoded in 2.0 s of wall clock (the stream is 25 fps), geometry
160x128, `frameCount` tracking, alpha 0x80 on every pixel, and the picture
reassembled by *inverting* the macroblock mapping — which is what proves the
repack order rather than just that something decoded.

### mmanage.o — done

All 11 `ZERO2.MAP` `.text` exports, verified 11/11 against the map and 11/11
against `functions.txt`. `.text` is accounted for byte-for-byte
(0x218a00..0x218ee8 = 0x4d4 of bodies plus five 4-byte alignment fills = the
section's 0x4e8, no gap of 8 bytes or more, so there is no unlisted body), and
all three `switch` bodies are driven against a literal transcription of their
emitted branch trees over every `OL_LOAD_READY` value and both `bForceFree`
settings — return codes *and* the exact side-effect trace match on all 1920
cases.

It is the model manager: three resource families over one `OL_LOAD` slot table,
each adding nothing but a file-number base and the one fixup its data needs the
first time it lands (item -> `sgdRemap`, character model -> `motInitOneEnemyMdl`,
animation -> none).

- **The object has no data of its own.** `.rodata` (0x187) is the
  `fixed_array<%s,%d>` assert literal, `ModelMemoryFree`'s `__FUNCTION__` and
  its seven message strings; `.sdata` (0x42) is that literal's `str` pointer,
  the `"void*"`/`"char*"` type names, ten unused `_$tmp_N` slots and the
  two-byte `"\n"` that both `printf` runs share. `globals.txt` has no section
  for the file. Same signature as `subtitle.o` and `title.o` — and note
  `"unsigned int*"` is 14 bytes so it lands in `.rodata` rather than `.sdata`
  under `-G 8`, which is why the two lists look split.
- **`ModelMemoryFree`'s `StringBuf[2000]` is block-scope, not function-scope.**
  The stabs put it inside `$LBB9`, the innermost of three nested blocks, i.e.
  inside the *inner* `if (PreloadedEneAllRelease(...) == 0)` — which is the only
  place it is used. `functions.txt` cannot show that; see
  [[stabs-lbrac-reveals-block-scope-locals]].
- **Each family's Req and Clear go through a one-line file-number helper and
  IsReady does not.** ROM lines 45 (item), 87 (model) and 117 (animation) each
  sit in the gap just before their family's block and are expanded into exactly
  two functions each; `mmanageIsReady*` spells `mdl_no + BASE` out on its own
  call line (64 / 97 / 126). The helpers are fully inlined and carry no symbol,
  so the port's names for them are the port's. The asymmetry is measured, not
  assumed — line 45 appears in `mmanageReqItemMdl` and `mmanageClearItemMdl`
  and nowhere else in the object.
- **Every `IsReady` is a `switch` on the call's result with no local to hold
  it.** `functions.txt` lists only the three parameters and the `int` stab list
  is exhaustive, so there is no `ready` variable. `case WAIT_MEMORY` falls
  through into `default`, which is what gives both of them the single shared
  `return 0`; `case READY`'s `return 1` emits no code at all, because GCC knows
  `v0` already holds the 1 it just compared against.
- **The bases are 2015 / 303 / 382** — `I000_PLAY_CAMERA_PK2`, `CH000_MIO_MDL`,
  `CH000_MIO_ANM`, all confirmed in `cddat.h`.
- `ITEM_MODEL_PACK_ORDER` and `MMANAGE_ERR` are the ROM's own: `types.txt`
  carries them immediately after the `OL_LOAD` family, which is what places them
  in `mmanage.h`.

**Two general debug-info readings this object is the clearest example of:**

- **A `PROC` record's `SI(n)` is a symbol *index*, not a line number.**
  ~~They are `mmanage.h`'s prototype lines.~~ **Corrected by man_data.o —
  see its section below.** The eleven here read 9, 11, 13 … 29 in exactly
  `.text` order, which reads like a header layout but is a bare counter
  (1, 3, 5, 7 went to the four fixed_array statics first). It recovers
  nothing. The half of this that still holds: the opening brace is the `$LM`
  that sits immediately *before* the `PROC` record — the subtitle.o rule.
- **GCC's switch decision tree is emitted at the end of the switch and then
  moved to the front, so it inherits the switch head's line number.**
  `expand_end_case` finishes with an explicit `reorder_insns` of the dispatch
  code to before the case bodies. That is why the whole `li 3 / beq`, `slti 4`,
  `li 1 / beq` ladder here carries line 64 (the `switch (...)` line) and emits
  no `$LM` of its own — and it is what distinguishes a real `switch` from an
  if-chain, which would leave a note per test. See
  [[gcc-switch-decision-tree]] for the shape of the tree itself.

**Run in-game — this module has been live all along.** Unlike most of the tree
these eleven bodies were already reachable and already worked; `scene.c`,
`enemy.c`, `fly_ctrl.c`, `plyr_mdl.c`, `man_data.c` and `effect_butterfly.c` all
call them every room load. The reconstruction is a correctness and faithfulness
pass over an existing implementation, not new reach: the behaviour changes are
`StringBuf`'s scope, the loss of the invented `ready` local, and the
`WAIT_MEMORY`/`default` merge — none observable.

One residual, not touched: **`src/ingame/map/mmanage.h` is a stale hallucinated
duplicate** declaring `mmanageIsReadyMdl(void)` / `mmanageIsReadyItemMdl(void)` /
`mmanageClearMdl(void)` / `mmanageClearItemMdl(void)`. Nothing includes it and
`ZERO2.MAP` has no such object; anything that did include it would fail to
compile against the real header.

### man_data.o — done

All 9 `ZERO2.MAP` `.text` exports plus both `.gnu.linkonce.t` bodies, verified
9/9 against the map and 10/10 against `functions.txt` (the one extra entry is
the `MAN_DATA type_info function` node, a compiler artifact). `.text` is
accounted for byte-for-byte (0x1d85d0..0x1d8cdc = 0x70c: 0x6e8 of bodies plus
nine 4-byte alignment fills, so there is no unlisted body), and `aBoneLabelTbl`
is byte-identical to the ROM's `.data` — 0x40, diffed out of the compiled
`.obj`, which also emits exactly the two COMDAT bodies the ROM puts in
linkonce. It is `MAN_DATA`: the resource owner `PLYR_PLYR_DATA` (plyr_mdl.o)
and `SIS_DATA` (sis_mdl.o) both derive from, holding one model, one shadow
model, one animation pak, one sound bank and one accessory as a set.

**The earlier pass's version was a good sketch with four real errors**, which is
the standing lesson about existing code. In order of consequence:

- **`MAN_DATA::IsReady()` calls `InitIn()`** when `ReadyIn()` comes back
  non-zero. The stub was `return ReadyIn(...)`, so a plain MAN_DATA would have
  polled for ever and never built an ANI_CTRL. Only reachable through the vtable
  and both subclasses override it, so nothing in this build was broken — but the
  linkonce body at 0x2b7dc0 calls 0x1d8a98 *and* 0x1d89e8, plainly.
- **`AccessoryDraw`'s assert does not guard the draw.** The ROM falls straight
  through into `GetItemSgdAddr()` whether the model is resident or not; the
  earlier version added a `return`. It is a `G3DASSERT`, and the stringized
  condition in `.rodata` — `"(mmanageIsReadyItemMdl(mAcsNo, &mpAcsMdl))"` —
  carries the source's own extra parens **and proves `mmanageIsReadyItemMdl`'s
  third parameter is defaulted to 0** in the ROM's `mmanage.h`, since the
  emitted call passes `a2 = 0` against a two-argument spelling.
- **`ReleaseAnmIn` clears `man_ready_anm_init` (215) before nulling the two
  ANI_CTRL pointers (217)**, and **`ReleaseIn` clears `man_data_bank_no` (244)
  before `mBDNo` (245)** — both the reverse of what was there. The two null
  stores share line 217 and the two -1 stores share one materialised `li v1,-1`.
- **`ManItemSGDDraw` has three locals, not one** (`l_cp`, `hs`, `cp` —
  `functions.txt` lists all three), so lines 61/62/63 are three declarations
  with initialisers, not one nested expression.

Facts worth knowing before touching it:

- **`ReadyIn` uses `&=`, not `&&`.** `ret` starts at 1 and every piece ANDs its
  own answer in (`andi s1,v0,0x1` for the first, where GCC folded `1 & x`), so a
  loader that ever returned 2 would zero the whole set. A piece that was never
  requested (`number < 0`) sets `ret = 0` outright — except the accessory, which
  is optional and simply skips.
- **`SetupIn`'s sound-bank arm is the only one that does not touch `ret`.**
  Nothing downstream has to be rebuilt when only the voice bank changes; the
  other four arms all mean "your ANI_CTRLs are about to go".
- **The five accessors and both virtuals live in `man_data.h`.** None has a
  symbol anywhere in the build, and the two virtuals are in linkonce because
  they are inline *and* virtual. `plyr_mdl.o` and `sis_mdl.o` pin every line:
  `GetAniCtrl` 32 (x75), `GetShadowAniCtrl` 35 (x2, and its two expansions are
  `plyr_mdlGetShadowANI_CTRL`/`sis_mdlGetShadowANI_CTRL`), `DrawLock` 39 (x2,
  `PlayerDrawLock`/`SisterDrawLock`), `DrawUnlock` 43, `IsLocked` 46/48/49/54,
  `GetSndBankNo` 58 (x20). `GetAnmNo()` is declared by `types.txt` and expanded
  by nothing.
- **`IsLocked`'s warning is `PRINT_WARNING`, and its own `__LINE__` confirms the
  tally**: the expansion passes `a2 = 0x31` = 49, which is exactly the line the
  SOL/`$LM` sweep independently assigns it. Message
  `"man_data_draw_lock_cnt < 0"`, banner file `"man_data.h"`.
- **`aBoneLabelTbl[16]`** is `{17, 6 x5, 2 x10}` — accessory 0 (the camera)
  rides bone 17, 1..5 ride bone 6, everything from 6 up rides bone 2.
- **The object has no static data beyond that table.** Its `.rodata` (0xca) is
  the `fixed_array` literal, the four G3DASSERT strings and `"unsigned int*"`;
  its `.sdata` (0x40) is the boilerplate. Same signature as `subtitle.o` and
  `title.o`.
- **`man_ready_req_mdl`/`_smdl`/`_anm`/`_bd` (bits 1..4) are never read or
  written anywhere in the object.** `InitializeIn` clears only bits 0 and 5, in
  one load/and/and/store.

**The debug-info correction this module forces** is above, in mmanage.o's
section: `SI(n)` on a `PROC` record is a symbol index, not a declaration line.
man_data.o proves it three ways — three instantiations of one template read 3,
5, 7; the counter runs 9..25 through man_data.c and continues 27, 29, 31 into
the header's linkonce bodies; and `MAN_DATA::Setup`'s `PROC` says 29 while its
own `$LM`s measure its body at man_data.h **18-20**. `mmanage.h`'s banner is
corrected at the site and [[proc-stab-line-is-the-declaration-line]] rewritten.
A header's real line map comes from tallying `SOL <header>` + `$LM` pairs across
every object that expands its inlines, which is what placed all seven members
here.

Two smaller readings worth carrying:

- **An insn GCC moves into a branch delay slot loses its line note.** That is
  what makes every `ret = 1;` in `SetupIn`, the `int ret = 0;` initialiser and
  `mAcsNo = iAcsNo;` unmarked, and their positions then fall out of the gaps
  they exactly fill. It also settles block 4: `ret = 1;` sits at 119, right
  after `ReleaseAnmIn()`, because `mmanageReqAnm`'s own delay slot was already
  taken by its argument — had `ret = 1` come after that call it would have been
  emitted plainly and carried a note.
- **`bltzl` with a store in the annulled delay slot duplicates the store.**
  `SetupIn`'s model arms each have two copies of `mMdlNo = mdl_no`; only the
  one at the merge point is the source's statement.

**Run in-game — this module has been live all along**, like `mmanage.o` below
it. `plyr_mdl.c` and `sis_mdl.c` drive every one of these bodies on each room
load, so this is a correctness pass rather than new reach; the only observable
change is `AccessoryDraw` no longer returning early on a failed assert, which
by construction cannot happen (`ReadyIn` folds the same test into the answer
`IsReady()` gives).
