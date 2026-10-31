// Turbo: exhaust-driven turbochargers in two sizes (3x3x2 and 6x6x2), built from the stock impeller.
//
// Each size is a new part of the stock impeller's class (gas_pump_centrifugal). The impeller already has two gas
// nodes, the compressor inlet and outlet, and pumps between their two gas contents. The mod gives each turbo a
// hot side: two more gas contents and nodes (turbine inlet, turbine exhaust out), created with the game's own
// functions and registered in the vehicle's gas node container, so pipes, islands and pressure all treat them
// like any other gas port. Each server tick the turbo moves exhaust through its hot side, spools its shaft from
// that flow (with lag that grows with size) and runs its compressor at the spool's head. The engine's existing
// boost does the rest: intake pressure up to 10 (the game's "98 kPa") doubles engine power.
// The model and its numbers are in turbo_logic.h; the meshes are made from turbo_a.mesh (turbo_mesh.h).
//
// Superchargers (small 2x2x2 and large 4x4x4) are the same class too, driven by a stock Pulley Wheel on their torque
// spot belted to the engine: their head follows the pulley's speed, and the air they push takes its work back off the
// belt (apply_torque on their torque node, as the stock impeller does). They replaced the 2x2 and 4x4 turbos, which are
// gone (a saved vehicle that had one loads without it).
//
// Getting it: world loot in the "mechanical" group, tech_tier 3 (after the 2nd bunker) for the 3x3 turbo and the small
// supercharger, wastegate, blow-off valve and air filter, 4 (after the 3rd) for the rest (turbo_logic.h). Single player / host only for
// now: the definitions are added on this game's server and client scene, and every player would need the mod.
// The hot-side gas is not saved; it refills from the pipes after loading.
//
// EXPERIMENTAL. Hooks game functions through the experimental SDK (turbo_bindings.h): Anymaker 0.1.27 /
// Steam build 26113663 only. First runs are diagnostic: read anymaker_modding.log.
#define NOMINMAX
#include <windows.h>
#include "anyapi_services_v1.h"
#include "anyapi_experimental.hpp"
#include "anyhelpers_settings_v1.h"
#include "turbo_bindings.h"
#include "route_cell.h"
#include "engine_parts_shared.h"
#include "turbo_logic.h"
#include "turbo_mesh.h"
#include "turbo_sound.h"
#include "turbo_upkeep_mesh.h"
#include "turbo_supercharger_mesh.h"
#include "turbo_health.h"
#include "turbo_wear_store.h"
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace turbo {
namespace {
constexpr const char* kModId = "turbo";

AnyModHostV1 host;
std::atomic<bool> g_ready{false}, g_stop{false};

void log(int level, const char* fmt, ...) {
    if (!host.log) return;
    char buf[640];
    va_list ap; va_start(ap, fmt); vsnprintf(buf, sizeof buf, fmt, ap); va_end(ap);
    host.log(uint32_t(level), kModId, buf);
}

// Reference layouts (metadata, Anymaker 0.1.24; unchanged in 0.1.25).
namespace off {
constexpr ptrdiff_t component_id = 64;               // server_scene.vehicle_component.m_id (s32)
constexpr ptrdiff_t component_definition = 312;      // m_definition (ptr<const vehicle_component_definition>)
constexpr ptrdiff_t component_colors = 136;          // m_colors (array<replication.master.property_s32>: buffer, count, element size)
constexpr ptrdiff_t property_modified = 16;          // replication.master.property.m_is_modified (bool)
constexpr ptrdiff_t property_s32_value = 24;         // property_type_simple<s32>._m_value
constexpr ptrdiff_t definition_id = 0;               // vehicle_component_definition.m_id (string)
constexpr ptrdiff_t definition_mesh_static = 224;     // vehicle_component_definition.m_mesh_static (mesh_composite)
constexpr ptrdiff_t pump_factor_speed = 480;         // gas_pump_centrifugal.m_factor_speed (replicated, drives the client's sound)
constexpr ptrdiff_t pump_content_0 = 528;            // m_content_0: compressor inlet
constexpr ptrdiff_t pump_content_1 = 584;            // m_content_1: compressor outlet
constexpr ptrdiff_t pump_node_a = 640;               // m_gas_node_a (inlet node)
constexpr ptrdiff_t pump_node_b = 704;               // m_gas_node_b (outlet node)
constexpr ptrdiff_t pump_pressure = 872;             // m_pressure (head; shown in the stock tooltip)
constexpr ptrdiff_t pump_display_power = 888;        // m_display_power
constexpr ptrdiff_t pump_flow = 896;                 // m_flow (litres this tick)
constexpr ptrdiff_t pump_torque = 808;               // m_torque (array<vehicle_torque_node>: buffer, count, element size); node 0 is the shaft
constexpr ptrdiff_t torque_node_component = 0;       // vehicle_torque_node.m_component
constexpr int32_t torque_node_size = 104;
constexpr ptrdiff_t vehicle_gas_nodes = 2040;        // server_scene.vehicle.m_nodes_gas (vehicle_gas_node.container)
constexpr ptrdiff_t content_amounts = 0;             // vehicle_gas_content.m_amounts (array<f64> per gas type: buffer, count)
constexpr ptrdiff_t content_volume = 32;             // vehicle_gas_content.m_volume (f64, litres)
constexpr ptrdiff_t content_capacity = 40;           // m_capacity (f64, litres)
constexpr ptrdiff_t content_temperature = 48;        // m_temperature (f64, C)
constexpr ptrdiff_t node_component = 0;              // vehicle_gas_node.m_component
constexpr ptrdiff_t node_component_flood = 8;        // vehicle_gas_node.m_component_flood (asked which nodes it floods to)
constexpr ptrdiff_t node_island = 48;                // vehicle_gas_node.m_island (ptr<vehicle_gas_island>; its m_nodes vector at +0)
constexpr ptrdiff_t node_node_index = 20;
constexpr ptrdiff_t node_content = 24;              // vehicle_gas_node.m_content
constexpr ptrdiff_t tank_content = 480;             // gas_tank.m_content (the intercooler is a gas tank)
constexpr ptrdiff_t port_mated_component = 544;     // gas_port.m_mated_component (the part a gas port is snapped onto)
constexpr ptrdiff_t container_nodes = 8;             // vehicle_gas_node.container.m_nodes (vector<ptr<vehicle_gas_node>>)
constexpr ptrdiff_t client_definition = 176;         // client_scene.vehicle_component.m_definition
constexpr ptrdiff_t client_factor_speed = 0xe0 + 8;  // client gas_pump_centrifugal.m_factor_speed's value (f64)
constexpr ptrdiff_t mat34_t = 72;                    // mat34.t (vec3)            // vehicle_gas_node.m_node_index (s32; -1 when not found)
constexpr size_t content_size = 56, node_size = 64, definition_file_size = 40;
constexpr int container_create_node_slot = 3, container_destroy_node_slot = 5;   // vehicle_gas_node.container method table
// Upkeep (oil ports, intercoolers, engines), Anymaker 0.1.25.
constexpr ptrdiff_t vehicle_id = 64;                 // server_scene.vehicle.m_id (s32, saved)
constexpr ptrdiff_t vehicle_liquid_nodes = 1944;     // m_nodes_liquid (vehicle_liquid_node.container; create/destroy at the gas slots)
constexpr ptrdiff_t component_grid = 328;            // vehicle_component.m_grid; its m_grid_origin_dir value sits at +48
constexpr ptrdiff_t grid_origin_dir = 48;
constexpr ptrdiff_t component_extend = 296;          // m_extend_size's value (vec3_s32): cells added by stretching
constexpr ptrdiff_t engine_air_manifolds = 1256;     // engine.m_air_manifolds (vector<ptr<air_manifold>>)
constexpr ptrdiff_t engine_damage_factor = 1456;     // engine.m_damage_factor (f64; a damage step at 1)
constexpr ptrdiff_t engine_rps = 1424;               // engine.m_rps (f64; the tick reads it before setting the new speed)
constexpr ptrdiff_t manifold_content = 576;          // air_manifold.m_content_gas (vehicle_gas_content)
constexpr ptrdiff_t manifold_air_temperature = 624;  // air_manifold.m_content_gas.m_temperature
constexpr size_t liquid_content_size = 232, liquid_node_size = 80;
constexpr int32_t kLiquidOil = 2, kLiquidPollutant = 3;   // server_scene.e_vehicle_liquid_type
}  // namespace off
constexpr int32_t kStoreSystem = 1;                  // file.e_store.system (as GpsPart)
constexpr int32_t kGasAir = 0;                       // server_scene.e_vehicle_gas_type.air

template <class T> T& at(void* base, ptrdiff_t offset) { return *reinterpret_cast<T*>(static_cast<uint8_t*>(base) + offset); }
template <class T> const T& at(const void* base, ptrdiff_t offset) { return *reinterpret_cast<const T*>(static_cast<const uint8_t*>(base) + offset); }

// ------------------------------------------------------------------------------------- game functions
using self_fn_t = void (*)(void* self);
using content_create_t = void (*)(void* content, void* vehicle, const void* component, const int32_t* layer, const double* capacity_factor);
using content_modify_t = void (*)(void* content, const int32_t* type, const double* amount, const double* temperature);
using content_move_t = void (*)(void* from, void* to, const double* volume);
using create_node_t = void (*)(void* container, void* node, void* component, void* const* component_flood, const int32_t* index, void* const* content);
using destroy_node_t = void (*)(void* container, void* node);
using property_set_t = void (*)(void* property, const double* value);
using string_ctor_t = void (*)(anymaker::gc_string_view*, const char*);
using get_definition_t = void (*)(const void** ret, const void* container, const anymaker::gc_string_view* id);
using liquid_create_t = void (*)(void* content, void* vehicle, const void* component, const void* grid_origin_dir, const int32_t* layer, const double* capacity_factor);
using liquid_tick_t = void (*)(void* content, void* vehicle, const double* static_pressure);
using liquid_move_t = void (*)(double* ret, void* from, void* to, const double* amount);
using liquid_modify_t = void (*)(void* content, const int32_t* type, const double* amount);
using create_liquid_node_t = void (*)(void* container, void* node, void* component, void* const* component_flood, const int32_t* index, void* const* content, const bool* expandable);
using get_rps_t = void (*)(double* ret, const void* node);
using apply_torque_t = void (*)(void* node, const double* torque);

self_fn_t g_content_ctor, g_content_dtor, g_node_ctor, g_node_dtor;
content_create_t g_content_create;
content_modify_t g_content_modify;
content_move_t g_content_move;
create_node_t g_create_node_bound;   // located by code; cross-checked against the container's method table
property_set_t g_property_set;
string_ctor_t g_string_ctor;
get_definition_t g_get_definition;
// Oil ports. Without them (these not found, or the game not binding a turbo's oil nodes) a turbo counts as oil fed,
// so it never wears for want of oil.
self_fn_t g_liquid_ctor, g_liquid_dtor, g_liquid_node_ctor, g_liquid_node_dtor;
liquid_create_t g_liquid_create;
liquid_tick_t g_liquid_tick;
liquid_move_t g_liquid_move;
liquid_modify_t g_liquid_modify;
bool g_oil_ready;
// Superchargers: their shaft's speed and the drag of the air they push. Without them a supercharger makes no boost.
get_rps_t g_get_rps;
apply_torque_t g_apply_torque;
const double* g_newtons_per_joule;
// Inline blow-off valves: the island flood joins both their ports into one pipe (see hk_flood). Off if the
// two-node flood function was not found; the valve then evens out its two sides every tick instead.
using flood_nodes_t = void (*)(void* self, void* out_nodes);
flood_nodes_t g_two_node_flood;
bool g_bov_inline;
const double* g_delta_time;

// ------------------------------------------------------------------------------------- per-turbo state
// The hot side's gas lives in memory the mod owns; the game holds pointers to these nodes and contents in the
// vehicle's gas node container until the component is destroyed. Process heap, so it outlives the DLL.
struct HotSide {
    alignas(16) uint8_t content[2][off::content_size];
    alignas(16) uint8_t node[2][off::node_size];
};
// The oil feed: two liquid contents and nodes (oil in, oil out) for the turbo's two liquid logic nodes.
struct OilSide {
    alignas(16) uint8_t content[2][off::liquid_content_size];
    alignas(16) uint8_t node[2][off::liquid_node_size];
};
struct Turbo {
    int size = 0;                                    // cells across (a supercharger's too: 2 or 4)
    bool blower = false;                             // a supercharger: belt driven, no hot side, oil or wear
    bool large = false;                              // supercharger: the large one
    Spec spec;
    HotSide* hot = nullptr;
    OilSide* oil = nullptr;
    bool oil_ports = false;                          // its oil nodes bound (oil_bound): without, it counts as oil fed
    UpkeepState up;                                  // its temperature (the condition lives in g_wear, saved)
    double oil_lps = 0;                              // smoothed
    double last_upkeep_log = -1e9;
    double last_pipe_log = -1e9;                     // when it last logged what is on a boost pipe without an intercooler
    double topology_at = -1e9;                       // when the pipes around it were last looked at (refresh_topology)
    const void* upstream = nullptr;                  // compound: the turbo whose boost its compressor breathes, or nullptr
    double cooler_cells = 0;                         // intercoolers on its boost pipe, total frontal cells
    int coolers = 0;
    double boost_capacity = 0;                       // litres its whole boost pipe holds (refresh_topology)
    int feeders = 1;                                 // compressors pushing into that pipe, itself included
    void* filter = nullptr;                          // the air filter on its intake this tick (a second stage: none)
    State state;
    double last_log = -1e9;
    int ticks = 0;
    double boost = 0;                                // cold outlet minus cold inlet, game pressure units (tooltip)
    bool was_surging = false, was_venting = false;   // for logging the changes
    // What its boost pipe feeds (refresh_topology), for the Dyno's name of the boost setup.
    const void* feeds_engine = nullptr;              // the engine whose air manifold is on it
    const void* feeds_stage = nullptr;               // the turbo or supercharger whose intake is on it
    const void* vehicle = nullptr;
    double ticked_at = -1e9;
    double drive_kw = 0;                             // supercharger: smoothed power its belt takes (log)
};
std::unordered_map<const void*, Turbo> g_turbos;     // keyed by server component (superchargers too); nodes keep their address
// Air manifolds, noted by the engine tick hook: manifold component -> the engine breathing through it, and when.
struct ManifoldSeen { const void* engine = nullptr; double at = -1e9; };
std::unordered_map<const void*, ManifoldSeen> g_manifolds;
// Two-step launch limiters, by engine: the RPM a wastegate's two_step input asks for (renewed by the turbo ticks),
// and the limiter's state (hk_engine_tick).
struct Launch { double rpm = 0, at = -1e9, cut_at = -1e9; bool cutting = false; };
std::unordered_map<const void*, Launch> g_launch;
struct ValvePart {
    int kind = 0;
    ValveState state;
    double* setting = nullptr;                        // blow-off spring (kPa) then sound (BlowOffSound): process heap,
                                                      // never freed (the Properties Tool holds pointers to them)
    double vented = 0;                                // litres the turbos vented through it since its last tick
    double open_until = 0;                            // seconds (now_seconds) it shows open after venting
    const void* vented_by = nullptr;                  // the turbo that vented through it since its last tick
    double antilag_until = 0;                         // wastegate: seconds (now_seconds) it shows antilag firing
    double antilag_level = 0;                         // how hard (0-1), for the bangs
    int antilag_size = 3;                             // the turbo it fires for
    int vent_size = 3;                                // size of the turbo that last vented through it (its sound)
    double last_tick = 0;                             // seconds (now_seconds) of its last own server tick
};
std::unordered_map<const void*, ValvePart> g_valves;
std::unordered_map<const void*, int32_t> g_painted_parts;   // air filters and intercoolers given their starting colours (they have
                                                     // no create hook): address -> component id, as the game reuses addresses
std::unordered_map<const void*, int> g_definition_size;   // definition pointer -> 2-5, or 0 for other parts
std::recursive_mutex g_mutex;                        // tick, create, destroy and data lookups (recursive: game calls made
                                                     // under it may come back through our hooks)

double now_seconds() {
    static LARGE_INTEGER freq = [] { LARGE_INTEGER f; QueryPerformanceFrequency(&f); return f; }();
    LARGE_INTEGER t; QueryPerformanceCounter(&t);
    return double(t.QuadPart) / double(freq.QuadPart);
}

// What a gas_pump_centrifugal component is: 3 or 6 a turbo of that size, kKindBlowOff(Large) / kKindWastegate a valve,
// kKindSupercharger(Large) a supercharger, 0 a stock impeller.
constexpr int kKindBlowOff = 10, kKindWastegate = 11, kKindBlowOffLarge = 12, kKindWastegateLarge = 13;
constexpr int kKindIntercooler = 14, kKindAirFilter = 16, kKindAirFilterLarge = 17;
constexpr int kKindSupercharger = 20, kKindSuperchargerLarge = 21;
bool is_blower(int kind) { return kind == kKindSupercharger || kind == kKindSuperchargerLarge; }
bool is_intercooler(int kind) { return kind == kKindIntercooler; }
bool is_air_filter(int kind) { return kind == kKindAirFilter || kind == kKindAirFilterLarge; }
bool is_blow_off(int kind) { return kind == kKindBlowOff || kind == kKindBlowOffLarge; }
bool is_wastegate(int kind) { return kind == kKindWastegate || kind == kKindWastegateLarge; }
bool is_valve(int kind) { return is_blow_off(kind) || is_wastegate(kind); }
int kind_of_definition(const void* definition) {
    if (!definition) return 0;
    auto found = g_definition_size.find(definition);
    if (found != g_definition_size.end()) return found->second;
    int n = 0;
    if (anymaker::readable(static_cast<const uint8_t*>(definition) + off::definition_id, sizeof(anymaker::gc_string_view))) {
        auto& id = at<anymaker::gc_string_view>(definition, off::definition_id);
        if (id.data && id.length > 0 && id.length < 64 && anymaker::readable(id.data, size_t(id.length))) {
            n = size_from_id(id.data, size_t(id.length));
            Valve v = valve_from_id(id.data, size_t(id.length));
            if (v == kBlowOff) n = kKindBlowOff;
            else if (v == kBlowOffLarge) n = kKindBlowOffLarge;
            else if (v == kWastegate) n = kKindWastegate;
            else if (v == kWastegateLarge) n = kKindWastegateLarge;
            else if (v == kIntercooler) n = kKindIntercooler;
            else if (v == kAirFilter) n = kKindAirFilter;
            else if (v == kAirFilterLarge) n = kKindAirFilterLarge;
            Blower b = blower_from_id(id.data, size_t(id.length));
            if (b == kBlowerSmall) n = kKindSupercharger;
            else if (b == kBlowerLarge) n = kKindSuperchargerLarge;
        }
    }
    g_definition_size[definition] = n;
    return n;
}
int part_kind(const void* component) {
    if (!component || !anymaker::readable(static_cast<const uint8_t*>(component) + off::component_definition, 8)) return 0;
    return kind_of_definition(at<const void*>(component, off::component_definition));
}
int turbo_size(const void* component) { int k = part_kind(component); return k >= kMinSize && k <= kMaxSize ? k : 0; }

void* container_of(void* vehicle) { return static_cast<uint8_t*>(vehicle) + off::vehicle_gas_nodes; }
void* container_method(void* container, int slot) {
    if (!container || !anymaker::readable(container, 8)) return nullptr;
    auto ti = anymaker::typeinfo_of(container);
    if (!ti.p || !anymaker::readable(ti.p + 0x80, 8) || !ti.table() || !anymaker::readable(ti.table() + slot, 8)) return nullptr;
    return ti.method(slot);
}

double pressure_of(const void* content) {
    double capacity = at<double>(content, off::content_capacity);
    return capacity > 0 ? at<double>(content, off::content_volume) / capacity : 1.0;
}

// Builds the hot side: two contents sized like the impeller's own, filled with ambient air, and two gas nodes
// for logic nodes 2 and 3 (gas ordinals 2 and 3 of the definition).
HotSide* create_hot_side(void* component, void* vehicle) {
    void* container = container_of(vehicle);
    auto create_node = reinterpret_cast<create_node_t>(container_method(container, off::container_create_node_slot));
    if (!create_node) { log(2, "gas node container not readable; this turbo has no hot side"); return nullptr; }
    static std::atomic<bool> checked{false};
    if (!checked.exchange(true) && g_create_node_bound && reinterpret_cast<void*>(create_node) != reinterpret_cast<void*>(g_create_node_bound))
        log(1, "container create_node slot (%p) differs from the located function (%p); using the slot", reinterpret_cast<void*>(create_node), reinterpret_cast<void*>(g_create_node_bound));
    auto* hot = static_cast<HotSide*>(HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(HotSide)));
    if (!hot) return nullptr;
    for (int i = 0; i < 2; ++i) {
        g_content_ctor(hot->content[i]);
        g_node_ctor(hot->node[i]);
        int32_t layer = 0; double factor = 0.25;
        g_content_create(hot->content[i], vehicle, component, &layer, &factor);
        double amount = at<double>(hot->content[i], off::content_capacity), temperature = kAmbientC;
        int32_t air = kGasAir;
        if (amount > 0) g_content_modify(hot->content[i], &air, &amount, &temperature);
        void* flood = nullptr; int32_t index = kHotIn + i; void* content = hot->content[i];
        create_node(container, hot->node[i], component, &flood, &index, &content);
    }
    if (at<int32_t>(hot->node[0], off::node_node_index) < 0 || at<int32_t>(hot->node[1], off::node_node_index) < 0)
        log(2, "the game did not find the turbo's hot-side logic nodes (indices %d, %d)", at<int32_t>(hot->node[0], off::node_node_index), at<int32_t>(hot->node[1], off::node_node_index));
    return hot;
}

void destroy_hot_side(HotSide* hot, void* vehicle) {
    if (!hot) return;
    void* container = vehicle ? container_of(vehicle) : nullptr;
    auto destroy_node = reinterpret_cast<destroy_node_t>(container_method(container, off::container_destroy_node_slot));
    if (!destroy_node) { log(2, "gas node container not readable at destroy; leaking a turbo hot side rather than risk a crash"); return; }
    for (int i = 1; i >= 0; --i) destroy_node(container, hot->node[i]);
    for (int i = 0; i < 2; ++i) { g_node_dtor(hot->node[i]); g_content_dtor(hot->content[i]); }
    HeapFree(GetProcessHeap(), 0, hot);
}

// Sets a content to ambient: tops it up with air, or removes the excess from every gas in proportion.
void hold_ambient(void* content) {
    double capacity = at<double>(content, off::content_capacity), volume = at<double>(content, off::content_volume);
    if (!(capacity > 0) || !std::isfinite(volume)) return;
    double temperature = kAmbientC;
    if (volume < capacity - 1e-6) {
        double missing = capacity - volume;
        int32_t air = kGasAir;
        g_content_modify(content, &air, &missing, &temperature);
    } else if (volume > capacity + 1e-6) {
        double excess = volume - capacity;
        const double* amounts = at<const double*>(content, off::content_amounts);
        int32_t count = at<int32_t>(content, off::content_amounts + 8);
        if (!amounts || count <= 0 || count > 8 || !anymaker::readable(amounts, sizeof(double) * size_t(count))) return;
        double removal[8]{};
        for (int32_t i = 0; i < count; ++i) removal[i] = -excess * std::max(0.0, amounts[i]) / volume;
        for (int32_t i = 0; i < count; ++i) if (removal[i] < 0) g_content_modify(content, &i, &removal[i], &temperature);
    }
}

// Removes `litres` from a content, from every gas in proportion.
void remove_volume(void* content, double litres) {
    double volume = at<double>(content, off::content_volume);
    if (!(litres > 0) || !(volume > 0) || !std::isfinite(volume)) return;
    litres = std::min(litres, volume);
    const double* amounts = at<const double*>(content, off::content_amounts);
    int32_t count = at<int32_t>(content, off::content_amounts + 8);
    if (!amounts || count <= 0 || count > 8 || !anymaker::readable(amounts, sizeof(double) * size_t(count))) return;
    double removal[8]{}, temperature = kAmbientC;
    for (int32_t i = 0; i < count; ++i) removal[i] = -litres * std::max(0.0, amounts[i]) / volume;
    for (int32_t i = 0; i < count; ++i) if (removal[i] < 0) g_content_modify(content, &i, &removal[i], &temperature);
}

std::vector<uint8_t> read_file(const std::wstring& path);
bool write_file(const std::wstring& path, const void* data, size_t size);

// ------------------------------------------------------------------------------------- oil feed
// Builds the oil side like the stock fuel manifold builds its liquid port: a liquid content sized for the part and a
// liquid node bound to liquid logic node 0 (oil in) and 1 (oil out), registered in the vehicle's liquid container.
void* liquid_container_of(void* vehicle) { return static_cast<uint8_t*>(vehicle) + off::vehicle_liquid_nodes; }
// Both oil nodes bound to the part's liquid logic nodes: pipes can reach them.
bool oil_bound(OilSide* oil) { return oil && at<int32_t>(oil->node[0], off::node_node_index) >= 0 && at<int32_t>(oil->node[1], off::node_node_index) >= 0; }
OilSide* create_oil_side(void* component, void* vehicle) {
    if (!g_oil_ready) return nullptr;
    void* container = liquid_container_of(vehicle);
    auto create_node = reinterpret_cast<create_liquid_node_t>(container_method(container, off::container_create_node_slot));
    void* grid = anymaker::readable(static_cast<uint8_t*>(component) + off::component_grid, 8) ? at<void*>(component, off::component_grid) : nullptr;
    if (!create_node || !grid || !anymaker::readable(static_cast<uint8_t*>(grid) + off::grid_origin_dir, 24)) {
        log(1, "liquid node container or part grid not readable; this turbo has no oil ports");
        return nullptr;
    }
    const void* origin = static_cast<uint8_t*>(grid) + off::grid_origin_dir;
    auto* oil = static_cast<OilSide*>(HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(OilSide)));
    if (!oil) return nullptr;
    for (int i = 0; i < 2; ++i) {
        g_liquid_ctor(oil->content[i]);
        g_liquid_node_ctor(oil->node[i]);
        int32_t layer = 0; double factor = 0.1;
        g_liquid_create(oil->content[i], vehicle, component, origin, &layer, &factor);
        void* flood = nullptr; int32_t index = i; void* content = oil->content[i]; bool expandable = false;
        create_node(container, oil->node[i], component, &flood, &index, &content, &expandable);
    }
    if (!oil_bound(oil))
        log(1, "the game did not find a turbo's oil logic nodes (indices %d, %d); that turbo counts as oil fed", at<int32_t>(oil->node[0], off::node_node_index),
            at<int32_t>(oil->node[1], off::node_node_index));
    static std::atomic<int> logs{6};
    if (logs.fetch_sub(1) > 0)
        log(0, "oil ports: nodes %d/%d, contents %.3f/%.3f L", at<int32_t>(oil->node[0], off::node_node_index), at<int32_t>(oil->node[1], off::node_node_index),
            at<double>(oil->content[0], off::content_capacity), at<double>(oil->content[1], off::content_capacity));
    return oil;
}
void destroy_oil_side(OilSide* oil, void* vehicle) {
    if (!oil) return;
    void* container = vehicle ? liquid_container_of(vehicle) : nullptr;
    auto destroy_node = reinterpret_cast<destroy_node_t>(container_method(container, off::container_destroy_node_slot));
    if (!destroy_node) { log(2, "liquid node container not readable at destroy; leaking a turbo's oil ports rather than risk a crash"); return; }
    for (int i = 1; i >= 0; --i) destroy_node(container, oil->node[i]);
    for (int i = 0; i < 2; ++i) { g_liquid_node_dtor(oil->node[i]); g_liquid_dtor(oil->content[i]); }
    HeapFree(GetProcessHeap(), 0, oil);
}
// Oil and pollutant litres in a liquid content.
bool oil_amounts(const void* content, double& oil, double& pollutant) {
    const double* amounts = at<const double*>(content, off::content_amounts);
    int32_t count = at<int32_t>(content, off::content_amounts + 8);
    if (!amounts || count <= off::kLiquidPollutant || count > 8 || !anymaker::readable(amounts, sizeof(double) * size_t(count))) return false;
    oil = std::max(0.0, amounts[off::kLiquidOil]); pollutant = std::max(0.0, amounts[off::kLiquidPollutant]);
    return std::isfinite(oil) && std::isfinite(pollutant);
}

