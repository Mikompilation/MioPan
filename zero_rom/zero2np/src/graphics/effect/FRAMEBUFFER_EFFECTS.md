# Framebuffer effects — survey before the port

Every effect in this tree that reads the frame buffer back and redraws it: what
it does, which GS mechanism it uses, and why none of it is visible on the host
yet.

The inventory (§1–§4) is a survey and changes nothing. The three bugs in §5
and the fourth in §10 are **fixed** — they predate the port work and each would
otherwise have been misread as a capture failure later.

Measured against the Feb 6 2004 prototype (`SLES_523.84`) and the current
working tree.

---

## 1. The four mechanisms

The ROM has four separate ways of getting the frame back, and they fail on the
host for **different reasons**. Porting has to address them separately.

### A. Sample the live frame buffer directly as a texture

`DispSprD2()` with a TEX0 whose `TBP0` is one of the two display buffers. No
copy: the GS reads the field that is on screen while drawing into the other one.

```c
ds.tex0 = (u_long)((sys_wrk.count + 1 & 1) * 0x1180) | 0x2000000268128000ULL;
```

Decoded: `TBP0 = 0x0000 or 0x1180`, `TBW = 10` (640 px), `PSMCT24`,
`TW/TH = 1024x512`, `CLD = 1`.

### B. VRAM → VRAM blit into a scratch page, then sample the scratch

`LocalCopyLtoL(type, src_block, dst_block)` — `g2d_draw.c:1377`. Builds a GIF
packet that points `FRAME` at the destination and `TEX0` at the source, then
draws one textured sprite. `scpw[type]` holds the src/dst rectangle.

### C. VRAM → EE readback, process on the EE, upload back

`LocalCopyLtoB()` → `EffImageHalf32()` → `LocalCopyBtoL()`. Used where the
effect wants a stash that survives the buffer swap, or a half-width image.
`EffImageHalf32()` (`effect.c:1800`) keeps every other pixel of every row,
packed to the front — the height is unchanged.

### D. Depth readback

`SetScreenZ()` / `LocalCopyZtoBZ()` / `LocalCopyBZtoZ()` (`effect_sub.c`),
`PSMZ16S` at `0x2300`. Already documented: this is what `CheckPointDepth()`
uses for finder-target occlusion, and it is why the test currently degrades to
"on screen ⇒ visible".

---

## 2. The GS scratch map

| Block | TBW | PSM | Geometry | Written by | Read by |
|---|---|---|---|---|---|
| `0x0000` / `0x1180` | 10 (640) | PSMCT24 | 640x448, double buffered | the GS itself | mechanism A, directly |
| `0x2bc0` | 5 (320) | PSMCT24 | 320x448 | `LtoL` type 3, `BtoL` type 2 | nega, all six deforms, overlap cross-fade, parts deform, camera flash, ghost big-hit |
| `0x3aa0` | 5 (320) | PSMCT24 | 320x224 | `LtoL` type 1, in `EffectControl()` | `SubBlur()` photo path, `SubPartsDeform1()` page 1 |
| `0x3480` | 5 (320) | PSMCT24 | 320x224 | `LtoL` type 1 | `draw_distortion_particles()` |
| `0x2200` + CLUT `0x3ffc` | 2 (128) | PSMT8H | 128x128 | `sceGsExecLoadImage` | `SubDither3()` / `SubDither4()` |
| `0x2300` | 10 | PSMZ16S | Z strip | `LocalCopyZtoBZ()` | `CheckPointDepth()` |

**`0x2bc0` is shared.** `ScreenSaverDraw()` samples the same block as a
`PSMT8` image with a CLUT at `0x2cc0` — the loaded screensaver picture. Any
host-side allocator for these pages has to respect that the game reuses the
address for two unrelated things at two different times.

### The six `LocalCopyLtoL` geometries

Recovered from the ROM at `0x314830` (see §5, bug 1):

| type | source | destination | used for |
|---|---|---|---|
| 0 | 640x448 | 640x448 | 1:1 full-frame copy (pause, photo) |
| 1 | 640x448 | 320x224 | quarter-size stash (overlap, distortion particles) |
| 2 | 320x224 | 640x448 | the type-1 stash blown back up |
| 3 | 640x448 | 320x448 | half-width scratch — the deform/refraction source |
| 4 | 320x448 | 640x448 | the type-3 scratch blown back up |
| 5 | 640x448 | 640x224 | half-height (photo) |

---

## 3. The inventory

### `effect_scr.o` — the screen-filter module

| Effect | Mech | Source | Note |
|---|---|---|---|
| `SubBlur()` | A / B | frame buf, or `0x3aa0` when `bPhotoType == 0` | radial smear, rotated and scaled about (320, 224) |
| `SubFocus()` / `SetFocus` / `RunFocus` | A | frame buf | defocus envelope |
| `SetForcusDepth()` | A | frame buf | depth-of-field variant |
| `SubContrast2()` / `SetContrast2` | A | frame buf | whole-frame contrast lift |
| `SubNega()` / `SetNega` | B | `LtoL` 3 → `0x2bc0` | colour inversion after a ghost's blow |
| `SetDeform0/2/3/4/5/6` | B | `LtoL` 3 → `0x2bc0` | six mesh warps; raw DIRECT packets |
| `SetOverRap()` | A **and** C | frame buf, then `LtoB` → half → `BtoL` → `0x2bc0` | frame ghosting / trails |

