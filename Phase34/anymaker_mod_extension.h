#pragma once
#include <cstddef>
#include <cstdint>

// Separate export; the 34-slot AnymakerModContextV16 is unchanged.
// No raw game address appears in this extension. Copies remain historical
// observations; validate a token again before requesting another copy.
enum AnyExtResult : uint32_t {
    ANY_EXT_OK, ANY_EXT_BAD_ARGUMENT, ANY_EXT_UNAVAILABLE,
    ANY_EXT_STALE_TOKEN, ANY_EXT_BUFFER_TOO_SMALL, ANY_EXT_RESYNC_REQUIRED
};
enum AnyObjectKind : uint32_t { ANY_OBJECT_ACTOR=1, ANY_OBJECT_COMPONENT=2 };
struct AnyObjectTokenV1 {
    uint64_t world_epoch, object_generation, object_id;
    uint32_t kind, reserved;
};
enum AnyExtCapability : uint32_t {
    ANY_EXT_OBJECT_TOKENS=1, ANY_EXT_COPIED_ACTORS=2,
    ANY_EXT_COPIED_COMPONENTS=4, ANY_EXT_HANDHELD_CACHE=8,
    ANY_EXT_INVALIDATION_JOURNAL=16
};
struct AnyExtensionInfoV1 {
    uint32_t struct_size, version, capabilities, native_observers_ready;
    uint64_t world_epoch;
    uint32_t session_role; // 0 = UNKNOWN; not inferred from handler activity.
    uint32_t external_mod_loading; // Remains zero.
};
struct AnyActorSnapshotV1 {
    uint32_t struct_size, version;
    AnyObjectTokenV1 token;
    uint64_t valid_fields, sampled_tick;
    int32_t actor_id;
    uint32_t flags;
    double position[3], orientation[2], linear_velocity[3];
};
struct AnyComponentSnapshotV1 {
    uint32_t struct_size, version;
    AnyObjectTokenV1 token;
    uint32_t side, class_name_valid;
    char class_name[128];
    // No vehicle address or continuous transform is implied.
};
struct AnyHandheldSnapshotV1 {
    uint32_t struct_size, version;
    uint64_t world_epoch, sampled_tick, observation_revision;
    uint32_t side, readable, empty, definition_valid;
    uint32_t quantity_valid, item_identity_valid; // Both zero until native proof.
    uint64_t quantity;
    char definition_id[128], definition_name[192], definition_class[128];
    // Exactly ONE handheld observation, not a complete inventory enumeration.
};
enum AnyInvalidationKind : uint32_t { ANY_OBJECT_RETIRED=1, ANY_WORLD_RETIRED=2 };
struct AnyInvalidationV1 {
    uint64_t sequence;
    uint32_t reason, reserved;
    AnyObjectTokenV1 token;
};
struct AnymakerModExtensionV1 {
    uint32_t struct_size, version;
    AnyExtResult (*get_info)(AnyExtensionInfoV1*, size_t);
    AnyExtResult (*enumerate_tokens)(uint32_t, AnyObjectTokenV1*, size_t, size_t*, size_t*);
    AnyExtResult (*validate_token)(AnyObjectTokenV1);
    AnyExtResult (*get_actor_snapshot)(AnyObjectTokenV1, AnyActorSnapshotV1*, size_t);
    AnyExtResult (*get_component_snapshot)(AnyObjectTokenV1, AnyComponentSnapshotV1*, size_t);
    AnyExtResult (*get_handheld_snapshot)(uint32_t, AnyHandheldSnapshotV1*, size_t);
    AnyExtResult (*read_invalidations)(uint64_t, AnyInvalidationV1*, size_t, size_t*, uint64_t*);
};
// GetProcAddress(loader, "AnymakerGetExtensionV1")(1, sizeof(...)).
// Returned function table is static/process-lifetime. This does not enable
// external mod discovery, callback unregister, unload, native calls or mutation.
using AnymakerGetExtensionV1Fn=const AnymakerModExtensionV1* (*)(uint32_t,size_t);
static_assert(sizeof(AnyObjectTokenV1)==32);
