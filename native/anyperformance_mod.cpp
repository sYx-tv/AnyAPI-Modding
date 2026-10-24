// AnyPerformance: finds and smooths out hitches and slow-motion stalls.
//
// Measure: the game's per-frame work is timed by hooking (call through, never changed) the functions it is made of:
// client.tick (the whole client update of a frame), inside it client_scene.tick (simulation and the client physics
// step) and client_scene.tick_loading (tile streaming), inside that the attach of a finished terrain tile
// (client_scene.environment_tile_task.integrate) and vehicle mesh rebuilds (client_scene.vehicle.container.
// _update_tasks). The time between two client.tick calls is the frame time. On the host the server thread is timed
// too: server.tick and its spacing, and server tile streaming. Every frame slower than the hitch threshold is logged
// with what it spent its time on, and a summary (fps, 1% low, hitches, server tick pacing) is logged every 30 s.
//
// Slow motion: the client draws the world from server ticks with its own playback clock (inside client.tick:
// client+0xec8 playback tick, +0xed0 buffer in ticks, +0xf58 newest server tick). When server ticks stop arriving
// it slows the world down; when they arrive in a burst it speeds up, up to 4x, to catch up. Every such episode is
// logged with the worst server tick inside it and what that tick spent its time on (tile streaming, bunkers,
// loose items, vehicles, physics, network, autosave).
//
// Fix 1, sim smoothing (off by default): changes two constants of that playback clock in the game's own code, the
// smallest buffer (1 tick in the game; 2 or 4 here, so short server stalls are absorbed instead of slowing the
// world) and the fastest catch-up (4x in the game; 2x or 1.5x here). The cost is 17 to 50 ms more delay on screen.
//
// Fix 2: the game's server loop (server_tick) runs 60 ticks a second and waits between ticks with Sleep(1). Windows'
// default timer ticks every 15.6 ms, so that wait can last up to 15.6 ms: ticks arrive late and then bunch up to
// catch up, which shows as stutter on vehicles and physics in solo and when hosting. "Precise timer" asks Windows
// for a 1 ms timer (timeBeginPeriod(1)), as most games do. The summary's server spacing jitter shows the effect.
//
// Fix 3, spread wreck loot (on by default): a terrain tile that loads on the host fills all its wrecks with loot in
// one server tick (about 7 ms a wreck); the mod lets 8 ms of it run per tick and fills the rest over the next ticks.
//
// Fix 4, distant container items (on by default): containers on parked vehicles over 50 m away place their items
// every 8th frame instead of every frame (see hk_inventory).
//
// Settings (AnyHelpers, optional): sim smoothing, precise timer, hitch threshold, FPS overlay, spread wreck loot,
// profiler, distant container items.
//
// EXPERIMENTAL. Hooks game functions through the experimental SDK (anyperformance_bindings.h): Anymaker 0.1.27 /
// Steam build 26113663 only. Read anymaker_modding.log.
#define NOMINMAX
#include <windows.h>
#include <timeapi.h>
#include <tlhelp32.h>
#include "anyapi_services_v1.h"
#include "anyapi_gpu_draw_v1.h"
#include "anyapi_ui_state_v1.h"
#include "anyhelpers_settings_v1.h"
#include "anyapi_experimental.hpp"
#include "anyperformance_bindings.h"
#include "anyperformance_logic.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <condition_variable>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <cwchar>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>
#include <string>

