#pragma once
// AnyCombat game bindings, generated for Anymaker 0.1.27 / Steam build 26113663 with
//   python tools/bind.py --reference . --signature "<signature>" --out <file>.hpp
// run from the SDK reference root (reference-0.1.25), one --signature per binding below.
// Experimental: located by code signature, not reviewed as an AnyAPI service.
// Regenerate and re-test after any game update.
#include "anymaker_sdk_runtime.hpp"
namespace anycombat::bind {
using anymaker::func_desc;
// The local client reporting a hit it made to the server: actor id, damage, hit point, surface normal, damage type,
// flesh effect. Called by item_world.melee.tick_stab, projectile.apply_impact_effects and actor_character.tick.
// () client_peer.data.push_event_actor_take_damage (client_peer.data, const s32, const f64, const vec3, const vec3, const e_actor_damage_type, const s32) | side: client
inline constexpr func_desc push_event_actor_take_damage{"() client_peer.data.push_event_actor_take_damage (client_peer.data, const s32, const f64, const vec3, const vec3, const e_actor_damage_type, const s32)","53 55 56 57 48 81 ec c8 00 00 00 48 8b 9c 24 10 01 00 00 48 8b ac 24 18 01 00 00 48 8b b4 24 20 01 00 00 48 89 4c 24 60 48 89 54 24 68 4c 89 44 24 70 4c 89 4c 24 78 48 89 9c 24 80 00 00 00 48 89 ac 24 88 00 00 00 48 89 b4 24 90 00 00 00 48 8b 05 fa 00 00 00 48 8b 00 48 8d bc 24 98 00 00 00 4c 8b 15 f0 00 00 00 41 bb 58",{"53 55 56 57 48 81 ec 98 03 00 00 48 8b b4 24 e0 03 00 00 48 8b ac 24 e8 03 00 00 48 8b bc 24 f0", {5952u}, 1},"",0u,-1};
// The camera the frame renders with (application.graphics.camera), after hand-held sway and shake.
// Called by client_scene.tick and client_scene.build_scene_render on the main thread.
// (application.graphics.camera) client_scene.get_camera (client_scene) | side: client
inline constexpr func_desc client_scene_get_camera{"(application.graphics.camera) client_scene.get_camera (client_scene)","53 55 48 81 ec 78 04 00 00 48 89 8c 24 38 01 00 00 48 89 94 24 40 01 00 00 48 8d 84 24 48 01 00",{"", {}, 0},"53 48 83 ec 30 48 89 4c 24 20 48 8d 01 48 8b 15 cc 08 00 00 48 89 4c 24 28 48 89 c1 ff 12 48 8b",2280u,18};
// The local player's actor (as ThirdPerson uses it).
// (ptr<client_scene.actor>) client_scene.actor.container.get_peer_actor (client_scene.actor.container) | side: client
inline constexpr func_desc actor_container_get_peer_actor{"(ptr<client_scene.actor>) client_scene.actor.container.get_peer_actor (client_scene.actor.container)","48 83 ec 18 48 89 0c 24 48 89 54 24 08 48 83 7a 30 00 0f 95 c0 3c 00 0f 84 0c 00 00 00 48 8b 42",{"", {}, 0},"48 83 ec 38 48 89 4c 24 20 48 8d 01 48 8b 15 4d 00 00 00 48 89 4c 24 28 48 89 c1 ff 12 48 8b 05 44 00 00 00 48 8b 4c 24 28 48 89 01 48 c7 41 30",104u,21};
// Looks an actor up by its replicated id (typeinfo slot 22 of the container, next to get_peer_actor).
// (ptr<client_scene.actor>) client_scene.actor.container.get_actor_by_id (const client_scene.actor.container, const s32) | side: client
inline constexpr func_desc actor_container_get_actor_by_id{"(ptr<client_scene.actor>) client_scene.actor.container.get_actor_by_id (const client_scene.actor.container, const s32)","",{"", {}, 0},"48 83 ec 38 48 89 4c 24 20 48 8d 01 48 8b 15 4d 00 00 00 48 89 4c 24 28 48 89 c1 ff 12 48 8b 05 44 00 00 00 48 8b 4c 24 28 48 89 01 48 c7 41 30",104u,22};
// The replicated "incapacitated" flag: true once a zombie, creature or player is down (the server decides).
// (bool) client_scene.actor.get_is_incapacitated (const client_scene.actor) | side: client
inline constexpr func_desc actor_get_is_incapacitated{"(bool) client_scene.actor.get_is_incapacitated (const client_scene.actor)","53 55 48 83 ec 58 48 89 4c 24 20 48 89 54 24 28 48 8d 44 24 30 48 8d 5a 28 48 8b 52 28 48 8b 92 80 00 00 00 48 8b 2d 45 00 00 00 48 01 ea 48 8b 12 48 8b 12 48 89 44 24 38 48 89 4c 24 40 48 89 54 24 48 48 8b 44 24 48 48 8b 4c 24 38 48 89 da ff d0 48 8b 44 24 30 8a",{"", {}, 0},"53 55 56 48 83 ec 70 48 89 4c 24 30 48 89 54 24 38 4c 89 44 24 40 48 8d 02 48 89 44 24 50 48 8b",336u,99};
} // namespace anycombat::bind
