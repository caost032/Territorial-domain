#include "game_internal.h"
#include "odm_status.h"

#include <stdint.h>

odg_world g_odg;
uint8_t g_odg_framebuffer[ODG_RENDER_PIXELS_MAX * 4u];
uint16_t g_odg_depth[ODG_RENDER_PIXELS_MAX];

_Static_assert(sizeof(odg_ffi_abi_info) == 64u,
               "ODG FFI ABI v1 discovery structure must remain 64 bytes");
_Static_assert(offsetof(odg_ffi_abi_info, feature_bits) == 48u,
               "ODG FFI ABI v1 feature offset changed");

static int odg_render_size_valid(uint32_t width, uint32_t height) {
    uint64_t pixels;
    if (width == 0u || height == 0u ||
        width > ODG_MAX_RENDER_WIDTH || height > ODG_MAX_RENDER_HEIGHT) return 0;
    pixels = (uint64_t)width * (uint64_t)height;
    return pixels <= (uint64_t)ODG_MAX_RENDER_PIXELS;
}

uint32_t odg_rand_bounded(odm_rng *rng, uint32_t bound) {
    uint32_t v = 0u;
    if (bound == 0u) return 0u;
    if (odm_rng_bounded_u32(rng, bound, &v) != ODM_STATUS_OK) return 0u;
    return v;
}

int32_t odg_rand_range_fx(odm_rng *rng, int32_t lo, int32_t hi) {
    uint32_t span;
    if (hi <= lo) return lo;
    span = (uint32_t)(hi - lo);
    return lo + (int32_t)odg_rand_bounded(rng, span);
}

uint32_t odg_isqrt_u64(uint64_t value) {
    uint64_t result = 0u;
    uint64_t bit = UINT64_C(1) << 62;
    while (bit > value) bit >>= 2u;
    while (bit != 0u) {
        if (value >= result + bit) {
            value -= result + bit;
            result = (result >> 1u) + bit;
        } else {
            result >>= 1u;
        }
        bit >>= 2u;
    }
    return (uint32_t)result;
}

static int32_t terrain_hill_fx(int32_t x, int32_t z, int32_t cx_m, int32_t cz_m,
                               int32_t radius_m, int32_t height_fx) {
    int64_t dx=(int64_t)x-(int64_t)cx_m*ODG_FX_ONE;
    int64_t dz=(int64_t)z-(int64_t)cz_m*ODG_FX_ONE;
    uint64_t d2=(uint64_t)(dx*dx+dz*dz);
    int64_t r=(int64_t)radius_m*ODG_FX_ONE;
    uint64_t r2=(uint64_t)(r*r);
    uint64_t remain;
    uint32_t q15;
    if (d2>=r2) return 0;
    remain=r2-d2;
    /* Squared radial falloff avoids a sqrt for every visual terrain sample while keeping
     * a smooth broad hill. Q15 staging also keeps all intermediate products bounded. */
    q15=(uint32_t)((remain*(uint64_t)ODG_Q15_ONE)/r2);
    return (int32_t)(((int64_t)height_fx*q15*q15)/((int64_t)ODG_Q15_ONE*ODG_Q15_ONE));
}

int32_t odg_terrain_height_fx(int32_t x, int32_t z) {
    int32_t h=150;
    /* The authoritative territory remains a 2D topology, but the physical presentation
     * now has enough relief to matter: broad hills, saddles and shallow valleys produce
     * readable slopes without introducing cliff-like grid discontinuities. Everything is
     * integer/deterministic so native and WASM see the same surface. */
    h += terrain_hill_fx(x,z,-31, -9,36,1550);
    h += terrain_hill_fx(x,z, 28, 19,33,1360);
    h += terrain_hill_fx(x,z,  5,-38,29,1120);
    h += terrain_hill_fx(x,z, -6, 41,27,1260);
    h += terrain_hill_fx(x,z, 44,-17,23, 820);
    h += terrain_hill_fx(x,z,-45, 25,21, 690);
    h += terrain_hill_fx(x,z,  4,  4,20,-520);
    h += terrain_hill_fx(x,z,-18, 29,14,-310);
    h += terrain_hill_fx(x,z, 31,-29,16,-280);
    if (h<80) h=80;
    if (h>2650) h=2650;
    return h;
}

