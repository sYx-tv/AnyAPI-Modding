#pragma once
#include <cstdint>
enum AnyInventorySide:uint32_t {ANY_INVENTORY_PLAYER=0,ANY_INVENTORY_STORAGE=1};
struct AnyInventoryUiFrameV1 {
 uint32_t struct_size{sizeof(AnyInventoryUiFrameV1)},version{1};uint64_t context{},tick{};
 uint32_t player_items{},storage_items{},player_grids{},storage_grids{};char storage_name[128]{};
};
struct AnyStoredItemV1 {
 uint32_t struct_size{sizeof(AnyStoredItemV1)},version{1};uint64_t token{},grid{};
 uint32_t side{};int32_t quantity{},capacity{},x{},y{},width{},height{},rotation{};
 char definition[128]{},name[128]{};
};
struct AnyStorageGridV1 {
 uint32_t struct_size{sizeof(AnyStorageGridV1)},version{1};uint64_t token{};
 uint32_t side{};int32_t width{},height{};uint32_t fixed_slots{},allow_rotation{};
};
// Frame-scoped native inventory extensions. No game-owned pointer is exposed.
// Item/grid tokens are stable only in this world and inspected-storage context.
// Mutation calls submit one native event per callback frame; false submits nothing.
// Native replication on following frames is the acknowledgement, not submission alone.
struct AnyInventoryUiV1 {
 uint32_t struct_size{sizeof(AnyInventoryUiV1)},version{1};
 bool (*register_section)(const char* id,void(*draw)(const AnyInventoryUiFrameV1*,void*),void* user){};
 uint32_t (*available)(){};
 bool (*copy_item)(uint32_t side,uint32_t index,AnyStoredItemV1*){};
 bool (*copy_grid)(uint32_t side,uint32_t index,AnyStorageGridV1*){};
 bool (*begin_row)(const char* id,uint32_t columns){};void (*end_row)(){};
 uint32_t (*button)(const char* id,const char* text,uint32_t disabled){};
 bool (*same_type)(uint64_t a,uint64_t b){};bool (*can_stack)(uint64_t source,uint64_t destination){};
 bool (*stack)(uint64_t source,uint64_t destination){};
 bool (*quick_store)(uint64_t item,uint32_t destination){};
 bool (*accepts)(uint64_t item,uint64_t grid,int32_t rotation){}; // filter/rotation, ignores occupancy
 bool (*can_move)(uint64_t item,uint64_t grid,int32_t x,int32_t y,int32_t rotation){};
 bool (*move)(uint64_t item,uint64_t grid,int32_t x,int32_t y,int32_t rotation){};
};
