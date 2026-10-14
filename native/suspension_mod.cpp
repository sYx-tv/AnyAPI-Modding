// AnySuspension: a free-spinning bearing and a straight (linear) spring suspension, like Trailmakers' parts.
//
// Bearing: a pin and ring pair cloned from the game's hinge pin and knuckle, with no swing stops. The game's own
// hinge code runs it; only the definitions differ (suspension_logic.h).
//
// Suspension: a rail and slider pair cloned from the game's rail and rail slider. The game's slider joint (Jolt)
// has a motor; on each server tick of a suspension slider this mod sets that motor to a spring and damper that pulls
// the slider back to where it was placed along the rail. Stiffness follows the heavier body's mass shared between
// the suspension sliders holding it, so every vehicle sags about the same: g / (2 pi f)^2, 4 cm at the default 2.5 Hz.
// Each slider has Stiffness and Damping rows in the Properties Tool (0 to 1, like the stock wheel's). The rest
// position and those values are saved with each world.
//
// Single player / host: the definitions are added on this game's server and client scene, and every player needs the
// mod. The spring runs on the server, so clients only see the motion.
//
// EXPERIMENTAL. Hooks game functions through the experimental SDK (suspension_bindings.h) and calls three game.exe
// physics natives by address: Anymaker 0.1.25 / Steam build 25904830 only. First runs are diagnostic: read
// anymaker_modding.log.
#define NOMINMAX
#include <windows.h>
#include "anyapi_services_v1.h"
#include "anyapi_experimental.hpp"
#include "suspension_bindings.h"
#include "suspension_logic.h"
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

