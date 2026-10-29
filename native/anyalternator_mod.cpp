// AnyAlternator: three bigger alternators beside the stock one. The bigger they are, the more they make and the harder
// they are to turn: Medium 5 kW, Large 15 kW, Heavy 40 kW at 100 rps, each with a cut-in speed below which it makes
// nothing, a lower efficiency (more drag on the shaft per watt) and more weight. Input "enabled" (0 = off) stops
// charging and drag; outputs load_percent and shaft_power join the stock rps, power, energy_capacity, energy_store.
//
// How: the definitions (anyalternator_logic.h) are written to "AnyAPI and Modding/AnyAlternator" and added to the
// game's part definitions after the game adds its own (as GpsPart and AnyBattery do). Each is the game's own alternator
// (class "alternator") with a bigger wattage; their meshes are the stock alternator's, grown (anyalternator_mesh.h),
// written into rom/meshes/components. A hook on the alternator's server tick sets the size of the room in its buffer
// before the game's tick, which is what limits the energy the game takes from the shaft, and puts the real energy
// back after it with the part's own efficiency. A hook on the client tick lowers the pitch of the whine for the big
// ones. Stock alternators pass through untouched.
//
// Co-op: the alternators work on the host, like every stock part; every player needs the mod for the parts to exist
// on their side.
//
// EXPERIMENTAL. Hooks game functions through the experimental SDK (anyalternator_bindings.h): Anymaker 0.1.28 /
// Steam build 26277967 only. First runs are diagnostic: read anymaker_modding.log.
#define NOMINMAX
#include <windows.h>
#include "anyapi_services_v1.h"
#include "anyapi_experimental.hpp"
#include "anyalternator_bindings.h"
#include "route_cell.h"
#include "anyalternator_logic.h"
#include "anyalternator_mesh.h"
#include <atomic>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