// ------------------------------------------------------------------------------------- saved wear
// Turbo and air filter condition, in a file in the game's working folder (turbo_wear_store.h). The game swaps that
// folder's contents when a world loads, so the file is watched: a new file (or none) means another world.
WearMap g_wear;
std::wstring g_wear_path;                            // empty: wear is kept for this session only
struct FileStamp { bool exists = false; FILETIME created{}, written{}; uint64_t size = 0; };
FileStamp g_wear_stamp;
bool g_wear_dirty = false;
double g_wear_polled = -1e9, g_wear_written = -1e9;

FileStamp stamp_of(const std::wstring& path) {
    FileStamp s;
    WIN32_FILE_ATTRIBUTE_DATA a{};
    if (!GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &a)) return s;
    s.exists = true; s.created = a.ftCreationTime; s.written = a.ftLastWriteTime;
    s.size = (uint64_t(a.nFileSizeHigh) << 32) | a.nFileSizeLow;
    return s;
}
bool same_stamp(const FileStamp& a, const FileStamp& b) {
    if (a.exists != b.exists) return false;
    return !a.exists || (CompareFileTime(&a.created, &b.created) == 0 && CompareFileTime(&a.written, &b.written) == 0 && a.size == b.size);
}
void init_wear_path() {
    wchar_t buf[MAX_PATH];
    DWORD n = GetEnvironmentVariableW(L"APPDATA", buf, MAX_PATH);
    if (!n || n >= MAX_PATH) { log(1, "APPDATA not set; turbo wear is not saved"); return; }
    std::wstring dir = std::wstring(buf) + L"\\Anymaker\\working";
    DWORD attributes = GetFileAttributesW(dir.c_str());
    // The folder exists while a world is loaded; before that, look for the game's user folder.
    if (attributes == INVALID_FILE_ATTRIBUTES && GetFileAttributesW((std::wstring(buf) + L"\\Anymaker").c_str()) == INVALID_FILE_ATTRIBUTES) {
        log(1, "the game's user folder (%%APPDATA%%\\Anymaker) was not found; turbo wear is not saved");
        return;
    }
    g_wear_path = dir + L"\\anyengineparts_wear.json";
    log(0, "turbo wear is saved with each world (working folder %s)", attributes == INVALID_FILE_ATTRIBUTES ? "not there yet" : "found");
}
// Reloads the wear when the world changed under us; writes ours every few seconds when it changed. Server tick.
void sync_wear(double now) {
    if (g_wear_path.empty() || now - g_wear_polled < 0.5) return;
    g_wear_polled = now;
    FileStamp s = stamp_of(g_wear_path);
    if (!same_stamp(s, g_wear_stamp)) {
        if (s.exists) {
            std::vector<uint8_t> text = read_file(g_wear_path);
            g_wear = decode_wear(std::string(text.begin(), text.end()));
            log(0, "turbo wear loaded: %zu parts", g_wear.size());
        } else if (g_wear_stamp.exists) {
            g_wear.clear();                              // another world, without any wear yet
            log(0, "turbo wear file gone (another world); starting fresh");
        }
        g_wear_stamp = s;
        g_wear_dirty = false;
        return;
    }
    if (!g_wear_dirty || now - g_wear_written < 5.0) return;
    std::string text = encode_wear(g_wear);
    std::wstring temp = g_wear_path + L".tmp";
    if (!write_file(temp, text.data(), text.size()) || !MoveFileExW(temp.c_str(), g_wear_path.c_str(), MOVEFILE_REPLACE_EXISTING)) {
        static std::atomic<int> logs{3};
        if (logs.fetch_sub(1) > 0) log(1, "could not write the turbo wear file (error %lu)", GetLastError());
        g_wear_written = now;
        return;
    }
    g_wear_stamp = stamp_of(g_wear_path);
    g_wear_written = now;
    g_wear_dirty = false;
}
double& wear_of(char kind, void* vehicle, const void* component) {
    WearKey key{kind, at<int32_t>(vehicle, off::vehicle_id), at<int32_t>(component, off::component_id)};
    auto found = g_wear.find(key);
    if (found == g_wear.end()) found = g_wear.emplace(key, 1.0).first;
    return found->second;
}

// ------------------------------------------------------------------------------------- impeller hooks
using create_t = void (*)(void* self, void* vehicle);
using destroy_t = void (*)(void* self, void* vehicle, void* scene);
using tick_t = void (*)(void* self, void* vehicle, void* scene);
using get_f64_t = void (*)(double** ret, void* self, const anymaker::gc_string_view* name);
anymaker::cell_hook h_create, h_destroy, h_tick, h_get_f64;

// The valve's settings (spring kPa, sound), allocated on first use; the save data may be read before create.
double* valve_setting(ValvePart& v) {
    if (!v.setting) {
        v.setting = static_cast<double*>(HeapAlloc(GetProcessHeap(), 0, 2 * sizeof(double)));
        if (v.setting) { v.setting[0] = kBlowOffDefaultKpa; v.setting[1] = kWhoosh; }
    }
    return v.setting;
}

// Starting colours, as indices into the game's colour palette (rom/textures/color_palette.txtr): the meshes mark the
// turbo's cold and hot housings and each valve's body and cap as paint slots 0 and 1 (turbo_mesh.h).
// Starting colours (palette indices into rom/textures/color_palette.txtr, and their colours for the preview).
constexpr int32_t kPaletteSteel = 81, kPaletteCastIron = 79, kPaletteBrightSteel = 83, kPaletteBrightRed = 36;
struct StartColours { int32_t slot[2]; uint8_t rgb[2][3]; };
constexpr StartColours kTurboColours{{kPaletteSteel, kPaletteCastIron}, {{197, 199, 196}, {55, 67, 69}}};
constexpr StartColours kValveColours{{kPaletteBrightSteel, kPaletteBrightRed}, {{236, 236, 231}, {204, 44, 36}}};
constexpr StartColours kFilterColours{{kPaletteBrightRed, kPaletteBrightSteel}, {{204, 44, 36}, {236, 236, 231}}};   // element, caps
constexpr StartColours kCoolerColours{{kPaletteBrightSteel, kPaletteSteel}, {{236, 236, 231}, {197, 199, 196}}};     // tanks, end caps
constexpr StartColours kBlowerColours{{kPaletteBrightSteel, kPaletteCastIron}, {{236, 236, 231}, {55, 67, 69}}};     // housing, gear case

