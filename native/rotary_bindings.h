#pragma once
// Rotary game bindings, generated for Anymaker 0.2.0 / Steam build 26328453 with
//   python tools/bind.py --reference . --signature "<signature>" --out <file>.hpp
// run from the regenerated SDK reference (rebind_mod_bindings.py checks them per build), one --signature per binding below. The definition-file five and
// the world transform are the same as Turbo's; the client engine tick and the voice update are the same as
// EngineSound's (both mods hook them: cell hooks chain, each calls the one below it). Experimental: located by
// code signature, call-cell route or typeinfo slot, not reviewed as an AnyAPI service. Regenerate and re-test
// after any game update.
#include "anymaker_sdk_runtime.hpp"
namespace rotary::bind {
using anymaker::func_desc;
// () ctor (vehicle_component_definition_file) | side: shared
inline constexpr func_desc definition_file_ctor{"() ctor (vehicle_component_definition_file)","",{"53 48 81 ec e0 00 00 00 48 89 4c 24 60 48 89 54 24 68 48 8d 44 24 70 48 8b 1d 8a 01 00 00 48 89", {424u}, 1},"",0u,-1};
// () dtor (vehicle_component_definition_file) | side: shared
inline constexpr func_desc definition_file_dtor{"() dtor (vehicle_component_definition_file)","",{"53 48 81 ec e0 00 00 00 48 89 4c 24 60 48 89 54 24 68 48 8d 44 24 70 48 8b 1d 8a 01 00 00 48 89", {496u}, 1},"",0u,-1};
// () vehicle_component_definition_container._add_definitions (vehicle_component_definition_container, const vehicle_component_definition_file) | side: shared
inline constexpr func_desc definitions_add_definitions{"() vehicle_component_definition_container._add_definitions (vehicle_component_definition_container, const vehicle_component_definition_file)","",{"53 55 56 57 41 54 41 55 41 56 41 57 48 81 ec d8 03 00 00 48 89 8c 24 50 03 00 00 48 8d 81 c0 29", {5184u,248u}, 2},"",0u,-1};
// (bool) vehicle_component_definition_file.load (vehicle_component_definition_file, const file.path) | side: shared
inline constexpr func_desc definition_file_load{"(bool) vehicle_component_definition_file.load (vehicle_component_definition_file, const file.path)","",{"53 55 56 57 41 54 41 55 41 56 41 57 48 81 ec d8 03 00 00 48 89 8c 24 50 03 00 00 48 8d 81 c0 29", {5184u,240u}, 2},"",0u,-1};
// (ptr<const vehicle_component_definition>) vehicle_component_definition_container.get_definition_by_id_const (const vehicle_component_definition_container, const string) | side: shared
inline constexpr func_desc definitions_get_by_id{"(ptr<const vehicle_component_definition>) vehicle_component_definition_container.get_definition_by_id_const (const vehicle_component_definition_container, const string)","",{"53 48 81 ec a0 00 00 00 48 89 4c 24 50 48 89 54 24 58 48 8d 41 28 48 8b 1d 2b 01 00 00 48 89 4c", {368u}, 1},"",0u,-1};
// () server_scene.vehicle_component.engine.create (server_scene.vehicle_component.engine, server_scene.vehicle) | side: server
inline constexpr func_desc engine_create{"() server_scene.vehicle_component.engine.create (server_scene.vehicle_component.engine, server_scene.vehicle)","53 55 56 48 81 ec 80 00 00 00 48 89 4c 24 30 48 89 54 24 38 48 8d 01 48 8d 1a 48 8b 2d 7f 01 00 00 48 89 4c 24 48",{"", {}, 0},"53 48 83 ec 30 48 89 4c 24 20 48 8d 01 48 8b 15 c4 02 00 00 48 89 4c 24 28 48 89 c1 ff 12 48 8b 05 bb 02 00 00 48 8b 4c 24 28 48 89 01 48 8d 81 e0 01 00 00 48 8d",736u,10};
// () client_scene.vehicle_component.engine.tick (client_scene.vehicle_component.engine, client, client_scene.vehicle, client_scene, const f64) | side: client
inline constexpr func_desc client_engine_tick{"() client_scene.vehicle_component.engine.tick (client_scene.vehicle_component.engine, client, client_scene.vehicle, client_scene, const f64)","53 55 56 57 48 81 ec c8 03 00 00 48 8b bc 24 10 04 00 00 48 89 8c 24 d0 01 00 00 48 89 94 24 d8",{"", {}, 0},"53 48 83 ec 30 48 89 4c 24 20 48 8d 01 48 8b 15 c4 01 00 00 48 89 4c 24 28 48 89 c1 ff 12 48 8b",480u,8};
// () audio_effect_single.update_play_volume_speed (audio_effect_single, const f64, const f64, const e_audio_effect, const e_audio_group, const vec3) | side: shared
inline constexpr func_desc effect_update_play_volume_speed{"() audio_effect_single.update_play_volume_speed (audio_effect_single, const f64, const f64, const e_audio_effect, const e_audio_group, const vec3)","53 55 56 57 48 81 ec 18 01 00 00 48 8b ac 24 60 01 00 00 48 8b 9c 24 68 01 00 00 48 89 4c 24 78",{"53 55 48 83 ec 78 48 89 4c 24 30 48 89 54 24 38 4c 89 44 24 40 4c 89 4c 24 48 48 8d 01 48 8d 12", {352u}, 1},"",0u,-1};
// (mat34) client_scene.vehicle_component.get_world_transform (const client_scene.vehicle_component) | side: client
inline constexpr func_desc client_get_world_transform{"(mat34) client_scene.vehicle_component.get_world_transform (const client_scene.vehicle_component)","53 55 48 81 ec 48 01 00 00 48 89 4c 24 50 48 89 54 24 58 48 8b 9a c8 00 00 00 48 8d 44 24 60 48",{"", {}, 0},"53 48 83 ec 40 48 89 4c 24 20 48 8d 01 48 8b 15 14 01 00 00 48 89 4c 24 30 48 89 c1 ff 12 48 8b",304u,37};
} // namespace rotary::bind
