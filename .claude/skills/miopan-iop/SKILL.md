---
name: miopan-iop
description: iopsys.irx — the IOP-side sound and file server of Fatal Frame II (Crimson Butterfly) PS2 prototype. Covers both halves of the job the reconstruction (complete, in iop/src/system/iop/) and the live port of it into MioPan (zero_rom/zero2np/src/system/iop/ plus the IOP SDK shims in src/sdk/). Includes the Ghidra bridge trap, IRX import resolution, the tooling in iop/tools/, the 64-bit pointer-truncation hazards that dominate the porting work, the EE<->IOP RPC bridge, the SPU2 voice engine and mixer that make it audible, the reverb unit, and how to read an audio fault back to its cause. Use whenever work touches iop/, iopsys.irx, an IOP-side function (iopCommand, StreamStart, iopSndMain, PCMStreamReadThread, thRingBufRead, ...), the IOP SDK shims (iop_host, iop_sif, iop_libsd, iop_voice, iop_reverb, thbase.h, thsemap.h, libsd.h, ...), or the EE<->IOP protocol. Sister skill to miopan-reversing, which covers the EE side.
---

# Reconstructing iopsys.irx

`iop/iopsys.irx` is the **IOP half** of Fatal Frame II's sound and file system —
the far end of everything under `zero_rom/zero2np/src/system/eeiop/`. The EE
builds command packets and queues them; this module executes them. Both halves
are being reconstructed, and cross-checking one against the other is the single
most productive technique here (it is how the `REQ_STREAM_ABORT` bug below was
found).

Read `miopan-reversing` for the EE side, the repo build, and the general
methodology. This skill covers only what is different about the IOP.

## Layout

| Path | What |
|---|---|
| `iop/iopsys.irx` | the module: relocatable MIPS ELF, 282 KB |
| `iop/iopsys_files.txt` | source file → `.text` start offset |
| `iop/iopsys_functions.c` | every function's signature **and locals** (stdump) |
| `iop/iopsys_globals.c` | statics per source file, with `bss`/`data` offsets |
| `iop/iopsys_types.h` | every struct/enum, with offsets and sizes |
| `iop/src/system/iop/` | **the reconstruction** — mirrors the ROM's paths |
| `iop/tools/irx.py` | ELF reader + capstone disassembler (see below) |
| `iop/tools/iopsdk/` | cut-down IOP SDK headers, for syntax checking only |
| `iop/tools/check.sh` | syntax-checks every reconstructed `.c` |
| `iop/tools/verify.py` | five whole-module checks against the ELF (see below) |
| `zero_rom/zero2np/src/system/iop/` | **the port** — a copy of the reconstruction, in MioPan's build |
| `zero_rom/zero2np/src/sdk/iop_host.{h,cpp}` | IOP kernel on SDL: threads, semaphores, timers, memory |
| `zero_rom/zero2np/src/sdk/iop_sif.cpp` | SIF RPC bridge + DMA, and the IOP boot hook |
| `zero_rom/zero2np/src/sdk/iop_libsd.cpp` | SPU2 register model + 2 MB SPU RAM; forwards to the voice engine |
| `zero_rom/zero2np/src/sdk/iop_voice.{h,cpp}` | the synthesiser and mixer: ADPCM decode, ADSR, resampling, one output stream |
| `zero_rom/zero2np/src/sdk/iop_reverb.{h,cpp}` | the SPU2 effect unit: preset tables and the comb/all-pass network |
| `zero_rom/zero2np/src/sdk/{thbase,thsemap,intrman,sysmem,sysclib,timrman,loadcore,ioman,sifcmd,sifman,libsd}.h` | SDK-named wrappers the reconstruction includes |

**Two copies now exist and they diverge on purpose.** `iop/src/` is the
reference reconstruction, kept faithful and verified by `verify.py`.
`zero_rom/.../system/iop/` is the port and carries the changes the host needs.
Diff them before assuming a fix is in both; port-only edits are all marked
`PORT:` in a comment.

`iop/` sits **outside** the CMake glob (`zero_rom/zero2np/src/**`) on purpose:
this is IRX code and must never enter the MioPan EE build. Verify that still
holds if you add directories.

## The Ghidra bridge serves ONE program at a time

Port 8089, same as the EE work. **It is bound to whichever program is active,
and there is no endpoint to switch it.** Always confirm before trusting output:

```bash
curl -s "http://127.0.0.1:8089/decompile_function?address=0x00000738"
```

`0x738` is the IRX's `start()`. If that returns `No function found`, the bridge
is on the EE binary — ask the user to make `iopsys.irx` the active program.
Cross-check the other way with `0x00274f28` (the EE's `sndGetNeedSize`).

Useful endpoints beyond the ones in `miopan-reversing`: **`list_functions`**
returns every symbol with its address, and for this module it also names every
IRX import stub — which is the authority for library calls (see below).

## Import resolution — do not trust published export lists

The module has no undefined symbols. Library calls go through **IRX import
tables** at the end of `.text`: magic `0x41E00000`, version, `char name[8]`,
then 8-byte `{ jr ra ; li v0, funcid }` stubs. 13 libraries, 73 imports.

**Export indices are version-specific and PS2SDK's published lists are wrong
here past about index 16.** Four were wrong on the first pass:

| Stub | Naive guess | Actually |
|---|---|---|
| `sifcmd` 8 | isceSifSendCmd | **sceSifSetCmdBuffer** |
| `sifcmd` 12 | sceSifSetSreg | **sceSifSendCmd** |
| `thbase` 22 / 24 / 25 / 26 | SleepThread / iWakeup / CancelWakeup / iCancelWakeup | **ReferThreadStatus / SleepThread / WakeupThread / iWakeupThread** |

Two more that are non-obvious but were derived correctly: `sifman` 32 is
**`sceSifSetDmaIntr`** (four arguments, not `sceSifSetDma` — the relocation
hands it `_intr_SifSetDma` at address 0), and `ioman` 8 is **`lseek`**, used by
`iopCommandQuery` as `lseek(fd, 0, 2)` for a file size.

`iop/tools/irx.py` now carries a **verified address→name map** taken from
Ghidra's IRX loader, which overrides its index table. Trust that map. If you
meet a new unresolved stub, resolve it from the **call site**, never from
memory — the strongest evidence is the module's own assert text (that is how
`timrman` 4/20/22/23 were pinned to `AllocHardTimer` / `SetTimerHandler` /
`SetupHardTimer` / `StartHardTimer`, which do not match PS2SDK either).

## Tooling

```bash
cd iop/tools
python irx.py libs                # every import stub, resolved
python irx.py funcs               # 134 FUNC symbols: address, size, name
python irx.py objs                # 100 OBJECT symbols by section
python irx.py d <name|0xaddr> ...  # capstone disassembly, calls and strings named
python irx.py strs 0x6d30 200     # .rodata strings
```

`irx.py` also exposes `rd()`, `cstr()`, `f32()`, `SYMS`, `FUNCS`, `IMPORTS`,
`RELTEXT` for one-off scripts. It resolves `jal` targets, `lui`/`addiu` pairs
into `.data`/`.bss` symbol names, and `.rodata` string literals inline — which
is often faster than reading Ghidra's decompile for a specific detail.

