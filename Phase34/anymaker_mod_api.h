#pragma once
#include <stdint.h>
#include <stddef.h>

#ifdef _WIN32
#define ANYMAKER_MOD_EXPORT extern "C" __declspec(dllexport)
#else
#define ANYMAKER_MOD_EXPORT extern "C"
#endif

#define ANYMAKER_MOD_API_VERSION 16u
#define ANYMAKER_ITEM_LIFECYCLE_EVENT_VERSION 2u
#define ANYMAKER_DIGITAL_ACTION_EVENT_VERSION 2u
#define ANYMAKER_SERVER_ITEM_USE_EVENT_VERSION 2u
#define ANYMAKER_LOCAL_PLAYER_STATE_VERSION 2u
#define ANYMAKER_VEHICLE_COMPONENT_EVENT_VERSION 1u
#define ANYMAKER_API_INVALID_SIZE ((size_t)-1)

#define ANYMAKER_ITEM_ID_SNAPSHOT_MAX 128u
#define ANYMAKER_ITEM_NAME_SNAPSHOT_MAX 192u
#define ANYMAKER_ITEM_CLASS_SNAPSHOT_MAX 128u
#define ANYMAKER_COMPONENT_CLASS_SNAPSHOT_MAX 128u

enum AnymakerLogLevel : uint32_t {
    ANY_LOG_INFO  = 0,
    ANY_LOG_WARN  = 1,
    ANY_LOG_ERROR = 2,
    ANY_LOG_DEBUG = 3
};

enum AnymakerItemSide : uint32_t {
    ANY_ITEM_SIDE_CLIENT = 1,
    ANY_ITEM_SIDE_SERVER = 2
};

enum AnymakerItemLifecycleKind : uint32_t {
    ANY_ITEM_CREATED   = 1,
    ANY_ITEM_REMOVED   = 2,
    ANY_ITEM_DESTROYED = 3
};

enum AnymakerDigitalActionPhase : uint32_t {
    ANY_DIGITAL_ACTION_BEGIN = 1,
    ANY_DIGITAL_ACTION_END   = 2
};

enum AnymakerServerItemUseKind : uint32_t {
    ANY_SERVER_ITEM_USE_SELF      = 1,
    ANY_SERVER_ITEM_USE_ACTOR     = 2,
    ANY_SERVER_ITEM_USE_VEHICLE   = 3,
    ANY_SERVER_ITEM_USE_TRANSFORM = 4
};


enum AnymakerVehicleComponentSide : uint32_t {
    ANY_COMPONENT_SIDE_CLIENT = 1,
    ANY_COMPONENT_SIDE_SERVER = 2
};

enum AnymakerVehicleComponentLifecycleKind : uint32_t {
    ANY_COMPONENT_CREATED   = 1,
    ANY_COMPONENT_DESTROYED = 2
};

enum AnymakerPlayerStateValid : uint64_t {
    ANY_PLAYER_VALID_POSITION          = 1ull << 0,
    ANY_PLAYER_VALID_ORIENTATION       = 1ull << 1,
    ANY_PLAYER_VALID_LINEAR_VELOCITY   = 1ull << 2,
    ANY_PLAYER_VALID_INCAPACITATED     = 1ull << 3,
    ANY_PLAYER_VALID_SWIMMING          = 1ull << 4,
    ANY_PLAYER_VALID_ACTIVE_ITEM       = 1ull << 5,
    ANY_PLAYER_VALID_HANDHELD_ITEM     = 1ull << 6,
    ANY_PLAYER_VALID_STAMINA           = 1ull << 7,
    ANY_PLAYER_VALID_HUNGER            = 1ull << 8,
    ANY_PLAYER_VALID_THIRST            = 1ull << 9,
    ANY_PLAYER_VALID_OXYGEN            = 1ull << 10,
    ANY_PLAYER_VALID_WETNESS           = 1ull << 11,
    ANY_PLAYER_VALID_INFECTION         = 1ull << 12,
    ANY_PLAYER_VALID_TEMPERATURE_COLD  = 1ull << 13,
    ANY_PLAYER_VALID_TEMPERATURE_HOT   = 1ull << 14
};

typedef void (*AnymakerLogFn)(
    AnymakerLogLevel level,
    const char* mod_id,
    const char* message
);