void odg_normalize_q15(int32_t x, int32_t z, int32_t *out_x, int32_t *out_z) {
    uint64_t m2;
    uint32_t m;
    int64_t sx;
    int64_t sz;
    if (!out_x || !out_z) return;
    m2 = (uint64_t)((int64_t)x * x + (int64_t)z * z);
    if (m2 == 0u) { *out_x = 0; *out_z = 0; return; }
    m = odg_isqrt_u64(m2);
    if (m == 0u) { *out_x = 0; *out_z = 0; return; }
    sx = ((int64_t)x * ODG_Q15_ONE) / (int64_t)m;
    sz = ((int64_t)z * ODG_Q15_ONE) / (int64_t)m;
    *out_x = odg_clamp_i32((int32_t)sx, -ODG_Q15_ONE, ODG_Q15_ONE);
    *out_z = odg_clamp_i32((int32_t)sz, -ODG_Q15_ONE, ODG_Q15_ONE);
}

uint32_t odg_api_version(void) { return ODG_API_VERSION; }

int32_t odg_ffi_abi_query(uint32_t requested_ffi_abi,
                          odg_ffi_abi_info *out_info,
                          uint64_t capacity,
                          uint64_t *out_required) {
    odg_ffi_abi_info info;
    if (!out_required) return ODG_STATUS_INVALID_ARGUMENT;
    *out_required = (uint64_t)sizeof(info);
    if (requested_ffi_abi != ODG_FFI_ABI_VERSION) return ODG_STATUS_VERSION_MISMATCH;
    if (!out_info || capacity < (uint64_t)sizeof(info)) return ODG_STATUS_BUFFER_TOO_SMALL;
    odg_memset(&info, 0, sizeof(info));
    info.struct_size = (uint32_t)sizeof(info);
    info.ffi_abi_version = ODG_FFI_ABI_VERSION;
    info.engine_api_version = ODG_API_VERSION;
    info.endian_marker = ODG_FFI_ENDIAN_MARKER;
    info.game_stats_size = (uint32_t)sizeof(odg_game_stats);
    info.leader_entry_size = (uint32_t)sizeof(odg_leader_entry);
    info.tick_rate = ODG_TICK_RATE;
    info.max_render_width = ODG_MAX_RENDER_WIDTH;
    info.max_render_height = ODG_MAX_RENDER_HEIGHT;
    info.max_render_pixels = ODG_MAX_RENDER_PIXELS;
    info.framebuffer_pixel_format = ODG_PIXEL_FORMAT_RGBA8;
    info.framebuffer_bytes_per_pixel = 4u;
    info.feature_bits = ODG_FFI_FEATURE_FRAMEBUFFER_PTR |
                        ODG_FFI_FEATURE_FRAMEBUFFER_COPY |
                        ODG_FFI_FEATURE_STATS_PTR |
                        ODG_FFI_FEATURE_STATS_COPY |
                        ODG_FFI_FEATURE_PORTRAIT_RENDER |
                        ODG_FFI_FEATURE_FIXED_120HZ |
                        ODG_FFI_FEATURE_CAMERA_INPUT;
    *out_info = info;
    return ODG_STATUS_OK;
}

int32_t odg_init(uint64_t seed, uint32_t width, uint32_t height) {
    if (!odg_render_size_valid(width, height)) return ODG_STATUS_INVALID_ARGUMENT;
    odg_memset(&g_odg, 0, sizeof(g_odg));
    g_odg.visual_theme = ODG_VISUAL_THEME_NEON_TIDES;
    g_odg.presentation_mode = ODG_PRESENTATION_GAMEPLAY;
    g_odg.initialized = 1u;
    g_odg.width = width;
    g_odg.height = height;
    odg_world_build(seed == 0u ? UINT64_C(0x4f44504152524946) : seed);
    odg_rebuild_stats();
    return ODG_STATUS_OK;
}

