#pragma once
// AnyCareer game bindings, generated for Anymaker 0.1.25 / Steam build 25904830 with
//   python tools/bind.py --reference . --signature "<signature>" --out <file>.hpp
// run from the SDK reference root (reference-0.1.25), one --signature per binding below.
// Experimental: located by code signature, not reviewed as an AnyAPI service.
// Regenerate and re-test after any game update.
#include "anymaker_sdk_runtime.hpp"
namespace anycareer::bind {
using anymaker::func_desc;
// Server tick: AnyCareer's queued host work runs at its start, on the server thread.
// () server_scene.tick (server_scene, server) | side: server
inline constexpr func_desc server_scene_tick{"() server_scene.tick (server_scene, server)","53 55 56 57 48 81 ec a8 02 00 00 48 89 8c 24 f0 00 00 00 48 89 94 24 f8 00 00 00 48 8d 84 24 00",{"", {}, 0},"53 55 48 83 ec 38 48 89 4c 24 20 48 8d 01 48 8b 15 db 06 00 00 48 89 4c 24 30 48 89 c1 ff 12 48",1784u,13};

// A connected player by peer id (server side).
// (ptr<server_scene.peer>) server_scene.peer.container.get_peer_by_id (server_scene.peer.container, const s32) | side: server
inline constexpr func_desc peer_container_get_peer_by_id{"(ptr<server_scene.peer>) server_scene.peer.container.get_peer_by_id (server_scene.peer.container, const s32)","53 48 81 ec a0 00 00 00 48 89 4c 24 48 48 89 54 24 50 4c 89 44 24 58 48 8b 44 24 28 48 89 44 24 60 4c 8d 4c 24 60 48 8d 44 24 68 48 89 84 24 90 00 00 00 48 8d 02 48 8b 15 2b 01 00 00 48 89 84 24 88 00 00 00 48 89 8c 24 80 00 00 00 48 89 54 24 40 48 8b 44 24 40 4c 89 c9 48 8b 94 24 88 00 00 00 48 89 4c 24 20 4c 89 44 24 38 ff 10 48 8d 44 24 70 48 8b 0d f6 00 00 00 48 89 44 24 30 48 89 8c 24 98 00 00 00 48 8b 84 24 98 00 00 00 48 8b 4c 24 30 48 8b 94 24 88 00 00 00 ff 10 48 8b 44 24 70 48 39 44 24 60 0f 84 96",{"", {}, 0},"48 83 ec 38 48 89 4c 24 20 48 8d 01 48 8b 15 25 00 00 00 48 89 4c 24 28 48 89 c1 ff 12 48 8b 05 1c 00 00 00 48 8b 4c 24 28 48 89 01 48 83 c4 38 c3 00 00 00 00 00 00 00",64u,19};

// Adds a parsed definition file to an inventory definition container (both scenes call it at load). Hooked: AnyCareer adds the Cash item right after the game's own items.
// () inventory_definition_container._add_definitions (inventory_definition_container, const inventory_definition_file, const outfit_resources) | side: server+client
inline constexpr func_desc inv_add_definitions{"() inventory_definition_container._add_definitions (inventory_definition_container, const inventory_definition_file, const outfit_resources)","53 55 56 48 81 ec 00 02 00 00 48 89 8c 24 28 01 00 00 48 89 94 24 30 01 00 00 4c 89 84 24 38 01",{"53 55 56 48 81 ec a0 01 00 00 48 89 8c 24 e0 00 00 00 48 89 94 24 e8 00 00 00 4c 89 84 24 f0 00 00 00 48 8d 01", {1280u}, 1},"",0u,-1};

// Constructs an empty inventory_definition_file.
// () ctor (inventory_definition_file) | side: shared
inline constexpr func_desc inv_file_ctor{"() ctor (inventory_definition_file)","",{"53 55 56 48 81 ec a0 01 00 00 48 89 8c 24 e0 00 00 00 48 89 94 24 e8 00 00 00 4c 89 84 24 f0 00 00 00 48 8d 01", {1256u}, 1},"",0u,-1};

// Destroys an inventory_definition_file.
// () dtor (inventory_definition_file) | side: shared
inline constexpr func_desc inv_file_dtor{"() dtor (inventory_definition_file)","",{"53 55 56 48 81 ec a0 01 00 00 48 89 8c 24 e0 00 00 00 48 89 94 24 e8 00 00 00 4c 89 84 24 f0 00 00 00 48 8d 01", {1416u}, 1},"",0u,-1};

// Parses an inventory definitions JSON file into an inventory_definition_file.
// (bool) inventory_definition_file.load (inventory_definition_file, const file.path) | side: shared
inline constexpr func_desc inv_file_load{"(bool) inventory_definition_file.load (inventory_definition_file, const file.path)","",{"53 55 56 48 81 ec a0 01 00 00 48 89 8c 24 e0 00 00 00 48 89 94 24 e8 00 00 00 4c 89 84 24 f0 00 00 00 48 8d 01", {1264u}, 1},"",0u,-1};

// An inventory definition by its string id.
// (ptr<const inventory_definition>) inventory_definition_container.get_definition_by_id_const (const inventory_definition_container, const string) | side: shared
inline constexpr func_desc inv_get_definition_by_id{"(ptr<const inventory_definition>) inventory_definition_container.get_definition_by_id_const (const inventory_definition_container, const string)","",{"53 48 83 ec 60 48 89 4c 24 20 48 89 54 24 28 4c 89 44 24 30 48 8d 44 24 38 48 8d 15 58 00 00 00 48 8b 1d 69 00 00 00 48 89 4c 24 48 48 89 c1 4c 89 44 24 50 ff 13 48 8b 4c 24 48 48 8d 09 4c 8b 44 24 50 49 8d 90 30 15 00 00 4c 8d 44 24 38 48 8b 05 42 00 00 00 ff 10 48 8d 4c 24 38 48 8b 05 3c 00 00 00 ff 10 e9 00 00 00 00 48 83 c4 60 5b c3 00 00 00 00 00 00 00 33 30", {152u}, 1},"",0u,-1};

// Creates a new item of a definition (what the creative dome uses to make items).
// (ref<server_scene.item_world>) server_scene.item_world.create_item (const inventory_definition, server_scene) | side: server
inline constexpr func_desc item_world_create_item{"(ref<server_scene.item_world>) server_scene.item_world.create_item (const inventory_definition, server_scene)","53 55 48 81 ec 28 01 00 00 48 89 8c 24 b8 00 00 00 48 89 94 24 c0 00 00 00 4c 89 84 24 c8 00 00",{"53 55 56 48 81 ec 20 01 00 00 48 89 8c 24 b0 00 00 00 48 89 94 24 b8 00 00 00 48 8b 42 08 48 8d", {1112u}, 1},"",0u,-1};

// Puts an item on the floor at a transform with a velocity.
// (ptr<server_scene.item_floor>) server_scene.item_floor_container.create_item (server_scene.item_floor_container, physics.scene, const mat34, const vec3, const ref<server_scene.item_world>) | side: server
inline constexpr func_desc floor_create_item{"(ptr<server_scene.item_floor>) server_scene.item_floor_container.create_item (server_scene.item_floor_container, physics.scene, const mat34, const vec3, const ref<server_scene.item_world>)","53 55 56 57 48 81 ec 88 00 00 00 48 8b ac 24 d0 00 00 00 48 8b 9c 24 d8 00 00 00 48 89 4c 24 20 48 89 54 24 28 4c 89 44 24 30 4c 89 4c 24 38 48 89 6c 24 40 48 89 5c 24 48 48",{"", {}, 0},"48 83 ec 38 48 89 4c 24 20 48 8d 01 48 8b 15 3d 00 00 00 48 89 4c 24 28 48 89 c1 ff 12 48 8b 05 34 00 00 00 48 8b 4c 24 28 48 89 01 8b 05 16 00 00 00 89 41 70",88u,16};

} // namespace anycareer::bind
