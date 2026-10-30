// AnyMoto: motorcycle parts for the stock Motorcycle Wheel item.
//
//   Motorcycle Fork      a wheel part placed on the front face of a bike's frame: steers from the steering links,
//                        brakes from the brake link, 14 cm of travel; raked sliding fork legs and a mudguard.
//   Motorcycle Swingarm  a wheel part placed on the back face: drive from the torque face on its side at the pivot
//                        (a pulley wheel there takes a belt from an engine wheel), 12 cm of travel; the arm swings
//                        and the shock compresses.
//   Lean Assist          keeps a two-wheeler upright and leans it into turns (Power in the Properties tool, data
//                        input "assist" switches it off).
//   Motorcycle Seat      a saddle with the seat's control faces.
//   Handlebars           the steering wheel's links on bars that turn about a raked axis, hands on the grips.
//
// How: the definitions are written to "AnyAPI and Modding/AnyMoto" and added to the game's part definitions after
// the game adds its own (as GpsPart does); the meshes are generated (anymoto_mesh.h) into rom/meshes/components.
// The game gives a wheel its code-side tuning (vehicle_element_definition.wheel) by definition id and knows only
// its own ids, so the fork and swingarm get copies of the stock wheel's, set on the server after
// assign_definition and on the client before the wheel and its element are built (an unset client tuning is what
// crashed AnySuspension's hub in 0.1.25). The copies accept only the Motorcycle Wheel and carry its radius.
// Wheels, seats, steering wheels and gyro sensors keep their stock behaviour: every hook passes other parts through.
//
// EXPERIMENTAL. Hooks game functions through the experimental SDK (anymoto_bindings.h): Anymaker 0.1.27 /
// Steam build 26113663 only. First runs are diagnostic: read anymaker_modding.log.
#define NOMINMAX
#include <windows.h>
#include "anyapi_services_v1.h"
#include "anyapi_experimental.hpp"
#include "anymoto_bindings.h"
#include "route_cell.h"
#include "anymoto_logic.h"
#include "anymoto_mesh.h"
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
#include <vector>