int32_t odg_resize(uint32_t width, uint32_t height) {
    if (!g_odg.initialized) return ODG_STATUS_INVALID_STATE;
    if (!odg_render_size_valid(width, height)) return ODG_STATUS_INVALID_ARGUMENT;
    g_odg.width = width;
    g_odg.height = height;
    odg_rebuild_stats();
    return ODG_STATUS_OK;
}

void odg_reset(uint64_t seed) {
    uint32_t w = g_odg.width != 0u ? g_odg.width : 480u;
    uint32_t h = g_odg.height != 0u ? g_odg.height : 270u;
    uint32_t theme = g_odg.visual_theme;
    uint32_t presentation = g_odg.presentation_mode;
    (void)odg_init(seed, w, h);
    g_odg.visual_theme = theme < ODG_VISUAL_THEME_COUNT ? theme : ODG_VISUAL_THEME_NEON_TIDES;
    g_odg.presentation_mode = presentation == ODG_PRESENTATION_SHOWCASE ?
                              ODG_PRESENTATION_SHOWCASE : ODG_PRESENTATION_GAMEPLAY;
}

void odg_set_input(int32_t move_x_q15, int32_t move_y_q15,
                   int32_t aim_x_q15, int32_t aim_y_q15,
                   uint32_t buttons) {
    uint32_t mag;
    if (!g_odg.initialized) return;
    g_odg.input.move_x_q15 = odg_clamp_i32(move_x_q15, -ODG_Q15_ONE, ODG_Q15_ONE);
    g_odg.input.move_z_q15 = odg_clamp_i32(move_y_q15, -ODG_Q15_ONE, ODG_Q15_ONE);
    mag=odg_isqrt_u64((uint64_t)((int64_t)g_odg.input.move_x_q15*g_odg.input.move_x_q15+
                                 (int64_t)g_odg.input.move_z_q15*g_odg.input.move_z_q15));
    g_odg.input.move_strength_q15=(int32_t)(mag>(uint32_t)ODG_Q15_ONE?(uint32_t)ODG_Q15_ONE:mag);
    g_odg.input.aim_x_q15 = odg_clamp_i32(aim_x_q15, -ODG_Q15_ONE, ODG_Q15_ONE);
    g_odg.input.aim_z_q15 = odg_clamp_i32(aim_y_q15, -ODG_Q15_ONE, ODG_Q15_ONE);
    g_odg.input.buttons = buttons;
    g_odg.input.world_heading_mode = 0u;
}

void odg_set_world_input(int32_t world_x_q15, int32_t world_z_q15,
                         int32_t strength_q15,
                         int32_t aim_x_q15, int32_t aim_y_q15,
                         uint32_t buttons) {
    int32_t x=odg_clamp_i32(world_x_q15,-ODG_Q15_ONE,ODG_Q15_ONE);
    int32_t z=odg_clamp_i32(world_z_q15,-ODG_Q15_ONE,ODG_Q15_ONE);
    int32_t strength=odg_clamp_i32(strength_q15,0,ODG_Q15_ONE);
    if (!g_odg.initialized) return;
    if (strength<=ODG_PLAYER_INPUT_DEADZONE || (x==0 && z==0)) {
        x=0;z=0;strength=0;
    } else {
        odg_normalize_q15(x,z,&x,&z);
    }
    g_odg.input.move_x_q15=x;
    g_odg.input.move_z_q15=z;
    g_odg.input.move_strength_q15=strength;
    g_odg.input.aim_x_q15=odg_clamp_i32(aim_x_q15,-ODG_Q15_ONE,ODG_Q15_ONE);
    g_odg.input.aim_z_q15=odg_clamp_i32(aim_y_q15,-ODG_Q15_ONE,ODG_Q15_ONE);
    g_odg.input.buttons=buttons;
    g_odg.input.world_heading_mode=1u;
}

