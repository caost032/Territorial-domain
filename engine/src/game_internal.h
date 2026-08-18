#ifndef ODPAR_GAME_INTERNAL_H
#define ODPAR_GAME_INTERNAL_H

#include "odpar_game.h"
#include "odm_rng.h"

#include <stddef.h>
#include <stdint.h>

#define ODG_FX_SHIFT 10
#define ODG_FX_ONE ((int32_t)1 << ODG_FX_SHIFT)
#define ODG_Q15_ONE 32767
#define ODG_GRID_SHIFT 7u
#define ODG_GRID_SIZE (1u << ODG_GRID_SHIFT)
#define ODG_CELL_COUNT (ODG_GRID_SIZE * ODG_GRID_SIZE)
#define ODG_CELL_FX ODG_FX_ONE
#define ODG_WORLD_HALF_CELLS ((int32_t)(ODG_GRID_SIZE / 2u))
#define ODG_WORLD_HALF_FX (ODG_WORLD_HALF_CELLS * ODG_FX_ONE)
#define ODG_MAX_ACTORS 10u
#define ODG_BOT_COUNT (ODG_MAX_ACTORS - 1u)
#define ODG_MAX_OBSTACLES 24u
#define ODG_MAX_PARTICLES 220u
#define ODG_MAX_LEADERS 8u
#define ODG_MAX_TRAIL_POINTS ODG_CELL_COUNT
#define ODG_MAX_TURRETS 14u
#define ODG_MAX_CHIPS 8u
#define ODG_BOT_TRAIL_SOFT_LIMIT 36u
#define ODG_PLAYER_ID 0u
#define ODG_PLAYER_SPEED_FX 60
#define ODG_BOT_SPEED_FX 47
#define ODG_PLAYER_INPUT_DEADZONE 3000
#define ODG_PLAYER_TURN_MAX_SIN_Q15 4300
#define ODG_BOT_TURN_MAX_SIN_Q15 2100
#define ODG_CAMERA_TURN_MAX_SIN_Q15 2500
#define ODG_PLAYER_TURN_ACCEL_Q15 9200
#define ODG_BOT_TURN_ACCEL_Q15 5000
#define ODG_CAMERA_TURN_ACCEL_Q15 5600
#define ODG_PLAYER_MOVE_TURN_MAX_SIN_Q15 9000
#define ODG_BOT_MOVE_TURN_MAX_SIN_Q15 3600
#define ODG_BOT_STEER_COMMIT_TICKS 24u
#define ODG_SLIDE_LOCK_TICKS 18u
#define ODG_CONTACT_STEER_MIN_DOT_Q15 5600
#define ODG_BOT_PROGRESS_WINDOW_TICKS 72u
#define ODG_BOT_PROGRESS_MIN_FX 180
#define ODG_CAMERA_DISTANCE_FX 2860
#define ODG_CAMERA_MIN_DISTANCE_FX 1420
#define ODG_CAMERA_COLLISION_RADIUS_FX 150
#define ODG_CAMERA_PLAYER_HEIGHT_FX 1750
#define ODG_CAMERA_GROUND_CLEARANCE_FX 1010
#define ODG_CAMERA_LOOK_DEADZONE 900
#define ODG_CAMERA_LOOK_MAX_SIN_Q15 2050
#define ODG_CAMERA_MANUAL_HOLD_TICKS 96u
#define ODG_CAMERA_PITCH_DEFAULT_Q15 6000
#define ODG_CAMERA_PITCH_MIN_Q15 3600
#define ODG_CAMERA_PITCH_MAX_Q15 12500
#define ODG_CAMERA_PITCH_STEP_Q15 260
/* ~2.4 degrees. Small intentional stick changes must not be quantized away. */
#define ODG_INPUT_REBASE_DOT_Q15 32738
#define ODG_DASH_SPEED_NUM 15
#define ODG_DASH_SPEED_DEN 10
#define ODG_DASH_COOLDOWN_TICKS (2u * ODG_TICK_RATE)
#define ODG_DASH_DURATION_TICKS 18u
#define ODG_MAX_STEP_US 50000u
#define ODG_TICK_US_NUM 1000000u
#define ODG_RENDER_PIXELS_MAX ODG_MAX_RENDER_PIXELS
#define ODG_CAPTURE_WIN_PERMILLE 550u
#define ODG_ACTOR_PLAYER 1u
#define ODG_ACTOR_BOT 2u
#define ODG_BOT_INSIDE 0u
#define ODG_BOT_OUTBOUND 1u
#define ODG_BOT_SIDELEG 2u
#define ODG_BOT_RETURN 3u
#define ODG_OWNER_NONE 0u
#define ODG_OWNER_FROM_ID(id) ((uint8_t)((id) + 1u))
#define ODG_ID_FROM_OWNER(owner) ((uint32_t)((owner) - 1u))
#define ODG_TURRET_NEUTRAL 0u
#define ODG_TURRET_NONE UINT32_MAX
#define ODG_TURRET_AMMO 48u
#define ODG_TURRET_FIRE_PERIOD 360u
#define ODG_TURRET_FIRE_JITTER 144u
#define ODG_TURRET_LOCK_TICKS 240u
#define ODG_TURRET_LOCK_JITTER 120u
#define ODG_TURRET_TRAIL_MIN_CELLS 4u
#define ODG_TURRET_RANGE_FX (13 * ODG_FX_ONE)
#define ODG_TURRET_CAPTURE_RADIUS 2
#define ODG_INITIAL_AMMO_CRATES 14u
#define ODG_MAX_AMMO_CRATES 24u
#define ODG_AMMO_CRATE_MIN 10u
#define ODG_AMMO_CRATE_SPAN 15u
#define ODG_AMMO_PICKUP_RANGE_FX (13 * ODG_FX_ONE / 10)
#define ODG_AMMO_DELIVERY_RANGE_FX (3 * ODG_FX_ONE)
#define ODG_CAPTURE_CELLS_PER_AMMO 10u
#define ODG_AMMO_RESERVE_MAX 96u
#define ODG_TURRET_TARGET_NONE 0u
#define ODG_TURRET_TARGET_TRAIL 1u
#define ODG_TURRET_TARGET_TERRITORY 2u
#define ODG_TURRET_MIN_TARGET_FX (2 * ODG_FX_ONE)
#define ODG_TURRET_RETARGET_GRACE_TICKS 42u
#define ODG_TURRET_AMMO_LABEL_RANGE_FX (10 * ODG_FX_ONE)
#define ODG_CHIP_KIND_REPROGRAM 1u
#define ODG_CHIP_PICKUP_RANGE_FX (13 * ODG_FX_ONE / 10)
#define ODG_CHIP_HACK_RANGE_FX (23 * ODG_FX_ONE / 10)

