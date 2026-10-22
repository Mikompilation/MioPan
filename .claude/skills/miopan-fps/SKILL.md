---
name: miopan-fps
description: Unlocking the frame rate of the MioPan port of Fatal Frame II — why the game is hard-locked to 30 fps, what is architecturally in the way, the decoupled-present plan that was chosen and why, what has already been built, and the exact next step. Use whenever work touches frame pacing, vfunc/SYSTEM_VBLANK_WAIT_NUM, the V-blank thread in libgraph.cpp, MioPan_RendererEndFrame, camera reprojection or interpolation, MioPan_RendererReprojectDraws, the profiler's workload/deadline-miss figures, or when the user asks about fps, frame rate, 60fps, smoothness, stutter, or frame pacing. Sister skill to miopan-reversing (EE reconstruction) and miopan-iop (IOP side).
---

# Unlocking the frame rate in MioPan

MioPan is a PC port of Fatal Frame II (Crimson Butterfly), Feb 6 2004 PS2
prototype `SLES_523.84`, reconstructed function-by-function from Ghidra output.
**Read `miopan-reversing` first** for the repo layout, build commands, PS2/EE
conventions and the renderer architecture. This skill covers only the frame-rate
work and assumes that background.

The game is hard-locked to **exactly 30.0 fps**. This document is the whole
picture of why, what can be done about it, and where the work currently stands.

Source root:
`zero_rom/zero2np/src/`. All paths below are relative to that root unless
stated otherwise.

---

## Current state

| Increment | Status |
|---|---|
| Two pre-existing pacing bugs | **done** — PAL field rate, dead present-interval default |
| Measurement harness (runtime pacing override) | **done** |
| 1. Camera reprojection primitive | **done**, verified over 200k cases |
| 2. Split `MioPan_RendererEndFrame` into upload / re-callable present | **done** |
| 3. Drive reprojected presents from the frame loop | **done**, verified in-game |
| 4. Interpolated geometry | **built**, maths verified, **not yet judged in-game** |
| Smoothing admission check (`SmoothingFits()`) | **done** — a tick that cannot spare the in-between's field presents once; see Part 10 |
| 5. Display-rate presentation (`[smoothing] mode = display`) | **prototype done**, measured at 240 Hz in-game — Part 11 |

The build is clean and the whole chain runs: menu bar → **Settings → Graphics →
Frame smoothing** presents each logical frame up to four times, on an interpolated
camera and — since increment 4 — through an interpolated world, while the
simulation keeps its 30 Hz tick.  **Present at display rate** in the same menu
(increment 5) replaces that with a presenter thread that presents once per
monitor refresh; the 60 Hz path is what runs whenever it is off.

Increment 4 is built and its maths is verified over 200k randomised cases, but
**whether it reads better has not been judged**; that wants a human at the
controls, comparing **Interpolate motion** on against off with smoothing at 1
extra present.  Note the room-load SIGSEGV is unrelated and pre-existing: a
control run with smoothing off dies in the same place,
`STORY_LOAD_MISSION_EVENT` → `STORY_NORMAL`.

---

## Part 1 — What the lock actually is

Three layers. Only the first one matters.

### The pacing loop

- `system/os/system.c:63` — `static int SYSTEM_VBLANK_WAIT_NUM = 2;`
- `vfunc()` at `system/os/system.c:9733` drains the V-blank semaphore, then
  `WaitSema()`s until that many fields have elapsed.  The wait count is read at
  `:9749`, through the harness override.
- The fields come from a dedicated host thread: `VblankThreadMain` →
  `WaitForVblank` in `sdk/libgraph.cpp`, which calls `v_callback` →
  `iSignalSema(vblank_sema)`.
- 2 fields × 16.67 ms = **33.3 ms**.

Call chain: `main()` → `GPhaseSysMain()` → phase callbacks → `after_super()` →
`SendDMAMain()` (`system.c:9814`) → `vfunc()`.

### Presentation is not a limiter

The present mode is the player's `[window] vsync` setting
(`MioPan_RendererSetVsync()`): `on` (VSYNC, the default), `off` (IMMEDIATE,
falling back to MAILBOX then VSYNC — what the port hard-coded before the
setting existed) or `mailbox`. None of them paces the game: with VSYNC a
present can wait up to one refresh, and that wait comes out of the field wait
the tick does anyway, exactly as the smoothing presents' waits do.
`g_present_interval` is 1, so `ShouldPresentFrame()` always returns true.

The background pause (`[window] pause_in_background`) is the one other thing
that stops the loop: it holds the game thread at the end of
`MioPan_RendererEndFrame()` and stops the V-blank thread with
`MioPan_PacingSetHostHold()`, which restarts its schedule from "now" on release
so no fields bank across the pause.

### Nothing else blocks

`g3dGsSyncPath()` spins on emulated registers that read 0; the `sceGsSyncPath`
shim returns immediately; the one synchronous GPU readback
(`MioPan_RendererReadbackScreen`) explicitly excludes the two per-frame effect
stashes and only fires on pause/photo capture.

### Proof the mechanism runs at 1

`graphics/movie/movie.c:221` calls `SetVBlankWaitNum(1)` for PSS playback and
restores 2 at line 286. The loop already ticks at 60 Hz during movies.

**So `SYSTEM_VBLANK_WAIT_NUM = 1` gives 60 fps in one line, with the whole game
running at double speed.** That is the entire point of everything below.

---

## Part 2 — Why the simulation cannot simply tick faster

**The tick is the unit of time.** One `GPhaseSysMain()` iteration is one
animation step, one physics step, one input sample (`PadSyncCallback()` /
`PadAnalogMain()` in `pre_super`). There is no delta time anywhere in the
engine.

Four forms it takes, each verified:

- **Motion.** `motAddFrame()` (`graphics/motion/motion.c:871`):
  `m_ctrl->inp_cnt += (float)m_ctrl->reso / 100.0f;` — a fixed increment, then
  an integer keyframe advance when it crosses `inp_allcnt`.
- **Movement.** `PlyrPosSet()` (`ingame/plyr/player.c:4911`):
  `sceVu0AddVector(mb->pos, mb->pos, tv)` where `tv` is `plyr_wrk.spd[]`
  directly. Position += velocity per frame. The ghost movers in `enemy_act.c`
  are the same.
- **Timers.** Integer frame counts everywhere, e.g.
  `cam_cng_tm = (GetPALMode() != 0) ? 16 : 20;` (`player.c:390`). Measured: 181
  `x++;` and 57 `x--;` sites on identifiers named cnt/timer/count/tm/frame —
  and that undercounts (it misses `wait_time -= reso`, table-driven waits, and
  the whole `enemy_act.o` opcode interpreter).
- **Input history is frame-indexed.** `PadInfoTmpSave()` rolls a 5-frame
  direction history and a 2-frame rotation history — the about-face/double-tap
  detector. At 60 Hz its wall-clock window halves.

**Scale:** 436 TUs, ~321k lines, 479 functions named
`*Main`/`*Ctrl`/`*Proc`/`*Work`/`*Exec`.

**The ROM's own calibration.** PAL support is implemented as rescaling authored
constants at 64 `GetPALMode()` sites — for a **20%** rate change. And it is
already buggy at that scale: `EJobM08`'s PAL branch computes exactly what its
NTSC branch does (two `.lit4` slots both holding 1.57079625; the 60/50 factor
was never applied). 30 → 60 is a **100%** change over a far larger surface.

---

## Part 3 — The two structural obstacles

These are not fixed by rescaling constants, and any plan has to answer them.

### 1. The draw pass mutates simulation state

`one_Story_Normal()` (`ingame/ingame.c:668`) interleaves both: `PlayerMainCmn()`,
`EnemyMain()`, `IngameCameraMain()` … then `IngameDrawSub()` at line 975. And
`IngameDrawSub()` (`ingame/ingame.c:625`) is itself not pure — `EffectControl(5)`
calls `EneDmgMain()`, `EneHitEffectMain()`, `EffectEndParticleMain()`, and
`FadeMain()` / `CallVibrate()` / `EvDispMain()` / `MisDispTimeProc()` all
advance counters.