namespace suspension {
namespace {
constexpr const char* kModId = "anysuspension";

AnyModHostV1 host;
std::atomic<bool> g_ready{false}, g_stop{false};

void log(int level, const char* fmt, ...) {
    if (!host.log) return;
    char buf[640];
    va_list ap; va_start(ap, fmt); vsnprintf(buf, sizeof buf, fmt, ap); va_end(ap);
    host.log(uint32_t(level), kModId, buf);
}

// Reference layouts (metadata, Anymaker 0.1.25).
namespace off {
constexpr ptrdiff_t component_id = 64;               // server_scene.vehicle_component.m_id (s32, saved)
constexpr ptrdiff_t component_definition = 312;      // m_definition (ptr<const vehicle_component_definition>)
constexpr ptrdiff_t component_vehicle = 320;         // m_parent_vehicle (ptr<server_scene.vehicle>)
constexpr ptrdiff_t definition_id = 0;               // vehicle_component_definition.m_id (string)
constexpr ptrdiff_t slider_constraint = 544;         // rail_slider._m_constraint (physics.constraint.slider, 16 bytes)
constexpr ptrdiff_t slider_rail = 560;               // rail_slider.m_connected_component (ptr<rail>)
constexpr ptrdiff_t vehicle_id = 64;                 // server_scene.vehicle.m_id (s32, saved)
constexpr ptrdiff_t vehicle_physics = 2320;          // server_scene.vehicle.m_physics (physics.object)
// Jolt's SliderConstraint (game.exe: physics.constraint.slider.set_motor writes these). A physics handle holds a
// pointer at +8 to the slot that holds the Jolt object.
constexpr ptrdiff_t jolt_has_limits = 0xa0;          // bool
constexpr ptrdiff_t jolt_limit_min = 0xa4, jolt_limit_max = 0xa8;   // float
constexpr ptrdiff_t jolt_spring_mode = 0xbc;         // motor spring: ESpringMode (u8): 0 frequency, 1 stiffness
constexpr ptrdiff_t jolt_spring_value = 0xc0;        // frequency (Hz) or stiffness (N/m), float
constexpr ptrdiff_t jolt_spring_damping = 0xc4;      // damping ratio or N s/m, float
constexpr ptrdiff_t jolt_motor_state = 0xd8;         // EMotorState (s32): 2 position
constexpr ptrdiff_t jolt_target = 0xe0;              // target position, float
}  // namespace off
// game.exe natives, Anymaker 0.1.25 (found from their registration strings).
namespace natives {
constexpr uint32_t slider_set_motor_rva = 0x14cff0;     // (slider, &f64 frequency, &f64 damping, &f64 target)
constexpr uint32_t slider_get_position_rva = 0x14d0b0;  // (&f64 ret, slider)
constexpr uint32_t object_get_mass_rva = 0x14bf50;      // (&f64 ret, physics.object)
}  // namespace natives
constexpr int32_t kStoreSystem = 1;                  // file.e_store.system

template <class T> T& at(void* base, ptrdiff_t offset) { return *reinterpret_cast<T*>(static_cast<uint8_t*>(base) + offset); }
template <class T> const T& at(const void* base, ptrdiff_t offset) { return *reinterpret_cast<const T*>(static_cast<const uint8_t*>(base) + offset); }
bool readable(const void* p, size_t n) { return p && anymaker::readable(p, n); }

using self_fn_t = void (*)(void* self);
using string_ctor_t = void (*)(anymaker::gc_string_view*, const char*);
using get_definition_t = void (*)(const void** ret, const void* container, const anymaker::gc_string_view* id);
using set_motor_t = void (*)(const void* slider, const double* frequency, const double* damping, const double* target);
using get_position_t = void (*)(double* ret, const void* slider);
using get_mass_t = void (*)(double* ret, const void* object);
string_ctor_t g_string_ctor;
get_definition_t g_get_definition;
set_motor_t g_set_motor;
get_position_t g_get_position;
get_mass_t g_get_mass;

double now_seconds() {
    static LARGE_INTEGER freq = [] { LARGE_INTEGER f; QueryPerformanceFrequency(&f); return f; }();
    LARGE_INTEGER t; QueryPerformanceCounter(&t);
    return double(t.QuadPart) / double(freq.QuadPart);
}

std::recursive_mutex g_mutex;                        // definition cache (server and main thread)
std::unordered_map<const void*, bool> g_is_slider;   // definition pointer -> suspension slider?

bool slider_definition(const void* definition) {
    if (!definition) return false;
    std::lock_guard<std::recursive_mutex> lock(g_mutex);
    auto found = g_is_slider.find(definition);
    if (found != g_is_slider.end()) return found->second;
    bool yes = false;
    if (readable(static_cast<const uint8_t*>(definition) + off::definition_id, sizeof(anymaker::gc_string_view))) {
        auto& id = at<anymaker::gc_string_view>(definition, off::definition_id);
        if (id.data && id.length > 0 && id.length < 64 && readable(id.data, size_t(id.length)))
            yes = is_suspension_slider_id(id.data, size_t(id.length));
    }
    g_is_slider[definition] = yes;
    return yes;
}
const void* pointer_at(const void* base, ptrdiff_t offset) {
    if (!base || !readable(static_cast<const uint8_t*>(base) + offset, 8)) return nullptr;
    return at<const void*>(base, offset);
}

// ------------------------------------------------------------------------------------- files
bool write_file(const std::wstring& path, const void* data, size_t size) {
    HANDLE h = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    DWORD written = 0;
    bool ok = WriteFile(h, data, DWORD(size), &written, nullptr) && written == size;
    CloseHandle(h);
    return ok;
}
std::string read_file(const std::wstring& path) {
    HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return {};
    std::string out;
    LARGE_INTEGER size{};
    if (GetFileSizeEx(h, &size) && size.QuadPart > 0 && size.QuadPart < (1 << 22)) {
        out.resize(size_t(size.QuadPart));
        DWORD read = 0;
        if (!ReadFile(h, out.data(), DWORD(out.size()), &read, nullptr)) read = 0;
        out.resize(read);
    }
    CloseHandle(h);
    return out;
}

// ------------------------------------------------------------------------------------- part definitions
// GamePath = file.path { e_store m_store; string m_path; } (24 bytes)
struct GamePath { int32_t store; uint32_t pad; anymaker::gc_string_view path; };
static_assert(sizeof(GamePath) == 24, "file.path");
using add_definitions_t = void (*)(void* container, const void* file);
using file_load_t = void (*)(bool* ret, void* file, const void* path);
anymaker::cell_hook h_add_definitions;
self_fn_t g_file_ctor, g_file_dtor;
file_load_t g_file_load;
std::string g_json_path;                            // absolute, backslashes

// Writes the definition file into "AnyAPI and Modding/AnySuspension".
bool write_definition_file() {
    HMODULE self = nullptr;
    wchar_t dll[MAX_PATH]{};
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCWSTR)&write_definition_file, &self) ||
        !GetModuleFileNameW(self, dll, MAX_PATH)) return false;
    std::wstring dir(dll);
    dir = dir.substr(0, dir.find_last_of(L"\\/"));      // ...\AnyAPI and Modding\mods
    dir = dir.substr(0, dir.find_last_of(L"\\/"));      // ...\AnyAPI and Modding
    dir += L"\\AnySuspension";
    CreateDirectoryW(dir.c_str(), nullptr);
    std::wstring file = dir + L"\\anysuspension_parts.json";
    std::string json = definitions_json();
    bool ok = write_file(file, json.data(), json.size());
    int n = WideCharToMultiByte(CP_UTF8, 0, file.c_str(), -1, nullptr, 0, nullptr, nullptr);
    g_json_path.assign(size_t(n > 0 ? n - 1 : 0), '\0');
    if (n > 1) WideCharToMultiByte(CP_UTF8, 0, file.c_str(), -1, g_json_path.data(), n, nullptr, nullptr);
    return ok && !g_json_path.empty();
}