namespace anyperformance {
namespace {
constexpr const char* kModId = "anyperformance";
constexpr double kSummarySeconds = 30;
constexpr double kPauseMs = 2000;       // a gap this long is alt-tab, a breakpoint or a dialog, not a hitch
constexpr int kMaxHitchLogs = 300;      // per session; summaries keep going after that

AnyModHostV1 host;
const AnyGpuDrawV1* gpu;
const AnyUiStateV1* ui;
const AnyHelpersSettingsV1* settings;
uint64_t g_tokens[7], g_settings_revision = ~0ull;
std::atomic<bool> g_stop{false};

// Settings (defaults when AnyHelpers is absent).
std::atomic<bool> g_precise_timer{true}, g_overlay{false};
std::atomic<double> g_hitch_ms{50};
std::atomic<int> g_smoothing{0};         // 0 game, 1 light, 2 strong
std::atomic<bool> g_profiler{false};      // sampling profiler (anyperformance_profiler.inc)
bool g_timer_on;                         // timeBeginPeriod(1) is in effect (main thread)

LARGE_INTEGER g_qpc_freq;
double now_ms() { LARGE_INTEGER t; QueryPerformanceCounter(&t); return double(t.QuadPart) * 1000.0 / double(g_qpc_freq.QuadPart); }

void log(int level, const char* fmt, ...) {
    if (!host.log) return;
    char buf[1024];
    va_list ap; va_start(ap, fmt); vsnprintf(buf, sizeof buf, fmt, ap); va_end(ap);
    host.log(uint32_t(level), kModId, buf);
}

// ------------------------------------------------------------------ main-thread state (client hooks only)
FrameParts g_parts;                      // the frame in progress
std::atomic<DWORD> g_main_thread{0};     // set by the first client.tick
bool g_parts_loading;                    // the loading flag client.tick passed on to tick_loading this frame
double g_frame_start = -1, g_window_start = -1;
Window<16384> g_frames;                  // frame times of this summary window
FrameWindow g_frame_window;              // frame parts of this summary window (not on loading screens)
int g_hitches, g_loading_hitches, g_hitch_logs, g_tiles_window;
double g_worst_hitch, g_tile_max_window;

// Client playback clock (client.tick). Layout read from the 0.1.25 code of client.tick (unchanged in 0.1.27), which updates it.
namespace clock_off {
constexpr ptrdiff_t play = 0xec8;        // f64: the server tick being shown (fractional)
constexpr ptrdiff_t buffer = 0xed0;      // f64: how many ticks behind the newest one it aims to show (clamped 1..16)
}  // namespace clock_off
SimEpisode g_episode;
double g_episode_buffer_max;
std::atomic<double> g_sim_rate{1};       // smoothed playback rate, for the overlay

// ------------------------------------------------------------------ server-thread state
std::mutex g_server_mutex;               // pacing and the worst tick are written on the server thread, read on main
TickPacing g_pacing;
double g_server_last_start = -1, g_server_tile_ms_window;
std::atomic<double> g_server_last_ms{0};
std::atomic<DWORD> g_server_thread{0};
ServerParts g_tick;                      // the server tick in progress (server thread only)
ServerParts g_worst_tick;                // the slowest tick since the main thread last took it (g_server_mutex)
ServerWindow g_server_window;            // every tick of this summary window (g_server_mutex)

// Takes the slowest server tick since the last call. Main thread.
ServerParts take_worst_tick() {
    std::lock_guard lock(g_server_mutex);
    ServerParts w = g_worst_tick; g_worst_tick = ServerParts{};
    return w;
}
bool on_server_thread() { return GetCurrentThreadId() == g_server_thread.load(std::memory_order_relaxed); }

// Steps timed one by one inside a server tick (bunker loading and terrain streaming) or a frame (client.tick outside the simulation and
// streaming). Every gc-x64 argument is a pointer (a result too, through rcx), so one six-pointer hook fits them all:
// it passes on what it got, and slots the callee does not take are never read.
struct StepSpec { const anymaker::func_desc* desc; const char* name; bool server; };
constexpr StepSpec kSteps[] = {
    {&bind::entrance_load, "entrance load", true},
    {&bind::entrance_create, "entrance create", true},
    {&bind::entrance_post_load, "entrance physics", true},
    {&bind::dungeon_load_static, "bunker static load", true},
    {&bind::dungeon_load_static_cells, "bunker cells", true},
    {&bind::dungeon_create, "bunker physics shape", true},
    {&bind::dungeon_generate_navmesh, "bunker navmesh", true},
    {&bind::dungeon_load_dynamics, "bunker objects load", true},
    {&bind::dungeon_tick_layers, "bunker layers", true},
    {&bind::dungeon_load_layer, "layer load", true},
    {&bind::dungeon_populate, "layer populate (loot, creatures, vehicles)", true},
    {&bind::entrance_save, "entrance save", true},
    {&bind::dungeon_save_layer, "layer save", true},
    {&bind::dungeon_save_dynamics, "bunker objects save", true},
    {&bind::dungeon_save_static, "bunker static save", true},
    {&bind::dungeon_destroy, "bunker unload", true},
    {&bind::entrance_destroy, "entrance unload", true},
    {&bind::dungeon_from_data_static, "static data to objects", true},
    {&bind::dungeon_from_data_dynamics, "object data to objects", true},
    {&bind::load_objects_integrate, "objects into the world", true},
    {&bind::read_json_dungeon, "bunker save file parse", true},
    {&bind::read_json_entrance, "entrance save file parse", true},
    {&bind::tile_task_integrate_server, "terrain tile into the world", true},
    {&bind::tile_populate_loot_zones, "terrain loot zones", true},
    {&bind::vehicle_generate_loot, "wreck loot", true},
    {&bind::loot_tables, "wreck loot tables", true},
    {&bind::loot_required, "wreck required loot", true},
    {&bind::loot_floor, "wreck floor loot", true},
    {&bind::loot_furniture, "wreck furniture loot", true},
    {&bind::loot_furniture_table, "furniture loot table", true},
    {&bind::loot_position, "furniture loot placement", true},
    {&bind::item_create, "item creation", true},
    {&bind::item_store, "item stored in a container", true},
    {&bind::tile_loaded_event, "tile loaded event", true},
    {&bind::tile_save_level_1, "terrain tile save (near)", true},
    {&bind::tile_save_level_2, "terrain tile save (mid)", true},
    {&bind::tile_save_level_3, "terrain tile save (far)", true},
    {&bind::tile_destroy, "terrain tile unload", true},
    {&bind::nav_tile_create, "navmesh tile start", true},
    {&bind::nav_tile_destroy, "navmesh tile unload", true},
    {&bind::tiles_in_range, "tiles in range", true},
    {&bind::tile_integrate_physics, "terrain tile physics", true},
    {&bind::physics_add_object, "physics bodies added", true},
    {&bind::tile_actor_spawn_zones, "creature spawn zones", true},
    {&bind::vehicle_sleeping_actors, "sleeping creatures in wrecks", true},
    {&bind::navmesh_add_tile, "navmesh tile add", true},
    {&bind::vehicle_bunker_keycard, "wreck keycard", true},
    {&bind::vehicle_loot_deaddrop, "dead-drop loot", true},
    {&bind::save_game_inner, "world save", true},
    {&bind::srv_creatures, "creatures (spawning)", true},
    {&bind::srv_paths, "creature pathfinding", true},
    {&bind::srv_actors, "creatures and players", true},
    {&bind::srv_actors_post, "creatures and players (physics)", true},
    {&bind::srv_projectiles, "projectiles", true},
    {&bind::srv_ocean, "ocean", true},
    {&bind::srv_weather, "weather", true},
    {&bind::srv_pseudo, "pseudo objects", true},
    {&bind::srv_collisions, "collision events", true},
    {&bind::srv_explosions, "explosions", true},
    {&bind::srv_transmissions, "radio transmissions", true},
    {&bind::apply_update, "network update", false},
    {&bind::client_network_state, "network state", false},
    {&bind::client_update_ui, "UI update", false},
    {&bind::client_peers_tick, "players", false},
    {&bind::client_peer_data_tick, "player data", false},
    {&bind::client_authority_update, "authority update", false},
    {&bind::cl_vehicles, "vehicles", false},
    {&bind::cl_actors, "creatures and players", false},
    {&bind::cl_particles, "particles", false},
    {&bind::cl_environment, "environment", false},
    {&bind::cl_lights, "lights", false},
    {&bind::cl_weather, "weather particles", false},
    {&bind::cl_actors_post, "creatures and players (physics)", false},
    {&bind::cl_actor_inv, "inventories", false},
    {&bind::cl_daylight, "daylight", false},
    {&bind::cl_interactions, "interaction prompts", false},
    {&bind::cl_overlay, "name tags and markers", false},
};
constexpr int kStepCount = int(sizeof kSteps / sizeof kSteps[0]);
static_assert(kStepCount <= kMaxSteps, "raise kMaxSteps");
const char* const* step_names() {
    static const char* names[kStepCount];
    for (int i = 0; i < kStepCount; ++i) names[i] = kSteps[i].name;
    return names;
}
constexpr int step_index(const anymaker::func_desc* desc) {
    for (int i = 0; i < kStepCount; ++i) if (kSteps[i].desc == desc) return i;
    return -1;
}
constexpr int kTileStep = step_index(&bind::tile_task_integrate_server), kLootStep = step_index(&bind::vehicle_generate_loot),
              kSaveStep = step_index(&bind::save_game_inner);
static_assert(kTileStep >= 0 && kLootStep >= 0 && kSaveStep >= 0, "the wreck loot queue needs these steps");
constexpr bool is_tile_save(int i) {
    return kSteps[i].desc == &bind::tile_save_level_1 || kSteps[i].desc == &bind::tile_save_level_2 ||
           kSteps[i].desc == &bind::tile_save_level_3;
}
// Steps take up to 10 pointer arguments (the furniture loot table takes 9 and a result). A step with fewer passes
// along whatever is in the caller's stack slots, which it never reads.
using step_fn_t = void (*)(void*, void*, void*, void*, void*, void*, void*, void*, void*, void*);
anymaker::cell_hook h_steps[kStepCount];

// Fix 4, distant container items: client_scene.vehicle_component.inventory.tick runs for every container of every
// loaded vehicle every frame and walks every item in it to place it in the world (13% of the main thread in the
// 0.1.2 profile). A container on a vehicle more than kInventoryNear from the local player that has not moved since its
// last real tick is ticked every kInventoryEvery frames, with the frame times it skipped added to delta_time.
constexpr double kInventoryNear = 50, kInventoryMoved = 0.02;
constexpr int kInventoryEvery = 8;
constexpr ptrdiff_t kClientVehiclePos = 0x310 + 0x48;   // client_scene.vehicle.m_transform (mat34) translation
using inventory_tick_t = void (*)(void* self, void* client, void* vehicle, void* scene, const double* dt);
anymaker::cell_hook h_inventory;
std::atomic<bool> g_inventory_lod{true};
struct InventoryLod { double pos[3]; double dt; };
std::unordered_map<void*, InventoryLod> g_inventory;   // main thread only, keyed by the inventory component
double g_local_pos[3];
bool g_local_ok;
uint32_t g_frame_no;
long long g_inventory_ran, g_inventory_skipped, g_inventory_calls, g_inventory_no_player;

// ------------------------------------------------------------------ spread wreck loot (server thread)
// When a terrain tile goes into the world, server_scene.environment_tile_task.integrate loops over every vehicle, calls
// vehicle.generate_loot (required, floor and furniture items) for each wreck on that tile that has none yet, then marks
// the tile's loot as done. Each call creates the wreck's items, about 7 ms in 0.1.27, so a tile with 80 wrecks held the
// server for over 600 ms and the world went into slow motion. With "Spread wreck loot" on, wreck loot inside a tile
// integrate runs until kLootBudgetMs of loot has run this tick; the rest is queued by vehicle id and filled after the
// following ticks, kLootBudgetMs a tick (80 wrecks take about 1.3 s, on tiles at the edge of the loaded area). A
// queued wreck that is gone by then is skipped. Before a world save on the server thread the whole queue is filled,
// and before a tile save that tile's own wrecks are, so a save never stores a wreck as looted without its items. Only a save from the main thread (the
// pause menu's save in the first second after a tile loads) can miss queued loot; that is logged.
constexpr double kLootBudgetMs = 8;
constexpr ptrdiff_t kServerScene = 0xa0;      // server.m_scene, ref<server_scene> at +0x98: object pointer at +0xa0
constexpr ptrdiff_t kSceneVehicles = 0x1b0;   // server_scene.m_vehicles (server_scene.vehicle.container)
constexpr ptrdiff_t kVehicleId = 0x40;        // server_scene.vehicle.m_id (s32)
using vehicle_by_id_t = void (*)(void** ret, void* container, const int32_t* id);
// tile/grid: the tile the wreck was queued in (environment_tile_task.m_tile_loaded +0x310, m_position +0x8), so a
// tile save fills only its own wrecks (environment_tile.m_grid_position +0x170). vehicle == nullptr: already filled.
struct PendingLoot { void* scene; void* vehicle; int32_t id, tier; const void* tile; int32_t grid[3]; bool has_grid; };
constexpr ptrdiff_t kTaskTile = 0x310, kTaskGrid = 0x8, kTileGrid = 0x170;
void* g_tile_task;                            // the server tile integrate in progress
std::atomic<bool> g_spread_loot{true};
std::vector<PendingLoot> g_loot_queue;        // server thread only
std::atomic<int> g_loot_pending{0};
size_t g_loot_next;                           // first entry of g_loot_queue not filled yet
int g_tile_depth;                             // inside a server tile integrate
double g_loot_debt;                           // wreck loot ms over budget not yet paid back (server thread)
constexpr double kLootRepayMs = 4;
double g_loot_ms_tick;                        // wreck loot run in the server tick in progress
int g_loot_queued_total, g_loot_skipped_total, g_loot_ticks;   // the batch since the queue was last empty
vehicle_by_id_t g_vehicle_by_id;

// Queues one generate_loot call from a tile integrate once this tick's budget is used. True when queued.
bool queue_loot(void* vehicle, void* scene, const void* tier) {
    if (!g_spread_loot || !g_vehicle_by_id || g_loot_ms_tick < kLootBudgetMs) return false;
    auto* v = static_cast<uint8_t*>(vehicle);
    if (!v || !scene || !anymaker::readable(v + kVehicleId, 4) || (tier && !anymaker::readable(tier, 4))) return false;
    PendingLoot p{scene, vehicle, *reinterpret_cast<const int32_t*>(v + kVehicleId), tier ? *static_cast<const int32_t*>(tier) : 0};
    auto* task = static_cast<const uint8_t*>(g_tile_task);
    if (task && anymaker::readable(task + kTaskTile, 8)) p.tile = *reinterpret_cast<void* const*>(task + kTaskTile);
    if (task && anymaker::readable(task + kTaskGrid, 12)) { std::memcpy(p.grid, task + kTaskGrid, 12); p.has_grid = true; }
    g_loot_queue.push_back(p);
    g_loot_pending.store(int(g_loot_queue.size() - g_loot_next), std::memory_order_relaxed);
    ++g_loot_queued_total;
    return true;
}
// Fills queued wreck loot for scene until budget_ms of loot has run this tick (all of it with budget_ms < 0).
bool fill_one(PendingLoot& p, void* scene) {
    auto original = reinterpret_cast<step_fn_t>(h_steps[kLootStep].original);
    void* now = nullptr;
    if (p.scene == scene) g_vehicle_by_id(&now, static_cast<uint8_t*>(scene) + kSceneVehicles, &p.id);
    if (now != p.vehicle) { p.vehicle = nullptr; return false; }   // gone, or another world
    double start = now_ms();
    original(p.vehicle, scene, &p.tier, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr);
    double ms = now_ms() - start;
    g_loot_ms_tick += ms;
    g_tick.steps.add(kLootStep, ms);
    p.vehicle = nullptr;
    return true;
}
// A tile is being saved (unloaded or changing detail): fill the queued wrecks of that tile only. Wrecks queued with
// no known tile are filled too, to be safe.
void fill_tile_loot(const void* tile, void* scene) {
    auto* t = static_cast<const uint8_t*>(tile);
    int32_t grid[3]{};
    bool has_grid = t && anymaker::readable(t + kTileGrid, 12);
    if (has_grid) std::memcpy(grid, t + kTileGrid, 12);
    int filled = 0;
    for (size_t i = g_loot_next; i < g_loot_queue.size(); ++i) {
        auto& p = g_loot_queue[i];
        if (!p.vehicle) continue;
        bool same = (p.tile && p.tile == tile) || (has_grid && p.has_grid && std::memcmp(p.grid, grid, 12) == 0) ||
                    (!p.tile && !p.has_grid);
        if (!same) continue;
        if (fill_one(p, scene)) ++filled; else ++g_loot_skipped_total;
    }
    if (filled) log(0, "Spread wreck loot: tile %d,%d,%d was saved, so its %d waiting wrecks were filled at once", grid[0],
                    grid[1], grid[2], filled);
    // Diagnostics for the first saves that match nothing, to check the tile matching in game.
    static int unmatched_logs = 0;
    if (!filled && unmatched_logs < 5 && g_loot_next < g_loot_queue.size()) {
        const auto& q = g_loot_queue[g_loot_next];
        ++unmatched_logs;
        log(0, "Spread wreck loot: tile %d,%d,%d saved with none of its wrecks waiting (first waiting wreck is from tile %d,%d,%d%s)",
            grid[0], grid[1], grid[2], q.grid[0], q.grid[1], q.grid[2], q.tile ? "" : ", no tile pointer");
    }
}
void fill_loot(void* scene, double budget_ms) {
    while (g_loot_next < g_loot_queue.size() && (budget_ms < 0 || g_loot_ms_tick < budget_ms)) {
        PendingLoot& p = g_loot_queue[g_loot_next++];
        if (p.vehicle && !fill_one(p, scene)) ++g_loot_skipped_total;   // null: filled by its tile's save
    }
    ++g_loot_ticks;
    if (g_loot_next >= g_loot_queue.size()) {
        log(0, "Spread wreck loot: %d wrecks filled over %d server ticks%s", g_loot_queued_total - g_loot_skipped_total,
            g_loot_ticks, g_loot_skipped_total ? " (some wrecks were gone by then)" : "");
        g_loot_queue.clear(); g_loot_next = 0; g_loot_queued_total = g_loot_skipped_total = g_loot_ticks = 0;
    }
    g_loot_pending.store(int(g_loot_queue.size() - g_loot_next), std::memory_order_relaxed);
}
void* server_scene_of(void* server) {
    auto* s = static_cast<uint8_t*>(server);
    return s && anymaker::readable(s + kServerScene, 8) ? *reinterpret_cast<void**>(s + kServerScene) : nullptr;
}

template <int I> void hk_step(void* a, void* b, void* c, void* d, void* e, void* f, void* g, void* h, void* i, void* j) {
    auto original = reinterpret_cast<step_fn_t>(h_steps[I].original);
    bool mine = kSteps[I].server ? on_server_thread() : GetCurrentThreadId() == g_main_thread;
    if (!mine) {
        if constexpr (I == kSaveStep)
            if (int n = g_loot_pending.load(std::memory_order_relaxed))
                log(1, "a save from outside the server thread ran with %d wrecks still waiting for their loot", n);
        original(a, b, c, d, e, f, g, h, i, j); return;
    }
    if constexpr (I == kLootStep)
        if (g_tile_depth > 0 && queue_loot(a, b, c)) return;
    if constexpr (I == kSaveStep)   // server._save_game (server, ...)
        if (g_loot_pending.load(std::memory_order_relaxed))
            if (void* scene = server_scene_of(a)) fill_loot(scene, -1);
    if constexpr (is_tile_save(I))  // environment_tile.save_level_N (tile, server_scene, ...)
        if (g_loot_pending.load(std::memory_order_relaxed) && b) {
            fill_tile_loot(a, b);
            g_loot_pending.store(int(g_loot_queue.size() - g_loot_next), std::memory_order_relaxed);
        }
    [[maybe_unused]] void* outer_task = g_tile_task;
    if constexpr (I == kTileStep) { ++g_tile_depth; g_tile_task = b; }   // (bool) integrate: a is the result
    double start = now_ms();
    original(a, b, c, d, e, f, g, h, i, j);
    double ms = now_ms() - start;
    if constexpr (I == kTileStep) { --g_tile_depth; g_tile_task = outer_task; }
    if constexpr (I == kLootStep) g_loot_ms_tick += ms;
    (kSteps[I].server ? g_tick.steps : g_parts.steps).add(I, ms);
}
template <int... I> std::array<void*, sizeof...(I)> step_hooks(std::integer_sequence<int, I...>) {
    return {reinterpret_cast<void*>(&hk_step<I>)...};
}
const std::array<void*, kStepCount> kStepHooks = step_hooks(std::make_integer_sequence<int, kStepCount>{});

// ------------------------------------------------------------------ sim smoothing (patches client.tick constants)
// Two literals in client.tick's constant table, each checked against the instruction that reads it.
struct Literal { uint32_t user, offset; const char* user_bytes; double game; double light, strong; };
constexpr Literal kBufferMin{0x5ee, 0xa78, "48 8d 1d 83 04 00 00", 1.0, 2.0, 4.0};      // also the buffer after a reset
constexpr Literal kCatchUpMax{0x506, 0xa60, "f2 0f 59 05 52 05 00 00", 4.0, 2.0, 1.5};
std::atomic<uint8_t*> g_client_tick_code{nullptr};  // client.tick entry, found by its code signature
int g_smoothing_applied = 0;             // main thread
bool g_smoothing_broken;

bool bytes_match(const uint8_t* p, const char* hex) {
    for (; *hex; ++p) {
        while (*hex == ' ') ++hex;
        if (!*hex) break;
        unsigned v; if (std::sscanf(hex, "%2x", &v) != 1 || *p != v) return false;
        hex += 2;
    }
    return true;
}
double* literal_at(const Literal& l) {
    uint8_t* code = g_client_tick_code.load();
    if (!code || !anymaker::readable(code + l.user, 8) || !bytes_match(code + l.user, l.user_bytes)) return nullptr;
    auto* lit = reinterpret_cast<double*>(code + l.offset);
    return anymaker::readable(lit, 8) ? lit : nullptr;
}
bool write_double(double* at, double v) {
    DWORD old;
    if (!VirtualProtect(at, 8, PAGE_EXECUTE_READWRITE, &old)) return false;
    std::memcpy(at, &v, 8);
    VirtualProtect(at, 8, old, &old);
    FlushInstructionCache(GetCurrentProcess(), at, 8);
    return true;
}
// Applies the smoothing level. Main thread, before client.tick runs (nothing else reads these constants).
void apply_smoothing() {
    int want = g_smoothing.load();
    if (want == g_smoothing_applied || g_smoothing_broken) return;
    double* buffer = literal_at(kBufferMin); double* catch_up = literal_at(kCatchUpMax);
    auto expect = [](const Literal& l, int level) { return level == 2 ? l.strong : level == 1 ? l.light : l.game; };
    if (!buffer || !catch_up || *buffer != expect(kBufferMin, g_smoothing_applied) || *catch_up != expect(kCatchUpMax, g_smoothing_applied)) {
        g_smoothing_broken = true; log(1, "Sim smoothing: client.tick does not look as expected; left unchanged"); return;
    }
    if (!write_double(buffer, expect(kBufferMin, want)) || !write_double(catch_up, expect(kCatchUpMax, want))) {
        g_smoothing_broken = true; log(1, "Sim smoothing: could not write the game's code; left unchanged"); return;
    }
    g_smoothing_applied = want;
    log(0, "Sim smoothing %s: buffer at least %.0f tick(s), catch-up at most %.1fx", want == 2 ? "strong" : want == 1 ? "light" : "off (game)",
        *buffer, *catch_up);
}

// ------------------------------------------------------------------ overlay state (written main, read render)
std::mutex g_overlay_mutex;
double g_ov_last_hitch_ms, g_ov_last_hitch_at = -1;
char g_ov_last_blame[48];
Window<256> g_recent;                    // recent frame times for the live readout

// Precise timer on or off. Main thread.
void apply_timer() {
    bool want = g_precise_timer.load();
    if (want == g_timer_on) return;
    if (want) {
        g_timer_on = timeBeginPeriod(1) == TIMERR_NOERROR;
        log(g_timer_on ? 0 : 1, g_timer_on ? "Precise timer on (1 ms)" : "Windows refused the 1 ms timer");
    } else {
        timeEndPeriod(1); g_timer_on = false;
        log(0, "Precise timer off (Windows default)");
    }
}

// The system timer resolution right now, in ms (ntdll NtQueryTimerResolution; 0 when unavailable). Before the mod
// asks for 1 ms this shows whether something else (another app, an overlay) already did.
double timer_resolution_ms() {
    using query_t = LONG(NTAPI*)(PULONG, PULONG, PULONG);
    static auto query = reinterpret_cast<query_t>(GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "NtQueryTimerResolution"));
    ULONG lo = 0, hi = 0, cur = 0;
    return query && query(&lo, &hi, &cur) == 0 ? cur / 10000.0 : 0;
}

void read_settings() {
    if (!settings) return;
    uint64_t revision = settings->revision();
    if (revision == g_settings_revision) return;
    g_settings_revision = revision;
    AnySettingValueV1 v;
    if (g_tokens[0] && settings->get(g_tokens[0], &v)) g_precise_timer = v.number != 0;
    if (g_tokens[1] && settings->get(g_tokens[1], &v) && std::isfinite(v.number)) g_hitch_ms = std::clamp(v.number, 20.0, 1000.0);
    if (g_tokens[2] && settings->get(g_tokens[2], &v)) g_overlay = v.number != 0;
    if (g_tokens[3] && settings->get(g_tokens[3], &v) && std::isfinite(v.number)) g_smoothing = std::clamp(int(v.number), 0, 2);
    if (g_tokens[4] && settings->get(g_tokens[4], &v)) g_spread_loot = v.number != 0;
    if (g_tokens[5] && settings->get(g_tokens[5], &v)) g_profiler = v.number != 0;
    if (g_tokens[6] && settings->get(g_tokens[6], &v)) g_inventory_lod = v.number != 0;
}

// Counts one hitch or slow-motion log line; false once the session's limit is used up.
bool take_log_slot() {
    if (g_hitch_logs >= kMaxHitchLogs) return false;
    if (++g_hitch_logs == kMaxHitchLogs) log(0, "Hitch log limit reached; only summaries from now on");
    return true;
}

// One frame's playback rate (1 = normal speed) from the clock before and after client.tick. Main thread.
void sim_rate(double now, double frame_ms, double dt, double play_before, double play_after, double buffer) {
    if (!(dt > 0) || frame_ms > kPauseMs) return;
    double rate = (play_after - play_before) / (dt * 60.0);
    if (!std::isfinite(rate) || rate <= 0 || rate > 10) return;    // not updated this frame, or a clock reset (new world)
    AnyUiSnapshotV1 ui_now;
    if (!ui || !ui->copy(&ui_now) || ui_now.kind != ANY_UI_GAMEPLAY) {   // a solo world pauses in menus: not a stall
        g_episode.reset(); return;
    }
    g_sim_rate = g_sim_rate.load() * 0.8 + rate * 0.2;
    bool was = g_episode.active;
    if (g_episode.add(now / 1000.0, frame_ms / 1000.0, rate)) {
        ServerParts w = take_worst_tick();
        if (take_log_slot()) {
            char tick[320] = "";
            if (w.total > 0)
                std::snprintf(tick, sizeof tick, " | worst server tick %.0f ms, mostly %s: loading %.1f (tiles %.1f), simulation %.1f "
                              "(bunkers %.1f, items %.1f, vehicles %.1f, entities %.1f, physics step %.1f, other %.1f), network %.1f, autosave %.1f",
                              w.total, blame_server(w), w.loading, w.tiles, w.scene, w.dungeons, w.items, w.vehicles, w.entities, w.physics,
                              std::max(0.0, w.scene - w.dungeons - w.items - w.vehicles - w.entities - w.physics), w.network, w.autosave);
            else std::snprintf(tick, sizeof tick, " | no server tick here (joined game: the host's server or the network)");
            char steps[256] = "";
            if (w.total > 0) format_steps(steps, sizeof steps, w.steps, step_names(), kStepCount, 5, 1.0);
            log(0, "Slow motion: %.2fx for %.1f s, catch-up %.2fx for %.1f s, buffer up to %.1f ticks%s%s%s", g_episode.min_rate,
                g_episode.slow_s, g_episode.max_rate, g_episode.fast_s, g_episode_buffer_max, tick, steps[0] ? " | server steps: " : "", steps);
        }
        g_episode.reset();
    } else if (g_episode.active && !was) {
        take_worst_tick();                                           // start the episode's server window fresh
        g_episode_buffer_max = 0;
    }
    if (g_episode.active) g_episode_buffer_max = std::max(g_episode_buffer_max, buffer);
}

// CPU use per thread over the summary window, in percent of one core. Threads are grouped by role: the main thread
// (the client tick), the server thread (server.tick), Jolt's physics job pool (threads named "Worker N"), the game's
// tile and vehicle workers, and everything else (audio, driver, Steam, other mods). Runs on its own thread (the
// thread snapshot takes a few ms), which the main thread wakes once per summary.
struct ThreadCpu { std::wstring name; uint64_t cpu = 0; bool seen = false; };
std::unordered_map<DWORD, ThreadCpu> g_thread_cpu;
uint64_t g_process_cpu_last;

uint64_t ft64(const FILETIME& f) { return (uint64_t(f.dwHighDateTime) << 32) | f.dwLowDateTime; }
void cpu_summary(double seconds) {
    using describe_t = HRESULT(WINAPI*)(HANDLE, PWSTR*);
    static auto describe = reinterpret_cast<describe_t>(GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "GetThreadDescription"));
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    if (snap == INVALID_HANDLE_VALUE) return;
    bool first = g_thread_cpu.empty();
    for (auto& [id, t] : g_thread_cpu) t.seen = false;
    double main_pct = 0, server_pct = 0, workers_pct = 0, workers_max = 0, game_workers_pct = 0, other_pct = 0;
    int workers = 0;
    DWORD pid = GetCurrentProcessId(), server = g_server_thread.load();
    THREADENTRY32 te{}; te.dwSize = sizeof te;
    for (BOOL ok = Thread32First(snap, &te); ok; ok = Thread32Next(snap, &te)) {
        if (te.th32OwnerProcessID != pid) continue;
        HANDLE h = OpenThread(THREAD_QUERY_LIMITED_INFORMATION, FALSE, te.th32ThreadID);
        if (!h) continue;
        FILETIME c, e, k, u;
        uint64_t cpu = GetThreadTimes(h, &c, &e, &k, &u) ? ft64(k) + ft64(u) : 0;
        auto& t = g_thread_cpu[te.th32ThreadID];
        if (t.name.empty() && describe) {
            PWSTR name = nullptr;
            if (SUCCEEDED(describe(h, &name)) && name) { t.name = *name ? name : L"-"; LocalFree(name); }
        }
        CloseHandle(h);
        double pct = t.cpu && cpu >= t.cpu && seconds > 0 ? double(cpu - t.cpu) / 1e7 / seconds * 100.0 : 0;
        t.cpu = cpu; t.seen = true;
        if (te.th32ThreadID == g_main_thread) main_pct += pct;
        else if (te.th32ThreadID == server) server_pct += pct;
        else if (t.name.rfind(L"Worker ", 0) == 0) { ++workers; workers_pct += pct; workers_max = std::max(workers_max, pct); }
        else if (t.name.find(L"Tile Worker") != std::wstring::npos || t.name.find(L"Vehicle Worker") != std::wstring::npos) game_workers_pct += pct;
        else other_pct += pct;
    }
    CloseHandle(snap);
    for (auto it = g_thread_cpu.begin(); it != g_thread_cpu.end();) it = it->second.seen ? std::next(it) : g_thread_cpu.erase(it);
    FILETIME c, e, k, u;
    uint64_t process = GetProcessTimes(GetCurrentProcess(), &c, &e, &k, &u) ? ft64(k) + ft64(u) : 0;
    double process_pct = g_process_cpu_last && process >= g_process_cpu_last && seconds > 0 ? double(process - g_process_cpu_last) / 1e7 / seconds * 100.0 : 0;
    g_process_cpu_last = process;
    if (first) return;                                   // the first pass only records starting totals
    SYSTEM_INFO si; GetNativeSystemInfo(&si);
    log(0, "CPU (100%% = one core): game total %.0f%% (%u logical cores = %u%%) | main thread %.0f%% | server thread %.0f%% | physics workers (%d) "
           "%.0f%% total, busiest %.0f%% | tile/vehicle workers %.0f%% | other threads %.0f%%",
        process_pct, si.dwNumberOfProcessors, si.dwNumberOfProcessors * 100u, main_pct, server_pct, workers, workers_pct, workers_max, game_workers_pct, other_pct);
}