Not framebuffer effects, despite living next door: `SubContrast3()` is an
untextured flat blend drawn twice, `SubFadeFrame()` uses `effdat[0x4a]`, and
`BrightnessAdjustmentFilterDraw()` is a plain `DispSqrD`. `SubDither3()` /
`SubDither4()` sample the uploaded noise sheet, not the frame — **and they
already work on the host**, because `MakeRDither3()` uploads through
`sceGsExecLoadImage()`, which is implemented.

### `effect_obj.o`

| Effect | Mech | Source |
|---|---|---|
| `SubPartsDeform1()` | B | `LtoL` 3 → `0x2bc0` (page 0), or `0x3aa0` directly (page 1) |
| `SubPartsDeform2()` | B | `LtoL` 3 → `0x2bc0` |
| `SetCameraFlash()` + `EffectCameraFlashDrawSub()` | C | `LtoB` → `EffImageHalf32` → `BtoL` → `0x2bc0` |

### `effect_ene.o`

| Effect | Mech | Source |
|---|---|---|
| `EneDmgLargeHitCtrlMain()` + `EneDmgLargeHitEffectDisp()` | C | `LtoB` → `EffImageHalf32` → `BtoL` → `0x2bc0` |

The ghost's heavy blow is a screen refraction, not a sprite.

### `effect_oth.o`

| Effect | Mech | Source |
|---|---|---|
| `draw_distortion_particles()` | B | `LtoL` 1 → `0x3480` |

Four call sites (`SubHalo()` and friends). Its ST comes from the particle's own
*screen* position scaled by 1/1024 and 1/512 — the frame buffer page's
dimensions — so each particle samples whatever is behind it, displaced. Its
sibling `draw_distortion_particles2()` reads ST from a table and is an ordinary
sprite.

### `effect.o` — the driver

`EffectControl()` at ROM line 1684 does the per-frame overlap stash:
`LocalCopyLtoL(1, (count & 1) * 0x1180, 0x3aa0)`, gated on `EffWrkStopFlgGet()`.
Everything that samples `0x3aa0` depends on this one call.

### Same machinery outside `effect/`

Worth porting together, because they share every host-side prerequisite:

| File | Sites | What |
|---|---:|---|
| `ingame/photo/photo.c` | 8 | the photo phase's frame juggling |
| `ingame/ingame.c` | 4 | `end_Story_Pause` / `_Menu` / `_Map` / `_Pause_Mission` |
| `ingame/pause/prg/pause.c` | 2 | the paused-frame backdrop |
| `outgame/SpriteCmn.c` | 2 | `SpCmnGetScreen()` / `SpCmnDrawScreen()` |
| `ingame/photo/photo_make.c` | 1 | `CopyScreenToBuffer2()` — the saved picture itself |

---

## 4. Why none of it shows on the host

This is the state as surveyed. **Mechanism A has since been fixed** — but not
by removing either blocker below; see §7 for why that route was abandoned and
what replaced it. Both blockers still stand for mechanisms B and C.

Two independent blockers. Fixing either alone changes nothing.

**The renderer never mirrors the frame into emulated GS memory.**
`MioPan::GS::gsHelper` is a complete 4 MB swizzled VRAM emulation with correct
upload and download for every PSM, but it is fed *only* by EE→GS uploads
(`sceGsExecLoadImage` → `MioPan_GsUpload`). The renderer draws to the SDL
swapchain and never writes back. So `TBP0 = 0x0000` / `0x1180` decode whatever
stale bytes happen to be there. The `framebufferUvOffset` / `framebufferUvScale`
/ `framebufferContentUvMax` uniforms at `miopan_renderer.cpp:374` are declared,
defaulted, and never driven — placeholders, not a partial implementation.

**`dmaVif1` discards every packet.** `LocalCopyLtoL()` and
`LocalCopyBtoLAdrs()` build raw GIF packets into the PK2D ring;
`dmaVif1Kick()` (`dmaVif1.c:236`) only flips a toggle and clears the ring. So
mechanism B is dropped whole, and mechanism C's upload half is dropped.

`LocalCopyLtoB()` is the one exception — it goes through
`g3dGsExecStoreImage()`, which is real — but it reads the frame buffer that was
never written, so it returns zeros.

Neither `MakeScrDeformPacket()` nor `MakePartsDeformPacket()` has a host bridge
(unlike `Set3DPosTexure()`, which has `RendererPacket3D`). Even with a captured
frame, the six deforms and both parts-deforms would still draw nothing.

### The hook that already exists

