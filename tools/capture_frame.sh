#!/bin/sh
set -eu
cc -std=c11 -O2 -fno-builtin -Iengine/include -Iengine/src -Iengine/vendor engine/src/platform.c engine/src/game.c engine/src/sim.c engine/src/render.c engine/vendor/odm_status.c engine/vendor/odm_rng.c tools/capture_frame.c -o build/capture_frame
./build/capture_frame "${1:-build/frame.ppm}" "${2:-1280}" "${3:-720}" "${4:-0}"