namespace anymoto {
namespace {
constexpr const char* kModId = "anymoto";

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
template <class T> T& at(void* base, ptrdiff_t offset) { return *reinterpret_cast<T*>(static_cast<uint8_t*>(base) + offset); }
template <class T> T at(const void* base, ptrdiff_t offset) { return *reinterpret_cast<const T*>(static_cast<const uint8_t*>(base) + offset); }

// Reference layouts (0.1.27 metadata).
namespace off {
constexpr ptrdiff_t server_component_definition = 312;   // server_scene.vehicle_component.m_definition
constexpr ptrdiff_t server_component_vehicle = 320;      // .m_parent_vehicle
constexpr ptrdiff_t server_wheel_tuning = 480 + 8;       // server wheel: m_element_wheel (480) .m_definition (+8)
constexpr ptrdiff_t server_wheel_steering = 480 + 0x1f0; // .m_element_wheel.m_value_steering (f64, the smoothed steering input)
constexpr ptrdiff_t client_component_definition = 176;   // client_scene.vehicle_component.m_definition
constexpr ptrdiff_t client_wheel_element = 224;          // client wheel: m_element_wheel
constexpr ptrdiff_t client_wheel_tuning = 224 + 8;       // .m_definition
constexpr ptrdiff_t client_steering_display = 368;       // client steering_wheel.m_display_steering (f64)
constexpr ptrdiff_t definition_id = 0;                   // vehicle_component_definition.m_id (string)
constexpr ptrdiff_t server_vehicle_physics = 2320;       // server_scene.vehicle.m_physics (physics.object)
// vehicle_element_definition.wheel (328 bytes)
constexpr size_t tuning_size = 328;
constexpr ptrdiff_t tuning_radius = 8, tuning_position = 16, tuning_rotation = 40, tuning_steering_position = 80;
constexpr ptrdiff_t tuning_suspension_length = 176, tuning_steer_min = 208, tuning_steer_max = 216;
constexpr ptrdiff_t tuning_tyres = 280;                  // vector<string> m_compatible_tyre_definitions
// tyre_data: string id, f64 radius (+16), f64 thickness (+24); 32 bytes. The container is one vector<tyre_data>.
constexpr size_t tyre_data_size = 32;
// client_scene.actor_character.util.deferred_transform: s32 type, ..., mat34 transform at +24.
constexpr ptrdiff_t deferred_type = 0, deferred_transform = 24;
}  // namespace off
constexpr int32_t kStoreSystem = 1;

using string_ctor_t = void (*)(anymaker::gc_string_view*, const char*);
string_ctor_t g_string_ctor;
using get_definition_t = void (*)(const void** ret, const void* container, const anymaker::gc_string_view* id);
get_definition_t g_get_definition;

bool can_read(const void* p, size_t n) { return p && anymaker::readable(p, n); }

// ================================================================== which parts are ours
// Keyed by definition pointer; every world loads its definitions anew, so the cache starts afresh on each add.
std::mutex g_kind_mutex;
std::unordered_map<const void*, Kind> g_kinds;
Kind kind_of_definition(const void* definition) {
    if (!definition) return Kind::other;
    std::lock_guard lock(g_kind_mutex);
    auto found = g_kinds.find(definition);
    if (found != g_kinds.end()) return found->second;
    Kind k = Kind::other;
    if (can_read(static_cast<const uint8_t*>(definition) + off::definition_id, sizeof(anymaker::gc_string_view))) {
        auto* id = reinterpret_cast<const anymaker::gc_string_view*>(static_cast<const uint8_t*>(definition) + off::definition_id);
        if (id->data && id->length > 0 && id->length < 64 && can_read(id->data, size_t(id->length))) k = kind_of_id(id->data, size_t(id->length));
    }
    g_kinds[definition] = k;
    return k;
}
// Known without reading memory: only definitions already seen (used where the pointer may not be a definition).
Kind known_kind(const void* definition) {
    std::lock_guard lock(g_kind_mutex);
    auto found = g_kinds.find(definition);
    return found == g_kinds.end() ? Kind::other : found->second;
}
void forget_definitions() { std::lock_guard lock(g_kind_mutex); g_kinds.clear(); }
Kind server_kind(const void* component) {
    return component && can_read(static_cast<const uint8_t*>(component) + off::server_component_definition, 8)
        ? kind_of_definition(at<const void*>(component, off::server_component_definition)) : Kind::other;
}
Kind client_kind(const void* component) {
    return component && can_read(static_cast<const uint8_t*>(component) + off::client_component_definition, 8)
        ? kind_of_definition(at<const void*>(component, off::client_component_definition)) : Kind::other;
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

std::string g_json_path;   // absolute, backslashes
bool write_files() {
    HMODULE self = nullptr;
    wchar_t dll[MAX_PATH]{};
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCWSTR)&write_files, &self) ||
        !GetModuleFileNameW(self, dll, MAX_PATH)) return false;
    std::wstring dir(dll);
    dir = dir.substr(0, dir.find_last_of(L"\\/"));      // ...\AnyAPI and Modding\mods
    dir = dir.substr(0, dir.find_last_of(L"\\/"));      // ...\AnyAPI and Modding
    std::wstring game_dir = dir.substr(0, dir.find_last_of(L"\\/"));
    std::wstring components = game_dir + L"\\rom\\meshes\\components\\";
    struct Out { const char* name; Mesh mesh; std::string* path; };
    MeshPaths paths;
    Out meshes[] = {
        {"anymoto_fork_base", fork_base(), &paths.fork_base}, {"anymoto_fork_steer", fork_steer(), &paths.fork_steer},
        {"anymoto_fork_lower", fork_lower(), &paths.fork_lower}, {"anymoto_swingarm_base", swingarm_base(), &paths.swingarm_base},
        {"anymoto_swingarm_arm", swingarm_arm(), &paths.swingarm_arm}, {"anymoto_swingarm_shock", swingarm_shock(), &paths.swingarm_shock},
        {"anymoto_hidden", hidden_speck(), &paths.hidden}, {"anymoto_lean_assist", lean_assist(), &paths.lean_assist},
        {"anymoto_seat", seat(), &paths.seat}, {"anymoto_handlebars_base", handlebars_base(), &paths.handlebars_base},
        {"anymoto_handlebars_bars", handlebars_bars(), &paths.handlebars_bars},
    };
    for (auto& m : meshes) {
        std::vector<uint8_t> bytes = encode_mesh(m.mesh, m.name);
        std::wstring file = components + std::wstring(m.name, m.name + std::strlen(m.name)) + L".mesh";
        if (!write_file(file, bytes.data(), bytes.size())) { log(2, "could not write %s.mesh into rom\\meshes\\components; AnyMoto is off", m.name); return false; }
        *m.path = std::string("meshes/components/") + m.name + ".mesh";
    }
    dir += L"\\AnyMoto";
    CreateDirectoryW(dir.c_str(), nullptr);
    std::wstring file = dir + L"\\anymoto_parts.json";
    std::string json = definitions_json(paths);
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
void hk_add_definitions(void* container, const void* file) {
    reinterpret_cast<add_definitions_t>(h_add_definitions.original)(container, file);
    if (!g_ready.load() || g_adding == std::this_thread::get_id() || !container || find_definition(container, kForkId)) return;
    forget_definitions();   // a new set of definitions (a world load): pointers seen before may be reused
    std::string forward = g_json_path;
    for (char& c : forward) if (c == '\\') c = '/';
    bool ok = add_ours(container, g_json_path) || add_ours(container, forward);
    // Remember our definitions, so hooks that cannot read a definition safely still know them.
    int found = 0;
    for (const char* id : {kForkId, kSwingarmId, kLeanAssistId, kSeatId, kHandlebarsId})
        if (const void* d = find_definition(container, id)) { kind_of_definition(d); ++found; }
    static std::atomic<int> logs{6};
    if (logs.fetch_sub(1) > 0)
        log(ok ? 0 : 2, ok ? "AnyMoto parts added to the game's parts (%d of 5 found after adding; %s)" : "could not load the AnyMoto definitions from %s",
            ok ? found : 0, g_json_path.c_str());
}

// ================================================================== wheel tuning
// Copies of the stock wheel's tuning for the fork and swingarm. Never freed: wheels keep pointing at them. Each has
// its own one-entry tyre list (the Motorcycle Wheel), built over the stock list's header; the game only reads it.
const uint8_t* g_stock_tuning;
uint8_t* g_tuning[2];                  // fork, swingarm
anymaker::gc_string_view* g_tyre_list;
std::once_flag g_tuning_once;
int32_t g_rotation[2][9];              // the copies' m_rotation, for drawing

void make_tuning() {
    std::call_once(g_tuning_once, [] {
        if (!can_read(g_stock_tuning, off::tuning_size)) return;
        auto* stock_list = reinterpret_cast<const anymaker::gc_vector_raw*>(g_stock_tuning + off::tuning_tyres);
        if (stock_list->element_size != sizeof(anymaker::gc_string_view)) { log(2, "stock tyre list element size %u, expected 16; fork and swingarm left out", stock_list->element_size); return; }
        g_tyre_list = static_cast<anymaker::gc_string_view*>(HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(anymaker::gc_string_view)));
        if (!g_tyre_list) return;
        g_string_ctor(g_tyre_list, kMotorcycleTyre);
        const double stock_min = at<double>(g_stock_tuning, off::tuning_steer_min), stock_max = at<double>(g_stock_tuning, off::tuning_steer_max);
        const int32_t* stock_rotation = reinterpret_cast<const int32_t*>(g_stock_tuning + off::tuning_rotation);
        for (int part = 0; part < 2; ++part) {
            WheelTuning t = part == 0 ? fork_tuning() : swingarm_tuning();
            auto* copy = static_cast<uint8_t*>(HeapAlloc(GetProcessHeap(), 0, off::tuning_size));
            if (!copy) return;
            std::memcpy(copy, g_stock_tuning, off::tuning_size);
            at<double>(copy, off::tuning_radius) = std::max(at<double>(copy, off::tuning_radius), t.radius);
            double pos[3] = {t.tyre.x, t.tyre.y, t.tyre.z};
            std::memcpy(copy + off::tuning_position, pos, sizeof pos);
            std::memcpy(copy + off::tuning_steering_position, pos, sizeof pos);
            quarter_turn_y(stock_rotation, g_rotation[part]);
            std::memcpy(copy + off::tuning_rotation, g_rotation[part], sizeof g_rotation[part]);
            at<double>(copy, off::tuning_suspension_length) = t.travel;
            // Keep the stock sign convention (min may be the negative side or the positive one).
            double scale = std::abs(stock_max) > 1e-6 ? rad(t.steer_deg) / std::abs(stock_max) : 0;
            at<double>(copy, off::tuning_steer_min) = stock_min * scale;
            at<double>(copy, off::tuning_steer_max) = stock_max * scale;
            auto* list = reinterpret_cast<anymaker::gc_vector_raw*>(copy + off::tuning_tyres);
            list->buffer = reinterpret_cast<uint8_t*>(g_tyre_list);
            list->offset = 0; list->count = 1; list->capacity = 1;
            g_tuning[part] = copy;
        }
        const double* p = reinterpret_cast<const double*>(g_stock_tuning + off::tuning_position);
        log(0, "wheel tuning: stock radius %.3f travel %.3f steer %.3f..%.3f rad, tyre at (%.3f, %.3f, %.3f), rotation [%d %d %d | %d %d %d | %d %d %d]; "
               "fork and swingarm accept %s only", at<double>(g_stock_tuning, off::tuning_radius), at<double>(g_stock_tuning, off::tuning_suspension_length),
            stock_min, stock_max, p[0], p[1], p[2], stock_rotation[0], stock_rotation[1], stock_rotation[2], stock_rotation[3], stock_rotation[4],
            stock_rotation[5], stock_rotation[6], stock_rotation[7], stock_rotation[8], kMotorcycleTyre);
    });
}
const void* tuning_for(Kind k) {
    if (k != Kind::fork && k != Kind::swingarm) return nullptr;
    make_tuning();
    return g_tuning[k == Kind::fork ? 0 : 1];
}