typedef uintptr_t AnymakerItemDefinitionHandle;
typedef uintptr_t AnymakerItemHandle;
typedef uintptr_t AnymakerActorHandle;
typedef uintptr_t AnymakerClientSceneHandle;
typedef uintptr_t AnymakerServerHandle;
typedef uintptr_t AnymakerServerPeerDataHandle;
typedef uintptr_t AnymakerServerActorHandle;
typedef uintptr_t AnymakerServerItemHandle;
typedef uintptr_t AnymakerVehicleComponentHandle;
typedef uintptr_t AnymakerVehicleHandle;

struct AnymakerItemLifecycleEventV2 {
    uint32_t struct_size;
    uint32_t event_version;
    uint64_t sequence;

    AnymakerItemSide side;
    AnymakerItemLifecycleKind kind;

    AnymakerItemDefinitionHandle definition;
    AnymakerItemHandle item;
    uintptr_t georef_word0;

    char definition_id[ANYMAKER_ITEM_ID_SNAPSHOT_MAX];
    char definition_name[ANYMAKER_ITEM_NAME_SNAPSHOT_MAX];
    char definition_class[ANYMAKER_ITEM_CLASS_SNAPSHOT_MAX];
};

typedef void (*AnymakerItemLifecycleCallback)(
    const AnymakerItemLifecycleEventV2* event,
    void* user_data
);

typedef bool (*AnymakerRegisterItemLifecycleFn)(
    const char* mod_id,
    AnymakerItemLifecycleCallback callback,
    void* user_data
);

struct AnymakerDigitalActionEventV2 {
    uint32_t struct_size;
    uint32_t event_version;
    uint64_t sequence;

    AnymakerDigitalActionPhase phase;
    int32_t action_id;
    uint32_t handled_by_game;

    AnymakerActorHandle actor;
    AnymakerClientSceneHandle client_scene;
};

typedef void (*AnymakerDigitalActionCallback)(
    const AnymakerDigitalActionEventV2* event,
    void* user_data
);

typedef bool (*AnymakerRegisterDigitalActionFn)(
    const char* mod_id,
    AnymakerDigitalActionCallback callback,
    void* user_data
);

// Server-authoritative item-use request. V2 resolves the numeric event item_id
// to the actual server item + definition BEFORE the game processes the use.
struct AnymakerServerItemUseEventV2 {
    uint32_t struct_size;
    uint32_t event_version;
    uint64_t sequence;

    AnymakerServerItemUseKind kind;

    int32_t actor_id;
    int32_t item_id;
    int32_t action;
    int32_t target_id;
    int32_t component_id;
    int32_t data;

    AnymakerServerHandle server;
    AnymakerServerPeerDataHandle peer_data;

    uint32_t item_resolved;
    uint32_t reserved0;

    AnymakerServerActorHandle server_actor;
    AnymakerServerItemHandle server_item;
    AnymakerItemDefinitionHandle definition;

    char definition_id[ANYMAKER_ITEM_ID_SNAPSHOT_MAX];
    char definition_name[ANYMAKER_ITEM_NAME_SNAPSHOT_MAX];
    char definition_class[ANYMAKER_ITEM_CLASS_SNAPSHOT_MAX];
};

typedef void (*AnymakerServerItemUseCallback)(
    const AnymakerServerItemUseEventV2* event,
    void* user_data
);

typedef bool (*AnymakerRegisterServerItemUseFn)(
    const char* mod_id,
    AnymakerServerItemUseCallback callback,
    void* user_data
);

struct AnymakerPlayerItemSnapshotV1 {
    AnymakerItemHandle item;
    AnymakerItemDefinitionHandle definition;

    char definition_id[ANYMAKER_ITEM_ID_SNAPSHOT_MAX];
    char definition_name[ANYMAKER_ITEM_NAME_SNAPSHOT_MAX];
    char definition_class[ANYMAKER_ITEM_CLASS_SNAPSHOT_MAX];
};

struct AnymakerLocalPlayerStateV2 {
    uint32_t struct_size;
    uint32_t state_version;
    uint64_t sequence;
    uint64_t valid_fields;

    AnymakerActorHandle actor;
    AnymakerClientSceneHandle client_scene;

    // World-space state.
    double position[3];
    double orientation[2];
    double linear_velocity[3];

    uint32_t incapacitated;
    uint32_t swimming;