typedef struct {
    uint32_t active;
    uint32_t type;
    uint32_t id;
    uint32_t name_code;
    int32_t x, z;
    int32_t vx, vz;
    int32_t face_x_q15, face_z_q15;
    int32_t radius;
    uint32_t hp, max_hp;
    uint32_t level;
    uint32_t score;
    uint32_t kills;
    uint32_t deaths;
    uint32_t dash_cd;
    uint32_t dash_ticks;
    uint32_t flash_ticks;
    uint32_t death_reason;
    uint32_t trail_active;
    uint32_t trail_len;
    uint32_t last_cell;
    uint32_t home_cell;
    uint32_t think_cd;
    uint32_t bot_mode;
    uint32_t bot_leg_target;
    int32_t ai_x_q15, ai_z_q15;
    int32_t bot_out_x_q15, bot_out_z_q15;
    int32_t turn_sign;
    int32_t turn_rate_q15;
    int32_t speed_fx;
    int32_t steer_q15;
    int32_t control_raw_x_q15;
    int32_t control_raw_z_q15;
    uint32_t ai_commit_ticks;
    uint32_t ai_plan_cell;
    uint32_t slide_lock_ticks;
    uint32_t slide_axis;
    int32_t slide_dir_x_q15, slide_dir_z_q15;
    int32_t progress_x, progress_z;
    uint32_t progress_ticks;
    uint32_t stuck_windows;
    uint32_t carried_ammo_crate;
    uint32_t ammo_reserve;
    uint32_t capture_ammo_credit;
    uint32_t carried_chip;
    odm_rng rng;
} odg_actor;

typedef struct { int32_t x, z, hx, hz, height_fx; uint32_t palette; } odg_obstacle;
typedef struct { uint32_t active; int32_t x,z,vx,vz,y_fx,vy_fx; uint32_t life,color; } odg_particle;
typedef struct {
    int32_t move_x_q15, move_z_q15;
    int32_t move_strength_q15;
    int32_t aim_x_q15, aim_z_q15;
    uint32_t buttons;
    uint32_t world_heading_mode;
} odg_input;
typedef struct {
    uint32_t active;
    uint32_t id;
    uint8_t owner;
    uint8_t pad0, pad1, pad2;
    int32_t x, z;
    uint32_t ammo;
    uint32_t max_ammo;
    uint32_t fire_cd;
    uint32_t fire_period;
    int32_t range_fx;
    uint32_t carried_by;
    uint32_t shots_fired;
    uint32_t cells_conquered;
    uint32_t last_target_cell;
    uint32_t beam_ticks;
    uint32_t target_kind;
    uint32_t aim_ticks;
    uint32_t aim_required;
    uint32_t target_actor_id;
    uint32_t retarget_cd;
    int32_t head_x_q15, head_z_q15;
    int32_t head_turn_rate_q15;
    int32_t head_turn_sign;
} odg_turret;

typedef struct {
    uint32_t active;
    uint32_t id;
    int32_t x, z;
    uint32_t ammo;
    uint32_t carried_by;
    uint32_t pickup_cd;
} odg_ammo_crate;