// Sets paint slots 0 and 1, as the paint tool would (the property's modified flag sends it to the clients). With
// only_new, only a newly placed part, whose slots all hold the same default; a part that was painted (or whose saved
// colours are already in) is left alone.
void set_paint(void* self, int32_t slot0, int32_t slot1, bool only_new) {
    auto* colors = static_cast<uint8_t*>(self) + off::component_colors;
    if (!anymaker::readable(colors, 16)) return;
    auto* buffer = at<uint8_t*>(colors, 0);
    int32_t count = at<int32_t>(colors, 8), size = at<int32_t>(colors, 12);
    if (!buffer || count < 2 || count > 16 || size < off::property_s32_value + 4 || !anymaker::readable(buffer, size_t(count) * size_t(size))) return;
    int32_t first = at<int32_t>(buffer, off::property_s32_value);
    bool fresh = true;
    for (int32_t k = 1; k < count; ++k) fresh = fresh && at<int32_t>(buffer + size_t(k) * size_t(size), off::property_s32_value) == first;
    static std::atomic<int> logs{6};
    if (only_new && logs.fetch_sub(1) > 0)
        log(0, "component %d colours: slot 0 = %d, slot 1 = %d (%s)", at<int32_t>(self, off::component_id), first,
            at<int32_t>(buffer + size_t(size), off::property_s32_value), fresh ? "new: starting colours applied" : "painted: kept");
    if (only_new && !fresh) return;
    const int32_t want[2] = {slot0, slot1};
    for (int k = 0; k < 2; ++k) {
        uint8_t* property = buffer + size_t(k) * size_t(size);
        at<int32_t>(property, off::property_s32_value) = want[k];
        at<bool>(property, off::property_modified) = true;
    }
}
void default_paint(void* self, int32_t slot0, int32_t slot1) { set_paint(self, slot0, slot1, true); }
// True the first time a part without a create hook is seen. The address alone is not enough: the game reuses a freed
// part's address for a new part, which then needs its starting colours too, so the component id is kept with it.
bool first_sight(const void* part) {
    const auto* id_at = static_cast<const uint8_t*>(part) + off::component_id;
    int32_t id = anymaker::readable(id_at, 4) ? at<int32_t>(part, off::component_id) : -1;
    auto [it, added] = g_painted_parts.try_emplace(part, id);
    if (added) return true;
    if (it->second == id) return false;
    it->second = id;
    return true;
}
// A blown turbo goes sooty: both housings dark (palette 63, a burnt dark brown).
constexpr int32_t kPaletteSoot = 63;

void hk_create(void* self, void* vehicle) {
    reinterpret_cast<create_t>(h_create.original)(self, vehicle);
    if (!self || !vehicle) return;
    std::lock_guard<std::recursive_mutex> lock(g_mutex);
    int kind = part_kind(self);
    if (is_valve(kind)) {
        ValvePart& v = g_valves[self];
        v.kind = kind;
        valve_setting(v);
        default_paint(self, kValveColours.slot[0], kValveColours.slot[1]);
        if (is_blow_off(kind) && g_bov_inline) {
            // Make both gas nodes ask this component which nodes they flood to (hk_flood joins them).
            at<void*>(static_cast<uint8_t*>(self) + off::pump_node_a, off::node_component_flood) = self;
            at<void*>(static_cast<uint8_t*>(self) + off::pump_node_b, off::node_component_flood) = self;
        }
        static std::atomic<int> logs{8};
        if (logs.fetch_sub(1) > 0)
            log(0, "%s created (component %d): contents %.2f/%.2f L", is_blow_off(kind) ? "Blow-off valve" : "Wastegate", at<int32_t>(self, off::component_id),
                at<double>(self, off::pump_content_0 + off::content_capacity), at<double>(self, off::pump_content_1 + off::content_capacity));
        return;
    }
    if (is_blower(kind)) {
        Turbo& t = g_turbos[self];
        if (t.blower) return;
        t.blower = true; t.large = kind == kKindSuperchargerLarge;
        t.spec = blower_spec(t.large); t.size = t.spec.size;
        default_paint(self, kBlowerColours.slot[0], kBlowerColours.slot[1]);
        static std::atomic<int> logs{8};
        if (logs.fetch_sub(1) > 0)
            log(0, "Supercharger (%s) created (component %d): contents %.2f/%.2f L", t.large ? "large" : "small", at<int32_t>(self, off::component_id),
                at<double>(self, off::pump_content_0 + off::content_capacity), at<double>(self, off::pump_content_1 + off::content_capacity));
        return;
    }
    int n = turbo_size(self);
    if (!n) return;
    Turbo& t = g_turbos[self];
    if (t.hot) return;
    t.size = n; t.spec = spec(n);
    default_paint(self, kTurboColours.slot[0], kTurboColours.slot[1]);
    t.hot = create_hot_side(self, vehicle);
    t.oil = create_oil_side(self, vehicle);
    t.oil_ports = oil_bound(t.oil);
    static std::atomic<int> logs{8};
    if (logs.fetch_sub(1) > 0 && t.hot)
        log(0, "Turbo %dx%d created (component %d): hot side nodes %d/%d, contents %.2f/%.2f L, impeller contents %.2f/%.2f L", n, n, at<int32_t>(self, off::component_id),
            at<int32_t>(t.hot->node[0], off::node_node_index), at<int32_t>(t.hot->node[1], off::node_node_index), at<double>(t.hot->content[0], off::content_capacity),
            at<double>(t.hot->content[1], off::content_capacity), at<double>(self, off::pump_content_0 + off::content_capacity), at<double>(self, off::pump_content_1 + off::content_capacity));
}

void hk_destroy(void* self, void* vehicle, void* scene) {
    {
        std::lock_guard<std::recursive_mutex> lock(g_mutex);
        auto found = g_turbos.find(self);
        if (found != g_turbos.end()) { destroy_hot_side(found->second.hot, vehicle); destroy_oil_side(found->second.oil, vehicle); g_turbos.erase(found); }
        g_valves.erase(self);
    }
    reinterpret_cast<destroy_t>(h_destroy.original)(self, vehicle, scene);
}

// Every component's get_gas_flood_nodes shares this one cell (the base is empty; pipes and brackets override it).
// A blow-off valve answers with both its gas nodes, so the boost pipe runs straight through it to the intake.
using flood_t = void (*)(void* self, void* out_nodes);
anymaker::cell_hook h_flood;
void hk_flood(void* self, void* out_nodes) {
    reinterpret_cast<flood_t>(h_flood.original)(self, out_nodes);
    bool valve = false;
    {
        std::lock_guard<std::recursive_mutex> lock(g_mutex);
        auto found = g_valves.find(self);
        valve = found != g_valves.end() && is_blow_off(found->second.kind);
    }
    if (valve && g_two_node_flood)
        g_two_node_flood(static_cast<uint8_t*>(self) + off::pump_node_a - bind::kTwoNodeFloodFirst, out_nodes);
}

// ------------------------------------------------------------------------------------- valves
// A game vector of pointers (vector<ptr<T>>: buffer +0, ring start +8, count +12, capacity +16, element size +20). It is
// a ring (anymaker::gc_vector_raw): entry i sits at (start + i) % capacity, so a list that has wrapped past the end of
// its buffer carries on at the buffer's start (reading count entries straight on from the ring start missed those).
// read_ring leaves it empty and returns false when the fields do not look right.
struct PointerRing {
    void* const* buffer = nullptr;
    int32_t start = 0, count = 0, capacity = 0;
    void* operator[](int32_t i) const { return buffer[(int64_t(start) + i) % capacity]; }
};
bool read_ring(const void* vec, int32_t max_count, PointerRing& out) {
    out = PointerRing{};
    if (!vec || !anymaker::readable(vec, 24)) return false;
    auto* buffer = at<void* const*>(vec, 0);
    int32_t start = at<int32_t>(vec, 8), count = at<int32_t>(vec, 12), capacity = at<int32_t>(vec, 16), size = at<int32_t>(vec, 20);
    if (!buffer || count <= 0 || count > max_count || capacity < count || start < 0 || start >= capacity || size != 8 ||
        !anymaker::readable(buffer, size_t(capacity) * 8)) return false;
    out = PointerRing{buffer, start, count, capacity};
    return true;
}

// Calls f(node) for every gas node pooled with `node` (its island). The island's node vector is read defensively:
// every entry must point back at the same island, else nothing is visited.
template <class F> void for_each_on_island(void* node, F f) {
    if (!node || !anymaker::readable(static_cast<uint8_t*>(node) + off::node_island, 8)) return;
    void* island = at<void*>(node, off::node_island);
    PointerRing ring;
    if (!island || !read_ring(island, 4096, ring)) return;
    for (int32_t start : {ring.start, 0}) {           // from the ring's start, else (as before) from the buffer's
        PointerRing entries = ring;
        entries.start = start;
        bool ok = true;
        for (int32_t i = 0; i < entries.count && ok; ++i)
            ok = entries[i] && anymaker::readable(static_cast<uint8_t*>(entries[i]) + off::node_island, 8) && at<void*>(entries[i], off::node_island) == island;
        if (!ok) continue;
        for (int32_t i = 0; i < entries.count; ++i) f(entries[i]);
        return;
    }
}

// Blow-off valves on the turbo's boost pipe vent the boost the turbo can no longer hold. Returns true if any vented
// or is still open from venting (spring_kpa: the stiffest of them).
bool vent_blow_off_valves(void* turbo, int size, double excess, double dt, double& spring_kpa) {
    if (!(excess > 0) || !(dt > 0)) return false;
    bool vented = false, latched = false;
    double now = now_seconds();
    for_each_on_island(static_cast<uint8_t*>(turbo) + off::pump_node_b, [&](void* node) {
        void* component = at<void*>(node, off::node_component);
        auto found = g_valves.find(component);
        if (found == g_valves.end() || !is_blow_off(found->second.kind)) return;
        // Inline valves join the boost pipe through both ports; the old layout only through its inlet.
        if (!g_bov_inline && node != static_cast<uint8_t*>(component) + off::pump_node_a) return;
        if (found->second.vented_by == turbo) return; // once per tick (an inline valve shows up through both ports)
        double setting = found->second.setting ? *found->second.setting : kBlowOffDefaultKpa;
        // Once open it stays open through the lift: between puffs the boost sits under the spring, and counting
        // those ticks as surge made it chatter surge, vent, surge as the spool ran down.
        if (now < found->second.open_until) { spring_kpa = latched ? std::max(spring_kpa, setting) : setting; latched = true; }
        // Twin turbos on one boost pipe: the other already vented this tick's excess through it. It is open for this
        // one too (latched above), and venting again would take the same boost twice.
        if (found->second.vented_by) return;
        void* in = static_cast<uint8_t*>(component) + off::pump_content_0;
        double want = blow_off_vent(excess, setting, at<double>(in, off::content_volume), dt, found->second.kind == kKindBlowOffLarge);
        if (want <= 0) return;
        remove_volume(in, want);                      // out through the cap, to the air
        found->second.vented_by = turbo;
        found->second.vented += want;
        found->second.vent_size = size;
        found->second.open_until = now + 0.3;
        if (!latched) spring_kpa = vented ? std::max(spring_kpa, setting) : setting;
        vented = true;
    });
    return vented || latched;
}

// A wastegate with its antilag input on, on the turbo's exhaust inlet pipe; nullptr if none.
ValvePart* antilag_wastegate(Turbo& t) {
    if (!t.hot) return nullptr;
    ValvePart* found = nullptr;
    for_each_on_island(t.hot->node[0], [&](void* node) {
        auto v = g_valves.find(at<void*>(node, off::node_component));
        if (!found && v != g_valves.end() && is_wastegate(v->second.kind) && v->second.state.slots[kValveAntilag] > 0.5) found = &v->second;
    });
    return found;
}

// The highest two_step RPM a wastegate on the turbo's exhaust inlet pipe asks for (0: none).
double two_step_rpm(Turbo& t) {
    if (!t.hot) return 0;
    double rpm = 0;
    for_each_on_island(t.hot->node[0], [&](void* node) {
        auto v = g_valves.find(at<void*>(node, off::node_component));
        if (v != g_valves.end() && is_wastegate(v->second.kind) && std::isfinite(v->second.state.slots[kValveTwoStep]))
            rpm = std::max(rpm, std::min(v->second.state.slots[kValveTwoStep], kMaxTwoStepRpm));
    });
    return rpm;
}
// The engine a turbo's boost reaches: the one on its boost pipe, or through the stage its boost feeds (a compound).
const void* boosted_engine(const Turbo& t) {
    if (t.feeds_engine) return t.feeds_engine;
    auto next = t.feeds_stage ? g_turbos.find(t.feeds_stage) : g_turbos.end();
    return next != g_turbos.end() ? next->second.feeds_engine : nullptr;
}

void publish_valve(void* self, ValvePart& v, double litres, double dt);

// Evens out a part's two gas contents (the pressure on both sides becomes the same).
void level_sides(void* c0, void* c1) {
    double c0cap = at<double>(c0, off::content_capacity), c1cap = at<double>(c1, off::content_capacity);
    double v0 = at<double>(c0, off::content_volume), v1 = at<double>(c1, off::content_volume);
    if (!(c0cap > 0) || !(c1cap > 0)) return;
    double move = (v0 * c1cap - v1 * c0cap) / (c0cap + c1cap);
    if (move > 1e-9) g_content_move(c0, c1, &move);
    else if (move < -1e-9) { double back = -move; g_content_move(c1, c0, &back); }
}

// The intercooler a gas node belongs to, or nullptr. It is a gas tank: gas ports snapped onto it get nodes whose content
// is the tank's own (gas_tank.m_content, +480), so the tank is found from the content, whichever part owns the node.
const char* definition_id_of(const void* component, size_t& length) {
    length = 0;
    if (!component || !anymaker::readable(static_cast<const uint8_t*>(component) + off::component_definition, 8)) return nullptr;
    const void* definition = at<const void*>(component, off::component_definition);
    if (!definition || !anymaker::readable(static_cast<const uint8_t*>(definition) + off::definition_id, sizeof(anymaker::gc_string_view))) return nullptr;
    auto& id = at<anymaker::gc_string_view>(definition, off::definition_id);
    if (!id.data || id.length <= 0 || id.length > 64 || !anymaker::readable(id.data, size_t(id.length))) return nullptr;
    length = size_t(id.length);
    return id.data;
}
bool is_intercooler_part(const void* component) {
    size_t length = 0;
    const char* id = definition_id_of(component, length);   // not cached: most candidates here are not components at all
    return id && valve_from_id(id, length) == kIntercooler;
}
// The gas tank's own add_mating_component makes a snapped-on port's node with the tank's content, so the content leads
// back to the tank. The port also keeps the part it is snapped onto, which is the second way to the intercooler.
void* intercooler_of(void* node) {
    if (!anymaker::readable(static_cast<uint8_t*>(node), off::node_content + 8)) return nullptr;
    auto* content = at<uint8_t*>(node, off::node_content);
    if (content && reinterpret_cast<uintptr_t>(content) >= off::tank_content && is_intercooler_part(content - off::tank_content))
        return content - off::tank_content;
    auto* component = at<uint8_t*>(node, off::node_component);
    size_t length = 0;
    const char* id = definition_id_of(component, length);
    if (!id || length < 8 || std::memcmp(id, "gas_port", 8) != 0 || !anymaker::readable(component + off::port_mated_component, 8)) return nullptr;
    void* mated = at<void*>(component, off::port_mated_component);
    return mated && is_intercooler_part(mated) ? mated : nullptr;
}

void* island_of(void* node) {
    return node && anymaker::readable(static_cast<uint8_t*>(node) + off::node_island, 8) ? at<void*>(node, off::node_island) : nullptr;
}
// Calls add(node) for every other gas node of the vehicle holding `node`'s content: the other ports of a gas tank
// (each port snapped onto it is its own node, and often on its own island).
template <class F> void for_each_sharing_content(void* vehicle, void* node, F add) {
    if (!vehicle) return;
    const uint8_t* vec = static_cast<uint8_t*>(container_of(vehicle)) + off::container_nodes;
    const void* content = at<void*>(node, off::node_content);
    PointerRing entries;
    if (!content || !read_ring(vec, 8192, entries)) return;
    for (int32_t i = 0; i < entries.count; ++i) {
        void* other = entries[i];
        if (other && other != node && anymaker::readable(static_cast<uint8_t*>(other), off::node_island + 8) && at<void*>(other, off::node_content) == content) add(other);
    }
}