// The tyre size table has no Motorcycle Wheel. If the vector has a free slot, the entry is added in place (the game
// owns and frees the memory as before); otherwise the tuning's m_radius carries the size (the wheel takes the larger).
void add_tyre_size(void* container) {
    auto* v = static_cast<anymaker::gc_vector_raw*>(container);
    if (!can_read(v, sizeof *v) || v->element_size != off::tyre_data_size || v->count <= 0 || v->capacity <= 0 || v->count > v->capacity) {
        log(1, "tyre size table not readable; the Motorcycle Wheel's size comes from the fork and swingarm tuning");
        return;
    }
    for (int32_t i = 0; i < v->count; ++i) {
        auto* id = reinterpret_cast<anymaker::gc_string_view*>(v->buffer + size_t((int64_t(v->offset) + i) % v->capacity) * v->element_size);
        if (can_read(id, 16) && id->view() == kMotorcycleTyre) return;
    }
    if (v->count == v->capacity) {
        log(0, "tyre size table full (%d); the Motorcycle Wheel's size comes from the fork and swingarm tuning", v->count);
        return;
    }
    uint8_t* slot = v->buffer + size_t((int64_t(v->offset) + v->count) % v->capacity) * v->element_size;
    std::memset(slot, 0, off::tyre_data_size);
    g_string_ctor(reinterpret_cast<anymaker::gc_string_view*>(slot), kMotorcycleTyre);
    at<double>(slot, 16) = kMotorcycleTyreRadius;
    at<double>(slot, 24) = kMotorcycleTyreWidth;
    InterlockedIncrement(reinterpret_cast<volatile LONG*>(&v->count));
    log(0, "Motorcycle Wheel added to the tyre size table (radius %.2f m, width %.2f m)", kMotorcycleTyreRadius, kMotorcycleTyreWidth);
}

// Server: the game sets the tuning by id; ours get theirs after.
using assign_t = void (*)(void** component);
anymaker::cell_hook h_assign;
// Our server wheels by vehicle, so the Lean Assist can find where the tyres touch the ground. Entries are checked
// before use (still ours, still on that vehicle) and dropped when not.
std::mutex g_wheels_mutex;
std::unordered_map<const void*, std::vector<void*>> g_wheels;
void remember_wheel(void* component) {
    if (!can_read(static_cast<uint8_t*>(component) + off::server_component_vehicle, 8)) return;
    const void* vehicle = at<const void*>(component, off::server_component_vehicle);
    if (!vehicle) return;
    std::lock_guard lock(g_wheels_mutex);
    auto& list = g_wheels[vehicle];
    if (std::find(list.begin(), list.end(), component) == list.end()) list.push_back(component);
}
void hk_assign(void** component) {
    reinterpret_cast<assign_t>(h_assign.original)(component);
    void* self = component && can_read(component, 8) ? *component : nullptr;
    if (!self) return;
    const void* tuning = tuning_for(server_kind(self));
    if (!tuning || !can_read(static_cast<uint8_t*>(self) + off::server_wheel_tuning, 8)) return;
    at<const void*>(self, off::server_wheel_tuning) = tuning;
    remember_wheel(self);
    static std::atomic<int> logs{4};
    if (logs.fetch_sub(1) > 0) log(0, "server: %s tuning set", server_kind(self) == Kind::fork ? "fork" : "swingarm");
}

// Client: set before the game builds the wheel (it reads the tuning while building and skips ids it does not know).
using client_create_t = void (*)(void* self, const void* container, void* vehicle, void* grid);
anymaker::cell_hook h_client_create;
void hk_client_create(void* self, const void* container, void* vehicle, void* grid) {
    if (const void* tuning = tuning_for(client_kind(self)); tuning && can_read(static_cast<uint8_t*>(self) + off::client_wheel_tuning, 8)) {
        at<const void*>(self, off::client_wheel_tuning) = tuning;
        static std::atomic<int> logs{4};
        if (logs.fetch_sub(1) > 0) log(0, "client: %s tuning set before the wheel is built", client_kind(self) == Kind::fork ? "fork" : "swingarm");
    }
    reinterpret_cast<client_create_t>(h_client_create.original)(self, container, vehicle, grid);
}
// The element's own create, as a second guard: the element sits at +224 in a wheel component. Only definitions
// already known as ours are trusted here (other wheel-element owners keep the element elsewhere).
using element_create_t = void (*)(void* element, void* vehicle);
anymaker::cell_hook h_element_create;
void hk_element_create(void* element, void* vehicle) {
    auto* component = static_cast<uint8_t*>(element) - off::client_wheel_element;
    if (element && can_read(component + off::client_component_definition, 8) && can_read(static_cast<uint8_t*>(element) + 8, 8)) {
        Kind k = known_kind(at<const void*>(component, off::client_component_definition));
        const void* tuning = (k == Kind::fork || k == Kind::swingarm) ? tuning_for(k) : nullptr;
        if (tuning && at<const void*>(element, 8) != tuning) {
            at<const void*>(element, 8) = tuning;
            log(1, "client: wheel element of a %s had no tuning when built; set it", k == Kind::fork ? "fork" : "swingarm");
        }
    }
    reinterpret_cast<element_create_t>(h_element_create.original)(element, vehicle);
}

// ================================================================== drawing
using mat_of_t = void (*)(Mat34* ret, const void* object);
using render_t = void (*)(const void* self, const Mat34* transform, void* out);
using mesh_by_index_t = void (*)(const void** ret, const void* definition, const int32_t* index);
using add_mesh_t = void (*)(void* out, const void* mesh_dynamic, const Mat34* transform);
mat_of_t g_client_component_transform, g_accessory_transform, g_steering_transform, g_vehicle_render_transform;
render_t g_element_render;
mesh_by_index_t g_mesh_by_index;
add_mesh_t g_add_mesh;
anymaker::cell_hook h_wheel_render, h_steering_render;

