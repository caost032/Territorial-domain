#include "game_internal.h"

#include <stdint.h>
#include <stdio.h>

static int failures = 0;
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"FAIL %s:%d: %s\n",__FILE__,__LINE__,#x); ++failures; } } while (0)

static uint64_t run_script(uint64_t seed) {
    uint32_t i;
    CHECK(odg_init(seed, 320u, 180u) == ODG_STATUS_OK);
    for (i = 0u; i < 1600u && !odg_match_over(); ++i) {
        int32_t mx = (i % 360u < 180u) ? 25000 : -21000;
        int32_t mz = (i % 520u < 260u) ? 19000 : -24000;
        uint32_t buttons = (i % 400u == 0u) ? ODG_BUTTON_DASH : 0u;
        odg_set_input(mx, mz, 0, 0, buttons);
        odg_step_ticks(1u);
    }
    return odg_state_hash();
}

static int counts_are_consistent(void) {
    uint32_t counts[ODG_MAX_ACTORS] = {0u};
    uint32_t trails[ODG_MAX_ACTORS] = {0u};
    uint32_t c;
    for (c = 0u; c < ODG_CELL_COUNT; ++c) {
        uint8_t o = g_odg.territory[c];
        uint8_t t = g_odg.trail_owner[c];
        if (g_odg.playable[c] == 0u && (o != ODG_OWNER_NONE || t != ODG_OWNER_NONE)) return 0;
        if (o != ODG_OWNER_NONE) {
            uint32_t id = ODG_ID_FROM_OWNER(o);
            if (id >= ODG_MAX_ACTORS) return 0;
            ++counts[id];
        }
        if (t != ODG_OWNER_NONE) {
            uint32_t id = ODG_ID_FROM_OWNER(t);
            if (id >= ODG_MAX_ACTORS) return 0;
            ++trails[id];
        }
    }
    for (c = 0u; c < ODG_MAX_ACTORS; ++c) {
        if (counts[c] != g_odg.territory_count[c]) return 0;
        if (trails[c] != g_odg.actors[c].trail_len) return 0;
    }
    return 1;
}

static uint32_t count_rgb_difference(const uint8_t *a, const uint8_t *b, uint32_t bytes) {
    uint32_t i,n=0u;
    for(i=0u;i+3u<bytes;i+=4u) {
        int32_t dr=(int32_t)a[i]-(int32_t)b[i];
        int32_t dg=(int32_t)a[i+1u]-(int32_t)b[i+1u];
        int32_t db=(int32_t)a[i+2u]-(int32_t)b[i+2u];
        uint32_t delta=(uint32_t)(dr<0?-dr:dr)+(uint32_t)(dg<0?-dg:dg)+
                       (uint32_t)(db<0?-db:db);
        if(delta>24u) ++n;
    }
    return n;
}

static uint32_t find_adjacent_playable(uint32_t center) {
    uint32_t x = center & (ODG_GRID_SIZE - 1u);
    uint32_t z = center >> ODG_GRID_SHIFT;
    if (x + 1u < ODG_GRID_SIZE && g_odg.playable[center + 1u]) return center + 1u;
    if (x > 0u && g_odg.playable[center - 1u]) return center - 1u;
    if (z + 1u < ODG_GRID_SIZE && g_odg.playable[center + ODG_GRID_SIZE]) return center + ODG_GRID_SIZE;
    if (z > 0u && g_odg.playable[center - ODG_GRID_SIZE]) return center - ODG_GRID_SIZE;
    return UINT32_MAX;
}

static uint32_t find_playable_at_distance(uint32_t center, uint32_t min_cells, uint32_t max_cells) {
    int32_t cx=(int32_t)(center & (ODG_GRID_SIZE-1u));
    int32_t cz=(int32_t)(center >> ODG_GRID_SHIFT);
    uint32_t d;
    for(d=min_cells;d<=max_cells;++d){
        int32_t off=(int32_t)d;
        const int32_t dirs[8][2]={{1,0},{-1,0},{0,1},{0,-1},{1,1},{1,-1},{-1,1},{-1,-1}};
        uint32_t i;
        for(i=0u;i<8u;++i){
            int32_t x=cx+dirs[i][0]*off,z=cz+dirs[i][1]*off;
            if(x>=0&&z>=0&&x<(int32_t)ODG_GRID_SIZE&&z<(int32_t)ODG_GRID_SIZE){
                uint32_t c=(uint32_t)z*ODG_GRID_SIZE+(uint32_t)x;
                if(g_odg.playable[c]) return c;
            }
        }
    }
    return UINT32_MAX;
}

static void set_test_territory_owner(uint32_t cell,uint8_t owner) {
    uint8_t old;
    if (cell>=ODG_CELL_COUNT || g_odg.playable[cell]==0u) return;
    old=g_odg.territory[cell];
    if (old==owner) return;
    if (old!=ODG_OWNER_NONE) {
        uint32_t old_id=ODG_ID_FROM_OWNER(old);
        if (old_id<ODG_MAX_ACTORS && g_odg.territory_count[old_id]>0u) --g_odg.territory_count[old_id];
    }
    g_odg.territory[cell]=owner;
    if (owner!=ODG_OWNER_NONE) {
        uint32_t new_id=ODG_ID_FROM_OWNER(owner);
        if (new_id<ODG_MAX_ACTORS) ++g_odg.territory_count[new_id];
    }
}

