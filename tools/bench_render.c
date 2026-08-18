#define _POSIX_C_SOURCE 200809L
#include "odpar_game.h"

#include <stdint.h>
#include <stdio.h>
#include <time.h>

static double now_s(void) {
    struct timespec t;
    (void)clock_gettime(CLOCK_MONOTONIC, &t);
    return (double)t.tv_sec + (double)t.tv_nsec / 1e9;
}

static int run_bench(uint32_t width, uint32_t height, uint32_t frames) {
    uint32_t i;
    double a;
    double b;
    volatile uintptr_t sink = 0u;
    /* Keep one world/input path at every raster size so the benchmark measures
     * presentation cost instead of accidentally comparing different generated maps. */
    if (odg_init(UINT64_C(0x42454e43484f4450), width, height) != 0) return 1;
    odg_set_visual_theme(ODG_VISUAL_THEME_NEON_TIDES);
    for (i = 0u; i < 720u; ++i) {
        int32_t mx = (i % 240u < 120u) ? 18000 : -18000;
        odg_set_input(mx, 25000, 0, 0, 0u);
        odg_step_ticks(1u);
        if (odg_match_over() != 0u) break;
    }
    a = now_s();
    for (i = 0u; i < frames; ++i) {
        odg_step_ticks(2u);
        sink ^= odg_render_frame();
    }
    b = now_s();
    printf("render=%ux%u frames=%u seconds=%.6f ms_per_frame=%.3f fps_equiv=%.1f sink=%llu\n",
           width, height, frames, b - a, (b - a) * 1000.0 / (double)frames,
           (double)frames / (b - a), (unsigned long long)sink);
    return 0;
}

int main(void) {
    if (run_bench(480u, 270u, 180u) != 0) return 1;
    if (run_bench(960u, 540u, 60u) != 0) return 2;
    if (run_bench(1280u, 720u, 24u) != 0) return 3;
    if (run_bench(540u, 960u, 36u) != 0) return 4;
    if (run_bench(720u, 1280u, 18u) != 0) return 5;
    return 0;
}