void add_dynamic(void* out, const void* definition, int32_t index, const Mat34& transform) {
    if (!finite(transform)) return;
    const void* mesh = nullptr;
    g_mesh_by_index(&mesh, definition, &index);
    if (mesh) g_add_mesh(out, mesh, &transform);
}
Mat34 rotation_of(const int32_t r[9]) {
    Mat34 m;
    for (int i = 0; i < 9; ++i) m.m[i] = r[i];
    return m;
}
Mat34 transpose_rotation(const Mat34& a) {
    Mat34 t;
    for (int c = 0; c < 3; ++c) for (int r = 0; r < 3; ++r) t.m[c * 3 + r] = a.m[r * 3 + c];
    return t;
}

void hk_wheel_render(const void* self, const Mat34* transform, void* out) {
    Kind k = g_ready.load() ? client_kind(self) : Kind::other;
    if ((k != Kind::fork && k != Kind::swingarm) || !transform || !out) {
        reinterpret_cast<render_t>(h_wheel_render.original)(self, transform, out);
        return;
    }
    const void* definition = at<const void*>(self, off::client_component_definition);
    const void* element = static_cast<const uint8_t*>(self) + off::client_wheel_element;
    Mat34 component, accessory, steering;
    g_client_component_transform(&component, self);
    g_accessory_transform(&accessory, element);
    g_steering_transform(&steering, element);
    Mat34 render = compose(*transform, component);
    Vec3 tyre = column(accessory, 3);
    if (k == Kind::fork) {
        // The game steers the tyre by R * rotation_y(angle) in the tuning's rotated frame; the same in the part frame.
        Mat34 r = rotation_of(g_rotation[0]);
        Mat34 steer = compose(compose(r, rotation_y(steering_angle_of(steering))), transpose_rotation(r));
        ForkFrames f = fork_frames(tyre, steering_angle_of(steer));
        add_dynamic(out, definition, 0, compose(render, f.upper));
        add_dynamic(out, definition, 1, compose(render, f.lower));
    } else {
        add_dynamic(out, definition, 0, compose(render, compose(translation(kSwingarmPivot), rotation_x(swingarm_angle(tyre)))));
        Vec3 along = tyre - kSwingarmPivot;
        Vec3 lug = kSwingarmPivot + normalized(along) * (swingarm_length() * kShockAlongArm) + normalized(cross(Vec3{1, 0, 0}, along)) * -0.045;
        add_dynamic(out, definition, 1, compose(render, stretch_between(kShockTop, lug)));
    }
    g_element_render(element, &render, out);
    static std::atomic<int> logs{2};
    if (logs.fetch_sub(1) > 0)
        log(0, "client: drawing a %s, tyre centre (%.3f, %.3f, %.3f), steering %.1f deg", k == Kind::fork ? "fork" : "swingarm",
            tyre.x, tyre.y, tyre.z, steering_angle_of(steering) * 180 / kPi);
}

// Handlebars: the game's m_display_steering is the steering wheel's turn; the bars use it scaled to their lock,
// learning the largest turn seen (at least 0.5) so a full turn of input is full lock.
std::atomic<double> g_display_max{0.5};
double bars_steering(const void* self) {
    double d = at<double>(self, off::client_steering_display);
    if (!std::isfinite(d)) return 0;
    double m = g_display_max.load();
    if (std::abs(d) > m) { m = std::min(std::abs(d), 10.0); g_display_max = m; }
    return std::clamp(d / m, -1.0, 1.0);
}
void hk_steering_render(const void* self, const Mat34* transform, void* out) {
    if (!g_ready.load() || client_kind(self) != Kind::handlebars || !transform || !out) {
        reinterpret_cast<render_t>(h_steering_render.original)(self, transform, out);
        return;
    }
    Mat34 component;
    g_client_component_transform(&component, self);
    add_dynamic(out, at<const void*>(self, off::client_component_definition), 0, compose(compose(*transform, component), bars_frame(bars_steering(self))));
}

// Seated hands: the game puts them on a steering wheel's rim; on handlebars they go to the grips (positions only,
// the game's hand rotation is kept).
using hand_pose_t = void (*)(const void* self, const void* vehicle, bool* is_r, void* hand_r, int32_t* pose_r, bool* is_l, void* hand_l, int32_t* pose_l);
anymaker::cell_hook h_hand_pose;
void hk_hand_pose(const void* self, const void* vehicle, bool* is_r, void* hand_r, int32_t* pose_r, bool* is_l, void* hand_l, int32_t* pose_l) {
    reinterpret_cast<hand_pose_t>(h_hand_pose.original)(self, vehicle, is_r, hand_r, pose_r, is_l, hand_l, pose_l);
    if (!g_ready.load() || client_kind(self) != Kind::handlebars || !vehicle || !g_vehicle_render_transform) return;
    Mat34 vt, component;
    g_vehicle_render_transform(&vt, vehicle);
    g_client_component_transform(&component, self);
    Mat34 bars = compose(compose(vt, component), bars_frame(bars_steering(self)));
    auto place = [&](bool* is, void* hand, double side) {
        if (!is || !*is || !can_read(hand, off::deferred_transform + sizeof(Mat34))) return;
        Vec3 grip = apply(bars, {kGripRight.x * side, kGripRight.y, kGripRight.z});
        auto& m = at<Mat34>(hand, off::deferred_transform);
        if (!finite(m)) return;
        m.m[9] = grip.x; m.m[10] = grip.y; m.m[11] = grip.z;
    };
    place(is_r, hand_r, 1);    // the game's frames are left-handed: facing +z with y up, the rider's right is +x
    place(is_l, hand_l, -1);
    static std::atomic<int> logs{2};
    if (logs.fetch_sub(1) > 0)
        log(0, "client: hands on the handlebar grips (hand transform types %d / %d)", hand_r ? at<int32_t>(hand_r, off::deferred_type) : -1,
            hand_l ? at<int32_t>(hand_l, off::deferred_type) : -1);
}