namespace anyalternator {
namespace {
constexpr const char* kModId = "anyalternator";

AnyModHostV1 host;
std::atomic<bool> g_ready{false}, g_stop{false};

void log(int level, const char* fmt, ...) {
    if (!host.log) return;
    char buf[640];
    va_list ap; va_start(ap, fmt); vsnprintf(buf, sizeof buf, fmt, ap); va_end(ap);
    host.log(uint32_t(level), kModId, buf);
}
template <class T> T& at(void* base, ptrdiff_t offset) { return *reinterpret_cast<T*>(static_cast<uint8_t*>(base) + offset); }
bool can_read(const void* p, size_t n) { return p && anymaker::readable(p, n); }

// Reference layouts (0.1.28 metadata).
namespace off {
constexpr ptrdiff_t component_id = 64;               // server_scene.vehicle_component.m_id (s32, saved)
constexpr ptrdiff_t component_definition = 312;      // server_scene.vehicle_component.m_definition
constexpr ptrdiff_t client_definition = 176;         // client_scene.vehicle_component.m_definition
constexpr ptrdiff_t definition_id = 0;               // vehicle_component_definition.m_id (string)
constexpr ptrdiff_t vehicle_id = 64;                 // server_scene.vehicle.m_id (s32, saved)
constexpr ptrdiff_t energy = 0x250;                  // alternator.m_electric_content.m_energy (f64, J)
constexpr ptrdiff_t capacity = 0x258;                // .m_capacity (f64, J)
constexpr ptrdiff_t display_rps = 0x2c0;             // alternator.m_display_rps (f64, signed; written last in tick)
constexpr ptrdiff_t display_power = 0x2c8;           // alternator.m_display_power (f64, W; tooltip and "power")
constexpr ptrdiff_t audio_operate = 248;             // client alternator.m_audio_operate (audio_effect_single)
}  // namespace off
constexpr int32_t kStoreSystem = 1;

using string_ctor_t = void (*)(anymaker::gc_string_view*, const char*);
string_ctor_t g_string_ctor;
using get_definition_t = void (*)(const void** ret, const void* container, const anymaker::gc_string_view* id);
get_definition_t g_get_definition;
const double* g_delta_time;

// ================================================================== which parts are ours
// Keyed by definition pointer; every world loads its definitions anew, so the cache starts afresh on each add.
std::mutex g_definition_mutex;
std::unordered_map<const void*, int> g_size_of;
int definition_size(const void* definition) {
    if (!definition) return -1;
    std::lock_guard lock(g_definition_mutex);
    auto found = g_size_of.find(definition);
    if (found != g_size_of.end()) return found->second;
    int size = -1;
    if (can_read(static_cast<const uint8_t*>(definition) + off::definition_id, sizeof(anymaker::gc_string_view))) {
        auto* id = reinterpret_cast<const anymaker::gc_string_view*>(static_cast<const uint8_t*>(definition) + off::definition_id);
        if (id->data && id->length > 0 && id->length < 64 && can_read(id->data, size_t(id->length))) size = size_of_id(id->data, size_t(id->length));
    }
    g_size_of[definition] = size;
    return size;
}
void forget_definitions() { std::lock_guard lock(g_definition_mutex); g_size_of.clear(); }
int component_size(const void* component, ptrdiff_t definition_offset) {
    if (!component || !can_read(static_cast<const uint8_t*>(component) + definition_offset, 8)) return -1;
    return definition_size(*reinterpret_cast<const void* const*>(static_cast<const uint8_t*>(component) + definition_offset));
}

// ================================================================== files: meshes and definitions
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
    if (read_file(path) == std::vector<uint8_t>(static_cast<const uint8_t*>(data), static_cast<const uint8_t*>(data) + size)) return true;
    HANDLE h = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    DWORD written = 0;
    bool ok = WriteFile(h, data, DWORD(size), &written, nullptr) && written == size;
    CloseHandle(h);
    return ok;
}
std::wstring wide(const char* s) { return std::wstring(s, s + std::strlen(s)); }

std::string g_json_path;   // absolute, backslashes
bool write_files() {
    HMODULE self = nullptr;
    wchar_t dll[MAX_PATH]{};
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCWSTR)&write_files, &self) ||
        !GetModuleFileNameW(self, dll, MAX_PATH)) return false;
    std::wstring dir(dll);
    dir = dir.substr(0, dir.find_last_of(L"\\/"));      // ...\AnyAPI and Modding\mods
    dir = dir.substr(0, dir.find_last_of(L"\\/"));      // ...\AnyAPI and Modding
    std::wstring components = dir.substr(0, dir.find_last_of(L"\\/")) + L"\\rom\\meshes\\components\\";
    std::vector<uint8_t> stock = read_file(components + L"alternator_a_motor.mesh");
    std::string meshes[kSizeCount];
    for (int i = 0; i < kSizeCount; ++i) {
        std::vector<uint8_t> bytes = build_mesh(stock, kSizes[i]);
        bool ok = !bytes.empty() && write_file(components + wide(kSizes[i].mesh) + L".mesh", bytes.data(), bytes.size());
        meshes[i] = ok ? mesh_path(kSizes[i]) : std::string("meshes/components/alternator_a_motor.mesh");
        if (!ok) log(1, "could not build %s.mesh from alternator_a_motor.mesh (%s); the %s uses the stock look", kSizes[i].mesh,
                     stock.empty() ? "not found" : bytes.empty() ? "not the expected shape" : "not writable", kSizes[i].name);
    }
    dir += L"\\AnyAlternator";
    CreateDirectoryW(dir.c_str(), nullptr);
    std::wstring file = dir + L"\\anyalternator_parts.json";
    std::string json = definitions_json(meshes);
    bool ok = write_file(file, json.data(), json.size());
    int n = WideCharToMultiByte(CP_UTF8, 0, file.c_str(), -1, nullptr, 0, nullptr, nullptr);
    g_json_path.assign(size_t(n > 0 ? n - 1 : 0), '\0');
    if (n > 1) WideCharToMultiByte(CP_UTF8, 0, file.c_str(), -1, g_json_path.data(), n, nullptr, nullptr);
    return ok && !g_json_path.empty();
}

// ================================================================== part definitions
struct GamePath { int32_t store; uint32_t pad; anymaker::gc_string_view path; };
static_assert(sizeof(GamePath) == 24, "file.path");
using add_definitions_t = void (*)(void* container, const void* file);
using file_ctor_t = void (*)(void* file);
using file_load_t = void (*)(bool* ret, void* file, const void* path);
anymaker::cell_hook h_add_definitions;
file_ctor_t g_file_ctor, g_file_dtor;
file_load_t g_file_load;
std::thread::id g_adding;