    // Character survival/movement values from the game's own getters.
    double stamina;
    double hunger;
    double thirst;
    double oxygen;
    double wetness;
    double infection;
    double temperature_cold;
    double temperature_hot;

    AnymakerPlayerItemSnapshotV1 active_item;
    AnymakerPlayerItemSnapshotV1 handheld_item;
};

typedef void (*AnymakerLocalPlayerStateCallback)(
    const AnymakerLocalPlayerStateV2* state,
    void* user_data
);

typedef bool (*AnymakerRegisterLocalPlayerStateFn)(
    const char* mod_id,
    AnymakerLocalPlayerStateCallback callback,
    void* user_data
);

typedef bool (*AnymakerGetLocalPlayerStateFn)(
    AnymakerLocalPlayerStateV2* out
);


// Vehicle-component lifecycle.
//
// create_component_class supplies the behavior class identity (for example
// "wheel", "engine", "seat"). The framework associates that identity with
// the actual component object, then emits CREATED once the base component is
// attached to a vehicle. DESTROYED is emitted after the game's destroy path.
//
// The event carries its own class-name snapshot, so destroyed callbacks never
// need to dereference stale game memory.
struct AnymakerVehicleComponentEventV1 {
    uint32_t struct_size;
    uint32_t event_version;
    uint64_t sequence;

    AnymakerVehicleComponentSide side;
    AnymakerVehicleComponentLifecycleKind kind;

    AnymakerVehicleComponentHandle component;
    AnymakerVehicleHandle vehicle;

    char class_name[ANYMAKER_COMPONENT_CLASS_SNAPSHOT_MAX];
};

typedef void (*AnymakerVehicleComponentCallback)(
    const AnymakerVehicleComponentEventV1* event,
    void* user_data
);

typedef bool (*AnymakerRegisterVehicleComponentFn)(
    const char* mod_id,
    AnymakerVehicleComponentCallback callback,
    void* user_data
);

// Registry-backed accessors. These never require mods to dereference the
// opaque component pointer.
typedef bool (*AnymakerIsVehicleComponentLiveFn)(
    AnymakerVehicleComponentHandle component
);

typedef AnymakerVehicleHandle (*AnymakerGetVehicleComponentVehicleFn)(
    AnymakerVehicleComponentHandle component
);

typedef size_t (*AnymakerCopyVehicleComponentClassFn)(
    AnymakerVehicleComponentHandle component,
    char* out,
    size_t out_size
);

typedef size_t (*AnymakerCopyDefinitionStringFn)(
    AnymakerItemDefinitionHandle definition,
    char* out,
    size_t out_size
);

// -----------------------------------------------------------------------------
// Runtime API v16 generic UI/render surface.
// -----------------------------------------------------------------------------
//
// Phase 26 retains the Phase 23 generic UI boundary: world-map/render-probe helpers
// remain out of the PUBLIC context. The framework privately owns reverse-engineered
// internals; mods receive generic rendering, mod-action, actor-state, and resource APIs.
// The example world map remains a normal external test mod, not framework behavior.
//
// UI coordinates use a top-left origin:
//   x = 0 is the left edge, x increases right
//   y = 0 is the top edge,  y increases down
// The logical viewport dimensions are supplied on every render callback.

#define ANYMAKER_UI_RENDER_FRAME_VERSION 1u
#define ANYMAKER_UI_API_INFO_VERSION 1u

// Current generic texture resolver backend can resolve these engine-owned
// resources by normal game asset path. Unknown paths return false. Future
// backends may support arbitrary texture assets without changing this API.
#define ANYMAKER_TEXTURE_PATH_MAP_BACKGROUND "textures/map_background.txtr"
#define ANYMAKER_TEXTURE_PATH_MAP_TREES      "textures/map_trees.txtr"
#define ANYMAKER_TEXTURE_PATH_MAP_LINES      "textures/map_lines.txtr"

typedef uintptr_t AnymakerUiTextureHandle;

// Packed as 0xAABBGGRR so the low byte is red. Use ANYMAKER_RGBA8 rather than
// depending on the integer layout directly.
#define ANYMAKER_RGBA8(r,g,b,a) \
    ((uint32_t)(((uint32_t)(r) & 0xffu)       | \
                (((uint32_t)(g) & 0xffu)<<8) | \
                (((uint32_t)(b) & 0xffu)<<16)| \
                (((uint32_t)(a) & 0xffu)<<24)))