`MioPan_RendererCaptureScreen(addr)` and
`MioPan_RendererDrawCapturedScreen(addr, r, g, b, a)`
(`miopan_renderer.cpp:5194`, `:5205`) already do exactly the right thing for one
case: they key a GPU texture by GS address, blit the swapchain into it in the
frame's copy pass, and draw it back. They were built for graphics.o's
`CaptureScreen()` / `DrawScreen()`, which currently have no callers anywhere in
the tree.

That is the right shape. What it needs to become general:

- **N slots keyed by GS block address**, not one `g_pause_capture_addr`.
- **Mid-frame capture.** `RecordPauseScreenCapture()` runs at present time;
  these effects capture and re-draw within the same frame, several times.
- **Arbitrary geometry.** `DrawCapturedScreen` forces a full-screen quad at
  fixed UV. `SubBlur()` rotates and scales about a centre; the deforms want a
  33x25 (screen) or 16x16 (parts) UV mesh.
- **Downscaled captures.** Types 1 and 3 are not 1:1 — and the half-width
  scratch at `0x2bc0` is what almost every refraction samples.

---

## 5. Three bugs found while surveying — all fixed

A fourth, found only by running the result, is in §10.

All three are fixed in the working tree and the build is clean. They are kept
written up here because each one would otherwise be misread as a
framebuffer-capture failure once the capture is real.

### Bug 1 — `scpw[6]` lost its ROM initialisers &nbsp;·&nbsp; FIXED

`g2d_draw.c:1379` declares

```c
static SCREEN_COPY_WRK scpw[6];     // data 314830
```

with no initialiser, and nothing writes it at runtime. But `ZERO2.MAP` puts
g2d_draw.o's `.data` at exactly `0x314830`, size `0x100`, and the ROM's bytes
carry all six copy geometries. Every field except `stbp` and `dtbp` — which
`LocalCopyLtoL()` assigns per call — is therefore zero: zero buffer width, zero
source rect, zero destination rect. Even once `dmaVif1` executes packets, every
blit in mechanism B would copy a 0x0 rectangle.

Recovered from the ROM (`stbp` and `dtbp` are always overwritten, so they read 0
here):

```
      sfbw  stw  sth   dfbw   dw   dh    du   dv
[0]     10   10    9     10  640  448   640  448
[1]     10   10    9      5  320  224   640  448
[2]      5    9    8     10  640  448   320  224
[3]     10   10    9      5  320  448   640  448
[4]      5    9    9     10  640  448   320  448
[5]     10   10    9     10  640  224   640  448
```

This is [[zeroed-statics-lose-rom-initialisers]] again.

**Fixed.** The table is written out in `LocalCopyLtoL()` and verified with
`objdiff.py`: the compiled `g2d_draw.c.obj` now contains the ROM's 240 bytes
verbatim, which proves values, layout and padding all round-tripped.

### Bug 2 — a dropped shift makes the ghost's big hit read block `0x23` &nbsp;·&nbsp; FIXED

`effect_ene.c:3155` and `:3165` read

```c
LocalCopyLtoB(0, 0, ((sys_wrk.count + 1) & 1) * 0x23);
```

Ghidra prints the ROM as `(((sys_wrk.count + 1 & 1) * 0x23 << 0x27) >> 0x20)`,
which is a `dsll32`/`dsra32` pair — `v << 39 >> 32` is `v << 7`, and
`0x23 << 7 == 0x1180`. The transcription kept the `0x23` and dropped the shift,
so the readback targets GS block `0x23` instead of the second frame buffer.
`effect_obj.c:3091` writes the same expression correctly as `* 0x1180`.
graphics.o's `CaptureScreen()` has the identical ROM idiom.

**Fixed.** Both sites now read `* 0x1180`, with the shift explained at the first
one. `graphics.c:1197` is deliberately left alone: it drops the readback
entirely and calls `MioPan_RendererCaptureScreen()` instead, which is a host
substitution rather than the same bug. Worth knowing while generalising that
API — the ROM's `CaptureScreen()` **ignores its `addr` parameter** and derives
the address from `sys_wrk.count`; the port uses `addr` as the capture key.

### Bug 3 — three untranslated EE addresses into `EffImageHalf32()` &nbsp;·&nbsp; FIXED

| Site | Argument |
|---|---|
| `effect_ene.c:3156` | `(u_int *)0x1e79b00` |
| `effect_ene.c:3165` | `(u_int *)0x1e79b00` |
| `effect_obj.c:3092` | `(u_int *)0x1e79b00` |
| `effect_scr.c:1838` | `MioPan_GetHostPointer(0x1e79b00)` — correct |

`0x01E79B00` is inside the EE window (`0x00400000..0x05100000`,
`miopan_memory.cpp:7`), so the first three are wild pointers.
`EffImageHalf32(p, 640, 448)` writes 320x448 words (573 KB) and reads about
1.1 MB from there. These fire the first time a ghost lands a heavy blow or the
camera flash goes off. The raw literal is faithful to the ROM; the host needs
the translation, exactly as `effect_scr.c` already does it.