bool already_added(void* container) {
    if (!g_get_definition || !g_string_ctor) return false;
    anymaker::gc_string_view s{};
    g_string_ctor(&s, kSuspensionSlider);
    const void* found = nullptr;
    g_get_definition(&found, container, &s);
    return found != nullptr;
}

// Loads our JSON into a fresh vehicle_component_definition_file and hands it to the container. The path's
// characters must outlive the call ($string_ctor_cstr makes a non-owning view), so they are static. Other mods'
// hooks (GpsPart, AnyEngineParts) may sit under ours on this cell; each checks for its own parts before adding.
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
    if (ok) reinterpret_cast<add_definitions_t>(h_add_definitions.original)(container, file);
    g_file_dtor(file);
    return ok;
}

thread_local bool t_adding = false;
void hk_add_definitions(void* container, const void* file) {
    reinterpret_cast<add_definitions_t>(h_add_definitions.original)(container, file);
    if (!t_adding) { std::lock_guard<std::recursive_mutex> lock(g_mutex); g_is_slider.clear(); }   // definitions may move
    if (!g_ready.load() || t_adding || !container || already_added(container)) return;
    t_adding = true;
    std::string forward = g_json_path;
    for (char& c : forward) if (c == '\\') c = '/';
    bool ok = add_ours(container, g_json_path) || add_ours(container, forward);
    t_adding = false;
    static std::atomic<int> logs{6};
    if (logs.fetch_sub(1) > 0)
        log(ok ? 0 : 2, ok ? "Bearing, Bearing Ring, Suspension Rail and Suspension Slider added to the game's parts (%s)"
                           : "could not load the AnySuspension definitions from %s", g_json_path.c_str());
}

// ------------------------------------------------------------------------------------- saved slider settings
// In a file in the game's working folder (suspension_logic.h). The game swaps that folder's contents when a world
// loads, so the file is watched: a new file (or none) means another world.
SaveMap g_rest;
uint64_t g_rest_generation = 1;                      // bumped whenever the file is (re)loaded or dropped
std::wstring g_rest_path;                            // empty: rest positions are kept for this session only
struct FileStamp { bool exists = false; FILETIME created{}, written{}; uint64_t size = 0; };
FileStamp g_rest_stamp;
bool g_rest_dirty = false;
double g_rest_polled = -1e9, g_rest_written = -1e9;

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
void init_rest_path() {
    wchar_t buf[MAX_PATH];
    DWORD n = GetEnvironmentVariableW(L"APPDATA", buf, MAX_PATH);
    if (!n || n >= MAX_PATH) { log(1, "APPDATA not set; suspension sliders are not saved"); return; }
    std::wstring dir = std::wstring(buf) + L"\\Anymaker\\working";
    if (GetFileAttributesW(dir.c_str()) == INVALID_FILE_ATTRIBUTES && GetFileAttributesW((std::wstring(buf) + L"\\Anymaker").c_str()) == INVALID_FILE_ATTRIBUTES) {
        log(1, "the game's user folder (%%APPDATA%%\\Anymaker) was not found; suspension sliders are not saved");
        return;
    }
    g_rest_path = dir + L"\\anysuspension_sliders.json";
}
// Reloads when the world changed under us; writes ours a second after it changed. Server tick.
void sync_rest(double now) {
    if (g_rest_path.empty() || now - g_rest_polled < 0.5) return;
    g_rest_polled = now;
    FileStamp s = stamp_of(g_rest_path);
    if (!same_stamp(s, g_rest_stamp)) {
        if (s.exists) {
            g_rest = decode_sliders(read_file(g_rest_path));
            log(0, "suspension sliders loaded: %zu", g_rest.size());
        } else if (g_rest_stamp.exists) {
            g_rest.clear();                              // another world, without any yet
            log(0, "suspension slider file gone (another world); starting fresh");
        }
        g_rest_stamp = s;
        g_rest_dirty = false;
        ++g_rest_generation;
        return;
    }
    if (!g_rest_dirty || now - g_rest_written < 1.0) return;
    std::string text = encode_sliders(g_rest);
    std::wstring temp = g_rest_path + L".tmp";
    if (!write_file(temp, text.data(), text.size()) || !MoveFileExW(temp.c_str(), g_rest_path.c_str(), MOVEFILE_REPLACE_EXISTING)) {
        static std::atomic<int> logs{3};
        if (logs.fetch_sub(1) > 0) log(1, "could not write the suspension slider file (error %lu)", GetLastError());
        g_rest_written = now;
        return;
    }
    g_rest_stamp = stamp_of(g_rest_path);
    g_rest_written = now;
    g_rest_dirty = false;
}