Syntax check after every file:

```bash
cd iop && sh tools/check.sh
```

Only `error` lines matter. Int↔pointer cast warnings are expected and faithful:
the IOP is 32-bit and the ROM stores addresses in `int` fields (`rb_top`,
`ring_buf_top`, `adrs`). Do **not** widen those to fix warnings.

## No source line numbers

Ghidra reports **no `; Line NNN` for this module**, unlike the EE binary. IOP
files therefore carry **no trailing ROM-line annotations** — say so once in each
file header rather than inventing them. `iopsys_functions.c`'s locals list is
the substitute: it tells you how many locals the original had, their types and
their registers, which constrains the reconstruction the same way.

## File conventions

Mirror the ROM's tree: `iop/src/system/iop/<name>.c` plus a `<name>.h` per TU.
Shared records live in:

- **`iop_types.h`** — `IOP_RET_STATUS`, `IOP_STREAM_RET`, `EEIOP_STREAM_STATUS`,
  `LOAD_DEF_STRUCT`, `IOP_READ_WRK`, `RING_BUF_WRK`, `LOAD_IOP_WRK`, `STM_IOP_WRK`
- **`iop_snd_def.h`** — `IOP_COMMAND_ENUM` and every command payload; the mirror
  of the EE's `system/eeiop/snd_def.h`, and the two must agree byte for byte

The ROM's own header names are not recoverable — the debug info gives types,
globals and signatures but no per-header attribution, and nothing in this module
is an inline. Say that in `iop_types.h` rather than guessing names.

Header style, comment density and the "explain *why*" rule are the same as the
EE side; match the neighbouring files.

## Status

**COMPLETE — 9 of 9 TUs, 134 of 134 functions.** All syntax-check clean.

| TU | `.text` | Functions |
|---|---|---:|
| `iop.c` | 0x0000 | 27 |
| `iop_load.c` | 0x10c0 | 6 |
| `iop_load_stream.c` | 0x17a0 | 6 |
| `iop_sb.c` | 0x1a00 | 6 |
| `iop_stream.c` | 0x1c20 | 19 |
| `iop_snd.c` | 0x3b20 | 30 |
| `utility2i.c` | 0x4fa0 | 21 |
| `iop_pcmstream.c` | 0x55e0 | 14 |
| `iop_ring_buf.c` | 0x6420 | 5 |

**The live work is now the port, not the reconstruction — see Part 2.** What
follows in this section is about keeping `iop/src/` faithful; the port copy
under `zero_rom/` is allowed to diverge and does.

Work on the reconstruction is refinement, not coverage: tighten comments and confirm the
guesses flagged in each file. There are no known gaps — the five checks under
"Whole-module verification" below all pass, and nothing warns under `-Wall`
beyond the documented int↔pointer, `%x`-on-pointer and `char`-subscript
classes. `tools/verify.py` runs all five.

`MyOpen` / `MyClose` / `MyPcRead` are **empty stubs** — the host-PC read path is
compiled out, exactly as on the EE side. `MyOpen()` answering 0 is why
`iopCommandQuery()`'s `REQ_FILE_SIZE` always `lseek`s fd 0.

`iop_snd_def.h` carries `IOP_COMMAND_QUERY_ENUM` and every payload struct; the
EE's `ee_iop_q.c` is still a stub, so the query enum's four values are named
from the IOP's own debug info and match the EE's function names one for one
(`QueryFileSize` → `REQ_FILE_SIZE`, and so on).

### Verify layouts by `sizeof` / `offsetof`

Every `iopCommand()` case ends in a literal `addiu $s0, $s0, N`, and
`iopsys_types.h` gives an offset for every struct member. Building the
reconstruction's headers into a throwaway `sizeof`/`offsetof` program and
diffing against both checks the whole protocol in one pass — it is what proved
the `REQ_STREAM_ABORT` hole, and it confirmed `STREAM_WRK`'s 26 members and
0x15c size exactly. Structs holding pointers legitimately disagree on a 64-bit
host (`IOP_SND_INIT` 0xc, `IOP_READ_WRK` 0x10); everything else must match.

### Prefer the ROM's own enums

`iopsys_types.h` carries enums the ROM actually used, and it is easy to invent
`#define`s for values they already name. **Grep it for an enum before writing a
constant.** The ones that matter: `THREAD_PRIORITY_ENUM` (`PRI_MAIN`,
`PRI_IOP_READ`, `PRI_STREAM_VOICE`, … — every thread priority in the module),
`enum_SPU_CORE` (`SPU_CORE_1/2/NUM`, and the ROM's own printf text in
`iopSndInit()` confirms those names), `PAUSE_PHASE`, `enumSPU_LOOP`,
`VOICE_TYPE_ENUM`, `FADE_MODE`, `IOP_COMMAND_QUERY_ENUM`. Shared ones live in
`iop_types.h`; module-local ones in that module's header.

## Idioms worth recognising

- **Thread arguments travel in `ThreadParam.option`.** IOP threads take no
  argument, so `InitRingBufSub()`, `PCMStreamCreate()` and friends put the work
  pointer in `option` and the thread recovers it with
  `ReferThreadStatus(0, &info)` → `info.option`. That is why those thread
  bodies have an otherwise unmotivated `ThreadInfo` local.
- **Two argument conventions in `iop_snd.c`.** `MyOnVoice`, `MyOffVoice` and the
  `VolSet` family take `core` and `voice_no` separately; `PitchSet()` and
  `AdsrSet()` take a single packed key `(voice_no << 1) | core`, called `set` or
  `voice_shift` in the ROM's locals. The `& 0x7fff` you see on the shift is a
  u16 truncation, not a mask on the voice number.
- **`sceSdBlockTrans` is called with both four and five arguments.** libsd's
  prototype takes five; `start_addr` is only read for mode `0x13`. Pass 0 at the
  four-argument sites so the call is well-formed C, and note it.
- **Interrupt-paced loops.** `sceSdSetTransIntrHandler()` points a DMA
  completion at `_intr_SignalSema`, which signals a semaphore the reader thread
  waits on. One refill per interrupt; the ring cannot outrun the hardware.
- **Cooperative cancellation.** `load_clear_flg` / `load_end_flg` / `stop` are
  checked *after* the blocking wait, so a request that lands while a thread is
  parked is still seen. `iop_stream.c` takes it furthest: both threads raise
  their own `read_end` / `voice_end` bit on the way out and whichever finishes
  second calls `StreamReleaseSub()`, so the ring buffer is freed exactly once.
- **Poll a hardware register until two reads agree.** `SD_VA_NAX` advances
  while it is being read, so `IsInStopBlock()` and `MyPauseVoice()` both do
  `iAdrs = -1; while ((iTmpAdrs = sceSdGetAddr(...)) != iAdrs) iAdrs = iTmpAdrs;`
  A single `sceSdGetAddr` on a play address is always a bug.
