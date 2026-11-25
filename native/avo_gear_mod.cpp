// AVO Trailing Link Gear: steerable suspension legs with a built-in wheel, small / medium / large (Nate 2026-11-25,
// look 3 of three). Joins the AnyVehicleOverhaul bundle once it has passed its tests; until then it is its own test
// DLL (AvoGear.dll), as new mods stay separate until they are live. Keep kModId, the avo_gear_* part ids and the
// AvoGear data folder as they are, or players lose their gear.
//
// How (the AnyMoto route, anymoto_mod.cpp, proven in game for its fork and swingarm):
//   - The definitions are written to "AnyAPI and Modding/AvoGear" and added to the game's part definitions after the
//     game adds its own; the meshes are generated (avo_gear_mesh.h) into rom/meshes/components.
//   - Each size is a wheel-class part. The game gives a wheel its code-side tuning by definition id and knows only its
//     own ids, so each size gets a copy of the stock wheel's, set on the server after assign_definition and on the
//     client before the wheel and its element are built. The copies have the tyre built in (m_is_tread_integral), no
//     tyre items, the size's radius, travel and steering lock.
//   - Drawing: the wheel's moving meshes are ours (the leg turns with the steering, the arm follows the axle, the
//     shock stretches between them, the wheel spins); there is no tyre item for the game to draw.
// Every hook passes other parts through, so it sits alongside AnyMoto and the AVO wheels on the same functions.
//
// EXPERIMENTAL: game functions through the experimental SDK (AnyMoto's bindings), Anymaker 0.2.0 / Steam build
// 26328453 only. First runs are diagnostic: read anymaker_modding.log ("avogear" lines).
#define NOMINMAX
#include <windows.h>
#include "anyapi_services_v1.h"
#include "anyapi_experimental.hpp"
#include "anymoto_bindings.h"
#include "route_cell.h"
#include "avo_gear_logic.h"
#include "avo_gear_mesh.h"
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