// ================================================================== lean assist (server)
using physics_vec_t = void (*)(Vec3* ret, const void* object);
using physics_set_vec_t = void (*)(void* object, const Vec3* value);
physics_vec_t g_get_angular, g_get_linear;
physics_set_vec_t g_set_angular, g_set_linear;
mat_of_t g_server_vehicle_transform, g_server_component_transform;
physics_vec_t g_get_com;
// Where the bike's tyres touch the ground, in the world: the middle of our fork's and swingarm's contact points.
// The rider's steering, from the fork(s): the wheel's smoothed steering input, about -1..1. NaN = no fork.
double fork_steering(void* vehicle) {
    std::vector<void*> list;
    {
        std::lock_guard lock(g_wheels_mutex);
        auto found = g_wheels.find(vehicle);
        if (found != g_wheels.end()) list = found->second;
    }
    double sum = 0;
    int n = 0;
    for (void* c : list) {
        if (!can_read(static_cast<uint8_t*>(c) + off::server_component_definition, 16) || at<const void*>(c, off::server_component_vehicle) != vehicle) continue;
        if (known_kind(at<const void*>(c, off::server_component_definition)) != Kind::fork) continue;
        if (!can_read(static_cast<uint8_t*>(c) + off::server_wheel_steering, 8)) continue;
        double v = at<double>(c, off::server_wheel_steering);
        if (std::isfinite(v)) { sum += std::clamp(v, -1.0, 1.0); ++n; }
    }
    return n ? sum / n : std::numeric_limits<double>::quiet_NaN();
}

bool contact_point(void* vehicle, const Mat34& vehicle_transform, Vec3* out) {
    std::vector<void*> list;
    {
        std::lock_guard lock(g_wheels_mutex);
        auto found = g_wheels.find(vehicle);
        if (found == g_wheels.end()) return false;
        auto& l = found->second;
        l.erase(std::remove_if(l.begin(), l.end(), [&](void* c) {
            if (!can_read(static_cast<uint8_t*>(c) + off::server_component_definition, 16)) return true;
            Kind k = known_kind(at<const void*>(c, off::server_component_definition));
            return (k != Kind::fork && k != Kind::swingarm) || at<const void*>(c, off::server_component_vehicle) != vehicle;
        }), l.end());
        list = l;
    }
    Vec3 sum{};
    int n = 0;
    for (void* c : list) {
        Mat34 local;
        g_server_component_transform(&local, c);
        Mat34 world = compose(vehicle_transform, local);
        if (!finite(world)) continue;
        Kind k = known_kind(at<const void*>(c, off::server_component_definition));
        Vec3 tyre = apply(world, (k == Kind::fork ? fork_tuning() : swingarm_tuning()).tyre);
        sum = sum + tyre - Vec3{0, kMotorcycleTyreRadius, 0};
        ++n;
    }
    if (!n) return false;
    *out = sum * (1.0 / n);
    return true;
}

struct Assist {
    double power = 0.6;            // Properties: Power (0..1); per session
    double turn_lean = 1;          // Properties: Roll sensitivity (0..2), how far it leans into turns; per session
    double slots[4] = {0, 1, 0, 0};// data: rps (stock), assist input (unconnected = on), lean, target_lean
    double last = 0;
    // Learning which way the physics spins: the lean change seen against the one the read roll rate predicts.
    bool have_prev = false;
    double prev_lean = 0, prev_set_wf = 0;
    double agree = 0, weight = 0;  // decaying sums of observed * predicted and predicted^2
    double sign = 1;
    double kept = 0, kept_n = 0;   // how much of the roll rate we set is still there next tick (1 = all), for the log
    double kept_ema = 1;           // the same, smoothed: below 0 the physics throws the roll back (a rider on board)
    double gain = 1;               // full strength always: backing off with a rider on left the bike to fall
    double rate_f = 0;             // roll rate from the bike's own lean change, lightly smoothed
    double yaw_f = 0;              // yaw rate, smoothed over about 0.4 s
    double steer_agree = 0;        // decaying sum of steering x yaw: which way the steering turns the bike
    bool have_rate = false;
    double next_log = 0;
};
std::mutex g_assist_mutex;
std::unordered_map<const void*, Assist> g_assists;   // keyed by server component; nodes keep their address
int slot_of(std::string_view name) {
    if (name == "assist") return 1;
    if (name == "lean") return 2;
    if (name == "target_lean") return 3;
    return -1;
}

// A seated rider is a separate physics body (their capsule) welded to the vehicle's body. Spinning the bike alone
// stretches the weld, and the weld throws the spin back next tick: the assist fights the rider and the tyres skate.
// The seat tick names both bodies, so the assist can move the rider's capsule with the bike as one piece.
struct Rider {
    uint64_t vehicle_body[2];      // the vehicle's physics.object handle, as the seat keeps a copy of it
    void* capsule;                 // the rider's physics.object
    const void* seat;              // the seat_data that welds it
    double seen;
};
std::mutex g_rider_mutex;
std::vector<Rider> g_riders;
namespace seat_off {
constexpr ptrdiff_t seat_physics = 0xa0;   // server_scene.actor.seat_data._m_seat_physics (copy of the vehicle's body while welded)
constexpr ptrdiff_t capsule = 0x8;         // physics.actor_character._m_capsule (its primary physics object)
}
using seat_tick_t = void (*)(void* self, void* scene, void* actor_physics, uint64_t is_authority, void* vehicles);
anymaker::cell_hook h_seat_tick;
void hk_seat_tick(void* self, void* scene, void* actor_physics, uint64_t is_authority, void* vehicles) {
    reinterpret_cast<seat_tick_t>(h_seat_tick.original)(self, scene, actor_physics, is_authority, vehicles);
    if (!g_ready.load() || !self || !actor_physics) return;
    uint64_t body[2];
    std::memcpy(body, static_cast<uint8_t*>(self) + seat_off::seat_physics, sizeof body);
    void* capsule = static_cast<uint8_t*>(actor_physics) + seat_off::capsule;
    double now = now_seconds();
    std::lock_guard lock(g_rider_mutex);
    std::erase_if(g_riders, [&](const Rider& r) { return r.capsule == capsule || r.seat == self || now - r.seen > 5.0; });
    if (body[0]) g_riders.push_back({{body[0], body[1]}, capsule, self, now});
}
// The rider leaves the world: forget their capsule before it is freed.
using seat_destroy_t = void (*)(void* self, void* scene);
anymaker::cell_hook h_seat_destroy;
void hk_seat_destroy(void* self, void* scene) {
    {
        std::lock_guard lock(g_rider_mutex);
        std::erase_if(g_riders, [&](const Rider& r) { return r.seat == self; });
    }
    reinterpret_cast<seat_destroy_t>(h_seat_destroy.original)(self, scene);
}
// The capsules welded to this vehicle body. Actors don't run their seat tick every physics tick (it was missing on
// most ticks while stopped), so a weld counts until its seat says otherwise or goes quiet for 2 s.
int riders_of(const void* vehicle_body, void** out, int max) {
    uint64_t body[2];
    std::memcpy(body, vehicle_body, sizeof body);
    double now = now_seconds();
    int n = 0;
    std::lock_guard lock(g_rider_mutex);
    for (const Rider& r : g_riders)
        if (n < max && r.vehicle_body[0] == body[0] && r.vehicle_body[1] == body[1] && now - r.seen < 2.0) out[n++] = r.capsule;
    return n;
}

