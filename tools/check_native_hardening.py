#!/usr/bin/env python3
"""Verify the security properties promised by the native host build."""

from __future__ import annotations

import pathlib
import subprocess
import sys


ROOT = pathlib.Path(__file__).resolve().parents[1]
LIBRARY = ROOT / "build" / "libodpar_territorial_domain.so"


def output(*args: str) -> str:
    return subprocess.check_output(args, text=True, stderr=subprocess.STDOUT)


def main() -> int:
    if not LIBRARY.is_file():
        print(f"missing native library: {LIBRARY}", file=sys.stderr)
        return 2

    header = output("readelf", "-W", "-h", str(LIBRARY))
    program = output("readelf", "-W", "-l", str(LIBRARY))
    dynamic = output("readelf", "-W", "-d", str(LIBRARY))
    symbols = output("nm", "-D", str(LIBRARY))

    checks = {
        "ELF shared object": "Type:" in header and "DYN" in header,
        "full RELRO segment": "GNU_RELRO" in program,
        "immediate binding": "BIND_NOW" in dynamic or "NOW" in dynamic,
        "non-executable stack": any(
            "GNU_STACK" in line and "E" not in line.split()[-2]
            for line in program.splitlines()
        ),
        "stack protector": "__stack_chk_fail" in symbols,
        "no text relocations": "TEXTREL" not in dynamic,
    }
    failed = [name for name, passed in checks.items() if not passed]
    if failed:
        for name in failed:
            print(f"hardening check failed: {name}", file=sys.stderr)
        return 1
    print("NATIVE HARDENING OK relro=full nx_stack=yes canary=yes textrel=no")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