// The path's characters must outlive the call ($string_ctor_cstr makes a non-owning view), so they are kept.
bool add_ours(void* container, const std::string& text) {
    static std::string kept[4];
    static int next;
    std::string& chars = kept[next++ % 4];
    chars = text;
    GamePath path{kStoreSystem, 0, {}};
    g_string_ctor(&path.path, chars.c_str());
    alignas(16) uint8_t file[64]{};
    g_file_ctor(file);
    bool ok = false;
    g_file_load(&ok, file, &path);
    if (ok) {
        g_adding = std::this_thread::get_id();
        reinterpret_cast<add_definitions_t>(h_add_definitions.original)(container, file);
        g_adding = {};
    }
    g_file_dtor(file);
    return ok;
}
const void* find_definition(void* container, const char* id) {
    if (!g_get_definition || !g_string_ctor) return nullptr;
    anymaker::gc_string_view s{};
    g_string_ctor(&s, id);
    const void* found = nullptr;
    g_get_definition(&found, container, &s);
    return found;
}
// Other mods' hooks (GpsPart, AnyMoto, AnyBattery, AnyEngineParts) may sit under ours on this cell; calling down
// through them is fine because every mod checks whether its parts are already in the container before adding them.
void hk_add_definitions(void* container, const void* file) {
    reinterpret_cast<add_definitions_t>(h_add_definitions.original)(container, file);
    if (!g_ready.load() || g_adding == std::this_thread::get_id() || !container || find_definition(container, kSizes[0].id)) return;
    forget_definitions();   // a new set of definitions (a world load): pointers seen before may be reused
    std::string forward = g_json_path;
    for (char& c : forward) if (c == '\\') c = '/';
    bool ok = add_ours(container, g_json_path) || add_ours(container, forward);
    for (const Size& s : kSizes) if (const void* d = find_definition(container, s.id)) definition_size(d);
    static std::atomic<int> logs{6};
    if (logs.fetch_sub(1) > 0)
        log(ok ? 0 : 2, ok ? "Medium, Large and Heavy Alternators added to the game's parts (%s)" : "could not load the alternator definitions from %s", g_json_path.c_str());
}

// ================================================================== per part (server)
// Data slots are never freed: microcontrollers keep the pointer get_data_f64 hands them, so it must outlive a world
// change. A new part at a reused address gets the slot back with its defaults.
struct Live {
    int32_t vehicle = 0, component = 0;
    bool seen = false;
    double enabled = 1;      // input "enabled": unconnected stays 1 (on)
    double load = 0;         // output load_percent
    double shaft = 0;        // output shaft_power (W)
};
std::mutex g_mutex;
std::unordered_map<const void*, Live*> g_live;
Live* live_of(const void* component) {
    Live*& slot = g_live[component];
    if (!slot) {
        slot = static_cast<Live*>(HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(Live)));
        if (slot) *slot = Live{};
    }
    return slot;
}