void odg_tick_us(uint32_t elapsed_us) {
    uint64_t scaled;
    uint32_t guard = 0u;
    if (!g_odg.initialized) return;
    if (elapsed_us > ODG_MAX_STEP_US) elapsed_us = ODG_MAX_STEP_US;
    scaled = (uint64_t)elapsed_us * ODG_TICK_RATE;
    g_odg.tick_accum_scaled += scaled;
    while (g_odg.tick_accum_scaled >= ODG_TICK_US_NUM && guard < 8u) {
        odg_sim_step();
        g_odg.tick_accum_scaled -= ODG_TICK_US_NUM;
        ++guard;
    }
    odg_rebuild_stats();
}

void odg_step_ticks(uint32_t ticks) {
    uint32_t i;
    if (!g_odg.initialized) return;
    if (ticks > 1200u) ticks = 1200u;
    for (i = 0u; i < ticks; ++i) odg_sim_step();
    odg_rebuild_stats();
}

uintptr_t odg_render_frame(void) {
    if (!g_odg.initialized) return (uintptr_t)0;
    odg_render_internal();
    odg_rebuild_stats();
    return (uintptr_t)g_odg_framebuffer;
}

uintptr_t odg_framebuffer_ptr(void) { return (uintptr_t)g_odg_framebuffer; }
uint32_t odg_framebuffer_bytes(void) { return g_odg.width * g_odg.height * 4u; }
uint32_t odg_framebuffer_stride_bytes(void) { return g_odg.width * 4u; }
int32_t odg_copy_framebuffer(uint8_t *out_rgba,
                             uint64_t capacity,
                             uint64_t *out_required) {
    uint64_t required;
    if (!out_required) return ODG_STATUS_INVALID_ARGUMENT;
    required = g_odg.initialized ? (uint64_t)odg_framebuffer_bytes() : 0u;
    *out_required = required;
    if (!g_odg.initialized) return ODG_STATUS_INVALID_STATE;
    if (!out_rgba || capacity < required) return ODG_STATUS_BUFFER_TOO_SMALL;
    odg_memcpy(out_rgba, g_odg_framebuffer, (size_t)required);
    return ODG_STATUS_OK;
}
uint32_t odg_render_width(void) { return g_odg.width; }
uint32_t odg_render_height(void) { return g_odg.height; }
void odg_set_visual_theme(uint32_t theme) {
    g_odg.visual_theme = theme < ODG_VISUAL_THEME_COUNT ? theme : ODG_VISUAL_THEME_NEON_TIDES;
}
uint32_t odg_visual_theme(void) {
    return g_odg.visual_theme < ODG_VISUAL_THEME_COUNT ? g_odg.visual_theme : ODG_VISUAL_THEME_NEON_TIDES;
}
void odg_set_presentation_mode(uint32_t mode) {
    g_odg.presentation_mode = mode == ODG_PRESENTATION_SHOWCASE ?
                              ODG_PRESENTATION_SHOWCASE : ODG_PRESENTATION_GAMEPLAY;
}
uint32_t odg_presentation_mode(void) {
    return g_odg.presentation_mode == ODG_PRESENTATION_SHOWCASE ?
           ODG_PRESENTATION_SHOWCASE : ODG_PRESENTATION_GAMEPLAY;
}
const odg_game_stats *odg_stats(void) { return &g_odg.stats; }
uintptr_t odg_stats_ptr(void) { return (uintptr_t)&g_odg.stats; }
int32_t odg_copy_stats(odg_game_stats *out_stats,
                       uint64_t capacity,
                       uint64_t *out_required) {
    const uint64_t required = (uint64_t)sizeof(odg_game_stats);
    if (!out_required) return ODG_STATUS_INVALID_ARGUMENT;
    *out_required = required;
    if (!g_odg.initialized) return ODG_STATUS_INVALID_STATE;
    if (!out_stats || capacity < required) return ODG_STATUS_BUFFER_TOO_SMALL;
    *out_stats = g_odg.stats;
    return ODG_STATUS_OK;
}
uint32_t odg_player_health(void) { return g_odg.actors[0].hp; }
uint32_t odg_player_max_health(void) { return g_odg.actors[0].max_hp; }
uint32_t odg_player_score(void) { return g_odg.territory_count[0]; }
uint32_t odg_player_level(void) { return g_odg.actors[0].level; }
uint32_t odg_player_kills(void) { return g_odg.actors[0].kills; }
uint32_t odg_player_deaths(void) { return g_odg.actors[0].deaths; }
uint32_t odg_zone_radius_milli(void) { return (uint32_t)ODG_WORLD_HALF_CELLS * 1000u; }
uint32_t odg_territory_total_cells(void) { return g_odg.playable_count; }
uint32_t odg_player_territory_cells(void) { return g_odg.territory_count[0]; }
uint32_t odg_player_territory_permille(void) { return g_odg.playable_count != 0u ? (g_odg.territory_count[0] * 1000u) / g_odg.playable_count : 0u; }
uint32_t odg_player_trail_cells(void) { return g_odg.actors[0].trail_len; }
uint32_t odg_player_trail_active(void) { return g_odg.actors[0].trail_active; }
uint32_t odg_match_over(void) { return g_odg.match_over; }
uint32_t odg_winner_id(void) { return g_odg.winner_id; }
uint32_t odg_player_death_reason(void) { return g_odg.actors[0].death_reason; }
uint32_t odg_turret_count(void) { return g_odg.turret_count; }
uint32_t odg_player_owned_turrets(void) {
    uint32_t i, n=0u;
    uint8_t own=ODG_OWNER_FROM_ID(ODG_PLAYER_ID);
    for (i=0u;i<g_odg.turret_count;++i) if (g_odg.turrets[i].active && g_odg.turrets[i].owner==own) ++n;
    return n;
}
uint32_t odg_player_carrying_turret(void) { return g_odg.player_carried_turret < g_odg.turret_count ? 1u : 0u; }
uint32_t odg_player_carried_turret_ammo(void) {
    return g_odg.player_carried_turret < g_odg.turret_count ? g_odg.turrets[g_odg.player_carried_turret].ammo : 0u;
}
uint32_t odg_player_hack_action_available(void) {
    const odg_actor *p=&g_odg.actors[ODG_PLAYER_ID];
    const odg_chip *chip;
    uint32_t i;
    uint8_t own=ODG_OWNER_FROM_ID(ODG_PLAYER_ID);
    if(!g_odg.initialized || !p->active || p->hp==0u ||
       p->carried_chip>=g_odg.chip_count) return 0u;
    chip=&g_odg.chips[p->carried_chip];
    if(!chip->active || chip->kind!=ODG_CHIP_KIND_REPROGRAM ||
       chip->carried_by!=ODG_PLAYER_ID) return 0u;
    for(i=0u;i<g_odg.turret_count;++i){
        const odg_turret *t=&g_odg.turrets[i];
        /* Neutral infrastructure is commissioned by territorial majority. Chips are
         * reserved for already-programmed enemy turrets. */
        if(t->active && t->carried_by==ODG_TURRET_NONE &&
           t->owner!=ODG_TURRET_NEUTRAL && t->owner!=own &&
           odg_dist2(p->x,p->z,t->x,t->z)<=(int64_t)ODG_CHIP_HACK_RANGE_FX*ODG_CHIP_HACK_RANGE_FX) return 1u;
    }
    return 0u;
}
uint32_t odg_player_turret_action_available(void) {
    const odg_actor *p=&g_odg.actors[ODG_PLAYER_ID];
    uint32_t i;
    uint8_t own=ODG_OWNER_FROM_ID(ODG_PLAYER_ID);
    int64_t max_d2=(int64_t)(3*ODG_FX_ONE)*(3*ODG_FX_ONE);
    if(!g_odg.initialized || !p->active || p->hp==0u) return 0u;
    if (odg_player_hack_action_available()) return 1u;
    if (g_odg.player_carried_turret < g_odg.turret_count) return 1u;
    /* Repositioning infrastructure is a domain-only privilege. Being near an owned
     * turret from hostile/neutral ground must not expose a pickup action. */
    { uint32_t pc=odg_cell_from_world(p->x,p->z); if(pc>=ODG_CELL_COUNT || g_odg.territory[pc]!=own) return 0u; }
    for (i=0u;i<g_odg.turret_count;++i) {
        const odg_turret *t=&g_odg.turrets[i];
        uint32_t tc;
        if(!t->active || t->owner!=own || t->carried_by!=ODG_TURRET_NONE) continue;
        tc=odg_cell_from_world(t->x,t->z);
        if(tc>=ODG_CELL_COUNT || g_odg.territory[tc]!=own) continue;
        if(odg_dist2(p->x,p->z,t->x,t->z)<=max_d2) return 1u;
    }
    return 0u;
}
uint32_t odg_player_drop_action_available(void) {
    const odg_actor *p=&g_odg.actors[ODG_PLAYER_ID];
    if(!g_odg.initialized || !p->active || p->hp==0u) return 0u;
    return (g_odg.player_carried_turret<g_odg.turret_count ||
            p->carried_ammo_crate<g_odg.ammo_crate_count ||
            p->carried_chip<g_odg.chip_count ||
            p->ammo_reserve!=0u) ? 1u : 0u;
}
uint32_t odg_chip_count(void) {
    uint32_t i,n=0u;for(i=0u;i<g_odg.chip_count;++i)if(g_odg.chips[i].active)++n;return n;
}
uint32_t odg_player_carrying_chip(void) {return g_odg.actors[ODG_PLAYER_ID].carried_chip<g_odg.chip_count?1u:0u;}
uint32_t odg_player_chip_kind(void) {
    uint32_t i=g_odg.actors[ODG_PLAYER_ID].carried_chip;return i<g_odg.chip_count?g_odg.chips[i].kind:0u;
}
static uint32_t nearby_owned_turret_index(void){
    const odg_actor *p=&g_odg.actors[ODG_PLAYER_ID];uint32_t i,best=UINT32_MAX;int64_t bd=(int64_t)ODG_TURRET_AMMO_LABEL_RANGE_FX*ODG_TURRET_AMMO_LABEL_RANGE_FX;uint8_t own=ODG_OWNER_FROM_ID(ODG_PLAYER_ID);
    for(i=0u;i<g_odg.turret_count;++i){const odg_turret *t=&g_odg.turrets[i];int64_t d;if(!t->active||t->owner!=own||t->carried_by!=ODG_TURRET_NONE)continue;d=odg_dist2(p->x,p->z,t->x,t->z);if(d<=bd){bd=d;best=i;}}
    return best;
}
uint32_t odg_player_nearby_owned_turret_visible(void){return nearby_owned_turret_index()<g_odg.turret_count?1u:0u;}
uint32_t odg_player_nearby_owned_turret_ammo(void){uint32_t i=nearby_owned_turret_index();return i<g_odg.turret_count?g_odg.turrets[i].ammo:0u;}
uint32_t odg_player_nearby_owned_turret_max_ammo(void){uint32_t i=nearby_owned_turret_index();return i<g_odg.turret_count?g_odg.turrets[i].max_ammo:0u;}

