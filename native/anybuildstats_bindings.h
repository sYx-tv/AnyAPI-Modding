#pragma once
// AnyBuildStats game bindings, generated for Anymaker 0.1.23 / Steam build 25755694 with
//   python tools/bind.py --reference reference --signature "<signature>" --out <file>.hpp
// run from the extracted AnyAPI-Experimental-SDK sdk/experimental folder, one --signature per binding below.
// Experimental: located by code signature, call-cell route or typeinfo slot, not reviewed as an AnyAPI service.
// Regenerate and re-test after any game update.
#include "anymaker_sdk_runtime.hpp"
namespace anybuildstats::bind {
using anymaker::func_desc;
using anymaker::global_desc;
// () client_scene.vehicle.tick (client_scene.vehicle, client, client_scene, const f64, const f64, const vec3) | side: client
inline constexpr func_desc client_vehicle_tick{"() client_scene.vehicle.tick (client_scene.vehicle, client, client_scene, const f64, const f64, const vec3)","53 55 56 57 41 54 48 81 ec 60 06 00 00 48 8b bc 24 b0 06 00 00 48 8b 84 24 b8 06 00 00 48 89 8c",{"", {}, 0},"48 83 ec 38 48 89 4c 24 20 48 8d 01 48 8b 15 9d 04 00 00 48 89 4c 24 28 48 89 c1 ff 12 48 8b 05",1208u,6};
} // namespace anybuildstats::bind