std::mutex g_cpu_mutex;
std::condition_variable g_cpu_wake;
double g_cpu_pending = -1;               // the window to report, in seconds; -1 when nothing is asked (g_cpu_mutex)
void cpu_worker() {
    std::unique_lock lock(g_cpu_mutex);
    for (;;) {
        g_cpu_wake.wait(lock, [] { return g_stop.load() || g_cpu_pending >= 0; });
        if (g_stop) return;
        double seconds = g_cpu_pending; g_cpu_pending = -1;
        lock.unlock(); cpu_summary(seconds); lock.lock();
    }
}
#include "anyperformance_profiler.inc"

void request_cpu_summary(double seconds) {
    { std::lock_guard lock(g_cpu_mutex); g_cpu_pending = seconds; }
    g_cpu_wake.notify_one();
}

void summary(double now) {
    double seconds = (now - g_window_start) / 1000.0;
    TickPacing pacing; double server_tiles;
    static ServerWindow sw;   // static: two Steps arrays are too big for comfort on the stack
    {
        std::lock_guard lock(g_server_mutex);
        pacing = g_pacing; g_pacing = TickPacing{};
        server_tiles = g_server_tile_ms_window; g_server_tile_ms_window = 0;
        sw = g_server_window; g_server_window = ServerWindow{};
    }
    if (g_frames.count) {
        char line[640];
        int n = format_summary(line, sizeof line, seconds, g_frames.count, g_frames.mean(), g_frames.percentile(99),
                               g_hitches, g_worst_hitch, pacing, g_tiles_window, g_tile_max_window);
        if (n > 0 && size_t(n) < sizeof line) {
            n += std::snprintf(line + n, sizeof line - n, " | timer %.1f ms", timer_resolution_ms());
            if (pacing.ticks && size_t(n) < sizeof line) n += std::snprintf(line + n, sizeof line - n, " | server tile streaming %.0f ms total", server_tiles);
            if (g_loading_hitches && size_t(n) < sizeof line) std::snprintf(line + n, sizeof line - n, " | %d more on loading screens", g_loading_hitches);
        }
        log(0, "%s", line);
    }
    // Average breakdowns: where a normal frame and a normal server tick go, not only the slow ones.
    if (g_frame_window.frames) {
        const FrameWindow& w = g_frame_window; double n = w.frames;
        char steps[320];
        format_step_avg(steps, sizeof steps, w.sum.steps, step_names(), kStepCount, 6, w.frames, 0.1);
        log(0, "Frame average (%d frames): %.1f ms = client tick %.1f (simulation+physics %.1f, streaming %.1f), "
               "rendering/UI %.1f%s%s", w.frames, w.frame_ms / n, w.sum.client_tick / n, w.sum.scene_tick / n,
            w.sum.loading / n, (w.frame_ms - w.sum.client_tick) / n, steps[0] ? " | steps: " : "", steps);
    }
    if (h_inventory.cell && g_inventory_lod) {
        long long calls = g_inventory_calls;
        log(0, "Distant container items: %lld container updates, %lld skipped (%.0f%% saved), %lld far ones run, "
               "%lld with no player position", calls, g_inventory_skipped, calls ? 100.0 * double(g_inventory_skipped) / double(calls) : 0.0,
            g_inventory_ran, g_inventory_no_player);
        g_inventory_ran = g_inventory_skipped = g_inventory_calls = g_inventory_no_player = 0;
    }
    if (sw.ticks) {
        const ServerParts& t = sw.sum; double n = sw.ticks;
        char steps[400];
        format_step_avg(steps, sizeof steps, t.steps, step_names(), kStepCount, 8, sw.ticks, 0.1);
        log(0, "Server tick average (%d ticks): %.1f ms = loading %.1f (tiles %.1f), simulation %.1f (bunkers %.1f, items %.1f, "
               "vehicles %.1f, entities %.1f, physics step %.1f, other %.1f), network %.1f, autosave %.1f%s%s",
            sw.ticks, t.total / n, t.loading / n, t.tiles / n, t.scene / n, t.dungeons / n, t.items / n, t.vehicles / n,
            t.entities / n, t.physics / n, (t.scene - t.dungeons - t.items - t.vehicles - t.entities - t.physics) / n,
            t.network / n, t.autosave / n, steps[0] ? " | steps: " : "", steps);
    }
    g_frame_window = FrameWindow{};
    request_cpu_summary(seconds);
    g_frames.clear(); g_hitches = g_loading_hitches = g_tiles_window = 0; g_worst_hitch = g_tile_max_window = 0;
    g_window_start = now;
}

