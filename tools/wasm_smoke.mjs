import fs from 'node:fs';

const bytes = fs.readFileSync(new URL('../build/odpar_territorial_domain.wasm', import.meta.url));
const { instance } = await WebAssembly.instantiate(bytes, {});
const e = instance.exports;

if (e.odg_api_version() !== 14) throw new Error(`bad api ${e.odg_api_version()}`);
if (e.odg_init(0x1234567890abcdn, 480, 270) !== 0) throw new Error('init failed');

const required = [
  'odg_ffi_abi_query', 'odg_framebuffer_stride_bytes', 'odg_copy_framebuffer',
  'odg_stats_ptr', 'odg_copy_stats',
  'odg_territory_total_cells', 'odg_player_territory_cells', 'odg_player_territory_permille',
  'odg_player_trail_cells', 'odg_player_trail_active', 'odg_turret_count',
  'odg_player_owned_turrets', 'odg_player_carrying_turret', 'odg_player_carried_turret_ammo',
  'odg_player_turret_action_available', 'odg_ammo_crate_count', 'odg_player_carrying_ammo_crate', 'odg_player_carried_ammo',
  'odg_player_facing_x_q15', 'odg_player_facing_z_q15', 'odg_camera_dir_x_q15', 'odg_camera_dir_z_q15',
  'odg_control_heading_x_q15', 'odg_control_heading_z_q15', 'odg_control_local_x_q15', 'odg_control_local_z_q15', 'odg_control_strength_q15',
  'odg_set_world_input', 'odg_set_visual_theme', 'odg_visual_theme', 'odg_set_presentation_mode', 'odg_presentation_mode',
  'odg_match_over', 'odg_winner_id', 'odg_player_death_reason',
  'odg_chip_count', 'odg_player_carrying_chip', 'odg_player_hack_action_available', 'odg_player_drop_action_available',
  'odg_player_nearby_owned_turret_visible', 'odg_player_nearby_owned_turret_ammo', 'odg_player_nearby_owned_turret_max_ammo'
];
for (const name of required) if (typeof e[name] !== 'function') throw new Error(`missing export ${name}`);

e.odg_set_world_input(32767, 0, 30000, 0, 0, 0);
e.odg_step_ticks(24);
for (let i = 0; i < 1000 && e.odg_match_over() === 0; i++) {
  const phase = Math.floor(i / 200) % 4;
  const mx = phase === 1 ? 25000 : phase === 3 ? -25000 : 0;
  const my = phase === 0 ? 30000 : phase === 2 ? -30000 : 0;
  e.odg_set_input(mx, my, 0, 0, 0);
  e.odg_step_ticks(1);
}

const ptr = e.odg_render_frame();
const len = e.odg_framebuffer_bytes();
const view = new Uint8Array(e.memory.buffer, ptr, len);
let hash = 2166136261 >>> 0;
for (let i = 0; i < view.length; i += 97) { hash ^= view[i]; hash = Math.imul(hash, 16777619) >>> 0; }
const total = e.odg_territory_total_cells();
const permille = e.odg_player_territory_permille();
const alive = e.odg_alive_count();
if (!ptr || len !== 480 * 270 * 4 || hash === 0) throw new Error('bad frame');
if (!(total > 8000 && total < 16384)) throw new Error(`bad playable territory ${total}`);
if (e.odg_turret_count() !== 14) throw new Error('bad turret count');
if (permille > 1000 || alive > 10) throw new Error('bad gameplay stats');

e.odg_set_visual_theme(3);
if (e.odg_visual_theme() !== 3) throw new Error('theme api failed');
const gameplayHash=e.odg_state_hash();
e.odg_set_presentation_mode(1);
if (e.odg_presentation_mode() !== 1 || e.odg_state_hash() !== gameplayHash) throw new Error('presentation api failed');
e.odg_set_presentation_mode(0);
console.log(`WASM OK api=14 bytes=${bytes.length} framebuffer=${len} sampleHash=${hash.toString(16)} playable=${total} territory=${(permille/10).toFixed(1)}% alive=${alive} turrets=${e.odg_turret_count()}`);
