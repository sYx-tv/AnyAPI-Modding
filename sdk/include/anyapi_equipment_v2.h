#pragma once
#include "anyapi_equipment_v1.h"
constexpr uint32_t ANY_EQUIPMENT_TOOLS_CAPACITY=64;
struct AnyEquipmentToolV2 {
 int32_t item_id{-1};uint32_t reserved{};
 char definition_id[128]{},name[192]{};
};
struct AnyEquipmentToolsSnapshotV2 {
 uint32_t struct_size{sizeof(AnyEquipmentToolsSnapshotV2)},version{2},count{},reserved{};
 uint64_t context{},sampled_tick{};
 AnyEquipmentToolV2 tools[ANY_EQUIPMENT_TOOLS_CAPACITY]{};
};
// One entry per construction-tool definition in the local carried inventory.
// Equip uses native replicated hotbar assignment and one shared, non-destructive slot.
// APPLIED follows the replicated slot/item match and native local selection.
struct AnyEquipmentV2 {
 uint32_t struct_size{sizeof(AnyEquipmentV2)},version{2};
 bool (*copy)(AnyEquipmentToolsSnapshotV2*){};
 uint64_t (*equip)(uint64_t context,int32_t expected_item_id){};
 uint32_t (*state)(uint64_t ticket){};
};
