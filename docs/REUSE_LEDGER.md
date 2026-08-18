# Reuse Ledger

Source supplied by the user:

- User-supplied title: `ODPAR-Music-Motion-LATEST (27).zip`
- Source ZIP SHA-256 observed for this build:
  `2be6e55761f6fef2c9e43b312d56700a02bda32864a9b1b3735199bf5ae286b4`

## Directly reused

### Status capability

- `engine/vendor/odm_status.h`
- `engine/vendor/odm_status.c`

Purpose: stable explicit status vocabulary and C ABI discipline.

### Deterministic RNG capability

- `engine/vendor/odm_rng.h`
- `engine/vendor/odm_rng.c`

Purpose: PCG32 XSH-RR seeded and derived streams for world/bot determinism.

## Reused as architecture, rewritten for game hot path

### Engine-first app boundary

Music Motion's rule that the app discovers/edits engine-owned capability is retained.
ODPAR: Territorial domain's app shell does not duplicate territory, capture, collision, bot or render-state
truth.

### Exact time / FPS independence

Music Motion's sample-domain timing principle becomes a 120 Hz game tick with an exact
microsecond rational accumulator.

### Semantic color and mobile presentation

Music Motion's semantic-role palette, linear-light color discipline, safe-area
attachments and raster-budget policy informed the rewritten Territorial Domain
presentation. The implementation remains game-specific: territory readability,
turret allegiance, trail hazard and depth cues take priority over decorative effects,
and quality changes never alter the 120 Hz simulation.

### Stable FFI discovery

The ABI-query, fixed-width POD, caller-owned-buffer and minimal exported-symbol patterns
were adapted into Territorial Domain's FFI ABI v1. No Music Motion media or preview API
is exposed by the game.

### Bounded mobile runtime

Music Motion's viewport-budget and no-hidden-allocation direction becomes fixed-capacity
game state, capped catch-up, explicit maximum resolution and a small software renderer.

### Scene3D concepts

World space, camera space, procedural geometry, depth-tested rasterization and
separation of authoritative world semantics from raster quality are retained as design
principles. The actual ODPAR: Territorial domain renderer was newly written rather than pulling the
full Scene3D dependency graph into the game.

## Intentionally NOT reused

The Music Motion media, music analysis, timeline authoring, delivery/export, project
packaging, studio workflow and advanced authoring graph were not linked into this game.
They would increase binary/API surface without serving the real-time arena loop.

This is deliberate capability extraction rather than copying the whole source motor.
