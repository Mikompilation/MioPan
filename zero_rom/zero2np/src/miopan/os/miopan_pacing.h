#ifndef MIOPAN_PACING_H
#define MIOPAN_PACING_H

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Host override for the game loop's per-frame V-blank wait.
 *
 * The game paces itself in vfunc() by waiting SYSTEM_VBLANK_WAIT_NUM CRTC
 * fields per logical frame -- 2, so 30 fps against a 60 Hz field rate.  That
 * value is the game's, not the port's: movie.c drops it to 1 for the duration
 * of a PSS movie and puts it back afterwards, so it cannot simply be edited.
 *
 * This is a *measurement harness*, not a setting, and it is deliberately not
 * persisted to miopan.ini.  One field per frame runs the whole game at double
 * speed, because every timer, animation step and movement integration in the
 * engine is a fixed per-frame increment with no delta time -- the point is not
 * that it is playable, it is that the profiler's workload figures are then
 * taken at the frame rate you are asking about instead of inferred from a
 * 30 Hz sample.
 *
 * Read the answer off the profiler overlay: `vblank_deadline_misses` rising at
 * one field per frame means the workload does not fit in 16.7 ms, and the
 * per-phase breakdown says what to do about it.
 */

/* Fields to wait per frame, or 0 to follow whatever the game asked for.
 * Values are clamped to 1..8. */
void MioPan_SetVBlankWaitOverride(int fields);

/* The current override, or 0 when the game is in charge. */
int MioPan_GetVBlankWaitOverride(void);

/* What vfunc() should actually wait for: the override when one is set,
 * otherwise the value the game requested. */
int MioPan_ResolveVBlankWait(int game_requested);

/*
 * The CRTC field, as a signal the host can pace against.
 *
 * Separate from the game's V-blank semaphore on purpose.  vfunc() measures the
 * logical frame by *draining* that semaphore and counting how many fields went
 * by, so a field spent here is one it finds already banked and does not wait
 * for again -- which is what lets the renderer put an interpolated in-between
 * frame on screen between two of the game's own without lengthening the tick.
 * Consuming the game's semaphore instead would add a field per frame and slow
 * the simulation down.
 *
 * MioPan_PacingSignalField() is called from the V-blank thread, once per field.
 * MioPan_PacingWaitField() blocks until the *next* one -- it drains the backlog
 * first, because fields signalled while the game was busy have already elapsed
 * and returning on one of those would let no time pass at all.  It returns
 * immediately if no field has ever been signalled, so a caller running before
 * the V-blank thread exists degrades to no wait rather than to a hang.
 */
void MioPan_PacingSignalField(void);
void MioPan_PacingWaitField(void);

/*
 * The field count vfunc() last resolved, or 0 before the first frame.
 *
 * How many fields the host may spend on its own follows directly from it, and
 * it is one *less*: after draining, vfunc() performs an unconditional
 * WaitSema() no matter how many fields are already banked --
 *
 *     remain = (wait_num - count) - 1;
 *     if (0 < remain) { ...wait remain times... }
 *     WaitSema(vblank_sema);          <-- always
 *
 * -- so the last field of every tick is one vfunc() insists on waiting for and
 * cannot be borrowed.  At the game's usual 2 that leaves exactly 1, which is
 * why 60 Hz presentation is the ceiling for a 30 Hz tick.  Spending more does
 * not raise the frame rate; it lengthens the tick and slows the game down.
 */
int MioPan_PacingLastVBlankWait(void);

#ifdef __cplusplus
}
#endif

#endif /* MIOPAN_PACING_H */