using tick_t = void (*)(void* self, void* vehicle, void* scene);
anymaker::cell_hook h_gyro_tick;
void hk_gyro_tick(void* self, void* vehicle, void* scene) {
    reinterpret_cast<tick_t>(h_gyro_tick.original)(self, vehicle, scene);
    if (!g_ready.load() || !self || !vehicle || server_kind(self) != Kind::lean_assist) return;
    void* physics = static_cast<uint8_t*>(vehicle) + off::server_vehicle_physics;
    double now = now_seconds();
    LeanInput in;
    {
        std::lock_guard lock(g_assist_mutex);
        Assist& a = g_assists[self];
        in.dt = a.last > 0 ? now - a.last : 0;
        a.last = now;
        in.strength = std::abs(a.slots[1]) > 1e-6 ? std::clamp(a.power, 0.0, 1.0) : 0;
        in.turn_lean = a.turn_lean;
    }
    Mat34 v, c;
    g_server_vehicle_transform(&v, vehicle);
    g_server_component_transform(&c, self);
    Mat34 world = compose(v, c);
    if (!finite(world)) return;
    in.forward = column(world, 2);
    in.up = column(world, 1);
    g_get_angular(&in.angular_velocity, physics);
    g_get_linear(&in.linear_velocity, physics);
    in.speed = length(in.linear_velocity);
    const double steer = fork_steering(vehicle);
    const Vec3 f = normalized(in.forward);
    const double wf = dot(in.angular_velocity, f);
    double observed = 0, predicted = lean_rate_for(in.forward, in.up, wf);
    {
        std::lock_guard lock(g_assist_mutex);
        Assist& a = g_assists[self];
        LeanOutput probe = lean_assist(LeanInput{in.forward, in.up, {}, {}, 1, 1, 1});   // lean only
        double lean = rad(probe.lean_deg);
        if (a.have_prev && in.dt > 0.002 && in.dt < 0.1) {
            observed = (lean - a.prev_lean) / in.dt;
            // The lean change over the last tick against the roll rate the physics reports now, which moved it.
            double p = predicted;
            if (std::abs(p) > 0.05) {
                a.agree = a.agree * 0.98 + observed * p;
                a.weight = a.weight * 0.98 + p * p;
                if (a.weight > 0.02) {
                    double ratio = a.agree / a.weight;
                    double sign = ratio < -0.3 ? -1 : ratio > 0.3 ? 1 : a.sign;
                    if (sign != a.sign) log(1, "lean assist: the physics spins the other way round from this mod's maths (fit %.2f); flipped", ratio);
                    a.sign = sign;
                }
            }
            if (std::abs(a.prev_set_wf) > 0.05) {
                double k = std::clamp(wf / a.prev_set_wf, -3.0, 3.0);
                a.kept += k;
                a.kept_n += 1;
                a.kept_ema = a.kept_ema * 0.9 + k * 0.1;
            }
            double per_rad = lean_rate_for(in.forward, in.up, 1);
            if (std::abs(per_rad) > 0.3) {
                double r = observed / per_rad;
                double alpha = in.dt / (in.dt + 0.015);
                a.rate_f = a.have_rate ? a.rate_f + alpha * (r - a.rate_f) : r;
                a.have_rate = true;
            }
        }
        a.prev_lean = lean;
        a.have_prev = true;
        if (in.dt > 0 && in.dt < 0.1) a.yaw_f += in.dt / (in.dt + 0.4) * (in.angular_velocity.y - a.yaw_f);
        // The turn lean comes from the rider's steering, not from the bike's yaw: a leaned bike turns by itself, so a
        // lean set from the yaw fed itself and weaved side to side at speed. Which way the steering turns the bike is
        // learned from the yaw it brings; until then the bike is held upright.
        if (std::isfinite(steer)) {
            if (in.speed > 3 && std::abs(steer) > 0.15 && in.dt > 0 && in.dt < 0.1) a.steer_agree = a.steer_agree * 0.995 + steer * a.yaw_f * in.dt;
            double sign = a.steer_agree > 0.02 ? 1 : a.steer_agree < -0.02 ? -1 : 0;
            in.yaw_rate = sign * steer_yaw_rate(steer, in.speed);
        } else {
            in.yaw_rate = a.yaw_f;
        }
        in.spin_sign = a.sign;
        in.gain_scale = a.gain;
        // The physics' own roll rate has no lag, so it damps best; only while the physics throws the roll back (a
        // rider on board) does the rate seen from the lean take over.
        if (a.have_rate && a.kept_ema < 0.3) in.measured_rate = a.rate_f;
    }
    LeanOutput out = lean_assist(in);
    double set_wf = 0, pivot_height = -1;
    int rider_count = 0;
    if (out.active && !out.settled && std::isfinite(out.angular_velocity.x) && std::isfinite(out.angular_velocity.y) && std::isfinite(out.angular_velocity.z)) {
        g_set_angular(physics, &out.angular_velocity);
        // Pivot about the tyres' contact line, not the centre of mass, so the tyres are not dragged sideways.
        Vec3 com, contact;
        const Vec3 spin_change = out.angular_velocity - in.angular_velocity;
        bool have_contact = g_set_linear && g_get_com && contact_point(vehicle, v, &contact);
        if (!have_contact && g_get_com) {
            g_get_com(&com, physics);
            contact = com - Vec3{0, 0.5, 0};                       // no wheels found: about half a metre down
            have_contact = g_set_linear != nullptr;
        }
        if (have_contact) {
            g_get_com(&com, physics);
            Vec3 lin = in.linear_velocity + pivot_velocity_change(spin_change, contact, com);
            if (std::isfinite(lin.x) && std::isfinite(lin.y) && std::isfinite(lin.z)) g_set_linear(physics, &lin);
            pivot_height = dot(com - contact, Vec3{0, 1, 0});
            // The riders turn with the bike about the same line, so their welds stay where they are.
            void* riders[4];
            rider_count = riders_of(physics, riders, 4);
            for (int i = 0; i < rider_count; ++i) {
                Vec3 w, l, c;
                g_get_angular(&w, riders[i]);
                g_get_linear(&l, riders[i]);
                g_get_com(&c, riders[i]);
                w = w + spin_change;
                l = l + pivot_velocity_change(spin_change, contact, c);
                if (!std::isfinite(w.x + w.y + w.z + l.x + l.y + l.z)) continue;
                g_set_angular(riders[i], &w);
                g_set_linear(riders[i], &l);
            }
        }
        set_wf = dot(out.angular_velocity, f);
    }
    bool log_now = false;
    double sign = 1, kept = -1, gain = 1, rate_f = 0, steer_agree = 0;
    {
        std::lock_guard lock(g_assist_mutex);
        Assist& a = g_assists[self];
        a.slots[2] = out.lean_deg;
        a.slots[3] = out.target_deg;
        a.prev_set_wf = set_wf;
        if (now >= a.next_log) {
            log_now = true;
            a.next_log = now + 1.0;
            sign = a.sign;
            gain = a.gain;
            rate_f = a.rate_f;
            steer_agree = a.steer_agree;
            if (a.kept_n > 0) kept = a.kept / a.kept_n;
            a.kept = a.kept_n = 0;
        }
    }
    static std::atomic<int> logs{1800};  // one a second for 30 minutes
    if (log_now && logs.fetch_sub(1) > 0)
        log(0, "lean assist: lean %.1f deg (moving %.0f deg/s), target %.1f deg, roll rate %.2f -> %.2f rad/s, spin sign %+.0f, "
               "roll rate kept between ticks %.2f, smoothed roll %.2f rad/s, gain %.2f, speed %.1f m/s, pivot %.2f m below the centre of mass, riders moved %d, power %.1f, turn lean %.1f, steering %.2f (learned way %.3f), %s (dt %.4f s)",
            out.lean_deg, observed * 180 / kPi, out.target_deg, wf, set_wf, sign, kept, rate_f, gain, in.speed, pivot_height, rider_count, in.strength, in.turn_lean, steer, steer_agree,
            out.settled ? "settled" : out.active ? "correcting" : "idle", in.dt);
}

