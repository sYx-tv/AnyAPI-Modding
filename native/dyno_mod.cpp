// Dyno: a "Dyno" sensor part that records engine power and torque curves and draws them on screen.
//
// Put the part anywhere on a vehicle. Hold the throttle at 90 % or more for a pull: the engines' power (the energy the
// game's engine tick turns into motion, x60) is binned by RPM, and when the throttle comes off a pull of at least
// 1,500 RPM is kept. The last three pulls are drawn over each other (oldest grey, then blue, newest orange), with
// power solid and torque dashed, beside each pull's peaks, the latest pull's boost, engine temperature and knock.
// Power is net of what superchargers take through their belts, and the header names the boost setup (Turbo's
// vehicle_boost: single to quad turbo, compound, supercharged, twincharged).
// The graph shows while the local player is within 40 m of a vehicle carrying a Dyno; the "Show / hide dyno" key
// (F6, rebindable in Controls) hides it, and holding that key for a second clears the pulls. The part's data port
// gives rpm, power, torque, boost, temperature and the latest peak to microcontrollers (dyno_logic.h).
//
// How: the part is the game's altitude sensor class (dyno_logic.h; nothing else hooks that class), added to the
// game's part definitions like the Rotary. The server engine tick is hooked to note each engine's numbers, and the
// altitude sensor's server tick (for Dyno parts only) sums its vehicle's engines and feeds the recorder. Rendering
// uses anyapi.gpu_draw. Single player / host: the engines and the part tick on the server of this game.
//
// EXPERIMENTAL. Hooks game functions through the experimental SDK (dyno_bindings.h): Anymaker 0.2.0 / Steam build
// 26328453 only. First runs are diagnostic: read anymaker_modding.log.
#define NOMINMAX
#include <windows.h>
#include "anyapi_services_v1.h"
#include "anyapi_gpu_draw_v1.h"
#include "anyapi_ui_state_v1.h"
#include "anyapi_players_v1.h"
#include "mod_controls_v1.h"
#include "anyapi_experimental.hpp"
#include "dyno_bindings.h"
#include "route_cell.h"
#include "dyno_logic.h"
#include "dyno_mesh.h"
#include "engine_parts_shared.h"
#include "dyno_net.h"
#include "steam_messages.h"
#include <atomic>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <cwchar>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

