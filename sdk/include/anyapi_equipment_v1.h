#pragma once
#include <cstdint>
constexpr uint32_t ANY_EQUIPMENT_CAPACITY=33;
struct AnyEquipmentSlotV1 {
 int32_t slot_index{-1},item_id{-1};
 char definition_id[128]{},name[192]{};
};
struct AnyEquipmentSnapshotV1 {
 uint32_t struct_size{sizeof(AnyEquipmentSnapshotV1)},version{1},count{},reserved{};
 uint64_t context{},sampled_tick{};
 int32_t selected_slot{-1};uint32_t padding{};
 AnyEquipmentSlotV1 slots[ANY_EQUIPMENT_CAPACITY]{};
};
enum AnyEquipmentRequestStateV1:uint32_t {ANY_EQUIPMENT_UNKNOWN=0,ANY_EQUIPMENT_QUEUED,ANY_EQUIPMENT_APPLIED,ANY_EQUIPMENT_EXPIRED,ANY_EQUIPMENT_FAILED};
// Copied local hotbar slots, including -1 for the primary-hand / empty-hands slot.
// Requests execute once on the native player thread and revalidate the item ID.
// APPLIED means native client selection, not a separate server acknowledgement.
struct AnyEquipmentV1 {
 uint32_t struct_size{sizeof(AnyEquipmentV1)},version{1};
 bool (*copy)(AnyEquipmentSnapshotV1*){};
 uint64_t (*select)(uint64_t context,int32_t slot_index,int32_t expected_item_id){};
 uint32_t (*state)(uint64_t ticket){};
};
