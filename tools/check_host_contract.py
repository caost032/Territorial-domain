#!/usr/bin/env python3
"""Static drift guard for the C authority, Dart FFI host and WASM preview host."""

from __future__ import annotations

import json
import pathlib
import re
import sys


ROOT = pathlib.Path(__file__).resolve().parents[1]


def read(relative: str) -> str:
    return (ROOT / relative).read_text(encoding="utf-8")


def fail(message: str) -> None:
    print(f"host contract failed: {message}", file=sys.stderr)
    raise SystemExit(1)


def main() -> int:
    header = read("engine/include/odpar_game.h")
    dart = read("app/flutter/lib/src/native/odg_bindings.dart")
    runtime = read("app/flutter/lib/src/engine/game_runtime.dart")
    flutter_sources = "\n".join(
        path.read_text(encoding="utf-8")
        for path in sorted((ROOT / "app/flutter/lib").rglob("*.dart"))
    )
    web = read("app/web/index.html")
    smoke = read("tools/wasm_smoke.mjs")
    cmake = read("app/flutter/CMakeLists.txt")
    workflow = read(".github/workflows/android.yml")
    spine = json.loads(read("PROJECT_SPINE.json"))

    if "#define ODG_API_VERSION UINT32_C(14)" not in header:
        fail("C header is not API 14")
    if "const int odgApiVersion = 14;" not in dart:
        fail("Dart binding is not API 14")
    if "wasm.odg_api_version()!==14" not in web:
        fail("web host is not API 14")
    if "api=14" not in smoke or "!== 14" not in smoke:
        fail("WASM smoke test is not API 14")
    if spine.get("runtime", {}).get("api_version") != 14:
        fail("project spine is not API 14")

    public = set(re.findall(r"\b(odg_[a-zA-Z0-9_]+)\s*\(", header))
    dart_lookups = set(re.findall(r"lookupFunction<[^;]+?\(\s*'([^']+)'", dart, re.S))
    missing_native = sorted(dart_lookups - public)
    if missing_native:
        fail("Dart looks up non-public symbols: " + ", ".join(missing_native))
    required = {
        "odg_api_version",
        "odg_ffi_abi_query",
        "odg_init",
        "odg_resize",
        "odg_reset",
        "odg_set_input",
        "odg_tick_us",
        "odg_render_frame",
        "odg_copy_framebuffer",
        "odg_copy_stats",
        "odg_framebuffer_bytes",
        "odg_framebuffer_stride_bytes",
        "odg_render_width",
        "odg_render_height",
    }
    if not required.issubset(dart_lookups):
        fail("Dart is missing required calls: " + ", ".join(sorted(required - dart_lookups)))

    engine_sources = {
        "src/platform.c",
        "src/game.c",
        "src/sim.c",
        "src/render.c",
        "vendor/odm_status.c",
        "vendor/odm_rng.c",
    }
    for source in engine_sources:
        if f'"${{ODPAR_ENGINE_ROOT}}/{source}"' not in cmake:
            fail(f"Flutter CMake omits authoritative engine source {source}")

    if re.search(r"\b(webview|WebView)\b", flutter_sources):
        fail("Flutter host contains a WebView")
    if "api.copyFramebuffer" not in runtime or "api.copyStats" not in runtime:
        fail("Flutter runtime does not use coherent copy APIs")
    if "odg_state_hash" not in dart:
        fail("Flutter binding omits deterministic state hash")
    if "make wasm" not in workflow or "flutter build apk --release" not in workflow:
        fail("CI does not build both Flutter packaging and the WASM preview")
    if "arm64-v8a" not in workflow:
        fail("CI does not verify the primary mobile ABI")

    print(
        "HOST CONTRACT OK "
        f"api=14 dart_symbols={len(dart_lookups)} public_c={len(public)} "
        "authority=C11 webview=no"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
