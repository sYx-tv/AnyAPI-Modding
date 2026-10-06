#pragma once
#include <cstdint>
enum AnyBuildStatus : uint32_t { ANY_BUILD_UNCHECKED=0, ANY_BUILD_MATCH=1, ANY_BUILD_MISMATCH=2 };
struct AnyBuildInfoV1 {
    uint32_t struct_size{sizeof(AnyBuildInfoV1)}, version{1};
    uint32_t status{}, api_revision{27};
    char api_version[24]{}, game_version[24]{}, steam_build[24]{};
    char executable_sha256[65]{}, game_data_sha256[65]{};
};
// Exact checked file identity, copied under a lock; no game pointers. Querying
// availability never implies a particular hook is resolved or gameplay-validated.
struct AnyBuildV1 {
    uint32_t struct_size{sizeof(AnyBuildV1)}, version{1};
    bool (*copy)(AnyBuildInfoV1*){};
};