// Calls f(node) for every gas node the air reaches from `start` without passing a turbo: its island, and on through
// inline blow-off valves (an inline valve joins its two pipes into one, hk_flood, but if the game did not, the boost
// still crosses it, tick_valve) and intercoolers (each port snapped onto one holds its air, often on an island of its
// own). Each island once, at most kMaxIslands of them.
constexpr int kMaxIslands = 8;
template <class F> void for_each_connected(void* vehicle, void* start, F f) {
    void* islands[kMaxIslands]{};
    void* queue[kMaxIslands]{};
    int n = 0;
    auto add = [&](void* node) {
        void* island = island_of(node);
        if (!island || n == kMaxIslands || std::find(islands, islands + n, island) != islands + n) return;
        islands[n] = island;
        queue[n++] = node;
    };
    add(start);
    for (int i = 0; i < n; ++i)
        for_each_on_island(queue[i], [&](void* node) {
            f(node);
            void* component = at<void*>(node, off::node_component);
            auto valve = g_valves.find(component);
            if (valve != g_valves.end() && is_blow_off(valve->second.kind)) {
                add(static_cast<uint8_t*>(component) + off::pump_node_a);
                add(static_cast<uint8_t*>(component) + off::pump_node_b);
            }
            if (intercooler_of(node)) for_each_sharing_content(vehicle, node, add);
        });
}
// A boosting turbo that finds no intercooler logs the parts on its boost pipe (and what each gas port is snapped onto),
// so a player's log shows how the pipe is laid when an intercooler on it is not picked up.
void log_boost_pipe(void* turbo, void* vehicle, Turbo& t, double now) {
    static std::atomic<int> logs{12};
    if (now - t.last_pipe_log < 60.0 || logs.load() <= 0) return;
    t.last_pipe_log = now;
    logs.fetch_sub(1);
    std::vector<std::string> parts;
    int nodes = 0;
    for_each_connected(vehicle, static_cast<uint8_t*>(turbo) + off::pump_node_b, [&](void* node) {
        ++nodes;
        auto* component = at<uint8_t*>(node, off::node_component);
        size_t length = 0;
        const char* id = definition_id_of(component, length);
        std::string name = id ? std::string(id, length) : std::string("?");
        if (id && length >= 8 && std::memcmp(id, "gas_port", 8) == 0 && anymaker::readable(component + off::port_mated_component, 8)) {
            size_t on_length = 0;
            const char* on = definition_id_of(at<void*>(component, off::port_mated_component), on_length);
            name += " on " + (on ? std::string(on, on_length) : std::string("nothing"));
        }
        if (parts.size() < 16 && std::find(parts.begin(), parts.end(), name) == parts.end()) parts.push_back(name);
    });
    std::string list;
    for (const std::string& p : parts) list += (list.empty() ? "" : ", ") + p;
    log(0, "Turbo %dx%d id %d: no intercooler on its boost pipe (%d gas nodes: %s)", t.size, t.size, at<int32_t>(turbo, off::component_id), nodes, list.c_str());
}

void tick_valve(void* self, int kind) {
    ValvePart& v = g_valves[self];
    v.kind = kind;
    void* c0 = static_cast<uint8_t*>(self) + off::pump_content_0;
    void* c1 = static_cast<uint8_t*>(self) + off::pump_content_1;
    double dt = g_delta_time && std::isfinite(*g_delta_time) && *g_delta_time > 0 ? *g_delta_time : 1.0 / 60.0;
    double litres = 0;
    if (is_blow_off(kind)) {
        // Inline: the gas passes straight through (both ports are one pipe, see hk_flood). Evening out the two
        // sides as well costs nothing when they are already one pipe, and passes the gas if they are not.
        level_sides(c0, c1);
        litres = v.vented;
        v.vented = 0;
        v.vented_by = nullptr;
    } else {
        double move = wastegate_move(v.state.slots[kValveOpen], pressure_of(c0), pressure_of(c1), at<double>(c0, off::content_volume), at<double>(c1, off::content_volume), dt,
                                     kind == kKindWastegateLarge);
        if (move > 0) g_content_move(c0, c1, &move);
        else if (move < 0) { double back = -move; g_content_move(c1, c0, &back); }
        litres = std::abs(move);
    }
    v.last_tick = now_seconds();
    publish_valve(self, v, litres, dt);
}

// Flow, open and the sound byte the clients play, from the litres that went through this tick.
void publish_valve(void* self, ValvePart& v, double litres, double dt) {
    int kind = v.kind;
    void* c0 = static_cast<uint8_t*>(self) + off::pump_content_0;
    v.state.flow += (litres / dt - v.state.flow) * std::min(1.0, dt / 0.15);
    if (is_blow_off(kind)) v.state.slots[kValveOpen] = blow_off_shows_open(now_seconds() < v.open_until, v.state.flow) ? 1.0 : 0.0;
    v.state.slots[kValveFlow] = v.state.flow;
    v.state.slots[kValvePressure] = pressure_of(c0) * kKpaPerUnit;
    at<double>(self, off::pump_pressure) = 0;
    at<double>(self, off::pump_display_power) = 0;
    at<double>(self, off::pump_flow) = litres;
    int style = v.setting && std::isfinite(v.setting[1]) ? int(std::lround(v.setting[1])) : kWhoosh;
    double sound = is_blow_off(kind) ? encode_blow_off_factor(v.state.slots[kValveOpen] > 0.5, v.state.flow / 60.0, style, v.vent_size)
                                     : encode_blow_off_factor(now_seconds() < v.antilag_until, v.antilag_level, kWhoosh, v.antilag_size);   // antilag bangs
    g_property_set(static_cast<uint8_t*>(self) + off::pump_factor_speed, &sound);
}

// The game stops ticking a gas part whose pipes have settled: off throttle a blow-off valve went to sleep still
// showing open and its sound played until the next throttle input woke it. The turbos keep ticking, so they
// bring a sleeping valve's flow, open state and sound up to date (and hand it anything vented through it).
void publish_sleeping_valves(double dt) {
    double now = now_seconds();
    static double last = 0;                           // every turbo calls this: once per server tick is enough
    if (now - last < dt * 0.5) return;
    last = now;
    for (auto& [self, v] : g_valves) {
        if (now - v.last_tick < 0.1) continue;
        double litres = 0;
        if (is_blow_off(v.kind)) { litres = v.vented; v.vented = 0; v.vented_by = nullptr; }
        publish_valve(const_cast<void*>(self), v, litres, dt);
    }
}

// "Turbo 3x3" or "Supercharger (large)", for the log.
std::string booster_name(const Turbo& t) {
    if (t.blower) return t.large ? "Supercharger (large)" : "Supercharger (small)";
    return "Turbo " + std::to_string(t.size) + "x" + std::to_string(t.size);
}

// Frontal area of an intercooler in cells: 3 x 2 as placed, plus what it was stretched by.
double cooler_cells(const void* cooler) {
    if (!anymaker::readable(static_cast<const uint8_t*>(cooler) + off::component_extend, 12)) return 6;
    int32_t x = std::clamp(at<int32_t>(cooler, off::component_extend), 0, 30), y = std::clamp(at<int32_t>(cooler, off::component_extend + 4), 0, 30);
    return double(3 + x) * double(2 + y);
}

// What is piped to a turbo or supercharger, looked at twice a second (pipes change rarely, and the walk can cross every
// gas node of the vehicle): the turbo or supercharger whose boost its compressor breathes (it is then a second stage),
// the intercoolers on its boost pipe, which it paints with their starting colours the first time it finds them, and
// what that pipe feeds (an engine's air manifold, or the next stage's intake).
void refresh_topology(void* self, void* vehicle, Turbo& t, double now) {
    if (now - t.topology_at < 0.5) return;
    t.topology_at = now;
    const void* upstream = nullptr;
    for_each_connected(vehicle, static_cast<uint8_t*>(self) + off::pump_node_a, [&](void* node) {
        auto* component = at<uint8_t*>(node, off::node_component);
        if (!upstream && component && component != self && node == component + off::pump_node_b && g_turbos.count(component)) upstream = component;
    });
    if (upstream != t.upstream) {
        auto up = upstream ? g_turbos.find(upstream) : g_turbos.end();
        if (up != g_turbos.end())
            log(0, "%s id %d: second stage, breathing the boost of %s id %d", booster_name(t).c_str(), at<int32_t>(self, off::component_id),
                booster_name(up->second).c_str(), at<int32_t>(upstream, off::component_id));
        else
            log(0, "%s id %d: breathes outside air", booster_name(t).c_str(), at<int32_t>(self, off::component_id));
        t.upstream = upstream;
    }
    double cells = 0;
    const void* seen[8]{};
    int coolers = 0;
    const void* engine = nullptr;
    const void* stage = nullptr;
    // The boost pipe's size (every gas content on it, once each) and how many compressors push into it: the turbo model
    // needs both to tell air that only fills the pipe from air the engine takes (turbo_logic.h, working_flow).
    const void* contents[64]{};
    int content_count = 0, feeders = 0;
    double capacity = 0;
    for_each_connected(vehicle, static_cast<uint8_t*>(self) + off::pump_node_b, [&](void* node) {
        auto* component = at<uint8_t*>(node, off::node_component);
        if (component && node == component + off::pump_node_b && g_turbos.count(component)) ++feeders;
        if (anymaker::readable(static_cast<uint8_t*>(node), off::node_content + 8)) {
            auto* content = at<uint8_t*>(node, off::node_content);
            if (content && content_count < 64 && std::find(contents, contents + content_count, content) == contents + content_count &&
                anymaker::readable(content, off::content_capacity + 8)) {
                contents[content_count++] = content;
                double c = at<double>(content, off::content_capacity);
                if (std::isfinite(c) && c > 0 && c < 1e6) capacity += c;
            }
        }
        if (!engine && component) {
            auto m = g_manifolds.find(component);
            if (m != g_manifolds.end() && now - m->second.at < 2.0) engine = m->second.engine;
        }
        if (!stage && component && component != self && node == component + off::pump_node_a && g_turbos.count(component)) stage = component;
        void* cooler = intercooler_of(node);
        if (!cooler || coolers == 8) return;
        for (int k = 0; k < coolers; ++k) if (seen[k] == cooler) return;   // one per intercooler, however many ports it has
        seen[coolers++] = cooler;
        cells += cooler_cells(cooler);
        if (first_sight(cooler)) default_paint(cooler, kCoolerColours.slot[0], kCoolerColours.slot[1]);
    });
    t.cooler_cells = cells;
    t.coolers = coolers;
    t.boost_capacity = capacity;
    t.feeders = std::max(1, feeders);
    if (engine != t.feeds_engine || stage != t.feeds_stage) {
        static std::atomic<int> logs{40};
        auto next = stage ? g_turbos.find(stage) : g_turbos.end();
        if (logs.fetch_sub(1) > 0)
            log(0, "%s id %d: boost feeds %s%s%s", booster_name(t).c_str(), at<int32_t>(self, off::component_id), engine ? "an engine" : "",
                engine && next != g_turbos.end() ? " and " : "", next != g_turbos.end() ? (booster_name(next->second) + "'s intake").c_str() : engine ? "" : "no engine (yet)");
        t.feeds_engine = engine;
        t.feeds_stage = stage;
    }
}

// The supercharger's shaft: torque node 0, made for its "torque" surface.
void* blower_shaft(void* self) {
    auto* array = static_cast<uint8_t*>(self) + off::pump_torque;
    if (!anymaker::readable(array, 16)) return nullptr;
    auto* buffer = at<uint8_t*>(array, 0);
    int32_t count = at<int32_t>(array, 8), size = at<int32_t>(array, 12);
    if (!buffer || count < 1 || count > 8 || size != off::torque_node_size || !anymaker::readable(buffer, size_t(size))) return nullptr;
    if (at<void*>(buffer, off::torque_node_component) != self) {
        static std::atomic<int> logs{3};
        if (logs.fetch_sub(1) > 0) log(1, "a supercharger's torque node names component %p, not itself (%p)", at<void*>(buffer, off::torque_node_component), self);
    }
    return buffer;
}

// A supercharger tick, in place of the impeller's: its head follows its pulley, it pushes air from intake to outlet
// (heated, then cooled by any intercoolers on its boost pipe), and the work that takes comes off its shaft, which
// slows the belt and the engine driving it.
void tick_blower(void* self, void* vehicle, Turbo& t) {
    void* c0 = static_cast<uint8_t*>(self) + off::pump_content_0;
    void* c1 = static_cast<uint8_t*>(self) + off::pump_content_1;
    double now_s = now_seconds();
    t.vehicle = vehicle; t.ticked_at = now_s;
    refresh_topology(self, vehicle, t, now_s);
    auto upstream = t.upstream ? g_turbos.find(t.upstream) : g_turbos.end();
    bool fed = upstream != g_turbos.end();            // a turbo feeds its intake: it is the second stage
    if (!fed) hold_ambient(c0);                       // open to the air, as a turbo's intake (hk_tick)
    Inputs in;
    void* contents[2] = {c0, c1};
    for (int i = 0; i < 2; ++i) {
        in.pressure[i] = pressure_of(contents[i]);
        in.volume[i] = at<double>(contents[i], off::content_volume);
    }
    in.dt = g_delta_time && std::isfinite(*g_delta_time) && *g_delta_time > 0 ? *g_delta_time : 1.0 / 60.0;
    double inlet_c = fed ? at<double>(c0, off::content_temperature) : kAmbientC;
    in.intake_temperature = inlet_c;
    in.boost_capacity = t.boost_capacity;
    in.feeders = t.feeders;
    void* shaft = g_get_rps && g_apply_torque && g_newtons_per_joule ? blower_shaft(self) : nullptr;
    double rps = 0;
    if (shaft) g_get_rps(&rps, shaft);
    if (!std::isfinite(rps)) rps = 0;
    // An air filter on its intake: a dirty one restricts it, and it clogs with use (the supercharger itself does not wear).
    sync_wear(now_s);
    void* filter = nullptr;
    if (!fed)
        for_each_on_island(static_cast<uint8_t*>(self) + off::pump_node_a, [&](void* node) {
            void* component = at<void*>(node, off::node_component);
            if (!filter && component && is_air_filter(part_kind(component))) filter = component;
        });
    t.filter = filter;
    bool filtered = fed ? upstream->second.filter != nullptr : filter != nullptr;
    double* filter_condition = filter ? &wear_of('f', vehicle, filter) : nullptr;
    if (filter && first_sight(filter)) default_paint(filter, kFilterColours.slot[0], kFilterColours.slot[1]);
    t.state.health = filter_head_factor(filter != nullptr, filter_condition ? *filter_condition : 1.0);
    t.state.blown = false;
    in.bov_venting = vent_blow_off_valves(self, t.size, (in.pressure[kColdOut] - in.pressure[kColdIn]) - head_for(t.state, in.pressure[kColdIn]), in.dt, in.bov_spring_kpa);
    BlowerStep o = blower_step(t.state, t.spec, in, rps);
    publish_sleeping_valves(in.dt);
    if (o.compressor_move > 0) g_content_move(c0, c1, &o.compressor_move);
    else if (o.compressor_move < 0) { double v = -o.compressor_move; g_content_move(c1, c0, &v); }
    // The belt pays for the air: as the stock impeller does, a drag torque on its shaft (energy x newtons per joule).
    if (shaft && o.drive_joules > 0 && rps != 0) {
        double torque = -o.drive_joules * *g_newtons_per_joule * (rps > 0 ? 1.0 : -1.0);
        if (std::isfinite(torque)) g_apply_torque(shaft, &torque);
    }
    double cells = t.cooler_cells;
    double cooling = intercooler_effectiveness(cells, t.state.intake_flow);
    double air_c = boost_air_c(inlet_c, o.pressure_ratio, o.efficiency, cooling, kAmbientC);
    if (o.compressor_move > 0) at<double>(c1, off::content_temperature) = air_c;
    if (filter_condition && t.state.spool > 0) {
        *filter_condition = std::max(0.0, *filter_condition - filter_clog(t.state.spool, in.dt));
        g_wear_dirty = true;
    }
    t.state.slots[kOilFlow] = 0;
    t.state.slots[kTurboTemperature] = boost_air_c(inlet_c, o.pressure_ratio, o.efficiency, 0.0, kAmbientC);   // its housing: the air leaving it
    t.state.slots[kAirFiltered] = filtered ? 1.0 : 0.0;
    t.state.slots[kBoostAirTemperature] = air_c;
    t.state.slots[kPowerMultiplier] = boost_factor_for_air(t.state.slots[kPowerMultiplier], air_c);
    t.drive_kw += (t.state.slots[kDrivePower] - t.drive_kw) * std::min(1.0, in.dt / 0.5);
    at<double>(self, off::pump_pressure) = o.head;
    at<double>(self, off::pump_display_power) = o.drive_joules / in.dt;
    at<double>(self, off::pump_flow) = o.compressor_move;
    t.boost = std::max(0.0, in.pressure[kColdOut] - in.pressure[kColdIn]);
    double sound = encode_turbo_factor(blower_sound_level(rps), t.state.surging);   // the clients' whine (turbo_sound.h)
    g_property_set(static_cast<uint8_t*>(self) + off::pump_factor_speed, &sound);
    static std::atomic<int> logs{200};
    if (!shaft) {
        static std::atomic<int> missing{2};
        if (missing.fetch_sub(1) > 0) log(1, "supercharger shaft unavailable (rps %d, torque %d, newtons per joule %d, node %d): no boost",
                                          !!g_get_rps, !!g_apply_torque, !!g_newtons_per_joule, !!blower_shaft(self));
    } else if ((t.ticks++ == 0 || (std::abs(rps) > 1 && now_s - t.last_log >= 5.0)) && logs.fetch_sub(1) > 0) {
        t.last_log = now_s;
        log(0, "%s id %d tick %d: pulley %.0f rpm (%.0f%% of full boost), head %.2f, intake %.2f -> boost %.2f, %.1f L/s, belt drag %.2f kW, boost air %.0f C (%.0f cooler cells)%s%s",
            booster_name(t).c_str(), at<int32_t>(self, off::component_id), t.ticks, rps * 60, t.state.spool * 100, o.head, in.pressure[kColdIn], in.pressure[kColdOut],
            t.state.intake_flow, t.drive_kw, air_c, cells, t.state.surging ? ", SURGE" : "", in.bov_venting ? ", blow-off venting" : "");
    }
}