- **The SPU's own IRQ is the stream clock.** `StreamVoiceThread()` points
  `SD_A_IRQA` at the buffer it is about to refill, enables `SD_C_IRQ_ENABLE`,
  and sleeps; `_intr_SignalSemaSPUAdrs()` signals the slot semaphore when
  playback crosses it. Finding the semaphore *already* signalled means the
  refill missed its window — that is the "Trans Is Not in Time" banner.
- **Deferred key on/off.** Nothing keys a voice directly: callers set a bit in
  `key_on_voices[]` / `key_off_voices[]` and `FrameWrkVoices()` flushes both
  once per `iopCommand()`. That is why `KeyOnVoices()`/`KeyOffVoices()` cancel
  each other — an off-then-on inside one frame never reaches the SPU.

## Hazards

- **Ghidra misreports call arity here too.** `sceSifSendCmd` decompiles with
  eight arguments in `thTransMemDecode` — it really takes six, and the two extra
  are `CMP_HEADER` fields the compiler kept live in registers across the call.
  Check the disassembly whenever an SDK call's arity looks wrong.
- **Large structs are passed by value via an invisible reference.** The callee
  copies them onto its own frame from the pointer in `a0`; `iopsys_functions.c`
  reports them as by-value parameters. Reconstruct them as by-value.
- **A `*/` inside a block comment ends it.** Writing `MyOn*/MyOff` or
  `/* NNN */` inside a file header comment breaks the file. This has bitten
  twice — reword instead.
- **A jump-table slot pointing at the loop top is a *missing* case, not an empty
  one.** With no `default:`, GCC gives every hole in the case range the same
  label as `break`, and sends the out-of-range branch there too. So in
  `iopCommand()` the tell that command 14 has no case is not the table slot
  alone but the out-of-range branch (`sltiu $v1, 0x22`; `beqz → loop top`)
  landing on the *same* address. The real `default:` body, if there is one, is
  whatever the slot for a value the loop never reaches points at — here index 0,
  which holds an unreachable `printf("iopCommand() illegal\n")`.
- **Peeled loop iterations look like duplicated code.** `PCMStreamReadThread`
  has a genuine prime read before its loop, and a stop-printf block sitting
  *inside* the prologue's address range that reads like a loop latch. Map the
  real back-edge in raw MIPS before concluding anything about loop structure.

## ROM bugs found so far

- **`REQ_STREAM_ABORT` desyncs the command queue.** `iopCommand()` advances past
  the command word *before* the switch, so each case must skip its own payload.
  The jump table gives commands 2, 14 and 19 the loop-top address, which is the
  switch's `break` target — the switch has **no case for them at all** (see the
  hazard below). Command 14 (`REQ_STREAM_ABORT`) carries a 4-byte
  `STREAM_ABORT { int wrk_id; }` that is therefore never skipped, so `wrk_id` is
  read as the next command: 0 reads as `IOP_COM_END` and drops the rest of the
  frame's queue, 1 reads as `REQ_IOP_REBOOT`. The EE really sends it, from
  `SndStreamMain()`'s `WAIT_END` arm after 600 frames. `StreamAbort()` exists in
  `iop_stream.c` and has **zero references anywhere in the module** — not a
  `jal`, not a data word — so the case was simply never wired up. Commands 2 and
  19 are harmless: the EE never sends either and neither has a payload.
  Cross-noted at the EE send site in `eeiop/snd_stream.c`.
  **FIXED IN THE PORT (not in `iop/src/`).** This is the ROM's *only* recovery
  when a slot sticks in `ST_STREAM_WAIT_END`, and it fires for real: the EE arm
  sends it after 600 frames, the IOP ignores it, and the slot is then held for
  good -- the next stream spins on `StreamAutoIsPreload() Wait Other Stream End`
  and the scene never loads.  Confirmed live by the stall dump:
  `slot0 ... IsUse=1 status=ST_STREAM_WAIT_END` beside a free-but-disabled
  slot1.  `zero_rom/.../iop.c` now has the `case REQ_STREAM_ABORT:` calling the
  already-written `StreamAbort()` and skipping `sizeof(STREAM_ABORT)`; the
  tracer table in `iop_sif.cpp` was updated to match.  Caveat: `StreamAbort()`
  expects `TerminateThread()` to kill the stream threads, and the host shim
  cannot -- it only signals their wake semaphore, so a thread parked somewhere
  that never tests `stop` stays parked while `StreamReleaseSub()` frees the ring
  under it.  Treat it as a rescue, not a reason to stop chasing why the release
  was missed.
- **`StreamStart()` reads one past `spu_loop_packet[]`.** The `just_loop`
  decision is `if (stp->spu_loop_packet[i] == 0 || (both fractions are 0))`,
  but `i` is left at `stp->nchannel` by the copy loop just above it. For the
  usual two channels that reads `spu_packet[0][0]`, which is never zero, so the
  "no loop packet was reserved, treat this as a whole-packet loop" guard can
  never fire — a fractional loop with no loop packet then reaches
  `PreloadLoopPacketSub()` with a null destination. Almost certainly meant
  `[0]`. Confirmed in raw MIPS: `sll $v0, $s3, 2` at 0x2254 reuses the loop's
  own induction variable.
- **`PCMStreamReadThread`'s prime read does not advance `offset_sector`,** so the
  loop's first read fetches the same sectors again into the slots the prime read
  moved past.
- **`PCMStreamMain()` is never called, so PCM pause does not work.** Address
  0x61b8 appears **nowhere** in the module — no `jal`, no `lui`/`addiu` pair, no
  data word. It is the PCM counterpart of `iopSndMain()`, which `iopSndInit()`
  does start as a thread; this one was never wired up. It is also the only
  reader of `pause_phase`, so `REQ_PCM_STREAMPAUSE` / `REQ_PCM_STREAMRESTART`
  set a field nothing acts on and **pause and restart do nothing at all**.
  Stopping still works via `PCMStreamStop()`'s separate `stop` flag, but it asks
  for a fade first that never runs, so a PCM stream cuts off at full volume.
  Takes `PCMStreamPauseSet()` and `iopPauseSubB()` down with it — they have no
  other caller.
- **`GetSPUMemory`-style asymmetry in `iopPauseSubB`:** +300 per frame up, −800
  down, so a PCM pause fades out nearly three times faster than it fades back in.
  Deliberate, not a bug — and `iop_snd.c`'s `iopPauseSub()` is the same design
  at exactly half scale (+150 / −400), so the ratio is a house rule. Note it
  never actually executes, per the entry above.

Reproduce all of these as found and comment them; do not repair one side in
isolation.

## Dead and empty code

**23 of the 134 functions are unreachable** — no `jal` and no address reference
anywhere in the image. Most are `utility2i.c`, which is the IOP's copy of the
EE's whole `common/utility2.c` and is expected to carry unused helpers. The
module-specific ones are listed below; reproduce them, because a dead function
is usually evidence about the ROM's history.

Prove reachability by scanning for **both** the `jal` encoding *and* the
function's address as a 16-bit `lui`/`addiu` operand or a raw data word — thread
entries, interrupt handlers and RPC handlers are never `jal`ed. Ghidra xrefs are
not a substitute.