// ------------------------------------------------------------------------------------- spring (server tick)
// The Jolt slider behind a physics.constraint.slider handle, or null while the slider is not attached.
uint8_t* jolt_slider(const uint8_t* handle) {
    const void* slot = pointer_at(handle, 8);
    const void* jolt = slot ? pointer_at(slot, 0) : nullptr;
    auto* p = static_cast<uint8_t*>(const_cast<void*>(jolt));
    return readable(p, off::jolt_target + 4) ? p : nullptr;
}
double mass_of(const void* vehicle) {
    if (!vehicle || !readable(static_cast<const uint8_t*>(vehicle) + off::vehicle_physics, 16)) return 0;
    const uint8_t* object = static_cast<const uint8_t*>(vehicle) + off::vehicle_physics;
    if (!pointer_at(object, 8) || !pointer_at(pointer_at(object, 8), 0)) return 0;
    double m = 0;
    g_get_mass(&m, object);
    return std::isfinite(m) && m > 0 ? m : 0;
}

struct SliderState {
    const void* jolt = nullptr;                      // the constraint seen last tick (a new one means reconnected)
    double last_seen = 0;
    bool layout_checked = false, layout_ok = false;
    int logs = 0;
    double* setting = nullptr;                       // [stiffness, damping], Properties Tool values: process heap, never
                                                     // freed (the Properties Tool holds pointers to them)
    double saved[2] = {-1, -1};                      // what the slider file holds for them
    uint64_t generation = 0;                         // the slider file generation they were loaded from
};
std::recursive_mutex g_slider_mutex;                 // ticks (server thread) and Properties Tool rows
std::unordered_map<const void*, SliderState> g_sliders;                              // keyed by slider component
std::unordered_map<const void*, std::unordered_map<const void*, double>> g_carried; // heavy vehicle -> sliders, last seen
double g_last_sweep = 0;

int springs_carrying(const void* heavy, const void* slider, double now) {
    auto& seen = g_carried[heavy];
    seen[slider] = now;
    int n = 0;
    for (auto& [s, t] : seen) if (now - t < 0.5) ++n;
    return n;
}

RestKey key_of(const void* self, const void* vehicle) {
    return RestKey{vehicle ? at<int32_t>(vehicle, off::vehicle_id) : 0, at<int32_t>(self, off::component_id)};
}

// The slider's Stiffness and Damping, loaded from the slider file when it (re)loads, or the defaults. Changes made
// with the Properties Tool are written back.
SliderState& slider_state(const void* self, const void* vehicle) {
    SliderState& st = g_sliders[self];
    if (!st.setting) {
        st.setting = static_cast<double*>(HeapAlloc(GetProcessHeap(), 0, 2 * sizeof(double)));
        if (st.setting) { st.setting[0] = kDefaultStiffness; st.setting[1] = kDefaultDamping; }
    }
    if (st.setting && vehicle && st.generation != g_rest_generation) {
        st.generation = g_rest_generation;
        auto found = g_rest.find(key_of(self, vehicle));
        if (found != g_rest.end()) {
            st.setting[0] = found->second.stiffness; st.setting[1] = found->second.damping;
            st.saved[0] = st.setting[0]; st.saved[1] = st.setting[1];
        }
    }
    return st;
}