// One frame ended (from one client.tick start to the next). Main thread.
void frame_done(double frame_ms, const FrameParts& p, bool loading, double now) {
    if (frame_ms > kPauseMs) return;
    g_frames.add(frame_ms);
    if (!loading) g_frame_window.add(p, frame_ms);
    g_tiles_window += p.tile_attaches; g_tile_max_window = std::max(g_tile_max_window, p.tile_attach);
    const char* cause = nullptr;
    if (frame_ms >= g_hitch_ms.load()) {
        cause = blame(p, frame_ms);
        if (loading) ++g_loading_hitches; else { ++g_hitches; g_worst_hitch = std::max(g_worst_hitch, frame_ms); }
        if (take_log_slot()) {
            char steps[200] = "";
            format_steps(steps, sizeof steps, p.steps, step_names(), kStepCount, 3, 1.0);
            log(0, "Hitch %.0f ms%s, mostly %s: client tick %.1f (simulation+physics %.1f, streaming %.1f: %d tile attach %.1f, "
                   "vehicle rebuild %.1f%s%s), rendering/UI %.1f | last server tick %.1f ms",
                frame_ms, loading ? " on a loading screen" : "", cause, p.client_tick, p.scene_tick, p.loading, p.tile_attaches,
                p.tile_attach, p.vehicle_tasks, steps[0] ? "; steps: " : "", steps, std::max(0.0, frame_ms - p.client_tick),
                g_server_last_ms.load());
        }
    }
    std::lock_guard lock(g_overlay_mutex);
    g_recent.add(frame_ms);
    if (cause && !loading) {
        g_ov_last_hitch_ms = frame_ms; g_ov_last_hitch_at = now;
        std::snprintf(g_ov_last_blame, sizeof g_ov_last_blame, "%s", cause);
    }
}

