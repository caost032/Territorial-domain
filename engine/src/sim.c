#include "game_internal.h"

#include <stdint.h>

static const int32_t dir16[16][2] = {
    { 32767,     0}, { 30273, 12539}, { 23170, 23170}, { 12539, 30273},
    {     0, 32767}, {-12539, 30273}, {-23170, 23170}, {-30273, 12539},
    {-32767,     0}, {-30273,-12539}, {-23170,-23170}, {-12539,-30273},
    {     0,-32767}, { 12539,-30273}, { 23170,-23170}, { 30273,-12539}
};

void odg_update_turret_ownership_internal(void);
static void award_capture_ammo(uint32_t actor_id, uint32_t gained);
static void update_ammo_crates(void);
static void update_chips(void);
static void handle_drop_action(void);
static void turret_clear_target(odg_turret *t);

static uint32_t cell_x(uint32_t cell) { return cell & (ODG_GRID_SIZE - 1u); }
static uint32_t cell_z(uint32_t cell) { return cell >> ODG_GRID_SHIFT; }


static int cell_xy_in_bounds(int32_t x, int32_t z) {
    return x >= 0 && z >= 0 && x < (int32_t)ODG_GRID_SIZE && z < (int32_t)ODG_GRID_SIZE;
}

static int mask_formula(int32_t wx, int32_t wz) {
    /* Organic "country" silhouette: a broad mainland built from overlapping integer
     * ellipses plus two carved bays. It deliberately does not touch the square
     * simulation bounds, so the playable world has a coastline rather than box walls. */
    int64_t x = wx;
    int64_t z = wz;
    int main_land = (x*x*3136 + z*z*3600) <= INT64_C(3136) * INT64_C(3600);
    int east = ((x-39)*(x-39) + (z-7)*(z-7)) <= 24*24;
    int west = ((x+38)*(x+38) + (z+8)*(z+8)) <= 23*23;
    int north = ((x-8)*(x-8) + (z-43)*(z-43)) <= 21*21;
    int south = ((x+14)*(x+14) + (z+42)*(z+42)) <= 22*22;
    int land = main_land || east || west || north || south;
    int bay_ne = ((x-51)*(x-51) + (z-32)*(z-32)) <= 18*18;
    int bay_sw = ((x+50)*(x+50) + (z+30)*(z+30)) <= 17*17;
    if (bay_ne || bay_sw) land = 0;
    return land;
}

static void build_playable_mask(void) {
    uint32_t z;
    g_odg.playable_count = 0u;
    for (z = 0u; z < ODG_GRID_SIZE; ++z) {
        uint32_t x;
        for (x = 0u; x < ODG_GRID_SIZE; ++x) {
            int32_t wx = (int32_t)x - ODG_WORLD_HALF_CELLS;
            int32_t wz = (int32_t)z - ODG_WORLD_HALF_CELLS;
            uint32_t c = z * ODG_GRID_SIZE + x;
            g_odg.playable[c] = mask_formula(wx, wz) ? 1u : 0u;
            if (g_odg.playable[c] != 0u) ++g_odg.playable_count;
        }
    }
}

static int world_point_playable(int32_t x, int32_t z) {
    uint32_t c;
    if (x <= -ODG_WORLD_HALF_FX || x >= ODG_WORLD_HALF_FX ||
        z <= -ODG_WORLD_HALF_FX || z >= ODG_WORLD_HALF_FX) return 0;
    c = odg_cell_from_world(x, z);
    return g_odg.playable[c] != 0u;
}

static int disk_playable(int32_t x, int32_t z, int32_t r) {
    return world_point_playable(x, z) &&
           world_point_playable(x + r, z) && world_point_playable(x - r, z) &&
           world_point_playable(x, z + r) && world_point_playable(x, z - r);
}

static int32_t approach_signed(int32_t current, int32_t target, int32_t step) {
    if (current < target) {
        current += step;
        if (current > target) current = target;
    } else if (current > target) {
        current -= step;
        if (current < target) current = target;
    }
    return current;
}

/* Deterministic angular inertia. The old constant-angle turn made every actor feel like
 * a token rotating on a table. Here steering first builds angular velocity, then bleeds
 * it away as the body converges on the requested heading. Translation remains aligned
 * to the physical forward vector, so arcs are real rather than lateral sliding. */
static void rotate_vec_inertial_toward(int32_t *x, int32_t *z, int32_t tx, int32_t tz,
                                       int32_t *turn_rate_q15, int32_t max_sin_q15,
                                       int32_t accel_q15, int32_t *fallback_sign) {
    int32_t dot;
    int32_t cross;
    int32_t sign;
    int32_t error_mag;
    int32_t desired_rate;
    int32_t rate;
    int32_t sin_step;
    int32_t cos_step;
    int32_t nx;
    int32_t nz;
    int32_t new_cross;
    if (!x || !z || !turn_rate_q15 || (tx == 0 && tz == 0)) return;
    dot = (int32_t)(((int64_t)(*x) * tx + (int64_t)(*z) * tz) / ODG_Q15_ONE);
    dot = odg_clamp_i32(dot, -ODG_Q15_ONE, ODG_Q15_ONE);
    cross = (int32_t)(((int64_t)(*x) * tz - (int64_t)(*z) * tx) / ODG_Q15_ONE);
    cross = odg_clamp_i32(cross, -ODG_Q15_ONE, ODG_Q15_ONE);

    if (dot > 32754 && odg_abs_i32(cross) < 920) {
        *x = tx;
        *z = tz;
        *turn_rate_q15 = approach_signed(*turn_rate_q15, 0, accel_q15);
        return;
    }

    if (cross > 0) sign = 1;
    else if (cross < 0) sign = -1;
    else sign = (fallback_sign && *fallback_sign < 0) ? -1 : 1;
    if (fallback_sign) *fallback_sign = sign;

    error_mag = odg_abs_i32(cross);
    /* At ~180 degrees the cross product approaches zero even though the heading error
     * is maximal. Keep a full-rate turn in that case using the remembered turn side. */
    if (dot < 0) {
        int32_t reverse_mag = (ODG_Q15_ONE - dot) / 2;
        if (reverse_mag > error_mag) error_mag = reverse_mag;
    }
    if (error_mag < 1800) error_mag = 1800;
    if (error_mag > ODG_Q15_ONE) error_mag = ODG_Q15_ONE;
    desired_rate = sign * error_mag;
    rate = approach_signed(*turn_rate_q15, desired_rate, accel_q15);
    *turn_rate_q15 = rate;

    sin_step = (int32_t)(((int64_t)max_sin_q15 * odg_abs_i32(rate)) / ODG_Q15_ONE);
    if (sin_step < 1) sin_step = 1;
    /* Small-angle cos approximation is deterministic and accurate at our <3 degree step. */
    cos_step = ODG_Q15_ONE - (int32_t)(((int64_t)sin_step * sin_step) / (2 * ODG_Q15_ONE));
    if (rate > 0) {
        nx = (int32_t)(((int64_t)(*x) * cos_step - (int64_t)(*z) * sin_step) / ODG_Q15_ONE);
        nz = (int32_t)(((int64_t)(*x) * sin_step + (int64_t)(*z) * cos_step) / ODG_Q15_ONE);
    } else {
        nx = (int32_t)(((int64_t)(*x) * cos_step + (int64_t)(*z) * sin_step) / ODG_Q15_ONE);
        nz = (int32_t)((-(int64_t)(*x) * sin_step + (int64_t)(*z) * cos_step) / ODG_Q15_ONE);
    }
    odg_normalize_q15(nx, nz, x, z);
    new_cross = (int32_t)(((int64_t)(*x) * tz - (int64_t)(*z) * tx) / ODG_Q15_ONE);
    /* Never orbit past the requested direction. */
    if ((cross > 0 && new_cross <= 0) || (cross < 0 && new_cross >= 0)) {
        *x = tx;
        *z = tz;
        *turn_rate_q15 = 0;
    }
}

uint32_t odg_cell_from_world(int32_t x, int32_t z) {
    int32_t cx = (x + ODG_WORLD_HALF_FX) / ODG_CELL_FX;
    int32_t cz = (z + ODG_WORLD_HALF_FX) / ODG_CELL_FX;
    cx = odg_clamp_i32(cx, 0, (int32_t)ODG_GRID_SIZE - 1);
    cz = odg_clamp_i32(cz, 0, (int32_t)ODG_GRID_SIZE - 1);
    return (uint32_t)cz * ODG_GRID_SIZE + (uint32_t)cx;
}

int32_t odg_cell_center_x(uint32_t cell) {
    int32_t cx = (int32_t)cell_x(cell);
    return (cx - (int32_t)(ODG_GRID_SIZE / 2u)) * ODG_CELL_FX + ODG_CELL_FX / 2;
}

int32_t odg_cell_center_z(uint32_t cell) {
    int32_t cz = (int32_t)cell_z(cell);
    return (cz - (int32_t)(ODG_GRID_SIZE / 2u)) * ODG_CELL_FX + ODG_CELL_FX / 2;
}

static int circle_aabb_overlap(int32_t x, int32_t z, int32_t r, const odg_obstacle *o) {
    int32_t cx = odg_clamp_i32(x, o->x - o->hx, o->x + o->hx);
    int32_t cz = odg_clamp_i32(z, o->z - o->hz, o->z + o->hz);
    int64_t dx = (int64_t)x - cx;
    int64_t dz = (int64_t)z - cz;
    return dx * dx + dz * dz < (int64_t)r * r;
}


static int camera_segment_clear(const odg_actor *player,int32_t dir_x_q15,int32_t dir_z_q15,int32_t distance_fx) {
    int32_t d;
    if (!player) return 1;
    /* Sample the entire chase ray, not only its endpoint. A camera can have a clear
     * endpoint while a building still sits between it and the player. 0.28 m samples
     * are comfortably smaller than the narrowest current obstacle feature. */
    for (d=ODG_FX_ONE/2;d<=distance_fx;d+=ODG_FX_ONE/4) {
        int32_t px=player->x-(int32_t)(((int64_t)dir_x_q15*d)/ODG_Q15_ONE);
        int32_t pz=player->z-(int32_t)(((int64_t)dir_z_q15*d)/ODG_Q15_ONE);
        uint32_t oi;
        for (oi=0u;oi<g_odg.obstacle_count;++oi) {
            if (circle_aabb_overlap(px,pz,ODG_CAMERA_COLLISION_RADIUS_FX,&g_odg.obstacles[oi])) return 0;
        }
    }
    return 1;
}

static int position_clear(int32_t x, int32_t z, int32_t r) {
    uint32_t i;
    if (x < -ODG_WORLD_HALF_FX + r || x > ODG_WORLD_HALF_FX - r ||
        z < -ODG_WORLD_HALF_FX + r || z > ODG_WORLD_HALF_FX - r) return 0;
    if (!disk_playable(x, z, r + ODG_FX_ONE / 3)) return 0;
    for (i = 0u; i < g_odg.obstacle_count; ++i) {
        if (circle_aabb_overlap(x, z, r, &g_odg.obstacles[i])) return 0;
    }
    return 1;
}

static int actor_position_clear(const odg_actor *a,int32_t x,int32_t z) {
    if (!a || !position_clear(x,z,a->radius)) return 0;
    /* Infrastructure rule: a carried turret is part of the owner's domain logistics.
     * It can be repositioned anywhere INSIDE owned territory, including boundary cells,
     * but cannot be smuggled across hostile/neutral ground. */
    if (a->id==ODG_PLAYER_ID && g_odg.player_carried_turret<g_odg.turret_count) {
        uint32_t c=odg_cell_from_world(x,z);
        if (c>=ODG_CELL_COUNT || g_odg.territory[c]!=ODG_OWNER_FROM_ID(a->id)) return 0;
    }
    return 1;
}


static int bot_nav_edge_clear(uint32_t a,uint32_t b) {
    const int32_t r=420;
    int32_t ax,az,bx,bz,mx,mz;
    if (a>=ODG_CELL_COUNT||b>=ODG_CELL_COUNT||g_odg.playable[a]==0u||g_odg.playable[b]==0u) return 0;
    ax=odg_cell_center_x(a);az=odg_cell_center_z(a);
    bx=odg_cell_center_x(b);bz=odg_cell_center_z(b);
    mx=(ax+bx)/2;mz=(az+bz)/2;
    return position_clear(ax,az,r) && position_clear(mx,mz,r) && position_clear(bx,bz,r);
}

static void build_bot_navigation(void) {
    uint32_t c;
    odg_memset(g_odg.bot_nav_edges,0,sizeof(g_odg.bot_nav_edges));
    for (c=0u;c<ODG_CELL_COUNT;++c) {
        uint32_t x,z;
        uint8_t bits=0u;
        if (g_odg.playable[c]==0u) continue;
        x=cell_x(c);z=cell_z(c);
        if (x>0u && bot_nav_edge_clear(c,c-1u)) bits|=1u;
        if (x+1u<ODG_GRID_SIZE && bot_nav_edge_clear(c,c+1u)) bits|=2u;
        if (z>0u && bot_nav_edge_clear(c,c-ODG_GRID_SIZE)) bits|=4u;
        if (z+1u<ODG_GRID_SIZE && bot_nav_edge_clear(c,c+ODG_GRID_SIZE)) bits|=8u;
        g_odg.bot_nav_edges[c]=bits;
    }
}

static void resolve_actor_obstacles(odg_actor *a) {
    uint32_t i;
    for (i = 0u; i < g_odg.obstacle_count; ++i) {
        const odg_obstacle *o = &g_odg.obstacles[i];
        int32_t minx = o->x - o->hx - a->radius;
        int32_t maxx = o->x + o->hx + a->radius;
        int32_t minz = o->z - o->hz - a->radius;
        int32_t maxz = o->z + o->hz + a->radius;
        if (a->x > minx && a->x < maxx && a->z > minz && a->z < maxz) {
            int32_t dl = a->x - minx;
            int32_t dr = maxx - a->x;
            int32_t db = a->z - minz;
            int32_t dt = maxz - a->z;
            int32_t m = dl;
            uint32_t side = 0u;
            if (dr < m) { m = dr; side = 1u; }
            if (db < m) { m = db; side = 2u; }
            if (dt < m) { side = 3u; }
            if (side == 0u) { a->x = minx; if (a->vx > 0) a->vx = 0; }
            else if (side == 1u) { a->x = maxx; if (a->vx < 0) a->vx = 0; }
            else if (side == 2u) { a->z = minz; if (a->vz > 0) a->vz = 0; }
            else { a->z = maxz; if (a->vz < 0) a->vz = 0; }
        }
    }
}

static void add_obstacle(int x, int z, int hx, int hz, int h, uint32_t palette) {
    odg_obstacle *o;
    if (g_odg.obstacle_count >= ODG_MAX_OBSTACLES) return;
    o = &g_odg.obstacles[g_odg.obstacle_count++];
    o->x = x * ODG_FX_ONE;
    o->z = z * ODG_FX_ONE;
    o->hx = hx * ODG_FX_ONE;
    o->hz = hz * ODG_FX_ONE;
    o->height_fx = h * ODG_FX_ONE;
    o->palette = palette;
}

static void sync_actor_score(uint32_t id) {
    odg_actor *a;
    uint32_t tier;
    if (id >= ODG_MAX_ACTORS) return;
    a = &g_odg.actors[id];
    a->score = g_odg.territory_count[id];
    tier = 1u + a->score / 64u;
    a->level = tier > 20u ? 20u : tier;
}