**Fixed.** All three now go through `MioPan_GetHostPointer()`. A sweep for the
rest of the family found no other live cases: `effect_sub.c`'s `buf`/`buf2`
are assigned and never read, `pk2d_wrk.idx_now` is only dereferenced by two
callerless `printf("kitemasu")` stubs, and `LANG_MSG_ADDR` is passed as a
value to `LoadReq()`/`PK2SendVram()`, which translate it themselves.

---

## 6. Suggested porting order

1. ~~**Fix the three bugs above.**~~ **Done** — all three fixed, build clean,
   `scpw` verified against the ROM out of the compiled object. A fourth bug
   (§10) surfaced later, on the first run.
2. ~~**Mirror the resolved frame into `gsHelper.mem_`.**~~ **Done, by a
   different route — see §7.** Mirroring into GS memory turned out to be the
   wrong mechanism; mechanism A is served from a GPU-side capture instead.
3. ~~**Give `LocalCopyLtoL()` a host bridge.**~~ **Done — see §8.** It lights
   up fewer effects than this line assumed: most mechanism-B consumers draw
   through packet builders that have no host bridge at all, so they wait on
   step 5.
4. ~~**Bridge `LocalCopyBtoLAdrs()`.**~~ **Done — see §8**, but by capture-slot
   copy rather than `MioPan_GsUpload`, and **unobservable until step 5**: both
   of mechanism C's consumers draw through `EneDmgLargeHitMakePacket`, which is
   also unbridged.
5. ~~**Host bridges for the three packet builders.**~~ **Mostly done — see
   §9.** All three are bridged. Two call sites remain: `SetDeform0/2/3` build
   their packets inline rather than through `MakeScrDeformPacket`, and
   `draw_distortion_particles()`'s textured case.

**Step 5 is now the bottleneck.** Steps 2–4 fill every capture slot the effects
read; those three packet builders are the only thing between that and the six
deforms, both parts-deforms, the camera flash, the ghost's heavy blow and the
heat haze.

---

## 7. Mechanism A is done — the GPU-side scene capture

Implemented in `miopan/rendering/miopan_renderer.cpp`. **Not yet run in-game.**

### Why not `gsHelper.mem_`

Step 2 was written as "mirror the resolved frame into emulated GS memory", on
the grounds that `GetTexture()` already decodes from there and no game code
would have to change. Measuring the renderer killed that plan on two counts,
and the second is fatal rather than merely expensive:

- **Cost.** It needs a GPU->CPU readback of the colour target, a swizzle into
  GS memory, then a hash, a decode and an upload back — about 5 MB of traffic
  and a pipeline stall, every frame, with a guaranteed texture-cache miss
  because the frame changes every frame.
- **It cannot produce the right picture.** Host draws are queued and replayed
  at present time, so at the moment the game calls the effect nothing has been
  submitted yet. A readback can only ever return the *previous* frame. Half
  these effects sample `(count & 1) * 0x1180` — the page being drawn into,
  mid-composite — and would be a frame stale and missing everything drawn
  after them.

### What was built instead

`g_draws` is an ordered command list replayed at present, which is exactly the
hook needed. A draw that samples the frame buffer now **breaks the replay**:

```
DrawTexturedQuadImpl        IsFramebufferTex0(tex0)?  ->  draw from the capture,
                                                          flag capture_before
MioPan_RendererEndFrame     replay [0, i)  ->  end pass
                            RecordSceneCapture()      (copy pass)
                            replay [i, ..) ->  end pass
```

A copy pass between two render passes is ordered against both, so the draw sees
exactly the composite that preceded it — which is what the GS gave it when it
sampled the display buffer directly. No readback, no stall, no cache miss.

- **`IsFramebufferTex0()`** — `TBW == 10` (the 640 px frame stride), a PSMCT
  format, and `TBP0` of `0x0000` or `0x1180`. Nothing else can live below the
  Z buffer at `0x2300`, so this cannot catch a real texture.
- **The quad is grown about the frame centre by `g_view_extend`** before
  `ApplyOriginalAspectToVertices()` maps it back out, the same trick
  `MioPan_RendererDrawSolidQuad()` uses, and the UVs are normalised against
  640x448. Window maps onto window, so a 1:1 filter stays 1:1 on any aspect.
- **The capture texture is allocated on demand**, in `UpdateViewExtend()` and
  nowhere else. It is a full output-sized target and most of the game never
  draws a screen filter. `RecordSceneCapture()` deliberately allocates
  nothing — draws queued earlier already hold the pointer, so reallocating
  mid-frame on a window resize would leave them dangling; the copy is clamped
  to whatever BeginFrame allocated instead. Cost is one dropped frame the very
  first time a screen filter appears in a session.
- **A framebuffer draw at index 0** gets no capture, because nothing has been
  drawn yet; it samples the previous frame's capture, which is a fair reading
  of "the displayed page".

The segmentation loop was checked exhaustively over every capture-flag pattern
for 0..12 queued draws: every draw is replayed exactly once and in order, a
capture is issued immediately before every flagged draw but the first, no empty
segments, always terminates. With no framebuffer draw it is a single segment
and one pass, so an ordinary frame pays one vector walk.