// ------------------------------------------------------------------ hooks
using client_tick_t = void (*)(void* client, const double* dt, const bool* loading, void* frontend, void* settings);
using scene_tick_t = void (*)(void* scene, void* client, const double* a, const double* b);
using tick_loading_t = void (*)(void* scene, void* client, const double* dt, const bool* loading);
using integrate_t = void (*)(void* task, void* container);
using update_tasks_t = void (*)(void* container, void* client, void* scene);
using server_tick_t = void (*)(void* server);
using server_tiles_t = void (*)(void* container, void* scene, const void* positions, const int32_t* n);
using two_t = void (*)(void* a, void* b);
using vehicles_t = void (*)(void* container, void* scene, const bool* flag);
using vehicle_post_t = void (*)(void* container, void* tiles, void* ocean, const double* dt, const bool* flag);
using items_post_t = void (*)(void* container, void* physics, void* ocean, const double* dt);
using replicate_t = void (*)(const int32_t* peer, void* data, void* object, void* buffer);
using autosave_t = void (*)(bool* ret, void* server);
anymaker::cell_hook h_client_tick, h_scene_tick, h_tick_loading, h_integrate, h_update_tasks, h_server_tick, h_server_tiles;
// physics.scene.update is native (game.exe), so it has no gcl code of its own to find. Its cell is read from
// server_scene.tick's constant table: slot 0xbf8 (dependency 31), checked against the instruction that loads it.
constexpr uint32_t kPhysicsSlot = 0xbf8, kPhysicsUser = 0x5f3;
constexpr const char* kPhysicsUserBytes = "48 8b 1d fe 05 00 00";   // mov rbx, [rip+0x5fe] -> slot 0xbf8
using physics_update_t = void (*)(void* physics_scene, const double* dt);
anymaker::cell_hook h_physics;
anymaker::cell_hook h_server_loading, h_server_scene, h_dungeons, h_items, h_vehicles, h_entities, h_vehicles_post, h_items_post,
    h_replicate, h_autosave;

