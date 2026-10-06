#pragma once
#include <cstdint>
// Supplied by AnyHelpers.dll, never by base AnyAPI.
// Query anyhelpers.controls v1; modcontrols.bindings v1 is a compatibility alias.
struct ModControlActionV1 {
 uint32_t struct_size{sizeof(ModControlActionV1)},default_key{};
 const char* mod_id{};const char* mod_name{};const char* action_id{};const char* label{};
};
struct ModControlsV1 {
 uint32_t struct_size{sizeof(ModControlsV1)},version{1};
 uint64_t (*register_action)(const ModControlActionV1*){}; // zero means rejected
 uint32_t (*key)(uint64_t action){}; // committed Windows virtual key, zero=unbound
 bool (*key_name)(uint64_t action,char* output,uint32_t capacity){};
};