namespace dyno {
namespace {
constexpr const char* kModId = "dyno";

AnyModHostV1 host;
std::atomic<bool> g_ready{false}, g_stop{false};

void log(int level, const char* fmt, ...) {
    if (!host.log) return;
    char buf[640];
    va_list ap; va_start(ap, fmt); vsnprintf(buf, sizeof buf, fmt, ap); va_end(ap);
    host.log(uint32_t(level), kModId, buf);
}
double now_seconds() {
    static LARGE_INTEGER freq = [] { LARGE_INTEGER f; QueryPerformanceFrequency(&f); return f; }();
    LARGE_INTEGER t; QueryPerformanceCounter(&t);
    return double(t.QuadPart) / double(freq.QuadPart);
}

// Reference layouts (metadata, Anymaker 0.1.25).
namespace off {
constexpr ptrdiff_t component_definition = 312;      // server_scene.vehicle_component.m_definition
constexpr ptrdiff_t definition_id = 0;               // vehicle_component_definition.m_id (string)
constexpr ptrdiff_t engine_rps = 1424;               // server_scene.vehicle_component.engine.m_rps (f64)
constexpr ptrdiff_t engine_temperature = 1432;       // m_temperature (f64, C)
constexpr ptrdiff_t engine_throttle = 1472;          // m_display_throttle (f64)
constexpr ptrdiff_t engine_boost = 1496;             // m_display_air_pressure_factor (f64, 1..2)
constexpr ptrdiff_t engine_energy = 1536;            // m_display_torque (f64): energy this tick
// 0.2.0 layouts (sdk/reference-0.2.0): m_content_lubricant +0x2a0 and m_content_coolant +0x388 are
// vehicle_liquid_content_volume, whose vehicle_liquid_content base has m_volume +0x20, m_capacity +0x28, m_temperature +0x30.
constexpr ptrdiff_t engine_oil_volume = 0x2a0 + 0x20, engine_oil_capacity = 0x2a0 + 0x28, engine_oil_temperature = 0x2a0 + 0x30;
constexpr ptrdiff_t engine_coolant_temperature = 0x388 + 0x30;
}  // namespace off
constexpr int32_t kStoreSystem = 1;                  // file.e_store.system (as GpsPart, Turbo and Rotary)

template <class T> const T& at(const void* base, ptrdiff_t offset) { return *reinterpret_cast<const T*>(static_cast<const uint8_t*>(base) + offset); }

using self_fn_t = void (*)(void* self);
using string_ctor_t = void (*)(anymaker::gc_string_view*, const char*);
using get_definition_t = void (*)(const void** ret, const void* container, const anymaker::gc_string_view* id);
using add_definitions_t = void (*)(void* container, const void* file);
using file_load_t = void (*)(bool* ret, void* file, const void* path);
using engine_tick_t = void (*)(void* self, void* vehicle, void* scene);
using sensor_tick_t = void (*)(uint8_t* sensor, void* vehicle, void* scene);
using get_f64_t = void (*)(double** ret, uint8_t* sensor, const anymaker::gc_string_view* name);
using transform_t = void (*)(double* out, const void* object);

string_ctor_t g_string_ctor;
get_definition_t g_get_definition;
self_fn_t g_file_ctor, g_file_dtor;
file_load_t g_file_load;
transform_t g_vehicle_transform, g_component_transform;
anymaker::cell_hook h_add_definitions, h_engine_tick, h_sensor_tick, h_get_f64;

// ------------------------------------------------------------------------------------- shared state
struct EngineEntry {
    const void* vehicle = nullptr; EngineSample sample; double time = 0, knock = 0, knock_time = -1e9;
    double oil_level = 0, oil_c = NAN, coolant_c = NAN;   // for the Oil Gauge (gauges_logic.h)
};
// A Boost Controller, Boost Gauge or Oil Gauge (gauges_logic.h). Kept like the Dynos: microcontrollers hold pointers
// into the slots.
struct Gauge {
    gauges::Kind kind = gauges::kNone; double slots[gauges::kMaxSlots]{}; double peak = 0; bool reset_held = false; double last_tick = 0;
    // ECU (ecu_logic.h): its panel tune (loaded from the world's file on first tick, saved when the panel changes it)
    ecu::Tune panel; bool panel_loaded = false, panel_dirty = false;
    void* vehicle = nullptr;
    double position[3]{}; bool placed = false;
};
struct Dyno {
    Recorder recorder;
    std::string setup;                                // Turbo's name for the boost setup ("Twin turbo", ...)
    double slots[kChannelCount]{};
    double position[3]{};
    bool placed = false, clear_held = false;
    double last_tick = 0;
};
std::mutex g_mutex;                                   // server thread (ticks, data) and render thread
std::unordered_map<const void*, EngineEntry> g_engines;
// Keyed by server component. Entries are never erased: microcontrollers keep pointers into their slots (hk_get_f64)
// until their next rebuild, so a Dyno that stops ticking keeps its entry (a few KB) for the rest of the session.
std::unordered_map<const uint8_t*, Dyno> g_dynos;
std::unordered_map<const void*, int> g_is_dyno;       // definition pointer -> kDynoKind, a gauges::Kind or 0 (cleared on each add)
std::unordered_map<const uint8_t*, Gauge> g_gauges;    // keyed by server component, never erased (as g_dynos)
constexpr int kDynoKind = 100;
std::atomic<int64_t> g_gauge_tick_ms{INT64_MIN / 2};   // when an Oil Gauge last ticked: engines' oil is noted while one does
double g_last_sweep = 0;
// When a Dyno last ticked (ms, now_seconds clock). Engine ticks are only noted while one did in the last 5 s.
std::atomic<int64_t> g_dyno_tick_ms{INT64_MIN / 2};
int64_t now_ms() { return int64_t(now_seconds() * 1000); }
bool dyno_ticking() { return now_ms() - g_dyno_tick_ms.load(std::memory_order_relaxed) < 5000; }
bool oil_gauge_ticking() { return now_ms() - g_gauge_tick_ms.load(std::memory_order_relaxed) < 5000; }
// When this PC's server last ticked any altitude-class sensor: it is the host (or single player) while it does.
std::atomic<int64_t> g_server_tick_ms{INT64_MIN / 2};
bool hosting() { return now_ms() - g_server_tick_ms.load(std::memory_order_relaxed) < 2000; }

// Which of our sensor parts this altitude sensor is: kDynoKind, a gauges::Kind, or 0 (a stock altitude sensor).
int sensor_kind(const uint8_t* sensor) {
    if (!sensor || !anymaker::readable(sensor + off::component_definition, 8)) return 0;
    auto* definition = at<const uint8_t*>(sensor, off::component_definition);
    if (!definition) return 0;
    auto found = g_is_dyno.find(definition);
    if (found != g_is_dyno.end()) return found->second;
    int kind = 0;
    if (anymaker::readable(definition + off::definition_id, sizeof(anymaker::gc_string_view))) {
        auto* id = reinterpret_cast<const anymaker::gc_string_view*>(definition + off::definition_id);
        if (id->data && id->length > 0 && id->length < 64 && anymaker::readable(id->data, size_t(id->length)))
            kind = id->view() == kDefinitionId ? kDynoKind : int(gauges::kind_of_id(id->data, size_t(id->length)));
    }
    g_is_dyno[definition] = kind;
    return kind;
}

// ------------------------------------------------------------------------------------- part definition
struct GamePath { int32_t store; uint32_t pad; anymaker::gc_string_view path; };
static_assert(sizeof(GamePath) == 24, "file.path");
std::string g_json_path, g_json_forward;              // absolute, backslashes / slashes; set before g_ready, then fixed
thread_local bool t_adding = false;

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

// Writes the Dyno mesh into rom/meshes/components (where the game loads part meshes) and the definition file into
// "AnyAPI and Modding/Dyno".
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
    std::string mesh_path = kStockMeshPath;
    std::vector<uint8_t> stock = read_file(components + L"compass_sensor_a.mesh");
    std::vector<uint8_t> mesh = build_dyno_mesh(stock, kDynoMeshName);
    std::string name = kDynoMeshName;
    if (!mesh.empty() && write_file(components + std::wstring(name.begin(), name.end()) + L".mesh", mesh.data(), mesh.size())) mesh_path = kDynoMeshPath;
    else log(1, "compass mesh not recognised or not writable (%zu bytes); the Dyno uses the stock compass look", stock.size());
    dir += L"\\Dyno";
    CreateDirectoryW(dir.c_str(), nullptr);
    std::wstring file = dir + L"\\dyno.json";
    std::string json = definition_json(mesh_path);
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
    anymaker::gc_string_view id{};
    g_string_ctor(&id, kDefinitionId);
    const void* found = nullptr;
    g_get_definition(&found, container, &id);
    return found != nullptr;
}

// Loads our JSON into a fresh vehicle_component_definition_file and hands it to the container. The path's
// characters must outlive the call (the string view does not own them): they are g_json_path or g_json_forward,
// which never change once the part is ready, so the main and server threads can both add.
bool add_ours(void* container, const std::string& chars) {
    GamePath path{kStoreSystem, 0, {}};
    g_string_ctor(&path.path, chars.c_str());
    alignas(16) uint8_t file[64]{};
    g_file_ctor(file);
    bool ok = false;
    g_file_load(&ok, file, &path);
    t_adding = true;
    if (ok) reinterpret_cast<add_definitions_t>(h_add_definitions.original)(container, file);
    t_adding = false;
    g_file_dtor(file);
    return ok;
}

void hk_add_definitions(void* container, const void* file) {
    reinterpret_cast<add_definitions_t>(h_add_definitions.original)(container, file);
    if (!t_adding) { std::lock_guard lock(g_mutex); g_is_dyno.clear(); }   // definitions may move (as Turbo and Rotary)
    if (!g_ready.load() || t_adding || !container || already_added(container)) return;
    bool ok = add_ours(container, g_json_path) || add_ours(container, g_json_forward);
    static std::atomic<int> logs{6};
    if (logs.fetch_sub(1) > 0) log(ok ? 0 : 2, ok ? "Dyno part added to the game's parts (%s)" : "could not load the Dyno definition from %s", g_json_path.c_str());
}

// ------------------------------------------------------------------------------------- server ticks
void hk_engine_tick(void* self, void* vehicle, void* scene) {
    // The engine speed the tick makes its power at: the game reads m_rps first and only sets the new speed at the end.
    // Read after the tick, an engine bouncing on the fuel cut showed power above 8,000 RPM.
    bool feed = self && vehicle && (dyno_ticking() || oil_gauge_ticking()) &&
                anymaker::readable(static_cast<uint8_t*>(self) + off::engine_rps, off::engine_energy + 8 - off::engine_rps);
    double rps_before = feed ? at<double>(self, off::engine_rps) : 0;
    reinterpret_cast<engine_tick_t>(h_engine_tick.original)(self, vehicle, scene);
    if (!feed) return;                              // nothing to feed: keep the engine tick cheap
    EngineSample s;
    s.engines = 1;
    s.rps = rps_before;
    s.watts = at<double>(self, off::engine_energy) * kTicksPerSecond;
    s.throttle = at<double>(self, off::engine_throttle);
    s.boost = at<double>(self, off::engine_boost);
    s.temperature_c = at<double>(self, off::engine_temperature);
    for (double* v : {&s.rps, &s.watts, &s.throttle, &s.boost, &s.temperature_c}) if (!std::isfinite(*v)) *v = 0;
    double oil_level = 0, oil_c = NAN, coolant_c = NAN;
    auto* engine = static_cast<const uint8_t*>(self);
    if (anymaker::readable(engine + off::engine_oil_volume, off::engine_coolant_temperature + 8 - off::engine_oil_volume)) {
        double volume = at<double>(engine, off::engine_oil_volume), capacity = at<double>(engine, off::engine_oil_capacity);
        oil_level = std::isfinite(volume) && std::isfinite(capacity) && capacity > 0 ? std::clamp(volume / capacity, 0.0, 1.0) : 0.0;
        oil_c = at<double>(engine, off::engine_oil_temperature);
        coolant_c = at<double>(engine, off::engine_coolant_temperature);
    }
    std::lock_guard lock(g_mutex);
    auto& e = g_engines[self];
    e.vehicle = vehicle; e.sample = s; e.time = now_seconds();
    e.oil_level = oil_level; e.oil_c = oil_c; e.coolant_c = coolant_c;
}

// A Boost Controller, Boost Gauge or Oil Gauge's server tick.
void ecu_tick(uint8_t* sensor, void* vehicle);
void gauge_tick(uint8_t* sensor, void* vehicle, gauges::Kind kind) {
    using namespace gauges;
    if (kind == kEcu) { ecu_tick(sensor, vehicle); return; }
    double target = 0;
    if (kind == kController) {
        { std::lock_guard lock(g_mutex); target = controller_target(g_gauges[sensor].slots); }
        turbo::set_boost_target(vehicle, target);   // Turbo's lock, not ours
    }
    turbo::VehicleBoost boost = kind == kOilGauge ? turbo::VehicleBoost{} : turbo::vehicle_boost(vehicle);
    std::lock_guard lock(g_mutex);
    double now = now_seconds();
    Gauge& g = g_gauges[sensor];
    bool fresh = !(g.last_tick > 0) || now - g.last_tick > 5.0;
    g.kind = kind; g.last_tick = now;
    double* o = g.slots;
    if (kind == kController) {
        o[kControllerBoostPsi] = boost.boost_psi;
        o[kTargetSet] = target;
    } else if (kind == kBoostGauge) {
        double psi = std::max(0.0, boost.boost_psi);
        bool reset = o[kResetPeak] > 0.5;
        if ((reset && !g.reset_held) || fresh) g.peak = 0;
        g.reset_held = reset;
        g.peak = std::max(g.peak, psi);
        o[kBoostPsi] = psi; o[kBoostKpa] = psi / kPsiPerKpa; o[kBoostBar] = psi / kPsiPerKpa / 100.0;
        o[kBoostTarget] = boost.target_psi; o[kPeakPsi] = g.peak;
    } else {
        g_gauge_tick_ms.store(int64_t(now * 1000), std::memory_order_relaxed);
        const EngineEntry* hot = nullptr;           // the hottest engine running on this vehicle
        for (auto& [engine, e] : g_engines)
            if (e.vehicle == vehicle && now - e.time < 0.25 && (!hot || e.sample.temperature_c > hot->sample.temperature_c)) hot = &e;
        o[kOilPressurePsi] = hot ? oil_pressure_psi(hot->oil_level, hot->oil_c, hot->sample.rps * 60.0) : 0.0;
        o[kOilTemperature] = hot && std::isfinite(hot->oil_c) ? hot->oil_c : 0.0;
        o[kOilLevel] = hot ? hot->oil_level : 0.0;
        o[kCoolantTemperature] = hot && std::isfinite(hot->coolant_c) ? hot->coolant_c : 0.0;
        o[kEngineTemperature] = hot ? hot->sample.temperature_c : 0.0;
    }
    if (now - g_last_sweep > 2.0) {                 // as the Dyno's tick: engines gone from the scene stop ticking
        g_last_sweep = now;
        for (auto it = g_engines.begin(); it != g_engines.end();) it = now - it->second.time > 5.0 ? g_engines.erase(it) : std::next(it);
    }
    if (fresh) {
        static std::atomic<int> logs{12};
        if (logs.fetch_sub(1) > 0) log(0, "%s ticking on a vehicle", kind == kController ? "Boost Controller" : kind == kBoostGauge ? "Boost Gauge" : "Oil Gauge");
    }
}

// The ECU's server tick: its tune (panel, then wired inputs) goes to every engine on its vehicle (Turbo's engine
// tick applies it), its boost target to the turbos; its outputs show what the engines did.
void ecu_tick(uint8_t* sensor, void* vehicle) {
    double saved = 1.0;
    bool load = false;
    {
        std::lock_guard lock(g_mutex);
        load = !g_gauges[sensor].panel_loaded;
    }
    if (load) saved = turbo::ecu_saved(vehicle, sensor);   // Turbo's lock, not ours
    ecu::Tune tune;
    double save = 0;
    {
        std::lock_guard lock(g_mutex);
        Gauge& g = g_gauges[sensor];
        double now = now_seconds();
        bool fresh = !(g.last_tick > 0) || now - g.last_tick > 5.0;
        g.kind = gauges::kEcu; g.last_tick = now; g.vehicle = vehicle;
        if (load) {
            g.panel = ecu::unpack(saved); g.panel_loaded = true;
            log(0, "ECU loaded its tune (%s)", saved == 1.0 ? "defaults" : "saved");
        }
        if (g.panel_dirty) { save = ecu::pack(g.panel); g.panel_dirty = false; }
        tune = ecu::effective(g.panel, g.slots);
        if (fresh) { static std::atomic<int> logs{12}; if (logs.fetch_sub(1) > 0) log(0, "ECU ticking on a vehicle"); }
        if (g_vehicle_transform && g_component_transform) {
            double v[12], c[12], t[3];
            g_vehicle_transform(v, vehicle);
            g_component_transform(c, sensor);
            for (int i = 0; i < 3; ++i) t[i] = v[i] * c[9] + v[3 + i] * c[10] + v[6 + i] * c[11] + v[9 + i];
            if (std::isfinite(t[0]) && std::isfinite(t[1]) && std::isfinite(t[2]) && std::abs(t[0]) < 1e7 && std::abs(t[2]) < 1e7) {
                std::memcpy(g.position, t, sizeof t); g.placed = true;
            }
        }
    }
    if (save > 0) turbo::ecu_save(vehicle, sensor, save);
    turbo::set_ecu_tune(vehicle, tune);
    if (tune.boost_psi > 0) turbo::set_boost_target(vehicle, tune.boost_psi);
    turbo::EcuReport r = turbo::ecu_report(vehicle);
    turbo::VehicleBoost boost = turbo::vehicle_boost(vehicle);
    std::lock_guard lock(g_mutex);
    double* o = g_gauges[sensor].slots;
    o[ecu::kOutRpm] = r.rpm; o[ecu::kOutBoostPsi] = std::max(0.0, boost.boost_psi); o[ecu::kOutKnock] = r.knock;
    o[ecu::kOutPowerScale] = r.power_scale; o[ecu::kOutKnockLimit] = r.knock_limit;
}

EngineSample vehicle_sample(const void* vehicle, double now) {
    EngineSample sum;
    sum.boost = 0;
    for (auto& [engine, e] : g_engines) {
        if (e.vehicle != vehicle || now - e.time > 0.25) continue;
        const EngineSample& s = e.sample;
        ++sum.engines;
        sum.watts += std::max(0.0, s.watts);
        sum.rps = std::max(sum.rps, s.rps);
        sum.throttle = std::max(sum.throttle, s.throttle);
        sum.boost = std::max(sum.boost, s.boost);
        sum.temperature_c = std::max(sum.temperature_c, s.temperature_c);
        if (now - e.knock_time < 0.25) sum.knock += e.knock;
    }
    if (sum.engines == 0) sum.boost = 1;
    return sum;
}

void net_host_tick();   // co-op, below
void hk_sensor_tick(uint8_t* sensor, void* vehicle, void* scene) {
    reinterpret_cast<sensor_tick_t>(h_sensor_tick.original)(sensor, vehicle, scene);
    if (!sensor || !vehicle) return;
    g_server_tick_ms.store(now_ms(), std::memory_order_relaxed);
    net_host_tick();
    int kind;
    {
        std::lock_guard lock(g_mutex);
        kind = sensor_kind(sensor);
    }
    if (!kind) return;
    if (kind != kDynoKind) { gauge_tick(sensor, vehicle, gauges::Kind(kind)); return; }
    // The boost setup, and the power superchargers take through their belts: the curves show what reaches the
    // wheels, net of it. Asked before taking the Dyno's lock (Turbo has its own).
    turbo::VehicleBoost boost = turbo::vehicle_boost(vehicle);
    std::lock_guard lock(g_mutex);
    double now = now_seconds();
    g_dyno_tick_ms.store(int64_t(now * 1000), std::memory_order_relaxed);
    Dyno& d = g_dynos[sensor];                      // may already exist: a data channel connected first
    bool fresh = !(d.last_tick > 0) || now - d.last_tick > 5.0;
    double dt = d.last_tick > 0 ? now - d.last_tick : 1.0 / kTicksPerSecond;
    d.last_tick = now;
    bool clear_held = d.slots[kClearRuns] > 0.5;    // microcontroller input: clears once each time it goes high
    if (clear_held && !d.clear_held) {
        d.recorder.clear();
        log(0, "pulls cleared by the data port");
    }
    d.clear_held = clear_held;
    EngineSample s = vehicle_sample(vehicle, now);
    if (s.engines > 0 && boost.drive_watts > 0) s.watts = std::max(0.0, s.watts - boost.drive_watts);
    if (d.setup != boost.setup) {
        if (s.engines > 0) log(0, "boost setup: %s", boost.setup.c_str());
        d.setup = boost.setup;
    }
    if (d.recorder.feed(s, dt)) {
        double w, rpm, nm, nm_rpm;
        d.recorder.latest()->peak_power(w, rpm);
        d.recorder.latest()->peak_torque(nm, nm_rpm);
        log(0, "pull %u kept: %.1f hp at %.0f rpm, %.0f N.m at %.0f rpm, boost %.2f, %.1f s%s (%s)", d.recorder.latest()->number, w / kWattsPerHp, rpm, nm, nm_rpm,
            d.recorder.latest()->boost_peak, d.recorder.latest()->seconds, d.recorder.latest()->knocked() ? ", knocked" : "", d.setup.c_str());
    }
    double clear = d.slots[kClearRuns];
    fill_outputs(d.recorder, d.slots);
    d.slots[kClearRuns] = clear;
    if (g_vehicle_transform && g_component_transform) {
        double v[12], c[12];
        g_vehicle_transform(v, vehicle);
        g_component_transform(c, sensor);
        // world = vehicle * component; only the translation is needed (mat34: three basis columns, then t).
        double t[3];
        for (int i = 0; i < 3; ++i) t[i] = v[i] * c[9] + v[3 + i] * c[10] + v[6 + i] * c[11] + v[9 + i];
        if (std::isfinite(t[0]) && std::isfinite(t[1]) && std::isfinite(t[2]) && std::abs(t[0]) < 1e7 && std::abs(t[2]) < 1e7) {
            std::memcpy(d.position, t, sizeof t);
            d.placed = true;
        }
    }
    if (fresh) {
        static std::atomic<int> logs{12};
        if (logs.fetch_sub(1) > 0) log(0, "Dyno on a vehicle at (%.1f, %.1f, %.1f), %d engine(s) running", d.position[0], d.position[1], d.position[2], s.engines);
    }
    if (now - g_last_sweep > 2.0) {                 // engines gone from the scene stop ticking (Dynos stay: see g_dynos)
        g_last_sweep = now;
        for (auto it = g_engines.begin(); it != g_engines.end();) it = now - it->second.time > 5.0 ? g_engines.erase(it) : std::next(it);
    }
}

}  // namespace