enum AnymakerUiPanelFlags : uint32_t { ANY_UI_PANEL_INVENTORY = 1u << 0 };

struct AnymakerUiRenderFrameV1 {
    uint32_t struct_size;
    uint32_t frame_version;
    uint64_t frame_id;

    // Friendly top-left coordinate space exposed to mods.
    double viewport_width;
    double viewport_height;

    // Game UI logical cell size, useful for scale-aware widgets.
    double ui_cell_size;

    // Appended in Phase26 v2; check struct_size before reading.
    uint32_t visible_panels;
    uint32_t reserved_panel_flags;
};

typedef void (*AnymakerUiRenderCallback)(
    const AnymakerUiRenderFrameV1* frame,
    void* user_data
);

typedef bool (*AnymakerRegisterUiRenderFn)(
    const char* mod_id,
    AnymakerUiRenderCallback callback,
    void* user_data
);

typedef bool (*AnymakerUiDrawRectFn)(
    const AnymakerUiRenderFrameV1* frame,
    double x,
    double y,
    double width,
    double height,
    uint32_t rgba8
);

typedef bool (*AnymakerUiDrawTextureFn)(
    const AnymakerUiRenderFrameV1* frame,
    AnymakerUiTextureHandle texture,
    double x,
    double y,
    double width,
    double height,
    uint32_t rgba8
);

typedef bool (*AnymakerResolveUiTextureFn)(
    const char* asset_path_utf8,
    AnymakerUiTextureHandle* out_texture
);

// Diagnostic/raw physical-key polling retained for compatibility. New mods should
// prefer Runtime API v16 registered mod actions below. The framework owns physical
// key polling and persistence; external mods receive semantic action events.
typedef bool (*AnymakerIsVirtualKeyDownFn)(uint32_t virtual_key);

struct AnymakerUiApiInfoV1 {
    uint32_t struct_size;
    uint32_t info_version;

    uint32_t ui_hook_ready;
    uint32_t rectangle_ready;
    uint32_t texture_ready;
    uint32_t registered_callbacks;

    double last_viewport_width;
    double last_viewport_height;
    double ui_cell_size;

    uint64_t render_frames;
    uint64_t callback_invocations;
    uint64_t rectangle_draws;
    uint64_t texture_draws;
    uint64_t texture_resolve_ok;
    uint64_t texture_resolve_fail;
    uint64_t rejected_draws;
    uint64_t callback_exceptions;
    uint64_t key_queries;
};

typedef bool (*AnymakerGetUiApiInfoFn)(AnymakerUiApiInfoV1* out);


// -----------------------------------------------------------------------------
// Runtime API v16 registered mod actions.
// -----------------------------------------------------------------------------
//
// Actions are named per mod, have a default Windows virtual-key code, and are
// persisted by the framework in mods\anymaker_mod_bindings.tsv. External mods
// never call Win32 input APIs. Runtime API v16 adds a framework-owned
// "Mod Controls" section inside Anymaker's existing Controls screen. Registered
// actions appear as native UI buttons and can be clicked to capture a new key.
// A dedicated top-level Mod Controls tab remains a future routing/UI refinement;
// the section keeps game-menu mutation narrow while proving native rebinding.

#define ANYMAKER_MOD_ACTION_EVENT_VERSION 1u
#define ANYMAKER_MOD_ACTION_API_INFO_VERSION 2u
#define ANYMAKER_MOD_ACTION_ID_MAX 96u
#define ANYMAKER_MOD_ACTION_LABEL_MAX 128u

enum AnymakerModActionPhase : uint32_t {
    ANY_MOD_ACTION_BEGIN = 1,
    ANY_MOD_ACTION_END   = 2
};

struct AnymakerModActionEventV1 {
    uint32_t struct_size;
    uint32_t event_version;
    uint64_t sequence;

    AnymakerModActionPhase phase;
    uint32_t virtual_key;

    char action_id[ANYMAKER_MOD_ACTION_ID_MAX];
    char display_name[ANYMAKER_MOD_ACTION_LABEL_MAX];
};

typedef void (*AnymakerModActionCallback)(
    const AnymakerModActionEventV1* event,
    void* user_data
);