static void set_territory_owner(uint32_t cell, uint8_t owner) {
    uint8_t old;
    if (cell >= ODG_CELL_COUNT || g_odg.playable[cell] == 0u) return;
    old = g_odg.territory[cell];
    if (old == owner) return;
    if (old != ODG_OWNER_NONE) {
        uint32_t old_id = ODG_ID_FROM_OWNER(old);
        if (old_id < ODG_MAX_ACTORS && g_odg.territory_count[old_id] > 0u) {
            --g_odg.territory_count[old_id];
        }
    }
    g_odg.territory[cell] = owner;
    if (owner != ODG_OWNER_NONE) {
        uint32_t new_id = ODG_ID_FROM_OWNER(owner);
        if (new_id < ODG_MAX_ACTORS) ++g_odg.territory_count[new_id];
    }
}

static void stamp_initial_territory(odg_actor *a) {
    uint32_t center = a->home_cell;
    int32_t cx = (int32_t)cell_x(center);
    int32_t cz = (int32_t)cell_z(center);
    int32_t dz;
    uint8_t owner = ODG_OWNER_FROM_ID(a->id);
    for (dz = -4; dz <= 4; ++dz) {
        int32_t dx;
        for (dx = -4; dx <= 4; ++dx) {
            int32_t x = cx + dx;
            int32_t z = cz + dz;
            if (x < 0 || z < 0 || x >= (int32_t)ODG_GRID_SIZE || z >= (int32_t)ODG_GRID_SIZE) continue;
            if (dx * dx + dz * dz > 16) continue;
            if (g_odg.playable[(uint32_t)z * ODG_GRID_SIZE + (uint32_t)x] == 0u) continue;
            set_territory_owner((uint32_t)z * ODG_GRID_SIZE + (uint32_t)x, owner);
        }
    }
    sync_actor_score(a->id);
}

static void spawn_actor(uint32_t id, uint32_t type) {
    odg_actor *a;
    odm_rng rng;
    int32_t x;
    int32_t z;
    if (id >= ODG_MAX_ACTORS) return;
    a = &g_odg.actors[id];
    (void)odm_rng_seed_derived(&rng, g_odg.seed, UINT64_C(0x5445525249544f52), id + 1u);
    odg_memset(a, 0, sizeof(*a));
    a->active = 1u;
    a->type = type;
    a->id = id;
    a->name_code = id;
    a->radius = type == ODG_ACTOR_PLAYER ? 430 : 420;
    a->max_hp = 1u;
    a->hp = 1u;
    a->level = 1u;
    a->turn_sign = (id & 1u) ? 1 : -1;
    a->carried_ammo_crate = UINT32_MAX;
    a->carried_chip = UINT32_MAX;
    a->ai_plan_cell = UINT32_MAX;
    a->rng = rng;
    {
        uint32_t fd = odg_rand_bounded(&a->rng, 16u);
        a->face_x_q15 = dir16[fd][0];
        a->face_z_q15 = dir16[fd][1];
    }
    {
        uint32_t attempt;
        int found = 0;
        x = 0; z = 0;
        for (attempt = 0u; attempt < 240u; ++attempt) {
            uint32_t cell = odg_rand_bounded(&a->rng, ODG_CELL_COUNT);
            uint32_t prior;
            int separated = 1;
            if (g_odg.playable[cell] == 0u) continue;
            x = odg_cell_center_x(cell);
            z = odg_cell_center_z(cell);
            if (!position_clear(x, z, a->radius + 2 * ODG_FX_ONE)) continue;
            for (prior = 0u; prior < id; ++prior) {
                if (g_odg.actors[prior].active &&
                    odg_dist2(x,z,g_odg.actors[prior].x,g_odg.actors[prior].z) <
                    (int64_t)(17 * ODG_FX_ONE) * (17 * ODG_FX_ONE)) {
                    separated = 0; break;
                }
            }
            if (separated) { found = 1; break; }
        }
        if (!found) {
            /* Deterministic fallback walks playable cells rather than collapsing all
             * failed spawns to the map center. */
            uint32_t cell;
            for (cell = id; cell < ODG_CELL_COUNT; cell += ODG_MAX_ACTORS) {
                if (g_odg.playable[cell] != 0u) {
                    x = odg_cell_center_x(cell); z = odg_cell_center_z(cell);
                    if (position_clear(x,z,a->radius)) break;
                }
            }
        }
    }
    a->x = x;
    a->z = z;
    a->progress_x = x;
    a->progress_z = z;
    a->home_cell = odg_cell_from_world(x, z);
    a->last_cell = a->home_cell;
    a->bot_mode = ODG_BOT_INSIDE;
    a->think_cd = 14u + odg_rand_bounded(&a->rng, 30u);
}

static void clear_actor_trail(odg_actor *a) {
    uint32_t cell;
    uint8_t owner;
    if (!a) return;
    owner = ODG_OWNER_FROM_ID(a->id);
    /* The trail grid is authoritative. Scanning the bounded territory grid on capture/elimination is
     * rare, bounded and removes any path-capacity failure mode for long human loops. */
    for (cell = 0u; cell < ODG_CELL_COUNT; ++cell) {
        if (g_odg.trail_owner[cell] == owner) g_odg.trail_owner[cell] = ODG_OWNER_NONE;
    }
    a->trail_len = 0u;
    a->trail_active = 0u;
}

static void release_actor_territory(odg_actor *a) {
    uint32_t cell;
    uint8_t owner;
    if (!a) return;
    owner = ODG_OWNER_FROM_ID(a->id);
    for (cell = 0u; cell < ODG_CELL_COUNT; ++cell) {
        if (g_odg.territory[cell] == owner) g_odg.territory[cell] = ODG_OWNER_NONE;
    }
    g_odg.territory_count[a->id] = 0u;
    sync_actor_score(a->id);
}

static void eliminate_actor(odg_actor *victim, uint32_t killer_id, uint32_t reason) {
    if (!victim || !victim->active || victim->hp == 0u) return;
    victim->hp = 0u;
    victim->vx = 0;
    victim->vz = 0;
    victim->death_reason = reason;
    ++victim->deaths;
    if (victim->carried_ammo_crate < g_odg.ammo_crate_count) {
        odg_ammo_crate *c=&g_odg.ammo_crates[victim->carried_ammo_crate];
        c->x=victim->x;c->z=victim->z;c->carried_by=UINT32_MAX;c->pickup_cd=30u;
        victim->carried_ammo_crate=UINT32_MAX;
    }
    if (victim->carried_chip < g_odg.chip_count) {
        odg_chip *c=&g_odg.chips[victim->carried_chip];
        c->x=victim->x;c->z=victim->z;c->carried_by=UINT32_MAX;c->pickup_cd=30u;
        victim->carried_chip=UINT32_MAX;
    }
    if (victim->id==ODG_PLAYER_ID && g_odg.player_carried_turret<g_odg.turret_count) {
        odg_turret *t=&g_odg.turrets[g_odg.player_carried_turret];
        t->x=victim->x;t->z=victim->z;t->carried_by=ODG_TURRET_NONE;t->fire_cd=t->fire_period;
        turret_clear_target(t);
        g_odg.player_carried_turret=ODG_TURRET_NONE;
    }
    clear_actor_trail(victim);
    release_actor_territory(victim);
    {
        uint32_t ti;
        uint8_t dead_owner=ODG_OWNER_FROM_ID(victim->id);
        for (ti=0u;ti<g_odg.turret_count;++ti) {
            odg_turret *t=&g_odg.turrets[ti];
            if (t->active && t->owner==dead_owner) {
                t->owner=ODG_TURRET_NEUTRAL;
                t->carried_by=ODG_TURRET_NONE;
                t->target_kind=ODG_TURRET_TARGET_NONE;
                t->last_target_cell=UINT32_MAX;
                t->target_actor_id=UINT32_MAX;
                t->aim_ticks=0u;
            }
        }
    }
    odg_emit_particles(victim->x, victim->z, 0xff6b86ffu, 28u);
    if (killer_id < ODG_MAX_ACTORS && killer_id != victim->id) {
        odg_actor *killer = &g_odg.actors[killer_id];
        if (killer->active && killer->hp != 0u) {
            ++killer->kills;
            killer->flash_ticks = 12u;
        }
    }
    if (victim->id == ODG_PLAYER_ID) {
        g_odg.match_over = 1u;
        g_odg.winner_id = killer_id < ODG_MAX_ACTORS ? killer_id : UINT32_MAX;
    }
}

static int capture_barrier(uint32_t cell, uint8_t owner) {
    return g_odg.playable[cell] == 0u || g_odg.territory[cell] == owner || g_odg.trail_owner[cell] == owner;
}

static void flood_push(uint32_t cell, uint8_t owner, uint32_t *tail) {
    if (cell >= ODG_CELL_COUNT || !tail || *tail >= ODG_CELL_COUNT) return;
    if (g_odg.flood_seen[cell] || capture_barrier(cell, owner)) return;
    g_odg.flood_seen[cell] = 1u;
    g_odg.flood_queue[*tail] = (uint16_t)cell;
    ++(*tail);
}

static void capture_actor(odg_actor *a) {
    uint32_t head = 0u;
    uint32_t tail = 0u;
    uint32_t x;
    uint32_t z;
    uint32_t cell;
    uint32_t gained = 0u;
    uint8_t owner;
    if (!a || !a->trail_active || a->trail_len == 0u) return;
    owner = ODG_OWNER_FROM_ID(a->id);
    odg_memset(g_odg.flood_seen, 0, sizeof(g_odg.flood_seen));

    /* Seed the flood from the irregular coastline, not the square allocation edge. */
    for (z = 0u; z < ODG_GRID_SIZE; ++z) {
        for (x = 0u; x < ODG_GRID_SIZE; ++x) {
            uint32_t c = z * ODG_GRID_SIZE + x;
            int coast = 0;
            if (g_odg.playable[c] == 0u) continue;
            if (x == 0u || z == 0u || x + 1u == ODG_GRID_SIZE || z + 1u == ODG_GRID_SIZE) coast = 1;
            else if (g_odg.playable[c-1u] == 0u || g_odg.playable[c+1u] == 0u ||
                     g_odg.playable[c-ODG_GRID_SIZE] == 0u || g_odg.playable[c+ODG_GRID_SIZE] == 0u) coast = 1;
            if (coast) flood_push(c, owner, &tail);
        }
    }

    while (head < tail) {
        uint32_t c = (uint32_t)g_odg.flood_queue[head++];
        uint32_t cx = cell_x(c);
        uint32_t cz = cell_z(c);
        if (cx > 0u) flood_push(c - 1u, owner, &tail);
        if (cx + 1u < ODG_GRID_SIZE) flood_push(c + 1u, owner, &tail);
        if (cz > 0u) flood_push(c - ODG_GRID_SIZE, owner, &tail);
        if (cz + 1u < ODG_GRID_SIZE) flood_push(c + ODG_GRID_SIZE, owner, &tail);
    }

    for (cell = 0u; cell < ODG_CELL_COUNT; ++cell) {
        if (g_odg.playable[cell] != 0u &&
            (g_odg.trail_owner[cell] == owner || (!g_odg.flood_seen[cell] && g_odg.territory[cell] != owner))) {
            if (g_odg.territory[cell] != owner) ++gained;
            set_territory_owner(cell, owner);
        }
    }
    clear_actor_trail(a);
    sync_actor_score(a->id);
    if (gained > 0u) {
        uint32_t particles = 8u + gained / 8u;
        if (particles > 24u) particles = 24u;
        odg_emit_particles(a->x, a->z, 0x62e7ffffu, particles);
        award_capture_ammo(a->id, gained);
    }

    /* If a capture consumed an opponent's final cell, that opponent cannot close a loop anymore. */
    for (cell = 0u; cell < ODG_MAX_ACTORS; ++cell) {
        odg_actor *other = &g_odg.actors[cell];
        sync_actor_score(cell);
        if (cell != a->id && other->active && other->hp != 0u && g_odg.territory_count[cell] == 0u) {
            eliminate_actor(other, a->id, ODG_DEATH_TERRITORY_LOST);
        }
    }
    odg_update_turret_ownership_internal();
}

static void trail_append(odg_actor *a, uint32_t cell) {
    uint8_t owner;
    if (!a || cell >= ODG_CELL_COUNT) return;
    owner = ODG_OWNER_FROM_ID(a->id);
    if (!a->trail_active) {
        a->trail_active = 1u;
        a->trail_len = 0u;
        if (a->type == ODG_ACTOR_BOT && a->bot_mode == ODG_BOT_INSIDE) a->bot_mode = ODG_BOT_OUTBOUND;
    }
    if (g_odg.trail_owner[cell] != owner) {
        g_odg.trail_owner[cell] = owner;
        if (a->trail_len < ODG_CELL_COUNT) ++a->trail_len;
    }
}

static void process_actor_cell(odg_actor *a, uint32_t new_cell) {
    uint8_t owner;
    uint8_t trail;
    if (!a || a->hp == 0u || new_cell >= ODG_CELL_COUNT || new_cell == a->last_cell) return;
    owner = ODG_OWNER_FROM_ID(a->id);
    a->last_cell = new_cell;

    /* Topology rule: exposed trail is independent from ground ownership. An actor
     * entering its OWN territory can still cut an enemy trail crossing that cell, and
     * an enemy running through someone else's territory can likewise be cut. The old
     * early return on territory ownership made trails accidentally invulnerable there. */
    trail = g_odg.trail_owner[new_cell];
    if (trail != ODG_OWNER_NONE) {
        uint32_t victim_id = ODG_ID_FROM_OWNER(trail);
        if (victim_id != a->id && victim_id < ODG_MAX_ACTORS) {
            eliminate_actor(&g_odg.actors[victim_id], a->id, ODG_DEATH_TRAIL_CUT);
        }
    }
    if (a->hp == 0u) return;

    if (g_odg.territory[new_cell] == owner) {
        if (a->trail_active) capture_actor(a);
        if (a->type == ODG_ACTOR_BOT) {
            a->bot_mode = ODG_BOT_INSIDE;
            a->think_cd = 4u + odg_rand_bounded(&a->rng, 12u);
        }
        return;
    }
    trail_append(a, new_cell);
}

static void resolve_trail_contacts(void) {
    uint32_t cutter_id;
    /* Cell transitions catch normal crossings. This pass also catches a trail drawn
     * underneath a cube already occupying that cell. Ground ownership never grants
     * immunity: trail_owner and territory are deliberately separate layers. */
    for (cutter_id=0u;cutter_id<ODG_MAX_ACTORS;++cutter_id) {
        odg_actor *cutter=&g_odg.actors[cutter_id];
        uint32_t cell;
        uint8_t trail;
        uint32_t victim_id;
        if (!cutter->active || cutter->hp==0u) continue;
        cell=odg_cell_from_world(cutter->x,cutter->z);
        trail=g_odg.trail_owner[cell];
        if (trail==ODG_OWNER_NONE || trail==ODG_OWNER_FROM_ID(cutter_id)) continue;
        victim_id=ODG_ID_FROM_OWNER(trail);
        if (victim_id<ODG_MAX_ACTORS && g_odg.actors[victim_id].hp!=0u)
            eliminate_actor(&g_odg.actors[victim_id],cutter_id,ODG_DEATH_TRAIL_CUT);
    }
}

static int direction_clear_for_actor(const odg_actor *a, int32_t dx_q15, int32_t dz_q15) {
    int32_t look = 2 * ODG_FX_ONE;
    int32_t x = a->x + (int32_t)(((int64_t)dx_q15 * look) / ODG_Q15_ONE);
    int32_t z = a->z + (int32_t)(((int64_t)dz_q15 * look) / ODG_Q15_ONE);
    uint32_t cell;
    if (!position_clear(x, z, a->radius)) return 0;
    cell = odg_cell_from_world(x, z);
    /* Trails are an independent vulnerable topology layer, not walls. Crossing an
     * enemy trail is legal and defeats its owner regardless of who owns the ground. */
    (void)cell;
    return 1;
}

