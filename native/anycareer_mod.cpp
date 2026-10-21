// AnyCareer TEST BUILD (step 1): proves the three game levers a career mode (missions, traders, cash) stands on.
//
//   Cash item: a new inventory item, "Cash", added to the game's item definitions right after the game adds its
//      own (inventory_definition_container._add_definitions, on the server and the client scene). Its definition is
//      made at start-up from the player's own copy of the game's item file: the first ammo box entry is copied and
//      renamed, so it stacks like ammo (class ammo_box: the stack size is the definition's gun_magazine_capacity,
//      9999 here) and fits no gun. No game data ships with the mod. It never spawns as loot (spawn probability 0,
//      loot group none). Every player needs the mod, as for any mod-added item.
//   F10 (host): gives the host a stack of 250 Cash, dropped at your feet. Uses the creative dome's recipe:
//      server_scene.item_world.create_item, then server_scene.item_floor_container.create_item. Runs at the start of
//      the next server tick, on the server thread. The stack starts full (ammo boxes fill to capacity when made) and
//      is then set to 250 through the count property's own setter, which is checked by reading it back.
//   F9 (host): writes the price list. Every item and part definition the server holds, with its loot group,
//      tech tier, spawn probability, a first-pass price and whether it can drop at the world's current loot level,
//      goes to "AnyAPI and Modding/mods/AnyCareer/prices.tsv" (stays on this PC: it is game data). The log gets
//      a summary line.
//
// Everything is logged to anymaker_modding.log with an ANYCAREER prefix.
//
// EXPERIMENTAL. Experimental SDK bindings (anycareer_bindings.h): Anymaker 0.1.25 / Steam build 25904830 only.
#define NOMINMAX
#include <windows.h>
#include "anyapi_services_v1.h"
#include "anyapi_ui_state_v1.h"
#include "anyapi_experimental.hpp"
#include "anycareer_bindings.h"
#include "anycareer_logic.h"
#include "anyapi_asset_json.h"
#include "route_cell.h"
#include "vehicle_reads.h"
#include <atomic>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <deque>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

