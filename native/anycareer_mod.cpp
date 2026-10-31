// AnyCareer 0.0.8: a survival career. Cash, nine traders with buying, selling and job boards, delivery and kill jobs.
//
//   Cash item: a new inventory item, "Cash", added to the game's item definitions right after the game adds its
//      own (inventory_definition_container._add_definitions, on the server and the client scene). Its definition is
//      made at start-up from the player's own copy of the game's item file: the first ammo box entry is copied and
//      renamed, so it stacks like ammo (class ammo_box: the stack size is the definition's gun_magazine_capacity,
//      9999 here) and fits no gun. No game data ships with the mod. Found as loot in small amounts (Loot Cash
//      below). Every player needs the mod, as for any mod-added item. It looks like a bundle of green bills
//      with a paper band: a flat-colour mesh made at start-up (anycareer_cash_mesh.h) and written beside the game's
//      part meshes; if that fails, Cash keeps the ammo box look.
//   The Mechanic: one trader per world, at the gas station spot nearest the Survival start (gas station spots are
//      read from the player's own rom/data/tiles at start-up). The spot is kept in "AnyAPI and Modding/mods/AnyCareer/worlds/<world seed>.txt". Traders show on AnyMap; within kTradeRange, J (rebindable in Mod Controls) opens the
//      shop, and AnyMap (when installed) shows him as a pin through its anymap.pins service. Shop rows show the
//      part's picture (AnyAPI item images). Mouse: click a row's Buy or Sell button, a tab or X; the wheel
//      scrolls; right click closes. The keyboard works too. Buy: up to 16 mechanical and electrical parts that can drop at the world's loot level, 1-3 of each, picked from
//      the world seed and the restock number; the shelf restocks every kRestockDays in-game days (midnights are
//      counted while the world runs). Sell: mechanical and electrical parts and items you carry, for 32% of the price.
//      Everything happens on the host in one server-tick step: buying checks your Cash, takes it from your stacks
//      (a stack's count is set through its own property setter; emptied stacks are removed with
//      server_scene.inventory.destroy_item) and drops the part at your feet; selling removes the item first and
//      then drops the Cash. The shop's stock only exists in the mod, so there is no box to break, steal from or
//      duplicate. Co-op: joiners trade and take jobs too (anycareer_net.h over steam_messages.h, the game's own
//      Steam sessions). A joiner's AnyCareer sends his key presses to the host; the host checks the sender against
//      the players in the world, does the same step at the joiner's feet and from his inventory, and sends him his
//      screen twice a second. Jobs belong to the team: one job in hand per world, any player can deliver or claim
//      it. Without an answer from the host (no AnyCareer 0.0.8 there, or a direct-IP game), a joiner still gets
//      every trader's pin (logic::layout_traders) and a note at the counter.
//   The other traders (logic::kShops): Builder (building parts), Junkyard (cheap used parts, new every
//      day; buys anything at scrap price), Grocer (food, drink, survival supplies), Medic (medical), Marine & Air (boat and plane parts) and
//      the Mod Shop (parts other mods add: any part the game's own rom/data/vehicle_component_definitions.json
//      doesn't list). Also the Outfitter (clothing) and the Gunsmith (guns and ammo, a bit dearer). One of each,
//      spread over the map (Nate): each at the spot of his kind of place (town, industrial, farm, ...) farthest from
//      the traders already standing, never on the top island (logic::layout_traders: from the map alone, so every PC
//      works out the same spots; Hayzeco at the gas station nearest the Survival start), with his own AnyMap pin. A trader whose goods all start at a later loot tier (Marine &
//      Air and the Mod Shop at tier 1) stands there with an empty shelf that says which tier
//      unlocks them (Nate: no speeding ahead).
//      The Mechanic is called Hayzeco Autoparts on screen (Nate). Items are bought as themselves (create_item with their definition), parts
//      as part items.
//   Delivery jobs: every trader but the Mod Shop has a Jobs tab with three jobs a day ("bring 3 x Bandage to a town,
//      1.2 km"), made from the world seed, the trader and the day number: findable goods of his own class
//      (logic::job_goods; parts too, delivered as part items) to a town, house, gas station or other building
//      400 m - 3 km from his door (spawn_zones from the tiles).
//      One job at a time. The drop-off shows as a blue AnyMap pin and a line at the top of the screen; there, J opens
//      the Delivery panel: Deliver takes the items (each carried item counts once) and then you pick the reward:
//      the Cash, or one of three of the trader's goods worth about the same. The job in hand is saved with the world.
//   Kill jobs: two a day on the Gunsmith's board: a number of zombies, and a few of one special kind once the world
//      spawns them (kinds from the player's own rom/data/zombie_definitions.json). A kill counts when one of the host's
//      hits takes a zombie's blood to zero (server_scene.actor_zombie.damage is hooked; its damage source names the
//      attacking actor), or when a zombie dies within kKillReach of the host (shots' source ids didn't match in tests).
//      Then back to the Gunsmith for the reward. Without the hook the board only has deliveries.
//   Hard jobs: more rows on each board (a pricier delivery, more of it, 3-6 km out, double pay; and a big kill job,
//      special zombies when the world has them, half again the pay). They unlock after 3 finished jobs in the world.
//   Loot Cash: Cash turns up in house, office and shop furniture (the clothing loot group), a little at a time
//      (mostly 3-25, now and then up to 250). The ammo_box init hook sets the amount; without it Cash stays out of loot.
//   Prices are a quarter higher than the first pass; what the Mechanic pays for things and what jobs pay are not.
//   The test keys below (F2-F6) are only in test builds (-DANYCAREER_DEV_KEYS=1; off by default, so releases have none).
//   F2 (host, test key): toggles the game's own free move (the developers' debug-menu fly mode, client_scene.
//      m_is_free_move): you fly where you look, through anything, and sprint flies faster. For crossing the map quickly.
//   F6 (host, test key): teleports you to the delivery's drop-off, or to the job's trader (the Mechanic without a job;
//      the game's own peer teleport).
//   F5 (host, test key): drops the items the delivery in hand asks for, so a delivery can be tested quickly.
//   F3 (host): gives the host a stack of 250 Cash, dropped at your feet (test money).
//   F4 (host): writes the price list to "AnyAPI and Modding/mods/AnyCareer/prices.tsv" (stays on this PC: it is
//      game data). The log gets a summary line.
//
// Everything is logged to anymaker_modding.log with an ANYCAREER prefix.
//
// EXPERIMENTAL. Experimental SDK bindings (anycareer_bindings.h): Anymaker 0.1.27 / Steam build 26113663 only.
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
#include "anycareer_net.h"
#include "steam_messages.h"
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

// Layouts from the 0.1.25 reference (metadata), unchanged in 0.1.27.
namespace off {
// server_scene
constexpr ptrdiff_t server_world_seed = 72 + 24;    // m_world_seed (property_s32), its value
constexpr ptrdiff_t server_peers = 0xa8;            // m_peers (server_scene.peer.container)
constexpr ptrdiff_t server_floor_items = 280;       // m_floor_items (server_scene.item_floor_container)
constexpr ptrdiff_t server_physics = 2656;          // m_physics (physics.scene)
constexpr ptrdiff_t server_inventory_defs = 4032;   // m_inventory_definitions; its m_definitions vector is at +0
constexpr ptrdiff_t server_component_defs = 0x1068 + 8;  // m_component_definitions.m_definitions (as AnyLoot reads it)
constexpr ptrdiff_t server_loot_level = 5400;       // m_loot_level (s32)
// server_scene.peer
constexpr ptrdiff_t peer_position = 0x1a18;         // position (vec3), as AnyHelpers' admin uses it
// client (render side)
constexpr ptrdiff_t client_scene = 0xfb0;           // client.m_scene (embedded), as AnyMap reads it
constexpr ptrdiff_t scene_free_move = 0x276;        // client_scene.m_is_free_move (bool): the game's creative-mode fly (free move)
constexpr ptrdiff_t scene_free_move_transform = 0x280;  // m_free_move_transform (mat34): where free move flies from
constexpr ptrdiff_t scene_authority_actor = 0x188;  // m_actors.m_authority_actor (ptr<client_scene.actor>)
constexpr ptrdiff_t client_actor_transform = 392;   // client_scene.actor.m_transform (mat34)
constexpr ptrdiff_t client_actor_id = 8;            // client_scene.actor.m_id (s32)
constexpr ptrdiff_t peer_data = 0x19f0;            // server_peer.data (as AnyHelpers' admin teleport uses it)
constexpr ptrdiff_t peer_actor_id = 6512 + 24;      // m_authority_actor_id (property_s32), its value: the id damage sources carry
constexpr ptrdiff_t peer_actor = 6648;              // m_authority_actor (ptr<server_scene.actor>)
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
constexpr ptrdiff_t item_world_definition = 144;    // ptr<const inventory_definition>
constexpr ptrdiff_t ammo_count = 152;               // server_scene.item_world.ammo_box.m_ammo_count (property_s32)
constexpr ptrdiff_t part_stack_count = 184;         // vehicle_editor_add_component._m_stack_count (property_s32)
constexpr ptrdiff_t part_definition = 216;          // vehicle_editor_add_component.m_component_definition
constexpr ptrdiff_t property_value = 24;            // replication.master.property_type_simple<s32>._m_value
// server_scene.actor / actor_zombie / actor_damage_src
constexpr ptrdiff_t actor_id = 64;                  // m_id (s32)
constexpr ptrdiff_t actor_blood = 72 + 24;          // _m_blood_factor (property_f64_compressed_u8), its value
constexpr ptrdiff_t zombie_definition = 536;        // m_definition (ptr<const zombie_definition>); its m_id string is at +0
constexpr ptrdiff_t damage_src_actor = 0;           // m_actor_id_src (s32)
constexpr ptrdiff_t damage_src_type = 4;            // m_type (e_actor_damage_type, s32; logged only)
constexpr ptrdiff_t server_actor_position = 336 + 72;  // server_scene.actor.m_transform (mat34) .t
}  // namespace off

constexpr int kPropertySetSlot = 5;                 // property_type_simple<T>.set in the property's method table
                                                    // (the slot AnyHelpers' property_type_simple<bool>.set binding uses)
constexpr int32_t kStoreSystem = 1;                 // file.e_store.system (static inference; GpsPart uses it)
constexpr uint32_t kRefDtorRva = 0x4e1b0;           // N:() $ref_dtor (uptr) [metadata]
constexpr int32_t kGiveAmount = 250;
// The test keys F2-F6: off (Nate: remove the dev stuff when it's done); test builds pass -DANYCAREER_DEV_KEYS=1.
#ifndef ANYCAREER_DEV_KEYS
#define ANYCAREER_DEV_KEYS 0
#endif
constexpr bool kDevKeys = ANYCAREER_DEV_KEYS != 0;
constexpr double kKillReach = 120;                  // metres: a zombie that dies this close to the host is his kill
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
struct TeleportEvent { uint8_t header[0x10]; Vec3 position; };   // client_peer.data.event.peer_teleport, 40 bytes
static_assert(sizeof(TeleportEvent) == 40, "peer_teleport");
using teleport_t = void (*)(uint8_t* server, void* peer_data, const TeleportEvent* e);
// 0.2.0 added a second f64 after the amount; it is forwarded untouched.
using zombie_damage_t = void (*)(uint8_t* zombie, uint8_t* scene, const double* amount, const double* extra, const uint8_t* src);
using ammo_box_init_t = void (*)(uint8_t* item);

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

anymaker::cell_hook h_tick, h_add_definitions, h_zombie_damage, h_ammo_init;
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
teleport_t teleport_event;                          // optional (F6 test key)
std::atomic<uint8_t**> g_client_ref{};              // address of g_client (ref<client>); optional (F2 test key)

std::string g_cash_json;                            // absolute path of our definition file (UTF-8); fixed before g_ready
std::filesystem::path g_data_dir;                   // mods/AnyCareer
std::vector<logic::Zone> g_stations;                // gas station spots; fixed before g_stations_ready
std::vector<logic::Spot> g_places;                  // delivery places (towns, houses, ...); fixed before g_stations_ready
std::vector<logic::ZombieKind> g_zombies;           // from rom/data/zombie_definitions.json; fixed before g_stations_ready
thread_local bool t_adding = false;
std::atomic<uint64_t> g_last_tick{0};
std::atomic<bool> g_shop_open{false};
std::atomic<bool> g_free_move{false};             // F2 test key (render side sets it)
// The host's character id as its own client sees it (client_scene.actor m_id): guns send this id as the shot's
// source (client_scene.item_world.gun pushes projectiles_actor_add with actor.m_id), and it is not the server
// actor's m_id (the bulk test: shots by 23, server id 1). Set by the render thread.
std::atomic<int32_t> g_client_actor_id{-3};
ULONGLONG g_free_move_logs_until = 0;
std::atomic<uint32_t> g_mode{ANY_MODE_UNKNOWN};

enum class Job { give_cash, price_list, buy, sell, take, abandon, deliver, claim, test_items, teleport };
// key: shelf row (buy), item id (sell), offer number (take), reward choice (claim: 0 Cash, 1-3 a part)
// shop: the trader (buy, sell); peer_id: a joiner's server peer id (co-op, anycareer_net.h), -1 for the host.
struct Work { Job job; double yaw; ULONGLONG posted; int32_t key; int shop = 0; int32_t peer_id = -1; };
std::mutex g_mutex;
std::deque<Work> g_queue;   // key presses waiting for the next server tick