uint32_t odg_ammo_crate_count(void) { return g_odg.ammo_crate_count; }
uint32_t odg_player_carrying_ammo_crate(void) { return g_odg.actors[ODG_PLAYER_ID].carried_ammo_crate < g_odg.ammo_crate_count ? 1u : 0u; }
uint32_t odg_player_carried_ammo(void) {
    uint32_t c=g_odg.actors[ODG_PLAYER_ID].carried_ammo_crate;
    return c<g_odg.ammo_crate_count ? g_odg.ammo_crates[c].ammo : 0u;
}
uint32_t odg_player_ammo_reserve(void) { return g_odg.actors[ODG_PLAYER_ID].ammo_reserve; }
int32_t odg_player_facing_x_q15(void) { return g_odg.actors[ODG_PLAYER_ID].face_x_q15; }
int32_t odg_player_facing_z_q15(void) { return g_odg.actors[ODG_PLAYER_ID].face_z_q15; }
int32_t odg_camera_dir_x_q15(void) { return g_odg.camera_dir_x_q15; }
int32_t odg_camera_dir_z_q15(void) { return g_odg.camera_dir_z_q15; }
int32_t odg_control_basis_x_q15(void) { return g_odg.control_basis_x_q15; }
int32_t odg_control_basis_z_q15(void) { return g_odg.control_basis_z_q15; }
int32_t odg_control_heading_x_q15(void) { return g_odg.control_active ? g_odg.control_heading_x_q15 : 0; }
int32_t odg_control_heading_z_q15(void) { return g_odg.control_active ? g_odg.control_heading_z_q15 : 0; }
int32_t odg_control_local_x_q15(void) {
    int64_t v;
    if (!g_odg.control_active) return 0;
    /* camera-right = (camera_z,-camera_x) */
    v=((int64_t)g_odg.control_heading_x_q15*g_odg.camera_dir_z_q15 -
       (int64_t)g_odg.control_heading_z_q15*g_odg.camera_dir_x_q15)/ODG_Q15_ONE;
    return odg_clamp_i32((int32_t)v,-ODG_Q15_ONE,ODG_Q15_ONE);
}
int32_t odg_control_local_z_q15(void) {
    int64_t v;
    if (!g_odg.control_active) return 0;
    v=((int64_t)g_odg.control_heading_x_q15*g_odg.camera_dir_x_q15 +
       (int64_t)g_odg.control_heading_z_q15*g_odg.camera_dir_z_q15)/ODG_Q15_ONE;
    return odg_clamp_i32((int32_t)v,-ODG_Q15_ONE,ODG_Q15_ONE);
}
int32_t odg_control_strength_q15(void) { return g_odg.control_active ? g_odg.control_strength_q15 : 0; }

