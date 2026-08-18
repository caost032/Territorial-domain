# Changelog

## 14.0.1 — 2026-08-18

### Presentation correction

- Restored the deliberately square player and bot silhouettes without changing their
  authoritative footprint, state or camera.
- Removed redundant diagonal trail links that closed cardinal corners into visible
  `/\|\/` triangles and teeth.
- Replaced the one-pixel live trail tail with the same continuous outer/inner band used
  by committed trail cells.

### Host boundary

- This is an engine-renderer-only update. The Flutter/Dart UI, controls, Gradle setup
  and FFI ABI remain unchanged; CMake recompiles the same `engine/src/render.c` source.

## 14.0.0 — 2026-08-18

### Gameplay

- Neutral turrets now belong to the first living actor that establishes a strict local
  territorial majority; non-playable coastal cells are excluded from the vote.
- Territory paint can never reprogram infrastructure that already has an owner.
- Reprogram chips now target only enemy-owned turrets and cannot be consumed or
  advertised beside neutral infrastructure.
- Bot routing uses the same reprogrammable-enemy predicate and never pursues a neutral
  turret with a chip.
- Added defensive carrier/actor/turret validation and deterministic regressions for
  every ownership transition.

### Presentation

- Reworked semantic palettes, land ownership, trails, coast, architecture, sky,
  atmospheric depth and final color response in the C renderer.
- Replaced bubble-like territory tiles and heavily saturated decoration with continuous
  terrain-integrated material and restrained signals.
- Added aspect-aware portrait/landscape rendering under one adaptive pixel budget.
- Reworked the browser host into a quiet field-interface aesthetic; it remains a thin
  presenter of the real C/WASM engine.

### Flutter / FFI

- Promoted `app/flutter/` from a binding sketch to a complete Flutter application with
  lifecycle handling, safe areas, multitouch movement/look/action/drop, dynamic raster
  quality and coherent RGBA/stat snapshots.
- Added FFI ABI v1 discovery with fixed-width POD sizes, endian/pixel-format markers,
  feature bits and caller-owned copy APIs.
- Added target packaging through CMake plus GitHub Actions; all targets compile the same
  C11 authority instead of copying gameplay into Dart.

### Verification and hardening

- Public gameplay API is now 14.
- Added portrait/landscape state-equivalence and FFI buffer-boundary tests.
- Added strict dynamic-symbol allowlisting, full RELRO/NOW, non-executable stack,
  stack-protector and text-relocation checks for native builds.
- Expanded the long soak with chip and DROP invariants.
