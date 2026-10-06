#pragma once
#include <cstdint>
// Requests use stable authored IDs. Native objects remain private to the host.
enum AnyInventoryRequestState:uint32_t {ANY_ITEM_REQUEST_UNKNOWN=0,ANY_ITEM_REQUEST_QUEUED=1,ANY_ITEM_REQUEST_SENT=2,ANY_ITEM_REQUEST_EXPIRED=3,ANY_ITEM_REQUEST_FAILED=4};
struct AnyInventoryActionsV1 {
 uint32_t struct_size{sizeof(AnyInventoryActionsV1)},version{1};
 uint32_t (*available)(){};
 // Queue one native item instance; server authority decides creation/placement.
 // 0 means not queued. SENT confirms event dispatch, not server success.
 uint64_t (*add_one)(const char* definition_id){};
 uint32_t (*state)(uint64_t ticket){};
};