static int bot_trail_hazard_ahead(const odg_actor *a, int32_t dx_q15, int32_t dz_q15, uint32_t cells) {
    uint32_t step;
    if (!a) return 1;
    /* Kept as a planner boundary probe. Self trails are non-lethal and enemy trails are
     * desirable cut opportunities, so neither may be treated as a movement hazard. */
    for (step = 1u; step <= cells; ++step) {
        int32_t dist = (int32_t)step * ODG_CELL_FX;
        int32_t x = a->x + (int32_t)(((int64_t)dx_q15 * dist) / ODG_Q15_ONE);
        int32_t z = a->z + (int32_t)(((int64_t)dz_q15 * dist) / ODG_Q15_ONE);
        if (!world_point_playable(x,z)) return 1;
    }
    return 0;
}

static int bot_find_safe_home_step(odg_actor *a, int32_t *out_x, int32_t *out_z) {
    uint32_t start;
    uint32_t head = 0u;
    uint32_t tail = 0u;
    uint32_t goal = UINT32_MAX;
    uint8_t owner;
    if (!a || !out_x || !out_z) return 0;
    start = odg_cell_from_world(a->x, a->z);
    owner = ODG_OWNER_FROM_ID(a->id);
    odg_memset(g_odg.bot_parent, 0xff, sizeof(g_odg.bot_parent));
    g_odg.bot_parent[start] = (uint16_t)start;
    g_odg.flood_queue[tail++] = (uint16_t)start;
    while (head < tail) {
        uint32_t c = (uint32_t)g_odg.flood_queue[head++];
        uint32_t cx = cell_x(c);
        uint32_t cz = cell_z(c);
        uint32_t neighbors[4];
        uint8_t edge_bits[4];
        uint32_t ncount = 0u;
        uint32_t k;
        if (cx > 0u) {neighbors[ncount]=c-1u;edge_bits[ncount++]=1u;}
        if (cx + 1u < ODG_GRID_SIZE) {neighbors[ncount]=c+1u;edge_bits[ncount++]=2u;}
        if (cz > 0u) {neighbors[ncount]=c-ODG_GRID_SIZE;edge_bits[ncount++]=4u;}
        if (cz + 1u < ODG_GRID_SIZE) {neighbors[ncount]=c+ODG_GRID_SIZE;edge_bits[ncount++]=8u;}
        for (k = 0u; k < ncount; ++k) {
            uint32_t n = neighbors[k];
            if ((g_odg.bot_nav_edges[c]&edge_bits[k])==0u) continue;
            if (g_odg.bot_parent[n] != UINT16_MAX || g_odg.playable[n] == 0u) continue;
            /* Enemy trail cells remain navigable: touching them is how a cube cuts them. */
            g_odg.bot_parent[n] = (uint16_t)c;
            if (g_odg.territory[n] == owner) { goal = n; head = tail; break; }
            if (tail < ODG_CELL_COUNT) g_odg.flood_queue[tail++] = (uint16_t)n;
        }
    }
    if (goal == UINT32_MAX) return 0;
    while ((uint32_t)g_odg.bot_parent[goal] != start && (uint32_t)g_odg.bot_parent[goal] != goal) {
        goal = (uint32_t)g_odg.bot_parent[goal];
    }
    /* Steer to the CENTER of the selected adjacent BFS cell. The segment from any
     * point inside the current cell to the center of a cardinal neighbor crosses
     * their shared edge, never a third diagonal cell. This removes the old grid-like
     * left/right snap while preserving the exact safe topology chosen by BFS. */
    if (!((goal + 1u == start) || (goal == start + 1u) ||
          (goal + ODG_GRID_SIZE == start) || (goal == start + ODG_GRID_SIZE))) return 0;
    odg_normalize_q15(odg_cell_center_x(goal) - a->x, odg_cell_center_z(goal) - a->z, out_x, out_z);
    return (*out_x != 0 || *out_z != 0);
}

static uint32_t bot_enemy_turret_risk(const odg_actor *a, int32_t x, int32_t z) {
    uint32_t i;
    uint32_t risk=0u;
    uint8_t own;
    if (!a) return 0u;
    own=ODG_OWNER_FROM_ID(a->id);
    for (i=0u;i<g_odg.turret_count;++i) {
        const odg_turret *t=&g_odg.turrets[i];
        int32_t margin;
        if (!t->active || t->owner==ODG_TURRET_NEUTRAL || t->owner==own || t->ammo==0u || t->carried_by!=ODG_TURRET_NONE) continue;
        margin=t->range_fx+2*ODG_FX_ONE;
        if (odg_dist2(x,z,t->x,t->z)<=(int64_t)margin*margin) ++risk;
    }
    return risk;
}

static int turret_is_reprogrammable_enemy(const odg_turret *t,uint8_t own) {
    uint32_t owner_id;
    if (!t || !t->active || t->carried_by!=ODG_TURRET_NONE ||
        t->owner==ODG_TURRET_NEUTRAL || t->owner==own) return 0;
    owner_id=ODG_ID_FROM_OWNER(t->owner);
    return owner_id<ODG_MAX_ACTORS;
}

static void bot_set_side_direction(odg_actor *a) {
    int32_t x = a->bot_out_x_q15;
    int32_t z = a->bot_out_z_q15;
    if (a->turn_sign > 0) { a->ai_x_q15 = -z; a->ai_z_q15 = x; }
    else { a->ai_x_q15 = z; a->ai_z_q15 = -x; }
    a->ai_commit_ticks = ODG_BOT_STEER_COMMIT_TICKS;
}

static void bot_set_return_direction(odg_actor *a) {
    a->ai_x_q15 = -a->bot_out_x_q15;
    a->ai_z_q15 = -a->bot_out_z_q15;
    a->bot_mode = ODG_BOT_RETURN;
    a->ai_plan_cell = UINT32_MAX;
    a->ai_commit_ticks = ODG_BOT_STEER_COMMIT_TICKS;
    a->think_cd = 0u;
}

static void bot_choose_expansion(odg_actor *a) {
    uint32_t start = odg_rand_bounded(&a->rng, 16u);
    uint32_t k;
    int32_t best_x = 0;
    int32_t best_z = ODG_Q15_ONE;
    int32_t best_score = INT32_MIN;
    uint8_t owner = ODG_OWNER_FROM_ID(a->id);
    for (k = 0u; k < 16u; ++k) {
        uint32_t d = (start + k) & 15u;
        int32_t dx = dir16[d][0];
        int32_t dz = dir16[d][1];
        int32_t score = 0;
        uint32_t step;
        if (!direction_clear_for_actor(a, dx, dz)) continue;
        if (bot_trail_hazard_ahead(a, dx, dz, 4u)) continue;
        for (step = 3u; step <= 8u; step += 1u) {
            int32_t dist = (int32_t)step * ODG_CELL_FX;
            int32_t sx = a->x + (int32_t)(((int64_t)dx * dist) / ODG_Q15_ONE);
            int32_t sz = a->z + (int32_t)(((int64_t)dz * dist) / ODG_Q15_ONE);
            uint32_t c;
            if (sx <= -ODG_WORLD_HALF_FX || sx >= ODG_WORLD_HALF_FX ||
                sz <= -ODG_WORLD_HALF_FX || sz >= ODG_WORLD_HALF_FX) { score -= 30; continue; }
            c = odg_cell_from_world(sx, sz);
            if (g_odg.playable[c] == 0u) { score -= 40; continue; }
            if (g_odg.territory[c] != owner) score += 4;
            if (g_odg.trail_owner[c] != ODG_OWNER_NONE) score -= 12;
            score -= (int32_t)(bot_enemy_turret_risk(a,sx,sz)*7u);
        }
        /* Favor space away from hard arena edges. */
        {
            int32_t probe = 8 * ODG_CELL_FX;
            int32_t px = a->x + (int32_t)(((int64_t)dx * probe) / ODG_Q15_ONE);
            int32_t pz = a->z + (int32_t)(((int64_t)dz * probe) / ODG_Q15_ONE);
            int32_t edge_x = ODG_WORLD_HALF_FX - odg_abs_i32(px);
            int32_t edge_z = ODG_WORLD_HALF_FX - odg_abs_i32(pz);
            if (edge_x < 8 * ODG_FX_ONE || edge_z < 8 * ODG_FX_ONE) score -= 20;
        }
        score += (int32_t)odg_rand_bounded(&a->rng, 5u);
        if (score > best_score) { best_score = score; best_x = dx; best_z = dz; }
    }
    a->ai_x_q15 = best_x;
    a->ai_z_q15 = best_z;
    a->bot_out_x_q15 = best_x;
    a->bot_out_z_q15 = best_z;
    a->bot_mode = ODG_BOT_INSIDE;
    a->bot_leg_target = 4u + odg_rand_bounded(&a->rng, 5u);
    a->turn_sign = odg_rand_bounded(&a->rng, 2u) ? 1 : -1;
    a->ai_commit_ticks = ODG_BOT_STEER_COMMIT_TICKS;
}

static int bot_supply_direction(odg_actor *a, int32_t *out_x, int32_t *out_z) {
    uint32_t i;
    uint32_t best=UINT32_MAX;
    int64_t best_d2=INT64_MAX;
    uint8_t own;
    int32_t tx=0,tz=0;
    if (!a || !out_x || !out_z || a->trail_active) return 0;
    own=ODG_OWNER_FROM_ID(a->id);

    /* Chip logistics: a bot carrying a reprogram chip seeks only already-programmed
     * enemy infrastructure. Neutral turrets are commissioned by territorial majority,
     * so routing a chip carrier toward one would create an objective it can never use. */
    if(a->carried_chip<g_odg.chip_count){
        best_d2=INT64_MAX;best=UINT32_MAX;
        for(i=0u;i<g_odg.turret_count;++i){
            const odg_turret *t=&g_odg.turrets[i];int64_t d2;
            if(!turret_is_reprogrammable_enemy(t,own)) continue;
            d2=odg_dist2(a->x,a->z,t->x,t->z);if(d2<best_d2){best=i;best_d2=d2;}
        }
        if(best<g_odg.turret_count){tx=g_odg.turrets[best].x;tz=g_odg.turrets[best].z;goto bot_supply_finish;}
    } else {
        int programmable_exists=0;
        for(i=0u;i<g_odg.turret_count;++i){if(turret_is_reprogrammable_enemy(&g_odg.turrets[i],own)){programmable_exists=1;break;}}
        if(programmable_exists){
            best=UINT32_MAX;best_d2=(int64_t)(22*ODG_FX_ONE)*(22*ODG_FX_ONE);
            for(i=0u;i<g_odg.chip_count;++i){const odg_chip *c=&g_odg.chips[i];int64_t d2;if(!c->active||c->carried_by!=UINT32_MAX)continue;d2=odg_dist2(a->x,a->z,c->x,c->z);if(d2<best_d2){best=i;best_d2=d2;}}
            if(best<g_odg.chip_count){tx=g_odg.chips[best].x;tz=g_odg.chips[best].z;goto bot_supply_finish;}
        }
    }

    /* A carried crate or earned reserve creates a concrete logistics objective: reach
     * the nearest owned turret that still needs ammunition. */
    if (a->carried_ammo_crate<g_odg.ammo_crate_count || a->ammo_reserve!=0u) {
        for (i=0u;i<g_odg.turret_count;++i) {
            const odg_turret *t=&g_odg.turrets[i];
            int64_t d2;
            if (!t->active || t->owner!=own || t->carried_by!=ODG_TURRET_NONE || t->ammo>=t->max_ammo) continue;
            d2=odg_dist2(a->x,a->z,t->x,t->z);
            if (d2<best_d2) {best=i;best_d2=d2;}
        }
        if (best<g_odg.turret_count) {tx=g_odg.turrets[best].x;tz=g_odg.turrets[best].z;}
    } else {
        /* Only divert for a crate if this bot actually owns a turret worth supplying. */
        int needs_supply=0;
        for (i=0u;i<g_odg.turret_count;++i) {
            const odg_turret *t=&g_odg.turrets[i];
            if (t->active && t->owner==own && t->carried_by==ODG_TURRET_NONE && t->ammo+8u<t->max_ammo) {needs_supply=1;break;}
        }
        if (!needs_supply) return 0;
        best=UINT32_MAX;best_d2=(int64_t)(26*ODG_FX_ONE)*(26*ODG_FX_ONE);
        for (i=0u;i<g_odg.ammo_crate_count;++i) {
            const odg_ammo_crate *c=&g_odg.ammo_crates[i];
            int64_t d2;
            if (!c->active || c->carried_by!=UINT32_MAX) continue;
            d2=odg_dist2(a->x,a->z,c->x,c->z);
            if (d2<best_d2) {best=i;best_d2=d2;}
        }
        if (best<g_odg.ammo_crate_count) {tx=g_odg.ammo_crates[best].x;tz=g_odg.ammo_crates[best].z;}
    }
bot_supply_finish:
    if (tx==0 && tz==0 && best==UINT32_MAX) return 0;
    odg_normalize_q15(tx-a->x,tz-a->z,out_x,out_z);
    if (!direction_clear_for_actor(a,*out_x,*out_z)) return 0;
    return 1;
}