void spring_tick(void* self, void* vehicle) {
    double now = now_seconds();
    std::lock_guard<std::recursive_mutex> lock(g_slider_mutex);
    sync_rest(now);
    SliderState& st = slider_state(self, vehicle);
    st.last_seen = now;
    double stiffness = st.setting ? clamp(st.setting[0], 0, 1) : kDefaultStiffness;
    double damping_factor = st.setting ? clamp(st.setting[1], 0, 1) : kDefaultDamping;
    const uint8_t* handle = static_cast<const uint8_t*>(self) + off::slider_constraint;
    uint8_t* jolt = readable(handle, 16) ? jolt_slider(handle) : nullptr;
    if (!jolt) { st.jolt = nullptr; return; }       // not attached to a rail

    // First sight: check the Jolt layout the way the game's set_motor writes it before writing to it ourselves.
    if (!st.layout_checked) {
        st.layout_checked = true;
        uint8_t mode = at<uint8_t>(jolt, off::jolt_spring_mode);
        int32_t state = at<int32_t>(jolt, off::jolt_motor_state);
        float lo = at<float>(jolt, off::jolt_limit_min), hi = at<float>(jolt, off::jolt_limit_max);
        st.layout_ok = mode <= 1 && state >= 0 && state <= 2 && std::isfinite(lo) && std::isfinite(hi) && hi >= lo && hi - lo < 1000.0f;
        if (!st.layout_ok) log(2, "slider %d: the physics joint does not look as expected (spring mode %u, motor state %d, limits %.3f to %.3f); this slider stays free",
                               at<int32_t>(self, off::component_id), unsigned(mode), int(state), double(lo), double(hi));
    }
    if (!st.layout_ok) return;

    bool has_limits = at<uint8_t>(jolt, off::jolt_has_limits) != 0;
    double lo = at<float>(jolt, off::jolt_limit_min), hi = at<float>(jolt, off::jolt_limit_max);
    double position = 0;
    g_get_position(&position, handle);
    if (!std::isfinite(position)) return;

    RestKey key = key_of(self, vehicle);
    auto found = g_rest.find(key);
    bool fresh = found == g_rest.end();
    if (fresh) {
        found = g_rest.emplace(key, SliderSave{rest_from_position(position, has_limits ? lo : 0.0), stiffness, damping_factor}).first;
        st.saved[0] = stiffness; st.saved[1] = damping_factor;
        g_rest_dirty = true;
    } else if (stiffness != st.saved[0] || damping_factor != st.saved[1]) {   // changed with the Properties Tool
        found->second.stiffness = st.saved[0] = stiffness;
        found->second.damping = st.saved[1] = damping_factor;
        g_rest_dirty = true;
        log(0, "slider %d: stiffness %.1f (%.2f Hz), damping %.1f", key.component, stiffness, frequency_from_factor(stiffness), damping_factor);
    }
    double target = has_limits ? target_from_rest(found->second.rest, lo, hi) : found->second.rest;

    const void* rail = pointer_at(self, off::slider_rail);
    const void* rail_vehicle = rail ? pointer_at(rail, off::component_vehicle) : nullptr;
    double mass_slider = mass_of(vehicle), mass_rail = mass_of(rail_vehicle);
    const void* heavy = mass_rail > mass_slider ? rail_vehicle : vehicle;
    int carried = springs_carrying(heavy, self, now);
    double share = sprung_share(mass_slider, mass_rail, carried);
    if (!(share > 0)) share = 100.0;                 // masses unreadable: a light default rather than none
    double frequency = frequency_from_factor(stiffness), ratio = damping_from_factor(damping_factor);
    Spring spring = spring_for(share, frequency, ratio);

    g_set_motor(handle, &frequency, &ratio, &target);   // position motor, target clamped to the rail's ends
    at<uint8_t>(jolt, off::jolt_spring_mode) = 1;        // stiffness and damping in N/m and N s/m
    at<float>(jolt, off::jolt_spring_value) = float(spring.stiffness);
    at<float>(jolt, off::jolt_spring_damping) = float(spring.damping);

    if (st.jolt != jolt || fresh) {
        st.jolt = jolt;
        if (st.logs++ < 6)
            log(0, "slider %d on vehicle %d: %s rest %.3f m along the rail (position %.3f, limits %s%.3f to %.3f), target %.3f; "
                   "masses %.0f kg (slider side) / %.0f kg (rail side), %d spring%s share %.0f kg; stiffness %.1f (%.2f Hz) damping %.1f: %.0f N/m, %.0f N s/m",
                key.component, key.vehicle, fresh ? "new," : "saved", found->second.rest, position, has_limits ? "" : "none, ", lo, hi, target,
                mass_slider, mass_rail, carried, carried == 1 ? "" : "s", share, stiffness, frequency, damping_factor, spring.stiffness, spring.damping);
    }
    if (now - g_last_sweep > 2.0) {                  // sliders gone from the scene stop ticking
        g_last_sweep = now;
        for (auto it = g_sliders.begin(); it != g_sliders.end();)
            if (now - it->second.last_seen > 30.0) it = g_sliders.erase(it); else ++it;
        for (auto it = g_carried.begin(); it != g_carried.end();) {
            for (auto c = it->second.begin(); c != it->second.end();) if (now - c->second > 2.0) c = it->second.erase(c); else ++c;
            if (it->second.empty()) it = g_carried.erase(it); else ++it;
        }
    }
}