// Turbo's engine tick hook reports knock (hot boost air) here, after the game's tick, whichever of the two hooks sits
// on top of the other. Only noted while a Dyno is ticking.
void note_knock(const void* engine, double knock) {
    if (!engine || !(knock > 0) || !std::isfinite(knock) || !dyno_ticking()) return;
    std::lock_guard lock(g_mutex);
    auto& e = g_engines[engine];
    e.knock = knock; e.knock_time = now_seconds();
}

namespace {

void hk_get_f64(double** ret, uint8_t* sensor, const anymaker::gc_string_view* name) {
    reinterpret_cast<get_f64_t>(h_get_f64.original)(ret, sensor, name);
    if (!ret || *ret || !sensor || !name) return;
    auto view = name->view();
    std::lock_guard lock(g_mutex);
    int kind = sensor_kind(sensor);
    if (kind && kind != kDynoKind) {                // a gauge or the Boost Controller
        int i = gauges::channel_index(gauges::Kind(kind), view.data(), view.size());
        if (i < 0) return;
        *ret = &g_gauges[sensor].slots[i];
        static std::atomic<int> glogs{8};
        if (glogs.fetch_sub(1) > 0) log(0, "gauge data channel %s connected", gauges::channels(gauges::Kind(kind))[i].name);
        return;
    }
    int i = channel_index(view.data(), view.size());
    if (i < 0 || kind != kDynoKind) return;
    *ret = &g_dynos[sensor].slots[i];               // map nodes keep their address
    static std::atomic<int> logs{8};
    if (logs.fetch_sub(1) > 0) log(0, "data channel %s connected", kChannels[i].name);
}

// ------------------------------------------------------------------------------------- screen
const AnyGpuDrawV1* gpu;
const AnyUiStateV1* ui;
const ModControlsV1* controls;
uint64_t g_action;
uint64_t g_ecu_action;                                // the ECU panel's key (F9 by default)
// The ECU panel (ecu_logic.h): open on its key near an ECU; Up/Down pick a row, Left/Right change it (Shift: 4 steps).
std::atomic<bool> g_ecu_open{false};
std::atomic<int> g_ecu_row{0};
std::atomic<bool> g_ecu_near{false};                   // an ECU within reach (render thread writes, input reads)
constexpr double kEcuReach = 8.0;
std::mutex g_input_mutex;
bool g_hidden = false, g_key_down = false, g_cleared_this_hold = false;   // under g_input_mutex
std::atomic<bool> g_near{false};                      // a Dyno's graph is in range (render thread writes, input reads)
double g_key_down_at = 0;
constexpr double kShowDistance = 40.0;

uint32_t toggle_key() {
    if (controls && g_action) return controls->key(g_action);
    return VK_F6;
}
bool gameplay() {
    AnyUiSnapshotV1 s;
    return ui && ui->copy(&s) && s.kind == ANY_UI_GAMEPLAY;
}

void ecu_nudge(int steps);
uint32_t ecu_key();
uint32_t ecu_input(const AnyInputV1* e) {
    if (e->kind != ANY_KEY_DOWN && e->kind != ANY_KEY_UP) return 0;
    uint32_t key = ecu_key();
    if (key && e->key == key) {
        if (e->kind == ANY_KEY_DOWN && (g_ecu_near || g_ecu_open)) { g_ecu_open = !g_ecu_open; return 1; }
        return g_ecu_open ? 1 : 0;
    }
    if (!g_ecu_open) return 0;
    bool arrow = e->key == VK_UP || e->key == VK_DOWN || e->key == VK_LEFT || e->key == VK_RIGHT;
    if (!arrow) return 0;
    if (e->kind == ANY_KEY_DOWN) {
        int steps = (GetKeyState(VK_SHIFT) & 0x8000) ? 4 : 1;
        if (e->key == VK_UP) g_ecu_row = (g_ecu_row.load() + ecu::kRowCount - 1) % ecu::kRowCount;
        else if (e->key == VK_DOWN) g_ecu_row = (g_ecu_row.load() + 1) % ecu::kRowCount;
        else ecu_nudge(e->key == VK_LEFT ? -steps : steps);
    }
    return 1;
}

uint32_t input(const AnyInputV1* e, void*) {
    if (!e) return 0;
    if (g_ready && e->kind != ANY_FOCUS_LOST && ecu_input(e)) return 1;
    std::lock_guard lock(g_input_mutex);
    if (e->kind == ANY_FOCUS_LOST) { g_key_down = false; return 0; }
    uint32_t key = toggle_key();
    if (!key || e->key != key || !g_ready) return 0;
    if (e->kind == ANY_KEY_DOWN) {
        if (!g_near || !gameplay()) return 0;
        if (!g_key_down) { g_key_down = true; g_key_down_at = now_seconds(); g_cleared_this_hold = false; }
        return 1;
    }
    if (e->kind == ANY_KEY_UP && g_key_down) {
        g_key_down = false;
        if (!g_cleared_this_hold) g_hidden = !g_hidden;
        return 1;
    }
    return 0;
}

// A copy of what the panel shows, taken under the lock.
struct View { Recorder recorder; std::string setup; double distance = -1; };

// ------------------------------------------------------------------------------------- co-op (dyno_net.h)
// The host sends each joiner the Dyno graphs and ECUs near him; a joiner keeps them in g_dynos / g_gauges under the
// host's keys (never dereferenced: only his screens read them) and sends his ECU changes and Dyno clears back.
steam_messages::Line g_line;                          // under g_net_mutex
std::mutex g_net_mutex;
struct Remote { int32_t peer_id; uint64_t steam; double position[3]; bool has_position; };
std::vector<Remote> remote_players(AnySessionPlayersV1& buffer) {
    std::vector<Remote> out;
    buffer = AnySessionPlayersV1{};
    buffer.struct_size = sizeof buffer;
    uint32_t flags = 0;
    if (!host.copy_players || !host.copy_players(&buffer, &flags)) return out;
    for (uint32_t i = 0; i < buffer.count && i < ANYAPI_PLAYERS_CAPACITY; ++i) {
        const auto& p = buffer.players[i];
        if ((p.valid_fields & PLAYER_LOCAL) || !(p.valid_fields & PLAYER_STEAM_ID)) continue;
        char id[33]{};
        std::memcpy(id, p.steam_id, sizeof p.steam_id < 32 ? sizeof p.steam_id : 32);
        uint64_t steam = dyno_net::parse_steam_id(id);
        if (!steam) continue;
        Remote r{p.peer_id, steam, {}, (p.valid_fields & PLAYER_POSITION) != 0};
        if (r.has_position) std::memcpy(r.position, p.position, sizeof r.position);
        out.push_back(r);
    }
    return out;
}

// Host, server thread: requests in, then each joiner's snapshot out, four times a second.
void net_host_tick() {
    static std::atomic<int64_t> last{0};
    const int64_t now = now_ms();
    int64_t was = last.load(std::memory_order_relaxed);
    if (now - was < 250 || !last.compare_exchange_strong(was, now)) return;
    static AnySessionPlayersV1 roster_buffer;         // server thread only (guarded by the exchange above)
    std::vector<Remote> roster = remote_players(roster_buffer);
    if (roster.empty()) return;
    std::lock_guard net_lock(g_net_mutex);
    if (!g_line.ready(dyno_net::kChannel)) return;
    static uint8_t in[4096];
    for (int i = 0; i < 32; ++i) {
        uint32_t size = 0;
        uint64_t sender = 0;
        int got = g_line.receive(in, sizeof in, size, sender);
        if (got < 0) { log(2, "co-op: Steam call fault; joiners' Dyno and ECU screens are off this session"); return; }
        if (!got) break;
        const Remote* from = nullptr;
        for (const auto& r : roster) if (sender && r.steam == sender) from = &r;
        dyno_net::Request req;
        static int drops = 0;
        if (!from || size > sizeof in || !dyno_net::decode(in, size, req)) { if (drops++ < 10) log(1, "co-op: dropped a message (%s)", from ? "bad format" : "not a player in this world"); continue; }
        std::lock_guard lock(g_mutex);
        const auto* key = reinterpret_cast<const uint8_t*>(uintptr_t(req.key));
        if (req.kind == dyno_net::kSetTune) {
            auto g = g_gauges.find(key);
            if (g == g_gauges.end() || g->second.kind != gauges::kEcu || !g->second.placed) continue;
            if (from->has_position && dyno_net::distance(from->position, g->second.position) > kEcuReach + dyno_net::kReachSlack) continue;
            g->second.panel = dyno_net::clamp_tune(req.panel);
            g->second.panel_dirty = true;
            log(0, "ECU panel changed by a joiner (peer %d)", from->peer_id);
        } else {
            auto d = g_dynos.find(key);
            if (d == g_dynos.end()) continue;
            if (from->has_position && d->second.placed && dyno_net::distance(from->position, d->second.position) > kShowDistance + dyno_net::kReachSlack) continue;
            d->second.recorder.clear();
            log(0, "pulls cleared by a joiner (peer %d)", from->peer_id);
        }
    }
    const double t = now_seconds();
    for (const auto& r : roster) {
        if (!r.has_position) continue;
        dyno_net::Snapshot snap;
        {
            std::lock_guard lock(g_mutex);
            for (const auto& [sensor, d] : g_dynos) {
                if (t - d.last_tick > 1.0 || !d.placed || dyno_net::distance(r.position, d.position) > dyno_net::kNearJoiner) continue;
                if (int(snap.dynos.size()) >= dyno_net::kMaxDynos) break;
                dyno_net::DynoEntry e;
                e.key = uint64_t(uintptr_t(sensor));
                std::memcpy(e.position, d.position, sizeof e.position);
                std::snprintf(e.setup, sizeof e.setup, "%s", d.setup.c_str());
                e.recorder = d.recorder;
                snap.dynos.push_back(e);
            }
            for (const auto& [sensor, g] : g_gauges) {
                if (g.kind != gauges::kEcu || t - g.last_tick > 1.0 || !g.placed || dyno_net::distance(r.position, g.position) > dyno_net::kNearJoiner) continue;
                if (int(snap.ecus.size()) >= dyno_net::kMaxEcus) break;
                dyno_net::EcuEntry e;
                e.key = uint64_t(uintptr_t(sensor));
                std::memcpy(e.position, g.position, sizeof e.position);
                e.panel = g.panel;
                std::memcpy(e.slots, g.slots, sizeof e.slots);
                snap.ecus.push_back(e);
            }
        }
        if (snap.dynos.empty() && snap.ecus.empty()) continue;
        std::vector<uint8_t> bytes = dyno_net::encode(snap);
        int32_t sent = g_line.send(r.steam, bytes.data(), uint32_t(bytes.size()));
        static std::atomic<int> logs{4};
        if (logs.fetch_sub(1) > 0) log(sent == 1 ? 0 : 1, "co-op: sent peer %d %d Dyno(s) and %d ECU(s), %u bytes (Steam result %d)", r.peer_id,
                                      int(snap.dynos.size()), int(snap.ecus.size()), unsigned(bytes.size()), sent);
    }
}

// Joiner, render thread: the host's snapshots in. ECU rows the joiner just changed keep his value for a moment, so
// a snapshot sent before the host had his change does not flick it back.
uint64_t g_host_steam = 0;
std::unordered_map<uint64_t, double> g_tune_pending;   // key -> until (now_seconds), under g_mutex
void joiner_send(const dyno_net::Request& r) {
    std::vector<uint8_t> bytes = dyno_net::encode(r);
    std::lock_guard lock(g_net_mutex);
    if (g_host_steam && g_line.ready(dyno_net::kChannel)) g_line.send(g_host_steam, bytes.data(), uint32_t(bytes.size()));
}
void net_joiner_tick() {
    static AnySessionPlayersV1 roster_buffer;          // render thread only
    static int64_t roster_at = 0;
    const int64_t now = now_ms();
    if (now - roster_at >= 1000) {
        roster_at = now;
        uint64_t host_steam = 0;
        int32_t lowest = INT32_MAX;                    // the host joined first: the lowest peer id
        for (const auto& r : remote_players(roster_buffer)) if (r.peer_id < lowest) { lowest = r.peer_id; host_steam = r.steam; }
        if (host_steam != g_host_steam) { g_host_steam = host_steam; if (host_steam) log(0, "co-op: joined a host; his Dyno and ECU screens come from him"); }
    }
    if (!g_host_steam) return;
    static std::vector<uint8_t> buffer(64 * 1024);
    for (int i = 0; i < 8; ++i) {
        uint32_t size = 0;
        uint64_t sender = 0;
        int got;
        {
            std::lock_guard lock(g_net_mutex);
            got = g_line.receive(buffer.data(), uint32_t(buffer.size()), size, sender);
        }
        if (got <= 0) break;
        dyno_net::Snapshot snap;
        if (sender != g_host_steam || size > buffer.size() || !dyno_net::decode(buffer.data(), size, snap)) continue;
        static bool first = true;
        if (first) { first = false; log(0, "co-op: first screens from the host (%d Dyno(s), %d ECU(s))", int(snap.dynos.size()), int(snap.ecus.size())); }
        const double t = now_seconds();
        std::lock_guard lock(g_mutex);
        for (const auto& e : snap.dynos) {
            Dyno& d = g_dynos[reinterpret_cast<const uint8_t*>(uintptr_t(e.key))];
            d.recorder = e.recorder; d.setup = e.setup;
            std::memcpy(d.position, e.position, sizeof d.position);
            d.placed = true; d.last_tick = t;
        }
        for (const auto& e : snap.ecus) {
            Gauge& g = g_gauges[reinterpret_cast<const uint8_t*>(uintptr_t(e.key))];
            g.kind = gauges::kEcu; g.panel_loaded = true; g.last_tick = t;
            auto pending = g_tune_pending.find(e.key);
            if (pending == g_tune_pending.end() || t > pending->second) g.panel = e.panel;
            std::memcpy(g.slots, e.slots, sizeof g.slots);
            std::memcpy(g.position, e.position, sizeof g.position);
            g.placed = true;
        }
    }
}

bool local_player(double pos[3]) {
    if (!host.copy_players) return false;
    static AnySessionPlayersV1 players;            // render thread only
    uint32_t flags = 0;
    players.struct_size = sizeof players;
    if (!host.copy_players(&players, &flags)) return false;
    for (uint32_t i = 0; i < players.count && i < ANYAPI_PLAYERS_CAPACITY; ++i) {
        const auto& p = players.players[i];
        if ((p.valid_fields & PLAYER_LOCAL) && (p.valid_fields & PLAYER_POSITION)) { std::memcpy(pos, p.position, sizeof p.position); return true; }
    }
    return false;
}

uint64_t g_cleared_key = 0;                           // the Dyno the last clear was for (render thread)
// The nearest Dyno that ticked in the last second, within kShowDistance of the local player. Without a player
// position (should not happen in a world) the most recently ticked one.
bool pick(View& out, bool clear) {
    double player[3];
    bool have_player = local_player(player);
    double now = now_seconds();
    std::lock_guard lock(g_mutex);
    Dyno* best = nullptr;
    double best_d = 1e30;
    for (auto& [sensor, d] : g_dynos) {
        if (now - d.last_tick > 1.0) continue;
        double dist = 0;
        if (have_player && d.placed) {
            double dx = d.position[0] - player[0], dy = d.position[1] - player[1], dz = d.position[2] - player[2];
            dist = std::sqrt(dx * dx + dy * dy + dz * dz);
        } else dist = -(d.last_tick);              // most recent first
        if (dist < best_d) { best_d = dist; best = &d; }
    }
    static std::atomic<int> logs{3};
    if (best && have_player && logs.fetch_sub(1) > 0) log(0, "nearest Dyno %.1f m from the player", best_d);
    if (!best || (have_player && best->placed && best_d > kShowDistance)) return false;
    if (clear) {
        best->recorder.clear();
        log(0, "pulls cleared");
        for (const auto& [key, dyno] : g_dynos) if (&dyno == best) g_cleared_key = uint64_t(uintptr_t(key));
    }
    out.recorder = best->recorder;
    out.setup = best->setup;
    out.distance = have_player ? best_d : -1;
    return true;
}

struct Painter {
    float s = 1;
    void rect(float x, float y, float w, float h, uint32_t color, float radius = 0, uint32_t flags = 0, float stroke = 1) const {
        AnyGpuCommandV1 c; c.kind = radius > 0 ? ANY_GPU_ROUND_RECT : ANY_GPU_RECT; c.rect[0] = x; c.rect[1] = y; c.rect[2] = w; c.rect[3] = h;
        c.color = color; c.radius = radius; c.flags = flags; c.stroke = stroke; gpu->emit(&c);
    }
    void line(float x0, float y0, float x1, float y1, uint32_t color, float width) const {
        AnyGpuCommandV1 c; c.kind = ANY_GPU_LINE; c.rect[0] = x0; c.rect[1] = y0; c.rect[2] = x1; c.rect[3] = y1;
        c.color = color; c.stroke = width; gpu->emit(&c);
    }
    void text(float x, float y, float w, float h, const std::wstring& t, float size, uint32_t color, uint32_t flags = 0) const {
        AnyGpuCommandV1 c; c.kind = ANY_GPU_TEXT; c.rect[0] = x; c.rect[1] = y; c.rect[2] = w; c.rect[3] = h;
        c.text = t.c_str(); c.text_length = uint32_t(t.size()); c.font_size = size; c.color = color;
        c.flags = flags | ANY_GPU_NOWRAP | ANY_GPU_VCENTER; gpu->emit(&c);
    }
};
std::wstring wide(const std::string& s) {           // our strings are Latin-1 (· × °)
    std::wstring w;
    for (char c : s) w += wchar_t(static_cast<unsigned char>(c));
    return w;
}

uint32_t ecu_key() {
    if (controls && g_ecu_action) return controls->key(g_ecu_action);
    return ecu::kPanelKeyDefault;
}
// The nearest ECU that ticked in the last second, within kEcuReach of the local player (under g_mutex).
Gauge* nearest_ecu(const double player[3], bool have_player, double* distance) {
    double now = now_seconds(), best_d = 1e30;
    Gauge* best = nullptr;
    for (auto& [sensor, g] : g_gauges) {
        if (g.kind != gauges::kEcu || now - g.last_tick > 1.0) continue;
        double d = 0;
        if (have_player && g.placed) {
            double dx = g.position[0] - player[0], dy = g.position[1] - player[1], dz = g.position[2] - player[2];
            d = std::sqrt(dx * dx + dy * dy + dz * dz);
        }
        if (d < best_d) { best_d = d; best = &g; }
    }
    if (distance) *distance = best_d;
    return best && (!have_player || !best->placed || best_d <= kEcuReach) ? best : nullptr;
}
void ecu_nudge(int steps) {
    double player[3];
    bool have = local_player(player);
    dyno_net::Request to_host;
    bool send = false;
    {
        std::lock_guard lock(g_mutex);
        Gauge* g = nearest_ecu(player, have, nullptr);
        if (!g) return;
        int row = std::clamp(g_ecu_row.load(), 0, ecu::kRowCount - 1);
        double before = ecu::get(g->panel, row);
        ecu::nudge(g->panel, row, steps);
        if (ecu::get(g->panel, row) != before) {
            g->panel_dirty = true;
            log(0, "ECU panel: %s %s", ecu::kRows[row].label, ecu::row_text(g->panel, row).c_str());
            if (!hosting()) {                         // a joiner: the host's ECU takes the change
                for (const auto& [key, gauge] : g_gauges)
                    if (&gauge == g) { to_host.kind = dyno_net::kSetTune; to_host.key = uint64_t(uintptr_t(key)); to_host.panel = g->panel; send = true; }
                if (send) g_tune_pending[to_host.key] = now_seconds() + 1.5;
            }
        }
    }
    if (send) joiner_send(to_host);
}
void ecu_draw(const AnyFrameV1* f) {
    double player[3];
    bool have = local_player(player);
    ecu::Tune panel, live;
    bool wired[ecu::kRowCount]{};
    double out[5]{};
    {
        std::lock_guard lock(g_mutex);
        Gauge* g = nearest_ecu(player, have, nullptr);
        g_ecu_near = g != nullptr;
        if (!g) { g_ecu_open = false; return; }
        if (!g_ecu_open) return;
        panel = g->panel;
        live = ecu::effective(g->panel, g->slots);
        for (int r = 0; r < ecu::kRowCount; ++r) wired[r] = std::isfinite(g->slots[r]) && g->slots[r] != 0.0;
        for (int i = 0; i < 5; ++i) out[i] = g->slots[ecu::kOutRpm + i];
    }
    Painter p;
    p.s = std::max(0.75f, float(f->height) / 1080.f);
    float s = p.s, row_h = 30 * s, W = 430 * s, H = 74 * s + row_h * ecu::kRowCount + 58 * s;
    float x = 24 * s, y = std::max(8 * s, float(f->height) * .5f - H * .5f);
    p.rect(x, y, W, H, 0xf0161b21, 8 * s);
    p.text(x + 16 * s, y + 10 * s, W, 20 * s, L"ECU TUNING", 12 * s, 0xffaeb7c2, ANY_GPU_BOLD);
    char key_name[32] = "F9";
    if (controls && g_ecu_action && controls->key_name) controls->key_name(g_ecu_action, key_name, sizeof key_name);
    p.text(x + 16 * s, y + 32 * s, W - 32 * s, 18 * s, wide(std::string("Up/Down: row   Left/Right: change (Shift x4)   ") + key_name + ": close"), 11 * s, 0xff8c96a2);
    int sel = std::clamp(g_ecu_row.load(), 0, ecu::kRowCount - 1);
    float ry = y + 60 * s;
    for (int r = 0; r < ecu::kRowCount; ++r, ry += row_h) {
        if (r == sel) p.rect(x + 8 * s, ry, W - 16 * s, row_h - 2 * s, 0xff3a4048, 4 * s);
        p.text(x + 20 * s, ry, 200 * s, row_h, wide(ecu::kRows[r].label), 14 * s, r == sel ? 0xffedc56c : 0xffdfe4ea, r == sel ? ANY_GPU_BOLD : 0);
        std::string v = ecu::row_text(panel, r);
        if (wired[r]) v = ecu::row_text(live, r) + "  (wired)";
        p.text(x + 220 * s, ry, W - 236 * s, row_h, wide(v), 14 * s, wired[r] ? 0xff7fc6ff : 0xfff5f7fa);
    }
    char line[160];
    std::snprintf(line, sizeof line, "%.0f rpm   %.1f psi   knock %.0f%%   power x%.2f   knock limit %.2f", out[0], out[1], out[2] * 100, out[3], out[4]);
    p.text(x + 16 * s, ry + 8 * s, W - 32 * s, 20 * s, wide(line), 12 * s, out[2] > 0.001 ? 0xffe05050 : 0xffaab4bd);
}

void draw(const AnyFrameV1* f, void*) {
    if (g_ready.load() && !hosting()) net_joiner_tick();
    if (f && gpu && f->focused && g_ready.load() && gameplay()) ecu_draw(f);
    else g_ecu_near = false;
    if (!f || !gpu || !f->focused || !g_ready.load() || !gameplay()) { g_near = false; return; }
    bool clear = false;
    bool hidden;
    {
        std::lock_guard lock(g_input_mutex);
        if (g_key_down && !g_cleared_this_hold && now_seconds() - g_key_down_at > 1.0) { g_cleared_this_hold = true; clear = true; g_hidden = false; }
        hidden = g_hidden;
    }
    static View view;                               // render thread only
    g_cleared_key = 0;
    bool shown = pick(view, clear);
    if (clear && g_cleared_key && !hosting()) { dyno_net::Request r; r.kind = dyno_net::kClearDyno; r.key = g_cleared_key; joiner_send(r); }
    g_near = shown;
    if (!shown || hidden) return;

    Painter p;
    p.s = std::max(0.75f, float(f->height) / 1080.f);
    float s = p.s;
    const float W = 600 * s, H = 310 * s, x = float(f->width) - W - 24 * s, y = 110 * s;
    const uint32_t panel = 0xe0161b21, grid = 0x40ffffff, light = 0xffe8ecef, dim = 0xffaab4bd, red = 0xffe05050;
    const uint32_t colors[kRunsKept] = {0xff8a96a3, 0xff4a9eff, 0xffff7a2a};
    p.rect(x, y, W, H, panel, 8 * s);
    const Recorder& r = view.recorder;

    // Header: title and live readout.
    const EngineSample& live = r.live;
    double rpm_now = live.rps * 60;
    p.text(x + 14 * s, y + 6 * s, 80 * s, 26 * s, L"Dyno", 18 * s, 0xffff7a2a, ANY_GPU_BOLD);
    std::string readout = live.engines == 0 ? "No engine running" :
        thousands(rpm_now) + " rpm   " + fmt("%.1f hp", live.watts / kWattsPerHp) + "   " + fmt("%.0f N\xB7m", torque_nm(live.watts, live.rps)) + "   boost " + fmt("%.2f\xD7", live.boost);
    p.text(x + 80 * s, y + 6 * s, 330 * s, 26 * s, wide(readout), 14 * s, light);
    if (live.engines > 0 && !view.setup.empty()) p.text(x + 80 * s, y + 30 * s, 330 * s, 14 * s, wide(view.setup), 11 * s, dim);
    if (r.active) p.text(x + 410 * s, y + 6 * s, 60 * s, 26 * s, L"REC", 13 * s, red, ANY_GPU_BOLD);

    // Graph area.
    const float gx = x + 46 * s, gy = y + 54 * s, gw = 372 * s, gh = 204 * s;
    double max_w = 1, max_nm = 1;
    auto consider = [&](const Run& run) {
        for (int b = run.lo; b <= run.hi; ++b) if (run.has(b)) {
            max_w = std::max(max_w, run.watts(b));
            if (b > 0) max_nm = std::max(max_nm, torque_nm(run.watts(b), Run::bin_rpm(b) / 60.0));
        }
    };
    for (int i = 0; i < r.count; ++i) consider(r.runs[i]);
    if (r.active) consider(r.current);
    double hp_top, hp_step, nm_top, nm_step;
    nice_axis(max_w / kWattsPerHp, hp_top, hp_step);
    nice_axis(max_nm, nm_top, nm_step);
    const double rpm_lo = 0, rpm_hi = 8500;
    auto px = [&](double rpm) { return gx + float((rpm - rpm_lo) / (rpm_hi - rpm_lo)) * gw; };
    auto py_hp = [&](double hp) { return gy + gh - float(hp / hp_top) * gh; };
    auto py_nm = [&](double nm) { return gy + gh - float(nm / nm_top) * gh; };
    for (double v = 0; v <= hp_top + 1e-9; v += hp_step) {
        float yy = py_hp(v);
        p.line(gx, yy, gx + gw, yy, grid, 1);
        p.text(x + 4 * s, yy - 8 * s, 38 * s, 16 * s, wide(fmt(hp_step < 1 ? "%.1f" : "%.0f", v)), 11 * s, dim, ANY_GPU_CENTER);
        if (v > 0) {                                // torque axis labels on the right, on the same grid lines
            double nm = nm_top * v / hp_top;
            p.text(gx + gw + 2 * s, yy - 8 * s, 34 * s, 16 * s, wide(fmt(nm < 10 ? "%.1f" : "%.0f", nm)), 11 * s, dim, ANY_GPU_CENTER);
        }
    }
    for (int k = 1; k <= 8; ++k) {
        float xx = px(k * 1000.0);
        p.line(xx, gy, xx, gy + gh, grid, 1);
        p.text(xx - 16 * s, gy + gh + 2 * s, 32 * s, 16 * s, wide(fmt("%.0fk", double(k))), 11 * s, dim, ANY_GPU_CENTER);
    }
    p.text(x + 4 * s, gy - 18 * s, 60 * s, 14 * s, L"hp", 11 * s, dim, ANY_GPU_CENTER);
    p.text(gx + gw, gy - 18 * s, 40 * s, 14 * s, L"N\xB7m", 11 * s, dim, ANY_GPU_CENTER);
    p.line(px(8000), gy, px(8000), gy + gh, red, 1.5f * s);

    auto curves = [&](const Run& run, uint32_t color, float width) {
        float lx = 0, ly = 0, tx = 0, ty = 0;
        bool have = false;
        for (int b = run.lo; b <= run.hi; ++b) {
            if (!run.has(b) || b == 0) { have = false; continue; }
            double w = run.watts(b), rpm = Run::bin_rpm(b);
            float cx = px(rpm), cy = py_hp(w / kWattsPerHp), ny = py_nm(torque_nm(w, rpm / 60.0));
            if (have) {
                p.line(lx, ly, cx, cy, color, width);
                if ((b / 2) % 2 == 0) p.line(tx, ty, cx, ny, color, width * 0.75f);   // torque dashed: 200 RPM on, 200 off
            }
            lx = cx; ly = cy; tx = cx; ty = ny; have = true;
        }
    };
    for (int i = 0; i < r.count; ++i) curves(r.runs[i], colors[kRunsKept - r.count + i], (i == r.count - 1 && !r.active ? 3.f : 2.f) * s);
    if (r.active) curves(r.current, 0xffffc878, 3 * s);
    if (live.engines > 0 && rpm_now > 0) p.line(px(std::min(rpm_now, rpm_hi)), gy, px(std::min(rpm_now, rpm_hi)), gy + gh, 0x80ffffff, 1 * s);

    // Side panel: each kept pull's peaks, then the latest pull's details.
    float sx = x + 462 * s, sy = y + 46 * s;
    p.text(sx, sy, 130 * s, 18 * s, L"Peak power", 13 * s, light, ANY_GPU_BOLD);
    sy += 22 * s;
    if (r.count == 0) {
        p.text(sx, sy, 130 * s, 16 * s, L"No pulls yet", 12 * s, dim);
        sy += 18 * s;
    }
    for (int i = r.count - 1; i >= 0; --i) {
        const Run& run = r.runs[i];
        double w, rpm;
        run.peak_power(w, rpm);
        p.rect(sx, sy + 4 * s, 10 * s, 10 * s, colors[kRunsKept - r.count + i]);
        p.text(sx + 16 * s, sy, 120 * s, 18 * s, wide("Pull " + std::to_string(run.number) + fmt("  %.1f hp", w / kWattsPerHp)), 12 * s, light);
        p.text(sx + 16 * s, sy + 16 * s, 120 * s, 14 * s, wide("at " + thousands(rpm) + " rpm"), 11 * s, dim);
        sy += 32 * s;
    }
    if (const Run* last = r.latest()) {
        sy += 4 * s;
        double nm, nm_rpm;
        last->peak_torque(nm, nm_rpm);
        p.text(sx, sy, 130 * s, 18 * s, L"Latest pull", 13 * s, light, ANY_GPU_BOLD);
        std::string rows[] = {fmt("Torque %.0f N\xB7m", nm) + " @ " + thousands(nm_rpm), fmt("Boost peak %.2f\xD7", last->boost_peak),
                              fmt("Engine %.0f \xB0" "C", last->temperature_peak), last->knocked() ? "Knock: yes" : "Knock: none"};
        for (auto& row : rows) {
            sy += 17 * s;
            p.text(sx, sy, 136 * s, 16 * s, wide(row), 11 * s, last->knocked() && &row == &rows[3] ? red : dim);
        }
    }

    // Footer hint.
    char key_name[32] = "F6";
    if (controls && g_action && controls->key_name) controls->key_name(g_action, key_name, sizeof key_name);
    std::string hint = std::string("Floor it to record a pull.   ") + key_name + " hides, hold " + key_name + " to clear.   Solid: power, dashed: torque";
    p.text(x + 14 * s, y + H - 26 * s, W - 28 * s, 20 * s, wide(hint), 11 * s, dim);
}

// ------------------------------------------------------------------------------------- install (background thread)
// The add cell must hold _add_definitions or another hook on it (Turbo's and Rotary's live in this DLL, GpsPart's in
// its own), never container.load, and the load function must be file.load (route::add_cell_ok and
// route::file_load_ok in route_cell.h).
void install_hooks() {
    using namespace anymaker::experimental;
    void **add = nullptr, **load_cell = nullptr, **engine = nullptr, **tick = nullptr, **get = nullptr;
    for (int attempt = 0; attempt < 180 && !g_stop; ++attempt) {
        if (attempt) Sleep(attempt < 30 ? 1000 : 5000);
        if (!matching_build()) { if (attempt == 0) log(1, "game build does not match the SDK reference; Dyno stays off"); return; }
        if (!add) add = route::cell(bind::definitions_add_definitions);
        if (add && !route::add_cell_ok(add)) add = nullptr;
        if (!g_file_ctor) g_file_ctor = reinterpret_cast<self_fn_t>(function(bind::definition_file_ctor));
        if (!g_file_dtor) g_file_dtor = reinterpret_cast<self_fn_t>(function(bind::definition_file_dtor));
        if (!load_cell) load_cell = route::cell(bind::definition_file_load);
        if (!g_file_load && load_cell) {
            if (route::file_load_ok(*load_cell)) g_file_load = reinterpret_cast<file_load_t>(*load_cell);
            else load_cell = nullptr;            // a wrong route: look again next time
        }
        if (!g_get_definition) g_get_definition = reinterpret_cast<get_definition_t>(function(bind::definitions_get_by_id));
        if (!g_string_ctor) g_string_ctor = reinterpret_cast<string_ctor_t>(native(anymaker::sym::string_ctor_cstr_rva));
        if (!engine) {                           // the game's engine tick, or Turbo's hook on it (hot boost air), to chain onto
            void** c = route::cell(bind::engine_tick); void* entry = route::function(bind::engine_tick);
            if (c && entry && anymaker::readable(c, 8) && (*c == entry || route::in_other_module(*c))) engine = c;
        }
        if (!tick) tick = hook_cell(bind::altitude_tick);
        if (!get) get = hook_cell(bind::altitude_get_data_f64);
        if (!g_vehicle_transform) g_vehicle_transform = reinterpret_cast<transform_t>(function(bind::server_vehicle_get_transform));
        if (!g_component_transform) g_component_transform = reinterpret_cast<transform_t>(function(bind::server_component_get_transform));
        // The definition lookup is needed too: without it the part would be added again on every definitions add.
        bool core = add && g_file_ctor && g_file_dtor && g_file_load && g_string_ctor && g_get_definition && engine && tick && get;
        if (!core) {
            if (attempt == 5 || attempt == 179)
                log(attempt == 179 ? 2 : 1, "%s (add=%d file=%d/%d/%d string=%d lookup=%d engine=%d sensor=%d/%d)", attempt == 179 ? "could not locate the game functions; Dyno is off" : "still looking for the game functions",
                    !!add, !!g_file_ctor, !!g_file_dtor, !!g_file_load, !!g_string_ctor, !!g_get_definition, !!engine, !!tick, !!get);
            continue;
        }
        std::lock_guard install_lock(engine_parts::install_mutex());   // one AnyEngineParts part hooks at a time
        if (g_stop) return;                          // shut down while it was looking
        if (!g_vehicle_transform || !g_component_transform) log(1, "transforms not found; the graph shows for the most recent Dyno, wherever it is");
        if (!write_definition_file()) { log(2, "could not write the Dyno definition file; Dyno is off"); return; }
        if (!h_engine_tick.install(engine, (void*)&hk_engine_tick)) { log(2, "engine tick hook not installed; Dyno is off"); return; }
        if (!h_get_f64.install(get, (void*)&hk_get_f64) || !h_sensor_tick.install(tick, (void*)&hk_sensor_tick)) {
            h_get_f64.remove((void*)&hk_get_f64); h_engine_tick.remove((void*)&hk_engine_tick);
            log(2, "sensor hooks not installed; Dyno is off");
            return;
        }
        if (!route::add_cell_ok(add) || !h_add_definitions.install(add, (void*)&hk_add_definitions)) {
            h_sensor_tick.remove((void*)&hk_sensor_tick); h_get_f64.remove((void*)&hk_get_f64); h_engine_tick.remove((void*)&hk_engine_tick);
            log(2, "the definitions hook cell was taken or changed; Dyno is off");
            return;
        }
        g_ready = true;
        log(0, "Ready after %d s. Load or start a world to add the Dyno part (definition %s).", attempt, g_json_path.c_str());
        return;
    }
}
void remove_hooks() {
    h_add_definitions.remove((void*)&hk_add_definitions);
    h_sensor_tick.remove((void*)&hk_sensor_tick);
    h_get_f64.remove((void*)&hk_get_f64);
    h_engine_tick.remove((void*)&hk_engine_tick);
}
}  // namespace

