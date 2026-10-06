#pragma once
#include <cstdint>
#include <cstddef>
// Copied session-player transport. No native pointer or rendering policy.
constexpr uint32_t ANYAPI_PLAYERS_VERSION=1,ANYAPI_PLAYERS_CAPACITY=64;
enum : uint32_t { PLAYER_POSITION=1,PLAYER_FACING=2,PLAYER_NAME=4,PLAYER_STEAM_ID=8,PLAYER_LOCAL=16 };
struct AnySessionPlayerV1 {
    int32_t peer_id{},actor_id{};
    uint32_t valid_fields{},reserved{};
    // World look heading: yaw 0 faces +Z; +pi/2 faces +X, radians.
    // Includes the body transform and character-local look yaw.
    double position[3]{},yaw_radians{};
    char steam_name[256]{},steam_id[32]{};
};
struct AnySessionPlayersV1 {
    uint32_t struct_size{sizeof(AnySessionPlayersV1)},version{1},count{},dropped{};
    uint64_t sampled_tick{},world_epoch{};
    AnySessionPlayerV1 players[ANYAPI_PLAYERS_CAPACITY]{};
};
struct AnyPlayersChannelV1 {
    uint32_t magic{0x41504c59},version{1},struct_size{sizeof(AnyPlayersChannelV1)},reserved{};
    alignas(8) volatile int64_t sequence{};
    AnySessionPlayersV1 snapshot{};
};
static_assert(offsetof(AnyPlayersChannelV1,sequence)%8==0);