void hk_tick(void* self, void* vehicle, void* scene) {
    if (!self) return;
    std::unique_lock<std::recursive_mutex> lock(g_mutex);
    int kind = part_kind(self);
    if (is_valve(kind)) { tick_valve(self, kind); return; }
    if (is_blower(kind)) {
        Turbo& t = g_turbos[self];                    // it has nothing to build, so one created before the hooks works too
        if (!t.blower) { t.blower = true; t.large = kind == kKindSuperchargerLarge; t.spec = blower_spec(t.large); t.size = t.spec.size; }
        tick_blower(self, vehicle, t);
        return;
    }
    if (!turbo_size(self)) { lock.unlock(); reinterpret_cast<tick_t>(h_tick.original)(self, vehicle, scene); return; }
    auto found = g_turbos.find(self);
    if (found == g_turbos.end()) {                  // placed before the hooks were in: no hot side, no boost
        static std::atomic<int> logs{4};
        if (logs.fetch_sub(1) > 0) log(1, "a turbo without a hot side is ticking (it existed before Turbo was ready); rebuild or reload it");
        return;
    }
    Turbo& t = found->second;
    void* c0 = static_cast<uint8_t*>(self) + off::pump_content_0;
    void* c1 = static_cast<uint8_t*>(self) + off::pump_content_1;
    double now_s = now_seconds();
    t.vehicle = vehicle; t.ticked_at = now_s;
    refresh_topology(self, vehicle, t, now_s);
    auto upstream = t.upstream ? g_turbos.find(t.upstream) : g_turbos.end();
    bool fed = upstream != g_turbos.end();           // a compound's second stage: it breathes another turbo's boost
    // The compressor inlet is open to the air, like a real turbo's intake: hold it at ambient every tick, both ways.
    // A game vent on the inlet starved the compressor in the first test; and boost left in the
    // pipes when the engine stops flows back through the compressor and out here, instead of staying trapped.
    // A pipe on the inlet still works. A second stage's inlet is the first stage's boost pipe, so it is left alone.
    if (!fed) hold_ambient(c0);
    void* contents[kNodeCount] = {c0, c1, t.hot ? t.hot->content[0] : nullptr, t.hot ? t.hot->content[1] : nullptr};
    Inputs in;
    for (int i = 0; i < kNodeCount; ++i) {
        in.pressure[i] = contents[i] ? pressure_of(contents[i]) : 1.0;
        in.volume[i] = contents[i] ? at<double>(contents[i], off::content_volume) : 0.0;
    }
    if (contents[kHotIn]) in.exhaust_temperature = at<double>(contents[kHotIn], off::content_temperature);
    in.dt = g_delta_time && std::isfinite(*g_delta_time) && *g_delta_time > 0 ? *g_delta_time : 1.0 / 60.0;
    double inlet_c = fed ? at<double>(c0, off::content_temperature) : kAmbientC;
    in.intake_temperature = inlet_c;
    in.boost_capacity = t.boost_capacity;
    in.feeders = t.feeders;
    // Upkeep, from what the last ticks left: its condition and any air filter on the compressor intake. A second
    // stage breathes air its first stage filtered (or not): that one's filter does the work, and wears.
    sync_wear(now_s);
    double& condition = wear_of('t', vehicle, self);
    void* filter = nullptr;
    if (!fed)
        for_each_on_island(static_cast<uint8_t*>(self) + off::pump_node_a, [&](void* node) {
            void* component = at<void*>(node, off::node_component);
            if (!filter && component && is_air_filter(part_kind(component))) filter = component;
        });
    t.filter = filter;
    bool filtered = fed ? upstream->second.filter != nullptr : filter != nullptr;
    double* filter_condition = filter ? &wear_of('f', vehicle, filter) : nullptr;
    if (filter && first_sight(filter)) default_paint(filter, kFilterColours.slot[0], kFilterColours.slot[1]);
    t.state.health = condition_head_factor(condition) * filter_head_factor(filter != nullptr, filter_condition ? *filter_condition : 1.0);
    t.state.blown = turbo_blown(condition);           // worn out: no boost, a choked intake (limp mode)
    ValvePart* antilag = antilag_wastegate(t);
    in.antilag = antilag != nullptr;
    // Two-step: hand the engine it boosts the launch RPM a wastegate asks for; the engine tick does the limiting, and
    // while it cuts, its fuel reaches the exhaust for antilag to light.
    if (const void* engine = boosted_engine(t)) {
        double launch_rpm = two_step_rpm(t);
        auto l = g_launch.find(engine);
        if (launch_rpm > 0) {
            if (l == g_launch.end()) {
                l = g_launch.emplace(engine, Launch{}).first;
                static std::atomic<int> logs{40};
                if (logs.fetch_sub(1) > 0) log(0, "two-step on: the Turbo %dx%d's engine holds at %.0f RPM", t.size, t.size, launch_rpm);
            }
            l->second.rpm = launch_rpm; l->second.at = now_s;
        }
        in.two_step = l != g_launch.end() && now_s - l->second.cut_at < 0.3;
    }
    in.bov_venting = vent_blow_off_valves(self, t.size, (in.pressure[kColdOut] - in.pressure[kColdIn]) - head_for(t.state, in.pressure[kColdIn]), in.dt, in.bov_spring_kpa);
    Step o = step(t.state, t.spec, in);
    if (antilag && t.state.antilag_firing) {          // the wastegate plays the bangs
        antilag->antilag_until = now_seconds() + 0.15;
        antilag->antilag_level = clamp01(1.0 - t.state.exhaust_flow / t.spec.rated_flow);
        antilag->antilag_size = t.size;
    }
    publish_sleeping_valves(in.dt);
    if (t.hot) {
        if (o.turbine_move > 0) g_content_move(contents[kHotIn], contents[kHotOut], &o.turbine_move);
        else if (o.turbine_move < 0) { double v = -o.turbine_move; g_content_move(contents[kHotOut], contents[kHotIn], &v); }
    }
    if (o.compressor_move > 0) g_content_move(c0, c1, &o.compressor_move);
    else if (o.compressor_move < 0) { double v = -o.compressor_move; g_content_move(c1, c0, &v); }
    // Compressing the air heats it (on top of a first stage's heat, for a second stage); intercoolers on the boost
    // pipe take that heat back out. The engine breathes whatever reaches its air manifold (hk_engine_tick).
    double cells = t.cooler_cells;
    if (t.coolers == 0 && t.state.spool > 0.5) log_boost_pipe(self, vehicle, t, now_s);
    double cooling = intercooler_effectiveness(cells, t.state.intake_flow);
    double air_c = boost_air_c(inlet_c, o.pressure_ratio, o.efficiency, cooling, kAmbientC);
    if (o.compressor_move > 0) at<double>(c1, off::content_temperature) = air_c;
    // Oil: what comes in the oil port passes through (as engine oil pressure would push it) and leaves hotter.
    constexpr double kOilPassLps = 0.8;
    double oil_c = kAmbientC, pollution = 0, oil_now = 0, moved = 0;
    if (t.oil && t.oil_ports) {
        void* oil_in = t.oil->content[0];
        void* oil_out = t.oil->content[1];
        double still = 0;
        g_liquid_tick(oil_in, vehicle, &still);
        g_liquid_tick(oil_out, vehicle, &still);
        double oil = 0, pollutant = 0, volume = at<double>(oil_in, off::content_volume);
        if (oil_amounts(oil_in, oil, pollutant) && volume > 1e-9 && std::isfinite(volume)) {
            double share = std::clamp((oil + pollutant) / volume, 0.0, 1.0);   // water or fuel in the line does not count
            pollution = oil + pollutant > 0 ? pollutant / (oil + pollutant) : 0.0;
            oil_c = at<double>(oil_in, off::content_temperature);
            double want = std::min(kOilPassLps * in.dt, volume);
            if (want > 1e-9) g_liquid_move(&moved, oil_in, oil_out, &want);
            if (!std::isfinite(moved) || moved < 0) moved = 0;
            oil_now = moved * share / in.dt;
        }
    } else {
        oil_now = kOilFullFlow;                       // no oil ports (none in this build, or the game would not make them: logged)
    }
    t.oil_lps += (oil_now - t.oil_lps) * std::min(1.0, in.dt / 0.5);
    UpkeepInputs ui;
    ui.spool = t.state.spool; ui.exhaust_fraction = t.state.exhaust_flow / t.spec.rated_flow; ui.exhaust_c = in.exhaust_temperature;
    ui.oil_lps = t.oil_lps; ui.oil_c = oil_c; ui.oil_pollution = pollution; ui.filter = filtered;
    ui.filter_condition = filter_condition ? *filter_condition : 1.0; ui.ambient_c = kAmbientC; ui.dt = in.dt;
    double was = condition;
    t.up.condition = condition;
    UpkeepStep us = upkeep(t.up, ui);
    condition = t.up.condition;
    if (filter_condition && us.filter_wear > 0) *filter_condition = std::max(0.0, *filter_condition - us.filter_wear);
    if (condition != was || us.filter_wear > 0) g_wear_dirty = true;
    if (!turbo_blown(was) && turbo_blown(condition)) {   // it just wore out: it blows (the clients hear it go, turbo_sound.h)
        t.state.blown = true;
        set_paint(self, kPaletteSoot, kPaletteSoot, false);
        log(1, "Turbo %dx%d id %d BLEW UP (worn out at %.0f C, oil %.2f L/s, %s): no boost and a choked intake until it is replaced",
            t.size, t.size, at<int32_t>(self, off::component_id), t.up.temperature, t.oil_lps, filtered ? "filtered" : "no air filter");
    }
    if (t.oil && moved > 0) {                         // the oil leaving carries the turbo's heat, and it ages a little
        void* oil_out = t.oil->content[1];
        double held = std::max(at<double>(oil_out, off::content_volume), moved);
        double& oil_t = at<double>(oil_out, off::content_temperature);
        if (std::isfinite(oil_t)) oil_t += (us.oil_out_c - oil_t) * std::min(1.0, moved / held);
        double oil = 0, pollutant = 0, aged = 1.2e-5 * t.state.spool * in.dt;
        if (aged > 0 && oil_amounts(oil_out, oil, pollutant) && oil > aged) {
            int32_t type = off::kLiquidOil; double amount = -aged;
            g_liquid_modify(oil_out, &type, &amount);
            type = off::kLiquidPollutant; amount = aged;
            g_liquid_modify(oil_out, &type, &amount);
        }
    }
    t.state.slots[kOilFlow] = t.oil_lps;
    t.state.slots[kTurboTemperature] = t.up.temperature;
    t.state.slots[kCondition] = condition;
    t.state.slots[kAirFiltered] = filtered ? 1.0 : 0.0;
    t.state.slots[kBoostAirTemperature] = air_c;
    t.state.slots[kPowerMultiplier] = boost_factor_for_air(t.state.slots[kPowerMultiplier], air_c);   // as the engine gets it (hk_remap)
    static std::atomic<int> upkeep_logs{120};
    if (t.state.spool > 0.05 && now_s - t.last_upkeep_log >= 10.0 && upkeep_logs.fetch_sub(1) > 0) {
        t.last_upkeep_log = now_s;
        log(0, "Turbo %dx%d id %d upkeep: %.0f C, condition %.4f, oil %.2f L/s at %.0f C (pollutant %.0f%%), filter %s, boost air %.0f C (%.0f cooler cells, cooling %.2f), health %.2f",
            t.size, t.size, at<int32_t>(self, off::component_id), t.up.temperature, condition, t.oil_lps, oil_c, pollution * 100,
            filter ? (std::to_string(int(std::lround(ui.filter_condition * 100))) + "%").c_str() : fed ? "first stage's" : "none", air_c, cells, cooling, t.state.health);
    }
    at<double>(self, off::pump_pressure) = o.head;
    at<double>(self, off::pump_display_power) = o.power;
    at<double>(self, off::pump_flow) = o.compressor_move;
    t.boost = std::max(0.0, in.pressure[kColdOut] - in.pressure[kColdIn]);
    double sound = encode_turbo_factor(std::min(1.0, t.state.speed), t.state.surging, t.state.blown);   // the clients' turbo sound: its shaft speed (turbo_sound.h)
    g_property_set(static_cast<uint8_t*>(self) + off::pump_factor_speed, &sound);
    static std::atomic<int> event_logs{60};
    if (t.state.surging != t.was_surging || in.bov_venting != t.was_venting) {
        if ((t.state.surging || in.bov_venting) && event_logs.fetch_sub(1) > 0)
            log(0, "Turbo %dx%d id %d: %s (spool %.2f, boost %.2f over a head of %.2f)", t.size, t.size, at<int32_t>(self, off::component_id),
                t.state.surging ? "compressor surge (no blow-off valve venting)" : "blow-off valve venting", t.state.spool, in.pressure[kColdOut] - in.pressure[kColdIn], o.head);
        t.was_surging = t.state.surging; t.was_venting = in.bov_venting;
    }
    // Diagnostics for tuning: the first tick, then every 5 s per turbo while gas moves or any port is off ambient,
    // up to 300 lines a session.
    static std::atomic<int> logs{300};
    double now = now_seconds();
    bool off_ambient = false;
    for (int i = 0; i < kNodeCount; ++i) off_ambient = off_ambient || std::abs(in.pressure[i] - 1.0) > 0.05;
    if ((t.ticks++ == 0 || ((t.state.exhaust_flow > 0.05 || t.state.spool > 0.01 || off_ambient) && now - t.last_log >= 5.0)) && logs.fetch_sub(1) > 0) {
        t.last_log = now;
        log(0, "Turbo %dx%d id %d tick %d: hot in %.2f (%.2f L, %.0f C) -> hot out %.2f (%.2f L), exhaust %.1f L/s, shaft %.0f rpm (spool %.2f), turbine %.1f kW, compressor %.1f kW, wastegate %.2f, head %.2f, cold in %.2f (%.2f L) -> cold out %.2f (%.2f L, pipe %.1f L, %d feeding), intake %.1f L/s (power x%.2f)%s%s, dt %.4f",
            t.size, t.size, at<int32_t>(self, off::component_id), t.ticks, in.pressure[kHotIn], in.volume[kHotIn], in.exhaust_temperature, in.pressure[kHotOut], in.volume[kHotOut],
            t.state.exhaust_flow, t.state.slots[kRpm], t.state.spool, o.turbine_power / 1000.0, o.power / 1000.0, t.state.bypass, o.head, in.pressure[kColdIn], in.volume[kColdIn],
            in.pressure[kColdOut], in.volume[kColdOut], t.boost_capacity, t.feeders, t.state.intake_flow,
            power_multiplier(in.pressure[kColdOut]), t.state.surging ? ", SURGE" : "", in.bov_venting ? ", blow-off venting" : "", in.dt);
    }
}

void hk_get_f64(double** ret, void* self, const anymaker::gc_string_view* name) {
    reinterpret_cast<get_f64_t>(h_get_f64.original)(ret, self, name);
    if (!ret || !self || !name) return;
    auto view = name->view();
    {
        std::lock_guard<std::recursive_mutex> lock(g_mutex);
        auto valve = g_valves.find(self);
        if (valve != g_valves.end()) {                // valves answer their own channels, even names the impeller knows
            int c = valve_channel_index(view.data(), view.size());
            if (c >= 0) *ret = &valve->second.state.slots[c];
            return;
        }

    }
    if (*ret) return;
    int i = channel_index(view.data(), view.size());
    if (i < 0) return;
    std::lock_guard<std::recursive_mutex> lock(g_mutex);
    auto found = g_turbos.find(self);
    if (found == g_turbos.end()) return;
    *ret = &found->second.state.slots[i];
    static std::atomic<int> logs{8};
    if (logs.fetch_sub(1) > 0) log(0, "data channel %s connected", kChannels[i].name);
}

// ------------------------------------------------------------------------------------- tooltip
// The stock impeller tooltip shows flow, its shaft's speed (always 0 on a turbo: nothing drives the shaft) and power.
// A turbo shows its own: intake flow, turbine speed from the spool, and boost, formatted by the game's own units
// (so boost reads in kPa or psi like every other pressure in the game). A supercharger the same, with its impeller's
// speed.
using build_tooltip_t = void (*)(void* self, void* data);
using units_t = void (*)(anymaker::gc_string_view* ret, const double* value);
using to_pascals_t = void (*)(double* ret, const double* value);
using add_entry_t = void (*)(void* data, const int32_t* label, const anymaker::gc_string_view* value, const int32_t* icon, const int32_t* color);
anymaker::cell_hook h_tooltip;
build_tooltip_t g_component_tooltip;
units_t g_units_flow, g_units_frequency, g_units_pressure, g_units_temperature, g_units_percentage;
to_pascals_t g_to_pascals;
self_fn_t g_string_dtor;
constexpr int kAddTooltipEntrySlot = 68;             // server_scene.peer.inspection_data.vehicle method table
enum : int32_t { kLocCondition = 205, kLocTemperature = 214, kLocSpeed = 199, kLocFlow = 209, kLocPressure = 217, kLocBoost = 236, kLocStiffness = 354, kLocSoundEffect = 327 };   // e_localization_string
enum : int32_t { kIconCondition = 6, kIconFlow = 16, kIconPressure = 30, kIconSpeed = 33, kIconTemperature = 37 };         // inspection_util.e_tooltip_icon
enum : int32_t { kColorDefault = 0, kColorMissing = 1 };   // inspection_util.e_tooltip_color (missing: the stock "no air filter" row)

