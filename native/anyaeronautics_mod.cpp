// AnyAeronautics: jet parts that fly. First parts: the Cone Nozzle and the Fighter Nozzle, straight jet nozzles that
// actually push (the "Thrust Nozzle").
//
// The game's own jet engine (compressor, combustion chamber, exhaust) already has a throttle input, spool-up, fuel
// burn and temperature, and its combustion chamber writes the exhaust's thrust factor (its speed factor, 0..1) every
// tick. The stock exhaust turns that into a force so small it barely moves anything. The Thrust Nozzle is the stock
// exhaust (same class, faces and mass, built from the game's own definition when the game starts; its own straight
// mesh from anyaeronautics_mesh.h, since the stock outlet bends up about 45 degrees) that pushes
// the vehicle with real thrust: max thrust x spool^2, at the nozzle, toward the engine in front of it. Outputs thrust
// (kN) and spool (0..1). Stock exhausts are untouched.
//
// How: the definitions are written to "AnyAPI and Modding/AnyAeronautics" and added right after the game adds its
// own (the GpsPart / AnyLaser / AnyCVT route). A hook on the jet exhaust's server tick runs the stock tick, then for
// our nozzles reads the thrust factor and applies the force with the exhaust's own impulse activator through
// physics_util.impulse_activator.add_force (force, world position), the same call the stock exhaust makes. A hook on
// its get_data_f64 serves the two outputs.
//
// Single player / host: physics runs on the host, so the push is the host's. Every player needs the mod (the parts
// are definitions).
//
// EXPERIMENTAL. Hooks game functions through the experimental SDK (anyaeronautics_bindings.h): Anymaker 0.2.0 /
// Steam build 26328453 only. First runs are diagnostic: read anymaker_modding.log.
#define NOMINMAX
#include <windows.h>
#include "anyapi_services_v1.h"
#include "anyapi_experimental.hpp"
#include "anyaeronautics_bindings.h"
#include "route_cell.h"
#include "anyaeronautics_logic.h"
#include "anyaeronautics_mesh.h"
#include <atomic>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

