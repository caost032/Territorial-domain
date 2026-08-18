# ODPAR: Territorial Domain — v14

`Territorial Domain` is a lightweight 3D territory game whose single authority is a
deterministic native C11 engine. Flutter/Dart and WebAssembly are hosts of that engine;
neither contains a second gameplay implementation.

## What v14 changes

- **Neutral infrastructure follows conquest.** A neutral turret is commissioned by the
  first living actor to own a strict majority of its playable local 5×5 neighborhood.
  Ocean cells do not make coastal turrets impossible to claim.
- **The chip keeps a distinct purpose.** Territory never flips a turret that already has
  an owner. A reprogram chip is required for enemy infrastructure and is never consumed
  or advertised for a neutral turret.
- **One topology truth.** Ground ownership and exposed-trail ownership remain independent.
  Crossing an enemy trail defeats its owner on friendly, hostile or neutral ground;
  crossing your own trail remains safe.
- **Adult visual direction.** The C renderer uses restrained material palettes,
  terrain-integrated ownership, thinner signal-like trails, atmospheric depth and a
  quieter architectural/UI hierarchy instead of large toy-like color blocks. Actors
  remain deliberately square, with restrained materials and a small direction marker.
- **A real Flutter app.** `app/flutter/` is a complete Dart/FFI host, not a placeholder.
  It drives the same C sources directly and supports portrait/landscape, safe areas,
  multitouch, lifecycle and adaptive raster density. Android is only one packaging
  target for that Flutter app; it is not a second engine or gameplay implementation.
- **A hardened ABI.** FFI ABI v1 publishes a fixed 64-byte discovery structure,
  endian marker, sizes, feature bits, safe copy APIs and a strict native symbol allowlist.
- **Orientation does not change the game.** The render contract accepts any width/height
  up to 1280 within a 921,600-pixel budget, including 720×1280. Raster size is excluded
  from the deterministic state hash.

## Game contract

- one player plus nine deterministic local bots;
- organic 128×128 playable topology;
- fixed 120 Hz simulation with integer/fixed-point authoritative state;
- leave owned ground to expose a trail; reconnect it to flood-fill the enclosed region;
- self trails are non-lethal; enemy trails are vulnerable regardless of ground owner;
- 14 finite-ammunition topology turrets with telegraphed target locks;
- territory capture earns ammunition; crates and chips are physical carried items;
- no automatic respawn;
- four presentation profiles and a deterministic day/night world;
- no required gameplay heap allocation.

## Architecture

```text
                         one C11 authority
             engine/src + engine/include + vendor
                         /              \
              native C11 library      freestanding WASM
                    |                       |
           Flutter / Dart FFI        browser preview host
                    \                       /
                 input + pixels + POD snapshots
```

The browser page really executes the compiled C/WASM module. JavaScript only collects
input, presents the RGBA framebuffer and renders host UI. The Flutter app follows the
same boundary through FFI. GitHub Actions packages its APK and regenerates WASM from
the same source set.

## Build and verification

Native host:

```sh
make test
make soak
make native
make symbols
```

When Clang/LLD with the WebAssembly target is installed:

```sh
make wasm
node tools/wasm_smoke.mjs
make bundle
```

Flutter/Dart FFI (or use the included GitHub Actions workflow for APK packaging):

```sh
cd app/flutter
flutter pub get
flutter analyze
flutter test
flutter build apk --release
```

See `docs/FLUTTER_APP.md`, `docs/APP_EMBEDDING.md` and `CERTIFICATION.md` for the exact
host and verification boundaries.

## Scope not claimed

The current game is local play against bots. A deployed authoritative multiplayer
service, matchmaking, accounts, reconciliation and anti-cheat backend are not claimed.
The checked-in Flutter source is build-ready, but this source snapshot does not claim a
locally built APK when Flutter and its target toolchain are unavailable on the
verification host.
