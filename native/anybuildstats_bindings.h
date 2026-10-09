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
// () server_scene.vehicle_component.engine.tick (server_scene.vehicle_component.engine, server_scene.vehicle, server_scene) | side: server
inline constexpr func_desc server_engine_tick{"() server_scene.vehicle_component.engine.tick (server_scene.vehicle_component.engine, server_scene.vehicle, server_scene)","53 55 56 57 41 54 41 55 41 56 41 57 48 81 ec c8 07 00 00 48 89 8c 24 78 05 00 00 48 89 94 24 80",{"", {}, 0},"53 48 83 ec 30 48 89 4c 24 20 48 8d 01 48 8b 15 c4 02 00 00 48 89 4c 24 28 48 89 c1 ff 12 48 8b 05 bb 02 00 00 48 8b 4c 24 28 48 89 01 48 8d 81 e0 01 00 00 48 8d",736u,20};
// () server_scene.vehicle_component.motor.tick (server_scene.vehicle_component.motor, server_scene.vehicle, server_scene) | side: server
inline constexpr func_desc server_motor_tick{"() server_scene.vehicle_component.motor.tick (server_scene.vehicle_component.motor, server_scene.vehicle, server_scene)","53 55 48 81 ec 08 01 00 00 48 89 8c 24 80 00 00 00 48 89 94 24 88 00 00 00 4c 89 84 24 90 00 00",{"", {}, 0},"53 48 83 ec 30 48 89 4c 24 20 48 8d 01 48 8b 15 2c 01 00 00 48 89 4c 24 28 48 89 c1 ff 12 48 8b",328u,20};
// () server_scene.vehicle_component.wheel.tick (server_scene.vehicle_component.wheel, server_scene.vehicle, server_scene) | side: server
inline constexpr func_desc server_wheel_tick{"() server_scene.vehicle_component.wheel.tick (server_scene.vehicle_component.wheel, server_scene.vehicle, server_scene)","53 55 56 57 48 81 ec d8 04 00 00 48 89 8c 24 e8 01 00 00 48 89 94 24 f0 01 00 00 4c 89 84 24 f8 01 00 00 8a 81 d8",{"", {}, 0},"53 48 83 ec 30 48 89 4c 24 20 48 8d 01 48 8b 15 3c 01 00 00 48 89 4c 24 28 48 89 c1 ff 12 48 8b 05 33 01 00 00 48 8b 4c 24 28 48 89 01 48 8d 81 e0 01 00 00 48 8b",344u,20};
// () server_scene.vehicle_component.wheel_hydraulic.tick (server_scene.vehicle_component.wheel_hydraulic, server_scene.vehicle, server_scene) | side: server
inline constexpr func_desc server_wheel_hydraulic_tick{"() server_scene.vehicle_component.wheel_hydraulic.tick (server_scene.vehicle_component.wheel_hydraulic, server_scene.vehicle, server_scene)","53 55 56 57 48 81 ec d8 04 00 00 48 89 8c 24 e8 01 00 00 48 89 94 24 f0 01 00 00 4c 89 84 24 f8 01 00 00 8a 81 80",{"", {}, 0},"53 48 83 ec 30 48 89 4c 24 20 48 8d 01 48 8b 15 7c 01 00 00 48 89 4c 24 28 48 89 c1 ff 12 48 8b 05 73 01 00 00 48 8b 4c 24 28 48 89 01 48 8d 81 e0 01 00 00 48 8b",408u,20};
// () server_scene.vehicle_component.train_wheel.tick (server_scene.vehicle_component.train_wheel, server_scene.vehicle, server_scene) | side: server
inline constexpr func_desc server_train_wheel_tick{"() server_scene.vehicle_component.train_wheel.tick (server_scene.vehicle_component.train_wheel, server_scene.vehicle, server_scene)","53 55 56 57 48 81 ec 08 0d 00 00 48 89 8c 24 08 0a 00 00 48 89 94 24 10 0a 00 00 4c 89 84 24 18",{"", {}, 0},"53 48 83 ec 30 48 89 4c 24 20 48 8d 01 48 8b 15 84 01 00 00 48 89 4c 24 28 48 89 c1 ff 12 48 8b 05 7b 01 00 00 48 8b 4c 24 28 48 89 01 48 8d 81 e0 01 00 00 48 8d",416u,20};
// () server_scene.vehicle_component.sprocket.tick (server_scene.vehicle_component.sprocket, server_scene.vehicle, server_scene) | side: server
inline constexpr func_desc server_sprocket_tick{"() server_scene.vehicle_component.sprocket.tick (server_scene.vehicle_component.sprocket, server_scene.vehicle, server_scene)","53 55 48 81 ec 38 04 00 00 48 89 4c 24 60 48 89 54 24 68 4c 89 44 24 70 48 8d 84 24 80 00 00 00",{"", {}, 0},"53 48 83 ec 30 48 89 4c 24 20 48 8d 01 48 8b 15 9c 00 00 00 48 89 4c 24 28 48 89 c1 ff 12 48 8b 05 93 00 00 00 48 8b 4c 24 28 48 89 01 48 8d 81 e0 01 00 00 48 8d 15 65",184u,20};
// () server_scene.vehicle_component.engine_wheel.tick (server_scene.vehicle_component.engine_wheel, server_scene.vehicle, server_scene) | side: server
inline constexpr func_desc server_engine_wheel_tick{"() server_scene.vehicle_component.engine_wheel.tick (server_scene.vehicle_component.engine_wheel, server_scene.vehicle, server_scene)","",{"", {}, 0},"53 48 83 ec 30 48 89 4c 24 20 48 8d 01 48 8b 15 94 00 00 00 48 89 4c 24 28 48 89 c1 ff 12 48 8b 05 8b 00 00 00 48 8b 4c 24 28 48 89 01 48 8d 81 e0 01 00 00 48 8b 15 7d 00 00 00 48 89 c1 ff 12 48 8b 4c 24 28 48 8d 81 20",176u,20};
} // namespace anybuildstats::bind
