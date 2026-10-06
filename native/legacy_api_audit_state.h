#pragma once
#include <atomic>
#include <cstddef>
struct P29ApiAuditRow {
 std::atomic<unsigned long long> guard_checks{},guard_failures{},functional_checks{},functional_failures{};
 void guard(bool ok){guard_checks.fetch_add(1);if(!ok)guard_failures.fetch_add(1);}
 void functional(bool ok){functional_checks.fetch_add(1);if(!ok)functional_failures.fetch_add(1);}
 const char* status(bool quarantined) const {
  if(guard_failures.load() || functional_failures.load())return "FAILED";
  if(quarantined)return guard_checks.load()?"QUARANTINED_GUARD_CHECKED":"QUARANTINED_NOT_CALLED";
  if(functional_checks.load())return "EXERCISED_CHECKED_PARTIAL";
  return guard_checks.load()?"GUARD_ONLY":"NOT_CALLED";
 }
};
static constexpr const char* P29_API_NAMES[]={
 "log",
 "register_item_lifecycle",
 "register_digital_action",
 "register_server_item_use",
 "register_local_player_state",
 "get_local_player_state",
 "register_vehicle_component",
 "is_vehicle_component_live",
 "get_vehicle_component_vehicle",
 "copy_vehicle_component_class",
 "register_ui_render",
 "ui_draw_rect",
 "ui_draw_texture",
 "resolve_ui_texture",
 "is_virtual_key_down",
 "get_ui_api_info",
 "register_mod_action",
 "get_mod_action_binding",
 "set_mod_action_binding",
 "get_mod_action_api_info",
 "register_client_actor_lifecycle",
 "enumerate_client_actors",
 "get_client_actor_state",
 "is_client_actor_live",
 "get_client_actor_api_info",
 "copy_item_definition_id",
 "copy_item_definition_name",
 "copy_item_definition_description",
 "copy_item_definition_class",
 "copy_item_definition_mesh_file",
 "register_inventory_pulse",
 "enumerate_inventory",
 "can_merge_inventory_items",
 "request_inventory_merge"
};
static_assert(sizeof(P29_API_NAMES)/sizeof(*P29_API_NAMES)==34);
