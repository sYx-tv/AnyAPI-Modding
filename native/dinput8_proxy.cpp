#define NOMINMAX
#include <windows.h>
#include <tlhelp32.h>
#include <bcrypt.h>
#include "runtime_build_identity.h"
#include <unordered_map>
#include <iterator>
#include <guiddef.h>
#include <unknwn.h>
#include <algorithm>
#include <cstdint>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>
#include "anymaker_mod_api.h"
#include "legacy_signatures.h"
#include "legacy_semantic_targets.h"
#include "legacy_virtual_owners.h"
#include "legacy_hook_specs.h"
#include "native_pointer_pool.h"
#include "legacy_telemetry.inc"
#include "runtime_state.inc"
static void p29_update_actor_from_player(const AnymakerLocalPlayerStateV2& state);
static void p29_capture_component_transform(uintptr_t component,bool server);
static volatile LONG64 g_p27_hook_calls[std::size(P27_HOOK_SPECS)]{};
static volatile LONG g_p27_hook_installed[std::size(P27_HOOK_SPECS)]{};
static volatile LONG64 g_p27_protection_failures=0;
static uintptr_t g_p272_installed_targets[std::size(P27_HOOK_SPECS)]{};
static void p27_validate_actor_virtuals(void* actor);
static void p27_capture_actor_container(void* container);
static void p27_validate_server_virtuals(void* scene);
#include <intrin.h>

static HMODULE g_real=nullptr;
static std::vector<HMODULE> g_mods;
static std::filesystem::path g_game_dir;
static std::string g_game_dir_utf8;
static std::string g_framework_dir_utf8;
static AnymakerModContextV16 g_ctx{};
static LONG g_loader_started=0;
static bool g_p27_resolution_only=false;

using DirectInput8CreateFn = HRESULT (WINAPI*)(HINSTANCE,DWORD,REFIID,LPVOID*,LPUNKNOWN);
static DirectInput8CreateFn pDirectInput8Create=nullptr;

static std::filesystem::path module_dir() {
    wchar_t buf[32768];
    DWORD n=GetModuleFileNameW(nullptr,buf,32768);
    return std::filesystem::path(std::wstring(buf,n)).parent_path();
}
static std::string utf8(const std::filesystem::path& p) {
    auto w=p.wstring();
    if(w.empty()) return {};
    int n=WideCharToMultiByte(CP_UTF8,0,w.c_str(),(int)w.size(),nullptr,0,nullptr,nullptr);
    std::string s(n,'\0');
    WideCharToMultiByte(CP_UTF8,0,w.c_str(),(int)w.size(),s.data(),n,nullptr,nullptr);
    return s;
}
static SRWLOCK g_log_lock=SRWLOCK_INIT;
static void log_line(AnymakerLogLevel level,const char* mod,const char* message) {
    AcquireSRWLockExclusive(&g_log_lock);
    try {
        std::ofstream f(g_game_dir/"anymaker_modding.log",std::ios::app);
        f << "[" << (unsigned)level << "] [" << (mod?mod:"framework") << "] "
          << "[pid=" << GetCurrentProcessId() << "] "
          << (message?message:"") << "\n";
    } catch(...) {}
    ReleaseSRWLockExclusive(&g_log_lock);
}
static std::string hexptr(uintptr_t p) {
    char b[32]{};
    std::snprintf(b,sizeof(b),"0x%llX",(unsigned long long)p);
    return b;
}
static bool load_real() {
    if(g_real) return true;
    wchar_t sys[MAX_PATH];
    UINT n=GetSystemDirectoryW(sys,MAX_PATH);
    if(!n) return false;
    auto p=std::filesystem::path(std::wstring(sys,n))/"dinput8.dll";
    g_real=LoadLibraryW(p.c_str());
    if(!g_real) return false;
    pDirectInput8Create=(DirectInput8CreateFn)GetProcAddress(g_real,"DirectInput8Create");
    return pDirectInput8Create!=nullptr;
}

// -----------------------------------------------------------------------------
// Safe item-definition metadata accessors.
// -----------------------------------------------------------------------------

struct GeoString16 {
    uintptr_t data;
    uint32_t length;
    uint32_t capacity;
};

static bool safe_read_memory(uintptr_t address,void* out,size_t size) {
    if(!address || !out || !size) return false;
    SIZE_T got=0;
    return ReadProcessMemory(GetCurrentProcess(),(LPCVOID)address,out,size,&got) && got==size;
}