### What this does and does not light up

| | |
|---|---|
| **Works now** | Mechanism A — `SubBlur` (live path), `SubFocus`, `SetForcusDepth`, `SubContrast2`, `SetOverRap`'s first-frame branch |
| **Still inert** | Mechanism B and C, which need `LocalCopyLtoL` / `LocalCopyBtoLAdrs` bridged (steps 3 and 4); the deform packet builders still have no host bridge at all (step 5) |

The generalisation the later steps need is already visible in the shape above:
the capture is a single slot because mechanism A only ever wants "now". Steps 3
and 4 want captures keyed by GS block address, which is the same change
`MioPan_RendererCaptureScreen()` needs to serve more than the pause screen.

---

## 8. Mechanisms B and C — the capture slot pool

Steps 3 and 4, in `miopan_renderer.cpp` and `g2d_draw.c`. **Not yet run
in-game.**

§7's single scene capture is generalised into a pool of **capture slots**, each
standing for one page of GS memory the game fills by copying the frame buffer
into it and later samples as a texture.

```
LocalCopyLtoL(type, fb, dst)   -> capture live      into slot dst, scpw[type].dw x dh
LocalCopyLtoB(type, no,  fb)   -> capture live      into slot <EE staging addr>
LocalCopyBtoL(type, no, dst)   -> copy slot <EE addr> into slot dst
DispSprD2 with TBP0 = a slot   -> draw from that slot
```

- **A slot is keyed by GS address *and* logical size**, not address alone. The
  game reuses one page for differently-shaped copies: `0x2bc0` is the effects'
  320x448 refraction scratch *and* pause.c's 640x448 screen grab. Keying on the
  pair lets both exist instead of thrashing one slot between two sizes.
- **The slot texture is allocated at `output x logical / 640x448`**, so a
  quarter-size PS2 copy really is quarter-size here and softens the same way
  when the effect stretches it back over the screen. The copy is a scaling
  `SDL_BlitGPUTexture`, not a straight texel copy, because the PS2 copy scales.
- **`CaptureSlotForTex0()` matches `TBW * 64` against the slot's own logical
  width.** That is what keeps `ScreenSaverDraw()` out: it samples `0x2bc0` —
  the same block — as a 256 px `PSMT8` image with its own CLUT, at a time when
  no effect owns the page. Matching the width separates the two without
  hardcoding either.
- **Indices, not pointers, throughout.** Declaring a slot can reallocate the
  vector, so a held `CaptureSlot *` is a use-after-free waiting for the second
  slot to appear.
- **`EffImageHalf32()` is reproduced by geometry, not executed.** Mechanism C
  reads the frame back to the EE, halves it in C, and uploads it again. On the
  host the readback would be a stall for no gain, so the EE-side halving still
  runs (on memory nothing here reads) and the *picture* it produces comes from
  the slot sizes instead: `LocalCopyLtoB` type 0 declares 640x448, the matching
  `LocalCopyBtoL` type 2 declares 320x448, and the blit between them is the
  halving.
- **Only frame-buffer-to-scratch is bridged.** A copy whose *destination* is a
  frame buffer is a blit back onto the screen — pause.c and photo.c do that —
  and the renderer has its own path for it
  (`MioPan_RendererDrawCapturedScreen`). Those are left alone.

The segmentation loop from §7 is now driven by an ordered capture-point list
rather than a flag on a draw, because `LocalCopyLtoL` has no draw of its own to
hang a flag on. Re-verified exhaustively over every combination of queue length
0..8 and up to three capture points at any indices, duplicates included: every
draw replayed exactly once and in order, every capture executed exactly once and
never before its own index, always terminates.

### What this lights up, and what it does not

