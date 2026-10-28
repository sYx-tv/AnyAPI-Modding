// AnyLoot TEST BUILD: proves the two game levers searchable zombie and animal bodies need.
//
// Zombies and creatures are plain actors with no inventory, and items can only sit on the floor, in a wearable or
// hand slot, or in a vehicle part. So a searchable body is a storage part the host hides inside the corpse, shown
// when the body is inspected. This build checks both halves:
//
//   F8 (host): spawns a small storage crate two metres in front of you and fills it with the game's own loot roll
//      at the world's loot level. It follows the creative dome's recipe (server_scene.creative_dome.
//      _spawn_container_vehicle): vehicle.container.create, vehicle.add_component, vehicle.post_load, then
//      vehicle.generate_loot. Runs at the start of the next server tick, on the server thread.
//   Bodies: client_scene.actor.on_inspect fills the items the game shows when you look at and inspect an actor.
//      Stock it adds nothing for zombies and creatures (only characters override it). While a test crate exists,
//      a downed zombie or creature reports that crate's items instead, through the crate part's own on_inspect, so
//      looking at the body and pressing inspect should open the storage screen with the crate's loot.
//   The game's loot roll can leave a fresh crate empty, so three food items are always added with the game's own
//   _spawn_loot_item_in_furniture. A second and a half later the crate is looked up on this client and its items
//   counted, and a line at the top of the screen says what happened (also for each body inspected).
//
// Everything is logged to anymaker_modding.log with an ANYLOOT prefix: which storage part was used, whether the
// crate spawned, was filled and reached this client, and every inspect call on a zombie or creature.
// 0.1.0 had an F7 body toggle; it is gone, since in the first test the bodies were inspected while it was off.
//
// EXPERIMENTAL. Experimental SDK bindings (anyloot_bindings.h): Anymaker 0.1.27 / Steam build 26113663 only.
#define NOMINMAX
#include <windows.h>
#include "anyapi_services_v1.h"
#include "anyapi_ui_state_v1.h"
#include "anyapi_gpu_draw_v1.h"
#include "anyapi_client_tasks_v1.h"
#include "anyapi_experimental.hpp"
#include "anyloot_bindings.h"
#include "route_cell.h"
#include "vehicle_reads.h"
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <deque>
#include <filesystem>
#include <fstream>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace anyloot {
namespace {
constexpr const char* kModId = "anyloot";
using vehicle_reads::rd;

// Layouts from the 0.1.27 reference (metadata; unchanged since 0.1.25).
namespace off {
// server_scene
constexpr ptrdiff_t server_peers = 0xa8;            // m_peers (server_scene.peer.container)
constexpr ptrdiff_t server_vehicles = 0x1b0;        // m_vehicles (server_scene.vehicle.container)
constexpr ptrdiff_t server_physics = 0xa60;         // m_physics (physics.scene)
constexpr ptrdiff_t server_tiles = 0xc48;           // m_tile_container (server_scene.environment_tile_container)
constexpr ptrdiff_t server_component_defs = 0x1028 + 8;  // m_component_definitions.m_definitions vector<ref<definition>>
constexpr ptrdiff_t server_loot_level = 0x14d8;     // m_loot_level (s32)
constexpr ptrdiff_t server_item_defs = 0xf80;       // m_inventory_definitions.m_definitions vector<ref<inventory_definition>>
constexpr ptrdiff_t peer_position = 0x19f8;         // server_scene.peer position (vec3), as AnyHelpers' admin uses it
constexpr ptrdiff_t server_object_id = 0x40;        // server_scene.vehicle.m_id and server_scene.vehicle_component.m_id
constexpr ptrdiff_t server_vehicle_motion = 0x5a0 + 0x18;  // server_scene.vehicle motion type property _m_value (u32, 1 = static)
// vehicle_component_definition
constexpr ptrdiff_t def_id = 0, def_name = 0x10, def_class = 0x30, def_building_furniture = 0x1b2;
// client
constexpr ptrdiff_t client_scene = 0xf50;           // client.m_scene (embedded)
constexpr ptrdiff_t scene_vehicles = 0xa0;          // client_scene.m_vehicles (client_scene.vehicle.container)
constexpr ptrdiff_t actor_id = 8;                   // client_scene.actor.m_id
constexpr ptrdiff_t actor_incapacitated = 0x28 + 8; // client_scene.actor._m_is_incapacitated value (as AnyMap reads it)
constexpr ptrdiff_t storage_items = 0xe0 + 8;       // client_scene.vehicle_component.inventory.m_items -> vector<ref<item>>
constexpr ptrdiff_t client_vehicle_motion = 0x298 + 8;    // client_scene.vehicle.m_physics_motion_type value
constexpr ptrdiff_t client_vehicle_in_scene = 0x2b8;      // client_scene.vehicle.m_is_added_to_scene (bool)
}  // namespace off

// Not in anyloot_bindings.h because they are only anchors: g_client, and the zombie and creature tick functions
// whose typeinfo slots name those classes (the same descriptors AnyMap uses).
namespace anchor {
using anymaker::func_desc;
using anymaker::global_desc;
inline constexpr global_desc g_client{"g_client","48 83 ec 38 48 89 4c 24 20 48 89 54 24 28 48 8b 05 4b 00 00 00 48 83 78 08 00 0f 95 c0 3c 00 0f",96u};
inline constexpr func_desc zombie_tick{"() client_scene.actor_zombie.tick (client_scene.actor_zombie, client, client_scene, const f64, const f64, const vec3)","53 55 56 57 41 54 41 55 41 56 41 57 48 81 ec 08 10 00 00 4c 8b a4 24 70 10 00 00 48 8b 9c 24 78",{"53 55 56 57 48 81 ec 38 01 00 00 48 8b 84 24 80 01 00 00 48 8b ac 24 88 01 00 00 48 89 4c 24 70", {536u}, 1},"53 48 83 ec 30 48 89 4c 24 20 48 8d 01 48 8b 15 24 03 00 00 48 89 4c 24 28 48 89 c1 ff 12 48 8b",832u,4};
inline constexpr func_desc creature_tick{"() client_scene.actor_creature.tick (client_scene.actor_creature, client, client_scene, const f64, const f64, const vec3)","53 55 56 57 48 81 ec c8 01 00 00 48 8b 84 24 10 02 00 00 48 8b 9c 24 18 02 00 00 48 89 8c 24 e0",{"53 55 56 57 41 54 41 55 41 56 41 57 48 81 ec 28 07 00 00 4c 8b a4 24 90 07 00 00 48 8b 84 24 98", {6848u}, 1},"48 83 ec 38 48 89 4c 24 20 48 8d 01 48 8b 15 fd 01 00 00 48 89 4c 24 28 48 89 c1 ff 12 48 8b 05",536u,4};
}  // namespace anchor

constexpr int kMotionSetSlot = 4;   // replication.master.property_enum<physics.e_motion_type>.set
constexpr int kInspectSlot = 36;   // vehicle_component.on_inspect in the part's method table (the binding's route slot)

struct Vec3 { double x, y, z; };
struct Vec3i { int32_t x, y, z; };
struct Mat34 { double v0[3], v1[3], v2[3], t[3]; };   // three basis vectors, then the translation
struct GridOriginDir { Vec3i origin, dir; };          // vehicle_util.grid_origin_dir, 24 bytes
static_assert(sizeof(Mat34) == 96 && sizeof(GridOriginDir) == 24, "layouts");

using tick_t = void (*)(uint8_t* scene, uint8_t* server);
using get_by_id_t = void (*)(uint8_t** ret, uint8_t* container, const int32_t* id);
using vehicle_create_t = void (*)(uint8_t** ret, uint8_t* vehicles, uint8_t* physics, uint8_t* tiles, const Mat34* transform);
using add_component_t = void (*)(uint8_t** ret, uint8_t* vehicle, uint8_t* scene, const GridOriginDir* grid, const Vec3i* position,
                                 const int32_t* rotation, const Vec3i* extend, const uint8_t* definition);
using post_load_t = void (*)(uint8_t* vehicle, uint8_t* scene);
using generate_loot_t = void (*)(uint8_t* vehicle, uint8_t* scene, const int32_t* level);
using on_inspect_t = void (*)(uint8_t* self, anymaker::gc_vector_raw* items);
using spawn_item_t = void (*)(bool* ret, uint8_t* vehicle, uint8_t* scene, uint32_t* random, const uint8_t* const* definition);

AnyModHostV1 host;
const AnyUiStateV1* ui;
const AnyGpuDrawV1* gpu;
const AnyClientTasksV1* tasks;
uint64_t g_task_owner;
std::atomic<bool> g_stop{false}, g_ready{false}, g_bodies{true};

anymaker::cell_hook h_tick, h_inspect;
get_by_id_t peer_by_id, client_vehicle_by_id, client_component_by_id;
vehicle_create_t vehicle_create;
add_component_t add_component;
post_load_t post_load;
generate_loot_t generate_loot;
spawn_item_t spawn_item;
on_inspect_t inventory_inspect;
uint8_t** g_client_ref;
const uint8_t *g_zombie_type, *g_creature_type;

// The last test crate, written by the server thread and read by the main thread.
std::atomic<int32_t> g_crate_vehicle{-1}, g_crate_component{-1};
std::atomic<uint64_t> g_last_tick{0}, g_check_after{0};   // check_after: when the client looks for the newest crate
std::atomic<int> g_food_added{0};

// One line at the top of the screen for a few seconds, so the tester sees what each step did.
std::mutex g_toast_mutex;
std::wstring g_toast;
ULONGLONG g_toast_until;
void toast(const std::wstring& text) { std::lock_guard lock(g_toast_mutex); g_toast = text; g_toast_until = GetTickCount64() + 5000; }

struct Spawn { double yaw; ULONGLONG posted; bool dynamic; };
std::mutex g_mutex;
std::deque<Spawn> g_queue;   // F8 presses waiting for the next server tick

void log(int level, const char* fmt, ...) {
    if (!host.log) return;
    char buf[1024];
    va_list ap; va_start(ap, fmt); vsnprintf(buf, sizeof buf, fmt, ap); va_end(ap);
    host.log(uint32_t(level), kModId, buf);
}

std::string read_string(const uint8_t* at) {
    if (!anymaker::readable(at, sizeof(anymaker::gc_string_view))) return {};
    anymaker::gc_string_view s; std::memcpy(&s, at, sizeof s);
    if (!s.data || s.length <= 0 || s.length > 256 || !anymaker::readable(s.data, size_t(s.length))) return {};
    return std::string(s.data, size_t(s.length));
}

bool gameplay() { AnyUiSnapshotV1 s; return ui && ui->copy(&s) && s.kind == ANY_UI_GAMEPLAY; }

// ------------------------------------------------------------------ server thread (host only)
// The storage part for the crate. AnyLoot.test.txt beside the DLL may name a part id on its first line; otherwise
// the first storage-class part whose id says crate, then box, then any storage part. The first scan logs every part
// class and the storage candidates, so the right part can be named for the real mod.
const uint8_t* pick_storage_part(uint8_t* scene) {
    static bool logged = false;
    std::string wanted;
    if (host.plugin_directory) {
        std::filesystem::path file = std::filesystem::path(host.plugin_directory) / L"AnyLoot.test.txt";
        std::ifstream in(file);
        std::getline(in, wanted);
        while (!wanted.empty() && (wanted.back() == '\r' || wanted.back() == ' ')) wanted.pop_back();
    }
    std::map<std::string, int> classes;
    std::vector<std::pair<std::string, const uint8_t*>> storage;
    const uint8_t* named = nullptr;
    vehicle_reads::for_each_ref(scene + off::server_component_defs, 8192, [&](uint8_t* def) {
        if (!anymaker::readable(def, off::def_building_furniture + 1)) return true;
        std::string id = read_string(def + off::def_id), cls = read_string(def + off::def_class);
        if (id.empty()) return true;
        ++classes[cls];
        if (!wanted.empty() && id == wanted) named = def;
        if (cls.find("inventory") != std::string::npos || cls.find("storage") != std::string::npos) storage.push_back({id, def});
        return true;
    });
    if (!logged) {
        logged = true;
        std::string text;
        for (auto& [cls, n] : classes) { text += " " + (cls.empty() ? std::string("?") : cls) + "=" + std::to_string(n); if (text.size() > 900) break; }
        log(0, "ANYLOOT_PART_CLASSES%s", text.c_str());
        text.clear();
        for (auto& [id, def] : storage) {
            text += " " + id + (rd<bool>(def, off::def_building_furniture) ? "(furniture)" : "");
            if (text.size() > 900) { log(0, "ANYLOOT_STORAGE_PARTS%s", text.c_str()); text.clear(); }
        }
        log(0, "ANYLOOT_STORAGE_PARTS%s", text.empty() ? " (none)" : text.c_str());
    }
    if (named) return named;
    if (!wanted.empty()) log(1, "ANYLOOT_PART id=%s not found; picking one", wanted.c_str());
    for (const char* word : {"crate", "box"})
        for (auto& [id, def] : storage) if (id.find(word) != std::string::npos) return def;
    return storage.empty() ? nullptr : storage.front().second;
}

// Up to `n` random food items (class food, food_canned, food_jar...) from the world's item definitions. The first
// call logs every item class, for planning the real loot tables.
std::vector<const uint8_t*> pick_food(uint8_t* scene, int n) {
    static bool logged = false;
    std::map<std::string, int> classes;
    std::vector<const uint8_t*> food;
    vehicle_reads::for_each_ref(scene + off::server_item_defs, 8192, [&](uint8_t* def) {
        if (!anymaker::readable(def, 0x40)) return true;
        std::string cls = read_string(def + off::def_class);
        ++classes[cls];
        if (cls.rfind("food", 0) == 0) food.push_back(def);
        return true;
    });
    if (!logged) {
        logged = true;
        std::string text;
        for (auto& [cls, k] : classes) { text += " " + (cls.empty() ? std::string("?") : cls) + "=" + std::to_string(k); if (text.size() > 900) break; }
        log(0, "ANYLOOT_ITEM_CLASSES%s", text.c_str());
    }
    std::vector<const uint8_t*> out;
    for (int i = 0; i < n && !food.empty(); ++i) out.push_back(food[(GetTickCount64() / 7 + i * 7919u) % food.size()]);
    return out;
}

// Sets the vehicle's motion type through the property's own setter, so it is marked changed and reaches the
// clients (a plain write left every client at dynamic, with no visible body). 0 dynamic, 1 static, 2 kinematic.
const char* set_motion(uint8_t* vehicle, uint32_t value) {
    using set_t = void (*)(uint8_t* property, const uint32_t* value);
    uint8_t* property = vehicle + off::server_vehicle_motion - 0x18;
    void* fn = anymaker::typeinfo_of(property).method(kMotionSetSlot);
    static const uint8_t head[] = {0x48, 0x83, 0xec, 0x48, 0x48, 0x89, 0x4c, 0x24, 0x20, 0x48, 0x89, 0x54, 0x24, 0x28, 0x8b, 0x02, 0x3b, 0x41, 0x18};
    if (!fn || !anymaker::readable(fn, sizeof head) || std::memcmp(fn, head, sizeof head)) return "setter_not_found";
    reinterpret_cast<set_t>(fn)(property, &value);
    return "setter";
}

void spawn_crate(uint8_t* scene, const Spawn& s) {
    uint8_t* host_peer = nullptr;
    for (int32_t id = 0; id < 64 && !host_peer; ++id) peer_by_id(&host_peer, scene + off::server_peers, &id);   // the host joins first
    if (!host_peer || !anymaker::readable(host_peer + off::peer_position, 24)) { log(1, "ANYLOOT_SPAWN ok=0 reason=no_host_player"); return; }
    Vec3 p = rd<Vec3>(host_peer, off::peer_position);
    const uint8_t* part = pick_storage_part(scene);
    if (!part) { log(2, "ANYLOOT_SPAWN ok=0 reason=no_storage_part"); return; }
    std::string part_id = read_string(part + off::def_id);
    // Two metres in front of you (yaw 0 faces +Z, +pi/2 faces +X). F8 places it static on the ground, the way the
    // game places a crate itself; F9 keeps the old loose crate a metre up so it drops, for comparison.
    Mat34 m{{1, 0, 0}, {0, 1, 0}, {0, 0, 1}, {p.x + 2 * std::sin(s.yaw), p.y + (s.dynamic ? 1 : 0), p.z + 2 * std::cos(s.yaw)}};
    uint8_t* vehicle = nullptr;
    vehicle_create(&vehicle, scene + off::server_vehicles, scene + off::server_physics, scene + off::server_tiles, &m);
    if (!vehicle) { log(2, "ANYLOOT_SPAWN ok=0 reason=vehicle_create_null part=%s", part_id.c_str()); return; }
    const char* motion_how = set_motion(vehicle, s.dynamic ? 0 : 1);   // before any part goes in, as the game does
    uint32_t motion = anymaker::readable(vehicle + off::server_vehicle_motion, 4) ? rd<uint32_t>(vehicle, off::server_vehicle_motion) : 99;
    GridOriginDir grid{};
    Vec3i at{0, 0, 0};
    const int32_t identity[9] = {1, 0, 0, 0, 1, 0, 0, 0, 1};
    uint8_t* component = nullptr;
    Vec3i extend{0, 0, 0};
    add_component(&component, vehicle, scene, &grid, &at, identity, &extend, part);
    if (!component) {   // the extend size might count blocks rather than extra blocks
        extend = {1, 1, 1};
        add_component(&component, vehicle, scene, &grid, &at, identity, &extend, part);
    }
    post_load(vehicle, scene);
    int32_t level = rd<int32_t>(scene, off::server_loot_level);
    std::string food_text;
    int added = 0;
    if (component) {
        generate_loot(vehicle, scene, &level);
        uint32_t random = uint32_t(GetTickCount64()) | 1;
        for (const uint8_t* def : pick_food(scene, 3)) {
            bool ok = false;
            spawn_item(&ok, vehicle, scene, &random, &def);
            added += ok;
            food_text += " " + read_string(def + off::def_id) + (ok ? "" : "(no room)");
        }
    }
    int32_t vid = rd<int32_t>(vehicle, off::server_object_id), cid = component ? rd<int32_t>(component, off::server_object_id) : -1;
    g_crate_vehicle = vid; g_crate_component = cid; g_food_added = added;
    log(component ? 0 : 2, "ANYLOOT_SPAWN ok=%d motion=%u(%s,%s) part=%s vehicle=%d component=%d extend=%d loot_level=%d at=(%.1f, %.1f, %.1f) food_added=%d:%s",
        component ? 1 : 0, motion, s.dynamic ? "F9_dynamic" : "F8_static", motion_how, part_id.c_str(), vid, cid, extend.x, level, m.t[0], m.t[1], m.t[2], added, food_text.empty() ? " none" : food_text.c_str());
}

// Kept apart from hk_tick: __try cannot share a function with objects that need unwinding.
void spawn_guarded(uint8_t* scene, const Spawn& s) {
    __try { spawn_crate(scene, s); }
    __except (EXCEPTION_EXECUTE_HANDLER) { log(2, "ANYLOOT_SPAWN ok=0 reason=native_exception"); }
}

void hk_tick(uint8_t* scene, uint8_t* server) {
    if (scene && !g_stop) {
        g_last_tick = GetTickCount64();
        std::deque<Spawn> work;
        { std::lock_guard lock(g_mutex); work.swap(g_queue); }
        for (auto& s : work) {
            if (GetTickCount64() - s.posted > 3000) { log(1, "ANYLOOT_SPAWN ok=0 reason=expired"); continue; }
            spawn_guarded(scene, s);
        }
    }
    reinterpret_cast<tick_t>(h_tick.original)(scene, server);
}

// ------------------------------------------------------------------ main thread (every player)
uint8_t* client_scene() {
    uint8_t** ref = g_client_ref;
    uint8_t* client = ref && anymaker::readable(ref, 16) ? ref[1] : nullptr;
    return client && anymaker::readable(client + off::client_scene, 0x200) ? client + off::client_scene : nullptr;
}

const char* body_kind(const uint8_t* actor) {
    auto type = anymaker::typeinfo_of(actor);
    if (!anymaker::readable(type.p, 0x30)) return nullptr;
    if (g_zombie_type && anymaker::typeinfo_is(type, g_zombie_type)) return "zombie";
    if (g_creature_type && anymaker::typeinfo_is(type, g_creature_type)) return "creature";
    return nullptr;
}

int32_t count(const anymaker::gc_vector_raw* v) { return v && anymaker::readable(v, sizeof *v) ? v->count : -1; }

// The test crate's storage part on this client, or null with the reason. Main thread only.
int32_t g_client_motion = -1, g_client_in_scene = -1;   // what the last client_crate saw on the crate vehicle
uint8_t* client_crate(const char** why) {
    int32_t vid = g_crate_vehicle, cid = g_crate_component;
    if (vid < 0 || cid < 0) { *why = "no_crate"; return nullptr; }
    uint8_t* scene = client_scene();
    if (!scene) { *why = "no_client_scene"; return nullptr; }
    uint8_t *vehicle = nullptr, *component = nullptr;
    client_vehicle_by_id(&vehicle, scene + off::scene_vehicles, &vid);
    if (!vehicle) { *why = "crate_vehicle_not_on_client"; return nullptr; }
    if (anymaker::readable(vehicle, off::client_vehicle_in_scene + 1)) {
        g_client_motion = rd<uint32_t>(vehicle, off::client_vehicle_motion);
        g_client_in_scene = rd<uint8_t>(vehicle, off::client_vehicle_in_scene);
    }
    client_component_by_id(&component, vehicle, &cid);
    if (!component || !anymaker::readable(component, off::storage_items + sizeof(anymaker::gc_vector_raw))) { *why = "crate_part_not_on_client"; return nullptr; }
    return component;
}

// Adds the test crate's items to a downed body's inspect list. Returns what happened, for the log.
const char* add_crate_items(anymaker::gc_vector_raw* items) {
    const char* why = "";
    uint8_t* component = client_crate(&why);
    if (!component) return why;
    // Call the part's own on_inspect only when its method table really holds the storage version (or another
    // mod's hook on it), never some other part's.
    void* fn = anymaker::typeinfo_of(component).method(kInspectSlot);
    if (fn != reinterpret_cast<void*>(inventory_inspect) && !route::in_other_module(fn)) return "crate_part_is_not_storage";
    reinterpret_cast<on_inspect_t>(fn)(component, items);
    return "added";
}

// Kept apart from hk_on_inspect: building the text needs objects that unwind.
void inspect_toast(const char* kind, int32_t items, const char* result) {
    wchar_t buf[160];
    if (!std::strcmp(result, "added")) swprintf(buf, 160, L"AnyLoot: this %hs body holds %d items. Press interact to open it.", kind, items);
    else swprintf(buf, 160, L"AnyLoot: %hs body, nothing added (%hs)", kind, result);
    toast(buf);
}

void hk_on_inspect(uint8_t* actor, anymaker::gc_vector_raw* items) {
    reinterpret_cast<on_inspect_t>(h_inspect.original)(actor, items);
    if (g_stop || !actor || !items) return;
    static int logs = 0;
    static int32_t last_logged = -1;
    __try {
        const char* kind = body_kind(actor);
        if (!kind) return;
        bool down = rd<bool>(actor, off::actor_incapacitated);
        int32_t id = rd<int32_t>(actor, off::actor_id), before = count(items);
        const char* result = !down ? "alive" : !g_bodies ? "bodies_off" : add_crate_items(items);
        // on_inspect runs every frame you look at something, so log each body once per change of target.
        if (logs < 60 && id != last_logged) {
            ++logs; last_logged = id;
            log(0, "ANYLOOT_INSPECT actor=%d kind=%s down=%d items_before=%d items_after=%d result=%s", id, kind, down ? 1 : 0, before, count(items), result);
            if (down) inspect_toast(kind, count(items), result);
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        g_bodies = false;
        log(2, "ANYLOOT_INSPECT native_exception: the body hook is off for this session");
    }
}

// A second and a half after a spawn, on the client actor tick: does this client see the crate, and how full is it?
void check_crate(const AnyClientTaskFrameV1*, void*);
void post_check() { if (tasks && g_task_owner) tasks->post(g_task_owner, 0, check_crate, nullptr); }
void check_crate_guarded(int32_t* items, const char** why) {
    __try {
        uint8_t* component = client_crate(why);
        if (component) *items = count(reinterpret_cast<anymaker::gc_vector_raw*>(component + off::storage_items));
    } __except (EXCEPTION_EXECUTE_HANDLER) { *why = "native_exception"; }
}
void check_crate(const AnyClientTaskFrameV1*, void*) {
    if (g_stop) return;
    if (GetTickCount64() < g_check_after) { post_check(); return; }
    int32_t items = -1;
    const char* why = "found";
    g_client_motion = g_client_in_scene = -1;
    check_crate_guarded(&items, &why);
    log(0, "ANYLOOT_CRATE_CLIENT vehicle=%d result=%s items=%d food_added=%d client_motion=%d in_scene=%d", g_crate_vehicle.load(), why, items,
        g_food_added.load(), g_client_motion, g_client_in_scene);
    wchar_t buf[160];
    if (items >= 0) swprintf(buf, 160, L"AnyLoot: crate spawned in front of you with %d items", items);
    else swprintf(buf, 160, L"AnyLoot: crate made on the host, but this client can't see it (%hs)", why);
    toast(buf);
}

void draw(const AnyFrameV1* f, void*) {
    if (!f || !f->focused || !gameplay()) return;
    std::wstring text;
    { std::lock_guard lock(g_toast_mutex); if (GetTickCount64() < g_toast_until) text = g_toast; }
    if (text.empty()) return;
    float s = std::max(0.75f, float(f->height) / 1080.f), w = 900 * s, h = 40 * s;
    AnyGpuCommandV1 c; c.kind = ANY_GPU_ROUND_RECT; c.rect[0] = (f->width - w) / 2; c.rect[1] = 70 * s; c.rect[2] = w; c.rect[3] = h;
    c.radius = 8 * s; c.color = 0xff15181c; c.opacity = 0.8f; gpu->emit(&c);
    AnyGpuCommandV1 t; t.kind = ANY_GPU_TEXT; t.rect[0] = c.rect[0]; t.rect[1] = c.rect[1]; t.rect[2] = w; t.rect[3] = h;
    t.text = text.c_str(); t.text_length = uint32_t(text.size()); t.font_size = 20 * s; t.color = 0xfff2e6c8;
    t.flags = ANY_GPU_CENTER | ANY_GPU_VCENTER | ANY_GPU_NOWRAP; gpu->emit(&t);
}

// ------------------------------------------------------------------ input
uint32_t on_input(const AnyInputV1* e, void*) {
    static bool held;   // a consumed F8/F9 press also consumes its release
    if (!e || (e->kind != ANY_KEY_DOWN && e->kind != ANY_KEY_UP) || (e->key != VK_F8 && e->key != VK_F9)) return 0;
    if (e->kind == ANY_KEY_UP) { bool consume = held; held = false; return consume ? 1 : 0; }
    if (!g_ready || !gameplay()) return 0;
    held = true;
    if (GetTickCount64() - g_last_tick > 2000) {
        log(1, "ANYLOOT_SPAWN ok=0 reason=not_hosting (the host spawns crates)");
        toast(L"AnyLoot: only the host can spawn the test crate");
        return 1;
    }
    double yaw = 0;
    static AnySessionPlayersV1 players;   // about 21 KB, so not on the input thread's stack
    players = AnySessionPlayersV1{};
    uint32_t flags = 0;
    if (host.copy_players && host.copy_players(&players, &flags))
        for (uint32_t i = 0; i < players.count && i < 64; ++i)
            if ((players.players[i].valid_fields & PLAYER_LOCAL) && (players.players[i].valid_fields & PLAYER_FACING))
                yaw = players.players[i].yaw_radians;
    std::lock_guard lock(g_mutex);
    if (g_queue.size() < 4) g_queue.push_back({yaw, GetTickCount64(), e->key == VK_F9});
    g_check_after = GetTickCount64() + 1500;
    post_check();
    return 1;
}

// ------------------------------------------------------------------ install (background thread)
const uint8_t* typeinfo_for(const anymaker::func_desc& d) {
    if (!d.ti_anchor_sig || !*d.ti_anchor_sig || d.method_slot < 0) return nullptr;
    uint8_t* anchor_fn = anymaker::find_gcl_function(d.ti_anchor_sig);
    auto* ti = anchor_fn ? reinterpret_cast<const uint8_t*>(anymaker::read_slot(anchor_fn, d.ti_slot_offset)) : nullptr;
    if (!ti || !anymaker::readable(ti, 0x88)) return nullptr;
    anymaker::gc_typeinfo_view view{ti};
    void* const* table = view.table();
    if (view.kind() != 13 || !anymaker::readable(table, 8 * (size_t(d.method_slot) + 1))) return nullptr;
    void* entry = anymaker::resolve(d);
    return entry && view.method(d.method_slot) == entry ? ti : nullptr;
}

void install() {
    using namespace anymaker::experimental;
    for (int attempt = 0; attempt < 180 && !g_stop; ++attempt) {
        if (attempt) Sleep(1000);
        if (!matching_build()) { if (!attempt) log(1, "ANYLOOT_OFF reason=game_build_mismatch"); return; }
        auto f = [](const anymaker::func_desc& d) { return route::function(d); };
        void* fns[] = {f(bind::peer_container_get_peer_by_id), f(bind::vehicle_container_create), f(bind::vehicle_add_component),
                       f(bind::vehicle_post_load), f(bind::vehicle_generate_loot), f(bind::inventory_on_inspect),
                       f(bind::vehicle_container_get_vehicle_by_id), f(bind::vehicle_get_component_by_id),
                       f(bind::vehicle_spawn_loot_item_in_furniture)};
        void** tick = route::cell(bind::server_scene_tick);
        void** inspect = route::cell(bind::actor_on_inspect);
        auto client = static_cast<uint8_t**>(global(anchor::g_client));
        auto zombie = typeinfo_for(anchor::zombie_tick), creature = typeinfo_for(anchor::creature_tick);
        bool all = tick && inspect && client && zombie;
        std::string found;
        for (void* p : fns) { all = all && p; found += p ? "1" : "0"; }
        if (!all) {
            if (attempt == 20 || attempt == 179)
                log(attempt == 179 ? 2 : 1, "ANYLOOT_LOOKUP functions=%s tick=%d inspect=%d client=%d zombie=%d creature=%d", found.c_str(),
                    !!tick, !!inspect, !!client, !!zombie, !!creature);
            continue;
        }
        peer_by_id = (get_by_id_t)fns[0]; vehicle_create = (vehicle_create_t)fns[1]; add_component = (add_component_t)fns[2];
        post_load = (post_load_t)fns[3]; generate_loot = (generate_loot_t)fns[4]; inventory_inspect = (on_inspect_t)fns[5];
        client_vehicle_by_id = (get_by_id_t)fns[6]; client_component_by_id = (get_by_id_t)fns[7]; spawn_item = (spawn_item_t)fns[8];
        g_client_ref = client; g_zombie_type = zombie; g_creature_type = creature;
        if (!h_tick.install(tick, (void*)&hk_tick) || !h_inspect.install(inspect, (void*)&hk_on_inspect)) {
            h_tick.remove((void*)&hk_tick);
            log(2, "ANYLOOT_OFF reason=hook_cell_not_writable");
            return;
        }
        g_ready = true;
        log(0, "ANYLOOT_READY after_s=%d creatures=%d: F8 spawns a test crate (host), then inspect a downed zombie or creature",
            attempt, creature ? 1 : 0);
        return;
    }
}
}  // namespace
}  // namespace anyloot

