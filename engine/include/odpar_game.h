#ifndef ODPAR_GAME_H
#define ODPAR_GAME_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define ODG_API_VERSION UINT32_C(14)
#define ODG_MAX_RENDER_WIDTH UINT32_C(1280)
#define ODG_MAX_RENDER_HEIGHT UINT32_C(1280)
#define ODG_MAX_RENDER_PIXELS (UINT32_C(1280) * UINT32_C(720))
#define ODG_TICK_RATE UINT32_C(120)

/* Stable host-discovery contract. The game API and the FFI schema evolve
 * independently: hosts query both before reading any POD snapshot. */
#define ODG_FFI_ABI_VERSION UINT32_C(1)
#define ODG_FFI_ENDIAN_MARKER UINT32_C(0x01020304)
#define ODG_PIXEL_FORMAT_RGBA8 UINT32_C(1)
#define ODG_FFI_FEATURE_FRAMEBUFFER_PTR  (UINT64_C(1) << 0)
#define ODG_FFI_FEATURE_FRAMEBUFFER_COPY (UINT64_C(1) << 1)
#define ODG_FFI_FEATURE_STATS_PTR        (UINT64_C(1) << 2)
#define ODG_FFI_FEATURE_STATS_COPY       (UINT64_C(1) << 3)
#define ODG_FFI_FEATURE_PORTRAIT_RENDER  (UINT64_C(1) << 4)
#define ODG_FFI_FEATURE_FIXED_120HZ      (UINT64_C(1) << 5)
#define ODG_FFI_FEATURE_CAMERA_INPUT     (UINT64_C(1) << 6)

#define ODG_VISUAL_THEME_NEON_TIDES     UINT32_C(0)
#define ODG_VISUAL_THEME_EMERALD_CROWN  UINT32_C(1)
#define ODG_VISUAL_THEME_SOLAR_EMBER    UINT32_C(2)
#define ODG_VISUAL_THEME_OBSIDIAN_PULSE UINT32_C(3)
#define ODG_VISUAL_THEME_COUNT          UINT32_C(4)

#define ODG_PRESENTATION_GAMEPLAY UINT32_C(0)
#define ODG_PRESENTATION_SHOWCASE UINT32_C(1)

/* FIRE remains reserved for ABI compatibility. Territory gameplay does not damage actors. */
#define ODG_BUTTON_FIRE    (UINT32_C(1) << 0)
#define ODG_BUTTON_DASH    (UINT32_C(1) << 1)
#define ODG_BUTTON_RESTART (UINT32_C(1) << 2)
#define ODG_BUTTON_ACTION  (UINT32_C(1) << 3) /* contextual turret / chip action */
#define ODG_BUTTON_DROP    (UINT32_C(1) << 4) /* drop currently carried world item */

#define ODG_STATUS_OK 0
#define ODG_STATUS_INVALID_ARGUMENT 1
#define ODG_STATUS_INVALID_STATE 2
#define ODG_STATUS_UNSUPPORTED 3
#define ODG_STATUS_BUFFER_TOO_SMALL 4
#define ODG_STATUS_VERSION_MISMATCH 5

#define ODG_DEATH_NONE 0u
#define ODG_DEATH_TRAIL_CUT 1u
#define ODG_DEATH_SELF_CROSS 2u /* legacy; self trails are non-lethal in API v8 */
#define ODG_DEATH_BOUNDARY 3u
#define ODG_DEATH_TERRITORY_LOST 4u
#define ODG_DEATH_TURRET_TRAIL_CUT 5u

/* Stable, POD-only snapshot for FFI/WASM/native hosts. */
typedef struct {
    uint32_t struct_size;
    uint32_t api_version;
    uint64_t tick;
    uint64_t match_seed;
    uint32_t width;
    uint32_t height;
    uint32_t alive_count;
    uint32_t player_alive;
    uint32_t player_health;      /* compatibility: 1 alive, 0 eliminated */
    uint32_t player_max_health;  /* compatibility: 1 */
    uint32_t player_level;       /* coverage tier */
    uint32_t player_score;       /* territory cells */
    uint32_t player_kills;       /* territorial defeats */
    uint32_t player_deaths;
    uint32_t zone_radius_milli;  /* compatibility: world half-size */
    uint32_t simulation_hz;
    uint32_t render_triangles;
    uint32_t render_pixels_touched;
    uint64_t deterministic_state_hash;
    uint32_t territory_cells;
    uint32_t territory_total_cells; /* playable cells, not bounding square */
    uint32_t territory_permille;
    uint32_t player_trail_cells;
    uint32_t player_trail_active;
    uint32_t match_over;
    uint32_t winner_id;
    uint32_t player_death_reason;
    uint32_t turret_total;
    uint32_t player_owned_turrets;
    uint32_t player_carrying_turret;
    uint32_t carried_turret_ammo;
    uint32_t turret_action_available;
    uint32_t ammo_crates_total;
    uint32_t player_carrying_ammo_crate;
    uint32_t player_carried_ammo;
    uint32_t player_ammo_reserve;
    uint32_t chips_total;
    uint32_t player_carrying_chip;
    uint32_t player_chip_kind;
    uint32_t hack_action_available;
    uint32_t drop_action_available;
    uint32_t nearby_owned_turret_visible;
    uint32_t nearby_owned_turret_ammo;
    uint32_t nearby_owned_turret_max_ammo;
} odg_game_stats;