| | |
|---|---|
| **Works now** | `SubBlur` — the normal in-game blur, the most visible screen effect; `SubNega`; `SetOverRap`'s cross-fade |
| **Slot filled, nothing reads it** | camera flash, ghost's heavy blow (both draw through `EneDmgLargeHitMakePacket`) |
| **Still inert** | six deforms (`MakeScrDeformPacket`), both parts-deforms (`MakePartsDeformPacket`), heat haze (`draw_distortion_particles`'s textured case) |

Worth being blunt about: **step 4 has no visible effect on its own.** Both of
mechanism C's consumers draw through an unbridged packet builder, so the slot it
fills is read by nothing until step 5. It is built because it shares the slot
pool with step 3 and costs a few lines, not because it shows anything yet.

The `SubBlur` result is the one to look at first — it is armed on damage and
uses `0x3aa0`, filled once a frame by `EffectControl()`.

---

## 9. Step 5 — the packet builders

`MioPan_RendererDrawTexturedTriangles2D()` is the shape none of the existing
bridges carried: screen-space triangles with a **UV per vertex**, resolving its
TEX0 through the capture slots. Three builders now convert their own topology
to a triangle list and queue it. **Not yet run in-game.**

| Builder | Topology | UV form | Drives |
|---|---|---|---|
| `MakeScrDeformPacket` | grid tristrip | texels | deform types 5, 6, 7 |
| `SetDeform0` (inline) | grid tristrip | texels | deform types 1, 2 |
| `SetDeform2` (inline) | grid tristrip | texels | deform type 3 |
| `SetDeform3` (inline) | grid tristrip | texels | deform type 4 |
| `draw_distortion_particles` | diamond fan, per particle | normalised | heat haze 2, 3, 4 |
| `MakePartsDeformPacket` | grid tristrip | ST / Q | the 17x17 refracting grid |
| `EneDmgLargeHitMakePacket` | triangle fan | ST / Q | camera flash, ghost's heavy blow |

- **The GS carries texture coordinates two ways and the builders use both.**
  The UV registers are texels, the ST registers are normalised and divided by Q.
  Rather than make every caller convert into a size it would have to ask for,
  the bridge takes a `uv_normalised` flag and does it against the slot's own
  logical size.
- **`vtiw` / `vtw` are GS *window* coordinates**, so the 3D env's XYOFFSET comes
  back off: 1728 = 2048 - 320 and 1824 = 2048 - 224, the 640x448 frame centred
  in the GS's 4096x4096 space. `SetDeform0` writing `j * 640 / 24 + 1728` is
  what settles it.
- **The clip rules are not the same in both builders and both are reproduced.**
  `MakePartsDeformPacket` drops a triangle unless *every* corner is inside the
  guard band; `EneDmgLargeHitMakePacket` drops one only when all three are
  outside. The ROM carries this in the register list (XYZF2 draws, XYZF3 does
  not) rather than by branching.
- **Winding is not preserved and does not need to be.** The second triangle of
  each deform cell comes out in the opposite order from the strip's; sprite
  pipelines never cull, because the GS had no back-face cull at all — VU1 did it
  per mesh.

Verified by construction rather than by eye. Each conversion was checked
against a reconstruction of the ROM's own tristrip and produces the **identical**
triangle set — `MakeScrDeformPacket` 1536, `SetDeform0` 768, `SetDeform2` 1536 —
the parts-deform index arithmetic stays inside its 289-vertex grid, and every
static buffer was sized against its worst case.

One trap in doing that checking: **in a GS tristrip a triangle is kicked by its
third vertex, so ADC suppresses that one triangle only.** A harness that skips
any triangle merely *containing* an ADC vertex under-counts — it reported 1488
against `SetDeform2`'s real 1536 and briefly looked like a conversion bug.

### Notes on the inline deforms

- **`SetDeform0`** — bridged in place, since it builds its packet
  *inline* rather than calling `MakeScrDeformPacket`. Its 17x25 grid is the same
  tristrip shape: verified against a reconstruction of the ROM's strip as the
  identical 768-triangle set, and the `swch` debug ramp (which advances two per
  strip vertex, so `4 * col + 2 * (row - band)` once the strip order is gone)
  reproduces at every shared grid position. It covers deform types 0 and 1.
- **`SetDeform2` is done.** Its 33x25 grid is *projected* — the plane is hung
  2000 units in front of the camera and pushed through `matWorldScreen` — but
  the strip it emits is the same `(i, i + 33)` pair shape `MakePartsDeformPacket`
  uses, and the projection lands in GS window coordinates, so the same
  1728/1824 conversion applies. Two facts make it work: `vtw` is the *deformed*
  projection while `tx`/`ty` are the *undeformed* one (the texture stays put and
  the geometry swings — that is the warp), and **the top vertex of every pair
  gets a fixed `0x80` alpha while only the bottom one gets `alp`**, so a vertex
  emitted twice carries a different alpha in each row band. Both reproduced.
- **`SetDeform3` is done.** The same ripple in *pure screen space* about the
  centre of the frame, over `tx[25][33]` / `vtw[25][33][4]` — the same strip as
  `SetDeform0`, one row wider. Position is `vt + vtw`, and that split is the
  point: `vt` is the flat 2048 screen centre and `vtw` the polar ripple offset
  about it, so the sum is a window coordinate like everywhere else. Its `swch`
  ramp advances **one** per strip vertex where `SetDeform0`'s advances two.
- **The heat haze is done.** `draw_distortion_particles()`'s refracting types
  (2, 3, 4) draw a diamond fan per particle, each vertex sampling the `0x3480`
  copy displaced by `warp_add` — the position does *not* carry that
  displacement, and the difference between the two is the whole refraction.
  Its TEX0 is inherited GS state rather than packet data, so it is rebuilt for
  the bridge from what identifies the slot: TBP0 `0x3480`, TBW 5, PSMCT24.
  The ROM's ST is scaled by 1/1024 and 1/512 against an inherited 512x256 page,
  so the GS reads `texel = pixel / 2` — exactly the half-scale copy type 1
  produced. Normalising over the copy's own 320x224 gives the identical sample
  point without the page size, checked against `ST * 2^TW` across the domain.

**`SubDeform()`'s dispatch is 1-based, and the function names do not match the
types.** Type 1 and 2 both reach `SetDeform0`, 3 reaches `SetDeform2`, 4 reaches
`SetDeform3`, and 5/6/7 reach `SetDeform4`/`5`/`6`. Type 0 does nothing. Worth
knowing before testing a particular one — the two live call sites are
`player.c`'s `CallDeform2(..., 7, 13)` when a ghost grabs the player (type 7 ->
`SetDeform6`) and `EffectScreenEnemyDead()`'s `SetEffects_DEFORM(1, 2, 24, ...)`
on a ghost's death (type 2 -> `SetDeform0`); the latter is gated on
`look_debugmenu`, which `InitEffects()` sets to 1, so it is live.