typedef bool (*AnymakerRegisterModActionFn)(
    const char* mod_id,
    const char* action_id,
    const char* display_name,
    uint32_t default_virtual_key,
    AnymakerModActionCallback callback,
    void* user_data
);

typedef bool (*AnymakerGetModActionBindingFn)(
    const char* mod_id,
    const char* action_id,
    uint32_t* out_virtual_key
);

typedef bool (*AnymakerSetModActionBindingFn)(
    const char* mod_id,
    const char* action_id,
    uint32_t virtual_key
);

struct AnymakerModActionApiInfoV1 {
    uint32_t struct_size;
    uint32_t info_version;

    uint32_t registered_actions;
    uint32_t controls_menu_hook_ready;
    uint32_t controls_menu_recently_seen;
    uint32_t controls_section_ready;

    uint32_t controls_section_visible;
    uint32_t rebind_capture_active;
    uint32_t controls_section_mode; // 1 = dedicated section inside Controls
    uint32_t reserved0;

    uint64_t begin_events;
    uint64_t end_events;
    uint64_t binding_file_loads;
    uint64_t binding_file_writes;
    uint64_t controls_section_frames;
    uint64_t controls_button_clicks;
    uint64_t rebind_commits;
    uint64_t rebind_cancels;
    uint64_t controls_ui_exceptions;
};

typedef bool (*AnymakerGetModActionApiInfoFn)(AnymakerModActionApiInfoV1* out);

// -----------------------------------------------------------------------------
// Runtime API v16 client actor registry/state.
// -----------------------------------------------------------------------------
//
// Handles are opaque. The framework tracks the client actor container lifecycle
// and exposes snapshots instead of requiring mods to dereference game memory.
// Position is sourced from client_scene.actor.get_transform. Orientation and
// velocity are additionally populated for the known local player when available.

#define ANYMAKER_CLIENT_ACTOR_STATE_VERSION 1u
#define ANYMAKER_CLIENT_ACTOR_EVENT_VERSION 1u
#define ANYMAKER_CLIENT_ACTOR_API_INFO_VERSION 2u

enum AnymakerClientActorValid : uint64_t {
    ANY_CLIENT_ACTOR_VALID_ID              = 1ull << 0,
    ANY_CLIENT_ACTOR_VALID_POSITION        = 1ull << 1,
    ANY_CLIENT_ACTOR_VALID_ORIENTATION     = 1ull << 2,
    ANY_CLIENT_ACTOR_VALID_LINEAR_VELOCITY = 1ull << 3
};

enum AnymakerClientActorFlags : uint32_t {
    ANY_CLIENT_ACTOR_FLAG_LOCAL_PLAYER = 1u << 0
};

enum AnymakerClientActorLifecycleKind : uint32_t {
    ANY_CLIENT_ACTOR_CREATED   = 1,
    ANY_CLIENT_ACTOR_DESTROYED = 2
};

struct AnymakerClientActorStateV1 {
    uint32_t struct_size;
    uint32_t state_version;
    uint64_t sequence;
    uint64_t valid_fields;

    AnymakerActorHandle actor;
    int32_t actor_id;
    uint32_t flags;

    double position[3];
    double orientation[2];
    double linear_velocity[3];
};

struct AnymakerClientActorLifecycleEventV1 {
    uint32_t struct_size;
    uint32_t event_version;
    uint64_t sequence;

    AnymakerClientActorLifecycleKind kind;
    uint32_t reserved0;
    AnymakerClientActorStateV1 state;
};

typedef void (*AnymakerClientActorLifecycleCallback)(
    const AnymakerClientActorLifecycleEventV1* event,
    void* user_data
);

typedef bool (*AnymakerRegisterClientActorLifecycleFn)(
    const char* mod_id,
    AnymakerClientActorLifecycleCallback callback,
    void* user_data
);

typedef bool (*AnymakerClientActorEnumerateCallback)(
    const AnymakerClientActorStateV1* state,
    void* user_data
);

typedef size_t (*AnymakerEnumerateClientActorsFn)(
    AnymakerClientActorEnumerateCallback callback,
    void* user_data
);

typedef bool (*AnymakerGetClientActorStateFn)(
    AnymakerActorHandle actor,
    AnymakerClientActorStateV1* out
);

typedef bool (*AnymakerIsClientActorLiveFn)(AnymakerActorHandle actor);