| Symbol | State |
|---|---|
| `PCMStreamMain` | unreachable — and takes PCM pause with it, see the bug list |
| `PCMStreamPauseSet`, `iopPauseSubB` | reachable only from `PCMStreamMain` |
| `SetSPU_PCMZeroBlock` | unreachable; `PCMStreamReadThread()` open-codes it instead, and its printfs still name it |
| `StreamAbort` | unreachable — `iopCommand()` never dispatches to it |
| `IsValidVoice` | unreachable; its two-line body is written out by hand at all five sites that need it |
| `MyOnVoiceSub` | unreachable |
| `iopSndVoiceStop` | unreachable, and empty anyway |
| `ReferSemaNowCount`, `MyCdSeek` | unreachable |
| `SetPrintAssert` / `SetPrintWarning` | unreachable, so `print_assert_func` stays NULL and all six `PrintAssertReal()` sites end in its `for(;;)` — an IOP assert is a hard hang |
| `StreamRelease` | empty — `REQ_STREAM_RELEASE` does nothing; slots are freed by the threads |
| `SetAdrsStopBlock` | empty, but called from `StreamReleaseSub()` |
| `MyOpen` / `MyClose` / `MyPcRead` | empty — host-PC path compiled out |
| `STREAM_WRK::ready`, `::pre_load` | declared, never written; `StreamPlay()`'s "Stream Not Ready" therefore never prints |
| `iopCommand()`'s `case IOP_COM_END` | unreachable `printf("iopCommand() illegal\n")` |
| loop at the top of `StreamVoiceThread()`'s main loop | `for (i = 0; i < nchannel; i++) ;` — body gone, count still emitted |

## Part 2 — the port into MioPan

The reconstruction is finished; the live work is making it *run*. It does: the
IOP boots inside MioPan, all four RPC services come up, the EE drives them, and
the game gets further with the IOP on than off.

### Building and running

```bash
export PATH="/path/to/mingw/bin:$PATH"   # the bin/ next to CMAKE_CXX_COMPILER
ninja -C "$PWD/cmake-build-relwithdebinfo"
```

Read the real toolchain paths out of `CMakeCache.txt` (`CMAKE_C_COMPILER`,
`CMAKE_MAKE_PROGRAM`).

Three traps, each of which cost real time:

- **The compiler needs its own `bin` on `PATH`** (for its runtime DLLs). Without
  it every compile fails with **exit 1 and no diagnostic at all** — including
  CMake's own compiler test, which then reports a broken toolchain. If a build
  fails silently, this is why. It is not a missing object directory.
- **Use absolute paths with `ninja -C`.** The Bash tool's cwd persists between
  calls and drifts; a bare `rm -rf CMakeFiles` from a drifted cwd deleted the
  build directory's `CMakeFiles/` once. Recoverable with `cmake -S . -B <dir>`.
- **stdout is block-buffered**, so a crash or a `timeout` kill loses the tail.
  Under `timeout`, the last few KB of log may simply not be there.

The exact toolchain paths are whatever `CMAKE_C_COMPILER` / `CMAKE_MAKE_PROGRAM`
say in the build directory's `CMakeCache.txt` — read them from there rather than
trusting this file, they have moved once already.

Debugger: any MinGW-compatible `gdb` (CLion bundles one under `bin/gdb/win/x64/bin/`).

```bash
"$GDB" -batch -ex "set pagination off" -ex "run" -ex "bt 20" ./cmake-build-relwithdebinfo/MioPan/MioPan.exe
```

### Runtime switches

| Env | Effect |
|---|---|
| `MIOPAN_IOP=0` | never boot the IOP; every `sceSifCallRpc` falls through. The A/B for "is this an IOP regression?" |

### 64-bit pointer truncation is THE porting hazard

The ROM keeps addresses in `int` / `unsigned int` because an IOP pointer was 32
bits. On this host that silently produces unmapped pointers, and it is the cause
of essentially every crash so far. Symptoms are wild: a `memset`/`strncpy` fault
in unrelated-looking code, far from the truncation.

Already fixed (all marked `PORT:` in the port copy):

- `GetAlignUp((unsigned int)buf, 4)` — 8 sites in `iop.c` / `iop_load.c` /
  `iop_load_stream.c`; `GetAlignUp` itself widened to `uintptr_t`.
- `(int)AllocSysMemory(...)` — `ring_buf_top`, `rb_top` × 2, `zero_buf`.
- `RING_BUF_WRK::ring_buf_top`, `::now_adrs`; `LOAD_IOP_WRK::adrs`,
  `::tmp_ee_adrs`; `LOAD_REQ_NEW::adrs`, `::tmp_ee_adrs`; `STREAM_WRK::rb_top`;
  `PCM_STREAM_WRK::rb_top`; `MyTransEEWait`'s two address parameters.
- **`ThreadParam::option`** — three sites (`iop_stream.c`, `iop_ring_buf.c`,
  `iop_pcmstream.c`) pass a work *pointer* through it, and the field itself was
  `u_int` on both `IopThreadParam` and `ThreadInfo`. Every stream and
  ring-buffer thread was starting on a truncated `stp`. Fixing this took the
  game from stopping at the title screen to running into `STORY_MAIN`.

The EE side was already widened to `intptr_t` (see `system/eeiop/fileload.h`) —
**match it, do not invent a different width.** Anywhere the two halves share a
struct they must agree exactly; the RPC bridge memcpys between them.

Two allocators keep this survivable:

- IOP memory comes from a **4 MB reservation below 4 GB** (`VirtualAlloc` walking
  up from 16 MB) so `(int)` round-trips losslessly and the ROM's `int` address
  fields stay faithful. `<windows.h>` cannot be included there — it collides with
  `eekernel.h` and `scetypes.h` — so `VirtualAlloc` is declared by hand.
- SPU addresses are **not host pointers**. They are offsets into
  `iop_libsd.cpp`'s 2 MB `sd_ram`; go through `MioPan_SpuRamPointer()`.

### The SDK shim design

Modelled on [MikuPan](https://github.com/Mikompilation/MikuPan) (see `sdk/` and
`src/iop/`) — read it before designing anything new here.

IOP threads become real SDL threads, semaphores SDL semaphores, the hard timer
its own thread. Every entry point is `MioPan_Iop*` with a compat macro carrying
the ROM's spelling, because **`CreateThread`, `ExitThread` and `TerminateThread`
are Win32 API functions** and `ThreadParam` / `SemaParam` collide with the EE
kernel shim (hence `IopThreadParam` / `IopSemaParam`, macroed back).

**Only one IOP context runs at a time — there is a big kernel lock.** The IOP
was one CPU, so every critical section in `iopsys.irx` is written against
"no other thread can be running", which is why the ROM's own protection is
`CpuSuspendIntr()` and nothing else. Real host threads plus `iopCommand()`
running directly on the EE's thread destroyed that guarantee and raced the
ROM's records — `STREAM_WRK` written by `iopCommand()` while
`StreamVoiceThread()` reads it, `IOP_RET_STATUS` written by
`StreamVoiceThread()` while `ee_iopMain()` copies the whole struct out.
`MioPan_IopBkl{Enter,Leave,Suspend,Resume}` in `iop_host.cpp` put it back:

- **Held by** `IopThreadMain` (the whole thread body), `MioPan_IopBoot`
  (`start()`), and `MioPan_IopRpcDispatch` (the handler, on the EE's thread).
- **Dropped inside** `WaitSema`, `SleepThread`, `DelayThread`,
  `StartThread`'s grace spin and join, and `sceCdRead`'s host read — every
  point where the IOP was parked anyway. It is a *recursive* lock with a
  per-thread depth counter, because a blocking call has to give back every
  level it holds and retake the same depth after.
- **Not held by the voice decode thread.** That models the SPU2, which really
  did run alongside the IOP. It also calls the ROM's SPU interrupt handlers
  while holding `voice_mutex`, which an IOP context takes *under* the BKL —
  taking the BKL there inverts the order and deadlocks. Still open: those
  handlers' one register write into `iop_libsd.cpp`'s unlocked model. Fixing it
  needs a lock design for libsd, which is entangled with the voice engine
  (`sceSdSetSwitch` → `MioPan_VoiceKeyOn` → `voice_mutex`) and is its own job.
- The 2 kHz sound timer handler is likewise unlocked: it only reads an `int` and
  signals a wake semaphore, both already atomic, and locking at that rate would
  cost more than it buys.

**Five of the ten thread families are gone.** The four RPC service loops
(`iopRpcLoop`, `iopRpcQueryLoop`, `iopFileLoadLoop`, `iopFileLoadStmLoop`) never
were threads here — `sceSifRpcLoop()` returns immediately, so they did their
setup and died. Their creators call them inline instead, which also closes the
"registered but not yet ready" window: everything is in place before `start()`
returns. And `iopRead` is no longer started at all — see below.

Not modelled: **thread priority**. The ROM leans on it (`PRI_STREAM_VOICE` 11
must outrun `PRI_MAIN` 19). It is passed to SDL as a hint only — suspect this
first if audio stutters once there is audio.

**A thread that has signalled completion is not yet dormant.** The one place
the unmodelled priority actually breaks correctness, not just smoothness.
`iopRead()` ends with `SignalSema(read_end_sema); SignalSema(sema_CD);
ExitThread();` — on the IOP it runs at `PRI_IOP_READ`, above its waiters, so it
always reached `ExitThread()` (dormant) before anyone could relaunch it. With
priority as a hint, the waiter can get to `iopReqRead()`'s
`StartThread(iop_read_th_idx, 0)` inside that window, be told "already
running", and then block on `read_end_sema` for ever — **`iopReqRead()` ignores
the return value**. The victim is a stream read thread parked in `iopReqRead()`,
where `stp->stop` is never tested, so `StreamStop()`'s `WakeupThread` cannot
reach it, `ReadThreadExit()` never runs, and neither thread calls
`StreamReleaseSub()` — the play slot is held for good and the next stream spins
on `StreamAutoIsPreload() Wait Other Stream End`.

**That one is now designed out rather than narrowed.** `iopReqRead()` performs
the read on the calling thread (`iopReadOnce()`, a `PORT:` split of `iopRead()`
without its `ExitThread()`) instead of starting a thread: `sceCdRead()` is a
synchronous host read that fires `cdvd_callback()` — and so signals
`sema_Sync_CD` — *before it returns*, so `MyCdRead()`'s wait is already
satisfied on arrival and the reader thread never actually blocked. The
semaphore accounting is unchanged. The `ExitThread()` must not come along:
on this host it marks the **calling** thread exited, and the caller is a live
stream or ring-buffer thread. `sceCdRead()` drops the BKL around the host read,
since that is where the IOP used to be parked.

`StartThread()` still gives a busy-looking thread a 10 ms grace period before
answering "already running" — the ring-buffer reader and the two stream threads
are relaunched the same way.

**Reading the release log:** `VoiceThreadExit()` and `ReadThreadExit()` each
raise their own bit and whichever finishes *second* calls `StreamReleaseSub()`.
So a `PRESTREAM VOICE END RELEASE N` with no matching
`PRE STREAM READ END RELEASE N` means that slot's **read** thread is wedged and
the release will never happen — which is the stall above, not an audio fault.

**Never block while holding `iop_table_lock`.** It is one global mutex over the
thread, semaphore, timer and memory tables, so anything that blocks under it
stops every IOP thread — they all queue up in `IopLock()`, which is where a hung
process will be sitting. The trap is `StartThread()` relaunching a finished
thread: `ExitThread()` sets `exited` from *inside* the thread body, so a thread
can be flagged exited while still unwinding, and an IOP thread's unwind runs the
ROM's release path (`StreamReleaseSub` → `FreeSysMemory`, `DeleteSema`) which
takes that same lock. Joining it under the lock deadlocks the pair. Capture the
stale `SDL_Thread *`, drop the lock, join, then retake it — with a `starting`
flag holding the slot so a second `StartThread()` cannot spawn a duplicate in
the unlocked window. Symptom: hang at the end of a scene, where one stream's
threads exit exactly as the next stream starts one.

### The RPC bridge

One process, so `sceSifCallRpc` is a direct call on the caller's thread:
`sceSifRegisterRpc` records (rpc number → handler) and `MioPan_IopRpcDispatch`
looks it up. DMA is memcpy. Two details that are not optional:

- **`sceSifRpcLoop()`, not `sceSifRegisterRpc()`, marks a service usable.**
  `iopRpcLoop()` registers RPC 1 and only *afterwards* waits for the disc and
  sets `pIopRet16`; a request accepted in that window reaches `iopCommand()`
  with a null status block. Calling the four loops inline rather than as
  threads means that window no longer exists — but keep the distinction, it is
  what makes `ready` correct.
- `sceSifBindRpc` leaves `serve` null until the service is up, so the ROM's own
  `do { bind; spin } while (serve == NULL)` retry loops handle the start-up race
  exactly as they did against hardware.

`ee_iop.c`'s IRX loader is compiled out (`ee_iop_boot_iop` is 0), so the first
`sceSifBindRpc` calls `MioPan_IopBoot()` → `start()`.

### One process, two copies of utility2

The ROM compiled `common/utility2.c` into both CPUs, so the port has ten
duplicate symbols (`GetAlignUp`, `PrintAssertReal`, `StrToLower`, ...). They are
renamed `Iop*` by a `#define` block at the top of the port's `utility2i.h` —
nothing in either `.c` changed, and deleting the block restores the ROM's
spelling. The bodies were reconstructed independently and differ only in style
(`tolower()` vs `isupper() + ' '`, named constants vs literals, a defensive
`& 0x1f` on a shift), so collapsing onto one copy later is safe — it needs the
eleven helpers only the IOP copy has (`RingBufAdd`, `GetStrLen`,
`GetOffsetChar`, ...) reconstructed on the EE side first.

### File loading stays on the fast native path — deliberately

`rpcFileLoadReqSub` calls `MioPan_FileLoadServe` directly on `thFileLoad`; it
never calls `sceSifCallRpc`. RPC 3 (`iopCommandLoad`) *is* registered and
reachable, so it is easy to "helpfully" reconnect — **do not.** Routing loads
back through the IOP means the ROM's ring buffer, its per-frame command queue
and a thread hand-off per block. There is a `KEEP IT THIS WAY` note at the call
site. The IOP is wired for sound, not file I/O.

One thing the port had to teach that loader: `FILE_LOAD_TYPE_SPU` loads (sound
bank `.bd` bodies) have an **SPU** destination, so `MioPan_FileLoadReq` carries
`is_spu` and resolves through `MioPan_SpuRamPointer()`. Without it the loader
memsets address 0x5050.

### Where it actually gets to

Sound works. SE, BGM, event voice lines and scene ADPCM all play; streams start,
refill, release and hand their voices back. The game runs from the logos through
the title, into the story and on through cutscenes (`STORY_SCENE_MAIN`, scenes
0110/0120) and several room changes -- thousands of frames, not tens.

There is **no IOP tracing any more.** `MIOPAN_IOP_TRACE` and everything it
gated -- the command-queue walk in `iop_sif.cpp`, the SPU key-on and
`irqa armed` / `irq` lines in `iop_libsd.cpp`, the per-voice and reverb lines in
`iop_voice.cpp`, the work-area and depth lines in `iop_reverb.cpp` -- were all
removed on request. What survives in `miopan.log` is the ROM's own printf stream
plus the port's always-on banners: the audio device line, the one-time cross-core
IRQA warning, `Trans Is Not in Time`, and the stream release lines. A healthy run
shows the releases pairing up and **no** `Trans Is Not in Time`.

If you need per-voice or per-command detail again, add it back locally and take
it out before committing -- do not reintroduce the env var.

Open, none of them sound-related:

- `SgPreRenderPrim` crashes on a garbage SGD pointer after
  `NO_MODEL_IN_FURN_CTL[0.sgd]` -- map/model code. `NO_FURN_KEY_NAME`,
  `GetMapLabelFromAreaLabel` and `PFire Work Is Full` are the same family.
- `MioPan_IopHostShutdown()` exists but nothing calls it, so IOP threads are not
  joined at exit.

Runs are not deterministic -- real threads, so timing varies, and several of the
faults below only appear in some runs.

### How the audio engine got built, and what it cost

Kept as a record of the faults, because each one has a distinctive symptom and
they will recur if the engine is reworked. Item 1 was EE-side; the rest are the
port. `sceSdBlockTrans` (the `iop_pcmstream.c` path) is still stubbed -- and
`PCMStreamMain()` is unreachable in the ROM anyway, so PCM pause and its volume
ramp are dead code regardless of the mixer.

1. ~~**Nothing asks for sound.**~~ **RESOLVED — the EE does issue `REQ_SB_PLAY`
   and the IOP keys voices on.** Two separate things were being read as one
   blocker:

   - **The idle title screen legitimately plays nothing.** `TITLE_TOP` has no
     BGM (title.c's only `StreamAutoPlay` is in the *Load Game* submenu) and its
     only SE is `SystemBankPlay(3, ...)` inside `TitleTopPad()`, fired on START.
     An unattended run parks there, so "no commands past init" is the expected
     reading, not a fault. `snd.c` / `snd_buffer.c` / `snd_bank.c` are complete
     and the trigger sites are reached; forcing the `TitleTopPad()` call gives
     `SndBankPlay` → `SndBufPlay` → `iopCommandRegister(REQ_SB_PLAY)` → command
     20 at the IOP → `SPU KON core=0 mask=000001`, end to end.
   - **A real loader race was silencing the system SE bank ~2 runs in 3.**
     `thFileLoad()` dropped the loader mutex after `rpcFileLoadReqSub()` and then
     re-derived the in-flight entry from `yet_files` *unguarded*. A
     `FileLoadReq()` landing in that window runs `SwapFileLoadWrk()` +
     `yet_files++` while the loader is indexing, so the re-read lands on a
     different request's slot: that entry's callback fires **twice** and the
     served entry's callback is **never fired**. Caught directly — three BD
     loads armed (`3321`, `3323`, `3101`), three callbacks delivered, but two
     of them identical (`arg`/`buffer` both `…c48`/`0x26ad0`, file 3323) and
     none for 3321. `intrSndBankFileBD` therefore never set `m_Ready` on
     `base_sys_bd.bd`, `snd_bankIsReady()` stayed 0, and **every**
     `SystemBankPlay()` returned `CSND_BUF_PLAY_NO_ID` silently. Fixed by
     re-claiming the mutex around the re-read and bookkeeping and firing both
     callbacks after releasing it (`fileload.c`, marked `PORT:`). Bank readiness
     went from 2/6 to 8/8, with 8/8 key-ons.

   Note this bites *any* big file that other requests queue behind, not just
   sound — `base_sys_bd.bd` (0x21a70) is simply the widest window. **A "LoadEnd
   printed but the completion callback never ran" symptom is this bug.**
2. ~~**No output at all.**~~ **DONE — `sdk/iop_voice.{h,cpp}` is the
   synthesiser and the mixer.** It began as MikuPan's `src/iop/se/voice.cpp`
   (one `SDL_AudioStream` per voice, SDL doing the summing and the resampling)
   and has since been rewritten around a real mixer -- see "The mixer, and why
   there had to be one" below. ADPCM block decode, full ADSR in Q16, our own
   resampler. Addresses are **bytes** here (MikuPan's `spuRam` is `s16[]`, so
   its `nax`/`lsa`/`irqa` are half-word indices), and voice ownership stays in
   the ROM.
3. **`SD_A_IRQA` is wired** via `MioPan_SdVoiceReachedAddress()`, called from the
   decoder as playback actually crosses the armed address, with one block of
   tolerance (the decoder steps 16 bytes, so an IRQA off the step would
   otherwise be missed). Streams verified working end to end: BGM, event voice
   lines and scene ADPCM all start, refill, and release cleanly.
4. **`SD_VA_NAX` / `SD_S_ENDX` now come from real playback.**
   `MioPan_SdSetVoiceEnd()` latches ENDX on **any** loop-end block, repeating or
   not — that is the hardware behaviour and MikuPan does *not* do it (it raises
   ENDX only on the non-repeating case). Fatal Frame parks a finished one-shot
   in a self-repeating stop block, so MikuPan's rule would never latch and
   `EndVoiceFindWork()` would leak the voice slot.

### The three bugs that stood between "streams start" and "streams work"

Found in this order; each hid the next. All three are `PORT:`-commented.

- **The ADPCM loop-start bit must not override a host LSAX write.**
  `FillAdpcmHeader()` honouring bit 10 unconditionally (MikuPan's behaviour)
  overwrites the loop address `iop_stream.c` rewrites every cycle to swing the
  voice between its two SPU buffers, so the voice loops back into the packet it
  is already playing — *the stream repeats its first packet forever*. SPU2
  latches a host LSAX write and suppresses the header bit; `lsa_host_set` models
  that. MikuPan gets away without it because its music streamer does not drive
  LSAX this way.
- **`irq_core` is a stream-slot index, not a core.** `StreamVoiceThread()` arms
  `SD_A_IRQA | stp->irq_core` with *channel 0's* address, but `SetIRQCore()`
  hands `irq_core` out of its own two-entry `irq_core_source[]` pool while the
  core a channel plays on is `wrk->p.attr[i].core` from the HXD. They agree only
  by convention, and for a second concurrent stream they routinely do not —
  IRQA armed on core 1, voices on core 0, refill never fires. The port matches
  the other core's armed IRQA as a fallback (safe: stream buffers are distinct
  SPU allocations) and prints a one-time banner naming it.
- **NAX must follow LSAX after the voice is keyed off.** The ROM's stop sequence
  is *end the voice, key it off, then* point LSAX at the stop block and poll NAX
  until it arrives. Gating the NAX read on `keyed_on` (or on "still draining")
  leaves it reporting a stale mid-buffer address forever:
  `stop voice wait id N adrs ...` spins, the stream slot never releases, the
  next stream blocks on `StreamAutoIsPreload() Wait Other Stream End`, and the
  leaked voices starve `GetSPUVoiceCore()` so **in-game SE stop working too**.
  An idle SPU2 voice sits on LSAX; report `loop_adrs` whenever the engine is not
  actively decoding, with no `keyed_on` test.

**Reading the symptoms:** a stream repeating its opening = the first bug. A
*second* stream silent while the first is fine = the second. `stop voice wait`
repeating with a mid-buffer address = the third — and that one also explains
in-game SE going missing, which looks like an unrelated fault.
**Jitter/stutter lives in the decode thread, not the decoder.** Two fixed:
`SDL_GetAudioStreamQueued()` was called per decoded block (up to 128 a voice, 48
voices a pass) and SDL takes the stream's own lock inside it — the same lock its
mixing thread needs to pull playable audio, so the query storm starved the
callback. Measure the shortfall once and derive a block budget. And the decode
thread ran at default priority, which MioPan's loader and decompression threads
out-compete during room/scene transitions; it asks for
`SDL_THREAD_PRIORITY_HIGH` now. If stutter persists, the next two levers are
`voice_mutex` contention (held across all 48 voices while the EE pushes a
`sceSdSetParam` per active voice per frame) and `SDL_SetAudioStreamFrequencyRatio`
being reconfigured on every pitch ramp step.

5. **`sceSdBlockTrans`** — still stubbed, the whole `iop_pcmstream.c` path.
6. ~~**Reverb**~~ **DONE — `sdk/iop_reverb.{h,cpp}`**; see its own section
   below.

**A latent bug this uncovered:** `EntryReg()` masked `0xff00`, which strips the
`0x40` bit that is *part of* the `SD_VA_*` selectors (`SD_VA_SSA` is `0x2040`),
so **every** `sceSdSetAddr()` write fell through to `default:` and was discarded.
Invisible while nothing read the addresses back — `start_adrs` stayed 0 and the
modelled NAX still satisfied the ROM's settle loops — and it surfaced instantly
as a voice decoding silence from SPU address 0. The register field is
`entry & ~0x3f`, since core is bit 0 and voice is bits 1..5.

**And the identical bug next door, found while wiring reverb:** the master-volume
forwarding in `sceSdSetParam()` tested `EntryReg(entry) == (SD_P_MVOLL & 0xff00)`
-- 0x0900 against `EntryReg()`'s 0x0980, which can never match. `SD_P_MVOLL` is a
per-core parameter at 0x0980, so the 0x80 is part of the selector exactly as the
0x40 is for `SD_VA_*`. Master volume had therefore **never once** reached the
voice engine; it looked right only because the engine's own default is 0x3fff,
which is the value `iopSndInit()` passes as `si->mvol`. Compare with `EntryReg()`
on both sides, never with a hand-written mask -- the `SD_P_EVOLL/EVOLR` and
`SD_P_BVOLL/BVOLR` tests beside it now all do.

Note `PCMStreamMain()` is unreachable in the ROM, so PCM pause and volume ramps
will not work even with a perfect mixer.

### What is left of stereo pairing

`MioPan_VoiceSetStereoPair()` existed because channels of one stream, given a
`SDL_AudioStream` each, drifted: every fill rounded up to a whole ADPCM block
independently, so the pair skewed by up to 28 samples and the skew *moved* every
pass. A wandering inter-channel delay is a sweeping comb filter, and on speech it
was unmistakable flanging -- reported as voices sounding "like a helicopter".
Two further bugs were then introduced while fixing that, both audible as the same
voice twice, slightly apart.

**The mixer makes all of that impossible.** Every voice steps off one block
counter, so two channels cannot skew by even a sample, and there is no second
stream for a copy to escape onto.

What the pairing still buys is a shared **lifetime**: a channel left sounding
after its partner stopped keeps `MioPan_VoiceIsPlaying()` answering yes, which
pins `SD_VA_NAX` to a frozen position instead of the loop address -- the
stop-block stall again, from a new direction. `BeginVoiceEnd()` and
`StopVoicePlayback()` propagate to the partner for that reason alone. The port's
`iop_stream.c` `StreamPlay()` still declares the pairing once the EE's voice
assignment is known (a `PORT:` line), and should keep doing so; a pairing is
armed there and consumed by the next key-on, so it survives exactly the playback
it was declared for.

### The mixer, and why there had to be one

The engine was 48 independent `SDL_AudioStream`s, one per voice, with SDL summing
them and `SDL_SetAudioStreamFrequencyRatio()` doing the resampling. **That shape
cannot carry an effect send**, and the reason generalises: reverb needs a shared
wet bus, a bus needs its contributors aligned in *output* time, and with the
resampling happening inside SDL there is no way to know where a voice's samples
landed. Each voice also carried its own 0..42 ms decode backlog, so a bus fed
from decode time would put the tail ahead of its own transient by a margin that
wanders per voice and over time -- a pre-echo, not a room. Shrinking the backlog
to hide it only trades it for underruns, which the file already documents as a
live risk during room transitions.

So `iop_voice.cpp` now owns the resampler and the mix: 256-frame blocks, per-core
dry and wet accumulators, one output stream. Three workarounds went away with the
old shape -- the stereo pairing's alignment role (above), the per-voice
queue-depth pacing heuristic and its storm of `SDL_GetAudioStreamQueued()` calls,
and an envelope that used to step once per *source* sample and so ran fast on a
pitched-up voice.

Two things about it are load-bearing:

- **A pass ends when an SPU2 interrupt fires**, not after a fixed number of
  blocks. The mixer holds the voice lock for a whole pass, so until it lets go
  the IOP cannot service the interrupt and re-arm `SD_A_IRQA` -- and a voice that
  crossed a second armed address in the meantime would match nothing and lose
  that refill outright. The old code approximated this with a 128-block cap
  (exactly one 0x800 buffer half); a block count cannot express it once pitch
  varies, so `MioPan_SdIrqSerial()` is sampled instead.
- **Interpolation is 4-point Catmull-Rom.** SPU2 uses a 512-entry gaussian FIR
  held in hardware ROM, which cannot be derived. This is the closest practical
  stand-in; it is also a small step down from SDL's band-limited resampler, which
  is the one thing the rewrite cost.

Deliberately **not** modelled: the core-0-feeds-core-1 chain (`SD_P_AVOLL/AVOLR`
and MMIX's SIN bits). Both cores' output is simply summed. On hardware core 0
would pass through core 1's master volume as well and land 6 dB below core 1's
own voices; reproducing that would rebalance every sound in the game, which is a
separate question from whether reverb exists.

### Reverb

`iop_reverb.cpp`. The PlayStation reverb network, unchanged on SPU2 apart from
the clock: two IIR reflection stages (same-side and cross-side), a four-tap comb
early echo, two all-pass sections, all over one circular work area at the top of
SPU RAM that advances a sample per tick, at half the output rate.

The ROM drove all of it already and always had -- `map_reverb.c` carries a
66-entry per-area depth table and re-applies it whenever the player crosses into
a new area, `outgame.c` sets 0x2fff at two points, and `iopSndInit()` sizes and
clears the work area at boot. Only the DSP was missing, so every interior played
anechoic.

Four things worth knowing before touching it:

- **The game uses exactly one preset.** Every `SndSetEffect()` call site passes
  mode 3. After the first one the EE's `effect_mode[core]` matches, so every
  later call takes the same-mode path and moves only `SD_P_EVOLL/EVOLR` -- the
  per-room depth is the *only* thing that ever changes.
- **The preset tables verify against the ROM.** `eff_use_size_tbl[]` in the EE's
  `snd.c` lists ten work-area sizes, and they match the ten published SPU reverb
  presets exactly and in order, which is what pins mode 3 to **Studio Medium**
  (0x4840 bytes, ~385 ms). For seven of the ten, the preset's longest tap times
  eight lands within 0x20 bytes of the ROM's own figure. Two independent sources
  give identical coefficients for mode 3.
- **The address registers count EIGHT BYTES**, so a preset's taps are `reg * 4`
  samples and psx-spx's `[mLSAME-2]` -- a byte offset -- is one sample back.
  Taken at face value a preset would use a quarter of the buffer the game
  reserved for it, which is also how you can tell the conversion is right.
- **Volumes are signed Q15**, so 0x8000 is -1.0. `vLIN`/`vRIN` are 0x8000 in
  every preset: the send is phase-inverted going in. That is inaudible on its own
  and matters only if someone "fixes" it.

`SD_A_EEA` is the **last byte** of the work area (`spu_mem.c` hands out
`final_end_adrs - 1`), and the ROM writes it immediately before every
`sceSdSetEffectAttr()`; the base is derived from it and the preset's size. A bad
EEA leaves the core inactive rather than scribbling over sample data. At boot
both cores are pointed at the same area with effect disabled, and only core 0 is
ever enabled -- the ROM never calls `SndSetEffect()` with core 1.

`attr.delay` and `attr.feedback` are ignored. They mean something only to the
echo and delay presets, and `SndSetEffect()` never initialises either field --
both arrive as whatever was on the EE's stack.

`MioPan_ReverbIsActive()` is the single gate: enabled, a work area that resolved,
and a non-zero EVOL. When reverb is inaudible the order to check is
`SD_C_EFFECT_ENABLE`, then EVOL, then the `SD_S_VMIXEL` send that `EffectMix()`
builds out of the sound data. A wet return pinned near 32767 instead would be an
IIR ringing rather than decaying -- but nothing prints either of those now, so
the quick way to tell them apart is the standalone harness below rather than a
game run.

The DSP is separable enough to exercise on its own -- compile `iop_reverb.cpp`
against a stub `MioPan_SpuRamPointer()` and a stub `SDL3/SDL_mutex.h`, impulse
it, and watch the envelope. Studio Medium at full depth should build for ~150 ms
and decay smoothly to -60 dB by about 1.2 s. That is a far quicker answer than
waiting for an unattended run to reach a room, because it will not: the game
parks at the title screen, and only phantom input gets it further.

### Diagnosing an audio fault by ear

Adjectives map onto distinct mechanisms here, and the mapping is worth trusting
far enough to pick the first thing to look at:

| Symptom | Mechanism |
|---|---|
| a stream repeats its opening forever | ADPCM loop-start bit overwriting the host LSAX |
| a *second* stream is silent, the first fine | `irq_core` vs `attr.core` mismatch -- no refill |
| general stutter under load, everything at once | mixer thread starved -- it asks for `SDL_THREAD_PRIORITY_HIGH`, so look at what is out-competing it |
| chopping at ~13 Hz | one SPU buffer half (0x800 B) per chop -- refill not landing |
| every interior sounds anechoic | reverb: `SD_C_EFFECT_ENABLE`, EVOL, or the `SD_S_VMIXEL` send |
| a metallic ring that never decays | reverb IIR: a wrong `vIIR`/`vWALL`, or taps outside the work area |
| flanging / "helicopter", or the same audio twice | **was** per-voice audio streams drifting; the mixer rules it out, so look elsewhere |

But do not stop at the adjective. Two mis-attributions in one session came from
reasoning forward from a description; both were settled in one run by a probe.
With the tracing gone the probe has to be written each time -- the shapes worth
reaching for are a per-voice line keyed on `nax` jumping backwards (printing a
block `peak`, so "running but silent" is obvious) and a wet-return peak in
`MixReverb()`. There is also a `TEMP PROBE` stall dump in
`stream_auto.c`'s `StreamAutoIsPreload()` wait branch printing every play slot's
`disable` / `IsUse` / `ST_STREAM_*` and the wait-queue head. That dump is what
identified the `WAIT_END` deadlock; read it before theorising.

## Whole-module verification

```bash
cd iop/tools && python verify.py
```

Five checks that between them cover the reconstruction end to end. All pass at
the time of writing; re-run after any substantial edit, alongside `check.sh`.
Neither needs Ghidra — everything comes from the ELF via `irx.py` and from the
stdump listings.

1. **Every FUNC symbol has a definition** — `irx.py funcs` against the sources,
   134/134.
2. **Signatures match stdump.** Compares each function's parameter *names* and
   `static`-ness with `iopsys_functions.c`. Parameter names are the strongest
   cheap signal; all 134 agree.
3. **Globals match**, same way, against `iopsys_globals.c` — 51/51 with the
   right storage class. Then asserts the exact set of non-zero bytes in `.data`
   (0x7b10..0x80b0): seven, being `Module`'s name pointer and version and
   `stop_block[4]`'s four `0x07` loop flags. Anything else would be an
   initialiser lost to a zeroed declaration — see
   `zeroed-statics-lose-rom-initialisers`.
4. **Every `.rodata` literal is used.** Extracts the 110 strings (skipping
   `iopCommand()`'s jump table at 0x6e68) and greps each in the sources; a
   missing one means a missing `printf` or assert.
5. **Per-function call-graph diff.** Collects each function's `jal` targets and
   diffs against the callee names in the reconstructed body — the check most
   likely to catch a dropped call in otherwise plausible logic. Two
   false positives are handled in the script: `MyCdRead`'s `memset` is GCC
   expanding the `sceCdRMode crm = {...}` initialiser, and function names inside
   printf *format strings* look like calls, so string literals are stripped
   first.