static void bot_control(odg_actor *a, int32_t *out_x, int32_t *out_z) {
    uint32_t current_cell;
    if (!a || !out_x || !out_z) return;
    if (a->think_cd > 0u) --a->think_cd;
    if (a->ai_commit_ticks > 0u) --a->ai_commit_ticks;
    current_cell = odg_cell_from_world(a->x, a->z);

    /* Logistics steering is sampled, not recomputed every tick. Re-pointing at a
     * moving/near target every 1/120 s was one source of left-right chatter. */
    if (!a->trail_active) {
        int32_t sx=0,sz=0;
        if ((a->ai_commit_ticks==0u || (a->ai_x_q15==0 && a->ai_z_q15==0)) &&
            bot_supply_direction(a,&sx,&sz)) {
            a->ai_x_q15=sx;
            a->ai_z_q15=sz;
            a->ai_commit_ticks=ODG_BOT_STEER_COMMIT_TICKS;
            a->think_cd=12u;
            *out_x=a->ai_x_q15; *out_z=a->ai_z_q15;
            return;
        }
    }

    if (!a->trail_active) {
        if ((a->think_cd == 0u && a->ai_commit_ticks==0u) ||
            (a->ai_x_q15 == 0 && a->ai_z_q15 == 0)) {
            bot_choose_expansion(a);
            a->think_cd = 28u + odg_rand_bounded(&a->rng, 38u);
        }
    } else if (a->bot_mode == ODG_BOT_OUTBOUND && a->trail_len >= a->bot_leg_target) {
        bot_set_side_direction(a);
        a->bot_mode = ODG_BOT_SIDELEG;
        a->bot_leg_target = a->trail_len + 2u + odg_rand_bounded(&a->rng, 3u);
    } else if (a->bot_mode == ODG_BOT_SIDELEG && a->trail_len >= a->bot_leg_target) {
        bot_set_return_direction(a);
    }

    /* A return path is replanned only after entering another territory cell. The previous
     * four-tick replanner could alternately prefer X then Z while the cube was still
     * inside one cell, visibly producing left-right-left-right motion. */
    if (a->bot_mode == ODG_BOT_RETURN &&
        (a->ai_plan_cell != current_cell || (a->ai_x_q15==0 && a->ai_z_q15==0))) {
        int32_t hx = a->ai_x_q15;
        int32_t hz = a->ai_z_q15;
        if (bot_find_safe_home_step(a, &hx, &hz)) {
            a->ai_x_q15 = hx;
            a->ai_z_q15 = hz;
            a->ai_plan_cell = current_cell;
            a->ai_commit_ticks = ODG_BOT_STEER_COMMIT_TICKS;
        }
    }

    if (a->trail_active && bot_trail_hazard_ahead(a, a->ai_x_q15, a->ai_z_q15, 2u)) {
        if (a->bot_mode == ODG_BOT_OUTBOUND) {
            bot_set_side_direction(a);
            a->bot_mode = ODG_BOT_SIDELEG;
            a->bot_leg_target = a->trail_len + 3u;
        } else if (a->bot_mode == ODG_BOT_SIDELEG) {
            bot_set_return_direction(a);
        }
    }
    if (a->trail_active && bot_enemy_turret_risk(a,a->x,a->z)!=0u && a->bot_mode!=ODG_BOT_RETURN) {
        if (a->bot_mode==ODG_BOT_OUTBOUND) {
            bot_set_side_direction(a); a->bot_mode=ODG_BOT_SIDELEG; a->bot_leg_target=a->trail_len+2u;
        } else {
            bot_set_return_direction(a);
        }
    }

    if (a->trail_active && a->trail_len + 4u >= ODG_BOT_TRAIL_SOFT_LIMIT &&
        a->bot_mode == ODG_BOT_SIDELEG) {
        bot_set_return_direction(a);
    }

    /* Obstacle avoidance has hysteresis. Once a side is selected, keep it long enough
     * for the actor's physical turn to make progress instead of choosing the opposite
     * side on the next tick. */
    if (!direction_clear_for_actor(a, a->ai_x_q15, a->ai_z_q15) && a->ai_commit_ticks==0u) {
        int32_t x = a->ai_x_q15;
        int32_t z = a->ai_z_q15;
        int32_t preferred_x = a->turn_sign > 0 ? -z : z;
        int32_t preferred_z = a->turn_sign > 0 ? x : -x;
        int32_t other_x = -preferred_x;
        int32_t other_z = -preferred_z;
        if (direction_clear_for_actor(a, preferred_x, preferred_z) &&
            !bot_trail_hazard_ahead(a, preferred_x, preferred_z, 2u)) {
            a->ai_x_q15=preferred_x; a->ai_z_q15=preferred_z;
            a->ai_commit_ticks=2u*ODG_BOT_STEER_COMMIT_TICKS;
        } else if (direction_clear_for_actor(a, other_x, other_z) &&
                   !bot_trail_hazard_ahead(a, other_x, other_z, 2u)) {
            a->turn_sign=-a->turn_sign;
            a->ai_x_q15=other_x; a->ai_z_q15=other_z;
            a->ai_commit_ticks=2u*ODG_BOT_STEER_COMMIT_TICKS;
        } else if (!a->trail_active) {
            bot_choose_expansion(a);
        }
    }

    /* The organic coast is already represented by position_clear(); this outer guard
     * remains only as an early long-range cue and is also hysteretic. */
    if (a->ai_commit_ticks==0u) {
        int32_t margin = 8 * ODG_FX_ONE;
        if ((a->x > ODG_WORLD_HALF_FX - margin && a->ai_x_q15 > 0) ||
            (a->x < -ODG_WORLD_HALF_FX + margin && a->ai_x_q15 < 0) ||
            (a->z > ODG_WORLD_HALF_FX - margin && a->ai_z_q15 > 0) ||
            (a->z < -ODG_WORLD_HALF_FX + margin && a->ai_z_q15 < 0)) {
            if (a->trail_active) {
                if (a->bot_mode == ODG_BOT_OUTBOUND) {
                    bot_set_side_direction(a);
                    a->bot_mode = ODG_BOT_SIDELEG;
                    a->bot_leg_target = a->trail_len + 3u;
                } else if (a->bot_mode == ODG_BOT_SIDELEG) {
                    bot_set_return_direction(a);
                }
            } else {
                bot_choose_expansion(a);
            }
        }
    }

    *out_x = a->ai_x_q15;
    *out_z = a->ai_z_q15;
}

static int32_t q15_dot(int32_t ax, int32_t az, int32_t bx, int32_t bz) {
    int64_t d = ((int64_t)ax * bx + (int64_t)az * bz) / ODG_Q15_ONE;
    return odg_clamp_i32((int32_t)d, -ODG_Q15_ONE, ODG_Q15_ONE);
}

static int32_t q15_cross(int32_t ax, int32_t az, int32_t bx, int32_t bz) {
    int64_t c = ((int64_t)ax * bz - (int64_t)az * bx) / ODG_Q15_ONE;
    return odg_clamp_i32((int32_t)c, -ODG_Q15_ONE, ODG_Q15_ONE);
}

/* Steering and facing are intentionally separate. The stick names an exact WORLD
 * heading; translation bends toward it immediately with a bounded angular step while
 * the visible cube keeps its own inertial body rotation. This preserves a readable arc
 * without the old failure mode where the cube continued straight until facing caught up. */
static void steer_translation_heading(int32_t current_x, int32_t current_z,
                                      int32_t target_x, int32_t target_z,
                                      int32_t max_sin_q15,
                                      int32_t *out_x, int32_t *out_z) {
    int32_t dot;
    int32_t cross;
    int32_t sign;
    int32_t error;
    int32_t sin_step;
    int32_t cos_step;
    int32_t nx;
    int32_t nz;
    int32_t new_cross;
    if (!out_x || !out_z) return;
    if ((target_x == 0 && target_z == 0) || (current_x == 0 && current_z == 0)) {
        *out_x = target_x;
        *out_z = target_z;
        return;
    }
    odg_normalize_q15(current_x, current_z, &current_x, &current_z);
    odg_normalize_q15(target_x, target_z, &target_x, &target_z);
    dot = q15_dot(current_x, current_z, target_x, target_z);
    cross = q15_cross(current_x, current_z, target_x, target_z);
    if (dot > 32754 && odg_abs_i32(cross) < 920) {
        *out_x = target_x;
        *out_z = target_z;
        return;
    }
    /* A near-opposite request is handled by approach_heading_velocity(): it brakes the
     * old trajectory before inversion. Returning the target here avoids an arbitrary
     * left/right choice at exactly 180 degrees. */
    if (dot < -22000) {
        *out_x = target_x;
        *out_z = target_z;
        return;
    }
    sign = cross >= 0 ? 1 : -1;
    error = odg_abs_i32(cross);
    if (dot < 0) {
        int32_t reverse_mag = (ODG_Q15_ONE - dot) / 2;
        if (reverse_mag > error) error = reverse_mag;
    }
    if (error < 1800) error = 1800;
    if (error > ODG_Q15_ONE) error = ODG_Q15_ONE;
    sin_step = (int32_t)(((int64_t)max_sin_q15 * error) / ODG_Q15_ONE);
    if (sin_step < 1) sin_step = 1;
    cos_step = ODG_Q15_ONE - (int32_t)(((int64_t)sin_step * sin_step) / (2 * ODG_Q15_ONE));
    if (sign > 0) {
        nx = (int32_t)(((int64_t)current_x * cos_step - (int64_t)current_z * sin_step) / ODG_Q15_ONE);
        nz = (int32_t)(((int64_t)current_x * sin_step + (int64_t)current_z * cos_step) / ODG_Q15_ONE);
    } else {
        nx = (int32_t)(((int64_t)current_x * cos_step + (int64_t)current_z * sin_step) / ODG_Q15_ONE);
        nz = (int32_t)((-(int64_t)current_x * sin_step + (int64_t)current_z * cos_step) / ODG_Q15_ONE);
    }
    odg_normalize_q15(nx, nz, &nx, &nz);
    new_cross = q15_cross(nx, nz, target_x, target_z);
    if ((cross > 0 && new_cross <= 0) || (cross < 0 && new_cross >= 0)) {
        nx = target_x;
        nz = target_z;
    }
    *out_x = nx;
    *out_z = nz;
}

/* Translation uses a scalar speed plus an authoritative requested heading. Component-wise
 * inertia made a fresh diagonal command inherit the old axis for several ticks. v8 keeps
 * acceleration in speed, not in direction. Only a near-opposite reversal brakes along the
 * old vector before changing sign; normal steering obeys the requested heading immediately. */
static void approach_heading_velocity(odg_actor *a, int32_t dir_x_q15, int32_t dir_z_q15,
                                      int32_t target_speed, int32_t accel, int32_t brake) {
    int32_t current_speed;
    int32_t next_speed;
    int32_t use_x=dir_x_q15,use_z=dir_z_q15;
    int32_t cur_x=0,cur_z=0;
    int reversing=0;
    if (!a) return;
    current_speed=a->speed_fx;
    if (current_speed<0) current_speed=0;
    if (a->vx!=0 || a->vz!=0) odg_normalize_q15(a->vx,a->vz,&cur_x,&cur_z);

    if (target_speed<=0 || (dir_x_q15==0 && dir_z_q15==0)) {
        next_speed=approach_signed(current_speed,0,brake);
        if (cur_x!=0 || cur_z!=0) {use_x=cur_x;use_z=cur_z;}
        else {use_x=a->face_x_q15;use_z=a->face_z_q15;}
    } else {
        if ((cur_x!=0 || cur_z!=0) && current_speed>6) {
            int32_t d=q15_dot(cur_x,cur_z,dir_x_q15,dir_z_q15);
            /* >~132 degree reversal: brake before translating backward. A 90 degree or
             * diagonal change is NOT delayed; precision wins there. */
            if (d < -22000) reversing=1;
        }
        if (reversing) {
            next_speed=approach_signed(current_speed,0,brake);
            use_x=cur_x;use_z=cur_z;
        } else {
            int32_t step=target_speed<current_speed?brake:accel;
            next_speed=approach_signed(current_speed,target_speed,step);
            use_x=dir_x_q15;use_z=dir_z_q15;
        }
    }
    a->speed_fx=next_speed;
    a->vx=(int32_t)(((int64_t)use_x*next_speed)/ODG_Q15_ONE);
    a->vz=(int32_t)(((int64_t)use_z*next_speed)/ODG_Q15_ONE);
}

/* Collision response is continuous enough for a 120 Hz fixed step: try the requested
 * displacement, then retain the unobstructed tangent component instead of zeroing the
 * whole velocity.  This removes the old stop-go feeling along rocks/buildings/coast. */
static void rotate_dir_q15(int32_t x, int32_t z, int32_t cos_q15, int32_t sin_q15,
                           int sign, int32_t *out_x, int32_t *out_z) {
    int32_t nx,nz;
    if (sign >= 0) {
        nx=(int32_t)(((int64_t)x*cos_q15-(int64_t)z*sin_q15)/ODG_Q15_ONE);
        nz=(int32_t)(((int64_t)x*sin_q15+(int64_t)z*cos_q15)/ODG_Q15_ONE);
    } else {
        nx=(int32_t)(((int64_t)x*cos_q15+(int64_t)z*sin_q15)/ODG_Q15_ONE);
        nz=(int32_t)((-(int64_t)x*sin_q15+(int64_t)z*cos_q15)/ODG_Q15_ONE);
    }
    odg_normalize_q15(nx,nz,out_x,out_z);
}

static int contact_candidate_clear(const odg_actor *a,int32_t dx_q15,int32_t dz_q15,int32_t speed,
                                   int32_t *out_x,int32_t *out_z) {
    int32_t vx=(int32_t)(((int64_t)dx_q15*speed)/ODG_Q15_ONE);
    int32_t vz=(int32_t)(((int64_t)dz_q15*speed)/ODG_Q15_ONE);
    if (vx==0 && vz==0) return 0;
    if (!actor_position_clear(a,a->x+vx,a->z+vz)) return 0;
    if (out_x) *out_x=vx;
    if (out_z) *out_z=vz;
    return 1;
}

/* v10 contact steering: collision may change the ACTUAL displacement, never the user's
 * commanded world heading.  When the requested step is blocked, search a narrow angular
 * fan for the closest legal tangent and latch that contact side briefly.  This removes
 * the old X/Z stair-step at organic coastlines and obstacle corners while a true head-on
 * collision still stops rather than inventing a route. */
static void move_actor_with_slide(odg_actor *a) {
    /* 11.25 .. 78.75 degrees. The broad end of this fan is only reached when the
     * requested direction is genuinely blocked; nearest legal angle always wins. */
    static const int32_t rot[][2] = {
        {32137,6393},{30273,12539},{27245,18204},{23170,23170},
        {18204,27245},{12539,30273},{6393,32137}
    };
    int32_t ox,oz,req_x,req_z,req_dir_x=0,req_dir_z=0,speed;
    int32_t chosen_vx=0,chosen_vz=0,chosen_dx=0,chosen_dz=0;
    uint32_t i;
    int chosen_sign=0;
    if (!a) return;
    if (a->slide_lock_ticks>0u) --a->slide_lock_ticks;
    ox=a->x;oz=a->z;req_x=a->vx;req_z=a->vz;
    speed=(int32_t)odg_isqrt_u64((uint64_t)((int64_t)req_x*req_x+(int64_t)req_z*req_z));
    if (speed<=0) {a->slide_lock_ticks=0u;a->slide_axis=0u;return;}
    odg_normalize_q15(req_x,req_z,&req_dir_x,&req_dir_z);

    if (actor_position_clear(a,ox+req_x,oz+req_z)) {
        a->x=ox+req_x;a->z=oz+req_z;
        a->slide_lock_ticks=0u;a->slide_axis=0u;
        a->slide_dir_x_q15=0;a->slide_dir_z_q15=0;
        return;
    }

    /* Reuse a still-valid tangent. This hysteresis prevents corner chatter. */
    if (a->slide_lock_ticks>0u && (a->slide_dir_x_q15!=0 || a->slide_dir_z_q15!=0) &&
        q15_dot(req_dir_x,req_dir_z,a->slide_dir_x_q15,a->slide_dir_z_q15)>=ODG_CONTACT_STEER_MIN_DOT_Q15 &&
        contact_candidate_clear(a,a->slide_dir_x_q15,a->slide_dir_z_q15,speed,&chosen_vx,&chosen_vz)) {
        chosen_dx=a->slide_dir_x_q15;chosen_dz=a->slide_dir_z_q15;
        chosen_sign=(a->slide_axis==2u)?-1:1;
    } else {
        /* Nearest angle wins. If both sides are legal, preserve the last side or use
         * the actor's deterministic turn preference. */
        for (i=0u;i<(uint32_t)(sizeof(rot)/sizeof(rot[0])) && chosen_sign==0;++i) {
            int order0=(a->slide_axis==2u || (a->slide_axis==0u && a->turn_sign<0))?-1:1;
            int pass;
            for (pass=0;pass<2;++pass) {
                int sign=pass==0?order0:-order0;
                int32_t dx,dz,vx,vz;
                rotate_dir_q15(req_dir_x,req_dir_z,rot[i][0],rot[i][1],sign,&dx,&dz);
                if (q15_dot(req_dir_x,req_dir_z,dx,dz)<ODG_CONTACT_STEER_MIN_DOT_Q15) continue;
                if (contact_candidate_clear(a,dx,dz,speed,&vx,&vz)) {
                    chosen_vx=vx;chosen_vz=vz;chosen_dx=dx;chosen_dz=dz;chosen_sign=sign;break;
                }
            }
        }
    }

    if (chosen_sign!=0) {
        a->x=ox+chosen_vx;a->z=oz+chosen_vz;
        a->vx=chosen_vx;a->vz=chosen_vz;
        a->slide_dir_x_q15=chosen_dx;a->slide_dir_z_q15=chosen_dz;
        a->slide_axis=chosen_sign>0?1u:2u;
        a->slide_lock_ticks=ODG_SLIDE_LOCK_TICKS;
    } else {
        /* A full 120 Hz step can be rejected while a shorter step is still legal.
         * Preserve that sub-step before declaring a true head-on stop. */
        int32_t divisor;
        int moved=0;
        for (divisor=2;divisor<=8;divisor*=2) {
            int32_t vx=req_x/divisor;
            int32_t vz=req_z/divisor;
            if ((vx!=0 || vz!=0) && actor_position_clear(a,ox+vx,oz+vz)) {
                a->x=ox+vx;a->z=oz+vz;a->vx=vx;a->vz=vz;
                a->slide_axis=0u;a->slide_lock_ticks=0u;
                a->slide_dir_x_q15=0;a->slide_dir_z_q15=0;
                moved=1;break;
            }
        }
        if (!moved) {
            a->vx=0;a->vz=0;a->speed_fx=0;
            a->slide_axis=0u;a->slide_lock_ticks=0u;
            a->slide_dir_x_q15=0;a->slide_dir_z_q15=0;
        }
    }
    {
        uint32_t mag=odg_isqrt_u64((uint64_t)((int64_t)a->vx*a->vx+(int64_t)a->vz*a->vz));
        a->speed_fx=mag>INT32_MAX?INT32_MAX:(int32_t)mag;
    }
}