**You cannot call the game's draw path twice per sim tick.** Extra frames have
to come from replaying the *renderer's* draw list, never from re-running the
game.

### 2. Frame parity and mid-frame framebuffer capture

68 sites read `sys_wrk.count`, nearly all as `& 1` to pick a GS framebuffer page
(`(count & 1) * 0x1180`). `LocalCopyLtoL` (`graphics/graph2d/g2d_draw.c:1777`)
captures the **live** rendered frame mid-draw via
`MioPan_RendererCaptureGsBlock(…, MIOPAN_GS_CAPTURE_LIVE, …)` so a later draw
*in the same frame* can sample it — refraction, blur, ghost haze, brightness.
That is a read-after-write inside one frame off a parity-alternating source.

**Consequence for reprojection:** a reprojected frame must reuse the capture
rather than retake it. `DrawQueuedSprites()`'s `first`/`last` range exists
precisely to break the render pass at these points (`g_capture_points`).

---

## Part 4 — What makes this tractable anyway

Four facts, all verified in the source. These are why the plan below works.

1. **Vertices are model-space; the transform is a uniform.**
   `resources/shaders/hlsl/mesh.vert.hlsl`:
   `output.position = mul(mvp, input.aPos)`.
2. **`DrawCommand` stores both the baked `mvp[16]` and the source `model[16]`**
   (`miopan/rendering/miopan_renderer.cpp:295`). Re-aiming a draw is one matrix
   product; no vertex work.
3. **The animation system already interpolates sub-frame.** `motInterpAnm()`
   (`graphics/motion/motion.c:2081`) blends `rst0`→`rst1` by
   `rate = inp_cnt / inp_allcnt`. Posing a skeleton between two keys needs no
   new code — this is what makes increment 4 cheap if it is ever wanted.
4. **The ROM already ships a fractional accumulator with a catch-up loop.**
   `graphics/scene/fod.c:266`: `fc->float_now_frame += resolution;` (1.0 NTSC /
   1.19999993 PAL), and `SceneEffectMain()` (`graphics/scene/scene_effect.c:582`)
   runs the request stream once per *integer* frame crossed. That is the
   decoupled shape, already working, in the cutscene subsystem.

5. **The engine has already split every object's motion into two host-side
   places, and which one it uses is decided per block.**
   `GetHostRuntimeMeshTransform()` (`gra3dSGD.c:138`) hands the renderer
   **identity** when the weighted buffer is live — `CalcVertexBuffer()` has
   folded the bone pair into every vertex, so the geometry arrives in *world*
   space — and the block's own `matLocalWorld` otherwise, with vertices
   constant in that space. So a skinned block's motion is entirely in its
   vertices and a rigid block's entirely in its matrix. That is what makes
   increment 4 possible without re-running any game code.

**Counter-fact:** skinned character positions are streamed from the CPU every
frame. `CalcVertexBuffer()` (`graphics/graph3d/gra3dSGD.c:977`) does the
two-bone weighted blend on the CPU, reproducing the VU1. The GPU-skinning
shaders (`resources/shaders/hlsl/mesh_0x2_skinned.vert.hlsl`, `uBonePalette`)
are inherited from MikuPan and **are not wired into this renderer** —
`uBonePalette` appears nowhere in `miopan_renderer.cpp`.

**And the mesh cache is not the live path.** `MioPan_RendererDrawIndexedMesh()`
— the old cache's arenas, the `MESH_PIPELINE_CACHED_ANIMATED` layout, the
`AnimatedMeshVertex` stream — **has no caller anywhere in the tree**. Two paths
are live instead:

- **Preset meshes (rooms, furniture, doors) are resident.**
  `MioPan_Graph3dDrawPresetMesh()` decodes a whole model once, on its first draw,
  into one GPU mesh (`FindResidentPresetUnit()` / `BuildResidentPresetModel()`,
  miopan_graph3d.cpp), and each unit's draw goes through
  `MioPan_RendererDrawResidentMesh()`: `resident_mesh` set, drawn through the
  `MESH_PIPELINE_CACHED_STATIC` layout, only its colours in `g_mesh_colours`.
  Consecutive units with identical state are folded into one draw. Their
  vertices never enter `g_vertices`, so **their motion is all in `model`** —
  the geometry key uses the index span plus the mesh (`GeometryDrawKey::mesh`),
  and `ApplyGeometryVertexBlend()` skips them.
- **Everything else streams** through `MioPan_RendererBeginMeshStream()`, which
  pushes `SpriteVertex`es into `g_vertices` and stores `model` and `mvp` on the
  draw: characters, items, anything skinned — and any preset unit the resident
  path declines (resident meshes off, a model whose UVs scroll every frame).

---

## Part 5 — The options, and which was chosen

| Option | What | Verdict |
|---|---|---|
| 0 | Flip `SYSTEM_VBLANK_WAIT_NUM` to 1 | Measurement harness only — game runs 2× speed |
| 1 | Rescale everything to a 60 Hz sim | **Rejected** |
| 2 | Sim stays 30, present at N with camera reprojection | **Chosen** |
| 3 | Option 2 + interpolated skeletons | Later, optional |

**Option 1 was rejected on decompilation grounds, not performance.** It rewrites
timing constants across ~436 TUs and permanently forks the reconstruction away
from the ROM. In a matching decomp that is unrecoverable damage. It also changes
collision behaviour (`MapHitCheck` is a swept test over `bpos`→`pos`, so halving
the step changes penetration) and reschedules `enemy_act.o`'s `wait_time -= reso`
machine.

**Option 2 touches zero game code.** The sim keeps ticking at exactly 30, every
reconstructed body stays byte-faithful, and all the work lives in the host layer.

**Decision on camera policy (user, explicit): interpolate, not extrapolate.**
Extrapolation overshoots and snaps back exactly where this game moves the camera
fastest (turning in finder mode, cuts between fixed angles).

### Standing question — settled: no lag

Interpolation was chosen alongside accepting a one-tick (33 ms) input lag. While
building increment 1 it emerged that **the lag is not actually required**,
because only the camera is being interpolated:

- *with lag:* `[geom N-1, cam blend]`, then `[geom N, cam N]`
- *without lag:* `[geom N, cam blend]`, then `[geom N, cam N]`

Identical camera paths, identical 30 Hz geometry (blending geometry is
increment 4, not this). The no-lag version's geometry is half a tick *early*
instead of a full tick *stale*, costs no input latency, and needs **no draw-list
retention** — which was going to be the expensive part of increment 3.

The user picked the lag version explicitly before this was known; asked again at
the start of increment 3, with the added fact that increment 2's repeat present
replays *the current frame's* buffers and so supports no-lag directly while the
lagged version needs a draw-list retention mechanism that does not exist, they
chose **no lag**.  That is what is built.

---

## Part 6 — What is already built

### Two pre-existing bugs, fixed

**PAL ran 20% fast on the host.** `WaitFor60HzVblank` was hard-coded to 1/60
regardless of video mode, while `GetPALMode()` still divided every authored
frame count by 1.2 across 64 sites and `fod.c` still stepped
`float_now_frame` by 1.19999993. Fixed in `sdk/libgraph.cpp`: the field period
is now an atomic `s_vblank_period_ns`, set by `sceGsResetGraph()` from its
`omode` argument (1/50 for `SCE_GS_PAL`, else 1/60) — which is where the real
library gets it, and `SetGsResetGraph()` (`system.c:9660`) was already passing
the right standard. `WaitFor60HzVblank` is now `WaitForVblank` and re-reads the
period every field, so `ChangeVideoMode()` picks it up.

**`g_present_interval` defaulted to 2 and was dead.** Both `InitVBlank()` and
`SetVBlankWaitNum()` force it to 1 before the first frame. Default is now 1
(`miopan_renderer.cpp:738`). The skip branch is kept — it is the mechanism
increment 3 would use — but note it is **still never exercised at runtime**.

