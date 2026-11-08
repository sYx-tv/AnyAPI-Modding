// AnyLaser: a Laser Block and a Laser Sensor.
//
// Laser Block: shoots a beam straight out of its lens (+Z) every server tick, up to 100 m, using the game's own
// physics raycast (physics_util.raycast_distance on server_scene.m_physics, the query the wheels use). Players
// standing in the beam block it too (their capsules from the session player list). Data channels: hit (1/0),
// distance (m, lens to hit), and the input off (1 = off; unconnected = on).
// Laser Sensor: lit (1 while a beam lands on its lens), broken (1 when no beam reaches it) and distance (m from the
// laser's centre). A beam "lands" when its end is within 6.5 cm of the sensor's centre and it comes in from the lens
// side.
//
// How: both parts are new vehicle component definitions of the stock compass sensor's class (the GpsPart route),
// written to "AnyAPI and Modding/AnyLaser" and added right after the game adds its own. The compass sensor's server
// tick and get_data_f64 are hooked; they only change anything for parts whose definition id is ours. The beam is
// drawn with anyapi.gpu_draw over the 3D view, projected with the camera from client_scene.get_camera (AnyCombat's
// route), so it shows on top of anything between you and it.
//
// Single player / host: the beams and data run on the host's server scene. Every player needs the mod (the parts
// are definitions). Joining clients do not see beams yet (unproven).
//
// EXPERIMENTAL. Hooks game functions through the experimental SDK (anylaser_bindings.h): Anymaker 0.2.0 /
// Steam build 26328453 only. First runs are diagnostic: read anymaker_modding.log.
#define NOMINMAX
#include <windows.h>
#include "anyapi_services_v1.h"
#include "anyapi_gpu_draw_v1.h"
#include "anyapi_ui_state_v1.h"
#include "anyapi_players_v1.h"
#include "anyapi_experimental.hpp"
#include "anylaser_bindings.h"
#include "route_cell.h"
#include "anylaser_logic.h"
#include "anylaser_mesh.h"
#include <atomic>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