uint32_t odg_alive_count(void) {
    uint32_t i;
    uint32_t alive = 0u;
    for (i = 0u; i < ODG_MAX_ACTORS; ++i) {
        if (g_odg.actors[i].active && g_odg.actors[i].hp != 0u) ++alive;
    }
    return alive;
}

uint64_t odg_state_hash(void) {
    /* Raster dimensions and initialization plumbing are deliberately outside the
     * authoritative digest. A portrait Flutter host and a landscape/WASM host that
     * apply the same fixed-tick inputs must publish the same simulation hash. */
    const uint8_t *p = (const uint8_t *)&g_odg + offsetof(odg_world, seed);
    size_t n = offsetof(odg_world, stats) - offsetof(odg_world, seed);
    uint64_t h = UINT64_C(1469598103934665603);
    size_t i;
    for (i = 0u; i < n; ++i) {
        h ^= p[i];
        h *= UINT64_C(1099511628211);
    }
    return h;
}

uint32_t odg_leader_count(void) { return g_odg.leader_count; }
int32_t odg_leader_get(uint32_t rank, odg_leader_entry *out_entry) {
    if (!out_entry || rank >= g_odg.leader_count) return ODG_STATUS_INVALID_ARGUMENT;
    *out_entry = g_odg.leaders[rank];
    return ODG_STATUS_OK;
}
uint32_t odg_leader_score(uint32_t rank) { return rank < g_odg.leader_count ? g_odg.leaders[rank].score : 0u; }
uint32_t odg_leader_name_code(uint32_t rank) { return rank < g_odg.leader_count ? g_odg.leaders[rank].name_code : 0u; }
uint32_t odg_leader_is_player(uint32_t rank) { return rank < g_odg.leader_count ? g_odg.leaders[rank].is_player : 0u; }

