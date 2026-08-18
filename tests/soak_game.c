#include "game_internal.h"

#include <stdint.h>
#include <stdio.h>

static uint32_t rng32(uint32_t *s) { uint32_t x=*s; x^=x<<13; x^=x>>17; x^=x<<5; *s=x; return x; }

static int check_world(void) {
    uint32_t territory_counts[ODG_MAX_ACTORS]={0u};
    uint32_t trail_counts[ODG_MAX_ACTORS]={0u};
    uint32_t playable=0u;
    uint32_t i;
    for(i=0u;i<ODG_CELL_COUNT;++i){
        uint8_t o=g_odg.territory[i],t=g_odg.trail_owner[i];
        if(g_odg.playable[i]) ++playable;
        else if(o!=ODG_OWNER_NONE||t!=ODG_OWNER_NONE) return 0;
        if(o!=ODG_OWNER_NONE){uint32_t id=ODG_ID_FROM_OWNER(o);if(id>=ODG_MAX_ACTORS)return 0;++territory_counts[id];}
        if(t!=ODG_OWNER_NONE){uint32_t id=ODG_ID_FROM_OWNER(t);if(id>=ODG_MAX_ACTORS)return 0;++trail_counts[id];}
    }
    if(playable!=g_odg.playable_count || playable==ODG_CELL_COUNT) return 0;
    for(i=0u;i<ODG_MAX_ACTORS;++i){
        odg_actor *a=&g_odg.actors[i];
        if(!a->active||a->hp>1u||a->max_hp!=1u) return 0;
        if(a->ammo_reserve>ODG_AMMO_RESERVE_MAX) return 0;
        if(a->carried_ammo_crate!=UINT32_MAX){
            odg_ammo_crate *c;
            if(a->carried_ammo_crate>=g_odg.ammo_crate_count) return 0;
            c=&g_odg.ammo_crates[a->carried_ammo_crate];
            if(!c->active||c->carried_by!=i||c->ammo==0u) return 0;
        }
        if(territory_counts[i]!=g_odg.territory_count[i]||trail_counts[i]!=a->trail_len) return 0;
        if((a->trail_len!=0u)!=(a->trail_active!=0u)) return 0;
        if(a->hp==0u&&(a->trail_len!=0u||territory_counts[i]!=0u)) return 0;
        if(a->hp!=0u){
            uint32_t c=odg_cell_from_world(a->x,a->z);
            if(g_odg.playable[c]==0u||g_odg.territory_count[i]==0u) return 0;
        }
    }
    for(i=0u;i<g_odg.turret_count;++i){
        odg_turret *t=&g_odg.turrets[i];
        if(!t->active||t->ammo>t->max_ammo) return 0;
        if(t->target_kind>ODG_TURRET_TARGET_TERRITORY) return 0;
        if(t->target_kind==ODG_TURRET_TARGET_NONE && t->aim_ticks!=0u) return 0;
        if(t->aim_ticks>t->aim_required) return 0;
        if(t->owner!=ODG_TURRET_NEUTRAL&&ODG_ID_FROM_OWNER(t->owner)>=ODG_MAX_ACTORS) return 0;
        if(t->carried_by==ODG_TURRET_NONE){uint32_t c=odg_cell_from_world(t->x,t->z);if(g_odg.playable[c]==0u)return 0;}
    }
    for(i=0u;i<g_odg.ammo_crate_count;++i){
        odg_ammo_crate *c=&g_odg.ammo_crates[i];
        if(!c->active){if(c->carried_by!=UINT32_MAX)return 0;continue;}
        if(c->ammo==0u) return 0;
        if(c->carried_by!=UINT32_MAX){
            if(c->carried_by>=ODG_MAX_ACTORS) return 0;
            if(g_odg.actors[c->carried_by].hp==0u||g_odg.actors[c->carried_by].carried_ammo_crate!=i) return 0;
        } else {uint32_t cc=odg_cell_from_world(c->x,c->z);if(g_odg.playable[cc]==0u)return 0;}
    }
    for(i=0u;i<g_odg.chip_count;++i){
        odg_chip *c=&g_odg.chips[i];
        if(!c->active){if(c->carried_by!=UINT32_MAX)return 0;continue;}
        if(c->kind!=ODG_CHIP_KIND_REPROGRAM) return 0;
        if(c->carried_by!=UINT32_MAX){
            if(c->carried_by>=ODG_MAX_ACTORS) return 0;
            if(g_odg.actors[c->carried_by].hp==0u||
               g_odg.actors[c->carried_by].carried_chip!=i) return 0;
        } else {uint32_t cc=odg_cell_from_world(c->x,c->z);if(g_odg.playable[cc]==0u)return 0;}
    }
    if(g_odg.player_carried_turret!=ODG_TURRET_NONE){
        if(g_odg.player_carried_turret>=g_odg.turret_count) return 0;
        if(g_odg.turrets[g_odg.player_carried_turret].carried_by!=ODG_PLAYER_ID) return 0;
        if(g_odg.actors[ODG_PLAYER_ID].carried_ammo_crate!=UINT32_MAX) return 0;
    }
    return 1;
}

int main(void){
    uint32_t r=0x1234abcdu,i,previous_alive,rounds=1u;
    uint64_t reset_seed=UINT64_C(0xf00dcafe12345678);
    if(odg_init(reset_seed,480u,270u)!=0)return 2;
    previous_alive=odg_alive_count();
    for(i=0u;i<60000u;++i){
        int32_t mx=(int32_t)(rng32(&r)&65535u)-32768;
        int32_t mz=(int32_t)(rng32(&r)&65535u)-32768;
        uint32_t buttons=0u;
        if((rng32(&r)&511u)==0u) buttons|=ODG_BUTTON_DASH;
        if((rng32(&r)&2047u)==0u) buttons|=ODG_BUTTON_ACTION;
        if((rng32(&r)&4095u)==0u) buttons|=ODG_BUTTON_DROP;
        odg_set_input(mx,mz,0,0,buttons); odg_step_ticks(1u);
        if(odg_alive_count()>previous_alive){fprintf(stderr,"automatic respawn at %u\n",i);return 5;}
        previous_alive=odg_alive_count();
        if((i%120u)==0u||odg_match_over()!=0u){
            uint32_t mode=(i/120u)%3u;
            if(mode==0u)(void)odg_resize(320u,180u); else if(mode==1u)(void)odg_resize(480u,270u); else (void)odg_resize(640u,360u);
            if(odg_render_frame()==(uintptr_t)0)return 3;
            if(!check_world()){fprintf(stderr,"invariant failed tick=%u round=%u\n",i,rounds);return 4;}
        }
        if(odg_match_over()!=0u){
            reset_seed^=((uint64_t)i+UINT64_C(1))*UINT64_C(0x9e3779b97f4a7c15);
            odg_reset(reset_seed);++rounds;previous_alive=odg_alive_count();
            if(previous_alive!=ODG_MAX_ACTORS||!check_world())return 6;
        }
    }
    printf("SOAK V14 OK outer_ticks=60000 rounds=%u hash=%016llx cells=%u alive=%u turrets=%u chips=%u\n",rounds,(unsigned long long)odg_state_hash(),odg_player_territory_cells(),odg_alive_count(),odg_turret_count(),odg_chip_count());
    return 0;
}