// The value text comes from the game's own formatter, which builds a game string the tooltip can keep. Text made with
// $string_ctor_cstr is only a view of the caller's buffer (it stores the pointer, copies nothing), so a row made from
// a stack buffer showed an empty Condition: the buffer was gone by the time the tooltip was sent.
void add_entry(void* data, add_entry_t add, int32_t label, int32_t icon, units_t format, double value, int32_t color = kColorDefault) {
    anymaker::gc_string_view text{};
    g_string_ctor(&text, "");                        // a valid empty string, as the stock builder's $string_ctor gives
    format(&text, &value);
    add(data, &label, &text, &icon, &color);
    g_string_dtor(&text);
}

void hk_tooltip(void* self, void* data) {
    double flow = 0, rps = 0, boost = 0, condition = 1, temperature_c = kAmbientC;
    bool turbo = false, valve = false, blower = false;
    {
        std::lock_guard<std::recursive_mutex> lock(g_mutex);
        auto v = g_valves.find(self);
        if (v != g_valves.end()) {
            valve = true;
            flow = v->second.state.flow;
            boost = v->second.state.slots[kValvePressure] / kKpaPerUnit;    // the pipe's pressure, game units
        }
        auto found = g_turbos.find(self);
        if (found != g_turbos.end()) {
            turbo = true;
            blower = found->second.blower;
            flow = found->second.state.intake_flow;
            rps = found->second.state.slots[kRpm] / 60.0;   // shaft (or impeller) speed
            boost = found->second.boost;
            condition = found->second.up.condition;
            temperature_c = found->second.up.temperature;
        }
    }
    auto add = (turbo || valve) && data ? reinterpret_cast<add_entry_t>(container_method(data, kAddTooltipEntrySlot)) : nullptr;
    if (!add) { reinterpret_cast<build_tooltip_t>(h_tooltip.original)(self, data); return; }
    g_component_tooltip(self, data);
    double pascals = 0;
    g_to_pascals(&pascals, &boost);
    if (valve) {
        add_entry(data, add, kLocFlow, kIconFlow, g_units_flow, flow);
        add_entry(data, add, kLocPressure, kIconPressure, g_units_pressure, pascals);
        return;
    }
    add_entry(data, add, kLocFlow, kIconFlow, g_units_flow, flow);
    add_entry(data, add, kLocSpeed, kIconSpeed, g_units_frequency, rps);
    add_entry(data, add, kLocBoost, kIconPressure, g_units_pressure, pascals);
    if (blower) return;                               // no wear or heat of its own
    // Upkeep: how worn the turbo is (100% new, 0% blown) and how hot it runs, as the stock air and oil filters show their
    // condition (units.percentage rounds to whole percent). A worn turbo reads at least 1% until it blows.
    if (g_units_percentage) {
        double c = std::clamp(std::isfinite(condition) ? condition : 1.0, 0.0, 1.0);
        bool blown = !(c > 0);
        add_entry(data, add, kLocCondition, kIconCondition, g_units_percentage, blown ? 0.0 : std::max(c, 0.01), blown ? kColorMissing : kColorDefault);
    }
    if (g_units_temperature && std::isfinite(temperature_c)) add_entry(data, add, kLocTemperature, kIconTemperature, g_units_temperature, temperature_c);
}

// ------------------------------------------------------------------------------------- Properties Tool
// The blow-off valve adds its spring and sound to the impeller's own rows (just the name): "Stiffness", 0-50 kPa
// of boost over what the turbo holds before it opens, and "Sound effect", 0-3. Neither is saved (see below).
using get_properties_t = void (*)(void* self, void* properties);
using add_f64_range_t = void (*)(void* properties, const int32_t* label, double* const* value, const double* min, const double* max, const double* step);
constexpr int kAddPropertyF64RangeSlot = 10;          // server_scene.peer.property_data method table
anymaker::cell_hook h_properties;

void hk_properties(void* self, void* properties) {
    reinterpret_cast<get_properties_t>(h_properties.original)(self, properties);
    double* setting = nullptr;
    {
        std::lock_guard<std::recursive_mutex> lock(g_mutex);
        auto v = g_valves.find(self);
        if (v != g_valves.end() && is_blow_off(v->second.kind)) setting = v->second.setting;
    }
    if (!setting || !properties) return;
    auto add = reinterpret_cast<add_f64_range_t>(container_method(properties, kAddPropertyF64RangeSlot));
    if (!add) return;
    int32_t label = kLocStiffness;
    double min = 0, max = kBlowOffMaxKpa, step = 1;
    add(properties, &label, &setting, &min, &max, &step);
    double* sound = setting + 1;                      // 0 whoosh, 1 flutter, 2 chirp, 3 dump (turbo_sound.h)
    label = kLocSoundEffect; max = kBlowOffSoundCount - 1;
    add(properties, &label, &sound, &min, &max, &step);
    static std::atomic<int> logs{4};
    if (logs.fetch_sub(1) > 0) log(0, "blow-off valve spring and sound rows added (%.0f kPa, sound %.0f)", setting[0], setting[1]);
}

// Not saved: the save data is binary and read by position, so values added after the impeller's own shift every
// field after them and broke the tile save in a 0.1.25 test (a crash in the creature_location vector). The valve's
// spring and sound reset to the defaults when the vehicle loads.

// ------------------------------------------------------------------------------------- part definitions
// GamePath = file.path { e_store m_store; string m_path; } (24 bytes)
struct GamePath { int32_t store; uint32_t pad; anymaker::gc_string_view path; };
static_assert(sizeof(GamePath) == 24, "file.path");
using add_definitions_t = void (*)(void* container, const void* file);
using file_load_t = void (*)(bool* ret, void* file, const void* path);
anymaker::cell_hook h_add_definitions;
self_fn_t g_file_ctor, g_file_dtor;
file_load_t g_file_load;
std::string g_json_path, g_json_forward;            // absolute, backslashes / slashes; set before g_ready, then fixed

std::vector<uint8_t> read_file(const std::wstring& path) {
    std::vector<uint8_t> data;
    HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return data;
    LARGE_INTEGER size{};
    if (GetFileSizeEx(h, &size) && size.QuadPart > 0 && size.QuadPart < (1 << 24)) {
        data.resize(size_t(size.QuadPart));
        DWORD got = 0;
        if (!ReadFile(h, data.data(), DWORD(data.size()), &got, nullptr) || got != data.size()) data.clear();
    }
    CloseHandle(h);
    return data;
}
bool write_file(const std::wstring& path, const void* data, size_t size) {
    HANDLE h = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    DWORD written = 0;
    bool ok = WriteFile(h, data, DWORD(size), &written, nullptr) && written == size;
    CloseHandle(h);
    return ok;
}

// Writes the turbo, valve, air filter, intercooler and supercharger meshes into rom/meshes/components (where the game
// loads part meshes) and the definitions file into "AnyAPI and Modding/Turbo".
bool write_definition_file() {
    HMODULE self = nullptr;
    wchar_t dll[MAX_PATH]{};
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCWSTR)&write_definition_file, &self) ||
        !GetModuleFileNameW(self, dll, MAX_PATH)) return false;
    std::wstring dir(dll);
    dir = dir.substr(0, dir.find_last_of(L"\\/"));      // ...\AnyAPI and Modding\mods
    dir = dir.substr(0, dir.find_last_of(L"\\/"));      // ...\AnyAPI and Modding
    std::wstring game_dir = dir.substr(0, dir.find_last_of(L"\\/"));
    std::wstring components = game_dir + L"\\rom\\meshes\\components\\";
    std::vector<uint8_t> stock = read_file(components + L"turbo_a.mesh");
    bool built[kMaxSize - kMinSize + 1]{};
    for (int n : kTurboSizes) {
        std::string id = definition_id(n);
        std::vector<uint8_t> mesh = build_turbo_mesh(stock, n, id.c_str());
        std::wstring file = components + L"turbo_" + std::wstring(id.begin(), id.end()) + L".mesh";
        built[n - kMinSize] = !mesh.empty() && write_file(file, mesh.data(), mesh.size());
    }
    if (!built[3 - kMinSize] || !built[6 - kMinSize]) log(1, "impeller mesh not recognised or not writable (%zu bytes); some turbos use the stock impeller look", stock.size());
    bool bov_built[2]{};
    for (int large = 0; large < 2; ++large) {
        std::string path = blow_off_mesh_path(large != 0);              // meshes/components/turbo_<id>.mesh
        std::string name = path.substr(path.find_last_of('/') + 1);
        name = name.substr(0, name.size() - 5);
        std::vector<uint8_t> mesh = build_blow_off_mesh(stock, large != 0, name.c_str(), kBlowOffStyle);
        std::wstring file = components + std::wstring(name.begin(), name.end()) + L".mesh";
        bov_built[large] = !mesh.empty() && write_file(file, mesh.data(), mesh.size());
    }
    if (!bov_built[0] || !bov_built[1]) log(1, "blow-off valve meshes not written; the valves use the stock impeller look");
    bool wg_built[2]{};
    for (int large = 0; large < 2; ++large) {
        std::string path = wastegate_mesh_path(large != 0);
        std::string name = path.substr(path.find_last_of('/') + 1);
        name = name.substr(0, name.size() - 5);
        std::vector<uint8_t> mesh = build_wastegate_mesh(stock, large != 0, name.c_str(), kWastegateStyle);
        std::wstring file = components + std::wstring(name.begin(), name.end()) + L".mesh";
        wg_built[large] = !mesh.empty() && write_file(file, mesh.data(), mesh.size());
    }
    if (!wg_built[0] || !wg_built[1]) log(1, "wastegate meshes not written; the wastegates use the gas port look");
    bool filter_built[2]{}, cooler_built = true;
    for (int large = 0; large < 2; ++large) {
        std::string path = air_filter_mesh_path(large != 0);
        std::string name = path.substr(path.find_last_of('/') + 1);
        name = name.substr(0, name.size() - 5);
        std::vector<uint8_t> mesh = build_air_filter_mesh(stock, large != 0, name.c_str());
        std::wstring file = components + std::wstring(name.begin(), name.end()) + L".mesh";
        filter_built[large] = !mesh.empty() && write_file(file, mesh.data(), mesh.size());
    }
    for (int k = 0; k < 18; ++k) {                           // nine tiles each, laid out along x and y
        int tx = k % 3, ty = k / 3 % 3;
        bool round = k >= 9;
        std::string path = intercooler_mesh_path(tx, ty, round);
        std::string name = path.substr(path.find_last_of('/') + 1);
        name = name.substr(0, name.size() - 5);
        std::vector<uint8_t> mesh = build_intercooler_tile(stock, tx, ty, name.c_str(), round ? kIntercoolerRoundStyle : kIntercoolerStyle);
        std::wstring file = components + std::wstring(name.begin(), name.end()) + L".mesh";
        cooler_built = cooler_built && !mesh.empty() && write_file(file, mesh.data(), mesh.size());
    }
    if (!filter_built[0] || !filter_built[1] || !cooler_built)
        log(1, "air filter or intercooler meshes not written; they use stock looks (filters %d/%d, intercooler %d)", filter_built[0], filter_built[1], cooler_built);
    bool blower_built[2]{};
    for (int large = 0; large < 2; ++large) {
        std::string path = blower_mesh_path(large != 0);
        std::string name = path.substr(path.find_last_of('/') + 1);
        name = name.substr(0, name.size() - 5);
        std::vector<uint8_t> mesh = build_supercharger_mesh(stock, large != 0, name.c_str());
        std::wstring file = components + std::wstring(name.begin(), name.end()) + L".mesh";
        blower_built[large] = !mesh.empty() && write_file(file, mesh.data(), mesh.size());
    }
    if (!blower_built[0] || !blower_built[1]) log(1, "supercharger meshes not written; they use the stock impeller look");
    dir += L"\\Turbo";
    CreateDirectoryW(dir.c_str(), nullptr);
    std::wstring file = dir + L"\\turbochargers.json";
    std::string json = definitions_json(built, bov_built, wg_built, filter_built, &cooler_built, blower_built);
    bool ok = write_file(file, json.data(), json.size());
    int n = WideCharToMultiByte(CP_UTF8, 0, file.c_str(), -1, nullptr, 0, nullptr, nullptr);
    g_json_path.assign(size_t(n > 0 ? n - 1 : 0), '\0');
    if (n > 1) WideCharToMultiByte(CP_UTF8, 0, file.c_str(), -1, g_json_path.data(), n, nullptr, nullptr);
    g_json_forward = g_json_path;
    for (char& c : g_json_forward) if (c == '\\') c = '/';
    return ok && !g_json_path.empty();
}

bool already_added(void* container) {
    if (!g_get_definition || !g_string_ctor) return false;
    static const std::string id = definition_id(kTurboSizes[0]);
    anymaker::gc_string_view s{};
    g_string_ctor(&s, id.c_str());
    const void* found = nullptr;
    g_get_definition(&found, container, &s);
    return found != nullptr;
}

// Loads our JSON into a fresh vehicle_component_definition_file and hands it to the container. The path's
// characters must outlive the call ($string_ctor_cstr makes a non-owning view): they are g_json_path or
// g_json_forward, which never change once Turbo is ready, so the main and server threads can both add. Another
// mod's hook may sit under ours on this cell (GpsPart hooks it too); calling down through it is fine because every
// mod checks whether its parts are already in the container before adding them.
bool add_ours(void* container, const std::string& chars) {
    GamePath path{kStoreSystem, 0, {}};
    g_string_ctor(&path.path, chars.c_str());
    alignas(16) uint8_t file[64]{};
    g_file_ctor(file);
    bool ok = false;
    g_file_load(&ok, file, &path);
    if (ok) reinterpret_cast<add_definitions_t>(h_add_definitions.original)(container, file);
    g_file_dtor(file);
    return ok;
}

thread_local bool t_adding = false;
void hk_add_definitions(void* container, const void* file) {
    reinterpret_cast<add_definitions_t>(h_add_definitions.original)(container, file);
    if (!t_adding) { std::lock_guard<std::recursive_mutex> lock(g_mutex); g_definition_size.clear(); }   // definitions may move
    if (!g_ready.load() || t_adding || !container || already_added(container)) return;
    t_adding = true;
    bool ok = add_ours(container, g_json_path) || add_ours(container, g_json_forward);
    t_adding = false;
    static std::atomic<int> logs{6};
    if (logs.fetch_sub(1) > 0) log(ok ? 0 : 2, ok ? "Turbo parts added to the game's parts (%s)" : "could not load the Turbo definitions from %s", g_json_path.c_str());
}

// ------------------------------------------------------------------------------------- sounds (client)
// Each turbo and blow-off valve streams its own synthesized sound (turbo_sound.h) into a positional voice effect,
// the way the game plays voice chat, in place of the stock impeller hum. Main thread only.
namespace audio {
constexpr uint32_t play_voice_rva = 0x13f310;       // (effect_3d) application.audio.play_voice (s32 group, vec3, vec3, f64)
constexpr uint32_t append_rva = 0x13fa90;           // effect_3d.append_audio_data (takes the buffer's memory)
constexpr uint32_t set_position_velocity_rva = 0x13fa70;
constexpr uint32_t stop_rva = 0x13fa40;
constexpr uint32_t reset_rva = 0x13fa50;            // effect_3d.reset (releases the handle)
constexpr uint32_t buffer_reset_rva = 0x41b40;      // binary.buffer.reset (allocates with the game's allocator)
constexpr int32_t kGroupVehicle = 4;                // e_audio_group.vehicle: follows the vehicle volume slider
constexpr double kRange = 80.0;                     // metres
constexpr double kLead = 0.08;                      // seconds of sound queued ahead
// The voice object (what play_voice hands back) keeps the PCM still to play: a byte buffer whose size is at +0x78 and
// a read cursor in samples at +0x80 (effect_3d.append_audio_data's handler and the mixer, game.exe 0xe65b0/0xe66a0;
// 0.1.25: 0xe85f0/0xe86e0).
constexpr int kVoiceBytes = 0x78, kVoiceCursor = 0x80;
}  // namespace audio
struct GameBuffer { void* data; int32_t size; uint32_t pad; };
static_assert(sizeof(GameBuffer) == 16, "binary.buffer");
using play_voice_t = void (*)(void** effect, const int32_t* group, const double* position, const double* velocity, const double* range);
using effect_fn_t = void (*)(void** effect);
using effect_append_t = void (*)(void** effect, GameBuffer* buffer);
using effect_place_t = void (*)(void** effect, const double* position, const double* velocity);
using buffer_reset_t = void (*)(GameBuffer* buffer, const int32_t* size);
using world_transform_t = void (*)(double* ret, const void* component);
using client_tick_t = void (*)(void* self, void* network, void* vehicle, void* scene, const double* dt);
play_voice_t g_play_voice;
effect_fn_t g_effect_stop, g_effect_reset;
effect_append_t g_effect_append;
effect_place_t g_effect_place;
buffer_reset_t g_buffer_reset;
world_transform_t g_world_transform;
anymaker::cell_hook h_client_tick;

struct ClientVoice {
    void* effect = nullptr;                         // application.audio.effect_3d (a handle)
    Synth synth;
    double start = 0, last_tick = 0, retry_at = 0;
    long long written = 0;                          // samples queued since start
    bool rate_logged = false;
    double position[3]{}, velocity[3]{};
    bool placed = false;
};
std::unordered_map<const void*, ClientVoice> g_voices;   // keyed by client component
std::vector<int16_t> g_pcm;
double g_last_sweep = 0;

void release_voice(ClientVoice& v) {
    if (!v.effect) return;
    g_effect_stop(&v.effect);
    g_effect_reset(&v.effect);
    v.effect = nullptr;
}