struct AnymakerClientActorApiInfoV1 {
    uint32_t struct_size;
    uint32_t info_version;
    uint32_t registry_live;
    uint32_t registered_callbacks;

    uint64_t created_captured;
    uint64_t destroyed_captured;
    uint64_t events_dispatched;
    uint64_t events_dropped;
    uint64_t state_queries;
    uint64_t state_position_ok;
    uint64_t stale_prune_checks;
    uint64_t stale_pruned;
    uint64_t synthetic_destroyed;
};

typedef bool (*AnymakerGetClientActorApiInfoFn)(AnymakerClientActorApiInfoV1* out);

// Phase 26: synchronous authoritative inventory scope. New handles are tokens,
// valid only during the server pulse callback that produced them. Never store
// them for a later callback/thread. The framework rejects expired scopes.
typedef uint64_t AnymakerInventoryScope;
typedef uint64_t AnymakerInventoryItemToken;
struct AnymakerInventoryPulseV1 {
    uint32_t struct_size;
    uint32_t event_version;
    uint64_t monotonic_ms;
    AnymakerInventoryScope inventory;
    int32_t actor_id;
    uint32_t reserved;
};
struct AnymakerInventoryItemV1 {
    uint32_t struct_size;
    uint32_t snapshot_version;
    AnymakerInventoryItemToken item;
    int32_t item_id;
    uint32_t reserved;
    char definition_id[ANYMAKER_ITEM_ID_SNAPSHOT_MAX];
    char definition_class[ANYMAKER_ITEM_CLASS_SNAPSHOT_MAX];
};
enum AnymakerInventoryResult : uint32_t {
    ANY_INVENTORY_OK=0,
    ANY_INVENTORY_UNAVAILABLE=1,
    ANY_INVENTORY_STALE=2,
    ANY_INVENTORY_INCOMPATIBLE=3,
    ANY_INVENTORY_NATIVE_EXCEPTION=4,
    ANY_INVENTORY_TRUNCATED=5,
    ANY_INVENTORY_NATIVE_RETURNED=6,
    ANY_INVENTORY_SOURCE_CONSUMED=7
};
// NATIVE_RETURNED means the native event returned, not proof of a quantity change.
// SOURCE_CONSUMED additionally proves that the source no longer resolves.
typedef void (*AnymakerInventoryPulseCallback)(const AnymakerInventoryPulseV1*,void*);
typedef bool (*AnymakerRegisterInventoryPulseFn)(const char*,AnymakerInventoryPulseCallback,void*);
typedef AnymakerInventoryResult (*AnymakerEnumerateInventoryFn)(
    AnymakerInventoryScope,AnymakerInventoryItemV1*,size_t,size_t*);
typedef AnymakerInventoryResult (*AnymakerCanMergeInventoryFn)(
    AnymakerInventoryScope,AnymakerInventoryItemToken,AnymakerInventoryItemToken,bool*);
typedef AnymakerInventoryResult (*AnymakerRequestInventoryMergeFn)(
    AnymakerInventoryScope,AnymakerInventoryItemToken destination,AnymakerInventoryItemToken source);

struct AnymakerModContextV15 {
    uint32_t struct_size;
    uint32_t api_version;

    const char* game_directory_utf8;
    const char* framework_directory_utf8;

    AnymakerLogFn log;

    AnymakerRegisterItemLifecycleFn register_item_lifecycle;
    AnymakerRegisterDigitalActionFn register_digital_action;
    AnymakerRegisterServerItemUseFn register_server_item_use;

    AnymakerRegisterLocalPlayerStateFn register_local_player_state;
    AnymakerGetLocalPlayerStateFn get_local_player_state;

    AnymakerRegisterVehicleComponentFn register_vehicle_component;
    AnymakerIsVehicleComponentLiveFn is_vehicle_component_live;
    AnymakerGetVehicleComponentVehicleFn get_vehicle_component_vehicle;
    AnymakerCopyVehicleComponentClassFn copy_vehicle_component_class;

    // Generic UI/render API. No world-map semantics live here.
    AnymakerRegisterUiRenderFn register_ui_render;
    AnymakerUiDrawRectFn ui_draw_rect;
    AnymakerUiDrawTextureFn ui_draw_texture;
    AnymakerResolveUiTextureFn resolve_ui_texture;
    AnymakerIsVirtualKeyDownFn is_virtual_key_down;
    AnymakerGetUiApiInfoFn get_ui_api_info;