int main(void) {
    uint64_t a, b, c;
    uintptr_t fb;
    uint32_t bytes;
    uint32_t i;
    uint32_t nonzero = 0u;
    int32_t spawn_x, spawn_z;

    CHECK(odg_api_version() == ODG_API_VERSION);
    CHECK(ODG_API_VERSION == 14u);
    CHECK(odg_init(1u, 0u, 180u) == ODG_STATUS_INVALID_ARGUMENT);
    CHECK(odg_init(1u, ODG_MAX_RENDER_WIDTH + 1u, 180u) == ODG_STATUS_INVALID_ARGUMENT);

    CHECK(odg_init(UINT64_C(0x123456789abcdef0), 480u, 270u) == ODG_STATUS_OK);
    CHECK(g_odg.playable_count > 8000u && g_odg.playable_count < ODG_CELL_COUNT);
    CHECK(g_odg.playable[0] == 0u && g_odg.playable[ODG_CELL_COUNT - 1u] == 0u);
    CHECK(odg_territory_total_cells() == g_odg.playable_count);
    CHECK(odg_alive_count() == ODG_MAX_ACTORS);
    CHECK(odg_turret_count() == ODG_MAX_TURRETS);
    CHECK(odg_ammo_crate_count() == ODG_INITIAL_AMMO_CRATES);
    CHECK(counts_are_consistent());
    for (i=0u;i<ODG_MAX_TURRETS;++i) {
        uint32_t tc=odg_cell_from_world(g_odg.turrets[i].x,g_odg.turrets[i].z);
        CHECK(g_odg.turrets[i].owner == ODG_TURRET_NEUTRAL);
        CHECK(g_odg.turrets[i].ammo == g_odg.turrets[i].max_ammo);
        CHECK(g_odg.playable[tc] != 0u);
    }

    /* Terrain is authoritative/deterministic rather than a visual-only plane. The chase
     * camera must remain above the terrain under its own position while traversing hills. */
    {
        int32_t h_hill=odg_terrain_height_fx(-31*ODG_FX_ONE,-9*ODG_FX_ONE);
        int32_t h_valley=odg_terrain_height_fx(4*ODG_FX_ONE,4*ODG_FX_ONE);
        CHECK(h_hill>h_valley+700);
    }
    CHECK(odg_init(UINT64_C(0x5445525241494e33), 320u, 180u) == ODG_STATUS_OK);
    odg_set_input(18000,28000,0,0,0u);
    for(i=0u;i<420u && g_odg.actors[0].hp!=0u;++i){
        odg_actor *p=&g_odg.actors[0];
        int32_t cam_x,cam_z,clearance;
        odg_step_ticks(1u);
        CHECK(g_odg.camera_anchor_x==p->x && g_odg.camera_anchor_z==p->z);
        CHECK(g_odg.camera_distance_fx>=ODG_CAMERA_MIN_DISTANCE_FX && g_odg.camera_distance_fx<=ODG_CAMERA_DISTANCE_FX);
        cam_x=p->x-(int32_t)(((int64_t)g_odg.camera_dir_x_q15*g_odg.camera_distance_fx)/ODG_Q15_ONE);
        cam_z=p->z-(int32_t)(((int64_t)g_odg.camera_dir_z_q15*g_odg.camera_distance_fx)/ODG_Q15_ONE);
        clearance=odg_terrain_height_fx(cam_x,cam_z)+ODG_CAMERA_GROUND_CLEARANCE_FX;
        CHECK(g_odg.camera_height_fx>=clearance);
    }

    /* Spawn positions are seeded, not hard-coded. */
    spawn_x=g_odg.actors[0].x; spawn_z=g_odg.actors[0].z;
    CHECK(odg_init(UINT64_C(0x123456789abcdef1), 480u, 270u) == ODG_STATUS_OK);
    CHECK(g_odg.actors[0].x != spawn_x || g_odg.actors[0].z != spawn_z);

    /* v10 contact steering: a diagonal command into an obstacle corner must retain
     * forward/tangential progress instead of degenerating into repeated stop/X/Z ticks. */
    CHECK(odg_init(UINT64_C(0x434f4e5441435431), 320u, 180u) == ODG_STATUS_OK);
    {
        odg_actor *p=&g_odg.actors[0];
        int32_t sx,sz;
        p->x=-10600;p->z=-9600;p->vx=0;p->vz=0;p->speed_fx=0;
        p->face_x_q15=23170;p->face_z_q15=23170;
        p->last_cell=odg_cell_from_world(p->x,p->z);
        g_odg.camera_dir_x_q15=0;g_odg.camera_dir_z_q15=ODG_Q15_ONE;g_odg.control_active=0u;
        sx=p->x;sz=p->z;
        odg_set_input(23170,23170,0,0,0u);
        for(i=0u;i<90u && p->hp!=0u;++i) odg_step_ticks(1u);
        CHECK(odg_dist2(sx,sz,p->x,p->z)>(int64_t)ODG_FX_ONE*ODG_FX_ONE);
        CHECK(p->slide_lock_ticks<=ODG_SLIDE_LOCK_TICKS);
    }

    /* Third-person camera retracts before entering world geometry, but never teleports
     * the controlled actor or drops below its configured minimum chase distance. */
    CHECK(odg_init(UINT64_C(0x43414d434f4c4c31), 320u, 180u) == ODG_STATUS_OK);
    {
        odg_actor *p=&g_odg.actors[0];
        p->x=-9*ODG_FX_ONE;p->z=-5*ODG_FX_ONE;p->vx=0;p->vz=0;p->speed_fx=0;
        p->face_x_q15=0;p->face_z_q15=ODG_Q15_ONE;p->last_cell=odg_cell_from_world(p->x,p->z);
        g_odg.camera_dir_x_q15=0;g_odg.camera_dir_z_q15=ODG_Q15_ONE;
        g_odg.camera_distance_fx=ODG_CAMERA_DISTANCE_FX;g_odg.control_active=0u;
        odg_set_input(0,0,0,0,0u);
        for(i=0u;i<16u;++i) odg_step_ticks(1u);
        CHECK(g_odg.camera_distance_fx<ODG_CAMERA_DISTANCE_FX);
        CHECK(g_odg.camera_distance_fx>=ODG_CAMERA_MIN_DISTANCE_FX);
        p->x=0;p->z=0;p->last_cell=odg_cell_from_world(0,0);
        for(i=0u;i<120u;++i) odg_step_ticks(1u);
        CHECK(g_odg.camera_distance_fx>ODG_CAMERA_MIN_DISTANCE_FX);
    }

    /* v12 free-look contract: camera yaw is independent from locomotion. It rotates
     * while the player is stationary and, after release, HOLDS the chosen yaw instead of
     * recentering behind the cube. */
    CHECK(odg_init(UINT64_C(0x4c4f4f4b56313231), 320u, 180u) == ODG_STATUS_OK);
    {
        odg_actor *p=&g_odg.actors[0];
        int32_t body_x,body_z,cam_after_x,cam_after_z;
        p->x=0;p->z=0;p->vx=0;p->vz=0;p->speed_fx=0;
        p->face_x_q15=0;p->face_z_q15=ODG_Q15_ONE;
        g_odg.camera_dir_x_q15=0;g_odg.camera_dir_z_q15=ODG_Q15_ONE;
        g_odg.camera_pitch_q15=ODG_CAMERA_PITCH_DEFAULT_Q15;
        g_odg.camera_manual_ticks=0u;g_odg.camera_turn_rate_q15=0;
        body_x=p->face_x_q15;body_z=p->face_z_q15;
        odg_set_input(0,0,ODG_Q15_ONE,0,0u);
        for(i=0u;i<24u;++i) odg_step_ticks(1u);
        CHECK(g_odg.camera_dir_x_q15>12000);
        CHECK(p->face_x_q15==body_x && p->face_z_q15==body_z);
        CHECK(p->x==0 && p->z==0);
        cam_after_x=g_odg.camera_dir_x_q15;cam_after_z=g_odg.camera_dir_z_q15;
        odg_set_input(0,0,0,0,0u);
        for(i=0u;i<ODG_CAMERA_MANUAL_HOLD_TICKS+140u;++i) odg_step_ticks(1u);
        CHECK(((int64_t)g_odg.camera_dir_x_q15*cam_after_x+
               (int64_t)g_odg.camera_dir_z_q15*cam_after_z)/ODG_Q15_ONE>32000);
        CHECK(p->face_x_q15==body_x && p->face_z_q15==body_z);
    }

    /* Theme choice is presentation-only and must not perturb the deterministic state. */
    CHECK(odg_init(UINT64_C(0x5448454d45563131), 320u, 180u) == ODG_STATUS_OK);
    {
        uint64_t h0=odg_state_hash();
        uintptr_t r0=odg_render_frame();
        const uint8_t *p0=(const uint8_t*)r0;
        uint32_t sky0=((uint32_t)p0[0]<<16)|((uint32_t)p0[1]<<8)|p0[2];
        odg_set_visual_theme(ODG_VISUAL_THEME_SOLAR_EMBER);
        CHECK(odg_visual_theme()==ODG_VISUAL_THEME_SOLAR_EMBER);
        CHECK(odg_state_hash()==h0);
        {
            uintptr_t r1=odg_render_frame();
            const uint8_t *p1=(const uint8_t*)r1;
            uint32_t sky1=((uint32_t)p1[0]<<16)|((uint32_t)p1[1]<<8)|p1[2];
            CHECK(sky0!=sky1); /* theme is a real C-renderer change, not host CSS */
        }
        odg_set_visual_theme(ODG_VISUAL_THEME_OBSIDIAN_PULSE);
        CHECK(odg_state_hash()==h0);
        odg_set_presentation_mode(ODG_PRESENTATION_SHOWCASE);
        CHECK(odg_presentation_mode()==ODG_PRESENTATION_SHOWCASE);
        CHECK(odg_state_hash()==h0);
        (void)odg_render_frame();
        CHECK(odg_state_hash()==h0);
        odg_set_presentation_mode(ODG_PRESENTATION_GAMEPLAY);
        CHECK(odg_presentation_mode()==ODG_PRESENTATION_GAMEPLAY);
    }

    /* v12 precision locomotion contract: normal direction changes own the ground path on
     * the same tick. The cube body rotates inertially afterward, but old forward velocity
     * must not make a RIGHT/diagonal command continue straight. */
    CHECK(odg_init(UINT64_C(0x5150415449414c), 320u, 180u) == ODG_STATUS_OK);
    {
        odg_actor *p=&g_odg.actors[0];
        int32_t initial_fx,initial_fz,initial_camx,initial_camz,turn_start_z;
        p->x=0;p->z=0;p->vx=0;p->vz=0;p->speed_fx=0;
        p->face_x_q15=0;p->face_z_q15=ODG_Q15_ONE;
        g_odg.camera_dir_x_q15=0;g_odg.camera_dir_z_q15=ODG_Q15_ONE;g_odg.control_active=0u;
        odg_set_input(0,30000,0,0,0u);
        for(i=0u;i<40u;++i) odg_step_ticks(1u);
        CHECK(p->vz>30 && odg_abs_i32(p->vx)<8);
        initial_fx=p->face_x_q15;initial_fz=p->face_z_q15;
        initial_camx=g_odg.camera_dir_x_q15;initial_camz=g_odg.camera_dir_z_q15;
        turn_start_z=p->z;
        odg_set_input(30000,0,0,0,0u);
        odg_step_ticks(1u);
        CHECK(p->face_x_q15>0); /* body rotation starts, but does not gate translation */
        CHECK(p->vx>30 && odg_abs_i32(p->vz)<8); /* RIGHT is physically RIGHT immediately */
        CHECK((int64_t)p->face_x_q15*initial_fx+(int64_t)p->face_z_q15*initial_fz>0); /* body does not snap */
        CHECK((int64_t)g_odg.camera_dir_x_q15*initial_camx+(int64_t)g_odg.camera_dir_z_q15*initial_camz>0);
        for(i=0u;i<12u;++i) odg_step_ticks(1u);
        CHECK(p->vx>30);
        CHECK(odg_abs_i32(p->z-turn_start_z)<40); /* no hidden forward drift while steering right */
        for(i=0u;i<48u;++i) odg_step_ticks(1u);
        CHECK(p->vx>35);
        CHECK(p->face_x_q15>18000);
        CHECK(p->hp==1u);
    }

    /* A true 180-degree reversal is the exception to instant heading authority: speed
     * brakes along the old trajectory before changing sign, while body/camera rotate. */
    CHECK(odg_init(UINT64_C(0x5245564552534538), 320u, 180u) == ODG_STATUS_OK);
    {
        odg_actor *p=&g_odg.actors[0];
        int32_t before_speed,old_face_z;
        p->x=0;p->z=0;p->vx=0;p->vz=0;p->speed_fx=0;
        p->face_x_q15=0;p->face_z_q15=ODG_Q15_ONE;
        g_odg.camera_dir_x_q15=0;g_odg.camera_dir_z_q15=ODG_Q15_ONE;g_odg.control_active=0u;
        odg_set_input(0,30000,0,0,0u);for(i=0u;i<50u;++i)odg_step_ticks(1u);
        before_speed=p->speed_fx;old_face_z=p->face_z_q15;
        odg_set_input(0,-30000,0,0,0u);odg_step_ticks(1u);
        CHECK(p->vz>0); /* still braking forward, not teleporting into reverse */
        CHECK(p->speed_fx<before_speed);
        CHECK(p->face_z_q15>0 && old_face_z>0); /* orientation has begun a physical turn, no snap */
        for(i=0u;i<80u;++i)odg_step_ticks(1u);
        CHECK(p->vz<0);
        CHECK(p->face_z_q15<0);
    }

    /* Diagonal intent must create a curved ground trajectory, not a stale-axis march.
     * A held gesture owns a persistent world heading; rotating the finger rotates that
     * heading directly instead of allowing camera follow to redefine it. */
    CHECK(odg_init(UINT64_C(0x444941474f4e414c), 320u, 180u) == ODG_STATUS_OK);
    {
        odg_actor *p=&g_odg.actors[0];
        int32_t x0,z0;
        p->x=0;p->z=0;p->vx=0;p->vz=0;p->speed_fx=0;
        p->face_x_q15=0;p->face_z_q15=ODG_Q15_ONE;
        g_odg.camera_dir_x_q15=0;g_odg.camera_dir_z_q15=ODG_Q15_ONE;g_odg.control_active=0u;
        x0=p->x;z0=p->z;
        odg_set_input(23000,-23000,0,0,0u);
        for(i=0u;i<180u;++i) odg_step_ticks(1u);
        CHECK(p->x > x0 + 4*ODG_FX_ONE);
        CHECK(p->z < z0 - 2*ODG_FX_ONE);
        CHECK(p->steer_q15!=0 || p->face_x_q15>10000);
        CHECK(p->face_x_q15>9000 && p->face_z_q15<0);
        {
            int32_t old_hx=g_odg.control_heading_x_q15,old_hz=g_odg.control_heading_z_q15;
            int32_t old_bx=g_odg.control_basis_x_q15,old_bz=g_odg.control_basis_z_q15;
            odg_set_input(0,30000,0,0,0u);
            odg_step_ticks(1u);
            CHECK((int64_t)g_odg.control_heading_x_q15*old_hx+
                  (int64_t)g_odg.control_heading_z_q15*old_hz < 0);
            CHECK((int64_t)g_odg.control_basis_x_q15*old_bx+
                  (int64_t)g_odg.control_basis_z_q15*old_bz > INT64_C(1000000000));
        }
    }

    /* A rear diagonal is a genuine turn, not an instantaneous sideways teleport. The
     * lateral component begins on tick one, crosses through the old forward component,
     * then converges to the requested 45-degree heading without stop-go braking. */
    CHECK(odg_init(UINT64_C(0x4152434e4f534c49), 320u, 180u) == ODG_STATUS_OK);
    {
        odg_actor *p=&g_odg.actors[0];
        uint32_t moving_ticks=0u;
        p->x=0;p->z=0;p->vx=0;p->vz=0;p->speed_fx=0;
        p->face_x_q15=0;p->face_z_q15=ODG_Q15_ONE;
        g_odg.camera_dir_x_q15=0;g_odg.camera_dir_z_q15=ODG_Q15_ONE;g_odg.control_active=0u;
        odg_set_input(22000,-22000,0,0,0u);
        for(i=0u;i<100u;++i){
            odg_step_ticks(1u);
            if(i<8u){ CHECK(p->vx>=0); CHECK(p->face_x_q15>0); }
            if(i==24u){ CHECK(p->vx>0); CHECK(p->vz<0); }
            if(p->speed_fx>0) ++moving_ticks;
        }
        CHECK(moving_ticks>96u);
        CHECK(p->x>3*ODG_FX_ONE);
        CHECK(p->z<-2*ODG_FX_ONE);
        CHECK(odg_abs_i32(p->vx + p->vz)<18); /* ~45 degree target */
    }

    /* Analog magnitude is authoritative. A small stick displacement must remain slower
     * than a full displacement instead of being normalized to 100 percent. */
    CHECK(odg_init(UINT64_C(0x414e414c4f473037), 320u, 180u) == ODG_STATUS_OK);
    {
        odg_actor *p=&g_odg.actors[0];
        int32_t weak_speed,strong_speed;
        p->x=0;p->z=0;p->vx=0;p->vz=0;p->speed_fx=0;p->face_x_q15=0;p->face_z_q15=ODG_Q15_ONE;
        g_odg.camera_dir_x_q15=0;g_odg.camera_dir_z_q15=ODG_Q15_ONE;g_odg.control_active=0u;
        odg_set_input(0,12000,0,0,0u);for(i=0u;i<80u;++i)odg_step_ticks(1u);weak_speed=p->speed_fx;
        p->vx=0;p->vz=0;p->speed_fx=0;g_odg.control_active=0u;
        odg_set_input(0,30000,0,0,0u);for(i=0u;i<80u;++i)odg_step_ticks(1u);strong_speed=p->speed_fx;
        CHECK(weak_speed>0);
        CHECK(strong_speed>weak_speed*2);
    }

    /* v12 camera-relative movement. Joystick RIGHT must not rotate the camera by itself.
     * After explicit free-look rotates the camera, joystick UP follows that new view. */
    CHECK(odg_init(UINT64_C(0x43414d5631324c4f), 320u, 180u) == ODG_STATUS_OK);
    {
        odg_actor *p=&g_odg.actors[0];
        int32_t cam0x=0,cam0z=ODG_Q15_ONE,turned_x,turned_z;
        p->x=0;p->z=0;p->vx=0;p->vz=0;p->speed_fx=0;
        p->face_x_q15=0;p->face_z_q15=ODG_Q15_ONE;
        g_odg.camera_dir_x_q15=cam0x;g_odg.camera_dir_z_q15=cam0z;
        g_odg.camera_turn_rate_q15=0;g_odg.control_active=0u;
        odg_set_input(30000,0,0,0,0u);
        for(i=0u;i<70u;++i) odg_step_ticks(1u);
        CHECK(g_odg.camera_dir_x_q15==cam0x && g_odg.camera_dir_z_q15==cam0z);
        CHECK(p->face_x_q15>25000);
        CHECK(odg_control_local_x_q15()>28000);
        odg_set_input(0,0,32767,0,0u);
        for(i=0u;i<24u;++i) odg_step_ticks(1u);
        turned_x=g_odg.camera_dir_x_q15;turned_z=g_odg.camera_dir_z_q15;
        CHECK(turned_x>12000);
        odg_set_input(0,30000,0,0,0u);
        odg_step_ticks(1u);
        CHECK(((int64_t)odg_control_heading_x_q15()*turned_x+
               (int64_t)odg_control_heading_z_q15()*turned_z)/ODG_Q15_ONE>30000);
        {
            int32_t vx=0,vz=0;
            odg_normalize_q15(p->vx,p->vz,&vx,&vz);
            CHECK(((int64_t)vx*turned_x+(int64_t)vz*turned_z)/ODG_Q15_ONE>30000);
        }
    }

    /* Fixed joystick in v12 is camera-local, not a rebasing world-heading dial. The
     * knob can stay physically UP/RIGHT while camera movement rotates the world request. */
    CHECK(odg_init(UINT64_C(0x4a4f595631324341), 320u, 180u) == ODG_STATUS_OK);
    {
        odg_actor *p=&g_odg.actors[0];
        int32_t h0x,h0z,cam_after_x,cam_after_z;
        p->x=0;p->z=0;p->vx=0;p->vz=0;p->speed_fx=0;
        p->face_x_q15=0;p->face_z_q15=ODG_Q15_ONE;
        g_odg.camera_dir_x_q15=0;g_odg.camera_dir_z_q15=ODG_Q15_ONE;
        g_odg.camera_turn_rate_q15=0;g_odg.control_active=0u;
        odg_set_input(30000,0,0,0,0u);
        odg_step_ticks(1u);
        h0x=odg_control_heading_x_q15();h0z=odg_control_heading_z_q15();
        CHECK(odg_control_local_x_q15()>28000);
        for(i=1u;i<60u;++i)odg_step_ticks(1u);
        CHECK(odg_control_local_x_q15()>28000); /* camera did not move */
        odg_set_input(30000,0,32767,0,0u);
        for(i=0u;i<18u;++i)odg_step_ticks(1u);
        cam_after_x=g_odg.camera_dir_x_q15;cam_after_z=g_odg.camera_dir_z_q15;
        CHECK((int64_t)odg_control_heading_x_q15()*h0x+
              (int64_t)odg_control_heading_z_q15()*h0z<INT64_C(1000000000));
        CHECK(odg_control_local_x_q15()>26000);
        odg_set_input(0,30000,0,0,0u);
        odg_step_ticks(1u);
        CHECK(((int64_t)odg_control_heading_x_q15()*cam_after_x+
               (int64_t)odg_control_heading_z_q15()*cam_after_z)/ODG_Q15_ONE>30000);
        odg_set_input(0,0,0,0,0u);
        for(i=0u;i<ODG_CAMERA_MANUAL_HOLD_TICKS+170u;++i)odg_step_ticks(1u);
        CHECK(((int64_t)g_odg.camera_dir_x_q15*cam_after_x+
               (int64_t)g_odg.camera_dir_z_q15*cam_after_z)/ODG_Q15_ONE>32000);
    }

    /* Exact world-heading path remains available for native/replay hosts. Unlike local
     * joystick input, a supplied world vector is intentionally independent of camera yaw. */
    CHECK(odg_init(UINT64_C(0x574f524c44563132), 320u, 180u) == ODG_STATUS_OK);
    {
        odg_actor *p=&g_odg.actors[0];
        int32_t first_hx,first_hz,before_x,before_z;
        p->x=0;p->z=0;p->vx=0;p->vz=0;p->speed_fx=0;
        p->face_x_q15=0;p->face_z_q15=ODG_Q15_ONE;
        g_odg.camera_dir_x_q15=0;g_odg.camera_dir_z_q15=ODG_Q15_ONE;
        g_odg.camera_turn_rate_q15=0;g_odg.control_active=0u;
        odg_set_world_input(ODG_Q15_ONE,0,30000,0,0,0u);
        odg_step_ticks(3u);
        CHECK(p->face_x_q15>0 && p->vx>0);
        first_hx=odg_control_heading_x_q15();first_hz=odg_control_heading_z_q15();
        /* Rotate only camera; exact world input must stay exactly world-right. */
        odg_set_world_input(ODG_Q15_ONE,0,30000,32767,0,0u);
        for(i=0u;i<18u;++i)odg_step_ticks(1u);
        CHECK((int64_t)odg_control_heading_x_q15()*first_hx+
              (int64_t)odg_control_heading_z_q15()*first_hz>INT64_C(1000000000));
        before_x=p->x;before_z=p->z;
        odg_set_world_input(0,-ODG_Q15_ONE,30000,0,0,0u);
        odg_step_ticks(2u);
        CHECK((int64_t)odg_control_heading_x_q15()*first_hx+
              (int64_t)odg_control_heading_z_q15()*first_hz<INT64_C(300000000));
        CHECK(p->x!=before_x || p->z!=before_z);
        CHECK(odg_abs_i32(p->steer_q15)>1000);
        odg_set_world_input(0,0,0,0,0,0u);odg_step_ticks(1u);
        CHECK(odg_control_strength_q15()==0);
    }

    /* Bot return steering must not replan X/Z repeatedly while the actor is still in
     * the same cell. This specifically protects against the visible left-right chatter
     * reported in the previous controller. */
    CHECK(odg_init(UINT64_C(0x424f544e4f434841), 320u, 180u) == ODG_STATUS_OK);
    {
        odg_actor *bot=&g_odg.actors[1];
        uint32_t same_cell_changes=0u,prev_cell,prev_mode;
        int32_t prev_x,prev_z;
        g_odg.obstacle_count=0u;
        bot->bot_mode=ODG_BOT_RETURN;
        bot->trail_active=1u;
        bot->trail_len=8u;
        bot->ai_plan_cell=UINT32_MAX;
        bot->ai_commit_ticks=0u;
        prev_cell=odg_cell_from_world(bot->x,bot->z);
        prev_x=bot->ai_x_q15;prev_z=bot->ai_z_q15;prev_mode=bot->bot_mode;
        for(i=0u;i<240u && bot->hp!=0u;++i){
            uint32_t cell;
            odg_step_ticks(1u);
            cell=odg_cell_from_world(bot->x,bot->z);
            if(cell==prev_cell && prev_mode==ODG_BOT_RETURN && bot->bot_mode==ODG_BOT_RETURN &&
               (bot->ai_x_q15!=prev_x || bot->ai_z_q15!=prev_z)) ++same_cell_changes;
            prev_cell=cell;prev_x=bot->ai_x_q15;prev_z=bot->ai_z_q15;prev_mode=bot->bot_mode;
        }
        CHECK(same_cell_changes<=2u);
    }

    /* Self trail remains explicitly non-lethal. */
    CHECK(odg_init(UINT64_C(0x77771111), 320u, 180u) == ODG_STATUS_OK);
    {
        odg_actor *p=&g_odg.actors[0];
        uint32_t cur=p->last_cell;
        uint32_t target=find_adjacent_playable(cur);
        CHECK(target != UINT32_MAX);
        if (target != UINT32_MAX) {
            int32_t dx=odg_cell_center_x(target)-odg_cell_center_x(cur);
            int32_t dz=odg_cell_center_z(target)-odg_cell_center_z(cur);
            uint8_t own=ODG_OWNER_FROM_ID(0u);
            p->x=odg_cell_center_x(cur); p->z=odg_cell_center_z(cur);
            p->vx=0; p->vz=0; p->trail_active=1u; p->trail_len=1u;
            g_odg.trail_owner[target]=own;
            g_odg.territory[target]=ODG_OWNER_NONE;
            odg_normalize_q15(dx,dz,&p->face_x_q15,&p->face_z_q15);
            g_odg.camera_dir_x_q15=p->face_x_q15; g_odg.camera_dir_z_q15=p->face_z_q15;
            g_odg.control_active=0u;
            odg_set_input(0,32767,0,0,0u);
            for(i=0u;i<30u;++i) odg_step_ticks(1u);
            CHECK(p->hp == 1u);
            CHECK(p->death_reason != ODG_DEATH_SELF_CROSS);
        }
    }

    /* Trail-vs-ground priority: ground ownership never makes an exposed enemy trail
     * invulnerable. A bot can cut the player's trail on bot-owned ground and the player
     * can cut a bot trail while standing inside player-owned ground. */
    CHECK(odg_init(UINT64_C(0x545241494c505249), 320u, 180u) == ODG_STATUS_OK);
    {
        odg_actor *p=&g_odg.actors[0],*b1=&g_odg.actors[1];
        uint32_t pc=p->last_cell,bc=b1->last_cell;
        uint32_t pt=find_adjacent_playable(pc),bt=find_adjacent_playable(bc);
        CHECK(pt!=UINT32_MAX && bt!=UINT32_MAX);
        if(pt!=UINT32_MAX && bt!=UINT32_MAX){
            uint8_t po=ODG_OWNER_FROM_ID(0u),bo=ODG_OWNER_FROM_ID(1u);
            /* Bot-owned ground carrying a player trail: bot contact must eliminate player. */
            g_odg.territory[bt]=bo;g_odg.trail_owner[bt]=po;p->trail_active=1u;p->trail_len=1u;
            b1->last_cell=bc;b1->x=odg_cell_center_x(bt);b1->z=odg_cell_center_z(bt);
            /* Force the next simulation step to process the transition by restoring old last cell. */
            b1->last_cell=bc;odg_step_ticks(1u);
            CHECK(p->hp==0u);CHECK(p->death_reason==ODG_DEATH_TRAIL_CUT);
        }
    }
    CHECK(odg_init(UINT64_C(0x545241494c505232), 320u, 180u) == ODG_STATUS_OK);
    {
        odg_actor *p=&g_odg.actors[0],*b1=&g_odg.actors[1];
        uint32_t pc=p->last_cell,pt=find_adjacent_playable(pc);
        CHECK(pt!=UINT32_MAX);
        if(pt!=UINT32_MAX){
            uint8_t po=ODG_OWNER_FROM_ID(0u),bo=ODG_OWNER_FROM_ID(1u);
            g_odg.territory[pt]=po;g_odg.trail_owner[pt]=bo;b1->trail_active=1u;b1->trail_len=1u;
            p->x=odg_cell_center_x(pt);p->z=odg_cell_center_z(pt);p->last_cell=pc;odg_step_ticks(1u);
            CHECK(b1->hp==0u);CHECK(b1->death_reason==ODG_DEATH_TRAIL_CUT);
        }
    }

    /* Same-cell contact regression: a cube already occupying a cell must still cut
     * a newly exposed enemy trail drawn underneath it. This guards the contact resolver
     * in addition to normal cell-transition checks. */
    CHECK(odg_init(UINT64_C(0x53414d4543454c4c), 320u, 180u) == ODG_STATUS_OK);
    {
        odg_actor *p=&g_odg.actors[0],*b1=&g_odg.actors[1];
        uint32_t cell=b1->last_cell;
        CHECK(cell<ODG_CELL_COUNT && g_odg.playable[cell]!=0u);
        p->trail_active=1u;p->trail_len=ODG_TURRET_TRAIL_MIN_CELLS;
        g_odg.trail_owner[cell]=ODG_OWNER_FROM_ID(0u);
        b1->x=odg_cell_center_x(cell);b1->z=odg_cell_center_z(cell);b1->last_cell=cell;
        odg_set_input(0,0,0,0,0u);odg_step_ticks(1u);
        CHECK(p->hp==0u);CHECK(p->death_reason==ODG_DEATH_TRAIL_CUT);
    }

    /* Disconnected territory is intentionally persistent. Losing a bridge cell does not
     * garbage-collect the owner's islands: the original owner may later reconnect them. */
    CHECK(odg_init(UINT64_C(0x49534c414e445631), 320u, 180u) == ODG_STATUS_OK);
    {
        uint32_t scan_cell=0u,left=UINT32_MAX,mid=UINT32_MAX,right=UINT32_MAX,j;
        uint8_t po=ODG_OWNER_FROM_ID(0u),bo=ODG_OWNER_FROM_ID(1u);
        for(scan_cell=1u;scan_cell+1u<ODG_CELL_COUNT;++scan_cell){
            if((scan_cell&(ODG_GRID_SIZE-1u))==0u || (scan_cell&(ODG_GRID_SIZE-1u))==ODG_GRID_SIZE-1u) continue;
            if(g_odg.playable[scan_cell-1u] && g_odg.playable[scan_cell] && g_odg.playable[scan_cell+1u]){left=scan_cell-1u;mid=scan_cell;right=scan_cell+1u;break;}
        }
        CHECK(left!=UINT32_MAX && mid!=UINT32_MAX && right!=UINT32_MAX);
        if(left!=UINT32_MAX){
            for(j=0u;j<ODG_MAX_ACTORS;++j) g_odg.territory_count[j]=0u;
            for(j=0u;j<ODG_CELL_COUNT;++j) if(g_odg.territory[j]!=ODG_OWNER_NONE) g_odg.territory[j]=ODG_OWNER_NONE;
            g_odg.territory[left]=po;g_odg.territory[mid]=bo;g_odg.territory[right]=po;
            g_odg.territory_count[0]=2u;g_odg.territory_count[1]=1u;
            for(j=1u;j<ODG_MAX_ACTORS;++j) g_odg.actors[j].active=0u;
            odg_set_input(0,0,0,0,0u);odg_step_ticks(4u);
            CHECK(g_odg.territory[left]==po);CHECK(g_odg.territory[right]==po);
            CHECK(g_odg.territory[mid]==bo);CHECK(g_odg.territory_count[0]==2u);
        }
    }

    /* v14 neutral infrastructure belongs to the first living actor that controls a strict
     * majority of its local playable 5x5 neighborhood. A minority is insufficient and,
     * once commissioned, later ground painting cannot silently flip the programming. */
    CHECK(odg_init(UINT64_C(0x434c41494d), 320u, 180u) == ODG_STATUS_OK);
    {
        odg_turret *t=&g_odg.turrets[0];
        uint32_t tc=odg_cell_from_world(t->x,t->z);
        uint32_t local[25],local_count=0u,j;
        int32_t cx=(int32_t)(tc & (ODG_GRID_SIZE-1u)),cz=(int32_t)(tc >> ODG_GRID_SHIFT),dz;
        uint8_t own=ODG_OWNER_FROM_ID(0u),other=ODG_OWNER_FROM_ID(1u);
        t->owner=ODG_TURRET_NEUTRAL;t->carried_by=ODG_TURRET_NONE;
        for(dz=-2;dz<=2;++dz){int32_t dx;for(dx=-2;dx<=2;++dx){int32_t x=cx+dx,z=cz+dz;if(x>=0&&z>=0&&x<(int32_t)ODG_GRID_SIZE&&z<(int32_t)ODG_GRID_SIZE){uint32_t cc=(uint32_t)z*ODG_GRID_SIZE+(uint32_t)x;if(g_odg.playable[cc]){local[local_count++]=cc;set_test_territory_owner(cc,ODG_OWNER_NONE);}}}}
        CHECK(local_count>0u);
        for(j=0u;j<local_count/2u;++j)set_test_territory_owner(local[j],own);
        odg_update_turret_ownership_internal();CHECK(t->owner==ODG_TURRET_NEUTRAL);
        set_test_territory_owner(local[local_count/2u],own);
        odg_update_turret_ownership_internal();CHECK(t->owner==own);
        for(j=0u;j<local_count;++j)set_test_territory_owner(local[j],other);
        odg_update_turret_ownership_internal();CHECK(t->owner==own);
        CHECK(counts_are_consistent());
    }

    /* A neutral turret cannot consume or advertise a reprogram chip. ACTION leaves the
     * carried chip intact; territorial majority is the only neutral commissioning path. */
    CHECK(odg_init(UINT64_C(0x4e45555452414c43), 320u, 180u) == ODG_STATUS_OK);
    {
        odg_actor *p=&g_odg.actors[0];odg_turret *t=&g_odg.turrets[0];odg_chip *chip=&g_odg.chips[0];
        t->owner=ODG_TURRET_NEUTRAL;t->carried_by=ODG_TURRET_NONE;p->x=t->x;p->z=t->z;
        chip->active=1u;chip->kind=ODG_CHIP_KIND_REPROGRAM;chip->carried_by=0u;chip->pickup_cd=0u;p->carried_chip=0u;
        CHECK(odg_player_hack_action_available()==0u);
        odg_set_input(0,0,0,0,ODG_BUTTON_ACTION);odg_step_ticks(1u);
        CHECK(t->owner==ODG_TURRET_NEUTRAL);CHECK(p->carried_chip==0u);CHECK(chip->active!=0u);CHECK(chip->carried_by==0u);
        p->active=0u;
        CHECK(odg_player_hack_action_available()==0u);
        CHECK(odg_player_turret_action_available()==0u);
        CHECK(odg_player_drop_action_available()==0u);
    }

    /* Bot logistics must obey the same neutral/chip rule. With only a neutral turret in
     * the world, a chip carrier keeps its existing route instead of pursuing an objective
     * that consume_chip_and_hack() will necessarily reject. */
    CHECK(odg_init(UINT64_C(0x424f544e45555452), 320u, 180u) == ODG_STATUS_OK);
    {
        odg_actor *bot=&g_odg.actors[1];odg_turret *t=&g_odg.turrets[0];odg_chip *chip=&g_odg.chips[0];uint32_t j;
        g_odg.obstacle_count=0u;
        for(j=0u;j<g_odg.turret_count;++j)g_odg.turrets[j].active=0u;
        t->active=1u;t->owner=ODG_TURRET_NEUTRAL;t->carried_by=ODG_TURRET_NONE;t->x=0;t->z=5*ODG_FX_ONE;
        bot->x=0;bot->z=0;bot->last_cell=odg_cell_from_world(0,0);bot->vx=0;bot->vz=0;
        bot->face_x_q15=ODG_Q15_ONE;bot->face_z_q15=0;bot->ai_x_q15=ODG_Q15_ONE;bot->ai_z_q15=0;
        bot->think_cd=100u;bot->ai_commit_ticks=0u;bot->trail_active=0u;bot->carried_ammo_crate=UINT32_MAX;bot->ammo_reserve=0u;
        chip->active=1u;chip->kind=ODG_CHIP_KIND_REPROGRAM;chip->carried_by=1u;chip->pickup_cd=0u;bot->carried_chip=0u;
        odg_set_input(0,0,0,0,0u);odg_step_ticks(1u);
        CHECK(bot->ai_x_q15==ODG_Q15_ONE);CHECK(bot->ai_z_q15==0);CHECK(bot->ai_commit_ticks==0u);
        CHECK(t->owner==ODG_TURRET_NEUTRAL);CHECK(chip->active!=0u);CHECK(chip->carried_by==1u);CHECK(bot->carried_chip==0u);
    }

    /* Enemy-programmed turrets likewise ignore ground painting. A reprogram chip is
     * consumed on use and changes only that turret's allegiance. */
    CHECK(odg_init(UINT64_C(0x434849504841434b), 320u, 180u) == ODG_STATUS_OK);
    {
        odg_actor *p=&g_odg.actors[0];odg_turret *t=&g_odg.turrets[0];odg_chip *chip=&g_odg.chips[0];
        uint8_t own=ODG_OWNER_FROM_ID(0u),enemy=ODG_OWNER_FROM_ID(1u);
        uint32_t tc=odg_cell_from_world(t->x,t->z);int32_t cx=(int32_t)(tc&(ODG_GRID_SIZE-1u)),cz=(int32_t)(tc>>ODG_GRID_SHIFT),dz;
        t->owner=enemy;t->carried_by=ODG_TURRET_NONE;
        for(dz=-2;dz<=2;++dz){int32_t dx;for(dx=-2;dx<=2;++dx){int32_t x=cx+dx,z=cz+dz;if(x>=0&&z>=0&&x<(int32_t)ODG_GRID_SIZE&&z<(int32_t)ODG_GRID_SIZE){uint32_t cc=(uint32_t)z*ODG_GRID_SIZE+(uint32_t)x;if(g_odg.playable[cc]){uint8_t old=g_odg.territory[cc];if(old!=ODG_OWNER_NONE&&g_odg.territory_count[ODG_ID_FROM_OWNER(old)]>0u)--g_odg.territory_count[ODG_ID_FROM_OWNER(old)];g_odg.territory[cc]=own;++g_odg.territory_count[0];}}}}
        odg_update_turret_ownership_internal();CHECK(t->owner==enemy);
        p->x=t->x;p->z=t->z;chip->active=1u;chip->kind=ODG_CHIP_KIND_REPROGRAM;chip->carried_by=0u;chip->pickup_cd=0u;p->carried_chip=0u;
        CHECK(odg_player_hack_action_available()!=0u);
        odg_set_input(0,0,0,0,ODG_BUTTON_ACTION);odg_step_ticks(1u);
        CHECK(t->owner==own);CHECK(p->carried_chip==UINT32_MAX);CHECK(chip->active==0u);
    }

    /* Infrastructure cannot be deployed from hostile ground even while already carried. */
    CHECK(odg_init(UINT64_C(0x444f4d41494e5452), 320u, 180u) == ODG_STATUS_OK);
    {
        odg_actor *p=&g_odg.actors[0];odg_turret *t=&g_odg.turrets[0];int32_t tx=0,tz=0;uint32_t pc=p->last_cell,foreign=find_adjacent_playable(pc);
        t->owner=ODG_OWNER_FROM_ID(0u);t->carried_by=0u;g_odg.player_carried_turret=0u;
        CHECK(foreign!=UINT32_MAX);
        if(foreign!=UINT32_MAX){g_odg.territory[foreign]=ODG_OWNER_FROM_ID(1u);p->x=odg_cell_center_x(foreign);p->z=odg_cell_center_z(foreign);CHECK(odg_turret_drop_candidate_internal(p,&tx,&tz)==0);}
    }

    /* Owned turret can be picked up/dropped and its fire attacks topology, not HP damage. */
    CHECK(odg_init(UINT64_C(0x545552524554), 320u, 180u) == ODG_STATUS_OK);
    {
        odg_turret *t=&g_odg.turrets[0];
        odg_actor *p=&g_odg.actors[0];
        t->owner=ODG_OWNER_FROM_ID(0u); t->x=p->x; t->z=p->z; t->carried_by=ODG_TURRET_NONE;
        odg_set_input(0,0,0,0,ODG_BUTTON_ACTION); odg_step_ticks(1u);
        CHECK(odg_player_carrying_turret()!=0u);
        odg_set_input(0,0,0,0,0u); odg_step_ticks(1u);
        {
            int32_t px=p->x,pz=p->z,fx=p->face_x_q15,fz=p->face_z_q15;
            odg_set_input(0,0,0,0,ODG_BUTTON_ACTION); odg_step_ticks(1u);
            CHECK(odg_player_carrying_turret()==0u);
            if (odg_player_carrying_turret()==0u) {
                int32_t dx=t->x-px,dz=t->z-pz;
                int64_t cross=(int64_t)dx*fz-(int64_t)dz*fx;
                int64_t dot=(int64_t)dx*fx+(int64_t)dz*fz;
                CHECK(cross>-900000 && cross<900000);
                CHECK(dot>0);
                CHECK(odg_dist2(px,pz,t->x,t->z)>(int64_t)(16*ODG_FX_ONE/10)*(16*ODG_FX_ONE/10));
                CHECK(odg_dist2(px,pz,t->x,t->z)<(int64_t)(22*ODG_FX_ONE/10)*(22*ODG_FX_ONE/10));
            }
        }
    }

    /* Supply crates are physical logistics: contact picks one up, proximity to an
     * owned depleted turret transfers its ammunition automatically. */
    CHECK(odg_init(UINT64_C(0x535550504c59), 320u, 180u) == ODG_STATUS_OK);
    {
        odg_actor *p=&g_odg.actors[0];
        odg_turret *t=&g_odg.turrets[0];
        odg_ammo_crate *cr=&g_odg.ammo_crates[0];
        uint32_t before;
        t->owner=ODG_OWNER_FROM_ID(0u);t->ammo=1u;t->max_ammo=48u;t->carried_by=ODG_TURRET_NONE;
        cr->active=1u;cr->carried_by=UINT32_MAX;cr->ammo=12u;cr->x=p->x;cr->z=p->z;
        odg_set_input(0,0,0,0,0u);odg_step_ticks(1u);
        CHECK(odg_player_carrying_ammo_crate()!=0u);
        before=t->ammo;
        p->x=t->x;p->z=t->z;
        odg_step_ticks(1u);
        CHECK(t->ammo>before);
        CHECK(cr->ammo<12u);
    }

    CHECK(odg_init(UINT64_C(0x54555246495245), 320u, 180u) == ODG_STATUS_OK);
    {
        odg_turret *t=&g_odg.turrets[0];
        uint32_t tc=odg_cell_from_world(t->x,t->z);
        uint32_t target=find_playable_at_distance(tc,3u,4u);
        CHECK(target!=UINT32_MAX);
        if(target!=UINT32_MAX){
            uint8_t enemy=ODG_OWNER_FROM_ID(1u);
            t->owner=ODG_OWNER_FROM_ID(0u); t->fire_cd=0u; t->ammo=4u; t->range_fx=6*ODG_FX_ONE;
            g_odg.trail_owner[target]=enemy;
            g_odg.actors[1].trail_active=1u; g_odg.actors[1].trail_len=ODG_TURRET_TRAIL_MIN_CELLS;
            odg_set_input(0,0,0,0,0u); odg_step_ticks(1u);
            CHECK(g_odg.actors[1].hp==1u);
            CHECK(t->target_kind==ODG_TURRET_TARGET_TRAIL);
            CHECK(t->aim_ticks>0u);
            CHECK(t->ammo==4u);
            odg_step_ticks(t->aim_required+1u);
            CHECK(g_odg.actors[1].hp==0u);
            CHECK(g_odg.actors[1].death_reason==ODG_DEATH_TURRET_TRAIL_CUT);
            CHECK(t->ammo==3u);
        }
    }

    /* A turret commits to one trail cell while locking. A newly appearing closer cell
     * must not make the head chatter or restart aim every tick. */
    CHECK(odg_init(UINT64_C(0x535441424c4c4f43), 320u, 180u) == ODG_STATUS_OK);
    {
        odg_turret *t=&g_odg.turrets[0];uint32_t tc=odg_cell_from_world(t->x,t->z);
        uint32_t far=find_playable_at_distance(tc,4u,4u),near=find_playable_at_distance(tc,3u,3u);
        uint8_t enemy=ODG_OWNER_FROM_ID(1u);
        CHECK(far!=UINT32_MAX && near!=UINT32_MAX && far!=near);
        if(far!=UINT32_MAX && near!=UINT32_MAX && far!=near){
            t->owner=ODG_OWNER_FROM_ID(0u);t->fire_cd=0u;t->ammo=4u;t->range_fx=6*ODG_FX_ONE;t->target_kind=ODG_TURRET_TARGET_NONE;t->retarget_cd=0u;
            g_odg.trail_owner[far]=enemy;g_odg.actors[1].trail_active=1u;g_odg.actors[1].trail_len=ODG_TURRET_TRAIL_MIN_CELLS;
            odg_step_ticks(1u);CHECK(t->target_kind==ODG_TURRET_TARGET_TRAIL);CHECK(t->last_target_cell==far);
            g_odg.trail_owner[near]=enemy;
            odg_step_ticks(12u);CHECK(t->last_target_cell==far);CHECK(t->target_kind==ODG_TURRET_TARGET_TRAIL);
        }
    }

    /* A short exposed trail can close/disappear before lock completes. The turret must
     * cancel that shot instead of retroactively killing its former owner. */
    CHECK(odg_init(UINT64_C(0x4c4f434b45534350), 320u, 180u) == ODG_STATUS_OK);
    {
        odg_turret *t=&g_odg.turrets[0];
        uint32_t tc=odg_cell_from_world(t->x,t->z);
        uint32_t target=find_playable_at_distance(tc,3u,4u);
        CHECK(target!=UINT32_MAX);
        if(target!=UINT32_MAX){
            uint8_t enemy=ODG_OWNER_FROM_ID(1u);
            uint32_t ammo_before=4u;
            t->owner=ODG_OWNER_FROM_ID(0u); t->fire_cd=0u; t->ammo=ammo_before;
            t->range_fx=6*ODG_FX_ONE; t->target_kind=ODG_TURRET_TARGET_NONE;
            g_odg.trail_owner[target]=enemy;
            g_odg.actors[1].trail_active=1u; g_odg.actors[1].trail_len=1u;
            odg_set_input(0,0,0,0,0u); odg_step_ticks(1u);
            CHECK(t->target_kind==ODG_TURRET_TARGET_NONE);
            CHECK(t->ammo==ammo_before);
            g_odg.actors[1].trail_len=ODG_TURRET_TRAIL_MIN_CELLS;
            odg_step_ticks(1u);
            CHECK(t->target_kind==ODG_TURRET_TARGET_TRAIL);
            CHECK(t->aim_ticks>0u);
            g_odg.trail_owner[target]=ODG_OWNER_NONE;
            g_odg.actors[1].trail_active=0u; g_odg.actors[1].trail_len=0u;
            odg_step_ticks(t->aim_required+2u);
            CHECK(g_odg.actors[1].hp==1u);
            CHECK(t->ammo==ammo_before);
            CHECK(t->target_kind==ODG_TURRET_TARGET_NONE);
        }
    }

    /* Territory-earned reserve is spatial logistics, not map-wide teleportation. */
    CHECK(odg_init(UINT64_C(0x52455345525645), 320u, 180u) == ODG_STATUS_OK);
    {
        odg_actor *p=&g_odg.actors[0];
        odg_turret *t=&g_odg.turrets[0];
        uint32_t far=0u;
        t->owner=ODG_OWNER_FROM_ID(0u);t->ammo=0u;t->max_ammo=48u;t->carried_by=ODG_TURRET_NONE;
        p->ammo_reserve=11u;
        while(far<ODG_CELL_COUNT && (!g_odg.playable[far] ||
              odg_dist2(t->x,t->z,odg_cell_center_x(far),odg_cell_center_z(far)) <
              (int64_t)(8*ODG_FX_ONE)*(8*ODG_FX_ONE))) ++far;
        CHECK(far<ODG_CELL_COUNT);
        if(far<ODG_CELL_COUNT){p->x=odg_cell_center_x(far);p->z=odg_cell_center_z(far);}
        odg_set_input(0,0,0,0,0u);odg_step_ticks(1u);
        CHECK(t->ammo==0u);
        CHECK(p->ammo_reserve==11u);
        p->x=t->x;p->z=t->z;
        odg_step_ticks(1u);
        CHECK(t->ammo==11u);
        CHECK(p->ammo_reserve==0u);
        /* Partial top-up: a 47/48 turret consumes exactly one round, not the whole reserve. */
        t->ammo=47u;p->ammo_reserve=9u;odg_step_ticks(1u);
        CHECK(t->ammo==48u);CHECK(p->ammo_reserve==8u);
        CHECK(odg_player_nearby_owned_turret_visible()!=0u);
        CHECK(odg_player_nearby_owned_turret_ammo()==48u);
        CHECK(odg_player_nearby_owned_turret_max_ammo()==48u);
    }

    /* DROP materializes territory-earned reserve as a physical crate instead of deleting it. */
    CHECK(odg_init(UINT64_C(0x44524f5052455356), 320u, 180u) == ODG_STATUS_OK);
    {
        odg_actor *p=&g_odg.actors[0];uint32_t before=g_odg.ammo_crate_count;
        p->ammo_reserve=7u;CHECK(odg_player_drop_action_available()!=0u);
        odg_set_input(0,0,0,0,ODG_BUTTON_DROP);odg_step_ticks(1u);
        CHECK(p->ammo_reserve==0u);CHECK(g_odg.ammo_crate_count>=before);
        {uint32_t found=0u,j;for(j=0u;j<g_odg.ammo_crate_count;++j){odg_ammo_crate *crate=&g_odg.ammo_crates[j];if(crate->active&&crate->carried_by==UINT32_MAX&&crate->ammo==7u&&odg_dist2(crate->x,crate->z,p->x,p->z)<=(int64_t)ODG_FX_ONE*ODG_FX_ONE){found=1u;break;}}CHECK(found!=0u);}
    }

    /* Contextual action is truly contextual: hidden away from owned turrets, visible
     * in pickup range or while carrying one, and still available while carrying supply. */
    CHECK(odg_init(UINT64_C(0x434f4e54455854), 320u, 180u) == ODG_STATUS_OK);
    {
        odg_actor *p=&g_odg.actors[0];
        odg_turret *t=&g_odg.turrets[0];
        odg_ammo_crate *cr=&g_odg.ammo_crates[0];
        uint32_t far_cell=0u;
        while(far_cell<ODG_CELL_COUNT && (!g_odg.playable[far_cell] ||
              odg_dist2(p->x,p->z,odg_cell_center_x(far_cell),odg_cell_center_z(far_cell)) <
              (int64_t)(8*ODG_FX_ONE)*(8*ODG_FX_ONE))) ++far_cell;
        CHECK(far_cell<ODG_CELL_COUNT);
        t->owner=ODG_OWNER_FROM_ID(0u); t->carried_by=ODG_TURRET_NONE;
        if(far_cell<ODG_CELL_COUNT){t->x=odg_cell_center_x(far_cell);t->z=odg_cell_center_z(far_cell);}
        CHECK(odg_player_turret_action_available()==0u);
        t->x=p->x;t->z=p->z;
        CHECK(odg_player_turret_action_available()!=0u);
        cr->active=1u;cr->ammo=10u;cr->carried_by=0u;p->carried_ammo_crate=0u;
        CHECK(odg_player_turret_action_available()!=0u);
        cr->carried_by=UINT32_MAX;p->carried_ammo_crate=UINT32_MAX;
        g_odg.player_carried_turret=0u;t->carried_by=0u;
        CHECK(odg_player_turret_action_available()!=0u);
    }

    /* If no exposed trail exists, a turret spends one shot to transfer exactly one
     * enemy territory cell instead of damaging an actor. */
    CHECK(odg_init(UINT64_C(0x43454c4c53484f54), 320u, 180u) == ODG_STATUS_OK);
    {
        odg_turret *t=&g_odg.turrets[0];
        uint32_t tc=odg_cell_from_world(t->x,t->z);
        uint32_t target=find_adjacent_playable(tc);
        CHECK(target!=UINT32_MAX);
        if(target!=UINT32_MAX){
            uint8_t own=ODG_OWNER_FROM_ID(0u),enemy=ODG_OWNER_FROM_ID(1u);
            uint8_t old=g_odg.territory[target];
            if(old!=ODG_OWNER_NONE){uint32_t oid=ODG_ID_FROM_OWNER(old);if(g_odg.territory_count[oid])--g_odg.territory_count[oid];}
            g_odg.territory[target]=enemy; ++g_odg.territory_count[1];
            t->owner=own; t->fire_cd=0u; t->ammo=4u; t->range_fx=5*ODG_FX_ONE;
            odg_set_input(0,0,0,0,0u); odg_step_ticks(1u);
            CHECK(g_odg.territory[target]==enemy);
            CHECK(t->target_kind==ODG_TURRET_TARGET_TERRITORY);
            CHECK(t->ammo==4u);
            odg_step_ticks(t->aim_required+1u);
            CHECK(g_odg.territory[target]==own);
            CHECK(t->ammo==3u);
            CHECK(g_odg.actors[1].hp==1u);
        }
    }

    /* Coplanar territory must remain visible from all cardinal chase-camera yaws. */
    CHECK(odg_init(UINT64_C(0x535441424c455245), 320u, 180u) == ODG_STATUS_OK);
    {
        static const int32_t dirs[8][2]={{0,ODG_Q15_ONE},{23170,23170},{ODG_Q15_ONE,0},{23170,-23170},{0,-ODG_Q15_ONE},{-23170,-23170},{-ODG_Q15_ONE,0},{-23170,23170}};
        static uint8_t owned_frame[320u*180u*4u];
        uint32_t d;
        odg_actor *p=&g_odg.actors[0];
        odg_memset(g_odg.territory,0,sizeof(g_odg.territory));
        odg_memset(g_odg.territory_count,0,sizeof(g_odg.territory_count));
        for(d=0u;d<ODG_CELL_COUNT;++d){
            int32_t cx=(int32_t)(d&(ODG_GRID_SIZE-1u))-(int32_t)(ODG_GRID_SIZE/2u);
            int32_t cz=(int32_t)(d>>ODG_GRID_SHIFT)-(int32_t)(ODG_GRID_SIZE/2u);
            if(g_odg.playable[d] && cx>=-7&&cx<=7&&cz>=-7&&cz<=7){g_odg.territory[d]=ODG_OWNER_FROM_ID(0u);++g_odg.territory_count[0];}
        }
        p->x=0;p->z=0;g_odg.camera_anchor_x=0;g_odg.camera_anchor_z=0;
        for(d=0u;d<8u;++d){
            uint32_t off;
            p->face_x_q15=dirs[d][0];p->face_z_q15=dirs[d][1];
            g_odg.camera_dir_x_q15=dirs[d][0];g_odg.camera_dir_z_q15=dirs[d][1];
            for(off=0u;off<3u;++off){
                uintptr_t rp;
                uint32_t changed,byte_count,cell;
                static const int32_t offs[3][2]={{0,0},{2*ODG_FX_ONE,ODG_FX_ONE},{-2*ODG_FX_ONE,-ODG_FX_ONE}};
                p->x=offs[off][0];p->z=offs[off][1];
                g_odg.camera_anchor_x=p->x;g_odg.camera_anchor_z=p->z;
                rp=odg_render_frame();
                byte_count=odg_framebuffer_bytes();
                odg_memcpy(owned_frame,(const uint8_t*)rp,byte_count);
                for(cell=0u;cell<ODG_CELL_COUNT;++cell)
                    if(g_odg.territory[cell]==ODG_OWNER_FROM_ID(0u))g_odg.territory[cell]=ODG_OWNER_NONE;
                rp=odg_render_frame();
                changed=count_rgb_difference(owned_frame,(const uint8_t*)rp,byte_count);
                CHECK(changed>260u);
                for(cell=0u;cell<ODG_CELL_COUNT;++cell){
                    int32_t cx=(int32_t)(cell&(ODG_GRID_SIZE-1u))-(int32_t)(ODG_GRID_SIZE/2u);
                    int32_t cz=(int32_t)(cell>>ODG_GRID_SHIFT)-(int32_t)(ODG_GRID_SIZE/2u);
                    if(g_odg.playable[cell]&&cx>=-7&&cx<=7&&cz>=-7&&cz<=7)
                        g_odg.territory[cell]=ODG_OWNER_FROM_ID(0u);
                }
            }
        }
    }

    a = run_script(UINT64_C(0x123456789abcdef0));
    b = run_script(UINT64_C(0x123456789abcdef0));
    c = run_script(UINT64_C(0x123456789abcdef1));
    CHECK(a == b);
    CHECK(a != c);
    CHECK(counts_are_consistent());

    CHECK(odg_resize(480u, 270u) == ODG_STATUS_OK);
    fb = odg_render_frame();
    CHECK(fb != (uintptr_t)0);
    bytes = odg_framebuffer_bytes();
    CHECK(bytes == 480u * 270u * 4u);
    {
        const uint8_t *px = (const uint8_t *)fb;
        for (i = 0u; i < bytes; i += 257u) if (px[i] != 0u) ++nonzero;
    }
    CHECK(nonzero > 100u);
    CHECK(odg_player_territory_permille() <= 1000u);

    /* Ultra preview target is a real engine resolution, not browser upscaling. */
    CHECK(odg_resize(1280u,720u)==ODG_STATUS_OK);
    fb=odg_render_frame();
    CHECK(fb!=(uintptr_t)0);
    CHECK(odg_framebuffer_bytes()==1280u*720u*4u);
    CHECK(odg_resize(480u,270u)==ODG_STATUS_OK);

    /* Opening population remains stable: no artificial self-cross deaths. */
    CHECK(odg_init(1234u, 320u, 180u) == ODG_STATUS_OK);
    for (i = 0u; i < 3600u; ++i) { odg_set_input(0,0,0,0,0u); odg_step_ticks(1u); }
    CHECK(odg_alive_count() >= 8u);

    /* Planner/physics agreement regression. The return BFS now excludes physically
     * obstructed cells, and locomotion has contact hysteresis. Across a deterministic
     * six-thousand-tick sample no bot may fall into rapid full-direction reversals or
     * accumulate repeated no-progress watchdog windows. */
    CHECK(odg_init(UINT64_C(0x424f544348415454), 320u, 180u) == ODG_STATUS_OK);
    {
        int32_t pvx[ODG_MAX_ACTORS]={0},pvz[ODG_MAX_ACTORS]={0};
        uint32_t reversals[ODG_MAX_ACTORS]={0u};
        uint32_t t,j;
        for(t=0u;t<6000u;++t){
            odg_set_input(0,0,0,0,0u);odg_step_ticks(1u);
            for(j=1u;j<ODG_MAX_ACTORS;++j){
                odg_actor *bot=&g_odg.actors[j];
                if(!bot->active||bot->hp==0u) continue;
                if((pvx[j]!=0||pvz[j]!=0) && (bot->vx!=0||bot->vz!=0)){
                    int64_t dot=(int64_t)pvx[j]*bot->vx+(int64_t)pvz[j]*bot->vz;
                    if(dot<-400) ++reversals[j];
                }
                pvx[j]=bot->vx;pvz[j]=bot->vz;
            }
        }
        for(j=1u;j<ODG_MAX_ACTORS;++j){
            CHECK(reversals[j]<=4u);
            CHECK(g_odg.actors[j].stuck_windows<=2u);
        }
    }

    if (failures != 0) {
        fprintf(stderr, "%d test(s) failed\n", failures);
        return 1;
    }
    printf("OK v14 deterministic=%016llx framebuffer=%u playable=%u turrets=%u\n",
           (unsigned long long)a, bytes, odg_territory_total_cells(), odg_turret_count());
    return 0;
}
