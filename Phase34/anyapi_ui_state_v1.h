#pragma once
#include <cstdint>
enum AnyUiKind:uint32_t {ANY_UI_UNKNOWN=0,ANY_UI_GAMEPLAY=1,ANY_UI_INVENTORY=2,ANY_UI_MENU=3};
struct AnyUiSnapshotV1 {uint32_t struct_size{sizeof(AnyUiSnapshotV1)},version{1};int32_t native_state{-1};uint32_t kind{ANY_UI_UNKNOWN};};
// Reads the current native UI state, independently of player simulation updates.
// False or UNKNOWN means unavailable: callers should hide UI and release input.
struct AnyUiStateV1 {uint32_t struct_size{sizeof(AnyUiStateV1)},version{1};bool (*copy)(AnyUiSnapshotV1*){};};