    // Registered mod actions. Prefer these over raw key polling.
    AnymakerRegisterModActionFn register_mod_action;
    AnymakerGetModActionBindingFn get_mod_action_binding;
    AnymakerSetModActionBindingFn set_mod_action_binding;
    AnymakerGetModActionApiInfoFn get_mod_action_api_info;

    // Generic client actor lifecycle/enumeration/state.
    AnymakerRegisterClientActorLifecycleFn register_client_actor_lifecycle;
    AnymakerEnumerateClientActorsFn enumerate_client_actors;
    AnymakerGetClientActorStateFn get_client_actor_state;
    AnymakerIsClientActorLiveFn is_client_actor_live;
    AnymakerGetClientActorApiInfoFn get_client_actor_api_info;

    AnymakerCopyDefinitionStringFn copy_item_definition_id;
    AnymakerCopyDefinitionStringFn copy_item_definition_name;
    AnymakerCopyDefinitionStringFn copy_item_definition_description;
    AnymakerCopyDefinitionStringFn copy_item_definition_class;
    AnymakerCopyDefinitionStringFn copy_item_definition_mesh_file;
};

struct AnymakerModContextV16 {
    uint32_t struct_size;
    uint32_t api_version;

    const char* game_directory_utf8;
    const char* framework_directory_utf8;

    AnymakerLogFn log;

    AnymakerRegisterItemLifecycleFn register_item_lifecycle;
    AnymakerRegisterDigitalActionFn register_digital_action;
    AnymakerRegisterServerItemUseFn register_server_item_use;

    AnymakerRegisterLocalPlayerStateFn register_local_player_state;
    AnymakerGetLocalPlayerStateFn get_local_player_state;

    AnymakerRegisterVehicleComponentFn register_vehicle_component;
    AnymakerIsVehicleComponentLiveFn is_vehicle_component_live;
    AnymakerGetVehicleComponentVehicleFn get_vehicle_component_vehicle;
    AnymakerCopyVehicleComponentClassFn copy_vehicle_component_class;

    // Generic UI/render API. No world-map semantics live here.
    AnymakerRegisterUiRenderFn register_ui_render;
    AnymakerUiDrawRectFn ui_draw_rect;
    AnymakerUiDrawTextureFn ui_draw_texture;
    AnymakerResolveUiTextureFn resolve_ui_texture;
    AnymakerIsVirtualKeyDownFn is_virtual_key_down;
    AnymakerGetUiApiInfoFn get_ui_api_info;

    // Registered mod actions. Prefer these over raw key polling.
    AnymakerRegisterModActionFn register_mod_action;
    AnymakerGetModActionBindingFn get_mod_action_binding;
    AnymakerSetModActionBindingFn set_mod_action_binding;
    AnymakerGetModActionApiInfoFn get_mod_action_api_info;

    // Generic client actor lifecycle/enumeration/state.
    AnymakerRegisterClientActorLifecycleFn register_client_actor_lifecycle;
    AnymakerEnumerateClientActorsFn enumerate_client_actors;
    AnymakerGetClientActorStateFn get_client_actor_state;
    AnymakerIsClientActorLiveFn is_client_actor_live;
    AnymakerGetClientActorApiInfoFn get_client_actor_api_info;

    AnymakerCopyDefinitionStringFn copy_item_definition_id;
    AnymakerCopyDefinitionStringFn copy_item_definition_name;
    AnymakerCopyDefinitionStringFn copy_item_definition_description;
    AnymakerCopyDefinitionStringFn copy_item_definition_class;
    AnymakerCopyDefinitionStringFn copy_item_definition_mesh_file;

    AnymakerRegisterInventoryPulseFn register_inventory_pulse;
    AnymakerEnumerateInventoryFn enumerate_inventory;
    AnymakerCanMergeInventoryFn can_merge_inventory_items;
    AnymakerRequestInventoryMergeFn request_inventory_merge;
};

typedef bool (*AnymakerModInitFn)(const AnymakerModContextV16* ctx);
typedef void (*AnymakerModShutdownFn)();

static_assert(offsetof(AnymakerModContextV16,register_inventory_pulse)==sizeof(AnymakerModContextV15),"v16 must append to the v15 prefix");