using namespace anyloot;
extern "C" __declspec(dllexport) bool AnyAPI_ModInit(const AnyModHostV1* h, AnyModCallbacksV1* out) {
    if (!h || !out || h->struct_size != sizeof(*h) || h->abi != ANYAPI_MOD_ABI || out->struct_size != sizeof(*out)) return false;
    host = *h; out->id = kModId; out->input = on_input;
    out->shutdown = [](void*) { g_stop = true; h_inspect.remove((void*)&hk_on_inspect); h_tick.remove((void*)&hk_tick); };
    return true;
}
extern "C" __declspec(dllexport) void AnyAPI_ModReady() {
    auto services = AnyAPI_Services(); if (!services) return;
    ui = static_cast<const AnyUiStateV1*>(services->query("anyapi.ui_state", 1));
    if (!ui || ui->struct_size != sizeof(*ui) || ui->version != 1 || !ui->copy) { ui = nullptr; log(2, "ANYLOOT_OFF reason=no_ui_state"); return; }
    gpu = static_cast<const AnyGpuDrawV1*>(services->query("anyapi.gpu_draw", 1));
    if (!gpu || gpu->struct_size != sizeof(*gpu) || gpu->version != 1 || !gpu->emit || !gpu->register_renderer || !gpu->register_renderer(draw, nullptr))
        { gpu = nullptr; log(1, "ANYLOOT gpu_draw unavailable: no on-screen messages"); }
    tasks = static_cast<const AnyClientTasksV1*>(services->query("anyapi.client_tasks", 1));
    if (tasks && tasks->struct_size == sizeof(*tasks) && tasks->version == 1 && tasks->register_owner && tasks->post) g_task_owner = tasks->register_owner();
    else { tasks = nullptr; log(1, "ANYLOOT client_tasks unavailable: the crate is not checked on this client"); }
    std::thread(install).detach();   // resolving scans game memory; keep it off the loader and frame threads
}
