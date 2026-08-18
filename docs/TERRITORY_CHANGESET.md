# ODPAR: Territorial Domain — correction ledger through v14

## v14 — first-conquest infrastructure + complete Flutter boundary

- neutral turrets are commissioned by the first living actor with a strict majority of
  playable territory in their local 5×5 neighborhood;
- ocean cells are excluded from that vote, so coastal infrastructure is claimable;
- a commissioned turret never flips from later surrounding paint;
- reprogram chips target only already-owned enemy turrets and remain intact near
  neutral infrastructure;
- malformed actor, carrier, chip and turret states are rejected defensively;
- FFI ABI v1 adds fixed-width discovery, feature flags, size/endian validation and safe
  caller-owned framebuffer/stat copies;
- portrait and landscape share one 921,600-pixel presentation budget without changing
  simulation state;
- Flutter/Dart drives the same C11 engine; the browser remains a C/WASM preview rather
  than a second game implementation;
- visual direction moves to restrained semantic materials, atmospheric depth and a
  quieter host UI;
- square actors retain their original identity while using the quieter v14 material
  treatment;
- redundant diagonal trail links are suppressed, so cardinal corners no longer close
  into `/\|\/` triangles; genuine isolated diagonal steps remain continuous.

## v13 — independent trail authority + programmable infrastructure

- enemy trail contact now has priority over ground ownership, fixing home-territory immunity bugs;
- self trails remain non-lethal;
- partial turret refill consumes only missing ammunition;
- nearby owned-turret `ammo/max` visibility is range-bounded;
- owned turret pickup/transport/placement is constrained to owner territory;
- already-owned enemy turrets require a reprogram chip instead of flipping from surrounding paint;
- neutral turret commissioning by territorial dominance remains;
- turret trail target lock is committed through telegraph and resists closer-target chatter;
- minimum lock distance prevents unreadable under-base aiming;
- general DROP action releases turret/chip/ammo crate or materializes reserve ammo;
- dropped items receive brief pickup immunity;
- sun/moon/stars are world-relative with deterministic celestial phase;
- contact shadows follow terrain and layer above territory fill;
- trail visuals are narrowed/rounded without changing deterministic collision topology.

## v12 — camera-relative movement + Ultra renderer

- fixed joystick resolved against current camera basis each fixed tick;
- independent persistent free-look camera;
- immediate normal movement direction with inertial body facing;
- ground-gliding cubes without cyclic vertical bob;
- scanline-span triangle rasterizer and incremental inverse depth;
- WebGL2 preview presentation with Canvas2D fallback;
- full 1280×720 Ultra target with measured-cost protection.

## Earlier foundations

v10 introduced continuous contact steering, physics-aware bot navigation, bot progress watchdog, camera obstacle compression and depth-tested world lines. Earlier versions established deterministic flood-fill territory, bots, turrets, ammo logistics, organic topology and configurable fixed joystick placement.