// What the shop screen shows, written by the server tick and read by the renderer.
// id: part or item definition, for its picture. Job rows name their own button (and key says which job it posts).
struct Row { std::wstring name; int price = 0, quantity = 0; int32_t key = 0; std::string id; std::wstring button; bool off = false; };
constexpr int32_t kKeyAbandon = 100, kKeyDeliver = 200, kKeyClaim = 300;
struct ShopPin { bool placed = false; logic::Zone at; };
struct View {
    bool placed = false;                          // the Mechanic
    logic::Zone trader;
    ShopPin shops[logic::kShopCount];             // every trader, the Mechanic too
    int shop = -1;                                // the trader the host stands at: buy and sell are his
    int64_t cash = 0, days = 0, restock_in = 0, board = -1;
    std::vector<Row> buy, sell, jobs;
    std::wstring status;
    int job_state = 0;                            // 0 none, 1 carrying it, 2 delivered (reward to pick)
    logic::Zone job_at;
    std::wstring job_text;                        // "3 x Bandage to the town"
    int job_have = 0, job_count = 0;              // delivery: items carried; kill job: kills so far
    bool job_kill = false;
    int job_shop = 0;                             // whose job it is (kill jobs end there)
    int next_tier = 0;                            // the open shop has nothing yet: his goods start at this loot tier
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

std::string utf8w(const std::wstring& w) {
    if (w.empty()) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), int(w.size()), nullptr, 0, nullptr, nullptr);
    std::string out(size_t(n > 0 ? n : 0), '\0');
    if (n > 0) WideCharToMultiByte(CP_UTF8, 0, w.data(), int(w.size()), out.data(), n, nullptr, nullptr);
    return out;
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
bool write_cash_definition(bool loot) {
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
    std::string cash = logic::make_cash_entry(entry, mesh, lo, hi, loot);
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
// Part ids in the game's own rom/data/vehicle_component_definitions.json (sorted): any other part is a mod's, for
// the Mod Shop. Empty when the file can't be read (then no part counts as modded).
std::vector<std::string> g_stock_parts;
void load_stations() {
    ULONGLONG start = GetTickCount64();
    std::filesystem::path dir = std::filesystem::path(host.game_directory) / L"rom" / L"data" / L"tiles" / L"0";
    std::vector<logic::Spot> spots;
    int files = 0;
    std::error_code ec;
    for (std::filesystem::directory_iterator it(dir, ec), end; !ec && it != end && !g_stop; it.increment(ec)) {
        if (it->path().extension() != L".json") continue;
        ++files;
        std::ifstream in(it->path(), std::ios::binary);
        std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        logic::spawn_spots(text, spots);
    }
    std::vector<std::pair<std::string, int>> kinds;   // filter -> spots, for the log
    for (const auto& s : spots) {
        if (s.filter == "gas_station") g_stations.push_back(s.at);
        if (logic::delivery_place(s.filter)) g_places.push_back(s);
        auto k = std::find_if(kinds.begin(), kinds.end(), [&](const auto& p) { return p.first == s.filter; });
        if (k == kinds.end()) kinds.push_back({s.filter, 1}); else ++k->second;
    }
    {
        std::ifstream zin(std::filesystem::path(host.game_directory) / L"rom" / L"data" / L"zombie_definitions.json", std::ios::binary);
        g_zombies = logic::parse_zombies(std::string((std::istreambuf_iterator<char>(zin)), std::istreambuf_iterator<char>()));
        std::string z;
        for (const auto& k : g_zombies) z += " " + k.id + "(tier " + std::to_string(k.tier) + ")";
        log(g_zombies.empty() ? 1 : 0, "ANYCAREER_ZOMBIES kinds=%d:%s", int(g_zombies.size()), z.c_str());
    }
    {
        std::ifstream pin(std::filesystem::path(host.game_directory) / L"rom" / L"data" / L"vehicle_component_definitions.json", std::ios::binary);
        g_stock_parts = logic::definition_ids(std::string((std::istreambuf_iterator<char>(pin)), std::istreambuf_iterator<char>()));
        log(g_stock_parts.empty() ? 1 : 0, "ANYCAREER_STOCK_PARTS game_parts=%d%s", int(g_stock_parts.size()), g_stock_parts.empty() ? " (no Mod Shop)" : "");
    }
    std::sort(kinds.begin(), kinds.end(), [](const auto& a, const auto& b) { return a.second > b.second; });
    std::string list;
    for (size_t i = 0; i < kinds.size() && i < 24; ++i) list += (i ? "," : "") + kinds[i].first + "=" + std::to_string(kinds[i].second);
    g_stations_ready = true;
    log(g_stations.empty() ? 2 : 0, "ANYCAREER_STATIONS found=%d delivery_places=%d tile_files=%d ms=%llu kinds=%s", int(g_stations.size()),
        int(g_places.size()), files, (unsigned long long)(GetTickCount64() - start), list.c_str());
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

// The newest Cash definitions (one per definition container; each world load makes a server and a client one): the
// ammo_box init hook knows Cash by these. A ring, so the current world's always fit however many worlds were loaded
// in one run (a full list once left loot Cash at the ammo box's 9999 fill from the third load on).
std::atomic<const uint8_t*> g_cash_defs[4]{};
std::atomic<uint32_t> g_cash_next{0};
std::mutex g_cash_mutex;
void remember_cash(const uint8_t* def) {
    if (!def) return;
    std::lock_guard lock(g_cash_mutex);
    for (auto& d : g_cash_defs) if (d.load() == def) return;
    g_cash_defs[g_cash_next++ % 4].store(def);
}
bool is_cash(const uint8_t* def) {
    for (const auto& d : g_cash_defs) if (def && d.load() == def) return true;
    return false;
}

void hk_add_definitions(void* container, const void* file, const void* outfit) {
    reinterpret_cast<add_definitions_t>(h_add_definitions.original)(container, file, outfit);
    if (!g_ready || g_stop || t_adding || !container) return;
    if (const uint8_t* had = find_definition(container, logic::kCashId)) { remember_cash(had); return; }
    std::string forward = g_cash_json;
    for (char& c : forward) if (c == '\\') c = '/';
    bool ok = add_ours(container, outfit, g_cash_json) || add_ours(container, outfit, forward);
    const uint8_t* found = ok ? find_definition(container, logic::kCashId) : nullptr;
    if (found) remember_cash(found);
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

// The player a job is for: the host, or the joiner who asked (by his server peer id).
uint8_t* work_peer(uint8_t* scene, const Work& w) {
    if (w.peer_id < 0) return host_peer(scene);
    uint8_t* peer = nullptr;
    int32_t id = w.peer_id;
    peer_by_id(&peer, scene + off::server_peers, &id);
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
// part: a vehicle part (else an item); base: the game's price (job rewards), price: this trader's.
struct Candidate { const uint8_t* def; std::string id, name; int price; int32_t stack; bool part = true; int base = 0; };
struct Carried { uint8_t* item; int32_t id; const uint8_t* def; };

struct Trader {
    uint8_t* scene = nullptr;
    uint8_t* server = nullptr;                    // the server the tick runs for (F2 teleport)
    int32_t seed = 0;
    bool loaded = false;
    logic::WorldState world;
    logic::DayClock clock;
    std::vector<Candidate> stock[logic::kShopCount];   // what each trader can have on his shelf, sorted by id
    int32_t level = -1;
    int64_t restock = -1, day = -1;
    std::vector<logic::StockRow> shelf[logic::kShopCount];
    bool stock_knows_mods = false;                // built after the game's part list was read
    std::vector<Candidate> goods[logic::kShopCount];   // what each trader's deliveries can ask for (findable now), sorted by id
    std::vector<logic::Offer> offers[logic::kShopCount];   // today's boards
    int next_tier[logic::kShopCount] = {};        // a trader with nothing to sell yet: the loot tier his goods start at
    int64_t offers_board = -2;
    std::string job_name;                         // display name of the job's item (looked up once)
    ULONGLONG tp_check = 0;                       // when to log where the host ended up after F6
    logic::Zone tp_to;
    const uint8_t* cash_def = nullptr;
    const uint8_t* part_item_def = nullptr;       // "vehicle_editor_add_component"
    uint32_t ticks = 0;
    std::wstring status;
    std::unordered_map<int32_t, std::wstring> peer_status;   // each joiner's last message line, by server peer id
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

bool modded_part(const std::string& id) {   // only once the files are read (g_stations_ready)
    return g_stations_ready && !g_stock_parts.empty() && !std::binary_search(g_stock_parts.begin(), g_stock_parts.end(), id);
}

logic::Goods goods_of(const logic::Entry& e, const uint8_t* def) {
    logic::Goods g;
    g.part = e.part;
    g.loot_group = e.loot_group;
    if (e.part) {
        g.building = rd<bool>(def, off::part_is_building);
        g.modded = modded_part(e.id);
        g.marine_air = logic::marine_air(e.category, e.id);
    }
    return g;
}

// Everything each trader can stock at this loot level, sorted by id so every PC builds the same lists: parts and
// items that can drop by now (a mod's parts once their tier is reached, even if they never drop).
void build_stock(uint8_t* scene) {
    for (auto& s : T.stock) s.clear();
    for (auto& s : T.goods) s.clear();
    std::fill(std::begin(T.next_tier), std::end(T.next_tier), 0);
    int level = std::max(T.level, 1);
    auto add = [&](uint8_t* def, bool part) {
        logic::Entry e = entry_of(def, part);
        if (e.id.empty() || def == T.cash_def || def == T.part_item_def) return;
        if (!part && (e.id.rfind("vehicle_editor", 0) == 0 || e.cls.rfind("vehicle_editor", 0) == 0)) return;
        logic::Goods g = goods_of(e, def);
        bool can = g.modded ? std::max(e.tech_tier, 1) <= level : logic::findable(e, level);
        int32_t stack = part ? std::max(1, rd<int32_t>(def, off::part_default_stack)) : 1;
        std::string name = e.name.empty() ? e.id : e.name;
        for (int s = 0; s < logic::kShopCount; ++s) {
            // Deliveries ask for what can be found by now (not ammo: a box counts once, whatever is in it).
            if (logic::job_goods(s, g) && logic::findable(e, level) && !(!part && e.cls == "ammo_box"))
                T.goods[s].push_back({def, e.id, name, e.price, stack, part, e.price});
            if (!logic::shop_sells(s, g)) continue;
            int price = std::max(1, e.price * logic::kShops[s].price_pct / 100);
            if (!can) {   // not yet (Nate: no speeding ahead): remember the earliest tier, for an empty shop's note
                int tier = std::max(e.tech_tier, 1);
                if ((g.modded || e.spawn_probability > 0) && (!T.next_tier[s] || tier < T.next_tier[s])) T.next_tier[s] = tier;
                continue;
            }
            T.stock[s].push_back({def, e.id, name, price, stack, part, e.price});
        }
    };
    vehicle_reads::for_each_ref(scene + off::server_component_defs, 8192, [&](uint8_t* def) {
        if (anymaker::readable(def, off::part_spawn_probability + 16)) add(def, true);
        return true;
    });
    vehicle_reads::for_each_ref(scene + off::server_inventory_defs, 8192, [&](uint8_t* def) {
        if (anymaker::readable(def, off::item_category + 16)) add(def, false);
        return true;
    });
    auto by_id = [](const Candidate& a, const Candidate& b) { return a.id < b.id; };
    for (auto& s : T.stock) std::sort(s.begin(), s.end(), by_id);
    for (auto& s : T.goods) std::sort(s.begin(), s.end(), by_id);
    for (int s = 0; s < logic::kShopCount; ++s) if (!T.stock[s].empty()) T.next_tier[s] = 0;
}

// What one carried item is worth to trader `shop` (0: he doesn't buy it). Parts in a stack are priced per part.
int sell_value(const Carried& c, int shop, std::string* name, int32_t* count, std::string* id = nullptr) {
    if (!c.def || c.def == T.cash_def || shop < 0 || shop >= logic::kShopCount) return 0;
    const int pct = logic::kShops[shop].sell_pct;
    if (c.def == T.part_item_def) {
        const uint8_t* part = rd<const uint8_t*>(c.item, off::part_definition);
        if (!part || !anymaker::readable(part, off::part_spawn_probability + 16)) return 0;
        logic::Entry e = entry_of(part, true);
        if (!logic::shop_buys(shop, goods_of(e, part))) return 0;
        int32_t n = std::max(1, rd<int32_t>(c.item, off::part_stack_count + off::property_value));
        int32_t per_stack = std::max(1, rd<int32_t>(part, off::part_default_stack));
        if (name) *name = e.name.empty() ? e.id : e.name;
        if (count) *count = n;
        if (id) *id = e.id;
        return std::max(1, int(int64_t(logic::sell_price(e.price, pct)) * n / per_stack));
    }
    if (!anymaker::readable(c.def, off::item_category + 16)) return 0;
    logic::Entry e = entry_of(c.def, false);
    if (e.cls.rfind("vehicle_editor", 0) == 0 || !logic::shop_buys(shop, goods_of(e, c.def))) return 0;
    if (name) *name = e.name.empty() ? e.id : e.name;
    if (count) *count = 1;
    if (id) *id = e.id;
    return logic::sell_price(e.price, pct);
}

bool shop_placed(int s) { return s == logic::kMechanic ? T.world.placed : T.world.shops[s].placed; }
logic::Zone shop_at(int s) { return s == logic::kMechanic ? T.world.trader : T.world.shops[s].at; }
std::vector<int>& shop_sold(int s) { return s == logic::kMechanic ? T.world.sold : T.world.shops[s].sold; }
int64_t& shop_restock(int s) { return s == logic::kMechanic ? T.world.restock : T.world.shops[s].restock; }
std::vector<int>& shop_taken(int s) { return s == logic::kMechanic ? T.world.taken : T.world.shops[s].taken; }

bool near_shop(uint8_t* peer, int s) {
    if (s < 0 || s >= logic::kShopCount || !shop_placed(s)) return false;
    Vec3 p = rd<Vec3>(peer, off::peer_position);
    logic::Zone at = shop_at(s);
    double dx = p.x - at.x, dz = p.z - at.z;
    return dx * dx + dz * dz <= (kTradeRange + 10) * (kTradeRange + 10);
}
// The trader the host stands at (the nearest, when two are close), or -1.
int shop_here(uint8_t* peer) {
    int best = -1;
    double best_d = 0;
    Vec3 p = rd<Vec3>(peer, off::peer_position);
    for (int s = 0; s < logic::kShopCount; ++s) {
        if (!near_shop(peer, s)) continue;
        logic::Zone at = shop_at(s);
        double d = std::hypot(p.x - at.x, p.z - at.z);
        if (best < 0 || d < best_d) { best = s; best_d = d; }
    }
    return best;
}

void buy(uint8_t* scene, const Work& w) {
    uint8_t* peer = work_peer(scene, w);
    uint8_t* inventory = peer_inventory(peer);
    const int s = w.shop;
    auto fail = [&](const char* why, const std::wstring& say) { T.status = say; log(1, "ANYCAREER_BUY ok=0 reason=%s shop=%d row=%d", why, s, w.key); };
    if (!inventory) return fail("no_host_player", L"You need to be alive to trade.");
    if (s < 0 || s >= logic::kShopCount || !near_shop(peer, s)) return fail("too_far", L"Walk back to the trader to trade.");
    const auto& shelf = T.shelf[s];
    const auto& stock = T.stock[s];
    auto& sold = shop_sold(s);
    if (w.key < 0 || w.key >= int(shelf.size()) || w.key >= int(sold.size()) || shelf[size_t(w.key)].candidate >= int(stock.size()))
        return fail("no_such_row", L"That shelf changed. Pick again.");
    const auto& row = shelf[size_t(w.key)];
    const Candidate& c = stock[size_t(row.candidate)];
    if (row.quantity - sold[size_t(w.key)] <= 0) return fail("sold_out", L"Sold out until the next restock.");
    int n = carried(inventory);
    std::vector<logic::Stack> stacks;
    for (int i = 0; i < n; ++i)
        if (g_carried[i].def == T.cash_def) stacks.push_back({g_carried[i].id, rd<int32_t>(g_carried[i].item, off::ammo_count + off::property_value)});
    std::vector<logic::Payment> plan;
    if (!logic::plan_payment(stacks, c.price, plan)) return fail("not_enough_cash", L"Not enough Cash on you.");
    // Make the goods first, so a failure costs nothing.
    Ref item{};
    if (c.part) {
        create_item(&item, T.part_item_def, scene);
        if (!item.ptr || rd<const uint8_t*>(item.ptr, off::item_world_definition) != T.part_item_def) return fail("create_item_failed", L"The trade failed (see the log).");
        int32_t count = c.stack;
        component_set(item.ptr, c.def, &count);
        if (rd<const uint8_t*>(item.ptr, off::part_definition) != c.def) return fail("part_not_set", L"The trade failed (see the log).");
    } else {
        create_item(&item, c.def, scene);
        if (!item.ptr || rd<const uint8_t*>(item.ptr, off::item_world_definition) != c.def) return fail("create_item_failed", L"The trade failed (see the log).");
    }
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
    ++sold[size_t(w.key)];
    save_world();
    T.status = L"Bought " + wide(c.name) + L" for " + std::to_wstring(c.price) + L" Cash.";
    bool ok = paid == c.price && dropped;
    log(ok ? 0 : 2, "ANYCAREER_BUY ok=%d shop=%s %s=%s stack=%d price=%d paid=%d stacks_removed=%d dropped=%d", ok ? 1 : 0, logic::kShops[s].id,
        c.part ? "part" : "item", c.id.c_str(), c.part ? rd<int32_t>(item.ptr, off::part_stack_count + off::property_value) : 1, c.price, paid, removed,
        dropped ? 1 : 0);
}

void sell(uint8_t* scene, const Work& w) {
    uint8_t* peer = work_peer(scene, w);
    uint8_t* inventory = peer_inventory(peer);
    const int s = w.shop;
    auto fail = [&](const char* why, const wchar_t* say) { T.status = say; log(1, "ANYCAREER_SELL ok=0 reason=%s shop=%d item=%d", why, s, w.key); };
    if (!inventory) return fail("no_host_player", L"You need to be alive to trade.");
    if (s < 0 || s >= logic::kShopCount || !near_shop(peer, s)) return fail("too_far", L"Walk back to the trader to trade.");
    int n = carried(inventory);
    const Carried* found = nullptr;
    for (int i = 0; i < n; ++i) if (g_carried[i].id == w.key) found = &g_carried[i];
    std::string name;
    int value = found ? sell_value(*found, s, &name, nullptr) : 0;
    if (!value) return fail("not_carried_or_not_wanted", L"You don't have that any more.");
    // Take the item first, then pay: nothing can be sold twice.
    Ref gone{};
    destroy_item(&gone, inventory, &w.key);
    if (!gone.ptr) return fail("remove_failed", L"The trade failed (see the log).");
    ref_dtor(&gone);
    int paid = 0;
    for (int32_t stack : logic::cash_stacks(value)) if (drop_cash(scene, peer, w.yaw, stack)) paid += stack;
    T.status = L"Sold " + wide(name) + L" for " + std::to_wstring(value) + L" Cash.";
    log(paid == value ? 0 : 2, "ANYCAREER_SELL ok=%d shop=%s item=%s value=%d paid=%d", paid == value ? 1 : 0, logic::kShops[s].id, name.c_str(), value, paid);
}

// ------------------------------------------------------------------ delivery jobs (server thread)
// Every trader but the Mod Shop has a board: deliveries of his class of goods (logic::job_goods; build_stock lists
// them), from his door to places around the map, and the Gunsmith's kill jobs. A trader with nothing findable yet
// (Marine & Air before tier 2) asks for the Mechanic's goods meanwhile.
const std::vector<Candidate>& job_goods_of(int s) { return T.goods[s].empty() ? T.goods[logic::kMechanic] : T.goods[s]; }
// The Mechanic keeps the board seed he always had.
uint64_t board_seed(int s) { return uint64_t(uint32_t(T.seed)) ^ (s ? logic::mix(0xb0a2d + uint64_t(s)) : 0); }

void refresh_offers() {
    const bool kills = h_zombie_damage.cell != nullptr;
    const std::vector<int> specials = logic::special_zombies(g_zombies, T.level);
    std::string list;
    int total = 0;
    for (int s = 0; s < logic::kShopCount; ++s) {
        auto& offers = T.offers[s];
        offers.clear();
        if (s == logic::kModShop || !shop_placed(s)) continue;
        const auto& goods = job_goods_of(s);
        std::vector<int> prices;
        for (const auto& g : goods) prices.push_back(g.base);
        const logic::Zone from = shop_at(s);
        const uint64_t seed = board_seed(s);
        offers = logic::make_offers(prices, g_places, from, seed, T.world.board, 3);
        if (kills && s == logic::kKillShop)
            for (const auto& o : logic::make_kill_offers(specials, seed, T.world.board)) offers.push_back(o);
        for (const auto& o : logic::make_hard_offers(prices, g_places, from, specials, seed, T.world.board, kills && s == logic::kKillShop))
            offers.push_back(o);
        total += int(offers.size());
        list += std::string(" ") + logic::kShops[s].id + "[" + std::to_string(goods.size()) + "]:";
        for (const auto& o : offers)
            if (o.kill) list += std::string(o.hard ? " HARD" : "") + " kill " + std::to_string(o.count) + "x" + (o.item < 0 ? std::string("any") : g_zombies[size_t(o.item)].id) + "=" + std::to_string(o.reward);
            else list += std::string(o.hard ? " HARD" : "") + " " + std::to_string(o.count) + "x" + goods[size_t(o.item)].id + "->" + g_places[size_t(o.spot)].filter + "(" +
                    std::to_string(int(std::hypot(g_places[size_t(o.spot)].at.x - from.x, g_places[size_t(o.spot)].at.z - from.z))) + "m)=" + std::to_string(o.reward);
    }
    T.offers_board = T.world.board;
    log(0, "ANYCAREER_JOBS board=%lld places=%d done=%d offers=%d:%s", (long long)T.world.board, int(g_places.size()), T.world.jobs_done, total, list.c_str());
}

std::wstring metres_text(double m) {
    wchar_t b[32];
    if (m >= 1000) swprintf_s(b, L"%.1f km", m / 1000); else swprintf_s(b, L"%d m", int(m / 10) * 10);
    return b;
}

// "Acid Zombie" for a special kind, "zombie" for any.
std::string zombie_name(const std::string& id) {
    if (id == "any" || id == logic::kBasicZombie) return "zombie";
    for (const auto& z : g_zombies) if (z.id == id) return z.name.empty() ? logic::place_name(id) : z.name;
    return logic::place_name(id);
}
std::wstring kill_text(int count, const std::string& id) {
    std::string n = zombie_name(id);
    return std::to_wstring(count) + L" " + wide(n) + (count == 1 ? L"" : L"s");
}

// The delivery's goods, from the traders' lists (a part's definition is not an item's, so it is looked up there).
const Candidate* job_candidate() {
    for (const auto* lists : {T.goods, T.stock})
        for (int s = 0; s < logic::kShopCount; ++s)
            for (const auto& c : lists[s]) if (c.part == T.world.job_part && c.id == T.world.job_item) return &c;
    return nullptr;
}
const uint8_t* job_def(uint8_t* scene) {
    if (const Candidate* c = job_candidate()) return c->def;
    return T.world.job_part || !scene ? nullptr : find_definition(scene + off::server_inventory_defs, T.world.job_item.c_str());
}

std::string job_name() {
    if (T.world.job_kill) return zombie_name(T.world.job_item);
    if (T.job_name.empty()) {
        if (const Candidate* c = job_candidate()) T.job_name = c->name;
        else if (const uint8_t* def = job_def(T.scene)) { logic::Entry e = entry_of(def, false); T.job_name = e.name; }
        if (T.job_name.empty()) T.job_name = T.world.job_item;
    }
    return T.job_name;
}

// The job's goods the player carries (each item counts once; a part counts once per part item, whatever its stack).
int job_items(int n, const uint8_t* def, std::vector<int32_t>* ids = nullptr) {
    int have = 0;
    for (int i = 0; i < n; ++i) {
        bool match = T.world.job_part ? g_carried[i].def == T.part_item_def && rd<const uint8_t*>(g_carried[i].item, off::part_definition) == def
                                      : g_carried[i].def == def;
        if (def && match) { ++have; if (ids) ids->push_back(g_carried[i].id); }
    }
    return have;
}

bool near_spot(uint8_t* peer, const logic::Zone& at) {
    Vec3 p = rd<Vec3>(peer, off::peer_position);
    double dx = p.x - at.x, dz = p.z - at.z;
    return dx * dx + dz * dz <= (kTradeRange + 10) * (kTradeRange + 10);
}

void clear_job() {
    T.world.job_state = 0; T.world.job_item.clear(); T.world.job_count = T.world.job_reward = 0; T.world.job_place.clear();
    T.world.job_parts.clear(); T.job_name.clear(); T.world.job_kill = false; T.world.job_progress = 0;
    T.world.job_hard = false; T.world.job_shop = logic::kMechanic; T.world.job_part = false;
}

// The reward choices come from the job's trader (the Mechanic's when his stock is empty).
const std::vector<Candidate>& reward_pool() {
    int s = T.world.job_shop;
    return s >= 0 && s < logic::kShopCount && !T.stock[s].empty() ? T.stock[s] : T.stock[logic::kMechanic];
}

// The three goods choices for the job's reward, then the reward is owed (delivered, or all the kills made).
void job_done() {
    const auto& pool = reward_pool();
    std::vector<int> prices;
    for (const auto& c : pool) prices.push_back(c.price);
    T.world.job_parts.clear();
    for (int i : logic::reward_parts(prices, T.world.job_reward, logic::mix(uint64_t(uint32_t(T.seed)) ^ uint64_t(T.world.board) ^ 0xca5e)))
        T.world.job_parts.push_back(pool[size_t(i)].id);
    T.world.job_state = 2;
    save_world();
}

void take(uint8_t* scene, const Work& w) {
    uint8_t* peer = work_peer(scene, w);
    const int s = w.shop;
    auto fail = [&](const char* why, const wchar_t* say) { T.status = say; log(1, "ANYCAREER_TAKE ok=0 reason=%s shop=%d offer=%d", why, s, w.key); };
    if (!peer) return fail("no_host_player", L"You need to be alive to take a job.");
    if (s < 0 || s >= logic::kShopCount || !near_shop(peer, s)) return fail("too_far", L"Walk back to the trader to take a job.");
    if (T.world.job_state) return fail("job_in_hand", L"Finish or abandon your job first.");
    const auto& offers = T.offers[s];
    auto& taken = shop_taken(s);
    if (w.key < 0 || w.key >= int(offers.size()) || T.offers_board != T.world.board) return fail("no_such_offer", L"The board changed. Pick again.");
    if (std::find(taken.begin(), taken.end(), w.key) != taken.end()) return fail("taken", L"That job is taken.");
    const logic::Offer& o = offers[size_t(w.key)];
    if (o.hard && T.world.jobs_done < logic::kHardUnlock) return fail("hard_locked", L"Finish a few more jobs to unlock hard jobs.");
    if (o.kill) {
        if (o.item >= int(g_zombies.size())) return fail("stale_offer", L"The board changed. Pick again.");
        taken.push_back(w.key);
        clear_job();
        T.world.job_state = 1; T.world.job_kill = true; T.world.job_item = o.item < 0 ? "any" : g_zombies[size_t(o.item)].id;
        T.world.job_count = o.count; T.world.job_reward = o.reward; T.world.job_place = logic::kShops[s].id; T.world.job_at = shop_at(s);
        T.world.job_hard = o.hard; T.world.job_shop = s;
        save_world();
        T.status = L"Job taken: kill " + kill_text(o.count, T.world.job_item) + L", then come back here.";
        log(0, "ANYCAREER_TAKE ok=1 shop=%s kill=%s count=%d reward=%d hard=%d", logic::kShops[s].id, T.world.job_item.c_str(), o.count, o.reward, o.hard ? 1 : 0);
        return;
    }
    const auto& goods = job_goods_of(s);
    if (size_t(o.item) >= goods.size() || size_t(o.spot) >= g_places.size()) return fail("stale_offer", L"The board changed. Pick again.");
    const Candidate& g = goods[size_t(o.item)];
    const logic::Spot& at = g_places[size_t(o.spot)];
    taken.push_back(w.key);
    clear_job();
    T.world.job_state = 1; T.world.job_item = g.id; T.world.job_count = o.count; T.world.job_reward = o.reward;
    T.world.job_place = at.filter; T.world.job_at = at.at; T.job_name = g.name; T.world.job_hard = o.hard;
    T.world.job_shop = s; T.world.job_part = g.part;
    save_world();
    T.status = L"Job taken: bring " + std::to_wstring(o.count) + L" x " + wide(g.name) + L". It is on your map.";
    log(0, "ANYCAREER_TAKE ok=1 shop=%s %s=%s count=%d place=%s at=(%.0f, %.0f) reward=%d hard=%d", logic::kShops[s].id, g.part ? "part" : "item", g.id.c_str(),
        o.count, at.filter.c_str(), at.at.x, at.at.z, o.reward, o.hard ? 1 : 0);
}

void abandon(uint8_t*, const Work&) {
    if (T.world.job_state != 1) { log(1, "ANYCAREER_ABANDON ok=0 reason=no_job"); return; }
    log(0, "ANYCAREER_ABANDON ok=1 item=%s", T.world.job_item.c_str());
    clear_job();
    save_world();
    T.status = L"Job abandoned. Today's other jobs are still on the board.";
}

void deliver(uint8_t* scene, const Work& w) {
    uint8_t* peer = work_peer(scene, w);
    uint8_t* inventory = peer_inventory(peer);
    auto fail = [&](const char* why, const std::wstring& say) { T.status = say; log(1, "ANYCAREER_DELIVER ok=0 reason=%s", why); };
    if (!inventory) return fail("no_host_player", L"You need to be alive to deliver.");
    if (T.world.job_state != 1 || T.world.job_kill) return fail("no_job", L"No delivery in hand.");
    if (!near_spot(peer, T.world.job_at)) return fail("too_far", L"This isn't the drop-off. Follow the blue pin.");
    const uint8_t* def = job_def(scene);
    int n = carried(inventory);
    std::vector<int32_t> ids;
    int have = job_items(n, def, &ids);
    if (have < T.world.job_count)
        return fail("not_enough", L"You have " + std::to_wstring(have) + L" of " + std::to_wstring(T.world.job_count) + L" " + wide(job_name()) + L".");
    // Take the goods first, then the reward is owed: nothing can be handed in twice.
    int removed = 0;
    for (int i = 0; i < T.world.job_count; ++i) {
        Ref gone{};
        destroy_item(&gone, inventory, &ids[size_t(i)]);
        if (gone.ptr) { ++removed; ref_dtor(&gone); }
    }
    job_done();
    T.status = L"Delivered! Pick your reward.";
    log(removed == T.world.job_count ? 0 : 2, "ANYCAREER_DELIVER ok=%d item=%s count=%d removed=%d reward=%d part_choices=%d", removed == T.world.job_count ? 1 : 0,
        T.world.job_item.c_str(), T.world.job_count, removed, T.world.job_reward, int(T.world.job_parts.size()));
}

const Candidate* candidate(const std::string& id) {
    for (const auto& c : reward_pool()) if (c.id == id) return &c;
    return nullptr;
}

// A new part item (with the part's usual stack) or item of the goods; empty on failure.
Ref make_goods(uint8_t* scene, const Candidate& c) {
    Ref item{};
    create_item(&item, c.part ? T.part_item_def : c.def, scene);
    if (!item.ptr || rd<const uint8_t*>(item.ptr, off::item_world_definition) != (c.part ? T.part_item_def : c.def)) return Ref{};
    if (c.part) {
        int32_t count = c.stack;
        component_set(item.ptr, c.def, &count);
        if (rd<const uint8_t*>(item.ptr, off::part_definition) != c.def) return Ref{};
    }
    return item;
}

void claim(uint8_t* scene, const Work& w) {
    uint8_t* peer = work_peer(scene, w);
    auto fail = [&](const char* why, const wchar_t* say) { T.status = say; log(1, "ANYCAREER_CLAIM ok=0 reason=%s choice=%d", why, w.key); };
    if (!peer) return fail("no_host_player", L"You need to be alive to take your reward.");
    if (T.world.job_state != 2) return fail("nothing_owed", L"No reward waiting.");
    int reward = T.world.job_reward;
    if (w.key == 0) {
        clear_job();
        ++T.world.jobs_done;
        save_world();
        int paid = 0;
        for (int32_t stack : logic::cash_stacks(reward)) if (drop_cash(scene, peer, w.yaw, stack)) paid += stack;
        T.status = L"Paid " + std::to_wstring(reward) + L" Cash. New jobs come every day.";
        log(paid == reward ? 0 : 2, "ANYCAREER_CLAIM ok=%d choice=cash reward=%d paid=%d", paid == reward ? 1 : 0, reward, paid);
        return;
    }
    if (w.key < 1 || w.key > int(T.world.job_parts.size())) return fail("no_such_choice", L"Pick again.");
    const Candidate* c = candidate(T.world.job_parts[size_t(w.key - 1)]);
    if (!c) return fail("goods_not_found", L"That isn't available. Pick another reward.");
    Ref item = make_goods(scene, *c);
    if (!item.ptr) return fail("create_item_failed", L"That failed (see the log).");
    std::string id = c->id;
    const bool part = c->part;
    std::wstring name = wide(c->name);
    clear_job();
    ++T.world.jobs_done;
    save_world();
    bool dropped = drop(scene, peer, w.yaw, item);
    T.status = L"Your reward: " + name + L". New jobs come every day.";
    log(dropped ? 0 : 2, "ANYCAREER_CLAIM ok=%d choice=%s id=%s reward_cash=%d", dropped ? 1 : 0, part ? "part" : "item", id.c_str(), reward);
}

// F5 (host, test key): drops the items the delivery in hand asks for.
void test_items(uint8_t* scene, const Work& w) {
    uint8_t* peer = host_peer(scene);
    if (!peer || T.world.job_state != 1 || T.world.job_kill) {
        T.status = L"F5 drops a delivery job's items. Take a delivery job first.";
        log(1, "ANYCAREER_TEST_ITEMS ok=0 reason=no_delivery_in_hand");
        return;
    }
    const Candidate* c = job_candidate();
    const uint8_t* def = job_def(scene);
    int dropped = 0;
    for (int i = 0; def && i < T.world.job_count && i < 20; ++i) {
        Ref item{};
        if (c) item = make_goods(scene, *c); else create_item(&item, def, scene);
        if (item.ptr && drop(scene, peer, w.yaw + 0.25 * i, item)) ++dropped;
    }
    log(dropped == T.world.job_count ? 0 : 2, "ANYCAREER_TEST_ITEMS ok=%d item=%s wanted=%d dropped=%d", dropped == T.world.job_count ? 1 : 0,
        T.world.job_item.c_str(), T.world.job_count, dropped);
}

// F6 (host, test key): jumps to the delivery's drop-off, or to the Mechanic (no job, a kill job, or a reward waiting).
void teleport(uint8_t* scene, const Work&) {
    uint8_t* peer = host_peer(scene);
    void* data = peer && anymaker::readable(peer + off::peer_data, 8) ? rd<void*>(peer, off::peer_data) : nullptr;
    if (g_free_move) { T.status = L"Turn free move off (F2) before teleporting."; log(1, "ANYCAREER_TELEPORT ok=0 reason=free_move_on"); return; }
    if (!teleport_event || !T.server || !data || !T.world.placed) { log(1, "ANYCAREER_TELEPORT ok=0 reason=%s", !teleport_event ? "no_teleport_function" : "not_ready"); return; }
    bool drop_off = T.world.job_state == 1 && !T.world.job_kill;
    const int back = T.world.job_state ? T.world.job_shop : logic::kMechanic;   // a kill job or reward: the job's trader
    logic::Zone at = drop_off ? T.world.job_at : shop_placed(back) ? shop_at(back) : T.world.trader;
    TeleportEvent e{};
    e.position = {at.x + 3, at.y + 2, at.z + 3};   // beside the spot, a little up, so you land on the ground
    teleport_event(T.server, data, &e);
    T.status = drop_off ? L"Teleported to the drop-off (test key)." : L"Teleported to " + wide(logic::kShops[back].name) + L" (test key).";
    T.tp_check = GetTickCount64() + 1500; T.tp_to = {e.position.x, e.position.y, e.position.z};
    log(0, "ANYCAREER_TELEPORT ok=1 to=%s at=(%.0f, %.0f, %.0f)", drop_off ? "dropoff" : logic::kShops[back].id, e.position.x, e.position.y, e.position.z);
}

// A zombie hit (server thread, inside the game's damage call): a kill when its blood went from above zero to zero.
// Credited when the damage source is the host's character.
void on_zombie_hit(uint8_t* zombie, uint8_t* scene, const uint8_t* src, double blood_before) {
    if (scene != T.scene || !T.loaded || !anymaker::readable(zombie, off::zombie_definition + 8)) return;
    double blood = rd<double>(zombie, off::actor_blood);
    if (!(blood_before > 0 && blood <= 0)) return;
    int32_t by = src && anymaker::readable(src, 4) ? rd<int32_t>(src, off::damage_src_actor) : -1;
    uint8_t* peer = host_peer(scene);
    uint8_t* actor = peer && anymaker::readable(peer + off::peer_actor, 8) ? rd<uint8_t*>(peer, off::peer_actor) : nullptr;
    // Player hits arrive as client events whose damage source is the peer's authority actor id (server.on_event
    // actor_take_damage); test 7 showed the actor's own m_id field doesn't match it, so the peer's id is used.
    int32_t host_id = peer && anymaker::readable(peer + off::peer_actor_id, 4) ? rd<int32_t>(peer, off::peer_actor_id) : -2;
    int32_t actor_field = actor && anymaker::readable(actor + off::actor_id, 4) ? rd<int32_t>(actor, off::actor_id) : -2;
    const uint8_t* def = rd<const uint8_t*>(zombie, off::zombie_definition);
    std::string kind = def ? read_string(def) : std::string();
    int32_t client_id = g_client_actor_id.load();
    int32_t type = src && anymaker::readable(src + off::damage_src_type, 4) ? rd<int32_t>(src, off::damage_src_type) : -1;
    // Where it died, from the nearest player. The final test logged sources that were neither id (28, 66, 121, 627,
    // ...: shots can carry other ids), so a kill close to a player counts too: a zombie that drops next to you is
    // yours. The job is the whole team's (co-op), so any player's kill counts.
    double metres = -1;
    if (anymaker::readable(zombie + off::server_actor_position, sizeof(Vec3))) {
        Vec3 z = rd<Vec3>(zombie, off::server_actor_position);
        for (int32_t id = 0; id < 64; ++id) {
            uint8_t* other = nullptr;
            peer_by_id(&other, scene + off::server_peers, &id);
            if (!other || !anymaker::readable(other + off::peer_position, 24)) continue;
            Vec3 p = rd<Vec3>(other, off::peer_position);
            double d = std::hypot(z.x - p.x, z.z - p.z);
            if (metres < 0 || d < metres) metres = d;
        }
    }
    bool mine = (by >= 0 && (by == host_id || by == client_id)) || (metres >= 0 && metres <= kKillReach);
    bool counts = mine && T.world.job_state == 1 && T.world.job_kill && (T.world.job_item == "any" || T.world.job_item == kind);
    if (counts) {
        ++T.world.job_progress;
        if (T.world.job_progress >= T.world.job_count) {
            job_done();
            T.status = L"Kill job done! Pick your reward in the Jobs tab.";
        } else {
            save_world();
        }
    }
    static int logs = 0;
    if (logs++ < 40 || counts)
        log(0, "ANYCAREER_KILL zombie=%s by_actor=%d type=%d metres=%.0f host_actor=%d actor_field=%d client_actor=%d mine=%d counted=%d job=%s progress=%d/%d",
            kind.c_str(), by, type, metres, host_id, actor_field, client_id, mine ? 1 : 0, counts ? 1 : 0, T.world.job_kill && T.world.job_state == 1 ? T.world.job_item.c_str() : "none", T.world.job_progress,
            T.world.job_count);
}
void run_zombie_hit(uint8_t* zombie, uint8_t* scene, const uint8_t* src, double blood_before) {
    __try { on_zombie_hit(zombie, scene, src, blood_before); } __except (EXCEPTION_EXECUTE_HANDLER) { log(2, "ANYCAREER_KILL ok=0 reason=native_exception"); }
}
double blood_of(uint8_t* actor) {
    __try { return anymaker::readable(actor + off::actor_blood, 8) ? rd<double>(actor, off::actor_blood) : 0; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}
// Cash found as loot: the game's ammo_box init fills a new box to its capacity (9999 for Cash). For Cash the mod
// then sets a small found amount. Every other way Cash is made (the mod's drops, a save loading) sets its count after
// init, so only loot keeps this amount.
std::atomic<uint64_t> g_loot_rolls{0};
void set_loot_cash(uint8_t* item) {
    __try {
        if (!anymaker::readable(item + off::item_world_definition, 8) || !is_cash(rd<const uint8_t*>(item, off::item_world_definition))) return;
        uint64_t n = g_loot_rolls.fetch_add(1);
        int amount = logic::loot_cash_amount(logic::mix(n ^ (uint64_t(uintptr_t(item)) << 7) ^ GetTickCount64()));
        bool ok = set_count(item, off::ammo_count, amount);
        if (n < 30) log(ok ? 0 : 2, "ANYCAREER_LOOT_CASH ok=%d amount=%d", ok ? 1 : 0, amount);
    } __except (EXCEPTION_EXECUTE_HANDLER) { log(2, "ANYCAREER_LOOT_CASH ok=0 reason=native_exception"); }
}
void hk_ammo_init(uint8_t* item) {
    reinterpret_cast<ammo_box_init_t>(h_ammo_init.original)(item);
    if (item && !g_stop) set_loot_cash(item);
}

void hk_zombie_damage(uint8_t* zombie, uint8_t* scene, const double* amount, const double* extra, const uint8_t* src) {
    bool watch = zombie && scene && !g_stop && g_ready;
    double before = watch ? blood_of(zombie) : 0;
    reinterpret_cast<zombie_damage_t>(h_zombie_damage.original)(zombie, scene, amount, extra, src);
    if (watch && before > 0) run_zombie_hit(zombie, scene, src, before);
}

// The Jobs tab: the board of the trader the host stands at, or the job in hand, or the reward choice.
void job_rows(View& v, uint8_t* peer, int have) {
    v.job_state = T.world.job_state;
    v.job_shop = T.world.job_shop;
    v.job_at = T.world.job_at;
    v.board = T.world.board;
    v.job_have = have;
    v.job_count = T.world.job_count;
    v.job_kill = T.world.job_kill;
    if (T.world.job_kill) { v.job_text = kill_text(T.world.job_count, T.world.job_item); v.job_have = T.world.job_progress; }
    else if (T.world.job_state) v.job_text = std::to_wstring(T.world.job_count) + L" x " + wide(job_name()) + L" to the " + wide(logic::place_name(T.world.job_place));
    if (T.world.job_state == 0 && T.offers_board == T.world.board && v.shop >= 0) {
        const int s = v.shop;
        const auto& offers = T.offers[s];
        const auto& goods = job_goods_of(s);
        const auto& taken_list = shop_taken(s);
        const logic::Zone from = shop_at(s);
        int to_unlock = logic::kHardUnlock - T.world.jobs_done;
        for (size_t i = 0; i < offers.size(); ++i) {
            const auto& o = offers[i];
            bool taken = std::find(taken_list.begin(), taken_list.end(), int(i)) != taken_list.end();
            bool locked = o.hard && to_unlock > 0;
            // Hard jobs: marked, and greyed out with what unlocks them until enough jobs are done.
            auto hard = [&](Row& r) {
                if (!o.hard) return;
                r.name = L"HARD: " + r.name;
                if (locked) {
                    r.name += L" (finish " + std::to_wstring(to_unlock) + L" more job" + (to_unlock == 1 ? L"" : L"s") + L" to unlock)";
                    r.off = true; r.button = L"locked";
                }
            };
            if (o.kill) {
                if (o.item >= int(g_zombies.size())) continue;
                Row r;
                r.name = L"Kill " + kill_text(o.count, o.item < 0 ? std::string("any") : g_zombies[size_t(o.item)].id) + L" (any weapon, anywhere), then come back";
                r.price = o.reward; r.key = int32_t(i); r.off = taken;
                r.button = taken ? L"taken" : L"Take  " + std::to_wstring(o.reward);
                hard(r);
                v.jobs.push_back(std::move(r));
                continue;
            }
            if (size_t(o.item) >= goods.size() || size_t(o.spot) >= g_places.size()) continue;
            const Candidate& g = goods[size_t(o.item)];
            const auto& at = g_places[size_t(o.spot)];
            Row r;
            r.name = L"Bring " + std::to_wstring(o.count) + L" x " + wide(g.name) + L" to a " + wide(logic::place_name(at.filter)) + L", " +
                     metres_text(std::hypot(at.at.x - from.x, at.at.z - from.z));
            r.price = o.reward; r.key = int32_t(i); r.id = g.id; r.off = taken;
            r.button = taken ? L"taken" : L"Take  " + std::to_wstring(o.reward);
            hard(r);
            v.jobs.push_back(std::move(r));
        }
    } else if (T.world.job_state == 1 && T.world.job_kill) {
        Row r;
        r.name = L"Kill " + v.job_text + L" (" + std::to_wstring(T.world.job_progress) + L" so far)";
        r.price = T.world.job_reward; r.key = kKeyAbandon; r.button = L"Abandon";
        v.jobs.push_back(std::move(r));
    } else if (T.world.job_state == 1) {
        bool there = peer && near_spot(peer, T.world.job_at);
        Row r;
        r.name = L"Bring " + v.job_text + L" (you have " + std::to_wstring(have) + L")";
        r.price = T.world.job_reward; r.id = T.world.job_item;
        r.key = there ? kKeyDeliver : kKeyAbandon;
        r.button = there ? L"Deliver" : L"Abandon";
        v.jobs.push_back(std::move(r));
    } else {
        Row cash;
        cash.name = L"Reward: " + std::to_wstring(T.world.job_reward) + L" Cash"; cash.key = kKeyClaim; cash.id = logic::kCashId; cash.button = L"Take";
        v.jobs.push_back(std::move(cash));
        for (size_t i = 0; i < T.world.job_parts.size(); ++i) {
            const Candidate* c = candidate(T.world.job_parts[i]);
            Row r;
            r.name = L"Or: " + (c ? wide(c->name) + (c->stack > 1 ? L" x" + std::to_wstring(c->stack) : L"") : wide(T.world.job_parts[i]));
            r.key = kKeyClaim + int32_t(i) + 1; r.id = T.world.job_parts[i]; r.button = L"Take"; r.off = !c;
            v.jobs.push_back(std::move(r));
        }
    }
}

// One player's screen: the trader he stands at, its shelf, what he carries that it buys (when his screen is open),
// and the job board. The host's goes to the renderer; each joiner's is sent to him (anycareer_net.h).
View build_view(uint8_t* scene, uint8_t* peer, bool open, const std::wstring& status) {
    View v;
    v.placed = T.world.placed;
    v.trader = T.world.trader;
    v.days = T.world.days;
    v.status = status;
    for (int s = 0; s < logic::kShopCount; ++s) v.shops[s] = {shop_placed(s), shop_at(s)};
    v.shop = peer ? shop_here(peer) : -1;
    if (v.shop >= 0) {
        int64_t period = logic::kShops[v.shop].daily ? 1 : kRestockDays;
        v.restock_in = period - T.world.days % period;
        v.next_tier = T.next_tier[v.shop];
        const auto& shelf = T.shelf[v.shop];
        const auto& stock = T.stock[v.shop];
        const auto& sold = shop_sold(v.shop);
        for (size_t i = 0; i < shelf.size() && i < sold.size(); ++i) {
            if (shelf[i].candidate >= int(stock.size())) continue;
            const Candidate& c = stock[size_t(shelf[i].candidate)];
            std::wstring name = wide(c.name);
            if (c.stack > 1) name += L" x" + std::to_wstring(c.stack);
            v.buy.push_back({name, c.price, std::max(0, shelf[i].quantity - sold[i]), int32_t(i), c.id});
        }
    }
    uint8_t* inventory = (open || (T.world.job_state == 1 && !T.world.job_kill)) && T.cash_def ? peer_inventory(peer) : nullptr;
    int n = inventory ? carried(inventory) : 0;
    int have = 0;
    if (T.world.job_state == 1 && !T.world.job_kill && inventory) have = job_items(n, job_def(scene));
    job_rows(v, peer, have);
    if (inventory && open) {
        for (int i = 0; i < n; ++i) {
            if (g_carried[i].def == T.cash_def) { v.cash += rd<int32_t>(g_carried[i].item, off::ammo_count + off::property_value); continue; }
            std::string name, id;
            int32_t count = 1;
            int value = sell_value(g_carried[i], v.shop, &name, &count, &id);
            if (value) v.sell.push_back({wide(name) + (count > 1 ? L" x" + std::to_wstring(count) : L""), value, count, g_carried[i].id, id});
        }
    }
    return v;
}

void publish_view(uint8_t* scene) {
    View v = build_view(scene, host_peer(scene), g_shop_open, T.status);
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
    // Every trader where logic::layout_traders puts him, so joiners (who work it out themselves) see the same spots.
    // Worlds from before layout 3 move theirs once (Hayzeco was already at that gas station in every test world).
    if (g_stations_ready && (!T.world.placed || T.world.layout < 3)) {
        logic::Zone at[logic::kShopCount];
        bool placed[logic::kShopCount];
        if (logic::layout_traders(g_stations, g_places, at, placed)) {
            T.world.placed = true;
            T.world.trader = at[logic::kMechanic];
            for (int s = 1; s < logic::kShopCount; ++s) {
                bool moved = T.world.shops[s].placed != placed[s] || T.world.shops[s].at.x != at[s].x || T.world.shops[s].at.z != at[s].z;
                T.world.shops[s].placed = placed[s];
                T.world.shops[s].at = at[s];
                if (moved) T.world.shops[s].taken.clear();
                log(placed[s] ? 0 : 1, "ANYCAREER_SHOP_PLACED shop=%s ok=%d at=(%.0f, %.0f) moved=%d", logic::kShops[s].id, placed[s] ? 1 : 0, at[s].x, at[s].z, moved ? 1 : 0);
            }
            T.world.layout = 3;
            T.offers_board = -2;
            dirty = true;
            log(0, "ANYCAREER_MECHANIC placed_at=(%.0f, %.0f, %.0f) layout=3", T.world.trader.x, T.world.trader.y, T.world.trader.z);
        }
    }
    AnyWorldTimeSnapshotV1 time;
    if (world_time && world_time->copy(&time) && T.clock.update(time.cycle_factor)) {
        T.world.days = T.clock.days;
        dirty = true;
        log(0, "ANYCAREER_DAY days=%lld", (long long)T.world.days);
    }
    int32_t level = rd<int32_t>(scene, off::server_loot_level);
    bool rebuild = level != T.level || T.stock[logic::kMechanic].empty() || (!T.stock_knows_mods && g_stations_ready);
    if (rebuild || T.world.days != T.day) {
        bool new_level = T.level >= 0 && level != T.level;
        if (rebuild) {
            T.level = level;
            T.stock_knows_mods = g_stations_ready;
            build_stock(scene);
            T.offers_board = -2;   // offers follow the goods
        }
        T.day = T.world.days;
        // Each trader's shelf: picked from his stock by the world seed and his restock number (every kRestockDays
        // days, the Junkyard every day). The Mechanic keeps the seed he always had.
        std::string list;
        bool any_new = false;
        for (int s = 0; s < logic::kShopCount; ++s) {
            int64_t restock = T.world.days / (logic::kShops[s].daily ? 1 : kRestockDays);
            uint64_t seed = uint64_t(uint32_t(T.seed)) ^ (s ? logic::mix(0x5ab0 + uint64_t(s)) : 0);
            T.shelf[s] = logic::pick_stock(T.stock[s].size(), seed, restock, kShelfSize);
            bool new_shelf = restock != shop_restock(s) || new_level;
            if (new_shelf) { shop_restock(s) = restock; shop_sold(s).assign(T.shelf[s].size(), 0); dirty = true; any_new = true; }
            if (shop_sold(s).size() != T.shelf[s].size()) { shop_sold(s).resize(T.shelf[s].size(), 0); dirty = true; }
            list += std::string(" ") + logic::kShops[s].id + "=" + std::to_string(T.stock[s].size()) + "/" + std::to_string(T.shelf[s].size());
        }
        static int stock_logs = 0;
        if (stock_logs++ < 20 || any_new)
            log(0, "ANYCAREER_STOCK loot_level=%d days=%lld stock/shelf:%s", level, (long long)T.world.days, list.c_str());
    }
    // A new job board every in-game day.
    if (T.world.placed && T.world.board != T.world.days) {
        T.world.board = T.world.days;
        for (int s = 0; s < logic::kShopCount; ++s) shop_taken(s).clear();
        dirty = true;
    }
    if (T.world.placed && g_stations_ready && T.offers_board != T.world.board && !T.goods[logic::kMechanic].empty()) refresh_offers();
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
        case Job::take: take(scene, w); break;
        case Job::abandon: abandon(scene, w); break;
        case Job::deliver: deliver(scene, w); break;
        case Job::claim: claim(scene, w); break;
        case Job::test_items: test_items(scene, w); break;
        case Job::teleport: teleport(scene, w); break;
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) { log(2, "ANYCAREER_JOB ok=0 job=%d reason=native_exception", int(w.job)); }
}

// ------------------------------------------------------------------ co-op (anycareer_net.h, steam_messages.h)
// Joiners run AnyCareer too. Each asks the host (poll twice a second, and buy/sell/take/... when he clicks); the host
// does it in this server tick exactly as for itself (same checks, at the joiner's own position and inventory) and
// sends him his screen. Jobs are the team's: one job in hand for the world, any player can deliver it or claim it.
static_assert(net::kShops == logic::kShopCount, "the wire format carries one spot per trader");
steam_messages::Line g_line;                      // under g_net_mutex (server tick on the host; render and input on a joiner)
std::mutex g_net_mutex;

net::View to_net(const View& v) {
    net::View n;
    n.placed = v.placed;
    for (int s = 0; s < logic::kShopCount; ++s) n.shops[s] = {v.shops[s].placed, v.shops[s].at.x, v.shops[s].at.y, v.shops[s].at.z};
    n.shop = int8_t(v.shop); n.cash = v.cash; n.days = v.days; n.restock_in = v.restock_in; n.board = v.board;
    n.job_state = v.job_state; n.job_have = v.job_have; n.job_count = v.job_count; n.job_shop = v.job_shop; n.next_tier = v.next_tier;
    n.job_kill = v.job_kill; n.job_at = {true, v.job_at.x, v.job_at.y, v.job_at.z};
    n.job_text = utf8w(v.job_text); n.status = utf8w(v.status);
    auto rows = [](const std::vector<Row>& from, std::vector<net::Row>& to) {
        for (const auto& r : from) to.push_back({utf8w(r.name), r.id, utf8w(r.button), r.price, r.quantity, r.key, r.off});
    };
    rows(v.buy, n.buy); rows(v.sell, n.sell); rows(v.jobs, n.jobs);
    return n;
}
View from_net(const net::View& n) {
    View v;
    v.placed = n.placed && n.shops[logic::kMechanic].placed;
    v.trader = {n.shops[logic::kMechanic].x, n.shops[logic::kMechanic].y, n.shops[logic::kMechanic].z};
    for (int s = 0; s < logic::kShopCount; ++s) v.shops[s] = {n.shops[s].placed, {n.shops[s].x, n.shops[s].y, n.shops[s].z}};
    v.shop = n.shop; v.cash = n.cash; v.days = n.days; v.restock_in = n.restock_in; v.board = n.board;
    v.job_state = n.job_state; v.job_have = n.job_have; v.job_count = n.job_count; v.job_shop = n.job_shop; v.next_tier = n.next_tier;
    v.job_kill = n.job_kill; v.job_at = {n.job_at.x, n.job_at.y, n.job_at.z};
    v.job_text = wide(n.job_text); v.status = wide(n.status);
    auto rows = [](const std::vector<net::Row>& from, std::vector<Row>& to) {
        for (const auto& r : from) { Row row; row.name = wide(r.name); row.price = r.price; row.quantity = r.quantity; row.key = r.key; row.id = r.id;
                                     row.button = wide(r.button); row.off = r.off; to.push_back(std::move(row)); }
    };
    rows(n.buy, v.buy); rows(n.sell, v.sell); rows(n.jobs, v.jobs);
    return v;
}

// The players in the world with a Steam id (not this PC's), as peer id -> Steam id.
struct RosterEntry { int32_t peer_id; uint64_t steam; };
std::vector<RosterEntry> remote_players(AnySessionPlayersV1& buffer) {
    std::vector<RosterEntry> out;
    buffer = AnySessionPlayersV1{};
    uint32_t flags = 0;
    if (!host.copy_players || !host.copy_players(&buffer, &flags)) return out;
    for (uint32_t i = 0; i < buffer.count && i < ANYAPI_PLAYERS_CAPACITY; ++i) {
        const auto& p = buffer.players[i];
        if ((p.valid_fields & PLAYER_LOCAL) || !(p.valid_fields & PLAYER_STEAM_ID)) continue;
        char id[33]{};
        std::memcpy(id, p.steam_id, sizeof p.steam_id);
        if (uint64_t steam = net::parse_steam_id(id)) out.push_back({p.peer_id, steam});
    }
    return out;
}

Job job_of(uint8_t action) {
    switch (action) {
    case net::kBuy: return Job::buy;
    case net::kSell: return Job::sell;
    case net::kTake: return Job::take;
    case net::kAbandon: return Job::abandon;
    case net::kDeliver: return Job::deliver;
    default: return Job::claim;
    }
}

// A job, with its message line kept for the player it was for (the host's screen, or that joiner's).
void do_job(uint8_t* scene, const Work& w) {
    std::wstring host_status = T.status;
    T.status.clear();
    run_job(scene, w);
    if (w.peer_id >= 0) {
        if (!T.status.empty()) T.peer_status[w.peer_id] = T.status;
        T.status = host_status;
    } else if (T.status.empty()) {
        T.status = host_status;
    }
}

struct Joiner { uint64_t steam = 0; int32_t peer_id = -1; bool open = false, dirty = false, logged = false; ULONGLONG last = 0, sent = 0; net::Rate rate; };
std::vector<Joiner> g_joiners;                    // server thread
uint8_t g_net_in[net::kMaxMessage];               // server thread

void net_host_tick(uint8_t* scene) {
    if (!T.loaded || !T.world.placed) return;
    const ULONGLONG now = GetTickCount64();
    static AnySessionPlayersV1 roster_buffer;     // about 21 KB; server thread only
    static std::vector<RosterEntry> roster;
    static ULONGLONG roster_at = 0;
    if (now - roster_at >= 500) { roster_at = now; roster = remote_players(roster_buffer); }
    if (roster.empty()) { g_joiners.clear(); return; }
    std::vector<Work> asked;
    {
        std::lock_guard lock(g_net_mutex);
        if (!g_line.ready(net::kChannel)) return;
        for (int i = 0; i < 32; ++i) {
            uint32_t size = 0;
            uint64_t sender = 0;
            int got = g_line.receive(g_net_in, sizeof g_net_in, size, sender);
            if (got < 0) { log(2, "ANYCAREER_COOP ok=0 reason=steam_call_fault (co-op off for this session)"); return; }
            if (!got) break;
            const RosterEntry* from = nullptr;
            for (const auto& r : roster) if (sender && r.steam == sender) from = &r;
            net::Request req;
            static int drops = 0;
            if (!from || size > sizeof g_net_in || !net::decode(g_net_in, size, req)) {
                if (drops++ < 20) log(1, "ANYCAREER_COOP dropped reason=%s size=%u", !from ? "not_in_world" : "bad_format", size);
                continue;
            }
            Joiner* j = nullptr;
            for (auto& x : g_joiners) if (x.steam == sender) j = &x;
            if (!j) { g_joiners.push_back({}); j = &g_joiners.back(); j->steam = sender; }
            if (!net::admit(j->rate, now)) continue;
            j->peer_id = from->peer_id; j->open = req.open; j->last = now;
            if (req.action != net::kPoll) {
                asked.push_back({job_of(req.action), double(req.yaw), now, req.key, req.shop, from->peer_id});
                j->dirty = true;
            }
        }
    }
    std::erase_if(g_joiners, [&](const Joiner& j) { return now - j.last > 5000; });
    for (const auto& w : asked) {
        log(0, "ANYCAREER_COOP job=%d peer=%d shop=%d key=%d", int(w.job), w.peer_id, w.shop, w.key);
        do_job(scene, w);
    }
    for (auto& j : g_joiners) {
        if (!j.dirty && now - j.sent < 500) continue;
        Work who{};
        who.peer_id = j.peer_id;
        uint8_t* peer = work_peer(scene, who);
        if (!j.logged) { j.logged = true; log(peer ? 0 : 1, "ANYCAREER_COOP joiner peer=%d found=%d", j.peer_id, peer ? 1 : 0); }
        if (!peer) continue;
        auto it = T.peer_status.find(j.peer_id);
        std::vector<uint8_t> bytes = net::encode(to_net(build_view(scene, peer, j.open, it != T.peer_status.end() ? it->second : std::wstring())));
        j.sent = now; j.dirty = false;
        if (bytes.size() > net::kMaxMessage) { log(1, "ANYCAREER_COOP view_too_big bytes=%u", unsigned(bytes.size())); continue; }
        std::lock_guard lock(g_net_mutex);
        int32_t result = g_line.send(j.steam, bytes.data(), uint32_t(bytes.size()));
        static int sends = 0;
        if (sends++ < 5 || result != 1) log(result == 1 ? 0 : 1, "ANYCAREER_COOP view peer=%d bytes=%u result=%d", j.peer_id, unsigned(bytes.size()), result);
    }
}
void run_net_host(uint8_t* scene) {
    __try { net_host_tick(scene); } __except (EXCEPTION_EXECUTE_HANDLER) { log(2, "ANYCAREER_COOP ok=0 reason=native_exception"); }
}

void hk_tick(uint8_t* scene, uint8_t* server) {
    if (scene && !g_stop) {
        g_last_tick = GetTickCount64();
        run_trader(scene);
        T.server = server;   // after: a new scene resets T
        std::deque<Work> work;
        { std::lock_guard lock(g_mutex); work.swap(g_queue); }
        for (auto& w : work) {
            if (GetTickCount64() - w.posted > 3000) { log(1, "ANYCAREER_JOB ok=0 reason=expired"); continue; }
            do_job(scene, w);
        }
        if (career_mode()) run_net_host(scene);
        if (T.tp_check && GetTickCount64() > T.tp_check) {
            T.tp_check = 0;
            uint8_t* peer = host_peer(scene);
            Vec3 p = peer ? rd<Vec3>(peer, off::peer_position) : Vec3{0, 0, 0};
            log(0, "ANYCAREER_TELEPORT_CHECK now=(%.0f, %.0f, %.0f) target=(%.0f, %.0f, %.0f) off_by_m=%.0f", p.x, p.y, p.z, T.tp_to.x, T.tp_to.y, T.tp_to.z,
                std::hypot(p.x - T.tp_to.x, p.z - T.tp_to.z));
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
bool g_dropoff = false;                           // opened at the delivery's drop-off: the Jobs tab only
int g_shop = 0;                                   // the trader whose screen is open
int g_tab = 0, g_cursor = 0;                      // tabs: 0 Buy, 1 Sell, 2 Jobs
int64_t g_seen_board = -1;
ULONGLONG g_toast_until = 0;
std::wstring g_toast = L"New jobs on the traders' boards";
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

// A joiner's side of co-op (anycareer_net.h): his screen comes from the host. Until it does (or when the host runs no
// AnyCareer), the traders' spots come from the map alone (logic::layout_traders, as the host places them): pins and a
// note at the counter. Render and input threads, under g_ui_mutex.
View g_joiner_layout;
bool g_joiner_layout_done = false;
View g_remote;                                    // the host's last screen for us
ULONGLONG g_remote_at = 0;
uint64_t g_host_steam = 0;
bool remote_fresh() { return g_remote_at && GetTickCount64() - g_remote_at < 3000; }

void joiner_send(const net::Request& r) {
    std::vector<uint8_t> bytes = net::encode(r);
    std::lock_guard lock(g_net_mutex);
    if (g_host_steam && g_line.ready(net::kChannel)) g_line.send(g_host_steam, bytes.data(), uint32_t(bytes.size()));
}

void net_joiner_tick() {
    const ULONGLONG now = GetTickCount64();
    static AnySessionPlayersV1 roster_buffer;     // about 21 KB; render thread only
    static ULONGLONG roster_at = 0, polled = 0;
    if (now - roster_at >= 500) {
        roster_at = now;
        // The host is the player who joined first (the lowest peer id).
        uint64_t host_steam = 0;
        int32_t lowest = INT32_MAX;
        for (const auto& r : remote_players(roster_buffer)) if (r.peer_id < lowest) { lowest = r.peer_id; host_steam = r.steam; }
        if (host_steam != g_host_steam) { g_host_steam = host_steam; g_remote_at = 0; log(0, "ANYCAREER_COOP host_found=%d", host_steam ? 1 : 0); }
    }
    if (!g_host_steam) return;
    if (now - polled >= 500) {
        polled = now;
        net::Request poll;
        poll.action = net::kPoll; poll.shop = int8_t(g_open && !g_dropoff ? g_shop : -1); poll.open = g_open;
        joiner_send(poll);
    }
    static uint8_t buffer[net::kMaxMessage];
    std::lock_guard lock(g_net_mutex);
    for (int i = 0; i < 8; ++i) {
        uint32_t size = 0;
        uint64_t sender = 0;
        int got = g_line.receive(buffer, sizeof buffer, size, sender);
        if (got <= 0) break;
        net::View n;
        if (sender != g_host_steam || size > sizeof buffer || !net::decode(buffer, size, n)) continue;
        bool first = !g_remote_at;
        g_remote = from_net(n);
        g_remote_at = now;
        if (first) log(0, "ANYCAREER_COOP host_screen=1 bytes=%u shop=%d", size, n.shop);
    }
}

// The screen this PC shows: the host's own, a joiner's from the host, or a joiner's map-only pins.
View screen_view() {
    if (hosting()) { std::lock_guard view_lock(g_view_mutex); return g_view; }
    if (remote_fresh()) return g_remote;
    return g_joiner_layout;
}

// The local client scene (F2 free move), as AnyMap reaches it.
uint8_t* local_scene() {
    uint8_t** ref = g_client_ref.load();
    uint8_t* client = ref && anymaker::readable(ref, 16) ? ref[1] : nullptr;
    return client && anymaker::readable(client + off::client_scene, off::scene_free_move + 1) ? client + off::client_scene : nullptr;
}

void post(Job job, int32_t key);

bool close_to(const logic::Zone& at, const Local& l) {
    double dx = l.x - at.x, dz = l.z - at.z;
    return l.ok && dx * dx + dz * dz <= kTradeRange * kTradeRange;
}
// The trader the local player stands at (the nearest when two are close), or -1.
int shop_in_range(const View& v, const Local& l) {
    int best = -1;
    double best_d = 0;
    for (int s = 0; s < logic::kShopCount; ++s) {
        if (!v.shops[s].placed || !close_to(v.shops[s].at, l)) continue;
        double d = std::hypot(l.x - v.shops[s].at.x, l.z - v.shops[s].at.z);
        if (best < 0 || d < best_d) { best = s; best_d = d; }
    }
    return best;
}
constexpr const wchar_t* kBuysText[] = {L"mechanical and electrical parts and items", L"building parts", L"anything, at scrap price",
                                                     L"food, drink and survival supplies", L"medical supplies", L"boat and plane parts", L"parts other mods add",
                                                     L"clothing", L"guns and ammo"};
static_assert(sizeof(kBuysText) / sizeof(kBuysText[0]) == logic::kShopCount, "one buys text per trader");
int tab_count() { return g_shop == logic::kModShop ? 2 : 3; }   // every trader but the Mod Shop has a Jobs tab
bool at_dropoff(const View& v, const Local& l) { return v.job_state && !v.job_kill && close_to(v.job_at, l); }   // kill jobs end at their trader
// Buy and Sell rows are the trader's the host stands at; until the server has caught up with the screen, none.
const std::vector<Row>& tab_rows(const View& v) {
    static const std::vector<Row> none;
    if (g_tab < 2 && v.shop != g_shop) return none;
    return g_tab == 0 ? v.buy : g_tab == 1 ? v.sell : v.jobs;
}

// What a row's button posts.
void act(const Row& r) {
    if (r.off) return;
    if (g_tab == 0) post(Job::buy, r.key);
    else if (g_tab == 1) post(Job::sell, r.key);
    else if (r.key == kKeyAbandon) post(Job::abandon, 0);
    else if (r.key == kKeyDeliver) post(Job::deliver, 0);
    else if (r.key >= kKeyClaim) post(Job::claim, r.key - kKeyClaim);
    else post(Job::take, r.key);
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
    uint32_t count = std::min<uint32_t>(catalog->count(), 20000);
    if (count != g_catalog_seen) {
        // A few hundred catalog entries a frame: reading all of them at once stalled the frame the shop opened
        // (AnyPerformance logged 150-180 ms UI updates in the bulk test).
        static AnyItemDefinitionV1 d;   // about 2.6 KB; render thread only
        uint32_t i = g_catalog_seen;
        for (uint32_t end = std::min(count, g_catalog_seen + 300); i < end; ++i) { d = AnyItemDefinitionV1{}; if (catalog->copy(i, &d)) g_catalog_index.emplace(d.id, i); }
        g_catalog_seen = i;
    }
    // A picture that failed is asked for again a few times (the image service can be busy), then the tile stays.
    static std::unordered_map<std::string, std::pair<int, ULONGLONG>> failed;   // id -> tries, next try
    auto again = failed.find(id);
    if (again != failed.end() && GetTickCount64() < again->second.second) return 0;
    uint64_t texture = 0;
    auto index = g_catalog_index.find(id);
    if (index == g_catalog_index.end()) index = g_catalog_index.find("component:" + id);   // parts are listed as component:<id>
    if (index == g_catalog_index.end()) index = g_catalog_index.find("item:" + id);
    if (index == g_catalog_index.end() && g_catalog_seen < count) return 0;   // not read that far yet: try next frame
    if (index != g_catalog_index.end()) {
        static std::vector<uint8_t> bgra(65536);
        uint32_t required = 0;
        if (images->copy(index->second, bgra.data(), uint32_t(bgra.size()), &required) && required == bgra.size() &&
            gpu->texture(kTextureBase + index->second, 128, 128, 512, bgra.data(), 1))
            texture = kTextureBase + index->second;
    }
    int tries = again != failed.end() ? again->second.first + 1 : 1;
    if (texture || tries >= 3) g_pictures[id] = texture;
    else failed[id] = {tries, GetTickCount64() + 4000};
    static int logs = 0, misses = 0;
    if (texture ? logs++ < 6 : misses++ < 40)
        log(texture ? 0 : 1, "ANYCAREER_PICTURE id=%s in_catalog=%d picture=%d try=%d catalog=%u", id.c_str(), index != g_catalog_index.end() ? 1 : 0, texture ? 1 : 0,
            tries, count);
    return texture;
}

// The Mechanic's pin on AnyMap (when AnyMap is installed; its service may appear after ours, so it is looked up late).
const AnyMapPinsV1* g_pins;
bool g_pin_shown = false;
logic::Zone g_pin_at;
ULONGLONG g_pins_looked = 0;
// The delivery's drop-off pin.
bool g_drop_shown = false;
logic::Zone g_drop_at;
void update_drop_pin(const View* v) {
    if (!g_pins) return;
    bool want = v && v->job_state == 1 && !v->job_kill;
    if (!want) {
        if (g_drop_shown) { g_pins->remove(kModId, "delivery"); g_drop_shown = false; }
        return;
    }
    if (g_drop_shown && g_drop_at.x == v->job_at.x && g_drop_at.z == v->job_at.z) return;
    AnyMapPinV1 pin;
    pin.x = v->job_at.x; pin.z = v->job_at.z; pin.color = 0xff6ac8e8;
    strcpy_s(pin.owner, kModId); strcpy_s(pin.id, "delivery"); strcpy_s(pin.label, "Delivery drop-off");
    g_drop_shown = g_pins->set(&pin);
    g_drop_at = v->job_at;
    log(g_drop_shown ? 0 : 1, "ANYCAREER_MAP delivery_pin_set=%d at=(%.0f, %.0f)", g_drop_shown ? 1 : 0, pin.x, pin.z);
}

// The other traders' pins, one each.
bool g_shop_pin_shown[logic::kShopCount] = {};
logic::Zone g_shop_pin_at[logic::kShopCount];
void update_shop_pins(const View* v) {
    static constexpr uint32_t kColors[] = {0xffe8c66a, 0xffd99a4e, 0xff9a8f80, 0xff7fc96b, 0xffe86a6a, 0xff6a9de8, 0xffb98ae8,
                                                             0xffe89ac8, 0xff8a6a4a};
    static_assert(sizeof(kColors) / sizeof(kColors[0]) == logic::kShopCount, "one pin colour per trader");
    for (int s = 1; s < logic::kShopCount; ++s) {
        const char* id = logic::kShops[s].id;
        if (!v || !v->shops[s].placed) {
            if (g_shop_pin_shown[s]) { g_pins->remove(kModId, id); g_shop_pin_shown[s] = false; }
            continue;
        }
        const logic::Zone& at = v->shops[s].at;
        if (g_shop_pin_shown[s] && g_shop_pin_at[s].x == at.x && g_shop_pin_at[s].z == at.z) continue;
        AnyMapPinV1 pin;
        pin.x = at.x; pin.z = at.z; pin.color = kColors[s];
        strcpy_s(pin.owner, kModId); strcpy_s(pin.id, id);
        snprintf(pin.label, sizeof pin.label, "%s (trader)", logic::kShops[s].name);
        g_shop_pin_shown[s] = g_pins->set(&pin);
        g_shop_pin_at[s] = at;
        log(g_shop_pin_shown[s] ? 0 : 1, "ANYCAREER_MAP pin=%s set=%d at=(%.0f, %.0f)", id, g_shop_pin_shown[s] ? 1 : 0, pin.x, pin.z);
    }
}

void update_pin(const View* v) {
    ULONGLONG now = GetTickCount64();
    if (!g_pins && services_api && now - g_pins_looked > 3000) {
        g_pins_looked = now;
        auto p = static_cast<const AnyMapPinsV1*>(services_api->query("anymap.pins", 1));
        if (p && p->struct_size == sizeof(*p) && p->version == 1 && p->set && p->remove) { g_pins = p; log(0, "ANYCAREER_MAP pins=1"); }
    }
    if (!g_pins) return;
    update_drop_pin(v);
    update_shop_pins(v);
    if (!v || !v->placed) {
        if (g_pin_shown) { g_pins->remove(kModId, "mechanic"); g_pin_shown = false; }
        return;
    }
    if (g_pin_shown && g_pin_at.x == v->trader.x && g_pin_at.z == v->trader.z) return;
    AnyMapPinV1 pin;
    pin.x = v->trader.x; pin.z = v->trader.z; pin.color = 0xffe8c66a;
    strcpy_s(pin.owner, kModId); strcpy_s(pin.id, "mechanic"); strcpy_s(pin.label, "Hayzeco Autoparts (trader)");
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
    text(x + 20, y + 14, w - 220, 30, g_dropoff ? L"Delivery" : wide(logic::kShops[g_shop].name), 22, 0xfff5f7fa, ANY_GPU_NOWRAP | ANY_GPU_VCENTER | ANY_GPU_BOLD);
    if (!g_dropoff) text(x + w - 230, y + 14, 180, 30, L"Cash: " + std::to_wstring(v.cash), 18, 0xffe8c66a);
    const wchar_t* tabs[] = {L"Buy", L"Sell", L"Jobs"};
    for (int i = g_dropoff ? 2 : 0; i < tab_count(); ++i) {
        float tx = x + 20 + (g_dropoff ? 0 : i) * 110;
        bool hover = Area{tx, y + 56, 100, 30, Hit::tab, i}.contains(g_mouse_x, g_mouse_y);
        box(ANY_GPU_ROUND_RECT, tx, y + 56, 100, 30, g_tab == i ? 0xff3a6ea5 : hover ? 0xff3a3f48 : 0xff2a2e35, 5);
        g_areas.push_back({tx, y + 56, 100, 30, Hit::tab, i});
        text(tx, y + 56, 100, 30, tabs[i], 16, 0xfff5f7fa, ANY_GPU_NOWRAP | ANY_GPU_VCENTER | ANY_GPU_CENTER);
    }
    const auto& rows = tab_rows(v);
    float list_y = y + 100, row_h = 44;
    int budget = 3;
    int visible = std::max(1, int((h - 100 - 90) / row_h));
    g_cursor = rows.empty() ? 0 : std::clamp(g_cursor, 0, int(rows.size()) - 1);
    if (g_cursor < g_first) g_first = g_cursor;
    if (g_cursor >= g_first + visible) g_first = g_cursor - visible + 1;
    g_first = std::clamp(g_first, 0, std::max(0, int(rows.size()) - visible));
    int first = g_first;
    if (rows.empty())
        text(x + 20, list_y, w - 40, row_h, g_tab < 2 && v.shop != g_shop ? L"..." : g_tab == 0 && v.next_tier > 1
             ? L"Nothing yet: these goods turn up from loot tier " + std::to_wstring(v.next_tier) + L" (after bunker " + std::to_wstring(v.next_tier - 1) + L")."
             : g_tab == 0 ? L"Nothing on the shelf right now."
             : g_tab == 1 ? L"Nothing you carry sells here (this trader buys " + std::wstring(kBuysText[g_shop]) + L")."
             : L"No jobs on the board right now. New ones come every day.", 15, 0xffaeb7c2);
    for (int i = first; i < int(rows.size()) && i < first + visible; ++i) {
        float ry = list_y + float(i - first) * row_h;
        if (i == g_cursor) box(ANY_GPU_ROUND_RECT, x + 14, ry, w - 28, row_h - 2, 0xff2f4f73, 4);
        g_areas.push_back({x + 14, ry, w - 160, row_h - 2, Hit::row, i});
        const Row& r = rows[size_t(i)];
        bool out = (g_tab == 0 && r.quantity <= 0) || r.off;
        if (uint64_t texture = picture(r.id, &budget)) {
            AnyGpuCommandV1 c;
            c.kind = ANY_GPU_IMAGE; c.texture = texture; c.rect[0] = x + 20; c.rect[1] = ry + 2; c.rect[2] = c.rect[3] = row_h - 6;
            c.source[2] = c.source[3] = 128; c.opacity = out ? 0.4f : 1;
            gpu->emit(&c);
        } else if (!r.id.empty()) {   // no picture for it (yet): a tile with its first letter, so no row looks broken
            float side = row_h - 6;
            box(ANY_GPU_ROUND_RECT, x + 20, ry + 2, side, side, out ? 0xff24282e : 0xff2f3540, 6);
            std::wstring letter = g_tab < 2 && !r.name.empty() ? r.name.substr(0, 1) : L"?";
            text(x + 20, ry + 2, side, side, letter, 18, out ? 0xff7d848c : 0xffc9d1da, ANY_GPU_NOWRAP | ANY_GPU_VCENTER | ANY_GPU_CENTER | ANY_GPU_BOLD);
        }
        float name_w = g_tab == 2 ? w - 190 - row_h : w - 260 - row_h;
        text(x + 24 + row_h, ry, name_w, row_h, r.name, g_tab == 2 ? 13 : 15, out ? 0xff7d848c : 0xfff5f7fa,
             g_tab == 2 ? ANY_GPU_VCENTER : ANY_GPU_NOWRAP | ANY_GPU_VCENTER);
        if (g_tab == 0) text(x + w - 220, ry, 80, row_h, out ? L"sold out" : std::to_wstring(r.quantity) + L" left", 13, 0xffaeb7c2);
        // The button that buys or sells this row, with the price on it.
        Area button{x + w - 140, ry + 2, 122, row_h - 6, Hit::action, i};
        bool hover = !out && button.contains(g_mouse_x, g_mouse_y);
        box(ANY_GPU_ROUND_RECT, button.x, button.y, button.w, button.h, out ? 0xff2a2e35 : hover ? 0xff4f8a3c : 0xff3b6b2e, 4);
        std::wstring label = !r.button.empty() ? r.button : g_tab == 0 && r.quantity <= 0 ? L"sold out"
                             : (g_tab == 0 ? L"Buy  " : L"Sell  ") + std::to_wstring(r.price);
        text(button.x, button.y, button.w, button.h, label, 14,
             out ? 0xff7d848c : 0xfffff3c4, ANY_GPU_NOWRAP | ANY_GPU_VCENTER | ANY_GPU_CENTER);
        if (!out) g_areas.push_back(button);
    }
    std::wstring restock = g_shop == logic::kJunkyard ? L"New junk every day (day " + std::to_wstring(v.days) + L" of this world)" : L"Restock in " + std::to_wstring(v.restock_in) + (v.restock_in == 1 ? L" day" : L" days") + L" (day " +
                           std::to_wstring(v.days) + L" of this world)";
    text(x + 20, y + h - 84, w - 40, 22, v.status, 15, 0xff9fd39a);
    if (g_tab == 2 && v.job_state == 1 && v.job_kill)
        restock = L"Kills so far: " + std::to_wstring(v.job_have) + L" of " + std::to_wstring(v.job_count) + L". Only kills by you count.";
    else if (g_tab == 2 && v.job_state == 1)
        restock = L"Bring " + v.job_text + L". The blue pin on the map marks the drop-off." + (kDevKeys ? L" (Test: F5 drops the items.)" : L"");
    else if (g_tab == 2 && v.job_state == 2)
        restock = L"Pick one reward.";
    else if (g_tab == 2)
        restock = L"Take one job at a time. New jobs every day (day " + std::to_wstring(v.days) + L").";
    text(x + 20, y + h - 58, w - 40, 20, restock, 13, 0xffaeb7c2);
    text(x + 20, y + h - 34, w - 40, 20, L"Click a button (or Up/Down and Enter)   Wheel scrolls   Esc closes", 13, 0xffaeb7c2);
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
    const bool host_here = hosting();
    if (!host_here && !g_joiner_layout_done && g_stations_ready) {
        logic::Zone at[logic::kShopCount];
        bool placed[logic::kShopCount];
        if (logic::layout_traders(g_stations, g_places, at, placed)) {
            g_joiner_layout.placed = true;
            g_joiner_layout.trader = at[logic::kMechanic];
            for (int s = 0; s < logic::kShopCount; ++s) g_joiner_layout.shops[s] = {placed[s], at[s]};
            log(0, "ANYCAREER_JOINER traders=1 mechanic=(%.0f, %.0f)", at[logic::kMechanic].x, at[logic::kMechanic].z);
        }
        g_joiner_layout_done = true;
    }
    if (!host_here && g_ready && gameplay() && career_mode()) net_joiner_tick();
    bool live = g_ready && f->focused && gameplay() && career_mode() && (host_here || g_joiner_layout.placed || remote_fresh());
    if (!live) { if (g_open) close_shop(); if (!host_here) update_pin(nullptr); return; }
    ULONGLONG now = GetTickCount64();
    if (now - g_local_at > 100) {
        g_local = local_player();
        g_local_at = now;
        uint8_t* scene = local_scene();
        uint8_t* actor = scene && anymaker::readable(scene + off::scene_authority_actor, 8) ? rd<uint8_t*>(scene, off::scene_authority_actor) : nullptr;
        g_client_actor_id = actor && anymaker::readable(actor + off::client_actor_id, 4) ? rd<int32_t>(actor, off::client_actor_id) : -3;
    }
    View v = screen_view();
    const bool can_trade = host_here || remote_fresh();
    if (g_open && !(g_dropoff ? at_dropoff(v, g_local) : shop_in_range(v, g_local) == g_shop)) close_shop();
    update_pin(&v);
    if (v.board >= 0 && v.board != g_seen_board) {   // a new job board (not the one already there when the world loaded)
        if (g_seen_board >= 0 && v.job_state == 0) { g_toast = L"New jobs on the traders' boards"; g_toast_until = now + 8000; }
        g_seen_board = v.board;
    }
    if (g_open) { host.capture_input(1); draw_shop(f, v); return; }
    if (now < g_free_move_logs_until) {   // what the game does with the flag, once a second for 5 s after F2
        static ULONGLONG last = 0;
        uint8_t* scene = local_scene();
        uint8_t* actor = scene ? rd<uint8_t*>(scene, off::scene_authority_actor) : nullptr;
        if (scene && now - last >= 1000) {
            last = now;
            Mat34 a = actor && anymaker::readable(actor + off::client_actor_transform, sizeof(Mat34)) ? rd<Mat34>(actor, off::client_actor_transform) : Mat34{};
            Mat34 m = rd<Mat34>(scene, off::scene_free_move_transform);
            log(0, "ANYCAREER_FREE_MOVE_CHECK flag=%d character=(%.0f, %.0f, %.0f) free_move_at=(%.0f, %.0f, %.0f) speed=%.2f", rd<bool>(scene, off::scene_free_move) ? 1 : 0,
                a.t[0], a.t[1], a.t[2], m.t[0], m.t[1], m.t[2], rd<double>(scene, off::scene_free_move + 2));
        }
    }
    if (g_free_move) {
        uint8_t* scene = local_scene();
        g_free_move = scene && rd<bool>(scene, off::scene_free_move);
        if (g_free_move) text(16, float(f->height) - 40, 400, 24, L"Free move ON (F2 to land)", 15, 0xffe8c66a);
    }
    if (!v.placed || !g_local.ok) return;
    int here = shop_in_range(v, g_local);
    float w = 520, x = (float(f->width) - w) / 2;
    if (now < g_toast_until) {
        float y = float(f->height) * 0.2f;
        box(ANY_GPU_ROUND_RECT, x, y, w, 34, 0xd0181b20, 6);
        text(x, y, w, 34, g_toast, 16, 0xffe8c66a, ANY_GPU_NOWRAP | ANY_GPU_VCENTER | ANY_GPU_CENTER);
    }
    std::wstring prompt;
    if (here >= 0 && !can_trade) prompt = wide(logic::kShops[here].name) + L": waiting for the host's AnyCareer (0.0.8 or later)";
    else if (here >= 0) prompt = L"Press " + key_name() + L" to trade with " + (here == logic::kMechanic ? L"" : L"the ") + wide(logic::kShops[here].name);
    else if (at_dropoff(v, g_local)) prompt = L"Press " + key_name() + (v.job_state == 1 ? L" to deliver" : L" to pick your reward");
    if (!prompt.empty()) {
        float y = float(f->height) * 0.62f;
        box(ANY_GPU_ROUND_RECT, x, y, w, 34, 0xd0181b20, 6);
        text(x, y, w, 34, prompt, 16, 0xfff5f7fa, ANY_GPU_NOWRAP | ANY_GPU_VCENTER | ANY_GPU_CENTER);
    }
    if (v.job_state == 1 && v.job_kill) {
        std::wstring hint = L"Kill job: " + std::to_wstring(v.job_have) + L" of " + v.job_text + L". Then back to the " + wide(logic::kShops[v.job_shop].name) + L".";
        text(x - 100, 8, w + 200, 22, hint, 14, 0xff9fd8ec, ANY_GPU_NOWRAP | ANY_GPU_VCENTER | ANY_GPU_CENTER);
    } else if (v.job_state == 1 && !at_dropoff(v, g_local)) {
        std::wstring hint = L"Delivery: " + v.job_text + L" (" + std::to_wstring(v.job_have) + L"/" + std::to_wstring(v.job_count) + L"), " +
                            wide(logic::direction_text(g_local.x, g_local.z, g_local.yaw, v.job_at.x, v.job_at.z));
        text(x - 100, 8, w + 200, 22, hint, 14, 0xff9fd8ec, ANY_GPU_NOWRAP | ANY_GPU_VCENTER | ANY_GPU_CENTER);
    } else if (v.job_state == 2) {
        text(x - 100, 8, w + 200, 22, (v.job_kill ? L"Kill job done: pick your reward in the " : L"Delivered: pick your reward in the ") + wide(logic::kShops[v.job_shop].name) +
             L" Jobs tab", 14, 0xff9fd8ec, ANY_GPU_NOWRAP | ANY_GPU_VCENTER | ANY_GPU_CENTER);
    }
}

// ------------------------------------------------------------------ input
// F2: the game's own free move, flipped on the local client scene (the flag the debug menu's checkbox sets).
void toggle_free_move() {
    uint8_t* scene = hosting() ? local_scene() : nullptr;
    if (!scene) { log(1, "ANYCAREER_FREE_MOVE ok=0 reason=%s", hosting() ? "no_client_scene" : "not_hosting"); return; }
    bool on = !rd<bool>(scene, off::scene_free_move);
    // As the game's own toggle does (client_scene.on_action_digital_begin): free move starts from the character's
    // transform. Test 8 flipped only the flag, so the character jumped to a stale transform.
    if (on) {
        uint8_t* actor = anymaker::readable(scene + off::scene_authority_actor, 8) ? rd<uint8_t*>(scene, off::scene_authority_actor) : nullptr;
        if (!actor || !anymaker::readable(actor + off::client_actor_transform, sizeof(Mat34))) {
            log(1, "ANYCAREER_FREE_MOVE ok=0 reason=no_local_character");
            return;
        }
        Mat34 m = rd<Mat34>(actor, off::client_actor_transform);
        std::memcpy(scene + off::scene_free_move_transform, &m, sizeof m);
        log(0, "ANYCAREER_FREE_MOVE from=(%.0f, %.0f, %.0f) speed=%.2f", m.t[0], m.t[1], m.t[2], rd<double>(scene, off::scene_free_move + 2));
    }
    *reinterpret_cast<bool*>(scene + off::scene_free_move) = on;
    g_free_move = on;
    g_free_move_logs_until = GetTickCount64() + 5000;
    log(0, "ANYCAREER_FREE_MOVE on=%d", on ? 1 : 0);
}

void post(Job job, int32_t key) {
    if (!hosting()) {   // a joiner: the host does it (anycareer_net.h)
        net::Request r;
        switch (job) {
        case Job::buy: r.action = net::kBuy; break;
        case Job::sell: r.action = net::kSell; break;
        case Job::take: r.action = net::kTake; break;
        case Job::abandon: r.action = net::kAbandon; break;
        case Job::deliver: r.action = net::kDeliver; break;
        case Job::claim: r.action = net::kClaim; break;
        default: return;   // the test keys are the host's
        }
        r.shop = int8_t(g_shop); r.open = g_open; r.key = key; r.yaw = float(g_local.yaw);
        joiner_send(r);
        log(0, "ANYCAREER_COOP asked action=%d shop=%d key=%d", int(r.action), g_shop, key);
        return;
    }
    std::lock_guard lock(g_mutex);
    if (g_queue.size() < 4) g_queue.push_back({job, g_local.yaw, GetTickCount64(), key, g_shop});
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
            View v = screen_view();
            const auto& rows = tab_rows(v);
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
                else if (a.index < int(rows.size())) { g_cursor = a.index; act(rows[size_t(a.index)]); }
                break;
            }
            return 1;
        }
        if (e->key < 256 && e->kind == ANY_KEY_UP) held[e->key] = false;
        if (e->kind != ANY_KEY_DOWN || e->key >= 256) return 1;
        uint32_t k = e->key;
        bool repeat = held[k];
        held[k] = true;
        View v = screen_view();
        const auto& rows = tab_rows(v);
        if (k == VK_ESCAPE || (k == trade && !repeat)) close_shop();
        else if (k == VK_UP || k == 'W') g_cursor = std::max(0, g_cursor - 1);
        else if (k == VK_DOWN || k == 'S') g_cursor = std::min(std::max(0, int(rows.size()) - 1), g_cursor + 1);
        else if ((k == VK_LEFT || k == VK_RIGHT || k == 'A' || k == 'D' || k == VK_TAB) && !repeat && !g_dropoff) {
            g_tab = (g_tab + (k == VK_LEFT || k == 'A' ? tab_count() - 1 : 1)) % tab_count(); g_cursor = 0; g_first = 0;
        } else if ((k == VK_RETURN || k == VK_SPACE || k == 'E') && !repeat && g_cursor < int(rows.size()))
            act(rows[size_t(g_cursor)]);
        return 1;
    }
    if ((e->kind != ANY_KEY_DOWN && e->kind != ANY_KEY_UP) || e->key >= 256) return 0;
    bool& was = held[e->key];
    if (e->kind == ANY_KEY_UP) { bool consume = was; was = false; return consume ? 1 : 0; }
    bool dev = kDevKeys && (e->key == VK_F2 || e->key == VK_F3 || e->key == VK_F4 || e->key == VK_F5 || e->key == VK_F6);
    if (!dev && e->key != trade) return 0;
    if (!g_ready || !gameplay()) return 0;
    if (was) return 1;   // a held key repeats; one press, one action
    if (e->key == trade) {
        View v = screen_view();
        if (!career_mode() || !(hosting() || remote_fresh())) return 0;
        int shop = shop_in_range(v, g_local);
        if (shop < 0 && !at_dropoff(v, g_local)) return 0;
        was = true;
        g_open = true; g_shop_open = true; g_dropoff = shop < 0; g_shop = std::max(shop, 0);
        g_tab = shop < 0 || (shop == v.job_shop && shop != logic::kModShop && v.job_state == 2) ? 2 : 0; g_cursor = 0; g_first = 0;
        host.capture_input(1);
        centre_pointer();
        log(0, "ANYCAREER_SHOP opened=1 at=%s buy_rows=%d job_rows=%d", shop < 0 ? "dropoff" : logic::kShops[shop].id, int(v.buy.size()), int(v.jobs.size()));
        return 1;
    }
    was = true;
    if (e->key == VK_F2) { toggle_free_move(); return 1; }
    if (!hosting()) { log(1, "ANYCAREER_JOB ok=0 reason=not_hosting (the host runs these)"); return 1; }
    g_local = local_player();
    if (e->key == VK_F5) {
        View v;
        { std::lock_guard view_lock(g_view_mutex); v = g_view; }
        if (v.job_state != 1 || v.job_kill) { g_toast = L"F5 needs a delivery job: take one in a trader's Jobs tab"; g_toast_until = GetTickCount64() + 5000; return 1; }
    }
    post(e->key == VK_F6 ? Job::teleport : e->key == VK_F3 ? Job::give_cash : e->key == VK_F5 ? Job::test_items : Job::price_list, 0);
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
        // Cash goes into the loot pool only when the mod can set how much a find holds.
        void** init_cell = route::cell(bind::ammo_box_init);
        bool loot = init_cell && h_ammo_init.install(init_cell, (void*)&hk_ammo_init);
        log(loot ? 0 : 1, "ANYCAREER_LOOT hook=%d%s", loot ? 1 : 0, loot ? " (Cash is found in houses, offices and shops)" : " (no Cash in loot this session)");
        if (!write_cash_definition(loot)) { h_ammo_init.remove((void*)&hk_ammo_init); log(2, "ANYCAREER_OFF reason=no_cash_definition"); return; }
        if (!h_tick.install(tick, (void*)&hk_tick) || !h_add_definitions.install(add, (void*)&hk_add_definitions)) {
            h_tick.remove((void*)&hk_tick);
            log(2, "ANYCAREER_OFF reason=hook_cell_not_writable");
            return;
        }
        teleport_event = reinterpret_cast<teleport_t>(route::function(bind::server_on_teleport));
        g_client_ref = static_cast<uint8_t**>(global(bind::g_client));
        // Kill jobs need the zombie damage hook; without it the board only has deliveries.
        void** zombie_cell = route::cell(bind::zombie_damage);
        bool kills = zombie_cell && h_zombie_damage.install(zombie_cell, (void*)&hk_zombie_damage);
        log(kills ? 0 : 1, "ANYCAREER_KILLS hook=%d%s teleport=%d free_move=%d", kills ? 1 : 0, kills ? "" : " (no kill jobs this session)",
            teleport_event ? 1 : 0, g_client_ref.load() ? 1 : 0);
        g_ready = true;
        log(0, "ANYCAREER_READY after_s=%d cash_file=%s draw=%d dev_keys=%d: in Survival the Mechanic waits at a gas station (Buy, Sell, Jobs)%s",
            attempt, g_cash_json.c_str(), gpu ? 1 : 0, kDevKeys ? 1 : 0,
            kDevKeys ? "; F3 gives 250 Cash (host), F4 writes the price list, F5 drops a delivery job's items, F2 free move, F6 teleport (tests)" : "");
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
    out->shutdown = [](void*) {
        g_stop = true;
        h_zombie_damage.remove((void*)&hk_zombie_damage);
        h_ammo_init.remove((void*)&hk_ammo_init);
        h_add_definitions.remove((void*)&hk_add_definitions);
        h_tick.remove((void*)&hk_tick);
    };
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
