#include "odpar_game.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

static int failures = 0;
static uint8_t copied_frame[ODG_MAX_RENDER_PIXELS * 4u];

#define CHECK(expr) do { \
    if (!(expr)) { \
        (void)fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #expr); \
        ++failures; \
    } \
} while (0)

static uint64_t scripted_hash(uint64_t seed, uint32_t width, uint32_t height) {
    uint32_t i;
    CHECK(odg_init(seed, width, height) == ODG_STATUS_OK);
    for (i = 0u; i < 480u; ++i) {
        int32_t x = (i % 120u) < 60u ? 18000 : -22000;
        int32_t z = (i % 180u) < 90u ? 27000 : 15000;
        odg_set_world_input(x, z, 30000, 0, 0, 0u);
        odg_step_ticks(1u);
    }
    return odg_state_hash();
}

int main(void) {
    odg_ffi_abi_info abi;
    odg_game_stats stats;
    uint64_t required = 0u;
    uint64_t landscape_hash;
    uint64_t portrait_hash;
    uint64_t before_resize;
    const uint8_t *native_frame;
    uint32_t frame_bytes;

    CHECK(sizeof(odg_ffi_abi_info) == 64u);
    CHECK(odg_ffi_abi_query(ODG_FFI_ABI_VERSION, NULL, 0u, &required) ==
          ODG_STATUS_BUFFER_TOO_SMALL);
    CHECK(required == sizeof(odg_ffi_abi_info));
    CHECK(odg_ffi_abi_query(ODG_FFI_ABI_VERSION + 1u, &abi, sizeof(abi), &required) ==
          ODG_STATUS_VERSION_MISMATCH);
    CHECK(odg_ffi_abi_query(ODG_FFI_ABI_VERSION, &abi, sizeof(abi), &required) ==
          ODG_STATUS_OK);
    CHECK(abi.struct_size == sizeof(abi));
    CHECK(abi.engine_api_version == ODG_API_VERSION);
    CHECK(abi.endian_marker == ODG_FFI_ENDIAN_MARKER);
    CHECK(abi.game_stats_size == sizeof(odg_game_stats));
    CHECK(abi.max_render_width == 1280u && abi.max_render_height == 1280u);
    CHECK(abi.max_render_pixels == 1280u * 720u);
    CHECK((abi.feature_bits & ODG_FFI_FEATURE_PORTRAIT_RENDER) != 0u);

    CHECK(odg_copy_stats(&stats, sizeof(stats), &required) == ODG_STATUS_INVALID_STATE);
    CHECK(odg_init(UINT64_C(0x504f525452414954), 720u, 1280u) == ODG_STATUS_OK);
    CHECK(odg_render_width() == 720u && odg_render_height() == 1280u);
    CHECK(odg_framebuffer_stride_bytes() == 720u * 4u);
    CHECK(odg_resize(721u, 1280u) == ODG_STATUS_INVALID_ARGUMENT);
    CHECK(odg_resize(1280u, 1280u) == ODG_STATUS_INVALID_ARGUMENT);

    native_frame = (const uint8_t *)odg_render_frame();
    frame_bytes = odg_framebuffer_bytes();
    CHECK(native_frame != NULL);
    CHECK(frame_bytes == ODG_MAX_RENDER_PIXELS * 4u);
    CHECK(odg_copy_framebuffer(NULL, 0u, &required) == ODG_STATUS_BUFFER_TOO_SMALL);
    CHECK(required == frame_bytes);
    CHECK(odg_copy_framebuffer(copied_frame, sizeof(copied_frame), &required) == ODG_STATUS_OK);
    CHECK(memcmp(native_frame, copied_frame, frame_bytes) == 0);
    CHECK(odg_copy_stats(&stats, sizeof(stats), &required) == ODG_STATUS_OK);
    CHECK(stats.struct_size == sizeof(stats));
    CHECK(stats.api_version == ODG_API_VERSION);
    CHECK(stats.width == 720u && stats.height == 1280u);
    CHECK(stats.render_triangles > 0u);
    CHECK(stats.render_pixels_touched > 1000u);

    landscape_hash = scripted_hash(UINT64_C(0x4f5249454e544154), 640u, 360u);
    portrait_hash = scripted_hash(UINT64_C(0x4f5249454e544154), 360u, 640u);
    CHECK(landscape_hash == portrait_hash);
    before_resize = portrait_hash;
    CHECK(odg_resize(720u, 1280u) == ODG_STATUS_OK);
    CHECK(odg_state_hash() == before_resize);
    (void)odg_render_frame();
    CHECK(odg_state_hash() == before_resize);

    if (failures != 0) return 1;
    (void)printf("FFI v%u OK api=%u portrait=720x1280 pixels=%u hash=%016llx\n",
                 ODG_FFI_ABI_VERSION, ODG_API_VERSION, ODG_MAX_RENDER_PIXELS,
                 (unsigned long long)portrait_hash);
    return 0;
}
