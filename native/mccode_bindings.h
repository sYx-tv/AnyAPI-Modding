#pragma once
// MCCode game bindings, generated for Anymaker 0.1.25 / Steam build 25904830 with
//   python tools/bind.py --reference . --signature "<signature>" --out <file>.hpp
// run from the SDK reference root (reference-0.1.25), one --signature per binding below.
// Experimental: located by code signature, call-cell route or typeinfo slot, not reviewed as an AnyAPI service.
// Regenerate and re-test after any game update.
#include "anymaker_sdk_runtime.hpp"
namespace mccode::bind {
using anymaker::func_desc;
// () frontend_ui_microcontroller.update_ui (frontend_ui_microcontroller, frontend_ui, client, client.ui_data, const client_peer.inspection_request, microcontroller, client_scene, vector<client.ui_data.control_hint>) | side: client
inline constexpr func_desc client_microcontroller_update_ui{"() frontend_ui_microcontroller.update_ui (frontend_ui_microcontroller, frontend_ui, client, client.ui_data, const client_peer.inspection_request, client_scene.peer.inspection_data.microcontroller, client_scene, vector<client.ui_data.control_hint>)","53 55 56 57 41 54 41 55 41 56 41 57 48 81 ec 88 01 00 00 4c 8b a4 24 f0 01 00 00 4c 8b 94 24 f8",{"53 55 56 57 41 54 41 55 48 81 ec 98 04 00 00 4c 8b a4 24 f0 04 00 00 4c 8b 94 24 f8 04 00 00 4c", {6816u}, 1},"",0u,-1};
// () client.on_network_event (client, application.network.client_event, messages_server_to_client.microcontroller_save_script_data) | side: client
inline constexpr func_desc client_on_save_script_data{"() client.on_network_event (client, application.network.client_event, messages_server_to_client.microcontroller_save_script_data)","53 48 81 ec 90 01 00 00 48 89 8c 24 d8 00 00 00 48 89 94 24 e0 00 00 00 4c 89 84 24 e8 00 00 00 48 8d 84 24 08 01 00 00 48 8d 15 0d 03 00 00 48 8b 1d 12 03 00 00 48 89 4c 24 28 48 89 c1 4c 89 84 24 a8 00 00 00 ff 13 48 8d 84 24 18 01 00 00 48 8b 0d f9 02 00 00 48 89 84 24 b0 00 00 00 48 89 8c 24 b8 00 00 00 48 8b 84 24 b8 00 00 00 48 8b 8c 24 b0 00 00 00 ff 10 48 8d 94 24 08 01 00 00 4c 8b 84 24 a8 00 00 00 49 8d 40 08 48 8b 0d c4 02 00 00 48 89 84 24 c0 00 00 00 48 89 8c 24 c8 00 00 00 48 8b 84 24 c8 00 00 00 48 8b 8c 24 b0 00 00 00 4c 8b 84 24 c0 00 00 00 ff 10 48 8d 84 24 28 01 00 00 48 8d 15 78",{"53 55 48 81 ec 68 04 00 00 48 89 8c 24 00 03 00 00 48 89 94 24 08 03 00 00 48 8d 84 24 10 03 00", {4520u}, 1},"",0u,-1};
// () client_peer.data.push_event_microcontroller_load (client_peer.data, const s32, const s32, const string) | side: client
inline constexpr func_desc push_load{"() client_peer.data.push_event_microcontroller_load (client_peer.data, const s32, const s32, const string)","",{"53 55 56 57 41 54 48 81 ec c0 08 00 00 4c 8b 94 24 10 09 00 00 4c 8b a4 24 18 09 00 00 48 89 8c", {9792u}, 1},"",0u,-1};
// () client_peer.data.push_event_microcontroller_save (client_peer.data, const s32, const s32, const string) | side: client
inline constexpr func_desc push_save{"() client_peer.data.push_event_microcontroller_save (client_peer.data, const s32, const s32, const string)","",{"53 55 56 57 41 54 48 81 ec 10 06 00 00 4c 8b 9c 24 60 06 00 00 4c 8b a4 24 68 06 00 00 48 89 8c", {6720u}, 1},"",0u,-1};
// () client_peer.data.push_event_microcontroller_pause (client_peer.data, const s32, const s32) | side: client
inline constexpr func_desc push_pause{"() client_peer.data.push_event_microcontroller_pause (client_peer.data, const s32, const s32)","",{"53 55 56 57 41 54 41 55 41 56 48 81 ec 40 05 00 00 4c 8b 9c 24 a0 05 00 00 48 89 8c 24 a0 04 00", {7544u}, 1},"",0u,-1};
// () client_peer.data.push_event_microcontroller_resume (client_peer.data, const s32, const s32) | side: client
inline constexpr func_desc push_resume{"() client_peer.data.push_event_microcontroller_resume (client_peer.data, const s32, const s32)","",{"53 55 56 57 41 54 41 55 41 56 48 81 ec 40 05 00 00 4c 8b 9c 24 a0 05 00 00 48 89 8c 24 a0 04 00", {7456u}, 1},"",0u,-1};
// () client_peer.data.push_event_microcontroller_step_in (client_peer.data, const s32, const s32) | side: client
inline constexpr func_desc push_step_in{"() client_peer.data.push_event_microcontroller_step_in (client_peer.data, const s32, const s32)","",{"53 55 56 57 41 54 41 55 41 56 48 81 ec 40 05 00 00 4c 8b 9c 24 a0 05 00 00 48 89 8c 24 a0 04 00", {7472u}, 1},"",0u,-1};
// () client_peer.data.push_event_microcontroller_step_over (client_peer.data, const s32, const s32) | side: client
inline constexpr func_desc push_step_over{"() client_peer.data.push_event_microcontroller_step_over (client_peer.data, const s32, const s32)","",{"53 55 56 57 41 54 41 55 41 56 48 81 ec 40 05 00 00 4c 8b 9c 24 a0 05 00 00 48 89 8c 24 a0 04 00", {7464u}, 1},"",0u,-1};
// () client_peer.data.push_event_microcontroller_step_out (client_peer.data, const s32, const s32) | side: client
inline constexpr func_desc push_step_out{"() client_peer.data.push_event_microcontroller_step_out (client_peer.data, const s32, const s32)","",{"53 55 56 57 41 54 41 55 41 56 48 81 ec 40 05 00 00 4c 8b 9c 24 a0 05 00 00 48 89 8c 24 a0 04 00", {7480u}, 1},"",0u,-1};
// () client_peer.data.push_event_microcontroller_toggle_breakpoint (client_peer.data, const s32, const s32, const s32) | side: client
inline constexpr func_desc push_toggle_breakpoint{"() client_peer.data.push_event_microcontroller_toggle_breakpoint (client_peer.data, const s32, const s32, const s32)","",{"53 55 56 57 41 54 41 55 41 56 41 57 48 81 ec 68 04 00 00 4c 8b ac 24 d0 04 00 00 48 8b bc 24 d8", {5104u}, 1},"",0u,-1};
// () client_peer.data.push_event_microcontroller_undo_action (client_peer.data, const s32, const s32) | side: client
inline constexpr func_desc push_undo{"() client_peer.data.push_event_microcontroller_undo_action (client_peer.data, const s32, const s32)","",{"53 55 56 57 48 81 ec c8 02 00 00 4c 8b 94 24 10 03 00 00 48 89 8c 24 40 02 00 00 48 89 94 24 48", {3152u}, 1},"",0u,-1};
// () microcontroller_debug_value_type_s32.replicate (microcontroller_debug_value_type_s32, replication.slave.replicator) | side: client
inline constexpr func_desc debug_s32_replicate{"() client_scene.peer.inspection_data.microcontroller_debug_value_type_s32.replicate (client_scene.peer.inspection_data.microcontroller_debug_value_type_s32, replication.slave.replicator)","",{"", {}, 0},"",0u,-1};
// () microcontroller_debug_value_type_f64.replicate (microcontroller_debug_value_type_f64, replication.slave.replicator) | side: client
inline constexpr func_desc debug_f64_replicate{"() client_scene.peer.inspection_data.microcontroller_debug_value_type_f64.replicate (client_scene.peer.inspection_data.microcontroller_debug_value_type_f64, replication.slave.replicator)","",{"", {}, 0},"",0u,-1};
// () microcontroller_debug_value_type_bool.replicate (microcontroller_debug_value_type_bool, replication.slave.replicator) | side: client
inline constexpr func_desc debug_bool_replicate{"() client_scene.peer.inspection_data.microcontroller_debug_value_type_bool.replicate (client_scene.peer.inspection_data.microcontroller_debug_value_type_bool, replication.slave.replicator)","",{"", {}, 0},"",0u,-1};
// () microcontroller_debug_value_type_string.replicate (microcontroller_debug_value_type_string, replication.slave.replicator) | side: client
inline constexpr func_desc debug_string_replicate{"() client_scene.peer.inspection_data.microcontroller_debug_value_type_string.replicate (client_scene.peer.inspection_data.microcontroller_debug_value_type_string, replication.slave.replicator)","",{"", {}, 0},"",0u,-1};
// () microcontroller_debug_value_type_list.replicate (microcontroller_debug_value_type_list, replication.slave.replicator) | side: client
inline constexpr func_desc debug_list_replicate{"() client_scene.peer.inspection_data.microcontroller_debug_value_type_list.replicate (client_scene.peer.inspection_data.microcontroller_debug_value_type_list, replication.slave.replicator)","",{"", {}, 0},"",0u,-1};
// () microcontroller_debug_value_type_object.replicate (microcontroller_debug_value_type_object, replication.slave.replicator) | side: client
inline constexpr func_desc debug_object_replicate{"() client_scene.peer.inspection_data.microcontroller_debug_value_type_object.replicate (client_scene.peer.inspection_data.microcontroller_debug_value_type_object, replication.slave.replicator)","",{"", {}, 0},"",0u,-1};
}  // namespace mccode::bind