// ------------------------------------------------------------------------------------- Properties Tool
// A Suspension Slider shows Stiffness and Damping rows, 0 to 1 in steps of 0.1, like the stock wheel's. The rail
// slider class has no rows of its own, so its method-table slot holds the base (empty) get_properties, whose cell
// every class without rows shares: the hook passes everything else straight through.
using get_properties_t = void (*)(void* self, void* properties);
using add_f64_range_t = void (*)(void* properties, const int32_t* label, double* const* value, const double* min, const double* max, const double* step);
constexpr int kAddPropertyF64RangeSlot = 10;          // server_scene.peer.property_data method table
enum : int32_t { kLocStiffness = 351, kLocDamping = 352 };   // e_localization_string property_stiffness, property_damping
anymaker::cell_hook h_properties;

void* container_method(void* container, int slot) {
    if (!container || !anymaker::readable(container, 8)) return nullptr;
    auto ti = anymaker::typeinfo_of(container);
    if (!ti.p || !anymaker::readable(ti.p + 0x80, 8) || !ti.table() || !anymaker::readable(ti.table() + slot, 8)) return nullptr;
    return ti.method(slot);
}

void hk_properties(void* self, void* properties) {
    reinterpret_cast<get_properties_t>(h_properties.original)(self, properties);
    if (!self || !properties || !g_ready.load() || !slider_definition(pointer_at(self, off::component_definition))) return;
    double* setting = nullptr;
    {
        std::lock_guard<std::recursive_mutex> lock(g_slider_mutex);
        setting = slider_state(self, pointer_at(self, off::component_vehicle)).setting;
    }
    auto add = setting ? reinterpret_cast<add_f64_range_t>(container_method(properties, kAddPropertyF64RangeSlot)) : nullptr;
    if (!add) return;
    double min = 0, max = 1, step = 0.1;
    int32_t label = kLocStiffness;
    add(properties, &label, &setting, &min, &max, &step);
    double* damping = setting + 1;
    label = kLocDamping;
    add(properties, &label, &damping, &min, &max, &step);
    static std::atomic<int> logs{4};
    if (logs.fetch_sub(1) > 0) log(0, "slider %d: Stiffness and Damping rows added (%.1f, %.1f)", at<int32_t>(self, off::component_id), setting[0], setting[1]);
}

using tick_t = void (*)(void* self, void* vehicle, void* scene);
anymaker::cell_hook h_tick;
void hk_tick(void* self, void* vehicle, void* scene) {
    reinterpret_cast<tick_t>(h_tick.original)(self, vehicle, scene);
    if (!self || !vehicle || !g_ready.load() || !slider_definition(pointer_at(self, off::component_definition))) return;
    spring_tick(self, vehicle);
}

