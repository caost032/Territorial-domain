#!/usr/bin/env python3
"""Fail when the native shared library exports anything outside the public ODG ABI."""

from __future__ import annotations

import pathlib
import re
import subprocess
import sys


ROOT = pathlib.Path(__file__).resolve().parents[1]
HEADER = ROOT / "engine" / "include" / "odpar_game.h"
LIBRARY = ROOT / "build" / "libodpar_territorial_domain.so"


def public_header_symbols() -> set[str]:
    text = HEADER.read_text(encoding="utf-8")
    return set(re.findall(r"\b(odg_[a-zA-Z0-9_]+)\s*\(", text))


def dynamic_symbols() -> set[str]:
    output = subprocess.check_output(
        ["nm", "-D", "--defined-only", str(LIBRARY)], text=True
    )
    symbols: set[str] = set()
    for line in output.splitlines():
        token = line.split()[-1]
        token = token.split("@@", 1)[0].split("@", 1)[0]
        if token.startswith("odg_"):
            symbols.add(token)
    return symbols


def main() -> int:
    if not LIBRARY.is_file():
        print(f"missing native library: {LIBRARY}", file=sys.stderr)
        return 2
    expected = public_header_symbols()
    actual = dynamic_symbols()
    missing = sorted(expected - actual)
    extra = sorted(actual - expected)
    if missing or extra:
        if missing:
            print("missing public symbols:", ", ".join(missing), file=sys.stderr)
        if extra:
            print("unexpected exported symbols:", ", ".join(extra), file=sys.stderr)
        return 1
    print(f"NATIVE SYMBOL SURFACE OK public={len(expected)} extra=0")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