void update_local_player() {
    static AnySessionPlayersV1 players;
    if (g_frame_no % 4) return;
    g_local_ok = false;
    uint32_t ui_flags = 0;   // copy_players fails without it
    if (!g_inventory_lod || !host.copy_players || !host.copy_players(&players, &ui_flags)) return;
    for (uint32_t i = 0; i < players.count && i < ANYAPI_PLAYERS_CAPACITY; ++i) {
        const auto& p = players.players[i];
        if ((p.valid_fields & (PLAYER_LOCAL | PLAYER_POSITION)) != (PLAYER_LOCAL | PLAYER_POSITION)) continue;
        std::copy(p.position, p.position + 3, g_local_pos);
        g_local_ok = std::isfinite(p.position[0]) && std::isfinite(p.position[1]) && std::isfinite(p.position[2]);
        return;
    }
}

void hk_inventory(void* self, void* client, void* vehicle, void* scene, const double* dt) {
    auto run = [&](const double* d) { reinterpret_cast<inventory_tick_t>(h_inventory.original)(self, client, vehicle, scene, d); };
    auto* v = static_cast<const uint8_t*>(vehicle);
    ++g_inventory_calls;
    if (g_inventory_lod && !g_local_ok) ++g_inventory_no_player;
    if (!g_inventory_lod || !g_local_ok || g_stop || !dt || !v || !anymaker::readable(v + kClientVehiclePos, 24)) return run(dt);
    const auto* pos = reinterpret_cast<const double*>(v + kClientVehiclePos);
    double dx = pos[0] - g_local_pos[0], dy = pos[1] - g_local_pos[1], dz = pos[2] - g_local_pos[2];
    if (!(dx * dx + dy * dy + dz * dz > kInventoryNear * kInventoryNear)) { g_inventory.erase(self); return run(dt); }
    if (g_inventory.size() > 20000) g_inventory.clear();
    auto [it, fresh] = g_inventory.try_emplace(self);
    auto& e = it->second;
    double mx = pos[0] - e.pos[0], my = pos[1] - e.pos[1], mz = pos[2] - e.pos[2];
    bool moved = fresh || mx * mx + my * my + mz * mz > kInventoryMoved * kInventoryMoved;
    // Spread containers over the frames by address so the work stays even.
    bool turn = (g_frame_no + uint32_t(reinterpret_cast<uintptr_t>(self) >> 6)) % kInventoryEvery == 0;
    e.dt += *dt;
    if (!moved && !turn) { ++g_inventory_skipped; return; }
    double total = e.dt;
    e.dt = 0;
    std::copy(pos, pos + 3, e.pos);
    ++g_inventory_ran;
    run(&total);
}

void hk_client_tick(void* client, const double* dt, const bool* loading, void* frontend, void* game_settings) {
    double start = now_ms();
    auto* c = static_cast<uint8_t*>(client);
    bool clock_ok = c && anymaker::readable(c + clock_off::play, 0xf60 - clock_off::play);
    double play_before = clock_ok ? *reinterpret_cast<double*>(c + clock_off::play) : 0;
    double frame_ms = g_frame_start >= 0 ? start - g_frame_start : 0;
    if (!g_stop) {
        read_settings();
        apply_timer();
        apply_smoothing();
        if (g_frame_start >= 0) frame_done(start - g_frame_start, g_parts, g_parts_loading, start);
        if (g_window_start < 0) g_window_start = start;
        else if (start - g_window_start >= kSummarySeconds * 1000) summary(start);
        g_frame_start = start; g_parts = FrameParts{}; g_parts_loading = loading && *loading;
        ++g_frame_no;
        update_local_player();
        if (!g_main_thread) {
            g_main_thread = GetCurrentThreadId();
            std::thread(cpu_worker).detach();
            request_cpu_summary(0);   // starting totals
        }
    }
    reinterpret_cast<client_tick_t>(h_client_tick.original)(client, dt, loading, frontend, game_settings);
    g_parts.client_tick += now_ms() - start;
    if (clock_ok && dt && !g_stop && !(loading && *loading))
        sim_rate(start, frame_ms, *dt, play_before, *reinterpret_cast<double*>(c + clock_off::play),
                 *reinterpret_cast<double*>(c + clock_off::buffer));
}
void hk_scene_tick(void* scene, void* client, const double* a, const double* b) {
    double start = now_ms();
    reinterpret_cast<scene_tick_t>(h_scene_tick.original)(scene, client, a, b);
    g_parts.scene_tick += now_ms() - start;
}
void hk_tick_loading(void* scene, void* client, const double* dt, const bool* loading) {
    double start = now_ms();
    reinterpret_cast<tick_loading_t>(h_tick_loading.original)(scene, client, dt, loading);
    g_parts.loading += now_ms() - start;
}
void hk_integrate(void* task, void* container) {
    double start = now_ms();
    reinterpret_cast<integrate_t>(h_integrate.original)(task, container);
    g_parts.tile_attach += now_ms() - start; ++g_parts.tile_attaches;
}
void hk_update_tasks(void* container, void* client, void* scene) {
    double start = now_ms();
    reinterpret_cast<update_tasks_t>(h_update_tasks.original)(container, client, scene);
    g_parts.vehicle_tasks += now_ms() - start;
}
void hk_server_tick(void* server) {
    g_server_thread.store(GetCurrentThreadId(), std::memory_order_relaxed);
    g_tick = ServerParts{};
    g_loot_ms_tick = 0;
    double start = now_ms();
    reinterpret_cast<server_tick_t>(h_server_tick.original)(server);
    if (g_loot_pending.load(std::memory_order_relaxed)) {
        // One wreck can cost 100-280 ms on its own (towns). Every ms over the budget is paid back first at
        // kLootRepayMs a tick, so heavy wrecks are spaced out and the world catches up between them.
        if (g_loot_debt > 0) g_loot_debt -= kLootRepayMs;
        else if (void* scene = server_scene_of(server)) fill_loot(scene, kLootBudgetMs);
        g_loot_debt += std::max(0.0, g_loot_ms_tick - kLootBudgetMs);
        if (!g_loot_pending.load(std::memory_order_relaxed)) g_loot_debt = 0;
    }
    double ms = now_ms() - start;
    g_tick.total = ms;
    g_server_last_ms = ms;
    std::lock_guard lock(g_server_mutex);
    double gap = g_server_last_start >= 0 ? start - g_server_last_start : 0;
    g_server_last_start = start;
    g_pacing.add(gap < kPauseMs ? gap : 0, ms);
    g_server_tile_ms_window += g_tick.tiles;
    if (ms > g_worst_tick.total) g_worst_tick = g_tick;
    g_server_window.add(g_tick);
}
// Server-thread pieces add to the tick in progress. replicate_peer also runs on the main thread (client authority
// updates), so every piece checks the thread.
#define TIMED(field, call) \
    if (!on_server_thread()) { call; return; } \
    double start = now_ms(); call; g_tick.field += now_ms() - start;