typedef struct {
    uint32_t active;
    uint32_t id;
    uint32_t kind;
    int32_t x, z;
    uint32_t carried_by;
    uint32_t pickup_cd;
} odg_chip;

typedef struct {
    uint32_t initialized;
    uint32_t width, height;
    uint64_t seed;
    uint64_t tick;
    uint64_t tick_accum_scaled;
    odm_rng rng;
    odg_input input;
    uint32_t prev_buttons;
    odg_actor actors[ODG_MAX_ACTORS];
    odg_obstacle obstacles[ODG_MAX_OBSTACLES];
    odg_particle particles[ODG_MAX_PARTICLES];
    odg_turret turrets[ODG_MAX_TURRETS];
    odg_ammo_crate ammo_crates[ODG_MAX_AMMO_CRATES];
    odg_chip chips[ODG_MAX_CHIPS];
    uint32_t obstacle_count;
    uint32_t turret_count;
    uint32_t ammo_crate_count;
    uint32_t chip_count;
    uint32_t player_carried_turret;
    uint8_t playable[ODG_CELL_COUNT];
    uint8_t bot_nav_edges[ODG_CELL_COUNT]; /* 1=L 2=R 4=-Z 8=+Z, derived at round build */
    uint8_t territory[ODG_CELL_COUNT];
    uint8_t trail_owner[ODG_CELL_COUNT];
    uint8_t flood_seen[ODG_CELL_COUNT];
    uint16_t flood_queue[ODG_CELL_COUNT];
    uint32_t playable_count;
    uint32_t territory_count[ODG_MAX_ACTORS];
    int32_t camera_dir_x_q15;
    int32_t camera_dir_z_q15;
    int32_t camera_anchor_x;
    int32_t camera_anchor_z;
    int32_t camera_yaw_turn_sign;
    int32_t camera_turn_rate_q15;
    uint32_t camera_manual_ticks;
    int32_t camera_height_fx;
    int32_t camera_distance_fx;
    int32_t camera_pitch_q15;
    int32_t control_basis_x_q15;
    int32_t control_basis_z_q15;
    int32_t control_heading_x_q15;
    int32_t control_heading_z_q15;
    int32_t control_strength_q15;
    uint32_t control_active;
    uint32_t match_over;
    uint32_t winner_id;
    odg_game_stats stats;
    uint32_t visual_theme; /* presentation-only: excluded from state hash */
    uint32_t presentation_mode; /* menu/showcase camera only; excluded from state hash */
    /* Non-authoritative AI pathfinding scratch; excluded from odg_state_hash. */
    uint16_t bot_parent[ODG_CELL_COUNT];
    odg_leader_entry leaders[ODG_MAX_LEADERS];
    uint32_t leader_count;
    uint32_t render_triangles;
    uint32_t render_pixels_touched;
} odg_world;

extern odg_world g_odg;
extern uint8_t g_odg_framebuffer[ODG_RENDER_PIXELS_MAX * 4u];
extern uint16_t g_odg_depth[ODG_RENDER_PIXELS_MAX];
void *odg_memset(void *dst, int value, size_t n);
void *odg_memcpy(void *dst, const void *src, size_t n);
static inline int32_t odg_abs_i32(int32_t v) { return v < 0 ? -v : v; }
static inline int32_t odg_clamp_i32(int32_t v, int32_t lo, int32_t hi) { return v < lo ? lo : (v > hi ? hi : v); }
static inline uint32_t odg_min_u32(uint32_t a, uint32_t b) { return a < b ? a : b; }
static inline float odg_fx_to_float(int32_t v) { return (float)v / (float)ODG_FX_ONE; }
static inline int64_t odg_dist2(int32_t ax, int32_t az, int32_t bx, int32_t bz) { int64_t dx=(int64_t)ax-bx,dz=(int64_t)az-bz; return dx*dx+dz*dz; }
uint32_t odg_rand_bounded(odm_rng *rng, uint32_t bound);
int32_t odg_rand_range_fx(odm_rng *rng, int32_t lo, int32_t hi);
void odg_normalize_q15(int32_t x, int32_t z, int32_t *out_x, int32_t *out_z);
uint32_t odg_isqrt_u64(uint64_t v);
uint32_t odg_cell_from_world(int32_t x, int32_t z);
int32_t odg_terrain_height_fx(int32_t x, int32_t z);
int32_t odg_cell_center_x(uint32_t cell);
int32_t odg_cell_center_z(uint32_t cell);
void odg_world_build(uint64_t seed);
void odg_sim_step(void);
void odg_render_internal(void);
void odg_rebuild_stats(void);
void odg_emit_particles(int32_t x, int32_t z, uint32_t color, uint32_t count);
void odg_update_turret_ownership_internal(void);
int odg_turret_drop_candidate_internal(const odg_actor *p, int32_t *out_x, int32_t *out_z);

#endif
