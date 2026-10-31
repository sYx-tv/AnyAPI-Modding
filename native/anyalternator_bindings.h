#pragma once
// AnyAlternator game bindings, generated for Anymaker 0.2.0 / Steam build 26328453 with
//   python tools/bind.py --reference <reference-0.2.0> --signature "<signature>" --out <file>.hpp
// run from sdk/experimental, one --signature per binding below. The definition-file five are the same as GpsPart's
// and AnyBattery's; the voice update is EngineSound's; the server tick length is AnyEngineParts'. Experimental:
// located by code signature, call-cell route or typeinfo slot, not reviewed as an AnyAPI service. Regenerate and
// re-test after any game update.
#include "anymaker_sdk_runtime.hpp"
namespace anyalternator::bind {
using anymaker::func_desc;
using anymaker::global_desc;
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
// () server_scene.vehicle_component.alternator.tick (server_scene.vehicle_component.alternator, server_scene.vehicle, server_scene) | server, method slot 20: makes energy from the shaft's rps and brakes it
inline constexpr func_desc alternator_tick{"() server_scene.vehicle_component.alternator.tick (server_scene.vehicle_component.alternator, server_scene.vehicle, server_scene)","53 55 48 81 ec d8 00 00 00 48 89 4c 24 48 48 89 54 24 50 4c 89 44 24 58 48 8d 44 24 68 48 8d 91",{"", {}, 0},"53 48 83 ec 30 48 89 4c 24 20 48 8d 01 48 8b 15 fc 00 00 00 48 89 4c 24 28 48 89 c1 ff 12 48 8b 05 f3 00 00 00 48 8b 4c 24 28 48 89 01 48 8d 81",280u,20};
// (ptr<f64>) server_scene.vehicle_component.alternator.get_data_f64 (server_scene.vehicle_component.alternator, const string) | server, method slot 59: rps, power, energy_capacity, energy_store
inline constexpr func_desc alternator_get_data_f64{"(ptr<f64>) server_scene.vehicle_component.alternator.get_data_f64 (server_scene.vehicle_component.alternator, const string)","53 55 48 81 ec 38 01 00 00 48 89 8c 24 d8 00 00 00 48 89 94 24 e0 00 00 00 4c 89 84 24 e8 00 00 00 48 8d 84 24 f0 00 00 00 48 8d 1d 78 03 00 00 48 8b 2d 99",{"", {}, 0},"53 48 83 ec 30 48 89 4c 24 20 48 8d 01 48 8b 15 fc 00 00 00 48 89 4c 24 28 48 89 c1 ff 12 48 8b 05 f3 00 00 00 48 8b 4c 24 28 48 89 01 48 8d 81",280u,60};
// () client_scene.vehicle_component.alternator.tick (client_scene.vehicle_component.alternator, client, client_scene.vehicle, client_scene, const f64) | client, method slot 8: the whine (m_audio_operate)
inline constexpr func_desc client_alternator_tick{"() client_scene.vehicle_component.alternator.tick (client_scene.vehicle_component.alternator, client, client_scene.vehicle, client_scene, const f64)","53 55 48 81 ec f8 00 00 00 48 8b 84 24 30 01 00 00 48 89 4c 24 30 48 89 54 24 38 4c 89 44 24 40",{"", {}, 0},"53 48 83 ec 30 48 89 4c 24 20 48 8d 01 48 8b 15 84 00 00 00 48 89 4c 24 28 48 89 c1 ff 12 48 8b 05 7b 00 00 00 48 8b 4c 24 28 48 89 01 48 8d 81 e0 00 00 00 48 8d 15 4d 00 00 00 4c 8d 05 4e 00",160u,8};
// () audio_effect_single.update_play_volume_speed (audio_effect_single, const f64, const f64, const e_audio_effect, const e_audio_group, const vec3) | side: shared
inline constexpr func_desc effect_update_play_volume_speed{"() audio_effect_single.update_play_volume_speed (audio_effect_single, const f64, const f64, const e_audio_effect, const e_audio_group, const vec3)","53 55 56 57 48 81 ec 18 01 00 00 48 8b ac 24 60 01 00 00 48 8b 9c 24 68 01 00 00 48 89 4c 24 78",{"53 55 48 83 ec 78 48 89 4c 24 30 48 89 54 24 38 4c 89 44 24 40 4c 89 4c 24 48 48 8d 01 48 8d 12", {352u}, 1},"",0u,-1};
// f64 g_server_delta_time (server tick length, seconds) | anchored on a creature tick's relocation slot
inline constexpr global_desc server_delta_time{"g_server_delta_time","53 55 48 83 ec 68 48 8b 84 24 a0 00 00 00 48 89 4c 24 20 48 89 54 24 28 4c 89 44 24 30 4c 89 4c 24 38 48 89 44 24 40 48 8d 00",136u};
}  // namespace anyalternator::bind