typedef struct {
    uint32_t actor_id;
    uint32_t score;
    uint32_t level;
    uint32_t alive;
    uint32_t is_player;
    uint32_t name_code;
} odg_leader_entry;

/* Frozen at 64 bytes for FFI ABI v1. It contains no pointers, size_t values,
 * native enums or floating-point fields. */
typedef struct {
    uint32_t struct_size;
    uint32_t ffi_abi_version;
    uint32_t engine_api_version;
    uint32_t endian_marker;
    uint32_t game_stats_size;
    uint32_t leader_entry_size;
    uint32_t tick_rate;
    uint32_t max_render_width;
    uint32_t max_render_height;
    uint32_t max_render_pixels;
    uint32_t framebuffer_pixel_format;
    uint32_t framebuffer_bytes_per_pixel;
    uint64_t feature_bits;
    uint64_t reserved_u64;
} odg_ffi_abi_info;

uint32_t odg_api_version(void);
int32_t odg_ffi_abi_query(uint32_t requested_ffi_abi,
                          odg_ffi_abi_info *out_info,
                          uint64_t capacity,
                          uint64_t *out_required);
int32_t odg_init(uint64_t seed, uint32_t width, uint32_t height);
int32_t odg_resize(uint32_t width, uint32_t height);
void odg_set_visual_theme(uint32_t theme);
uint32_t odg_visual_theme(void);
void odg_set_presentation_mode(uint32_t mode);
uint32_t odg_presentation_mode(void);
void odg_reset(uint64_t seed);
void odg_set_input(int32_t move_x_q15, int32_t move_y_q15,
                   int32_t aim_x_q15, int32_t aim_y_q15,
                   uint32_t buttons);
/* Exact native/app world-heading path for replays, AI drivers and specialized hosts.
 * Normal gameplay should use odg_set_input(): its move vector is camera-local and the
 * look vector is independent. */
void odg_set_world_input(int32_t world_x_q15, int32_t world_z_q15,
                         int32_t strength_q15,
                         int32_t aim_x_q15, int32_t aim_y_q15,
                         uint32_t buttons);
void odg_tick_us(uint32_t elapsed_us);
void odg_step_ticks(uint32_t ticks);

uintptr_t odg_render_frame(void);
uintptr_t odg_framebuffer_ptr(void);
uint32_t odg_framebuffer_bytes(void);
uint32_t odg_framebuffer_stride_bytes(void);
int32_t odg_copy_framebuffer(uint8_t *out_rgba,
                             uint64_t capacity,
                             uint64_t *out_required);
uint32_t odg_render_width(void);
uint32_t odg_render_height(void);

const odg_game_stats *odg_stats(void);
uintptr_t odg_stats_ptr(void);
int32_t odg_copy_stats(odg_game_stats *out_stats,
                       uint64_t capacity,
                       uint64_t *out_required);
uint32_t odg_player_health(void);
uint32_t odg_player_max_health(void);
uint32_t odg_player_score(void);
uint32_t odg_player_level(void);
uint32_t odg_player_kills(void);
uint32_t odg_player_deaths(void);
uint32_t odg_alive_count(void);
uint32_t odg_zone_radius_milli(void);
uint64_t odg_state_hash(void);

uint32_t odg_territory_total_cells(void);
uint32_t odg_player_territory_cells(void);
uint32_t odg_player_territory_permille(void);
uint32_t odg_player_trail_cells(void);
uint32_t odg_player_trail_active(void);
uint32_t odg_match_over(void);
uint32_t odg_winner_id(void);
uint32_t odg_player_death_reason(void);
uint32_t odg_turret_count(void);
uint32_t odg_player_owned_turrets(void);
uint32_t odg_player_carrying_turret(void);
uint32_t odg_player_carried_turret_ammo(void);
uint32_t odg_player_turret_action_available(void);
uint32_t odg_ammo_crate_count(void);
uint32_t odg_player_carrying_ammo_crate(void);
uint32_t odg_player_carried_ammo(void);
uint32_t odg_player_ammo_reserve(void);
uint32_t odg_chip_count(void);
uint32_t odg_player_carrying_chip(void);
uint32_t odg_player_chip_kind(void);
uint32_t odg_player_hack_action_available(void);
uint32_t odg_player_drop_action_available(void);
uint32_t odg_player_nearby_owned_turret_visible(void);
uint32_t odg_player_nearby_owned_turret_ammo(void);
uint32_t odg_player_nearby_owned_turret_max_ammo(void);
int32_t odg_player_facing_x_q15(void);
int32_t odg_player_facing_z_q15(void);
int32_t odg_camera_dir_x_q15(void);
int32_t odg_camera_dir_z_q15(void);
int32_t odg_control_basis_x_q15(void);
int32_t odg_control_basis_z_q15(void);
int32_t odg_control_heading_x_q15(void);
int32_t odg_control_heading_z_q15(void);
int32_t odg_control_local_x_q15(void);
int32_t odg_control_local_z_q15(void);
int32_t odg_control_strength_q15(void);

uint32_t odg_leader_count(void);
int32_t odg_leader_get(uint32_t rank, odg_leader_entry *out_entry);
uint32_t odg_leader_score(uint32_t rank);
uint32_t odg_leader_name_code(uint32_t rank);
uint32_t odg_leader_is_player(uint32_t rank);

#ifdef __cplusplus
}
#endif

#endif