// AnyEngineParts (engine_parts_mod.cpp) is the DLL; it starts this part of it.
void init(const AnyModHostV1& h) { host = h; }
uint32_t on_input(const AnyInputV1* e, void* user) { return input(e, user); }
void ready() {
    auto services = AnyAPI_Services();
    if (!services) return;
    ui = static_cast<const AnyUiStateV1*>(services->query("anyapi.ui_state", 1));
    if (ui && (ui->struct_size != sizeof(*ui) || ui->version != 1 || !ui->copy)) ui = nullptr;
    controls = static_cast<const ModControlsV1*>(services->query("anyhelpers.controls", 1));
    if (controls && controls->struct_size == sizeof(*controls) && controls->version == 1 && controls->register_action && controls->key) {
        ModControlActionV1 d; d.mod_id = "anyengineparts"; d.mod_name = "AnyEngineParts"; d.action_id = "dyno_toggle";
        d.label = "Show / hide dyno (hold: clear pulls)"; d.default_key = VK_F6;
        g_action = controls->register_action(&d);
        ModControlActionV1 e; e.mod_id = "anyengineparts"; e.mod_name = "AnyVehicleOverhaul"; e.action_id = "ecu_panel";
        e.label = "ECU tuning panel (near an ECU)"; e.default_key = ecu::kPanelKeyDefault;
        g_ecu_action = controls->register_action(&e);
    } else controls = nullptr;
    gpu = static_cast<const AnyGpuDrawV1*>(services->query("anyapi.gpu_draw", 1));
    if (gpu && (gpu->struct_size != sizeof(*gpu) || gpu->version != 1 || !gpu->register_renderer || !gpu->emit)) gpu = nullptr;
    if (!ui || !gpu || !gpu->register_renderer(draw, nullptr)) { gpu = nullptr; log(1, "gpu_draw or ui_state unavailable; the Dyno part works but shows no graph"); }
}
void start() { install_hooks(); }       // on AnyEngineParts' install thread
void shutdown() {
    g_stop = true;
    std::lock_guard install_lock(engine_parts::install_mutex());   // not while a hook install is under way
    remove_hooks();
}
}  // namespace dyno
