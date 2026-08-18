# Architecture — ODPAR: Territorial Domain v14

## Authority

Native C11 is the source of truth. Flutter/Dart FFI is the primary app host and the web
shell is a preview host. UI layout, joystick placement and host presentation never enter
deterministic world state.

## Fixed-step simulation

The simulation advances at 120 Hz. Territory ownership, exposed trails, bots, turrets, chips, ammo logistics, RNG and rules are fixed-point/integer authoritative.

## Territory and trail are separate layers

The terrain cell owner and the exposed-trail owner are independent. Trail-contact resolution runs before same-ground/home-ground handling. Therefore a defender can cut an invader's exposed trail while standing in the defender's own territory, and an invader can cut a defender's exposed trail even when the invader is standing in its own territory. Self trail remains non-lethal.

Flood-fill capture changes cells actually enclosed by the reconnecting trail; it does not globally erase every disconnected remnant of a previous owner.

## Camera-relative control

`odg_set_input()` receives camera-local movement and independent look deltas. UP means current camera-forward on every fixed tick. Normal turns own the ground-path direction immediately while actor body facing rotates inertially; near-180 reversal retains deliberate braking. `odg_set_world_input()` remains available for replays/specialized native hosts that intentionally own a world-space heading.

## Turret authority

The first living actor with a strict majority of the playable cells in a neutral
turret's local 5×5 neighborhood commissions it. Non-playable ocean cells are excluded.
Once a turret has an owner, painting around it alone does not reprogram it. Enemy-owned
infrastructure requires a carried reprogram chip plus a contextual action inside hack
range; neutral turrets are neither advertised as chip targets nor allowed to consume a
chip.

Player turret relocation is domain-bound: pickup requires the player and turret to be on player-owned cells; placement requires a valid owned destination. Carried-turret movement cannot leave owned territory.

Turrets acquire exposed-trail cells with a committed lock. During the telegraph the target does not switch merely because a closer valid cell appears. Invalidated targets receive bounded retarget handling. A minimum target distance keeps the horizontal turret head readable around the base.

## Ammunition and carried items

Territorial expansion can accrue ammo reserve. Nearby owned turrets top up partially and consume only the amount required. World ammo crates and reprogram chips are physical carryables. The general DROP action releases the current carried object; reserve ammo can be materialized as a physical crate. A short pickup cooldown prevents immediate drop/re-pick cycles.

## Navigation

Bots use a derived physics-aware cardinal navigation-edge field built from coast, actor footprint and static obstacles. Contact steering searches a nearby valid tangent with hysteresis. A progress watchdog invalidates plans that fail to move. Bot chip/ammo logistics share the same physical world rules.

## World and sky

The 128×128 allocation remains a compact topology with an irregular playable mask. Render terrain is a deterministic 3D height field. Celestial phase is deterministic; sun/moon/night references are projected from world-space bearings relative to camera orientation, never fixed screen coordinates.

## Renderer

The renderer is dependency-free C with near-plane clipping, perspective-correct inverse
depth, scanline span triangle rasterization and depth-tested world lines.
Territory/trail surfaces follow the same terrain height field. Contact shadows sample
terrain per vertex and are deliberately layered above territory fill but below
trail/border accents. Semantic color roles, atmospheric depth and restrained material
contrast keep allegiance and hazards legible without turning the world into saturated
toy blocks.

Actors deliberately retain the game's square silhouette. A restrained material treatment,
thin player belt and small front marker communicate identity and heading without changing
the authoritative cubic footprint, steering or contact rules.

The render contract is orientation-neutral: each dimension may be up to 1280, with a
hard budget of 921,600 pixels. A 720×1280 portrait framebuffer and a 1280×720 landscape
framebuffer are equally valid. Resizing is presentation-only and cannot change the
authoritative state hash.

The Flutter host queries FFI ABI v1 before initialization, serializes all calls through
one engine owner and copies RGBA/POD snapshots into Dart-owned memory. The WASM preview
can present the same C framebuffer through WebGL2 texture upload when available, with
Canvas2D fallback. Neither presentation path is part of game authority.