namespace anylaser {
namespace {
constexpr const char* kModId = "anylaser";
constexpr int kLook = 0;   // the look Nate picks (anylaser_logic.h kLooks, anylaser_mesh.h build_part)

AnyModHostV1 host;
std::atomic<bool> g_ready{false}, g_stop{false};

void log(int level, const char* fmt, ...) {
    if (!host.log) return;
    char buf[512];
    va_list ap; va_start(ap, fmt); vsnprintf(buf, sizeof buf, fmt, ap); va_end(ap);
    host.log(uint32_t(level), kModId, buf);
}
double now_seconds() {
    static LARGE_INTEGER freq = [] { LARGE_INTEGER f; QueryPerformanceFrequency(&f); return f; }();
    LARGE_INTEGER t; QueryPerformanceCounter(&t);
    return double(t.QuadPart) / double(freq.QuadPart);
}
template <class T> T rd(const uint8_t* p, ptrdiff_t o) { T v; std::memcpy(&v, p + o, sizeof v); return v; }
Vec3 rd_vec(const uint8_t* p, ptrdiff_t o) { return {rd<double>(p, o), rd<double>(p, o + 8), rd<double>(p, o + 16)}; }

// Layouts from the 0.2.0 SDK reference (metadata).
namespace off {
constexpr ptrdiff_t component_definition = 312;  // server_scene.vehicle_component.m_definition (ptr<const vehicle_component_definition>)
constexpr ptrdiff_t definition_id = 0;           // vehicle_component_definition.m_id (string)
constexpr ptrdiff_t scene_physics = 2656;        // server_scene.m_physics (physics.scene, 24 bytes)
constexpr ptrdiff_t cam_transform = 8;           // application.graphics.camera.m_transform (mat34: v0 right, v1 up, v2 forward, t)
constexpr ptrdiff_t cam_transform_t = 8 + 72;
constexpr ptrdiff_t cam_near = 104;              // m_near (f64)
constexpr ptrdiff_t cam_fov = 672;               // m_fov (f64, vertical radians)
constexpr ptrdiff_t cam_tan_half_fov = 680;      // m_tan_half_fov
constexpr size_t cam_size = 720;
}  // namespace off
constexpr int32_t kStoreSystem = 1;              // file.e_store.system (GpsPart)

using string_ctor_t = void (*)(anymaker::gc_string_view*, const char*);
string_ctor_t g_string_ctor;
using get_definition_t = void (*)(const void** ret, const void* container, const anymaker::gc_string_view* id);
get_definition_t g_get_definition;
struct GamePath { int32_t store; uint32_t pad; anymaker::gc_string_view path; };   // file.path (24 bytes)
static_assert(sizeof(GamePath) == 24, "file.path");

// ================================================================== part definitions (GpsPart's route)
using add_definitions_t = void (*)(void* container, const void* file);
using file_ctor_t = void (*)(void* file);
using file_load_t = void (*)(bool* ret, void* file, const void* path);
anymaker::cell_hook h_add_definitions;
file_ctor_t g_file_ctor, g_file_dtor;
file_load_t g_file_load;
std::string g_json_path;
std::thread::id g_adding;

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

// Writes our part meshes beside the stock ones (rom/meshes/components, where the game loads part meshes from).
// Returns the mesh path for the definition: the stock compass mesh when the stock header is not recognised.
std::string build_mesh(const std::wstring& game_dir, bool laser) {
    std::wstring components = game_dir + L"\\rom\\meshes\\components\\";
    std::vector<uint8_t> stock = read_file(components + L"compass_sensor_a.mesh");
    const char* name = laser ? "anylaser_laser_a" : "anylaser_sensor_a";
    std::vector<uint8_t> mesh = build_part_mesh(stock, kLook, laser, name);
    if (mesh.empty()) { log(1, "compass mesh not recognised (%zu bytes); the %s uses the stock compass look", stock.size(), laser ? "Laser Block" : "Laser Sensor"); return kStockMeshPath; }
    std::wstring file = components + (laser ? L"anylaser_laser_a.mesh" : L"anylaser_sensor_a.mesh");
    if (!write_file(file, mesh.data(), mesh.size())) { log(1, "could not write the %s mesh; using the stock compass look", laser ? "Laser Block" : "Laser Sensor"); return kStockMeshPath; }
    return std::string("meshes/components/") + name + ".mesh";
}

bool write_definition_file() {
    HMODULE self = nullptr;
    wchar_t dll[MAX_PATH]{};
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCWSTR)&write_definition_file, &self) ||
        !GetModuleFileNameW(self, dll, MAX_PATH)) return false;
    std::wstring dir(dll);
    dir = dir.substr(0, dir.find_last_of(L"\\/"));      // ...\AnyAPI and Modding\mods
    dir = dir.substr(0, dir.find_last_of(L"\\/"));      // ...\AnyAPI and Modding
    std::wstring game_dir = dir.substr(0, dir.find_last_of(L"\\/"));
    std::string laser_mesh = build_mesh(game_dir, true), sensor_mesh = build_mesh(game_dir, false);
    dir += L"\\AnyLaser";
    CreateDirectoryW(dir.c_str(), nullptr);
    std::wstring file = dir + L"\\anylaser_parts.json";
    std::string json = definitions_json(laser_mesh, sensor_mesh);
    bool ok = write_file(file, json.data(), json.size());
    log(0, "meshes: %s, %s", laser_mesh.c_str(), sensor_mesh.c_str());
    int n = WideCharToMultiByte(CP_UTF8, 0, file.c_str(), -1, nullptr, 0, nullptr, nullptr);
    g_json_path.assign(size_t(n > 0 ? n - 1 : 0), '\0');
    if (n > 1) WideCharToMultiByte(CP_UTF8, 0, file.c_str(), -1, g_json_path.data(), n, nullptr, nullptr);
    return ok && !g_json_path.empty();
}

