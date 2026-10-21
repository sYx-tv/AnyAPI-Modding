// AnyCareer TEST BUILD (step 2): Cash plus the first trader, the Mechanic, with buying and selling.
//
//   Cash item: a new inventory item, "Cash", added to the game's item definitions right after the game adds its
//      own (inventory_definition_container._add_definitions, on the server and the client scene). Its definition is
//      made at start-up from the player's own copy of the game's item file: the first ammo box entry is copied and
//      renamed, so it stacks like ammo (class ammo_box: the stack size is the definition's gun_magazine_capacity,
//      9999 here) and fits no gun. No game data ships with the mod. It never spawns as loot (spawn probability 0,
//      loot group none). Every player needs the mod, as for any mod-added item. It looks like a bundle of green bills
//      with a paper band: a flat-colour mesh made at start-up (anycareer_cash_mesh.h) and written beside the game's
//      part meshes; if that fails, Cash keeps the ammo box look.
//   The Mechanic: one trader per world, at the gas station spot nearest to where the host is the first time the
//      world runs with AnyCareer (gas station spots are read from the player's own rom/data/tiles at start-up). The
//      spot is kept in "AnyAPI and Modding/mods/AnyCareer/worlds/<world seed>.txt". Far away, a line at the top of
//      the screen gives the distance and direction; within kTradeRange, J (rebindable in Mod Controls) opens the
//      shop, and AnyMap (when installed) shows him as a pin through its anymap.pins service. Shop rows show the
//      part's picture (AnyAPI item images). Mouse: click a row's Buy or Sell button, a tab or X; the wheel
//      scrolls; right click closes. The keyboard works too. Buy: up to 16 mechanical parts that can drop at the world's loot level, 1-3 of each, picked from
//      the world seed and the restock number; the shelf restocks every kRestockDays in-game days (midnights are
//      counted while the world runs). Sell: mechanical parts and items you carry, for 40% of the trader's price.
//      Everything happens on the host in one server-tick step: buying checks your Cash, takes it from your stacks
//      (a stack's count is set through its own property setter; emptied stacks are removed with
//      server_scene.inventory.destroy_item) and drops the part at your feet; selling removes the item first and
//      then drops the Cash. The shop's stock only exists in the mod, so there is no box to break, steal from or
//      duplicate. Host and solo only in this build.
//   F3 (host): gives the host a stack of 250 Cash, dropped at your feet (test money).
//   F4 (host): writes the price list to "AnyAPI and Modding/mods/AnyCareer/prices.tsv" (stays on this PC: it is
//      game data). The log gets a summary line.
//
// Everything is logged to anymaker_modding.log with an ANYCAREER prefix.
//
// EXPERIMENTAL. Experimental SDK bindings (anycareer_bindings.h): Anymaker 0.1.25 / Steam build 25904830 only.
#define NOMINMAX
#include <windows.h>
#include "anyapi_services_v1.h"
#include "anyapi_gpu_draw_v1.h"
#include "anyapi_session_v1.h"
#include "anyapi_ui_state_v1.h"
#include "anyapi_world_time_v1.h"
#include "mod_controls_v1.h"
#include "anyapi_item_catalog_v1.h"
#include "anyapi_item_images_v1.h"
#include "anymap_pins_v1.h"
#include "anyapi_experimental.hpp"
#include "anycareer_bindings.h"
#include "anycareer_logic.h"
#include "anycareer_cash_mesh.h"
#include "anyapi_asset_json.h"
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
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

