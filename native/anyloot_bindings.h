#pragma once
// AnyLoot game bindings, generated for Anymaker 0.1.28 / Steam build 26277967 with
//   python tools/bind.py --reference . --signature "<signature>" --out <file>.hpp
// run from the SDK reference root (reference-0.1.25), one --signature per binding below.
// Experimental: located by code signature, not reviewed as an AnyAPI service.
// Regenerate and re-test after any game update.
#include "anymaker_sdk_runtime.hpp"
namespace anyloot::bind {
using anymaker::func_desc;
// Server tick: AnyLoot's queued host work runs at its start, on the server thread.
// () server_scene.tick (server_scene, server) | side: server
inline constexpr func_desc server_scene_tick{"() server_scene.tick (server_scene, server)","53 55 56 57 48 81 ec a8 02 00 00 48 89 8c 24 f0 00 00 00 48 89 94 24 f8 00 00 00 48 8d 84 24 00",{"", {}, 0},"53 55 48 83 ec 38 48 89 4c 24 20 48 8d 01 48 8b 15 db 06 00 00 48 89 4c 24 30 48 89 c1 ff 12 48",1784u,13};
// A connected player by peer id (server side).
// (ptr<server_scene.peer>) server_scene.peer.container.get_peer_by_id (server_scene.peer.container, const s32) | side: server
inline constexpr func_desc peer_container_get_peer_by_id{"(ptr<server_scene.peer>) server_scene.peer.container.get_peer_by_id (server_scene.peer.container, const s32)","53 48 81 ec a0 00 00 00 48 89 4c 24 48 48 89 54 24 50 4c 89 44 24 58 48 8b 44 24 28 48 89 44 24 60 4c 8d 4c 24 60 48 8d 44 24 68 48 89 84 24 90 00 00 00 48 8d 02 48 8b 15 2b 01 00 00 48 89 84 24 88 00 00 00 48 89 8c 24 80 00 00 00 48 89 54 24 40 48 8b 44 24 40 4c 89 c9 48 8b 94 24 88 00 00 00 48 89 4c 24 20 4c 89 44 24 38 ff 10 48 8d 44 24 70 48 8b 0d f6 00 00 00 48 89 44 24 30 48 89 8c 24 98 00 00 00 48 8b 84 24 98 00 00 00 48 8b 4c 24 30 48 8b 94 24 88 00 00 00 ff 10 48 8b 44 24 70 48 39 44 24 60 0f 84 96",{"", {}, 0},"48 83 ec 38 48 89 4c 24 20 48 8d 01 48 8b 15 25 00 00 00 48 89 4c 24 28 48 89 c1 ff 12 48 8b 05 1c 00 00 00 48 8b 4c 24 28 48 89 01 48 83 c4 38 c3 00 00 00 00 00 00 00",64u,19};
// Creates an empty vehicle at a transform (what the creative dome uses for its storage containers).
// (ptr<server_scene.vehicle>) server_scene.vehicle.container.create (server_scene.vehicle.container, physics.scene, server_scene.environment_tile_container, const mat34) | side: server
inline constexpr func_desc vehicle_container_create{"(ptr<server_scene.vehicle>) server_scene.vehicle.container.create (server_scene.vehicle.container, physics.scene, server_scene.environment_tile_container, const mat34)","53 55 56 57 48 81 ec b8 00 00 00 48 8b 9c 24 00 01 00 00 48 89 4c 24 50 48 89 54 24 58 4c 89 44 24 60 4c 89 4c 24 68 48 89 5c 24 70 48 8b 05 5d",{"", {}, 0},"48 83 ec 38 48 89 4c 24 20 48 8d 01 48 8b 15 45 00 00 00 48 89 4c 24 28 48 89 c1 ff 12 48 8b 05 3c 00 00 00 48 8b 4c 24 28 48 89 01 8b 05 1e 00 00 00 89 41 78 48 8d 89 80 00 00 00 48 8b 05 25",96u,27};
// Adds one part to a vehicle at a grid position, rotation and extend size.
// (ptr<server_scene.vehicle_component>) server_scene.vehicle.add_component (server_scene.vehicle, server_scene, const vehicle_util.grid_origin_dir, const vec3_s32, const mat33_s32, const vec3_s32, const vehicle_component_definition) | side: server
inline constexpr func_desc vehicle_add_component{"(ptr<server_scene.vehicle_component>) server_scene.vehicle.add_component (server_scene.vehicle, server_scene, const vehicle_util.grid_origin_dir, const vec3_s32, const mat33_s32, const vec3_s32, const vehicle_component_definition)","53 55 56 57 41 54 48 81 ec b0 00 00 00 48 8b ac 24 00 01 00 00 48 8b bc 24 08 01 00 00 4c 8b 94",{"", {}, 0},"53 48 83 ec 30 48 89 4c 24 20 48 8d 01 48 8b 15 b4 04 00 00 48 89 4c 24 28 48 89 c1 ff 12 48 8b",1232u,43};
// Finishes a vehicle after its parts are added (the creative dome calls it after add_component).
// () server_scene.vehicle.post_load (server_scene.vehicle, server_scene) | side: server
inline constexpr func_desc vehicle_post_load{"() server_scene.vehicle.post_load (server_scene.vehicle, server_scene)","53 55 48 81 ec 88 00 00 00 48 89 4c 24 40 48 89 54 24 48 8a 05 97 01 00 00 88 81 10 0b 00 00 48",{"", {}, 0},"53 48 83 ec 30 48 89 4c 24 20 48 8d 01 48 8b 15 b4 04 00 00 48 89 4c 24 28 48 89 c1 ff 12 48 8b",1232u,5};
// Rolls the game's own loot into a vehicle's storage at a loot level.
// () server_scene.vehicle.generate_loot (server_scene.vehicle, server_scene, const s32) | side: server
inline constexpr func_desc vehicle_generate_loot{"() server_scene.vehicle.generate_loot (server_scene.vehicle, server_scene, const s32)","53 55 56 57 48 81 ec c8 00 00 00 48 89 4c 24 68 48 89 54 24 70 4c 89 44 24 78 48 8d 84 24 80 00",{"", {}, 0},"53 48 83 ec 30 48 89 4c 24 20 48 8d 01 48 8b 15 b4 04 00 00 48 89 4c 24 28 48 89 c1 ff 12 48 8b",1232u,86};
// Fills the items shown when a zombie or creature is inspected; stock it adds none (characters override it).
// () client_scene.actor.on_inspect (const client_scene.actor, vector<inventory_item_util.item_id>) | side: client
inline constexpr func_desc actor_on_inspect{"() client_scene.actor.on_inspect (const client_scene.actor, vector<inventory_item_util.item_id>)","",{"", {}, 0},"53 55 56 48 83 ec 70 48 89 4c 24 30 48 89 54 24 38 4c 89 44 24 40 48 8d 02 48 89 44 24 50 48 8b",336u,73};
// A storage part's item list for the inspect/storage screen.
// () client_scene.vehicle_component.inventory.on_inspect (const client_scene.vehicle_component.inventory, vector<inventory_item_util.item_id>) | side: client
inline constexpr func_desc inventory_on_inspect{"() client_scene.vehicle_component.inventory.on_inspect (const client_scene.vehicle_component.inventory, vector<inventory_item_util.item_id>)","53 48 83 ec 40 48 89 4c 24 20 48 89 54 24 28 48 8d 44 24 30 48 8d 12 48 8b 1d 4a 00 00 00 48 89",{"", {}, 0},"48 83 ec 38 48 89 4c 24 20 48 8d 01 48 8b 15 35 00 00 00 48 89 4c 24 28 48 89 c1 ff 12 48 8b 05 2c 00 00 00 48 8b 4c 24 28 48 89 01 48 8d 89 e0 00 00 00 48 8b 05 1e 00 00 00 ff 10 48 83 c4 38",80u,36};
// A client vehicle by its replicated id.
// (ptr<client_scene.vehicle>) client_scene.vehicle.container.get_vehicle_by_id (const client_scene.vehicle.container, const s32) | side: client
inline constexpr func_desc vehicle_container_get_vehicle_by_id{"(ptr<client_scene.vehicle>) client_scene.vehicle.container.get_vehicle_by_id (const client_scene.vehicle.container, const s32)","",{"", {}, 0},"48 83 ec 38 48 89 4c 24 20 48 8d 01 48 8b 15 75 00 00 00 48 89 4c 24 28 48 89 c1 ff 12 48 8b 05 6c 00 00 00 48 8b 4c 24 28 48 89 01 48 8d 41 30",144u,15};
// A client vehicle's part by its id.
// (ptr<client_scene.vehicle_component>) client_scene.vehicle.get_component_by_id (client_scene.vehicle, const s32) | side: client
inline constexpr func_desc vehicle_get_component_by_id{"(ptr<client_scene.vehicle_component>) client_scene.vehicle.get_component_by_id (client_scene.vehicle, const s32)","",{"53 55 56 48 83 ec 70 48 89 4c 24 20 48 89 54 24 28 4c 89 44 24 30 41 8b 00 3b 05 f1 00 00 00 0f", {288u}, 1},"",0u,-1};
// The replicated "incapacitated" flag: true once a zombie, creature or player is down.
// (bool) client_scene.actor.get_is_incapacitated (const client_scene.actor) | side: client
inline constexpr func_desc actor_get_is_incapacitated{"(bool) client_scene.actor.get_is_incapacitated (const client_scene.actor)","53 55 48 83 ec 58 48 89 4c 24 20 48 89 54 24 28 48 8d 44 24 30 48 8d 5a 28 48 8b 52 28 48 8b 92 80 00 00 00 48 8b 2d 45 00 00 00 48 01 ea 48 8b 12 48 8b 12 48 89 44 24 38 48 89 4c 24 40 48 89 54 24 48 48 8b 44 24 48 48 8b 4c 24 38 48 89 da ff d0 48 8b 44 24 30 8a",{"", {}, 0},"53 55 56 48 83 ec 70 48 89 4c 24 30 48 89 54 24 38 4c 89 44 24 40 48 8d 02 48 89 44 24 50 48 8b",336u,99};
// A client actor by its replicated id.
// (ptr<client_scene.actor>) client_scene.actor.container.get_actor_by_id (const client_scene.actor.container, const s32) | side: client
inline constexpr func_desc actor_container_get_actor_by_id{"(ptr<client_scene.actor>) client_scene.actor.container.get_actor_by_id (const client_scene.actor.container, const s32)","",{"", {}, 0},"48 83 ec 38 48 89 4c 24 20 48 8d 01 48 8b 15 4d 00 00 00 48 89 4c 24 28 48 89 c1 ff 12 48 8b 05 44 00 00 00 48 8b 4c 24 28 48 89 01 48 c7 41 30",104u,22};
// Puts one item of a given definition into the first storage part of a vehicle with room for it (the game's own
// dead-drop and furniture loot use it).
// (bool) server_scene.vehicle._spawn_loot_item_in_furniture (server_scene.vehicle, server_scene, math.random, const ptr<const inventory_definition>) | side: server
inline constexpr func_desc vehicle_spawn_loot_item_in_furniture{"(bool) server_scene.vehicle._spawn_loot_item_in_furniture (server_scene.vehicle, server_scene, math.random, const ptr<const inventory_definition>)","53 55 56 57 41 54 41 55 41 56 48 81 ec 00 02 00 00 4c 8b a4 24 60 02 00 00 48 89 8c 24 38 01 00",{"", {}, 0},"53 48 83 ec 30 48 89 4c 24 20 48 8d 01 48 8b 15 b4 04 00 00 48 89 4c 24 28 48 89 c1 ff 12 48 8b",1232u,89};
} // namespace anyloot::bind