// Samples the voice still has to play, or -1 when its fields don't look right.
long long voice_queued(void* effect) {
    auto* base = static_cast<uint8_t*>(effect);
    if (!effect || !anymaker::readable(base + audio::kVoiceBytes, 0x10)) return -1;
    int32_t bytes = at<int32_t>(effect, audio::kVoiceBytes), cursor = at<int32_t>(effect, audio::kVoiceCursor);
    if (bytes < 0 || cursor < 0 || bytes > 64 * kSampleRate) return -1;
    return std::max<long long>(0, bytes / 2 - cursor);    // the audio thread may be mid-update: never negative
}

// Turbo volume (Mod Settings, AnyHelpers): 0-200 %, applied to every sound this file makes (whistle, flutter,
// blow-off, antilag). Without AnyHelpers it stays at 100 %.
const AnyHelpersSettingsV1* g_settings = nullptr;
uint64_t g_volume_token = 0, g_blower_volume_token = 0, g_settings_revision = UINT64_MAX;
double g_volume = 1.0, g_blower_volume = 1.0;

void register_settings() {
    auto services = AnyAPI_Services();
    auto* s = services ? static_cast<const AnyHelpersSettingsV1*>(services->query("anyhelpers.settings", 1)) : nullptr;
    if (!s || s->struct_size != sizeof(*s) || s->version != 1 || !s->register_setting || !s->get || !s->revision) return;
    AnyModSettingV1 d;
    d.kind = ANY_SETTING_INTEGER;
    d.mod_id = "anyengineparts"; d.mod_name = "AnyEngineParts";
    d.setting_id = "turbo_volume"; d.label = "Turbo volume (%)";
    d.description = "Loudness of the turbo whistle, blow-off valves and antilag. 100 is the normal level.";
    d.default_number = 100; d.minimum = 0; d.maximum = 200; d.step = 5;
    g_volume_token = s->register_setting(&d);
    d.setting_id = "supercharger_volume"; d.label = "Supercharger volume (%)";
    d.description = "Loudness of the supercharger gear whine. 100 is the normal level.";
    g_blower_volume_token = s->register_setting(&d);
    if (g_volume_token || g_blower_volume_token) g_settings = s;
}

// The two volume settings (0-2), read again whenever the settings change.
void read_volumes() {
    if (!g_settings) return;
    uint64_t revision = g_settings->revision();
    if (revision == g_settings_revision) return;
    g_settings_revision = revision;
    AnySettingValueV1 v;
    if (g_volume_token && g_settings->get(g_volume_token, &v) && std::isfinite(v.number)) g_volume = std::clamp(v.number, 0.0, 200.0) / 100.0;
    if (g_blower_volume_token && g_settings->get(g_blower_volume_token, &v) && std::isfinite(v.number)) g_blower_volume = std::clamp(v.number, 0.0, 200.0) / 100.0;
}
double turbo_volume() { read_volumes(); return g_volume; }
double supercharger_volume() { read_volumes(); return g_blower_volume; }

void stream_sound(void* self, int kind, const double* dt) {
    double now = now_seconds();
    ClientVoice& v = g_voices[self];
    v.last_tick = now;
    double factor = at<double>(self, off::client_factor_speed), level = 0;
    bool flag = false, blown = false;
    if (is_blow_off(kind)) {
        v.synth.voice = Voice::kBlowOff;
        decode_blow_off_factor(factor, flag, level, v.synth.sound, v.synth.size);
    } else if (is_wastegate(kind)) {
        v.synth.voice = Voice::kAntilag;
        int unused = 0;
        decode_blow_off_factor(factor, flag, level, unused, v.synth.size);
    } else if (is_blower(kind)) {
        v.synth.voice = Voice::kSupercharger;
        v.synth.size = kind == kKindSuperchargerLarge ? 4 : 2;
        decode_turbo_factor(factor, level, flag, blown);
        blown = false;
    } else {
        v.synth.voice = Voice::kTurbo;
        v.synth.size = kind;
        decode_turbo_factor(factor, level, flag, blown);
    }
    double matrix[12]{};
    g_world_transform(matrix, self);
    const double* p = matrix + off::mat34_t / 8;
    double step = dt && std::isfinite(*dt) && *dt > 1e-4 ? *dt : 1.0 / 60.0;
    if (std::isfinite(p[0]) && std::isfinite(p[1]) && std::isfinite(p[2])) {
        for (int i = 0; i < 3; ++i) {
            double vel = v.placed ? (p[i] - v.position[i]) / step : 0.0;
            vel = std::max(-150.0, std::min(150.0, vel));
            v.velocity[i] += (vel - v.velocity[i]) * 0.2;   // frame-to-frame jitter would wobble the pitch
            v.position[i] = p[i];
        }
        v.placed = true;
    }
    if (!v.effect) {
        if (now < v.retry_at || !v.placed) return;
        int32_t group = audio::kGroupVehicle; double range = audio::kRange;
        g_play_voice(&v.effect, &group, v.position, v.velocity, &range);
        if (!v.effect) {
            v.retry_at = now + 1.0;
            static std::atomic<int> logs{3};
            if (logs.fetch_sub(1) > 0) log(1, "the game gave no voice for a turbo sound; retrying");
            return;
        }
        v.start = now; v.written = 0;
    }
    g_effect_place(&v.effect, v.position, v.velocity);
    // Keep about kLead seconds queued. The voice's own queue says how much is still to play: the game can play it
    // slower than real time or start late, and counting by the clock alone let a backlog build up until the sound
    // ran seconds behind the engine. Without it, count by the clock; after a stall the queue ran dry: start again.
    long long played = (long long)((now - v.start) * kSampleRate);
    long long queued = voice_queued(v.effect);
    if (queued >= 0) played = v.written - queued;
    if (v.written < played) v.written = played;
    if (!v.rate_logged && queued >= 0 && now - v.start > 10.0) {
        v.rate_logged = true;
        log(0, "turbo sound plays %.0f samples/s, %.0f ms queued", double(v.written - queued) / (now - v.start), queued * 1000.0 / kSampleRate);
    }
    long long want = played + (long long)(audio::kLead * kSampleRate) - v.written;
    if (want <= 0) return;
    int n = int(std::min<long long>(want, kSampleRate / 5));
    g_pcm.resize(size_t(n));
    render(v.synth, level, flag, g_pcm.data(), n, blown);
    if (double gain = is_blower(kind) ? supercharger_volume() : turbo_volume(); gain != 1.0)
        for (auto& sample : g_pcm) sample = int16_t(std::clamp(std::lround(sample * gain), -32768L, 32767L));
    GameBuffer buffer{nullptr, 0, 0};
    int32_t bytes = n * 2;
    g_buffer_reset(&buffer, &bytes);
    if (!buffer.data || buffer.size != bytes) return;
    std::memcpy(buffer.data, g_pcm.data(), size_t(bytes));
    g_effect_append(&v.effect, &buffer);
    if (buffer.data) { int32_t zero = 0; g_buffer_reset(&buffer, &zero); }   // the effect refused it (queue full)
    v.written += n;
}

void hk_client_tick(void* self, void* network, void* vehicle, void* scene, const double* dt) {
    int kind = 0;
    if (self && anymaker::readable(static_cast<uint8_t*>(self) + off::client_definition, 8)) {
        std::lock_guard<std::recursive_mutex> lock(g_mutex);
        kind = kind_of_definition(at<const void*>(self, off::client_definition));
    }
    if (!kind) { reinterpret_cast<client_tick_t>(h_client_tick.original)(self, network, vehicle, scene, dt); return; }
    // The wastegate only makes a sound (antilag bangs) once it has fired.
    bool silent = is_wastegate(kind) && !g_voices.count(self) && !(at<double>(self, off::client_factor_speed) > 0);
    if (!silent) stream_sound(self, kind, dt);
    double now = now_seconds();
    if (now - g_last_sweep > 1.0) {                 // parts gone from the scene stop ticking: free their voices
        g_last_sweep = now;
        for (auto it = g_voices.begin(); it != g_voices.end();)
            if (now - it->second.last_tick > 1.0) { release_voice(it->second); it = g_voices.erase(it); }
            else ++it;
    }
}

// ------------------------------------------------------------------------------------- engines: hot intake air
// The game's engines read only intake pressure: boost factor = remap(clamp(pressure, 1, 10), 1, 10, 1, 2), worked
// out once per engine tick with math.remap. Every engine (stock, rotary) gets the same treatment: the server engine
// tick notes its air manifolds' temperature, the remap call it makes scales the boost part of that factor by the
// air's density (turbo_health.h), and afterwards hot boosted air adds knock to its damage counter.
// math.remap is everywhere, so its hook only acts inside an engine tick and only on that call's constants.
using engine_tick_t = void (*)(void* self, void* vehicle, void* scene);
using remap_t = void (*)(double* ret, const double* v, const double* a, const double* b, const double* c, const double* d);
anymaker::cell_hook h_engine_tick, h_remap;
thread_local bool t_in_engine = false, t_fuel_cut = false;
thread_local double t_intake_c = 0, t_intake_pressure = 0, t_boost_factor = 0;

// An engine's air manifolds (its m_air_manifolds vector): their count, 0 if it cannot be read.
int32_t air_manifolds(const void* engine, PointerRing& entries) {
    read_ring(static_cast<const uint8_t*>(engine) + off::engine_air_manifolds, 32, entries);
    return entries.count;
}

// Notes which engine breathes through each of its air manifolds, twice a second per engine, so refresh_topology can
// tell what a boost pipe feeds (the Dyno names the boost setup from it). Entries not renewed go after a few seconds.
std::unordered_map<const void*, double> g_engine_noted;
double g_manifold_sweep = 0;
void note_manifolds(const void* engine) {
    double now = now_seconds();
    std::lock_guard<std::recursive_mutex> lock(g_mutex);
    double& noted = g_engine_noted[engine];
    if (now - noted < 0.5) return;
    noted = now;
    PointerRing entries;
    int32_t count = air_manifolds(engine, entries);
    for (int32_t i = 0; i < count; ++i)
        if (void* manifold = entries[i]) g_manifolds[manifold] = {engine, now};
    if (now - g_manifold_sweep > 10.0) {
        g_manifold_sweep = now;
        for (auto it = g_manifolds.begin(); it != g_manifolds.end();) it = now - it->second.at > 5.0 ? g_manifolds.erase(it) : std::next(it);
        for (auto it = g_engine_noted.begin(); it != g_engine_noted.end();) it = now - it->second > 5.0 ? g_engine_noted.erase(it) : std::next(it);
    }
}

// Average temperature of an engine's air manifolds' gas (or NaN), and in `pressure` their average pressure as the
// engine works it out (each content's volume over capacity, 0 for none; NaN if a manifold cannot be read).
double engine_intake_c(const void* engine, double& pressure) {
    pressure = NAN;
    PointerRing entries;
    int32_t count = air_manifolds(engine, entries);
    if (!count) return NAN;
    double sum = 0, pressure_sum = 0;
    int n = 0;
    bool all = true;
    for (int32_t i = 0; i < count; ++i) {
        const uint8_t* m = static_cast<const uint8_t*>(entries[i]);
        if (!m || !anymaker::readable(m + off::manifold_content, off::manifold_air_temperature + 8 - off::manifold_content)) { all = false; continue; }
        double c = at<double>(m, off::manifold_air_temperature);
        if (std::isfinite(c) && c > -100 && c < 2000) { sum += c; ++n; }
        double volume = at<double>(m, off::manifold_content + off::content_volume), capacity = at<double>(m, off::manifold_content + off::content_capacity);
        if (std::isfinite(volume) && std::isfinite(capacity) && capacity > 0) pressure_sum += volume / capacity;
    }
    if (all) pressure = pressure_sum / count;
    return n ? sum / n : NAN;
}

// The engine's boost factor: remap(clamp(pressure, 1, 10), 1, 10, 1, 2). Past 10 (only compounds get there) it carries
// on to kMaxEnginePressure, 3x; then the boost part is scaled for the air's density.
void hk_remap(double* ret, const double* v, const double* a, const double* b, const double* c, const double* d) {
    reinterpret_cast<remap_t>(h_remap.original)(ret, v, a, b, c, d);
    if (!t_in_engine || !ret || !a || !b || !c || !d || *a != 1.0 || *b != 10.0 || *c != 1.0 || *d != 2.0) return;
    if (t_fuel_cut) { t_boost_factor = 0; *ret = 0; return; }   // two-step: no fuel this tick (fuel burn scales with it)
    double factor = *ret;
    if (v && *v >= kStockEnginePressure && std::isfinite(t_intake_pressure) && t_intake_pressure > kStockEnginePressure)
        factor = power_multiplier(t_intake_pressure);
    t_boost_factor = factor;
    *ret = boost_factor_for_air(factor, t_intake_c);
}

void hk_engine_tick(void* self, void* vehicle, void* scene) {
    double pressure = NAN;
    double intake = self ? engine_intake_c(self, pressure) : NAN;
    t_in_engine = std::isfinite(intake);
    t_intake_c = intake;
    t_intake_pressure = pressure;
    t_boost_factor = 0;
    t_fuel_cut = false;
    if (self && t_in_engine) {                      // two-step: cut the fuel at the launch RPM a turbo's wastegate asks for
        std::lock_guard<std::recursive_mutex> lock(g_mutex);
        auto l = g_launch.find(self);
        if (l != g_launch.end()) {
            double now = now_seconds();
            auto* rps = static_cast<uint8_t*>(self) + off::engine_rps;
            double rpm = anymaker::readable(rps, 8) ? at<double>(rps, 0) * 60.0 : NAN;
            t_fuel_cut = two_step_cut(l->second.cutting, rpm, now - l->second.at < 0.5 ? l->second.rpm : 0.0);
            if (t_fuel_cut) l->second.cut_at = now;
            else if (now - l->second.at > 1.0) g_launch.erase(l);   // its input went to 0 (or the wastegate is gone)
        }
    }
    reinterpret_cast<engine_tick_t>(h_engine_tick.original)(self, vehicle, scene);
    t_in_engine = false;
    t_fuel_cut = false;
    if (self) note_manifolds(self);
    if (!std::isfinite(intake) || !(t_boost_factor > 1.0)) return;
    double dt = g_delta_time && std::isfinite(*g_delta_time) && *g_delta_time > 0 ? *g_delta_time : 1.0 / 60.0;
    double knock = knock_damage(intake, t_boost_factor, dt);
    if (knock > 0 && anymaker::readable(static_cast<uint8_t*>(self) + off::engine_damage_factor, 8)) at<double>(self, off::engine_damage_factor) += knock;
    if (knock > 0) dyno::note_knock(self, knock);     // the Dyno's "Knock" row
    static std::atomic<int> logs{40};
    static std::atomic<int64_t> last_ms{-1000000};
    int64_t ms = int64_t(now_seconds() * 1000);
    if (t_boost_factor > 1.05 && ms - last_ms.load() > 10000 && logs.fetch_sub(1) > 0) {
        last_ms = ms;
        log(0, "engine intake %.0f C: boost factor %.2f -> %.2f%s", intake, t_boost_factor, boost_factor_for_air(t_boost_factor, intake), knock > 0 ? " (knocking)" : "");
    }
}

void install_intake_air() {
    using namespace anymaker::experimental;
    void** tick = route::cell(bind::engine_tick);
    void* entry = route::function(bind::engine_tick);
    void** remap = route::cell(bind::math_remap);
    // The engine tick cell holds the game's function, or the Dyno's hook on it (any start order works).
    bool tick_ok = tick && entry && anymaker::readable(tick, 8) && (*tick == entry || route::in_other_module(*tick));
    bool ok = tick_ok && remap && *remap && route::executable(*remap);
    if (ok && h_remap.install(remap, (void*)&hk_remap)) {
        if (h_engine_tick.install(tick, (void*)&hk_engine_tick)) { log(0, "hot boost air on: intercoolers matter to every engine"); return; }
        h_remap.remove((void*)&hk_remap);
    }
    log(1, "hot boost air off (engine tick %d/%d/%d, remap %d); engines ignore intake temperature", !!tick, !!entry, tick_ok, !!remap);
}

// ------------------------------------------------------------------------------------- install (background thread)
// The destroy_node slot is read from the stock impeller's destroy, which calls it through the container's method
// table: relocation 1 of that function, right after its code (code_end 0xe8). It must be 8 * slot.
bool destroy_slot_matches(void* destroy_entry) {
    if (!destroy_entry) return false;
    uint64_t value = anymaker::read_slot(static_cast<const uint8_t*>(destroy_entry), 0xe8 + 8);
    if (value != uint64_t(8 * off::container_destroy_node_slot)) {
        log(2, "impeller destroy's container slot reads %llu, expected %d; Turbo stays off", static_cast<unsigned long long>(value), 8 * off::container_destroy_node_slot);
        return false;
    }
    return true;
}