static bool safe_read_native(uintptr_t address,void* out,size_t size) {
    if(!address || !out || !size) return false;
    __try {
        std::memcpy(out,(const void*)address,size);
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

#include "legacy_native_probes.inc"

static size_t copy_geostring_object(
    uintptr_t string_object,
    char* out,
    size_t out_size
) {
    if(out && out_size) out[0]='\0';
    if(!string_object) return ANYMAKER_API_INVALID_SIZE;

    GeoString16 gs{};
    if(!safe_read_memory(string_object,&gs,sizeof(gs)))
        return ANYMAKER_API_INVALID_SIZE;

    constexpr uint32_t MAX_REASONABLE_STRING=1024u*1024u;
    if(gs.length>MAX_REASONABLE_STRING ||
       gs.capacity>MAX_REASONABLE_STRING ||
       gs.length>gs.capacity) {
        return ANYMAKER_API_INVALID_SIZE;
    }

    if(gs.length==0) {
        if(out && out_size) out[0]='\0';
        return 0;
    }

    if(!gs.data) return ANYMAKER_API_INVALID_SIZE;

    if(out && out_size) {
        size_t copy_n=std::min<size_t>((size_t)gs.length,out_size-1);
        if(copy_n && !safe_read_memory(gs.data,out,copy_n)) {
            out[0]='\0';
            return ANYMAKER_API_INVALID_SIZE;
        }
        out[copy_n]='\0';
    }

    return (size_t)gs.length;
}

static size_t copy_definition_string_at(
    AnymakerItemDefinitionHandle definition,
    uintptr_t offset,
    char* out,
    size_t out_size
) {
    if(out && out_size) out[0]='\0';
    if(!definition) return ANYMAKER_API_INVALID_SIZE;

    GeoString16 s{};
    if(!safe_read_memory((uintptr_t)definition+offset,&s,sizeof(s)))
        return ANYMAKER_API_INVALID_SIZE;

    constexpr uint32_t MAX_REASONABLE_STRING=1024u*1024u;
    if(s.length>MAX_REASONABLE_STRING || s.capacity>MAX_REASONABLE_STRING || s.length>s.capacity)
        return ANYMAKER_API_INVALID_SIZE;

    if(s.length==0) {
        if(out && out_size) out[0]='\0';
        return 0;
    }
    if(!s.data) return ANYMAKER_API_INVALID_SIZE;

    if(out && out_size) {
        size_t copy_n=std::min<size_t>((size_t)s.length,out_size-1);
        if(copy_n && !safe_read_memory(s.data,out,copy_n)) {
            out[0]='\0';
            return ANYMAKER_API_INVALID_SIZE;
        }
        out[copy_n]='\0';
    }
    return (size_t)s.length;
}

static size_t copy_item_definition_id(AnymakerItemDefinitionHandle d,char* o,size_t n) {
    return copy_definition_string_at(d,0x00,o,n);
}
static size_t copy_item_definition_name(AnymakerItemDefinitionHandle d,char* o,size_t n) {
    return copy_definition_string_at(d,0x10,o,n);
}
static size_t copy_item_definition_description(AnymakerItemDefinitionHandle d,char* o,size_t n) {
    return copy_definition_string_at(d,0x20,o,n);
}
static size_t copy_item_definition_class(AnymakerItemDefinitionHandle d,char* o,size_t n) {
    return copy_definition_string_at(d,0x30,o,n);
}
static size_t copy_item_definition_mesh_file(AnymakerItemDefinitionHandle d,char* o,size_t n) {
    return copy_definition_string_at(d,0x40,o,n);
}

// -----------------------------------------------------------------------------
// Public lifecycle callback registry.
// -----------------------------------------------------------------------------

struct LifecycleRegistration {
    const char* mod_id;
    AnymakerItemLifecycleCallback callback;
    void* user_data;
};

static constexpr LONG MAX_LIFECYCLE_CALLBACKS=32;
static LifecycleRegistration g_callbacks[MAX_LIFECYCLE_CALLBACKS]{};
static LONG g_callback_count=0;
static SRWLOCK g_callback_lock=SRWLOCK_INIT;

static bool register_item_lifecycle(
    const char* mod_id,
    AnymakerItemLifecycleCallback callback,
    void* user_data
) {
    if(!callback) return false;

    AcquireSRWLockExclusive(&g_callback_lock);
    if(g_callback_count>=MAX_LIFECYCLE_CALLBACKS) {
        ReleaseSRWLockExclusive(&g_callback_lock);
        log_line(ANY_LOG_ERROR,"framework","Too many item lifecycle callbacks registered.");
        return false;
    }

    LONG slot=g_callback_count++;
    g_callbacks[slot]={mod_id,callback,user_data};
    ReleaseSRWLockExclusive(&g_callback_lock);

    std::string m="Registered OnItemLifecycle callback";
    if(mod_id && *mod_id) m += std::string(" for ")+mod_id;
    log_line(ANY_LOG_INFO,"framework",m.c_str());
    return true;
}

// -----------------------------------------------------------------------------
// Queued event dispatch. Game hooks never call mod callbacks directly.
// -----------------------------------------------------------------------------

enum EventSlot : int {
    SLOT_CLIENT_CREATED=0,
    SLOT_CLIENT_REMOVED=1,
    SLOT_SERVER_CREATED=2,
    SLOT_SERVER_DESTROYED=3,
    SLOT_COUNT=4
};

struct DefinitionSnapshot {
    char id[ANYMAKER_ITEM_ID_SNAPSHOT_MAX]{};
    char name[ANYMAKER_ITEM_NAME_SNAPSHOT_MAX]{};
    char cls[ANYMAKER_ITEM_CLASS_SNAPSHOT_MAX]{};
    bool readable=false;
};

static DefinitionSnapshot capture_definition_snapshot(uintptr_t definition) {
    DefinitionSnapshot s{};
    if(!definition) return s;

    size_t a=copy_item_definition_id(definition,s.id,sizeof(s.id));
    size_t b=copy_item_definition_name(definition,s.name,sizeof(s.name));
    size_t c=copy_item_definition_class(definition,s.cls,sizeof(s.cls));
    char description[192]{},mesh[192]{};
    size_t description_size=copy_item_definition_description(definition,description,sizeof(description));
    size_t mesh_size=copy_item_definition_mesh_file(definition,mesh,sizeof(mesh));
    g_p29_api_audit[25].functional(a!=ANYMAKER_API_INVALID_SIZE);
    g_p29_api_audit[26].functional(b!=ANYMAKER_API_INVALID_SIZE);
    g_p29_api_audit[27].functional(description_size!=ANYMAKER_API_INVALID_SIZE);
    g_p29_api_audit[28].functional(c!=ANYMAKER_API_INVALID_SIZE);
    g_p29_api_audit[29].functional(mesh_size!=ANYMAKER_API_INVALID_SIZE);

    s.readable =
        a!=ANYMAKER_API_INVALID_SIZE &&
        b!=ANYMAKER_API_INVALID_SIZE &&
        c!=ANYMAKER_API_INVALID_SIZE;
    return s;
}
static void p33_capture_handheld_cache(bool server) {
    P29InventorySample sample{};
    AcquireSRWLockShared(&g_p29_inventory_sample_lock);sample=g_p29_inventory_sample;ReleaseSRWLockShared(&g_p29_inventory_sample_lock);
    if(sample.server!=server || sample.thread!=GetCurrentThreadId())return;
    AnyHandheldSnapshotV1 copy{};copy.struct_size=sizeof(copy);copy.version=1;
    copy.world_epoch=sample.revision;copy.sampled_tick=GetTickCount64();copy.side=server?1:0;
    copy.readable=sample.probe.header_readable;copy.empty=sample.probe.empty;
    if(sample.probe.definition_readable){auto definition=capture_definition_snapshot(sample.probe.definition);
        copy.definition_valid=definition.readable;
        std::strncpy(copy.definition_id,definition.id,sizeof(copy.definition_id)-1);
        std::strncpy(copy.definition_name,definition.name,sizeof(copy.definition_name)-1);
        std::strncpy(copy.definition_class,definition.cls,sizeof(copy.definition_class)-1);}
    // Quantity and item identity remain unknown; no guessed vector/count ABI.
    AcquireSRWLockExclusive(&g_p33_handheld_lock);
    uint64_t revision=g_p33_handheld[copy.side].observation_revision;
    if(revision!=UINT64_MAX){copy.observation_revision=revision+1;g_p33_handheld[copy.side]=copy;}
    else g_p33_handheld[copy.side]={};
    ReleaseSRWLockExclusive(&g_p33_handheld_lock);
}


struct LifecycleRecord {
    uint64_t sequence{};
    AnymakerItemSide side{};
    AnymakerItemLifecycleKind kind{};
    uintptr_t definition{};
    uintptr_t item{};
    uintptr_t georef_word0{};
    EventSlot slot{};
    DefinitionSnapshot snapshot{};
};

static constexpr LONG EVENT_CAPACITY=16384;
static LifecycleRecord g_events[EVENT_CAPACITY]{};
static LONG g_event_head=0;
static LONG g_event_tail=0;
static LONG g_event_count=0;
static SRWLOCK g_event_lock=SRWLOCK_INIT;

static volatile LONG64 g_next_sequence=0;
static volatile LONG64 g_captured[SLOT_COUNT]{};
static volatile LONG64 g_dispatched[SLOT_COUNT]{};
static volatile LONG64 g_dropped[SLOT_COUNT]{};

static volatile LONG64 g_client_create_nonzero=0;
static volatile LONG64 g_client_create_backlink_match=0;
static volatile LONG64 g_server_create_nonzero=0;
static volatile LONG64 g_server_create_backlink_match=0;
static volatile LONG64 g_client_remove_snapshot_ok=0;
static volatile LONG64 g_server_destroy_snapshot_ok=0;

static LONG64 read64(volatile LONG64* p) {
    return InterlockedCompareExchange64(p,0,0);
}

static void enqueue_event(
    EventSlot slot,
    AnymakerItemSide side,
    AnymakerItemLifecycleKind kind,
    uintptr_t definition,
    uintptr_t item,
    uintptr_t georef_word0,
    const DefinitionSnapshot& snapshot
) {
    uint64_t seq=(uint64_t)InterlockedIncrement64(&g_next_sequence);

    AcquireSRWLockExclusive(&g_event_lock);
    if(g_event_count>=EVENT_CAPACITY) {
        ReleaseSRWLockExclusive(&g_event_lock);
        InterlockedIncrement64(&g_dropped[slot]);
        return;
    }

    LifecycleRecord rec{};
    rec.sequence=seq;
    rec.side=side;
    rec.kind=kind;
    rec.definition=definition;
    rec.item=item;
    rec.georef_word0=georef_word0;
    rec.slot=slot;
    rec.snapshot=snapshot;

    g_events[g_event_tail]=rec;
    g_event_tail=(g_event_tail+1)%EVENT_CAPACITY;
    ++g_event_count;
    ReleaseSRWLockExclusive(&g_event_lock);

    InterlockedIncrement64(&g_captured[slot]);
}

static bool pop_event(LifecycleRecord& out) {
    AcquireSRWLockExclusive(&g_event_lock);
    if(g_event_count<=0) {
        ReleaseSRWLockExclusive(&g_event_lock);
        return false;
    }

    out=g_events[g_event_head];
    g_event_head=(g_event_head+1)%EVENT_CAPACITY;
    --g_event_count;
    ReleaseSRWLockExclusive(&g_event_lock);
    return true;
}

static void dispatch_event(const LifecycleRecord& rec) {
    LifecycleRegistration local[MAX_LIFECYCLE_CALLBACKS]{};
    LONG count=0;

    AcquireSRWLockShared(&g_callback_lock);
    count=g_callback_count;
    for(LONG i=0;i<count;++i) local[i]=g_callbacks[i];
    ReleaseSRWLockShared(&g_callback_lock);

    AnymakerItemLifecycleEventV2 ev{};
    ev.struct_size=sizeof(ev);
    ev.event_version=ANYMAKER_ITEM_LIFECYCLE_EVENT_VERSION;
    ev.sequence=rec.sequence;
    ev.side=rec.side;
    ev.kind=rec.kind;
    ev.definition=rec.definition;
    ev.item=rec.item;
    ev.georef_word0=rec.georef_word0;

    std::memcpy(ev.definition_id,rec.snapshot.id,sizeof(ev.definition_id));
    std::memcpy(ev.definition_name,rec.snapshot.name,sizeof(ev.definition_name));
    std::memcpy(ev.definition_class,rec.snapshot.cls,sizeof(ev.definition_class));

    for(LONG i=0;i<count;++i) {
        __try {
            local[i].callback(&ev,local[i].user_data);
        } __except(EXCEPTION_EXECUTE_HANDLER) {
            log_line(
                ANY_LOG_ERROR,
                local[i].mod_id?local[i].mod_id:"unknown_mod",
                "Exception escaped OnItemLifecycle callback; event skipped."
            );
        }
    }

    InterlockedIncrement64(&g_dispatched[rec.slot]);
}

// -----------------------------------------------------------------------------
// Digital-action callback registry + queue.
// -----------------------------------------------------------------------------

struct DigitalRegistration {
    const char* mod_id;
    AnymakerDigitalActionCallback callback;
    void* user_data;
};

static constexpr LONG MAX_DIGITAL_CALLBACKS=32;
static DigitalRegistration g_digital_callbacks[MAX_DIGITAL_CALLBACKS]{};
static LONG g_digital_callback_count=0;
static SRWLOCK g_digital_callback_lock=SRWLOCK_INIT;

static bool register_digital_action(
    const char* mod_id,
    AnymakerDigitalActionCallback callback,
    void* user_data
) {
    if(!callback) return false;

    AcquireSRWLockExclusive(&g_digital_callback_lock);
    if(g_digital_callback_count>=MAX_DIGITAL_CALLBACKS) {
        ReleaseSRWLockExclusive(&g_digital_callback_lock);
        log_line(ANY_LOG_ERROR,"framework","Too many digital action callbacks registered.");
        return false;
    }

    LONG slot=g_digital_callback_count++;
    g_digital_callbacks[slot]={mod_id,callback,user_data};
    ReleaseSRWLockExclusive(&g_digital_callback_lock);

    std::string m="Registered OnDigitalAction callback";
    if(mod_id && *mod_id) m += std::string(" for ")+mod_id;
    log_line(ANY_LOG_INFO,"framework",m.c_str());
    return true;
}

enum DigitalSlot : int {
    DIGITAL_BEGIN=0,
    DIGITAL_END=1,
    DIGITAL_SLOT_COUNT=2
};

struct DigitalRecord {
    uint64_t sequence{};
    AnymakerDigitalActionPhase phase{};
    int32_t action_id{};
    uint32_t handled{};
    uintptr_t actor{};
    uintptr_t scene{};
    DigitalSlot slot{};
};

static constexpr LONG DIGITAL_EVENT_CAPACITY=4096;
static DigitalRecord g_digital_events[DIGITAL_EVENT_CAPACITY]{};
static LONG g_digital_head=0;
static LONG g_digital_tail=0;
static LONG g_digital_count=0;
static SRWLOCK g_digital_event_lock=SRWLOCK_INIT;

static volatile LONG64 g_digital_captured[DIGITAL_SLOT_COUNT]{};
static volatile LONG64 g_digital_dispatched[DIGITAL_SLOT_COUNT]{};
static volatile LONG64 g_digital_dropped[DIGITAL_SLOT_COUNT]{};

static void enqueue_digital(
    DigitalSlot slot,
    AnymakerDigitalActionPhase phase,
    int32_t action_id,
    uint32_t handled,
    uintptr_t actor,
    uintptr_t scene
) {
    uint64_t seq=(uint64_t)InterlockedIncrement64(&g_next_sequence);

    AcquireSRWLockExclusive(&g_digital_event_lock);
    if(g_digital_count>=DIGITAL_EVENT_CAPACITY) {
        ReleaseSRWLockExclusive(&g_digital_event_lock);
        InterlockedIncrement64(&g_digital_dropped[slot]);
        return;
    }

    g_digital_events[g_digital_tail]={seq,phase,action_id,handled,actor,scene,slot};
    g_digital_tail=(g_digital_tail+1)%DIGITAL_EVENT_CAPACITY;
    ++g_digital_count;
    ReleaseSRWLockExclusive(&g_digital_event_lock);

    InterlockedIncrement64(&g_digital_captured[slot]);
}

static bool pop_digital(DigitalRecord& out) {
    AcquireSRWLockExclusive(&g_digital_event_lock);
    if(g_digital_count<=0) {
        ReleaseSRWLockExclusive(&g_digital_event_lock);
        return false;
    }

    out=g_digital_events[g_digital_head];
    g_digital_head=(g_digital_head+1)%DIGITAL_EVENT_CAPACITY;
    --g_digital_count;
    ReleaseSRWLockExclusive(&g_digital_event_lock);
    return true;
}

static void dispatch_digital(const DigitalRecord& rec) {
    DigitalRegistration local[MAX_DIGITAL_CALLBACKS]{};
    LONG count=0;

    AcquireSRWLockShared(&g_digital_callback_lock);
    count=g_digital_callback_count;
    for(LONG i=0;i<count;++i) local[i]=g_digital_callbacks[i];
    ReleaseSRWLockShared(&g_digital_callback_lock);

    AnymakerDigitalActionEventV2 ev{};
    ev.struct_size=sizeof(ev);
    ev.event_version=ANYMAKER_DIGITAL_ACTION_EVENT_VERSION;
    ev.sequence=rec.sequence;
    ev.phase=rec.phase;
    ev.action_id=rec.action_id;
    ev.handled_by_game=rec.handled;
    ev.actor=rec.actor;
    ev.client_scene=rec.scene;

    for(LONG i=0;i<count;++i) {
        __try {
            local[i].callback(&ev,local[i].user_data);
        } __except(EXCEPTION_EXECUTE_HANDLER) {
            log_line(
                ANY_LOG_ERROR,
                local[i].mod_id?local[i].mod_id:"unknown_mod",
                "Exception escaped OnDigitalAction callback; event skipped."
            );
        }
    }

    InterlockedIncrement64(&g_digital_dispatched[rec.slot]);
}

// -----------------------------------------------------------------------------
// Server item-use callback registry + queue.
// -----------------------------------------------------------------------------

struct ServerUseRegistration {
    const char* mod_id;
    AnymakerServerItemUseCallback callback;
    void* user_data;
};

static constexpr LONG MAX_SERVER_USE_CALLBACKS=32;
static ServerUseRegistration g_server_use_callbacks[MAX_SERVER_USE_CALLBACKS]{};
static LONG g_server_use_callback_count=0;
static SRWLOCK g_server_use_callback_lock=SRWLOCK_INIT;

static bool register_server_item_use(
    const char* mod_id,
    AnymakerServerItemUseCallback callback,
    void* user_data
) {
    if(!callback) return false;

    AcquireSRWLockExclusive(&g_server_use_callback_lock);
    if(g_server_use_callback_count>=MAX_SERVER_USE_CALLBACKS) {
        ReleaseSRWLockExclusive(&g_server_use_callback_lock);
        log_line(ANY_LOG_ERROR,"framework","Too many server item-use callbacks registered.");
        return false;
    }

    LONG slot=g_server_use_callback_count++;
    g_server_use_callbacks[slot]={mod_id,callback,user_data};
    ReleaseSRWLockExclusive(&g_server_use_callback_lock);

    std::string m="Registered OnServerItemUse callback";
    if(mod_id && *mod_id) m += std::string(" for ")+mod_id;
    log_line(ANY_LOG_INFO,"framework",m.c_str());
    return true;
}

enum ServerUseSlot : int {
    USE_SELF_SLOT=0,
    USE_ACTOR_SLOT=1,
    USE_VEHICLE_SLOT=2,
    USE_TRANSFORM_SLOT=3,
    SERVER_USE_SLOT_COUNT=4
};

struct ServerUseRecord {
    uint64_t sequence{};
    AnymakerServerItemUseKind kind{};
    int32_t actor_id{};
    int32_t item_id{};
    int32_t action{};
    int32_t target_id{-1};
    int32_t component_id{-1};
    int32_t data{};
    uintptr_t server{};
    uintptr_t peer_data{};
    uintptr_t server_actor{};
    uintptr_t server_item{};
    uintptr_t definition{};
    uint32_t item_resolved{};
    ServerUseSlot slot{};
    DefinitionSnapshot snapshot{};
};

static constexpr LONG SERVER_USE_EVENT_CAPACITY=4096;
static ServerUseRecord g_server_use_events[SERVER_USE_EVENT_CAPACITY]{};
static LONG g_server_use_head=0;
static LONG g_server_use_tail=0;
static LONG g_server_use_count=0;
static SRWLOCK g_server_use_event_lock=SRWLOCK_INIT;

static volatile LONG64 g_server_use_captured[SERVER_USE_SLOT_COUNT]{};
static volatile LONG64 g_server_use_dispatched[SERVER_USE_SLOT_COUNT]{};
static volatile LONG64 g_server_use_dropped[SERVER_USE_SLOT_COUNT]{};

using ServerActorGetInventoryFn=void (*)(uintptr_t* out_inventory,void* actor);
using ServerInventoryGetItemFn=void (*)(
    uintptr_t* out_item_ref,
    void* inventory,
    const int32_t* item_id,
    uintptr_t* out_item_world,
    uintptr_t* out_item_world_grid
);
using ServerActorGetByIdFn=void (*)(uintptr_t* out_actor,void* actor_container,const int32_t* actor_id);

static ServerActorGetInventoryFn g_server_actor_get_inventory=nullptr;
static ServerInventoryGetItemFn g_server_inventory_get_item=nullptr;
static ServerActorGetByIdFn g_server_actor_get_by_id=nullptr;

// MSVC forbids __try inside functions that contain C++ objects requiring
// unwinding (C2712). Keep all SEH-only game calls in tiny POD helpers.
static bool seh_server_actor_get_by_id(
    ServerActorGetByIdFn fn,
    uintptr_t* out_actor,
    void* actor_container,
    const int32_t* actor_id
) {
    if(!fn || !out_actor || !actor_id) return false;
    *out_actor=0;
    __try {
        fn(out_actor,actor_container,actor_id);
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        *out_actor=0;
        return false;
    }
}

static bool seh_server_actor_get_inventory(
    ServerActorGetInventoryFn fn,
    uintptr_t* out_inventory,
    void* actor
) {
    if(!fn || !out_inventory || !actor) return false;
    *out_inventory=0;
    __try {
        fn(out_inventory,actor);
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        *out_inventory=0;
        return false;
    }
}

static bool seh_server_inventory_get_item(
    ServerInventoryGetItemFn fn,
    uintptr_t* out_item_ref,
    void* inventory,
    const int32_t* item_id,
    uintptr_t* out_grid_container,
    uintptr_t* out_grid_element
) {
    if(!fn || !out_item_ref || !inventory || !item_id ||
       !out_grid_container || !out_grid_element) {
        return false;
    }

    *out_item_ref=0;
    *out_grid_container=0;
    *out_grid_element=0;

    __try {
        fn(
            out_item_ref,
            inventory,
            item_id,
            out_grid_container,
            out_grid_element
        );
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        *out_item_ref=0;
        *out_grid_container=0;
        *out_grid_element=0;
        return false;
    }
}

static volatile LONG64 g_server_use_resolve_attempts=0;
static volatile LONG64 g_server_use_peer_state_rpm_ok=0;
static volatile LONG64 g_server_use_peer_state_native_ok=0;
static volatile LONG64 g_server_use_peer_state_nonzero=0;
static volatile LONG64 g_server_use_peer_actor_rpm_ok=0;
static volatile LONG64 g_server_use_peer_actor_native_ok=0;
static volatile LONG64 g_server_use_peer_actor_nonzero=0;
static volatile LONG64 g_server_use_actor_chain_ok=0;
static volatile LONG64 g_server_use_actor_fallback_attempts=0;
static volatile LONG64 g_server_use_actor_fallback_ok=0;
static volatile LONG64 g_server_use_actor_resolved=0;
static volatile LONG64 g_server_use_actor_id_match=0;
static volatile LONG64 g_server_use_inventory_getter_ok=0;
static volatile LONG64 g_server_use_inventory_offset_match=0;
static volatile LONG64 g_server_use_lookup_call_ok=0;
static volatile LONG64 g_server_use_item_ref_direct_ok=0;
static volatile LONG64 g_server_use_item_resolved=0;
static volatile LONG64 g_server_use_definition_ok=0;
static volatile LONG64 g_server_use_local_active_match=0;
static volatile LONG64 g_server_use_local_handheld_match=0;
static volatile LONG64 g_server_use_probe_logs=0;

// Shared latest local-player snapshot. These globals must be declared before
// resolve_server_use_item(), which uses them only for a read-only client/server
// metadata cross-check.
static AnymakerLocalPlayerStateV2 g_latest_player_state{};
static bool g_have_latest_player_state=false;
static SRWLOCK g_latest_player_lock=SRWLOCK_INIT;

#include "legacy_server_use_resolution.inc"

static void enqueue_server_use(
    ServerUseSlot slot,
    AnymakerServerItemUseKind kind,
    int32_t actor_id,
    int32_t item_id,
    int32_t action,
    int32_t target_id,
    int32_t component_id,
    int32_t data,
    uintptr_t server,
    uintptr_t peer_data,
    const ServerUseResolvedItem& resolved
) {
    uint64_t seq=(uint64_t)InterlockedIncrement64(&g_next_sequence);

    AcquireSRWLockExclusive(&g_server_use_event_lock);
    if(g_server_use_count>=SERVER_USE_EVENT_CAPACITY) {
        ReleaseSRWLockExclusive(&g_server_use_event_lock);
        InterlockedIncrement64(&g_server_use_dropped[slot]);
        return;
    }

    ServerUseRecord rec{};
    rec.sequence=seq;
    rec.kind=kind;
    rec.actor_id=actor_id;
    rec.item_id=item_id;
    rec.action=action;
    rec.target_id=target_id;
    rec.component_id=component_id;
    rec.data=data;
    rec.server=server;
    rec.peer_data=peer_data;
    rec.server_actor=resolved.server_actor;
    rec.server_item=resolved.server_item;
    rec.definition=resolved.definition;
    rec.item_resolved=resolved.resolved?1u:0u;
    rec.slot=slot;
    rec.snapshot=resolved.snapshot;

    g_server_use_events[g_server_use_tail]=rec;
    g_server_use_tail=(g_server_use_tail+1)%SERVER_USE_EVENT_CAPACITY;
    ++g_server_use_count;
    ReleaseSRWLockExclusive(&g_server_use_event_lock);

    InterlockedIncrement64(&g_server_use_captured[slot]);
}

static bool pop_server_use(ServerUseRecord& out) {
    AcquireSRWLockExclusive(&g_server_use_event_lock);
    if(g_server_use_count<=0) {
        ReleaseSRWLockExclusive(&g_server_use_event_lock);
        return false;
    }

    out=g_server_use_events[g_server_use_head];
    g_server_use_head=(g_server_use_head+1)%SERVER_USE_EVENT_CAPACITY;
    --g_server_use_count;
    ReleaseSRWLockExclusive(&g_server_use_event_lock);
    return true;
}

static void dispatch_server_use(const ServerUseRecord& rec) {
    ServerUseRegistration local[MAX_SERVER_USE_CALLBACKS]{};
    LONG count=0;

    AcquireSRWLockShared(&g_server_use_callback_lock);
    count=g_server_use_callback_count;
    for(LONG i=0;i<count;++i) local[i]=g_server_use_callbacks[i];
    ReleaseSRWLockShared(&g_server_use_callback_lock);

    AnymakerServerItemUseEventV2 ev{};
    ev.struct_size=sizeof(ev);
    ev.event_version=ANYMAKER_SERVER_ITEM_USE_EVENT_VERSION;
    ev.sequence=rec.sequence;
    ev.kind=rec.kind;
    ev.actor_id=rec.actor_id;
    ev.item_id=rec.item_id;
    ev.action=rec.action;
    ev.target_id=rec.target_id;
    ev.component_id=rec.component_id;
    ev.data=rec.data;
    ev.server=rec.server;
    ev.peer_data=rec.peer_data;
    ev.item_resolved=rec.item_resolved;
    ev.server_actor=rec.server_actor;
    ev.server_item=rec.server_item;
    ev.definition=rec.definition;

    std::memcpy(ev.definition_id,rec.snapshot.id,sizeof(ev.definition_id));
    std::memcpy(ev.definition_name,rec.snapshot.name,sizeof(ev.definition_name));
    std::memcpy(ev.definition_class,rec.snapshot.cls,sizeof(ev.definition_class));

    for(LONG i=0;i<count;++i) {
        __try {
            local[i].callback(&ev,local[i].user_data);
        } __except(EXCEPTION_EXECUTE_HANDLER) {
            log_line(
                ANY_LOG_ERROR,
                local[i].mod_id?local[i].mod_id:"unknown_mod",
                "Exception escaped OnServerItemUse callback; event skipped."
            );
        }
    }

    InterlockedIncrement64(&g_server_use_dispatched[rec.slot]);
}
















// -----------------------------------------------------------------------------
// Local-player state API.
//
// Findings used here:
//   client_scene.actor.get_transform copies mat34 from actor + 0x188.
//   mat34 translation is at +0x48, so base actor world position is actor + 0x1D0.
//   actor_character orientation vec2 is at actor + 0x8E8.
//   active/handheld item getters return client item_world* through hidden output.
// -----------------------------------------------------------------------------

using PlayerTickFn=void (*)(void* actor,void* client,void* client_scene,
                            const double* arg4,const double* arg5,const void* arg6);
using GetTransformFn=void (*)(void* out_mat34,void* actor);
using GetItemPtrFn=void (*)(uintptr_t* out_item,void* actor);
using GetBoolFn=void (*)(uint8_t* out_value,void* actor);
using GetF64Fn=void (*)(double* out_value,void* actor);
using GetVec2Fn=void (*)(double* out_value,void* actor);
using GetVec3Fn=void (*)(double* out_value,void* actor);

static PlayerTickFn g_player_tick_original=nullptr;
static GetTransformFn g_get_transform=nullptr;
static GetItemPtrFn g_get_item_active=nullptr;
static GetItemPtrFn g_get_item_handheld=nullptr;
static GetBoolFn g_get_incapacitated=nullptr;
static GetBoolFn g_get_swimming=nullptr;

static GetVec3Fn g_get_linear_velocity=nullptr;
static GetVec2Fn g_get_orientation=nullptr;
static GetF64Fn g_get_hunger=nullptr;
static GetF64Fn g_get_infection=nullptr;
static GetF64Fn g_get_oxygen=nullptr;
static GetF64Fn g_get_stamina=nullptr;
static GetF64Fn g_get_temp_cold=nullptr;
static GetF64Fn g_get_temp_hot=nullptr;
static GetF64Fn g_get_thirst=nullptr;
static GetF64Fn g_get_wetness=nullptr;

static volatile LONG64 g_local_actor_hint=0;
static volatile LONG64 g_local_scene_hint=0;
static volatile LONG64 g_last_player_capture_ms=0;

struct PlayerStateRegistration {
    const char* mod_id;
    AnymakerLocalPlayerStateCallback callback;
    void* user_data;
};

static constexpr LONG MAX_PLAYER_STATE_CALLBACKS=32;
static PlayerStateRegistration g_player_callbacks[MAX_PLAYER_STATE_CALLBACKS]{};
static LONG g_player_callback_count=0;
static SRWLOCK g_player_callback_lock=SRWLOCK_INIT;

static bool register_local_player_state(
    const char* mod_id,
    AnymakerLocalPlayerStateCallback callback,
    void* user_data
) {
    if(!callback) return false;

    AcquireSRWLockExclusive(&g_player_callback_lock);
    if(g_player_callback_count>=MAX_PLAYER_STATE_CALLBACKS) {
        ReleaseSRWLockExclusive(&g_player_callback_lock);
        log_line(ANY_LOG_ERROR,"framework","Too many local-player-state callbacks registered.");
        return false;
    }

    LONG slot=g_player_callback_count++;
    g_player_callbacks[slot]={mod_id,callback,user_data};
    ReleaseSRWLockExclusive(&g_player_callback_lock);

    std::string m="Registered OnLocalPlayerState callback";
    if(mod_id && *mod_id) m += std::string(" for ")+mod_id;
    log_line(ANY_LOG_INFO,"framework",m.c_str());
    return true;
}

static constexpr LONG PLAYER_EVENT_CAPACITY=2048;
static P29SnapshotQueue<AnymakerLocalPlayerStateV2,PLAYER_EVENT_CAPACITY> g_p29_player_queue;
static SRWLOCK g_player_event_lock=SRWLOCK_INIT;


static volatile LONG64 g_player_captured=0;
static volatile LONG64 g_player_dispatched=0;
static volatile LONG64 g_player_dropped=0;

static volatile LONG64 g_player_position_read_ok=0;
static volatile LONG64 g_player_orientation_read_ok=0;
static volatile LONG64 g_player_transform_getter_ok=0;
static volatile LONG64 g_player_transform_matches=0;
static volatile LONG64 g_player_incapacitated_getter_ok=0;
static volatile LONG64 g_player_swimming_getter_ok=0;
static volatile LONG64 g_player_active_nonzero=0;
static volatile LONG64 g_player_active_definition_ok=0;
static volatile LONG64 g_player_handheld_nonzero=0;
static volatile LONG64 g_player_handheld_definition_ok=0;

static volatile LONG64 g_player_velocity_getter_ok=0;
static volatile LONG64 g_player_orientation_getter_ok=0;
static volatile LONG64 g_player_orientation_matches=0;
static volatile LONG64 g_player_stamina_getter_ok=0;
static volatile LONG64 g_player_hunger_getter_ok=0;
static volatile LONG64 g_player_thirst_getter_ok=0;
static volatile LONG64 g_player_oxygen_getter_ok=0;
static volatile LONG64 g_player_wetness_getter_ok=0;
static volatile LONG64 g_player_infection_getter_ok=0;
static volatile LONG64 g_player_temp_cold_getter_ok=0;
static volatile LONG64 g_player_temp_hot_getter_ok=0;

static bool get_local_player_state(AnymakerLocalPlayerStateV2* out) {
    if(!out) return false;
    *out={};

    AcquireSRWLockShared(&g_latest_player_lock);
    bool ok=g_have_latest_player_state;
    if(ok) *out=g_latest_player_state;
    ReleaseSRWLockShared(&g_latest_player_lock);
    return ok;
}

static void fill_player_item_snapshot(
    uintptr_t item,
    AnymakerPlayerItemSnapshotV1& out
) {
    out={};
    out.item=item;
    if(!item) return;

    uintptr_t definition=0;
    if(!safe_read_memory(item+0x28,&definition,sizeof(definition)) || !definition)
        return;

    out.definition=definition;
    auto s=capture_definition_snapshot(definition);
    if(!s.readable) return;

    std::memcpy(out.definition_id,s.id,sizeof(out.definition_id));
    std::memcpy(out.definition_name,s.name,sizeof(out.definition_name));
    std::memcpy(out.definition_class,s.cls,sizeof(out.definition_class));
}

static void enqueue_player_state(const AnymakerLocalPlayerStateV2& state,uint64_t revision) {
    AcquireSRWLockExclusive(&g_player_event_lock);
    if(!g_p29_player_queue.push({state.actor,revision,state.client_scene},state)) {
        ReleaseSRWLockExclusive(&g_player_event_lock);
        InterlockedIncrement64(&g_player_dropped);
        return;
    }

    ReleaseSRWLockExclusive(&g_player_event_lock);

    AcquireSRWLockExclusive(&g_latest_player_lock);
    if(revision==g_p29_world_revision.load()) {
        g_latest_player_state=state;
        g_have_latest_player_state=true;
    }
    ReleaseSRWLockExclusive(&g_latest_player_lock);

    InterlockedIncrement64(&g_player_captured);
}

static bool pop_player_state(AnymakerLocalPlayerStateV2& out) {
    AcquireSRWLockExclusive(&g_player_event_lock);
    uint64_t before=g_p29_player_queue.stale;
    bool ok=g_p29_player_queue.pop(out,[](P29Ticket ticket) {
        return ticket.generation==g_p29_world_revision.load();
    });
    g_p29_stale_player_events.fetch_add(g_p29_player_queue.stale-before);
    ReleaseSRWLockExclusive(&g_player_event_lock);
    return ok;
}

static void dispatch_player_state(const AnymakerLocalPlayerStateV2& state) {
    PlayerStateRegistration local[MAX_PLAYER_STATE_CALLBACKS]{};
    LONG count=0;

    AcquireSRWLockShared(&g_player_callback_lock);
    count=g_player_callback_count;
    for(LONG i=0;i<count;++i) local[i]=g_player_callbacks[i];
    ReleaseSRWLockShared(&g_player_callback_lock);

    for(LONG i=0;i<count;++i) {
        __try {
            local[i].callback(&state,local[i].user_data);
        } __except(EXCEPTION_EXECUTE_HANDLER) {
            log_line(
                ANY_LOG_ERROR,
                local[i].mod_id?local[i].mod_id:"unknown_mod",
                "Exception escaped OnLocalPlayerState callback; snapshot skipped."
            );
        }
    }

    InterlockedIncrement64(&g_player_dispatched);
}

static bool finite3(const double p[3]) {
    return std::isfinite(p[0]) && std::isfinite(p[1]) && std::isfinite(p[2]);
}

static bool close3(const double a[3],const double b[3]) {
    constexpr double EPS=0.00001;
    return std::fabs(a[0]-b[0])<EPS &&
           std::fabs(a[1]-b[1])<EPS &&
           std::fabs(a[2]-b[2])<EPS;
}

static bool call_player_f64(
    GetF64Fn fn,
    void* actor,
    double& out,
    volatile LONG64* ok_counter
) {
    if(!fn) return false;
    bool ok=false;
    __try {
        fn(&out,actor);
        ok=std::isfinite(out);
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        ok=false;
    }
    if(ok && ok_counter) InterlockedIncrement64(ok_counter);
    return ok;
}

static bool call_player_vec2(
    GetVec2Fn fn,
    void* actor,
    double out[2],
    volatile LONG64* ok_counter
) {
    if(!fn) return false;
    bool ok=false;
    __try {
        fn(out,actor);
        ok=std::isfinite(out[0]) && std::isfinite(out[1]);
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        ok=false;
    }
    if(ok && ok_counter) InterlockedIncrement64(ok_counter);
    return ok;
}

static bool call_player_vec3(
    GetVec3Fn fn,
    void* actor,
    double out[3],
    volatile LONG64* ok_counter
) {
    if(!fn) return false;
    bool ok=false;
    __try {
        fn(out,actor);
        ok=finite3(out);
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        ok=false;
    }
    if(ok && ok_counter) InterlockedIncrement64(ok_counter);
    return ok;
}

static void capture_player_state(void* actor,void* client_scene) {
    if(!actor) return;
    const uint64_t revision=g_p29_world_revision.load();

    AnymakerLocalPlayerStateV2 state{};
    state.struct_size=sizeof(state);
    state.state_version=ANYMAKER_LOCAL_PLAYER_STATE_VERSION;
    state.sequence=(uint64_t)InterlockedIncrement64(&g_next_sequence);
    state.actor=(uintptr_t)actor;
    state.client_scene=(uintptr_t)client_scene;

    bool position_ok=safe_read_memory(
        (uintptr_t)actor+0x1D0,state.position,sizeof(state.position)
    ) && finite3(state.position);
    if(position_ok) {
        state.valid_fields|=ANY_PLAYER_VALID_POSITION;
        InterlockedIncrement64(&g_player_position_read_ok);
    }

    bool orientation_ok=safe_read_memory(
        (uintptr_t)actor+0x8E8,state.orientation,sizeof(state.orientation)
    ) && std::isfinite(state.orientation[0]) && std::isfinite(state.orientation[1]);
    if(orientation_ok) {
        state.valid_fields|=ANY_PLAYER_VALID_ORIENTATION;
        InterlockedIncrement64(&g_player_orientation_read_ok);
    }

    if(g_get_transform) {
        alignas(16) unsigned char mat34[96]{};
        bool call_ok=false;
        __try {
            g_get_transform(mat34,actor);
            call_ok=true;
        } __except(EXCEPTION_EXECUTE_HANDLER) {
            call_ok=false;
        }

        if(call_ok) {
            InterlockedIncrement64(&g_player_transform_getter_ok);
            double transform_position[3]{};
            std::memcpy(transform_position,mat34+0x48,sizeof(transform_position));
            if(position_ok && finite3(transform_position) &&
               close3(state.position,transform_position)) {
                InterlockedIncrement64(&g_player_transform_matches);
            }
        }
    }

    double orientation_getter[2]{};
    if(call_player_vec2(
        g_get_orientation,actor,orientation_getter,&g_player_orientation_getter_ok
    )) {
        if(orientation_ok &&
           std::fabs(state.orientation[0]-orientation_getter[0])<0.00001 &&
           std::fabs(state.orientation[1]-orientation_getter[1])<0.00001) {
            InterlockedIncrement64(&g_player_orientation_matches);
        }
    }

    if(call_player_vec3(
        g_get_linear_velocity,actor,state.linear_velocity,&g_player_velocity_getter_ok
    )) {
        state.valid_fields|=ANY_PLAYER_VALID_LINEAR_VELOCITY;
    }

    if(g_get_incapacitated) {
        uint8_t v=0;
        bool call_ok=false;
        __try {
            g_get_incapacitated(&v,actor);
            call_ok=true;
        } __except(EXCEPTION_EXECUTE_HANDLER) {
            call_ok=false;
        }
        if(call_ok) {
            state.incapacitated=v?1u:0u;
            state.valid_fields|=ANY_PLAYER_VALID_INCAPACITATED;
            InterlockedIncrement64(&g_player_incapacitated_getter_ok);
        }
    }

    if(g_get_swimming) {
        uint8_t v=0;
        bool call_ok=false;
        __try {
            g_get_swimming(&v,actor);
            call_ok=true;
        } __except(EXCEPTION_EXECUTE_HANDLER) {
            call_ok=false;
        }
        if(call_ok) {
            state.swimming=v?1u:0u;
            state.valid_fields|=ANY_PLAYER_VALID_SWIMMING;
            InterlockedIncrement64(&g_player_swimming_getter_ok);
        }
    }

    if(call_player_f64(g_get_stamina,actor,state.stamina,&g_player_stamina_getter_ok))
        state.valid_fields|=ANY_PLAYER_VALID_STAMINA;
    if(call_player_f64(g_get_hunger,actor,state.hunger,&g_player_hunger_getter_ok))
        state.valid_fields|=ANY_PLAYER_VALID_HUNGER;
    if(call_player_f64(g_get_thirst,actor,state.thirst,&g_player_thirst_getter_ok))
        state.valid_fields|=ANY_PLAYER_VALID_THIRST;
    if(call_player_f64(g_get_oxygen,actor,state.oxygen,&g_player_oxygen_getter_ok))
        state.valid_fields|=ANY_PLAYER_VALID_OXYGEN;
    if(call_player_f64(g_get_wetness,actor,state.wetness,&g_player_wetness_getter_ok))
        state.valid_fields|=ANY_PLAYER_VALID_WETNESS;
    if(call_player_f64(g_get_infection,actor,state.infection,&g_player_infection_getter_ok))
        state.valid_fields|=ANY_PLAYER_VALID_INFECTION;
    if(call_player_f64(g_get_temp_cold,actor,state.temperature_cold,&g_player_temp_cold_getter_ok))
        state.valid_fields|=ANY_PLAYER_VALID_TEMPERATURE_COLD;
    if(call_player_f64(g_get_temp_hot,actor,state.temperature_hot,&g_player_temp_hot_getter_ok))
        state.valid_fields|=ANY_PLAYER_VALID_TEMPERATURE_HOT;

    uintptr_t active=0;
    bool active_call_ok=false;
    if(g_get_item_active) {
        __try {
            g_get_item_active(&active,actor);
            active_call_ok=true;
        } __except(EXCEPTION_EXECUTE_HANDLER) {
            active=0;
            active_call_ok=false;
        }
    }
    if(active_call_ok) state.valid_fields|=ANY_PLAYER_VALID_ACTIVE_ITEM;
    fill_player_item_snapshot(active,state.active_item);
    if(active) {
        InterlockedIncrement64(&g_player_active_nonzero);
        if(state.active_item.definition)
            InterlockedIncrement64(&g_player_active_definition_ok);
    }

    uintptr_t handheld=0;
    bool handheld_call_ok=false;
    if(g_get_item_handheld) {
        __try {
            g_get_item_handheld(&handheld,actor);
            handheld_call_ok=true;
        } __except(EXCEPTION_EXECUTE_HANDLER) {
            handheld=0;
            handheld_call_ok=false;
        }
    }
    if(handheld_call_ok) state.valid_fields|=ANY_PLAYER_VALID_HANDHELD_ITEM;
    fill_player_item_snapshot(handheld,state.handheld_item);
    if(handheld) {
        InterlockedIncrement64(&g_player_handheld_nonzero);
        if(state.handheld_item.definition)
            InterlockedIncrement64(&g_player_handheld_definition_ok);
    }

    if(revision!=g_p29_world_revision.load()) return;
    p29_update_actor_from_player(state);
    enqueue_player_state(state,revision);
}

static void player_tick_hook(
    void* actor,
    void* client,
    void* client_scene,
    const double* arg4,
    const double* arg5,
    const void* arg6
) {
    InterlockedIncrement64(&g_p27_hook_calls[11]); p29_note_hook(11);
    g_player_tick_original(actor,client,client_scene,arg4,arg5,arg6);

    // Digital input gives us a high-confidence local actor identity.
    uintptr_t local_actor=(uintptr_t)InterlockedCompareExchange64(
        &g_local_actor_hint,0,0
    );
    if(!local_actor || (uintptr_t)actor!=local_actor)
        return;

    if(client_scene) {
        InterlockedExchange64(&g_local_scene_hint,(LONG64)(uintptr_t)client_scene);
    }

    // Capture at <=10 Hz even though actor_character.tick is much hotter.
    LONG64 now=(LONG64)GetTickCount64();
    LONG64 last=InterlockedCompareExchange64(&g_last_player_capture_ms,0,0);
    if(now-last<100)
        return;
    if(InterlockedCompareExchange64(&g_last_player_capture_ms,now,last)!=last)
        return;

    p27_validate_actor_virtuals(actor);
    capture_player_state(actor,client_scene);
}



// -----------------------------------------------------------------------------
// Runtime API v16 generic client actor registry/state.
//
// Static RE facts used here:
//   - client_scene.actor.container.create_object index 4729, ordinary args
//     RCX=container, RDX=replicator_context, R8=client_scene.actor*.
//   - destroy_object index 4733 uses the same argument shape.
//   - get_actor_by_id compares [actor + 0x08] to the requested s32 id.
//   - client_scene.actor.get_transform (already resolved/proven by the local
//     player API) works on the base actor and supplies world translation.
// -----------------------------------------------------------------------------

struct ClientActorRegistryEntry {
    uintptr_t container{};
    uintptr_t actor{};
    int32_t actor_id{};
    bool live{};
    bool have_snapshot{};
    uint16_t reserved0{};
    AnymakerClientActorStateV1 last_state{};
    uint64_t last_seen_ms{};
    P29Identity identity{};
    uint64_t p33_epoch{};
};

static constexpr LONG CLIENT_ACTOR_REGISTRY_CAPACITY=4096;
static ClientActorRegistryEntry g_client_actor_registry[CLIENT_ACTOR_REGISTRY_CAPACITY]{};
static LONG g_client_actor_registry_highwater=0;
static SRWLOCK g_client_actor_registry_lock=SRWLOCK_INIT;

struct ClientActorLifecycleRegistration {
    const char* mod_id;
    AnymakerClientActorLifecycleCallback callback;
    void* user_data;
};
static constexpr LONG MAX_CLIENT_ACTOR_CALLBACKS=32;
static ClientActorLifecycleRegistration g_client_actor_callbacks[MAX_CLIENT_ACTOR_CALLBACKS]{};
static LONG g_client_actor_callback_count=0;
static SRWLOCK g_client_actor_callback_lock=SRWLOCK_INIT;

static constexpr LONG CLIENT_ACTOR_EVENT_CAPACITY=2048;
static AnymakerClientActorLifecycleEventV1 g_client_actor_events[CLIENT_ACTOR_EVENT_CAPACITY]{};
static LONG g_client_actor_event_head=0;
static LONG g_client_actor_event_tail=0;
static LONG g_client_actor_event_count=0;
static SRWLOCK g_client_actor_event_lock=SRWLOCK_INIT;

static volatile LONG64 g_client_actor_created_captured=0;
static volatile LONG64 g_client_actor_destroyed_captured=0;
static volatile LONG64 g_client_actor_events_dispatched=0;
static volatile LONG64 g_client_actor_events_dropped=0;
static volatile LONG64 g_client_actor_state_queries=0;
static volatile LONG64 g_client_actor_state_position_ok=0;
static volatile LONG64 g_client_actor_create_arg_valid=0;
static volatile LONG64 g_client_actor_destroy_arg_valid=0;
static volatile LONG64 g_client_actor_stale_prune_checks=0;
static volatile LONG64 g_client_actor_stale_pruned=0;
static volatile LONG64 g_client_actor_synthetic_destroyed=0;

typedef void (*ClientActorContainerObjectFn)(void* container,void* context,void* actor);
static ClientActorContainerObjectFn g_client_actor_create_original=nullptr;
static ClientActorContainerObjectFn g_client_actor_destroy_original=nullptr;
static void enqueue_client_actor_event(const AnymakerClientActorLifecycleEventV1& ev);

static bool client_actor_registry_is_live(uintptr_t actor) {
    if(!actor) return false;
    bool live=false;
    AcquireSRWLockShared(&g_client_actor_registry_lock);
    for(LONG i=0;i<g_client_actor_registry_highwater;++i) {
        const auto& e=g_client_actor_registry[i];
        if(e.actor==actor) { live=e.live; break; }
    }
    ReleaseSRWLockShared(&g_client_actor_registry_lock);
    return live;
}

static void client_actor_registry_mark_live(uintptr_t actor,int32_t actor_id,uintptr_t container=0) {
    if(!actor) return;
    AcquireSRWLockExclusive(&g_client_actor_registry_lock);
    LONG free_slot=-1;
    for(LONG i=0;i<g_client_actor_registry_highwater;++i) {
        auto& e=g_client_actor_registry[i];
        if(e.actor==actor) {
            const bool new_lifetime=!e.live || e.p33_epoch!=g_p29_world_revision.load() || e.actor_id!=actor_id || (container && e.container!=container);
            if(new_lifetime) {
                e.have_snapshot=false;
                e.last_state={};
                g_p29_reused_addresses.fetch_add(1);
            }
            if(new_lifetime) {
                if(e.identity.live)p33_invalidate(p33_token(e.p33_epoch,e.identity.ticket.generation,size_t(i),ANY_OBJECT_ACTOR));
                e.identity.begin(actor,container,p29_next_generation());
                e.p33_epoch=g_p29_world_revision.load();
            }
            e.actor_id=actor_id;
            if(container) e.container=container;
            e.live=true;
            e.last_seen_ms=(uint64_t)GetTickCount64();
            ReleaseSRWLockExclusive(&g_client_actor_registry_lock);
            return;
        }
        if(free_slot<0 && !e.live) free_slot=i;
    }
    if(free_slot<0 && g_client_actor_registry_highwater<CLIENT_ACTOR_REGISTRY_CAPACITY)
        free_slot=g_client_actor_registry_highwater++;
    if(free_slot>=0) {
        auto& e=g_client_actor_registry[free_slot];
        e={};
        e.actor=actor;
        e.container=container;
        e.actor_id=actor_id;
        e.live=true;
        e.identity.begin(actor,container,p29_next_generation());
        e.p33_epoch=g_p29_world_revision.load();
        e.last_seen_ms=(uint64_t)GetTickCount64();
    } else {
        log_line(ANY_LOG_ERROR,"framework","PHASE27_ACTOR_REGISTRY capacity exhausted.");
    }
    ReleaseSRWLockExclusive(&g_client_actor_registry_lock);
}

static void client_actor_registry_update_snapshot(
    uintptr_t actor,const AnymakerClientActorStateV1& st
) {
    if(!actor) return;
    AcquireSRWLockExclusive(&g_client_actor_registry_lock);
    for(LONG i=0;i<g_client_actor_registry_highwater;++i) {
        auto& e=g_client_actor_registry[i];
        if(e.actor==actor && e.live) {
            e.actor_id=st.actor_id;
            e.last_state=st;
            e.have_snapshot=true;
            e.last_seen_ms=(uint64_t)GetTickCount64();
            break;
        }
    }
    ReleaseSRWLockExclusive(&g_client_actor_registry_lock);
}

static bool client_actor_registry_mark_dead(
    uintptr_t actor,AnymakerClientActorStateV1* out_last=nullptr
) {
    if(out_last) *out_last={};
    if(!actor) return false;
    bool was_live=false;
    AcquireSRWLockExclusive(&g_client_actor_registry_lock);
    for(LONG i=0;i<g_client_actor_registry_highwater;++i) {
        auto& e=g_client_actor_registry[i];
        if(e.actor==actor) {
            was_live=e.live;
            if(e.identity.live)p33_invalidate(p33_token(e.p33_epoch,e.identity.ticket.generation,size_t(i),ANY_OBJECT_ACTOR));
            e.live=false;
            e.identity.retire();
            if(out_last && e.have_snapshot) *out_last=e.last_state;
            break;
        }
    }
    ReleaseSRWLockExclusive(&g_client_actor_registry_lock);
    return was_live;
}

static void p29_update_actor_from_player(const AnymakerLocalPlayerStateV2& player) {
    AnymakerClientActorStateV1 actor{};
    AcquireSRWLockExclusive(&g_client_actor_registry_lock);
    for(LONG i=0;i<g_client_actor_registry_highwater;++i) {
        auto& entry=g_client_actor_registry[i];
        if(entry.actor!=player.actor || !entry.live || !entry.identity.live) continue;
        actor=entry.last_state;
        actor.struct_size=sizeof(actor);actor.state_version=ANYMAKER_CLIENT_ACTOR_STATE_VERSION;
        actor.actor=player.actor;actor.actor_id=entry.actor_id;actor.sequence=player.sequence;
        actor.flags|=ANY_CLIENT_ACTOR_FLAG_LOCAL_PLAYER;
        if(player.valid_fields&ANY_PLAYER_VALID_POSITION) {
            std::memcpy(actor.position,player.position,sizeof(actor.position));actor.valid_fields|=ANY_CLIENT_ACTOR_VALID_POSITION;
        }
        if(player.valid_fields&ANY_PLAYER_VALID_ORIENTATION) {
            std::memcpy(actor.orientation,player.orientation,sizeof(actor.orientation));actor.valid_fields|=ANY_CLIENT_ACTOR_VALID_ORIENTATION;
        }
        if(player.valid_fields&ANY_PLAYER_VALID_LINEAR_VELOCITY) {
            std::memcpy(actor.linear_velocity,player.linear_velocity,sizeof(actor.linear_velocity));actor.valid_fields|=ANY_CLIENT_ACTOR_VALID_LINEAR_VELOCITY;
        }
        entry.last_state=actor;entry.have_snapshot=true;break;
    }
    ReleaseSRWLockExclusive(&g_client_actor_registry_lock);
    if(player.valid_fields&ANY_PLAYER_VALID_POSITION) {
        g_p29_transform_samples.fetch_add(1);
        if(!finite3(player.position)) g_p29_transform_invalid.fetch_add(1);
    }
}

static void enqueue_synthetic_client_actor_destroy(
    uintptr_t actor,const AnymakerClientActorStateV1* best_snapshot
) {
    AnymakerClientActorStateV1 st{};
    if(best_snapshot && best_snapshot->struct_size) st=*best_snapshot;
    else {
        st.struct_size=sizeof(st);
        st.state_version=ANYMAKER_CLIENT_ACTOR_STATE_VERSION;
        st.sequence=(uint64_t)InterlockedIncrement64(&g_next_sequence);
        st.actor=(AnymakerActorHandle)actor;
    }
    st.actor=(AnymakerActorHandle)actor;

    AnymakerClientActorLifecycleEventV1 ev{};
    ev.struct_size=sizeof(ev);
    ev.event_version=ANYMAKER_CLIENT_ACTOR_EVENT_VERSION;
    ev.sequence=(uint64_t)InterlockedIncrement64(&g_next_sequence);
    ev.kind=ANY_CLIENT_ACTOR_DESTROYED;
    ev.state=st;
    enqueue_client_actor_event(ev);
    InterlockedIncrement64(&g_client_actor_destroyed_captured);
    InterlockedIncrement64(&g_client_actor_synthetic_destroyed);
}

static bool capture_client_actor_state_raw(
    uintptr_t actor,AnymakerClientActorStateV1& out,bool require_live
) {
    out={};
    out.struct_size=sizeof(out);
    out.state_version=ANYMAKER_CLIENT_ACTOR_STATE_VERSION;
    out.sequence=(uint64_t)InterlockedIncrement64(&g_next_sequence);
    out.actor=actor;
    if(!actor || (require_live && !client_actor_registry_is_live(actor))) return false;

    int32_t actor_id=0;
    if(safe_read_memory(actor+0x08,&actor_id,sizeof(actor_id))) {
        out.actor_id=actor_id;
        out.valid_fields|=ANY_CLIENT_ACTOR_VALID_ID;
    }

    // Pinned GCL 4901 copies the mat34 at +0x188. Read the POD matrix
    // on the existing creation/destruction hook rather than calling a getter
    // with an unestablished caller-thread contract.
    double mat34[12]{};
    bool pos_ok=safe_read_memory(actor+0x188,mat34,sizeof(mat34)) && p29_translation(mat34,out.position);
    if(pos_ok) {
        out.valid_fields|=ANY_CLIENT_ACTOR_VALID_POSITION;
        InterlockedIncrement64(&g_client_actor_state_position_ok);
    }

    uintptr_t local=(uintptr_t)InterlockedCompareExchange64(&g_local_actor_hint,0,0);
    if(actor==local && local!=0) {
        out.flags|=ANY_CLIENT_ACTOR_FLAG_LOCAL_PLAYER;
        AnymakerLocalPlayerStateV2 st{};
        AcquireSRWLockShared(&g_latest_player_lock);
        bool have=g_have_latest_player_state && g_latest_player_state.actor==actor;
        if(have) st=g_latest_player_state;
        ReleaseSRWLockShared(&g_latest_player_lock);
        if(have) {
            if(st.valid_fields&ANY_PLAYER_VALID_ORIENTATION) {
                std::memcpy(out.orientation,st.orientation,sizeof(out.orientation));
                out.valid_fields|=ANY_CLIENT_ACTOR_VALID_ORIENTATION;
            }
            if(st.valid_fields&ANY_PLAYER_VALID_LINEAR_VELOCITY) {
                std::memcpy(out.linear_velocity,st.linear_velocity,sizeof(out.linear_velocity));
                out.valid_fields|=ANY_CLIENT_ACTOR_VALID_LINEAR_VELOCITY;
            }
        }
    }
    return (out.valid_fields&ANY_CLIENT_ACTOR_VALID_ID)!=0 || pos_ok;
}



static bool is_client_actor_live(AnymakerActorHandle actor) {
    return client_actor_registry_is_live((uintptr_t)actor);
}

// Keep SEH in a POD-only leaf function. MSVC rejects __try inside functions
// that may require C++ object unwinding (C2712); enumerate_client_actors()
// performs std::string-based diagnostic formatting later in the function.
static bool seh_client_actor_enumerate_callback(
    AnymakerClientActorEnumerateCallback callback,
    const AnymakerClientActorStateV1* state,
    void* user_data
) {
    if(!callback || !state) return false;
    bool keep_going=false;
    __try { keep_going=callback(state,user_data); }
    __except(EXCEPTION_EXECUTE_HANDLER) { keep_going=false; }
    return keep_going;
}



#include "legacy_actor_queries.inc"

static bool register_client_actor_lifecycle(
    const char* mod_id,AnymakerClientActorLifecycleCallback callback,void* user_data
) {
    if(!callback) return false;
    AcquireSRWLockExclusive(&g_client_actor_callback_lock);
    if(g_client_actor_callback_count>=MAX_CLIENT_ACTOR_CALLBACKS) {
        ReleaseSRWLockExclusive(&g_client_actor_callback_lock);
        return false;
    }
    LONG slot=g_client_actor_callback_count++;
    g_client_actor_callbacks[slot]={mod_id,callback,user_data};
    ReleaseSRWLockExclusive(&g_client_actor_callback_lock);
    std::string m="Registered OnClientActorLifecycle callback";
    if(mod_id && *mod_id) m += std::string(" for ")+mod_id;
    log_line(ANY_LOG_INFO,"framework",m.c_str());
    return true;
}

static void enqueue_client_actor_event(const AnymakerClientActorLifecycleEventV1& ev) {
    AcquireSRWLockExclusive(&g_client_actor_event_lock);
    if(g_client_actor_event_count>=CLIENT_ACTOR_EVENT_CAPACITY) {
        ReleaseSRWLockExclusive(&g_client_actor_event_lock);
        InterlockedIncrement64(&g_client_actor_events_dropped);
        return;
    }
    g_client_actor_events[g_client_actor_event_tail]=ev;
    g_client_actor_event_tail=(g_client_actor_event_tail+1)%CLIENT_ACTOR_EVENT_CAPACITY;
    ++g_client_actor_event_count;
    ReleaseSRWLockExclusive(&g_client_actor_event_lock);
}

static bool pop_client_actor_event(AnymakerClientActorLifecycleEventV1& ev) {
    AcquireSRWLockExclusive(&g_client_actor_event_lock);
    if(g_client_actor_event_count<=0) {
        ReleaseSRWLockExclusive(&g_client_actor_event_lock);
        return false;
    }
    ev=g_client_actor_events[g_client_actor_event_head];
    g_client_actor_event_head=(g_client_actor_event_head+1)%CLIENT_ACTOR_EVENT_CAPACITY;
    --g_client_actor_event_count;
    ReleaseSRWLockExclusive(&g_client_actor_event_lock);
    return true;
}

static void dispatch_client_actor_event(const AnymakerClientActorLifecycleEventV1& ev) {
    ClientActorLifecycleRegistration local[MAX_CLIENT_ACTOR_CALLBACKS]{};
    LONG count=0;
    AcquireSRWLockShared(&g_client_actor_callback_lock);
    count=g_client_actor_callback_count;
    for(LONG i=0;i<count;++i) local[i]=g_client_actor_callbacks[i];
    ReleaseSRWLockShared(&g_client_actor_callback_lock);
    for(LONG i=0;i<count;++i) {
        __try { local[i].callback(&ev,local[i].user_data); }
        __except(EXCEPTION_EXECUTE_HANDLER) {
            log_line(ANY_LOG_ERROR,local[i].mod_id?local[i].mod_id:"unknown_mod",
                     "Exception escaped OnClientActorLifecycle callback.");
        }
    }
    InterlockedIncrement64(&g_client_actor_events_dispatched);
}

static bool get_client_actor_api_info(AnymakerClientActorApiInfoV1* out) {
    if(!out) return false;
    out->struct_size=sizeof(*out);
    out->info_version=ANYMAKER_CLIENT_ACTOR_API_INFO_VERSION;
    uint32_t live=0;
    AcquireSRWLockShared(&g_client_actor_registry_lock);
    for(LONG i=0;i<g_client_actor_registry_highwater;++i)
        if(g_client_actor_registry[i].live) ++live;
    ReleaseSRWLockShared(&g_client_actor_registry_lock);
    out->registry_live=live;
    AcquireSRWLockShared(&g_client_actor_callback_lock);
    out->registered_callbacks=(uint32_t)g_client_actor_callback_count;
    ReleaseSRWLockShared(&g_client_actor_callback_lock);
    out->created_captured=(uint64_t)InterlockedCompareExchange64(&g_client_actor_created_captured,0,0);
    out->destroyed_captured=(uint64_t)InterlockedCompareExchange64(&g_client_actor_destroyed_captured,0,0);
    out->events_dispatched=(uint64_t)InterlockedCompareExchange64(&g_client_actor_events_dispatched,0,0);
    out->events_dropped=(uint64_t)InterlockedCompareExchange64(&g_client_actor_events_dropped,0,0);
    out->state_queries=(uint64_t)InterlockedCompareExchange64(&g_client_actor_state_queries,0,0);
    out->state_position_ok=(uint64_t)InterlockedCompareExchange64(&g_client_actor_state_position_ok,0,0);
    out->stale_prune_checks=(uint64_t)InterlockedCompareExchange64(&g_client_actor_stale_prune_checks,0,0);
    out->stale_pruned=(uint64_t)InterlockedCompareExchange64(&g_client_actor_stale_pruned,0,0);
    out->synthetic_destroyed=(uint64_t)InterlockedCompareExchange64(&g_client_actor_synthetic_destroyed,0,0);
    return true;
}

static void client_actor_create_hook(void* container,void* context,void* actor) {
    InterlockedIncrement64(&g_p27_hook_calls[18]); p29_note_hook(18);
    g_client_actor_create_original(container,context,actor);
    p27_capture_actor_container(container);
    uintptr_t a=(uintptr_t)actor;
    int32_t id=0;
    bool id_ok=a && safe_read_memory(a+0x08,&id,sizeof(id));
    AnymakerClientActorStateV1 st{};
    bool state_ok=capture_client_actor_state_raw(a,st,false);
    if(id_ok || state_ok) {
        InterlockedIncrement64(&g_client_actor_create_arg_valid);
        client_actor_registry_mark_live(a,id_ok?id:st.actor_id,(uintptr_t)container);
        // recapture after registry insertion so normal live semantics apply.
        capture_client_actor_state_raw(a,st,true);
        client_actor_registry_update_snapshot(a,st);
        AnymakerClientActorLifecycleEventV1 ev{};
        ev.struct_size=sizeof(ev);
        ev.event_version=ANYMAKER_CLIENT_ACTOR_EVENT_VERSION;
        ev.sequence=(uint64_t)InterlockedIncrement64(&g_next_sequence);
        ev.kind=ANY_CLIENT_ACTOR_CREATED;
        ev.state=st;
        enqueue_client_actor_event(ev);
        InterlockedIncrement64(&g_client_actor_created_captured);
    }
}

static void client_actor_destroy_hook(void* container,void* context,void* actor) {
    InterlockedIncrement64(&g_p27_hook_calls[19]); p29_note_hook(19);
    uintptr_t a=(uintptr_t)actor;
    if(a && InterlockedCompareExchange64(&g_local_actor_hint,0,(LONG64)a)==(LONG64)a) {
        p33_retire_world();
        InterlockedExchange64(&g_local_scene_hint,0);
        AcquireSRWLockExclusive(&g_latest_player_lock);g_have_latest_player_state=false;g_latest_player_state={};ReleaseSRWLockExclusive(&g_latest_player_lock);
    }
    AnymakerClientActorStateV1 st{};
    bool state_ok=capture_client_actor_state_raw(a,st,false);
    int32_t id=0;
    bool id_ok=a && safe_read_memory(a+0x08,&id,sizeof(id));
    if(id_ok || state_ok) InterlockedIncrement64(&g_client_actor_destroy_arg_valid);

    AnymakerClientActorStateV1 last{};
    bool was_live=client_actor_registry_mark_dead(a,&last);
    // Invalidate registry access before the native destructor frees the object.
    g_client_actor_destroy_original(container,context,actor);
    if(!was_live) return; // stale-prune may already have synthesized teardown.

    if(!(id_ok || state_ok) && last.struct_size) st=last;
    if(id_ok) { st.actor_id=id; st.valid_fields|=ANY_CLIENT_ACTOR_VALID_ID; }
    st.actor=(AnymakerActorHandle)a;
    if(!st.struct_size) {
        st.struct_size=sizeof(st);
        st.state_version=ANYMAKER_CLIENT_ACTOR_STATE_VERSION;
        st.sequence=(uint64_t)InterlockedIncrement64(&g_next_sequence);
    }

    AnymakerClientActorLifecycleEventV1 ev{};
    ev.struct_size=sizeof(ev);
    ev.event_version=ANYMAKER_CLIENT_ACTOR_EVENT_VERSION;
    ev.sequence=(uint64_t)InterlockedIncrement64(&g_next_sequence);
    ev.kind=ANY_CLIENT_ACTOR_DESTROYED;
    ev.state=st;
    enqueue_client_actor_event(ev);
    InterlockedIncrement64(&g_client_actor_destroyed_captured);
}


































// -----------------------------------------------------------------------------
// Vehicle-component lifecycle API + registry.
// -----------------------------------------------------------------------------

struct VehicleComponentRegistration {
    const char* mod_id;
    AnymakerVehicleComponentCallback callback;
    void* user_data;
};

static constexpr LONG MAX_VEHICLE_COMPONENT_CALLBACKS=32;
static VehicleComponentRegistration g_vehicle_component_callbacks[MAX_VEHICLE_COMPONENT_CALLBACKS]{};
static LONG g_vehicle_component_callback_count=0;
static SRWLOCK g_vehicle_component_callback_lock=SRWLOCK_INIT;

static bool register_vehicle_component(
    const char* mod_id,
    AnymakerVehicleComponentCallback callback,
    void* user_data
) {
    if(!callback) return false;

    AcquireSRWLockExclusive(&g_vehicle_component_callback_lock);
    if(g_vehicle_component_callback_count>=MAX_VEHICLE_COMPONENT_CALLBACKS) {
        ReleaseSRWLockExclusive(&g_vehicle_component_callback_lock);
        log_line(ANY_LOG_ERROR,"framework","Too many vehicle-component callbacks registered.");
        return false;
    }

    LONG slot=g_vehicle_component_callback_count++;
    g_vehicle_component_callbacks[slot]={mod_id,callback,user_data};
    ReleaseSRWLockExclusive(&g_vehicle_component_callback_lock);

    std::string m="Registered OnVehicleComponent callback";
    if(mod_id && *mod_id) m += std::string(" for ")+mod_id;
    log_line(ANY_LOG_INFO,"framework",m.c_str());
    return true;
}

struct VehicleComponentRegistryEntry {
    uintptr_t metadata_definition{};
    uint32_t class_source{};
    uintptr_t component{};
    uintptr_t vehicle{};
    AnymakerVehicleComponentSide side{};
    uint32_t live{};
    char class_name[ANYMAKER_COMPONENT_CLASS_SNAPSHOT_MAX]{};
    P29Identity identity{};
    uint64_t p33_epoch{};
};

static constexpr LONG COMPONENT_REGISTRY_CAPACITY=262144;
static VehicleComponentRegistryEntry g_component_registry[COMPONENT_REGISTRY_CAPACITY]{};
static LONG g_component_registry_count=0;
static SRWLOCK g_component_registry_lock=SRWLOCK_INIT;

static P27PointerPool<COMPONENT_REGISTRY_CAPACITY,524288> g_component_index;
static volatile LONG64 g_p27_component_registry_overflow=0;
static LONG find_component_registry_unlocked(uintptr_t component) { return g_component_index.find(component); }
static LONG ensure_component_registry_unlocked(uintptr_t component) {
    bool replaced=false;LONG slot=g_component_index.ensure(component,replaced);
    if(slot<0) {InterlockedIncrement64(&g_p27_component_registry_overflow);return -1;}
    if(replaced) {g_component_registry[slot]={};g_component_registry[slot].component=component;}
    InterlockedExchange(&g_component_registry_count,g_component_index.highwater);return slot;
}

static void component_registry_class_created(
    uintptr_t component,
    AnymakerVehicleComponentSide side,
    const char* class_name
) {
    if(!component) return;

    AcquireSRWLockExclusive(&g_component_registry_lock);
    LONG slot=ensure_component_registry_unlocked(component);
    if(slot>=0) {
        auto& e=g_component_registry[slot];
        if(e.identity.ticket.generation) g_p29_reused_addresses.fetch_add(1);
        if(e.identity.live)p33_invalidate(p33_token(e.p33_epoch,e.identity.ticket.generation,size_t(slot),ANY_OBJECT_COMPONENT));
        e.identity.begin(component,0,p29_next_generation());
        e.p33_epoch=g_p29_world_revision.load();
        e.component=component;
        e.vehicle=0;
        e.metadata_definition=0;
        e.class_source=1;
        e.side=side;
        e.live=1;
        e.class_name[0]='\0';
        if(class_name) {
            std::strncpy(e.class_name,class_name,sizeof(e.class_name)-1);
            e.class_name[sizeof(e.class_name)-1]='\0';
        }
    }
    ReleaseSRWLockExclusive(&g_component_registry_lock);
}

#include "legacy_component_metadata.inc"

static bool component_registry_attach(
    uintptr_t component,
    uintptr_t vehicle,
    AnymakerVehicleComponentSide side,
    char out_class[ANYMAKER_COMPONENT_CLASS_SNAPSHOT_MAX]
) {
    if(out_class) out_class[0]='\0';
    if(!component) return false;

    bool had_class=false;
    char native_class[ANYMAKER_COMPONENT_CLASS_SNAPSHOT_MAX]{};
    uintptr_t native_definition=0;
    bool metadata_read=p273_read_component_class(component,side,native_class,&native_definition);

    AcquireSRWLockExclusive(&g_component_registry_lock);
    LONG slot=ensure_component_registry_unlocked(component);
    if(slot>=0) {
        auto& e=g_component_registry[slot];
        if(e.identity.live && e.p33_epoch!=g_p29_world_revision.load()){
            p33_invalidate(p33_token(e.p33_epoch,e.identity.ticket.generation,size_t(slot),ANY_OBJECT_COMPONENT));
            e.identity.retire();
        }
        if(!e.identity.live){
            if(e.class_name[0] || e.metadata_definition)g_p29_component_metadata_resets.fetch_add(1);
            p29_reset_retired_component(e,component,vehicle,p29_next_generation());
            e.p33_epoch=g_p29_world_revision.load();
        }
        if(metadata_read){
            bool previously_named=e.class_name[0]!=0;
            P28Context context{component,native_definition,e.metadata_definition,e.vehicle,e.class_source,(uint32_t)e.side,e.live};
            bool agrees=!previously_named || std::strcmp(e.class_name,native_class)==0;
            p273_merge_class(side,e.class_name,native_class,&context);
            if(agrees){e.metadata_definition=native_definition;if(!previously_named)e.class_source=native_class[0]?2:3;}
        }
        had_class=e.class_name[0]!='\0';
        e.component=component;
        e.vehicle=vehicle;
        e.identity.ticket.owner=vehicle;
        e.side=side;
        e.live=1;
        if(out_class) {
            std::memcpy(out_class,e.class_name,ANYMAKER_COMPONENT_CLASS_SNAPSHOT_MAX);
            out_class[ANYMAKER_COMPONENT_CLASS_SNAPSHOT_MAX-1]='\0';
        }
    }
    ReleaseSRWLockExclusive(&g_component_registry_lock);

    return had_class;
}

static bool component_registry_snapshot_and_destroy(
    uintptr_t component,
    uintptr_t fallback_vehicle,
    AnymakerVehicleComponentSide side,
    uintptr_t& out_vehicle,
    char out_class[ANYMAKER_COMPONENT_CLASS_SNAPSHOT_MAX]
) {
    out_vehicle=fallback_vehicle;
    if(out_class) out_class[0]='\0';
    if(!component) return false;

    bool found=false;

    AcquireSRWLockExclusive(&g_component_registry_lock);
    LONG slot=find_component_registry_unlocked(component);
    if(slot>=0) {
        auto& e=g_component_registry[slot];
        found=true;
        if(e.vehicle) out_vehicle=e.vehicle;
        if(out_class) {
            std::memcpy(out_class,e.class_name,ANYMAKER_COMPONENT_CLASS_SNAPSHOT_MAX);
            out_class[ANYMAKER_COMPONENT_CLASS_SNAPSHOT_MAX-1]='\0';
        }
        e.side=side;
        if(e.identity.live)p33_invalidate(p33_token(e.p33_epoch,e.identity.ticket.generation,size_t(slot),ANY_OBJECT_COMPONENT));
        e.live=0;
        e.identity.retire();
        g_component_index.retire(slot);
    }
    ReleaseSRWLockExclusive(&g_component_registry_lock);

    return found;
}

static bool is_vehicle_component_live(AnymakerVehicleComponentHandle component) {
    if(!component) return false;

    bool live=false;
    AcquireSRWLockShared(&g_component_registry_lock);
    LONG slot=find_component_registry_unlocked((uintptr_t)component);
    if(slot>=0) live=g_component_registry[slot].live!=0;
    ReleaseSRWLockShared(&g_component_registry_lock);
    return live;
}

static AnymakerVehicleHandle get_vehicle_component_vehicle(
    AnymakerVehicleComponentHandle component
) {
    if(!component) return 0;

    uintptr_t vehicle=0;
    AcquireSRWLockShared(&g_component_registry_lock);
    LONG slot=find_component_registry_unlocked((uintptr_t)component);
    if(slot>=0 && g_component_registry[slot].live)
        vehicle=g_component_registry[slot].vehicle;
    ReleaseSRWLockShared(&g_component_registry_lock);
    return (AnymakerVehicleHandle)vehicle;
}

static size_t copy_vehicle_component_class(
    AnymakerVehicleComponentHandle component,
    char* out,
    size_t out_size
) {
    if(out && out_size) out[0]='\0';
    if(!component) return ANYMAKER_API_INVALID_SIZE;

    char local[ANYMAKER_COMPONENT_CLASS_SNAPSHOT_MAX]{};
    bool live=false;

    AcquireSRWLockShared(&g_component_registry_lock);
    LONG slot=find_component_registry_unlocked((uintptr_t)component);
    if(slot>=0) {
        live=g_component_registry[slot].live!=0;
        std::memcpy(local,g_component_registry[slot].class_name,sizeof(local));
        local[sizeof(local)-1]='\0';
    }
    ReleaseSRWLockShared(&g_component_registry_lock);

    if(!live) return ANYMAKER_API_INVALID_SIZE;

    size_t len=std::strlen(local);
    if(out && out_size) {
        size_t n=std::min(len,out_size-1);
        if(n) std::memcpy(out,local,n);
        out[n]='\0';
    }
    return len;
}

enum VehicleComponentEventSlot : int {
    VC_CLIENT_CREATED=0,
    VC_CLIENT_DESTROYED=1,
    VC_SERVER_CREATED=2,
    VC_SERVER_DESTROYED=3,
    VC_SLOT_COUNT=4
};

struct VehicleComponentRecord {
    uint64_t sequence{};
    AnymakerVehicleComponentSide side{};
    AnymakerVehicleComponentLifecycleKind kind{};
    uintptr_t component{};
    uintptr_t vehicle{};
    VehicleComponentEventSlot slot{};
    char class_name[ANYMAKER_COMPONENT_CLASS_SNAPSHOT_MAX]{};
};

static constexpr LONG COMPONENT_EVENT_CAPACITY=16384;
static VehicleComponentRecord g_component_events[COMPONENT_EVENT_CAPACITY]{};
static LONG g_component_event_head=0;
static LONG g_component_event_tail=0;
static LONG g_component_event_count=0;
static SRWLOCK g_component_event_lock=SRWLOCK_INIT;

static volatile LONG64 g_component_captured[VC_SLOT_COUNT]{};
static volatile LONG64 g_component_dispatched[VC_SLOT_COUNT]{};
static volatile LONG64 g_component_dropped[VC_SLOT_COUNT]{};

static volatile LONG64 g_component_client_class_created=0;
static volatile LONG64 g_component_server_class_created=0;
static volatile LONG64 g_component_client_attach_class_match=0;
static volatile LONG64 g_component_server_attach_class_match=0;
static volatile LONG64 g_component_client_destroy_registry_match=0;
static volatile LONG64 g_component_server_destroy_registry_match=0;

static void enqueue_vehicle_component(
    VehicleComponentEventSlot slot,
    AnymakerVehicleComponentSide side,
    AnymakerVehicleComponentLifecycleKind kind,
    uintptr_t component,
    uintptr_t vehicle,
    const char* class_name
) {
    uint64_t seq=(uint64_t)InterlockedIncrement64(&g_next_sequence);

    AcquireSRWLockExclusive(&g_component_event_lock);
    if(g_component_event_count>=COMPONENT_EVENT_CAPACITY) {
        ReleaseSRWLockExclusive(&g_component_event_lock);
        InterlockedIncrement64(&g_component_dropped[slot]);
        return;
    }

    VehicleComponentRecord rec{};
    rec.sequence=seq;
    rec.side=side;
    rec.kind=kind;
    rec.component=component;
    rec.vehicle=vehicle;
    rec.slot=slot;
    if(class_name) {
        std::strncpy(rec.class_name,class_name,sizeof(rec.class_name)-1);
        rec.class_name[sizeof(rec.class_name)-1]='\0';
    }

    g_component_events[g_component_event_tail]=rec;
    g_component_event_tail=(g_component_event_tail+1)%COMPONENT_EVENT_CAPACITY;
    ++g_component_event_count;
    ReleaseSRWLockExclusive(&g_component_event_lock);

    InterlockedIncrement64(&g_component_captured[slot]);
}

static bool pop_vehicle_component(VehicleComponentRecord& out) {
    AcquireSRWLockExclusive(&g_component_event_lock);
    if(g_component_event_count<=0) {
        ReleaseSRWLockExclusive(&g_component_event_lock);
        return false;
    }

    out=g_component_events[g_component_event_head];
    g_component_event_head=(g_component_event_head+1)%COMPONENT_EVENT_CAPACITY;
    --g_component_event_count;
    ReleaseSRWLockExclusive(&g_component_event_lock);
    return true;
}

static void dispatch_vehicle_component(const VehicleComponentRecord& rec) {
    VehicleComponentRegistration local[MAX_VEHICLE_COMPONENT_CALLBACKS]{};
    LONG count=0;

    AcquireSRWLockShared(&g_vehicle_component_callback_lock);
    count=g_vehicle_component_callback_count;
    for(LONG i=0;i<count;++i) local[i]=g_vehicle_component_callbacks[i];
    ReleaseSRWLockShared(&g_vehicle_component_callback_lock);

    AnymakerVehicleComponentEventV1 ev{};
    ev.struct_size=sizeof(ev);
    ev.event_version=ANYMAKER_VEHICLE_COMPONENT_EVENT_VERSION;
    ev.sequence=rec.sequence;
    ev.side=rec.side;
    ev.kind=rec.kind;
    ev.component=rec.component;
    ev.vehicle=rec.vehicle;
    std::memcpy(ev.class_name,rec.class_name,sizeof(ev.class_name));

    for(LONG i=0;i<count;++i) {
        __try {
            local[i].callback(&ev,local[i].user_data);
        } __except(EXCEPTION_EXECUTE_HANDLER) {
            log_line(
                ANY_LOG_ERROR,
                local[i].mod_id?local[i].mod_id:"unknown_mod",
                "Exception escaped OnVehicleComponent callback; event skipped."
            );
        }
    }

    InterlockedIncrement64(&g_component_dispatched[rec.slot]);
}

static const char* vehicle_component_slot_name(VehicleComponentEventSlot slot) {
    switch(slot) {
        case VC_CLIENT_CREATED: return "client_created";
        case VC_CLIENT_DESTROYED: return "client_destroyed";
        case VC_SERVER_CREATED: return "server_created";
        case VC_SERVER_DESTROYED: return "server_destroyed";
        default: return "unknown";
    }
}

// GeoCode ABI for vehicle component construction/lifecycle.
using VehicleComponentClassCreateFn=void (*)(void* out_ref,const void* class_name);
using ClientVehicleComponentCreateFn=void (*)(void* self,const void* defs,void* vehicle,void* grid);
using ClientVehicleComponentDestroyFn=void (*)(void* self,void* vehicle,void* physics_scene);
using ServerVehicleComponentCreateFn=void (*)(void* self,void* vehicle);
using ServerVehicleComponentDestroyFn=void (*)(void* self,void* vehicle,void* server_scene);

static VehicleComponentClassCreateFn g_vc_client_class_original=nullptr;
static ClientVehicleComponentCreateFn g_vc_client_create_original=nullptr;
static ClientVehicleComponentDestroyFn g_vc_client_destroy_original=nullptr;
static VehicleComponentClassCreateFn g_vc_server_class_original=nullptr;
static ServerVehicleComponentCreateFn g_vc_server_create_original=nullptr;
static ServerVehicleComponentDestroyFn g_vc_server_destroy_original=nullptr;

static void vc_client_class_hook(void* out_ref,const void* class_name) {
    InterlockedIncrement64(&g_p27_hook_calls[12]); p29_note_hook(12);
    char cls[ANYMAKER_COMPONENT_CLASS_SNAPSHOT_MAX]{};
    copy_geostring_object((uintptr_t)class_name,cls,sizeof(cls));

    g_vc_client_class_original(out_ref,class_name);

    uintptr_t component=0;
    if(out_ref) std::memcpy(&component,(unsigned char*)out_ref+8,sizeof(component));
    if(component) {
        component_registry_class_created(
            component,ANY_COMPONENT_SIDE_CLIENT,cls
        );
        InterlockedIncrement64(&g_component_client_class_created);
    }
}

static void vc_server_class_hook(void* out_ref,const void* class_name) {
    InterlockedIncrement64(&g_p27_hook_calls[15]); p29_note_hook(15);
    char cls[ANYMAKER_COMPONENT_CLASS_SNAPSHOT_MAX]{};
    copy_geostring_object((uintptr_t)class_name,cls,sizeof(cls));

    g_vc_server_class_original(out_ref,class_name);

    uintptr_t component=0;
    if(out_ref) std::memcpy(&component,(unsigned char*)out_ref+8,sizeof(component));
    if(component) {
        component_registry_class_created(
            component,ANY_COMPONENT_SIDE_SERVER,cls
        );
        InterlockedIncrement64(&g_component_server_class_created);
    }
}

static void vc_client_create_hook(
    void* self,const void* defs,void* vehicle,void* grid
) {
    InterlockedIncrement64(&g_p27_hook_calls[13]); p29_note_hook(13);
    g_vc_client_create_original(self,defs,vehicle,grid);
    p29_capture_component_transform((uintptr_t)self,false);

    char cls[ANYMAKER_COMPONENT_CLASS_SNAPSHOT_MAX]{};
    bool class_match=component_registry_attach(
        (uintptr_t)self,(uintptr_t)vehicle,ANY_COMPONENT_SIDE_CLIENT,cls
    );
    if(class_match)
        InterlockedIncrement64(&g_component_client_attach_class_match);

    enqueue_vehicle_component(
        VC_CLIENT_CREATED,ANY_COMPONENT_SIDE_CLIENT,ANY_COMPONENT_CREATED,
        (uintptr_t)self,(uintptr_t)vehicle,cls
    );
}

static void vc_server_create_hook(void* self,void* vehicle) {
    InterlockedIncrement64(&g_p27_hook_calls[16]); p29_note_hook(16);
    g_vc_server_create_original(self,vehicle);
    p29_capture_component_transform((uintptr_t)self,true);

    char cls[ANYMAKER_COMPONENT_CLASS_SNAPSHOT_MAX]{};
    bool class_match=component_registry_attach(
        (uintptr_t)self,(uintptr_t)vehicle,ANY_COMPONENT_SIDE_SERVER,cls
    );
    if(class_match)
        InterlockedIncrement64(&g_component_server_attach_class_match);

    enqueue_vehicle_component(
        VC_SERVER_CREATED,ANY_COMPONENT_SIDE_SERVER,ANY_COMPONENT_CREATED,
        (uintptr_t)self,(uintptr_t)vehicle,cls
    );
}

static void vc_client_destroy_hook(void* self,void* vehicle,void* physics_scene) {
    InterlockedIncrement64(&g_p27_hook_calls[14]); p29_note_hook(14);
    uintptr_t event_vehicle=(uintptr_t)vehicle;
    char cls[ANYMAKER_COMPONENT_CLASS_SNAPSHOT_MAX]{};
    bool matched=component_registry_snapshot_and_destroy(
        (uintptr_t)self,(uintptr_t)vehicle,ANY_COMPONENT_SIDE_CLIENT,
        event_vehicle,cls
    );
    if(matched)
        InterlockedIncrement64(&g_component_client_destroy_registry_match);

    g_vc_client_destroy_original(self,vehicle,physics_scene);

    enqueue_vehicle_component(
        VC_CLIENT_DESTROYED,ANY_COMPONENT_SIDE_CLIENT,ANY_COMPONENT_DESTROYED,
        (uintptr_t)self,event_vehicle,cls
    );
}

static void vc_server_destroy_hook(void* self,void* vehicle,void* server_scene) {
    InterlockedIncrement64(&g_p27_hook_calls[17]); p29_note_hook(17);
    uintptr_t event_vehicle=(uintptr_t)vehicle;
    char cls[ANYMAKER_COMPONENT_CLASS_SNAPSHOT_MAX]{};
    bool matched=component_registry_snapshot_and_destroy(
        (uintptr_t)self,(uintptr_t)vehicle,ANY_COMPONENT_SIDE_SERVER,
        event_vehicle,cls
    );
    if(matched)
        InterlockedIncrement64(&g_component_server_destroy_registry_match);

    g_vc_server_destroy_original(self,vehicle,server_scene);

    enqueue_vehicle_component(
        VC_SERVER_DESTROYED,ANY_COMPONENT_SIDE_SERVER,ANY_COMPONENT_DESTROYED,
        (uintptr_t)self,event_vehicle,cls
    );
}








































































static bool executable_protect(DWORD p) {
    if(p & PAGE_GUARD) return false;
    p &= 0xff;
    return p==PAGE_EXECUTE || p==PAGE_EXECUTE_READ ||
           p==PAGE_EXECUTE_READWRITE || p==PAGE_EXECUTE_WRITECOPY;
}

static std::vector<uintptr_t> scan_exact(const unsigned char* pat,size_t pat_size) {
    std::vector<uintptr_t> hits;
    SYSTEM_INFO si{};
    GetSystemInfo(&si);

    uintptr_t addr=(uintptr_t)si.lpMinimumApplicationAddress;
    uintptr_t max=(uintptr_t)si.lpMaximumApplicationAddress;
    HANDLE proc=GetCurrentProcess();

    constexpr SIZE_T CHUNK=1u<<20;
    std::vector<unsigned char> buf(CHUNK+pat_size+64);

    while(addr<max) {
        MEMORY_BASIC_INFORMATION mbi{};
        SIZE_T q=VirtualQuery((LPCVOID)addr,&mbi,sizeof(mbi));
        if(!q) break;

        uintptr_t base=(uintptr_t)mbi.BaseAddress;
        SIZE_T region=mbi.RegionSize;

        if(mbi.State==MEM_COMMIT && executable_protect(mbi.Protect)) {
            SIZE_T pos=0;
            while(pos<region) {
                SIZE_T core=std::min<SIZE_T>(CHUNK,region-pos);
                SIZE_T want=core;
                if(pos+core<region)
                    want=std::min<SIZE_T>(core+pat_size-1,region-pos);

                SIZE_T got=0;
                if(ReadProcessMemory(proc,(LPCVOID)(base+pos),buf.data(),want,&got) &&
                   got>=pat_size) {
                    auto begin=buf.begin();
                    auto end=buf.begin()+got;
                    auto it=begin;

                    while(it!=end) {
                        it=std::search(it,end,pat,pat+pat_size);
                        if(it==end) break;

                        uintptr_t h=base+pos+(uintptr_t)(it-begin);
                        if(std::find(hits.begin(),hits.end(),h)==hits.end())
                            hits.push_back(h);
                        ++it;
                    }
                }

                if(pos+core>=region) break;
                pos+=core;
            }
        }

        uintptr_t next=base+region;
        if(next<=addr) break;
        addr=next;
    }

    std::sort(hits.begin(),hits.end());
    return hits;
}

struct TargetResolution {
    const char* label;
    const unsigned char* body;
    size_t body_size;
    uintptr_t address;
    size_t hits;
};

static bool resolve_target(TargetResolution& t) {
    auto hits=scan_exact(t.body,t.body_size);t.hits=hits.size();t.address=hits.size()==1?hits[0]:0;
    if(!t.address) for(const auto& semantic:P27_SEMANTIC_TARGETS) {
        if(semantic.body!=t.body) continue;
        auto owners=scan_exact(semantic.owner,semantic.owner_size);
        if(owners.size()!=1) break;
        uintptr_t cell=0,fn=0;
        if(safe_read_memory(owners[0]+semantic.dependency_offset,&cell,8) && cell &&
           safe_read_memory(cell,&fn,8) && fn &&
           std::find(hits.begin(),hits.end(),fn)!=hits.end()) t.address=fn;
        break;
    }
    std::string message="PHASE27_RESOLVE target="+std::string(t.label)+" hits="+std::to_string(t.hits)+
        " resolved="+std::to_string(t.address?1:0);
    if(t.address) message+=" address="+hexptr(t.address);
    log_line(t.address?ANY_LOG_INFO:ANY_LOG_WARN,"framework",message.c_str());return t.address!=0;
}

static bool resolve_target_between(TargetResolution& t,uintptr_t,uintptr_t) { return resolve_target(t); }
#include "legacy_component_transform.inc"
static bool resolve_target_nearest_before(TargetResolution& t,uintptr_t,uintptr_t) { return resolve_target(t); }
static bool resolve_target_nth_before(TargetResolution& t,uintptr_t,uintptr_t,size_t) { return resolve_target(t); }

// Hook machinery.

// -----------------------------------------------------------------------------

static void write_abs_jump(unsigned char* p,uintptr_t dst) {
    p[0]=0xFF; p[1]=0x25;
    p[2]=0; p[3]=0; p[4]=0; p[5]=0;
    std::memcpy(p+6,&dst,8);
}

#include "native_detours.inc"

// GeoCode create_class ABI:
// RCX = hidden 16-byte ref<T> output, RDX = inventory_definition*.
using CreateClassFn=void (*)(void* out_ref,const void* definition);
using ClientRemoveFn=void (*)(void* self,void* insignia_textures);
using ServerDestroyFn=void (*)(void* self,void* server_scene);

// Corrected GeoCode input ABI:
// BEGIN source signature:
//   (bool) actor_character.on_action_digital_begin(self, const s32, client_scene)
// Native:
//   RCX=bool* hidden result, RDX=self, R8=s32* action, R9=client_scene
//
// END source signature:
//   () actor_character.on_action_digital_end(self, const s32)
// Native:
//   RCX=self, RDX=s32* action
using ActorCharacterActionBeginFn=void (*)(uint8_t* out_result,void* actor,const int32_t* action_id,void* client_scene);
using ActorCharacterActionEndFn=void (*)(void* actor,const int32_t* action_id);

// server.on_event(server, server_peer.data, event) is ordinary pointer ABI:
// RCX=server, RDX=server_peer.data, R8=event*
using ServerUseDispatchFn=void (*)(void* server,void* peer_data,void* event_ptr);

static CreateClassFn g_client_create_original=nullptr;
static ClientRemoveFn g_client_remove_original=nullptr;
static CreateClassFn g_server_create_original=nullptr;
static ServerDestroyFn g_server_destroy_original=nullptr;
static ActorCharacterActionBeginFn g_input_begin_original=nullptr;
static ActorCharacterActionEndFn g_input_end_original=nullptr;

static ServerUseDispatchFn g_use_self_original=nullptr;
static ServerUseDispatchFn g_use_actor_original=nullptr;
static ServerUseDispatchFn g_use_vehicle_original=nullptr;
static ServerUseDispatchFn g_use_transform_original=nullptr;

static void client_create_hook(void* out_ref,const void* definition) {
    InterlockedIncrement64(&g_p27_hook_calls[1]); p29_note_hook(1);
    g_client_create_original(out_ref,definition);

    uintptr_t ref0=0,item=0,backlink=0;
    std::memcpy(&ref0,(unsigned char*)out_ref,8);
    std::memcpy(&item,(unsigned char*)out_ref+8,8);

    if(item) {
        InterlockedIncrement64(&g_client_create_nonzero);
        if(safe_read_memory(item+0x28,&backlink,sizeof(backlink)) &&
           backlink==(uintptr_t)definition) {
            InterlockedIncrement64(&g_client_create_backlink_match);
        }

        auto snapshot=capture_definition_snapshot((uintptr_t)definition);
        enqueue_event(
            SLOT_CLIENT_CREATED,ANY_ITEM_SIDE_CLIENT,ANY_ITEM_CREATED,
            (uintptr_t)definition,item,ref0,snapshot
        );
    }
}

static void client_remove_hook(void* self,void* insignia_textures) {
    InterlockedIncrement64(&g_p27_hook_calls[2]); p29_note_hook(2);
    uintptr_t definition=0;
    if(self)
        safe_read_memory((uintptr_t)self+0x28,&definition,sizeof(definition));

    auto snapshot=capture_definition_snapshot(definition);
    if(snapshot.readable)
        InterlockedIncrement64(&g_client_remove_snapshot_ok);

    uintptr_t item=(uintptr_t)self;
    g_client_remove_original(self,insignia_textures);

    enqueue_event(
        SLOT_CLIENT_REMOVED,ANY_ITEM_SIDE_CLIENT,ANY_ITEM_REMOVED,
        definition,item,0,snapshot
    );
}

static void server_create_hook(void* out_ref,const void* definition) {
    InterlockedIncrement64(&g_p27_hook_calls[3]); p29_note_hook(3);
    g_server_create_original(out_ref,definition);

    uintptr_t ref0=0,item=0,backlink=0;
    std::memcpy(&ref0,(unsigned char*)out_ref,8);
    std::memcpy(&item,(unsigned char*)out_ref+8,8);

    if(item) {
        InterlockedIncrement64(&g_server_create_nonzero);

        if(safe_read_memory(item+0x88,&backlink,sizeof(backlink)) &&
           backlink==(uintptr_t)definition) {
            InterlockedIncrement64(&g_server_create_backlink_match);
        }

        auto snapshot=capture_definition_snapshot((uintptr_t)definition);
        enqueue_event(
            SLOT_SERVER_CREATED,ANY_ITEM_SIDE_SERVER,ANY_ITEM_CREATED,
            (uintptr_t)definition,item,ref0,snapshot
        );
    }
}

static void server_destroy_hook(void* self,void* sc) {
    InterlockedIncrement64(&g_p27_hook_calls[4]); p29_note_hook(4);
    uintptr_t definition=0;
    if(self)
        safe_read_memory((uintptr_t)self+0x88,&definition,sizeof(definition));

    auto snapshot=capture_definition_snapshot(definition);
    if(snapshot.readable)
        InterlockedIncrement64(&g_server_destroy_snapshot_ok);

    uintptr_t item=(uintptr_t)self;
    g_server_destroy_original(self,sc);

    enqueue_event(
        SLOT_SERVER_DESTROYED,ANY_ITEM_SIDE_SERVER,ANY_ITEM_DESTROYED,
        definition,item,0,snapshot
    );
}

static void input_begin_hook(
    uint8_t* out_result,
    void* actor,
    const int32_t* action_id,
    void* client_scene
) {
    InterlockedIncrement64(&g_p27_hook_calls[5]); p29_note_hook(5);
    if(actor) InterlockedExchange64(&g_local_actor_hint,(LONG64)(uintptr_t)actor);
    if(client_scene) InterlockedExchange64(&g_local_scene_hint,(LONG64)(uintptr_t)client_scene);

    g_input_begin_original(out_result,actor,action_id,client_scene);

    // Phase 16 v2 fallback/validation path: input_begin is already a proven,
    // local-player game-thread hook. Capture here so player-state getters are
    // exercised even if the continuous per-frame trigger is not installed yet.
    p27_validate_actor_virtuals(actor);
    capture_player_state(actor,client_scene);

    int32_t action=0;
    if(action_id)
        safe_read_memory((uintptr_t)action_id,&action,sizeof(action));

    uint32_t handled=(out_result && *out_result)?1u:0u;

    enqueue_digital(
        DIGITAL_BEGIN,ANY_DIGITAL_ACTION_BEGIN,action,handled,
        (uintptr_t)actor,(uintptr_t)client_scene
    );
}

static void input_end_hook(void* actor,const int32_t* action_id) {
    InterlockedIncrement64(&g_p27_hook_calls[6]); p29_note_hook(6);
    if(actor) InterlockedExchange64(&g_local_actor_hint,(LONG64)(uintptr_t)actor);

    int32_t action=0;
    if(action_id)
        safe_read_memory((uintptr_t)action_id,&action,sizeof(action));

    g_input_end_original(actor,action_id);

    enqueue_digital(
        DIGITAL_END,ANY_DIGITAL_ACTION_END,action,0u,
        (uintptr_t)actor,0
    );
}

static int32_t read_event_s32(void* event_ptr,uintptr_t off,int32_t fallback=0) {
    int32_t v=fallback;
    if(event_ptr)
        safe_read_memory((uintptr_t)event_ptr+off,&v,sizeof(v));
    return v;
}

static void use_self_hook(void* server,void* peer_data,void* event_ptr) {
    InterlockedIncrement64(&g_p27_hook_calls[7]); p29_note_hook(7);
    int32_t actor_id=read_event_s32(event_ptr,0x10,-1);
    int32_t item_id =read_event_s32(event_ptr,0x14,-1);
    int32_t action  =read_event_s32(event_ptr,0x18,-1);

    auto resolved=resolve_server_use_item(server,peer_data,actor_id,item_id);

    g_use_self_original(server,peer_data,event_ptr);

    enqueue_server_use(
        USE_SELF_SLOT,ANY_SERVER_ITEM_USE_SELF,
        actor_id,item_id,action,-1,-1,0,
        (uintptr_t)server,(uintptr_t)peer_data,resolved
    );
}

static void use_actor_hook(void* server,void* peer_data,void* event_ptr) {
    InterlockedIncrement64(&g_p27_hook_calls[8]); p29_note_hook(8);
    int32_t actor_id =read_event_s32(event_ptr,0x10,-1);
    int32_t item_id  =read_event_s32(event_ptr,0x14,-1);
    int32_t action   =read_event_s32(event_ptr,0x18,-1);
    int32_t target_id=read_event_s32(event_ptr,0x1C,-1);

    auto resolved=resolve_server_use_item(server,peer_data,actor_id,item_id);

    g_use_actor_original(server,peer_data,event_ptr);

    enqueue_server_use(
        USE_ACTOR_SLOT,ANY_SERVER_ITEM_USE_ACTOR,
        actor_id,item_id,action,target_id,-1,0,
        (uintptr_t)server,(uintptr_t)peer_data,resolved
    );
}

static void use_vehicle_hook(void* server,void* peer_data,void* event_ptr) {
    InterlockedIncrement64(&g_p27_hook_calls[9]); p29_note_hook(9);
    int32_t actor_id    =read_event_s32(event_ptr,0x10,-1);
    int32_t item_id     =read_event_s32(event_ptr,0x14,-1);
    int32_t action      =read_event_s32(event_ptr,0x18,-1);
    int32_t target_id   =read_event_s32(event_ptr,0x1C,-1);
    int32_t component_id=read_event_s32(event_ptr,0x20,-1);
    int32_t data        =read_event_s32(event_ptr,0x94,0);

    auto resolved=resolve_server_use_item(server,peer_data,actor_id,item_id);

    g_use_vehicle_original(server,peer_data,event_ptr);

    enqueue_server_use(
        USE_VEHICLE_SLOT,ANY_SERVER_ITEM_USE_VEHICLE,
        actor_id,item_id,action,target_id,component_id,data,
        (uintptr_t)server,(uintptr_t)peer_data,resolved
    );
}

static void use_transform_hook(void* server,void* peer_data,void* event_ptr) {
    InterlockedIncrement64(&g_p27_hook_calls[10]); p29_note_hook(10);
    int32_t actor_id=read_event_s32(event_ptr,0x10,-1);
    int32_t item_id =read_event_s32(event_ptr,0x14,-1);
    int32_t action  =read_event_s32(event_ptr,0x18,-1);
    int32_t data    =read_event_s32(event_ptr,0x8C,0);

    auto resolved=resolve_server_use_item(server,peer_data,actor_id,item_id);

    g_use_transform_original(server,peer_data,event_ptr);

    enqueue_server_use(
        USE_TRANSFORM_SLOT,ANY_SERVER_ITEM_USE_TRANSFORM,
        actor_id,item_id,action,-1,-1,data,
        (uintptr_t)server,(uintptr_t)peer_data,resolved
    );
}


// -----------------------------------------------------------------------------
// Phase 27 rendering/UI reverse-engineering + retained world-map projection API.
//
// Static RE from Anymaker's own map screenshot subsystem gives:
//   map center X/Z = 81000 / 52000
//   half width      = 6508
//   atlas size      = 4096x4096
// The game renderer owns three atlas textures at renderer + 0x4E0/0x4E8/0x4F0.
// We re-read the live map globals when their constructor JIT records are found,
// then validate those values against the static constants.
// -----------------------------------------------------------------------------

struct Vec2S32 { int32_t x; int32_t y; };
struct Vec2F64 { double x; double y; };
struct Color8 { uint8_t r; uint8_t g; uint8_t b; uint8_t a; };

using ClientUiRenderFn=void (*)(void* client_ui);
using ClientUiRenderTextureFn=void (*)(void* client_ui,const Vec2S32* position,
                                       const Vec2S32* size,void* asset_texture);
using ClientUiRenderCircleFn=void (*)(void* client_ui,const Vec2S32* position,
                                    const Vec2S32* size,const double* scale,
                                    const Color8* color);
using ClientUiRenderRectangleFn=void (*)(void* client_ui,const Vec2S32* position,
                                        const Vec2S32* size,const Color8* color);
using ClientUiRenderRectangleAbsoluteFn=void (*)(void* client_ui,const Vec2F64* position,
                                                const Vec2F64* size,const Color8* color);
using RendererCreateMapTexturesFn=void (*)(void* renderer);

// Native client_ui_renderer engine entry points referenced indirectly by the
// JIT wrappers. These are discovered from the wrapper's embedded function-cell
// pointers at runtime instead of hardcoding addresses.
using NativeUiRendererBeginFn=void (*)(void* renderer);
using NativeUiRendererRectangleFn=void (*)(void* renderer,const Vec2F64* position,
                                          const Vec2F64* size,const Color8* color);
using NativeUiRendererTextureAssetFn=void (*)(void* renderer,const Vec2F64* position,
                                             const Vec2F64* size,void* asset_texture,
                                             const Color8* color);
using NativeUiRendererRenderFn=void (*)(void* renderer);

static ClientUiRenderFn g_ui_render_original=nullptr;
static ClientUiRenderTextureFn g_ui_render_texture=nullptr;
static ClientUiRenderCircleFn g_ui_render_circle=nullptr;
static ClientUiRenderRectangleFn g_ui_render_rectangle=nullptr;
static ClientUiRenderRectangleAbsoluteFn g_ui_render_rectangle_absolute=nullptr;
static RendererCreateMapTexturesFn g_renderer_create_map_textures=nullptr;

static NativeUiRendererBeginFn g_native_ui_begin=nullptr;
static NativeUiRendererRectangleFn g_native_ui_rectangle=nullptr;
static NativeUiRendererTextureAssetFn g_native_ui_texture_asset=nullptr;
static NativeUiRendererRenderFn g_native_ui_render=nullptr;

static uintptr_t g_renderer_object=0;
static uintptr_t g_map_position_global=0;
static uintptr_t g_map_half_width_global=0;
static double g_map_center_x=81000.0;
static double g_map_center_z=52000.0;
static double g_map_half_width=6508.0;
static volatile LONG g_world_map_visible=0;
static volatile LONG g_map_ui_hook_ready=0;
static volatile LONG64 g_map_render_frames=0;
static volatile LONG64 g_map_texture_create_calls=0;
static volatile LONG64 g_map_texture_ready_frames=0;
static volatile LONG64 g_map_marker_frames=0;
static volatile LONG64 g_map_render_exceptions=0;

// Phase 27 rendering diagnostics.
static uintptr_t g_ui_cell_size_global=0;
static double g_ui_cell_size=0.0;
static volatile LONG g_render_probe_mode=0;
static volatile LONG64 g_render_probe_hook_frames=0;
static volatile LONG64 g_render_probe_cell_rect=0;
static volatile LONG64 g_render_probe_abs_rect=0;
static volatile LONG64 g_render_probe_native_rect=0;
static volatile LONG64 g_render_probe_viewport_frames=0;
static volatile LONG64 g_render_probe_single_texture=0;
static volatile LONG64 g_render_probe_native_map=0;
static volatile LONG64 g_render_probe_marker=0;
static volatile LONG64 g_render_probe_marker_cell_fallback=0;
static volatile LONG64 g_render_probe_flushes=0;
static volatile LONG64 g_render_probe_exceptions=0;
static uintptr_t g_last_client_ui_renderer=0;
static Vec2F64 g_last_ui_origin{0.0,0.0};
static Vec2F64 g_last_ui_viewport{0.0,0.0};
static Vec2F64 g_last_map_position{0.0,0.0};
static Vec2F64 g_last_map_size{0.0,0.0};
static Vec2F64 g_last_marker_position{0.0,0.0};
static uintptr_t g_absolute_rectangle_wrapper=0;
static volatile LONG g_ui_object_logged=0;


// -----------------------------------------------------------------------------
// Runtime API v16 generic UI callback registry.
//
// This is the public boundary we ultimately want mod authors to use. External
// mods never receive the native client_ui/client_ui_renderer pointers. Draw
// calls are accepted only while the framework is synchronously invoking that
// mod from the game UI render hook.
// -----------------------------------------------------------------------------
struct UiRenderRegistration {
    const char* mod_id;
    AnymakerUiRenderCallback callback;
    void* user_data;
};

static constexpr LONG MAX_UI_RENDER_CALLBACKS=32;
static UiRenderRegistration g_ui_render_callbacks[MAX_UI_RENDER_CALLBACKS]{};
static LONG g_ui_render_callback_count=0;
static SRWLOCK g_ui_render_callback_lock=SRWLOCK_INIT;

static volatile LONG64 g_ui_api_frames=0;
static volatile LONG64 g_ui_api_callback_invocations=0;
static volatile LONG64 g_ui_api_rect_draws=0;
static volatile LONG64 g_ui_api_texture_draws=0;
static volatile LONG64 g_ui_api_texture_resolve_ok=0;
static volatile LONG64 g_ui_api_texture_resolve_fail=0;
static volatile LONG64 g_ui_api_rejected_draws=0;
static volatile LONG64 g_ui_api_callback_exceptions=0;
static volatile LONG64 g_ui_api_key_queries=0;

static thread_local bool g_ui_api_callback_active=false;
static thread_local uintptr_t g_ui_api_renderer_tls=0;
static thread_local uint64_t g_ui_api_frame_id_tls=0;
static thread_local double g_ui_api_viewport_w_tls=0.0;
static thread_local double g_ui_api_viewport_h_tls=0.0;

static bool register_ui_render(const char*,AnymakerUiRenderCallback,void*) { return false; }


static bool ui_frame_is_current(const AnymakerUiRenderFrameV1* frame) {
    return frame &&
           frame->frame_version==ANYMAKER_UI_RENDER_FRAME_VERSION &&
           g_ui_api_callback_active &&
           frame->frame_id==g_ui_api_frame_id_tls &&
           g_ui_api_renderer_tls!=0;
}

static Color8 unpack_rgba8(uint32_t rgba) {
    Color8 c{};
    c.r=(uint8_t)(rgba & 0xffu);
    c.g=(uint8_t)((rgba>>8) & 0xffu);
    c.b=(uint8_t)((rgba>>16) & 0xffu);
    c.a=(uint8_t)((rgba>>24) & 0xffu);
    return c;
}

static bool ui_draw_rect(
    const AnymakerUiRenderFrameV1* frame,
    double x,double y,double width,double height,
    uint32_t rgba8
) {
    return false; // Phase27 renderer quarantine.

    if(!ui_frame_is_current(frame) || !g_native_ui_rectangle ||
       !std::isfinite(x) || !std::isfinite(y) ||
       !std::isfinite(width) || !std::isfinite(height) ||
       width<=0.0 || height<=0.0) {
        InterlockedIncrement64(&g_ui_api_rejected_draws);
        return false;
    }

    // Public API: top-left origin, +Y down.
    // Native renderer: center origin, +Y up, rectangle position is lower-left.
    Vec2F64 p{
        -g_ui_api_viewport_w_tls*0.5 + x,
         g_ui_api_viewport_h_tls*0.5 - y - height
    };
    Vec2F64 size{width,height};
    Color8 color=unpack_rgba8(rgba8);
    g_native_ui_rectangle((void*)g_ui_api_renderer_tls,&p,&size,&color);
    InterlockedIncrement64(&g_ui_api_rect_draws);
    return true;
}

static bool is_virtual_key_down(uint32_t virtual_key) {
    InterlockedIncrement64(&g_ui_api_key_queries);
    if(virtual_key>0xffu) return false;
    HWND hwnd=GetForegroundWindow();
    if(!hwnd) return false;
    DWORD pid=0;
    GetWindowThreadProcessId(hwnd,&pid);
    if(pid!=GetCurrentProcessId()) return false;
    return (GetAsyncKeyState((int)virtual_key)&0x8000)!=0;
}


// -----------------------------------------------------------------------------
// Runtime API v16 registered mod actions + native Mod Controls section.
// -----------------------------------------------------------------------------
// The public mod sees semantic BEGIN/END events. The framework privately owns
// physical key polling, persistence, and the game-menu integration. Phase 27
// deliberately chooses a dedicated "MOD CONTROLS" section inside the existing
// Controls screen rather than mixing mod actions into vanilla categories. A
// separate top-level tab can be pursued later once the options-router ABI is
// proven; this section is the lower-risk native integration point.

struct SavedModActionBinding {
    std::string mod_id;
    std::string action_id;
    uint32_t virtual_key{};
};

struct ModActionRegistration {
    char mod_id[128]{};
    char action_id[ANYMAKER_MOD_ACTION_ID_MAX]{};
    char display_name[ANYMAKER_MOD_ACTION_LABEL_MAX]{};
    uint32_t virtual_key{};
    bool was_down{};
    AnymakerModActionCallback callback{};
    void* user_data{};
};

static constexpr LONG MAX_MOD_ACTIONS=64;
static ModActionRegistration g_mod_actions[MAX_MOD_ACTIONS]{};
static LONG g_mod_action_count=0;
static SRWLOCK g_mod_action_lock=SRWLOCK_INIT;
static std::vector<SavedModActionBinding> g_saved_mod_action_bindings;
static bool g_saved_mod_action_bindings_loaded=false;
static SRWLOCK g_saved_mod_action_lock=SRWLOCK_INIT;
static volatile LONG64 g_mod_action_begin_events=0;
static volatile LONG64 g_mod_action_end_events=0;
static volatile LONG64 g_mod_action_binding_file_loads=0;
static volatile LONG64 g_mod_action_binding_file_writes=0;

static volatile LONG g_controls_menu_hook_ready=0;
static volatile LONG64 g_controls_menu_last_seen_ms=0;
static volatile LONG g_controls_menu_seen_count=0;
static volatile LONG g_controls_footer_hook_ready=0;
static volatile LONG64 g_controls_section_last_seen_ms=0;
static volatile LONG64 g_controls_section_frames=0;
static volatile LONG64 g_controls_button_clicks=0;
static volatile LONG64 g_controls_rebind_commits=0;
static volatile LONG64 g_controls_rebind_cancels=0;
static volatile LONG64 g_controls_ui_exceptions=0;
static volatile LONG g_mod_controls_capture_index=-1;
static volatile LONG64 g_mod_controls_capture_started_ms=0;
static volatile LONG g_mod_controls_capture_wait_release=0;

static uintptr_t g_controls_ui_begin_address=0;
static uintptr_t g_controls_ui_rectangle_address=0;
static uintptr_t g_controls_footer_address=0;
static uintptr_t g_p26_controls_body=0;
using P26EndContainerFn=void (*)(void*);
using P26TableFn=void (*)(void*,const GeoString16*,const int32_t*,const Vec2S32*,const int32_t*);
static P26EndContainerFn g_p26_end_original=nullptr;
static P26TableFn g_p26_table=nullptr;
static bool read_indirect_function_cell(uintptr_t,uintptr_t,uintptr_t&,uintptr_t&);
// Replace only this body's dependency reference, preserving the game's original
// shared callable cell. This does not globally redirect end_container/push_back.
static bool p26_replace_dependency(uintptr_t slot,uintptr_t expected,uintptr_t replacement) {
    return false; // Quarantined: Phase27 never rewrites a game dependency.
    if(slot%8) return false;
    DWORD old=0;
    if(!VirtualProtect((void*)slot,8,PAGE_EXECUTE_READWRITE,&old)) return false;
    auto previous=InterlockedCompareExchangePointer((void* volatile*)slot,(void*)replacement,(void*)expected);
    DWORD ignored=0;VirtualProtect((void*)slot,8,old,&ignored);
    return (uintptr_t)previous==expected;
}
static bool p26_begin_table(void* ui,const GeoString16* id,const int32_t* columns,const Vec2S32* size,const int32_t* spacing) {
    __try { g_p26_table(ui,id,columns,size,spacing);return true; }
    __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}
static void p26_end_table(void* ui) {
    __try { g_p26_end_original(ui); }
    __except(EXCEPTION_EXECUTE_HANDLER) { log_line(ANY_LOG_ERROR,"framework","PHASE27_CONTROLS end_table_exception=1"); }
}

static uintptr_t g_controls_heading_address=0;
static uintptr_t g_controls_button_address=0;

using ControlsBuilderFn=void (*)(void* frontend_ui,void* settings,void* options_ui_data);
using ControlsFooterFn=void (*)(void* frontend_ui,void* options_ui_data,void* is_valid,void* footer_text);
using ClientUiHeadingFn=void (*)(void* client_ui,const GeoString16* id,const GeoString16* text);
using ClientUiButtonFn=void (*)(
    uint8_t* out_clicked,
    void* client_ui,
    const GeoString16* id,
    const GeoString16* text,
    const Vec2S32* size,
    const uintptr_t* icon,
    const bool* selected,
    const bool* disabled
);

static ControlsBuilderFn g_controls_builder_original=nullptr;
static ControlsFooterFn g_controls_footer_original=nullptr;
static ClientUiHeadingFn g_controls_heading=nullptr;
static ClientUiButtonFn g_controls_button=nullptr;
static thread_local bool g_inside_controls_builder=false;

static GeoString16 make_const_geostring(const char* s) {
    GeoString16 out{};
    if(!s) return out;
    size_t n=std::strlen(s);
    if(n>0xffffffffu) n=0xffffffffu;
    out.data=(uintptr_t)s;
    out.length=(uint32_t)n;
    out.capacity=(uint32_t)n;
    return out;
}

static bool seh_controls_heading_call(
    ClientUiHeadingFn fn,void* ui,const GeoString16* id,const GeoString16* text
) {
    if(!fn || !ui || !id || !text) return false;
    __try { fn(ui,id,text); return true; }
    __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}

static bool seh_controls_button_call(
    ClientUiButtonFn fn,uint8_t* out_clicked,void* ui,
    const GeoString16* id,const GeoString16* text,const Vec2S32* size,
    const uintptr_t* icon,const bool* selected,const bool* disabled
) {
    if(out_clicked) *out_clicked=0;
    if(!fn || !out_clicked || !ui || !id || !text || !size || !icon || !selected || !disabled)
        return false;
    __try {
        fn(out_clicked,ui,id,text,size,icon,selected,disabled);
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}

static std::string virtual_key_display_name(uint32_t vk) {
    if(vk>='A' && vk<='Z') return std::string(1,(char)vk);
    if(vk>='0' && vk<='9') return std::string(1,(char)vk);
    if(vk>=VK_F1 && vk<=VK_F24) return "F"+std::to_string((unsigned)(vk-VK_F1+1));
    switch(vk) {
        case VK_SPACE: return "Space";
        case VK_TAB: return "Tab";
        case VK_RETURN: return "Enter";
        case VK_BACK: return "Backspace";
        case VK_DELETE: return "Delete";
        case VK_INSERT: return "Insert";
        case VK_HOME: return "Home";
        case VK_END: return "End";
        case VK_PRIOR: return "Page Up";
        case VK_NEXT: return "Page Down";
        case VK_UP: return "Up";
        case VK_DOWN: return "Down";
        case VK_LEFT: return "Left";
        case VK_RIGHT: return "Right";
        case VK_SHIFT: return "Shift";
        case VK_CONTROL: return "Ctrl";
        case VK_MENU: return "Alt";
        case VK_ESCAPE: return "Escape";
        default: break;
    }
    UINT sc=MapVirtualKeyA(vk,MAPVK_VK_TO_VSC);
    if(sc) {
        char name[96]{};
        LONG lp=(LONG)(sc<<16);
        if(vk==VK_LEFT || vk==VK_UP || vk==VK_RIGHT || vk==VK_DOWN ||
           vk==VK_PRIOR || vk==VK_NEXT || vk==VK_END || vk==VK_HOME ||
           vk==VK_INSERT || vk==VK_DELETE) lp|=(1<<24);
        if(GetKeyNameTextA(lp,name,(int)sizeof(name))>0 && name[0]) return name;
    }
    char b[24]{};
    std::snprintf(b,sizeof(b),"VK_%02X",(unsigned)vk);
    return b;
}

static void copy_cstr_trunc(char* dst,size_t cap,const char* src);

static bool controls_native_section_ready() {
    return InterlockedCompareExchange(&g_controls_footer_hook_ready,0,0)!=0 &&
           g_controls_heading && g_controls_button && g_p26_table && g_p26_end_original;
}

static void begin_mod_action_rebind_capture(LONG index) {
    AcquireSRWLockShared(&g_mod_action_lock);
    bool valid=index>=0 && index<g_mod_action_count;
    char mod_id[128]{};
    char action_id[ANYMAKER_MOD_ACTION_ID_MAX]{};
    if(valid) {
        copy_cstr_trunc(mod_id,sizeof(mod_id),g_mod_actions[index].mod_id);
        copy_cstr_trunc(action_id,sizeof(action_id),g_mod_actions[index].action_id);
    }
    ReleaseSRWLockShared(&g_mod_action_lock);
    if(!valid) return;

    InterlockedExchange(&g_mod_controls_capture_index,index);
    InterlockedExchange64(&g_mod_controls_capture_started_ms,(LONG64)GetTickCount64());
    InterlockedExchange(&g_mod_controls_capture_wait_release,1);
    InterlockedIncrement64(&g_controls_button_clicks);
    char b[384]{};
    std::snprintf(b,sizeof(b),
        "PHASE27_MOD_CONTROLS_CAPTURE_BEGIN index=%ld mod=\"%s\" action=\"%s\"",
        (long)index,mod_id,action_id);
    log_line(ANY_LOG_INFO,"framework",b);
}

static void render_mod_controls_section(void* frontend_ui) {
    if(!frontend_ui || !g_controls_heading || !g_controls_button) return;

    ModActionRegistration local[MAX_MOD_ACTIONS]{};
    LONG count=0;
    AcquireSRWLockShared(&g_mod_action_lock);
    count=g_mod_action_count;
    for(LONG i=0;i<count;++i) local[i]=g_mod_actions[i];
    ReleaseSRWLockShared(&g_mod_action_lock);
    if(count<=0) return;

    const char* title_id_c="anymaker_mod_controls_heading";
    const char* title_text_c="MOD CONTROLS";
    GeoString16 title_id=make_const_geostring(title_id_c);
    GeoString16 title_text=make_const_geostring(title_text_c);
    if(!seh_controls_heading_call(g_controls_heading,frontend_ui,&title_id,&title_text)) {
        InterlockedIncrement64(&g_controls_ui_exceptions);
        return;
    }

    GeoString16 table_id=make_const_geostring("anymaker_mod_controls_rows");
    int32_t columns=1,spacing=0;
    Vec2S32 row_size{24,1};
    if(!p26_begin_table(frontend_ui,&table_id,&columns,&row_size,&spacing)) {
        InterlockedIncrement64(&g_controls_ui_exceptions);return;
    }
    LONG capture=InterlockedCompareExchange(&g_mod_controls_capture_index,-1,-1);
    for(LONG i=0;i<count;++i) {
        std::string id="anymaker_mod_control_"+std::to_string((long long)i);
        std::string label;
        if(capture==i) {
            label=std::string(local[i].display_name)+"  [press a key - Esc cancels]";
        } else {
            label=std::string(local[i].display_name)+"  ["+
                  virtual_key_display_name(local[i].virtual_key)+"]";
        }
        GeoString16 gid=make_const_geostring(id.c_str());
        GeoString16 glabel=make_const_geostring(label.c_str());
        Vec2S32 button_size{24,1};
        uintptr_t icon=0;
        bool selected=(capture==i);
        bool disabled=false;
        uint8_t clicked=0;
        if(!seh_controls_button_call(
            g_controls_button,&clicked,frontend_ui,&gid,&glabel,&button_size,
            &icon,&selected,&disabled)) {
            InterlockedIncrement64(&g_controls_ui_exceptions);
            continue;
        }
        if(clicked && capture<0) begin_mod_action_rebind_capture(i);
    }

    p26_end_table(frontend_ui);
    InterlockedExchange64(&g_controls_section_last_seen_ms,(LONG64)GetTickCount64());
    InterlockedIncrement64(&g_controls_section_frames);
}

static void p26_render_section_seh(void* ui) {
    __try { render_mod_controls_section(ui); }
    __except(EXCEPTION_EXECUTE_HANDLER) { InterlockedIncrement64(&g_controls_ui_exceptions); }
}
static void p26_controls_end(void* ui) {
    uintptr_t caller=(uintptr_t)_ReturnAddress();
    // GCL builder +0x74CE calls end_container for the scroll; its return is
    // +0x74D0. Earlier table/layout closures have already run at this point.
    if(g_inside_controls_builder && caller==g_p26_controls_body+0x74D0) {
        LONG64 frames=InterlockedCompareExchange64(&g_controls_section_frames,0,0);
        if(frames<3) {
            char message[256]{};
            std::snprintf(message,sizeof(message),"PHASE27_CONTROLS_SCROLL_INSERT return_offset=0x74D0 ui=%p native_parent_open=1 rows=registered_actions",ui);
            log_line(ANY_LOG_INFO,"framework",message);
        }
        p26_render_section_seh(ui);
    }
    g_p26_end_original(ui);
}
static P26EndContainerFn g_p26_end_cell=&p26_controls_end;
static bool ensure_mod_controls_native_section() {
    return false; // Detection-only Controls observer until insertion is revalidated.
    if(controls_native_section_ready()) return true;
    if(!g_p26_controls_body) return false;
    uintptr_t cell=0,fn=0;
    // Use the builder's own dependency cells; do not scan ambiguous wrappers.
    if(read_indirect_function_cell(g_p26_controls_body,0x8438,cell,fn)) g_controls_heading=(ClientUiHeadingFn)fn;
    if(read_indirect_function_cell(g_p26_controls_body,0x8490,cell,fn)) g_controls_button=(ClientUiButtonFn)fn;
    if(read_indirect_function_cell(g_p26_controls_body,0x8448,cell,fn)) g_p26_table=(P26TableFn)fn;
    if(!g_controls_heading || !g_controls_button || !g_p26_table) return false;
    if(!g_p26_end_original && read_indirect_function_cell(g_p26_controls_body,0x8440,cell,fn)) {
        g_p26_end_original=(P26EndContainerFn)fn;
        if(p26_replace_dependency(g_p26_controls_body+0x8440,cell,(uintptr_t)&g_p26_end_cell)) {
            InterlockedExchange(&g_controls_footer_hook_ready,1);
            log_line(ANY_LOG_INFO,"framework","PHASE27_MOD_CONTROLS_SECTION_READY mode=native_scroll_table native_apply_reset=unimplemented");
        } else g_p26_end_original=nullptr;
    }
    return controls_native_section_ready();
}

static void controls_builder_hook(void* frontend_ui,void* settings,void* options_ui_data) {
    InterlockedIncrement64(&g_p27_hook_calls[20]); p29_note_hook(20);
    InterlockedExchange64(&g_controls_menu_last_seen_ms,(LONG64)GetTickCount64());
    LONG seen=InterlockedIncrement(&g_controls_menu_seen_count);

    // First Controls frame may materialize dependencies lazily. Re-run this on
    // every early frame until the native heading/button/footer bridge is ready.
    if(!controls_native_section_ready()) ensure_mod_controls_native_section();

    if(seen<=3) {
        char b[384]{};
        std::snprintf(b,sizeof(b),
            "PHASE27_CONTROLS_BRIDGE_SEEN count=%ld frontend_ui=%p settings_nonzero=%d options_nonzero=%d section_ready=%d mode=observer_only",
            (long)seen,frontend_ui,settings?1:0,options_ui_data?1:0,
            controls_native_section_ready()?1:0);
        log_line(ANY_LOG_INFO,"framework",b);
    }
    g_inside_controls_builder=true;
    if(g_controls_builder_original)
        g_controls_builder_original(frontend_ui,settings,options_ui_data);
    g_inside_controls_builder=false;
}

static std::filesystem::path mod_action_binding_path() {
    return g_game_dir/"mods"/"anymaker_mod_bindings.tsv";
}

static void copy_cstr_trunc(char* dst,size_t cap,const char* src) {
    if(!dst || cap==0) return;
    dst[0]='\0';
    if(!src) return;
    std::strncpy(dst,src,cap-1);
    dst[cap-1]='\0';
}

static bool valid_action_key(const char* s) {
    if(!s || !*s) return false;
    size_t n=std::strlen(s);
    if(n>=ANYMAKER_MOD_ACTION_ID_MAX) return false;
    for(size_t i=0;i<n;++i) {
        unsigned char c=(unsigned char)s[i];
        if(!(std::isalnum(c) || c=='_' || c=='-' || c=='.')) return false;
    }
    return true;
}

static void load_saved_mod_action_bindings_locked() {
    if(g_saved_mod_action_bindings_loaded) return;
    g_saved_mod_action_bindings_loaded=true;
    g_saved_mod_action_bindings.clear();

    std::ifstream f(mod_action_binding_path());
    std::string line;
    while(std::getline(f,line)) {
        if(line.empty() || line[0]=='#') continue;
        size_t a=line.find('\t');
        size_t b=(a==std::string::npos)?std::string::npos:line.find('\t',a+1);
        if(a==std::string::npos || b==std::string::npos) continue;
        SavedModActionBinding rec{};
        rec.mod_id=line.substr(0,a);
        rec.action_id=line.substr(a+1,b-a-1);
        try {
            unsigned long v=std::stoul(line.substr(b+1),nullptr,0);
            if(v<=0xffu) rec.virtual_key=(uint32_t)v;
        } catch(...) { rec.virtual_key=0; }
        if(!rec.mod_id.empty() && valid_action_key(rec.action_id.c_str()) && rec.virtual_key)
            g_saved_mod_action_bindings.push_back(rec);
    }
    InterlockedIncrement64(&g_mod_action_binding_file_loads);
}

static bool write_saved_mod_action_bindings_locked() {
    std::error_code ec;
    std::filesystem::create_directories(g_game_dir/"mods",ec);
    std::ofstream f(mod_action_binding_path(),std::ios::trunc);
    if(!f) return false;
    f << "# Anymaker Modding API registered actions - Phase 27 / Runtime API v16\n";
    f << "# mod_id<TAB>action_id<TAB>windows_virtual_key\n";
    for(const auto& rec:g_saved_mod_action_bindings) {
        f << rec.mod_id << '\t' << rec.action_id << '\t' << rec.virtual_key << "\n";
    }
    f.flush();
    bool ok=f.good();
    if(ok) InterlockedIncrement64(&g_mod_action_binding_file_writes);
    return ok;
}

static uint32_t saved_mod_action_key_locked(
    const char* mod_id,const char* action_id,uint32_t fallback
) {
    load_saved_mod_action_bindings_locked();
    for(const auto& rec:g_saved_mod_action_bindings) {
        if(rec.mod_id==mod_id && rec.action_id==action_id)
            return rec.virtual_key;
    }
    SavedModActionBinding rec{};
    rec.mod_id=mod_id;
    rec.action_id=action_id;
    rec.virtual_key=fallback;
    g_saved_mod_action_bindings.push_back(rec);
    write_saved_mod_action_bindings_locked();
    return fallback;
}

static bool register_mod_action(
    const char* mod_id,const char* action_id,const char* display_name,
    uint32_t default_virtual_key,AnymakerModActionCallback callback,void* user_data
) {
    if(!mod_id || !*mod_id || !valid_action_key(action_id) || !display_name || !*display_name ||
       default_virtual_key==0 || default_virtual_key>0xffu || !callback) return false;

    AcquireSRWLockExclusive(&g_mod_action_lock);
    for(LONG i=0;i<g_mod_action_count;++i) {
        if(std::strcmp(g_mod_actions[i].mod_id,mod_id)==0 &&
           std::strcmp(g_mod_actions[i].action_id,action_id)==0) {
            ReleaseSRWLockExclusive(&g_mod_action_lock);
            return false;
        }
    }
    if(g_mod_action_count>=MAX_MOD_ACTIONS) {
        ReleaseSRWLockExclusive(&g_mod_action_lock);
        log_line(ANY_LOG_ERROR,"framework","Too many registered mod actions.");
        return false;
    }

    uint32_t key=default_virtual_key;
    AcquireSRWLockExclusive(&g_saved_mod_action_lock);
    key=saved_mod_action_key_locked(mod_id,action_id,default_virtual_key);
    ReleaseSRWLockExclusive(&g_saved_mod_action_lock);

    LONG slot=g_mod_action_count++;
    auto& r=g_mod_actions[slot];
    copy_cstr_trunc(r.mod_id,sizeof(r.mod_id),mod_id);
    copy_cstr_trunc(r.action_id,sizeof(r.action_id),action_id);
    copy_cstr_trunc(r.display_name,sizeof(r.display_name),display_name);
    r.virtual_key=key;
    r.was_down=false;
    r.callback=callback;
    r.user_data=user_data;
    ReleaseSRWLockExclusive(&g_mod_action_lock);

    char b[512]{};
    std::snprintf(b,sizeof(b),
        "PHASE27_MOD_ACTION_REGISTER mod=\"%s\" action=\"%s\" label=\"%s\" default_vk=%u active_vk=%u persisted=%d",
        mod_id,action_id,display_name,(unsigned)default_virtual_key,(unsigned)key,
        key!=default_virtual_key?1:0);
    log_line(ANY_LOG_INFO,"framework",b);
    return true;
}

static bool get_mod_action_binding(
    const char* mod_id,const char* action_id,uint32_t* out_virtual_key
) {
    if(out_virtual_key) *out_virtual_key=0;
    if(!mod_id || !action_id || !out_virtual_key) return false;
    AcquireSRWLockShared(&g_mod_action_lock);
    for(LONG i=0;i<g_mod_action_count;++i) {
        const auto& r=g_mod_actions[i];
        if(std::strcmp(r.mod_id,mod_id)==0 && std::strcmp(r.action_id,action_id)==0) {
            *out_virtual_key=r.virtual_key;
            ReleaseSRWLockShared(&g_mod_action_lock);
            return true;
        }
    }
    ReleaseSRWLockShared(&g_mod_action_lock);
    return false;
}

static bool set_mod_action_binding(
    const char* mod_id,const char* action_id,uint32_t virtual_key
) {
    if(!mod_id || !action_id || virtual_key==0 || virtual_key>0xffu) return false;
    bool found=false;
    AcquireSRWLockExclusive(&g_mod_action_lock);
    for(LONG i=0;i<g_mod_action_count;++i) {
        auto& r=g_mod_actions[i];
        if(std::strcmp(r.mod_id,mod_id)==0 && std::strcmp(r.action_id,action_id)==0) {
            r.virtual_key=virtual_key;
            r.was_down=false;
            found=true;
            break;
        }
    }
    ReleaseSRWLockExclusive(&g_mod_action_lock);
    if(!found) return false;

    bool write_ok=false;
    AcquireSRWLockExclusive(&g_saved_mod_action_lock);
    load_saved_mod_action_bindings_locked();
    bool saved=false;
    for(auto& rec:g_saved_mod_action_bindings) {
        if(rec.mod_id==mod_id && rec.action_id==action_id) {
            rec.virtual_key=virtual_key; saved=true; break;
        }
    }
    if(!saved) g_saved_mod_action_bindings.push_back({mod_id,action_id,virtual_key});
    write_ok=write_saved_mod_action_bindings_locked();
    ReleaseSRWLockExclusive(&g_saved_mod_action_lock);

    char b[384]{};
    std::snprintf(b,sizeof(b),
        "PHASE27_MOD_ACTION_REBIND mod=\"%s\" action=\"%s\" vk=%u persisted=%d",
        mod_id,action_id,(unsigned)virtual_key,write_ok?1:0);
    log_line(write_ok?ANY_LOG_INFO:ANY_LOG_WARN,"framework",b);
    return write_ok;
}

static bool get_mod_action_api_info(AnymakerModActionApiInfoV1* out) {
    if(!out) return false;
    out->struct_size=sizeof(*out);
    out->info_version=ANYMAKER_MOD_ACTION_API_INFO_VERSION;
    AcquireSRWLockShared(&g_mod_action_lock);
    out->registered_actions=(uint32_t)g_mod_action_count;
    ReleaseSRWLockShared(&g_mod_action_lock);
    out->controls_menu_hook_ready=(uint32_t)(InterlockedCompareExchange(&g_controls_menu_hook_ready,0,0)!=0);
    LONG64 last=InterlockedCompareExchange64(&g_controls_menu_last_seen_ms,0,0);
    LONG64 section_last=InterlockedCompareExchange64(&g_controls_section_last_seen_ms,0,0);
    LONG64 now=(LONG64)GetTickCount64();
    out->controls_menu_recently_seen=(last!=0 && now-last<500)?1u:0u;
    out->controls_section_ready=controls_native_section_ready()?1u:0u;
    out->controls_section_visible=(section_last!=0 && now-section_last<500)?1u:0u;
    out->rebind_capture_active=(InterlockedCompareExchange(&g_mod_controls_capture_index,-1,-1)>=0)?1u:0u;
    out->controls_section_mode=1u;
    out->reserved0=0;
    out->begin_events=(uint64_t)InterlockedCompareExchange64(&g_mod_action_begin_events,0,0);
    out->end_events=(uint64_t)InterlockedCompareExchange64(&g_mod_action_end_events,0,0);
    out->binding_file_loads=(uint64_t)InterlockedCompareExchange64(&g_mod_action_binding_file_loads,0,0);
    out->binding_file_writes=(uint64_t)InterlockedCompareExchange64(&g_mod_action_binding_file_writes,0,0);
    out->controls_section_frames=(uint64_t)InterlockedCompareExchange64(&g_controls_section_frames,0,0);
    out->controls_button_clicks=(uint64_t)InterlockedCompareExchange64(&g_controls_button_clicks,0,0);
    out->rebind_commits=(uint64_t)InterlockedCompareExchange64(&g_controls_rebind_commits,0,0);
    out->rebind_cancels=(uint64_t)InterlockedCompareExchange64(&g_controls_rebind_cancels,0,0);
    out->controls_ui_exceptions=(uint64_t)InterlockedCompareExchange64(&g_controls_ui_exceptions,0,0);
    return true;
}

static bool mod_controls_capture_any_key_down() {
    for(uint32_t vk=8;vk<=0xfeu;++vk) {
        if((GetAsyncKeyState((int)vk)&0x8000)!=0) return true;
    }
    return false;
}

static void cancel_mod_controls_rebind_capture(const char* reason) {
    LONG index=InterlockedExchange(&g_mod_controls_capture_index,-1);
    InterlockedExchange(&g_mod_controls_capture_wait_release,0);
    if(index>=0) {
        InterlockedIncrement64(&g_controls_rebind_cancels);
        char b[256]{};
        std::snprintf(b,sizeof(b),"PHASE27_MOD_CONTROLS_CAPTURE_CANCEL index=%ld reason=%s",
                      (long)index,reason?reason:"unknown");
        log_line(ANY_LOG_INFO,"framework",b);
    }
}

static void poll_mod_controls_rebind_capture() {
    LONG index=InterlockedCompareExchange(&g_mod_controls_capture_index,-1,-1);
    if(index<0) return;

    LONG64 now=(LONG64)GetTickCount64();
    LONG64 controls_last=InterlockedCompareExchange64(&g_controls_menu_last_seen_ms,0,0);
    if(!controls_last || now-controls_last>1500) {
        cancel_mod_controls_rebind_capture("controls_closed");
        return;
    }

    LONG64 started=InterlockedCompareExchange64(&g_mod_controls_capture_started_ms,0,0);
    if(now-started>30000) {
        cancel_mod_controls_rebind_capture("timeout");
        return;
    }

    if(InterlockedCompareExchange(&g_mod_controls_capture_wait_release,0,0)!=0) {
        if(now-started<150 || mod_controls_capture_any_key_down()) return;
        InterlockedExchange(&g_mod_controls_capture_wait_release,0);
        return;
    }

    uint32_t chosen=0;
    for(uint32_t vk=8;vk<=0xfeu;++vk) {
        if((GetAsyncKeyState((int)vk)&0x8000)!=0) { chosen=vk; break; }
    }
    if(!chosen) return;
    if(chosen==VK_ESCAPE) {
        cancel_mod_controls_rebind_capture("escape");
        return;
    }

    char mod_id[128]{};
    char action_id[ANYMAKER_MOD_ACTION_ID_MAX]{};
    char display_name[ANYMAKER_MOD_ACTION_LABEL_MAX]{};
    bool valid=false;
    AcquireSRWLockShared(&g_mod_action_lock);
    if(index>=0 && index<g_mod_action_count) {
        copy_cstr_trunc(mod_id,sizeof(mod_id),g_mod_actions[index].mod_id);
        copy_cstr_trunc(action_id,sizeof(action_id),g_mod_actions[index].action_id);
        copy_cstr_trunc(display_name,sizeof(display_name),g_mod_actions[index].display_name);
        valid=true;
    }
    ReleaseSRWLockShared(&g_mod_action_lock);
    if(!valid) {
        cancel_mod_controls_rebind_capture("action_missing");
        return;
    }

    bool ok=set_mod_action_binding(mod_id,action_id,chosen);
    if(ok) {
        // The captured key is currently held. Mark it down so the mod does not
        // receive a BEGIN until the user releases and presses it normally.
        AcquireSRWLockExclusive(&g_mod_action_lock);
        if(index>=0 && index<g_mod_action_count &&
           std::strcmp(g_mod_actions[index].mod_id,mod_id)==0 &&
           std::strcmp(g_mod_actions[index].action_id,action_id)==0) {
            g_mod_actions[index].was_down=true;
        }
        ReleaseSRWLockExclusive(&g_mod_action_lock);
        InterlockedIncrement64(&g_controls_rebind_commits);
        InterlockedExchange(&g_mod_controls_capture_index,-1);
        InterlockedExchange(&g_mod_controls_capture_wait_release,0);
        std::string key=virtual_key_display_name(chosen);
        char b[512]{};
        std::snprintf(b,sizeof(b),
            "PHASE27_MOD_CONTROLS_REBIND_COMMIT mod=\"%s\" action=\"%s\" label=\"%s\" vk=%u key=\"%s\"",
            mod_id,action_id,display_name,(unsigned)chosen,key.c_str());
        log_line(ANY_LOG_INFO,"framework",b);
    } else {
        cancel_mod_controls_rebind_capture("persist_failed");
    }
}

static void poll_registered_mod_actions() {
    poll_mod_controls_rebind_capture();
    if(InterlockedCompareExchange(&g_mod_controls_capture_index,-1,-1)>=0) return;

    ModActionRegistration local[MAX_MOD_ACTIONS]{};
    LONG count=0;
    AcquireSRWLockExclusive(&g_mod_action_lock);
    count=g_mod_action_count;
    for(LONG i=0;i<count;++i) {
        bool down=is_virtual_key_down(g_mod_actions[i].virtual_key);
        if(down!=g_mod_actions[i].was_down) {
            local[i]=g_mod_actions[i];
            local[i].was_down=down;
            g_mod_actions[i].was_down=down;
        } else {
            local[i].callback=nullptr;
        }
    }
    ReleaseSRWLockExclusive(&g_mod_action_lock);

    for(LONG i=0;i<count;++i) {
        if(!local[i].callback) continue;
        AnymakerModActionEventV1 ev{};
        ev.struct_size=sizeof(ev);
        ev.event_version=ANYMAKER_MOD_ACTION_EVENT_VERSION;
        ev.sequence=(uint64_t)InterlockedIncrement64(&g_next_sequence);
        ev.phase=local[i].was_down?ANY_MOD_ACTION_BEGIN:ANY_MOD_ACTION_END;
        ev.virtual_key=local[i].virtual_key;
        copy_cstr_trunc(ev.action_id,sizeof(ev.action_id),local[i].action_id);
        copy_cstr_trunc(ev.display_name,sizeof(ev.display_name),local[i].display_name);
        __try {
            local[i].callback(&ev,local[i].user_data);
        } __except(EXCEPTION_EXECUTE_HANDLER) {
            log_line(ANY_LOG_ERROR,local[i].mod_id,
                     "Exception escaped registered mod-action callback.");
        }
        if(ev.phase==ANY_MOD_ACTION_BEGIN) InterlockedIncrement64(&g_mod_action_begin_events);
        else InterlockedIncrement64(&g_mod_action_end_events);
    }
}

static bool world_to_map_uv(double world_x,double world_z,double* out_u,double* out_v) {
    if(!out_u || !out_v || !std::isfinite(world_x) || !std::isfinite(world_z) ||
       !std::isfinite(g_map_half_width) || g_map_half_width<=0.0) return false;

    const double diameter=g_map_half_width*2.0;
    const double u=0.5 + (world_x-g_map_center_x)/diameter;
    const double v_world=0.5 + (world_z-g_map_center_z)/diameter;

    // Decoded texture space is top-left origin, so invert world Z for atlas V.
    *out_u=u;
    *out_v=1.0-v_world;
    return std::isfinite(*out_u) && std::isfinite(*out_v);
}

static bool map_textures_ready(uintptr_t* bg_out=nullptr,
                               uintptr_t* trees_out=nullptr,
                               uintptr_t* lines_out=nullptr) {
    if(!g_renderer_object) return false;
    uintptr_t bg=0,trees=0,lines=0;
    bool ok=safe_read_memory(g_renderer_object+0x4E0,&bg,sizeof(bg)) &&
            safe_read_memory(g_renderer_object+0x4E8,&trees,sizeof(trees)) &&
            safe_read_memory(g_renderer_object+0x4F0,&lines,sizeof(lines)) &&
            bg && trees && lines;
    if(ok) {
        if(bg_out) *bg_out=bg;
        if(trees_out) *trees_out=trees;
        if(lines_out) *lines_out=lines;
    }
    return ok;
}

static bool read_indirect_function_cell(
    uintptr_t wrapper_address,
    uintptr_t embedded_cell_offset,
    uintptr_t& out_cell,
    uintptr_t& out_function
) {
    out_cell=0;
    out_function=0;
    uintptr_t cell=0;
    if(!wrapper_address ||
       !safe_read_memory(wrapper_address+embedded_cell_offset,&cell,sizeof(cell)) ||
       !cell) return false;

    uintptr_t fn=0;
    if(!safe_read_memory(cell,&fn,sizeof(fn)) || !fn) return false;

    MEMORY_BASIC_INFORMATION mbi{};
    if(!VirtualQuery((LPCVOID)fn,&mbi,sizeof(mbi)) ||
       mbi.State!=MEM_COMMIT ||
       !executable_protect(mbi.Protect)) return false;

    out_cell=cell;
    out_function=fn;
    return true;
}

// The 56-byte render_rectangle_absolute wrapper is byte-identical to other
// absolute wrappers in this build. Resolve it by semantics: every candidate's
// +0x38 cell is dereferenced and only the wrapper whose native target equals
// client_ui_renderer.rectangle is accepted.
static bool resolve_wrapper_by_native_target(
    TargetResolution& t,
    uintptr_t embedded_cell_offset,
    uintptr_t expected_native
) {
    auto hits=scan_exact(t.body,t.body_size);
    t.hits=hits.size();

    std::vector<uintptr_t> candidates;
    for(uintptr_t h:hits) {
        uintptr_t cell=0,fn=0;
        if(read_indirect_function_cell(h,embedded_cell_offset,cell,fn) &&
           fn==expected_native) {
            candidates.push_back(h);
        }
    }

    t.address=(candidates.size()==1)?candidates[0]:0;
    std::string m="PHASE27_SEMANTIC_RESOLVE target="+std::string(t.label)+
        " raw_hits="+std::to_string(hits.size())+
        " native_target_matches="+std::to_string(candidates.size())+
        " expected_native="+hexptr(expected_native);
    if(t.address) m += " address="+hexptr(t.address);
    log_line(t.address?ANY_LOG_INFO:ANY_LOG_ERROR,"framework",m.c_str());
    return t.address!=0;
}

static bool read_ui_renderer_geometry(
    void* client_ui,
    uintptr_t& renderer_out,
    Vec2F64& origin_out,
    Vec2F64& viewport_out
) {
    renderer_out=0;
    origin_out={0.0,0.0};
    viewport_out={0.0,0.0};
    if(!client_ui) return false;

    uintptr_t ui=(uintptr_t)client_ui;
    uintptr_t renderer=0;
    Vec2F64 origin{};
    Vec2F64 viewport{};
    if(!safe_read_memory(ui+0x40,&renderer,sizeof(renderer)) || !renderer)
        return false;
    if(!safe_read_memory(ui+0xA8,&origin,sizeof(origin)))
        return false;
    if(!safe_read_memory(ui+0x90,&viewport,sizeof(viewport)))
        return false;
    if(!std::isfinite(origin.x) || !std::isfinite(origin.y) ||
       !std::isfinite(viewport.x) || !std::isfinite(viewport.y) ||
       viewport.x<=0.0 || viewport.y<=0.0 ||
       viewport.x>100.0 || viewport.y>100.0)
        return false;

    renderer_out=renderer;
    origin_out=origin;
    viewport_out=viewport;
    return true;
}

static double current_ui_cell_size() {
    if(g_ui_cell_size_global) {
        double v=0.0;
        if(safe_read_memory(g_ui_cell_size_global,&v,sizeof(v)) &&
           std::isfinite(v) && v>0.0 && v<10000.0) {
            g_ui_cell_size=v;
        }
    }
    return g_ui_cell_size;
}

struct LegacyRenderProbeInfoV2 {
    uint32_t struct_size; uint32_t info_version; uint32_t mode; uint32_t ui_hook_ready;
    double ui_cell_size; double last_ui_origin[2]; double viewport_size[2];
    double map_rect_position[2]; double map_rect_size[2]; double marker_position[2];
    uint64_t hook_frames; uint64_t cell_rectangle_draws; uint64_t absolute_rectangle_draws;
    uint64_t native_rectangle_draws; uint64_t viewport_probe_frames; uint64_t single_texture_frames;
    uint64_t native_map_draws; uint64_t marker_draws; uint64_t marker_cell_fallback_draws;
    uint64_t explicit_flushes; uint64_t render_exceptions;
    uintptr_t client_ui_renderer; uintptr_t native_renderer_begin; uintptr_t native_renderer_rectangle;
    uintptr_t native_renderer_texture_asset; uintptr_t native_renderer_render; uintptr_t absolute_rectangle_wrapper;
};
struct LegacyWorldMapInfoV1 {
    uint32_t struct_size; uint32_t info_version; double center_x; double center_z; double half_width;
    uint32_t texture_size_pixels; uint32_t textures_ready; uint32_t ui_hook_ready; uint32_t visible;
};

static bool get_render_probe_info(LegacyRenderProbeInfoV2* out) {
    if(!out) return false;
    out->struct_size=sizeof(*out);
    out->info_version=2u;
    out->mode=(uint32_t)InterlockedCompareExchange(&g_render_probe_mode,0,0);
    out->ui_hook_ready=InterlockedCompareExchange(&g_map_ui_hook_ready,0,0)?1u:0u;
    out->ui_cell_size=current_ui_cell_size();
    out->last_ui_origin[0]=g_last_ui_origin.x;
    out->last_ui_origin[1]=g_last_ui_origin.y;
    out->viewport_size[0]=g_last_ui_viewport.x;
    out->viewport_size[1]=g_last_ui_viewport.y;
    out->map_rect_position[0]=g_last_map_position.x;
    out->map_rect_position[1]=g_last_map_position.y;
    out->map_rect_size[0]=g_last_map_size.x;
    out->map_rect_size[1]=g_last_map_size.y;
    out->marker_position[0]=g_last_marker_position.x;
    out->marker_position[1]=g_last_marker_position.y;
    out->hook_frames=(uint64_t)InterlockedCompareExchange64(&g_render_probe_hook_frames,0,0);
    out->cell_rectangle_draws=(uint64_t)InterlockedCompareExchange64(&g_render_probe_cell_rect,0,0);
    out->absolute_rectangle_draws=(uint64_t)InterlockedCompareExchange64(&g_render_probe_abs_rect,0,0);
    out->native_rectangle_draws=(uint64_t)InterlockedCompareExchange64(&g_render_probe_native_rect,0,0);
    out->viewport_probe_frames=(uint64_t)InterlockedCompareExchange64(&g_render_probe_viewport_frames,0,0);
    out->single_texture_frames=(uint64_t)InterlockedCompareExchange64(&g_render_probe_single_texture,0,0);
    out->native_map_draws=(uint64_t)InterlockedCompareExchange64(&g_render_probe_native_map,0,0);
    out->marker_draws=(uint64_t)InterlockedCompareExchange64(&g_render_probe_marker,0,0);
    out->marker_cell_fallback_draws=(uint64_t)InterlockedCompareExchange64(&g_render_probe_marker_cell_fallback,0,0);
    out->explicit_flushes=(uint64_t)InterlockedCompareExchange64(&g_render_probe_flushes,0,0);
    out->render_exceptions=(uint64_t)InterlockedCompareExchange64(&g_render_probe_exceptions,0,0);
    out->client_ui_renderer=g_last_client_ui_renderer;
    out->native_renderer_begin=(uintptr_t)g_native_ui_begin;
    out->native_renderer_rectangle=(uintptr_t)g_native_ui_rectangle;
    out->native_renderer_texture_asset=(uintptr_t)g_native_ui_texture_asset;
    out->native_renderer_render=(uintptr_t)g_native_ui_render;
    out->absolute_rectangle_wrapper=g_absolute_rectangle_wrapper;
    return true;
}

static bool set_render_probe_mode(uint32_t mode) {
    if(mode>6u) return false;
    InterlockedExchange(&g_render_probe_mode,(LONG)mode);
    char b[192]{};
    std::snprintf(
        b,sizeof(b),
        "PHASE27_RENDER_PROBE_MODE mode=%u ui_ready=%d",
        (unsigned)mode,
        (int)InterlockedCompareExchange(&g_map_ui_hook_ready,0,0)
    );
    log_line(ANY_LOG_INFO,"framework",b);
    return InterlockedCompareExchange(&g_map_ui_hook_ready,0,0)!=0;
}

static bool get_world_map_info(LegacyWorldMapInfoV1* out) {
    if(!out) return false;
    out->struct_size=sizeof(*out);
    out->info_version=1u;
    out->center_x=g_map_center_x;
    out->center_z=g_map_center_z;
    out->half_width=g_map_half_width;
    out->texture_size_pixels=4096u;
    out->textures_ready=map_textures_ready()?1u:0u;
    out->ui_hook_ready=InterlockedCompareExchange(&g_map_ui_hook_ready,0,0)?1u:0u;
    out->visible=InterlockedCompareExchange(&g_world_map_visible,0,0)?1u:0u;
    return true;
}

static bool set_world_map_visible(bool visible) {
    InterlockedExchange(&g_world_map_visible,visible?1:0);
    log_line(ANY_LOG_INFO,"framework",
             visible?"PHASE27_MAP_VISIBILITY visible=1":"PHASE27_MAP_VISIBILITY visible=0");
    return InterlockedCompareExchange(&g_map_ui_hook_ready,0,0)!=0;
}

static bool is_world_map_visible() {
    return InterlockedCompareExchange(&g_world_map_visible,0,0)!=0;
}

static bool read_jit_global_slot(uintptr_t function_address,uintptr_t slot_offset,
                                 uintptr_t& global_address) {
    global_address=0;
    if(!function_address) return false;
    uintptr_t p=0;
    if(!safe_read_memory(function_address+slot_offset,&p,sizeof(p)) || !p) return false;
    MEMORY_BASIC_INFORMATION mbi{};
    if(!VirtualQuery((LPCVOID)p,&mbi,sizeof(mbi)) || mbi.State!=MEM_COMMIT) return false;
    global_address=p;
    return true;
}

static bool ensure_map_textures(uintptr_t& bg,uintptr_t& trees,uintptr_t& lines) {
    bg=trees=lines=0;
    if(map_textures_ready(&bg,&trees,&lines)) return true;
    if(!g_renderer_create_map_textures || !g_renderer_object) return false;
    g_renderer_create_map_textures((void*)g_renderer_object);
    InterlockedIncrement64(&g_map_texture_create_calls);
    return map_textures_ready(&bg,&trees,&lines);
}


static bool resolve_ui_texture(const char* asset_path_utf8,AnymakerUiTextureHandle* out_texture) {
    if(out_texture) *out_texture=0;
    return false; // Phase27 renderer quarantine.

    if(out_texture) *out_texture=0;
    if(!asset_path_utf8 || !out_texture) {
        InterlockedIncrement64(&g_ui_api_texture_resolve_fail);
        return false;
    }

    uintptr_t bg=0,trees=0,lines=0;
    if(!ensure_map_textures(bg,trees,lines)) {
        InterlockedIncrement64(&g_ui_api_texture_resolve_fail);
        return false;
    }

    uintptr_t chosen=0;
    if(std::strcmp(asset_path_utf8,ANYMAKER_TEXTURE_PATH_MAP_BACKGROUND)==0) chosen=bg;
    else if(std::strcmp(asset_path_utf8,ANYMAKER_TEXTURE_PATH_MAP_TREES)==0) chosen=trees;
    else if(std::strcmp(asset_path_utf8,ANYMAKER_TEXTURE_PATH_MAP_LINES)==0) chosen=lines;

    if(!chosen) {
        InterlockedIncrement64(&g_ui_api_texture_resolve_fail);
        return false;
    }
    *out_texture=(AnymakerUiTextureHandle)chosen;
    InterlockedIncrement64(&g_ui_api_texture_resolve_ok);
    return true;
}

static bool ui_draw_texture(
    const AnymakerUiRenderFrameV1* frame,
    AnymakerUiTextureHandle texture,
    double x,double y,double width,double height,
    uint32_t rgba8
) {
    return false; // Phase27 renderer quarantine.

    if(!ui_frame_is_current(frame) || !g_native_ui_texture_asset || !texture ||
       !std::isfinite(x) || !std::isfinite(y) ||
       !std::isfinite(width) || !std::isfinite(height) ||
       width<=0.0 || height<=0.0) {
        InterlockedIncrement64(&g_ui_api_rejected_draws);
        return false;
    }

    Vec2F64 p{
        -g_ui_api_viewport_w_tls*0.5 + x,
         g_ui_api_viewport_h_tls*0.5 - y - height
    };
    Vec2F64 size{width,height};
    Color8 color=unpack_rgba8(rgba8);
    g_native_ui_texture_asset((void*)g_ui_api_renderer_tls,&p,&size,(void*)texture,&color);
    InterlockedIncrement64(&g_ui_api_texture_draws);
    return true;
}

static bool get_ui_api_info(AnymakerUiApiInfoV1* out) {
    if(!out) return false;
    out->struct_size=sizeof(*out);
    out->info_version=ANYMAKER_UI_API_INFO_VERSION;
    out->ui_hook_ready=InterlockedCompareExchange(&g_map_ui_hook_ready,0,0)?1u:0u;
    out->rectangle_ready=0; // Quarantined.
    out->texture_ready=0; // Quarantined.
    out->registered_callbacks=(uint32_t)InterlockedCompareExchange(&g_ui_render_callback_count,0,0);
    out->last_viewport_width=g_last_ui_viewport.x;
    out->last_viewport_height=g_last_ui_viewport.y;
    out->ui_cell_size=current_ui_cell_size();
    out->render_frames=(uint64_t)InterlockedCompareExchange64(&g_ui_api_frames,0,0);
    out->callback_invocations=(uint64_t)InterlockedCompareExchange64(&g_ui_api_callback_invocations,0,0);
    out->rectangle_draws=(uint64_t)InterlockedCompareExchange64(&g_ui_api_rect_draws,0,0);
    out->texture_draws=(uint64_t)InterlockedCompareExchange64(&g_ui_api_texture_draws,0,0);
    out->texture_resolve_ok=(uint64_t)InterlockedCompareExchange64(&g_ui_api_texture_resolve_ok,0,0);
    out->texture_resolve_fail=(uint64_t)InterlockedCompareExchange64(&g_ui_api_texture_resolve_fail,0,0);
    out->rejected_draws=(uint64_t)InterlockedCompareExchange64(&g_ui_api_rejected_draws,0,0);
    out->callback_exceptions=(uint64_t)InterlockedCompareExchange64(&g_ui_api_callback_exceptions,0,0);
    out->key_queries=(uint64_t)InterlockedCompareExchange64(&g_ui_api_key_queries,0,0);
    return true;
}

static void dispatch_ui_render_callbacks(const AnymakerUiRenderFrameV1& frame,uintptr_t ui_renderer) {
    UiRenderRegistration regs[MAX_UI_RENDER_CALLBACKS]{};
    LONG count=0;
    AcquireSRWLockShared(&g_ui_render_callback_lock);
    count=std::min<LONG>(g_ui_render_callback_count,MAX_UI_RENDER_CALLBACKS);
    for(LONG i=0;i<count;++i) regs[i]=g_ui_render_callbacks[i];
    ReleaseSRWLockShared(&g_ui_render_callback_lock);

    if(count<=0) return;

    g_ui_api_callback_active=true;
    g_ui_api_renderer_tls=ui_renderer;
    g_ui_api_frame_id_tls=frame.frame_id;
    g_ui_api_viewport_w_tls=frame.viewport_width;
    g_ui_api_viewport_h_tls=frame.viewport_height;

    for(LONG i=0;i<count;++i) {
        if(!regs[i].callback) continue;
        __try {
            regs[i].callback(&frame,regs[i].user_data);
            InterlockedIncrement64(&g_ui_api_callback_invocations);
        } __except(EXCEPTION_EXECUTE_HANDLER) {
            InterlockedIncrement64(&g_ui_api_callback_exceptions);
            char b[256]{};
            std::snprintf(b,sizeof(b),"PHASE27_UI_CALLBACK_EXCEPTION mod=%s",
                          (regs[i].mod_id && *regs[i].mod_id)?regs[i].mod_id:"<unknown>");
            log_line(ANY_LOG_ERROR,"framework",b);
        }
    }

    g_ui_api_callback_active=false;
    g_ui_api_renderer_tls=0;
    g_ui_api_frame_id_tls=0;
    g_ui_api_viewport_w_tls=0.0;
    g_ui_api_viewport_h_tls=0.0;
}

static bool compute_centered_map_geometry(const Vec2F64& viewport,
                                          Vec2F64& pos,Vec2F64& size) {
    if(!std::isfinite(viewport.x) || !std::isfinite(viewport.y) ||
       viewport.x<=0.0 || viewport.y<=0.0) return false;
    const double side=std::min(viewport.x,viewport.y)*0.92;
    if(!(side>0.0) || !std::isfinite(side)) return false;
    size={side,side};
    pos={-side*0.5,-side*0.5};
    g_last_map_position=pos;
    g_last_map_size=size;
    return true;
}

static bool latest_player_map_marker(const Vec2F64& map_pos,const Vec2F64& map_size,
                                     Vec2F64& marker_pos,Vec2F64& marker_size) {
    AnymakerLocalPlayerStateV2 st{};
    AcquireSRWLockShared(&g_latest_player_lock);
    bool have=g_have_latest_player_state;
    if(have) st=g_latest_player_state;
    ReleaseSRWLockShared(&g_latest_player_lock);
    if(!have || !(st.valid_fields&ANY_PLAYER_VALID_POSITION)) return false;

    double u=0.0,v_top=0.0;
    if(!world_to_map_uv(st.position[0],st.position[2],&u,&v_top)) return false;
    if(u<0.0 || u>1.0 || v_top<0.0 || v_top>1.0) return false;

    const double side=map_size.x;
    const double marker=std::max(side*0.018,current_ui_cell_size()*0.45);
    const double cx=map_pos.x + u*map_size.x;
    // Native UI coordinates are +Y upward, while atlas V=0 is the top.
    const double cy=map_pos.y + (1.0-v_top)*map_size.y;
    marker_size={marker,marker};
    marker_pos={cx-marker*0.5,cy-marker*0.5};
    g_last_marker_position={cx,cy};
    return true;
}

static void draw_native_rect(uintptr_t ui_renderer,const Vec2F64& pos,
                             const Vec2F64& size,const Color8& color) {
    if(!g_native_ui_rectangle || !ui_renderer) return;
    g_native_ui_rectangle((void*)ui_renderer,&pos,&size,&color);
    InterlockedIncrement64(&g_render_probe_native_rect);
}

static bool draw_map_layers(uintptr_t ui_renderer,const Vec2F64& pos,
                            const Vec2F64& size,bool all_layers) {
    if(!g_native_ui_texture_asset || !ui_renderer) return false;
    uintptr_t bg=0,trees=0,lines=0;
    if(!ensure_map_textures(bg,trees,lines)) return false;
    Color8 white{255,255,255,255};
    g_native_ui_texture_asset((void*)ui_renderer,&pos,&size,(void*)bg,&white);
    if(all_layers) {
        g_native_ui_texture_asset((void*)ui_renderer,&pos,&size,(void*)trees,&white);
        g_native_ui_texture_asset((void*)ui_renderer,&pos,&size,(void*)lines,&white);
    }
    InterlockedIncrement64(all_layers?&g_render_probe_native_map:&g_render_probe_single_texture);
    InterlockedIncrement64(&g_map_texture_ready_frames);
    return true;
}

static bool draw_player_marker(void* client_ui,uintptr_t ui_renderer,const Vec2F64& origin,
                               const Vec2F64& map_pos,const Vec2F64& map_size,double cell) {
    Vec2F64 marker_pos{},marker_size{};
    if(!latest_player_map_marker(map_pos,map_size,marker_pos,marker_size)) return false;

    if(g_native_ui_rectangle) {
        Color8 outline{0,0,0,255};
        Color8 green{32,255,64,255};
        Vec2F64 outline_pos{marker_pos.x-marker_size.x*0.35,marker_pos.y-marker_size.y*0.35};
        Vec2F64 outline_size{marker_size.x*1.70,marker_size.y*1.70};
        draw_native_rect(ui_renderer,outline_pos,outline_size,outline);
        draw_native_rect(ui_renderer,marker_pos,marker_size,green);
        InterlockedIncrement64(&g_render_probe_marker);
        InterlockedIncrement64(&g_map_marker_frames);
        return true;
    }

    // Fallback only for diagnostics. The regular rectangle wrapper is already
    // proven visible and uses integer UI cells.
    if(g_ui_render_rectangle && client_ui && cell>0.0) {
        Vec2S32 p{
            (int32_t)std::llround((marker_pos.x-origin.x)/cell),
            (int32_t)std::llround((marker_pos.y-origin.y)/cell)
        };
        Vec2S32 s{1,1};
        Color8 green{32,255,64,255};
        g_ui_render_rectangle(client_ui,&p,&s,&green);
        InterlockedIncrement64(&g_render_probe_cell_rect);
        InterlockedIncrement64(&g_render_probe_marker_cell_fallback);
        InterlockedIncrement64(&g_map_marker_frames);
        return true;
    }
    return false;
}

static void draw_viewport_probe(uintptr_t ui_renderer,const Vec2F64& viewport) {
    if(!g_native_ui_rectangle) return;
    const double t=std::min(viewport.x,viewport.y)*0.025;
    const double left=-viewport.x*0.5;
    const double bottom=-viewport.y*0.5;
    Color8 magenta{255,32,224,220};
    Vec2F64 p{},s{};

    p={left,bottom}; s={t,viewport.y}; draw_native_rect(ui_renderer,p,s,magenta);
    p={left+viewport.x-t,bottom}; s={t,viewport.y}; draw_native_rect(ui_renderer,p,s,magenta);
    p={left,bottom}; s={viewport.x,t}; draw_native_rect(ui_renderer,p,s,magenta);
    p={left,bottom+viewport.y-t}; s={viewport.x,t}; draw_native_rect(ui_renderer,p,s,magenta);
    InterlockedIncrement64(&g_render_probe_viewport_frames);
}

// Generic panel visibility: observe the native inventory page builder, without
// placing AutoStack-specific state or drawing policy inside the framework.
using P26InventoryUiFn=void (*)(void*,void*,void*,void*,void*,void*,void*,void*,void*,void*);
static P26InventoryUiFn g_p26_inventory_ui_original=nullptr;
static volatile LONG64 g_p26_inventory_ui_generation=0;
static void p26_inventory_ui_hook(void* a,void* b,void* c,void* d,void* e,
                                  void* f,void* g,void* h,void* i,void* j) {
    InterlockedIncrement64(&g_p27_hook_calls[0]); p29_note_hook(0);
    p29_capture_inventory((uintptr_t)h,false);
    p33_capture_handheld_cache(false);
    g_p26_inventory_ui_original(a,b,c,d,e,f,g,h,i,j);
    InterlockedIncrement64(&g_p26_inventory_ui_generation);
}
static void p26_ensure_inventory_ui() {
    if(g_p26_inventory_ui_original) return;
    TargetResolution target{"p26_inventory_ui_page",BODY_P26_INVENTORY_UI,sizeof(BODY_P26_INVENTORY_UI),0,0};
    if(resolve_target(target)) {
        void* trampoline=nullptr;
        if(install_detour("p26_inventory_ui_page",target.address,BODY_P26_INVENTORY_UI,19,
                          (uintptr_t)&p26_inventory_ui_hook,(void**)&g_p26_inventory_ui_original)) {

            log_line(ANY_LOG_INFO,"framework","PHASE27_INVENTORY_UI_HOOK_READY visibility=page_builder_generation");
        }
    }
}

static void ui_render_hook(void* client_ui) {
    InterlockedIncrement64(&g_p27_hook_calls[21]); p29_note_hook(21);
    InterlockedIncrement64(&g_render_probe_hook_frames);
    uintptr_t renderer=0;Vec2F64 origin{},viewport{};
    if(read_ui_renderer_geometry(client_ui,renderer,origin,viewport)) {
        AcquireSRWLockExclusive(&g_p29_ui_snapshot_lock);
        if(g_p29_ui_snapshot.renderer && g_p29_ui_snapshot.renderer!=renderer) g_p29_ui_resource_changes.fetch_add(1);
        g_p29_ui_snapshot={renderer,{origin.x,origin.y},{viewport.x,viewport.y},g_p29_world_revision.load(),GetCurrentThreadId()};
        ReleaseSRWLockExclusive(&g_p29_ui_snapshot_lock);
        InterlockedIncrement64(&g_ui_api_frames);
    }
    g_ui_render_original(client_ui);
}

static const char* slot_name(EventSlot s) {
    switch(s) {
        case SLOT_CLIENT_CREATED: return "client_created";
        case SLOT_CLIENT_REMOVED: return "client_removed";
        case SLOT_SERVER_CREATED: return "server_created";
        case SLOT_SERVER_DESTROYED: return "server_destroyed";
        default: return "unknown";
    }
}

static const char* server_use_slot_name(ServerUseSlot s) {
    switch(s) {
        case USE_SELF_SLOT: return "self";
        case USE_ACTOR_SLOT: return "actor";
        case USE_VEHICLE_SLOT: return "vehicle";
        case USE_TRANSFORM_SLOT: return "transform";
        default: return "unknown";
    }
}

#include "legacy_inventory_observer.inc"
#include "legacy_actor_teardown.inc"
#include "legacy_routes.inc"
#include "legacy_runtime.inc"
#include "legacy_runtime_audit.inc"
#include "legacy_event_audit.inc"
#include "runtime_extension.inc"
#include "runtime_reporting.inc"
#include "legacy_report.inc"
#include "legacy_api_audit.inc"
#include "runtime_diagnostics.inc"

static DWORD WINAPI framework_runtime_thread(void*) {
    p27_register_event_audit();
    p29_api_smoke();
    Sleep(5000);

    log_line(ANY_LOG_INFO,"framework",
             "PHASE33_START Runtime API v16 broad thread/lifetime/native-contract pass. External mods, drawing, Controls insertion and inventory mutation remain disabled.");

    TargetResolution tc{"client_create",BODY_CLIENT_CREATE,sizeof(BODY_CLIENT_CREATE),0,0};
    TargetResolution tr{"client_remove",BODY_CLIENT_REMOVE,sizeof(BODY_CLIENT_REMOVE),0,0};
    TargetResolution ts{"server_create",BODY_SERVER_CREATE,sizeof(BODY_SERVER_CREATE),0,0};
    TargetResolution td{"server_destroy",BODY_SERVER_DESTROY,sizeof(BODY_SERVER_DESTROY),0,0};
    TargetResolution ib{"input_begin",BODY_INPUT_BEGIN,sizeof(BODY_INPUT_BEGIN),0,0};
    TargetResolution ie{"input_end",BODY_INPUT_END,sizeof(BODY_INPUT_END),0,0};
    TargetResolution us{"server_use_self",BODY_USE_SELF,sizeof(BODY_USE_SELF),0,0};
    TargetResolution ua{"server_use_actor",BODY_USE_ACTOR,sizeof(BODY_USE_ACTOR),0,0};
    TargetResolution uv{"server_use_vehicle",BODY_USE_VEHICLE,sizeof(BODY_USE_VEHICLE),0,0};
    TargetResolution ut{"server_use_transform",BODY_USE_TRANSFORM,sizeof(BODY_USE_TRANSFORM),0,0};

    TargetResolution pt{"player_tick_inventory",BODY_PLAYER_TICK,sizeof(BODY_PLAYER_TICK),0,0};
    TargetResolution gt{"player_get_transform",BODY_GET_TRANSFORM,sizeof(BODY_GET_TRANSFORM),0,0};
    TargetResolution ga{"player_get_item_active",BODY_GET_ITEM_ACTIVE,sizeof(BODY_GET_ITEM_ACTIVE),0,0};
    TargetResolution gh{"player_get_item_handheld",BODY_GET_ITEM_HANDHELD,sizeof(BODY_GET_ITEM_HANDHELD),0,0};
    TargetResolution gi{"player_get_incapacitated",BODY_GET_INCAPACITATED,sizeof(BODY_GET_INCAPACITATED),0,0};
    TargetResolution gs{"player_get_swimming",BODY_GET_SWIMMING,sizeof(BODY_GET_SWIMMING),0,0};

    TargetResolution gv{"player_get_linear_velocity",BODY_GET_LINEAR_VELOCITY,sizeof(BODY_GET_LINEAR_VELOCITY),0,0};
    TargetResolution go{"player_get_orientation",BODY_GET_ORIENTATION,sizeof(BODY_GET_ORIENTATION),0,0};
    TargetResolution ghu{"player_get_hunger",BODY_GET_HUNGER,sizeof(BODY_GET_HUNGER),0,0};
    TargetResolution gin{"player_get_infection",BODY_GET_INFECTION,sizeof(BODY_GET_INFECTION),0,0};
    TargetResolution gox{"player_get_oxygen",BODY_GET_OXYGEN,sizeof(BODY_GET_OXYGEN),0,0};
    TargetResolution gst{"player_get_stamina",BODY_GET_STAMINA,sizeof(BODY_GET_STAMINA),0,0};
    TargetResolution gtc{"player_get_temp_cold",BODY_GET_TEMP_COLD,sizeof(BODY_GET_TEMP_COLD),0,0};
    TargetResolution gth{"player_get_temp_hot",BODY_GET_TEMP_HOT,sizeof(BODY_GET_TEMP_HOT),0,0};
    TargetResolution gtr{"player_get_thirst",BODY_GET_THIRST,sizeof(BODY_GET_THIRST),0,0};
    TargetResolution gwe{"player_get_wetness",BODY_GET_WETNESS,sizeof(BODY_GET_WETNESS),0,0};

    TargetResolution sag{"server_actor_get_inventory",BODY_SERVER_ACTOR_GET_INVENTORY,sizeof(BODY_SERVER_ACTOR_GET_INVENTORY),0,0};
    TargetResolution sig{"server_inventory_get_item",BODY_SERVER_INVENTORY_GET_ITEM,sizeof(BODY_SERVER_INVENTORY_GET_ITEM),0,0};
    TargetResolution sabi{"server_actor_get_by_id",BODY_SERVER_ACTOR_BY_ID,sizeof(BODY_SERVER_ACTOR_BY_ID),0,0};

    // Keep the general inventory_item_util resolver as read-only discovery.
    TargetResolution sil{"server_item_lookup_discovery",BODY_SERVER_ITEM_LOOKUP,sizeof(BODY_SERVER_ITEM_LOOKUP),0,0};

    TargetResolution vcc{"vc_client_class",BODY_VC_CLIENT_CLASS,sizeof(BODY_VC_CLIENT_CLASS),0,0};
    TargetResolution vca{"vc_client_create",BODY_VC_CLIENT_CREATE,sizeof(BODY_VC_CLIENT_CREATE),0,0};
    TargetResolution vcd{"vc_client_destroy",BODY_VC_CLIENT_DESTROY,sizeof(BODY_VC_CLIENT_DESTROY),0,0};
    TargetResolution vcs{"vc_server_class",BODY_VC_SERVER_CLASS,sizeof(BODY_VC_SERVER_CLASS),0,0};
    TargetResolution vsa{"vc_server_create",BODY_VC_SERVER_CREATE,sizeof(BODY_VC_SERVER_CREATE),0,0};
    TargetResolution vsd{"vc_server_destroy",BODY_VC_SERVER_DESTROY,sizeof(BODY_VC_SERVER_DESTROY),0,0};

    TargetResolution map_ui_render{"map_ui_render",BODY_UI_RENDER,sizeof(BODY_UI_RENDER),0,0};
    TargetResolution map_ui_texture{"map_ui_render_texture",BODY_UI_RENDER_TEXTURE,sizeof(BODY_UI_RENDER_TEXTURE),0,0};
    TargetResolution map_ui_circle{"map_ui_render_circle",BODY_UI_RENDER_CIRCLE,sizeof(BODY_UI_RENDER_CIRCLE),0,0};
    TargetResolution ui_begin{"ui_begin",BODY_UI_BEGIN,sizeof(BODY_UI_BEGIN),0,0};
    TargetResolution ui_rectangle{"ui_render_rectangle",BODY_UI_RENDER_RECTANGLE,sizeof(BODY_UI_RENDER_RECTANGLE),0,0};
    TargetResolution ui_rectangle_absolute{"ui_render_rectangle_absolute",BODY_UI_RENDER_RECTANGLE_ABSOLUTE,sizeof(BODY_UI_RENDER_RECTANGLE_ABSOLUTE),0,0};
    TargetResolution ui_cell_size_ctor{"ui_cell_size_ctor",BODY_UI_CELL_SIZE_CTOR,sizeof(BODY_UI_CELL_SIZE_CTOR),0,0};
    TargetResolution render_record_commands{"renderer_record_render_commands",BODY_RENDER_RECORD_COMMANDS,sizeof(BODY_RENDER_RECORD_COMMANDS),0,0};
    TargetResolution renderer_render{"renderer_render",BODY_RENDERER_RENDER,sizeof(BODY_RENDERER_RENDER),0,0};
    TargetResolution frontend_render{"frontend_ui_render",BODY_FRONTEND_UI_RENDER,sizeof(BODY_FRONTEND_UI_RENDER),0,0};
    TargetResolution map_create_textures{"map_create_textures",BODY_RENDERER_CREATE_MAP_TEXTURES,sizeof(BODY_RENDERER_CREATE_MAP_TEXTURES),0,0};
    TargetResolution map_renderer_ctor{"map_renderer_global_ctor",BODY_G_RENDERER_CTOR,sizeof(BODY_G_RENDERER_CTOR),0,0};
    TargetResolution map_renderer_anchor{"map_renderer_anchor_replication_float",BODY_REPLICATION_FLOAT_THRESHOLD_CTOR,sizeof(BODY_REPLICATION_FLOAT_THRESHOLD_CTOR),0,0};
    TargetResolution map_position_ctor{"map_position_global_ctor",BODY_MAP_POSITION_CTOR,sizeof(BODY_MAP_POSITION_CTOR),0,0};
    TargetResolution map_half_ctor{"map_half_width_global_ctor",BODY_MAP_HALF_WIDTH_CTOR,sizeof(BODY_MAP_HALF_WIDTH_CTOR),0,0};

    TargetResolution key_bind{"mod_key_bind_key",BODY_BINDING_DIGITAL_BIND_KEY,sizeof(BODY_BINDING_DIGITAL_BIND_KEY),0,0};
    TargetResolution key_attrs{"mod_key_set_attributes",BODY_BINDING_DIGITAL_SET_ATTRIBUTES,sizeof(BODY_BINDING_DIGITAL_SET_ATTRIBUTES),0,0};
    TargetResolution key_create_all{"mod_key_create_input_bindings",BODY_CREATE_INPUT_BINDINGS,sizeof(BODY_CREATE_INPUT_BINDINGS),0,0};
    TargetResolution key_update_all{"mod_key_update_input_bindings",BODY_UPDATE_INPUT_BINDINGS,sizeof(BODY_UPDATE_INPUT_BINDINGS),0,0};

    TargetResolution actor_create_obj{"client_actor_container_create_object",BODY_ACTOR_CREATE_OBJECT,sizeof(BODY_ACTOR_CREATE_OBJECT),0,0};
    TargetResolution actor_destroy_obj{"client_actor_container_destroy_object",BODY_ACTOR_DESTROY_OBJECT,sizeof(BODY_ACTOR_DESTROY_OBJECT),0,0};
    TargetResolution controls_builder{"controls_builder_prefix",BODY_CONTROLS_BUILDER_PREFIX,sizeof(BODY_CONTROLS_BUILDER_PREFIX),0,0};

    bool okc=resolve_target(tc);
    bool okr=resolve_target(tr);
    bool oks=resolve_target(ts);
    bool okd=resolve_target(td);
    bool okib=resolve_target(ib);
    bool okie=resolve_target(ie);
    bool okus=resolve_target(us);
    bool okua=resolve_target(ua);
    bool okuv=resolve_target(uv);
    bool okut=resolve_target(ut);

    bool okpt=resolve_target(pt);
    bool okgt=resolve_target(gt);
    bool okga=resolve_target(ga);
    bool okgh=resolve_target(gh);
    bool okgi=resolve_target(gi);
    bool okgs=resolve_target(gs);

    // linear_velocity and orientation have byte-identical sibling functions.
    // Resolve unique surrounding player getters first, then constrain them to
    // the actor-character getter cluster between handheld and oxygen.
    bool okgv=false;
    bool okgo=false;
    bool okghu=resolve_target(ghu);
    bool okgin=resolve_target(gin);
    bool okgox=resolve_target(gox);
    bool okgst=resolve_target(gst);
    bool okgtc=resolve_target(gtc);
    bool okgth=resolve_target(gth);
    bool okgtr=resolve_target(gtr);
    bool okgwe=resolve_target(gwe);

    bool oksag=resolve_target(sag);
    bool oksig=resolve_target(sig);

    // Context resolution now has enough anchors.
    if(okgh && okgox) {
        okgv=resolve_target_between(gv,gh.address,gox.address);
        okgo=resolve_target_between(go,gh.address,gox.address);
    } else {
        log_line(ANY_LOG_ERROR,"framework",
                 "PHASE27_CONTEXT_RESOLVE player cluster anchors unavailable.");
    }

    bool oksabi=false;
    if(oksag) {
        // actor.container.get_actor_by_id is byte-identical to other typed
        // container getters. The actor instantiation is the nearest preceding
        // match in the server actor code cluster.
        oksabi=resolve_target_nearest_before(
            sabi,
            sag.address,
            0x400000
        );
    }

    bool oksil=resolve_target(sil);

    bool okvcc=resolve_target(vcc);
    bool okvca=resolve_target(vca);
    bool okvcd=resolve_target(vcd);
    bool okvcs=resolve_target(vcs);
    bool okvsa=resolve_target(vsa);
    bool okvsd=resolve_target(vsd);

    bool okmap_ui_render=resolve_target(map_ui_render);
    bool okmap_ui_texture=resolve_target(map_ui_texture);
    bool okmap_ui_circle=resolve_target(map_ui_circle);
    bool okui_begin=resolve_target(ui_begin);
    bool okui_rectangle=resolve_target(ui_rectangle);
    if(okui_begin) g_controls_ui_begin_address=ui_begin.address;
    if(okui_rectangle) g_controls_ui_rectangle_address=ui_rectangle.address;
    // render_rectangle_absolute has three byte-identical JIT bodies in this build.
    // Defer it until the native rectangle function has been recovered from the
    // unique render_rectangle wrapper, then resolve by native-target identity.
    bool okui_rectangle_absolute=false;
    bool okui_cell_size_ctor=resolve_target(ui_cell_size_ctor);
    bool okrender_record_commands=resolve_target(render_record_commands);
    bool okrenderer_render=resolve_target(renderer_render);
    bool okfrontend_render=resolve_target(frontend_render);
    bool okmap_create_textures=resolve_target(map_create_textures);
    bool okmap_renderer_anchor=resolve_target(map_renderer_anchor);
    bool okmap_renderer_ctor=false;
    if(okmap_renderer_anchor) {
        // g_renderer.$ctor and .$dtor share a generic 32-byte body. In GCL
        // order the dtor is immediately before the unique replication-float
        // ctor anchor, and the renderer ctor is the SECOND generic match back.
        okmap_renderer_ctor=resolve_target_nth_before(
            map_renderer_ctor,map_renderer_anchor.address,0x20000,2
        );
    }
    bool okmap_position_ctor=resolve_target(map_position_ctor);
    bool okmap_half_ctor=resolve_target(map_half_ctor);

    bool okkey_bind=resolve_target(key_bind);
    bool okkey_attrs=resolve_target(key_attrs);
    bool okkey_create_all=resolve_target(key_create_all);
    bool okkey_update_all=resolve_target(key_update_all);
    int mod_key_helpers=(okkey_bind?1:0)+(okkey_attrs?1:0)+(okkey_create_all?1:0)+(okkey_update_all?1:0);

    bool okactor_create=resolve_target(actor_create_obj);
    bool okactor_destroy=resolve_target(actor_destroy_obj);
    bool okcontrols_builder=resolve_target(controls_builder);

    int lifecycle_hooks=0;
    int input_hooks=0;
    int server_use_hooks=0;
    int player_tick_hooks=0;
    int player_getters=0;
    int server_use_helpers=0;
    int vehicle_component_hooks=0;
    int map_helpers=0;
    int map_globals=0;
    int map_ui_hooks=0;
    int render_probe_wrappers=0;
    int render_pipeline_nodes=0;
    int native_ui_functions=0;
    int client_actor_hooks=0;
    int controls_builder_hooks=0;
    void* tramp=nullptr;

    if(okgt) { g_get_transform=(GetTransformFn)gt.address; ++player_getters; }
    if(okga) { g_get_item_active=(GetItemPtrFn)ga.address; ++player_getters; }
    if(okgh) { g_get_item_handheld=(GetItemPtrFn)gh.address; ++player_getters; }
    if(okgi) { g_get_incapacitated=(GetBoolFn)gi.address; ++player_getters; }
    if(okgs) { g_get_swimming=(GetBoolFn)gs.address; ++player_getters; }

    if(okgv) { g_get_linear_velocity=(GetVec3Fn)gv.address; ++player_getters; }
    if(okgo) { g_get_orientation=(GetVec2Fn)go.address; ++player_getters; }
    if(okghu) { g_get_hunger=(GetF64Fn)ghu.address; ++player_getters; }
    if(okgin) { g_get_infection=(GetF64Fn)gin.address; ++player_getters; }
    if(okgox) { g_get_oxygen=(GetF64Fn)gox.address; ++player_getters; }
    if(okgst) { g_get_stamina=(GetF64Fn)gst.address; ++player_getters; }
    if(okgtc) { g_get_temp_cold=(GetF64Fn)gtc.address; ++player_getters; }
    if(okgth) { g_get_temp_hot=(GetF64Fn)gth.address; ++player_getters; }
    if(okgtr) { g_get_thirst=(GetF64Fn)gtr.address; ++player_getters; }
    if(okgwe) { g_get_wetness=(GetF64Fn)gwe.address; ++player_getters; }

    if(oksag) {
        g_server_actor_get_inventory=(ServerActorGetInventoryFn)sag.address;
        ++server_use_helpers;
    }
    if(oksig) {
        g_server_inventory_get_item=(ServerInventoryGetItemFn)sig.address;
        ++server_use_helpers;
    }
    if(oksabi) {
        g_server_actor_get_by_id=(ServerActorGetByIdFn)sabi.address;
        ++server_use_helpers;
    }

    if(okmap_ui_texture) {
        g_ui_render_texture=(ClientUiRenderTextureFn)map_ui_texture.address;
        ++map_helpers;
    }
    if(okmap_ui_circle) {
        g_ui_render_circle=(ClientUiRenderCircleFn)map_ui_circle.address;
        ++map_helpers;
    }
    if(okui_rectangle) {
        g_ui_render_rectangle=(ClientUiRenderRectangleFn)ui_rectangle.address;
        ++render_probe_wrappers;
    }
    if(okui_cell_size_ctor) {
        uintptr_t p=0;
        double v=0.0;
        if(read_jit_global_slot(ui_cell_size_ctor.address,0x28,p) &&
           safe_read_memory(p,&v,sizeof(v)) &&
           std::isfinite(v) && v>0.0 && v<10000.0) {
            g_ui_cell_size_global=p;
            g_ui_cell_size=v;
            ++render_probe_wrappers;
            char b[256]{};
            std::snprintf(
                b,sizeof(b),
                "PHASE27_UI_GLOBAL cell_size_ptr=%s cell_size=%.12f",
                hexptr(p).c_str(),v
            );
            log_line(ANY_LOG_INFO,"framework",b);
        } else {
            log_line(ANY_LOG_WARN,"framework",
                     "PHASE27_UI_GLOBAL cell-size slot/value unavailable.");
        }
    }

    if(okrender_record_commands) ++render_pipeline_nodes;
    if(okrenderer_render) ++render_pipeline_nodes;
    if(okfrontend_render) ++render_pipeline_nodes;

    // Recover native UI renderer entry points from JIT wrapper indirection cells.
    // Phase 27 improves rectangle resolution: the unique cell-space rectangle
    // wrapper's trailing +0x248 cell is dependency #5, client_ui_renderer.rectangle.
    // We then use that native function identity to select the correct 56-byte
    // render_rectangle_absolute wrapper from its three byte-identical candidates.
    {
        uintptr_t cell=0,fn=0;
        if(okui_begin && read_indirect_function_cell(ui_begin.address,0x328,cell,fn)) {
            g_native_ui_begin=(NativeUiRendererBeginFn)fn;
            ++native_ui_functions;
            log_line(ANY_LOG_INFO,"framework",
                     ("PHASE27_NATIVE_UI name=begin cell="+hexptr(cell)+" function="+hexptr(fn)).c_str());
        }
        if(okui_rectangle && read_indirect_function_cell(ui_rectangle.address,0x248,cell,fn)) {
            g_native_ui_rectangle=(NativeUiRendererRectangleFn)fn;
            ++native_ui_functions;
            log_line(ANY_LOG_INFO,"framework",
                     ("PHASE27_NATIVE_UI name=rectangle source=render_rectangle+0x248 cell="+
                      hexptr(cell)+" function="+hexptr(fn)).c_str());

            okui_rectangle_absolute=resolve_wrapper_by_native_target(
                ui_rectangle_absolute,0x38,fn
            );
            if(okui_rectangle_absolute) {
                g_ui_render_rectangle_absolute=(ClientUiRenderRectangleAbsoluteFn)ui_rectangle_absolute.address;
                g_absolute_rectangle_wrapper=ui_rectangle_absolute.address;
                ++render_probe_wrappers;
            }
        } else {
            log_line(ANY_LOG_ERROR,"framework",
                     "PHASE27_NATIVE_UI name=rectangle source=render_rectangle+0x248 unavailable");
        }
        if(okmap_ui_texture &&
           read_indirect_function_cell(map_ui_texture.address,0x2B8,cell,fn)) {
            g_native_ui_texture_asset=(NativeUiRendererTextureAssetFn)fn;
            ++native_ui_functions;
            log_line(ANY_LOG_INFO,"framework",
                     ("PHASE27_NATIVE_UI name=texture_asset cell="+hexptr(cell)+" function="+hexptr(fn)).c_str());
        }
        if(okmap_ui_render &&
           read_indirect_function_cell(map_ui_render.address,0xB8,cell,fn)) {
            g_native_ui_render=(NativeUiRendererRenderFn)fn;
            ++native_ui_functions;
            log_line(ANY_LOG_INFO,"framework",
                     ("PHASE27_NATIVE_UI name=render cell="+hexptr(cell)+" function="+hexptr(fn)).c_str());
        }
    }

    if(okmap_create_textures) {
        g_renderer_create_map_textures=(RendererCreateMapTexturesFn)map_create_textures.address;
        ++map_helpers;
    }

    if(okmap_renderer_ctor) {
        uintptr_t p=0;
        if(read_jit_global_slot(map_renderer_ctor.address,0x20,p)) {
            g_renderer_object=p;
            ++map_globals;
            log_line(ANY_LOG_INFO,"framework",
                     ("PHASE27_MAP_GLOBAL renderer="+hexptr(p)+" source=jit_slot").c_str());
        } else {
            log_line(ANY_LOG_ERROR,"framework","PHASE27_MAP_GLOBAL renderer slot unreadable.");
        }
    }

    if(okmap_position_ctor) {
        uintptr_t p=0;
        double position[3]{};
        if(read_jit_global_slot(map_position_ctor.address,0x50,p) &&
           safe_read_memory(p,position,sizeof(position)) &&
           std::isfinite(position[0]) && std::isfinite(position[2])) {
            g_map_position_global=p;
            g_map_center_x=position[0];
            g_map_center_z=position[2];
            ++map_globals;
            char b[256]{};
            std::snprintf(b,sizeof(b),
                "PHASE27_MAP_GLOBAL position_ptr=%s center=(%.3f,%.3f) camera_y=%.3f expected_center=(81000,52000)",
                hexptr(p).c_str(),position[0],position[2],position[1]);
            log_line(ANY_LOG_INFO,"framework",b);
        } else {
            log_line(ANY_LOG_ERROR,"framework","PHASE27_MAP_GLOBAL position slot/value unreadable; using static calibration.");
        }
    }

    if(okmap_half_ctor) {
        uintptr_t p=0;
        double half=0.0;
        if(read_jit_global_slot(map_half_ctor.address,0x20,p) &&
           safe_read_memory(p,&half,sizeof(half)) &&
           std::isfinite(half) && half>0.0) {
            g_map_half_width_global=p;
            g_map_half_width=half;
            ++map_globals;
            char b[192]{};
            std::snprintf(b,sizeof(b),
                "PHASE27_MAP_GLOBAL half_width_ptr=%s half_width=%.3f expected=6508",
                hexptr(p).c_str(),half);
            log_line(ANY_LOG_INFO,"framework",b);
        } else {
            log_line(ANY_LOG_ERROR,"framework","PHASE27_MAP_GLOBAL half-width slot/value unreadable; using static calibration.");
        }
    }

    {
        const bool calibration_match=
            std::fabs(g_map_center_x-81000.0)<0.001 &&
            std::fabs(g_map_center_z-52000.0)<0.001 &&
            std::fabs(g_map_half_width-6508.0)<0.001;
        log_line(calibration_match?ANY_LOG_INFO:ANY_LOG_WARN,"framework",
                 calibration_match?
                 "PHASE27_MAP_CALIBRATION live_or_static_match=1 center_x=81000 center_z=52000 half_width=6508 atlas=4096":
                 "PHASE27_MAP_CALIBRATION live_or_static_match=0 values differ from extracted defaults.");
    }

    if(okc && install_detour("client_create",tc.address,BODY_CLIENT_CREATE,17,
                             (uintptr_t)&client_create_hook,(void**)&g_client_create_original)) {
         ++lifecycle_hooks;
    }

    tramp=nullptr;
    if(okr && install_detour("client_remove",tr.address,BODY_CLIENT_REMOVE,17,
                             (uintptr_t)&client_remove_hook,(void**)&g_client_remove_original)) {
         ++lifecycle_hooks;
    }

    tramp=nullptr;
    if(oks && install_detour("server_create",ts.address,BODY_SERVER_CREATE,17,
                             (uintptr_t)&server_create_hook,(void**)&g_server_create_original)) {
         ++lifecycle_hooks;
    }

    tramp=nullptr;
    if(okd && install_detour("server_destroy",td.address,BODY_SERVER_DESTROY,20,
                             (uintptr_t)&server_destroy_hook,(void**)&g_server_destroy_original)) {
         ++lifecycle_hooks;
    }

    tramp=nullptr;
    if(okib && install_detour("input_begin",ib.address,BODY_INPUT_BEGIN,20,
                              (uintptr_t)&input_begin_hook,(void**)&g_input_begin_original)) {
         ++input_hooks;
    }

    tramp=nullptr;
    if(okie && install_detour("input_end",ie.address,BODY_INPUT_END,17,
                              (uintptr_t)&input_end_hook,(void**)&g_input_end_original)) {
         ++input_hooks;
    }

    tramp=nullptr;
    if(okus && install_detour("server_use_self",us.address,BODY_USE_SELF,21,
                              (uintptr_t)&use_self_hook,(void**)&g_use_self_original)) {
         ++server_use_hooks;
    }

    tramp=nullptr;
    if(okua && install_detour("server_use_actor",ua.address,BODY_USE_ACTOR,21,
                              (uintptr_t)&use_actor_hook,(void**)&g_use_actor_original)) {
         ++server_use_hooks;
    }

    tramp=nullptr;
    if(okuv && install_detour("server_use_vehicle",uv.address,BODY_USE_VEHICLE,29,
                              (uintptr_t)&use_vehicle_hook,(void**)&g_use_vehicle_original)) {
         ++server_use_hooks;
    }

    tramp=nullptr;
    if(okut && install_detour("server_use_transform",ut.address,BODY_USE_TRANSFORM,21,
                              (uintptr_t)&use_transform_hook,(void**)&g_use_transform_original)) {
         ++server_use_hooks;
    }

    // 8 nonvolatile pushes (12 bytes total) + `sub rsp,0x10d8` (7 bytes) = 19.
    // No RIP-relative instruction occurs in this overwritten tick prologue.
    tramp=nullptr;
    if(okpt && install_detour("player_tick_inventory",pt.address,BODY_PLAYER_TICK,19,
                              (uintptr_t)&player_tick_hook,(void**)&g_player_tick_original)) {

        ++player_tick_hooks;
    }

    tramp=nullptr;
    if(okvcc && install_detour("vc_client_class",vcc.address,BODY_VC_CLIENT_CLASS,17,
                               (uintptr_t)&vc_client_class_hook,(void**)&g_vc_client_class_original)) {

        ++vehicle_component_hooks;
    }

    tramp=nullptr;
    if(okvca && install_detour("vc_client_create",vca.address,BODY_VC_CLIENT_CREATE,17,
                               (uintptr_t)&vc_client_create_hook,(void**)&g_vc_client_create_original)) {

        ++vehicle_component_hooks;
    }

    tramp=nullptr;
    if(okvcd && install_detour("vc_client_destroy",vcd.address,BODY_VC_CLIENT_DESTROY,15,
                               (uintptr_t)&vc_client_destroy_hook,(void**)&g_vc_client_destroy_original)) {

        ++vehicle_component_hooks;
    }

    tramp=nullptr;
    if(okvcs && install_detour("vc_server_class",vcs.address,BODY_VC_SERVER_CLASS,17,
                               (uintptr_t)&vc_server_class_hook,(void**)&g_vc_server_class_original)) {

        ++vehicle_component_hooks;
    }

    tramp=nullptr;
    if(okvsa && install_detour("vc_server_create",vsa.address,BODY_VC_SERVER_CREATE,16,
                               (uintptr_t)&vc_server_create_hook,(void**)&g_vc_server_create_original)) {

        ++vehicle_component_hooks;
    }

    tramp=nullptr;
    if(okvsd && install_detour("vc_server_destroy",vsd.address,BODY_VC_SERVER_DESTROY,17,
                               (uintptr_t)&vc_server_destroy_hook,(void**)&g_vc_server_destroy_original)) {

        ++vehicle_component_hooks;
    }

    tramp=nullptr;
    if(okactor_create &&
       install_detour("client_actor_container_create_object",actor_create_obj.address,BODY_ACTOR_CREATE_OBJECT,17,
                      (uintptr_t)&client_actor_create_hook,(void**)&g_client_actor_create_original)) {

        ++client_actor_hooks;
    }

    tramp=nullptr;
    if(okactor_destroy &&
       install_detour("client_actor_container_destroy_object",actor_destroy_obj.address,BODY_ACTOR_DESTROY_OBJECT,18,
                      (uintptr_t)&client_actor_destroy_hook,(void**)&g_client_actor_destroy_original)) {

        ++client_actor_hooks;
    }

    tramp=nullptr;
    if(okcontrols_builder &&
       install_detour("controls_builder_prefix",controls_builder.address,BODY_CONTROLS_BUILDER_PREFIX,19,
                      (uintptr_t)&controls_builder_hook,(void**)&g_controls_builder_original)) {
        g_p26_controls_body=controls_builder.address;

        InterlockedExchange(&g_controls_menu_hook_ready,1);
        ++controls_builder_hooks;
        ensure_mod_controls_native_section();
    }

    tramp=nullptr;
    if(okmap_ui_render &&
       install_detour("phase26_ui_render",map_ui_render.address,BODY_UI_RENDER,15,
                      (uintptr_t)&ui_render_hook,(void**)&g_ui_render_original)) {

        InterlockedExchange(&g_map_ui_hook_ready,1);
        ++map_ui_hooks;
    }

    log_line(ANY_LOG_INFO,"framework",
             ("PHASE27_MOD_KEYS_DISCOVERY helpers="+std::to_string(mod_key_helpers)+
              "/4 bind_key="+std::to_string(okkey_bind?1:0)+
              " set_attributes="+std::to_string(okkey_attrs?1:0)+
              " create_input_bindings="+std::to_string(okkey_create_all?1:0)+
              " update_input_bindings="+std::to_string(okkey_update_all?1:0)+
              " controls_builder_index=25696 controls_builder_hook="+
              std::to_string(controls_builder_hooks)+"/1 mode=observer_only").c_str());

    log_line(ANY_LOG_INFO,"framework",
             ("PHASE27_MAP_RESOLVE globals="+std::to_string(map_globals)+
              "/3 helpers="+std::to_string(map_helpers)+
              "/3 ui_hooks="+std::to_string(map_ui_hooks)+
              "/1 renderer="+hexptr(g_renderer_object)).c_str());

    log_line(ANY_LOG_INFO,"framework",
             ("PHASE27_RENDER_RESOLVE wrappers="+std::to_string(render_probe_wrappers)+
              "/3 native_ui="+std::to_string(native_ui_functions)+
              "/4 pipeline_nodes="+std::to_string(render_pipeline_nodes)+
              "/3 ui_hook="+std::to_string(map_ui_hooks)+
              "/1 absolute_semantic="+std::to_string(okui_rectangle_absolute?1:0)+
              "/1").c_str());

    log_line(ANY_LOG_INFO,"framework",
             ("PHASE27_RENDER_PIPELINE backend=d3d12_dxgi"
              " order=renderer.render>_record_render_commands>"
              "postprocess>map_render>frontend_ui.render>client_ui.render>"
              "client_ui_renderer.render>mm_ui.render>studio_logo"
              " record_commands="+
              (okrender_record_commands?hexptr(render_record_commands.address):std::string("<unresolved>"))+
              " renderer_render="+
              (okrenderer_render?hexptr(renderer_render.address):std::string("<unresolved>"))+
              " frontend_ui_render="+
              (okfrontend_render?hexptr(frontend_render.address):std::string("<unresolved>"))).c_str());

    log_line(ANY_LOG_INFO,"framework",
             ("PHASE27_DISCOVERY inventory_item_util_get_item="+
              std::to_string(oksil?1:0)+"/1"+
              (oksil?(" address="+hexptr(sil.address)):"")).c_str());

    log_line(ANY_LOG_INFO,"framework",
             ("PHASE27_READY lifecycle_hooks="+std::to_string(lifecycle_hooks)+
              "/4 input_hooks="+std::to_string(input_hooks)+
              "/2 server_use_hooks="+std::to_string(server_use_hooks)+
              "/4 player_tick_hooks="+std::to_string(player_tick_hooks)+
              "/1 player_getters="+std::to_string(player_getters)+
              "/15 server_use_helpers="+std::to_string(server_use_helpers)+
              "/3 vehicle_component_hooks="+std::to_string(vehicle_component_hooks)+
              "/6 map_globals="+std::to_string(map_globals)+
              "/3 map_helpers="+std::to_string(map_helpers)+
              "/3 map_ui_hooks="+std::to_string(map_ui_hooks)+
              "/1 render_probe_wrappers="+std::to_string(render_probe_wrappers)+
              "/3 native_ui="+std::to_string(native_ui_functions)+
              "/4 render_pipeline="+std::to_string(render_pipeline_nodes)+
              "/3 mod_key_helpers="+std::to_string(mod_key_helpers)+
              "/4 client_actor_hooks="+std::to_string(client_actor_hooks)+
              "/2 controls_builder_hooks="+std::to_string(controls_builder_hooks)+
              "/1 controls_section_ready="+std::to_string(controls_native_section_ready()?1:0)+
              " api=Runtime_API_v16").c_str());

    {
        char b[512]{};
        std::snprintf(
            b,sizeof(b),
            "PHASE27_PUBLIC_UI_READY ui_hook=%d rectangle=%d texture=%d texture_path_resolver=%d key_poll_compat=1 mod_actions=1 mod_controls_section=0 actor_api=1 ui_info=1 public_context_has_map_widget=0",
            map_ui_hooks==1?1:0,
            g_native_ui_rectangle?1:0,
            g_native_ui_texture_asset?1:0,
            (g_renderer_create_map_textures && g_renderer_object)?1:0
        );
        log_line(ANY_LOG_INFO,"framework",b);
    }

    p27_retry_helpers();
    p27_report_hooks();
    LONG64 last_total=-1;
    DWORD lastStats=GetTickCount();
    DWORD lastPlayerTriggerRetry=GetTickCount();
    DWORD lastActorHookRetry=GetTickCount();
    DWORD lastControlsBuilderRetry=GetTickCount();

    for(;;) {
        // The player tick family appears to be lazily materialized. If the first
        // main-menu scan missed it, retry only after input has identified the local
        // actor and the world is actually live.
        if(player_tick_hooks==0 &&
           InterlockedCompareExchange64(&g_local_actor_hint,0,0)!=0) {
            DWORD triggerNow=GetTickCount();
            if(triggerNow-lastPlayerTriggerRetry>=10000) {
                lastPlayerTriggerRetry=triggerNow;

                TargetResolution retry{
                    "player_tick_inventory",
                    BODY_PLAYER_TICK,
                    sizeof(BODY_PLAYER_TICK),
                    0,
                    0
                };

                if(resolve_target(retry)) {
                    void* retryTrampoline=nullptr;
                    if(install_detour(
                        "player_tick_inventory",
                        retry.address,
                        BODY_PLAYER_TICK,
                        19,
                        (uintptr_t)&player_tick_hook,
                        (void**)&g_player_tick_original
                    )) {

                        player_tick_hooks=1;

                        log_line(
                            ANY_LOG_INFO,
                            "framework",
                            "PHASE27_PLAYER_TRIGGER_READY source=client_scene.actor.tick_inventory"
                        );
                    }
                }
            }
        }

        if(client_actor_hooks<2) {
            DWORD retryNow=GetTickCount();
            if(retryNow-lastActorHookRetry>=10000) {
                lastActorHookRetry=retryNow;
                if(!g_client_actor_create_original) {
                    TargetResolution retry{"client_actor_container_create_object",BODY_ACTOR_CREATE_OBJECT,sizeof(BODY_ACTOR_CREATE_OBJECT),0,0};
                    if(resolve_target(retry)) {
                        void* t=nullptr;
                        if(install_detour("client_actor_container_create_object",retry.address,BODY_ACTOR_CREATE_OBJECT,17,
                                          (uintptr_t)&client_actor_create_hook,(void**)&g_client_actor_create_original)) {

                            ++client_actor_hooks;
                            log_line(ANY_LOG_INFO,"framework","PHASE27_ACTOR_HOOK_READY kind=create source=retry");
                        }
                    }
                }
                if(!g_client_actor_destroy_original) {
                    TargetResolution retry{"client_actor_container_destroy_object",BODY_ACTOR_DESTROY_OBJECT,sizeof(BODY_ACTOR_DESTROY_OBJECT),0,0};
                    if(resolve_target(retry)) {
                        void* t=nullptr;
                        if(install_detour("client_actor_container_destroy_object",retry.address,BODY_ACTOR_DESTROY_OBJECT,18,
                                          (uintptr_t)&client_actor_destroy_hook,(void**)&g_client_actor_destroy_original)) {

                            ++client_actor_hooks;
                            log_line(ANY_LOG_INFO,"framework","PHASE27_ACTOR_HOOK_READY kind=destroy source=retry");
                        }
                    }
                }
            }
        }

        if(controls_builder_hooks==0) {
            DWORD retryNow=GetTickCount();
            if(retryNow-lastControlsBuilderRetry>=10000) {
                lastControlsBuilderRetry=retryNow;
                TargetResolution retry{"controls_builder_prefix",BODY_CONTROLS_BUILDER_PREFIX,sizeof(BODY_CONTROLS_BUILDER_PREFIX),0,0};
                if(resolve_target(retry)) {
                    void* t=nullptr;
                    if(install_detour("controls_builder_prefix",retry.address,BODY_CONTROLS_BUILDER_PREFIX,19,
                                      (uintptr_t)&controls_builder_hook,(void**)&g_controls_builder_original)) {
                        g_p26_controls_body=retry.address;

                        InterlockedExchange(&g_controls_menu_hook_ready,1);
                        controls_builder_hooks=1;
                        log_line(ANY_LOG_INFO,"framework","PHASE27_CONTROLS_BRIDGE_READY source=retry mode=observer_only");
                        ensure_mod_controls_native_section();
                    }
                }
            }
        }

        LifecycleRecord lr{};
        int lifecycle_batch=0;
        while(lifecycle_batch<2048 && pop_event(lr)) {
            dispatch_event(lr);
            ++lifecycle_batch;
        }

        DigitalRecord dr{};
        int digital_batch=0;
        while(digital_batch<2048 && pop_digital(dr)) {
            dispatch_digital(dr);
            ++digital_batch;
        }

        ServerUseRecord ur{};
        int use_batch=0;
        while(use_batch<2048 && pop_server_use(ur)) {
            dispatch_server_use(ur);
            ++use_batch;
        }

        AnymakerLocalPlayerStateV2 ps{};
        int player_batch=0;
        while(player_batch<512 && pop_player_state(ps)) {
            dispatch_player_state(ps);
            ++player_batch;
        }

        VehicleComponentRecord vcr{};
        int component_batch=0;
        while(component_batch<2048 && pop_vehicle_component(vcr)) {
            dispatch_vehicle_component(vcr);
            ++component_batch;
        }

        AnymakerClientActorLifecycleEventV1 actor_event{};
        int actor_batch=0;
        while(actor_batch<1024 && pop_client_actor_event(actor_event)) {
            dispatch_client_actor_event(actor_event);
            ++actor_batch;
        }

        DWORD now=GetTickCount();
        if(now-lastStats>=60000) {
            LONG64 total=0;
            for(int i=0;i<SLOT_COUNT;++i) total+=read64(&g_captured[i]);
            for(int i=0;i<DIGITAL_SLOT_COUNT;++i) total+=read64(&g_digital_captured[i]);
            for(int i=0;i<SERVER_USE_SLOT_COUNT;++i) total+=read64(&g_server_use_captured[i]);
            total+=read64(&g_player_captured);
            for(int i=0;i<VC_SLOT_COUNT;++i)
                total+=read64(&g_component_captured[i]);
            total+=read64(&g_client_actor_created_captured);
            total+=read64(&g_client_actor_destroyed_captured);
            total+=read64(&g_mod_action_begin_events);
            total+=read64(&g_mod_action_end_events);

            if(total!=last_total) {
                LONG lifecycle_callbacks=0;
                AcquireSRWLockShared(&g_callback_lock);
                lifecycle_callbacks=g_callback_count;
                ReleaseSRWLockShared(&g_callback_lock);

                LONG digital_callbacks=0;
                AcquireSRWLockShared(&g_digital_callback_lock);
                digital_callbacks=g_digital_callback_count;
                ReleaseSRWLockShared(&g_digital_callback_lock);

                LONG use_callbacks=0;
                AcquireSRWLockShared(&g_server_use_callback_lock);
                use_callbacks=g_server_use_callback_count;
                ReleaseSRWLockShared(&g_server_use_callback_lock);

                LONG player_callbacks=0;
                AcquireSRWLockShared(&g_player_callback_lock);
                player_callbacks=g_player_callback_count;
                ReleaseSRWLockShared(&g_player_callback_lock);

                LONG component_callbacks=0;
                AcquireSRWLockShared(&g_vehicle_component_callback_lock);
                component_callbacks=g_vehicle_component_callback_count;
                ReleaseSRWLockShared(&g_vehicle_component_callback_lock);

                for(int i=0;i<SLOT_COUNT;++i) {
                    std::string m="PHASE27_LIFECYCLE_STATS type="+std::string(slot_name((EventSlot)i))+
                        " captured="+std::to_string((long long)read64(&g_captured[i]))+
                        " dispatched="+std::to_string((long long)read64(&g_dispatched[i]))+
                        " dropped="+std::to_string((long long)read64(&g_dropped[i]))+
                        " callbacks="+std::to_string(lifecycle_callbacks);
                    log_line(ANY_LOG_INFO,"framework",m.c_str());
                }

                for(int i=0;i<DIGITAL_SLOT_COUNT;++i) {
                    const char* n=(i==DIGITAL_BEGIN)?"begin":"end";
                    std::string m="PHASE27_INPUT_STATS phase="+std::string(n)+
                        " captured="+std::to_string((long long)read64(&g_digital_captured[i]))+
                        " dispatched="+std::to_string((long long)read64(&g_digital_dispatched[i]))+
                        " dropped="+std::to_string((long long)read64(&g_digital_dropped[i]))+
                        " callbacks="+std::to_string(digital_callbacks);
                    log_line(ANY_LOG_INFO,"framework",m.c_str());
                }

                for(int i=0;i<SERVER_USE_SLOT_COUNT;++i) {
                    std::string m="PHASE27_SERVER_USE_STATS kind="+
                        std::string(server_use_slot_name((ServerUseSlot)i))+
                        " captured="+std::to_string((long long)read64(&g_server_use_captured[i]))+
                        " dispatched="+std::to_string((long long)read64(&g_server_use_dispatched[i]))+
                        " dropped="+std::to_string((long long)read64(&g_server_use_dropped[i]))+
                        " callbacks="+std::to_string(use_callbacks);
                    log_line(ANY_LOG_INFO,"framework",m.c_str());
                }

                std::string player=
                    "PHASE27_PLAYER_STATS captured="+
                    std::to_string((long long)read64(&g_player_captured))+
                    " dispatched="+std::to_string((long long)read64(&g_player_dispatched))+
                    " dropped="+std::to_string((long long)read64(&g_player_dropped))+
                    " callbacks="+std::to_string(player_callbacks);
                log_line(ANY_LOG_INFO,"framework",player.c_str());

                std::string pv=
                    "PHASE27_PLAYER_VALIDATE position_ok="+
                    std::to_string((long long)read64(&g_player_position_read_ok))+
                    " orientation_ok="+
                    std::to_string((long long)read64(&g_player_orientation_read_ok))+
                    " transform_getter_ok="+
                    std::to_string((long long)read64(&g_player_transform_getter_ok))+
                    " transform_matches="+
                    std::to_string((long long)read64(&g_player_transform_matches))+
                    " orientation_getter_ok="+
                    std::to_string((long long)read64(&g_player_orientation_getter_ok))+
                    " orientation_matches="+
                    std::to_string((long long)read64(&g_player_orientation_matches))+
                    " velocity_getter_ok="+
                    std::to_string((long long)read64(&g_player_velocity_getter_ok))+
                    " incapacitated_getter_ok="+
                    std::to_string((long long)read64(&g_player_incapacitated_getter_ok))+
                    " swimming_getter_ok="+
                    std::to_string((long long)read64(&g_player_swimming_getter_ok))+
                    " active_nonzero="+
                    std::to_string((long long)read64(&g_player_active_nonzero))+
                    " active_definition_ok="+
                    std::to_string((long long)read64(&g_player_active_definition_ok))+
                    " handheld_nonzero="+
                    std::to_string((long long)read64(&g_player_handheld_nonzero))+
                    " handheld_definition_ok="+
                    std::to_string((long long)read64(&g_player_handheld_definition_ok));
                log_line(ANY_LOG_INFO,"framework",pv.c_str());

                std::string ext=
                    "PHASE27_PLAYER_EXT_VALIDATE stamina_ok="+
                    std::to_string((long long)read64(&g_player_stamina_getter_ok))+
                    " hunger_ok="+
                    std::to_string((long long)read64(&g_player_hunger_getter_ok))+
                    " thirst_ok="+
                    std::to_string((long long)read64(&g_player_thirst_getter_ok))+
                    " oxygen_ok="+
                    std::to_string((long long)read64(&g_player_oxygen_getter_ok))+
                    " wetness_ok="+
                    std::to_string((long long)read64(&g_player_wetness_getter_ok))+
                    " infection_ok="+
                    std::to_string((long long)read64(&g_player_infection_getter_ok))+
                    " temp_cold_ok="+
                    std::to_string((long long)read64(&g_player_temp_cold_getter_ok))+
                    " temp_hot_ok="+
                    std::to_string((long long)read64(&g_player_temp_hot_getter_ok));
                log_line(ANY_LOG_INFO,"framework",ext.c_str());

                std::string svr=
                    "PHASE27_SERVER_USE_RESOLVE attempts="+
                    std::to_string((long long)read64(&g_server_use_resolve_attempts))+
                    " peer_state_rpm_ok="+
                    std::to_string((long long)read64(&g_server_use_peer_state_rpm_ok))+
                    " peer_state_native_ok="+
                    std::to_string((long long)read64(&g_server_use_peer_state_native_ok))+
                    " peer_state_nonzero="+
                    std::to_string((long long)read64(&g_server_use_peer_state_nonzero))+
                    " peer_actor_rpm_ok="+
                    std::to_string((long long)read64(&g_server_use_peer_actor_rpm_ok))+
                    " peer_actor_native_ok="+
                    std::to_string((long long)read64(&g_server_use_peer_actor_native_ok))+
                    " peer_actor_nonzero="+
                    std::to_string((long long)read64(&g_server_use_peer_actor_nonzero))+
                    " peer_chain_ok="+
                    std::to_string((long long)read64(&g_server_use_actor_chain_ok))+
                    " fallback_attempts="+
                    std::to_string((long long)read64(&g_server_use_actor_fallback_attempts))+
                    " fallback_ok="+
                    std::to_string((long long)read64(&g_server_use_actor_fallback_ok))+
                    " actor_resolved="+
                    std::to_string((long long)read64(&g_server_use_actor_resolved))+
                    " actor_id_match="+
                    std::to_string((long long)read64(&g_server_use_actor_id_match))+
                    " inventory_getter_ok="+
                    std::to_string((long long)read64(&g_server_use_inventory_getter_ok))+
                    " inventory_offset_match="+
                    std::to_string((long long)read64(&g_server_use_inventory_offset_match))+
                    " lookup_call_ok="+
                    std::to_string((long long)read64(&g_server_use_lookup_call_ok))+
                    " item_ref_direct_ok="+
                    std::to_string((long long)read64(&g_server_use_item_ref_direct_ok))+
                    " item_resolved="+
                    std::to_string((long long)read64(&g_server_use_item_resolved))+
                    " definition_ok="+
                    std::to_string((long long)read64(&g_server_use_definition_ok))+
                    " local_active_match="+
                    std::to_string((long long)read64(&g_server_use_local_active_match))+
                    " local_handheld_match="+
                    std::to_string((long long)read64(&g_server_use_local_handheld_match));
                log_line(ANY_LOG_INFO,"framework",svr.c_str());

                for(int i=0;i<VC_SLOT_COUNT;++i) {
                    std::string m=
                        "PHASE27_COMPONENT_STATS type="+
                        std::string(vehicle_component_slot_name((VehicleComponentEventSlot)i))+
                        " captured="+std::to_string((long long)read64(&g_component_captured[i]))+
                        " dispatched="+std::to_string((long long)read64(&g_component_dispatched[i]))+
                        " dropped="+std::to_string((long long)read64(&g_component_dropped[i]))+
                        " callbacks="+std::to_string(component_callbacks);
                    log_line(ANY_LOG_INFO,"framework",m.c_str());
                }

                std::string component_validate=
                    "PHASE27_COMPONENT_VALIDATE client_class_created="+
                    std::to_string((long long)read64(&g_component_client_class_created))+
                    " client_attach_class_match="+
                    std::to_string((long long)read64(&g_component_client_attach_class_match))+
                    " client_destroy_registry_match="+
                    std::to_string((long long)read64(&g_component_client_destroy_registry_match))+
                    " server_class_created="+
                    std::to_string((long long)read64(&g_component_server_class_created))+
                    " server_attach_class_match="+
                    std::to_string((long long)read64(&g_component_server_attach_class_match))+
                    " server_destroy_registry_match="+
                    std::to_string((long long)read64(&g_component_server_destroy_registry_match))+
                    " registry_entries="+std::to_string((long long)InterlockedCompareExchange(&g_component_registry_count,0,0));
                log_line(ANY_LOG_INFO,"framework",component_validate.c_str());

                std::string abi=
                    "PHASE27_ABI_STATS client_create_nonzero="+
                    std::to_string((long long)read64(&g_client_create_nonzero))+
                    " client_create_backlink_match="+
                    std::to_string((long long)read64(&g_client_create_backlink_match))+
                    " client_remove_snapshot_ok="+
                    std::to_string((long long)read64(&g_client_remove_snapshot_ok))+
                    " server_create_nonzero="+
                    std::to_string((long long)read64(&g_server_create_nonzero))+
                    " server_create_backlink_match="+
                    std::to_string((long long)read64(&g_server_create_backlink_match))+
                    " server_destroy_snapshot_ok="+
                    std::to_string((long long)read64(&g_server_destroy_snapshot_ok));
                log_line(ANY_LOG_INFO,"framework",abi.c_str());

                {
                    AnymakerModActionApiInfoV1 ai{};
                    get_mod_action_api_info(&ai);
                    char b[1200]{};
                    std::snprintf(
                        b,sizeof(b),
                        "PHASE27_MOD_ACTION_STATS registered=%u controls_hook_ready=%u controls_recent=%u "
                        "section_ready=%u section_visible=%u capture_active=%u section_mode=%u "
                        "begin=%llu end=%llu binding_loads=%llu binding_writes=%llu "
                        "section_frames=%llu button_clicks=%llu rebind_commits=%llu rebind_cancels=%llu ui_exceptions=%llu",
                        ai.registered_actions,ai.controls_menu_hook_ready,ai.controls_menu_recently_seen,
                        ai.controls_section_ready,ai.controls_section_visible,ai.rebind_capture_active,ai.controls_section_mode,
                        (unsigned long long)ai.begin_events,(unsigned long long)ai.end_events,
                        (unsigned long long)ai.binding_file_loads,(unsigned long long)ai.binding_file_writes,
                        (unsigned long long)ai.controls_section_frames,(unsigned long long)ai.controls_button_clicks,
                        (unsigned long long)ai.rebind_commits,(unsigned long long)ai.rebind_cancels,
                        (unsigned long long)ai.controls_ui_exceptions
                    );
                    log_line(ANY_LOG_INFO,"framework",b);
                }

                {
                    AnymakerClientActorApiInfoV1 ai{};
                    get_client_actor_api_info(&ai);
                    char b[1100]{};
                    std::snprintf(
                        b,sizeof(b),
                        "PHASE27_ACTOR_STATS hooks=%d/2 registry_live=%u callbacks=%u created=%llu destroyed=%llu "
                        "dispatched=%llu dropped=%llu state_queries=%llu position_ok=%llu "
                        "stale_checks=%llu stale_pruned=%llu synthetic_destroyed=%llu "
                        "create_arg_valid=%lld destroy_arg_valid=%lld",
                        client_actor_hooks,ai.registry_live,ai.registered_callbacks,
                        (unsigned long long)ai.created_captured,(unsigned long long)ai.destroyed_captured,
                        (unsigned long long)ai.events_dispatched,(unsigned long long)ai.events_dropped,
                        (unsigned long long)ai.state_queries,(unsigned long long)ai.state_position_ok,
                        (unsigned long long)ai.stale_prune_checks,(unsigned long long)ai.stale_pruned,
                        (unsigned long long)ai.synthetic_destroyed,
                        (long long)read64(&g_client_actor_create_arg_valid),
                        (long long)read64(&g_client_actor_destroy_arg_valid)
                    );
                    log_line(ANY_LOG_INFO,"framework",b);
                }

                {
                    AnymakerUiApiInfoV1 ui{};
                    get_ui_api_info(&ui);
                    char b[1024]{};
                    std::snprintf(
                        b,sizeof(b),
                        "PHASE27_UI_API_STATS hook_ready=%u rectangle_ready=%u texture_ready=%u callbacks=%u "
                        "frames=%llu callback_invocations=%llu rect_draws=%llu texture_draws=%llu "
                        "texture_resolve_ok=%llu texture_resolve_fail=%llu rejected_draws=%llu "
                        "callback_exceptions=%llu key_queries=%llu viewport=(%.12f,%.12f) cell_size=%.12f",
                        ui.ui_hook_ready,ui.rectangle_ready,ui.texture_ready,ui.registered_callbacks,
                        (unsigned long long)ui.render_frames,
                        (unsigned long long)ui.callback_invocations,
                        (unsigned long long)ui.rectangle_draws,
                        (unsigned long long)ui.texture_draws,
                        (unsigned long long)ui.texture_resolve_ok,
                        (unsigned long long)ui.texture_resolve_fail,
                        (unsigned long long)ui.rejected_draws,
                        (unsigned long long)ui.callback_exceptions,
                        (unsigned long long)ui.key_queries,
                        ui.last_viewport_width,ui.last_viewport_height,ui.ui_cell_size
                    );
                    log_line(ANY_LOG_INFO,"framework",b);
                }

                uintptr_t map_bg=0,map_trees=0,map_lines=0;
                bool map_tex=map_textures_ready(&map_bg,&map_trees,&map_lines);
                std::string map_stats=
                    "PHASE27_MAP_STATS visible="+
                    std::to_string(is_world_map_visible()?1:0)+
                    " ui_ready="+
                    std::to_string((int)InterlockedCompareExchange(&g_map_ui_hook_ready,0,0))+
                    " textures_ready="+std::to_string(map_tex?1:0)+
                    " render_frames="+
                    std::to_string((long long)read64(&g_map_render_frames))+
                    " texture_ready_frames="+
                    std::to_string((long long)read64(&g_map_texture_ready_frames))+
                    " marker_frames="+
                    std::to_string((long long)read64(&g_map_marker_frames))+
                    " texture_create_calls="+
                    std::to_string((long long)read64(&g_map_texture_create_calls))+
                    " render_exceptions="+
                    std::to_string((long long)read64(&g_map_render_exceptions))+
                    " bg="+hexptr(map_bg)+
                    " trees="+hexptr(map_trees)+
                    " lines="+hexptr(map_lines);
                log_line(ANY_LOG_INFO,"framework",map_stats.c_str());

                {
                    char b[1536]{};
                    double live_cell=current_ui_cell_size();
                    LONG probe_mode=InterlockedCompareExchange(&g_render_probe_mode,0,0);
                    std::snprintf(
                        b,sizeof(b),
                        "PHASE27_RENDER_PROBE_STATS mode=%ld hook_frames=%lld cell_rect=%lld abs_rect=%lld "
                        "native_rect=%lld viewport_frames=%lld single_texture=%lld native_map=%lld marker=%lld "
                        "marker_fallback=%lld explicit_flush=%lld exceptions=%lld cell_size=%.12f "
                        "origin=(%.12f,%.12f) viewport=(%.12f,%.12f) map_pos=(%.12f,%.12f) "
                        "map_size=(%.12f,%.12f) marker_pos=(%.12f,%.12f) ui_renderer=%s native_begin=%s "
                        "native_rectangle=%s native_texture=%s native_render=%s abs_wrapper=%s",
                        (long)probe_mode,
                        (long long)read64(&g_render_probe_hook_frames),
                        (long long)read64(&g_render_probe_cell_rect),
                        (long long)read64(&g_render_probe_abs_rect),
                        (long long)read64(&g_render_probe_native_rect),
                        (long long)read64(&g_render_probe_viewport_frames),
                        (long long)read64(&g_render_probe_single_texture),
                        (long long)read64(&g_render_probe_native_map),
                        (long long)read64(&g_render_probe_marker),
                        (long long)read64(&g_render_probe_marker_cell_fallback),
                        (long long)read64(&g_render_probe_flushes),
                        (long long)read64(&g_render_probe_exceptions),
                        live_cell,g_last_ui_origin.x,g_last_ui_origin.y,
                        g_last_ui_viewport.x,g_last_ui_viewport.y,
                        g_last_map_position.x,g_last_map_position.y,
                        g_last_map_size.x,g_last_map_size.y,
                        g_last_marker_position.x,g_last_marker_position.y,
                        hexptr(g_last_client_ui_renderer).c_str(),
                        hexptr((uintptr_t)g_native_ui_begin).c_str(),
                        hexptr((uintptr_t)g_native_ui_rectangle).c_str(),
                        hexptr((uintptr_t)g_native_ui_texture_asset).c_str(),
                        hexptr((uintptr_t)g_native_ui_render).c_str(),
                        hexptr(g_absolute_rectangle_wrapper).c_str()
                    );
                    log_line(ANY_LOG_INFO,"framework",b);
                }

                AnymakerLocalPlayerStateV2 map_st{};
                if(get_local_player_state(&map_st) &&
                   (map_st.valid_fields&ANY_PLAYER_VALID_POSITION)) {
                    double map_u=0.0,map_v=0.0;
                    if(world_to_map_uv(map_st.position[0],map_st.position[2],&map_u,&map_v)) {
                        char b[320]{};
                        std::snprintf(b,sizeof(b),
                            "PHASE27_MAP_PROJECTION world=(%.3f,%.3f) uv_top_left=(%.6f,%.6f) in_bounds=%d",
                            map_st.position[0],map_st.position[2],map_u,map_v,
                            (map_u>=0.0&&map_u<=1.0&&map_v>=0.0&&map_v<=1.0)?1:0);
                        log_line(ANY_LOG_INFO,"framework",b);
                    }
                }

                last_total=total;
            }
            lastStats=now;
        }

        static DWORD p26_last_retry=0;
        if(GetTickCount()-p26_last_retry>=10000) {
            p26_last_retry=GetTickCount();p27_retry_helpers();p29_resolve_transforms();p27_retry_hooks();p272_retry_routes();p29_api_sample();
            if(p33_summary_due()){p27_report_hooks();p27_report_events();p271_report_semantics();p272_report();p272_report_callbacks();p273_report_metadata();p28_report_metadata_details();p29_report();p29_api_report();p33_report();}
        }
        static DWORD action_poll=0;
        if(GetTickCount()-action_poll>=50){action_poll=GetTickCount();poll_registered_mod_actions();p31_diagnostics_poll();}
        Sleep((lifecycle_batch||digital_batch||use_batch||player_batch||component_batch||actor_batch)?0:5);
    }
}

#include "runtime_build_guard.inc"

#include "native_players.inc"
#include "anyapi_ui_state.inc"
#include "anyapi_platform.inc"
#include "anyapi_menu.inc"
#include "anyapi_session.inc"
#include "anyapi_inventory_actions.inc"
#include "anyapi_screen_layout.inc"
#include "anyapi_scene_antialiasing.inc"
#include "anyapi_scene_controls.inc"
#include "anyapi_inventory_ui.inc"
#include "anyapi_client_tasks.inc"
static DWORD WINAPI loader_thread(void*) {
    g_game_dir=module_dir();g_game_dir_utf8=utf8(g_game_dir);
    if(!p27_build_matches())return 0;
    log_line(ANY_LOG_INFO,"framework","DLL_PLUGIN_PROFILE current_build=MATCH generic_players=1 v16_legacy_hooks=DISABLED map_specific_startup=0");
    platform::load();platform::ready();
    if(platform::plugin_count.load()&&!platform::start())log_line(ANY_LOG_ERROR,"framework","PLUGIN_PLATFORM graphics_hooks=FAILED");
    HANDLE scene_worker=CreateThread(nullptr,0,scene_controls::worker,nullptr,0,nullptr);if(scene_worker)CloseHandle(scene_worker);
    HANDLE menu_worker=CreateThread(nullptr,0,menus::worker,nullptr,0,nullptr);if(menu_worker)CloseHandle(menu_worker);
    HANDLE layout_worker=CreateThread(nullptr,0,screen_layout::worker,nullptr,0,nullptr);if(layout_worker)CloseHandle(layout_worker);
    HANDLE inventory_worker=CreateThread(nullptr,0,inventory_actions::worker,nullptr,0,nullptr);if(inventory_worker)CloseHandle(inventory_worker);
    HANDLE storage_worker=CreateThread(nullptr,0,inventory_ui::worker,nullptr,0,nullptr);if(storage_worker)CloseHandle(storage_worker);
    HANDLE worker=CreateThread(nullptr,0,p34_players_worker,nullptr,0,nullptr);
    if(worker)CloseHandle(worker);
    return 0;
}

extern "C" __declspec(dllexport)
HRESULT WINAPI DirectInput8Create(
    HINSTANCE a,DWORD b,REFIID c,LPVOID* d,LPUNKNOWN e
) {
    if(!load_real()) return E_FAIL;
    return pDirectInput8Create(a,b,c,d,e);
}

BOOL APIENTRY DllMain(HMODULE h,DWORD reason,LPVOID) {
    if(reason==DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(h);
        if(InterlockedCompareExchange(&g_loader_started,1,0)==0) {
            HANDLE t=CreateThread(nullptr,0,loader_thread,nullptr,0,nullptr);
            if(t) CloseHandle(t);
        }
    }
    return TRUE;
}