namespace anyaeronautics {
namespace {
constexpr const char* kModId = "anyaeronautics";

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
bool can_read(const void* p, size_t n) { return p && anymaker::readable(p, n); }
template <class T> T rd(const void* p, ptrdiff_t o) { T v; std::memcpy(&v, static_cast<const uint8_t*>(p) + o, sizeof v); return v; }

// Layouts from the 0.2.0 SDK reference (metadata, carried from 0.1.27 where the code was read).
namespace off {
constexpr ptrdiff_t component_definition = 312;   // server_scene.vehicle_component.m_definition
constexpr ptrdiff_t definition_id = 0;            // vehicle_component_definition.m_id (string)
// server_scene.vehicle_component.jet_engine_exhaust (784 bytes)
constexpr ptrdiff_t exhaust_upstream = 640;       // m_jet_engine_upstream (ptr<vehicle_component>): the combustion chamber
constexpr ptrdiff_t exhaust_thrust_factor = 720;  // m_thrust_factor (f64): written by combustion.tick (its speed factor)
constexpr ptrdiff_t exhaust_activator = 728;      // _m_impulse_activator (physics_util.impulse_activator, 40 bytes)
constexpr size_t exhaust_size = 784;
// server_scene.vehicle
constexpr ptrdiff_t vehicle_physics = 2320;       // m_physics (physics.object)
// server_scene.vehicle_component.jet_engine_combustion (1208 bytes), logged to calibrate a fuel-burn thrust model
constexpr ptrdiff_t combustion_rps = 1112, combustion_throttle = 1160, combustion_fuel_flow = 1176, combustion_torque = 1184;
constexpr size_t combustion_size = 1208;
}  // namespace off
constexpr int32_t kStoreSystem = 1;               // file.e_store.system

using string_ctor_t = void (*)(anymaker::gc_string_view*, const char*);
string_ctor_t g_string_ctor;
using get_definition_t = void (*)(const void** ret, const void* container, const anymaker::gc_string_view* id);
get_definition_t g_get_definition;
struct GamePath { int32_t store; uint32_t pad; anymaker::gc_string_view path; };   // file.path (24 bytes)
static_assert(sizeof(GamePath) == 24, "file.path");

// ================================================================== files
std::vector<uint8_t> read_file(const std::wstring& path) {
    std::vector<uint8_t> data;
    HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return data;
    LARGE_INTEGER size{};
    if (GetFileSizeEx(h, &size) && size.QuadPart > 0 && size.QuadPart < (1 << 26)) {
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

std::string g_json_path;   // absolute, backslashes
bool write_files() {
    HMODULE self = nullptr;
    wchar_t dll[MAX_PATH]{};
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCWSTR)&write_files, &self) ||
        !GetModuleFileNameW(self, dll, MAX_PATH)) return false;
    std::wstring dir(dll);
    dir = dir.substr(0, dir.find_last_of(L"\\/"));      // ...\AnyAPI and Modding\mods
    dir = dir.substr(0, dir.find_last_of(L"\\/"));      // ...\AnyAPI and Modding
    std::wstring rom = dir.substr(0, dir.find_last_of(L"\\/")) + L"\\rom\\";
    std::wstring components = rom + L"meshes\\components\\";
    std::vector<uint8_t> stock_mesh = read_file(components + L"jet_exhaust_a.mesh");
    std::vector<std::string> meshes(kSizeCount);
    for (int i = 0; i < kSizeCount; ++i) {
        const Size& s = kSizes[i];
        std::vector<uint8_t> bytes = build_nozzle_mesh(stock_mesh, i == 0 ? Look::Cone : Look::Fighter, s.mesh);
        std::wstring file = components + std::wstring(s.mesh, s.mesh + std::strlen(s.mesh)) + L".mesh";
        if (!bytes.empty() && write_file(file, bytes.data(), bytes.size())) meshes[i] = mesh_path(s);
        else log(1, "could not build %s.mesh (%s); the %s uses the stock exhaust look", s.mesh,
                 stock_mesh.empty() ? "jet_exhaust_a.mesh not found" : bytes.empty() ? "jet_exhaust_a.mesh not the expected format" : "not writable", s.name);
    }
    std::vector<uint8_t> stock_defs = read_file(rom + L"data\\vehicle_component_definitions.json");
    std::string error, stock_id;
    std::string json = definitions_json(std::string_view(reinterpret_cast<const char*>(stock_defs.data()), stock_defs.size()), meshes, &error, &stock_id);
    if (json.empty()) { log(2, "could not build the Thrust Nozzle definitions: %s", stock_defs.empty() ? "rom\\data\\vehicle_component_definitions.json not found" : error.c_str()); return false; }
    log(0, "Cone and Fighter Nozzles built from the stock jet exhaust \"%s\" (meshes: %s, %s)", stock_id.c_str(), meshes[0].empty() ? "stock" : "ours", meshes[1].empty() ? "stock" : "ours");
    dir += L"\\AnyAeronautics";
    CreateDirectoryW(dir.c_str(), nullptr);
    std::wstring file = dir + L"\\anyaeronautics_parts.json";
    bool ok = write_file(file, json.data(), json.size());
    int n = WideCharToMultiByte(CP_UTF8, 0, file.c_str(), -1, nullptr, 0, nullptr, nullptr);
    g_json_path.assign(size_t(n > 0 ? n - 1 : 0), '\0');
    if (n > 1) WideCharToMultiByte(CP_UTF8, 0, file.c_str(), -1, g_json_path.data(), n, nullptr, nullptr);
    return ok && !g_json_path.empty();
}

// ================================================================== part definitions
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

// Which definitions are ours, keyed by definition pointer. Every world loads its definitions anew, so the cache
// starts afresh on each add.
std::mutex g_definition_mutex;
std::unordered_map<const void*, int> g_size_of;
void forget_definitions() { std::lock_guard lock(g_definition_mutex); g_size_of.clear(); }
int definition_size(const void* definition) {
    if (!definition) return -1;
    std::lock_guard lock(g_definition_mutex);
    auto found = g_size_of.find(definition);
    if (found != g_size_of.end()) return found->second;
    int size = -1;
    auto* p = static_cast<const uint8_t*>(definition) + off::definition_id;
    if (can_read(p, sizeof(anymaker::gc_string_view))) {
        auto* id = reinterpret_cast<const anymaker::gc_string_view*>(p);
        if (id->data && id->length > 0 && id->length < 64 && can_read(id->data, size_t(id->length))) size = size_of_id(id->data, size_t(id->length));
    }
    g_size_of[definition] = size;
    return size;
}
int component_size(const void* component) {
    if (!component || !can_read(static_cast<const uint8_t*>(component) + off::component_definition, 8)) return -1;
    return definition_size(*reinterpret_cast<const void* const*>(static_cast<const uint8_t*>(component) + off::component_definition));
}

// Other mods' hooks may sit under ours on this cell; every mod checks whether its parts are already in the container.
void hk_add_definitions(void* container, const void* file) {
    reinterpret_cast<add_definitions_t>(h_add_definitions.original)(container, file);
    if (!g_ready.load() || g_adding == std::this_thread::get_id() || !container || find_definition(container, kSizes[0].id)) return;
    forget_definitions();   // a new set of definitions (a world load): pointers seen before may be reused
    std::string forward = g_json_path;
    for (char& c : forward) if (c == '\\') c = '/';
    bool ok = add_ours(container, g_json_path) || add_ours(container, forward);
    static std::atomic<int> logs{6};
    if (logs.fetch_sub(1) > 0)
        log(ok ? 0 : 2, ok ? "Thrust Nozzles added to the game's parts (%s)" : "could not load the Thrust Nozzle definitions from %s", g_json_path.c_str());
}

// ================================================================== thrust (server thread)
// Data slots are never freed: microcontrollers keep the pointers get_data_f64 hands them.
struct Live { double slots[kSlots]{}; double newtons = 0, last = 0, next_log = 0, last_fuel = 0; int logs = 8; };
std::mutex g_mutex;
std::unordered_map<const void*, Live> g_live;   // keyed by server component

using tick_t = void (*)(uint8_t* exhaust, uint8_t* vehicle, void* scene);
using get_f64_t = void (*)(double** ret, uint8_t* exhaust, const anymaker::gc_string_view* name);
using transform_t = void (*)(double* out, const void* object);
using add_force_t = void (*)(void* activator, void* object, const double* force, const double* world_position);
using get_mass_t = void (*)(double* ret, const void* object);
using is_added_t = void (*)(bool* ret, const void* object);
anymaker::cell_hook h_tick, h_get_f64;
transform_t g_vehicle_transform, g_component_transform;
add_force_t g_add_force;
get_mass_t g_get_mass;
is_added_t g_is_added;

Mat34 world_of(const uint8_t* vehicle, const Mat34& v, const void* component) {
    Mat34 c;
    g_component_transform(c.m, component);
    return compose(v, c);
}

void hk_tick(uint8_t* exhaust, uint8_t* vehicle, void* scene) {
    reinterpret_cast<tick_t>(h_tick.original)(exhaust, vehicle, scene);
    if (!g_ready.load() || !exhaust || !vehicle || g_stop) return;
    int size = component_size(exhaust);
    if (size < 0 || !can_read(exhaust, off::exhaust_size) || !can_read(vehicle + off::vehicle_physics, 8)) return;
    double spool = rd<double>(exhaust, off::exhaust_thrust_factor);
    const void* upstream = rd<const void*>(exhaust, off::exhaust_upstream);
    bool has_engine = upstream && can_read(upstream, off::combustion_size);
    double raw_fuel = has_engine ? rd<double>(upstream, off::combustion_fuel_flow) : 0;
    double engine_throttle = has_engine ? rd<double>(upstream, off::combustion_throttle) : 0;
    void* physics = vehicle + off::vehicle_physics;
    std::lock_guard lock(g_mutex);
    Live& live = g_live[exhaust];
    double fuel = has_engine ? limiter_fuel(raw_fuel, spool, engine_throttle, live.last_fuel) : 0;
    double target = fuel_thrust_newtons(kSizes[size].tsfc, fuel);
    double now = now_seconds();
    live.newtons = lag_thrust(live.newtons, target, live.last > 0 ? now - live.last : 0);
    live.last = now;
    double newtons = live.newtons < 1.0 ? 0 : live.newtons;
    live.slots[kSpool] = std::isfinite(spool) ? std::clamp(spool, 0.0, 1.0) : 0;
    live.slots[kThrust] = 0;
    if (newtons <= 0) return;
    if (g_is_added) { bool added = false; g_is_added(&added, physics); if (!added) return; }
    Mat34 v;
    g_vehicle_transform(v.m, vehicle);
    Mat34 nozzle = world_of(vehicle, v, exhaust), engine = world_of(vehicle, v, upstream);
    Vec3 at = column(nozzle, 3), dir;
    if (!finite(nozzle) || !finite(engine) || !push_direction(at, column(engine, 3), dir)) return;
    Vec3 force = dir * newtons;
    double f[3] = {force.x, force.y, force.z}, p[3] = {at.x, at.y, at.z};
    g_add_force(exhaust + off::exhaust_activator, physics, f, p);
    live.slots[kThrust] = newtons / 1000.0;
    if (live.logs > 0 && newtons > 2000 && now >= live.next_log) {   // about once a second, 8 times per nozzle
        --live.logs;
        live.next_log = now + 1.0;
        double mass = 0;
        if (g_get_mass) g_get_mass(&mass, physics);
        double rps = 0, throttle = 0, torque = 0;
        if (can_read(upstream, off::combustion_size)) {
            rps = rd<double>(upstream, off::combustion_rps); throttle = rd<double>(upstream, off::combustion_throttle);
            torque = rd<double>(upstream, off::combustion_torque);
        }
        log(0, "%s: spool %.2f, thrust %.1f kN along (%.2f, %.2f, %.2f), vehicle mass %.0f kg (%.2f g); engine rps %.1f, throttle %.2f, fuel flow %.5g, torque %.1f",
            kSizes[size].name, spool, newtons / 1000.0, dir.x, dir.y, dir.z, mass, mass > 0 ? newtons / (mass * 9.81) : 0.0, rps, throttle, fuel, torque);
    }
}

void hk_get_f64(double** ret, uint8_t* exhaust, const anymaker::gc_string_view* name) {
    reinterpret_cast<get_f64_t>(h_get_f64.original)(ret, exhaust, name);
    if (!ret || *ret || !exhaust || !name || component_size(exhaust) < 0) return;
    auto view = name->view();
    int i = channel_index(view.data(), view.size());
    if (i < 0) return;
    std::lock_guard lock(g_mutex);
    *ret = &g_live[exhaust].slots[i];
}

// ================================================================== install (background thread)
void remove_hooks() {
    h_get_f64.remove((void*)&hk_get_f64);
    h_add_definitions.remove((void*)&hk_add_definitions);
    h_tick.remove((void*)&hk_tick);
}
void install_hooks() {
    using namespace anymaker::experimental;
    for (int attempt = 0; attempt < 180 && !g_stop; ++attempt) {
        if (attempt) Sleep(1000);
        if (!matching_build()) { if (attempt == 0) log(1, "game build does not match the SDK reference; AnyAeronautics stays off"); return; }
        int roots = -1;
        void** add = route::cell(bind::definitions_add_definitions, &roots);
        auto ctor = reinterpret_cast<file_ctor_t>(route::function(bind::definition_file_ctor));
        auto dtor = reinterpret_cast<file_ctor_t>(route::function(bind::definition_file_dtor));
        auto load = reinterpret_cast<file_load_t>(route::function(bind::definition_file_load));
        auto sc = reinterpret_cast<string_ctor_t>(native(anymaker::sym::string_ctor_cstr_rva));
        auto vt = reinterpret_cast<transform_t>(route::function(bind::server_vehicle_get_transform));
        auto ct = reinterpret_cast<transform_t>(route::function(bind::server_component_get_transform));
        auto force = reinterpret_cast<add_force_t>(route::function(bind::add_force_at));
        void** tick = route::cell(bind::exhaust_tick);
        if (add && !route::add_cell_ok(add)) {
            static int logs = 3;
            if (logs-- > 0) log(1, "the definitions route led to %s, not _add_definitions; looking again", route::what_is(anymaker::readable(add, 8) ? *add : nullptr));
            add = nullptr;
        }
        if (load && !route::file_load_ok((void*)load)) load = nullptr;
        if (!add || !ctor || !dtor || !load || !sc || !vt || !ct || !force || !tick) {
            if (attempt == 20 || attempt == 179)
                log(attempt == 179 ? 2 : 1, "%s the game functions (route root matches %d, add=%d file ctor=%d dtor=%d load=%d string=%d transforms=%d/%d add_force=%d exhaust tick=%d)",
                    attempt == 179 ? "could not locate" : "still looking for", roots, !!add, !!ctor, !!dtor, !!load, !!sc, !!vt, !!ct, !!force, !!tick);
            continue;
        }
        g_file_ctor = ctor; g_file_dtor = dtor; g_file_load = load; g_string_ctor = sc;
        g_vehicle_transform = vt; g_component_transform = ct; g_add_force = force;
        g_get_definition = reinterpret_cast<get_definition_t>(route::function(bind::definitions_get_by_id));
        if (!g_get_definition) { log(2, "definition lookup not found; AnyAeronautics is off"); return; }
        g_get_mass = reinterpret_cast<get_mass_t>(route::function(bind::physics_get_mass));
        g_is_added = reinterpret_cast<is_added_t>(route::function(bind::physics_is_added));
        if (!g_is_added) log(1, "physics.object.get_is_added_to_scene not found; thrust is applied without that check");
        if (!write_files()) { log(2, "could not write the AnyAeronautics definition file; AnyAeronautics is off"); return; }
        // The tick hook goes in before the parts exist: without it a nozzle is a stock exhaust.
        if (!h_tick.install(tick, (void*)&hk_tick)) { log(2, "jet exhaust tick hook not installed (cell taken or not writable); AnyAeronautics is off"); return; }
        if (!h_add_definitions.install(add, (void*)&hk_add_definitions)) {
            log(2, "the definitions hook cell was already taken or not writable; AnyAeronautics is off");
            remove_hooks();
            return;
        }
        void** data = route::cell(bind::exhaust_get_data_f64);
        if (!data || !h_get_f64.install(data, (void*)&hk_get_f64)) log(1, "thrust and spool outputs unavailable (thrust still works)");
        g_ready = true;
        log(0, "Ready after %d s (mass readout %s). Load or start a world to add the Thrust Nozzles (definition file %s).", attempt,
            g_get_mass ? "on" : "off", g_json_path.c_str());
        return;
    }
}
}  // namespace
}  // namespace anyaeronautics

using namespace anyaeronautics;
extern "C" __declspec(dllexport) bool AnyAPI_ModInit(const AnyModHostV1* h, AnyModCallbacksV1* out) {
    if (!h || !out || h->struct_size != sizeof(*h) || h->abi != ANYAPI_MOD_ABI || out->struct_size != sizeof(*out)) return false;
    host = *h; out->id = kModId;
    out->shutdown = [](void*) { g_stop = true; remove_hooks(); };
    return true;
}
extern "C" __declspec(dllexport) void AnyAPI_ModReady() {
    std::thread(install_hooks).detach();   // resolving scans game memory; keep it off the loader and frame threads
}
