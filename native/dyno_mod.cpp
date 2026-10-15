// Dyno: a "Dyno" sensor part that records engine power and torque curves and draws them on screen.
//
// Put the part anywhere on a vehicle. Hold the throttle at 90 % or more for a pull: the engines' power (the energy the
// game's engine tick turns into motion, x60) is binned by RPM, and when the throttle comes off a pull of at least
// 1,500 RPM is kept. The last three pulls are drawn over each other (oldest grey, then blue, newest orange), with
// power solid and torque dashed, beside each pull's peaks, the latest pull's boost, engine temperature and knock.
// The graph shows while the local player is within 40 m of a vehicle carrying a Dyno; the "Show / hide dyno" key
// (F6, rebindable in Controls) hides it, and holding that key for a second clears the pulls. The part's data port
// gives rpm, power, torque, boost, temperature and the latest peak to microcontrollers (dyno_logic.h).
//
// How: the part is the game's altitude sensor class (dyno_logic.h; nothing else hooks that class), added to the
// game's part definitions like the Rotary. The server engine tick is hooked to note each engine's numbers, and the
// altitude sensor's server tick (for Dyno parts only) sums its vehicle's engines and feeds the recorder. Rendering
// uses anyapi.gpu_draw. Single player / host: the engines and the part tick on the server of this game.
//
// EXPERIMENTAL. Hooks game functions through the experimental SDK (dyno_bindings.h): Anymaker 0.1.25 / Steam build
// 25904830 only. First runs are diagnostic: read anymaker_modding.log.
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
struct EngineEntry { const void* vehicle = nullptr; EngineSample sample; double time = 0, knock = 0, knock_time = -1e9; };
struct Dyno {
    Recorder recorder;
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
std::unordered_map<const void*, bool> g_is_dyno;      // definition pointer -> dyno_sensor? (cleared on each add)
double g_last_sweep = 0;
// When a Dyno last ticked (ms, now_seconds clock). Engine ticks are only noted while one did in the last 5 s.
std::atomic<int64_t> g_dyno_tick_ms{INT64_MIN / 2};
int64_t now_ms() { return int64_t(now_seconds() * 1000); }
bool dyno_ticking() { return now_ms() - g_dyno_tick_ms.load(std::memory_order_relaxed) < 5000; }

bool is_dyno(const uint8_t* sensor) {
    if (!sensor || !anymaker::readable(sensor + off::component_definition, 8)) return false;
    auto* definition = at<const uint8_t*>(sensor, off::component_definition);
    if (!definition) return false;
    auto found = g_is_dyno.find(definition);
    if (found != g_is_dyno.end()) return found->second;
    bool dyno = false;
    if (anymaker::readable(definition + off::definition_id, sizeof(anymaker::gc_string_view))) {
        auto* id = reinterpret_cast<const anymaker::gc_string_view*>(definition + off::definition_id);
        dyno = id->data && id->length > 0 && id->length < 64 && anymaker::readable(id->data, size_t(id->length)) && id->view() == kDefinitionId;
    }
    g_is_dyno[definition] = dyno;
    return dyno;
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
    reinterpret_cast<engine_tick_t>(h_engine_tick.original)(self, vehicle, scene);
    if (!self || !vehicle || !dyno_ticking()) return;   // nothing to feed: keep the engine tick cheap
    if (!anymaker::readable(static_cast<uint8_t*>(self) + off::engine_rps, off::engine_energy + 8 - off::engine_rps)) return;
    EngineSample s;
    s.engines = 1;
    s.rps = at<double>(self, off::engine_rps);
    s.watts = at<double>(self, off::engine_energy) * kTicksPerSecond;
    s.throttle = at<double>(self, off::engine_throttle);
    s.boost = at<double>(self, off::engine_boost);
    s.temperature_c = at<double>(self, off::engine_temperature);
    for (double* v : {&s.rps, &s.watts, &s.throttle, &s.boost, &s.temperature_c}) if (!std::isfinite(*v)) *v = 0;
    std::lock_guard lock(g_mutex);
    auto& e = g_engines[self];
    e.vehicle = vehicle; e.sample = s; e.time = now_seconds();
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

void hk_sensor_tick(uint8_t* sensor, void* vehicle, void* scene) {
    reinterpret_cast<sensor_tick_t>(h_sensor_tick.original)(sensor, vehicle, scene);
    if (!sensor || !vehicle) return;
    std::lock_guard lock(g_mutex);
    if (!is_dyno(sensor)) return;
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
    if (d.recorder.feed(s, dt)) {
        double w, rpm, nm, nm_rpm;
        d.recorder.latest()->peak_power(w, rpm);
        d.recorder.latest()->peak_torque(nm, nm_rpm);
        log(0, "pull %u kept: %.1f hp at %.0f rpm, %.0f N.m at %.0f rpm, boost %.2f, %.1f s%s", d.recorder.latest()->number, w / kWattsPerHp, rpm, nm, nm_rpm,
            d.recorder.latest()->boost_peak, d.recorder.latest()->seconds, d.recorder.latest()->knocked() ? ", knocked" : "");
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
    int i = channel_index(view.data(), view.size());
    if (i < 0) return;
    std::lock_guard lock(g_mutex);
    if (!is_dyno(sensor)) return;
    *ret = &g_dynos[sensor].slots[i];               // map nodes keep their address
    static std::atomic<int> logs{8};
    if (logs.fetch_sub(1) > 0) log(0, "data channel %s connected", kChannels[i].name);
}

// ------------------------------------------------------------------------------------- screen
const AnyGpuDrawV1* gpu;
const AnyUiStateV1* ui;
const ModControlsV1* controls;
uint64_t g_action;
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

uint32_t input(const AnyInputV1* e, void*) {
    if (!e) return 0;
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
struct View { Recorder recorder; double distance = -1; };

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
    if (clear) { best->recorder.clear(); log(0, "pulls cleared"); }
    out.recorder = best->recorder;
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

void draw(const AnyFrameV1* f, void*) {
    if (!f || !gpu || !f->focused || !g_ready.load() || !gameplay()) { g_near = false; return; }
    bool clear = false;
    bool hidden;
    {
        std::lock_guard lock(g_input_mutex);
        if (g_key_down && !g_cleared_this_hold && now_seconds() - g_key_down_at > 1.0) { g_cleared_this_hold = true; clear = true; g_hidden = false; }
        hidden = g_hidden;
    }
    static View view;                               // render thread only
    bool shown = pick(view, clear);
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
constexpr const char* kSigAddDefinitions = "53 48 81 ec 90 00 00 00 48 89 4c 24 48 48 89 54 24 50 48 8b 44 24 28 48 89 44 24 58";
constexpr const char* kSigContainerLoad = "53 48 81 ec a0 00 00 00 48 89 4c 24 40 48 89 54 24 48 48 8d 01 48 8b 1d";
constexpr const char* kSigFileLoad = "53 55 48 81 ec 98 00 00 00 48 89 4c 24 48 48 89 54 24 50 4c 89 44 24 58";
bool code_is(const void* fn, const char* sig) {
    anymaker::pattern pat(sig);
    return fn && !pat.bytes.empty() && anymaker::readable(fn, pat.bytes.size()) && pat.match(static_cast<const uint8_t*>(fn));
}
bool in_other_module(const void* fn) {
    HMODULE m = nullptr;
    return fn && GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCWSTR)fn, &m) &&
           m && m != GetModuleHandleW(nullptr);
}
// The add cell must hold _add_definitions or another hook on it (Turbo's and Rotary's live in this DLL, GpsPart's in
// its own), never container.load (see rotary_mod.cpp: a wrong route once crashed the game).
bool add_cell_ok(void** cell) {
    void* fn = cell && anymaker::readable(cell, 8) ? *cell : nullptr;
    if (!fn || code_is(fn, kSigContainerLoad) || code_is(fn, kSigFileLoad)) return false;
    return code_is(fn, kSigAddDefinitions) || in_other_module(fn);
}
void install_hooks() {
    using namespace anymaker::experimental;
    void **add = nullptr, **load_cell = nullptr, **engine = nullptr, **tick = nullptr, **get = nullptr;
    for (int attempt = 0; attempt < 180 && !g_stop; ++attempt) {
        if (attempt) Sleep(attempt < 30 ? 1000 : 5000);
        if (!matching_build()) { if (attempt == 0) log(1, "game build does not match the SDK reference; Dyno stays off"); return; }
        if (!add) add = route::cell(bind::definitions_add_definitions);
        if (add && !add_cell_ok(add)) add = nullptr;
        if (!g_file_ctor) g_file_ctor = reinterpret_cast<self_fn_t>(function(bind::definition_file_ctor));
        if (!g_file_dtor) g_file_dtor = reinterpret_cast<self_fn_t>(function(bind::definition_file_dtor));
        if (!load_cell) load_cell = route::cell(bind::definition_file_load);
        if (!g_file_load && load_cell) {
            if (code_is(*load_cell, kSigFileLoad)) g_file_load = reinterpret_cast<file_load_t>(*load_cell);
            else load_cell = nullptr;            // a wrong route: look again next time
        }
        if (!g_get_definition) g_get_definition = reinterpret_cast<get_definition_t>(function(bind::definitions_get_by_id));
        if (!g_string_ctor) g_string_ctor = reinterpret_cast<string_ctor_t>(native(anymaker::sym::string_ctor_cstr_rva));
        if (!engine) {                           // the game's engine tick, or Turbo's hook on it (hot boost air), to chain onto
            void** c = route::cell(bind::engine_tick); void* entry = route::function(bind::engine_tick);
            if (c && entry && anymaker::readable(c, 8) && (*c == entry || in_other_module(*c))) engine = c;
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
        if (!g_vehicle_transform || !g_component_transform) log(1, "transforms not found; the graph shows for the most recent Dyno, wherever it is");
        if (!write_definition_file()) { log(2, "could not write the Dyno definition file; Dyno is off"); return; }
        if (!h_engine_tick.install(engine, (void*)&hk_engine_tick)) { log(2, "engine tick hook not installed; Dyno is off"); return; }
        if (!h_get_f64.install(get, (void*)&hk_get_f64) || !h_sensor_tick.install(tick, (void*)&hk_sensor_tick)) {
            h_get_f64.remove((void*)&hk_get_f64); h_engine_tick.remove((void*)&hk_engine_tick);
            log(2, "sensor hooks not installed; Dyno is off");
            return;
        }
        if (!add_cell_ok(add) || !h_add_definitions.install(add, (void*)&hk_add_definitions)) {
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
    } else controls = nullptr;
    gpu = static_cast<const AnyGpuDrawV1*>(services->query("anyapi.gpu_draw", 1));
    if (gpu && (gpu->struct_size != sizeof(*gpu) || gpu->version != 1 || !gpu->register_renderer || !gpu->emit)) gpu = nullptr;
    if (!ui || !gpu || !gpu->register_renderer(draw, nullptr)) { gpu = nullptr; log(1, "gpu_draw or ui_state unavailable; the Dyno part works but shows no graph"); }
}
void start() { install_hooks(); }       // on AnyEngineParts' install thread
void shutdown() { g_stop = true; remove_hooks(); }
}  // namespace dyno