// Sounds are optional: without them the parts keep the stock impeller hum.
// The base get_gas_flood_nodes is 18 bytes of nothing (no unique code), so its cell is checked by those bytes; the
// two-node function by its whole code, which must point at our layout (its second node 0x40 after the first).
void install_inline_valves() {
    static const uint8_t kEmptyFlood[] = {0x48, 0x83, 0xec, 0x18, 0x48, 0x89, 0x0c, 0x24, 0x48, 0x89, 0x54, 0x24, 0x08, 0x48, 0x83, 0xc4, 0x18, 0xc3};
    static_assert(off::pump_node_b - off::pump_node_a == 0x220 - bind::kTwoNodeFloodFirst, "the two-node function reads nodes 0x40 apart");
    uint8_t* hits[4]{};
    size_t n = anymaker::find_gcl_functions(bind::kTwoNodeFloodCode, hits, 4);
    void** cell = route::cell(bind::component_get_gas_flood_nodes);
    bool empty = cell && *cell && anymaker::readable(*cell, sizeof kEmptyFlood) && std::memcmp(*cell, kEmptyFlood, sizeof kEmptyFlood) == 0;
    if (n && empty) {
        g_two_node_flood = reinterpret_cast<flood_nodes_t>(hits[0]);
        g_bov_inline = h_flood.install(cell, (void*)&hk_flood);
    }
    if (!g_bov_inline)
        log(1, "inline blow-off valves pass gas by levelling their two sides (flood hook: code %zu, cell %d, empty %d)", n, !!cell, empty);
}

// The oil ports' liquid functions. Without them turbos have no oil ports and count as oil fed.
void install_oil() {
    using namespace anymaker::experimental;
    g_liquid_ctor = reinterpret_cast<self_fn_t>(route::function(bind::liquid_content_ctor));
    g_liquid_dtor = reinterpret_cast<self_fn_t>(route::function(bind::liquid_content_dtor));
    g_liquid_node_ctor = reinterpret_cast<self_fn_t>(route::function(bind::liquid_node_ctor));
    g_liquid_node_dtor = reinterpret_cast<self_fn_t>(route::function(bind::liquid_node_dtor));
    g_liquid_create = reinterpret_cast<liquid_create_t>(route::function(bind::liquid_content_create));
    g_liquid_tick = reinterpret_cast<liquid_tick_t>(route::function(bind::liquid_content_tick));
    g_liquid_move = reinterpret_cast<liquid_move_t>(route::function(bind::liquid_content_move));
    g_liquid_modify = reinterpret_cast<liquid_modify_t>(route::function(bind::liquid_content_modify));
    g_oil_ready = g_liquid_ctor && g_liquid_dtor && g_liquid_node_ctor && g_liquid_node_dtor && g_liquid_create && g_liquid_tick && g_liquid_move && g_liquid_modify;
    if (!g_oil_ready)
        log(1, "turbo oil ports off (content %d/%d/%d/%d/%d/%d, node %d/%d); turbos count as oil fed", !!g_liquid_ctor, !!g_liquid_dtor, !!g_liquid_create, !!g_liquid_tick,
            !!g_liquid_move, !!g_liquid_modify, !!g_liquid_node_ctor, !!g_liquid_node_dtor);
}

void install_sound() {
    using namespace anymaker::experimental;
    void** cell = route::cell(bind::client_centrifugal_tick);
    void* entry = route::function(bind::client_centrifugal_tick);
    g_world_transform = reinterpret_cast<world_transform_t>(route::function(bind::client_get_world_transform));
    g_play_voice = reinterpret_cast<play_voice_t>(native(audio::play_voice_rva));
    g_effect_append = reinterpret_cast<effect_append_t>(native(audio::append_rva));
    g_effect_place = reinterpret_cast<effect_place_t>(native(audio::set_position_velocity_rva));
    g_effect_stop = reinterpret_cast<effect_fn_t>(native(audio::stop_rva));
    g_effect_reset = reinterpret_cast<effect_fn_t>(native(audio::reset_rva));
    g_buffer_reset = reinterpret_cast<buffer_reset_t>(native(audio::buffer_reset_rva));
    bool found = cell && entry && *cell == entry && g_world_transform && g_play_voice && g_effect_append && g_effect_place &&
                 g_effect_stop && g_effect_reset && g_buffer_reset;
    if (!found || !h_client_tick.install(cell, (void*)&hk_client_tick)) {
        log(1, "turbo sounds unavailable (client tick %d/%d/%d, transform %d, audio %d/%d/%d/%d/%d/%d); turbos keep the impeller hum",
            !!cell, !!entry, cell && entry && *cell == entry, !!g_world_transform, !!g_play_voice, !!g_effect_append, !!g_effect_place,
            !!g_effect_stop, !!g_effect_reset, !!g_buffer_reset);
        return;
    }
    log(0, "turbo sounds on");
}

// Placement preview: the editor draws the part with an empty colour list, so every paint slot shows the default
// grey. For the mod's parts, hand it the colours a new part starts with instead.
using push_mesh_static_t = void (*)(void*, void*, const void*, const void*, const void*, const void*, const void*, const void*, const void*);
anymaker::cell_hook h_push_mesh_static;
struct ColourArray { const uint8_t* buffer; int32_t count, element_size; void* ctor; void* dtor; };   // array<color8>
void hk_push_mesh_static(void* renderer, void* layers, const void* composite, const void* transform, const void* a, const void* b, const void* c,
                         const void* d, const void* colours) {
    auto original = reinterpret_cast<push_mesh_static_t>(h_push_mesh_static.original);
    const StartColours* start = nullptr;
    if (composite && colours && anymaker::readable(colours, sizeof(ColourArray)) && static_cast<const ColourArray*>(colours)->count == 0) {
        std::lock_guard<std::recursive_mutex> lock(g_mutex);
        int kind = kind_of_definition(static_cast<const uint8_t*>(composite) - off::definition_mesh_static);
        if (is_valve(kind)) start = &kValveColours;
        else if (is_air_filter(kind)) start = &kFilterColours;
        else if (is_intercooler(kind)) start = &kCoolerColours;
        else if (kind >= kMinSize && kind <= kMaxSize) start = &kTurboColours;
        else if (is_blower(kind)) start = &kBlowerColours;
    }
    if (!start) { original(renderer, layers, composite, transform, a, b, c, d, colours); return; }
    uint8_t rgba[2][4] = {{start->rgb[0][0], start->rgb[0][1], start->rgb[0][2], 255}, {start->rgb[1][0], start->rgb[1][1], start->rgb[1][2], 255}};
    ColourArray painted{rgba[0], 2, 4, static_cast<const ColourArray*>(colours)->ctor, static_cast<const ColourArray*>(colours)->dtor};
    original(renderer, layers, composite, transform, a, b, c, d, &painted);
}
void install_preview_colours() {
    using namespace anymaker::experimental;
    void** cell = route::cell(bind::push_mesh_static);
    void* entry = route::function(bind::push_mesh_static);
    if (!cell || !entry || *cell != entry || !h_push_mesh_static.install(cell, (void*)&hk_push_mesh_static))
        log(1, "placement preview colours unavailable (cell %d, entry %d); new parts show grey until placed", !!cell, !!entry);
}

void remove_hooks();
void install_hooks() {
    using namespace anymaker::experimental;
    for (int attempt = 0; attempt < 180 && !g_stop; ++attempt) {
        if (attempt) Sleep(1000);
        if (!matching_build()) { if (attempt == 0) log(1, "game build does not match the SDK reference; Turbo stays off"); return; }
        int roots = -1;                               // matches of the definition functions' route root (route_cell.h)
        void** add = route::cell(bind::definitions_add_definitions, &roots);
        void** create = route::cell(bind::centrifugal_create);
        void** destroy = route::cell(bind::centrifugal_destroy);
        void** tick = route::cell(bind::centrifugal_tick);
        void** get = route::cell(bind::centrifugal_get_data_f64);
        auto fctor = reinterpret_cast<self_fn_t>(route::function(bind::definition_file_ctor));
        auto fdtor = reinterpret_cast<self_fn_t>(route::function(bind::definition_file_dtor));
        auto fload = reinterpret_cast<file_load_t>(route::function(bind::definition_file_load));
        auto cctor = reinterpret_cast<self_fn_t>(route::function(bind::gas_content_ctor));
        auto cdtor = reinterpret_cast<self_fn_t>(route::function(bind::gas_content_dtor));
        auto nctor = reinterpret_cast<self_fn_t>(route::function(bind::gas_node_ctor));
        auto ndtor = reinterpret_cast<self_fn_t>(route::function(bind::gas_node_dtor));
        auto ccreate = reinterpret_cast<content_create_t>(route::function(bind::gas_content_create));
        auto cmodify = reinterpret_cast<content_modify_t>(route::function(bind::gas_content_modify));
        auto cmove = reinterpret_cast<content_move_t>(route::function(bind::gas_content_move));
        auto pset = reinterpret_cast<property_set_t>(route::function(bind::property_u8_set));
        auto sc = reinterpret_cast<string_ctor_t>(native(anymaker::sym::string_ctor_cstr_rva));
        auto dt = static_cast<const double*>(global(bind::server_delta_time));
        if (add && !route::add_cell_ok(add)) {        // a wrong route: look again (route_cell.h)
            static int logs = 3;
            if (logs-- > 0) log(1, "the definitions route led to %s, not _add_definitions; looking again", route::what_is(anymaker::readable(add, 8) ? *add : nullptr));
            add = nullptr;
        }
        if (fload && !route::file_load_ok((void*)fload)) {
            static int logs = 3;
            if (logs-- > 0) log(1, "the file load route led to %s, not vehicle_component_definition_file.load; looking again", route::what_is((void*)fload));
            fload = nullptr;
        }
        bool core = add && create && destroy && tick && get && fctor && fdtor && fload && cctor && cdtor && nctor && ndtor && ccreate && cmodify && cmove && pset && sc;
        if (!core) {
            if (attempt == 20 || attempt == 179)   // once early, so a stuck start shows in the log
                log(attempt == 179 ? 2 : 1, "%s the game functions (route root matches %d, add=%d create=%d destroy=%d tick=%d get=%d file=%d/%d/%d content=%d/%d/%d/%d/%d node=%d/%d property=%d string=%d)",
                    attempt == 179 ? "could not locate" : "still looking for", roots, !!add, !!create, !!destroy, !!tick, !!get, !!fctor, !!fdtor, !!fload, !!cctor, !!cdtor, !!ccreate, !!cmodify, !!cmove, !!nctor, !!ndtor, !!pset, !!sc);
            continue;
        }
        std::lock_guard install_lock(engine_parts::install_mutex());   // one AnyEngineParts part hooks at a time
        if (!destroy_slot_matches(*destroy)) return;
        g_file_ctor = fctor; g_file_dtor = fdtor; g_file_load = fload;
        g_content_ctor = cctor; g_content_dtor = cdtor; g_node_ctor = nctor; g_node_dtor = ndtor;
        g_content_create = ccreate; g_content_modify = cmodify; g_content_move = cmove; g_property_set = pset; g_string_ctor = sc;
        g_delta_time = dt && anymaker::readable(dt, 8) ? dt : nullptr;
        void** tooltip = route::cell(bind::centrifugal_build_tooltip);
        g_component_tooltip = reinterpret_cast<build_tooltip_t>(route::function(bind::component_build_tooltip));
        g_units_flow = reinterpret_cast<units_t>(route::function(bind::units_flow));
        g_units_frequency = reinterpret_cast<units_t>(route::function(bind::units_frequency));
        g_units_pressure = reinterpret_cast<units_t>(route::function(bind::units_pressure));
        g_units_temperature = reinterpret_cast<units_t>(route::function(bind::units_temperature));   // optional: no row without it
        g_units_percentage = reinterpret_cast<units_t>(route::function(bind::units_percentage));     // optional too (Condition)
        g_to_pascals = reinterpret_cast<to_pascals_t>(route::function(bind::pressure_to_pascals));
        g_string_dtor = reinterpret_cast<self_fn_t>(native(anymaker::sym::string_dtor_rva));
        bool tooltip_ok = tooltip && g_component_tooltip && g_units_flow && g_units_frequency && g_units_pressure && g_to_pascals && g_string_dtor;
        g_create_node_bound = reinterpret_cast<create_node_t>(route::function(bind::gas_container_create_node));
        g_get_definition = reinterpret_cast<get_definition_t>(route::function(bind::definitions_get_by_id));
        if (!g_get_definition) log(1, "definition lookup not found; Turbo may add its parts twice if another mod re-adds definitions");
        g_get_rps = reinterpret_cast<get_rps_t>(route::function(bind::torque_node_get_rps));
        g_apply_torque = reinterpret_cast<apply_torque_t>(route::function(bind::torque_node_apply_torque));
        auto npj = static_cast<const double*>(global(bind::torque_newtons_per_joule));
        g_newtons_per_joule = npj && anymaker::readable(npj, 8) && std::isfinite(*npj) && *npj > 0 && *npj < 100 ? npj : nullptr;
        if (!g_get_rps || !g_apply_torque || !g_newtons_per_joule)
            log(1, "supercharger shaft functions not found (rps %d, torque %d, newtons per joule %d); superchargers make no boost", !!g_get_rps, !!g_apply_torque, !!g_newtons_per_joule);
        else log(0, "supercharger shaft functions found (%.4f newtons per joule)", *g_newtons_per_joule);
        if (!write_definition_file()) { log(2, "could not write the Turbo definitions file; Turbo is off"); return; }
        if (!route::add_cell_ok(add)) { log(2, "the definitions hook cell changed to %s before hooking; Turbo is off", route::what_is(*add)); return; }
        bool ok = h_destroy.install(destroy, (void*)&hk_destroy) && h_create.install(create, (void*)&hk_create) &&
                  h_tick.install(tick, (void*)&hk_tick) && h_get_f64.install(get, (void*)&hk_get_f64) &&
                  h_add_definitions.install(add, (void*)&hk_add_definitions);
        if (!ok) { log(2, "a hook cell was already taken or not writable; Turbo is off"); remove_hooks(); return; }   // none left half-installed
        void** properties = route::cell(bind::centrifugal_get_properties);
        if (!properties || !h_properties.install(properties, (void*)&hk_properties))
            log(1, "blow-off valve spring setting unavailable (Properties Tool hook not found); it stays at %.0f kPa", kBlowOffDefaultKpa);
        if (!tooltip_ok || !h_tooltip.install(tooltip, (void*)&hk_tooltip))
            log(1, "turbo tooltip unavailable (tooltip=%d base=%d units=%d/%d/%d pascals=%d string=%d); turbos show the stock impeller tooltip",
                !!tooltip, !!g_component_tooltip, !!g_units_flow, !!g_units_frequency, !!g_units_pressure, !!g_to_pascals, !!g_string_dtor);
        else if (!g_units_percentage || !g_units_temperature)
            log(1, "turbo tooltip without its %s row (game formatter not found)", !g_units_percentage ? "Condition" : "Temperature");
        install_inline_valves();
        install_oil();
        install_intake_air();
        init_wear_path();
        install_sound();
        install_preview_colours();
        g_ready = true;
        if (roots > 1) log(0, "the definition functions' route root matched %d times; all matches agreed", roots);
        log(0, "Ready after %d s (server tick %s). Load or start a world to add the Turbo parts (definitions %s).", attempt,
            g_delta_time ? "found" : "not found, assuming 60 Hz", g_json_path.c_str());
        return;
    }
}
void remove_hooks() {
    h_engine_tick.remove((void*)&hk_engine_tick);
    h_remap.remove((void*)&hk_remap);
    h_client_tick.remove((void*)&hk_client_tick);
    h_push_mesh_static.remove((void*)&hk_push_mesh_static);
    h_add_definitions.remove((void*)&hk_add_definitions);
    h_flood.remove((void*)&hk_flood);
    h_properties.remove((void*)&hk_properties);
    h_tooltip.remove((void*)&hk_tooltip);
    h_get_f64.remove((void*)&hk_get_f64);
    h_tick.remove((void*)&hk_tick);
    h_create.remove((void*)&hk_create);
    h_destroy.remove((void*)&hk_destroy);
}
}  // namespace

// The Dyno's view of a vehicle's boost (engine_parts_shared.h). A turbo or supercharger counts when its boost reaches
// an engine (straight, or through further stages); if none is seen to (an air manifold the walk did not reach), every
// one on the vehicle counts. A compound is a turbo breathing another turbo's boost.
VehicleBoost vehicle_boost(const void* vehicle) {
    VehicleBoost out;
    if (!vehicle) return out;
    std::lock_guard<std::recursive_mutex> lock(g_mutex);
    double now = now_seconds();
    auto feeds_engine = [&](const void* component) {
        for (int hop = 0; component && hop < 6; ++hop) {
            auto t = g_turbos.find(component);
            if (t == g_turbos.end()) return false;
            if (t->second.feeds_engine) return true;
            component = t->second.feeds_stage;
        }
        return false;
    };
    BoostSetup all, fed;
    for (auto& [component, t] : g_turbos) {
        if (t.vehicle != vehicle || now - t.ticked_at > 1.0) continue;
        auto up = t.upstream ? g_turbos.find(t.upstream) : g_turbos.end();
        bool compound = !t.blower && up != g_turbos.end() && !up->second.blower;
        for (BoostSetup* b : {&all, feeds_engine(component) ? &fed : nullptr}) {
            if (!b) continue;
            ++(t.blower ? b->superchargers : b->turbos);
            b->compound = b->compound || compound;
        }
        if (t.blower && std::isfinite(t.state.slots[kDrivePower])) out.drive_watts += std::max(0.0, t.state.slots[kDrivePower]) * 1000.0;
    }
    out.setup = boost_setup_name(fed.turbos + fed.superchargers > 0 ? fed : all);
    return out;
}

// AnyEngineParts (engine_parts_mod.cpp) is the DLL; it starts this part of it.
void init(const AnyModHostV1& h) { host = h; }
void ready() { register_settings(); }   // AnyAPI_ModReady, on the loader thread
void start() { install_hooks(); }       // on AnyEngineParts' install thread
void shutdown() { g_stop = true; remove_hooks(); }
}  // namespace turbo