void hk_server_tiles(void* container, void* scene, const void* positions, const int32_t* n) {
    TIMED(tiles, reinterpret_cast<server_tiles_t>(h_server_tiles.original)(container, scene, positions, n));
}
void hk_server_loading(void* scene, void* server) { TIMED(loading, reinterpret_cast<two_t>(h_server_loading.original)(scene, server)); }
void hk_server_scene(void* scene, void* server) { TIMED(scene, reinterpret_cast<two_t>(h_server_scene.original)(scene, server)); }
void hk_dungeons(void* m, void* scene) { TIMED(dungeons, reinterpret_cast<two_t>(h_dungeons.original)(m, scene)); }
void hk_items(void* c, void* scene) { TIMED(items, reinterpret_cast<two_t>(h_items.original)(c, scene)); }
void hk_entities(void* c, void* scene) { TIMED(entities, reinterpret_cast<two_t>(h_entities.original)(c, scene)); }
void hk_vehicles(void* c, void* scene, const bool* f) { TIMED(vehicles, reinterpret_cast<vehicles_t>(h_vehicles.original)(c, scene, f)); }
void hk_vehicles_post(void* c, void* tiles, void* ocean, const double* dt, const bool* f) {
    TIMED(vehicles, reinterpret_cast<vehicle_post_t>(h_vehicles_post.original)(c, tiles, ocean, dt, f));
}
void hk_items_post(void* c, void* physics, void* ocean, const double* dt) {
    TIMED(items, reinterpret_cast<items_post_t>(h_items_post.original)(c, physics, ocean, dt));
}
void hk_replicate(const int32_t* peer, void* data, void* object, void* buffer) {
    TIMED(network, reinterpret_cast<replicate_t>(h_replicate.original)(peer, data, object, buffer));
}
void hk_physics(void* scene, const double* dt) {
    TIMED(physics, reinterpret_cast<physics_update_t>(h_physics.original)(scene, dt));
}
void hk_autosave(bool* ret, void* server) { TIMED(autosave, reinterpret_cast<autosave_t>(h_autosave.original)(ret, server)); }
#undef TIMED

// ------------------------------------------------------------------ overlay (render thread)
bool gameplay() { AnyUiSnapshotV1 s; return ui && ui->copy(&s) && s.kind == ANY_UI_GAMEPLAY; }

void text(const wchar_t* s, float x, float y, float size, uint32_t color) {
    AnyGpuCommandV1 c; c.kind = ANY_GPU_TEXT; c.text = s; c.text_length = uint32_t(std::wcslen(s)); c.font_size = size;
    c.flags = ANY_GPU_NOWRAP | ANY_GPU_BOLD;
    c.rect[0] = x + 1; c.rect[1] = y + 1; c.rect[2] = size * 40; c.rect[3] = size * 1.6f; c.color = 0xff000000; c.opacity = 0.7f;
    gpu->emit(&c);
    c.rect[0] = x; c.rect[1] = y; c.color = color; c.opacity = 1;
    gpu->emit(&c);
}

void draw(const AnyFrameV1* f, void*) {
    if (!g_overlay || !f || !f->focused || !gpu || !gameplay()) return;
    double avg, low, hitch_ms, hitch_at; char blame_text[48];
    {
        std::lock_guard lock(g_overlay_mutex);
        avg = g_recent.mean(); low = g_recent.percentile(99);
        hitch_ms = g_ov_last_hitch_ms; hitch_at = g_ov_last_hitch_at;
        std::memcpy(blame_text, g_ov_last_blame, sizeof blame_text);
    }
    float s = std::max(0.75f, float(f->height) / 1080.f), x = 12 * s, y = 10 * s, size = 15 * s;
    wchar_t line[160];
    double rate = g_sim_rate.load();
    std::swprintf(line, 160, L"%.0f fps   1%% low %.0f   server %.1f ms   world speed %.2fx", avg > 0 ? 1000 / avg : 0,
                  low > 0 ? 1000 / low : 0, g_server_last_ms.load(), rate);
    text(line, x, y, size, rate < SimEpisode::kSlow || rate > SimEpisode::kFast ? 0xffffc060 : 0xffe8f0e8);
    if (hitch_at >= 0 && now_ms() - hitch_at < 10000) {
        std::swprintf(line, 160, L"hitch %.0f ms: %hs", hitch_ms, blame_text);
        text(line, x, y + size * 1.4f, size, 0xffffc060);
    }
}

// ------------------------------------------------------------------ install (background thread)
struct HookSpec { const anymaker::func_desc* desc; anymaker::cell_hook* hook; void* fn; const char* name; bool done; };

void install_hooks() {
    using namespace anymaker::experimental;
    HookSpec specs[] = {
        {&bind::tile_task_integrate, &h_integrate, (void*)&hk_integrate, "tile attach", false},
        {&bind::vehicle_update_tasks, &h_update_tasks, (void*)&hk_update_tasks, "vehicle rebuilds", false},
        {&bind::client_scene_tick_loading, &h_tick_loading, (void*)&hk_tick_loading, "streaming", false},
        {&bind::client_scene_tick, &h_scene_tick, (void*)&hk_scene_tick, "client simulation", false},
        {&bind::server_tile_loading, &h_server_tiles, (void*)&hk_server_tiles, "server tile streaming", false},
        {&bind::server_scene_tick_loading, &h_server_loading, (void*)&hk_server_loading, "server loading", false},
        {&bind::dungeon_manager_tick, &h_dungeons, (void*)&hk_dungeons, "bunkers", false},
        {&bind::item_floor_tick, &h_items, (void*)&hk_items, "loose items", false},
        {&bind::item_floor_post_physics, &h_items_post, (void*)&hk_items_post, "loose items (physics)", false},
        {&bind::vehicle_container_tick, &h_vehicles, (void*)&hk_vehicles, "vehicles", false},
        {&bind::vehicle_post_physics, &h_vehicles_post, (void*)&hk_vehicles_post, "vehicles (physics)", false},
        {&bind::entity_container_tick, &h_entities, (void*)&hk_entities, "entities", false},
        {&bind::server_scene_tick, &h_server_scene, (void*)&hk_server_scene, "server simulation", false},
        {&bind::replicate_peer, &h_replicate, (void*)&hk_replicate, "network replication", false},
        {&bind::autosave_game, &h_autosave, (void*)&hk_autosave, "autosave", false},
        {&bind::cl_inventory, &h_inventory, (void*)&hk_inventory, "vehicle inventory", false},
        {&bind::server_tick, &h_server_tick, (void*)&hk_server_tick, "server tick", false},
        {&bind::client_tick, &h_client_tick, (void*)&hk_client_tick, "client tick", false},   // last: it starts the frame clock
    };
    bool physics_done = false, steps_done[kStepCount] = {};
    for (int attempt = 0; attempt < 180 && !g_stop; ++attempt) {
        if (attempt) Sleep(1000);
        if (!matching_build()) { if (attempt == 0) log(1, "game build does not match the SDK reference; AnyPerformance stays off"); return; }
        if (!g_client_tick_code) g_client_tick_code = static_cast<uint8_t*>(function(bind::client_tick));   // before its hook
        bool all = true;
        if (!g_vehicle_by_id) g_vehicle_by_id = reinterpret_cast<vehicle_by_id_t>(function(bind::vehicle_by_id));
        if (!g_vehicle_by_id) all = false;   // until found, wreck loot runs as in the game
        for (auto& h : specs) {
            if (h.done) continue;
            if (void** cell = hook_cell(*h.desc)) {
                if (h.hook->install(cell, h.fn)) h.done = true;
                else all = false;          // another mod changed the cell at that moment: try again next second
            } else all = false;
        }
        for (int i = 0; i < kStepCount; ++i) {
            if (steps_done[i]) continue;
            if (void** cell = hook_cell(*kSteps[i].desc)) {
                if (h_steps[i].install(cell, kStepHooks[size_t(i)])) steps_done[i] = true;
                else all = false;
            } else all = false;
        }
        if (!physics_done) {
            auto* scene_tick = static_cast<uint8_t*>(function(bind::server_scene_tick));
            auto exe = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
            auto nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(exe + reinterpret_cast<const IMAGE_DOS_HEADER*>(exe)->e_lfanew);
            if (scene_tick && anymaker::readable(scene_tick + kPhysicsUser, 8) && bytes_match(scene_tick + kPhysicsUser, kPhysicsUserBytes)) {
                auto** cell = reinterpret_cast<void**>(anymaker::read_slot(scene_tick, kPhysicsSlot));
                auto target = cell && anymaker::readable(cell, 8) ? reinterpret_cast<uintptr_t>(*cell) : 0;
                if (target >= exe && target < exe + nt->OptionalHeader.SizeOfImage && h_physics.install(cell, (void*)&hk_physics)) physics_done = true;
                else { log(1, "the physics step's cell did not check out; the physics step is not timed on its own"); physics_done = true; }
            } else all = false;
        }
        if (all) {
            log(0, "Ready after %d s. Timer resolution %.1f ms before the mod; hitch threshold %.0f ms.", attempt,
                timer_resolution_ms(), g_hitch_ms.load());
            return;
        }
    }
    for (auto& h : specs) if (!h.done && !g_stop) log(1, "could not locate or hook the %s function; it is not timed", h.name);
    for (int i = 0; i < kStepCount; ++i) if (!steps_done[i] && !g_stop) log(1, "could not locate or hook the %s step; it is not timed", kSteps[i].name);
    if (!g_vehicle_by_id && !g_stop) log(1, "could not locate the vehicle lookup; wreck loot is not spread out");
}

