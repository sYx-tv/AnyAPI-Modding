#pragma once
// AnyCareer game bindings, generated for Anymaker 0.1.27 / Steam build 26113663 with
//   python tools/bind.py --reference . --signature "<signature>" --out <file>.hpp
// run from the SDK reference root (reference-0.1.27), one --signature per binding below.
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

// Constructs an empty vector<ptr<server_scene.item_world>> (for inventory get_items).
// () ctor (vector<ptr<server_scene.item_world>>) | side: shared
inline constexpr func_desc item_vector_ctor{"() ctor (vector<ptr<server_scene.item_world>>)","",{"53 55 56 48 81 ec 70 01 00 00 48 89 8c 24 90 00 00 00 48 89 94 24 98 00 00 00 4c 89 84 24 a0 00", {1456u}, 1},"",0u,-1};

// Destroys that vector.
// () dtor (vector<ptr<server_scene.item_world>>) | side: shared
inline constexpr func_desc item_vector_dtor{"() dtor (vector<ptr<server_scene.item_world>>)","",{"53 55 56 48 81 ec 70 01 00 00 48 89 8c 24 90 00 00 00 48 89 94 24 98 00 00 00 4c 89 84 24 a0 00", {1536u}, 1},"",0u,-1};

// Every item a player carries: hands, worn slots and one level inside them (backpacks, pockets).
// () server_scene.inventory.get_items (server_scene.inventory, vector<ptr<server_scene.item_world>>) | side: server
inline constexpr func_desc inventory_get_items{"() server_scene.inventory.get_items (server_scene.inventory, vector<ptr<server_scene.item_world>>)","",{"", {}, 0},"53 48 83 ec 40 48 89 4c 24 20 48 8d 01 48 8b 15 d4 00 00 00 48 89 4c 24 30 48 89 c1 ff 12 48 8b",240u,28};

// Removes one carried item by id and hands back the last reference (the caller releases it, as the game does).
// (ref<server_scene.item_world>) server_scene.inventory.destroy_item (server_scene.inventory, const s32) | side: server
inline constexpr func_desc inventory_destroy_item{"(ref<server_scene.item_world>) server_scene.inventory.destroy_item (server_scene.inventory, const s32)","53 55 56 57 48 83 ec 78 48 89 4c 24 20 48 89 54 24 28 4c 89 44 24 30 48 c7 44 24 38 00 00 00 00",{"", {}, 0},"53 48 83 ec 40 48 89 4c 24 20 48 8d 01 48 8b 15 d4 00 00 00 48 89 4c 24 30 48 89 c1 ff 12 48 8b",240u,20};

// Turns a fresh vehicle_editor_add_component item into a stack of one part (dungeon loot does the same).
// () server_scene.item_world.vehicle_editor_add_component.set (server_scene.item_world.vehicle_editor_add_component, const vehicle_component_definition, const s32) | side: server
inline constexpr func_desc add_component_set{"() server_scene.item_world.vehicle_editor_add_component.set (server_scene.item_world.vehicle_editor_add_component, const vehicle_component_definition, const s32)","53 55 48 83 ec 58 48 89 4c 24 20 48 89 54 24 28 4c 89 44 24 30 48 8d 02 48 89 81 d0 00 00 00 48",{"", {}, 0},"53 55 56 57 41 54 41 55 48 81 ec 98 05 00 00 48 89 8c 24 60 04 00 00 48 89 94 24 68 04 00 00 4c",6280u,31};

// The same for building parts (vehicle_editor_add_building_component).
// () server_scene.item_world.vehicle_editor_add_building_component.set (server_scene.item_world.vehicle_editor_add_building_component, const vehicle_component_definition, const s32) | side: server
inline constexpr func_desc add_building_component_set{"() server_scene.item_world.vehicle_editor_add_building_component.set (server_scene.item_world.vehicle_editor_add_building_component, const vehicle_component_definition, const s32)","53 55 48 83 ec 68 48 89 4c 24 20 48 89 54 24 28 4c 89 44 24 30 48 83 ba b0 02 00 00 00 0f 95 c0",{"", {}, 0},"53 55 56 57 41 54 41 55 48 81 ec 98 05 00 00 48 89 8c 24 60 04 00 00 48 89 94 24 68 04 00 00 4c",6232u,31};
// Damage to a zombie (every hit, any source; its damage source names the attacking actor's id). Hooked: AnyCareer
// counts a kill when the hit takes the zombie's blood to zero.
// () server_scene.actor_zombie.damage (server_scene.actor_zombie, server_scene, const f64, const server_scene.actor_damage_src) | side: server
inline constexpr func_desc zombie_damage{"() server_scene.actor_zombie.damage (server_scene.actor_zombie, server_scene, const f64, const server_scene.actor_damage_src)","",{"53 55 56 57 48 81 ec e8 00 00 00 48 89 4c 24 70 48 89 54 24 78 4c 89 84 24 80 00 00 00 4c 89 8c 24 88 00 00 00 48", {760u}, 1},"48 83 ec 38 48 89 4c 24 20 48 8d 01 48 8b 15 a5 01 00 00 48 89 4c 24 28 48 89 c1 ff 12 48 8b 05",448u,99};
// A new ammo box is filled to its definition's capacity (ammo_box.init). Hooked: Cash found as loot gets a small
// amount instead of a full stack.
// () server_scene.item_world.ammo_box.init (server_scene.item_world.ammo_box) | side: server
inline constexpr func_desc ammo_box_init{"() server_scene.item_world.ammo_box.init (server_scene.item_world.ammo_box)","",{"", {}, 0},"53 55 56 48 81 ec 60 01 00 00 48 8b 9c 24 a0 01 00 00 48 8b 84 24 a8 01 00 00 48 89 8c 24 90 00",1720u,6};
// The game's own debug teleport for a peer (as AnyHelpers' admin tab uses it): F6 test key.
// () server.on_event (server, server_peer.data, client_peer.data.event.peer_teleport) | side: server
inline constexpr func_desc server_on_teleport{"() server.on_event (server, server_peer.data, client_peer.data.event.peer_teleport)","53 55 48 81 ec 98 00 00 00 48 89 4c 24 20 48 89 54 24 28 4c 89 44 24 30 48 8b 82 98 06 00 00 48",{"53 55 48 81 ec 68 17 00 00 48 89 8c 24 48 16 00 00 48 89 94 24 50 16 00 00 4c 89 84 24 58 16 00", {51392u}, 1},"",0u,-1};
// ref<client> g_client (as AnyMap's map_actors.h finds it): F2 test key reaches client_scene through it.
inline constexpr anymaker::global_desc g_client{"g_client","48 83 ec 38 48 89 4c 24 20 48 89 54 24 28 48 8b 05 4b 00 00 00 48 83 78 08 00 0f 95 c0 3c 00 0f",96u};
} // namespace anycareer::bind