static int32_t terrain_speed_factor_q15(const odg_actor *a, int32_t dir_x_q15, int32_t dir_z_q15) {
    const int32_t sample = ODG_FX_ONE / 2;
    int32_t ax,az,bx,bz;
    int32_t dh;
    int32_t factor=ODG_Q15_ONE;
    if (!a) return factor;
    if (dir_x_q15==0 && dir_z_q15==0) return factor;
    ax=a->x+(int32_t)(((int64_t)dir_x_q15*sample)/ODG_Q15_ONE);
    az=a->z+(int32_t)(((int64_t)dir_z_q15*sample)/ODG_Q15_ONE);
    bx=a->x-(int32_t)(((int64_t)dir_x_q15*sample)/ODG_Q15_ONE);
    bz=a->z-(int32_t)(((int64_t)dir_z_q15*sample)/ODG_Q15_ONE);
    dh=odg_terrain_height_fx(ax,az)-odg_terrain_height_fx(bx,bz);
    /* Uphill work is visible in speed without turning slopes into sticky walls. Downhill
     * gets only a small assist so players cannot exploit hills as launch pads. */
    if (dh>0) factor-=odg_clamp_i32(dh*18,0,9000);
    else if (dh<0) factor+=odg_clamp_i32((-dh)*6,0,2500);
    return odg_clamp_i32(factor,22000,ODG_Q15_ONE+2500);
}

static int32_t steering_speed_factor_q15(int32_t face_x,int32_t face_z,
                                         int32_t desired_x,int32_t desired_z) {
    int32_t dot=q15_dot(face_x,face_z,desired_x,desired_z);
    if (dot>=28000) return ODG_Q15_ONE;
    if (dot>=0) return 19000+(int32_t)(((int64_t)dot*13767)/28000);
    if (dot>=-24000) return 8000+(int32_t)(((int64_t)(dot+24000)*11000)/24000);
    return 2500+(int32_t)(((int64_t)(dot+ODG_Q15_ONE)*5500)/(ODG_Q15_ONE-24000));
}


static void update_actor(odg_actor *a) {
    int32_t desired_x = 0;
    int32_t desired_z = 0;
    int32_t move_x = 0;
    int32_t move_z = 0;
    int32_t strength_q15 = 0;
    int32_t base_speed;
    int32_t target_speed = 0;
    uint32_t before_cell;
    uint32_t after_cell;
    if (!a || !a->active || a->hp == 0u || g_odg.match_over) return;

    if (a->dash_cd > 0u) --a->dash_cd;
    if (a->dash_ticks > 0u) --a->dash_ticks;
    if (a->flash_ticks > 0u) --a->flash_ticks;

    if (a->type == ODG_ACTOR_PLAYER) {
        int has_heading=0;
        if (g_odg.input.world_heading_mode!=0u) {
            /* v12 exact world-control path: the host has already resolved the fixed joystick
             * against the current camera and supplies one stable WORLD heading. Camera
             * chase can rotate freely without feeding back into translation, while a
             * new drag delta can bend this heading immediately on the next tick. */
            strength_q15=odg_clamp_i32(g_odg.input.move_strength_q15,0,ODG_Q15_ONE);
            if (strength_q15>ODG_PLAYER_INPUT_DEADZONE &&
                (g_odg.input.move_x_q15!=0 || g_odg.input.move_z_q15!=0)) {
                desired_x=g_odg.input.move_x_q15;
                desired_z=g_odg.input.move_z_q15;
                odg_normalize_q15(desired_x,desired_z,&desired_x,&desired_z);
                g_odg.control_heading_x_q15=desired_x;
                g_odg.control_heading_z_q15=desired_z;
                g_odg.control_strength_q15=strength_q15;
                g_odg.control_basis_x_q15=g_odg.camera_dir_x_q15;
                g_odg.control_basis_z_q15=g_odg.camera_dir_z_q15;
                g_odg.control_active=1u;
                a->control_raw_x_q15=0;
                a->control_raw_z_q15=0;
                has_heading=1;
            }
        } else {
            int32_t lx=g_odg.input.move_x_q15;
            int32_t lf=g_odg.input.move_z_q15;
            uint32_t mag=odg_isqrt_u64((uint64_t)((int64_t)lx*lx+(int64_t)lf*lf));
            if(mag>(uint32_t)ODG_PLAYER_INPUT_DEADZONE){
                int32_t raw_x,raw_z;
                int32_t fx=g_odg.camera_dir_x_q15,fz=g_odg.camera_dir_z_q15;
                int32_t rx=fz,rz=-fx;
                uint32_t cmag=mag>(uint32_t)ODG_Q15_ONE?(uint32_t)ODG_Q15_ONE:mag;
                odg_normalize_q15(lx,lf,&raw_x,&raw_z);
                strength_q15=(int32_t)(((uint64_t)(cmag-(uint32_t)ODG_PLAYER_INPUT_DEADZONE)*(uint32_t)ODG_Q15_ONE)/
                                       (uint32_t)(ODG_Q15_ONE-ODG_PLAYER_INPUT_DEADZONE));
                /* v12 camera-relative locomotion: the fixed joystick is a local vector,
                 * never a world-heading dial. Holding UP while the independently controlled
                 * camera rotates continuously rotates the requested world velocity too. */
                desired_x=(int32_t)((((int64_t)rx*raw_x)+((int64_t)fx*raw_z))/ODG_Q15_ONE);
                desired_z=(int32_t)((((int64_t)rz*raw_x)+((int64_t)fz*raw_z))/ODG_Q15_ONE);
                odg_normalize_q15(desired_x,desired_z,&desired_x,&desired_z);
                g_odg.control_basis_x_q15=fx;g_odg.control_basis_z_q15=fz;
                g_odg.control_heading_x_q15=desired_x;g_odg.control_heading_z_q15=desired_z;
                g_odg.control_strength_q15=strength_q15;g_odg.control_active=1u;
                a->control_raw_x_q15=raw_x;a->control_raw_z_q15=raw_z;
                has_heading=1;
            }
        }
        if (!has_heading) {
            g_odg.control_active = 0u;
            g_odg.control_heading_x_q15 = 0;
            g_odg.control_heading_z_q15 = 0;
            g_odg.control_strength_q15 = 0;
            a->control_raw_x_q15 = 0;
            a->control_raw_z_q15 = 0;
            a->steer_q15 = 0;
            strength_q15=0;
        } else {
            a->steer_q15=q15_cross(a->face_x_q15,a->face_z_q15,desired_x,desired_z);
            rotate_vec_inertial_toward(&a->face_x_q15,&a->face_z_q15,desired_x,desired_z,
                                       &a->turn_rate_q15,ODG_PLAYER_TURN_MAX_SIN_Q15,
                                       ODG_PLAYER_TURN_ACCEL_Q15,&a->turn_sign);
            /* Precision rule: the joystick owns ground-path direction immediately. Body
             * orientation is a separate inertial presentation/steering state, so it may lag
             * visually without dragging the player's trajectory with it. Scalar speed still
             * accelerates/brakes smoothly, and approach_heading_velocity preserves the one
             * deliberate exception: a near-180 degree reversal brakes before translating
             * backward. This makes camera-relative UP follow camera yaw on the same tick. */
            move_x=desired_x;
            move_z=desired_z;
        }
        if ((g_odg.input.buttons & ODG_BUTTON_DASH) != 0u && a->dash_cd == 0u && strength_q15 != 0) {
            a->dash_cd = ODG_DASH_COOLDOWN_TICKS;
            a->dash_ticks = ODG_DASH_DURATION_TICKS;
        }
        base_speed = ODG_PLAYER_SPEED_FX;
    } else {
        bot_control(a,&desired_x,&desired_z);
        strength_q15=(desired_x!=0 || desired_z!=0)?ODG_Q15_ONE:0;
        if (strength_q15!=0) {
            odg_normalize_q15(desired_x,desired_z,&desired_x,&desired_z);
            a->steer_q15=q15_cross(a->face_x_q15,a->face_z_q15,desired_x,desired_z);
            rotate_vec_inertial_toward(&a->face_x_q15,&a->face_z_q15,desired_x,desired_z,
                                       &a->turn_rate_q15,ODG_BOT_TURN_MAX_SIN_Q15,
                                       ODG_BOT_TURN_ACCEL_Q15,&a->turn_sign);
            if (a->vx != 0 || a->vz != 0) {
                steer_translation_heading(a->vx, a->vz, desired_x, desired_z,
                                           ODG_BOT_MOVE_TURN_MAX_SIN_Q15,
                                           &move_x, &move_z);
            } else {
                steer_translation_heading(a->face_x_q15, a->face_z_q15,
                                           desired_x, desired_z,
                                           ODG_BOT_MOVE_TURN_MAX_SIN_Q15,
                                           &move_x, &move_z);
            }
        } else {
            a->steer_q15=0;
        }
        base_speed=ODG_BOT_SPEED_FX;
    }

    if (strength_q15!=0) {
        int32_t steering_factor = a->type == ODG_ACTOR_PLAYER ? ODG_Q15_ONE :
            steering_speed_factor_q15(a->face_x_q15,a->face_z_q15,desired_x,desired_z);
        target_speed=(int32_t)(((int64_t)base_speed*strength_q15)/ODG_Q15_ONE);
        target_speed=(int32_t)(((int64_t)target_speed*steering_factor)/ODG_Q15_ONE);
        target_speed=(int32_t)(((int64_t)target_speed*terrain_speed_factor_q15(a,move_x,move_z))/ODG_Q15_ONE);
    }
    if (a->type==ODG_ACTOR_PLAYER && a->dash_ticks>0u)
        target_speed=(target_speed*ODG_DASH_SPEED_NUM)/ODG_DASH_SPEED_DEN;

    {
        int32_t accel=a->type==ODG_ACTOR_PLAYER?8:6;
        int32_t brake=a->type==ODG_ACTOR_PLAYER?11:8;
        approach_heading_velocity(a,move_x,move_z,target_speed,accel,brake);
    }

    before_cell=odg_cell_from_world(a->x,a->z);
    move_actor_with_slide(a);
    resolve_actor_obstacles(a);
    after_cell=odg_cell_from_world(a->x,a->z);
    if (after_cell!=before_cell || after_cell!=a->last_cell) process_actor_cell(a,after_cell);

    if (a->type==ODG_ACTOR_BOT) {
        ++a->progress_ticks;
        if (a->progress_ticks>=ODG_BOT_PROGRESS_WINDOW_TICKS) {
            int64_t pd2=odg_dist2(a->x,a->z,a->progress_x,a->progress_z);
            int64_t min2=(int64_t)ODG_BOT_PROGRESS_MIN_FX*ODG_BOT_PROGRESS_MIN_FX;
            if (strength_q15!=0 && pd2<min2) {
                ++a->stuck_windows;
                a->ai_commit_ticks=0u;
                a->think_cd=0u;
                a->turn_sign=-a->turn_sign;
                a->slide_lock_ticks=0u;
                a->slide_axis=0u;
                a->slide_dir_x_q15=0;a->slide_dir_z_q15=0;
                if (a->trail_active) bot_set_return_direction(a);
                else bot_choose_expansion(a);
            } else if (a->stuck_windows>0u) {
                --a->stuck_windows;
            }
            a->progress_x=a->x;a->progress_z=a->z;a->progress_ticks=0u;
        }
    }
}


void odg_update_turret_ownership_internal(void) {
    uint32_t ti;
    /* Neutral infrastructure is commissioned by territorial control. Once programmed,
     * ground paint alone can never flip it again: changing an enemy turret still requires
     * a physical reprogram chip. A strict majority of the playable cells in the local
     * 5x5 neighborhood makes coastal turrets attainable without counting ocean cells. */
    for (ti=0u;ti<g_odg.turret_count;++ti) {
        odg_turret *t=&g_odg.turrets[ti];
        uint32_t counts[ODG_MAX_ACTORS]={0u};
        uint32_t playable=0u;
        uint32_t best_id=UINT32_MAX;
        uint32_t best_count=0u;
        uint32_t center;
        int32_t cx,cz,dz;
        if (!t->active || t->owner!=ODG_TURRET_NEUTRAL || t->carried_by!=ODG_TURRET_NONE) continue;
        center=odg_cell_from_world(t->x,t->z);
        if (center>=ODG_CELL_COUNT || g_odg.playable[center]==0u) continue;
        cx=(int32_t)cell_x(center);cz=(int32_t)cell_z(center);
        for (dz=-ODG_TURRET_CAPTURE_RADIUS;dz<=ODG_TURRET_CAPTURE_RADIUS;++dz) {
            int32_t dx;
            for (dx=-ODG_TURRET_CAPTURE_RADIUS;dx<=ODG_TURRET_CAPTURE_RADIUS;++dx) {
                int32_t x=cx+dx,z=cz+dz;
                uint32_t cell,id;
                uint8_t owner;
                if (!cell_xy_in_bounds(x,z)) continue;
                cell=(uint32_t)z*ODG_GRID_SIZE+(uint32_t)x;
                if (g_odg.playable[cell]==0u) continue;
                ++playable;
                owner=g_odg.territory[cell];
                if (owner==ODG_OWNER_NONE) continue;
                id=ODG_ID_FROM_OWNER(owner);
                if (id<ODG_MAX_ACTORS && g_odg.actors[id].active && g_odg.actors[id].hp!=0u) ++counts[id];
            }
        }
        for (cx=0;cx<(int32_t)ODG_MAX_ACTORS;++cx) {
            uint32_t id=(uint32_t)cx;
            if (counts[id]>best_count) {best_count=counts[id];best_id=id;}
        }
        if (best_id==UINT32_MAX || playable==0u || best_count<=playable/2u) continue;
        t->owner=ODG_OWNER_FROM_ID(best_id);
        t->fire_cd=t->fire_period;
        t->beam_ticks=0u;
        t->target_kind=ODG_TURRET_TARGET_NONE;
        t->last_target_cell=UINT32_MAX;
        t->target_actor_id=UINT32_MAX;
        t->aim_ticks=0u;
        t->retarget_cd=ODG_TURRET_RETARGET_GRACE_TICKS;
        odg_emit_particles(t->x,t->z,0x8ce8ffffu,18u);
    }
}

static int turret_position_separated(int32_t x, int32_t z, uint32_t upto) {
    uint32_t i;
    for (i = 0u; i < ODG_MAX_ACTORS; ++i) {
        if (g_odg.actors[i].active && odg_dist2(x,z,g_odg.actors[i].x,g_odg.actors[i].z) <
            (int64_t)(8 * ODG_FX_ONE) * (8 * ODG_FX_ONE)) return 0;
    }
    for (i = 0u; i < upto; ++i) {
        if (g_odg.turrets[i].active && odg_dist2(x,z,g_odg.turrets[i].x,g_odg.turrets[i].z) <
            (int64_t)(10 * ODG_FX_ONE) * (10 * ODG_FX_ONE)) return 0;
    }
    return 1;
}

