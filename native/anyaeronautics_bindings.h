#pragma once
// AnyAeronautics game bindings, generated for Anymaker 0.2.0 / Steam build 26328453 with
//   python tools/bind.py --reference reference --signature "<signature>" --out <file>.hpp
// run from sdk/experimental against the 0.2.0 SDK reference, one --signature per binding below. The part-definition
// and transform bindings are AnyLaser's (anylaser_bindings.h), copied so the mods stay independent.
// Experimental: regenerate and re-test after any game update.
#include "anymaker_sdk_runtime.hpp"
namespace anyaeronautics::bind {
using anymaker::func_desc;
// () ctor (vehicle_component_definition_file) | side: shared
inline constexpr func_desc definition_file_ctor{"() ctor (vehicle_component_definition_file)","",{"53 48 81 ec e0 00 00 00 48 89 4c 24 60 48 89 54 24 68 48 8d 44 24 70 48 8b 1d 8a 01 00 00 48 89", {424u}, 1},"",0u,-1};
// () dtor (vehicle_component_definition_file) | side: shared
inline constexpr func_desc definition_file_dtor{"() dtor (vehicle_component_definition_file)","",{"53 48 81 ec e0 00 00 00 48 89 4c 24 60 48 89 54 24 68 48 8d 44 24 70 48 8b 1d 8a 01 00 00 48 89", {496u}, 1},"",0u,-1};
// (mat34) server_scene.vehicle.get_transform (const server_scene.vehicle) | side: server
inline constexpr func_desc server_vehicle_get_transform{"(mat34) server_scene.vehicle.get_transform (const server_scene.vehicle)","48 83 ec 38 48 89 4c 24 20 48 89 54 24 28 48 8d 09 48 8d 92 e8 05 00 00 48 8b 05 11 00 00 00 ff",{"", {}, 0},"53 48 83 ec 30 48 89 4c 24 20 48 8d 01 48 8b 15 b4 04 00 00 48 89 4c 24 28 48 89 c1 ff 12 48 8b",1232u,22};
// (mat34) server_scene.vehicle_component.get_transform (const server_scene.vehicle_component) | side: server
inline constexpr func_desc server_component_get_transform{"(mat34) server_scene.vehicle_component.get_transform (const server_scene.vehicle_component)","53 55 48 81 ec 48 01 00 00 48 89 4c 24 50 48 89 54 24 58 48 8b 9a 48 01 00 00 48 8d 44 24 60 48",{"", {}, 0},"53 48 83 ec 40 48 89 4c 24 20 48 8d 01 48 8b 15 64 01 00 00 48 89 4c 24 30 48 89 c1 ff 12 48 8b",384u,26};
// () vehicle_component_definition_container._add_definitions (vehicle_component_definition_container, const vehicle_component_definition_file) | side: shared
inline constexpr func_desc definitions_add_definitions{"() vehicle_component_definition_container._add_definitions (vehicle_component_definition_container, const vehicle_component_definition_file)","",{"53 55 56 57 41 54 41 55 41 56 41 57 48 81 ec d8 03 00 00 48 89 8c 24 50 03 00 00 48 8d 81 c0 29", {5184u,248u}, 2},"",0u,-1};
// (bool) vehicle_component_definition_file.load (vehicle_component_definition_file, const file.path) | side: shared
inline constexpr func_desc definition_file_load{"(bool) vehicle_component_definition_file.load (vehicle_component_definition_file, const file.path)","",{"53 55 56 57 41 54 41 55 41 56 41 57 48 81 ec d8 03 00 00 48 89 8c 24 50 03 00 00 48 8d 81 c0 29", {5184u,240u}, 2},"",0u,-1};
// (ptr<const vehicle_component_definition>) vehicle_component_definition_container.get_definition_by_id_const (const vehicle_component_definition_container, const string) | side: shared
inline constexpr func_desc definitions_get_by_id{"(ptr<const vehicle_component_definition>) vehicle_component_definition_container.get_definition_by_id_const (const vehicle_component_definition_container, const string)","",{"53 48 81 ec a0 00 00 00 48 89 4c 24 50 48 89 54 24 58 48 8d 41 28 48 8b 1d 2b 01 00 00 48 89 4c", {368u}, 1},"",0u,-1};
// () server_scene.vehicle_component.jet_engine_exhaust.tick (server_scene.vehicle_component.jet_engine_exhaust, server_scene.vehicle, server_scene) | side: server
inline constexpr func_desc exhaust_tick{"() server_scene.vehicle_component.jet_engine_exhaust.tick (server_scene.vehicle_component.jet_engine_exhaust, server_scene.vehicle, server_scene)","53 55 48 81 ec 08 03 00 00 48 89 8c 24 a0 01 00 00 48 89 94 24 a8 01 00 00 4c 89 84 24 b0 01 00",{"", {}, 0},"53 48 83 ec 30 48 89 4c 24 20 48 8d 01 48 8b 15 4c 01 00 00 48 89 4c 24 28 48 89 c1 ff 12 48 8b 05 43 01 00 00 48 8b 4c 24 28 48 89 01 48 8d 81 e0 01",360u,20};
// (ptr<f64>) server_scene.vehicle_component.jet_engine_exhaust.get_data_f64 (server_scene.vehicle_component.jet_engine_exhaust, const string) | side: server
inline constexpr func_desc exhaust_get_data_f64{"(ptr<f64>) server_scene.vehicle_component.jet_engine_exhaust.get_data_f64 (server_scene.vehicle_component.jet_engine_exhaust, const string)","",{"", {}, 0},"53 48 83 ec 30 48 89 4c 24 20 48 8d 01 48 8b 15 4c 01 00 00 48 89 4c 24 28 48 89 c1 ff 12 48 8b 05 43 01 00 00 48 8b 4c 24 28 48 89 01 48 8d 81 e0 01",360u,60};
// () physics_util.impulse_activator.add_force (physics_util.impulse_activator, physics.object, const vec3, const vec3) | side: shared
inline constexpr func_desc add_force_at{"() physics_util.impulse_activator.add_force (physics_util.impulse_activator, physics.object, const vec3, const vec3)","",{"53 55 48 81 ec 08 03 00 00 48 89 8c 24 a0 01 00 00 48 89 94 24 a8 01 00 00 4c 89 84 24 b0 01 00", {2024u}, 1},"",0u,-1};
// (f64) physics.object.get_mass (const physics.object) | side: shared
inline constexpr func_desc physics_get_mass{"(f64) physics.object.get_mass (const physics.object)","",{"53 55 48 83 ec 68 48 89 4c 24 20 48 89 54 24 28 f2 0f 10 05 a8 00 00 00 f2 0f 11 44 24 30 48 8d", {208u}, 1},"",0u,-1};
// (bool) physics.object.get_is_added_to_scene (const physics.object) | side: shared
inline constexpr func_desc physics_is_added{"(bool) physics.object.get_is_added_to_scene (const physics.object)","",{"53 55 48 83 ec 58 48 89 4c 24 20 48 89 54 24 28 4c 89 44 24 30 48 8d 44 24 38 48 8d 9a c8 00 00", {168u}, 1},"",0u,-1};
} // namespace anyaeronautics::bind