namespace avogear {
namespace {
namespace bind = anymoto::bind;
using anymoto::compose;
using anymoto::column;
using anymoto::finite;
constexpr const char* kModId = "avogear";

AnyModHostV1 host;
std::atomic<bool> g_ready{false}, g_stop{false};

void log(int level, const char* fmt, ...) {
    if (!host.log) return;
    char buf[640];
    va_list ap; va_start(ap, fmt); vsnprintf(buf, sizeof buf, fmt, ap); va_end(ap);
    host.log(uint32_t(level), kModId, buf);
}
template <class T> T& at(void* base, ptrdiff_t offset) { return *reinterpret_cast<T*>(static_cast<uint8_t*>(base) + offset); }
template <class T> T at(const void* base, ptrdiff_t offset) { return *reinterpret_cast<const T*>(static_cast<const uint8_t*>(base) + offset); }
bool can_read(const void* p, size_t n) { return p && anymaker::readable(p, n); }

// Layouts as AnyMoto's (0.2.0 reference).
namespace off {
constexpr ptrdiff_t server_component_definition = 312;
constexpr ptrdiff_t server_wheel_tuning = 480 + 8;
constexpr ptrdiff_t client_component_definition = 176;
constexpr ptrdiff_t client_wheel_element = 224;
constexpr ptrdiff_t client_wheel_tuning = 224 + 8;
constexpr ptrdiff_t client_element_rotation = 0x128;     // client_scene.vehicle_element.wheel.m_rotation (f64, spin)
constexpr ptrdiff_t definition_id = 0;
constexpr size_t tuning_size = 328;
constexpr ptrdiff_t tuning_radius = 8, tuning_position = 16, tuning_rotation = 40, tuning_steering_position = 80;
constexpr ptrdiff_t tuning_suspension_length = 176, tuning_steer_min = 208, tuning_steer_max = 216;
constexpr ptrdiff_t tuning_tyres = 280, tuning_tread_integral = 320;
}  // namespace off
constexpr int32_t kStoreSystem = 1;

using string_ctor_t = void (*)(anymaker::gc_string_view*, const char*);
string_ctor_t g_string_ctor;
using get_definition_t = void (*)(const void** ret, const void* container, const anymaker::gc_string_view* id);
get_definition_t g_get_definition;

// ================================================================== which parts are ours (size index, or -1)
std::mutex g_kind_mutex;
std::unordered_map<const void*, int> g_kinds;
int size_of_definition(const void* definition) {
    if (!definition) return -1;
    std::lock_guard lock(g_kind_mutex);
    auto found = g_kinds.find(definition);
    if (found != g_kinds.end()) return found->second;
    int k = -1;
    if (can_read(static_cast<const uint8_t*>(definition) + off::definition_id, sizeof(anymaker::gc_string_view))) {
        auto* id = reinterpret_cast<const anymaker::gc_string_view*>(static_cast<const uint8_t*>(definition) + off::definition_id);
        if (id->data && id->length > 0 && id->length < 64 && can_read(id->data, size_t(id->length))) k = size_of_id(id->data, size_t(id->length));
    }
    g_kinds[definition] = k;
    return k;
}
int known_size(const void* definition) {
    std::lock_guard lock(g_kind_mutex);
    auto found = g_kinds.find(definition);
    return found == g_kinds.end() ? -1 : found->second;
}
void forget_definitions() { std::lock_guard lock(g_kind_mutex); g_kinds.clear(); }
int server_size(const void* c) {
    return c && can_read(static_cast<const uint8_t*>(c) + off::server_component_definition, 8) ? size_of_definition(at<const void*>(c, off::server_component_definition)) : -1;
}
int client_size(const void* c) {
    return c && can_read(static_cast<const uint8_t*>(c) + off::client_component_definition, 8) ? size_of_definition(at<const void*>(c, off::client_component_definition)) : -1;
}

// ================================================================== files
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

std::string g_json_path;
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
    MeshPaths paths;
    auto put = [&](const std::string& name, const Mesh& mesh, std::string& path) {
        std::vector<uint8_t> bytes = anymoto::encode_mesh(mesh, name);
        std::wstring file = components + std::wstring(name.begin(), name.end()) + L".mesh";
        if (!write_file(file, bytes.data(), bytes.size())) { log(2, "could not write %s.mesh into rom\\meshes\\components; the gear is off", name.c_str()); return false; }
        path = "meshes/components/" + name + ".mesh";
        return true;
    };
    for (int i = 0; i < kSizeCount; ++i) {
        const Size& s = kSizes[i];
        std::string k = std::string("avo_gear_trailing_") + s.key;
        if (!put(k + "_base", gear_base(s), paths.base[i]) || !put(k + "_steer", gear_steer(s), paths.steer[i]) || !put(k + "_arm", gear_arm(s), paths.arm[i]) ||
            !put(k + "_shock", gear_shock(s), paths.shock[i]) || !put(k + "_tyre", gear_tyre(s), paths.tyre[i])) return false;
    }
    if (!put("avo_gear_hidden", anymoto::hidden_speck(), paths.hidden)) return false;
    dir += L"\\AvoGear";
    CreateDirectoryW(dir.c_str(), nullptr);
    std::wstring file = dir + L"\\avo_gear_parts.json";
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
    if (!g_ready.load() || g_adding == std::this_thread::get_id() || !container || find_definition(container, kSizes[0].id)) return;
    forget_definitions();
    std::string forward = g_json_path;
    for (char& c : forward) if (c == '\\') c = '/';
    bool ok = add_ours(container, g_json_path) || add_ours(container, forward);
    int found = 0;
    for (const Size& s : kSizes)
        if (const void* d = find_definition(container, s.id)) { size_of_definition(d); ++found; }
    static std::atomic<int> logs{6};
    if (logs.fetch_sub(1) > 0)
        log(ok ? 0 : 2, ok ? "trailing link gear added to the game's parts (%d of 3 found; %s)" : "could not load the gear definitions from %s", ok ? found : 0, g_json_path.c_str());
}

// ================================================================== wheel tuning
const uint8_t* g_stock_tuning;
uint8_t* g_tuning[kSizeCount];
int32_t g_rotation[9];               // the copies' m_rotation (axle across, along x), for the steering angle
std::once_flag g_tuning_once;
uint8_t g_empty_list[16];            // the copies' tyre list points here with no entries

void make_tuning() {
    std::call_once(g_tuning_once, [] {
        if (!can_read(g_stock_tuning, off::tuning_size)) return;
        auto* stock_list = reinterpret_cast<const anymaker::gc_vector_raw*>(g_stock_tuning + off::tuning_tyres);
        if (stock_list->element_size != sizeof(anymaker::gc_string_view)) { log(2, "stock tyre list element size %u, expected 16; the gear is off", stock_list->element_size); return; }
        const double stock_min = at<double>(g_stock_tuning, off::tuning_steer_min), stock_max = at<double>(g_stock_tuning, off::tuning_steer_max);
        const int32_t* stock_rotation = reinterpret_cast<const int32_t*>(g_stock_tuning + off::tuning_rotation);
        anymoto::quarter_turn_y(stock_rotation, g_rotation);
        for (int i = 0; i < kSizeCount; ++i) {
            const Size& s = kSizes[i];
            Layout l = layout(s);
            auto* copy = static_cast<uint8_t*>(HeapAlloc(GetProcessHeap(), 0, off::tuning_size));
            if (!copy) return;
            std::memcpy(copy, g_stock_tuning, off::tuning_size);
            at<double>(copy, off::tuning_radius) = s.radius;
            double pos[3] = {l.tyre_top.x, l.tyre_top.y, l.tyre_top.z};
            std::memcpy(copy + off::tuning_position, pos, sizeof pos);
            // Steer about the leg, not the tyre's own centre: the tuning keeps the steering pivot apart from the tyre
            // position (AnyMoto puts both on the tyre). Likely the pivot; the "drawing" log line shows whether the
            // game's axle follows the drawn one at full lock.
            double pivot[3] = {0, l.tyre_top.y, l.leg_z};
            std::memcpy(copy + off::tuning_steering_position, pivot, sizeof pivot);
            std::memcpy(copy + off::tuning_rotation, g_rotation, sizeof g_rotation);
            at<double>(copy, off::tuning_suspension_length) = s.travel;
            double scale = std::abs(stock_max) > 1e-6 ? rad(s.steer_deg) / std::abs(stock_max) : 0;
            at<double>(copy, off::tuning_steer_min) = stock_min * scale;
            at<double>(copy, off::tuning_steer_max) = stock_max * scale;
            auto* list = reinterpret_cast<anymaker::gc_vector_raw*>(copy + off::tuning_tyres);
            list->buffer = g_empty_list;
            list->offset = 0; list->count = 0; list->capacity = 1;
            at<bool>(copy, off::tuning_tread_integral) = true;
            g_tuning[i] = copy;
        }
        const double* p = reinterpret_cast<const double*>(g_stock_tuning + off::tuning_position);
        log(0, "wheel tuning: stock radius %.3f travel %.3f steer %.3f..%.3f rad, tyre at (%.3f, %.3f, %.3f), tread integral %d; gear sizes %.2f/%.2f/%.2f m",
            at<double>(g_stock_tuning, off::tuning_radius), at<double>(g_stock_tuning, off::tuning_suspension_length), stock_min, stock_max, p[0], p[1], p[2],
            int(at<uint8_t>(g_stock_tuning, off::tuning_tread_integral)), kSizes[0].radius, kSizes[1].radius, kSizes[2].radius);
    });
}
const void* tuning_for(int size) {
    if (size < 0 || size >= kSizeCount) return nullptr;
    make_tuning();
    return g_tuning[size];
}

using assign_t = void (*)(void** component);
anymaker::cell_hook h_assign;
void hk_assign(void** component) {
    reinterpret_cast<assign_t>(h_assign.original)(component);
    void* self = component && can_read(component, 8) ? *component : nullptr;
    if (!self) return;
    int size = server_size(self);
    const void* tuning = tuning_for(size);
    if (!tuning || !can_read(static_cast<uint8_t*>(self) + off::server_wheel_tuning, 8)) return;
    at<const void*>(self, off::server_wheel_tuning) = tuning;
    static std::atomic<int> logs{4};
    if (logs.fetch_sub(1) > 0) log(0, "server: %s tuning set", kSizes[size].name);
}

using client_create_t = void (*)(void* self, const void* container, void* vehicle, void* grid);
anymaker::cell_hook h_client_create;
void hk_client_create(void* self, const void* container, void* vehicle, void* grid) {
    int size = client_size(self);
    if (const void* tuning = tuning_for(size); tuning && can_read(static_cast<uint8_t*>(self) + off::client_wheel_tuning, 8)) {
        at<const void*>(self, off::client_wheel_tuning) = tuning;
        static std::atomic<int> logs{4};
        if (logs.fetch_sub(1) > 0) log(0, "client: %s tuning set before the wheel is built", kSizes[size].name);
    }
    reinterpret_cast<client_create_t>(h_client_create.original)(self, container, vehicle, grid);
}
using element_create_t = void (*)(void* element, void* vehicle);
anymaker::cell_hook h_element_create;
void hk_element_create(void* element, void* vehicle) {
    auto* component = static_cast<uint8_t*>(element) - off::client_wheel_element;
    if (element && can_read(component + off::client_component_definition, 8) && can_read(static_cast<uint8_t*>(element) + 8, 8)) {
        int size = known_size(at<const void*>(component, off::client_component_definition));
        const void* tuning = tuning_for(size);
        if (tuning && at<const void*>(element, 8) != tuning) {
            at<const void*>(element, 8) = tuning;
            log(1, "client: wheel element of a %s had no tuning when built; set it", kSizes[size].name);
        }
    }
    reinterpret_cast<element_create_t>(h_element_create.original)(element, vehicle);
}

// ================================================================== drawing
using mat_of_t = void (*)(Mat34* ret, const void* object);
using render_t = void (*)(const void* self, const Mat34* transform, void* out);
using mesh_by_index_t = void (*)(const void** ret, const void* definition, const int32_t* index);
using add_mesh_t = void (*)(void* out, const void* mesh_dynamic, const Mat34* transform);
mat_of_t g_client_component_transform, g_accessory_transform, g_steering_transform;
mesh_by_index_t g_mesh_by_index;
add_mesh_t g_add_mesh;
anymaker::cell_hook h_wheel_render;

void add_dynamic(void* out, const void* definition, int32_t index, const Mat34& transform) {
    if (!finite(transform)) return;
    const void* mesh = nullptr;
    g_mesh_by_index(&mesh, definition, &index);
    if (mesh) g_add_mesh(out, mesh, &transform);
}
Mat34 rotation_of(const int32_t r[9]) { Mat34 m; for (int i = 0; i < 9; ++i) m.m[i] = r[i]; return m; }
Mat34 transpose_rotation(const Mat34& a) {
    Mat34 t;
    for (int c = 0; c < 3; ++c) for (int r = 0; r < 3; ++r) t.m[c * 3 + r] = a.m[r * 3 + c];
    return t;
}

void hk_wheel_render(const void* self, const Mat34* transform, void* out) {
    int size = g_ready.load() ? client_size(self) : -1;
    if (size < 0 || !transform || !out) {
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
    // The game steers by R * rotation_y(angle) in the tuning's rotated frame; the same angle in the part frame.
    Mat34 r = rotation_of(g_rotation);
    double steer = anymoto::steering_angle_of(compose(compose(r, anymoto::rotation_y(anymoto::steering_angle_of(steering))), transpose_rotation(r)));
    double spin = can_read(static_cast<const uint8_t*>(element) + off::client_element_rotation, 8) ? at<double>(element, off::client_element_rotation) : 0;
    if (!std::isfinite(spin)) spin = 0;
    if (!std::isfinite(steer)) steer = 0;
    Frames f = frames(kSizes[size], column(accessory, 3).y, steer, spin);
    add_dynamic(out, definition, 0, compose(render, f.steer));
    add_dynamic(out, definition, 1, compose(render, f.arm));
    add_dynamic(out, definition, 2, compose(render, f.shock));
    add_dynamic(out, definition, 3, compose(render, f.tyre));
    static std::atomic<int> logs{3};
    static std::atomic<int> steer_logs{4};
    if ((logs.load() > 0 && logs.fetch_sub(1) > 0) || (std::abs(steer) > rad(10) && steer_logs.load() > 0 && steer_logs.fetch_sub(1) > 0)) {
        anymoto::Vec3 t = column(accessory, 3), d = column(f.tyre, 3);
        log(0, "client: drawing a %s, game axle (%.3f, %.3f, %.3f), drawn axle (%.3f, %.3f, %.3f), steering %.1f deg, spin %.2f",
            kSizes[size].name, t.x, t.y, t.z, d.x, d.y, d.z, steer * 180 / kPi, spin);
    }
}

// ================================================================== install (background thread)
struct HookPlan { anymaker::cell_hook* hook; const anymaker::func_desc* desc; void* replacement; const char* what; };
void remove_hooks();
void install_hooks() {
    using namespace anymaker::experimental;
    for (int attempt = 0; attempt < 180 && !g_stop; ++attempt) {
        if (attempt) Sleep(1000);
        if (!matching_build()) { if (attempt == 0) log(1, "game build does not match the SDK reference; the gear stays off"); return; }
        int roots = -1;
        void** add = route::cell(bind::definitions_add_definitions, &roots);
        auto ctor = reinterpret_cast<file_ctor_t>(route::function(bind::definition_file_ctor));
        auto dtor = reinterpret_cast<file_ctor_t>(route::function(bind::definition_file_dtor));
        auto load = reinterpret_cast<file_load_t>(route::function(bind::definition_file_load));
        auto sc = reinterpret_cast<string_ctor_t>(native(anymaker::sym::string_ctor_cstr_rva));
        g_stock_tuning = static_cast<const uint8_t*>(global(bind::g_wheel_definition_wheel));
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
        g_mesh_by_index = reinterpret_cast<mesh_by_index_t>(route::function(bind::definition_dynamic_mesh));
        g_add_mesh = reinterpret_cast<add_mesh_t>(route::function(bind::render_add_mesh));
        if (!g_get_definition || !g_client_component_transform || !g_accessory_transform || !g_steering_transform || !g_mesh_by_index || !g_add_mesh) {
            log(2, "wheel drawing functions not found (lookup %d, transform %d, accessory %d, steering %d, mesh %d/%d); the gear is off",
                !!g_get_definition, !!g_client_component_transform, !!g_accessory_transform, !!g_steering_transform, !!g_mesh_by_index, !!g_add_mesh);
            return;
        }
        if (!write_files()) { log(2, "could not write the gear files; the gear is off"); return; }
        make_tuning();
        for (auto* t : g_tuning) if (!t) { log(2, "could not build the gear tuning; the gear is off"); return; }
        HookPlan plan[] = {
            {&h_assign, &bind::wheel_assign_definition, (void*)&hk_assign, "server wheel tuning"},
            {&h_client_create, &bind::client_wheel_create, (void*)&hk_client_create, "client wheel create"},
            {&h_element_create, &bind::client_element_create, (void*)&hk_element_create, "client wheel element create"},
            {&h_wheel_render, &bind::client_wheel_render, (void*)&hk_wheel_render, "client wheel drawing"},
            {&h_add_definitions, &bind::definitions_add_definitions, (void*)&hk_add_definitions, "part definitions"},
        };
        for (auto& p : plan) {
            void** cell = p.hook == &h_add_definitions ? add : route::cell(*p.desc);
            if (!cell || !p.hook->install(cell, p.replacement)) {
                log(2, "%s hook not installed (cell %s); the gear is off", p.what, cell ? "taken or not writable" : "not found");
                remove_hooks();
                return;
            }
        }
        g_ready = true;
        log(0, "Ready after %d s. Load or start a world to add the trailing link gear (definition file %s).", attempt, g_json_path.c_str());
        return;
    }
}
void remove_hooks() {
    h_add_definitions.remove((void*)&hk_add_definitions);
    h_wheel_render.remove((void*)&hk_wheel_render);
    h_element_create.remove((void*)&hk_element_create);
    h_client_create.remove((void*)&hk_client_create);
    h_assign.remove((void*)&hk_assign);
}
}  // namespace
}  // namespace avogear

using namespace avogear;
extern "C" __declspec(dllexport) bool AnyAPI_ModInit(const AnyModHostV1* h, AnyModCallbacksV1* out) {
    if (!h || !out || h->struct_size != sizeof(*h) || h->abi != ANYAPI_MOD_ABI || out->struct_size != sizeof(*out)) return false;
    host = *h; out->id = kModId;
    out->shutdown = [](void*) { g_stop = true; remove_hooks(); };
    return true;
}
extern "C" __declspec(dllexport) void AnyAPI_ModReady() {
    std::thread(install_hooks).detach();
}