using tick_t = void (*)(void* self, void* vehicle, void* scene);
anymaker::cell_hook h_tick;
void hk_tick(void* self, void* vehicle, void* scene) {
    auto original = reinterpret_cast<tick_t>(h_tick.original);
    int size_index = g_ready.load() ? component_size(self, off::component_definition) : -1;
    if (size_index < 0 || !vehicle || !can_read(static_cast<uint8_t*>(vehicle) + off::vehicle_id, 4)) return original(self, vehicle, scene);
    const Size& s = kSizes[size_index];
    double dt = g_delta_time && std::isfinite(*g_delta_time) && *g_delta_time > 0 ? *g_delta_time : 1.0 / 60.0;
    int32_t vehicle_id = at<int32_t>(vehicle, off::vehicle_id), component_id = at<int32_t>(self, off::component_id);
    std::unique_lock lock(g_mutex);
    Live* live = live_of(self);
    if (!live) { lock.unlock(); return original(self, vehicle, scene); }
    if (!live->seen || live->vehicle != vehicle_id || live->component != component_id) {
        *live = Live{};                              // a new part (or one at a freed address)
        live->seen = true; live->vehicle = vehicle_id; live->component = component_id;
        static std::atomic<int> logs{12};
        if (logs.fetch_sub(1) > 0)
            log(0, "%s %d:%d: %.0f W out, %.0f W from the shaft at 100 rps, cut-in %.0f rps", s.name, vehicle_id, component_id, s.output_watts,
                s.wattage(), s.cut_in_rps);
    }
    bool enabled = std::fabs(live->enabled) > 1e-6;
    lock.unlock();
    double& energy = at<double>(self, off::energy);
    double capacity = at<double>(self, off::capacity);
    double wanted = wanted_energy(s, at<double>(self, off::display_rps), enabled, dt, energy, capacity);
    Tick t = before_tick(energy, capacity, wanted);
    original(self, vehicle, scene);
    Result r = after_tick(s, t, energy, at<double>(self, off::capacity), dt);
    at<double>(self, off::display_power) = r.output_w;
    lock.lock();
    live->load = load_percent(s, r);
    live->shaft = r.shaft_w;
}

using get_f64_t = void (*)(double** ret, void* self, const anymaker::gc_string_view* name);
anymaker::cell_hook h_get_f64;
void hk_get_f64(double** ret, void* self, const anymaker::gc_string_view* name) {
    reinterpret_cast<get_f64_t>(h_get_f64.original)(ret, self, name);
    if (!ret || *ret || !self || !name || component_size(self, off::component_definition) < 0) return;
    std::string_view n = name->view();
    std::lock_guard lock(g_mutex);
    Live* live = live_of(self);
    if (!live) return;
    if (n == "enabled") *ret = &live->enabled;
    else if (n == "load_percent") *ret = &live->load;
    else if (n == "shaft_power") *ret = &live->shaft;
}

// ================================================================== the whine (client)
// The client tick plays the stock loop through its m_audio_operate voice; for ours the voice update in that tick
// gets a lower playback speed.
using client_tick_t = void (*)(void* self, void* client, void* vehicle, void* scene, const double* dt);
using vol_speed_t = void (*)(uint8_t* single, const double* volume, const double* speed, const int32_t* effect, const int32_t* group, const void* pos);
anymaker::cell_hook h_client_tick, h_vol_speed;
thread_local uint8_t* t_voice = nullptr;
thread_local double t_pitch = 1;
void hk_client_tick(void* self, void* client, void* vehicle, void* scene, const double* dt) {
    auto original = reinterpret_cast<client_tick_t>(h_client_tick.original);
    int size_index = g_ready.load() ? component_size(self, off::client_definition) : -1;
    if (size_index < 0) return original(self, client, vehicle, scene, dt);
    uint8_t* prev_voice = t_voice; double prev_pitch = t_pitch;
    t_voice = static_cast<uint8_t*>(self) + off::audio_operate; t_pitch = kSizes[size_index].pitch;
    original(self, client, vehicle, scene, dt);
    t_voice = prev_voice; t_pitch = prev_pitch;
}
void hk_vol_speed(uint8_t* single, const double* volume, const double* speed, const int32_t* effect, const int32_t* group, const void* pos) {
    auto original = reinterpret_cast<vol_speed_t>(h_vol_speed.original);
    if (!t_voice || single != t_voice || !speed) return original(single, volume, speed, effect, group, pos);
    double lower = *speed * t_pitch;
    original(single, volume, &lower, effect, group, pos);
}

