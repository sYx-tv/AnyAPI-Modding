#pragma once
// ThirdPerson game bindings, generated for Anymaker 0.1.28 / Steam build 26277967 with
//   python tools/bind.py --reference reference --signature "<signature>" --out <file>.hpp
// run against the 0.1.25 SDK reference, one call per signature below; re-bound for 0.1.27 by
// native/tools/game_update.py.
// Experimental: located by code signature, not reviewed as an AnyAPI service.
// Regenerate and re-test after any game update.
#include "anymaker_sdk_runtime.hpp"
namespace thirdperson::bind {
using anymaker::func_desc;
using anymaker::global_desc;
// (mat34) client_scene.get_camera_transform (const client_scene) | side: client; threads: main, worker
// Virtual slot 16 of client_scene; the cell hook covers direct and virtual calls.
inline constexpr func_desc client_scene_get_camera_transform{"(mat34) client_scene.get_camera_transform (const client_scene)","53 55 56 48 81 ec e0 01 00 00 48 89 8c 24 80 00 00 00 48 89 94 24 88 00 00 00 48 8d 84 24 90 00",{"", {}, 0},"53 48 83 ec 30 48 89 4c 24 20 48 8d 01 48 8b 15 cc 08 00 00 48 89 4c 24 28 48 89 c1 ff 12 48 8b",2280u,16};
// (ptr<client_scene.actor>) client_scene.actor.container.get_peer_actor (client_scene.actor.container) | side: client
// The local player's actor; get_camera_transform calls it the same way.
inline constexpr func_desc actor_container_get_peer_actor{"(ptr<client_scene.actor>) client_scene.actor.container.get_peer_actor (client_scene.actor.container)","48 83 ec 18 48 89 0c 24 48 89 54 24 08 48 83 7a 30 00 0f 95 c0 3c 00 0f 84 0c 00 00 00 48 8b 42",{"", {}, 0},"48 83 ec 38 48 89 4c 24 20 48 8d 01 48 8b 15 4d 00 00 00 48 89 4c 24 28 48 89 c1 ff 12 48 8b 05 44 00 00 00 48 8b 4c 24 28 48 89 01 48 c7 41 30",104u,21};
// bool client_scene.actor.g_is_debug_render_first_person_head: client_scene.actor_character.render hides the local
// player's head unless this is set. Anchored on actor_character.render's relocation slot 29.
inline constexpr global_desc g_render_first_person_head{"client_scene.actor.g_is_debug_render_first_person_head","53 55 56 57 41 54 41 55 41 56 41 57 48 81 ec 28 0c 00 00 4c 8b ac 24 90 0c 00 00 48 8b 84 24 98",15488u};
} // namespace thirdperson::bind