static void spawn_turrets(void) {
    uint32_t i;
    g_odg.turret_count = ODG_MAX_TURRETS;
    for (i = 0u; i < ODG_MAX_TURRETS; ++i) {
        odg_turret *t = &g_odg.turrets[i];
        uint32_t attempt;
        odg_memset(t, 0, sizeof(*t));
        t->active = 1u;
        t->id = i;
        t->owner = ODG_TURRET_NEUTRAL;
        t->ammo = ODG_TURRET_AMMO;
        t->max_ammo = ODG_TURRET_AMMO;
        t->fire_period = ODG_TURRET_FIRE_PERIOD + odg_rand_bounded(&g_odg.rng, ODG_TURRET_FIRE_JITTER + 1u);
        t->fire_cd = t->fire_period;
        t->range_fx = ODG_TURRET_RANGE_FX + (int32_t)odg_rand_bounded(&g_odg.rng, 3u * ODG_FX_ONE);
        t->carried_by = ODG_TURRET_NONE;
        t->last_target_cell = UINT32_MAX;
        t->target_kind = ODG_TURRET_TARGET_NONE;
        t->target_actor_id = UINT32_MAX;
        t->retarget_cd = 0u;
        t->head_x_q15 = 0;
        t->head_z_q15 = ODG_Q15_ONE;
        t->head_turn_sign = 1;
        t->head_turn_rate_q15 = 0;
        t->aim_required = ODG_TURRET_LOCK_TICKS + odg_rand_bounded(&g_odg.rng, ODG_TURRET_LOCK_JITTER + 1u);
        {
            int placed = 0;
            for (attempt = 0u; attempt < 300u; ++attempt) {
                uint32_t c = odg_rand_bounded(&g_odg.rng, ODG_CELL_COUNT);
                int32_t x;
                int32_t z;
                if (g_odg.playable[c] == 0u || g_odg.territory[c] != ODG_OWNER_NONE) continue;
                x = odg_cell_center_x(c); z = odg_cell_center_z(c);
                if (!position_clear(x,z,ODG_FX_ONE) || !turret_position_separated(x,z,i)) continue;
                t->x = x; t->z = z; placed = 1; break;
            }
            if (!placed) {
                uint32_t c;
                for (c=i;c<ODG_CELL_COUNT;c+=ODG_MAX_TURRETS) {
                    int32_t x,z;
                    if (g_odg.playable[c]==0u || g_odg.territory[c]!=ODG_OWNER_NONE) continue;
                    x=odg_cell_center_x(c); z=odg_cell_center_z(c);
                    if (position_clear(x,z,ODG_FX_ONE) && turret_position_separated(x,z,i)) {
                        t->x=x; t->z=z; placed=1; break;
                    }
                }
            }
            if (!placed) { t->active=0u; }
        }
    }
}

static uint32_t find_near_owned_turret(const odg_actor *p) {
    uint32_t i;
    uint32_t best = UINT32_MAX;
    int64_t best_d2 = (int64_t)(3 * ODG_FX_ONE) * (3 * ODG_FX_ONE);
    uint8_t own;
    if (!p) return UINT32_MAX;
    own = ODG_OWNER_FROM_ID(p->id);
    { uint32_t pc=odg_cell_from_world(p->x,p->z); if(pc>=ODG_CELL_COUNT || g_odg.territory[pc]!=own) return UINT32_MAX; }
    for (i = 0u; i < g_odg.turret_count; ++i) {
        odg_turret *t = &g_odg.turrets[i];
        int64_t d2;
        uint32_t tc;
        if (!t->active || t->owner != own || t->carried_by != ODG_TURRET_NONE) continue;
        tc=odg_cell_from_world(t->x,t->z);
        /* An owned turret can only be picked up while it is physically inside the
         * owner's current domain. Border placement is valid; hostile/neutral ground is not. */
        if (tc>=ODG_CELL_COUNT || g_odg.territory[tc]!=own) continue;
        d2 = odg_dist2(p->x,p->z,t->x,t->z);
        if (d2 <= best_d2) { best_d2 = d2; best = i; }
    }
    return best;
}

static uint32_t find_near_enemy_turret(const odg_actor *p) {
    uint32_t i,best=UINT32_MAX;
    int64_t best_d2=(int64_t)ODG_CHIP_HACK_RANGE_FX*ODG_CHIP_HACK_RANGE_FX;
    uint8_t own;
    if (!p) return UINT32_MAX;
    own=ODG_OWNER_FROM_ID(p->id);
    for(i=0u;i<g_odg.turret_count;++i){
        const odg_turret *t=&g_odg.turrets[i];
        int64_t d2;
        /* Neutral turrets are earned by local territorial majority, never by spending a
         * chip. Only already-programmed enemy infrastructure is a reprogram target. */
        if(!turret_is_reprogrammable_enemy(t,own)) continue;
        d2=odg_dist2(p->x,p->z,t->x,t->z);
        if(d2<=best_d2){best_d2=d2;best=i;}
    }
    return best;
}

static int consume_chip_and_hack(uint32_t actor_id,uint32_t turret_id){
    odg_actor *a;
    odg_chip *c;
    odg_turret *t;
    if(actor_id>=ODG_MAX_ACTORS || turret_id>=g_odg.turret_count) return 0;
    a=&g_odg.actors[actor_id];
    if(!a->active || a->hp==0u || a->carried_chip>=g_odg.chip_count) return 0;
    c=&g_odg.chips[a->carried_chip];t=&g_odg.turrets[turret_id];
    if(!c->active || c->kind!=ODG_CHIP_KIND_REPROGRAM || c->carried_by!=actor_id ||
       !turret_is_reprogrammable_enemy(t,ODG_OWNER_FROM_ID(actor_id))) return 0;
    if(odg_dist2(a->x,a->z,t->x,t->z)>(int64_t)ODG_CHIP_HACK_RANGE_FX*ODG_CHIP_HACK_RANGE_FX) return 0;
    t->owner=ODG_OWNER_FROM_ID(actor_id);
    t->fire_cd=t->fire_period;
    t->target_kind=ODG_TURRET_TARGET_NONE;t->last_target_cell=UINT32_MAX;t->target_actor_id=UINT32_MAX;
    t->aim_ticks=0u;t->retarget_cd=ODG_TURRET_RETARGET_GRACE_TICKS;
    c->active=0u;c->carried_by=UINT32_MAX;a->carried_chip=UINT32_MAX;
    odg_emit_particles(t->x,t->z,0xc983ffffu,24u);
    return 1;
}

int odg_turret_drop_candidate_internal(const odg_actor *p, int32_t *out_x, int32_t *out_z) {
    const int32_t distance_fx = 19 * ODG_FX_ONE / 10;
    int32_t x;
    int32_t z;
    uint32_t i,cell;
    uint8_t own;
    if (!p || !out_x || !out_z) return 0;
    own=ODG_OWNER_FROM_ID(p->id);
    if(g_odg.territory[odg_cell_from_world(p->x,p->z)]!=own) return 0;
    x = p->x + (int32_t)(((int64_t)p->face_x_q15 * distance_fx) / ODG_Q15_ONE);
    z = p->z + (int32_t)(((int64_t)p->face_z_q15 * distance_fx) / ODG_Q15_ONE);
    cell=odg_cell_from_world(x,z);
    if (cell>=ODG_CELL_COUNT || g_odg.territory[cell]!=own) return 0;
    if (!disk_playable(x,z,ODG_FX_ONE) || !position_clear(x,z,ODG_FX_ONE)) return 0;
    for (i=0u;i<g_odg.turret_count;++i) {
        const odg_turret *t=&g_odg.turrets[i];
        if (!t->active || t->carried_by!=ODG_TURRET_NONE) continue;
        if (odg_dist2(x,z,t->x,t->z) < (int64_t)(2*ODG_FX_ONE)*(2*ODG_FX_ONE)) return 0;
    }
    *out_x=x;
    *out_z=z;
    return 1;
}

static void drop_player_turret_if_valid(odg_actor *p){
    if(g_odg.player_carried_turret<g_odg.turret_count){
        odg_turret *t=&g_odg.turrets[g_odg.player_carried_turret];
        int32_t x,z;
        if(odg_turret_drop_candidate_internal(p,&x,&z)){
            t->x=x;t->z=z;t->carried_by=ODG_TURRET_NONE;t->fire_cd=t->fire_period;
            t->target_kind=ODG_TURRET_TARGET_NONE;t->aim_ticks=0u;t->last_target_cell=UINT32_MAX;
            t->target_actor_id=UINT32_MAX;t->retarget_cd=ODG_TURRET_RETARGET_GRACE_TICKS;
            g_odg.player_carried_turret=ODG_TURRET_NONE;
        }
    }
}

static void handle_turret_action(void) {
    odg_actor *p = &g_odg.actors[ODG_PLAYER_ID];
    uint32_t action_now = g_odg.input.buttons & ODG_BUTTON_ACTION;
    uint32_t action_prev = g_odg.prev_buttons & ODG_BUTTON_ACTION;
    if (action_now == 0u || action_prev != 0u || p->hp == 0u) return;

    /* A reprogram chip is contextual and has priority near any turret not already ours. */
    if(p->carried_chip<g_odg.chip_count){
        uint32_t enemy=find_near_enemy_turret(p);
        if(enemy<g_odg.turret_count && consume_chip_and_hack(ODG_PLAYER_ID,enemy)) return;
    }
    if (g_odg.player_carried_turret < g_odg.turret_count) {
        drop_player_turret_if_valid(p);
    } else {
        /* Carrying ammunition no longer hides/disables turret interaction. Partial ammo
         * transfer happens automatically in update_ammo_crates(), while ACTION remains
         * available to pick up the owned turret itself. */
        uint32_t best = find_near_owned_turret(p);
        if (best < g_odg.turret_count) {
            g_odg.player_carried_turret = best;
            g_odg.turrets[best].carried_by = ODG_PLAYER_ID;
            g_odg.turrets[best].target_kind=ODG_TURRET_TARGET_NONE;
            g_odg.turrets[best].aim_ticks=0u;
            g_odg.turrets[best].last_target_cell=UINT32_MAX;
            g_odg.turrets[best].target_actor_id=UINT32_MAX;
        }
    }
}


static uint32_t turret_find_trail_target_for_actor(const odg_turret *t,uint32_t actor_filter) {
    int32_t r = t->range_fx / ODG_CELL_FX;
    uint32_t center = odg_cell_from_world(t->x,t->z);
    int32_t cx = (int32_t)cell_x(center);
    int32_t cz = (int32_t)cell_z(center);
    int64_t best_d2 = INT64_MAX;
    uint32_t best = UINT32_MAX;
    int64_t min_d2=(int64_t)ODG_TURRET_MIN_TARGET_FX*ODG_TURRET_MIN_TARGET_FX;
    int32_t dz;
    for (dz=-r; dz<=r; ++dz) {
        int32_t dx;
        for (dx=-r; dx<=r; ++dx) {
            int32_t x=cx+dx,z=cz+dz;
            uint32_t c,trail_id;
            uint8_t tr;
            int64_t d2;
            if (!cell_xy_in_bounds(x,z)) continue;
            c=(uint32_t)z*ODG_GRID_SIZE+(uint32_t)x;
            tr=g_odg.trail_owner[c];
            if (tr==ODG_OWNER_NONE || tr==t->owner) continue;
            trail_id=ODG_ID_FROM_OWNER(tr);
            if(trail_id>=ODG_MAX_ACTORS || (actor_filter<ODG_MAX_ACTORS && trail_id!=actor_filter)) continue;
            if(g_odg.actors[trail_id].trail_len<ODG_TURRET_TRAIL_MIN_CELLS) continue;
            d2=odg_dist2(t->x,t->z,odg_cell_center_x(c),odg_cell_center_z(c));
            /* Never choose the square directly underneath the turret. It creates a fake
             * vertical-looking shot and makes the head appear to disappear into its base. */
            if (d2 < min_d2) continue;
            if (d2 <= (int64_t)t->range_fx*t->range_fx && d2 < best_d2) {best_d2=d2;best=c;}
        }
    }
    return best;
}

static uint32_t turret_find_trail_target(const odg_turret *t) {
    return turret_find_trail_target_for_actor(t,UINT32_MAX);
}


static uint32_t turret_find_territory_target(const odg_turret *t) {
    int32_t r = t->range_fx / ODG_CELL_FX;
    uint32_t center = odg_cell_from_world(t->x,t->z);
    int32_t cx = (int32_t)cell_x(center);
    int32_t cz = (int32_t)cell_z(center);
    int64_t best_d2 = INT64_MAX;
    uint32_t best = UINT32_MAX;
    int32_t dz;
    for (dz=-r; dz<=r; ++dz) {
        int32_t dx;
        for (dx=-r; dx<=r; ++dx) {
            int32_t x=cx+dx,z=cz+dz;
            uint32_t c;
            uint8_t owner;
            int64_t d2;
            if (!cell_xy_in_bounds(x,z)) continue;
            c=(uint32_t)z*ODG_GRID_SIZE+(uint32_t)x;
            if (g_odg.playable[c]==0u) continue;
            owner=g_odg.territory[c];
            if (owner==ODG_OWNER_NONE || owner==t->owner) continue;
            d2=odg_dist2(t->x,t->z,odg_cell_center_x(c),odg_cell_center_z(c));
            if (d2 <= (int64_t)t->range_fx*t->range_fx && d2 < best_d2) {best_d2=d2;best=c;}
        }
    }
    return best;
}

static int turret_target_valid(const odg_turret *t) {
    uint32_t c;
    if (!t || t->last_target_cell>=ODG_CELL_COUNT || t->owner==ODG_TURRET_NEUTRAL) return 0;
    c=t->last_target_cell;
    if (odg_dist2(t->x,t->z,odg_cell_center_x(c),odg_cell_center_z(c)) > (int64_t)t->range_fx*t->range_fx) return 0;
    if (t->target_kind==ODG_TURRET_TARGET_TRAIL) {
        uint8_t tr=g_odg.trail_owner[c];
        if(tr==ODG_OWNER_NONE || tr==t->owner) return 0;
        if(t->target_actor_id<ODG_MAX_ACTORS && ODG_ID_FROM_OWNER(tr)!=t->target_actor_id) return 0;
        return odg_dist2(t->x,t->z,odg_cell_center_x(c),odg_cell_center_z(c)) >=
               (int64_t)ODG_TURRET_MIN_TARGET_FX*ODG_TURRET_MIN_TARGET_FX;
    }
    if (t->target_kind==ODG_TURRET_TARGET_TERRITORY)
        return g_odg.playable[c]!=0u && g_odg.territory[c]!=ODG_OWNER_NONE && g_odg.territory[c]!=t->owner;
    return 0;
}

static void turret_clear_target(odg_turret *t) {
    if (!t) return;
    t->target_kind=ODG_TURRET_TARGET_NONE;
    t->target_actor_id=UINT32_MAX;
    t->aim_ticks=0u;
    if (t->beam_ticks==0u) t->last_target_cell=UINT32_MAX;
}