// ================================================================== install (background thread)
void remove_hooks() {
    h_vol_speed.remove((void*)&hk_vol_speed);
    h_client_tick.remove((void*)&hk_client_tick);
    h_get_f64.remove((void*)&hk_get_f64);
    h_add_definitions.remove((void*)&hk_add_definitions);
    h_tick.remove((void*)&hk_tick);
}
void install_hooks() {
    using namespace anymaker::experimental;
    for (int attempt = 0; attempt < 180 && !g_stop; ++attempt) {
        if (attempt) Sleep(1000);
        if (!matching_build()) { if (attempt == 0) log(1, "game build does not match the SDK reference; AnyAlternator stays off"); return; }
        int roots = -1;
        void** add = route::cell(bind::definitions_add_definitions, &roots);
        auto ctor = reinterpret_cast<file_ctor_t>(route::function(bind::definition_file_ctor));
        auto dtor = reinterpret_cast<file_ctor_t>(route::function(bind::definition_file_dtor));
        auto load = reinterpret_cast<file_load_t>(route::function(bind::definition_file_load));
        auto sc = reinterpret_cast<string_ctor_t>(native(anymaker::sym::string_ctor_cstr_rva));
        void** tick = route::cell(bind::alternator_tick);
        if (add && !route::add_cell_ok(add)) {
            static int logs = 3;
            if (logs-- > 0) log(1, "the definitions route led to %s, not _add_definitions; looking again", route::what_is(anymaker::readable(add, 8) ? *add : nullptr));
            add = nullptr;
        }
        if (load && !route::file_load_ok((void*)load)) load = nullptr;
        if (!add || !ctor || !dtor || !load || !sc || !tick) {
            if (attempt == 20 || attempt == 179)
                log(attempt == 179 ? 2 : 1, "%s the game functions (route root matches %d, add=%d file ctor=%d dtor=%d load=%d string=%d alternator tick=%d)",
                    attempt == 179 ? "could not locate" : "still looking for", roots, !!add, !!ctor, !!dtor, !!load, !!sc, !!tick);
            continue;
        }
        g_file_ctor = ctor; g_file_dtor = dtor; g_file_load = load; g_string_ctor = sc;
        g_get_definition = reinterpret_cast<get_definition_t>(route::function(bind::definitions_get_by_id));
        if (!g_get_definition) { log(2, "definition lookup not found; AnyAlternator is off"); return; }
        auto dt = static_cast<const double*>(global(bind::server_delta_time));
        g_delta_time = dt && anymaker::readable(dt, 8) ? dt : nullptr;
        if (!write_files()) { log(2, "could not write the AnyAlternator definition file; AnyAlternator is off"); return; }
        // The tick hook goes in before the parts exist: without it they would run at the stock 75% with no cut-in.
        if (!h_tick.install(tick, (void*)&hk_tick)) { log(2, "alternator tick hook not installed (cell taken or not writable); AnyAlternator is off"); return; }
        if (!h_add_definitions.install(add, (void*)&hk_add_definitions)) {
            log(2, "the definitions hook cell was already taken or not writable; AnyAlternator is off");
            remove_hooks();
            return;
        }
        void** data = route::cell(bind::alternator_get_data_f64);
        if (!data || !h_get_f64.install(data, (void*)&hk_get_f64)) log(1, "enabled, load_percent and shaft_power unavailable (the stock outputs still work)");
        void** client_tick = route::cell(bind::client_alternator_tick);
        void** voice = route::cell(bind::effect_update_play_volume_speed);
        // The voice hook goes in first: it does nothing until the client tick hook marks a voice.
        if (!client_tick || !voice || !h_vol_speed.install(voice, (void*)&hk_vol_speed) || !h_client_tick.install(client_tick, (void*)&hk_client_tick))
            log(1, "alternator whine hook not installed; the big alternators sound like the stock one");
        g_ready = true;
        log(0, "Ready after %d s (server tick length %s). Load or start a world to add the alternators (definition file %s).", attempt,
            g_delta_time ? "found" : "not found, assuming 60 Hz", g_json_path.c_str());
        return;
    }
}
}  // namespace
}  // namespace anyalternator

using namespace anyalternator;
extern "C" __declspec(dllexport) bool AnyAPI_ModInit(const AnyModHostV1* h, AnyModCallbacksV1* out) {
    if (!h || !out || h->struct_size != sizeof(*h) || h->abi != ANYAPI_MOD_ABI || out->struct_size != sizeof(*out)) return false;
    host = *h; out->id = kModId;
    out->shutdown = [](void*) { g_stop = true; remove_hooks(); };
    return true;
}
extern "C" __declspec(dllexport) void AnyAPI_ModReady() {
    std::thread(install_hooks).detach();   // resolving scans game memory; keep it off the loader and frame threads
}