namespace anycareer {
namespace {
constexpr const char* kModId = "anycareer";
using vehicle_reads::rd;

// Layouts from the 0.1.25 reference (metadata).
namespace off {
// server_scene
constexpr ptrdiff_t server_world_seed = 72 + 24;    // m_world_seed (property_s32), its value
constexpr ptrdiff_t server_peers = 0xa8;            // m_peers (server_scene.peer.container)
constexpr ptrdiff_t server_floor_items = 280;       // m_floor_items (server_scene.item_floor_container)
constexpr ptrdiff_t server_physics = 2656;          // m_physics (physics.scene)
constexpr ptrdiff_t server_inventory_defs = 3968;   // m_inventory_definitions; its m_definitions vector is at +0
constexpr ptrdiff_t server_component_defs = 0x1028 + 8;  // m_component_definitions.m_definitions (as AnyLoot reads it)
constexpr ptrdiff_t server_loot_level = 5336;       // m_loot_level (s32)
// server_scene.peer
constexpr ptrdiff_t peer_position = 0x19f8;         // position (vec3), as AnyHelpers' admin uses it
constexpr ptrdiff_t peer_actor = 6616;              // m_authority_actor (ptr<server_scene.actor>)
// server_scene.actor_character
constexpr ptrdiff_t actor_inventory = 1920;         // m_inventory (server_scene.inventory, 248 bytes, embedded)
// inventory_definition
constexpr ptrdiff_t item_id = 0, item_name = 16, item_class = 48;
constexpr ptrdiff_t item_tech_tier = 792, item_spawn_probability = 800, item_loot_group = 808, item_category = 816;
// vehicle_component_definition
constexpr ptrdiff_t part_id = 0, part_name = 16, part_class = 48, part_category = 440;
constexpr ptrdiff_t part_is_building = 433;         // m_is_building_component (bool)
constexpr ptrdiff_t part_default_stack = 580;       // m_inventory_default_stack_size (s32; dungeon loot spawns this many)
constexpr ptrdiff_t part_tech_tier = 728, part_spawn_probability = 736, part_loot_group = 744;
// server_scene.item_world
constexpr ptrdiff_t item_world_id = 64 + 24;        // m_item_id (property_s32), its value
constexpr ptrdiff_t item_world_definition = 136;    // ptr<const inventory_definition>
constexpr ptrdiff_t ammo_count = 144;               // server_scene.item_world.ammo_box.m_ammo_count (property_s32)
constexpr ptrdiff_t part_stack_count = 176;         // vehicle_editor_add_component._m_stack_count (property_s32)
constexpr ptrdiff_t part_definition = 208;          // vehicle_editor_add_component.m_component_definition
constexpr ptrdiff_t property_value = 24;            // replication.master.property_type_simple<s32>._m_value
}  // namespace off

constexpr int kPropertySetSlot = 5;                 // property_type_simple<T>.set in the property's method table
                                                    // (the slot AnyHelpers' property_type_simple<bool>.set binding uses)
constexpr int32_t kStoreSystem = 1;                 // file.e_store.system (static inference; GpsPart uses it)
constexpr uint32_t kRefDtorRva = 0x4ed40;           // N:() $ref_dtor (uptr) [metadata]
constexpr int32_t kGiveAmount = 250;
constexpr int32_t kMechanical = 6;                  // e_loot_group mechanical
constexpr double kTradeRange = 30;                  // metres from the gas station spot (the building stands on it)
constexpr int64_t kRestockDays = 2;
constexpr int kShelfSize = 16;                // Nate (2026-10-21): "a wee bit larger" than 10

struct Vec3 { double x, y, z; };
struct Mat34 { double v0[3], v1[3], v2[3], t[3]; };   // three basis vectors, then the translation
struct GamePath { int32_t store; uint32_t pad; anymaker::gc_string_view path; };
static_assert(sizeof(Mat34) == 96 && sizeof(GamePath) == 24, "layouts");
using Ref = anymaker::gc_ref_raw<uint8_t>;

using tick_t = void (*)(uint8_t* scene, uint8_t* server);
using get_by_id_t = void (*)(uint8_t** ret, uint8_t* container, const int32_t* id);
using add_definitions_t = void (*)(void* container, const void* file, const void* outfit);
using file_ctor_t = void (*)(void* file);
using file_load_t = void (*)(bool* ret, void* file, const void* path);
using get_definition_t = void (*)(const uint8_t** ret, const void* container, const anymaker::gc_string_view* id);
using create_item_t = void (*)(Ref* ret, const uint8_t* definition, uint8_t* scene);
using floor_create_t = void (*)(uint8_t** ret, uint8_t* floor_items, uint8_t* physics, const Mat34* transform, const Vec3* velocity,
                                const Ref* item);
using property_set_s32_t = void (*)(uint8_t* property, const int32_t* value);
using string_ctor_t = void (*)(anymaker::gc_string_view*, const char*);
using vector_life_t = void (*)(anymaker::gc_vector_raw*);
using get_items_t = void (*)(uint8_t* inventory, anymaker::gc_vector_raw* out);
using destroy_item_t = void (*)(Ref* ret, uint8_t* inventory, const int32_t* item_id);
using component_set_t = void (*)(uint8_t* item, const uint8_t* component_definition, const int32_t* count);
using ref_dtor_t = void (*)(Ref*);

AnyModHostV1 host;
const AnyUiStateV1* ui;
const AnyGpuDrawV1* gpu;
const AnyWorldTimeV1* world_time;
const AnySessionV1* session;
const ModControlsV1* controls;
const AnyItemCatalogV1* catalog;
const AnyItemImagesV1* images;
const AnyServicesV1* services_api;
uint64_t trade_action;
std::atomic<bool> g_stop{false}, g_ready{false}, g_stations_ready{false};

anymaker::cell_hook h_tick, h_add_definitions;
get_by_id_t peer_by_id;
file_ctor_t file_ctor, file_dtor;
file_load_t file_load;
get_definition_t get_definition;
create_item_t create_item;
floor_create_t floor_create;
string_ctor_t string_ctor;
vector_life_t vector_ctor, vector_dtor;
get_items_t get_items;
destroy_item_t destroy_item;
component_set_t component_set;
ref_dtor_t ref_dtor;

std::string g_cash_json;                            // absolute path of our definition file (UTF-8); fixed before g_ready
std::filesystem::path g_data_dir;                   // mods/AnyCareer
std::vector<logic::Zone> g_stations;                // gas station spots; fixed before g_stations_ready
thread_local bool t_adding = false;
std::atomic<uint64_t> g_last_tick{0};
std::atomic<bool> g_shop_open{false};
std::atomic<uint32_t> g_mode{ANY_MODE_UNKNOWN};

enum class Job { give_cash, price_list, buy, sell };
struct Work { Job job; double yaw; ULONGLONG posted; int32_t key; };   // key: shelf row (buy) or item id (sell)
std::mutex g_mutex;
std::deque<Work> g_queue;   // key presses waiting for the next server tick

// What the shop screen shows, written by the server tick and read by the renderer.
struct Row { std::wstring name; int price = 0, quantity = 0; int32_t key = 0; std::string id; };   // id: part or item definition, for its picture
struct View {
    bool placed = false;
    logic::Zone trader;
    int64_t cash = 0, days = 0, restock_in = 0;
    std::vector<Row> buy, sell;
    std::wstring status;
};
std::mutex g_view_mutex;
View g_view;

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

std::wstring wide(const std::string& s) {
    if (s.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), int(s.size()), nullptr, 0);
    std::wstring w(size_t(std::max(n, 0)), L'\0');
    if (n > 0) MultiByteToWideChar(CP_UTF8, 0, s.data(), int(s.size()), w.data(), n);
    return w;
}

bool gameplay() { AnyUiSnapshotV1 s; return ui && ui->copy(&s) && s.kind == ANY_UI_GAMEPLAY; }
bool career_mode() { uint32_t m = g_mode; return m != ANY_MODE_CREATIVE && m != ANY_MODE_SANDBOX; }   // unknown counts as on

// ------------------------------------------------------------------ the Cash definition (start-up, background thread)
// The money-bundle look, written beside the game's part meshes (as GpsPart does) from the stock compass mesh's
// header. Returns the mesh path for the definition, or "" to keep the ammo box look.
std::string write_cash_mesh(const std::string& entry) {
    std::string stock_mesh;
    try { stock_mesh = assetjson::Parser(entry).parse().get("mesh_file").text; } catch (const std::exception&) {}
    std::filesystem::path components = std::filesystem::path(host.game_directory) / L"rom" / L"meshes" / L"components";
    std::ifstream in(components / L"compass_sensor_a.mesh", std::ios::binary);
    std::vector<uint8_t> stock((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    std::vector<uint8_t> mesh = cashmesh::build(stock, "anycareer_cash_a");
    if (mesh.empty()) { log(1, "ANYCAREER_CASH_LOOK ok=0 reason=template_mesh_not_recognised bytes=%d", int(stock.size())); return {}; }
    std::ofstream out(components / L"anycareer_cash_a.mesh", std::ios::binary | std::ios::trunc);
    out.write(reinterpret_cast<const char*>(mesh.data()), std::streamsize(mesh.size()));
    out.close();
    if (!out) { log(1, "ANYCAREER_CASH_LOOK ok=0 reason=could_not_write"); return {}; }
    log(0, "ANYCAREER_CASH_LOOK ok=1 mesh=meshes/components/anycareer_cash_a.mesh bytes=%d stock_item_mesh=%s", int(mesh.size()), stock_mesh.c_str());
    return "meshes/components/anycareer_cash_a.mesh";
}

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
    std::string mesh = write_cash_mesh(entry);
    const double lo[3] = {-cashmesh::kLength / 2, 0, -cashmesh::kWidth / 2}, hi[3] = {cashmesh::kLength / 2, cashmesh::kHeight, cashmesh::kWidth / 2};
    std::string cash = logic::make_cash_entry(entry, mesh, lo, hi);
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

// Gas station spots from the player's own world tiles (background thread, once).
void load_stations() {
    ULONGLONG start = GetTickCount64();
    std::filesystem::path dir = std::filesystem::path(host.game_directory) / L"rom" / L"data" / L"tiles" / L"0";
    std::vector<logic::Zone> found;
    int files = 0;
    std::error_code ec;
    for (std::filesystem::directory_iterator it(dir, ec), end; !ec && it != end && !g_stop; it.increment(ec)) {
        if (it->path().extension() != L".json") continue;
        ++files;
        std::ifstream in(it->path(), std::ios::binary);
        std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        logic::gas_stations(text, found);
    }
    g_stations = std::move(found);
    g_stations_ready = true;
    log(g_stations.empty() ? 2 : 0, "ANYCAREER_STATIONS found=%d tile_files=%d ms=%llu", int(g_stations.size()), files,
        (unsigned long long)(GetTickCount64() - start));
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

uint8_t* peer_inventory(uint8_t* peer) {
    uint8_t* actor = peer && anymaker::readable(peer + off::peer_actor, 8) ? rd<uint8_t*>(peer, off::peer_actor) : nullptr;
    return actor && anymaker::readable(actor + off::actor_inventory, 248) ? actor + off::actor_inventory : nullptr;
}

bool set_count(uint8_t* item, ptrdiff_t property_offset, int32_t value) {
    uint8_t* property = item + property_offset;
    void* set = anymaker::typeinfo_of(property).method(kPropertySetSlot);
    if (!route::executable(set)) return false;
    reinterpret_cast<property_set_s32_t>(set)(property, &value);
    return rd<int32_t>(item, property_offset + off::property_value) == value;
}

// Drops a made item 0.6 m in front of the player (yaw 0 faces +Z, +pi/2 faces +X), half a metre up. Our own
// reference is kept (not released): the floor holds its own, and this is the recipe proven in step 1.
bool drop(uint8_t* scene, uint8_t* peer, double yaw, const Ref& item) {
    Vec3 p = rd<Vec3>(peer, off::peer_position);
    Mat34 m{{1, 0, 0}, {0, 1, 0}, {0, 0, 1}, {p.x + 0.6 * std::sin(yaw), p.y + 0.5, p.z + 0.6 * std::cos(yaw)}};
    Vec3 still{0, 0, 0};
    uint8_t* floor_item = nullptr;
    floor_create(&floor_item, scene + off::server_floor_items, scene + off::server_physics, &m, &still, &item);
    return floor_item != nullptr;
}

// One Cash stack of `amount` (at most 9999) at the player's feet.
bool drop_cash(uint8_t* scene, uint8_t* peer, double yaw, int32_t amount, int32_t* before = nullptr, int32_t* after = nullptr) {
    const uint8_t* def = find_definition(scene + off::server_inventory_defs, logic::kCashId);
    if (!def) return false;
    Ref item{};
    create_item(&item, def, scene);
    if (!item.ptr || !anymaker::readable(item.ptr, off::ammo_count + 32) || rd<const uint8_t*>(item.ptr, off::item_world_definition) != def) return false;
    int32_t was = rd<int32_t>(item.ptr, off::ammo_count + off::property_value);
    bool ok = was == amount || set_count(item.ptr, off::ammo_count, amount);
    if (before) *before = was;
    if (after) *after = rd<int32_t>(item.ptr, off::ammo_count + off::property_value);
    return ok && drop(scene, peer, yaw, item);
}

void give_cash(uint8_t* scene, const Work& w) {
    uint8_t* peer = host_peer(scene);
    if (!peer) { log(1, "ANYCAREER_GIVE ok=0 reason=no_host_player"); return; }
    if (!find_definition(scene + off::server_inventory_defs, logic::kCashId)) {
        log(2, "ANYCAREER_GIVE ok=0 reason=cash_item_not_registered (see ANYCAREER_CASH lines)");
        return;
    }
    int32_t before = -1, after = -1;
    bool ok = drop_cash(scene, peer, w.yaw, kGiveAmount, &before, &after);
    log(ok ? 0 : 2, "ANYCAREER_GIVE ok=%d count_before=%d count_after=%d wanted=%d", ok ? 1 : 0, before, after, kGiveAmount);
}

logic::Entry entry_of(const uint8_t* def, bool part) {
    logic::Entry e;
    const ptrdiff_t end = part ? off::part_category + 16 : off::item_category + 16;
    if (!anymaker::readable(def, size_t(end))) return e;
    e.part = part;
    e.id = read_string(def + (part ? off::part_id : off::item_id));
    e.name = read_string(def + (part ? off::part_name : off::item_name));
    e.cls = read_string(def + (part ? off::part_class : off::item_class));
    e.category = read_string(def + (part ? off::part_category : off::item_category));
    e.tech_tier = rd<int32_t>(def, part ? off::part_tech_tier : off::item_tech_tier);
    e.spawn_probability = rd<double>(def, part ? off::part_spawn_probability : off::item_spawn_probability);
    e.loot_group = rd<int32_t>(def, part ? off::part_loot_group : off::item_loot_group);
    e.price = logic::price(e);
    return e;
}

void price_list(uint8_t* scene) {
    int32_t level = rd<int32_t>(scene, off::server_loot_level);
    std::vector<logic::Entry> rows;
    auto collect = [&](ptrdiff_t vec, bool part) {
        vehicle_reads::for_each_ref(scene + vec, 8192, [&](uint8_t* def) {
            logic::Entry e = entry_of(def, part);
            if (!e.id.empty()) rows.push_back(std::move(e));
            return true;
        });
    };
    collect(off::server_inventory_defs, false);
    collect(off::server_component_defs, true);
    std::ostringstream tsv;
    tsv << logic::tsv_header() << "\n";
    int findable = 0, items = 0, parts = 0;
    for (auto& e : rows) {
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

// ------------------------------------------------------------------ the Mechanic (server thread)
struct Candidate { const uint8_t* def; std::string id, name; int price; int32_t stack; };
struct Carried { uint8_t* item; int32_t id; const uint8_t* def; };

struct Trader {
    uint8_t* scene = nullptr;
    int32_t seed = 0;
    bool loaded = false;
    logic::WorldState world;
    logic::DayClock clock;
    std::vector<Candidate> candidates;
    int32_t level = -1;
    int64_t restock = -1;
    std::vector<logic::StockRow> shelf;
    const uint8_t* cash_def = nullptr;
    const uint8_t* part_item_def = nullptr;       // "vehicle_editor_add_component"
    uint32_t ticks = 0;
    std::wstring status;
};
Trader T;
Carried g_carried[1024];                          // server thread only; too big for its stack

std::filesystem::path world_file() { return g_data_dir / L"worlds" / (std::to_wstring(T.seed) + L".txt"); }

void save_world() {
    std::error_code ec;
    std::filesystem::create_directories(world_file().parent_path(), ec);
    std::ofstream out(world_file(), std::ios::binary | std::ios::trunc);
    out << logic::save_world(T.world);
}

// Every item the player carries (hands, worn slots, and inside them), through the game's own get_items.
int carried(uint8_t* inventory) {
    anymaker::gc_vector_raw v{};
    vector_ctor(&v);
    get_items(inventory, &v);
    int n = 0;
    if (v.element_size == 8 && v.count >= 0 && v.count <= 4096 && v.capacity >= v.count &&
        (!v.count || anymaker::readable(v.buffer, size_t(v.capacity) * 8)))
        for (int32_t i = 0; i < v.count && n < 1024; ++i) {
            uint8_t** slot = v.at<uint8_t*>(i);
            uint8_t* item = slot ? *slot : nullptr;
            if (!item || !anymaker::readable(item, off::part_definition + 8)) continue;
            g_carried[n++] = {item, rd<int32_t>(item, off::item_world_id), rd<const uint8_t*>(item, off::item_world_definition)};
        }
    vector_dtor(&v);
    return n;
}

// Mechanical parts that can drop at this loot level, sorted by id so every PC builds the same list.
void build_candidates(uint8_t* scene) {
    T.candidates.clear();
    vehicle_reads::for_each_ref(scene + off::server_component_defs, 8192, [&](uint8_t* def) {
        if (!anymaker::readable(def, off::part_spawn_probability + 16)) return true;
        logic::Entry e = entry_of(def, true);
        if (e.id.empty() || e.loot_group != kMechanical || rd<bool>(def, off::part_is_building) || !logic::findable(e, std::max(T.level, 1)))
            return true;
        T.candidates.push_back({def, e.id, e.name.empty() ? e.id : e.name, e.price, std::max(1, rd<int32_t>(def, off::part_default_stack))});
        return true;
    });
    std::sort(T.candidates.begin(), T.candidates.end(), [](const Candidate& a, const Candidate& b) { return a.id < b.id; });
}

// What one carried item is worth to the Mechanic (0: he doesn't buy it). Parts in a stack are priced per part.
int sell_value(const Carried& c, std::string* name, int32_t* count, std::string* id = nullptr) {
    if (!c.def || c.def == T.cash_def) return 0;
    if (c.def == T.part_item_def) {
        const uint8_t* part = rd<const uint8_t*>(c.item, off::part_definition);
        if (!part || !anymaker::readable(part, off::part_spawn_probability + 16)) return 0;
        logic::Entry e = entry_of(part, true);
        if (e.loot_group != kMechanical) return 0;
        int32_t n = std::max(1, rd<int32_t>(c.item, off::part_stack_count + off::property_value));
        int32_t per_stack = std::max(1, rd<int32_t>(part, off::part_default_stack));
        if (name) *name = e.name.empty() ? e.id : e.name;
        if (count) *count = n;
        if (id) *id = e.id;
        return std::max(1, int(int64_t(logic::sell_price(e.price)) * n / per_stack));
    }
    logic::Entry e = entry_of(c.def, false);
    if (e.loot_group != kMechanical) return 0;
    if (name) *name = e.name.empty() ? e.id : e.name;
    if (count) *count = 1;
    if (id) *id = e.id;
    return logic::sell_price(e.price);
}

bool near_trader(uint8_t* peer) {
    if (!T.world.placed) return false;
    Vec3 p = rd<Vec3>(peer, off::peer_position);
    double dx = p.x - T.world.trader.x, dz = p.z - T.world.trader.z;
    return dx * dx + dz * dz <= (kTradeRange + 10) * (kTradeRange + 10);
}

void buy(uint8_t* scene, const Work& w) {
    uint8_t* peer = host_peer(scene);
    uint8_t* inventory = peer_inventory(peer);
    auto fail = [&](const char* why, const wchar_t* say) { T.status = say; log(1, "ANYCAREER_BUY ok=0 reason=%s row=%d", why, w.key); };
    if (!inventory) return fail("no_host_player", L"You need to be alive to trade.");
    if (!near_trader(peer)) return fail("too_far", L"Walk back to the Mechanic to trade.");
    if (w.key < 0 || w.key >= int(T.shelf.size()) || T.shelf[size_t(w.key)].candidate >= int(T.candidates.size()))
        return fail("no_such_row", L"That shelf changed. Pick again.");
    const auto& row = T.shelf[size_t(w.key)];
    const Candidate& c = T.candidates[size_t(row.candidate)];
    if (row.quantity - T.world.sold[size_t(w.key)] <= 0) return fail("sold_out", L"Sold out until the next restock.");
    int n = carried(inventory);
    std::vector<logic::Stack> stacks;
    for (int i = 0; i < n; ++i)
        if (g_carried[i].def == T.cash_def) stacks.push_back({g_carried[i].id, rd<int32_t>(g_carried[i].item, off::ammo_count + off::property_value)});
    std::vector<logic::Payment> plan;
    if (!logic::plan_payment(stacks, c.price, plan)) return fail("not_enough_cash", L"Not enough Cash on you.");
    // Make the part first, so a failure costs nothing.
    Ref item{};
    create_item(&item, T.part_item_def, scene);
    if (!item.ptr || rd<const uint8_t*>(item.ptr, off::item_world_definition) != T.part_item_def) return fail("create_item_failed", L"The trade failed (see the log).");
    int32_t count = c.stack;
    component_set(item.ptr, c.def, &count);
    if (rd<const uint8_t*>(item.ptr, off::part_definition) != c.def) return fail("part_not_set", L"The trade failed (see the log).");
    int paid = 0, removed = 0;
    for (const auto& p : plan) {
        const Carried* stack = nullptr;
        for (int i = 0; i < n; ++i) if (g_carried[i].id == p.item_id) stack = &g_carried[i];
        if (!stack) continue;
        int32_t had = rd<int32_t>(stack->item, off::ammo_count + off::property_value);
        if (p.new_count == 0) {
            Ref gone{};
            destroy_item(&gone, inventory, &p.item_id);
            if (gone.ptr) { ++removed; paid += had; ref_dtor(&gone); }
        } else if (set_count(stack->item, off::ammo_count, p.new_count)) {
            paid += had - p.new_count;
        }
    }
    bool dropped = drop(scene, peer, w.yaw, item);
    ++T.world.sold[size_t(w.key)];
    save_world();
    T.status = L"Bought " + wide(c.name) + L" for " + std::to_wstring(c.price) + L" Cash.";
    bool ok = paid == c.price && dropped;
    log(ok ? 0 : 2, "ANYCAREER_BUY ok=%d part=%s stack=%d price=%d paid=%d stacks_removed=%d dropped=%d", ok ? 1 : 0, c.id.c_str(),
        rd<int32_t>(item.ptr, off::part_stack_count + off::property_value), c.price, paid, removed, dropped ? 1 : 0);
}

void sell(uint8_t* scene, const Work& w) {
    uint8_t* peer = host_peer(scene);
    uint8_t* inventory = peer_inventory(peer);
    auto fail = [&](const char* why, const wchar_t* say) { T.status = say; log(1, "ANYCAREER_SELL ok=0 reason=%s item=%d", why, w.key); };
    if (!inventory) return fail("no_host_player", L"You need to be alive to trade.");
    if (!near_trader(peer)) return fail("too_far", L"Walk back to the Mechanic to trade.");
    int n = carried(inventory);
    const Carried* found = nullptr;
    for (int i = 0; i < n; ++i) if (g_carried[i].id == w.key) found = &g_carried[i];
    std::string name;
    int value = found ? sell_value(*found, &name, nullptr) : 0;
    if (!value) return fail("not_carried_or_not_wanted", L"You don't have that any more.");
    // Take the item first, then pay: nothing can be sold twice.
    Ref gone{};
    destroy_item(&gone, inventory, &w.key);
    if (!gone.ptr) return fail("remove_failed", L"The trade failed (see the log).");
    ref_dtor(&gone);
    int paid = 0;
    for (int32_t stack : logic::cash_stacks(value)) if (drop_cash(scene, peer, w.yaw, stack)) paid += stack;
    T.status = L"Sold " + wide(name) + L" for " + std::to_wstring(value) + L" Cash.";
    log(paid == value ? 0 : 2, "ANYCAREER_SELL ok=%d item=%s value=%d paid=%d", paid == value ? 1 : 0, name.c_str(), value, paid);
}

void publish_view(uint8_t* scene) {
    View v;
    v.placed = T.world.placed;
    v.trader = T.world.trader;
    v.days = T.world.days;
    v.restock_in = kRestockDays - T.world.days % kRestockDays;
    v.status = T.status;
    for (size_t i = 0; i < T.shelf.size() && i < T.world.sold.size(); ++i) {
        if (T.shelf[i].candidate >= int(T.candidates.size())) continue;
        const Candidate& c = T.candidates[size_t(T.shelf[i].candidate)];
        std::wstring name = wide(c.name);
        if (c.stack > 1) name += L" x" + std::to_wstring(c.stack);
        v.buy.push_back({name, c.price, std::max(0, T.shelf[i].quantity - T.world.sold[i]), int32_t(i), c.id});
    }
    uint8_t* inventory = g_shop_open && T.cash_def ? peer_inventory(host_peer(scene)) : nullptr;
    if (inventory) {
        int n = carried(inventory);
        for (int i = 0; i < n; ++i) {
            if (g_carried[i].def == T.cash_def) { v.cash += rd<int32_t>(g_carried[i].item, off::ammo_count + off::property_value); continue; }
            std::string name, id;
            int32_t count = 1;
            int value = sell_value(g_carried[i], &name, &count, &id);
            if (value) v.sell.push_back({wide(name) + (count > 1 ? L" x" + std::to_wstring(count) : L""), value, count, g_carried[i].id, id});
        }
    }
    std::lock_guard lock(g_view_mutex);
    g_view = std::move(v);
}

// Once per server tick, before the game's own: world state, days, the shelf.
void trader_tick(uint8_t* scene) {
    if (scene != T.scene) { T = Trader{}; T.scene = scene; }
    if (!career_mode()) return;
    uint8_t* peer = host_peer(scene);
    if (!peer) return;
    if (!T.loaded) {
        T.seed = rd<int32_t>(scene, off::server_world_seed);
        std::ifstream in(world_file(), std::ios::binary);
        std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        logic::WorldState w;
        if (!text.empty() && logic::load_world(text, w)) T.world = w;
        T.clock.days = T.world.days;
        T.cash_def = find_definition(scene + off::server_inventory_defs, logic::kCashId);
        T.part_item_def = find_definition(scene + off::server_inventory_defs, "vehicle_editor_add_component");
        T.loaded = true;
        log(T.cash_def && T.part_item_def ? 0 : 2, "ANYCAREER_WORLD seed=%d saved=%d mechanic_placed=%d days=%lld cash_item=%d part_item=%d", T.seed,
            text.empty() ? 0 : 1, T.world.placed ? 1 : 0, (long long)T.world.days, T.cash_def ? 1 : 0, T.part_item_def ? 1 : 0);
    }
    if (!T.cash_def || !T.part_item_def) return;
    bool dirty = false;
    if (!T.world.placed && g_stations_ready) {
        Vec3 p = rd<Vec3>(peer, off::peer_position);
        int i = logic::nearest(g_stations, p.x, p.z);
        if (i >= 0) {
            T.world.placed = true;
            T.world.trader = g_stations[size_t(i)];
            dirty = true;
            log(0, "ANYCAREER_MECHANIC placed_at=(%.0f, %.0f, %.0f) from_host=(%.0f, %.0f) distance_m=%.0f", T.world.trader.x, T.world.trader.y,
                T.world.trader.z, p.x, p.z, std::hypot(T.world.trader.x - p.x, T.world.trader.z - p.z));
        }
    }
    AnyWorldTimeSnapshotV1 time;
    if (world_time && world_time->copy(&time) && T.clock.update(time.cycle_factor)) {
        T.world.days = T.clock.days;
        dirty = true;
        log(0, "ANYCAREER_DAY days=%lld", (long long)T.world.days);
    }
    int32_t level = rd<int32_t>(scene, off::server_loot_level);
    int64_t restock = T.world.days / kRestockDays;
    if (level != T.level || restock != T.restock || T.candidates.empty()) {
        bool new_shelf = restock != T.world.restock || (T.level >= 0 && level != T.level);
        T.level = level;
        T.restock = restock;
        build_candidates(scene);
        T.shelf = logic::pick_stock(T.candidates.size(), uint64_t(uint32_t(T.seed)), restock, kShelfSize);
        if (new_shelf) { T.world.restock = restock; T.world.sold.assign(T.shelf.size(), 0); dirty = true; }
        static int stock_logs = 0;
        if (stock_logs++ < 20 || new_shelf)
            log(0, "ANYCAREER_STOCK loot_level=%d candidates=%d shelf=%d restock=%lld", level, int(T.candidates.size()), int(T.shelf.size()),
                (long long)restock);
    }
    if (T.world.sold.size() != T.shelf.size()) { T.world.sold.resize(T.shelf.size(), 0); dirty = true; }
    if (dirty) save_world();
}

// Kept apart from objects that need unwinding: __try cannot share a function with them.
void run_trader(uint8_t* scene) {
    __try { trader_tick(scene); } __except (EXCEPTION_EXECUTE_HANDLER) { log(2, "ANYCAREER_TRADER ok=0 reason=native_exception"); }
}
void run_publish(uint8_t* scene) {
    __try { publish_view(scene); } __except (EXCEPTION_EXECUTE_HANDLER) { log(2, "ANYCAREER_VIEW ok=0 reason=native_exception"); }
}
void run_job(uint8_t* scene, const Work& w) {
    __try {
        switch (w.job) {
        case Job::give_cash: give_cash(scene, w); break;
        case Job::price_list: price_list(scene); break;
        case Job::buy: buy(scene, w); break;
        case Job::sell: sell(scene, w); break;
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) { log(2, "ANYCAREER_JOB ok=0 job=%d reason=native_exception", int(w.job)); }
}

void hk_tick(uint8_t* scene, uint8_t* server) {
    if (scene && !g_stop) {
        g_last_tick = GetTickCount64();
        run_trader(scene);
        std::deque<Work> work;
        { std::lock_guard lock(g_mutex); work.swap(g_queue); }
        for (auto& w : work) {
            if (GetTickCount64() - w.posted > 3000) { log(1, "ANYCAREER_JOB ok=0 reason=expired"); continue; }
            run_job(scene, w);
        }
        // The shop screen refreshes a few times a second while open (and right after a trade), rarely otherwise.
        if (!work.empty() || ++T.ticks % (g_shop_open ? 10u : 120u) == 0) run_publish(scene);
    }
    reinterpret_cast<tick_t>(h_tick.original)(scene, server);
}

// ------------------------------------------------------------------ screen (render thread)
struct Local { bool ok = false; double x = 0, z = 0, yaw = 0; };
std::mutex g_ui_mutex;                            // the screen state below, shared by render and input
bool g_open = false;
int g_tab = 0, g_cursor = 0;
Local g_local;
ULONGLONG g_local_at = 0;
// Mouse: the pointer in frame pixels, and the clickable areas the last shop frame drew.
enum class Hit { tab, row, action, close };
struct Area { float x, y, w, h; Hit kind; int index; bool contains(float px, float py) const { return px >= x && py >= y && px < x + w && py < y + h; } };
std::vector<Area> g_areas;
float g_mouse_x = 0, g_mouse_y = 0, g_frame_w = 1920, g_frame_h = 1080;
int g_first = 0;                                  // first list row on screen

Local local_player() {
    static AnySessionPlayersV1 players;   // about 21 KB, so not on the stack; only called under g_ui_mutex
    players = AnySessionPlayersV1{};
    uint32_t flags = 0;
    Local l;
    if (host.copy_players && host.copy_players(&players, &flags))
        for (uint32_t i = 0; i < players.count && i < 64; ++i) {
            const auto& p = players.players[i];
            if ((p.valid_fields & PLAYER_LOCAL) && (p.valid_fields & PLAYER_POSITION)) {
                l.ok = true; l.x = p.position[0]; l.z = p.position[2];
                if (p.valid_fields & PLAYER_FACING) l.yaw = p.yaw_radians;
            }
        }
    return l;
}

bool hosting() { return GetTickCount64() - g_last_tick < 2000; }

bool in_range(const View& v, const Local& l) {
    double dx = l.x - v.trader.x, dz = l.z - v.trader.z;
    return v.placed && l.ok && dx * dx + dz * dz <= kTradeRange * kTradeRange;
}

void close_shop() { g_open = false; g_shop_open = false; g_areas.clear(); host.capture_input(0); }

// Window-client pixels to renderer pixels (they differ at non-native resolutions), as AnyQuickWheel does.
void mouse_to_frame(int32_t cx, int32_t cy) {
    RECT r{};
    HWND window = GetForegroundWindow();
    DWORD pid = 0;
    if (window) GetWindowThreadProcessId(window, &pid);
    if (pid == GetCurrentProcessId() && GetClientRect(window, &r) && r.right > 0 && r.bottom > 0) {
        g_mouse_x = float(cx) * g_frame_w / float(r.right);
        g_mouse_y = float(cy) * g_frame_h / float(r.bottom);
    } else { g_mouse_x = float(cx); g_mouse_y = float(cy); }
}

// Puts the Windows pointer in the middle of the game window, so the shop's pointer starts there.
void centre_pointer() {
    RECT r{};
    POINT p{};
    HWND window = GetForegroundWindow();
    DWORD pid = 0;
    if (window) GetWindowThreadProcessId(window, &pid);
    if (pid == GetCurrentProcessId() && GetClientRect(window, &r)) {
        p.x = r.right / 2; p.y = r.bottom / 2;
        if (ClientToScreen(window, &p)) SetCursorPos(p.x, p.y);
    }
    g_mouse_x = g_frame_w / 2; g_mouse_y = g_frame_h / 2;
}

void box(uint32_t kind, float x, float y, float w, float h, uint32_t color, float radius = 0) {
    AnyGpuCommandV1 c;
    c.kind = kind; c.rect[0] = x; c.rect[1] = y; c.rect[2] = w; c.rect[3] = h; c.color = color; c.radius = radius;
    gpu->emit(&c);
}
void text(float x, float y, float w, float h, const std::wstring& s, float size, uint32_t color, uint32_t flags = ANY_GPU_NOWRAP | ANY_GPU_VCENTER) {
    if (s.empty()) return;
    AnyGpuCommandV1 c;
    c.kind = ANY_GPU_TEXT; c.rect[0] = x; c.rect[1] = y; c.rect[2] = w; c.rect[3] = h; c.font_size = size; c.color = color; c.flags = flags;
    c.text = s.c_str(); c.text_length = uint32_t(s.size());
    gpu->emit(&c);
}

std::wstring key_name() {
    char name[32]{};
    if (controls && trade_action && controls->key_name && controls->key_name(trade_action, name, sizeof name) && name[0]) return wide(name);
    return L"J";
}

// Part and item pictures from AnyAPI's item images (128 x 128, rendered from the game's own meshes), uploaded as
// GPU textures once each. Ids are looked up in the item catalog, which grows when mods add definitions.
std::unordered_map<std::string, uint32_t> g_catalog_index;
uint32_t g_catalog_seen = 0;
std::unordered_map<std::string, uint64_t> g_pictures;   // definition id -> texture id (0: none)
constexpr uint64_t kTextureBase = 0x4361720000000000ull; // "Car" + index: keeps clear of other mods' texture ids

uint64_t picture(const std::string& id, int* budget) {
    if (!catalog || !images || !gpu->texture || id.empty()) return 0;
    auto known = g_pictures.find(id);
    if (known != g_pictures.end()) return known->second;
    if (*budget <= 0) return 0;   // a few new pictures per frame, so opening the shop never stalls
    --*budget;
    uint32_t count = catalog->count();
    if (count != g_catalog_seen) {
        static AnyItemDefinitionV1 d;   // about 2.6 KB; render thread only
        for (uint32_t i = g_catalog_seen; i < count && i < 20000; ++i) { d = AnyItemDefinitionV1{}; if (catalog->copy(i, &d)) g_catalog_index.emplace(d.id, i); }
        g_catalog_seen = count;
    }
    uint64_t texture = 0;
    auto index = g_catalog_index.find(id);
    if (index != g_catalog_index.end()) {
        static std::vector<uint8_t> bgra(65536);
        uint32_t required = 0;
        if (images->copy(index->second, bgra.data(), uint32_t(bgra.size()), &required) && required == bgra.size() &&
            gpu->texture(kTextureBase + index->second, 128, 128, 512, bgra.data(), 1))
            texture = kTextureBase + index->second;
    }
    g_pictures[id] = texture;
    static int logs = 0;
    if (logs++ < 6) log(texture ? 0 : 1, "ANYCAREER_PICTURE id=%s in_catalog=%d picture=%d catalog=%u", id.c_str(), index != g_catalog_index.end() ? 1 : 0, texture ? 1 : 0, count);
    return texture;
}

// The Mechanic's pin on AnyMap (when AnyMap is installed; its service may appear after ours, so it is looked up late).
const AnyMapPinsV1* g_pins;
bool g_pin_shown = false;
logic::Zone g_pin_at;
ULONGLONG g_pins_looked = 0;
void update_pin(const View* v) {
    ULONGLONG now = GetTickCount64();
    if (!g_pins && services_api && now - g_pins_looked > 3000) {
        g_pins_looked = now;
        auto p = static_cast<const AnyMapPinsV1*>(services_api->query("anymap.pins", 1));
        if (p && p->struct_size == sizeof(*p) && p->version == 1 && p->set && p->remove) { g_pins = p; log(0, "ANYCAREER_MAP pins=1"); }
    }
    if (!g_pins) return;
    if (!v || !v->placed) {
        if (g_pin_shown) { g_pins->remove(kModId, "mechanic"); g_pin_shown = false; }
        return;
    }
    if (g_pin_shown && g_pin_at.x == v->trader.x && g_pin_at.z == v->trader.z) return;
    AnyMapPinV1 pin;
    pin.x = v->trader.x; pin.z = v->trader.z; pin.color = 0xffe8c66a;
    strcpy_s(pin.owner, kModId); strcpy_s(pin.id, "mechanic"); strcpy_s(pin.label, "Mechanic (trader)");
    g_pin_shown = g_pins->set(&pin);
    g_pin_at = v->trader;
    log(g_pin_shown ? 0 : 1, "ANYCAREER_MAP pin_set=%d at=(%.0f, %.0f)", g_pin_shown ? 1 : 0, pin.x, pin.z);
}

void draw_shop(const AnyFrameV1* f, const View& v) {
    float w = std::min(640.f, float(f->width) - 32), h = std::min(600.f, float(f->height) - 32);
    float x = (float(f->width) - w) / 2, y = (float(f->height) - h) / 2;
    g_areas.clear();
    box(ANY_GPU_ROUND_RECT, x, y, w, h, 0xf0181b20, 8);
    {   // close button
        float cx = x + w - 44, cy = y + 14;
        bool hover = Area{cx, cy, 30, 30, Hit::close, 0}.contains(g_mouse_x, g_mouse_y);
        box(ANY_GPU_ROUND_RECT, cx, cy, 30, 30, hover ? 0xffa54040 : 0xff2a2e35, 5);
        text(cx, cy, 30, 30, L"X", 15, 0xfff5f7fa, ANY_GPU_NOWRAP | ANY_GPU_VCENTER | ANY_GPU_CENTER | ANY_GPU_BOLD);
        g_areas.push_back({cx, cy, 30, 30, Hit::close, 0});
    }
    text(x + 20, y + 14, w - 220, 30, L"Mechanic", 22, 0xfff5f7fa, ANY_GPU_NOWRAP | ANY_GPU_VCENTER | ANY_GPU_BOLD);
    text(x + w - 230, y + 14, 180, 30, L"Cash: " + std::to_wstring(v.cash), 18, 0xffe8c66a);
    const wchar_t* tabs[] = {L"Buy", L"Sell"};
    for (int i = 0; i < 2; ++i) {
        float tx = x + 20 + i * 110;
        bool hover = Area{tx, y + 56, 100, 30, Hit::tab, i}.contains(g_mouse_x, g_mouse_y);
        box(ANY_GPU_ROUND_RECT, tx, y + 56, 100, 30, g_tab == i ? 0xff3a6ea5 : hover ? 0xff3a3f48 : 0xff2a2e35, 5);
        g_areas.push_back({tx, y + 56, 100, 30, Hit::tab, i});
        text(tx, y + 56, 100, 30, tabs[i], 16, 0xfff5f7fa, ANY_GPU_NOWRAP | ANY_GPU_VCENTER | ANY_GPU_CENTER);
    }
    const auto& rows = g_tab == 0 ? v.buy : v.sell;
    float list_y = y + 100, row_h = 44;
    int budget = 3;
    int visible = std::max(1, int((h - 100 - 90) / row_h));
    g_cursor = rows.empty() ? 0 : std::clamp(g_cursor, 0, int(rows.size()) - 1);
    if (g_cursor < g_first) g_first = g_cursor;
    if (g_cursor >= g_first + visible) g_first = g_cursor - visible + 1;
    g_first = std::clamp(g_first, 0, std::max(0, int(rows.size()) - visible));
    int first = g_first;
    if (rows.empty())
        text(x + 20, list_y, w - 40, row_h, g_tab == 0 ? L"Nothing on the shelf right now." : L"You carry nothing the Mechanic buys (mechanical parts and items).",
             15, 0xffaeb7c2);
    for (int i = first; i < int(rows.size()) && i < first + visible; ++i) {
        float ry = list_y + float(i - first) * row_h;
        if (i == g_cursor) box(ANY_GPU_ROUND_RECT, x + 14, ry, w - 28, row_h - 2, 0xff2f4f73, 4);
        g_areas.push_back({x + 14, ry, w - 160, row_h - 2, Hit::row, i});
        const Row& r = rows[size_t(i)];
        bool out = g_tab == 0 && r.quantity <= 0;
        if (uint64_t texture = picture(r.id, &budget)) {
            AnyGpuCommandV1 c;
            c.kind = ANY_GPU_IMAGE; c.texture = texture; c.rect[0] = x + 20; c.rect[1] = ry + 2; c.rect[2] = c.rect[3] = row_h - 6;
            c.source[2] = c.source[3] = 128; c.opacity = out ? 0.4f : 1;
            gpu->emit(&c);
        }
        text(x + 24 + row_h, ry, w - 260 - row_h, row_h, r.name, 15, out ? 0xff7d848c : 0xfff5f7fa);
        if (g_tab == 0) text(x + w - 220, ry, 80, row_h, out ? L"sold out" : std::to_wstring(r.quantity) + L" left", 13, 0xffaeb7c2);
        // The button that buys or sells this row, with the price on it.
        Area button{x + w - 140, ry + 2, 122, row_h - 6, Hit::action, i};
        bool hover = !out && button.contains(g_mouse_x, g_mouse_y);
        box(ANY_GPU_ROUND_RECT, button.x, button.y, button.w, button.h, out ? 0xff2a2e35 : hover ? 0xff4f8a3c : 0xff3b6b2e, 4);
        text(button.x, button.y, button.w, button.h, (g_tab == 0 ? L"Buy  " : L"Sell  ") + std::to_wstring(r.price), 14,
             out ? 0xff7d848c : 0xfffff3c4, ANY_GPU_NOWRAP | ANY_GPU_VCENTER | ANY_GPU_CENTER);
        if (!out) g_areas.push_back(button);
    }
    std::wstring restock = L"Restock in " + std::to_wstring(v.restock_in) + (v.restock_in == 1 ? L" day" : L" days") + L" (day " +
                           std::to_wstring(v.days) + L" of this world)";
    text(x + 20, y + h - 84, w - 40, 22, v.status, 15, 0xff9fd39a);
    text(x + 20, y + h - 58, w - 40, 20, restock, 13, 0xffaeb7c2);
    text(x + 20, y + h - 34, w - 40, 20, L"Click Buy or Sell (or Up/Down and Enter)   Wheel scrolls   Esc closes", 13, 0xffaeb7c2);
    // The game hides the Windows pointer in play, so the shop draws its own.
    AnyGpuPointV1 arrow[3] = {{g_mouse_x, g_mouse_y}, {g_mouse_x, g_mouse_y + 18}, {g_mouse_x + 12, g_mouse_y + 13}};
    AnyGpuCommandV1 c;
    c.kind = ANY_GPU_POLYGON; c.points = arrow; c.point_count = 3; c.color = 0xfff5f7fa;
    gpu->emit(&c);
    c.flags = ANY_GPU_STROKE; c.color = 0xff101214; c.stroke = 1.5f;
    gpu->emit(&c);
}

void draw(const AnyFrameV1* f, void*) {
    if (!f || !gpu) return;
    if (session) { AnySessionStateV1 s; if (session->copy(&s)) g_mode = s.mode; }
    std::lock_guard lock(g_ui_mutex);
    g_frame_w = float(f->width); g_frame_h = float(f->height);
    bool live = g_ready && f->focused && gameplay() && hosting() && career_mode();
    if (!live) { if (g_open) close_shop(); if (!hosting()) update_pin(nullptr); return; }
    ULONGLONG now = GetTickCount64();
    if (now - g_local_at > 100) { g_local = local_player(); g_local_at = now; }
    View v;
    { std::lock_guard view_lock(g_view_mutex); v = g_view; }
    if (g_open && !in_range(v, g_local)) close_shop();
    update_pin(&v);
    if (g_open) { host.capture_input(1); draw_shop(f, v); return; }
    if (!v.placed || !g_local.ok) return;
    float w = 460, x = (float(f->width) - w) / 2;
    if (in_range(v, g_local)) {
        float y = float(f->height) * 0.62f;
        box(ANY_GPU_ROUND_RECT, x, y, w, 34, 0xd0181b20, 6);
        text(x, y, w, 34, L"Press " + key_name() + L" to trade with the Mechanic", 16, 0xfff5f7fa, ANY_GPU_NOWRAP | ANY_GPU_VCENTER | ANY_GPU_CENTER);
    } else {
        std::wstring hint = L"Mechanic: " + wide(logic::direction_text(g_local.x, g_local.z, g_local.yaw, v.trader.x, v.trader.z));
        text(x, 8, w, 22, hint, 14, 0xffd8dde3, ANY_GPU_NOWRAP | ANY_GPU_VCENTER | ANY_GPU_CENTER);
    }
}

// ------------------------------------------------------------------ input
void post(Job job, int32_t key) {
    std::lock_guard lock(g_mutex);
    if (g_queue.size() < 4) g_queue.push_back({job, g_local.yaw, GetTickCount64(), key});
}

uint32_t on_input(const AnyInputV1* e, void*) {
    if (!e) return 0;
    std::lock_guard lock(g_ui_mutex);
    static bool held[256];
    if (e->kind == ANY_FOCUS_LOST) {
        std::fill(std::begin(held), std::end(held), false);
        if (g_open) close_shop();
        return 0;
    }
    uint32_t trade = controls && trade_action ? controls->key(trade_action) : uint32_t('J');
    if (g_open) {   // the shop has the keyboard and mouse
        if (e->kind == ANY_MOUSE_MOVE) { mouse_to_frame(e->x, e->y); return 1; }
        if (e->kind == ANY_MOUSE_WHEEL || e->kind == ANY_MOUSE_DOWN) {
            View v;
            { std::lock_guard view_lock(g_view_mutex); v = g_view; }
            const auto& rows = g_tab == 0 ? v.buy : v.sell;
            if (e->kind == ANY_MOUSE_WHEEL) {
                int step = e->wheel > 0 ? -1 : 1;
                g_first = std::max(0, g_first + step);
                g_cursor = std::clamp(g_cursor + step, 0, std::max(0, int(rows.size()) - 1));
                return 1;
            }
            if (e->button == 2) { close_shop(); return 1; }   // right click closes
            if (e->button != 1) return 1;
            mouse_to_frame(e->x, e->y);
            for (const Area& a : g_areas) {
                if (!a.contains(g_mouse_x, g_mouse_y)) continue;
                if (a.kind == Hit::close) close_shop();
                else if (a.kind == Hit::tab) { if (g_tab != a.index) { g_tab = a.index; g_cursor = 0; g_first = 0; } }
                else if (a.kind == Hit::row) g_cursor = a.index;
                else if (a.index < int(rows.size())) { g_cursor = a.index; post(g_tab == 0 ? Job::buy : Job::sell, rows[size_t(a.index)].key); }
                break;
            }
            return 1;
        }
        if (e->key < 256 && e->kind == ANY_KEY_UP) held[e->key] = false;
        if (e->kind != ANY_KEY_DOWN || e->key >= 256) return 1;
        uint32_t k = e->key;
        bool repeat = held[k];
        held[k] = true;
        View v;
        { std::lock_guard view_lock(g_view_mutex); v = g_view; }
        const auto& rows = g_tab == 0 ? v.buy : v.sell;
        if (k == VK_ESCAPE || (k == trade && !repeat)) close_shop();
        else if (k == VK_UP || k == 'W') g_cursor = std::max(0, g_cursor - 1);
        else if (k == VK_DOWN || k == 'S') g_cursor = std::min(std::max(0, int(rows.size()) - 1), g_cursor + 1);
        else if ((k == VK_LEFT || k == VK_RIGHT || k == 'A' || k == 'D' || k == VK_TAB) && !repeat) { g_tab ^= 1; g_cursor = 0; g_first = 0; }
        else if ((k == VK_RETURN || k == VK_SPACE || k == 'E') && !repeat && g_cursor < int(rows.size()))
            post(g_tab == 0 ? Job::buy : Job::sell, rows[size_t(g_cursor)].key);
        return 1;
    }
    if ((e->kind != ANY_KEY_DOWN && e->kind != ANY_KEY_UP) || e->key >= 256) return 0;
    bool& was = held[e->key];
    if (e->kind == ANY_KEY_UP) { bool consume = was; was = false; return consume ? 1 : 0; }
    if (e->key != VK_F3 && e->key != VK_F4 && e->key != trade) return 0;
    if (!g_ready || !gameplay()) return 0;
    if (was) return 1;   // a held key repeats; one press, one action
    if (e->key == trade) {
        View v;
        { std::lock_guard view_lock(g_view_mutex); v = g_view; }
        if (!hosting() || !career_mode() || !in_range(v, g_local)) return 0;
        was = true;
        g_open = true; g_shop_open = true; g_tab = 0; g_cursor = 0; g_first = 0;
        host.capture_input(1);
        centre_pointer();
        log(0, "ANYCAREER_SHOP opened=1 buy_rows=%d", int(v.buy.size()));
        return 1;
    }
    was = true;
    if (!hosting()) { log(1, "ANYCAREER_JOB ok=0 reason=not_hosting (the host runs these)"); return 1; }
    g_local = local_player();
    post(e->key == VK_F3 ? Job::give_cash : Job::price_list, 0);
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
                       f(bind::inv_get_definition_by_id), f(bind::item_world_create_item), f(bind::floor_create_item),
                       f(bind::item_vector_ctor), f(bind::item_vector_dtor), f(bind::inventory_get_items), f(bind::inventory_destroy_item),
                       f(bind::add_component_set)};
        void** tick = route::cell(bind::server_scene_tick);
        void** add = route::cell(bind::inv_add_definitions);
        auto sc = reinterpret_cast<string_ctor_t>(native(anymaker::sym::string_ctor_cstr_rva));
        auto release = reinterpret_cast<ref_dtor_t>(native(kRefDtorRva));
        bool all = tick && add && sc && release;
        std::string found;
        for (void* p : fns) { all = all && p; found += p ? "1" : "0"; }
        // The add cell must hold inventory _add_definitions itself (or another mod's hook on it), and the
        // definition-file functions reached through the same route must be different functions; so must the
        // vector's constructor and destructor.
        bool add_ok = add && anymaker::readable(add, 8) &&
                      (route::code_is(*add, bind::inv_add_definitions.code_sig) || route::in_other_module(*add));
        bool distinct = fns[1] != fns[2] && fns[1] != fns[3] && fns[2] != fns[3] && (!add || (*add != fns[1] && *add != fns[3])) && fns[7] != fns[8];
        if (!all || !add_ok || !distinct) {
            if (attempt == 20 || attempt == 179)
                log(attempt == 179 ? 2 : 1, "ANYCAREER_LOOKUP functions=%s tick=%d add=%d add_ok=%d distinct=%d natives=%d", found.c_str(), !!tick,
                    !!add, add_ok ? 1 : 0, distinct ? 1 : 0, sc && release ? 1 : 0);
            continue;
        }
        peer_by_id = (get_by_id_t)fns[0]; file_ctor = (file_ctor_t)fns[1]; file_dtor = (file_ctor_t)fns[2]; file_load = (file_load_t)fns[3];
        get_definition = (get_definition_t)fns[4]; create_item = (create_item_t)fns[5]; floor_create = (floor_create_t)fns[6];
        vector_ctor = (vector_life_t)fns[7]; vector_dtor = (vector_life_t)fns[8]; get_items = (get_items_t)fns[9];
        destroy_item = (destroy_item_t)fns[10]; component_set = (component_set_t)fns[11];
        string_ctor = sc; ref_dtor = release;
        if (!write_cash_definition()) { log(2, "ANYCAREER_OFF reason=no_cash_definition"); return; }
        if (!h_tick.install(tick, (void*)&hk_tick) || !h_add_definitions.install(add, (void*)&hk_add_definitions)) {
            h_tick.remove((void*)&hk_tick);
            log(2, "ANYCAREER_OFF reason=hook_cell_not_writable");
            return;
        }
        g_ready = true;
        log(0, "ANYCAREER_READY after_s=%d cash_file=%s draw=%d: in Survival the Mechanic waits at a gas station; F3 gives 250 Cash (host), "
            "F4 writes the price list", attempt, g_cash_json.c_str(), gpu ? 1 : 0);
        load_stations();
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
    gpu = static_cast<const AnyGpuDrawV1*>(services->query("anyapi.gpu_draw", 1));
    if (gpu && (gpu->struct_size != sizeof(*gpu) || gpu->version != 1 || !gpu->emit || !gpu->register_renderer)) gpu = nullptr;
    world_time = static_cast<const AnyWorldTimeV1*>(services->query("anyapi.world_time", 1));
    if (world_time && (world_time->struct_size != sizeof(*world_time) || world_time->version != 1 || !world_time->copy)) world_time = nullptr;
    session = static_cast<const AnySessionV1*>(services->query("anyapi.session", 1));
    if (session && (session->struct_size != sizeof(*session) || session->version != 1 || !session->copy)) session = nullptr;
    services_api = services;
    catalog = static_cast<const AnyItemCatalogV1*>(services->query("anyapi.item_catalog", 1));
    if (catalog && (catalog->struct_size != sizeof(*catalog) || catalog->version != 1 || !catalog->count || !catalog->copy)) catalog = nullptr;
    images = static_cast<const AnyItemImagesV1*>(services->query("anyapi.item_images", 1));
    if (images && (images->struct_size != sizeof(*images) || images->version != 1 || !images->copy)) images = nullptr;
    controls = static_cast<const ModControlsV1*>(services->query("anyhelpers.controls", 1));
    if (controls && controls->struct_size == sizeof(*controls) && controls->version == 1 && controls->register_action && controls->key) {
        ModControlActionV1 d; d.mod_id = kModId; d.mod_name = "AnyCareer"; d.action_id = "trade"; d.label = "Trade with a trader"; d.default_key = 'J';
        trade_action = controls->register_action(&d);
    } else controls = nullptr;
    if (gpu && !gpu->register_renderer(draw, nullptr)) gpu = nullptr;
    if (!gpu) log(2, "ANYCAREER_SHOP draw=0 reason=no_gpu_draw (the shop screen needs AnyAPI's GPU drawing)");
    std::thread(install).detach();   // resolving scans game memory; keep it off the loader and frame threads
}
