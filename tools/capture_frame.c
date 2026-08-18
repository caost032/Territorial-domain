#include "odpar_game.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

int main(int argc, char **argv) {
    const char *path = argc > 1 ? argv[1] : "build/frame.ppm";
    uint32_t width = argc > 2 ? (uint32_t)strtoul(argv[2], NULL, 10) : 480u;
    uint32_t height = argc > 3 ? (uint32_t)strtoul(argv[3], NULL, 10) : 270u;
    uint32_t theme = argc > 4 ? (uint32_t)strtoul(argv[4], NULL, 10) :
                                ODG_VISUAL_THEME_NEON_TIDES;
    uint32_t i;
    FILE *f;
    const uint8_t *p;
    if (width == 0u || height == 0u || width > ODG_MAX_RENDER_WIDTH || height > ODG_MAX_RENDER_HEIGHT) return 4;
    if (theme >= ODG_VISUAL_THEME_COUNT) return 5;
    if (odg_init(UINT64_C(0x5249465444454d4f), width, height) != 0) return 1;
    odg_set_visual_theme(theme);
    /* Walk far enough out of the starting domain to expose live territory/trail. */
    for (i = 0u; i < 126u; ++i) {
        odg_set_input(9000, 30000, 0, 0, 0u);
        odg_step_ticks(1u);
    }
    p = (const uint8_t *)odg_render_frame();
    f = fopen(path, "wb");
    if (!f) return 2;
    fprintf(f, "P6\n%u %u\n255\n", odg_render_width(), odg_render_height());
    for (i = 0u; i < odg_render_width() * odg_render_height(); ++i) fwrite(p + i * 4u, 1u, 3u, f);
    fclose(f);
    printf("capture %ux%u theme=%u cells=%u trail=%u alive=%u\n",
           odg_render_width(), odg_render_height(), theme, odg_player_territory_cells(),
           odg_player_trail_cells(), odg_alive_count());
    return 0;
}