**Adjacent fix:** `SetVBlankWaitNum()` (`system.c:9356`) now returns early when
the value has not changed. It calls `MioPan_RendererSetPresentInterval(1)`,
which zeroes the FPS window and calls `MioPan_ProfilerReset()`, and
`PlayMovie()` was calling it every frame of a movie with the same `1` — so the
profiler was reset once per frame throughout every movie.

### Measurement harness

`miopan/os/miopan_pacing.{h,cpp}` — a host override for the per-frame field
wait, clamped 1..8, `0` meaning "follow the game".

- `vfunc()` reads `MioPan_ResolveVBlankWait(SYSTEM_VBLANK_WAIT_NUM)`, checked
  per frame rather than latched (movies rewrite the game's value every frame).
- UI: menu bar → **Debug → Frame pacing**. Not persisted to `miopan.ini` —
  it is a harness, and a config file that silently double-speeds the game would
  be a trap.
- The existing `Pacing target %d VBlanks` line in the summary reads the resolved
  value, so it confirms the override took.

### Increment 1 — camera reprojection primitive

Three functions, declared at `miopan/rendering/miopan_renderer.h:389`:

```c
int MioPan_RendererHavePreviousCamera(void);
int MioPan_RendererBlendCameraFromPrevious(float t, float *view, float *projection);
int MioPan_RendererReprojectDraws(const float *view, const float *projection);
```

`MioPan_RendererBeginFrame()` (`miopan_renderer.cpp:6773`) latches the outgoing
camera into `g_prev_3d_view` / `g_prev_3d_projection` before the game installs
the new one. Shadow rendering swaps the camera mid-frame but always restores it,
so what is live at that point is the main eye.

`MioPan_RendererReprojectDraws()` (`miopan_renderer.cpp:9057`) calls
`MioPan_RendererSet3DViewProjection()` (which also refreshes the uniform-block
copies the shaders read) and then rewrites `draw.mvp` for each eligible draw.

**The eligibility rule needed no new flag, and this is worth not re-deriving:**
`transform_mesh` is set at exactly two sites — the mesh-cache queue path (which
has no callers, see Part 4) and `MioPan_RendererBeginMeshStream()`, which is the
one every 3D mesh actually goes through — and **both also store `model`**. Every
`QueueTriangleList` caller passes `mvp == nullptr` and is screen-space. So
`transform_mesh` already means precisely "has a model matrix built against the
main camera". Shadow casters are additionally skipped: their mvp is
light-relative and their pixels belong to a shadow map a reprojected frame
reuses rather than re-renders.

The composition order that matters:
`MulMatrixRowMajor(command.mvp, local_world, g_3d_view_projection)` and
`command.model = local_world`. So **mvp = model × view_projection**, row-major,
in that operand order.

---

### Increment 2 — the upload / present split

`MioPan_RendererEndFrame()` is 55 lines. The 608 came out as four file-static
stages in the anonymous namespace, in call order:

| Stage | What it owns |
|---|---|
| `RecordCopyOnlyUploads(cmd, label)` | The bounded copy-only warm batch. Was written out twice — once for a frame the present interval skips, once for a frame that missed the swapchain — and the two differed only in the string a submit failure logs. |
| `UploadFrameResources(cmd, …)` | `ApplyOriginalAspectToVertices`, textures, `UploadMeshData`, the fallback-entry gather, `ConvertMeshDrawsToStreamed`, `UploadVertexBuffer`. **Everything that writes a GPU buffer or mutates `g_draws`.** |
| `RecordFramePasses(cmd, …, repeat, …)` | Render size, targets, shadow pass, the capture-split scene loop, pause/mirror stills, the present blit, the UI pass. Read-only over the draw queue. |
| `PresentFrame(repeat)` | Acquire, swapchain acquire, the miss branch, the two above, submit, commit, transfer release. Returns whether a frame reached the swapchain. |

`MioPan_RendererEndFrame()` now reads: prologue → copy-only branch →
`PresentFrame(false)` → the once-per-logical-frame teardown, which is unchanged
and still runs exactly once.

**The three readiness flags outlive the upload stage.** `vertices_uploaded`,
`colours_ready` and `animated_vertices_ready` were locals that the record stage
took as arguments; they are now `g_frame_upload`, a four-field struct beside
`g_frame_active`, cleared in `MioPan_RendererBeginFrame()` next to
`g_draws.clear()`. Its `valid` field is what says an upload stage has run for
the frame currently queued.

**A `repeat` present skips exactly four things**, each for its own reason:

- **The whole upload stage.** Not just cost: `ApplyOriginalAspectToVertices()`
  scales in place, so a second pass shrinks every screen-space draw twice, and
  `ConvertMeshDrawsToStreamed()` rewrites `g_draws`.
- **`RecordShadowMapPass()`**, and `g_shadow_valid` with it. The map is
  light-relative and the light did not move.
- **The GS block captures**, which collapses the scene back to a single pass
  segment (`capture_count` is forced to 0, so the two-cursor loop runs once).
  The slots already hold this frame's composite taken at the right draw index.
  *Known imprecision:* a slot captured twice in one frame keeps only its last
  capture, which a repeated present's earlier draws then sample. Accepted — the
  frames that repeat are in-betweens.
- **`RecordPauseScreenCapture` / `RecordScreenMirror`.** Those are the game's
  own frame; retaking them from an in-between hands the game a picture it never
  simulated.

`texture_upload` and `mesh_upload` are left default-constructed on a repeat,
which makes `CommitTextureUploads`, `CommitMeshUploads` and both transfer
release loops no-ops without a second copy of the submit tail. **A repeat
present allocates no transfer buffer at all.** The pins still go through
`BuildMeshSubmissionPins(…, include_draw_entries = true, …)`, so the arena
ranges the replayed draws reference stay pinned for the second submission too.

Public entry point, declared beside the reprojection trio:

```c
int MioPan_RendererRepeatPresent(void);
```

It requires `g_frame_upload.valid`, calls `PresentFrame(true)`, and on success
bumps `g_fps_present_frames` only — the rest of the stats block describes the
logical frame and is still published once, by `EndFrame`.

### The ImGui constraint, resolved

`MioPanUi::RenderDrawData()` used to clear `g_frame_rendered`, which made the
overlay a one-shot per `ImGui::Render()` — a repeated present would have
inherited a UI that flashed at the logical rate. The latch is now left set and
cleared only by `MioPanUi::BeginFrame()`, once per logical frame. That is safe
in both directions: `ImGui::Render()` freezes the draw data until the next
`NewFrame()`, and the SDL_GPU3 backend's `PrepareDrawData` re-uploads it from
scratch into its persistent `MainWindowFrameData` (with `cycle = true`), so
calling the pair again is idempotent.

### The identity check

Increment 3's menu keeps increment 2's harness as a checkbox: **Profiler →
Frame smoothing → Interpolate camera**, off. The extras then present the same
draw list through the same camera, and every one must be pixel-identical to the
real present.

**It reports itself.** The profiler summary's first line carries
`present/frame` next to the two FPS figures, so the ratio going 1.00 → 2.00 is
visible with nothing to set up. `Renderer upload` on the timing lines must not
move with it: a repeat re-records, it does not re-upload. That pair — ratio up,
upload flat — is the whole check.

Measured over ~500 frames each, boot to language select:

```
off      work  5.40 | rupload  0.40  rrecord  0.07  racquire  0.02  rsubmit  0.34
1 extra  work  3.98 | rupload  0.40  rrecord  0.10  racquire  0.03  rsubmit  0.60
```

`rupload` **identical**; record, acquire and submit all rose. No SDL_GPU
validation errors, nothing on stderr, no crash.


### Increment 3 — decoupled presents in the frame loop

**Menu bar → Profiler → Frame smoothing**: Off / 1 / 2 / 3 extra presents, plus
an `Interpolate camera` toggle. Default off, and persisted to `miopan.ini` under
`[smoothing]` — unlike Frame pacing, which stays a harness because it changes
game speed. This does not: the tick is unchanged, so it is an ordinary setting.

**The lag question was settled: no lag** (user, explicit, after being shown that
increment 2 supports it directly and the lagged version does not). Part 5's
standing question is closed.

#### 60 Hz is the ceiling, and why

**Only `wait_num - 1` fields can be spent on smoothing, so at the game's normal
two-field tick exactly one extra present fits.** After draining the banked
fields, `vfunc()` performs a final `WaitSema()` unconditionally:

```c
remain = (wait_num - count) - 1;
if (0 < remain) { ...wait remain times... }
WaitSema(vblank_sema);          /* always */
```

That last field cannot be borrowed. Asking for more does not raise the frame
rate — the tick grows by the fields that could not be borrowed, which is the
simulation slowing down. Measured, before the clamp existed:

```
extra=1   paced 33.34 ms -> 30.0 Hz sim
extra=2   paced 50.00 ms -> 20.0 Hz sim   (same 60 Hz output, slower game)
```

`MioPan_PacingLastVBlankWait()` publishes the resolved count and
`PresentLogicalFrame()` clamps to it, so an over-large setting degrades to the
budget instead of slowing the game; the menu shows the rows above it disabled.
A pacing-harness setting of 1 field leaves a budget of 0 and disables smoothing
entirely, which is correct — there are no spare fields at 60 Hz simulation.

#### Where the extra fields come from

`vfunc()` does not sleep for a fixed time. It *drains* `vblank_sema`, counts how
many fields went by while the game was busy, and waits only for the remainder:

```c
while (PollSema(vblank_sema) != -1) count++;
remain = (wait_num - count) - 1;
```

So a field that elapses anywhere else in the tick is one `vfunc()` finds already
banked and does not wait for again. That is the whole trick: the renderer spends
a field between two presents, `vfunc()` waits one less, **the tick still takes
exactly `wait_num` fields and the simulation does not speed up.** Measured:
`paced` is 33.34 ms with smoothing off and 33.33 ms with it on.

It only works because the wait is on a *separate* signal.
`MioPan_PacingWaitField()` (`miopan/os/miopan_pacing.cpp`) blocks on its own
semaphore, posted by the same V-blank thread from `VblankThreadMain()`
alongside the game's callback. **Consuming `vblank_sema` instead would make
`vfunc()`'s drain come up short and it would wait a full `wait_num` on top —
three fields per tick, a 20 Hz simulation.** The semaphore is constructed at
static-init time, before `EnsureVblankThread()` can start the thread, so the
signaller and the waiter never race to create it; a wait before any field has
ever been signalled returns immediately rather than hanging.

#### The order, and why it is that way

`PresentLogicalFrame()` (`miopan_renderer.cpp`, end of the anonymous namespace)
presents `1 + extra` times. **The in-betweens go first, the frame's own camera
last.** At this point in the tick the only cameras that exist are N-1 and N —
there is nothing to interpolate *toward* past N — so an in-between can only sit
between them and must be shown before N to be in order.

```
present i (i = 0 .. total-1):  camera = blend(N-1 -> N, t = (i+1)/total)
                               i == 0 uploads; the rest are repeats
                               one field of wait between presents
```

The geometry is identical in all of them — posed for tick N throughout. An
in-between shows tick N's world *half a tick early* rather than tick N-1's a
full tick late. Same camera path as the lagged version, no input latency, and no
draw-list retention: the repeats replay the buffers this frame's upload stage
filled, which is exactly what increment 2 built.

Two invariants the loop exists to maintain:

- **The last blend is `t = 1`.** `BlendViewMatrix` short-circuits that to a
  memcpy, and the projection lerp now does too — `a + (b - a) * 1.0f` is not
  exactly `b` in float, and the present that lands on a simulation tick has to
  be bit-identical to the undecoupled one or the seam shows every tick.
- **`t = 1` last also leaves the true camera installed**, which is what the next
  `MioPan_RendererBeginFrame()` latches as "previous". An in-between must never
  become the frame a later blend is measured from.

The true camera is held aside in locals before the loop, because the first
reprojection overwrites the live one. `BlendCameraBetween()` is the blend
against explicit endpoints; `MioPan_RendererBlendCameraFromPrevious()` is now a
thin wrapper on it, so there is still one implementation of the verified maths.

The shadow map is recorded during present 0, with an in-between camera live.
That is fine and checked: casters' mvps are light-relative and reprojection
skips them, and `MioPan_RendererSet3DViewProjection()` refreshes only
`view`/`projection`/`viewProj` in the uniform block — nothing shadow-related.

#### The profiler had to be corrected for it

The field wait sits between `MioPan_ProfilerBeginRenderer()` and
`MioPan_ProfilerEndFrame()`, so it was being charged to `renderer_ms` —
`workload` read 19.9 ms for a frame doing 1.2 ms of work, which would have made
Part 9's budget meaningless. `MioPan_ProfilerRecordPresentWait()` now subtracts
it, exactly as `vblank_wait_ticks` is subtracted from the game's half, and the
stutter log grew a `pwait` field.

```
off            paced 33.34  work 5.40 | rupload 0.40  rrecord 0.07  rsubmit 0.34  pwait  0.00
smoothing x2   paced 33.33  work 3.97 | rupload 0.34  rrecord 0.13  rsubmit 0.72  pwait 15.95
```

`paced` unchanged, `pwait` one field, `rupload` flat, record and submit up.

Zero ROM source was touched. `SendDMAMain()` and `vfunc()` are exactly as they
were; the whole increment lives in the host layer, which is the reason Option 2
was chosen in the first place.

#### Driven in-game: clean

Run with smoothing on, 2026-08-23, including with the camera moving: no
shearing, no breathing, no jolt at a cut, no seam. The camera half of the
frame-rate work is verified.

One thing that had to be explained before it could be judged: the first run
showed no visible difference at all, which was expected rather than a fault.
Fatal Frame II is mostly fixed camera angles, and a camera that did not move
between two ticks makes `blend(N-1, N, t)` equal to N for every t — so every
in-between is byte-identical to the real frame. **Only a moving camera can show
smoothing**: finder mode, and scripted pans.

So the summary line reports the camera's own motion beside the FPS figures,
which is what says whether a given moment is even testing anything:

```
Game 30.0 FPS | Present 60.0 FPS | present/frame 2.00 | cam 12.4/tick
```

- `present/frame` 1.00 → smoothing is not on.
- `cam --` → interpolation is off, or there is no camera pair yet.
- `cam 0.0` with `present/frame` 2.00 → **working, static camera, nothing to
  see.** Not a fault.
- `cam` > 0 with `present/frame` 2.00 → actively interpolating; this is where
  to judge it.


## Part 7 — The camera-blend math (do not "simplify" it)

All matrices in the renderer are row-major in **row-vector convention**
(`p' = p * M`, see `ApplyMatrixRowVector` at `miopan_renderer.cpp:983`): the
rotation is the upper-left 3×3 with basis vectors in **rows**, translation is
row 3.

Two traps, both handled in `BlendViewMatrix` (`miopan_renderer.cpp:1144`):

- **A view matrix must not be blended element-wise.** The average of two
  rotation matrices is not a rotation — it shears and shrinks, which on a
  turning camera reads as the world breathing. Orientation goes through a
  quaternion slerp.
- **Row 3 of a view matrix is `-eye * R`, not the eye.** Lerping it directly
  swings the camera along an arc around the world origin. `ViewMatrixEye()`
  recovers `eye[j] = -(row j of R) · t` first, lerps that, then recomposes.

**The eye recovery and recomposition are in `double` deliberately.** A room
camera sits a few thousand units out, where float carries ~2.4e-4 of absolute
resolution, and the value is fed straight back through the same product. In
float the round trip lost ~1e-3 units even at `t == 0`. It runs once per
presented frame, not per draw, so the width is free.

`BlendViewMatrix` short-circuits `t <= 0` and `t >= 1` to a `memcpy`. That is
not an optimisation: **a presented frame landing on a simulation tick must be
bit-identical to the undecoupled one, or the seam shows every 33 ms.**

`RotationRowsToQuat` and `QuatToRotationRows` are a matched pair — the sign
conventions of one only make sense against the other. If either is edited, both
must be, and the harness re-run.

### Verification harness

`tools/renderer/camblend_test.cpp` copies the four functions verbatim and drives
them over 200k randomised camera pairs. If either file changes, re-copy and
re-run.

```bash
export PATH="/path/to/mingw/bin:$PATH"   # the bin/ next to CMAKE_CXX_COMPILER
cd tools/renderer
g++ -O2 -o camblend_test.exe camblend_test.cpp && ./camblend_test.exe
```

Results as of increment 1:

```
cases                       200000
quat round-trip  max err    5.364e-07
blend t=0 vs A   max err    0.000e+00
blend t=1 vs B   max err    0.000e+00
orthonormality   max err    6.990e-07
determinant-1    max err    1.412e-06
eye lerp (rel)   max err    6.574e-04
point xform t=0  max err    0.000e+00
```

The `eye lerp` residual is absolute float noise from 4000-unit intermediates —
the metric normalises by `1+|want|`, so it peaks where the true value is near
zero. Sub-milli-unit against a ~50-unit character.

---

## Part 8 — What is left

### The camera half is finished

Increments 1–3 are done and verified in-game. 60 fps of camera motion out of an
untouched 30 Hz simulation, one setting, zero ROM source changed.

**60 Hz is the ceiling of the field loop** — not of the approach but of
`vfunc()`'s unconditional last `WaitSema()`; see Part 6.  Increment 5 gets past
it without touching `vfunc()`: the presents leave the game thread altogether,
so there is no field to borrow and no ceiling but the display's.  See Part 11.

### The cut guard — done

Seen by the user (2026-10-21): with smoothing on, a camera cut showed one
present of wrong geometry. `g_prev` and the new camera are unrelated across a
cut, so the in-between was a view nobody set up, with the world dragged through
it by the motion blend. Before the game's first camera the renderer holds an
**all-zero view matrix**, finite and "valid", so the first room frame after a
load blended from that too.

`CameraPairIsCut()` (beside `BlendModelMatrix`) calls a cut when either view's
rotation block is not a rotation (`ViewRotationIsRigid()` — this is what catches
the zero matrix), the eye moved more than `kSmoothingCutMove` (300 units), the
orientation turned more than 30°, or either focal scale changed more than 15%.
`PresentLogicalFrame()` then presents the tick's own frame every time — no
camera blend and so no geometry blend — while keeping the presents and their
field waits, so pacing does not hitch, and the true camera still becomes the
next tick's previous. Separately, `ModelJumped()` (in `ReprojectDrawsAt()`) and
a per-draw check in `ApplyGeometryVertexBlend()` leave unblended any object
that moved more than `kGeometryMaxTickJump` (200 units) in one tick.

The thresholds come from a per-tick log of driven runs in chapters 1 and 3:
following Mio at a walk the camera moved ≤ 10.5 units and ≤ 0.4° a tick; the
real cuts moved 1599–4521 units and turned 58–173°; the fastest object moved 53
units a tick. With the guard on, every cut tick and nothing else was flagged.
**Not measured: finder turning and zoom** — scripted input could not raise the
finder in either chapter. A false positive only costs one tick unsmoothed.

The summary line now reads `… | cam 12.4/tick | cuts 3 | geo 96%`. `cuts`
counts cut ticks since startup, only when there is a real camera to cut to; it
should step once per visible cut and never while the camera merely moves.

(Increment 4's `BlendModelMatrix` still carries its own >90° rotation guard;
`ModelJumped()` is kept outside it so `tools/renderer/modelblend_test.cpp`'s
verbatim copy stays valid.)

### Increment 4 — interpolated geometry

**Menu bar → Profiler → Frame smoothing → Interpolate motion**, persisted to
`miopan.ini` as `[smoothing] interpolate_motion`. It rides on `Interpolate
camera` — a world that moves under an eye that does not would be worse than
either — and it needs a previous frame whose draw list still corresponds.

**It is not skeleton interpolation, and that was the whole finding.** Posing a
skeleton in between would mean re-running `motInterpAnm()` and then the CPU
skinning, i.e. re-running game code, which Part 3 rules out. It is not needed,
because by the time geometry reaches the renderer the ROM has already split
every object's motion into exactly two places, and both are host-side data:

| | where the motion is | what blending it costs |
|---|---|---|
| rigid block | the per-draw `model` matrix | one matrix blend, no upload |
| skinned block | the streamed vertex positions | one vertex-buffer upload per present |

`GetHostRuntimeMeshTransform()` (`gra3dSGD.c:138`) is what says which:
**a block whose weighted buffer is live gets an identity `model` and world-space
vertices**, because `CalcVertexBuffer()` has already folded its bone pair into
every vertex; every other block gets its bone's `matLocalWorld` and vertices
that are constant in that space. So blending the matrix moves the first kind and
blending the vertices moves the second, and the two sets do not overlap.

Blending skinned positions linearly is *not* the same as blending the pose, but
the difference is the classic candy-wrapper term and it scales with the
per-step rotation — which at 30 Hz, over half a tick, is small.

#### Correspondence is the whole safety argument

`g_draws` is rebuilt from scratch every frame and says nothing about which draw
was which last time, so both halves need that established first. Draw `k` is
paired with draw `k` of the previous frame while the keys match, and the match
**stops at the first mismatch rather than resynchronising** — pairing past a
divergence is how one object's geometry would get blended toward another's.

The key is `(first_vertex, vertex_count, texture, flags)`. `first_vertex` is
what makes it safe: any insertion, removal or size change shifts it for every
draw after it, so the prefix ends there rather than sliding by one. Room and
characters are built before effects, so the churn that ends the prefix is
usually behind them. Everything past it keeps this frame's own data, which is
increment 3's behaviour — the failure mode is invisible, not wrong.

**The `geo` figure in the profiler summary is the share of 3D draws that
paired**, and it is what says whether this is doing anything:

```
Game 30.0 FPS | Present 60.0 FPS | present/frame 2.00 | cam 12.4/tick | cuts 3 | geo 96%
```

`geo --` is off or no corresponding previous frame; a low figure means the draw
list keeps changing shape, not that anything is broken.

#### The pieces

- **`BlendModelMatrix()`** — *not* `BlendViewMatrix`, and it cannot be. Row 3
  of a local→world matrix is the position, not `-eye * R`, so the translation
  lerps directly; and the 3×3 can carry **scale**, which a camera's never does.
  Rows split into length and direction, the directions slerp, the lengths lerp.
  It **returns false** — and the caller then uses the frame's own matrix — for a
  degenerate row, a mirrored basis, a non-affine matrix, or more than 90° in one
  tick, which is a cut rather than motion.
- **`CaptureGeometrySnapshot()` / `FinishGeometrySnapshot()`** — once per
  logical frame, before any present, because the model blend feeds the very
  first reprojection. Ping-ponged rather than copied.
- **`ApplyGeometryVertexBlend(t)`** — once per *present*, writing into
  `g_vertices` in place, immediately before the vertex upload. `t >= 1` restores
  the frame's own positions out of the snapshot rather than blending to 1, for
  the same reason `BlendViewMatrix` short-circuits its endpoints.
- **`ReprojectDrawsAt(view, projection, model_t)`** — the reprojection with the
  model blend folded in. `MioPan_RendererReprojectDraws()` is now this with
  `model_t = -1`.
- **`PresentFrame(repeat, geometry_t)`** — `geometry_t >= 0` is the one thing a
  repeat can still owe: a vertex-only upload. Everything else about a repeat is
  unchanged.

#### What it costs, and the invariant that changed

**`Renderer upload` now rises with `present/frame`.** Every present needs its
own copy of the vertex buffer — the in-betweens blended, the last one the
frame's own — so increment 2 and 3's check that "`rupload` stays flat while the
ratio doubles" **only holds with Interpolate motion off**. That is still the
right check for the camera half; it is not a fault here.

The blend is charged to `MIOPAN_PROFILE_RENDERER_UPLOAD` deliberately, rather
than getting a phase of its own: it exists to feed that upload and sits inside
it.

#### Verified

`tools/renderer/modelblend_test.cpp`, the sibling of `camblend_test.cpp`, copies
the functions verbatim and drives them over 200k randomised transform pairs.
Same rule: if either file changes, re-copy and re-run.

```bash
export PATH="/path/to/mingw/bin:$PATH"   # the bin/ next to CMAKE_CXX_COMPILER
cd tools/renderer
g++ -O2 -o modelblend_test.exe modelblend_test.cpp && ./modelblend_test.exe
```

```
blend t=0 vs A   max err    0.000e+00      rotation vs slerp max err   7.749e-07
blend t=1 vs B   max err    0.000e+00      scale lerp (rel) max err    7.948e-07
point xform t=0  max err    0.000e+00      translation lerp max err    5.378e-07
wrongly accepted            0
rejected: cut 200000  shear 199877  mirror 200000  degenerate 200000  projective 200000
```

The shear count is deliberately **not** the case count: those matrices are built
rotate-then-scale, and when the rotation is near axis-aligned that genuinely is
the same matrix as scale-then-rotate, so accepting one is correct. What must
hold is agreement with the harness's own independent `IsDecomposable()`, which
is the `wrongly accepted 0`.

#### What is left on it

**Judging it.** Turn smoothing to 1 extra present and toggle `Interpolate
motion` while something is moving — a ghost, Mio walking, a fluttering charm.
Watch `geo` first: if it is low, the draw list is churning and there is nothing
to judge yet.

## Part 9 — Measuring

High CPU is a known, separate workstream — **do not gate the architecture on
it** (user, explicit). But it bounds the payoff, since each reprojected frame
costs `renderer_ms`.

The profiler split falls exactly on the boundary that matters, because
`MioPan_ProfilerBeginRenderer()` is called at the top of `EndFrame`:

- **`game_cpu_ms`** = BeginFrame → EndFrame minus the V-blank wait — game logic
  *plus* the whole draw-list build (SGD walk, mesh decode, CPU lighting, CPU
  skinning). Per **simulation tick**.
- **`renderer_ms`** = upload, record, acquire, submit. Per **presented frame**.

| Option | Requirement |
|---|---|
| 60 Hz sim | `workload_ms ≤ 16.7` |
| Sim 30 / present 60 | `game_cpu_ms + repeat present ≤ 16.7` — one field, not two (Part 10, first hazard) |
| Either | `vblank_deadline_misses == 0` |

Overlay: menu bar → **Debug → Summary**, or **Summary → FPS only** for
just its first line — `Game … | Present … | present/frame … | cam … | geo …` —
when the frame
rate is the only question and the rest of the block is in the way. For worst frames rather than
averages, `MIOPAN_STUTTER_MS=15` appends every frame over threshold to
`miopan_stutter.log` with the full phase split (`sgd`, `meshcpu`, `meshlit`,
`meshskin`, `texmiss`, `gsupload`, `rupload`, `rrecord`, `racquire`, `rsubmit`,
`gpuidle`).

Measure in a room with ghosts and furniture, and separately in finder mode —
they stress different halves of the split.

### Debug builds: the cost is per element, not per draw

Measured 2026-10-21 on the MSVC x64-Debug build (`/Od /RTC1`,
`_ITERATOR_DEBUG_LEVEL=2`) with one extra present and `spotlights =
fragment-all`, on chapter 0's rope bridge — room 9, event 1819, the shot across
the bridge to the village, the one that missed 30 fps. About 1,077 draws, and
`rrecord + rsubmit` was ~2 ms of it. **Draw count was never the problem**:
restitching model parts into fewer draws (MikuPan's sgdstitch) could have
bought those 2 ms at most.

A sampling profile put the time in per-vertex host code that `/Od` turns into
calls:

- **`push_back` per element.** At iterator-debug level 2 each one runs
  `_Orphan_range`, which takes the global `_Lockit` — ~5% of the frame was
  lock traffic alone.
- **`std::isfinite`, `std::min`, `std::max`, `Clamp()` per colour component**,
  each a real call.
- **Shadow receivers were lit for nothing.** `gra3dShadow.c` walks every
  receiver block a second time, and the preset path ran the full colour pass on
  it again — 47% of all preset strips — for `shadow_receiver.vert`, which reads
  only the position and `model`.

The fixes: the colour path is decided once per unit (`BeginPresetColorPass()`)
and each strip is written in place into storage sized once
(`WritePresetStripColors()`; the same for `MioPan_RendererDrawResidentMesh()`'s
colours and `MioPan_RendererAppendIndexedMesh()`'s vertices and indices); the
clamps are spelled with operators (`ClampMeshColour()`, `Ps2ColorFloat()`:
`v - v == 0` is `isfinite`, checked bit-identical over all 2³² floats at `/Od`
and `/O2`); and a receiver skips its colours and its fragment light state
(`MioPan_RendererShadowReceiverActive()`). An in-game check ran the old colour
code beside the new for every strip, in all three lighting modes: no mismatch
in 4.2 M (fragment-all) and 3.6 M (vertex, fragment) strips.

Bridge frame work went from **29.3 / 34.1 ms** (median / p99) to
**15.3 / 19.3 ms** — and the game was *still* not at 30, because of frame
smoothing's budget (Part 10, the first hazard): with one extra present the
game's work has one field, not two. `SmoothingFits()` fixed that by presenting
a tick once when its successor's work would not fit.

Then the per-frame transfer buffers: the vertex, colour and animated streams
each created an upload transfer buffer every frame and released it after the
submit — steady `NtGdiDdDDICreateAllocation` / `DestroyAllocation2` traffic and,
every few hundred ticks, a 12–46 ms `rupload` or `rsubmit` stall.
`MapStreamTransfer()` keeps one per stream and maps it with cycle set; the
stalls went to zero and `rupload` from ~1 ms to 0.1 ms.

Game FPS over 900 in-room ticks, Debug, one extra present, fragment-all:

| Room | Before | After | Missed fields after |
|---|---|---|---|
| Forest start (65) | 20.00 | 30.00 | 0 |
| Torii (0, ev1550) | 23.70 | 30.00 | 0 |
| Rope bridge (9, ev1819) | 19.99 | 29.97 | 2 (one 39 ms tick, unattributed) |

Smoothing was skipped on ~99% of the forest and bridge ticks and ~12% of the
torii's: in Debug those rooms run at their own 30 Hz. Getting the in-betweens
back there means getting the game's work plus a repeat present under ~13.7 ms
(a field less the 3 ms margin) — about 3 ms more off the bridge.

**Measuring missed ticks: do not count `paced` ≥ 45 ms.** `paced` runs from one
EndFrame's end to the next, and turning smoothing on moves the tick's last
present a field later, so the first smoothed tick reads 50 ms (and the first
unsmoothed one ~17 ms) with no field lost. Sum `paced` over the window against
two fields a tick instead.

So: **on a per-vertex path, size the destination once and write through a
pointer, and make no library call per component.** It costs nothing in an
optimised build and is most of a Debug frame. And do not read a Debug profile's
ranking as Release's.

What was left on the bridge afterwards: the preset walk and its colours ~14% of
wall time, `DrawGirl` ~7%, the shadow caster projection (`AssignShadowPrim`)
~6%, and the sky's TIM2 being re-uploaded through the GS every frame
(`MapSkySetTim2Vram` → `UploadPSMT8`) ~4%.

The ranking came from a temporary in-process sampler, not the stutter log
(which only splits phases): a thread that suspends the game thread at ~1 kHz,
unwinds it with `GetThreadContext` + `RtlVirtualUnwind` (nothing allocates
while it is suspended), and symbolises with DbgHelp at the end. ~150 lines;
rebuild it rather than keep it.

---

## Part 10 — Hazards

- **An in-between's field comes out of the game's next tick.** `vfunc()` runs
  *before* `MioPan_RendererEndFrame()` (`SendDMAMain()`), so a smoothed tick
  presents its in-between and waits out a field right after the game's wait,
  and the next tick's whole work — end of the presents to `vfunc()` — has the
  one field left: `game_cpu + repeat ≲ 16.7 ms`, not the `game_cpu + 2 ×
  renderer ≤ 33.3` this skill used to give. Past it, `vfunc()` finds both fields banked
  and still waits its unconditional one: a 50 ms tick, a 20 Hz game. Before
  `SmoothingFits()` existed, the Debug rope bridge did that on every tick.
  `SmoothingFits()` admits an in-between only when the longest span of the last
  16 ticks plus the last repeat present plus 3 ms fits a field; a skipped tick
  presents once and counts in the summary line's `late`. Do not loosen it
  without measuring: at 8 ticks and 1.5 ms, ~3% of admitted Debug bridge ticks
  overran.
- **The `Pacing target %d VBlanks (~%.1f ms)` line in the profiler summary
  hardcodes `1000.0/60.0`.** Now that PAL really is 50 Hz, that ms figure is 20%
  wrong in PAL mode. Display-only. `MioPan_PacingFieldPeriodNs()` now exposes
  the real field period, so the fix is a one-liner when someone wants it.
- **`g_present_interval`'s skip branch has never run.** Nothing sets the interval
  above 1. If increment 3 starts using it, expect first-run bugs in the bounded
  copy-only upload path in `EndFrame`.
- **The smoothing budget is `wait_num - 1`, not `wait_num`.** `vfunc()`'s final
  `WaitSema()` is unconditional, so the last field of every tick cannot be
  borrowed. Presenting past the budget does not raise the frame rate, it grows
  the tick — 2 extra presents measured 50.00 ms, a 20 Hz simulation. Anything
  that changes how presents are scheduled has to keep the clamp in
  `PresentLogicalFrame()` honest.
- **`MioPanUi::RenderDrawData()` deliberately does not clear its latch.** One
  ImGui frame is meant to feed several presents; `MioPanUi::BeginFrame()` owns
  the reset. Putting the one-shot guard back makes the overlay flash at the
  logical rate as soon as any frame is presented twice.
- **A repeated present reuses the GS block captures.** A slot captured twice in
  one logical frame keeps only its last capture. Accepted for in-betweens — but
  it means a repeat is not bit-identical to the real present in the handful of
  frames that do that, and Settings > Graphics > Frame smoothing will show it.
- **Do not reproject a draw without `transform_mesh`.** Screen-space draws have
  no `model` and their `mvp` is unused; rewriting it is harmless today but the
  invariant is what makes the eligibility test a one-liner.
- **Do not reproject shadow casters.** Their mvp is light-relative.
- **`sys_wrk.count` parity** drives which GS framebuffer page the effect
  read-backs use. Any scheme that changes how many rendered frames exist per
  logical frame has to keep parity coherent.
- **`draw.model` must never be overwritten by a blend.** The present that lands
  on the tick rebuilds its mvp from the frame's own transform, and the snapshot
  taken from it becomes the next frame's previous. `ReprojectDrawsAt()` blends
  into a temporary for exactly this reason.
- **A negative `t` is not a `t`.** Both `ApplyGeometryVertexBlend()` and
  `ReprojectDrawsAt()` take negative to mean "the frame as it was built", which
  is also what the loop passes when there is no previous camera. Running the
  lerp with one extrapolates *backwards* past the previous frame. This was a
  real bug during increment 4 and it is invisible in the common case, because
  the path only opens when the camera pair is missing but the geometry pair is
  not.
- **A geometry snapshot must be taken before the first present and swapped
  after the last.** Taken later it cannot feed the first reprojection; swapped
  earlier, a frame would blend against itself. A frame whose snapshot did not
  complete clears the pair instead of keeping it, or tick N+1 would be paired
  with tick N-1 across a frame that was never shown.
- **`ConvertMeshDrawsToStreamed()` invalidates the geometry snapshot**, because
  it rewrites `first_vertex`/`vertex_count` and appends to `g_vertices` after
  the snapshot was taken. Never reached in this build — the mesh cache has no
  callers — but the snapshot's whole safety rests on those indices.
- **Vertex colours are deliberately not blended.** A skinned character's vertex
  colour is its VU1 lighting, evaluated against the pose the game simulated;
  only positions move.
- **Per-frame renderer state is `thread_local`** (increment 5; the note above
  `g_shadow_episodes`).  A new global the record stage reads has to be
  `thread_local` too *and* added to `HandOverFrameState()` /
  `ExchangePresenterState()`, or the presenter records it from its own,
  never-written copy — an empty list, a zero matrix, a null buffer.  Any
  renderer call from a thread other than the game thread sees that thread's
  copies; the only other EE thread that runs game code (`thFileLoad`) makes
  none.
- **Never block in an SDL_GPU wait on the presenter.** On D3D12,
  `WaitForFences()` holds the device's submit lock across an infinite wait, so
  `SDL_WaitAndAcquireGPUSwapchainTexture()`, `SDL_WaitForGPUSwapchain()` and
  `SDL_WaitForGPUFences()` stall every other thread's submit.  Measured: the
  game thread's hand-off submit took 2.8 ms (p50) / 4.4 ms (max) until the
  presenter switched to polling the non-blocking acquire; then 0.05 / 0.30 ms.
  `MioPan_RendererReadbackScreen()` still waits on a fence (a photo), which
  holds the presenter up for that one frame.
- **The swapchain is acquired off the window's thread.** SDL documents it as
  window-thread only.  It works on D3D12 (fence waits, GetBuffer,
  ResizeBuffers, flip-model Present — none needs the window thread, and the
  backend never calls SetFullscreenState) as long as the game/window thread
  never blocks on the presenter for long.  Vulkan should be the same; Metal is
  where the rule is real.  Moving the game loop off the main thread (the
  reverted `miopan_host` split) would let the main thread run
  `PresenterStep()` itself and close this.
- **A texture a queued draw can name is released through
  `ReleaseFrameTexture()`**, which in display mode defers it until the
  presenter has moved past every packet that might name it.  A new release
  site for a draw-visible texture must use it.
- **Swapchain parameters change only under both locks.**
  `ApplySwapchainSettings()` re-runs `ApplyHdrComposition()` /
  `ApplyPresentMode()` in display mode only when one of their inputs moved,
  because `ResolveHdrComposition()` queries the swapchain the presenter is
  using.
- **Build:** `-fno-strict-aliasing` is not optional, and without the PATH export
  `cc1plus` fails with exit 1 and zero diagnostics.

```bash
export PATH="/path/to/mingw/bin:$PATH"   # the bin/ next to CMAKE_CXX_COMPILER
ninja -C cmake-build-relwithdebinfo MioPan
```

`ld.exe: cannot open output file MioPan\MioPan.exe: Permission denied` means the
game is still running — ask the user to close it.

Sources are globbed, so new `.cpp` files under `zero_rom/zero2np/src/` are picked
up automatically.

---

## Part 11 — Increment 5: display-rate presentation

**Menu bar → Settings → Graphics → Frame smoothing → Present at display rate**, persisted as
`[smoothing] mode = field | display` (default `field`), `MIOPAN_SMOOTHING`
overrides it for one run.  Field mode is the increment-3/4 path, unchanged.

### Why the field loop could not do it

The field loop presents inside the tick, one CRTC field apart, so it can only
borrow the one field `vfunc()` lets go of (Part 6): at most 60 Hz, paced by the
emulated 60 Hz field clock rather than the monitor — on a 144 Hz panel each
present is held for two refreshes or three, which judders — and the in-between's
field comes out of the next tick's work (`SmoothingFits()`).

### What display mode does

- **Game thread:** ticks exactly as before.  At the end of the tick
  `HandOffFrame()` runs the upload stage on its own command buffer, submits it,
  and hands the frame over as a `FramePacket` stamped with **T**, the
  *scheduled* counter of the field that released `vfunc()`
  (`MioPan_PacingLastFieldCounter()`, published by the V-blank thread from its
  limiter's target, not its wake-up).  It presents nothing and waits for
  nothing.  `SmoothingFits()` and `extra_presents` do not apply.
- **Presenter thread** (`PresenterStep()`, one refresh per call): polls the
  non-blocking swapchain acquire (see the D3D12 hazard in Part 10), turns the
  moment it returns into a refresh slot **S** with a phase-locked loop on the
  display's period (`AdvanceDisplaySlot()`: period from
  `SDL_GetCurrentDisplayMode`, 1/8 correction, resync past half a refresh),
  adopts the newest packet whose window has opened, and presents it at
  `t = (S - T - budget) / period` through `BlendCameraBetween()`,
  `ReprojectDrawsAt()`, `ApplyGeometryVertexBlend()` and `RecordFramePasses()`
  — the field loop's own functions.  The first present of a packet does the
  packet's once-only work (shadow map, GS captures, displayed page, screen
  mirror, depth-probe download); the rest are repeats.  A packet shown at t = 1
  with nothing newer is not presented again — the thread waits for the next
  tick instead of redrawing the same picture.

### Why t counts from T + budget

Evenly spaced frames need displayed game time = S − constant, and pure
interpolation needs tick N in hand for every S that shows anything past tick
N−1, so the constant is at least a tick plus the field-edge-to-hand-off time.
`budget` is that hand-off time, learned from the packets (rises at once on a
late one, decays 0.05 ms per packet, 0.5 ms margin, capped at 10 ms).  A packet
is adopted once S passes T + budget, so the last present before a hand-off
lands near t = 1 and the first after it near t = 0 — at 60 Hz with aligned
refreshes, exactly the field loop's 0.5 / 1.0.  **The cost is latency**: at the
first refresh after a hand-off the screen shows about a tick less one refresh
of age, where the field loop shows half a tick — +12.5 ms at 240 Hz, +9.7 ms at
144 Hz.  Only extrapolation avoids that, and the camera policy is
interpolation (Part 5).

### Threads, state and ownership

- Everything the record stage reads is `thread_local`: draw list and streams,
  capture points and slots, shadow episodes, light states, uniform block,
  camera and previous camera, GS fog, view extend, upload flags, depth-probe
  postings, the geometry pair, the four per-frame GPU buffers, the stream
  staging buffers.  The game thread builds into its copies; the presenter
  records from its own; the record code is unchanged.  The profiler's
  `g_frame` is `thread_local` too, so every profiler call the recorder makes on
  the presenter is a no-op.
- Three `FramePacket` slots (FREE / FILLING / PUBLISHED / PRESENTING), so the
  game never waits for the presenter.  `HandOverFrameState()` swaps the frame
  in and copies what the game carries into its next tick;
  `ExchangePresenterState()` swaps everything both ways on adopt and retire.  A
  retired packet goes back to the game thread with its storage and buffers, so
  every `DrawCommand` (and `ResidentMesh`) is still destroyed on the game
  thread.
- The ImGui overlay goes over as a `MioPanUi::DrawSnapshot`: the game thread
  does texture creates/updates itself, copies the draw lists with their texture
  references resolved to IDs, and holds retired ImGui textures for 3 frames.
- Locks: `g_swapchain_lock` (presenter, acquire → submit; game thread only for
  vsync/HDR changes), `g_present_state_lock` (recursive; presenter across
  record + submit; game thread around depth-probe resolve, screen capture /
  readback / captured-screen draw, MSAA / anisotropy / HDR setters, and the
  mesh-pin list), `g_packet_lock` (slot status only).  Order:
  swapchain → state.

### Measured (RelWithDebInfo, RTX 4080, D3D12, HDR10, 5120x1417 window, 240 Hz)

Chapter 3, scripted walk, 43 s, same script both modes.  Measured with a
temporary per-present trace — one line per present from `PresenterStep()` and
`PresentLogicalFrame()`: slot time, tick, raw t, t, and the eye recovered from
the live view (`ViewMatrixEye(g_3d_view)`), ~25 lines, removed afterwards;
rebuild it — plus the stutter log per tick.  "Eye velocity vs neighbours" is,
per present while the camera moves, |eye step / time step| against the mean of
the two presents either side:

| | field (1 extra) | display |
|---|---|---|
| presents/s | 60.0 | 240.0 |
| present spacing sd | 0.345 ms | 0.029 ms |
| presents at a double interval | 0 | 0 of 10,326 |
| t distribution (eighths) | 50% at 0.5, 50% at 1.0 | 12.5% each |
| eye velocity vs neighbours while moving | 5.6% | 1.8% |
| held presents (raw t > 1) | — | 6 (0.14/s) |
| paced | 33.33 ms, 2.00 fields/tick | 33.33 ms, 2.00 fields/tick |
| game thread `VBlank/gate` (time `vfunc()` waits) | 13.1 ms | 29.9 ms |
| game thread renderer (upload + submit) | 1.67 ms | 0.36 ms |
| CPU per present | ~0.55 ms (record + submit) | 0.52 ms avg, 0.77 max (presenter) |
| hand-off (field edge → published) | — | 0.36 ms, budget 0.83 ms |

So **game_cpu gets its full two fields back**: the game thread waits ~29.9 of
every 33.3 ms instead of 13.1, and `SmoothingFits()` never has to skip.

**MSVC Debug** (`/Od /RTC1`), same walk, against a Debug build of the base
commit with the same test hooks:

| | base, field | this, field | this, display |
|---|---|---|---|
| sgd / meshcpu per tick | 8.26 / 9.50 ms | 8.15 / 9.40 ms | 8.22 / 9.48 ms |
| presents/s | — | 42.9 (`SmoothingFits()` skipping) | 240.0, none held |
| eye velocity vs neighbours | — | 9.0% | 2.0% |
| CPU per present | — | — | 0.92 ms avg, 1.67 max |

`thread_local` costs the game thread nothing measurable even at `/Od`.  On
chapter 0's rope bridge (room 9, event 1819 — the Debug shot that could not
smooth at all) field mode presents 30.2/s and display mode 239.9/s, 2.00 fields
a tick, over 900 ticks.  The
stalls and jumps the eye-velocity check flags are the game's own camera
pausing for a tick at a stick change, faithfully interpolated, and camera cuts.

Robustness, same build: 29 runtime mode switches (field → display costs
nothing on screen; display → field drops one tick, ~37 ms, while the
presenter joins), the pause screen over its captured still, and five window
resizes while the presenter owned the swapchain — no crash, no SDL error, the
swapchain followed every size.

### Known imprecisions (prototype)

- The pause still, the photo mirror and the depth-probe download are taken on
  the first present of each packet, at t ≈ 0 — about a tick older than the
  frame the game just simulated (the field loop takes them at t = 0.5).
- Video frames upload into the same texture a packet still being shown
  samples, so a movie frame can appear one tick early.
- A late hand-off holds one refresh at t = 1 (0.14/s measured), then the budget
  covers it.
- MinGW-GCC builds use emulated TLS for `thread_local` (a call per access);
  MSVC's is a TLS-slot load.  Measure the game thread there before relying on
  it.

### Next steps

- Take the pause still / mirror / depth probes from the present nearest t = 1,
  or render them once per packet at t = 1 at mirror resolution.
- Phase-lock the emulated field clock to the display when its rate is a
  multiple of 60 (120/240 Hz): presents would land on exact t = k/n and the
  budget's last refresh of latency would go.
- Move the game loop off the main thread so the main thread presents.