namespace anycareer {
namespace {
constexpr const char* kModId = "anycareer";
using vehicle_reads::rd;

// Layouts from the 0.1.25 reference (metadata).
namespace off {
// server_scene
constexpr ptrdiff_t server_peers = 0xa8;            // m_peers (server_scene.peer.container)
constexpr ptrdiff_t server_floor_items = 280;       // m_floor_items (server_scene.item_floor_container)
constexpr ptrdiff_t server_physics = 2656;          // m_physics (physics.scene)
constexpr ptrdiff_t server_inventory_defs = 3968;   // m_inventory_definitions; its m_definitions vector is at +0
constexpr ptrdiff_t server_component_defs = 0x1028 + 8;  // m_component_definitions.m_definitions (as AnyLoot reads it)
constexpr ptrdiff_t server_loot_level = 5336;       // m_loot_level (s32)
constexpr ptrdiff_t peer_position = 0x19f8;         // server_scene.peer position (vec3), as AnyHelpers' admin uses it
// inventory_definition
constexpr ptrdiff_t item_id = 0, item_name = 16, item_class = 48;
constexpr ptrdiff_t item_tech_tier = 792, item_spawn_probability = 800, item_loot_group = 808, item_category = 816;
// vehicle_component_definition
constexpr ptrdiff_t part_id = 0, part_name = 16, part_class = 48, part_category = 440;
constexpr ptrdiff_t part_tech_tier = 728, part_spawn_probability = 736, part_loot_group = 744;
// server_scene.item_world
constexpr ptrdiff_t item_world_definition = 136;    // ptr<const inventory_definition>
constexpr ptrdiff_t ammo_count = 144;               // server_scene.item_world.ammo_box.m_ammo_count (property_s32)
constexpr ptrdiff_t property_value = 24;            // replication.master.property_type_simple<s32>._m_value
}  // namespace off

constexpr int kPropertySetSlot = 5;                 // property_type_simple<T>.set in the property's method table
                                                    // (the slot AnyHelpers' property_type_simple<bool>.set binding uses)
constexpr int32_t kStoreSystem = 1;                 // file.e_store.system (static inference; GpsPart uses it)
constexpr int32_t kGiveAmount = 250;

struct Vec3 { double x, y, z; };
struct Mat34 { double v0[3], v1[3], v2[3], t[3]; };   // three basis vectors, then the translation
struct GamePath { int32_t store; uint32_t pad; anymaker::gc_string_view path; };
static_assert(sizeof(Mat34) == 96 && sizeof(GamePath) == 24, "layouts");

using tick_t = void (*)(uint8_t* scene, uint8_t* server);
using get_by_id_t = void (*)(uint8_t** ret, uint8_t* container, const int32_t* id);
using add_definitions_t = void (*)(void* container, const void* file, const void* outfit);
using file_ctor_t = void (*)(void* file);
using file_load_t = void (*)(bool* ret, void* file, const void* path);
using get_definition_t = void (*)(const uint8_t** ret, const void* container, const anymaker::gc_string_view* id);
using create_item_t = void (*)(anymaker::gc_ref_raw<uint8_t>* ret, const uint8_t* definition, uint8_t* scene);
using floor_create_t = void (*)(uint8_t** ret, uint8_t* floor_items, uint8_t* physics, const Mat34* transform, const Vec3* velocity,
                                const anymaker::gc_ref_raw<uint8_t>* item);
using property_set_s32_t = void (*)(uint8_t* property, const int32_t* value);
using string_ctor_t = void (*)(anymaker::gc_string_view*, const char*);

AnyModHostV1 host;
const AnyUiStateV1* ui;
std::atomic<bool> g_stop{false}, g_ready{false};

anymaker::cell_hook h_tick, h_add_definitions;
get_by_id_t peer_by_id;
file_ctor_t file_ctor, file_dtor;
file_load_t file_load;
get_definition_t get_definition;
create_item_t create_item;
floor_create_t floor_create;
string_ctor_t string_ctor;

std::string g_cash_json;                            // absolute path of our definition file (UTF-8); fixed before g_ready
std::filesystem::path g_data_dir;                   // mods/AnyCareer
thread_local bool t_adding = false;
std::atomic<uint64_t> g_last_tick{0};

enum class Job { give_cash, price_list };
struct Work { Job job; double yaw; ULONGLONG posted; };
std::mutex g_mutex;
std::deque<Work> g_queue;   // key presses waiting for the next server tick

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

std::string utf8(const std::filesystem::path& p) {
    auto u = p.u8string();
    return std::string(u.begin(), u.end());
}

bool gameplay() { AnyUiSnapshotV1 s; return ui && ui->copy(&s) && s.kind == ANY_UI_GAMEPLAY; }

// ------------------------------------------------------------------ the Cash definition (start-up, background thread)
// Copies the first ammo_box entry of the game's own item file and rewrites the fields that make it Cash.
bool write_cash_definition() {
    if (!host.game_directory || !host.plugin_directory) return false;
    std::filesystem::path stock = std::filesystem::path(host.game_directory) / L"rom" / L"data" / L"inventory_definitions.json";
    std::ifstream in(stock, std::ios::binary);
    if (!in) { log(2, "ANYCAREER_CASH ok=0 reason=no_stock_item_file path=%s", utf8(stock).c_str()); return false; }
    std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    std::string entry;
    try {
        assetjson::Value root = assetjson::Parser(text).parse();
        for (const auto& def : root.get("definitions").array)
            if (def.kind == 'o' && def.get("class").text == "ammo_box") { entry = text.substr(def.begin, def.end - def.begin); break; }
    } catch (const std::exception&) {
        log(2, "ANYCAREER_CASH ok=0 reason=stock_item_file_unreadable");
        return false;
    }
    if (entry.empty()) { log(2, "ANYCAREER_CASH ok=0 reason=no_ammo_box_in_stock_items"); return false; }
    std::string cash = logic::make_cash_entry(entry);
    if (cash.empty()) { log(2, "ANYCAREER_CASH ok=0 reason=could_not_rewrite_entry"); return false; }
    std::error_code ec;
    std::filesystem::create_directories(g_data_dir, ec);
    std::filesystem::path file = g_data_dir / L"cash_item.json";
    std::ofstream out(file, std::ios::binary | std::ios::trunc);
    out << "{\n  \"definitions\": [\n" << cash << "\n  ]\n}\n";
    out.close();
    if (!out) { log(2, "ANYCAREER_CASH ok=0 reason=could_not_write path=%s", utf8(file).c_str()); return false; }
    g_cash_json = utf8(file);
    return true;
}

// ------------------------------------------------------------------ adding Cash to the game's items (both scenes)
const uint8_t* find_definition(const void* container, const char* id) {
    if (!get_definition || !string_ctor) return nullptr;
    anymaker::gc_string_view s{};
    string_ctor(&s, id);
    const uint8_t* found = nullptr;
    get_definition(&found, container, &s);
    return found;
}

// Loads our JSON into a fresh inventory_definition_file and hands it to the container through the original add.
// The path's characters must outlive the call ($string_ctor_cstr makes a non-owning view), so they are static.
bool add_ours(void* container, const void* outfit, const std::string& text) {
    static std::string kept[4];
    static std::atomic<int> next{0};
    std::string& chars = kept[next++ % 4];
    chars = text;
    GamePath path{kStoreSystem, 0, {}};
    string_ctor(&path.path, chars.c_str());
    alignas(16) uint8_t file[64]{};   // inventory_definition_file is 40 bytes
    file_ctor(file);
    bool ok = false;
    file_load(&ok, file, &path);
    if (ok) {
        t_adding = true;
        reinterpret_cast<add_definitions_t>(h_add_definitions.original)(container, file, outfit);
        t_adding = false;
    }
    file_dtor(file);
    return ok;
}

void hk_add_definitions(void* container, const void* file, const void* outfit) {
    reinterpret_cast<add_definitions_t>(h_add_definitions.original)(container, file, outfit);
    if (!g_ready || g_stop || t_adding || !container || find_definition(container, logic::kCashId)) return;
    std::string forward = g_cash_json;
    for (char& c : forward) if (c == '\\') c = '/';
    bool ok = add_ours(container, outfit, g_cash_json) || add_ours(container, outfit, forward);
    bool found = ok && find_definition(container, logic::kCashId);
    static std::atomic<int> logs{6};
    if (logs.fetch_sub(1) > 0)
        log(found ? 0 : 2, "ANYCAREER_CASH added=%d found_after=%d container=%p file=%s", ok ? 1 : 0, found ? 1 : 0, container, g_cash_json.c_str());
}

// ------------------------------------------------------------------ server thread (host only)
uint8_t* host_peer(uint8_t* scene) {
    uint8_t* peer = nullptr;
    for (int32_t id = 0; id < 64 && !peer; ++id) peer_by_id(&peer, scene + off::server_peers, &id);   // the host joins first
    return peer && anymaker::readable(peer + off::peer_position, 24) ? peer : nullptr;
}

void give_cash(uint8_t* scene, const Work& w) {
    uint8_t* peer = host_peer(scene);
    if (!peer) { log(1, "ANYCAREER_GIVE ok=0 reason=no_host_player"); return; }
    const uint8_t* def = find_definition(scene + off::server_inventory_defs, logic::kCashId);
    if (!def) { log(2, "ANYCAREER_GIVE ok=0 reason=cash_item_not_registered (see ANYCAREER_CASH lines)"); return; }
    anymaker::gc_ref_raw<uint8_t> item{};
    create_item(&item, def, scene);
    if (!item.ptr) { log(2, "ANYCAREER_GIVE ok=0 reason=create_item_null"); return; }
    uint8_t* made = item.ptr;
    const bool is_cash = anymaker::readable(made, off::ammo_count + 32) && rd<const uint8_t*>(made, off::item_world_definition) == def;
    int32_t before = is_cash ? rd<int32_t>(made, off::ammo_count + off::property_value) : -1, after = before;
    if (is_cash && before != kGiveAmount) {
        uint8_t* property = made + off::ammo_count;
        void* set = anymaker::typeinfo_of(property).method(kPropertySetSlot);
        if (route::executable(set)) {
            int32_t want = kGiveAmount;
            reinterpret_cast<property_set_s32_t>(set)(property, &want);
            after = rd<int32_t>(made, off::ammo_count + off::property_value);
        }
    }
    // A metre in front of you (yaw 0 faces +Z, +pi/2 faces +X) and half a metre up, so it drops onto the ground.
    Vec3 p = rd<Vec3>(peer, off::peer_position);
    Mat34 m{{1, 0, 0}, {0, 1, 0}, {0, 0, 1}, {p.x + std::sin(w.yaw), p.y + 0.5, p.z + std::cos(w.yaw)}};
    Vec3 still{0, 0, 0};
    uint8_t* floor_item = nullptr;
    floor_create(&floor_item, scene + off::server_floor_items, scene + off::server_physics, &m, &still, &item);
    // Our reference to the item is not released ($ref_dtor is not bound yet): the floor keeps its own, and this
    // test build makes one item per key press.
    log(floor_item ? 0 : 2, "ANYCAREER_GIVE ok=%d definition_match=%d count_before=%d count_after=%d wanted=%d at=(%.1f, %.1f, %.1f)",
        floor_item ? 1 : 0, is_cash ? 1 : 0, before, after, kGiveAmount, m.t[0], m.t[1], m.t[2]);
}

void price_list(uint8_t* scene) {
    int32_t level = rd<int32_t>(scene, off::server_loot_level);
    std::vector<logic::Entry> rows;
    auto collect = [&](ptrdiff_t vec, bool part) {
        vehicle_reads::for_each_ref(scene + vec, 8192, [&](uint8_t* def) {
            const ptrdiff_t end = part ? off::part_category + 16 : off::item_category + 16;
            if (!anymaker::readable(def, size_t(end))) return true;
            logic::Entry e;
            e.part = part;
            e.id = read_string(def + (part ? off::part_id : off::item_id));
            if (e.id.empty()) return true;
            e.name = read_string(def + (part ? off::part_name : off::item_name));
            e.cls = read_string(def + (part ? off::part_class : off::item_class));
            e.category = read_string(def + (part ? off::part_category : off::item_category));
            e.tech_tier = rd<int32_t>(def, part ? off::part_tech_tier : off::item_tech_tier);
            e.spawn_probability = rd<double>(def, part ? off::part_spawn_probability : off::item_spawn_probability);
            e.loot_group = rd<int32_t>(def, part ? off::part_loot_group : off::item_loot_group);
            rows.push_back(std::move(e));
            return true;
        });
    };
    collect(off::server_inventory_defs, false);
    collect(off::server_component_defs, true);
    std::ostringstream tsv;
    tsv << logic::tsv_header() << "\n";
    int findable = 0, items = 0, parts = 0;
    for (auto& e : rows) {
        e.price = logic::price(e);
        e.findable = logic::findable(e, level);
        findable += e.findable;
        (e.part ? parts : items)++;
        tsv << logic::tsv_row(e) << "\n";
    }
    std::error_code ec;
    std::filesystem::create_directories(g_data_dir, ec);
    std::filesystem::path file = g_data_dir / L"prices.tsv";
    std::ofstream out(file, std::ios::binary | std::ios::trunc);
    out << tsv.str();
    out.close();
    log(out ? 0 : 2, "ANYCAREER_PRICES ok=%d loot_level=%d items=%d parts=%d findable_now=%d file=%s", out ? 1 : 0, level, items, parts,
        findable, utf8(file).c_str());
}

// Kept apart from hk_tick: __try cannot share a function with objects that need unwinding.
void run_guarded(uint8_t* scene, const Work& w) {
    __try {
        if (w.job == Job::give_cash) give_cash(scene, w);
        else price_list(scene);
    } __except (EXCEPTION_EXECUTE_HANDLER) { log(2, "ANYCAREER_%s ok=0 reason=native_exception", w.job == Job::give_cash ? "GIVE" : "PRICES"); }
}

void hk_tick(uint8_t* scene, uint8_t* server) {
    if (scene && !g_stop) {
        g_last_tick = GetTickCount64();
        std::deque<Work> work;
        { std::lock_guard lock(g_mutex); work.swap(g_queue); }
        for (auto& w : work) {
            if (GetTickCount64() - w.posted > 3000) { log(1, "ANYCAREER_JOB ok=0 reason=expired"); continue; }
            run_guarded(scene, w);
        }
    }
    reinterpret_cast<tick_t>(h_tick.original)(scene, server);
}

// ------------------------------------------------------------------ input
uint32_t on_input(const AnyInputV1* e, void*) {
    static bool held[2];   // F9, F10: a consumed press also consumes its release
    if (!e || (e->kind != ANY_KEY_DOWN && e->kind != ANY_KEY_UP) || (e->key != VK_F9 && e->key != VK_F10)) return 0;
    bool& was = held[e->key == VK_F10];
    if (e->kind == ANY_KEY_UP) { bool consume = was; was = false; return consume ? 1 : 0; }
    if (!g_ready || !gameplay()) return 0;
    was = true;
    if (GetTickCount64() - g_last_tick > 2000) { log(1, "ANYCAREER_JOB ok=0 reason=not_hosting (the host runs these)"); return 1; }
    double yaw = 0;
    static AnySessionPlayersV1 players;   // about 21 KB, so not on the input thread's stack
    players = AnySessionPlayersV1{};
    uint32_t flags = 0;
    if (host.copy_players && host.copy_players(&players, &flags))
        for (uint32_t i = 0; i < players.count && i < 64; ++i)
            if ((players.players[i].valid_fields & PLAYER_LOCAL) && (players.players[i].valid_fields & PLAYER_FACING))
                yaw = players.players[i].yaw_radians;
    std::lock_guard lock(g_mutex);
    if (g_queue.size() < 4) g_queue.push_back({e->key == VK_F10 ? Job::give_cash : Job::price_list, yaw, GetTickCount64()});
    return 1;
}

// ------------------------------------------------------------------ install (background thread)
void install() {
    using namespace anymaker::experimental;
    for (int attempt = 0; attempt < 180 && !g_stop; ++attempt) {
        if (attempt) Sleep(1000);
        if (!matching_build()) { if (!attempt) log(1, "ANYCAREER_OFF reason=game_build_mismatch"); return; }
        auto f = [](const anymaker::func_desc& d) { return route::function(d); };
        void* fns[] = {f(bind::peer_container_get_peer_by_id), f(bind::inv_file_ctor), f(bind::inv_file_dtor), f(bind::inv_file_load),
                       f(bind::inv_get_definition_by_id), f(bind::item_world_create_item), f(bind::floor_create_item)};
        void** tick = route::cell(bind::server_scene_tick);
        void** add = route::cell(bind::inv_add_definitions);
        auto sc = reinterpret_cast<string_ctor_t>(native(anymaker::sym::string_ctor_cstr_rva));
        bool all = tick && add && sc;
        std::string found;
        for (void* p : fns) { all = all && p; found += p ? "1" : "0"; }
        // The add cell must hold inventory _add_definitions itself (or another mod's hook on it), and the four
        // definition-file functions reached through the same route must be four different functions.
        bool add_ok = add && anymaker::readable(add, 8) &&
                      (route::code_is(*add, bind::inv_add_definitions.code_sig) || route::in_other_module(*add));
        bool distinct = fns[1] != fns[2] && fns[1] != fns[3] && fns[2] != fns[3] && (!add || (*add != fns[1] && *add != fns[3]));
        if (!all || !add_ok || !distinct) {
            if (attempt == 20 || attempt == 179)
                log(attempt == 179 ? 2 : 1, "ANYCAREER_LOOKUP functions=%s tick=%d add=%d add_ok=%d distinct=%d string=%d", found.c_str(), !!tick,
                    !!add, add_ok ? 1 : 0, distinct ? 1 : 0, !!sc);
            continue;
        }
        peer_by_id = (get_by_id_t)fns[0]; file_ctor = (file_ctor_t)fns[1]; file_dtor = (file_ctor_t)fns[2]; file_load = (file_load_t)fns[3];
        get_definition = (get_definition_t)fns[4]; create_item = (create_item_t)fns[5]; floor_create = (floor_create_t)fns[6];
        string_ctor = sc;
        if (!write_cash_definition()) { log(2, "ANYCAREER_OFF reason=no_cash_definition"); return; }
        if (!h_tick.install(tick, (void*)&hk_tick) || !h_add_definitions.install(add, (void*)&hk_add_definitions)) {
            h_tick.remove((void*)&hk_tick);
            log(2, "ANYCAREER_OFF reason=hook_cell_not_writable");
            return;
        }
        g_ready = true;
        log(0, "ANYCAREER_READY after_s=%d cash_file=%s: load or start a world, then F10 gives 250 Cash (host), F9 writes the price list",
            attempt, g_cash_json.c_str());
        return;
    }
}
}  // namespace
}  // namespace anycareer

using namespace anycareer;
extern "C" __declspec(dllexport) bool AnyAPI_ModInit(const AnyModHostV1* h, AnyModCallbacksV1* out) {
    if (!h || !out || h->struct_size != sizeof(*h) || h->abi != ANYAPI_MOD_ABI || out->struct_size != sizeof(*out)) return false;
    host = *h; out->id = kModId; out->input = on_input;
    if (host.plugin_directory) g_data_dir = std::filesystem::path(host.plugin_directory) / L"AnyCareer";
    out->shutdown = [](void*) { g_stop = true; h_add_definitions.remove((void*)&hk_add_definitions); h_tick.remove((void*)&hk_tick); };
    return true;
}
extern "C" __declspec(dllexport) void AnyAPI_ModReady() {
    auto services = AnyAPI_Services(); if (!services) return;
    ui = static_cast<const AnyUiStateV1*>(services->query("anyapi.ui_state", 1));
    if (!ui || ui->struct_size != sizeof(*ui) || ui->version != 1 || !ui->copy) { ui = nullptr; log(2, "ANYCAREER_OFF reason=no_ui_state"); return; }
    std::thread(install).detach();   // resolving scans game memory; keep it off the loader and frame threads
}
