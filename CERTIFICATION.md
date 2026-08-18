# ODPAR: Territorial Domain v14 — verification snapshot

Date: 2026-08-18  
Authority: deterministic C11 core  
Gameplay API: **14**  
FFI ABI: **1**

This document separates what was executed on this host from what is prepared for a
target toolchain. It does not label an APK or a WASM binary as built when that compiler
was unavailable.

## Executed native gates

Strict C11 (`-Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wsign-conversion
-Werror`): **PASS**

Deterministic unit/integration and renderer regressions: **PASS**

```text
OK v14 deterministic=c7a4c7626d26e643 framebuffer=518400 playable=10295 turrets=14
```

FFI ABI/buffer/orientation suite: **PASS**

```text
FFI v1 OK api=14 portrait=720x1280 pixels=921600 hash=864a36eda50a2346
```

The portrait and landscape scripted runs publish the same authoritative state hash;
`odg_resize()` and `odg_render_frame()` do not change it.

Expanded 60,000-tick state/render soak, executed twice: **PASS, identical result**

```text
SOAK V14 OK outer_ticks=60000 rounds=1 hash=f9dc40603bd91989 cells=46 alive=5 turrets=14 chips=1
```

The soak continuously varies inputs and raster sizes, exercises ACTION and DROP, and
checks territory/trail counts, actor lifetime, turret locks/ammunition, crates, chips,
carriers, playable topology and absence of automatic respawn.

## Sanitizers

AddressSanitizer + UndefinedBehaviorSanitizer: **PASS** for all three gates:

- unit/integration/renderer suite;
- FFI portrait/copy/orientation suite;
- 60,000-tick soak.

LeakSanitizer cannot run under this host's ptrace-based executor, so the verified command
used `ASAN_OPTIONS=detect_leaks=0`. This is an environment boundary, not a reported leak.
The C engine owns fixed-capacity global state and requires no gameplay heap allocation.

## Native ABI and hardening

Optimized shared library build: **PASS**

```text
NATIVE SYMBOL SURFACE OK public=73 extra=0
NATIVE HARDENING OK relro=full nx_stack=yes canary=yes textrel=no
HOST CONTRACT OK api=14 dart_symbols=22 public_c=73 authority=C11 webview=no
```

The library uses a version-script allowlist, full RELRO/NOW, non-executable stack,
stack protector and no text relocations. Flutter CMake also requests 16 KiB ELF segment
alignment for target packages made with older NDKs.

## Renderer benchmark

`make bench` uses one fixed world/input path at every size. Representative optimized
native run on this host:

| Raster | ms/frame | Raster fps equivalent |
| --- | ---: | ---: |
| 480×270 | 4.394 | 227.6 |
| 960×540 | 9.216 | 108.5 |
| 1280×720 | 26.355 | 37.9 |
| 540×960 | 10.306 | 97.0 |
| 720×1280 | 16.369 | 61.1 |

These are software-renderer costs on this host, not promised device FPS. Flutter starts
at balanced density and uses hysteresis to adjust only raster pixels; simulation stays at
120 Hz and no mechanics, bots or map precision are removed.

## Protected v14 behavior

The suites explicitly protect:

- strict local majority is required to commission a neutral turret;
- non-playable ocean cells do not count in the commissioning vote;
- once commissioned, later paint cannot flip a turret;
- a neutral turret neither advertises nor consumes a reprogram chip;
- an enemy-owned turret still requires and consumes the chip;
- bots never route toward neutral infrastructure as if a chip could reprogram it;
- malformed/dead actor, carrier, chip and turret states cannot expose contextual action;
- enemy trail contact remains independent of ground owner in both directions;
- self trail remains non-lethal;
- territory counts and persistent disconnected remnants remain coherent;
- partial turret refill, domain-bound relocation, committed target lock and exact
  deployment preview remain protected;
- portrait/landscape render metadata, safe copies and buffer-too-small statuses remain
  protected.

## Flutter and WASM target status

Flutter/Dart source, Gradle/CMake packaging, XML/YAML, FFI lookup surface and host
contract checks: **PASS**. The app includes SafeArea portrait/landscape layout,
multitouch movement/look/ACTION/DROP, lifecycle neutralization and adaptive raster.

The local host does not contain Flutter, Dart, CMake, Android SDK/NDK or Gradle, so no
local APK, `flutter analyze` or `flutter test` result is claimed. The included GitHub
Actions gate installs Flutter stable, performs those checks, builds universal and
ABI-split APKs, and verifies the packaged C library.

The web host is a presenter of the C framebuffer and its API 14 smoke test is updated.
Clang/LLD's WebAssembly target is not installed locally, so no v14 WASM binary is claimed
here. CI rebuilds and smoke-tests WASM plus the self-contained preview from the same C
sources. Stale v13 generated web binaries are excluded from the v14 source release.

## 14.0.1 renderer correction

Landscape and portrait captures were regenerated after restoring the square actor and
removing redundant diagonal corner links from trails. Real isolated diagonal steps stay
connected; an orthogonal corner no longer receives an extra diagonal hypotenuse. The
live actor-to-last-cell tail now uses the same two-layer band as committed trail cells.

This patch changes only `engine/src/render.c`. `make host-check` confirms that Flutter
still compiles that source directly and continues to consume API 14 / FFI ABI 1 through
the unchanged RGBA8 copy contract; no Dart, UI, Gradle or input code was rewritten.

## Runtime footprint

| Object | Bytes |
| --- | ---: |
| `odg_world` | 161,152 |
| `odg_actor` | 232 |
| `odg_turret` | 96 |
| `odg_ammo_crate` | 28 |
| `odg_chip` | 28 |
| `odg_game_stats` | 192 |
| `odg_ffi_abi_info` | 64 |
| maximum RGBA framebuffer | 3,686,400 |
| maximum depth buffer | 1,843,200 |

The final downloadable archive is additionally verified from a clean extraction before
delivery.