void shutdown(void*) {
    g_stop = true;
    { std::lock_guard lock(g_cpu_mutex); }
    g_cpu_wake.notify_all();
    h_client_tick.remove((void*)&hk_client_tick); h_scene_tick.remove((void*)&hk_scene_tick);
    h_tick_loading.remove((void*)&hk_tick_loading); h_integrate.remove((void*)&hk_integrate);
    h_update_tasks.remove((void*)&hk_update_tasks); h_server_tick.remove((void*)&hk_server_tick);
    h_server_tiles.remove((void*)&hk_server_tiles); h_server_loading.remove((void*)&hk_server_loading);
    h_server_scene.remove((void*)&hk_server_scene); h_dungeons.remove((void*)&hk_dungeons); h_items.remove((void*)&hk_items);
    h_vehicles.remove((void*)&hk_vehicles); h_entities.remove((void*)&hk_entities); h_vehicles_post.remove((void*)&hk_vehicles_post);
    h_items_post.remove((void*)&hk_items_post); h_replicate.remove((void*)&hk_replicate); h_autosave.remove((void*)&hk_autosave);
    h_physics.remove((void*)&hk_physics); h_inventory.remove((void*)&hk_inventory);
    for (int i = 0; i < kStepCount; ++i) h_steps[i].remove(kStepHooks[size_t(i)]);
    if (g_timer_on) { timeEndPeriod(1); g_timer_on = false; }
}

void register_settings() {
    settings = static_cast<const AnyHelpersSettingsV1*>(AnyAPI_Services()->query("anyhelpers.settings", 1));
    if (!settings || settings->struct_size != sizeof(*settings) || settings->version != 1 || !settings->register_setting ||
        !settings->get || !settings->revision) { settings = nullptr; return; }
    AnyModSettingV1 d; d.mod_id = kModId; d.mod_name = "AnyPerformance";
    d.setting_id = "precise_timer"; d.label = "Precise timer"; d.kind = ANY_SETTING_BOOL; d.order = 1; d.default_number = 1;
    d.description = "Keeps the world's 60 updates a second evenly spaced, so vehicles and physics stutter less in solo and when hosting. Recommended on.";
    g_tokens[0] = settings->register_setting(&d);
    d = AnyModSettingV1{}; d.mod_id = kModId; d.mod_name = "AnyPerformance";
    d.setting_id = "hitch_ms"; d.label = "Hitch threshold (ms)"; d.kind = ANY_SETTING_INTEGER; d.order = 2;
    d.default_number = 50; d.minimum = 20; d.maximum = 1000; d.step = 5;
    d.description = "Frames slower than this many milliseconds are written to anymaker_modding.log with what caused them. Only affects the log.";
    g_tokens[1] = settings->register_setting(&d);
    d = AnyModSettingV1{}; d.mod_id = kModId; d.mod_name = "AnyPerformance";
    d.setting_id = "overlay"; d.label = "FPS overlay"; d.kind = ANY_SETTING_BOOL; d.order = 3; d.default_number = 0;
    d.description = "Shows FPS, 1% low, server time, world speed and the last hitch's cause in the top-left corner.";
    g_tokens[2] = settings->register_setting(&d);
    static const char* const kSmoothing[] = {"Off (game)", "Light", "Strong"};
    d = AnyModSettingV1{}; d.mod_id = kModId; d.mod_name = "AnyPerformance";
    d.setting_id = "sim_smoothing"; d.label = "Sim smoothing"; d.kind = ANY_SETTING_CHOICE; d.order = 0; d.default_number = 0;
    d.choices = kSmoothing; d.choice_count = 3; d.maximum = 2;
    d.description = "Smooths the slow-down and speed-up after a stall. Light and Strong hide short stalls and cap the catch-up speed, "
                    "at the cost of 17 to 50 ms more delay on screen. Off is the game's own behaviour.";
    g_tokens[3] = settings->register_setting(&d);
    d = AnyModSettingV1{}; d.mod_id = kModId; d.mod_name = "AnyPerformance";
    d.setting_id = "spread_loot"; d.label = "Spread wreck loot"; d.kind = ANY_SETTING_BOOL; d.order = 4; d.default_number = 1;
    d.description = "Fills the wrecks on newly loaded terrain a few at a time instead of all at once, which stops the big stall "
                    "while driving. Wrecks far ahead may get their items a second later. Host and solo only.";
    g_tokens[4] = settings->register_setting(&d);
    d = AnyModSettingV1{}; d.mod_id = kModId; d.mod_name = "AnyPerformance";
    d.setting_id = "profiler"; d.label = "Profiler (for testing)"; d.kind = ANY_SETTING_BOOL; d.order = 5; d.default_number = 0;
    d.description = "Writes the game functions that use the most CPU on the main and server threads to anymaker_modding.log "
                    "every two minutes. Costs about 1% CPU. Leave off unless asked.";
    g_tokens[5] = settings->register_setting(&d);
    d = AnyModSettingV1{}; d.mod_id = kModId; d.mod_name = "AnyPerformance";
    d.setting_id = "inventory_lod"; d.label = "Distant container items"; d.kind = ANY_SETTING_BOOL; d.order = 6; d.default_number = 1;
    d.description = "Updates the items inside containers of parked vehicles more than 50 m away every 8th frame instead of "
                    "every frame. Anything moving or near you is updated every frame.";
    g_tokens[6] = settings->register_setting(&d);
    read_settings();
}
}  // namespace
}  // namespace anyperformance

using namespace anyperformance;
extern "C" __declspec(dllexport) bool AnyAPI_ModInit(const AnyModHostV1* h, AnyModCallbacksV1* out) {
    if (!h || !out || h->struct_size != sizeof(*h) || h->abi != ANYAPI_MOD_ABI || out->struct_size != sizeof(*out)) return false;
    host = *h; out->id = kModId;
    QueryPerformanceFrequency(&g_qpc_freq);
    out->shutdown = shutdown;
    return true;
}
extern "C" __declspec(dllexport) void AnyAPI_ModReady() {
    auto services = AnyAPI_Services(); if (!services) return;
    ui = static_cast<const AnyUiStateV1*>(services->query("anyapi.ui_state", 1));
    if (ui && (ui->struct_size != sizeof(*ui) || ui->version != 1 || !ui->copy)) ui = nullptr;
    gpu = static_cast<const AnyGpuDrawV1*>(services->query("anyapi.gpu_draw", 1));
    if (!gpu || gpu->struct_size != sizeof(*gpu) || gpu->version != 1 || !gpu->emit || !gpu->register_renderer || !ui ||
        !gpu->register_renderer(draw, nullptr)) { gpu = nullptr; log(1, "GPU drawing unavailable; no overlay (logging still works)"); }
    register_settings();
    std::thread(install_hooks).detach();
    std::thread(profiler_thread).detach();   // resolving scans game memory; keep it off the loader and frame threads
}