static void turret_fire_locked(odg_turret *t) {
    uint32_t c;
    uint32_t owner_id;
    if (!t || t->owner==ODG_TURRET_NEUTRAL || t->ammo==0u || t->carried_by!=ODG_TURRET_NONE || !turret_target_valid(t)) {
        turret_clear_target(t); return;
    }
    owner_id=ODG_ID_FROM_OWNER(t->owner);
    if (owner_id>=ODG_MAX_ACTORS || g_odg.actors[owner_id].hp==0u) { turret_clear_target(t); return; }
    c=t->last_target_cell;
    if (t->target_kind==ODG_TURRET_TARGET_TRAIL) {
        uint8_t victim_owner=g_odg.trail_owner[c];
        uint32_t victim_id=ODG_ID_FROM_OWNER(victim_owner);
        --t->ammo; ++t->shots_fired; t->fire_cd=t->fire_period; t->beam_ticks=14u;
        odg_emit_particles(odg_cell_center_x(c),odg_cell_center_z(c),0xffd36bffu,14u);
        if (victim_id<ODG_MAX_ACTORS) eliminate_actor(&g_odg.actors[victim_id],owner_id,ODG_DEATH_TURRET_TRAIL_CUT);
        turret_clear_target(t);
        return;
    }
    if (t->target_kind==ODG_TURRET_TARGET_TERRITORY) {
        uint8_t old=g_odg.territory[c];
        uint32_t old_id=ODG_ID_FROM_OWNER(old);
        --t->ammo; ++t->shots_fired; ++t->cells_conquered; t->fire_cd=t->fire_period; t->beam_ticks=14u;
        set_territory_owner(c,t->owner);
        sync_actor_score(owner_id);
        if (old_id<ODG_MAX_ACTORS) {
            sync_actor_score(old_id);
            if (g_odg.actors[old_id].hp!=0u && g_odg.territory_count[old_id]==0u)
                eliminate_actor(&g_odg.actors[old_id],owner_id,ODG_DEATH_TERRITORY_LOST);
        }
        odg_emit_particles(odg_cell_center_x(c),odg_cell_center_z(c),0xffd36bffu,8u);
        odg_update_turret_ownership_internal();
        turret_clear_target(t);
    }
}

static void turret_acquire_target(odg_turret *t) {
    uint32_t c;
    if (!t || t->owner==ODG_TURRET_NEUTRAL || t->ammo==0u || t->retarget_cd!=0u) return;
    c=turret_find_trail_target(t);
    if (c!=UINT32_MAX) {
        uint8_t tr=g_odg.trail_owner[c];
        t->target_kind=ODG_TURRET_TARGET_TRAIL;
        t->target_actor_id=tr!=ODG_OWNER_NONE?ODG_ID_FROM_OWNER(tr):UINT32_MAX;
    } else {
        c=turret_find_territory_target(t);
        if (c!=UINT32_MAX) {t->target_kind=ODG_TURRET_TARGET_TERRITORY;t->target_actor_id=UINT32_MAX;}
    }
    if (c!=UINT32_MAX) {
        t->last_target_cell=c;
        t->aim_ticks=t->aim_required;
    }
}

static void update_turrets(void) {
    uint32_t i;
    odg_actor *p=&g_odg.actors[ODG_PLAYER_ID];
    if (g_odg.player_carried_turret<g_odg.turret_count) {
        odg_turret *ct=&g_odg.turrets[g_odg.player_carried_turret];
        ct->x=p->x;ct->z=p->z;
    }
    for (i=0u;i<g_odg.turret_count;++i) {
        odg_turret *t=&g_odg.turrets[i];
        if (!t->active) continue;
        if (t->beam_ticks>0u) --t->beam_ticks;
        if (t->retarget_cd>0u) --t->retarget_cd;
        if (t->carried_by!=ODG_TURRET_NONE) continue;
        if (t->owner==ODG_TURRET_NEUTRAL || t->ammo==0u) { turret_clear_target(t); continue; }

        /* Head yaw is an inertial state. It tracks the committed target cell horizontally
         * and never pitches into the ground, so running near the base cannot make the
         * turret look headless or spin between adjacent squares. */
        if(t->target_kind!=ODG_TURRET_TARGET_NONE && t->last_target_cell<ODG_CELL_COUNT){
            int32_t dx=odg_cell_center_x(t->last_target_cell)-t->x;
            int32_t dz=odg_cell_center_z(t->last_target_cell)-t->z;
            int32_t tx=0,tz=0;
            odg_normalize_q15(dx,dz,&tx,&tz);
            if(tx!=0 || tz!=0)
                rotate_vec_inertial_toward(&t->head_x_q15,&t->head_z_q15,tx,tz,
                                           &t->head_turn_rate_q15,2200,3800,&t->head_turn_sign);
        } else {
            t->head_turn_rate_q15=approach_signed(t->head_turn_rate_q15,0,2500);
        }

        if (t->fire_cd>0u) { --t->fire_cd; continue; }
        if (t->target_kind==ODG_TURRET_TARGET_NONE) { turret_acquire_target(t); continue; }
        if (!turret_target_valid(t)) {
            /* Do not immediately jump to a different square. A short retarget grace makes
             * the lock legible and prevents decision chatter while a trail evolves. */
            turret_clear_target(t);t->retarget_cd=ODG_TURRET_RETARGET_GRACE_TICKS;continue;
        }
        if (t->aim_ticks>0u) { --t->aim_ticks; continue; }
        turret_fire_locked(t);
    }
}


static uint32_t refill_nearby_owned_turrets(uint32_t actor_id, uint32_t ammo) {
    uint8_t owner;
    odg_actor *a;
    uint32_t pass=0u;
    if (actor_id>=ODG_MAX_ACTORS || ammo==0u) return ammo;
    a=&g_odg.actors[actor_id];
    owner=ODG_OWNER_FROM_ID(actor_id);
    /* Captured ammunition is carried as reserve. It does not teleport across the map:
     * the actor must come within loading distance of an owned turret. */
    while (ammo!=0u && pass<ODG_MAX_TURRETS) {
        uint32_t i;
        uint32_t best=UINT32_MAX;
        int64_t best_d2=INT64_MAX;
        for (i=0u;i<g_odg.turret_count;++i) {
            odg_turret *t=&g_odg.turrets[i];
            int64_t d2;
            if (!t->active || t->owner!=owner || t->carried_by!=ODG_TURRET_NONE || t->ammo>=t->max_ammo) continue;
            d2=odg_dist2(a->x,a->z,t->x,t->z);
            if (d2>(int64_t)ODG_AMMO_DELIVERY_RANGE_FX*ODG_AMMO_DELIVERY_RANGE_FX) continue;
            if (d2<best_d2) {best=i;best_d2=d2;}
        }
        if (best==UINT32_MAX) break;
        {
            odg_turret *t=&g_odg.turrets[best];
            uint32_t need=t->max_ammo-t->ammo;
            uint32_t give=odg_min_u32(need,ammo);
            t->ammo+=give;
            ammo-=give;
            odg_emit_particles(t->x,t->z,0xffdf72ffu,odg_min_u32(give,8u));
        }
        ++pass;
    }
    return ammo;
}

static void award_capture_ammo(uint32_t actor_id, uint32_t gained) {
    odg_actor *a;
    uint32_t reward;
    uint32_t room;
    if (actor_id>=ODG_MAX_ACTORS || gained==0u) return;
    a=&g_odg.actors[actor_id];
    /* Preserve fractional progress across many small captures. Roughly every 10 newly
     * acquired cells becomes one round of supply, so expansion itself fuels defense. */
    a->capture_ammo_credit += gained;
    reward=a->capture_ammo_credit/ODG_CAPTURE_CELLS_PER_AMMO;
    a->capture_ammo_credit%=ODG_CAPTURE_CELLS_PER_AMMO;
    if (reward==0u) return;
    if (reward>24u) reward=24u;
    room=a->ammo_reserve<ODG_AMMO_RESERVE_MAX?ODG_AMMO_RESERVE_MAX-a->ammo_reserve:0u;
    a->ammo_reserve+=odg_min_u32(room,reward);
}

static int ammo_crate_separated(int32_t x,int32_t z,uint32_t upto) {
    uint32_t i;
    for (i=0u;i<g_odg.turret_count;++i) if (g_odg.turrets[i].active && odg_dist2(x,z,g_odg.turrets[i].x,g_odg.turrets[i].z)<(int64_t)(4*ODG_FX_ONE)*(4*ODG_FX_ONE)) return 0;
    for (i=0u;i<upto;++i) if (g_odg.ammo_crates[i].active && odg_dist2(x,z,g_odg.ammo_crates[i].x,g_odg.ammo_crates[i].z)<(int64_t)(5*ODG_FX_ONE)*(5*ODG_FX_ONE)) return 0;
    return 1;
}

static void spawn_ammo_crates(void) {
    uint32_t i;
    g_odg.ammo_crate_count=ODG_INITIAL_AMMO_CRATES;
    for (i=0u;i<ODG_INITIAL_AMMO_CRATES;++i) {
        odg_ammo_crate *c=&g_odg.ammo_crates[i];
        uint32_t attempt;
        odg_memset(c,0,sizeof(*c)); c->active=1u; c->id=i; c->carried_by=UINT32_MAX;
        c->ammo=ODG_AMMO_CRATE_MIN+odg_rand_bounded(&g_odg.rng,ODG_AMMO_CRATE_SPAN+1u);
        for (attempt=0u;attempt<400u;++attempt) {
            uint32_t cell=odg_rand_bounded(&g_odg.rng,ODG_CELL_COUNT);
            int32_t x,z;
            if (g_odg.playable[cell]==0u || g_odg.territory[cell]!=ODG_OWNER_NONE) continue;
            x=odg_cell_center_x(cell); z=odg_cell_center_z(cell);
            if (!position_clear(x,z,ODG_FX_ONE/2) || !ammo_crate_separated(x,z,i)) continue;
            c->x=x;c->z=z;break;
        }
        if (attempt==400u) c->active=0u;
    }
}

static int chip_separated(int32_t x,int32_t z,uint32_t upto){
    uint32_t i;
    for(i=0u;i<upto;++i) if(g_odg.chips[i].active &&
        odg_dist2(x,z,g_odg.chips[i].x,g_odg.chips[i].z)<(int64_t)(7*ODG_FX_ONE)*(7*ODG_FX_ONE)) return 0;
    for(i=0u;i<g_odg.turret_count;++i) if(g_odg.turrets[i].active &&
        odg_dist2(x,z,g_odg.turrets[i].x,g_odg.turrets[i].z)<(int64_t)(4*ODG_FX_ONE)*(4*ODG_FX_ONE)) return 0;
    return 1;
}

static void spawn_chips(void){
    uint32_t i;
    g_odg.chip_count=ODG_MAX_CHIPS;
    for(i=0u;i<ODG_MAX_CHIPS;++i){
        odg_chip *c=&g_odg.chips[i];
        uint32_t attempt;
        odg_memset(c,0,sizeof(*c));c->active=1u;c->id=i;c->kind=ODG_CHIP_KIND_REPROGRAM;c->carried_by=UINT32_MAX;
        for(attempt=0u;attempt<500u;++attempt){
            uint32_t cell=odg_rand_bounded(&g_odg.rng,ODG_CELL_COUNT);
            int32_t x,z;
            if(g_odg.playable[cell]==0u || g_odg.territory[cell]!=ODG_OWNER_NONE) continue;
            x=odg_cell_center_x(cell);z=odg_cell_center_z(cell);
            if(!position_clear(x,z,ODG_FX_ONE/2) || !chip_separated(x,z,i)) continue;
            c->x=x;c->z=z;break;
        }
        if(attempt==500u)c->active=0u;
    }
}

static void update_chips(void){
    uint32_t aid,ci0;
    for(ci0=0u;ci0<g_odg.chip_count;++ci0) if(g_odg.chips[ci0].active && g_odg.chips[ci0].carried_by==UINT32_MAX && g_odg.chips[ci0].pickup_cd>0u) --g_odg.chips[ci0].pickup_cd;
    for(aid=0u;aid<ODG_MAX_ACTORS;++aid){
        odg_actor *a=&g_odg.actors[aid];
        if(!a->active || a->hp==0u) continue;
        if(a->carried_chip<g_odg.chip_count){
            odg_chip *c=&g_odg.chips[a->carried_chip];
            int32_t rx=a->face_z_q15,rz=-a->face_x_q15;
            c->x=a->x+(int32_t)(((int64_t)rx*(7*ODG_FX_ONE/10)+(int64_t)a->face_x_q15*(-2*ODG_FX_ONE/10))/ODG_Q15_ONE);
            c->z=a->z+(int32_t)(((int64_t)rz*(7*ODG_FX_ONE/10)+(int64_t)a->face_z_q15*(-2*ODG_FX_ONE/10))/ODG_Q15_ONE);
            if(aid!=ODG_PLAYER_ID){
                uint32_t enemy=find_near_enemy_turret(a);
                if(enemy<g_odg.turret_count)(void)consume_chip_and_hack(aid,enemy);
            }
        }else if(!(aid==ODG_PLAYER_ID && g_odg.player_carried_turret<g_odg.turret_count)){
            uint32_t ci;
            for(ci=0u;ci<g_odg.chip_count;++ci){
                odg_chip *c=&g_odg.chips[ci];
                if(!c->active || c->carried_by!=UINT32_MAX || c->pickup_cd!=0u) continue;
                if(odg_dist2(a->x,a->z,c->x,c->z)<=(int64_t)ODG_CHIP_PICKUP_RANGE_FX*ODG_CHIP_PICKUP_RANGE_FX){
                    c->carried_by=aid;a->carried_chip=ci;odg_emit_particles(c->x,c->z,0xc983ffffu,10u);break;
                }
            }
        }
    }
}

static void handle_drop_action(void){
    odg_actor *p=&g_odg.actors[ODG_PLAYER_ID];
    uint32_t now=g_odg.input.buttons&ODG_BUTTON_DROP;
    uint32_t prev=g_odg.prev_buttons&ODG_BUTTON_DROP;
    if(now==0u || prev!=0u || p->hp==0u)return;
    if(g_odg.player_carried_turret<g_odg.turret_count){
        drop_player_turret_if_valid(p);
        return;
    }
    if(p->carried_chip<g_odg.chip_count){
        odg_chip *c=&g_odg.chips[p->carried_chip];
        c->x=p->x;c->z=p->z;c->carried_by=UINT32_MAX;c->pickup_cd=45u;p->carried_chip=UINT32_MAX;
        return;
    }
    if(p->carried_ammo_crate<g_odg.ammo_crate_count){
        odg_ammo_crate *c=&g_odg.ammo_crates[p->carried_ammo_crate];
        c->x=p->x;c->z=p->z;c->carried_by=UINT32_MAX;c->pickup_cd=45u;p->carried_ammo_crate=UINT32_MAX;
        return;
    }
    /* Territory-earned reserve is still a physical resource when the player chooses
     * to drop it. Materialize the reserve as a world crate at the cube's position so
     * DROP remains a general 'release what I carry' action rather than deleting ammo. */
    if(p->ammo_reserve!=0u){
        uint32_t ci=UINT32_MAX,i;
        for(i=0u;i<g_odg.ammo_crate_count;++i) if(!g_odg.ammo_crates[i].active){ci=i;break;}
        if(ci==UINT32_MAX && g_odg.ammo_crate_count<ODG_MAX_AMMO_CRATES) ci=g_odg.ammo_crate_count++;
        if(ci<ODG_MAX_AMMO_CRATES){
            odg_ammo_crate *c=&g_odg.ammo_crates[ci];
            odg_memset(c,0,sizeof(*c));c->active=1u;c->id=ci;c->x=p->x;c->z=p->z;
            c->ammo=p->ammo_reserve;c->carried_by=UINT32_MAX;c->pickup_cd=45u;p->ammo_reserve=0u;
            odg_emit_particles(c->x,c->z,0xffdf72ffu,8u);
        }
    }
}