bool add_ours(void* container, int32_t store, const std::string& text) {
    static std::string kept[4];   // $string_ctor_cstr makes a non-owning view: the characters must outlive the call
    static int next;
    std::string& chars = kept[next++ % 4];
    chars = text;
    GamePath path{store, 0, {}};
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

// Other mods hooking the same cell (GpsPart, Turbo) call down through this hook when they add their own parts;
// without this check our parts would be added twice.
bool already_added(void* container) {
    if (!g_get_definition || !g_string_ctor) return false;
    anymaker::gc_string_view id{};
    g_string_ctor(&id, kLaserId);
    const void* found = nullptr;
    g_get_definition(&found, container, &id);
    return found != nullptr;
}

void forget_definitions();
void hk_add_definitions(void* container, const void* file) {
    reinterpret_cast<add_definitions_t>(h_add_definitions.original)(container, file);
    if (g_adding != std::this_thread::get_id()) forget_definitions();
    if (!g_ready.load() || g_adding == std::this_thread::get_id() || !container || already_added(container)) return;
    std::string forward = g_json_path;
    for (char& c : forward) if (c == '\\') c = '/';
    bool ok = add_ours(container, kStoreSystem, g_json_path) || add_ours(container, kStoreSystem, forward);
    static std::atomic<int> logs{6};
    if (logs.fetch_sub(1) > 0) log(ok ? 0 : 2, ok ? "Laser Block and Laser Sensor added to the game's parts (%s)" : "could not load the AnyLaser definitions from %s", g_json_path.c_str());
}

// ================================================================== host side (server thread)
using tick_t = void (*)(uint8_t* sensor, void* vehicle, void* scene);
using get_f64_t = void (*)(double** ret, uint8_t* sensor, const anymaker::gc_string_view* name);
using transform_t = void (*)(double* out, const uint8_t* object);
struct Ray { double origin[3]; double normal[3]; };   // collision.ray: m_origin vec3, m_normal vec3 (48 bytes)
static_assert(sizeof(Ray) == 48, "collision.ray");
using raycast_t = void (*)(double* ret, void* physics_scene, const Ray* ray, const double* length);
anymaker::cell_hook h_tick, h_get_f64;
transform_t g_vehicle_transform, g_component_transform;
raycast_t g_raycast;

std::mutex g_mutex;                                     // server tick, data lookups and the renderer
std::unordered_map<const uint8_t*, Part> g_parts;       // keyed by server component; never erased (slots are handed out)
std::unordered_map<const void*, Kind> g_kinds;          // definition pointer -> kind (cleared on each add)
std::vector<Vec3> g_players;                            // feet positions, refreshed by the renderer
struct DrawnBeam { Beam beam; double seen; };
std::unordered_map<const uint8_t*, DrawnBeam> g_beams;  // per laser, for the renderer

void forget_definitions() {
    std::lock_guard lock(g_mutex);
    g_kinds.clear();
}

Kind kind_of_component(const uint8_t* component) {
    if (!component || !anymaker::readable(component + off::component_definition, 8)) return Kind::None;
    auto* definition = *reinterpret_cast<const uint8_t* const*>(component + off::component_definition);
    if (!definition) return Kind::None;
    auto found = g_kinds.find(definition);
    if (found != g_kinds.end()) return found->second;
    Kind kind = Kind::None;
    if (anymaker::readable(definition + off::definition_id, sizeof(anymaker::gc_string_view))) {
        auto* id = reinterpret_cast<const anymaker::gc_string_view*>(definition + off::definition_id);
        if (id->data && id->length > 0 && id->length < 64 && anymaker::readable(id->data, size_t(id->length))) kind = kind_of(id->data, size_t(id->length));
    }
    g_kinds[definition] = kind;
    return kind;
}

double cast(void* scene, const Beam& from, const Vec3& dir) {
    Ray ray{{from.start.x, from.start.y, from.start.z}, {dir.x, dir.y, dir.z}};
    double length = kRange, out = kRange;
    g_raycast(&out, static_cast<uint8_t*>(scene) + off::scene_physics, &ray, &length);
    return out;
}

void tick_laser(const uint8_t* component, Part& p, void* scene, double now) {
    Beam b;
    if (p.slots[kLaserOff] >= 0.5) {
        update_laser(p, b);
        g_beams.erase(component);
        return;
    }
    b = beam_from(p.pose, kRange);
    double d = cast(scene, b, p.pose.forward);
    for (const Vec3& feet : g_players) d = block_by_capsule(b.start, p.pose.forward, d, feet - Vec3{0, 1.0, 0}, 0.3, 2.9);
    b = beam_from(p.pose, d);
    update_laser(p, b);
    for (auto& [key, s] : g_parts)
        if (s.kind == Kind::Sensor && now - s.seen < 1.0 && lands_on(b, s.pose)) light_sensor(s, b, now);
    g_beams[component] = {b, now};
    static std::atomic<int> logs{6};
    if (logs.fetch_sub(1) > 0) log(0, "laser at (%.2f, %.2f, %.2f) facing (%.2f, %.2f, %.2f): %s at %.2f m", p.pose.centre.x, p.pose.centre.y, p.pose.centre.z,
                                   p.pose.forward.x, p.pose.forward.y, p.pose.forward.z, b.hit ? "hit" : "nothing", b.distance);
}

void hk_tick(uint8_t* component, void* vehicle, void* scene) {
    reinterpret_cast<tick_t>(h_tick.original)(component, vehicle, scene);
    if (!component || !vehicle || !scene) return;
    std::lock_guard lock(g_mutex);
    Kind kind = kind_of_component(component);
    if (kind == Kind::None) return;
    Mat34 v, c;
    g_vehicle_transform(v.m, static_cast<const uint8_t*>(vehicle));
    g_component_transform(c.m, component);
    Pose pose;
    if (!pose_of(compose(v, c), pose)) return;
    double now = now_seconds();
    Part& p = g_parts[component];
    p.kind = kind; p.pose = pose; p.seen = now;
    if (kind == Kind::Laser) tick_laser(component, p, scene, now);
    else {
        bool was = p.slots[kSensorLit] > 0.5;
        update_sensor(p, now);
        static std::atomic<int> logs{12};
        if (was != (p.slots[kSensorLit] > 0.5) && logs.fetch_sub(1) > 0) log(0, "sensor %s (%.2f m)", was ? "beam broken" : "lit", p.lit_distance);
    }
}

void hk_get_f64(double** ret, uint8_t* component, const anymaker::gc_string_view* name) {
    reinterpret_cast<get_f64_t>(h_get_f64.original)(ret, component, name);
    if (!ret || *ret || !component || !name) return;
    std::lock_guard lock(g_mutex);
    Kind kind = kind_of_component(component);
    if (kind == Kind::None) return;
    auto view = name->view();
    int i = channel_index(kind, view.data(), view.size());
    if (i < 0) return;
    Part& p = g_parts[component];
    p.kind = kind;
    *ret = &p.slots[i];
    static std::atomic<int> logs{8};
    if (logs.fetch_sub(1) > 0) log(0, "%s data channel %s connected", kind == Kind::Laser ? "laser" : "sensor", (kind == Kind::Laser ? kLaserChannels : kSensorChannels)[i].name);
}

// ================================================================== beam drawing (main thread)
using get_camera_t = void (*)(uint8_t* ret, uint8_t* scene);
anymaker::cell_hook h_camera;
std::mutex g_camera_mutex;
Camera g_camera;
double g_camera_time = -1e9;
const AnyGpuDrawV1* gpu;
const AnyUiStateV1* ui;

void hk_get_camera(uint8_t* ret, uint8_t* scene) {
    reinterpret_cast<get_camera_t>(h_camera.original)(ret, scene);
    if (g_stop || !ret || !anymaker::readable(ret, off::cam_size)) return;
    Camera c;   // basis from m_transform: v0 right, v1 up, v2 forward (AnyCombat)
    c.position = rd_vec(ret, off::cam_transform_t);
    c.right = rd_vec(ret, off::cam_transform); c.up = rd_vec(ret, off::cam_transform + 24); c.forward = rd_vec(ret, off::cam_transform + 48);
    c.near_plane = rd<double>(ret, off::cam_near);
    c.tan_half_fov = rd<double>(ret, off::cam_tan_half_fov);
    double fov = rd<double>(ret, off::cam_fov);
    if (!(c.tan_half_fov > 0.01 && c.tan_half_fov < 20) && fov > 0.05 && fov < 3.1) c.tan_half_fov = std::tan(fov / 2);
    if (!c.valid()) return;
    std::lock_guard lock(g_camera_mutex);
    g_camera = c; g_camera_time = now_seconds();
}

void refresh_players(double now) {
    static double last = -1e9;
    if (now - last < 0.05 || !host.copy_players) return;
    last = now;
    static AnySessionPlayersV1 snapshot;   // 64 players with names: too big for the stack of a frame callback
    snapshot = AnySessionPlayersV1{};
    uint32_t flags = 0;
    std::vector<Vec3> players;
    if (host.copy_players(&snapshot, &flags))
        for (uint32_t i = 0; i < snapshot.count && i < ANYAPI_PLAYERS_CAPACITY; ++i)
            if (snapshot.players[i].valid_fields & PLAYER_POSITION) players.push_back({snapshot.players[i].position[0], snapshot.players[i].position[1], snapshot.players[i].position[2]});
    std::lock_guard lock(g_mutex);
    g_players.swap(players);
}

void quad(const Quad& q, uint32_t colour) {
    AnyGpuPointV1 pts[4];
    for (int i = 0; i < 4; ++i) pts[i] = {q.x[i], q.y[i]};
    AnyGpuCommandV1 c; c.kind = ANY_GPU_POLYGON; c.color = colour; c.points = pts; c.point_count = 4;
    gpu->emit(&c);
}
void spot(double x, double y, double r, uint32_t colour) {
    AnyGpuCommandV1 c; c.kind = ANY_GPU_ELLIPSE; c.color = colour;
    c.rect[0] = float(x - r); c.rect[1] = float(y - r); c.rect[2] = float(2 * r); c.rect[3] = float(2 * r);
    gpu->emit(&c);
}

void render(const AnyFrameV1* frame, void*) {
    if (!frame || !gpu || !g_ready) return;
    double now = now_seconds();
    refresh_players(now);
    if (!frame->focused) return;
    AnyUiSnapshotV1 s;
    if (ui && (!ui->copy(&s) || s.kind != ANY_UI_GAMEPLAY)) return;
    Camera cam;
    { std::lock_guard lock(g_camera_mutex); if (now - g_camera_time > 0.25) return; cam = g_camera; }
    std::vector<Beam> beams;
    {
        std::lock_guard lock(g_mutex);
        for (auto& [key, b] : g_beams) if (now - b.seen < kBeamHold) beams.push_back(b.beam);
    }
    const Look& look = kLooks[kLook];
    double w = frame->width, h = frame->height;
    for (const Beam& b : beams) {
        ScreenPoint a, e;
        if (!project_segment(cam, b.start, b.end, w, h, a, e)) continue;
        auto half = [&](double world, double depth, float min_px) { return std::max(double(min_px), pixels(cam, world, depth, h)) * 0.5; };
        Quad q;
        if (beam_quad(a, e, half(look.glow_m, a.depth, look.min_glow_px), half(look.glow_m, e.depth, look.min_glow_px), q)) quad(q, look.glow);
        if (beam_quad(a, e, half(look.core_m, a.depth, look.min_core_px), half(look.core_m, e.depth, look.min_core_px), q)) quad(q, look.core);
        if (b.hit && dot(b.end - cam.position, cam.forward) > cam.near_plane) {   // the spot where it lands
            double r = std::max(2.5, pixels(cam, look.glow_m, e.depth, h) * 0.8);
            spot(e.x, e.y, r * 1.8, look.glow);
            spot(e.x, e.y, r * 0.6, look.core);
        }
    }
}

// ================================================================== install (background thread)
void remove_hooks();
void install_hooks() {
    using namespace anymaker::experimental;
    for (int attempt = 0; attempt < 180 && !g_stop; ++attempt) {
        if (attempt) Sleep(1000);
        if (!matching_build()) { if (attempt == 0) log(1, "game build does not match the SDK reference; AnyLaser stays off"); return; }
        int roots = -1;
        void** add = route::cell(bind::definitions_add_definitions, &roots);
        void** tick_cell = route::cell(bind::server_compass_tick);
        void** get_cell = route::cell(bind::server_compass_get_data_f64);
        auto ctor = reinterpret_cast<file_ctor_t>(route::function(bind::definition_file_ctor));
        auto dtor = reinterpret_cast<file_ctor_t>(route::function(bind::definition_file_dtor));
        auto load = reinterpret_cast<file_load_t>(route::function(bind::definition_file_load));
        auto vt = reinterpret_cast<transform_t>(route::function(bind::server_vehicle_get_transform));
        auto ct = reinterpret_cast<transform_t>(route::function(bind::server_component_get_transform));
        auto ray = reinterpret_cast<raycast_t>(route::function(bind::raycast_distance));
        auto sc = reinterpret_cast<string_ctor_t>(native(anymaker::sym::string_ctor_cstr_rva));
        if (add && !route::add_cell_ok(add)) {
            static int logs = 3;
            if (logs-- > 0) log(1, "the definitions route led to %s, not _add_definitions; looking again", route::what_is(anymaker::readable(add, 8) ? *add : nullptr));
            add = nullptr;
        }
        if (load && !route::file_load_ok((void*)load)) {
            static int logs = 3;
            if (logs-- > 0) log(1, "the file load route led to %s, not vehicle_component_definition_file.load; looking again", route::what_is((void*)load));
            load = nullptr;
        }
        if (!add || !tick_cell || !get_cell || !ctor || !dtor || !load || !vt || !ct || !ray || !sc) {
            if (attempt == 20 || attempt == 179)
                log(attempt == 179 ? 2 : 1, "%s the game functions (route root matches %d, add=%d tick=%d get=%d file ctor=%d dtor=%d load=%d transforms=%d/%d raycast=%d string=%d)",
                    attempt == 179 ? "could not locate" : "still looking for", roots, !!add, !!tick_cell, !!get_cell, !!ctor, !!dtor, !!load, !!vt, !!ct, !!ray, !!sc);
            continue;
        }
        if (!write_definition_file()) { log(2, "could not write the AnyLaser definition file; AnyLaser is off"); return; }
        g_file_ctor = ctor; g_file_dtor = dtor; g_file_load = load; g_vehicle_transform = vt; g_component_transform = ct; g_raycast = ray; g_string_ctor = sc;
        g_get_definition = reinterpret_cast<get_definition_t>(route::function(bind::definitions_get_by_id));
        if (!g_get_definition) log(1, "definition lookup not found; another mod re-adding definitions may add the laser parts twice");
        if (!route::add_cell_ok(add)) { log(2, "the definitions hook cell changed to %s before hooking; AnyLaser is off", route::what_is(*add)); return; }
        bool ok = h_tick.install(tick_cell, (void*)&hk_tick) && h_get_f64.install(get_cell, (void*)&hk_get_f64) &&
                  h_add_definitions.install(add, (void*)&hk_add_definitions);
        if (!ok) { log(2, "a hook cell was already taken or not writable; AnyLaser is off"); remove_hooks(); return; }
        void** cam_cell = route::cell(bind::client_scene_get_camera);
        if (!gpu) log(1, "beams are invisible (anyapi.gpu_draw missing); the parts and data still work");
        else if (!cam_cell || !h_camera.install(cam_cell, (void*)&hk_get_camera)) log(1, "beams are invisible (camera hook not found); the parts and data still work");
        g_ready = true;
        if (roots > 1) log(0, "the definition functions' route root matched %d times; all matches agreed", roots);
        log(0, "Ready after %d s (look %d). Load or start a world to add the Laser Block and Laser Sensor (definition file %s).", attempt, kLook + 1, g_json_path.c_str());
        return;
    }
}
void remove_hooks() {
    h_camera.remove((void*)&hk_get_camera);
    h_add_definitions.remove((void*)&hk_add_definitions);
    h_get_f64.remove((void*)&hk_get_f64);
    h_tick.remove((void*)&hk_tick);
}
}  // namespace
}  // namespace anylaser

using namespace anylaser;
extern "C" __declspec(dllexport) bool AnyAPI_ModInit(const AnyModHostV1* h, AnyModCallbacksV1* out) {
    if (!h || !out || h->struct_size != sizeof(*h) || h->abi != ANYAPI_MOD_ABI || out->struct_size != sizeof(*out)) return false;
    host = *h; out->id = kModId;
    out->shutdown = [](void*) { g_stop = true; remove_hooks(); };
    return true;
}
extern "C" __declspec(dllexport) void AnyAPI_ModReady() {
    if (auto services = AnyAPI_Services()) {
        auto* u = static_cast<const AnyUiStateV1*>(services->query("anyapi.ui_state", 1));
        if (u && u->struct_size == sizeof(*u) && u->version == 1 && u->copy) ui = u;
        auto* g = static_cast<const AnyGpuDrawV1*>(services->query("anyapi.gpu_draw", 1));
        if (g && g->struct_size == sizeof(*g) && g->version == 1 && g->emit && g->register_renderer && g->register_renderer(render, nullptr)) gpu = g;
    }
    std::thread(install_hooks).detach();   // resolving scans game memory; keep it off the loader and frame threads
}
