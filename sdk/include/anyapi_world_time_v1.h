#pragma once
#include <cstdint>
// Copied native day/night cycle; not the system clock. No calendar/day counter.
struct AnyWorldTimeSnapshotV1 {
 uint32_t struct_size{sizeof(AnyWorldTimeSnapshotV1)},version{1};
 double cycle_factor{}; // normalized [0,1): midnight=0, noon=0.5
 uint32_t seconds_since_midnight{},reserved{};
 uint64_t sampled_tick{};
};
struct AnyWorldTimeV1 {
 uint32_t struct_size{sizeof(AnyWorldTimeV1)},version{1};
 bool (*copy)(AnyWorldTimeSnapshotV1*){}; // false when unavailable or stale
};