static void update_ammo_crates(void) {
    uint32_t aid,ci0;
    for(ci0=0u;ci0<g_odg.ammo_crate_count;++ci0) if(g_odg.ammo_crates[ci0].active && g_odg.ammo_crates[ci0].carried_by==UINT32_MAX && g_odg.ammo_crates[ci0].pickup_cd>0u) --g_odg.ammo_crates[ci0].pickup_cd;
    for (aid=0u;aid<ODG_MAX_ACTORS;++aid) {
        odg_actor *a=&g_odg.actors[aid];
        if (!a->active || a->hp==0u) continue;
        if (a->carried_ammo_crate<g_odg.ammo_crate_count) {
            odg_ammo_crate *c=&g_odg.ammo_crates[a->carried_ammo_crate];
            uint32_t ti;
            int32_t rx=a->face_z_q15, rz=-a->face_x_q15;
            c->x=a->x+(int32_t)(((int64_t)rx*(-7*ODG_FX_ONE/10)+(int64_t)a->face_x_q15*(-2*ODG_FX_ONE/10))/ODG_Q15_ONE);
            c->z=a->z+(int32_t)(((int64_t)rz*(-7*ODG_FX_ONE/10)+(int64_t)a->face_z_q15*(-2*ODG_FX_ONE/10))/ODG_Q15_ONE);
            for (ti=0u;ti<g_odg.turret_count && c->ammo!=0u;++ti) {
                odg_turret *t=&g_odg.turrets[ti];
                if (!t->active || t->owner!=ODG_OWNER_FROM_ID(aid) || t->carried_by!=ODG_TURRET_NONE || t->ammo>=t->max_ammo) continue;
                if (odg_dist2(a->x,a->z,t->x,t->z) <= (int64_t)ODG_AMMO_DELIVERY_RANGE_FX*ODG_AMMO_DELIVERY_RANGE_FX) {
                    uint32_t give=odg_min_u32(c->ammo,t->max_ammo-t->ammo);
                    c->ammo-=give;t->ammo+=give;
                    odg_emit_particles(t->x,t->z,0xffdf72ffu,10u);
                }
            }
            if (c->ammo==0u) { c->active=0u;c->carried_by=UINT32_MAX;a->carried_ammo_crate=UINT32_MAX; }
        } else if (!(aid==ODG_PLAYER_ID && g_odg.player_carried_turret<g_odg.turret_count)) {
            uint32_t ci;
            for (ci=0u;ci<g_odg.ammo_crate_count;++ci) {
                odg_ammo_crate *c=&g_odg.ammo_crates[ci];
                if (!c->active || c->carried_by!=UINT32_MAX || c->pickup_cd!=0u) continue;
                if (odg_dist2(a->x,a->z,c->x,c->z) <= (int64_t)ODG_AMMO_PICKUP_RANGE_FX*ODG_AMMO_PICKUP_RANGE_FX) {
                    c->carried_by=aid;a->carried_ammo_crate=ci;break;
                }
            }
        }
        if (a->ammo_reserve!=0u) a->ammo_reserve=refill_nearby_owned_turrets(aid,a->ammo_reserve);
    }
}


void odg_emit_particles(int32_t x, int32_t z, uint32_t color, uint32_t count) {
    uint32_t i;
    uint32_t j;
    for (i = 0u; i < count; ++i) {
        for (j = 0u; j < ODG_MAX_PARTICLES; ++j) {
            if (!g_odg.particles[j].active) {
                odg_particle *p = &g_odg.particles[j];
                p->active = 1u;
                p->x = x;
                p->z = z;
                p->y_fx = 240;
                p->vx = odg_rand_range_fx(&g_odg.rng, -80, 81);
                p->vz = odg_rand_range_fx(&g_odg.rng, -80, 81);
                p->vy_fx = 65 + odg_rand_range_fx(&g_odg.rng, 0, 75);
                p->life = 22u + odg_rand_bounded(&g_odg.rng, 26u);
                p->color = color;
                break;
            }
        }
    }
}

static void update_particles(void) {
    uint32_t i;
    for (i = 0u; i < ODG_MAX_PARTICLES; ++i) {
        odg_particle *p = &g_odg.particles[i];
        if (!p->active) continue;
        p->x += p->vx / 8;
        p->z += p->vz / 8;
        p->y_fx += p->vy_fx / 8;
        p->vy_fx -= 18;
        p->vx = (p->vx * 15) / 16;
        p->vz = (p->vz * 15) / 16;
        if (p->life > 0u) --p->life;
        if (p->life == 0u || p->y_fx < 0) p->active = 0u;
    }
}

static void check_match_end(void) {
    uint32_t i;
    uint32_t alive = 0u;
    uint32_t last = UINT32_MAX;
    if (g_odg.match_over) return;
    for (i = 0u; i < ODG_MAX_ACTORS; ++i) {
        odg_actor *a = &g_odg.actors[i];
        if (a->active && a->hp != 0u) { ++alive; last = i; }
        if (a->active && a->hp != 0u &&
            g_odg.playable_count != 0u && g_odg.territory_count[i] * 1000u >= g_odg.playable_count * ODG_CAPTURE_WIN_PERMILLE) {
            g_odg.match_over = 1u;
            g_odg.winner_id = i;
            return;
        }
    }
    if (alive <= 1u) {
        g_odg.match_over = 1u;
        g_odg.winner_id = last;
    }
}

void odg_world_build(uint64_t seed) {
    uint32_t i;
    g_odg.seed = seed;
    g_odg.tick = 0u;
    g_odg.tick_accum_scaled = 0u;
    g_odg.obstacle_count = 0u;
    g_odg.turret_count = 0u;
    g_odg.ammo_crate_count = 0u;
    g_odg.chip_count = 0u;
    g_odg.player_carried_turret = ODG_TURRET_NONE;
    g_odg.prev_buttons = 0u;
    g_odg.match_over = 0u;
    g_odg.winner_id = UINT32_MAX;
    (void)odm_rng_seed(&g_odg.rng, seed, UINT64_C(0x5249465454455252));

    odg_memset(g_odg.playable, 0, sizeof(g_odg.playable));
    build_playable_mask();
    odg_memset(g_odg.territory, 0, sizeof(g_odg.territory));
    odg_memset(g_odg.trail_owner, 0, sizeof(g_odg.trail_owner));
    odg_memset(g_odg.territory_count, 0, sizeof(g_odg.territory_count));
    odg_memset(g_odg.particles, 0, sizeof(g_odg.particles));
    odg_memset(g_odg.turrets, 0, sizeof(g_odg.turrets));
    odg_memset(g_odg.ammo_crates, 0, sizeof(g_odg.ammo_crates));
    odg_memset(g_odg.chips, 0, sizeof(g_odg.chips));

    /* A 128m arena needs landmarks, not clutter. Structures are spaced into
     * recognizable districts so the player can build a stable mental map. */
    add_obstacle( -9, -8, 1, 1, 2, 0);
    add_obstacle(  9,  8, 1, 1, 2, 1);
    add_obstacle(-10, 10, 1, 1, 3, 2);
    add_obstacle( 10,-10, 1, 1, 3, 2);
    add_obstacle(-22,-17, 3, 2, 3, 0);
    add_obstacle( 23, 18, 3, 2, 4, 1);
    add_obstacle(-25, 20, 2, 4, 5, 2);
    add_obstacle( 24,-22, 2, 4, 5, 2);
    add_obstacle(  0, 27, 4, 2, 3, 0);
    add_obstacle(  0,-29, 4, 2, 3, 1);
    add_obstacle(-38,  2, 2, 5, 4, 1);
    add_obstacle( 39, -2, 2, 5, 4, 0);
    add_obstacle(-12, 37, 3, 2, 2, 2);
    add_obstacle( 14,-38, 3, 2, 2, 2);
    add_obstacle(-42,-34, 2, 2, 6, 0);
    add_obstacle( 42, 34, 2, 2, 6, 1);
    /* Natural mid-field anchors: these are real collision geometry, not decorative
     * sprites, so the domain gains recognizable groves/rock clusters that also create
     * tactical bends in territory routes. */
    add_obstacle(-34,-27, 2, 2, 3, 2);
    add_obstacle( 32, 29, 2, 2, 3, 2);
    add_obstacle(-31, 34, 2, 2, 2, 1);
    add_obstacle( 35,-31, 2, 2, 2, 1);
    add_obstacle(-18, -3, 1, 2, 2, 2);
    add_obstacle( 19,  4, 1, 2, 2, 2);
    add_obstacle( -5, 20, 2, 1, 2, 1);
    add_obstacle(  7,-21, 2, 1, 2, 1);

    build_bot_navigation();

    for (i = 0u; i < ODG_MAX_ACTORS; ++i) {
        spawn_actor(i, i == ODG_PLAYER_ID ? ODG_ACTOR_PLAYER : ODG_ACTOR_BOT);
    }
    for (i = 0u; i < ODG_MAX_ACTORS; ++i) stamp_initial_territory(&g_odg.actors[i]);
    spawn_turrets();
    spawn_ammo_crates();
    spawn_chips();
    /* Third-person camera begins behind the player's actual facing. It then rotates
     * with its own slower angular limit instead of snapping to input. */
    g_odg.camera_dir_x_q15 = g_odg.actors[ODG_PLAYER_ID].face_x_q15;
    g_odg.camera_dir_z_q15 = g_odg.actors[ODG_PLAYER_ID].face_z_q15;
    g_odg.camera_yaw_turn_sign = 1;
    g_odg.camera_turn_rate_q15 = 0;
    g_odg.camera_manual_ticks = 0u;
    g_odg.camera_pitch_q15 = ODG_CAMERA_PITCH_DEFAULT_Q15;
    g_odg.camera_height_fx = odg_terrain_height_fx(g_odg.actors[ODG_PLAYER_ID].x, g_odg.actors[ODG_PLAYER_ID].z) + ODG_CAMERA_PLAYER_HEIGHT_FX;
    g_odg.camera_distance_fx = ODG_CAMERA_DISTANCE_FX;
    g_odg.control_basis_x_q15 = g_odg.camera_dir_x_q15;
    g_odg.control_basis_z_q15 = g_odg.camera_dir_z_q15;
    g_odg.control_heading_x_q15 = 0;
    g_odg.control_heading_z_q15 = 0;
    g_odg.control_strength_q15 = 0;
    g_odg.control_active = 0u;
    g_odg.camera_anchor_x = g_odg.actors[ODG_PLAYER_ID].x;
    g_odg.camera_anchor_z = g_odg.actors[ODG_PLAYER_ID].z;
}

void odg_sim_step(void) {
    uint32_t i;
    ++g_odg.tick;

    if ((g_odg.input.buttons & ODG_BUTTON_RESTART) != 0u) {
        uint64_t next_seed = g_odg.seed ^ (g_odg.tick * UINT64_C(0x9e3779b97f4a7c15));
        odg_world_build(next_seed);
        return;
    }

    if (!g_odg.match_over) {
        /* Player first keeps local control latency deterministic and minimal. */
        update_actor(&g_odg.actors[ODG_PLAYER_ID]);
        if (g_odg.actors[ODG_PLAYER_ID].hp != 0u) {
            odg_actor *player = &g_odg.actors[ODG_PLAYER_ID];
            /* The chase camera is position-locked to the player so the avatar stays
             * centered. Realistic lag is rotational only; positional lag makes steering
             * look imprecise because the controlled body drifts across the screen. */
            g_odg.camera_anchor_x = player->x;
            g_odg.camera_anchor_z = player->z;
            {
                int32_t look_x=g_odg.input.aim_x_q15;
                int32_t look_z=g_odg.input.aim_z_q15;
                int32_t look_abs=odg_abs_i32(look_x);
                if (look_abs>ODG_CAMERA_LOOK_DEADZONE) {
                    int32_t strength=(int32_t)(((int64_t)(look_abs-ODG_CAMERA_LOOK_DEADZONE)*ODG_Q15_ONE)/
                                               (ODG_Q15_ONE-ODG_CAMERA_LOOK_DEADZONE));
                    int32_t sin_step=(int32_t)(((int64_t)ODG_CAMERA_LOOK_MAX_SIN_Q15*strength)/ODG_Q15_ONE);
                    int32_t cos_step;
                    int32_t nx,nz;
                    if (sin_step<1) sin_step=1;
                    cos_step=ODG_Q15_ONE-(int32_t)(((int64_t)sin_step*sin_step)/(2*ODG_Q15_ONE));
                    rotate_dir_q15(g_odg.camera_dir_x_q15,g_odg.camera_dir_z_q15,
                                   cos_step,sin_step,look_x>0?-1:1,&nx,&nz);
                    g_odg.camera_dir_x_q15=nx;
                    g_odg.camera_dir_z_q15=nz;
                    g_odg.camera_turn_rate_q15=0;
                    g_odg.camera_yaw_turn_sign=look_x>0?-1:1;
                    g_odg.camera_manual_ticks=ODG_CAMERA_MANUAL_HOLD_TICKS;
                }
                if (odg_abs_i32(look_z)>ODG_CAMERA_LOOK_DEADZONE) {
                    int32_t delta=(int32_t)(((int64_t)look_z*ODG_CAMERA_PITCH_STEP_Q15)/ODG_Q15_ONE);
                    g_odg.camera_pitch_q15=odg_clamp_i32(g_odg.camera_pitch_q15-delta,
                                                         ODG_CAMERA_PITCH_MIN_Q15,
                                                         ODG_CAMERA_PITCH_MAX_Q15);
                    g_odg.camera_manual_ticks=ODG_CAMERA_MANUAL_HOLD_TICKS;
                }
                if(look_abs<=ODG_CAMERA_LOOK_DEADZONE &&
                   odg_abs_i32(look_z)<=ODG_CAMERA_LOOK_DEADZONE){
                    if(g_odg.camera_manual_ticks>0u)--g_odg.camera_manual_ticks;
                    /* Fortnite-style free chase view: releasing the look surface freezes
                     * the chosen yaw instead of dragging it back behind the cube. Movement
                     * then interprets joystick UP against this persistent camera heading. */
                    g_odg.camera_turn_rate_q15=approach_signed(g_odg.camera_turn_rate_q15,0,ODG_CAMERA_TURN_ACCEL_Q15);
                }
            }
            {
                int32_t desired_dist=ODG_CAMERA_DISTANCE_FX;
                int32_t probe;
                /* Camera collision is presentation-only but deterministic. Pull the chase
                 * camera inward before it enters a building/rock, then release outward
                 * slowly. This removes sudden full-screen clipping without moving the player. */
                for (probe=ODG_CAMERA_DISTANCE_FX;probe>=ODG_CAMERA_MIN_DISTANCE_FX;probe-=180) {
                    if (camera_segment_clear(player,g_odg.camera_dir_x_q15,g_odg.camera_dir_z_q15,probe)) {
                        desired_dist=probe;break;
                    }
                    desired_dist=ODG_CAMERA_MIN_DISTANCE_FX;
                }
                g_odg.camera_distance_fx=approach_signed(g_odg.camera_distance_fx,desired_dist,
                                                          desired_dist<g_odg.camera_distance_fx?150:24);
                {
                    int32_t cam_x = player->x - (int32_t)(((int64_t)g_odg.camera_dir_x_q15 * g_odg.camera_distance_fx) / ODG_Q15_ONE);
                    int32_t cam_z = player->z - (int32_t)(((int64_t)g_odg.camera_dir_z_q15 * g_odg.camera_distance_fx) / ODG_Q15_ONE);
                    int32_t desired = odg_terrain_height_fx(player->x, player->z) + ODG_CAMERA_PLAYER_HEIGHT_FX;
                    int32_t clearance = odg_terrain_height_fx(cam_x, cam_z) + ODG_CAMERA_GROUND_CLEARANCE_FX;
                    int32_t target = desired > clearance ? desired : clearance;
                    int32_t step = target > g_odg.camera_height_fx ? 18 : 6;
                    g_odg.camera_height_fx = approach_signed(g_odg.camera_height_fx, target, step);
                    if (g_odg.camera_height_fx < clearance) g_odg.camera_height_fx = clearance;
                }
            }
        }
        for (i = 1u; i < ODG_MAX_ACTORS; ++i) update_actor(&g_odg.actors[i]);
        resolve_trail_contacts();
        handle_turret_action();
        handle_drop_action();
        update_turrets();
        update_ammo_crates();
        update_chips();
        /* Territory runners are non-solid to each other. Contact is decided by
         * exposed-trail topology, not by a body solver that can fabricate cell moves. */
        check_match_end();
    }
    g_odg.prev_buttons = g_odg.input.buttons;
    update_particles();
}
