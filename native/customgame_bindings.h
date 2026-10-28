#pragma once
// CustomGame game bindings for Anymaker 0.1.27 / Steam build 26113663.
// Functions: python tools/bind.py --reference reference --signature "<signature>" --out <file>.hpp, run against the
// 0.1.27 SDK reference, one call per signature below.
// Globals: each anchor is a function whose code start is unique in game.gcl and whose relocation table holds the
// global; slot_offset = the anchor's code size + 8 * the global's dependency index (parse_gcl.py records).
// Experimental: located by code signature, call-cell route or typeinfo slot, not reviewed as an AnyAPI service.
// Regenerate and re-test after any game update.
#include "anymaker_sdk_runtime.hpp"
namespace customgame::bind {
using anymaker::func_desc;
using anymaker::global_desc;
// () frontend_ui._update_ui_new_game (frontend_ui, const f64, settings, main_menu.ui_data) | side: client
inline constexpr func_desc update_ui_new_game{"() frontend_ui._update_ui_new_game (frontend_ui, const f64, settings, main_menu.ui_data)","53 55 56 57 48 81 ec 98 0b 00 00 48 89 8c 24 10 0b 00 00 48 89 94 24 18 0b 00 00 4c 89 84 24 20",{"53 55 48 81 ec 98 00 00 00 48 89 4c 24 30 48 89 54 24 38 4c 89 44 24 40 4c 89 4c 24 48 8b 81 28", {1296u}, 1},"",0u,-1};
// () frontend_ui._start_new_game (frontend_ui, main_menu.ui_data, const e_gamemode) | side: client
inline constexpr func_desc start_new_game{"() frontend_ui._start_new_game (frontend_ui, main_menu.ui_data, const e_gamemode)","53 55 48 81 ec 88 00 00 00 48 89 4c 24 30 48 89 54 24 38 4c 89 44 24 40 48 8b 05 c1 00 00 00 48",{"53 55 56 57 48 81 ec 98 0b 00 00 48 89 8c 24 10 0b 00 00 48 89 94 24 18 0b 00 00 4c 89 84 24 20", {14392u}, 1},"",0u,-1};
// () server_scene._generate_new_game (server_scene) | side: server
inline constexpr func_desc generate_new_game{"() server_scene._generate_new_game (server_scene)","53 55 48 83 ec 78 48 89 4c 24 48 48 8b 1d fe 00 00 00 48 8d 44 24 50 48 8b 15 fa 00 00 00 48 89",{"", {}, 0},"53 55 48 83 ec 38 48 89 4c 24 20 48 8d 01 48 8b 15 db 06 00 00 48 89 4c 24 30 48 89 c1 ff 12 48",1784u,8};
// () server_scene.tick (server_scene, server) | side: server
inline constexpr func_desc server_scene_tick{"() server_scene.tick (server_scene, server)","53 55 56 57 48 81 ec a8 02 00 00 48 89 8c 24 f0 00 00 00 48 89 94 24 f8 00 00 00 48 8d 84 24 00",{"", {}, 0},"53 55 48 83 ec 38 48 89 4c 24 20 48 8d 01 48 8b 15 db 06 00 00 48 89 4c 24 30 48 89 c1 ff 12 48",1784u,13};
// () server_scene.tick_loading (server_scene, server) | side: server
inline constexpr func_desc server_scene_tick_loading{"() server_scene.tick_loading (server_scene, server)","53 55 48 81 ec e8 00 00 00 48 89 4c 24 58 48 89 54 24 60 48 8d 44 24 68 48 8b 1d 51 03 00 00 48",{"", {}, 0},"53 55 48 83 ec 38 48 89 4c 24 20 48 8d 01 48 8b 15 db 06 00 00 48 89 4c 24 30 48 89 c1 ff 12 48",1784u,11};
// () server_scene.set_loot_level (server_scene, const s32) | side: server
inline constexpr func_desc set_loot_level{"() server_scene.set_loot_level (server_scene, const s32)","53 55 48 81 ec a8 03 00 00 48 89 8c 24 00 01 00 00 48 89 94 24 08 01 00 00 48 8d 81 d8 14 00 00",{"", {}, 0},"53 55 48 83 ec 38 48 89 4c 24 20 48 8d 01 48 8b 15 db 06 00 00 48 89 4c 24 30 48 89 c1 ff 12 48",1784u,26};
// () replication.master.property_type_simple<bool>.set (replication.master.property_type_simple<bool>, const bool) | side: shared
inline constexpr func_desc property_bool_set{"() replication.master.property_type_simple<bool>.set (replication.master.property_type_simple<bool>, const bool)","48 83 ec 48 48 89 4c 24 20 48 89 54 24 28 8a 02 3a 41 18 0f 95 c0 3c 00 0f 84 3d 00 00 00 8a 02",{"", {}, 0},"48 83 ec 38 48 89 4c 24 20 48 8d 01 48 8b 15 35 00 00 00 48 89 4c 24 28 48 89 c1 ff 12 48 8b 05 2c 00 00 00 48 8b 4c 24 28 48 89 01 8a 05 0e 00 00 00 88 41 18 48 83 c4 38 c3 00 00 00 00 00 00",80u,5};
// () replication.master.property_type_simple<f64>.set (replication.master.property_type_simple<f64>, const f64) | side: shared
inline constexpr func_desc property_f64_set{"() replication.master.property_type_simple<f64>.set (replication.master.property_type_simple<f64>, const f64)","48 83 ec 48 48 89 4c 24 20 48 89 54 24 28 f2 0f 10 02 66 0f 2e 41 18 9f f6 c4 44 0f 8b 07 00 00",{"", {}, 0},"48 83 ec 38 48 89 4c 24 20 48 8d 01 48 8b 15 35 00 00 00 48 89 4c 24 28 48 89 c1 ff 12 48 8b 05 2c 00 00 00 48 8b 4c 24 28 48 89 01 f2 0f 10 05 0c 00 00 00 f2 0f 11 41 18",80u,5};
// () client_scene.tick (client_scene, client, const f64, const f64) | side: client
inline constexpr func_desc client_scene_tick{"() client_scene.tick (client_scene, client, const f64, const f64)","53 55 56 57 41 54 48 81 ec d0 06 00 00 48 89 8c 24 20 02 00 00 48 89 94 24 28 02 00 00 4c 89 84",{"", {}, 0},"53 48 83 ec 30 48 89 4c 24 20 48 8d 01 48 8b 15 cc 08 00 00 48 89 4c 24 28 48 89 c1 ff 12 48 8b",2280u,5};
// () client_scene.tick_loading (client_scene, client, const f64, const bool) | side: client
inline constexpr func_desc client_scene_tick_loading{"() client_scene.tick_loading (client_scene, client, const f64, const bool)","53 55 56 48 81 ec 60 01 00 00 48 89 8c 24 98 00 00 00 48 89 94 24 a0 00 00 00 4c 89 84 24 a8 00 00 00 4c 89 8c 24 b0 00 00 00 41",{"", {}, 0},"53 48 83 ec 30 48 89 4c 24 20 48 8d 01 48 8b 15 cc 08 00 00 48 89 4c 24 28 48 89 c1 ff 12 48 8b",2280u,4};
// (weather_data) weather_manager.get_weather_data (const weather_manager, const vec3, const f64) | side: shared
inline constexpr func_desc get_weather_data{"(weather_data) weather_manager.get_weather_data (const weather_manager, const vec3, const f64)","53 55 56 48 81 ec a0 01 00 00 48 89 8c 24 90 00 00 00 48 89 94 24 98 00 00 00 4c 89 84 24 a0 00",{"53 55 56 57 48 81 ec d8 00 00 00 48 8b ac 24 20 01 00 00 48 89 4c 24 48 48 89 54 24 50 4c 89 44", {424u}, 1},"",0u,-1};
// () client_scene.weather_particles.create (client_scene.weather_particles) | side: client
inline constexpr func_desc weather_particles_create{"() client_scene.weather_particles.create (client_scene.weather_particles)","53 55 48 81 ec c8 0a 00 00 48 89 8c 24 58 09 00 00 f2 0f 10 05 9f 26 00 00 f2 0f 11 84 24 60 09",{"53 55 56 57 41 54 41 55 41 56 41 57 48 81 ec d8 03 00 00 48 89 8c 24 50 03 00 00 48 8d 81 a0 29", {5456u}, 1},"",0u,-1};

// () server_scene.creature_manager._tick_spawn (server_scene.creature_manager, server_scene, const vector<vec3>, const f64) | side: server
inline constexpr func_desc creature_tick_spawn{"() server_scene.creature_manager._tick_spawn (server_scene.creature_manager, server_scene, const vector<vec3>, const f64)","53 55 56 57 41 54 41 55 41 56 41 57 48 81 ec a8 05 00 00 48 89 d5 48 89 8c 24 58 03 00 00 48 89",{"53 55 56 48 81 ec 80 00 00 00 48 89 4c 24 20 48 89 54 24 28 4c 89 44 24 30 48 8d 01 48 8d 1a 48", {544u}, 1},"",0u,-1};
// () server_scene.environment_tile.populate_actor_spawn_zones (server_scene.environment_tile, server_scene) | side: server
inline constexpr func_desc populate_actor_spawn_zones{"() server_scene.environment_tile.populate_actor_spawn_zones (server_scene.environment_tile, server_scene)","53 55 56 48 81 ec 50 04 00 00 48 89 8c 24 f8 02 00 00 48 89 94 24 00 03 00 00 48 8d 84 24 08 03",{"53 55 56 57 41 54 41 55 41 56 48 81 ec 70 04 00 00 48 89 8c 24 c8 03 00 00 48 89 94 24 d0 03 00", {8344u}, 1},"",0u,-1};
// () server_scene.vehicle.spawn_sleeping_actors (server_scene.vehicle, server_scene) | side: server
inline constexpr func_desc spawn_sleeping_actors{"() server_scene.vehicle.spawn_sleeping_actors (server_scene.vehicle, server_scene)","53 55 56 57 48 81 ec 18 07 00 00 48 89 8c 24 28 05 00 00 48 89 94 24 30 05 00 00 48 8d 84 24 38",{"", {}, 0},"53 48 83 ec 30 48 89 4c 24 20 48 8d 01 48 8b 15 b4 04 00 00 48 89 4c 24 28 48 89 c1 ff 12 48 8b",1232u,85};
// (bool) server_scene.zombie_horde_controller.is_destroy_horde (server_scene.zombie_horde_controller, const server_scene) | side: server
inline constexpr func_desc is_destroy_horde{"(bool) server_scene.zombie_horde_controller.is_destroy_horde (server_scene.zombie_horde_controller, const server_scene)","53 55 56 48 83 ec 60 48 89 4c 24 20 48 89 54 24 28 4c 89 44 24 30 48 8d 44 24 38 49 8d 98 88 00",{"53 48 81 ec b0 00 00 00 48 89 4c 24 50 48 89 54 24 58 48 8b 44 24 28 48 89 44 24 60 4c 8d 4c 24 60 48 8d 44 24 68 48 89 84 24 98 00 00 00 48 8d 01 48 8b 1d a8", {504u}, 1},"",0u,-1};
// () server_scene.zombie_horde_manager.spawn_horde (server_scene.zombie_horde_manager, server_scene) | side: server
inline constexpr func_desc spawn_horde{"() server_scene.zombie_horde_manager.spawn_horde (server_scene.zombie_horde_manager, server_scene)","53 55 56 48 81 ec 40 02 00 00 48 89 8c 24 70 01 00 00 48 89 94 24 78 01 00 00 48 8d 84 24 80 01",{"53 48 81 ec b0 00 00 00 48 89 4c 24 50 48 89 54 24 58 48 8b 44 24 28 48 89 44 24 60 4c 8d 4c 24 60 48 8d 44 24 68 48 89 84 24 98 00 00 00 48 8d 01 48 8b 1d a8", {552u}, 1},"",0u,-1};

// (bool) server_scene.environment_tile_task.integrate (server_scene.environment_tile_task, server_scene, server_scene.environment_tile_container) | side: server
inline constexpr func_desc tile_task_integrate{"(bool) server_scene.environment_tile_task.integrate (server_scene.environment_tile_task, server_scene, server_scene.environment_tile_container)","53 55 56 57 41 54 41 55 41 56 48 81 ec 70 04 00 00 48 89 8c 24 c8 03 00 00 48 89 94 24 d0 03 00",{"53 55 56 48 81 ec 50 01 00 00 48 89 8c 24 c8 00 00 00 48 89 94 24 d0 00 00 00 48 83 b9 a8 02 00", {1168u}, 1},"",0u,-1};
// () server_scene.dungeon_manager.generate (server_scene.dungeon_manager, server_scene) | side: server
inline constexpr func_desc dungeon_generate{"() server_scene.dungeon_manager.generate (server_scene.dungeon_manager, server_scene)","53 55 56 57 41 54 41 55 41 56 48 81 ec 00 05 00 00 48 89 8c 24 b0 03 00 00 48 89 94 24 b8 03 00",{"", {}, 0},"48 83 ec 38 48 89 4c 24 20 48 8d 01 48 8b 15 4d 00 00 00 48 89 4c 24 28 48 89 c1 ff 12 48 8b 05 44 00 00 00 48 8b 4c 24 28 48 89 01 48 8d 41 70",104u,36};
// () server_scene.dungeon.tile.populate_creatures (server_scene.dungeon.tile, server_scene, const mat34, const e_dungeon_actor_variety) | side: server
inline constexpr func_desc dungeon_populate_creatures{"() server_scene.dungeon.tile.populate_creatures (server_scene.dungeon.tile, server_scene, const mat34, const e_dungeon_actor_variety)","53 55 56 57 41 54 48 81 ec 00 06 00 00 48 89 8c 24 f0 03 00 00 48 89 94 24 f8 03 00 00 4c 89 84",{"", {}, 0},"53 48 83 ec 30 48 89 4c 24 20 48 8d 01 48 8b 15 94 00 00 00 48 89 4c 24 28 48 89 c1 ff 12 48 8b 05 8b 00 00 00 48 8b 4c 24 28 48 89 01 48 8d 81 40",176u,5};
// () server_scene.dungeon.tile.generate_loot (server_scene.dungeon.tile, server_scene, const mat34, const s32) | side: server
inline constexpr func_desc dungeon_generate_loot{"() server_scene.dungeon.tile.generate_loot (server_scene.dungeon.tile, server_scene, const mat34, const s32)","53 55 56 57 41 54 41 55 41 56 48 81 ec e0 05 00 00 48 89 8c 24 b8 03 00 00 48 89 94 24 c0 03 00",{"", {}, 0},"53 48 83 ec 30 48 89 4c 24 20 48 8d 01 48 8b 15 94 00 00 00 48 89 4c 24 28 48 89 c1 ff 12 48 8b 05 8b 00 00 00 48 8b 4c 24 28 48 89 01 48 8d 81 40",176u,6};

// g_day_night_cycle_ticks (anchor: () g_day_night_cycle_ticks.$ctor ())
inline constexpr global_desc g_day_ticks{"g_day_night_cycle_ticks","48 8b 0d 21 00 00 00 8b 05 13 00 00 00 0f af 05 0c 00 00 00 0f af 05 05 00 00 00 89 01 c3 00 00",40u};
// g_time_start_offset (anchor: () g_time_start_offset.$ctor ())
inline constexpr global_desc g_time_start_offset{"g_time_start_offset","48 8b 0d 21 00 00 00 8b 05 13 00 00 00 0f af 05 0c 00 00 00 0f af 05 09 00 00 00 89 01 c3 00 00 3c 00 00 00 14",40u};
// g_sandbox_cycle_factor (anchor: (f64) client_scene.time_of_day.get_cycle_factor (client_scene.time_of_day, const e_gamemode, const s32))
inline constexpr global_desc g_sandbox_cycle_factor{"g_sandbox_cycle_factor","53 55 56 48 81 ec 80 00 00 00 48 89 4c 24 40 48 89 54 24 48 4c 89 44 24 50 4c 89 4c 24 58 48 8d 44 24 60 48 8d 5a 08",360u};
// g_server_delta_time (anchor: () server_scene.actor_creature_large_crusher._tick_abilities (server_scene.actor_creature_large_crusher, serve)
inline constexpr global_desc g_server_delta_time{"g_server_delta_time","53 55 48 83 ec 48 48 89 4c 24 20 48 89 54 24 28 f2 0f 10 81 18 03 00 00 f2 0f 11 44 24 30 48 8b 05 ab 00 00 00 f2 0f 10 81 18 03 00 00 f2 0f 58 00 f2 0f 11 81 18 03 00 00 8b 81 10 03 00 00 3b 05 7b 00 00 00 0f 94 c0 3c 00 0f 84 39 00 00 00 48 8d 01 48 8d 12 4c 8d 44 24 30 48 8b 19 48 8b 9b 80 00 00 00 48 8b 2d 6c 00 00 00 48 01 eb 48 8b 1b 48 8b 1b 48 89 4c 24 38 48 89 c1 ff d3 48 8b 4c 24 38 e9 00 00 00 00 f2 0f 10 81 18 03 00 00 66 0f 2f 05 2f 00 00 00 0f 93 c0 3c 00 0f 84 11 00 00 00 8b 05 1e 00 00 00 89 81 10 03 00 00 e9 00 00 00 00 48 83 c4 48 5d 5b c3 00 00 00 00 01",208u};
// server.settings.g_max_player_count (anchor: () server.settings.update_settings (server.settings, server, const s32, const bool, const string))
inline constexpr global_desc g_max_player_count{"server.settings.g_max_player_count","53 55 56 57 48 81 ec a8 02 00 00 48 8b ac 24 f0 02 00 00 48 89 8c 24 d8 01 00 00 48 89 94 24 e0",2064u};
// server_scene.actor_character.g_hunger_max_seconds (anchor: () server_scene.actor_character.tick_conditions (server_scene.actor_character, server_scene))
inline constexpr global_desc g_hunger_seconds{"server_scene.actor_character.g_hunger_max_seconds","48 8b 05 31 00 00 00 f2 0f 10 05 19 00 00 00 f2 0f 59 05 11 00 00 00 f2 0f 59 05 11 00 00 00 f2 0f 11 00 c3 00 00 00 00 00 00 00 00 00 00 4e 40 00 00 00 00 00 00 00",56u};
// server_scene.actor_character.g_thirst_max_seconds (anchor: () server_scene.actor_character.tick_conditions (server_scene.actor_character, server_scene))
inline constexpr global_desc g_thirst_seconds{"server_scene.actor_character.g_thirst_max_seconds","48 8b 05 31 00 00 00 f2 0f 10 05 19 00 00 00 f2 0f 59 05 11 00 00 00 f2 0f 59 05 11 00 00 00 f2 0f 11 00 c3 00 00 00 00 00 00 00 00 00 00 4e 40 00 00 00 00 00 00 f0",56u};
// server_scene.actor_character.g_stamina_drain_seconds (anchor: () server_scene.actor_character.g_stamina_drain_seconds.$ctor ())
inline constexpr global_desc g_stamina_drain_seconds{"server_scene.actor_character.g_stamina_drain_seconds","48 8b 05 19 00 00 00 f2 0f 10 05 09 00 00 00 f2 0f 11 00 c3 00 00 00 00 00 00 00 00 00 80 46 40",32u};
// server_scene.actor_character.g_stamina_recover_seconds (anchor: () server_scene.actor_character.tick_stamina (server_scene.actor_character))
inline constexpr global_desc g_stamina_recover_seconds{"server_scene.actor_character.g_stamina_recover_seconds","53 55 48 83 ec 68 48 89 4c 24 20 48 8d 44 24 30 48 8d 91 68 0e 00 00 48 8b 99 68 0e 00 00 48 8b",1008u};
// server_scene.actor_character.g_oxygen_deplete_seconds (anchor: () server_scene.actor_character.tick_oxygen (server_scene.actor_character, server_scene))
inline constexpr global_desc g_oxygen_deplete_seconds{"server_scene.actor_character.g_oxygen_deplete_seconds","53 55 56 48 81 ec 40 01 00 00 48 89 8c 24 a0 00 00 00 48 89 94 24 a8 00 00 00 48 8d 84 24 b0 00 00 00 48 8d 19",912u};
// server_scene.actor_character.g_oxygen_recover_seconds (anchor: () server_scene.actor_character.tick_oxygen (server_scene.actor_character, server_scene))
inline constexpr global_desc g_oxygen_recover_seconds{"server_scene.actor_character.g_oxygen_recover_seconds","53 55 56 48 81 ec 40 01 00 00 48 89 8c 24 a0 00 00 00 48 89 94 24 a8 00 00 00 48 8d 84 24 b0 00 00 00 48 8d 19",928u};
// server_scene.actor_character.g_oxygen_drown_seconds (anchor: () server_scene.actor_character.tick_conditions (server_scene.actor_character, server_scene))
inline constexpr global_desc g_oxygen_drown_seconds{"server_scene.actor_character.g_oxygen_drown_seconds","53 55 56 48 81 ec 60 06 00 00 48 89 8c 24 90 04 00 00 48 89 94 24 98 04 00 00 8b 81 10 0d 00 00",8688u};
// server_scene.inventory.g_clothing_rain_soak_seconds (anchor: (f64) server_scene.inventory.update_clothing_wetness (server_scene.inventory, const f64, const f64, const f64))
inline constexpr global_desc g_rain_soak_seconds{"server_scene.inventory.g_clothing_rain_soak_seconds","53 55 56 57 48 81 ec 28 01 00 00 48 8b ac 24 70 01 00 00 48 89 8c 24 88 00 00 00 48 89 94 24 90",1592u};
// server_scene.inventory.g_clothing_swim_soak_seconds (anchor: () server_scene.inventory.g_clothing_swim_soak_seconds.$ctor ())
inline constexpr global_desc g_swim_soak_seconds{"server_scene.inventory.g_clothing_swim_soak_seconds","48 8b 05 19 00 00 00 f2 0f 10 05 09 00 00 00 f2 0f 11 00 c3 00 00 00 00 00 00 00 00 00 00 2e 40",32u};
// server_scene.inventory.g_clothing_dry_seconds_warm (anchor: () server_scene.inventory.g_clothing_dry_seconds_warm.$ctor ())
inline constexpr global_desc g_dry_seconds_warm{"server_scene.inventory.g_clothing_dry_seconds_warm","48 8b 05 19 00 00 00 f2 0f 10 05 09 00 00 00 f2 0f 11 00 c3 00 00 00 00 00 00 00 00 00 c0 82 40",32u};
// server_scene.inventory.g_clothing_dry_seconds_cold (anchor: () server_scene.inventory.g_clothing_dry_seconds_cold.$ctor ())
inline constexpr global_desc g_dry_seconds_cold{"server_scene.inventory.g_clothing_dry_seconds_cold","48 8b 05 19 00 00 00 f2 0f 10 05 09 00 00 00 f2 0f 11 00 c3 00 00 00 00 00 00 00 00 00 c0 92 40",32u};
// server_scene.actor.g_max_corpse_time (anchor: (bool) server_scene.actor.tick_is_despawn (server_scene.actor, server_scene))
inline constexpr global_desc g_corpse_seconds{"server_scene.actor.g_max_corpse_time","48 8b 05 29 00 00 00 f2 0f 10 05 11 00 00 00 f2 0f 59 05 11 00 00 00 f2 0f 11 00 c3 00 00 00 00 00 00 00 00 00 00 4e 40 00 00 00 00 00 00 20",48u};
// g_server_peer_lost_body_time (anchor: () server_scene.peer._tick_body_recovery (server_scene.peer, server_scene))
inline constexpr global_desc g_lost_body_seconds{"g_server_peer_lost_body_time","53 55 56 48 81 ec a0 00 00 00 48 89 4c 24 50 48 89 54 24 58 48 8d 44 24 68 48 8d 99 50 19 00 00",1264u};
// g_temperature_degrees_min (anchor: () g_temperature_degrees_min.$ctor ())
inline constexpr global_desc g_temperature_min{"g_temperature_degrees_min","48 8b 05 19 00 00 00 f2 0f 10 05 09 00 00 00 f2 0f 11 00 c3 00 00 00 00 00 00 00 00 00 00 14 c0",32u};
// g_temperature_degrees_max (anchor: () g_temperature_degrees_max.$ctor ())
inline constexpr global_desc g_temperature_max{"g_temperature_degrees_max","48 8b 05 19 00 00 00 f2 0f 10 05 09 00 00 00 f2 0f 11 00 c3 00 00 00 00 00 00 00 00 00 00 3c 40",32u};
// g_is_debug_disable_creature_spawning (anchor: () server_scene.creature_manager.tick (server_scene.creature_manager, server_scene, const vector<vec3>))
inline constexpr global_desc g_disable_creatures{"g_is_debug_disable_creature_spawning","53 55 56 48 81 ec 80 00 00 00 48 89 4c 24 20 48 89 54 24 28 4c 89 44 24 30 48 8d 01 48 8d 1a 48",504u};
// g_is_debug_disable_zombie_spawning (anchor: (bool) server_scene.zombie_horde_controller.is_destroy_horde (server_scene.zombie_horde_controller, const serv)
inline constexpr global_desc g_disable_zombies{"g_is_debug_disable_zombie_spawning","53 55 56 48 83 ec 60 48 89 4c 24 20 48 89 54 24 28 4c 89 44 24 30 48 8d 44 24 38 49 8d 98 88 00",296u};
// g_zombie_horde_size (anchor: () server_scene.zombie_horde_controller.create (server_scene.zombie_horde_controller, server_scene, const vec3)
inline constexpr global_desc g_horde_size{"g_zombie_horde_size","53 55 56 57 48 81 ec 38 02 00 00 48 89 8c 24 e0 00 00 00 48 89 94 24 e8 00 00 00 4c 89 84 24 f0",1136u};
// g_max_spawned_actors (anchor: () server_scene.zombie_horde_manager.spawn_horde (server_scene.zombie_horde_manager, server_scene))
inline constexpr global_desc g_max_spawned_actors{"g_max_spawned_actors","53 55 56 48 81 ec 40 02 00 00 48 89 8c 24 70 01 00 00 48 89 94 24 78 01 00 00 48 8d 84 24 80 01",2408u};
// server_scene.actor_character.ai_utility.g_visible_dist_max (anchor: () server_scene.actor_character.ai_state.idle.tick (server_scene.actor_character.ai_state.idle, server_scene.a)
inline constexpr global_desc g_survivor_sight{"server_scene.actor_character.ai_utility.g_visible_dist_max","53 55 56 57 48 81 ec e8 01 00 00 48 8b b4 24 30 02 00 00 48 8b ac 24 38 02 00 00 48 89 8c 24 f0",1832u};
// server_scene.actor_character.ai_utility.g_audible_dist_max (anchor: () server_scene.actor_character.ai_state.idle.tick (server_scene.actor_character.ai_state.idle, server_scene.a)
inline constexpr global_desc g_survivor_hearing{"server_scene.actor_character.ai_utility.g_audible_dist_max","53 55 56 57 48 81 ec e8 01 00 00 48 8b b4 24 30 02 00 00 48 8b ac 24 38 02 00 00 48 89 8c 24 f0",1840u};
// server_scene.actor_creature.ai_utility.g_visible_dist_max (anchor: () server_scene.actor_zombie.ai_state.zombie_acid_flee.begin (server_scene.actor_zombie.ai_state.zombie_acid_f)
inline constexpr global_desc g_creature_sight{"server_scene.actor_creature.ai_utility.g_visible_dist_max","53 55 56 57 48 81 ec 18 01 00 00 48 8b 84 24 60 01 00 00 48 89 4c 24 70 48 89 54 24 78 4c 89 84",592u};
// server_scene.actor_creature.ai_utility.g_audible_dist_max (anchor: () server_scene.actor_zombie.ai_state.zombie_acid_flee.begin (server_scene.actor_zombie.ai_state.zombie_acid_f)
inline constexpr global_desc g_creature_hearing{"server_scene.actor_creature.ai_utility.g_audible_dist_max","53 55 56 57 48 81 ec 18 01 00 00 48 8b 84 24 60 01 00 00 48 89 4c 24 70 48 89 54 24 78 4c 89 84",600u};
// server_scene.actor_zombie.ai_utility.g_visible_dist_max (anchor: (ptr<server_scene.actor>) server_scene.actor_zombie.ai_utility.find_actor_visible (server_scene.actor_zombie, )
inline constexpr global_desc g_zombie_sight{"server_scene.actor_zombie.ai_utility.g_visible_dist_max","53 55 56 57 48 81 ec 88 01 00 00 48 8b ac 24 d0 01 00 00 48 89 8c 24 c8 00 00 00 48 89 94 24 d0 00 00 00 4c 89 84 24 d8 00 00 00 4c 89 8c 24 e0 00 00 00 48 89 ac 24 e8 00 00 00 8b 41 08 2b 05 44",1200u};
// server_scene.actor_zombie.ai_utility.g_audible_dist_max (anchor: (ptr<server_scene.actor>) server_scene.actor_zombie.ai_utility.find_actor_visible (server_scene.actor_zombie, )
inline constexpr global_desc g_zombie_hearing{"server_scene.actor_zombie.ai_utility.g_audible_dist_max","53 55 56 57 48 81 ec 88 01 00 00 48 8b ac 24 d0 01 00 00 48 89 8c 24 c8 00 00 00 48 89 94 24 d0 00 00 00 4c 89 84 24 d8 00 00 00 4c 89 8c 24 e0 00 00 00 48 89 ac 24 e8 00 00 00 8b 41 08 2b 05 44",1208u};
// g_spawn_creature_exclusion_range (anchor: () g_spawn_creature_exclusion_range.$ctor ())
// server_scene.actor_character.ai_utility.g_attack_range_gun_max (anchor: () server_scene.actor_character.ai_state.attack_vehicle.tick (...))
inline constexpr global_desc g_survivor_gun_range{"server_scene.actor_character.ai_utility.g_attack_range_gun_max","53 55 56 57 41 54 48 81 ec 50 07 00 00 48 8b bc 24 a0 07 00 00 48 8b b4 24 a8 07 00 00 48 89 8c",7336u};
// server_scene.actor_character.ai_utility.g_attack_range_gun_min (same anchor)
inline constexpr global_desc g_survivor_gun_min{"server_scene.actor_character.ai_utility.g_attack_range_gun_min","53 55 56 57 41 54 48 81 ec 50 07 00 00 48 8b bc 24 a0 07 00 00 48 8b b4 24 a8 07 00 00 48 89 8c",7328u};
inline constexpr global_desc g_spawn_exclusion{"g_spawn_creature_exclusion_range","48 8b 05 19 00 00 00 f2 0f 10 05 09 00 00 00 f2 0f 11 00 c3 00 00 00 00 00 00 00 00 00 00 69 40",32u};
// g_loot_random_loot_capacity (anchor: () server_scene.dungeon.tile.generate_loot (server_scene.dungeon.tile, server_scene, const mat34, const s32))
inline constexpr global_desc g_loot_capacity{"g_loot_random_loot_capacity","53 55 56 57 41 54 41 55 41 56 48 81 ec e0 05 00 00 48 89 8c 24 b8 03 00 00 48 89 94 24 c0 03 00",7352u};
// g_constraint_break_force (anchor: () g_constraint_break_force.$ctor ())
inline constexpr global_desc g_joint_break_force{"g_constraint_break_force","48 8b 05 19 00 00 00 f2 0f 10 05 09 00 00 00 f2 0f 11 00 c3 00 00 00 00 00 00 00 00 00 6a f8 40",32u};
// g_weather_particle_count_rain (anchor: () client_scene.weather_particles.render (client_scene.weather_particles, scene_renderer))
inline constexpr global_desc g_rain_particles{"g_weather_particle_count_rain","53 55 56 48 81 ec 80 00 00 00 48 89 4c 24 30 48 89 54 24 38 f2 0f 10 05 c4 01 00 00 f2 0f 5c 42",520u};
// g_weather_particle_count_snow (anchor: () client_scene.weather_particles.render (client_scene.weather_particles, scene_renderer))
inline constexpr global_desc g_snow_particles{"g_weather_particle_count_snow","53 55 56 48 81 ec 80 00 00 00 48 89 4c 24 30 48 89 54 24 38 f2 0f 10 05 c4 01 00 00 f2 0f 5c 42",528u};
} // namespace customgame::bind
