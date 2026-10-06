#pragma once
#include <cstdint>
enum AnySessionMode:uint32_t {ANY_MODE_UNKNOWN=0,ANY_MODE_SURVIVAL=1,ANY_MODE_CAREER=2,ANY_MODE_CREATIVE=3,ANY_MODE_SANDBOX=4};
constexpr uint32_t ANY_MODE_MASK_CREATIVE_SANDBOX=(1u<<ANY_MODE_CREATIVE)|(1u<<ANY_MODE_SANDBOX);
struct AnySessionStateV1 {uint32_t struct_size{sizeof(AnySessionStateV1)},version{1};uint32_t mode{ANY_MODE_UNKNOWN},reserved{};uint64_t sampled_tick{};char native_mode_name[32]{};};
// Copied, recent native session mode. False/UNKNOWN must never grant Add permission.
struct AnySessionV1 {uint32_t struct_size{sizeof(AnySessionV1)},version{1};bool (*copy)(AnySessionStateV1*){};};
