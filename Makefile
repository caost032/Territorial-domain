SHELL := /bin/sh
CC ?= cc
CLANG ?= clang
BUILD := build
INCLUDES := -Iengine/include -Iengine/src -Iengine/vendor
WARN := -Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wsign-conversion -Werror
NATIVE_HARDEN := -fstack-protector-strong -D_FORTIFY_SOURCE=2
NATIVE_LINK := -Wl,--version-script=engine/odpar_territorial_domain.exports.map -Wl,-z,relro,-z,now,-z,noexecstack
SANITIZER_OPTIONS ?= detect_leaks=1
ENGINE := engine/src/platform.c engine/src/game.c engine/src/sim.c engine/src/render.c engine/vendor/odm_status.c engine/vendor/odm_rng.c

.PHONY: all native symbols hardening host-check wasm test ffi-test soak asan bench clean bundle
all: native wasm

$(BUILD):
	mkdir -p $(BUILD)

native: $(BUILD)
	$(CC) -std=c11 -O3 -fPIC -fno-builtin $(NATIVE_HARDEN) $(WARN) $(INCLUDES) $(ENGINE) -shared $(NATIVE_LINK) -o $(BUILD)/libodpar_territorial_domain.so
	$(MAKE) symbols
	$(MAKE) hardening

symbols:
	python3 tools/check_native_symbols.py

hardening:
	python3 tools/check_native_hardening.py

host-check:
	python3 tools/check_host_contract.py

wasm: $(BUILD)
	$(CLANG) --target=wasm32 -std=c11 -O3 -ffreestanding -fno-builtin -nostdlib $(INCLUDES) $(ENGINE) \
		-Wl,--no-entry -Wl,--export-memory -Wl,--initial-memory=16777216 -Wl,--max-memory=33554432 \
		-Wl,--export=odg_api_version -Wl,--export=odg_ffi_abi_query -Wl,--export=odg_init -Wl,--export=odg_resize -Wl,--export=odg_reset \
		-Wl,--export=odg_set_input -Wl,--export=odg_set_world_input -Wl,--export=odg_tick_us -Wl,--export=odg_step_ticks \
		-Wl,--export=odg_render_frame -Wl,--export=odg_framebuffer_ptr -Wl,--export=odg_framebuffer_bytes -Wl,--export=odg_framebuffer_stride_bytes -Wl,--export=odg_copy_framebuffer \
		-Wl,--export=odg_render_width -Wl,--export=odg_render_height -Wl,--export=odg_stats_ptr -Wl,--export=odg_copy_stats \
		-Wl,--export=odg_set_visual_theme -Wl,--export=odg_visual_theme -Wl,--export=odg_set_presentation_mode -Wl,--export=odg_presentation_mode \
		-Wl,--export=odg_player_health -Wl,--export=odg_player_max_health -Wl,--export=odg_player_score \
		-Wl,--export=odg_player_level -Wl,--export=odg_player_kills -Wl,--export=odg_player_deaths \
		-Wl,--export=odg_alive_count -Wl,--export=odg_zone_radius_milli -Wl,--export=odg_state_hash \
		-Wl,--export=odg_territory_total_cells -Wl,--export=odg_player_territory_cells -Wl,--export=odg_player_territory_permille \
		-Wl,--export=odg_player_trail_cells -Wl,--export=odg_player_trail_active \
		-Wl,--export=odg_match_over -Wl,--export=odg_winner_id -Wl,--export=odg_player_death_reason \
		-Wl,--export=odg_turret_count -Wl,--export=odg_player_owned_turrets -Wl,--export=odg_player_carrying_turret -Wl,--export=odg_player_carried_turret_ammo \
		-Wl,--export=odg_player_turret_action_available -Wl,--export=odg_ammo_crate_count -Wl,--export=odg_player_carrying_ammo_crate -Wl,--export=odg_player_carried_ammo -Wl,--export=odg_player_ammo_reserve \
		-Wl,--export=odg_chip_count -Wl,--export=odg_player_carrying_chip -Wl,--export=odg_player_chip_kind -Wl,--export=odg_player_hack_action_available -Wl,--export=odg_player_drop_action_available \
		-Wl,--export=odg_player_nearby_owned_turret_visible -Wl,--export=odg_player_nearby_owned_turret_ammo -Wl,--export=odg_player_nearby_owned_turret_max_ammo \
		-Wl,--export=odg_player_facing_x_q15 -Wl,--export=odg_player_facing_z_q15 -Wl,--export=odg_camera_dir_x_q15 -Wl,--export=odg_camera_dir_z_q15 -Wl,--export=odg_control_basis_x_q15 -Wl,--export=odg_control_basis_z_q15 \
		-Wl,--export=odg_control_heading_x_q15 -Wl,--export=odg_control_heading_z_q15 -Wl,--export=odg_control_local_x_q15 -Wl,--export=odg_control_local_z_q15 -Wl,--export=odg_control_strength_q15 \
		-Wl,--export=odg_leader_count -Wl,--export=odg_leader_score -Wl,--export=odg_leader_name_code \
		-Wl,--export=odg_leader_is_player -o $(BUILD)/odpar_territorial_domain.wasm
	cp $(BUILD)/odpar_territorial_domain.wasm app/web/odpar_territorial_domain.wasm

bundle: wasm
	python3 tools/make_bundle.py

test: $(BUILD)
	$(CC) -std=c11 -O2 -fno-builtin $(WARN) $(INCLUDES) $(ENGINE) tests/test_game.c -o $(BUILD)/test_game
	$(BUILD)/test_game
	$(MAKE) ffi-test

ffi-test: $(BUILD)
	$(CC) -std=c11 -O2 -fno-builtin $(WARN) $(INCLUDES) $(ENGINE) tests/test_ffi.c -o $(BUILD)/test_ffi
	$(BUILD)/test_ffi

soak: $(BUILD)
	$(CC) -std=c11 -O2 -fno-builtin $(WARN) $(INCLUDES) $(ENGINE) tests/soak_game.c -o $(BUILD)/soak_game
	$(BUILD)/soak_game

bench: $(BUILD)
	$(CC) -std=c11 -O3 -fno-builtin $(WARN) $(INCLUDES) $(ENGINE) tools/bench_render.c -o $(BUILD)/bench_render
	$(BUILD)/bench_render

asan: $(BUILD)
	$(CC) -std=c11 -O2 -g -fno-builtin -fsanitize=address,undefined -fno-omit-frame-pointer $(WARN) $(INCLUDES) $(ENGINE) tests/test_game.c -o $(BUILD)/test_game_asan
	ASAN_OPTIONS=$(SANITIZER_OPTIONS) $(BUILD)/test_game_asan
	$(CC) -std=c11 -O2 -g -fno-builtin -fsanitize=address,undefined -fno-omit-frame-pointer $(WARN) $(INCLUDES) $(ENGINE) tests/test_ffi.c -o $(BUILD)/test_ffi_asan
	ASAN_OPTIONS=$(SANITIZER_OPTIONS) $(BUILD)/test_ffi_asan
	$(CC) -std=c11 -O2 -g -fno-builtin -fsanitize=address,undefined -fno-omit-frame-pointer $(WARN) $(INCLUDES) $(ENGINE) tests/soak_game.c -o $(BUILD)/soak_game_asan
	ASAN_OPTIONS=$(SANITIZER_OPTIONS) $(BUILD)/soak_game_asan

clean:
	rm -rf $(BUILD) app/web/odpar_territorial_domain.wasm app/web/ODPAR_Territorial_Domain.html