using get_f64_t = void (*)(double** ret, void* self, const anymaker::gc_string_view* name);
anymaker::cell_hook h_gyro_get_f64;
void hk_gyro_get_f64(double** ret, void* self, const anymaker::gc_string_view* name) {
    reinterpret_cast<get_f64_t>(h_gyro_get_f64.original)(ret, self, name);
    if (!ret || *ret || !self || !name || server_kind(self) != Kind::lean_assist) return;
    int i = slot_of(name->view());
    if (i < 0) return;
    std::lock_guard lock(g_assist_mutex);
    *ret = &g_assists[self].slots[i];
}

using get_properties_t = void (*)(void* self, void* properties);
using add_f64_range_t = void (*)(void* properties, const int32_t* label, double* const* value, const double* min, const double* max, const double* step);
constexpr int kAddPropertyF64RangeSlot = 10;     // server_scene.peer.property_data method table (as AnyEngineParts)
constexpr int32_t kLocPower = 326;               // e_localization_string property_power
constexpr int32_t kLocRollSensitivity = 332;     // e_localization_string property_sensitivity_roll
anymaker::cell_hook h_gyro_properties;
void* container_method(void* container, int slot) {
    if (!container || !anymaker::readable(container, 8)) return nullptr;
    auto ti = anymaker::typeinfo_of(container);
    if (!ti.p || !anymaker::readable(ti.p + 0x80, 8) || !ti.table() || !anymaker::readable(ti.table() + slot, 8)) return nullptr;
    return ti.method(slot);
}
void hk_gyro_properties(void* self, void* properties) {
    reinterpret_cast<get_properties_t>(h_gyro_properties.original)(self, properties);
    if (!g_ready.load() || !self || !properties || server_kind(self) != Kind::lean_assist) return;
    auto add = reinterpret_cast<add_f64_range_t>(container_method(properties, kAddPropertyF64RangeSlot));
    if (!add) return;
    double* power = nullptr;
    double* turn_lean = nullptr;
    {
        std::lock_guard lock(g_assist_mutex);
        power = &g_assists[self].power;
        turn_lean = &g_assists[self].turn_lean;
    }
    double min = 0, max = 1, step = 0.1;
    int32_t label = kLocPower;
    add(properties, &label, &power, &min, &max, &step);
    double lean_max = 2;
    label = kLocRollSensitivity;
    add(properties, &label, &turn_lean, &min, &lean_max, &step);
}