// ------------------------------------------------------------------------------------- install (background thread)
bool executable(const void* p) {
    MEMORY_BASIC_INFORMATION r{};
    const DWORD x = PAGE_EXECUTE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY;
    return p && VirtualQuery(p, &r, sizeof(r)) && r.State == MEM_COMMIT && (r.Protect & x) && !(r.Protect & PAGE_GUARD);
}
// The call cell of a binding reached through a call route; every match of the route's root is followed and the
// cell is taken when all that lead to a live function agree (some sessions hold a second copy of the root code).
void** route_cell(const anymaker::func_desc& d) {
    if (!anymaker::experimental::matching_build()) return nullptr;
    if (void** cell = anymaker::cell_of(d)) return cell;
    const anymaker::slot_route& r = d.call_route;
    if (!r.nhops || r.nhops > 8 || !r.root_sig || !*r.root_sig) return nullptr;
    uint8_t* roots[8];
    size_t n = anymaker::find_gcl_functions(r.root_sig, roots, 8);
    void** found = nullptr;
    for (size_t i = 0; i < n; ++i) {
        uint8_t* fn = roots[i];
        void** cell = nullptr;
        for (uint8_t h = 0; fn && h < r.nhops; ++h) {
            cell = reinterpret_cast<void**>(anymaker::read_slot(fn, r.hops[h]));
            fn = anymaker::readable(cell, 8) ? reinterpret_cast<uint8_t*>(*cell) : nullptr;
        }
        if (!fn || !executable(fn)) continue;
        if (found && found != cell) return nullptr;
        found = cell;
    }
    return found;
}

// Identity checks for the definition functions, from their first code bytes (as Rotary): the add cell must hold
// _add_definitions (or another mod's hook on it), never container.load, and the load cell file.load.
constexpr const char* kSigAddDefinitions = "53 48 81 ec 90 00 00 00 48 89 4c 24 48 48 89 54 24 50 48 8b 44 24 28 48 89 44 24 58";
constexpr const char* kSigContainerLoad = "53 48 81 ec a0 00 00 00 48 89 4c 24 40 48 89 54 24 48 48 8d 01 48 8b 1d";
constexpr const char* kSigFileLoad = "53 55 48 81 ec 98 00 00 00 48 89 4c 24 48 48 89 54 24 50 4c 89 44 24 58";
// The three natives' first bytes (game.exe 0.1.25), so a different exe never gets called at these addresses.
constexpr const char* kSigSetMotor = "48 83 ec 28 48 8b 05 ?? ?? ?? ?? 48 33 c4 48 89 44 24 10 48 8b 41 08 f2 41 0f 10 08 f2 0f 10 02";
constexpr const char* kSigGetPosition = "40 53 48 83 ec 20 48 8b 42 08 48 8b d9 48 8b 08 e8";
constexpr const char* kSigGetMass = "48 89 5c 24 18 48 89 7c 24 20 55 48 81 ec e0 01 00 00 0f 29 b4 24 d0 01 00 00";
bool code_is(const void* fn, const char* sig) {
    anymaker::pattern pat(sig);
    return fn && !pat.bytes.empty() && anymaker::readable(fn, pat.bytes.size()) && pat.match(static_cast<const uint8_t*>(fn));
}
bool in_other_module(const void* fn) {
    HMODULE m = nullptr;
    return fn && GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCWSTR)fn, &m) &&
           m && m != GetModuleHandleW(nullptr);
}
bool add_cell_ok(void** cell) {
    void* fn = cell && anymaker::readable(cell, 8) ? *cell : nullptr;
    if (!fn || code_is(fn, kSigContainerLoad) || code_is(fn, kSigFileLoad)) return false;
    return code_is(fn, kSigAddDefinitions) || in_other_module(fn);
}

