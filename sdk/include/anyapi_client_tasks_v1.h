#pragma once
#include <cstdint>
// Runs on the native client actor tick, after its original call. This is not a
// server queue. Execution requires an active client world and may pause in menus.
struct AnyClientTaskFrameV1 {
    uint32_t struct_size{sizeof(AnyClientTaskFrameV1)}, version{1};
    uint64_t world_epoch{}, tick{};
};
struct AnyClientTasksV1 {
    uint32_t struct_size{sizeof(AnyClientTasksV1)}, version{1};
    uint64_t (*register_owner)(){}; // ModInit/Ready or an owned mod callback
    uint64_t (*post)(uint64_t owner, uint64_t expected_world_epoch,
        void (*callback)(const AnyClientTaskFrameV1*, void*), void* user){};
    bool (*cancel)(uint64_t owner, uint64_t ticket){};
};
// Zero epoch accepts the next active world. A nonzero mismatching epoch discards
// the task. Post returns zero when the bounded queue is full or owner is invalid.
// Cancel succeeds only before dispatch; user data must remain alive until execution
// or successful cancellation. Callbacks must be short and never block the game.
// Stale/faulted-owner jobs are dropped without a callback. Retain process-lifetime
// user data, or successfully cancel before releasing it. See client-tasks.md.