// ================================================================== install (background thread)
struct HookPlan { anymaker::cell_hook* hook; const anymaker::func_desc* desc; void* replacement; const char* what; };
void remove_hooks();
void install_hooks() {
    using namespace anymaker::experimental;
    for (int attempt = 0; attempt < 180 && !g_stop; ++attempt) {
        if (attempt) Sleep(1000);
        if (!matching_build()) { if (attempt == 0) log(1, "game build does not match the SDK reference; AnyMoto stays off"); return; }
        int roots = -1;
        void** add = route::cell(bind::definitions_add_definitions, &roots);
        auto ctor = reinterpret_cast<file_ctor_t>(route::function(bind::definition_file_ctor));
        auto dtor = reinterpret_cast<file_ctor_t>(route::function(bind::definition_file_dtor));
        auto load = reinterpret_cast<file_load_t>(route::function(bind::definition_file_load));
        auto sc = reinterpret_cast<string_ctor_t>(native(anymaker::sym::string_ctor_cstr_rva));
        g_stock_tuning = static_cast<const uint8_t*>(global(bind::g_wheel_definition_wheel));
        void* tyres = global(bind::g_tyre_data_container);
        if (add && !route::add_cell_ok(add)) {
            static int logs = 3;
            if (logs-- > 0) log(1, "the definitions route led to %s, not _add_definitions; looking again", route::what_is(anymaker::readable(add, 8) ? *add : nullptr));
            add = nullptr;
        }
        if (load && !route::file_load_ok((void*)load)) load = nullptr;
        if (!add || !ctor || !dtor || !load || !sc || !can_read(g_stock_tuning, off::tuning_size)) {
            if (attempt == 20 || attempt == 179)
                log(attempt == 179 ? 2 : 1, "%s the game functions (route root matches %d, add=%d file ctor=%d dtor=%d load=%d string=%d wheel tuning=%d)",
                    attempt == 179 ? "could not locate" : "still looking for", roots, !!add, !!ctor, !!dtor, !!load, !!sc, can_read(g_stock_tuning, off::tuning_size));
            continue;
        }
        g_file_ctor = ctor; g_file_dtor = dtor; g_file_load = load; g_string_ctor = sc;
        g_get_definition = reinterpret_cast<get_definition_t>(route::function(bind::definitions_get_by_id));
        g_client_component_transform = reinterpret_cast<mat_of_t>(route::function(bind::client_component_transform));
        g_accessory_transform = reinterpret_cast<mat_of_t>(route::function(bind::client_element_accessory_transform));
        g_steering_transform = reinterpret_cast<mat_of_t>(route::function(bind::client_element_steering_transform));
        g_vehicle_render_transform = reinterpret_cast<mat_of_t>(route::function(bind::client_vehicle_render_transform));
        g_element_render = reinterpret_cast<render_t>(route::function(bind::client_element_render));
        g_mesh_by_index = reinterpret_cast<mesh_by_index_t>(route::function(bind::definition_dynamic_mesh));
        g_add_mesh = reinterpret_cast<add_mesh_t>(route::function(bind::render_add_mesh));
        g_server_vehicle_transform = reinterpret_cast<mat_of_t>(route::function(bind::server_vehicle_transform));
        g_server_component_transform = reinterpret_cast<mat_of_t>(route::function(bind::server_component_get_transform));
        g_get_angular = reinterpret_cast<physics_vec_t>(route::function(bind::physics_get_angular_velocity));
        g_get_linear = reinterpret_cast<physics_vec_t>(route::function(bind::physics_get_linear_velocity));
        g_set_angular = reinterpret_cast<physics_set_vec_t>(route::function(bind::physics_set_angular_velocity));
        g_set_linear = reinterpret_cast<physics_set_vec_t>(route::function(bind::physics_set_linear_velocity));
        g_get_com = reinterpret_cast<physics_vec_t>(route::function(bind::physics_get_center_of_mass));
        if (!g_set_linear || !g_get_com) log(1, "Lean Assist: linear velocity or centre of mass not found (%d/%d); it rolls about the centre of mass", !!g_set_linear, !!g_get_com);
        if (!g_get_definition || !g_client_component_transform || !g_accessory_transform || !g_steering_transform || !g_element_render ||
            !g_mesh_by_index || !g_add_mesh) {
            log(2, "wheel drawing functions not found (lookup %d, transform %d, accessory %d, steering %d, tyre %d, mesh %d/%d); AnyMoto is off",
                !!g_get_definition, !!g_client_component_transform, !!g_accessory_transform, !!g_steering_transform, !!g_element_render, !!g_mesh_by_index, !!g_add_mesh);
            return;
        }
        if (!write_files()) { log(2, "could not write the AnyMoto files; AnyMoto is off"); return; }
        add_tyre_size(tyres);
        make_tuning();
        if (!g_tuning[0] || !g_tuning[1]) { log(2, "could not build the fork and swingarm tuning; AnyMoto is off"); return; }
        // The wheel hooks are all or nothing: a fork without its tuning on either side would crash the game.
        HookPlan wheel[] = {
            {&h_assign, &bind::wheel_assign_definition, (void*)&hk_assign, "server wheel tuning"},
            {&h_client_create, &bind::client_wheel_create, (void*)&hk_client_create, "client wheel create"},
            {&h_element_create, &bind::client_element_create, (void*)&hk_element_create, "client wheel element create"},
            {&h_wheel_render, &bind::client_wheel_render, (void*)&hk_wheel_render, "client wheel drawing"},
            {&h_steering_render, &bind::client_steering_render, (void*)&hk_steering_render, "client steering wheel drawing"},
            {&h_add_definitions, &bind::definitions_add_definitions, (void*)&hk_add_definitions, "part definitions"},
        };
        for (auto& p : wheel) {
            void** cell = p.hook == &h_add_definitions ? add : route::cell(*p.desc);
            if (!cell || !p.hook->install(cell, p.replacement)) {
                log(2, "%s hook not installed (cell %s); AnyMoto is off", p.what, cell ? "taken or not writable" : "not found");
                remove_hooks();
                return;
            }
        }
        // Optional extras: without them the parts still work.
        void** hands = route::cell(bind::client_steering_hand_pose);
        if (!g_vehicle_render_transform || !hands || !h_hand_pose.install(hands, (void*)&hk_hand_pose)) log(1, "seated hand hook not installed; hands stay where a steering wheel's rim would be");
        bool lean = g_server_vehicle_transform && g_server_component_transform && g_get_angular && g_get_linear && g_set_angular;
        void** tick = route::cell(bind::gyro_tick);
        void** data = route::cell(bind::gyro_get_data_f64);
        void** props = route::cell(bind::gyro_get_properties);
        if (!lean || !tick || !h_gyro_tick.install(tick, (void*)&hk_gyro_tick))
            log(1, "Lean Assist inactive (physics %d/%d/%d, transforms %d/%d, tick %d)", !!g_get_angular, !!g_get_linear, !!g_set_angular,
                !!g_server_vehicle_transform, !!g_server_component_transform, !!tick);
        if (!data || !h_gyro_get_f64.install(data, (void*)&hk_gyro_get_f64)) log(1, "Lean Assist data channels unavailable");
        if (!props || !h_gyro_properties.install(props, (void*)&hk_gyro_properties)) log(1, "Lean Assist Power row unavailable (it runs at 0.6)");
        void** seat = route::cell(bind::server_seat_tick);
        void** seat_gone = route::cell(bind::server_seat_destroy);
        if (!seat || !seat_gone || !h_seat_destroy.install(seat_gone, (void*)&hk_seat_destroy) || !h_seat_tick.install(seat, (void*)&hk_seat_tick))
            log(1, "seat hooks not installed; Lean Assist can't carry the rider and fights them");
        g_ready = true;
        log(0, "Ready after %d s. Load or start a world to add the motorcycle parts (definition file %s).", attempt, g_json_path.c_str());
        return;
    }
}
void remove_hooks() {
    h_gyro_properties.remove((void*)&hk_gyro_properties);
    h_gyro_get_f64.remove((void*)&hk_gyro_get_f64);
    h_seat_tick.remove((void*)&hk_seat_tick);
    h_seat_destroy.remove((void*)&hk_seat_destroy);
    h_gyro_tick.remove((void*)&hk_gyro_tick);
    h_hand_pose.remove((void*)&hk_hand_pose);
    h_add_definitions.remove((void*)&hk_add_definitions);
    h_steering_render.remove((void*)&hk_steering_render);
    h_wheel_render.remove((void*)&hk_wheel_render);
    h_element_create.remove((void*)&hk_element_create);
    h_client_create.remove((void*)&hk_client_create);
    h_assign.remove((void*)&hk_assign);
}
}  // namespace
}  // namespace anymoto

using namespace anymoto;
extern "C" __declspec(dllexport) bool AnyAPI_ModInit(const AnyModHostV1* h, AnyModCallbacksV1* out) {
    if (!h || !out || h->struct_size != sizeof(*h) || h->abi != ANYAPI_MOD_ABI || out->struct_size != sizeof(*out)) return false;
    host = *h; out->id = kModId;
    out->shutdown = [](void*) { g_stop = true; remove_hooks(); };
    return true;
}
extern "C" __declspec(dllexport) void AnyAPI_ModReady() {
    std::thread(install_hooks).detach();   // resolving scans game memory; keep it off the loader and frame threads
}
