#pragma once
// AnyLights game bindings, generated for Anymaker 0.1.27 / Steam build 26113663 with
//   python tools/bind.py --reference reference --signature "<signature>" --out <file>.hpp
// run from the extracted AnyAPI-Experimental-SDK sdk/experimental folder, one --signature per binding below.
// Experimental: located by code signature, call-cell route or typeinfo slot, not reviewed as an AnyAPI service.
// Regenerate and re-test after any game update.
#include "anymaker_sdk_runtime.hpp"
namespace anylights::bind {
using anymaker::func_desc;
using anymaker::global_desc;
// () client_scene.item_world.vehicle_editor_paint.tick_active (client_scene.item_world.vehicle_editor_paint, client, client_scene, client_scene.actor, const mat34, const bool, const inventory_item_util.e_item_location_type, const f64) | side: client
inline constexpr func_desc paint_tick_active{"() client_scene.item_world.vehicle_editor_paint.tick_active (client_scene.item_world.vehicle_editor_paint, client, client_scene, client_scene.actor, const mat34, const bool, const inventory_item_util.e_item_location_type, const f64)","53 55 56 57 41 54 41 55 41 56 41 57 48 81 ec 78 0a 00 00 48 8b 84 24 e0 0a 00 00 4c 8b 94 24 e8",{"", {}, 0},"48 83 ec 38 48 89 4c 24 20 48 8d 01 48 8b 15 6d 01 00 00 48 89 4c 24 28 48 89 c1 ff 12 48 8b 05",392u,8};
// () client_peer.data.push_event_vehicle_paint_fill_component (client_peer.data, const s32, const s32, const s32, const s32) | side: client
inline constexpr func_desc push_fill_component{"() client_peer.data.push_event_vehicle_paint_fill_component (client_peer.data, const s32, const s32, const s32, const s32)","",{"53 55 56 57 41 54 41 55 41 56 41 57 48 81 ec 78 0a 00 00 48 8b 84 24 e0 0a 00 00 4c 8b 94 24 e8", {15584u}, 1},"",0u,-1};
// () client_peer.data.push_event_vehicle_paint_fill_plate (client_peer.data, const s32, const s32, const s32, const bool) | side: client
inline constexpr func_desc push_fill_plate{"() client_peer.data.push_event_vehicle_paint_fill_plate (client_peer.data, const s32, const s32, const s32, const bool)","",{"53 55 56 57 41 54 41 55 41 56 41 57 48 81 ec 78 0a 00 00 48 8b 84 24 e0 0a 00 00 4c 8b 94 24 e8", {15688u}, 1},"",0u,-1};
// () client_scene.vehicle_component.light_omni.tick (client_scene.vehicle_component.light_omni, client, client_scene.vehicle, client_scene, const f64) | side: client
inline constexpr func_desc client_omni_tick{"() client_scene.vehicle_component.light_omni.tick (client_scene.vehicle_component.light_omni, client, client_scene.vehicle, client_scene, const f64)","53 55 56 48 81 ec b0 02 00 00 48 8b 84 24 f0 02 00 00 48 89 8c 24 98 01 00 00 48 89 94 24 a0 01",{"", {}, 0},"53 48 83 ec 30 48 89 4c 24 20 48 8d 01 48 8b 15 9c 00 00 00 48 89 4c 24 28 48 89 c1 ff 12 48 8b 05 93 00 00 00 48 8b 4c 24 28 48 89 01 48 8d 81 e0 00",184u,8};
// () client_scene.vehicle_component.light_rotate.tick (client_scene.vehicle_component.light_rotate, client, client_scene.vehicle, client_scene, const f64) | side: client
inline constexpr func_desc client_rotate_tick{"() client_scene.vehicle_component.light_rotate.tick (client_scene.vehicle_component.light_rotate, client, client_scene.vehicle, client_scene, const f64)","53 55 56 48 81 ec 90 02 00 00 48 8b 84 24 d0 02 00 00 48 89 8c 24 a0 00 00 00 48 89 94 24 a8 00",{"", {}, 0},"53 48 83 ec 30 48 89 4c 24 20 48 8d 01 48 8b 15 c4 00 00 00 48 89 4c 24 28 48 89 c1 ff 12 48 8b 05 bb 00 00 00 48 8b 4c 24 28 48 89 01 48 8d 81 e0",224u,8};
// () client_scene.vehicle_component.light_spot.tick (client_scene.vehicle_component.light_spot, client, client_scene.vehicle, client_scene, const f64) | side: client
inline constexpr func_desc client_spot_tick{"() client_scene.vehicle_component.light_spot.tick (client_scene.vehicle_component.light_spot, client, client_scene.vehicle, client_scene, const f64)","53 55 56 48 81 ec 10 05 00 00 48 8b 84 24 50 05 00 00 48 89 8c 24 98 01 00 00 48 89 94 24 a0 01",{"", {}, 0},"53 48 83 ec 30 48 89 4c 24 20 48 8d 01 48 8b 15 14 01 00 00 48 89 4c 24 28 48 89 c1 ff 12 48 8b 05 0b 01 00 00 48 8b 4c 24 28 48 89 01 48 8d 81 e0",304u,8};
// color_palette g_color_palette (vector<color8> at +0), read through get_hotbar_icon_color
inline constexpr global_desc g_color_palette{"g_color_palette","53 55 56 48 83 ec 50 48 89 4c 24 20 48 89 54 24 28 48 8b 1d 70 00 00 00 48 8d 44 24 30 48 8d 6a",136u};
} // namespace anylights::bind
