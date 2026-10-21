#pragma once
// Dyno game bindings, generated for Anymaker 0.1.27 / Steam build 26113663 with
//   python3 tools/bind.py --reference . --signature "<signature>" --out <file>.hpp
// run from the 0.1.25 SDK reference folder, one --signature per binding below (several are the same lines as
// rotary_bindings.h, turbo_bindings.h and gpspart_bindings.h).
// Experimental: located by code signature, call-cell route or typeinfo slot, not reviewed as an AnyAPI service.
// Regenerate and re-test after any game update.
#include "anymaker_sdk_runtime.hpp"
namespace dyno::bind {
using anymaker::func_desc;
// () ctor (vehicle_component_definition_file) | side: shared
inline constexpr func_desc definition_file_ctor{"() ctor (vehicle_component_definition_file)","",{"53 48 81 ec e0 00 00 00 48 89 4c 24 60 48 89 54 24 68 48 8d 44 24 70 48 8b 1d 8a 01 00 00 48 89", {424u}, 1},"",0u,-1};
// () dtor (vehicle_component_definition_file) | side: shared
inline constexpr func_desc definition_file_dtor{"() dtor (vehicle_component_definition_file)","",{"53 48 81 ec e0 00 00 00 48 89 4c 24 60 48 89 54 24 68 48 8d 44 24 70 48 8b 1d 8a 01 00 00 48 89", {496u}, 1},"",0u,-1};
// () vehicle_component_definition_container._add_definitions (vehicle_component_definition_container, const vehicle_component_definition_file) | side: shared
inline constexpr func_desc definitions_add_definitions{"() vehicle_component_definition_container._add_definitions (vehicle_component_definition_container, const vehicle_component_definition_file)","",{"53 55 56 57 41 54 41 55 41 56 41 57 48 81 ec d8 03 00 00 48 89 8c 24 50 03 00 00 48 8d 81 a0 29", {5184u,248u}, 2},"",0u,-1};
// (bool) vehicle_component_definition_file.load (vehicle_component_definition_file, const file.path) | side: shared
inline constexpr func_desc definition_file_load{"(bool) vehicle_component_definition_file.load (vehicle_component_definition_file, const file.path)","",{"53 55 56 57 41 54 41 55 41 56 41 57 48 81 ec d8 03 00 00 48 89 8c 24 50 03 00 00 48 8d 81 a0 29", {5184u,240u}, 2},"",0u,-1};
// (ptr<const vehicle_component_definition>) vehicle_component_definition_container.get_definition_by_id_const (const vehicle_component_definition_container, const string) | side: shared
inline constexpr func_desc definitions_get_by_id{"(ptr<const vehicle_component_definition>) vehicle_component_definition_container.get_definition_by_id_const (const vehicle_component_definition_container, const string)","",{"53 48 81 ec a0 00 00 00 48 89 4c 24 50 48 89 54 24 58 48 8d 41 28 48 8b 1d 2b 01 00 00 48 89 4c", {368u}, 1},"",0u,-1};
// () server_scene.vehicle_component.engine.tick (server_scene.vehicle_component.engine, server_scene.vehicle, server_scene) | side: server
inline constexpr func_desc engine_tick{"() server_scene.vehicle_component.engine.tick (server_scene.vehicle_component.engine, server_scene.vehicle, server_scene)","53 55 56 57 41 54 41 55 41 56 41 57 48 81 ec c8 07 00 00 48 89 8c 24 78 05 00 00 48 89 94 24 80",{"", {}, 0},"53 48 83 ec 30 48 89 4c 24 20 48 8d 01 48 8b 15 c4 02 00 00 48 89 4c 24 28 48 89 c1 ff 12 48 8b 05 bb 02 00 00 48 8b 4c 24 28 48 89 01 48 8d 81 e0 01 00 00 48 8d",736u,20};
// (mat34) server_scene.vehicle.get_transform (const server_scene.vehicle) | side: server
inline constexpr func_desc server_vehicle_get_transform{"(mat34) server_scene.vehicle.get_transform (const server_scene.vehicle)","48 83 ec 38 48 89 4c 24 20 48 89 54 24 28 48 8d 09 48 8d 92 e8 05 00 00 48 8b 05 11 00 00 00 ff",{"", {}, 0},"53 48 83 ec 30 48 89 4c 24 20 48 8d 01 48 8b 15 b4 04 00 00 48 89 4c 24 28 48 89 c1 ff 12 48 8b",1232u,21};
// (mat34) server_scene.vehicle_component.get_transform (const server_scene.vehicle_component) | side: server
inline constexpr func_desc server_component_get_transform{"(mat34) server_scene.vehicle_component.get_transform (const server_scene.vehicle_component)","53 55 48 81 ec 48 01 00 00 48 89 4c 24 50 48 89 54 24 58 48 8b 9a 48 01 00 00 48 8d 44 24 60 48",{"", {}, 0},"53 48 83 ec 40 48 89 4c 24 20 48 8d 01 48 8b 15 64 01 00 00 48 89 4c 24 30 48 89 c1 ff 12 48 8b",384u,25};
// (ptr<f64>) server_scene.vehicle_component.altitude_sensor.get_data_f64 (server_scene.vehicle_component.altitude_sensor, const string) | side: server
inline constexpr func_desc altitude_get_data_f64{"(ptr<f64>) server_scene.vehicle_component.altitude_sensor.get_data_f64 (server_scene.vehicle_component.altitude_sensor, const string)","53 55 48 81 ec 98 00 00 00 48 89 4c 24 48 48 89 54 24 50 4c 89 44 24 58 48 8d 44 24 60 48 8d 1d cc 00 00 00 48 8b 2d d5 00 00 00 48 89 4c 24 28 48 89 94 24 80 00 00 00 48 89 c1 48 89 da 4c 89 44 24 78 ff 55 00 48 8d 44 24 70 4c 8b 44 24 78 49 8d 08 4c 8d 44 24 60 48 8b 15 a9 00 00 00 48 89 44 24 40 48 89 4c 24 20 48 89 54 24 38 48 8b 44 24 38 48 8b 4c 24 40 48 8b 54 24 20 ff 10 8a 5c 24 70 48 8d 44 24 60 48 8b 0d 81 00 00 00 48 89 44 24 30 48 89 8c 24 88 00 00 00 48 8b 84 24 88 00 00 00 48 8b 4c 24 30 ff 10 80 fb 00 0f 84 1c 00 00 00 48 8b 94 24 80 00 00 00 48 8d 82 18 02 00 00 48 8b 4c 24 28 48 89 01 e9 11 00 00 00 48 8b 4c 24 28 48 c7 01 00 00 00 00 e9 00 00 00 00 48 81 c4 98 00 00 00 5d 5b c3 00 00 00 00 00 61",{"", {}, 0},"53 48 83 ec 30 48 89 4c 24 20 48 8d 01 48 8b 15 7c 00 00 00 48 89 4c 24 28 48 89 c1 ff 12 48 8b 05 73 00 00 00 48 8b 4c 24 28 48 89 01 48 8d 81 e0 01 00 00 48 8b 15 65 00 00 00 48 89 c1 ff 12",152u,59};
// () server_scene.vehicle_component.altitude_sensor.tick (server_scene.vehicle_component.altitude_sensor, server_scene.vehicle, server_scene) | side: server
inline constexpr func_desc altitude_tick{"() server_scene.vehicle_component.altitude_sensor.tick (server_scene.vehicle_component.altitude_sensor, server_scene.vehicle, server_scene)","53 55 48 81 ec d8 01 00 00 48 89 8c 24 80 00 00 00 48 89 94 24 88 00 00 00 4c 89 84 24 90 00 00 00 48 8d 84 24 f8 00 00 00 48 8b 1d 98",{"", {}, 0},"53 48 83 ec 30 48 89 4c 24 20 48 8d 01 48 8b 15 7c 00 00 00 48 89 4c 24 28 48 89 c1 ff 12 48 8b 05 73 00 00 00 48 8b 4c 24 28 48 89 01 48 8d 81 e0 01 00 00 48 8b 15 65 00 00 00 48 89 c1 ff 12",152u,20};
} // namespace dyno::bind