void odg_rebuild_stats(void) {
    uint32_t alive = 0u;
    uint32_t i;
    uint32_t j;
    odg_leader_entry temp[ODG_MAX_ACTORS];
    odg_actor *p = &g_odg.actors[0];

    for (i = 0u; i < ODG_MAX_ACTORS; ++i) {
        const odg_actor *a = &g_odg.actors[i];
        temp[i].actor_id = a->id;
        temp[i].score = g_odg.territory_count[i];
        temp[i].level = a->level;
        temp[i].alive = (a->active && a->hp != 0u) ? 1u : 0u;
        temp[i].is_player = a->type == ODG_ACTOR_PLAYER ? 1u : 0u;
        temp[i].name_code = a->name_code;
        if (temp[i].alive != 0u) ++alive;
    }
    for (i = 1u; i < ODG_MAX_ACTORS; ++i) {
        odg_leader_entry key = temp[i];
        j = i;
        while (j > 0u && (temp[j - 1u].score < key.score ||
               (temp[j - 1u].score == key.score && temp[j - 1u].alive < key.alive) ||
               (temp[j - 1u].score == key.score && temp[j - 1u].alive == key.alive &&
                temp[j - 1u].actor_id > key.actor_id))) {
            temp[j] = temp[j - 1u];
            --j;
        }
        temp[j] = key;
    }
    g_odg.leader_count = ODG_MAX_ACTORS < ODG_MAX_LEADERS ? ODG_MAX_ACTORS : ODG_MAX_LEADERS;
    for (i = 0u; i < g_odg.leader_count; ++i) g_odg.leaders[i] = temp[i];

    odg_memset(&g_odg.stats, 0, sizeof(g_odg.stats));
    g_odg.stats.struct_size = (uint32_t)sizeof(g_odg.stats);
    g_odg.stats.api_version = ODG_API_VERSION;
    g_odg.stats.tick = g_odg.tick;
    g_odg.stats.match_seed = g_odg.seed;
    g_odg.stats.width = g_odg.width;
    g_odg.stats.height = g_odg.height;
    g_odg.stats.alive_count = alive;
    g_odg.stats.player_alive = p->hp != 0u ? 1u : 0u;
    g_odg.stats.player_health = p->hp;
    g_odg.stats.player_max_health = p->max_hp;
    g_odg.stats.player_level = p->level;
    g_odg.stats.player_score = g_odg.territory_count[0];
    g_odg.stats.player_kills = p->kills;
    g_odg.stats.player_deaths = p->deaths;
    g_odg.stats.zone_radius_milli = (uint32_t)ODG_WORLD_HALF_CELLS * 1000u;
    g_odg.stats.simulation_hz = ODG_TICK_RATE;
    g_odg.stats.render_triangles = g_odg.render_triangles;
    g_odg.stats.render_pixels_touched = g_odg.render_pixels_touched;
    g_odg.stats.deterministic_state_hash = odg_state_hash();
    g_odg.stats.territory_cells = g_odg.territory_count[0];
    g_odg.stats.territory_total_cells = g_odg.playable_count;
    g_odg.stats.territory_permille = odg_player_territory_permille();
    g_odg.stats.player_trail_cells = p->trail_len;
    g_odg.stats.player_trail_active = p->trail_active;
    g_odg.stats.match_over = g_odg.match_over;
    g_odg.stats.winner_id = g_odg.winner_id;
    g_odg.stats.player_death_reason = p->death_reason;
    g_odg.stats.turret_total = g_odg.turret_count;
    g_odg.stats.player_owned_turrets = odg_player_owned_turrets();
    g_odg.stats.player_carrying_turret = odg_player_carrying_turret();
    g_odg.stats.carried_turret_ammo = odg_player_carried_turret_ammo();
    g_odg.stats.turret_action_available = odg_player_turret_action_available();
    g_odg.stats.ammo_crates_total = g_odg.ammo_crate_count;
    g_odg.stats.player_carrying_ammo_crate = odg_player_carrying_ammo_crate();
    g_odg.stats.player_carried_ammo = odg_player_carried_ammo();
    g_odg.stats.player_ammo_reserve = odg_player_ammo_reserve();
    g_odg.stats.chips_total = odg_chip_count();
    g_odg.stats.player_carrying_chip = odg_player_carrying_chip();
    g_odg.stats.player_chip_kind = odg_player_chip_kind();
    g_odg.stats.hack_action_available = odg_player_hack_action_available();
    g_odg.stats.drop_action_available = odg_player_drop_action_available();
    g_odg.stats.nearby_owned_turret_visible = odg_player_nearby_owned_turret_visible();
    g_odg.stats.nearby_owned_turret_ammo = odg_player_nearby_owned_turret_ammo();
    g_odg.stats.nearby_owned_turret_max_ammo = odg_player_nearby_owned_turret_max_ammo();
}
