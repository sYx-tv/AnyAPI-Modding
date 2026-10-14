#pragma once
// AnySuspension game bindings for Anymaker 0.1.25 / Steam build 25904830, generated with
//   python tools/bind.py --reference . --signature "<signature>" --out <file>.hpp
// from the 0.1.25 SDK reference, one --signature per binding below. The definition-file five are the same as
// Rotary's and GpsPart's. Experimental: located by code signature, call-cell route or typeinfo slot, not reviewed
// as an AnyAPI service. Regenerate and re-test after any game update.
#include "anymaker_sdk_runtime.hpp"
namespace suspension::bind {
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
// () server_scene.vehicle_component.rail_slider.tick (server_scene.vehicle_component.rail_slider, server_scene.vehicle, server_scene) | side: server
inline constexpr func_desc rail_slider_tick{"() server_scene.vehicle_component.rail_slider.tick (server_scene.vehicle_component.rail_slider, server_scene.vehicle, server_scene)","53 55 48 81 ec d8 00 00 00 48 89 4c 24 60 48 89 54 24 68 4c 89 44 24 70 48 8d 44 24 78 48 8d 91 00",{"", {}, 0},"48 83 ec 38 48 89 4c 24 20 48 8d 01 48 8b 15 95 00 00 00 48 89 4c 24 28 48 89 c1 ff 12 48 8b 05 8c 00 00 00 48 8b 4c 24 28 48 89 01 48 8d 81 e0 01 00 00 48 8b 15 7e 00 00 00 48 89 c1 ff 12 48 8b 4c 24 28 48 8d 81 00 02 00 00 48 8b 15 6e 00 00 00 48 89 c1 ff 12 48 8b 4c 24 28 48 8d 81 20 02 00 00 48 8b 15 5e 00 00 00 48 89 c1 ff 12 48",176u,20};
// () server_scene.vehicle_component.get_properties (server_scene.vehicle_component, server_scene.peer.property_data) | side: server
// The rail slider has no rows of its own: its method-table slot 37 (V:server_scene.vehicle_component#37) holds the
// base function. Reached through the rail slider's typeinfo, like its tick (slot 20). Cells are shared per function,
// so the hook sees every component class without its own rows; it acts only on suspension sliders.
inline constexpr func_desc rail_slider_get_properties{"() server_scene.vehicle_component.get_properties (server_scene.vehicle_component, server_scene.peer.property_data)","",{"", {}, 0},"48 83 ec 38 48 89 4c 24 20 48 8d 01 48 8b 15 95 00 00 00 48 89 4c 24 28 48 89 c1 ff 12 48 8b 05 8c 00 00 00 48 8b 4c 24 28 48 89 01 48 8d 81 e0 01 00 00 48 8b 15 7e 00 00 00 48 89 c1 ff 12 48 8b 4c 24 28 48 8d 81 00 02 00 00 48 8b 15 6e 00 00 00 48 89 c1 ff 12 48 8b 4c 24 28 48 8d 81 20 02 00 00 48 8b 15 5e 00 00 00 48 89 c1 ff 12 48",176u,37};
} // namespace suspension::bind
