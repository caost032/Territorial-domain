# App embedding — API 14 / FFI ABI 1

Production hosts call the native C library directly. WASM is a preview/testing target
compiled from the same source files; it is not a separate engine.

## Startup contract

1. Load `libodpar_territorial_domain.so`.
2. Call `odg_ffi_abi_query(ODG_FFI_ABI_VERSION, ...)`.
3. Reject mismatched ABI/API, endian marker, POD sizes or pixel format.
4. Choose a render width/height whose dimensions are at most 1280 and whose product is
   at most `ODG_MAX_RENDER_PIXELS` (921,600).
5. Call `odg_init(seed, width, height)` on the single runtime-owning thread/isolate.

The engine is currently a single global instance. It is intentionally not reentrant or
thread-safe; all `odg_*` calls must be serialized by one host owner.

## Per-frame sequence

```text
consolidate host input
    -> odg_set_input(...)
    -> odg_tick_us(elapsed_us)
    -> odg_render_frame()
    -> odg_copy_framebuffer(...)
    -> odg_copy_stats(...)
```

The pointer accessors remain available for synchronous low-level hosts. Flutter uses
the copy APIs so asynchronous image decoding never aliases a framebuffer that C is
rewriting.

## Input

Normal gameplay uses:

```c
odg_set_input(move_right_q15, move_forward_q15,
              look_yaw_q15, look_pitch_q15, buttons);
```

- movement is local to the current C camera;
- forward always means the current camera-forward direction;
- free-look is independent from movement;
- `ODG_BUTTON_ACTION` is the contextual owned-turret/enemy-chip action;
- `ODG_BUTTON_DROP` releases the carried object or materializes reserve ammunition;
- buttons are edge-triggered by the simulation, so touch hosts emit clean pulses.

`odg_set_world_input()` is reserved for deterministic replay, tests and specialized
hosts that intentionally provide a world-space heading.

## Rotation and quality

Rotation calls `odg_resize()` and never resets a match. The engine accepts landscape,
portrait and intermediate aspect ratios under one fixed pixel budget. Render dimensions
are presentation state and do not enter `odg_state_hash()`.

The host may adapt raster density with hysteresis. It must not lower simulation rate,
remove game features or fork world state. Flutter UI remains at device resolution while
only the copied C framebuffer changes density.

## Contextual state

`odg_game_stats` exposes ownership, trail, carrying, ACTION/DROP and nearby owned-turret
ammo state as one fixed-width snapshot. Neutral turrets are never hack targets: conquest
commissions them automatically; chips are only for already-owned enemy infrastructure.