**Every packet builder in the folder is now bridged.** `SubPartsDeform1` and
`SubPartsDeform2` are covered through `MakePartsDeformPacket`; deforms 4, 5 and
6 through `MakeScrDeformPacket`; 0, 2 and 3 in place.
- **`draw_distortion_particles()`'s textured case** (heat-haze types 2/3/4).
  Its ST comes from each particle's own screen position, so it needs the
  diamond fan converted per particle. The solid path already bridges, and the
  return value the callers use for brightness was always right.

---

## 10. RESOLVED — `PartsDeformClipCheck` cut effects off at close range

**Status: fixed.** The check was innocent; the camera feeding it was wrong.

### The symptom

Most effects rendered correctly at a distance and **cut out abruptly** — not
faded — as the camera approached. Forcing `PartsDeformClipCheck()`
(`effect_obj.c`, ~line 890) to `return 0` made everything work, which pointed at
the Z test:

```c
if ((u_int)(ivec[2] - 1) > 0xffffe)   clip = 1;     /* ivec[2] = z_screen * 16 */
```

`0xfffff == 0xffff * 16`, so the window accepts `z_screen` up to 65535. That is
the ROM's own code and it is correct.

### The cause

`g_CameraDefault.fNearZ` was transcribed as `10.0f`. The ROM holds
`0x3dcccccc` at `0x3b5154` — **`0.1f`**, one ulp low in the usual EE-GCC way
(see `[[ee-gcc-truncates-float-literals]]`). The struct's own comment already
said "near 0.1"; only the literal disagreed.

`g3dCalcViewScreenMatrixPerspective()` makes `fAspectZ` linear in `fNearZ`, and
`z_screen = fAspectZ / z + fCenterZ` — a `1/z` mapping — so a 100x error in the
near plane only ever shows at close range:

```
fNearZ      fAspectZ      fCenterZ    z_screen > 65535 when
0.1f         1677724        -25.60    z <    25.6    (~1 m at the 25:1 scale)
10.0f      167797754      -2560.43    z <  2464.2    (~99 m)
```

The correct reject is about a metre in front of the lens. The transcribed one
was the whole room, so nearly every effect clipped.

`s_Camera` is seeded from `g_CameraDefault` at `gra3d.c:327`, and no gameplay
camera calls `gra3dcamSetClip()` — only `SceneCameraSet()` (cutscenes) and its
restore do — so that one literal set the live near plane for all of ingame.

### The fix, and a second instance of it

- `gra3dConst.c` — `g_CameraDefault.fNearZ` is now `0.1f`. The whole 480-byte
  `GRA3DCAMERA` diffs clean against the ROM out of the compiled `.obj`: one
  expected ulp on `fNearZ`, every other word identical.
- `scene.c:1180` — `SceneCameraSet()` carried the same slip independently:
  `gra3dcamSetClip(10.0f, ...)` where the ROM loads `0x3f199999` from `.sdata`
  via `lwc1 $f12, -0x6fbc($gp)`, i.e. **`0.6f`**. The trailing `/* 0.6f */`
  comment had been right and the code wrong. Fixed; this is the cutscene path.
- A sweep of every source line whose float literal disagrees with its own
  trailing comment found no third case — the remaining hits are all documented
  one-ulp annotations and degree/radian conversions.

### What was ruled out along the way (all still true, keep the fixes)

- **`sceVu0RotTransPers` mode 0 applies `vftoi4` to XYZW.** Verified at ROM
  `0x28a9f0`; the SDK shim already matches. Not the bug.
- **`_gra3dSetCameraForce` dropped `fZmax`.** It copied eight fields by hand and
  stopped at `fZmin` (0x01c); the ROM (`0x1b1ce8`) is a full `0x1e0`-byte
  struct-copy loop. Fixed and correct — but not the cause. `fZmax` really is
  `16777215.0f` (24-bit), confirmed in the ROM.
- The `aprate` distance fade is faithful: far = full, close = 0, deliberate.
- Ghost strength chain, `bep`, `def_type1/2` tables: all measured healthy.

### The depth-precision consequence, and reversed-Z

Correcting `fNearZ` to 0.1 is right, but it costs depth precision: screen depth
goes as `1/z`, so a near plane 100x closer spends 100x more of the buffer on the
first unit in front of the lens. The host was rendering forward-Z ([0,1], near
at 0, `LESS_OR_EQUAL`, clear 1.0), which puts the whole scene up against 1.0
where float32 ulp is 6e-8. Measured resolvable depth separation, fp32 arithmetic
throughout:

```
view z      forward       reversed          gain
    50       0.0012     0.0000024          500x
   500       0.0823     0.0000324         2539x
  5000       2.5461     0.0002950         8631x
 10000      28.6393     0.0003648        78512x
 30000     562.4639     0.0007892       712744x
```

At 10000 units forward-Z could not separate surfaces less than ~1.1 m apart.

The GS ran reversed (larger Z nearer, `ZTST` GEQUAL), so matching it is both
faithful and the standard fix. Three changes:

- **`MioPan_Graph3dApplyCamera()`** rewrites the depth row of the matrix it
  hands the renderer -- `mat[2][2] = -n/(f-n)`, `mat[3][2] = fn/(f-n)` -- giving
  clip z of near -> w, far -> 0. Exactly the two slots
  `g3dCalcViewClipMatrixPerspective` writes, so the downstream convention is
  untouched. `PT_ORTHO` (unused) falls back to the generic `z' = 0.5w - 0.5z`.
- **The 17 matrix-transformed vertex shaders** drop `MikuPanFixClipZ()`. The
  three fed CPU-built clip coords (`sprite`, `untextured_coloured_sprite`,
  `heat_haze`) keep it, and it becomes the conversion `z = 0.5w - 0.5z` for
  whatever still arrives in the *engine's* symmetric clip z -- the 2D GS-Z
  passes (`Gs2dZToNdc()`). The flame and spark billboards
  (`MioPan_RendererDrawTexturedQuadDepth`) and the clip-space triangle, line
  and point bridges no longer go through it: they rebuild z from the clip w
  with the meshes' own depth row and raise `uClipZ.x`, which makes
  `MikuPanFixClipZ()` pass the position through (see below for why).
  A plain UI sprite passes z=0, w=1 and lands on 0.5 either way.
- **`GREATER_OR_EQUAL` + a 0.0 depth clear.**

**For the scene, the reversal has to live in the matrix, not the shader.**
Written per vertex as `0.5*w - 0.5*z` the two terms are both about the view
depth and cancel, so the small result inherits an absolute error of one ulp of
the depth -- precisely the error forward-Z already had. As a fix for scene
depth the one-line shader flip is a no-op. Folded into the projection the same
value comes out of two small coefficients and keeps full relative precision.

That same cancelling subtraction *is* what `MikuPanFixClipZ()` does, and it
was first judged good enough for billboards being depth-tested against the
scene: measured near the world origin the billboard lands 3e-8 low -- 0.007
world units at z=100, 0.37 at z=1000, 40 at z=10000. **It is not good enough
outdoors.** The CPU's clip z and w each carry an ulp of the *world*
coordinates fed through the matrix, and the outdoor maps sit ~10^4 units from
the origin: there the error is 4 / 12 / 33 / 107 units (median; p99 25 / 65 /
152 / 466) at view depths 2k / 5k / 10k / 18k, and it changes whenever the
camera or the particle moves. That flipped hazes, glows and flames in front of
and behind the geometry they overlap, frame to frame. The bridges therefore
rebuild z as `w * P[2][2] + P[3][2]` from the renderer's own projection --
two small terms, nothing to cancel -- which agrees with the meshes to < 0.01
units, and skip the shader conversion via `uClipZ`.

It also cannot go in `g3dCalcViewClipMatrixPerspective()` itself: that matrix
feeds `gra3dVu0ClipFlags()`, whose VU0 CLIP emulation tests z against `+-w`. A
`[0,w]` volume would leave both z bits permanently clear and silently retire the
near/far half of the bounding-box cull.

Reversed-Z earns its precision from the float exponent, so it needs the
`D32_FLOAT` target (already first choice); the renderer now warns if it has to
fall back to a normalised integer depth format, where the gain is nil.

### Build note

The toolchain fails silently — exit 1, no diagnostic — when MinGW's `bin` is
not on `PATH`: `cc1plus.exe` lives in `libexec/` and loads
`libwinpthread-1.dll` / `libssp-0.dll` from `bin`, so it dies with
`0xC0000135` (DLL not found) before it can print anything. CLion sets this up;
a plain shell does not. Prefix the build with:

```
$env:PATH = "<path to mingw>\bin;$env:PATH"   # the bin\ next to CMAKE_CXX_COMPILER
```

### Process notes for whoever picks this up

- **Never filter greps with `-v "/\*"`.** Every reconstructed line carries a
  trailing `/* NNN */` ROM-line annotation, so that filter hides exactly the
  lines you are looking for. It produced two confidently-wrong conclusions this
  session ("16 missing `d_pda` stores" and "nothing writes `bep`"); both were
  present all along.
- **Do not use bash heredocs for content with backslashes** — `\n` loses its
  backslash and breaks string literals. Use the Write/Edit tools.
- **Assert brace balance before scripted deletion.** One removal here matched
  `"    }"` inside an eight-space-indented line and silently broke a function.