// The spring is optional: without it the suspension slider runs free like the stock rail slider.
void install_spring() {
    using namespace anymaker::experimental;
    void** tick = hook_cell(bind::rail_slider_tick);
    void* set_motor = native(natives::slider_set_motor_rva);
    void* get_position = native(natives::slider_get_position_rva);
    void* get_mass = native(natives::object_get_mass_rva);
    bool natives_ok = code_is(set_motor, kSigSetMotor) && code_is(get_position, kSigGetPosition) && code_is(get_mass, kSigGetMass);
    if (!tick || !natives_ok) {
        log(1, "suspension spring unavailable (slider tick %d, natives %d/%d/%d); suspension sliders run free",
            !!tick, code_is(set_motor, kSigSetMotor), code_is(get_position, kSigGetPosition), code_is(get_mass, kSigGetMass));
        return;
    }
    g_set_motor = reinterpret_cast<set_motor_t>(set_motor);
    g_get_position = reinterpret_cast<get_position_t>(get_position);
    g_get_mass = reinterpret_cast<get_mass_t>(get_mass);
    init_rest_path();
    if (!h_tick.install(tick, (void*)&hk_tick)) { log(1, "rail slider tick hook not installed; suspension sliders run free"); return; }
    void** properties = hook_cell(bind::rail_slider_get_properties);
    if (!properties || !h_properties.install(properties, (void*)&hk_properties))
        log(1, "Properties Tool rows unavailable; suspension sliders use stiffness %.1f and damping %.1f", kDefaultStiffness, kDefaultDamping);
    log(0, "suspension spring on");
}

void install_hooks() {
    using namespace anymaker::experimental;
    void **add = nullptr, **load_cell = nullptr;
    self_fn_t fctor = nullptr, fdtor = nullptr;
    file_load_t fload = nullptr;
    string_ctor_t sc = nullptr;
    for (int attempt = 0; attempt < 180 && !g_stop; ++attempt) {
        if (attempt) Sleep(attempt < 30 ? 1000 : 5000);
        if (!matching_build()) { if (attempt == 0) log(1, "game build does not match the SDK reference; AnySuspension stays off"); return; }
        if (!add) add = route_cell(bind::definitions_add_definitions);
        if (!fctor) fctor = reinterpret_cast<self_fn_t>(function(bind::definition_file_ctor));
        if (!fdtor) fdtor = reinterpret_cast<self_fn_t>(function(bind::definition_file_dtor));
        if (!load_cell) load_cell = route_cell(bind::definition_file_load);
        if (!fload && load_cell) fload = reinterpret_cast<file_load_t>(*load_cell);
        if (add && !add_cell_ok(add)) add = nullptr;
        if (fload && !code_is((void*)fload, kSigFileLoad)) { fload = nullptr; load_cell = nullptr; }
        if (!sc) sc = reinterpret_cast<string_ctor_t>(native(anymaker::sym::string_ctor_cstr_rva));
        if (!(add && fctor && fdtor && fload && sc)) {
            if (attempt == 179) log(2, "could not locate the game functions (add=%d file=%d/%d/%d string=%d)", !!add, !!fctor, !!fdtor, !!fload, !!sc);
            continue;
        }
        g_file_ctor = fctor; g_file_dtor = fdtor; g_file_load = fload; g_string_ctor = sc;
        g_get_definition = reinterpret_cast<get_definition_t>(function(bind::definitions_get_by_id));
        if (!g_get_definition) log(1, "definition lookup not found; the parts may be added twice if another mod re-adds definitions");
        if (!write_definition_file()) { log(2, "could not write the AnySuspension definition file; AnySuspension is off"); return; }
        if (!add_cell_ok(add) || !h_add_definitions.install(add, (void*)&hk_add_definitions)) {
            log(2, "the definitions hook cell was taken or changed; AnySuspension is off");
            return;
        }
        install_spring();
        g_ready = true;
        log(0, "Ready after %d s. Load or start a world to add the parts (definitions %s).", attempt, g_json_path.c_str());
        return;
    }
}
void remove_hooks() {
    h_properties.remove((void*)&hk_properties);
    h_tick.remove((void*)&hk_tick);
    h_add_definitions.remove((void*)&hk_add_definitions);
}
}  // namespace
}  // namespace suspension

extern "C" __declspec(dllexport) bool AnyAPI_ModInit(const AnyModHostV1* h, AnyModCallbacksV1* out) {
    if (!h || !out || h->struct_size != sizeof(*h) || h->abi != ANYAPI_MOD_ABI || out->struct_size != sizeof(*out)) return false;
    suspension::host = *h;
    out->id = suspension::kModId;
    out->shutdown = [](void*) { suspension::g_stop = true; suspension::remove_hooks(); };
    return true;
}
extern "C" __declspec(dllexport) void AnyAPI_ModReady() {
    // Resolving scans game memory: keep it off the loader and frame threads.
    std::thread([] { suspension::install_hooks(); }).detach();
}
