#pragma once
#include "anyapi_inventory_actions_v1.h"
#include "anyapi_session_v1.h"
// Caller owns mode policy. Host checks the mask both when queued and dispatched.
struct AnyInventoryActionsV2 {uint32_t struct_size{sizeof(AnyInventoryActionsV2)},version{2};const AnyInventoryActionsV1* v1{};uint64_t (*add_one)(const char* definition_id,uint32_t allowed_modes){};};
